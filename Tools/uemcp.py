"""Minimal MCP (Streamable HTTP) client for the Unreal editor MCP server at 127.0.0.1:8000/mcp.

Usage:
  python uemcp.py list                       # list tools (name + first line of description)
  python uemcp.py desc <tool>                # full description + input schema of one tool
  python uemcp.py call <tool> '<json args>'  # call a tool with JSON args (or @file.json)
"""
import json
import sys
import urllib.request
import os

URL = "http://127.0.0.1:8000/mcp"
SESSION_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".uemcp_session")
_next_id = [1]


def _post(payload, session_id=None):
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(URL, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json, text/event-stream")
    if session_id:
        req.add_header("Mcp-Session-Id", session_id)
    with urllib.request.urlopen(req, timeout=600) as resp:
        sid = resp.headers.get("Mcp-Session-Id")
        body = resp.read().decode("utf-8", errors="replace")
        ctype = resp.headers.get("Content-Type", "")
    if "text/event-stream" in ctype:
        # take the last data: line
        msgs = [l[5:].strip() for l in body.splitlines() if l.startswith("data:")]
        body = msgs[-1] if msgs else "{}"
    return sid, (json.loads(body) if body.strip() else None)


def _rpc(method, params=None, session_id=None, notify=False):
    payload = {"jsonrpc": "2.0", "method": method}
    if params is not None:
        payload["params"] = params
    if not notify:
        payload["id"] = _next_id[0]
        _next_id[0] += 1
    return _post(payload, session_id)


def connect():
    sid, res = _rpc("initialize", {
        "protocolVersion": "2025-03-26",
        "capabilities": {},
        "clientInfo": {"name": "claude-uemcp", "version": "0.1"},
    })
    try:
        _rpc("notifications/initialized", {}, sid, notify=True)
    except Exception:
        pass
    return sid


def list_tools(sid):
    tools = []
    cursor = None
    while True:
        params = {"cursor": cursor} if cursor else {}
        _, res = _rpc("tools/list", params, sid)
        result = res.get("result", {})
        tools.extend(result.get("tools", []))
        cursor = result.get("nextCursor")
        if not cursor:
            break
    return tools


def call_tool(sid, name, args):
    _, res = _rpc("tools/call", {"name": name, "arguments": args}, sid)
    return res


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    sid = connect()
    if cmd == "list":
        tools = list_tools(sid)
        print(f"{len(tools)} tools")
        for t in tools:
            desc = (t.get("description") or "").strip().splitlines()
            print(f"- {t['name']}: {desc[0] if desc else ''}")
        return 0
    if cmd == "desc":
        tools = {t["name"]: t for t in list_tools(sid)}
        for name in sys.argv[2:]:
            t = tools.get(name)
            if not t:
                print(f"## {name}: NOT FOUND")
                continue
            print(f"## {name}\n{t.get('description','')}\nschema: {json.dumps(t.get('inputSchema', {}), ensure_ascii=False)}\n")
        return 0
    if cmd == "call":
        name = sys.argv[2]
        raw = sys.argv[3] if len(sys.argv) > 3 else "{}"
        if raw.startswith("@"):
            with open(raw[1:], "r", encoding="utf-8") as f:
                raw = f.read()
        args = json.loads(raw)
        res = call_tool(sid, name, args)
        if res is None:
            print("(no response)")
            return 1
        if "error" in res:
            print("RPC ERROR:", json.dumps(res["error"], ensure_ascii=False, indent=1))
            return 1
        result = res.get("result", {})
        for c in result.get("content", []):
            if c.get("type") == "text":
                print(c["text"])
            elif c.get("type") == "image":
                import base64, time as _t
                fn = f"capture_{int(_t.time())}.{ 'png' if 'png' in c.get('mimeType','') else 'jpg'}"
                open(fn, "wb").write(base64.b64decode(c["data"]))
                print("IMAGE SAVED:", fn, c.get("mimeType"))
            else:
                print(json.dumps(c, ensure_ascii=False)[:500])
        if result.get("isError"):
            print("[isError=true]")
            return 1
        sc = result.get("structuredContent")
        if sc:
            print("structured:", json.dumps(sc, ensure_ascii=False, indent=1))
        return 0
    print("unknown command")
    return 2


if __name__ == "__main__":
    sys.exit(main())

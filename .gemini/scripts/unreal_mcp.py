import sys
import json
import urllib.request
import urllib.error

DEFAULT_URL = "http://127.0.0.1:8000/mcp"

class UnrealMcpClient:
    def __init__(self, url=DEFAULT_URL):
        self.url = url
        self.session_id = None
        self.request_id = 0

    def _post(self, payload):
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(self.url, data=data, headers={"Content-Type": "application/json"})
        if self.session_id:
            req.add_header("Mcp-Session-Id", self.session_id)

        with urllib.request.urlopen(req) as resp:
            session_id = resp.headers.get("Mcp-Session-Id")
            if session_id:
                self.session_id = session_id
            body = resp.read().decode("utf-8")
            return json.loads(body) if body.strip() else None

    def initialize(self):
        self.request_id += 1
        res = self._post({
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "Antigravity", "version": "1.0.0"}
            }
        })
        self._post({
            "jsonrpc": "2.0",
            "method": "notifications/initialized"
        })
        return res

    def call_raw(self, name, arguments=None):
        self.request_id += 1
        return self._post({
            "jsonrpc": "2.0",
            "id": self.request_id,
            "method": "tools/call",
            "params": {
                "name": name,
                "arguments": arguments or {}
            }
        })

    def list_toolsets(self):
        res = self.call_raw("list_toolsets")
        return [c["text"] for c in res.get("result", {}).get("content", [])]

    def describe_toolset(self, toolset_name):
        res = self.call_raw("describe_toolset", {"toolset_name": toolset_name})
        return [c["text"] for c in res.get("result", {}).get("content", [])]

    def call_tool(self, toolset_name, tool_name, arguments=None):
        args = {
            "tool_name": tool_name,
            "arguments": arguments or {}
        }
        if toolset_name:
            args["toolset_name"] = toolset_name
        res = self.call_raw("call_tool", args)
        return res.get("result", {}).get("content", [])

if __name__ == "__main__":
    client = UnrealMcpClient()
    client.initialize()

    if len(sys.argv) < 2:
        print("Usage:")
        print("  python unreal_mcp.py list-toolsets")
        print("  python unreal_mcp.py describe <toolset_name>")
        print("  python unreal_mcp.py call <toolset_name> <tool_name> [json_args]")
        sys.exit(0)

    cmd = sys.argv[1]
    if cmd == "list-toolsets":
        for t in client.list_toolsets():
            print(t)
    elif cmd == "describe" and len(sys.argv) >= 3:
        for t in client.describe_toolset(sys.argv[2]):
            print(t)
    elif cmd == "call" and len(sys.argv) >= 4:
        toolset = sys.argv[2]
        tool = sys.argv[3]
        args = json.loads(sys.argv[4]) if len(sys.argv) > 4 else {}
        for content in client.call_tool(toolset, tool, args):
            print(content.get("text", ""))

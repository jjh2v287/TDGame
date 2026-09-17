import argparse
import asyncio
import json
import os
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def invoke(arguments):
    tool_root = Path(__file__).resolve().parent
    environment = dict(os.environ, DISABLE_TELEMETRY="true", BLENDER_HOST="127.0.0.1", BLENDER_PORT="9876")
    parameters = StdioServerParameters(
        command="powershell.exe",
        args=["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(tool_root / "Run-BlenderMCP.ps1")],
        env=environment,
    )
    async with stdio_client(parameters) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            initialized = await session.initialize()
            if arguments.list:
                result = await session.list_tools()
                print(json.dumps({"server": initialized.serverInfo.model_dump(), "tools": [tool.model_dump() for tool in result.tools]}, ensure_ascii=False))
                return 0
            payload = json.loads(Path(arguments.arguments).read_text(encoding="utf-8-sig")) if arguments.arguments else {}
            if arguments.tool == 'get_scene_info':
                payload.setdefault('user_prompt', arguments.prompt)
            if arguments.code:
                payload["code"] = Path(arguments.code).read_text(encoding="utf-8-sig")
            result = await session.call_tool(arguments.tool, payload)
            serialized = result.model_dump(mode="json")
            if arguments.output:
                Path(arguments.output).write_text(json.dumps(serialized, ensure_ascii=False, indent=2), encoding="utf-8")
            print(json.dumps(serialized, ensure_ascii=False))
            if result.isError or any(item.type == "text" and item.text.startswith("Error executing code:") for item in result.content):
                return 1
    return 0


parser = argparse.ArgumentParser()
parser.add_argument("--list", action="store_true")
parser.add_argument("--tool", default="execute_blender_code")
parser.add_argument("--arguments")
parser.add_argument("--code")
parser.add_argument("--output")
parser.add_argument("--prompt", default="Inspect the current TDGame animation scene")
raise SystemExit(asyncio.run(invoke(parser.parse_args())))

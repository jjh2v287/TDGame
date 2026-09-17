import importlib.util
import os
from pathlib import Path
import sys

import bpy

os.environ["DISABLE_TELEMETRY"] = "true"
tool_root = Path(__file__).resolve().parent
addon_path = tool_root / ".runtime" / "blender-mcp-7684c6b3ad2aa0710bbdb1cb06b497c90899ae00" / "addon.py"
addon_spec = importlib.util.spec_from_file_location("td_blender_mcp_upstream", addon_path)
addon_module = importlib.util.module_from_spec(addon_spec)
sys.modules[addon_spec.name] = addon_module
addon_spec.loader.exec_module(addon_module)
addon_module.register()

for scene in bpy.data.scenes:
    scene.blendermcp_auto_start_server = False
    scene.blendermcp_port = 9876
    for integration in ("polyhaven", "hyper3d", "sketchfab", "polypizza", "hunyuan3d"):
        setattr(scene, "blendermcp_use_" + integration, False)
    scene["TD_MCP_PROJECT"] = str(tool_root.parents[1])

server = addon_module.BlenderMCPServer(host="127.0.0.1", port=9876)
bpy.types.blendermcp_server = server
server.start()
bpy.context.scene.blendermcp_server_running = server.running
print("TD_BLENDER_MCP_READY", bpy.app.version_string, flush=True)

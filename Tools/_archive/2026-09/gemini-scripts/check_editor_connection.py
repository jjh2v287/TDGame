import os
import sys
sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def check_connection():
    try:
        client = UnrealMcpClient()
        res = client.initialize()
        session_id = client.session_id
        current_level = client.call_tool("editor_toolset.toolsets.scene.SceneTools", "get_current_level", {})
        level_name = current_level[0].get("text", "")
        print(f"[SUCCESS] Editor Connected! (Session: {session_id})")
        print(f"[CURRENT LEVEL] {level_name}")
        return True
    except Exception as e:
        print(f"[WAITING] Editor not detected or starting up... ({e})")
        return False

if __name__ == "__main__":
    check_connection()

# File: .gemini/scripts/setup_real_landscape.py
import os
import sys
import json

sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def setup_landscape_and_clean_floor():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/3] Connected to Unreal Editor MCP.")

    # 1. Check current actors in level
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    
    res = client.call_tool("editor_toolset.toolsets.scene.SceneTools", "find_actors", {
        "name": "", "tag": "", "collision_channels": []
    })
    actors = json.loads(res[0]["text"])["returnValue"]

    # 2. Check if a real Landscape actor is present
    landscapes = [a for a in actors if "Landscape" in a["refPath"] and "Gizmo" not in a["refPath"] and "Placeholder" not in a["refPath"]]
    floor_actor = next((a for a in actors if "Floor" in a["refPath"]), None)

    print(f"[2/3] Status: {len(landscapes)} Landscape actor(s) found.")

    if landscapes:
        print(f"  - Real Landscape detected: {landscapes[0]['refPath']}")
        # Remove old static mesh floor if real landscape exists
        if floor_actor:
            print("  - Removing old static mesh Floor actor...")
            client.call_tool("editor_toolset.toolsets.scene.SceneTools", "remove_from_scene", {"actor": floor_actor})
            print("  - Old Floor removed successfully!")
    else:
        print("  - No real Landscape found yet.")
        print("  - Temporary Floor actor kept active until Landscape is created via Shift+2 in Editor.")

    # 3. Save Assets
    print("[3/3] Saving level assets...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    print("[DONE] Level state verified.")

if __name__ == "__main__":
    setup_landscape_and_clean_floor()

# File: .gemini/scripts/populate_outer_wilderness.py
import os
import sys
import json
import math
import random

sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient
from take_screenshot import capture_rect_from_desktop, find_unreal_window, switch_to_default_desktop

def populate_outer_wilderness():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/5] Connected to Unreal MCP.")

    wilderness_script = '''
import json
import math

def run():
    trees = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch3"
    ]
    cliffs = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff4",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3"
    ]
    bushes = ["/Game/DarkFantasyTopDown/StaticMeshes/Bush/SM_Bush"]
    carpets = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Grass/SM_GrassPlane",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_BirchLavesPlane"
    ]

    total = 0

    # -------------------------------------------------------------
    # 1. 20 Dense Forest Copses (Radius 25,000 ~ 75,000)
    # -------------------------------------------------------------
    for g in range(20):
        angle = (2.0 * math.pi / 20) * g + (g * 0.31)
        dist = 28000.0 + ((g * 1741) % 45000)
        gx = dist * math.cos(angle)
        gy = dist * math.sin(angle)

        # 6-10 tightly clustered Birch trees per copse
        num_trees = 6 + (g % 5)
        for t in range(num_trees):
            t_angle = (2.0 * math.pi / num_trees) * t + (t * 0.4)
            t_dist = 180.0 + (t * 160.0)
            tx = gx + t_dist * math.cos(t_angle)
            ty = gy + t_dist * math.sin(t_angle)
            t_mesh = trees[(g + t) % len(trees)]
            t_yaw = float((g * 61 + t * 89) % 360)
            t_scale = 1.6 + ((g + t) % 4) * 0.3

            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": t_mesh,
                "name": f"Grove_Tree_{g}_{t}",
                "xform": {
                    "location": {"x": tx, "y": ty, "z": 300.0},
                    "rotation": {"pitch": 0.0, "yaw": t_yaw, "roll": 0.0},
                    "scale": {"x": t_scale, "y": t_scale, "z": t_scale}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": "Foliage/Wild_Forest_Groves"
            }))
            total += 1

        # 2-3 bushes under the grove
        for b in range(2):
            bx = gx + (250.0 if b == 0 else -250.0)
            by = gy + (-200.0 if b == 0 else 220.0)
            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": bushes[0],
                "name": f"Grove_Bush_{g}_{b}",
                "xform": {
                    "location": {"x": bx, "y": by, "z": 300.0},
                    "rotation": {"pitch": 0.0, "yaw": float((g * 37 + b * 73) % 360), "roll": 0.0},
                    "scale": {"x": 1.6, "y": 1.6, "z": 1.6}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": "Foliage/Wild_Forest_Groves"
            }))
            total += 1

        # 1-2 Ground leaf carpets under the grove
        actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": carpets[g % len(carpets)],
            "name": f"Grove_Carpet_{g}",
            "xform": {
                "location": {"x": gx, "y": gy, "z": 300.0},
                "rotation": {"pitch": 0.0, "yaw": float((g * 49) % 360), "roll": 0.0},
                "scale": {"x": 3.5, "y": 3.5, "z": 3.5}
            },
            "snap_to_ground": True
        }))["returnValue"]
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
            "actor": actor,
            "folder_path": "Foliage/Wild_Forest_Groves"
        }))
        total += 1

    # -------------------------------------------------------------
    # 2. 16 Cliff Formations & Mega-Boulders (Radius 35,000 ~ 95,000)
    # -------------------------------------------------------------
    for f in range(16):
        angle = (2.0 * math.pi / 16) * f + 0.15
        dist = 38000.0 + ((f * 2113) % 55000)
        fx = dist * math.cos(angle)
        fy = dist * math.sin(angle)

        c_mesh = cliffs[f % len(cliffs)]
        c_yaw = float((f * 43) % 360)
        c_scale = 2.2 + ((f % 3) * 0.6)

        actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": c_mesh,
            "name": f"Wild_Cliff_{f}",
            "xform": {
                "location": {"x": fx, "y": fy, "z": 400.0},
                "rotation": {"pitch": 0.0, "yaw": c_yaw, "roll": 0.0},
                "scale": {"x": c_scale, "y": c_scale, "z": c_scale}
            },
            "snap_to_ground": True
        }))["returnValue"]
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
            "actor": actor,
            "folder_path": "Foliage/Wild_Cliffs"
        }))
        total += 1

    return {"spawned_wilderness": total}
'''

    print("[2/5] Spawning outer wilderness groves and cliff formations...")
    res = client.call_tool("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "execute_tool_script", {
        "script": wilderness_script
    })
    print("  - Wilderness result:", res[0]["text"] if res else "None")

    # Save
    print("[3/5] Saving external actors...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "save_current_level", {})

    print("[4/5] Synchronizing to L_TDWorld_Main...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "duplicate", {
        "source_asset": "/Game/Level/LV_DarkFantasy_OpenWorld",
        "destination_path": "/Game/World/Maps",
        "new_name": "L_TDWorld_Main"
    })

    # Screenshot
    print("[5/5] Capturing screenshot...")
    switch_to_default_desktop()
    hwnd, _ = find_unreal_window()
    if hwnd:
        import ctypes
        from ctypes import wintypes
        rect = wintypes.RECT()
        ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
        capture_rect_from_desktop(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, "C:/Project/TDGame/editor_screenshot.png")
    print("[SUCCESS] Outer wilderness population complete!")

if __name__ == "__main__":
    populate_outer_wilderness()

# File: .gemini/scripts/populate_dense_nature.py
import os
import sys
import json
import math
import random
import time

sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient
from take_screenshot import capture_rect_from_desktop, find_unreal_window, switch_to_default_desktop

def populate_dense_nature():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/6] Connected to Unreal MCP.")

    # 1. Level check
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/6] Loaded level: {level_path}")

    # 2. Batch 1: Ground Carpets (Grass Planes, Leaves Planes, Rocks Tiles)
    print("[3/6] Generating Ground Carpets (Grass, Leaves, Pebbles)...")
    carpet_script = '''
import json
import math

def run():
    grass_plane = "/Game/DarkFantasyTopDown/StaticMeshes/Grass/SM_GrassPlane"
    leaves_plane = "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_BirchLavesPlane"
    rocks_tile = "/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile"

    total = 0
    # Central and village surroundings (-25,000 to +25,000)
    # 14x14 grid with jitter
    steps = [i * 3500.0 - 22750.0 for i in range(14)]
    count = 0
    for gx in steps:
        for gy in steps:
            count += 1
            # Skip dead center where buildings are
            dist_center = math.sqrt(gx * gx + gy * gy)
            if dist_center < 1800.0:
                continue

            jx = gx + ((count * 617) % 1200 - 600)
            jy = gy + ((count * 743) % 1200 - 600)
            yaw = float((count * 47) % 360)
            scale = 2.8 + ((count % 5) * 0.4)

            # Alternate between GrassPlane and LeavesPlane
            if count % 3 == 0:
                asset = leaves_plane
                folder = "Foliage/Carpets_Leaves"
                name = f"Carpet_Leaves_{count}"
            elif count % 3 == 1:
                asset = grass_plane
                folder = "Foliage/Carpets_Grass"
                name = f"Carpet_Grass_{count}"
            else:
                asset = rocks_tile
                folder = "Foliage/Carpets_Rocks"
                name = f"Carpet_Rocks_{count}"
                scale *= 0.8

            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": asset,
                "name": name,
                "xform": {
                    "location": {"x": jx, "y": jy, "z": 200.0},
                    "rotation": {"pitch": 0.0, "yaw": yaw, "roll": 0.0},
                    "scale": {"x": scale, "y": scale, "z": scale}
                },
                "snap_to_ground": True
            }))["returnValue"]

            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": folder
            }))
            total += 1

    return {"spawned_carpets": total}
'''
    res1 = client.call_tool("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "execute_tool_script", {
        "script": carpet_script
    })
    print("  - Carpets result:", res1[0]["text"] if res1 else "None")

    # 3. Batch 2: Dense Rock Formations & Pebble Clusters
    print("[4/6] Generating Dense Rock Formations...")
    rock_script = '''
import json
import math

def run():
    big_rocks = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3"
    ]
    small_rocks = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rocks",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_3",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_4",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_5",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_6",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_7",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_8"
    ]

    total = 0
    # 40 rock clusters distributed around the village and paths
    for c in range(40):
        angle = (2.0 * math.pi / 40) * c + (c * 0.17)
        dist = 2200.0 + ((c * 941) % 22000)
        cx = dist * math.cos(angle)
        cy = dist * math.sin(angle)

        # 1 Big rock anchor
        b_mesh = big_rocks[c % len(big_rocks)]
        b_yaw = float((c * 53) % 360)
        b_scale = 1.4 + ((c % 4) * 0.3)
        actor_b = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": b_mesh,
            "name": f"Rock_Anchor_{c}",
            "xform": {
                "location": {"x": cx, "y": cy, "z": 200.0},
                "rotation": {"pitch": 0.0, "yaw": b_yaw, "roll": 0.0},
                "scale": {"x": b_scale, "y": b_scale, "z": b_scale}
            },
            "snap_to_ground": True
        }))["returnValue"]
        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
            "actor": actor_b,
            "folder_path": "Foliage/Rock_Clusters"
        }))
        total += 1

        # 3-5 Small rocks scattered around the anchor
        num_small = 3 + (c % 3)
        for s in range(num_small):
            s_angle = (2.0 * math.pi / num_small) * s + 0.3
            s_dist = 120.0 + (s * 80.0)
            sx = cx + s_dist * math.cos(s_angle)
            sy = cy + s_dist * math.sin(s_angle)
            s_mesh = small_rocks[(c * 3 + s) % len(small_rocks)]
            s_yaw = float((c * 71 + s * 113) % 360)
            s_scale = 1.0 + ((s % 3) * 0.4)
            actor_s = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": s_mesh,
                "name": f"Rock_Small_{c}_{s}",
                "xform": {
                    "location": {"x": sx, "y": sy, "z": 200.0},
                    "rotation": {"pitch": 0.0, "yaw": s_yaw, "roll": 0.0},
                    "scale": {"x": s_scale, "y": s_scale, "z": s_scale}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor_s,
                "folder_path": "Foliage/Rock_Clusters"
            }))
            total += 1

    return {"spawned_rocks": total}
'''
    res2 = client.call_tool("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "execute_tool_script", {
        "script": rock_script
    })
    print("  - Rocks result:", res2[0]["text"] if res2 else "None")

    # 4. Batch 3: High-Density Bushes, Grass Tufts, Dreadplants & Mushrooms
    print("[5/6] Generating High-Density Flora & Thickets...")
    flora_script = '''
import json
import math

def run():
    bushes = ["/Game/DarkFantasyTopDown/StaticMeshes/Bush/SM_Bush"]
    grasses = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass_VarB"
    ]
    dreadplants = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1"
    ]
    props = [
        "/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_Bone",
        "/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_Skull",
        "/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_StickedSkull",
        "/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_RamSkull",
        "/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_WoodenStick"
    ]

    total = 0
    # 50 dense thicket clusters
    for t in range(50):
        angle = (2.0 * math.pi / 50) * t + (t * 0.23)
        dist = 1800.0 + ((t * 887) % 24000)
        cx = dist * math.cos(angle)
        cy = dist * math.sin(angle)

        # 2-3 overlapping bushes
        for b in range(2):
            bx = cx + (80.0 if b == 0 else -100.0)
            by = cy + (-90.0 if b == 0 else 80.0)
            b_scale = 1.3 + (b * 0.3)
            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": bushes[0],
                "name": f"Thicket_Bush_{t}_{b}",
                "xform": {
                    "location": {"x": bx, "y": by, "z": 200.0},
                    "rotation": {"pitch": 0.0, "yaw": float((t * 43 + b * 97) % 360), "roll": 0.0},
                    "scale": {"x": b_scale, "y": b_scale, "z": b_scale}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": "Foliage/Thickets_Bushes"
            }))
            total += 1

        # 2 grass tufts
        for g in range(2):
            gx = cx + (150.0 if g == 0 else -160.0)
            gy = cy + (120.0 if g == 0 else -140.0)
            g_mesh = grasses[(t + g) % len(grasses)]
            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": g_mesh,
                "name": f"Thicket_Grass_{t}_{g}",
                "xform": {
                    "location": {"x": gx, "y": gy, "z": 200.0},
                    "rotation": {"pitch": 0.0, "yaw": float((t * 67 + g * 53) % 360), "roll": 0.0},
                    "scale": {"x": 1.6, "y": 1.6, "z": 1.6}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": "Foliage/Thickets_Grass"
            }))
            total += 1

        # 2 dreadplants or mushrooms
        for d in range(2):
            dx = cx + (200.0 if d == 0 else -210.0)
            dy = cy + (-180.0 if d == 0 else 190.0)
            d_mesh = dreadplants[(t * 2 + d) % len(dreadplants)]
            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": d_mesh,
                "name": f"Thicket_Flora_{t}_{d}",
                "xform": {
                    "location": {"x": dx, "y": dy, "z": 200.0},
                    "rotation": {"pitch": 0.0, "yaw": float((t * 79 + d * 31) % 360), "roll": 0.0},
                    "scale": {"x": 1.5, "y": 1.5, "z": 1.5}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": "Foliage/Thickets_Flora"
            }))
            total += 1

        # 1 prop (skull/bone/stick) every other thicket
        if t % 2 == 0:
            p_mesh = props[(t // 2) % len(props)]
            actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": p_mesh,
                "name": f"Thicket_Prop_{t}",
                "xform": {
                    "location": {"x": cx + 50.0, "y": cy + 50.0, "z": 200.0},
                    "rotation": {"pitch": 0.0, "yaw": float((t * 83) % 360), "roll": 0.0},
                    "scale": {"x": 1.4, "y": 1.4, "z": 1.4}
                },
                "snap_to_ground": True
            }))["returnValue"]
            execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
                "actor": actor,
                "folder_path": "Foliage/DarkFantasy_Props"
            }))
            total += 1

    return {"spawned_flora": total}
'''
    res3 = client.call_tool("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "execute_tool_script", {
        "script": flora_script
    })
    print("  - Flora result:", res3[0]["text"] if res3 else "None")

    # 5. Save assets and duplicate
    print("[6/6] Saving all external actors and synchronizing to L_TDWorld_Main...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "save_current_level", {})
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "duplicate", {
        "source_asset": "/Game/Level/LV_DarkFantasy_OpenWorld",
        "destination_path": "/Game/World/Maps",
        "new_name": "L_TDWorld_Main"
    })
    print("  - Saved and duplicated successfully!")

    # 6. Capture screenshot
    switch_to_default_desktop()
    hwnd, _ = find_unreal_window()
    if hwnd:
        import ctypes
        from ctypes import wintypes
        rect = wintypes.RECT()
        ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
        capture_rect_from_desktop(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, "C:/Project/TDGame/editor_screenshot.png")
    print("[DONE] Dense nature population complete!")

if __name__ == "__main__":
    populate_dense_nature()

# File: .gemini/scripts/populate_foliage.py
import os
import sys
import json
import math
import random

sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient
from take_screenshot import capture_rect_from_desktop, find_unreal_window, switch_to_default_desktop

def populate_foliage():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/5] Connected to Unreal MCP.")

    # 1. Level check
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/5] Active level: {level_path}")

    # 2. Vegetation generation script executed inside Unreal
    print("[3/5] Spawning foliage clusters across Landscape...")
    foliage_script = '''
import json
import math

def run():
    trees = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch3"
    ]
    bushes = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Bush/SM_Bush"
    ]
    dreadplants = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1"
    ]
    grasses = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass_VarB",
        "/Game/DarkFantasyTopDown/StaticMeshes/Grass/SM_GrassPlane"
    ]

    total_spawned = 0

    def spawn_foliage(asset, name, loc, rot, scale, folder):
        actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": asset,
            "name": name,
            "xform": {
                "location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])},
                "rotation": {"pitch": float(rot[0]), "yaw": float(rot[1]), "roll": float(rot[2])},
                "scale": {"x": float(scale[0]), "y": float(scale[1]), "z": float(scale[2])}
            },
            "snap_to_ground": True
        }))["returnValue"]

        execute_tool("editor_toolset.toolsets.scene.SceneTools.set_actor_folder", json.dumps({
            "actor": actor,
            "folder_path": folder
        }))
        return actor

    # -------------------------------------------------------------
    # ZONE 1: Village Surroundings & Roadsides (Radius 2,000 ~ 12,000)
    # -------------------------------------------------------------
    # 18 near clusters right outside village fence and along roads
    for i in range(18):
        angle = (2.0 * math.pi / 18) * i + (i * 0.13)
        dist = 2400.0 + ((i * 733) % 7000)
        cx = dist * math.cos(angle)
        cy = dist * math.sin(angle)

        # 2-4 Birch trees per cluster
        num_trees = 2 + (i % 3)
        for t in range(num_trees):
            t_angle = angle + (t * 0.7)
            t_dist = 250.0 + (t * 300.0)
            tx = cx + t_dist * math.cos(t_angle)
            ty = cy + t_dist * math.sin(t_angle)
            t_yaw = float((i * 47 + t * 93) % 360)
            t_s = 1.3 + ((i + t) % 4) * 0.25
            t_mesh = trees[(i + t) % len(trees)]
            spawn_foliage(t_mesh, f"Near_Tree_{i}_{t}", (tx, ty, 50.0), (0, t_yaw, 0), (t_s, t_s, t_s), "Foliage/Near_Trees")
            total_spawned += 1

        # 2 Bushes per cluster
        for b in range(2):
            bx = cx + (300.0 if b == 0 else -350.0)
            by = cy + (-250.0 if b == 0 else 300.0)
            b_yaw = float((i * 31 + b * 117) % 360)
            b_s = 1.2 + (b * 0.3)
            spawn_foliage(bushes[0], f"Near_Bush_{i}_{b}", (bx, by, 50.0), (0, b_yaw, 0), (b_s, b_s, b_s), "Foliage/Near_Bushes")
            total_spawned += 1

        # 2 Mushrooms/Dreadplants per cluster
        for d in range(2):
            dx = cx + (150.0 if d == 0 else -180.0)
            dy = cy + (180.0 if d == 0 else -150.0)
            d_mesh = dreadplants[(i * 2 + d) % len(dreadplants)]
            d_yaw = float((i * 61 + d * 43) % 360)
            d_s = 1.4 + (d * 0.4)
            spawn_foliage(d_mesh, f"Near_Flora_{i}_{d}", (dx, dy, 50.0), (0, d_yaw, 0), (d_s, d_s, d_s), "Foliage/Near_Flora")
            total_spawned += 1

        # 2 Grass patches
        for g in range(2):
            gx = cx + (450.0 if g == 0 else -400.0)
            gy = cy + (200.0 if g == 0 else -250.0)
            g_mesh = grasses[(i + g) % len(grasses)]
            g_yaw = float((i * 79 + g * 53) % 360)
            g_s = 1.5 + (g * 0.5)
            spawn_foliage(g_mesh, f"Near_Grass_{i}_{g}", (gx, gy, 50.0), (0, g_yaw, 0), (g_s, g_s, g_s), "Foliage/Near_Grass")
            total_spawned += 1

    # -------------------------------------------------------------
    # ZONE 2: Intermediate Woods & Groves (Radius 12,000 ~ 65,000)
    # -------------------------------------------------------------
    # 24 groves distributed across the inner open world
    grid_steps = [-48000.0, -28000.0, 28000.0, 48000.0]
    g_count = 0
    for gx in grid_steps:
        for gy in grid_steps:
            g_count += 1
            cx = gx + ((g_count * 137) % 6000 - 3000)
            cy = gy + ((g_count * 191) % 6000 - 3000)

            # 4 Birch trees
            for t in range(4):
                tx = cx + ((t * 600) - 900)
                ty = cy + ((t * 450) % 800 - 400)
                t_yaw = float((g_count * 29 + t * 83) % 360)
                t_s = 1.5 + (t % 3) * 0.35
                t_mesh = trees[(g_count + t) % len(trees)]
                spawn_foliage(t_mesh, f"Mid_Tree_{g_count}_{t}", (tx, ty, 50.0), (0, t_yaw, 0), (t_s, t_s, t_s), "Foliage/Mid_Trees")
                total_spawned += 1

            # 2 Bushes
            for b in range(2):
                bx = cx + (500.0 if b == 0 else -500.0)
                by = cy + (-350.0 if b == 0 else 400.0)
                b_yaw = float((g_count * 41 + b * 67) % 360)
                spawn_foliage(bushes[0], f"Mid_Bush_{g_count}_{b}", (bx, by, 50.0), (0, b_yaw, 0), (1.5, 1.5, 1.5), "Foliage/Mid_Bushes")
                total_spawned += 1

            # 2 Dreadplants/Mycelium
            for d in range(2):
                dx = cx + (300.0 if d == 0 else -250.0)
                dy = cy + (350.0 if d == 0 else -300.0)
                d_mesh = dreadplants[(g_count + d) % len(dreadplants)]
                spawn_foliage(d_mesh, f"Mid_Flora_{g_count}_{d}", (dx, dy, 50.0), (0, float((g_count * 57) % 360), 0), (1.8, 1.8, 1.8), "Foliage/Mid_Flora")
                total_spawned += 1

    # -------------------------------------------------------------
    # ZONE 3: Outer Wilderness & Foothill Thickets (Radius 65,000 ~ 130,000)
    # -------------------------------------------------------------
    # 16 wild forest belts near the mountains and cliffs
    for w in range(16):
        w_angle = (2.0 * math.pi / 16) * w
        w_dist = 85000.0 + ((w * 1117) % 35000)
        wx = w_dist * math.cos(w_angle)
        wy = w_dist * math.sin(w_angle)

        # 4 Dense trees
        for t in range(4):
            tx = wx + (t * 800.0 - 1200.0)
            ty = wy + ((t * 700.0) % 1000.0 - 500.0)
            t_yaw = float((w * 33 + t * 77) % 360)
            t_s = 2.0 + (t % 3) * 0.4
            t_mesh = trees[(w + t) % len(trees)]
            spawn_foliage(t_mesh, f"Wild_Tree_{w}_{t}", (tx, ty, 50.0), (0, t_yaw, 0), (t_s, t_s, t_s), "Foliage/Wild_Trees")
            total_spawned += 1

        # 2 Mycelium / Dreadplant mushrooms
        for d in range(2):
            dx = wx + (600.0 if d == 0 else -600.0)
            dy = wy + (-400.0 if d == 0 else 500.0)
            d_mesh = dreadplants[(w + d) % len(dreadplants)]
            spawn_foliage(d_mesh, f"Wild_Flora_{w}_{d}", (dx, dy, 50.0), (0, float((w * 89) % 360), 0), (2.2, 2.2, 2.2), "Foliage/Wild_Flora")
            total_spawned += 1

    return {"status": "ok", "total_foliage_spawned": total_spawned}
'''

    res = client.call_tool("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "execute_tool_script", {
        "script": foliage_script
    })
    print("  - Foliage Spawn Result:", res[0]["text"] if res else "None")

    # 3. Save Level & Synchronize
    print("[4/5] Saving level and synchronizing to L_TDWorld_Main...")
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "save_current_level", {})
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "duplicate", {
        "source_asset": "/Game/Level/LV_DarkFantasy_OpenWorld",
        "destination_path": "/Game/World/Maps",
        "new_name": "L_TDWorld_Main"
    })
    print("  - Levels saved and synchronized.")

    # 4. Capture screenshot
    print("[5/5] Capturing updated editor viewport screenshot...")
    switch_to_default_desktop()
    hwnd, _ = find_unreal_window()
    if hwnd:
        import ctypes
        from ctypes import wintypes
        rect = wintypes.RECT()
        ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
        capture_rect_from_desktop(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, "C:/Project/TDGame/editor_screenshot.png")
    print("[SUCCESS] Foliage successfully populated and captured!")

if __name__ == "__main__":
    populate_foliage()

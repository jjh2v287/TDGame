import os
import sys
import json
import math
import random
sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def build_arpg_world():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/6] Connected to Unreal Editor MCP.")

    # 1. Level Check
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/6] Loaded level: {level_path}")

    # Helper spawn functions
    def spawn_asset(asset_path, name, loc, rot=(0,0,0), scale=(1,1,1)):
        return client.call_tool("editor_toolset.toolsets.scene.SceneTools", "add_to_scene_from_asset", {
            "asset_path": asset_path,
            "name": name,
            "xform": {
                "location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])},
                "rotation": {"pitch": float(rot[0]), "yaw": float(rot[1]), "roll": float(rot[2])},
                "scale": {"x": float(scale[0]), "y": float(scale[1]), "z": float(scale[2])}
            },
            "snap_to_ground": False
        })

    def spawn_point_light(name, loc, intensity=35.0, color=(1.0, 0.5, 0.15), radius=900.0):
        res = client.call_tool("editor_toolset.toolsets.scene.SceneTools", "add_to_scene_from_class", {
            "actor_type": {"refPath": "/Script/Engine.PointLight"},
            "name": name,
            "xform": {
                "location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            },
            "snap_to_ground": False
        })
        actor = json.loads(res[0]["text"])["returnValue"]
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": actor})[0]["text"])["returnValue"]
        for c in comps:
            if "LightComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "intensity": intensity,
                        "lightColor": {"r": color[0], "g": color[1], "b": color[2], "a": 1.0},
                        "attenuationRadius": radius
                    })
                })
        return actor

    # 2. Build Undulating Terrain (Rolling Hills, Mounds, Ridges, Slopes across 3km)
    print("[3/6] Building Undulating Terrain & Rolling Hills across 3km World...")
    random.seed(42) # Deterministic layout

    # Hill clusters & natural land elevations
    hill_assets = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff4",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3"
    ]

    # Spawn 32 rolling hill ridges and mounds in non-village zones
    for i in range(32):
        angle = (i / 32.0) * 2 * math.pi + random.uniform(-0.1, 0.1)
        dist = random.uniform(20000, 110000)
        hx = dist * math.cos(angle)
        hy = dist * math.sin(angle)
        # Avoid town zone (-18000 to 0, -18000 to 0)
        if -18000 < hx < 2000 and -18000 < hy < 2000:
            hx += 25000
        hz = random.uniform(-50, 450)
        scale_xy = random.uniform(2.5, 4.5)
        scale_z = random.uniform(1.2, 2.5)
        yaw = random.uniform(0, 360)
        mesh = hill_assets[i % len(hill_assets)]
        spawn_asset(mesh, f"TerrainRidge_{i}", (hx, hy, hz), (0, yaw, 0), (scale_xy, scale_xy, scale_z))

    # 3. Construct Living ARPG Town ("잿빛 안식처 - Ashen Haven") at (-6,000, -6,000)
    print("[4/6] Constructing Full ARPG Village (Ashen Haven) at (-6,000, -6,000)...")
    town_x = -6000.0
    town_y = -6000.0

    # 3-1. Village Plaza Wooden Deck & Central Bonfire/Well
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFlooring1", "Town_Plaza_Deck", (town_x, town_y, 5), (0, 0, 0), (2.5, 2.5, 1.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "Town_CentralWellFire", (town_x, town_y, 25), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn_point_light("Town_BonfireLight", (town_x, town_y, 90), intensity=45.0, color=(1.0, 0.48, 0.15), radius=1200.0)

    # Tavern Gathering Furniture
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Table", "Town_PlazaTable", (town_x + 350, town_y + 200, 15), (0, 30, 0), (1.4, 1.4, 1.4))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Bench", "Town_PlazaBench1", (town_x + 350, town_y + 280, 15), (0, 30, 0), (1.3, 1.3, 1.3))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Bench", "Town_PlazaBench2", (town_x + 350, town_y + 120, 15), (0, 210, 0), (1.3, 1.3, 1.3))

    # 3-2. Blacksmith & Armory Forge (대장간)
    bs_x = town_x - 700.0
    bs_y = town_y + 600.0
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction3", "Blacksmith_Shop", (bs_x, bs_y, 0), (0, -45, 0), (1.4, 1.4, 1.4))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Anvil", "Blacksmith_Anvil", (bs_x + 200, bs_y - 150, 10), (0, 15, 0), (1.3, 1.3, 1.3))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Hammer", "Blacksmith_Hammer", (bs_x + 200, bs_y - 150, 65), (0, 45, 0), (1.3, 1.3, 1.3))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Armory", "Blacksmith_WeaponsRack", (bs_x - 180, bs_y - 100, 10), (0, 45, 0), (1.3, 1.3, 1.3))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Props2/SM_Cauldron", "Blacksmith_Cauldron", (bs_x + 100, bs_y - 250, 10), (0, 0, 0), (1.4, 1.4, 1.4))
    spawn_point_light("Blacksmith_ForgeLight", (bs_x + 100, bs_y - 250, 70), intensity=35.0, color=(1.0, 0.35, 0.08), radius=750.0)

    # 3-3. General Merchant & Supplies Market (잡화 상점 및 식량 가판대)
    mkt_x = town_x + 800.0
    mkt_y = town_y - 500.0
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_FishTable", "Market_DisplayTable", (mkt_x, mkt_y, 10), (0, 20, 0), (1.4, 1.4, 1.4))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Food/SM_Fish", "Market_Fish", (mkt_x, mkt_y, 70), (0, 30, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenCrate", "Market_Crate1", (mkt_x + 150, mkt_y + 120, 10), (0, -10, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Food/SM_Pumkin", "Market_Pumkin", (mkt_x + 150, mkt_y + 120, 75), (0, 0, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Barrels/SM_Barrel1", "Market_Barrel", (mkt_x - 120, mkt_y - 100, 10), (0, 0, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest", "Market_Chest", (mkt_x - 250, mkt_y, 10), (0, 50, 0), (1.2, 1.2, 1.2))

    # 3-4. Residential Houses (주택 및 롯지)
    # House A (North Residential)
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "Town_HouseA", (town_x - 400, town_y + 1100, 0), (0, 15, 0), (1.5, 1.5, 1.5))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenSteps", "Town_HouseA_Steps", (town_x - 400, town_y + 750, 0), (0, 15, 0), (1.3, 1.3, 1.3))

    # House B (East Residential)
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction2", "Town_HouseB", (town_x + 1200, town_y + 300, 0), (0, -75, 0), (1.4, 1.4, 1.4))

    # House C (South Cottage)
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction4", "Town_HouseC", (town_x - 200, town_y - 1200, 0), (0, 160, 0), (1.4, 1.4, 1.4))

    # 3-5. Village Protective Palisade Fences & Entrance Gate
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence1", "Town_Fence_N1", (town_x + 400, town_y + 1400, 0), (0, 10, 0), (1.6, 1.6, 1.6))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence2", "Town_Fence_N2", (town_x + 950, town_y + 1300, 0), (0, 30, 0), (1.6, 1.6, 1.6))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence1", "Town_Fence_S1", (town_x + 600, town_y - 1400, 0), (0, -15, 0), (1.6, 1.6, 1.6))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence2", "Town_Fence_S2", (town_x - 800, town_y - 1400, 0), (0, 5, 0), (1.6, 1.6, 1.6))

    # Village Entrance Gate Posts & Lanterns
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "Town_GateLamp_L", (town_x - 1400, town_y, 0), (0, 45, 0), (1.5, 1.5, 1.5))
    spawn_point_light("Town_GateLight_L", (town_x - 1400, town_y, 280), intensity=35.0, color=(1.0, 0.55, 0.15), radius=900.0)

    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "Town_GateLamp_R", (town_x + 1600, town_y, 0), (0, -45, 0), (1.5, 1.5, 1.5))
    spawn_point_light("Town_GateLight_R", (town_x + 1600, town_y, 280), intensity=35.0, color=(1.0, 0.55, 0.15), radius=900.0)

    # 4. Foliage & Vegetation Clusters across 3km (Birch Groves, Bushes, Dreadplants)
    print("[5/6] Spawning Vast Vegetation & Foliage Clusters across 3km World...")
    tree_assets = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch3"
    ]
    shrub_assets = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Bush/SM_Bush",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1"
    ]

    # Spawn 48 birch forest groves across the 3km expanse
    for i in range(48):
        angle = (i / 48.0) * 2 * math.pi + random.uniform(-0.15, 0.15)
        dist = random.uniform(15000, 125000)
        tx = dist * math.cos(angle)
        ty = dist * math.sin(angle)
        # Avoid exact town center
        if -9000 < tx < -3000 and -9000 < ty < -3000:
            continue
        tz = 0.0
        pitch = random.uniform(-6, 6)
        yaw = random.uniform(0, 360)
        scale = random.uniform(1.6, 2.6)
        tree_mesh = tree_assets[i % len(tree_assets)]
        spawn_asset(tree_mesh, f"WorldBirch_{i}", (tx, ty, tz), (pitch, yaw, 0), (scale, scale, scale))

        # Add an undergrowth bush/dreadplant next to tree
        if i % 2 == 0:
            shrub_mesh = shrub_assets[i % len(shrub_assets)]
            sx = tx + random.uniform(-250, 250)
            sy = ty + random.uniform(-250, 250)
            sscale = random.uniform(1.2, 1.8)
            spawn_asset(shrub_mesh, f"WorldShrub_{i}", (sx, sy, tz), (0, yaw, 0), (sscale, sscale, sscale))

    # 5. Place PlayerStart at Town Plaza Entrance
    print("  - Orienting PlayerStart at Ashen Haven Village Gateway...")
    ps_actors = json.loads(client.call_tool("editor_toolset.toolsets.scene.SceneTools", "find_actors", {"name": "PlayerStart", "tag": "", "collision_channels": []})[0]["text"])["returnValue"]
    if ps_actors:
        client.call_tool("editor_toolset.toolsets.actor.ActorTools", "set_actor_transform", {
            "actor": ps_actors[0],
            "xform": {
                "location": {"x": town_x + 1800.0, "y": town_y, "z": 80.0},
                "rotation": {"pitch": 0.0, "yaw": 180.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            },
            "worldspace": True
        })

    # 6. Save Level and Sync to L_TDWorld_Main
    print("[6/6] Saving 3km Living ARPG World...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    print("Vast 3km Undulating ARPG World + Ashen Haven Village successfully created!")

if __name__ == "__main__":
    build_arpg_world()

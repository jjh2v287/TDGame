import os
import sys
import json
import math
sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def build_3km_world():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/7] Connected to Unreal Editor MCP.")

    # 1. Level Check
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/7] Active level: {level_path}")

    # 2. Query Existing Actors
    actors = json.loads(client.call_tool("editor_toolset.toolsets.scene.SceneTools", "find_actors", {"name": "", "tag": "", "collision_channels": []})[0]["text"])["returnValue"]

    dir_light = next((a for a in actors if "DirectionalLight" in a["refPath"]), None)
    height_fog = next((a for a in actors if "ExponentialHeightFog" in a["refPath"]), None)
    sky_light = next((a for a in actors if "SkyLight" in a["refPath"]), None)
    floor_actor = next((a for a in actors if "Floor" in a["refPath"]), None)
    ppv_actor = next((a for a in actors if "PostProcessVolume" in a["refPath"]), None)

    # 3. Expand Ground to 3km x 3km (Scale: 300 x 300, 300,000cm x 300,000cm)
    print("[3/7] Expanding World Floor to 3km x 3km (300,000cm x 300,000cm)...")
    if floor_actor:
        client.call_tool("editor_toolset.toolsets.actor.ActorTools", "set_actor_transform", {
            "actor": floor_actor,
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": -1.0},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 300.0, "y": 300.0, "z": 1.0}
            },
            "worldspace": True
        })
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": floor_actor})[0]["text"])["returnValue"]
        for c in comps:
            if "StaticMeshComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "overrideMaterials": [{"refPath": "/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_GraySoil"}]
                    })
                })
        print("  - Floor expanded to 3km x 3km with MI_GraySoil.")

    # 4. Tune Atmosphere for 3km Sightline
    print("[4/7] Tuning Lighting & Fog for 3km Open World Visibility...")
    if height_fog:
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": height_fog})[0]["text"])["returnValue"]
        for c in comps:
            if "HeightFogComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "fogDensity": 0.006,
                        "fogHeightFalloff": 0.04,
                        "bEnableVolumetricFog": True,
                        "volumetricFogScatteringDistribution": 0.2,
                        "volumetricFogExtinctionScale": 0.5,
                        "volumetricFogAlbedo": {"r": 0.72, "g": 0.75, "b": 0.78, "a": 1.0}
                    })
                })
        print("  - HeightFog adjusted to 0.006 density for 3km horizon visibility.")

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

    # 5. Build Outer 3km Perimeter Mountain Barriers (±135,000 ~ ±145,000 cm)
    print("[5/7] Constructing 3km Perimeter Mountain Cliffs (Border Barriers)...")
    cliff_assets = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff4"
    ]

    border_dist = 140000.0
    for i, offset in enumerate([-100000, -50000, 0, 50000, 100000]):
        # North Wall
        spawn_asset(cliff_assets[i % 4], f"BorderCliff_N_{i}", (offset, border_dist, -50), (0, -10 + i*5, 0), (3.5, 3.5, 4.5))
        # South Wall
        spawn_asset(cliff_assets[(i+1) % 4], f"BorderCliff_S_{i}", (offset, -border_dist, -50), (0, 170 + i*5, 0), (3.5, 3.5, 4.5))
        # East Wall
        spawn_asset(cliff_assets[(i+2) % 4], f"BorderCliff_E_{i}", (border_dist, offset, -50), (0, -80 + i*5, 0), (3.5, 3.5, 4.5))
        # West Wall
        spawn_asset(cliff_assets[(i+3) % 4], f"BorderCliff_W_{i}", (-border_dist, offset, -50), (0, 80 + i*5, 0), (3.5, 3.5, 4.5))

    # 6. Build the 4 Major Regional Zones across the 3km World
    print("[6/7] Building 4 Major Regional Zones across 3km World...")

    # Zone 1: North Ruined Citadel (X: 0, Y: 75,000 ~ 95,000)
    print("  - Zone 1: North Ruined Citadel...")
    north_center_y = 80000.0
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1", "Citadel_Backwall", (0, north_center_y + 8000, -30), (0, 0, 0), (2.8, 2.8, 3.5))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "Citadel_Tower_L", (-2500, north_center_y, 0), (0, 30, 0), (2.2, 2.2, 2.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "Citadel_Tower_R", (2500, north_center_y, 0), (0, -30, 0), (2.2, 2.2, 2.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence1", "Citadel_Gate_L", (-1200, north_center_y - 1500, 0), (0, 15, 0), (2.5, 2.5, 2.5))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence1", "Citadel_Gate_R", (1200, north_center_y - 1500, 0), (0, -15, 0), (2.5, 2.5, 2.5))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "Citadel_MainBrazier", (0, north_center_y, 40), (0, 0, 0), (2.5, 2.5, 2.5))
    spawn_point_light("Citadel_Fire", (0, north_center_y, 140), intensity=50.0, color=(1.0, 0.45, 0.12), radius=1800.0)

    # Zone 2: South Abandoned Outpost & Trade Camp (X: 0, Y: -75,000 ~ -95,000)
    print("  - Zone 2: South Abandoned Outpost & Caravan...")
    south_center_y = -80000.0
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Cart", "SouthOutpost_Cart1", (500, south_center_y + 800, 0), (12, 25, 10), (1.8, 1.8, 1.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Cart", "SouthOutpost_Cart2", (-800, south_center_y - 600, 0), (-10, -45, 0), (1.6, 1.6, 1.6))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Barrels/SM_Barrels1", "SouthOutpost_Barrels", (1200, south_center_y, 0), (0, 35, 0), (1.8, 1.8, 1.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest", "SouthOutpost_Chest", (300, south_center_y - 400, 0), (0, 60, 0), (1.8, 1.8, 1.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "SouthOutpost_Lamp", (0, south_center_y, 0), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn_point_light("SouthOutpost_Light", (0, south_center_y, 350), intensity=40.0, color=(1.0, 0.5, 0.15), radius=1400.0)

    # Zone 3: East Dungeon Gateway & Necropolis (X: 75,000 ~ 95,000, Y: 0)
    print("  - Zone 3: East Dungeon Gateway & Necropolis...")
    east_center_x = 80000.0
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "NecroGate_L", (east_center_x, -1800, -20), (0, -20, 0), (2.2, 2.2, 2.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "NecroGate_R", (east_center_x, 1800, -20), (0, 20, 0), (2.2, 2.2, 2.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile", "NecroPortal_Platform", (east_center_x, 0, 10), (0, 0, 0), (3.5, 3.5, 1.5))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "NecroPortal_Brazier", (east_center_x, 0, 30), (0, 0, 0), (2.2, 2.2, 2.2))
    spawn_point_light("NecroPortal_Light", (east_center_x, 0, 120), intensity=45.0, color=(0.35, 0.75, 1.0), radius=1600.0)

    # Zone 4: West Ashen Birch Forest & Labyrinth (X: -75,000 ~ -95,000, Y: 0)
    print("  - Zone 4: West Ashen Birch Forest...")
    west_center_x = -80000.0
    trees = [
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1", (-1200, 1500), (10, 45, 0)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch2", (800, 1200), (-8, 120, 0)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch3", (-600, -1800), (12, -75, 0)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1", (1400, -1000), (-6, -140, 0))
    ]
    for idx, (tpath, (tx, ty), trot) in enumerate(trees):
        spawn_asset(tpath, f"WestForest_Tree_{idx}", (west_center_x + tx, ty, 0), trot, (2.2, 2.2, 2.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2", "WestForest_Megalith", (west_center_x, 0, -20), (0, 45, 0), (2.5, 2.5, 2.5))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1", "WestForest_Dreadplant", (west_center_x + 500, 300, 0), (0, 15, 0), (1.8, 1.8, 1.8))

    # Road Network Waypoints & Beacons (Every 25,000 units from center)
    print("  - Placing Road Network Waypoints connecting the 4 regions...")
    road_points = [
        # North Road
        (0, 25000), (0, 50000),
        # South Road
        (0, -25000), (0, -50000),
        # East Road
        (25000, 0), (50000, 0),
        # West Road
        (-25000, 0), (-50000, 0)
    ]
    for idx, (rx, ry) in enumerate(road_points):
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile", f"RoadTile_{idx}", (rx, ry, 5), (0, (idx*45)%360, 0), (2.0, 2.0, 1.0))
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", f"RoadLamp_{idx}", (rx + 250, ry + 250, 0), (0, 45, 0), (1.5, 1.5, 1.5))
        spawn_point_light(f"RoadLight_{idx}", (rx + 250, ry + 250, 300), intensity=30.0, color=(1.0, 0.55, 0.2), radius=1000.0)

    # 7. Move Dungeon Atlas to X: 500,000 (Safe 5km distance beyond 3km world boundary)
    print("  - Relocating Dungeon Atlas Slot 1 to safe 5km distance (X: 500,000, Y: 0)...")
    dungeon_x = 500000.0
    for rx in range(-2, 3):
        for ry in range(-2, 3):
            spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile", f"AtlasD1_Floor_{rx}_{ry}", (dungeon_x + rx * 240, ry * 240, 0), (0, 0, 0), (1.2, 1.2, 1.0))

    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "AtlasD1_NorthWall", (dungeon_x, 700, -20), (0, 0, 0), (1.8, 1.8, 2.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "AtlasD1_SouthWall", (dungeon_x, -700, -20), (0, 180, 0), (1.8, 1.8, 2.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "AtlasD1_ReturnPortal", (dungeon_x, 0, 15), (0, 0, 0), (1.4, 1.4, 1.4))
    spawn_point_light("AtlasD1_Light", (dungeon_x, 0, 75), intensity=30.0, color=(0.4, 0.7, 1.0), radius=800.0)

    # Save
    print("[7/7] Saving all 3km Open World assets...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    print("3km x 3km Open World successfully constructed and saved!")

if __name__ == "__main__":
    build_3km_world()

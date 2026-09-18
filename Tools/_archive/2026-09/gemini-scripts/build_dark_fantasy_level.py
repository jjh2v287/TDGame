import os
import sys
import json
sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def build_level():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/6] Unreal MCP Client connected.")

    # 1. Load Level
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/6] Loaded level: {level_path}")

    # 2. Configure Lighting & Atmosphere
    actors = json.loads(client.call_tool("editor_toolset.toolsets.scene.SceneTools", "find_actors", {"name": "", "tag": "", "collision_channels": []})[0]["text"])["returnValue"]

    dir_light = None
    height_fog = None
    sky_light = None
    floor_actor = None
    ppv_actor = None

    for a in actors:
        ref = a["refPath"]
        if "DirectionalLight" in ref:
            dir_light = a
        elif "ExponentialHeightFog" in ref:
            height_fog = a
        elif "SkyLight" in ref:
            sky_light = a
        elif "Floor" in ref:
            floor_actor = a
        elif "PostProcessVolume" in ref:
            ppv_actor = a

    # 2-1. Configure Directional Light (Cold Nocturnal Moon)
    if dir_light:
        client.call_tool("editor_toolset.toolsets.actor.ActorTools", "set_actor_transform", {
            "actor": dir_light,
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": 1000.0},
                "rotation": {"pitch": -55.0, "yaw": -45.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            },
            "worldspace": True
        })
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": dir_light})[0]["text"])["returnValue"]
        for c in comps:
            if "LightComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "intensity": 0.75,
                        "lightColor": {"r": 0.45, "g": 0.6, "b": 0.95, "a": 1.0},
                        "bCastVolumetricShadow": True,
                        "volumetricScatteringIntensity": 2.2
                    })
                })
        print("  - DirectionalLight configured for cold top-down moonlight.")

    # 2-2. Configure Fog (Ominous Volumetric Mist)
    if height_fog:
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": height_fog})[0]["text"])["returnValue"]
        for c in comps:
            if "HeightFogComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "fogDensity": 0.055,
                        "fogHeightFalloff": 0.04,
                        "bEnableVolumetricFog": True,
                        "volumetricFogScatteringDistribution": 0.35,
                        "volumetricFogExtinctionScale": 1.4,
                        "volumetricFogAlbedo": {"r": 0.25, "g": 0.35, "b": 0.5, "a": 1.0}
                    })
                })
        print("  - ExponentialHeightFog configured for thick volumetric dark mist.")

    # 2-3. Configure SkyLight (Deep Dark Ambient)
    if sky_light:
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": sky_light})[0]["text"])["returnValue"]
        for c in comps:
            if "LightComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "intensity": 0.15,
                        "lightColor": {"r": 0.2, "g": 0.25, "b": 0.4, "a": 1.0}
                    })
                })
        print("  - SkyLight configured for deep shadowy ambiance.")

    # 2-4. Configure PostProcessVolume (Vignette, Desaturation, Crisp Contrast)
    if not ppv_actor:
        res = client.call_tool("editor_toolset.toolsets.scene.SceneTools", "add_to_scene_from_class", {
            "actor_type": {"refPath": "/Script/Engine.PostProcessVolume"},
            "name": "PPV_DarkFantasy",
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": 0.0},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            },
            "snap_to_ground": False
        })
        ppv_actor = json.loads(res[0]["text"])["returnValue"]

    client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
        "instance": ppv_actor,
        "values": json.dumps({
            "bUnbound": True,
            "settings": {
                "bOverride_VignetteIntensity": True,
                "vignetteIntensity": 0.72,
                "bOverride_ColorSaturation": True,
                "colorSaturation": {"x": 0.72, "y": 0.72, "z": 0.78, "w": 1.0},
                "bOverride_ColorContrast": True,
                "colorContrast": {"x": 1.25, "y": 1.25, "z": 1.3, "w": 1.0},
                "bOverride_FilmGrainIntensity": True,
                "filmGrainIntensity": 0.14,
                "bOverride_AutoExposureMinBrightness": True,
                "autoExposureMinBrightness": 0.8,
                "bOverride_AutoExposureMaxBrightness": True,
                "autoExposureMaxBrightness": 1.2,
                "bOverride_BloomIntensity": True,
                "bloomIntensity": 0.4
            }
        })
    })
    print("  - PostProcessVolume configured with Diablo grim tone and vignette.")

    # 3. Expand Open World Floor
    if floor_actor:
        client.call_tool("editor_toolset.toolsets.actor.ActorTools", "set_actor_transform", {
            "actor": floor_actor,
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": -1.0},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 35.0, "y": 35.0, "z": 1.0}
            },
            "worldspace": True
        })
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": floor_actor})[0]["text"])["returnValue"]
        for c in comps:
            if "StaticMeshComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "overrideMaterials": [{"refPath": "/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_BaseWorldAlignedSoil"}]
                    })
                })
        print("  - Floor expanded to 35x35 and mapped to Dark Fantasy WorldAlignedSoil.")

    print("[3/6] Atmosphere and Lighting setup complete.")

    # Helper function to spawn asset
    def spawn_asset(asset_path, name, loc, rot=(0,0,0), scale=(1,1,1)):
        args = {
            "asset_path": asset_path,
            "name": name,
            "xform": {
                "location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])},
                "rotation": {"pitch": float(rot[0]), "yaw": float(rot[1]), "roll": float(rot[2])},
                "scale": {"x": float(scale[0]), "y": float(scale[1]), "z": float(scale[2])}
            },
            "snap_to_ground": False
        }
        res = client.call_tool("editor_toolset.toolsets.scene.SceneTools", "add_to_scene_from_asset", args)
        return json.loads(res[0]["text"])["returnValue"]

    # Helper function to spawn PointLight
    def spawn_point_light(name, loc, intensity=35.0, color=(1.0, 0.45, 0.12), radius=850.0):
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

    # 4. Compose Diablo-Style Sanctuary Environment
    print("[4/6] Spawning Central Altar & Desecrated Waypoint...")

    # Central Stone Pavements (Cracked Ritual Ground)
    tile_coords = [
        (0, 0, 0), (220, 0, 0), (-220, 0, 0), (0, 220, 0), (0, -220, 0),
        (160, 160, 0), (-160, 160, 0), (160, -160, 0), (-160, -160, 0)
    ]
    for idx, (tx, ty, tz) in enumerate(tile_coords):
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile", f"AltarTile_{idx}", (tx, ty, tz), (0, (idx*37)%360, 0), (1.1, 1.1, 1.0))

    # Central Brazier & Flame
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "CentralBrazier", (0, 0, 10), (0, 0, 0), (1.3, 1.3, 1.3))
    spawn_point_light("CentralAltarFire", (0, 0, 70), intensity=45.0, color=(1.0, 0.42, 0.1), radius=1000.0)

    # Candles Clusters
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_CandlesGroup", "Candles_1", (70, -60, 15), (0, 45, 0), (1.2, 1.2, 1.2))
    spawn_point_light("CandleLight_1", (70, -60, 50), intensity=15.0, color=(1.0, 0.65, 0.2), radius=400.0)
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_CandlesGroup", "Candles_2", (-60, 70, 15), (0, 190, 0), (1.2, 1.2, 1.2))
    spawn_point_light("CandleLight_2", (-60, 70, 50), intensity=15.0, color=(1.0, 0.65, 0.2), radius=400.0)

    # Impaled Skulls & Remains
    skull_posts = [
        (260, 0, 0, 15), (-260, 0, 0, -30), (0, 260, 0, 95), (0, -260, 0, -90),
        (200, 200, 0, 45), (-200, 200, 0, 135), (200, -200, 0, -45), (-200, -200, 0, -135)
    ]
    for idx, (sx, sy, sz, syaw) in enumerate(skull_posts):
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_StickedSkull", f"StickedSkull_{idx}", (sx, sy, sz), (0, syaw, 0), (1.0, 1.0, 1.0))

    # Sacrificial Remains & Debris on Floor
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_RamSkull", "RamSkull_1", (110, 40, 5), (0, 30, 10), (1.1, 1.1, 1.1))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_RamSkull2", "RamSkull_2", (-90, -50, 5), (0, -80, -5), (1.1, 1.1, 1.1))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_Skull", "Skull_Floor_1", (60, 100, 5), (0, 120, 0), (1.0, 1.0, 1.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_Bone", "Bone_Floor_1", (90, -80, 5), (0, 60, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_ChainCurved", "Chain_Altar", (-120, 0, 5), (0, 45, 0), (1.2, 1.2, 1.2))

    # Discarded Weapons
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword", "SacrificialSword", (140, -40, 20), (18, 40, -10), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Shield", "RustedShield", (-140, 60, 15), (-75, 120, 0), (1.1, 1.1, 1.1))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Axe", "WarAxe", (40, 140, 10), (10, -70, 0), (1.2, 1.2, 1.2))

    print("[5/6] Spawning Ruined Outpost, Overturned Wagon, and Barricades...")

    # Overturned Wagon & Supplies
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Cart", "RuinedWagon", (680, 260, 0), (15, 35, 12), (1.1, 1.1, 1.1))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Barrels/SM_Barrels1", "BarrelsGroup_1", (600, 360, 0), (0, -20, 0), (1.0, 1.0, 1.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Barrels/SM_Barrel1", "Barrel_1", (740, 180, 0), (85, 40, 0), (1.0, 1.0, 1.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest", "LootChest_1", (620, 180, 0), (0, 65, 0), (1.1, 1.1, 1.1))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenCrate", "Crate_1", (760, 310, 0), (0, -15, 0), (1.0, 1.0, 1.0))

    # Ruined Wooden Barricades & Palisades
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence1", "Fence_1", (460, 380, 0), (0, 25, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence2", "Fence_2", (560, 480, 0), (0, 50, 0), (1.2, 1.2, 1.2))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "WoodenShelter", (-550, 350, 0), (0, -45, 0), (1.1, 1.1, 1.1))

    # Iron Lamppost with Torch Fires (Chiaroscuro Night Lighting)
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "Lamppost_Path_1", (420, 120, 0), (0, 10, 0), (1.2, 1.2, 1.2))
    spawn_point_light("LamppostLight_1", (420, 120, 240), intensity=35.0, color=(1.0, 0.45, 0.12), radius=850.0)

    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "Lamppost_Path_2", (920, 420, 0), (0, -35, 0), (1.2, 1.2, 1.2))
    spawn_point_light("LamppostLight_2", (920, 420, 240), intensity=35.0, color=(1.0, 0.45, 0.12), radius=850.0)

    # Surrounding Enclosing Cliff Walls (Dark Gorge Fortress)
    print("  - Building perimeter rock canyons and cliffs...")
    cliff_specs = [
        # North Wall
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "NorthCliff_1", (-900, 1400, -30), (0, 15, 0), (1.8, 1.8, 2.2)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1", "NorthCliff_2", (0, 1500, -30), (0, -10, 0), (2.0, 2.0, 2.4)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "NorthCliff_3", (950, 1450, -30), (0, 30, 0), (1.8, 1.8, 2.2)),
        # South Wall
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff4", "SouthCliff_1", (-950, -1450, -30), (0, -170, 0), (1.9, 1.9, 2.2)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1", "SouthCliff_2", (0, -1550, -30), (0, 180, 0), (2.1, 2.1, 2.4)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "SouthCliff_3", (900, -1400, -30), (0, 160, 0), (1.8, 1.8, 2.2)),
        # West Wall
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "WestCliff_1", (-1450, -600, -30), (0, 85, 0), (2.0, 2.0, 2.4)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "WestCliff_2", (-1500, 600, -30), (0, 95, 0), (2.0, 2.0, 2.4)),
        # Inner Rocks
        ("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1", "BigRock_1", (-500, -450, -10), (0, 40, 0), (1.4, 1.4, 1.4)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2", "BigRock_2", (500, -500, -10), (0, -60, 0), (1.3, 1.3, 1.3)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3", "BigRock_3", (-450, 750, -10), (0, 120, 0), (1.4, 1.4, 1.4))
    ]
    for mesh_path, cname, cloc, crot, cscale in cliff_specs:
        spawn_asset(mesh_path, cname, cloc, crot, cscale)

    # Corrupted Nature (Dead Twisted Trees & Dreadplants)
    print("  - Spawning dead birch trees and corrupted dreadplants...")
    trees = [
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1", "DeadTree_1", (-380, -320, 0), (5, 45, 0), (1.3, 1.3, 1.3)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch2", "DeadTree_2", (360, -380, 0), (-8, 120, 0), (1.2, 1.2, 1.2)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch3", "DeadTree_3", (-360, 520, 0), (10, -75, 0), (1.3, 1.3, 1.3)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1", "DeadTree_4", (820, -250, 0), (-5, -150, 0), (1.4, 1.4, 1.4))
    ]
    for mesh_path, tname, tloc, trot, tscale in trees:
        spawn_asset(mesh_path, tname, tloc, trot, tscale)

    dreadplants = [
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1", "Dreadplant_1", (-180, -80, 0), (0, 20, 0), (1.2, 1.2, 1.2)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant2", "Dreadplant_2", (160, 90, 0), (0, -45, 0), (1.1, 1.1, 1.1)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom1", "Mushrooms_1", (-90, 160, 0), (0, 80, 0), (1.3, 1.3, 1.3)),
        ("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1", "Mycelium_1", (120, -160, 0), (0, -110, 0), (1.4, 1.4, 1.4))
    ]
    for mesh_path, dname, dloc, drot, dscale in dreadplants:
        spawn_asset(mesh_path, dname, dloc, drot, dscale)

    # 5. Save Level & Assets
    print("[6/6] Saving all dirty assets and level...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    print("Level build successfully finished!")

if __name__ == "__main__":
    build_level()

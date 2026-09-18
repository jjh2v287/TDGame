import os
import sys
import json
import math
import random

sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def build_dense_world():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/7] Connected to Unreal Editor MCP.")

    # 1. Ensure Level is Loaded
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/7] Active level confirmed: {level_path}")

    def run_editor_script(script_body):
        res = client.call_tool("editor_toolset.toolsets.programmatic.ProgrammaticToolset", "execute_tool_script", {"script": script_body})
        return res

    # -------------------------------------------------------------
    # BATCH 1: Base Environment, Lighting, Floor & Perimeter Walls
    # -------------------------------------------------------------
    print("[3/7] Setting up Base 3km Terrain, Perimeter Mountain Ring & PCG Volumes...")
    batch1_script = '''
import json

def run():
    actors = execute_tool("editor_toolset.toolsets.scene.SceneTools.find_actors", json.dumps({
        "name": "", "tag": "", "collision_channels": []
    }))["returnValue"]

    floor = next((a for a in actors if "Floor" in a["refPath"]), None)
    dir_light = next((a for a in actors if "DirectionalLight" in a["refPath"]), None)
    sky_light = next((a for a in actors if "SkyLight" in a["refPath"]), None)
    fog = next((a for a in actors if "ExponentialHeightFog" in a["refPath"]), None)
    ppv = next((a for a in actors if "PostProcessVolume" in a["refPath"]), None)

    # Scale floor to 300,000 x 300,000 cm (3km x 3km)
    if floor:
        execute_tool("editor_toolset.toolsets.actor.ActorTools.set_actor_transform", json.dumps({
            "actor": floor,
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": -1.0},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 300.0, "y": 300.0, "z": 1.0}
            },
            "worldspace": True
        }))
        comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": floor}))["returnValue"]
        for c in comps:
            if "StaticMeshComponent" in c["refPath"]:
                execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                    "instance": c,
                    "values": json.dumps({
                        "overrideMaterials": [{"refPath": "/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_GraySoil"}]
                    })
                }))

    # Directional Light: 3.2 lux silver-gray daylight
    if dir_light:
        comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": dir_light}))["returnValue"]
        for c in comps:
            if "DirectionalLightComponent" in c["refPath"]:
                execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                    "instance": c,
                    "values": json.dumps({
                        "intensity": 3.2,
                        "lightColor": {"r": 0.88, "g": 0.90, "b": 0.94, "a": 1.0}
                    })
                }))
        execute_tool("editor_toolset.toolsets.actor.ActorTools.set_actor_transform", json.dumps({
            "actor": dir_light,
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": 1000.0},
                "rotation": {"pitch": -48.0, "yaw": 55.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            },
            "worldspace": True
        }))

    # Sky Light: 1.5 lux ashen ambient (bright open shadows)
    if sky_light:
        comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": sky_light}))["returnValue"]
        for c in comps:
            if "SkyLightComponent" in c["refPath"]:
                execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                    "instance": c,
                    "values": json.dumps({
                        "intensity": 1.5,
                        "lightColor": {"r": 0.75, "g": 0.78, "b": 0.82, "a": 1.0}
                    })
                }))

    # Fog: 0.005 density for open 3km horizon sightline
    if fog:
        comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": fog}))["returnValue"]
        for c in comps:
            if "HeightFogComponent" in c["refPath"]:
                execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                    "instance": c,
                    "values": json.dumps({
                        "fogDensity": 0.005,
                        "fogHeightFalloff": 0.03,
                        "bEnableVolumetricFog": True,
                        "volumetricFogScatteringDistribution": 0.2,
                        "volumetricFogExtinctionScale": 0.4,
                        "volumetricFogAlbedo": {"r": 0.75, "g": 0.78, "b": 0.82, "a": 1.0}
                    })
                }))

    # PPV: 0.65 saturation for ashen ruin tone
    if ppv:
        comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": ppv}))["returnValue"]
        for c in comps:
            if "PostProcessComponent" in c["refPath"]:
                execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                    "instance": c,
                    "values": json.dumps({
                        "settings": {
                            "bOverride_VignetteIntensity": True,
                            "vignetteIntensity": 0.12,
                            "bOverride_ColorSaturation": True,
                            "colorSaturation": {"x": 0.65, "y": 0.65, "z": 0.65, "w": 1.0},
                            "bOverride_ColorContrast": True,
                            "colorContrast": {"x": 1.05, "y": 1.05, "z": 1.05, "w": 1.0},
                            "bOverride_AutoExposureMinBrightness": True,
                            "autoExposureMinBrightness": 1.05,
                            "bOverride_AutoExposureMaxBrightness": True,
                            "autoExposureMaxBrightness": 1.05
                        }
                    })
                }))

    # 2. Build Perimeter Border Mountain Ring (24 Giant Cliffs at +-140,000 cm)
    border_dist = 140000.0
    cliff_assets = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff4"
    ]

    border_count = 0
    offsets = [-120000.0, -80000.0, -40000.0, 0.0, 40000.0, 80000.0, 120000.0]
    for i, off in enumerate(offsets):
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": cliff_assets[i % 4],
            "name": f"Border_N_{i}",
            "xform": {"location": {"x": off, "y": border_dist, "z": -100.0}, "rotation": {"pitch": 0.0, "yaw": -10.0 + i*5.0, "roll": 0.0}, "scale": {"x": 5.0, "y": 5.0, "z": 6.5}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": cliff_assets[(i+1) % 4],
            "name": f"Border_S_{i}",
            "xform": {"location": {"x": off, "y": -border_dist, "z": -100.0}, "rotation": {"pitch": 0.0, "yaw": 170.0 + i*5.0, "roll": 0.0}, "scale": {"x": 5.0, "y": 5.0, "z": 6.5}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": cliff_assets[(i+2) % 4],
            "name": f"Border_E_{i}",
            "xform": {"location": {"x": border_dist, "y": off, "z": -100.0}, "rotation": {"pitch": 0.0, "yaw": -85.0 + i*5.0, "roll": 0.0}, "scale": {"x": 5.0, "y": 5.0, "z": 6.5}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": cliff_assets[(i+3) % 4],
            "name": f"Border_W_{i}",
            "xform": {"location": {"x": -border_dist, "y": off, "z": -100.0}, "rotation": {"pitch": 0.0, "yaw": 85.0 + i*5.0, "roll": 0.0}, "scale": {"x": 5.0, "y": 5.0, "z": 6.5}},
            "snap_to_ground": False
        }))
        border_count += 4

    # 3. Spawn 4 Quadrant PCG Volumes for Micro-Vegetation
    pcg_configs = [
        {"name": "PCGVol_Center", "loc": [0.0, 0.0, 200.0], "scale": [600.0, 600.0, 50.0], "graph": "/Game/DarkFantasyTopDown/PCG/PCG_Grass1.PCG_Grass1"},
        {"name": "PCGVol_NE", "loc": [75000.0, 75000.0, 200.0], "scale": [700.0, 700.0, 50.0], "graph": "/Game/DarkFantasyTopDown/PCG/PCG_Rocks.PCG_Rocks"},
        {"name": "PCGVol_NW", "loc": [-75000.0, 75000.0, 200.0], "scale": [700.0, 700.0, 50.0], "graph": "/Game/DarkFantasyTopDown/PCG/PCG_Leaves.PCG_Leaves"},
        {"name": "PCGVol_SE", "loc": [75000.0, -75000.0, 200.0], "scale": [700.0, 700.0, 50.0], "graph": "/Game/DarkFantasyTopDown/PCG/PCG_Grass2.PCG_Grass2"},
        {"name": "PCGVol_SW", "loc": [-75000.0, -75000.0, 200.0], "scale": [700.0, 700.0, 50.0], "graph": "/Game/DarkFantasyTopDown/PCG/PCG_SkullsAndBones.PCG_SkullsAndBones"}
    ]
    for cfg in pcg_configs:
        vol_actor = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class", json.dumps({
            "actor_type": {"refPath": "/Script/PCG.PCGVolume"},
            "name": cfg["name"],
            "xform": {
                "location": {"x": cfg["loc"][0], "y": cfg["loc"][1], "z": cfg["loc"][2]},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": cfg["scale"][0], "y": cfg["scale"][1], "z": cfg["scale"][2]}
            },
            "snap_to_ground": False
        }))["returnValue"]
        vol_comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": vol_actor}))["returnValue"]
        pcg_comp = next((c for c in vol_comps if "PCG Component" in c["refPath"] or "PCGComponent" in c["refPath"]), None)
        if pcg_comp:
            execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                "instance": pcg_comp,
                "values": json.dumps({
                    "graphInstance": {"graph": {"refPath": cfg["graph"]}},
                    "generationTrigger": "GenerateOnLoad",
                    "bActivated": True
                })
            }))

    return {"status": "ok", "borders_spawned": border_count, "pcg_volumes": len(pcg_configs)}
'''
    res1 = run_editor_script(batch1_script)
    print("  - Batch 1 Result:", res1[0]["text"] if res1 else "None")

    # -------------------------------------------------------------
    # BATCH 2: Undulating Terrain Formations across 16 Sectors
    # -------------------------------------------------------------
    print("[4/7] Constructing Undulating Terrain Hills, Ridges & Geological Formations...")
    batch2_script = '''
import json

def run():
    hills = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff4",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3"
    ]

    grid_coords = [-105000.0, -65000.0, -25000.0, 25000.0, 65000.0, 105000.0]

    spawned = 0
    idx = 0
    for gx in grid_coords:
        for gy in grid_coords:
            if abs(gx) < 30000.0 and abs(gy) < 30000.0:
                continue

            asset_a = hills[idx % len(hills)]
            scale_a = 4.0 + (idx % 3) * 0.8
            z_a = 30.0 + (idx % 5) * 60.0
            yaw_a = (idx * 47) % 360
            execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": asset_a,
                "name": f"TerrainHill_A_{idx}",
                "xform": {
                    "location": {"x": gx + ((idx*13)%3000 - 1500), "y": gy + ((idx*17)%3000 - 1500), "z": z_a},
                    "rotation": {"pitch": 0.0, "yaw": float(yaw_a), "roll": 0.0},
                    "scale": {"x": scale_a, "y": scale_a, "z": scale_a * 0.7}
                },
                "snap_to_ground": False
            }))
            spawned += 1

            asset_b = hills[(idx + 3) % len(hills)]
            scale_b = 3.2 + (idx % 4) * 0.6
            z_b = -20.0 + (idx % 4) * 40.0
            yaw_b = (idx * 83) % 360
            execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": asset_b,
                "name": f"TerrainHill_B_{idx}",
                "xform": {
                    "location": {"x": gx + 4500.0, "y": gy - 4000.0, "z": z_b},
                    "rotation": {"pitch": 0.0, "yaw": float(yaw_b), "roll": 0.0},
                    "scale": {"x": scale_b, "y": scale_b, "z": scale_b * 0.8}
                },
                "snap_to_ground": False
            }))
            spawned += 1
            idx += 1

    return {"status": "ok", "terrain_mounds_spawned": spawned}
'''
    res2 = run_editor_script(batch2_script)
    print("  - Batch 2 Result:", res2[0]["text"] if res2 else "None")

    # -------------------------------------------------------------
    # BATCH 3: The Complete Ashen Haven Village (Central Basin)
    # -------------------------------------------------------------
    print("[5/7] Constructing Complete Ashen Haven Village Settlement...")
    batch3_script = '''
import json

def run():
    spawned = 0

    def spawn(asset, name, loc, rot=(0,0,0), scale=(1,1,1)):
        nonlocal spawned
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": asset,
            "name": name,
            "xform": {
                "location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])},
                "rotation": {"pitch": float(rot[0]), "yaw": float(rot[1]), "roll": float(rot[2])},
                "scale": {"x": float(scale[0]), "y": float(scale[1]), "z": float(scale[2])}
            },
            "snap_to_ground": False
        }))
        spawned += 1

    def spawn_light(name, loc, intensity=35.0, color=(1.0, 0.5, 0.15), radius=1000.0):
        nonlocal spawned
        act = execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_class", json.dumps({
            "actor_type": {"refPath": "/Script/Engine.PointLight"},
            "name": name,
            "xform": {
                "location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])},
                "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}
            },
            "snap_to_ground": False
        }))["returnValue"]
        comps = execute_tool("editor_toolset.toolsets.actor.ActorTools.get_components", json.dumps({"actor": act}))["returnValue"]
        for c in comps:
            if "LightComponent" in c["refPath"]:
                execute_tool("editor_toolset.toolsets.object.ObjectTools.set_properties", json.dumps({
                    "instance": c,
                    "values": json.dumps({
                        "intensity": intensity,
                        "lightColor": {"r": color[0], "g": color[1], "b": color[2], "a": 1.0},
                        "attenuationRadius": radius
                    })
                }))
        spawned += 1

    # 1. Town Plaza Pavement
    tile_asset = "/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile"
    for ix in range(-2, 3):
        for iy in range(-2, 3):
            spawn(tile_asset, f"Plaza_Tile_{ix}_{iy}", (ix * 600, iy * 600, 2), (0, ((ix+iy)*30)%360, 0), (2.5, 2.5, 1.0))

    # 2. Central Fire & Community Plaza
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "Plaza_GrandBrazier", (0, 0, 10), (0, 0, 0), (2.4, 2.4, 2.4))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_WoodenBrick", "Plaza_LogPile1", (120, 80, 5), (0, 35, 0), (1.5, 1.5, 1.5))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_WoodenBrick", "Plaza_LogPile2", (-100, -90, 5), (0, -45, 0), (1.5, 1.5, 1.5))
    spawn_light("Plaza_Bonfire_Light", (0, 0, 120), intensity=50.0, color=(1.0, 0.48, 0.12), radius=2200.0)

    # Plaza Benches and Table
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Table", "Plaza_Table", (600, 500, 5), (0, 15, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Bench", "Plaza_Bench1", (600, 650, 5), (0, 15, 0), (1.5, 1.5, 1.5))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Bench", "Plaza_Bench2", (600, 350, 5), (0, 195, 0), (1.5, 1.5, 1.5))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Stool", "Plaza_Stool1", (420, 500, 5), (0, 80, 0), (1.5, 1.5, 1.5))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props1/SM_Bottle", "Plaza_Bottle", (600, 500, 85), (0, 0, 0), (1.5, 1.5, 1.5))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props1/SM_Cup", "Plaza_Cup", (630, 480, 85), (0, 0, 0), (1.5, 1.5, 1.5))

    # 3. The Blacksmith Workshop (West of Plaza)
    bs_x, bs_y = -1800, 600
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "Blacksmith_Workshop", (bs_x, bs_y, 0), (0, 90, 0), (2.0, 2.0, 2.0))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFlooring1", "Blacksmith_Floor", (bs_x, bs_y, 5), (0, 90, 0), (2.0, 2.0, 1.0))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Anvil", "Blacksmith_Anvil", (bs_x + 350, bs_y, 10), (0, 45, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props3/SM_Hammer", "Blacksmith_Hammer", (bs_x + 350, bs_y, 110), (0, 30, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props1/SM_Axe", "Blacksmith_Axe", (bs_x + 200, bs_y + 400, 50), (15, 0, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props2/SM_Cauldron", "Blacksmith_Cauldron", (bs_x - 300, bs_y - 200, 5), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenShelving", "Blacksmith_Shelf", (bs_x - 450, bs_y + 200, 5), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest", "Blacksmith_Chest", (bs_x - 300, bs_y + 400, 5), (0, 25, 0), (1.6, 1.6, 1.6))
    spawn_light("Blacksmith_Forge_Light", (bs_x - 300, bs_y - 200, 120), intensity=40.0, color=(1.0, 0.42, 0.1), radius=1400.0)

    # 4. Merchant Caravanserai & General Goods (North of Plaza)
    mc_x, mc_y = 400, 2000
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Cart", "Merchant_Cart1", (mc_x - 600, mc_y, 0), (0, 35, 0), (1.7, 1.7, 1.7))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Cart", "Merchant_Cart2", (mc_x + 700, mc_y - 200, 0), (-5, -20, 0), (1.7, 1.7, 1.7))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Barrels/SM_Barrels1", "Merchant_Barrels", (mc_x - 200, mc_y + 400, 0), (0, 45, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenCrate", "Merchant_Crate1", (mc_x + 200, mc_y + 300, 0), (0, 15, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenCrate2", "Merchant_Crate2", (mc_x + 200, mc_y + 300, 100), (0, -10, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest", "Merchant_Chest", (mc_x, mc_y + 500, 0), (0, 90, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Food/SM_Bread", "Merchant_Bread", (mc_x + 180, mc_y + 280, 175), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Food/SM_Pumkin", "Merchant_Pumpkin", (mc_x - 100, mc_y + 350, 0), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Food/SM_Fish", "Merchant_Fish", (mc_x + 230, mc_y + 310, 175), (0, 45, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "Merchant_Lamp", (mc_x, mc_y - 200, 0), (0, 0, 0), (1.6, 1.6, 1.6))
    spawn_light("Merchant_Light", (mc_x, mc_y - 200, 320), intensity=35.0, color=(1.0, 0.55, 0.2), radius=1200.0)

    # 5. Three Residential Cottages
    h1_x, h1_y = 2200, -200
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction2", "House1_Chieftain", (h1_x, h1_y, 0), (0, -90, 0), (2.0, 2.0, 2.0))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenSteps", "House1_Steps", (h1_x - 450, h1_y, 0), (0, -90, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost", "House1_Lamp", (h1_x - 550, h1_y + 250, 0), (0, 0, 0), (1.5, 1.5, 1.5))
    spawn_light("House1_Light", (h1_x - 550, h1_y + 250, 300), intensity=30.0, color=(1.0, 0.55, 0.2), radius=1000.0)

    h2_x, h2_y = 1800, 1800
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction3", "House2_Herbalist", (h2_x, h2_y, 0), (0, -135, 0), (1.9, 1.9, 1.9))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/SmallProps/SM_WoodenBrick", "House2_Firewood", (h2_x - 350, h2_y - 200, 0), (0, 20, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props2/SM_Bag", "House2_HerbBag", (h2_x - 200, h2_y - 350, 0), (0, 0, 0), (1.6, 1.6, 1.6))

    h3_x, h3_y = -1600, -1600
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction4", "House3_Hunter", (h3_x, h3_y, 0), (0, 45, 0), (1.9, 1.9, 1.9))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenLadder", "House3_Ladder", (h3_x + 350, h3_y + 200, 0), (10, 45, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Containers/SM_WoodenChest", "House3_Chest", (h3_x + 200, h3_y - 300, 0), (0, -30, 0), (1.6, 1.6, 1.6))

    # 6. Livestock Stable & Farmyard
    st_x, st_y = 0, -2000
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props2/SM_Trough", "Stable_Trough", (st_x - 300, st_y, 0), (0, 90, 0), (1.8, 1.8, 1.8))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props2/SM_Pitchfork", "Stable_Pitchfork", (st_x - 450, st_y + 150, 20), (20, 45, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/Props1/SM_Bucket", "Stable_Bucket", (st_x - 200, st_y + 200, 0), (0, 0, 0), (1.6, 1.6, 1.6))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenWheel", "Stable_Wheel", (st_x + 350, st_y, 10), (15, 30, 0), (1.6, 1.6, 1.6))

    # 7. Village Palisade Perimeter Fences & Watchtowers
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "Village_Watchtower_N", (600, 3200, 0), (0, 0, 0), (1.8, 1.8, 2.2))
    spawn("/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1", "Village_Watchtower_S", (-600, -3200, 0), (0, 180, 0), (1.8, 1.8, 2.2))

    import math
    fence_asset = "/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFence1"
    for a_deg in range(0, 360, 24):
        if abs(a_deg - 90) < 18 or abs(a_deg - 270) < 18:
            continue
        rad = math.radians(a_deg)
        fx = 3400.0 * math.cos(rad)
        fy = 3400.0 * math.sin(rad)
        fyaw = a_deg + 90.0
        spawn(fence_asset, f"Village_Palisade_{a_deg}", (fx, fy, 0), (0, fyaw, 0), (2.0, 2.0, 2.0))

    return {"status": "ok", "village_actors_spawned": spawned}
'''
    res3 = run_editor_script(batch3_script)
    print("  - Batch 3 Result:", res3[0]["text"] if res3 else "None")

    # -------------------------------------------------------------
    # BATCH 4: Dense Forest Groves across the 3km World (50 Groves)
    # -------------------------------------------------------------
    print("[6/7] Distributing Dense Forest Clusters across 3km World...")
    batch4_script = '''
import json

def run():
    trees = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch3"
    ]
    bushes = "/Game/DarkFantasyTopDown/StaticMeshes/Bush/SM_Bush"
    flora = [
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant2",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1",
        "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rocks"
    ]

    grove_coords = [-115000.0, -80000.0, -45000.0, 45000.0, 80000.0, 115000.0]

    spawned = 0
    g_idx = 0
    for cx in grove_coords:
        for cy in grove_coords:
            if abs(cx) < 25000.0 and abs(cy) < 25000.0:
                continue

            cluster_offsets = [
                (0.0, 0.0), (1200.0, 800.0), (-1100.0, 900.0),
                (900.0, -1200.0), (-800.0, -900.0)
            ]
            for t_i, (ox, oy) in enumerate(cluster_offsets):
                t_asset = trees[(g_idx + t_i) % len(trees)]
                tx = cx + ox + ((g_idx * 11) % 600 - 300)
                ty = cy + oy + ((g_idx * 13) % 600 - 300)
                t_yaw = float((g_idx * 37 + t_i * 71) % 360)
                t_scale = 1.8 + ((g_idx + t_i) % 4) * 0.35

                execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                    "asset_path": t_asset,
                    "name": f"Grove_{g_idx}_Tree_{t_i}",
                    "xform": {
                        "location": {"x": tx, "y": ty, "z": 0.0},
                        "rotation": {"pitch": 0.0, "yaw": t_yaw, "roll": 0.0},
                        "scale": {"x": t_scale, "y": t_scale, "z": t_scale}
                    },
                    "snap_to_ground": False
                }))
                spawned += 1

            execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": bushes,
                "name": f"Grove_{g_idx}_Bush_1",
                "xform": {
                    "location": {"x": cx + 500.0, "y": cy - 400.0, "z": 0.0},
                    "rotation": {"pitch": 0.0, "yaw": float((g_idx * 53) % 360), "roll": 0.0},
                    "scale": {"x": 1.6, "y": 1.6, "z": 1.6}
                },
                "snap_to_ground": False
            }))
            execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": flora[g_idx % len(flora)],
                "name": f"Grove_{g_idx}_Flora",
                "xform": {
                    "location": {"x": cx - 600.0, "y": cy + 300.0, "z": 0.0},
                    "rotation": {"pitch": 0.0, "yaw": float((g_idx * 29) % 360), "roll": 0.0},
                    "scale": {"x": 1.8, "y": 1.8, "z": 1.8}
                },
                "snap_to_ground": False
            }))
            spawned += 2
            g_idx += 1

    return {"status": "ok", "grove_count": g_idx, "flora_spawned": spawned}
'''
    res4 = run_editor_script(batch4_script)
    print("  - Batch 4 Result:", res4[0]["text"] if res4 else "None")

    # -------------------------------------------------------------
    # BATCH 5: Cobblestone Road Network & 4 Regional POI Landmarks
    # -------------------------------------------------------------
    print("[7/7] Placing Cobblestone Highway, Waypoint Lampposts & 4 Regional Landmarks...")
    batch5_script = '''
import json

def run():
    spawned = 0
    tile_asset = "/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile"
    lamp_asset = "/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Lamppost"

    road_distances = [6000.0, 18000.0, 32000.0, 48000.0, 65000.0, 85000.0, 105000.0]
    for idx, d in enumerate(road_distances):
        # North Road
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": tile_asset,
            "name": f"RoadTile_N_{idx}",
            "xform": {"location": {"x": 0.0, "y": d, "z": 5.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 2.8, "y": 2.8, "z": 1.0}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": lamp_asset,
            "name": f"RoadLamp_N_{idx}",
            "xform": {"location": {"x": 400.0, "y": d, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0}, "scale": {"x": 1.6, "y": 1.6, "z": 1.6}},
            "snap_to_ground": False
        }))

        # South Road
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": tile_asset,
            "name": f"RoadTile_S_{idx}",
            "xform": {"location": {"x": 0.0, "y": -d, "z": 5.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 2.8, "y": 2.8, "z": 1.0}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": lamp_asset,
            "name": f"RoadLamp_S_{idx}",
            "xform": {"location": {"x": -400.0, "y": -d, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": -90.0, "roll": 0.0}, "scale": {"x": 1.6, "y": 1.6, "z": 1.6}},
            "snap_to_ground": False
        }))

        # East Road
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": tile_asset,
            "name": f"RoadTile_E_{idx}",
            "xform": {"location": {"x": d, "y": 0.0, "z": 5.0}, "rotation": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0}, "scale": {"x": 2.8, "y": 2.8, "z": 1.0}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": lamp_asset,
            "name": f"RoadLamp_E_{idx}",
            "xform": {"location": {"x": d, "y": 400.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 1.6, "y": 1.6, "z": 1.6}},
            "snap_to_ground": False
        }))

        # West Road
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": tile_asset,
            "name": f"RoadTile_W_{idx}",
            "xform": {"location": {"x": -d, "y": 0.0, "z": 5.0}, "rotation": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0}, "scale": {"x": 2.8, "y": 2.8, "z": 1.0}},
            "snap_to_ground": False
        }))
        execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
            "asset_path": lamp_asset,
            "name": f"RoadLamp_W_{idx}",
            "xform": {"location": {"x": -d, "y": -400.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 180.0, "roll": 0.0}, "scale": {"x": 1.6, "y": 1.6, "z": 1.6}},
            "snap_to_ground": False
        }))
        spawned += 8

    # Zone 1: North Ruined Citadel
    ny = 85000.0
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1",
        "name": "POI_Citadel_Wall",
        "xform": {"location": {"x": 0.0, "y": ny + 4000.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 4.5, "y": 4.5, "z": 4.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1",
        "name": "POI_Citadel_Tower_L",
        "xform": {"location": {"x": -1800.0, "y": ny, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 25.0, "roll": 0.0}, "scale": {"x": 2.2, "y": 2.2, "z": 2.5}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenConstruction1",
        "name": "POI_Citadel_Tower_R",
        "xform": {"location": {"x": 1800.0, "y": ny, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": -25.0, "roll": 0.0}, "scale": {"x": 2.2, "y": 2.2, "z": 2.5}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl",
        "name": "POI_Citadel_Brazier",
        "xform": {"location": {"x": 0.0, "y": ny, "z": 20.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 2.6, "y": 2.6, "z": 2.6}},
        "snap_to_ground": False
    }))
    spawned += 4

    # Zone 2: South Sunken Crypt
    sy = -85000.0
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
        "name": "POI_Necro_Megalith1",
        "xform": {"location": {"x": -1500.0, "y": sy, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 45.0, "roll": 0.0}, "scale": {"x": 3.0, "y": 3.0, "z": 4.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock2",
        "name": "POI_Necro_Megalith2",
        "xform": {"location": {"x": 1500.0, "y": sy, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": -45.0, "roll": 0.0}, "scale": {"x": 3.0, "y": 3.0, "z": 4.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_Cart",
        "name": "POI_Necro_WreckedCart",
        "xform": {"location": {"x": 0.0, "y": sy + 800.0, "z": 0.0}, "rotation": {"pitch": 15.0, "yaw": 30.0, "roll": 10.0}, "scale": {"x": 1.8, "y": 1.8, "z": 1.8}},
        "snap_to_ground": False
    }))
    spawned += 3

    # Zone 3: East Megalith Sanctuary
    ex = 85000.0
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "name": "POI_East_Gate_L",
        "xform": {"location": {"x": ex, "y": -2200.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": -20.0, "roll": 0.0}, "scale": {"x": 3.2, "y": 3.2, "z": 4.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3",
        "name": "POI_East_Gate_R",
        "xform": {"location": {"x": ex, "y": 2200.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 20.0, "roll": 0.0}, "scale": {"x": 3.2, "y": 3.2, "z": 4.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": tile_asset,
        "name": "POI_East_Altar",
        "xform": {"location": {"x": ex, "y": 0.0, "z": 15.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 4.0, "y": 4.0, "z": 1.8}},
        "snap_to_ground": False
    }))
    spawned += 3

    # Zone 4: West Dread Morass
    wx = -85000.0
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1",
        "name": "POI_West_Mycelium1",
        "xform": {"location": {"x": wx, "y": -1200.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 30.0, "roll": 0.0}, "scale": {"x": 3.0, "y": 3.0, "z": 3.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Mycelium1",
        "name": "POI_West_Mycelium2",
        "xform": {"location": {"x": wx + 800.0, "y": 1400.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": -60.0, "roll": 0.0}, "scale": {"x": 2.5, "y": 2.5, "z": 2.5}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock3",
        "name": "POI_West_Boulder",
        "xform": {"location": {"x": wx - 500.0, "y": 0.0, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 90.0, "roll": 0.0}, "scale": {"x": 3.5, "y": 3.5, "z": 3.5}},
        "snap_to_ground": False
    }))
    spawned += 3

    # 3. Dungeon Atlas Slot 1
    dungeon_x = 500000.0
    for rx in range(-2, 3):
        for ry in range(-2, 3):
            execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
                "asset_path": tile_asset,
                "name": f"DungeonAtlas_Floor_{rx}_{ry}",
                "xform": {"location": {"x": dungeon_x + rx * 240, "y": ry * 240, "z": 0.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 1.2, "y": 1.2, "z": 1.0}},
                "snap_to_ground": False
            }))
            spawned += 1

    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "name": "DungeonAtlas_WallN",
        "xform": {"location": {"x": dungeon_x, "y": 700.0, "z": -20.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 1.8, "y": 1.8, "z": 2.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
        "name": "DungeonAtlas_WallS",
        "xform": {"location": {"x": dungeon_x, "y": -700.0, "z": -20.0}, "rotation": {"pitch": 0.0, "yaw": 180.0, "roll": 0.0}, "scale": {"x": 1.8, "y": 1.8, "z": 2.0}},
        "snap_to_ground": False
    }))
    execute_tool("editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset", json.dumps({
        "asset_path": "/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl",
        "name": "DungeonAtlas_Portal",
        "xform": {"location": {"x": dungeon_x, "y": 0.0, "z": 15.0}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 1.4, "y": 1.4, "z": 1.4}},
        "snap_to_ground": False
    }))
    spawned += 3

    return {"status": "ok", "roads_landmarks_spawned": spawned}
'''
    res5 = run_editor_script(batch5_script)
    print("  - Batch 5 Result:", res5[0]["text"] if res5 else "None")

    # -------------------------------------------------------------
    # BATCH 6: Save Level & Synchronize
    # -------------------------------------------------------------
    print("Saving all open world assets...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})
    print("Dense 3km x 3km Open World Level successfully generated and saved!")

if __name__ == "__main__":
    build_dense_world()

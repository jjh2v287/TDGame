import os
import sys
import json
sys.path.insert(0, os.path.dirname(__file__))
from unreal_mcp import UnrealMcpClient

def build_ash_ruin_world():
    client = UnrealMcpClient()
    client.initialize()
    print("[1/6] Connected to Unreal Editor MCP.")

    # 1. Verify / Load Active Level
    level_path = "/Game/Level/LV_DarkFantasy_OpenWorld"
    client.call_tool("editor_toolset.toolsets.scene.SceneTools", "load_level", {"level_path": level_path})
    print(f"[2/6] Active level verified: {level_path}")

    # 2. Query Existing Level Actors
    actors = json.loads(client.call_tool("editor_toolset.toolsets.scene.SceneTools", "find_actors", {"name": "", "tag": "", "collision_channels": []})[0]["text"])["returnValue"]

    dir_light = next((a for a in actors if "DirectionalLight" in a["refPath"]), None)
    height_fog = next((a for a in actors if "ExponentialHeightFog" in a["refPath"]), None)
    sky_light = next((a for a in actors if "SkyLight" in a["refPath"]), None)
    floor_actor = next((a for a in actors if "Floor" in a["refPath"]), None)
    ppv_actor = next((a for a in actors if "PostProcessVolume" in a["refPath"]), None)

    # 3. Re-tune Lighting for "Ashen Ruin" (Bright, High Visibility, Soft Grey Day Haze)
    print("[3/6] Applying Ashen Ruin Lighting & Clear Top-Down Visibility...")

    # 3-1. Directional Light: 3.0 Lux (Clear, pale silvery daylight, soft shadows)
    if dir_light:
        client.call_tool("editor_toolset.toolsets.actor.ActorTools", "set_actor_transform", {
            "actor": dir_light,
            "xform": {
                "location": {"x": 0.0, "y": 0.0, "z": 1000.0},
                "rotation": {"pitch": -50.0, "yaw": -45.0, "roll": 0.0},
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
                        "intensity": 3.0,
                        "lightColor": {"r": 0.88, "g": 0.90, "b": 0.93, "a": 1.0},
                        "bCastVolumetricShadow": True,
                        "volumetricScatteringIntensity": 1.0
                    })
                })
        print("  - DirectionalLight: Intensity 3.0 lux, Pale Silvery Daylight.")

    # 3-2. SkyLight: 1.2 Lux (Open, readable shadows without crushed blacks)
    if sky_light:
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": sky_light})[0]["text"])["returnValue"]
        for c in comps:
            if "LightComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "intensity": 1.2,
                        "lightColor": {"r": 0.74, "g": 0.76, "b": 0.80, "a": 1.0}
                    })
                })
        print("  - SkyLight: Intensity 1.2 lux, Soft Grey Ambient (Open Shadows).")

    # 3-3. ExponentialHeightFog: Very light, transparent grey haze (0.012 density)
    if height_fog:
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": height_fog})[0]["text"])["returnValue"]
        for c in comps:
            if "HeightFogComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "fogDensity": 0.012,
                        "fogHeightFalloff": 0.06,
                        "bEnableVolumetricFog": True,
                        "volumetricFogScatteringDistribution": 0.25,
                        "volumetricFogExtinctionScale": 0.7,
                        "volumetricFogAlbedo": {"r": 0.70, "g": 0.72, "b": 0.75, "a": 1.0}
                    })
                })
        print("  - HeightFog: Density 0.012, Transparent Ashen Mist.")

    # 3-4. PostProcessVolume: Minimal Vignette (0.15), Desaturated Grey Tone (0.65), Gentle Contrast (1.05)
    if ppv_actor:
        client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
            "instance": ppv_actor,
            "values": json.dumps({
                "bUnbound": True,
                "settings": {
                    "bOverride_VignetteIntensity": True,
                    "vignetteIntensity": 0.15,
                    "bOverride_ColorSaturation": True,
                    "colorSaturation": {"x": 0.65, "y": 0.65, "z": 0.70, "w": 1.0},
                    "bOverride_ColorContrast": True,
                    "colorContrast": {"x": 1.05, "y": 1.05, "z": 1.06, "w": 1.0},
                    "bOverride_FilmGrainIntensity": True,
                    "filmGrainIntensity": 0.06,
                    "bOverride_AutoExposureMinBrightness": True,
                    "autoExposureMinBrightness": 1.0,
                    "bOverride_AutoExposureMaxBrightness": True,
                    "autoExposureMaxBrightness": 1.0,
                    "bOverride_BloomIntensity": True,
                    "bloomIntensity": 0.3
                }
            })
        })
        print("  - PostProcess: Vignette 0.15, Desaturation 0.65, Clear Constant Exposure.")

    # 3-5. Floor: Ashen Grey Soil Material
    if floor_actor:
        comps = json.loads(client.call_tool("editor_toolset.toolsets.actor.ActorTools", "get_components", {"actor": floor_actor})[0]["text"])["returnValue"]
        for c in comps:
            if "StaticMeshComponent" in c["refPath"]:
                client.call_tool("editor_toolset.toolsets.object.ObjectTools", "set_properties", {
                    "instance": c,
                    "values": json.dumps({
                        "overrideMaterials": [{"refPath": "/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_GraySoil"}]
                    })
                })
        print("  - Floor Material: MI_GraySoil (Bleak Ashen Earth).")

    # 4. Clean Up Impaled Skulls for a cleaner, monumental stone ruin aesthetic
    print("[4/6] Removing excessive impaled skull props for ancient ruin look...")
    for a in actors:
        if "StickedSkull" in a["refPath"]:
            client.call_tool("editor_toolset.toolsets.scene.SceneTools", "remove_from_scene", {"actor": a})

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

    def spawn_point_light(name, loc, intensity=25.0, color=(0.85, 0.90, 1.0), radius=600.0):
        res = client.call_tool("editor_toolset.toolsets.scene.SceneTools", "add_to_scene_from_class", {
            "actor_type": {"refPath": "/Script/Engine.PointLight"},
            "name": name,
            "xform": {"location": {"x": float(loc[0]), "y": float(loc[1]), "z": float(loc[2])}, "rotation": {"pitch": 0.0, "yaw": 0.0, "roll": 0.0}, "scale": {"x": 1.0, "y": 1.0, "z": 1.0}},
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

    # 5. Build Ruined Stone Megaliths & Ancient Dungeon Entrance Portal in Outdoor Field
    print("[5/6] Building Ancient Ashen Ruin Portal & Monuments (Field: 0, 0, 0)...")

    # Ancient Stone Monolith Pillars around Sanctuary
    monoliths = [
        (350, 250, 0, 15, (1.6, 1.6, 2.0)),
        (350, -250, 0, -25, (1.5, 1.5, 1.9)),
        (-350, 250, 0, 45, (1.7, 1.7, 2.1)),
        (-350, -250, 0, -40, (1.6, 1.6, 2.0))
    ]
    for idx, (mx, my, mz, myaw, mscale) in enumerate(monoliths):
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1", f"AshenMonolith_{idx}", (mx, my, mz), (0, myaw, 0), mscale)

    # Ancient Dungeon Portal Gateway at (700, 0, 0)
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "DungeonGate_Left", (680, -220, -10), (0, -30, 0), (1.4, 1.4, 1.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "DungeonGate_Right", (680, 220, -10), (0, 30, 0), (1.4, 1.4, 1.8))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile", "DungeonPortal_Floor", (680, 0, 5), (0, 0, 0), (2.0, 2.0, 1.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "DungeonPortal_Brazier", (680, 0, 20), (0, 0, 0), (1.5, 1.5, 1.5))
    spawn_point_light("PortalGlowLight", (680, 0, 80), intensity=35.0, color=(0.4, 0.7, 1.0), radius=900.0)

    # 6. Build Dungeon Atlas Slot 1 (3km / 300,000 units away, Per Design Doc R-04, R-50)
    print("  - Building Dungeon Atlas Slot 1 (X: 300,000, Y: 0, Z: 0)...")
    dungeon_origin_x = 300000.0
    dungeon_origin_y = 0.0

    # Dungeon Room 1: Entrance Foyer Platform (Stone Floor 3x3)
    for rx in range(-2, 3):
        for ry in range(-2, 3):
            spawn_asset(
                "/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile",
                f"Dungeon1_Floor_{rx}_{ry}",
                (dungeon_origin_x + rx * 240, dungeon_origin_y + ry * 240, 0),
                (0, 0, 0),
                (1.2, 1.2, 1.0)
            )

    # Dungeon Room 1 Perimeter Walls
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "Dungeon1_NorthWall", (dungeon_origin_x, dungeon_origin_y + 700, -20), (0, 0, 0), (1.8, 1.8, 2.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2", "Dungeon1_SouthWall", (dungeon_origin_x, dungeon_origin_y - 700, -20), (0, 180, 0), (1.8, 1.8, 2.0))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff3", "Dungeon1_WestWall", (dungeon_origin_x - 700, dungeon_origin_y, -20), (0, 90, 0), (1.8, 1.8, 2.0))

    # Dungeon Room 1 Portal Altar & Light
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "Dungeon1_ReturnPortal", (dungeon_origin_x, dungeon_origin_y, 15), (0, 0, 0), (1.4, 1.4, 1.4))
    spawn_point_light("Dungeon1_PortalLight", (dungeon_origin_x, dungeon_origin_y, 75), intensity=30.0, color=(0.4, 0.7, 1.0), radius=800.0)

    # Dungeon Connecting Corridor
    for step in range(1, 5):
        cx = dungeon_origin_x + 600 + step * 250
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile", f"Dungeon1_Corridor_{step}", (cx, dungeon_origin_y, 0), (0, 0, 0), (1.2, 1.2, 1.0))
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1", f"CorridorPillar_L_{step}", (cx, dungeon_origin_y + 180, 0), (0, 45, 0), (1.2, 1.2, 1.6))
        spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1", f"CorridorPillar_R_{step}", (cx, dungeon_origin_y - 180, 0), (0, -45, 0), (1.2, 1.2, 1.6))

    # Dungeon Room 2: Boss Sanctuary Chamber (X: 302,200, Y: 0)
    boss_room_x = dungeon_origin_x + 2200.0
    for bx in range(-3, 4):
        for by in range(-3, 4):
            spawn_asset(
                "/Game/DarkFantasyTopDown/StaticMeshes/RocksTIle/SM_RocksTile",
                f"BossRoom_Floor_{bx}_{by}",
                (boss_room_x + bx * 240, dungeon_origin_y + by * 240, 0),
                (0, 0, 0),
                (1.2, 1.2, 1.0)
            )

    # Boss Room Walls & Columns
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1", "BossRoom_NorthWall", (boss_room_x, dungeon_origin_y + 950, -20), (0, 0, 0), (2.0, 2.0, 2.4))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1", "BossRoom_SouthWall", (boss_room_x, dungeon_origin_y - 950, -20), (0, 180, 0), (2.0, 2.0, 2.4))
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliffs1", "BossRoom_EastWall", (boss_room_x + 950, dungeon_origin_y, -20), (0, -90, 0), (2.0, 2.0, 2.4))

    # Boss Dais & Sconces
    spawn_asset("/Game/DarkFantasyTopDown/StaticMeshes/Lanterns/SM_Bowl", "BossAltarBrazier", (boss_room_x + 400, dungeon_origin_y, 25), (0, 0, 0), (1.8, 1.8, 1.8))
    spawn_point_light("BossAltarFire", (boss_room_x + 400, dungeon_origin_y, 90), intensity=40.0, color=(1.0, 0.45, 0.15), radius=1200.0)

    # 7. Save All Changes
    print("[6/6] Saving level assets...")
    client.call_tool("editor_toolset.toolsets.asset.AssetTools", "save_assets", {"asset_paths": []})

    # Duplicate to official design doc path /Game/World/Maps/L_TDWorld_Main if not yet duplicated
    has_main_map = json.loads(client.call_tool("editor_toolset.toolsets.asset.AssetTools", "exists", {"path": "/Game/World/Maps/L_TDWorld_Main"})[0]["text"])["returnValue"]
    if not has_main_map:
        client.call_tool("editor_toolset.toolsets.asset.AssetTools", "duplicate", {
            "path": level_path,
            "new_path": "/Game/World/Maps/L_TDWorld_Main"
        })
        print("  - Duplicated level to design-doc standard: /Game/World/Maps/L_TDWorld_Main")

    print("Ash & Ruin World + Dungeon Atlas generation finished successfully!")

if __name__ == "__main__":
    build_ash_ruin_world()

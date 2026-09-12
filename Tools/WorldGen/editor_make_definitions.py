"""에디터 안에서 실행: 설계서 정의 데이터 에셋을 만든다(P3-01, P2-02, P1-03).

만드는 에셋(이미 있으면 값만 갱신):
  /Game/World/Definitions/DA_TDWorld_Main, DA_TDRegion_Forest, DA_TDBiome_Forest, DA_TDDungeonAtlas_Main
  /Game/POI/Definitions/DA_TDPoi_Camp, DA_TDPoi_Shrine, DA_TDPoi_Ruin, DA_TDPoi_Graveyard
  /Game/Dungeon/Themes/DA_TDTheme_Crypt, /Game/Dungeon/Flows/DA_TDFlow_Linear|Branch|Loop|Hub|KeyLock
그리고 프로젝트 설정 TD World Generation 의 DefaultDungeonAtlas / DefaultWorldDefinition 을 채운다.
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_make_definitions.py
"""
import unreal

EAL = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MESH = "/Game/DarkFantasyTopDown/StaticMeshes"


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def data_asset(path, cls):
    folder, name = path.rsplit("/", 1)
    ensure_dir(folder)
    asset = EAL.load_asset(path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        asset = tools.create_asset(name, folder, cls, factory)
    return asset


def soft(asset):
    return asset


def mesh_list(names):
    out = []
    for n in names:
        m = EAL.load_asset(f"{MESH}/{n}")
        if m:
            out.append(m)
    return out


pois = {}
for pid, kind, radius, clear, combat in (("Camp", unreal.TDPoiKind.CAMP, 2400.0, 1200.0, False), ("Shrine", unreal.TDPoiKind.SHRINE, 2200.0, 1000.0, False), ("Ruin", unreal.TDPoiKind.RUIN, 2200.0, 1200.0, True), ("Graveyard", unreal.TDPoiKind.GRAVEYARD, 4000.0, 2000.0, True)):
    a = data_asset(f"/Game/POI/Definitions/DA_TDPoi_{pid}", unreal.TDPoiArchetype)
    a.set_editor_property("poi_id", pid)
    a.set_editor_property("kind", kind)
    a.set_editor_property("exclusion_radius_cm", radius)
    a.set_editor_property("required_clear_radius_cm", clear)
    a.set_editor_property("requires_combat_space", combat)
    EAL.save_loaded_asset(a)
    pois[pid] = a

biome = data_asset("/Game/World/Definitions/DA_TDBiome_Forest", unreal.TDBiomeDefinition)
biome.set_editor_property("biome_id", "Forest")
biome.set_editor_property("tree_set", mesh_list(["Birch/SM_Birch1", "Birch/SM_Birch2", "Birch/SM_Birch3"]))
biome.set_editor_property("rock_set", mesh_list(["Rocks2/SM_Rock1", "Rocks2/SM_Rock2", "Rocks2/SM_Rock3", "Nature/Cliff/SM_Cliff2", "Nature/Cliff/SM_Cliff4"]))
biome.set_editor_property("bush_set", mesh_list(["Bush/SM_Bush", "Nature/Dreadplants/SM_Dreadplant1", "Nature/Dreadplants/SM_Dreadplant2"]))
biome.set_editor_property("ground_clutter_set", mesh_list(["SmallProps/SM_Skull", "SmallProps/SM_Bone", "Nature/Rocks/SmallRocks/SM_Rock_4"]))
biome.set_editor_property("tree_density_per100_sq_m", 8.0)
biome.set_editor_property("min_tree_distance_cm", 370.0)
biome.set_editor_property("max_slope_deg", 30.0)
biome.set_editor_property("road_clearance_cm", 800.0)
biome.set_editor_property("poi_clearance_cm", 1000.0)
EAL.save_loaded_asset(biome)

region = data_asset("/Game/World/Definitions/DA_TDRegion_Forest", unreal.TDRegionDefinition)
region.set_editor_property("region_id", "Forest")
region.set_editor_property("biome", soft(biome))
region.set_editor_property("town_count", 1)
region.set_editor_property("main_dungeon_count", 1)
region.set_editor_property("side_dungeon_count", 3)
quotas = []
for pid, rng in (("Camp", (2, 3)), ("Shrine", (2, 3)), ("Ruin", (2, 3)), ("Graveyard", (1, 2))):
    q = unreal.TDPoiQuota()
    q.set_editor_property("archetype", soft(pois[pid]))
    q.set_editor_property("count_range", unreal.IntPoint(rng[0], rng[1]))
    quotas.append(q)
region.set_editor_property("poi_quotas", quotas)
region.set_editor_property("event_area_range", unreal.IntPoint(2, 4))
region.set_editor_property("arena_range", unreal.IntPoint(2, 3))
region.set_editor_property("min_entrance_spacing_cm", 15000.0)
region.set_editor_property("min_poi_spacing_cm", 4000.0)
EAL.save_loaded_asset(region)

theme = data_asset("/Game/Dungeon/Themes/DA_TDTheme_Crypt", unreal.TDDungeonTheme)
theme.set_editor_property("theme_id", "Crypt")
theme.set_editor_property("cell_size_cm", 400)
prev_levels = {str(m.get_editor_property("module_id")): m.get_editor_property("level_asset") for m in theme.get_editor_property("modules")}
theme.fill_crypt_placeholder_modules()
relinked = list(theme.get_editor_property("modules"))
for m in relinked:
    prev = prev_levels.get(str(m.get_editor_property("module_id")))
    if prev is not None:
        m.set_editor_property("level_asset", prev)
theme.set_editor_property("modules", relinked)
EAL.save_loaded_asset(theme)

flows = {}
for name, kind in (("Linear", unreal.TDDungeonFlowKind.LINEAR), ("Branch", unreal.TDDungeonFlowKind.BRANCH), ("Loop", unreal.TDDungeonFlowKind.LOOP), ("Hub", unreal.TDDungeonFlowKind.HUB), ("KeyLock", unreal.TDDungeonFlowKind.KEY_LOCK)):
    f = data_asset(f"/Game/Dungeon/Flows/DA_TDFlow_{name}", unreal.TDDungeonFlowTemplate)
    f.apply_kind_defaults(kind)
    EAL.save_loaded_asset(f)
    flows[name] = f

atlas = data_asset("/Game/World/Definitions/DA_TDDungeonAtlas_Main", unreal.TDDungeonAtlasDefinition)
atlas.set_editor_property("origin_cm", unreal.Vector(300000.0, 300000.0, 0.0))
atlas.set_editor_property("slot_pitch_cm", 30000.0)
atlas.set_editor_property("columns", 4)
atlas.set_editor_property("loading_range_cm", 12800.0)
existing_slots = {str(s.get_editor_property("dungeon_id")): s for s in atlas.get_editor_property("slots")}
slots = []
for idx, (did, flow, size, seed, ret) in enumerate((("MainCrypt", "KeyLock", unreal.TDDungeonSize.MEDIUM, 7, (29600.0, -26800.0, 2133.0)), ("HollowCave", "Branch", unreal.TDDungeonSize.SMALL, 3, (-37200.0, -26200.0, 500.0)), ("SunkenCrypt", "Loop", unreal.TDDungeonSize.MEDIUM, 5, (31800.0, 41200.0, 250.0)))):
    s = unreal.TDDungeonSlot()
    s.set_editor_property("dungeon_id", did)
    s.set_editor_property("slot_index", idx)
    origin = atlas.get_slot_origin_cm(idx)
    s.set_editor_property("world_transform", unreal.Transform(location=origin, rotation=unreal.Rotator(0, 0, 0), scale=unreal.Vector(1, 1, 1)))
    s.set_editor_property("theme", soft(theme))
    s.set_editor_property("flow_template", soft(flows[flow]))
    s.set_editor_property("size", size)
    s.set_editor_property("seed", seed)
    s.set_editor_property("field_return_transform", unreal.Transform(location=unreal.Vector(ret[0], ret[1], ret[2] + 120.0), rotation=unreal.Rotator(0, 0, 0), scale=unreal.Vector(1, 1, 1)))
    prev = existing_slots.get(did)
    if prev is not None:
        for keep in ("entry_transform", "exit_transform", "bounds", "validation_passed", "generator_version"):
            s.set_editor_property(keep, prev.get_editor_property(keep))
    slots.append(s)
atlas.set_editor_property("slots", slots)
EAL.save_loaded_asset(atlas)

world_def = data_asset("/Game/World/Definitions/DA_TDWorld_Main", unreal.TDWorldDefinition)
world_def.set_editor_property("world_id", "Main")
world_def.set_editor_property("regions", [soft(region)])
world_def.set_editor_property("dungeon_atlas", soft(atlas))
world_def.set_editor_property("field_bounds_cm", unreal.Box2D(unreal.Vector2D(-50400.0, -50400.0), unreal.Vector2D(50400.0, 50400.0)))
anchors = []
for aid, kind, loc, radius, yaw in (("Town", unreal.TDWorldAnchorKind.TOWN, (-11000.0, 2000.0, 520.0), 10000.0, 0.0), ("MainDungeon", unreal.TDWorldAnchorKind.MAIN_DUNGEON, (29600.0, -26800.0, 2133.0), 3000.0, 135.0), ("PlayerStart", unreal.TDWorldAnchorKind.PLAYER_START, (-10600.0, 2600.0, 530.0), 0.0, 0.0), ("Graveyard", unreal.TDWorldAnchorKind.LANDMARK, (1000.0, -33000.0, 366.0), 4000.0, 0.0)):
    a = unreal.TDWorldAnchor()
    a.set_editor_property("anchor_id", aid)
    a.set_editor_property("yaw_deg", yaw)
    a.set_editor_property("kind", kind)
    a.set_editor_property("location_cm", unreal.Vector(*loc))
    a.set_editor_property("exclusion_radius_cm", radius)
    a.set_editor_property("locked", True)
    anchors.append(a)
world_def.set_editor_property("hand_authored_anchors", anchors)
EAL.save_loaded_asset(world_def)

if hasattr(unreal, "TDWorldGenSettings"):
    settings = unreal.get_default_object(unreal.TDWorldGenSettings)
    settings.set_editor_property("default_dungeon_atlas", soft(atlas))
    settings.set_editor_property("default_world_definition", soft(world_def))
    try:
        settings.save_config()
    except Exception as e:
        print("settings save_config skipped:", e)
else:
    print("TDWorldGenSettings not exposed to Python; set Config/DefaultGame.ini [/Script/TDWorldGen.TDWorldGenSettings] DefaultDungeonAtlas manually (Tools/ue_editor.py does not do this)")
print("definitions created:", [a.get_path_name() for a in (biome, region, theme, atlas, world_def)] + [p.get_path_name() for p in pois.values()] + [f.get_path_name() for f in flows.values()])

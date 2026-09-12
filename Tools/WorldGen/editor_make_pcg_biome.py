"""P3-07 Forest 바이옴 PCG 그래프 생성 + 레벨 볼륨 배치 + Normal 모드 생성 시작.

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_make_pcg_biome.py
생성물: /Game/World/PCG/PCG_TDBiome_Forest(메인) + 서브그래프 5종, /Game/Dungeon/PCG/PCG_TDRoomDressing,
        레벨 액터 TDGen_PCG_BiomeForest(PCGVolume, 폴더 TDGen/PCG, 파티션 + HiGen).
재실행: 같은 라벨 액터는 cleanup 후 삭제, 같은 경로 에셋은 삭제 후 재생성.
파라미터 값은 DA_TDBiome_Forest에서 읽어 노드 프로퍼티에 넣는다(그래프 사용자 파라미터는 Python에서 선언 불가 → C++ 주입 필요).
생성은 비동기라 스크립트 종료 후 진행된다. 결과 집계는 editor_check_pcg_determinism.py.
"""
import math
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

PCG_DIR = "/Game/World/PCG"
DUNGEON_PCG_DIR = "/Game/Dungeon/PCG"
MESH_ROOT = "/Game/DarkFantasyTopDown/StaticMeshes"
BIOME_DEF_PATH = "/Game/World/Definitions/DA_TDBiome_Forest"
VOLUME_LABEL = "TDGen_PCG_BiomeForest"
VOLUME_FOLDER = "TDGen/PCG"
COMPONENT_SEED = 7

BASE_POINTS_PER_SQM = 0.04
POI_KEEP_FRACTION = 0.6
TREE_POINTS_PER_SQM_MAX = 0.0004
ROAD_WEIGHT_OPEN_MAX = 0.15
ROAD_WEIGHT_EDGE_MIN = 0.05

MESHES = {
    "bush": [("Bush/SM_Bush", 3), ("Nature/Dreadplants/SM_Dreadplant1", 2), ("Nature/Dreadplants/SM_Dreadplant2", 2),
             ("Nature/Dreadplants/SM_DreadplantMushroom1", 1), ("Nature/Dreadplants/SM_Mycelium1", 1)],
    "tree": [("Birch/SM_Birch1", 2), ("Birch/SM_Birch2", 2), ("Birch/SM_Birch3", 1)],
    "rock": [("Nature/Rocks/SmallRocks/SM_Rock_3", 3), ("Nature/Rocks/SmallRocks/SM_Rock_5", 3),
             ("Nature/Rocks/SmallRocks/SM_Rock_7", 2), ("Rocks2/SM_Rock1", 1)],
    "clutter": [("SmallProps/SM_Bone", 3), ("SmallProps/SM_Skull", 1), ("SmallProps/SM_WoodenStick", 4),
                ("WoodenParts/SM_WoodenPart1", 1)],
    "roadside": [("Nature/Rocks/SmallRocks/SM_Rock_4", 3), ("Nature/Rocks/SmallRocks/SM_Rock_6", 2),
                 ("SmallProps/SM_WoodenStick", 2)],
    "poi": [("Barrels/SM_Barrel1_Empty", 2), ("Containers/SM_WoodenCrate2", 2), ("SmallProps/SM_Skull", 1),
            ("WoodenParts/SM_WoodenPart3", 1), ("Props1/SM_Bucket", 1)],
    "room": [("Barrels/SM_Barrel1", 3), ("Containers/SM_WoodenCrate", 2), ("SmallProps/SM_Bone", 2),
             ("SmallProps/SM_RamSkull", 1)],
}


def log(msg):
    print(f"[TDTool {time.time() - t0:6.1f}s] {msg}")


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        log(f"property skip {name}: {str(e)[:100]}")
        return False


def read_biome_parameters():
    params = {"max_slope_deg": 30.0, "min_tree_distance_cm": 800.0, "poi_clearance_cm": 1500.0,
              "road_clearance_cm": 400.0, "tree_density_per100_sq_m": 0.1}
    definition = EAL.load_asset(BIOME_DEF_PATH)
    if definition is None:
        log("biome definition missing, using defaults")
        return params, None
    for key in list(params.keys()):
        try:
            value = float(definition.get_editor_property(key))
            if value > 0:
                params[key] = value
        except Exception as e:
            log(f"definition read skip {key}: {str(e)[:80]}")
    return params, definition


def clear_graph(graph):
    nodes = list(graph.get_editor_property("nodes"))
    for node in nodes:
        graph.remove_node(node)
    log(f"reused {graph.get_name()} (delete refused): removed {len(nodes)} nodes")


def create_graph(name, folder):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        if EAL.delete_asset(path):
            log(f"deleted {path}")
        else:
            graph = EAL.load_asset(path)
            clear_graph(graph)
            return graph
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    graph = tools.create_asset(name, folder, unreal.PCGGraph, unreal.PCGGraphFactory())
    if graph is None:
        raise RuntimeError(f"create_asset failed: {folder}/{name}")
    return graph


class GraphBuilder:
    def __init__(self, graph):
        self.graph = graph
        self.input = graph.get_input_node()
        self.output = graph.get_output_node()
        self.input.set_node_position(-600, 0)
        self.output.set_node_position(2400, 0)
        self.column = 0

    def add(self, settings_class, x, y, **props):
        node, settings = self.graph.add_node_of_type(settings_class)
        node.set_node_position(x, y)
        for key, value in props.items():
            setp(settings, key, value)
        return node, settings

    def link(self, from_node, from_pin, to_node, to_pin):
        self.graph.add_edge(from_node, from_pin, to_node, to_pin)


def load_mesh(rel):
    mesh = EAL.load_asset(f"{MESH_ROOT}/{rel}")
    if mesh is None:
        log(f"mesh missing: {rel}")
    return mesh


def make_weighted_entries(kind, cull_end_cm):
    entries = []
    for rel, weight in MESHES[kind]:
        mesh = load_mesh(rel)
        if mesh is None:
            continue
        descriptor = unreal.PCGSoftISMComponentDescriptor()
        descriptor.set_editor_property("static_mesh", mesh)
        descriptor.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        setp(descriptor, "instance_start_cull_distance", int(cull_end_cm * 0.85))
        setp(descriptor, "instance_end_cull_distance", int(cull_end_cm))
        entry = unreal.PCGMeshSelectorWeightedEntry()
        entry.set_editor_property("descriptor", descriptor)
        entry.set_editor_property("weight", weight)
        entries.append(entry)
    return entries


def add_spawner(builder, x, y, kind, cull_end_cm=12000):
    node, settings = builder.add(unreal.PCGStaticMeshSpawnerSettings, x, y)
    settings.set_editor_property("mesh_selector_type", unreal.PCGMeshSelectorWeighted)
    selector = settings.get_editor_property("mesh_selector_parameters")
    selector.set_editor_property("mesh_entries", make_weighted_entries(kind, cull_end_cm))
    return node, settings


def add_attribute_filter(builder, x, y, attribute_name, operator, threshold):
    node, settings = builder.add(unreal.PCGAttributeFilteringSettings, x, y, operator=operator,
                                 use_constant_threshold=True, warn_on_data_missing_attribute=False)
    target = settings.get_editor_property("target_attribute")
    target.import_text(f"PCGBegin({attribute_name})PCGEnd")
    settings.set_editor_property("target_attribute", target)
    constant = settings.get_editor_property("attribute_types")
    constant.set_editor_property("type", unreal.PCGMetadataTypes.FLOAT)
    constant.set_editor_property("float_value", float(threshold))
    settings.set_editor_property("attribute_types", constant)
    return node, settings


def add_density_noise(builder, x, y):
    node, settings = builder.add(unreal.PCGAttributeNoiseSettings, x, y, mode=unreal.PCGAttributeNoiseMode.SET,
                                 noise_min=0.0, noise_max=1.0)
    source = settings.get_editor_property("input_source")
    source.import_text("PCGBegin($Density)PCGEnd")
    settings.set_editor_property("input_source", source)
    target = settings.get_editor_property("output_target")
    target.import_text("PCGBegin($Density)PCGEnd")
    settings.set_editor_property("output_target", target)
    return node, settings


def add_random_transform(builder, x, y, scale_min, scale_max):
    return builder.add(unreal.PCGTransformPointsSettings, x, y,
                       rotation_min=unreal.Rotator(0, 0, 0), rotation_max=unreal.Rotator(0, 360, 0),
                       scale_min=unreal.Vector(scale_min, scale_min, scale_min),
                       scale_max=unreal.Vector(scale_max, scale_max, scale_max), uniform_scale=True)


def add_self_pruning(builder, x, y):
    node, settings = builder.add(unreal.PCGSelfPruningSettings, x, y)
    parameters = settings.get_editor_property("parameters")
    parameters.set_editor_property("pruning_type", unreal.PCGSelfPruningType.LARGE_TO_SMALL)
    settings.set_editor_property("parameters", parameters)
    return node, settings


def add_landscape(builder, x, y):
    node, settings = builder.add(unreal.PCGGetLandscapeSettings, x, y)
    sampling = settings.get_editor_property("sampling_properties")
    sampling.set_editor_property("get_height_only", False)
    sampling.set_editor_property("get_layer_weights", True)
    settings.set_editor_property("sampling_properties", sampling)
    return node, settings


def add_actor_data(builder, x, y, settings_class, tag, mode):
    node, settings = builder.add(settings_class, x, y, mode=mode)
    selector = settings.get_editor_property("actor_selector")
    selector.set_editor_property("actor_filter", unreal.PCGActorFilter.ALL_WORLD_ACTORS)
    selector.set_editor_property("actor_selection", unreal.PCGActorSelection.BY_TAG)
    selector.set_editor_property("actor_selection_tag", tag)
    selector.set_editor_property("select_multiple", True)
    selector.set_editor_property("must_overlap_self", False)
    settings.set_editor_property("actor_selector", selector)
    return node, settings


def add_bounds_set(builder, x, y, half_extent_cm, z_half_cm=None):
    z = z_half_cm if z_half_cm is not None else half_extent_cm
    return builder.add(unreal.PCGBoundsModifierSettings, x, y, mode=unreal.PCGBoundsModifierMode.SET,
                       bounds_min=unreal.Vector(-half_extent_cm, -half_extent_cm, -z),
                       bounds_max=unreal.Vector(half_extent_cm, half_extent_cm, z))


def slope_density_threshold(max_slope_deg):
    return math.cos(math.radians(max_slope_deg))


def build_thin_and_spawn_chain(builder, first_node, first_pin, x0, y, keep_fraction, scale_min, scale_max, kind,
                               cull_end_cm=12000):
    noise, _ = add_density_noise(builder, x0, y)
    builder.link(first_node, first_pin, noise, "In")
    keep, _ = builder.add(unreal.PCGDensityFilterSettings, x0 + 300, y, lower_bound=1.0 - keep_fraction, upper_bound=1.0)
    builder.link(noise, "Out", keep, "In")
    prune, _ = add_self_pruning(builder, x0 + 600, y)
    builder.link(keep, "Out", prune, "In")
    transform, _ = add_random_transform(builder, x0 + 900, y, scale_min, scale_max)
    builder.link(prune, "Out", transform, "In")
    spawner, _ = add_spawner(builder, x0 + 1200, y, kind, cull_end_cm)
    builder.link(transform, "Out", spawner, "In")
    builder.link(spawner, "Out", builder.output, "Out")
    return spawner


def build_vegetation(graph):
    b = GraphBuilder(graph)
    rock_filter, _ = add_attribute_filter(b, -300, 0, "Rock", unreal.PCGAttributeFilterOperator.LESSER, 0.5)
    b.link(b.input, "In", rock_filter, "In")
    build_thin_and_spawn_chain(b, rock_filter, "InsideFilter", 0, 0, 0.15, 0.8, 1.25, "bush", 10000)


def build_rock_scatter(graph):
    b = GraphBuilder(graph)
    rock_filter, _ = add_attribute_filter(b, -300, 0, "Rock", unreal.PCGAttributeFilterOperator.GREATER, 0.2)
    b.link(b.input, "In", rock_filter, "In")
    build_thin_and_spawn_chain(b, rock_filter, "InsideFilter", 0, 0, 0.2, 0.6, 1.4, "rock", 14000)


def build_clutter(graph):
    b = GraphBuilder(graph)
    build_thin_and_spawn_chain(b, b.input, "In", 0, 0, 0.07, 0.9, 1.1, "clutter", 8000)


def build_roadside(graph):
    b = GraphBuilder(graph)
    edge_filter, _ = add_attribute_filter(b, -300, 0, "Road", unreal.PCGAttributeFilterOperator.GREATER, ROAD_WEIGHT_EDGE_MIN)
    b.link(b.input, "In", edge_filter, "In")
    build_thin_and_spawn_chain(b, edge_filter, "InsideFilter", 0, 0, 0.15, 0.7, 1.2, "roadside", 10000)


def build_poi_dressing(graph):
    b = GraphBuilder(graph)
    build_thin_and_spawn_chain(b, b.input, "In", 0, 0, POI_KEEP_FRACTION, 0.9, 1.1, "poi", 10000)


def build_room_dressing(graph):
    b = GraphBuilder(graph)
    volume, volume_settings = b.add(unreal.PCGGetVolumeSettings, -300, 0)
    selector = volume_settings.get_editor_property("actor_selector")
    selector.set_editor_property("actor_filter", unreal.PCGActorFilter.SELF)
    volume_settings.set_editor_property("actor_selector", selector)
    sampler, _ = b.add(unreal.PCGVolumeSamplerSettings, 0, 0, voxel_size=unreal.Vector(150, 150, 400))
    b.link(volume, "Out", sampler, "Volume")
    b.link(b.input, "In", sampler, "Bounding Shape")
    build_thin_and_spawn_chain(b, sampler, "Out", 300, 0, 0.12, 0.9, 1.1, "room", 6000)


def build_main(graph, subgraphs, params):
    b = GraphBuilder(graph)
    graph.set_editor_property("use_hierarchical_generation", True)
    graph.set_editor_property("hi_gen_grid_size", unreal.PCGHiGenGrid.GRID1024)
    slope_threshold = slope_density_threshold(params["max_slope_deg"])

    grid, _ = b.add(unreal.PCGHiGenGridSizeSettings, -600, 200, hi_gen_grid_size=unreal.PCGHiGenGrid.GRID256)
    b.link(b.input, "In", grid, "In")
    landscape, _ = add_landscape(b, -300, -300)
    b.link(grid, "Out", landscape, "BoundingShape")
    sampler, _ = b.add(unreal.PCGSurfaceSamplerSettings, 0, 0, points_per_squared_meter=BASE_POINTS_PER_SQM,
                       point_extents=unreal.Vector(50, 50, 50), looseness=1.0)
    b.link(landscape, "Out", sampler, "Surface")
    b.link(grid, "Out", sampler, "Bounding Shape")
    slope, _ = b.add(unreal.PCGNormalToDensitySettings, 300, 0, density_mode=unreal.PCGNormalToDensityMode.SET)
    b.link(sampler, "Out", slope, "In")
    slope_filter, _ = b.add(unreal.PCGDensityFilterSettings, 600, 0, lower_bound=slope_threshold, upper_bound=1.0)
    b.link(slope, "Out", slope_filter, "In")

    poi, _ = add_actor_data(b, -300, 400, unreal.PCGDataFromActorSettings, "TDPoi",
                            unreal.PCGGetDataFromActorMode.GET_SINGLE_POINT)
    b.link(grid, "Out", poi, "BoundingShape")
    poi_zone, _ = add_bounds_set(b, 0, 400, params["poi_clearance_cm"], 3000)
    b.link(poi, "Out", poi_zone, "In")
    exclusion, _ = add_actor_data(b, -300, 600, unreal.PCGDataFromActorSettings, "TDExclusion",
                                  unreal.PCGGetDataFromActorMode.PARSE_ACTOR_COMPONENTS)
    b.link(grid, "Out", exclusion, "BoundingShape")
    road, _ = add_actor_data(b, -300, 800, unreal.PCGGetSplineSettings, "TDRoad",
                             unreal.PCGGetDataFromActorMode.PARSE_ACTOR_COMPONENTS)
    b.link(grid, "Out", road, "BoundingShape")
    road_sampler, road_sampler_settings = b.add(unreal.PCGSplineSamplerSettings, 0, 800)
    road_params = road_sampler_settings.get_editor_property("params")
    road_params.set_editor_property("dimension", unreal.PCGSplineSamplingDimension.ON_SPLINE)
    road_params.set_editor_property("mode", unreal.PCGSplineSamplingMode.DISTANCE)
    road_params.set_editor_property("distance_increment", params["road_clearance_cm"])
    road_sampler_settings.set_editor_property("params", road_params)
    b.link(road, "Out", road_sampler, "Spline")
    road_zone, _ = add_bounds_set(b, 300, 800, params["road_clearance_cm"], 1000)
    b.link(road_sampler, "Out", road_zone, "In")

    clear, _ = b.add(unreal.PCGDifferenceSettings, 900, 0, mode=unreal.PCGDifferenceMode.DISCRETE)
    b.link(slope_filter, "Out", clear, "Source")
    b.link(poi_zone, "Out", clear, "Differences")
    b.link(exclusion, "Out", clear, "Differences")
    b.link(road_zone, "Out", clear, "Differences")
    open_filter, _ = add_attribute_filter(b, 1200, 0, "Road", unreal.PCGAttributeFilterOperator.LESSER, ROAD_WEIGHT_OPEN_MAX)
    b.link(clear, "Out", open_filter, "In")

    y = -300
    for name in ("PCG_TDVegetation", "PCG_TDRockScatter", "PCG_TDClutter"):
        node, settings = b.add(unreal.PCGSubgraphSettings, 1600, y)
        settings.get_editor_property("subgraph_instance").set_editor_property("graph", subgraphs[name])
        b.link(open_filter, "InsideFilter", node, "In")
        b.link(node, "Out", b.output, "Out")
        y += 200
    roadside, settings = b.add(unreal.PCGSubgraphSettings, 1600, 300)
    settings.get_editor_property("subgraph_instance").set_editor_property("graph", subgraphs["PCG_TDRoadside"])
    b.link(clear, "Out", roadside, "In")
    b.link(roadside, "Out", b.output, "Out")
    outside_poi, _ = b.add(unreal.PCGDifferenceSettings, 600, 500, mode=unreal.PCGDifferenceMode.DISCRETE)
    b.link(slope_filter, "Out", outside_poi, "Source")
    b.link(poi_zone, "Out", outside_poi, "Differences")
    inside_poi, _ = b.add(unreal.PCGDifferenceSettings, 900, 500, mode=unreal.PCGDifferenceMode.DISCRETE)
    b.link(slope_filter, "Out", inside_poi, "Source")
    b.link(outside_poi, "Out", inside_poi, "Differences")
    poi_inner, _ = b.add(unreal.PCGBoundsModifierSettings, 900, 700, mode=unreal.PCGBoundsModifierMode.SCALE,
                         bounds_min=unreal.Vector(0.4, 0.4, 1.0), bounds_max=unreal.Vector(0.4, 0.4, 1.0))
    b.link(poi_zone, "Out", poi_inner, "In")
    poi_ring, _ = b.add(unreal.PCGDifferenceSettings, 1200, 500, mode=unreal.PCGDifferenceMode.DISCRETE)
    b.link(inside_poi, "Out", poi_ring, "Source")
    b.link(poi_inner, "Out", poi_ring, "Differences")
    poi_dressing, settings = b.add(unreal.PCGSubgraphSettings, 1600, 500)
    settings.get_editor_property("subgraph_instance").set_editor_property("graph", subgraphs["PCG_TDPoiDressing"])
    b.link(poi_ring, "Out", poi_dressing, "In")
    b.link(poi_dressing, "Out", b.output, "Out")

    build_tree_branch(b, params, slope_threshold)


def build_tree_branch(b, params, slope_threshold):
    y = -900
    grid = b.input
    landscape, _ = add_landscape(b, 0, y - 200)
    b.link(grid, "In", landscape, "BoundingShape")
    tree_density = min(TREE_POINTS_PER_SQM_MAX, max(0.0001, params["tree_density_per100_sq_m"] / 100.0 * 0.1))
    sampler, _ = b.add(unreal.PCGSurfaceSamplerSettings, 300, y, points_per_squared_meter=tree_density,
                       point_extents=unreal.Vector(100, 100, 100), looseness=1.0)
    b.link(landscape, "Out", sampler, "Surface")
    b.link(grid, "In", sampler, "Bounding Shape")
    slope, _ = b.add(unreal.PCGNormalToDensitySettings, 600, y, density_mode=unreal.PCGNormalToDensityMode.SET)
    b.link(sampler, "Out", slope, "In")
    slope_filter, _ = b.add(unreal.PCGDensityFilterSettings, 900, y, lower_bound=slope_threshold, upper_bound=1.0)
    b.link(slope, "Out", slope_filter, "In")
    poi, _ = add_actor_data(b, 0, y + 250, unreal.PCGDataFromActorSettings, "TDPoi",
                            unreal.PCGGetDataFromActorMode.GET_SINGLE_POINT)
    b.link(grid, "In", poi, "BoundingShape")
    poi_zone, _ = add_bounds_set(b, 300, y + 250, params["poi_clearance_cm"] * 1.5, 3000)
    b.link(poi, "Out", poi_zone, "In")
    exclusion, _ = add_actor_data(b, 300, y + 400, unreal.PCGDataFromActorSettings, "TDExclusion",
                                  unreal.PCGGetDataFromActorMode.PARSE_ACTOR_COMPONENTS)
    b.link(grid, "In", exclusion, "BoundingShape")
    clear, _ = b.add(unreal.PCGDifferenceSettings, 1200, y, mode=unreal.PCGDifferenceMode.DISCRETE)
    b.link(slope_filter, "Out", clear, "Source")
    b.link(poi_zone, "Out", clear, "Differences")
    b.link(exclusion, "Out", clear, "Differences")
    road_filter, _ = add_attribute_filter(b, 1500, y, "Road", unreal.PCGAttributeFilterOperator.LESSER, ROAD_WEIGHT_OPEN_MAX)
    b.link(clear, "Out", road_filter, "In")
    spacing, _ = add_bounds_set(b, 1800, y, params["min_tree_distance_cm"] * 0.5, 200)
    b.link(road_filter, "InsideFilter", spacing, "In")
    prune, _ = add_self_pruning(b, 2100, y)
    b.link(spacing, "Out", prune, "In")
    transform, _ = add_random_transform(b, 2400, y, 0.9, 1.3)
    b.link(prune, "Out", transform, "In")
    spawner, _ = add_spawner(b, 2700, y, "tree", 30000)
    b.link(transform, "Out", spawner, "In")
    b.link(spawner, "Out", b.output, "Out")


def find_actor_by_label(label):
    for actor in eas.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def remove_previous_volume():
    actor = find_actor_by_label(VOLUME_LABEL)
    if actor is None:
        return
    component = actor.get_editor_property("pcg_component")
    if component is not None:
        component.cleanup(True)
    eas.destroy_actor(actor)
    log("previous volume removed")


def landscape_bounds():
    lo = hi = None
    for actor in eas.get_all_level_actors():
        if not isinstance(actor, unreal.LandscapeProxy):
            continue
        origin, extent = actor.get_actor_bounds(False)
        mn, mx = origin - extent, origin + extent
        if lo is None:
            lo, hi = mn, mx
            continue
        lo = unreal.Vector(min(lo.x, mn.x), min(lo.y, mn.y), min(lo.z, mn.z))
        hi = unreal.Vector(max(hi.x, mx.x), max(hi.y, mx.y), max(hi.z, mx.z))
    return lo, hi


def place_volume(graph):
    lo, hi = landscape_bounds()
    if lo is None:
        raise RuntimeError("no LandscapeProxy in level")
    lo = unreal.Vector(lo.x, lo.y, lo.z - 500)
    hi = unreal.Vector(hi.x, hi.y, hi.z + 1500)
    center = (lo + hi) * 0.5
    target_extent = (hi - lo) * 0.5
    actor = eas.spawn_actor_from_class(unreal.PCGVolume, center, unreal.Rotator(0, 0, 0))
    actor.set_actor_scale3d(unreal.Vector(10, 10, 10))
    _, probe_extent = actor.get_actor_bounds(False)
    unit_extent = probe_extent / 10.0
    scale = unreal.Vector(target_extent.x / max(unit_extent.x, 1.0), target_extent.y / max(unit_extent.y, 1.0),
                          target_extent.z / max(unit_extent.z, 1.0))
    actor.set_actor_scale3d(scale)
    actor.set_actor_label(VOLUME_LABEL)
    actor.set_folder_path(VOLUME_FOLDER)
    actor.tags = [unreal.Name("TDGen"), unreal.Name("TDGenPCG")]
    setp(actor, "is_spatially_loaded", False)
    component = actor.get_editor_property("pcg_component")
    component.set_editor_property("is_component_partitioned", True)
    component.set_editor_property("seed", COMPONENT_SEED)
    component.set_editor_property("generation_trigger", unreal.PCGComponentGenerationTrigger.GENERATE_ON_LOAD)
    component.set_graph(graph)
    origin, extent = actor.get_actor_bounds(False)
    log(f"volume bounds center={origin} extent={extent} partitioned={component.get_editor_property('is_component_partitioned')}")
    return actor, component


def main():
    world = ues.get_editor_world()
    log(f"level: {world.get_path_name()}")
    params, definition = read_biome_parameters()
    log(f"biome parameters: {params}")

    remove_previous_volume()
    if definition is not None:
        setp(definition, "pcg_graph", None)

    subgraphs = {}
    subgraphs["PCG_TDVegetation"] = create_graph("PCG_TDVegetation", PCG_DIR)
    build_vegetation(subgraphs["PCG_TDVegetation"])
    subgraphs["PCG_TDRockScatter"] = create_graph("PCG_TDRockScatter", PCG_DIR)
    build_rock_scatter(subgraphs["PCG_TDRockScatter"])
    subgraphs["PCG_TDClutter"] = create_graph("PCG_TDClutter", PCG_DIR)
    build_clutter(subgraphs["PCG_TDClutter"])
    subgraphs["PCG_TDRoadside"] = create_graph("PCG_TDRoadside", PCG_DIR)
    build_roadside(subgraphs["PCG_TDRoadside"])
    subgraphs["PCG_TDPoiDressing"] = create_graph("PCG_TDPoiDressing", PCG_DIR)
    build_poi_dressing(subgraphs["PCG_TDPoiDressing"])
    main_graph = create_graph("PCG_TDBiome_Forest", PCG_DIR)
    build_main(main_graph, subgraphs, params)
    for name, graph in list(subgraphs.items()) + [("PCG_TDBiome_Forest", main_graph)]:
        log(f"graph {name}: nodes={len(graph.get_editor_property('nodes'))} edges={len(graph.get_all_edges())}")

    room_graph = create_graph("PCG_TDRoomDressing", DUNGEON_PCG_DIR)
    build_room_dressing(room_graph)
    log(f"graph PCG_TDRoomDressing: nodes={len(room_graph.get_editor_property('nodes'))}")

    if definition is not None:
        setp(definition, "pcg_graph", main_graph)

    actor, component = place_volume(main_graph)
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved before generate: {ok}")
    component.generate_local(True)
    log("generation started (async). Poll with editor_check_pcg_determinism.py")


main()

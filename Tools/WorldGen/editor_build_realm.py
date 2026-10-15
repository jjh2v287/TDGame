"""에디터 안에서 실행: generate_realm 이 만든 이계 layout(v3, 월드 좌표)을 LV_DarkFantasy_OpenWorld 의 필드 옆 자리에 굽고 연결로 소품(TDRealm_Connector_<GateId>_*)을 그 이계 소유로 놓는다(P3-19 명세 4절, 데이터 레이어 DL_TDRegion_<RealmId>).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_build_realm.py (PowerShell, C++ C1 빌드·editor_setup_data_layers.py·필드 베이크 다음. 대상은 Saved/WorldGen/Realms/build_targets.json 목록, 없으면 Saved/WorldGen/Realms/*/layout.json 전부)
출력: 레벨 액터 TDRealm_<RealmId>_*·TDRealm_Connector_<GateId>_*(폴더 TDRealm/Connectors/<GateId>), /Game/World/Definitions/DA_TDRealm_<RealmId>(관문·승강 lift·gameplay_data_layer, C++ 프로퍼티가 없으면 건너뛰고 보고), 이계 재질 인스턴스, Saved/WorldGen/Realms/<RealmId>/build_report.json
상태: 실험(에디터 실행 검증 전). 굽기 전에 모든 layout 의 버전·옛 이음매 키·좌표(이계 상자+10m, 연결로 띠, 필드 상자)를 검사해 어긋나면 아무것도 지우지 않고 멈춘다. 이계 R 의 랜드스케이프는 태그 TDRealm:R 로, 그 밖은 DL_TDRegion_R 소속·태그 TDRealm:R·이전 접두(TDRealm_R_, TDRealm_Passage_R*, TDRealm_Gate_, TDRealm_Connector_<관문>_)로 지워 옛 먼 좌표 콘텐츠도 함께 정리한다.
"""
import glob
import importlib
import json
import math
import os
import sys
import time

import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
WORLDGEN_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "WorldGen")
if WORLDGEN_TOOLS_DIR not in sys.path:
    sys.path.insert(0, WORLDGEN_TOOLS_DIR)

open_world = importlib.reload(importlib.import_module("editor_build_open_world"))
data_layers = open_world.data_layers

LEVEL_PATH = open_world.LEVEL_PATH
LEVEL_NAME = LEVEL_PATH.rsplit("/", 1)[-1]
REALMS_ROOT = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "Realms")
BUILD_TARGETS_PATH = os.path.join(REALMS_ROOT, "build_targets.json")
DEFINITION_FOLDER = "/Game/World/Definitions"
LEGACY_GATE_LABEL_PREFIX = "TDRealm_Gate_"
LEGACY_PASSAGE_LABEL_PREFIX = "TDRealm_Passage_"
CONNECTOR_LABEL_PREFIX = "TDRealm_Connector_"
CONNECTOR_FOLDER_ROOT = "TDRealm/Connectors"
CONNECTOR_TAG = "TDConnector"
FIELD_POST_PROCESS_LABEL = "TDGen_PostProcess"
LAYOUT_LIST_KEYS = ("actors", "planes", "lights", "niagara", "markers")
MIN_GENERATOR_VERSION = 3
DEFAULT_HALF_EXTENT_Z_CM = 20000.0
REALM_BOX_MARGIN_CM = 1000.0
FIELD_HALF_EXTENT_CM = 50400.0
FIELD_POINT_MARGIN_CM = 1000.0
CONNECTOR_BAND_MARGIN_CM = 4000.0
OUTER_RING_REACH_CM = 21000.0
PLANE_MESH_SIZE_CM = 100.0
OVERLAP_TOLERANCE_CM = 1.0
LIFT_LANDING_XY_TOLERANCE_CM = 1.0
VOLUME_BRUSH_SIZE_CM = 200.0
MAX_REPORTED_PROBLEMS = 40
GATE_KIND_LIFT = "Lift"
GATE_KINDS = ("Passage", GATE_KIND_LIFT)
GATE_LENGTH_PROPERTIES = ("approach_length_cm", "departure_length_cm", "passage_half_width_cm", "preload_lead_cm")
REQUIRED_GATE_KEYS = ("gate_id", "gate_kind", "arrival", "field_return") + GATE_LENGTH_PROPERTIES
LIFT_TRANSFORM_PROPERTIES = ("field_landing_transform", "realm_landing_transform")
LIFT_FLOAT_PROPERTIES = ("cruise_speed_cm_per_second", "acceleration_cm_per_second_squared", "brake_deceleration_cm_per_second_squared", "board_dwell_seconds",
                         "approach_radius_cm", "call_speed_cm_per_second", "hold_depth_cm", "max_treadmill_seconds", "ring_module_height_cm",
                         "ring_top_depth_cm", "abyss_screen_depth_cm")
LIFT_INT_PROPERTIES = ("ring_module_count",)
LIFT_VECTOR_PROPERTIES = ("deck_half_extent_cm",)
LIFT_ASSET_PROPERTIES = ("deck_mesh", "rail_mesh", "deck_material", "liner_panel_mesh", "liner_material", "abyss_screen_material")
REQUIRED_LIFT_KEYS = LIFT_TRANSFORM_PROPERTIES + LIFT_FLOAT_PROPERTIES + LIFT_INT_PROPERTIES + LIFT_VECTOR_PROPERTIES
GATE_STRUCT_PROPERTIES = ("gate_id", "gate_kind", "arrival_transform", "field_return_transform") + GATE_LENGTH_PROPERTIES + ("lift",)
LIFT_STRUCT_PROPERTIES = REQUIRED_LIFT_KEYS + LIFT_ASSET_PROPERTIES
DEFINITION_NEW_PROPERTIES = ("gameplay_data_layer",)
REMOVED_CPP_PROPERTIES = {"TDRealmGate": ("seam_buffer_cm",), "TDRealmLift": ("hold_margin_cm", "max_wait_seconds"),
                          "TDRealmDefinition": ("seam_mist_density", "seam_mist_radius_cm")}
STALE_KEY_FRAGMENTS = ("seam", "old_passage_proxy", "hold_margin_cm", "max_wait_seconds")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

t0 = time.time()


def log(msg):
    print(f"[TDRealm {time.time() - t0:6.1f}s] {msg}")


def linear_color(values):
    alpha = values[3] if len(values) > 3 else 1.0
    return unreal.LinearColor(values[0], values[1], values[2], alpha)


def vector4(values):
    w = values[3] if len(values) > 3 else 1.0
    return unreal.Vector4(values[0], values[1], values[2], w)


def rotation_values(value):
    if isinstance(value, (int, float)):
        return [0.0, float(value), 0.0]
    return list(value)


def transform_location(entry):
    return [float(v) for v in (entry["loc_cm"] if "loc_cm" in entry else entry["loc"])]


def transform_rotation(entry):
    if "yaw_deg" in entry:
        return [0.0, float(entry["yaw_deg"]), 0.0]
    if "rot" in entry:
        return [float(v) for v in rotation_values(entry["rot"])]
    return [0.0, 0.0, 0.0]


def make_transform(entry):
    return unreal.Transform(location=open_world.vec(transform_location(entry)), rotation=open_world.rot(transform_rotation(entry)), scale=unreal.Vector(1.0, 1.0, 1.0))


def half_extent_xyz(value):
    if isinstance(value, (int, float)):
        return [float(value), float(value), DEFAULT_HALF_EXTENT_Z_CM]
    if len(value) == 2:
        return [float(value[0]), float(value[1]), DEFAULT_HALF_EXTENT_Z_CM]
    return [float(v) for v in value]


def realm_box(realm):
    center = realm["center_cm"]
    half = half_extent_xyz(realm["half_extent_cm"])
    return (center[0] - half[0], center[1] - half[1], -1.0e6), (center[0] + half[0], center[1] + half[1], 1.0e6)


def field_box():
    return (-FIELD_HALF_EXTENT_CM, -FIELD_HALF_EXTENT_CM, -1.0e6), (FIELD_HALF_EXTENT_CM, FIELD_HALF_EXTENT_CM, 1.0e6)


def boxes_overlap_xy(first, second, tolerance=OVERLAP_TOLERANCE_CM):
    (a_min, a_max), (b_min, b_max) = first, second
    return all(a_min[i] < b_max[i] - tolerance and a_max[i] > b_min[i] + tolerance for i in range(2))


def square_box(center_xy, half):
    return (center_xy[0] - half, center_xy[1] - half), (center_xy[0] + half, center_xy[1] + half)


def realm_tag(realm_id):
    return f"{open_world.REALM_TAG_PREFIX}{realm_id}"


def realm_label_prefix(realm_id):
    return f"TDRealm_{realm_id}_"


def outer_ring_label_prefix(realm_id):
    return f"{realm_label_prefix(realm_id)}OuterRing_"


def connector_label_prefix(gate_id):
    return f"{CONNECTOR_LABEL_PREFIX}{gate_id}_"


def is_lift_gate(gate):
    return gate.get("gate_kind") == GATE_KIND_LIFT


def connector_gate_id(label, gate_ids):
    if not label.startswith(CONNECTOR_LABEL_PREFIX):
        return None
    for gate_id in sorted(gate_ids, key=len, reverse=True):
        if label.startswith(connector_label_prefix(gate_id)):
            return gate_id
    return ""


def connector_polyline_xy(gate):
    if gate.get("centerline_cm"):
        return [(float(p[0]), float(p[1])) for p in gate["centerline_cm"]]
    points = [tuple(transform_location(gate["field_return"])[:2])]
    if is_lift_gate(gate):
        points.append(tuple(transform_location(gate["lift"]["field_landing_transform"])[:2]))
    points.append(tuple(transform_location(gate["arrival"])[:2]))
    return points


def distance_to_segment_xy(point, start, end):
    dx, dy = end[0] - start[0], end[1] - start[1]
    length_squared = dx * dx + dy * dy
    if length_squared <= 0.0:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    t = max(0.0, min(1.0, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / length_squared))
    return math.hypot(point[0] - (start[0] + t * dx), point[1] - (start[1] + t * dy))


def distance_to_polyline_xy(point, points):
    if len(points) == 1:
        return math.hypot(point[0] - points[0][0], point[1] - points[0][1])
    return min(distance_to_segment_xy(point, points[i], points[i + 1]) for i in range(len(points) - 1))


def connector_band_half_width_cm(gate):
    return float(gate["passage_half_width_cm"]) + CONNECTOR_BAND_MARGIN_CM


def is_in_connector_band(x, y, gate):
    return distance_to_polyline_xy((x, y), connector_polyline_xy(gate)) <= connector_band_half_width_cm(gate)


def connector_load_box(gate):
    points = connector_polyline_xy(gate)
    half = connector_band_half_width_cm(gate)
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return (min(xs) - half, min(ys) - half, -1.0e6), (max(xs) + half, max(ys) + half, 1.0e6)


def stale_keys(mapping, where):
    return [f"{where}.{key}" for key in mapping if any(fragment in key for fragment in STALE_KEY_FRAGMENTS)]


def schema_problems(layout):
    realm = layout["realm"]
    gates = realm.get("gates", [])
    problems = []
    if layout.get("generator_version", 0) < MIN_GENERATOR_VERSION:
        problems.append(f"generator_version {layout.get('generator_version')} < {MIN_GENERATOR_VERSION}: not a continuous-world layout, rerun generate_realm.py")
    problems += stale_keys(realm, "realm")
    if not gates:
        problems.append("realm has no gates")
    for gate in gates:
        gate_id = gate.get("gate_id", "?")
        problems += [f"{gate_id}: missing {key}" for key in REQUIRED_GATE_KEYS if key not in gate]
        problems += stale_keys(gate, gate_id)
        if gate.get("gate_kind") not in GATE_KINDS:
            problems.append(f"{gate_id}: gate_kind {gate.get('gate_kind')} not in {GATE_KINDS}")
        if is_lift_gate(gate):
            lift = gate.get("lift", {})
            problems += [f"{gate_id}: missing lift.{key}" for key in REQUIRED_LIFT_KEYS if key not in lift]
            problems += stale_keys(lift, f"{gate_id}.lift")
    return problems


def outer_ring_problems(key, entry, label, realm_min, realm_max, blocked_boxes):
    x, y = entry["loc"][0], entry["loc"][1]
    problems = []
    if not open_world.is_xy_in_box(x, y, realm_min, realm_max, OUTER_RING_REACH_CM):
        problems.append(f"{key} {label} at ({x:.0f},{y:.0f}) is outside realm box+{OUTER_RING_REACH_CM:.0f}")
    scale = entry.get("scale", [1.0, 1.0, 1.0])
    tile = square_box((x, y), max(abs(scale[0]), abs(scale[1])) * PLANE_MESH_SIZE_CM * 0.5)
    for name, box in blocked_boxes:
        if boxes_overlap_xy(tile, box):
            problems.append(f"{key} {label} tile at ({x:.0f},{y:.0f}) overlaps the {name} box (spec 1.9)")
    return problems


def entry_position_problems(key, entry, realm_id, gates_by_id, realm_min, realm_max, blocked_boxes):
    label = entry.get("label", "") or entry.get("kind", "")
    x, y = entry["loc"][0], entry["loc"][1]
    gate_id = connector_gate_id(label, gates_by_id)
    if gate_id == "":
        return [f"{key} {label}: connector label for a gate this realm does not own"]
    if gate_id:
        if is_in_connector_band(x, y, gates_by_id[gate_id]):
            return []
        return [f"{key} {label} at ({x:.0f},{y:.0f}) is outside the {gate_id} connector band"]
    if label.startswith(LEGACY_PASSAGE_LABEL_PREFIX):
        return [f"{key} {label}: legacy field passage item in a continuous-world layout"]
    if key == "planes" and label.startswith(outer_ring_label_prefix(realm_id)):
        return outer_ring_problems(key, entry, label, realm_min, realm_max, blocked_boxes)
    if open_world.is_xy_in_box(x, y, realm_min, realm_max, REALM_BOX_MARGIN_CM):
        return []
    return [f"{key} {label} at ({x:.0f},{y:.0f}) is outside realm box+{REALM_BOX_MARGIN_CM:.0f} (world coordinates only)"]


def ism_problems(entry, gates_by_id, realm_min, realm_max):
    gate_id = entry.get("gate_id")
    if gate_id is not None and gate_id not in gates_by_id:
        return [f"ism {entry.get('key')}: gate_id {gate_id} is not owned by this realm"]
    for t in entry["transforms"]:
        inside = is_in_connector_band(t[0], t[1], gates_by_id[gate_id]) if gate_id else open_world.is_xy_in_box(t[0], t[1], realm_min, realm_max, REALM_BOX_MARGIN_CM)
        if not inside:
            return [f"ism {entry.get('key')} cell {entry.get('cell')} instance at ({t[0]:.0f},{t[1]:.0f}) is outside its {'connector band' if gate_id else 'realm box'}"]
    return []


def gate_position_problems(gate, realm_min, realm_max):
    gate_id = gate["gate_id"]
    field_min, field_max = field_box()
    problems = []
    arrival = transform_location(gate["arrival"])
    if not open_world.is_xy_in_box(arrival[0], arrival[1], realm_min, realm_max, REALM_BOX_MARGIN_CM):
        problems.append(f"{gate_id}: arrival {arrival[:2]} is outside the realm box")
    field_return = transform_location(gate["field_return"])
    if not open_world.is_xy_in_box(field_return[0], field_return[1], field_min, field_max, FIELD_POINT_MARGIN_CM):
        problems.append(f"{gate_id}: field_return {field_return[:2]} is outside the field box")
    if not is_lift_gate(gate):
        return problems
    lift = gate["lift"]
    top = transform_location(lift["field_landing_transform"])
    bottom = transform_location(lift["realm_landing_transform"])
    for name, point in (("field_landing", top), ("realm_landing", bottom)):
        if not is_in_connector_band(point[0], point[1], gate):
            problems.append(f"{gate_id}: lift {name} {point[:2]} is outside the connector band")
    if math.hypot(top[0] - bottom[0], top[1] - bottom[1]) > LIFT_LANDING_XY_TOLERANCE_CM:
        problems.append(f"{gate_id}: lift landings differ in XY {top[:2]} vs {bottom[:2]}")
    if top[2] <= bottom[2]:
        problems.append(f"{gate_id}: lift field landing z {top[2]} is not above realm landing z {bottom[2]}")
    return problems


def volume_problems(realm, realm_min, realm_max):
    problems = []
    for name in ("nav_bounds", "post_process"):
        box = realm.get(name)
        if not box:
            continue
        for corner in (box["box_min_cm"], box["box_max_cm"]):
            if not open_world.is_xy_in_box(corner[0], corner[1], realm_min, realm_max, REALM_BOX_MARGIN_CM):
                problems.append(f"{name} corner {corner[:2]} is outside realm box+{REALM_BOX_MARGIN_CM:.0f}")
    return problems


def validate_layout(layout, other_realm_boxes):
    problems = schema_problems(layout)
    if problems:
        return problems
    realm = layout["realm"]
    realm_id = realm["realm_id"]
    realm_min, realm_max = realm_box(realm)
    blocked_boxes = [("field", field_box())] + sorted(other_realm_boxes.items())
    for name, box in blocked_boxes:
        if boxes_overlap_xy((realm_min, realm_max), box):
            problems.append(f"realm box {realm_min[:2]}~{realm_max[:2]} overlaps the {name} box")
    gates_by_id = {gate["gate_id"]: gate for gate in realm["gates"]}
    for key in LAYOUT_LIST_KEYS:
        for entry in layout.get(key, []):
            if key == "markers" and entry.get("kind") == "PlayerStart":
                continue
            problems += entry_position_problems(key, entry, realm_id, gates_by_id, realm_min, realm_max, blocked_boxes)
    for entry in layout.get("ism", []):
        problems += ism_problems(entry, gates_by_id, realm_min, realm_max)
    for gate in realm["gates"]:
        problems += gate_position_problems(gate, realm_min, realm_max)
    problems += volume_problems(realm, realm_min, realm_max)
    return problems


def resolve_layout_paths():
    if os.path.isfile(BUILD_TARGETS_PATH):
        realm_ids = json.load(open(BUILD_TARGETS_PATH, encoding="utf-8"))
        paths = [os.path.join(REALMS_ROOT, realm_id, "layout.json") for realm_id in realm_ids]
        missing = [p for p in paths if not os.path.isfile(p)]
        if missing:
            raise FileNotFoundError(f"build_targets.json lists realms without layout.json: {missing}")
        return paths
    return sorted(glob.glob(os.path.join(REALMS_ROOT, "*", "layout.json")))


def load_json(path):
    with open(path, encoding="utf-8") as fp:
        return json.load(fp)


def all_realm_boxes():
    boxes = {}
    for path in sorted(glob.glob(os.path.join(REALMS_ROOT, "*", "layout.json"))):
        realm = load_json(path)["realm"]
        boxes[realm["realm_id"]] = realm_box(realm)
    return boxes


def validate_layouts(layouts):
    boxes = all_realm_boxes()
    failures = {}
    for path, layout in layouts.items():
        realm_id = layout["realm"]["realm_id"]
        boxes[realm_id] = realm_box(layout["realm"])
        others = {other_id: box for other_id, box in boxes.items() if other_id != realm_id}
        problems = validate_layout(layout, others)
        if problems:
            failures[path] = problems[:MAX_REPORTED_PROBLEMS] + ([f"... {len(problems) - MAX_REPORTED_PROBLEMS} more"] if len(problems) > MAX_REPORTED_PROBLEMS else [])
    if failures:
        raise RuntimeError(f"realm layouts failed validation, nothing was deleted or baked: {json.dumps(failures, ensure_ascii=False)}")
    log(f"layouts validated: {list(layouts)}")


def ensure_level_loaded():
    world = ues.get_editor_world()
    if world is not None and world.get_name() == LEVEL_NAME:
        return world
    les.load_level(LEVEL_PATH)
    world = ues.get_editor_world()
    if world is None or world.get_name() != LEVEL_NAME:
        raise RuntimeError(f"level {LEVEL_PATH} could not be loaded")
    log(f"level loaded: {world.get_name()}")
    return world


def realm_label_prefixes(realm_id, gate_ids):
    prefixes = [realm_label_prefix(realm_id), LEGACY_GATE_LABEL_PREFIX, f"{LEGACY_PASSAGE_LABEL_PREFIX}{realm_id}"]
    prefixes += [f"{LEGACY_PASSAGE_LABEL_PREFIX}{gate_id}_" for gate_id in gate_ids]
    prefixes += [connector_label_prefix(gate_id) for gate_id in gate_ids]
    return tuple(prefixes)


def is_desc_relevant(desc, realm_id, prefixes, boxes):
    native_class = desc.native_class
    if native_class is not None and "HLOD" in native_class.get_name():
        return False
    if str(desc.label).startswith(prefixes):
        return True
    if data_layers.region_layer_name(realm_id) in data_layers.desc_layer_names(desc):
        return True
    bounds = desc.bounds
    if not bounds.is_valid:
        return False
    for box_min, box_max in boxes:
        if bounds.max.x >= box_min[0] and bounds.min.x <= box_max[0] and bounds.max.y >= box_min[1] and bounds.min.y <= box_max[1]:
            return True
    return False


def load_world_partition_actors(realm_id, prefixes, boxes):
    library = unreal.WorldPartitionBlueprintLibrary
    descs = library.get_actor_descs()
    if descs is None:
        log("WARNING world partition actor descs unavailable; only loaded actors are handled")
        return {"available": False}
    relevant = [desc for desc in descs if is_desc_relevant(desc, realm_id, prefixes, boxes)]
    if relevant:
        library.load_actors([desc.guid for desc in relevant])
    result = {"available": True, "all_descs": len(descs), "relevant_descs": len(relevant), "load_boxes": [[list(box_min[:2]), list(box_max[:2])] for box_min, box_max in boxes]}
    log(f"world partition: {result}")
    return result


def realm_clear_decision(label, class_name, tags, layer_names, realm_id, gate_ids):
    if class_name in open_world.KEEP_CLASSES or class_name in open_world.LANDSCAPE_CLASS_NAMES:
        return False, "kept_class"
    if data_layers.region_layer_name(realm_id) in layer_names:
        return True, "deleted_realm_layer"
    if layer_names & data_layers.REGION_LAYER_NAMES:
        return False, "other_region_layer"
    if realm_tag(realm_id) in tags:
        return True, "deleted_realm_tag"
    if label.startswith(realm_label_prefix(realm_id)):
        return True, "deleted_realm_prefix"
    if label.startswith(f"{LEGACY_PASSAGE_LABEL_PREFIX}{realm_id}") or label.startswith(tuple(f"{LEGACY_PASSAGE_LABEL_PREFIX}{g}_" for g in gate_ids)):
        return True, "deleted_legacy_passage"
    if label.startswith(LEGACY_GATE_LABEL_PREFIX):
        return True, "deleted_legacy_gate"
    if label.startswith(tuple(connector_label_prefix(g) for g in gate_ids)):
        return True, "deleted_connector_prefix"
    return False, "untouched"


def destroy_realm_landscape(world, realm_id):
    tag = realm_tag(realm_id)
    library = unreal.TDLandscapeEditorLibrary
    if hasattr(library, "destroy_landscape_actors_with_tag"):
        return library.destroy_landscape_actors_with_tag(world, tag)
    log("WARNING TDLandscapeEditorLibrary.destroy_landscape_actors_with_tag missing (C++ not rebuilt); Python fallback by tag")
    return open_world.destroy_actors([proxy for proxy in open_world.landscape_actors(world) if tag in open_world.actor_tag_names(proxy)])


def remove_previous_realm(world, realm_id, gate_ids):
    counts = {"landscape": destroy_realm_landscape(world, realm_id)}
    for actor in list(eas.get_all_level_actors()):
        delete, reason = realm_clear_decision(actor.get_actor_label(), actor.get_class().get_name(), open_world.actor_tag_names(actor),
                                              data_layers.actor_layer_names(actor), realm_id, gate_ids)
        counts[reason] = counts.get(reason, 0) + 1
        if delete:
            eas.destroy_actor(actor)
    log(f"removed: {counts}")
    return counts


def ensure_directory(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def load_or_create_material_instance(asset_path):
    if EAL.does_asset_exist(asset_path):
        return EAL.load_asset(asset_path)
    folder, name = asset_path.rsplit("/", 1)
    ensure_directory(folder)
    return asset_tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())


def clear_material_instance_parameters(instance, result):
    if not hasattr(MEL, "clear_all_material_instance_parameters"):
        result["cleared"] = False
        log("WARNING MaterialEditingLibrary.clear_all_material_instance_parameters missing; old overrides kept")
        return
    MEL.clear_all_material_instance_parameters(instance)
    result["cleared"] = True


def build_material_instance(spec):
    asset_path = spec["asset"]
    result = {"asset": asset_path, "applied": [], "missing": [], "error": "", "cleared": False}
    instance = load_or_create_material_instance(asset_path)
    if instance is None:
        result["error"] = "create failed"
        log(f"WARNING material {asset_path}: create failed")
        return result
    parent = EAL.load_asset(spec["parent"]) if spec.get("parent") else None
    if parent is None:
        result["error"] = f"parent missing {spec.get('parent')}"
        log(f"WARNING material {asset_path}: parent missing {spec.get('parent')}")
        return result
    MEL.set_material_instance_parent(instance, parent)
    clear_material_instance_parameters(instance, result)
    vector_names = {str(n) for n in MEL.get_vector_parameter_names(parent) or []}
    scalar_names = {str(n) for n in MEL.get_scalar_parameter_names(parent) or []}
    for name, value in spec.get("vectors", {}).items():
        if name not in vector_names:
            result["missing"].append(name)
            continue
        MEL.set_material_instance_vector_parameter_value(instance, name, linear_color(value))
        result["applied"].append(name)
    for name, value in spec.get("scalars", {}).items():
        if name not in scalar_names:
            result["missing"].append(name)
            continue
        MEL.set_material_instance_scalar_parameter_value(instance, name, float(value))
        result["applied"].append(name)
    MEL.update_material_instance(instance)
    log(f"material {asset_path}: applied {result['applied']} missing {result['missing']}")
    return result


def build_materials(realm):
    specs = []
    if realm.get("landscape_material"):
        specs.append(realm["landscape_material"])
    specs.extend(realm.get("materials", []))
    return [build_material_instance(spec) for spec in specs]


def landscape_sort_key(proxy):
    x, y = open_world.landscape_center_xy(proxy)
    return (round(y), round(x))


def build_landscape(world, cfg, realm_id, realm_prefix, folder_root, tags, realm_min, realm_max):
    def is_new_realm_landscape(proxy):
        if open_world.actor_tag_names(proxy):
            return False
        x, y = open_world.landscape_center_xy(proxy)
        return open_world.is_xy_in_box(x, y, realm_min, realm_max)

    def finish_proxies(proxies):
        streaming_index = 0
        for proxy in sorted(proxies, key=landscape_sort_key):
            if isinstance(proxy, unreal.Landscape):
                label = f"{realm_prefix}Landscape"
            else:
                label = f"{realm_prefix}Landscape_{streaming_index:02d}"
                streaming_index += 1
            open_world.setp(proxy, "enable_nanite", True)
            proxy.set_actor_label(label)
            proxy.set_folder_path(f"{folder_root}/Landscape")
            proxy.tags = [unreal.Name(t) for t in tags]

    landscape, proxies, result = open_world.create_region_landscape(world, cfg, realm_id, is_new_realm_landscape, finish_proxies)
    open_world.build_grass_maps()
    result["streaming_proxies"] = sum(1 for proxy in proxies if not isinstance(proxy, unreal.Landscape))
    log(f"landscape created: {landscape.get_actor_label()} {result}")
    return result


def split_layout(layout, has_nav_bounds, gate_ids):
    realm_part = {key: [] for key in LAYOUT_LIST_KEYS}
    connector_parts = {gate_id: {key: [] for key in LAYOUT_LIST_KEYS} for gate_id in gate_ids}
    skipped = []
    for key in LAYOUT_LIST_KEYS:
        for entry in layout.get(key, []):
            label = entry.get("label", "")
            kind = entry.get("kind", "")
            if key == "markers" and (kind == "PlayerStart" or (kind == "NavMeshBounds" and has_nav_bounds)):
                skipped.append(label or kind)
                continue
            gate_id = connector_gate_id(label, gate_ids)
            target = connector_parts[gate_id] if gate_id else realm_part
            target[key].append(entry)
    realm_part["ism"] = [entry for entry in layout.get("ism", []) if not entry.get("gate_id")]
    for gate_id, part in connector_parts.items():
        part["ism"] = [entry for entry in layout.get("ism", []) if entry.get("gate_id") == gate_id]
    if skipped:
        log(f"markers skipped (handled by this builder): {skipped}")
    return realm_part, connector_parts


def spawn_layout_part(part, label_prefix, folder_root, extra_tags):
    counts = {}
    counts["ism_cells"], counts["ism_instances"] = open_world.spawn_ism_cells(part, label_prefix, folder_root, extra_tags)
    counts["actors"] = open_world.spawn_actors(part, label_prefix, folder_root, extra_tags)
    counts["planes"] = open_world.spawn_planes(part, label_prefix, folder_root, extra_tags)
    counts["lights"] = open_world.spawn_lights(part, label_prefix, folder_root, extra_tags)
    counts["niagara"] = open_world.spawn_niagara(part, label_prefix, folder_root, extra_tags)
    counts["markers"] = open_world.spawn_markers(part, label_prefix, folder_root, extra_tags)
    return counts


def spawn_box_volume(actor_class, box_min, box_max, label, folder, tags):
    center = [(box_min[i] + box_max[i]) * 0.5 for i in range(3)]
    size = [box_max[i] - box_min[i] for i in range(3)]
    actor = eas.spawn_actor_from_class(actor_class, open_world.vec(center), unreal.Rotator(0, 0, 0))
    actor.set_actor_scale3d(unreal.Vector(size[0] / VOLUME_BRUSH_SIZE_CM, size[1] / VOLUME_BRUSH_SIZE_CM, size[2] / VOLUME_BRUSH_SIZE_CM))
    open_world.finish_actor(actor, label, folder, False, tags)
    _, extent = actor.get_actor_bounds(False)
    actual = [round(extent.x * 2.0), round(extent.y * 2.0), round(extent.z * 2.0)]
    if any(abs(actual[i] - size[i]) > 1.0 for i in range(3)):
        log(f"WARNING {label}: brush size {actual} differs from requested {size}")
    return actor, {"center_cm": center, "size_cm": size, "measured_size_cm": actual}


def spawn_nav_bounds(nav_bounds, realm_prefix, folder_root, tags):
    if not nav_bounds:
        log("WARNING layout has no nav_bounds; navigation volume not created")
        return None
    _, result = spawn_box_volume(unreal.NavMeshBoundsVolume, nav_bounds["box_min_cm"], nav_bounds["box_max_cm"], f"{realm_prefix}NavBounds", f"{folder_root}/Navigation", tags)
    log(f"nav bounds {result}")
    return result


def read_field_exposure_bias():
    for actor in eas.get_all_level_actors():
        if actor.get_actor_label() != FIELD_POST_PROCESS_LABEL:
            continue
        settings = actor.get_editor_property("settings")
        if not settings.get_editor_property("override_auto_exposure_bias"):
            return 0.0
        return float(settings.get_editor_property("auto_exposure_bias"))
    return float(open_world.LOOK["exposure_bias"])


def post_process_value(name, value, field_exposure_bias):
    if name == "auto_exposure_bias":
        return field_exposure_bias + float(value)
    if isinstance(value, (list, tuple)):
        return vector4(value)
    return float(value)


def spawn_post_process(post_process, realm_prefix, folder_root, tags, field_exposure_bias):
    if not post_process:
        log("WARNING layout has no post_process; volume not created")
        return None
    actor, result = spawn_box_volume(unreal.PostProcessVolume, post_process["box_min_cm"], post_process["box_max_cm"], f"{realm_prefix}PostProcess", f"{folder_root}/Atmosphere", tags)
    open_world.setp(actor, "unbound", False)
    open_world.setp(actor, "priority", float(post_process.get("priority", 10)))
    settings = actor.get_editor_property("settings")
    applied = {}
    skipped = []
    for name, value in post_process.get("settings", {}).items():
        converted = post_process_value(name, value, field_exposure_bias)
        try:
            settings.set_editor_property("override_" + name, True)
            settings.set_editor_property(name, converted)
            applied[name] = value if name != "auto_exposure_bias" else converted
        except Exception as e:
            skipped.append(name)
            log(f"pp skip {name}: {str(e)[:80]}")
    actor.set_editor_property("settings", settings)
    result.update({"applied": applied, "skipped": skipped, "field_exposure_bias": field_exposure_bias})
    log(f"post process {result}")
    return result


def load_or_create_data_asset(asset_path, asset_class):
    if EAL.does_asset_exist(asset_path):
        return EAL.load_asset(asset_path)
    folder, name = asset_path.rsplit("/", 1)
    ensure_directory(folder)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return asset_tools.create_asset(name, folder, asset_class, factory)


def atmosphere_value(name, value):
    if name == "sun_rotation":
        return open_world.rot(rotation_values(value))
    if isinstance(value, (list, tuple)):
        return linear_color(value)
    return float(value)


def make_atmosphere(values):
    atmosphere = unreal.TDRealmAtmosphere()
    skipped = []
    for name, value in values.items():
        if not open_world.setp(atmosphere, name, atmosphere_value(name, value)):
            skipped.append(name)
    return atmosphere, skipped


def has_editor_property(target, name):
    if hasattr(target, name):
        return True
    try:
        target.get_editor_property(name)
        return True
    except Exception:
        return False


def set_if_present(target, owner, name, value, missing):
    if not has_editor_property(target, name):
        missing.append(f"{owner}.{name}")
        return False
    target.set_editor_property(name, value)
    return True


def cpp_contract():
    samples = {"TDRealmGate": unreal.TDRealmGate()}
    if hasattr(unreal, "TDRealmLift"):
        samples["TDRealmLift"] = unreal.TDRealmLift()
    samples["TDRealmDefinition"] = unreal.get_default_object(unreal.TDRealmDefinition)
    wanted = {"TDRealmGate": GATE_STRUCT_PROPERTIES, "TDRealmLift": LIFT_STRUCT_PROPERTIES, "TDRealmDefinition": DEFINITION_NEW_PROPERTIES}
    missing = [f"{owner}.{name}" for owner, names in wanted.items() for name in names if owner not in samples or not has_editor_property(samples[owner], name)]
    removed_still_present = [f"{owner}.{name}" for owner, names in REMOVED_CPP_PROPERTIES.items() for name in names if owner in samples and has_editor_property(samples[owner], name)]
    result = {"missing": missing, "removed_still_present": removed_still_present, "gate_kind_enum": hasattr(unreal, "TDRealmGateKind")}
    if missing or removed_still_present:
        log(f"WARNING C++ realm definition contract not met (build C1 and restart the editor): {result}")
    return result


def gate_kind_value(gate):
    return unreal.TDRealmGateKind.LIFT if is_lift_gate(gate) else unreal.TDRealmGateKind.PASSAGE


def set_lift_asset(lift, name, path, result, missing):
    if not has_editor_property(lift, name):
        missing.append(f"TDRealmLift.{name}")
        result["assets"][name] = "property missing"
        return
    if not path:
        result["assets"][name] = None
        return
    asset = EAL.load_asset(path)
    if asset is None:
        result["assets"][name] = f"missing {path}"
        log(f"WARNING lift {name}: asset missing {path}")
        return
    lift.set_editor_property(name, asset)
    result["assets"][name] = path


def make_lift(gate, missing):
    data = gate["lift"]
    lift = unreal.TDRealmLift()
    for name in LIFT_TRANSFORM_PROPERTIES:
        set_if_present(lift, "TDRealmLift", name, make_transform(data[name]), missing)
    for name in LIFT_FLOAT_PROPERTIES:
        set_if_present(lift, "TDRealmLift", name, float(data[name]), missing)
    for name in LIFT_INT_PROPERTIES:
        set_if_present(lift, "TDRealmLift", name, int(data[name]), missing)
    for name in LIFT_VECTOR_PROPERTIES:
        set_if_present(lift, "TDRealmLift", name, open_world.vec(data[name]), missing)
    result = {"field_landing_loc": transform_location(data["field_landing_transform"]), "realm_landing_loc": transform_location(data["realm_landing_transform"]),
              "floats": {name: float(data[name]) for name in LIFT_FLOAT_PROPERTIES}, "ints": {name: int(data[name]) for name in LIFT_INT_PROPERTIES},
              "deck_half_extent_cm": data["deck_half_extent_cm"], "assets": {}}
    for name in LIFT_ASSET_PROPERTIES:
        set_lift_asset(lift, name, data.get(name), result, missing)
    return lift, result


def make_realm_gates(gates):
    has_kind = hasattr(unreal, "TDRealmGateKind")
    has_lift = hasattr(unreal, "TDRealmLift")
    structs = []
    missing = []
    report = {"gate_kinds": {}, "lifts": {}, "lift_gates_skipped_no_cpp_support": []}
    for gate in gates:
        if is_lift_gate(gate) and not (has_kind and has_lift):
            report["lift_gates_skipped_no_cpp_support"].append(gate["gate_id"])
            log(f"WARNING {gate['gate_id']}: lift gate skipped, unreal.TDRealmGateKind/TDRealmLift missing (build and load TDWorldGen first)")
            continue
        entry = unreal.TDRealmGate()
        set_if_present(entry, "TDRealmGate", "gate_id", gate["gate_id"], missing)
        set_if_present(entry, "TDRealmGate", "arrival_transform", make_transform(gate["arrival"]), missing)
        set_if_present(entry, "TDRealmGate", "field_return_transform", make_transform(gate["field_return"]), missing)
        for name in GATE_LENGTH_PROPERTIES:
            set_if_present(entry, "TDRealmGate", name, float(gate[name]), missing)
        if has_kind:
            set_if_present(entry, "TDRealmGate", "gate_kind", gate_kind_value(gate), missing)
        report["gate_kinds"][gate["gate_id"]] = gate["gate_kind"] if has_kind else "Passage(enum missing, default)"
        if is_lift_gate(gate):
            lift, lift_report = make_lift(gate, missing)
            set_if_present(entry, "TDRealmGate", "lift", lift, missing)
            report["lifts"][gate["gate_id"]] = lift_report
        structs.append(entry)
    report["cpp_missing_properties"] = sorted(set(missing))
    return structs, report


def gameplay_layer_asset(realm, missing):
    path = realm.get("gameplay_data_layer") or data_layers.layer_asset_path(data_layers.gameplay_layer_name(realm["realm_id"]))
    if not EAL.does_asset_exist(path):
        missing.append(f"asset {path}")
        log(f"WARNING gameplay data layer asset missing {path}; run editor_setup_data_layers.py")
        return path, None
    return path, EAL.load_asset(path)


def write_realm_definition(realm, gates):
    realm_id = realm["realm_id"]
    asset_path = f"{DEFINITION_FOLDER}/DA_TDRealm_{realm_id}"
    asset = load_or_create_data_asset(asset_path, unreal.TDRealmDefinition)
    if asset is None:
        raise RuntimeError(f"data asset {asset_path} could not be created")
    half = half_extent_xyz(realm["half_extent_cm"])
    atmosphere, skipped = make_atmosphere(realm.get("atmosphere", {}))
    asset.set_editor_property("realm_id", realm_id)
    asset.set_editor_property("display_name", unreal.Text(realm.get("display_name_ko", realm_id)))
    asset.set_editor_property("center_cm", open_world.vec(realm["center_cm"]))
    asset.set_editor_property("half_extent_cm", open_world.vec(half))
    asset.set_editor_property("fall_recovery_height_cm", float(realm["fall_recovery_height_cm"]))
    asset.set_editor_property("transition_fade_color", linear_color(realm["transition_fade_color"]))
    asset.set_editor_property("atmosphere", atmosphere)
    gate_structs, gate_report = make_realm_gates(gates)
    asset.set_editor_property("gates", gate_structs)
    missing = list(gate_report["cpp_missing_properties"])
    layer_path, layer_asset = gameplay_layer_asset(realm, missing)
    if layer_asset is not None:
        set_if_present(asset, "TDRealmDefinition", "gameplay_data_layer", layer_asset, missing)
    written = [g["gate_id"] for g in gates if g["gate_id"] not in gate_report["lift_gates_skipped_no_cpp_support"]]
    result = {"asset": asset_path, "gates": written, "atmosphere_skipped": skipped, "gameplay_data_layer": layer_path,
              "passages": {g["gate_id"]: {name: g[name] for name in GATE_LENGTH_PROPERTIES} for g in gates}}
    result.update(gate_report)
    result["cpp_missing_properties"] = sorted(set(missing))
    if result["cpp_missing_properties"]:
        log(f"WARNING {asset_path}: skipped missing C++ properties/assets {result['cpp_missing_properties']}")
    log(f"definition {result}")
    return result


def bake_realm(world, layout_path, layout, field_exposure_bias):
    realm = layout["realm"]
    realm_id = realm["realm_id"]
    gates = realm["gates"]
    gate_ids = [g["gate_id"] for g in gates]
    realm_prefix = realm_label_prefix(realm_id)
    folder_root = f"TDRealm/{realm_id}"
    realm_tags = ["TDRealm", realm_tag(realm_id)]
    realm_min, realm_max = realm_box(realm)
    log(f"realm {realm_id}: box {realm_min[:2]}~{realm_max[:2]} gates {gate_ids}")
    report = {"realm_id": realm_id, "layout": layout_path, "seed": layout.get("seed"), "generator_version": layout.get("generator_version")}
    load_boxes = [(realm_min, realm_max)] + [connector_load_box(g) for g in gates]
    report["world_partition"] = load_world_partition_actors(realm_id, realm_label_prefixes(realm_id, gate_ids), load_boxes)
    report["removed"] = remove_previous_realm(world, realm_id, gate_ids)
    report["materials"] = build_materials(realm)
    report["landscape"] = build_landscape(world, layout["landscape"], realm_id, realm_prefix, folder_root, ["TDGen"] + realm_tags, realm_min, realm_max)
    realm_part, connector_parts = split_layout(layout, bool(realm.get("nav_bounds")), gate_ids)
    with data_layers.region_context(realm_id):
        report["spawned"] = spawn_layout_part(realm_part, realm_prefix, folder_root, realm_tags)
        report["spawned_connectors"] = {gate_id: spawn_layout_part(part, connector_label_prefix(gate_id), f"{CONNECTOR_FOLDER_ROOT}/{gate_id}", realm_tags + [CONNECTOR_TAG])
                                        for gate_id, part in connector_parts.items()}
        report["nav_bounds"] = spawn_nav_bounds(realm.get("nav_bounds"), realm_prefix, folder_root, ["TDGen"] + realm_tags)
        report["post_process"] = spawn_post_process(realm.get("post_process"), realm_prefix, folder_root, ["TDGen"] + realm_tags, field_exposure_bias)
    report["definition"] = write_realm_definition(realm, gates)
    return report


def require_field_landscape_unchanged(before, after):
    if after["field"] != before["field"]:
        raise RuntimeError(f"field landscape actor count changed {before['field']} -> {after['field']} during the realm bake, level NOT saved")


def write_report(report):
    path = os.path.join(os.path.dirname(report["layout"]), "build_report.json")
    open_world.write_json_atomic(path, report)
    log(f"report written: {path}")


def main():
    layout_paths = resolve_layout_paths()
    if not layout_paths:
        log(f"no realm layouts under {REALMS_ROOT}")
        return
    layouts = {path: load_json(path) for path in layout_paths}
    validate_layouts(layouts)
    world = ensure_level_loaded()
    data_layer_state = data_layers.prepare_for_build()
    contract = cpp_contract()
    open_world.load_landscape_actors()
    before = open_world.landscape_counts(world)
    field_exposure_bias = read_field_exposure_bias()
    reports = [bake_realm(world, path, layout, field_exposure_bias) for path, layout in layouts.items()]
    after = open_world.landscape_counts(world)
    require_field_landscape_unchanged(before, after)
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved: {ok}")
    for report in reports:
        report.update({"saved": bool(ok), "data_layers": data_layer_state, "cpp_contract": contract, "landscape_counts_before": before, "landscape_counts_after": after})
        write_report(report)
        log(f"summary {report['realm_id']}: spawned {report['spawned']} connectors {report['spawned_connectors']} gates {report['definition']['gates']} landscape {report['landscape']}")


if __name__ == "__main__":
    main()

"""에디터 안에서 실행: generate_realm 이 만든 이계 layout 을 LV_DarkFantasy_OpenWorld 의 먼 좌표에 굽고 필드 통로 소품(TDRealm_Passage_*)을 놓는다(P3-13, P3-15 명세 4절, 에디터 Python API와 기존 C++ 라이브러리로 충분해 editor_* 로 둔다).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_build_realm.py (PowerShell. 대상은 Saved/WorldGen/Realms/build_targets.json 목록, 없으면 Saved/WorldGen/Realms/*/layout.json 전부)
출력: 레벨 액터 TDRealm_<RealmId>_*·TDRealm_Passage_<GateId>_*, /Game/World/Definitions/DA_TDRealm_<RealmId>(통로 필드·이음매 안개 포함), 이계 재질 인스턴스, Saved/WorldGen/Realms/<RealmId>/build_report.json
상태: 실험(에디터 실행 검증 전). 이계마다 자기 라벨·상자·관문 통로 접두어만 지우고 다시 만들며, 옛 포탈(TDRealm_Gate_*·TDRealm_<RealmId>_Exit_*)은 일반 접두어로 지운다.
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

LEVEL_PATH = open_world.LEVEL_PATH
LEVEL_NAME = LEVEL_PATH.rsplit("/", 1)[-1]
REALMS_ROOT = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "Realms")
BUILD_TARGETS_PATH = os.path.join(REALMS_ROOT, "build_targets.json")
DEFINITION_FOLDER = "/Game/World/Definitions"
OLD_FIELD_GATE_LABEL_PREFIX = "TDRealm_Gate_"
FIELD_PASSAGE_LABEL_PREFIX = "TDRealm_Passage_"
FIELD_PASSAGE_FOLDER = "TDRealm/FieldPassages"
FIELD_PASSAGE_TAGS = ["TDRealm", "TDRealmPassage"]
FIELD_POST_PROCESS_LABEL = "TDGen_PostProcess"
LAYOUT_LIST_KEYS = ("actors", "planes", "lights", "niagara", "markers")
DEFAULT_HALF_EXTENT_Z_CM = 20000.0
REALM_BOX_MARGIN_CM = 1000.0
FIELD_PASSAGE_BAND_HALF_WIDTH_CM = 2400.0
FIELD_PASSAGE_LOAD_MARGIN_CM = 3000.0
VOLUME_BRUSH_SIZE_CM = 200.0
GATE_LENGTH_PROPERTIES = ("approach_length_cm", "departure_length_cm", "seam_buffer_cm", "passage_half_width_cm", "preload_lead_cm")
REQUIRED_GATE_KEYS = ("gate_id", "arrival", "field_return") + GATE_LENGTH_PROPERTIES
REALM_MIST_PROPERTIES = ("seam_mist_density", "seam_mist_radius_cm")

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


def make_transform(entry):
    return unreal.Transform(location=open_world.vec(entry["loc"]), rotation=open_world.rot(rotation_values(entry["rot"])), scale=unreal.Vector(1.0, 1.0, 1.0))


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


def to_realm_world(loc, realm, box_min, box_max, what):
    if open_world.is_xy_in_box(loc[0], loc[1], box_min, box_max):
        return list(loc)
    center = realm["center_cm"]
    shifted = [loc[0] + center[0], loc[1] + center[1], loc[2] + center[2]]
    if open_world.is_xy_in_box(shifted[0], shifted[1], box_min, box_max):
        log(f"{what}: realm-local coordinates shifted by center")
        return shifted
    log(f"WARNING {what}: {loc} is outside the realm box {box_min[:2]}~{box_max[:2]}")
    return list(loc)


def resolve_box(box, realm, realm_min, realm_max, what):
    box_min = box["box_min_cm"]
    box_max = box["box_max_cm"]
    middle = [(box_min[i] + box_max[i]) * 0.5 for i in range(3)]
    shifted_middle = to_realm_world(middle, realm, realm_min, realm_max, what)
    offset = [shifted_middle[i] - middle[i] for i in range(3)]
    return [box_min[i] + offset[i] for i in range(3)], [box_max[i] + offset[i] for i in range(3)]


def field_passage_prefix(gate_id):
    return f"{FIELD_PASSAGE_LABEL_PREFIX}{gate_id}_"


def require_passage_gates(layout_path, realm, gates):
    missing = {g.get("gate_id", "?"): [k for k in REQUIRED_GATE_KEYS if k not in g] for g in gates}
    missing = {gate_id: keys for gate_id, keys in missing.items() if keys}
    missing_realm = [k for k in REALM_MIST_PROPERTIES if k not in realm]
    if missing or missing_realm or not gates:
        raise RuntimeError(f"{layout_path}: layout is not a P3-15 passage layout (gates missing {missing}, realm missing {missing_realm}); rerun generate_realm.py")


def field_passage_load_box(gate):
    x, y = gate["field_return"]["loc"][0], gate["field_return"]["loc"][1]
    yaw = math.radians(rotation_values(gate["field_return"]["rot"])[1])
    forward = (math.cos(yaw), math.sin(yaw))
    lateral = (-math.sin(yaw), math.cos(yaw))
    half = FIELD_PASSAGE_BAND_HALF_WIDTH_CM + FIELD_PASSAGE_LOAD_MARGIN_CM
    xs, ys = [], []
    for s in (-gate["approach_length_cm"] - FIELD_PASSAGE_LOAD_MARGIN_CM, gate["seam_buffer_cm"] + FIELD_PASSAGE_LOAD_MARGIN_CM):
        for l in (-half, half):
            xs.append(x + forward[0] * s + lateral[0] * l)
            ys.append(y + forward[1] * s + lateral[1] * l)
    return (min(xs), min(ys)), (max(xs), max(ys))


def resolve_layout_paths():
    if os.path.isfile(BUILD_TARGETS_PATH):
        realm_ids = json.load(open(BUILD_TARGETS_PATH, encoding="utf-8"))
        paths = [os.path.join(REALMS_ROOT, realm_id, "layout.json") for realm_id in realm_ids]
        missing = [p for p in paths if not os.path.isfile(p)]
        if missing:
            raise FileNotFoundError(f"build_targets.json lists realms without layout.json: {missing}")
        return paths
    return sorted(glob.glob(os.path.join(REALMS_ROOT, "*", "layout.json")))


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


def is_desc_relevant(desc, label, boxes, labels, prefixes):
    native_class = desc.native_class
    if native_class is not None and "HLOD" in native_class.get_name():
        return False
    if label in labels or label.startswith(prefixes):
        return True
    bounds = desc.bounds
    if not bounds.is_valid:
        return False
    for box_min, box_max in boxes:
        if bounds.max.x >= box_min[0] and bounds.min.x <= box_max[0] and bounds.max.y >= box_min[1] and bounds.min.y <= box_max[1]:
            return True
    return False


def load_world_partition_actors(boxes, labels, prefixes):
    library = unreal.WorldPartitionBlueprintLibrary
    descs = library.get_actor_descs()
    if descs is None:
        log("WARNING world partition actor descs unavailable; only loaded actors are handled")
        return {"available": False}
    loaded_labels = {a.get_actor_label() for a in eas.get_all_level_actors()}
    relevant = []
    for desc in descs:
        label = str(desc.label)
        if is_desc_relevant(desc, label, boxes, labels, tuple(prefixes)):
            relevant.append((desc, label))
    loaded_before = sum(1 for _, label in relevant if label in loaded_labels)
    if relevant:
        library.load_actors([desc.guid for desc, _ in relevant])
    loaded_after_labels = {a.get_actor_label() for a in eas.get_all_level_actors()}
    loaded_after = sum(1 for _, label in relevant if label in loaded_after_labels)
    result = {"available": True, "all_descs": len(descs), "relevant_descs": len(relevant), "loaded_before": loaded_before, "loaded_after": loaded_after}
    log(f"world partition: {result}")
    return result


def remove_previous_realm(world, realm_prefix, gates, box_min, box_max):
    library = unreal.TDWorldGenEditorLibrary
    removed = {"landscape": unreal.TDLandscapeEditorLibrary.destroy_landscape_actors_in_bounds(world, open_world.vec(box_min), open_world.vec(box_max))}
    removed["old_exit_gates"] = library.remove_actors_with_label_prefix(world, f"{realm_prefix}Exit_")
    removed["realm_prefix"] = library.remove_actors_with_label_prefix(world, realm_prefix)
    removed["old_field_gates"] = library.remove_actors_with_label_prefix(world, OLD_FIELD_GATE_LABEL_PREFIX)
    removed["field_passages"] = sum(library.remove_actors_with_label_prefix(world, field_passage_prefix(g["gate_id"])) for g in gates)
    log(f"removed: {removed}")
    return removed


def ensure_directory(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def load_or_create_material_instance(asset_path):
    if EAL.does_asset_exist(asset_path):
        return EAL.load_asset(asset_path)
    folder, name = asset_path.rsplit("/", 1)
    ensure_directory(folder)
    return asset_tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())


def build_material_instance(spec):
    asset_path = spec["asset"]
    result = {"asset": asset_path, "applied": [], "missing": [], "error": ""}
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


def build_landscape(world, cfg, realm_prefix, folder_root, tags, box_min, box_max):
    landscape = open_world.create_landscape_actor(world, cfg)
    proxies = sorted(open_world.landscape_proxies_in_box(world, box_min, box_max, REALM_BOX_MARGIN_CM), key=landscape_sort_key)
    streaming_index = 0
    for proxy in proxies:
        if isinstance(proxy, unreal.Landscape):
            label = f"{realm_prefix}Landscape"
        else:
            label = f"{realm_prefix}Landscape_{streaming_index:02d}"
            streaming_index += 1
        open_world.setp(proxy, "enable_nanite", True)
        proxy.set_actor_label(label)
        proxy.set_folder_path(f"{folder_root}/Landscape")
        proxy.tags = [unreal.Name(t) for t in tags]
    try:
        unreal.get_editor_subsystem(unreal.LandscapeSubsystem).build_grass_maps()
    except Exception as e:
        log(f"grass maps build not available: {str(e)[:80]}")
    log(f"landscape created: {landscape.get_actor_label()} proxies in realm box={len(proxies)}")
    return {"proxies": len(proxies), "streaming_proxies": streaming_index}


def split_layout(layout, has_nav_bounds):
    realm_part = {key: [] for key in LAYOUT_LIST_KEYS}
    passage_part = {key: [] for key in LAYOUT_LIST_KEYS}
    skipped = []
    for key in LAYOUT_LIST_KEYS:
        for entry in layout.get(key, []):
            label = entry.get("label", "")
            kind = entry.get("kind", "")
            if key == "markers" and (kind == "PlayerStart" or (kind == "NavMeshBounds" and has_nav_bounds)):
                skipped.append(label or kind)
                continue
            target = passage_part if label.startswith(FIELD_PASSAGE_LABEL_PREFIX) else realm_part
            target[key].append(entry)
    realm_part["ism"] = layout.get("ism", [])
    passage_part["ism"] = []
    if skipped:
        log(f"markers skipped (handled by this builder): {skipped}")
    return realm_part, passage_part


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


def spawn_nav_bounds(nav_bounds, realm, realm_min, realm_max, realm_prefix, folder_root, tags):
    if not nav_bounds:
        log("WARNING layout has no nav_bounds; navigation volume not created")
        return None
    box_min, box_max = resolve_box(nav_bounds, realm, realm_min, realm_max, "nav_bounds")
    _, result = spawn_box_volume(unreal.NavMeshBoundsVolume, box_min, box_max, f"{realm_prefix}NavBounds", f"{folder_root}/Navigation", tags)
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


def spawn_post_process(post_process, realm, realm_min, realm_max, realm_prefix, folder_root, tags, field_exposure_bias):
    if not post_process:
        log("WARNING layout has no post_process; volume not created")
        return None
    box_min, box_max = resolve_box(post_process, realm, realm_min, realm_max, "post_process")
    actor, result = spawn_box_volume(unreal.PostProcessVolume, box_min, box_max, f"{realm_prefix}PostProcess", f"{folder_root}/Atmosphere", tags)
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


def make_realm_gates(realm, gates, realm_min, realm_max):
    structs = []
    for gate in gates:
        arrival = dict(gate["arrival"])
        arrival["loc"] = to_realm_world(arrival["loc"], realm, realm_min, realm_max, f"{gate['gate_id']}.arrival")
        entry = unreal.TDRealmGate()
        entry.set_editor_property("gate_id", gate["gate_id"])
        entry.set_editor_property("arrival_transform", make_transform(arrival))
        entry.set_editor_property("field_return_transform", make_transform(gate["field_return"]))
        for name in GATE_LENGTH_PROPERTIES:
            entry.set_editor_property(name, float(gate[name]))
        structs.append(entry)
    return structs


def write_realm_definition(realm, gates, realm_min, realm_max):
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
    asset.set_editor_property("gates", make_realm_gates(realm, gates, realm_min, realm_max))
    for name in REALM_MIST_PROPERTIES:
        asset.set_editor_property(name, float(realm[name]))
    result = {"asset": asset_path, "gates": [g["gate_id"] for g in gates], "atmosphere_skipped": skipped,
              "passages": {g["gate_id"]: {name: g[name] for name in GATE_LENGTH_PROPERTIES} for g in gates}, "seam_mist": {name: realm[name] for name in REALM_MIST_PROPERTIES}}
    log(f"definition {result}")
    return result


def bake_realm(world, layout_path, field_exposure_bias):
    layout = json.load(open(layout_path, encoding="utf-8"))
    realm = layout["realm"]
    realm_id = realm["realm_id"]
    gates = realm.get("gates", [])
    require_passage_gates(layout_path, realm, gates)
    realm_prefix = f"TDRealm_{realm_id}_"
    folder_root = f"TDRealm/{realm_id}"
    realm_tags = ["TDRealm", f"TDRealm:{realm_id}"]
    realm_min, realm_max = realm_box(realm)
    log(f"realm {realm_id}: box {realm_min[:2]}~{realm_max[:2]} gates {[g['gate_id'] for g in gates]}")
    report = {"realm_id": realm_id, "layout": layout_path, "seed": layout.get("seed"), "generator_version": layout.get("generator_version")}

    load_boxes = [(realm_min, realm_max)] + [field_passage_load_box(g) for g in gates]
    load_prefixes = [realm_prefix, OLD_FIELD_GATE_LABEL_PREFIX] + [field_passage_prefix(g["gate_id"]) for g in gates]
    report["world_partition"] = load_world_partition_actors(load_boxes, set(), load_prefixes)
    report["world_partition"]["field_passage_boxes"] = [[list(box_min), list(box_max)] for box_min, box_max in load_boxes[1:]]
    report["removed"] = remove_previous_realm(world, realm_prefix, gates, realm_min, realm_max)
    report["materials"] = build_materials(realm)
    report["landscape"] = build_landscape(world, layout["landscape"], realm_prefix, folder_root, ["TDGen"] + realm_tags, realm_min, realm_max)

    realm_part, passage_part = split_layout(layout, bool(realm.get("nav_bounds")))
    report["spawned"] = spawn_layout_part(realm_part, realm_prefix, folder_root, realm_tags)
    report["spawned_field_passages"] = spawn_layout_part(passage_part, FIELD_PASSAGE_LABEL_PREFIX, FIELD_PASSAGE_FOLDER, FIELD_PASSAGE_TAGS)
    report["nav_bounds"] = spawn_nav_bounds(realm.get("nav_bounds"), realm, realm_min, realm_max, realm_prefix, folder_root, ["TDGen"] + realm_tags)
    report["post_process"] = spawn_post_process(realm.get("post_process"), realm, realm_min, realm_max, realm_prefix, folder_root, ["TDGen"] + realm_tags, field_exposure_bias)
    report["definition"] = write_realm_definition(realm, gates, realm_min, realm_max)
    return report


def write_report(report):
    path = os.path.join(os.path.dirname(report["layout"]), "build_report.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, ensure_ascii=False, indent=2)
    log(f"report written: {path}")


def main():
    layout_paths = resolve_layout_paths()
    if not layout_paths:
        log(f"no realm layouts under {REALMS_ROOT}")
        return
    world = ensure_level_loaded()
    field_exposure_bias = read_field_exposure_bias()
    reports = [bake_realm(world, path, field_exposure_bias) for path in layout_paths]
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved: {ok}")
    for report in reports:
        report["saved"] = bool(ok)
        write_report(report)
        log(f"summary {report['realm_id']}: spawned {report['spawned']} field passages {report['spawned_field_passages']} gates {report['definition']['gates']} landscape {report['landscape']}")


if __name__ == "__main__":
    main()

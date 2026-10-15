"""에디터 안에서 실행: 연속 세계(P3-19 명세 2.1~2.3)의 데이터 레이어(DL) 자산 5개와 LV_DarkFantasy_OpenWorld 인스턴스를 멱등으로 만들고, 빌더가 쓰는 DL 문맥·소속 판정 함수를 제공한다.
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_setup_data_layers.py (PowerShell, 두 번 실행해도 결과가 같다)
출력: /Game/World/DataLayers/DL_TDRegion_{Field,SunkenStarfield,PaleHeights}(Editor)·DL_TDGameplay_{SunkenStarfield,PaleHeights}(Runtime, 초기 Loaded), 레벨 WorldDataLayers 인스턴스, Saved/WorldGen/data_layers_report.json
상태: 현행 (2026-10-15, P3-19 S4 재베이크에서 실행). Python 바인딩 이름이 다르면 아무것도 만들기 전에 멈추고 빠진 이름을 보고한다.
"""
import contextlib
import json
import os
import re
import time

import unreal

LEVEL_PATH = "/Game/Level/LV_DarkFantasy_OpenWorld"
DATA_LAYER_FOLDER = "/Game/World/DataLayers"
FIELD_REGION_ID = "Field"
REALM_IDS = ("SunkenStarfield", "PaleHeights")
REGION_IDS = (FIELD_REGION_ID,) + REALM_IDS
EDITOR_TYPE_NAME = "EDITOR"
RUNTIME_TYPE_NAME = "RUNTIME"
GAMEPLAY_INITIAL_STATE_NAME = "LOADED"
DATA_LAYERS_ENV = "TD_WORLDGEN_DATA_LAYERS"
LANDSCAPE_DATA_LAYER_ENV = "TD_WORLDGEN_LANDSCAPE_DATA_LAYER"
REPORT_PATH = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen", "data_layers_report.json")
REQUIRED_BINDINGS = ("DataLayerAsset", "DataLayerEditorSubsystem", "DataLayerCreationParameters", "DataLayerType", "DataLayerRuntimeState", "WorldPartitionBlueprintLibrary")
REQUIRED_SUBSYSTEM_METHODS = ("create_data_layer_instance", "get_data_layer_instance", "get_all_data_layers", "add_to_actor_editor_context",
                              "remove_from_actor_editor_context", "set_data_layers_is_loaded_in_editor", "set_data_layers_visibility")
LANDSCAPE_STREAMING_PROXY_CLASS = "LandscapeStreamingProxy"
HEX_TOKEN = re.compile(r"[0-9A-F]+")
WRITE_RETRY_COUNT = 5
WRITE_RETRY_SECONDS = 2.0

t0 = time.time()


def log(msg):
    print(f"[TDDataLayers {time.time() - t0:6.1f}s] {msg}")


def region_layer_name(region_id):
    return f"DL_TDRegion_{region_id}"


def gameplay_layer_name(realm_id):
    return f"DL_TDGameplay_{realm_id}"


def layer_asset_path(layer_name):
    return f"{DATA_LAYER_FOLDER}/{layer_name}"


def layer_specs():
    specs = [(region_layer_name(region_id), EDITOR_TYPE_NAME) for region_id in REGION_IDS]
    return specs + [(gameplay_layer_name(realm_id), RUNTIME_TYPE_NAME) for realm_id in REALM_IDS]


KNOWN_LAYER_NAMES = tuple(name for name, _ in layer_specs())
REGION_LAYER_NAMES = frozenset(region_layer_name(region_id) for region_id in REGION_IDS)
LAYER_NAME_PATTERNS = {name: re.compile(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])") for name in KNOWN_LAYER_NAMES}


def is_enabled():
    return os.environ.get(DATA_LAYERS_ENV, "1") != "0"


def is_landscape_layer_enabled():
    return is_enabled() and os.environ.get(LANDSCAPE_DATA_LAYER_ENV, "1") != "0"


def missing_bindings():
    missing = [name for name in REQUIRED_BINDINGS if not hasattr(unreal, name)]
    if hasattr(unreal, "DataLayerEditorSubsystem"):
        missing += [f"DataLayerEditorSubsystem.{name}" for name in REQUIRED_SUBSYSTEM_METHODS if not hasattr(unreal.DataLayerEditorSubsystem, name)]
    return missing


def require_bindings():
    missing = missing_bindings()
    if missing:
        raise RuntimeError(f"data layer Python bindings missing, nothing was changed (spec 2.3 step 7): {missing}")


def editor_subsystem():
    return unreal.get_editor_subsystem(unreal.DataLayerEditorSubsystem)


def load_layer_asset(layer_name):
    path = layer_asset_path(layer_name)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    return unreal.EditorAssetLibrary.load_asset(path)


def find_instance(layer_name):
    asset = load_layer_asset(layer_name)
    if asset is None:
        return None
    return editor_subsystem().get_data_layer_instance(asset)


def find_known_instances():
    return {name: find_instance(name) for name in KNOWN_LAYER_NAMES}


def require_region_instances():
    instances = {region_id: find_instance(region_layer_name(region_id)) for region_id in REGION_IDS}
    missing = [region_layer_name(region_id) for region_id, instance in instances.items() if instance is None]
    if missing:
        raise RuntimeError(f"data layer instances missing {missing}: run Tools/WorldGen/editor_setup_data_layers.py first (or set {DATA_LAYERS_ENV}=0 to build without data layers)")
    return instances


def set_layers_loaded_and_visible(instances):
    if not instances:
        return
    subsystem = editor_subsystem()
    subsystem.set_data_layers_is_loaded_in_editor(instances, True, False)
    subsystem.set_data_layers_visibility(instances, True)


def clear_actor_editor_context(instances):
    subsystem = editor_subsystem()
    for instance in instances:
        subsystem.remove_from_actor_editor_context(instance)


def prepare_for_build():
    if not is_enabled():
        log(f"data layers disabled by {DATA_LAYERS_ENV}=0; actors are built without data layers")
        return {"enabled": False, "landscape_layer": False}
    require_bindings()
    instances = require_region_instances()
    present = [instance for instance in find_known_instances().values() if instance is not None]
    set_layers_loaded_and_visible(present)
    clear_actor_editor_context(present)
    state = {"enabled": True, "landscape_layer": is_landscape_layer_enabled(), "region_layers": sorted(region_layer_name(r) for r in instances), "known_layers_loaded": len(present)}
    log(f"data layers ready: {state}")
    return state


@contextlib.contextmanager
def region_context(region_id, include=True):
    if not include or not is_enabled():
        yield None
        return
    instances = require_region_instances()
    clear_actor_editor_context([instance for instance in find_known_instances().values() if instance is not None])
    target = instances[region_id]
    subsystem = editor_subsystem()
    try:
        subsystem.add_to_actor_editor_context(target)
        yield target
    finally:
        subsystem.remove_from_actor_editor_context(target)


def layer_names_in_text(text):
    return {name for name, pattern in LAYER_NAME_PATTERNS.items() if pattern.search(text)}


def soft_path_text(path):
    if isinstance(path, str):
        return path
    if not hasattr(path, "export_text"):
        return str(path)
    try:
        text = path.export_text()
    except Exception:
        return str(path)
    return str(text) if text else str(path)


def desc_layer_names(desc):
    names = set()
    for path in list(getattr(desc, "data_layer_assets", None) or []):
        names |= layer_names_in_text(soft_path_text(path))
    return names


def actor_layer_names(actor):
    try:
        assets = actor.get_editor_property("data_layer_assets")
    except Exception:
        return set()
    names = set()
    for asset in list(assets or []):
        if asset is not None:
            names |= layer_names_in_text(asset.get_path_name())
    return names


def partition_name_has_context_hash(object_name):
    tokens = object_name.split("_")
    if len(tokens) < 3:
        return False
    return tokens[-2] == "0" and tokens[-1] != "0" and HEX_TOKEN.fullmatch(tokens[-1]) is not None


def check_landscape_layer(landscape_actors, region_id):
    layer = region_layer_name(region_id)
    missing_layer, extra_layer, missing_hash = [], [], []
    for actor in landscape_actors:
        name = actor.get_name()
        names = actor_layer_names(actor)
        if layer not in names:
            missing_layer.append(name)
        if names - {layer}:
            extra_layer.append(name)
        if actor.get_class().get_name() == LANDSCAPE_STREAMING_PROXY_CLASS and not partition_name_has_context_hash(name):
            missing_hash.append(name)
    ok = bool(landscape_actors) and not (missing_layer or extra_layer or missing_hash)
    return {"ok": ok, "layer": layer, "actors": len(landscape_actors), "missing_layer_count": len(missing_layer), "extra_layer_count": len(extra_layer),
            "missing_hash_count": len(missing_hash), "missing_layer": missing_layer[:10], "extra_layer": extra_layer[:10], "missing_hash": missing_hash[:10]}


def ensure_level_loaded():
    level_name = LEVEL_PATH.rsplit("/", 1)[-1]
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    if world is not None and world.get_name() == level_name:
        return world
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL_PATH)
    world = editor.get_editor_world()
    if world is None or world.get_name() != level_name:
        raise RuntimeError(f"level {LEVEL_PATH} could not be loaded")
    return world


def create_layer_asset(layer_name):
    library = unreal.EditorAssetLibrary
    if not library.does_directory_exist(DATA_LAYER_FOLDER):
        library.make_directory(DATA_LAYER_FOLDER)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = None
    if hasattr(unreal, "DataLayerFactory"):
        try:
            asset = tools.create_asset(layer_name, DATA_LAYER_FOLDER, unreal.DataLayerAsset, unreal.DataLayerFactory())
        except Exception as e:
            log(f"DataLayerFactory create failed for {layer_name}: {str(e)[:160]}; retrying without factory")
    if asset is None:
        asset = tools.create_asset(layer_name, DATA_LAYER_FOLDER, unreal.DataLayerAsset, None)
    if asset is None:
        raise RuntimeError(f"data layer asset {layer_asset_path(layer_name)} could not be created")
    return asset


def ensure_editor_property(target, name, value, changes):
    if target.get_editor_property(name) == value:
        return
    target.set_editor_property(name, value)
    changes.append(f"{target.get_name()}.{name}")


def ensure_instance(asset, changes):
    subsystem = editor_subsystem()
    instance = subsystem.get_data_layer_instance(asset)
    if instance is not None:
        return instance
    parameters = unreal.DataLayerCreationParameters()
    parameters.set_editor_property("data_layer_asset", asset)
    parameters.set_editor_property("is_private", False)
    instance = subsystem.create_data_layer_instance(parameters)
    if instance is None:
        raise RuntimeError(f"data layer instance for {asset.get_path_name()} could not be created")
    changes.append(f"{asset.get_name()}.instance")
    return instance


def setup_layer(layer_name, type_name, changes):
    asset = load_layer_asset(layer_name)
    if asset is None:
        asset = create_layer_asset(layer_name)
        changes.append(f"{layer_name}.asset")
    ensure_editor_property(asset, "data_layer_type", getattr(unreal.DataLayerType, type_name), changes)
    instance = ensure_instance(asset, changes)
    if type_name == RUNTIME_TYPE_NAME:
        ensure_editor_property(instance, "initial_runtime_state", getattr(unreal.DataLayerRuntimeState, GAMEPLAY_INITIAL_STATE_NAME), changes)
    ensure_editor_property(instance, "is_initially_loaded_in_editor", True, changes)
    ensure_editor_property(instance, "is_initially_visible", True, changes)
    return asset, instance


def instance_counts_by_layer():
    counts = {name: 0 for name in KNOWN_LAYER_NAMES}
    for instance in list(editor_subsystem().get_all_data_layers() or []):
        asset = instance.get_asset() if hasattr(instance, "get_asset") else None
        if asset is not None and asset.get_name() in counts:
            counts[asset.get_name()] += 1
    return counts


def describe_layer(layer_name, type_name, asset, instance):
    expected_state = getattr(unreal.DataLayerRuntimeState, GAMEPLAY_INITIAL_STATE_NAME)
    return {"layer": layer_name, "asset": asset.get_path_name(), "expected_type": type_name, "type": str(asset.get_editor_property("data_layer_type")),
            "instance": instance.get_name(), "is_runtime": bool(instance.is_runtime()), "initial_runtime_state": str(instance.get_initial_runtime_state()),
            "initial_state_ok": type_name != RUNTIME_TYPE_NAME or instance.get_initial_runtime_state() == expected_state, "is_visible": bool(instance.is_visible())}


def verification_problems(layers, counts):
    problems = [f"{name}: {count} instances, expected 1" for name, count in counts.items() if count != 1]
    for entry in layers:
        if entry["is_runtime"] != (entry["expected_type"] == RUNTIME_TYPE_NAME):
            problems.append(f"{entry['layer']}: is_runtime={entry['is_runtime']} but expected type {entry['expected_type']}")
        if not entry["initial_state_ok"]:
            problems.append(f"{entry['layer']}: initial runtime state {entry['initial_runtime_state']}, expected {GAMEPLAY_INITIAL_STATE_NAME}")
    return problems


def write_report(report):
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    temporary = REPORT_PATH + ".tmp"
    for attempt in range(WRITE_RETRY_COUNT):
        try:
            with open(temporary, "w", encoding="utf-8", newline="\n") as fp:
                fp.write(text)
            os.replace(temporary, REPORT_PATH)
            return
        except OSError as e:
            if attempt == WRITE_RETRY_COUNT - 1:
                raise
            log(f"report write retry after {e}")
            time.sleep(WRITE_RETRY_SECONDS)


def main():
    require_bindings()
    ensure_level_loaded()
    changes = []
    layers = []
    instances = []
    for layer_name, type_name in layer_specs():
        asset, instance = setup_layer(layer_name, type_name, changes)
        instances.append(instance)
        layers.append((layer_name, type_name, asset, instance))
    set_layers_loaded_and_visible(instances)
    clear_actor_editor_context(instances)
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    described = [describe_layer(*entry) for entry in layers]
    counts = instance_counts_by_layer()
    problems = verification_problems(described, counts)
    report = {"level": LEVEL_PATH, "changes": changes, "saved": bool(saved), "instance_counts": counts, "layers": described, "problems": problems}
    write_report(report)
    log(f"changes {len(changes)}: {changes}")
    log(f"instances {counts}; saved {bool(saved)}; report {REPORT_PATH}")
    if problems:
        raise RuntimeError(f"data layer verification failed: {problems}")
    log("DONE data layers ready")


if __name__ == "__main__":
    main()

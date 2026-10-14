"""에디터 안에서 실행: 오픈월드 레벨 배치물(StaticMesh·ISM·HISM, 베이크 액터·TDGen_ISM_*·TDRealm_*·마을·PCG 파티션 액터 출력)의 재질 슬롯을 탑다운 거울 MI_TD_*로 바꾸고 이계 MI 부모를 거울로 맞춘다(D-43).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_apply_topdown_materials.py (PowerShell). 옵션은 Saved/WorldGen/topdown_apply_request.json {"dry_run": false, "patch_pcg": false, "reparent_realm": true}, 파일이 없으면 기본값. 베이크·PCG 재생성 뒤에 실행한다. 재실행 안전.
출력: 바뀐 액터 외부 패키지(+PCG 패치 시 그래프 에셋)만 저장, Saved/WorldGen/topdown_apply_report.json. dry_run은 아무것도 바꾸지 않고 센다.
상태: 현행(P3-17). 랜드스케이프·거울 없는 재질(물·반투명·데칼·파티클)은 건너뛰고 이름별로 센다
"""
import collections
import importlib
import json
import os
import sys
import time

import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
WORLDGEN_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "WorldGen")
if WORLDGEN_TOOLS_DIR not in sys.path:
    sys.path.insert(0, WORLDGEN_TOOLS_DIR)

td = importlib.reload(importlib.import_module("topdown_materials"))

LEVEL_PATH = "/Game/Level/LV_DarkFantasy_OpenWorld"
PCG_GRAPH_ROOT = "/Game/World/PCG"
REQUEST_PATH = os.environ.get("TD_TOPDOWN_APPLY_REQUEST", os.path.join(PROJECT_DIR, "Saved", "WorldGen", "topdown_apply_request.json"))
REPORT_PATH = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "topdown_apply_report.json")
SKIPPED_ACTOR_CLASS_PREFIX = "Landscape"
SAVE_CHUNK = 200

EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
t0 = time.time()


def log(message):
    print(f"[TDTopDownApply {time.time() - t0:6.1f}s] {message}")


def read_request():
    request = {"dry_run": False, "patch_pcg": False, "reparent_realm": True}
    if not os.path.isfile(REQUEST_PATH):
        return request
    with open(REQUEST_PATH, encoding="utf-8-sig") as stream:
        request.update(json.load(stream))
    return request


def ensure_level():
    world = ues.get_editor_world()
    if world is not None and world.get_path_name().split(".")[0] == LEVEL_PATH:
        return world
    les.load_level(LEVEL_PATH)
    return ues.get_editor_world()


def load_all_partition_actors():
    library = unreal.WorldPartitionBlueprintLibrary
    descs = library.get_actor_descs()
    if descs is None:
        log("WARNING world partition actor descs unavailable; only loaded actors are handled")
        return 0
    guids = []
    for desc in descs:
        native_class = desc.native_class
        class_name = native_class.get_name() if native_class else ""
        if class_name.startswith(SKIPPED_ACTOR_CLASS_PREFIX) or "HLOD" in class_name:
            continue
        guids.append(desc.guid)
    if guids:
        library.load_actors(guids)
    log(f"world partition actors loaded: {len(guids)} of {len(descs)}")
    return len(guids)


class MirrorResolver:
    def __init__(self):
        self.cache = {}

    def resolve(self, material_path):
        if material_path in self.cache:
            return self.cache[material_path]
        mirror_path = td.topdown_material_path(material_path)
        if mirror_path is None:
            result = ("no_counterpart", None)
        elif not EAL.does_asset_exist(mirror_path):
            result = ("no_mirror_asset", None)
        else:
            result = ("mirror", EAL.load_asset(mirror_path))
        self.cache[material_path] = result
        return result


class LevelCounters:
    def __init__(self):
        self.slots = collections.Counter()
        self.components = collections.Counter()
        self.changed_by_actor_class = collections.Counter()
        self.skipped_materials = collections.Counter()
        self.missing_mirror_materials = collections.Counter()
        self.mirror_usage = collections.Counter()
        self.changed_actors = {}


def should_skip_actor(actor):
    return isinstance(actor, unreal.LandscapeProxy) or actor.get_class().get_name().startswith(SKIPPED_ACTOR_CLASS_PREFIX)


def apply_to_component(actor, component, resolver, counters, dry_run):
    counters.components[component.get_class().get_name()] += 1
    component_modified = False
    for slot in range(component.get_num_materials()):
        material = component.get_material(slot)
        counters.slots["total"] += 1
        if material is None:
            counters.slots["empty"] += 1
            continue
        material_path = td.to_package_path(material.get_path_name())
        if td.is_topdown_path(material_path):
            counters.slots["already_topdown"] += 1
            continue
        status, mirror = resolver.resolve(material_path)
        if status == "no_counterpart":
            counters.slots["skipped_no_counterpart"] += 1
            counters.skipped_materials[td.asset_name(material_path)] += 1
            continue
        if status == "no_mirror_asset":
            counters.slots["skipped_no_mirror_asset"] += 1
            counters.missing_mirror_materials[td.asset_name(material_path)] += 1
            continue
        counters.slots["changed"] += 1
        counters.changed_by_actor_class[actor.get_class().get_name()] += 1
        counters.mirror_usage[td.asset_name(mirror.get_path_name())] += 1
        if dry_run:
            continue
        if not component_modified:
            component.modify()
            component_modified = True
        component.set_material(slot, mirror)
    return component_modified


def apply_to_level(dry_run):
    resolver = MirrorResolver()
    counters = LevelCounters()
    scanned = 0
    for actor in eas.get_all_level_actors():
        if should_skip_actor(actor):
            counters.slots["skipped_landscape_actors"] += 1
            continue
        scanned += 1
        actor_modified = False
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            if apply_to_component(actor, component, resolver, counters, dry_run):
                actor_modified = True
        if actor_modified:
            actor.modify()
            counters.changed_actors[actor.get_path_name()] = actor
    return scanned, counters


def save_changed_actors(changed_actors):
    packages = []
    seen = set()
    for actor in changed_actors.values():
        package = actor.get_package()
        name = package.get_path_name()
        if name in seen:
            continue
        seen.add(name)
        packages.append(package)
    saved = 0
    for start in range(0, len(packages), SAVE_CHUNK):
        chunk = packages[start:start + SAVE_CHUNK]
        if unreal.EditorLoadingAndSavingUtils.save_packages(chunk, True):
            saved += len(chunk)
        else:
            log(f"WARNING save_packages failed for chunk starting {start}")
    log(f"saved packages {saved} of {len(packages)}")
    return len(packages), saved


def describe_pcg_components():
    rows = []
    for actor in eas.get_all_level_actors():
        for component in actor.get_components_by_class(unreal.PCGComponent):
            rows.append({
                "actor": actor.get_actor_label(),
                "class": actor.get_class().get_name(),
                "trigger": component.get_editor_property("generation_trigger").name,
                "partitioned": bool(component.get_editor_property("is_component_partitioned")),
                "generated": bool(component.get_editor_property("generated")),
                "regenerate_in_editor": bool(component.get_editor_property("regenerate_in_editor")),
            })
    return rows


def summarize_pcg_components(rows):
    summary = collections.Counter()
    for row in rows:
        summary[f"{row['class']}|{row['trigger']}|partitioned={row['partitioned']}|generated={row['generated']}|regenerate_in_editor={row['regenerate_in_editor']}"] += 1
    return dict(summary)


def mesh_slot_materials(mesh):
    return [item.get_editor_property("material_interface") for item in mesh.get_editor_property("static_materials")]


def wanted_override_materials(mesh, resolver):
    wanted = []
    changed = False
    for original in mesh_slot_materials(mesh):
        if original is None:
            wanted.append(None)
            continue
        status, mirror = resolver.resolve(td.to_package_path(original.get_path_name()))
        if status == "mirror":
            wanted.append(mirror)
            changed = True
        else:
            wanted.append(original)
    return wanted if changed else None


def material_paths(materials):
    return [td.to_package_path(item.get_path_name()) if item else "" for item in materials]


def patch_pcg_graphs(resolver, dry_run):
    results = []
    for asset_path in EAL.list_assets(PCG_GRAPH_ROOT, recursive=True, include_folder=False):
        graph = EAL.load_asset(asset_path)
        if not isinstance(graph, unreal.PCGGraph):
            continue
        entry_count = 0
        patched_count = 0
        for node in graph.get_editor_property("nodes"):
            settings = node.get_settings()
            if not isinstance(settings, unreal.PCGStaticMeshSpawnerSettings):
                continue
            selector = settings.get_editor_property("mesh_selector_parameters")
            entries = list(selector.get_editor_property("mesh_entries"))
            node_patched = False
            for entry in entries:
                entry_count += 1
                descriptor = entry.get_editor_property("descriptor")
                mesh = descriptor.get_editor_property("static_mesh")
                if mesh is None:
                    continue
                wanted = wanted_override_materials(mesh, resolver)
                if wanted is None:
                    continue
                current = list(descriptor.get_editor_property("override_materials"))
                if material_paths(current) == material_paths(wanted):
                    continue
                patched_count += 1
                if dry_run:
                    continue
                descriptor.set_editor_property("override_materials", wanted)
                entry.set_editor_property("descriptor", descriptor)
                node_patched = True
            if node_patched:
                selector.set_editor_property("mesh_entries", entries)
        if patched_count and not dry_run:
            EAL.save_loaded_asset(graph, False)
        results.append({"graph": asset_path, "entries": entry_count, "patched": patched_count})
    return results


def main():
    request = read_request()
    dry_run = bool(request["dry_run"])
    world = ensure_level()
    log(f"level {world.get_path_name()} request {request}")
    loaded = load_all_partition_actors()
    reparented = td.reparent_realm_instances(dry_run) if request["reparent_realm"] else []
    scanned, counters = apply_to_level(dry_run)
    packages, saved = (0, 0) if dry_run else save_changed_actors(counters.changed_actors)
    pcg_rows = describe_pcg_components()
    pcg_patch = patch_pcg_graphs(MirrorResolver(), dry_run) if request["patch_pcg"] else []
    report = {
        "dry_run": dry_run,
        "request": request,
        "level": world.get_path_name(),
        "partition_actors_requested": loaded,
        "actors_scanned": scanned,
        "components": dict(counters.components),
        "slots": dict(counters.slots),
        "changed_actors": len(counters.changed_actors),
        "changed_slots_by_actor_class": dict(counters.changed_by_actor_class),
        "mirror_usage": dict(counters.mirror_usage),
        "skipped_no_counterpart_materials": dict(counters.skipped_materials),
        "skipped_no_mirror_asset_materials": dict(counters.missing_mirror_materials),
        "saved_packages": saved,
        "packages_to_save": packages,
        "reparented_realm_instances": reparented,
        "pcg_components": summarize_pcg_components(pcg_rows),
        "pcg_graph_patch": pcg_patch,
    }
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=1)
    log(f"DONE dry_run={dry_run} scanned={scanned} changed_actors={len(counters.changed_actors)} slots={dict(counters.slots)} saved={saved}/{packages}")


main()

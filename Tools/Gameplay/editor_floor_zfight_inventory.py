"""실행 환경: 에디터 안(Tools/run_in_editor.py). PIE 가 돌고 있으면 PIE 세계를, 아니면 에디터 세계를 읽는다(읽기만, 저장 없음). 오픈 월드 경로 주변 셀이 로드돼 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/Gameplay/editor_floor_zfight_inventory.py
출력: Saved/Gameplay/FloorPopping/zfight_inventory_<YYYYMMDD_HHMMSS>.json(지형 액터·컴포넌트 범위와 서로 다른 지형끼리 겹침, 경로 구역의 납작한 메시·인스턴스·데칼·HLOD·지형 Nanite 컴포넌트 상태) + 표준 출력 요약
상태: GF-07 바닥 Z-파이팅 진단용(2026-11-20), 미실행(미검증) — 원인은 layout.json 판 높이 대조로 먼저 찾음. 지면 높이와의 비교는 시스템 Python 쪽에서 높이 지도로 한다.
"""
import datetime
import json
import os

import unreal

CORRIDOR_MIN_CM = (-27500.0, -9500.0)
CORRIDOR_MAX_CM = (-8500.0, 5000.0)
FLAT_HEIGHT_LIMIT_CM = 60.0
FLAT_SPAN_MINIMUM_CM = 50.0
INSTANCE_SAMPLE_LIMIT = 400
OVERLAP_AREA_MINIMUM_CM2 = 10000.0
OUTPUT_DIRECTORY = os.path.join(unreal.Paths.project_saved_dir(), "Gameplay", "FloorPopping")


def find_world():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    game_world = editor.get_game_world()
    if game_world:
        return game_world, True
    return editor.get_editor_world(), False


def vector_list(vector):
    return [round(vector.x, 1), round(vector.y, 1), round(vector.z, 1)]


def is_inside_corridor(origin, extent):
    return (origin.x + extent.x >= CORRIDOR_MIN_CM[0] and origin.x - extent.x <= CORRIDOR_MAX_CM[0]
            and origin.y + extent.y >= CORRIDOR_MIN_CM[1] and origin.y - extent.y <= CORRIDOR_MAX_CM[1])


def read_property(target, name):
    try:
        return target.get_editor_property(name)
    except Exception:
        return None


def describe_material(material):
    if material is None:
        return None
    base = material.get_base_material() if hasattr(material, "get_base_material") else None
    blend = read_property(base, "blend_mode") if base else None
    domain = read_property(base, "material_domain") if base else None
    return {"path": material.get_path_name(), "blend": str(blend), "domain": str(domain)}


def component_bounds(component):
    origin, extent, _ = unreal.SystemLibrary.get_component_bounds(component)
    return origin, extent


def object_name_of(text):
    return str(text).split(".")[-1].strip("'>\" ")


def landscape_group_of(proxy):
    guid = read_property(proxy, "landscape_guid")
    if guid is not None:
        return str(guid)
    if isinstance(proxy, unreal.Landscape):
        return proxy.get_name()
    for name in ("landscape_actor_ref", "landscape_actor"):
        value = read_property(proxy, name)
        if value is not None:
            return object_name_of(value)
    return proxy.get_name()


def collect_landscapes(actors):
    proxies = []
    components = []
    nanite = []
    for actor in actors:
        if not isinstance(actor, unreal.LandscapeProxy):
            continue
        origin, extent = actor.get_actor_bounds(False)
        group = landscape_group_of(actor)
        proxies.append({"label": actor.get_actor_label(), "class": actor.get_class().get_name(), "group": group, "min": vector_list(origin - extent), "max": vector_list(origin + extent)})
        for component in actor.get_components_by_class(unreal.LandscapeComponent):
            component_origin, component_extent = component_bounds(component)
            components.append({"proxy": actor.get_actor_label(), "group": group, "min": vector_list(component_origin - component_extent), "max": vector_list(component_origin + component_extent), "visible": component.is_visible()})
        nanite_class = getattr(unreal, "LandscapeNaniteComponent", None)
        if nanite_class is None:
            continue
        for component in actor.get_components_by_class(nanite_class):
            nanite.append({"proxy": actor.get_actor_label(), "visible": component.is_visible(), "hidden_in_game": bool(read_property(component, "hidden_in_game")), "registered": component.is_registered()})
    return proxies, components, nanite


def find_landscape_overlaps(components):
    overlaps = []
    for first_index, first in enumerate(components):
        for second in components[first_index + 1:]:
            if first["group"] == second["group"]:
                continue
            width = min(first["max"][0], second["max"][0]) - max(first["min"][0], second["min"][0])
            depth = min(first["max"][1], second["max"][1]) - max(first["min"][1], second["min"][1])
            if width <= 0.0 or depth <= 0.0 or width * depth < OVERLAP_AREA_MINIMUM_CM2:
                continue
            overlaps.append({"first": first["proxy"], "second": second["proxy"], "area_m2": round(width * depth / 10000.0, 1), "first_z": [first["min"][2], first["max"][2]], "second_z": [second["min"][2], second["max"][2]]})
    return overlaps


def is_flat_box(box_min, box_max):
    height = box_max.z - box_min.z
    span = min(box_max.x - box_min.x, box_max.y - box_min.y)
    return height <= FLAT_HEIGHT_LIMIT_CM and span >= FLAT_SPAN_MINIMUM_CM


def instance_location(component, index):
    result = component.get_instance_transform(index, True)
    transform = result[1] if isinstance(result, tuple) else result
    return transform.translation if transform is not None else None


def describe_instanced(actor, component, mesh):
    box = mesh.get_bounding_box()
    count = component.get_instance_count()
    samples = []
    inside = 0
    for index in range(count):
        location = instance_location(component, index)
        if location is None:
            continue
        if not (CORRIDOR_MIN_CM[0] <= location.x <= CORRIDOR_MAX_CM[0] and CORRIDOR_MIN_CM[1] <= location.y <= CORRIDOR_MAX_CM[1]):
            continue
        inside += 1
        if len(samples) < INSTANCE_SAMPLE_LIMIT:
            samples.append(vector_list(location))
    return {"owner": actor.get_actor_label(), "owner_class": actor.get_class().get_name(), "component": component.get_class().get_name(), "mesh": mesh.get_path_name(),
            "mesh_box_min": vector_list(box.min), "mesh_box_max": vector_list(box.max), "instances_total": count, "instances_in_corridor": inside, "instance_locations": samples,
            "materials": [describe_material(material) for material in component.get_materials()]}


def collect_ground_pieces(actors):
    flat_meshes = []
    flat_instanced = []
    decals = []
    hlods = []
    for actor in actors:
        if isinstance(actor, unreal.LandscapeProxy):
            continue
        origin, extent = actor.get_actor_bounds(False)
        if not is_inside_corridor(origin, extent):
            continue
        if "HLOD" in actor.get_class().get_name():
            hlods.append({"label": actor.get_actor_label(), "class": actor.get_class().get_name(), "min": vector_list(origin - extent), "max": vector_list(origin + extent), "hidden": actor.is_hidden_ed() if hasattr(actor, "is_hidden_ed") else None})
        for component in actor.get_components_by_class(unreal.DecalComponent):
            component_origin, component_extent = component_bounds(component)
            decals.append({"owner": actor.get_actor_label(), "material": describe_material(component.get_decal_material()), "sort_order": read_property(component, "sort_order"), "center": vector_list(component_origin), "extent": vector_list(component_extent), "visible": component.is_visible()})
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = read_property(component, "static_mesh")
            if mesh is None or not component.is_visible():
                continue
            if isinstance(component, unreal.InstancedStaticMeshComponent):
                box = mesh.get_bounding_box()
                if is_flat_box(box.min, box.max):
                    flat_instanced.append(describe_instanced(actor, component, mesh))
                continue
            component_origin, component_extent = component_bounds(component)
            if not is_flat_box(component_origin - component_extent, component_origin + component_extent):
                continue
            flat_meshes.append({"owner": actor.get_actor_label(), "owner_class": actor.get_class().get_name(), "mesh": mesh.get_path_name(), "center": vector_list(component_origin), "extent": vector_list(component_extent),
                                "mobility": str(read_property(component, "mobility")), "materials": [describe_material(material) for material in component.get_materials()]})
    return flat_meshes, flat_instanced, decals, hlods


def read_console_value(world, name):
    try:
        return unreal.SystemLibrary.get_console_variable_int_value(name)
    except Exception:
        return None


def main():
    world, is_pie = find_world()
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    proxies, components, nanite = collect_landscapes(actors)
    overlaps = find_landscape_overlaps(components)
    flat_meshes, flat_instanced, decals, hlods = collect_ground_pieces(actors)
    report = {
        "world": world.get_path_name(), "is_pie": is_pie, "actor_count": len(actors),
        "corridor_min_cm": CORRIDOR_MIN_CM, "corridor_max_cm": CORRIDOR_MAX_CM,
        "landscape_proxies": proxies, "landscape_components": components, "landscape_overlaps": overlaps,
        "landscape_nanite_components": nanite, "render_nanite_cvar": read_console_value(world, "landscape.RenderNanite"),
        "flat_meshes": flat_meshes, "flat_instanced": flat_instanced, "decals": decals, "hlods": hlods,
    }
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    path = os.path.join(OUTPUT_DIRECTORY, "zfight_inventory_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + ".json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    groups = sorted({proxy["group"] for proxy in proxies})
    print(f"zfight inventory: {path}")
    print(f"world {report['world']} pie={is_pie} actors={len(actors)} landscape proxies={len(proxies)} groups={len(groups)} components={len(components)} overlaps={len(overlaps)}")
    print(f"nanite components={len(nanite)} visible={sum(1 for item in nanite if item['visible'])} render_nanite={report['render_nanite_cvar']}")
    print(f"flat meshes={len(flat_meshes)} flat instanced={len(flat_instanced)} (in corridor {sum(item['instances_in_corridor'] for item in flat_instanced)}) decals={len(decals)} hlods={len(hlods)}")


main()

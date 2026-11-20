"""실행 환경: 에디터 안(Tools/run_in_editor.py), PIE 가 돌고 있을 때만. PIE 세계의 웅덩이 판(TDGen_Puddle, SM_Plane + MI_PuddlesDecal)만 바꾸고 에디터 레벨·에셋은 건드리지 않으며 저장하지 않는다.
실행 명령: python Tools/run_in_editor.py -c "import sys; sys.argv=['x','<status|hide|show|decals|clear>']; exec(open('C:/Project/TDGame/Tools/Gameplay/editor_puddle_decal_preview.py', encoding='utf-8').read()) (절대 경로)"
출력: 표준 출력 한 줄 요약(판 수·판 메시 크기·재질 도메인·바꾼 수) + Saved/Gameplay/FloorPopping/puddle_preview_<모드>.json
상태: GF-07 바닥 Z-파이팅 원인 확인·고침 미리보기(2026-11-20), 미실행(미검증) — 실제 변환(editor_convert_puddle_planes.py)으로 바로 진행함. decals 는 판 자리마다 지형으로 투사하는 데칼 액터를 PIE 에만 만들고 판을 숨긴다, clear 는 되돌린다.
"""
import json
import os
import sys

import unreal

PUDDLE_LABEL_PREFIX = "TDGen_Puddle"
PUDDLE_MATERIAL_NAME = "MI_PuddlesDecal"
PREVIEW_TAG = "GF07PuddlePreview"
PROJECTION_HALF_DEPTH_CM = 40.0
OUTPUT_DIRECTORY = os.path.join(unreal.Paths.project_saved_dir(), "Gameplay", "FloorPopping")


def find_pie_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def is_puddle_plane(actor):
    if not isinstance(actor, unreal.StaticMeshActor):
        return False
    component = actor.static_mesh_component
    materials = component.get_materials() if component else []
    has_puddle_material = any(material and PUDDLE_MATERIAL_NAME in material.get_name() for material in materials)
    return has_puddle_material or actor.get_actor_label().startswith(PUDDLE_LABEL_PREFIX)


def find_puddle_planes(world):
    return [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor) if is_puddle_plane(actor)]


def find_preview_decals(world):
    return [actor for actor in unreal.GameplayStatics.get_all_actors_with_tag(world, PREVIEW_TAG) if isinstance(actor, unreal.DecalActor)]


def describe_plane_asset(planes):
    if not planes:
        return {}
    component = planes[0].static_mesh_component
    mesh = component.get_editor_property("static_mesh")
    box = mesh.get_bounding_box()
    material = component.get_materials()[0]
    base = material.get_base_material()
    return {"mesh": mesh.get_path_name(), "mesh_box_min": [box.min.x, box.min.y, box.min.z], "mesh_box_max": [box.max.x, box.max.y, box.max.z],
            "material": material.get_path_name(), "domain": str(base.get_editor_property("material_domain")), "blend": str(base.get_editor_property("blend_mode"))}


def spawn_decal_for(world, plane):
    component = plane.static_mesh_component
    mesh = component.get_editor_property("static_mesh")
    box = mesh.get_bounding_box()
    scale = plane.get_actor_scale3d()
    half_x = (box.max.x - box.min.x) * 0.5 * abs(scale.x)
    half_y = (box.max.y - box.min.y) * 0.5 * abs(scale.y)
    yaw = plane.get_actor_rotation().yaw
    transform = unreal.Transform(plane.get_actor_location(), unreal.Rotator(roll=0.0, pitch=-90.0, yaw=yaw), unreal.Vector(1.0, 1.0, 1.0))
    actor = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(world, unreal.DecalActor, transform)
    if actor is None:
        return False
    actor.tags = [PREVIEW_TAG]
    decal = actor.decal
    decal.set_editor_property("decal_material", component.get_materials()[0])
    decal.set_editor_property("decal_size", unreal.Vector(PROJECTION_HALF_DEPTH_CM, half_y, half_x))
    unreal.GameplayStatics.finish_spawning_actor(actor, transform)
    return True


def run_mode(mode):
    world = find_pie_world()
    if world is None:
        print("puddle preview: PIE 세계 없음 — 아무것도 바꾸지 않음")
        return {"mode": mode, "error": "no pie world"}
    planes = find_puddle_planes(world)
    report = {"mode": mode, "planes": len(planes), "asset": describe_plane_asset(planes)}
    if mode == "hide":
        for plane in planes:
            plane.set_actor_hidden_in_game(True)
    if mode == "show":
        for plane in planes:
            plane.set_actor_hidden_in_game(False)
    if mode == "decals":
        spawned = 0
        for plane in planes:
            if spawn_decal_for(world, plane):
                plane.set_actor_hidden_in_game(True)
                spawned += 1
        report["decals_spawned"] = spawned
    if mode == "clear":
        decals = find_preview_decals(world)
        for decal in decals:
            decal.destroy_actor()
        for plane in planes:
            plane.set_actor_hidden_in_game(False)
        report["decals_removed"] = len(decals)
    report["preview_decals_now"] = len(find_preview_decals(world))
    return report


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    report = run_mode(mode)
    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)
    with open(os.path.join(OUTPUT_DIRECTORY, f"puddle_preview_{mode}.json"), "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    print("puddle preview:", json.dumps(report, ensure_ascii=False))


main()

"""실행 환경: 에디터 안(Tools/run_in_editor.py), PIE 가 꺼져 있을 때. LV_DarkFantasy_OpenWorld 를 열고 월드 파티션 웅덩이 판 액터를 모두 불러온다.
실행 명령: python Tools/run_in_editor.py -c "import sys; sys.argv=['x','<report|apply>']; exec(open('C:/Project/TDGame/Tools/WorldGen/editor_convert_puddle_planes.py', encoding='utf-8').read()) (에디터 작업 폴더가 프로젝트 루트가 아니라 절대 경로)" (기본 report = 바꾸지 않고 개수만)
출력: Saved/WorldGen/puddle_plane_conversion_<모드>.json(판 수·지역별 수·만든 데칼·지운 판 패키지 목록·저장 전후 미저장 패키지) + apply 때 레벨·외부 액터 패키지 저장. 변환 직전 미저장 패키지가 하나라도 있으면 apply 를 거부한다(refused)
상태: GF-07(2026-11-20). 데칼 재질(MI_PuddlesDecal)을 붙인 평면 메시는 지형 곡면과 엇갈려 회색·흰 판이 드러나고 몇 cm 차이 경계가 Z-파이팅한다 — 같은 이름·태그·폴더·데이터 레이어의 지형 투사 데칼 액터로 바꾼다. apply 는 사용자 승인 뒤에만.
"""
import importlib
import json
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "WorldGen"))
data_layers = importlib.reload(importlib.import_module("editor_setup_data_layers"))
open_world = importlib.reload(importlib.import_module("editor_build_open_world"))

PUDDLE_MATERIAL_NAME = "MI_PuddlesDecal"
REPORT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "WorldGen", "puddle_plane_conversion_{mode}.json")


def is_pie_running():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None


def is_puddle_plane(actor):
    if not isinstance(actor, unreal.StaticMeshActor):
        return False
    component = actor.static_mesh_component
    return component is not None and any(material and PUDDLE_MATERIAL_NAME in material.get_name() for material in component.get_materials())


def load_puddle_descs():
    library = unreal.WorldPartitionBlueprintLibrary
    descs = library.get_actor_descs() or []
    guids = [desc.guid for desc in descs if "Puddle" in str(desc.label)]
    if guids:
        library.load_actors(guids)
    return len(guids)


def region_of(actor):
    names = data_layers.actor_layer_names(actor)
    for realm_id in data_layers.REALM_IDS:
        if data_layers.region_layer_name(realm_id) in names:
            return realm_id
    if data_layers.region_layer_name(data_layers.FIELD_REGION_ID) in names:
        return data_layers.FIELD_REGION_ID
    return None


def package_name_of(actor):
    return actor.get_outermost().get_name()


def dirty_package_names():
    utils = unreal.EditorLoadingAndSavingUtils
    packages = list(utils.get_dirty_map_packages() or []) + list(utils.get_dirty_content_packages() or [])
    return sorted(package.get_name() for package in packages)


def replace_with_decal(plane, region_id, packages):
    entry = open_world.plane_entry_from_actor(plane)
    removed_package = package_name_of(plane)
    with data_layers.region_context(region_id, include=region_id is not None):
        decal = open_world.spawn_plane_decal(entry, plane.static_mesh_component.get_materials()[0], plane.get_folder_path(), [str(tag) for tag in plane.tags])
    if decal is None:
        return False
    packages["added"].append(package_name_of(decal))
    unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(plane)
    packages["removed"].append(removed_package)
    return True


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "report"
    report = {"mode": mode}
    if is_pie_running():
        report["error"] = "PIE 가 돌고 있음 — 끄고 다시 실행"
        print("puddle conversion:", json.dumps(report, ensure_ascii=False))
        return
    world = data_layers.ensure_level_loaded()
    report["descs_loaded"] = load_puddle_descs()
    planes = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StaticMeshActor) if is_puddle_plane(actor)]
    regions = {}
    for plane in planes:
        key = region_of(plane) or "none"
        regions[key] = regions.get(key, 0) + 1
    report.update({"planes": len(planes), "by_region": regions})
    dirty_before = dirty_package_names()
    report["dirty_before"] = dirty_before
    if mode == "apply" and dirty_before:
        report["error"] = "저장 안 된 패키지가 있음 — 함께 저장될 수 있어 변환하지 않음"
        mode = "refused"
    if mode == "apply":
        packages = {"added": [], "removed": []}
        converted = sum(1 for plane in planes if replace_with_decal(plane, region_of(plane), packages))
        report["converted"] = converted
        report["dirty_to_save"] = dirty_package_names()
        report["saved"] = bool(unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True))
        report["packages"] = packages
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH.format(mode=mode), "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    print("puddle conversion:", json.dumps(report, ensure_ascii=False))


main()

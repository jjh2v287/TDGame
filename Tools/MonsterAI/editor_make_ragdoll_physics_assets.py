"""실행 환경: 언리얼 에디터 내장 Python(에디터가 TDGame을 연 상태, PIE 아님, M3-38 C++ 빌드 뒤 — 종 에셋에 ragdoll_physics_asset 필드 필요).
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_make_ragdoll_physics_assets.py (환경변수 TD_RAGDOLL_SPECIES="StoneGolem,Wukong"로 일부만, TD_RAGDOLL_REBUILD=1이면 기존 에셋을 지우지 않고 다시 생성)
출력: /Game/MonsterAI/Physics/PA_TD_<종>_Ragdoll 물리 에셋(FBX 가져오기 기본값으로 메시에서 생성)과 DA_TDMonster_<종>.RagdollPhysicsAsset 연결·저장, 줄마다 TD_RAGDOLL_PA 결과
상태: 현행(M3-38). 팩 원본 메시·물리 에셋은 바꾸지 않는다. 메시 폴더에 <메시>_PhysicsAsset 이름이 이미 있으면(Greystone) 메시를 임시 폴더에 복제해 그 옆에 생성한 뒤 옮기고 복제본을 지운다
"""
import json
import os

import unreal

PROJECT_DIRECTORY = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
TABLE_PATH = os.path.join(PROJECT_DIRECTORY, "Tools", "MonsterAI", "species_table.json")
PHYSICS_FOLDER = "/Game/MonsterAI/Physics"
TEMPORARY_FOLDER = PHYSICS_FOLDER + "/Temp"
SPECIES_FOLDER = "/Game/MonsterAI/Species"
DEFAULT_SPECIES = ["StoneGolem", "Wukong", "Greystone"]
ASSETS = unreal.EditorAssetLibrary


def content_file(package_path):
    return os.path.join(PROJECT_DIRECTORY, "Content", package_path[len("/Game/"):] + ".uasset")


def generate_beside(mesh):
    generated_path = f"{mesh.get_path_name().split('.')[0].rsplit('/', 1)[0]}/{mesh.get_name()}_PhysicsAsset"
    if os.path.exists(content_file(generated_path)):
        return None
    return unreal.SkeletalMeshEditorSubsystem.create_physics_asset(mesh, False, 0)


def generate_from_copy(mesh):
    copy_path = f"{TEMPORARY_FOLDER}/{mesh.get_name()}"
    if ASSETS.does_asset_exist(copy_path):
        ASSETS.delete_asset(copy_path)
    copy = ASSETS.duplicate_asset(mesh.get_path_name().split(".")[0], copy_path)
    physics_asset = unreal.SkeletalMeshEditorSubsystem.create_physics_asset(copy, False, 0)
    return physics_asset, copy_path


def make_physics_asset(species_name, mesh, rebuild):
    target_path = f"{PHYSICS_FOLDER}/PA_TD_{species_name}_Ragdoll"
    if ASSETS.does_asset_exist(target_path) and not rebuild:
        return unreal.load_asset(target_path), "reused"
    if ASSETS.does_asset_exist(target_path):
        ASSETS.delete_asset(target_path)
    physics_asset = generate_beside(mesh)
    copy_path = None
    if physics_asset is None:
        physics_asset, copy_path = generate_from_copy(mesh)
    if physics_asset is None:
        return None, "generation failed"
    if not ASSETS.rename_asset(physics_asset.get_path_name().split(".")[0], target_path):
        return None, "rename failed"
    if copy_path:
        ASSETS.delete_asset(copy_path)
    physics_asset = unreal.load_asset(target_path)
    try:
        physics_asset.set_editor_property("preview_skeletal_mesh", mesh)
    except Exception:
        pass
    ASSETS.save_loaded_asset(physics_asset, False)
    return physics_asset, "generated from copy" if copy_path else "generated"


def main():
    with open(TABLE_PATH, encoding="utf-8") as stream:
        table = {entry["name"]: entry for entry in json.load(stream)["species"]}
    requested = os.environ.get("TD_RAGDOLL_SPECIES")
    species_names = [name.strip() for name in requested.split(",")] if requested else DEFAULT_SPECIES
    rebuild = os.environ.get("TD_RAGDOLL_REBUILD") == "1"
    for species_name in species_names:
        entry = table.get(species_name)
        species = unreal.load_asset(f"{SPECIES_FOLDER}/DA_TDMonster_{species_name}") if entry else None
        mesh = unreal.load_asset(entry["mesh"]) if entry else None
        if not species or not mesh:
            print("TD_RAGDOLL_PA", species_name, "missing species or mesh")
            continue
        physics_asset, state = make_physics_asset(species_name, mesh, rebuild)
        if not physics_asset:
            print("TD_RAGDOLL_PA", species_name, state)
            continue
        species.set_editor_property("ragdoll_physics_asset", physics_asset)
        ASSETS.save_loaded_asset(species, False)
        body_count = len([candidate for candidate in unreal.ObjectIterator(unreal.BodySetup) if candidate.get_outer() == physics_asset])
        print("TD_RAGDOLL_PA", species_name, state, physics_asset.get_path_name(), "bodies", body_count)
    if ASSETS.does_directory_exist(TEMPORARY_FOLDER) and not ASSETS.list_assets(TEMPORARY_FOLDER):
        ASSETS.delete_directory(TEMPORARY_FOLDER)


main()

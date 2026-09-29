"""에디터 안에서 실행: M3-16(Mover·UAF 전환) 뒤 플레이어 블루프린트 메시의 AnimClass(ABP) 참조를 지우고 컴파일·저장한 다음(Mover 공유 설정은 저장 시 채워지므로 저장 → 컴파일 → 저장 순서), 참조가 남지 않은 옛 애니메이션 자산(ABP_Unarmed, AM_TD_Player_SwordAttack01)을 삭제한다. 삭제 전에 참조자를 검사해 남은 참조가 있으면 그 자산은 지우지 않고 보고한다.
실행: python Tools/run_in_editor.py Tools/Movement/editor_migrate_character_assets.py (PowerShell; Git Bash는 /Game/ 경로를 바꾼다)
출력: 로그 [TDMigrate] 줄 + Saved/AgentOps/migrate_character_assets.json
상태: 현행 (2026-09-30)
"""
import json

import unreal

PLAYER_BLUEPRINT = "/Game/Combat/Blueprints/BP_TDCombatCharacter"
OBSOLETE_ASSETS = [
    "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed",
    "/Game/Characters/Mannequins/Anims/Sword/AM_TD_Player_SwordAttack01",
]
REPORT = unreal.Paths.project_saved_dir() + "AgentOps/migrate_character_assets.json"


def log(message):
    print(f"[TDMigrate] {message}")


def describe_mesh(component):
    mesh = component.get_editor_property("skeletal_mesh_asset")
    anim_class = component.get_editor_property("anim_class")
    return {
        "name": component.get_name(),
        "mesh": mesh.get_path_name() if mesh else None,
        "anim_class": anim_class.get_path_name() if anim_class else None,
        "animation_mode": str(component.get_editor_property("animation_mode")),
    }


def clear_blueprint_anim_class(report):
    blueprint = unreal.load_asset(PLAYER_BLUEPRINT)
    if blueprint is None:
        raise RuntimeError(f"블루프린트 없음: {PLAYER_BLUEPRINT}")
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    changed = []
    for handle in subobjects.k2_gather_subobject_data_for_blueprint(blueprint):
        data = subobjects.k2_find_subobject_data_from_handle(handle)
        component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
        if not isinstance(component, unreal.SkeletalMeshComponent):
            continue
        before = describe_mesh(component)
        if component.get_editor_property("anim_class") is not None:
            component.modify()
            component.set_editor_property("anim_class", None)
            component.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_CUSTOM_MODE)
        changed.append({"before": before, "after": describe_mesh(component)})
    unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    default_object = unreal.get_default_object(blueprint.generated_class())
    report["blueprint"] = {"components": changed, "saved": saved, "cdo_mesh": describe_mesh(default_object.get_editor_property("mesh"))}
    log(f"blueprint components={changed} saved={saved}")


def delete_obsolete_assets(report):
    results = []
    for asset_path in OBSOLETE_ASSETS:
        if not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            results.append({"asset": asset_path, "result": "already missing"})
            continue
        referencers = [path for path in unreal.EditorAssetLibrary.find_package_referencers_for_asset(asset_path, True) if not path.startswith(asset_path)]
        if referencers:
            results.append({"asset": asset_path, "result": "kept", "referencers": referencers})
            log(f"kept {asset_path}: referenced by {referencers}")
            continue
        deleted = unreal.EditorAssetLibrary.delete_asset(asset_path)
        results.append({"asset": asset_path, "result": "deleted" if deleted else "delete failed"})
        log(f"delete {asset_path}: {deleted}")
    report["obsolete_assets"] = results


def main():
    report = {}
    clear_blueprint_anim_class(report)
    delete_obsolete_assets(report)
    with open(REPORT, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
    log(f"report {REPORT}")


main()

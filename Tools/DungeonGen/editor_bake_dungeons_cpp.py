"""에디터 안에서 실행: C++ `UTDWorldGenEditorLibrary`로 아틀라스 정의의 슬롯을 전부 생성·검증·베이크하고, C++ 월드 생성기·검증기를 현재 랜드스케이프로 시험한다.

실행: python Tools/run_in_editor.py Tools/DungeonGen/editor_bake_dungeons_cpp.py
출력: Saved/WorldGen/cpp_dungeon_<id>.md 리포트
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = ues.get_editor_world()
lib = unreal.TDWorldGenEditorLibrary
atlas = EAL.load_asset("/Game/World/Definitions/DA_TDDungeonAtlas_Main")
out_dir = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen")
os.makedirs(out_dir, exist_ok=True)


def unpack(result):
    if isinstance(result, (tuple, list)):
        return result
    return (result,)


for slot in atlas.get_editor_property("slots"):
    did = str(slot.get_editor_property("dungeon_id"))
    idx = slot.get_editor_property("slot_index")
    theme = slot.get_editor_property("theme") or EAL.load_asset("/Game/Dungeon/Themes/DA_TDTheme_Crypt")
    flow = slot.get_editor_property("flow_template") or EAL.load_asset("/Game/Dungeon/Flows/DA_TDFlow_KeyLock")
    res = unpack(lib.generate_and_bake_dungeon(world, theme, flow, slot.get_editor_property("size"), slot.get_editor_property("seed"), atlas, idx, did))
    ok = res[0]
    report = res[1] if len(res) > 1 else ""
    open(os.path.join(out_dir, f"cpp_dungeon_{did}.md"), "w", encoding="utf-8").write(str(report))
    print(f"slot {idx} {did}: baked={ok} report_len={len(str(report))}")

EAL.save_loaded_asset(atlas)

saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
print("saved:", saved)

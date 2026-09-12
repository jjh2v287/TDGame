"""에디터 안에서 실행: C++ 월드 그래프·도로 생성기와 검증기를 현재 레벨의 랜드스케이프 샘플러로 시험한다(베이크는 하지 않음).

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_cpp_world_layout.py
출력: Saved/WorldGen/cpp_world_layout.md
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
    return result if isinstance(result, (tuple, list)) else (result,)


landscape = None
for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy):
    if a.get_class().get_name() == "Landscape":
        landscape = a
region = EAL.load_asset("/Game/World/Definitions/DA_TDRegion_Forest")
world_def = EAL.load_asset("/Game/World/Definitions/DA_TDWorld_Main")
anchors = list(world_def.get_editor_property("hand_authored_anchors"))
bounds = world_def.get_editor_property("field_bounds_cm")
res = unpack(lib.generate_world_layout(world, region, anchors, bounds, 7, landscape, atlas))
print("world layout result:", [type(r).__name__ for r in res])
layout = next((r for r in res if isinstance(r, unreal.TDWorldLayout)), None)
if layout is not None:
    report = lib.validate_world_layout(layout, atlas)
    items = report.get_editor_property("items")
    errors = [i for i in items if str(i.get_editor_property("severity")).endswith("ERROR")]
    print("world layout: pois", len(layout.get_editor_property("pois")), "entrances", len(layout.get_editor_property("entrances")), "roads", len(layout.get_editor_property("roads")))
    print("world validation: score", report.get_editor_property("score"), "passed", report.get_editor_property("passed"), "errors", len(errors))
    lines = ["# C++ 월드 레이아웃 검증 (랜드스케이프 샘플러 사용, seed 7)", f"- POI {len(layout.get_editor_property('pois'))}, 입구 {len(layout.get_editor_property('entrances'))}, 도로 {len(layout.get_editor_property('roads'))}", f"- 점수 {report.get_editor_property('score'):.1f}, 통과 {report.get_editor_property('passed')}", ""]
    for i in items:
        lines.append(f"- [{str(i.get_editor_property('severity')).split('.')[-1]}] {i.get_editor_property('code')}: {i.get_editor_property('message')}")
    open(os.path.join(out_dir, "cpp_world_layout.md"), "w", encoding="utf-8").write("\n".join(lines))
    lib.log_report_to_message_log(report, "C++ world layout seed 7")

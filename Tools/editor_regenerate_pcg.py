"""에디터 안에서 실행: 현재 레벨의 모든 PCG 컴포넌트를 다시 생성한다(설계서 12장 "Regenerate PCG").

사용: python Tools/run_in_editor.py Tools/editor_regenerate_pcg.py
      라벨 필터: 환경 변수 대신 Saved/WorldGen/pcg_filter.txt 에 라벨 접두어를 한 줄 적으면 그 액터만 대상.
"""
import os

import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
prefix = None
filter_path = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen", "pcg_filter.txt")
if os.path.exists(filter_path):
    prefix = open(filter_path, encoding="utf-8").read().strip() or None
count = 0
for actor in eas.get_all_level_actors():
    if prefix and not actor.get_actor_label().startswith(prefix):
        continue
    for comp in actor.get_components_by_class(unreal.PCGComponent):
        comp.generate(True)
        count += 1
        print("regenerate:", actor.get_actor_label())
print("pcg components regenerated:", count)

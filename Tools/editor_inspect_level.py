"""에디터 안에서 실행: 현재(또는 지정) 레벨의 액터 요약을 JSON으로 저장한다. 에이전트가 작업 전에 상태를 파악할 때 쓴다.

사용: python Tools/run_in_editor.py Tools/editor_inspect_level.py
      (다른 레벨: 먼저 -c "import unreal; unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Level/LV-World')")
출력: Saved/Inspect/<레벨이름>.json — {level, actor_count, class_histogram, folders, actors:[{label, class, loc, folder, tags}] (최대 3000개)}
"""
import collections
import json
import os

import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()
actors = eas.get_all_level_actors()
hist = collections.Counter(a.get_class().get_name() for a in actors)
folders = collections.Counter(str(a.get_folder_path()) for a in actors)
rows = []
for a in actors[:3000]:
    loc = a.get_actor_location()
    rows.append({"label": a.get_actor_label(), "class": a.get_class().get_name(), "loc": [round(loc.x), round(loc.y), round(loc.z)], "folder": str(a.get_folder_path()), "tags": [str(t) for t in a.tags]})
out_dir = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "Inspect")
os.makedirs(out_dir, exist_ok=True)
path = os.path.join(out_dir, world.get_name() + ".json")
json.dump({"level": world.get_path_name(), "actor_count": len(actors), "class_histogram": dict(hist.most_common()), "folders": dict(folders.most_common(50)), "actors": rows}, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
print("level:", world.get_path_name(), "actors:", len(actors))
print("classes:", dict(hist.most_common(15)))
print("saved:", path)

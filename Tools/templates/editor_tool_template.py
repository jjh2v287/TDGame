"""(템플릿) 에디터 안에서 실행되는 도구 스크립트. 복사해서 Tools/<영역>/editor_<동사>_<대상>.py 로 저장한다.

실행: python Tools/run_in_editor.py Tools/<영역>/editor_<동사>_<대상>.py
규칙: 생성 액터 라벨 접두어 TDGen_ (또는 TDTest_ 는 정리 대상), 아웃라이너 폴더 지정, 저장은 마지막에 한 번.
      게임 로직은 C++로(AGENTS.md 7절); 이 스크립트는 에셋 생성·배치·설정 같은 구성 작업만 한다.
"""
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(msg):
    print(f"[TDTool {time.time() - t0:6.1f}s] {msg}")


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:
        log(f"property skip {name}: {str(e)[:80]}")
        return False


def main():
    world = ues.get_editor_world()
    log(f"level: {world.get_path_name()}")
    actor = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(0, 0, 100), unreal.Rotator(0, 0, 0))
    actor.set_actor_label("TDGen_Example")
    actor.set_folder_path("TDGen/Example")
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved: {ok}")


main()

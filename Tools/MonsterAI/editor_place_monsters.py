"""에디터 안에서 실행: LV-Cambat 레벨에 몬스터 무리 7개(ATDMonsterCharacter + 종 에셋)를 배치하고 저장한다. 이전에 이 도구가 만든 TDGen_Monster_ 액터는 지우고 다시 만든다(재실행 안전).
실행: python Tools/run_in_editor.py Tools/MonsterAI/editor_place_monsters.py (PowerShell; Git Bash는 /Game/ 경로를 바꾼다)
출력: /Game/Level/LV-Cambat 저장(무리 7개, 25마리), 로그에 배치 목록
상태: 현행
"""
import math
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
LEVEL_PATH = "/Game/Level/LV-Cambat"
SPECIES = "/Game/MonsterAI/Species/"
LABEL_PREFIX = "TDGen_Monster_"
FOLDER = "TDGen/Monsters/"
PLAYER_START = unreal.Vector(0.0, 0.0, 0.0)

GROUPS = [
    ("HyenaPack_East", (1900.0, 0.0), [("DA_TDMonster_Hyena", (0, 0)), ("DA_TDMonster_Hyena", (140, 160)), ("DA_TDMonster_Hyena", (140, -160)), ("DA_TDMonster_Hyena", (280, 0))]),
    ("BruteSquad_North", (0.0, 2300.0), [("DA_TDMonster_BookHeadBrute", (0, 0)), ("DA_TDMonster_SkeletonGuard", (-180, 160)), ("DA_TDMonster_SkeletonGuard", (180, 160))]),
    ("Casters_West", (-2300.0, -500.0), [("DA_TDMonster_BookHeadCaster", (0, 0)), ("DA_TDMonster_BookHeadCaster", (-120, 300)), ("DA_TDMonster_BookHeadBrute", (220, 120))]),
    ("MixedPack_South", (1400.0, -2600.0), [("DA_TDMonster_Hyena", (0, 0)), ("DA_TDMonster_Hyena", (160, 140)), ("DA_TDMonster_Hyena", (-160, 140)), ("DA_TDMonster_BookHeadCaster", (0, -350))]),
    ("GuardPost_NorthWest", (-1900.0, 2400.0), [("DA_TDMonster_SkeletonGuard", (0, 0)), ("DA_TDMonster_SkeletonGuard", (200, -120)), ("DA_TDMonster_BookHeadBrute", (-200, -150))]),
    ("GoblinBand_SouthWest", (-1700.0, -2700.0), [("DA_TDMonster_Goblin", (0, 0)), ("DA_TDMonster_Goblin", (130, 120)), ("DA_TDMonster_Goblin", (130, -120)), ("DA_TDMonster_Goblin", (-130, 120)), ("DA_TDMonster_Goblin", (-130, -120))]),
    ("GolemLair_NorthEast", (2500.0, 2400.0), [("DA_TDMonster_StoneGolem", (0, 0)), ("DA_TDMonster_Goblin", (-260, -200)), ("DA_TDMonster_Goblin", (-200, 260))]),
]


def log(msg):
    print(f"[TDPlaceMonsters {time.time() - t0:6.1f}s] {msg}")


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    if dirty:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(package.get_name() for package in dirty))
    monster_class = unreal.load_class(None, "/Script/TDGame.TDMonsterCharacter")
    if monster_class is None:
        raise RuntimeError("TDMonsterCharacter 클래스를 찾지 못했습니다(빌드 확인).")

    world = editor.get_editor_world()
    if not world.get_path_name().startswith(LEVEL_PATH):
        if not levels.load_level(LEVEL_PATH):
            raise RuntimeError(f"레벨 로드 실패: {LEVEL_PATH}")
    world = editor.get_editor_world()
    log(f"level: {world.get_path_name()}")

    removed = 0
    for actor in actors.get_all_level_actors():
        if actor.get_actor_label().startswith(LABEL_PREFIX):
            actors.destroy_actor(actor)
            removed += 1
    log(f"removed previous monsters: {removed}")

    placed = []
    for group_name, (center_x, center_y), members in GROUPS:
        for index, (species_name, (offset_x, offset_y)) in enumerate(members):
            species = EAL.load_asset(SPECIES + species_name)
            if species is None:
                raise RuntimeError(f"종 에셋 없음: {species_name} (editor_make_monster_species.py 먼저 실행)")
            half_height = species.get_editor_property("capsule_half_height")
            location = unreal.Vector(center_x + offset_x, center_y + offset_y, half_height + 3.0)
            yaw = math.degrees(math.atan2(PLAYER_START.y - location.y, PLAYER_START.x - location.x))
            actor = actors.spawn_actor_from_class(monster_class, location, unreal.Rotator(0.0, 0.0, yaw))
            if actor is None:
                raise RuntimeError(f"스폰 실패: {group_name}/{species_name}")
            actor.set_species(species)
            actor.set_actor_label(f"{LABEL_PREFIX}{group_name}_{index}")
            actor.set_folder_path(FOLDER + group_name)
            placed.append((actor.get_actor_label(), species_name, round(location.x), round(location.y)))

    for entry in placed:
        log(f"placed {entry}")
    if not levels.save_current_level():
        raise RuntimeError("레벨 저장 실패")
    log(f"saved {LEVEL_PATH} with {len(placed)} monsters")


main()

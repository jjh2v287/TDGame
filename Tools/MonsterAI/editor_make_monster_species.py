"""에디터 안에서 실행: 몬스터 종 에셋(UTDMonsterSpeciesAsset)과 몬스터 전용 애니메이션 복제본을 만들거나 갱신한다(재실행 안전).
실행: python Tools/run_in_editor.py Tools/MonsterAI/editor_make_monster_species.py (PowerShell; Git Bash는 /Game/ 경로를 바꾼다)
출력: /Game/MonsterAI/Species/DA_TDMonster_*, /Game/MonsterAI/Animations/**, 로그 요약
상태: 현행
"""
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

SPECIES_FOLDER = "/Game/MonsterAI/Species"
ANIM_FOLDER = "/Game/MonsterAI/Animations"
HYENA = "/Game/Fab/Hyena_A1/Hyenas_A1_AllMotionHyenas_A15_"
BOOKHEAD_ANIMS = "/Game/BookHeadMonster/Demo/Animations/"
MANNY_ATTACK = "/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01"
GUARD_ANIMS = "/Game/Skeleton_Guard/Demoscene_UE4/Animations/"
GOBLIN_ANIMS = "/Game/MonsterAI/Animations/Goblin/"
GOLEM_ANIMS = "/Game/MonsterAI/Animations/Golem/"
GOLEM_DEMO = "/Game/Stone_Golem/demo/animations/"

ROOT_LOCKED_COPIES = {
    f"{ANIM_FOLDER}/BookHead/AS_TD_BookHead_Attack01": MANNY_ATTACK,
}


def log(msg):
    print(f"[TDMonsterSpecies {time.time() - t0:6.1f}s] {msg}")


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def load(path):
    asset = EAL.load_asset(path)
    if asset is None:
        log(f"missing asset: {path}")
    return asset


def make_root_locked_copy(target_path, source_path):
    ensure_dir(target_path.rsplit("/", 1)[0])
    if not EAL.does_asset_exist(target_path):
        if EAL.duplicate_asset(source_path, target_path) is None:
            raise RuntimeError(f"duplicate failed: {source_path} -> {target_path}")
    anim = EAL.load_asset(target_path)
    anim.set_editor_property("force_root_lock", True)
    anim.set_editor_property("enable_root_motion", False)
    EAL.save_loaded_asset(anim)
    return anim


def clip(path, start=0.0, end=0.0, rate=1.0, impact=0.0, recovery=0.3):
    anim = load(path) if path else None
    value = unreal.TDMonsterAnimClip()
    value.set_editor_property("animation", anim)
    value.set_editor_property("start_seconds", start)
    value.set_editor_property("end_seconds", end)
    value.set_editor_property("play_rate", rate)
    value.set_editor_property("impact_seconds", impact)
    value.set_editor_property("recovery_seconds", recovery)
    return value


def skeleton_of(asset):
    try:
        return asset.get_editor_property("skeleton")
    except Exception:
        return None


def check_clip_skeleton(species_name, mesh, value):
    anim = value.get_editor_property("animation")
    if anim is None or mesh is None:
        return
    mesh_skeleton = mesh.get_editor_property("skeleton")
    anim_skeleton = skeleton_of(anim)
    if anim_skeleton == mesh_skeleton:
        return
    compatible = []
    try:
        compatible = [s.get_path_name() for s in mesh_skeleton.get_editor_property("compatible_skeletons")]
    except Exception:
        compatible = []
    anim_path = anim_skeleton.get_path_name() if anim_skeleton else "None"
    note = "compatible" if any(anim_path.startswith(p.split(".")[0]) for p in compatible) else "NOT compatible"
    log(f"{species_name}: {anim.get_name()} uses skeleton {anim_path} ({note} with mesh skeleton)")


def species(name, definition_id, mesh_path, scale, yaw, radius, half_height, idle, walk, run, walk_speed, run_speed, death, abilities, corpse_seconds=8.0, height_offset=0.0):
    ensure_dir(SPECIES_FOLDER)
    path = f"{SPECIES_FOLDER}/{name}"
    asset = EAL.load_asset(path)
    if asset is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.TDMonsterSpeciesAsset)
        asset = TOOLS.create_asset(name, SPECIES_FOLDER, unreal.TDMonsterSpeciesAsset, factory)
    mesh = load(mesh_path)
    asset.set_editor_property("definition_id", definition_id)
    asset.set_editor_property("mesh", mesh)
    asset.set_editor_property("mesh_scale", scale)
    asset.set_editor_property("mesh_yaw", yaw)
    asset.set_editor_property("mesh_height_offset", height_offset)
    asset.set_editor_property("capsule_radius", radius)
    asset.set_editor_property("capsule_half_height", half_height)
    asset.set_editor_property("idle", idle)
    asset.set_editor_property("walk", walk)
    asset.set_editor_property("run", run)
    asset.set_editor_property("walk_clip_speed", walk_speed)
    asset.set_editor_property("run_clip_speed", run_speed)
    asset.set_editor_property("death", death)
    asset.set_editor_property("ability_clips", abilities)
    asset.set_editor_property("use_ragdoll_without_death_clip", True)
    asset.set_editor_property("corpse_life_seconds", corpse_seconds)
    for value in [idle, walk, run, death] + list(abilities.values()):
        check_clip_skeleton(name, mesh, value)
    EAL.save_loaded_asset(asset)
    log(f"saved {path} -> definition {definition_id}")
    return asset


def main():
    for target, source in ROOT_LOCKED_COPIES.items():
        make_root_locked_copy(target, source)
        log(f"root-locked copy {target}")
    bookhead_attack = f"{ANIM_FOLDER}/BookHead/AS_TD_BookHead_Attack01"

    hyena_bite = clip(HYENA + "Atk", 0.0, 0.6, 1.0, 0.3, 0.25)
    species("DA_TDMonster_Hyena", "Hyena", "/Game/Fab/Hyena_A1/Hyenas_A1_AllMotion", 0.65, -90.0, 45.0, 48.0,
            clip(HYENA + "Idle", 0.0, 2.5), clip(HYENA + "Walk", 0.0, 1.333), clip(HYENA + "Run", 0.0, 0.5), 70.0, 925.0,
            clip(HYENA + "Die", 0.0, 1.667), {"Bite": hyena_bite}, corpse_seconds=6.0)

    brute_attack = clip(bookhead_attack, 0.0, 1.0, 1.0, 0.45, 0.35)
    species("DA_TDMonster_BookHeadBrute", "BookHead_Brute", "/Game/BookHeadMonster/Meshes/BookHeadMonster", 1.0, -90.0, 40.0, 90.0,
            clip(BOOKHEAD_ANIMS + "MM_Idle"), clip(BOOKHEAD_ANIMS + "MM_Walk_Fwd"), clip(BOOKHEAD_ANIMS + "MM_Run_Fwd"), 230.0, 510.0,
            clip(None), {"Smash": brute_attack, "Swipe": brute_attack})

    caster_attack = clip(bookhead_attack, 0.0, 1.0, 1.0, 0.45, 0.4)
    species("DA_TDMonster_BookHeadCaster", "BookHead_Caster", "/Game/BookHeadMonster/Meshes/BookHeadMonster", 0.9, -90.0, 36.0, 81.0,
            clip(BOOKHEAD_ANIMS + "MM_Idle"), clip(BOOKHEAD_ANIMS + "MM_Walk_Fwd"), clip(BOOKHEAD_ANIMS + "MM_Run_Fwd"), 230.0, 510.0,
            clip(None), {"Bolt": caster_attack, "Burst": caster_attack})

    species("DA_TDMonster_SkeletonGuard", "Goblin_Melee", "/Game/Skeleton_Guard/Mesh_UE4/Full/SKM_Skeleton_Guard", 1.0, -90.0, 40.0, 92.0,
            clip(GUARD_ANIMS + "ThirdPersonIdle"), clip(GUARD_ANIMS + "ThirdPersonWalk"), clip(GUARD_ANIMS + "ThirdPersonRun"), 150.0, 375.0,
            clip(None), {})

    goblin_slash = clip(GOBLIN_ANIMS + "AS_TD_Goblin_Slash", 0.0, 1.3, 1.0, 0.52, 0.3)
    species("DA_TDMonster_Goblin", "Goblin_Melee", "/Game/Fab/Stylized_Goblin_Minion_-_Free/SK_Goblin", 1.06, -90.0, 28.0, 60.0,
            clip(GOBLIN_ANIMS + "AS_TD_Goblin_Idle"), clip(GOBLIN_ANIMS + "AS_TD_Goblin_Walk"), clip(GOBLIN_ANIMS + "AS_TD_Goblin_Run"), 175.0, 355.0,
            clip(None), {"Slash": goblin_slash}, corpse_seconds=6.0)

    golem_attack = clip(GOLEM_ANIMS + "AS_TD_Golem_Attack01", 0.0, 1.0, 1.0, 0.39, 0.45)
    species("DA_TDMonster_StoneGolem", "StoneGolem", "/Game/Stone_Golem/mesh/SKM_Stone_Golem", 0.64, -90.0, 58.0, 105.0,
            clip(GOLEM_DEMO + "ThirdPersonIdle"), clip(GOLEM_DEMO + "ThirdPersonWalk"), clip(GOLEM_DEMO + "ThirdPersonRun"), 325.0, 950.0,
            clip(None), {"Smash": golem_attack, "Stomp": golem_attack}, corpse_seconds=10.0, height_offset=6.0)
    log("done")


main()

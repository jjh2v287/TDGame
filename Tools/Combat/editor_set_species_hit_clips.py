"""에디터 안에서 실행: 몬스터 종 자산(DA_TDMonster_*)의 비어 있는 Hit 클립에 M3-27 피격 애니를 연결한다(이미 값이 있으면 건너뛴다, 클립 자산이 없으면 건너뛴다).
실행: python Tools/run_in_editor.py Tools/Combat/editor_set_species_hit_clips.py (PowerShell, L-repo-01)
출력: 바뀐 종 자산 저장, 로그의 [TDTool] species 줄(종·클립·길이)
상태: 현행 (2026-10-18, M3-27)
"""
import unreal

EAL = unreal.EditorAssetLibrary
SPECIES_FOLDER = "/Game/MonsterAI/Species"
HIT_PLAY_RATE = 1.25
HIT_CLIPS = {
    "DA_TDMonster_BookHeadBrute": "/Game/MonsterAI/Animations/BookHead/AS_TD_BookHead_HitReact",
    "DA_TDMonster_BookHeadCaster": "/Game/MonsterAI/Animations/BookHead/AS_TD_BookHead_HitReact",
    "DA_TDMonster_Goblin": "/Game/MonsterAI/Animations/Goblin/AS_TD_Goblin_HitReact",
    "DA_TDMonster_StoneGolem": "/Game/MonsterAI/Animations/Golem/AS_TD_Golem_HitReact",
    "DA_TDMonster_SkeletonGuard": "/Game/MonsterAI/Animations/Guard/AS_TD_Guard_HitReact",
    "DA_TDMonster_Hyena": "/Game/MonsterAI/Animations/Hyena/AS_TD_Hyena_Hit",
}


def log(message):
    unreal.log(f"[TDTool] {message}")


def assign_hit_clip(species_name, clip_path):
    species_path = f"{SPECIES_FOLDER}/{species_name}"
    species = EAL.load_asset(species_path)
    if species is None:
        log(f"species skip (missing) {species_path}")
        return
    current = species.get_editor_property("hit")
    if current.get_editor_property("animation") is not None:
        log(f"species skip (hit already set) {species_name} -> {current.get_editor_property('animation').get_path_name()}")
        return
    animation = EAL.load_asset(clip_path) if EAL.does_asset_exist(clip_path) else None
    if not isinstance(animation, unreal.AnimSequence):
        log(f"species skip (no clip) {species_name} {clip_path}")
        return
    skeleton = species.get_editor_property("mesh").get_editor_property("skeleton")
    if animation.get_editor_property("skeleton") != skeleton:
        log(f"species skip (skeleton mismatch) {species_name} clip={animation.get_editor_property('skeleton').get_path_name()} mesh={skeleton.get_path_name()}")
        return
    clip = unreal.TDMonsterAnimClip()
    clip.set_editor_property("animation", animation)
    clip.set_editor_property("start_seconds", 0.0)
    clip.set_editor_property("end_seconds", 0.0)
    clip.set_editor_property("play_rate", HIT_PLAY_RATE)
    species.set_editor_property("hit", clip)
    EAL.save_loaded_asset(species, only_if_is_dirty=False)
    log(f"species {species_name} hit={clip_path} length={animation.get_play_length():.3f} rate={HIT_PLAY_RATE}")


for name, path in HIT_CLIPS.items():
    assign_hit_clip(name, path)
log("done species hit clips")

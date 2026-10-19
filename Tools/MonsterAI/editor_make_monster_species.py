"""에디터 안에서 실행: Tools/MonsterAI/species_table.json 의 종마다 몬스터 종 에셋(UTDMonsterSpeciesAsset)과 루트 고정 애니메이션 복제본을 만들거나 갱신한다(재실행 안전).
실행: M3-40 C++ 빌드·에디터 재시작 뒤 python Tools/run_in_editor.py Tools/MonsterAI/editor_make_monster_species.py (PowerShell; Git Bash는 /Game/ 경로를 바꾼다. 종 에셋에 새 필드가 없으면 아무것도 바꾸지 않고 첫 줄에서 오류로 멈춘다). 일부 종만: -c "TD_ONLY_SPECIES=['Goblin']; exec(open('C:/Project/TDGame/Tools/MonsterAI/editor_make_monster_species.py', encoding='utf-8').read())"
출력: /Game/MonsterAI/Species/DA_TDMonster_*(사망 표현 play_death_clip_before_ragdoll·has_horizontal_body_plan·corpse_rest_height_ratio(표 키 생략이면 0, 몸 구조 기본 누운 높이) 기록, 기존 에셋은 불러와 표의 속성만 바꾸므로 editor_make_ragdoll_physics_assets.py 가 연결한 ragdoll_physics_asset 은 그대로 두고 저장 줄에 표시. 선택 키 phases·takeoff/fly_idle/land/fly_hit·channel/stun은 해당 C++ 필드가 있을 때만 기록), 표의 root_locked_copies 복제본, 종마다 스켈레톤 호환·능력 키 대조·없는 에셋 로그와 마지막 요약 한 줄
상태: 현행(M3-40, 종 목록은 표가 정본. 옛 editor_set_species_hit_clips.py 는 이 도구의 hit 항목으로 흡수)
"""
import importlib
import os
import sys
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
TABLE_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "MonsterAI")
if TABLE_TOOLS_DIR not in sys.path:
    sys.path.insert(0, TABLE_TOOLS_DIR)
import species_table

importlib.reload(species_table)

TABLE = species_table.load_table()
SPECIES_FOLDER = TABLE["paths"]["species_folder"]
DEFINITIONS_DIR = os.path.join(PROJECT_DIR, TABLE["paths"]["definitions_dir"])
ONLY_SPECIES = globals().get("TD_ONLY_SPECIES")
REQUIRED_SPECIES_PROPERTIES = ("play_death_clip_before_ragdoll", "has_horizontal_body_plan", "corpse_rest_height_ratio", "ragdoll_physics_asset")
warning_count = 0


def log(msg):
    print(f"[TDMonsterSpecies {time.time() - t0:6.1f}s] {msg}")


def warn(msg):
    global warning_count
    warning_count += 1
    log(msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def load(path, missing):
    asset = EAL.load_asset(path)
    if asset is None:
        missing.append(path)
        warn(f"missing asset: {path}")
    return asset


def make_root_locked_copy(target_path, source_path):
    ensure_dir(target_path.rsplit("/", 1)[0])
    if not EAL.does_asset_exist(target_path):
        if not EAL.does_asset_exist(source_path):
            warn(f"missing asset: {source_path}")
            return None
        if EAL.duplicate_asset(source_path, target_path) is None:
            raise RuntimeError(f"duplicate failed: {source_path} -> {target_path}")
    anim = EAL.load_asset(target_path)
    anim.set_editor_property("force_root_lock", True)
    anim.set_editor_property("enable_root_motion", False)
    EAL.save_loaded_asset(anim)
    return anim


def make_clip(raw_clip, missing):
    values = species_table.fill_clip_defaults(raw_clip)
    clip = unreal.TDMonsterAnimClip()
    clip.set_editor_property("animation", load(values["anim"], missing) if values["anim"] else None)
    clip.set_editor_property("start_seconds", values["start"])
    clip.set_editor_property("end_seconds", values["end"])
    clip.set_editor_property("play_rate", values["rate"])
    clip.set_editor_property("impact_seconds", values["impact"])
    clip.set_editor_property("recovery_seconds", values["recovery"])
    return clip


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
    is_compatible = any(anim_path.startswith(p.split(".")[0]) for p in compatible)
    note = "compatible" if is_compatible else "NOT compatible"
    report = log if is_compatible else warn
    report(f"{species_name}: {anim.get_name()} uses skeleton {anim_path} ({note} with mesh skeleton)")


def check_ability_keys(entry):
    definition_keys = species_table.resolve_definition_abilities(DEFINITIONS_DIR, entry["definition"])
    if definition_keys is None:
        warn(f"missing definition: {entry['definition']} ({entry['name']})")
        return
    clip_only, definition_only = species_table.compare_ability_keys(entry, definition_keys)
    if clip_only or definition_only:
        warn(species_table.format_ability_mismatch(entry["name"], clip_only, definition_only))


def find_missing_species_properties():
    species_defaults = unreal.get_default_object(unreal.TDMonsterSpeciesAsset)
    missing_properties = []
    for property_name in REQUIRED_SPECIES_PROPERTIES:
        try:
            species_defaults.get_editor_property(property_name)
        except Exception:
            missing_properties.append(property_name)
    return missing_properties


def describe_invalid_death_keys(entry):
    if not isinstance(entry["death_clip_before_ragdoll"], bool):
        return f"death_clip_before_ragdoll {entry['death_clip_before_ragdoll']!r} is not true or false"
    if entry["body_plan"] not in species_table.BODY_PLANS:
        return f"body_plan {entry['body_plan']!r} is not one of {list(species_table.BODY_PLANS)}"
    if not species_table.is_corpse_rest_height_ratio(entry["corpse_rest_height_ratio"]):
        return f"corpse_rest_height_ratio {entry['corpse_rest_height_ratio']!r} is not a number from 0 to 1"
    return None


def describe_ragdoll_physics_asset(asset):
    physics_asset = asset.get_editor_property("ragdoll_physics_asset")
    return physics_asset.get_path_name() if physics_asset else "None"


def load_or_create_species_asset(asset_name):
    path = f"{SPECIES_FOLDER}/{asset_name}"
    asset = EAL.load_asset(path)
    if asset is not None:
        return asset
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDMonsterSpeciesAsset)
    return TOOLS.create_asset(asset_name, SPECIES_FOLDER, unreal.TDMonsterSpeciesAsset, factory)


def set_follower_meshes(asset, species_name, follower_meshes):
    if not follower_meshes:
        return
    try:
        meshes = unreal.Array(unreal.SkeletalMesh)
        for mesh in follower_meshes:
            meshes.append(mesh)
        asset.set_editor_property("follower_meshes", meshes)
    except Exception as error:
        warn(f"follower field unavailable: {species_name}: {error}")


def set_extra_clips(asset, species_name, extra_clips):
    for key, clip in extra_clips.items():
        try:
            asset.set_editor_property(species_table.EXTRA_CLIP_PROPERTIES[key], clip)
        except Exception as error:
            warn(f"clip field unavailable: {species_name}.{key}: {error}")


def make_phase_presentation(phase_clips, follower_meshes):
    presentation = unreal.TDMonsterPhasePresentation()
    for key, clip in phase_clips.items():
        presentation.set_editor_property(key, clip)
    meshes = unreal.Array(unreal.SkeletalMesh)
    for mesh in follower_meshes:
        meshes.append(mesh)
    presentation.set_editor_property("follower_meshes", meshes)
    return presentation


def set_phase_presentations(asset, species_name, phases):
    if not phases:
        return
    try:
        presentations = {unreal.Name(phase_id): make_phase_presentation(clips, followers) for phase_id, (clips, followers) in phases.items()}
        asset.set_editor_property("phase_presentations", presentations)
    except Exception as error:
        warn(f"phase field unavailable: {species_name}: {error}")


def enable_idle_loop(idle_clip):
    animation = idle_clip.get_editor_property("animation")
    if not isinstance(animation, unreal.AnimSequence) or animation.get_editor_property("loop"):
        return
    animation.set_editor_property("loop", True)
    EAL.save_loaded_asset(animation, False)
    log(f"idle loop enabled: {animation.get_path_name()}")


def build_species(raw_entry):
    entry = species_table.fill_species_defaults(raw_entry)
    name = entry["name"]
    invalid_death_keys = describe_invalid_death_keys(entry)
    if invalid_death_keys:
        warn(f"skip {name}: {invalid_death_keys}, not saved (run Tools/MonsterAI/check_species_table.py)")
        return False
    check_ability_keys(entry)
    missing = []
    mesh = load(entry["mesh"], missing)
    follower_meshes = [load(path, missing) for path in entry["followers"]]
    clips = {key: make_clip(entry[key], missing) for key in species_table.LOCOMOTION_CLIP_KEYS + species_table.OPTIONAL_CLIP_KEYS}
    ability_clips = {ability: make_clip(raw, missing) for ability, raw in entry["abilities"].items()}
    extra_clips = {key: make_clip(raw, missing) for key, raw in entry["extra_clips"].items()}
    phases = {phase_id: ({key: make_clip(phase[key], missing) for key in species_table.PHASE_CLIP_KEYS}, [load(path, missing) for path in phase["followers"]])
              for phase_id, phase in entry["phases"].items()}
    if missing:
        log(f"skip {name}: {len(missing)} missing asset(s), not saved")
        return False
    ensure_dir(SPECIES_FOLDER)
    asset_name = species_table.species_asset_name(name)
    asset = load_or_create_species_asset(asset_name)
    asset.set_editor_property("definition_id", entry["definition"])
    asset.set_editor_property("mesh", mesh)
    asset.set_editor_property("mesh_scale", entry["scale"])
    asset.set_editor_property("mesh_yaw", entry["yaw"])
    asset.set_editor_property("mesh_height_offset", entry["height_offset"])
    asset.set_editor_property("capsule_radius", entry["radius"])
    asset.set_editor_property("capsule_half_height", entry["half_height"])
    set_follower_meshes(asset, name, follower_meshes)
    asset.set_editor_property("idle", clips["idle"])
    asset.set_editor_property("walk", clips["walk"])
    asset.set_editor_property("run", clips["run"])
    asset.set_editor_property("walk_clip_speed", entry["walk_clip_speed"])
    asset.set_editor_property("run_clip_speed", entry["run_clip_speed"])
    asset.set_editor_property("hit", clips["hit"])
    asset.set_editor_property("death", clips["death"])
    asset.set_editor_property("ability_clips", ability_clips)
    set_extra_clips(asset, name, extra_clips)
    set_phase_presentations(asset, name, phases)
    asset.set_editor_property("use_ragdoll_without_death_clip", entry["ragdoll_without_death_clip"])
    asset.set_editor_property("ragdoll_after_death_clip", entry["ragdoll_after_death"])
    asset.set_editor_property("play_death_clip_before_ragdoll", entry["death_clip_before_ragdoll"])
    asset.set_editor_property("has_horizontal_body_plan", entry["body_plan"] == species_table.HORIZONTAL_BODY_PLAN)
    asset.set_editor_property("corpse_rest_height_ratio", float(entry["corpse_rest_height_ratio"]))
    asset.set_editor_property("corpse_sink_seconds", entry["corpse_sink_seconds"])
    asset.set_editor_property("corpse_life_seconds", entry["corpse_seconds"])
    phase_clip_values = [clip for phase_clips, _ in phases.values() for clip in phase_clips.values()]
    for value in list(clips.values()) + list(ability_clips.values()) + list(extra_clips.values()) + phase_clip_values:
        check_clip_skeleton(name, mesh, value)
    enable_idle_loop(clips["idle"])
    EAL.save_loaded_asset(asset)
    log(f"saved {SPECIES_FOLDER}/{asset_name} -> definition {entry['definition']}, ragdoll physics asset kept: {describe_ragdoll_physics_asset(asset)}")
    return True


def main():
    missing_properties = find_missing_species_properties()
    if missing_properties:
        raise RuntimeError(f"TDMonsterSpeciesAsset has no property {missing_properties}: build the M3-40 C++ and restart the editor (python Tools/ue_editor.py restart) before running this tool; nothing was changed")
    for copy in TABLE["root_locked_copies"]:
        if make_root_locked_copy(copy["target"], copy["source"]):
            log(f"root-locked copy {copy['target']}")
    known_names = {entry["name"] for entry in TABLE["species"]}
    unknown_names = sorted(set(ONLY_SPECIES or []) - known_names)
    if unknown_names:
        warn(f"TD_ONLY_SPECIES names not in table: {unknown_names}")
    saved_count = 0
    skipped_count = 0
    for entry in species_table.iter_species(TABLE, ONLY_SPECIES):
        if build_species(entry):
            saved_count += 1
            continue
        skipped_count += 1
    log(f"done: {saved_count} species saved, {skipped_count} skipped, {warning_count} warnings")


main()

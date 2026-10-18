"""실행 환경: 일반 파이썬 3 표준 라이브러리만(에디터 불필요). 프로젝트 루트의 Content 디스크 파일과 정의 JSON 을 읽기만 한다
실행 명령: python Tools/MonsterAI/check_species_table.py [--root <프로젝트 루트>] [--table <species_table.json 경로>]
출력: 문제마다 한 줄(ERROR 또는 WARN)과 마지막 요약 한 줄, ERROR 가 하나라도 있으면 종료 코드 1
상태: 현행(M3-33)
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import species_table

DEFAULT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z0-9_]+$")
SPECIES_REQUIRED_KEYS = ("name", "definition", "mesh", "scale", "yaw", "radius", "half_height", "walk_clip_speed", "run_clip_speed", "idle", "walk", "run")
SPECIES_OPTIONAL_KEYS = ("followers", "height_offset", "hit", "death", "abilities", "corpse_seconds", "ragdoll_after_death", "ragdoll_without_death_clip", "corpse_sink_seconds", "phases") + tuple(species_table.EXTRA_CLIP_PROPERTIES)
REQUIRED_PATH_KEYS = ("species_folder", "animation_folder", "definitions_dir")
CLIP_KEYS = set(species_table.CLIP_DEFAULTS)
RETARGET_CLIP_KINDS = ("idle", "locomotion", "attack", "hit", "death")
BONE_ROLES = ("root", "pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r", "ball_l", "ball_r")
LOOP_ROLES = species_table.LOCOMOTION_CLIP_KEYS


class Findings:
    def __init__(self):
        self.lines = []
        self.error_count = 0
        self.warning_count = 0

    def error(self, where, message):
        self.error_count += 1
        self.lines.append(f"ERROR {where}: {message}")

    def warn(self, where, message):
        self.warning_count += 1
        self.lines.append(f"WARN {where}: {message}")


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def check_number(findings, where, key, value, minimum, is_exclusive=False):
    if not is_number(value):
        findings.error(where, f"{key} must be a number (got {value!r})")
        return
    if value < minimum or (is_exclusive and value == minimum):
        findings.error(where, f"{key} must be {'>' if is_exclusive else '>='} {minimum} (got {value})")


def check_game_path_value(findings, where, key, value):
    if not isinstance(value, str) or not value.startswith("/Game/"):
        findings.error(where, f"{key} must be a /Game/ path string (got {value!r})")
        return False
    return True


def check_clip(findings, where, raw_clip, game_paths, is_empty_allowed):
    if not isinstance(raw_clip, dict):
        findings.error(where, "clip must be an object")
        return
    for key in sorted(set(raw_clip) - CLIP_KEYS):
        findings.warn(where, f"unknown clip key '{key}'")
    if "anim" not in raw_clip:
        findings.error(where, "missing key 'anim' (use null for an empty clip)")
        return
    clip = species_table.fill_clip_defaults(raw_clip)
    if clip["anim"] is None:
        if not is_empty_allowed:
            findings.warn(where, "empty clip (anim is null)")
    elif check_game_path_value(findings, where, "anim", clip["anim"]):
        game_paths.append((f"{where}.anim", clip["anim"]))
    for key in ("start", "end", "impact", "recovery"):
        check_number(findings, where, key, clip[key], 0.0)
    check_number(findings, where, "rate", clip["rate"], 0.0, True)
    if is_number(clip["end"]) and is_number(clip["start"]) and clip["end"] > 0 and clip["end"] <= clip["start"]:
        findings.error(where, f"end must be greater than start (start {clip['start']}, end {clip['end']})")


def check_species_entry(findings, entry, definitions_dir, game_paths):
    name = entry.get("name") if isinstance(entry, dict) else None
    where = f"species[{name}]"
    if not isinstance(entry, dict):
        findings.error("species", "entry must be an object")
        return
    for key in SPECIES_REQUIRED_KEYS:
        if key not in entry:
            findings.error(where, f"missing key '{key}'")
    for key in sorted(set(entry) - set(SPECIES_REQUIRED_KEYS) - set(SPECIES_OPTIONAL_KEYS)):
        findings.warn(where, f"unknown key '{key}'")
    if not isinstance(name, str) or not IDENTIFIER_PATTERN.match(name):
        findings.error(where, f"name must match [A-Za-z0-9_]+ (got {name!r})")
    if not all(key in entry for key in SPECIES_REQUIRED_KEYS):
        return
    filled = {**species_table.SPECIES_DEFAULTS, **entry}
    check_number(findings, where, "scale", filled["scale"], 0.0, True)
    check_number(findings, where, "yaw", filled["yaw"], float("-inf"))
    check_number(findings, where, "height_offset", filled["height_offset"], float("-inf"))
    check_number(findings, where, "radius", filled["radius"], 5.0)
    check_number(findings, where, "half_height", filled["half_height"], 10.0)
    check_number(findings, where, "walk_clip_speed", filled["walk_clip_speed"], 1.0)
    check_number(findings, where, "run_clip_speed", filled["run_clip_speed"], 1.0)
    check_number(findings, where, "corpse_seconds", filled["corpse_seconds"], 0.0)
    check_number(findings, where, "corpse_sink_seconds", filled["corpse_sink_seconds"], 0.0)
    for key in ("ragdoll_after_death", "ragdoll_without_death_clip"):
        if not isinstance(filled[key], bool):
            findings.error(where, f"{key} must be true or false (got {filled[key]!r})")
    if check_game_path_value(findings, where, "mesh", filled["mesh"]):
        game_paths.append((f"{where}.mesh", filled["mesh"]))
    if not isinstance(filled["followers"], list):
        findings.error(where, "followers must be a list")
    else:
        for index, follower in enumerate(filled["followers"]):
            if check_game_path_value(findings, where, f"followers[{index}]", follower):
                game_paths.append((f"{where}.followers[{index}]", follower))
    for key in species_table.LOCOMOTION_CLIP_KEYS:
        check_clip(findings, f"{where}.{key}", entry[key], game_paths, False)
    for key in species_table.OPTIONAL_CLIP_KEYS:
        if key in entry:
            check_clip(findings, f"{where}.{key}", entry[key], game_paths, True)
    for key in species_table.EXTRA_CLIP_PROPERTIES:
        if key in entry:
            check_clip(findings, f"{where}.{key}", entry[key], game_paths, False)
    check_phases(findings, where, entry, definitions_dir, game_paths)
    abilities = entry.get("abilities", {})
    if not isinstance(abilities, dict):
        findings.error(where, "abilities must be an object")
    else:
        for ability, raw_clip in abilities.items():
            check_clip(findings, f"{where}.abilities.{ability}", raw_clip, game_paths, False)
    check_definition(findings, where, entry, definitions_dir)


def check_phases(findings, where, entry, definitions_dir, game_paths):
    phases = entry.get("phases", {})
    if not isinstance(phases, dict):
        findings.error(where, "phases must be an object keyed by definition phase id")
        return
    known_ids = species_table.resolve_definition_phase_ids(definitions_dir, entry.get("definition")) if phases else []
    for phase_id, phase in phases.items():
        phase_where = f"{where}.phases.{phase_id}"
        if not isinstance(phase, dict):
            findings.error(phase_where, "phase must be an object")
            continue
        if phase_id not in known_ids:
            findings.warn(phase_where, f"phase id is not in definition phases {known_ids}")
        for key in sorted(set(phase) - set(species_table.PHASE_CLIP_KEYS) - {"followers"}):
            findings.warn(phase_where, f"unknown phase key '{key}'")
        for key in species_table.PHASE_CLIP_KEYS:
            if key in phase:
                check_clip(findings, f"{phase_where}.{key}", phase[key], game_paths, True)
        followers = phase.get("followers", [])
        if not isinstance(followers, list):
            findings.error(phase_where, "followers must be a list")
            continue
        for index, follower in enumerate(followers):
            if check_game_path_value(findings, phase_where, f"followers[{index}]", follower):
                game_paths.append((f"{phase_where}.followers[{index}]", follower))


def check_definition(findings, where, entry, definitions_dir):
    definition = entry["definition"]
    if not isinstance(definition, str) or not os.path.isfile(os.path.join(definitions_dir, f"{definition}.json")):
        findings.error(where, f"definition '{definition}' not found in {definitions_dir}")
        return
    if not isinstance(entry.get("abilities", {}), dict):
        return
    definition_keys = species_table.resolve_definition_abilities(definitions_dir, definition)
    clip_only, definition_only = species_table.compare_ability_keys(entry, definition_keys)
    if clip_only or definition_only:
        findings.warn(where, species_table.format_ability_mismatch(entry["name"], clip_only, definition_only))


def check_retarget(findings, retarget, game_paths):
    sources = retarget.get("sources", {})
    clips = retarget.get("clips", {})
    targets = retarget.get("targets", {})
    for key, source in sources.items():
        where = f"retarget.sources[{key}]"
        for field in ("mesh", "rig"):
            if field not in source:
                findings.error(where, f"missing key '{field}'")
        if "mesh" in source and check_game_path_value(findings, where, "mesh", source["mesh"]):
            game_paths.append((f"{where}.mesh", source["mesh"]))
    for key, clip in clips.items():
        where = f"retarget.clips[{key}]"
        if clip.get("source") not in sources:
            findings.error(where, f"source '{clip.get('source')}' not in retarget.sources")
        if clip.get("kind") not in RETARGET_CLIP_KINDS:
            findings.error(where, f"kind must be one of {list(RETARGET_CLIP_KINDS)} (got {clip.get('kind')!r})")
        if check_game_path_value(findings, where, "anim", clip.get("anim")):
            game_paths.append((f"{where}.anim", clip["anim"]))
    for key, target in targets.items():
        where = f"retarget.targets[{key}]"
        if not IDENTIFIER_PATTERN.match(key):
            findings.error(where, "target key must match [A-Za-z0-9_]+")
        if target.get("source") not in sources:
            findings.error(where, f"source '{target.get('source')}' not in retarget.sources")
        if check_game_path_value(findings, where, "mesh", target.get("mesh")):
            game_paths.append((f"{where}.mesh", target["mesh"]))
        target_clips = target.get("clips", [])
        if not target_clips:
            findings.error(where, "clips must be a non-empty list")
        for clip_key in target_clips:
            if clip_key not in clips:
                findings.error(where, f"clip '{clip_key}' not in retarget.clips")
            elif clips[clip_key].get("source") != target.get("source"):
                findings.error(where, f"clip '{clip_key}' source '{clips[clip_key].get('source')}' differs from target source '{target.get('source')}'")
        for role in target.get("bone_names", {}):
            if role not in BONE_ROLES:
                findings.warn(where, f"bone_names role '{role}' is not one of {list(BONE_ROLES)}")


def check_loop_entries(findings, table, species_by_name, game_paths):
    seen_species = set()
    for index, entry in enumerate(table.get("loop_clips", [])):
        where = f"loop_clips[{entry.get('species', index)}]"
        species_name = entry.get("species")
        if species_name not in species_by_name:
            findings.error(where, f"species '{species_name}' not in species")
        if species_name in seen_species:
            findings.error(where, "duplicate loop_clips entry for the same species")
        seen_species.add(species_name)
        if check_game_path_value(findings, where, "target_folder", entry.get("target_folder")) is False:
            continue
        if not isinstance(entry.get("update_species_run_speed"), bool):
            findings.error(where, "update_species_run_speed must be true or false")
        for field in ("feet", "feet_tips", "helper_prefixes"):
            value = entry.get(field)
            if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
                findings.error(where, f"{field} must be a list of strings")
        if not entry.get("feet"):
            findings.error(where, "feet must not be empty")
        check_loop_clips(findings, where, entry, species_by_name.get(species_name), game_paths)


def check_loop_clips(findings, where, entry, species_entry, game_paths):
    clips = entry.get("clips", [])
    if not clips:
        findings.error(where, "clips must be a non-empty list")
    roles = []
    for clip in clips:
        clip_where = f"{where}.{clip.get('role')}"
        if clip.get("role") not in LOOP_ROLES:
            findings.error(clip_where, f"role must be one of {list(LOOP_ROLES)}")
        roles.append(clip.get("role"))
        if check_game_path_value(findings, clip_where, "source", clip.get("source")):
            game_paths.append((f"{clip_where}.source", clip["source"]))
        check_game_path_value(findings, clip_where, "target", clip.get("target"))
        frame = clip.get("motion_end_frame")
        if not isinstance(frame, int) or isinstance(frame, bool) or frame < 2:
            findings.error(clip_where, f"motion_end_frame must be an integer >= 2 (got {frame!r})")
        if species_entry and isinstance(species_entry.get(clip.get("role")), dict) and species_entry[clip["role"]].get("anim") != clip.get("target"):
            findings.warn(clip_where, f"species {clip.get('role')} anim '{species_entry[clip['role']].get('anim')}' differs from loop target '{clip.get('target')}'")
    if len(set(roles)) != len(roles):
        findings.error(where, "duplicate role in clips")
    if entry.get("update_species_run_speed") is True and "run" not in roles:
        findings.error(where, "update_species_run_speed needs a clip with role 'run'")


def collect_generated_paths(table):
    generated = set()
    for copy in table.get("root_locked_copies", []):
        generated.add(copy.get("target"))
    for entry in table.get("loop_clips", []):
        generated.update(clip.get("target") for clip in entry.get("clips", []))
    for target_key, target in table.get("retarget", {}).get("targets", {}).items():
        generated.update(species_table.retarget_output_path(table, target_key, clip_key) for clip_key in target.get("clips", []))
    return generated


def check_paths_on_disk(findings, game_paths, generated, project_root):
    for where, game_path in game_paths:
        if os.path.isfile(species_table.content_path_to_file(game_path, project_root)):
            continue
        if game_path in generated:
            findings.warn(where, f"asset not generated yet: {game_path}")
            continue
        findings.error(where, f"missing asset on disk: {game_path}")


def check_table(table, project_root):
    findings = Findings()
    if table.get("schema") != 1:
        findings.error("table", f"schema must be 1 (got {table.get('schema')!r})")
    paths = table.get("paths")
    if not isinstance(paths, dict) or any(key not in paths for key in REQUIRED_PATH_KEYS):
        findings.error("table", f"paths must contain {list(REQUIRED_PATH_KEYS)}")
        return findings
    for key in ("species", "retarget", "loop_clips", "root_locked_copies"):
        if key not in table:
            findings.error("table", f"missing section '{key}'")
    if not isinstance(table.get("species"), list):
        return findings
    definitions_dir = os.path.join(project_root, paths["definitions_dir"])
    game_paths = []
    species_by_name = {}
    for entry in table["species"]:
        name = entry.get("name") if isinstance(entry, dict) else None
        if name in species_by_name:
            findings.error(f"species[{name}]", "duplicate species name")
        species_by_name[name] = entry
        check_species_entry(findings, entry, definitions_dir, game_paths)
    check_retarget(findings, table.get("retarget", {}), game_paths)
    check_loop_entries(findings, table, species_by_name, game_paths)
    for index, copy in enumerate(table.get("root_locked_copies", [])):
        where = f"root_locked_copies[{index}]"
        for field in ("target", "source"):
            check_game_path_value(findings, where, field, copy.get(field))
        if isinstance(copy.get("source"), str):
            game_paths.append((f"{where}.source", copy["source"]))
    check_paths_on_disk(findings, game_paths, collect_generated_paths(table), project_root)
    return findings


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="Check Tools/MonsterAI/species_table.json without the editor.")
    parser.add_argument("--root", default=DEFAULT_ROOT, help="project root containing Content/")
    parser.add_argument("--table", default=species_table.DEFAULT_TABLE_PATH, help="path to species_table.json")
    arguments = parser.parse_args()
    try:
        table = species_table.load_table(arguments.table)
    except (OSError, ValueError) as error:
        print(f"ERROR table: cannot read {arguments.table}: {error}")
        return 1
    try:
        findings = check_table(table, arguments.root)
    except (AttributeError, KeyError, TypeError) as error:
        print(f"ERROR table: malformed structure ({type(error).__name__}: {error})")
        return 1
    for line in findings.lines:
        print(line)
    retarget = table.get("retarget", {})
    print(f"SUMMARY: {len(table.get('species', []))} species, {len(retarget.get('targets', {}))} retarget targets, {len(table.get('loop_clips', []))} loop entries; "
          f"{findings.error_count} errors, {findings.warning_count} warnings")
    return 1 if findings.error_count else 0


if __name__ == "__main__":
    sys.exit(main())

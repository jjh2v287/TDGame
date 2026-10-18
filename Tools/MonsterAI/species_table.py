"""실행 환경: 일반 파이썬(unreal 모듈 없이 import 가능). 몬스터 종 도구와 표 검사 도구가 함께 쓰는 표 읽기·기본값·경로 함수
실행 명령: 직접 실행하지 않는다. 에디터 도구와 check_species_table.py 가 import 한다
출력: 없음(함수 모음). 표 정본은 Tools/MonsterAI/species_table.json
상태: 현행(M3-33)
"""
import json
import os

DEFAULT_TABLE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "species_table.json")
SPECIES_ASSET_PREFIX = "DA_TDMonster_"
MAXIMUM_EXTENDS_DEPTH = 3
CLIP_DEFAULTS = {"anim": None, "start": 0.0, "end": 0.0, "rate": 1.0, "impact": 0.0, "recovery": 0.3}
SPECIES_DEFAULTS = {
    "followers": [],
    "height_offset": 0.0,
    "corpse_seconds": 8.0,
    "ragdoll_after_death": False,
    "ragdoll_without_death_clip": True,
    "corpse_sink_seconds": 1.5,
}
LOCOMOTION_CLIP_KEYS = ("idle", "walk", "run")
OPTIONAL_CLIP_KEYS = ("hit", "death")


def load_table(path=None):
    with open(path or DEFAULT_TABLE_PATH, encoding="utf-8") as handle:
        return json.load(handle)


def iter_species(table, only=None):
    for entry in table.get("species", []):
        if only and entry["name"] not in only:
            continue
        yield entry


def fill_clip_defaults(raw_clip):
    clip = dict(CLIP_DEFAULTS)
    if raw_clip:
        clip.update(raw_clip)
    return clip


def fill_species_defaults(entry):
    filled = dict(SPECIES_DEFAULTS)
    filled["followers"] = []
    filled.update(entry)
    for key in LOCOMOTION_CLIP_KEYS + OPTIONAL_CLIP_KEYS:
        filled[key] = fill_clip_defaults(entry.get(key))
    filled["abilities"] = {name: fill_clip_defaults(raw) for name, raw in entry.get("abilities", {}).items()}
    return filled


def species_asset_name(species_name):
    return SPECIES_ASSET_PREFIX + species_name


def species_asset_path(table, species_name):
    return f"{table['paths']['species_folder']}/{species_asset_name(species_name)}"


def retarget_output_path(table, target_key, clip_key):
    return f"{table['paths']['animation_folder']}/{target_key}/AS_TD_{target_key}_{clip_key}"


def content_path_to_file(game_path, project_root):
    relative = game_path[len("/Game/"):] if game_path.startswith("/Game/") else game_path.lstrip("/")
    folder, _, name = relative.rpartition("/")
    name = name.split(".")[0]
    parts = [project_root, "Content"] + ([folder] if folder else []) + [name + ".uasset"]
    return os.path.normpath(os.path.join(*parts))


def read_definition_ability_keys(definitions_dir, definition_id):
    path = os.path.join(definitions_dir, definition_id + ".json")
    if not os.path.isfile(path):
        return None, None
    with open(path, encoding="utf-8") as handle:
        root = json.load(handle)
    abilities = root.get("abilities")
    return (list(abilities) if isinstance(abilities, dict) else []), root.get("extends")


def resolve_definition_abilities(definitions_dir, definition_id):
    keys, parent_id = read_definition_ability_keys(definitions_dir, definition_id)
    if keys is None:
        return None
    resolved = list(keys)
    visited = {definition_id}
    depth = 0
    while parent_id and parent_id not in visited and depth < MAXIMUM_EXTENDS_DEPTH:
        parent_keys, next_parent_id = read_definition_ability_keys(definitions_dir, parent_id)
        if parent_keys is None:
            break
        resolved += [key for key in parent_keys if key not in resolved]
        visited.add(parent_id)
        parent_id = next_parent_id
        depth += 1
    return resolved


def compare_ability_keys(entry, definition_keys):
    clip_keys = set(entry.get("abilities", {}))
    definition_set = set(definition_keys)
    return sorted(clip_keys - definition_set), sorted(definition_set - clip_keys)


def format_ability_mismatch(species_name, clip_only, definition_only):
    return f"ABILITY MISMATCH {species_name}: 클립만 {clip_only} / 정의만 {definition_only}"

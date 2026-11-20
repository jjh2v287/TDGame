"""실행 환경: 밖에서는 시스템 Python, 안에서는 TDGame 언리얼 에디터(LV_DarkFantasy_OpenWorld 열림·공터 3곳 표식 지역 로드·PIE 꺼짐, ATDArenaEncounter와 정의의 열린 모드 필드가 컴파일된 뒤). 같은 파일이 두 모드로 돈다.
실행 명령: python Tools/Gameplay/editor_place_field_encounters.py [--dry-run]  (PowerShell, 동료 세션이 에디터를 쓰지 않고 build_hold.json이 빌 때만)
출력: 조우 정의 DA_TDArena_Field{West,North,East}와 공터 표식 위 ATDArenaEncounter 3개(외부 액터), Saved/Gameplay/FieldEncounters/placement.json(공터별 표식·바닥 높이·요·반경·체크포인트·정의/액터 처리 create|update|unchanged·저장한 패키지). 종료 0 = 성공, 1 = 실패(남은 더러운 패키지 포함), 2 = 에디터 연결 실패
상태: 신규(GP-03), 에디터 미실행 검증만 됨. 같은 값이면 설정·저장 0이라 여러 번 돌려도 같은 결과이고, 이 스크립트가 바꾼 정의와 외부 액터 패키지만 저장한다(save_dirty_packages 안 씀).
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys

try:
    import unreal
except ImportError:
    unreal = None

MAP_NAME = "LV_DarkFantasy_OpenWorld"
DEFINITION_DIRECTORY = "/Game/MonsterAI/Encounters"
SPECIES_DIRECTORY = "/Game/MonsterAI/Species"
ACTOR_TAG = "TDFieldEncounter"
OUTPUT_DIRECTORY_PARTS = ("Saved", "Gameplay", "FieldEncounters")
REQUEST_FILE_NAME = "request.json"
PLACEMENT_FILE_NAME = "placement.json"
EXTERNAL_ACTOR_FOLDER_MARKER = "__ExternalActors__"
FLOAT_TOLERANCE = 1e-3
YAW_STEP_DEGREES = 15.0
YAW_STEP_COUNT = 12
START_INSET_CM = 400.0
CHECKPOINT_OUTSET_CANDIDATES_CM = (600.0, 800.0, 1000.0)
PLAYER_CAPSULE_RADIUS_CM = 34.0
PLAYER_CAPSULE_HALF_HEIGHT_CM = 88.0
CHECKPOINT_CLEARANCE_CM = 25.0
CHECKPOINT_PROBE_MARGIN_CM = 15.0
CAPSULE_SWEEP_ABOVE_CM = 1000.0
CAPSULE_SWEEP_BELOW_CM = 300.0
PLAYER_PROFILE_NAME = "Pawn"
BLOCKING_OBJECT_TYPE_NAMES = ("OBJECT_TYPE_QUERY1", "OBJECT_TYPE_QUERY2", "OBJECT_TYPE_QUERY4")
PLAYER_CHANNEL_NAME = "ECC_PAWN"
BLOCK_RESPONSE_NAME = "ECR_BLOCK"
HIT_BLOCKING = 0
HIT_INITIAL_OVERLAP = 1
HIT_LOCATION = 4
CHECKPOINT_HEIGHT_TOLERANCE_CM = 200.0
SIGHT_HEIGHT_CM = 100.0
FLOOR_TRACE_HALF_RANGE_CM = 5000.0
WALKABLE_NORMAL_Z_MIN = 0.5
LEASH_MARGIN_CM = 600.0
LEASH_RESET_SECONDS = 5.0
FAILURE_RESET_SECONDS = 3.0
BANNER_SECONDS = 3.0
VICTORY_BANNER_SECONDS = 4.0
AROUND_CHALLENGER = "aroundchallenger"
HIT_DISTANCE = 3
HIT_IMPACT_POINT = 5
HIT_IMPACT_NORMAL = 7
HIT_ACTOR = 9
HIT_COMPONENT = 10

SPECIES_ASSETS = {
    "Goblin": "DA_TDMonster_Goblin",
    "Hyena": "DA_TDMonster_Hyena",
    "Barghest": "DA_TDMonster_Barghest",
    "BarghestAlpha": "DA_TDMonster_BarghestAlpha",
    "BookHeadBrute": "DA_TDMonster_BookHeadBrute",
    "BookHeadCaster": "DA_TDMonster_BookHeadCaster",
    "StoneGolem": "DA_TDMonster_StoneGolem",
    "Centaur": "DA_TDMonster_Centaur",
}

FIELDS = (
    {
        "id": "FieldWest",
        "marker_label": "TDArena_ArenaWest",
        "display_name": "서쪽 공터 — 고블린 척후",
        "reward_text": "서쪽 공터를 되찾았다",
        "radius_cm": 2200.0,
        "waves": (
            {"title": "고블린 척후", "delay_seconds": 2.0, "entries": (("Goblin", 3, 800.0, 120.0),)},
            {"title": "하이에나 습격", "delay_seconds": 2.5, "entries": (("Hyena", 4, 1000.0, 180.0), ("Goblin", 2, 700.0, 90.0))},
        ),
    },
    {
        "id": "FieldNorth",
        "marker_label": "TDArena_ArenaNorth",
        "display_name": "북쪽 공터 — 바게스트 무리",
        "reward_text": "북쪽 공터를 되찾았다",
        "radius_cm": 2400.0,
        "waves": (
            {"title": "돌진하는 무리", "delay_seconds": 2.0, "entries": (("Barghest", 3, 900.0, 140.0),)},
            {"title": "우두머리의 무리", "delay_seconds": 2.5, "entries": (("BarghestAlpha", 1, 1000.0, 0.0), ("Barghest", 3, 900.0, 160.0))},
        ),
    },
    {
        "id": "FieldEast",
        "marker_label": "TDArena_ArenaEast",
        "display_name": "동쪽 공터 — 책머리 사제단",
        "reward_text": "동쪽 공터를 되찾았다",
        "radius_cm": 2600.0,
        "waves": (
            {"title": "책머리 사제단", "delay_seconds": 2.0, "entries": (("BookHeadBrute", 1, 800.0, 0.0), ("BookHeadCaster", 2, 1300.0, 120.0))},
            {"title": "석상과 사수", "delay_seconds": 2.5, "entries": (("StoneGolem", 1, 900.0, 0.0), ("Centaur", 1, 1400.0, 0.0))},
        ),
    },
)


class FieldEncounterError(RuntimeError):
    pass


def project_root():
    if unreal is not None:
        return unreal.SystemLibrary.get_project_directory()
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def output_directory():
    return os.path.join(project_root(), *OUTPUT_DIRECTORY_PARTS)


def write_json_file(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=1)
        handle.write("\n")


def start_radius_cm(field):
    return field["radius_cm"] - START_INSET_CM


def leash_radius_cm(field):
    return field["radius_cm"] + LEASH_MARGIN_CM


def species_object_path(species_key):
    asset_name = SPECIES_ASSETS[species_key]
    return f"{SPECIES_DIRECTORY}/{asset_name}.{asset_name}"


def normalize_token(value):
    return re.sub(r"[^a-z0-9]", "", str(getattr(value, "name", value)).lower())


def normalize_object_path(text):
    match = re.search(r"/(?:Game|Engine)/[^\s'\"]+", text)
    if match is None:
        return ""
    path = match.group(0)
    if "." in path:
        return path
    return f"{path}.{path.rsplit('/', 1)[1]}"


def to_object_path(value):
    if value is None:
        return ""
    for getter_name in ("get_path_name", "to_soft_object_path", "get_asset_path_string", "export_text"):
        getter = getattr(value, getter_name, None)
        if getter is None:
            continue
        try:
            converted = getter()
        except Exception:
            continue
        path = normalize_object_path(str(converted))
        if path:
            return path
    return normalize_object_path(str(value))


def wrap_degrees(angle):
    return ((angle + 180.0) % 360.0) - 180.0


def plain_equal(left, right):
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(plain_equal(left[key], right[key]) for key in left)
    if isinstance(left, (list, tuple)) and isinstance(right, (list, tuple)):
        return len(left) == len(right) and all(plain_equal(a, b) for a, b in zip(left, right))
    if isinstance(left, bool) or isinstance(right, bool):
        return left is right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return abs(float(left) - float(right)) <= FLOAT_TOLERANCE
    return left == right


def wanted_entry(entry):
    species_key, count, distance_cm, spread_degrees = entry
    return {
        "species": species_object_path(species_key),
        "count": count,
        "anchor": AROUND_CHALLENGER,
        "distance_cm": distance_cm,
        "spread_degrees": spread_degrees,
        "shows_boss_health_bar": False,
        "boss_display_name": "",
        "boss_phase_health_marks": [],
    }


def wanted_definition(field):
    return {
        "encounter_id": field["id"],
        "display_name": field["display_name"],
        "waves": [
            {
                "title": wave["title"],
                "start_delay_seconds": wave["delay_seconds"],
                "entries": [wanted_entry(entry) for entry in wave["entries"]],
            }
            for wave in field["waves"]
        ],
        "victory_reward_text": field["reward_text"],
        "failure_reset_seconds": FAILURE_RESET_SECONDS,
        "banner_seconds": BANNER_SECONDS,
        "victory_banner_seconds": VICTORY_BANNER_SECONDS,
        "should_start_from_any_direction": True,
        "should_reset_when_challenger_leaves": True,
        "leash_margin_cm": LEASH_MARGIN_CM,
        "leash_reset_seconds": LEASH_RESET_SECONDS,
    }


def wanted_actor(field, layout, definition_path):
    return {
        "label": f"TDField_Encounter{field['id']}",
        "tags": sorted([ACTOR_TAG, f"{ACTOR_TAG}_{field['id']}"]),
        "definition": definition_path,
        "arena_radius_cm": field["radius_cm"],
        "should_place_entry_from_radius": False,
        "should_set_respawn_checkpoint": True,
        "should_record_victory_in_world_state": True,
        "seal_gate_count": 0,
        "exit_gate_count": 0,
        "location": layout["center_cm"],
        "yaw": layout["yaw_degrees"],
        "checkpoint_arrow": layout["checkpoint_relative_cm"],
    }


def actor_plain_equal(left, right):
    if not plain_equal({key: value for key, value in left.items() if key != "yaw"}, {key: value for key, value in right.items() if key != "yaw"}):
        return False
    return abs(wrap_degrees(left["yaw"] - right["yaw"])) <= FLOAT_TOLERANCE


def candidate_yaws(start_yaw):
    candidates = [wrap_degrees(start_yaw)]
    for step in range(1, YAW_STEP_COUNT + 1):
        for sign in (1.0, -1.0):
            candidate = wrap_degrees(start_yaw + sign * step * YAW_STEP_DEGREES)
            if not any(abs(wrap_degrees(candidate - existing)) < FLOAT_TOLERANCE for existing in candidates):
                candidates.append(candidate)
    return candidates


def point_behind(center_xy, yaw_degrees, distance_cm):
    radians = math.radians(yaw_degrees)
    return [center_xy[0] - math.cos(radians) * distance_cm, center_xy[1] - math.sin(radians) * distance_cm]


def pick_yaw(center_xy, floor_z, start_yaw, radius_cm, find_floor_z, is_sight_clear, find_stop_z, is_checkpoint_clear):
    for yaw in candidate_yaws(start_yaw):
        for outset_cm in CHECKPOINT_OUTSET_CANDIDATES_CM:
            checkpoint_xy = point_behind(center_xy, yaw, radius_cm + outset_cm)
            checkpoint_floor_z = find_floor_z(checkpoint_xy[0], checkpoint_xy[1])
            if checkpoint_floor_z is None or abs(checkpoint_floor_z - floor_z) > CHECKPOINT_HEIGHT_TOLERANCE_CM:
                continue
            stop_z = find_stop_z(checkpoint_xy[0], checkpoint_xy[1], checkpoint_floor_z)
            if stop_z is None:
                continue
            checkpoint_z = stop_z + CHECKPOINT_CLEARANCE_CM
            edge_xy = point_behind(center_xy, yaw, radius_cm - START_INSET_CM)
            sight_start = [checkpoint_xy[0], checkpoint_xy[1], checkpoint_floor_z + SIGHT_HEIGHT_CM]
            sight_end = [edge_xy[0], edge_xy[1], floor_z + SIGHT_HEIGHT_CM]
            if not is_sight_clear(sight_start, sight_end):
                continue
            if not is_checkpoint_clear(checkpoint_xy[0], checkpoint_xy[1], checkpoint_z):
                continue
            return yaw, checkpoint_xy, checkpoint_floor_z, checkpoint_z, outset_cm
    return None


def vec(values):
    return unreal.Vector(float(values[0]), float(values[1]), float(values[2]))


def read_property(target, name):
    for candidate in (name, "b_" + name):
        try:
            return target.get_editor_property(candidate)
        except Exception:
            continue
    raise FieldEncounterError(f"프로퍼티를 읽을 수 없음: {target.get_name()}.{name}")


def write_property(target, name, value):
    for candidate in (name, "b_" + name):
        try:
            target.set_editor_property(candidate, value)
            return
        except Exception:
            continue
    raise FieldEncounterError(f"프로퍼티를 쓸 수 없음: {target.get_name()}.{name}")


def dirty_package_names():
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return sorted({package.get_name() for package in dirty})


def load_species_assets():
    species = {}
    for species_key in sorted({entry[0] for field in FIELDS for wave in field["waves"] for entry in wave["entries"]}):
        asset = unreal.EditorAssetLibrary.load_asset(species_object_path(species_key))
        if asset is None:
            raise FieldEncounterError(f"종 에셋 없음: {species_object_path(species_key)}")
        species[species_key] = asset
    return species


def read_entry(entry):
    return {
        "species": to_object_path(read_property(entry, "species")),
        "count": int(read_property(entry, "count")),
        "anchor": normalize_token(read_property(entry, "anchor")),
        "distance_cm": float(read_property(entry, "distance_cm")),
        "spread_degrees": float(read_property(entry, "spread_degrees")),
        "shows_boss_health_bar": bool(read_property(entry, "shows_boss_health_bar")),
        "boss_display_name": str(read_property(entry, "boss_display_name")),
        "boss_phase_health_marks": [float(mark) for mark in read_property(entry, "boss_phase_health_marks")],
    }


def read_definition(definition):
    return {
        "encounter_id": str(read_property(definition, "encounter_id")),
        "display_name": str(read_property(definition, "display_name")),
        "waves": [
            {
                "title": str(read_property(wave, "title")),
                "start_delay_seconds": float(read_property(wave, "start_delay_seconds")),
                "entries": [read_entry(entry) for entry in read_property(wave, "entries")],
            }
            for wave in read_property(definition, "waves")
        ],
        "victory_reward_text": str(read_property(definition, "victory_reward_text")),
        "failure_reset_seconds": float(read_property(definition, "failure_reset_seconds")),
        "banner_seconds": float(read_property(definition, "banner_seconds")),
        "victory_banner_seconds": float(read_property(definition, "victory_banner_seconds")),
        "should_start_from_any_direction": bool(read_property(definition, "should_start_from_any_direction")),
        "should_reset_when_challenger_leaves": bool(read_property(definition, "should_reset_when_challenger_leaves")),
        "leash_margin_cm": float(read_property(definition, "leash_margin_cm")),
        "leash_reset_seconds": float(read_property(definition, "leash_reset_seconds")),
    }


def make_entry(entry, species):
    species_key, count, distance_cm, spread_degrees = entry
    created = unreal.TDArenaSpawnEntry()
    write_property(created, "species", species[species_key])
    write_property(created, "count", count)
    write_property(created, "anchor", unreal.TDArenaSpawnAnchor.AROUND_CHALLENGER)
    write_property(created, "distance_cm", distance_cm)
    write_property(created, "spread_degrees", spread_degrees)
    return created


def make_wave(wave, species):
    created = unreal.TDArenaWave()
    write_property(created, "title", unreal.Text(wave["title"]))
    write_property(created, "start_delay_seconds", wave["delay_seconds"])
    write_property(created, "entries", [make_entry(entry, species) for entry in wave["entries"]])
    return created


def apply_definition(definition, field, species):
    write_property(definition, "encounter_id", field["id"])
    write_property(definition, "display_name", unreal.Text(field["display_name"]))
    write_property(definition, "waves", [make_wave(wave, species) for wave in field["waves"]])
    write_property(definition, "victory_reward_text", unreal.Text(field["reward_text"]))
    write_property(definition, "failure_reset_seconds", FAILURE_RESET_SECONDS)
    write_property(definition, "banner_seconds", BANNER_SECONDS)
    write_property(definition, "victory_banner_seconds", VICTORY_BANNER_SECONDS)
    write_property(definition, "should_start_from_any_direction", True)
    write_property(definition, "should_reset_when_challenger_leaves", True)
    write_property(definition, "leash_margin_cm", LEASH_MARGIN_CM)
    write_property(definition, "leash_reset_seconds", LEASH_RESET_SECONDS)


def create_definition(definition_name):
    assets = unreal.EditorAssetLibrary
    if not assets.does_directory_exist(DEFINITION_DIRECTORY) and not assets.make_directory(DEFINITION_DIRECTORY):
        raise FieldEncounterError(f"정의 폴더 생성 실패: {DEFINITION_DIRECTORY}")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TDArenaEncounterDefinition)
    definition = unreal.AssetToolsHelpers.get_asset_tools().create_asset(definition_name, DEFINITION_DIRECTORY, unreal.TDArenaEncounterDefinition, factory)
    if not isinstance(definition, unreal.TDArenaEncounterDefinition):
        raise FieldEncounterError(f"조우 정의 생성 실패: {definition_name}")
    return definition


def ensure_definition(field, species, is_dry_run):
    assets = unreal.EditorAssetLibrary
    definition_name = f"DA_TDArena_{field['id']}"
    asset_path = f"{DEFINITION_DIRECTORY}/{definition_name}"
    object_path = f"{asset_path}.{definition_name}"
    exists = assets.does_asset_exist(asset_path)
    definition = assets.load_asset(asset_path) if exists else None
    if exists and not isinstance(definition, unreal.TDArenaEncounterDefinition):
        raise FieldEncounterError(f"기존 에셋이 조우 정의가 아님: {asset_path}")
    if exists and plain_equal(read_definition(definition), wanted_definition(field)):
        is_unsaved = definition.get_outermost().get_name() in dirty_package_names()
        if not is_unsaved or is_dry_run:
            return "unchanged", object_path, None
        if not assets.save_loaded_asset(definition, only_if_is_dirty=False):
            raise FieldEncounterError(f"조우 정의 저장 실패: {asset_path}")
        return "unchanged", object_path, asset_path
    action = "update" if exists else "create"
    if is_dry_run:
        return action, object_path, None
    if not exists:
        definition = create_definition(definition_name)
    apply_definition(definition, field, species)
    if not assets.save_loaded_asset(definition, only_if_is_dirty=False):
        raise FieldEncounterError(f"조우 정의 저장 실패: {asset_path}")
    return action, object_path, asset_path


def describe_hit(hit):
    values = hit.to_tuple()
    actor = values[HIT_ACTOR]
    component = values[HIT_COMPONENT]
    return {
        "distance": float(values[HIT_DISTANCE]),
        "impact": [float(values[HIT_IMPACT_POINT].x), float(values[HIT_IMPACT_POINT].y), float(values[HIT_IMPACT_POINT].z)],
        "normal_z": float(values[HIT_IMPACT_NORMAL].z),
        "actor_class": actor.get_class().get_name() if actor else "",
        "component_class": component.get_class().get_name() if component else "",
    }


def trace_hits(world, start, end, ignored_actors):
    hits = unreal.SystemLibrary.line_trace_multi(world, vec(start), vec(end), unreal.TraceTypeQuery.ECC_VISIBILITY, True, ignored_actors, unreal.DrawDebugTrace.NONE, True) or []
    return [describe_hit(hit) for hit in hits]


def make_floor_finder(world, ignored_actors):
    def find_floor_z(x, y):
        hits = trace_hits(world, [x, y, FLOOR_TRACE_HALF_RANGE_CM], [x, y, -FLOOR_TRACE_HALF_RANGE_CM], ignored_actors)
        for hit in hits:
            if "Landscape" in hit["actor_class"] + hit["component_class"]:
                return hit["impact"][2]
        for hit in hits:
            if hit["component_class"] != "BrushComponent" and hit["normal_z"] > WALKABLE_NORMAL_Z_MIN:
                return hit["impact"][2]
        return None

    return find_floor_z


def make_sight_check(world, ignored_actors):
    def is_sight_clear(start, end):
        return all(hit["component_class"] == "BrushComponent" for hit in trace_hits(world, start, end, ignored_actors))

    return is_sight_clear


def unwrap_hit(result):
    if result is None:
        return None
    if isinstance(result, (tuple, list)):
        if len(result) == 2 and isinstance(result[0], bool):
            return result[1] if result[0] else None
        return result[0] if result else None
    return result


def unwrap_components(result):
    if result is None:
        return []
    if isinstance(result, tuple) and len(result) == 2 and isinstance(result[0], bool):
        return list(result[1] or [])
    return list(result)


def blocking_object_types():
    types = []
    for name in BLOCKING_OBJECT_TYPE_NAMES:
        value = getattr(unreal.ObjectTypeQuery, name, None)
        if value is None:
            raise FieldEncounterError(f"오브젝트 타입 열거값 없음: {name}")
        types.append(value)
    return types


def make_stop_height_finder(world, ignored_actors):
    def find_stop_z(x, y, floor_hint_z):
        start = vec([x, y, floor_hint_z + CAPSULE_SWEEP_ABOVE_CM])
        end = vec([x, y, floor_hint_z - CAPSULE_SWEEP_BELOW_CM])
        hit = unwrap_hit(unreal.SystemLibrary.capsule_trace_single_by_profile(world, start, end, PLAYER_CAPSULE_RADIUS_CM, PLAYER_CAPSULE_HALF_HEIGHT_CM, unreal.Name(PLAYER_PROFILE_NAME), False, ignored_actors, unreal.DrawDebugTrace.NONE, True))
        if hit is None:
            return None
        values = hit.to_tuple()
        if not values[HIT_BLOCKING] or values[HIT_INITIAL_OVERLAP]:
            return None
        return float(values[HIT_LOCATION].z)

    return find_stop_z


def player_block_rule():
    channel = getattr(getattr(unreal, "CollisionChannel", None), PLAYER_CHANNEL_NAME, None)
    block_response = getattr(getattr(unreal, "CollisionResponseType", None), BLOCK_RESPONSE_NAME, None)
    if channel is None or block_response is None:
        raise FieldEncounterError(f"충돌 열거값 없음: CollisionChannel.{PLAYER_CHANNEL_NAME} 또는 CollisionResponseType.{BLOCK_RESPONSE_NAME}")
    return channel, block_response


def make_checkpoint_clear_check(world, ignored_actors):
    object_types = blocking_object_types()
    player_channel, block_response = player_block_rule()

    def blocks_player(component):
        return component.get_collision_response_to_channel(player_channel) == block_response

    def is_checkpoint_clear(x, y, z):
        probe_center = vec([x, y, z + CHECKPOINT_PROBE_MARGIN_CM])
        overlapped = unwrap_components(unreal.SystemLibrary.capsule_overlap_components(world, probe_center, PLAYER_CAPSULE_RADIUS_CM + CHECKPOINT_PROBE_MARGIN_CM, PLAYER_CAPSULE_HALF_HEIGHT_CM + CHECKPOINT_PROBE_MARGIN_CM, object_types, None, ignored_actors))
        return not any(blocks_player(component) for component in overlapped)

    return is_checkpoint_clear


def find_actor_by_label(actors, label, actor_class):
    matches = [actor for actor in actors if isinstance(actor, actor_class) and actor.get_actor_label() == label]
    if len(matches) > 1:
        raise FieldEncounterError(f"라벨이 겹치는 액터 {len(matches)}개: {label}")
    return matches[0] if matches else None


def find_field_actors(actors):
    return [actor for actor in actors if isinstance(actor, unreal.TDArenaEncounter) and ACTOR_TAG in [str(tag) for tag in actor.get_editor_property("tags")]]


def find_encounter_actor(field_actors, field_id):
    tagged = [actor for actor in field_actors if f"{ACTOR_TAG}_{field_id}" in [str(tag) for tag in actor.get_editor_property("tags")]]
    if len(tagged) > 1:
        raise FieldEncounterError(f"같은 태그의 조우 액터 {len(tagged)}개(자동 삭제 안 함): {ACTOR_TAG}_{field_id}")
    return tagged[0] if tagged else None


def read_actor(actor):
    location = actor.get_actor_location()
    arrow_location = actor.checkpoint_arrow.get_editor_property("relative_location")
    return {
        "label": actor.get_actor_label(),
        "tags": sorted(str(tag) for tag in actor.get_editor_property("tags")),
        "definition": to_object_path(read_property(actor, "definition")),
        "arena_radius_cm": float(read_property(actor, "arena_radius_cm")),
        "should_place_entry_from_radius": bool(read_property(actor, "should_place_entry_from_radius")),
        "should_set_respawn_checkpoint": bool(read_property(actor, "should_set_respawn_checkpoint")),
        "should_record_victory_in_world_state": bool(read_property(actor, "should_record_victory_in_world_state")),
        "seal_gate_count": len(list(read_property(actor, "seal_gate_actors"))),
        "exit_gate_count": len(list(read_property(actor, "exit_gate_actors"))),
        "location": [location.x, location.y, location.z],
        "yaw": actor.get_actor_rotation().yaw,
        "checkpoint_arrow": [arrow_location.x, arrow_location.y, arrow_location.z],
    }


def apply_actor(actor, wanted):
    actor.set_actor_label(wanted["label"])
    write_property(actor, "tags", [unreal.Name(tag) for tag in wanted["tags"]])
    write_property(actor, "should_place_entry_from_radius", wanted["should_place_entry_from_radius"])
    write_property(actor, "arena_radius_cm", wanted["arena_radius_cm"])
    write_property(actor, "should_set_respawn_checkpoint", wanted["should_set_respawn_checkpoint"])
    write_property(actor, "should_record_victory_in_world_state", wanted["should_record_victory_in_world_state"])
    write_property(actor, "seal_gate_actors", [])
    write_property(actor, "exit_gate_actors", [])
    definition = unreal.EditorAssetLibrary.load_asset(wanted["definition"].split(".")[0])
    if definition is None:
        raise FieldEncounterError(f"조우 정의를 불러올 수 없음: {wanted['definition']}")
    write_property(actor, "definition", definition)
    actor.checkpoint_arrow.set_editor_property("relative_location", vec(wanted["checkpoint_arrow"]))
    actor.set_actor_location_and_rotation(vec(wanted["location"]), unreal.Rotator(roll=0.0, pitch=0.0, yaw=wanted["yaw"]), False, True)


def find_actor_package(actor):
    for getter_name in ("get_external_package", "get_package", "get_outermost"):
        getter = getattr(actor, getter_name, None)
        if getter is None:
            continue
        try:
            package = getter()
        except Exception:
            continue
        if package is not None and EXTERNAL_ACTOR_FOLDER_MARKER in package.get_name():
            return package
    raise FieldEncounterError(f"외부 액터 패키지를 찾지 못함: {actor.get_actor_label()}")


def ensure_actor(field, layout, definition_path, field_actors, encounter_class, is_dry_run):
    actors_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    wanted = wanted_actor(field, layout, definition_path)
    actor = find_encounter_actor(field_actors, field["id"])
    if actor is not None and actor_plain_equal(read_actor(actor), wanted):
        package = find_actor_package(actor)
        is_unsaved = package.get_name() in dirty_package_names()
        return "unchanged", (package if is_unsaved and not is_dry_run else None)
    action = "update" if actor is not None else "create"
    if is_dry_run:
        return action, None
    if actor is None:
        actor = actors_subsystem.spawn_actor_from_class(encounter_class, vec(wanted["location"]), unreal.Rotator(roll=0.0, pitch=0.0, yaw=wanted["yaw"]))
        if actor is None:
            raise FieldEncounterError(f"조우 액터 생성 실패: {field['id']}")
    apply_actor(actor, wanted)
    return action, find_actor_package(actor)


def build_layout(field, marker, player_start, find_floor_z, is_sight_clear, find_stop_z, is_checkpoint_clear):
    marker_location = marker.get_actor_location()
    center_xy = [marker_location.x, marker_location.y]
    floor_z = find_floor_z(center_xy[0], center_xy[1])
    if floor_z is None:
        raise FieldEncounterError(f"표식 아래 바닥을 찾지 못함: {field['marker_label']}")
    start_location = player_start.get_actor_location()
    start_yaw = math.degrees(math.atan2(center_xy[1] - start_location.y, center_xy[0] - start_location.x))
    picked = pick_yaw(center_xy, floor_z, start_yaw, field["radius_cm"], find_floor_z, is_sight_clear, find_stop_z, is_checkpoint_clear)
    if picked is None:
        raise FieldEncounterError(f"체크포인트가 막히지 않는 요를 찾지 못함: {field['id']}")
    yaw, checkpoint_xy, checkpoint_floor_z, checkpoint_z, outset_cm = picked
    return {
        "marker_cm": [marker_location.x, marker_location.y, marker_location.z],
        "center_cm": [center_xy[0], center_xy[1], floor_z],
        "floor_z_cm": floor_z,
        "yaw_degrees": yaw,
        "checkpoint_cm": [checkpoint_xy[0], checkpoint_xy[1], checkpoint_z],
        "checkpoint_outset_cm": outset_cm,
        "checkpoint_relative_cm": [-(field["radius_cm"] + outset_cm), 0.0, checkpoint_z - floor_z],
    }


def describe_field(field, layout, definition_action, definition_path, actor_action):
    return {
        "id": field["id"],
        "marker_label": field["marker_label"],
        "marker_cm": layout["marker_cm"],
        "center_cm": layout["center_cm"],
        "floor_z_cm": layout["floor_z_cm"],
        "yaw_degrees": layout["yaw_degrees"],
        "radius_cm": field["radius_cm"],
        "start_radius_cm": start_radius_cm(field),
        "leash_radius_cm": leash_radius_cm(field),
        "checkpoint_cm": layout["checkpoint_cm"],
        "checkpoint_outset_cm": layout["checkpoint_outset_cm"],
        "definition_path": definition_path,
        "definition_action": definition_action,
        "actor_label": f"TDField_Encounter{field['id']}",
        "actor_action": actor_action,
    }


def run_in_editor_mode():
    placement_path = os.path.join(output_directory(), PLACEMENT_FILE_NAME)
    report = {"ok": False, "error": None, "dry_run": False, "map": None, "fields": [], "saved_packages": [], "dirty_before": [], "leftover_dirty": []}
    try:
        with open(os.path.join(output_directory(), REQUEST_FILE_NAME), encoding="utf-8") as handle:
            report["dry_run"] = bool(json.load(handle).get("dry_run"))
        execute_placement(report)
        report["ok"] = True
    except Exception as error:
        report["error"] = str(error)
    write_json_file(placement_path, report)
    print("PLACEMENT_JSON " + json.dumps(report, ensure_ascii=False))
    if not report["ok"]:
        raise RuntimeError(report["error"])


def execute_placement(report):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = editor.get_editor_world()
    if world is None or world.get_name() != MAP_NAME:
        raise FieldEncounterError(f"편집 월드가 {MAP_NAME}이 아님: {None if world is None else world.get_name()}")
    report["map"] = world.get_name()
    if not report["dry_run"] and editor.get_game_world() is not None:
        raise FieldEncounterError("PIE를 종료한 뒤 실행하세요.")
    report["dirty_before"] = dirty_package_names()
    species = load_species_assets()
    actors = list(actors_subsystem.get_all_level_actors())
    player_starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    if not player_starts:
        raise FieldEncounterError("PlayerStart를 찾지 못함(지역 미로드)")
    field_actors = find_field_actors(actors)
    find_floor_z = make_floor_finder(world, field_actors)
    is_sight_clear = make_sight_check(world, field_actors)
    find_stop_z = make_stop_height_finder(world, field_actors)
    is_checkpoint_clear = make_checkpoint_clear_check(world, field_actors)
    changed_packages = []
    saved_names = []
    for field in FIELDS:
        marker = find_actor_by_label(actors, field["marker_label"], unreal.TargetPoint)
        if marker is None:
            raise FieldEncounterError(f"표식을 찾지 못함(지역 미로드): {field['marker_label']}")
        layout = build_layout(field, marker, player_starts[0], find_floor_z, is_sight_clear, find_stop_z, is_checkpoint_clear)
        definition_action, definition_path, definition_package = ensure_definition(field, species, report["dry_run"])
        actor_action, actor_package = ensure_actor(field, layout, definition_path, field_actors, unreal.TDArenaEncounter, report["dry_run"])
        report["fields"].append(describe_field(field, layout, definition_action, definition_path, actor_action))
        if definition_package is not None:
            saved_names.append(definition_package)
        if actor_package is not None:
            changed_packages.append(actor_package)
    if changed_packages:
        if not unreal.EditorLoadingAndSavingUtils.save_packages(changed_packages, False):
            raise FieldEncounterError("외부 액터 패키지 저장 실패")
        saved_names.extend(package.get_name() for package in changed_packages)
    report["saved_packages"] = saved_names
    if report["dry_run"]:
        return
    report["leftover_dirty"] = sorted(set(dirty_package_names()) - set(report["dirty_before"]))
    if report["leftover_dirty"]:
        raise FieldEncounterError("저장 뒤에도 새로 더러운 패키지가 남음(자동 저장 안 함): " + ", ".join(report["leftover_dirty"]))


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="공터 3곳에 열린 조우 정의와 액터를 놓는다(에디터 원격 실행).")
    parser.add_argument("--dry-run", action="store_true", help="placement.json만 쓰고 에디터 내용은 바꾸지 않는다")
    return parser.parse_args(argv)


def run_outside_mode(argv):
    options = parse_arguments(argv)
    os.makedirs(output_directory(), exist_ok=True)
    write_json_file(os.path.join(output_directory(), REQUEST_FILE_NAME), {"dry_run": options.dry_run})
    run_in_editor_script = os.path.join(project_root(), "Tools", "run_in_editor.py")
    completed = subprocess.run([sys.executable, run_in_editor_script, os.path.abspath(__file__)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in completed.stdout.splitlines():
        if line.strip() and "PLACEMENT_JSON" not in line:
            print(line)
    placement_path = os.path.join(output_directory(), PLACEMENT_FILE_NAME)
    if completed.returncode == 2 or not os.path.exists(placement_path):
        return 2 if completed.returncode == 2 else 1
    with open(placement_path, encoding="utf-8") as handle:
        report = json.load(handle)
    print(json.dumps({"ok": report["ok"], "error": report["error"], "dry_run": report["dry_run"], "fields": [(item["id"], item["definition_action"], item["actor_action"]) for item in report["fields"]], "saved_packages": report["saved_packages"]}, ensure_ascii=False))
    return 0 if report["ok"] and completed.returncode == 0 else 1


if unreal is not None:
    run_in_editor_mode()
elif __name__ == "__main__":
    sys.exit(run_outside_mode(sys.argv[1:]))

"""실행 환경: 시스템 Python(언리얼 에디터가 TDGame을 열고 원격 실행이 켜진 상태, 에디터 앞창 유지 L-editor-06, FX-16b 통합 빌드와 에디터 재시작 뒤 UTDFluidSimStateCacheEditorLibrary·UTDFluidSimStateCacheAsset 노출, 미저장 작업 없음). 무대 생성기(editor_make_fluid_water_stage.py 등)로 맵을 만들고 저장한 뒤에만 실행하고, 무대를 다시 만들면 SimStateCache 지정이 사라지므로 이 도구를 다시 실행한다(R16b-S13).
실행 명령: python Tools/FluidFX/pie_bake_fluid_sim_cache.py [--request Saved/FluidFX/fluid_sim_cache_request.json] (PowerShell). 요청 JSON = {"level": "/Game/FluidFX/Maps/LV_TDFluidWaterStage", "backend": "gpu|cpu", "actors": [{"label": "<액터 라벨>", "name": "River", "asset_name": "<선택>"}], "settle_seconds": 60, "time_dilation": 1.0, "console": ["<PIE 콘솔 명령>"], "package_path": "/Game/FluidFX/SimCaches", "overwrite": false, "assign": false, "save_level": false, "verify": false, "verify_settle_seconds": 0.2}. 순서 = 레벨 열기 → PIE 시작 → console·time_dilation 적용 → 게임 시간 settle_seconds 대기 → capture_sim_state_cache_to_file → PIE 종료 → create_sim_state_cache_asset → assign 이 참이면 sim_state_cache 지정(다시 읽어 판정) → save_level 이 참일 때만 맵 저장. verify 가 참이면 가져오기·지정 없이 PIE 를 다시 돌려 재포착한 채널 이름·크기·형식을 이전 보고와 비교한다(CRC 는 순환 중복 검사 값으로 기록만)
출력: Saved/FluidFX/SimCache/<시각>/<name>.json·.bin(포착 파일), /Game/FluidFX/SimCaches/SC_TD_<레벨 이름에서 LV_TDFluid 를 뺀 이름>_<name> 데이터 에셋, Saved/FluidFX/fluid_sim_cache.json(백엔드·해상도·채널 이름·바이트·CRC·에셋·지정·맵 저장 여부), verify 는 Saved/FluidFX/fluid_sim_cache_verify.json. 종료 코드 0 성공 / 1 포착·가져오기·지정·검증 실패 / 2 환경 문제(요청 오류·unreal 이름 없음·PIE 시작 실패)
상태: 작성만(2026-10-29 FX-16b 묶음 E①, py_compile 과 가짜 unreal 정적 검토만, 실제 PIE·에셋 생성 미실행). 에셋 이름 규칙은 6c 예시(SC_TD_WaterStage_River)를 따르며 2.4 표의 'LV_ 뺀 이름'과 다르므로 필요하면 asset_name 으로 덮는다. 포착 파일 머리의 backend 키 철자는 명세에 없어 대소문자 무시·접미사로 비교하고 키가 없으면 경고만 한다.
"""
import argparse
import json
import re
import sys
import time
from datetime import date, datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))

DEFAULT_REQUEST_PATH = ROOT / "Saved" / "FluidFX" / "fluid_sim_cache_request.json"
REPORT_PATH = ROOT / "Saved" / "FluidFX" / "fluid_sim_cache.json"
VERIFY_REPORT_PATH = ROOT / "Saved" / "FluidFX" / "fluid_sim_cache_verify.json"
CACHE_OUTPUT_ROOT = ROOT / "Saved" / "FluidFX" / "SimCache"
DEFAULT_PACKAGE_PATH = "/Game/FluidFX/SimCaches"
RESULT_MARKER = "TD_FLUID_SIM_CACHE_RESULT"
ALLOWED_REQUEST_KEYS = frozenset({"level", "backend", "actors", "settle_seconds", "time_dilation", "console", "package_path", "overwrite", "assign", "save_level", "verify", "verify_settle_seconds"})
ALLOWED_ACTOR_KEYS = frozenset({"label", "name", "asset_name"})
NAME_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
LEVEL_PATTERN = re.compile(r"^/Game(/[A-Za-z0-9_]+)+$")
BACKENDS = ("cpu", "gpu")
EDITOR_TIMEOUT_SECONDS = 30
PIE_START_TIMEOUT_SECONDS = 240.0
PIE_STOP_TIMEOUT_SECONDS = 90.0
POLL_INTERVAL_SECONDS = 2.0
SETTLE_REAL_TIME_FACTOR = 6.0
SETTLE_REAL_TIME_MARGIN_SECONDS = 180.0
EXIT_FAILED = 1
EXIT_ENVIRONMENT = 2
REQUIRED_CLASSES = ("TDFluidLiveActor", "TDFluidSimStateCacheAsset", "TDFluidSimStateCacheEditorLibrary")
REQUIRED_METHODS = {
    "LevelEditorSubsystem": ["load_level", "save_current_level", "editor_request_begin_play", "editor_request_end_play"],
    "UnrealEditorSubsystem": ["get_game_world", "get_editor_world"],
    "GameplayStatics": ["get_time_seconds", "set_global_time_dilation", "get_all_actors_of_class"],
    "SystemLibrary": ["execute_console_command"],
    "TDFluidSimStateCacheEditorLibrary": ["capture_sim_state_cache_to_file", "create_sim_state_cache_asset"],
    "EditorAssetLibrary": ["load_asset", "save_asset"],
    "EditorActorSubsystem": ["get_all_level_actors"],
    "MathLibrary": ["class_is_child_of"],
    "EditorLoadingAndSavingUtils": ["get_dirty_map_packages", "get_dirty_content_packages"],
}

EDITOR_PRELUDE = r'''
import json
import re
import traceback
import unreal

PARAMS = json.loads(__PARAMS__)
MARKER = PARAMS["marker"]
LIBRARY = getattr(unreal, "TDFluidSimStateCacheEditorLibrary", None)


def emit(payload):
    print(MARKER, json.dumps(payload, ensure_ascii=False))


def find_missing_names():
    missing = [name for name in PARAMS["required_classes"] if not hasattr(unreal, name)]
    for class_name, method_names in PARAMS["required_methods"].items():
        owner = getattr(unreal, class_name, None)
        missing.extend(class_name + "." + method_name for method_name in method_names if owner is None or not hasattr(owner, method_name))
    return missing


def get_game_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def map_actors_by_label(actors):
    return {actor.get_actor_label(): actor for actor in actors}


def parse_library_result(text):
    if text is None:
        return {"ok": False, "error": "라이브러리 함수가 None 을 돌려줬다"}
    try:
        parsed = json.loads(text)
    except ValueError:
        return {"ok": False, "error": "라이브러리 결과가 JSON 이 아니다", "raw": str(text)[:300]}
    return parsed if isinstance(parsed, dict) else {"ok": False, "error": "라이브러리 결과가 객체가 아니다", "raw": str(text)[:300]}


def reference_package(value):
    if value is None:
        return ""
    getter = getattr(value, "get_path_name", None)
    text = getter() if getter is not None else str(value)
    match = re.search(r"/Game/[A-Za-z0-9_/]+", text)
    return match.group(0) if match else ""


def run_guarded(action):
    missing = find_missing_names()
    if missing:
        emit({"error": "unreal 이름 없음(통합 빌드·에디터 재시작 전이거나 바이너리가 헤더보다 오래됨)", "missing": missing})
        return
    try:
        emit(action())
    except Exception as error:
        emit({"error": str(error), "traceback": traceback.format_exc()[-1500:]})
'''

PREPARE_SNIPPET = r'''

def prepare():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        return {"error": "PIE 가 이미 실행 중이다. 먼저 끄세요"}
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    current_level = editor.get_editor_world().get_path_name().split(".")[0]
    level_changed = current_level != PARAMS["level"]
    if level_changed:
        dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
        if dirty:
            return {"error": "미저장 작업을 보존하려고 레벨을 바꾸지 않는다", "dirty": sorted({package.get_name() for package in dirty})}
        if not levels.load_level(PARAMS["level"]):
            return {"error": "레벨 열기 실패", "level": PARAMS["level"]}
    actors = map_actors_by_label(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    live_class = unreal.TDFluidLiveActor.static_class()
    entries = []
    for label in PARAMS["labels"]:
        actor = actors.get(label)
        is_live = actor is not None and unreal.MathLibrary.class_is_child_of(actor.get_class(), live_class)
        existing = ""
        if is_live:
            try:
                existing = reference_package(actor.get_editor_property("sim_state_cache"))
            except Exception:
                existing = ""
        entries.append({"label": label, "found": actor is not None, "is_live_actor": bool(is_live), "existing_cache": existing})
    return {"level_loaded_now": level_changed, "actors": entries}


run_guarded(prepare)
'''

START_PIE_SNIPPET = r'''

def start_pie():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
    return {"requested": True}


run_guarded(start_pie)
'''

POLL_SNIPPET = r'''

def poll():
    world = get_game_world()
    if world is None:
        return {"pie_running": False}
    actors = map_actors_by_label(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor))
    return {
        "pie_running": True,
        "game_seconds": float(unreal.GameplayStatics.get_time_seconds(world)),
        "labels_found": [label for label in PARAMS["labels"] if label in actors],
    }


run_guarded(poll)
'''

CONFIGURE_SNIPPET = r'''

def configure():
    world = get_game_world()
    if world is None:
        return {"error": "PIE 월드 없음"}
    for command in PARAMS["console"]:
        unreal.SystemLibrary.execute_console_command(world, command)
    unreal.GameplayStatics.set_global_time_dilation(world, PARAMS["time_dilation"])
    try:
        read_back = float(world.get_world_settings().get_editor_property("time_dilation"))
    except Exception:
        read_back = None
    return {"console": PARAMS["console"], "time_dilation": PARAMS["time_dilation"], "time_dilation_read_back": read_back, "game_seconds": float(unreal.GameplayStatics.get_time_seconds(world))}


run_guarded(configure)
'''

CAPTURE_SNIPPET = r'''

def capture():
    world = get_game_world()
    if world is None:
        return {"error": "PIE 월드 없음"}
    actors = map_actors_by_label(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDFluidLiveActor))
    results = []
    for request in PARAMS["captures"]:
        actor = actors.get(request["label"])
        if actor is None:
            results.append({"label": request["label"], "name": request["name"], "library_result": {"ok": False, "error": "PIE 월드에 액터 없음"}})
            continue
        text = LIBRARY.capture_sim_state_cache_to_file(actor, PARAMS["output_directory"], request["name"])
        results.append({"label": request["label"], "name": request["name"], "library_result": parse_library_result(text)})
    return {"game_seconds": float(unreal.GameplayStatics.get_time_seconds(world)), "results": results}


run_guarded(capture)
'''

STOP_PIE_SNIPPET = r'''

def stop_pie():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
    return {"requested": True}


run_guarded(stop_pie)
'''

IMPORT_SNIPPET = r'''

def assign_reference(actor, asset, asset_path):
    actor.modify()
    try:
        actor.set_editor_property("sim_state_cache", asset)
    except Exception:
        actor.set_editor_property("sim_state_cache", unreal.SoftObjectPath(asset_path + "." + asset_path.rsplit("/", 1)[1]))
    return reference_package(actor.get_editor_property("sim_state_cache"))


def import_one(item, actors):
    entry = {"label": item["label"], "asset_name": item["asset_name"], "assigned": False, "assigned_read_back": ""}
    asset_path = PARAMS["package_path"] + "/" + item["asset_name"]
    result = parse_library_result(LIBRARY.create_sim_state_cache_asset(item["json_path"], PARAMS["package_path"], item["asset_name"], PARAMS["overwrite"]))
    entry["library_result"] = result
    entry["asset_path"] = asset_path
    if not result.get("ok"):
        return entry
    entry["saved"] = bool(unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False))
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    entry["asset_loaded"] = asset is not None and isinstance(asset, unreal.TDFluidSimStateCacheAsset)
    if not entry["asset_loaded"]:
        return entry
    entry["is_usable"] = bool(asset.is_usable())
    if not PARAMS["assign"]:
        return entry
    actor = actors.get(item["label"])
    if actor is None:
        entry["assign_error"] = "에디터 레벨에 액터 없음"
        return entry
    entry["assigned_read_back"] = assign_reference(actor, asset, asset_path)
    entry["assigned"] = entry["assigned_read_back"] == asset_path
    return entry


def import_and_assign():
    if get_game_world() is not None:
        return {"error": "PIE 가 아직 실행 중이다(DA 생성·저장은 PIE 밖)"}
    actors = map_actors_by_label(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    entries = [import_one(item, actors) for item in PARAMS["imports"]]
    any_assigned = any(entry["assigned"] for entry in entries)
    level_saved = False
    if any_assigned and PARAMS["save_level"]:
        level_saved = bool(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level())
    return {"imports": entries, "level_saved": level_saved, "level_dirty_unsaved": bool(any_assigned and not level_saved)}


run_guarded(import_and_assign)
'''


def read_number(raw, key, default, minimum, maximum):
    value = raw.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{key}: 숫자여야 한다")
    if not minimum <= float(value) <= maximum:
        raise ValueError(f"{key}: {minimum}~{maximum} 범위여야 한다")
    return float(value)


def read_flag(raw, key):
    value = raw.get(key, False)
    if not isinstance(value, bool):
        raise ValueError(f"{key}: true 또는 false 여야 한다")
    return value


def make_default_asset_name(level, name):
    stem = level.rsplit("/", 1)[1]
    stem = stem[len("LV_"):] if stem.startswith("LV_") else stem
    stem = stem[len("TDFluid"):] if stem.startswith("TDFluid") else stem
    return f"SC_TD_{stem}_{name}"


def normalize_actor_entry(entry, level):
    if not isinstance(entry, dict) or set(entry) - ALLOWED_ACTOR_KEYS:
        raise ValueError(f"actors 항목은 label·name·asset_name 만 가진 객체여야 한다: {entry!r}")
    label = entry.get("label")
    name = entry.get("name")
    if not isinstance(label, str) or not label:
        raise ValueError("actors[].label: 비어 있지 않은 문자열이어야 한다")
    if not isinstance(name, str) or not NAME_PATTERN.match(name):
        raise ValueError(f"actors[].name: 영문자로 시작하는 영문·숫자·밑줄 이름이어야 한다: {name!r}")
    asset_name = entry.get("asset_name", make_default_asset_name(level, name))
    if not isinstance(asset_name, str) or not NAME_PATTERN.match(asset_name):
        raise ValueError(f"actors[].asset_name: 영문자로 시작하는 영문·숫자·밑줄 이름이어야 한다: {asset_name!r}")
    return {"label": label, "name": name, "asset_name": asset_name}


def normalize_request(raw):
    if not isinstance(raw, dict):
        raise ValueError("요청은 JSON 객체여야 한다")
    unknown = sorted(set(raw) - ALLOWED_REQUEST_KEYS)
    if unknown:
        raise ValueError(f"알 수 없는 키: {', '.join(unknown)}")
    level = raw.get("level")
    if not isinstance(level, str) or not LEVEL_PATTERN.match(level):
        raise ValueError("level: /Game/... 레벨 패키지 경로여야 한다")
    backend = raw.get("backend")
    if backend not in BACKENDS:
        raise ValueError("backend: cpu 또는 gpu 여야 한다")
    entries = raw.get("actors")
    if not isinstance(entries, list) or not entries:
        raise ValueError("actors: 비어 있지 않은 배열이어야 한다")
    actors = [normalize_actor_entry(entry, level) for entry in entries]
    for key in ("label", "name", "asset_name"):
        values = [actor[key] for actor in actors]
        if len(set(values)) != len(values):
            raise ValueError(f"actors[].{key} 가 겹친다")
    console = raw.get("console", [])
    if not isinstance(console, list) or not all(isinstance(command, str) and command for command in console):
        raise ValueError("console: 문자열 배열이어야 한다")
    package_path = raw.get("package_path", DEFAULT_PACKAGE_PATH)
    if not isinstance(package_path, str) or not LEVEL_PATTERN.match(package_path):
        raise ValueError("package_path: /Game/... 폴더 경로여야 한다")
    request = {
        "level": level,
        "backend": backend,
        "actors": actors,
        "settle_seconds": read_number(raw, "settle_seconds", 60.0, 0.1, 3600.0),
        "time_dilation": read_number(raw, "time_dilation", 1.0, 0.1, 20.0),
        "verify_settle_seconds": read_number(raw, "verify_settle_seconds", 0.2, 0.0, 60.0),
        "console": console,
        "package_path": package_path.rstrip("/"),
        "overwrite": read_flag(raw, "overwrite"),
        "assign": read_flag(raw, "assign"),
        "save_level": read_flag(raw, "save_level"),
        "verify": read_flag(raw, "verify"),
    }
    if request["save_level"] and not request["assign"]:
        raise ValueError("save_level 은 assign 이 참일 때만 쓴다")
    if request["verify"] and (request["assign"] or request["save_level"]):
        raise ValueError("verify 는 지정·맵 저장 없이 재포착 비교만 한다(assign·save_level 을 끈다)")
    return request


def parse_marker(lines):
    for line in lines:
        marker_index = line.find(RESULT_MARKER)
        if marker_index >= 0:
            return json.loads(line[marker_index + len(RESULT_MARKER):])
    return None


def run_snippet(snippet, params):
    import run_in_editor

    full_params = dict(params, marker=RESULT_MARKER, required_classes=list(REQUIRED_CLASSES), required_methods=REQUIRED_METHODS)
    code = (EDITOR_PRELUDE + snippet).replace("__PARAMS__", repr(json.dumps(full_params)))
    outcome = run_in_editor.run_code(code, timeout=EDITOR_TIMEOUT_SECONDS)
    if not outcome["connected"]:
        return {"error": "에디터에 연결하지 못했다", "lines": outcome["lines"][-5:]}
    result = parse_marker(outcome["lines"])
    if result is None:
        return {"error": "결과 표식이 없다", "lines": outcome["lines"][-8:]}
    return result


def wait_for_pie(labels, timeout_seconds):
    started = time.monotonic()
    state = {"pie_running": False}
    while time.monotonic() - started < timeout_seconds:
        state = run_snippet(POLL_SNIPPET, {"labels": labels})
        if "error" in state:
            return state
        if state["pie_running"] and state["game_seconds"] > 0.0 and len(state["labels_found"]) == len(labels):
            return state
        time.sleep(POLL_INTERVAL_SECONDS)
    return {"error": "PIE 시작 대기 시간 초과", "last_state": state}


def wait_for_pie_end(timeout_seconds):
    started = time.monotonic()
    while time.monotonic() - started < timeout_seconds:
        state = run_snippet(POLL_SNIPPET, {"labels": []})
        if "error" in state:
            return False
        if not state["pie_running"]:
            return True
        time.sleep(POLL_INTERVAL_SECONDS)
    return False


def wait_for_settle(labels, start_seconds, settle_seconds, time_dilation):
    real_timeout = settle_seconds * SETTLE_REAL_TIME_FACTOR / min(time_dilation, 1.0) + SETTLE_REAL_TIME_MARGIN_SECONDS
    started = time.monotonic()
    state = {}
    while time.monotonic() - started < real_timeout:
        state = run_snippet(POLL_SNIPPET, {"labels": labels})
        if "error" in state or not state["pie_running"]:
            return {"error": "정착 대기 중 PIE 가 사라졌다", "last_state": state}
        if state["game_seconds"] - start_seconds >= settle_seconds:
            return state
        time.sleep(POLL_INTERVAL_SECONDS)
    return {"error": "정착 대기 시간 초과", "last_state": state}


def read_capture_header(capture_directory, name):
    header_path = capture_directory / f"{name}.json"
    if not header_path.is_file():
        return None, f"포착 머리 파일 없음: {header_path}"
    try:
        header = json.loads(header_path.read_text(encoding="utf-8"))
    except ValueError as error:
        return None, f"포착 머리 파일이 JSON 이 아님: {error}"
    if not isinstance(header.get("channels"), list):
        return None, "포착 머리에 channels 배열이 없다"
    return header, None


def summarize_channels(header):
    return [{"name": channel.get("name"), "width": channel.get("width"), "height": channel.get("height"), "pixel_format": channel.get("pixel_format"), "size": channel.get("size"), "crc32": channel.get("crc32")} for channel in header["channels"]]


def check_backend(header, backend):
    value = header.get("backend")
    if value is None:
        return None, "포착 머리에 backend 키가 없어 백엔드를 확인하지 못했다"
    if str(value).lower().endswith(backend):
        return True, None
    return False, f"백엔드 불일치: 요청 {backend}, 포착 {value}"


def describe_capture(capture_directory, actor, backend):
    header, problem = read_capture_header(capture_directory, actor["name"])
    entry = {"label": actor["label"], "name": actor["name"], "capture_json": (capture_directory / f"{actor['name']}.json").as_posix(), "capture_bin": (capture_directory / f"{actor['name']}.bin").as_posix(), "problems": [], "warnings": []}
    if problem is not None:
        entry["problems"].append(problem)
        return entry
    backend_matches, backend_note = check_backend(header, backend)
    if backend_matches is False:
        entry["problems"].append(backend_note)
    if backend_matches is None:
        entry["warnings"].append(backend_note)
    entry["header_backend"] = header.get("backend")
    entry["resolution"] = header.get("resolution")
    entry["channels"] = summarize_channels(header)
    entry["total_bytes"] = sum(int(channel["size"] or 0) for channel in entry["channels"])
    if not entry["channels"]:
        entry["problems"].append("채널이 하나도 없다")
    if not entry["capture_bin"] or not Path(entry["capture_bin"]).is_file():
        entry["problems"].append("포착 바이너리 파일 없음")
    return entry


def run_capture_session(request, capture_directory, settle_seconds):
    labels = [actor["label"] for actor in request["actors"]]
    started = run_snippet(START_PIE_SNIPPET, {})
    if "error" in started:
        return {"error": "PIE 시작 요청 실패", "detail": started}
    try:
        ready = wait_for_pie(labels, PIE_START_TIMEOUT_SECONDS)
        if "error" in ready:
            return {"error": "PIE 준비 실패", "detail": ready}
        configured = run_snippet(CONFIGURE_SNIPPET, {"console": request["console"], "time_dilation": request["time_dilation"]})
        if "error" in configured:
            return {"error": "PIE 설정 실패", "detail": configured}
        settled = wait_for_settle(labels, configured["game_seconds"], settle_seconds, request["time_dilation"])
        if "error" in settled:
            return {"error": "정착 실패", "detail": settled}
        captured = run_snippet(CAPTURE_SNIPPET, {"captures": [{"label": actor["label"], "name": actor["name"]} for actor in request["actors"]], "output_directory": capture_directory.as_posix()})
        if "error" in captured:
            return {"error": "포착 실패", "detail": captured}
        return {"configured": configured, "captured": captured}
    finally:
        run_snippet(STOP_PIE_SNIPPET, {})
        ended = wait_for_pie_end(PIE_STOP_TIMEOUT_SECONDS)
        if not ended:
            print("경고: PIE 종료를 확인하지 못했다. 에디터에서 직접 끄세요", file=sys.stderr)


def collect_capture_entries(request, session, capture_directory):
    library_results = {item["name"]: item["library_result"] for item in session["captured"]["results"]}
    entries = []
    for actor in request["actors"]:
        entry = describe_capture(capture_directory, actor, request["backend"])
        library_result = library_results.get(actor["name"], {"ok": False, "error": "라이브러리 결과 없음"})
        entry["library_result"] = library_result
        if not library_result.get("ok"):
            entry["problems"].append(f"포착 라이브러리 실패: {library_result.get('error')}")
        entries.append(entry)
    return entries


def run_import(request, entries):
    good = [entry for entry in entries if not entry["problems"]]
    if not good:
        return None
    names = {actor["name"]: actor for actor in request["actors"]}
    imports = [{"label": entry["label"], "json_path": entry["capture_json"], "asset_name": names[entry["name"]]["asset_name"]} for entry in good]
    return run_snippet(IMPORT_SNIPPET, {"level": request["level"], "imports": imports, "package_path": request["package_path"], "overwrite": request["overwrite"], "assign": request["assign"], "save_level": request["save_level"]})


def find_import_problem(request, item):
    library_result = item.get("library_result", {})
    if not library_result.get("ok"):
        return f"DA 생성 실패: {library_result.get('error')}"
    if not item.get("asset_loaded") or not item.get("is_usable"):
        return "DA 를 다시 읽지 못했거나 IsUsable 이 거짓이다"
    if item.get("assign_error"):
        return item["assign_error"]
    if request["assign"] and not item.get("assigned"):
        return f"지정을 다시 읽은 값이 에셋과 다르다: '{item.get('assigned_read_back')}'"
    return None


def merge_import_result(request, entries, import_result):
    imported = {item["label"]: item for item in import_result.get("imports", [])}
    for entry in entries:
        item = imported.get(entry["label"])
        if item is None:
            continue
        entry["asset"] = item
        problem = find_import_problem(request, item)
        if problem is not None:
            entry["problems"].append(problem)


def compare_with_baseline(entries, baseline_path):
    if not baseline_path.is_file():
        return {"baseline": None, "note": "이전 보고(fluid_sim_cache.json)가 없어 비교하지 못했다"}
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    baseline_by_name = {item["name"]: item for item in baseline.get("actors", [])}
    comparisons = []
    for entry in entries:
        before = baseline_by_name.get(entry["name"])
        if before is None or "channels" not in entry:
            entry["problems"].append("이전 보고에 같은 이름이 없거나 재포착 채널이 없다")
            continue
        old = {channel["name"]: channel for channel in before.get("channels", [])}
        new = {channel["name"]: channel for channel in entry["channels"]}
        structure_equal = sorted(old) == sorted(new) and all(old[name]["size"] == new[name]["size"] and old[name]["pixel_format"] == new[name]["pixel_format"] for name in old)
        crc_equal = [name for name in new if name in old and old[name]["crc32"] == new[name]["crc32"]]
        comparison = {"name": entry["name"], "structure_equal": structure_equal, "crc_equal_channels": crc_equal, "channel_count": len(new)}
        comparisons.append(comparison)
        if not structure_equal:
            entry["problems"].append("재포착 채널 이름·크기·형식이 이전 보고와 다르다")
    return {"baseline": baseline_path.as_posix(), "comparisons": comparisons}


def write_report(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes((json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True, default=str) + "\n").encode("utf-8"))


def prepare_editor(request):
    labels = [actor["label"] for actor in request["actors"]]
    prepared = run_snippet(PREPARE_SNIPPET, {"level": request["level"], "labels": labels})
    if "error" in prepared:
        return None, prepared
    problems = [f"{entry['label']}: 액터 없음" if not entry["found"] else f"{entry['label']}: TDFluidLiveActor 가 아님" for entry in prepared["actors"] if not entry["found"] or not entry["is_live_actor"]]
    if problems:
        return None, {"error": "요청 액터를 찾지 못했다", "problems": problems}
    if request["assign"] and not request["overwrite"]:
        already = [entry["label"] for entry in prepared["actors"] if entry["existing_cache"]]
        if already:
            print("경고: 이미 SimStateCache 가 지정된 액터는 이번 에셋으로 지정이 바뀐다:", ", ".join(already))
    return prepared, None


def run(request):
    token = datetime.now().strftime("%Y%m%d-%H%M%S")
    capture_directory = CACHE_OUTPUT_ROOT / token
    report_path = VERIFY_REPORT_PATH if request["verify"] else REPORT_PATH
    report = {"date": date.today().isoformat(), "token": token, "request": request, "verify": request["verify"], "actors": []}
    prepared, failure = prepare_editor(request)
    if failure is not None:
        report["error"] = failure
        write_report(report_path, report)
        print("환경 문제:", json.dumps(failure, ensure_ascii=False)[:600], file=sys.stderr)
        return EXIT_ENVIRONMENT
    report["prepare"] = prepared
    settle_seconds = request["verify_settle_seconds"] if request["verify"] else request["settle_seconds"]
    session = run_capture_session(request, capture_directory, settle_seconds)
    if "error" in session:
        report["error"] = session
        write_report(report_path, report)
        print("환경 문제:", json.dumps(session, ensure_ascii=False)[:600], file=sys.stderr)
        return EXIT_ENVIRONMENT
    report["pie"] = {"configured": session["configured"], "game_seconds_at_capture": session["captured"]["game_seconds"], "capture_directory": capture_directory.as_posix()}
    entries = collect_capture_entries(request, session, capture_directory)
    if request["verify"]:
        report["comparison"] = compare_with_baseline(entries, REPORT_PATH)
    else:
        import_result = run_import(request, entries)
        if import_result is not None:
            if "error" in import_result:
                report["import_error"] = import_result
                for entry in entries:
                    entry["problems"].append(f"가져오기 단계 실패: {import_result['error']}")
            else:
                merge_import_result(request, entries, import_result)
                report["level_saved"] = import_result["level_saved"]
                report["level_dirty_unsaved"] = import_result["level_dirty_unsaved"]
    report["actors"] = entries
    failed = [entry for entry in entries if entry["problems"]]
    report["ok"] = not failed
    write_report(report_path, report)
    for entry in entries:
        print("실패" if entry["problems"] else "통과", entry["label"], entry["name"], "; ".join(entry["problems"] + entry["warnings"]))
    print("보고", report_path.as_posix())
    if report.get("level_dirty_unsaved"):
        print("주의: 지정은 메모리에만 있고 맵이 저장되지 않았다(save_level 이 거짓). 저장하지 않으면 지정이 사라진다")
    return EXIT_FAILED if failed else 0


def parse_arguments():
    parser = argparse.ArgumentParser(description="PIE 에서 유체 액터의 시뮬레이션 상태 캐시를 포착해 데이터 에셋으로 가져오고 지정한다(FX-16b O5, 6c)")
    parser.add_argument("--request", type=Path, default=DEFAULT_REQUEST_PATH)
    return parser.parse_args()


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    arguments = parse_arguments()
    request_path = arguments.request if arguments.request.is_absolute() else ROOT / arguments.request
    try:
        request = normalize_request(json.loads(request_path.read_text(encoding="utf-8")))
    except (OSError, ValueError) as error:
        print("요청 오류:", error, file=sys.stderr)
        return EXIT_ENVIRONMENT
    return run(request)


if __name__ == "__main__":
    sys.exit(main())

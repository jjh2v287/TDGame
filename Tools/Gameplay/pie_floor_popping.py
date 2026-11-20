"""실행 환경: 시스템 Python(PowerShell). 실제 측정은 에디터가 LV_DarkFantasy_OpenWorld 를 열고 있고 MCP 포트 8000·원격 실행이 켜져 있어야 하며, 에디터 창을 앞에 둔다. --dry-run 과 --offline 은 에디터가 필요 없다.
실행 명령: python -B Tools/Gameplay/pie_floor_popping.py --run <이름> [--route village_loop|teleport_spots|smoke] [--cvars "이름=값;이름=값"] [--speed 550] [--capture external|engine] [--grab-hz 12] [--shot-hz 8(engine 때만)] [--flush-seconds 1.5] [--prestream-before 초] [--prestream-tail 2] [--start-pie] [--stop-pie] [--no-analyze] [--overwrite] [--dry-run] [--offline]
출력: Saved/Gameplay/FloorPopping/<run>/ plan.json·request.json·probe_status.json·result.json·frames.csv·shots/shot_0000.png(보정 사진)·grabs/grab_NNNNN.png·grabs.csv·capture.json·profile.csv·gpu.csv, 분석기 산출물(analysis.json·summary.md·sheets). 종료 코드 0 성공, 1 실패, 2 PIE 없음·시작 실패
상태: GF-07. 기본 촬영은 엔진 밖 화면 복사(external, 엔진 Shot 은 장당 약 200ms 로 게임을 멈춰 걷는 동안 쓰지 않음, 보정 사진 1장만). 2026-11-20 스모크로 PIE 시작·탐침·CSV·GPU 수집 확인.
"""
import argparse
import array
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLS = os.path.join(ROOT, "Tools")
OUTPUT_BASE = os.path.join(ROOT, "Saved", "Gameplay", "FloorPopping")
ROUTES_PATH = os.path.join(TOOLS, "Gameplay", "floor_popping_routes.json")
PROBE_PATH = os.path.join(TOOLS, "Gameplay", "editor_floor_popping_probe.py")
ANALYZER_PATH = os.path.join(TOOLS, "Gameplay", "analyze_floor_popping.py")
RUN_IN_EDITOR_PATH = os.path.join(TOOLS, "run_in_editor.py")
CSV_PROFILE_DIRECTORY = os.path.join(ROOT, "Saved", "Profiling", "CSV")
ACTIVE_REQUEST_PATH = os.path.join(OUTPUT_BASE, "active_request.txt")
HEIGHT_PATH = os.path.join(ROOT, "Saved", "WorldGen", "AshenVale", "height.r16")
NVIDIA_SMI_FALLBACK = r"C:\Windows\System32\nvidia-smi.exe"
HEIGHT_SAMPLES = 1009
HEIGHT_CELL_CM = 100.0
HEIGHT_RAW_ZERO = 32768.0
HEIGHT_RAW_PER_100CM = 128.0
PAWN_HALF_HEIGHT_CM = 88.0
DEFAULT_CVARS = [["r.GPUCsvStatsEnabled", "1"]]
PIE_WARMUP_SECONDS = 20.0
PIE_START_TIMEOUT_SECONDS = 180.0
PIE_POLL_SECONDS = 5.0
STATUS_POLL_SECONDS = 1.0
TIMEOUT_FACTOR = 2.0
TIMEOUT_EXTRA_SECONDS = 60.0
SHOT_WAIT_SECONDS = 30.0
CSV_WAIT_SECONDS = 30.0
CSV_STABLE_SECONDS = 2.0
RUN_NAME_PATTERN = re.compile(r"^[A-Za-z0-9_-]+$")
CALIBRATION_SHOT_AT_SECONDS = 3.0
PRESTREAM_TAIL_SECONDS = 2.0
CALIBRATION_WAIT_SECONDS = 25.0
CALIBRATION_STABLE_SECONDS = 0.4
PIE_QUERY_CODE = "import unreal\nworld = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\nprint('PIE_WORLD', world is not None)\n"


def log(message):
    print(message, flush=True)


def load_height_values():
    if not os.path.exists(HEIGHT_PATH):
        raise FileNotFoundError(f"height map missing: {HEIGHT_PATH}")
    values = array.array("H")
    with open(HEIGHT_PATH, "rb") as handle:
        values.fromfile(handle, HEIGHT_SAMPLES * HEIGHT_SAMPLES)
    if sys.byteorder == "big":
        values.byteswap()
    return values


def raw_to_height_cm(raw):
    return (raw - HEIGHT_RAW_ZERO) / HEIGHT_RAW_PER_100CM * 100.0


def sample_height_bilinear_cm(values, x_cm, y_cm):
    half = (HEIGHT_SAMPLES - 1) / 2.0
    column = x_cm / HEIGHT_CELL_CM + half
    row = y_cm / HEIGHT_CELL_CM + half
    if not (0.0 <= column <= HEIGHT_SAMPLES - 1 and 0.0 <= row <= HEIGHT_SAMPLES - 1):
        raise ValueError(f"point outside height map: ({x_cm}, {y_cm})")
    column0 = min(int(math.floor(column)), HEIGHT_SAMPLES - 2)
    row0 = min(int(math.floor(row)), HEIGHT_SAMPLES - 2)
    fraction_x = column - column0
    fraction_y = row - row0
    base = row0 * HEIGHT_SAMPLES + column0
    top = raw_to_height_cm(values[base]) * (1 - fraction_x) + raw_to_height_cm(values[base + 1]) * fraction_x
    bottom = raw_to_height_cm(values[base + HEIGHT_SAMPLES]) * (1 - fraction_x) + raw_to_height_cm(values[base + HEIGHT_SAMPLES + 1]) * fraction_x
    return top * (1 - fraction_y) + bottom * fraction_y


def pawn_point(values, x_cm, y_cm):
    return [round(float(x_cm), 2), round(float(y_cm), 2), round(sample_height_bilinear_cm(values, x_cm, y_cm) + PAWN_HALF_HEIGHT_CM, 2)]


def build_glide_points(values, start, end, spacing_cm):
    length = math.hypot(end[0] - start[0], end[1] - start[1])
    count = max(1, int(math.ceil(length / spacing_cm)))
    points = []
    for index in range(count + 1):
        fraction = index / count
        points.append(pawn_point(values, start[0] + (end[0] - start[0]) * fraction, start[1] + (end[1] - start[1]) * fraction))
    return points, length


def build_segments(values, route, defaults, speed_cm_s):
    segments = []
    position_xy = list(route["start"])
    for index, definition in enumerate(route["segments"]):
        kind = definition["kind"]
        segment = {"index": index, "kind": kind}
        if kind == "glide":
            end_xy = list(definition["to"])
            points, length = build_glide_points(values, position_xy, end_xy, defaults["point_spacing_cm"])
            segment.update(points=points, length_cm=length, expected_seconds=length / speed_cm_s, start=points[0], end=points[-1])
            position_xy = end_xy
        else:
            if kind == "teleport":
                position_xy = list(definition["to"])
            position = pawn_point(values, position_xy[0], position_xy[1])
            seconds = float(definition.get("seconds", 0.0))
            segment.update(position=position, length_cm=0.0, expected_seconds=seconds, seconds=seconds, start=position, end=position)
            if kind == "stare":
                segment["shot_times"] = list(definition["shot_times"])
        if definition.get("console"):
            segment["console"] = list(definition["console"])
        for flag in ("free", "kill_player"):
            if definition.get(flag):
                segment[flag] = True
        segments.append(segment)
    return segments


def describe_segment_row(segment):
    zs = [point[2] for point in segment["points"]] if segment["kind"] == "glide" else [segment["position"][2]]
    return f"{segment['index']:>3} {segment['kind']:<9} {segment['length_cm'] / 100.0:>8.1f} {segment['expected_seconds']:>9.1f}   {min(zs):>9.1f} .. {max(zs):<9.1f}"


def print_segment_table(segments):
    log("구간 종류      길이(m)  예상(초)      z 범위(cm)")
    for segment in segments:
        log(describe_segment_row(segment))
    log(f"합계 예상 {sum(segment['expected_seconds'] for segment in segments):.1f} 초")


def parse_cvars(text):
    pairs = []
    for item in [part.strip() for part in text.split(";") if part.strip()]:
        name, separator, value = item.partition("=")
        if not separator or not name.strip():
            raise ValueError(f"bad cvar item: {item}")
        pairs.append([name.strip(), value.strip()])
    return pairs


def write_json(path, value):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, ensure_ascii=False, indent=1)


def read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def prepare_run_directory(run_dir, overwrite):
    if os.path.exists(run_dir):
        if not overwrite:
            raise FileExistsError(f"run folder exists (use --overwrite): {run_dir}")
        if os.path.dirname(os.path.abspath(run_dir)) != os.path.abspath(OUTPUT_BASE):
            raise ValueError(f"refusing to clear outside output base: {run_dir}")
        shutil.rmtree(run_dir)
    os.makedirs(os.path.join(run_dir, "shots"))


def apply_external_capture_schedule(segments):
    first_settle = next((segment for segment in segments if segment["kind"] == "settle"), None)
    if first_settle is not None:
        first_settle["shot_at"] = min(CALIBRATION_SHOT_AT_SECONDS, max(0.5, first_settle["seconds"] * 0.5))
    for segment in segments:
        if segment["kind"] == "stare":
            segment["shot_times"] = []


def apply_teleport_options(segments, teleport_api, prestream_seconds, prestream_tail_seconds):
    for position, segment in enumerate(segments):
        if segment["kind"] != "teleport":
            continue
        segment["api"] = teleport_api
        if prestream_seconds <= 0.0 or position == 0:
            continue
        before = segments[position - 1]
        if before["kind"] in ("stare", "settle"):
            before["prestream_next"] = {"position": segment["position"], "seconds": prestream_seconds + prestream_tail_seconds}


def build_plan_and_request(args, run_dir):
    routes = read_json(ROUTES_PATH)
    if args.route not in routes["routes"]:
        raise ValueError(f"unknown route: {args.route}")
    defaults = routes["defaults"]
    values = load_height_values()
    segments = build_segments(values, routes["routes"][args.route], defaults, args.speed)
    slash_dir = run_dir.replace("\\", "/")
    if " " in slash_dir:
        raise ValueError(f"run folder path has a space: {slash_dir}")
    if args.capture == "external":
        apply_external_capture_schedule(segments)
    apply_teleport_options(segments, args.teleport_api, args.prestream_before, args.prestream_tail)
    request = {
        "request_id": uuid.uuid4().hex, "run_dir": slash_dir, "cvars": parse_cvars(args.cvars),
        "speed_cm_s": args.speed, "shot_hz": 0.0 if args.capture == "external" else args.shot_hz, "segments": segments, "default_cvars": DEFAULT_CVARS,
        "flush_streaming_seconds": args.flush_seconds, "drive": args.drive,
    }
    plan = {
        "run": args.run, "route": args.route, "request_id": request["request_id"], "speed_cm_s": args.speed, "shot_hz": args.shot_hz,
        "capture": args.capture, "grab_hz": args.grab_hz, "drive": args.drive, "teleport_api": args.teleport_api, "prestream_before": args.prestream_before, "prestream_tail": args.prestream_tail,
        "cvars": request["cvars"], "default_cvars": DEFAULT_CVARS, "flush_streaming_seconds": args.flush_seconds,
        "expected_seconds": sum(segment["expected_seconds"] for segment in segments),
        "segments": [{key: value for key, value in segment.items() if key != "points"} for segment in segments],
        "created_epoch": time.time(), "gpu_start_epoch": None,
    }
    return plan, request, segments


def pie_tool(tool):
    import uemcp
    session = uemcp.connect()
    arguments = {"options": {"bSimulate": False}} if tool == "StartPIE" else {}
    return uemcp.call_tool(session, "call_tool", {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": tool, "arguments": arguments})


def is_pie_running():
    import run_in_editor
    result = run_in_editor.run_code(PIE_QUERY_CODE)
    if not result["connected"]:
        return None
    return any("PIE_WORLD True" in line for line in result["lines"])


def wait_for_pie(timeout_seconds):
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if is_pie_running():
            return True
        time.sleep(PIE_POLL_SECONDS)
    return False


def ensure_pie(start_pie):
    state = is_pie_running()
    if state is None:
        log("에디터 연결 실패")
        return False
    if state:
        return True
    if not start_pie:
        log("PIE 가 돌고 있지 않음 (--start-pie 로 시작 가능)")
        return False
    pie_tool("StartPIE")
    if not wait_for_pie(PIE_START_TIMEOUT_SECONDS):
        log("PIE 시작 시간 초과")
        return False
    log(f"PIE 시작됨, {PIE_WARMUP_SECONDS:.0f} 초 데움")
    time.sleep(PIE_WARMUP_SECONDS)
    return True


def find_nvidia_smi():
    return shutil.which("nvidia-smi") or (NVIDIA_SMI_FALLBACK if os.path.exists(NVIDIA_SMI_FALLBACK) else None)


def start_gpu_logger(run_dir):
    executable = find_nvidia_smi()
    if executable is None:
        log("경고: nvidia-smi 없음, gpu.csv 생략")
        return None, None
    handle = open(os.path.join(run_dir, "gpu.csv"), "w", encoding="utf-8", newline="")
    command = [executable, "--query-gpu=timestamp,memory.used,utilization.gpu", "--format=csv,noheader,nounits", "-lms", "250"]
    return subprocess.Popen(command, stdout=handle, stderr=subprocess.DEVNULL), handle


def stop_gpu_logger(process, handle):
    if process is not None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
    if handle is not None:
        handle.close()


def run_probe_in_editor():
    result = subprocess.run([sys.executable, RUN_IN_EDITOR_PATH, PROBE_PATH], capture_output=True, text=True, encoding="utf-8", errors="replace")
    text = "\n".join(line.strip() for line in result.stdout.splitlines() if line.strip())
    return result.returncode, text


def write_active_request(text):
    os.makedirs(OUTPUT_BASE, exist_ok=True)
    with open(ACTIVE_REQUEST_PATH, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text + "\n")


def read_status(run_dir, request_id):
    path = os.path.join(run_dir, "probe_status.json")
    if not os.path.exists(path):
        return None
    try:
        status = read_json(path)
    except (ValueError, OSError):
        return None
    return status if status.get("request_id") == request_id else None


def wait_for_probe(run_dir, request_id, timeout_seconds):
    deadline = time.time() + timeout_seconds
    last_report = 0.0
    while time.time() < deadline:
        status = read_status(run_dir, request_id)
        if status is not None and status["state"] in ("complete", "failed"):
            return status
        if status is not None and time.time() - last_report > 15.0:
            last_report = time.time()
            log(f"  진행: 구간 {status.get('seg')} 사진 {status.get('shots')} 행 {status.get('rows')}")
        time.sleep(STATUS_POLL_SECONDS)
    return None


def abort_probe(request_path):
    write_active_request("ABORT")
    run_probe_in_editor()
    write_active_request(request_path)


def list_csv_files():
    if not os.path.isdir(CSV_PROFILE_DIRECTORY):
        return []
    return [os.path.join(CSV_PROFILE_DIRECTORY, name) for name in os.listdir(CSV_PROFILE_DIRECTORY) if name.lower().endswith(".csv")]


def collect_profile_csv(run_dir, since_epoch):
    deadline = time.time() + CSV_WAIT_SECONDS
    while time.time() < deadline:
        candidates = [path for path in list_csv_files() if os.path.getmtime(path) >= since_epoch]
        if candidates:
            newest = max(candidates, key=os.path.getmtime)
            size = os.path.getsize(newest)
            time.sleep(CSV_STABLE_SECONDS)
            if os.path.getsize(newest) == size and size > 0:
                shutil.copyfile(newest, os.path.join(run_dir, "profile.csv"))
                return newest
        time.sleep(1.0)
    return None


def wait_for_shot_files(run_dir):
    result_path = os.path.join(run_dir, "result.json")
    if not os.path.exists(result_path):
        return None
    result = read_json(result_path)
    paths = [shot["path"] for shot in result.get("shots", [])]
    deadline = time.time() + SHOT_WAIT_SECONDS
    missing = paths
    while time.time() < deadline:
        missing = [path for path in paths if not (os.path.exists(path) and os.path.getsize(path) > 0)]
        if not missing:
            break
        time.sleep(1.0)
    result["missing"] = missing
    write_json(result_path, result)
    return missing


def run_analyzer(run_dir):
    if not os.path.exists(ANALYZER_PATH):
        log(f"경고: 분석기 파일이 없어 건너뜀: {ANALYZER_PATH}")
        return 0
    return subprocess.call([sys.executable, "-B", ANALYZER_PATH, "--run", run_dir])


def wait_for_stable_file(path, timeout_seconds):
    deadline = time.time() + timeout_seconds
    previous_size = -1
    while time.time() < deadline:
        size = os.path.getsize(path) if os.path.exists(path) else -1
        if size > 0 and size == previous_size:
            return True
        previous_size = size
        time.sleep(CALIBRATION_STABLE_SECONDS)
    return False


def start_external_capture(run_dir, grab_hz):
    import floor_popping_capture as capture
    calibration_path = os.path.join(run_dir, "shots", "shot_0000.png")
    if not wait_for_stable_file(calibration_path, CALIBRATION_WAIT_SECONDS):
        return None, {"ok": False, "reason": "calibration shot missing"}
    location = capture.locate_viewport(calibration_path)
    if not location.get("ok"):
        return None, location
    session = capture.CaptureSession(run_dir, location, grab_hz)
    session.start()
    return session, location


def finish_external_capture(run_dir, session, location):
    import floor_popping_capture as capture
    report = {"location": location, "stats": session.stop() if session is not None else None}
    capture.write_capture_report(run_dir, report)
    return report


def run_cleanup_step(label, action):
    try:
        action()
    except Exception as error:
        log(f"정리 실패({label}): {error!r}")


def execute_live_run(args, run_dir, plan, request, request_path):
    if not ensure_pie(args.start_pie):
        return 2
    write_active_request(request_path.replace("\\", "/"))
    start_epoch = time.time()
    plan["gpu_start_epoch"] = start_epoch
    write_json(os.path.join(run_dir, "plan.json"), plan)
    gpu_process, gpu_handle = start_gpu_logger(run_dir)
    session, location = None, None
    exit_code = 1
    try:
        code, text = run_probe_in_editor()
        if code != 0:
            log(f"탐침 실행 실패(코드 {code}): {text[-400:]}")
            return 1
        if args.capture == "external":
            session, location = start_external_capture(run_dir, args.grab_hz)
            log(f"뷰포트 보정: {json.dumps(location, default=str)[:300]}")
            if session is None:
                log("뷰포트를 찾지 못해 중단(에디터 창을 앞에 두고 다시 실행)")
                abort_probe(request_path.replace("\\", "/"))
                return 1
        timeout_seconds = plan["expected_seconds"] * TIMEOUT_FACTOR + TIMEOUT_EXTRA_SECONDS
        status = wait_for_probe(run_dir, request["request_id"], timeout_seconds)
        if status is None:
            log("탐침 시간 초과, 중단 요청")
            abort_probe(request_path.replace("\\", "/"))
            return 1
        if status["state"] != "complete":
            log(f"탐침 실패: {status.get('errors')}")
            return 1
        exit_code = 0
    finally:
        run_cleanup_step("GPU 기록 정지", lambda: stop_gpu_logger(gpu_process, gpu_handle))
        if args.capture == "external" and location is not None:
            run_cleanup_step("화면 복사 마무리", lambda: log(f"화면 복사: {json.dumps(finish_external_capture(run_dir, session, location).get('stats'), default=str)[:300]}"))
        if args.stop_pie:
            run_cleanup_step("PIE 종료", lambda: pie_tool("StopPIE"))
    csv_source = collect_profile_csv(run_dir, start_epoch)
    log(f"프로파일러 CSV: {csv_source or '없음(경고)'}")
    missing = wait_for_shot_files(run_dir)
    log(f"사진 누락: {0 if missing is None else len(missing)}")
    return exit_code


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True)
    parser.add_argument("--route", default="village_loop", choices=tuple(read_json(ROUTES_PATH)["routes"]))
    parser.add_argument("--cvars", default="")
    parser.add_argument("--speed", type=float, default=550.0)
    parser.add_argument("--shot-hz", type=float, default=8.0)
    parser.add_argument("--flush-seconds", type=float, default=1.5)
    parser.add_argument("--capture", default="external", choices=("external", "engine"))
    parser.add_argument("--drive", default="teleport", choices=("teleport", "input"))
    parser.add_argument("--teleport-api", default="set", choices=("set", "game"))
    parser.add_argument("--prestream-before", type=float, default=0.0)
    parser.add_argument("--prestream-tail", type=float, default=PRESTREAM_TAIL_SECONDS)
    parser.add_argument("--grab-hz", type=float, default=12.0)
    parser.add_argument("--start-pie", action="store_true")
    parser.add_argument("--stop-pie", action="store_true")
    parser.add_argument("--no-analyze", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--offline", action="store_true")
    return parser.parse_args()


def main():
    args = parse_arguments()
    sys.path.insert(0, TOOLS)
    sys.path.insert(0, os.path.join(TOOLS, "Gameplay"))
    if args.capture == "external" and not args.dry_run and not args.offline:
        import floor_popping_capture
        floor_popping_capture.make_process_dpi_aware()
    if not RUN_NAME_PATTERN.match(args.run):
        log("--run 은 영문·숫자·_·- 만")
        return 1
    run_dir = os.path.join(OUTPUT_BASE, args.run)
    if args.offline:
        if not os.path.isdir(run_dir):
            log(f"run 폴더 없음: {run_dir}")
            return 1
        return run_analyzer(run_dir)
    try:
        prepare_run_directory(run_dir, args.overwrite)
        plan, request, segments = build_plan_and_request(args, run_dir)
    except (FileExistsError, ValueError, FileNotFoundError) as error:
        log(f"실패: {error}")
        return 1
    request_path = os.path.join(run_dir, "request.json")
    write_json(os.path.join(run_dir, "plan.json"), plan)
    write_json(request_path, request)
    print_segment_table(segments)
    if args.dry_run:
        log(f"dry-run 완료: {run_dir}")
        return 0
    exit_code = execute_live_run(args, run_dir, plan, request, request_path)
    if exit_code != 0:
        return exit_code
    if args.no_analyze:
        return 0
    return 0 if run_analyzer(run_dir) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

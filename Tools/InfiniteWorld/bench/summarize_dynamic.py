"""
실행 환경: Python 3.10+ (표준 라이브러리만)
실행 명령: python Tools/InfiniteWorld/bench/summarize_dynamic.py [결과 폴더 기본=Saved/AgentOps/IWBench/results] [로그 폴더 기본=Saved/AgentOps/IWBench/logs]
출력: 표준 출력에 마크다운 표(stream·nav·nav.setup·late_bounds·로그 오류 집계), 두 회차 값은 'r1 / r2'
상태: 실험(2026-10-23 무한 세계 벤치 2단계)
"""
import glob
import json
import os
import re
import sys

BENCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", *"Saved/AgentOps/IWBench".split("/"))
RESULTS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BENCH, "results")
LOGS = sys.argv[2] if len(sys.argv) > 2 else os.path.join(BENCH, "logs")


def load(pattern):
    runs = {}
    for path in sorted(glob.glob(os.path.join(RESULTS, pattern))):
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        records = data["results"] if isinstance(data, dict) else data
        runs[os.path.basename(path)[:-5]] = (data, records)
    return runs


def fmt(value, digits=2):
    if value is None:
        return "-"
    if isinstance(value, float):
        if abs(value) >= 100:
            return f"{value:.0f}"
        return f"{value:.{digits}f}"
    return str(value)


def dist(record, key, field):
    obj = record.get(key) or {}
    return obj.get(field)


def pair(values, digits=2):
    return " / ".join(fmt(v, digits) for v in values)


def collect(runs, name, rep_of, key_of):
    table = {}
    for run_name, (_, records) in runs.items():
        rep = rep_of(run_name)
        for record in records:
            if record.get("name") != name:
                continue
            table.setdefault(key_of(run_name, record), {})[rep] = record
    return table


def rep_from(run_name):
    return run_name.rsplit("-", 1)[-1]


def stream_tables():
    runs = {k: v for k, v in load("stream-*.json").items() if re.search(r"-(A|B)-r\d$|-render$", k)}
    table = collect(runs, "stream.case", rep_from, lambda run, rec: (("render-" if run.endswith("render") else "") + rec["case"]))
    order = ["x6", "d6", "x15", "d15", "x50", "d50", "f6", "render-x15", "render-d50"]
    print("### stream: 프레임·관리자 게임 스레드 시간 (ms, r1 / r2)\n")
    print("| 경우 | 관리자 p50 | 관리자 p99 | 관리자 최대 | 프레임 CPU p50 | 프레임 CPU p99 | 프레임 CPU 최대 | 프레임 간격 p99 | 프레임 수 |")
    print("|---|---|---|---|---|---|---|---|---|")
    for case in order:
        reps = table.get(case)
        if not reps:
            continue
        keys = sorted(reps)
        row = [case]
        for key, field in [("manager_ms", "p50"), ("manager_ms", "p99"), ("manager_ms", "max"), ("frame_cpu_ms", "p50"), ("frame_cpu_ms", "p99"), ("frame_cpu_ms", "max"), ("frame_delta_ms", "p99")]:
            row.append(pair([dist(reps[k], key, field) for k in keys], 3 if field == "p50" and key == "manager_ms" else 2))
        row.append(pair([reps[k].get("frames") for k in keys]))
        print("| " + " | ".join(row) + " |")
    print("\n### stream: 대기열·준비·지연 (r1 / r2)\n")
    print("| 경우 | 게임 스레드 대기열 최대(작업 수) | 작업 스레드 동시 최대 | 미준비 프레임 | 미준비 구간 | 요청→충돌 p50 ms | 요청→충돌 p95 | 요청→충돌 최대 | 요청→인스턴스 완료 p50 | 요청→인스턴스 최대 | 요청 청크 | 해제 청크 | 사전 채움 s |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for case in order:
        reps = table.get(case)
        if not reps:
            continue
        keys = sorted(reps)
        row = [case]
        row.append(pair([reps[k].get("max_game_thread_queue_items") for k in keys]))
        row.append(pair([reps[k].get("max_worker_in_flight") for k in keys]))
        row.append(pair([reps[k].get("unready_frames") for k in keys]))
        row.append(pair([reps[k].get("unready_episodes") for k in keys]))
        for key, field in [("latency_request_to_collision_ms", "p50"), ("latency_request_to_collision_ms", "p95"), ("latency_request_to_collision_ms", "max"), ("latency_request_to_instances_ms", "p50"), ("latency_request_to_instances_ms", "max")]:
            row.append(pair([dist(reps[k], key, field) for k in keys], 0))
        row.append(pair([reps[k].get("chunks_requested") for k in keys]))
        row.append(pair([reps[k].get("chunks_released") for k in keys]))
        row.append(pair([reps[k].get("prefill_s") for k in keys]))
        print("| " + " | ".join(row) + " |")
    print("\n### stream: 게임 스레드 작업 단위 비용 (ms, r1 / r2)\n")
    print("| 경우 | 지형 생성 p50 | 지형 최대 | ISM 128개 p50 | ISM 128개 최대 | 해제 p50 | 해제 최대 | 작업 스레드 빌드 p50 | 요청→빌드 완료 p50 |")
    print("|---|---|---|---|---|---|---|---|---|")
    for case in order:
        reps = table.get(case)
        if not reps:
            continue
        keys = sorted(reps)
        row = [case]
        for key, field in [("item_terrain_ms", "p50"), ("item_terrain_ms", "max"), ("item_batch128_ms", "p50"), ("item_batch128_ms", "max"), ("item_release_ms", "p50"), ("item_release_ms", "max"), ("worker_build_ms", "p50"), ("latency_request_to_build_ms", "p50")]:
            row.append(pair([dist(reps[k], key, field) for k in keys]))
        print("| " + " | ".join(row) + " |")


def nav_tables():
    runs = {k: v for k, v in load("nav-T*.json").items()}
    tile_of = lambda run: run.split("-")[1]
    setup = collect(runs, "nav.setup", rep_from, lambda run, rec: tile_of(run))
    print("\n### nav: 설정 확인 (r1 / r2)\n")
    print("| 요청 TileSizeUU | 실제 타일 크기 | 고정 풀 | 풀 크기 | 런타임 생성 | 등록 경계 | 내비 메시 생성 s | 설정 중 최대 프레임 ms |")
    print("|---|---|---|---|---|---|---|---|")
    for tile in sorted(setup, key=lambda t: int(t[1:])):
        reps = setup[tile]
        keys = sorted(reps)
        first = reps[keys[0]]
        print(f"| {tile[1:]} | {pair([reps[k].get('tile_size_uu') for k in keys])} | {first.get('fixed_tile_pool')} | {first.get('tile_pool_size')} | {first.get('runtime_generation')} | {first.get('registered_bounds_box', '')[:90]} | {pair([reps[k].get('navmesh_spawn_s') for k in keys], 3)} | {pair([reps[k].get('setup_max_frame_cpu_ms') for k in keys])} |")
    cases = collect(runs, "nav.case", rep_from, lambda run, rec: (tile_of(run), rec["case"]))
    print("\n### nav: 타일·경로·지연 (r1 / r2)\n")
    print("| 타일 | 경우 | 예열 s | 예열 타일 | 이동 중 생성 타일 | 초당 타일 p50 | 초당 타일 최대 | 활성 타일 최대 | 경로 완전/시도 | 부분 | 지면없음 | 투영 실패(시작/끝) | 탐색 실패 | 요청→내비 p50 ms | 요청→내비 최대 | 범위·충돌 준비→내비 p50 | 같은 최대 | 진입 즉시 준비 | 내비 메모리 최대 KB |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for key in sorted(cases, key=lambda k: (int(k[0][1:]), k[1])):
        reps = cases[key]
        keys = sorted(reps)
        get = lambda field: pair([reps[k].get(field) for k in keys])
        row = [key[0][1:], key[1], get("nav_warm_s"), get("nav_warm_active_tiles"), get("tiles_built_during_move")]
        row.append(pair([dist(reps[k], "tiles_built_per_s", "p50") for k in keys], 0))
        row.append(pair([dist(reps[k], "tiles_built_per_s", "max") for k in keys], 0))
        row.append(pair([dist(reps[k], "active_tiles", "max") for k in keys], 0))
        row.append(" / ".join(f"{reps[k].get('path_complete')}/{reps[k].get('path_attempts')}" for k in keys))
        row.append(get("path_partial"))
        row.append(get("path_no_ground"))
        row.append(" / ".join(f"{reps[k].get('path_project_start_fail')},{reps[k].get('path_project_end_fail')}" for k in keys))
        row.append(get("path_search_fail"))
        for dkey, field in [("latency_request_to_nav_ms", "p50"), ("latency_request_to_nav_ms", "max"), ("latency_ready_in_range_to_nav_ms", "p50"), ("latency_ready_in_range_to_nav_ms", "max")]:
            row.append(pair([dist(reps[k], dkey, field) for k in keys], 0))
        row.append(get("nav_already_at_range_entry"))
        row.append(pair([(reps[k].get("nav_memory_max_bytes") or 0) / 1024.0 for k in keys], 0))
        print("| " + " | ".join(row) + " |")
    print("\n### nav: 게임 스레드 (ms, r1 / r2)\n")
    print("| 타일 | 경우 | 관리자 p99 | 관리자 최대 | 프레임 CPU p50 | 프레임 CPU p99 | 프레임 CPU 최대 | 내비 갱신 호출 p50 | 내비 갱신 최대 | 경로 질의 p50 | 경로 질의 최대 | 미준비 프레임 |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    for key in sorted(cases, key=lambda k: (int(k[0][1:]), k[1])):
        reps = cases[key]
        keys = sorted(reps)
        row = [key[0][1:], key[1]]
        for dkey, field in [("manager_ms", "p99"), ("manager_ms", "max"), ("frame_cpu_ms", "p50"), ("frame_cpu_ms", "p99"), ("frame_cpu_ms", "max"), ("nav_update_component_ms", "p50"), ("nav_update_component_ms", "max"), ("path_query_ms", "p50"), ("path_query_ms", "max")]:
            row.append(pair([dist(reps[k], dkey, field) for k in keys], 3 if field == "p50" else 2))
        row.append(pair([reps[k].get("unready_frames") for k in keys]))
        print("| " + " | ".join(row) + " |")
    late = collect(runs, "nav.late_bounds", rep_from, lambda run, rec: tile_of(run))
    print("\n### nav: 인보커 활성 뒤 경계 추가(±1e6cm) 비용 (r1 / r2)\n")
    print("| 타일 | 경계 안 타일 수 | 활성 타일 | 기준 최대 프레임 ms | 추가 뒤 최대 프레임 ms | ns/타일 | ±1.1e8 경계 외삽(초) |")
    print("|---|---|---|---|---|---|---|")
    for tile in sorted(late, key=lambda t: int(t[1:])):
        reps = late[tile]
        keys = sorted(reps)
        tiles = reps[keys[0]].get("tiles_in_box")
        per_tile = [((reps[k].get("max_frame_cpu_ms") or 0) - (reps[k].get("baseline_max_frame_cpu_ms") or 0)) * 1e6 / tiles for k in keys]
        full_tiles = (2 * 1.1e8 / reps[keys[0]].get("tile_size_uu")) ** 2
        extrapolated = [p * full_tiles / 1e9 for p in per_tile]
        print(f"| {tile[1:]} | {tiles:.3g} | {pair([reps[k].get('active_tiles_at_spawn') for k in keys])} | {pair([reps[k].get('baseline_max_frame_cpu_ms') for k in keys])} | {pair([reps[k].get('max_frame_cpu_ms') for k in keys])} | {pair(per_tile)} | {pair(extrapolated, 0)} |")


def log_scan():
    patterns = {
        "bounds_too_large": r"Navmesh bounds are too large",
        "indices_too_large": r"too large to fit in an int32",
        "nav_error": r"LogNavigation: Error",
        "nav_warning_non_mem": r"LogNavigation: Warning: (?!RecastNavMesh-Default: ARecastNavMesh:)",
        "any_error": r"\bError: ",
        "ensure": r"Ensure condition failed|LogOutputDevice: Error",
    }
    print("\n### 로그 오류 집계 (줄 수)\n")
    print("| 로그 | " + " | ".join(patterns) + " | 첫 일반 Error 줄 |")
    print("|---|" + "---|" * (len(patterns) + 1))
    for path in sorted(glob.glob(os.path.join(LOGS, "stream-*.log")) + glob.glob(os.path.join(LOGS, "nav-T*.log"))):
        if "backup" in path or "smoke" in path:
            continue
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        counts = [str(len(re.findall(p, text))) for p in patterns.values()]
        first = re.search(r"^.*\bError: .*$", text, re.M)
        first_text = first.group(0)[-140:].replace("|", "/") if first else ""
        print(f"| {os.path.basename(path)} | " + " | ".join(counts) + f" | {first_text} |")


if __name__ == "__main__":
    stream_tables()
    nav_tables()
    log_scan()

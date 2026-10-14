"""에디터 밖에서 실행: 이계 통로 관문마다 PIE 폰을 실제 이동 입력으로 들어가기→되돌아오기 왕복 걷고, 이음매 통과·분위기 진행도·속도와 프레임 시간(평균·95백분위·최대·끊김 횟수와 그 통로 좌표)을 요약한다(P3-17).
실행: python Tools/WorldGen/pie_walk_passages.py [--run 이름] [--gates SunkenStarfieldSouthwest,PaleHeights] [--speed 550] [--hitch-ms 100] [--overwrite] (PowerShell, 에디터 실행 중이고 다른 갈래가 에디터를 쓰지 않을 때)
출력: Saved/WorldGen/PieWalk/<run>/ 의 <관문 ID>_in.json·<관문 ID>_out.json(원 결과)과 summary.md. 종료 코드 0 = 모든 걷기에 결과 있음, 1 = 결과 없거나 오류인 걷기 있음, 2 = PIE 시작 실패
상태: 현행 (2026-10-14)

절차: StartPIE → 관문마다 [필드의 통로 입구 안쪽 400cm 지점에 폰 배치 → 관문 방향으로 걷기(in) → 같은 자리에서 반대 방향으로 걷기(out)] → StopPIE. 걷는 시간 = (접근 길이 + 이탈 길이) / --speed + 6초. 에디터 쪽 동작은 editor_pie_probe.py의 walk.
프레임 시간: frame_ms = 월드 실시간(GetRealTimeSeconds) 틱 간 차이(주 값), slate_ms = 슬레이트 틱 인수(교차 확인). 에디터 창이 백그라운드면 스로틀(L-editor-06)로 프레임이 300ms 안팎이 되어 끊김으로 잡힌다. 통로 좌표 = 통로 필드 쪽 입구에서 걸은 거리(cm), 이음매를 넘으면 평행이동하므로 월드 좌표와 따로 적는다.
"""
import argparse
import glob
import math
import os
import sys
import time

WORLDGEN = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, WORLDGEN)
import pie_capture_spots as spots  # noqa: E402

WALK_ROOT = os.path.join(spots.ROOT, "Saved", "WorldGen", "PieWalk")
REALM_LAYOUTS = os.path.join(spots.ROOT, "Saved", "WorldGen", "Realms", "*", "layout.json")
ENTRANCE_INSET_CM = 400.0
EXTRA_WALK_SECONDS = 6.0
START_CLEARANCE_CM = 150.0
RESULT_MARGIN_SECONDS = 60.0
P95_FRACTION = 0.95


def load_gates():
    gates = []
    for layout_path in sorted(glob.glob(REALM_LAYOUTS)):
        realm = spots.read_json(layout_path)["realm"]
        for gate in realm.get("gates") or []:
            gates.append((realm["realm_id"], gate))
    return gates


def select_gates(gates, gates_text):
    tokens = spots.split_tokens(gates_text)
    unknown = [token for token in tokens if not any(gate["gate_id"].startswith(token) for _, gate in gates)]
    if unknown:
        raise ValueError(f"unknown gates: {unknown}")
    selected = [(realm_id, gate) for realm_id, gate in gates if not tokens or any(gate["gate_id"].startswith(token) for token in tokens)]
    if not selected:
        raise ValueError("no gates selected")
    return selected


def build_plan(realm_id, gate, ground_cm, speed_cm_per_second):
    yaw_degrees = gate["arrival"]["rot"][1]
    direction_x = math.cos(math.radians(yaw_degrees))
    direction_y = math.sin(math.radians(yaw_degrees))
    field_return = gate["field_return"]["loc"]
    approach_cm = float(gate["approach_length_cm"])
    departure_cm = float(gate["departure_length_cm"])
    start_back_cm = approach_cm - ENTRANCE_INSET_CM
    start_x = field_return[0] - direction_x * start_back_cm
    start_y = field_return[1] - direction_y * start_back_cm
    ground = ground_cm("field", start_x, start_y)
    return {
        "gate_id": gate["gate_id"],
        "realm_id": realm_id,
        "yaw": yaw_degrees,
        "start": [start_x, start_y, None if ground is None else ground + START_CLEARANCE_CM],
        "seconds": (approach_cm + departure_cm) / speed_cm_per_second + EXTRA_WALK_SECONDS,
        "start_passage_cm": ENTRANCE_INSET_CM,
        "approach_cm": approach_cm,
        "departure_cm": departure_cm,
    }


def walk_request(plan, forward, hitch_ms):
    return {
        "action": "walk",
        "gate_id": plan["gate_id"],
        "realm_id": plan["realm_id"],
        "yaw": plan["yaw"],
        "start": plan["start"] if forward else None,
        "seconds": plan["seconds"],
        "forward": forward,
        "hitch_ms": hitch_ms,
    }


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def frame_stats(frame_ms, hitch_ms):
    if not frame_ms:
        return None
    return {
        "samples": len(frame_ms),
        "mean_ms": round(sum(frame_ms) / len(frame_ms), 1),
        "p95_ms": round(percentile(frame_ms, P95_FRACTION), 1),
        "max_ms": round(max(frame_ms), 1),
        "over_hitch": sum(1 for value in frame_ms if value > hitch_ms),
    }


def median(values):
    if not values:
        return None
    return sorted(values)[len(values) // 2]


def summarize_walk(gate_id, direction, result, start_passage_cm, hitch_ms):
    summary = {"gate_id": gate_id, "direction": direction, "error": None, "hitches": []}
    if result is None:
        summary["error"] = "no result"
        return summary
    if not result.get("ok") or not result.get("frames"):
        summary["error"] = result.get("error") or "no frames"
        return summary
    frames = result["frames"]
    sign = 1.0 if result.get("forward", True) else -1.0
    speeds = [frame["speed"] for frame in frames]
    summary.update({
        "crossings": [(crossing["t"], crossing["state"]) for crossing in result["crossings"]],
        "start": frames[0]["loc"],
        "end": frames[-1]["loc"],
        "w_start": frames[0]["w"],
        "w_end": frames[-1]["w"],
        "speed_min": min(speeds),
        "speed_median": median(speeds),
        "states": sorted({frame["state"] for frame in frames}),
        "walked_cm": result["walked_cm"],
        "frame": frame_stats(result.get("frame_ms") or [], hitch_ms),
        "slate_max_ms": round(max(result["slate_ms"]), 1) if result.get("slate_ms") else None,
    })
    for hitch in result.get("hitches") or []:
        passage_cm = None if start_passage_cm is None else round(start_passage_cm + sign * hitch["walked_cm"])
        summary["hitches"].append(dict(hitch, passage_cm=passage_cm))
    return summary


def pooled_stats(results, hitch_ms):
    pooled = []
    for result in results:
        if result and result.get("ok"):
            pooled += result.get("frame_ms") or []
    return frame_stats(pooled, hitch_ms)


def format_stats_row(label_cells, stats, slate_max):
    if stats is None:
        return f"| {label_cells} | - | - | - | - | - | - |"
    slate_text = "-" if slate_max is None else f"{slate_max:.1f}"
    return f"| {label_cells} | {stats['samples']} | {stats['mean_ms']:.1f} | {stats['p95_ms']:.1f} | {stats['max_ms']:.1f} | {slate_text} | {stats['over_hitch']} |"


def render_summary(run_name, created, plans, summaries, pooled, hitch_ms, speed_cm_per_second):
    lines = [
        f"# PIE 통로 걷기 {run_name}",
        "",
        f"- 실행: {created}, 이동 속도 가정 {speed_cm_per_second:.0f}cm/s, 끊김 기준 {hitch_ms:.0f}ms 초과",
        "- 프레임 시간 = 월드 실시간(GetRealTimeSeconds) 틱 간 차이, 최대(slate)는 슬레이트 틱 인수의 최대(교차 확인). 에디터 창이 백그라운드면 스로틀로 느려진다.",
        "- 통로 좌표(cm) = 필드 쪽 입구 0, 이음매 = 접근 길이, 끝 = 접근 + 이탈 길이. 걷기는 입구 안쪽 400에서 시작한다.",
    ]
    for plan in plans:
        lines.append(f"- {plan['gate_id']}: 이음매 {plan['approach_cm']:.0f}, 끝 {plan['approach_cm'] + plan['departure_cm']:.0f}, 걷는 시간 {plan['seconds']:.1f}초")
    lines += [
        "",
        "## 걷기 요약",
        "",
        "| 관문 | 방향 | 이음매 통과(초, 이동 상태) | 시작 -> 끝(cm) | 진행도 w | 속도 최소/중앙(cm/s) | 걸은 거리(cm) | 이동 상태 |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for summary in summaries:
        if summary["error"]:
            lines.append(f"| {summary['gate_id']} | {summary['direction']} | 실패: {spots.table_cell(summary['error'])} | - | - | - | - | - |")
            continue
        crossings = ", ".join(f"{t}s {state}" for t, state in summary["crossings"]) or "없음"
        lines.append(
            f"| {summary['gate_id']} | {summary['direction']} | {spots.table_cell(crossings)} | {summary['start']} -> {summary['end']} "
            f"| {summary['w_start']} -> {summary['w_end']} | {summary['speed_min']} / {summary['speed_median']} | {summary['walked_cm']:.0f} | {', '.join(summary['states'])} |"
        )
    lines += [
        "",
        "## 프레임 시간(ms)",
        "",
        "| 관문 | 방향 | 표본 | 평균 | 95백분위 | 최대 | 최대(slate) | 끊김 횟수 |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for summary in summaries:
        if summary["error"]:
            continue
        lines.append(format_stats_row(f"{summary['gate_id']} | {summary['direction']}", summary["frame"], summary["slate_max_ms"]))
    lines.append(format_stats_row("전체 | -", pooled, None))
    lines += ["", f"## 끊김 순간(프레임 시간 {hitch_ms:.0f}ms 초과)", ""]
    hitch_rows = [(summary, hitch) for summary in summaries for hitch in summary["hitches"]]
    if not hitch_rows:
        lines.append("없음")
        return "\n".join(lines) + "\n"
    lines += ["| 관문 | 방향 | t(초) | 프레임 ms | slate ms | 통로 좌표(cm) | 월드 좌표(cm) | 이동 상태 | w |", "|---|---|---|---|---|---|---|---|---|"]
    for summary, hitch in hitch_rows:
        passage = "-" if hitch["passage_cm"] is None else hitch["passage_cm"]
        lines.append(f"| {summary['gate_id']} | {summary['direction']} | {hitch['t']} | {hitch['frame_ms']} | {hitch['slate_ms']} | {passage} | {hitch['loc']} | {hitch['state']} | {hitch['w']} |")
    return "\n".join(lines) + "\n"


def walk_gate(plan, out_dir, hitch_ms):
    outputs = {}
    for direction, forward in (("in", True), ("out", False)):
        try:
            result = spots.call_probe(walk_request(plan, forward, hitch_ms), result_timeout_seconds=plan["seconds"] + RESULT_MARGIN_SECONDS)
        except Exception as error:
            result = {"ok": False, "error": repr(error)}
        spots.write_json(os.path.join(out_dir, f"{plan['gate_id']}_{direction}.json"), result)
        spots.log(f"{plan['gate_id']} {direction}: ok={result.get('ok')} frames={len(result.get('frame_ms') or [])} hitches={len(result.get('hitches') or [])}")
        outputs[direction] = result
    out_start = None
    if outputs["in"].get("ok"):
        out_start = plan["start_passage_cm"] + outputs["in"]["walked_cm"]
    return outputs, out_start


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="PIE 통로 왕복 걷기(실제 이동 입력) + 프레임 시간 요약")
    parser.add_argument("--spots", default=spots.DEFAULT_SPOTS, help="필드 높이맵 위치를 읽을 지점 목록 JSON(기본 Tools/WorldGen/topdown_spots.json)")
    parser.add_argument("--run", default=None, help="결과 폴더 이름(기본 현재 시각), Saved/WorldGen/PieWalk/<run>/")
    parser.add_argument("--gates", default="", help="쉼표로 구분한 관문 ID 또는 접두어(SunkenStarfield 등), 비우면 전부")
    parser.add_argument("--speed", type=float, default=550.0, help="걷는 시간 산정에 쓰는 이동 속도 cm/s")
    parser.add_argument("--hitch-ms", type=float, default=100.0, help="이 값을 넘는 프레임을 끊김으로 센다")
    parser.add_argument("--warmup", type=float, default=15.0, help="PIE 시작 뒤 대기 초")
    parser.add_argument("--overwrite", action="store_true", help="이미 있는 run 폴더에 덮어쓴다")
    return parser.parse_args(argv)


def main(argv=None):
    spots.configure_output()
    args = parse_args(argv)
    regions, _ = spots.load_spots(os.path.abspath(args.spots))
    ground_cm = spots.build_ground_lookup(regions)
    selected = select_gates(load_gates(), args.gates)
    run_name = args.run or time.strftime("%Y%m%d_%H%M%S")
    out_dir = os.path.join(WALK_ROOT, run_name)
    summary_path = os.path.join(out_dir, "summary.md")
    if os.path.exists(summary_path) and not args.overwrite:
        raise SystemExit(f"run folder already exists (use another --run or --overwrite): {out_dir}")
    os.makedirs(out_dir, exist_ok=True)
    plans = [build_plan(realm_id, gate, ground_cm, args.speed) for realm_id, gate in selected]
    summaries = []
    raw_results = []
    try:
        spots.start_pie(args.warmup)
    except spots.PieStartError as error:
        spots.log(str(error))
        return 2
    try:
        for plan in plans:
            outputs, out_start = walk_gate(plan, out_dir, args.hitch_ms)
            summaries.append(summarize_walk(plan["gate_id"], "in", outputs["in"], plan["start_passage_cm"], args.hitch_ms))
            summaries.append(summarize_walk(plan["gate_id"], "out", outputs["out"], out_start, args.hitch_ms))
            raw_results += [outputs["in"], outputs["out"]]
    finally:
        spots.stop_pie()
    text = render_summary(run_name, time.strftime("%Y-%m-%d %H:%M:%S"), plans, summaries, pooled_stats(raw_results, args.hitch_ms), args.hitch_ms, args.speed)
    spots.write_text(summary_path, text)
    spots.log(f"summary: {summary_path}")
    return 1 if any(summary["error"] for summary in summaries) else 0


if __name__ == "__main__":
    sys.exit(main())

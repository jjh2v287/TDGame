"""실행 환경: 시스템 Python 3.12(에디터 불필요). pie_check_tactics.py가 모은 PIE 표본 기록(JSONL)을 읽어 무리 전술층 판정 지표를 계산한다.
실행 명령: python Tools/MonsterAI/tactics_pie_analysis.py <표본.jsonl> [--out <요약.json>]
출력: 표준 출력 요약 + --out JSON(장면별 동시 공격·예산, 대기 고리 포위·표류·위협 발짝, 막힌 칸 재배정, 디렉터 박자, 공격 차례 간격)
상태: 현행(2026-10-18 M3-14 PIE 검증용)
"""
import argparse
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

MONSTER_PATTERN = re.compile(
    r"sim=(?P<sim>\d+) actor=(?P<actor>\S+) def=(?P<def>\S+) state=(?P<state>\w+) action=(?P<action>\S+) engaged=(?P<engaged>\d) "
    r"engagement=(?P<engagement>\w+) phase=(?P<phase>\d+) hp=(?P<hp>[-\d.]+) pos=\((?P<x>[-\d]+),(?P<y>[-\d]+)\)"
    r"(?: token=(?P<token>\S+) slot=(?P<slot>\S+) ondeck=(?P<ondeck>\d))?(?: alarm=(?P<alarm>\S+) morale=(?P<morale>\S+))?")
TACTICS_PATTERN = re.compile(
    r"record=(?P<record>\d+) target=(?P<target>\S+) melee=(?P<melee>\d+) ranged=(?P<ranged>\d+) base_tokens=(?P<base_m>\d+)/(?P<base_r>\d+) "
    r"budget=(?P<budget_m>\d+)/(?P<budget_r>\d+) held=(?P<held_m>\d+)/(?P<held_r>\d+) phase=(?P<phase>\w+) intensity=(?P<intensity>\d+) "
    r"spacing=(?P<spacing>\d+) mercy=(?P<mercy>\d) freeze=(?P<freeze>\d) grace=(?P<grace>\d)(?: blocked=(?P<blocked>\d+))?")
WAIT_SLOT_PATTERN = re.compile(r"W(?P<ring>\d):(?P<sector>\d+)")
DIRECTOR_CYCLE = ["BuildUp", "SustainPeak", "PeakFade", "Relax"]
STATIONARY_SPEED_CM = 20.0
STUCK_SPEED_CM = 15.0
STUCK_SECONDS = 2.0
STUCK_RING_ERROR_CM = 150.0
FEINT_DIP_CM = 20.0
STINT_MIN_SECONDS = 3.0
ELIGIBLE_RADIUS_CM = 900.0
STARVATION_SECONDS = 20.0


def parse_sample(entry):
    monsters = []
    for line in entry.get("monsters", []):
        match = MONSTER_PATTERN.search(line)
        if not match:
            continue
        monster = match.groupdict()
        for key in ("sim", "engaged", "phase", "ondeck"):
            monster[key] = int(monster[key]) if monster[key] is not None else 0
        monster["x"] = float(monster["x"])
        monster["y"] = float(monster["y"])
        monster["hp"] = float(monster["hp"])
        monster["token"] = monster["token"] or "-"
        monster["slot"] = monster["slot"] or "-"
        monster["morale"] = monster["morale"] or "-"
        monsters.append(monster)
    records = []
    for line in entry.get("tactics", []):
        match = TACTICS_PATTERN.search(line)
        if not match:
            continue
        record = {key: (value if key in ("target", "phase") else int(value)) for key, value in match.groupdict().items() if value is not None}
        records.append(record)
    return monsters, records


def bearing_degrees(player, monster):
    return math.degrees(math.atan2(monster["y"] - player["y"], monster["x"] - player["x"])) % 360.0


def planar_distance(player, monster):
    return math.hypot(monster["x"] - player["x"], monster["y"] - player["y"])


def largest_angle_gap(bearings):
    if len(bearings) < 2:
        return 360.0
    ordered = sorted(bearings)
    gaps = [ordered[index + 1] - ordered[index] for index in range(len(ordered) - 1)]
    gaps.append(360.0 - ordered[-1] + ordered[0])
    return max(gaps)


def angle_span(bearings):
    if not bearings:
        return 0.0
    reference = bearings[0]
    offsets = [((value - reference + 180.0) % 360.0) - 180.0 for value in bearings]
    return max(offsets) - min(offsets)


def summarize_concurrency(samples):
    max_casting = 0
    max_token_casting = 0
    budget_violation_samples = 0
    tokenless_cast_samples = 0
    held_over_budget_samples = 0
    worst = None
    engaged_to_base = defaultdict(set)
    for sample in samples:
        engaged = [m for m in sample["monsters"] if m["engaged"]]
        casting = [m for m in engaged if m["state"] == "Cast"]
        token_casting = [m for m in casting if m["token"] in ("C", "R")]
        tokenless = [m for m in casting if m["token"] == "-"]
        max_casting = max(max_casting, len(casting))
        max_token_casting = max(max_token_casting, len(token_casting))
        if tokenless:
            tokenless_cast_samples += 1
        for record in sample["records"]:
            token_budget = record["budget_m"] // 4 + record["budget_r"] // 4
            engaged_to_base[record["melee"]].add(record["base_m"])
            if record["held_m"] > record["budget_m"] or record["held_r"] > record["budget_r"]:
                held_over_budget_samples += 1
            if len(token_casting) > token_budget:
                budget_violation_samples += 1
                if worst is None or len(token_casting) - token_budget > worst["excess"]:
                    worst = {"t": sample["t"], "casting": len(token_casting), "token_budget": token_budget, "excess": len(token_casting) - token_budget, "phase": record["phase"]}
    return {
        "samples": len(samples),
        "max_casting_engaged": max_casting,
        "max_casting_with_token": max_token_casting,
        "samples_casting_over_token_budget": budget_violation_samples,
        "samples_held_over_budget": held_over_budget_samples,
        "samples_with_tokenless_cast": tokenless_cast_samples,
        "worst_over_budget": worst,
        "melee_engaged_to_base_tokens": {str(key): sorted(value) for key, value in sorted(engaged_to_base.items())},
    }


def player_speed(samples, index):
    if index == 0:
        return 0.0
    previous = samples[index - 1]
    current = samples[index]
    elapsed = max(1e-3, current["t"] - previous["t"])
    return math.hypot(current["player"]["x"] - previous["player"]["x"], current["player"]["y"] - previous["player"]["y"]) / elapsed


def summarize_surround(samples):
    gaps = []
    distances_by_ring = defaultdict(list)
    waiter_counts = []
    quadrant_counts = []
    for index, sample in enumerate(samples):
        if player_speed(samples, index) > STATIONARY_SPEED_CM:
            continue
        player = sample["player"]
        waiters = [m for m in sample["monsters"] if m["engaged"] and WAIT_SLOT_PATTERN.match(m["slot"])]
        if len(waiters) < 3:
            continue
        bearings = [bearing_degrees(player, m) for m in waiters]
        gaps.append(largest_angle_gap(bearings))
        waiter_counts.append(len(waiters))
        quadrant_counts.append(len({int(value // 90) for value in bearings}))
        for monster in waiters:
            ring = int(WAIT_SLOT_PATTERN.match(monster["slot"]).group("ring"))
            distances_by_ring[ring].append(planar_distance(player, monster))
    if not gaps:
        return {"stationary_samples_with_3_waiters": 0}
    return {
        "stationary_samples_with_3_waiters": len(gaps),
        "waiters_median": statistics.median(waiter_counts),
        "largest_angle_gap_median_deg": round(statistics.median(gaps), 1),
        "largest_angle_gap_p90_deg": round(sorted(gaps)[int(len(gaps) * 0.9) - 1 if len(gaps) >= 10 else -1], 1),
        "quadrants_occupied_median": statistics.median(quadrant_counts),
        "ring_distance_median_cm": {str(ring): round(statistics.median(values)) for ring, values in sorted(distances_by_ring.items())},
        "ring_distance_p10_p90_cm": {str(ring): [round(sorted(values)[len(values) // 10]), round(sorted(values)[len(values) * 9 // 10])] for ring, values in sorted(distances_by_ring.items())},
    }


def collect_stints(samples):
    stints = []
    open_stints = {}
    for index, sample in enumerate(samples):
        player = sample["player"]
        stationary = player_speed(samples, index) <= STATIONARY_SPEED_CM
        seen = set()
        for monster in sample["monsters"]:
            if not monster["engaged"] or not WAIT_SLOT_PATTERN.match(monster["slot"]):
                continue
            key = monster["sim"]
            seen.add(key)
            point = {"t": sample["t"], "bearing": bearing_degrees(player, monster), "distance": planar_distance(player, monster), "stationary": stationary, "x": monster["x"], "y": monster["y"]}
            stint = open_stints.get(key)
            if stint is None or stint["slot"] != monster["slot"]:
                if stint is not None:
                    stints.append(stint)
                stint = {"sim": key, "def": monster["def"], "slot": monster["slot"], "points": []}
                open_stints[key] = stint
            stint["points"].append(point)
        for key in list(open_stints):
            if key not in seen:
                stints.append(open_stints.pop(key))
    stints.extend(open_stints.values())
    return stints


def count_feint_dips(distances):
    if len(distances) < 5:
        return 0
    baseline = statistics.median(distances)
    dips = 0
    is_inside_dip = False
    for value in distances:
        if not is_inside_dip and value < baseline - FEINT_DIP_CM:
            dips += 1
            is_inside_dip = True
        elif is_inside_dip and value > baseline - FEINT_DIP_CM * 0.5:
            is_inside_dip = False
    return dips


def summarize_drift_and_feint(samples):
    long_stints = []
    for stint in collect_stints(samples):
        points = [p for p in stint["points"] if p["stationary"]]
        if len(points) < 2 or points[-1]["t"] - points[0]["t"] < STINT_MIN_SECONDS:
            continue
        settled = [p for p in points if p["t"] - points[0]["t"] >= 1.0]
        if len(settled) < 5:
            continue
        distances = [p["distance"] for p in settled]
        long_stints.append({
            "sim": stint["sim"],
            "slot": stint["slot"],
            "seconds": round(settled[-1]["t"] - settled[0]["t"], 1),
            "bearing_span_deg": round(angle_span([p["bearing"] for p in settled]), 1),
            "distance_span_cm": round(max(distances) - min(distances)),
            "feint_dips": count_feint_dips(distances),
            "path_cm": round(sum(math.hypot(b["x"] - a["x"], b["y"] - a["y"]) for a, b in zip(settled, settled[1:]))),
        })
    if not long_stints:
        return {"settled_wait_stints": 0}
    return {
        "settled_wait_stints": len(long_stints),
        "bearing_span_median_deg": round(statistics.median(s["bearing_span_deg"] for s in long_stints), 1),
        "distance_span_median_cm": round(statistics.median(s["distance_span_cm"] for s in long_stints)),
        "stints_with_feint_dip": sum(1 for s in long_stints if s["feint_dips"] > 0),
        "stints_motionless": sum(1 for s in long_stints if s["path_cm"] < 30),
        "path_per_second_median_cm": round(statistics.median(s["path_cm"] / max(0.1, s["seconds"]) for s in long_stints)),
        "longest": sorted(long_stints, key=lambda s: -s["seconds"])[:5],
    }


def summarize_blocked(samples):
    blocked_max = 0
    blocked_samples = 0
    slot_changes = 0
    previous_slot = {}
    for sample in samples:
        for record in sample["records"]:
            value = record.get("blocked")
            if value is None:
                continue
            blocked_max = max(blocked_max, value)
            blocked_samples += 1 if value > 0 else 0
        for monster in sample["monsters"]:
            if not WAIT_SLOT_PATTERN.match(monster["slot"]):
                continue
            last = previous_slot.get(monster["sim"])
            if last is not None and last != monster["slot"]:
                slot_changes += 1
            previous_slot[monster["sim"]] = monster["slot"]
    ring_medians = defaultdict(list)
    for index, sample in enumerate(samples):
        for monster in sample["monsters"]:
            match = WAIT_SLOT_PATTERN.match(monster["slot"])
            if match and monster["engaged"]:
                ring_medians[int(match.group("ring"))].append(planar_distance(sample["player"], monster))
    ring_reference = {ring: statistics.median(values) for ring, values in ring_medians.items() if values}
    stuck_runs = []
    run_start = {}
    last_position = {}
    for sample in samples:
        player = sample["player"]
        for monster in sample["monsters"]:
            match = WAIT_SLOT_PATTERN.match(monster["slot"])
            key = monster["sim"]
            previous = last_position.get(key)
            last_position[key] = (sample["t"], monster["x"], monster["y"])
            if not match or not monster["engaged"] or previous is None:
                run_start.pop(key, None)
                continue
            elapsed = max(1e-3, sample["t"] - previous[0])
            speed = math.hypot(monster["x"] - previous[1], monster["y"] - previous[2]) / elapsed
            error = abs(planar_distance(player, monster) - ring_reference.get(int(match.group("ring")), 0.0))
            if speed < STUCK_SPEED_CM and error > STUCK_RING_ERROR_CM:
                start = run_start.setdefault(key, (sample["t"], monster["slot"]))
                if sample["t"] - start[0] >= STUCK_SECONDS and (not stuck_runs or stuck_runs[-1]["sim"] != key or stuck_runs[-1]["start"] != start[0]):
                    stuck_runs.append({"sim": key, "slot": start[1], "start": start[0], "x": monster["x"], "y": monster["y"], "ring_error_cm": round(error)})
            else:
                run_start.pop(key, None)
    return {
        "blocked_sectors_max": blocked_max,
        "samples_with_blocked_sector": blocked_samples,
        "wait_slot_changes": slot_changes,
        "ring_reference_cm": {str(ring): round(value) for ring, value in sorted(ring_reference.items())},
        "stuck_waiter_runs_over_2s": len(stuck_runs),
        "stuck_examples": stuck_runs[:6],
    }


def summarize_director(samples):
    timeline = []
    intensity_max = 0
    for sample in samples:
        for record in sample["records"]:
            intensity_max = max(intensity_max, record["intensity"])
            if not timeline or timeline[-1]["phase"] != record["phase"]:
                timeline.append({"phase": record["phase"], "t": sample["t"], "intensity": record["intensity"]})
            break
    for index, entry in enumerate(timeline):
        end = timeline[index + 1]["t"] if index + 1 < len(timeline) else (samples[-1]["t"] if samples else entry["t"])
        entry["seconds"] = round(end - entry["t"], 2)
    cycles = 0
    out_of_order = 0
    for previous, current in zip(timeline, timeline[1:]):
        if previous["phase"] in DIRECTOR_CYCLE and current["phase"] in DIRECTOR_CYCLE:
            expected = DIRECTOR_CYCLE[(DIRECTOR_CYCLE.index(previous["phase"]) + 1) % len(DIRECTOR_CYCLE)]
            if current["phase"] != expected:
                out_of_order += 1
            if previous["phase"] == "Relax" and current["phase"] == "BuildUp":
                cycles += 1
    seconds_by_phase = defaultdict(float)
    for entry in timeline:
        seconds_by_phase[entry["phase"]] += entry["seconds"]
    return {
        "transitions": len(timeline) - 1 if timeline else 0,
        "completed_cycles": cycles,
        "out_of_order_transitions": out_of_order,
        "intensity_max": intensity_max,
        "seconds_by_phase": {key: round(value, 1) for key, value in seconds_by_phase.items()},
        "timeline": [{"phase": e["phase"], "t": round(e["t"], 2), "seconds": e["seconds"], "intensity": e["intensity"]} for e in timeline[:40]],
    }


def summarize_turns(samples):
    runs = defaultdict(list)
    open_runs = {}
    previous_state = {}
    definition = {}
    tokenless_streaks = defaultdict(float)
    tokenless_open = {}
    for sample in samples:
        player = sample["player"]
        seen = set()
        for monster in sample["monsters"]:
            key = monster["sim"]
            definition[key] = monster["def"]
            is_engaged = monster["engaged"] and monster["engagement"] == "Engaged"
            if is_engaged:
                seen.add(key)
                run = open_runs.setdefault(key, {"begin": sample["t"], "end": sample["t"], "starts": []})
                run["end"] = sample["t"]
                if monster["state"] == "Cast" and previous_state.get(key) != "Cast":
                    run["starts"].append(sample["t"])
                is_eligible = monster["token"] == "-" and monster["state"] != "Cast" and planar_distance(player, monster) <= ELIGIBLE_RADIUS_CM
                if is_eligible:
                    tokenless_open.setdefault(key, sample["t"])
                    tokenless_streaks[key] = max(tokenless_streaks[key], sample["t"] - tokenless_open[key])
                else:
                    tokenless_open.pop(key, None)
            previous_state[key] = monster["state"]
        for key in list(open_runs):
            if key not in seen:
                runs[key].append(open_runs.pop(key))
                tokenless_open.pop(key, None)
    for key, run in open_runs.items():
        runs[key].append(run)
    rows = []
    for key, monster_runs in runs.items():
        longest = 0.0
        engaged_seconds = 0.0
        starts = 0
        for run in monster_runs:
            moments = [run["begin"]] + run["starts"] + [run["end"]]
            longest = max([longest] + [b - a for a, b in zip(moments, moments[1:])])
            engaged_seconds += run["end"] - run["begin"]
            starts += len(run["starts"])
        rows.append({"sim": key, "def": definition[key], "engaged_seconds": round(engaged_seconds, 1), "engaged_runs": len(monster_runs), "attack_starts": starts,
                     "longest_engaged_gap_seconds": round(longest, 1), "longest_eligible_tokenless_seconds": round(tokenless_streaks.get(key, 0.0), 1)})
    rows.sort(key=lambda row: -row["longest_engaged_gap_seconds"])
    no_cast_time = 0.0
    engaged_time = 0.0
    for previous, current in zip(samples, samples[1:]):
        engaged = [m for m in previous["monsters"] if m["engaged"]]
        if not engaged:
            continue
        elapsed = current["t"] - previous["t"]
        engaged_time += elapsed
        if not any(m["state"] == "Cast" for m in engaged):
            no_cast_time += elapsed
    return {
        "engaged_monsters": len(rows),
        "attack_starts_total": sum(row["attack_starts"] for row in rows),
        "monsters_with_engaged_gap_over_20s": sum(1 for row in rows if row["longest_engaged_gap_seconds"] > STARVATION_SECONDS),
        "longest_engaged_gap_seconds": rows[0]["longest_engaged_gap_seconds"] if rows else 0.0,
        "longest_eligible_tokenless_seconds": max((row["longest_eligible_tokenless_seconds"] for row in rows), default=0.0),
        "share_of_engaged_time_without_cast": round(no_cast_time / engaged_time, 3) if engaged_time else None,
        "worst_monsters": rows[:6],
    }


def summarize_scenario(entries):
    samples = []
    for entry in entries:
        monsters, records = parse_sample(entry)
        samples.append({"t": entry["t"], "player": entry["player"], "monsters": monsters, "records": records})
    samples.sort(key=lambda sample: sample["t"])
    morale_seen = sorted({m["morale"] for sample in samples for m in sample["monsters"]})
    return {
        "seconds": round(samples[-1]["t"] - samples[0]["t"], 1) if samples else 0.0,
        "morale_labels_seen": morale_seen,
        "concurrency": summarize_concurrency(samples),
        "surround": summarize_surround(samples),
        "drift_and_feint": summarize_drift_and_feint(samples),
        "blocked": summarize_blocked(samples),
        "director": summarize_director(samples),
        "turns": summarize_turns(samples),
    }


def summarize_file(path):
    by_scenario = defaultdict(list)
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        entry = json.loads(line)
        if entry.get("kind") != "sample":
            continue
        by_scenario[entry["scenario"]].append(entry)
    summary = {name: summarize_scenario(entries) for name, entries in by_scenario.items()}
    everything = []
    for entries in by_scenario.values():
        for entry in entries:
            monsters, records = parse_sample(entry)
            everything.append({"t": entry["t"], "player": entry["player"], "monsters": monsters, "records": records})
    everything.sort(key=lambda sample: sample["t"])
    summary["_session_director"] = summarize_director(everything)
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("samples")
    parser.add_argument("--out")
    args = parser.parse_args()
    summary = summarize_file(args.samples)
    text = json.dumps(summary, indent=2, ensure_ascii=False)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()

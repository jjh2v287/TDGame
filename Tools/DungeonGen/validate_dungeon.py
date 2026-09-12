"""던전 layout.json 검증기 (설계서 11.1 항목, 언리얼 모듈 사용 안 함).

사용:
  python Tools/DungeonGen/validate_dungeon.py Saved/DungeonGen/<이름>/layout.json [--max-deadend-ratio 0.4]

검사 항목: 연결성(입구→보스), 필수 룸(입구·보스), 셀 겹침, 룸 수 범위, 막다른 길 비율, 열쇠·잠금 진행 논리,
내비 단절 대체 검사(모든 문 연결이 격자상 인접·정합인지). 결과는 layout.json 의 validation 필드에 기록하고
같은 폴더에 report.md 를 쓴다. 함수 validate(layout) 는 다른 스크립트에서 직접 부른다.
"""
import argparse
import json
import os
import sys

DIR_VEC = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}
OPPOSITE = {"N": "S", "S": "N", "E": "W", "W": "E"}
SIZE_NODE_RANGE = {"Small": (6, 9), "Medium": (10, 15), "Large": (16, 24)}
DEFAULT_MAX_DEADEND_RATIO = 0.4


def is_corridor(room):
    return "corridor" in room["tags"]


def build_adjacency(layout):
    adjacency = {room["id"]: [] for room in layout["rooms"]}
    for door in layout["doors"]:
        adjacency[door["room_a"]].append((door["room_b"], door))
        adjacency[door["room_b"]].append((door["room_a"], door))
    return adjacency


def reachable_with_keys(layout, adjacency, start_id, respect_locks):
    key_ids_by_room = {}
    for entry in layout.get("keys_locks", []):
        if entry.get("key_room"):
            key_ids_by_room.setdefault(entry["key_room"], []).append(entry["key_id"])
    held = set()
    visited = [start_id]
    visited_set = {start_id}
    changed = True
    while changed:
        changed = False
        frontier = list(visited)
        index = 0
        while index < len(frontier):
            current = frontier[index]
            index += 1
            for key_id in key_ids_by_room.get(current, []):
                held.add(key_id)
            for neighbor, door in adjacency[current]:
                if neighbor in visited_set:
                    continue
                if respect_locks and door["locked"] and door.get("key_id") not in held:
                    continue
                visited_set.add(neighbor)
                visited.append(neighbor)
                frontier.append(neighbor)
                changed = True
    return visited, held


def shortest_path(adjacency, start_id, goal_id):
    previous = {start_id: None}
    frontier = [start_id]
    index = 0
    while index < len(frontier):
        current = frontier[index]
        index += 1
        if current == goal_id:
            path = [current]
            while previous[path[-1]] is not None:
                path.append(previous[path[-1]])
            path.reverse()
            return path
        for neighbor, _ in adjacency[current]:
            if neighbor in previous:
                continue
            previous[neighbor] = current
            frontier.append(neighbor)
    return None


def check_required_rooms(layout):
    starts = [r["id"] for r in layout["rooms"] if "start" in r["tags"]]
    bosses = [r["id"] for r in layout["rooms"] if "boss" in r["tags"]]
    passed = len(starts) == 1 and len(bosses) >= 1
    return {
        "name": "required_rooms",
        "passed": passed,
        "message": "start=%s boss=%s" % (starts, bosses),
        "details": [],
    }


def check_overlap(layout):
    owner = {}
    details = []
    for room in layout["rooms"]:
        for cell in room["cells"]:
            key = tuple(cell)
            if key in owner:
                details.append("cell %s: %s and %s" % (list(key), owner[key], room["id"]))
            owner[key] = room["id"]
    return {"name": "overlap", "passed": not details, "message": "%d overlapping cells" % len(details), "details": details}


def check_door_integrity(layout):
    owner = {}
    for room in layout["rooms"]:
        for cell in room["cells"]:
            owner[tuple(cell)] = room["id"]
    room_by_id = {r["id"]: r for r in layout["rooms"]}
    details = []
    for door in layout["doors"]:
        cell_a = tuple(door["cell_a"])
        cell_b = tuple(door["cell_b"])
        expected_b = (cell_a[0] + DIR_VEC[door["dir_a"]][0], cell_a[1] + DIR_VEC[door["dir_a"]][1])
        if expected_b != cell_b or OPPOSITE[door["dir_a"]] != door["dir_b"]:
            details.append("door %s->%s cells %s/%s not adjacent-facing" % (door["room_a"], door["room_b"], list(cell_a), list(cell_b)))
            continue
        if owner.get(cell_a) != door["room_a"] or owner.get(cell_b) != door["room_b"]:
            details.append("door %s->%s cell ownership mismatch at %s/%s" % (door["room_a"], door["room_b"], list(cell_a), list(cell_b)))
            continue
        for room_id, cell, direction in ((door["room_a"], cell_a, door["dir_a"]), (door["room_b"], cell_b, door["dir_b"])):
            sockets = room_by_id[room_id]["doors"]
            if not any(tuple(s["cell"]) == cell and s["dir"] == direction for s in sockets):
                details.append("room %s has no socket at %s %s" % (room_id, list(cell), direction))
    return {
        "name": "door_integrity_grid_nav",
        "passed": not details,
        "message": "%d door connections checked, %d problems" % (len(layout["doors"]), len(details)),
        "details": details,
    }


def check_connectivity(layout, adjacency, start_id, boss_id):
    visited, _ = reachable_with_keys(layout, adjacency, start_id, respect_locks=False)
    visited_set = set(visited)
    unreachable = [r["id"] for r in layout["rooms"] if r["id"] not in visited_set]
    boss_ok = boss_id in visited_set
    details = ["unreachable room %s cells %s" % (rid, next(r["cells"] for r in layout["rooms"] if r["id"] == rid)) for rid in unreachable]
    return {
        "name": "connectivity",
        "passed": boss_ok and not unreachable,
        "message": "boss reachable=%s, unreachable rooms=%d" % (boss_ok, len(unreachable)),
        "details": details,
    }


def check_room_count(layout):
    count = sum(1 for r in layout["rooms"] if not is_corridor(r))
    low, high = SIZE_NODE_RANGE.get(layout.get("size", "Medium"), (1, 10 ** 6))
    passed = low <= count <= high
    return {
        "name": "room_count",
        "passed": passed,
        "message": "%d rooms (allowed %d..%d for %s)" % (count, low, high, layout.get("size")),
        "details": [] if passed else ["room count %d outside %d..%d" % (count, low, high)],
    }


def check_deadend_ratio(layout, adjacency, max_ratio):
    rooms = [r for r in layout["rooms"] if not is_corridor(r)]
    dead_ends = []
    for room in rooms:
        if "start" in room["tags"] or "boss" in room["tags"]:
            continue
        if len(adjacency[room["id"]]) == 1:
            dead_ends.append(room["id"])
    ratio = len(dead_ends) / float(len(rooms)) if rooms else 0.0
    passed = ratio <= max_ratio
    return {
        "name": "deadend_ratio",
        "passed": passed,
        "message": "%d/%d = %.2f (max %.2f)" % (len(dead_ends), len(rooms), ratio, max_ratio),
        "details": [] if passed else ["dead-end rooms: %s" % ", ".join(dead_ends)],
    }, ratio, dead_ends


def check_progression(layout, adjacency, start_id, boss_id):
    visited, held = reachable_with_keys(layout, adjacency, start_id, respect_locks=True)
    visited_set = set(visited)
    details = []
    for entry in layout.get("keys_locks", []):
        if entry.get("key_room") is None:
            details.append("lock %s has no key room" % entry["key_id"])
            continue
        if entry["key_room"] not in visited_set:
            details.append("key room %s (%s) unreachable before its lock" % (entry["key_room"], entry["key_id"]))
    if boss_id not in visited_set:
        details.append("boss %s unreachable when locks are respected" % boss_id)
    blocked_rooms = [r["id"] for r in layout["rooms"] if r["id"] not in visited_set]
    if blocked_rooms:
        details.append("rooms never reachable with keys: %s" % ", ".join(blocked_rooms))
    return {
        "name": "progression_key_lock",
        "passed": not details,
        "message": "locks=%d keys_held=%s" % (len(layout.get("keys_locks", [])), sorted(held)),
        "details": details,
    }


def count_sealed_facing_pairs(layout):
    socket_index = {}
    for room in layout["rooms"]:
        for socket in room["doors"]:
            if socket["connected_room"] is None:
                socket_index[(tuple(socket["cell"]), socket["dir"])] = room["id"]
    pairs = 0
    for (cell, direction), _ in socket_index.items():
        other = ((cell[0] + DIR_VEC[direction][0], cell[1] + DIR_VEC[direction][1]), OPPOSITE[direction])
        if other in socket_index and (cell, direction) < other:
            pairs += 1
    return pairs


def validate(layout, max_deadend_ratio=DEFAULT_MAX_DEADEND_RATIO):
    checks = [check_required_rooms(layout), check_overlap(layout), check_door_integrity(layout)]
    adjacency = build_adjacency(layout)
    starts = [r["id"] for r in layout["rooms"] if "start" in r["tags"]]
    bosses = [r["id"] for r in layout["rooms"] if "boss" in r["tags"]]
    metrics = {
        "room_count": sum(1 for r in layout["rooms"] if not is_corridor(r)),
        "module_count": len(layout["rooms"]),
        "door_count": len(layout["doors"]),
        "locked_door_count": sum(1 for d in layout["doors"] if d["locked"]),
        "loop_count": len(layout["doors"]) - (len(layout["rooms"]) - 1),
        "branch_count": sum(1 for r in layout["rooms"] if not is_corridor(r) and len(adjacency[r["id"]]) >= 3),
        "sealed_facing_pairs": count_sealed_facing_pairs(layout),
    }
    if starts and bosses:
        start_id, boss_id = starts[0], bosses[0]
        checks.append(check_connectivity(layout, adjacency, start_id, boss_id))
        checks.append(check_room_count(layout))
        deadend_check, ratio, dead_ends = check_deadend_ratio(layout, adjacency, max_deadend_ratio)
        checks.append(deadend_check)
        checks.append(check_progression(layout, adjacency, start_id, boss_id))
        path = shortest_path(adjacency, start_id, boss_id)
        room_by_id = {r["id"]: r for r in layout["rooms"]}
        path_rooms = [rid for rid in (path or []) if not is_corridor(room_by_id[rid])]
        metrics["deadend_count"] = len(dead_ends)
        metrics["deadend_ratio"] = round(ratio, 4)
        metrics["main_path_modules"] = len(path) if path else 0
        metrics["main_path_rooms"] = len(path_rooms)
        metrics["main_path_ratio"] = round(len(path_rooms) / float(metrics["room_count"]), 4) if metrics["room_count"] else 0.0
        metrics["main_path"] = path or []
    passed = all(c["passed"] for c in checks)
    return {"passed": passed, "checks": checks, "metrics": metrics, "max_deadend_ratio": max_deadend_ratio}


def write_report(layout, result, path):
    lines = []
    lines.append("# 던전 검증 리포트: %s %s %s seed=%s" % (layout.get("theme"), layout.get("flow"), layout.get("size"), layout.get("seed")))
    lines.append("")
    lines.append("결과: **%s**" % ("PASS" if result["passed"] else "FAIL"))
    lines.append("")
    lines.append("| 검사 | 결과 | 요약 |")
    lines.append("|---|---|---|")
    for check in result["checks"]:
        lines.append("| %s | %s | %s |" % (check["name"], "PASS" if check["passed"] else "FAIL", check["message"]))
    failed = [c for c in result["checks"] if not c["passed"]]
    if failed:
        lines.append("")
        lines.append("## 실패 원인")
        for check in failed:
            lines.append("- %s: %s" % (check["name"], check["message"]))
            for detail in check["details"]:
                lines.append("  - %s" % detail)
    lines.append("")
    lines.append("## 지표")
    for key in sorted(result["metrics"]):
        lines.append("- %s: %s" % (key, result["metrics"][key]))
    lines.append("")
    lines.append("## 룸 목록")
    lines.append("| id | module | rot | origin | tags | flow_node | connected doors |")
    lines.append("|---|---|---|---|---|---|---|")
    for room in layout["rooms"]:
        connected = [d["connected_room"] + ("(L)" if d["locked"] else "") for d in room["doors"] if d["connected_room"]]
        lines.append("| %s | %s | %d | %s | %s | %s | %s |" % (
            room["id"], room["module"], room["rotation"], room["cell_origin"], ",".join(room["tags"]),
            room["flow_node"] or "-", ",".join(connected)))
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("layout", help="layout.json 경로")
    parser.add_argument("--max-deadend-ratio", type=float, default=DEFAULT_MAX_DEADEND_RATIO)
    args = parser.parse_args(argv)
    with open(args.layout, "r", encoding="utf-8") as handle:
        layout = json.load(handle)
    result = validate(layout, args.max_deadend_ratio)
    layout["validation"] = result
    with open(args.layout, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(layout, handle, indent=1, sort_keys=True, ensure_ascii=False)
        handle.write("\n")
    report_path = os.path.join(os.path.dirname(os.path.abspath(args.layout)), "report.md")
    write_report(layout, result, report_path)
    for check in result["checks"]:
        print("%-26s %s  %s" % (check["name"], "PASS" if check["passed"] else "FAIL", check["message"]))
    print("RESULT: %s -> %s" % ("PASS" if result["passed"] else "FAIL", report_path))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

"""잿빛 골짜기(Ashen Vale) 월드 레이아웃 검증기 (에디터 밖에서 실행, numpy + scipy).

설계서 11.2(R-91) 항목을 `generate_ashen_vale.py`의 출력 폴더에 대해 검사한다.
  1 entrance_spacing  던전 입구(마커 kind DungeonEntrance) 사이 최소 거리
  2 poi_density       POI(마커 kind Poi) 개수 범위와 POI 사이 최소 거리
  3 road_reach        마을(PlayerStart) 근처 도로 격자에서 연결된 도로가 모든 POI·입구에 허용 거리 안까지 닿는지
                      (layout.json에 "roads" 폴리라인이 있으면 그것을 격자에 그려 쓰고, 없으면 layer_Road.r8 가중치를 쓴다)
  4 terrain_fit       POI·입구·PlayerStart 위치의 경사(반지름 8m 표본)와 수면 아래 여부
  5 streaming_safety  던전 아틀라스 슬롯 로딩 반경이 서로/야외 지역과 겹치지 않는지
  6 play_density      콘텐츠(POI·입구·마을)에서 멀리 떨어진 연속 도로 구간의 길이

실행: python Tools/WorldGen/validate_world.py [--dir Saved/WorldGen/AshenVale] [--report 경로] [--json 경로] [--slots 3]
출력: <dir>/report.md, <dir>/validation.json (점수 0~100 = 항목별 가중치 × 항목 점수의 합). 종료 코드 0 = 전부 통과, 1 = 실패 있음.
점수: 항목 3·4는 대상별 통과 비율, 나머지는 통과 1 / 실패 0. 가중치는 WEIGHTS 참고.

좌표 규약은 생성기와 같다: 높이맵 [iy, ix], 월드 X = ix - 504 m, Y = iy - 504 m, 마커 위치는 cm.
다른 스크립트에서는 run_validation(dir, opts) 를 불러 dict 결과를 받을 수 있다.
"""
import argparse
import heapq
import json
import math
import os
import sys

import numpy as np
from scipy import ndimage

SIZE = 1009
HALF = (SIZE - 1) // 2
UNITS_PER_METER = 128.0
WATER_Z_M = -1.75
WEIGHTS = {"entrance_spacing": 15, "poi_density": 15, "road_reach": 25, "terrain_fit": 20, "streaming_safety": 10, "play_density": 15}
NAMES = {
    "entrance_spacing": "던전 입구 간격", "poi_density": "POI 밀도", "road_reach": "도로 도달성",
    "terrain_fit": "지형 적합성", "streaming_safety": "스트리밍 안전", "play_density": "플레이 밀도",
}
DEFAULTS = {
    "entrance_min_m": 150.0, "poi_min_count": 6, "poi_max_count": 12, "poi_min_m": 40.0,
    "road_weight": 100, "road_reach_m": 40.0, "max_slope_deg": 25.0, "sample_r_m": 8.0, "steep_frac": 0.5,
    "slots": 3, "slot_origin_cm": (300000.0, 300000.0), "slot_pitch_cm": 30000.0, "load_radius_cm": 12800.0,
    "content_far_m": 120.0, "empty_max_m": 180.0,
}
EIGHT = np.ones((3, 3), dtype=bool)


def load_world(d):
    with open(os.path.join(d, "layout.json"), "r", encoding="utf-8") as fp:
        layout = json.load(fp)
    hu = np.fromfile(os.path.join(d, "height.r16"), dtype="<u2")
    if hu.size != SIZE * SIZE:
        raise SystemExit(f"height.r16 크기가 {SIZE}x{SIZE}가 아님: {hu.size}")
    h = (hu.reshape(SIZE, SIZE).astype(np.float64) - 32768.0) / UNITS_PER_METER
    road_path = os.path.join(d, "layer_Road.r8")
    road = np.fromfile(road_path, dtype=np.uint8).reshape(SIZE, SIZE) if os.path.exists(road_path) else None
    return {"layout": layout, "h": h, "road": road, "dir": d}


def marker_xy_m(m):
    return m["loc"][0] / 100.0, m["loc"][1] / 100.0


def to_grid(x_m, y_m):
    return int(round(x_m + HALF)), int(round(y_m + HALF))


def in_grid(ix, iy):
    return 0 <= ix < SIZE and 0 <= iy < SIZE


def loc_cm(m):
    return [round(float(v), 1) for v in m["loc"][:2]]


def markers_of(layout, kind):
    return [m for m in layout.get("markers", []) if m.get("kind") == kind]


def player_start(layout):
    ps = markers_of(layout, "PlayerStart")
    if ps:
        return ps[0]
    v = layout.get("region", {}).get("village", [0.0, 0.0])
    return {"kind": "PlayerStart", "label": "village", "loc": [v[0] * 100.0, v[1] * 100.0, 0.0]}


def pair_min_distance(ms, min_m):
    issues = []
    worst = None
    for i in range(len(ms)):
        for j in range(i + 1, len(ms)):
            ax, ay = marker_xy_m(ms[i])
            bx, by = marker_xy_m(ms[j])
            dm = math.hypot(ax - bx, ay - by)
            if worst is None or dm < worst:
                worst = dm
            if dm < min_m:
                issues.append({"label": f"{ms[i]['label']} ~ {ms[j]['label']}", "loc_cm": loc_cm(ms[i]), "reason": f"거리 {dm:.1f} m < 최소 {min_m:.0f} m"})
    return worst, issues


def check_entrance_spacing(w, o):
    ents = markers_of(w["layout"], "DungeonEntrance")
    worst, issues = pair_min_distance(ents, o["entrance_min_m"])
    passed = not issues and len(ents) >= 1
    if not ents:
        issues.append({"label": "-", "loc_cm": [0, 0], "reason": "DungeonEntrance 마커 없음"})
    return {"passed": passed, "score": 1.0 if passed else 0.0, "issues": issues,
            "metrics": {"entrance_count": len(ents), "min_pair_m": None if worst is None else round(worst, 1)},
            "summary": f"입구 {len(ents)}개, 최소 간격 {worst if worst is None else round(worst, 1)} m (기준 ≥ {o['entrance_min_m']:.0f} m)"}


def check_poi_density(w, o):
    pois = markers_of(w["layout"], "Poi")
    worst, issues = pair_min_distance(pois, o["poi_min_m"])
    n = len(pois)
    if not (o["poi_min_count"] <= n <= o["poi_max_count"]):
        issues.insert(0, {"label": "-", "loc_cm": [0, 0], "reason": f"POI 개수 {n}개가 범위 {o['poi_min_count']}~{o['poi_max_count']} 밖"})
    passed = not issues
    return {"passed": passed, "score": 1.0 if passed else 0.0, "issues": issues,
            "metrics": {"poi_count": n, "min_pair_m": None if worst is None else round(worst, 1)},
            "summary": f"POI {n}개 (범위 {o['poi_min_count']}~{o['poi_max_count']}), 최소 간격 {worst if worst is None else round(worst, 1)} m (기준 ≥ {o['poi_min_m']:.0f} m)"}


def rasterize_roads(roads):
    mask = np.zeros((SIZE, SIZE), dtype=bool)
    width_m = 4.0
    for r in roads:
        pts = r.get("points") or r.get("points_cm") or []
        width_m = max(width_m, float(r.get("width_cm", 400.0)) / 100.0)
        for a, b in zip(pts[:-1], pts[1:]):
            ax, ay, bx, by = a[0] / 100.0, a[1] / 100.0, b[0] / 100.0, b[1] / 100.0
            n = max(2, int(math.hypot(bx - ax, by - ay) * 2.0) + 1)
            for t in np.linspace(0.0, 1.0, n):
                ix, iy = to_grid(ax + (bx - ax) * t, ay + (by - ay) * t)
                if in_grid(ix, iy):
                    mask[iy, ix] = True
    if mask.any():
        mask = ndimage.distance_transform_edt(~mask) <= width_m * 0.5
    return mask


def passable_mask(w, o):
    roads = w["layout"].get("roads")
    if roads:
        return rasterize_roads(roads), "layout.json roads 폴리라인"
    if w["road"] is None:
        return np.zeros((SIZE, SIZE), dtype=bool), "도로 없음"
    return w["road"] >= o["road_weight"], f"layer_Road.r8 ≥ {o['road_weight']}"


def content_targets(layout):
    return markers_of(layout, "Poi") + markers_of(layout, "DungeonEntrance") + markers_of(layout, "Arena")


def check_road_reach(w, o):
    passable, source = passable_mask(w, o)
    labels, n_lab = ndimage.label(passable, structure=EIGHT)
    ps = player_start(w["layout"])
    sx, sy = to_grid(*marker_xy_m(ps))
    r = int(o["road_reach_m"])
    y0, y1, x0, x1 = max(0, sy - r), min(SIZE, sy + r + 1), max(0, sx - r), min(SIZE, sx + r + 1)
    yy, xx = np.mgrid[y0:y1, x0:x1]
    disk = (yy - sy) ** 2 + (xx - sx) ** 2 <= r * r
    start_labels = np.unique(labels[y0:y1, x0:x1][disk])
    start_labels = start_labels[start_labels > 0]
    issues = []
    targets = content_targets(w["layout"])
    if start_labels.size == 0:
        issues.append({"label": ps["label"], "loc_cm": loc_cm(ps), "reason": f"PlayerStart {o['road_reach_m']:.0f} m 안에 도로 셀 없음"})
        return {"passed": False, "score": 0.0, "issues": issues, "metrics": {"source": source, "road_cells": int(passable.sum()), "components": int(n_lab)},
                "summary": "마을에서 도로를 찾지 못함"}
    comp = np.isin(labels, start_labels)
    dist_comp = ndimage.distance_transform_edt(~comp)
    dist_any = ndimage.distance_transform_edt(~passable)
    reached = 0
    per = []
    for m in targets:
        ix, iy = to_grid(*marker_xy_m(m))
        if not in_grid(ix, iy):
            issues.append({"label": m["label"], "loc_cm": loc_cm(m), "reason": "지도 밖"})
            continue
        dc, da = float(dist_comp[iy, ix]), float(dist_any[iy, ix])
        per.append({"label": m["label"], "road_m": round(da, 1), "connected_road_m": round(dc, 1)})
        if dc <= o["road_reach_m"]:
            reached += 1
        elif da <= o["road_reach_m"]:
            issues.append({"label": m["label"], "loc_cm": loc_cm(m), "reason": f"도로가 {da:.1f} m 거리에 있으나 마을 도로망과 끊김 (연결 도로까지 {dc:.1f} m)"})
        else:
            issues.append({"label": m["label"], "loc_cm": loc_cm(m), "reason": f"가장 가까운 도로 {da:.1f} m > 허용 {o['road_reach_m']:.0f} m"})
    score = reached / len(targets) if targets else 0.0
    return {"passed": not issues and bool(targets), "score": score, "issues": issues,
            "metrics": {"source": source, "road_cells": int(passable.sum()), "components": int(n_lab), "start_components": int(start_labels.size), "targets": per},
            "summary": f"대상 {len(targets)}개 중 {reached}개 도달 (도로 {source}, 연결 성분 {n_lab}개, 허용 {o['road_reach_m']:.0f} m)"}


def water_mask(w):
    h = w["h"]
    low = h < WATER_Z_M
    seeds = []
    for p in w["layout"].get("planes", []):
        if "Water" in str(p.get("material", "")):
            ix, iy = to_grid(p["loc"][0] / 100.0, p["loc"][1] / 100.0)
            if in_grid(ix, iy):
                seeds.append((iy, ix))
    if not seeds:
        return low
    labels, _ = ndimage.label(low, structure=EIGHT)
    keep = {int(labels[iy, ix]) for iy, ix in seeds if labels[iy, ix] > 0}
    return np.isin(labels, list(keep)) if keep else low


def check_terrain_fit(w, o):
    h = w["h"]
    gy, gx = np.gradient(h)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    water = water_mask(w)
    r = int(o["sample_r_m"])
    yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
    disk = yy * yy + xx * xx <= r * r
    targets = [player_start(w["layout"])] + content_targets(w["layout"])
    issues, per, ok = [], [], 0
    for m in targets:
        ix, iy = to_grid(*marker_xy_m(m))
        if not (r <= ix < SIZE - r and r <= iy < SIZE - r):
            issues.append({"label": m["label"], "loc_cm": loc_cm(m), "reason": "지도 가장자리/밖"})
            continue
        sw = slope[iy - r:iy + r + 1, ix - r:ix + r + 1][disk]
        ww = water[iy - r:iy + r + 1, ix - r:ix + r + 1][disk]
        c_slope, c_water = float(slope[iy, ix]), bool(water[iy, ix])
        steep = float((sw > o["max_slope_deg"]).mean())
        rec = {"label": m["label"], "height_m": round(float(h[iy, ix]), 2), "center_slope_deg": round(c_slope, 1), "max_slope_deg": round(float(sw.max()), 1), "steep_frac": round(steep, 2), "water_frac": round(float(ww.mean()), 2)}
        per.append(rec)
        reasons = []
        if c_water:
            reasons.append(f"중심 높이 {h[iy, ix]:.2f} m 가 수면 {WATER_Z_M} m 아래")
        if c_slope > o["max_slope_deg"]:
            reasons.append(f"중심 경사 {c_slope:.1f}° > {o['max_slope_deg']:.0f}°")
        if steep > o["steep_frac"]:
            reasons.append(f"반경 {r} m 표본의 {steep * 100:.0f}% 가 경사 {o['max_slope_deg']:.0f}° 초과 (허용 {o['steep_frac'] * 100:.0f}%)")
        if reasons:
            issues.append({"label": m["label"], "loc_cm": loc_cm(m), "reason": "; ".join(reasons)})
        else:
            ok += 1
    score = ok / len(targets) if targets else 0.0
    return {"passed": not issues, "score": score, "issues": issues, "metrics": {"targets": per},
            "summary": f"대상 {len(targets)}개 중 {ok}개 적합 (최대 경사 {o['max_slope_deg']:.0f}°, 표본 반경 {r} m, 수면 {WATER_Z_M} m)"}


def check_streaming_safety(w, o):
    ox, oy = o["slot_origin_cm"]
    R = o["load_radius_cm"]
    slots = [(ox + k * o["slot_pitch_cm"], oy) for k in range(int(o["slots"]))]
    issues = []
    for i in range(len(slots)):
        for j in range(i + 1, len(slots)):
            d = math.hypot(slots[i][0] - slots[j][0], slots[i][1] - slots[j][1])
            if d < 2.0 * R:
                issues.append({"label": f"slot{i} ~ slot{j}", "loc_cm": [slots[i][0], slots[i][1]], "reason": f"원점 거리 {d:.0f} cm < 로딩 반경 합 {2 * R:.0f} cm (겹침 {2 * R - d:.0f} cm)"})
    half = w["layout"].get("region", {}).get("size_m", SIZE - 1) * 100.0 * 0.5
    for k, (sx, sy) in enumerate(slots):
        cx, cy = min(max(sx, -half), half), min(max(sy, -half), half)
        d = math.hypot(sx - cx, sy - cy)
        if d < R:
            issues.append({"label": f"slot{k} ~ 야외 지역", "loc_cm": [sx, sy], "reason": f"야외 경계까지 {d:.0f} cm < 로딩 반경 {R:.0f} cm"})
    passed = not issues
    return {"passed": passed, "score": 1.0 if passed else 0.0, "issues": issues,
            "metrics": {"slots": slots, "load_radius_cm": R, "required_pitch_cm": 2.0 * R},
            "summary": f"슬롯 {len(slots)}개, 간격 {o['slot_pitch_cm']:.0f} cm, 로딩 반경 {R:.0f} cm (겹치지 않으려면 간격 ≥ {2 * R:.0f} cm)"}


def geodesic_length(cells):
    cell_set = set(cells)
    nbrs = [(-1, -1, math.sqrt(2)), (-1, 0, 1.0), (-1, 1, math.sqrt(2)), (0, -1, 1.0), (0, 1, 1.0), (1, -1, math.sqrt(2)), (1, 0, 1.0), (1, 1, math.sqrt(2))]

    def farthest(src):
        dist = {src: 0.0}
        pq = [(0.0, src)]
        far, far_d = src, 0.0
        while pq:
            d, c = heapq.heappop(pq)
            if d > dist.get(c, math.inf):
                continue
            if d > far_d:
                far, far_d = c, d
            for dy, dx, cost in nbrs:
                n = (c[0] + dy, c[1] + dx)
                if n in cell_set and d + cost < dist.get(n, math.inf):
                    dist[n] = d + cost
                    heapq.heappush(pq, (d + cost, n))
        return far, far_d

    a, _ = farthest(cells[0])
    b, length = farthest(a)
    return length, a, b


def check_play_density(w, o):
    passable, source = passable_mask(w, o)
    content = np.zeros((SIZE, SIZE), dtype=bool)
    for m in [player_start(w["layout"])] + content_targets(w["layout"]):
        ix, iy = to_grid(*marker_xy_m(m))
        if in_grid(ix, iy):
            content[iy, ix] = True
    if not content.any() or not passable.any():
        return {"passed": False, "score": 0.0, "issues": [{"label": "-", "loc_cm": [0, 0], "reason": "콘텐츠 또는 도로 없음"}], "metrics": {}, "summary": "측정 불가"}
    far = passable & (ndimage.distance_transform_edt(~content) > o["content_far_m"])
    labels, n = ndimage.label(far, structure=EIGHT)
    segments, issues = [], []
    for k in range(1, n + 1):
        ys, xs = np.nonzero(labels == k)
        if ys.size < 4:
            continue
        length, a, b = geodesic_length(list(zip(ys.tolist(), xs.tolist())))
        mid = (int((a[1] + b[1]) / 2), int((a[0] + b[0]) / 2))
        seg = {"length_m": round(length, 1), "cells": int(ys.size), "from_cm": [(a[1] - HALF) * 100.0, (a[0] - HALF) * 100.0], "to_cm": [(b[1] - HALF) * 100.0, (b[0] - HALF) * 100.0]}
        segments.append(seg)
        if length > o["empty_max_m"]:
            issues.append({"label": f"빈 도로 구간 {len(segments)}", "loc_cm": [(mid[0] - HALF) * 100.0, (mid[1] - HALF) * 100.0],
                           "reason": f"콘텐츠에서 {o['content_far_m']:.0f} m 이상 떨어진 도로 {length:.0f} m 연속 > 허용 {o['empty_max_m']:.0f} m ({seg['from_cm']} → {seg['to_cm']})"})
    segments.sort(key=lambda s: -s["length_m"])
    longest = segments[0]["length_m"] if segments else 0.0
    passed = not issues
    return {"passed": passed, "score": 1.0 if passed else 0.0, "issues": issues,
            "metrics": {"source": source, "empty_segments": segments[:10], "longest_m": longest},
            "summary": f"콘텐츠 {o['content_far_m']:.0f} m 밖 도로 구간 {len(segments)}개, 최장 {longest:.0f} m (허용 ≤ {o['empty_max_m']:.0f} m)"}


CHECKS = [
    ("entrance_spacing", check_entrance_spacing), ("poi_density", check_poi_density), ("road_reach", check_road_reach),
    ("terrain_fit", check_terrain_fit), ("streaming_safety", check_streaming_safety), ("play_density", check_play_density),
]


def run_validation(d, opts=None):
    o = dict(DEFAULTS)
    if opts:
        o.update({k: v for k, v in opts.items() if v is not None})
    w = load_world(d)
    checks = {}
    total = 0.0
    for cid, fn in CHECKS:
        res = fn(w, o)
        res["weight"] = WEIGHTS[cid]
        res["name"] = NAMES[cid]
        res["points"] = round(WEIGHTS[cid] * res["score"], 1)
        total += res["points"]
        checks[cid] = res
    return {"dir": os.path.abspath(d), "seed": w["layout"].get("seed"), "score": round(total, 1), "passed": all(c["passed"] for c in checks.values()),
            "failed": [cid for cid, c in checks.items() if not c["passed"]], "checks": checks, "options": {k: v for k, v in o.items()}}


def write_report(result, path):
    lines = [f"# 월드 검증 리포트 — {os.path.basename(result['dir'])} (seed {result['seed']})", "",
             f"- 점수: **{result['score']} / 100**", f"- 결과: **{'통과' if result['passed'] else '실패'}** (실패 항목: {', '.join(result['failed']) or '없음'})",
             f"- 폴더: `{result['dir']}`", "", "| 항목 | 결과 | 점수 | 요약 |", "|---|---|---|---|"]
    for cid, c in result["checks"].items():
        lines.append(f"| {c['name']} (`{cid}`) | {'통과' if c['passed'] else '실패'} | {c['points']} / {c['weight']} | {c['summary']} |")
    for cid, c in result["checks"].items():
        if not c["issues"]:
            continue
        lines += ["", f"## {c['name']} — 실패 위치", "", "| 대상 | 위치 (cm) | 원인 |", "|---|---|---|"]
        for it in c["issues"]:
            lines.append(f"| {it['label']} | ({it['loc_cm'][0]:.0f}, {it['loc_cm'][1]:.0f}) | {it['reason']} |")
    t = result["checks"]["terrain_fit"]["metrics"].get("targets", [])
    if t:
        lines += ["", "## 지형 표본", "", "| 대상 | 높이 m | 중심 경사° | 최대 경사° | 급경사 비율 | 수면 비율 |", "|---|---|---|---|---|---|"]
        for r in t:
            lines.append(f"| {r['label']} | {r['height_m']} | {r['center_slope_deg']} | {r['max_slope_deg']} | {r['steep_frac']} | {r['water_frac']} |")
    rt = result["checks"]["road_reach"]["metrics"].get("targets", [])
    if rt:
        lines += ["", "## 도로 거리", "", "| 대상 | 가장 가까운 도로 m | 마을 연결 도로 m |", "|---|---|---|"]
        for r in rt:
            lines.append(f"| {r['label']} | {r['road_m']} | {r['connected_road_m']} |")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write("\n".join(lines) + "\n")


def add_threshold_args(ap):
    ap.add_argument("--slots", type=int, default=None)
    ap.add_argument("--slot-pitch-cm", dest="slot_pitch_cm", type=float, default=None)
    ap.add_argument("--load-radius-cm", dest="load_radius_cm", type=float, default=None)
    ap.add_argument("--entrance-min-m", dest="entrance_min_m", type=float, default=None)
    ap.add_argument("--poi-min-m", dest="poi_min_m", type=float, default=None)
    ap.add_argument("--road-reach-m", dest="road_reach_m", type=float, default=None)
    ap.add_argument("--road-weight", dest="road_weight", type=int, default=None)
    ap.add_argument("--max-slope-deg", dest="max_slope_deg", type=float, default=None)
    ap.add_argument("--steep-frac", dest="steep_frac", type=float, default=None)
    ap.add_argument("--content-far-m", dest="content_far_m", type=float, default=None)
    ap.add_argument("--empty-max-m", dest="empty_max_m", type=float, default=None)


def opts_from_args(args):
    return {k: getattr(args, k, None) for k in DEFAULTS if hasattr(args, k)}


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join(root, "Saved", "WorldGen", "AshenVale"))
    ap.add_argument("--report", default=None)
    ap.add_argument("--json", default=None)
    add_threshold_args(ap)
    args = ap.parse_args()
    result = run_validation(args.dir, opts_from_args(args))
    report = args.report or os.path.join(args.dir, "report.md")
    jpath = args.json or os.path.join(args.dir, "validation.json")
    write_report(result, report)
    with open(jpath, "w", encoding="utf-8") as fp:
        json.dump(result, fp, ensure_ascii=False, indent=1, default=float)
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    print(f"score {result['score']} / 100  {'PASS' if result['passed'] else 'FAIL'}  failed: {', '.join(result['failed']) or '-'}")
    for cid, c in result["checks"].items():
        print(f"  [{'OK' if c['passed'] else 'NG'}] {cid:18s} {c['points']:>5}/{c['weight']:<3} {c['summary']}")
        for it in c["issues"]:
            print(f"        - {it['label']} @({it['loc_cm'][0]:.0f}, {it['loc_cm'][1]:.0f}) cm: {it['reason']}")
    print("report:", report)
    sys.exit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()

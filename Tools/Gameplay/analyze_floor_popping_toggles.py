"""실행 환경: 시스템 Python(numpy·Pillow). 경로 toggle_probe 로 찍은 run 폴더(frames.csv·grabs.csv·plan.json)를 읽어, 카메라가 멈춘 채 콘솔 변수 하나를 켰을 때 화면이 바뀌는 양을 끈 상태와 견준다.
실행 명령: python -B Tools/Gameplay/analyze_floor_popping_toggles.py --run <run 이름> [--window 1.0] [--cell 16]
출력: Saved/Gameplay/FloorPopping/<run>/toggles.json·toggles.md·toggles/<지점>_<번호>.png(끈 상태 평균|켠 상태 평균|차이 열지도) + 표준 출력 표
상태: GF-07 원인 판별(2026-11-20). 차이 = 켠 구간 마지막 window 초 평균 화면과 직전 끈 구간 마지막 window 초 평균 화면의 칸별 평균 절대차. 잡음 = 연속한 두 끈 구간끼리의 같은 값.
"""
import argparse
import csv
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUTPUT_BASE = os.path.join(ROOT, "Saved", "Gameplay", "FloorPopping")
GRAB_LATENCY_SECONDS = 0.08
HUD_MASK = (0.38, 0.88, 0.62, 1.0)
HEAT_GAIN = 6.0


def load_rows(run_dir):
    with open(os.path.join(run_dir, "frames.csv"), encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    epochs = np.array([float(row["real_epoch"]) for row in rows])
    segments = np.array([int(row["seg"]) for row in rows])
    return epochs, segments


def load_grabs(run_dir):
    with open(os.path.join(run_dir, "grabs.csv"), encoding="utf-8") as handle:
        grabs = list(csv.DictReader(handle))
    middles = np.array([(float(grab["epoch_start"]) + float(grab["epoch_end"])) / 2.0 for grab in grabs])
    return grabs, middles


def segment_window(epochs, segments, index, window):
    rows = np.flatnonzero(segments == index)
    if rows.size == 0:
        return None
    end = epochs[rows[-1]]
    return end - window, end


def mean_image(grabs, middles, span):
    picked = [grabs[position]["path"] for position in np.flatnonzero((middles - GRAB_LATENCY_SECONDS >= span[0]) & (middles - GRAB_LATENCY_SECONDS <= span[1]))]
    if not picked:
        return None, 0
    total = None
    for path in picked:
        image = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32) / 255.0
        total = image if total is None else total + image
    return total / len(picked), len(picked)


def cell_difference(first, second, cell):
    height, width = first.shape[:2]
    difference = np.abs(first - second).mean(axis=2)
    left, top, right, bottom = HUD_MASK
    difference[int(top * height):int(bottom * height), int(left * width):int(right * width)] = 0.0
    rows, columns = height // cell, width // cell
    return difference, difference[:rows * cell, :columns * cell].reshape(rows, cell, columns, cell).mean(axis=(1, 3))


def write_panel(path, base, toggled, difference, title):
    tiles = [Image.fromarray((np.clip(image, 0, 1) * 255).astype(np.uint8)) for image in (base, toggled)]
    heat = np.clip(difference * HEAT_GAIN, 0, 1)
    tiles.append(Image.fromarray((np.stack([heat, heat * 0.35, np.zeros_like(heat)], axis=2) * 255).astype(np.uint8)))
    width, height = tiles[0].size
    scale = 0.5
    panel = Image.new("RGB", (int(width * scale) * 3, int(height * scale) + 18))
    for position, tile in enumerate(tiles):
        panel.paste(tile.resize((int(width * scale), int(height * scale))), (position * int(width * scale), 18))
    ImageDraw.Draw(panel).text((4, 3), title, fill=(255, 255, 0))
    panel.save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run", required=True)
    parser.add_argument("--window", type=float, default=1.0)
    parser.add_argument("--cell", type=int, default=16)
    args = parser.parse_args()
    run_dir = os.path.join(OUTPUT_BASE, args.run)
    with open(os.path.join(run_dir, "plan.json"), encoding="utf-8") as handle:
        plan = json.load(handle)
    epochs, segments = load_rows(run_dir)
    grabs, middles = load_grabs(run_dir)
    os.makedirs(os.path.join(run_dir, "toggles"), exist_ok=True)
    plan_segments = plan["segments"]
    results = []
    spot = -1
    previous_off = None
    noise_values = []
    for position, segment in enumerate(plan_segments):
        if segment["kind"] == "teleport":
            spot += 1
            previous_off = None
            continue
        if segment["kind"] != "stare":
            continue
        commands = segment.get("console", [])
        span = segment_window(epochs, segments, segment["index"], args.window)
        if span is None:
            continue
        image, count = mean_image(grabs, middles, span)
        if image is None:
            continue
        is_on = bool(commands) and command_is_on(commands[0])
        if not is_on:
            if previous_off is not None:
                _, cells = cell_difference(previous_off, image, args.cell)
                noise_values.append(cells.ravel())
            previous_off = image
            continue
        if previous_off is None:
            continue
        difference, cells = cell_difference(previous_off, image, args.cell)
        name = commands[0]
        record = {"spot": spot, "segment": segment["index"], "command": name, "frames": count, "mean": float(cells.mean()), "p99": float(np.percentile(cells, 99)), "cells": cells}
        results.append(record)
        write_panel(os.path.join(run_dir, "toggles", f"spot{spot}_seg{segment['index']:02d}.png"), previous_off, image, difference, f"spot {spot} | {name} | mean {record['mean']:.4f} p99 {record['p99']:.3f}")
    noise = np.concatenate(noise_values) if noise_values else np.array([0.0])
    threshold = float(max(0.02, np.percentile(noise, 99) * 1.5))
    lines = [f"# {args.run} 정지 전환 비교", "", f"잡음(끈 구간끼리 칸 차이) 99분위 {np.percentile(noise, 99):.4f}, 바뀐 칸 기준 {threshold:.4f}", "", "| 지점 | 명령 | 평균 차 | 99분위 | 바뀐 칸 비율 |", "|---|---|---|---|---|"]
    table = []
    for record in results:
        changed = float((record.pop("cells") > threshold).mean())
        record["changed_fraction"] = changed
        table.append(record)
        lines.append(f"| {record['spot']} | {record['command']} | {record['mean']:.4f} | {record['p99']:.3f} | {changed:.3f} |")
    with open(os.path.join(run_dir, "toggles.json"), "w", encoding="utf-8") as handle:
        json.dump({"threshold": threshold, "noise_p99": float(np.percentile(noise, 99)), "results": table}, handle, ensure_ascii=False, indent=1)
    with open(os.path.join(run_dir, "toggles.md"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def command_is_on(command):
    name, _, value = command.partition(" ")
    defaults = {"foliage.ForceLOD": "-1", "r.ForceLOD": "-1", "r.Streaming.FullyLoadUsedTextures": "0", "grass.CullDistanceScale": "1", "r.Shadow.Virtual.Enable": "1", "r.MotionBlurQuality": "4", "r.Velocity.EnableVertexDeformation": "2", "r.AntiAliasingMethod": "4"}
    return defaults.get(name) is not None and value.strip() != defaults[name]


if __name__ == "__main__":
    sys.exit(main())

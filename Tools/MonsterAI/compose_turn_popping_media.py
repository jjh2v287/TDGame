"""실행 환경: 시스템 Python 3.12(PIL·matplotlib·numpy). 에디터 불필요.
실행 명령: python Tools/MonsterAI/compose_turn_popping_media.py --replay <label> --species Hyena [--monster 0] [--start 2] [--seconds 8] [--slow 2] [--span 800] | --clip-before <label> --clip-after <label> --species Hyena [--slow 2] [--crop 0.6] [--clip-first 0 --clip-step 2 --clip-frames 60 --clip-width 480 --colors 64] [--webp-dir <폴더>(같은 프레임을 손실 압축 WEBP 로도, 보기용·커밋 안 함)] [--caption-before 글 --caption-after 글] | --rate-before <label> --rate-after <label> --species Hyena [--monster 0] [--start 2] [--seconds 8] [--rate-limit 540] [--task M3-43] (PowerShell)
출력: Docs/MonsterAI_CombatSim/measurements/<task>/media/ — replay_<종>.gif(같은 측정 한 판에서 왼쪽 논리 방향 = 고치기 전 보이던 몸, 오른쪽 보이는 메시 방향 = 고친 뒤를 위에서 본 나란히 재생, 주목 몬스터(--monster)를 가운데 두고 한 변 --span cm), yaw_<종>.png(몸 방향·각속도 시간 그래프), clip_<종>.gif(끔/켬 두 판의 실제 PIE 화면 나란히, 두 판은 같은 장면 설정이지만 프레임 단위로 같지는 않다), rate_<종>.png(두 판의 논리 방향(캡슐) 각속도를 위·아래로, 판마다 자기 시계).
상태: 현행(M3-42, M3-43 에 캡션·두 판 각속도 그래프 추가). 원본은 pie_measure_turn_popping.py 의 report.json 이 가리키는 raw/<종>.json, 촬영은 같은 라벨 frames/<종>/.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
MEASUREMENT_ROOT = ROOT / "Docs" / "MonsterAI_CombatSim" / "measurements"
FONT_PATH = Path("C:/Windows/Fonts/malgun.ttf")
PANEL_SIZE = 520
BODY_LENGTH_CM = 150.0
BODY_WIDTH_CM = 60.0
REPLAY_FPS = 30.0
COLORS = [(214, 69, 65), (52, 120, 200), (60, 160, 90), (200, 140, 30), (140, 80, 180)]
COLUMN_TIME, COLUMN_DELTA, COLUMN_X, COLUMN_Y, COLUMN_YAW, COLUMN_MESH = range(6)
COLUMN_ALIVE = 12


def wrap(angle):
    return (angle + 180.0) % 360.0 - 180.0


def font(size):
    return ImageFont.truetype(str(FONT_PATH), size) if FONT_PATH.exists() else ImageFont.load_default()


def load_raw(task, label, species):
    report = json.loads((MEASUREMENT_ROOT / task / label / "report.json").read_text(encoding="utf-8"))
    return json.loads((ROOT / report["species"][species]["raw"]).read_text(encoding="utf-8"))


def visual_heading(frame, info):
    return wrap(frame[COLUMN_MESH] - info.get("mesh_yaw", 0.0))


def window_frames(frames, start_seconds, seconds):
    if not frames:
        return []
    origin = frames[0][COLUMN_TIME]
    return [frame for frame in frames if start_seconds <= frame[COLUMN_TIME] - origin <= start_seconds + seconds and frame[COLUMN_ALIVE]]


def body_polygon(center, heading_degrees, to_pixel):
    radians = math.radians(heading_degrees)
    forward = (math.cos(radians), math.sin(radians))
    right = (-forward[1], forward[0])
    half_length, half_width = BODY_LENGTH_CM / 2.0, BODY_WIDTH_CM / 2.0
    corners = [(half_length * 1.25, 0.0), (half_length * 0.6, half_width), (-half_length, half_width * 0.8), (-half_length, -half_width * 0.8), (half_length * 0.6, -half_width)]
    return [to_pixel(center[0] + forward[0] * along + right[0] * side, center[1] + forward[1] * along + right[1] * side) for along, side in corners]


def make_projection(center, span):
    scale = PANEL_SIZE / span

    def to_pixel(x, y):
        return (PANEL_SIZE / 2.0 + (y - center[1]) * scale, PANEL_SIZE / 2.0 - (x - center[0]) * scale)
    return to_pixel


def draw_panel(title, frame_index, tracks, player, to_pixel, heading_of, rate_text):
    panel = Image.new("RGB", (PANEL_SIZE, PANEL_SIZE), (246, 244, 238))
    draw = ImageDraw.Draw(panel)
    for grid in range(0, PANEL_SIZE, 52):
        draw.line([(grid, 0), (grid, PANEL_SIZE)], fill=(232, 229, 220))
        draw.line([(0, grid), (PANEL_SIZE, grid)], fill=(232, 229, 220))
    if player and frame_index < len(player):
        px, py = to_pixel(player[frame_index][2], player[frame_index][3])
        draw.ellipse([px - 9, py - 9, px + 9, py + 9], fill=(30, 30, 30))
    for color_index, (info, frames) in enumerate(tracks):
        if frame_index >= len(frames):
            continue
        frame = frames[frame_index]
        polygon = body_polygon((frame[COLUMN_X], frame[COLUMN_Y]), heading_of(frame, info), to_pixel)
        draw.polygon(polygon, fill=COLORS[color_index % len(COLORS)], outline=(20, 20, 20))
    draw.text((12, 8), title, fill=(20, 20, 20), font=font(20))
    draw.text((12, PANEL_SIZE - 30), rate_text, fill=(60, 60, 60), font=font(16))
    return panel


def rate_at(frames, index, heading_of, info):
    if index <= 0 or index >= len(frames):
        return 0.0
    delta = frames[index][COLUMN_TIME] - frames[index - 1][COLUMN_TIME]
    return abs(wrap(heading_of(frames[index], info) - heading_of(frames[index - 1], info))) / delta if delta > 0 else 0.0


def compose_replay(arguments):
    raw = load_raw(arguments.task, arguments.replay, arguments.species)
    tracks = [(monster["info"], window_frames(monster["frames"], arguments.start, arguments.seconds)) for monster in raw["monsters"]]
    tracks = [(info, frames) for info, frames in tracks if frames]
    if not tracks:
        sys.exit("no frames in window")
    origin = raw["monsters"][0]["frames"][0][COLUMN_TIME]
    player = [frame for frame in raw["player"] if arguments.start <= frame[0] - origin <= arguments.start + arguments.seconds]
    focus_info, focus_frames = tracks[min(arguments.monster, len(tracks) - 1)]
    logic_heading = lambda frame, info: frame[COLUMN_YAW]
    sample_step = max(1, round((len(focus_frames) / max(arguments.seconds, 1e-3)) / REPLAY_FPS))
    images = []
    for index in range(0, len(focus_frames), sample_step):
        seconds = focus_frames[index][COLUMN_TIME] - origin
        to_pixel = make_projection((focus_frames[index][COLUMN_X], focus_frames[index][COLUMN_Y]), arguments.span)
        left = draw_panel("고치기 전: 몸 = 논리 방향", index, tracks, player, to_pixel, logic_heading, f"t={seconds:5.2f}s  빨간 몸 {rate_at(focus_frames, index, logic_heading, focus_info):5.0f}도/초")
        right = draw_panel("고친 뒤: 보이는 몸만 부드럽게", index, tracks, player, to_pixel, visual_heading, f"t={seconds:5.2f}s  빨간 몸 {rate_at(focus_frames, index, visual_heading, focus_info):5.0f}도/초")
        canvas = Image.new("RGB", (PANEL_SIZE * 2 + 8, PANEL_SIZE), (255, 255, 255))
        canvas.paste(left, (0, 0))
        canvas.paste(right, (PANEL_SIZE + 8, 0))
        images.append(canvas.convert("P", palette=Image.ADAPTIVE, colors=64))
    out_dir = MEASUREMENT_ROOT / arguments.task / "media"
    out_dir.mkdir(parents=True, exist_ok=True)
    gif_path = out_dir / f"replay_{arguments.species}.gif"
    images[0].save(gif_path, save_all=True, append_images=images[1:], duration=round(1000.0 / REPLAY_FPS * arguments.slow), loop=0, optimize=True)
    chart_path = out_dir / f"yaw_{arguments.species}.png"
    write_yaw_chart(chart_path, focus_info, focus_frames, origin, arguments)
    print(json.dumps({"replay": str(gif_path.relative_to(ROOT)), "chart": str(chart_path.relative_to(ROOT)), "frames": len(images)}, ensure_ascii=False))


def unwrap(values):
    result = [values[0]]
    for value in values[1:]:
        result.append(result[-1] + wrap(value - result[-1]))
    return result


def write_yaw_chart(path, info, frames, origin, arguments):
    times = [frame[COLUMN_TIME] - origin for frame in frames]
    logic = unwrap([frame[COLUMN_YAW] for frame in frames])
    visual = unwrap([visual_heading(frame, info) for frame in frames])
    offset = 360.0 * round((logic[0] - visual[0]) / 360.0)
    visual = [value + offset for value in visual]
    logic_rate = [0.0] + [abs(logic[i] - logic[i - 1]) / max(times[i] - times[i - 1], 1e-6) for i in range(1, len(times))]
    visual_rate = [0.0] + [abs(visual[i] - visual[i - 1]) / max(times[i] - times[i - 1], 1e-6) for i in range(1, len(times))]
    plt.rcParams["font.family"] = "Malgun Gothic"
    plt.rcParams["axes.unicode_minus"] = False
    figure, (top, bottom) = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
    top.plot(times, logic, color="#999999", linewidth=1.2, label="논리 방향(고치기 전 보이던 몸)")
    top.plot(times, visual, color="#2f6db5", linewidth=1.8, label="보이는 몸(고친 뒤)")
    top.set_ylabel("몸 방향(도)")
    top.legend(loc="upper left")
    bottom.plot(times, logic_rate, color="#999999", linewidth=1.0, label="논리 각속도")
    bottom.plot(times, visual_rate, color="#2f6db5", linewidth=1.6, label="보이는 몸 각속도")
    bottom.axhline(720.0, color="#c0392b", linestyle="--", linewidth=1.0, label="720도/초")
    bottom.set_ylim(0, max(900.0, min(max(logic_rate + [0.0]) * 1.05, 4000.0)))
    bottom.set_ylabel("각속도(도/초)")
    bottom.set_xlabel("게임 시각(초)")
    bottom.legend(loc="upper left")
    figure.suptitle(f"{arguments.species} {info['name']} — 같은 판의 논리 방향 대 보이는 몸")
    figure.tight_layout()
    figure.savefig(path, dpi=110)
    plt.close(figure)


def captured_frames(raw):
    return [shot for shot in raw.get("capture", []) if shot.get("saved")]


def crop_center(image, fraction):
    if fraction >= 1.0:
        return image
    width, height = round(image.width * fraction), round(image.height * fraction)
    left, top = (image.width - width) // 2, (image.height - height) // 2
    return image.crop((left, top, left + width, top + height))


def fit_width(image, width):
    return image if image.width == width else image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)


def caption(image, text):
    canvas = Image.new("RGB", (image.width, image.height + 34), (20, 20, 20))
    canvas.paste(image, (0, 34))
    ImageDraw.Draw(canvas).text((10, 4), text, fill=(240, 240, 240), font=font(20))
    return canvas


def compose_clip(arguments):
    before = captured_frames(load_raw(arguments.task, arguments.clip_before, arguments.species))
    after = captured_frames(load_raw(arguments.task, arguments.clip_after, arguments.species))
    indices = list(range(arguments.clip_first, min(len(before), len(after)), arguments.clip_step))[:arguments.clip_frames]
    if not indices:
        sys.exit("no captured frames")
    images = []
    full_frames = []
    durations = []
    for position, index in enumerate(indices):
        left = caption(fit_width(crop_center(Image.open(before[index]["path"]).convert("RGB"), arguments.crop), arguments.clip_width), arguments.caption_before)
        right = caption(fit_width(crop_center(Image.open(after[index]["path"]).convert("RGB"), arguments.crop), arguments.clip_width), arguments.caption_after)
        canvas = Image.new("RGB", (left.width + right.width + 6, max(left.height, right.height)), (255, 255, 255))
        canvas.paste(left, (0, 0))
        canvas.paste(right, (left.width + 6, 0))
        images.append(canvas.convert("P", palette=Image.ADAPTIVE, colors=arguments.colors))
        full_frames.append(canvas)
        next_index = indices[position + 1] if position + 1 < len(indices) else index
        step = (after[next_index]["captured_seconds"] or 0.0) - (after[index]["captured_seconds"] or 0.0) if next_index != index else arguments.clip_step / REPLAY_FPS
        durations.append(max(20, round(1000.0 * max(step, 1.0 / 60.0) * arguments.slow)))
    out_dir = MEASUREMENT_ROOT / arguments.task / "media"
    out_dir.mkdir(parents=True, exist_ok=True)
    gif_path = out_dir / f"clip_{arguments.species}.gif"
    images[0].save(gif_path, save_all=True, append_images=images[1:], duration=durations, loop=0, optimize=True)
    result = {"clip": str(gif_path.relative_to(ROOT)), "frames": len(images), "bytes": gif_path.stat().st_size}
    if arguments.webp_dir:
        webp_path = Path(arguments.webp_dir) / f"clip_{arguments.species}.webp"
        webp_path.parent.mkdir(parents=True, exist_ok=True)
        full_frames[0].save(webp_path, save_all=True, append_images=full_frames[1:], duration=durations, loop=0, format="WEBP", quality=80, method=4)
        result.update({"webp": str(webp_path), "webp_bytes": webp_path.stat().st_size})
    print(json.dumps(result, ensure_ascii=False))


def logic_rates(raw, monster_index, start_seconds, seconds):
    monsters = raw["monsters"]
    frames = window_frames(monsters[min(monster_index, len(monsters) - 1)]["frames"], start_seconds, seconds)
    if len(frames) < 2:
        return [], []
    origin = monsters[0]["frames"][0][COLUMN_TIME]
    times = [frame[COLUMN_TIME] - origin for frame in frames]
    yaws = unwrap([frame[COLUMN_YAW] for frame in frames])
    rates = [abs(yaws[i] - yaws[i - 1]) / max(times[i] - times[i - 1], 1e-6) for i in range(1, len(times))]
    return times[1:], rates


def compose_rate_chart(arguments):
    before_times, before_rates = logic_rates(load_raw(arguments.task, arguments.rate_before, arguments.species), arguments.monster, arguments.start, arguments.seconds)
    after_times, after_rates = logic_rates(load_raw(arguments.task, arguments.rate_after, arguments.species), arguments.monster, arguments.start, arguments.seconds)
    if not before_rates or not after_rates:
        sys.exit("no frames in window")
    plt.rcParams["font.family"] = "Malgun Gothic"
    plt.rcParams["axes.unicode_minus"] = False
    figure, (top, bottom) = plt.subplots(2, 1, figsize=(10, 6), sharex=True, sharey=True)
    top.plot(before_times, before_rates, color="#999999", linewidth=1.2, label=arguments.caption_before)
    bottom.plot(after_times, after_rates, color="#2f6db5", linewidth=1.6, label=arguments.caption_after)
    for axis in (top, bottom):
        axis.axhline(arguments.rate_limit, color="#c0392b", linestyle="--", linewidth=1.0, label=f"{arguments.rate_limit:.0f}도/초")
        axis.set_ylabel("캡슐 각속도(도/초)")
        axis.legend(loc="upper left")
    top.set_ylim(0, max(arguments.rate_limit * 1.25, min(max(before_rates + after_rates) * 1.05, 4000.0)))
    bottom.set_xlabel("게임 시각(초, 판마다 자기 시계)")
    figure.suptitle(f"{arguments.species} 몬스터 {arguments.monster} — 논리 방향(캡슐) 각속도, 같은 장면 설정의 두 판")
    figure.tight_layout()
    out_dir = MEASUREMENT_ROOT / arguments.task / "media"
    out_dir.mkdir(parents=True, exist_ok=True)
    chart_path = out_dir / f"rate_{arguments.species}.png"
    figure.savefig(chart_path, dpi=110)
    plt.close(figure)
    print(json.dumps({"rate_chart": str(chart_path.relative_to(ROOT)), "before_frames": len(before_rates), "after_frames": len(after_rates)}, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description="회전 팝핑 전·후 나란히 보기 만들기(M3-42·M3-43)")
    parser.add_argument("--task", default="M3-42")
    parser.add_argument("--species", required=True)
    parser.add_argument("--replay")
    parser.add_argument("--monster", type=int, default=0)
    parser.add_argument("--start", type=float, default=2.0)
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--slow", type=float, default=2.0)
    parser.add_argument("--span", type=float, default=800.0)
    parser.add_argument("--crop", type=float, default=0.6)
    parser.add_argument("--clip-first", type=int, default=0)
    parser.add_argument("--clip-step", type=int, default=2)
    parser.add_argument("--clip-frames", type=int, default=60)
    parser.add_argument("--clip-width", type=int, default=480)
    parser.add_argument("--colors", type=int, default=64)
    parser.add_argument("--webp-dir")
    parser.add_argument("--clip-before")
    parser.add_argument("--clip-after")
    parser.add_argument("--caption-before", default="고치기 전(VisualTurn 0)")
    parser.add_argument("--caption-after", default="고친 뒤(VisualTurn 1)")
    parser.add_argument("--rate-before")
    parser.add_argument("--rate-after")
    parser.add_argument("--rate-limit", type=float, default=720.0)
    arguments = parser.parse_args()
    if arguments.replay:
        compose_replay(arguments)
    if arguments.clip_before and arguments.clip_after:
        compose_clip(arguments)
    if arguments.rate_before and arguments.rate_after:
        compose_rate_chart(arguments)
    return 0


if __name__ == "__main__":
    sys.exit(main())

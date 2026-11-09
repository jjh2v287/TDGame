"""실행 환경: 시스템 Python(Pillow, 에디터 불필요, PowerShell 또는 Git Bash).
실행 명령: python -B Tools/FluidFX/make_fluid_before_after.py --pair <이름> <전.png> <후.png> [--pair …] [--crop x0 y0 x1 y1 …] [--metrics <전.json> <후.json> …] [--gif <폴더> <패턴> …] [--tile-width 960] [--gif-width 960] --output <png>
출력: --output PNG(--pair 마다 한 행: 이름 | BEFORE | AFTER 축소판(잘라낼 영역은 노란 테), 그 아래 --crop 마다 전·후 1:1 잘라내기, i번째 --metrics(measure_water_look.py 결과 JSON 의 summary)가 있으면 i번째 행 아래에 주요 지표 전 -> 후 캡션, 글자는 영문·숫자만), --gif 마다 <output 이름>-gif<번호>.gif(패턴에 맞는 장을 이름순으로 100 ms 간격, 가로 --gif-width)
상태: 실험(roadmap-v2 2.4 T3, Q1 물 전·후 비교 그림)."""
import argparse
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

BACKGROUND_COLOR = (28, 28, 30)
TEXT_COLOR = (235, 235, 235)
CROP_OUTLINE_COLOR = (255, 214, 0)
GAP_PX = 8
TEXT_LINE_HEIGHT_PX = 22
FONT_SIZE_PX = 16
GIF_FRAME_MILLISECONDS = 100
CAPTION_METRICS = (
    ("region.band_std.low", "low"),
    ("region.band_std.mid", "mid"),
    ("region.band_std.high", "high"),
    ("region.floor_band_std", "floor"),
    ("region.green_over_red", "G/R"),
    ("region.blue_over_red", "B/R"),
    ("frame.far_minus_near_blue_over_green", "farB/G-nearB/G"),
    ("region.sparkle.percent", "sparkle%"),
    ("region.crescent.ratio_three_or_more", "crescent3+"),
    ("frame.void_percent", "void%"),
    ("temporal.low", "t_low"),
    ("temporal.high", "t_high"),
)
BOX_METRIC_PATTERN = re.compile(r"^boxes\.(.+)\.(white_percent|fill_ratio|largest_blob_share)$")
BOX_METRIC_LABELS = {"white_percent": "white%", "fill_ratio": "fill", "largest_blob_share": "largest"}
CAPTION_ITEMS_PER_LINE = 4


def load_font():
    try:
        return ImageFont.load_default(size=FONT_SIZE_PX)
    except TypeError:
        return ImageFont.load_default()


def ascii_text(text):
    return text.encode("ascii", "replace").decode("ascii")


def scale_to_width(image, width):
    height = max(1, round(image.height * width / image.width))
    return image.resize((width, height), Image.LANCZOS)


def text_strip(lines, width, font):
    strip = Image.new("RGB", (width, TEXT_LINE_HEIGHT_PX * len(lines) + GAP_PX // 2), BACKGROUND_COLOR)
    draw = ImageDraw.Draw(strip)
    for index, line in enumerate(lines):
        draw.text((GAP_PX, GAP_PX // 2 + index * TEXT_LINE_HEIGHT_PX), ascii_text(line), fill=TEXT_COLOR, font=font)
    return strip


def stack_vertically(images):
    width = max(image.width for image in images)
    sheet = Image.new("RGB", (width, sum(image.height for image in images)), BACKGROUND_COLOR)
    top = 0
    for image in images:
        sheet.paste(image, (0, top))
        top += image.height
    return sheet


def place_side_by_side(left, right):
    canvas = Image.new("RGB", (left.width + GAP_PX + right.width, max(left.height, right.height)), BACKGROUND_COLOR)
    canvas.paste(left, (0, 0))
    canvas.paste(right, (left.width + GAP_PX, 0))
    return canvas


def outline_crops(tile, source_size, crops):
    scale_x, scale_y = tile.width / source_size[0], tile.height / source_size[1]
    draw = ImageDraw.Draw(tile)
    for x0, y0, x1, y1 in crops:
        draw.rectangle([x0 * scale_x, y0 * scale_y, x1 * scale_x, y1 * scale_y], outline=CROP_OUTLINE_COLOR, width=2)
    return tile


def format_number(value):
    return "-" if value is None else f"{value:.4g}"


def caption_lines(before_summary, after_summary):
    items = []
    for key, label in CAPTION_METRICS:
        if key in before_summary or key in after_summary:
            items.append(f"{label} {format_number(before_summary.get(key))}->{format_number(after_summary.get(key))}")
    for key in sorted(set(before_summary) | set(after_summary)):
        match = BOX_METRIC_PATTERN.match(key)
        if match:
            items.append(f"{match.group(1)} {BOX_METRIC_LABELS[match.group(2)]} {format_number(before_summary.get(key))}->{format_number(after_summary.get(key))}")
    return ["   ".join(items[index:index + CAPTION_ITEMS_PER_LINE]) for index in range(0, len(items), CAPTION_ITEMS_PER_LINE)]


def load_summary(path):
    return json.loads(Path(path).read_text(encoding="utf-8")).get("summary", {})


def build_pair_block(name, before_path, after_path, crops, metrics, tile_width, font):
    before = Image.open(before_path).convert("RGB")
    after = Image.open(after_path).convert("RGB")
    parts = [text_strip([f"{name}    BEFORE: {Path(before_path).name}    AFTER: {Path(after_path).name}"], tile_width * 2 + GAP_PX, font)]
    before_tile = outline_crops(scale_to_width(before, tile_width), before.size, crops)
    after_tile = outline_crops(scale_to_width(after, tile_width), after.size, crops)
    parts.append(place_side_by_side(before_tile, after_tile))
    for crop in crops:
        parts.append(text_strip([f"crop 1:1 [{crop[0]},{crop[1]},{crop[2]},{crop[3]}]  BEFORE | AFTER"], tile_width * 2 + GAP_PX, font))
        parts.append(place_side_by_side(before.crop(tuple(crop)), after.crop(tuple(crop))))
    if metrics is not None:
        lines = caption_lines(load_summary(metrics[0]), load_summary(metrics[1]))
        if lines:
            parts.append(text_strip(lines, tile_width * 2 + GAP_PX, font))
    parts.append(Image.new("RGB", (1, GAP_PX * 2), BACKGROUND_COLOR))
    return stack_vertically(parts)


def write_gif(folder, pattern, gif_path, gif_width):
    frames = sorted(Path(folder).glob(pattern))
    if not frames:
        return 0
    images = [scale_to_width(Image.open(path).convert("RGB"), gif_width).convert("P", palette=Image.ADAPTIVE) for path in frames]
    images[0].save(gif_path, save_all=True, append_images=images[1:], duration=GIF_FRAME_MILLISECONDS, loop=0)
    return len(images)


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Q1 물 전·후 비교 그림(roadmap-v2 2.4 T3)")
    parser.add_argument("--pair", nargs=3, action="append", default=[], metavar=("NAME", "BEFORE", "AFTER"))
    parser.add_argument("--crop", nargs=4, type=int, action="append", default=[], metavar=("X0", "Y0", "X1", "Y1"))
    parser.add_argument("--metrics", nargs=2, action="append", default=[], metavar=("BEFORE_JSON", "AFTER_JSON"))
    parser.add_argument("--gif", nargs=2, action="append", default=[], metavar=("FOLDER", "PATTERN"))
    parser.add_argument("--tile-width", type=int, default=960)
    parser.add_argument("--gif-width", type=int, default=960)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    if not arguments.pair and not arguments.gif:
        parser.error("--pair 또는 --gif 가 하나 이상 필요하다")
    missing = [path for _, before, after in arguments.pair for path in (before, after) if not Path(path).is_file()]
    missing += [path for pair in arguments.metrics for path in pair if not Path(path).is_file()]
    if missing:
        print("파일이 없다:", ", ".join(missing))
        return 1
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    font = load_font()
    if arguments.pair:
        blocks = []
        for index, (name, before, after) in enumerate(arguments.pair):
            metrics = arguments.metrics[index] if index < len(arguments.metrics) else None
            blocks.append(build_pair_block(name, before, after, arguments.crop, metrics, arguments.tile_width, font))
        sheet = stack_vertically(blocks)
        sheet.save(arguments.output)
        print("저장", arguments.output.as_posix(), f"{sheet.width}x{sheet.height}")
    for index, (folder, pattern) in enumerate(arguments.gif, start=1):
        gif_path = arguments.output.with_name(f"{arguments.output.stem}-gif{index}.gif")
        frame_count = write_gif(folder, pattern, gif_path, arguments.gif_width)
        if frame_count == 0:
            print("GIF 장이 없다:", folder, pattern)
            return 1
        print("저장", gif_path.as_posix(), f"{frame_count}장")
    return 0


if __name__ == "__main__":
    sys.exit(main())

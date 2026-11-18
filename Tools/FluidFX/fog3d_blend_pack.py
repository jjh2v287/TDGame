"""실행 환경: 시스템 Python(Pillow 필요, 언리얼 에디터 불필요). fog3d_blend_variants.py 로 찍은 18실행의 장 파일을 읽어 장면 x 품질마다 세 판을 나란히 붙인 움직이는 그림(webp)과 정지 그림 나란히(jpg)를 만들고 index.html 과 한 파일 페이지로 묶는다.
실행 명령: python -B Tools/FluidFX/fog3d_blend_pack.py [--webp-quality 70] [--tile-width 480] [--budget-mb 9.5] [--single Saved/AgentOps/20261117/fog3d-blend-gpu/eye_judgment_blend.html] [--self-test]
출력: Docs/VolumeFog3D/measurements/blend-b4-screen/{index.html, media/*.webp, media/*.jpg}, 한 파일 페이지(--single). 예산(원본 합계 MB)을 넘으면 같은 처리를 세 판과 모든 그림에 적용해 webp 품질을 낮춘다.
상태: 합성 장으로 만든 점검만 확인(--self-test). 실제 장으로 만든 결과는 만든 뒤 크기·그림 수로만 확인한다.
"""
import argparse
import base64
import html
import json
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
for directory in (ROOT / "Tools", ROOT / "Tools" / "FluidFX"):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))
import fog3d_blend_variants as bv  # noqa: E402
import fog3d_calib_report as rep  # noqa: E402

OUTPUT_DIRECTORY = ROOT / "Docs" / "VolumeFog3D" / "measurements" / "blend-b4-screen"
DEFAULT_SINGLE_PATH = ROOT / "Saved" / "AgentOps" / "20261117" / "fog3d-blend-gpu" / "eye_judgment_blend.html"
VERSION_LABELS = {"cur": "지금 식", "b4g100": "새 식 중 (배율 1.0)", "b4g125": "새 식 강 (배율 1.25)"}
VERSION_ORDER = ("cur", "b4g100", "b4g125")
QUALITY_TITLES = {"qh": "높은 품질", "ql": "낮은 품질"}
STILL_TILE_WIDTH = 640
STILL_JPEG_QUALITY = 86
LABEL_HEIGHT = 26
KOREAN_FONT_PATH = "C:/Windows/Fonts/malgun.ttf"
FRAME_DURATION_MILLISECONDS = 100
TITLE = "3D 안개 — 바람 섞기 지금 식과 새 식 비교 (독립 검증 전)"
PAGE_ITEMS = (
    "1. 무엇을 보시나요: 캐릭터가 안개 속을 움직일 때 몸과 칼이 안개에 바람을 넣는 방식을 고쳤습니다. 세 칸은 왼쪽부터 지금 식(승인하신 그대로), 새 식의 '중'(바람 배율 1.0), 새 식의 '강'(바람 배율 1.25)입니다. 위는 높은 품질, 아래는 낮은 품질입니다.",
    "2. 왜 고쳤나요: 승인하신 그림에는 서 있는 동안의 끝없는 휘저음이 들어 있었습니다. 지금 식은 몸의 바람 세기 3.0 에서 바람을 너무 세게 섞어, 캐릭터가 멈춰 서 있어도 몸 주변 바람이 잦아들지 않고 매 순간 방향이 뒤집히며 안개를 계속 휘저었습니다. 걸을 때 남는 자국에도 이 휘저음이 섞여 있었습니다. 새 식은 바람이 목표를 지나치지 않게 섞어서, 멈추면 바람이 잦아들고 안개가 다시 가라앉습니다.",
    "3. 함께 바뀌는 것: 새 식에서는 낮은 품질(초당 계산 횟수가 절반)에서도 높은 품질과 같은 빠르기로 바람이 따라옵니다. 지금 식에서는 낮은 품질이 더 느리게 따라왔습니다. 이 점은 원하시면 되돌릴 수 있습니다.",
    "4. 보는 곳: (1) 걷다 멈춘 뒤 — 서 있는 동안 발밑 안개가 계속 움직이는지, 가라앉는지 (2) 칼을 휘두른 뒤 — 휘두름이 남긴 자국과 그 뒤의 움직임 (3) 걷다가 직각으로 꺾을 때 — 꺾는 자리의 자국.",
    "5. 여쭙는 것: ① 세 칸 가운데 어느 쪽이 보기 좋으신가요? ② 새 식을 고르신다면 '중'과 '강' 가운데 어느 쪽인가요? ③ 기본값을 새 식으로 바꿀까요? (지금 기본값은 지금 식 그대로입니다.)",
    "6. 아직 하지 않은 것: 독립 검증이 끝나지 않은 그림입니다. 검증에서 문제가 나오면 그림을 바꿔 다시 올립니다.",
)
SCENE_ORDER = ("s1walkstop", "s2swing", "s3turn")
STILL_REFERENCE_WORDS = {"s1walkstop": "멈춘 뒤", "s2swing": "휘두름 끝 뒤", "s3turn": "멈춘 뒤"}
MOVIE_RANGE_WORDS = {"s1walkstop": "멈추기 0.5초 전부터 멈춘 뒤 4초까지", "s2swing": "휘두르기 시작부터 4.5초 뒤까지", "s3turn": "걷기 시작부터 멈춘 뒤 2초까지"}
PAGE_STYLE = ("<style>:root{--bg:#eef1f1;--ink:#1a2227;--mut:#55626b;--line:#d3dadd;--sf:#fff}@media(prefers-color-scheme:dark){:root{--bg:#12181b;--ink:#e4eaec;--mut:#9ba8b0;--line:#2d383e;--sf:#1a2226}}"
              "body{background:var(--bg);color:var(--ink);font:16px/1.7 'Malgun Gothic',system-ui,sans-serif;margin:0}.p{max-width:1180px;margin:auto;padding:24px 16px 60px}h1{font-size:1.9rem}"
              "h2{font-size:1.3rem;margin:36px 0 4px}p{color:var(--ink);max-width:72ch}.g{display:flex;flex-direction:column;gap:18px;margin-top:12px}"
              "figure{margin:0;background:var(--sf);border:1px solid var(--line);padding:10px}img{width:100%;height:auto;display:block}figcaption{font-size:.92rem;color:var(--mut);margin-top:6px}</style>")


class InputProblem(Exception):
    pass


def load_shots(scene_id, quality, version):
    capture_directory = bv.BLEND_ROOT / f"{scene_id}_{quality}_{version}" / "capture"
    status = json.loads(Path(rep.find_schedule_file(capture_directory)).read_text(encoding="utf-8"))
    shots = {round(float(shot["time"]), 3): Path(shot["path"]) for shot in status["shots"] if shot.get("saved")}
    missing = [time_value for time_value in bv.shot_times(scene_id) if time_value not in shots or not shots[time_value].is_file()]
    if missing:
        raise InputProblem(f"{scene_id}_{quality}_{version}: 없는 장 {missing[:6]}")
    return shots


def reference_time(scene_id):
    scene = bv.SCENES[scene_id]
    return scene["swing"] if scene_id == "s2swing" else scene["stop"]


def still_reference_time(scene_id):
    return reference_time(scene_id) + (bv.SWING_LENGTH_SECONDS if scene_id == "s2swing" else 0.0)


def relative_text(scene_id, time_value, for_still=False):
    base_time = still_reference_time(scene_id) if for_still else reference_time(scene_id)
    word = ("휘두름 끝" if for_still else "휘두름 시작") if scene_id == "s2swing" else "멈춤"
    return f"{word} {round(time_value - base_time, 2):+.2f}초"


def label_strip(width, text, right_text, font):
    strip = Image.new("RGB", (width, LABEL_HEIGHT), (24, 30, 34))
    draw = ImageDraw.Draw(strip)
    draw.text((8, 3), text, fill=(235, 240, 242), font=font)
    if right_text:
        draw.text((width - 8 - draw.textlength(right_text, font=font), 3), right_text, fill=(170, 184, 192), font=font)
    return strip


def compose_row(tiles_by_version, tile_width, right_text, font):
    columns = []
    for version in VERSION_ORDER:
        tile = tiles_by_version[version]
        height = int(round(tile.height * tile_width / tile.width))
        column = Image.new("RGB", (tile_width, height + LABEL_HEIGHT))
        column.paste(label_strip(tile_width, VERSION_LABELS[version], right_text if version == VERSION_ORDER[-1] else "", font), (0, 0))
        column.paste(tile.resize((tile_width, height), Image.LANCZOS), (0, LABEL_HEIGHT))
        columns.append(column)
    row = Image.new("RGB", (tile_width * len(columns) + 4 * (len(columns) - 1), columns[0].height), (255, 255, 255))
    for index, column in enumerate(columns):
        row.paste(column, (index * (tile_width + 4), 0))
    return row


def build_media(output_media, webp_quality, tile_width):
    output_media.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(KOREAN_FONT_PATH, 15)
    records = []
    for scene_id in SCENE_ORDER:
        for quality in bv.QUALITIES:
            shots = {version: load_shots(scene_id, quality, version) for version in VERSION_ORDER}
            frames = []
            for time_value in bv.frame_times(scene_id):
                tiles = {version: Image.open(shots[version][time_value]).convert("RGB") for version in VERSION_ORDER}
                frames.append(compose_row(tiles, tile_width, relative_text(scene_id, time_value), font))
            movie_path = output_media / f"{scene_id}_{quality}.webp"
            frames[0].save(movie_path, save_all=True, append_images=frames[1:], duration=FRAME_DURATION_MILLISECONDS, loop=0, format="WEBP", quality=webp_quality, method=4)
            records.append({"kind": "movie", "scene": scene_id, "quality": quality, "file": movie_path.name, "frames": len(frames)})
            for time_value in bv.still_times(scene_id):
                tiles = {version: Image.open(shots[version][time_value]).convert("RGB") for version in VERSION_ORDER}
                offset = round(time_value - still_reference_time(scene_id), 2)
                still_path = output_media / f"still_{scene_id}_{quality}_{int(round(offset * 10)):02d}.jpg"
                compose_row(tiles, STILL_TILE_WIDTH, relative_text(scene_id, time_value, True), font).save(still_path, format="JPEG", quality=STILL_JPEG_QUALITY, optimize=True)
                records.append({"kind": "still", "scene": scene_id, "quality": quality, "file": still_path.name, "time": time_value, "offset": offset})
    return records


def media_total_bytes(output_media):
    return sum(path.stat().st_size for path in output_media.iterdir() if path.is_file())


def figure_html(file_name, alt, caption):
    return f'<figure><img src="media/{html.escape(file_name)}" alt="{html.escape(alt)}" loading="lazy"><figcaption>{html.escape(caption)}</figcaption></figure>'


def build_index(records):
    parts = [f'<!doctype html><html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(TITLE)}</title>{PAGE_STYLE}</head><body><div class="p">',
             f"<h1>{html.escape(TITLE)}</h1>"]
    parts += [f"<p>{html.escape(item)}</p>" for item in PAGE_ITEMS[:4]]
    for scene_id in SCENE_ORDER:
        scene_title = bv.SCENES[scene_id]["title"]
        parts.append(f'<section><h2>{html.escape(scene_title)}</h2><div class="g">')
        for quality in bv.QUALITIES:
            movie = next(record for record in records if record["kind"] == "movie" and record["scene"] == scene_id and record["quality"] == quality)
            parts.append(figure_html(movie["file"], f"{scene_title} {QUALITY_TITLES[quality]}", f"{scene_title} · {QUALITY_TITLES[quality]} · {MOVIE_RANGE_WORDS[scene_id]} (0.1초 간격)"))
        for quality in bv.QUALITIES:
            for still in [record for record in records if record["kind"] == "still" and record["scene"] == scene_id and record["quality"] == quality]:
                parts.append(figure_html(still["file"], f"{scene_title} {QUALITY_TITLES[quality]} {STILL_REFERENCE_WORDS[scene_id]} {still['offset']}초",
                                         f"{scene_title} · {QUALITY_TITLES[quality]} · {STILL_REFERENCE_WORDS[scene_id]} {still['offset']:g}초"))
        parts.append("</div></section>")
    parts += [f"<p>{html.escape(item)}</p>" for item in PAGE_ITEMS[4:]]
    parts.append("</div></body></html>")
    return "".join(parts)


def inline_page(index_text, media_directory):
    mime = {".jpg": "image/jpeg", ".webp": "image/webp"}

    def replace(match):
        path = media_directory / match.group(1)
        return f'src="data:{mime[path.suffix.lower()]};base64,{base64.b64encode(path.read_bytes()).decode("ascii")}"'

    return re.sub(r'src="media/([^"]+)"', replace, index_text)


def build_all(webp_quality, tile_width, budget_megabytes, single_path):
    media_directory = OUTPUT_DIRECTORY / "media"
    for old in list(media_directory.glob("*")) if media_directory.is_dir() else []:
        old.unlink()
    quality = webp_quality
    while True:
        records = build_media(media_directory, quality, tile_width)
        total = media_total_bytes(media_directory)
        print(f"webp 품질 {quality} 타일 {tile_width}px 합계 {total / 1e6:.2f}MB", flush=True)
        if total <= budget_megabytes * 1e6 or quality <= 30:
            break
        quality -= 10
    index_text = build_index(records)
    (OUTPUT_DIRECTORY / "index.html").write_text(index_text, encoding="utf-8", newline="\n")
    single_text = inline_page(index_text, media_directory)
    Path(single_path).parent.mkdir(parents=True, exist_ok=True)
    Path(single_path).write_text(single_text, encoding="utf-8", newline="\n")
    size = Path(single_path).stat().st_size
    print(f"그림 {len(records)}개, 한 파일 {size} bytes ({size / 1e6:.2f}MB), webp 품질 {quality}")
    return size, records, quality


def run_self_test():
    failures = []

    def check(condition, label):
        if not condition:
            failures.append(label)
        print(("통과 " if condition else "실패 ") + label)

    index_text = build_index([{"kind": "movie", "scene": scene, "quality": quality, "file": f"{scene}_{quality}.webp", "frames": 1} for scene in SCENE_ORDER for quality in bv.QUALITIES]
                             + [{"kind": "still", "scene": scene, "quality": quality, "file": f"still_{scene}_{quality}_{index:02d}.jpg", "time": 1.0, "offset": index / 10} for scene in SCENE_ORDER for quality in bv.QUALITIES for index in (5, 15)])
    plain = html.unescape(index_text)
    check(all(item in plain for item in PAGE_ITEMS), "페이지 글 여섯 항목이 글자 그대로 있음")
    check("승인하신 그림에는 서 있는 동안의 끝없는 휘저음이 들어 있었습니다." in plain, "핵심 문장")
    check(TITLE in plain and plain.index(PAGE_ITEMS[3]) < plain.index(PAGE_ITEMS[4]), "제목과 항목 순서")
    check(len(re.findall(r"<img ", index_text)) == 6 + 12, "그림 수 18")
    check(not re.search(r"통과|실패|합격|불합격|낫다|좋다", re.sub(re.escape(PAGE_ITEMS[4]), "", re.sub(re.escape(PAGE_ITEMS[2]), "", index_text))), "판정 낱말 없음")
    print("자체 점검", "통과" if not failures else "실패 " + str(len(failures)) + "건")
    return 0 if not failures else 1


def main(argument_list=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description="3D 안개 섞기 비교 그림 묶음")
    parser.add_argument("--webp-quality", type=int, default=70)
    parser.add_argument("--tile-width", type=int, default=480)
    parser.add_argument("--budget-mb", type=float, default=9.5)
    parser.add_argument("--single", default=str(DEFAULT_SINGLE_PATH))
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args(argument_list)
    if arguments.self_test:
        return run_self_test()
    try:
        build_all(arguments.webp_quality, arguments.tile_width, arguments.budget_mb, arguments.single)
    except (InputProblem, OSError, KeyError, ValueError) as error:
        print("입력 문제:", error)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

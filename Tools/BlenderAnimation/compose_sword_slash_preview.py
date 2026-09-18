"""시스템 Python(PIL)으로 실행: render_sword_slash_preview.py의 프레임 PNG를 시점별 실시간·1/3속 GIF와 주요 포즈 대조표 PNG로 합친다.
실행: python Tools/BlenderAnimation/compose_sword_slash_preview.py [--out Docs/Validation/BlenderAnimation]
출력: <out>/sword-slash-<view>.gif, sword-slash-slow.gif, sword-slash-poses.png, sword-slash-tip-path.png
상태: 현행 (2026-09-18)
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Saved/BlenderAnimation/SwordSlash'


def frames(view):
    return [Image.open(path).convert('RGB') for path in sorted((WORK / view).glob('frame_*.png'))]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', default=str(ROOT / 'Docs/Validation/BlenderAnimation'))
    parser.add_argument('--views', nargs='*', default=['front', 'side', 'game'])
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = json.loads((WORK / 'author-result.json').read_text(encoding='utf-8'))
    fps = report['summary']['fps']
    per_view = {view: frames(view) for view in args.views}
    count = min(len(images) for images in per_view.values())
    width, height = per_view[args.views[0]][0].size
    written = []
    for view, images in per_view.items():
        images[0].save(out / f'sword-slash-{view}.gif', save_all=True, append_images=images[1:], duration=int(1000 / fps), loop=0, optimize=False)
        written.append(f'sword-slash-{view}.gif')
    combined = []
    for index in range(count):
        sheet = Image.new('RGB', (width * len(args.views), height))
        for column, view in enumerate(args.views):
            sheet.paste(per_view[view][index], (column * width, 0))
        ImageDraw.Draw(sheet).text((8, 8), f'f{index:02d}  {index / fps:.2f}s', fill=(255, 255, 255))
        combined.append(sheet.resize((width * len(args.views) // 2, height // 2), Image.LANCZOS))
    combined[0].save(out / 'sword-slash-three-views.gif', save_all=True, append_images=combined[1:], duration=int(1000 / fps), loop=0)
    combined[0].save(out / 'sword-slash-slow.gif', save_all=True, append_images=combined[1:], duration=int(3000 / fps), loop=0)
    written += ['sword-slash-three-views.gif', 'sword-slash-slow.gif']
    key_frames = [0, 5, 8, 11, 13, 15, 17, 19, 21, 24, 28, 34, 39]
    key_frames = [frame for frame in key_frames if frame < count]
    columns = 5
    rows = (len(key_frames) + columns - 1) // columns
    poses = Image.new('RGB', (width * columns, height * len(args.views) * rows))
    for slot, frame in enumerate(key_frames):
        column, row = slot % columns, slot // columns
        for line, view in enumerate(args.views):
            poses.paste(per_view[view][frame], (column * width, (row * len(args.views) + line) * height))
        ImageDraw.Draw(poses).text((column * width + 8, row * len(args.views) * height + 8), f'f{frame:02d} {frame / fps:.2f}s', fill=(255, 255, 0))
    poses.thumbnail((width * columns, height * len(args.views) * rows))
    poses.save(out / 'sword-slash-poses.png')
    written.append('sword-slash-poses.png')
    plot = Image.new('RGB', (900, 900), (24, 26, 30))
    draw = ImageDraw.Draw(plot)
    scale = 3.0
    origin = (450, 620)
    for grid in range(-140, 200, 20):
        color = (60, 62, 70) if grid else (110, 112, 120)
        draw.line([(origin[0] - 140 * scale, origin[1] - grid * scale), (origin[0] + 140 * scale, origin[1] - grid * scale)], fill=color)
        draw.line([(origin[0] + grid * scale, origin[1] - 200 * scale), (origin[0] + grid * scale, origin[1] + 140 * scale)], fill=color)
    tips = [(origin[0] + row['sword_tip'][1] * scale, origin[1] - row['sword_tip'][0] * scale) for row in report['records']]
    hands = [(origin[0] + row['hand_r'][1] * scale, origin[1] - row['hand_r'][0] * scale) for row in report['records']]
    draw.line(tips, fill=(240, 90, 60), width=3)
    draw.line(hands, fill=(90, 180, 240), width=2)
    for index, point in enumerate(tips):
        if index % 2 == 0:
            draw.text((point[0] + 3, point[1] - 3), str(index), fill=(255, 220, 160))
    for row in report['records'][::6]:
        for side, color in (('l', (120, 240, 120)), ('r', (240, 200, 120))):
            ball = row['feet'][side]['ball']
            draw.ellipse([origin[0] + ball[1] * scale - 4, origin[1] - ball[0] * scale - 4, origin[0] + ball[1] * scale + 4, origin[1] - ball[0] * scale + 4], outline=color)
    draw.text((10, 10), 'top view, cm: sword tip (red), hand_r (blue), ball contacts every 6 frames (green=L, tan=R); up=forward, right=right', fill=(220, 220, 220))
    plot.save(out / 'sword-slash-tip-path.png')
    written.append('sword-slash-tip-path.png')
    print(json.dumps({'written': written, 'frames': count}))


if __name__ == '__main__':
    main()

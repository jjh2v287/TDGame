"""sword_attack01_render.py 결과 PNG로 시점별 포즈표(프레임 번호 표기)와 실시간·느린 GIF를 만든다.
실행: python Tools/BlenderAnimation/sword_attack01_compose.py <렌더 폴더 이름> [--frames 1,10,20 ...] [--columns 8] [--gif] [--triptych front,right,game] [--tile 240] [--every 2]
출력: Saved/BlenderAnimation/SwordAttack01/<폴더>/sheet_<view>.png, (--gif) anim_<view>.gif(실시간)·slow_<view>.gif(1/3속), sheet_all.png(시점을 행으로 묶은 표), (--triptych) triptych.gif·triptych_slow.gif(시점을 옆으로 붙인 실시간·1/3속)
상태: 현행 (2026-09-25)
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'Saved/BlenderAnimation/SwordAttack01'


def frame_files(view_dir):
    return sorted(view_dir.glob('f_*.png'), key=lambda path: int(path.stem[2:]))


def labeled(path, scale):
    image = Image.open(path).convert('RGB')
    if scale != 1.0:
        image = image.resize((int(image.width * scale), int(image.height * scale)))
    ImageDraw.Draw(image).text((6, 4), path.stem[2:], fill=(255, 235, 120))
    return image


def sheet(images, columns):
    rows = (len(images) + columns - 1) // columns
    width, height = images[0].size
    canvas = Image.new('RGB', (columns * width, rows * height), (10, 10, 12))
    for index, image in enumerate(images):
        canvas.paste(image, ((index % columns) * width, (index // columns) * height))
    return canvas


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('folder')
    parser.add_argument('--frames', default='')
    parser.add_argument('--columns', type=int, default=8)
    parser.add_argument('--scale', type=float, default=0.5)
    parser.add_argument('--gif', action='store_true')
    parser.add_argument('--triptych', default='')
    parser.add_argument('--tile', type=int, default=320)
    parser.add_argument('--every', type=int, default=1)
    arguments = parser.parse_args()
    folder = BASE / arguments.folder
    report = json.loads((folder / 'render-result.json').read_text(encoding='utf-8'))
    fps, step = report['fps'], report['step']
    wanted = {int(value) for value in arguments.frames.split(',') if value}
    rows = []
    for view in report['views']:
        files = frame_files(folder / view)
        chosen = [path for path in files if not wanted or int(path.stem[2:]) in wanted]
        images = [labeled(path, arguments.scale) for path in chosen]
        view_sheet = sheet(images, arguments.columns)
        view_sheet.save(folder / f'sheet_{view}.png')
        rows.append(sheet(images, len(images)) if wanted else view_sheet)
        if arguments.gif:
            frames = [Image.open(path).convert('P', palette=Image.ADAPTIVE) for path in files]
            duration = int(round(1000.0 * step / fps))
            frames[0].save(folder / f'anim_{view}.gif', save_all=True, append_images=frames[1:], duration=max(duration, 20), loop=0)
            frames[0].save(folder / f'slow_{view}.gif', save_all=True, append_images=frames[1:], duration=duration * 3, loop=0)
    if wanted:
        width = max(row.width for row in rows)
        canvas = Image.new('RGB', (width, sum(row.height for row in rows)), (10, 10, 12))
        top = 0
        for row in rows:
            canvas.paste(row, (0, top))
            top += row.height
        canvas.save(folder / 'sheet_all.png')
    if arguments.triptych:
        views = arguments.triptych.split(',')
        columns = [frame_files(folder / view)[::arguments.every] for view in views]
        size = arguments.tile
        tiles = []
        for paths in zip(*columns):
            images = [Image.open(path).convert('RGB').resize((size, size)) for path in paths]
            canvas = Image.new('RGB', (size * len(images), size))
            for index, image in enumerate(images):
                canvas.paste(image, (index * size, 0))
            ImageDraw.Draw(canvas).text((6, size - 16), paths[0].stem[2:], fill=(255, 235, 120))
            tiles.append(canvas.convert('P', palette=Image.ADAPTIVE))
        duration = int(round(1000.0 * step * arguments.every / fps))
        tiles[0].save(folder / 'triptych.gif', save_all=True, append_images=tiles[1:], duration=max(duration, 20), loop=0)
        tiles[0].save(folder / 'triptych_slow.gif', save_all=True, append_images=tiles[1:], duration=duration * 3, loop=0)
    print(json.dumps({'folder': str(folder), 'views': report['views'], 'frames': sorted(wanted) or 'all'}))


main()

# File: Tools/BlenderAnimation/compose_sword_slash_ltor_preview.py
"""시스템 Python(PIL)으로 실행: render_sword_slash_ltor_preview.py의 프레임 PNG를 시점별 실시간·1/3속 GIF와 주요 포즈 대조표 PNG로 합친다.
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'Saved/BlenderAnimation/SwordSlashLToR'


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
        images[0].save(out / f'sword-slash-ltor-{view}.gif', save_all=True, append_images=images[1:], duration=int(1000 / fps), loop=0, optimize=False)
        written.append(f'sword-slash-ltor-{view}.gif')
        
    combined = []
    for index in range(count):
        sheet = Image.new('RGB', (width * len(args.views), height))
        for column, view in enumerate(args.views):
            sheet.paste(per_view[view][index], (column * width, 0))
        ImageDraw.Draw(sheet).text((8, 8), f'f{index:02d}  {index / fps:.2f}s (Attack02 LToR)', fill=(255, 255, 255))
        combined.append(sheet.resize((width * len(args.views) // 2, height // 2), Image.LANCZOS))
    combined[0].save(out / 'sword-slash-ltor-three-views.gif', save_all=True, append_images=combined[1:], duration=int(1000 / fps), loop=0)
    combined[0].save(out / 'sword-slash-ltor-slow.gif', save_all=True, append_images=combined[1:], duration=int(3000 / fps), loop=0)
    written += ['sword-slash-ltor-three-views.gif', 'sword-slash-ltor-slow.gif']
    
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
    poses.thumbnail((width * columns // 2, height * len(args.views) * rows // 2))
    poses.save(out / 'sword-slash-ltor-poses.png')
    written.append('sword-slash-ltor-poses.png')
    
    print("COMPOSE_SUCCESS:", json.dumps({'output_dir': str(out), 'files': written}))


if __name__ == '__main__':
    main()

# File: Tools/BlenderAnimation/compose_attack02_preview.py
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path('C:/Project/TDGame')
WORK = ROOT / 'Saved/BlenderAnimation/Attack02_Full'
OUT_DOCS = ROOT / 'Docs/Validation/BlenderAnimation'
OUT_DOCS.mkdir(parents=True, exist_ok=True)

VIEWS = ['front', 'side', 'game']
FRAMES = 32

for view in VIEWS:
    frames = [Image.open(WORK / view / f'frame_{f:03d}.png') for f in range(1, FRAMES + 1)]
    gif_path = OUT_DOCS / f'attack02-mocap-{view}.gif'
    frames[0].save(gif_path, save_all=True, append_images=frames[1:], duration=1000/30, loop=0)
    print(f'Wrote {gif_path}')

three_view_frames = []
for f in range(1, FRAMES + 1):
    imgs = [Image.open(WORK / v / f'frame_{f:03d}.png') for v in VIEWS]
    w, h = imgs[0].size
    combined = Image.new('RGB', (w * 3, h))
    for i, img in enumerate(imgs):
        combined.paste(img, (i * w, 0))
    three_view_frames.append(combined)

three_views_gif = OUT_DOCS / 'attack02-mocap-three-views.gif'
three_view_frames[0].save(three_views_gif, save_all=True, append_images=three_view_frames[1:], duration=1000/30, loop=0)
print(f'Wrote {three_views_gif}')

key_frames = [1, 5, 9, 13, 17, 21, 25, 29]
cols = len(key_frames)
rows = len(VIEWS)
w, h = Image.open(WORK / 'front' / 'frame_001.png').size
thumb_w, thumb_h = w // 2, h // 2
sheet = Image.new('RGB', (thumb_w * cols, thumb_h * rows))

for r, view in enumerate(VIEWS):
    for c, f in enumerate(key_frames):
        img = Image.open(WORK / view / f'frame_{f:03d}.png')
        img_thumb = img.resize((thumb_w, thumb_h), Image.Resampling.LANCZOS)
        sheet.paste(img_thumb, (c * thumb_w, r * thumb_h))

poses_png = OUT_DOCS / 'attack02-mocap-poses.png'
sheet.save(poses_png)
print(f'Wrote {poses_png}')

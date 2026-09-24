from pathlib import Path
from PIL import Image, ImageDraw

ROOT = Path("C:/Project/TDGame")
WORK = ROOT / "Saved/BlenderAnimation/MocapSlash"
OUT_DIR = ROOT / "Docs/Validation/BlenderAnimation"

OUT_DIR.mkdir(parents=True, exist_ok=True)
views = ["front", "side", "game"]
per_view = {}
for view in views:
    per_view[view] = [Image.open(p).convert("RGB") for p in sorted((WORK / view).glob("frame_*.png"))]

num_frames = len(per_view["front"])
width, height = per_view["front"][0].size
fps = 30

for view, imgs in per_view.items():
    imgs[0].save(OUT_DIR / f"mocap-slash-{view}.gif", save_all=True, append_images=imgs[1:], duration=int(1000 / fps), loop=0)

combined = []
for idx in range(num_frames):
    sheet = Image.new("RGB", (width * 3, height))
    for col, v in enumerate(views):
        sheet.paste(per_view[v][idx], (col * width, 0))
    ImageDraw.Draw(sheet).text((12, 12), f"f{idx:02d} {idx/fps:.2f}s [Mocap Attack 03]", fill=(255, 255, 255))
    combined.append(sheet.resize((width * 3 // 2, height // 2), Image.LANCZOS))

combined[0].save(OUT_DIR / "mocap-slash-three-views.gif", save_all=True, append_images=combined[1:], duration=int(1000 / fps), loop=0)

key_frames = [0, 5, 10, 15, 20, 25, 30, 35, 40]
key_frames = [f for f in key_frames if f < num_frames]
columns = 5
rows = (len(key_frames) + columns - 1) // columns
poses = Image.new("RGB", (width * columns, height * 3 * rows))

for slot, frame in enumerate(key_frames):
    col, row = slot % columns, slot // columns
    for line, v in enumerate(views):
        poses.paste(per_view[v][frame], (col * width, (row * 3 + line) * height))
    ImageDraw.Draw(poses).text((col * width + 12, row * 3 * height + 12), f"f{frame:02d} {frame/fps:.2f}s", fill=(255, 255, 0))

poses.thumbnail((width * columns // 2, height * 3 * rows // 2))
poses.save(OUT_DIR / "mocap-slash-poses.png")
print("MOCAP PREVIEW COMPOSE COMPLETE: mocap-slash-poses.png & mocap-slash-three-views.gif")

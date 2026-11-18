import sys, numpy
from pathlib import Path
from PIL import Image, ImageDraw
sys.path.insert(0, "C:/Project/TDGame/Saved/FluidFX/fog3d_diag")
import calc
import fog3d_visible_check as v
D = Path("C:/Project/TDGame/Saved/FluidFX/fog3d_diag")
out = Path("C:/Project/TDGame/Docs/VolumeFog3D/measurements/stage1b/diag")
rows = []
for s in ["S0", "S1", "S2", "S3", "S4"]:
    lumas, gaps, valid, cap = calc.load_run(D / f"run_{s}")
    h, w = lumas["mode2_a"].shape
    r = v.pawn_region(h, w)
    a = v.crop(lumas["mode2_a"], r); b = v.crop(lumas["mode0_a"], r)
    d = numpy.clip(numpy.abs(a - b) * 8, 0, 1)
    tiles = [(x * 255).astype(numpy.uint8) for x in (a, b, d)]
    row = numpy.concatenate(tiles, axis=1)
    img = Image.fromarray(row).convert("RGB")
    ImageDraw.Draw(img).text((6, 6), f"{s}: mode2_a | mode0_a | |diff|x8", fill=(255, 255, 0))
    rows.append(numpy.asarray(img))
fig = Image.fromarray(numpy.concatenate(rows, axis=0))
scale = 1280 / fig.width
fig = fig.resize((1280, int(fig.height * scale)))
fig.save(out / "reaction_compare.png")
print(fig.size)

"""에디터 밖에서 실행: MCP CaptureViewport로 주요 지점을 탑다운 각도로 캡처해 Saved/WorldGen/Captures/<이름>.png 로 저장한다.

사용: python Tools/WorldGen/capture_views_mcp.py [이름=x,y,pitch,yaw,dist ...]
이름을 주지 않으면 기본 8개 시점을 찍는다. 높이는 Saved/WorldGen/AshenVale/height.r16 에서 읽는다.
"""
import base64
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, r"C:\Users\jjh\.claude\projects\C--Project-TDGame\tools")
import uemcp  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "Saved", "WorldGen", "Captures")
HEIGHT = os.path.join(ROOT, "Saved", "WorldGen", "AshenVale", "height.r16")
SIZE = 1009
HALF = 504

DEFAULT = {
    "village": (-11000, 2000, -55, 35, 3200),
    "village_plaza": (-11000, 2000, -70, 120, 1800),
    "graveyard": (1000, -33000, -55, 120, 2800),
    "bridge": (6200, 2600, -50, -20, 2400),
    "marsh_bonepit": (21000, 33000, -55, 60, 2600),
    "mushroom_grove": (-39000, 30000, -55, 200, 2600),
    "main_crypt": (35200, -31800, -40, 45, 2600),
    "shrine": (-25000, -15000, -55, 60, 2400),
    "forest_road": (-9000, -16000, -55, 100, 2400),
    "overview": (0, 0, -89, 0, 70000),
}


def ground(xcm, ycm):
    if abs(xcm) > HALF * 100 or abs(ycm) > HALF * 100 or not os.path.exists(HEIGHT):
        return 0.0
    h = np.fromfile(HEIGHT, dtype="<u2").reshape(SIZE, SIZE)
    ix = int(np.clip(round(xcm / 100 + HALF), 0, SIZE - 1))
    iy = int(np.clip(round(ycm / 100 + HALF), 0, SIZE - 1))
    return (float(h[iy, ix]) - 32768.0) / 128.0 * 100.0


def capture(sid, name, x, y, pitch, yaw, dist):
    gz = ground(x, y)
    cam_z = gz + dist * math.sin(math.radians(-pitch))
    back = dist * math.cos(math.radians(-pitch))
    cx = x - back * math.cos(math.radians(yaw))
    cy = y - back * math.sin(math.radians(yaw))
    args = {
        "captureTransform": {"location": {"x": cx, "y": cy, "z": cam_z}, "rotation": {"pitch": pitch, "yaw": yaw, "roll": 0.0}, "scale": {"x": 1, "y": 1, "z": 1}},
        "annotations": {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0, "maxLabelDistance": 0, "classFilter": {"refPath": "/Script/Engine.Actor"}, "maxLabels": 0},
        "bShowUI": False,
    }
    res = uemcp.call_tool(sid, "call_tool", {"toolset_name": "EditorToolset.EditorAppToolset", "tool_name": "CaptureViewport", "arguments": args})
    content = res.get("result", {}).get("content", [])
    img = None
    for c in content:
        if c.get("type") == "image":
            img = c
        elif c.get("type") == "text" and "data" in c.get("text", ""):
            try:
                d = json.loads(c["text"])
                rv = d.get("returnValue", d)
                img = rv.get("image", rv)
            except Exception:
                pass
    if img is None:
        print("no image for", name, str(content)[:300])
        return
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".png")
    open(path, "wb").write(base64.b64decode(img["data"]))
    print("saved", path, "ground", round(gz))


def main():
    sid = uemcp.connect()
    views = DEFAULT
    if len(sys.argv) > 1:
        views = {}
        for a in sys.argv[1:]:
            n, v = a.split("=")
            views[n] = tuple(float(t) for t in v.split(","))
    for name, (x, y, pitch, yaw, dist) in views.items():
        capture(sid, name, x, y, pitch, yaw, dist)


if __name__ == "__main__":
    main()

"""에디터 밖에서 실행: MCP CaptureViewport로 주요 지점을 탑다운 각도로 캡처해 Saved/WorldGen/Captures/<이름>.png 로 저장한다(지점 지면 높이는 높이맵에서 읽는다).
실행: python Tools/WorldGen/capture_views_mcp.py [이름=x,y,pitch,yaw,dist ...] (PowerShell 또는 Git Bash, 에디터 실행 중·MCP 포트 8000 필요, 이름을 주지 않으면 기본 시점 전부)
출력: Saved/WorldGen/Captures/<이름>.png, 표준 출력에 저장 경로와 지면 높이
상태: 현행 (2026-10-15, P3-19 연속 세계: 이계 상자 안 지점은 Saved/WorldGen/Realms/<RealmId>/height.r16 로컬 높이에 layout.json realm.center_cm의 z(Zoff)를 더한다. 필드 상자 ±50400cm 밖이면서 이계 상자 밖이면 0)
"""
import base64
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # Tools/ (uemcp.py)
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


REALMS_DIR = os.path.join(ROOT, "Saved", "WorldGen", "Realms")


def find_realm_height_grid(xcm, ycm):
    if not os.path.isdir(REALMS_DIR):
        return None
    for realm_id in sorted(os.listdir(REALMS_DIR)):
        layout_path = os.path.join(REALMS_DIR, realm_id, "layout.json")
        height_path = os.path.join(REALMS_DIR, realm_id, "height.r16")
        if not (os.path.exists(layout_path) and os.path.exists(height_path)):
            continue
        realm = json.load(open(layout_path, encoding="utf-8"))["realm"]
        center = realm["center_cm"]
        half = realm["half_extent_cm"]
        half_x, half_y = (half[0], half[1]) if isinstance(half, list) else (half, half)
        if abs(xcm - center[0]) > half_x or abs(ycm - center[1]) > half_y:
            continue
        heights = np.fromfile(height_path, dtype="<u2")
        size = int(round(math.sqrt(heights.size)))
        z_offset_cm = float(center[2]) if len(center) > 2 else 0.0
        return heights.reshape(size, size), xcm - center[0], ycm - center[1], z_offset_cm
    return None


def sample_grid_height_cm(heights, local_xcm, local_ycm, z_offset_cm):
    size = heights.shape[0]
    half = (size - 1) // 2
    ix = int(np.clip(round(local_xcm / 100 + half), 0, size - 1))
    iy = int(np.clip(round(local_ycm / 100 + half), 0, size - 1))
    return (float(heights[iy, ix]) - 32768.0) / 128.0 * 100.0 + z_offset_cm


def ground(xcm, ycm):
    realm_grid = find_realm_height_grid(xcm, ycm)
    if realm_grid is not None:
        return sample_grid_height_cm(*realm_grid)
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

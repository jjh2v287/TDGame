"""에디터 안에서 실행: 주요 지점에 뷰포트 카메라를 놓고 고해상도 스크린샷을 찍는다.

Saved/WorldGen/capture_views.json ([[이름, x, y, z, pitch, yaw], ...]) 이 있으면 그 시점 하나(또는 여러 개 중 마지막)로 카메라를 놓는다. 실제 캡처는 밖에서 MCP CaptureEditorImage로 한다(tools/save_editor_capture.py).
기본은 마을·묘지·다리·습지·버섯 숲·메인 던전 입구를 탑다운 게임 카메라 각도(약 -55°)로 찍는다.
"""
import json
import os
import time

import unreal

OUT = os.environ.get("TD_CAPTURE_OUT", r"C:\Project\TDGame\Saved\WorldGen\Captures")
os.makedirs(OUT, exist_ok=True)
default_views = [
    ["village", -11000, 2000, 0, -55, 35],
    ["graveyard", 1000, -33000, 0, -55, 120],
    ["bridge", 6200, 2600, 0, -50, -20],
    ["marsh_bonepit", 21000, 33000, 0, -55, 60],
    ["mushroom_grove", -39000, 30000, 0, -55, 200],
    ["main_crypt", 35200, -31800, 0, -45, 225 + 180],
    ["shrine", -25000, -15000, 0, -55, 60],
    ["overview", 0, 0, 0, -89, 0],
]
PARAM = r"C:\Project\TDGame\Saved\WorldGen\capture_views.json"
views = json.load(open(PARAM, encoding="utf-8")) if os.path.exists(PARAM) else default_views

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = ues.get_editor_world()
landscape = None
for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy):
    if a.get_class().get_name() == "Landscape":
        landscape = a
        break


def ground_z(x, y):
    if landscape is None:
        return 0.0
    res = unreal.TDLandscapeEditorLibrary.try_get_landscape_height_at_location(landscape, unreal.Vector(x, y, 0))
    if isinstance(res, (tuple, list)):
        return float(res[1]) if res[0] else 0.0
    return float(res)


for name, x, y, z, pitch, yaw in views:
    gz = ground_z(x, y)
    if name == "overview":
        dist = 62000.0
    else:
        dist = 2600.0
    import math
    cam_z = gz + dist * math.sin(math.radians(-pitch))
    back = dist * math.cos(math.radians(-pitch))
    cx = x - back * math.cos(math.radians(yaw))
    cy = y - back * math.sin(math.radians(yaw))
    ues.set_level_viewport_camera_info(unreal.Vector(cx, cy, cam_z), unreal.Rotator(roll=0, pitch=pitch, yaw=yaw))
    les.editor_invalidate_viewports()
    print("capture requested:", name, cx, cy, cam_z, "ground", gz)

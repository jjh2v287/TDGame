"""에디터 안에서 실행(PIE 종료 상태): 오픈월드 필드 태양(TDGen_Sun) 회전과 이계 정의(DA_TDRealm_*) 태양 회전만 바꾼다. save=false면 메모리에만 바꿔 PIE 미리보기에 쓰고, save=true면 태양 외부 액터 패키지 하나와 바뀐 이계 정의 에셋만 저장한다(.umap·다른 액터 저장 없음).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_set_sun.py (PowerShell). 요청 Saved/WorldGen/sun_request.json {"field": [pitch, yaw], "realms_from_layout": true, "save": false}; 이계 값은 Saved/WorldGen/Realms/<RealmId>/layout.json realm.atmosphere.sun_rotation(generate_realm.py 출력)에서 읽는다
출력: 콘솔 SUN_RESULT 줄과 Saved/WorldGen/sun_result.json(status preview·saved·save_failed, 이전·이후 회전, 카메라 반대 yaw 225와의 수평 차이(30° 초과면 변경 전에 거부), 저장 여부, 콘텐츠·맵 미저장 패키지). 저장 실패면 오류로 끝난다. 필드 저장이 성공하면 Saved/WorldGen/look.json의 sun_pitch·sun_yaw도 맞춘다. 미리보기를 되돌리려면 sun_result.json의 before 값으로 다시 미리보기를 건다
상태: 현행 (2026-10-16, P3-20)
"""
import json
import math
import os

import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
WORLDGEN_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen")
REQUEST_PATH = os.path.join(WORLDGEN_DIR, "sun_request.json")
RESULT_PATH = os.path.join(WORLDGEN_DIR, "sun_result.json")
LOOK_PATH = os.path.join(WORLDGEN_DIR, "look.json")
RELIGHT_SNAPSHOT_PATH = os.path.join(WORLDGEN_DIR, "relight_field_snapshot.json")
LEVEL_PATH = "/Game/Level/LV_DarkFantasy_OpenWorld"
LEVEL_NAME = "LV_DarkFantasy_OpenWorld"
SUN_LABEL = "TDGen_Sun"
DEFINITION_FOLDER = "/Game/World/Definitions"
REALM_IDS = ("SunkenStarfield", "PaleHeights")
CAMERA_VIEW_DIRECTION = (500.0, 500.0, -843.0)
ROTATION_TOLERANCE_DEGREES = 0.01
CAMERA_OPPOSITE_YAW = 225.0
BACKLIGHT_MAX_OFFSET_DEGREES = 30.0

editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def load_json(path):
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8-sig") as handle:
        return json.load(handle)


def write_json(path, payload):
    temporary_path = path + ".tmp"
    with open(temporary_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=1)
    os.replace(temporary_path, path)


def angle_to_camera_view(pitch, yaw):
    pitch_radians, yaw_radians = math.radians(pitch), math.radians(yaw)
    light = (math.cos(pitch_radians) * math.cos(yaw_radians), math.cos(pitch_radians) * math.sin(yaw_radians), math.sin(pitch_radians))
    length = math.sqrt(sum(component * component for component in CAMERA_VIEW_DIRECTION))
    view = [component / length for component in CAMERA_VIEW_DIRECTION]
    dot = max(-1.0, min(1.0, sum(a * b for a, b in zip(light, view))))
    return round(math.degrees(math.acos(dot)), 1)


def describe_rotation(rotator):
    return {"pitch": round(rotator.pitch, 3), "yaw": round(rotator.yaw, 3), "roll": round(rotator.roll, 3), "angle_to_camera_view_deg": angle_to_camera_view(rotator.pitch, rotator.yaw)}


def is_same_rotation(rotator, pitch, yaw, roll):
    target = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    delta = unreal.MathLibrary.normalized_delta_rotator(rotator, target)
    return max(abs(delta.pitch), abs(delta.yaw), abs(delta.roll)) <= ROTATION_TOLERANCE_DEGREES


def open_field_level():
    if editor_subsystem.get_game_world() is not None:
        raise RuntimeError("PIE is running; stop PIE before changing the sun")
    world = editor_subsystem.get_editor_world()
    if world is not None and world.get_name() == LEVEL_NAME:
        return
    if not level_subsystem.load_level(LEVEL_PATH):
        raise RuntimeError("could not load " + LEVEL_PATH)


def find_field_sun():
    lights = [actor for actor in actor_subsystem.get_all_level_actors() if isinstance(actor, unreal.DirectionalLight)]
    suns = [actor for actor in lights if actor.get_actor_label() == SUN_LABEL]
    if len(suns) != 1:
        raise RuntimeError(f"expected exactly one {SUN_LABEL}, found {len(suns)} among {[actor.get_actor_label() for actor in lights]}")
    return suns[0]


def read_dirty_package_names():
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()) + list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    return sorted({package.get_path_name() for package in packages})


def horizontal_offset_from_camera_opposite(yaw):
    return round(abs((yaw - CAMERA_OPPOSITE_YAW + 180.0) % 360.0 - 180.0), 1)


def require_backlight_rule(name, yaw):
    offset = horizontal_offset_from_camera_opposite(yaw)
    if offset > BACKLIGHT_MAX_OFFSET_DEGREES:
        raise RuntimeError(f"{name} sun yaw {yaw} is {offset} deg away from camera-opposite yaw {CAMERA_OPPOSITE_YAW}; D-46 allows {BACKLIGHT_MAX_OFFSET_DEGREES}")
    return offset


def read_layout_sun_rotation(realm_id):
    layout = load_json(os.path.join(WORLDGEN_DIR, "Realms", realm_id, "layout.json"))
    if layout is None:
        raise RuntimeError(f"missing layout.json for {realm_id}; run Tools/WorldGen/generate_realm.py --realm all")
    return [float(value) for value in layout["realm"]["atmosphere"]["sun_rotation"]]


def load_realm_definition(realm_id):
    asset_path = f"{DEFINITION_FOLDER}/DA_TDRealm_{realm_id}"
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if asset is None:
        raise RuntimeError("missing " + asset_path)
    rotation = read_layout_sun_rotation(realm_id)
    return {"realm_id": realm_id, "asset_path": asset_path, "asset": asset, "rotation": rotation, "offset": require_backlight_rule(realm_id, rotation[1])}


def set_field_sun(sun, rotation):
    pitch, yaw = float(rotation[0]), float(rotation[1])
    report = {"label": SUN_LABEL, "package": sun.get_package().get_path_name(), "before": describe_rotation(sun.get_actor_rotation()), "offset_from_camera_opposite_deg": horizontal_offset_from_camera_opposite(yaw)}
    if not is_same_rotation(sun.get_actor_rotation(), pitch, yaw, 0.0):
        sun.modify()
        sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw), False)
    report["after"] = describe_rotation(sun.get_actor_rotation())
    return report


def set_realm_sun(definition):
    asset = definition["asset"]
    pitch, yaw, roll = definition["rotation"]
    atmosphere = asset.get_editor_property("atmosphere")
    current = atmosphere.get_editor_property("sun_rotation")
    report = {"asset": definition["asset_path"], "layout": definition["rotation"], "offset_from_camera_opposite_deg": definition["offset"], "before": describe_rotation(current)}
    report["changed"] = not is_same_rotation(current, pitch, yaw, roll)
    if report["changed"]:
        asset.modify()
        atmosphere.set_editor_property("sun_rotation", unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
        asset.set_editor_property("atmosphere", atmosphere)
    report["after"] = describe_rotation(asset.get_editor_property("atmosphere").get_editor_property("sun_rotation"))
    return report


def save_field_sun(sun, report, rotation):
    report["saved"] = bool(unreal.EditorLoadingAndSavingUtils.save_packages([sun.get_package()], False))
    if not report["saved"]:
        return [f"{SUN_LABEL} package was not saved"]
    report["look_json"] = sync_look_file(float(rotation[0]), float(rotation[1]))
    return []


def save_realm_definitions(definitions, reports, dirty_package_names):
    failures = []
    for definition, report in zip(definitions, reports):
        if not report["changed"] and definition["asset"].get_package().get_path_name() not in dirty_package_names:
            continue
        report["saved"] = bool(unreal.EditorAssetLibrary.save_loaded_asset(definition["asset"], False))
        if not report["saved"]:
            failures.append(definition["asset_path"] + " was not saved")
    return failures


def main():
    write_json(RESULT_PATH, {"status": "running"})
    request = load_json(REQUEST_PATH)
    if request is None:
        raise RuntimeError("missing " + REQUEST_PATH)
    if os.path.exists(RELIGHT_SNAPSHOT_PATH):
        raise RuntimeError("editor_relight.py realm preview is active (relight_field_snapshot.json exists); restore the field look first")
    should_save = bool(request.get("save"))
    field_rotation = request.get("field")
    if field_rotation:
        require_backlight_rule("field", float(field_rotation[1]))
    open_field_level()
    sun = find_field_sun() if field_rotation else None
    definitions = [load_realm_definition(realm_id) for realm_id in REALM_IDS] if request.get("realms_from_layout") else []
    dirty_before = read_dirty_package_names()
    result = {"status": "preview", "request": request, "dirty_before": dirty_before[:40]}
    if sun is not None:
        result["field"] = set_field_sun(sun, field_rotation)
    result["realms"] = [set_realm_sun(definition) for definition in definitions]
    failures = []
    if should_save:
        result["status"] = "saved"
        if sun is not None:
            failures += save_field_sun(sun, result["field"], field_rotation)
        failures += save_realm_definitions(definitions, result["realms"], dirty_before)
    result["dirty_after"] = read_dirty_package_names()[:40]
    if failures:
        result["status"] = "save_failed"
        result["failures"] = failures
    write_json(RESULT_PATH, result)
    print("SUN_RESULT", json.dumps(result, ensure_ascii=False))
    if failures:
        raise RuntimeError("; ".join(failures))


main()

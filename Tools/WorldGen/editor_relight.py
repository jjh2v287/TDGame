"""에디터 안에서 실행: 전체 재빌드 없이 조명·노출·안개만 다시 적용한다(빠른 반복용). 이계 미리보기 모드는 이계 대기 값을 전역 조명 액터에 저장 없이 얹는다.
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_relight.py (PowerShell)
출력: 에디터 월드 TDGen_Sun·TDGen_SkyLight·TDGen_SkyAtmosphere·TDGen_HeightFog·TDGen_PostProcess 값(이 스크립트는 저장하지 않는다), 이계 모드 필드 값 스냅숏 Saved/WorldGen/relight_field_snapshot.json
상태: 현행

대상 지정 파일 Saved/WorldGen/relight_target.json:
  없음 → Saved/WorldGen/look.json 적용. 스냅숏 파일이 있으면 먼저 필드 값을 복원하고 스냅숏을 지운다(이계 미리보기 복원).
  {"look": "<경로>"} → 그 look 파일 적용(상대 경로는 프로젝트 기준). 복원 동작은 위와 같다.
  {"realm": "<RealmId>"} → Saved/WorldGen/Realms/<RealmId>/layout.json 의 realm.atmosphere 적용. 스냅숏이 없을 때만 필드 값을 먼저 기록한다.
look.json 예:
{"sun_lux": 6.0, "sky_intensity": 2.2, "exposure_bias": 10.2, "fog_density": 0.022, "local_light_scale": 0.4,
 "sun_pitch": -52, "sun_yaw": 38, "sun_color": [0.8, 0.86, 1.0], "saturation": 0.8}
국소 조명 기본 세기는 layout.json 의 lights 항목(라벨로 대응)에서 다시 읽어 local_light_scale 을 곱한다.
"""
import json
import os

import unreal

WORLDGEN_DIR = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "WorldGen")
TARGET_PATH = os.path.join(WORLDGEN_DIR, "relight_target.json")
SNAPSHOT_PATH = os.path.join(WORLDGEN_DIR, "relight_field_snapshot.json")
LOOK_PATH = os.path.join(WORLDGEN_DIR, "look.json")
LAYOUT_PATH = os.path.join(WORLDGEN_DIR, "AshenVale", "layout.json")
FIELD_PROPERTIES = {
    "TDGen_Sun": ("intensity", "light_color"),
    "TDGen_SkyLight": ("intensity", "light_color"),
    "TDGen_SkyAtmosphere": ("rayleigh_scattering", "mie_scattering_scale", "sky_luminance_factor"),
    "TDGen_HeightFog": ("fog_density", "fog_height_falloff", "fog_inscattering_luminance", "fog_max_opacity", "start_distance",
                        "second_fog_data", "volumetric_fog_albedo", "volumetric_fog_emissive", "volumetric_fog_extinction_scale"),
}
REALM_ATMOSPHERE_KEYS = (
    "sun_rotation", "sun_intensity_lux", "sun_color", "sky_light_intensity", "sky_light_color", "sky_luminance_factor",
    "rayleigh_scattering", "mie_scattering_scale", "fog_density", "fog_height_falloff", "fog_height_cm", "fog_inscattering_color",
    "fog_max_opacity", "fog_start_distance_cm", "second_fog_density", "second_fog_height_falloff", "second_fog_height_offset_cm",
    "volumetric_fog_albedo", "volumetric_fog_emissive", "volumetric_fog_extinction_scale",
)

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
by_label = {a.get_actor_label(): a for a in eas.get_all_level_actors()}


def load_json(path):
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        print("skip", name, e)


def linear_color(rgb):
    return unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0)


def find_atmosphere_component(label, actor):
    if label == "TDGen_SkyAtmosphere":
        return actor.get_component_by_class(unreal.SkyAtmosphereComponent)
    if label == "TDGen_HeightFog":
        return actor.component
    return actor.light_component


def encode_property(value):
    if isinstance(value, unreal.StructBase):
        return {"text": value.export_text()}
    return value


def decode_property(current, stored):
    if not isinstance(stored, dict):
        return stored
    current.import_text(stored["text"])
    return current


def write_field_snapshot():
    snapshot = {}
    for label, names in FIELD_PROPERTIES.items():
        actor = by_label.get(label)
        if actor is None:
            continue
        component = find_atmosphere_component(label, actor)
        loc = actor.get_actor_location()
        rot = actor.get_actor_rotation()
        snapshot[label] = {
            "location": [loc.x, loc.y, loc.z],
            "rotation": [rot.pitch, rot.yaw, rot.roll],
            "properties": {name: encode_property(component.get_editor_property(name)) for name in names},
        }
    with open(SNAPSHOT_PATH, "w", encoding="utf-8") as f:
        json.dump(snapshot, f, ensure_ascii=False, indent=1)
    return sorted(snapshot)


def restore_field_snapshot():
    snapshot = load_json(SNAPSHOT_PATH)
    if snapshot is None:
        return []
    restored = []
    for label, entry in snapshot.items():
        actor = by_label.get(label)
        if actor is None:
            continue
        component = find_atmosphere_component(label, actor)
        x, y, z = entry["location"]
        pitch, yaw, roll = entry["rotation"]
        actor.set_actor_location(unreal.Vector(x, y, z), False, False)
        actor.set_actor_rotation(unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw), False)
        for name, stored in entry["properties"].items():
            setp(component, name, decode_property(component.get_editor_property(name), stored))
        restored.append(label)
    os.remove(SNAPSHOT_PATH)
    return restored


def apply_realm_atmosphere(atmosphere):
    applied = []
    sun = by_label.get("TDGen_Sun")
    if sun:
        pitch, yaw, roll = atmosphere["sun_rotation"]
        sun.set_actor_rotation(unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw), False)
        sun.light_component.set_intensity(atmosphere["sun_intensity_lux"])
        sun.light_component.set_light_color(linear_color(atmosphere["sun_color"]))
        applied.append("TDGen_Sun")
    sky = by_label.get("TDGen_SkyLight")
    if sky:
        sky.light_component.set_intensity(atmosphere["sky_light_intensity"])
        sky.light_component.set_light_color(linear_color(atmosphere["sky_light_color"]))
        applied.append("TDGen_SkyLight")
    atm = by_label.get("TDGen_SkyAtmosphere")
    if atm:
        sc = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
        setp(sc, "rayleigh_scattering", linear_color(atmosphere["rayleigh_scattering"]))
        setp(sc, "mie_scattering_scale", atmosphere["mie_scattering_scale"])
        setp(sc, "sky_luminance_factor", linear_color(atmosphere["sky_luminance_factor"]))
        applied.append("TDGen_SkyAtmosphere")
    fog = by_label.get("TDGen_HeightFog")
    if fog:
        fc = fog.component
        setp(fc, "fog_density", atmosphere["fog_density"])
        setp(fc, "fog_height_falloff", atmosphere["fog_height_falloff"])
        setp(fc, "fog_inscattering_luminance", linear_color(atmosphere["fog_inscattering_color"]))
        setp(fc, "fog_max_opacity", atmosphere["fog_max_opacity"])
        setp(fc, "start_distance", atmosphere["fog_start_distance_cm"])
        second = fc.get_editor_property("second_fog_data")
        second.set_editor_property("fog_density", atmosphere["second_fog_density"])
        second.set_editor_property("fog_height_falloff", atmosphere["second_fog_height_falloff"])
        second.set_editor_property("fog_height_offset", atmosphere["second_fog_height_offset_cm"])
        setp(fc, "second_fog_data", second)
        setp(fc, "volumetric_fog_albedo", unreal.MathLibrary.conv_linear_color_to_color(linear_color(atmosphere["volumetric_fog_albedo"]), True))
        setp(fc, "volumetric_fog_emissive", linear_color(atmosphere["volumetric_fog_emissive"]))
        setp(fc, "volumetric_fog_extinction_scale", atmosphere["volumetric_fog_extinction_scale"])
        loc = fog.get_actor_location()
        fog.set_actor_location(unreal.Vector(loc.x, loc.y, atmosphere["fog_height_cm"]), False, False)
        applied.append("TDGen_HeightFog")
    return applied


def preview_realm(realm_id):
    layout_path = os.path.join(WORLDGEN_DIR, "Realms", realm_id, "layout.json")
    layout = load_json(layout_path)
    if layout is None:
        raise RuntimeError("realm layout missing: " + layout_path)
    atmosphere = layout.get("realm", {}).get("atmosphere", {})
    missing = [k for k in REALM_ATMOSPHERE_KEYS if k not in atmosphere]
    if missing:
        raise RuntimeError(f"realm.atmosphere keys missing in {layout_path}: {missing}")
    snapshot_labels = None
    if not os.path.exists(SNAPSHOT_PATH):
        snapshot_labels = write_field_snapshot()
    applied = apply_realm_atmosphere(atmosphere)
    print("relight realm preview:", realm_id, "applied:", applied, "field snapshot:", snapshot_labels or "kept existing", "saved: no")


def apply_look(look):
    layout = json.load(open(LAYOUT_PATH, encoding="utf-8"))
    base_intensity = {e["label"]: e["intensity"] for e in layout["lights"]}
    sun = by_label.get("TDGen_Sun")
    if sun:
        c = sun.light_component
        if "sun_lux" in look:
            c.set_intensity(look["sun_lux"])
        if "sun_color" in look:
            c.set_light_color(unreal.LinearColor(*look["sun_color"], 1.0))
        if "sun_pitch" in look or "sun_yaw" in look:
            r = sun.get_actor_rotation()
            sun.set_actor_rotation(unreal.Rotator(roll=0, pitch=look.get("sun_pitch", r.pitch), yaw=look.get("sun_yaw", r.yaw)), False)
        if "sun_volumetric" in look:
            setp(c, "volumetric_scattering_intensity", look["sun_volumetric"])
    sky = by_label.get("TDGen_SkyLight")
    if sky and "sky_intensity" in look:
        sky.light_component.set_intensity(look["sky_intensity"])
    atm = by_label.get("TDGen_SkyAtmosphere")
    if atm and "sky_luminance" in look:
        setp(atm.get_component_by_class(unreal.SkyAtmosphereComponent), "sky_luminance_factor", unreal.LinearColor(*look["sky_luminance"], 1.0))
    fog = by_label.get("TDGen_HeightFog")
    if fog:
        fc = fog.component
        for k in ("fog_density", "fog_height_falloff", "volumetric_fog_extinction_scale", "start_distance"):
            if k in look:
                setp(fc, k, look[k])
        if "fog_color" in look:
            setp(fc, "fog_inscattering_luminance", unreal.LinearColor(*look["fog_color"], 1.0))
    ppv = by_label.get("TDGen_PostProcess")
    if ppv:
        s = ppv.settings
        if "exposure_bias" in look:
            s.set_editor_property("override_auto_exposure_bias", True)
            s.set_editor_property("auto_exposure_bias", look["exposure_bias"])
        if "saturation" in look:
            v = look["saturation"]
            s.set_editor_property("override_color_saturation", True)
            s.set_editor_property("color_saturation", unreal.Vector4(v, v, v, 1.0))
        if "contrast" in look:
            v = look["contrast"]
            s.set_editor_property("override_color_contrast", True)
            s.set_editor_property("color_contrast", unreal.Vector4(v, v, v, 1.0))
        if look.get("neutral_grade"):
            for k, v in (("color_gain", (1.0, 1.0, 1.0)), ("color_gain_shadows", (0.97, 0.98, 1.02)), ("color_saturation_shadows", (0.8, 0.8, 0.85)), ("color_gain_highlights", (1.02, 1.0, 0.97)), ("color_gamma", (1.0, 1.0, 1.0))):
                s.set_editor_property("override_" + k, True)
                s.set_editor_property(k, unreal.Vector4(v[0], v[1], v[2], 1.0))
            s.set_editor_property("override_color_offset_shadows", False)
        if "vignette" in look:
            s.set_editor_property("override_vignette_intensity", True)
            s.set_editor_property("vignette_intensity", look["vignette"])
        ppv.set_editor_property("settings", s)
    scale = look.get("local_light_scale")
    n = 0
    if scale is not None:
        for label, base in base_intensity.items():
            a = by_label.get(label)
            if a is None:
                continue
            a.light_component.set_intensity(base * scale)
            n += 1
    print("relight applied:", look, "lights:", n)


def resolve_look_path(target):
    path = target.get("look")
    if not path:
        return LOOK_PATH
    if os.path.isabs(path):
        return path
    return os.path.join(unreal.SystemLibrary.get_project_directory(), path)


target = load_json(TARGET_PATH) or {}
if target.get("realm"):
    preview_realm(target["realm"])
else:
    print("field snapshot restored:", restore_field_snapshot())
    apply_look(load_json(resolve_look_path(target)) or {})

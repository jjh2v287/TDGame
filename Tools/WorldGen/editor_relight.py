"""에디터 안에서 실행: 전체 재빌드 없이 조명·노출·안개만 다시 적용한다(빠른 반복용).

값은 Saved/WorldGen/look.json 에서 읽는다. 예:
{"sun_lux": 6.0, "sky_intensity": 2.2, "exposure_bias": 10.2, "fog_density": 0.022, "local_light_scale": 0.4,
 "sun_pitch": -52, "sun_yaw": 38, "sun_color": [0.8, 0.86, 1.0], "saturation": 0.8}
국소 조명 기본 세기는 layout.json 의 lights 항목(라벨로 대응)에서 다시 읽어 local_light_scale 을 곱한다.
"""
import json
import os

import unreal

LOOK_PATH = r"C:\Project\TDGame\Saved\WorldGen\look.json"
LAYOUT_PATH = r"C:\Project\TDGame\Saved\WorldGen\AshenVale\layout.json"
look = json.load(open(LOOK_PATH, encoding="utf-8")) if os.path.exists(LOOK_PATH) else {}
layout = json.load(open(LAYOUT_PATH, encoding="utf-8"))
base_intensity = {e["label"]: e["intensity"] for e in layout["lights"]}

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
by_label = {a.get_actor_label(): a for a in eas.get_all_level_actors()}


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as e:
        print("skip", name, e)


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

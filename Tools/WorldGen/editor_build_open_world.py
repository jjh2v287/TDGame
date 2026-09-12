"""에디터 안에서 실행: generate_ashen_vale.py 가 만든 layout.json 을 LV_DarkFantasy_OpenWorld 레벨에 반영한다.

절차: 레벨 로드 → 기존 생성물(TDGen_*)과 옛 액터 정리 → 랜드스케이프 생성(C++ UTDLandscapeEditorLibrary) →
      조명/안개/후처리 → 인스턴스 메시 셀 액터 → 고유 액터/평면/조명/나이아가라/마커 → 저장.
환경 변수 TD_WORLDGEN_LAYOUT 으로 layout.json 경로를 바꿀 수 있다.
"""
import json
import os
import time

import unreal

LEVEL_PATH = "/Game/Level/LV_DarkFantasy_OpenWorld"
LAYOUT_PATH = os.environ.get("TD_WORLDGEN_LAYOUT", r"C:\Project\TDGame\Saved\WorldGen\AshenVale\layout.json")
LOOK = {"sun_lux": 7.0, "sky_intensity": 1.8, "exposure_bias": 10.4, "fog_density": 0.022, "local_light_scale": 0.4}
KEEP_CLASSES = {"WorldSettings", "WorldDataLayers", "WorldPartitionMiniMap", "Brush", "LevelBounds"}

EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

t0 = time.time()


def log(msg):
    print(f"[TDWorldGen {time.time() - t0:6.1f}s] {msg}")


def setp(obj, name, value):
    """이름 후보를 차례로 시도해 프로퍼티를 설정한다. 실패해도 중단하지 않고 로그만 남긴다."""
    names = [name] if isinstance(name, str) else list(name)
    for n in names:
        try:
            obj.set_editor_property(n, value)
            return True
        except Exception:
            continue
    log(f"property skip {obj.get_class().get_name()}.{names[0]}")
    return False


def vec(v):
    return unreal.Vector(v[0], v[1], v[2])


def rot(r):
    return unreal.Rotator(roll=r[2], pitch=r[0], yaw=r[1])


def finish_actor(actor, label, folder, spatially_loaded=True, tags=None):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    try:
        actor.set_editor_property("is_spatially_loaded", spatially_loaded)
    except Exception:
        pass
    if tags:
        actor.tags = [unreal.Name(t) for t in tags]
    return actor


def clear_level(world):
    removed = unreal.TDLandscapeEditorLibrary.destroy_all_landscape_actors(world)
    log(f"landscape actors removed: {removed}")
    n = 0
    for a in list(eas.get_all_level_actors()):
        cn = a.get_class().get_name()
        if cn in KEEP_CLASSES:
            continue
        eas.destroy_actor(a)
        n += 1
    log(f"other actors removed: {n}")


def create_landscape(world, cfg):
    req = unreal.TDLandscapeCreateRequest()
    req.set_editor_property("location", vec(cfg["location"]))
    req.set_editor_property("scale", vec(cfg["scale"]))
    req.set_editor_property("component_count_x", cfg["components_x"])
    req.set_editor_property("component_count_y", cfg["components_y"])
    req.set_editor_property("sections_per_component", cfg["sections_per_component"])
    req.set_editor_property("quads_per_section", cfg["quads_per_section"])
    req.set_editor_property("heightmap_raw_path", cfg["heightmap"])
    req.set_editor_property("layer_names", [unreal.Name(l["name"]) for l in cfg["layers"]])
    req.set_editor_property("layer_raw_paths", [l["path"] for l in cfg["layers"]])
    req.set_editor_property("layer_info_package_path", "/Game/World/Landscape/Layers")
    req.set_editor_property("material", EAL.load_asset(cfg["material"]))
    req.set_editor_property("world_partition_grid_size_in_components", cfg["world_partition_grid_size"])
    landscape = unreal.TDLandscapeEditorLibrary.create_landscape_from_raw_files(world, req)
    if landscape is None:
        raise RuntimeError("landscape creation failed (see LogTDLandscape)")
    for proxy in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy):
        try:
            proxy.set_editor_property("enable_nanite", True)
        except Exception as e:
            log(f"nanite flag skipped on {proxy.get_actor_label()}: {e}")
        proxy.set_folder_path("TDGen/Landscape")
    try:
        unreal.get_editor_subsystem(unreal.LandscapeSubsystem).build_grass_maps()
        log("grass maps build requested")
    except Exception as e:
        log(f"grass maps build not available: {str(e)[:80]}")
    log(f"landscape created: {landscape.get_actor_label()} proxies={len(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeStreamingProxy))}")
    return landscape


def setup_atmosphere(world, region):
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 5000), unreal.Rotator(roll=0, pitch=-55, yaw=38))
    c = sun.light_component
    setp(c, "mobility", unreal.ComponentMobility.MOVABLE)
    c.set_intensity(LOOK["sun_lux"])
    c.set_light_color(unreal.LinearColor(0.93, 0.94, 1.0, 1.0))
    setp(c, "use_temperature", False)
    setp(c, "light_source_angle", 1.2)
    setp(c, "atmosphere_sun_light", True)
    setp(c, "cast_volumetric_shadow", True)
    setp(c, "volumetric_scattering_intensity", 3.0)
    setp(c, "dynamic_shadow_distance_movable_light", 12000.0)
    setp(c, "cascade_distribution_exponent", 2.5)
    setp(c, "dynamic_shadow_cascades", 4)
    setp(c, "enable_light_shaft_occlusion", True)
    setp(c, "enable_light_shaft_bloom", True)
    setp(c, "bloom_scale", 0.15)
    setp(c, "bloom_tint", unreal.Color(r=190, g=205, b=255, a=255))
    finish_actor(sun, "TDGen_Sun", "TDGen/Atmosphere", False)

    sky = eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    sc = sky.get_component_by_class(unreal.SkyAtmosphereComponent)
    setp(sc, "sky_luminance_factor", unreal.LinearColor(0.7, 0.72, 0.82, 1.0))
    setp(sc, "aerial_pespective_view_distance_scale", 1.6)
    setp(sc, "rayleigh_scattering", unreal.LinearColor(0.35, 0.5, 1.0, 1.0))
    setp(sc, "mie_scattering_scale", 0.008)
    setp(sc, "mie_anisotropy", 0.7)
    finish_actor(sky, "TDGen_SkyAtmosphere", "TDGen/Atmosphere", False)

    skylight = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 3000))
    sl = skylight.light_component
    setp(sl, "mobility", unreal.ComponentMobility.MOVABLE)
    setp(sl, "real_time_capture", True)
    sl.set_intensity(LOOK["sky_intensity"])
    setp(sl, "volumetric_scattering_intensity", 0.6)
    setp(sl, "lower_hemisphere_color", unreal.LinearColor(0.02, 0.025, 0.035, 1.0))
    finish_actor(skylight, "TDGen_SkyLight", "TDGen/Atmosphere", False)

    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -200))
    fc = fog.component
    setp(fc, "fog_density", LOOK["fog_density"])
    setp(fc, "fog_height_falloff", 0.35)
    setp(fc, "fog_inscattering_luminance", unreal.LinearColor(0.12, 0.13, 0.16, 1.0))
    setp(fc, "directional_inscattering_luminance", unreal.LinearColor(0.12, 0.13, 0.16, 1.0))
    setp(fc, "directional_inscattering_exponent", 8.0)
    setp(fc, "fog_max_opacity", 0.92)
    setp(fc, "start_distance", 600.0)
    setp(fc, ("enable_volumetric_fog", "volumetric_fog"), True)
    setp(fc, "volumetric_fog_scattering_distribution", 0.35)
    setp(fc, "volumetric_fog_albedo", unreal.Color(r=175, g=185, b=205, a=255))
    setp(fc, "volumetric_fog_extinction_scale", 2.2)
    setp(fc, "volumetric_fog_distance", 9000.0)
    second = unreal.ExponentialHeightFogData()
    second.set_editor_property("fog_density", 0.06)
    second.set_editor_property("fog_height_falloff", 1.8)
    second.set_editor_property("fog_height_offset", -260.0)
    setp(fc, "second_fog_data", second)
    finish_actor(fog, "TDGen_HeightFog", "TDGen/Atmosphere", False)

    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
    ppv.set_editor_property("unbound", True)
    s = ppv.settings
    overrides = {
        "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
        "auto_exposure_bias": LOOK["exposure_bias"],
        "color_saturation": unreal.Vector4(0.85, 0.85, 0.85, 1.0),
        "color_contrast": unreal.Vector4(1.05, 1.05, 1.05, 1.0),
        "color_gain": unreal.Vector4(1.0, 1.0, 1.0, 1.0),
        "color_gamma": unreal.Vector4(1.0, 1.0, 1.0, 1.0),
        "color_saturation_shadows": unreal.Vector4(0.8, 0.8, 0.85, 1.0),
        "color_gain_shadows": unreal.Vector4(0.97, 0.98, 1.02, 1.0),
        "color_saturation_highlights": unreal.Vector4(0.9, 0.85, 0.75, 1.0),
        "color_gain_highlights": unreal.Vector4(1.02, 1.0, 0.97, 1.0),
        "vignette_intensity": 0.5,
        "film_grain_intensity": 0.18,
        "bloom_intensity": 0.55,
        "bloom_threshold": 0.6,
        "scene_fringe_intensity": 0.12,
        "sharpen": 0.3,
        "ambient_occlusion_intensity": 0.7,
        "ambient_occlusion_radius": 120.0,
        "dynamic_global_illumination_method": unreal.DynamicGlobalIlluminationMethod.LUMEN,
        "reflection_method": unreal.ReflectionMethod.LUMEN,
        "lumen_scene_lighting_quality": 1.0,
        "lumen_final_gather_quality": 1.0,
        "lumen_scene_detail": 1.0,
        "local_exposure_highlight_contrast_scale": 0.85,
        "local_exposure_shadow_contrast_scale": 0.85,
        "film_slope": 0.92,
        "film_toe": 0.6,
        "film_shoulder": 0.28,
        "film_black_clip": 0.0,
        "film_white_clip": 0.04,
        "tone_curve_amount": 1.0,
    }
    for k, v in overrides.items():
        try:
            s.set_editor_property("override_" + k, True)
            s.set_editor_property(k, v)
        except Exception as e:
            log(f"pp skip {k}: {e}")
    ppv.set_editor_property("settings", s)
    finish_actor(ppv, "TDGen_PostProcess", "TDGen/Atmosphere", False)
    log("atmosphere done")


def spawn_ism_cells(layout):
    cells = layout["ism"]
    n_inst = 0
    for entry in cells:
        mesh = EAL.load_asset(entry["mesh"])
        if mesh is None:
            log(f"mesh missing {entry['mesh']}")
            continue
        cx, cy = entry["cell"]
        tr = entry["transforms"]
        center = unreal.Vector(sum(t[0] for t in tr) / len(tr), sum(t[1] for t in tr) / len(tr), sum(t[2] for t in tr) / len(tr))
        actor = eas.spawn_actor_from_class(unreal.TDInstancedMeshActor, center, unreal.Rotator(0, 0, 0))
        comp = actor.instanced_mesh_component
        comp.set_static_mesh(mesh)
        comp.set_editor_property("cast_shadow", True)
        transforms = []
        for t in tr:
            transforms.append(unreal.Transform(location=unreal.Vector(t[0], t[1], t[2]), rotation=unreal.Rotator(roll=t[5], pitch=t[3], yaw=t[4]), scale=unreal.Vector(t[6], t[7], t[8])))
        comp.add_instances(transforms, True, True)
        finish_actor(actor, f"TDGen_ISM_{entry['key']}_{cx}_{cy}", f"TDGen/Instances/{entry['key']}", True, ["TDGen", "TDGenISM"])
        n_inst += len(tr)
    log(f"ism cells {len(cells)} instances {n_inst}")


def spawn_actors(layout):
    n = 0
    for e in layout["actors"]:
        mesh = EAL.load_asset(e["mesh"])
        if mesh is None:
            log(f"mesh missing {e['mesh']}")
            continue
        actor = eas.spawn_actor_from_object(mesh, vec(e["loc"]), rot(e["rot"]))
        actor.set_actor_scale3d(vec(e["scale"]))
        comp = actor.static_mesh_component
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        for i, mp in enumerate(e.get("materials", [])):
            m = EAL.load_asset(mp)
            if m:
                comp.set_material(i, m)
        finish_actor(actor, e["label"], "TDGen/Props", True, ["TDGen"] + e.get("tags", []))
        n += 1
    log(f"actors {n}")


def spawn_planes(layout):
    n = 0
    for e in layout["planes"]:
        mesh = EAL.load_asset(e["mesh"])
        mat = EAL.load_asset(e["material"])
        if mesh is None or mat is None:
            log(f"plane asset missing {e}")
            continue
        actor = eas.spawn_actor_from_object(mesh, vec(e["loc"]), rot(e["rot"]))
        actor.set_actor_scale3d(vec(e["scale"]))
        comp = actor.static_mesh_component
        comp.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        comp.set_material(0, mat)
        comp.set_editor_property("cast_shadow", False)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        finish_actor(actor, e["label"], "TDGen/Planes", True, ["TDGen"])
        n += 1
    log(f"planes {n}")


def spawn_lights(layout):
    n = 0
    for e in layout["lights"]:
        cls = unreal.SpotLight if e["type"] == "Spot" else unreal.PointLight
        actor = eas.spawn_actor_from_class(cls, vec(e["loc"]), rot(e["rot"]))
        c = actor.light_component
        setp(c, "mobility", unreal.ComponentMobility.MOVABLE)
        setp(c, "intensity_units", unreal.LightUnits.CANDELAS)
        c.set_intensity(e["intensity"] * LOOK["local_light_scale"])
        c.set_light_color(unreal.LinearColor(e["color"][0], e["color"][1], e["color"][2], 1.0))
        setp(c, "attenuation_radius", e["radius"])
        setp(c, "source_radius", e.get("source_radius", 8.0))
        setp(c, "soft_source_radius", e.get("source_radius", 8.0) * 1.5)
        setp(c, "volumetric_scattering_intensity", e.get("volumetric", 1.0))
        setp(c, "cast_shadows", bool(e.get("cast_shadows", True)))
        setp(c, "use_temperature", False)
        if e["type"] == "Spot":
            setp(c, "outer_cone_angle", e["cone"])
            setp(c, "inner_cone_angle", e["inner"])
        if e.get("light_function"):
            lf = EAL.load_asset(e["light_function"])
            if lf:
                setp(c, "light_function_material", lf)
                setp(c, "light_function_scale", unreal.Vector(4000, 4000, 4000))
        finish_actor(actor, e["label"], "TDGen/Lights", True, ["TDGen"])
        n += 1
    log(f"lights {n}")


def spawn_niagara(layout):
    n = 0
    for e in layout["niagara"]:
        asset = EAL.load_asset(e["asset"])
        if asset is None:
            continue
        actor = eas.spawn_actor_from_class(unreal.NiagaraActor, vec(e["loc"]), unreal.Rotator(0, 0, 0))
        actor.niagara_component.set_asset(asset)
        finish_actor(actor, e["label"], "TDGen/FX", True, ["TDGen"])
        n += 1
    log(f"niagara {n}")


def spawn_markers(layout):
    for e in layout["markers"]:
        kind = e["kind"]
        if kind == "PlayerStart":
            actor = eas.spawn_actor_from_class(unreal.PlayerStart, vec(e["loc"]), rot(e["rot"]))
            finish_actor(actor, "PlayerStart", "TDGen/Markers", False)
        elif kind == "NavMeshBounds":
            actor = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(e["loc"]), unreal.Rotator(0, 0, 0))
            actor.set_actor_scale3d(vec(e["scale"]))
            finish_actor(actor, e["label"], "TDGen/Markers", False, ["TDGen"])
        else:
            actor = eas.spawn_actor_from_class(unreal.TargetPoint, vec(e["loc"]), rot(e["rot"]))
            finish_actor(actor, e["label"], "TDGen/Markers", True, ["TDGen"] + e.get("tags", []))
    log(f"markers {len(layout['markers'])}")


def main():
    layout = json.load(open(LAYOUT_PATH, encoding="utf-8"))
    les.load_level(LEVEL_PATH)
    world = ues.get_editor_world()
    log(f"level loaded: {world.get_name()}")
    clear_level(world)
    create_landscape(world, layout["landscape"])
    setup_atmosphere(world, layout["region"])
    spawn_ism_cells(layout)
    spawn_actors(layout)
    spawn_planes(layout)
    spawn_lights(layout)
    spawn_niagara(layout)
    spawn_markers(layout)
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved: {ok}; actors now {len(eas.get_all_level_actors())}")


main()

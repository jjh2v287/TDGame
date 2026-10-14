"""에디터 안에서 실행: generate_ashen_vale.py 가 만든 layout.json 을 LV_DarkFantasy_OpenWorld 레벨에 반영한다.
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_build_open_world.py (PowerShell). Saved/WorldGen/open_world_build_request.json 이 {"mode":"partial","previous_layout":"<경로>"} 이면 부분 모드, 없으면 전체 모드
출력: /Game/Level/LV_DarkFantasy_OpenWorld 와 외부 액터 패키지 저장
상태: 현행. 전체 모드는 이계(TDRealm 태그) 액터와 필드 상자(±60000cm) 밖 랜드스케이프만 남긴다. 부분 모드는 이전·새 layout 라벨·TDGen_ISM_·필드 랜드스케이프만 다시 만들고 던전·PCG·이계·대기·PlayerStart·내비 경계를 보존한다. editor_build_realm 이 spawn 함수를 가져다 쓴다.

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
REALM_TAG = "TDRealm"
FIELD_BOX_MIN = (-60000.0, -60000.0, -1.0e6)
FIELD_BOX_MAX = (60000.0, 60000.0, 1.0e6)
BUILD_REQUEST_PATH = os.environ.get("TD_WORLDGEN_BUILD_REQUEST", r"C:\Project\TDGame\Saved\WorldGen\open_world_build_request.json")
ATMOSPHERE_LABELS = {"TDGen_Sun", "TDGen_SkyAtmosphere", "TDGen_SkyLight", "TDGen_HeightFog", "TDGen_PostProcess"}
PARTIAL_KEPT_MARKER_KINDS = ("PlayerStart", "NavMeshBounds")
PARTIAL_PROTECTED_TAGS = ("TDRealm", "TDGenDungeon", "TDGenPCG", "TDLocked", "TDDungeonEntry", "TDDungeonSlot")
PARTIAL_PROTECTED_PREFIXES = ("TDGenDungeon_", "TDGen_Exit_", "TDGen_TestChest_", "TDGen_Tool_", "TDGen_PCG", "TDRealm_")
PARTIAL_PROTECTED_CLASSES = KEEP_CLASSES | {"RecastNavMesh", "PCGWorldActor", "PCGPartitionActor"}
GENERATED_ISM_PREFIX = "TDGen_ISM_"
GENERATED_ISM_TAG = "TDGenISM"

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


def apply_label_prefix(label, label_prefix):
    if label.startswith(label_prefix):
        return label
    return label_prefix + label


def is_xy_in_box(x, y, box_min, box_max, margin=0.0):
    return box_min[0] - margin <= x <= box_max[0] + margin and box_min[1] - margin <= y <= box_max[1] + margin


def landscape_center_xy(proxy):
    origin, extent = proxy.get_actor_bounds(False)
    if extent.x > 1.0 or extent.y > 1.0:
        return origin.x, origin.y
    location = proxy.get_actor_location()
    return location.x, location.y


def landscape_proxies_in_box(world, box_min, box_max, margin=0.0):
    found = []
    for proxy in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy):
        x, y = landscape_center_xy(proxy)
        if is_xy_in_box(x, y, box_min, box_max, margin):
            found.append(proxy)
    return found


def apply_static_mesh_materials(comp, entry):
    for i, mp in enumerate(entry.get("materials", [])):
        m = EAL.load_asset(mp)
        if m:
            comp.set_material(i, m)
    if not entry.get("material"):
        return
    m = EAL.load_asset(entry["material"])
    if m is None:
        log(f"material missing {entry['material']}")
        return
    for i in range(comp.get_num_materials()):
        comp.set_material(i, m)


def clear_level(world):
    removed = unreal.TDLandscapeEditorLibrary.destroy_landscape_actors_in_bounds(world, vec(FIELD_BOX_MIN), vec(FIELD_BOX_MAX))
    log(f"landscape actors removed: {removed}")
    n = 0
    kept_realm = 0
    for a in list(eas.get_all_level_actors()):
        cn = a.get_class().get_name()
        if cn in KEEP_CLASSES:
            continue
        if a.actor_has_tag(REALM_TAG):
            kept_realm += 1
            continue
        eas.destroy_actor(a)
        n += 1
    log(f"other actors removed: {n}; realm actors kept: {kept_realm}")


def create_landscape_actor(world, cfg):
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
    return landscape


def create_landscape(world, cfg):
    landscape = create_landscape_actor(world, cfg)
    for proxy in landscape_proxies_in_box(world, FIELD_BOX_MIN, FIELD_BOX_MAX):
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


def disable_collision(comp):
    comp.set_collision_profile_name("NoCollision")
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)


def spawn_ism_cells(layout, label_prefix="TDGen_", folder_root="TDGen", extra_tags=()):
    cells = layout["ism"]
    n_cells = 0
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
        if entry.get("collision") == "none":
            disable_collision(comp)
        apply_static_mesh_materials(comp, entry)
        transforms = []
        for t in tr:
            transforms.append(unreal.Transform(location=unreal.Vector(t[0], t[1], t[2]), rotation=unreal.Rotator(roll=t[5], pitch=t[3], yaw=t[4]), scale=unreal.Vector(t[6], t[7], t[8])))
        comp.add_instances(transforms, True, True)
        label = apply_label_prefix(f"ISM_{entry['key']}_{cx}_{cy}", label_prefix)
        finish_actor(actor, label, f"{folder_root}/Instances/{entry['key']}", True, ["TDGen", "TDGenISM"] + list(extra_tags))
        n_cells += 1
        n_inst += len(tr)
    log(f"ism cells {len(cells)} spawned {n_cells} instances {n_inst}")
    return n_cells, n_inst


def spawn_actors(layout, label_prefix="", folder_root="TDGen", extra_tags=()):
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
        if e.get("collision") == "none":
            disable_collision(comp)
        apply_static_mesh_materials(comp, e)
        finish_actor(actor, apply_label_prefix(e["label"], label_prefix), f"{folder_root}/Props", True, ["TDGen"] + e.get("tags", []) + list(extra_tags))
        n += 1
    log(f"actors {n}")
    return n


def spawn_planes(layout, label_prefix="", folder_root="TDGen", extra_tags=()):
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
        comp.set_collision_profile_name("NoCollision")
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        finish_actor(actor, apply_label_prefix(e["label"], label_prefix), f"{folder_root}/Planes", True, ["TDGen"] + list(extra_tags))
        n += 1
    log(f"planes {n}")
    return n


def spawn_lights(layout, label_prefix="", folder_root="TDGen", extra_tags=()):
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
        finish_actor(actor, apply_label_prefix(e["label"], label_prefix), f"{folder_root}/Lights", True, ["TDGen"] + list(extra_tags))
        n += 1
    log(f"lights {n}")
    return n


def spawn_niagara(layout, label_prefix="", folder_root="TDGen", extra_tags=()):
    n = 0
    for e in layout["niagara"]:
        asset = EAL.load_asset(e["asset"])
        if asset is None:
            continue
        actor = eas.spawn_actor_from_class(unreal.NiagaraActor, vec(e["loc"]), unreal.Rotator(0, 0, 0))
        actor.niagara_component.set_asset(asset)
        finish_actor(actor, apply_label_prefix(e["label"], label_prefix), f"{folder_root}/FX", True, ["TDGen"] + list(extra_tags))
        n += 1
    log(f"niagara {n}")
    return n


def spawn_pcg_exclusion_box(e, label, folder, tags):
    actor = eas.spawn_actor_from_class(unreal.TriggerBox, vec(e["loc"]), rot(e["rot"]))
    box = actor.get_component_by_class(unreal.BoxComponent)
    box.set_box_extent(vec(e["box_extent_cm"]), False)
    box.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    finish_actor(actor, label, folder, False, tags)
    return actor


def spawn_markers(layout, label_prefix="", folder_root="TDGen", extra_tags=(), skip_kinds=()):
    folder = f"{folder_root}/Markers"
    for e in layout["markers"]:
        kind = e["kind"]
        if kind in skip_kinds:
            log(f"marker kept (already in level): {kind}")
            continue
        if kind == "PlayerStart":
            actor = eas.spawn_actor_from_class(unreal.PlayerStart, vec(e["loc"]), rot(e["rot"]))
            finish_actor(actor, apply_label_prefix("PlayerStart", label_prefix), folder, False, list(extra_tags))
        elif kind == "NavMeshBounds":
            actor = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(e["loc"]), unreal.Rotator(0, 0, 0))
            actor.set_actor_scale3d(vec(e["scale"]))
            finish_actor(actor, apply_label_prefix(e["label"], label_prefix), folder, False, ["TDGen"] + list(extra_tags))
        elif kind == "PcgExclusion":
            spawn_pcg_exclusion_box(e, apply_label_prefix(e["label"], label_prefix), folder, ["TDGen"] + e.get("tags", []) + list(extra_tags))
        else:
            actor = eas.spawn_actor_from_class(unreal.TargetPoint, vec(e["loc"]), rot(e["rot"]))
            finish_actor(actor, apply_label_prefix(e["label"], label_prefix), folder, True, ["TDGen"] + e.get("tags", []) + list(extra_tags))
    log(f"markers {len(layout['markers'])}")
    return len(layout["markers"])


def read_build_request():
    if not os.path.isfile(BUILD_REQUEST_PATH):
        return {"mode": "full"}
    with open(BUILD_REQUEST_PATH, encoding="utf-8-sig") as fp:
        return json.load(fp)


def layout_labels(layout):
    labels = set()
    for key in ("actors", "planes", "lights", "niagara"):
        labels.update(e["label"] for e in layout.get(key, []))
    labels.update(e["label"] for e in layout.get("markers", []) if e["kind"] not in PARTIAL_KEPT_MARKER_KINDS)
    return labels


def actor_tag_names(actor):
    return {str(t) for t in actor.tags}


def is_partial_protected(label, class_name, tags):
    if class_name in PARTIAL_PROTECTED_CLASSES or label in ATMOSPHERE_LABELS:
        return True
    if label.startswith(PARTIAL_PROTECTED_PREFIXES):
        return True
    return any(tag in tags for tag in PARTIAL_PROTECTED_TAGS)


def is_partial_delete_target(label, tags, delete_labels):
    if label in delete_labels:
        return True
    return label.startswith(GENERATED_ISM_PREFIX) and GENERATED_ISM_TAG in tags


def is_desc_for_field(desc, delete_labels):
    native_class = desc.native_class
    if native_class is not None and "HLOD" in native_class.get_name():
        return False
    label = str(desc.label)
    if label in delete_labels or label.startswith(GENERATED_ISM_PREFIX):
        return True
    bounds = desc.bounds
    if not bounds.is_valid:
        return False
    return bounds.max.x >= FIELD_BOX_MIN[0] and bounds.min.x <= FIELD_BOX_MAX[0] and bounds.max.y >= FIELD_BOX_MIN[1] and bounds.min.y <= FIELD_BOX_MAX[1]


def load_field_actors(delete_labels):
    library = unreal.WorldPartitionBlueprintLibrary
    descs = library.get_actor_descs()
    if descs is None:
        log("WARNING world partition actor descs unavailable; only loaded actors are handled")
        return 0
    guids = [desc.guid for desc in descs if is_desc_for_field(desc, delete_labels)]
    if guids:
        library.load_actors(guids)
    log(f"world partition field actors loaded: {len(guids)} of {len(descs)}")
    return len(guids)


def clear_field_partial(world, delete_labels):
    counts = {"landscape": unreal.TDLandscapeEditorLibrary.destroy_landscape_actors_in_bounds(world, vec(FIELD_BOX_MIN), vec(FIELD_BOX_MAX)),
              "deleted_layout_labels": 0, "deleted_ism": 0, "protected": 0, "untouched": 0, "outside_field": 0}
    for actor in list(eas.get_all_level_actors()):
        label = actor.get_actor_label()
        tags = actor_tag_names(actor)
        if is_partial_protected(label, actor.get_class().get_name(), tags):
            counts["protected"] += 1
            continue
        if not is_partial_delete_target(label, tags, delete_labels):
            counts["untouched"] += 1
            continue
        location = actor.get_actor_location()
        if not is_xy_in_box(location.x, location.y, FIELD_BOX_MIN, FIELD_BOX_MAX):
            counts["outside_field"] += 1
            continue
        counts["deleted_layout_labels" if label in delete_labels else "deleted_ism"] += 1
        eas.destroy_actor(actor)
    log(f"partial clear: {counts}")
    return counts


def existing_kept_marker_kinds():
    kinds = set()
    for actor in eas.get_all_level_actors():
        if actor.get_class().get_name() == "PlayerStart":
            kinds.add("PlayerStart")
        if actor.get_actor_label() == "TDGen_NavMeshBounds":
            kinds.add("NavMeshBounds")
    return kinds


def spawn_field_content(layout, skip_marker_kinds=()):
    spawn_ism_cells(layout)
    spawn_actors(layout)
    spawn_planes(layout)
    spawn_lights(layout)
    spawn_niagara(layout)
    spawn_markers(layout, skip_kinds=skip_marker_kinds)


def build_full(world, layout):
    clear_level(world)
    create_landscape(world, layout["landscape"])
    setup_atmosphere(world, layout["region"])
    spawn_field_content(layout)


def build_partial(world, layout, request):
    previous_path = request.get("previous_layout", "")
    if not os.path.isfile(previous_path):
        raise RuntimeError(f"partial build needs previous_layout, not found: {previous_path}")
    with open(previous_path, encoding="utf-8") as fp:
        previous = json.load(fp)
    delete_labels = layout_labels(previous) | layout_labels(layout)
    log(f"partial: delete label set {len(delete_labels)} (previous {previous_path})")
    load_field_actors(delete_labels)
    clear_field_partial(world, delete_labels)
    create_landscape(world, layout["landscape"])
    spawn_field_content(layout, existing_kept_marker_kinds())


def main():
    layout = json.load(open(LAYOUT_PATH, encoding="utf-8"))
    request = read_build_request()
    mode = request.get("mode", "full")
    les.load_level(LEVEL_PATH)
    world = ues.get_editor_world()
    log(f"level loaded: {world.get_name()}; build mode {mode}")
    if mode == "partial":
        build_partial(world, layout, request)
    elif mode == "full":
        build_full(world, layout)
    else:
        raise RuntimeError(f"unknown build mode {mode} in {BUILD_REQUEST_PATH}")
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved: {ok}; actors now {len(eas.get_all_level_actors())}")


if __name__ == "__main__":
    main()

"""에디터 안에서 실행: generate_ashen_vale.py 가 만든 layout.json 을 LV_DarkFantasy_OpenWorld 레벨에 반영한다(필드 소유 액터는 데이터 레이어 DL_TDRegion_Field, P3-19 명세 2.4·4.1).
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_build_open_world.py (PowerShell, editor_setup_data_layers.py 다음). Saved/WorldGen/open_world_build_request.json 이 {"mode":"partial","previous_layout":"<경로>"} 이면 부분 모드, 없으면 전체 모드
출력: /Game/Level/LV_DarkFantasy_OpenWorld 와 외부 액터 패키지 저장, Saved/WorldGen/open_world_build_report.json(삭제 사유별 개수·랜드스케이프 DL 검사·랜드스케이프 수 전후)
상태: 현행(P3-19 규칙은 에디터 실행 검증 전). 랜드스케이프는 TDRealm 태그가 없는 것만, 그 밖의 액터는 DL_TDRegion_Field 소속과 전환기 규칙(지역 DL 없음, 이전·새 layout 라벨+TDGen 태그는 위치와 무관, 그 밖의 layout 라벨·TDGen_ISM_ 는 ±50450cm 상자 안)·TDGen_SeamPcgExclusion_ 만 지운다. 이계(TDRealm 접두·태그·이계 DL)·던전·PCG·대기·PlayerStart 는 보존하고, 레벨에 있거나 Saved/WorldGen/Realms/<R>/layout.json 이 있는 이계마다 랜드스케이프가 17개가 아니면 지우기 전에 멈춘다. editor_build_realm 이 spawn·랜드스케이프 함수를 가져다 쓴다.

절차: 레벨 로드 → 기존 생성물(TDGen_*)과 옛 액터 정리 → 랜드스케이프 생성(C++ UTDLandscapeEditorLibrary) →
      조명/안개/후처리 → 인스턴스 메시 셀 액터 → 고유 액터/평면/조명/나이아가라/마커 → 저장.
환경 변수 TD_WORLDGEN_LAYOUT 으로 layout.json 경로를 바꿀 수 있다.
"""
import importlib
import json
import os
import sys
import time

import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
WORLDGEN_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "WorldGen")
if WORLDGEN_TOOLS_DIR not in sys.path:
    sys.path.insert(0, WORLDGEN_TOOLS_DIR)

data_layers = importlib.reload(importlib.import_module("editor_setup_data_layers"))

LEVEL_PATH = "/Game/Level/LV_DarkFantasy_OpenWorld"
LAYOUT_PATH = os.environ.get("TD_WORLDGEN_LAYOUT", r"C:\Project\TDGame\Saved\WorldGen\AshenVale\layout.json")
LOOK = {"sun_lux": 7.0, "sky_intensity": 1.8, "exposure_bias": 10.4, "fog_density": 0.022, "local_light_scale": 0.4}
KEEP_CLASSES = {"WorldSettings", "WorldDataLayers", "WorldPartitionMiniMap", "Brush", "LevelBounds"}
REALM_TAG = "TDRealm"
REALM_TAG_PREFIX = "TDRealm:"
REALM_LABEL_PREFIX = "TDRealm_"
FIELD_BOX_MIN = (-50450.0, -50450.0, -1.0e6)
FIELD_BOX_MAX = (50450.0, 50450.0, 1.0e6)
FIELD_BUILDER_TAG = "TDGen"
FIELD_LANDSCAPE_TAGS = (FIELD_BUILDER_TAG, "TDRegion:Field")
FIELD_LANDSCAPE_FOLDER = "TDGen/Landscape"
REALM_LANDSCAPE_ACTOR_COUNT = 17
REALM_LAYOUTS_DIR = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "Realms")
REALM_LAYOUT_FILE_NAME = "layout.json"
LANDSCAPE_CLASS_NAMES = ("Landscape", "LandscapeStreamingProxy")
BUILD_REQUEST_PATH = os.environ.get("TD_WORLDGEN_BUILD_REQUEST", r"C:\Project\TDGame\Saved\WorldGen\open_world_build_request.json")
BUILD_REPORT_PATH = os.environ.get("TD_WORLDGEN_BUILD_REPORT", os.path.join(PROJECT_DIR, "Saved", "WorldGen", "open_world_build_report.json"))
ATMOSPHERE_LABELS = {"TDGen_Sun", "TDGen_SkyAtmosphere", "TDGen_SkyLight", "TDGen_HeightFog", "TDGen_PostProcess"}
PARTIAL_KEPT_MARKER_KINDS = ("PlayerStart",)
OUTSIDE_LAYER_MARKER_KINDS = ("PlayerStart",)
SEAM_RESIDUE_PREFIXES = ("TDGen_SeamPcgExclusion_",)
FIELD_LAYER_NAME = data_layers.region_layer_name(data_layers.FIELD_REGION_ID)
REALM_LAYER_NAMES = frozenset(data_layers.region_layer_name(realm_id) for realm_id in data_layers.REALM_IDS)
WRITE_RETRY_COUNT = 5
WRITE_RETRY_SECONDS = 2.0
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


def actor_tag_names(actor):
    return {str(t) for t in actor.tags}


def realm_id_of_tags(tags):
    for tag in sorted(tags):
        if tag.startswith(REALM_TAG_PREFIX):
            return tag[len(REALM_TAG_PREFIX):]
    return None


def landscape_actors(world):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy))


def is_landscape_desc(desc):
    native_class = desc.native_class
    return native_class is not None and native_class.get_name() in LANDSCAPE_CLASS_NAMES


def load_landscape_actors():
    library = unreal.WorldPartitionBlueprintLibrary
    descs = library.get_actor_descs()
    if descs is None:
        log("WARNING world partition actor descs unavailable; only loaded landscape actors are handled")
        return None
    guids = [desc.guid for desc in descs if is_landscape_desc(desc)]
    if guids:
        library.load_actors(guids)
    log(f"landscape actor descs loaded: {len(guids)}")
    return len(guids)


def landscape_counts(world):
    counts = {"total": 0, "field": 0, "realms": {}, "realm_without_id": 0}
    for proxy in landscape_actors(world):
        tags = actor_tag_names(proxy)
        counts["total"] += 1
        if REALM_TAG not in tags:
            counts["field"] += 1
            continue
        realm_id = realm_id_of_tags(tags)
        if realm_id is None:
            counts["realm_without_id"] += 1
            continue
        counts["realms"][realm_id] = counts["realms"].get(realm_id, 0) + 1
    return counts


def expected_landscape_actor_count(cfg):
    grid = cfg["world_partition_grid_size"]
    return -(-cfg["components_x"] // grid) * -(-cfg["components_y"] // grid) + 1


def realm_ids_with_layout():
    if not os.path.isdir(REALM_LAYOUTS_DIR):
        return []
    return sorted(name for name in os.listdir(REALM_LAYOUTS_DIR) if os.path.isfile(os.path.join(REALM_LAYOUTS_DIR, name, REALM_LAYOUT_FILE_NAME)))


def landscape_safety_problems(counts, landscape_desc_count, required_realm_ids):
    problems = []
    if counts["realm_without_id"]:
        problems.append(f"{counts['realm_without_id']} landscape actors have the {REALM_TAG} tag but no {REALM_TAG_PREFIX}<RealmId> tag")
    for realm_id in sorted(set(counts["realms"]) | set(required_realm_ids)):
        count = counts["realms"].get(realm_id, 0)
        if count != REALM_LANDSCAPE_ACTOR_COUNT:
            problems.append(f"{realm_id}: {count} landscape actors tagged {REALM_TAG_PREFIX}{realm_id}, expected {REALM_LANDSCAPE_ACTOR_COUNT}")
    if landscape_desc_count is not None and counts["total"] < landscape_desc_count:
        problems.append(f"only {counts['total']} of {landscape_desc_count} landscape actor descs are loaded")
    return problems


def require_landscapes_intact(world, landscape_desc_count):
    counts = landscape_counts(world)
    required_realm_ids = realm_ids_with_layout()
    problems = landscape_safety_problems(counts, landscape_desc_count, required_realm_ids)
    if problems:
        raise RuntimeError(f"landscape safety check failed before any deletion (spec 4.1): {problems}; counts {counts}; realms with {REALM_LAYOUT_FILE_NAME} {required_realm_ids}")
    log(f"landscape safety check passed: {counts}; realms with {REALM_LAYOUT_FILE_NAME} {required_realm_ids}")
    return counts


def destroy_actors(actors):
    unique = {actor.get_path_name(): actor for actor in actors}
    ordered = sorted(unique.values(), key=lambda actor: isinstance(actor, unreal.Landscape))
    for actor in ordered:
        eas.destroy_actor(actor)
    return len(ordered)


def destroy_field_landscape(world):
    library = unreal.TDLandscapeEditorLibrary
    if hasattr(library, "destroy_landscape_actors_without_tag"):
        return library.destroy_landscape_actors_without_tag(world, REALM_TAG)
    log("WARNING TDLandscapeEditorLibrary.destroy_landscape_actors_without_tag missing (C++ not rebuilt); Python fallback by tag")
    return destroy_actors([proxy for proxy in landscape_actors(world) if REALM_TAG not in actor_tag_names(proxy)])


def landscape_proxies_of(world, landscape, fallback_filter):
    library = unreal.TDLandscapeEditorLibrary
    if hasattr(library, "get_landscape_proxies_of"):
        return list(library.get_landscape_proxies_of(landscape))
    log("WARNING TDLandscapeEditorLibrary.get_landscape_proxies_of missing (C++ not rebuilt); tag and box fallback")
    return [proxy for proxy in landscape_actors(world) if fallback_filter(proxy)]


def is_untagged_field_landscape(proxy):
    if REALM_TAG in actor_tag_names(proxy):
        return False
    x, y = landscape_center_xy(proxy)
    return is_xy_in_box(x, y, FIELD_BOX_MIN, FIELD_BOX_MAX)


def finish_field_landscape_proxies(proxies):
    for proxy in proxies:
        setp(proxy, "enable_nanite", True)
        proxy.set_folder_path(FIELD_LANDSCAPE_FOLDER)
        proxy.tags = [unreal.Name(t) for t in FIELD_LANDSCAPE_TAGS]


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
    counts = {"landscape": destroy_field_landscape(world), "removed": 0, "kept_class": 0, "kept_realm": 0}
    log(f"landscape actors removed: {counts['landscape']}")
    for a in list(eas.get_all_level_actors()):
        cn = a.get_class().get_name()
        if cn in KEEP_CLASSES or cn in LANDSCAPE_CLASS_NAMES:
            counts["kept_class"] += 1
            continue
        if a.actor_has_tag(REALM_TAG) or data_layers.actor_layer_names(a) & REALM_LAYER_NAMES:
            counts["kept_realm"] += 1
            continue
        eas.destroy_actor(a)
        counts["removed"] += 1
    log(f"full clear: {counts}")
    return counts


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


def create_region_landscape(world, cfg, region_id, fallback_filter, finish_proxies):
    result = {"region": region_id, "mode": "tag_only", "check": None, "expected_actors": expected_landscape_actor_count(cfg)}
    if data_layers.is_landscape_layer_enabled():
        with data_layers.region_context(region_id):
            landscape = create_landscape_actor(world, cfg)
        proxies = landscape_proxies_of(world, landscape, fallback_filter)
        result["check"] = data_layers.check_landscape_layer(proxies, region_id)
        if result["check"]["ok"]:
            finish_proxies(proxies)
            result.update({"mode": "data_layer", "actors": len(proxies)})
            log(f"landscape {region_id}: data layer check passed ({len(proxies)} actors)")
            return landscape, proxies, result
        log(f"WARNING landscape {region_id}: data layer check failed {result['check']}; recreating it without data layer, tag only (spec 2.4)")
        destroy_actors(proxies + [proxy for proxy in landscape_actors(world) if fallback_filter(proxy)])
        result["mode"] = "tag_only_fallback"
    landscape = create_landscape_actor(world, cfg)
    proxies = landscape_proxies_of(world, landscape, fallback_filter)
    finish_proxies(proxies)
    result["actors"] = len(proxies)
    return landscape, proxies, result


def build_grass_maps():
    try:
        unreal.get_editor_subsystem(unreal.LandscapeSubsystem).build_grass_maps()
        log("grass maps build requested")
    except Exception as e:
        log(f"grass maps build not available: {str(e)[:80]}")


def create_landscape(world, cfg):
    landscape, proxies, result = create_region_landscape(world, cfg, data_layers.FIELD_REGION_ID, is_untagged_field_landscape, finish_field_landscape_proxies)
    build_grass_maps()
    log(f"landscape created: {landscape.get_actor_label()} actors={len(proxies)} expected={result['expected_actors']} mode={result['mode']}")
    return result


def setup_atmosphere(world, region):
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 5000), unreal.Rotator(roll=0, pitch=-70, yaw=205))
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


def spawn_markers(layout, label_prefix="", folder_root="TDGen", extra_tags=(), skip_kinds=(), only_kinds=None):
    folder = f"{folder_root}/Markers"
    spawned = 0
    for e in layout["markers"]:
        kind = e["kind"]
        if only_kinds is not None and kind not in only_kinds:
            continue
        if kind in skip_kinds:
            log(f"marker skipped here: {kind}")
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
        spawned += 1
    log(f"markers {spawned} of {len(layout['markers'])}")
    return spawned


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
    if label.startswith(REALM_LABEL_PREFIX):
        return False
    layer_names = data_layers.desc_layer_names(desc)
    if layer_names & REALM_LAYER_NAMES:
        return False
    if FIELD_LAYER_NAME in layer_names:
        return True
    if label in delete_labels or label.startswith(GENERATED_ISM_PREFIX) or label.startswith(SEAM_RESIDUE_PREFIXES):
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


def field_clear_decision(label, class_name, tags, layer_names, x, y, delete_labels):
    if class_name in LANDSCAPE_CLASS_NAMES:
        return False, "kept_landscape"
    if is_partial_protected(label, class_name, tags):
        return False, "protected"
    if layer_names & REALM_LAYER_NAMES:
        return False, "realm_layer"
    if FIELD_LAYER_NAME in layer_names:
        return True, "deleted_field_layer"
    if layer_names:
        return False, "other_layer"
    if label.startswith(SEAM_RESIDUE_PREFIXES):
        return True, "deleted_seam_residue"
    if not is_partial_delete_target(label, tags, delete_labels):
        return False, "untouched"
    is_inside_field = is_xy_in_box(x, y, FIELD_BOX_MIN, FIELD_BOX_MAX)
    if label in delete_labels and FIELD_BUILDER_TAG in tags:
        return True, "deleted_layout_labels" if is_inside_field else "deleted_layout_labels_outside_field"
    if not is_inside_field:
        return False, "outside_field"
    return True, "deleted_layout_labels" if label in delete_labels else "deleted_ism"


def clear_field_partial(world, delete_labels):
    counts = {"landscape": destroy_field_landscape(world)}
    for actor in list(eas.get_all_level_actors()):
        location = actor.get_actor_location()
        delete, reason = field_clear_decision(actor.get_actor_label(), actor.get_class().get_name(), actor_tag_names(actor),
                                              data_layers.actor_layer_names(actor), location.x, location.y, delete_labels)
        counts[reason] = counts.get(reason, 0) + 1
        if delete:
            eas.destroy_actor(actor)
    log(f"partial clear: {counts}")
    return counts


def existing_kept_marker_kinds():
    kinds = set()
    for actor in eas.get_all_level_actors():
        if actor.get_class().get_name() == "PlayerStart":
            kinds.add("PlayerStart")
    return kinds


def spawn_field_content(layout, skip_marker_kinds=()):
    counts = {}
    with data_layers.region_context(data_layers.FIELD_REGION_ID):
        counts["ism_cells"], counts["ism_instances"] = spawn_ism_cells(layout)
        counts["actors"] = spawn_actors(layout)
        counts["planes"] = spawn_planes(layout)
        counts["lights"] = spawn_lights(layout)
        counts["niagara"] = spawn_niagara(layout)
        counts["markers"] = spawn_markers(layout, skip_kinds=tuple(skip_marker_kinds) + OUTSIDE_LAYER_MARKER_KINDS)
    counts["markers_outside_layer"] = spawn_markers(layout, skip_kinds=tuple(skip_marker_kinds), only_kinds=OUTSIDE_LAYER_MARKER_KINDS)
    return counts


def build_full(world, layout):
    result = {"loaded_field_actor_descs": load_field_actors(set())}
    result["clear"] = clear_level(world)
    result["landscape"] = create_landscape(world, layout["landscape"])
    setup_atmosphere(world, layout["region"])
    result["spawned"] = spawn_field_content(layout)
    return result


def build_partial(world, layout, request):
    previous_path = request.get("previous_layout", "")
    if not os.path.isfile(previous_path):
        raise RuntimeError(f"partial build needs previous_layout, not found: {previous_path}")
    with open(previous_path, encoding="utf-8") as fp:
        previous = json.load(fp)
    delete_labels = layout_labels(previous) | layout_labels(layout)
    log(f"partial: delete label set {len(delete_labels)} (previous {previous_path})")
    result = {"previous_layout": previous_path, "delete_labels": len(delete_labels)}
    result["loaded_field_actor_descs"] = load_field_actors(delete_labels)
    result["clear"] = clear_field_partial(world, delete_labels)
    result["landscape"] = create_landscape(world, layout["landscape"])
    kept = existing_kept_marker_kinds()
    result["kept_marker_kinds"] = sorted(kept)
    result["spawned"] = spawn_field_content(layout, kept)
    return result


def require_landscapes_after(before, after, cfg):
    problems = []
    if after["realms"] != before["realms"] or after["realm_without_id"] != before["realm_without_id"]:
        problems.append(f"realm landscape counts changed {before['realms']} -> {after['realms']}")
    expected = expected_landscape_actor_count(cfg)
    if after["field"] != expected:
        problems.append(f"field landscape actors {after['field']}, expected {expected}")
    if problems:
        raise RuntimeError(f"landscape check after build failed, level NOT saved: {problems}")


def write_json_atomic(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = json.dumps(data, ensure_ascii=False, indent=2)
    temporary = path + ".tmp"
    for attempt in range(WRITE_RETRY_COUNT):
        try:
            with open(temporary, "w", encoding="utf-8", newline="\n") as fp:
                fp.write(text)
            os.replace(temporary, path)
            return
        except OSError as e:
            if attempt == WRITE_RETRY_COUNT - 1:
                raise
            log(f"write retry {path} after {e}")
            time.sleep(WRITE_RETRY_SECONDS)


def main():
    layout = json.load(open(LAYOUT_PATH, encoding="utf-8"))
    request = read_build_request()
    mode = request.get("mode", "full")
    if mode not in ("partial", "full"):
        raise RuntimeError(f"unknown build mode {mode} in {BUILD_REQUEST_PATH}")
    les.load_level(LEVEL_PATH)
    world = ues.get_editor_world()
    log(f"level loaded: {world.get_name()}; build mode {mode}")
    report = {"mode": mode, "layout": LAYOUT_PATH, "data_layers": data_layers.prepare_for_build()}
    report["landscape_descs"] = load_landscape_actors()
    report["landscape_before"] = require_landscapes_intact(world, report["landscape_descs"])
    if mode == "partial":
        report.update(build_partial(world, layout, request))
    else:
        report.update(build_full(world, layout))
    report["landscape_after"] = landscape_counts(world)
    require_landscapes_after(report["landscape_before"], report["landscape_after"], layout["landscape"])
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    report["saved"] = bool(ok)
    write_json_atomic(BUILD_REPORT_PATH, report)
    log(f"saved: {ok}; actors now {len(eas.get_all_level_actors())}; report {BUILD_REPORT_PATH}")


if __name__ == "__main__":
    main()

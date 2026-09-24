"""실행 환경: TDGame 언리얼 에디터 안에서 MegaMagic 주문 시험장을 신규 생성한다.
실행 명령: python Tools/run_in_editor.py Tools/Damage/editor_make_megamagic_arena.py
출력: /Game/Combat/Maps/LV_TDMegaMagicArena, 무광 재질 2종 및 생성 결과 JSON 로그
상태: 현행, 기존 맵과 미저장 작업이 있으면 중단하고 보존한다.
"""
import json

import unreal


ARENA_PATH = "/Game/Combat/Maps/LV_TDMegaMagicArena"
GAME_MODE_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode"
MATERIAL_DIRECTORY = "/Game/Combat/Maps"
FLOOR_MATERIAL_PATH = MATERIAL_DIRECTORY + "/M_TDMegaMagicFloor"
WALL_MATERIAL_PATH = MATERIAL_DIRECTORY + "/M_TDMegaMagicWall"


def create_matte_material(asset_path, color):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(asset_path):
        material = assets.load_asset(asset_path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋이 Material이 아닙니다: {asset_path}")
        return material

    asset_directory, asset_name = asset_path.rsplit("/", 1)
    if not assets.does_directory_exist(asset_directory) and not assets.make_directory(asset_directory):
        raise RuntimeError(f"재질 폴더 생성 실패: {asset_directory}")
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, asset_directory, unreal.Material, unreal.MaterialFactoryNew()
    )
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {asset_path}")

    editing = unreal.MaterialEditingLibrary
    base_color = editing.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -240, -120)
    roughness = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -240, 0)
    metallic = editing.create_material_expression(material, unreal.MaterialExpressionConstant, -240, 120)
    if base_color is None or roughness is None or metallic is None:
        raise RuntimeError(f"재질 노드 생성 실패: {asset_path}")
    base_color.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    roughness.set_editor_property("r", 0.9)
    metallic.set_editor_property("r", 0.0)
    for expression, material_property in (
        (base_color, unreal.MaterialProperty.MP_BASE_COLOR),
        (roughness, unreal.MaterialProperty.MP_ROUGHNESS),
        (metallic, unreal.MaterialProperty.MP_METALLIC),
    ):
        if not editing.connect_material_property(expression, "", material_property):
            raise RuntimeError(f"재질 연결 실패: {asset_path}, {material_property}")
    errors = editing.recompile_material(material)
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {asset_path}, {list(errors)}")
    if not assets.save_loaded_asset(material):
        raise RuntimeError(f"재질 저장 실패: {asset_path}")
    return material


def create_arena_materials():
    floor_material = create_matte_material(FLOOR_MATERIAL_PATH, (0.07, 0.08, 0.09))
    wall_material = create_matte_material(WALL_MATERIAL_PATH, (0.10, 0.11, 0.12))
    return floor_material, wall_material


def main():
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if assets.does_asset_exist(ARENA_PATH):
        raise RuntimeError(f"기존 맵을 보존합니다: {ARENA_PATH}")
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    if dirty:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(package.get_name() for package in dirty))

    game_mode = assets.load_blueprint_class(GAME_MODE_PATH)
    cube = assets.load_asset("/Engine/BasicShapes/Cube")
    cylinder = assets.load_asset("/Engine/BasicShapes/Cylinder")
    target_class = unreal.load_class(None, "/Script/TDGame.TDDamageTarget")
    required = {
        "게임 모드": game_mode,
        "큐브": cube,
        "기둥": cylinder,
        "데미지 대상 클래스": target_class,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        raise RuntimeError("필수 에셋 또는 클래스 없음: " + ", ".join(missing))

    floor_material, wall_material = create_arena_materials()
    if not levels.new_level(ARENA_PATH):
        raise RuntimeError(f"맵 생성 실패: {ARENA_PATH}")
    world = editor.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    created = []

    def finish(actor, label, folder):
        if actor is None:
            raise RuntimeError(f"액터 생성 실패: {label}")
        actor.set_actor_label("TDGen_" + label)
        actor.set_folder_path("TDGen/MegaMagic/" + folder)
        created.append(actor)
        return actor

    def spawn(actor_class, location, label, folder, rotation=None):
        actor = actors.spawn_actor_from_class(actor_class, unreal.Vector(*location), rotation or unreal.Rotator())
        return finish(actor, label, folder)

    def place_mesh(mesh, location, scale, material, label):
        actor = actors.spawn_actor_from_object(mesh, unreal.Vector(*location), unreal.Rotator())
        finish(actor, label, "Arena")
        actor.set_actor_scale3d(unreal.Vector(*scale))
        component = actor.static_mesh_component
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        component.set_material(0, material)
        return actor

    place_mesh(cube, (150, 0, -25), (30, 24, 0.5), floor_material, "ArenaFloor")
    for label, location, scale in (
        ("NorthRail", (150, -1170, 40), (30, 0.6, 0.8)),
        ("SouthRail", (150, 1170, 40), (30, 0.6, 0.8)),
        ("WestRail", (-1320, 0, 40), (0.6, 24, 0.8)),
        ("EastRail", (1620, 0, 40), (0.6, 24, 0.8)),
    ):
        place_mesh(cube, location, scale, wall_material, label)

    for index, (x, y) in enumerate(((-1220, -1070), (-1220, 1070), (1520, -1070), (1520, 1070))):
        place_mesh(cube, (x, y, 30), (1.6, 1.6, 0.6), wall_material, f"PillarBase_{index}")
        place_mesh(cylinder, (x, y, 165), (0.9, 0.9, 2.1), wall_material, f"Pillar_{index}")
        place_mesh(cube, (x, y, 285), (1.4, 1.4, 0.3), floor_material, f"PillarCrown_{index}")
        lamp = spawn(unreal.PointLight, (x, y, 330), f"RuneLight_{index}", "Lighting")
        lamp.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        lamp.light_component.set_intensity(150.0)
        lamp.light_component.set_attenuation_radius(550.0)
        lamp.light_component.set_cast_shadows(False)
        color = unreal.LinearColor(0.15, 0.6, 1.0, 1.0) if x < 0 else unreal.LinearColor(0.8, 0.2, 1.0, 1.0)
        lamp.light_component.set_light_color(color)

    sun = spawn(unreal.DirectionalLight, (0, 0, 1500), "ArenaSun", "Lighting", unreal.Rotator(pitch=-55, yaw=-35))
    sun.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(5.0)
    sun.light_component.set_light_color(unreal.LinearColor(0.8, 0.86, 1.0, 1.0))
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0), "ArenaAtmosphere", "Lighting")
    sky = spawn(unreal.SkyLight, (0, 0, 1000), "ArenaSkyLight", "Lighting")
    sky.light_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture", True)
    sky.light_component.set_intensity(0.8)

    post_process = spawn(unreal.PostProcessVolume, (0, 0, 0), "ArenaExposure", "Lighting")
    post_process.set_editor_property("unbound", True)
    settings = post_process.settings
    for name, value in (
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL),
        ("auto_exposure_apply_physical_camera_exposure", False),
        ("auto_exposure_bias", 0.0),
        ("bloom_intensity", 0.35),
        ("vignette_intensity", 0.2),
    ):
        settings.set_editor_property("override_" + name, True)
        settings.set_editor_property(name, value)
    post_process.set_editor_property("settings", settings)

    spawn(unreal.PlayerStart, (-500, 0, 96), "ArenaPlayerStart", "Gameplay")
    target_locations = [(x, y, 65) for x in (150, 400, 650) for y in (-180, 180)]
    for index, location in enumerate(target_locations):
        spawn(target_class, location, f"DamageTarget_{index + 1}", "Gameplay")

    navigation = spawn(unreal.NavMeshBoundsVolume, (150, 0, 200), "ArenaNavigation", "Gameplay")
    navigation.set_actor_scale3d(unreal.Vector(15, 12, 4))
    nav_origin, nav_extent = navigation.get_actor_bounds(False)
    if nav_extent.x < 1400 or nav_extent.y < 1100:
        raise RuntimeError(f"내비게이션 브러시 범위 검증 실패: {nav_extent}")
    for nav_mesh in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
        nav_mesh.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        nav_mesh.set_editor_property("is_world_partitioned", False)

    if not levels.save_current_level():
        raise RuntimeError(f"맵 저장 실패: {ARENA_PATH}")
    print(json.dumps({
        "map": ARENA_PATH,
        "game_mode": game_mode.get_path_name(),
        "created_actors": len(created),
        "targets": target_locations,
        "player_start": [-500, 0, 96],
        "navigation_extent": [nav_extent.x, nav_extent.y, nav_extent.z],
        "saved": True,
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()

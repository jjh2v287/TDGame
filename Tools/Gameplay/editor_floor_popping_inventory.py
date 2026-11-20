"""실행 환경: 에디터 안(Tools/run_in_editor.py). 에디터가 LV_DarkFantasy_OpenWorld 를 열고 있어야 랜드스케이프·액터 항목이 채워진다. 에셋은 읽기만 하고 저장하지 않는다.
실행 명령: python Tools/run_in_editor.py Tools/Gameplay/editor_floor_popping_inventory.py
출력: Saved/Gameplay/FloorPopping/inventory_<YYYYMMDD_HHMMSS>.json(지면 텍스처 크기·그룹·스트리밍, 지면 판 재질, 산포 메시 LOD·화면 크기·디더, 랜드스케이프 LOD 설정, 관련 콘솔 변수) + 표준 출력 요약
상태: GF-07 바닥 팝핑 진단용(2026-11-20).
"""
import datetime
import json
import os

import unreal

LANDSCAPE_MATERIAL_PATH = "/Game/World/Landscape/M_TD_Landscape.M_TD_Landscape"
PLANE_MATERIAL_PATHS = (
    "/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_PuddlesDecal",
    "/Game/DarkFantasyTopDown/Materials/VFX/MI_FogPlane_Var1",
    "/Game/DarkFantasyTopDown/Materials/VFX/MI_FogPlane_Var2",
    "/Game/DarkFantasyTopDown/Materials/WaterPlane/MI_WaterPlane",
)
SCATTER_MESH_PATHS = (
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_3",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Rocks/SmallRocks/SM_Rock_5",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_Dreadplant1",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Dreadplants/SM_DreadplantMushroom1",
    "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_Birch1",
    "/Game/DarkFantasyTopDown/StaticMeshes/Bush/SM_Bush",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/Cliff/SM_Cliff2",
    "/Game/DarkFantasyTopDown/StaticMeshes/Rocks2/SM_Rock1",
    "/Game/DarkFantasyTopDown/StaticMeshes/VFX/SM_Plane",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass",
    "/Game/DarkFantasyTopDown/StaticMeshes/Nature/SM_Grass_VarB",
    "/Game/DarkFantasyTopDown/StaticMeshes/Grass/SM_GrassPlane",
    "/Game/DarkFantasyTopDown/StaticMeshes/WoodenParts/SM_WoodenFloor",
)
TEXTURE_PROPERTIES = ("lod_group", "never_stream", "compression_settings", "virtual_texture_streaming", "lod_bias", "max_texture_size", "srgb", "mip_gen_settings")
LANDSCAPE_PROPERTIES = ("lod0_screen_size", "lod0_distribution_setting", "lod_distribution_setting", "lod_blend_range", "max_lod_level", "component_screen_size_to_use_sub_sections", "nanite_enabled", "enable_nanite", "use_dynamic_material_instance", "landscape_material")
CONSOLE_VARIABLES = (
    "r.Streaming.PoolSize", "r.Streaming.LimitPoolSizeToVRAM", "r.Streaming.UseFixedPoolSize", "r.Streaming.HiddenPrimitiveScale",
    "r.Streaming.Boost", "r.Streaming.MipBias", "r.Streaming.FramesForFullUpdate", "r.Streaming.UseTextureStreamingBuiltData",
    "r.Streaming.FullyLoadUsedTextures", "r.TextureStreaming", "r.VirtualTextures", "grass.Enable", "grass.CullDistanceScale",
    "grass.DensityScale", "grass.GrassMap.UseRuntimeGeneration", "grass.GuardBandMultiplier", "grass.MaxAsyncTasks", "r.ForceLOD",
    "foliage.ForceLOD", "foliage.DitheredLOD", "foliage.LODDistanceScale", "r.StaticMeshLODDistanceScale", "r.LandscapeLODBias",
    "r.LandscapeLODDistributionScale", "r.LandscapeLOD0DistributionScale", "r.Shadow.Virtual.Enable", "r.AntiAliasingMethod",
    "r.ScreenPercentage", "r.Decal.FadeScreenSizeMult", "r.PSOPrecaching", "r.PSOPrecache.ProxyCreationStrategy",
    "r.PSOPrecache.LandscapeMeshProxyCreationStrategy", "wp.Runtime.BlockOnSlowStreaming", "s.AsyncLoadingThreadEnabled",
)


def read_property(target, name):
    try:
        value = target.get_editor_property(name)
    except Exception:
        return None
    if isinstance(value, (bool, int, float, str)) or value is None:
        return value
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    return str(value)


def describe_texture(texture):
    record = {"path": texture.get_path_name(), "size": [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]}
    for name in TEXTURE_PROPERTIES:
        record[name] = read_property(texture, name)
    return record


def describe_material_textures(material):
    try:
        textures = unreal.MaterialEditingLibrary.get_used_textures(material)
    except Exception as error:
        return {"error": repr(error)}
    return [describe_texture(texture) for texture in textures if isinstance(texture, unreal.Texture2D)]


def find_base_material(material):
    current = material
    while isinstance(current, unreal.MaterialInstance):
        parent = current.get_editor_property("parent")
        if parent is None:
            return current
        current = parent
    return current


def describe_material(path):
    material = unreal.load_asset(path)
    if material is None:
        return {"path": path, "missing": True}
    base = find_base_material(material)
    return {
        "path": path,
        "base": base.get_path_name() if base else None,
        "blend_mode": read_property(base, "blend_mode"),
        "material_domain": read_property(base, "material_domain"),
        "dithered_lod_transition": read_property(base, "dithered_lod_transition"),
        "textures": describe_material_textures(material),
    }


def read_lod_screen_sizes(mesh):
    for reader in (lambda: unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_lod_screen_sizes(mesh), lambda: unreal.EditorStaticMeshLibrary.get_lod_screen_sizes(mesh)):
        try:
            return [round(float(value), 4) for value in reader()]
        except Exception:
            continue
    return None


def describe_mesh(path):
    mesh = unreal.load_asset(path)
    if mesh is None:
        return {"path": path, "missing": True}
    materials = []
    for slot in mesh.get_editor_property("static_materials"):
        interface = slot.get_editor_property("material_interface")
        base = find_base_material(interface) if interface else None
        materials.append({"material": interface.get_path_name() if interface else None, "dithered_lod_transition": read_property(base, "dithered_lod_transition") if base else None})
    return {
        "path": path,
        "lods": mesh.get_num_lods(),
        "screen_sizes": read_lod_screen_sizes(mesh),
        "nanite": read_property(mesh.get_editor_property("nanite_settings"), "enabled") if read_property(mesh, "nanite_settings") is not None else None,
        "materials": materials,
    }


def describe_landscapes(world):
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy)
    summary = {"count": len(actors), "classes": {}, "first": None}
    for actor in actors:
        class_name = actor.get_class().get_name()
        summary["classes"][class_name] = summary["classes"].get(class_name, 0) + 1
    if not actors:
        return summary
    first = actors[0]
    summary["first"] = {"label": first.get_actor_label(), "properties": {name: read_property(first, name) for name in LANDSCAPE_PROPERTIES}}
    components = first.get_components_by_class(unreal.LandscapeComponent)
    summary["first"]["components"] = len(components)
    if components:
        component_materials = components[0].get_materials()
        textures = []
        for material in component_materials:
            if material is None:
                continue
            for parameter in ("Weightmap0", "Weightmap1", "Weightmap2", "Heightmap"):
                try:
                    texture = material.get_texture_parameter_value(parameter)
                except Exception:
                    texture = None
                if texture is not None:
                    textures.append({"parameter": parameter, **describe_texture(texture)})
        summary["first"]["component_textures"] = textures
    return summary


def read_console_variables():
    values = {}
    for name in CONSOLE_VARIABLES:
        try:
            values[name] = unreal.SystemLibrary.get_console_variable_float_value(name)
        except Exception as error:
            values[name] = repr(error)
    return values


def summarize_texture_list(records):
    rows = []
    for record in records if isinstance(records, list) else []:
        rows.append(f"{record['path'].split('/')[-1].split('.')[0]} {record['size'][0]}x{record['size'][1]} group={record['lod_group']} vt={record['virtual_texture_streaming']} never_stream={record['never_stream']} bias={record['lod_bias']}")
    return rows


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    landscape_material = unreal.load_asset(LANDSCAPE_MATERIAL_PATH)
    report = {
        "world": world.get_path_name() if world else None,
        "landscape_material": {"path": LANDSCAPE_MATERIAL_PATH, "textures": describe_material_textures(landscape_material) if landscape_material else "missing"},
        "plane_materials": [describe_material(path) for path in PLANE_MATERIAL_PATHS],
        "scatter_meshes": [describe_mesh(path) for path in SCATTER_MESH_PATHS],
        "landscapes": describe_landscapes(world) if world else None,
        "console_variables": read_console_variables(),
    }
    out_dir = os.path.join(unreal.SystemLibrary.get_project_directory(), "Saved", "Gameplay", "FloorPopping")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "inventory_" + datetime.datetime.now().strftime("%Y%m%d_%H%M%S") + ".json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    print("inventory:", path)
    for row in summarize_texture_list(report["landscape_material"]["textures"]):
        print("landscape texture:", row)
    for mesh in report["scatter_meshes"]:
        print("mesh:", mesh["path"].split("/")[-1], "lods", mesh.get("lods"), "sizes", mesh.get("screen_sizes"), "dither", [m["dithered_lod_transition"] for m in mesh.get("materials", [])])
    for material in report["plane_materials"]:
        print("plane material:", material["path"].split("/")[-1], material.get("blend_mode"), material.get("material_domain"))
    print("landscapes:", json.dumps(report["landscapes"], ensure_ascii=False)[:900])
    print("cvars:", json.dumps(report["console_variables"]))


main()

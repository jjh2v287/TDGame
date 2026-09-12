"""에디터 안에서 실행: 랜드스케이프 레이어 머티리얼(M_TD_Landscape), 인스턴스, 풀(Grass) 타입 에셋을 만든다.

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_make_landscape_material.py
결과 에셋: /Game/World/Landscape/ 아래 (재실행하면 덮어쓴다). 레이어 인포(LI_TD_*)는 C++ UTDLandscapeEditorLibrary가 만든다.
"""
import unreal

ROOT = "/Game/World/Landscape"
TEX = "/Game/DarkFantasyTopDown/Textures/Nature"
MESH = "/Game/DarkFantasyTopDown/StaticMeshes"

LAYERS = [
    # 이름, 텍스처 폴더, 텍스처 접두어, 타일 크기(m), 기본색 틴트(RGB)
    ("Soil", "RockySoil2", "T_RockySoil2", 3.2, (0.92, 0.90, 0.88)),
    ("Moss", "Mold", "T_Mold", 2.8, (0.62, 0.80, 0.50)),
    ("Mud", "MuddySoil", "T_MuddySoil", 3.2, (0.62, 0.55, 0.48)),
    ("Road", "MuddyRoad", "T_MuddySoilRoad", 2.6, (1.05, 0.90, 0.72)),
    ("Rock", "RockySoil1", "T_RockySoil1", 3.6, (0.85, 0.86, 0.90)),
    ("Bedrock", "Bedrock2", "T_Bedrock2", 5.0, (0.70, 0.72, 0.78)),
]

GRASS_TYPES = {
    # 그래스 타입 이름: (입력 레이어, [(메시, 밀도(10m x 10m당 개수), 최소스케일, 최대스케일, 컬거리)])
    "GT_TD_AshGrass": ("Soil", [
        (f"{MESH}/Nature/SM_Grass", 260.0, 0.7, 1.25, 4500),
        (f"{MESH}/Nature/SM_Grass_VarB", 160.0, 0.7, 1.2, 4500),
        (f"{MESH}/Grass/SM_GrassPlane", 120.0, 1.2, 2.4, 3000),
    ]),
    "GT_TD_ForestFloor": ("Moss", [
        (f"{MESH}/Nature/Dreadplants/SM_Mycelium1", 45.0, 1.0, 2.6, 4000),
        (f"{MESH}/Nature/Dreadplants/SM_DreadplantMushroom1", 14.0, 0.6, 1.3, 4000),
        (f"{MESH}/Nature/Dreadplants/SM_DreadplantMushroom2", 8.0, 0.5, 1.1, 4000),
        (f"{MESH}/Birch/SM_BirchLavesPlane", 220.0, 2.5, 5.0, 3500),
        (f"{MESH}/Nature/SM_Grass_VarB", 70.0, 0.6, 1.0, 4000),
    ]),
    "GT_TD_MudReeds": ("Mud", [
        (f"{MESH}/Nature/SM_Grass", 27.0, 0.9, 1.6, 4500),
        (f"{MESH}/Nature/Dreadplants/SM_Dreadplant2", 1.8, 0.25, 0.5, 4500),
    ]),
    "GT_TD_Pebbles": ("Rock", [
        (f"{MESH}/Nature/Rocks/SmallRocks/SM_Rock_3", 30.0, 0.6, 1.1, 4000),
        (f"{MESH}/Nature/Rocks/SmallRocks/SM_Rock_4", 30.0, 0.6, 1.1, 4000),
        (f"{MESH}/Nature/Rocks/SmallRocks/SM_Rock_6", 18.0, 0.5, 1.0, 4000),
        (f"{MESH}/Nature/Rocks/SmallRocks/SM_Rock_8", 12.0, 0.5, 1.0, 4000),
    ]),
}

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def load_texture(folder, prefix, suffix):
    path = f"{TEX}/{folder}/{prefix}_{suffix}"
    tex = EAL.load_asset(path)
    if tex is None:
        raise RuntimeError("texture missing: " + path)
    return tex


def make_grass_types():
    ensure_dir(ROOT + "/Grass")
    result = {}
    for name, (layer, varieties) in GRASS_TYPES.items():
        path = f"{ROOT}/Grass/{name}"
        gt = EAL.load_asset(path)
        if gt is None:
            gt = asset_tools.create_asset(name, ROOT + "/Grass", unreal.LandscapeGrassType, unreal.LandscapeGrassTypeFactory())
        arr = []
        for mesh_path, density, smin, smax, cull in varieties:
            mesh = EAL.load_asset(mesh_path)
            if mesh is None:
                raise RuntimeError("mesh missing: " + mesh_path)
            v = unreal.GrassVariety()
            v.set_editor_property("grass_mesh", mesh)
            v.set_editor_property("grass_density", unreal.PerPlatformFloat(density))
            v.set_editor_property("use_grid", True)
            v.set_editor_property("placement_jitter", 1.0)
            v.set_editor_property("start_cull_distance", unreal.PerPlatformInt(int(cull * 0.75)))
            v.set_editor_property("end_cull_distance", unreal.PerPlatformInt(int(cull)))
            v.set_editor_property("min_lod", -1)
            v.set_editor_property("scaling", unreal.GrassScaling.UNIFORM)
            v.set_editor_property("scale_x", unreal.FloatInterval(smin, smax))
            v.set_editor_property("random_rotation", True)
            v.set_editor_property("align_to_surface", True)
            v.set_editor_property("use_landscape_lightmap", False)
            v.set_editor_property("receives_decals", True)
            v.set_editor_property("cast_dynamic_shadow", cull > 4000 and density < 100)
            arr.append(v)
        gt.set_editor_property("grass_varieties", arr)
        gt.set_editor_property("enable_density_scaling", True)
        EAL.save_asset(path)
        result[name] = (layer, gt)
    return result


def make_material(grass_types):
    ensure_dir(ROOT)
    mat_path = f"{ROOT}/M_TD_Landscape"
    # 제자리 재구성은 LandscapeGrassOutput 같은 커스텀 출력 노드가 남아 컴파일이 깨지므로 삭제 후 재생성한다
    if EAL.does_asset_exist(mat_path):
        EAL.delete_asset(mat_path)
    mat = asset_tools.create_asset("M_TD_Landscape", ROOT, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("two_sided", False)

    blend_color = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeLayerBlend, 600, -600)
    blend_normal = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeLayerBlend, 600, 200)
    blend_rough = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeLayerBlend, 600, 900)

    def layer_inputs():
        arr = []
        for name, _, _, _, _ in LAYERS:
            li = unreal.LayerBlendInput()
            li.set_editor_property("layer_name", name)
            li.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
            li.set_editor_property("preview_weight", 1.0 if name == "Soil" else 0.0)
            arr.append(li)
        return arr

    # 반복 패턴 제거: 엔진 내장 Texture_Bombing(무작위 오프셋·회전 셀 블렌드)으로 기본색·노멀을 샘플링한다.
    # 길·기반암은 높이 텍스처로 높이 블렌드(경계가 텍스처 굴곡을 따라 갈라짐), 나머지는 가중치 블렌드.
    HEIGHT_BLEND = {"Road", "Bedrock"}
    bombing_fn = EAL.load_asset("/Engine/Functions/Engine_MaterialFunctions01/Texturing/Texture_Bombing")
    for b in (blend_color, blend_normal, blend_rough):
        arr = []
        for name, _, _, _, _ in LAYERS:
            li = unreal.LayerBlendInput()
            li.set_editor_property("layer_name", name)
            li.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_HEIGHT_BLEND if name in HEIGHT_BLEND else unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
            li.set_editor_property("preview_weight", 1.0 if name == "Soil" else 0.0)
            arr.append(li)
        b.set_editor_property("layers", arr)

    def bombed_sample(tex, coords, is_normal, x, y_pos):
        tex_obj = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObject, x, y_pos)
        tex_obj.set_editor_property("texture", tex)
        call = MEL.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, x + 250, y_pos)
        call.set_editor_property("material_function", bombing_fn)
        MEL.connect_material_expressions(tex_obj, "", call, "Texture Object")
        MEL.connect_material_expressions(coords, "", call, "UVs")
        tiling = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y_pos + 70)
        tiling.set_editor_property("r", 1.0)
        MEL.connect_material_expressions(tiling, "", call, "Tiling")
        offset = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y_pos + 110)
        offset.set_editor_property("r", 1.0)
        MEL.connect_material_expressions(offset, "", call, "Offset")
        if is_normal:
            flag = MEL.create_material_expression(mat, unreal.MaterialExpressionStaticBool, x, y_pos + 150)
            flag.set_editor_property("value", True)
            MEL.connect_material_expressions(flag, "", call, "Is Normalmap")
        return call

    y = -1200
    for name, folder, prefix, tile, tint_rgb in LAYERS:
        coords = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1500, y)
        coords.set_editor_property("mapping_scale", tile)
        base_tex = load_texture(folder, prefix, "basecolor")
        base_call = bombed_sample(base_tex, coords, False, -1200, y)
        tint_node = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -650, y + 60)
        tint_node.set_editor_property("constant", unreal.LinearColor(tint_rgb[0], tint_rgb[1], tint_rgb[2], 1.0))
        tint_mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -450, y)
        MEL.connect_material_expressions(base_call, "Result", tint_mul, "A")
        MEL.connect_material_expressions(tint_node, "", tint_mul, "B")
        MEL.connect_material_expressions(tint_mul, "", blend_color, "Layer " + name)
        y += 220
        normal_tex = load_texture(folder, prefix, "normal")
        normal_call = bombed_sample(normal_tex, coords, True, -1200, y)
        MEL.connect_material_expressions(normal_call, "Result", blend_normal, "Layer " + name)
        y += 220
        rough_tex = load_texture(folder, prefix, "roughness")
        rs = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -1000, y)
        rs.set_editor_property("texture", rough_tex)
        rs.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if rough_tex.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        rs.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        MEL.connect_material_expressions(coords, "", rs, "UVs")
        MEL.connect_material_expressions(rs, "R", blend_rough, "Layer " + name)
        y += 220
        if name in HEIGHT_BLEND:
            htex = load_texture(folder, prefix, "height")
            hs = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -1000, y)
            hs.set_editor_property("texture", htex)
            hs.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if htex.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
            hs.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
            MEL.connect_material_expressions(coords, "", hs, "UVs")
            for blend in (blend_color, blend_normal, blend_rough):
                MEL.connect_material_expressions(hs, "R", blend, "Height " + name)
            y += 220
        y += 60

    # 거시 변화(macro variation): 큰 스케일 구름 노이즈로 밝기를 0.78~1.12 사이에서 흔든다
    macro_coords = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeLayerCoords, -1300, y + 100)
    macro_coords.set_editor_property("mapping_scale", 140.0)
    macro_tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -900, y + 100)
    macro_tex.set_editor_property("texture", EAL.load_asset("/Game/DarkFantasyTopDown/Textures/Shared/T_PerlinClouds"))
    macro_tex.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    MEL.connect_material_expressions(macro_coords, "", macro_tex, "UVs")
    macro_lerp = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -500, y + 100)
    macro_lerp.set_editor_property("const_a", 0.78)
    macro_lerp.set_editor_property("const_b", 1.12)
    MEL.connect_material_expressions(macro_tex, "R", macro_lerp, "Alpha")
    macro_mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1000, -500)
    MEL.connect_material_expressions(blend_color, "", macro_mul, "A")
    MEL.connect_material_expressions(macro_lerp, "", macro_mul, "B")

    # 전체 톤: 큰 노이즈의 G 채널로 따뜻한 색과 차가운 색 사이를 오가게 해 색 반복도 깬다
    warm = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 900, -300)
    warm.set_editor_property("constant", unreal.LinearColor(0.96, 0.91, 0.84, 1.0))
    cool = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 900, -150)
    cool.set_editor_property("constant", unreal.LinearColor(0.84, 0.90, 0.98, 1.0))
    tint = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, 1000, -220)
    MEL.connect_material_expressions(warm, "", tint, "A")
    MEL.connect_material_expressions(cool, "", tint, "B")
    MEL.connect_material_expressions(macro_tex, "G", tint, "Alpha")
    tint_mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 1250, -450)
    MEL.connect_material_expressions(macro_mul, "", tint_mul, "A")
    MEL.connect_material_expressions(tint, "", tint_mul, "B")

    MEL.connect_material_property(tint_mul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # 노멀 완화: 강한 흙 요철 노멀을 (0,0,1)과 섞어 탑다운에서 주름처럼 보이는 현상을 줄인다
    flat_n = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 1000, 300)
    flat_n.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    n_lerp = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, 1250, 200)
    n_lerp.set_editor_property("const_alpha", 0.62)
    MEL.connect_material_expressions(blend_normal, "", n_lerp, "A")
    MEL.connect_material_expressions(flat_n, "", n_lerp, "B")
    MEL.connect_material_property(n_lerp, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(blend_rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    # 풀 출력: 레이어 가중치를 그대로 밀도로 쓴다(도로 레이어가 강한 곳은 자동으로 풀이 줄어든다)
    grass_out = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeGrassOutput, 1000, 1200)
    inputs = []
    for gname, (layer, gt) in grass_types.items():
        gi = unreal.GrassInput()
        gi.set_editor_property("name", gname)
        gi.set_editor_property("grass_type", gt)
        inputs.append(gi)
    grass_out.set_editor_property("grass_types", inputs)
    gy = 1100
    for gname, (layer, gt) in grass_types.items():
        sample = MEL.create_material_expression(mat, unreal.MaterialExpressionLandscapeLayerSample, 500, gy)
        sample.set_editor_property("parameter_name", layer)
        MEL.connect_material_expressions(sample, "", grass_out, gname)
        gy += 150

    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    EAL.save_asset(mat_path)

    inst_path = f"{ROOT}/MI_TD_Landscape"
    if not EAL.does_asset_exist(inst_path):
        inst = asset_tools.create_asset("MI_TD_Landscape", ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    else:
        inst = EAL.load_asset(inst_path)
    MEL.set_material_instance_parent(inst, mat)
    EAL.save_asset(inst_path)
    return mat, inst




def reassign_landscape_material(inst):
    """현재 월드의 랜드스케이프가 옛 머티리얼 참조를 들고 있으면 새 인스턴스로 갈아 끼운다."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    proxies = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.LandscapeProxy)
    for proxy in proxies:
        proxy.set_editor_property("landscape_material", None)
        proxy.set_editor_property("landscape_material", inst)
    return len(proxies)


grass = make_grass_types()
mat, inst = make_material(grass)
print("landscape proxies reassigned:", reassign_landscape_material(inst))
print("DONE material", mat.get_path_name(), "instance", inst.get_path_name(), "grass", list(grass.keys()))

"""실행 환경: TDGame 언리얼 에디터 안에서 FluidFX 점성 표면 마스터 M_TD_FluidViscousSurface(마스크 불투명·DefaultLit·머티리얼 속성, Custom 노드 2개 = 정점 WPO TDFluidViscousOffset·화소 TDFluidViscousSurface, 공용 TDFluidViscousCommon), 점성 데칼 마스터 M_TD_FluidViscousDecal(MD_DEFERRED_DECAL·반투명, 연결 출력으로 DBuffer), 용암 마스터 M_TD_FluidLavaSurface(불투명·DefaultLit·발광, TDFluidTileMap.hlsl 를 자리표시자 TD_LAVA_TILE_MAP_INSERT 에 끼워 넣음)와 MI 4개(점성 진흙·피, 피 데칼, 용암)를 만든다. 새 텍스처·메시·MPC 는 만들지 않고, 모양 정본에 MI 4개가 병합되어 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_viscous_material.py (기존 에셋을 다시 지을 때는 Saved/FluidFX/fluid_viscous_material_request.json 에 {"overwrite": true})
출력: /Game/FluidFX/Materials/M_TD_FluidViscousSurface·M_TD_FluidViscousDecal·M_TD_FluidLavaSurface·MI_TD_FluidViscous_Mud·MI_TD_FluidViscous_Blood·MI_TD_FluidViscousDecal_Blood·MI_TD_FluidLava, Saved/FluidFX/fluid_viscous_material.json
상태: 미검증(2026-10-29 파동 3 FX-14 작성, py_compile·가짜 unreal·check_fluid_viscous_hlsl 만, 에디터 실행 전: 열거 이름 단어마다 _ 는 L-editor-73, 컴파일 뒤 사용 플래그는 L-editor-61, MI 값은 다시 읽어 판정 L-editor-66, 마스터는 지우지 않고 식만 비움 L-editor-67). 구동(MID, SimGridResolution > 0.5) 일 때만 보이고 마스터 기본 0 이면 클립한다. C++ 가 쓰는 이름은 DensityPressureTexture·SimCenterWorld·SimExtentsCm·SimGridResolution(창)과 TileMap1Texture·TileMap1OriginCm·TileMap1PeriodCm(용암 타일)뿐이고 모양 파일에 넣지 않는다.
"""
import hashlib
import importlib.util
import json
import os
import re

import unreal


FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
SURFACE_MASTER_NAME = "M_TD_FluidViscousSurface"
SURFACE_MASTER_PATH = f"{MATERIAL_DIR}/{SURFACE_MASTER_NAME}"
DECAL_MASTER_NAME = "M_TD_FluidViscousDecal"
DECAL_MASTER_PATH = f"{MATERIAL_DIR}/{DECAL_MASTER_NAME}"
LAVA_MASTER_NAME = "M_TD_FluidLavaSurface"
LAVA_MASTER_PATH = f"{MATERIAL_DIR}/{LAVA_MASTER_NAME}"
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
SHADER_DIRECTORY = ("Tools", "FluidFX", "Shaders")
COMMON_HLSL_NAME = "TDFluidViscousCommon.hlsl"
SURFACE_HLSL_NAME = "TDFluidViscousSurface.hlsl"
OFFSET_HLSL_NAME = "TDFluidViscousOffset.hlsl"
DECAL_HLSL_NAME = "TDFluidViscousDecal.hlsl"
LAVA_HLSL_NAME = "TDFluidLavaSurface.hlsl"
TILE_MAP_HLSL_NAME = "TDFluidTileMap.hlsl"
TILE_MAP_PLACEHOLDER = "TD_LAVA_TILE_MAP_INSERT"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_viscous_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_viscous_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
PARAMETER_GROUP = "TDFluidViscous"
MACRO_PREFIXES = ("TD_VISCOUS_", "TD_TILEMAP_")
MASK_CLIP_VALUE = 0.5

WINDOW_VECTOR_NAMES = ("SimCenterWorld", "SimExtentsCm")
WINDOW_SCALAR_DEFAULTS = (("SimGridResolution", 0.0),)
WINDOW_VECTOR_DEFAULTS = tuple((name, (0.0, 0.0, 0.0, 0.0)) for name in WINDOW_VECTOR_NAMES)
TILE_VECTOR_DEFAULTS = (("TileMap1OriginCm", (0.0, 0.0, 0.0, 0.0)), ("TileMap1PeriodCm", (0.0, 0.0, 0.0, 0.0)))
TEXTURE_PARAMETER_NAMES_BY_MASTER = {
    SURFACE_MASTER_NAME: ("DensityPressureTexture",),
    DECAL_MASTER_NAME: ("DensityPressureTexture",),
    LAVA_MASTER_NAME: ("DensityPressureTexture", "TileMap1Texture"),
}

SURFACE_LOOK_SCALAR_DEFAULTS = (
    ("ViscousMaskThreshold", 0.25),
    ("ViscousEdgeSoftness", 0.04),
    ("ViscousHeightCm", 3.0),
    ("ViscousHeightExponent", 0.6),
    ("ViscousPressureHeightCm", 0.5),
    ("ViscousNormalStrength", 1.0),
    ("ViscousRoughness", 0.22),
    ("ViscousSpecular", 0.5),
)
DECAL_LOOK_SCALAR_DEFAULTS = (
    ("ViscousMaskThreshold", 0.2),
    ("ViscousEdgeSoftness", 0.04),
    ("ViscousHeightCm", 1.0),
    ("ViscousHeightExponent", 0.5),
    ("ViscousPressureHeightCm", 0.2),
    ("ViscousNormalStrength", 0.8),
    ("ViscousRoughness", 0.15),
    ("ViscousDecalOpacity", 0.9),
)
VISCOUS_LOOK_VECTOR_DEFAULTS = (
    ("ViscousBaseColor", (0.09, 0.06, 0.04, 1.0)),
    ("ViscousSelectiveColor", (0.09, 0.06, 0.04, 1.0)),
)
LAVA_LOOK_SCALAR_DEFAULTS = (
    ("LavaCrustThreshold", 0.5),
    ("LavaCrustSoftness", 0.12),
    ("LavaEmissiveIntensity", 20.0),
    ("LavaEmissiveExponent", 2.0),
    ("LavaLocalRevealStrength", 1.0),
    ("LavaCrustRoughness", 0.8),
    ("LavaMoltenColorScale", 0.05),
    ("LavaMoltenRoughness", 0.4),
    ("TileMap1HeightCm", 4.0),
    ("TileMap1DensityHeightCm", 6.0),
    ("TileMap1NormalStrength", 1.0),
    ("TileMap1FadeStartCm", 100000.0),
    ("TileMap1FadeLengthCm", 1000.0),
    ("TileMap1FoamStrength", 1.0),
)
LAVA_LOOK_VECTOR_DEFAULTS = (
    ("LavaEmissiveColor", (1.0, 0.25, 0.05, 1.0)),
    ("LavaCrustColor", (0.03, 0.025, 0.02, 1.0)),
)

MASTER_PARAMETERS = {
    SURFACE_MASTER_NAME: (WINDOW_SCALAR_DEFAULTS + SURFACE_LOOK_SCALAR_DEFAULTS, WINDOW_VECTOR_DEFAULTS + VISCOUS_LOOK_VECTOR_DEFAULTS),
    DECAL_MASTER_NAME: (WINDOW_SCALAR_DEFAULTS + DECAL_LOOK_SCALAR_DEFAULTS, WINDOW_VECTOR_DEFAULTS + VISCOUS_LOOK_VECTOR_DEFAULTS),
    LAVA_MASTER_NAME: (WINDOW_SCALAR_DEFAULTS + LAVA_LOOK_SCALAR_DEFAULTS, WINDOW_VECTOR_DEFAULTS + TILE_VECTOR_DEFAULTS + LAVA_LOOK_VECTOR_DEFAULTS),
}
LOOK_SCALAR_NAMES_BY_MASTER = {
    SURFACE_MASTER_NAME: {name for name, _ in SURFACE_LOOK_SCALAR_DEFAULTS},
    DECAL_MASTER_NAME: {name for name, _ in DECAL_LOOK_SCALAR_DEFAULTS},
    LAVA_MASTER_NAME: {name for name, _ in LAVA_LOOK_SCALAR_DEFAULTS},
}
LOOK_VECTOR_NAMES_BY_MASTER = {
    SURFACE_MASTER_NAME: {name for name, _ in VISCOUS_LOOK_VECTOR_DEFAULTS},
    DECAL_MASTER_NAME: {name for name, _ in VISCOUS_LOOK_VECTOR_DEFAULTS},
    LAVA_MASTER_NAME: {name for name, _ in LAVA_LOOK_VECTOR_DEFAULTS},
}
INSTANCE_PARENTS = {
    "MI_TD_FluidViscous_Mud": SURFACE_MASTER_PATH,
    "MI_TD_FluidViscous_Blood": SURFACE_MASTER_PATH,
    "MI_TD_FluidViscousDecal_Blood": DECAL_MASTER_PATH,
    "MI_TD_FluidLava": LAVA_MASTER_PATH,
}
INSTANCE_MASTER_NAMES = {name: path.rsplit("/", 1)[-1] for name, path in INSTANCE_PARENTS.items()}
MID_DRIVEN_NAMES = (
    "DensityPressureTexture", "SimCenterWorld", "SimExtentsCm", "SimGridResolution",
    "TileMap1Texture", "TileMap1OriginCm", "TileMap1PeriodCm",
    "VelocityTexture", "PlaneAxisU", "PlaneAxisV", "PlaneAxisN", "PlaneCoordinateOffsetCm", "VolumeHeightCm",
    "VolumeBoxScale", "VolumeOffsetCm", "ExtrudeSymmetric", "AuxTexture", "AuxHeatActive",
)
CPP_DRIVEN_NAMES = ("DensityPressureTexture", "SimCenterWorld", "SimExtentsCm", "SimGridResolution", "TileMap1Texture", "TileMap1OriginCm", "TileMap1PeriodCm")

COMMON_INPUT_NAMES = (
    "ViscousPosition", "DensityPressureTexture", "SimCenterWorld", "SimExtentsCm", "SimGridResolution",
    "ViscousMaskThreshold", "ViscousEdgeSoftness", "ViscousHeightCm", "ViscousHeightExponent", "ViscousPressureHeightCm",
)
OFFSET_INPUT_NAMES = COMMON_INPUT_NAMES
SURFACE_INPUT_NAMES = COMMON_INPUT_NAMES + ("ViscousNormalStrength", "ViscousBaseColor", "ViscousSelectiveColor")
DECAL_INPUT_NAMES = SURFACE_INPUT_NAMES + ("ViscousDecalOpacity",)
LAVA_INPUT_NAMES = (
    "LavaPosition", "CameraWorldPosition", "DensityPressureTexture", "TileMap1Texture", "SimCenterWorld", "SimExtentsCm",
    "SimGridResolution", "TileMap1OriginCm", "TileMap1PeriodCm", "TileMap1HeightCm", "TileMap1DensityHeightCm",
    "TileMap1NormalStrength", "TileMap1FadeStartCm", "TileMap1FadeLengthCm", "TileMap1FoamStrength",
    "LavaCrustThreshold", "LavaCrustSoftness", "LavaEmissiveIntensity", "LavaEmissiveExponent", "LavaLocalRevealStrength",
    "LavaCrustRoughness", "LavaMoltenColorScale", "LavaMoltenRoughness", "LavaEmissiveColor", "LavaCrustColor",
)
VISCOUS_SAMPLER_NAMES = ("DensityPressureTextureSampler",)
LAVA_SAMPLER_NAMES = ("DensityPressureTextureSampler", "TileMap1TextureSampler")
VISCOUS_OUTPUT_SPECS = (("ViscousNormalWS", "CMOT_FLOAT3"),)
LAVA_OUTPUT_SPECS = (("LavaEmissiveOut", "CMOT_FLOAT3"), ("LavaNormalWS", "CMOT_FLOAT3"))
BUILTIN_SOURCE_NAMES = ("ViscousPosition", "LavaPosition", "CameraWorldPosition")
SURFACE_ATTRIBUTE_INPUT_NAMES = ("BaseColor", "Specular", "Roughness", "Normal", "OpacityMask", "WorldPositionOffset")
REQUIRED_UNREAL_NAMES = (
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.does_directory_exist",
    "EditorAssetLibrary.make_directory",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
    "AssetToolsHelpers",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.delete_all_material_expressions",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_vector_parameter_names",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.get_material_default_scalar_parameter_value",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialExpressionCustom",
    "CustomInput",
    "CustomOutput",
    "CustomMaterialOutputType.CMOT_FLOAT1",
    "CustomMaterialOutputType.CMOT_FLOAT3",
    "CustomMaterialOutputType.CMOT_FLOAT4",
    "MaterialExpressionWorldPosition",
    "WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS",
    "MaterialExpressionCameraPositionWS",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionVectorParameter",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionComponentMask",
    "MaterialExpressionMakeMaterialAttributes",
    "BlendMode.BLEND_MASKED",
    "BlendMode.BLEND_OPAQUE",
    "BlendMode.BLEND_TRANSLUCENT",
    "MaterialShadingModel.MSM_DEFAULT_LIT",
    "MaterialDomain.MD_DEFERRED_DECAL",
    "MaterialProperty.MP_MATERIAL_ATTRIBUTES",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "MaterialProperty.MP_NORMAL",
    "MaterialProperty.MP_OPACITY",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_NORMAL",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_NORMALMAP",
    "TextureCompressionSettings.TC_HALF_FLOAT",
    "TDFluidSurfaceOutputActor",
    "TDFluidPresetKind.VISCOUS_MUD",
    "TDFluidPresetKind.LAVA_TILE",
    "TDFluidAreaAddressMode.WRAP",
)

IDENTIFIER_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+((?:TD_VISCOUS_|TD_TILEMAP_)[A-Za-z0-9_]+)", re.MULTILINE)
ANY_DEFINE_PATTERN = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)", re.MULTILINE)
INVOCATION_PATTERN = re.compile(r"^[ \t]*TD_VISCOUS_HEIGHT_AT\(", re.MULTILINE)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


def read_text_file(path):
    if not os.path.isfile(path):
        raise RuntimeError(f"HLSL 파일 없음: {path}")
    with open(path, encoding="utf-8") as text_file:
        return text_file.read().replace("\r\n", "\n").strip("\n") + "\n"


def read_shader_sources(project_root):
    shader_root = os.path.join(project_root, *SHADER_DIRECTORY)
    names = {
        "common": COMMON_HLSL_NAME,
        "surface": SURFACE_HLSL_NAME,
        "offset": OFFSET_HLSL_NAME,
        "decal": DECAL_HLSL_NAME,
        "lava": LAVA_HLSL_NAME,
        "tile_map": TILE_MAP_HLSL_NAME,
    }
    return {label: read_text_file(os.path.join(shader_root, file_name)) for label, file_name in names.items()}


def assemble_node_code(prefix_code, node_code):
    body = prefix_code + "\n" + node_code if prefix_code else node_code
    macro_names = sorted(set(DEFINE_PATTERN.findall(body)))
    undefine_block = "".join(f"#undef {name}\n" for name in macro_names)
    return undefine_block + body + "\n" + undefine_block


def insert_tile_map(lava_code, tile_map_code):
    if lava_code.count(TILE_MAP_PLACEHOLDER) != 1:
        raise RuntimeError(f"용암 HLSL 의 자리표시자 {TILE_MAP_PLACEHOLDER} 가 정확히 1개가 아님")
    return lava_code.replace(TILE_MAP_PLACEHOLDER, tile_map_code.strip("\n"))


def assemble_all_node_codes(sources):
    return {
        "offset": assemble_node_code(sources["common"], sources["offset"]),
        "surface": assemble_node_code(sources["common"], sources["surface"]),
        "decal": assemble_node_code(sources["common"], sources["decal"]),
        "lava": assemble_node_code("", insert_tile_map(sources["lava"], sources["tile_map"])),
    }


def find_unbalanced_brackets(code):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for index, character in enumerate(code):
        if character in "([{":
            stack.append(character)
            continue
        if character in pairs and (not stack or stack.pop() != pairs[character]):
            return f"괄호 불일치 위치 {index}"
    return f"닫히지 않은 괄호 {len(stack)}개" if stack else ""


def validate_hlsl_code(code, input_names, sampler_names, output_names):
    problems = []
    identifiers = set(IDENTIFIER_PATTERN.findall(code))
    for label, names in (("Custom 입력", input_names), ("샘플러", sampler_names), ("추가 출력", output_names), ("엔진 심볼", ("Texture2DSampleLevel",))):
        missing = [name for name in names if name not in identifiers]
        if missing:
            problems.append(f"HLSL이 쓰지 않는 {label}: " + ", ".join(missing))
    stray = [name for name in ANY_DEFINE_PATTERN.findall(code) if not name.startswith(MACRO_PREFIXES)]
    if stray:
        problems.append("접두 밖 매크로: " + ", ".join(stray))
    if TILE_MAP_PLACEHOLDER in code:
        problems.append("타일 맵 자리표시자가 남아 있음")
    if "Texture2DSample(" in code:
        problems.append("정점에서 못 쓰는 Texture2DSample( 가 있음(Texture2DSampleLevel 만)")
    bracket_problem = find_unbalanced_brackets(code)
    if bracket_problem:
        problems.append(bracket_problem)
    if "return" not in identifiers:
        problems.append("HLSL에 return 이 없음")
    if "//" in code or "/*" in code:
        problems.append("HLSL에 주석이 있음")
    return problems


def validate_all_node_codes(codes):
    problems = []
    for label, code, inputs, samplers, outputs in (
        ("오프셋", codes["offset"], OFFSET_INPUT_NAMES, VISCOUS_SAMPLER_NAMES, ()),
        ("표면", codes["surface"], SURFACE_INPUT_NAMES, VISCOUS_SAMPLER_NAMES, [name for name, _ in VISCOUS_OUTPUT_SPECS]),
        ("데칼", codes["decal"], DECAL_INPUT_NAMES, VISCOUS_SAMPLER_NAMES, [name for name, _ in VISCOUS_OUTPUT_SPECS]),
        ("용암", codes["lava"], LAVA_INPUT_NAMES, LAVA_SAMPLER_NAMES, [name for name, _ in LAVA_OUTPUT_SPECS]),
    ):
        problems.extend(f"{label}: {item}" for item in validate_hlsl_code(code, inputs, samplers, outputs))
    pixel_only = [token for token in ("ViscousNormalStrength", "viscousSlope") if token in codes["offset"]]
    if INVOCATION_PATTERN.search(codes["offset"]):
        pixel_only.append("TD_VISCOUS_HEIGHT_AT 호출")
    if pixel_only:
        problems.append("오프셋에 화소 전용 이름: " + ", ".join(pixel_only))
    return problems


def find_unresolved_inputs():
    parameter_names = set()
    for scalars, vectors in MASTER_PARAMETERS.values():
        parameter_names |= {name for name, _ in scalars} | {name for name, _ in vectors}
    texture_names = {name for names in TEXTURE_PARAMETER_NAMES_BY_MASTER.values() for name in names}
    resolvable = set(BUILTIN_SOURCE_NAMES) | texture_names | parameter_names
    return sorted(set(OFFSET_INPUT_NAMES + SURFACE_INPUT_NAMES + DECAL_INPUT_NAMES + LAVA_INPUT_NAMES) - resolvable)


def find_instance_override_problems(entries):
    problems = []
    for name, parent_path in INSTANCE_PARENTS.items():
        entry = entries.get(asset_path(name))
        if entry is None:
            problems.append(f"{name}: 모양 정본에 없음")
            continue
        if entry.get("Parent") != parent_path:
            problems.append(f"{name}: 부모 {entry.get('Parent')} ≠ {parent_path}")
        master_name = INSTANCE_MASTER_NAMES[name]
        scalar_names = set(entry.get("Scalars", {}))
        vector_names = set(entry.get("Vectors", {}))
        driven = sorted((scalar_names | vector_names | set(entry.get("Switches", {}))) & set(MID_DRIVEN_NAMES))
        if driven:
            problems.append(f"{name}: C++ 구동 이름 덮어씀(R7) " + ", ".join(driven))
        unknown = sorted((scalar_names - LOOK_SCALAR_NAMES_BY_MASTER[master_name]) | (vector_names - LOOK_VECTOR_NAMES_BY_MASTER[master_name]))
        if unknown:
            problems.append(f"{name}: 마스터에 없거나 구획이 어긋난 이름 " + ", ".join(unknown))
        if entry.get("Switches"):
            problems.append(f"{name}: 정적 스위치는 마스터에 없음")
    return problems


def load_material_look_table():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())


def find_missing_unreal_names():
    missing = []
    for dotted_name in REQUIRED_UNREAL_NAMES:
        target = unreal
        for part in dotted_name.split("."):
            target = getattr(target, part, None)
            if target is None:
                missing.append(dotted_name)
                break
    return missing


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def collect_target_asset_paths():
    return [SURFACE_MASTER_PATH, DECAL_MASTER_PATH, LAVA_MASTER_PATH] + [asset_path(name) for name in INSTANCE_PARENTS]


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    return names if not overwrite else [name for name in names if not name.startswith(FLUID_ROOT + "/")]


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


def has_editor_property(owner, property_name):
    try:
        owner.get_editor_property(property_name)
    except Exception:
        return False
    return True


def choose_sampler_type(texture):
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression == unreal.TextureCompressionSettings.TC_NORMALMAP:
        return unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    if compression == unreal.TextureCompressionSettings.TC_HALF_FLOAT:
        return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    if compression != unreal.TextureCompressionSettings.TC_DEFAULT:
        return None
    return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if is_srgb else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture) if texture is not None else None
        if sampler_type is not None:
            return texture_path, texture, sampler_type
    raise RuntimeError("기본 텍스처로 쓸 엔진 텍스처가 없음: " + ", ".join(DEFAULT_TEXTURE_PATHS))


class SourceFactory:
    def __init__(self, material, master_name, default_texture, default_sampler, excludes_shader_offsets):
        self.material = material
        self.texture_names = TEXTURE_PARAMETER_NAMES_BY_MASTER[master_name]
        self.default_texture = default_texture
        self.default_sampler = default_sampler
        self.excludes_shader_offsets = excludes_shader_offsets
        scalars, vectors = MASTER_PARAMETERS[master_name]
        self.scalar_defaults = dict(scalars)
        self.vector_defaults = dict(vectors)
        self.sources = {}
        self.row = 0

    def node(self, expression_class, x, y, **properties):
        expression = unreal.MaterialEditingLibrary.create_material_expression(self.material, expression_class, x, y)
        if expression is None:
            raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
        for name, value in properties.items():
            expression.set_editor_property(name, value)
        return expression

    def truncated(self, source, x, y):
        truncate_class = getattr(unreal, "MaterialExpressionTruncateLWC", None)
        if truncate_class is None:
            return source
        truncated = self.node(truncate_class, x, y)
        link(source, truncated)
        return truncated

    def get(self, name):
        if name not in self.sources:
            x, y = -1600, -1800 + self.row * 110
            self.row += 1
            self.sources[name] = self.create(name, x, y)
        return self.sources[name]

    def create_position(self, x, y):
        if self.excludes_shader_offsets:
            position = self.node(unreal.MaterialExpressionWorldPosition, x - 220, y, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        else:
            position = self.node(unreal.MaterialExpressionWorldPosition, x - 220, y)
        return self.truncated(position, x, y), ""

    def create(self, name, x, y):
        if name in ("ViscousPosition", "LavaPosition"):
            return self.create_position(x, y)
        if name == "CameraWorldPosition":
            return self.truncated(self.node(unreal.MaterialExpressionCameraPositionWS, x - 220, y), x, y), ""
        if name in self.texture_names:
            node = self.node(unreal.MaterialExpressionTextureObjectParameter, x, y, parameter_name=name, texture=self.default_texture, sampler_type=self.default_sampler)
            assign_parameter_group(node)
            return node, ""
        if name in self.vector_defaults:
            node = self.node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*self.vector_defaults[name]))
            assign_parameter_group(node)
            return node, "RGB"
        if name in self.scalar_defaults:
            node = self.node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=self.scalar_defaults[name])
            assign_parameter_group(node)
            return node, ""
        raise RuntimeError(f"입력 원천을 정할 수 없음: {name}")


def link(source, target, target_input="", source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}")


def connect_material_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property} <- {source_output}")


def assign_parameter_group(expression):
    try:
        expression.set_editor_property("group", PARAMETER_GROUP)
    except Exception:
        return


def build_custom_node(factory, description, output_type, input_names, code, x, y, output_specs):
    custom = factory.node(unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", description)
    custom.set_editor_property("output_type", output_type)
    inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    outputs = []
    for name, kind in output_specs:
        custom_output = unreal.CustomOutput()
        custom_output.set_editor_property("output_name", name)
        custom_output.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(custom_output)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("code", code)
    stored = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored != list(input_names):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored))
    for name in input_names:
        source, source_output = factory.get(name)
        link(source, custom, name, source_output)
    return custom


def prepare_master(name, path, overwrite):
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        material = assets.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
        return material, False
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {path}")
    return material, True


def apply_master_properties(material, properties):
    for name, value in properties:
        material.set_editor_property(name, value)


def apply_mask_clip_value(material):
    if not has_editor_property(material, "opacity_mask_clip_value"):
        return False
    material.set_editor_property("opacity_mask_clip_value", MASK_CLIP_VALUE)
    return abs(float(material.get_editor_property("opacity_mask_clip_value")) - MASK_CLIP_VALUE) < 1e-6


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def compile_and_verify(material, master_name, path):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"{master_name} 컴파일 실패(Saved/Logs/TDGame.log 의 LogMaterial 확인): " + " | ".join(errors))
    editing = unreal.MaterialEditingLibrary
    scalars, vectors = MASTER_PARAMETERS[master_name]
    found = {
        "vector": {str(name) for name in editing.get_vector_parameter_names(material)},
        "scalar": {str(name) for name in editing.get_scalar_parameter_names(material)},
        "texture": {str(name) for name in editing.get_texture_parameter_names(material)},
    }
    expected = {"vector": [name for name, _ in vectors], "scalar": [name for name, _ in scalars], "texture": list(TEXTURE_PARAMETER_NAMES_BY_MASTER[master_name])}
    missing = [f"{kind}:{name}" for kind, names in expected.items() for name in names if name not in found[kind]]
    if missing:
        raise RuntimeError(f"{master_name} 마스터 파라미터 없음: " + ", ".join(missing))
    if abs(editing.get_material_default_scalar_parameter_value(material, "SimGridResolution")) > 1e-6:
        raise RuntimeError(f"{master_name} SimGridResolution 기본값이 0이 아님(구동 아님 판정 깨짐)")
    save_asset(material, path)
    return read_material_statistics(material)


def mask_channels(factory, source, x, y, channels):
    mask = factory.node(unreal.MaterialExpressionComponentMask, x, y, r="R" in channels, g="G" in channels, b="B" in channels, a="A" in channels)
    link(source, mask)
    return mask


def create_surface_master(codes, default_texture, default_sampler, overwrite):
    material, was_created = prepare_master(SURFACE_MASTER_NAME, SURFACE_MASTER_PATH, overwrite)
    apply_master_properties(material, (("blend_mode", unreal.BlendMode.BLEND_MASKED), ("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT), ("tangent_space_normal", False), ("two_sided", False), ("use_material_attributes", True)))
    clip_applied = apply_mask_clip_value(material)
    factory = SourceFactory(material, SURFACE_MASTER_NAME, default_texture, default_sampler, True)
    offset = build_custom_node(factory, "TDFluidViscousOffset", unreal.CustomMaterialOutputType.CMOT_FLOAT3, OFFSET_INPUT_NAMES, codes["offset"], -500, 700, ())
    surface = build_custom_node(factory, "TDFluidViscousSurface", unreal.CustomMaterialOutputType.CMOT_FLOAT4, SURFACE_INPUT_NAMES, codes["surface"], -500, 0, VISCOUS_OUTPUT_SPECS)
    color = mask_channels(factory, surface, -150, -120, "RGB")
    opacity = mask_channels(factory, surface, -150, 40, "A")
    roughness, roughness_output = factory.get("ViscousRoughness")
    specular, specular_output = factory.get("ViscousSpecular")
    attributes = factory.node(unreal.MaterialExpressionMakeMaterialAttributes, 200, 0)
    sources_by_attribute = {
        "BaseColor": (color, ""),
        "Specular": (specular, specular_output),
        "Roughness": (roughness, roughness_output),
        "Normal": (surface, "ViscousNormalWS"),
        "OpacityMask": (opacity, ""),
        "WorldPositionOffset": (offset, ""),
    }
    for attribute_name in SURFACE_ATTRIBUTE_INPUT_NAMES:
        source, source_output = sources_by_attribute[attribute_name]
        link(source, attributes, attribute_name, source_output)
    connect_material_output(attributes, unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    if not material.get_editor_property("use_material_attributes"):
        raise RuntimeError(f"{SURFACE_MASTER_NAME} use_material_attributes 설정 확인 실패")
    statistics = compile_and_verify(material, SURFACE_MASTER_NAME, SURFACE_MASTER_PATH)
    return {"asset": SURFACE_MASTER_PATH, "created": was_created, "material_attributes": list(SURFACE_ATTRIBUTE_INPUT_NAMES), "mask_clip_value_applied": clip_applied, "statistics": statistics}


def create_decal_master(codes, default_texture, default_sampler, overwrite):
    material, was_created = prepare_master(DECAL_MASTER_NAME, DECAL_MASTER_PATH, overwrite)
    apply_master_properties(material, (("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL), ("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT), ("tangent_space_normal", False)))
    factory = SourceFactory(material, DECAL_MASTER_NAME, default_texture, default_sampler, False)
    decal = build_custom_node(factory, "TDFluidViscousDecal", unreal.CustomMaterialOutputType.CMOT_FLOAT4, DECAL_INPUT_NAMES, codes["decal"], -500, 0, VISCOUS_OUTPUT_SPECS)
    color = mask_channels(factory, decal, -150, -120, "RGB")
    opacity = mask_channels(factory, decal, -150, 40, "A")
    roughness, roughness_output = factory.get("ViscousRoughness")
    connect_material_output(color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS, roughness_output)
    connect_material_output(decal, unreal.MaterialProperty.MP_NORMAL, "ViscousNormalWS")
    connect_material_output(opacity, unreal.MaterialProperty.MP_OPACITY)
    statistics = compile_and_verify(material, DECAL_MASTER_NAME, DECAL_MASTER_PATH)
    return {"asset": DECAL_MASTER_PATH, "created": was_created, "domain": "MD_DEFERRED_DECAL", "connected": ["BaseColor", "Roughness", "Normal", "Opacity"], "statistics": statistics}


def create_lava_master(codes, default_texture, default_sampler, overwrite):
    material, was_created = prepare_master(LAVA_MASTER_NAME, LAVA_MASTER_PATH, overwrite)
    apply_master_properties(material, (("blend_mode", unreal.BlendMode.BLEND_OPAQUE), ("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT), ("tangent_space_normal", False), ("two_sided", False)))
    factory = SourceFactory(material, LAVA_MASTER_NAME, default_texture, default_sampler, False)
    lava = build_custom_node(factory, "TDFluidLavaSurface", unreal.CustomMaterialOutputType.CMOT_FLOAT4, LAVA_INPUT_NAMES, codes["lava"], -500, 0, LAVA_OUTPUT_SPECS)
    color = mask_channels(factory, lava, -150, -120, "RGB")
    roughness = mask_channels(factory, lava, -150, 40, "A")
    connect_material_output(color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_material_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    connect_material_output(lava, unreal.MaterialProperty.MP_EMISSIVE_COLOR, "LavaEmissiveOut")
    connect_material_output(lava, unreal.MaterialProperty.MP_NORMAL, "LavaNormalWS")
    statistics = compile_and_verify(material, LAVA_MASTER_NAME, LAVA_MASTER_PATH)
    return {"asset": LAVA_MASTER_PATH, "created": was_created, "connected": ["BaseColor", "Roughness", "EmissiveColor", "Normal"], "statistics": statistics}


def prepare_instance(name, overwrite):
    assets = unreal.EditorAssetLibrary
    path = asset_path(name)
    if assets.does_asset_exist(path):
        if not overwrite:
            raise RuntimeError(f"기존 에셋 보존: {path}")
        instance = assets.load_asset(path)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            raise RuntimeError(f"기존 에셋 형식 불일치: {path}")
        return instance
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    return instance


def create_instances(look_table, masters, overwrite):
    records = []
    for name, parent_path in INSTANCE_PARENTS.items():
        instance = prepare_instance(name, overwrite)
        entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, asset_path(name), masters[parent_path])
        save_asset(instance, asset_path(name))
        records.append({"asset": asset_path(name), "parent": parent_path, "scalars": entry["Scalars"], "vectors": entry["Vectors"]})
    return records


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assets = unreal.EditorAssetLibrary
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")

    look_table = load_material_look_table()
    codes = assemble_all_node_codes(read_shader_sources(unreal.SystemLibrary.get_project_directory()))
    problems = validate_all_node_codes(codes) + [f"입력 원천 없음: {name}" for name in find_unresolved_inputs()]
    problems += find_instance_override_problems(look_table.look["Materials"])
    if problems:
        raise RuntimeError("자체 점검 실패: " + " | ".join(problems))

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_target_asset_paths() if assets.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 마스터 3")
    default_path, default_texture, default_sampler = pick_default_texture()
    surface_record = create_surface_master(codes, default_texture, default_sampler, overwrite)
    decal_record = create_decal_master(codes, default_texture, default_sampler, overwrite)
    lava_record = create_lava_master(codes, default_texture, default_sampler, overwrite)

    print("단계: 인스턴스 4")
    masters = {path: assets.load_asset(path) for path in (SURFACE_MASTER_PATH, DECAL_MASTER_PATH, LAVA_MASTER_PATH)}
    instance_records = create_instances(look_table, masters, overwrite)

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "default_texture": default_path,
        "runtime_parameter_names": list(CPP_DRIVEN_NAMES),
        "masters": {"surface": surface_record, "decal": decal_record, "lava": lava_record},
        "instances": instance_records,
        "hlsl_sha256": {label: hashlib.sha256(code.encode("utf-8")).hexdigest() for label, code in codes.items()},
        "material_look_sha256": look_table.source_sha256(),
        "saved": True,
    }
    report_path = write_report(report)
    print(json.dumps(report, ensure_ascii=False, default=str))
    print("보고서: " + report_path)


def run_with_failure_report():
    try:
        main()
    except Exception as error:
        write_report({"status": "failed", "error": str(error)})
        raise


if __name__ == "__main__":
    run_with_failure_report()

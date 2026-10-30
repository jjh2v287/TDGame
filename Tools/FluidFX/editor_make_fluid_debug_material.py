"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행, PIE 밖, PowerShell에서 호출 L-repo-01). 유체 버퍼 디버그 보기 재질 하나를 만든다: M_TD_FluidBufferView(표면·Unlit·Opaque·양면, Custom 노드 1개에 인라인 HLSL, DebugChannel 0 밀도 R·1 표면 G·2 선택 B·3 마스크 A·4 속도 RG·5 속력 B·6 경사 A·7 보조 R·8 수면 RGB, saturate(값 × DebugGain + DebugBias)를 Emissive 로 출력)와 채널별 인스턴스 9개. 텍스처 매개변수 4개는 C++ 가 쓰는 이름과 같다(TDFluidLiveComponent.cpp ApplyParametersToMID).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_debug_material.py (모양 정본에 look-fragment-fx16b 조각이 병합된 뒤, 무대·재질 생성보다 앞. 덮어쓰기는 Saved/FluidFX/fluid_debug_material_request.json 에 {"overwrite": true} 가 있을 때만)
출력: /Game/FluidFX/Materials/Debug/M_TD_FluidBufferView, MI_TD_FluidBufferView_{Density,Surface,Selective,Mask,Velocity,Speed,Slope,Aux,WaterSurface}, Saved/FluidFX/fluid_debug_material.json(컴파일 오류 수, 모양 정본 sha256, Custom 코드 sha256, 인스턴스별 다시 읽은 값). 인스턴스 값은 TDMaterialLookTable.apply_to_instance 만 쓴다. 기존 에셋이 있고 덮어쓰기 요청이 없으면 아무것도 바꾸지 않고 중단한다.
상태: 미검증(2026-10-29 FX-16b 묶음 E② 작성: py_compile·가짜 unreal 시험만, 에디터 실행 전). 사용 플래그 불필요(Unlit 표면, L-editor-61), 매개변수는 다시 읽어 판정(L-editor-66), 덮어쓰기 삭제 뒤 디스크 잔존을 확인(L-editor-67), 클래스 판정이 필요하면 unreal.MathLibrary.class_is_child_of(L-editor-73).
"""
import hashlib
import importlib.util
import json
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials/Debug"
MASTER_NAME = "M_TD_FluidBufferView"
MASTER_PATH = f"{MATERIAL_DIR}/{MASTER_NAME}"
INSTANCE_NAME_PREFIX = "MI_TD_FluidBufferView_"
CHANNEL_SPECS = (
    ("Density", 0),
    ("Surface", 1),
    ("Selective", 2),
    ("Mask", 3),
    ("Velocity", 4),
    ("Speed", 5),
    ("Slope", 6),
    ("Aux", 7),
    ("WaterSurface", 8),
)
TEXTURE_PARAMETER_NAMES = ("DensityPressureTexture", "VelocityTexture", "AuxTexture", "WaterSurfaceTexture")
SCALAR_PARAMETER_DEFAULTS = (("DebugChannel", 0.0), ("DebugGain", 1.0), ("DebugBias", 0.0))
SCALAR_PARAMETER_NAMES = tuple(name for name, _ in SCALAR_PARAMETER_DEFAULTS)
CUSTOM_INPUT_NAMES = ("Uv",) + SCALAR_PARAMETER_NAMES + TEXTURE_PARAMETER_NAMES
DEFAULT_TEXTURE_PATHS = ("/Engine/EngineResources/Black", "/Engine/EngineResources/DefaultTexture")
PARAMETER_GROUP = "TDFluidBufferView"
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_debug_material_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_debug_material.json")
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
REQUIRED_UNREAL_NAMES = (
    "AssetToolsHelpers.get_asset_tools",
    "AssetRegistryHelpers.get_asset_registry",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.get_scalar_parameter_names",
    "MaterialEditingLibrary.get_texture_parameter_names",
    "MaterialEditingLibrary.get_material_default_scalar_parameter_value",
    "MaterialEditingLibrary.get_material_instance_scalar_parameter_value",
    "MaterialExpressionCustom",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionTextureObjectParameter",
    "MaterialExpressionTextureCoordinate",
    "CustomInput",
    "CustomMaterialOutputType.CMOT_FLOAT3",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "BlendMode.BLEND_OPAQUE",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialSamplerType.SAMPLERTYPE_COLOR",
    "MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR",
    "TextureCompressionSettings.TC_DEFAULT",
    "TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP",
    "EditorAssetLibrary.does_asset_exist",
    "EditorAssetLibrary.does_directory_exist",
    "EditorAssetLibrary.make_directory",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.delete_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "EditorLoadingAndSavingUtils.unload_packages",
    "SystemLibrary.get_project_directory",
    "SystemLibrary.collect_garbage",
    "UnrealEditorSubsystem",
)
CUSTOM_CODE = """float4 densityPressure = Texture2DSample(DensityPressureTexture, DensityPressureTextureSampler, Uv.xy);
float4 velocity = Texture2DSample(VelocityTexture, VelocityTextureSampler, Uv.xy);
float4 aux = Texture2DSample(AuxTexture, AuxTextureSampler, Uv.xy);
float4 waterSurface = Texture2DSample(WaterSurfaceTexture, WaterSurfaceTextureSampler, Uv.xy);
int channel = (int)floor(DebugChannel + 0.5);
float3 value = densityPressure.rrr;
if (channel == 1) value = densityPressure.ggg;
if (channel == 2) value = densityPressure.bbb;
if (channel == 3) value = densityPressure.aaa;
if (channel == 4) value = float3(velocity.rg, 0.0);
if (channel == 5) value = velocity.bbb;
if (channel == 6) value = velocity.aaa;
if (channel == 7) value = aux.rrr;
if (channel == 8) value = waterSurface.rgb;
return saturate(value * DebugGain + DebugBias);
"""


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def instance_name(channel_name):
    return INSTANCE_NAME_PREFIX + channel_name


def instance_path(channel_name):
    return f"{MATERIAL_DIR}/{instance_name(channel_name)}"


def collect_target_asset_paths():
    return [instance_path(channel_name) for channel_name, _ in CHANNEL_SPECS] + [MASTER_PATH]


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


def find_unbalanced_brackets(code):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for character in code:
        if character in "([{":
            stack.append(character)
            continue
        if character in pairs and (not stack or stack.pop() != pairs[character]):
            return True
    return bool(stack)


def find_code_problems():
    problems = []
    if find_unbalanced_brackets(CUSTOM_CODE):
        problems.append("Custom 코드 괄호 짝이 맞지 않음")
    if "//" in CUSTOM_CODE or "/*" in CUSTOM_CODE:
        problems.append("Custom 코드에 주석이 있음")
    for name in CUSTOM_INPUT_NAMES:
        if name not in CUSTOM_CODE:
            problems.append(f"Custom 입력을 코드가 쓰지 않음: {name}")
    for name in TEXTURE_PARAMETER_NAMES:
        if f"{name}Sampler" not in CUSTOM_CODE:
            problems.append(f"텍스처 표본기 이름을 코드가 쓰지 않음: {name}Sampler")
    for _, channel in CHANNEL_SPECS[1:]:
        if f"channel == {channel})" not in CUSTOM_CODE:
            problems.append(f"채널 분기 없음: {channel}")
    return problems


def load_material_look_table():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())


def find_look_entry_problems(look_table):
    problems = []
    for channel_name, channel in CHANNEL_SPECS:
        path = instance_path(channel_name)
        try:
            entry = look_table.material(path, MASTER_PATH)
        except RuntimeError as error:
            problems.append(str(error))
            continue
        if set(entry["Scalars"]) != set(SCALAR_PARAMETER_NAMES):
            problems.append(f"{instance_name(channel_name)}: 스칼라 이름이 {', '.join(SCALAR_PARAMETER_NAMES)} 와 다름")
        if entry["Vectors"] or entry["Switches"]:
            problems.append(f"{instance_name(channel_name)}: 벡터·스위치 값은 이 재질에 없음")
        if entry["Scalars"].get("DebugChannel") != channel:
            problems.append(f"{instance_name(channel_name)}: DebugChannel 이 {channel} 이 아님")
    return problems


def read_overwrite_request():
    request_path = project_path(*REQUEST_RELATIVE_PATH)
    if not os.path.isfile(request_path):
        return False
    with open(request_path, encoding="utf-8") as request_file:
        request = json.load(request_file)
    return isinstance(request, dict) and request.get("overwrite") is True


def find_unsaved_package_names(overwrite):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    names = sorted({package.get_name() for package in dirty})
    if not overwrite:
        return names
    return [name for name in names if not name.startswith(MATERIAL_DIR + "/")]


def ensure_directory(directory_path):
    if unreal.EditorAssetLibrary.does_directory_exist(directory_path):
        return
    if not unreal.EditorAssetLibrary.make_directory(directory_path):
        raise RuntimeError(f"폴더 생성 실패: {directory_path}")


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


def delete_asset_from_disk(target_path):
    asset = unreal.EditorAssetLibrary.load_asset(target_path)
    package = asset.get_outermost() if asset is not None else None
    asset = None
    unreal.EditorAssetLibrary.delete_asset(target_path)
    if package is not None:
        unreal.EditorLoadingAndSavingUtils.unload_packages([package])
    package = None
    unreal.SystemLibrary.collect_garbage()
    file_path = project_path("Content", *target_path[len("/Game/"):].split("/")) + ".uasset"
    if os.path.exists(file_path):
        os.remove(file_path)
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([MATERIAL_DIR], True)
    if unreal.EditorAssetLibrary.does_asset_exist(target_path) or os.path.exists(file_path):
        raise RuntimeError(f"기존 에셋 삭제 뒤에도 남아 있음(참조 맵을 닫거나 python Tools/ue_editor.py restart 뒤 재실행, L-editor-67): {target_path}")


def delete_existing_assets(existing_paths):
    for target_path in existing_paths:
        delete_asset_from_disk(target_path)


def choose_sampler_type(texture):
    if texture is None:
        return None
    try:
        compression = texture.get_editor_property("compression_settings")
        is_srgb = bool(texture.get_editor_property("srgb"))
    except Exception:
        return None
    if compression not in (unreal.TextureCompressionSettings.TC_DEFAULT, unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP):
        return None
    if is_srgb:
        return unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
    return unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR


def pick_default_texture():
    attempts = []
    for texture_path in DEFAULT_TEXTURE_PATHS:
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        sampler_type = choose_sampler_type(texture)
        if sampler_type is not None:
            return texture_path, texture, sampler_type
        attempts.append(texture_path)
    raise RuntimeError("기본 텍스처로 쓸 수 있는 엔진 텍스처가 없음: " + ", ".join(attempts))


def add_node(material, expression_class, x, y, **properties):
    expression = unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)
    if expression is None:
        raise RuntimeError(f"재질 노드 생성 실패: {expression_class.__name__}")
    for name, value in properties.items():
        expression.set_editor_property(name, value)
    return expression


def link(source, target, target_input, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"재질 노드 연결 실패: {source.get_name()}[{source_output}] -> {target.get_name()}.{target_input}")


def connect_output(expression, material_property, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_property(expression, source_output, material_property):
        raise RuntimeError(f"재질 출력 연결 실패: {material_property}")


def add_input_sources(material, default_texture, sampler_type):
    sources = {"Uv": add_node(material, unreal.MaterialExpressionTextureCoordinate, -900, -200)}
    for row, (name, default_value) in enumerate(SCALAR_PARAMETER_DEFAULTS):
        sources[name] = add_node(material, unreal.MaterialExpressionScalarParameter, -900, 0 + row * 130, parameter_name=name, default_value=default_value, group=PARAMETER_GROUP)
    for row, name in enumerate(TEXTURE_PARAMETER_NAMES):
        sources[name] = add_node(material, unreal.MaterialExpressionTextureObjectParameter, -900, 460 + row * 160, parameter_name=name, texture=default_texture, sampler_type=sampler_type, group=PARAMETER_GROUP)
    return sources


def build_custom_node(material, sources):
    custom = add_node(material, unreal.MaterialExpressionCustom, -300, 0, description="TDFluidBufferView", output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in CUSTOM_INPUT_NAMES:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("code", CUSTOM_CODE)
    stored = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    if stored != list(CUSTOM_INPUT_NAMES):
        raise RuntimeError("Custom 노드 입력 이름 설정 확인 실패: " + ", ".join(stored))
    for name in CUSTOM_INPUT_NAMES:
        link(sources[name], custom, name)
    return custom


def compile_master(material):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: 오류 {len(errors)}개 {MASTER_PATH} (Saved/Logs/TDGame.log 의 LogMaterial 확인) " + " | ".join(errors))
    return len(errors)


def read_material_statistics(material):
    try:
        statistics = unreal.MaterialEditingLibrary.get_statistics(material)
        return {name: statistics.get_editor_property(name) for name in ("num_vertex_shader_instructions", "num_pixel_shader_instructions", "num_samplers")}
    except Exception:
        return {}


def verify_master_parameters(material):
    editing = unreal.MaterialEditingLibrary
    scalar_names = {str(name) for name in editing.get_scalar_parameter_names(material)}
    texture_names = {str(name) for name in editing.get_texture_parameter_names(material)}
    missing = [f"scalar:{name}" for name in SCALAR_PARAMETER_NAMES if name not in scalar_names]
    missing += [f"texture:{name}" for name in TEXTURE_PARAMETER_NAMES if name not in texture_names]
    if missing:
        raise RuntimeError("마스터 파라미터 없음: " + ", ".join(missing))
    for name, default_value in SCALAR_PARAMETER_DEFAULTS:
        if abs(editing.get_material_default_scalar_parameter_value(material, name) - default_value) > 1e-6:
            raise RuntimeError(f"마스터 {name} 기본값이 {default_value} 이 아님")


def create_master(default_texture, sampler_type):
    ensure_directory(MATERIAL_DIR)
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MASTER_NAME, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"마스터 재질 생성 실패: {MASTER_PATH}")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    sources = add_input_sources(material, default_texture, sampler_type)
    custom = build_custom_node(material, sources)
    connect_output(custom, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    compile_error_count = compile_master(material)
    verify_master_parameters(material)
    save_asset(material, MASTER_PATH)
    return material, {
        "asset": MASTER_PATH,
        "blend_mode": "BLEND_OPAQUE",
        "shading_model": "MSM_UNLIT",
        "two_sided": True,
        "scalar_parameters": list(SCALAR_PARAMETER_NAMES),
        "texture_parameters": list(TEXTURE_PARAMETER_NAMES),
        "compile_error_count": compile_error_count,
        "statistics": read_material_statistics(material),
        "custom_code_sha256": hashlib.sha256(CUSTOM_CODE.encode("utf-8")).hexdigest(),
    }


def create_instance(channel_name, channel, master, look_table):
    path = instance_path(channel_name)
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        instance_name(channel_name), MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, path, master)
    reread = {name: unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, name) for name in SCALAR_PARAMETER_NAMES}
    if int(round(reread["DebugChannel"])) != channel:
        raise RuntimeError(f"MI 다시 읽기 불일치: {path} DebugChannel {reread['DebugChannel']} ≠ {channel}")
    save_asset(instance, path)
    return {"asset": path, "channel": channel, "scalars": entry["Scalars"], "reread": reread}


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=2, default=str)
    return report_path


def main():
    missing_names = find_missing_unreal_names()
    if missing_names:
        raise RuntimeError("언리얼 Python 이름 없음: " + ", ".join(missing_names))
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    look_table = load_material_look_table()
    problems = find_code_problems() + find_look_entry_problems(look_table)
    if problems:
        raise RuntimeError("자체 점검 실패: " + " | ".join(problems))

    overwrite = read_overwrite_request()
    existing_paths = [path for path in collect_target_asset_paths() if unreal.EditorAssetLibrary.does_asset_exist(path)]
    if existing_paths and not overwrite:
        print(json.dumps({"status": "기존 에셋이 있어 아무것도 바꾸지 않았습니다", "existing_assets": existing_paths, "overwrite_request_file": project_path(*REQUEST_RELATIVE_PATH), "overwrite_request_content": {"overwrite": True}}, ensure_ascii=False))
        raise RuntimeError("기존 에셋 보존: overwrite 요청 파일이 없어 중단합니다.")
    unsaved_names = find_unsaved_package_names(overwrite)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))

    print("단계: 기존 에셋 정리" if existing_paths else "단계: 새로 생성")
    delete_existing_assets(existing_paths)
    default_path, default_texture, sampler_type = pick_default_texture()
    print("단계: 마스터 재질")
    master, master_record = create_master(default_texture, sampler_type)
    print("단계: 재질 인스턴스")
    instance_records = [create_instance(channel_name, channel, master, look_table) for channel_name, channel in CHANNEL_SPECS]

    report = {
        "status": "ok",
        "overwrite": overwrite,
        "default_texture": default_path,
        "default_sampler_type": str(sampler_type),
        "master": master_record,
        "instances": instance_records,
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

"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). 유체 구동 나이아가라 입자 재질 2종을 노드만으로(Custom HLSL 없음) 만든다: M_TD_FluidDrivenParticle(반투명·무조명, Emissive = ParticleColor × ParticleEmissive, Opacity = ParticleColor.A, 나이아가라 스프라이트·메시 사용 플래그)과 M_TD_FluidDrivenParticleMesh(불투명·기본 조명, BaseColor = ParticleColor, Roughness = ParticleRoughness, 나이아가라 메시 사용 플래그), 그리고 인스턴스 MI_*_Default 2개(값은 모양 정본 TDFluidMaterialLook.json Materials).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_particle_material.py (모양 정본에 MI 2개 항목이 병합된 뒤, spec-fx8n 4.3 순서 재질 → 시스템 7 → 무대 3)
출력: /Game/FluidFX/Materials/M_TD_FluidDrivenParticle·M_TD_FluidDrivenParticleMesh·MI_TD_FluidDrivenParticle_Default·MI_TD_FluidDrivenParticleMesh_Default, Saved/FluidFX/fluid_particle_material.json(material_look_sha256 포함). 대상 에셋이 하나라도 있으면 덮어쓰지 않고 중단하고, 미저장 작업이 있으면 중단한다(삭제는 사용자 승인 뒤).
상태: 미검증(2026-10-25 FX-8n B2 작성: py_compile·가짜 unreal 검사만, 에디터 실행 전). 나이아가라 렌더러 MI 값은 C++ MID 가 없어 굽기 전용이다(반박 S2, 실행 중 재적용 대상 아님).
"""
import importlib.util
import json
import os

import unreal

MATERIAL_DIR = "/Game/FluidFX/Materials"
SPRITE_MASTER_NAME = "M_TD_FluidDrivenParticle"
MESH_MASTER_NAME = "M_TD_FluidDrivenParticleMesh"
SPRITE_INSTANCE_NAME = "MI_TD_FluidDrivenParticle_Default"
MESH_INSTANCE_NAME = "MI_TD_FluidDrivenParticleMesh_Default"
EMISSIVE_PARAMETER_NAME = "ParticleEmissive"
ROUGHNESS_PARAMETER_NAME = "ParticleRoughness"
PARAMETER_GROUP = "TDFluidDrivenParticle"
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_particle_material.json")
REQUIRED_UNREAL_NAMES = (
    "AssetToolsHelpers.get_asset_tools",
    "Material",
    "MaterialFactoryNew",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialEditingLibrary.create_material_expression",
    "MaterialEditingLibrary.connect_material_expressions",
    "MaterialEditingLibrary.connect_material_property",
    "MaterialEditingLibrary.recompile_material",
    "MaterialEditingLibrary.set_material_usage",
    "MaterialEditingLibrary.has_material_usage",
    "MaterialEditingLibrary.set_material_instance_parent",
    "MaterialEditingLibrary.update_material_instance",
    "MaterialExpressionParticleColor",
    "MaterialExpressionScalarParameter",
    "MaterialExpressionMultiply",
    "MaterialUsage.MATUSAGE_NIAGARA_SPRITES",
    "MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES",
    "MaterialProperty.MP_EMISSIVE_COLOR",
    "MaterialProperty.MP_OPACITY",
    "MaterialProperty.MP_BASE_COLOR",
    "MaterialProperty.MP_ROUGHNESS",
    "BlendMode.BLEND_TRANSLUCENT",
    "BlendMode.BLEND_OPAQUE",
    "MaterialShadingModel.MSM_UNLIT",
    "MaterialShadingModel.MSM_DEFAULT_LIT",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "SystemLibrary.get_project_directory",
)


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def asset_path(name):
    return f"{MATERIAL_DIR}/{name}"


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


def load_material_look_table():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())


def find_unsaved_package_names():
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return sorted({package.get_name() for package in dirty})


def save_asset(asset, path):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"에셋 저장 실패: {path}")


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


def add_scalar_parameter(material, name, default_value, x, y):
    return add_node(material, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=default_value, group=PARAMETER_GROUP)


def enable_usages(material, usages):
    editing = unreal.MaterialEditingLibrary
    for usage in usages:
        editing.set_material_usage(material, usage)
        if not editing.has_material_usage(material, usage):
            raise RuntimeError(f"재질 사용 플래그 설정 실패: {usage}")


def compile_material(material, path):
    errors = [str(item) for item in unreal.MaterialEditingLibrary.recompile_material(material) or []]
    if errors:
        raise RuntimeError(f"재질 컴파일 실패: {path}, {errors}")


def create_master(name, blend_mode, shading_model):
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError(f"재질 생성 실패: {asset_path(name)}")
    material.set_editor_property("blend_mode", blend_mode)
    material.set_editor_property("shading_model", shading_model)
    return material


def build_sprite_master():
    path = asset_path(SPRITE_MASTER_NAME)
    material = create_master(SPRITE_MASTER_NAME, unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_UNLIT)
    particle_color = add_node(material, unreal.MaterialExpressionParticleColor, -600, 0)
    emissive_scale = add_scalar_parameter(material, EMISSIVE_PARAMETER_NAME, 1.0, -600, 200)
    multiply = add_node(material, unreal.MaterialExpressionMultiply, -300, 0)
    link(particle_color, multiply, "A")
    link(emissive_scale, multiply, "B")
    connect_output(multiply, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    connect_output(particle_color, unreal.MaterialProperty.MP_OPACITY, "A")
    enable_usages(material, (unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES, unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES))
    compile_material(material, path)
    save_asset(material, path)
    return material, {"asset": path, "blend_mode": "BLEND_TRANSLUCENT", "shading_model": "MSM_UNLIT", "parameters": [EMISSIVE_PARAMETER_NAME],
                      "usages": ["MATUSAGE_NIAGARA_SPRITES", "MATUSAGE_NIAGARA_MESH_PARTICLES"]}


def build_mesh_master():
    path = asset_path(MESH_MASTER_NAME)
    material = create_master(MESH_MASTER_NAME, unreal.BlendMode.BLEND_OPAQUE, unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    particle_color = add_node(material, unreal.MaterialExpressionParticleColor, -600, 0)
    roughness = add_scalar_parameter(material, ROUGHNESS_PARAMETER_NAME, 0.5, -600, 200)
    connect_output(particle_color, unreal.MaterialProperty.MP_BASE_COLOR)
    connect_output(roughness, unreal.MaterialProperty.MP_ROUGHNESS)
    enable_usages(material, (unreal.MaterialUsage.MATUSAGE_NIAGARA_MESH_PARTICLES,))
    compile_material(material, path)
    save_asset(material, path)
    return material, {"asset": path, "blend_mode": "BLEND_OPAQUE", "shading_model": "MSM_DEFAULT_LIT", "parameters": [ROUGHNESS_PARAMETER_NAME],
                      "usages": ["MATUSAGE_NIAGARA_MESH_PARTICLES"]}


def build_instance(name, master, look_table):
    path = asset_path(name)
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError(f"재질 인스턴스 생성 실패: {path}")
    entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, path, master)
    save_asset(instance, path)
    return {"asset": path, "scalar_overrides": entry["Scalars"], "vector_overrides": entry["Vectors"]}


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
    target_paths = [asset_path(name) for name in (SPRITE_MASTER_NAME, MESH_MASTER_NAME, SPRITE_INSTANCE_NAME, MESH_INSTANCE_NAME)]
    existing_paths = [path for path in target_paths if unreal.EditorAssetLibrary.does_asset_exist(path)]
    if existing_paths:
        raise RuntimeError("기존 에셋 보존(덮어쓰지 않음, 삭제는 사용자 승인 뒤): " + ", ".join(existing_paths))
    unsaved_names = find_unsaved_package_names()
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다: " + ", ".join(unsaved_names))
    look_table = load_material_look_table()
    for name in (SPRITE_INSTANCE_NAME, MESH_INSTANCE_NAME):
        look_table.material(asset_path(name))

    print("단계: 스프라이트·메시 마스터 재질")
    sprite_master, sprite_summary = build_sprite_master()
    mesh_master, mesh_summary = build_mesh_master()
    print("단계: 재질 인스턴스")
    instances = [build_instance(SPRITE_INSTANCE_NAME, sprite_master, look_table), build_instance(MESH_INSTANCE_NAME, mesh_master, look_table)]
    report = {"status": "ok", "material_look_sha256": look_table.source_sha256(), "masters": [sprite_summary, mesh_summary], "instances": instances, "saved": True}
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

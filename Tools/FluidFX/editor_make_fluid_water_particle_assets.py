"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). W-1b 물 부유물·물보라용 에셋 4개를 만든다: 접촉 데이터 채널 NDC_TD_FluidWaterContact(C++ 라이브러리 TDFluidNiagaraModuleEditorLibrary.create_mass_input_data_channel, 변수 배치는 FX-5 질량 입력 채널과 같음)와 재질 인스턴스 3개. 먼저 편집기 재시작 뒤 모양 조각 look-fragment-w1b.json 병합(A0)과 editor_make_fluid_particle_material.py 의 마스터 2개가 있어야 한다.
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_water_particle_assets.py (PowerShell, L-repo-01). 덮어쓰기는 Saved/FluidFX/fluid_water_particle_assets_request.json 에 {"overwrite": true} 가 있을 때만 한다.
출력: /Game/FluidFX/Niagara/DataChannels/NDC_TD_FluidWaterContact, /Game/FluidFX/Materials/MI_TD_FluidWaterDebris_Default(부모 M_TD_FluidDrivenParticleMesh)·MI_TD_FluidWaterWhitewater_Default·MI_TD_FluidWaterSplash_Default(부모 M_TD_FluidDrivenParticle), Saved/FluidFX/niagara/water-particle-assets.json(단계별 결과·삭제 기록·sha256·material_look_sha256). 성공 여부는 출력 본문의 TD_WATER_PARTICLE_ASSETS PASS 줄로 판정한다(L-editor-73, run_in_editor 는 FAIL 이어도 SUCCESS 를 낼 수 있음).
상태: 미검증(2026-10-29 W-1b D 작성: py_compile 과 가짜 unreal 시험만, 에디터 실행 전). 채널 생성 함수는 성공이면 문자열, 실패면 None 을 돌려준다(L-editor-73). 재질 인스턴스 값은 apply_to_instance 가 다시 읽어 확인하고(L-editor-66) 부모 경로도 따로 다시 읽는다. 덮어쓰기는 참조 나이아가라 3개 → 인스턴스 3개 → 채널 순으로 지우고 디스크 잔존이 있으면 L-editor-10 경로(unload_packages → collect_garbage → os.remove → 레지스트리 스캔)로 정리하며 미저장 패키지가 있으면 시작하지 않는다.
"""
import gc
import hashlib
import importlib.util
import json
import os

import unreal

FLUID_ROOT = "/Game/FluidFX"
MATERIAL_DIR = f"{FLUID_ROOT}/Materials"
NIAGARA_DIR = f"{FLUID_ROOT}/Niagara"
NDC_PACKAGE = f"{NIAGARA_DIR}/DataChannels/NDC_TD_FluidWaterContact"
SPRITE_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidDrivenParticle"
MESH_MASTER_PATH = f"{MATERIAL_DIR}/M_TD_FluidDrivenParticleMesh"
INSTANCE_SPECS = (
    {"name": "MI_TD_FluidWaterDebris_Default", "parent": MESH_MASTER_PATH, "checked_scalar": "ParticleRoughness"},
    {"name": "MI_TD_FluidWaterWhitewater_Default", "parent": SPRITE_MASTER_PATH, "checked_scalar": "ParticleEmissive"},
    {"name": "MI_TD_FluidWaterSplash_Default", "parent": SPRITE_MASTER_PATH, "checked_scalar": "ParticleEmissive"},
)
REFERENCING_SYSTEMS = (
    f"{NIAGARA_DIR}/NS_TD_FluidWaterSplash",
    f"{NIAGARA_DIR}/NS_TD_FluidWaterDebris",
    f"{NIAGARA_DIR}/NS_TD_FluidWaterWhitewater",
)
INSTANCE_PACKAGES = tuple(f"{MATERIAL_DIR}/{spec['name']}" for spec in INSTANCE_SPECS)
CREATED_ASSETS = (NDC_PACKAGE,) + INSTANCE_PACKAGES
DELETE_ORDER = REFERENCING_SYSTEMS + INSTANCE_PACKAGES + (NDC_PACKAGE,)
MATERIAL_LOOK_MODULE_RELATIVE_PATH = ("Tools", "FluidFX", "fluid_material_look.py")
REQUEST_RELATIVE_PATH = ("Saved", "FluidFX", "fluid_water_particle_assets_request.json")
REPORT_RELATIVE_PATH = ("Saved", "FluidFX", "niagara", "water-particle-assets.json")
REQUIRED_UNREAL_NAMES = (
    "TDFluidNiagaraModuleEditorLibrary.create_mass_input_data_channel",
    "AssetToolsHelpers.get_asset_tools",
    "MaterialInstanceConstant",
    "MaterialInstanceConstantFactoryNew",
    "MaterialEditingLibrary.set_material_instance_parent",
    "MaterialEditingLibrary.update_material_instance",
    "MaterialEditingLibrary.get_material_instance_scalar_parameter_value",
    "LinearColor",
    "EditorAssetLibrary.load_asset",
    "EditorAssetLibrary.save_loaded_asset",
    "EditorAssetLibrary.delete_asset",
    "EditorAssetLibrary.does_asset_exist",
    "EditorLoadingAndSavingUtils.get_dirty_map_packages",
    "EditorLoadingAndSavingUtils.get_dirty_content_packages",
    "EditorLoadingAndSavingUtils.unload_packages",
    "SystemLibrary.collect_garbage",
    "SystemLibrary.get_project_directory",
    "AssetRegistryHelpers.get_asset_registry",
)


class TDWaterParticleAssetError(Exception):
    pass


def project_path(*parts):
    return os.path.join(unreal.SystemLibrary.get_project_directory(), *parts)


def object_path(package):
    return f"{package}.{package.rsplit('/', 1)[1]}"


def disk_path(package):
    return project_path("Content", *package[len("/Game/"):].split("/")) + ".uasset"


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


def find_dirty_packages():
    utilities = unreal.EditorLoadingAndSavingUtils
    return sorted({str(package.get_name()) for package in list(utilities.get_dirty_content_packages()) + list(utilities.get_dirty_map_packages())})


def load_material_look_table():
    module_path = project_path(*MATERIAL_LOOK_MODULE_RELATIVE_PATH)
    module_spec = importlib.util.spec_from_file_location("fluid_material_look", module_path)
    if module_spec is None or module_spec.loader is None:
        raise TDWaterParticleAssetError(f"모양 정본 모듈 없음: {module_path}")
    material_look = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(material_look)
    return material_look.TDMaterialLookTable.read_project(unreal.SystemLibrary.get_project_directory())


def delete_asset_fully(package, report):
    asset = unreal.EditorAssetLibrary.load_asset(object_path(package))
    deleted = bool(unreal.EditorAssetLibrary.delete_asset(package))
    entry = {"asset": package, "deleted": deleted, "disk_cleanup": False}
    report["deleted"].append(entry)
    if not deleted:
        raise TDWaterParticleAssetError(f"삭제 실패(참조가 남았을 수 있음): {package}")
    if not os.path.isfile(disk_path(package)):
        return
    if asset is not None:
        unreal.EditorLoadingAndSavingUtils.unload_packages([asset.get_outermost()])
    asset = None
    gc.collect()
    unreal.SystemLibrary.collect_garbage()
    try:
        os.remove(disk_path(package))
    except OSError as error:
        raise TDWaterParticleAssetError(f"디스크 잔존 삭제 실패(에디터를 멈추고 이 파일만 지운 뒤 재실행, L-editor-67): {disk_path(package)} {error}")
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([package.rsplit("/", 1)[0]], True)
    entry["disk_cleanup"] = True
    if unreal.EditorAssetLibrary.does_asset_exist(package):
        raise TDWaterParticleAssetError(f"디스크에서 지웠지만 메모리에 남음(에디터 재시작, L-editor-10): {package}")


def delete_existing_assets(report):
    for package in DELETE_ORDER:
        if unreal.EditorAssetLibrary.does_asset_exist(package):
            delete_asset_fully(package, report)


def unpack_result(result):
    if result is None:
        return False, "C++ 가 실패를 돌려줌(UE Python 은 실패하면 None 만 돌려줘 메시지를 잃는다, L-editor-73)"
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    return True, str(result)


def save_loaded_package(label, package):
    loaded = unreal.EditorAssetLibrary.load_asset(object_path(package))
    if loaded is None:
        raise TDWaterParticleAssetError(f"{label}: 만든 에셋을 불러올 수 없음 {package}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(loaded, only_if_is_dirty=False):
        raise TDWaterParticleAssetError(f"{label}: 저장 실패 {package}")
    return loaded


def create_data_channel(report):
    succeeded, message = unpack_result(unreal.TDFluidNiagaraModuleEditorLibrary.create_mass_input_data_channel(NDC_PACKAGE))
    entry = {"step": "NDC", "asset": NDC_PACKAGE, "ok": succeeded, "report": message}
    report["steps"].append(entry)
    if not succeeded:
        raise TDWaterParticleAssetError(f"NDC 생성 실패: {message}")
    save_loaded_package("NDC", NDC_PACKAGE)
    entry["saved"] = True


def require_parent_asset(parent_path):
    parent = unreal.EditorAssetLibrary.load_asset(object_path(parent_path))
    if parent is None:
        raise TDWaterParticleAssetError(f"부모 마스터 재질 없음(editor_make_fluid_particle_material.py 먼저): {parent_path}")
    return parent


def read_back_instance(spec, instance, entry, parent_path):
    parent = instance.get_editor_property("parent")
    actual_parent = str(parent.get_path_name()).split(".", 1)[0] if parent is not None else ""
    if actual_parent != parent_path:
        raise TDWaterParticleAssetError(f"{spec['name']}: 부모 다시 읽기 불일치 {actual_parent} ≠ {parent_path}")
    scalar_name = spec["checked_scalar"]
    if scalar_name not in entry["Scalars"]:
        return {"parent": actual_parent, "checked_scalar": scalar_name, "value": None}
    value = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(instance, scalar_name)
    if abs(value - entry["Scalars"][scalar_name]) > 1e-4:
        raise TDWaterParticleAssetError(f"{spec['name']}: {scalar_name} 다시 읽기 불일치 {value} ≠ {entry['Scalars'][scalar_name]}")
    return {"parent": actual_parent, "checked_scalar": scalar_name, "value": value}


def create_instance(report, look_table, spec):
    package = f"{MATERIAL_DIR}/{spec['name']}"
    parent = require_parent_asset(spec["parent"])
    instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        spec["name"], MATERIAL_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise TDWaterParticleAssetError(f"재질 인스턴스 생성 실패: {package}")
    entry = look_table.apply_to_instance(unreal.MaterialEditingLibrary, unreal.LinearColor, instance, package, parent)
    step = {"step": "MI", "asset": package, "ok": True, "read_back": read_back_instance(spec, instance, entry, spec["parent"]),
            "scalar_overrides": entry["Scalars"], "vector_overrides": entry["Vectors"]}
    report["steps"].append(step)
    if not unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False):
        raise TDWaterParticleAssetError(f"재질 인스턴스 저장 실패: {package}")
    step["saved"] = True


def require_look_entries(look_table):
    for spec in INSTANCE_SPECS:
        try:
            look_table.material(f"{MATERIAL_DIR}/{spec['name']}", spec["parent"])
        except RuntimeError as error:
            raise TDWaterParticleAssetError(f"모양 정본에 조각 병합 필요(A0, look-fragment-w1b.json): {error}")


def sha256_of(package):
    path = disk_path(package)
    if not os.path.isfile(path):
        return ""
    with open(path, "rb") as asset_file:
        return hashlib.sha256(asset_file.read()).hexdigest()


def write_report(report):
    report_path = project_path(*REPORT_RELATIVE_PATH)
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=1, default=str)
        report_file.write("\n")
    return report_path


def build_assets(report):
    dirty = find_dirty_packages()
    if dirty:
        raise TDWaterParticleAssetError("미저장 패키지가 있음(저장 또는 되돌린 뒤 재실행): " + ", ".join(dirty[:10]))
    existing = [package for package in CREATED_ASSETS if unreal.EditorAssetLibrary.does_asset_exist(package)]
    if existing and not report["overwrite"]:
        raise TDWaterParticleAssetError(f"에셋이 이미 있음(덮어쓰기는 {project_path(*REQUEST_RELATIVE_PATH)} 에 {{\"overwrite\": true}}): " + ", ".join(existing))
    look_table = load_material_look_table()
    report["material_look_sha256"] = look_table.source_sha256()
    require_look_entries(look_table)
    for spec in INSTANCE_SPECS:
        require_parent_asset(spec["parent"])
    if report["overwrite"]:
        delete_existing_assets(report)
    create_data_channel(report)
    for spec in INSTANCE_SPECS:
        create_instance(report, look_table, spec)
    report["sha256"] = {package: sha256_of(package) for package in CREATED_ASSETS}
    report["ok"] = all(report["sha256"].values())


def run():
    report = {"overwrite": False, "deleted": [], "steps": [], "sha256": {}, "material_look_sha256": "", "ok": False, "error": "",
              "pending_checks": ["editor_make_fluid_driven_niagara.py --spec NS_TD_FluidWaterSplash.json --probe → 0a 모듈 입력 탐침(bExecute)"]}
    try:
        missing_names = find_missing_unreal_names()
        if missing_names:
            raise TDWaterParticleAssetError("언리얼 Python 이름 없음(재빌드와 에디터 재시작 필요): " + ", ".join(missing_names))
        report["overwrite"] = read_overwrite_request()
        build_assets(report)
    except TDWaterParticleAssetError as error:
        report["error"] = str(error)
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    report_path = write_report(report)
    print("TD_WATER_PARTICLE_ASSETS", "PASS" if report["ok"] else "FAIL", report["error"])
    print("보고: " + report_path)


if __name__ == "__main__":
    run()

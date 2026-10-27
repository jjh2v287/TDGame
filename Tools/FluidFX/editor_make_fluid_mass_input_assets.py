"""실행 환경: TDGame 언리얼 에디터 안(Python 원격 실행). TDGameEditor 에 UTDFluidNiagaraModuleEditorLibrary(FX-5 묶음 D)가 빌드돼 있어야 한다(IB2 통합 뒤 python Tools/ue_editor.py restart, L-build-01).
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_make_fluid_mass_input_assets.py (PowerShell, L-repo-01)
출력: /Game/FluidFX/Niagara/DataChannels/NDC_TD_FluidMassInput(쿠킹 폴더 분리, R5-M4), /Game/FluidFX/Niagara/Modules/NM_TD_FluidMassSplat·NM_TD_FluidNdcAppendPoint, Saved/FluidFX/niagara/mass-input-assets.json(C++ 보고·저장·sha256·삭제 기록). 모듈 입력 스키마 재읽기는 이 스크립트 뒤 python -B Tools/FluidFX/editor_make_fluid_driven_niagara.py --spec Tools/FluidFX/niagara_specs/NS_TD_FluidFlamethrower.json --probe 의 0a(mass_module_inputs_ok, R5-S14)가 맡는다. 성공 뒤 부모가 Kinds.MassInputBench.TDFluidMassInputSettings.DataChannel 값을 넣는다(R5-M4, 교차 A4-b).
상태: 2026-10-27 실행 — NDC 생성·저장 성공, 스플랫 모듈은 C++ 핀 이름 불일치(DI 'Execute' ↔ 라이브러리 'bExecute')로 실패. 보고의 report 는 C++ OutReport(UE Python 은 bool+출력 인자 함수가 성공이면 문자열, 실패면 None 을 돌려준다). 덮어쓰기는 Saved/FluidFX/fluid_mass_input_assets_request.json {"overwrite": true} 일 때만 하며 참조 NS 5개 → 모듈 2 → NDC 순으로 지운 뒤(R5-S11) 디스크 잔존이 있으면 중단한다(L-editor-67·L-editor-10). 미저장 패키지가 있으면 시작하지 않는다.
"""
import hashlib
import json
import pathlib

import unreal

NDC_PACKAGE = "/Game/FluidFX/Niagara/DataChannels/NDC_TD_FluidMassInput"
SPLAT_MODULE_PACKAGE = "/Game/FluidFX/Niagara/Modules/NM_TD_FluidMassSplat"
APPEND_MODULE_PACKAGE = "/Game/FluidFX/Niagara/Modules/NM_TD_FluidNdcAppendPoint"
REFERENCING_SYSTEMS = (
    "/Game/FluidFX/Niagara/NS_TD_FluidMassBench",
    "/Game/FluidFX/Niagara/NS_TD_FluidMassBench1k",
    "/Game/FluidFX/Niagara/NS_TD_FluidMassNdcBench",
    "/Game/FluidFX/Niagara/NS_TD_FluidFlamethrower",
    "/Game/FluidFX/Niagara/NS_TD_FluidAvalanche",
)
CREATED_ASSETS = (NDC_PACKAGE, SPLAT_MODULE_PACKAGE, APPEND_MODULE_PACKAGE)
DELETE_ORDER = REFERENCING_SYSTEMS + (SPLAT_MODULE_PACKAGE, APPEND_MODULE_PACKAGE, NDC_PACKAGE)
PROJECT_DIR = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
REQUEST_PATH = PROJECT_DIR / "Saved" / "FluidFX" / "fluid_mass_input_assets_request.json"
REPORT_PATH = PROJECT_DIR / "Saved" / "FluidFX" / "niagara" / "mass-input-assets.json"


class TDMassAssetError(Exception):
    pass


def object_path(package):
    return f"{package}.{package.rsplit('/', 1)[1]}"


def disk_path(package):
    return PROJECT_DIR / "Content" / (package[len("/Game/"):] + ".uasset")


def read_overwrite_request():
    if not REQUEST_PATH.is_file():
        return False
    return json.loads(REQUEST_PATH.read_text(encoding="utf-8")).get("overwrite") is True


def find_dirty_packages():
    utilities = unreal.EditorLoadingAndSavingUtils
    return [str(package.get_name()) for package in list(utilities.get_dirty_content_packages()) + list(utilities.get_dirty_map_packages())]


def delete_existing_assets(report):
    for package in DELETE_ORDER:
        if not unreal.EditorAssetLibrary.does_asset_exist(package):
            continue
        deleted = unreal.EditorAssetLibrary.delete_asset(package)
        report["deleted"].append({"asset": package, "deleted": bool(deleted)})
        if not deleted:
            raise TDMassAssetError(f"삭제 실패(참조가 남았을 수 있음): {package}")
    left_on_disk = [str(disk_path(package)) for package in DELETE_ORDER if disk_path(package).is_file()]
    if left_on_disk:
        raise TDMassAssetError("삭제 뒤 디스크 잔존(L-editor-10 경로로 정리 후 재실행): " + ", ".join(left_on_disk))


def unpack_result(result):
    if result is None:
        return False, "C++ 가 false 를 돌려줌(UE Python 은 bool 반환 + 출력 인자 함수가 false 면 None 만 돌려줘 OutReport 를 잃는다)"
    if isinstance(result, tuple):
        return bool(result[0]), str(result[1]) if len(result) > 1 else ""
    return True, str(result)


def create_asset(report, label, action, package):
    succeeded, message = unpack_result(action())
    entry = {"step": label, "asset": package, "ok": succeeded, "report": message}
    report["steps"].append(entry)
    if not succeeded:
        raise TDMassAssetError(f"{label}: {message}")
    loaded = unreal.EditorAssetLibrary.load_asset(object_path(package))
    entry["saved"] = bool(loaded) and bool(unreal.EditorAssetLibrary.save_loaded_asset(loaded, only_if_is_dirty=False))
    if not entry["saved"]:
        raise TDMassAssetError(f"{label}: 저장 실패 {package}")


def sha256_of(package):
    path = disk_path(package)
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else ""


def build_assets(report):
    library = unreal.TDFluidNiagaraModuleEditorLibrary
    create_asset(report, "NDC", lambda: library.create_mass_input_data_channel(NDC_PACKAGE), NDC_PACKAGE)
    create_asset(report, "module_splat", lambda: library.create_mass_splat_module_script(SPLAT_MODULE_PACKAGE), SPLAT_MODULE_PACKAGE)
    create_asset(report, "module_ndc_append",
                 lambda: library.create_data_channel_append_module_script(APPEND_MODULE_PACKAGE, object_path(NDC_PACKAGE)), APPEND_MODULE_PACKAGE)


def run():
    report = {"overwrite": read_overwrite_request(), "deleted": [], "steps": [], "sha256": {}, "ok": False, "error": "",
              "pending_checks": ["editor_make_fluid_driven_niagara.py --spec Tools/FluidFX/niagara_specs/NS_TD_FluidFlamethrower.json --probe → mass_module_inputs_ok"]}
    try:
        dirty = find_dirty_packages()
        if dirty:
            raise TDMassAssetError("미저장 패키지가 있음(저장 또는 되돌린 뒤 재실행): " + ", ".join(dirty[:10]))
        existing = [package for package in CREATED_ASSETS if unreal.EditorAssetLibrary.does_asset_exist(package)]
        if existing and not report["overwrite"]:
            raise TDMassAssetError(f"에셋이 이미 있음(덮어쓰기는 {REQUEST_PATH} 에 {{\"overwrite\": true}}): " + ", ".join(existing))
        if report["overwrite"]:
            delete_existing_assets(report)
        build_assets(report)
        report["sha256"] = {package: sha256_of(package) for package in CREATED_ASSETS}
        report["ok"] = all(report["sha256"].values())
    except TDMassAssetError as error:
        report["error"] = str(error)
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print("TD_MASS_INPUT_ASSETS", "PASS" if report["ok"] else "FAIL", report["error"])
    print("보고: " + str(REPORT_PATH))


run()

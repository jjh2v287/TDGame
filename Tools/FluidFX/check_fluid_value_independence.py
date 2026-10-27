"""실행 환경: 시스템 Python(에디터 밖), 묶음 A(-TDFluidTuningFile= 명령줄) 통합 뒤의 빌드된 작업 트리. 빌드 창 규칙은 build_and_test.py 와 같다(본 트리에서 --acquire-hold 뒤 실행, 끝나면 --release-hold). 정본 JSON 은 읽기만 한다
실행 명령: python Tools/FluidFX/check_fluid_value_independence.py [--scale 1.01] [--filter TDGame.FluidFX] [--rhi null] [--label w0-value-independence] [--holder 이름] [--allow 시험경로 ...] [--dry-run] [--no-confirm-load]
출력: Saved/FluidFX/value_independence/TDFluidPresetTuning.perturbed.json(Kinds·Groups 층의 float·double 필드 ×scale, Clamp 밖이면 ×(2-scale), 그래도 밖이면 건너뜀), Saved/AgentOps/<YYYYMMDD>/<label>-tests.log·<label>-value-independence.json. 종료 0 허용 목록(A.3) 밖 실패 0 / 1 밖 실패 있음 / 2 실행 불완전·충돌·결과 없음 / 3 사본 로드 확인 실패 / 4 표 로드 오류 / 5 정본이 바뀜 / 6 입력 오류
상태: 2026-10-27 구조체 검색에 TDFluidVolumeDriverTypes.h 추가(건너뜀 0), 2026-10-25 파동 0 묶음 D 작성, py_compile·--dry-run·묶음 A 로그 형식(명령줄 경로 Display 줄·"Preset tuning files … failed to load"·"… (loaded), material look …: N errors") 정규식 대조 확인(BuildersMatchTuningFile 은 활성 경로를 읽으므로 허용 목록에 넣지 않음), 헤드리스 판정은 묶음 A·C 통합 빌드 뒤 미검증
"""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
from datetime import datetime

ROOT_DEFAULT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TUNING_RELATIVE_PATH = os.path.join("Content", "FluidFX", "PresetData", "TDFluidPresetTuning.json")
PERTURBED_RELATIVE_PATH = os.path.join("Saved", "FluidFX", "value_independence", "TDFluidPresetTuning.perturbed.json")
LAYER_SECTIONS = ("Kinds", "Groups")
GROUP_MEMBER_KEY = "Kinds"
PERTURBED_TYPES = ("float", "double")
EXTRA_HEADER_PATHS = (os.path.join("Source", "TDGame", "FluidFX", "TDFluidVolumeDriverTypes.h"),)
TABLE_LOAD_ERROR = re.compile(r"Preset tuning files? .*failed to load|Preset tuning .*TDFluidPresetTuning\.perturbed\.json.*: [1-9]\d* errors")
THRESHOLD_PROPERTY_TESTS = (
    "TDGame.FluidFX.SolverRegression.BorderBandRetainsFootprint",
    "TDGame.FluidFX.SolverRegression.BrushFrameRateInvariance",
    "TDGame.FluidFX.SolverRegression.DisplayPressureIsResolutionIndependent",
    "TDGame.FluidFX.SolverRegression.GridEdgeObstacleNormalIsTangential",
    "TDGame.FluidFX.SolverRegression.MovingMudStampDisplacesSurface",
    "TDGame.FluidFX.SolverRegression.MovingStampLeavesNoPressureAfterSettling",
    "TDGame.FluidFX.SolverRegression.NonFiniteStampIsDiscarded",
    "TDGame.FluidFX.SolverRegression.SampleReportsVelocityCmPerSec",
    "TDGame.FluidFX.SolverRegression.StampVelocityIsCappedAtMaxSpeed",
    "TDGame.FluidFX.SolverRegression.StationaryStateHasNoPressureResidue",
    "TDGame.FluidFX.SolverRegression.WaterBorderBandKeepsDensityAndFadesAlpha",
    "TDGame.FluidFX.SolverBounds.PortalDriftIsInward",
    "TDGame.FluidFX.SolverBounds.VortexSpinsAndInverts",
    "TDGame.FluidFX.Explosion.HotCoreAreaTimeline",
    "TDGame.FluidFX.Explosion.UndergroundLossRatio",
    "TDGame.FluidFX.Fire.SubstepEmissionContinuous",
    "TDGame.FluidFX.Fire.TorchCoreHeatPinnedToAnchor",
    "TDGame.FluidFX.Fragment.ExplosionStampBinBudget",
    "TDGame.FluidFX.Perf.StepCostByResolution",
    "TDGame.FluidFX.SimplePainter.FootprintTrailAndFade",
)


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def file_sha256(path):
    with open(path, "rb") as source_file:
        return hashlib.sha256(source_file.read()).hexdigest()


def is_outside_clamp(value, field):
    if field["clamp_min"] is not None and value < field["clamp_min"]:
        return True
    return field["clamp_max"] is not None and value > field["clamp_max"]


def perturb_layer(layer, fields, scale, path, changes, skips):
    for field_name in list(layer):
        value = layer[field_name]
        field = fields.get(field_name)
        if field is None or field["type"] not in PERTURBED_TYPES:
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float)) or value == 0:
            continue
        candidate = value * scale
        if is_outside_clamp(candidate, field):
            candidate = value * (2.0 - scale)
        if is_outside_clamp(candidate, field):
            skips.append({"path": f"{path}.{field_name}", "reason": "clamp", "value": value})
            continue
        layer[field_name] = candidate
        changes.append({"path": f"{path}.{field_name}", "from": value, "to": candidate})


def read_struct_fields(root, struct_name, snapshot_tool):
    try:
        return snapshot_tool.read_struct_fields(root, struct_name)[0]
    except snapshot_tool.SnapshotError:
        for header_path in EXTRA_HEADER_PATHS:
            body = snapshot_tool.find_struct_body(snapshot_tool.read_text(os.path.join(root, header_path)), struct_name)
            if body is not None:
                return snapshot_tool.parse_struct_fields(body)
        raise


def perturb_table(root, table, scale, snapshot_tool):
    perturbed = copy.deepcopy(table)
    changes = []
    skips = []
    field_cache = {}
    for section in LAYER_SECTIONS:
        for owner, layers in perturbed.get(section, {}).items():
            if not isinstance(layers, dict):
                continue
            for struct_name, layer in layers.items():
                if section == "Groups" and struct_name == GROUP_MEMBER_KEY:
                    continue
                if not isinstance(layer, dict):
                    continue
                if struct_name not in field_cache:
                    try:
                        field_cache[struct_name] = read_struct_fields(root, struct_name, snapshot_tool)
                    except snapshot_tool.SnapshotError as error:
                        field_cache[struct_name] = None
                        skips.append({"path": struct_name, "reason": str(error)})
                if field_cache[struct_name] is None:
                    continue
                perturb_layer(layer, field_cache[struct_name], scale, f"{section}.{owner}.{struct_name}", changes, skips)
    return perturbed, changes, skips


def run_headless(build_tool, filters, rhi_mode, log_path, perturbed_path):
    command = [
        build_tool.EDITOR_CMD_EXE, build_tool.UPROJECT,
        "-ExecCmds=Automation RunTests " + "+".join(filters),
        "-TestExit=Automation Test Queue Empty",
        "-unattended", build_tool.RHI_ARGUMENTS[rhi_mode], "-NoSplash", "-NoSound",
        f"-abslog={log_path}",
        f"-TDFluidTuningFile={perturbed_path}",
    ]
    completed_process = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run_summary = build_tool.parse_test_log(log_path)
    run_summary["engine_exit_code"] = completed_process.returncode
    return run_summary


def read_log_text(log_path):
    if not os.path.exists(log_path):
        return ""
    with open(log_path, encoding="utf-8", errors="replace") as log_file:
        return log_file.read()


def judge(run_summary, log_text, perturbed_path, allowed, confirm_load):
    results = run_summary["results"]
    failed = sorted(entry["path"] for entry in results if entry["result"] != "Success")
    outside = [path for path in failed if path not in allowed]
    found_count = run_summary["found_count"]
    is_incomplete = not results or found_count is None or len(results) < found_count
    has_crashed = bool(run_summary["crash_lines"]) or run_summary["engine_exit_code"] != 0
    normalized_log = log_text.replace("\\", "/").lower()
    is_load_confirmed = os.path.basename(perturbed_path).lower() in normalized_log
    table_errors = [line.strip()[:300] for line in log_text.splitlines() if TABLE_LOAD_ERROR.search(line)][:10]
    verdict = {
        "failed": failed,
        "outside_allowlist": outside,
        "allowlisted_failures": [path for path in failed if path in allowed],
        "result_count": len(results),
        "found_count": found_count,
        "incomplete": is_incomplete,
        "crashed": has_crashed,
        "load_confirmed": is_load_confirmed,
        "table_errors": table_errors,
    }
    if table_errors:
        return verdict, 4
    if confirm_load and not is_load_confirmed:
        return verdict, 3
    if is_incomplete or has_crashed:
        return verdict, 2
    return verdict, 1 if outside else 0


def main():
    sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description="흔든 조절 JSON 사본으로 헤드리스 시험을 돌려 실패가 임계 성질 허용 목록 안인지 본다")
    parser.add_argument("--root", default=ROOT_DEFAULT)
    parser.add_argument("--scale", type=float, default=1.01)
    parser.add_argument("--filter", action="append", default=[])
    parser.add_argument("--rhi", choices=("null", "offscreen"), default="null")
    parser.add_argument("--label", default="w0-value-independence")
    parser.add_argument("--holder", default=None)
    parser.add_argument("--wait-seconds", type=int, default=1800)
    parser.add_argument("--allow", action="append", default=[], help="허용 목록에 더할 시험 경로")
    parser.add_argument("--dry-run", action="store_true", help="흔든 사본과 변경 목록만 만들고 시험은 돌리지 않는다")
    parser.add_argument("--no-confirm-load", action="store_true", help="로그에 사본 파일 이름이 없어도 판정한다")
    args = parser.parse_args()

    if args.scale <= 0.0 or args.scale == 1.0 or args.scale >= 2.0:
        print("오류: --scale 은 0~2 사이의 1 이 아닌 값이어야 한다")
        return 6
    root = os.path.abspath(args.root)
    tuning_path = os.path.join(root, TUNING_RELATIVE_PATH)
    perturbed_path = os.path.join(root, PERTURBED_RELATIVE_PATH)
    snapshot_tool = load_module("make_fluid_golden_snapshots", os.path.join(root, "Tools", "FluidFX", "make_fluid_golden_snapshots.py"))
    try:
        canonical_sha256 = file_sha256(tuning_path)
        with open(tuning_path, encoding="utf-8") as tuning_file:
            table = json.load(tuning_file)
    except (OSError, ValueError) as error:
        print(f"오류: 정본을 읽지 못함 {tuning_path}: {error}")
        return 6

    perturbed, changes, skips = perturb_table(root, table, args.scale, snapshot_tool)
    if not changes:
        print("오류: 흔들 수 있는 필드가 없다")
        return 6
    os.makedirs(os.path.dirname(perturbed_path), exist_ok=True)
    with open(perturbed_path, "w", encoding="utf-8", newline="\n") as perturbed_file:
        json.dump(perturbed, perturbed_file, ensure_ascii=False, indent=1)
        perturbed_file.write("\n")

    output_dir = os.path.join(root, "Saved", "AgentOps", datetime.now().strftime("%Y%m%d"))
    os.makedirs(output_dir, exist_ok=True)
    report_path = os.path.join(output_dir, f"{args.label}-value-independence.json")
    report = {
        "canonical": tuning_path,
        "canonical_sha256": canonical_sha256,
        "perturbed": perturbed_path,
        "scale": args.scale,
        "changed_field_count": len(changes),
        "changes": changes,
        "skips": skips,
    }
    print(f"흔든 사본: {perturbed_path} (필드 {len(changes)}개 변경, {len(skips)}개 건너뜀)")
    if args.dry_run:
        with open(report_path, "w", encoding="utf-8") as report_file:
            json.dump(report, report_file, ensure_ascii=False, indent=1)
        print(f"보고: {report_path} (dry-run)")
        return 0

    build_tool = load_module("build_and_test", os.path.join(root, "Tools", "build_and_test.py"))
    if not build_tool.wait_for_quiet_machine(args.wait_seconds, args.holder):
        return 2
    filters = args.filter or ["TDGame.FluidFX"]
    log_path = os.path.join(output_dir, f"{args.label}-tests.log")
    run_summary = run_headless(build_tool, filters, args.rhi, log_path, perturbed_path.replace("\\", "/"))
    allowed = set(THRESHOLD_PROPERTY_TESTS) | set(args.allow)
    verdict, exit_code = judge(run_summary, read_log_text(log_path), perturbed_path, allowed, not args.no_confirm_load)
    if file_sha256(tuning_path) != canonical_sha256:
        exit_code = 5
    report.update({"filters": filters, "rhi": args.rhi, "log": log_path, "verdict": verdict, "exit_code": exit_code, "results": run_summary["results"]})
    with open(report_path, "w", encoding="utf-8") as report_file:
        json.dump(report, report_file, ensure_ascii=False, indent=1)

    print(f"시험 {verdict['result_count']}/{verdict['found_count']} 완료, 실패 {len(verdict['failed'])}(허용 {len(verdict['allowlisted_failures'])}, 밖 {len(verdict['outside_allowlist'])}), 사본 로드 확인 {verdict['load_confirmed']} -> {report_path}")
    for path in verdict["outside_allowlist"]:
        print("  밖 실패", path)
    for line in verdict["table_errors"]:
        print("  표 오류", line)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())

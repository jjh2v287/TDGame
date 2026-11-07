"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 사본(PresetData·스테이징·cook/frames JSON·게임 로그)에서 fxp_audit.py package 를 돌려 분기 5종(실행 전 중단·쿠킹 실패·기동 결과 없음·첫 읽기 확인·첫 읽기 실패 ①~④)과 변형(성공 줄의 ExitCode=1·스테이징 파일 없음 근거 포함), 같은 바이트, 절대 경로·로그 시각 제거, 종료 2(인수 오류, 출력 없음)를 본다.
상태: 2026-11-07 FX-P X5a(B5), fix1 빌더 B가 2사례 추가(변이 package-success-any-exitcode·package-staged-missing-ok).
"""
import hashlib
from pathlib import Path

from check_fxp_fixtures import FixtureLog, run_audit, write_json, write_text

STAGED = "Saved/StagedBuilds/Windows"
PRESET = "Content/FluidFX/PresetData"
TUNING = "TDFluidPresetTuning.json"
LOOK = "TDFluidMaterialLook.json"
COOK = "Saved/AgentOps/cook_fxp-pkg-min.json"
FRAMES = "Saved/AgentOps/frames_fxp-pkg-min.json"
GAME_LOG = f"{STAGED}/TDGame/Saved/Logs/frames_fxp-pkg-min.log"
MANIFEST = f"{STAGED}/Manifest_NonUFSFiles_Win64.txt"
HOME_PATH = "C:\\Users\\fixture\\AppData\\Local\\UnrealEngine\\ErrorLog.txt"
STAMP = "[2026.11.07-10.00.00:000][  0]"
LOADED_TEXT = ("LogTDFluidPresetTuning: Preset tuning ../../../TDGame/Content/FluidFX/PresetData/TDFluidPresetTuning.json (loaded), "
               "material look ../../../TDGame/Content/FluidFX/PresetData/TDFluidMaterialLook.json (loaded): 0 errors, generation 1")
GPU = "미검증(전용 로그 줄 없음)"
COOK_STALE = "미검증(쿠킹 실패: 결과 JSON sha가 실행 전과 같음)"
LAUNCH_MISSING = "미검증(기동 결과 없음)"


def manifest_text(names):
    lines = ["TDGame/Binaries/Win64/TDGame.exe\t2026-11-07T01:00:00.000Z"]
    lines += [f"TDGame/{PRESET}/{name}\t2026-11-07T01:00:00.000Z" for name in names]
    return "\n".join(lines) + "\n"


def game_log(loaded=LOADED_TEXT, extra=()):
    lines = ["Log file open, 11/07/26 10:00:00", f"{STAMP}LogModuleManager: Display: TDFluidGpu loaded", f"{STAMP}{loaded}", *extra,
             f"{STAMP}LogShaderCompilers: Display: /TDFluidGpu/Private/Advect.usf compiled", f"{STAMP}LogExit: Exiting."]
    return "\ufeff" + "\n".join(lines) + "\n"


def world_files(root):
    return {
        f"{PRESET}/{TUNING}": '{"tuning": 1}\n',
        f"{PRESET}/{LOOK}": '{"look": 1}\n',
        f"{STAGED}/TDGame/{PRESET}/{TUNING}": '{"tuning": 1}\n',
        f"{STAGED}/TDGame/{PRESET}/{LOOK}": '{"look": 1}\n',
        MANIFEST: manifest_text((TUNING, LOOK)),
        COOK: {"label": "fxp-pkg-min", "map": "/Game/Level/LV-Cambat", "exit_code": 0, "seconds": 120, "log": str(root / "Saved/Logs/cook_fxp-pkg-min.log"),
               "error_count": 1, "warning_count": 1, "success_line": "AutomationTool exiting with ExitCode=0 (Success)",
               "first_problems": [f"AutomationTool encountered an error before a log file was set, see '{HOME_PATH}' for more details",
                                  f"{STAMP}LogShaderCompilers: Error: {root}\\Source\\TDFluidGpu\\Shaders\\Private\\Advect.usf failed"]},
        FRAMES: {"label": "fxp-pkg-min", "map": "/Game/Level/LV-Cambat", "exit_code": 777003, "csv": str(root / f"{STAGED}/TDGame/Saved/Profiling/CSV/p.csv"),
                 "log": str(root / GAME_LOG), "crash_lines": []},
        GAME_LOG: game_log(),
    }


def make_world(root, change=None):
    root.mkdir(parents=True, exist_ok=True)
    files = world_files(root)
    if change:
        change(files, root)
    for relative, value in files.items():
        if value is None:
            continue
        if isinstance(value, str):
            write_text(root, relative, value)
        else:
            write_json(root, relative, value)
    return root


def file_sha(root, relative):
    return hashlib.sha256((Path(root) / relative).read_bytes()).hexdigest()


def run_arguments(cook_before="none", frames_before="none", tag="min", cook=COOK, frames=FRAMES, staged=STAGED):
    return ["--tag", tag, "--cook-report", cook, "--cook-before-sha", cook_before, "--frames-report", frames, "--frames-before-sha", frames_before,
            "--staged-root", staged]


def statuses(document):
    return [check["status"] for check in document["checks"]] if document else None


def check_of(document, check_id):
    return next((check for check in document["checks"] if check["id"] == check_id), {}) if document else {}


def run_case(root, name, change=None, arguments=None):
    world = make_world(root / name, change)
    return world, run_audit(world, "package", name, arguments if arguments is not None else run_arguments())


def check_confirmed(log, root):
    world, first = run_case(root, "confirmed")
    second = run_audit(world, "package", "confirmed-2", run_arguments())
    document = first.document or {}
    text = first.json_bytes.decode("utf-8") if first.json_bytes else ""
    log.check("첫 읽기 확인 종료 0·verdict·분기", first.code == 0 and document.get("verdict") == "첫 읽기 확인" and document.get("branch") == "confirmed",
              f"종료 {first.code} {document.get('verdict')} {first.stderr[-300:]}")
    log.check("첫 읽기 확인 ①~④ 통과·문제 0", statuses(document) == ["통과"] * 4 and document.get("problems") == [], f"{statuses(document)} {document.get('problems')}")
    log.check("두 번 실행 같은 바이트(json·md)", first.json_bytes is not None and first.json_bytes == second.json_bytes and first.md_bytes == second.md_bytes, "바이트 다름")
    records = document.get("records") or {}
    log.check("기록만: TDFluidGpu 2줄·GPU 솔버·셰이더 매핑 미검증", records == {"tdfluidgpu_line_count": 2, "gpu_solver_first_launch": GPU, "shader_mapping": GPU}, f"{records}")
    launch = document.get("launch") or {}
    log.check("기동 exit_code 777003 정상 기록·로그 루트 상대", launch.get("exit_code") == 777003 and launch.get("exit_code_is_expected") is True and launch.get("log") == GAME_LOG,
              f"{launch}")
    error_lines = (document.get("cook") or {}).get("error_lines")
    log.check("절대 경로·로그 시각 없음(<절대 경로> 치환·루트 제거)", "C:\\\\Users" not in text and "[2026.11.07" not in text
              and str(world).replace("\\", "\\\\").lower() not in text.lower()
              and error_lines == ["AutomationTool encountered an error before a log file was set, see '<절대 경로>' for more details",
                                  "LogShaderCompilers: Error: Source\\TDFluidGpu\\Shaders\\Private\\Advect.usf failed"], f"{error_lines}")
    inputs = document.get("inputs") or {}
    log.check("inputs 루트 상대·로그·스테이징·원본 포함", all(":" not in key for key in inputs) and {COOK, FRAMES, GAME_LOG, MANIFEST, f"{PRESET}/{TUNING}"} <= set(inputs),
              f"{sorted(inputs)}")
    log.check("md 끝 SUMMARY", first.markdown.rstrip().endswith("SUMMARY tag=min branch=confirmed pass=4 fail=0 unevaluated=0 problems=0"), first.markdown[-200:])

    def build_successful(files, _root):
        files[COOK]["success_line"] = "BUILD SUCCESSFUL"
    _, run = run_case(root, "confirmed-build-successful", build_successful)
    log.check("success_line 'BUILD SUCCESSFUL'도 쿠킹 성공", run.code == 0 and (run.document or {}).get("verdict") == "첫 읽기 확인", f"종료 {run.code} {(run.document or {}).get('verdict')}")


def cook_change(**fields):
    def change(files, _root):
        files[COOK].update(fields)
    return change


def drop(relative):
    def change(files, _root):
        files[relative] = None
    return change


def replace(relative, value):
    def change(files, _root):
        files[relative] = value
    return change


def check_cook_failed(log, root):
    world = make_world(root / "cook-stale")
    stale = run_audit(world, "package", "cook-stale", run_arguments(cook_before=file_sha(world, COOK)))
    document = stale.document or {}
    log.check("쿠킹 실패: cook JSON sha 같음 → 미검증·①~④ 미평가·기동 평가 안 함",
              stale.code == 0 and document.get("verdict") == COOK_STALE and statuses(document) == ["미평가"] * 4 and (document.get("launch") or {}).get("evaluated") is False,
              f"종료 {stale.code} {document.get('verdict')} {statuses(document)}")
    cases = (("cook-missing", drop(COOK), "미검증(쿠킹 실패: 결과 JSON 없음)"),
             ("cook-exit", cook_change(exit_code=1, success_line="BUILD SUCCESSFUL"), "미검증(쿠킹 실패: exit_code 1)"),
             ("cook-no-success", cook_change(success_line=None), "미검증(쿠킹 실패: 성공 줄 없음)"),
             ("cook-exitcode-1-line", cook_change(success_line="AutomationTool exiting with ExitCode=1 (Error_Unknown)"), "미검증(쿠킹 실패: 성공 줄 없음)"),
             ("cook-timeout", cook_change(exit_code="timeout", success_line=None), "미검증(쿠킹 실패: exit_code timeout·성공 줄 없음)"),
             ("cook-broken", replace(COOK, "{not json\n"), "미검증(쿠킹 실패: 결과 JSON 형식 오류)"))
    for name, change, expected in cases:
        _, run = run_case(root, name, change)
        verdict = (run.document or {}).get("verdict")
        log.check(f"쿠킹 실패 {name} → {expected}", run.code == 0 and verdict == expected and run.json_bytes is not None, f"종료 {run.code} {verdict} {run.stderr[-200:]}")


def check_launch_missing(log, root):
    world = make_world(root / "frames-stale")
    stale = run_audit(world, "package", "frames-stale", run_arguments(frames_before=file_sha(world, FRAMES)))
    document = stale.document or {}
    log.check("기동 결과 없음: frames JSON sha 같음 → ①② 평가·③④ 미평가",
              stale.code == 0 and document.get("verdict") == LAUNCH_MISSING and statuses(document) == ["통과", "통과", "미평가", "미평가"]
              and (document.get("launch") or {}).get("missing_reason") == "결과 JSON sha가 실행 전과 같음", f"종료 {stale.code} {document.get('verdict')} {statuses(document)}")

    def log_null(files, _root):
        files[FRAMES]["log"] = None
    cases = (("frames-log-null", log_null, "log null"), ("frames-missing", drop(FRAMES), "결과 JSON 없음"))
    for name, change, reason in cases:
        _, run = run_case(root, name, change)
        document = run.document or {}
        log.check(f"기동 결과 없음 {name}", run.code == 0 and document.get("verdict") == LAUNCH_MISSING and (document.get("launch") or {}).get("missing_reason") == reason,
                  f"종료 {run.code} {document.get('verdict')} {(document.get('launch') or {}).get('missing_reason')}")


def drop_staging(files, _root):
    for relative in (MANIFEST, f"{STAGED}/TDGame/{PRESET}/{TUNING}", f"{STAGED}/TDGame/{PRESET}/{LOOK}"):
        files[relative] = None


def check_first_read_failed(log, root):
    cases = (("fail-manifest", replace(MANIFEST, manifest_text((TUNING,))), "첫 읽기 실패(①)"),
             ("fail-staged-sha", replace(f"{STAGED}/TDGame/{PRESET}/{TUNING}", '{"tuning": 2}\n'), "첫 읽기 실패(②)"),
             ("fail-one-error", replace(GAME_LOG, game_log(LOADED_TEXT.replace(": 0 errors", ": 1 errors"))), "첫 읽기 실패(③)"),
             ("fail-kept-previous", replace(GAME_LOG, game_log(LOADED_TEXT.replace("look ../../../TDGame/Content/FluidFX/PresetData/TDFluidMaterialLook.json (loaded)",
                                                                                    "look ../../../TDGame/Content/FluidFX/PresetData/TDFluidMaterialLook.json (kept previous)"))),
              "첫 읽기 실패(③)"),
             ("fail-other-category", replace(GAME_LOG, game_log(LOADED_TEXT.replace("LogTDFluidPresetTuning:", "LogTemp:"))), "첫 읽기 실패(③)"),
             ("fail-error-line", replace(GAME_LOG, game_log(extra=(f"{STAMP}LogTDFluidPresetTuning: Error: Unknown key Foo",))), "첫 읽기 실패(④)"),
             ("fail-no-log", drop(GAME_LOG), "첫 읽기 실패(③·④)"),
             ("fail-no-staging", drop_staging, "첫 읽기 실패(①·②)"))
    for name, change, expected in cases:
        _, run = run_case(root, name, change)
        document = run.document or {}
        log.check(f"첫 읽기 실패 {name} → {expected}", run.code == 0 and document.get("verdict") == expected and document.get("branch") == "failed",
                  f"종료 {run.code} {document.get('verdict')} {run.stderr[-200:]}")
    _, error_run = run_case(root, "fail-error-detail", replace(GAME_LOG, game_log(extra=(f"{STAMP}LogTDFluidPresetTuning: Error: Unknown key Foo",))))
    error_check = check_of(error_run.document, "④")
    log.check("④ 오류 줄 기록(시각 제거)", error_check.get("error_line_count") == 1 and error_check.get("error_lines") == ["LogTDFluidPresetTuning: Error: Unknown key Foo"],
              f"{error_check}")
    _, missing_run = run_case(root, "fail-staged-missing", drop(f"{STAGED}/TDGame/{PRESET}/{TUNING}"))
    staged_check = check_of(missing_run.document, "②")
    staged_shas = [entry.get("staged_sha256") for entry in staged_check.get("files", [])]
    log.check("② 스테이징 파일 없음 → 첫 읽기 실패(②)·근거 '스테이징 없음'(sha 다름 아님)", missing_run.code == 0
              and (missing_run.document or {}).get("verdict") == "첫 읽기 실패(②)" and staged_check.get("detail") == f"{TUNING} 스테이징 없음"
              and len(staged_shas) == 2 and staged_shas[0] is None and staged_shas[1] is not None, f"종료 {missing_run.code} {staged_check}")


def check_aborted(log, root):
    world = make_world(root / "aborted")
    run = run_audit(world, "package", "aborted", ["--tag", "fluid", "--aborted", "다른 빌드·쿠킹·측정 실행 중"])
    document = run.document or {}
    log.check("실행 전 중단 → 같은 스키마 JSON·종료 0·입력 0",
              run.code == 0 and document.get("verdict") == "미검증(실행 전 중단: 다른 빌드·쿠킹·측정 실행 중)" and document.get("branch") == "aborted"
              and document.get("tag") == "fluid" and statuses(document) == ["미평가"] * 4 and document.get("inputs") == {}, f"종료 {run.code} {document.get('verdict')} {run.stderr[-200:]}")
    _, confirmed = run_case(root, "schema-reference")
    log.check("모든 분기 같은 최상위·cook·launch 키", confirmed.document is not None and set(document) == set(confirmed.document)
              and set(document.get("cook") or {}) == set(confirmed.document["cook"]) and set(document.get("launch") or {}) == set(confirmed.document["launch"]), "키 다름")
    path_run = run_audit(world, "package", "aborted-path", ["--tag", "min", "--aborted", f"보류 파일 {world}\\Saved\\AgentOps\\build_hold.json 잠김"])
    verdict = (path_run.document or {}).get("verdict") or ""
    log.check("중단 사유의 절대 경로 → 루트 상대·종료 0", path_run.code == 0 and verdict == "미검증(실행 전 중단: 보류 파일 Saved\\AgentOps\\build_hold.json 잠김)",
              f"종료 {path_run.code} {verdict}")


def check_argument_errors(log, root):
    world = make_world(root / "arguments")
    outside = str(root / "elsewhere" / "cook.json")
    full = run_arguments()
    cases = (("no-tag", full[2:]),
             ("no-staged-root", full[:-2]),
             ("bad-sha", run_arguments(cook_before="abc")),
             ("aborted-with-run", ["--tag", "min", "--aborted", "중단", "--cook-report", COOK]),
             ("aborted-empty", ["--tag", "min", "--aborted", " "]),
             ("outside-root", run_arguments(cook=outside)),
             ("bad-tag", run_arguments(tag="Min!")))
    for name, arguments in cases:
        run = run_audit(world, "package", f"arg-{name}", arguments)
        log.check(f"인수 오류 {name} → 종료 2·출력 없음", run.code == 2 and run.json_bytes is None and run.md_bytes is None, f"종료 {run.code} {run.stderr[-200:]}")


def run(tmp_root):
    log = FixtureLog("package")
    root = Path(tmp_root)
    check_confirmed(log, root)
    check_cook_failed(log, root)
    check_launch_missing(log, root)
    check_first_read_failed(log, root)
    check_aborted(log, root)
    check_argument_errors(log, root)
    return log.result()

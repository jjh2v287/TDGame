"""실행 환경: 시스템 Python 3 와 dxc.exe(엔진 폴더의 dxc.exe 우선, 없으면 엔진 dxcompiler.dll 을 Windows Kits dxc.exe 앞단에 붙여 씀, 그것도 안 되면 Windows Kits dxc 단독), 에디터 불필요
실행 명령: python Tools/FluidFX/check_fluid_gpu_shaders.py [--root 프로젝트경로] [--engine 엔진Engine폴더] [--dxc dxc경로] [--keep-temp]
출력: 셰이더 클래스·순열·프로필별 PASS/FAIL 줄, RHI 시험 플래그 PASS/FAIL 줄과 마지막 줄 JSON 요약(사용한 컴파일러 포함), 하나라도 실패하면 종료 코드 1
상태: 2026-10-19 FX-4a 묶음 A 빌더 2가 작성. TDFluidGpuStepPasses.cpp 의 IMPLEMENT_GLOBAL_SHADER·SHADER_PERMUTATION 을 읽어 Source/TDFluidGpu/Shaders 의 .usf 를 가상 경로(/Engine/, /TDFluidGpu/) 그대로 펼쳐 cs_6_0 으로 컴파일한다. 엔진 셰이더 컴파일러 자체 검사(에디터 시작)를 대신하지 못한다. 같은 날 추가: Source/TDFluidGpu·Source/TDGame/FluidFX 의 `TDGame.FluidFXRhi.` 시험마다 EAutomationTestFlags::NonNullRHI 가 있는지 본다(RunTests 필터는 부분 문자열 일치라 -NullRHI 의 `TDGame.FluidFX` 실행이 RHI 시험을 함께 고르므로, 이 플래그로 널 RHI 목록에서 뺀다).
"""
import argparse
import glob
import itertools
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True

DEFAULT_ENGINE_DIR = r"C:\Program Files\Epic Games\UE_5.8\Engine"
SHADER_TARGET = "cs_6_0"
STEP_PASSES_SOURCE = pathlib.Path("Source") / "TDFluidGpu" / "Private" / "TDFluidGpuStepPasses.cpp"
MODULE_SHADER_DIR = pathlib.Path("Source") / "TDFluidGpu" / "Shaders"
INCLUDE_PATTERN = re.compile(r'^(\s*#\s*include\s*)"([^"]+)"', re.MULTILINE)
CLASS_PATTERN = re.compile(r"class\s+(FTDFluidGpu\w+CS)\s*:\s*public\s+\w+")
IMPLEMENT_PATTERN = re.compile(r'IMPLEMENT_GLOBAL_SHADER\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"(\w+)"\s*,\s*SF_Compute\s*\)')
BOOL_PERMUTATION_PATTERN = re.compile(r'SHADER_PERMUTATION_BOOL\(\s*"(\w+)"\s*\)')
INT_PERMUTATION_PATTERN = re.compile(r'SHADER_PERMUTATION_INT\(\s*"(\w+)"\s*,\s*(\d+)\s*\)')
COMMENT_PATTERN = re.compile(r"//|/\*")
RHI_TEST_SOURCE_DIRS = (pathlib.Path("Source") / "TDFluidGpu", pathlib.Path("Source") / "TDGame" / "FluidFX")
AUTOMATION_TEST_PATTERN = re.compile(r"(?:IMPLEMENT_\w*AUTOMATION_TEST|BEGIN_DEFINE_SPEC)\(([^)]*)\)")
QUOTED_TEST_NAME_PATTERN = re.compile(r'"([^"]+)"')
RHI_TEST_PREFIX = "TDGame.FluidFXRhi."
RHI_TEST_REQUIRED_FLAG = "EAutomationTestFlags::NonNullRHI"

PROFILE_DEFINES = {
    "SM5": {
        "COMPILER_HLSL": "1",
        "COMPILER_DXC": "1",
        "COMPILER_FXC": "0",
        "SM5_PROFILE": "1",
        "SM6_PROFILE": "0",
        "PLATFORM_SUPPORTS_CONSTANTBUFFER_OBJECT": "0",
        "PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS": "0",
        "PLATFORM_SUPPORTS_DIAGNOSTIC_BUFFER": "0",
        "PLATFORM_SUPPORTS_ROV": "1",
    },
    "SM6": {
        "COMPILER_HLSL": "1",
        "COMPILER_DXC": "1",
        "COMPILER_FXC": "0",
        "SM5_PROFILE": "0",
        "SM6_PROFILE": "1",
        "PLATFORM_SUPPORTS_CONSTANTBUFFER_OBJECT": "1",
        "PLATFORM_SUPPORTS_SM6_0_WAVE_OPERATIONS": "1",
        "PLATFORM_SUPPORTS_DIAGNOSTIC_BUFFER": "1",
        "PLATFORM_SUPPORTS_ROV": "1",
        "PLATFORM_SUPPORTS_STATIC_SAMPLERS": "1",
        "COMPILER_SUPPORTS_ATTRIBUTES": "1",
    },
}


def parse_arguments():
    parser = argparse.ArgumentParser(description="TDFluidGpu 전역 셰이더 오프라인 dxc 컴파일 검사")
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--engine", default=os.environ.get("UE_ENGINE_DIR", DEFAULT_ENGINE_DIR))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--keep-temp", action="store_true")
    return parser.parse_args()


def find_kits_dxc():
    program_files = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    found = sorted(glob.glob(os.path.join(program_files, "Windows Kits", "10", "bin", "*", "x64", "dxc.exe")))
    return found[-1] if found else ""


def probe_dxc(dxc_path):
    probe_dir = tempfile.mkdtemp(prefix="td_fluid_gpu_dxc_probe_")
    try:
        probe_source = pathlib.Path(probe_dir) / "probe.hlsl"
        probe_source.write_text("RWStructuredBuffer<uint> Output; [numthreads(1,1,1)] void MainCS() { Output[0] = 1u; }\n", encoding="utf-8")
        compile_result = subprocess.run([dxc_path, "-T", SHADER_TARGET, "-E", "MainCS", str(probe_source)], capture_output=True, text=True, timeout=120)
        if compile_result.returncode != 0:
            return ""
        help_result = subprocess.run([dxc_path, "-?"], capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    finally:
        shutil.rmtree(probe_dir, ignore_errors=True)
    version_lines = [line.strip() for line in (help_result.stdout + help_result.stderr).splitlines() if "version" in line.lower()]
    return version_lines[0] if version_lines else "컴파일 가능(버전 문자열 없음)"


def find_compiler(explicit_path, engine_dir, work_dir):
    for candidate, label in ((explicit_path, "지정 dxc"), (os.environ.get("DXC_PATH", ""), "DXC_PATH")):
        if candidate and os.path.isfile(candidate):
            return candidate, label, probe_dxc(candidate)

    engine_binaries = pathlib.Path(engine_dir) / "Binaries"
    engine_dxc = sorted(engine_binaries.glob("ThirdParty/**/dxc.exe"))
    if engine_dxc:
        return str(engine_dxc[0]), "엔진 dxc.exe", probe_dxc(str(engine_dxc[0]))

    kits_dxc = find_kits_dxc()
    engine_compiler = engine_binaries / "ThirdParty" / "ShaderConductor" / "Win64" / "dxcompiler.dll"
    engine_validator = engine_compiler.with_name("dxil.dll")
    if kits_dxc and engine_compiler.is_file():
        bridge_dir = pathlib.Path(work_dir) / "engine_dxc"
        bridge_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(kits_dxc, bridge_dir / "dxc.exe")
        shutil.copy2(engine_compiler, bridge_dir / "dxcompiler.dll")
        if engine_validator.is_file():
            shutil.copy2(engine_validator, bridge_dir / "dxil.dll")
        bridge_dxc = str(bridge_dir / "dxc.exe")
        version = probe_dxc(bridge_dxc)
        if version:
            return bridge_dxc, "엔진 dxcompiler.dll(Windows Kits dxc.exe 앞단)", version

    if kits_dxc:
        return kits_dxc, "Windows Kits dxc.exe(엔진 컴파일러 아님)", probe_dxc(kits_dxc)
    return "", "", ""


def supports_hlsl_2021(dxc_path, work_dir):
    probe_source = pathlib.Path(work_dir) / "hlsl2021_probe.hlsl"
    probe_source.write_text("[numthreads(1,1,1)] void MainCS() {}\n", encoding="utf-8")
    result = subprocess.run([dxc_path, "-T", SHADER_TARGET, "-E", "MainCS", "-HV", "2021", str(probe_source)], capture_output=True, text=True, timeout=120)
    return result.returncode == 0


def parse_shader_classes(step_passes_text):
    classes = []
    class_matches = list(CLASS_PATTERN.finditer(step_passes_text))
    for implement in IMPLEMENT_PATTERN.finditer(step_passes_text):
        class_name, virtual_path, entry_point = implement.groups()
        declaration = next((match for match in class_matches if match.group(1) == class_name), None)
        if declaration is None:
            continue
        body = step_passes_text[declaration.start():implement.start()]
        dimensions = [(name, ["0", "1"]) for name in BOOL_PERMUTATION_PATTERN.findall(body)]
        dimensions += [(name, [str(value) for value in range(int(count))]) for name, count in INT_PERMUTATION_PATTERN.findall(body)]
        classes.append({"class": class_name, "path": virtual_path, "entry": entry_point, "dimensions": dimensions})
    return classes


def resolve_virtual_path(virtual_path, engine_shader_dir, module_shader_dir):
    if virtual_path.startswith("/Engine/Shared/"):
        shared_relative = virtual_path[len("/Engine/Shared/"):]
        shader_shared = pathlib.Path(engine_shader_dir) / "Shared" / shared_relative
        if shader_shared.is_file():
            return shader_shared
        return pathlib.Path(engine_shader_dir).parent / "Source" / "Runtime" / "Core" / "Public" / shared_relative
    if virtual_path.startswith("/Engine/"):
        return pathlib.Path(engine_shader_dir) / virtual_path[len("/Engine/"):]
    if virtual_path.startswith("/TDFluidGpu/"):
        return pathlib.Path(module_shader_dir) / virtual_path[len("/TDFluidGpu/"):]
    return None


def join_virtual(including_virtual_path, relative_path):
    parent = including_virtual_path.rsplit("/", 1)[0]
    parts = []
    for part in (parent + "/" + relative_path).split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if parts:
                parts.pop()
            continue
        parts.append(part)
    return "/" + "/".join(parts)


class ShaderMirror:
    def __init__(self, mirror_dir, engine_shader_dir, module_shader_dir):
        self.mirror_dir = pathlib.Path(mirror_dir)
        self.engine_shader_dir = engine_shader_dir
        self.module_shader_dir = module_shader_dir
        self.materialized = {}
        self.unresolved = set()

    def mirror_path(self, virtual_path):
        return self.mirror_dir / virtual_path.lstrip("/")

    def materialize(self, virtual_path):
        if virtual_path in self.materialized:
            return self.materialized[virtual_path]
        source_path = resolve_virtual_path(virtual_path, self.engine_shader_dir, self.module_shader_dir)
        if source_path is None or not source_path.is_file():
            self.unresolved.add(virtual_path)
            self.materialized[virtual_path] = None
            return None
        target_path = self.mirror_path(virtual_path)
        self.materialized[virtual_path] = target_path
        text = source_path.read_text(encoding="utf-8", errors="replace")

        def rewrite(match):
            include_path = match.group(2)
            included_virtual = include_path if include_path.startswith("/") else join_virtual(virtual_path, include_path)
            included_target = self.materialize(included_virtual)
            if included_target is None:
                return match.group(0)
            return f'{match.group(1)}"{included_virtual.lstrip("/")}"'

        rewritten = INCLUDE_PATTERN.sub(rewrite, text)
        target_path.parent.mkdir(parents=True, exist_ok=True)
        target_path.write_text(rewritten, encoding="utf-8")
        return target_path


def compile_permutation(dxc_path, mirror_dir, source_path, entry_point, defines, hlsl_version):
    command = [dxc_path, "-T", SHADER_TARGET, "-E", entry_point, "-Zpr", "-I", str(mirror_dir)]
    if hlsl_version:
        command += ["-HV", hlsl_version]
    for name, value in defines.items():
        command += ["-D", f"{name}={value}"]
    command.append(str(source_path))
    result = subprocess.run(command, capture_output=True, text=True, timeout=600)
    output = (result.stdout + result.stderr).strip()
    errors = [line for line in output.splitlines() if "error" in line.lower()]
    return result.returncode == 0, errors


def find_comment_lines(shader_dir):
    findings = []
    for path in sorted(pathlib.Path(shader_dir).rglob("*")):
        if path.suffix not in (".usf", ".ush"):
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if COMMENT_PATTERN.search(line):
                findings.append(f"{path.name}:{line_number}")
    return findings


def find_rhi_tests_missing_non_null_flag(root):
    rhi_test_names = []
    missing_flag_tests = []
    for source_dir in RHI_TEST_SOURCE_DIRS:
        for path in sorted((root / source_dir).rglob("*.cpp")):
            text = path.read_text(encoding="utf-8", errors="replace")
            for match in AUTOMATION_TEST_PATTERN.finditer(text):
                arguments = match.group(1)
                name_match = QUOTED_TEST_NAME_PATTERN.search(arguments)
                if not name_match or not name_match.group(1).startswith(RHI_TEST_PREFIX):
                    continue
                rhi_test_names.append(name_match.group(1))
                if RHI_TEST_REQUIRED_FLAG not in arguments[name_match.end():]:
                    missing_flag_tests.append(f"{path.name}:{name_match.group(1)}")
    return rhi_test_names, missing_flag_tests


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    arguments = parse_arguments()
    root = pathlib.Path(arguments.root)
    engine_dir = pathlib.Path(arguments.engine)
    engine_shader_dir = engine_dir / "Shaders"
    module_shader_dir = root / MODULE_SHADER_DIR
    step_passes_path = root / STEP_PASSES_SOURCE
    summary = {"compiler": "", "compiler_version": "", "hlsl_versions": [], "passed": 0, "failed": 0, "failures": [], "unresolved_includes": [], "comment_lines": []}

    if not step_passes_path.is_file() or not module_shader_dir.is_dir() or not engine_shader_dir.is_dir():
        print(f"FAIL 입력 경로 없음: {step_passes_path} / {module_shader_dir} / {engine_shader_dir}")
        print(json.dumps(summary, ensure_ascii=False))
        return 1

    work_dir = tempfile.mkdtemp(prefix="td_fluid_gpu_shaders_")
    try:
        dxc_path, compiler_label, compiler_version = find_compiler(arguments.dxc, engine_dir, work_dir)
        summary["compiler"] = compiler_label
        summary["compiler_version"] = compiler_version
        if not dxc_path:
            print("FAIL dxc.exe 를 찾지 못함")
            print(json.dumps(summary, ensure_ascii=False))
            return 1

        hlsl_versions = [""]
        if supports_hlsl_2021(dxc_path, work_dir):
            hlsl_versions.append("2021")
        summary["hlsl_versions"] = [version or "기본" for version in hlsl_versions]

        shader_classes = parse_shader_classes(step_passes_path.read_text(encoding="utf-8"))
        if not shader_classes:
            print("FAIL IMPLEMENT_GLOBAL_SHADER 를 찾지 못함")
            print(json.dumps(summary, ensure_ascii=False))
            return 1

        mirror = ShaderMirror(pathlib.Path(work_dir) / "mirror", engine_shader_dir, module_shader_dir)
        for shader_class in shader_classes:
            source_path = mirror.materialize(shader_class["path"])
            if source_path is None:
                summary["failed"] += 1
                summary["failures"].append(f"{shader_class['class']}: 소스 없음 {shader_class['path']}")
                print(f"FAIL {shader_class['class']} 소스 없음 {shader_class['path']}")
                continue
            names = [name for name, _ in shader_class["dimensions"]]
            value_lists = [values for _, values in shader_class["dimensions"]]
            for values in itertools.product(*value_lists):
                for profile_name, profile_defines in PROFILE_DEFINES.items():
                    for hlsl_version in hlsl_versions:
                        defines = dict(profile_defines)
                        defines.update(zip(names, values))
                        permutation_label = ",".join(f"{name}={value}" for name, value in zip(names, values)) or "-"
                        label = f"{shader_class['class']} [{permutation_label}] {profile_name} HV{hlsl_version or '기본'}"
                        succeeded, errors = compile_permutation(dxc_path, mirror.mirror_dir, source_path, shader_class["entry"], defines, hlsl_version)
                        if succeeded:
                            summary["passed"] += 1
                            print(f"PASS {label}")
                            continue
                        summary["failed"] += 1
                        summary["failures"].append(label + " :: " + " | ".join(errors[:4]))
                        print(f"FAIL {label}")
                        for error in errors[:8]:
                            print(f"    {error}")

        summary["unresolved_includes"] = sorted(mirror.unresolved)
        summary["comment_lines"] = find_comment_lines(module_shader_dir)
        if summary["comment_lines"]:
            summary["failed"] += 1
            print("FAIL 셰이더 주석: " + ", ".join(summary["comment_lines"]))
        rhi_test_names, missing_flag_tests = find_rhi_tests_missing_non_null_flag(root)
        summary["rhi_tests"] = len(rhi_test_names)
        summary["rhi_tests_missing_non_null_flag"] = missing_flag_tests
        if missing_flag_tests:
            summary["failed"] += 1
            print(f"FAIL RHI 시험 {RHI_TEST_REQUIRED_FLAG} 없음: " + ", ".join(missing_flag_tests))
        else:
            print(f"PASS RHI 시험 {len(rhi_test_names)}개 모두 {RHI_TEST_REQUIRED_FLAG}")
    finally:
        if arguments.keep_temp:
            print(f"임시 폴더 유지: {work_dir}")
        else:
            shutil.rmtree(work_dir, ignore_errors=True)

    print(json.dumps(summary, ensure_ascii=False))
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

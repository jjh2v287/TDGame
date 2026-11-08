"""실행 환경: Windows 64비트 시스템 Python 3 와 dxc.exe(엔진 폴더의 dxc.exe 우선, 없으면 엔진 dxcompiler.dll 을 Windows Kits dxc.exe 앞단에 붙여 씀, 그것도 안 되면 Windows Kits dxc 단독), FXC 패스는 엔진 Engine/Binaries/ThirdParty/Windows/DirectX/x64/d3dcompiler_47.dll 을 ctypes 로 그 자리에서 불러 D3DCompile·D3DDisassemble 호출(fxc.exe·복사 불필요), 에디터 불필요
실행 명령: python -B Tools/FluidFX/check_fluid_gpu_shaders.py [--root 프로젝트경로] [--engine 엔진Engine폴더] [--dxc dxc경로] [--d3dcompiler d3dcompiler_47.dll경로] [--allow-missing-fxc] [--keep-temp]
출력: 셰이더 클래스·순열·프로필별 PASS/FAIL 줄, 순열별 `PASS|FAIL <클래스> [순열] FXC cs_5_0` 줄, RHI 시험 플래그 PASS/FAIL 줄, `FXC cs_5_0 통과 N 실패 M` 또는 `건너뜀(FXC 없음, --allow-missing-fxc)` 줄과 마지막 줄 JSON 요약(fxc=ran|skipped|unavailable·fxc_passed·fxc_failed 포함). 종료 코드 0 통과 / 1 입력 경로·dxc 없음 또는 dxc·FXC·주석·RHI 플래그 중 하나라도 실패 / 2 FXC 를 쓸 수 없음(d3dcompiler_47.dll 없음·불러오기 실패, fxc=unavailable). --allow-missing-fxc 면 기본 경로 dll 실패만 FXC 패스를 건너뛰고(fxc=skipped, 종료 코드엔 안 셈) --d3dcompiler 로 명시한 경로 실패는 그래도 종료 2
상태: 2026-11-08 FU-14 FXC 패스 추가 — 같은 순열을 엔진 SM5 정의(COMPILER_FXC=1)로 dxc `-P` 전처리(FXC 는 #pragma once·가변 인수 매크로를 못 다뤄 엔진도 자체 전처리 뒤 넘김, 산출물에 `void <진입점>(` 이 없으면 실패)한 뒤 엔진 플래그(/Gec /Zpr /O3)로 cs_5_0 컴파일해 (A) asm `dcl_uav_` 줄의 가장 높은 u 슬롯 + 1(범위 `[a:b]` 는 b) > 8 이거나 `64 UAV slots` 표시(앞머리 `;`·`//`)가 있음 (B) FXC 컴파일 오류가 하나라도 있음(X3058·X4026 등 모두, 엔진의 DXC 재작성 뒤 재시도 구제에 기대지 않음) (C) `Typed UAV Load Additional Formats` 인데 C++ ModifyCompilationEnvironment 가 그 순열에 CFLAG_AllowTypedUAVLoads 를 주지 않거나 판정 불가(엄격 기본) (D) 배정밀도 `enableDoublePrecisionFloatOps`(엔진 GD3DCheckForDoubles) 면 실패. C++ 판정은 클래스·같은 파일 부모 클래스의 `if (<순열 Get<>/!/&&/||/==> 조건>) return;`·`if (조건) { …Add(CFLAG_AllowTypedUAVLoads); }`·무조건 Add 만 해석하고, 플래그를 다루는 몸통의 `#`·삼항, 조건의 단일 `&`·`|`·연쇄 비교, 지역 변수 조건·else·중첩 if, 부모 ModifyCompilationEnvironment 를 부르는 자식 몸통의 return·if·else·반복·`#`·삼항, 다른 파일 부모, 프로젝트 설정 r.D3D.CheckedForTypedUAVs=0 은 판정 불가로 본다. 같은 날 반박 반영: FXC 못 쓰면 종료 2·--allow-missing-fxc, 진입점 확인, 판정 불가 실패, 2차 반박 뒤 X3058 구제 경로(DXIL 재판정) 삭제·UAV 최대 슬롯·부모 도달성·배정밀도. ShouldCompilePermutation 거르기와 클래스 밖 공유 순열 차원(TDFLUIDGPU_WRAP_AREA·TDFLUIDGPU_MASS_INPUT, 7클래스 엔진 순열 40개)은 dxc 패스와 같이 반영하지 않는다(전 순열 컴파일, 공유 차원은 기본값만; 2026-11-08 수동 확인 40/40 FXC 통과·UAV 최대 7). C++ CFLAG 판정의 알려진 비대상(엔진 셰이더 코드에서 쓰지 않는 형태, 검사기는 참·엔진은 거짓): 정수 차원의 `!Get<…>() == 1`, 몸통의 goto, 람다 안 부모 호출. 2026-10-19 FX-4a 묶음 A 빌더 2가 작성. Source/TDFluidGpu/Private/*.cpp 가운데 IMPLEMENT_GLOBAL_SHADER 가 있는 파일 전부(2026-10-29 W3-0 이전에는 TDFluidGpuStepPasses.cpp 하나)를 파일마다 따로 읽어 IMPLEMENT_GLOBAL_SHADER·SHADER_PERMUTATION 으로 Source/TDFluidGpu/Shaders 의 .usf 를 가상 경로(/Engine/, /TDFluidGpu/) 그대로 펼쳐 cs_6_0 으로 컴파일한다. 엔진 셰이더 컴파일러 자체 검사(에디터 시작)를 대신하지 못한다. 같은 날 추가: Source/TDFluidGpu·Source/TDGame/FluidFX 의 `TDGame.FluidFXRhi.` 시험마다 EAutomationTestFlags::NonNullRHI 가 있는지 본다(RunTests 필터는 부분 문자열 일치라 -NullRHI 의 `TDGame.FluidFX` 실행이 RHI 시험을 함께 고르므로, 이 플래그로 널 RHI 목록에서 뺀다).
"""
import argparse
import ast
import ctypes
import functools
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
SHADER_SOURCE_DIR = pathlib.Path("Source") / "TDFluidGpu" / "Private"
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

FXC_TARGET = "cs_5_0"
ENGINE_FXC_LIBRARY = pathlib.Path("Binaries") / "ThirdParty" / "Windows" / "DirectX" / "x64" / "d3dcompiler_47.dll"
FXC_PROFILE_DEFINES = dict(PROFILE_DEFINES["SM5"], COMPILER_DXC="0", COMPILER_FXC="1")
D3DCOMPILE_PACK_MATRIX_ROW_MAJOR = 1 << 3
D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY = 1 << 12
D3DCOMPILE_OPTIMIZATION_LEVEL3 = 1 << 15
FXC_COMPILE_FLAGS = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_OPTIMIZATION_LEVEL3
LEGACY_PREPROCESS_HELP_PATTERN = re.compile(r"^\s*-P <value>", re.MULTILINE)
BLOB_RELEASE_SLOT = 2
BLOB_POINTER_SLOT = 3
BLOB_SIZE_SLOT = 4
FEATURE_LEVEL_11_0_MAXIMUM_UAVS = 8
FXC_UNAVAILABLE_EXIT_CODE = 2
UAV_DECLARATION_LINE_PATTERN = re.compile(r"^\s*dcl_uav_[^\n]*", re.MULTILINE)
UAV_REGISTER_PATTERN = re.compile(r"\bu(\d+)\b|\b[uU]\d*\[\d+:(\d+)\]")
SIXTY_FOUR_UAV_SLOTS_PATTERN = re.compile(r"^\s*(?:;|//)\s+64 UAV slots\s*$", re.MULTILINE)
DOUBLE_PRECISION_FLAG = "enableDoublePrecisionFloatOps"
TYPED_UAV_LOAD_MARKER = "Typed UAV Load Additional Formats"
TYPED_UAV_LOAD_FLAG = "CFLAG_AllowTypedUAVLoads"
ENGINE_SHADER_BASE_CLASSES = ("FGlobalShader",)
MAXIMUM_BASE_CLASS_DEPTH = 8
CPP_COMMENT_PATTERN = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
DIMENSION_CLASS_PATTERN = re.compile(r'class\s+(\w+)\s*:\s*SHADER_PERMUTATION_(?:BOOL|INT)\(\s*"(\w+)"')
BASE_CLASS_PATTERN = re.compile(r":\s*public\s+(\w+)")
IF_STATEMENT_PATTERN = re.compile(r"\s*if\s*\(")
BLOCK_OPEN_PATTERN = re.compile(r"\s*\{")
ELSE_PATTERN = re.compile(r"\s*else\b")
UNSUPPORTED_STATEMENT_PATTERN = re.compile(r"\s*(?:for|while|switch|do|else)\b")
RETURN_PATTERN = re.compile(r"\breturn\b")
NESTED_IF_PATTERN = re.compile(r"\bif\b")
ADD_TYPED_UAV_LOAD_FLAG_PATTERN = re.compile(r"\.\s*Add\s*\(\s*CFLAG_AllowTypedUAVLoads\s*\)")
GET_DIMENSION_PATTERN = re.compile(r"\w+\s*\.\s*Get\s*<\s*(\w+)\s*>\s*\(\s*\)")
CONDITION_CHARACTERS_PATTERN = re.compile(r"[\d\s()<>=!&|]*")
SINGLE_BITWISE_OPERATOR_PATTERN = re.compile(r"(?<![&|])[&|](?![&|])")
FLAG_BODY_UNDECIDABLE_PATTERN = re.compile(r"[#?]")
PARENT_CALL_UNDECIDABLE_PATTERN = re.compile(r"[#?]|\b(?:return|if|else|for|while|do|switch|goto)\b")


def parse_arguments():
    parser = argparse.ArgumentParser(description="TDFluidGpu 전역 셰이더 오프라인 dxc 컴파일 검사")
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[2]))
    parser.add_argument("--engine", default=os.environ.get("UE_ENGINE_DIR", DEFAULT_ENGINE_DIR))
    parser.add_argument("--dxc", default="")
    parser.add_argument("--d3dcompiler", default="", help="FXC 패스용 d3dcompiler_47.dll(기본: <엔진>/Binaries/ThirdParty/Windows/DirectX/x64/d3dcompiler_47.dll). 명시했는데 못 불러오면 --allow-missing-fxc 가 있어도 종료 2")
    parser.add_argument("--allow-missing-fxc", action="store_true", help="기본 경로 d3dcompiler_47.dll 을 못 쓰면 FXC 패스를 건너뛰고(fxc=skipped) 계속한다. 없으면 종료 2")
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


def find_shader_sources(shader_source_dir):
    return [path for path in sorted(shader_source_dir.glob("*.cpp")) if IMPLEMENT_PATTERN.search(path.read_text(encoding="utf-8"))]


def parse_shader_classes(source_text):
    classes = []
    cpp_text = CPP_COMMENT_PATTERN.sub("", source_text)
    file_dimension_classes = dict(DIMENSION_CLASS_PATTERN.findall(cpp_text))
    class_matches = list(CLASS_PATTERN.finditer(source_text))
    for implement in IMPLEMENT_PATTERN.finditer(source_text):
        class_name, virtual_path, entry_point = implement.groups()
        declaration = next((match for match in class_matches if match.group(1) == class_name), None)
        if declaration is None:
            continue
        body = source_text[declaration.start():implement.start()]
        dimensions = [(name, ["0", "1"]) for name in BOOL_PERMUTATION_PATTERN.findall(body)]
        dimensions += [(name, [str(value) for value in range(int(count))]) for name, count in INT_PERMUTATION_PATTERN.findall(body)]
        dimension_classes = {**file_dimension_classes, **dict(DIMENSION_CLASS_PATTERN.findall(CPP_COMMENT_PATTERN.sub("", body)))}
        classes.append({"class": class_name, "path": virtual_path, "entry": entry_point, "dimensions": dimensions, "cpp_text": cpp_text, "dimension_classes": dimension_classes})
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


def call_blob_method(blob_pointer, slot, result_type):
    method_table = ctypes.cast(blob_pointer, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
    return ctypes.WINFUNCTYPE(result_type, ctypes.c_void_p)(method_table[slot])(blob_pointer)


def take_blob_bytes(blob_pointer):
    if not blob_pointer:
        return b""
    buffer_pointer = call_blob_method(blob_pointer, BLOB_POINTER_SLOT, ctypes.c_void_p)
    buffer_size = call_blob_method(blob_pointer, BLOB_SIZE_SLOT, ctypes.c_size_t)
    data = ctypes.string_at(buffer_pointer, buffer_size) if buffer_pointer and buffer_size else b""
    call_blob_method(blob_pointer, BLOB_RELEASE_SLOT, ctypes.c_ulong)
    return data


class FxcCompiler:
    def __init__(self, library_path):
        self.library_path = pathlib.Path(library_path)
        self.library = ctypes.WinDLL(str(self.library_path))
        self.library.D3DCompile.argtypes = [
            ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_void_p),
        ]
        self.library.D3DCompile.restype = ctypes.c_long
        self.library.D3DDisassemble.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.library.D3DDisassemble.restype = ctypes.c_long

    def compile(self, preprocessed_source, source_name, entry_point):
        code_blob = ctypes.c_void_p()
        message_blob = ctypes.c_void_p()
        result = self.library.D3DCompile(
            preprocessed_source, len(preprocessed_source), source_name.encode("ascii", errors="replace"), None, None,
            entry_point.encode("ascii"), FXC_TARGET.encode("ascii"), FXC_COMPILE_FLAGS, 0, ctypes.byref(code_blob), ctypes.byref(message_blob))
        messages = take_blob_bytes(message_blob.value).decode("utf-8", errors="replace")
        code = take_blob_bytes(code_blob.value)
        succeeded = result >= 0 and bool(code)
        errors = [line.strip() for line in messages.splitlines() if "error" in line.lower()]
        if not succeeded and not errors:
            errors = [f"HRESULT 0x{result & 0xFFFFFFFF:08X}"] + messages.splitlines()[:3]
        if not succeeded:
            return False, errors, ""
        disassembly_blob = ctypes.c_void_p()
        if self.library.D3DDisassemble(code, len(code), 0, b"", ctypes.byref(disassembly_blob)) < 0:
            return False, ["D3DDisassemble 실패"], ""
        return True, errors, take_blob_bytes(disassembly_blob.value).decode("ascii", errors="replace")


def load_fxc_compiler(explicit_path, engine_dir):
    library_path = pathlib.Path(explicit_path) if explicit_path else pathlib.Path(engine_dir) / ENGINE_FXC_LIBRARY
    if not hasattr(ctypes, "WinDLL"):
        return None, "Windows 아님"
    if not library_path.is_file():
        return None, f"{library_path} 없음"
    try:
        return FxcCompiler(library_path), str(library_path)
    except (OSError, AttributeError) as error:
        return None, f"{library_path} 불러오기 실패: {error}"


def resolve_fxc_compiler(explicit_path, engine_dir, allow_missing):
    fxc_compiler, description = load_fxc_compiler(explicit_path, engine_dir)
    if fxc_compiler:
        return fxc_compiler, "ran", description
    if allow_missing and not explicit_path:
        return None, "skipped", description
    return None, "unavailable", description


def has_entry_point(preprocessed_source, entry_point):
    return re.search(rb"\bvoid\s+" + re.escape(entry_point.encode("ascii")) + rb"\s*\(", preprocessed_source) is not None


@functools.lru_cache(maxsize=None)
def uses_legacy_preprocess_option(dxc_path):
    help_result = subprocess.run([dxc_path, "-help"], capture_output=True, text=True, timeout=60)
    return bool(LEGACY_PREPROCESS_HELP_PATTERN.search(help_result.stdout + help_result.stderr))


def preprocess_for_fxc(dxc_path, mirror_dir, source_path, entry_point, defines, output_path):
    output_arguments = ["-P", str(output_path)] if uses_legacy_preprocess_option(dxc_path) else ["-P", "-Fi", str(output_path)]
    command = [dxc_path, *output_arguments, "-T", SHADER_TARGET, "-E", entry_point, "-I", str(mirror_dir)]
    for name, value in defines.items():
        command += ["-D", f"{name}={value}"]
    command.append(str(source_path))
    result = subprocess.run(command, capture_output=True, text=True, timeout=600)
    output = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or not pathlib.Path(output_path).is_file():
        return None, ["dxc 전처리 실패: " + line for line in output.splitlines() if "error" in line.lower()][:4] or [f"dxc 전처리 실패(종료 {result.returncode})"]
    preprocessed_source = pathlib.Path(output_path).read_bytes()
    if not has_entry_point(preprocessed_source, entry_point):
        return None, [f"전처리 산출물에 진입점 void {entry_point}( 없음({len(preprocessed_source)}바이트)"]
    return preprocessed_source, []


def find_matching_delimiter(text, open_index, open_character, close_character):
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == open_character:
            depth += 1
        elif text[index] == close_character:
            depth -= 1
            if depth == 0:
                return index
    return -1


def find_class_text(cpp_text, class_name):
    header = re.search(r"\b(?:class|struct)\s+" + re.escape(class_name) + r"\b[^;{]*\{", cpp_text)
    if header is None:
        return None, None
    close_index = find_matching_delimiter(cpp_text, header.end() - 1, "{", "}")
    if close_index < 0:
        return None, None
    base_match = BASE_CLASS_PATTERN.search(header.group(0))
    return cpp_text[header.end():close_index], base_match.group(1) if base_match else None


def find_function_body(class_text, function_name):
    header = re.search(r"\b" + function_name + r"\s*\([^)]*\)\s*(?:const\s*)?\{", class_text)
    if header is None:
        return None
    close_index = find_matching_delimiter(class_text, header.end() - 1, "{", "}")
    return class_text[header.end():close_index] if close_index >= 0 else None


def evaluate_permutation_condition(condition, dimension_values):
    unknown_dimensions = []

    def substitute(match):
        if match.group(1) not in dimension_values:
            unknown_dimensions.append(match.group(1))
            return " 0 "
        return f" {dimension_values[match.group(1)]} "

    expression = GET_DIMENSION_PATTERN.sub(substitute, condition)
    expression = re.sub(r"\btrue\b", " 1 ", re.sub(r"\bfalse\b", " 0 ", expression))
    if unknown_dimensions or not CONDITION_CHARACTERS_PATTERN.fullmatch(expression) or SINGLE_BITWISE_OPERATOR_PATTERN.search(expression):
        return None
    expression = expression.replace("&&", " and ").replace("||", " or ").replace("!=", " \0 ").replace("!", " not ").replace("\0", "!=")
    try:
        syntax_tree = ast.parse(expression.strip(), mode="eval")
        if any(isinstance(node, ast.Compare) and len(node.ops) > 1 for node in ast.walk(syntax_tree)):
            return None
        return bool(eval(compile(syntax_tree, "<condition>", "eval"), {"__builtins__": {}}, {}))
    except (SyntaxError, TypeError, ValueError, NameError):
        return None


def read_statement(body, position):
    if UNSUPPORTED_STATEMENT_PATTERN.match(body, position):
        return None, None, "", len(body)
    if_match = IF_STATEMENT_PATTERN.match(body, position)
    if if_match is None:
        end = body.find(";", position)
        statement = body[position:] if end < 0 else body[position:end + 1]
        kind = None if "{" in statement else "plain"
        return kind, None, statement, len(body) if end < 0 else end + 1
    condition_end = find_matching_delimiter(body, if_match.end() - 1, "(", ")")
    if condition_end < 0:
        return None, None, "", len(body)
    condition = body[if_match.end():condition_end]
    block_match = BLOCK_OPEN_PATTERN.match(body, condition_end + 1)
    if block_match:
        block_end = find_matching_delimiter(body, block_match.end() - 1, "{", "}")
        if block_end < 0:
            return None, None, "", len(body)
        block, next_position = body[block_match.end():block_end], block_end + 1
    else:
        end = body.find(";", condition_end + 1)
        if end < 0:
            return None, None, "", len(body)
        block, next_position = body[condition_end + 1:end + 1], end + 1
    if ELSE_PATTERN.match(body, next_position):
        return None, None, "", len(body)
    return "if", condition, block, next_position


def evaluate_typed_uav_load_flag(function_body, dimension_values):
    if function_body is None or TYPED_UAV_LOAD_FLAG not in function_body:
        return False
    if FLAG_BODY_UNDECIDABLE_PATTERN.search(function_body):
        return None
    is_flag_added = False
    position = 0
    while function_body[position:].strip():
        kind, condition, block, position = read_statement(function_body, position)
        if kind is None:
            return None
        has_return = bool(RETURN_PATTERN.search(block))
        has_flag = TYPED_UAV_LOAD_FLAG in block
        if has_flag and not ADD_TYPED_UAV_LOAD_FLAG_PATTERN.search(block):
            return None
        if kind == "plain":
            if has_return:
                return is_flag_added
            is_flag_added = is_flag_added or has_flag
            continue
        if not has_return and not has_flag:
            continue
        if (has_return and has_flag) or NESTED_IF_PATTERN.search(block):
            return None
        is_condition_true = evaluate_permutation_condition(condition, dimension_values)
        if is_condition_true is None:
            return None
        if is_condition_true and has_return:
            return is_flag_added
        is_flag_added = is_flag_added or (is_condition_true and has_flag)
    return is_flag_added


def typed_uav_loads_allowed(cpp_text, class_name, dimension_values):
    for _ in range(MAXIMUM_BASE_CLASS_DEPTH):
        if class_name in ENGINE_SHADER_BASE_CLASSES:
            return False
        class_text, base_name = find_class_text(cpp_text, class_name)
        if class_text is None:
            return None
        function_body = find_function_body(class_text, "ModifyCompilationEnvironment")
        own_state = evaluate_typed_uav_load_flag(function_body, dimension_values)
        if own_state is not False:
            return own_state
        if base_name is None:
            return None
        if function_body is not None:
            if f"{base_name}::ModifyCompilationEnvironment" not in function_body:
                return False
            if PARENT_CALL_UNDECIDABLE_PATTERN.search(function_body):
                return None
        class_name = base_name
    return None


def count_uav_slots(disassembly):
    highest_slot = -1
    for line in UAV_DECLARATION_LINE_PATTERN.findall(disassembly):
        for match in UAV_REGISTER_PATTERN.finditer(line):
            highest_slot = max(highest_slot, int(match.group(1) or match.group(2)))
    return highest_slot + 1


def judge_fxc_output(succeeded, errors, disassembly, typed_uav_loads_state):
    problems = [] if succeeded else ["컴파일 오류: " + " | ".join(errors[:4])]
    uav_count = count_uav_slots(disassembly)
    if uav_count > FEATURE_LEVEL_11_0_MAXIMUM_UAVS:
        problems.append(f"UAV {uav_count}개(가장 높은 u 슬롯 + 1) > {FEATURE_LEVEL_11_0_MAXIMUM_UAVS}(기능 수준 11.0 한도)")
    if SIXTY_FOUR_UAV_SLOTS_PATTERN.search(disassembly):
        problems.append(f"'64 UAV slots' 표시(UAV 슬롯 {FEATURE_LEVEL_11_0_MAXIMUM_UAVS} 초과 요구)")
    if DOUBLE_PRECISION_FLAG in disassembly:
        problems.append(f"배정밀도 '{DOUBLE_PRECISION_FLAG}'(엔진 GD3DCheckForDoubles 가 거부)")
    has_typed_uav_load = TYPED_UAV_LOAD_MARKER in disassembly
    if has_typed_uav_load and typed_uav_loads_state is False:
        problems.append(f"'{TYPED_UAV_LOAD_MARKER}' 인데 C++ 가 이 순열에 {TYPED_UAV_LOAD_FLAG} 를 주지 않음")
    if has_typed_uav_load and typed_uav_loads_state is None:
        problems.append(f"'{TYPED_UAV_LOAD_MARKER}' 인데 C++ {TYPED_UAV_LOAD_FLAG} 판정 불가(판정 가능한 if·return·Add 형태로 바꿀 것)")
    return problems, uav_count


def compile_fxc(fxc_compiler, dxc_path, mirror_dir, source_path, source_name, entry_point, defines, preprocessed_path):
    preprocessed_source, errors = preprocess_for_fxc(dxc_path, mirror_dir, source_path, entry_point, defines, preprocessed_path)
    if preprocessed_source is None:
        return False, errors, ""
    succeeded, errors, disassembly = fxc_compiler.compile(preprocessed_source, source_name, entry_point)
    mirror_prefix = str(pathlib.Path(mirror_dir)).replace("\\", "\\\\") + "\\\\"
    return succeeded, [error.replace(mirror_prefix, "") for error in errors], disassembly


def check_fxc_permutation(fxc_compiler, dxc_path, mirror_dir, source_path, shader_class, names, values, summary):
    permutation_label = ",".join(f"{name}={value}" for name, value in zip(names, values)) or "-"
    label = f"{shader_class['class']} [{permutation_label}] FXC {FXC_TARGET}"
    defines = dict(FXC_PROFILE_DEFINES)
    defines.update(zip(names, values))
    preprocessed_path = pathlib.Path(mirror_dir).parent / "fxc" / f"{shader_class['class']}_{'_'.join(values) or 'none'}.usf"
    preprocessed_path.parent.mkdir(parents=True, exist_ok=True)
    succeeded, errors, disassembly = compile_fxc(
        fxc_compiler, dxc_path, mirror_dir, source_path, shader_class["path"], shader_class["entry"], defines, preprocessed_path)
    define_values = dict(zip(names, values))
    dimension_values = {dimension: int(define_values[define]) for dimension, define in shader_class["dimension_classes"].items() if define in define_values}
    typed_uav_loads_state = typed_uav_loads_allowed(shader_class["cpp_text"], shader_class["class"], dimension_values) if TYPED_UAV_LOAD_MARKER in disassembly else False
    problems, uav_count = judge_fxc_output(succeeded, errors, disassembly, typed_uav_loads_state)
    if not problems:
        summary["fxc_passed"] += 1
        print(f"PASS {label} uav={uav_count}")
        return
    summary["fxc_failed"] += 1
    summary["fxc_failures"].append(label + " :: " + " / ".join(problems))
    print(f"FAIL {label}")
    for problem in problems:
        print(f"    {problem}")


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
    shader_source_dir = root / SHADER_SOURCE_DIR
    summary = {"compiler": "", "compiler_version": "", "hlsl_versions": [], "passed": 0, "failed": 0, "failures": [], "unresolved_includes": [], "comment_lines": [],
               "fxc": "", "fxc_compiler": "", "fxc_passed": 0, "fxc_failed": 0, "fxc_failures": []}

    if not shader_source_dir.is_dir() or not module_shader_dir.is_dir() or not engine_shader_dir.is_dir():
        print(f"FAIL 입력 경로 없음: {shader_source_dir} / {module_shader_dir} / {engine_shader_dir}")
        print(json.dumps(summary, ensure_ascii=False))
        return 1

    fxc_compiler, summary["fxc"], summary["fxc_compiler"] = resolve_fxc_compiler(arguments.d3dcompiler, engine_dir, arguments.allow_missing_fxc)
    if summary["fxc"] == "unavailable":
        reason = "--d3dcompiler 명시 경로" if arguments.d3dcompiler else "건너뛰려면 --allow-missing-fxc"
        print(f"FAIL FXC 를 쓸 수 없음({reason}): {summary['fxc_compiler']}")
        print(json.dumps(summary, ensure_ascii=False))
        return FXC_UNAVAILABLE_EXIT_CODE
    if summary["fxc"] == "skipped":
        print(f"건너뜀(FXC 없음, --allow-missing-fxc): {summary['fxc_compiler']}")

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

        shader_sources = find_shader_sources(shader_source_dir)
        summary["shader_sources"] = [path.name for path in shader_sources]
        shader_classes = [shader_class for path in shader_sources for shader_class in parse_shader_classes(path.read_text(encoding="utf-8"))]
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
                if fxc_compiler:
                    check_fxc_permutation(fxc_compiler, dxc_path, mirror.mirror_dir, source_path, shader_class, names, values, summary)

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

    if summary["fxc"] == "ran":
        print(f"FXC {FXC_TARGET} 통과 {summary['fxc_passed']} 실패 {summary['fxc_failed']} ({summary['fxc_compiler']})")
    else:
        print(f"FXC {FXC_TARGET} 건너뜀(FXC 없음, --allow-missing-fxc): {summary['fxc_compiler']}")
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if summary["failed"] == 0 and summary["fxc_failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

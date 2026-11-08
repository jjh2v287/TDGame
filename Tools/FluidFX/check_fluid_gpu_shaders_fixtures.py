"""실행 환경: Windows 64비트 시스템 Python 3, 엔진 d3dcompiler_47.dll(FXC 사례)·dxc(검사기 본체와 같은 탐색, 전처리 사례)·git(고치기 전 트리 꺼내기), 에디터 불필요
실행 명령: python -B Tools/FluidFX/check_fluid_gpu_shaders_fixtures.py [--engine 엔진Engine폴더] [--d3dcompiler d3dcompiler_47.dll경로] [--allow-missing-fxc]
출력: 사례마다 PASS/FAIL 줄과 마지막 줄 `fixtures 통과/전체 fxc=ran|skipped`. 종료 코드 0 전부 통과 / 1 하나라도 틀림 / 2 FXC·dxc 를 쓸 수 없음(--allow-missing-fxc 면 기본 경로 dll 없음만 FXC 사례·트리 회귀를 건너뛰고 fxc=skipped, --d3dcompiler 로 명시한 경로 실패는 그래도 종료 2)
상태: 2026-11-08 FU-14 작성, 같은 날 반박 2회 반영. check_fluid_gpu_shaders.py 의 C++ CFLAG_AllowTypedUAVLoads 판정(typed_uav_loads_allowed: 부모 도달성·`#`·삼항·단일 &/|·연쇄 비교는 판정 불가), FXC 판정(judge_fxc_output: 가장 높은 u 슬롯 + 1·범위·`;`/`//` 64 UAV slots·배정밀도·형식 UAV 읽기), 순열 검사(check_fxc_permutation: X3058 실패·판정 불가 + 형식 UAV 읽기 실패), FXC 준비(resolve_fxc_compiler·본체 종료 2, 가짜 엔진 루트 포함), 전처리 진입점 확인(preprocess_for_fxc)을 합성 C++·HLSL·역어셈 조각으로 확인하고, 본체를 현재 트리(종료 0)와 고치기 전 셰이더 트리(커밋 2ca67122~1 을 git archive, FXC 실패 38 = WaterShift UAV 9·MultigridCoarseSolve X4026·PainterStep 형식 UAV 읽기 각 2 + 그 트리 Multigrid Down·Up 삼항식 X3058 16씩)로 한 번씩 돌린다(약 1~2분).
"""
import argparse
import collections
import contextlib
import importlib.util
import io
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile

sys.dont_write_bytecode = True

CHECKER_PATH = pathlib.Path(__file__).resolve().parent / "check_fluid_gpu_shaders.py"
FXC_UNAVAILABLE_EXIT_CODE = 2
PRE_FIX_COMMIT = "2ca67122~1"
PRE_FIX_TREE_PATHS = ("Source/TDFluidGpu", "Source/TDGame/FluidFX")
PRE_FIX_EXPECTED_FXC_FAILURES = {"FTDFluidGpuWaterShiftCS": 2, "FTDFluidGpuMultigridCoarseSolveCS": 2, "FTDFluidGpuPainterStepCS": 2,
                                 "FTDFluidGpuMultigridDownCS": 16, "FTDFluidGpuMultigridUpCS": 16}
PRE_FIX_TERNARY_ARRAY_CLASSES = ("FTDFluidGpuMultigridDownCS", "FTDFluidGpuMultigridUpCS")

PAINTER_LIKE_CPP = """
class FTDFluidGpuComputeShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};
class FTDFluidGpuSampleCS : public FTDFluidGpuComputeShader
{
	class FWritesVelocityDimension : SHADER_PERMUTATION_BOOL("TD_WRITES_VELOCITY");
	class FModeDimension : SHADER_PERMUTATION_INT("TD_MODE", 3);
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTDFluidGpuComputeShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (!PermutationVector.Get<FWritesVelocityDimension>())
		{
			return;
		}
		// OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads); comment must not count
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};
"""

BLOCK_CONDITION_CPP = """
class FTDFluidGpuBlockCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWritesVelocityDimension>() && PermutationVector.Get<FModeDimension>() != 2)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}
	}
};
"""

UNKNOWN_CONSTRUCT_CPP = """
class FTDFluidGpuElseCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		if (PermutationVector.Get<FWritesVelocityDimension>()) { OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads); } else { OutEnvironment.SetDefine(TEXT("X"), 1); }
	}
};
class FTDFluidGpuLocalVariableCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const bool bWritesVelocity = PermutationVector.Get<FWritesVelocityDimension>();
		if (!bWritesVelocity)
		{
			return;
		}
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};
class FTDFluidGpuOtherFileBaseCS : public FTDFluidGpuBaseFromHeader
{
};
class FTDFluidGpuPreprocessorCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
#if 0
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
#endif
	}
};
class FTDFluidGpuTernaryCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		PermutationVector.Get<FWritesVelocityDimension>() ? OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads) : void();
	}
};
class FTDFluidGpuBitwiseCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FModeDimension>() & 2 == 2)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}
	}
};
class FTDFluidGpuChainedComparisonCS : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FModeDimension>() == 2 == 2)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
		}
	}
};
"""

INHERITED_CPP = """
class FTDFluidGpuTypedBase : public FGlobalShader
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};
class FTDFluidGpuInheritsCS : public FTDFluidGpuTypedBase
{
};
class FTDFluidGpuCallsBaseCS : public FTDFluidGpuTypedBase
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTDFluidGpuTypedBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("Z"), 1);
	}
};
class FTDFluidGpuSkipsBaseCS : public FTDFluidGpuTypedBase
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("Y"), 1);
	}
};
class FTDFluidGpuGuardBeforeBaseCS : public FTDFluidGpuTypedBase
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWritesVelocityDimension>())
		{
			return;
		}
		FTDFluidGpuTypedBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
class FTDFluidGpuBaseInFalseIfCS : public FTDFluidGpuTypedBase
{
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (!PermutationVector.Get<FWritesVelocityDimension>())
		{
			FTDFluidGpuTypedBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}
	}
};
class FTDFluidGpuPlainCS : public FGlobalShader
{
};
"""

DIMENSIONS = {"FWritesVelocityDimension": "TD_WRITES_VELOCITY", "FModeDimension": "TD_MODE"}
VELOCITY_ON = {"FWritesVelocityDimension": 1}

CPP_CASES = [
    ("가드 return(부모 호출 뒤): 속도 0 이면 부모 도달 판정 불가", PAINTER_LIKE_CPP, "FTDFluidGpuSampleCS", {"FWritesVelocityDimension": 0, "FModeDimension": 0}, None),
    ("가드 return: 속도 1 이면 플래그 있음", PAINTER_LIKE_CPP, "FTDFluidGpuSampleCS", {"FWritesVelocityDimension": 1, "FModeDimension": 0}, True),
    ("블록 조건 && != 참", BLOCK_CONDITION_CPP, "FTDFluidGpuBlockCS", {"FWritesVelocityDimension": 1, "FModeDimension": 1}, True),
    ("블록 조건 && != 거짓", BLOCK_CONDITION_CPP, "FTDFluidGpuBlockCS", {"FWritesVelocityDimension": 1, "FModeDimension": 2}, False),
    ("블록 조건 차원 값 모름", BLOCK_CONDITION_CPP, "FTDFluidGpuBlockCS", VELOCITY_ON, None),
    ("else 는 판정 불가", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuElseCS", VELOCITY_ON, None),
    ("지역 변수 조건은 판정 불가", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuLocalVariableCS", VELOCITY_ON, None),
    ("다른 파일 부모는 판정 불가", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuOtherFileBaseCS", {}, None),
    ("#if 0 안의 Add 는 판정 불가", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuPreprocessorCS", {}, None),
    ("삼항식 안의 Add 는 판정 불가", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuTernaryCS", VELOCITY_ON, None),
    ("조건의 단일 & 는 판정 불가(C++ 는 & 가 == 보다 늦게 묶임)", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuBitwiseCS", {"FModeDimension": 2}, None),
    ("조건의 연쇄 비교는 판정 불가(C++ 는 왼쪽부터 묶임)", UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuChainedComparisonCS", {"FModeDimension": 2}, None),
    ("같은 파일 부모의 무조건 Add 상속", INHERITED_CPP, "FTDFluidGpuInheritsCS", {}, True),
    ("곧은 몸통의 자식이 부모를 부르면 상속", INHERITED_CPP, "FTDFluidGpuCallsBaseCS", {}, True),
    ("자기 ModifyCompilationEnvironment 가 부모를 안 부르면 상속 없음", INHERITED_CPP, "FTDFluidGpuSkipsBaseCS", {}, False),
    ("부모 호출 앞 가드 return 자식은 판정 불가", INHERITED_CPP, "FTDFluidGpuGuardBeforeBaseCS", VELOCITY_ON, None),
    ("거짓 if 안 부모 호출 자식은 판정 불가", INHERITED_CPP, "FTDFluidGpuBaseInFalseIfCS", VELOCITY_ON, None),
    ("FGlobalShader 만 상속하면 플래그 없음", INHERITED_CPP, "FTDFluidGpuPlainCS", {}, False),
]

TYPED_UAV_LOAD_NOTE = "Typed UAV Load Additional Formats"
SIXTY_FOUR_UAV_SLOTS_NOTE = "64 UAV slots"
DOUBLE_PRECISION_FLAG_TEXT = "enableDoublePrecisionFloatOps"


def make_fxc_disassembly(notes, declarations, comment_prefix="//"):
    note_lines = [f"{comment_prefix} Note: shader requires additional functionality:"] + [f"{comment_prefix}       {note}" for note in notes] + [comment_prefix] if notes else []
    return "\n".join([comment_prefix] + note_lines + ["cs_5_0", "dcl_globalFlags refactoringAllowed"] + declarations + ["ret"]) + "\n"


NINE_UAV_HLSL = "".join(f"RWStructuredBuffer<uint> Output{index};\n" for index in range(9)) + (
    "[numthreads(64,1,1)] void MainCS(uint GroupIndex : SV_GroupIndex)\n{\n"
    + "".join(f"\tOutput{index}[GroupIndex] = {index}u;\n" for index in range(9)) + "}\n")

SINGLE_HIGH_REGISTER_HLSL = """RWStructuredBuffer<uint> Output : register(u8);
[numthreads(64,1,1)] void MainCS(uint GroupIndex : SV_GroupIndex) { Output[GroupIndex] = GroupIndex; }
"""

PARTLY_USED_ARRAY_HLSL = """RWStructuredBuffer<uint> Outputs[9];
[numthreads(64,1,1)] void MainCS(uint GroupIndex : SV_GroupIndex) { Outputs[8][GroupIndex] = GroupIndex; Outputs[0][GroupIndex] = 1u; }
"""

DOUBLE_PRECISION_HLSL = """RWStructuredBuffer<float> Output;
[numthreads(16,1,1)] void MainCS(uint GroupIndex : SV_GroupIndex) { double Scaled = (double)GroupIndex * 1.0000001; Output[GroupIndex] = (float)(Scaled * Scaled); }
"""

VARYING_SYNC_HLSL = """groupshared uint SharedFlags[3];
RWStructuredBuffer<uint> Output;
[numthreads(64,1,1)] void MainCS(uint GroupIndex : SV_GroupIndex)
{
	uint Value = GroupIndex;
	[loop] for (uint LoopIndex = 0; LoopIndex < 16u; ++LoopIndex)
	{
		if (GroupIndex == 0u) { SharedFlags[LoopIndex % 3u] = 0u; }
		GroupMemoryBarrierWithGroupSync();
		if (Value > 3u) { InterlockedOr(SharedFlags[LoopIndex % 3u], 1u); Value >>= 1u; }
		GroupMemoryBarrierWithGroupSync();
		if (SharedFlags[LoopIndex % 3u] == 0u) { break; }
	}
	Output[GroupIndex] = Value;
}
"""

TYPED_LOAD_HLSL = """RWTexture2DArray<float2> Velocity;
[numthreads(8,8,1)] void MainCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	Velocity[DispatchThreadId] = Velocity[DispatchThreadId] * 2.0f;
}
"""

TERNARY_ARRAY_HLSL = """groupshared float SharedValue[(8 > 4 ? 8 : 4) * 2];
RWStructuredBuffer<float> Output;
[numthreads(16,1,1)] void MainCS(uint GroupIndex : SV_GroupIndex)
{
	SharedValue[GroupIndex] = GroupIndex;
	GroupMemoryBarrierWithGroupSync();
	Output[GroupIndex] = SharedValue[15u - GroupIndex];
}
"""

ENTRY_POINT_HIDDEN_HLSL = """RWStructuredBuffer<uint> Output;
#if 0
[numthreads(1,1,1)] void MainCS() { Output[0] = 1u; }
#endif
[numthreads(1,1,1)] void OtherCS() { Output[0] = 2u; }
"""

ENTRY_POINT_PRESENT_HLSL = """RWStructuredBuffer<uint> Output;
[numthreads(8,1,1)]
void MainCS(uint GroupIndex : SV_GroupIndex) { Output[GroupIndex] = 1u; }
"""


def load_checker():
    specification = importlib.util.spec_from_file_location("check_fluid_gpu_shaders", CHECKER_PATH)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def run_case(title, check):
    try:
        is_correct, detail = check()
    except Exception as error:
        is_correct, detail = False, f"{type(error).__name__}: {error}"
    return title, is_correct, detail


def judge_disassembly(checker, succeeded, errors, disassembly, typed_uav_loads_state):
    outcome = checker.judge_fxc_output(succeeded, errors, disassembly, typed_uav_loads_state)
    return outcome[0], outcome[-1]


def run_cpp_cases(checker):
    results = []
    for title, cpp_source, class_name, dimension_values, expected in CPP_CASES:
        cpp_text = checker.CPP_COMMENT_PATTERN.sub("", cpp_source)
        actual = checker.typed_uav_loads_allowed(cpp_text, class_name, dimension_values)
        results.append((f"C++ {title}", actual == expected, f"기대 {expected} 실제 {actual}"))
    dimension_classes = dict(checker.DIMENSION_CLASS_PATTERN.findall(checker.CPP_COMMENT_PATTERN.sub("", PAINTER_LIKE_CPP)))
    results.append(("C++ 순열 차원 클래스 → 정의 이름", dimension_classes == DIMENSIONS, f"실제 {dimension_classes}"))
    return results


def run_disassembly_text_cases(checker):
    def expect_problem(disassembly, expected_uav_count, expected_problem_text):
        problems, uav_count = judge_disassembly(checker, True, [], disassembly, False)
        return uav_count == expected_uav_count and any(expected_problem_text in problem for problem in problems), f"uav={uav_count} {problems}"

    single_declaration = ["dcl_uav_structured u0, 4"]
    range_declarations = single_declaration + ["dcl_uav_typed_texture2d (uint,uint,uint,uint) U1[3:8], space=0"]
    cases = [
        ("역어셈 '// 64 UAV slots'(FXC 앞머리)는 UAV 슬롯 1개여도 실패", lambda: expect_problem(make_fxc_disassembly([SIXTY_FOUR_UAV_SLOTS_NOTE], single_declaration), 1, SIXTY_FOUR_UAV_SLOTS_NOTE)),
        ("역어셈 '; 64 UAV slots'(DXIL 앞머리)도 실패", lambda: expect_problem(make_fxc_disassembly([SIXTY_FOUR_UAV_SLOTS_NOTE], single_declaration, ";"), 1, SIXTY_FOUR_UAV_SLOTS_NOTE)),
        ("dcl_uav 범위 U1[3:8] 은 끝 슬롯 8 → UAV 9 로 실패", lambda: expect_problem(make_fxc_disassembly([], range_declarations), 9, "UAV 9")),
    ]
    return [run_case(title, check) for title, check in cases]


def find_project_root():
    completed = subprocess.run(["git", "-C", str(CHECKER_PATH.parent), "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True, timeout=60)
    return pathlib.Path(completed.stdout.strip())


def run_checker_process(arguments, timeout_seconds):
    completed = subprocess.run([sys.executable, "-B", str(CHECKER_PATH), *arguments], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout_seconds)
    last_line = next((line for line in reversed(completed.stdout.splitlines()) if line.strip()), "")
    try:
        summary = json.loads(last_line)
    except json.JSONDecodeError:
        summary = {}
    return completed.returncode, summary, (completed.stdout + completed.stderr)[-300:]


def run_fxc_resolution_cases(checker, engine_dir, scratch_dir):
    empty_engine_dir = scratch_dir / "engine_without_d3dcompiler"
    (empty_engine_dir / "Shaders").mkdir(parents=True, exist_ok=True)
    missing_library = str(scratch_dir / "missing" / "d3dcompiler_47.dll")

    def expect_status(explicit_path, engine, allow_missing, expected_status):
        compiler, status, description = checker.resolve_fxc_compiler(explicit_path, engine, allow_missing)
        return compiler is None and status == expected_status, f"기대 {expected_status} 실제 {status} {description}"

    def expect_process_unavailable(arguments):
        exit_code, summary, tail = run_checker_process(["--root", str(find_project_root()), *arguments], 900)
        return exit_code == FXC_UNAVAILABLE_EXIT_CODE and summary.get("fxc") == "unavailable", f"종료 {exit_code} fxc={summary.get('fxc')} :: {tail}"

    cases = [
        ("FXC 준비: 기본 경로 dll 없음은 unavailable(본체 종료 2)", lambda: expect_status("", str(empty_engine_dir), False, "unavailable")),
        ("FXC 준비: 기본 경로 dll 없음 + --allow-missing-fxc 는 skipped", lambda: expect_status("", str(empty_engine_dir), True, "skipped")),
        ("FXC 준비: --d3dcompiler 명시 경로 실패는 --allow-missing-fxc 여도 unavailable", lambda: expect_status(missing_library, str(engine_dir), True, "unavailable")),
        ("본체: 가짜 엔진 루트(Shaders 만, dll 없음, --d3dcompiler 없음)는 종료 2·fxc=unavailable", lambda: expect_process_unavailable(["--engine", str(empty_engine_dir)])),
        ("본체: --d3dcompiler 틀린 경로는 종료 2·fxc=unavailable", lambda: expect_process_unavailable(["--engine", str(engine_dir), "--d3dcompiler", missing_library])),
        ("본체: --d3dcompiler 틀린 경로 + --allow-missing-fxc 도 종료 2·fxc=unavailable",
         lambda: expect_process_unavailable(["--engine", str(engine_dir), "--d3dcompiler", missing_library, "--allow-missing-fxc"])),
    ]
    return [run_case(title, check) for title, check in cases]


def write_hlsl(scratch_dir, name, hlsl):
    source_path = scratch_dir / f"{name}.usf"
    source_path.write_text(hlsl, encoding="ascii")
    (scratch_dir / "out").mkdir(exist_ok=True)
    return source_path


def run_preprocess_cases(checker, dxc_path, scratch_dir):
    def expect_preprocess(name, hlsl, is_entry_point_expected):
        source_path = write_hlsl(scratch_dir, name, hlsl)
        preprocessed_source, errors = checker.preprocess_for_fxc(dxc_path, scratch_dir, source_path, "MainCS", {}, scratch_dir / "out" / f"{name}.usf")
        if is_entry_point_expected:
            return preprocessed_source is not None and not errors, f"{errors}"
        return preprocessed_source is None and any("진입점" in error for error in errors), f"source={'있음' if preprocessed_source else '없음'} {errors}"

    cases = [
        ("전처리: 산출물에 진입점이 없으면 실패", lambda: expect_preprocess("entry_hidden", ENTRY_POINT_HIDDEN_HLSL, False)),
        ("전처리: 속성 다음 줄의 void MainCS( 는 진입점으로 인정", lambda: expect_preprocess("entry_present", ENTRY_POINT_PRESENT_HLSL, True)),
    ]
    return [run_case(title, check) for title, check in cases]


def run_fxc_cases(checker, compiler):
    def judge_compiled(hlsl, typed_uav_loads_state):
        succeeded, errors, disassembly = compiler.compile(hlsl.encode("ascii"), "fixture.usf", "MainCS")
        problems, uav_count = judge_disassembly(checker, succeeded, errors, disassembly, typed_uav_loads_state)
        return succeeded, errors, problems, uav_count

    def high_uav_slot(hlsl):
        succeeded, _, problems, uav_count = judge_compiled(hlsl, False)
        return succeeded and uav_count == 9 and any("UAV 9" in problem for problem in problems), f"uav={uav_count} {problems}"

    def varying_sync():
        succeeded, errors, problems, _ = judge_compiled(VARYING_SYNC_HLSL, False)
        return not succeeded and "X4026" in " ".join(errors) and bool(problems), f"{errors[:1]}"

    def typed_load(typed_uav_loads_state, is_failure_expected):
        _, _, problems, _ = judge_compiled(TYPED_LOAD_HLSL, typed_uav_loads_state)
        return bool(problems) == is_failure_expected, f"{problems}"

    def double_precision():
        succeeded, _, problems, _ = judge_compiled(DOUBLE_PRECISION_HLSL, False)
        return succeeded and any(DOUBLE_PRECISION_FLAG_TEXT in problem for problem in problems), f"{problems}"

    cases = [
        ("FXC (A) UAV 9개는 실패", lambda: high_uav_slot(NINE_UAV_HLSL)),
        ("FXC (A) register(u8) 하나만 선언해도 슬롯 9 로 실패", lambda: high_uav_slot(SINGLE_HIGH_REGISTER_HLSL)),
        ("FXC (A) 배열 Outputs[9] 의 [0]·[8] 만 써도 슬롯 9 로 실패", lambda: high_uav_slot(PARTLY_USED_ARRAY_HLSL)),
        ("FXC (B) X4026 은 실패", varying_sync),
        ("FXC (C) 형식 있는 UAV 읽기 + 플래그 없음은 실패", lambda: typed_load(False, True)),
        ("FXC (C) 형식 있는 UAV 읽기 + 플래그 있음은 통과", lambda: typed_load(True, False)),
        ("FXC (C) 형식 있는 UAV 읽기 + 플래그 판정 불가는 실패(엄격 기본)", lambda: typed_load(None, True)),
        ("FXC (D) double 사용은 실패", double_precision),
    ]
    return [run_case(title, check) for title, check in cases]


def run_permutation_cases(checker, compiler, dxc_path, scratch_dir):
    mirror_dir = scratch_dir / "permutation_mirror"
    mirror_dir.mkdir(parents=True, exist_ok=True)

    def check_permutation(name, hlsl, cpp_source, class_name, expected_problem_text):
        source_path = mirror_dir / f"{name}.usf"
        source_path.write_text(hlsl, encoding="ascii")
        shader_class = {"class": class_name, "path": f"{name}.usf", "entry": "MainCS", "dimensions": [("TD_WRITES_VELOCITY", ["0", "1"])],
                        "cpp_text": checker.CPP_COMMENT_PATTERN.sub("", cpp_source), "dimension_classes": {"FWritesVelocityDimension": "TD_WRITES_VELOCITY"}}
        summary = collections.defaultdict(list, fxc_passed=0, fxc_failed=0)
        with contextlib.redirect_stdout(io.StringIO()):
            checker.check_fxc_permutation(compiler, dxc_path, mirror_dir, source_path, shader_class, ["TD_WRITES_VELOCITY"], ("1",), summary)
        failures = " ".join(summary["fxc_failures"])
        return summary["fxc_failed"] == 1 and summary["fxc_passed"] == 0 and expected_problem_text in failures, f"통과 {summary['fxc_passed']} 실패 {summary['fxc_failed']} {failures[-200:]}"

    cases = [
        ("순열 검사: X3058(배열 크기 삼항식)은 실패", lambda: check_permutation("ternary_array", TERNARY_ARRAY_HLSL, INHERITED_CPP, "FTDFluidGpuPlainCS", "X3058")),
        ("순열 검사: 부모 호출 앞 가드 return 자식 + 형식 UAV 읽기는 실패",
         lambda: check_permutation("guard_before_base", TYPED_LOAD_HLSL, INHERITED_CPP, "FTDFluidGpuGuardBeforeBaseCS", "판정 불가")),
        ("순열 검사: 거짓 if 안 부모 호출 + 형식 UAV 읽기는 실패", lambda: check_permutation("base_in_false_if", TYPED_LOAD_HLSL, INHERITED_CPP, "FTDFluidGpuBaseInFalseIfCS", "판정 불가")),
        ("순열 검사: 판정 불가 클래스(else) + 형식 UAV 읽기는 실패", lambda: check_permutation("undecidable_else", TYPED_LOAD_HLSL, UNKNOWN_CONSTRUCT_CPP, "FTDFluidGpuElseCS", "판정 불가")),
    ]
    return [run_case(title, check) for title, check in cases]


def extract_pre_fix_tree(target_dir):
    archive = subprocess.run(["git", "-C", str(find_project_root()), "archive", "--format=tar", PRE_FIX_COMMIT, *PRE_FIX_TREE_PATHS], capture_output=True, check=True, timeout=300)
    with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as archive_file:
        archive_file.extractall(target_dir, filter="data")


def run_tree_cases(engine_dir, d3dcompiler_path, scratch_dir):
    engine_arguments = ["--engine", str(engine_dir)] + (["--d3dcompiler", d3dcompiler_path] if d3dcompiler_path else [])

    def current_tree():
        exit_code, summary, tail = run_checker_process(["--root", str(find_project_root()), *engine_arguments], 3600)
        is_correct = exit_code == 0 and summary.get("fxc") == "ran" and summary.get("failed") == 0 and summary.get("fxc_failed") == 0 and summary.get("fxc_passed", 0) > 0
        return is_correct, f"종료 {exit_code} fxc={summary.get('fxc')} dxc실패={summary.get('failed')} fxc통과={summary.get('fxc_passed')} fxc실패={summary.get('fxc_failed')} :: {tail}"

    def pre_fix_tree():
        tree_dir = scratch_dir / "pre_fix_tree"
        extract_pre_fix_tree(tree_dir)
        exit_code, summary, tail = run_checker_process(["--root", str(tree_dir), *engine_arguments], 3600)
        fxc_failures = summary.get("fxc_failures", [])
        failure_classes = dict(collections.Counter(failure.split(" [", 1)[0] for failure in fxc_failures))
        is_ternary_only_x3058 = all("X3058" in failure for failure in fxc_failures if failure.split(" [", 1)[0] in PRE_FIX_TERNARY_ARRAY_CLASSES)
        is_correct = (exit_code == 1 and summary.get("fxc") == "ran" and summary.get("failed") == 0
                      and failure_classes == PRE_FIX_EXPECTED_FXC_FAILURES and is_ternary_only_x3058)
        return is_correct, f"종료 {exit_code} fxc={summary.get('fxc')} dxc실패={summary.get('failed')} fxc실패={summary.get('fxc_failed')} {failure_classes} X3058만={is_ternary_only_x3058} :: {tail}"

    return [run_case("현재 트리 --root 회귀: 종료 0·FXC 실패 0(순열 차원 값 전달)", current_tree),
            run_case(f"고치기 전 트리({PRE_FIX_COMMIT}) --root 회귀: FXC 실패 38(기존 6 + Multigrid 삼항식 X3058 32)", pre_fix_tree)]


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    checker = load_checker()
    parser = argparse.ArgumentParser(description="check_fluid_gpu_shaders.py 판정 함수 fixture")
    parser.add_argument("--engine", default=os.environ.get("UE_ENGINE_DIR", checker.DEFAULT_ENGINE_DIR))
    parser.add_argument("--d3dcompiler", default="")
    parser.add_argument("--allow-missing-fxc", action="store_true")
    arguments = parser.parse_args()

    compiler, description = checker.load_fxc_compiler(arguments.d3dcompiler, arguments.engine)
    if not compiler and (arguments.d3dcompiler or not arguments.allow_missing_fxc):
        print(f"FAIL FXC 를 쓸 수 없음({'--d3dcompiler 명시 경로' if arguments.d3dcompiler else '건너뛰려면 --allow-missing-fxc'}): {description}")
        print("fixtures 0/0 fxc=unavailable")
        return FXC_UNAVAILABLE_EXIT_CODE
    if not compiler:
        print(f"건너뜀(FXC 없음, --allow-missing-fxc): {description}")

    scratch_dir = pathlib.Path(tempfile.mkdtemp(prefix="td_fluid_gpu_shader_fixtures_"))
    try:
        dxc_path, _, _ = checker.find_compiler("", arguments.engine, scratch_dir)
        if not dxc_path:
            print("FAIL dxc.exe 를 찾지 못함")
            print(f"fixtures 0/0 fxc={'ran' if compiler else 'skipped'}")
            return FXC_UNAVAILABLE_EXIT_CODE
        results = run_cpp_cases(checker)
        results += run_disassembly_text_cases(checker)
        results += run_fxc_resolution_cases(checker, arguments.engine, scratch_dir)
        results += run_preprocess_cases(checker, dxc_path, scratch_dir)
        if compiler:
            results += run_fxc_cases(checker, compiler)
            results += run_permutation_cases(checker, compiler, dxc_path, scratch_dir)
            results += run_tree_cases(arguments.engine, arguments.d3dcompiler, scratch_dir)
    finally:
        shutil.rmtree(scratch_dir, ignore_errors=True)

    for title, is_correct, detail in results:
        print(f"{'PASS' if is_correct else 'FAIL'} {title}" + ("" if is_correct else f" :: {detail}"))
    passed = sum(1 for _, is_correct, _ in results if is_correct)
    print(f"fixtures {passed}/{len(results)} fxc={'ran' if compiler else 'skipped'}")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())

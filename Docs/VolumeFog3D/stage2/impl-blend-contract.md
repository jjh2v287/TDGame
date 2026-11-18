# 바람 섞기 새 식(B4) GPU 반영 — 구현 계약

작성: 2026-11-17 claude(Opus 5.5 세션, 감독). 통과 기준은 잠긴 `prereg-blend-impl.md`(BG-1~9)다. 이 계약은 잠그지 않으며, 바꾸면 끝의 '바뀐 것'에 적는다. 코드 지도: `Saved/AgentOps/20261117/fog3d-blend-gpu/maps.md`. 식의 정의: `prereg-blend.md` 2절, 기준 계산 구현 `Tools/FluidFX/fog3d_ref/fog3d_blend_patch.py` 의 `factor_b4`.

## 0. 공통 규칙

- 장부(NOW·Worklog·progress·decisions·RESUME·Lessons·Tools/README)는 쓰지 않는다. 커밋·브랜치 변경·되돌리기 금지. 기존 에셋(.uasset·.umap)을 만들거나 저장하지 않는다.
- 잠긴 파일은 고치지 않는다: `Tools/FluidFX/fog3d_ref/` 의 기존 `fog3d_*.py`(새 일은 새 파일), `Docs/VolumeFog3D/stage2/prereg-*.md`, `Docs/VolumeFog3D/measurements/` 의 기존 파일.
- AGENTS.md 의 이름·흐름 규칙: 클래스·구조체에 TD, 동사형 함수, 명사형 변수, 불리언은 b 접두, 새 코드에 주석 없음, 가드 절·조기 리턴. 셰이더도 같은 이름 규칙(기존 `Fog3D` 꼴).
- **스위치를 끈 길은 지금과 같은 계산이어야 한다**(BG-1 이 GPU 덤프를 바이트로 견준다). 지금 식의 셰이더 줄 `return FaceWind + (SphereWindComponent - FaceWind) * (BrushWeight * WindStrength);` 는 글자 그대로 두고, 끈 길의 C++ 속도 셈에 곱셈 하나도 더하지 않는다.
- 빌드·시험은 `python Tools/build_and_test.py …`(포그라운드, 제한 시간 넉넉히). 명령이 실패하면 플래그만 바꿔 다시 돌리기 전에 `rg -n "<오류 핵심 문구>" Docs/Lessons Tools/README.md` 를 먼저 본다.

## 1. 게임 쪽 데이터 (TDGame 모듈)

1. `Source/TDGame/FluidFX/TDFog3DTypes.h` 의 `FTDFog3DSettings` 에 셋을 더한다(모두 `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")`):
   - `float BladeWindGain = 1.0f;` — `BladeWindStrength` 바로 뒤
   - `float BodyWindGain = 1.0f;` — `BodyWindStrength` 바로 뒤
   - `bool bUsesClampedTimeWindBlend = false;` — `MaximumInputSpeedCmPerSec` 바로 뒤
2. `TDFog3DTypes.cpp`: 유한성 표(`AppendFiniteProblems` 의 표)에 두 배율, `RequireAtLeast(…, 0.0)` 에 두 배율.
3. 정본 설정 `Content/FluidFX/PresetData/TDFluidPresetTuning.json` 의 `TDFog3DSettings` 블록에 같은 자리 차례로 `"BladeWindGain": 1.0`, `"BodyWindGain": 1.0`, `"bUsesClampedTimeWindBlend": false` 를 넣는다(값은 기본값 그대로, 들여쓰기·줄 끝 형식은 파일 그대로, 다른 줄은 건드리지 않는다).
4. `TDFog3DLayer.h` 의 `FTDFog3DInputSample` 에 `float WindGain = 1.0f;`(`WindStrength` 뒤).
5. `TDFog3DZoneActor`: `AppendBodySample`·`AppendBladeSamples` 가 배율을 받아 `Sample.WindGain` 에 넣는다. `BuildInputSamples` 는 몸 표본에 `InSettings.BodyWindGain`, 칼 표본에 `InSettings.BladeWindGain` 을 넘긴다.
6. `FTDFog3DLayer::PackSpheres`: 매개변수 `bool bAppliesWindGain` 을 더한다(호출하는 곳은 `Settings.bUsesClampedTimeWindBlend` 를 넘김). `if (bHasElapsedTime)` 블록 안에서 위치 차로 속도를 셈한 **바로 뒤, 상한 검사 앞**에 `if (bAppliesWindGain) { Velocity *= static_cast<double>(Input.WindGain); }`. 상한 블록은 그대로(상한이 배율 뒤 — 등록 1절의 결정).
7. 단계 입력을 묶는 곳(`TDFog3DLayer.cpp` 의 FrameInput 조립, 땅 묶음 `PackGroundStep` 근처)에서 `OutFrameInput.bUsesClampedTimeWindBlend = Settings.bUsesClampedTimeWindBlend;`.
8. 로그 한 줄: 설정이 적용될 때(층 구성 `Configure` 나 존 액터의 설정 적용, 바뀔 때만 — 매 프레임 아님) 기존 로그 범주로 `Fog3D settings: quality=<단계> resolution=<칸> stepsPerSecond=<값> clampedTimeWindBlend=<0|1> bodyWindGain=<값> bladeWindGain=<값> followsGround=<0|1>`. 촬영이 실행마다 조건을 확인하는 데 쓴다(등록 6절).
9. 스위치가 바뀌어도 상태를 다시 시작하지 않는다(바람 장은 어느 식에서나 유효한 상태다).

## 2. GPU 모듈 (TDFluidGpu)

1. `Public/TDFluidGpuFog3D.h`: `FTDFluidGpuFog3DFrameInput` 에 `bool bUsesClampedTimeWindBlend = false;`(`bFollowsGround` 뒤). 상수 `constexpr double TDFluidGpuFog3DWindBlendReferenceStepsPerSecond = 60.0;`, `constexpr double TDFluidGpuFog3DWindBlendUnitStepTolerance = 1.0e-6;`(시험의 기대값 계산도 이 상수를 쓴다).
2. `Private/TDFluidGpuFog3D.cpp` 의 `FTDFluidGpuFog3DAdvectCS` 매개변수에 `SHADER_PARAMETER(uint32, ClampedTimeWindBlend)`, `SHADER_PARAMETER(float, WindBlendStepRepeat)`, `SHADER_PARAMETER(uint32, WindBlendIsUnitStep)`. 채우는 곳(`AddFog3DAdvectPass` 또는 `FillFog3DAdvectFactors`):
   ```cpp
   const double WindBlendStepRepeat = TDFluidGpuFog3DWindBlendReferenceStepsPerSecond * static_cast<double>(FrameInput.DeltaSeconds);
   Parameters->ClampedTimeWindBlend = FrameInput.bUsesClampedTimeWindBlend ? 1u : 0u;
   Parameters->WindBlendStepRepeat = static_cast<float>(WindBlendStepRepeat);
   Parameters->WindBlendIsUnitStep = FMath::Abs(WindBlendStepRepeat - 1.0) <= TDFluidGpuFog3DWindBlendUnitStepTolerance ? 1u : 0u;
   ```
3. `Shaders/Private/TDFluidGpuFog3D.usf`: 전역 선언(다른 advect 매개변수 곁) `uint ClampedTimeWindBlend; float WindBlendStepRepeat; uint WindBlendIsUnitStep;`. 함수:
   ```hlsl
   float ComputeFog3DLogOnePlus(float Value)
   {
   	float OnePlusValue = 1.0f + Value;
   	if (OnePlusValue == 1.0f)
   	{
   		return Value;
   	}
   	return log(OnePlusValue) * Value / (OnePlusValue - 1.0f);
   }

   float ComputeFog3DExpMinusOne(float Value)
   {
   	float ExpValue = exp(Value);
   	if (ExpValue == 1.0f)
   	{
   		return Value;
   	}
   	float ExpValueMinusOne = ExpValue - 1.0f;
   	if (ExpValueMinusOne == -1.0f)
   	{
   		return -1.0f;
   	}
   	return ExpValueMinusOne * Value / log(ExpValue);
   }

   float ComputeFog3DClampedTimeBlendFactor(float WeightStrength)
   {
   	float ClampedFactor = min(WeightStrength, 1.0f);
   	if (WindBlendIsUnitStep != 0u || ClampedFactor >= 1.0f)
   	{
   		return ClampedFactor;
   	}
   	return -ComputeFog3DExpMinusOne(WindBlendStepRepeat * ComputeFog3DLogOnePlus(-ClampedFactor));
   }
   ```
   `BlendFog3DFaceWind` 는 가드 뒤에 `if (ClampedTimeWindBlend == 0u) { return <지금 식 줄 그대로>; }` 를 두고, 그 아래에 `return FaceWind + (SphereWindComponent - FaceWind) * ComputeFog3DClampedTimeBlendFactor(BrushWeight * WindStrength);`. 땅 따라가기 켬·끔 어느 길에서나 같은 함수가 쓰여야 한다(BG-6). 배율은 셰이더가 아니라 C++ 이 속도에 실어 보낸다(속도는 섞기 목표로만 쓰이고 윗면 목표는 속도에 대해 일차식이라 G × 목표가 된다).
4. `python Tools/FluidFX/check_fluid_gpu_shaders.py` 통과(SM5 FXC `cs_5_0` 포함).
5. 끔 길이 바이트로 달라지면(BG-1 실패) 먼저 원인을 찾는다. 컴파일러가 끔 길의 산술을 바꾼 탓이면 지금 식 길을 컴파일 시점 갈래(순열 차원 하나)로 나누는 것을 감독에게 제안하고 멈춘다(status=blocked).

## 3. 재생 파일 꼴과 GPU 시험 (TDFluidGpu Tests)

1. 재생 JSON(`fog3d-replay-1`)에 선택 키 둘:
   - 최상위 `"wind_blend"`: `"B0"`(없을 때 기본) 또는 `"B4"`. 그 밖의 값은 읽기 오류.
   - 구마다 `"g"`: 바람 배율(없을 때 1.0). `wind_blend` 가 B0 인데 g ≠ 1 이면 읽기 오류(기준 계산도 B0 에 배율을 막는다).
   기존 재생 파일에는 이 키가 없으므로 지금과 같게 돈다.
2. 재생 → 단계 입력: `FrameInput.bUsesClampedTimeWindBlend = (wind_blend == B4)`. B4 일 때만 구의 속도를 `static_cast<float>(v_cm_s × g)`(64비트 곱 뒤 32비트)로, B0 은 지금 그대로(곱셈 없음). 재생 길에는 속도 상한이 없다(지금처럼).
3. 새 시험 `TDGame.FluidFXRhi.Fog3D.ClampedTimeBlendReplayDumps`(새 파일 `Private/Tests/TDFluidGpuFog3DClampedTimeBlendRhiTests.cpp`, 본보기 `TDFluidGpuFog3DBlendProbeRhiTests.cpp`): `Tools/FluidFX/fog3d_ref/replays_blend_b4/manifest.json` 의 `files[].name` 차례로 돌려 `Saved/FluidFX/Fog3D/gpu_blend_b4/` 에 덤프를 쓴다(시작할 때 폴더를 비움, 이름 `<장면>__<덤프>`, 기존 덤프 파일 꼴 그대로 — `.json`·`.f32`·`.column.bin`·`.frames.bin`). 매니페스트가 없으면 '재생 묶음 없음'으로 실패. `files[]` 의 항목에 `"ground_off_twin": true` 가 있으면 같은 재생을 땅 따라가기를 끈 채(단계 입력의 `bFollowsGround = false`, 땅 자원 없이) 한 번 더 돌려, 덤프 걸음마다 3차원 장 u v w dx dy dz g 와 유체 표시를 땅 켬 실행과 **비트로** 견주고 다른 칸 수를 장마다 로그에 적는다 — 하나라도 다르면 실패(BG-6). 땅 끔 실행은 파일로 쓰지 않는다.
4. BG-7: `GroundPlaneBlendCases`(TDFluidGpuFog3DGroundRhiTests.cpp) 와 같은 꼴의 새 시험 `TDGame.FluidFXRhi.Fog3D.ClampedTimeBlendPlaneCases` — 세기 1.0·2.5·3.0 × dt 1/60·1/30·1/144 × 배율 1.0·1.25. 기대값은 시험 코드 안의 64비트 B4 식(`std::log1p`·`std::expm1`, `factor_b4` 와 같은 갈래: 64비트로 |60·dt − 1| ≤ 1e-6 이면 자른 값 그대로, 자른 값이 1 이면 1)과 목표 G × 속도. 허용은 `GroundPlaneBlendCases` 의 것을 그대로 쓴다. 기존 시험은 그대로 둔다.
5. 시험에 `NonNullRHI` 등 기존 RHI 시험의 플래그를 같게 준다(셰이더 검사기가 본다).

## 4. 게임 쪽 단위 시험 (BG-4·BG-8)

- `TDGame.FluidFX.Fog3D.SpherePacking`(또는 같은 파일의 새 시험): (가) 스위치 끔 + 배율 1.25 묶음 = 배율 1.0 묶음(구 구조체 바이트까지) (나) 스위치 켬 + 배율 1.25 → 속도 세 성분 = 1.25 × 원래(상대 1e-6) (다) 스위치 켬 + 배율 2 + 원래 속도 2000cm/s → 크기 3000(허용 1e-3).
- 설정 시험: 기본값(false·1.0·1.0), 음수·NaN 배율이 문제로 잡힘. 정본 JSON 의 세 값이 기본값과 같음(기존 설정 표 시험이 정본을 읽는 꼴이면 거기에, 아니면 JSON 을 읽어 보는 작은 시험).
- `FrameInputAssembly`: 스위치가 단계 입력에 실림. 존 액터 표본 시험: 몸 표본의 배율 = `BodyWindGain`, 칼 표본 = `BladeWindGain`.

## 5. 기준 계산 생성 도구 (파이썬, 새 파일 `Tools/FluidFX/fog3d_ref/fog3d_blend_b4_replay.py`)

- 첫머리 docstring 4줄(실행 환경·실행 명령·출력·상태). 식 설치는 `fog3d_blend_regress.py` 처럼 다른 import 보다 먼저 `fog3d_blend_patch` 를 import 하고 명령 안에서 `install(...)`. 하위 프로세스는 환경 변수로 식을 넘긴다(`install_from_environment`).
- 하위 명령:
  - `frame-check`: 지금 식(B0)으로 2a R5 장면 둘(`s2a_cliff_at_edge`, `s2a_slope_walk_up`)을 임시 폴더에 다시 만들어 `Tools/FluidFX/fog3d_ref/replays_s2a/R5/` 의 재생 파일과 `Saved/FluidFX/Fog3D/ref_s2a/R5/` 의 덤프(`.npz`·`.column.bin`·`.frames.bin`)와 바이트까지 같은지 본다. 하나라도 다르면 끝 코드 1(등록 4절 (다) — 그때는 B4 를 만들지 않는다).
  - `build [--jobs N]`: 식 B4 로 재생 파일 `Tools/FluidFX/fog3d_ref/replays_blend_b4/`(재생 JSON + 땅 `.ground.bin` + `manifest.json`)와 기준 덤프 `Saved/FluidFX/Fog3D/ref_blend_b4/`, 흔든 덤프 `ref_blend_b4/shake/` 와 `shake_diff.json`(덤프·묶음마다 `fog3d_compare_v2.group_diffs` 값)을 쓴다. 이미 있으면 지우지 말고 오류로 멈춘다(`--force` 일 때만 덮음).
  - `numbers --out <json>`: 숫자 파일(판 2, `Docs/VolumeFog3D/measurements/stage2a-impl/prereg-2a-impl-numbers-r5.json` 과 같은 꼴): `default = {relative: {wind: 0.002, disp: 0.002, fill: 0.002}, slope: 1e-6, frames_position_cm: 0.001, frames_slope: 1e-5}`, 덤프·묶음마다 X = min(0.03, max(0.002, 3 × 민감도))가 0.002 보다 큰 것만 `dumps` 에, 민감도 표 전체, 규칙 문장, 식 B4, 후보 R5, 도구 sha256.
  - `check`: 임시 폴더에 다시 만들어 재생·덤프의 sha256 이 `build` 결과와 같은지(결정론).
  - `list`: 장면·덤프 목록.
- 장면(등록 4절):
  - (가) `fog3d_replay_s2a.py --candidate R5` 의 13장면 그대로(이름·걸음·덤프 그대로). 그 장면들의 구에 `"g": 1.0`, 최상위 `"wind_blend": "B4"`.
  - (나) 평지 F0·128칸·`TUNING_S2A`·옮기기 N2·후보 R5(`fog3d_blend_probe.py` 의 `make_sim`·`build_gpu_scene` 본보기), 입력은 `fog3d_blend_probe.walk_stand_inputs`·`blade_inputs` 와 `fog3d_blend_gates.turn_steps` 와 같은 값. 이름(초당 걸음 h, 배율 g×100): `b4_walk_stand_h60_g100`·`_h60_g125`·`_h30_g100`·`_h30_g125`·`_h144_g100`, `b4_blade_h60_g100`·`_h60_g125`·`_h30_g100`·`_h30_g125`, `b4_turn_stand_h60_g100`·`_h60_g125`·`_h30_g100`·`_h30_g125`(꺾은 뒤 서기 120/60초를 더함), 그리고 `b4_slope_walk_up_h60_g125`(`s2a_slope_walk_up` 의 지형·입력에 몸 배율 1.25). 덤프(같은 초, 그 초에 가장 가까운 걸음): 걷다 멈춰 서기 `walk_end`·`stand_0p5`·`stand_1p5`·`end` / 칼 `swing_end`·`tail_0p25`·`end` / 꺾기 `turn`·`turn_walk_end`·`stand_1p0`·`end` / 비탈은 `s2a_slope_walk_up` 의 덤프 이름 그대로.
  - 매니페스트: `{"files": [{"name": "<장면>", "ground_off_twin": <bool>}, …]}`. (나)의 평지 장면은 땅 높이 그림이 모두 0 이면 true, 아니면 false 로 쓰고 보고에 적는다. (가)와 비탈은 false.
- 흔들기: `fog3d_replay_s2a.py` 의 흔들기와 같게(구 양 끝 ±1e-4cm, 씨앗 20261113).
- 도구 점검: `frame-check` 끝 코드 0 → `build` → `check` 같음 → 기준 덤프를 GPU 덤프 꼴로 옮겨 `fog3d_compare_s2a.py --numbers <만든 숫자 파일> --gpu-dir <옮긴 폴더> --ref-dir Saved/FluidFX/Fog3D/ref_blend_b4` 로 자기 자신과 견줘 실패 0·틀 없음 0(대조 도구가 새 폴더를 읽는지 확인. `compare-handoff.md` 의 자체 점검 방식). GPU 덤프와는 견주지 않는다.

## 6. 바뀐 것

- (없음)

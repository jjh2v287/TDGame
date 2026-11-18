# 1b 구현 계약 — 게임 연결·그리기

확정: 2026-11-13 claude(감독·결정). 초안은 하위 에이전트(Opus 5.5)가 기존 게임 코드를 읽고 썼고, 아래 "감독 확정" 절이 초안 10절의 결정 항목을 닫는다. 명세 `spec.md` v1 과 어긋나면 명세가 이기고, 통과 기준은 `prereg-1b.md`(등록본)가 정본이다.

## 감독 확정 (초안 10절의 결정과 덧붙임)

1. 그리는 식의 정본은 `Source/TDFluidGpu/Shaders/Private/TDFluidGpuFog3DDensity.ush` 한 파일이다. 재질 Custom 노드가 include 하고, 시험용 계산 셰이더도 같은 파일을 include 해 기준 모의 값과 맞대어 본다. 재질 컴파일이나 쿠킹에서 include 가 되지 않으면 재질 만드는 스크립트가 그 파일의 내용을 읽어 Custom 노드에 그대로 넣는다(그래도 정본은 그 파일 하나다).
2. 구역 기준점은 구역 상자 메시의 경계 중심이다(재질의 물체 위치 = C++ 의 `Bounds.Origin`).
3. 데이터 표에는 배열을 두지 않고 낱 필드로 둔다(뼈 네 개와 품질 단계 셋).
4. 구역 가장자리 흐림은 가로 네 면에만 건다(위아래 면에 걸면 바닥에 붙은 안개가 사라진다).
5. 압력 훑기 횟수·가속 계수·이동 상한은 데이터 표에 넣지 않는다(모듈 기본값. 통과 기준이 그 값으로 검증됐다).
6. 데이터 값 검사는 C++ 함수와 시험으로 한다(기존 검사 도구는 고치지 않는다).
7. 몸 반지름 잠정값(손·발 20cm, 골반 30cm)과 칼 밑·끝 오프셋은 전투 정의에서 복사해 데이터 기본값으로 둔다(전투 쪽이 바뀌면 어긋날 수 있음을 주석 없이 데이터 설명 문서에만 적는다. 1c 일괄 보정 대상).
8. 처음 창은 플레이어가 가운데 오도록 반올림 식으로 잡는다(명세 3.9 의 식).
9. 모듈 보강(1b-M): "지난 창이 상태에 든 창과 다르면 거부"는 다시 시작 계산과 첫 계산에는 적용하지 않는다. 층은 거부를 보면 다음 계산을 다시 시작으로 보낸다.
10. 물체 목록이 바뀐 계산의 동작은 아래 "물체 목록 변경" 절(보강 장면을 만든 작업자가 기준 모의로 정의하고 실측한 것)을 따른다. 정리 패스는 바람 텍스처 넷째 채널(옛 유체 표시)과 새 면 표시를 견줘 "옛 막힘이고 새 유체"인 칸의 밀린 거리·채움·압력과 그 칸에 닿은 면 바람을 0 으로 쓴다.
11. 시험 무대는 3D 안개 전용 새 맵이다. 기존 맵과 기존 에셋은 고치지 않는다. 에디터에서 에셋을 만들기 전에 저장되지 않은 다른 변경이 없는지 확인하고, 임시 물체를 둔 채 저장하지 않는다.
12. 작업자는 칸을 끝낼 때마다 `Saved/AgentOps/20261113/fog3d/stage1b/` 에 넘김 글을 남긴다.

## 물체 목록 변경의 정확한 동작 (GPU 가 같게 만들 것)
재생 형식 덧붙임 키: `step.obstacles`(목록, 최대 32, 1a 의 최상위 obstacles 와 같은 꼴). 있으면 그 계산부터 물체 목록을 이것으로 통째로 바꾼다. 그 계산은 굽기·정리를 돈다. 그 계산에 repeat > 1 은 못 쓴다. 형식 이름은 fog3d-replay-1 그대로(잠긴 `--check` 도 통과 [실측]).
한 계산 안의 차례(기준 모의 `fog3d_replay_add1.py` 의 `execute_add1`·`change_obstacles`):
1. 창 이동(있으면, 1a 와 같음) → 2. 물체 목록 변경 → 3. 손잡이·다시 시작·상태 조작·바람 덮어쓰기(1a 와 같음) → 4. 보통 계산(옮기기 → 입력 → 닫힌 면 0 → 압력 …).
2 의 내용:
- 가. 새 목록으로 굽기(3.8 그대로: 칸 막힘 → 면 표시). 압력 풀이의 계수도 새 막힘으로 다시 만든다(기준: `sim._geometry()` 가 막힘이 바뀌면 압력 계수 캐시를 버림 [확인: fog3d_core.py:604-609]).
- 나. 정리: 새 면 표시로 닫힌 면의 바람을 0 으로(→ 새로 막힌 칸에 닿은 면의 바람은 이 계산의 옮기기 전에 0). 기준 실측: 정리 직전 닫힌 면에 0 이 아닌 바람 100개 [실측].
- 다. 막힌 칸이었다가 유체가 된 칸: d = 0, g = 0, 압력 0, 그 칸의 여섯 면 바람 0. 기준 모의에서는 원래 막힌 칸의 d·g·압력과 닿은 면 바람이 이미 0 이라 이 대입은 값을 바꾸지 않는다(대입 전 0 이 아닌 값 0개 [실측]). GPU 는 막힌 칸의 d·g 자리에 그리기용 평균값을 적어 두므로(명세 3.6 마지막) 이 칸을 반드시 0 으로 써야 한다. 옛 유체 표시는 바람 텍스처 넷째 채널에 있으니 정리 패스가 "옛 막힘 ∧ 새 유체"를 알 수 있다 [추정].
- 라. 새로 막힌 칸의 d·g 는 판정하지 않는다(대조는 유체 칸만). 압력은 0.
기준 모의 함수: `sim.obstacles = 새 목록` → `sim._geometry()` → `sim.zero_closed_faces()` → 유체가 된 칸의 면 바람·d·g·압력 0 → `sim.pr[막힌 칸] = 0` → `sim._zero_solid_state()` → `sim.refresh_diff()`. 핵심 파일은 고치지 않고 덧붙임 도구 안에서 이 함수들을 부른다.
- 1a C++ 재생 도우미는 `bObstaclesChanged` 를 늘 거짓으로 둔다 [확인: v9-refute.md:26 인용]. 1b 에서 이 키를 읽어 물체 목록을 바꾸고 변경 표시를 켜야 한다.

---

(아래는 초안 원문이다. 10절의 "감독이 정할 것"은 위 확정으로 닫혔다.)

# Y1. 3D 안개 1b(게임 연결·그리기) 구현 계약 초안

작성 2026-11-13 Opus 5.5(하위 에이전트). 명세 `Docs/VolumeFog3D/stage1/spec.md` v1 의 정의는 바꾸지 않고 "게임 코드로 어떻게 놓을지"만 정한다. 어긋나면 명세가 이긴다.
표기: [확인: 파일:줄] 열어 봄, [실측] 직접 돌림, [추정] 추론. 줄임말: RT(렌더 타깃), MID(동적 재질 인스턴스), CVar(콘솔 변수), PIE(에디터 안 플레이), RDG(렌더 의존성 그래프), LWC(큰 좌표, Large World Coordinates).

## 0. 결론 다섯
1. 데이터 표에 배열을 두지 않는다. 기존 시험 `StructDefaultsMatchHeader` 의 값 비교 함수에 배열 갈래가 없고(그대로면 실패), 배열 기본값은 비어 있어야 한다 [확인: Source/TDGame/FluidFX/Tests/TDFluidPresetTuningTests.cpp:221-280·438-451·651-664]. 뼈·품질 단계는 낱 필드로 둔다.
2. 칼날 축은 전투 휘두르기 정의를 그대로 쓴다: 소켓 `HandGrip_R` + 소켓 기준 밑 (−1.15, 3.80, 6.06)·끝 (−13.37, 28.72, 98.11) cm [확인: Source/TDGame/Combat/Skills/TDPlayerCombatCatalog.cpp:37-40·70-73, 세계 변환 방식 TDMeleeSweep.cpp:216-226]. 길이 약 96.1cm → 반지름 20 이면 구 6개(명세 3.7 의 예와 같음).
3. 그리는 식은 한 파일(`Source/TDFluidGpu/Shaders/Private/TDFluidGpuFog3DDensity.ush`)에 두고, 재질 Custom 노드가 include 하며, 시험용 계산 셰이더도 같은 파일을 include 해 기준 모의 값과 GPU 에서 맞대어 본다(감독 결정 1). 기존 HLSL 검사기의 numpy 거울은 HLSL 을 실행하지 않는다 [확인: Tools/FluidFX/check_fluid_fog_hlsl.py:4·123-127].
4. 구역 기준점 = 구역 상자 메시의 경계 중심. 재질의 "물체 위치" 노드가 바로 이 값이다 [확인: <UE_5.8>/Engine/Source/Runtime/Engine/Public/PrimitiveUniformShaderParametersBuilder.h:194 `AbsoluteObjectWorldPosition = InWorldBounds.Origin`]. C++ 은 `ZoneVolume->Bounds.Origin`(64비트)을 같은 기준으로 쓴다.
5. 층은 렌더 스레드의 "거부" 수를 원자 정수로 돌려받아, 거부가 보이면 다음 계산을 다시 시작으로 보낸다(모듈이 "지난 창 ≠ 상태" 를 거부하게 바뀌면 영원히 거부되는 고리를 끊음).

## 1. 데이터 `FTDFog3DSettings` (`Source/TDGame/FluidFX/TDFog3DTypes.h`, 전역 표 `Globals.TDFog3DSettings`)
모든 필드 `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fog3D")`. 헤더 기본값 = JSON 값이어야 한다(위 시험). 파일은 FluidFX 바로 아래(데이터 검사기가 그 폴더 *.h 를 읽음 [확인: Tools/check_fluid_preset_data.py:4]).

| 무리 | 필드(형 = 기본값) | 뜻 |
|---|---|---|
| 켜기 | bEnabled(bool = true), Mode(int32 = 2), DefaultQualityLevel(int32 = 2) | 거짓이면 상자 숨김·계산 없음. 모드 0 끔(평소 안개만)·1 바람 없는 판·2 완전판. 단계 0 낮음·1 보통·2 높음 |
| 품질 | QualityLowResolution 128, QualityLowStepsPerSecond 30.0, QualityMediumResolution 256, QualityMediumStepsPerSecond 30.0, QualityHighResolution 256, QualityHighStepsPerSecond 60.0 | 명세 5절 표. 초당 계산은 상한 |
| 격자·박자 | CellSizeCm 15.625, LayerThicknessCm 30.0, MaximumStepSeconds 0.05, WindowFollowMarginCells 12 | 1단계는 15.625·30 만 받음(부풀리기 값이 이 크기 전용, 명세 3.8). dt 상한 1/20초(3.10). 창 따라가기 12칸(3.9). 걸음 16칸은 모듈 계약이라 코드 상수 |
| 평소 곡선 | BaseDensity 0.8, BaseThicknessCm 90.0, BaseTopBlurCm 45.0 | 재질만 씀(3.4) |
| 되돌림 | WindRefillPerSec 0.75, DisplacementRefillHorizontalPerSec 0.1, DisplacementRefillVerticalPerSec 0.3, FillRefillPerSec 0.3, EdgeBandCells 8, EdgeBandAbsorptionPerSec 4.0 | `FTDFluidGpuFog3DTuning` 같은 이름 필드로 복사. 압력 횟수·가속·이동 상한은 데이터에 두지 않고 모듈 기본값(감독 결정 5) |
| 칼 | BladeSocketName(FName = "HandGrip_R"), BladeBaseLocalCm(FVector = (−1.15, 3.80, 6.06)), BladeTipLocalCm(FVector = (−13.37, 28.72, 98.11)), BladeRadiusCm 20.0, BladeWindStrength 1.0, BladeErosionPerSec 30.0 | 3.7 |
| 몸 | HandBoneName "hand_r", LeftFootBoneName "foot_l", RightFootBoneName "foot_r", PelvisBoneName "pelvis", HandRadiusCm 20.0, FootRadiusCm 20.0, PelvisRadiusCm 30.0, BodyWindStrength 1.0, BodyErosionPerSec 0.0 | 뼈 이름 None 이면 그 입력 없음. 반지름은 잠정 [추정, 1c 보정] |
| 입력 상한 | MaximumInputSpeedCmPerSec 3000.0, MaximumInputSphereCount 16 | 3.7 |
| 물체 | MaximumObstacleCount 32, ObstacleBaseHalfThicknessCm 7.9, ObstacleUpwardExtraCm 7.2 | 3.8 |
| 구역 | ZoneEdgeFadeCm 500.0, ZoneEntryVerticalMarginCm 200.0 | 4절 가장자리 흐림. 구역 안 판정의 높이 여유 |
| 무늬 | bFineNoiseEnabled true, FineNoiseAmplitude 0.2, FineNoiseSpacingCm 50.0, FineNoiseSeed(int32 = 1), bStainNoiseEnabled false, StainNoiseAmplitude 0.3, StainNoiseSpacingCm 200.0, StainNoiseSeed 0 | 4절 |

정본 JSON 에 넣을 글(`"Globals"` 의 `TDFluidOutputQualitySettings` 뒤, 이 덩어리만 스테이징):
```json
    "TDFog3DSettings": {
      "bEnabled": true, "Mode": 2, "DefaultQualityLevel": 2,
      "QualityLowResolution": 128, "QualityLowStepsPerSecond": 30.0,
      "QualityMediumResolution": 256, "QualityMediumStepsPerSecond": 30.0,
      "QualityHighResolution": 256, "QualityHighStepsPerSecond": 60.0,
      "CellSizeCm": 15.625, "LayerThicknessCm": 30.0, "MaximumStepSeconds": 0.05, "WindowFollowMarginCells": 12,
      "BaseDensity": 0.8, "BaseThicknessCm": 90.0, "BaseTopBlurCm": 45.0,
      "WindRefillPerSec": 0.75, "DisplacementRefillHorizontalPerSec": 0.1, "DisplacementRefillVerticalPerSec": 0.3, "FillRefillPerSec": 0.3,
      "EdgeBandCells": 8, "EdgeBandAbsorptionPerSec": 4.0,
      "BladeSocketName": "HandGrip_R", "BladeBaseLocalCm": {"X": -1.15, "Y": 3.8, "Z": 6.06}, "BladeTipLocalCm": {"X": -13.37, "Y": 28.72, "Z": 98.11},
      "BladeRadiusCm": 20.0, "BladeWindStrength": 1.0, "BladeErosionPerSec": 30.0,
      "HandBoneName": "hand_r", "LeftFootBoneName": "foot_l", "RightFootBoneName": "foot_r", "PelvisBoneName": "pelvis",
      "HandRadiusCm": 20.0, "FootRadiusCm": 20.0, "PelvisRadiusCm": 30.0, "BodyWindStrength": 1.0, "BodyErosionPerSec": 0.0,
      "MaximumInputSpeedCmPerSec": 3000.0, "MaximumInputSphereCount": 16,
      "MaximumObstacleCount": 32, "ObstacleBaseHalfThicknessCm": 7.9, "ObstacleUpwardExtraCm": 7.2,
      "ZoneEdgeFadeCm": 500.0, "ZoneEntryVerticalMarginCm": 200.0,
      "bFineNoiseEnabled": true, "FineNoiseAmplitude": 0.2, "FineNoiseSpacingCm": 50.0, "FineNoiseSeed": 1,
      "bStainNoiseEnabled": false, "StainNoiseAmplitude": 0.3, "StainNoiseSpacingCm": 200.0, "StainNoiseSeed": 0
    }
```
- 실제 파일은 기존 줄 바꿈 꼴(한 필드 한 줄, 2칸 들여쓰기)로 쓴다. 앞 항목 끝 `}` 뒤에 쉼표.
- 데이터 검사(같은 헤더의 자유 함수 `TDGAME_API void CollectTDFog3DSettingsProblems(const FTDFog3DSettings&, TArray<FString>& OutProblems)`, 구현 `TDFog3DTypes.cpp`): Mode 0~2, 단계 0~2, 해상도 2의 거듭제곱·64~512, 초당 계산 (0, 240], CellSizeCm == 15.625·LayerThicknessCm == 30, MaximumStepSeconds (0, 0.05], 반지름 ≥ 16, 깎기·되돌림 ≥ 0, 속도 상한 > 0, 구 1~16, 물체 0~32, 띠 0~16, BaseThicknessCm > BaseTopBlurCm ≥ 0, 무늬 간격 > 0·세기 [0, 1)·씨앗 0~16777215(재질에 32비트 실수로 넘기므로 2²⁴ 미만), ZoneEdgeFadeCm ≥ 0, 모든 값 유한. 문제가 있으면 액터가 한 번 오류 로그 후 쉰다. 구역 크기 조건(한 변 ≥ 흐림 폭 × 2)은 액터 속성이라 액터가 검사.
- 품질 단계 고르기: `TDGAME_API FTDFog3DQualityTier ResolveTDFog3DQualityTier(const FTDFog3DSettings&, int32 Level)`(UObject 아닌 구조체 {Resolution, StepsPerSecond}).
- 등록: `TDFluidPresetTuningSubsystem.cpp` 에 include 1줄 + 등록표 끝에 `{ FTDFog3DSettings::StaticStruct(), ETDFluidPresetTuningScope::Global },` 1줄 [확인: 같은 파일:693-725]. 읽기: 표 세대가 바뀔 때만 `ApplyPresetTuning(ETDFluidPresetKind::Custom, FTDFog3DSettings::StaticStruct(), &Settings)`(전역 층은 종류를 무시 [확인: 같은 파일:923-929], 세대 캐시 본보기 TDFluidOutputQuality.cpp:29-43).

## 2. `FTDFog3DLayer` (`TDFog3DLayer.h/.cpp`, UObject 아님, 복사 금지)
공개 함수
- `void Configure(const FTDFog3DSettings&, int32 QualityLevel, int32 Mode)` — 해상도·모드·단계가 바뀌면 다시 시작 표시.
- `void RequestReset()` — 구역 재진입·볼륨 안개 다시 켜짐 등 액터가 아는 사유.
- `FTDFog3DClockDecision AdvanceClock(float FrameDeltaSeconds)` — {bStepsNow, StepDeltaSeconds(상한 적용), ElapsedSeconds(실제)}.
- `bool SubmitStep(const FTDFog3DStepRequest&, UTextureRenderTargetVolume& OutputTarget)` — 요청 = {FVector PlayerWorld; double FloorZ; TArray<FTDFog3DInputSample> Inputs; TArray<FTDFluidSdfObstacle> RegisteredObstacles; FTDFog3DClockDecision Clock}. 입력 표본 = {int32 SlotId; FVector CurrentWorld; float RadiusCm; float WindStrength; float ErosionPerSec}. 지난 위치는 층이 SlotId 로 들고 있다(마지막 계산 때 위치, 3.7).
- `void Release()`(렌더 명령으로 상태 해제, 본보기 TDFluidNearRippleLayer.cpp:204-220), `bool HasSubmittedWindow()`, `FIntPoint GetSubmittedWindowMinCell()`, `int32 GetResolution()`, `uint32 GetSubmittedStepCount()`, `int32 GetSeenRejectedStepCount()`, `FTDFluidGpuFog3DFrameInput BuildFrameInput(const FTDFog3DStepRequest&) const`(시험용으로 공개, 렌더 명령 없이 포장만).
- 렌더 몫: `TSharedPtr<FTDFog3DRenderSlot, ESPMode::ThreadSafe>`, 슬롯 = {FTDFluidGpuFog3DState State; std::atomic<int32> RejectedStepCount}. 명령: `FRDGBuilder` → `if (!AddTDFluidGpuFog3DPasses(...)) ++RejectedStepCount` → `Execute`(본보기 NearRipple.cpp:189-194).

순수 함수(모두 static, 64비트 좌표, 이름은 동사형)
| 함수 | 정의 |
|---|---|
| ComputeWorldCell(FVector2D WorldXY, double CellCm) | floor(좌표 ÷ h), 축마다 |
| SnapWindowMinCell(FIntPoint PlayerCell, int32 N) | 16 × floor((p − N/2 + 8) ÷ 16)(정수 나눗셈 내림) = 명세 3.9 의 반올림식. 처음 창에도 같은 식 [추정: 기준 모의 재생은 처음 창을 내림으로 잡지만(Tools/FluidFX/fog3d_ref/fog3d_replay.py:81-82) 게임은 플레이어 가운데가 낫다] |
| FollowWindowMinCell(FIntPoint Current, FIntPoint PlayerCell, int32 N, int32 Margin) | 축마다 \|p − (o + N/2)\| > Margin 이면 그 축만 Snap [기준 모의와 같은 식: fog3d_core.py:671-679] |
| IsWindowJumpTooFar(Prev, New, N) | 어느 축이든 \|차\| ≥ N (순간이동) |
| DecideClock(double SinceLastStep, float StepsPerSecond, float MaxStepSeconds) | SinceLastStep ≥ (1/S) × (1 − 1e-3) 이면 계산. dt = min(SinceLastStep, 0.05), Elapsed = SinceLastStep. 화면이 느리면 매 프레임 계산 |
| ResolveMode(int32 Console, int32 Data) / ResolveQualityLevel(...) | 콘솔 −1 이면 데이터, 아니면 0~2 로 자름 |
| ComputeBladeSphereCount(double LengthCm, double RadiusCm) | ceil(L ÷ r) + 1 (96.1·20 → 6) |
| PackSpheres(Inputs, PreviousBySlot, WindowOriginWorld(double), FloorZ, bIsResetStep, Elapsed, MaxSpeed, MaxCount, OutSpheres, OutDroppedCount) | 다시 시작 계산이거나 그 슬롯의 지난 위치가 없으면 A = B. V = (B − A) ÷ Elapsed, 크기만 MaxSpeed 로 자름. 창 기준 = 세계 − 창 원점(가로), 세계 z − 바닥(높이)을 64비트로 뺀 뒤 32비트. 들어온 차례 그대로 담고 MaxCount(≤16)를 넘으면 뒤에서 버림. 패킹 = (A, r) / (B, s) / (V, E) [확인: Source/TDFluidGpu/Public/TDFluidGpuFog3D.h:16-21] |
| ReadUprightYaw(const FTDFluidSdfObstacle&, double& OutYawDegrees) | bIsBox 이고 \|회전의 Z 축 · 위\| ≥ 1 − 1e-4 일 때만 참. 돌림 각 = atan2(X 축.Y, X 축.X)(뒤집힌 상자도 같은 상자) |
| SelectAndPackObstacles(Registered, WindowOriginWorld, WindowCenterWorldXY, FloorZ, Inflation, MaxCount, Out, OutSkippedCount) | 바로 선 상자만 → 창 가운데까지 가로 거리 제곱으로 StableSort(같으면 등록 순, 명세 3.8. 기존 `SelectNearestObstacles` 는 좌표로 순서를 정해 쓰지 않음 [확인: TDFluidLiveSubsystem.cpp:960-978]) → 앞 MaxCount 개를 `MakeTDFluidGpuFog3DUprightBox(창 기준 중심, BoxHalfExtents3DCm, 돌림 각, 부풀리기)` 로 포장 |
| HaveObstaclesChanged(PrevWorldList, NewWorldList) | 고른 물체의 (세계 중심, 반크기, 돌림 각) 목록을 정확 비교. 다르면 굽기 신호 `bObstaclesChanged` |
| ShouldResetState(const FTDFog3DResetInputs&) | 처음·Release 뒤 / 순간이동 / 재진입 / 모드·단계 변경 / 볼륨 안개 다시 켜짐 / 새 거부 발견 중 하나라도 참 |
| ComputeMaterialWindow(FIntPoint SubmittedMin, double CellCm, FVector ZoneReference, double FloorZ, int32 N) | FVector4f(o.x·h − ref.x, o.y·h − ref.y, 바닥 − ref.z, N), 64비트로 빼고 32비트로 |

`SubmitStep` 의 FrameInput: Resolution = 단계 해상도, Cell/Layer = 데이터, Mode = 해석된 모드(1·2, 0 이면 부르지 않음), WindowMinCell = 따라간 창, PreviousWindowMinCell = 지난 제출 창(없으면 지금 창), bResetsState = ShouldResetState, bObstaclesChanged = HaveObstaclesChanged, DeltaSeconds = Clock.StepDeltaSeconds, Tuning = 데이터 되돌림 6개 + 모듈 기본, OutputTarget = `GameThread_GetRenderTargetResource()`. 제출 뒤 지난 창·지난 위치·지난 물체 목록 갱신.

널 RHI 시험(`Source/TDGame/FluidFX/Tests/TDFog3DLayerTests.cpp`, `EditorContext | EngineFilter`)
- TDGame.FluidFX.Fog3D.WindowSnapAndFollow — 16의 배수, 12칸 이하 이동은 그대로·13칸이면 한 축만, 음수 좌표, N 128·256, 순간이동 판정, 기준 모의 `follow` 와 같은 결과 표 20개.
- TDGame.FluidFX.Fog3D.ClockCadence — 초당 60 단계를 화면 60·144·30에서, 초당 30 단계를 60에서: 계산 횟수·dt 상한 0.05·Elapsed 는 자르지 않음.
- TDGame.FluidFX.Fog3D.ModeAndQualityResolve — 콘솔 −1·0·1·2·9, 단계 표.
- TDGame.FluidFX.Fog3D.SpherePacking — 다시 시작 계산은 A = B·V = 0, 새 슬롯 A = B, 초속 50m → 30m(방향 같음), 17개 → 16개(뒤 버림·버린 수 1), 세계 1e7cm 에서도 창 기준 값이 작은 좌표 경우와 같음.
- TDGame.FluidFX.Fog3D.BladeSpheres — 6개, 간격 ≤ 반지름, 첫 구 = 끝, 마지막 = 밑.
- TDGame.FluidFX.Fog3D.ObstacleSelectAndPack — 기운 상자(0.02 rad)·구는 건너뜀, 뒤집힌 상자(roll 180) 받음, 40개 → 가까운 32개·같은 거리는 등록 순, 부풀린 반크기 (≥7.9, ≥7.9, ≥15.1).
- TDGame.FluidFX.Fog3D.ObstacleChangeSignal — 같으면 거짓, 0.01cm 이동·순서 바뀜이면 참.
- TDGame.FluidFX.Fog3D.ResetReasons — 사유마다 참, 평소 거짓.
- TDGame.FluidFX.Fog3D.FrameInputAssembly — `BuildFrameInput`: 처음은 reset·지난 창 = 지금 창, 둘째는 지난 창 = 첫 제출 창, 모드·해상도·Tuning 복사.
- TDGame.FluidFX.Fog3D.MaterialWindowParameters — 구역 기준점 (1e7, −1e7, 500) 에서도 오차 < 0.01cm.
- TDGame.FluidFX.Fog3D.SettingsValidation — 기본값 문제 0, 잘못된 값마다 문제 1건 이상(1b-1 이 만든다).

## 3. `ATDFog3DZoneActor` (`TDFog3DZoneActor.h/.cpp`)
- 컴포넌트: 뿌리 `USceneComponent`, 자식 `UStaticMeshComponent* ZoneVolume`(충돌 없음·그림자 없음·레이 트레이싱 끔·처음 숨김, 메시는 무대 스크립트가 엔진 큐브로 넣음 = 레벨 직렬화 참조라 런타임 동기 로드 아님). 크기는 OnConstruction 과 BeginPlay 에서 `상대 배율 = 2 × ZoneHalfExtentCm ÷ 메시 경계 크기`.
- 속성: `FVector ZoneHalfExtentCm`(기본 3000, 3000, 150; 상자 바닥 = 바닥 높이), `TSoftObjectPtr<UMaterialInterface> VolumeMaterial`(무대 스크립트가 /Game 재질 지정. 기존 프리셋도 같은 형식에 파이썬으로 넣음 [확인: Tools/FluidFX/editor_make_fluid_fog_stage.py:787]), Transient: `UTextureRenderTargetVolume* StateTarget`, `UMaterialInstanceDynamic* VolumeMID`, 비UPROPERTY: 층, 설정 캐시, 로드 핸들, 경고 한 번 표시들.
- 콘솔(이 .cpp 의 이름공간 `TDFog3DZoneActorPrivate`): `TD.Fog3D.Mode`(int, 기본 −1 데이터), `TD.Fog3D.Quality`(int, 기본 −1 데이터).
- 틱: `PrimaryActorTick.TickGroup = TG_PostUpdateWork`(애니메이션·소켓 뒤 [확인: r3 지도 1.1, LevelTick.cpp:1750-1883]).
- 한 틱의 차례(조기 반환)
  1. 대표가 아니면 반환. 설정 세대가 바뀌었으면 다시 읽고 문제가 있으면 쉼. bEnabled 거짓이면 상자 숨김·반환.
  2. 모드·단계 해석(콘솔 → 데이터). 바뀌면 층 `Configure`, 해상도가 바뀌면 RT 다시 만들고 MID 의 상태 텍스처 교체.
  3. 첫 로컬 플레이어 폰(`World->GetFirstPlayerController()->GetPawn()`)이 없으면 Active 0·반환.
  4. 구역 안 판정 `IsPointInsideZone(폰 위치, 상자 중심, 반크기, 높이 여유)`(가로는 상자 안, 높이는 상자 ±여유). 밖이면 Active 0, "나갔음" 기억, 반환. 다시 들어오면 `RequestReset`.
  5. 볼륨 안개: `r.VolumetricFog`(게임 스레드 `GetInt`) 참 그리고 월드의 높이 안개 컴포넌트 `bEnableVolumetricFog`·보임 [추정: 공개 속성, ExponentialHeightFogComponent.h:135-136]. 높이 안개 액터는 BeginPlay 에 `TActorIterator<AExponentialHeightFog>` 로 찾아 약한 참조로 두고 없어지면 1초마다 다시 찾음. 꺼졌으면 Active 0, 다시 켜지면 `RequestReset`.
  6. 모드 0 이면 Active 0·반환(평소 안개만 그려짐). `AdvanceClock` 가 아니라고 하면 재질 창 값은 그대로(마지막 제출 창), 반환.
  7. 입력 수집(아래), 물체 = `UTDFluidLiveSubsystem::CollectRegisteredObstacles`(서브시스템 없으면 빈 목록).
  8. `SubmitStep`. 9. 재질: Fog3DWindow = `ComputeMaterialWindow(제출 창, …, ZoneVolume->Bounds.Origin, 바닥)`, Fog3DActive 1. 값이 바뀔 때만 MID 에 쓴다.
- 칼·뼈에서 구 만들기: 메시 = `Cast<ACharacter>(폰)->GetMesh()`, 아니면 첫 `USkeletalMeshComponent`. 칼: `DoesSocketExist(BladeSocketName)` 이면 `Socket = GetSocketTransform(이름, RTS_World)`, 밑 = Socket.TransformPosition(BladeBaseLocalCm), 끝 = …(BladeTipLocalCm). 개수 K = ComputeBladeSphereCount, 구 i(0 = 끝) = 끝 + (밑 − 끝) × i ÷ (K − 1), SlotId = i. 이어서 손(SlotId 100)·왼발(101)·오른발(102)·골반(103) 을 `GetSocketLocation(뼈)`(뼈 이름도 받음)로. 소켓·뼈가 없으면 그 입력만 빼고 한 번 경고. 차례 = 칼날(끝 → 밑) → 손 → 발 → 골반(명세 3.7).
- 수명: BeginPlay — 대표 고르기(월드의 다른 `ATDFog3DZoneActor` 가 이미 대표면 경고 한 번·쉼. BeginPlay 가 차례로 도니 먼저 시작한 것이 대표, 전역 정적 상태 없음), 설정 읽기·검사, RT 생성(`NewObject<UTextureRenderTargetVolume>(this, NAME_None, RF_Transient)`, `bSupportsUAV = true`, `Filter = TF_Bilinear`, `Init(N, N, 8, PF_FloatRGBA)` [확인: Source/TDFluidGpu/Private/Tests/TDFluidGpuFog3DRhiTests.cpp:187-194]), 재질 비동기 로드(`UAssetManager::GetStreamableManager().RequestAsyncLoad`, 본보기 TDFluidVolumeDriverComponent.cpp:277-286) → 도착하면 Volume 도메인인지 확인 후 MID 생성·상수 파라미터·상태 텍스처 지정 → 그때 상자 보임. EndPlay(월드 전환·PIE 끝·스트리밍 해제 모두) — 로드 핸들 취소, `Layer.Release()`, MID·RT 참조만 끊음(이름 바꾸기·명시 삭제 금지, L-editor-95), 대표 해제.
- 월드에 둘 이상: 둘째부터 계산·그리기 모두 쉬고 상자 숨김(겹친 그리기 방지). 대표가 EndPlay 해도 다른 것을 올리지 않음(1단계).
- 모드 0·구역 밖·볼륨 안개 꺼짐: Fog3DActive = 0(무게 0 → 평소 안개만), 다른 파라미터는 마지막 값 유지. bEnabled 거짓·대표 아님: 상자 숨김.
- 액터 순수 함수(널 RHI 시험 TDGame.FluidFX.Fog3D.ZoneInside·BladeSamplesFromSocket): IsPointInsideZone, AppendBladeSamples(소켓 변환, 밑·끝, 반지름, …) — 고정 소켓 변환에서 첫 구 = 끝·순서·개수.

## 4. 물체 읽기 함수 (`TDFluidLiveSubsystem.h` 공개 + `.cpp`)
```cpp
void CollectRegisteredObstacles(TArray<FTDFluidSdfObstacle>& OutObstacles) const;
```
구현 요지: `OutObstacles.Reset()` → `ObstacleSources` 를 등록 순으로 돌며 `Get()` 이 유효하고 `BuildObstacle` 이 참인 것만 담는다. 목록·더러움 표시는 바꾸지 않는다(2D 계산이 매 계산 `bObstacleSourcesDirty` 를 지우므로 기대지 않음 [확인: TDFluidLiveSubsystem.cpp:989-1030]. 2D 액터가 없는 맵에서는 서브시스템이 틱하지 않아 죽은 약한 참조가 남을 수 있어 직접 거른다 [확인: TDFluidLiveSubsystem.h:89 IsTickable]). 시험 TDGame.FluidFX.Fog3D.SubsystemObstacleRead: `UWorld::CreateWorld(EWorldType::Game, …)`(본보기 TDFluidMassInputTests.cpp:280)에 상자 액터 3개 + 물체 컴포넌트, 하나 파괴 뒤 2개·등록 순·세계 중심.

## 5. 재질
에셋 `/Game/FluidFX/Materials/M_TD_Fog3DVolume`(MD_VOLUME, BLEND_ADDITIVE, 그래프 = Custom 노드 1개 + 파라미터 연결 + 앞단 출력). C++ 이 쓰는 이름(모양 정본 JSON 에 넣지 않는다: 런타임 이름이 거기 있으면 데이터 검사 오류 [확인: Tools/check_fluid_preset_data.py:4 R7])
| 이름 | 종류 | 값 | 쓰는 때 |
|---|---|---|---|
| Fog3DStateVolume | 텍스처 객체(볼륨, 기본 T_TD_FluidNoiseVolume, 선형 색) | 상태 RT | RT 만들 때 |
| Fog3DWindow | 벡터 | (창 원점 − 기준점 x, y, 바닥 − 기준점 z, N) | 계산한 틱 |
| Fog3DGrid | 벡터 | (칸 15.625, 층 30, 층 수 8, 띠 칸 8) | 설정 세대 |
| Fog3DActive | 스칼라 | 0/1 | 바뀔 때 |
| Fog3DBaseShape | 벡터 | (0.8, 90, 45, 0) | 설정 세대 |
| Fog3DZone | 벡터 | (반크기 x, y, z, 흐림 폭) | BeginPlay·설정 세대 |
| Fog3DFineNoise / Fog3DStainNoise | 벡터 | (켜짐이면 세기 아니면 0, 간격, 씨앗, 0) | 설정 세대 |
그래프 전용(모양 값, C++ 안 씀): Fog3DExtinctionPerM 0.2, Fog3DAlbedo (0.8, 0.85, 0.95)(2D 기본값 [확인: Tools/FluidFX/editor_make_fluid_fog_material.py:34·40]).
- 구역 기준 위치: `WorldPosition`(절대) − `ObjectPositionWS` 를 Subtract 노드로(LWC 끼리 빼기) → `TruncateLWC` → Custom 입력 `Fog3DZonePositionCm`. 2D 는 각각 먼저 자른 뒤 빼서 큰 좌표에서 정밀도를 잃는다 [확인: 같은 스크립트:478·482] — 3D 는 빼고 자른다.
- Custom 노드: `IncludeFilePaths = ["/TDFluidGpu/Private/TDFluidGpuFog3DDensity.ush"]`(엔진 속성 [확인: <UE>/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionCustom.h:97], 경로 맵은 PostConfigInit 모듈이 등록 [확인: Source/TDFluidGpu/Private/TDFluidGpuModule.cpp:10, TDGame.uproject:24]). 몸통:
```
float3 tdUvw = TDFog3DStateUvw(Fog3DZonePositionCm, Fog3DWindow, Fog3DGrid);
float4 tdState = Texture3DSampleLevel(Fog3DStateVolume, Fog3DStateVolumeSampler, tdUvw, 0);
float tdDensity = TDFog3DDensity(Fog3DZonePositionCm, tdState, Fog3DWindow, Fog3DGrid, Fog3DActive, Fog3DBaseShape, Fog3DZone, Fog3DFineNoise, Fog3DStainNoise);
return float4(Fog3DExtinctionPerM * tdDensity.xxx, tdDensity);
```
- `.ush` 의 식(주석 금지 — 셰이더 검사기 규칙, 함수 이름 접두 TDFog3D). P = 구역 기준 위치, Pw.xy = P.xy − Window.xy, 높이 hgt = P.z − Window.z, h = Grid.x, N = Window.w, Htop = Grid.y × Grid.z(240).
  - Uvw = (Pw.x ÷ (N h), Pw.y ÷ (N h), hgt ÷ Htop). 필터 선형·주소 고정은 RT 쪽(볼륨 RT 표본기는 Clamp 고정 [확인: r3 지도 0절, TextureRenderTargetVolume.cpp:277-282]).
  - 띠 칸 수 c = min(Pw.x/h, N − Pw.x/h, Pw.y/h, N − Pw.y/h)(칸 가운데에서 계산 쪽 i + 0.5 와 같음). fade = Grid.w > 0 ? smoothstep(0, Grid.w, c) : (c ≥ 0). 무게 W = fade × Active × (1 − smoothstep(Htop − Grid.y, Htop, hgt)) (= 210~240).
  - d = State.xyz × W, g = State.w × W, X = P − d, Xh = hgt − d.z.
  - 두께 T = BaseShape.y × (1 + 얼룩세기 × (2 n(X.xy; 얼룩 간격, 씨앗) − 1)) (세기 0 이면 BaseShape.y). B = BaseShape.x × (1 − smoothstep(T − BaseShape.z, T, max(Xh, 0))). 잔무늬: B × (1 + 잔무늬세기 × (2 n(X.xy; 50, 1) − 1)).
  - 구역 흐림 zf = smoothstep(0, Zone.w, min(Zone.x − |P.x|, Zone.y − |P.y|)) — 가로 네 면만(감독 결정 4). 농도 = max(0, 1 + g) × B × zf.
  - 값 잡음 n(Q; s, seed): f = Q ÷ s, i = int2(floor(f)), t = f − floor(f), w = smoothstep(0, 1, t), 격자 값 v(i) = 해시, (v00(1 − wx) + v10 wx)(1 − wy) + (v01(1 − wx) + v11 wx) wy. 해시(uint 끼리 2³² 감김): hh = uint(ix)·374761393 + uint(iy)·668265263 + seed·2246822519, hh = (hh ^ (hh >> 13))·1274126177, hh ^= hh >> 16, 값 = float(hh) × 2⁻³² [기준 모의와 같은 식: Tools/FluidFX/fog3d_ref/fog3d_core.py:239-247·272-288, 평소 곡선 :155-159, 그리는 식 :1212-1225·1517-1527]. 씨앗은 uint(값 + 0.5).
- 재질 만드는 스크립트 `Tools/FluidFX/editor_make_fog3d_material.py`(docstring 4줄): 2D 스크립트 꼴(덮어쓰기 요청 파일 `Saved/FluidFX/fog3d_material_request.json`, 미저장 보존, 노드 추가·연결 확인, Custom 입력 이름 다시 읽어 단언, 앞단은 2D 의 Substrate 1순위·실패 시 기존 핀 방식 [확인: 같은 스크립트:509-555], 볼륨 도메인 경고는 무해 L-editor-65) + `include_file_paths` 설정·다시 읽기, 재질 통계·컴파일 오류 0, 보고 `Saved/FluidFX/fog3d_material.json`(.ush sha256 포함).
- 에디터 없이 하는 "기준 모의와 같은 값" 시험(감독 결정 1 권장안)
  1. `Tools/FluidFX/fog3d_ref/fog3d_density_fixture.py`(새 도구, 잠긴 도구는 import 만): 기준 모의로 재생 파일 2개(낮게 휘두르기 `s_na_low_swing.json`, 내려찍기 `s_ga…`)를 끝까지 돌리고, 창 띠 8칸 밖·층 0~6(높이 210 아래) 칸 가운데 표본 4096개마다 (P, d, g, 기준 농도 = `drawn_density_v3` 의 그 칸 값)를 적는다. 무늬 3벌(끔 / 잔무늬 / 잔무늬 + 얼룩 씨앗 7). 구역 기준점 0, Active 1, 구역 흐림 1. 따로 무게·UV 경우(띠 안, 210~240, 창 밖) 200개는 명세 4절 식을 파이썬으로 계산. 출력 `Tools/FluidFX/fog3d_ref/fixtures/fog3d_density_fixture.json`(+ sha256), `--check` 는 다시 만들어 같은지 비교.
  2. GPU: `Source/TDFluidGpu/Shaders/Private/TDFluidGpuFog3DDensityProbe.usf`(같은 .ush include, 표본마다 Uvw·W·농도를 구조체 버퍼에 씀), `Private/TDFluidGpuFog3DDensityProbe.cpp`(`FTDFluidGpuFog3DDensityProbeCS` 선언과 등록 같은 파일 — 검사기가 하위 폴더·다른 파일을 말없이 건너뜀 [확인: 1a 계약 6절]), `Private/TDFluidGpuFog3DDensityProbe.h`(비공개 도우미), 시험 `Private/Tests/TDFluidGpuFog3DDensityRhiTests.cpp` = TDGame.FluidFXRhi.Fog3D.DensityMatchesReference. 판정: 농도 최대 절대 차 ≤ 1e-4·평균 ≤ 1e-5, 무게·UV 최대 차 ≤ 1e-6 [추정: 32비트 격자 좌표 오차 × 기울기 ≈ 1e-5 이하]. 씨앗·간격 하나만 틀려도 0.01 넘게 달라짐을 고의 변이 1회로 보인다.
  3. 파이썬 검사 `Tools/FluidFX/check_fog3d_material_hlsl.py`: 재질 스크립트를 import 해 Custom 코드·입력 이름을 얻고(2D 검사기 방식 [확인: check_fluid_fog_hlsl.py:92·110-127]) 입력 선언 + include 로 dxc ps_6_0 O3/Od 컴파일, .ush 주석 없음, Custom 몸통 4줄 고정점, 입력 이름 = C++ 파라미터 이름 집합.
  - 대안(권장 아님): 2D 처럼 `Tools/FluidFX/Shaders/TDFog3DVolume.hlsl` 을 붙여 넣고 numpy 거울로 대조 — 거울은 HLSL 을 실행하지 않아 "같은 값" 을 증명하지 못한다.

## 6. 새 시험 무대 `Tools/FluidFX/editor_make_fog3d_stage.py`
- 맵 `/Game/FluidFX/Maps/LV_TDFog3DStage`(없으면 `new_level`, 있으면 덮어쓰기 요청 파일 `Saved/FluidFX/fog3d_stage_request.json` 있을 때만 비우고 다시. 본보기 editor_make_fluid_fog_stage.py:710-722·819-842). 게임 모드 `BP_TDCombatGameMode`(같은 스크립트:28·879).
- 놓는 것(바닥 윗면 z = 0): 평지 바닥 큐브 120m × 120m × 1m(충돌 있음), DirectionalLight, SkyLight, SkyAtmosphere, ExponentialHeightFog(`enable_volumetric_fog` True, 밀도·산란·거리 = 2D 무대 상수 0.002·0.35·12000 [확인: 같은 스크립트:49-51·202-207]), 노출 고정 PostProcessVolume(2D 무대 방식), `ATDFog3DZoneActor` 중심 (0, 0, 150)·반크기 (3000, 3000, 150)·`volume_material` = M_TD_Fog3DVolume·ZoneVolume 메시 = /Engine/BasicShapes/Cube, PlayerStart (−1000, 0, 100) +X 를 봄.
- 물체(모두 `UTDFluidObstacleComponent`, `use_owner_bounds` True, 붙이는 법 editor_make_fluid_mass_input_stage.py:160-175): 낮은 상자 150×150×30cm (500, −400), 높은 상자 150×150×180cm (500, 400), 얇은 벽 10×300×250cm (1100, 0) — 크기는 설계 실험의 상자 [확인: Docs/VolumeFog3D/stage1/prereg-design-2.md:75-76], 벽 두께 = 명세 2절의 10cm 판. 서로 4m 이상.
- 2D 안개 액터를 두지 않는다(2D 구역과 20m 조건이 자명히 참). 라벨 `TDGen_Fog3D_*`, 폴더 `TDGen/FluidFX/Fog3DStage`.
- `--self-test`(unreal 없이): 상자가 구역 안·흐림 폭 밖(5m), 구역 반크기 ≥ 흐림 폭, 시작점이 구역 안, 바닥 높이 차 0. 보고 `Saved/FluidFX/fog3d_stage.json`(액터 목록·경계·preset_tuning_sha256).
- 3인칭 한 장: 기존 도구 `python Tools/FluidFX/pie_lookdev_fluid.py --variants Tools/FluidFX/fog3d_lookdev_1b.json --level /Game/FluidFX/Maps/LV_TDFog3DStage --stop-pie`(PowerShell, L-repo-01; 키 arm·view·hide·console·move 지원 [확인: Tools/FluidFX/pie_lookdev_fluid.py:2·53]). 변형: base {arm 400, view [−12, 0], 노출 고정} → zone_hidden(hide 구역 액터) / zone_shown / 걷기 move {distance_cm 400} 뒤 Mode 2 / 같은 걷기 뒤 Mode 0 / zone_shown 다시(잡음 바닥).

## 7. RHI 출력 시험 (1b-7) `Source/TDGame/FluidFX/Tests/TDFog3DLayerRhiTests.cpp`
TDGame.FluidFXRhi.Fog3D.LayerOutputFrames(`EditorContext | NonNullRHI | EngineFilter`): N = 128, RT 128×128×8, 120프레임 × dt 1/60, 플레이어가 초속 4m 로 +X 로 걸음(창이 약 28프레임마다 이동), 30프레임에 칼 구 내려찍기, 60~80프레임 가로 베기, 100프레임에 100m 순간이동. 10프레임마다 `FlushRenderingCommands` 뒤 층마다 `ReadFloat16Pixels`(본보기 TDFluidGpuFog3DRhiTests.cpp:680-690). 판정
- (가) 120번 모두 제출·거부 0(`GetSeenRejectedStepCount` == 0, 마지막에 렌더 슬롯 값도 0).
- (나) 0~29프레임 출력 모두 값 0.
- (다) 모든 읽기에서 유한, −1 − 1e-3 ≤ g ≤ 1e-3(볼록 섞기라 [−1, 0] 을 벗어나지 않음), \|d\| ≤ 500.
- (라) 내려찍기 뒤: 친 자리의 창 기준 칸(= 세계 칸 − `GetSubmittedWindowMinCell`)에서 g < −0.05, 30칸 넘게 떨어진 칸은 0. 창이 움직인 뒤 같은 세계 자리를 새 창 기준 칸에서 다시 찾았을 때도 g < −0.05(출력이 창 순서이고 재질 창 값 = 마지막 제출 창임을 확인).
- (마) 순간이동 직후 읽기: 입력 반지름 + 2칸 밖은 모두 0.
- (바) 같은 시험을 새 층·새 RT 로 두 번 → 10프레임마다 읽은 바이트가 같음(결정론).

## 8. 작업 나눔과 검증 (모두 `cd /c/Project/TDGame`, 감독 결정 반영 뒤)
| 칸 | 만들·고칠 파일 | 완료 조건 |
|---|---|---|
| 1b-1 데이터 | 새 TDFog3DTypes.h/.cpp, Tests/TDFog3DSettingsTests.cpp. 고침: PresetTuningSubsystem.cpp(include + 1줄), 정본 JSON Globals 1항목 | 데이터 검사 0, PresetData·Fog3D.SettingsValidation 통과 |
| 1b-3 물체 읽기 | 고침: TDFluidLiveSubsystem.h/.cpp. 새 Tests/TDFog3DObstacleReadTests.cpp | SubsystemObstacleRead 통과 |
| 1b-2 층 | 새 TDFog3DLayer.h/.cpp, Tests/TDFog3DLayerTests.cpp | 2절 널 RHI 시험 11개 통과 |
| 1b-4 액터 | 새 TDFog3DZoneActor.h/.cpp(+ 순수 함수 시험 2개를 LayerTests 옆 새 파일) | 빌드, Fog3D 널 RHI 전부, 동기 로드·쿠킹 검사 |
| 1b-7 출력 | 새 Tests/TDFog3DLayerRhiTests.cpp | 7절 통과(D3D12, 마감에 D3D11 1회) |
| 1b-5 재질 | 새 .ush·Probe.usf·Probe.cpp/.h·DensityRhiTests.cpp, fog3d_density_fixture.py(+ fixtures/), check_fog3d_material_hlsl.py, editor_make_fog3d_material.py | 셰이더 검사 Fog3D 10개, 재질 HLSL 검사 0, DensityMatchesReference 통과, 재질 생성 status ok·컴파일 오류 0 |
| 1b-6 무대 | 새 editor_make_fog3d_stage.py, fog3d_lookdev_1b.json | --self-test 통과, 에디터 실행 status ok |
| 1b-8 실행 | 없음 | 아래 명령 전부 + 3인칭 한 장 + 비용 |
차례: (1b-M 모듈 보강, 다른 작업자) → 1b-1 → 1b-3 → 1b-2 → 1b-4 → 1b-7 → 1b-5(코드) → 에디터 재시작 → 1b-5(재질 실행) → 1b-6 → 1b-8. 같은 빌드를 쓰므로 빌더는 한 명씩. 새 도구는 부모가 Tools/README.md 에 등록.
검증 명령
1. 새 .cpp 첫 빌드 전 `rm -f Intermediate/Build/Win64/x64/TDGameEditor/Development/Makefile.bin`(L-build-09), 에디터가 켜져 있으면 `python Tools/ue_editor.py stop`(새 USTRUCT·UCLASS 는 라이브 코딩 불가 L-build-01).
2. `python Tools/check_unity_collisions.py` → 0. `python Tools/check_fluid_preset_data.py` → 0.
3. `python Tools/build_and_test.py --filter FluidFX.Fog3D --label fog3d-1b-null` 와 `--no-build --filter FluidFX.PresetData --label fog3d-1b-preset`. tests.json 의 찾은 시험 수가 기대와 같은지.
4. `python -B Tools/FluidFX/check_fluid_gpu_shaders.py` → 0, `PASS FTDFluidGpuFog3D` FXC 줄 10개(1a 9 + 탐침 1). `python -B Tools/FluidFX/check_fog3d_material_hlsl.py` → 0. `python Tools/FluidFX/fog3d_ref/fog3d_density_fixture.py --check` → 0.
5. `python Tools/build_and_test.py --no-build --filter FluidFXRhi.Fog3D --label fog3d-1b-rhi --rhi offscreen`, 마감에 `--rhi d3d11` 1회.
6. 회귀: `--no-build --filter TDGame.FluidFXRhi --rhi offscreen`, `--no-build --filter TDGame.FluidFX`(기존 실패 1건 예외).
7. `python Tools/check_sync_loads.py`, `python Tools/check_cook_coverage.py`.
8. 에디터: `python Tools/ue_editor.py restart` → (PowerShell) `python Tools/run_in_editor.py Tools/FluidFX/editor_make_fog3d_material.py` → `python Tools/run_in_editor.py Tools/FluidFX/editor_make_fog3d_stage.py` → 6절 촬영 → `git status` 로 새 .uasset 2개(재질·맵)만, `git lfs status`.
9. 쿠킹(에디터 끈 뒤, PowerShell): `python Tools/cook_single_map.py --label fog3d-1b --map /Game/FluidFX/Maps/LV_TDFog3DStage` → 종료 0, 로그에 M_TD_Fog3DVolume 쿠킹·셰이더 오류 0(L-build-20; 전역 셰이더 캐시 함정 L-build-24 는 4번 FXC 로 보완).

## 9. 1b 통과 기준 초안에 넣을 문장
- B1 빌드·검사: 8절 2·4·7 이 모두 종료 0이고 Fog3D 셰이더가 FXC·dxc 에서 10개 검사된다.
- B2 데이터: `Globals.TDFog3DSettings` 가 C++ 로더 오류 0으로 읽히고 PresetData 시험이 모두 통과하며 헤더 기본값과 같다.
- B3 널 RHI: TDGame.FluidFX.Fog3D.* 가 모두 통과한다(이름 목록을 등록 때 고정, 찾은 수 = 목록 수).
- B4 출력: LayerOutputFrames 의 (가)~(바) 가 D3D12 오프스크린과 D3D11 에서 통과한다.
- B5 재질 식: DensityMatchesReference 가 픽스처(sha256 등록)에 대해 최대 절대 차 ≤ 1e-4·평균 ≤ 1e-5 로 통과하고, 고의 변이(씨앗 1 → 2)는 실패한다.
- B6 회귀: RHI 회귀 전부 통과, 헤드리스 회귀는 `TDGame.FluidFX.CloudSource.WindCurtainStamps`(기존 실패, decisions.md 편차 ㊻) 1건 말고 통과. 1a 시험 9개(+ 1b-M 추가분) 통과.
- B7 쿠킹: 새 맵 단일 쿠킹 종료 0, 셰이더 오류 0.
- B8 화면(모양 판정 없음): 3인칭 PIE 에서 (1) 구역 보임 대 숨김의 구역 영역 평균 밝기 차가 같은 조건 반복 쌍의 차보다 3배 이상 크고, (2) 걷기 뒤 Mode 2 대 Mode 0 장면에서 폰 둘레 2m 안 화소 차가 반복 쌍 잡음 바닥보다 크며, (3) 로그에 Fog3D 경고·RDG 오류·ensure 0, 층 거부 0.
- 정보: 구역 보임 대 숨김의 GPU 볼륨 안개 시간 차(csvprofile 600프레임 ABBA), 한 번 계산 시간.

## 10. 감독이 정할 것(권장 먼저), 확인 못 한 것, 위험
결정
1. 재질 식의 정본 위치: (권장) TDFluidGpu 셰이더 폴더 .ush + Custom include + GPU 탐침 시험 / 2D 처럼 Tools 아래 .hlsl 붙여넣기 + numpy 거울(같은 값 증명 못 함). 권장안은 TDFluidGpu 에 새 전역 셰이더 1개(0단계 탐침 선례)가 늘고 셰이더 검사 기대 수가 10이 된다.
2. 구역 기준점: (권장) 재질 ObjectPositionWS = 메시 경계 중심 + C++ `Bounds.Origin` / DoubleVectorParameter 를 MID `SetDoubleVectorParameterValue` 로 넣기(엔진에 있음 [확인: <UE>/Engine/Source/Runtime/Engine/Public/Materials/MaterialInstanceDynamic.h], 파이썬 노드 생성 미확인).
3. 배열 없는 데이터 표(낱 필드): (권장) / 기존 시험에 배열 비교를 더하기(기존 파일 변경 → 범위 밖).
4. 구역 가장자리 흐림은 가로 네 면만(위아래 면에 걸면 바닥에 붙은 안개가 사라짐): (권장) / 여섯 면.
5. 압력 횟수·가속·이동 상한을 데이터에서 뺌(명세 6절 목록에 없음, 모듈 기본값): (권장) / 데이터에 넣음.
6. 데이터 검사를 C++ 함수 + 시험으로(권장) / `check_fluid_preset_data.py` 에 규칙 추가(기존 파일 변경).
7. 몸 반지름 잠정값(손·발 20, 골반 30)과 칼 오프셋을 전투 카탈로그에서 복사(전투 쪽이 바뀌면 어긋남): 받아들임(권장) / 전투 카탈로그 공개 함수로 읽기(기존 파일 변경).
8. 처음 창: 반올림 식(권장, 플레이어가 가운데) / 기준 모의 재생의 내림 식.
확인 못 한 것
- 빌드·에디터·GPU 실행 없음(지시 범위). 볼륨 도메인 재질이 볼륨 안개 복셀화 때 ObjectPositionWS 를 제대로 받는지(2D PassiveField MI 가 같은 노드를 씀 [확인: editor_make_fluid_fog_material.py:482], 화면 확인은 못 봄).
- Custom 노드 include 가 프로젝트 모듈 가상 경로(/TDFluidGpu)에서 재질 컴파일·쿠킹 모두 되는지 [추정: 엔진 플러그인 경로와 같은 체계]. 가장 싼 확인 = 1b-5 재질 생성 직후 컴파일 오류 0 + 1b-8 쿠킹 로그.
- `bEnableVolumetricFog` 의 접근 지정자, 플레이어 메시에 `HandGrip_R` 소켓이 PIE 에서 있는지(전투 시험은 씀).
- 레벨 인스턴스의 소프트 재질 참조를 단일 맵 쿠킹이 따라가는지(r3 지도도 미확인) — 8절 9번 로그로 확인.
위험
- 모듈 보강(1b-M)의 "지난 창 거부" 규칙이 다시 시작 계산에서는 지난 창을 보지 않아야 층과 맞물린다. 1b-M 계약에 이 문장을 넣을 것.
- 렌더 스레드가 1~2프레임 늦어 거부가 보이기 전 몇 번 더 거부될 수 있다(그동안 재질 창 값과 RT 가 한 계산 어긋남, 화면 영향 미미 [추정]).
- 재질은 16비트 출력 + 하드웨어 선형 보간이라 기준 모의 칸 사이 값과 조금 다르다 — 그래서 식 시험은 상태 값을 직접 넣는다(보간 차는 1c 눈 판정).
- 칼 구가 벽 너머 칸에 닿는 것, 몸 깎기 0 등은 명세 10절 한계 그대로.

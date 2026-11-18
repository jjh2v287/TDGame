# 1a 구현 계약 — GPU 계산 핵심

확정: 2026-11-13 claude(감독·결정). 초안은 하위 에이전트(Opus 5.5)가 기존 GPU 모듈을 읽고 썼고, 아래 "감독 확정" 절이 초안 7절의 결정 항목을 닫는다. 명세 `spec.md` v1 과 어긋나면 명세가 이기고, 통과 기준은 `prereg-1a.md`(등록본)가 정본이다.

## 감독 확정 (초안 7절의 결정과 덧붙임)

1. 통과 기준의 "빈 곳 유지"는 채움 1 + g 최댓값 1e-6 이하로 등록했다(`prereg-1a.md`). 명세 3.6-5 의 d·g 섞는 식은 바꾸지 않는다.
2. 같음 판정은 C++ 에서 정규화한 바이트 비교로 한다. sha256 은 파이썬 대조 도구가 기록한다.
3. 부풀리기 함수는 GPU 모듈의 공개 함수 하나로 둔다(1b 층과 시험이 같이 쓴다).
4. 출력 렌더 타깃이 없으면 같은 형식의 임시 텍스처에 쓴다(셰이더는 하나).
5. 압력 손잡이를 끄면: 몰림은 계산하고, 압력 텍스처는 건드리지 않고, 기울기 패스는 빼기 없이 베끼며 닫힌 면만 0 으로 한다. 압력을 끈 장면의 몰림·압력 필드는 대조하지 않는다.
6. 바람 덮어쓰기 배열은 면 가운데 위치에서 64비트로 식을 계산해 32비트로 넘긴다: u 는 ((l+1)h, (l+½)h, 30k+15), v 는 ((l+½)h, (l+1)h, 30k+15), w 는 ((l+½)h, (l+½)h, 30(k+1)). 소용돌이의 x 는 창 가운데(창 최소 + N/2 칸) 기준이다. 식: uniform 은 모든 면에 같은 벡터 / vertical_osc 는 w = amp × sin(2π × t_s ÷ period_s) / vortex 는 u = sign × U × sin(πx/L) × cos(πz/L), w = −sign × U × cos(πx/L) × sin(πz/L), L = half_period_cm.
7. 재생 형식에 실제로 쓰인 덧붙임(시험 도우미가 모두 지원해야 한다): step.repeat(같은 계산을 그 횟수만큼 되풀이, 덤프는 마지막에만), step.dump_fields(덤프할 필드의 부분 집합. 없으면 10개 모두), 최상위 measure(대조 도구 전용이라 C++ 은 무시), 영역 종류 all·aabb(양 끝 포함)·halfspace·slab(d_min < (c − p)·n ≤ d_max)·cell_box(창 지역 칸 번호, 닫힌 구간)·not·union. halfspace 와 slab 은 z_min_cm·z_max_cm 를 가진다.
8. 굽기·정리 신호는 "reset ∨ 그 재생의 첫 계산 ∨ 창 최소 ≠ 지난 창 최소"로 정한다(재생 안에서 물체는 바뀌지 않는다).
9. d·g 다시 나누기에서 "제외된 칸이 있는데 남은 무게 합이 정확히 0"이면 기준 모의처럼 다시 나누지 않고 원래 무게로 섞는다(끝 칸의 무게가 0 이 아니라 생기지 않는 경우지만 꼴을 맞춘다).
10. 통과 기준 A6(창 이동)의 세 항목은 C++ 시험이 직접 판정한다. "입력이 닿지 않은 칸" = 새 띠 칸 가운데가 그 계산의 모든 구 선분에서 (반지름 + 2칸)보다 먼 칸.
11. 덤프의 step_index 는 "그 덤프까지 한 계산 수(1부터)"다.
12. 작업자는 칸을 끝낼 때마다 `Saved/AgentOps/20261113/fog3d/stage1a/` 에 넘김 글(무엇을 만들었고, 무엇이 통과했고, 다음 사람이 알아야 할 것)을 남긴다.

---

(아래는 초안 원문이다. 7절의 "감독이 정할 것"은 위 확정으로 닫혔다.)

# X1. 3D 안개 1a(GPU 계산 핵심) 구현 계약 초안

작성 2026-11-13 Opus 5.5(하위 에이전트). 명세 `Docs/VolumeFog3D/stage1/spec.md`(v1 확정)의 계산 정의는 바꾸지 않고 "엔진 코드로 어떻게 놓을지"만 정한다. 명세와 이 문서가 어긋나면 명세가 이긴다.
표기: [실측] 직접 돌린 값, [확인: 파일:줄] 파일을 열어 봄, [추정] 추론. 줄임말: RDG(렌더 의존성 그래프), UAV(쓰기 뷰), SRV(읽기 뷰), RT(렌더 타깃), CB(상수 버퍼), SM5(셰이더 모델 5), FXC(옛 D3D 셰이더 컴파일러, SM5 쿠킹이 씀).
뼈대 시험 파일: `Saved/AgentOps/20261113/fog3d/stage1a/x1_probe/`(가짜 프로젝트 뿌리. 셰이더 3개 + 선언만 든 .cpp 3개, 검사 기록 `check-run1.log`). 뼈대 셰이더는 **컴파일 확인용**이다(옮기기의 이동 상한·겹침 상자 자르기·깎기 구간·막힌 칸 채우기를 줄여 썼다). 구현자는 명세대로 새로 쓴다.

## 0. 결론 셋
1. 압력만 R32F 제자리 갱신, 나머지는 "SRV 로 읽고 다른 장 UAV 로 쓰기". 프로젝트 셰이더 검사기로 9개 셰이더가 dxc SM5·SM6 와 FXC cs_5_0 를 모두 통과했고(UAV 최대 3), 형식 있는 UAV 읽기 표시가 없다. 일부러 넣은 RGBA32F UAV 읽기 대조 셰이더는 같은 검사에서 실패했다 → 검사가 실제로 잡는다 [실측: x1_probe/check-run1.log].
2. 바람 "지금 장" 규칙 = "바람을 마지막으로 쓴 장". 정리·덮어쓰기·모드 1 옮기기는 장을 바꾸고, 평소 계산(옮기기 → 기울기)은 처음 장으로 돌아온다. 밀린 거리·채움(d·g) 장은 옮기기와 덮어쓰기 때마다 바뀐다.
3. 시험 덮어쓰기(wind_override·state_ops)는 CPU 가 창 순서 배열로 만들어 올리고, 일반 패스 하나(덮어쓰기)가 넷째 채널·닫힌 면 0 을 지키며 쓴다. 전체 상태 읽기는 "펴기 패스 → 구조체 버퍼 → 버퍼 리드백"으로 덤프 형식(.f32) 그대로 받는다.
- 명세 문제 1건(4절 끝, A11a 기준이 32비트 GPU 에서 헛실패), 엔진 함정 1건(SHA256 이 Windows 에서 없음, 6절).

## 1. 공개 헤더 `Source/TDFluidGpu/Public/TDFluidGpuFog3D.h` (선언 초안, 주석 없이 쓴다)
```cpp
#pragma once
#include "CoreMinimal.h"
#include "RendererInterface.h"
class FRDGBuilder; class FRHIGPUBufferReadback; class FTextureRenderTargetResource;

constexpr int32 TDFluidGpuFog3DLayerCount = 8;
constexpr int32 TDFluidGpuFog3DMaximumSphereCount = 16;
constexpr int32 TDFluidGpuFog3DMaximumObstacleCount = 32;
constexpr int32 TDFluidGpuFog3DMaximumEdgeBandCells = 16;
constexpr int32 TDFluidGpuFog3DDumpFieldCount = 10;

struct FTDFluidGpuFog3DSphere { FVector4f StartCmRadiusCm; FVector4f EndCmWindStrength; FVector4f VelocityCmPerSecErosionPerSec; };
static_assert(sizeof(FTDFluidGpuFog3DSphere) == 48, "FTDFluidGpuFog3DSphere must match the 48 byte shader layout.");
struct FTDFluidGpuFog3DObstacle { FVector4f CenterCmYawCosine; FVector4f HalfExtentCmYawSine; };
static_assert(sizeof(FTDFluidGpuFog3DObstacle) == 32, "FTDFluidGpuFog3DObstacle must match the 32 byte shader layout.");

struct FTDFluidGpuFog3DTuning {
  float WindRefillPerSec = 0.75f; float DisplacementRefillHorizontalPerSec = 0.1f; float DisplacementRefillVerticalPerSec = 0.3f;
  float FillRefillPerSec = 0.3f; int32 EdgeBandCells = 8; float EdgeBandAbsorptionPerSec = 4.0f;
  int32 PressureSweepCount = 8; float PressureOverRelaxation = 1.5f; float MoveLimitHorizontalCm = 125.0f; float MoveLimitVerticalCm = 120.0f; };
struct FTDFluidGpuFog3DTestKnobs { bool bAppliesPressure = true; bool bAppliesRefill = true; bool bAppliesEdgeBand = true; };
struct FTDFluidGpuFog3DStateOverride { TArray<FVector4f> WindFaces; TArray<uint8> FillOperations; };
struct FTDFluidGpuFog3DInflation { float BaseHalfThicknessCm = 7.9f; float UpwardExtraCm = 7.2f; };

struct FTDFluidGpuFog3DFrameInput {
  int32 Resolution = 256; float CellSizeCm = 15.625f; float LayerHeightCm = 30.0f; int32 Mode = 2;
  FIntPoint WindowMinCell = FIntPoint::ZeroValue; FIntPoint PreviousWindowMinCell = FIntPoint::ZeroValue;
  bool bResetsState = true; bool bObstaclesChanged = false; float DeltaSeconds = 1.0f / 60.0f;
  FTDFluidGpuFog3DTuning Tuning; FTDFluidGpuFog3DTestKnobs TestKnobs;
  TArray<FTDFluidGpuFog3DSphere> Spheres; TArray<FTDFluidGpuFog3DObstacle> Obstacles;
  const FTDFluidGpuFog3DStateOverride* StateOverride = nullptr; FTextureRenderTargetResource* OutputTarget = nullptr; };

class TDFLUIDGPU_API FTDFluidGpuFog3DState {
public:
  static int64 EstimateStateBytes(int32 Resolution);
  void Release();
  TRefCountPtr<IPooledRenderTarget> WindTargets[2]; TRefCountPtr<IPooledRenderTarget> DisplacementFillTargets[2];
  TRefCountPtr<IPooledRenderTarget> PressureTarget; TRefCountPtr<IPooledRenderTarget> DivergenceTarget;
  TRefCountPtr<IPooledRenderTarget> CellBlockedTarget; TRefCountPtr<IPooledRenderTarget> FaceFlagsTarget;
  int32 Resolution = 0; int32 CurrentWindIndex = 0; int32 CurrentDisplacementFillIndex = 0;
  FIntPoint WindowMinCell = FIntPoint::ZeroValue; bool bHasStepped = false; };

TDFLUIDGPU_API FTDFluidGpuFog3DObstacle MakeTDFluidGpuFog3DUprightBox(const FVector3d& CenterWindowCm, const FVector3d& HalfExtentCm, double YawDegrees, const FTDFluidGpuFog3DInflation& Inflation);
TDFLUIDGPU_API bool AddTDFluidGpuFog3DPasses(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, const FTDFluidGpuFog3DFrameInput& FrameInput);
TDFLUIDGPU_API int64 GetTDFluidGpuFog3DDumpByteCount(int32 Resolution);
TDFLUIDGPU_API bool AddTDFluidGpuFog3DDumpPass(FRDGBuilder& GraphBuilder, FTDFluidGpuFog3DState& State, FRHIGPUBufferReadback& Readback);
TDFLUIDGPU_API bool CopyTDFluidGpuFog3DDump(FRHIGPUBufferReadback& Readback, int32 Resolution, TArray<float>& OutValues);
```
- 위치는 모두 **창 기준 cm**(가로 = 창 최소 칸의 모서리 원점, 높이 = 구역 바닥 기준), 창은 정수 칸 번호. 세계 좌표 없음. 구 V·dt·모드는 받은 그대로 쓴다(상한·처음 계산 처리는 1b).
- 구조체 배치: CB 의 낱 값은 엔진이 이름으로 묶으므로 배치 불일치가 생기지 않는다. 구조체 버퍼 둘만 셰이더 `struct` 와 같은 float4 차례로 두고 위 static_assert 로 지킨다(본보기 [확인: Public/TDFluidGpuNearRipple.h:10-21]).
- 부풀리기 함수를 모듈에 두는 까닭: 1b 층(`FTDFog3DLayer`)과 1a 시험이 같은 정의를 쓰게(시험 모듈은 TDGame 을 볼 수 없다). 국소 축 a 마다 반두께 ← max(원래, Base + Upward × |a·위|), 돌림 cos·sin 은 64비트로 구해 32비트로(명세 3.8).
- 막는 조건(거짓 반환, 패스 없음): Resolution 이 2의 거듭제곱이 아니거나 16 미만, 창 최소가 16의 배수가 아님, 칸·층 크기 ≤ 0, Mode 가 1·2 가 아님(0 은 부르지 않는다), dt 가 유한 양수가 아님, 구 > 16, 물체 > 32, EdgeBandCells > 16, 덮어쓰기 배열 길이가 0 도 N×N×8 도 아님, 출력 RT 가 있는데 크기·형식(N×N×8, PF_FloatRGBA, UAV) 불일치.
- 한 축이라도 |창 − 지난 창| ≥ N 이면 다시 시작으로 친다(방어. 원래는 1b 가 순간이동으로 처리).

## 2. 텍스처
| 이름 | 형식 | 크기 | 만드는 법 | 쓰는 패스(UAV) | 읽는 패스(SRV) |
|---|---|---|---|---|---|
| Wind[2] (u,v,w,유체 1/0) | PF_A32B32G32R32F | N×N×8 | 3D, SRV\|UAV | 정리·덮어쓰기·옮기기(다른 장), 기울기(지금 장) | 정리·덮어쓰기·옮기기·몰림·기울기·덤프 |
| DisplacementFill[2] (dx,dy,dz,g) | PF_A32B32G32R32F | N×N×8 | 같음 | 정리(새 띠 0, 지금 장), 덮어쓰기·옮기기(다른 장) | 덮어쓰기·옮기기·덤프 |
| Pressure | PF_R32_FLOAT | N×N×8 | 같음 | 정리(새 띠 0), 압력(제자리 읽고 쓰기) | 기울기·덤프 |
| Divergence(몰림) | PF_R32_FLOAT | N×N×8 | 같음 | 몰림 | 압력·덤프 |
| CellBlocked | PF_R8_UINT | N×N×8 | 같음 | 굽기 1 | 굽기 2 |
| FaceFlags | PF_R8_UINT | N×N×8 | 같음 | 굽기 2 | 정리·덮어쓰기·옮기기·압력·기울기 |
| 출력 | PF_FloatRGBA(RT 또는 임시) | N×N×8 | 1절 | 옮기기(창 순서) | 재질(1b) |
- 만들기·등록: 처음(또는 N 이 바뀜)에는 `CreateTexture` → 모두 0 으로 `AddClearUAVPass` → `ConvertToExternalTexture` 로 State 에 둠, 그 뒤는 `RegisterExternalTexture` [확인: Private/TDFluidGpuVolFogProbe.cpp:204-208·219-266]. 다시 시작이면 8장 모두 0 으로 지우고 두 장 번호를 0 으로.
- 출력 RT 가 없으면 같은 형식의 임시 RDG 텍스처를 만들어 쓴다(셰이더 하나로 유지, 4MB 임시). 있으면 `RegisterExternalTexture(CreateRenderTarget(RHI))` + `SetTextureAccessFinal(SRVMask)` [확인: Private/TDFluidGpuNearRipple.cpp:185-193].
- 메모리: N=256 에서 524,288칸 × 74B = 38.8MB + 출력 4MB ≈ 43MB(명세 3.2 와 같다) [추정: 계산].
- 장 교대 규칙
  - 바람: 정리(지금 → 다른, 번호 바꿈) / 덮어쓰기(지금 → 다른, 바꿈) / 옮기기(지금 → 다른) → 몰림(다른 장 읽음) → 기울기(다른 → 지금, 번호 그대로) / 모드 1: 옮기기가 다른 장에 0 + 유체 표시를 쓰고 번호를 바꿈(기울기 없음).
  - d·g: 정리는 지금 장(= 이번에 읽을 장)의 새 띠만 0 / 덮어쓰기·옮기기는 지금 → 다른, 번호 바꿈.
  - 압력: 한 장. 몰림: 한 장(쓰고 나서 읽음).
- 형식 있는 UAV 읽기는 압력 패스의 `RWTexture3D<float>` 하나뿐이다. R32F UAV 읽기는 D3D11 FL11.0 기본 지원이라 `CFLAG_AllowTypedUAVLoads` 가 필요 없고, FXC 디스어셈블에 그 표시가 없어 검사기 판정 (C) 를 통과했다 [실측]. 다른 패스가 자기 텍셀 넷째 채널을 "다시 쓰기"는 SRV(지금 장)에서 읽고 UAV(다른 장)에 쓰므로 UAV 읽기가 아니다.

## 3. 패스 (한 번 계산)
공통: 3D 패스는 `[numthreads(8, 8, 4)]`, 디스패치 (N/8, N/8, 2). 스레드 = 지역 칸 l(창 순서). 저장 자리 = ((창 최소 + l.xy) & (N−1), k)(음수도 2의 보수 & 로 맞음). 공통 CB(`FTDFluidGpuFog3DCommonParameters`, 비공개 헤더 `Private/TDFluidGpuFog3DPasses.h` 에 `BEGIN_SHADER_PARAMETER_STRUCT`): Resolution, CellSizeCm, LayerHeightCm, WindowMinCell, PreviousWindowMinCell.
| 차례 | 클래스 / 진입점(.usf) | 언제 | 읽기 → 쓰기 | 명세 |
|---|---|---|---|---|
| 가1 | FTDFluidGpuFog3DBakeBlockedCS / Fog3DBakeBlockedCS (Fog3D.usf) | 다시 시작·첫 계산·창 이동·물체 변경 | 물체 버퍼 → CellBlocked | 3.8 굽기(열린 부등호, 칸 가운데 = (l+½)h, (k+½)×30) |
| 가2 | …BakeFaceFlagsCS / Fog3DBakeFaceFlagsCS | 같음 | CellBlocked(자기+창 안 이웃 6) → FaceFlags. 비트0 자기 막힘, 비트1~6 = +x,−x,+y,−y,+z,−z 가 "자기나 이웃 막힘"으로 닫힘. 이음매·바닥은 굽지 않음 | 3.2 표, 3.8 |
| 나 | …CleanCS / Fog3DCleanCS | 같음 | Wind 지금, FaceFlags → Wind 다른(닫힌 면·이음매·새 띠 0, 넷째 = 비트0 의 반대), DF 지금(새 띠 0), Pressure(새 띠 0). UAV 3 | 3.3 나, 3.9 |
| 시험 | …OverrideCS / Fog3DOverrideCS | StateOverride 가 있을 때만, 정리 뒤·옮기기 앞 | Wind·DF 지금, FaceFlags, 덮어쓸 배열 → Wind·DF 다른. UAV 2 | 3.12 |
| 1 | …AdvectCS / Fog3DAdvectCS | 매번 | Wind·DF 지금, FaceFlags, 구 버퍼 → Wind·DF 다른, 출력. UAV 3 | 3.3-1, 3.4, 3.6, 3.7, 3.9 띠 |
| 2 | …DivergenceCS / Fog3DDivergenceCS | 모드 2 | Wind 다른 → Divergence | 3.5 첫 줄 |
| 3 | …PressureColumnCS / Fog3DPressureColumnCS (Fog3DPressure.usf) | 모드 2, 압력 손잡이 켬. 0색·1색 × 훑기 수 = 16번 | Divergence, FaceFlags, Pressure(제자리) | 3.5 |
| 4 | …GradientCS / Fog3DGradientCS | 모드 2 | Wind 다른, Pressure, FaceFlags → Wind 지금 | 3.5 기울기, 3.3-4 |
| 덤프 | …DumpCS / Fog3DDumpCS | 시험이 부를 때 | Wind·DF 지금, Pressure, Divergence → 구조체 버퍼(float, 10 × N×N×8) | 3.12, 덤프 형식 |
- 평소 19번, 굽기·정리 계산 +3(명세 3.3 과 같다), 덮어쓰기 +1(시험만). 압력 디스패치: `[numthreads(8, 8, 1)]`, (N/16, N/8, 1), 지역 x = 2·tx + ((ty + 색) & 1)(색 = (지역 x + y) 의 홀짝, 창 원점이 16의 배수라 저장 자리 홀짝과 같다).
- 옮기기 안의 차례(명세 3.3-1 그대로): ① 자기 칸 막힘이면 바람 0 + 넷째 그대로, d·g 와 출력 = "창 안·층 안 유체 면 이웃의 지난 d·g 평균(없으면 0)"(유체 여부는 이웃의 Wind 지금 넷째 채널) 후 끝 ② 이동 δ = −V·dt, 상한 비율 곱(성분 0 은 빼고) ③ 겹침 상자·높이 15~225 로 자르기 ④ 길 따라가기 ⑤ 8칸 읽기·이어짐 ⑥ 되돌림 ⑦ 가장자리 띠 ⑧ 창 이동 계산이면 새 띠 칸의 바람·d·g 를 0 으로 "골라" 씀 ⑨ 구마다 바람 섞기(u·v·w 각 면 가운데) ⑩ 깎기 합 X > 0 인 칸만 g 갱신 ⑪ 닫힌 면 바람 0 골라 쓰기 ⑫ 모드 1 이면 바람·d 를 0 으로 ⑬ 쓰기(바람 넷째 = 자기 지금 장 넷째, 출력은 지역 칸 l 자리에 (d, g)).
- 모드 1 은 같은 셰이더에서 CB 값 Mode 로 갈라 ②~⑤·⑦·⑧(바람·d 부분)을 건너뛰고 g ← g × 채움 계수 → ⑧(새 띠 g 0) → ⑩ → ① 만 한다(명세 3.11). 굽기·정리 조건은 모드 2 와 같다.
- 압력 손잡이 끔: 몰림은 계산(덤프용), 압력 디스패치 없음, 기울기는 CB `AppliesPressureGradient = 0` 으로 빼기 없이 베끼며 닫힌 면만 0. 압력 텍스처는 건드리지 않는다 [추정: 기준 모의의 knobs.pressure=false 와 같은지 1a-0 에 확인 요청].
- 계수는 CPU(모듈)가 64비트로 계산해 32비트로 넘긴다: 되돌림 넷 = exp(−비율 × dt)(손잡이 끔이면 정확히 1), 가장자리 띠 표 16칸 = exp(−흡수 × (1 − smoothstep(0, B, i + 0.5)) × dt)(i = min(lx, N−1−lx, ly, N−1−ly), 손잡이 끔이면 1). 셰이더 안 `exp` 는 깎기 exp(−X) 하나뿐.

### 3.1 길 따라가기·이어짐을 셰이더로 옮길 때
- 좌표는 "자기 칸 가운데를 원점으로 한 칸 안 cm"(32비트). x = 0, x1 = 자른 δ. 경계 = (끝 칸 오프셋 ± ½) × h(높이는 × 30), 가운데 = 오프셋 × h, 겹침 상자 = (lo − l) × h ~ (hi − l) × h. 이 값들은 15.625 = 125/8 의 정수배라 32비트에서 정확하다 [추정: 2²⁴ 미만]. t = 경계 ÷ x1 성분 → 가로·세로 δ 가 같으면 t 가 비트까지 같다(명세 3.6-4 의 약속).
- 같은 t 는 x → y → z 순 앞 축(엄격한 `<`). 시작값 +무한, 고를 축이 없거나 t > 1 이면 끝. 매 되풀이에서 "묶기" 검사를 먼저, 그다음 경계 찾기. 칸 c 의 닫힘 = FaceFlags 비트 ∨ 이음매(가로 지역 0 에서 −, N−1 에서 +) ∨ 바닥(층 0 에서 −z). 맨 위 +z 는 열림.
- `[loop]` 24번 상한. 반복 안에서 벡터 성분을 변수 번호로 쓰지 않는다: 끝 칸 옮기기는 `EndOffset += select(축 == int3(0,1,2), 방향, 0)`(FXC 의 "l-value 번호" 오류 회피). 방향 = `int3(sign(x1))`(−0 은 0 → 처음부터 묶임).
- 8칸 기준: 표본 칸 단위 f = e ÷ (h, h, 30), 기준 = 자기 칸 + floor(f), t = f − floor(f), 기준이 (N−2, N−2, 6) 를 넘으면 그 축 t = 1 로 두고 기준을 자름. e 가 칸 가운데에 묶이면 f 가 정수로 정확히 나와 t = 0 이다.
- 이어짐: 모서리 번호 = 4·x비트 + 2·y비트 + z비트, 끝 칸 번호 r, 막힘 비트 M(8칸의 넷째 채널 < 0.5). k6 1절 식 그대로(R4·R2·R1 → R6·R5·R3 → R7 → ④). 축에 대해 대칭인 규칙이라 비트와 축의 짝짓기는 결과에 영향이 없다. 끝 칸이 묶음 밖(오프셋이 0/1 아님)이면 이어짐 = 유체 모두.
- 바람 섞기: z 쌍 → y → x 차례로 `a*(1-t) + b*t`(`lerp` 금지). 이어지지 않은 유체 칸은 0 을 골라 넣고, 막힌 칸은 그대로(면 값 0). d·g: 무게 = 세 축 무게 곱(축 무게는 `select(위쪽 비트, t, 1−t)`), 제외(막힘 ∨ 이어지지 않음)가 하나라도 있으면 남은 합으로 나눔. 8칸 모두 막힘(원래 M 만으로)이면 자기 칸 값.

## 4. 정지 0 과 결정론
- 0 유지: 닫힌 면·새 띠·모드 1 은 0 을 `select`/삼항으로 고른다(곱하지 않음, 명세 3.1). 깎기는 X > 0 일 때만. 섞기는 `a*(1-t)+b*t`. 바람 0 이면 δ = ±0 → 모든 축 묶임 → e = 0 → t = 0, x − e = 0 → 결과 ±0(값이 0, 시험은 −0 을 0 으로 본다).
- 결정론: 원자 연산·웨이브 연산·공유 메모리 없음. 같은 색 기둥은 서로 읽지 않는다. 압력 UAV 는 `GraphBuilder.CreateUAV(텍스처)` 기본값으로 만들어 패스 사이 UAV 장벽을 둔다 — `ERDGUnorderedAccessViewFlags::SkipBarrier`(프로젝트의 `MakeUnorderedUAV` 같은 도우미) 금지 [확인: Engine RenderCore/Public/RenderGraphDefinitions.h:221-227, Source/TDFluidGpu/Private/TDFluidGpuStepPasses.cpp:1221]. 한 패스에서 같은 텍스처를 SRV 와 UAV 로 함께 묶지 않는다(정리는 d·g 지금 장을 쓰기만 한다). 덮어쓰기 배열 업로드·계수는 CPU 결정적.
- 먼 원점(A4): 계산은 지역 번호만 쓰고 창 최소는 저장 자리 & 에만 들어간다 → 원점과 무관하게 같은 비트 [추정].
- **명세 문제(A11a)**: 명세 3.6-5 의 d·g "Σ(무게 × 값), 제외 없으면 다시 나누지 않음"을 32비트로 계산하면 고른 값 −1 장이 −1 로 남지 않는다. 무작위 t 20만 개에서 28.4% 칸이 1 + g ≠ 0, 최대 2.4e-7 [실측: numpy float32, 아래 7절]. 기준 모의는 64비트 계산 뒤 32비트 저장이라 −1 이 정확히 나온다. 그래서 prereg 초안 A11a "1 + g 의 최댓값이 0" 은 올바른 GPU 구현에서도 실패할 수 있다. 같은 실험에서 `a(1−t)+bt` 나무 꼴은 0% 였다.

## 5. 시험 대응표 (이름 `TDGame.FluidFXRhi.Fog3D.*`, 모두 `EAutomationTestFlags::EditorContext | NonNullRHI | EngineFilter`)
| 기준 | 판정 | 시험 이름 | 재생 파일(replays/) |
|---|---|---|---|
| A1 정지 | C++ 직접(덤프 7필드 + 압력이 모두 값 0, 막힌 칸 포함. 50번마다와 끝) | RestStaysZero | a1_rest_shift.json |
| A2 결정론 | C++ 직접(새 State 로 두 번, 정규화 바이트 같음) | Determinism | a2_determinism.json |
| A3 대조 | 덤프 → 파이썬 | ReplayDumps | s_ga…s_a 8개(가 내려찍기, 나 낮게, 다 높은 상자, 라 달리기, 마 1/30, 바 256, 사 얇은 벽, 아 이음매) |
| A4 먼 원점 | C++ 직접(장면 나를 +1,000,000·−1,000,000·+1,000,016칸 옮김: anchor·창·지난 창 정수에 더함, 바이트 같음) | FarOrigin | s_na_low_swing.json |
| A5 가라앉음 | 덤프 → 파이썬(유한성은 C++ 도 확인) | ReplayDumps | a5_settle.json |
| A6 창 이동 | 상태 부분: 덤프 → 파이썬 / 출력 텍스처 새 띠 안쪽 0: C++ 직접(볼륨 RT 를 넘겨 층마다 `ReadFloat16Pixels`) | WindowShiftOutput + ReplayDumps | s_ra_run.json |
| A7 닫힌 면 | C++ 직접(덤프의 유체 표시로 닫힌 면을 셈: 자기·이웃 비유체, 지역 N−1 의 u·v) | ClosedFaces | s_da_tall_box.json, s_ra_run.json |
| A8 압력 품질 | 덤프 → 파이썬(풀기 전 몰림 = 덤프 8번 필드, 뒤 몰림 = 최종 바람으로 다시 셈) | ReplayDumps | a8_t1…a8_t5, a8_moving_256 |
| A9 붓 높이 | 덤프 → 파이썬 | ReplayDumps | a9_brush_height.json(높이마다 reset + 1계산 + dump) |
| A10 깎기 | 덤프 → 파이썬 | ReplayDumps | a10_erosion_dt60/dt30/dt144 |
| A11 빈 곳·소용돌이 | 덤프 → 파이썬 | ReplayDumps | a11a_empty_osc, a11d_vortex |
| A12 이음매 | 덤프 → 파이썬 | ReplayDumps | s_a_seam.json(+1초 덤프) |
| A13 누수 | 덤프 → 파이썬 | ReplayDumps | a13_* (두꺼운 5, 얇은 벽 경우 × dt 2, 쌓은 벽, 막힌 방) |
| A14 밀폐 칸 | C++ 직접(유한, 바람 최대 ≤ 3000) | SealedColumn | a14_sealed.json |
| A15 모드 1 | 덤프 → 파이썬 | ReplayDumps | m1_na.json, m1_ra.json |
| A16 | 명령 실행(6절) | — | — |
| 정보 | 시간(시작·끝 RQT_AbsoluteTime, 0단계 방식 [확인: Private/Tests/TDFluidGpuVolFogProbeRhiTests.cpp:137-199]) | StepTiming | — |
- ReplayDumps 는 replays 폴더의 모든 파일을 돌려 `dump` 가 있는 계산마다 덤프를 쓴다. 【기준값】이 필요한 판정은 모두 파이썬, 고정 문턱(0·같음·유한·3000)만 C++.
- C++ 도우미(1a-4, `Private/Tests/` 아래, 파일 전체를 `#if WITH_DEV_AUTOMATION_TESTS` 로 감쌈. 이름공간은 파일마다 `TDFluidGpuFog3DReplayPrivate` 처럼 다르게)
  - `TDFluidGpuFog3DReplay.h/.cpp`: 재생 파일 읽기(모듈의 Json 비공개 의존 있음 [확인: Source/TDFluidGpu/TDFluidGpu.Build.cs:20]), 형식·키 검사(모르는 format 이면 실패), 계산마다 FrameInput 만들기(위치 → 창 기준 = 위치 + (anchor − 창 최소) × cell_cm 을 64비트로 계산 후 32비트, 물체는 매 계산 다시 바꿔 `MakeTDFluidGpuFog3DUprightBox` 로 부풀림, 굽기 신호 = reset ∨ 첫 계산 ∨ 창 ≠ 지난 창), wind_override 배열(면 가운데 위치에서 식을 64비트로: u 는 ((l+1)h, (l+½)h, 30k+15), v 는 ((l+½)h, (l+1)h, …), w 는 ((l+½)h, (l+½)h, 30(k+1)), 소용돌이 x 는 창 가운데 N·h/2 기준), state_ops 배열(칸 가운데로 영역 판정, 차례대로 적용 = 마지막 적용 연산이 이김, 0 유지·1 정지·2 비움, 셰이더가 유체 칸만 바꿈).
  - `TDFluidGpuFog3DDumpFiles.h/.cpp`: 덤프 패스 + 버퍼 리드백 + 준비될 때까지 다시 제출(상한 16, L-build-21 [확인: Private/Tests/TDFluidGpuVolFogProbeRhiTests.cpp:110-118]), `.f32`(리틀 엔디언 float 그대로)·`.json` 쓰기(Saved/FluidFX/Fog3D/gpu/<장면>__<덤프>.*), 정규화 바이트(u,v,w,dx,dy,dz,g 필드 차례, 비유체 칸 0, −0 → +0), 재생 실행기(게임 스레드 반복에서 계산마다 `ENQUEUE_RENDER_COMMAND` 하나, State 는 `TSharedPtr<…, ESPMode::ThreadSafe>`, 덤프는 같은 명령 안 별도 그래프 뒤 `FlushRenderingCommands`).
  - 해시: **엔진 `FPlatformMisc::GetSHA256Signature` 는 Windows 구현이 없어 `checkf(false)` 로 멈춘다** [확인: Engine Source/Runtime/Core/Private/GenericPlatform/GenericPlatformMisc.cpp:2019-2024, Windows 쪽 재정의 없음]. C++ 은 정규화 바이트를 `FMemory::Memcmp` 로 비교해 판정(해시 같음보다 강함)하고, sha256 은 파이썬 대조 도구가 .f32 로 계산해 기록한다.
  - 읽기 경로: 상태는 3D 텍스처를 직접 복사하지 않고 덤프 패스가 창 순서로 펴서 `RWStructuredBuffer<float>` 에 쓰고 `AddEnqueueCopyPass(Builder, FRHIGPUBufferReadback*, Buffer, Bytes)` [확인: Engine RenderCore/Public/RenderGraphUtils.h:847]. 바이트 = 10 × N×N×8 × 4(128: 2.6MB, 256: 21MB).

## 6. 작업 나눔과 칸마다 검증
| 칸 | 만들 파일 | 완료 조건 |
|---|---|---|
| 1a-1 | Public/TDFluidGpuFog3D.h, Private/TDFluidGpuFog3D.cpp(State·막는 조건·텍스처 만들기/지우기·셰이더 클래스 9개 선언+파라미터 구조체·패스 차례), Private/TDFluidGpuFog3DPasses.h(공통 CB), Shaders/Private/TDFluidGpuFog3DCommon.ush·TDFluidGpuFog3D.usf(진입점 8개, 몸통은 베끼기·0 쓰기 수준)·TDFluidGpuFog3DPressure.usf(제자리 R32F 읽기 포함), Private/TDFluidGpuFog3DPressure.cpp(압력 클래스), Private/Tests/TDFluidGpuFog3DRhiTests.cpp(ModuleSmoke: 막는 조건, 빈 입력 3계산 뒤 덤프 모두 0) | 검사 통과 + Fog3D 셰이더 9개가 실제로 검사됨 + 빌드·ModuleSmoke 통과 + 전역 셰이더 SM5 쿠킹 |
| 1a-2 | 같은 .cpp·.usf 몸통: 굽기 2, 정리, 덮어쓰기, 옮기기 전체, 몰림, 기울기(압력 0 으로), 덤프, `MakeTDFluidGpuFog3DUprightBox` | 위 + RestStaysZero(압력 끈 판도 됨)·ClosedFaces 임시판 통과 |
| 1a-3 | TDFluidGpuFog3DPressure.usf·.cpp 몸통, 피벗 보호 1e-12 × 대각 | 위 + SealedColumn 통과, RestStaysZero 압력 켬 통과 |
| 1a-4 | Private/Tests/ 도우미 4파일 + 시험 9개 완성(5절) | 모든 C++ 직접 판정 통과, 덤프 파일 생성(파이썬 판정은 1a-5) |
- 검증 명령(칸마다, PowerShell 또는 Git Bash, 모두 `cd C:/Project/TDGame` 에서)
  1. `python -B Tools/FluidFX/check_fluid_gpu_shaders.py` → 종료 0, 그리고 `FXC cs_5_0` 의 `PASS FTDFluidGpuFog3D` 줄이 9개, dxc `PASS FTDFluidGpuFog3D` 줄이 9 × 2(SM5·SM6) × HLSL 판 수(이 기계에서는 1 [실측]). 줄 수가 모자라면 검사기가 말없이 건너뛴 것이다: 클래스 선언이 `class FTDFluidGpuFog3D…CS : public 부모` 꼴이 아님(`final` 넣으면 정규식이 놓침), 선언과 `IMPLEMENT_GLOBAL_SHADER` 가 다른 파일, .cpp 가 Private 하위 폴더 [확인: Tools/FluidFX/check_fluid_gpu_shaders.py:28-29·176]. .usf·.ush 에 주석 금지(:32).
  2. `python -B Tools/check_unity_collisions.py` → 0. 공통 CB 구조체는 비공개 헤더 한 곳에만(두 .cpp 에 같은 이름으로 정의하면 통합 빌드 재정의).
  3. 새 .cpp 를 더한 첫 빌드 전: `rm -f Intermediate/Build/Win64/x64/TDGameEditor/Development/Makefile.bin`(L-build-09, `build_and_test.py` 는 지우지 않는다 [확인: Tools/build_and_test.py 에 Makefile 문자열 없음]). 빌드 로그에 `Creating makefile` 이 있고 tests.json 의 찾은 시험 수가 기대와 같은지 본다.
  4. `python Tools/build_and_test.py --filter FluidFXRhi.Fog3D --label fog3d-1a-<칸> --rhi offscreen`.
  5. 회귀(1a-4 끝과 1a 마감): `python Tools/build_and_test.py --no-build --filter TDGame.FluidFXRhi --label fluidfx-rhi --rhi offscreen`, `python Tools/build_and_test.py --no-build --filter TDGame.FluidFX --label fluidfx-null`.
  6. 셰이더를 바꾼 칸마다 SM5 전역 셰이더 쿠킹 1회(L-build-20: `UnrealEditor-Cmd -run=CookGlobalShaders -platform=Windows` 또는 `python Tools/cook_single_map.py --label fog3d-1a-<칸>`), 1a 마감에 `--rhi d3d11` 로 Fog3D 시험 1회, `python Tools/check_sync_loads.py`·`python Tools/check_cook_coverage.py`(1a 는 에셋 경로 없음).
- 오프라인 검사가 못 잡는 것: C++ 파라미터 구조체 이름과 .usf 전역 이름의 불일치(엔진이 셰이더를 실제로 컴파일할 때만 드러남 → 4번 시험 실행에서 확인) [추정: 엔진 바인딩 검사 문구 미확인].

## 7. 감독이 정할 것과 확인 못 한 것
결정(권장 먼저)
1. A11a 기준(명세 문제): (권장) prereg 의 A11a 를 설계 D-G12a 와 같은 "1 + g 최댓값 ≤ 1e-6" 으로 / 명세 3.6-5 를 "d·g 는 늘 남은 무게 합으로 나눈다"로 바꾸고 기준 모의도 맞춤(수학적으로 같고 고른 장을 정확히 보존) / 그대로 두고 실패를 감수.
2. 해시: C++ 은 바이트 비교, sha256 은 파이썬(권장) / OpenSSL 모듈 의존 추가(기존 Build.cs 변경이라 1a 범위 밖).
3. 부풀리기 함수 위치: GPU 모듈 공개 함수(권장, 정의 하나) / 1b 층 + 시험 쪽 복제.
4. 출력 RT 없을 때: 임시 텍스처(권장) / 순열 하나 추가.
5. 압력 손잡이 끔의 뜻(압력 장 유지·기울기 안 뺌·몰림은 계산)과 wind_override 의 계산 자리(면 가운데, 소용돌이 x = 창 가운데 기준), 정규화 바이트의 필드 차례 — 1a-0 기준 모의와 같은지 확인 요청.
6. 재생 형식 제안(바꾸지 않음): 계산마다 `rebake` 참거짓을 두면 굽기 조건 추정이 필요 없다(지금은 "reset ∨ 첫 계산 ∨ 창 이동"으로 유도). A6 출력 판정을 파이썬으로 옮기려면 덤프에 출력 4필드를 붙인 `.out.f32` 가 필요하다.
확인 못 한 것
- 실제 엔진 셰이더 컴파일·빌드·GPU 실행(빌드 금지 범위). 같은 그래프 안에서 State 를 다시 등록해 덤프하는 경로(별도 그래프를 권장).
- D3D11 에서 `FRHIGPUBufferReadback` 준비 지연이 텍스처 리드백과 같은지(L-build-21 은 텍스처 경로) — 다시 제출 반복으로 같이 덮는다.
- 기준 모의 코드(1a-0 전)와의 세부 일치: 모서리 합 차례, 높이 성분 경계 표, 모드 1 의 새 띠 처리.
실측 기록: `x1_probe/check-run1.log`(대조 셰이더 포함 10개 × SM5·SM6 = dxc 20줄 모두 PASS, FXC 는 9개 PASS·UAV 옮기기 3·굽기 1·1·정리 3·덮어쓰기 2·몰림 1·기울기 1·덤프 1·압력 1, 대조 셰이더만 "Typed UAV Load Additional Formats" 로 FAIL, 검사 종료 코드 1 은 이 대조 때문). 실행: `python -B Tools/FluidFX/check_fluid_gpu_shaders.py --root <절대경로>/Saved/AgentOps/20261113/fog3d/stage1a/x1_probe`. 32비트 합 실험은 numpy float32 로 무작위 t 20만 개에 8칸 무게 곱의 합과 `a(1−t)+bt` 나무 꼴을 비교했다.

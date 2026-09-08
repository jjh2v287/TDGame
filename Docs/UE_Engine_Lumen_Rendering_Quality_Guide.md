# 언리얼 엔진 5.8 루멘 렌더링 퀄리티 및 신기술 심층 가이드
(Unreal Engine 5.8 Lumen Quality & Advanced Rendering Guide: Nanite-Off Pipeline · Light Probes · MegaLights · Stylized/NPR)

- 대상 엔진: Unreal Engine 5.8 (5.5+ 호환)
- 목적: 나나이트(Nanite)를 비활성화한 프로젝트에서 루멘(Lumen)의 시각적 품질을 극대화하고, UE 5.8의 신규 기능인 **라이트 프로브(Irradiance Field Gather)**와 **메가라이츠(MegaLights)**를 적용하며, 실사(Photorealistic) 및 비실사(Stylized / NPR) 양쪽 파이프라인에서 최고 품질을 구현하기 위한 심층 아키텍처 가이드를 제공합니다.

---

## 목차
1. [나나이트 미사용 환경에서의 루멘 아티팩트와 극복 방안](#1-나나이트-미사용-환경에서의-루멘-아티팩트와-극복-방안)
2. [UE 5.8 라이트 프로브: Irradiance Field Gather 심층 분석](#2-ue-58-라이트-프로브-irradiance-field-gather-심층-분석)
3. [차세대 다이내믹 라이팅 MegaLights 아키텍처와 활용](#3-차세대-다이내믹-라이팅-megalights-아키텍처와-활용)
4. [실사(Photorealistic) 품질 극대화 파이프라인](#4-실사photorealistic-품질-극대화-파이프라인)
5. [비실사(Stylized / NPR) 품질 유지 및 오염 방지 파이프라인](#5-비실사stylized--npr-품질-유지-및-오염-방지-파이프라인)
6. [실전 엔진 설정(INI / CVar) 종합 레퍼런스](#6-실전-엔진-설정ini--cvar-종합-레퍼런스)
7. [아키텍처 절충점 요약 (Trade-offs)](#7-아키텍처-절충점-요약-trade-offs)

---

## 1. 나나이트 미사용 환경에서의 루멘 아티팩트와 극복 방안

### 1.1 나나이트 OFF 시 발생하는 문제점
- **소프트웨어 레이 트레이싱(SWRT) 폴백 한계**: 나나이트가 켜져 있으면 루멘은 나나이트 프록시 메쉬(Fallback Mesh)를 기반으로 고밀도 레이 트레이싱을 수행합니다. 그러나 나나이트가 꺼지면 **메쉬 디스턴스 필드(Mesh Distance Fields, MDF / SDF)**와 **글로벌 디스턴스 필드(GDF)**에 전적으로 의존합니다.
- **빛샘(Light Leaking)**: 얇은 두께의 벽체, 모서리, 복잡한 비다양체(Non-manifold) 기하 구조에서 SDF 해상도가 부족해 빛이 벽을 뚫고 내부로 새어 들어오는 현상이 심화됩니다.
- **서피스 캐시(Surface Cache) 블랙 블록 현상**: 루멘은 사물의 표면을 평면 카드(Lumen Mesh Cards)로 투영하여 라이팅을 캐싱합니다. 나나이트가 비활성화된 복잡한 스태틱 메쉬의 경우 카드 커버리지(Coverage)가 급감하여 표면에 검은 얼룩이나 사각형 블록 아티팩트가 노출됩니다.

### 1.2 핵심 해결책: 하드웨어 레이 트레이싱(HWRT) 전환 및 메쉬 카드 보정
1. **하드웨어 레이 트레이싱(HWRT) 강제**:
   - `r.Lumen.HardwareRayTracing 1`을 설정하면 거리 필드(SDF) 대신 실제 지오메트리의 삼각형 데이터(BLAS: Bottom-Level Acceleration Structure)를 직접 추적합니다.
   - 나나이트가 없더라도 메쉬의 실제 LOD0/LOD1 삼각형을 정밀하게 교차 검사하므로 얇은 벽면 빛샘과 모서리 누수가 완전히 해결됩니다.
   - `r.RayTracing.StaticMeshes.LOD`를 조절하여 레이 트레이싱 전용 LOD를 지정하면 성능과 기하 정밀도를 동시에 제어할 수 있습니다.
2. **SWRT 유지 시 필수 보정 파라미터**:
   - 하드웨어 레이 트레이싱을 사용할 수 없는 환경이라면 스태틱 메쉬 에디터에서 `Distance Field Resolution Scale`을 `2.0 ~ 3.0`으로 상향해야 합니다.
   - 스태틱 메쉬의 빌드 설정에서 `Max Lumen Mesh Cards`를 기본값(12개)에서 `24 ~ 32개`로 증가시켜 서피스 캐시가 복잡한 디테일까지 온전히 덮도록 해야 합니다.

---

## 2. UE 5.8 라이트 프로브: Irradiance Field Gather 심층 분석

### 2.1 메커니즘
UE 5.8 렌더러 소스(`LumenIrradianceFieldGather.cpp`)에 구현된 `r.Lumen.FinalGatherMethod 0`은 기존의 스크린 프로브 개더(Screen Probe Gather)를 보완/대체하는 **월드 스페이스 3차원 라이트 프로브 시스템(Irradiance Field Gather)**입니다.

- **스크린 프로브 개더(Screen Probe Gather - Method 1)**: 화면 픽셀 공간에 2D 프로브를 분배하여 레이를 쏘고, 시점 이동 시 시간 축 누적(Temporal Accumulation)을 수행합니다. 지오메트리 접촉 차폐(Contact AO) 디테일이 높지만, 카메라가 빠르게 회전하거나 화면 밖 지오메트리가 들어올 때 노이즈 및 고스팅(Ghosting)이 발생할 수 있습니다.
- **이래디언스 필드 개더(Irradiance Field Gather - Method 0)**: 카메라 주변 월드 공간에 다단계 클립맵(Clipmap) 형태로 3차원 복사 캐시 프로브(Radiance Cache Probes) 그리드를 배치합니다. 각 프로브에서 주변 구면 조도(Irradiance)와 차폐(Occlusion)를 미리 계산한 후, 픽셀 셰이더에서 이웃 프로브들을 삼선형 보간(Trilinear Interpolation)하여 간접광을 입힙니다.

```
[카메라 중심 3D 프로브 클립맵 그리드]
   ● --- ● --- ●       ● : 월드 스페이스 프로브 (Radiance Cache)
  /     /     /|       - 구면 조도(Irradiance) 사전 연산
 ● --- ● --- ● |       - 프로브 차폐(Probe Occlusion) 계산
 |     |     | ●  -->  [픽셀 단위 프로브 삼선형 보간]
 ● --- ● --- ●/         -> 시간 누적 노이즈/플리커링 원천 차단
```

### 2.2 특징 및 장점
- **시간 축 노이즈 및 화면 떨림(Flickering) 원천 억제**: 스크린 스페이스 시간 누적에 의존하지 않으므로 빠른 카메라 전환이나 캐릭터 이동 시에도 간접광의 플리커링이 현저히 줄어듭니다.
- **안정적이고 균일한 간접광(GI) 확산**: 노이즈가 없는 부드러운 전역 조명을 제공하므로, 미세한 픽셀 노이즈에 치명적인 비실사/카툰(NPR) 렌더링 및 중간 사양 하드웨어 환경에서 최상의 안정성을 제공합니다.

---

## 3. 차세대 다이내믹 라이팅 MegaLights 아키텍처와 활용

### 3.1 MegaLights의 패러다임 전환
언리얼 5.5에 도입되어 5.8에서 안정화된 **MegaLights**(`r.MegaLights.EnableForProject 1`)는 에픽게임즈가 기존 섀도우 맵 캐스케이드(CSM) 및 버추얼 섀도우 맵(VSM)의 한계를 극복하기 위해 설계한 확률적 레이 트레이싱 기반 다이렉트 라이팅 시스템입니다.

- **기존 방식의 한계**: 수십~수백 개의 포인트/스팟 라이트가 각각 그림자를 생성하면 VSM 페이지 할당 및 드로우콜 폭증으로 렌더링 성능이 급락했습니다. 이로 인해 대부분의 조명에서 그림자를 끄거나 거리를 제한해야 했습니다.
- **MegaLights의 원리**: 화면 내에 수백~수천 개의 그림자 드리우는 동적 광원이 존재하더라도, 픽셀당 가장 기여도가 높은 소수의 조명만을 통계적 가중치(Importance Sampling)로 선별하여 광선(Ray)을 투사합니다. 이를 시공간적 디노이저(Spatio-Temporal Denoiser)로 복원하여 모든 광원에 면적 광원(Area Light) 수준의 부드러운 반그림자(Soft Penumbra)를 실시간으로 입힙니다.

### 3.2 MegaLights 품질 튜닝
- `r.MegaLights.NumSamplesPerPixel`: 픽셀당 샘플 레이 수 (기본값 1, 품질 모드 2~4 권장). 값이 높을수록 미세한 접촉 그림자 노이즈가 제거됩니다.
- `r.MegaLights.LightingChannels 1`: 라이팅 채널을 온전히 지원하므로 캐릭터 전용 라이트 분리가 필요한 프로젝트에서도 완벽히 동작합니다.
- `r.MegaLights.DirectionalLights 1`: 디렉셔널 라이트까지 통합 처리하여 태양광과 실내 인공조명 간의 이음새 없는 자연스러운 그림자를 형성합니다.

---

## 4. 실사(Photorealistic) 품질 극대화 파이프라인

실사 프로젝트에서 사실감을 극대화하기 위해 적용해야 하는 핵심 렌더러 파이프라인 파라미터입니다.

### 4.1 힛 라이팅(Hit Lighting) 및 서피스 캐시 해상도
- **Hit Lighting 모드 활성화 (`r.Lumen.HardwareRayTracing.LightingMode 1`)**:
  - 기본 서피스 캐시 모드는 레이가 물체에 닿았을 때 미리 저해상도로 구워둔 카드 텍스처(Surface Cache)의 색상을 가져옵니다.
  - 힛 라이팅 모드로 변경하면 레이가 표면에 닿는 즉시 해당 지점의 실제 풀 머티리얼 셰이더를 직접 평가합니다. 거울 반사, 복잡한 범프 맵, 메탈릭 표면에서 실사와 동일한 선명도를 얻을 수 있습니다.
- **서피스 캐시 해상도 스케일 (`r.LumenScene.SurfaceCache.CardResolutionScale 2.0`)**: 서피스 캐시가 필요한 원거리 및 보조 연산의 해상도를 2배로 확장하여 블랙 블록 현상을 방지합니다.

### 4.2 다중 간접 바운스 및 반투명 리플렉션
- `r.Lumen.DiffuseIndirect.MaxBounces 2 ~ 3`: 빛이 붉은 벽에 튕긴 뒤 바닥을 비추고, 다시 천장을 비추는 컬러 블리딩(Color Bleeding)을 2차 이상까지 계산하여 밀폐된 공간의 암부를 풍성하게 채웁니다.
- `r.Lumen.Reflections.HardwareRayTracing.Translucent 1`: 유리창, 물웅덩이, 얼음 등 반투명(Translucent) 표면에도 래스터라이즈드 큐브맵 대신 실시간 루멘 하드웨어 레이 트레이싱 반사를 적용합니다.
- `r.Lumen.Reflections.MaxRoughnessToTrace 0.8`: 거친(Rough) 표면에서도 스펙큘러 레이를 넓게 분산 추적하여 금속 및 거친 석재의 물리적 사실감을 극대화합니다.

---

## 5. 비실사(Stylized / NPR) 품질 유지 및 오염 방지 파이프라인

루멘은 본래 실사 물리 기반 렌더링(PBR)을 위해 고안되었으므로, 이를 카툰/셀 셰이딩 프로젝트에 그대로 적용하면 심각한 아티팩트가 발생합니다.

### 5.1 비실사 렌더링에서의 루멘 충돌 문제
1. **얼굴 앰비언트 오클루전(AO) 때 현상**: 루멘의 세밀한 차폐 연산이 카툰 캐릭터의 코 밑, 눈가, 목 주위에 거뭇거뭇한 '때(Dirty spot)'를 만듭니다.
2. **부드러운 그라데이션과 셀 셰이딩의 충돌**: 카툰 렌더링의 핵심인 명확한 2단/3단 명암 경계선(Band)이 루멘의 부드러운 전역 조명으로 인해 뭉개져 실사풍의 애매한 음영이 됩니다.

### 5.2 해결 패턴: 머티리얼 레벨 간접광 양자화 및 마스킹
- **간접광 양자화 (Indirect Light Quantization)**: 루멘이 계산한 간접광의 휘도(Luminance)를 머티리얼 내에서 하프-램버트(Half-Lambert)로 재매핑한 후, 계단식 램프 텍스처(Ramp Texture)로 양자화하여 음영 단계를 강제 유지합니다.
- **얼굴 영역 AO 하한선 클램핑 (Face Mask Clamping)**: 캐릭터 머티리얼의 버텍스 컬러나 텍스처 마스크를 통해 얼굴 영역의 간접광 수신 하한선을 강제로 보정하여 그림자 오염을 차단합니다.
- **Custom Stencil 마스킹 분리**: 배경에는 풀 루멘 GI를 적용하고, 캐릭터는 `Custom Stencil` 버퍼로 마스킹하여 포스트 프로세스에서 캐릭터 전용 조명 보정을 가합니다.

```hlsl
// File: Shaders/ToonLumenQuantizer.ush
// 비실사(NPR) 캐릭터 머티리얼용 루멘 간접광 양자화 및 얼굴 얼룩 차단 셰이더

float Luminance = dot(IndirectIrradiance, float3(0.299, 0.587, 0.114));
float RemappedLuminance = saturate(Luminance * 0.5 + 0.5);

// 램프 텍스처 기반 단계적 셀 음영 매핑
float3 QuantizedColor = RampTexture.SampleLevel(RampSampler, float2(RemappedLuminance, 0.5), 0).rgb;

// 얼굴 부위(FaceMask) 루멘 AO 얼룩 원천 차단
if (FaceMask > 0.5)
{
    QuantizedColor = max(QuantizedColor, float3(0.75, 0.75, 0.75));
}

return QuantizedColor * IndirectIrradiance;
```

---

## 6. 실전 엔진 설정(INI / CVar) 종합 레퍼런스

프로젝트 루트의 `Config/DefaultEngine.ini`에 아래 설정을 적용하여 나나이트 비활성화 환경에서도 최고 수준의 렌더링 품질을 달성합니다.

```ini
; File: Config/DefaultEngine.ini
[/Script/Engine.RendererSettings]
; ====================================================
; 1. 루멘 기본 활성화 및 나나이트 미사용 대응 (HWRT 필수)
; ====================================================
r.DynamicGlobalIlluminationMethod=1
r.ReflectionMethod=1
r.Lumen.HardwareRayTracing=1
; 0: Surface Cache, 1: Hit Lighting (실제 머티리얼 평가, 반사 디테일 극대화)
r.Lumen.HardwareRayTracing.LightingMode=1
; 서피스 캐시 해상도 스케일 (기본 1.0 -> 2.0으로 상향하여 블록 현상 제거)
r.LumenScene.SurfaceCache.CardResolutionScale=2.0

; ====================================================
; 2. UE 5.8 라이트 프로브 (Irradiance Field Gather)
; ====================================================
; 0: Irradiance Field Gather (3D 프로브 기반 - 노이즈 억제 및 안정적 간접광)
; 1: Screen Probe Gather (픽셀 적응형 스크린 프로브)
r.Lumen.FinalGatherMethod=0
r.Lumen.IrradianceFieldGather.NumClipmaps=4
r.Lumen.IrradianceFieldGather.GridResolution=64
r.Lumen.IrradianceFieldGather.ProbeResolution=16
r.Lumen.IrradianceFieldGather.IrradianceProbeResolution=6
r.Lumen.IrradianceFieldGather.OcclusionProbeResolution=16

; ====================================================
; 3. UE 5.5+ MegaLights (확률적 레이 트레이싱 다이내믹 라이팅)
; ====================================================
r.MegaLights.EnableForProject=1
r.MegaLights.NumSamplesPerPixel=2
r.MegaLights.LightingChannels=1
r.MegaLights.DirectionalLights=1

; ====================================================
; 4. 실사 그래픽 디테일 튜닝
; ====================================================
; 간접광 최대 바운스 횟수 (다중 반사 컬러 블리딩 표현)
r.Lumen.DiffuseIndirect.MaxBounces=2
; 반사 계산을 수행할 최대 표면 거칠기 (거친 금속/석재의 사실적 스펙큘러)
r.Lumen.Reflections.MaxRoughnessToTrace=0.8
; 반투명 표면(유리, 물) 하드웨어 레이 트레이싱 반사 활성화
r.Lumen.Reflections.HardwareRayTracing.Translucent=1
; 그림자 품질을 위한 버추얼 섀도우 맵 해상도 유지
r.Shadow.Virtual.MaxPhysicalPages=4096
```

---

## 7. 아키텍처 절충점 요약 (Trade-offs)

| 항목 | 선택 옵션 | 품질 이점 | 비용 및 주의사항 |
| :--- | :--- | :--- | :--- |
| **지오메트리 추적** | `HWRT 1` (하드웨어 레이 트레이싱) | 나나이트 없이도 완벽한 차폐, 빛샘 0%, 정밀한 반사 | DXR/비디오 메모리(VRAM) 사용량 증가, BVH 빌드 비용 |
| **개더링 방식** | `FinalGatherMethod 0` (라이트 프로브) | 화면 시간축 노이즈/플리커링 제거, 부드럽고 균일한 간접광 | 미세한 접촉 차폐(Contact AO) 선명도가 스크린 프로브 대비 소폭 뭉개질 수 있음 |
| **다이렉트 라이팅** | `MegaLights 1` | 수백 개 광원에 소프트 에어리어 섀도우 구현, VSM 병목 제거 | GPU 컴퓨트 셰이더 및 디노이저 연산 부하 증가 |
| **라이팅 평가** | `LightingMode 1` (Hit Lighting) | 서피스 캐시 블록 현상 원천 차단, 실사급 리플렉션 | 복잡한 머티리얼이 많은 씬에서 레이 트레이싱 셰이딩 비용 증가 |
| **스타일라이즈드** | `Toon Ramp + Face Clamping` | 루멘 실시간 조명 색조를 받으면서 깔끔한 카툰 셰이딩 유지 | 캐릭터 머티리얼별 램프 텍스처 및 마스크 설정 필요 |

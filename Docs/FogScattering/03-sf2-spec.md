# SF-2 명세 — TDFogScattering 전체 기능

갱신: 2026-10-23 claude · 상태: v1 · 상위 D-62 · 선행 `02-sf1-spec.md`(SF-1 done)

범위(사용자 지시): 품질 프리셋·Format·하늘 대기·볼류메트릭 포그·HV·구름·스테레오·블루프린트 함수. 정본 `Plugins/TDFogScattering`, 작업 사본 `Saved/AgentOps/20261023/ssfs/staging2/Plugins/TDFogScattering`. 규칙은 SF-1 명세 2·9절과 같다(TD 이름, 새 코드 주석 없음, 조기 반환, 비내보내기 함수 호출 금지, 공용 트리·장부·커밋 금지).

## 1. 콘솔 변수 (SSFS 대응, 기본값)

| 이름 | 기본 | 의미 | SSFS |
|---|---|---|---|
| `r.TDFogScattering` | 1 | 켜기 | `r.SSFS` |
| `.QualityPreset` | 2 | 0~3 → 다운/업 탭 4/5·8/5·8/9·13/9 | `.QualityPreset` |
| `.Format` | -1 | 피라미드 형식: -1 SceneColor 형식 상속, 0 `PF_FloatR11G11B10`, 1 `PF_FloatRGBA`(UAV 저장 불가면 `PF_FloatRGBA`) | `.Format`(1.32 상속 기본) |
| `.LuminanceWeighting` | 1 | 첫 다운샘플 Karis | 같음 |
| `.PassAmount` | 8 | 1~12 | 같음 |
| `.Radius` | 0.9 | 0~1 | 같음 |
| `.Intensity` | 1.0 | 전체 강도 | 같음 |
| `.SkyAtmosphere` / `.SkyAtmosphereIntensity` | 0 / 1.0 | 공중 원근 투과율 포함 | 같음 |
| `.VolumetricCloud` / `.VolumetricCloudIntensity` | 0 / 1.0 | 구름 투과율 포함(실험) | 같음 |
| `.VolumetricFogILSIntensity` | 1.0 | 볼류메트릭 포그 통합 광산란 투과율 강도(0이면 제외) | 같음 |
| `.HeterogeneousVolumes` | 1 | HV 포함(5절) | 같음 |
| `.DistanceScale` | 1.0 | 높이 안개 광학두께 배율(탑다운 보정, SSFS 없음) | — |
| `.DebugView` | 0 | 1 가중치, 2 깊이÷100m, 3 장치 깊이×100, 4 매체 불투명도(1−매체 투과율) | — |

매체별 강도 적용: `T_i' = 1 − saturate(Intensity_i × (1 − T_i))`, 매체 투과율 = Π T_i'. 최종 가중치 `w = saturate(Intensity × (1 − T_height × T_media))`.

## 2. 인터페이스 계약 (두 빌더가 이 이름을 그대로 쓴다)

**핵심 빌더(A) 소유**: `TDFogScatteringConsoleVariables.h/.cpp`, `TDFogScatteringPasses.h/.cpp`, `TDFogScatteringViewExtension.*`, `TDFogScatteringSubsystem.*`(Public 포함), `Shaders/Private/TDFogScattering.usf`, `Shaders/Private/TDFogScatteringFog.ush`.
**매체 빌더(B) 소유(새 파일)**: `Private/TDFogScatteringMedia.h/.cpp`, `Shaders/Private/TDFogScatteringMedia.ush`. Renderer 비공개 헤더는 `TDFogScatteringFogParameters.cpp`와 `TDFogScatteringMedia.cpp`에서만 포함.

`FTDFogScatteringSettings`(A가 추가할 필드 이름 고정):
```cpp
int32 QualityPreset; int32 Format;
bool bShouldIncludeSkyAtmosphere; float SkyAtmosphereIntensity;
bool bShouldIncludeVolumetricCloud; float VolumetricCloudIntensity;
float VolumetricFogIntensity; bool bShouldIncludeHeterogeneousVolumes;
```
`TDFogScatteringMedia.h`(B):
```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FTDFogScatteringMediaParameters, ) ... END_SHADER_PARAMETER_STRUCT()
void SetupTDFogScatteringMediaParameters(FRDGBuilder& GraphBuilder, const FSceneView& View, const FTDFogScatteringSettings& Settings, FTDFogScatteringMediaParameters& OutParameters);
```
- 매체가 꺼졌거나 자원이 없으면 엔진 더미 텍스처(`GSystemTextures` 검정·볼륨 더미)와 꺼짐 플래그로 채운다(RDG 미바인딩 금지). 항상 호출 가능해야 한다.
`TDFogScatteringMedia.ush`(B, 매개변수 구조체 멤버와 같은 이름의 HLSL 전역 선언 포함):
```hlsl
float GetTDMediaHeightFogExcludeDistance(float3 CameraToReceiver);
float GetTDMediaTransmittance(uint2 ViewPixelPosition, float2 ScreenPosition, float DeviceZ, float SceneDepth, float3 CameraToReceiver);
```
- `ScreenPosition`은 `SvPositionToScreenPosition(...).xy`(NDC), `ViewPixelPosition`은 `View.ViewRectMin + 스레드`. 꺼진 매체는 투과율 1, 볼류메트릭 포그가 없으면 제외 거리 0.

A가 넣을 연결 지점: 합성 `FParameters`에 `SHADER_PARAMETER_STRUCT_INCLUDE(FTDFogScatteringMediaParameters, Media)`, `AddCompositePass`에서 `SetupTDFogScatteringMediaParameters(..., PassParameters->Media)`, 합성 셰이더에서 `#include "/Plugin/TDFogScattering/Private/TDFogScatteringMedia.ush"` 후 `ComputeTDFogTransmittance(..., ExcludeDistance)`의 제외 거리 = max(시작 거리, `GetTDMediaHeightFogExcludeDistance`), `T_total = T_height × GetTDMediaTransmittance(...)`, `DebugView 4`.

## 3. 품질 프리셋 (A)

탭 위치는 원본 텍셀 단위, 출력 텍셀 중심 기준. "탭" = 그 위치의 2×2 텍셀 평균(정수 모서리 쌍선형과 같음, 기존 `SampleInputBox`).

| 프리셋 | 다운샘플 | 업샘플(업샘플·합성 공통) |
|---|---|---|
| 0 | 4탭 (±1,±1) 각 0.25 | 5탭 십자: 중심 0.5, (±1,0)(0,±1) 각 0.125 |
| 1 | 8탭: (±1,±1) 각 0.125 + (±2,0)(0,±2) 각 0.125 | 5탭 |
| 2(기본) | 8탭 | 9탭 텐트(현재) |
| 3 | 13탭(현재 Jimenez) | 9탭 |

- Karis(LuminanceWeighting, 레벨 1만): 4·8탭은 탭마다, 13탭은 기존 상자 그룹마다 `1/(1+Luma)` 가중 후 정규화.
- 퍼뮤테이션: 다운샘플 `SHADER_PERMUTATION_SPARSE_INT("DOWNSAMPLE_TAP_COUNT", 4, 8, 13)` × `LUMINANCE_WEIGHTING`, 업샘플·합성 `SHADER_PERMUTATION_SPARSE_INT("UPSAMPLE_TAP_COUNT", 5, 9)`.

## 4. Format (A)

피라미드(다운·업샘플 중간) 텍스처 형식만 바꾼다. 출력은 지금처럼 SceneColor 설명(UAV 저장 불가면 `PF_FloatRGBA`). `-1`: SceneColor 형식이 `TypedUAVStore` 가능하면 그것, 아니면 `PF_FloatRGBA`. `0`: `PF_FloatR11G11B10`(UAV 저장 불가면 `PF_FloatRGBA`). `1`: `PF_FloatRGBA`.

## 5. 매체 (B)

매개변수 구조체 멤버 이름은 B가 정한다(A는 구조체를 통째로 포함만 한다). 각 매체 강도가 0이면 셰이더는 그 매체를 건너뛴다(더미 텍스처의 값에 의존 금지). 매체 투과율 `T_i' = 1 − saturate(I_i × (1 − T_i))`, 반환 = Π T_i'. 공통 입력: `CameraToReceiver`(cm, 카메라→수신점), `SceneDepth`(시선 깊이 cm), `ViewPixelPosition`(버퍼 절대 화소), `ScreenPosition`(NDC).

| 매체 | C++ 자원(`TDFogScatteringMedia.cpp`) | 셰이더 식 | 근거(UE 5.8) |
|---|---|---|---|
| 하늘 대기 공중 원근 | View 유니폼(이미 합성 패스에 바인딩)만. 강도 = `bShouldIncludeSkyAtmosphere ? SkyAtmosphereIntensity : 0` | `View.SkyAtmosphereApplyCameraAerialPerspectiveVolume > 0`이고 `IsSkyAtmosphereRenderedInMain(View.EnvironmentComponentsFlags)`이고 장치 깊이 ≠ 0일 때만. km = `length(CameraToReceiver) × 1e-5`, `tDepth = max(0, km − StartDepthKm)`, `NonLinW = saturate(sqrt(tDepth × SliceLengthKmInv × DepthResolutionInv))`, `NonLinSlice = NonLinW × DepthResolution`, `Weight = (NonLinSlice < 0.7071 ? saturate(NonLinSlice² × 2) : 1) × saturate(tDepth × 1e5)`, UV = `ScreenPosition.xy × (0.5, −0.5) + 0.5`, `T = 1 − Weight × (1 − CameraAerialPerspectiveVolume.SampleLevel(..., float3(UV, NonLinW), 0).a)` | `SkyAtmosphereCommon.ush:42-118`, View 멤버 `SceneView.h:1021-1031, 1220-1221`, 엔진 합성 `HeightFogPixelShader.usf:156-174` |
| 볼류메트릭 포그 | `FViewInfo::VolumetricFogResources.IntegratedLightScatteringTexture`(RDG 임시, 매 프레임 바인딩, 캐시 금지). null 또는 `VolumetricFogIntensity ≤ 0`이면 RDG 시스템 볼륨 더미 + 적용 0. 시작 거리 `FViewInfo::VolumetricFogStartDistance` | 적용 > 0일 때 `ZSlice = log2(SceneDepth × GridZParams.x + GridZParams.y) × GridZParams.z × InvGridSize.z`, UV = `min((ViewPixelPosition + 0.5 − View.ViewRectMin.xy) × View.VolumetricFogSVPosToVolumeUV, View.VolumetricFogUVMax)`, `T = lerp(1, Tex.SampleLevel(쌍선형 clamp, float3(UV, ZSlice), 0).a, saturate((SceneDepth − StartDistance) × 1e8))`. 제외 거리(`GetTDMediaHeightFogExcludeDistance`) = 이번 프레임에 볼류메트릭 포그가 그려졌으면(텍스처 존재, 강도와 무관 — 반박 SF2-1 정정) `View.VolumetricFogMaxDistance × (cos > 0.001 ? 1/cos : 0)`, cos = `dot(normalize(CameraToReceiver), View.ViewForward)`, 아니면 0 | `SceneRendering.h:853-867, 1583`, `VolumetricFog.cpp:2002, 2032`, `FogRendering.cpp:188-201`, `HeightFogPixelShader.usf:145-152`, `HeightFogCommon.ush:427-455, 535-585` |
| 볼류메트릭 구름(실험) | `bShouldIncludeVolumetricCloud`이고 `r.VolumetricRenderTarget.Mode == 0`(렌더 스레드에서 읽음, 아니면 끄고 한 번 경고)이고 `View.State != nullptr`일 때 `View.State->GetVolumetricCloudTexture(GraphBuilder)`·`GetVolumetricCloudTextureUVScale()`·`UVMax()`(공개 순수 가상). null이면 끔 | UV = `min((ViewPixelPosition + 0.5 − View.ViewRectMin.xy) × View.ViewSizeAndInvSize.zw × UVScale, UVMax)`, `T = Tex.SampleLevel(쌍선형 clamp, UV, 0).a` | `SceneManagement.h:179-181`, `SceneViewState.h:441-454`, `VolumetricRenderTarget.cpp:121-152, 463-554, 1058` |
| HV | `bShouldIncludeHeterogeneousVolumes`이고 `FViewInfo::HeterogeneousVolumeRadiance != nullptr`일 때 바인딩(텍스처 크기 함께), 아니면 끔 | 좌표 규칙은 엔진 합성 셰이더(`HeterogeneousVolumesComposite.usf:55-83`, 다운샘플 배율)와 같게, `T = Load(...).a` | `SceneRendering.h:1589-1591`, `HeterogeneousVolumes.cpp:1390-1392, 1643, 1679, 1702-1705` |

- 비공개 헤더는 `TDFogScatteringMedia.cpp`에서만, `FViewInfo` 데이터 멤버 읽기와 공개(가상·`*_API`·인라인) 함수만. RDG 시스템 텍스처 접근 함수의 내보내기 여부를 확인하고 쓴다.
- 반사 캡처·직교 투영에서는 구름이 null이다(검사 필수). 구름은 깊이 업샘플 없이 쌍선형이라 경계 번짐이 있는 근사다(기록만).

## 6. 블루프린트 (A)

`UTDFogScatteringSubsystem`(UEngineSubsystem, 블루프린트 노드 `Get Engine Subsystem`)에 `UFUNCTION(BlueprintCallable/BlueprintPure, Category = "TD|Fog Scattering")`:
`SetFogScatteringEnabled(bool)`, `IsFogScatteringEnabled()`, `SetFogScatteringIntensity(float)`, `GetFogScatteringIntensity()`, `SetFogScatteringQualityPreset(int32)`, `GetFogScatteringQualityPreset()`, `SetFogScatteringDistanceScale(float)`, `GetFogScatteringDistanceScale()`. 값의 정본은 콘솔 변수 하나(`ECVF_SetByConsole`로 설정해 콘솔과 나중에 쓴 쪽이 이김 — `SetByCode`는 콘솔 설정 뒤 무시되어 정정, 범위는 콘솔 변수 읽기와 같은 규칙으로 묶음). 게임 스레드 전용.

## 7. 스테레오·분할 화면 (A·B 공통 규칙)

- BeforeDOF 콜백은 뷰마다(스테레오 보조 뷰 포함) 불리고 뷰마다 View 유니폼이 따로 있다(`DeferredShadingRenderer.cpp:4262-4328`, `PostProcessing.cpp:385-409, 889-898`). 뷰 상태는 콜백 인자에서만 읽고 전역·정적 캐시에 두지 않는다. 모든 화소 좌표는 `ViewRect.Min`(셰이더 `View.ViewRectMin`) 오프셋을 반영한다(스테레오 오른쪽 눈·분할 화면 두 번째 뷰는 Min ≠ 0).
- 입력이 배열 슬라이스면 `CopyFromSlice`가 복사한다(`ScreenPass.cpp:62-92`, 2D면 복사 없음). 모바일 멀티뷰는 범위 밖.
- 시험: 분할 화면은 PIE에서 `DebugCreatePlayer 1`(바인딩 미확인, 안 되면 PIE 플레이어 수 2). 스테레오 흉내는 `-emulatestereo`·`r.EnableStereoEmulation`이 읽기 전용이라 재시작이 필요하고, 에디터 `-game`은 이 환경에서 즉시 종료되는 기록이 있어(`Tools/README.md` measure_game_frames 행) 정적 검토로 둔다.

## 8. 검증

1. 오프라인 셰이더 검사 `check_fog_scattering_shaders.py --plugin-dir <사본>`: 모든 퍼뮤테이션 SM5·SM6 통과.
2. `BuildPlugin` 종료 0(에디터·게임).
3. 통합 뒤 PIE(`LV_DarkFantasy_OpenWorld`, 측정 장치 v3 방식): 프리셋 0~3 GPU p50 단조 증가·화면 차이, Format 0/1/-1 화면 동등(SSIM 수준 차이 작음), 하늘 대기 켬/끔·볼류메트릭 포그 강도 0/1·구름(`LV-World` 하늘) 켬/끔 각각 `DebugView 4`로 매체 불투명도 확인, 분할 화면 경계 번짐 없음, 블루프린트 함수 호출 → 콘솔 변수 값 일치.
4. 정적 반박 치명 0.

## 9. 결과 (2026-10-23)

| 항목 | 결과 |
|---|---|
| 빌드 | 빌더 2개(핵심·매체, 파일 분리 계약) 합본이 `BuildPlugin` 첫 시도 통과(에디터·게임, 경고 0), 오프라인 셰이더 20/20, 공용 `TDGameEditor` 빌드·에디터 시작 시 퍼뮤테이션 10개 컴파일 오류 없음 |
| 기본 효과 | `LV_DarkFantasy_OpenWorld` 3인칭 켬/끔 띠별 선명도 손실 위 0.326·중간 0.103·아래 0.017, 휘도비 0.997(반복 짝 잡음 ≤ 0.008). SF-1(0.198)보다 강한 것은 볼류메트릭 포그 매체 기여 때문 |
| 품질 프리셋 | GPU p50 0.31·0.33·0.41·0.45ms(프리셋 0~3, RTX 2060, 에디터 PIE) 단조 증가, 화면 차이는 손실 ±0.007 수준 |
| Format | R11G11B10 휘도비 0.996(정밀도 차이), RGBA float = 상속(이 프로젝트 SceneColor RGBA16F) |
| 볼류메트릭 포그 | 강도 0→1에서 위 띠 손실 +0.163, 매체 진단에 거리 따라 증가하는 불투명도 |
| 하늘 대기 | 켬 차이 0.008(이 맵 규모 수백 m에서는 공중 원근 기여가 작음), 진단 화면 정상 |
| 구름 | `LV-World` 켬/끔 하늘 띠 손실 0.124·휘도비 0.996, 진단에 구름 모양·캐릭터 가림 반영, 끔이면 0 |
| HV | 볼류메트릭 포그 강도 0 진단에 지평선 빛 덩어리만 남음(HV 렌더러 효과로 추정, 시각 확인만) |
| 분할 화면 | `DebugCreatePlayer 1`로 위·아래 두 뷰, 뷰별 가중치 독립·경계 번짐 없음 |
| 블루프린트 | 엔진 서브시스템 함수 8개 호출 6검사 OK, 콘솔→블루프린트→콘솔→블루프린트 순서 모두 반영 |
| 정적 반박 | 치명 0·중요 2(볼류메트릭 포그 제외 조건, 구름 잔상) 수정, 경미 3건 수정(HV Upsample 묶기, 설정자 우선순위, 퍼뮤테이션 차원 이름 TD) |

- 한계(기록): 스테레오는 정적 검토만(흉내 장치는 재시작 필요, 에디터 `-game` 즉시 종료 기록), 구름은 깊이 업샘플 없는 쌍선형 근사, HV는 반투명 합성(AVSM) 모드의 투과율 미반영, 공중 원근 직교 투영 보정 없음, 높이 안개가 없는 장면은 다른 매체가 있어도 효과 없음(SSFS와 같은 전제), 볼류메트릭 포그 제외는 뷰 단위 판정(엔진은 패밀리 단위), 구름 모드 경고 로그 생략(전역 상태 규칙).
- 측정 시점 주의: 볼류메트릭 포그 강도 0 화면과 프리셋·Format·기본 효과 측정은 반박 수정 전 빌드였다. 수정은 강도 0일 때 제외 거리와 구름 잔상 조건만 바꾸므로 다른 수치는 그대로 유효하다.
- 재반박(수정 뒤): 합격(치명 0·중요 0). 경미 A(대기 표시 플래그·`r.VolumetricCloud` 꺼짐 때 구름 잔상)도 조건 추가로 막고 공용 `TDGameEditor` 빌드 통과(PIE 재측정 없음, 구름 끔 경우에만 영향).
- 근거 `measurements/SF-2/`, 원본 `Saved/FluidFX/lookdev/20261023-161322/`·`20261023-162258/`. 미커밋.

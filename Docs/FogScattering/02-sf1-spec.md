# SF-1 명세 — TDFogScattering 프로토(높이 안개) · 측정 장치 · 비공개 헤더 탐침

갱신: 2026-10-23 claude · 상태: v1 · 상위 결정 D-62 · 분석 `01-ssfs-fsss-feasibility.md`

순서(사용자 지시): 명세 → 측정 장치 → 비공개 헤더 컴파일 탐침 → 높이 안개 프로토. 측정 장치와 탐침은 서로 의존하지 않으므로 동시에 만들고, 프로토 통합은 둘 다 통과한 뒤에만 한다.

## 1. 범위

- 포함: 런타임 플러그인 `TDFogScattering`(UE 5.8, Win64), DOF 이전 주입, 높이 안개(두 층) 투과율로 가중한 밉 피라미드 블러 합성, 콘솔 변수 6개, GPU 통계, 측정 도구 4개 + 촬영 계획.
- 제외(SF-2 이후): 품질 프리셋 탭 수 변형, Format 변수, 볼류메트릭 포그·하늘 대기·구름·HV 기여, 인스턴스드 스테레오, 블루프린트 함수, 이계별 탑다운 값(SF-4).
- 공용 트리 원칙: 플러그인은 사본 `Saved/AgentOps/20261023/ssfs/staging/Plugins/TDFogScattering/`에서만 작성·빌드한다. 공용 `Plugins/`로의 복사는 부모가 빌드 창 보류 뒤에 한다(전역 셰이더 오류는 모든 세션의 에디터 시작을 깨므로).

## 2. 파일 계약 (사본 루트 기준)

| 파일 | 역할 |
|---|---|
| `TDFogScattering.uplugin` | FileVersion 3, FriendlyName "TD Fog Scattering", Category "Rendering", CanContainContent false, EnabledByDefault true, Modules: `TDFogScattering` Runtime `PostConfigInit` |
| `Source/TDFogScattering/TDFogScattering.Build.cs` | Public: Core, CoreUObject, Engine / Private: RenderCore, Renderer, RHI, Projects / PrivateIncludePaths: `Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private")`, `.../Renderer/Internal` |
| `Public/TDFogScatteringSubsystem.h` + `Private/.cpp` | `UTDFogScatteringSubsystem : UEngineSubsystem` — Initialize에서 `FSceneViewExtensions::NewExtension<FTDFogScatteringViewExtension>()` 보관, Deinitialize에서 해제. SF-1에는 블루프린트 함수 없음 |
| `Private/TDFogScatteringModule.cpp` | `FTDFogScatteringModule`: StartupModule에서 `AddShaderSourceDirectoryMapping(TEXT("/Plugin/TDFogScattering"), <플러그인>/Shaders)` |
| `Private/TDFogScatteringConsoleVariables.h/.cpp` | 3절 콘솔 변수 정의와 렌더 스레드 값 읽기 함수 |
| `Private/TDFogScatteringFogParameters.h/.cpp` | **Renderer 비공개 헤더를 포함하는 유일한 파일.** `bool TryGetTDFogScatteringFogParameters(const FSceneView& View, FTDFogScatteringFogParameters& OutParameters)` |
| `Private/TDFogScatteringViewExtension.h/.cpp` | `FTDFogScatteringViewExtension : FSceneViewExtensionBase` |
| `Private/TDFogScatteringPasses.h/.cpp` | 전역 셰이더 3종(컴퓨트)과 RDG 패스 구성 `FScreenPassTexture AddTDFogScatteringPasses(...)` |
| `Shaders/Private/TDFogScattering.usf` | 진입점 `MainDownsampleCS`, `MainUpsampleCS`, `MainCompositeCS` |
| `Shaders/Private/TDFogScatteringFog.ush` | 안개 투과율 함수(5절) |

이름 규칙: 모든 타입에 `TD`(AGENTS 3절). 새 코드에 주석 없음(5절). 로직은 C++·HLSL만(7절). 에셋·동기 로드 없음.

## 3. 콘솔 변수 (ECVF_RenderThreadSafe | ECVF_Scalability)

| 이름 | 기본 | 범위 | SSFS 대응 |
|---|---|---|---|
| `r.TDFogScattering` | 1 | 0/1 | `r.SSFS` |
| `r.TDFogScattering.PassAmount` | 8 | 1~12 | `r.SSFS.PassAmount` |
| `r.TDFogScattering.Radius` | 0.9 | 0~1 | `r.SSFS.Radius` |
| `r.TDFogScattering.Intensity` | 1.0 | ≥0 | `r.SSFS.Intensity` |
| `r.TDFogScattering.LuminanceWeighting` | 1 | 0/1 | `r.SSFS.LuminanceWeighting` |
| `r.TDFogScattering.DistanceScale` | 1.0 | ≥0 | 없음(SF-4 탑다운 보정 선행, 1이면 SSFS와 같음) |

## 4. 렌더 경로

1. `IsActiveThisFrame_Internal`: `r.TDFogScattering != 0`(게임 스레드 값).
2. `SubscribeToPostProcessingPass(PassId, View, Callbacks, bIsPassEnabled)`: `PassId == EPostProcessingPass::BeforeDOF`이고 아래 건너뛰기 조건이 아닐 때만 콜백 추가. 건너뛰기: 변수 0, Intensity ≤ 0, `View.Family->EngineShowFlags.PathTracing`, `!EngineShowFlags.Fog`, `View.bIsReflectionCapture`, `Scene == nullptr || !Scene->HasAnyExponentialHeightFog()`.
3. 콜백 `FScreenPassTexture (FRDGBuilder&, const FSceneView&, const FPostProcessMaterialInputs&)`:
   - `TryGetTDFogScatteringFogParameters` 실패 또는 뷰 사각형 최소 변 < 4 → `Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder)`.
   - SceneColor는 `FScreenPassTextureSlice` → `FScreenPassTexture`로 변환해 쓴다. 깊이는 `Inputs.SceneTextures`.
   - 이벤트·GPU 통계: `DECLARE_GPU_STAT_NAMED(TDFogScattering, TEXT("TDFogScattering"))`, 5.8 엔진 사용례(`DeferredShadingRenderer.cpp`의 `RDG_EVENT_SCOPE_STAT`·`RDG_GPU_STAT_SCOPE`)와 같은 형태. CSV 열 `GPU/TDFogScattering`이 나와야 한다.
4. 피라미드(가설 1):
   - 레벨 0 = SceneColor 뷰 사각형. 레벨 k 크기 = ceil(레벨 k−1 / 2). 레벨 수 N = clamp(PassAmount, 1, 12)에서 레벨 N의 최소 변이 2보다 작아지면 줄인다.
   - 다운샘플 D_k(k=1..N): 13탭(Jimenez 2014: 중앙 2×2 상자 4탭 가중 0.5, 모서리 상자 4개 각 0.125, 쌍선형). k=1이고 LuminanceWeighting이면 다섯 상자 평균에 각각 `1/(1+Luma)` 가중 후 가중합 정규화(Karis). 모든 입력은 NaN·Inf → 0, 상한 65000.
   - 업샘플: U_N = D_N, k = N−1..1에서 `U_k = lerp(D_k, Tent3x3(U_{k+1}), Radius)`(텐트 1·2·1 / 2·4·2 / 1·2·1 ÷16, 원본 해상도 1텍셀 간격).
   - 합성(레벨 0 해상도): `Blur = Tent3x3(U_1)`을 화소 UV에서 표본, `T = 5절 투과율`, `w = saturate(Intensity × (1 − T))`, `Out.rgb = lerp(SceneColor.rgb, Blur, w)`, `Out.a = SceneColor.a`.
   - 표본 경계: 레벨 0은 `SceneColor.ViewRect` 안(반 텍셀 안쪽)으로 UV를 묶어 분할 화면 번짐이 없게 한다. 레벨 1 이상은 각 텍스처가 원점 기준 정확한 크기.
   - 형식: 중간 텍스처 `PF_FloatRGBA`. 출력은 SceneColor 설명에 UAV를 더한 새 텍스처(형식이 UAV 불가면 `PF_FloatRGBA`). `Inputs.OverrideOutput`이 유효하면 결과를 그쪽으로 복사(`AddDrawTexturePass` 등)하고 그것을 반환.
5. 셰이더는 모두 `SF_Compute`, 스레드 그룹 8×8, `View` 유니폼 버퍼와 `/Engine/Private/Common.ush` 사용. 깊이→위치는 엔진 도우미(`SvPositionToTranslatedWorld` 등)로 번역 월드 좌표에서 계산하고 카메라 상대 벡터 = 번역 월드 위치 − `View.TranslatedWorldCameraOrigin`(대형 월드 안전).

## 5. 안개 투과율 (엔진 식 이식)

- 원본: `Engine/Shaders/Private/HeightFogCommon.ush` `GetExponentialHeightFog`(228행~)의 **알파(투과율) 경로만** 옮긴다. 색·방향성 산란·큐브맵·볼류메트릭 포그·전역 클립 평면·원점 재정의는 제외.
- 반드시 같은 것: 관측자 높이 상한(`ExponentialFogParameters.z`), 끝 거리(XY 투영, `FogEndDistance`), 시작 거리 제외(`ExponentialFogParameters.w`, `PERMUTATION_SUPPORT_FOG_START_DISTANCE` 경로), 두 층 선적분(`CalculateLineIntegralShared`, 밑 2, 테일러 근사 분기), 차단 거리(`ExponentialFogParameters3.w`), 최소 불투명도 하한(`MinFogOpacity = 1 − FogMaxOpacity`, 엔진의 `ExponentialFogColorParameter.w`와 같은 의미).
- 하늘(장치 깊이 0) 처리는 `HeightFogPixelShader.usf`(141·201-205행 부근)와 같게.
- 탑다운 보정: 선적분 합에 `DistanceScale`을 곱한 뒤 `exp2`를 취하고 그다음 최소 불투명도 하한을 적용한다. 즉 `T = max(saturate(exp2(−∫ × DistanceScale)), MinFogOpacity)`. 1이면 엔진과 같다.
- C++ 쪽 `FTDFogScatteringFogParameters`: `FVector4f ExponentialFogParameters, ExponentialFogParameters2, ExponentialFogParameters3; float FogMaxOpacity; float FogEndDistance;`. 값은 `static_cast<const FViewInfo&>(View)`의 같은 이름 멤버(`Renderer/Private/SceneRendering.h:1435-1441`, 계산 `FogRendering.cpp:357-457`)를 그대로 복사. `View.bIsViewInfo`가 거짓이면 실패 반환. 엔진 비내보내기 함수(`ShouldRenderFog` 등)는 호출하지 않는다(링크 오류).

## 6. 탐침 계약 (3단계)

- 사본에 2절의 uplugin·Build.cs·모듈·서브시스템·콘솔 변수·`TDFogScatteringFogParameters`만 먼저 두고(셰이더·패스 없음) `RunUAT BuildPlugin`으로 격리 빌드한다.
- 명령: `"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<사본>\TDFogScattering.uplugin" -Package="C:\Project\TDGame\Saved\AgentOps\20261023\ssfs\buildplugin-<label>" -TargetPlatforms=Win64 -Rocket` (공용 트리 바이너리를 건드리지 않음). 실행 전 다른 UBT·UAT·cl·link 프로세스가 없을 때까지 기다린다.
- 통과: 종료 0, 에디터·게임 대상 모두 컴파일·링크 성공, 경고 중 C4996 이외 새 경고 목록 보고.
- 실패 시 우회 (a): 같은 함수 시그니처를 유지하고, 게임 스레드에서 첫 `UExponentialHeightFogComponent` 값을 `SceneCore.cpp:389-445`·`FogRendering.cpp:357-457` 식으로 변환해 렌더 스레드에 넘긴다. 이 경우 Build.cs의 비공개 경로를 지운다.

## 7. 측정 장치 계약 (2단계)

| 도구 | 내용 | 자체 검증 |
|---|---|---|
| `Tools/FogScattering/check_fog_scattering_images.py` | 켬/끔 PNG 짝(또는 lookdev run 폴더와 짝 이름 목록) → JSON: 평균 휘도비(켬/끔), 변화 화소 비율(|Δ|>2/255), 수평 띠 3개(위·중간·아래)별 라플라시안 분산비(선명도 손실), 최대 차이 | `--self-test`: 합성 영상에 위쪽일수록 강한 가우시안 블렌드를 넣어 위 띠 손실 > 아래 띠 손실, 휘도비 ±1% 판정 |
| `Tools/FogScattering/check_fog_scattering_gpu.py` | lookdev run 동안 생긴 `Saved/Profiling/CSV/*.csv`를 시간순으로 켬/끔 변형에 짝지어 앞 60행 버린 뒤 `GPU/TDFogScattering`·`GPUTime`·`FrameTime` p50·p95, 켬−끔 GPUTime 차분 | 기존 CSV(`Saved/FluidFX/fx7p/**/G1_r1_1A.csv`의 `GPU/TDFluidGpuSolver`)로 열 읽기 시험 |
| `Tools/FogScattering/check_fog_scattering_shaders.py` | 플러그인 `Shaders/` .usf를 SM5·SM6 정의로 오프라인 dxc 컴파일(`IMPLEMENT_GLOBAL_SHADER(..., SF_Compute)`를 C++에서 읽음). `Tools/FluidFX/check_fluid_gpu_shaders.py`의 함수는 importlib로 재사용하고 그 파일은 고치지 않는다 | 고정 픽스처 셰이더 통과 + 일부러 깬 셰이더 실패 |
| `Tools/FogScattering/build_fog_scattering_plugin.py` | 6절 BuildPlugin 래퍼: 다른 UBT·UAT·cl·link·UnrealEditor 30초 부재 대기(`build_hold.json` 존중, `--holder`), 실행·시간 제한 40분, 오류 줄 요약 JSON | `--dry-run` 명령 출력 |
| `Tools/FogScattering/lookdev_fog_scattering_v1.json` | `pie_lookdev_fluid.py` 변형: warm → 탑다운 끔/켬 → 3인칭(arm 300, view [-10, 0]) 끔/켬 → FSSS 켬(props `enable_fsss`) → 복원, 노출 고정 `r.DefaultFeature.AutoExposure 0`, 마지막에 변수 원복 | JSON 로드 |

- 모든 새 스크립트는 첫머리 docstring 4줄(실행 환경·실행 명령·출력·상태, OP-15), `Tools/README.md` 표에 행 추가(OP-16), 다른 행은 수정하지 않는다.
- 촬영 함정: PIE가 켜져 있으면 `--level` 무시(먼저 `{"variants": []} --stop-pie`), 키보드 입력 금지, 에디터 앞창 유지(L-editor-06), HighResShot 4프레임(L-editor-46).

## 8. 수용 기준 (SF-1 완료)

1. 탐침: BuildPlugin 종료 0(또는 우회 (a)로 종료 0).
2. 프로토: BuildPlugin 종료 0, 셰이더 오프라인 검사 통과, 공용 트리 통합 뒤 `TDGameEditor` 빌드 성공, 에디터 시작 시 셰이더 오류 없음.
3. 화면(LV-World, 1280×720): 켬/끔 평균 휘도비 0.97~1.03, 3인칭 시점 위 띠 선명도 손실 > 아래 띠, `DistanceScale 8` 탑다운에서 변화 화소 비율이 `1`보다 큼.
4. GPU: `GPU/TDFogScattering` 열 존재, RTX 2060 렌더 해상도 기준 p50 ≤ 0.5ms(제안).
5. 정적 반박(td-refuter) 치명 지적 0.

## 9. 금지

- 공용 트리 `Source/`·`Plugins/`·`TDGame.uproject`·`Config/` 수정(통합은 부모), FluidFX 도구 수정, 커밋, 장부(NOW·Worklog·대장·Lessons) 쓰기.
- 블루프린트 로직, 동기 로드, 새 코드 주석, 엔진 비내보내기 함수 호출.

## 10. 결과 (2026-10-23, SF-1 done)

| 기준 | 결과 |
|---|---|
| 1 탐침 | `BuildPlugin` 종료 0(에디터·게임 Development·Shipping, 경고 0, 1분 43초). 우회 (a) 불필요 |
| 2 프로토 | 1차 컴파일 오류 3건(5.8 `Inputs.SceneTextures.SceneTextures`는 `TRDGUniformBufferBinding` → `GetUniformBuffer()->GetContents()`) 수정 뒤 `BuildPlugin` 0, 오프라인 셰이더 8/8, 공용 `TDGameEditor` 빌드 성공, 에디터 시작 시 전역 셰이더 3종 오류 없음 |
| 3 화면 | `LV_DarkFantasy_OpenWorld` 1280×720(AA 끔·시간 정지). 3인칭 켬/끔 휘도비 0.997, 띠별 선명도 손실 위 0.198·중간 0.035·아래 0.002(반복 짝 잡음 ≤ 0.004). 탑다운 기본 0.027·0.019·0.017(미미, 분석 예측과 일치), `DistanceScale 8` 0.196·0.125·0.110·휘도비 1.014 |
| 4 GPU | `GPU/TDFogScattering` p50 0.42ms(탑다운)·0.38ms(3인칭), p95 0.63·0.59(RTX 2060, 에디터 PIE, 렌더 해상도) |
| 5 정적 반박 | 치명 0·중요 2(반영: `IsFinite`, `IsExponentialFogRenderedInMain`)·경미 7(반영 5: 합성 입력 정리, 밀도 0 건너뛰기, 장면 캡처 하늘, 깊이·화면 좌표 `View.ViewRectMin` 기준, `FilterPlugin.ini` 제외. 볼류메트릭 포그 근거리 제외·출력 사각형 밖은 SF-2) |

- 추가: 진단 보기 `r.TDFogScattering.DebugView`(1 가중치, 2 깊이÷100m, 3 장치 깊이×100, 기본 0). 정본은 공용 `Plugins/TDFogScattering`(사본 staging은 진단 보기 이전 판).
- FSSS 기준선(`L_TDWorld_Main` F01, 아래 함정 때문에 참고용): 켜면 블러 대신 휘도 −5.7%(탑다운)·−3.4%(3인칭).
- 측정 함정: `L_TDWorld_Main` F01은 PIE에서 지형이 안 그려져 장치 깊이 0(L-editor-58) — 진단 보기로 판별. AA 끔 화면은 디더 머티리얼 점 무늬. 움직이는 요소가 있으면 변화 화소 비율 대신 띠별 선명도 손실로 판정.
- 근거 `measurements/SF-1/`(지표 JSON·GPU JSON·원경 확대 `third_horizon_off_top_on_bottom.png`·`world_main_f01_debug_views.png`), 원본 `Saved/FluidFX/lookdev/20261023-154117/`.
- 미완(SF-2): 품질 프리셋·Format·하늘 대기·ILS·HV·구름·스테레오·블루프린트 함수·볼류메트릭 포그 근거리 제외, 사용자 확인용 TSR 켠 화면. 미커밋.

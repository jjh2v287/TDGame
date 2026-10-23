# 안개 산란(SSFS 동등) 개발 가능성 분석 — 엔진 FSSS 비교 포함

갱신: 2026-10-23 claude · 상태: 분석 완료, 결정 대기(`Docs/Tasks/decisions.md` D-62)

요청(2026-10-23): Fab "Screen Space Fog Scattering"(SSFS)와 완전 동일하게 개발. 지금은 탑다운이지만 3인칭 전환·다른 프로젝트 재사용 가능성. 먼저 가능성 분석, 엔진 베타(실험) 기능도 같이 분석.

## 0. 결론

1. **기능 동등은 가능하다(UE 5.8, 엔진 수정 없이 독립 플러그인).** 공개된 콘솔 변수 13개의 의미·기본값, 주입 지점(DOF 이전), 분할 화면·VR, Path Tracing 비활성, 하늘 대기·볼류메트릭 포그·HV(Heterogeneous Volumes)·구름 강도까지 재현 경로가 있다. 단 안개 매개변수와 볼류메트릭 포그 텍스처는 Renderer 비공개(`FViewInfo`)에만 있어 비공개 헤더 의존이 필요하다 → 엔진 판올림마다 재컴파일·수정 부담(SSFS도 엔진 버전별로 따로 빌드한다).
2. **화소 단위 "완전 동일"은 원본 없이는 만들 수도 증명할 수도 없다.** 공개 자료로 구조(밉 피라미드 + 단계 이월 + 안개 불투명도 보간)는 추정되지만 탭 가중치·합성식·패스 해상도는 미확정이다. 현실적 목표는 "같은 장면·같은 콘솔 변수에서 지표 기준 동등"이며, 이를 검증하려면 원본 1좌석 구입(₩58,330~)이 필요하다. Fab 표기 "AI 사용 허용: 아니요"이므로 원본 셰이더·소스는 에이전트에게 열지 않는 클린룸으로 간다.
3. **엔진 내장 FSSS(Fog Screen Space Scattering, 5.8 실험)는 코드 0줄 대안이지만 SSFS와 성격이 다르다.** 안개 패스 안에서 돌고, 블러 폭이 물리 식(깊이×√광학두께)으로 정해진다. 탑다운 거리(9.6~26.6m)에서는 기본값 밉 지수 0.005 이하라 **거의 보이지 않을 것으로 추정**한다(SF-0 실측 대상). Spread에 상한이 없어 손입력으로 키울 수는 있지만 물리 근거가 없는 균일 블러가 되고 화면 아래쪽이 어두워질 수 있다. 기본 스케일러빌리티에서 Epic·Cine 품질만 켜지고, 블루프린트 Set 함수가 없다.
4. **현재 탑다운 카메라와 기본 안개(밀도 0.02)에서는 어느 방식이든 효과가 약하다**(화면 안개 불투명도 0.9~2.4%, 엔진 식). SSFS의 합성식이 추정(4.2절 ①)대로 "강도×불투명도 보간"이고 Intensity에 상한이 없다면 화면 고정 반경 블러를 강도로 끌어올릴 수 있지만, 원본 없이는 확정할 수 없다. 3인칭은 두 방식 모두 원래 설계 대상이나, 같은 안개에서도 200m 17%·1km 61%라 원경이 수백 m 이상 보여야 뚜렷하다.
5. **반투명(불·연기 파티클)은 두 방식 모두 기본적으로 흐려지지 않는다.** 머티리얼 기본값이 "Render After DOF"라 SSFS의 DOF 이전 주입보다 뒤에 합성되고, FSSS는 반투명 이전에 돈다. 반투명까지 흐리려면 해당 머티리얼을 Before DOF로 바꾸거나 별도 패스가 필요하다.
6. **권장**: SF-0 측정 장치 + FSSS 실측(코드 0) → SF-1 클린룸 플러그인 `TDFogScattering` 프로토(높이 안개만, 비공개 헤더 컴파일 탐침 포함) → SF-2 전체 기능 → SF-3 원본 블랙박스 대조(구입 결정 시). 6절.

## 1. 대상 제품 SSFS (공개 자료 기준)

- 저자 Dmitry Karpukhin, v1.32(2026-09-29 갱신), 코드 플러그인(C++ 클래스 2개), UE 4.27·5.0~5.8, Win64·Mac·Linux. Fab 평점 4.9(89), 표준 라이선스, "AI 사용 허용: 아니요". itch: 수정·상업 사용 허용, 소스 형태 재배포·재라이선스 금지, 좌석당 1 라이선스.
- 기능: 높이 안개가 있으면 자동 적용, 효과 세기는 안개 설정에 의존, 볼류메트릭 포그·PC VR·분할 화면·하늘 대기·HV 지원, 추가 설정 없음, 기본 후처리와 호환.
- 주입: `SceneViewExtensionBase`, DOF 이전.
- 성능(1.02 측정, 1080p): RTX 3080 Ti 0.20ms, RX 6800M 0.24, RTX 3060 0.38, GTX 1080 Ti 0.43, GTX 1660 0.66. 기본 Bloom 대비 40~50% 비쌈, 해상도에만 비례.

| 콘솔 변수 | 기본 | 의미(공개 문서) |
|---|---|---|
| `r.SSFS` | 1 | 켜기 |
| `r.SSFS.QualityPreset` | 2 | 0~3, 다운/업 탭 4/5·8/5·8/9·13/9 |
| `r.SSFS.Format` | 1 | 0 R11G11B10, 1 RGBA float(1.32부터 SceneColor 형식 상속) |
| `r.SSFS.LuminanceWeighting` | 1 | Karis 평균(밝은 점 억제) |
| `r.SSFS.PassAmount` | 8 | 최대 12, 권장 1080p 8·1440p 9·4K 10 |
| `r.SSFS.Radius` | 0.9 | 점진 반경, 권장 0.8~0.99 |
| `r.SSFS.Intensity` | 1.0 | 전체 강도(안개로 배율) |
| `r.SSFS.SkyAtmosphere` / `...Intensity` | 0 / 1.0 | 하늘 대기 |
| `r.SSFS.VolumetricCloud` / `...Intensity` | 0 / 1.0 | 구름(실험, `r.VolumetricRenderTarget.Mode 0` 필요) |
| `r.SSFS.VolumetricFogILSIntensity` | 1.0 | 볼류메트릭 포그 통합 광산란 강도 |
| `r.SSFS.HeterogeneousVolumes` | 1 | HV |

변경 이력 요지: 1.02 외곽선 아티팩트 수정 · 1.10 Karis 평균·하늘 대기 · 1.11 NaN 차단 · 1.20 HV·구름(실험)·원거리 정밀도·화면 어두워짐 수정 · 1.21 볼류메트릭 포그 노출 수정·Path Tracing 비활성 · 1.30 VR·분할 화면·품질 4단계 · 1.32 점 샘플링·SceneColor 형식 상속·블루프린트에서 보이는 서브시스템·5.6+ GPU 통계.

비교 이미지 직접 관찰(2560×1440, 2쌍): 먼 물체일수록 강하게 번지고(원경 기둥·나무가 거의 형체만 남음), 안개 속 광원 후광이 넓어지며, 가까운 바닥은 거의 그대로다. 전체 밝기는 거의 유지된다 → "안개 불투명도로 원본과 블러를 보간"하는 에너지 보존형과 맞는다.

## 2. 엔진 내장 FSSS (UE 5.8 실험 기능)

5.8 릴리스 노트에 "Experimental"로 도입. 안개 컴포넌트 `bEnableFSSS`(기본 false)로 켠다. 경로 약칭 `[R]`=`Engine/Source/Runtime/Renderer/Private/`, `[S]`=`Engine/Shaders/Private/`.

**알고리즘**(기호: L 장면색, F 안개 산란 휘도, T 안개 투과율, s 깊이(m), τ=−ln T)
1. 안개 컴퓨트가 분리 합성 텍스처 2장을 쓴다(`[S]HeightFogPixelShader.usf:220-241`). 주입비 `a = pow(saturate((1−T)·AmountScale), AmountPower)`, 텍스처0 = `F + T·a·L`, 텍스처1 = `(W, T·(1−a))`.
2. 점확산 폭 `W = sqrt(0.5/(A+B))`, Premoze 2004 식(`[S]FogScreenSpaceScattering.ush:40-60`). 알베도 1·흡수 0이므로 **W = 0.612·s·√τ**.
3. 밉 체인: 밉 수 `ceil(log2(min(가로,세로)))`(1080p 11장), 4×4 박스 다운샘플 + 5×5 가우시안 필터(탭마다 `MaxExposedLuminance` 10 클램프) + `lerp(밉k, 밉k+1, BlurControl)` 이월(`[R]FogSeparateComposition.cpp:242-320`, `[S]FogSeparateComposition.usf:37-226`). 밉1은 TAA 다운샘플(`r.Fog.ScreenSpaceScattering.TAA`).
4. 합성: 밉 지수 `m = saturate(1−T(1−a)) · SpreadScale · clamp(log2(W/2), 1, 밉수−1)`, 페이드 `saturate(W)`, 최종 `f·밉m[F + T·a·L] + T(1−a)·구름투과율·L`(`[S]FogSeparateComposition.ush:18-51`). 에너지 이동형이나 클램프·W<1 페이드에서 손실.

| 대상 | FSSS 산란 |
|---|---|
| 높이 안개·볼류메트릭 포그·Local Fog Volume | 됨 |
| 하늘 대기 공중 원근 | 조건부(sky 머티리얼 + 분리 합성 안에서 그릴 때) |
| 볼류메트릭 구름 | 부분(블러 입력엔 들어가나 밉 선택에서 제외) |
| HV·반투명(파티클·불·연기) | 안 됨(안개 뒤에 그려짐, `[R]DeferredShadingRenderer.cpp:3955-3970`) |

- 지원·비용: 풀해상도만(반해상도 미구현 `[R]FogSeparateComposition.cpp:79-83`), 1080p 디스패치 약 28개 + 안개 컴퓨트·TAA·합성, 텍스처 약 50MB(1080p). 디퍼드 데스크톱·`PF_FloatRGBA` 타입 UAV 로드 필요, 포워드·모바일·Path Tracing 제외, 분할 화면·스테레오는 뷰별. 밀리초 비용 미측정.
- 스케일러빌리티: `BaseScalability.ini` EffectsQuality 0~2는 `r.Fog.SeparateComposition=0`(FSSS 강제 꺼짐, 823·852·881행), Epic·Cine만 허용(913·948행).
- 첫 안개 컴포넌트(`ExponentialFogs[0]`)만 읽는다. 속성 5개(켜기 1 + 매개변수 4)는 `BlueprintReadOnly`이고 Set 함수가 없다 → 런타임 변경은 C++ 대입 + `MarkRenderStateDirty()`(미실측). AmountScale·SpreadScale은 UI 상한(1)만 있고 ClampMax가 없다(`Engine/Classes/Components/ExponentialHeightFogComponent.h:206, 221`).
- 밉 이월은 바로 위 밉 하나와의 병렬 혼합이다(재귀 누적 아님, `[R]FogSeparateComposition.cpp:296-320`). W의 s는 광선 길이가 아니라 시야 깊이(SceneDepth)다.
- 콘솔 변수: `r.Fog.SeparateComposition`(-1), `r.Fog.ScreenSpaceScattering`(1), `r.Fog.ScreenSpaceScattering.MaxExposedLuminance`(10), `r.Fog.ScreenSpaceScattering.TAA`(1).
- 알려진 문제: 릴리스 노트 — 구름·HV 미지원, 품질 단계 1개, 시각 결함 가능. 포럼 "Fog Artifacts in 5.8" — 두꺼운 안개의 결함은 FSSS가 아니라 5.8 TSR 얇은 형상 검출 변경 때문(`r.TSR.ThinGeometryDetection.Coverage.ShadingRange 1` 제안).

## 3. SSFS 대 FSSS

| 항목 | SSFS(플러그인) | FSSS(엔진 5.8 실험) |
|---|---|---|
| 주입 시점 | 후처리 DOF 이전(TSR 전, 렌더 해상도). SceneColor에는 불투명·안개·HV·구름·"Render After DOF"를 끈 반투명만 있음 | 안개 패스(HV·반투명 전) |
| 반투명 산란 | 기본 반투명은 안 됨(Before DOF 머티리얼만) | 안 됨 |
| HV 산란 | 됨(이미 SceneColor에 합성) | 안 됨 |
| 블러 크기 결정 | 화면 고정 피라미드(PassAmount·Radius) × 안개 불투명도 가중(추정) | 물리 PSF 폭 W(거리·광학두께) → 밉 선택 |
| 조절 | 콘솔 변수 13개, 품질 4단계, 소스별 강도 | 컴포넌트 속성 5개 + 콘솔 변수 4개, 품질 1단계 |
| 구름·HV | 강도 변수 있음(구름 실험) | 구름 부분, HV 없음 |
| 지원 | 4.27~5.8, VR·분할 화면 | 5.8+, 디퍼드 데스크톱, 기본 Epic 품질 이상만 |
| 런타임 변경 | 콘솔 변수·서브시스템 | C++만(Set 없음) |
| 비용 | 1080p 0.20~0.66ms(공개) | 미측정 |
| 비용(돈) | ₩58,330~/좌석 | 0 |

## 4. 동일 개발 가능성 판정

### 4.1 항목별

| 항목 | 판정 | 방법·근거 |
|---|---|---|
| DOF 이전 주입·SceneColor 교체 | 가능 | `SubscribeToPostProcessingPass(EPostProcessingPass::BeforeDOF)`(`Engine/Public/SceneViewExtension.h:122-138, 222`), 반환 텍스처가 SceneColor를 대체(`[R]PostProcess/PostProcessing.cpp:383-409, 894-899`). DOF가 꺼져도 호출됨(835). 선례 ColorCorrectRegions 플러그인 |
| 반투명 산란(SSFS도 기본은 안 함) | 부분 | 머티리얼 기본 `MTP_AfterDOF`(`Engine/Private/Materials/Material.cpp:1183`) → BeforeDOF 뒤(DOF 976·TSR 808·수동 합성 1059)에 합성. 흐리려면 머티리얼을 Before DOF로 바꾸거나 `TranslucencyAfterDOF` 단계 별도 처리(DOF가 이미 합성하면 콜백이 안 불림, TSR일 때만 보장 `PostProcessing.cpp:974, 1022-1035`) |
| 픽셀별 높이 안개 불투명도 | 부분(우회) | `FFogUniformParameters`·`CreateFogUniformBuffer`는 비공개·미내보내기(`[R]FogRendering.h:16-47`). (b) 비공개 헤더 포함 후 `static_cast<const FViewInfo&>`로 `ExponentialFogParameters` 등을 읽음(멤버 공개, 실제 객체는 FViewInfo) 또는 (a) 게임 스레드에서 컴포넌트 값을 엔진 식(`[R]SceneCore.cpp:389-445`, `[R]FogRendering.cpp:357-457`)으로 재계산 |
| 볼류메트릭 포그 ILS 강도 | 부분(비공개) | 텍스처는 `FViewInfo::VolumetricFogResources`에만 있음. 후처리 시점 수명 미검증 |
| 하늘 대기 | 가능 | 공개 View 유니폼의 `CameraAerialPerspectiveVolume`(`Engine/Public/SceneView.h:1220-1225`) |
| 구름·HV 강도 | 부분(비공개) | 공개 접근 경로 없음, 합성 결과만 SceneColor에 있음. SSFS가 구름에 `Mode 0`을 요구하는 것도 내부 타깃 의존 정황 |
| 분할 화면·VR | 가능(실측 필요) | 뷰별 후처리, `FScreenPassTextureViewport`·ViewRect·텍스처 배열 슬라이스(`Renderer/Public/ScreenPass.h`) |
| Path Tracing 비활성 | 가능 | `EngineShowFlags.PathTracing` |
| 피라미드·Karis·탭 프리셋 | 가능(자체 구현) | 엔진 다운샘플·가우시안은 비공개라 재사용 불가, 플러그인 셰이더로 작성 |
| 대형 월드 정밀도 | 가능 | `ScreenToTranslatedWorld`·`PreViewTranslation`(View 유니폼) |
| 노출 | 가능 | DOF 이전 SceneColor는 PreExposure 적용 후·눈 적응 전(`[S]HeightFogPixelShader.usf:189`) |
| 블루프린트 서브시스템 | 가능 | `UEngineSubsystem` |
| 모바일 | 범위 밖 | SSFS도 데스크톱 대상, 확장 호출은 있으나 입력 구조체 비공개 |
| 엔진 버전 4.27~5.7 | 비권장 | 비공개 헤더 의존부가 버전마다 다름. 5.8 한정이 현실적 |

### 4.2 알고리즘 추정(클린룸 출발점)

- 구조 가설 1: 2의 거듭제곱 피라미드 N=PassAmount 단계, 업샘플 `U_k = lerp(D_k, 텐트(U_{k+1}), Radius)`. 탭 수(4·8·13 다운, 5·9 업)와 Radius 하한(R⁸: 0.8→0.17, 0.9→0.43)을 설명하나 권장 패스 표는 설명 못 함(4K는 1080p보다 단계 +1이어야 하는데 +2).
- 구조 가설 2: 단계마다 화소 수 절반(축 1/√2). `N = round(log2(가로×세로)) − 13`이 1080p 8·1440p 9·4K 10·8K 12(상한)와 모두 맞는다. 작업량은 가설 1의 약 3배.
- 합성 후보: ① `lerp(S, Blur, saturate(Intensity·(1−T)))`(평균 보존, 관찰과 일치, 기본안) ② FSSS식 에너지 이동 ③ Bloom식 덧셈(“어두워짐 수정” 이력과 안 맞아 기각).
- 판별: 원본 구입 시 GPU 프로파일(`ProfileGPU`)에서 패스 이름·텍스처 크기만 확인. 셰이더 탭·RenderDoc 디스어셈블리는 보지 않는다.
- 참고 문헌: Elek 외 2013(IEEE CG&A, DOI 10.1109/MCG.2013.17), Jimenez 2014 COD:AW 후처리(13탭·Karis·텐트 업샘플), Unity SSMS(MIT, `lerp(원본, 블러, t)` 구조).

### 4.3 "완전 동일" 검증

- 원본 없이: 공개 JPEG 8쌍은 노출·안개·콘솔 변수·압축을 몰라 정성 비교만 가능.
- 원본 구입 시 블랙박스 대조: 같은 장면·카메라·콘솔 변수, TAA·Bloom·DOF·모션 블러·자동 노출 끔, HDR(EXR) 캡처. 점광원(방사 프로파일), 깊이 계단(후광), 평탄 장면(평균 휘도비)으로 PassAmount·Radius·밀도를 쓸어 맞춘다. 통과 기준(제안): SSIM > 0.99, ΔE2000 중앙값 < 1.0, 평균 휘도비 ±0.5%, 방사 프로파일 ±10%. 지표 계산은 결정론 스크립트(`Tools/`)로 하고, 원본이 설치된 폴더의 `.usf`·소스는 에이전트 읽기 대상에서 제외한다.
- 원본과 동시 설치 대조를 위해 우리 콘솔 변수는 `r.SSFS.*`와 이름을 겹치지 않게 `r.TDFogScattering.*`로 하고 의미·기본값만 1:1로 맞춘다.

## 5. 탑다운·3인칭·다른 프로젝트

카메라: `BP_TDCombatCharacter` 암 1100cm·피치 −50°·가로 FOV 90°(전투 아레나 중 ×1.4). 스프링암이 캡슐 중심(지면 +96cm)에 붙어 카메라 높이 약 939cm, 16:9에서 화면 아래·가운데·위 지면까지 9.6·12.3·26.6m(아레나 ×1.4면 약 13~37m). 근거 `Docs/Validation/P3-17-topdown.md:6`, `Tools/WorldGen/topdown_rules.py:21, 31`.

엔진 높이 안개 식 그대로(밑 2: 밀도·감쇠 ÷1000, 접힘 `Density·2^(−감쇠·(카메라Z−안개높이))` `[R]FogRendering.cpp:397-406`, 선적분 `(1−2^(−F))/F` `[S]HeightFogCommon.ush:207-214`, 투과율 `2^(−선적분)` 394행), 안개 높이 = 지면, 3인칭은 눈높이 175cm 수평 광선. 불투명도(%):

| 밀도(감쇠 0.2) | 탑다운 9.6m | 12.3m | 26.6m | 3인칭 20m | 50m | 200m | 1km |
|---|---|---|---|---|---|---|---|
| 0.02(프로젝트 기본) | 0.9 | 1.1 | 2.4 | 1.9 | 4.6 | 17.1 | 60.9 |
| 0.05 | 2.1 | 2.7 | 5.8 | 4.6 | 11.1 | 37.4 | 90.4 |
| 0.2 | 8.3 | 10.5 | 21.3 | 17.1 | 37.4 | 84.7 | 100 |

볼류메트릭 포그가 켜진 맵에서는 가까운 구간을 볼류메트릭 결과가 대체하므로(`[S]HeightFogPixelShader.usf:184`) 이 표는 높이 안개 단독 값이다. 안개 높이는 이계 설정(`TDRealmDefinitions.h:46`)에 따라 달라진다.

- FSSS(기본 Spread 0.1, Scale·Power 1, 밀도 0.02): 탑다운 시야 깊이 8.3~23.2m, W = 0.47~2.2 → `log2(W/2)`가 하한 1로 묶여 밉 지수 ≈ 0.002~0.005(사실상 블러 없음, 추정). UI 범위(Spread ≤ 1)에서는 최대 밉 1. Spread를 손입력으로 1보다 크게 하고 Scale을 키우면(덮임 → 1) 밉 = Spread가 되어 안개 농도와 무관한 균일 블러가 되며, W<1인 화면 아래쪽은 산란분과 안개 산란광이 W배로 줄어 최대 절반가량 어두워질 수 있다(추론). 3인칭 200m(불투명도 17%)는 W≈53 → 기본 밉 0.15, Spread 1이면 1.5.
- SSFS식(합성 가설 ①, 미확정): 블렌드 가중 = Intensity × 불투명도 → 탑다운 기본 0.9~2.4%. 가설이 맞고 Intensity 상한이 없다면 화면 고정 반경 블러라 짧은 거리에서도 광원 후광·번짐을 만들 수 있다.
- 탑다운에서 의미 있는 효과: 밤·동굴의 횃불·마법 광원 후광(광원 머티리얼이 불투명·발광이거나 Before DOF일 때), 짙은 지면 안개(두 번째 안개 층·Local Fog Volume)가 주 용도. 원경 연화는 시야에 원경이 없어 거의 없다.
- 3인칭 전환: 두 방식 모두 원래 대상. 다만 밀도 0.02에서는 약 150m 이상 원경에서야 불투명도가 15%를 넘으므로, 3인칭 맵의 안개 값은 따로 정해야 한다.
- 재사용 구조(권장): 프로젝트 플러그인 `Plugins/TDFogScattering/`(첫 자체 프로젝트 플러그인 — AGENTS 14절은 `Source/TDGame` 폴더만 규정하므로 D-62에 위치를 명시, 런타임 모듈 하나, `LoadingPhase: PostConfigInit`, `AddShaderSourceDirectoryMapping("/TDFogScattering", ...)`), TDGame 모듈 비의존, 의존은 Core·Engine·RenderCore·RHI·Renderer. 클래스 `FTDFogScatteringViewExtension`, `UTDFogScatteringSubsystem`(UEngineSubsystem, 블루프린트 노출). 다른 5.8 프로젝트에는 폴더 복사로 이식. 다른 엔진 버전은 비공개 헤더 의존부 이식 필요.
- 카메라별 강도 배율(탑다운 보정)은 SSFS에 없는 기능이라 "완전 동일" 범위 밖 — 필요 시 별도 결정.

## 6. 프로젝트 영향

- 안개가 있는 맵: `LV-World`(기본 맵, 안개·하늘 대기·구름), `L_TDWorld_Main`, `LV_DarkFantasy_OpenWorld`, `LV-Cambat`, FluidFX FireStage. `LV_TDBossArena`·`LV_TDMegaMagicArena`는 안개 없음 → 효과 없음.
- `TDRealmSubsystem`이 첫 안개 컴포넌트의 밀도·감쇠·색·높이·MaxOpacity·시작 거리·두 번째 안개·볼류메트릭 값을 런타임 블렌딩하고 안개 Z를 옮긴다(`Source/TDGame/World/Streaming/TDRealmSubsystem.cpp:873-884, 941-967`) → 엔진 안개 매개변수를 읽는 플러그인은 자동으로 따라간다. FSSS 값은 Set이 없어 이계별 조절을 하려면 C++ 대입이 필요. 서브시스템은 `FindFirstActorComponent`, FSSS는 `ExponentialFogs[0]`로 고르므로 안개가 둘 이상인 맵에서는 다른 컴포넌트일 수 있다.
- FluidFX 측정: 높이 안개가 색 지표를 끌어감(L-editor-47). 산란이 켜지면 FX 화면 대조에 또 하나의 변수 → 측정 캡처에서 끄는 콘솔 변수 필요. 불·연기 반투명은 머티리얼이 기본(After DOF)이면 어느 방식에서도 번지지 않는다.
- 렌더 설정: 디퍼드·Lumen·Substrate·레이 트레이싱·DX12 SM6, `DefaultScalability.ini` 없음(엔진 기본 → FSSS는 Epic 품질 이상만). 프로젝트에 SceneViewExtension 사용처 0건, 셰이더 모듈은 `TDFluidGpu`(컴퓨트 전용, PostConfigInit) 하나.
- 규칙: 로직 C++·셰이더(AGENTS 7절 부합), 이름 `TD` 포함(3절), 새 모듈은 라이브 코딩 불가 → 에디터 재시작(L-build-01), 빌드 창은 보류 파일로 조정(현재 `fx4b-wt` 보유).

## 7. 권장 진행안과 결정 대기

| 단계 | 내용 | 산출·통과 기준 | 상태·담당 |
|---|---|---|---|
| SF-0 | 측정 장치: 고정 카메라(탑다운·3인칭) 캡처, 켜기/끄기 차이 지표 스크립트, GPU 패스 시간 수집 + FSSS 실측(코드 0, LV-World) | 두 카메라 FSSS 켜기/끄기 화면·ms, 탑다운 가시성 판정 | SF-1에 흡수(측정 장치), FSSS 실측은 선택 |
| SF-1 | `TDFogScattering` 프로토: BeforeDOF, 높이 안개 불투명도(비공개 헤더 탐침 → 실패 시 재계산 우회), 피라미드(가설 1) + 보간 합성, 콘솔 변수 골격 | 컴파일, 켜기/끄기 PIE 화면, 1080p ms ≤ 0.5(제안) | done(2026-10-23 claude, 미커밋), 결과 `02-sf1-spec.md` 10절 |
| SF-2 | 전체 기능: 품질 프리셋·Karis·Format·하늘 대기·ILS·HV·구름(실험)·VR/분할 화면·Path Tracing·서브시스템·GPU 통계 | 콘솔 변수 13개 대응표 검사, 분할 화면 번짐 없음 | done(2026-10-23 claude, 미커밋), 결과 `03-sf2-spec.md` 9절 |
| SF-3 | 원본 블랙박스 대조(구입 시): 가설 판별, 매개변수 맞춤 | 4.3절 지표 통과 | 취소(D-62 원본 미구입) |
| SF-4 | 탑다운 보정(D-62 채택, 기본 꺼짐): 거리 배율·가중 지수·이계별 강도, 측정 캡처 끄기 | 사용자 화면 승인 | done(2026-10-23 claude, 미커밋), 결과 `04-sf4-spec.md` 6절, 실제 월드 화면 확인 남음 |

결정 대기(D-62): ① 경로 — FSSS로 충분한지(SF-0 뒤) / 클린룸 플러그인 진행 ② 원본 구입(동일성 검증용) 여부 ③ 지원 엔진 버전(5.8 한정 권장) ④ 탑다운 보정 기능을 "동일" 범위 밖으로 추가할지.

## 8. 미검증·위험

- 비공개 헤더(`Renderer/Private`) 포함 컴파일 가능성, `FViewInfo` 배치의 빌드 구성 의존 — SF-1 첫 작업으로 탐침.
- 볼류메트릭 포그 텍스처의 후처리 시점 수명, 인스턴스드 스테레오 슬라이스 처리, 5.6+ GPU 통계 매크로 변경.
- FSSS 밀리초 비용·탑다운 화면, W<1 페이드 체감, `PF_FloatRGBA` 타입 UAV 로드 지원(DX12 데스크톱은 보통 지원, 미확인).
- SSFS 구조 가설 1·2, 합성식, Intensity 범위 — 원본 없이 확정 불가.
- Fab 약관 원문(403)과 "AI 사용 허용: 아니요"의 정확한 범위 미확인 → 원본은 블랙박스로만 쓰는 보수적 운용.

## 근거

- 제품: https://dmkarpukhin.com/ssfs/ , https://dmkarpukhin.com/docs/ssfs/ , https://dmkarpukhin.itch.io/screen-space-fog-scattering , Fab 목록(브라우저 확인 2026-10-23), 80.lv 기사 2건.
- 엔진: UE 5.8 소스(위 파일:줄), 릴리스 노트 https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes , 포럼 https://forums.unrealengine.com/t/fog-artifacts-in-5-8/2762892
- 프로젝트: `Config/DefaultEngine.ini:49-114`, `TDGame.uproject:6-31`, `Source/TDGame/Characters/TDGameCharacter.cpp:138-150`, `Source/TDWorldGen/Public/World/TDRealmDefinitions.h:40-61`.
- 불투명도 표 계산: 엔진 식(밑 2, 5절 서술)을 파이썬으로 평가, 카메라 높이 939cm·안개 높이 0. 초안은 자연지수로 계산해 약 2배로 틀렸고 반박에서 정정(`Saved/AgentOps/20261023/ssfs/refute-numbers.md`, `refute-engine.md`).

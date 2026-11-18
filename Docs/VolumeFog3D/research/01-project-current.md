# 01. 프로젝트 현황: 지금 FluidFX 안개는 어떻게 돌고, 새 3D 안개가 무엇을 가져다 쓸 수 있나

작성 2026-11-12 (읽기 전용 조사, 코드·장부 변경 없음). 표기: [확인: 경로:줄]=원문을 직접 열어 확인, [추정]=내 추론, 미확인=못 찾음. 개발 PC는 RTX 2060(6GB).
약어: GPU=그래픽 처리 장치, RDG=렌더 의존성 그래프, HVOL=엔진의 이질 볼륨(Heterogeneous Volume) 컴포넌트, SDF=부호 있는 거리장, GT=게임 스레드, SVOL=자체 레이마칭 볼륨(상자 메시), PIE=에디터 안 게임 실행, Mcell=칸 100만 개.

## 0. 한눈에 보는 결론

1. 지금 안개의 시뮬레이션은 전부 평면(2D) 격자 한 장이다. 칸 하나에 밀도·속도(x,y 둘뿐)·압력이 있고, 높이(z) 칸은 시뮬레이션에 없다. 3D 텍스처 읽기는 정적 노이즈 한 장(T_TD_FluidNoiseVolume)뿐이다. [확인: Source/TDFluidGpu/Private/TDFluidGpuStepPasses.cpp:41-57 포맷표, 셰이더 전체에서 RWTexture3D 없음, Tools/FluidFX/Shaders/TDFluidFogVolume.hlsl 첫 줄]
2. 그림에서만 3D처럼 보인다. "밀도가 높은 곳일수록 안개가 더 높이 올라가는" 한 줄짜리 높이 규칙과 흐르는 3D 노이즈로 부피감을 만든다. [확인: TDFluidFogVolume.hlsl fogTopCm/fogLayerProfile/fogErosionNoise 줄]
3. 이 방식이 못 하는 것: 위아래로 흐름, 세로로 내려치는 베기, 책상 밑·위에 따로 있는 안개, 한 칸에 두 층, 높은 곳에서 날아가는 파편의 높이별 반응. 근거는 2절.
4. 이미 "주변만 계산"에 가까운 부품이 있다: 거리로 재우고 깨우는 액터(잠든 액터 0.0025~0.0075ms), 16x16 구역별 스탬프 목록, 플레이어 따라 창 이동. 다만 격자 자체는 빽빽한 2D 한 장이라 희소(Sparse) 구조는 없다.
5. 이 PC에서 측정된 가장 가까운 비용: 엔진 볼륨 안개 패스 1.08~1.52ms(약 1080 높이 내부 해상도, 칸 8픽셀·깊이 128층 ≈ 415만 칸), 물 GPU 계층 한 스텝 1024² 약 0.98ms(= 칸 100만 개당 약 0.84ms + 고정 약 0.10ms [추정: 세 점 직선 맞춤]). 안개/연기 GPU 시뮬레이션 단독 ms는 문서에서 못 찾음(미확인).
6. 재사용: 입력 수집(니아가라 데이터 채널 읽기, 파편 추적, 브러시 원천), 구동 컴포넌트(HVOL·안개 상자 배치), 지형 높이 캡처, 측정·판정 도구는 그대로 쓸 수 있다. 압력 풀이·와류·시프트·투과율은 2D 가정이 박혀 다시 써야 한다(10절 표).

## 1. 지금 안개 종류와 데이터 흐름

### 1.1 종류와 설정값 (정본 Content/FluidFX/PresetData/TDFluidPresetTuning.json, Kinds 절)
| 종류 | 격자(2D) | 창(가로 길이) | 칸 크기 | 백엔드 | 특징 |
|---|---|---|---|---|---|
| FogMist | 256x256 | 4000cm | 15.6cm | Gpu | 지형 높이 따라가기 켬, 높이원 SceneCapture |
| FogMistStir | 256x256 | 4000cm | 15.6cm | Gpu | FogMist와 같은 셰이더·캡처, 휘젓기 입력 |
| FogTileSim | 256x256 | 3000cm, 감싸기(Wrap) | 11.7cm | Gpu | 압력 반복 12, 상자 배율 3(화면 상자 90m 덮음) |
| Smoke / SmokeHeterogeneous / FloorSmokeCapture(HVOL 바닥 연기) | 256x256 | 700cm | 2.7cm | (미지정, 기본) | HVOL 해상도 256x256x128 |
[확인: JSON Kinds 항목, Tools/FluidFX/editor_make_fluid_fog_stage.py:53-56 창·배율·감싸기]

### 1.2 렌더 경로 3가지 (모두 2D 결과 텍스처 `DensityPressureTexture` 한 장을 읽음)
- 경로 A, 안개 상자(FogMist·FogMistStir·FogTileSim): 정적 메시에 Volume 영역 재질을 씌워 엔진 볼륨 안개 격자(froxel: 화면에 맞춘 3D 칸)에 밀도를 주입한다. 재질 Custom 노드 하나가 TDFluidFogVolume.hlsl이고 `return float4(FogExtinctionPerM*FogExtinctionTint*fogDensity, fogDensity)`로 소광·밀도를 낸다. [확인: TDFluidVolumeDriverComponent.cpp:385-400(상자 컴포넌트 생성), editor_make_fluid_fog_material.py 1-5행, TDFluidFogVolume.hlsl 마지막 줄]
- 경로 B, 엔진 이질 볼륨(HVOL): `UHeterogeneousVolumeComponent`를 만들고 `VolumeResolution = (XY 256, XY 256, Z 128)`(기본값, 가로 16~1024·세로 8~512 한도)로 엔진이 재질을 칸마다 계산해 3D로 굽는다. 셰이더 TDFluidHeterogeneousVolume.hlsl은 같은 2D 텍스처를 (x,y)로 읽는다. [확인: TDFluidVolumeDriverComponent.cpp:369-380, TDFluidVolumeDriverTypes.h:56-60, TDFluidHeterogeneousVolume.hlsl:23]
- 경로 C, 자체 레이마칭(SVOL): 상자 메시 위 재질이 매 화소 직접 광선을 쏜다(TDFluidVolumeRaymarch.hlsl, 단계 수·그림자 단계 1~16·캡슐 후광 루프). 2D 텍스처 읽기 + 3D 노이즈. 투과율은 별도 컴퓨트 패스(TDFluidGpuTransmittance.usf, 4단계, 해상도 1/4, 행진 4~24걸음)가 2D 밀도에서 높이 스케일 0.68 같은 상수로 미리 계산한다. [확인: Raymarch.hlsl:99-109·128, TDFluidGpuInputs.h:62-70, TDFluidVolumeTransmittance.h]
- 보조: Plugins/TDFogScattering은 시뮬레이션이 아니라 후처리다. "높이 안개 투과율 가중 밉 피라미드 블러를 심도 흐림 앞에 합성"하는 GPU 패스 모음이며 콘솔 변수 `r.TDFogScattering`로 켠다. [확인: TDFogScattering.uplugin, TDFogScatteringPasses.cpp:1-60]

### 1.3 높이(z)는 무엇으로 만들어지나 (FogMist 기준, TDFluidFogVolume.hlsl)
- 바닥 높이 fogFloorCm: 구동 안 되면 상자 아래면, 구동되면 창 중심 z. 지형 따라가기가 켜지면 위에서 아래로 찍은 정사영 높이 캡처(R32F 256², TerrainOnly)에서 읽은 지형 높이로 교체. 지형 아래 칸은 소광 0(`fogTerrainKeep`). [확인: 같은 파일 fogFloorCm 블록, TDFluidVolumeDriverComponent.cpp:416-433·754-776]
- 위쪽 끝 높이: `fogTopCm = FogExtrudeHeightCm * pow(밀도, FogExtrudeExponent)`(기본 90cm), 그 위는 부드럽게 0으로. 즉 한 (x,y)에 안개 기둥 하나, 높이는 밀도 하나로 결정.
- 부피 질감: 월드 좌표 + 바람 이동량으로 3D 노이즈를 읽어 `FogNoiseSubtract(0.3)` 만큼 깎는다. HVOL 경로는 2D 속도장으로 노이즈를 두 위상 교차 이동시켜 "흐르는 듯" 보이게 한다. [확인: TDFluidHeterogeneousVolume.hlsl:81-85]
- 가장자리: 창 가장자리 부드러운 가중치와 카메라 거리 페이드. 창 밖은 같은 재질의 "패시브" 노이즈 패턴(슬래브·원통·구 껍질 모양)으로 이어 붙임.

### 1.4 '진짜 3D가 아니다'가 구체적으로 뜻하는 것 (코드 근거)
| 장면 | 지금 | 근거 |
|---|---|---|
| 칼을 위에서 아래로 내려치기 | 스탬프는 원판+평면 속도 하나. 세로 성분은 버려지고, 땅에서의 높이는 "높이 페이드" 하나로 세기만 줄임 | FTDFluidGpuPackedStamp(48바이트: 평면 중심·평면 속도·반경)[확인: TDFluidGpuStampBins.h], `ComputeHeightFadeFactor(HeightAboveSurfaceCm, FadeDistanceCm)`[확인: TDFluidInputBrushRules.h:13] |
| 책상 밑 안개/윗면 안개 | 한 칸에 기둥 하나라 상판 위·아래를 따로 못 둠. 바닥 높이도 TerrainOnly 캡처라 상자·책상 윗면은 바닥이 아님 | 결정 'HVOL 바닥 연기 지형만 캡처': 상자 윗면까지 연기가 덮이는 문제를 TerrainOnly로 고침[확인: Docs/Tasks/decisions.md:510] |
| 안개가 위로 솟고 아래로 가라앉음 | 속도는 float2뿐. 높이 변화는 밀도→높이 규칙의 결과일 뿐 | VelocityIn는 `Texture2DArray<float2>`, 포맷 PF_G16R16F[확인: TDFluidGpuAdvectVelocity.usf:3-5, StepPasses.cpp:41] |
| 파편이 공중을 날며 지나감 | 파편 위치는 평면으로 눌러 스탬프. 높이 2m 파편도 바닥 칸을 휘젓는 것으로 처리(높이 페이드만 적용) | TDFluidFragmentEmitterActor가 지오메트리 컬렉션 잎 자세 이력을 평면 트레일 스탬프로 변환 [확인: 헤더 FTDFluidFragmentPositionHistory·LeafPoses], 브러시 원천 열거에 Fragment[확인: TDFluidTypes.h:33-46] |
| 오버행·동굴·다층 | 지형 높이도 단일값 정사영 | 높이 캡처가 위에서 한 번 찍은 한 장(R32F) |

장점도 분명하다: 비용이 2D 한 장 + 엔진 볼륨 안개 주입이라 싸고, 결정론 해시 시험이 걸려 있다(D-48의 골든 체크섬).

## 2. 입력 경로 (플레이어·무기·파편·장애물·지형 → 시뮬레이션)

| 입력 | 들어가는 방식 | 한도 | 3D로 넓힐 때 |
|---|---|---|---|
| 브러시 스탬프(플레이어 발·무기 뼈/피벗·포인터·트랙 점·전투 이벤트) | CPU에서 평면 투영(`FTDFluidPlaneBasis` U,V,N)해 `FTDFluidBrushStamp`로 만든 뒤 48바이트로 포장 | 틱당 16000개, 구역(bin)당 256개, 구역 16x16=256개 [확인: TDFluidGpuInputs.h:27-28·39-40] | 수집(원천·필터)은 재사용, 포장 구조체·평면 투영·구역 분할은 3D로 재작성 |
| 구역 나눔(StampBins) | 창을 16x16 구역으로 나눠 칸마다 자기 구역의 스탬프만 검사 → 이미 "주변만 계산" | 위와 같음 | 3D 구역(예 16³)으로 확장. 개념은 그대로 |
| 대량 점 입력(MassInput) | 니아가라 데이터 채널(Position·선택 Velocity·선택 RadiusCm)을 C++ 리더가 읽어 점 32바이트로 포장 → 고정소수 원자 덧셈으로 누적 → 적용 | 업로드당 16000점, 누적 격자는 기본 1/2 해상도, 반경 상한 8칸(최대 16), 채널 5개 [확인: TDFluidMassInputDataChannelReader.h, TDFluidGpuMassInput.h, TDFluidGpuInputs.h:29-38] | 점 자체는 3D(Position이 FVector). 누적 격자·반경 제한·원자 덧셈 격자를 3D화(메모리 급증 주의) |
| 장애물(정적 물체·캐릭터) | `FTDFluidSdfObstacle`(원판 또는 회전 상자, 3D 반크기 필드 `BoxHalfExtents3DCm` 있음)을 GPU가 칸마다 목록을 돌며 부호 거리로 굽는다(R16F). 목록이 바뀌거나 창이 움직일 때만 다시 굽는다 | 상자 목록 길이 한도는 미확인 | 3D 박스·회전 정보는 이미 있음. 3D SDF 굽기 + 칸 구역화 필요(칸마다 전체 목록 순회는 3D에서 비쌈 [추정]) |
| 지형 | (가) 높이 캡처 R32F 256² → 안개 바닥. (나) 충돌 마스크: 칸별 지형 높이가 [기준 z, 기준 z+300cm] 안이면 단단한 칸 → 직사각형으로 합쳐 최대 256개(최대 4096) 상자 장애물로 전환 [확인: TDFluidTerrainCollisionMask.h:12, TDFluidVolumeDriverTypes.h:155-159] | 상자 256 | 높이장은 3D에서 "바닥 아래 칸 모두 단단" 한 줄로 단순해짐. 오버행은 여전히 불가 |

2D 가정이 박힌 곳: 스탬프 구조체(평면 2D 벡터), 평면 기저에 얹는 변환, 장애물 SDF(2D 저장), 지형 마스크(칸당 높이 하나), 속도장(float2), 누적 격자.
그대로 쓸 수 있는 곳: 입력 원천 수집(뼈·피벗·포인터·데이터 채널·파편 위치 이력·전투 이벤트 큐), 필터(TDFluidInputActorFilter), 3D 위치·속도를 읽는 데이터 채널 리더, 구역 나눔 아이디어.

## 3. 이동 창: Shift와 WrapArea
- CPU 방식 Shift: 원하는 중심과 현재 중심 차를 칸 크기로 반올림해 정수 칸만큼 이동. 버퍼 9~20개(밀도·속도·압력·지형 고도·경사·열 등)를 새로 할당·복사, 새로 들어온 가장자리 띠는 0. 감싸기 모드면 아예 이동하지 않음. [확인: TDFluidGrid2DSolver.cpp:609-650]
- GPU 방식: ShiftGather와 ShiftScatter 두 패스가 스테이지 텍스처로 옮김. 장애물이 있으면 이동 때마다 SDF 재굽기 플래그가 선다. [확인: StepPasses.cpp:1498·1577, TDFluidGpuBatch.cpp:454-463]
- 감싸기(Wrap, 환형 주소): 칸 번호를 `& (해상도-1)`로 되돌려 데이터 이동 없이 창이 돈다. 해상도는 2의 거듭제곱이어야 하고 상한 256. FogTileSim이 사용. 대신 같은 패턴이 반복되고 그 위 상자를 3x3(90m)로 깔아 덮는다. [확인: TDFluidGpuCommon.ush:239-247, TDFluidGpuInputs.h:15, editor_make_fluid_fog_stage.py:56]
- 한계(측정): 물 액터 창 이동 프레임 p95는 개선 전 4.45~4.76ms → 병렬화·비동기 트레이스 뒤 2.72~2.87ms(깨어 있음 평균의 1.43~1.48배). 깨어난 뒤 지형 큐가 끝나기까지 0.66~0.67초. [확인: Docs/Validation/FluidFX/D-49-fxw1-perf-2026-10-23.md 표 3절·2절] (CPU 물 경로 기준. 안개 GPU 경로의 이동 비용은 미확인)
- 지형 높이 캡처는 중심이 4칸(기본 `SceneCaptureRecaptureCells`) 움직일 때 다시 찍는다. 강제 재캡처는 독립 렌더러일 때 약 +0.585ms, 게임 뷰포트 안 주 렌더러 캡처일 때 약 +0.036ms. [확인: TDFluidVolumeDriverTypes.h:170-171, Docs/Lessons/editor.md L-editor-93]

## 4. GPU 백엔드 구조
- 모듈 Source/TDFluidGpu. 액터 여러 개를 한 번에 처리하려고 모든 상태 텍스처를 `Texture2DArray`(조각 = 액터)로 만든다. 조각 축은 "액터 목록"이지 높이가 아니다. [확인: StepPasses.cpp:1264 `MakeStateArrayDesc(Resolution, SliceCapacity, Format)`]
- 포맷: 속도 PF_G16R16F, 밀도 PF_G32R32F, 압력해 PF_R32_FLOAT, 장애물 SDF PF_R16F, 발산 PF_R16F, 지형 고도 PF_R32_FLOAT, 출력 PF_FloatRGBA, 다중격자 값 R32F/구멍 비율 G16R16F/성분 라벨 R32_UINT, 열 R32F. [확인: StepPasses.cpp:41-57]
- 패스(소스 등장 순서, 실제 호출 순서는 미확인): ShiftGather/Scatter, RasterizeObstacles, TerrainUpdate, MassSplatPoints, ApplyStamps, Forces, Vorticity, JacobiTile, MultigridDown/CoarseSolve/Up, PressureMeanTiles/Final, SubtractGradient, AdvectVelocity, AdvectDensityPack, Transmittance, ReadbackDownsample. 물 전용 패스는 별도(WaterStep 등). [확인: StepPasses.cpp:1498-2438 RDG_EVENT_NAME]
- 압력 풀이: Jacobi는 16x16 타일 공유 메모리, 한 번 디스패치에 반복 4회(`JacobiIterationsPerDispatch = 4`), 반복 수 1~32. 다중격자는 최소 64 이상에서 강제 가능, 자동 선택은 512 이상, 가장 거친 격자 32, 사이클 1~4(기본 1), 따뜻한 시작 감쇠 32프레임. 안개 256²은 자동일 때 Jacobi로 보임 [추정]. 압력 평균 제거 패스가 따로 있음. [확인: StepPasses.cpp:35, TDFluidGpuInputs.h:12-26]
- 해상도 한도: 16~2048(GPU), CPU 256, 감싸기 256. [확인: TDFluidGpuInputs.h:12-15]
- 품질 프로필: HVOL용 `FTDFluidHeterogeneousVolumeRenderProfile`(최대 걸음 수, Z 해상도, 사전 셰이딩, 조명 캐시 다운샘플, 그림자, 간접광, AO, 최대 추적 거리, 합성, 볼륨 안개 연동; -1 = 건드리지 않음). 드라이버가 획득·반납한다(`bHasAcquiredRenderProfile`). [확인: TDFluidVolumeDriverTypes.h:183-217, DriverComponent.h:152]
- 3D 텍스처를 이미 쓰는 곳: 읽기 전용 정적 노이즈 `T_TD_FluidNoiseVolume`(Texture3DSampleLevel)뿐. 시뮬레이션 쓰기용 3D 텍스처·RWTexture3D 없음. [확인: Raymarch.hlsl:101, VolumeDensityCommon.hlsl:10, Grep 결과 RWTexture3D 0건]
- CPU 읽기 확인용 작은 되읽기 고리(64² 타일 아틀라스, 3중 링) 있음 — 게임플레이가 안개 값을 물을 때 사용. [확인: TDFluidGpuInputs.h:41-43]

## 5. 측정된 비용 (이 PC, RTX 2060)

### 5.1 표 (원문 수치)
| 무엇 | 조건 | 값 | 출처 |
|---|---|---|---|
| 엔진 볼륨 안개 패스 GPU/VolumetricFog | 안개 무대 7A 장면, 내부 약 1080행(ScreenPercentage 113.3), 칸 8픽셀·깊이 128층, TDFogScattering 켬, 안개 높이따라가기 켬/끔 | p50 1.518 / 1.518ms(켬=끔 차이 없음), 평균 1.557 / 1.564 | Saved/AgentOps/20261110/fog-terrain/cost/c1_summary.json [확인] |
| 같은 패스, 언덕(Relief) 장면 | 같은 설정 | p50 1.09(켬)/1.08(끔)ms | 같은 파일 [확인] |
| 전체 GPUTime | 위 장면 | 약 14.7~15.0ms | 같은 파일 [확인] |
| 높이 캡처 강제 재캡처 | 위 조건 | +0.036ms(주 렌더러), +0.585ms(독립 렌더러) | L-editor-93 [확인] |
| 물 GPU 계층 한 스텝(오프스크린) | 레버 적용 후 | 192²: 0.135ms, 512²: 0.318ms, 1024²: 0.982ms(출력 포함 1.088, 이동 중 1.214) | Docs/Validation/FluidFX/FX-4b-budget-2026-10-24.json offscreen_after_levers_ms [확인] |
| 위와 같은 항목 레버 적용 전 | 192² / 1024² | 0.1317 / 1.227ms. 목표(0.10 / 0.90)에는 못 미침으로 판정된 것으로 보임 [추정: 판정 글자가 깨져 값과 한도 비교로 해석] | 같은 파일 B192·B1024 |
| 물 GPU 계층 GT 비용 | 같은 시험 | 0.003ms | 같은 파일 game_thread_water_a_p50 [확인] |
| CPU 솔버(비교용) | 128² FluidSim, 최적화 후, 헤드리스 | 1.07~1.12ms (최적화 전 7.24), 256²: 4.08~4.33ms | D-48-perf md 6.3절 [확인] |
| 액터 4개 PIE 틱 | 128² 4개 | 28.77 → 1.29ms | 같은 파일 6.4절 [확인] |
| 잠든 액터 | CPU 틱 | 0.0041ms(D-48), 0.0075ms(D-49) | [확인] |
| 단일층 물 렌더 1080p | | 중앙값 1.065ms (ms/Mpx 1.29) | D-49 md 2·4절 [확인] |

안개/연기 GPU 시뮬레이션 자체(FogMist 256², Gpu) 단독 GPU ms: 이번에 열어본 문서에서는 못 찾음. CSV에 `GPU/TDFluidGpuSolver` 열이 있음은 확인했으나 값은 못 봄(미확인). HVOL 256x256x128 렌더 비용(`TDGame.FluidFXRhi.VolumeDriver.OffscreenCost` 시험 존재)도 값 미확인.

### 5.2 "칸 100만 개를 한 번 갱신하는 데 몇 ms" 환산
환산 식 두 가지.
(가) 한 스텝 전체: ms/Mcell = 측정 ms ÷ (해상도² ÷ 1,000,000).
- 1024²(1.049 Mcell): 0.982 ÷ 1.049 = 0.94 ms/Mcell
- 512²(0.262 Mcell): 0.318 ÷ 0.262 = 1.21 ms/Mcell
- 192²(0.0369 Mcell): 0.135 ÷ 0.0369 = 3.66 ms/Mcell (고정 비용이 지배)
- 512²와 1024² 두 점으로 직선: 기울기 = (0.982-0.318)/(1.049-0.262) = 0.84 ms/Mcell, 고정 = 0.318 - 0.84*0.262 ≈ 0.10ms. 이 식으로 192²를 예측하면 0.13ms로 실측 0.135와 맞는다. [추정: 세 점 직선 맞춤, 물 계층 기준]
(나) 패스 1회 환산: 스텝당 패스 수 N을 모른다. 물 스텝은 소스에서 15개 안팎의 패스가 보이므로 N≈10~14를 가정하면 패스 1회당 약 0.06~0.08 ms/Mcell. [추정: N 미확인, 불확실도 큼]
(다) 같은 식으로 엔진 볼륨 안개: 칸 수 = (1920/8)x(1080/8 올림 135)x128 = 4,147,200. 1.52ms ÷ 4.147 = 0.37 ms/Mcell(맑은 장면), 1.08ms ÷ 4.147 = 0.26 ms/Mcell. 이 패스는 광원 주입·산란 적분까지 포함해 위 시뮬 값과 직접 비교는 불가. [추정]
(라) CPU: 256² 4.2ms ÷ 0.0655 Mcell ≈ 64 ms/Mcell. CPU로 3D 볼륨은 불가 수준(100만 칸 한 번에 약 64ms). [추정: 단순 선형 환산]

참고 환산(3D 파일럿 상한 감각, 모두 [추정]): 물 계층 값 0.84 ms/Mcell을 2D 기준으로 보고 3D는 이웃이 4→6개, 속도 성분 2→3으로 칸당 2~3배라 가정. 가로 40m·세로 3m·칸 25cm = 160x160x12 = 30.7만 칸 → 약 0.10 + 0.84*0.307*(2~3) = 0.6~0.9ms. 칸 20cm(200x200x15=60만) → 약 1.1~1.6ms. 가정이 틀릴 수 있어 파일럿 측정이 필요.

## 6. 설계에 영향을 주는 프로젝트 규칙과 결정
- 로직은 C++만. 블루프린트·나이아가라 그래프에 규칙·계산을 넣지 않는다. 나이아가라는 입력 전달(데이터 채널)과 시각 출력(불티)용으로만 쓰고, 이 프로젝트는 읽기 쪽을 C++ 리더가 한다. [확인: AGENTS.md 7절, TDFluidMassInputDataChannelReader.h]
- 에셋 로드는 비동기: 드라이버가 `FStreamableHandle`로 슬롯 재질을 미리 요청한다. 새 코드에 LoadSynchronous·ConstructorHelpers 금지, 검사 `python Tools/check_sync_loads.py`. 볼륨 텍스처·재질도 소프트 참조로. [확인: AGENTS.md 7절, DriverComponent.h `SlotMaterialLoadHandle`]
- 값은 데이터: FluidFX 정본은 `Content/FluidFX/PresetData/TDFluidPresetTuning.json`(종류별 격자·창·압력 반복·백엔드·안개 높이)와 `TDFluidMaterialLook.json`. 새 시스템도 같은 방식으로 종류 표를 JSON에 두어야 함. [확인: 파일 존재, 위 1.1표 값]
- 편차는 `Docs/Tasks/decisions.md`에 proposed로 기록, 사전 등록 판정·측정 규칙은 결과를 본 뒤 바꾸지 않는다(Q4-1, Q4-1b, HVOL 바닥 연기). 최근 결정 요약: Q4-1은 FogMist에 지형 높이 캡처로 안개 바닥이 지형을 따라감(껏다 켰다 가능), Q4-1b는 FogMistStir 켬·FogTileSim 켬(재판정 통과)·SMOKE 끔(실내에서 방바닥 아래 랜드스케이프를 봄), HVOL 바닥 연기는 TerrainOnly 캡처로 상자 윗면 연기 침범을 40cm 38.1→3.4/255로 줄임. 주의: 보통 노출에서 타일 안개 층이 약 1/255로 엷어 거의 안 보임(층 밝기는 별도 문제). [확인: decisions.md:507-510]
- 성능 기록 방식: 1~2회 돌려 값을 쓰는 대신 ABBA 반복 + 사전 등록 + 판정 도구(`Tools/FluidFX/*_judge.py`, `pie_profile_fluid.py --csv-gpu`). 새 시스템 비용도 이 도구로 재는 것이 관례. 에디터 PIE는 GPU 값이 보수적으로 나올 수 있음(D-59). [확인: D-49 md 5절]
- 사용자 선호(메모리): 기능 하나마다 전 과정 검증 금지, 빌드·회귀·결정론·오프스크린만 하고 보정·판정은 일괄. 쉬운 말로 설명.
- 엔진 설정 (Config/DefaultEngine.ini): `r.GenerateMeshDistanceFields=True`(50행), 라이브 Lumen 전역 조명 `r.DynamicGlobalIlluminationMethod=1`(51행), `r.RayTracing=True`(59행), `r.Substrate=True`(63행), Direct3D12 SM6 대상(76행). `r.VolumetricFog.*`, `r.HeterogeneousVolumes.*`, DistanceField 세부 설정 줄은 없음 → 엔진 기본값 사용. 시험 계획은 콘솔로 `r.VolumetricFog.GridPixelSize 8`, `GridSizeZ 128`을 건다. 사용자 결정 대기 항목 [3-13]: 볼륨 안개 격자 전역값(잠정 권장은 엔진 기본 8픽셀·128층). [확인: Config/DefaultEngine.ini 해당 줄, Docs/UserApprovalQueue.md:632]
- 미실시: P-29 안개 격자 18장면 측정(FU-6)이 아직 안 돌았음. 그래서 칸 크기·층 수에 따른 엔진 볼륨 안개 비용 곡선은 아직 없음(계획 파일만 있음, 16x64 격자는 1080p에서 522,240칸). [확인: FX-P-audit md 8절 FU-6, fx10_fog_grid_plan.json meta]

## 7. 재사용 가능 부품 표
난이도: 하=거의 그대로, 중=구조 유지하며 차원 확장·수정, 상=다시 써야 함.

| 부품 | 경로 | 3D 확장 난이도 | 이유 |
|---|---|---|---|
| 입력 원천 수집·필터(뼈·피벗·포인터·트랙점·전투 이벤트·액터 필터) | Source/TDGame/FluidFX/TDFluidInputBrushRules.*, TDFluidInputActorFilter.*, TDFluidLiveActorInput.cpp | 하~중 | 3D 위치·속도를 이미 가짐. 평면 투영(PlaneBasis)과 높이 페이드를 건너뛰면 됨 |
| 니아가라 데이터 채널 리더 | TDFluidMassInputDataChannelReader.* | 하 | Position·Velocity가 FVector(3D) |
| 파편 추적(Chaos 지오메트리 컬렉션 잎 자세·이력) | TDFluidFragmentEmitterActor.* | 하 | 3D 자세를 이미 읽음. 출력 쪽만 평면 스탬프 → 3D 점 |
| 대량 점 누적(고정소수 원자 덧셈) | Source/TDFluidGpu/Private/TDFluidGpuMassInput.cpp, TDFluidGpuMassSplat*.ush/usf | 중 | 점은 이미 포장 구조체. 누적 격자·반경 제한을 3D로, 메모리 ×N |
| 스탬프 구역 나눔 | TDFluidGpuStampBins.* | 중 | 16x16 구역 해시 → 3D 구역, 48바이트 구조체를 3D 벡터로 재설계 |
| 장애물 목록 | TDFluidObstacleComponent.*, FTDFluidSdfObstacle(TDFluidTypes.h:266) | 중 | 3D 반크기·회전 필드가 이미 있음. GPU 굽기를 3D SDF + 구역 순회로 |
| 지형 충돌 마스크 / 높이 캡처 | TDFluidTerrainCollisionMask.*, 드라이버의 SceneCapture | 중 | 높이장→"바닥 아래 단단함"으로 쉬워짐. 오버행은 불가 |
| 이동 창 Shift | StepPasses.cpp ShiftGather/Scatter, TDFluidGrid2DSolver::ShiftOriginForSlidingWindow | 상 | 2D 배열 복사 구조. 희소 벽돌(brick) 풀 위 스크롤은 새 설계 |
| 감싸기 주소(Wrap) | TDFluidGpuCommon.ush:239-247 | 중 | `& (N-1)` 환형 주소는 3D로 자연 확장되나 256³=1,677만 칸이라 크기 한도 필요, 반복 패턴 문제 그대로 |
| RDG 컴퓨트 패스 뼈대(배치, 타임스탬프, 상태 텍스처 풀, 에디터 시험) | TDFluidGpuBatch.cpp, TDFluidGpuPassTimestamps.h, TDFluidGpuStepPasses.cpp 패턴 | 중 | 패스 등록·자원 수명·측정 훅은 재사용. 조각 축(액터)과 별개로 3D 자원 필요 |
| 압력 투영(Jacobi 타일·다중격자·평균 제거·기울기) | TDFluidGpuPressure.usf, Multigrid*.usf, KernelPressure*.usf | 상 | 5점→7점, 타일 공유 메모리·다중격자 거칠게 하기 전부 새로 |
| 이류(속도·밀도) | TDFluidGpuAdvectVelocity.usf, AdvectDensityPack.usf | 중 | 쌍선형→삼선형, 성분 3개 |
| 와류 보강·힘 | TDFluidGpuForces.usf, 와류 패스 | 상 | 2D 스칼라 curl → 3D 벡터 curl과 보강 |
| 투과율 사전 계산 | TDFluidGpuTransmittance.usf, TDFluidVolumeTransmittance.* | 상(대체) | 2D 밀도+높이 상수로 만든 근사. 진짜 3D면 엔진 조명 경로 또는 3D 빛 행진으로 교체 |
| 구동 컴포넌트(HVOL·안개 상자 배치·MID 파라미터·품질 프로필) | TDFluidVolumeDriverComponent.*, TDFluidVolumeDriverTypes.h | 중 | 컴포넌트 생성·배치·보임 관리 재사용. 텍스처 파라미터를 2D → 3D(또는 희소 풀)로 바꾸고 재질 새로 |
| 안개 재질 | Tools/FluidFX/Shaders/TDFluidFogVolume.hlsl, editor_make_fluid_fog_material.py | 중 | 밀도 읽기를 3D 텍스처로 바꾸고 높이 규칙 제거. 지형 따라가기 코드는 참고 |
| 정적 3D 노이즈 | T_TD_FluidNoiseVolume | 하 | 세부 침식 노이즈로 계속 사용 |
| 안개 후처리 | Plugins/TDFogScattering | 하(조건부) | 높이 안개 투과율 기준이라 새 3D 안개를 엔진 볼륨 안개로 주입하면 이어서 동작, 자체 레이마칭이면 별도 연결 필요 [추정: 미시험] |
| 데이터 정본 | Content/FluidFX/PresetData/TDFluidPresetTuning.json, MaterialLook.json | 하 | 종류 표 추가 방식 그대로 |
| 측정·판정 도구와 시험 | Tools/FluidFX/pie_profile_fluid.py, *_judge.py, calib/plans, Source/TDFluidGpu/Private/Tests, FluidFX/Tests | 하~중 | ABBA·사전 등록·GPU CSV 열 읽기 틀 재사용, 3D 시험은 새로 |
| 거리별 재우기/깨우기 | ATDFluidLiveActor ActivationVolume | 하 | 액터 수준 희소화로 이미 동작(잠든 액터 ~0.004ms) |

## 8. 열린 질문(이 조사에서 못 닫은 것)
1. FogMist 256² GPU 시뮬레이션 단독 ms (CSV `GPU/TDFluidGpuSolver` 값).
2. HVOL 256x256x128 렌더 ms와 볼륨 안개 froxel 주입 시 재질 평가 몫.
3. 엔진이 3D 텍스처(RDG 또는 볼륨 텍스처)를 HVOL/볼륨 안개 재질에 어떻게 넘기는지: 엔진 소스 확인은 다음 갈래.
4. 장애물 목록 길이 한도(GPU 굽기).
5. 프로젝트의 TDFogScattering이 자체 레이마칭 결과를 포함하는지.

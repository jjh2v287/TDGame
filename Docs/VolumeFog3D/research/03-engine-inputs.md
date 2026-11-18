# 3D 안개 입력·충돌 수단 조사 (엔진 5.8 소스 확인 보고서)

작성: 읽기 전용 조사 보조 / 2026-11-12
표기: [확인: 경로:줄]은 소스를 직접 열어 본 것, [추정]은 추론, 미확인은 이번 조사에서 열어 보지 못한 것.
주의: 도구 호출 예산(45회)을 다 써서 5절(3D 텍스처 컴퓨트)과 4절(캐릭터·무기)은 엔진 소스를 열어 확인하지 못했다. 해당 절은 전부 [추정]이며 다음 단계에서 확인할 목록을 끝에 적었다.

경로 약칭: ENG = C:/Program Files/Epic Games/UE_5.8/Engine, REN = ENG/Source/Runtime/Renderer, PRJ = C:/Project/TDGame.

## 한 줄 결론

안개가 "어디가 막혀 있는가"를 알려면 엔진이 이미 만들어 두는 전역 거리장(Global Distance Field, 가까운 곳은 촘촘하고 먼 곳은 성긴 여러 겹의 3D 거리 지도)을 공짜로 빌려 쓸 수 있다. 하지만 가장 촘촘한 겹도 한 칸이 약 20cm라서 책상 다리 같은 얇은 물체는 못 담는다. 그래서 지형은 높이 지도, 정적 물체는 충돌 도형(상자·캡슐)을 직접 찍기, 움직이는 몸·칼은 캡슐 목록, 파편은 점+반지름 목록으로 나눠 받는 혼합 방식이 현실적이다 [추정].

## 1. 전역 거리장 (Global Distance Field, 이하 GDF)

### 1-1. 지형이 들어가는가
- 들어간다. 지형(Landscape)을 GDF에 넣는 변수가 둘 있고 둘 다 기본 1(켜짐)이다: r.AOGlobalDistanceField.Heightfield [확인: REN/Private/GlobalDistanceField.cpp:90-96], r.AOGlobalDistanceFieldRepresentHeightfields [확인: 같은 파일:179-185].
- 지형 두께는 "복셀(3D 칸) 4개 두께"로 넣는다(r.GlobalDistanceFieldHeightFieldThicknessScale=4) [확인: 같은 파일:187-191]. 지형 전용 처리 파일도 따로 있다(GlobalDistanceFieldHeightfields.h) [확인: Glob 결과, 내용은 미확인].
- 지형을 켠 뒤 바꾸면 "멀리 갔다 돌아와야" 반영된다는 설명이 변수에 붙어 있다 [확인: 같은 파일:183].

### 1-2. 해상도와 겹(클립맵) 범위: 이 프로젝트의 실제 값
이 프로젝트는 루멘(Lumen, 엔진 실시간 간접광)을 쓴다: r.DynamicGlobalIlluminationMethod=1, r.ReflectionMethod=1 [확인: PRJ/Config/DefaultEngine.ini:49,51]. 값 1이 루멘이라는 것은 엔진 일반 지식이며 이번에 열어 보지 않았다 [추정]. 루멘이 켜지면 아래 루멘 전용 값이 쓰인다 [확인: GlobalDistanceField.cpp:466-499 에서 bLumenEnabled 분기].

| 항목 | 변수 | 기본값 | 출처 |
|---|---|---|---|
| 한 겹의 해상도 | r.LumenScene.GlobalSDF.Resolution | 252 | REN/Private/Lumen/LumenScene.cpp:69-75 [확인] |
| 첫 번째 겹의 범위 | r.LumenScene.GlobalSDF.ClipmapExtent | 2500 (cm) | 같은 파일:77-83 [확인] |
| 겹이 커지는 비율 | (코드 고정) | 겹마다 2배 | 같은 파일:261-264 [확인] |
| 겹 개수 | r.AOGlobalDistanceField.NumClipmaps | 4 (루멘이면 보는 거리에 따라 +1 또는 +2) | GlobalDistanceField.cpp:82-88, 425-446 [확인] |
| 해상도 맞춤 | (코드 고정) | 28의 배수로 올림. 252는 이미 28의 9배 | GlobalDistanceField.cpp:463, 475 [확인] |

참고로 루멘이 꺼진 경우: r.AOGlobalDFResolution=128 [확인: 같은 파일:162-168], 첫 겹 범위는 씬 설정의 GlobalDistanceFieldViewDistance 를 지수(r.AOGlobalDFClipmapDistanceExponent)의 3제곱으로 나눈 값 [확인: 같은 파일:496-497]. 지수 기본값은 읽지 않았다(미확인). 이 프로젝트는 루멘을 쓰므로 영향이 작다.

한 칸 크기 계산 [추정]: "범위(Extent)"를 절반 폭으로 보면(소스에서 겹의 전체 크기를 Bounds.GetSize()/해상도로 나누는 것은 확인 [확인: GlobalDistanceField.cpp:670-671], 범위=절반 폭인지는 미확인) 겹 0은 폭 5000cm / 252 = 약 19.8cm, 겹 1은 약 39.7cm, 겹 2는 약 79cm, 겹 3은 약 159cm다. 탑다운 화면(스프링 암 800cm, 60도) 전체가 겹 0 안에 들어온다 [추정: 스프링 암 값은 배경 설명에서 받음].

겹의 중심은 플레이어가 아니라 카메라 위치다 [확인: LumenScene.cpp:276-278 에서 View.ViewMatrices.GetViewOrigin() 사용]. 탑다운은 카메라가 플레이어 위·뒤에 있어 중심이 약간 어긋나지만 반경 2500cm라 문제 없다 [추정]. 1인칭은 카메라=플레이어라 어긋남이 없다 [추정].

희소(sparse) 구조: GDF 자체가 8x8x8 쪽(page, 가장자리 여백 포함해 실제 7칸)으로 쪼개 필요한 쪽만 큰 아틀라스에 담는다 [확인: GlobalDistanceField.cpp:453-454, 461-463]. 점유율 예상 0.3(r.AOGlobalDistanceField.OccupancyRatio) [확인: 같은 파일:74-80]. 즉 일반 3D 텍스처가 아니라 "쪽 표 + 아틀라스" 구조라서 좌표 한 번으로 읽을 수 없다. 샘플 함수는 엔진 셰이더(GlobalDistanceFieldShared.ush)에 있다 [확인: 같은 파일:449 주석 "Must match GlobalDistanceFieldShared.ush", 셰이더 파일 자체는 미확인].

갱신 방식: 카메라가 움직이면 새로 드러난 부분만 갱신(부분 갱신), 큰 겹은 덜 자주 갱신(r.AOGlobalDistanceFieldStaggeredUpdates), 프레임당 갱신할 겹 수는 r.AOGlobalDistanceFieldClipmapUpdatesPerFrame [확인: 같은 파일:116-137]. 움직이는 물체가 바뀌면 비용이 든다는 설명이 있다(r.AOGlobalDistanceFieldCacheMostlyStaticSeparately) [확인: 같은 파일:106-110].

### 1-3. 자체 컴퓨트 셰이더에서 읽는 방법 (나이아가라 방식 포함)
- 나이아가라의 충돌 질의 데이터 인터페이스(NiagaraDataInterfaceCollisionQuery)가 정확히 이 일을 한다. 셰이더 매개변수로 FGlobalDistanceFieldParameters2 구조체를 포함시키고 [확인: ENG/Plugins/FX/Niagara/Source/Niagara/Private/NiagaraDataInterfaceCollisionQuery.cpp:528,537], 실행 때 FNiagaraDistanceFieldHelper::SetGlobalDistanceFieldParameters(Context.GetComputeDispatchInterface().GetGlobalDistanceFieldData(), 구조체)로 채운다 [확인: 같은 파일:540-547].
- 구조체 내용: 쪽 아틀라스·덮임(coverage) 아틀라스·쪽 표·밉 텍스처(모두 Texture3D)와 겹별 중심·범위·좌표변환 배열, 겹 수 [확인: REN/Public/GlobalDistanceFieldParameters.h:48-74].
- 이 구조체를 만드는 원자료는 FGlobalDistanceFieldParameterData(실제 RHI 텍스처 포인터 보관) [확인: 같은 파일:20-46].

### 1-4. 공개 인터페이스인가
공개다. 렌더러 비공개 헤더가 필요 없다.
- REN/Public/GlobalDistanceFieldParameters.h: FGlobalDistanceFieldParameterData, FGlobalDistanceFieldParameters2, SetupGlobalDistanceFieldParameters() [확인: 20-76줄].
- IRendererModule::GetGlobalDistanceFieldParameterData(const FSceneView&) [확인: ENG/Source/Runtime/RenderCore/Public/RendererInterface.h:449].
- 포스트 오파크 렌더 델리게이트의 FPostOpaqueRenderParameters::GlobalDistanceFieldParams [확인: RendererInterface.h:100; 채우는 곳 REN/Private/SceneRendering.cpp:5651].
- UE::FXRenderingUtils::GetGlobalDistanceFieldParameterData(Views) [확인: REN/Public/FXRenderingUtils.h:45].
- 조건: 게임 모듈 빌드 파일에 "Renderer" 모듈이 있어야 한다. 현재 TDGame.Build.cs에는 RenderCore, RHI, NiagaraShader 는 있으나 Renderer 는 없다 [확인: PRJ/Source/TDGame/TDGame.Build.cs:11-40]. 별도 모듈(TDFluidGpu, TDFogScattering)의 빌드 파일은 열지 않았다(미확인).
- 주의 1: 파라미터 구조체 안의 텍스처는 그 프레임에 렌더러가 만든 RHI 포인터라서 렌더 스레드의 그 프레임 안에서만 쓰는 것이 안전하다 [추정].
- 주의 2: 셰이더에서 GDF 샘플 함수를 쓰려면 엔진 셰이더 폴더(/Engine/Private/…)를 포함해야 한다. 엔진 플러그인(나이아가라)은 그렇게 한다 [추정]. 프로젝트 쪽 셰이더 경로 매핑 가능 여부는 미확인.

### 1-5. 이 프로젝트가 거리장 생성을 켜 두었는가
- r.GenerateMeshDistanceFields=True [확인: PRJ/Config/DefaultEngine.ini:50]. Config 폴더에서 DistanceField 가 들어간 줄은 이것 하나뿐이므로 GDF 관련 변수는 기본값이다 [확인: Grep 결과 1건].
- 루멘 + r.RayTracing=True 조합 [확인: DefaultEngine.ini:49,51,59]에서 GDF가 실제로 매 프레임 만들어지는지는 미확인. 루멘이 하드웨어 레이트레이싱만 쓰면 GDF를 안 만들 수도 있다 [추정]. 에디터에서 GDF 시각화 보기와 `stat` 계열로 확인해야 한다.

## 2. 얇은 물체와 대안

### 2-1. GDF의 한계
- 한 칸이 겹 0에서도 약 20cm [추정, 1-2 참고]. 책상 다리·의자 다리(폭 3~6cm)는 한 칸보다 훨씬 얇아서 "없거나 퉁퉁하게 부푼 기둥"이 된다 [추정].
- 작은 메시는 아예 GDF에서 빼 버린다: r.AOGlobalDistanceField.MinMeshSDFRadius(월드 반지름), MinMeshSDFRadiusInVoxels(칸 수 기준) [확인: GlobalDistanceField.cpp:197-211. 기본값은 읽지 않음, 미확인].
- 영향 범위는 4칸(GGlobalDistanceFieldInfluenceRangeInVoxels=4) [확인: GlobalDistanceField.cpp:464]. 겹 0에서 약 80cm 안쪽에서만 거리값이 의미 있다 [추정].
- 지형은 두께 4칸 껍질로 들어가므로 가파른 절벽·동굴 천장 아래는 부정확할 수 있다 [추정].
- 움직이는 물체: 스켈레탈 메시(캐릭터)와 Chaos 파편은 메시 거리장이 없어 GDF에 반영되지 않는다 [추정: 엔진 일반 지식, 이번에 열어 보지 않음].

### 2-2. 대안 (가능성 평가)
| 대안 | 정확도 | 비용 | 평가 |
|---|---|---|---|
| 지형 높이 지도 직접 사용 | 지형은 정확 | 아주 낮음 | 가장 추천. 이 프로젝트가 이미 칸별 지형 높이 배열을 갖고 있다: FTDFluidTerrainCollisionMask::SetCellElevation / IsCellSolid(CellX, CellY, BaseZCm, AboveCm) [확인: PRJ/Source/TDGame/FluidFX/TDFluidTerrainCollisionMask.h:6-13]. 3D에서는 "칸 높이 < 지형 높이면 막힘" 한 줄로 끝난다 [추정]. 최근 커밋도 지형 높이 캡처를 도입했다 [확인: 세션 git log 564bcb2a 제목] |
| 충돌 단순 도형(상자·캡슐·구)을 직접 복셀에 찍기 | 도형을 제대로 만들면 다리 하나도 표현 | 정적 물체는 구역이 로드될 때 한 번만 | 책상 다리는 캡슐 4개로 정확히 표현 가능. 이 프로젝트에 방향 있는 상자 장애물 변환이 이미 있다: UTDFluidObstacleComponent::BuildObstacle / MakeOrientedBoxObstacle, BoxHalfExtentsCm 은 이미 3축 FVector [확인: PRJ/Source/TDGame/FluidFX/TDFluidObstacleComponent.h:9-26]. 3D로 가는 길은 짧다 [추정] |
| 위·아래 두 방향 깊이 캡처 | 층이 두 개뿐이라 "위 면~아래 면 사이는 전부 막힘"으로 채움 | 장면 캡처 두 번(정적이면 한 번) | 책상 아래 빈 공간이 막힘으로 오판된다. 상자·바위·나무 몸통에는 좋고 책상·의자에는 부적합 [추정] |
| 피직스 질의(OverlapMulti 등)를 구역 로드 때 격자 단위로 | 충돌 도형 그대로 | CPU 비용, 한 번만 | 정적 물체 굽기 용도로 가능 [추정, 이번에 엔진 소스 미확인] |
| GDF를 "큰 덩어리용 보조"로 | 20cm급 | 공짜(이미 있다면) | 상자·바위·벽 같은 큰 물체의 근사로 충분. 얇은 물체는 위의 도형 찍기가 덮어쓰는 구조 [추정] |

## 3. Chaos 파괴 파편(Geometry Collection)

### 3-1. 위치·속도·크기를 얻는 방법 (확인된 것)
1. 게임 스레드, 이 프로젝트가 이미 쓰는 방법: UGeometryCollectionComponent::GetCurrentTransformsFromIndices(const TArray<int32>&) 로 필요한 조각 번호만 변환(위치·회전) 배열을 받는다 [확인: ENG/Source/Runtime/Experimental/GeometryCollectionEngine/Public/GeometryCollection/GeometryCollectionComponent.h:1359]. 사용처: PRJ/Source/TDGame/FluidFX/TDFluidFragmentEmitterActor.cpp:377 과 389(조각 위치 캡처, 조각 포즈 캡처) [확인]. 말단 조각 번호는 GetRestCollection() 로 모은다 [확인: 같은 파일:337-361].
2. 속도: 이 프로젝트는 위치 이력 링 버퍼 FTDFluidFragmentPositionHistory 로 지난 프레임 위치를 보관하고 GetLaggedFrame 으로 읽는다 [확인: TDFluidFragmentEmitterActor.h:19-31]. 속도는 위치 차분으로 얻는다 [추정].
3. 크기: FTDFluidFragmentLeafPoses 가 조각별 지역 경계 상자(LocalBounds)를 갖는다 [확인: TDFluidFragmentEmitterActor.h:34-38]. 원 메시 경계에서 얻는다.
4. 부서지는 순간 이벤트: 컴포넌트의 OnChaosBreakEvent 와 OnRootBreakEvent 델리게이트, 켜는 함수 SetNotifyBreaks(bool) [확인: GeometryCollectionComponent.h:53, 1228, 1269, 1278]. 이벤트 구조체(FChaosBreakEvent) 안의 필드는 열지 않았다(미확인).
5. 나이아가라 쪽 두 길:
   - 지오메트리 컬렉션 데이터 인터페이스(NiagaraDataInterfaceGeometryCollection): 조각별 월드 변환(조각당 float4 3개), 역변환, 이전 프레임 변환, 경계(float4)를 GPU 읽기 버퍼로 올린다 [확인: ENG/Plugins/Experimental/ChaosNiagara/Source/ChaosNiagara/Public/NiagaraDataInterfaceGeometryCollection.h:16-93]. 자체 컴퓨트 셰이더에서 쓰려면 이 버퍼를 직접 받는 연결이 필요 [추정].
   - Chaos 파괴 데이터 인터페이스(UNiagaraDataInterfaceChaosDestruction): 충돌·파괴·궤적 세 종류 자료원을 갖고 GPU용 자료 구조(FNiagaraDIChaosDestruction_GPUData)가 있다 [확인: .../Classes/NiagaraDataInterfaceChaosDestruction.h:98-250, 295-300. 세부 동작은 미확인].
   - 둘 다 Plugins/Experimental 아래의 ChaosNiagara 플러그인이다 [확인: 경로]. 실험 단계라 5.8 이후 변경 위험이 있다 [추정]. 이 프로젝트에서 플러그인이 켜져 있는지는 열어 보지 않았다(미확인).
6. 프로젝트의 나이아가라 데이터 채널 길: FTDFluidMassInputDataChannelReader::ReadPoints(…, bReadPreviousFrame, MaxPoints, DefaultRadiusCm, OutPoints)가 "점+반지름 목록"을 읽는다(이전 프레임 읽기 옵션까지 있음) [확인: PRJ/Source/TDGame/FluidFX/TDFluidMassInputDataChannelReader.h:9-24]. 파편 입력을 이 형식으로 통일하면 새 시스템이 기존 2D 입력 경로를 재사용할 수 있다 [추정].

### 3-2. 수백 개일 때 비용
- 게임 스레드 복사: GetCurrentTransformsFromIndices 는 FTransform 배열을 새로 만들어 돌려준다 [확인: 반환형 TArray<FTransform>]. 조각 300개면 배열은 약 300x(최대 96바이트)=약 30KB 수준이라 메모리는 무시할 만하다 [추정: FTransform 크기는 정밀도에 따라 다름]. 게임 스레드 시간은 측정하지 않았다(미확인). 이 프로젝트는 이미 "움직인 조각 수"를 센다(GetMovedLeafCount) [확인: TDFluidFragmentEmitterActor.h:86-87].
- GPU 쪽은 조각을 구(점+반지름) 목록으로 올리고 각 칸이 근처 조각만 보면 되므로 조각 수에 거의 선형이다. 조각 수백 개는 구조상 부담이 작다 [추정].

### 3-3. 파편이 거리장에 반영되는가
- 반영되지 않는다고 본다 [추정]. GDF는 메시 거리장이 있는 정적/이동 프리미티브를 넣는 구조이고 Geometry Collection 조각은 거리장을 만들지 않는다. 엔진 소스로 확인하지 못했다(미확인). 따라서 파편은 항상 별도 입력(구 목록)이 필요하다.

## 4. 캐릭터·무기 (이번에 엔진 소스 미확인, 전부 추정)

일반적인 방법 [추정]:
1. 게임 스레드에서 매 프레임 뼈·소켓 위치를 읽는다(손, 칼 손잡이·칼끝, 발, 몸통). 이전 프레임 위치도 같이 보관.
2. 칼 휘두름은 "이전 프레임 칼끝~이번 프레임 칼끝" 사이를 잇는 쓸림(스윕) 캡슐 하나로 표현하고, 속도 = (이번-이전)/시간 으로 구한다. 빠른 휘두름도 칸 사이를 건너뛰지 않는다.
3. 한 프레임에 캡슐 수십 개(캐릭터 5~8개 + 칼 1~2개)를 작은 구조화 버퍼(수 KB)로 올린다.
4. 컴퓨트 셰이더에서 캡슐 주변 칸에만 "연기 밀도 제거 + 캡슐 속도 방향으로 밀어내기"를 적용한다.
5. 프로젝트에는 같은 역할의 2D 입력 구성품이 이미 있다(이름만 확인): TDFluidFieldInputComponent, TDFluidTrackSourceComponent, TDFluidHullWakeComponent [확인: Glob 결과 파일명, 내용 미확인]. 3D에서는 이 구성품의 입력을 2D 판 대신 캡슐 목록으로 확장하면 된다 [추정].
6. 캐릭터는 메시 거리장이 GDF에 없으므로(2-1) 이 경로가 유일한 수단이다 [추정].

## 5. 3D 텍스처 컴퓨트 (이번에 엔진 소스 미확인, 전부 추정)

일반 사실 [추정]:
- RDG(렌더 그래프)에서 3D 텍스처는 FRDGTextureDesc::Create3D(크기, 포맷, 지움값, ShaderResource|UAV 플래그)로 만들고 컴퓨트 패스에 UAV로 넘긴다. 셰이더는 RWTexture3D 로 쓴다.
- R16F, RGBA16F, R32F 쓰기(typed UAV store)는 DX11 기능 수준 11 이상에서 일반적으로 된다. 같은 텍스처를 한 패스에서 읽고 쓰는 것(typed UAV load)은 포맷에 따라 추가 기능이 필요할 수 있어 위험하다. 안전한 설계는 "읽기용(SRV)과 쓰기용(UAV)을 서로 다른 텍스처로 두고 매 프레임 맞바꾸기(핑퐁)"다. RTX 2060 은 지원 범위가 넓은 편이지만 실제 능력은 RHI 포맷 능력 조회(GPixelFormats 의 Capabilities)로 실행 때 확인해야 한다.
- 엔진 안 사용 예: 나이아가라는 Grid3D 컬렉션과 렌더 타깃 볼륨 데이터 인터페이스, 볼륨 안개·루멘이 3D 텍스처 컴퓨트를 쓴다. 이번 조사에서 해당 소스를 열지 않았다(미확인). 프로젝트의 별도 모듈 TDFluidGpu 가 이미 RDG 컴퓨트를 쓰는지도 열지 않았다(미확인).
- 5.8 주의점 후보 [추정]: (a) RDG 가 3D 텍스처 부분 영역 갱신을 자동 추적하지 못하므로 희소 벽돌(brick) 설계는 "벽돌 아틀라스 하나 + 벽돌 표" 구조가 안전하다. (b) 비동기 컴퓨트·렌더 스레드 순서는 GDF 갱신 이후에 놓아야 같은 프레임 GDF를 읽는다. 이 순서 제어 수단(포스트 오파크 델리게이트 시점 등)은 1-4에서 확인한 공개 인터페이스를 쓴다.

## 6. 입력 수단 표

| 대상 | 수단 | 공개 인터페이스 여부 | 정확도 한계 | 비용 |
|---|---|---|---|---|
| 지형 | 높이 지도(칸별 높이)로 직접 판정 | 프로젝트 자체 코드(FTDFluidTerrainCollisionMask) [확인] | 높이 지도라 동굴·오버행 표현 불가 [추정]. 해상도는 지도 해상도 | 매우 낮음 |
| 지형 | GDF의 높이장 껍질 | 공개(Renderer 모듈 헤더) [확인] | 겹 0도 약 20cm, 두께 4칸 껍질 [추정] | 이미 만들어 둔 것을 읽기만 (GDF가 실제 생성 중인지는 미확인) |
| 정적 큰 물체(상자·바위·나무 몸통) | GDF 샘플 | 공개 [확인] | 약 20cm 칸, 작은 메시는 제외(MinMeshSDFRadius) [확인 변수, 기본값 미확인] | 낮음 |
| 정적 얇은 물체(책상 다리·의자) | 충돌 단순 도형(캡슐·상자)을 구역 로드 때 복셀에 굽기 | 프로젝트 자체 코드(방향 상자 장애물 변환 존재) [확인] + 충돌 도형 읽기(엔진 일반 API) [추정] | 도형 정확도에 의존. 충돌 도형이 거칠면 같이 거침 | 구역 로드 때 한 번, 런타임 0 [추정] |
| 정적 물체 대안 | 위·아래 2층 깊이 캡처 | 장면 캡처 컴포넌트(엔진 공개) [추정] | 속이 빈 물체(책상 아래)를 막힘으로 오판 [추정] | 캡처 2회(정적이면 1회) |
| 움직이는 캐릭터 몸 | 뼈·소켓 위치에서 만든 캡슐 목록 | 게임 스레드 공개 API [추정] | 캡슐 근사. 외투·망토 같은 형태 무시 | 매우 낮음 (캡슐 수십 개) |
| 칼 휘두름 | 이전~현재 칼끝을 잇는 쓸림 캡슐 + 속도 | 프로젝트 자체 [추정] | 프레임 간 보간 직선 근사 | 매우 낮음 |
| 파편(수백 개) | GetCurrentTransformsFromIndices 로 위치 → 점+반지름 목록 (또는 나이아가라 데이터 채널) | 공개 [확인: GeometryCollectionComponent.h:1359; 프로젝트 사용 TDFluidFragmentEmitterActor.cpp:377] | 구 근사. 조각 모양 무시 | 게임 스레드 복사 소량 (시간 미측정), GPU 선형 |
| 파편(대안) | 나이아가라 GeometryCollection 데이터 인터페이스의 GPU 버퍼 | 실험 플러그인 ChaosNiagara [확인: 경로] | 플러그인 활성·5.8 호환 미확인 | 복사 없이 GPU 직접 [추정] |
| 파괴 순간 | OnChaosBreakEvent / SetNotifyBreaks | 공개 [확인: GeometryCollectionComponent.h:1228,1269] | 이벤트 필드 내용 미확인 | 이벤트당 소량 |
| 파편이 거리장에 반영 | 반영 안 됨으로 판단 | — | 미확인 | — |

## 7. 이 조사로 얻은 설계 시사점 (추정 포함)

1. 입력을 한 형식으로 통일하는 것이 가장 단순하다: "점·구·캡슐·방향 상자 목록 + 지형 높이 지도". 파편, 캐릭터, 칼, 책상 다리가 모두 이 목록으로 들어가고, 지형은 높이 지도로 들어간다. GDF는 큰 물체 보조 또는 첫 시험용 임시 수단으로 둔다 [추정].
2. 플레이어 주변만 계산하는 방식과 GDF의 겹 구조는 궁합이 좋다. GDF도 카메라 중심 이동 창이라 시뮬레이션 창이 따라 움직일 때 같은 방식(새로 드러난 쪽만 갱신)을 쓴다 [확인: GlobalDistanceField.cpp:116-123, 765-776].
3. 3개 시점 중 1인칭은 카메라와 플레이어가 같아 창 중심이 쉽다. 탑다운은 카메라가 800cm 떨어져 있으므로 창 중심은 카메라가 아니라 플레이어(또는 화면 중심 지면 점)로 잡아야 한다 [추정].

## 8. 다음 단계에서 열어 확인할 목록 (이번 예산으로 못 한 것)

1. GDF가 이 프로젝트 설정(루멘+레이트레이싱)에서 실제로 매 프레임 생성되는지: 에디터에서 GDF 시각화·통계 확인.
2. r.AOGlobalDFClipmapDistanceExponent, MinMeshSDFRadius, MinMeshSDFRadiusInVoxels 의 기본값 (GlobalDistanceField.cpp:150-160, 197-211).
3. 겹 "Extent"가 절반 폭인지 (GlobalDistanceField.cpp:1140-1150 근처).
4. GlobalDistanceFieldShared.ush 의 샘플 함수 이름과 프로젝트 셰이더에서 /Engine/Private 포함 가능 여부, TDFluidGpu·TDFogScattering 의 Build.cs 에 Renderer 모듈 유무.
5. FChaosBreakEvent 필드, ChaosNiagara 플러그인의 프로젝트 활성 여부(TDGame.uproject).
6. RDG 3D 텍스처 UAV 예제: 엔진의 VolumetricFog.cpp, 나이아가라 Grid3D(NiagaraDataInterfaceGrid3DCollection), 포맷 능력 조회 방법(EPixelFormatCapabilities).
7. 캐릭터 뼈·소켓 읽기와 물리 에셋 몸체 목록(USkinnedMeshComponent, UPhysicsAsset) 확인, 프로젝트의 TDFluidFieldInputComponent / TDFluidTrackSourceComponent / TDFluidHullWakeComponent 본문.
8. GetCurrentTransformsFromIndices 의 게임 스레드 시간(조각 100·300·1000개) 실측.

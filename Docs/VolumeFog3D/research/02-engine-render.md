# 02. 언리얼 5.8 렌더 경로 조사: "실시간으로 바뀌는 3D 안개 밀도"를 화면에 그리는 길

작성: 읽기 전용 조사 보조 / 2026-11-12 / 엔진 소스 `C:/Program Files/Epic Games/UE_5.8/Engine` 직접 확인.
표기: [확인: 경로:줄] = 원문을 직접 열어 봄, [추정] = 소스를 근거로 한 내 추론, 미확인 = 열어 보지 못함.
실측(밀리초) 숫자는 이번 조사에 없다. 이번은 소스 읽기만 했고 빌드·에디터 실행은 하지 않았다.

## 0. 먼저 한 장으로 보는 결론

```
[엔진 기본 볼륨 안개]  카메라 앞을 "화면 방향 벽돌 격자"로 자른다 (froxel = 카메라 절두체 칸)
   칸은 카메라에 붙어 움직인다 → 세상에 고정된 격자가 아니다
   멀수록 칸이 커진다 (가까이 13cm 폭, 4m 앞은 67cm 폭)
   결과를 지난 프레임과 90% 섞는다 → 밀리고 퍼지는 움직임이 0.2~0.7초 늦게 따라온다

[이질 볼륨 HV]       카메라에서 레이를 쏴 재질을 한 걸음마다 계산 (베타 기능, 이미 프로젝트가 사용 중)
[희소 볼륨 텍스처 SVT] 미리 구워 둔 파일을 스트리밍하는 용도. 런타임에 GPU가 써 넣는 길은 못 찾음
[직접 그리기]        씬 뷰 확장으로 우리 레이마칭 패스를 넣는 길은 열려 있고, 이 프로젝트가 이미 한 번 검증함
```

한 줄 판단: 시뮬레이션은 세상에 고정된 우리 격자(희소 격자)에서 돌리고, 화면에 그리는 길은 (가) 엔진 볼륨 안개에 재질로 밀어 넣기, (나) HV, (다) 우리 레이마칭 패스 중에서 고르게 된다. 우리 패스가 품질과 반응성은 가장 좋지만 조명·그림자를 직접 구현해야 해서 가장 어렵다. 상세는 맨 아래 비교 표.

---

## 1. 볼륨 안개(Volumetric Fog)의 해상도·콘솔 변수·반응 지연

### 1-1. 실제 콘솔 변수와 기본값 (5.8)
출처: `Renderer/Private/VolumetricFog.cpp` 94~233줄 [확인].

| 변수 | 기본값 | 뜻 |
|---|---|---|
| `r.VolumetricFog` | 1 | 기능 켬/끔 |
| `r.VolumetricFog.GridPixelSize` | 16 | 칸 한 개의 화면상 가로·세로 픽셀 |
| `r.VolumetricFog.GridSizeZ` | 64 | 깊이 방향 칸 수 |
| `r.VolumetricFog.DepthDistributionScale` | 32 | 깊이 칸을 가까운 쪽에 얼마나 몰아 줄지 |
| `r.VolumetricFog.HistoryWeight` | 0.9 | 지난 프레임 값을 얼마나 섞는지 |
| `r.VolumetricFog.TemporalReprojection` / `Jitter` | 1 / 1 | 시간 누적 켬 / 칸 안에서 표본 위치 흔들기 |
| `r.VolumetricFog.HistoryMissSupersampleCount` | 4 | 지난 값이 없는 칸에 쓰는 표본 수(1~16) |
| `r.VolumetricFog.InjectShadowedLightsSeparately` | 1 | 그림자 있는 국소 조명을 따로 주입 |
| `r.VolumetricFog.ConservativeDepth` | 1 (실험) | 깊이 버퍼로 계산을 줄임 |
| `r.VolumetricFog.GridCenterOffsetFromDepthBuffer` | 0.5 | 표면에 닿은 칸을 앞으로 밀어 깜빡임 감소 |
| `r.VolumetricFog.VoxelizationSlicesPerGSPass` | (읽기 전용) | 재질 복셀화 때 한 번에 그리는 슬라이스 수 |

최대 거리는 콘솔 변수가 아니라 높이 안개 컴포넌트 값 `VolumetricFogDistance` 기본 6000cm(60m) [확인: Engine/Private/Components/ExponentialHeightFogComponent.cpp:112]. 시작 거리(`VolumetricFogStartDistance`) 기본값은 이번에 못 찾았다(미확인, 아래 계산에서는 0으로 가정). 근평면은 기본 10cm로 가정 [추정].

### 1-2. 품질 등급별 값
출처: `Engine/Config/BaseScalability.ini` [확인: 143·178줄 끔, 213~216, 251~254, 289~292줄]. 등급 이름은 `[ShadowQuality@N]` 구역이다.

| 등급(ShadowQuality) | 볼륨 안개 | 픽셀 크기 | Z 칸 | 1920x1080 총 칸 수 | 미지원 표본 수 |
|---|---|---|---|---|---|
| 0 낮음, 1 중간 | 꺼짐 | - | - | - | - |
| 2 높음 | 켜짐 | 16 | 64 | 120x68x64 = 522,240 | 4 |
| 3 에픽 | 켜짐 | 8 | 128 | 240x135x128 = 4,147,200 | 4 |
| Cine 시네마틱 | 켜짐 | 4 | 128 | 480x270x128 = 16,588,800 | 16 |

칸 수 계산 근거: 격자 가로·세로는 해상도를 픽셀 크기로 나눠 올림한다 [확인: Renderer/Private/Froxel/FroxelGridUtils.cpp:20~23]. 텍스처 형식은 `PF_FloatRGBA`(칸당 8바이트) 3D 텍스처 [확인: VolumetricFog.cpp:1401~1407]. 따라서 한 장의 크기는 높음 약 4MB, 에픽 약 31.6MB(위 칸 수 x 8바이트, 내가 계산). 엔진이 이런 3D 텍스처를 몇 장 동시에 쓰는지는 세지 않았다(미확인). 프로젝트 `Config/DefaultEngine.ini`에는 `r.VolumetricFog` 관련 오버라이드가 없다 [확인: grep 결과 TDFogScattering 줄만 존재]. 즉 등급 기본값을 그대로 쓰고 있다.

### 1-3. 시간 누적이 만드는 반응 지연
- 섞는 식: `lerp(새 값, 지난 값, HistoryAlpha)` [확인: Shaders/Private/VolumetricFog.usf:1175~1179], `HistoryAlpha`는 기록이 유효하면 0.9 [확인: VolumetricFog.cpp:327].
- 즉 매 프레임 새 값은 10%만 반영된다. 목표 값의 90%에 이르는 데 ln(0.1)/ln(0.9) = 21.9프레임, 절반에 이르는 데 6.6프레임 (내가 계산). 

| 프레임률 | 절반 도달 | 90% 도달 |
|---|---|---|
| 60fps | 0.11초 | 0.36초 |
| 30fps | 0.22초 | 0.73초 |

- 의미: 칼을 휘둘러 안개를 밀어낼 때 모양이 0.1~0.4초 늦게 따라오고 끝이 흐려진다. 가볍게 휘둘러 지나가는 짧은 동작은 거의 다 뭉개질 수 있다 [추정].
- 이 지연은 재질로 넣은 밀도에도 똑같이 적용된다(복셀화 결과가 같은 합성식을 지난다) [추정: 복셀화 출력 VBufferA/B가 위 integration 입력이라는 구조에서 유도].
- 줄이는 방법: `HistoryWeight`를 낮추면 반응은 빨라지고 대신 깜빡임·노이즈가 늘어난다 [확인: 변수 설명 "tradeoff between visible jittering and responsiveness", VolumetricFog.cpp:153~154]. 값 0으로 끄면 `TemporalReprojection` 이득이 없어지고 해상도 한계가 그대로 보인다.

---

## 2. 볼륨 도메인 재질로 3D 밀도를 볼륨 안개에 넣기

### 2-1. 어떤 물체가 복셀화되는가
- 재질 도메인이 `MD_Volume`인 프리미티브만 `VolumetricMeshBatches`에 들어간다 [확인: Renderer/Private/SceneVisibility.cpp:1950~1970, 2618~2632; VolumetricFogVoxelization.cpp:575~579, 650].
- 그리는 방식: 카메라를 향한 사각형을 슬라이스마다 반복해서 그린다. 파티클 스프라이트는 그대로, 그 밖의 모든 메시(스태틱 메시 포함)는 메시 모양을 버리고 "카메라를 향한 사각형 한 장"으로 바꿔 그린다 [확인: VolumetricFogVoxelization.cpp:652~670].
- 모양 제한: 프리미티브의 경계 구(`PRIMITIVE_SPHERE_MODE`) 또는 오브젝트 경계 상자(`OBJECT_BOX_MODE`) 안쪽만 유효 [확인: Shaders/Private/VolumetricFogVoxelization.usf:265~290]. 메시 형태는 안개 모양과 무관.
- 그릴 슬라이스 수는 경계 구의 깊이 범위로 결정, 한 번에 `VoxelizationSlicesPerGSPass`개씩 여러 번 [확인: VolumetricFogVoxelization.cpp:672~685]. "`@todo` 가장 큰 파티클 크기로 슬라이스 수 계산" 주석이 있어 경계 구가 크면 과하게 그린다고 엔진이 인정한다 [확인: :673].
- 거리 컷: 카메라와 프리미티브 사이가 `VolumetricFogDistance + 반지름` 보다 멀면 건너뜀 [확인: :779~782].
- HV로 표시된 프리미티브는 이 길에서 빠진다 [확인: :769~774].

### 2-2. 재질 안에서 무엇을 읽는가 (픽셀 셰이더가 칸마다 한 번 실행)
- 소광(빛을 가리는 정도): 서브서피스 데이터 raw의 R 채널, 0~65000 [확인: VolumetricFogVoxelization.usf:29~40].
- 알베도: 베이스 컬러 [확인: :55~66]. 발광: 이미시브 [확인: :43~52]. 산란 = 알베도 x 소광 [확인: :328]. 출력 두 장: VBufferA(산란, 소광), VBufferB(발광) [확인: :343~344].
- 월드 위치가 재질 입력으로 들어와 칸마다 다른 값을 줄 수 있다 [확인: :313, `SvPositionToTranslatedWorld`].

### 2-3. 3D 텍스처 샘플이 되는가
- 재질 컴파일러에 3D 텍스처 타입(`MCT_VolumeTexture`)이 있고 `UTextureRenderTargetVolume`이 엔진에 있다 [확인: Engine/Private/Materials/MaterialExpressions.cpp:4253, 16556; Engine/Classes/Engine/TextureRenderTargetVolume.h:21]. 일반 재질 픽셀 셰이더이므로 볼륨 도메인에서도 3D 텍스처 샘플은 가능할 것이다 [추정: 이번에 복셀화 패스에서의 실제 샘플 성공은 에디터에서 돌려 보지 않았다 → 미검증].
- 런타임 볼륨 렌더 타깃을 재질에 넘기는 방법: 동적 머티리얼 인스턴스에 텍스처 파라미터로 넣는다 [추정: 이 프로젝트가 2D 시뮬 RT를 같은 방식으로 슬롯 MID에 넘긴다 → `TDFluidVolumeDriverComponent.cpp`:338~347, 564]. RDG 텍스처를 직접 재질에 넘기는 공개 길은 이번에 못 찾았다(미확인). 우리 컴퓨트 패스가 결과를 볼륨 렌더 타깃(UAV 가능 여부는 미확인)에 쓰거나 복사하는 형태가 현실적이다 [추정].
- 프로젝트 선례: 구동 컴포넌트가 `bRenderFogVolume` 경로(볼륨 도메인 재질 메시를 안개에 넣기)를 이미 갖고 있다 [확인: TDFluidVolumeDriverComponent.cpp:154, 201~216]. 단 이 경로가 3D 텍스처가 아니라 평면 기반이라는 것이 사용자 설명이다(코드로 직접 구분하진 않음).

### 2-4. 비용 구조
- 격자는 카메라 기준 칸이므로 우리 시뮬(세상 고정)의 값을 칸마다 샘플링해 옮기는 형태가 된다 [추정]. 칸 수가 높음 522k·에픽 4.1M이므로 샘플링 자체는 가볍고, 비용의 핵심은 사각형 그리기 반복(경계 구 슬라이스 수)과 이어지는 조명 주입·적분이다 [추정]. 실측 없음(미확인).
- 가장 큰 단점: 해상도와 칸 모양을 우리가 정할 수 없다(1-2, 7절). 시뮬이 5cm 격자라도 높음 등급이면 13cm x 60cm 칸으로 뭉개진다.

---

## 3. 조명: 볼륨 안개가 받는 빛, 자기 그림자

`Shaders/Private/VolumetricFog.usf` [확인] 기준.
- 방향광: 가상 그림자 맵(VSM) 샘플 [확인: :535~538, :955~958], 캐스케이드 그림자 포함(코드 일부만 확인). 구름 그림자 맵도 곱해짐 [확인: :973].
- 국소 조명: 점·스포트·사각 조명을 칸마다 주입, 그림자 있는 조명은 `InjectShadowedLightsSeparately=1`에서 따로 처리 [확인: VolumetricFog.cpp:102~108, 904~1053]. 조명 함수(light function) 지원 파일 `VolumetricFogLightFunction.cpp` 존재 [확인].
- 간접광: 루멘 GI 번역 볼륨(`LumenTranslucencyVolume`)을 읽는 분기 `bLumenGI` [확인: :25~26, :413]. 하늘빛 확산(SkyLight) 분기 [확인: :997]. 거리장 기반 하늘 가림(`HemisphereConeTraceAgainstGlobalDistanceField`) [확인: :651~722].
- 국소 안개 볼륨 주입: `LocalFogVolume`이 볼륨 안개에 들어간다 [확인: VolumetricFog.usf:35, :209; 변수 `r.LocalFogVolume.RenderIntoVolumetricFog=1`, `MaxDensityIntoVolumetricFog=0.01` LocalFogVolumeRendering.cpp].
- 자기 그림자: 볼륨 안개 쪽에 "안개가 안개를 가린다"는 계산(빛 방향으로 밀도를 적분)은 구름 그림자 맵 외에 찾지 못했다 [추정: grep 결과에 OpticalDepth는 구름 경로만 등장]. 즉 우리가 넣은 밀도가 짙은 덩어리 뒤쪽을 어둡게 만들지는 않는다. 그림자가 필요한 입체감(연기 덩어리 안쪽이 어두움)이 중요하면 HV 또는 직접 그리기 쪽이 유리하다.
- 반대 방향(안개가 바닥·물체에 그림자를 드리움): HV 쪽에 `r.HeterogeneousVolumes.Shadows` 기본 0 [확인: HeterogeneousVolumes.cpp:33~35]. 볼륨 안개 재질 밀도는 이 경로가 없다 [추정].

---

## 4. 헤테로지니어스 볼륨(HV)

### 4-1. 상태
- 프로젝트 설정 이름이 "Heterogeneous Volumes (Beta)" → 베타 [확인: Engine/Classes/Engine/RendererSettings.h:1188]. `r.HeterogeneousVolumes` 기본 1 [확인: HeterogeneousVolumes.cpp:19~21].
- 지원 조건: 기능 수준 SM5 이상, 포워드 셰이딩이 아닐 것 [확인: RenderCore/Private/RenderUtils.cpp:1551~1557]. 개발 PC(RTX 2060, 6GB)에서는 충족 [추정].
- 프로젝트가 이미 쓰고 있다: `UHeterogeneousVolumeComponent`를 만들어 해상도 256x256x128(기본), 높이 1000cm 등으로 배치 [확인: Source/TDGame/FluidFX/TDFluidVolumeDriverTypes.h:48~60; TDFluidVolumeDriverComponent.cpp:370~382]. 교훈 L-editor-80: HV 라이브 셰이딩에서 재질의 `ObjectPositionWS`는 (0,0,0), `ObjectBounds`는 거대값 [확인: Docs/Lessons/editor.md:706~710]. Preshading 1 전환은 에디터 충돌 → 미해결.

### 4-2. 런타임에 갱신되는 3D 텍스처를 넣을 수 있나
- 컴포넌트는 `UMeshComponent`이고 재질 슬롯 1개, 볼륨 해상도·프레임·스텝 계수 등 속성을 가진다 [확인: Engine/Classes/Components/HeterogeneousVolumeComponent.h:19~83]. 밀도는 재질이 만든다(희소 볼륨 텍스처 입력이 표준 입력).
- 라이브 셰이딩에서는 재질을 레이 한 걸음마다 평가한다 [추정: 파일명 `HeterogeneousVolumesLiveShadingPipeline.usf`와 위 교훈의 설명]. 따라서 재질에 3D 텍스처 샘플을 넣어 런타임 볼륨 렌더 타깃을 읽는 것은 가능해 보인다 [추정, 미검증]. 현재 프로젝트는 2D 시뮬 결과를 재질에서 높이로 확장해 쓰는 방식(사용자 설명)이고, 진짜 3D 입력을 시험한 기록은 이번에 확인 못 했다.
- `Preshading` 모드는 재질을 미리 한 볼륨에 구워 낸다(기본 0, 꺼짐) [확인: HeterogeneousVolumes.cpp:187~192]. 프로젝트에서는 충돌이 있어 못 쓴다(위 교훈).

### 4-3. 비용 수준
- 기본값: 최대 걸음 수 512, 최대 추적 거리 30000cm, 다운샘플 1, 조명 캐시 모드 2 [확인: HeterogeneousVolumes.cpp:166~175, 56, 270]. 화면 채우는 레이마칭이라 해상도·걸음 수에 비례 [추정]. 실측 없음(미확인). 프로젝트 쪽 `OffscreenCost` 시험 이름이 있으나 숫자는 열어 보지 않았다(미확인).
- 희소 복셀 렌더링 옵션(`r.HeterogeneousVolumes.SparseVoxel`) 기본 0, 꺼짐 [확인: :235~237]. 엔진이 HV 안에서 희소 복셀 가속을 갖고 있다는 뜻이지만 기본 꺼짐이므로 성숙도 낮다 [추정].

### 4-4. 그림자·정렬 문제
- 그림자: 환경에 드리우기 `r.HeterogeneousVolumes.Shadows` 기본 0, 형식 AVSM(적응 볼륨 그림자 맵)/복셀 격자 선택 [확인: :33~35, :101~106]. 볼륨 자기 그림자는 조명 캐시 `LightingCache`로 처리, AVSM 사용은 기본 0 [확인: :270~285].
- 반투명 정렬: 볼륨 간에는 중심점 기준 깊이 정렬 `DepthSort=1` [확인: :296~298]. 즉 볼륨 두 개가 겹치면 정렬이 틀릴 수 있다 [추정]. 반투명(파티클·유리)과의 합성: 기본은 "반투명 앞에" 그림, "반투명 뒤" 옵션은 프로젝트 설정 "Composite with Translucency" 필요 [확인: :63~71]. 조각이 날아다니는 반투명 파티클과 섞이면 순서가 어긋날 수 있다 [추정].
- 볼륨 안개·높이 안개를 HV에 적용하는 옵션이 있다(기본 켬) [확인: :303~315]. 즉 HV와 볼륨 안개를 동시에 쓰면 HV 뒤 배경에 안개가 이중 적용될 소지가 있다 [추정].

---

## 5. 희소 볼륨 텍스처(SVT)

- 구조: 큰 가상 볼륨을 "페이지 표(page table, 11|11|10 비트로 좌표를 묶은 목록)" + "실제 데이터가 든 물리 타일 텍스처"로 나눈다. 비어 있는 곳은 타일이 없다 [확인: Engine/Classes/SparseVolumeTexture/SparseVolumeTexture.h:40~74]. 상태 헤더에 AABB 최소·최대, 대체값(비어 있을 때 값)이 있다 [확인: :40~47].
- 클래스: `USparseVolumeTexture`(추상), `USparseVolumeTextureFrame`(한 프레임), `UStreamableSparseVolumeTexture`(스트리밍 대상, 정적·애니메이션 파생) [확인: :307, :364, :450, :614, :637].
- 쓰기 방식: 프레임 만들기는 CPU 쪽 `FTextureData`를 `Initialize`/`AppendFrame`/`EndInitialize`로 넣는 편집·임포트용 흐름 [확인: :388, :524~532]. 타일 업로드는 `FTileUploader`가 CPU 메모리를 GPU 타일 텍스처에 올린다 [확인: Private/SparseVolumeTexture/SparseVolumeTextureUpload.h]. 물리 타일 텍스처에 UAV 플래그가 있으나 쓰는 쪽은 엔진 내부 스트리밍이다 [확인: SparseVolumeTextureTileDataTexture.cpp:205].
- 스트리밍 설정이 모두 요청·대역폭·큐 개수에 관한 것이다 [확인: SparseVolumeTextureStreamingManager.cpp:35~107의 `r.SparseVolumeTexture.*`].
- 니아가라 쪽: SVT를 읽는 데이터 인터페이스와 오프라인 굽기(Baker)가 있다 [확인: Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraDataInterfaceSparseVolumeTexture.h, NiagaraBakerOutputSparseVolumeTexture.h].
- 결론: 매 프레임 GPU 시뮬이 결과를 써 넣는 공개 도구는 찾지 못했다. 스트리밍 전용, 오프라인 구움 중심이다. 그대로는 부적합. 단 "페이지 표 + 타일 풀" 구조 자체는 우리가 만들 희소 격자의 모델로 쓸 만하다 [추정]. SVT 타일 한 변 크기 등 세부는 열어 보지 않았다(미확인).

---

## 6. 직접 그리기: 씬 뷰 확장으로 우리 레이마칭 패스

### 6-1. 끼워 넣을 수 있는 지점
출처: `Engine/Public/SceneViewExtension.h` [확인: 145~232줄].
- `PreRenderViewFamily_RenderThread` / `PreRenderView_RenderThread`: 프레임 시작. 시뮬 컴퓨트 패스를 돌리기 좋음(RDG 빌더를 받음).
- `PreInitViews`, `PreRenderBasePass_RenderThread(bDepthBufferIsPopulated)`: 불투명 그리기 전.
- `PostRenderBasePassDeferred_RenderThread(GraphBuilder, View, RenderTargets, SceneTextures)`: 기본 패스 뒤. 장면 깊이·G버퍼 접근 가능.
- `PrePostProcessPass_RenderThread`: 후처리 직전.
- `SubscribeToPostProcessingPass(Pass, ...)`: 단계 선택 `BeforeDOF, AfterDOF, TranslucencyAfterDOF, SSRInput, ReplacingTonemapper, MotionBlur, Tonemap, FXAA, SMAA, VisualizeDepthOfField` [확인: SceneViewExtension.h:122~136]. 후처리 콜백은 장면 색을 받아 새 색을 돌려주는 형태.

### 6-2. 이 프로젝트에서 검증된 훅
`Plugins/TDFogScattering` (런타임 모듈, 로딩 단계 PostConfigInit [확인: TDFogScattering.uplugin:17~19]).
- `SubscribeToPostProcessingPass`에서 `EPostProcessingPass::BeforeDOF`만 골라 콜백을 등록 → 검증됨 [확인: TDFogScatteringViewExtension.cpp (`Pass != EPostProcessingPass::BeforeDOF` 분기)].
- 장면 깊이: `Inputs.SceneTextures.SceneTextures.GetUniformBuffer()->GetContents()->SceneDepthTexture` → 검증됨 [확인: TDFogScatteringPasses.cpp:187~194]. 컴퓨트 패스(`FComputeShaderUtils::AddPass`)로 다운샘플·업샘플·합성 [확인: Passes.cpp:240, 282, 332].
- 엔진 비공개 헤더 접근: `Build.cs`에서 Renderer의 `Private`·`Internal` 경로를 인클루드 경로에 추가하고 `FViewInfo`를 직접 읽는다 [확인: TDFogScattering.Build.cs; TDFogScatteringMedia.cpp:97~113에서 `ViewInfo.VolumetricFogResources.IntegratedLightScatteringTexture`와 `VolumetricFogStartDistance`를 읽음]. 따라서 이 프로젝트는 엔진 볼륨 안개의 적분 결과 텍스처를 읽어 쓰는 선례도 갖고 있다. 단 엔진 버전을 올리면 깨질 수 있는 결합이다 [추정].
- 검증 안 된 것: 기본 패스 직후 훅(`PostRenderBasePassDeferred`)과 `PreRenderView`에서의 컴퓨트 시뮬은 이 플러그인이 쓰지 않는다(미검증). 시뮬은 오히려 `TDFluidGpuSolverProxy` 계열이 이미 GPU로 돌리고 있으니 그 쪽 패턴을 재사용할 수 있다 [추정, 코드 확인 안 함].

### 6-3. 난이도 (내 평가 [추정])
| 요소 | 난이도 | 이유 |
|---|---|---|
| 깊이 버퍼 읽기 | 쉬움 | 위 선례. 레이를 물체 앞에서 멈추기 위해 사용 |
| 방향광 방향·색 | 중간 | 공개 API로 얻으려면 장면의 조명 목록 접근 필요. 프로젝트는 비공개 `FViewInfo` 접근 선례가 있음 |
| 엔진 그림자(VSM·캐스케이드) 샘플 | 어려움 | 엔진 내부 유니폼과 매개변수 묶음이 필요, 버전 종속 |
| 루멘 간접광 | 어려움 | 번역 볼륨 텍스처를 읽어야 함. 볼륨 안개가 쓰는 것을 재사용할 수는 있음 |
| 자체 그림자(밀도를 빛 방향으로 적분) | 중간 | 우리 격자에서 우리가 직접 가능, 자기 그림자도 이쪽에서만 쉽게 얻음 |
| 반투명과의 순서 | 중간 | `BeforeDOF` 위치 특성상 이미 그려진 반투명 위에 덮임 [추정] |
| 불투명 표면의 안개 가림(깊이 기준 클립) | 쉬움 | 깊이 비교 |

현실적 절충: 방향광 한 개와 하늘색 환경광은 단순 모델(우리 셰이더에서 직접 계산)로, 엔진 그림자는 처음에는 생략하고 우리 자체 그림자만 두는 길이 가장 안정적이다 [추정].

---

## 7. 시점별로 볼륨 안개 칸이 지면 근처에서 얼마나 큰가

### 7-1. 소스 식 그대로
`CalculateGridZParams` [확인: RenderCore/Public/RenderUtils.h:721~740]
```
N = NearPlane + 9.5          (NearOffset = 0.095*100)
F = FarPlane  (= VolumetricFogDistance)
S = DepthDistributionScale
O = (F - N * 2^(GridSizeZ / S)) / (F - N)
B = (1 - O) / N
slice = log2(z * B + O) * S                      [확인: Shaders/Private/Common.ush:2427~2430]
z(slice) = (2^(slice / S) - O) / B               [확인: Common.ush:2437~2441]
```
XY 폭(가정: 가로 시야각 90도, 1920x1080) = GridPixelSize x 2 x z x tan(45°) / 1920 = GridPixelSize x z / 960.
Z 두께 = z(slice+0.5) - z(slice-0.5) (내가 정의한 계산).

대입값: NearPlane=10(가정), N=19.5, F=6000, S=32.
- 높음(Z=64): O = (6000 - 19.5·2^(64/32)) / (6000 - 19.5) = (6000 - 78) / 5980.5 = 0.99022, B = (1 - 0.99022)/19.5 = 0.000502. 마지막 슬라이스 깊이 = 6000(식 검증됨, 내 파이썬 재계산).
- 에픽(Z=128): O = (6000 - 19.5·2^4) / 5980.5 = 0.95109, B = 0.002508.

### 7-2. 칸 크기 표 (센티미터, 1920x1080, 가로 시야각 90°, 내 파이썬 계산)

| 깊이 z | 높음 XY 폭 x Z 두께 | 에픽 | 시네마틱(픽셀4) |
|---|---|---|---|
| 300 | 5.0 x 49.3 | 2.5 x 14.7 | 1.2 x 14.7 |
| 444 | 7.4 x 52.4 | 3.7 x 17.8 | 1.8 x 17.8 |
| 600 | 10.0 x 55.8 | 5.0 x 21.2 | 2.5 x 21.2 |
| 800 | 13.3 x 60.1 | 6.7 x 25.5 | 3.3 x 25.5 |
| 1000 | 16.7 x 64.4 | 8.3 x 29.9 | 4.2 x 29.9 |
| 1200 | 20.0 x 68.8 | 10.0 x 34.2 | 5.0 x 34.2 |
| 2000 | 33.3 x 86.1 | 16.7 x 51.5 | 8.3 x 51.5 |
| 4000 | 66.7 x 129.4 | 33.3 x 94.9 | 16.7 x 94.9 |

### 7-3. 시점 세 가지
**탑다운** (스프링 암 800cm, 아래 60도 [확인: TDGameCharacter.cpp:141~143], 세로 시야각은 가로 90도에서 58.7도, 1080p):
- 카메라 높이 = 800·sin60° = 693cm(약 7m, 사용자 가정과 일치).
- 화면 아래 끝 바닥: 광선 각 60+29.4 = 89.4° → 길이 693, 깊이 z ≈ 604.
- 화면 가운데 바닥: z = 800.
- 화면 위 끝 바닥: 광선 각 30.6° → 길이 1359, z ≈ 1185.
- 즉 바닥은 깊이 604~1185cm 구간. 이 구간에서 높음 등급의 칸은 XY 10~20cm 폭, 깊이 두께 56~68cm. 에픽은 XY 5~10cm, 두께 21~34cm.
- 두께가 시선 방향이라 바닥 위 수직 방향으로는 두께/sin60° 정도로 더 크다: 높음 z=800에서 약 69cm [추정: 기하학 유도]. 지면 안개 층이 1m 두께라면 높음 등급에서 세로로 약 1.5칸에 불과하다.
- 보이는 바닥 면적: 가로 폭 12m(아래)~24m(위, 깊이 x 2), 앞뒤 길이 약 11.7m, 합쳐 약 210㎡ [추정: 같은 식으로 유도]. 이 영역만 격자로 덮으면 되므로 "플레이어 주변만" 계산이 탑다운에서 특히 잘 맞는다(후속 설계의 근거).

**3인칭** (카메라 높이 250cm, 수평 시선 가정 [추정], 같은 시야각):
- 화면 아래 끝 바닥 깊이 = 250/tan(29.4°) = 444cm. 지면 근처 칸: 높음 7.4 x 52 cm, 에픽 3.7 x 18 cm.
- 바닥이 지평선 쪽으로 길게 이어져 수십 m 바깥까지 보인다. 깊이 4000cm 지점은 높음 67 x 129cm. 최대 거리 6000cm 너머는 안개 자체가 안 그려짐.
- 가로가 넓어 "주변만 계산"이 한계에 부딪힌다: 시야 안의 먼 곳은 반응이 눈에 거의 안 띄므로 먼 곳은 싼 대체(정적 안개)로 두는 층 구조가 필요할 것이다 [추정].

**1인칭** (눈높이 160cm, 수평 시선 가정 [추정]):
- 화면 아래 끝 바닥 깊이 = 160/tan(29.4°) = 284cm. 이 구간 칸: 높음 약 4.7 x 48cm, 에픽 약 2.4 x 14cm (깊이 284 은 표 보간, 내 추정치).
- 가까운 곳 칸은 작다(첫 슬라이스는 깊이 19.5cm에서 시작). 대신 카메라가 좌우로 돌 때 칸이 카메라와 함께 돌아 안개가 깜빡이거나 번지는 현상(지난 값 재투영 실패)이 더 두드러진다 [추정].
- 3인칭·1인칭은 카메라가 빠르게 도는 일이 많아 시간 누적(1-3)의 부작용(번짐·잔상)이 탑다운보다 커진다 [추정].

### 7-4. 이 수치에서 얻는 핵심
1. 엔진 격자 해상도는 시점에 따라 달라지므로 같은 안개 반응이 시점마다 다르게 보인다. 세상에 고정된 격자(우리 것)가 시점 독립 품질을 준다.
2. 5~10cm 규모 휘저음을 보여 주려면 에픽 이상이어야 하고, 에픽은 칸 수가 4.1M이라 RTX 2060에서 비용이 클 위험이 있다 [추정, 미실측].
3. "바닥 위 30cm 안개가 칼에 갈라지는" 연출은 높음 등급 칸(60cm 두께)으로는 표현 불가.

---

## 8. 렌더 경로 비교 표

| 경로 | 가능 여부 | 품질 한계 | 예상 비용 | 프로젝트 내 선례 |
|---|---|---|---|---|
| A. 볼륨 안개 + 볼륨 도메인 재질(우리 3D 텍스처를 월드 위치로 샘플) | 가능성 높음, 3D 샘플은 미검증 | 칸 크기가 카메라 의존(13x60cm @높음). 0.1~0.7초 지연. 자기 그림자 없음. 칸 모양 못 정함 | 격자 522k(높음)~4.1M(에픽) 칸 평가 + 조명 주입. 숫자 미실측 | `bRenderFogVolume` 경로 있음 [확인]. 2D 시뮬 RT를 MID로 넘김 [확인] |
| B. 국소 안개 볼륨(Local Fog Volume) | 불가(밀도는 타원·높이 식 해석식, 3D 텍스처 입력 없음) [추정: LocalFogVolumeRendering.cpp 콘솔 변수 목록에 밀도 텍스처 입력이 없음, 셰이더 본문은 안 열어 봄] | 임의 밀도 모양 불가 | 타일 컬링 가볍다 | 미확인 |
| C. 이질 볼륨 HV + 재질 + 런타임 3D 텍스처 | 가능성 있음(라이브 셰이딩), 3D 입력은 미검증 | 베타. 볼륨 간 정렬 중심점 기준. 반투명 합성 순서 제약. 자체 그림자는 옵션 | 화면 해상도 x 걸음 수(최대 512). 숫자 미실측. 희소 복셀 옵션은 기본 꺼짐 | 사용 중 [확인: 256x256x128 기본, L-editor-80] |
| D. 희소 볼륨 텍스처 SVT를 HV에 | 런타임 GPU 쓰기 길 없음 → 시뮬 결과 표시는 불가 | 정적·미리 구운 연기만 | 스트리밍 비용 | 없음 |
| E. 씬 뷰 확장 + 우리 레이마칭(세상 고정 희소 격자) | 가능(훅 검증) | 가장 자유롭다. 조명·그림자·루멘을 직접 구현해야 함 | 우리 최적화에 달림. 숫자 없음 | TDFogScattering BeforeDOF 훅 [확인], 비공개 FViewInfo 접근 [확인] |
| F. 파티클 스프라이트 볼륨 재질(Niagara) | 가능(스프라이트가 복셀화의 정석 입력) | A와 같은 칸 한계. 스프라이트 수 만큼 그리기 비용 | 스프라이트 수에 비례 | 미확인 |

한 줄 정리: 진짜 3D·시점 독립·빠른 반응이 목표면 E가 본선이고, A나 C는 "엔진 조명을 공짜로 받는" 대신 해상도·지연이 따라온다. 빠른 검증 순서 제안(후속 계획 재료): (1) A·C 각각에 정적 3D 텍스처 한 장을 넣어 샘플이 되는지 확인, (2) E의 최소 패스(깊이 읽기 + 단순 조명)로 RTX 2060에서 1080p 비용 측정.

---

## 9. 이번 조사의 한계 (정직하게)
- 실행·실측 없음. 모든 비용은 구조 기반 추정이다.
- 볼륨 도메인 복셀화 패스에서 `UTextureRenderTargetVolume` 샘플이 실제 성공하는지, HV 재질에서 3D 텍스처를 읽을 수 있는지는 에디터에서 안 돌려 봤다.
- `VolumetricFogStartDistance` 기본값, 근평면 기본값, 엔진이 쓰는 3D 텍스처 장수, SVT 타일 크기는 확인 못 했다.
- 웹 자료(논문·블로그)는 이 임무 범위가 아니라 열지 않았다. 희소 격자 알고리즘 쪽은 다른 조사 파일 몫이다.
- 엔진 비공개 헤더에 의존하는 방식(E, 그리고 이미 쓰는 TDFogScattering)은 엔진 업데이트 시 깨질 위험이 있다.

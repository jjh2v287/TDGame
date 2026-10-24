# 01 엔진 런타임 지형 수단 비교 (UE 5.8) — 무한 샌드박스 가능성 갈래

작성: claude 하위 에이전트, 2026-10-23. 읽기 전용 조사(소스·설정 무수정, 에디터·빌드 미실행).
표기: [검증] = 원문(엔진 소스, 저장소 파일, 웹 페이지)을 직접 읽음 / [추정] = 추론 또는 원문 미확인.
엔진 경로 접두어 `E:` = `C:/Program Files/Epic Games/UE_5.8/Engine/`.

---

## 0. 결론 요약

| 수단 | 런타임 생성 | 런타임 변경 | 충돌 | Lumen(전역 조명) | 무한 세계 적합성 |
|---|---|---|---|---|---|
| 랜드스케이프(ALandscape) | 불가(임포트·편집 API가 에디터 전용) | 불가(높이·충돌 영역 갱신이 에디터 전용) | 로드 시 높이장 생성만 | 높이장 표현 지원 | 유한·사전 베이크 전용. 무한 불가 |
| UDynamicMeshComponent + GeometryCore/GeometryScripting | 가능 | 가능(위치만 바뀌면 빠른 갱신 경로) | 삼각 메시 비동기 쿠킹 | 거리장 없음(5.6에서 폐기). 하드웨어 광선 추적만 | 1순위 |
| UProceduralMeshComponent | 가능 | 가능(섹션 단위) | 삼각 메시 비동기 쿠킹 | 거리장 없음. 항상 동적 그리기 경로 | 2순위(구식) |
| 런타임 UStaticMesh(빠른 빌드) | 가능 | 재빌드만 | 바디 셋업 생성 | 거리장·Nanite 없음 | 변경 드문 청크에만 |
| Chaos 높이장 직접 사용 | 가능(공개 생성자) | 가능(EditHeights 부분 갱신) | 가장 저렴 | 렌더와 무관 | 충돌 최적화 2단계 |
| 복셀(마칭 큐브 등) | 가능(엔진 FMarchingCubes) | 가능, 비쌈 | 편집마다 재쿠킹 | 위와 같음 | 수준 C에서만, 탑다운 이득 작음 |
| Voxel Plugin 2.0 | — | — | — | — | UE 5.8 미지원(5.6·5.7 전용, 베타) |
| ISM(인스턴스 정적 메시) | 가능 | 증분 추가·삭제 | 인스턴스별 바디 | 정적 메시와 같음 | 수준 B 건설물 1순위 |
| HISM(계층 ISM) | 가능 | 변경마다 트리 전체 재빌드 | 〃 | 〃 | 자주 바뀌는 건설물엔 부적합 |
| Water 플러그인 | 배치는 가능 | 렌더 데이터 재생성이 에디터 전용 | 런타임 갱신 | — | 절차적 강엔 부적합(실험 단계) |

핵심 판단:
1. 엔진 랜드스케이프로는 무한 세계를 만들 수 없다. 무한 지형은 "청크 단위 런타임 메시 + 런타임 충돌 쿠킹"으로 직접 만들어야 하며, 엔진은 그 부품(동적 메시, 비동기 Chaos 쿠킹, 높이장 기하, 마칭 큐브)을 런타임 모듈로 모두 제공한다. [검증, 근거 2·3절]
2. 프로젝트는 Nanite가 꺼져 있고(`Config/DefaultEngine.ini:70`) 하드웨어 광선 추적이 켜져 있다(`:59`). 런타임 메시는 Nanite·거리장을 못 가지므로 이 설정은 오히려 런타임 지형과 잘 맞는다. 단 소프트웨어 Lumen 추적에는 동적 메시 지형이 보이지 않는다. [검증 설정, 추정 영향]
3. 탑다운 카메라의 화면 바닥 면적은 약 590m²(앞 15m, 먼 쪽 폭 42m)로 작아서, 생성 처리량 요구는 매우 낮다(초당 청크 1~2개). 병목은 작업 스레드 계산이 아니라 게임 스레드의 컴포넌트 등록·물리 상태 생성이다. [추정, 계산 1.2절]

---

## 1. 전제: 프로젝트 사실

### 1.1 설정 [검증]
- `Config/DefaultEngine.ini:50` `r.GenerateMeshDistanceFields=True`, `:51` `r.DynamicGlobalIlluminationMethod=1`(Lumen), `:52` `r.Shadow.Virtual.Enable=1`(가상 섀도 맵), `:59` `r.RayTracing=True`, `:61` 광선 추적 프록시 사용, `:70` `r.Nanite.ProjectEnabled=False`.
- `Config/DefaultEngine.ini:44-45` `RecastNavMesh RuntimeGeneration=Dynamic`(결정 D-06과 일치).
- `r.Lumen.HardwareRayTracing` 줄은 DefaultEngine.ini 해당 블록에 없다. 실제로 하드웨어 Lumen이 쓰이는지는 미확인. [추정]

### 1.2 카메라와 생성 예산
- 소스 생성자 값은 스프링암 800cm, 피치 -60도다(`Source/TDGame/Characters/TDGameCharacter.cpp:142-143`) [검증]. 공통 맥락의 실측(1100cm, -50도, 요 45도, 시야각 90도)과 다르다. 런타임에 어딘가에서 덮어쓰는 것으로 보인다 [추정]. 아래 계산은 실측값을 쓴다.
- 기본 이동 속도 600cm/s(`TDGameCharacter.cpp:122` `DefaultMaxMoveSpeed = 600.f`) [검증].
- 계산(가로 시야각 90도, 16:9 가정) [추정]: 카메라 높이 842cm, 플레이어 뒤 707cm. 화면 바닥 사다리꼴은 플레이어 뒤 5.5m부터 앞 15.3m까지, 폭은 가까운 쪽 15m, 먼 쪽 42m, 넓이 약 590m². 요 45도는 방향만 돌릴 뿐 넓이는 같다.
- 함의: 32m 청크에 반경 2청크(5×5)를 유지하면 시야 앞끝보다 약 49m 앞서 생성된다. 600cm/s로 약 8초의 여유가 생긴다. 32m를 지나는 데 5.3초 걸리고 그동안 새 청크가 5개(대각 이동이면 최대 9개) 필요하다. 따라서 **필요 처리량은 초당 1~2청크**다. 작업 스레드에서 청크당 수십 ms를 써도 충분하다. 게임 스레드 작업(등록, 렌더·물리 상태 생성)은 프레임당 한도를 두고 나눠야 한다. [추정]
- 같은 이유로 높이 변화가 커도(절벽·계곡) 화면에서 보이는 범위가 좁다. 먼 거리 LOD(세부 단계)나 임포스터가 거의 필요 없다. 이 점은 일반 3인칭 무한 세계보다 유리하다. [추정]

---

## 2. 랜드스케이프 (질문 1)

### 2.1 런타임 생성·높이 변경
- 높이·가중치 편집 인터페이스 `FLandscapeEditDataInterface`는 `#if WITH_EDITOR` 블록 안에 있다(`E:Source/Runtime/Landscape/Public/LandscapeEdit.h:34`에서 시작, `:125` 정의, `:588`에서 끝). [검증]
- `ALandscapeProxy::Import(...)`(새 랜드스케이프 데이터 생성)도 `WITH_EDITOR` 블록 안이다(`E:Source/Runtime/Landscape/Classes/LandscapeProxy.h:1326`에서 시작, `:1421`, `:1679`에서 끝). [검증]
- 편집 레이어 갱신 경로(`LandscapeEditLayers.cpp:5039-5040`에서 `UpdateCollisionData` 호출)는 에디터 편집 레이어 시스템의 일부다. `Landscape.Build.cs:38-52`는 에디터·프로그램 대상일 때만 LandscapeEditor 등에 의존한다. [검증]
- 결론: 쿠킹된 게임에서는 랜드스케이프를 새로 만들 수도, 높이를 지원되는 방식으로 바꿀 수도 없다. 높이맵 텍스처를 직접 덮어쓰는 우회는 렌더만 바뀌고 충돌·풀·내비가 따라오지 않는다. 지원되지 않는 방식이다. [추정]

### 2.2 런타임 충돌
- 충돌 영역 갱신 `ULandscapeHeightfieldCollisionComponent::UpdateHeightfieldRegion` 선언은 `WITH_EDITOR` 블록 안이다(`.../Classes/LandscapeHeightfieldCollisionComponent.h:240-263`, 선언 `:257`). 구현도 `WITH_EDITOR` 안이다(`.../Private/LandscapeCollision.cpp:1363-1589`, 구현 `:1518`). `GenerateCollisionObjects`·`CookCollisionData`(`.h:253-254`)도 같은 블록이다. [검증]
- 반면 등록 시점의 런타임 생성 경로는 있다. `CreateCollisionObject(...)`(`LandscapeCollision.cpp:1049-1121`)가 uint16 높이 배열로 `Chaos::FHeightField`를 직접 만든다. 그 안의 메시지는 "Attempting to create a runtime collision object"다. 높이 원본은 직렬화된 `CollisionHeightData`다(`:1166`). [검증]
- 함의: 엔진 자신이 런타임에 원시 높이로 Chaos 높이장을 만든다. 따라서 랜드스케이프 없이 높이장 충돌을 만드는 것은 기술적으로 가능하다(3.4절). [검증 경로, 추정 적용]

### 2.3 런타임 가상 텍스처(RVT)
- `URuntimeVirtualTextureComponent::Invalidate(WorldBounds, Priority)`는 `ENGINE_API`이고 에디터 가드 밖에 있다. 게임 중에 영역별로 무효화할 수 있다(`E:Source/Runtime/Engine/Classes/Components/RuntimeVirtualTextureComponent.h:165`). [검증]
- 낮은 밉 스트리밍 텍스처(`UVirtualTextureBuilder StreamingTexture`, `:105`)를 설정·초기화하는 함수는 `WITH_EDITOR` 안이다(`:234-243`, `SetStreamingTexture`, `InitializeStreamingTexture`). [검증]
- 함의: 무한 세계에서는 미리 구운 낮은 밉이 없다. 모든 페이지를 런타임에 그려야 하며, 지형 청크가 생기거나 바뀔 때마다 그 범위를 `Invalidate`해야 한다. 탑다운은 시야가 좁아 페이지 수 자체는 적다. RVT 없이 정점 색·삼평면 투영 재질로 시작하는 편이 단순하다. [추정]
- 참고: `VirtualHeightfieldMesh` 플러그인(실험 단계, 런타임 모듈, `E:Plugins/Experimental/VirtualHeightfieldMesh/VirtualHeightfieldMesh.uplugin:6,15,19-20`)은 RVT 높이를 그리는 렌더러다 [검증]. RVT 내용은 결국 다른 기본 요소가 기록해야 하므로 독립적인 지형 생성기가 아니다 [추정].

### 2.4 런타임 풀(grass)
- `grass.GrassMap.UseRuntimeGeneration`(기본 0)은 풀 밀도 맵을 저장하지 않고 런타임에 GPU로 렌더해 만든다(`E:Source/Runtime/Landscape/Private/LandscapeGrassMapsBuilder.cpp:43-47`). `grass.GrassMap.UseAsyncFetch`(`:69-73`)는 GPU 리드백을 비동기로 한다. 생성량 상한은 `grass.MaxAsyncTasks`=4(`LandscapeGrass.cpp:124-128`), `grass.MaxCreatePerFrame`(`:182-185`)이다. [검증]
- 이 기능은 랜드스케이프 컴포넌트 전용이다. 런타임 메시 지형에는 청크 생성 시 결정론적 해시로 ISM 인스턴스를 뿌리는 방식(또는 PCG 런타임 생성, 다른 갈래 범위)이 필요하다. [추정]

### 2.5 참고: MeshPartition (UE 5.8 실험 단계)
- 설명은 "공간 분할·비파괴 수정자 편집·플랫폼 적응 런타임 표현의 대규모 메시 저작 시스템"이다(`E:Plugins/Experimental/MeshPartition/MeshPartition.uplugin:6,15`). 런타임 모듈 `MeshPartition`·`MeshPartitionCompute`가 있고, 충돌 생성은 에디터 모듈에 있다(`MeshPartitionEditor/Private/MeshPartitionCollisionGeneration.h`). [검증]
- 함의: 에디터에서 저작하고 컴파일한 결과를 런타임에 쓰는 구조다. 런타임 생성 수단이 아니다. [추정]

---

## 3. 런타임 메시 대안 (질문 2)

### 3.1 UDynamicMeshComponent (GeometryFramework, 런타임 모듈)
- 충돌 [검증, `E:Source/Runtime/GeometryFramework/Public/Components/DynamicMeshComponent.h`]:
  - `CollisionType` 기본값은 `CTF_UseSimpleAsComplex`(`:751`). 지형은 `SetComplexAsSimpleCollisionEnabled`(`:722`)로 복합 충돌을 단순 충돌로 쓴다.
  - `bUseAsyncCooking=false`가 기본값이다(`:777`). 주석은 "물리 쿠킹을 게임 스레드 밖에서"라고 설명한다. 청크 지형은 반드시 켠다.
  - `bDeferCollisionUpdates`(`:788`)와 `UpdateCollision(bOnlyIfPending)`로 여러 번 편집한 뒤 한 번만 재쿠킹할 수 있다.
  - `geometry.DynamicMesh.MaxComplexCollisionTriCount`=250,000을 넘으면 복합 충돌을 물리에 넘기지 않는다(`.../Private/Components/DynamicMeshComponent.cpp:33-37`).
  - 비동기 충돌 빌드는 기본적으로 게임 월드(PIE·런타임)에서만 일어난다(`:39-42`).
- 갱신 경로 [검증]: `FastNotifyPositionsUpdated(bNormals, bColors, bUVs)`(`DynamicMeshComponent.h:287`)는 위상이 같고 위치만 바뀔 때 렌더 버퍼를 전부 다시 만들지 않는다. 높이 편집(수준 C의 2.5차원 변형)에 맞는다. 삼각형 부분 집합 갱신과 비동기 사전 계산은 `FastNotifyTriangleVerticesUpdated_TryPrecompute`(`:342`)로 한다.
- 그리기 경로 [검증]: `bUseStaticDrawPath = bPreferStaticDrawPath && AllowStaticDrawPath(View)`(`.../Private/Components/DynamicMeshSceneProxy.h:660`). 정적 그리기 경로를 고르면 캐시된 메시 그리기 명령을 쓸 수 있다. 청크 지형은 정적 경로를 쓴다.
- 거리장 [검증]: 거리장 관련 멤버는 모두 `UE_DEPRECATED(5.6, "Dynamic Mesh distance field support has been deprecated")`다(`DynamicMeshComponent.h:692-700`). 프록시에는 `bWillHaveDistanceField = false`, `bAffectDistanceFieldLighting = false`가 고정되어 있다(`.../Private/Components/BaseDynamicMeshSceneProxy.cpp:66-68`).
- Lumen [검증 코드, 추정 영향]:
  - 바운딩 박스 기반 단순 Lumen 카드를 만든다(`DynamicMeshSceneProxy.h:89-91`, CVar(콘솔 변수) `geometry.DynamicMesh.AllowMeshCardGeneration` `BaseDynamicMeshSceneProxy.cpp:27`).
  - 광선 추적 표현이 있다(`BaseDynamicMeshSceneProxy.cpp:676-690`, `IsRayTracingRelevant`·`HasRayTracingRepresentation`이 true이고 `GetDynamicRayTracingInstances` 사용).
  - 따라서 하드웨어 Lumen에서는 지형이 간접광 추적에 잡힌다. 소프트웨어 Lumen에서는 메시 거리장이 없어서 전역 거리장에도 들어가지 않는다. 결국 화면 공간 추적에만 기여한다.
  - 동적 광선 추적 인스턴스 경로는 매 프레임 컴포넌트마다 CPU 수집 비용이 든다. 청크 수를 수십 개로 억제해야 한다.
- 지형 생성 알고리즘 [검증]: `FMarchingCubes`가 런타임 모듈 GeometryCore에 있고 병렬 계산(`bParallelCompute=true`)·값 캐시를 지원한다(`E:Source/Runtime/GeometryCore/Public/Generators/MarchingCubes.h:74-167`). `GeometryScriptingCore` 모듈 형식은 Runtime이다(`E:Plugins/Runtime/GeometryScripting/GeometryScripting.uplugin:19-20`). 다만 로직은 C++만 허용하므로(AGENTS.md 7절) 블루프린트용 GeometryScripting 래퍼보다 GeometryCore의 `FDynamicMesh3`를 C++에서 직접 쓰는 편이 맞다.

### 3.2 UProceduralMeshComponent
- 런타임 플러그인이다(`E:Plugins/Runtime/ProceduralMeshComponent/ProceduralMeshComponent.uplugin:21-22`). `CreateMeshSection`/`UpdateMeshSection`(`.../Public/ProceduralMeshComponent.h:169-215`), `bUseAsyncCooking`(`:285`)을 제공한다. [검증]
- 프록시가 항상 `bDynamicRelevance = true`다(`.../Private/ProceduralMeshComponent.cpp:395`). 정적 그리기 경로가 없어 매 프레임 동적 메시 요소를 수집한다. 같은 일을 동적 메시 컴포넌트가 더 잘한다. [검증]
- 웹 일화: 충돌이 켜진 `CreateMeshSection`이 게임 스레드에서 50~200ms 멈춘다는 보고가 있다(https://bugnet.io/blog/fix-unreal-mesh-cooker-runtime-mesh-async-build-stalls). 측정 방법이 없어 신뢰도가 낮다. [추정]

### 3.3 런타임 UStaticMesh와 Nanite
- 에디터가 아닌 빌드에서 `UStaticMesh::BuildFromMeshDescriptions`는 빠른 빌드만 허용한다(`check(Params.bFastBuild)`, `E:Source/Runtime/Engine/Private/StaticMesh.cpp:8926-8929`). 이 함수 범위(`:8917-9100`)에는 Nanite나 거리장 생성 참조가 없다. [검증(범위 내 검색)]
- Nanite 빌더는 Developer 모듈이다(`E:Source/Developer/NaniteBuilder/NaniteBuilder.Build.cs`). Nanite 어셈블리 빌더도 `WITH_EDITOR` 전용이다(`E:Source/Runtime/Engine/Public/NaniteAssemblyDataBuilder.h:6`). [검증]
- 결론: Nanite는 런타임에 빌드할 수 없다. 무한 생성 지형은 Nanite가 아닌 메시가 된다. 프로젝트는 이미 Nanite를 껐으므로(`DefaultEngine.ini:70`) 손실은 없다. 다만 Nanite를 다시 켜도 지형은 Nanite가 아닌 메시로 남는다. 가상 섀도 맵이 Nanite가 아닌 메시를 그리는 비용은 남는다(3.6절). [검증 + 추정]
- 거리장: 런타임 빠른 빌드 경로에 거리장 생성이 없다. 런타임 메시 전반이 소프트웨어 Lumen·거리장 그림자·거리장 AO에 참여하지 못한다. [검증(범위 내 검색) + 추정]

### 3.4 런타임 충돌 쿠킹 비용과 높이장
- Chaos 쿠킹 코드(`E:Source/Runtime/Engine/Private/PhysicsEngine/Experimental/ChaosCooking.cpp`, 403줄)에는 `WITH_EDITOR` 가드가 없다. 삼각 메시(`BuildTriangleMeshes` `:212`)와 볼록 메시(`BuildConvexMeshes` `:165`) 쿠킹은 모든 빌드에 들어간다. [검증]
- 비동기 API는 `UBodySetup::CreatePhysicsMeshesAsync`(`E:Source/Runtime/Engine/Classes/PhysicsEngine/BodySetup.h:343`)와 대기 중 쿠킹 중단(`:345`, "잦은 런타임 갱신의 적체 방지")이다. [검증]
- 높이장을 직접 쓸 수 있다 [검증]:
  - `Chaos::FHeightField`에 공개 생성자 두 개가 있다(실수 높이 / uint16 높이, `E:Source/Runtime/Experimental/Chaos/Public/Chaos/HeightField.h:41-42`).
  - 부분 갱신 `EditHeights(...)`가 있다(`:51-52`).
  - 실험적 `RigidPhysics` 모듈이 `FHeightFieldGeometry`로 감싼다(`E:Source/Runtime/Experimental/RigidPhysics/Private/RigidPhysics/Geometry/HeightFieldGeometry.cpp:4,16-44`, `UE_RIGIDPHYSICS_API_ENABLED` 조건).
  - 그러나 이를 쓰는 기성 컴포넌트는 랜드스케이프뿐이다. 직접 쓰려면 사용자 정의 기본 요소 컴포넌트가 물리 바디를 이 기하로 만들어야 한다. 랜드스케이프 충돌 컴포넌트의 생성 코드(`LandscapeCollision.cpp:1049-1121`)가 견본이다. 구현 난도는 중간이고 엔진 내부 API 의존이 있다. [추정]
- 쿠킹 시간: 공신력 있는 수치는 찾지 못했다. 웹 검색 결과 벤치마크가 없다. 아래 6절 수치는 자료 구조로 추정한 값이며 Unreal Insights로 측정해야 한다. [추정]

### 3.5 지형 변경과 가상 섀도 맵(VSM) 캐시
- 캐시는 기본으로 켜져 있다(`r.Shadow.Virtual.Cache`=1, `E:Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapCacheManager.cpp:96-101`). 완전히 가려진 인스턴스의 무효화는 HZB(계층 깊이 버퍼)로 거른다(`:102-106`). 100프레임 동안 무효화가 없으면 정적 캐시로 옮긴다(`r.Shadow.Virtual.Cache.FramesStaticThreshold`=100, `:143-147`). 변형 메시로 표시된 프록시는 매 프레임 무효화한다(`:108-112`). [검증]
- 기본 요소별 무효화 정책 `EShadowCacheInvalidationBehavior`는 Auto, Always, Rigid, Static이다. Static도 "추가·삭제는 여전히 무효화를 일으킨다"(`E:Source/Runtime/Engine/Public/SceneTypes.h:222-236`). [검증]
- 함의 [추정]:
  - 청크 생성·편집은 그 청크 경계와 겹치는 그림자 페이지만 무효화한다. 탑다운 시야에서는 한 번에 수십 페이지 수준이다.
  - Nanite가 꺼져 있어 무효화된 페이지를 Nanite가 아닌 메시 패스로 다시 그린다. 청크 메시를 섹션 수 적게(재질 1~2개) 유지하는 것이 비용 관리의 핵심이다.
  - 편집 직후 100프레임 동안은 동적 취급을 받는다. 채굴처럼 연속 편집이 일어나는 청크는 그동안 매 프레임 비용이 든다.

### 3.6 정리: 지형 렌더 수단 선택
- 기본안: 청크마다 `UDynamicMeshComponent` 1개를 둔다. 정적 그리기 경로, 비동기 쿠킹, 복합을 단순으로, 재질 1개, 결정론적 해시 노이즈(전역 난수 금지 규칙과 일치)로 만든다.
- 변경이 드문 청크는 런타임 `UStaticMesh` 빠른 빌드로 바꿔 표준 정적 메시 프록시를 쓸 수 있다. 이득은 측정 후 판단한다. [추정]
- 충돌은 1단계로 삼각 메시 비동기 쿠킹을 쓴다. 수준 C에서 편집이 잦으면 2단계로 사용자 정의 Chaos 높이장(`EditHeights`)을 쓴다.

---

## 4. 샌드박스 (질문 3)

### 4.1 복셀 표면 추출(수준 C)
- 엔진에 런타임 `FMarchingCubes`가 있다(3.1절). 서피스 넷·듀얼 컨투어링·Transvoxel(LOD 이음매 처리)은 엔진에 없다. 직접 구현해야 한다(GeometryCore에서 SurfaceNet 파일 검색 결과 없음). [검증(파일 검색)]
- 현실성 [추정]:
  - 32³ 셀 청크 하나의 원시 데이터는 밀도 1B + 재질 1B로 약 64KB다. 추출은 작업 스레드에서 1~수 ms 수준이다.
  - 편집할 때마다 해당 청크(경계면 편집이면 이웃까지)를 재추출하고 삼각 메시를 재쿠킹해야 한다.
  - 저장은 "시드 + 청크별 편집 차분"이면 작다.
  - 어려운 부분은 LOD 이음매, 청크 경계 법선, 재질 블렌딩, 내비메시 재빌드 폭주다.
- 탑다운 적합성 [추정]: 피치 -50도 카메라는 동굴·돌출부 아래를 거의 보여주지 않는다. 지면을 파고 쌓는 효과의 대부분은 2.5차원 높이장 편집으로 얻는다. 높이장 편집은 위상이 그대로라서 `FastNotifyPositionsUpdated`와 `EditHeights`로 갱신할 수 있다. 완전 복셀은 동굴·터널이 게임 설계의 핵심일 때만 정당화된다.

### 4.2 외부 Voxel Plugin 2.x (웹)
- 최신 버전은 2.0p8이다. 문서는 "적극 개발 중이고 때로 버그가 있다"고 하며, "이 릴리스는 Unreal Engine 5.6과 5.7만 대상으로 한다"고 적었다(https://docs.voxelplugin.com/). **UE 5.8은 현재 미지원**이다. [검증]
- 라이선스: 영구 $349(부가세 별도)이며 기능 갱신 1년, 버그 수정 3년, 엔진 업그레이드 3년을 포함한다. 예산 10만 달러 초과 프로젝트는 별도 계약이다(https://docs.voxelplugin.com/resources/licensing). [검증]
- 런타임 편집은 스탬프 또는 스컬프트 함수(동기/비동기)로 한다. 저장은 `Get Save`/`Load from Save`로 하며, 멀티플레이어 복제는 기본 제공되지 않는다(https://docs.voxelplugin.com/knowledgebase/blueprints/runtime-edits-and-sculpting.html). [검증]
- 구판(Legacy)은 Fab 목록상 수명 종료 지원 상태다(https://www.fab.com/listings/5c85f2f0-cf03-4860-b22e-e4f470af4133). [검증(검색 요약 경유), 원문 미열람이면 추정]
- 판단: UE 5.8 프로젝트에는 지금 쓸 수 없다. 5.8 지원 시점은 미정이다("새 엔진 지원은 마이너 업데이트로"라는 문구뿐). 또 이 플러그인의 그래프·블루프린트 중심 저작 방식은 로직을 C++만으로 짠다는 규칙과 맞물리는지 별도 검토가 필요하다. [추정]

### 4.3 격자·타일 건설(수준 B): ISM / HISM
- HISM: `BuildTreeIfOutdated`는 인스턴스 변경, 재정렬표 불일치, 미빌드 인스턴스가 있으면 클러스터 트리를 통째로 재빌드한다. 플레이 시작 후에는 비동기다. 비동기 빌드 중에 또 바뀌면 `bConcurrentChanges = true`로 진행 중인 결과를 무효화한다(`E:Source/Runtime/Engine/Private/HierarchicalInstancedStaticMesh.cpp:2780-2830`). [검증]
  - 함의: 연속 설치·철거는 재빌드가 계속 무효화되고 지연되는 상황을 만든다. HISM은 거의 바뀌지 않는 장식(바위, 나무)에만 쓴다. [추정]
- ISM: `AddInstances`(`E:Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h:275`), `RemoveInstances`(`:421,427`), 제거 시 맨 뒤와 교체(`bSupportRemoveAtSwap` `:235`, `SetRemoveSwap()` `:465`)를 제공한다. GPU 장면 증분 갱신은 `FPrimitiveInstanceDataManager`(`:710`)가 맡는다. [검증]
  - 권고: 청크 × 메시 종류별로 ISM 1개를 두고 맨 뒤와 교체 제거를 켠다. 인스턴스 번호가 바뀌므로 게임 쪽은 "건물 ID → 인스턴스 번호" 대응표를 유지한다. 건물의 그림자 정책은 Static으로 둔다(추가·삭제만 무효화). [추정]
  - 충돌: ISM 인스턴스마다 물리 바디가 생긴다. 대량 건설에서는 단순 충돌 형상을 쓰고, 청크 단위 비동기 처리가 필요하다. [추정]
- Nanite ISM: 프로젝트가 Nanite를 껐으므로 현재 해당 없음. [검증 설정]
- 저장 모델(수준 B·C 공통) [추정]: 세계 = 시드 + 생성기 버전 + 청크별 차분(설치·철거 목록, 높이 편집). 생성기가 결정론적이므로 차분만 저장한다. 생성기 알고리즘을 바꾸면 기존 세이브가 깨지므로 생성기 버전 고정이 필수다.

---

## 5. 강·물 (질문 4)

- Water 플러그인은 실험 단계다(`E:Plugins/Experimental/Water/Water.uplugin:15`, `IsExperimentalVersion: true`). [검증]
- 런타임 경로: `UWaterBodyComponent::UpdateAll`(`.../Source/Runtime/Private/WaterBodyComponent.cpp:1564-1637`)은 에디터 가드 밖이다. 수위, 섬·제외 볼륨, 수역(워터 존) 갱신, 충돌·내비 설정을 처리한다. 워터 존은 영역 단위로 `MarkForRebuild(UpdateWaterInfoTexture)`를 런타임에 수행한다(`WaterZoneActor.cpp:345-367`). [검증]
- 그러나 수역 렌더 데이터 재생성 `UpdateWaterBodyRenderData()`의 호출부 4곳(`:1672`, `:1712`, `:1865`, `:2120`)은 모두 `WITH_EDITOR` 블록 안이다(`:1642-1677`, `:1709-`, `:1818-`, `:2112-`). 함수 자체도 플레이 중에는 `AreDynamicDataChangesAllowed()`일 때만 메시를 갱신한다(`:2132`). [검증]
- 함의 [추정]:
  - 런타임에 생성한 강 수역은 수역 정보 메시와 정적 메시가 없어 워터 존 렌더에 제대로 나오지 않을 가능성이 높다.
  - 강바닥을 파는 워터 브러시는 랜드스케이프 편집 레이어 기반이라 런타임 메시 지형에는 적용되지 않는다.
- 권고 [추정]: 절차적 세계의 강은 생성기가 높이장에 강바닥을 파고, 스플라인을 따라 리본 형태의 동적 메시를 만든 뒤 단일 레이어 물 재질을 입힌다. Water 플러그인은 손으로 저작하는 고정 지역(현재 잿빛 골짜기 등)에서만 쓴다. 물리 부력·수영 판정은 자체 C++ 수위 질의로 처리한다.

---

## 6. 비용 추정표 [전부 추정, 자료 구조 크기 기반 — 측정 필요]

기준 청크: 64m × 64m, 1m 격자 → 정점 4,225개, 삼각형 8,192개.

| 항목 | 메모리 | 시간 | 근거 |
|---|---|---|---|
| 렌더 버퍼(정점 32B: 위치 12 + 접선 8 + UV 8 + 색 4, 인덱스 16비트) | 약 0.19MB | 업로드 1ms 미만 | 정점 135KB + 인덱스 49KB |
| `FDynamicMesh3` CPU 사본(정점·삼각형·간선·법선·UV 오버레이) | 약 0.8~1.2MB | 생성 1~3ms(작업 스레드) | 배정밀도 위치 24B, 간선 약 12,400개 등 |
| Chaos 삼각 메시 충돌(정점 12B, 인덱스 16비트, BVH, 면 재매핑) | 약 0.2~0.3MB | 쿠킹 1~5ms(작업 스레드, 비동기) | `ChaosCooking.cpp:212-346` 구조 |
| Chaos 높이장 충돌(uint16 높이 + 칸별 재질 1B) | 약 0.02MB | 생성 1ms 미만, `EditHeights` 부분 갱신 | `HeightField.h:406-451` 저장 형식 |
| 높이 노이즈 계산(6옥타브 fBm, 표본당 약 100ns) | — | 약 0.4ms(작업 스레드 1개) | |
| 게임 스레드 등록(컴포넌트, 렌더 상태, 물리 상태) | — | 청크당 0.2~1ms | 프레임당 1~2청크로 제한 |
| 5×5 청크 유지(320m 정사각형) | 렌더 약 5MB, CPU 사본 약 25MB, 충돌 약 6MB | — | 0.5m 격자로 가면 약 4배 |
| 32³ 복셀 청크 원시 데이터 | 64KB | 추출 1~수 ms | 밀도 + 재질 각 1B |

- 처리량 요구(1.2절)는 초당 1~2청크다. 위 수치를 기준으로 하면 작업 스레드 예산은 여유가 10배 이상이다. [추정]
- 측정 계획(이번 갈래에서는 실행하지 않음): Unreal Insights에서 `UBodySetup::CreatePhysicsMeshesAsync`, `UDynamicMeshComponent::UpdateCollision`, `NotifyMeshUpdated`, VSM 무효화 페이지 수(`r.Shadow.Virtual.ShowStats`)를 청크 크기 32m·64m, 격자 0.5m·1m 조합으로 기록한다.

---

## 7. 수준별 권고

- (A) 무한 생성, 지형 변경 없음: **가능, 위험 중간.**
  - 청크 동적 메시, 비동기 충돌, 결정론적 노이즈를 쓴다. 장식은 HISM이나 ISM으로 뿌린다. 내비메시는 Dynamic이다.
  - 랜드스케이프·Water 플러그인·RVT 사전 베이크·랜드스케이프 풀은 쓰지 못한다.
  - 기존 잿빛 골짜기(랜드스케이프)와 무한 영역을 걸어서 잇는 이음매(D-42)는 경계에서 높이를 맞춰야 한다. 생성기가 랜드스케이프 가장자리 높이를 경계 조건으로 읽는다. [추정]
- (B) 설치·철거·건설: **가능, 위험 낮음~중간.** (A) 위에 청크별 ISM, 맨 뒤와 교체 제거, 차분 저장을 얹는다. HISM은 건설물에 쓰지 않는다.
- (C) 지형 변형: **2.5차원 높이 편집은 가능, 위험 중간. 완전 복셀은 가능하지만 위험이 높고 탑다운에서 이득이 작다.**
  - 2.5차원 경로: `FastNotifyPositionsUpdated`로 렌더를 갱신하고, 충돌은 청크 재쿠킹(지연·묶음) 또는 사용자 정의 Chaos 높이장 `EditHeights`로 갱신한다. VSM 페이지 무효화와 내비 타일 재빌드가 함께 따라온다.
  - Voxel Plugin 2는 UE 5.8 미지원이라 지금은 선택지가 아니다.

---

## 8. 미해결 질문
1. 하드웨어 Lumen 사용 여부(`r.Lumen.HardwareRayTracing` 프로젝트 기본값)와 목표 사양. 소프트웨어 Lumen이면 동적 메시 지형이 간접광에 빠진다.
2. 카메라 실측(1100/-50/45)과 소스 생성자(800/-60)의 차이. 어디서 덮어쓰는지 확인해야 한다.
3. Chaos 삼각 메시 쿠킹과 동적 메시 등록의 실제 ms. 웹에 공신력 있는 수치가 없어 측정해야 한다.
4. 사용자 정의 Chaos 높이장 컴포넌트를 만들 때 필요한 `FBodyInstance`·물리 프록시 생성 경로(엔진 내부 API 의존 범위).
5. Water 플러그인 `AreDynamicDataChangesAllowed()`의 정의 위치와 기본값. 이번 조사에서 정의부를 찾지 못했다.
6. 무한 영역 청크 액터를 월드 파티션 스트리밍 밖에서 자체 관리할 때 데이터 레이어·저장 체계와의 관계(다른 갈래 범위).

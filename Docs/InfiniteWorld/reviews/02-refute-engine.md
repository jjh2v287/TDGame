# 02-draft 반박: 엔진·코드 사실 (refute-engine)

작성: 2026-10-23, claude 하위 에이전트(반박 갈래). TDGame Source·Config·Content·장부 무변경, 에디터·빌드 미실행.
경로 약칭: `E:` = `C:/Program Files/Epic Games/UE_5.8/Engine/Source/`, `IW:` = `C:/Project/TDGame/Saved/AgentOps/20261023/infinite-world/`, `B:` = `IW:bench/IWBench/Source/IWBench/`.

## 판정: 조건부

엔진 인용은 대부분 원문과 맞는다. 다만 수준 A 판정 문장(0절)이 벤치가 실제로 잰 범위보다 넓게 쓰였다. 거대 내비 경계 규칙에는 코드로 확인한 공백(RebuildAll 경로)이 하나 있다. 아래 1~4번을 고치면 결론(A 가능·B 조건부·C 2.5차원 조건부)은 유지된다.

## 지적 (중요 4, 경미 10)

### 1. [중요] 0절 A·3.3절: "폰이 준비 안 된 청크 위에 선 프레임 0"은 폰 측정이 아니고, 판별력도 낮다
- **문제**
  - 벤치에는 폰이 없다. `ATDBenchMoverActor`(B:TDBenchStream.cpp:30-36)는 충돌과 이동 컴포넌트가 없는 액터이고, 매 프레임 벽시계 시간으로 위치를 `SetActorLocation` 한다(:692-712).
  - "미준비" 판정은 이동점 XY에서 Visibility 채널로 수직 트레이스 1개를 쏴서, 맞은 것이 임의의 `UDynamicMeshComponent`인지만 본다(:722-734). ISM은 Visibility를 무시하므로(:950) 판정 대상이 아니다.
  - 상주 반경이 96 m(:49)인데 요청→충돌 최대 지연은 119 ms다. 그래서 50 m/s(0.12 s에 6 m)에서도 발밑 청크는 구조상 항상 준비된다. 값 0은 설계상 거의 보장된 값이라 판별력이 없다.
- **근거** [검증] 벤치 소스 위 줄. `IW:bench/results/stream-B-r2.json` x50 `latency_request_to_collision_ms` 최대 119, `latency_request_to_instances_ms` 최대 153.
- **결론에 미치는 영향** 결론 자체는 다른 수치로 살아난다 [추정]. 청크는 96 m에서 요청되고, ISM까지 끝나는 데 최대 153 ms(50 m/s에서 7.7 m)가 걸린다. 화면 최원 35.3 m까지는 약 53 m 여유가 남는다. 다만 0절은 그 근거로 써야 한다.
- **수정**
  - 0절 문구를 "이동점 수직 트레이스 기준 미준비 0. 요청→지형 충돌 최대 119 ms·ISM 최대 153 ms, 링 여유 약 53~60 m"로 바꾼다.
  - IW-09·G1에 실제 Mover 폰 기준 지표를 추가한다. 화면 반경 35.3 m 안 청크의 지형+ISM 준비율과 캡슐 아래 충돌 존재율이다.

### 2. [중요] 0절 A: "60 fps를 유지했으며, 원점에서 1,000 km 떨어져도 수치가 같았다"의 범위가 넘친다
- **문제**
  - 60 fps는 벤치 코드가 `t.MaxFPS`를 60으로 강제한 상한이다(B:TDBenchStream.cpp:1345-1351. 설정 파일은 `t.MaxFPS=0`). 측정은 NullRHI라 렌더 스레드와 GPU 작업이 없다(`stream-A/B-*.json` meta `can_ever_render=false`).
  - 14경우 모두 프레임 간격 최대가 19.0~20.5 ms로, 60 fps를 놓친 프레임이 있다. 50 m/s의 CPU p99는 12.6~13.9 ms다. 이 값은 빈 월드 기준이라, 실제 게임 부하를 얹을 여유가 약 3 ms뿐이다.
  - 1,000 km 비교는 `f6`(6 m/s) 1경우 × 2회와 내비 f6i4000뿐이다. 15·50 m/s와 렌더는 원거리에서 재지 않았다.
- **근거** [검증] 위 JSON의 `frame_delta_ms.max`, `frame_cpu_ms.p99`, `ParseCases`(B:TDBenchStream.cpp:372-404, `f`는 6 m/s에만 쓰임).
- **수정** 0절 문구를 "NullRHI·60 fps 상한에서 프레임 간격 p99 16.7 ms(최대 20.5). CPU p99는 50 m/s에서 14 ms. 원거리 1e8 cm는 6 m/s에서 원점과 같음"으로 좁힌다.

### 3. [중요] 3.2·3.3절: "벤치가 잰 구조가 tech 구조와 같다"는 과장이다. 작업 부하와 콘텐츠가 다르다
- **문제** 벤치와 설계가 세 가지에서 다르다.
  - **생성기**: 벤치는 6옥타브 값 노이즈 자리표시자로, 높이만 0.26 ms다(B:TDBenchStream.cpp:115-127,229-247. `terrain-r1.json` `a0_height_generation_ms` 중앙값 0.259). 설계는 C++ 생성기 약 2 ms + L1·L2 계획·산포다(4.5·4.7절).
  - **동시성**: 벤치는 요청마다 `UE::Tasks::Launch`를 상한 없이 띄운다(:1033). 설계는 동시 2개다(4.1·4.5절). 요청→충돌 p50 36 ms는 무제한 병렬과 가벼운 노이즈로 얻은 값이다.
  - **ISM**: 벤치는 `/Engine/BasicShapes/Cube`(B:TDBenchGameMode.cpp:422) 하나다. 상자 충돌 1개짜리 1,024개를 32×32 격자에 모두 BlockAll로 깐다(B:TDBenchStream.cpp:45-48,200-219,947-951). 실제 나무·바위의 다중 볼록 충돌과 삼각형 수는 반영되지 않았다.
- **근거** [검증] 위 줄. [추정] 처리량 자체는 여유가 크다(동시 2개 × 약 4.2 ms → 약 470청크/s, 50 m/s 진입은 수 청크/s).
- **수정**
  - 3.2절의 "기술 실현성 5" 근거에 "생성기 자리표시자·무제한 동시성·큐브 ISM" 한계를 적는다.
  - IW-02 또는 IW-03에 실제 이식 생성기, 동시 2개 상한, 대표 메시 3종(나무·바위·풀)으로 stream x15·x50 재측정을 추가한다.

### 4. [중요] 4.10절 거대 경계 규칙: RebuildAll 경로가 빠졌다
- **문제**
  - 규칙은 "경계를 인보커보다 먼저 1회 등록"만 다룬다. 그런데 `FRecastNavMeshGenerator::RebuildAll()`은 인보커 활성 여부를 보지 않고 `MarkNavBoundsDirty()`를 부른다(E:Runtime/NavigationSystem/Private/NavMesh/RecastNavMeshGenerator.cpp:5785-5806, 호출은 :5803). 이 함수는 InclusionBounds 전체를 `MarkDirtyTiles`로 보낸다(:6697-6712).
  - 그러면 활성 집합이 비어 있지 않으므로(:6742) 경계 상자 전 타일 이중 루프(:6808-6812)를 돈다.
  - 같은 파일의 최초 생성 경로는 `IsActiveTilesGenerationEnabled()==false`일 때만 더럽힌다(:5550-5554). 엔진도 이 위험을 한쪽에서만 막고 있다는 뜻이다.
- **근거** [검증: 코드] 위 줄. [추정: 영향] 벤치의 타일당 17~23 ns를 적용하면 ±1.1e8 cm에서 약 380~1,340 s 정지다(초안 외삽과 같은 식).
- **수정**
  - 규칙에 "인보커가 활성인 동안 RebuildAll(`ANavigationData::RebuildAll`, `UNavigationSystemV1::Build`, `RebuildNavigation` 콘솔 명령, 내비 데이터 재생성) 금지"를 더한다.
  - IW-04에 RebuildAll 호출 시 정지 재현과 로그 검사를 넣는다. 런타임에 RebuildAll을 일으키는 경로 목록은 미확인이므로 IW-04에서 조사한다.

### 5. [경미] 2.2절 "청크 GT 약 1 ms = 등록·SetMesh 0.13~0.21 + 렌더 프록시 0.42~0.50 + 비동기 쿠킹 요청 0.38~0.50"은 중복 합산이다. 일부 범위는 중앙값만 적었다
- **문제**
  - `d_async_complex_collision.gt_total`(0.38~0.50)이 이미 생성·등록·SetMesh를 포함한다. 그러니 `b_component_no_collision.total`(0.13~0.21)을 더하면 이중 계산이다. 바른 값은 0.84~0.97 ms다(렌더 실행: 비동기 총합 0.42~0.50 + 프레임 끝 갱신 0.42~0.47).
  - "8개 동시 요청도 9.7~15 ms 안에 모두 적중"은 중앙값 범위다. 최대는 21.4 ms이고, 8청크를 한 번에 생성하는 GT는 렌더 실행에서 7.6~8.6 ms(최대 13.3)였다.
- **근거** [검증] `IW:bench/results/terrain-{r1,r2,render1,render2}.json`. 정의는 B:TDBenchGameMode.cpp:282-307,864-929,1028-1098.
- **수정** 식을 "비동기 생성 총합 0.38~0.50 + 프레임 끝 렌더 0.42~0.50 ≈ 0.8~1.0 ms"로 고치고, 범위에 "중앙값(최대 …)"을 표기한다.

### 6. [경미] 0절 C·4.6절: "재쿠킹 GT 0.28 ms에 6~8 ms 뒤 반영"은 프레임 끝 렌더 갱신을 뺀 값이고, "반영"은 충돌만 뜻한다
- **문제**
  - 편집 단계는 `EditMesh` 호출 시간만 잰다. `SendAllEndOfFrameUpdates`가 없다(B:TDBenchGameMode.cpp:974-1013). 초안이 직접 적은 함정(2.2절 "렌더 갱신이 프레임 끝으로 밀린다")이 이 측정에도 그대로 적용된다.
  - 전체 프록시를 다시 만드는 비용은 같은 크기에서 0.42~0.50 ms(최대 1.19)였다(`b_component_no_collision.eof_flush_ms`, 렌더 실행).
  - "반영"은 충돌 트레이스 갱신만 뜻한다(:996-998). 수치도 측정 중앙값은 5.7~7.0 ms, 최대는 7.8~10.5 ms다. 0절·4.6절의 "6~8 ms"와 어긋난다.
- **근거** [검증] 위 소스와 JSON.
- **수정** "편집 1회 GT 약 0.3 ms + 프레임 끝 렌더 갱신 최대 약 0.5 ms(미측정, `FastNotifyPositionsUpdated`로 줄일 후보). 충돌 반영 중앙값 5.7~7.0 ms·최대 10.5 ms"로 적는다.

### 7. [경미] 0절 B: "등록된 컴포넌트에 1개 추가·제거 0.26 / 0.18 ms"는 즉시 호출 시간뿐이다
- **문제** 단일 편집 단계는 `AddInstance`·`RemoveInstance` 호출만 잰다. 프레임 끝 렌더 인스턴스 갱신과 다음 프레임 비용은 재지 않았다(B:TDBenchGameMode.cpp, `QueueIsmConfig` case 4). `ism-r1/r2`는 NullRHI다. B의 "설치·철거 1회 GT ≤ 1 ms"(IW-22)의 근거로는 불완전하다.
- **근거** [검증] 벤치 소스, `ism-*.json` 값(4,096 충돌 켬 `add_one_registered` 0.26, `remove_one` 0.18)은 초안과 일치한다.
- **수정** IW-03이나 IW-22에 "편집 뒤 다음 프레임 GT"를 추가해 잰다.

### 8. [경미] 4.3절: 렌더 규칙의 근거로 물리 트레이스 정밀도를 썼다
- **문제** "1e9 cm까지 0.0001 cm"는 동기 쿠킹 충돌에 Chaos 트레이스를 쏴서 얻은 값이다(B:TDBenchGameMode.cpp:1107-1158). 그런데 4.3절은 이 값을 "지형 메시·ISM·나이아가라처럼 float 상대값으로 그리는 모든 것"의 근거로 쓴다. 렌더 위치 정밀도는 2.2절 스스로 미측정으로 적었다.
- **근거** [검증] 소스. 값 일치: 액터 변환 0.00011 cm, 정점에 구움 1e8 cm에서 1.719 cm·1e9 cm에서 11.64 cm.
- **수정** 4.3절 근거를 "물리 충돌 측정"으로 한정하고, 렌더는 IW-24(E10)로 넘긴다고 적는다.

### 9. [경미] F10 "쿠킹 완료 뒤 내비 갱신 호출 없음"의 표기가 [검증(벤치)]인데, 벤치는 이를 시험하지 않았다
- **문제** 벤치는 매번 `UpdateComponentData`를 부른다(B:TDBenchStream.cpp:1161-1169). 생략했을 때 비교한 실행이 없다. 코드 확인은 맞다. `FinishPhysicsAsyncCook`은 `RecreatePhysicsState`만 부르고(E:Runtime/GeometryFramework/Private/Components/DynamicMeshComponent.cpp:1688-1692), `UPrimitiveComponent::OnCreatePhysicsState`(E:Runtime/Engine/Private/Components/PrimitiveComponent.cpp:882~)에도 내비 호출이 없다(grep 0건).
- **수정** 표기를 [검증(코드)]로 바꾸고, IW-04에 생략 A/B(타일 갱신 유무)를 넣는다.

### 10. [경미] F9 인용 줄이 주장을 받치지 않는다
- **문제** `DynamicMeshComponent.cpp:1601-1666`은 `RebuildPhysicsData`다. 이 줄들이 보여 주는 것은 "비동기는 `World->IsGameWorld()`(또는 에디터 콘솔 변수)일 때만"(:1601-1609)이다. 등록 때 동기 쿠킹이 일어나는 경로는 이 범위에 없다.
- **보탤 사실** 에디터 월드에서 동적 메시를 쓰는 도구(허브 단면 추출기·세트피스 굽기)는 동기 쿠킹이 된다.
- **수정** F9는 [관측]만 남기고 인용을 "관측: `IW:bench/logs/…`"로 바꾼다. 4.2절 에디터 도구 항목에 동기 쿠킹 주의를 단다.

### 11. [경미] F11의 파생 데이터 캐시 문턱 방향이 모호하다
- **문제** `p.BodySetupSkipDDCThreshold`(기본 16384, WITH_EDITOR 전용)는 "정점 수가 문턱 **미만**이면 파생 데이터 캐시를 건너뛴다"는 뜻이다(E:Runtime/Engine/Private/PhysicsEngine/BodySetup.cpp:112-118). 따라서 65×65(4,225정점)는 기본 설정에서도 캐시를 건너뛴다. 캐시 경로를 타는 것은 129×129(16,641정점)뿐이다. 초안의 "편집기 쿠킹은 파생 데이터 캐시 경로(16,384가 문턱)"는 64 m 청크도 캐시를 탄다고 읽힌다.
- **수정** "편집기는 정점 16,384 이상만 캐시 경로. 64 m 청크는 기본에서도 직접 쿠킹"으로 고친다.

### 12. [경미] 2.2절 내비 메모리 "1.0~2.1 MB, 1단 가정 32 MB보다 훨씬 작다"는 집계 범위가 좁다
- **문제** `ARecastNavMesh::LogMemUsed`는 Detour 타일 데이터와 경로 메모리만 센다(E:Runtime/NavigationSystem/Private/NavMesh/RecastNavMesh.cpp:3246-3264, NavigationData.cpp:958-968). 내비 옥트리(벤치에서는 ISM 1,024개/청크가 모두 내비 관련)와 생성기 작업 메모리는 빠진다.
- **수정** "Detour 타일 데이터 기준"으로 표기하고, 150 MB 예산 실측(IW-02·G2)에 내비 옥트리를 포함한다.

### 13. [경미] IW-01·F5: 설정 인용 줄 오류와 거리장 손실 누락
- **문제**
  - IW-01이 렌더 설정이라며 인용한 `Config/DefaultEngine.ini:10-15,24,43,46`은 RecastNavMesh 설정이다(bFixedTilePoolSize…AgentRadius, TileNumberHardLimit, RuntimeGeneration).
  - `:50`은 `r.GenerateMeshDistanceFields=True`다. 프로젝트는 메시 거리장을 쓴다. 그런데 F5는 "Nanite가 꺼져 있어 손실이 적다"만 적었다. 지형 광선 추적을 끄는 대체안(4.6절)에서는 지형이 거리장에도 없으므로 Lumen 장면에서 완전히 빠진다.
- **근거** [검증] 설정 파일 줄.
- **수정** 인용을 `:50-52,59,70`으로 고치고, F5 영향에 "거리장 기반 기능(소프트웨어 Lumen 대체 경로) 손실"을 추가한다.

### 14. [경미] IW-03 완료 조건 "관리자 GT p99 ≤ 2.0 ms"는 벤치 스케줄러 방식으로는 구조적으로 넘는다
- **문제** 벤치 스케줄러는 매 프레임 작업을 최소 1개 하고, 예산은 작업 **시작 전**에만 검사한다(B:TDBenchStream.cpp:1097-1100). 그래서 마지막 작업 하나만큼 예산을 넘는다. 측정 p99도 2.5~3.3 ms였다. 같은 방식이면 작업이 1 ms 이하여도 p99는 약 3 ms까지 나온다.
- **수정** 설계 4.5절에 "예상 작업 비용 + 경과 시간 > 예산이면 다음 프레임으로" 규칙을 명시한다. 또는 IW-03 기준을 "예산 + 작업 1개(≤ 3.0 ms)"로 맞춘다.

## 확인한 인용 (원문과 일치)

- **동적 메시·충돌**
  - `DynamicMeshComponent.h:777`의 `bUseAsyncCooking=false`.
  - ChaosCooking.cpp에는 WITH_EDITOR 가드가 없다. `:143,341`의 `bCullsBackFaceRaycast=false`.
  - `HeightField.h:342-347`: BoundingBox가 CachedBounds를 채운다.
- **광선 추적·Lumen**
  - `BaseDynamicMeshSceneProxy.cpp:689` GetDynamicRayTracingInstances, `BaseDynamicMeshComponent.h:587,594`, `PrimitiveComponent.h:473`.
  - `LumenHardwareRayTracingCommon.cpp:16-22`: 기본값 1, "인스턴스 10만 개 이상" 경고.
- **내비**
  - `RecastNavMeshGenerator.cpp:5748-5773`(고정 풀은 TilePoolSize로 검사), `:6742`, `:6808-6812`.
  - `NavigationSystem.cpp:4163-4179`(GetComponentsBoundingBox(true)), `NavMeshBoundsVolume.cpp:117-124`, `NavigationSystem.h:391,585,824-834,1096`.
  - `RecastNavMesh.cpp:1894-1898`(셀 크기 배수로 내림. 프로젝트 셀 19이므로 1000→988·1600→1596·800→798, 벤치 JSON과 일치).
- **세이브**: `FileHelper.cpp:738-751`(직접 덮어쓰기), `SaveGameSystem.h:151-154`, `FileManagerGeneric.cpp:299-323`(삭제 뒤 이동), `WindowsPlatformFile.cpp:1216-1218`(MoveFileW 교체 플래그 없음).
- **부동소수점·빌드**: `VCToolChain.cs:1313-1335`(V7 이상에서 Editor·Program은 Precise, Default는 /fp:fast), `ModuleRules.cs:784-790`. `HotReload.cs:271-278`과 `IW:bench/logs/build1.log`의 "Unable to build while Live Coding is active".
- **좌표·월드 상수**: `UnrealMath.cpp:3633-3636`(float 절단·& 255), `EngineDefines.h:41,59`, `WorldSettings.cpp:116`(KillZ = −1,048,575 cm).
- **프로젝트 파일**: `TDWorldStateSubsystem.cpp:157`(SaveGameToSlot), `.h:52-53`, `TDWorldGenTypes.h:14-15`, `TDSeamlessTravelSubsystem.cpp:422-426`, `TDMonsterThinkSubsystem.h:64`(4500), `TDTopDownSettings.h:24-25`(260), `TDTopDownOcclusionSubsystem.cpp:52`(float 절단), `TDDungeonDefinitions.h:126`(300000 cm = 3 km), `DefaultEngine.ini:10,12,13,59,70`, decisions D-05·06·40·45·46·47 줄, GQ-08 :236·GQ-22 :250, M3-04 :503, 04 :424, 03 :141.
- **측정값**: 스트리밍 14경우 미준비 0, 관리자 최대 4.81, ISM 4,096 충돌 34~35/52~55/70~71 ms, 내비 59/59, 활성 타일 최대 263, 경계 0 실패(nav-smoke), 50 m/s 내비 출현 20 m. 늦은 경계 추가: 기준선을 뺀 타일당 17.4~23.3 ns → ±1.1e8 cm 외삽 380~1,340 s(초안 380~1,330과 일치).

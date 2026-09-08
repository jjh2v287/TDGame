[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: PCG 컴포넌트·서브시스템 API와 배치 노드 목록 (UE 5.8 엔진 소스 확인, 2026-09-09)

경로 기준 `Engine/Plugins/PCG/Source/PCG/Public` = `<PUB>`.

## A. UPCGComponent 생성 모드 (`<PUB>/PCGComponent.h`)
- `EPCGComponentGenerationTrigger { GenerateOnLoad, GenerateOnDemand, GenerateAtRuntime }` — L76-82.
- 프로퍼티: `int Seed = 42` L313(전용 Set 함수 없음, 직접 대입), `bActivated` L316, `bIsComponentPartitioned` L319(표시명 "Is Partitioned", 로컬 컴포넌트는 편집 불가), `GenerationTrigger` L326, `bOverrideGenerationRadii` / `FPCGRuntimeGenerationRadii GenerationRadii` L333-338, `SchedulingPolicyClass` L341, `bRegenerateInEditor` L354, `GenerationGridSize`(Transient) L628 + `Get/SetGenerationGridSize()` L277.
- 파티션 여부 조회는 `IsPartitioned()` L522 / `SetIsPartitioned(bool)` L520.
- HiGen(계층 생성)은 그래프 자산 쪽: `UPCGGraph::bUseHierarchicalGeneration`, `HiGenGridSize`(기본 Grid256), `HiGenGridSizeMultiplier`, `bUse2DGrid` — `<PUB>/PCGGraph.h:717-728`. 그리드 열거 `EPCGHiGenGrid` — `<PUB>/PCGCommon.h:519-553`. 런타임 반경 구조체 `FPCGRuntimeGenerationRadii`(그리드별 반경, `CleanupRadiusScalar`) — `PCGCommon.h:595-660`.
- 그래프 단위 결정론 토글은 없다. 결정론은 시드 결합으로 보장(F절).

## B. C++ 제어 함수·델리게이트 (`<PUB>/PCGComponent.h`)
- `GetGraph()` L186(상위 자산 — 파라미터 설정에 부적합), `GetGraphInstance()` L189, `SetGraphLocal(UPCGGraphInterface*)` L191.
- 생성: `Generate()` / `Cleanup()` L216-217, `GenerateLocal(bool bForce)` L220, `GenerateLocal(EPCGComponentGenerationTrigger, bool bForce, uint32 Grid, const TArray<FPCGTaskId>& Dependencies)` L224, `GenerateLocalGetTaskId(...)` L226-228(작업 ID 반환), `CleanupLocal(bool bRemoveComponents)` L231, `CleanupLocalImmediate(bool, bool)` L249, `CancelGeneration()` L260, `NotifyPropertiesChangedFromBlueprint()` L263.
- 델리게이트(C++): `OnPCGGraphStartGeneratingDelegate / CancelledDelegate / GeneratedDelegate / CleanedDelegate` L380-383. BP용 `...External` L386-399.

## C. 그래프 파라미터 설정
- `UPCGComponent::GetGraphInstance()` → `UPCGGraphParametersHelpers::Set*Parameter(UPCGGraphInterface*, FName, Value)` — `<PUB>/Helpers/PCGGraphParametersHelpers.h`(Float/Double/Bool/Int32/Int64/Name/String/Enum/SoftObjectPath/SoftObject/SoftClass/Object/Class/Vector/Rotator/Transform/Vector2D/Vector4). `IsOverridden` L33.
- 내부: `UPCGGraphInstance::ParametersOverrides`(`FPCGOverrideInstancedPropertyBag`) — `PCGGraph.h:959-963`; 템플릿 `SetGraphParameter<T>` — `PCGGraph.h:257-300`.
- 즉 바이옴 데이터 에셋 값을 C++에서 그래프 인스턴스 파라미터로 밀어 넣는 경로가 공식적으로 존재한다.

## D. UPCGSubsystem (`<PUB>/Subsystems/PCGSubsystem.h`)
- `GetInstance(UWorld*)` L113, `GetActiveEditorInstance()` L123(에디터), `ScheduleComponent(UPCGComponent*, uint32 Grid, bool bForce, deps)` L160, `ScheduleCleanup` L166, `GenerateAllPCGComponents(bool bForce)` L372, `CleanupAllPCGComponents(bool bPurge)` L375, `RefreshAllComponentsFiltered(TFunction<bool(UPCGComponent*)>)` L229(에디터), `OnAllComponentsGenerated` L387.
- 런타임 생성 스케줄러 `FPCGRuntimeGenScheduler` — `<PUB>/RuntimeGen/PCGRuntimeGenScheduler.h`. 생성 소스: `UPCGGenSourceComponent`(임의 액터), `PCGGenSourcePlayer`, 에디터 카메라. **`UPCGGenSourceWPStreamingSource`는 5.7에서 Deprecated** — 월드 파티션 스트리밍 소스 연계는 `FPCGGenSourceManager::UpdateWorldPartitionGenSources` 경로.
- CVar: `pcg.RuntimeGeneration.Enable / GlobalRadiusMultiplier / BasePoolSize / EnableWorldStreamingQueries`, `pcg.GraphMultithreading`, `pcg.Editor.MaxExecutingThreads(8)`, `pcg.Cache.Enabled`, `pcg.FlushCache`.

## E. 커맨드릿·에디터 유틸리티
- 전용 PCG 커맨드릿은 없다. 월드 파티션 빌더 사용: `UPCGWorldPartitionBuilder : UWorldPartitionBuilder` — `Engine/Plugins/PCG/Source/PCGEditor/Private/WorldPartitionBuilder/PCGWorldPartitionBuilder.h:135`. 커맨드라인: `<Project> <Map> -Unattended -AllowCommandletRendering -run=WorldPartitionBuilderCommandlet -Builder=PCGWorldPartitionBuilder [-IncludeGraphNames= -IncludeActorIDs= -OneComponentAtATime -IterativeCellLoading -IterativeCellSize=25600 -IgnoreGenerationErrors]`.
- 콘솔 `pcg.BuildComponents`(에디터). 메뉴 "Build > Build PCG", Tools > PCG Framework > Generate all / Cleanup all / Purge all.

## F. 결정론·시드
- `PCGHelpers::ComputeSeed(int A[, B[, C]])`, `ComputeSeedFromPosition(FVector)`, `GetRandomStreamFromSeed(int32 Seed, const UPCGSettings*, const IPCGGraphExecutionSource*)` — `<PUB>/Helpers/PCGHelpers.h:40-45`.
- 노드 시드: `UPCGSettings::Seed`(기본 0xC35A9631), `bUseSeed` — `<PUB>/PCGSettings.h:481, 564`. 실행 소스 시드 = 컴포넌트 `Seed`(`PCGComponentExecutionState.cpp:18-22`). 최종 시드 = 컴포넌트 시드 × 노드 시드 × 포인트 위치 해시.
- 따라서 "같은 입력 액터·같은 컴포넌트 시드·같은 그래프"면 결과가 같다. 컴포넌트 시드는 방/지역별로 결정론적으로 파생해 대입한다.

## G. 배치 노드 목록 (정확한 클래스·표시 이름)
샘플링: `UPCGSurfaceSamplerSettings`(Surface Sampler: `PointsPerSquaredMeter`, `Looseness`, `PointExtents`), `UPCGSplineSamplerSettings`(Spline Sampler: `Dimension = OnSpline/OnHorizontal/OnVertical/OnVolume/OnInterior`, `Mode = Subdivision/Distance/NumberOfSamples`, `InteriorSampleSpacing`, `SeedingMode`), `UPCGVolumeSamplerSettings`, `UPCGCreatePointsGridSettings`, `UPCGGetLandscapeSettings`(Get Landscape Data: `SamplingProperties.bGetHeightOnly/bGetLayerWeights`), `UPCGWorldRaycastElementSettings`(World Raycast), `UPCGWorldQuerySettings`.
필터·프루닝: `UPCGDensityFilterSettings`(Density Filter), `UPCGAttributeFilteringSettings`(Filter Attribute Elements, 별칭 Point Filter), `UPCGAttributeFilteringRangeSettings`, `UPCGSelfPruningSettings`(Self Pruning: `PruningType`, `RadiusSimilarityFactor`, `bRandomizedPruning`), `UPCGDistanceSettings`(Distance: `bOutputToAttribute`, `MaximumDistance`, `bSetDensity`), `UPCGDifferenceSettings`(Difference), `UPCGOuterIntersectionSettings`(Intersection), `UPCGBoundsModifierSettings`, `UPCGFilterByTagSettings`(Filter Data By Tag), `UPCGCullPointsOutsideActorBoundsSettings`, `UPCGProjectionSettings`.
노이즈·변형: `UPCGSpatialNoiseSettings`(Spatial Noise: Perlin2D/Caustic2D/Voronoi2D/FractionalBrownian2D/EdgeMask2D), `UPCGAttributeNoiseSettings`(Attribute Noise, 별칭 Density Noise), `UPCGAttributeRemapSettings`(Attribute Remap; Density Remap은 5.5부터 Deprecated), `UPCGTransformPointsSettings`(Transform Points).
액터·태그: `UPCGDataFromActorSettings`(Get Actor Data: `ActorSelector.ActorFilter = Self/Parent/Root/AllWorldActors/Original/FromInput(5.8 추가)`, `ActorSelection = ByTag/ByClass`, `ActorSelectionTag`), `UPCGGetSplineSettings`(Get Spline Data), `UPCGGetVolumeSettings`, `UPCGGetPrimitiveSettings`, `UPCGGetActorPropertySettings`.
출력: `UPCGStaticMeshSpawnerSettings`(Static Mesh Spawner) + `UPCGMeshSelectorWeighted` / `UPCGMeshSelectorByAttribute` / `UPCGMeshSelectorWeightedByCategory`(바이옴별 분기에 적합), `UPCGSpawnActorSettings`(Spawn Actor), `UPCGCopyPointsSettings`, `UPCGPointMatchAndSetSettings`, `UPCGMetadataPartitionSettings`(Attribute Partition), `UPCGSubgraphSettings`, `UPCGLoopSettings`, `UPCGHiGenGridSizeSettings`(Set/Change Grid Size).
5.6~5.8 유용 노드: `UPCGPathfindingSettings`(Pathfinding: 도로 생성에 직결, `bOutputAsSpline`), `UPCGClusterSettings`(Cluster, K-means), `UPCGAttractSettings`, `UPCGPointNeighborhoodSettings`, `UPCGBoundsFromMeshSettings`, Grammar 계열(`Subdivide Spline/Segment`, `Select Grammar`, `Spline to Segment`), `UPCGElevationIsolinesSettings`. `UPCGMeshSamplerSettings`와 `Primitive Cross-Section`은 PCGGeometryScriptInterop 플러그인.

## H. 경사·고도 필터
- "Slope" 노드는 없다. 표준 경로: Get Landscape Data(`bGetHeightOnly=false`로 노멀 포함) → Surface Sampler → `UPCGNormalToDensitySettings`(Normal To Density: `Normal`, `Offset`, `Strength`, `DensityMode`) → Density Filter. 랜드스케이프 레이어 가중치는 `bGetLayerWeights`로 속성이 붙으며 Filter Attribute Elements로 거른다.

## I. 목적별 노드 매핑 (이 프로젝트)
- 바이옴별 에셋: Get Landscape Data → Surface Sampler → 레이어 가중치 필터 → Static Mesh Spawner + WeightedByCategory.
- 도로 주변 배제: Get Spline Data → Spline Sampler(OnHorizontal) → Difference, 또는 Distance + Filter.
- POI 주변 배제: Get Actor Data(AllWorldActors, ByTag) → Distance → Filter, 또는 Cull Points Outside Actor Bounds.
- 전투 공간 확보: Self Pruning(LargeToSmall) + Bounds From Mesh, 보조로 Point Neighborhood.
- 던전 방 드레싱: Get Volume/Primitive Data → Volume Sampler 또는 Create Points Grid → Attribute Partition → Point Match And Set → Static Mesh Spawner. 벽면은 Primitive Cross-Section + Grammar.

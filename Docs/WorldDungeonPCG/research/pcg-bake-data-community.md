[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: PCG 베이크·데이터 에셋·인터롭·커뮤니티 사례 (UE 5.8, 2026-09-09)

경로 기준 `Engine/Plugins/PCG/Source` = `PCGSRC`.

## 1. 플러그인 위치와 상태
| 플러그인 | 위치 | 상태 |
|---|---|---|
| PCG | `Engine/Plugins/PCG` | 5.7부터 "production-ready" |
| PCGExternalDataInterop(Alembic), PCGGeometryScriptInterop, PCGPythonInterop | `Engine/Plugins/PCGInterops` | External Data는 Beta, 기본 비활성 |
| PCGNiagaraInterop, PCGWaterInterop, PCGInstancedActorsInterop, PCGFastGeoInterop, PCGNaniteAssembliesInterop | `Engine/Plugins/Experimental/PCGInterops` | Experimental |
| PCGBiomeCore, PCGBiomeSample | `Engine/Plugins/Experimental` | Experimental, C++ 없음(전부 콘텐츠·유저 정의 구조체) |
- **CSV 로드 노드는 없다.** CSV는 `UDataTable`로 임포트 후 `Load Data Table`(`PCGSRC/PCG/Public/Elements/IO/PCGDataTableElement.h:21`) 또는 `Data Table Row To Attribute Set`(`PCGDataTableRowToParamData.h:14`)으로 읽는다.

## 2. 베이크(영구화)
- 공식 "Bake" 기능은 없고 두 가지로 대체한다.
  1. 에디터 편집 모드 `EPCGEditorDirtyMode::Normal`로 생성하면 ISM 컴포넌트가 파티션 액터(`APCGPartitionActor`, `PCGSRC/PCG/Public/Grid/PCGPartitionActor.h:25`) 또는 소유 액터에 붙어 저장된다. `GenerateOnLoad`는 이미 생성된 결과가 있으면 재생성하지 않는다(Epic 답변). `Preview`/`LoadAsPreview`는 저장하지 않는다(`PCGCommon.h:588-593`).
  2. 링크 끊기: `UPCGComponent::ClearPCGLink(UClass* TemplateActor)` — `PCGComponent.h:270-275`, 구현 `PCGComponent.cpp:1206-1290`(새 액터 기본 라벨 `<원본>_PCGStamp`, 레벨·데이터 레이어·HLOD·폴더 승계). 전역 메뉴 "Clear link for all PCG components"(`PCGEditorModule.cpp:1050`), 서브시스템 `ClearLinkForAllPCGComponents`(`PCGSubsystem.cpp:1962`). 에디터 모드 툴 "Isolate"(`pcg.tool.Isolate <ActorPath> <Tag…>`, `UPCGActorHelpers::IsolateFromActor` `PCGActorHelpers.h:271`)는 `APCGIsolatedActor`로 옮긴다.
- 관리 리소스: `UPCGManagedResource` 계층(`PCGManagedResource.h:84-473`), `Release/ReleaseIfUnused/MoveResource`, `FPCGMoveResourceParams{TemplateTargetClass, Target, RequiredTags, ExcludedTags, bAttachToParent, DefaultName}` `:28-57`.
- Static Mesh Spawner 옵션(`PCGStaticMeshSpawner.h:73-136`): `MeshSelectorType/Parameters`, `StaticMeshComponentPropertyOverrides`, `TargetActor`, `bApplyMeshBoundsToPoints`, `PostProcessFunctionNames`. Spawn Actor(`PCGSpawnActor.h:16-22`): `EPCGSpawnActorOption { CollapseActors(기본), MergePCGOnly, NoMerging }`, `TemplateActorClass`, `bSpawnByAttribute`, `DataLayerSettings/HLODSettings`. Spawn Actor로 레벨 인스턴스 클래스 스폰 가능(Epic 확인).
- 5.8 신기능: Data Overrides(비파괴 수동 편집: 선택·제외·수정·복원), 복합 속성(배열·구조체), 임베디드 서브그래프, 파라미터 계층 편집기, 컴포넌트리스 GPU 스캐터(충돌·내비 불가 → 게임플레이 오브젝트는 CPU 경로).
- 이 프로젝트: 최종 베이크 = Normal 모드 생성 + 저장(파티션 액터에 ISM 유지). 수동 보정이 필요한 POI 소품은 Spawn Actor 또는 Clear PCG Link로 액터화.

## 3. 데이터 에셋과 파라미터
- `UPCGDataAsset`(`PCGDataAsset.h:43-109`): `FPCGDataCollection Data`, `ObjectPath`, `Category`, `CachedPins`. 노드 `Load PCG Data Asset`(`PCGLoadAssetElement.h:17`), `Save to PCG Data Assets`(5.5), `Get Asset List`. 레벨 → 에셋: 메뉴 "Create PCG Assets from Level(s)"(`PCGEditorMenuUtils.cpp:63-82`), `UPCGAssetExporterUtils::CreateAsset`(`PCGAssetExporterUtils.h:19-30`). Electric Dreams의 "어셈블리"가 이 방식(손으로 만든 구조물을 점군으로 재배치, POI 배치에 응용).
- 그래프 파라미터는 `FInstancedPropertyBag`(`PCGGraph.h:77-117`) 기반이라 구조체·오브젝트도 가능. `Get Property From Object Path`(`PCGGetPropertyFromObjectPath.h:15`), `Get Actor Property`(`PCGGetActorProperty.h:19`)로 데이터 에셋·액터 값 읽기. C++ 설정은 `UPCGGraphParametersHelpers`(pcg-api-and-nodes.md C절).
- 주의: 런타임 블루프린트 파라미터 변경이 Custom HLSL 노드에 반영되지 않는다는 보고(2026-07, 미해결). 파라미터 주입 후 결과 동일성은 프로젝트에서 직접 검증.

## 4. Biome Core (5.8 Experimental)
- 구조: Biome Definition(이름·색·우선순위, 값이 작을수록 높음) / Biome Asset(Generator 참조, 메시·어셈블리·액터 클래스, 가중치) / Generator(타입·우선순위·그래프) / Biome 액터(Volume·Spline·Texture). Local Biome Core 그래프 → Global Biome Core 그래프가 우선순위 차집합으로 겹침 정리. Exclusion은 태그 `PCG_BiomeExclusion` + 컴포넌트 태그 `BiomeExclusion`.
- 5.6 v2: 액터 로컬 바이옴 에셋, 바이옴 블렌딩·레이어링. 업그레이드 시 "global refresh" 필요.
- 이 프로젝트: 코어 로직 종속 금지(설계서 R-31). 구조(정의/에셋/생성기 3분리, 우선순위 차집합, 태그 기반 배제)만 자체 `UTDBiomeDefinition` + 그래프에 옮긴다. 필드명은 uasset이라 에디터에서 열어 확인(미확인).

## 5. 레벨 인스턴스 안의 PCG
- 편집 모드가 아닌 레벨 인스턴스 내부 컴포넌트는 그래프 변경이 잠긴다(`PCGGraph.cpp:3021-3029`). 파티션은 소유자가 파티션 액터가 아니고 월드 파티션이 있으면 가능(`PCGComponent.cpp:129-137, 2005-2013`). 레벨 인스턴스 내부 랜드스케이프는 PCG가 감지 못 함(2026-04 사례).
- 룸 드레싱: 룸 레벨 안에 PCG 컴포넌트(비파티션, Normal 모드, `Seed`를 방별로 대입) → 룸 레벨 저장 시 결과 저장. 방 인스턴스별 변형이 필요하면 베이커가 인스턴스 배치 후 파티션 없는 컴포넌트를 스폰해 시드 대입·생성·저장.

## 6. 커뮤니티 교훈 (2025~2026)
- 런타임 HiGen + GPU 스폰도 `PrepareForExecute`/CRC 비용으로 예산 초과 사례(PS5). 순수 런타임이면 `pcg.cache.runtime.enabled 0`, FastGeo Interop 권장.
- `Is Partitioned` + PIE에서 생성 안 됨 → 출력 로그 우선 확인. 파티션 + 런타임 조합은 별도 검증.
- 멀티플레이 클라이언트 생성 안 됨 → 각 클라이언트 캐릭터에 `PCGGenSourceComponent`.
- 스플라인 배제 대신 랜드스케이프 레이어 가중치로 배제(`bGetLayerWeights` + Filter).
- 대량 ISM 저장은 OFPA 파티션 액터 파일 수 증가 → 소스 컨트롤 부담.
- 런타임 던전 스티칭은 격자 단위 규격화가 충돌 검사보다 효과적.

## 7. 결정론 (요약)
- 최종 시드 = 컴포넌트 `Seed` × 노드 `Seed`(`bUseSeed`) × 포인트 위치(`Mutate Seed`). 그래프 단위 토글 없음. 노드 `DeterminismSettings`로 네이티브/BP 결정론 테스트 부착 가능(문서 미비). 볼륨 이동 시 위치 시드가 바뀌므로 아틀라스 슬롯 위치를 고정한 뒤 시드 확정.

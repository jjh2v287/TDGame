# P3-07 Forest 바이옴 PCG 그래프 — 생성·적용·결정론 검증 (2026-09-12, Claude)

스크립트: `Tools/WorldGen/editor_make_pcg_biome.py`(그래프·볼륨 생성), `Tools/WorldGen/editor_check_pcg_determinism.py`(결정론 검사).
실행 순서: `python Tools/run_in_editor.py Tools/WorldGen/editor_make_pcg_biome.py` → `python Tools/run_in_editor.py Tools/WorldGen/editor_check_pcg_determinism.py` → `Saved/WorldGen/pcg_determinism.json` 확인.

## 1. 만든 에셋

| 경로 | 노드 수 | 역할 |
|---|---|---|
| `/Game/World/PCG/PCG_TDBiome_Forest` | 35 | 메인. 랜드스케이프 샘플링·경사·배제 후 서브그래프 5개 호출 + 드문 나무 가지(HiGen 1024) |
| `/Game/World/PCG/PCG_TDVegetation` | 6 | Rock 가중치 < 0.5 → 15 % 유지 → 덤불·드레드플랜트·균사 |
| `/Game/World/PCG/PCG_TDRockScatter` | 6 | Rock 가중치 > 0.2 → 20 % 유지 → 작은 바위 |
| `/Game/World/PCG/PCG_TDClutter` | 5 | 7 % 유지 → 뼈·해골·나뭇가지·목재 조각 |
| `/Game/World/PCG/PCG_TDRoadside` | 6 | Road 가중치 > 0.05(도로 가장자리) → 15 % 유지 → 돌·나뭇가지 |
| `/Game/World/PCG/PCG_TDPoiDressing` | 5 | POI 고리(이격 반지름 40~100 %) 점 60 % 유지 → 통·상자·양동이·해골 |
| `/Game/Dungeon/PCG/PCG_TDRoomDressing` | 7 | P2-11 선행: Get Volume Data(Self) → Volume Sampler(150×150×400) → 12 % 유지 → 통·상자·뼈·양 해골. 비파티션 전제, 레벨 적용은 안 함 |

`DA_TDBiome_Forest.pcg_graph`에 메인 그래프를 연결했다.

## 2. 노드 구성(실제 사용한 세팅 클래스)

메인 `PCG_TDBiome_Forest` (`use_hierarchical_generation=True`, `hi_gen_grid_size=GRID1024`):
- `PCGHiGenGridSizeSettings`(GRID256) ← Input.In. 이 노드의 Out을 아래 모든 BoundingShape 핀에 연결해 256 m 격자에서 실행.
- `PCGGetLandscapeSettings`(`sampling_properties.get_height_only=False, get_layer_weights=True`) → `PCGSurfaceSamplerSettings`(0.04 pt/m², extents 50) → `PCGNormalToDensitySettings`(SET) → `PCGDensityFilterSettings`(lower=cos 30° = 0.866).
- 배제: `PCGDataFromActorSettings`(태그 TDPoi, AllWorldActors, ByTag, GetSinglePoint) → `PCGBoundsModifierSettings`(SET ±1000 cm, z ±3000) / `PCGDataFromActorSettings`(TDExclusion, ParseActorComponents) / `PCGGetSplineSettings`(TDRoad) → `PCGSplineSamplerSettings`(OnSpline, Distance 800) → `PCGBoundsModifierSettings`(SET ±800) → 셋 다 `PCGDifferenceSettings`(DISCRETE) Differences 핀.
- `PCGAttributeFilteringSettings`(Road < 0.15) InsideFilter → `PCGSubgraphSettings` × 3(Vegetation, RockScatter, Clutter). 배제 직후 점 → Roadside 서브그래프.
- POI 고리: Difference(샘플 − POI존) → Difference(샘플 − 바깥점) = 존 안 점 → Difference(− 존 0.4배 안쪽) → PoiDressing 서브그래프.
- 나무 가지(기본 격자 1024에서 실행, Input.In 직접 연결): Get Landscape → Surface Sampler(0.0004 pt/m², extents 100) → Normal To Density → Density Filter → Difference(POI 존 ×1.5, TDExclusion) → Attribute Filter(Road < 0.15) → Bounds Modifier(SET ±185 = 최소 나무 간격 370/2) → `PCGSelfPruningSettings`(LargeToSmall) → `PCGTransformPointsSettings`(yaw 0~360, scale 0.9~1.3) → `PCGStaticMeshSpawnerSettings`(`PCGMeshSelectorWeighted`, Birch 1/2/3).

서브그래프 공통 체인: (속성 필터) → `PCGAttributeNoiseSettings`(SET, $Density 0~1) → `PCGDensityFilterSettings`(lower = 1 − 유지비율) → `PCGSelfPruningSettings` → `PCGTransformPointsSettings` → `PCGStaticMeshSpawnerSettings`(Weighted, `PCGSoftISMComponentDescriptor.static_mesh`, cull 8~30 km).

## 3. 파라미터

| 파라미터 | 값 | 출처 | 들어간 곳 |
|---|---|---|---|
| 최대 경사 | 30° → 밀도 하한 0.866 | `DA_TDBiome_Forest.max_slope_deg` | Density Filter ×3 |
| 최소 나무 간격 | 370 cm | `min_tree_distance_cm` | 나무 Bounds Modifier(±185) + Self Pruning |
| POI 이격 | 1000 cm(나무 1500) | `poi_clearance_cm` | POI Bounds Modifier |
| 도로 이격 | 800 cm | `road_clearance_cm` | 도로 Spline Sampler 간격·Bounds Modifier |
| 나무 밀도 | 8/100 m² → 0.0004 pt/m²로 상한 | `tree_density_per100_sq_m` | 나무 Surface Sampler |
| 기본 샘플 밀도 | 0.04 pt/m² | 스크립트 상수 | 메인 Surface Sampler |
| 유지 비율 | 식생 0.15, 바위 0.2, 잔해 0.07, 도로변 0.15, POI 0.6 | 스크립트 상수 | 서브그래프 Density Filter |

**그래프 사용자 파라미터는 만들지 못했다(C++ 주입 필요).** `UPCGGraph::UserParameters`(`InstancedPropertyBag`)에 프로퍼티를 추가하는 함수가 Python에 없다. `PCGGraphParametersHelpers.set_float_parameter(graph, "Density", 0.5)`는 예외 없이 무시되고 로그에 `LogPCG: Error: 파라미터 Density 존재하지 않습니다`만 남는다. `InstancedPropertyBag.import_text(...)`도 True를 반환하지만 비어 있다. 따라서 값은 위 표대로 노드 프로퍼티에 직접 넣었고, `UTDBiomePcgBinder`(C++)가 `AddUserParameters` + `UPCGGraphParametersHelpers`로 바인딩하는 일은 후속 작업이다.

## 4. 볼륨·컴포넌트

- 액터 `TDGen_PCG_BiomeForest`(`PCGVolume`, 폴더 `TDGen/PCG`, 태그 TDGen/TDGenPCG, `is_spatially_loaded=False`).
- 범위: 중심 (0, 0, 3410.5) cm, 반경 (50400, 50400, 4248) cm = 랜드스케이프 프록시 64개 bounds 합(±504 m, z −337~6159) + z 여유 −500/+1500.
- 컴포넌트: `is_component_partitioned=True`, `seed=7`, `generation_trigger=GenerateOnLoad`, 그래프 지정, `generate_local(True)`.
- 생성 결과 파티션 액터: `PCGPartitionGridActor_25600_*` 16개(256 m 격자) + `PCGPartitionGridActor_102400_*` 4개(1024 m 격자, 나무). 생성 시간 약 6 초.

## 5. 생성 인스턴스 수 (시드 7)

총 **9,255개** (기존 `ATDInstancedMeshActor` HISM 78,834개와 별도). 메시별:
- 식생 4,513: SM_Bush 1538, SM_Dreadplant1 968, SM_Dreadplant2 994, SM_DreadplantMushroom1 467, SM_Mycelium1 546
- 바위 1,382: SM_Rock_3 468, SM_Rock_5 442, SM_Rock_7 310, SM_Rock1 162
- 잔해 2,424(도로변 나뭇가지 포함): SM_Bone 807, SM_Skull 280(POI 4 포함), SM_WoodenStick 1064, SM_WoodenPart1 273
- 도로변: SM_Rock_4 148, SM_Rock_6 94
- POI 드레싱 81: SM_Barrel1_Empty 32, SM_WoodenCrate2 28, SM_Bucket 8, SM_WoodenPart3 13 — 가장 가까운 POI까지 중앙값 9.2 m, 최대 13.6 m
- 나무 613: SM_Birch1 275, SM_Birch2 220, SM_Birch3 118 — POI 15 m 안 0개

## 6. 결정론 검사 (cleanup → generate 2회, 같은 시드 7)

| 회차 | 인스턴스 | 위치 해시(sha1, 0.1 cm 반올림·정렬) | 완료 판정 |
|---|---|---|---|
| 1 | 9,255 | `7d28536be724877354c1a836bc10cfcd9ea7c591` | `on_pcg_graph_generated_external` 델리게이트, 6 s |
| 2 | 9,255 | `7d28536be724877354c1a836bc10cfcd9ea7c591` | 델리게이트, 6 s |

`identical: true`. 원본 JSON: `Saved/WorldGen/pcg_determinism.json`.

## 7. 실패·우회한 API

| 시도 | 결과 | 우회 |
|---|---|---|
| `PCGAttributePropertyInputSelector.set_attribute_name("Road")` / `import_text("Road")` | 예외 없이 무시(`@Last` 유지) → 필터가 기본 `$Density`로 동작해 초기 결과가 전부 어긋남 | `selector.import_text("PCGBegin(Road)PCGEnd")` 후 `set_editor_property` |
| `PCGAttributePropertySelectorBlueprintHelpers.set_attribute_name(sel, name)` | 반환형이 기본 `PCGAttributePropertySelector`라 `target_attribute`(InputSelector)에 대입 불가 (`Cannot nativize`) | 위와 같음 |
| Surface Sampler `Bounding Shape`에 점 데이터(POI 존) 연결 | 점 bounds로 제한되지 않고 합집합 상자 전체를 샘플링 | 메인 그래프에서 Difference 두 번(샘플 − (샘플 − 존)) |
| `PCGIntersectionSettings` In에 샘플+POI 존 | 제한 효과 없음(원인 미확인) | Difference 두 번 |
| `PCGHiGenGridSizeSettings`(GRID1024) + 그래프 기본 GRID256 | 경고 "그리드 크기가 그래프 디폴트보다 크므로 범위 제한" | 그래프 기본을 가장 큰 격자(1024)로, 조밀 체인은 Grid Size 노드(256) |
| `EditorAssetLibrary.delete_asset` (메인 그래프) | 파티션 액터의 로컬 컴포넌트가 참조 중이라 False | 삭제 실패 시 노드만 전부 `remove_node`하고 재사용 |
| `PCGGraph.remove_nodes(list)` | Python에서 인자 없음(`takes no arguments`) | `remove_node` 반복 |
| `PCGVolume` 스폰 직후 `get_actor_bounds`로 스케일 계산 | 브러시 bounds가 아직 갱신 전이라 25/10배 작은 볼륨 | 스케일 10을 먼저 넣고 측정한 단위 extent로 재계산 |
| `PCGComponent.generated` | 파티션 컴포넌트는 항상 False | 델리게이트 + 인스턴스 수 안정(10 s) + 타임아웃 |
| `PCGGraphParametersHelpers.set_float_parameter` (없는 파라미터) | 무시, `LogPCG: Error 파라미터 … 존재하지 않습니다` | 사용자 파라미터 생성은 C++ 몫 |
| `PCGTaggedData.data`(Python) | `PCGDataPtrWrapper`라 직접 접근 불가 | `PCGDataFunctionLibrary.get_typed_inputs(collection, PCGBasePointData)` |

남은 경고: `Surface Sampler - too many data items arriving on single data pin 'Bounding Shape'` — Input 노드 In 핀에 `GetPCGData`와 `GetInputPCGData`가 같은 액터 데이터를 두 번 넣기 때문(엔진 `FPCGFetchInputElement`). 첫 항목만 쓰이며 결과는 결정적이다.

## 8. 남은 일
- `UTDBiomePcgBinder`(C++): `UPCGGraph::AddUserParameters`로 밀도·간격·경사·이격 파라미터를 만들고 `DA_TDBiome_Forest`에서 주입. 그때 서브그래프 Density Filter의 유지 비율도 파라미터로 승격.
- 레벨에 `ATDRoadSplineActor`/`ATDExclusionVolume`이 아직 없어 도로·배제 경로는 빈 입력으로만 검증됨(그래프는 통과). 베이크 후 재생성(`editor_regenerate_pcg.py`) 필요.
- `PCG_TDRoomDressing`은 룸 레벨에 비파티션 `PCGComponent`로 붙여 검증하지 않았다(P2-11).
- 캡처 검증(뷰포트)은 하지 않았다.

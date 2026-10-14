# Phase 3 — 야외 생성기

| 상태 | 개수 |
|---|---|
| todo | 3 |
| doing | 7 |
| done | 8 |

목표(설계서 Phase 3): 500m×500m 필드에 마을 1, 던전 3, POI 8을 생성하고 도로 스플라인·배제 마스크·Forest 바이옴 PCG·월드 검증·시드 후보 선택까지 완성한다.

### P3-00 랜드스케이프 생성 C++ 에디터 함수와 높이맵 파이프라인
- 상태: done
- 우선순위: 높음
- 선행: P0-01, P0-02
- 목표: 월드 기본 바탕(지형)을 결정론적으로 만들 수 있게 `ALandscape::Import`를 감싼 에디터 함수와 높이맵 생성 경로를 만든다.
- 완료 조건:
  - `UTDLandscapeEditorLibrary::CreateLandscape(UWorld*, FTransform, int32 ComponentsX, int32 ComponentsY, int32 QuadsPerSection, int32 SectionsPerComponent, const TArray<uint16>& Heights)`(BlueprintCallable, `TDGameEditor`)와 `ImportHeightmapFromTexture` 보조 함수
  - `TDWorldGen`에 `FTDHeightmapGenerator`(지역 정의 + 수작업 고도 앵커 + 노이즈 → uint16 배열, 시드 결정론)
  - 월드 파티션 큰 월드용 `LandscapeStreamingProxy` 분할 옵션(그리드 크기 배수)
  - 도로·강은 `editor_apply_spline`/랜드스케이프 스플라인, 바이옴 마스크는 웨이트맵 렌더타깃 임포트로 쓰는 절차를 문서화
  - Python 원격 실행 또는 커맨드릿에서 호출해 500m×500m 지형 생성 확인
  - **정정(2026-09-30 D-40)**: 높이맵 정본은 numpy 생성기(`Tools/WorldGen/generate_ashen_vale.py`), 랜드스케이프 생성은 raw 입력 `CreateLandscapeFromRawFiles`로 한다. 배열 입력 `CreateLandscape`·`ImportHeightmapFromTexture`·C++ `FTDHeightmapGenerator`는 만들지 않는다.
- 산출물: `Source/TDGameEditor/.../TDLandscapeEditorLibrary.h/.cpp`, `Source/TDWorldGen/.../TDHeightmapGenerator.h/.cpp`
- 검증: 같은 시드 2회 생성 시 높이 배열 해시 동일, 에디터에서 생성된 컴포넌트 수·바운드 확인
- 참조: research/landscape-mcp-test.md, R-20, R-32
- 기록: 2026-09-11 작성(MCP UI 자동화로 생성 가능함을 실측했으나 재현성 위해 C++ 경로 채택)
  2026-09-12(Claude): `UTDLandscapeEditorLibrary::CreateLandscapeFromRawFiles(WorldContext, FTDLandscapeCreateRequest)` 구현·검증 — raw 높이맵(uint16)·레이어 raw(uint8) 파일을 읽어 `ALandscapeProxy::Import` → `UE::Landscape::CreateTargetLayerInfo`로 레이어 인포 자동 생성 → `ULandscapeSubsystem::ChangeGridSize`로 월드 파티션 프록시 분할. 1009×1009, 6레이어, 그리드 2 → 프록시 64개 생성 확인(약 25초). 높이맵 생성기는 C++ `FTDHeightmapGenerator` 대신 numpy 프로토타입(`Tools/WorldGen/generate_ashen_vale.py`)으로 먼저 만들었음. 남은 조건: C++ 생성기 이식 여부 결정, 시드 재현성 해시 테스트, 문서화.
  2026-09-30 검증 종료(claude): 조건을 D-40으로 정정(높이맵 정본 = numpy `generate_ashen_vale.py`, 랜드스케이프 = raw 입력 `UTDLandscapeEditorLibrary::CreateLandscapeFromRawFiles`, 배열 입력·텍스처 임포트 함수와 C++ `FTDHeightmapGenerator`는 만들지 않음). 시드 7 2회 생성 SHA-256 동일(높이·레이어 6종·layout), 에디터 측정 랜드스케이프 1·프록시 64·컴포넌트 256·1,008m×1,008m, 절차 문서 `Tools/WorldGen/README.md` 'P3-00' 절. 증거 `Docs/Validation/P3-00-landscape.md`.

### P3-11 잿빛 골짜기(Ashen Vale) 메인 지역 프로토타입 생성·베이크
- 상태: doing
- 우선순위: 높음
- 선행: P3-00
- 목표: 설계서 4장 원칙(뼈대 수작업 + 환경 자동)으로 `LV_DarkFantasy_OpenWorld` 메인 지역 1008m×1008m를 전체 밀도로 채워 플레이 가능한 상태로 만든다.
- 완료 조건:
  - `Tools/WorldGen/generate_ashen_vale.py`(시드 결정론): 지형·6레이어·도로·강·호수·마을·POI 8·던전 입구 3·전투 공터 3·교량·널길·숲/바위/잔해 산포·물/안개 평면·조명·FX·마커
  - `Tools/WorldGen/editor_build_open_world.py`: 레벨 정리 → 랜드스케이프 → 대기 → `ATDInstancedMeshActor`(HISM) 셀 → 액터/조명/FX/마커 → 저장
  - 랜드스케이프 레이어 머티리얼 `M_TD_Landscape` + 풀 타입 4종(`editor_make_landscape_material.py`)
  - 에디터 캡처로 마을·숲·다리·묘지·습지·던전 입구 확인, `Docs/Validation/`에 캡처 기록
  - PIE에서 플레이어 스타트에서 걸어다니며 내비메시·충돌 확인
- 산출물: `Tools/WorldGen/*`, `Source/TDGame/World/Generation/TDInstancedMeshActor.*`, `Content/World/Landscape/*`, 레벨 `Content/Level/LV_DarkFantasy_OpenWorld.umap`
- 검증: 생성 로그(나무 약 3.7만, 인스턴스 약 7.8만, 고유 액터 약 1,100, 조명 97), 캡처
- 참조: R-20, R-21, R-30, 설계서 4.1·4.2, decisions D-05
- 기록: 2026-09-12 작성·진행(Claude). 최종 베이크 저장(액터 3,060, 인스턴스 79,054). 캡처·PIE 검증은 `Docs/Validation/P3-11-ashen-vale.md`. 남은 조건: 게임 카메라 기준 조명 재튠, 던전 입구 액터 교체(P1-03), 잠금(P3-06).

### P3-01 월드·지역·바이옴·POI 정의 에셋 타입
- 상태: done
- 우선순위: 높음
- 선행: P0-02
- 목표: `UTDWorldDefinition`, `UTDRegionDefinition`, `UTDBiomeDefinition`, `UTDPoiArchetype`를 `TDWorldGen`에 만든다.
- 완료 조건: 설계서 4.2·5.2 필드 반영, `DA_TDWorld_Main`, `DA_TDRegion_Forest`, `DA_TDBiome_Forest`, POI 원형 4종 에셋, `IsDataValid` 검사
- 참조: R-22, R-31, 03-architecture 3.2
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDWorldDefinition/UTDRegionDefinition/UTDBiomeDefinition/UTDPoiArchetype`(`TDWorldGen/Public/World/TDWorldDefinitions.h`, IsDataValid) + 에셋 생성 스크립트 `editor_make_definitions.py`(DA_TDWorld_Main, DA_TDRegion_Forest, DA_TDBiome_Forest, POI 4종).

### P3-02 수작업 앵커 액터
- 상태: doing
- 우선순위: 높음
- 선행: P3-01
- 목표: 마을·메인 던전·랜드마크·지역 경계를 디자이너가 놓는 앵커 액터(`ATDWorldAnchor`, 종류 열거, 배제 반경)와 지역 볼륨(`ATDRegionVolume`)을 만든다.
- 완료 조건: 액터 배치 가능, 생성기가 앵커를 입력으로 읽음, 앵커는 재생성에서 절대 이동·삭제되지 않음
- 참조: R-20, R-101(잠금)
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `ATDWorldAnchorActor`, `ATDRegionVolume`, `ATDPoiAnchor`, `ATDExclusionVolume`, `ATDRoadSplineActor` 런타임 액터 구현. 앵커는 정의 에셋 `HandAuthoredAnchors`로도 입력 가능. 생성기 입력 연결·잠금 보존은 남음.

### P3-03 월드 그래프 생성기 `FTDWorldGraphGenerator`
- 상태: done
- 우선순위: 높음
- 선행: P3-02
- 목표: 지역 정의 + 앵커 + 시드 → 사이드 던전 입구 후보, POI 후보, 이벤트 영역, 전투 공간을 포아송 디스크 샘플링으로 배치한다(`FTDWorldLayout`).
- 완료 조건: 최소 간격·앵커 배제 반경·지형 조건(경사·수면, 랜드스케이프 샘플은 에디터 단계에서 주입) 준수, 결정론 테스트, 지역별 수량 범위 충족
- 참조: R-21, R-32
- 기록: 2026-09-09 작성 / 2026-09-12(Claude) C++: `TDWorldGraphGenerator.cpp`(앵커 고정 → 포아송 디스크 → 입구/POI/공터/이벤트 배치, 결정론 GUID), 테스트 시드 1~50 통과율 100%.
  2026-09-30 검증 종료(claude): 테스트 `PlacementAvoidsWaterSteepSlopeAndAnchorExclusion`(합성 지형 시드 1~50 배치 822개 중 수면·경사 위반 0, 지형 없을 때는 287개가 금지 위치 → 필터 실효 확인, 앵커 배제 반경 안 0, Arena·EventArea 포함 간격 충족)·`PlacementCountsWithinRegionRanges`(시드 1~50 할당량·Arena·EventArea·SideDungeon 수량 범위 충족) 추가, `SameSeedProducesSameHash`·`FlatRegionPassRate` 50/50 유지. 75/75, 근거 `Saved/AgentOps/20260930/fix-world.md`.

### P3-04 도로 생성기 `FTDRoadGenerator`
- 상태: done
- 우선순위: 높음
- 선행: P3-03
- 목표: 마을·메인 POI·던전 입구를 잇는 도로 스플라인 점을 만든다.
- 완료 조건: 최소 신장 트리 + 보조 연결(설정 비율), 경사 비용 가중 A*(격자 코스트 맵은 에디터가 랜드스케이프에서 샘플해 전달), 도로 폭·배제 폭 출력
- 참조: R-21, R-32(스플라인 샘플링)
- 기록: 2026-09-09 작성 / 2026-09-12(Claude) C++: `TDRoadGenerator.cpp`(격자 A* 경사·수면 비용, 프림 MST + 보조 도로, 20m 재표본), 테스트 `RoadsReachAllEntrancesFromTown` 통과.
  2026-09-30 검증 종료(claude): 도로 노드에 Landmark 앵커 포함(`TDRoadGenerator.cpp` CollectNodes, 에디터는 잠긴 POI·입구를 Landmark로 넘김), 월드 검증기 road_reach 대상에도 포함(`TDWorldValidator.cpp` CollectLandmarkTargets). 테스트 `RoadsReachLandmarkAnchors`(주 도로 = 노드−1, 보조 = round(0.3×주), 먼 Landmark는 road_reach 오류)·`RoadsFollowGentlePassThroughSteepBand`(지형 있으면 최대 |Y| 15,400cm 통로 우회, 평지 200cm) 추가. 75/75.

### P3-05 월드 검증기 `FTDWorldValidator`
- 상태: doing
- 우선순위: 높음
- 선행: P3-04
- 목표: 입구 간격, POI 밀도, 도로 도달성, 지형 적합성, 슬롯 로딩 범위 중첩, 무콘텐츠 이동 구간을 검사한다.
- 완료 조건: 리포트에 위치·원인, 시드 1~200 통과율 보고
- 참조: R-91
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 프로토타입 `Tools/WorldGen/validate_world.py`로 6항목(입구 간격·POI 밀도·도로 도달성·지형 적합성·스트리밍 슬롯 중첩·플레이 밀도) 구현, report.md·validation.json·점수. 첫 실행에서 설계 결함 4건(부두 도로 거리, 뼈 구덩이 수면, 슬롯 간격, 무콘텐츠 구간)을 찾아 생성기 좌표·슬롯 규약(간격 300m)·길제단 3곳 추가로 수정 → 100/100 통과. C++ `FTDWorldValidator` 이식과 시드 1~200 통과율은 남음. / 2026-09-12(Claude) C++: `TDWorldValidator.cpp` 6항목(가중치 15/15/25/20/10/15) 이식, 테스트 통과. 시드 1~200 통과율 보고는 남음.

### P3-06 월드 베이커 `FTDWorldBaker`
- 상태: done
- 우선순위: 높음
- 선행: P3-05, P2-09
- 목표: 레이아웃을 에디터 월드에 태그 액터로 반영한다: POI 앵커(`ATDPoiAnchor`, 태그 `TDPoi`), 도로 스플라인 액터(`ATDRoadSpline`, 태그 `TDRoad`), 배제 볼륨(`ATDExclusionVolume`, 태그 `TDExclusion`), 던전 입구(`ATDDungeonEntrance`), POI 레벨 인스턴스.
- 완료 조건: 트랜잭션·OFPA 저장, 재생성 시 잠긴 요소 유지, 생성물 GUID 기록, 지역 단위 부분 재생성
- 참조: 03-architecture 3.5, R-101
- 기록: 2026-09-09 작성 / 2026-09-12(Claude) C++: `TDWorldBaker.*`(POI/입구/배제 TargetPoint, 도로 `ATDRoadSplineActor`) + `BakeWorldLayout`. 잠금 유지·부분 재생성은 남음. / 2026-09-12(Claude, 2단계): `FTDWorldBaker::BakeWithOptions`(지역 필터 `FTDWorldLayoutRegionFilter`·`bClearExisting`), `CollectLockedElements`(태그 `TDLocked`/`bLocked` → `FTDLockedLayoutElements`로 생성기 되먹임, `FTDWorldGraphGenerator::Generate(..., Locked, ...)`), 액터 태그 `TDGuid:<32자>`·`TDSeed:<n>`로 같은 GUID 액터는 트랜스폼만 갱신. 라이브러리 `GenerateWorldLayoutWithLocked`/`BakeWorldLayoutInRegion`/`CollectLockedLayoutElements`. 테스트 `TDGame.WorldGen.LockedAnchorsSurviveRegenerate`, `RegionFilterLeavesOtherRegionsUntouched` 통과. 결정 D-07·D-08.

### P3-07 Forest 바이옴 PCG 그래프
- 상태: doing
- 우선순위: 높음
- 선행: P3-06, P0-03
- 목표: `PCG_TDBiome_Forest`와 공용 서브그래프(`PCG_TDClutter`, `PCG_TDRockScatter`, `PCG_TDVegetation`), `PCG_TDRoadside`, `PCG_TDPoiDressing`을 만든다.
- 완료 조건:
  - 파이프라인: Get Landscape Data(레이어 가중치·노멀) → Surface Sampler → Normal To Density → Density Filter → Get Actor Data(태그 TDRoad/TDPoi/TDExclusion) → Distance/Difference 배제 → Self Pruning(Bounds From Mesh) → Static Mesh Spawner(WeightedByCategory)
  - 파라미터(밀도·최소 간격·최대 경사·이격)를 `UTDBiomeDefinition`에서 C++가 `UPCGGraphParametersHelpers`로 주입하는 `UTDBiomePcgBinder`(에디터 서브시스템 함수)
  - 파티션 컴포넌트 + HiGen(큰 나무 Grid 큰 값, 잔해 작은 값)
  - Normal 모드 생성 후 저장
- 검증: 재생성 결과 결정론 확인(같은 시드 2회 생성 후 ISM 인스턴스 수·해시 비교)
- 참조: R-30~R-33, research/pcg-api-and-nodes.md, research/pcg-bake-data-community.md
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `Tools/WorldGen/editor_make_pcg_biome.py`로 그래프 6종+룸 드레싱 그래프·볼륨 생성, 시드 7 인스턴스 9,255개, 결정론 2회 해시 동일(`Docs/Validation/P3-07-pcg-biome.md`). 그래프 사용자 파라미터·`UTDBiomePcgBinder`(C++)는 남음(값은 노드 프로퍼티에 직접 기입).

### P3-08 야외 후보 생성·선택 툴
- 상태: doing
- 우선순위: 중간
- 선행: P3-05, P2-08
- 목표: 월드 생성기 명령(에디터 서브시스템 + 월드 생성기 액터 `CallInEditor`)으로 시드 N개 생성·검증·점수 후 선택 베이크.
- 완료 조건: Generate Outdoor / Validate Outdoor / Regenerate PCG / Bake, 리포트 메시지 로그, 잠금 목록
- 참조: R-100, R-101, 04-tools 4.5
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `Tools/WorldGen/select_seed.py --seeds a-b`(생성→검증→점수표→추천), `editor_regenerate_pcg.py`, 베이크는 `editor_build_open_world.py`. 에디터 UI(CallInEditor 버튼)·잠금 목록은 남음.

### P3-09 500m×500m 버티컬 슬라이스 베이크
- 상태: doing
- 우선순위: 중간
- 선행: P3-07, P3-08, P2-12
- 목표: 마을 1, 던전 3(아틀라스 슬롯 3), POI 8을 실제로 베이크하고 플레이한다.
- 완료 조건: 이동 시간·전투 밀도 측정(1분당 콘텐츠 수), `Docs/Validation/P3-09-vertical-slice.md`
- 참조: R-110, 설계서 13장 규모 판단 기준
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): 이동 시간·콘텐츠 밀도 오프라인 추정을 `Docs/Validation/P3-09-vertical-slice.md`에 기록(마을→가장 먼 콘텐츠 약 80초, 무콘텐츠 최장 36m). 전투 밀도는 몬스터 스포너 연동 후 PIE 실측 필요.

### P3-10 커맨드릿 `UTDWorldGenBuilder`
- 상태: done
- 우선순위: 중간
- 선행: P3-08
- 목표: `UWorldPartitionBuilder` 파생으로 "생성 → 검증 → 리포트(→ 베이크)"를 무인 실행한다.
- 완료 조건: `-run=WorldPartitionBuilderCommandlet <Map> -Builder=TDWorldGenBuilder -Seed= -Validate [-Bake]`, 리포트 파일 `Saved/WorldGen/<날짜>.md`, 실패 시 종료 코드 ≠ 0
- 참조: research/persistence-editor-batch.md 3.1
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDWorldGenCommandlet`(`-run=TDWorldGen -Flow -Size -Seeds -Report`) 던전 시드 스윕 리포트 동작 확인. 월드 파티션 빌더 파생·베이크 옵션은 남음. / 2026-09-12(Claude): `UTDWorldGenBuilder : UWorldPartitionBuilder`(`Source/TDGameEditor/WorldGen/TDWorldGenBuilder.*`). `UnrealEditor-Cmd.exe TDGame.uproject /Game/Level/LV_DarkFantasy_OpenWorld -run=WorldPartitionBuilderCommandlet -Builder=TDWorldGenBuilder -Seed=7 -Validate [-Bake] [-Region=] [-Report=Saved/WorldGen/x.md] -unattended` → 시드 7 PASS 100/100, 종료 코드 0(검증 실패 시 1 확인). 주의: 에디터가 켜져 있으면 MCP 포트 8000 충돌 오류 로그 때문에 종료 코드가 1이 되므로 에디터를 닫고 실행. 결정 D-09. 첫 실행에서 MainDungeon 입구 지형 검사가 실패해 앵커에 `YawDeg` 추가·입구 접근로(전방 반원) 표본으로 검사 방식 변경(D-10).

### P3-12 ID 없는 Landmark 앵커 여러 개일 때 도로 ID 중복
- 상태: todo
- 우선순위: 낮음
- 선행: P3-04
- 목표: Landmark 앵커를 도로 노드로 모을 때 ID가 비어 있어도 도로 ID가 겹치지 않게 결정론적 대체 ID를 만든다.
- 완료 조건: ID 없는 Landmark 2개 이상 픽스처에서 도로 ID 고유, 결정론 해시 테스트 통과
- 참조: P3-04 2026-09-30 기록, `Saved/AgentOps/20260930/fix-world.md`
- 기록: 2026-09-30 발견(claude). 에디터 경로는 액터 라벨로 ID를 만들어 실제 영향은 작음

### P3-13 이계 지역(Realm) 2곳과 외곽 관문 심리스 이동
- 상태: done
- 담당:
- 우선순위: 높음
- 선행: P1-04, P1-05, P3-00, P3-11
- 목표: 외곽 관문이 벽 복도 던전 대신 메인 지역과 지형·분위기가 다른 오픈 지역 2곳(지하 세계풍 이계, 천공풍 고지)으로 로딩 화면 없이 이어지게 한다.
- 완료 조건:
  - C++: `UTDRealmDefinition`(TDWorldGen)·설정 `RealmDefinitions`, 이동 서브시스템이 관문 ID를 이계 도착점·필드 귀환점으로 해석하고 요청 시 지역 색 페이드아웃 → 순간이동 → 정착 후 페이드인, 선로딩 구 이탈 시 취소, 도착 직후 겹친 관문 선로딩, `UTDRealmSubsystem`(이동 순간 전역 태양·스카이라이트·대기·안개 전환과 귀환 복원, 낙하 복구)
  - 생성: `Tools/WorldGen/generate_realm.py` 두 이계(504m, 505×505) 시드 결정론·도달성·경사 검증 통과, 미리보기
  - 베이크: `editor_build_realm.py`가 같은 레벨 먼 좌표(별밭 (−3024m, 3024m), 고원 (−3024m, −3024m))에 랜드스케이프·산포·조명·후처리 볼륨·내비 경계·관문(필드 3·이계 3)·정의 에셋을 멱등 생성. 메인 빌더 재실행이 이계를 지우지 않음
  - 검증: 헤드리스 `TDGame.WorldGen`·`TDGame.SeamlessTravel`(신규 `RealmRoundTrip`·`PreloadCancelAllowsOtherTarget` 포함)·`TDGame.WorldState` 통과, PIE 걸어서 관문 3곳 왕복(로딩 화면 없음), 이계별 캡처 `Docs/Validation/P3-13-*`
- 산출물: `Source/TDWorldGen/*/World/TDRealmDefinitions.*`, `Source/TDGame/World/Streaming/TDRealmSubsystem.*`, 이동 서브시스템·관문 수정, `Tools/WorldGen/{generate_realm,editor_build_realm}.py`, `Content/World/Definitions/DA_TDRealm_*`, `Content/World/Realms/`
- 검증: 명세 `Saved/AgentOps/20261013/realm-spec.md` 2·3절 명령, `python Tools/WorldGen/pie_p1_checks.py --ids ...`
- 참조: P1-04·P1-05(심리스 이동·입구), P3-00(랜드스케이프 raw 입력), P3-11(메인 지역 베이크), D-04·D-06
- 기록: 2026-10-13 작성·청구(claude): 사용자 요청 "외곽 던전을 벽 복도가 아닌 오픈월드 특정 지역으로, 엘든링 지하 세계·공중 지역처럼 다른 세계, 심리스 로딩으로 이동"
  2026-10-14 완료(claude): C++(UTDRealmDefinition·UTDRealmSubsystem·이동 서브시스템 관문 해석·지역 색 페이드·선로딩 취소·도착 직후 재선로딩·이계 진입 뒤 필드 귀환점 10초 붙잡기·낙하 복구), 생성기·베이크(별밭 인스턴스 4,639·조명 55, 고원 1,584·구름 평면 72, 랜드스케이프 각 17 프록시, 필드 입구 3→관문 3), 재질 `RealmTint`·`RealmBrightness`·`RealmDesaturation`. 헤드리스 29/29(RealmRoundTrip 포함), PIE 관문 3곳 걸어서 왕복 FAIL 0, 귀환 준비 9~24s→0~2.7s(L-worldgen-01), 메인 빌더 보호는 실제 에디터 건식 실행(이계 액터 927 보존·태그 누락 0, 필드 상자 랜드스케이프 65=메인만). 증거 `Docs/Validation/P3-13-realms.md`. 후속 P1-11·P1-12·P3-14.

### P3-14 이계 인카운터와 이계 전용 장식 메시
- 상태: todo
- 우선순위: 중간
- 선행: P3-13
- 목표: 이계 공터(`TDRealmCombatClearing` 마커)에 몬스터 인카운터를 두고, 대체 중인 기둥·버섯을 이계 전용 메시(수정·유적·구름)로 바꾼다.
- 완료 조건: 이계별 `UTDEncounterSet` 에셋과 공터 스포너, 셀 재로드 시 처치 상태 유지 방식 결정(P1-06 영속 상태), 신규 메시 확보 경로(제작·구매) 사용자 결정
- 참조: `Docs/Validation/P3-13-realms.md`, 조사 `Saved/AgentOps/20261013/realm-understand.md` content-assets·editor-cpp 절
- 기록: 2026-10-14 작성(claude)

### P3-15 이계 통로: 걸어서 넘어가는 숨은 이음매 연결
- 상태: done
- 담당:
- 우선순위: 높음
- 선행: P3-13
- 목표: 외곽 입구 3곳에서 통로를 걸어가면 앞쪽 이계가 열리고 지나온 필드가 꺼지며, 암전·포탈 없이 이계로 넘어간다(D-42).
- 완료 조건:
  - 공유 통로 모듈 `Tools/WorldGen/realm_passages.py`로 필드 통로 A(입구→이음매, 남서·남동 하강, 북동 상승)와 이계 통로 B를 깎고, 이음매 ±30m 핵심 구간 높이가 정수 오프셋 뒤 1/128m 이내로 같다
  - C++: 걷는 동안 반대편 선로딩, 이음매 통과 시 암전 없는 평행이동(속도·회전 유지, 카메라 컷, 클릭 이동 목적지 이동), 진행도 기반 분위기·후처리 혼합과 이음매 안개, 낙하 복구
  - 포탈 관문 액터 제거, 메인 필드는 부분 재베이크(던전 슬롯·PCG·상자·내비 설정 보존 개수 확인), 던전 귀환점 갱신
  - 검증: 헤드리스 `TDGame.SeamlessTravel.RealmSeamWalk`(관문 3곳 왕복 연속성)·기존 테스트 통과, PIE에서 실제 이동 입력으로 관문 3곳 왕복, 이음매 전후 캡처 `Docs/Validation/P3-15-*`
- 산출물: `Tools/WorldGen/realm_passages.py`, 생성기·빌더 수정, `UTDRealmSubsystem` 틱 처리, 메인 필드 지형·산포 재베이크
- 검증: 명세 `Saved/AgentOps/20261013/seam-spec.md` 2.7·3·4절
- 참조: D-42, P3-13 증거 `Docs/Validation/P3-13-realms.md`, L-worldgen-01
- 기록: 2026-10-14 작성·청구(claude): 사용자 "포탈 같다, 플레이어가 이동하면서 다른 지역이 열리는 방식으로"
  2026-10-14 완료(claude): 공유 통로 모듈 `realm_passages.py`, 필드 통로 A 3곳(부분 재베이크: 지형 65·layout 액터 1,896·ISM 1,090 교체, 보존 1,114), 이계 통로 B·소품, 포탈 액터 33개 제거, C++ 이음매 통과·진행도 혼합(태양 회전 포함)·이음매 안개·후처리 가중치. 형상 일치 0/128m, 헤드리스 31/31(RealmSeamWalk 3관문 왕복), PIE 실제 이동 입력 걷기 6/6(통과 순간 속도 600cm/s 유지). 중간 발견: 평면 충돌 PIE 부활(L-editor-15, 338개 수정), 이음매 태양 회전 튐(L-worldgen-03). 증거 `Docs/Validation/P3-15-passages.md`. 후속 P3-16.

### P3-16 이계 통로 조명·카메라 가림 다듬기
- 상태: todo
- 우선순위: 낮음
- 선행: P3-15
- 목표: 통로 핵심 구간(깊은 협곡)의 어두움과 카메라가 벽 소품에 가려지는 장면을 줄여 전투 가독성을 확보한다.
- 완료 조건: 통로 점광원·벽 높이·이음매 안개 값 조정 후 PIE 캡처 비교, 사용자 확인
- 참조: `Docs/Validation/P3-15-passages.md` '남은 것', `Docs/Validation/P3-15-passages/*_2_*`·`*_3_*`
- 기록: 2026-10-14 작성(claude). 2026-10-14 P3-17이 카메라 가림(통로 카메라 쪽 벽 낮춤·입구 상인방 제거·재질 컷아웃)과 통로 화면 암전(어두운 비율 ≤0.001)을 해결함 — 남은 범위는 통로 조명 분위기 다듬기와 사용자 확인

### P3-17 탑다운 시점 시각 오류 정리: 큰 물체 가림·뒷면·빈 공간
- 상태: done
- 담당: claude/2026-10-14
- 우선순위: 높음
- 선행: P3-11, P3-13, P3-15
- 목표: 고정 탑다운 카메라(실제 값은 BP가 덮어씀: 암 1100cm, 피치 −50°, 요 45°, 폰 기준 (−500,−500,+843)cm)에서 큰 나무·바위·절벽이 플레이어를 가리거나 뒷면·빈 공간을 드러내는 현상을 없앤다(필드·이계·통로).
- 완료 조건: ① 프로젝트 탑다운 재질 체계(`/Game/World/Materials/TopDown/` MPC_TD·MF_TD·M_TD·MI_TD, D-43)와 C++ 가림 초점 서브시스템으로 폰 앞을 가리는 우리 배치물이 디더로 비워진다(그림자 유지, 콘솔 변수로 끄기) ② 결정론 배치 후처리(`topdown_rules.py`): 걷는 칸의 카메라 시선 가림 제거·축소, 바위 기울기 0·카메라 쪽 틈 묻기, 높은 기둥 상한, 통로 카메라 쪽 벽 낮춤(형상 일치 유지), 통로 입구 상인방 제거, 고원 가장자리 구름 확장 — 규칙 전후 수치 표 ③ 메인 부분 재베이크·이계 재베이크·PCG 재생성 뒤 헤드리스 테스트 회귀 없음, PIE 통로 실제 걷기 6/6 ④ PIE 27지점 조사 전후 비교에서 심각·높음(화면 암전·카메라 매몰·폰 가림) 0건 ⑤ 설계 문서 `Docs/WorldDungeonPCG/05-topdown-visuals.md`, 도구 등록
- 명세: `Saved/AgentOps/20261014/topdown/topdown-spec.md`
- 참조: P3-16(통로 카메라 가림 포함), `Docs/Validation/P3-15-passages.md`
- 기록: 2026-10-14 작성·청구(claude): 사용자 "큰 오브젝트 배치로 고정 카메라에 뒷면·빈 공간이 보인다. 탑다운에 맞춘 배치, 투명 처리, 긴 물체 높이 줄이기 등으로 자연스럽게. 평원은 괜찮고 다른 지역·큰 나무·바위에서"
  2026-10-14 완료(claude): ① 재질 체계 `/Game/World/Materials/TopDown/`(MPC·MF·마스터 5·거울 MI 31, 이계 MI 재부모화, PCG 그래프 6개 패치, 슬롯 3,432개 교체) + C++ `UTDTopDownOcclusionSubsystem`·`UTDTopDownSettings`(`td.TopDown.OcclusionCutout`), 줄기 Masked 함정은 `ClearMaskedAssumedOpaque`로 해결(L-editor-17) ② `topdown_rules.py` R1~R6(+별밭 바깥 고리): 걷는 칸 가림 필드 11.0→0.97%, 카메라 메시 안 3,889→0, 형상 일치 0/128m, 독립 반박 합격 ③ 메인 부분·이계 재베이크, 헤드리스 32/32, PIE 통로 실제 걷기 6/6(600cm/s, 프레임 평균 26~36ms) ④ 27지점 전후: 통로 암전 0.976/0.472/0.639→≤0.001, 가림 16→5(남은 5는 지점 자체 문제 2·디더 처리 3), 매몰 0 ⑤ 설계 `Docs/WorldDungeonPCG/05-topdown-visuals.md`, 도구 6개 등록. GPU: 컷아웃 켬/끔 차이 측정 잡음 이내. 증거 `Docs/Validation/P3-17-topdown.md`. 남은 것: 가파른 지형 텍스처 늘어남·이계 상자 지형 끝선, 고원 첨탑 62→6m 사용자 확인.

### P3-18 엘든링식 동적 승강기(별밭 남동)와 걷는 통로 동적 속도 조절
- 상태: doing
- 담당: claude/2026-10-14
- 우선순위: 높음
- 선행: P3-15, P3-17
- 목표: 남동 입구를 절벽 옆 열린 승강대로 바꿔, 하강하는 동안 별밭을 로드하고 준비되면 숨은 이음매를 넘어 바닥에서 멈추고 열리게 한다(로딩이 늦으면 이음매 위에서 느려지고 멈춰 기다림). 남서·북동 걷는 통로는 준비가 늦으면 이음매 앞에서 감속하고 안개를 짙게 한다(D-44).
- 완료 조건: (조사 뒤 기록)
- 참조: D-42·D-44, `Docs/Validation/P3-15-passages.md`, `Docs/WorldDungeonPCG/05-topdown-visuals.md`
- 기록: 2026-10-14 작성·청구(claude): 사용자 "엘든링 지하 엘리베이터처럼 로드되는 동안 계속 내려가다 완료되면 멈추고 문이 열리는 방식, 브릿지 구간이 동적인지, 가능하면 적용"

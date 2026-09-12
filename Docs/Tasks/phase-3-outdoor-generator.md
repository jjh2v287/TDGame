# Phase 3 — 야외 생성기

| 상태 | 개수 |
|---|---|
| doing | 9 |
| done | 3 |

목표(설계서 Phase 3): 500m×500m 필드에 마을 1, 던전 3, POI 8을 생성하고 도로 스플라인·배제 마스크·Forest 바이옴 PCG·월드 검증·시드 후보 선택까지 완성한다.

### P3-00 랜드스케이프 생성 C++ 에디터 함수와 높이맵 파이프라인
- 상태: doing
- 우선순위: 높음
- 선행: P0-01, P0-02
- 목표: 월드 기본 바탕(지형)을 결정론적으로 만들 수 있게 `ALandscape::Import`를 감싼 에디터 함수와 높이맵 생성 경로를 만든다.
- 완료 조건:
  - `UTDLandscapeEditorLibrary::CreateLandscape(UWorld*, FTransform, int32 ComponentsX, int32 ComponentsY, int32 QuadsPerSection, int32 SectionsPerComponent, const TArray<uint16>& Heights)`(BlueprintCallable, `TDGameEditor`)와 `ImportHeightmapFromTexture` 보조 함수
  - `TDWorldGen`에 `FTDHeightmapGenerator`(지역 정의 + 수작업 고도 앵커 + 노이즈 → uint16 배열, 시드 결정론)
  - 월드 파티션 큰 월드용 `LandscapeStreamingProxy` 분할 옵션(그리드 크기 배수)
  - 도로·강은 `editor_apply_spline`/랜드스케이프 스플라인, 바이옴 마스크는 웨이트맵 렌더타깃 임포트로 쓰는 절차를 문서화
  - Python 원격 실행 또는 커맨드릿에서 호출해 500m×500m 지형 생성 확인
- 산출물: `Source/TDGameEditor/.../TDLandscapeEditorLibrary.h/.cpp`, `Source/TDWorldGen/.../TDHeightmapGenerator.h/.cpp`
- 검증: 같은 시드 2회 생성 시 높이 배열 해시 동일, 에디터에서 생성된 컴포넌트 수·바운드 확인
- 참조: research/landscape-mcp-test.md, R-20, R-32
- 기록: 2026-09-11 작성(MCP UI 자동화로 생성 가능함을 실측했으나 재현성 위해 C++ 경로 채택)
  2026-09-12(Claude): `UTDLandscapeEditorLibrary::CreateLandscapeFromRawFiles(WorldContext, FTDLandscapeCreateRequest)` 구현·검증 — raw 높이맵(uint16)·레이어 raw(uint8) 파일을 읽어 `ALandscapeProxy::Import` → `UE::Landscape::CreateTargetLayerInfo`로 레이어 인포 자동 생성 → `ULandscapeSubsystem::ChangeGridSize`로 월드 파티션 프록시 분할. 1009×1009, 6레이어, 그리드 2 → 프록시 64개 생성 확인(약 25초). 높이맵 생성기는 C++ `FTDHeightmapGenerator` 대신 numpy 프로토타입(`Tools/WorldGen/generate_ashen_vale.py`)으로 먼저 만들었음. 남은 조건: C++ 생성기 이식 여부 결정, 시드 재현성 해시 테스트, 문서화.

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
- 상태: doing
- 우선순위: 높음
- 선행: P3-02
- 목표: 지역 정의 + 앵커 + 시드 → 사이드 던전 입구 후보, POI 후보, 이벤트 영역, 전투 공간을 포아송 디스크 샘플링으로 배치한다(`FTDWorldLayout`).
- 완료 조건: 최소 간격·앵커 배제 반경·지형 조건(경사·수면, 랜드스케이프 샘플은 에디터 단계에서 주입) 준수, 결정론 테스트, 지역별 수량 범위 충족
- 참조: R-21, R-32
- 기록: 2026-09-09 작성 / 2026-09-12(Claude) C++: `TDWorldGraphGenerator.cpp`(앵커 고정 → 포아송 디스크 → 입구/POI/공터/이벤트 배치, 결정론 GUID), 테스트 시드 1~50 통과율 100%.

### P3-04 도로 생성기 `FTDRoadGenerator`
- 상태: doing
- 우선순위: 높음
- 선행: P3-03
- 목표: 마을·메인 POI·던전 입구를 잇는 도로 스플라인 점을 만든다.
- 완료 조건: 최소 신장 트리 + 보조 연결(설정 비율), 경사 비용 가중 A*(격자 코스트 맵은 에디터가 랜드스케이프에서 샘플해 전달), 도로 폭·배제 폭 출력
- 참조: R-21, R-32(스플라인 샘플링)
- 기록: 2026-09-09 작성 / 2026-09-12(Claude) C++: `TDRoadGenerator.cpp`(격자 A* 경사·수면 비용, 프림 MST + 보조 도로, 20m 재표본), 테스트 `RoadsReachAllEntrancesFromTown` 통과.

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

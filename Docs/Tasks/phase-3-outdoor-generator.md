# Phase 3 — 야외 생성기

| 상태 | 개수 |
|---|---|
| todo | 10 |
| doing | 0 |
| done | 0 |

목표(설계서 Phase 3): 500m×500m 필드에 마을 1, 던전 3, POI 8을 생성하고 도로 스플라인·배제 마스크·Forest 바이옴 PCG·월드 검증·시드 후보 선택까지 완성한다.

### P3-01 월드·지역·바이옴·POI 정의 에셋 타입
- 상태: todo
- 우선순위: 높음
- 선행: P0-02
- 목표: `UTDWorldDefinition`, `UTDRegionDefinition`, `UTDBiomeDefinition`, `UTDPoiArchetype`를 `TDWorldGen`에 만든다.
- 완료 조건: 설계서 4.2·5.2 필드 반영, `DA_TDWorld_Main`, `DA_TDRegion_Forest`, `DA_TDBiome_Forest`, POI 원형 4종 에셋, `IsDataValid` 검사
- 참조: R-22, R-31, 03-architecture 3.2
- 기록: 2026-09-09 작성

### P3-02 수작업 앵커 액터
- 상태: todo
- 우선순위: 높음
- 선행: P3-01
- 목표: 마을·메인 던전·랜드마크·지역 경계를 디자이너가 놓는 앵커 액터(`ATDWorldAnchor`, 종류 열거, 배제 반경)와 지역 볼륨(`ATDRegionVolume`)을 만든다.
- 완료 조건: 액터 배치 가능, 생성기가 앵커를 입력으로 읽음, 앵커는 재생성에서 절대 이동·삭제되지 않음
- 참조: R-20, R-101(잠금)
- 기록: 2026-09-09 작성

### P3-03 월드 그래프 생성기 `FTDWorldGraphGenerator`
- 상태: todo
- 우선순위: 높음
- 선행: P3-02
- 목표: 지역 정의 + 앵커 + 시드 → 사이드 던전 입구 후보, POI 후보, 이벤트 영역, 전투 공간을 포아송 디스크 샘플링으로 배치한다(`FTDWorldLayout`).
- 완료 조건: 최소 간격·앵커 배제 반경·지형 조건(경사·수면, 랜드스케이프 샘플은 에디터 단계에서 주입) 준수, 결정론 테스트, 지역별 수량 범위 충족
- 참조: R-21, R-32
- 기록: 2026-09-09 작성

### P3-04 도로 생성기 `FTDRoadGenerator`
- 상태: todo
- 우선순위: 높음
- 선행: P3-03
- 목표: 마을·메인 POI·던전 입구를 잇는 도로 스플라인 점을 만든다.
- 완료 조건: 최소 신장 트리 + 보조 연결(설정 비율), 경사 비용 가중 A*(격자 코스트 맵은 에디터가 랜드스케이프에서 샘플해 전달), 도로 폭·배제 폭 출력
- 참조: R-21, R-32(스플라인 샘플링)
- 기록: 2026-09-09 작성

### P3-05 월드 검증기 `FTDWorldValidator`
- 상태: todo
- 우선순위: 높음
- 선행: P3-04
- 목표: 입구 간격, POI 밀도, 도로 도달성, 지형 적합성, 슬롯 로딩 범위 중첩, 무콘텐츠 이동 구간을 검사한다.
- 완료 조건: 리포트에 위치·원인, 시드 1~200 통과율 보고
- 참조: R-91
- 기록: 2026-09-09 작성

### P3-06 월드 베이커 `FTDWorldBaker`
- 상태: todo
- 우선순위: 높음
- 선행: P3-05, P2-09
- 목표: 레이아웃을 에디터 월드에 태그 액터로 반영한다: POI 앵커(`ATDPoiAnchor`, 태그 `TDPoi`), 도로 스플라인 액터(`ATDRoadSpline`, 태그 `TDRoad`), 배제 볼륨(`ATDExclusionVolume`, 태그 `TDExclusion`), 던전 입구(`ATDDungeonEntrance`), POI 레벨 인스턴스.
- 완료 조건: 트랜잭션·OFPA 저장, 재생성 시 잠긴 요소 유지, 생성물 GUID 기록, 지역 단위 부분 재생성
- 참조: 03-architecture 3.5, R-101
- 기록: 2026-09-09 작성

### P3-07 Forest 바이옴 PCG 그래프
- 상태: todo
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
- 기록: 2026-09-09 작성

### P3-08 야외 후보 생성·선택 툴
- 상태: todo
- 우선순위: 중간
- 선행: P3-05, P2-08
- 목표: 월드 생성기 명령(에디터 서브시스템 + 월드 생성기 액터 `CallInEditor`)으로 시드 N개 생성·검증·점수 후 선택 베이크.
- 완료 조건: Generate Outdoor / Validate Outdoor / Regenerate PCG / Bake, 리포트 메시지 로그, 잠금 목록
- 참조: R-100, R-101, 04-tools 4.5
- 기록: 2026-09-09 작성

### P3-09 500m×500m 버티컬 슬라이스 베이크
- 상태: todo
- 우선순위: 중간
- 선행: P3-07, P3-08, P2-12
- 목표: 마을 1, 던전 3(아틀라스 슬롯 3), POI 8을 실제로 베이크하고 플레이한다.
- 완료 조건: 이동 시간·전투 밀도 측정(1분당 콘텐츠 수), `Docs/Validation/P3-09-vertical-slice.md`
- 참조: R-110, 설계서 13장 규모 판단 기준
- 기록: 2026-09-09 작성

### P3-10 커맨드릿 `UTDWorldGenBuilder`
- 상태: todo
- 우선순위: 중간
- 선행: P3-08
- 목표: `UWorldPartitionBuilder` 파생으로 "생성 → 검증 → 리포트(→ 베이크)"를 무인 실행한다.
- 완료 조건: `-run=WorldPartitionBuilderCommandlet <Map> -Builder=TDWorldGenBuilder -Seed= -Validate [-Bake]`, 리포트 파일 `Saved/WorldGen/<날짜>.md`, 실패 시 종료 코드 ≠ 0
- 참조: research/persistence-editor-batch.md 3.1
- 기록: 2026-09-09 작성

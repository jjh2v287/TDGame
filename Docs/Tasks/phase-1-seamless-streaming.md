# Phase 1 — 심리스 스트리밍 프로토타입

| 상태 | 개수 |
|---|---|
| todo | 3 |
| done | 7 |

목표(설계서 14장 Phase 1, 17장): "던전 입구 → 목적지 스트리밍 소스 → 던전 슬롯 → 출구" 왕복이 로딩 화면 없이 안정적으로 동작하고, 영속 상태 최소판이 스트리밍을 견딘다.

### P1-01 월드 파티션 메인 월드 생성
- 상태: todo
- 우선순위: 높음
- 선행: P0-03, P0-04
- 목표: `Content/World/Maps/L_TDWorld_Main`을 월드 파티션 맵으로 만들고 스트리밍을 켠다.
- 완료 조건:
  - 월드 파티션 활성, `bEnableStreaming = true`, 런타임 해시 RuntimeHashSet, 메인 그리드 셀 크기·로딩 범위를 초기값으로 기록(제안: 셀 12800, 로딩 범위 8000, 이후 P1-09에서 조정)
  - 필드 테스트 영역(바닥·플레이어 스타트·간단한 장애물)과 그 위치에서 3km 이상 떨어진 던전 아틀라스 원점 마커
  - `Docs/Validation/P1-01-world-settings.png` 캡처
- 검증: MCP SceneTools `get_current_level`, 월드 설정 프로퍼티 확인, PIE에서 `wp.Runtime.ToggleDrawRuntimeHash2D`로 셀 표시
- 참조: R-10~R-13, G-01, research/worldpartition-streaming.md 5절
- 기록: 2026-09-09 작성

### P1-02 내비메시 월드 파티션 설정
- 상태: done
- 우선순위: 중간
- 선행: P1-01
- 목표: 내비메시가 셀과 함께 스트리밍되도록 설정하고 빌드한다.
- 완료 조건: `ARecastNavMesh::bIsWorldPartitioned` 켬, `RuntimeGeneration = Static`, `NavigationDataChunkGridSize`를 셀 크기 배수로 설정, `-Builder=WorldPartitionNavigationDataBuilder`로 빌드, 필드·더미 던전 둘 다 `ANavigationDataChunkActor` 생성 확인
- 검증: 커맨드릿 로그, 에디터에서 청크 액터 목록
- 참조: R-112, research/actor-id-and-navmesh.md B절, research/persistence-editor-batch.md 3.2
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): 정적 월드 파티션 내비메시(`Tools/WorldGen/editor_setup_navmesh_wp.py` + `WorldPartitionNavigationDataBuilder`)로 청크 액터 60개 생성·에디터 경로 검사는 통과했으나, PIE에서 심리스 이동 준비 판정(스트리밍 완료+내비 투영)이 60초 넘게 실패. 사용자 결정으로 Dynamic 런타임 생성으로 되돌림(`editor_setup_navmesh_dynamic.py`, 청크 액터 삭제, D-06). 정적 방식 재시도는 P1-09 프로파일링 뒤 과제.

### P1-03 던전 아틀라스 정의와 더미 슬롯
- 상태: done
- 우선순위: 높음
- 선행: P0-02, P1-01
- 목표: `UTDDungeonAtlasDefinition`(C++, `TDWorldGen`)과 `FTDDungeonSlot`을 만들고 `DA_TDDungeonAtlas_Main`에 슬롯 1개를 정의한 뒤 더미 던전(방 3개 수준의 정적 지오메트리)을 슬롯 위치에 손으로 배치한다.
- 완료 조건:
  - 구조체 필드: DungeonId(FName), WorldTransform, Bounds, EntryTransform, ExitTransform, Theme(소프트 참조), FlowTemplate(소프트 참조), Seed, GeneratorVersion, ValidationResult
  - 슬롯 간격 규칙: 인접 슬롯 중심 거리 ≥ 2×(로딩 범위 + 슬롯 반경). 정의 에셋의 `PostEditChangeProperty`에서 검사해 경고
  - 더미 던전 액터들이 슬롯 셀에 공간 로딩으로 들어감
- 검증: 에디터에서 정의 에셋 편집, PIE 셀 표시
- 참조: R-50, R-51, G-04, 03-architecture 3.2
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDDungeonAtlasDefinition`·`FTDDungeonSlot`(`TDWorldGen/Public/Dungeon/TDDungeonDefinitions.h`) 구현, 간격 검사 `IsSlotSpacingSafe`+PostEditChangeProperty 경고, `DA_TDDungeonAtlas_Main` 3슬롯(원점 (300000+K×30000, 300000)cm). 더미 던전은 C++ 베이커가 슬롯에 생성. / 2026-09-12 완료: `DA_TDDungeonAtlas_Main` 3슬롯(MainCrypt/HollowCave/SunkenCrypt), C++ 베이커가 슬롯에 던전 지오메트리 생성·저장(월드 파티션 셀에 공간 로딩). Config/DefaultGame.ini에 DefaultDungeonAtlas 등록.

### P1-04 심리스 이동 서브시스템 `UTDSeamlessTravelSubsystem`
- 상태: done
- 우선순위: 높음
- 선행: P1-03
- 목표: 목적지 선로딩·완료 판정·이동·정리를 담당하는 월드 서브시스템을 C++로 만든다.
- 완료 조건:
  - `IWorldPartitionStreamingSourceProvider` 구현, `RegisterStreamingSourceProvider`로 등록, 요청별 임시 소스(`TargetState=Activated`, `Priority=Highest`, `bUseGridLoadingRange=true`)
  - 상태 열거 `ETDSeamlessTravelState { Idle, Preloading, ReadyToTravel, Traveling, Settling }`와 전이 델리게이트(`OnTravelStateChanged`)
  - 완료 조건 = `UWorldPartitionSubsystem::IsStreamingCompleted(this)` && 목적지 `ProjectPointToNavigation` 성공. 타임아웃(기본 20초) 시 경고 로그 + 대기 연장, 절대 미완료 이동 금지
  - 이동은 `RequestTravel(FName DungeonId, bool bToEntry)` 하나로 입구·출구 공용. 이동 후 `Settling`에서 다음 `OnStreamingStateUpdated` 후 임시 소스 제거
  - Exec 콘솔 명령 `TDTravelToDungeon <DungeonId>` / `TDTravelToField`를 `ATDGamePlayerController`에 추가
- 산출물: `Source/TDGame/World/Streaming/TDSeamlessTravelSubsystem.h/.cpp`, 컨트롤러 Exec
- 검증: PIE에서 콘솔 명령으로 왕복, `LogTDGame`에 상태 전이와 소요 시간 출력, 필드 셀 언로드 확인(`wp.Runtime.ToggleDrawRuntimeCellsDetails`)
- 참조: R-14, R-60, R-62, G-02, 03-architecture 3.4.1, research/worldpartition-streaming.md 7절
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `Source/TDGame/World/Streaming/TDSeamlessTravelSubsystem.*` 구현(IWorldPartitionStreamingSourceProvider, 상태 5종·델리게이트, IsStreamingCompleted+ProjectPointToNavigation 완료 판정, 타임아웃 시 대기 연장), 컨트롤러 Exec `TDTravelToDungeon/TDTravelToField`. 빌드 성공. PIE 왕복 검증은 진행 중. / 2026-09-12 완료: PIE에서 `TDTravelToDungeon MainCrypt` → 0.48초 후 폰이 (305000,303400)로 이동, `TDTravelToField` → 10.5초 후 (29621,−26810) 귀환, 상태 전이 Idle→Preloading→ReadyToTravel→Traveling→Settling→Idle 로그 확인. 에디터 재시작 직후 첫 시도는 내비메시 미생성으로 타임아웃 경고만 반복(설계대로 미완료 이동 없음).

### P1-05 던전 입구 액터 `ATDDungeonEntrance`
- 상태: done
- 우선순위: 높음
- 선행: P1-04
- 목표: 입구 액터가 선로딩 범위 진입을 감지해 서브시스템에 요청하고, 상호작용(또는 트리거)으로 이동을 시작한다.
- 완료 조건:
  - 프로퍼티: `DungeonId`, `PreloadDistance`, `bIsExit`(출구면 필드 귀환), 귀환 위치
  - 선로딩 트리거는 구형 콜리전 오버랩(플레이어 폰만), 이동 트리거는 별도 박스 오버랩. 방향 내적 판정 같은 추정 로직 금지
  - 연출 훅: `BlueprintImplementableEvent OnTransitionBegin/End`(암전·통로 연출은 BP가 구현), 이동 중 입력 제한은 C++
  - 더미 던전 안에 출구 입구(`bIsExit`) 배치
- 검증: PIE에서 걸어서 왕복 3회, 로그
- 참조: R-61, G-03, 03-architecture 3.4.1
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `ATDDungeonEntrance`(구 선로딩 트리거·박스 이동 트리거·BP 연출 훅·입력 제한) 구현, `Tools/WorldGen/editor_place_dungeon_entrances.py`로 배치. / 2026-09-12: 입구·출구 액터 3쌍 배치 저장(`editor_place_dungeon_entrances.py`). 걸어서 트리거하는 왕복 3회 검증은 남음(콘솔 명령 왕복만 확인). / 2026-09-12: PIE에서 걸어서 왕복 3회 통과(`Tools/WorldGen/pie_p1_checks.py`: 선로딩 구 → 이동 상자 → 던전 x=305000 → 출구 상자 → 필드 x=29622). 셀 스트림 아웃·인 후 BeginPlay 재호출 시 오버랩 델리게이트 중복 바인딩 ensure가 나서 `AddUniqueDynamic` + `EndPlay`에서 `RemoveDynamic`으로 수정. 기록 `Docs/Validation/P1-pie-checks-2026-09-12.md`.

### P1-06 안정 ID 컴포넌트 `UTDPersistentStateComponent`와 인터페이스
- 상태: done
- 우선순위: 높음
- 선행: P0-02
- 목표: 스트리밍으로 언로드·재로드되는 액터의 상태를 키로 묶는 안정 ID와 저장·복원 인터페이스를 만든다.
- 완료 조건:
  - `UPROPERTY(SaveGame) FGuid StableId`. `PostRegisterAllComponents()` 오버라이드에서 `Super` 호출 전에 `FActorInstanceGuid::GetActorInstanceGuid(*Owner)`로 채움(무효면 경고). 에디터에서는 `#if WITH_EDITOR` 아래 `GetActorGuid()`로 미리 채워 저장
  - `ITDPersistentActor { WriteState(FTDActorStateRecord&), ReadState(const FTDActorStateRecord&) }`
  - 테스트 액터 `ATDPersistentTestChest`(열림 상태 하나) 구현
- 검증: PIE에서 상자를 연 뒤 멀리 이동해 셀 언로드 → 돌아와 열림 유지. 쿠킹 빌드 확인은 Phase 4 항목으로 이관
- 참조: R-80, R-81, G-10, research/actor-id-and-navmesh.md A절
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDPersistentStateComponent`(FActorInstanceGuid/에디터 GetActorGuid), `ITDPersistentActor`, `FTDActorStateRecord`, `ATDPersistentTestChest` 구현·빌드 성공. 스트리밍 후 상태 유지 PIE 검증은 남음. / 2026-09-12: PIE에서 상자 2개(마을·입구) 열기 → 던전 왕복(마을 셀 언로드) → 귀환 후 열림 유지 확인. 자동화 `TDGame.WorldState.ChestStateSurvivesStreaming` 통과.

### P1-07 월드 상태 서브시스템 `UTDWorldStateSubsystem` 최소판
- 상태: done
- 우선순위: 높음
- 선행: P1-06
- 목표: 액터 상태 레코드와 던전 상태를 보관하고 로드/언로드 훅에서 적용·기록한다.
- 완료 조건:
  - `TMap<FGuid, FTDActorStateRecord>`, `TMap<FName, FTDDungeonState>`
  - 액터 `BeginPlay`에서 컴포넌트가 조회·적용, `FWorldDelegates::PreLevelRemovedFromWorld`에서 해당 레벨 액터 상태 기록
  - 세이브: `UTDSaveGame`(포맷 버전, 마스터 시드, 생성기 버전 포함), `ArIsSaveGame` 프록시 아카이브로 직렬화, 슬롯 저장·로드 Exec 명령 `TDSaveWorldState`/`TDLoadWorldState`
- 검증: 저장 → PIE 재시작 → 로드 → 상자 상태 복원. 자동화 테스트 `TDGame.WorldState.ActorRecordRoundTrip`
- 참조: R-82, 03-architecture 3.4.2, research/persistence-editor-batch.md 1절
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDWorldStateSubsystem`·`UTDSaveGame`(포맷 버전 1, 마스터 시드, 생성기 버전)·Exec `TDSaveWorldState/TDLoadWorldState`, 테스트 `TDGame.WorldState.ActorRecordRoundTrip` 통과. / 2026-09-12: PIE `TDSaveWorldState 1` → 던전 왕복 → `TDLoadWorldState 1` 후 상자 열림 상태 유지 확인(`pie_p1_checks.py`).

### P1-08 PIE 왕복 자동화 테스트
- 상태: done
- 우선순위: 중간
- 선행: P1-05, P1-07
- 목표: 입구 왕복과 상태 복구를 자동화 테스트로 고정한다.
- 완료 조건: `TDGame.SeamlessTravel.RoundTripCompletesWithoutLoadingGap`(선로딩 완료 전 이동 없음, 왕복 후 필드 셀 언로드), `TDGame.WorldState.ChestStateSurvivesStreaming`. MCP AutomationTestToolset으로 실행
- 참조: R-112, 03-architecture 3.7
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `Source/TDGameEditor/Tests/TDSeamlessTravelPieTests.cpp`·`TDWorldStatePieTests.cpp`. NullRHI 실행 `UnrealEditor-Cmd.exe TDGame.uproject -ExecCmds="Automation RunTests TDGame.SeamlessTravel+TDGame.WorldState" -TestExit="Automation Test Queue Empty" -unattended -NullRHI`로 3개 통과(입장 0.45s, 귀환 6.8s). MCP AutomationTestToolset 대신 커맨드라인 실행을 표준으로 둠.

### P1-09 셀 크기·로딩 범위 프로파일링
- 상태: todo
- 우선순위: 중간
- 선행: P1-08
- 목표: 탑다운 카메라 가시거리와 이동 속도에 맞춰 셀 크기·로딩 범위·던전 슬롯 간격을 실측으로 정한다.
- 완료 조건: 3개 이상 조합을 PIE에서 측정(선로딩 소요 시간, 동시 로드 셀 수, 메모리), 결과 표를 `Docs/Validation/P1-09-streaming-profile.md`에 기록, 채택값을 월드 설정과 아틀라스 정의에 반영
- 참조: R-12, R-50
- 기록: 2026-09-09 작성

### P1-10 저속 디스크·프레임 드랍 조건 실패 케이스
- 상태: todo
- 우선순위: 낮음
- 선행: P1-09
- 목표: 로딩이 늦을 때 전환 연출이 빈 공간을 노출하지 않는지 확인한다.
- 완료 조건: `s.AsyncLoadingTimeLimit`·`s.LevelStreamingActorsUpdateTimeLimit`를 낮춰 지연을 유도, 타임아웃 경로에서 이동이 막히고 연출이 연장됨을 로그로 확인, 캡처 저장
- 참조: R-62, R-112
- 기록: 2026-09-09 작성

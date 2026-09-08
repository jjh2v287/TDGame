# Phase 1 — 심리스 스트리밍 프로토타입

| 상태 | 개수 |
|---|---|
| todo | 10 |
| doing | 0 |
| done | 0 |

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
- 상태: todo
- 우선순위: 중간
- 선행: P1-01
- 목표: 내비메시가 셀과 함께 스트리밍되도록 설정하고 빌드한다.
- 완료 조건: `ARecastNavMesh::bIsWorldPartitioned` 켬, `RuntimeGeneration = Static`, `NavigationDataChunkGridSize`를 셀 크기 배수로 설정, `-Builder=WorldPartitionNavigationDataBuilder`로 빌드, 필드·더미 던전 둘 다 `ANavigationDataChunkActor` 생성 확인
- 검증: 커맨드릿 로그, 에디터에서 청크 액터 목록
- 참조: R-112, research/actor-id-and-navmesh.md B절, research/persistence-editor-batch.md 3.2
- 기록: 2026-09-09 작성

### P1-03 던전 아틀라스 정의와 더미 슬롯
- 상태: todo
- 우선순위: 높음
- 선행: P0-02, P1-01
- 목표: `UTDDungeonAtlasDefinition`(C++, `TDWorldGen`)과 `FTDDungeonSlot`을 만들고 `DA_TDDungeonAtlas_Main`에 슬롯 1개를 정의한 뒤 더미 던전(방 3개 수준의 정적 지오메트리)을 슬롯 위치에 손으로 배치한다.
- 완료 조건:
  - 구조체 필드: DungeonId(FName), WorldTransform, Bounds, EntryTransform, ExitTransform, Theme(소프트 참조), FlowTemplate(소프트 참조), Seed, GeneratorVersion, ValidationResult
  - 슬롯 간격 규칙: 인접 슬롯 중심 거리 ≥ 2×(로딩 범위 + 슬롯 반경). 정의 에셋의 `PostEditChangeProperty`에서 검사해 경고
  - 더미 던전 액터들이 슬롯 셀에 공간 로딩으로 들어감
- 검증: 에디터에서 정의 에셋 편집, PIE 셀 표시
- 참조: R-50, R-51, G-04, 03-architecture 3.2
- 기록: 2026-09-09 작성

### P1-04 심리스 이동 서브시스템 `UTDSeamlessTravelSubsystem`
- 상태: todo
- 우선순위: 높음
- 선행: P1-03
- 목표: 목적지 선로딩·완료 판정·이동·정리를 담당하는 월드 서브시스템을 C++로 만든다.
- 완료 조건:
  - `IWorldPartitionStreamingSourceProvider` 구현, `RegisterStreamingSourceProvider`로 등록, 요청별 임시 소스(`TargetState=Activated`, `Priority=Highest`, `bUseGridLoadingRange=true`)
  - 상태 열거 `ETDSeamlessTravelState { Idle, Preloading, ReadyToTravel, Traveling, Settling }`와 전이 델리게이트(`OnTravelStateChanged`)
  - 완료 조건 = `UWorldPartitionSubsystem::IsStreamingCompleted(this)` && 목적지 `ProjectPointToNavigation` 성공. 타임아웃(기본 20초) 시 경고 로그 + 대기 연장, 절대 미완료 이동 금지
  - 이동은 `RequestTravel(FName DungeonId, bool bToEntry)` 하나로 입구·출구 공용. 이동 후 `Settling`에서 다음 `OnStreamingStateUpdated` 후 임시 소스 제거
  - Exec 콘솔 명령 `TDTravelToDungeon <DungeonId>` / `TDTravelToField`를 `ATDGamePlayerController`에 추가
- 산출물: `Source/TDGame/World/TDSeamlessTravelSubsystem.h/.cpp`, 컨트롤러 Exec
- 검증: PIE에서 콘솔 명령으로 왕복, `LogTDGame`에 상태 전이와 소요 시간 출력, 필드 셀 언로드 확인(`wp.Runtime.ToggleDrawRuntimeCellsDetails`)
- 참조: R-14, R-60, R-62, G-02, 03-architecture 3.4.1, research/worldpartition-streaming.md 7절
- 기록: 2026-09-09 작성

### P1-05 던전 입구 액터 `ATDDungeonEntrance`
- 상태: todo
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
- 기록: 2026-09-09 작성

### P1-06 안정 ID 컴포넌트 `UTDPersistentStateComponent`와 인터페이스
- 상태: todo
- 우선순위: 높음
- 선행: P0-02
- 목표: 스트리밍으로 언로드·재로드되는 액터의 상태를 키로 묶는 안정 ID와 저장·복원 인터페이스를 만든다.
- 완료 조건:
  - `UPROPERTY(SaveGame) FGuid StableId`. `PostRegisterAllComponents()` 오버라이드에서 `Super` 호출 전에 `FActorInstanceGuid::GetActorInstanceGuid(*Owner)`로 채움(무효면 경고). 에디터에서는 `#if WITH_EDITOR` 아래 `GetActorGuid()`로 미리 채워 저장
  - `ITDPersistentActor { WriteState(FTDActorStateRecord&), ReadState(const FTDActorStateRecord&) }`
  - 테스트 액터 `ATDPersistentTestChest`(열림 상태 하나) 구현
- 검증: PIE에서 상자를 연 뒤 멀리 이동해 셀 언로드 → 돌아와 열림 유지. 쿠킹 빌드 확인은 Phase 4 항목으로 이관
- 참조: R-80, R-81, G-10, research/actor-id-and-navmesh.md A절
- 기록: 2026-09-09 작성

### P1-07 월드 상태 서브시스템 `UTDWorldStateSubsystem` 최소판
- 상태: todo
- 우선순위: 높음
- 선행: P1-06
- 목표: 액터 상태 레코드와 던전 상태를 보관하고 로드/언로드 훅에서 적용·기록한다.
- 완료 조건:
  - `TMap<FGuid, FTDActorStateRecord>`, `TMap<FName, FTDDungeonState>`
  - 액터 `BeginPlay`에서 컴포넌트가 조회·적용, `FWorldDelegates::PreLevelRemovedFromWorld`에서 해당 레벨 액터 상태 기록
  - 세이브: `UTDSaveGame`(포맷 버전, 마스터 시드, 생성기 버전 포함), `ArIsSaveGame` 프록시 아카이브로 직렬화, 슬롯 저장·로드 Exec 명령 `TDSaveWorldState`/`TDLoadWorldState`
- 검증: 저장 → PIE 재시작 → 로드 → 상자 상태 복원. 자동화 테스트 `TDGame.WorldState.ActorRecordRoundTrip`
- 참조: R-82, 03-architecture 3.4.2, research/persistence-editor-batch.md 1절
- 기록: 2026-09-09 작성

### P1-08 PIE 왕복 자동화 테스트
- 상태: todo
- 우선순위: 중간
- 선행: P1-05, P1-07
- 목표: 입구 왕복과 상태 복구를 자동화 테스트로 고정한다.
- 완료 조건: `TDGame.SeamlessTravel.RoundTripCompletesWithoutLoadingGap`(선로딩 완료 전 이동 없음, 왕복 후 필드 셀 언로드), `TDGame.WorldState.ChestStateSurvivesStreaming`. MCP AutomationTestToolset으로 실행
- 참조: R-112, 03-architecture 3.7
- 기록: 2026-09-09 작성

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

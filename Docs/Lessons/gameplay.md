# Lessons — gameplay (게임플레이 기반·한 판)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A. 색인: `rg -n "^### L-" --glob '*.md' Docs/Lessons`

### L-gameplay-01 시험 월드에서 UWorldSubsystem이 게임 인스턴스를 조건으로 하면 만들어지지 않는다
- 증상: 사건 기록기 연결 서브시스템(`UTDGameplayRecorderBridge`)이 `ShouldCreateSubsystem`에서 게임 인스턴스 유무를 보자, `InitializeStandalone` 시험 월드에서 서브시스템이 생기지 않았다.
- 원인: 월드 서브시스템은 `UWorld::CreateWorld` 안에서 만들어지는데, 그때는 게임 인스턴스가 아직 월드에 붙지 않는다.
- 해결: `ShouldCreateSubsystem`은 월드 종류(Game·PIE)만 보고, 게임 인스턴스 확인은 `OnWorldBeginPlay`에서 한다. 메시지 서브시스템도 게임 인스턴스가 있어야 `Get`이 되므로 시험 월드는 게임 인스턴스를 만든다.
- 범위: UE 5.8, `Source/TDGame/Framework/TDGameplayRecorderBridge.*`, 게임 인스턴스를 쓰는 모든 월드 서브시스템
- 증거: `Saved/AgentOps/20261118/R-recorder-tests.json`(작업자 관찰, 독립 확인 없음 — 미검증)
- 날짜·상태: 2026-11-18 active

### L-gameplay-02 시험 월드에서 같은 프레임에 World->Tick 을 여러 번 부르면 타이머가 진행하지 않는다
- 증상: 일시정지 메뉴 시험에서 `World->Tick`을 연달아 불러도 타이머가 제때 터지지 않았다.
- 원인: 타이머 관리자는 같은 프레임 번호(`GFrameCounter`)의 중복 틱을 무시한다.
- 해결: 시험에서 틱 사이마다 `++GFrameCounter`를 한다. 함께 본 것: 게임 인스턴스 없이 `CreateWorld`로 만든 월드에서 `UWorld::SetGameMode`는 크래시하고, `UGameInstance::Shutdown`은 월드 컨텍스트를 남긴다.
- 범위: UE 5.8 자동화 시험(`Source/TDGame/Tests/TDPauseMenuTests.cpp`)
- 증거: `Saved/AgentOps/20261118/P1-tests-*`(작업자 관찰), `Saved/AgentOps/20261118/stage1-gp11/R4-refute.md`(셧다운 부분) — 프레임 번호 부분은 미검증
- 날짜·상태: 2026-11-18 active

### L-gameplay-03 에디터 PIE에서 Esc는 PIE 중지 단축키라 게임 입력보다 먼저 받는다
- 증상: Esc 일시정지 메뉴를 PIE에서 확인하려 하면 메뉴 대신 PIE가 꺼진다.
- 원인: 에디터의 PIE 중지 명령이 Esc에 묶여 있어 게임 입력보다 먼저 처리된다(엔진 `DebuggerCommands.cpp:358`, `PlayLevel.cpp:3092`).
- 해결: Esc를 쓰는 화면은 `-game` 실행으로 확인한다.
- 범위: UE 5.8 에디터 PIE
- 증거: `Saved/AgentOps/20261118/stage1-gp11/R4-refute.md`(반박자가 엔진 소스 줄 확인)
- 날짜·상태: 2026-11-18 active

### L-gameplay-04 Mover 폰 부활이 'SpawnActor failed because of collision'으로 취소돼 컨트롤러가 폰 없이 남는다
- 증상: `SpawnActor failed because of collision at [-8303.128,-20015.354,34.424] for BP_TDCombatCharacter_C` → `SpawnDefaultPawnAtTransform: Couldn't spawn` 뒤 플레이어 컨트롤러가 폰 없이 Inactive로 남아 진행이 멈춘다(북 공터 체크포인트, 바닥+100cm).
- 원인: 기본 `SpawnDefaultPawnAtTransform`은 APawn 기본값 `AdjustIfPossibleButDontSpawnIfColliding`을 쓴다(엔진 Pawn.cpp:93). Mover 폰은 캡슐뿐 아니라 질의 충돌이 켜진 메시까지 겹침 검사에 들어가고, 메시 쪽 보정량(MTD, 최소 이동 거리)이 0이라 엔진(UWorld::FindTeleportSpot)이 다른 자리를 시도하지 않고 생성을 취소한다. 캡슐 반높이가 88cm라 바닥+100이면 캡슐 바닥이 지면 위 12cm다.
- 해결: `ATDGameGameMode::SpawnDefaultPawnAtTransform_Implementation` 재정의 — 기본 폰 CDO의 루트 캡슐로 요청 위치 +0/25/75/150cm → 플레이어 스타트 순으로 빈자리를 찾고, 생성은 `AdjustIfPossibleButAlwaysSpawn`(취소 없음). 체크포인트 높이는 캡슐 쓸기 정지 높이 +25cm(배치 도구).
- 범위: UE 5.8, Mover 기반 플레이어 폰의 부활·생성(`Source/TDGame/Framework/TDGameGameMode.cpp`, 시험 `Source/TDGame/Tests/TDRespawnTests.cpp`)
- 증거: `Saved/AgentOps/20261119/gp03/d1-respawn.md`, 수정 전 빨강 `Saved/AgentOps/20261119/GP03-F-red2-tests.json`, PIE `Saved/Gameplay/FieldEncounters/final2/result.json`(세 공터 체크포인트 부활). 실제로 겹친 컴포넌트는 미검증
- 날짜·상태: 2026-11-19 active

### L-gameplay-05 오픈 월드 PCG 볼륨 브러시(WorldStatic, Pawn은 막지 않음)가 WorldStatic 객체 질의에 잡혀 지면 찾기·겹침 검사를 속인다
- 증상: 아레나 몬스터가 지형과 무관하게 한 높이(지면 위 약 2.5m)에서 스폰돼 떨어진다. 배치 도구의 체크포인트 겹침 검사(`capsule_overlap_components`)는 모든 후보를 막힘으로 본다(반박이 예측, 가짜 모듈로 재현).
- 원인: 오픈 월드 전체를 덮는 PCG 볼륨 `TDGen_PCG_BiomeForest`의 브러시는 WorldStatic 객체 유형이고 Pawn은 막지 않는다(엔진 AVolume 기본 프로필은 OverlapAll — Volume.cpp 26-27, 이 인스턴스의 실측 프로필 이름은 Custom). WorldStatic 객체 유형 선 추적은 시작점 초기 겹침으로 그 브러시에 맞고, 객체 유형 겹침 질의는 응답이 겹침이든 막힘이든 컴포넌트를 돌려준다(KismetSystemLibrary.cpp 1806-1821).
- 해결: C++ 지면 찾기는 몬스터 캡슐 자신의 콜리전으로 Pawn 채널 쓸기(`SweepSingleByChannel`), 시작 겹침·법선 Z<0.7은 거부. 파이썬 겹침 검사는 `get_collision_response_to_channel(Pawn)`이 Block인 컴포넌트만 막힘으로 센다(BlockingVolume은 세야 하므로 브러시 클래스째 빼지 않는다).
- 범위: UE 5.8 `LV_DarkFantasy_OpenWorld`의 지면·겹침 질의 전반(`Source/TDGame/World/Generation/TDArenaEncounter.cpp`, `Tools/Gameplay/editor_place_field_encounters.py`). 소환 졸개 `FindGroundHeight`(TDMonsterCharacter.cpp)에는 같은 결함이 남아 있다
- 증거: `Saved/AgentOps/20261119/gp03/d2-spawn.md`(낙하 적분 예측과 관측 z 일치), `fix-refute.md` M1, `f2_checkpoint_filter_check.py` 11/11, PIE final·final2 접지 3/3(발과 지면 차 2~3cm). 에디터 직접 질의로 확인(2026-11-20, `Saved/AgentOps/20261119/gp03/d2-confirm.txt`: 북 공터 두 점에서 WorldStatic 선 추적이 PCGVolume BrushComponent에 시작점 겹침·시간 0으로 맞음)
- 날짜·상태: 2026-11-19 active

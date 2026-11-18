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

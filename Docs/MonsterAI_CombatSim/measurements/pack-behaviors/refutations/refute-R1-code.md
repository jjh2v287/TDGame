# 반박 R1 — 통합 구현 결함·결정론 (정적, 빌드·실행 없음)

작성: 2026-10-18 15:40 td-refuter(R1). 대상: integrated.diff(15:27 스냅숏) + 공용 트리 현재 파일.
판정: 불합격 — 치명 0 · 주요 2 · 경미 9. 명세 위반·결정론 결함은 찾지 못했다. 불합격 사유는 주요 2건(통합 뒤 공용 트리의 명세 밖 변경, 판정 스텝의 O(N²) 경로를 300마리에서 측정하지 않음).
줄 번호는 15:37:45에 바뀐 현재 `TDMonsterThinkSubsystem.cpp` 기준이다.

## 주요

### J1. 통합 뒤 공용 트리에 명세 밖 변경이 들어옴(작성자 불명)
- 근거: `Source/TDGame/MonsterAI/TDMonsterThinkSubsystem.cpp` 수정 시각 15:37:45로 integrated.diff(15:27)보다 늦음. base 대비 diff에 integrated.diff에 없는 줄이 있다: :49 `CountBlockedTacticsSectors`(익명 ns 새 함수), :427 `TRACE_CPUPROFILER_EVENT_SCOPE(TDMonsterAI_Frame)`, :567 `TRACE_CPUPROFILER_EVENT_SCOPE(TDMonsterAI_Step)`, :521·:538 기록 디버그 줄 끝 ` blocked=%d`.
- 재현: `diff --strip-trailing-cr base/.../TDMonsterThinkSubsystem.cpp Source/.../TDMonsterThinkSubsystem.cpp`를 integrated.diff의 추가 줄과 비교.
- 영향: integrated.diff를 기준으로 한 검증(R1·R2·빌드)이 지금 트리와 다르다. 기록 디버그 줄 형식이 바뀌었다(위치로 파싱하는 시험이 있으면 깨짐). 동료 세션 작업이면 커밋에 섞일 수 있다.
- 방향: 부모가 작성자를 확인하고(동료 세션 build_hold·노트), 무리 행동 커밋과 분리하거나 계약에 넣는다. 빌드·시험은 현재 트리로 다시 돈다.

### J2. 판정 스텝마다 O(N²) 경로가 있고, 비싼 생존 확인이 거리 확인보다 먼저 돈다
- 근거: `ShouldPackSlotPanic` :1065~ 후보마다 OrderedSlots 전체를 순회하며 `IsPackAlly`(:1105, `GetPackTeamId` 두 번 + `UTDCombatLibrary::IsActorAlive` = 능력 시스템 컴포넌트 인터페이스 조회)를 거리 비교보다 먼저 부른다. `RecordPackLosses` :945~ 사망 d × 슬롯 N마다 `CanWitnessPackLoss`(IsActorAlive)를 거리 확인보다 먼저 부르고, 우두머리 사망이면 목격자마다 `HasOtherLivingLeader`(:1113, O(N))를 부른다. 이웃 격자(`NeighborGrid`)를 쓰지 않는다.
- 재현(정적 추정): 300마리 한 무리, 광역기로 80스텝 안에 2마리 이상 처치가 이어지면 손실 ≥ 2 후보 C가 수백이 된다. 지지값이 커서 공황에 들지 않은 후보는 결속되지 않으므로 사망이 난 스텝마다 다시 판정된다 → 사망 스텝마다 C × N ≈ 9만 회 IsActorAlive. 우두머리 사망 한 번에 목격자 × N.
- 영향: 300마리 기준으로 사망 스텝에 수 ms 급증 가능(측정 안 함). 명세 0절의 성능 전제(대량 몬스터)에 대한 위험.
- 방향: 거리(제곱 정수)를 먼저 확인하고 팀·Engaged·생존은 그 뒤에. 또는 이미 다시 만든 `NeighborGrid`로 1200·1600 반경만 조회. `HasOtherLivingLeader`는 사망 처리 전에 생존 우두머리 목록을 한 번만 모은다. 측정은 TRACE 범위(J1의 `TDMonsterAI_Step`)로 300마리 장면에서 한다.

## 경미

### m1. 공황 슬롯의 대기·원거리 칸 값이 낡은 채 남음
- 근거: `AssignWaitSectors` :2123~ Members 필터가 `!IsInPackFear`라서 공황 슬롯은 :2140의 `WaitRing = -1`(대기열 없음) 재설정과 칸 재선정에서 빠진다. `AssignRangedSectors`도 같다. `ResolveTacticsSlotKind`는 공황 동안만 None을 돌려준다.
- 재현: 대기열이 있을 때 W1:3을 받은 고블린이 공황 → 그동안 대기열이 사라짐 → 공황 끝 → 다음 고리 패스(최대 32스텝)까지 `TokenState None && WaitRing >= 0`이라 Wait 칸으로 판정돼 고리로 가서 멈춤(공격 안 함). 칸은 그사이 다른 몬스터가 가져갔을 수도 있다.
- 영향: 공황 뒤 재교전이 최대 0.5초 늦고 칸이 잠깐 겹친다(B1·B2 재교전 창 여유 안).
- 방향: 공황에 들어갈 때(StartPackFear 또는 필터 제외 시) WaitRing·WaitSector·RangedSector를 -1로, `SlotProgressAnchorStep = 0`으로.

### m2. 공황 슬롯이 대기열 판정(EngagedCostQuarters)에 그대로 들어감
- 근거: `CountTacticsEngaged` :1868~에 공황 제외가 없다.
- 영향: 도망가는 개체가 근접 비용을 채워 `HasMeleeQueue`가 참으로 남는다 → 남은 전투원이 대기 고리로 밀린다. 명세 2.5의 제외 목록에는 없는 빈틈(설계 결정 필요).
- 방향: `IsInPackFear`를 반경 조건에 더하는 안을 MD-48에 적는다.

### m3. 공황 슬롯에 대기열 대체 경로가 남음
- 근거: `IsAvoidingTacticsTarget` :2697~와 `FillTacticsContext` 끝의 대체 경로가 `ResolveTacticsSlotKind`의 OutRecord(공황 검사 전에 채워짐)로 `HasMeleeQueue`를 보고, 공황 슬롯에도 참·고리 0 반경을 준다.
- 영향: 대기(Pending)이거나 끊긴 뒤 재개 못 한 공황 슬롯이 대상을 장애물로 보고 고리 0 반경에서 멈춘다. 후퇴 중에는 영향 없음.
- 방향: 두 곳에 `!IsInPackFear(SlotIndex)`를 더한다.

### m4. 대기(Pending) 공황 슬롯도 토큰 후보가 됨
- 근거: `IsTokenCandidate` :2527~에 공황 검사가 없다. 후퇴가 시작되면 `RevokeInvalidReservation`이 회수하고 `RegrantReadyStep` 벌점을 준다.
- 영향: 토큰 한 번 낭비 + 다른 후보의 부여가 한 패스 늦어짐.
- 방향: 공황 상태면 후보에서 뺀다.

### m5. 전술 꺼짐 전환 뒤에도 진행 중이던 공황 후퇴가 최대 3초 남음
- 근거: `ResetTactics` :1745~는 사기 조각만 다시 만들고 두뇌의 시간 제한 후퇴(`IsInTimedRetreat`)는 끊지 않는다.
- 영향: `TD.MonsterAI.Tactics 0` 직후 ≤ 192스텝 동안 사고 없이 도망 → 꺼짐 경로가 그동안 레거시와 다르다(A3에는 영향 없음).
- 방향: ResetTactics에서 `Morales[i].FearState == Retreating && Brains[i].IsInTimedRetreat()`이면 `InterruptActivity`.

### m6. 다른 우두머리 판정이 팀·교전 여부를 보지 않음
- 근거: `HasOtherLivingLeader` :1113~.
- 영향: 1600 안에 다른 팀 또는 귀환 중인 엘리트가 있으면 우두머리 충격이 꺼진다(지금 데이터는 모두 팀 2라 실제 영향은 작음).
- 방향: `GetPackTeamId` 일치 + Engaged를 조건에 더한다.

### m7. HasOtherTokenCandidate가 상한 없는 역할에도 O(N) 집계를 먼저 함
- 근거: :2558~ `IsRoleHolderLimitReached(..., CountRoleHolders(...))` 인자가 먼저 평가된다.
- 영향: 반납 토큰마다 후보 × N. 돌진자만 있는 무리에서는 후보 전부가 막혀 M × N.
- 방향: `GetExclusiveRoleCap(OtherRole) == MAX_int32`면 바로 true.

### m8. RoleBlockedCount가 토큰 패스(8스텝)에만 갱신되고 쿨다운에 따라 깜빡일 수 있음(미검증)
- 근거: `GrantPoolTokens` :2321 `Pool.RoleBlockedCount = 0`. 막힌 돌진자의 능력이 쿨다운이면 `IsTokenCandidate` 거짓 → 0 → `HasMeleeQueue` 거짓 → 다음 고리 패스에서 대기 칸 해제 → 다가와 다시 막힘.
- 영향: 돌진자 무리의 대기 고리 진동 가능. C1 실행 로그로 확인 필요.
- 방향: 막힌 수를 "후보"가 아니라 "교전 반경 안 비보유 같은 역할"로 세거나, 히스테리시스 1 고리 패스.

### m9. TDPackAlarm.h가 혼자서는 컴파일되지 않음
- 근거: `Source/TDGame/MonsterAI/TDPackAlarm.h:10` `TWeakObjectPtr<AActor>`. include는 CoreMinimal뿐이고 `class AActor;` 선언이 없다. 공유 PCH(`PCHUsage = UseExplicitOrSharedPCHs`)를 강제로 포함할 때만 빌드된다.
- 방향: `class AActor;` 한 줄(계약 7.3 보강).

## 명세 일치로 확인한 것(반례 못 찾음)
- 1.2: 발견자를 SimulationId로 정렬. 같은 스텝의 두 번째 발견자는 Silent(StepIndex가 1부터라 AlarmCallStep 0과 겹치지 않음). 호출자를 기억하는 조건 6개 일치. BeginReturn·ResetTactics·Register에서 초기화.
- 1.3: 예약 조건·지연 식·AddOrKeepEarlier·(Due, SimulationId) 정렬·도착 검증(세대·활성·Home·차단·생존·영역; 팀은 정의 고정이라 예약 때 검사로 충분)·2단에서 정지. 새 예약은 Now+8 이상이라 같은 스텝에 처리되지 않는다. Unregister에서 RemoveSlot, 재사용은 세대로 이중 차단. 사망 경보: OnDamaged(:373)가 OnDeath(:389)보다 먼저라 PendingAlertSlots·Target(AcquireTarget은 플레이어를 거리 무관하게 반환)이 채워진다 → A4 경로 성립.
- 2.4: 판정 게이트(사망 또는 직전 후퇴 시작)·스냅숏(모은 뒤 일괄 적용)·대기 만료(`<`)·재개 조건(시간 제한 후퇴 아님·남은 시간 ≥ 16·CanThink·비빙결·비경직)·우두머리 공포 길이·결속(BeginReturn·FinishReturn에서 해제). 사기 진행이 ThinkAndAct보다 먼저라 경직·빙결이 풀린 다음 스텝에 사고보다 먼저 재개된다.
- 2.5: StepTimedRetreat 순서가 계약과 같다. 시간 완료는 FearEndStep−1에 끝나 헛된 재개가 없다. bWasRetreatCornered가 남는 경로: JSON Flee가 막힌 뒤 플래그가 남아도 Retreating으로 들어가는 길은 BeginRetreat(플래그 내림)뿐이라 다음 공황을 바로 끝내지 않는다.
- 3.2: 부여 루프 ①②③ 순서, 보복 +1, RemainingCandidates는 break 뒤 + 상한 밖, 가로채기의 증감, HasOtherTokenCandidate의 자기 제외(돌진자 교대가 가능해짐). 3.3: 띠 바닥 식과 예시 670 일치. 실행기·MeasureTacticsSlotProgress에 같은 값 적용.
- 결정론: 새 외부 콜백 목록 3개 모두 정렬. 새 TMap 순회 없음. 정렬 키가 겹치지 않아 불안정 정렬 문제 없음. PendingKillLocations(기존)는 정렬하지 않지만 같은 상수를 더하므로 순서와 무관.
- 전술 꺼짐: UpdateEngagement 레거시 루프 그대로. RecordPackDeath·UpdatePackMorale 건너뜀. 차선 함수는 ResolveTacticsSlotKind·FillTacticsContext·IsAvoidingTacticsTarget이 꺼짐에서 먼저 반환하므로 영향 없음(m5 제외). JSON Flee seconds·CanThink 변경은 두 모드 모두 적용(MD-43 의도).
- 범위·널: Morales는 Register에서 함께 늘어남. OrderedSlots에는 활성 슬롯만 있어 Definition이 유효. RecordPackDeath는 Definition·Morales를 초기화하기 전에 읽음. 정수 범위: FearEndStep−StepIndex에 가드 있음.
- 이름 충돌: TDPackAlarm.cpp·TDPackMorale.cpp에 익명 ns 없음. 실행기 도우미 `HasRangedBandFloor`·`ResolveTacticsBandMin/Max`, J1의 `CountBlockedTacticsSectors`는 저장소에서 유일. 시험 파일 익명 ns의 공통 이름(SpawnMonster·AddMonster)은 멤버 함수라 충돌하지 않음.
- 로더: `MoveAway`의 `seconds` 허용(TDMonsterActionRegistry.cpp:12), 행동 `cooldown` 키 허용(:39), `elite`를 상속받는 정의 없음(Archer는 Goblin_Melee 상속).

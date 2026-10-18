# 반박 R1-fix — fix1·fix2 수정 검증 (정적, 빌드·실행 없음)

작성: 2026-10-18 td-refuter(R1). 대상: fix1.diff(base2 → staging2), fix2.diff, 공용 트리 `TDMonsterThinkSubsystem.cpp`(16:15:34). RevokeInvalidReservation은 지시대로 보지 않았다.
판정: 합격 — 지적 9건 + J2 해결. 새 결함은 경미 2건이고 치명·주요는 없다. J1(통합 뒤 동료 변경)은 이번 수정 범위 밖이라 부모가 따로 확인해야 한다.

## 지적별 해결 여부
- J2 해결(부분): RecordPackLosses가 정수 제곱 거리를 먼저 보고(최대 반경은 우두머리 사망 1600², 그 밖 1200²), 팀·목격 조건은 그 뒤에 본다. 우두머리 목록은 사망 1건당 O(N)으로 한 번 모은다. ShouldPackSlotPanic도 거리를 먼저 본다. 후보 × N 순회는 남지만 반경 밖은 정수 연산만 해서 비싼 생존 확인이 반경 안으로 줄었다. 300마리 측정은 여전히 필요하다(TRACE 범위 있음).
- m1 해결: StartPackFear가 ClearTacticsSectors(대기·안쪽·원거리 칸 -1, 진척 기준점 0)와 bIsOnDeck = false를 한다. 공황이 끝나면 칸 None → 다음 고리 패스(≤ 32스텝)에서 다시 받는다. 그사이에는 대기열 대체 경로(고리 0 반경)나 자유 접근을 쓰고 낡은 칸은 쓰지 않는다. 표의 낡은 점유는 고리 패스마다 ClearOccupants로 다시 만들어 남지 않는다.
- m2 해결: CountTacticsEngaged :1902가 공황 슬롯을 뺀다(부작용은 n2).
- m3 해결: IsAvoidingTacticsTarget :2752, FillTacticsContext 대체 경로 :2843.
- m4 해결: IsTokenCandidate :2579. HasOtherTokenCandidate·CountRoleBlockedMembers도 공황 슬롯을 세지 않는다.
- m5 해결: ResetTactics :1782가 Retreating이고 시간 제한 후퇴 중이면 InterruptActivity를 부른다. bIsTacticsActive가 이미 바뀐 뒤라 MakeContext → FillTacticsContext는 곧바로 반환한다. 비활성 슬롯은 사기가 기본값이라 들어가지 않는다. 켜짐으로 바꿀 때는 Retreating 상태가 있을 수 없다. JSON Flee 후퇴는 건드리지 않는다(정상).
- m6 해결: GatherLivingLeaderSlots가 같은 팀·Engaged·생존 조건을 본다.
- m7 해결: 상한 없는 역할이면 바로 true.
- m8 해결: RoleBlockedCount = CountRoleBlockedMembers — 상한이 있는 역할의 비보유·비공황·Engaged 구성원 수이고, 보유 수가 상한 이상일 때만 센다. IsTokenCandidate(쿨다운·부여 반경)에 의존하지 않아 쿨다운 깜빡임이 없다. GrantPoolTokens 안에서 조기 반환 전에 늘 다시 계산한다. 진짜 경합이 있을 때만 HasMeleeQueue가 참이 된다.
- m9 해결: TDPackAlarm.h에 `class AActor;`.
- fix2 bExceedsRoleCap(:1948) 맞음: Committed가 끝날 때 같은 역할의 다른 보유자(자기 제외)가 상한 이상이면 반납한다. 단락 평가로 HasOtherTokenCandidate보다 먼저 돈다. 두 돌진자가 같은 스텝에 끝나면 OrderedSlots 순으로 앞쪽이 반납하고 뒤쪽은 다른 보유자 0으로 보고 유지한다(결정론, 둘 다 반납되지 않음). 보복 +1 보유자가 Reserved인 동안 정규 보유자가 끝나면 정규 쪽이 반납된다(상한 회복, 의도와 맞음). Boss·Melee는 상한이 MAX_int32라 그대로.

## 새 결함(경미)
### n1. RoleBlockedCount를 보복 가로채기 전에 계산함
- 근거: GrantPoolTokens :2382가 InterceptForRetaliation보다 먼저 CountRoleBlockedMembers를 부른다.
- 재현: 유일한 돌진자 보유자가 근접 보복자에게 가로채이면 돌진자 보유 0인데, 그 패스의 RoleBlockedCount는 남은 돌진자를 막힘으로 센다 → 다음 토큰 패스(8스텝)까지 HasMeleeQueue가 참으로 남는다(반대 경우도 있다).
- 방향: 가로채기 뒤(또는 return 직전 두 경로 모두)로 옮긴다.

### n2. 공황 제외로 기본 토큰 수가 내려가 정상 전투원의 Reserved가 회수될 수 있음(의도 확인)
- 근거: EngagedCount가 ComputeBaseTokens(TDCombatTactics.cpp:175, (n+1)/3)와 개시 유예(:2733, ≥ 2)의 입력이다.
- 재현: 근접 9마리 중 3마리가 공황 → 6마리 → 기본 토큰 3 → 2 → 낮춤 패스(2회) 뒤 초과 Reserved 1개 회수.
- 영향: 도망가는 개체만큼 동시 공격자가 줄어든다(설계상 타당할 수 있음). MD-48에 한 줄 남길 것.

## 그 밖
- JudgePackPanic 순서 교체(ShouldPackSlotPanic 먼저): 결과는 같다. 귀환 중이지만 손실이 남은 슬롯에서 O(N) 순회가 한 번 더 돌 수 있다(무시 가능).
- 결정론: 새 순회는 모두 OrderedSlots·MemberSlots 순서이고 집계는 순서와 무관하다. 새 TMap·콜백 목록 없음.

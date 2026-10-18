# 반박 A — 결정론·규칙 논리 (spec-v1 대조)

작성: 2026-10-18 td-refuter(관점 A). 코드·명세 수정 없음.
기준 트리 주의: 검토 도중 작업 트리의 `TDMonsterThinkSubsystem.cpp`에 M3-29가 통합되어 줄 번호가 +1~+24 밀렸다(예: UnregisterMonster 327→328, UpdateEngagement 602→603, GrantPoolTokens 1754→1761, InterceptForRetaliation 1818→1832). 아래 줄 번호는 "현재 트리" 기준이며 필요하면 이전 번호를 괄호로 적는다.

요약: 치명 1 · 주요 6 · 경미 12 (가정 표시 3)

---

## 치명

### K1. 공황 시작(④)이 판정 게이트 안에 있어 시전 중이던 목격자는 공황이 실제로 시작되지 않고, 시험 B1·B2는 이를 못 잡는다
- 근거: spec-v1 §2.4 제목 "스텝마다, 사망이 있었거나 직전 스텝에 공황이 시작됐을 때만 실행"이 ①~④ 전체를 덮는다. ④는 "대기 중인 W가 CanThink()·비빙결·비경직이면 BeginRetreat, 시전·시퀀스 중이면 끝날 때까지 대기, FearPendingUntilStep 지나면 취소"다. `TDMonsterBrainSlot.h:67-70`의 CanThink는 Cast·Stagger·시퀀스에서 거짓이다. §2.7 디버그 `morale=panic` = "후퇴 진행 중 **또는 대기**".
- 재현: B2(고블린 4, 1초 안 2마리 치명). 남은 2마리가 판정 스텝에 Slash 시전 중이면(교전 근접 고블린은 대부분의 시간이 Cast) ④에서 아무도 BeginRetreat를 못 한다 → "공황 시작" 없음 → 다음 스텝 게이트 닫힘 → 시전이 끝나도 ④가 다시 돌지 않음 → 64스텝 뒤 대기 만료. 실제 후퇴 0회.
- 시험 은폐: B1·B2의 "1초 안 morale=panic"은 대기만으로 참이 된다. "각 후퇴 ≤ 2.2초"는 후퇴가 0회면 공허하게 참이고, "4초 안 재교전 행동"도 참이다. 결함 있는 구현이 초록이 된다.
- 고칠 방향: ④를 게이트에서 분리한다 — `FearPendingUntilStep > Now`인 슬롯이 하나라도 있으면 매 스텝 실행. 전염 게이트의 "공황이 시작됐다"는 BeginRetreat 호출로 정의한다. 디버그를 `morale=panic`(후퇴 중)과 `morale=fear`(대기)로 나누고, B1·B2에 "공황 판정된 개체 중 1마리 이상이 MoveAway 후퇴를 32스텝 이상 수행" 단언을 추가한다.

## 주요

### J1. 사기 판정(①~④)이 RunKernelStep 안 어디서 도는지 정해지지 않았다 → 구현마다 결과가 다르고 파이썬 시뮬과 대조할 수 없다
- 근거: `TDMonsterThinkSubsystem.cpp:523-585`의 순서는 Sense → stale Unregister → RebuildNeighborGrid → UpdateEngagement → UpdateTactics → ThinkAndAct(슬롯별 ConfirmTacticsAttackStart) → Avoidance다. spec-v1 §2.4는 "스텝마다"만 적고 위치가 없다.
- 재현: (a) UpdateTactics 뒤에 두면 그 스텝 토큰 패스가 곧 공황할 W에게 토큰을 주고, 회수는 다음 스텝 SyncTacticsSlots(`RevokeInvalidReservation`의 MoveAway 분기)에서 RegrantCooldown 48스텝 벌점과 함께 일어난다. 앞에 두면 같은 스텝에 후보에서 빠진다 → 토큰 배분·돌진자 상한 결과가 달라진다. (b) UpdateEngagement ③에서 경보로 막 깨어난 몬스터가 같은 스텝 사망의 목격자(Engaged 조건)인지도 위치에 따라 바뀐다. (c) ThinkAndAct 안(슬롯별)에 두면 스냅샷 규칙과 별개로 ④의 CanThink 판정이 앞 슬롯들의 이번 스텝 행동에 좌우된다.
- 고칠 방향: "UpdateEngagement 직후, UpdateTactics 직전에 `UpdatePackMorale()`을 한 번(①→②→③→④)"으로 고정하고 sim_pack.py도 같은 순서로 맞춘다. bIsTacticsActive가 거짓이면 함수 전체를 건너뛴다(§0).

### J2. 우두머리 목격 반경 1600에 도달할 수 없다(명세 모순)
- 근거: §2.2는 "우두머리 사망은 반경 LeaderWitnessRadiusCm(1600) 안 … 공황"이라 하고, §2.4 ①은 목격자 W 필터 "평면 거리 ≤ WitnessRadiusCm(1200)"을 먼저 적용한 뒤 "d가 우두머리이고 거리 ≤ 1600이면 bLeaderShock"를 건다.
- 재현: 골렘이 죽은 지점에서 1300cm 떨어진 교전 고블린은 ① 필터에서 이미 빠진다 → 충격 없음.
- 영향: 우두머리 반경이 사실상 1200이다. sim_pack.py를 1600으로 짜면 C++과 결과가 갈린다.
- 고칠 방향: ① 필터 반경을 `bWasLeader ? 1600 : 1200`으로 하고, RecentLossCount 증가는 1200 안에서만 한다(두 반경 분리)고 명시한다.

### J3. 차선 역할로 바꿀 위치 목록에 두 곳이 빠졌다 — 돌진자는 안쪽 칸을 못 받고, 지원은 900cm 밖이면 토큰 후보가 될 수 없다
- 근거: §3.1의 바꿀 위치 목록(GetTacticsPoolIndex·AssignWaitSectors·AssignRangedSectors·ResolveTacticsSlotKind·IsAvoidingTacticsTarget·FillTacticsContext)에 다음 두 곳이 없다: `TDMonsterThinkSubsystem.cpp:1714`(구 1707) AssignInnerSectors `IsInnerHolder`의 `Role == Melee`, `:61` GetGrantRadiusCm의 `Role == Ranged`.
- 재현: 돌진자 보유자는 1713에서 InnerSector = -1로 지워지고 다시 받지 못한다 → ResolveTacticsSlotKind(2157)를 차선 기준으로 바꿔도 Inner가 되지 않는다 → 안쪽 칸 분산 없이 직선으로 돌진한다. 지원은 MeleeGrantRadius 900을 받으므로, 띠가 900을 넘는 지원 종(바닥 상향으로 BandMax가 넓어지는 경우 포함)은 후보에서 영구히 빠진다 → C2 "셋 모두 시전 ≥ 1" 실패 또는 무공격.
- 고칠 방향: 두 곳을 목록에 넣고, 수용 기준에 "`rg -n "ETDTacticsRole::(Melee|Ranged)" Source/TDGame/MonsterAI --glob "!**/Tests/**"` 결과가 MakeFragment와 IsMeleeLaneRole·IsRangedLaneRole 정의 밖에서 0건"을 넣는다.

### J4. 역할 상한 검사와 M3-29 예산 break 중 무엇을 먼저 하는지, "남은 후보"가 무엇인지 정해지지 않았다
- 근거: 현재 부여 루프 `:1802-1829`는 "앞에서부터 연속 부여, 첫 예산 부족에서 break, RemainingCandidates = RightChop(NextCandidate)"를 전제로 보복 가로채기(`:1832-1856`)와 OnDeck을 처리한다. spec-v1 §3.2는 "건너뛴다"만 적었다.
- 재현: 후보 [A 돌진자(상한, 비용 4), B 근접(비용 1)], 남은 예산 1. 상한을 먼저 보면 A를 건너뛰고 B에 준다. 예산을 먼저 보면 A에서 break해 B는 못 받는다. M3-29 token_cost가 종마다 다르면 실제로 갈린다. 또 초과 회수 루프(`:1781-1786`)가 돌진자 Reserved를 회수할 때 역할 보유 수를 줄이지 않으면, 그 패스 동안 돌진자가 0명인데도 상한에 걸린다. 가로채기에서 희생자가 같은 역할이면 교환은 상한에 영향이 없는데도 "상한에 걸린 후보 제외" 규칙에 막힌다(맞은 돌진자가 보복하지 못함).
- 고칠 방향: 루프 본문 순서를 명시한다 — ① 상한이면 RoleBlockedCount++ 후 continue ② 예산이 모자라면 break ③ 부여·HeldQuarters·역할 보유 수 증가. 역할 보유 수는 초과 회수 뒤에 세거나 회수할 때 줄인다. RemainingCandidates는 상한 후보를 뺀 별도 배열로 만들고 OnDeck·가로채기는 그 배열만 본다. 가로채기에서 같은 역할끼리 교환을 허용할지 한 줄로 정한다.

### J5. 외부 이벤트 순서와 무관하게 결과가 같은지 검증하는 시험이 없다
- 근거: `NotifyMonsterDamaged:371`의 PendingAlertSlots는 피해 콜백 순서로 쌓이고(현재 UpdateEngagement는 이 순서대로 경보한다), 사망은 `HandleDeath → StopMonsterBrain → UnregisterMonster`(`TDMonsterCharacter.cpp:172-181, 487-501`) 경로로 콜백 순서대로 들어온다. 명세는 "SimulationId로 정렬"을 요구하지만 G1은 같은 순서로 두 번 돌린 해시만 비교한다.
- 재현: 빌더가 발견자 정렬이나 PendingPackDeaths 정렬을 빠뜨려도 G1은 초록이다.
- 고칠 방향: G2 `EventOrderInvariant`를 추가한다 — 같은 프레임에 고블린 둘을 (a) 1→2, (b) 2→1 순서로 피격·치명 처리한 두 실행의 스텝별 디버그 줄 해시가 같은지(호출자 표시 포함) 단언한다.

### J6. 사기 조각 배열(Morales)을 슬롯 재사용 때 초기화하는 규칙이 명세에 없다
- 근거: `RegisterMonster:300-311`은 Brain·Intents·Avoidances·Tactics를 슬롯마다 다시 만든다. spec-v1 §2.7에는 "Morales(슬롯 병렬)"와 "MakeFragment(def)"만 있고, 어디서 부르는지(Register)와 배열을 어디서 늘리는지가 없다.
- 재현: 정예 골렘이 죽은 슬롯에 고블린이 런타임 스폰(소환)되면 bIsImmune·bIsLeader·RallyImmuneUntilStep·RecentLossCount가 남는다. 그 고블린이 "우두머리"가 되어 죽을 때 주변을 비율 판정 없이 공황시키거나, 공황 면역이 된다.
- 고칠 방향: RegisterMonster의 Tactics 줄 옆에 `Morales[SlotIndex] = FTDPackMorale::MakeFragment(*Definition)`를 두고, Bodies를 늘리는 곳에서 함께 늘린다. bWasLeader는 UnregisterMonster에서 슬롯을 비우기 전에 읽는다고 명시한다.

## 경미

- L1. 시퀀스 안 MoveAway와 충돌(잠재): `StepState:151-165`는 시퀀스 안 Move에서 이미 StateStepsRemaining을 줄인다. §2.5의 "매 스텝 남은 스텝 감소·정체 판정"을 `!IsInSequence`로 가두지 않으면 시퀀스 MoveAway가 2배 빨리 끝나고, 정체 판정 때문에 커서가 넘어간다. 현재 출하 시퀀스에 MoveAway는 없다. 방향: 새 분기를 "시퀀스 밖"으로 한정한다고 명시한다.
- L2. 경직·빙결이 공황을 끊는다: `NotifyMonsterDamaged:376-380` 강타 → EnterStagger → InterruptActivity, `NotifyMonsterFrozen` → InterruptActivity. 후퇴는 끝나지만 RallyImmune는 이미 걸려 있다. 의도라면 명세에 "공황은 경직·빙결로 취소되고 결속 면역은 유지"를 한 줄 적는다.
- L3. 공황 → Flee 연쇄: BeginRetreat는 Flee 행동 쿨다운을 걸지 않는다. 체력이 낮은 고블린은 공황 2초 뒤 곧바로 Flee(1.5초)를 골라 최대 3.5초 동안 물러난다. 방향: 공황이 끝날 때 Flee류 쿨다운을 걸거나, 시험 기준을 "공황 + Flee ≤ 3.6초"로 한다.
- L4. 전염 범위: ②의 비율 판정은 `RecentLossCount ≥ 2`가 전제다. 손실을 1번 이하로 본 몬스터는 공황한 아군이 아무리 많아도 전염되지 않는다. "전염"이 목격자 재평가일 뿐이라는 점을 명세와 S2 지표에 적는다(설계 확인 필요).
- L5. 지지·압력을 셀 대상의 상태가 정해지지 않음: "비공황 아군(반경 1200)"에 Home·Returning·빙결·경직 몬스터를 넣는지 불명확하다 → C++과 시뮬이 어긋난다. 방향: "활성·생존·같은 팀·Engaged"로 한정한다고 명시한다.
- L6. 호출자 규칙의 대상 비교: 대상 없이 Engage가 성립하는 경로(`Engage:1169-1183`에서 AcquireTarget 실패)가 있으면 M.Target == D.Target이 둘 다 null이라 참 → Silent가 된다. 방향: "D.Target이 유효"를 전제 조건에 넣는다.
- L7. BeginReturn이 AlarmCallStep을 지우지 않아, 귀환 중인 이전 호출자가 3초 동안 새 호출자를 막는다. 방향: BeginReturn에서 AlarmCallStep = 0도 함께 설정한다.
- L8. (가정) 팀 필터가 비대칭이다: 기존 경보(`UpdateEngagement:680-703`)와 새 경보·호출자 규칙에는 팀 조건이 없고, 사기만 팀을 본다. 팀이 다른 몬스터들이 같은 플레이어를 노리면 다른 팀의 경보로 깨어나고, 다른 팀 호출자가 내 호출을 막는다. 방향: 경보 예약·호출자 판정에 같은 팀 조건을 넣을지 정한다. 출하 종들의 team 값은 확인하지 않았다.
- L9. CVar 전환 잔재: `ResetTactics:1243-1258`은 PendingPackDeaths·bShouldEvaluateMorale을 비우지 않는다(명세 §1.2는 경보만 비운다). 켜짐 → 프레임 사이 사망 → 꺼짐 → 한참 뒤 켜짐이면, 묵은 사망이 Now 기준으로 처리돼 가짜 공황이 생긴다. 방향: ResetTactics에서 두 상태도 비운다.
- L10. (가정) 원거리 띠 바닥은 MoveToBand가 선택됐을 때만 작동한다. 점수기는 원래 거리로 KeepDistance를 매긴다(`Archer.json:8-10`, 중심 648인 반전 가우스). 바닥이 700을 넘으면(큰 몸집의 바깥 고리) 700 근처에서 KeepDistance ≈ 0.06, Idle = 0.05라 궁수가 바닥 안쪽에 그냥 서 있을 수 있다. C3 예(바닥 640)는 통과할 것으로 보인다. 방향: S4에서 바닥 800인 경우를 재 보고, 필요하면 바닥을 문맥 입력으로 점수기에 넘긴다.
- L11. 상한 때문에 토큰이 비는 구간: ReturnFinishedTacticsToken의 HasOtherTokenCandidate(현재 `:1976-1987` 부근)는 상한에 걸린 돌진자도 "다른 후보"로 센다. 그래서 근접 보유자가 토큰을 놓아도 받을 후보가 없어 최대 1패스(8스텝) 동안 토큰이 비고, 놓은 쪽은 RegrantCooldown 48스텝을 받는다. 방향: HasOtherTokenCandidate가 상한을 반영하게 한다.
- L12. (가정) 공황 후퇴와 교전 포기: 후퇴는 대상 반대쪽으로 최대 128스텝 × 이동 속도(고블린 약 900cm)를 간다. 집에서 멀리 끌려 나온 무리는 `MaxChaseDistanceFromHome`(`UpdateEngagement:636-640`)을 넘어 BeginReturn에 들어갈 수 있고, B1의 "Returning 0" 단언이 배치에 따라 흔들릴 수 있다. 방향: B1 배치를 집 근처로 고정하거나, 후퇴 방향을 대상 반대쪽과 집 쪽 중 짧은 쪽으로 한다.

## 참고(결함 아님, 확인함)
- 경보 예약은 Unregister 때 슬롯별로 지우고, 처리 시 세대·활성·Home·차단·대상 생존·영역을 확인한다 → 귀환·사망·세대 변경 뒤 잔재가 없다. 새 예약은 Due ≥ Now+8이라 같은 스텝 안 연쇄가 없다. 사기 전염은 다음 스텝으로 미뤄지고 RallyImmune 때문에 유한하게 끝난다.
- 시간 제한 후퇴가 영구 고착되는 경로는 없다: Engaged→Returning 전환은 모두 BeginReturn(InterruptActivity)을 거치고 빙결도 InterruptActivity를 부른다. 그래서 StepState가 불리지 않는 경로에서는 후퇴가 이미 끝나 있다.
- A1 수치: 거리 700 → 8 + 8×700/900 = 14스텝으로 단언 범위 8~16 안이다. B5: 정체 24스텝 + 첫 스텝 기준 설정 = 25스텝으로 범위 24~26 안이다.
- ComputeHopDelaySteps(int32)에 넘길 float→int 변환(버림/반올림)이 명세에 없다. 경계 거리에서 1스텝 차이가 나므로 시뮬과 같은 규칙으로 맞추기를 권한다.
# 반박 C — 코드 연결·시험 판별력 (명세 v1 대 M3-29 사본 + 현재 트리)

기준: `Saved/AgentOps/20261018/m329/staging/Source/TDGame/MonsterAI/*`(아래 "사본"), 현재 트리 `Source/TDGame/...`. 줄 번호는 사본 기준(따로 적은 것 제외). 빌드·실행 없음, 정적 대조만.
집계: 치명 2 · 주요 8(그중 가정 1) · 경미 5.

## 치명

### X1. 새 시험 파일은 서브시스템 비공개 상태에 접근할 수 없다 (C1·C2 컴파일 불가)
- 근거: `TDMonsterThinkSubsystem.h:105-107` friend는 `FTDMonsterThinkTickFunction`·`FTDMonsterThinkSubsystemTestAccess`(정의: `Tests/TDCircleAvoidanceTests.cpp:30`)·`FTDCombatTacticsTestAccess`(정의: `Tests/TDCombatTacticsTests.cpp:550`)뿐.
- 문제: C1·C2는 "등록 후 역할 리터럴 3/4"로 `Tactics[Slot].Role`(private)을 써야 한다. 새 `TDPackBehaviorTests.cpp`에서 기존 접근자 구조체를 다시 정의하면 ODR 위반(통합 빌드에서 같은 묶음이면 재정의 오류), 새 이름 구조체는 friend가 아니라 접근 불가. 명세 4절 "기존 테스트 접근자 패턴만"으로는 성립하지 않는다.
- 고칠 방향: (a) C1·C2(및 비공개 접근이 필요한 시험)를 `TDCombatTacticsTests.cpp`에 두고 `FTDCombatTacticsTestAccess`에 역할 설정 정적 함수 추가, 또는 (b) 빨강 커밋에 동작 무관 헤더 변경 `friend struct FTDPackBehaviorTestAccess;`를 명시 허용. 둘 중 하나를 명세에 고정.

### X2. 후퇴 정체 판정 문구를 글자대로 구현하면 모든 후퇴가 0.375초에 끝난다 (B4 판별 불가)
- 근거: 명세 2.5 "대상까지 평면 거리가 지금까지 최대보다 30 넘게 늘면 정체 0, 아니면 정체 +1"; 두뇌 조각 `RetreatBestDistance`.
- 문제: `RetreatBestDistance`를 매 스텝 최대값으로 갱신하면 스텝당 증가량이 30cm 미만인 한(고블린 420x1.1/64 = 약 7.2cm/스텝, B4 10cm/스텝) 정체가 매 스텝 +1 -> 24스텝에 "막힘" 종료. B4(96±1)는 실패하고 실제 게임에서 Flee·공황이 사실상 무력화된다. 기준점을 "30cm 넘게 늘었을 때만 갱신(앵커)"으로 읽어야 B4가 맞는다.
- 고칠 방향: "RetreatBestDistance는 진척(>30cm)으로 정체를 0으로 만들 때만 현재 거리로 갱신(초기 -1 -> 첫 스텝에 설정)"으로 명문화. B4에 "스텝당 5cm 이동(30 미만)도 96스텝 유지" 표본 추가.

## 주요

### M1. 역할 비교 교체 목록 누락 2곳
- 근거: 사본 `TDMonsterThinkSubsystem.cpp:59-62` `GetGrantRadiusCm`(`Role == Ranged`; 호출처 `IsTokenCandidate:1987`, `RevokeInvalidReservation:1446`), `:1714` `AssignInnerSectors`의 `IsInnerHolder`(`Role == Melee`). 명세 3.1 목록(GetTacticsPoolIndex·AssignWaitSectors·AssignRangedSectors·ResolveTacticsSlotKind·IsAvoidingTacticsTarget·FillTacticsContext)에 없음.
- 문제: Support는 부여 반경이 근접값 900(회수 1125)으로 묶여 사거리 900 초과 지원 종은 토큰을 못 받는다. Charger는 안쪽 칸을 못 받아 `ResolveTacticsSlotKind:2157` Inner 분기가 죽고 토큰 보유 돌진자가 칸 없이 접근한다.
- 고칠 방향: 두 곳을 `IsRangedLaneRole`/`IsMeleeLaneRole`로 교체 목록에 추가. 완료 점검 `rg -n "ETDTacticsRole::(Melee|Ranged)" Source/TDGame/MonsterAI`가 MakeFragment·차선 함수·시험만 남는지.

### M2. 슬롯 병렬 `Morales`와 새 대기열의 초기화·재사용·리셋 미명시
- 근거: `RegisterMonster:276-326`(AddDefaulted 290-295, 재설정 305-312), `Deinitialize:256-274`, `ResetTactics:1244-1259`(Role·Reach·ReserveCost만 보존). 명세 2.7은 "슬롯 병렬"만, 1.2는 경보 정리만 적음.
- 문제: `Morales.AddDefaulted` 누락 시 범위 밖 접근, 재사용 슬롯에 이전 FearPending/RallyImmune 잔존, CVar 전환 시 `PendingPackDeaths`·`bShouldEvaluateMorale` 잔존, 두뇌 `AlarmRole`이 CVar 0에서도 남아 디버그 줄에 `alarm=caller` 계속 표시.
- 고칠 방향: RegisterMonster에 `Morales.AddDefaulted()`·`Morales[Slot] = FTDPackMorale::MakeFragment(*Definition)`; Deinitialize에 `Morales/PendingPackDeaths/PendingAlarms.Reset()`; ResetTactics에 사기 조각 재생성(면역·우두머리 보존)·`PendingPackDeaths.Reset()`·`PendingAlarms.Reset()`·`bShouldEvaluateMorale=false`·전 슬롯 `AlarmRole=None, AlarmHop=0`.

### M3. C3가 구현 전에도 통과할 수 있다 (궁수 배치 미지정)
- 근거: 사본 `TDMonsterActionExecutor.cpp:362-385` MoveToBand — 띠 안이어도 칸 각도 밖이면 `ComputeTacticsSlotPoint:449-453`의 중간 반경 (500+800)/2 = 650으로 이동. 원거리 칸 12개·오프셋 15도(중심 15+30k도), 반각 약 15도, `FindBestSector` 남쪽 벌점(`TDCombatTactics.cpp:313`).
- 문제: 궁수가 칸 중심에서 벗어나 있거나 남쪽이면 구현 전에도 650으로 가서 "중앙값 >= 620"이 통과 -> 빨강이 아님.
- 고칠 방향: 궁수를 남쪽이 아닌 칸 중심 방위(예: 45도, 105도)·540cm에 두고, 구현 전 실행에서 "6초 뒤 궁수 거리 < 600" 기준 단언을 함께 둔다(또는 "바닥 = 대기 고리+150 이상"을 직접 단언).

### M4. 시험이 proposed 데이터 변경에 의존
- 근거: 픽스처는 DefinitionId로 파일 정의만 생성(`TDCombatTacticsTests.cpp:70-80`, 인라인 정의는 캐릭터로 생성 불가). B1은 StoneGolem `"elite": true`, C4·RoleClassification(`TDCombatTacticsTests.cpp` 1007 부근 Hyena=Melee 기대)은 Hyena `"role":"Charger"` 필요. 명세 2.2·3.1은 "권장/MD-48 제안".
- 문제: 데이터가 적용되지 않으면 B1·C4는 구현 뒤에도 실패.
- 고칠 방향: 데이터 2건(StoneGolem·BookHead_Brute elite, Hyena Charger)을 구현 범위 필수로 격상하거나, B1 우두머리를 Ogre_Boss(`boss`)로 바꾼다.

### M5. 통합 빌드 익명 네임스페이스 충돌 위험 (L-build-03)
- 근거: 새 .cpp 3개(TDPackAlarm·TDPackMorale·Tests/TDPackBehaviorTests) 추가로 묶음 구성 변경. 기존 익명 ns 이름: `FTDTacticsTestWorld`·`SpawnTacticsPlayerPawn`·`ReadTacticsLineField`·`AccumulateTacticsHash`(TDCombatTacticsTests.cpp:51-207), `ResolveSingle`·`ResolveSingleWithSpells`·`MakeKeyProbeText`·`SpawnKitingPlayerPawn`(TDMonsterAITests.cpp:93-108 외), `ToPlanar`·`GetDirectionToTarget`(Executor.cpp:6-16), `SquareCm`·`PlanarDistance`(ThinkSubsystem.cpp:49,188).
- 문제: 새 시험이 픽스처·파서를 복사하면 같은 이름 재정의 오류.
- 고칠 방향: 새 파일 익명 ns 이름에 Pack 접두(FTDPackTestWorld, ResolvePackDefinition, ReadPackLineField 등), 새 규칙 파일은 익명 ns 없이 클래스 정적 함수만. 빌드 시 `-DisableAdaptiveUnity` 1회.

### M6. 공황 슬롯의 칸 배정 제외·판정 위치 미명시
- 근거: `RunKernelStep:524-586` 순서 Sense -> UpdateEngagement -> UpdateTactics(SyncTacticsSlots·고리 32스텝·토큰 8스텝) -> ThinkAndAct. `AssignWaitSectors:1613-1616`·`AssignRangedSectors:1680-1683` 필터는 역할만 봄.
- 문제: 명세 2.5 "공황 중 슬롯은 대기·안쪽·원거리 칸 배정에서 빠진다"를 구현할 자리가 지정되지 않음. 사기 판정을 UpdateTactics 뒤에 두면 Reserved 토큰·대기 칸이 다음 패스까지(최대 32스텝) 남는다.
- 고칠 방향: 사기 판정·BeginRetreat를 UpdateEngagement 직후, UpdateTactics 전에 두고, 세 칸 배정 필터와 `ResolveTacticsSlotKind`에 "시간 제한 후퇴 중이면 제외/None"을 명시.

### M7. Flee(StartPrimitive 경로)의 후퇴 추적 초기화 누락
- 근거: 명세 2.5는 `BeginRetreat`에만 "후퇴 추적 초기화", 반면 `seconds` MoveAway는 `StartPrimitive:205-213`으로 시작. `InterruptActivity:129-144`·`CompleteActivity:299-320`는 새 필드를 모름.
- 문제: 두 번째 Flee가 이전 RetreatStallSteps·RetreatBestDistance를 물려받아 즉시 "막힘" 종료 가능(B6 판별에 영향).
- 고칠 방향: 시간 제한 후퇴 시작 공통 함수에서(BeginRetreat·StartPrimitive 둘 다) `RetreatStallSteps=0; RetreatBestDistance=-1`.

### M8 (가정). MD-43 데이터가 기존 시험을 흔들 수 있다
- 근거: `FleeingMonsterDoesNotStallTokens`(TDCombatTacticsTests.cpp:1192-1255): 고블린 3, 300cm 고리, 체력 <=24에서 6초 안 `action=Flee` 기대. 새 거리 고려(로지스틱 m -12, c 0.35) 적용 시 약 350cm에서 Flee 약 3x0.96x0.5x0.67 = 0.96 대 Approach 약 0.84로 근소(구 1.44). 1.5초 뒤 복귀한 도망자가 토큰을 다시 다퉈 "다른 고블린 최장 간격 <=128" 단언에도 영향 가능. `HitKitingIsCappedByMaxChase`·`KitingDoesNotYoYo`도 피격으로 체력이 내려가면 Flee 형태가 바뀜.
- 고칠 방향: 변경 전후 세 시험을 먼저 돌려 비교, 깨지면 시험 기대("Flee 선택 또는 시간 제한 후퇴 시작")나 데이터 커브를 조정하고 근거를 명세에 남긴다.

## 경미
- m1. 발견자 정의: `NotifyMonsterDamaged:369-372`는 Returning->Engaged(귀환 중 피격, ReturningReengageRadius 안)도 `PendingAlertSlots`에 넣는다. 명세 1.2 "Home->Engaged"와 다름 -> 문구를 "비교전->교전(근접 발견·피격)"으로.
- m2. `RoleBlockedCount` 초기화 위치 미명시 -> GrantPoolTokens 시작에서 0(HasMeleeQueue는 고리 패스·문맥에서 매 스텝 읽힘).
- m3. `CanThink` 거짓 동안 `ThinkAndAct:722`의 RefreshTarget·`:734` EnterPhase가 최대 2초 지연 — 의도면 명세에 적기.
- m4. 로더 오류 문구 "expected Melee or Ranged"(M3-29 2.1-3)를 4값으로 갱신 필요. `DefinitionTacticsKeys`는 `"boss": true`·`did you mean 'Ranged'` 부분 문자열만 보므로 영향 없음.
- m5. `Docs/Lessons/combat.md` L-combat-02의 "캐릭터 사망은 PIE로" 해결이 현재 코드(`TDMonsterCharacter.cpp:187-192` HasInstance 가드)와 어긋남 -> 부모가 갱신.

## 확인된 것(문제 없음)
- A1: B(1400,700)는 1565cm로 스스로 발견 못 함(Home 정지). 구현 전 tB=tA(700 <= AlertRadius 900), C 미교전 -> 빨강. 구현 뒤 tB=tA+14, tC=tB+14, D는 B·C에서 1400cm -> 미도달. 판별력 있음.
- A2·A3·G1: 문자열·CVar만 사용. 디버그 줄 끝 필드 추가는 `ReadTacticsLineField:161-177`(공백 구분)·`engaged=1` 포함 검사·해시 비교를 깨지 않음.
- B6 구현 전: Flee 3x0.9375x1 = 약 2.81 > Approach 0.9, 462cm/s 도주 -> LoseTargetRadius 3500 도달 약 6.7초 + GiveUp 2초 -> Returning(MaxChase 4500 전) -> 빨강 확실. 구현 뒤 400cm에서 Flee 약 1.55로 1.5초 후퇴 약 690cm -> 재교전.
- B5: 초기 -1 -> 1스텝 진척 처리 후 24스텝 정체 -> 25스텝 Idle(24~26 안).
- C1·C2 구현 전: 근접 4마리 기본 토큰 2, 원거리 3마리 2 -> 보유 2 -> 빨강.
- BeginRetreat의 InterruptActivity: 공황은 CanThink(Idle/Move)에서만 시작하므로 시전 취소 없음. Committed는 시전 중에만 있고 끝나면 `ReturnFinishedTacticsToken:1404-1428`이 `CanKeepTacticsToken`(MoveAway 거부, :1994-1998)으로 해제. Reserved는 `RevokeInvalidReservation:1449` bIsFleeing로 회수.
- `GetTacticsPoolIndex` 변경: Boss는 호출처마다 먼저 걸러지므로 영향 없음.
- 픽스처 치명 피해(가정, 기존 픽스처 시험 중 몬스터를 죽이는 것 없음): `HandleDeath`(현재 트리 `TDMonsterCharacter.cpp:172-185`) -> `StopMonsterBrain`(:488-500) -> UnregisterMonster 시점 체력 <= 0이라 `IsAlive` 거짓(`TDCombatComponent.cpp:161-164`, OnDeath :389) -> 사망 기록됨. `BroadcastDeathMessage` 가드(:190), `StartRagdoll`은 PhysicsAsset 없으면 반환(`TDCombatCharacter.cpp:449`), 수명 8초·가라앉기 타이머는 월드 틱으로 안전. 빌더는 B 시험 첫 단언으로 "치명 피해 후 GetMonsterDebugLines 수 감소"를 확인할 것.
- `ReturningIgnoresAllyAlert`(TDMonsterAITests.cpp:853-881): 귀환 중 슬롯은 예약 대상이 아니고 도착 후 ReengageBlock이 걸려 계속 통과.
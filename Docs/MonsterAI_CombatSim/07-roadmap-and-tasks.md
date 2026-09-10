[← 인덱스로](../MonsterAI_CombatSim_Plan.md)
# 07. 로드맵과 할 일 대장

## 이 문서가 답하는 질문

1. 몬스터 AI 와 결정론 전투 시뮬레이터를 어떤 순서(Phase 0~4)로 만들고, 각 단계의 "끝"을 무엇으로 판정하는가?
2. 기존 전투 코드에서 정확히 어느 줄을 왜 바꾸는가(Phase 0 최소 집합)?
3. 에이전트가 바로 집어 들 수 있는 할 일 항목(M0~M4)은 무엇이고, 각 항목은 어떤 명령으로 검증하는가?
4. 사용자가 결정해야 진행할 수 있는 항목은 무엇인가?
5. 실패 가능성이 큰 지점 12개와 그 감시 지표는 무엇인가?

## 결론 요약(결정 문장)

- Phase 0(1주)은 기존 코드 변경 최소 집합([결정 D38](#2-기존-코드-변경-목록phase-0-d38))과 **기존 자동화 테스트 28개의 실제 통과**로 끝난다. 28개는 GAS(Gameplay Ability System, 게임플레이 어빌리티 시스템) 전환 후 실행 결과가 기록되지 않은 상태(`Docs/TDGASFoundation.md:85` 는 컴파일 완료만 기록)라 "이미 증명됨"이 아니라 "먼저 확인할 것"이다.
- Phase 1(2~3주)이 끝나면 "고블린 10마리 vs 플레이어 규칙 봇" 승률이 헤드리스 커맨드렛에서 나오고, 같은 시드 2회·다른 프로세스 1회 해시가 일치한다.
- Phase 2(2~3주)가 끝나면 LLM(대형 언어 모델)이 종당 컴파일 0회로 몬스터를 만들고 30줄 요약을 읽는 루프가 닫힌다.
- Phase 3(3~4주)은 게임 몸·규모·공격 시간표이며 KPI(핵심 성과 지표, 300마리 몬스터 몫 ≤ 4.0ms)는 추정이 아니라 실측으로 판정한다.
- Phase 4는 조건부다. 자체 HTN(Hierarchical Task Network, 계층적 태스크 네트워크)·행동 복제 봇·CMA(Covariance Matrix Adaptation, 공분산 행렬 적응) 튜닝·ISM(Instanced Static Mesh, 인스턴스 스태틱 메시)/VAT(Vertex Animation Texture, 정점 애니메이션 텍스처)·Mass 스파이크는 각각 "요구가 실제로 생겼을 때"만 연다.
- 할 일 항목 50개(M0 8·M1 13·M2 12·M3 12·M4 5)는 이 문서 안에 둔다. 06 문서가 제안한 항목은 06 의 제안 번호(M2-15, M3-13, M3-15)를 그대로 써서 번호에 빈 자리가 있다. `Docs/Tasks/` 는 월드·던전·PCG 대장이므로 그 파일들을 수정하지 않으며, 사용자가 원하면 나중에 통합할 수 있다.
- 시뮬 러너는 `Step` 을 직접 호출하지 않는다. 게임과 시뮬 모두 `UTDMonsterThinkSubsystem` 의 틱 함수가 `World->Tick` 안에서 정확히 1회 돈다(이중 스텝 금지, 결정 D15).

---

## 1. Phase 0~4 로드맵

규모의 줄 수·기간은 모두 **추정**이다. 설계안 네 편이 각자 적은 Phase 0 규모 추정(A 1,200 / B 600 / C 600 / D 300줄; D 의 "3일·약 300줄" 은 심사 판정에 인용됨)을 결정 기록의 범위에 맞춰 다시 셌다. Phase 1 의 항목별 추정(등록표·입력 스냅샷 300 / 정의 USTRUCT·로더 350 / 곡선·점수기 250 / FSM 200 / 서브시스템·슬롯 400 / 몸 인터페이스·시뮬 몸·원시 2종 300 / 검증기 1·2단·커맨드렛 250 / 세션·커맨드렛·시나리오 400 / 해시 150 / 테스트 350 ≈ 2,950줄)은 심사가 "에이전트에게 과하다" 고 본 B 의 Phase 1 3,000줄과 수치는 비슷하지만, B 가 Phase 1 에 넣은 공간 해시·플로우 필드·SoA(Structure of Arrays, 배열 구조체) 브리지를 Phase 3 으로 미룬 구성이다. 선행 조건은 "이전 Phase 의 완료 조건 전부"가 기본이다.

| Phase | 목표 | 완료 조건(검증 가능) | 산출물 | 예상 규모(추정) | 선행 |
|---|---|---|---|---|---|
| **0 기반** | 기존 전투 코드를 시드·순서 결정적으로 만들고, 시뮬레이터가 딛고 설 픽스처와 기준선을 확보 | (a) 28개 기존 테스트가 `Automation RunTests TDGame.Combat` 로 실제 통과 (b) `TDGame.MonsterAI.NoGlobalRandom` 통과(`Source/TDGame/{Combat,MonsterAI,CombatSim}` 에서 `FMath::FRand/Rand/RandRange/SRand` 0건) (c) 종 1개 추가 시 증분 빌드 시간이 문서에 기록됨 | `FTDDamageContext` 스트림, `SimulationId`, `CombatSim/TDScopedCombatWorld.h`, `TDDamageFormula::Compute`, 컨트롤러 없는 `ATDMonsterCharacter` | C++ 약 400~600줄, 1주 | 없음 |
| **1 두뇌·시뮬 최소판** | 유틸리티 두뇌 + FSM 이 서브시스템 슬롯 배열 위에서 돌고, 커맨드렛이 승률을 낸다 | (a) `Goblin_Melee.json` 10마리 vs 규칙 봇 시나리오가 `-run=TDCombatSim` 으로 승패 산출 (b) `TDGame.CombatSim.Determinism` 통과: 같은 프로세스 2회 + 다른 프로세스 1회 + 골든 해시 일치, 스텝당 사고 횟수 검출 통과 (c) 검증기 1·2단이 의도적 오류 파일 10종을 위치(문법 오류는 파일:줄, 미지 키·형 불일치는 JSON 경로)와 함께 거부 (d) PIE(Play In Editor, 에디터 내 실행) 300마리 `stat TDMonsterAI` 로 think/move/judge 채널 단가가 실측되어 예산표에 기록됨 | `MonsterAI/` 등록표·로더·점수기·FSM·`UTDMonsterThinkSubsystem`, `CombatSim/` 세션·커맨드렛·해시, `Content/MonsterAI/Definitions/Goblin_Melee.json` | C++ 약 2,500~3,000줄, 2~3주 | Phase 0 |
| **2 밸런스 툴 완성** | 입력/출력 스키마·분석 스크립트·팬아웃·페르소나로 LLM 제작 루프를 닫는다. 난이도 축(M2-15)을 시나리오 입력에 넣는다 | (a) 1,000시드 배치가 8프로세스로 완주하고 승률 ± 표준오차(n=1,000 → ±1.6%p) 리포트 생성 (b) `summarize_batch.py` 가 30줄 요약을 냄 (c) 검증 3단(5초 생존 시뮬) 통과 (d) 스키마 덤프 커맨드렛 재실행 결과가 저장본과 diff 0 (e) LLM 이 새 잡몹 1종을 컴파일 0회로 검증기·생존 시뮬까지 통과 | 시나리오/결과 USTRUCT+JSON, 결정 로그 JSONL(JSON Lines, 한 줄 한 레코드), 파이썬 4종, 페르소나 3종, `Archer.json`·`Ogre_Boss.json`, `monster-definition.schema.json`·`inputs.md`·`actions.md` | C++ 약 1,500줄 + 파이썬 약 400줄, 2~3주 | Phase 1 |
| **3 게임 몸·규모·시간표** | 실제 맵에서 300마리가 돌고, 몽타주 몬스터가 생기면 시간표가 권위 판정이 된다 | (a) PIE 300마리에서 몬스터 몫 합 ≤ 4.0ms(실측, 조건 명시) (b) 화면 밖 몬스터 사고 주기가 주기표대로 줄고 이동·판정은 매 스텝 유지 (c) B단계 정합 테스트(적중 시각 ±1 스텝·총 피해 동일) 통과 (d) JSON→C++ 상수표 커맨드렛이 같은 해시를 내는 산출물 생성 (e) C단계 통계 등가 리포트 1회 (f) CC(Crowd Control, 군중제어)·넉백 순수 함수(M3-13)와 세이브/로드 왕복 해시(M3-15) 테스트 통과 | `ITDMonsterBody` 게임 구현 2종, `THierarchicalHashGrid2D` 탐색, `PeriodTable[4][4]`, 플로우 필드, `FTDAttackTimetable` 추출 커맨드렛, 되돌림 커맨드렛 | C++ 약 2,500줄, 3~4주 | Phase 2 |
| **4 확장(조건부)** | 실제 요구가 생긴 항목만 연다 | 항목별: HTN 보스 시나리오 결정론 게이트 통과 / BC(Behavior Cloning, 행동 복제) 봇 추론 2회 바이트 동일 / CMA 튜닝이 목표 승률 밴드 진입 / ISM·VAT 후열 1종 / Mass 스파이크 보고서 | 항목별 별도 | C++ 약 2,000~2,500줄, 4~6주(항목 합) | Phase 3 + 사용자 결정 |

각 Phase 는 이전 Phase 의 테스트를 유지한 채 진행한다. 상세 설계는 [02 아키텍처](02-architecture-and-definition-format.md), [03 틱·규모](03-tick-and-scale.md), [04 시뮬레이터](04-combat-simulator.md), [05 ML](05-ml-and-generative-ai.md)에 있고, 요구 밖 고려사항(텔레메트리·대시보드·벤치 하네스 등)과 그 할 일 ID 후보는 [06 요구 밖 고려사항](06-beyond-the-ask.md)에 있다. 이 문서는 순서와 완료 조건만 다룬다. 06 의 나머지 후보(M2-13·M2-14·M3-12 등)는 06 미결 사항이 답을 받은 뒤 이 대장에 추가한다.

---

## 2. 기존 코드 변경 목록(Phase 0, D38)

심사가 채택한 D §7 최소 집합에 C 의 `TDDamageFormula::Compute` 분리·`bAuthoritativeHitJudgment` 게이트·`ServerOnly` 옵션을 더한 것이다. 줄 번호는 2026-09-09 소스에서 직접 확인했다. **변경하지 않는 것**: `UTDDamageDefinition`·GE(GameplayEffect, 게임플레이 이펙트) CDO(Class Default Object, 클래스 기본 객체)·어빌리티 구조, `Variant_TwinStick`, `Docs/Tasks/*`, 월드젠 소유 파일.

| # | 파일:줄 | 무엇을 | 왜 | 근거 |
|---|---|---|---|---|
| 1 | `Source/TDGame/Combat/TDDamageTypes.h:187` `FTDDamageContext` | `FRandomStream*`(또는 스트림 핸들) 필드 추가. 널이면 `UTDDamageSubsystem` 의 Combat 스트림 | 치명타·산포가 시뮬 소유 스트림을 쓰도록 | project-current-combat-code 결론 1; engine-gas-determinism 바꿔야 하는 것 1 |
| 2 | `Source/TDGame/Combat/TDCombatComponent.cpp:220` | `FMath::FRand()` → 컨텍스트 스트림 `FRand()` | 전역 `rand()` 는 시드 불가 | project-current-combat-code 결론 1(`GenericPlatformMath.h:609-617`); engine-determinism-headless 결론 4 |
| 3 | `Source/TDGame/Combat/TDDamageSubsystem.cpp:203-204` | `FMath::FRand()` 2회 → 컨텍스트 스트림, 지역 변수로 순차 대입(인자 평가 순서 함정 회피) | 동일 | 동일 |
| 4 | `Source/TDGame/Combat/TDCombatComponent.cpp:221-223` | 저항·배율 공식을 `TDDamageFormula::Compute(RawDamage, bIsCritical, Multiplier, Resistance)` 정적 순수 함수로 분리 | GAS 경로와 미래 경량 커널이 같은 공식 사용 | engine-gas-determinism §규모별 선택 지침("GAS 와 공식 함수 공유")·결론 9(C §7, B §8-8) |
| 5 | `Source/TDGame/Combat/TDCombatComponent.h` | `int32 SimulationId`(스폰 순번) + 접근자 | 정렬·동률·해시·로그 키 | 결정 D14·D30 |
| 6 | `Source/TDGame/Combat/TDDamageSubsystem.cpp:247-270` `GatherTargets` | 결과를 `SimulationId` 오름차순 정렬. 등록 시 순번 부여 API 추가 | `TSet` 등록 순서 의존 제거 | project-current-combat-code 결론 4 |
| 7 | `Source/TDGame/Combat/TDDamageEntity.cpp:305-307` | `GetUniqueID()` 동률 판정 → `SimulationId` | 전역 오브젝트 인덱스는 프로세스 간 불일치 | project-current-combat-code 결론 4 |
| 8 | `Source/TDGame/Combat/TDDamageEntity.cpp:497-498` | 동일 교체(호밍 대상 정렬 람다) | 동일 | 동일 |
| 9 | `Source/TDGame/Combat/TDDamageEntity.cpp:17-24` | `UStaticMeshComponent`·`UNiagaraComponent` 생성을 헤드리스 게이트(`FApp::CanEverRender()` 또는 서브시스템 `bIsHeadless`)로 감쌈 | 헤드리스 오브젝트 비용 | project-current-combat-code 결론 8 |
| 10 | `Source/TDGame/Combat/TDDamageExamples.cpp:12` | `bDrawDebug = true` → 기본 false, 생성 함수에 `bDebug` 인자 | 매 틱 `DrawShape` 제거 | project-current-combat-code 결론 10 주의 |
| 11 | `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:30-88` | `FTDScopedCombatWorld` 를 `Source/TDGame/CombatSim/TDScopedCombatWorld.h` 로 승격. 시드·스텝·해시 옵션 추가, **기본 스텝 0.02 유지** | 세션·테스트·커맨드렛 공유 | engine-gas-determinism "그대로 쓰는 것"; 결정 D27·D28 |
| 12 | `Source/TDGame/Combat/Characters/TDMonsterCharacter.cpp:10-11` | `AutoPossessAI = Disabled`, `AIControllerClass` 제거. 정예 경로는 컨트롤러 없는 이동 요청 | 컨트롤러 액터 틱 제거, `IsLocallyControlled` 분기 회피. 정예 경로 추종 컴포넌트의 소유 주체(Pawn 소유 `UPathFollowingComponent` 또는 서브시스템 직접 `FindPathSync`)는 미확인이며 M3-01 에서 결정한다(Phase 3 플로우 필드 전까지는 직선 접근, D17) | engine-behaviortree-tick 결론 11; engine-gas-determinism 바꿔야 하는 것 4; engine-movement-anim-scale 결론 4(PathFollowing 은 `INavMovementInterface` 만 요구) |
| 13 | `Source/TDGame/Combat/GAS/TDDamageGameplayAbility.cpp:14-15` | 몬스터용 `NetExecutionPolicy = ServerOnly` 옵션(파생 또는 설정) | 컨트롤러 없는 폰은 현행 LocalOnly 도 권한자로 활성된다(`FGameplayAbilityActorInfo::IsLocallyControlled` 가 `IsNetAuthority()` 로 떨어짐, `GameplayAbilityTypes.cpp:107-126`). ServerOnly 는 TwinStick 호환 경로처럼 컨트롤러가 붙는 경우의 보험(옵션) | engine-gas-determinism 바꿔야 하는 것 4·미확인 8 |
| 14 | `Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.*` | `bAuthoritativeHitJudgment` 게이트 **추가만**(기본 true = 플레이어 경로 현행 유지). Phase 3 에서 몬스터 시간표 도입 시 몬스터만 false | 단일 판정 경로 준비 | 결정 D19·D20 |
| 15 | `Source/TDGame/TDGame.Build.cs:11` | `Json`, `JsonUtilities` 의존 추가(Phase 1 로더 직전) | JSON 정본 파서 | engine-misc-decision-tools R13; 결정 D13 |

D38 밖이지만 Phase 1 에서 반드시 따라오는 변경 1건: `Source/TDGame/Combat/TDCombatComponentStatus.cpp:434-442` 의 `Brain->PauseLogic` 경로는 새 두뇌가 `UBrainComponent` 가 아니므로 `UTDMonsterThinkSubsystem::SetFrozen(SimulationId, true)` 를 병행 호출한다(TwinStick 호환 경로는 유지). 참고로 BT 의 `PauseLogic` 이 보조 노드 틱을 못 멈추는 사실의 근거는 engine-behaviortree-tick **상세 1-4·피할 것 5**(`BehaviorTreeComponent.cpp:1760-1775`)이지 결론 12 가 아니다.

이중 스텝 금지의 코드 규약: 시뮬 세션은 `World->Tick(LEVELTICK_All, 1/64)` 만 호출하고, `UTDMonsterThinkSubsystem` 의 `Step` 을 직접 부르는 공개 함수를 두지 않는다. 게이트 테스트가 "스텝당 사고 횟수 == 예상"으로 검출한다(검출 방식은 결정 D15 의 규약; `++GFrameCounter` 없이 두 번째 `World->Tick` 이 틱을 건너뛰는 사실은 engine-determinism-headless 결론 1).

---

## 3. 할 일 대장(M0~M4)

### 3.1 항목 형식과 명령 약속

형식은 [Docs/Tasks/README.md](../Tasks/README.md) 와 같다. ID 는 `M<phase>-<번호>`. 아래 검증란의 네 명령은 다음 전체 명령을 줄인 것이다.

```text
BUILD:
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TDGameEditor Win64 Development
  -Project="C:\Project\TDGame\TDGame.uproject" -WaitMutex -NoHotReload

TEST <필터> <로그이름>:
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Project\TDGame\TDGame.uproject"
  -ExecCmds="Automation RunTests <필터>" -TestExit="Automation Test Queue Empty"
  -unattended -NullRHI -NoSplash -NoSound -log=<로그이름>.log

SIM <시나리오.json> [N]:
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Project\TDGame\TDGame.uproject"
  -run=TDCombatSim -Scenario=<시나리오.json> -SeedStart=0 -SeedCount=<N> -Out=<결과 폴더>
  -nullrhi -unattended -FixedSeed -onethread -abslog=<로그 파일>
  (골든 생성은 -WriteGolden 추가. 04 §1·§7.3 과 같은 인자)

VALIDATE <Id>:  위와 같되 -run=TDMonsterAIValidate -only=<Id> [-print-resolved] (02 §5.7)
```

시나리오 폴더: 결정론 게이트에 쓰는 시나리오는 `Content/CombatSim/Gate/`(04 §7.3), 일반 밸런스 시나리오는 `Content/CombatSim/Scenarios/` 에 둔다.

상태 값: `todo | doing | blocked | done | decision`. 우선순위: 높음 | 중간 | 낮음. 기록 칸은 날짜 + 한 줄 메모.

### 3.2 Phase 0 — 기반

### M0-01 기존 테스트 28개 기준선 확보
- 상태: todo
- 우선순위: 높음
- 선행: 없음
- 목표: GAS 전환 후 실행 결과가 기록되지 않은 28개 자동화 테스트를 실제로 돌려 통과·실패 목록을 확정한다.
- 완료 조건: `TDGame.Combat` 필터 28개 전부 통과. 실패가 있으면 원인과 수정 내역을 기록.
- 산출물: 로그 `Saved/Logs/M0-01-baseline.log`, 기록 칸의 통과 수
- 검증: BUILD 후 TEST `TDGame.Combat` `M0-01-baseline`; 로그의 "Test Completed" 28건 확인
- 참조: 결정 D28; project-current-combat-code 결론 5; engine-behaviortree-tick 결론 12(픽스처가 `World->Tick` 을 쓰므로 EQS·퍼셉션도 함께 틱)
- 기록: (없음)

### M0-02 난수 스트림 주입과 전역 난수 2곳 교체
- 상태: todo
- 우선순위: 높음
- 선행: M0-01
- 목표: `FTDDamageContext` 에 스트림을 싣고 치명타·산포 호출 2곳을 교체하며 피해 공식을 `TDDamageFormula::Compute` 로 분리한다.
- 완료 조건: 변경 목록 #1·#2·#3·#4 적용. `TDGame.Combat.ScalingMitigationAndCriticalBoundaries` 포함 28개 통과. 공식 분리 전후 피해 값 동일.
- 산출물: `Combat/TDDamageTypes.h`, `Combat/TDCombatComponent.cpp`, `Combat/TDDamageSubsystem.cpp`, `Combat/TDDamageFormula.h/.cpp`
- 검증: BUILD; TEST `TDGame.Combat` `M0-02`; `grep -n "FMath::FRand" Source/TDGame/Combat` 결과 0건
- 참조: 변경 목록 #1~#4; project-current-combat-code 결론 1; engine-gas-determinism 결론 7·바꿔야 하는 것 1
- 기록: (없음)

### M0-03 SimulationId 도입과 순서 정렬 3곳
- 상태: todo
- 우선순위: 높음
- 선행: M0-02
- 목표: 스폰 순번 `SimulationId` 를 `UTDCombatComponent` 에 두고 `GatherTargets` 정렬·`TDDamageEntity` 동률 판정 2곳을 교체한다.
- 완료 조건: 변경 목록 #5~#8 적용. `TDGame.Combat.TeamPolicyAndSpatialFiltering`·`Homing.EqualDeadlineActionsPreserveAuthoredOrder` 포함 28개 통과. `GetUniqueID` 가 `Combat/` 판정 코드에서 0건.
- 산출물: `Combat/TDCombatComponent.h`, `Combat/TDDamageSubsystem.cpp`, `Combat/TDDamageEntity.cpp`
- 검증: BUILD; TEST `TDGame.Combat` `M0-03`; `grep -n "GetUniqueID" Source/TDGame/Combat --include=*.cpp` 0건
- 참조: 변경 목록 #5~#8; project-current-combat-code 결론 4; engine-determinism-headless 결론 6·시사점 5; 결정 D30
- 기록: (없음)

### M0-04 헤드리스 프리젠테이션 게이트와 디버그 기본값
- 상태: todo
- 우선순위: 중간
- 선행: M0-01
- 목표: 데미지 엔티티의 메시·Niagara 컴포넌트 생성을 헤드리스에서 생략하고 예제 정의의 `bDrawDebug` 기본을 false 로 바꾼다.
- 완료 조건: 변경 목록 #9·#10 적용. 헤드리스 테스트 로그에 Niagara 초기화 경고 0건. PIE 에서 `bDebug=true` 로 생성하면 기존처럼 그려짐(수동 확인).
- 산출물: `Combat/TDDamageEntity.cpp`, `Combat/TDDamageExamples.cpp`, `Combat/TDDamageExamplesCommandlet.cpp`(인자 전달)
- 검증: BUILD; TEST `TDGame.Combat` `M0-04`; 로그에서 `DrawShape`·Niagara 관련 경고 grep 0건
- 참조: 변경 목록 #9·#10; project-current-combat-code 결론 8·10
- 기록: (없음)

### M0-05 픽스처 공용 헤더 승격
- 상태: todo
- 우선순위: 높음
- 선행: M0-03
- 목표: `FTDScopedCombatWorld` 를 `Source/TDGame/CombatSim/TDScopedCombatWorld.h` 로 옮기고 시드·스텝·해시 옵션을 더하되 기본 스텝 0.02 를 유지한다.
- 완료 조건: `TDDamageSystemTests.cpp` 가 새 헤더를 include 하고 그 파일의 19개 테스트가 무변경으로 통과(Homing 8·Melee 1 은 별도 픽스처, M2-10). `FTDHomingTestWorld`·`FTDScopedMeleeWorld` 는 이번에 손대지 않음(M2-10 에서 결정). 월드 초기화값 기본 `CreateNavigation(false)`·`CreateAISystem(false)`·`ShouldSimulatePhysics(false)`, `MaxUndilatedFrameTime`(기본 0.4초)·`MinUndilatedFrameTime` 을 스텝값으로 고정하는 옵션 존재.
- 산출물: `CombatSim/TDScopedCombatWorld.h/.cpp`
- 검증: BUILD; TEST `TDGame.Combat` `M0-05`; 헤더에 `Step` 기본 인자 0.02f 확인
- 참조: 변경 목록 #11; project-current-combat-code 결론 3·5; engine-determinism-headless 결론 1·9; 결정 D27·D28
- 기록: (없음)

### M0-06 몬스터 캐릭터의 컨트롤러 자동 소유 제거와 ServerOnly 옵션
- 상태: todo
- 우선순위: 중간
- 선행: M0-01
- 목표: `ATDMonsterCharacter` 의 `AutoPossessAI`/`AIControllerClass` 를 제거하고 몬스터 어빌리티에 `ServerOnly` 정책 옵션을 둔다. 컨트롤러 없는 폰은 LocalOnly 도 권한자로 활성되므로(변경 목록 #13) ServerOnly 는 컨트롤러가 붙는 경우의 보험이다.
- 완료 조건: 변경 목록 #12·#13 적용. `TDGame.Combat.GAS.*` 5개 통과. PIE 에서 몬스터 스폰 시 `AAIController` 액터가 생기지 않음(`GetController() == nullptr` 로그).
- 산출물: `Combat/Characters/TDMonsterCharacter.cpp`, `Combat/GAS/TDDamageGameplayAbility.h/.cpp`
- 검증: BUILD; TEST `TDGame.Combat.GAS` `M0-06`
- 참조: 변경 목록 #12·#13; engine-behaviortree-tick 결론 11; engine-gas-determinism 바꿔야 하는 것 4; 결정 D16
- 기록: (없음)

### M0-07 NoGlobalRandom 자동화 테스트
- 상태: todo
- 우선순위: 높음
- 선행: M0-02
- 목표: `Source/TDGame/{Combat,MonsterAI,CombatSim}` 소스를 읽어 `FMath::FRand|Rand|RandRange|SRand` 사용을 실패로 보고하는 테스트를 만든다.
- 완료 조건: `TDGame.MonsterAI.NoGlobalRandom` 통과. 의도적으로 `FMath::FRand()` 한 줄을 넣으면 파일:줄과 함께 실패(확인 후 되돌림).
- 산출물: `MonsterAI/Tests/TDNoGlobalRandomTests.cpp`
- 검증: BUILD; TEST `TDGame.MonsterAI.NoGlobalRandom` `M0-07`
- 참조: 결정 D29; engine-determinism-headless 결론 4
- 기록: (없음)

### M0-08 증분 빌드 시간 실측
- 상태: todo
- 우선순위: 중간
- 선행: M0-05
- 목표: "종 하나 추가" 규모의 C++ 변경(파일 1개 추가 + Build.cs 무변경)에 대한 증분 빌드 시간을 3회 측정해 기록한다.
- 완료 조건: 3회 측정값(초)과 PC 사양이 이 문서 기록 칸과 [02 아키텍처](02-architecture-and-definition-format.md)의 되돌림 절에 기록. 60초 미만이면 MD-03 결정 항목에 "C++ 빌더 진입점 병행 유지" 권장을 적음.
- 산출물: 기록 칸의 수치, `Docs/MonsterAI_CombatSim/measurements/incremental-build.md`
- 검증: BUILD 를 `Measure-Command { ... }` 로 감싸 3회 실행
- 참조: 결정 D10
- 기록: (없음)

### 3.3 Phase 1 — 두뇌·시뮬 최소판

### M1-01 정의 USTRUCT 와 JSON 로더
- 상태: todo
- 우선순위: 높음
- 선행: M0-05
- 목표: 평면 표 4개(actions, considerations, sequences, phases) + 메타(think_hz, lod 주기표 오버라이드, extends, tune)를 USTRUCT 로 정의하고 `FJsonObjectConverter` 로 읽는다. 로드 시 수치를 float 비트로 덤프해 정의 해시(dh)를 만든다.
- 완료 조건: `Content/MonsterAI/Definitions/Goblin_Melee.json` 로드 성공. 문법 오류는 파일:줄, 미지 키·형 불일치는 JSON 경로(예 `actions[2].considerations[0].curve`)와 허용값을 함께 보고하고 실패(`FJsonObjectConverter` 의 `bStrictMode` 는 속성 검사만 하고 파싱된 `FJsonObject` 에는 줄 번호가 없다 — `JsonObjectConverter.h:233-239`; 줄 번호까지 내려면 2차 토크나이저 패스가 필요하며 구현 방식은 미확인, M1-08 에서 결정). `extends` 병합 결과를 `-print-resolved` 로 덤프. 같은 파일 2회 로드의 dh 동일.
- 산출물: `MonsterAI/TDMonsterDefinition.h`, `MonsterAI/TDMonsterDefinitionLibrary.h/.cpp`(`UTDMonsterDefinitionLibrary`, UEngineSubsystem), `TDGame.Build.cs`(Json·JsonUtilities), `Content/MonsterAI/Definitions/Goblin_Melee.json`
- 검증: BUILD; TEST `TDGame.MonsterAI.Load` `M1-01`
- 참조: 결정 D6·D11·D13; engine-misc-decision-tools R13; [02 아키텍처](02-architecture-and-definition-format.md)
- 기록: (없음)

### M1-02 C++ 정적 등록표(입력·원시·FSM 상태)
- 상태: todo
- 우선순위: 높음
- 선행: M1-01
- 목표: 입력 함수·행동 원시·FSM 상태를 정적 등록표에 두고, 입력 함수는 `FTDBrainInputs` 스냅샷만 받도록 시그니처를 정적 단언으로 강제한다. 원시마다 `bSimulatable`·비용 등급.
- 완료 조건: 02 §5.5 등록표대로 입력 11개(DistanceToTarget … PotionCount, RingSlotFree)·원시 7개(MoveToward, MoveAway, MoveToBand, FaceTarget, Wait, CastAbility, PlaySequence) + 표현 전용 PlayEmote 등록(02 규칙 4). 액터/월드 포인터를 받는 입력 함수 등록은 컴파일 실패(`static_assert`). 검증기가 비용 등급 순으로 평가 순서를 정렬(레이캐스트 계열 마지막).
- 산출물: `MonsterAI/TDMonsterActionRegistry.h/.cpp`(`FTDMonsterActionRegistry`), `MonsterAI/TDBrainInputRegistry.h/.cpp`(`FTDBrainInputRegistry`), `MonsterAI/TDBrainInputs.h`
- 검증: BUILD; TEST `TDGame.MonsterAI.Registry` `M1-02`
- 참조: 결정 D7; web-llm-authorable-tooling 결론 7; web-ai-architecture-comparison 결론 5
- 기록: (없음)

### M1-03 응답 곡선 7종과 유틸리티 점수기
- 상태: todo
- 우선순위: 높음
- 선행: M1-02
- 목표: Linear/Quadratic/Logistic/Logit/Gaussian/Constant/Binary 곡선(m, k, b, c)과 순수 함수 점수기(곱 결합, 비용순 조기 종료, 최고점, 정의 순서 동률, InertiaSwitchRatio + 최소 유지 스텝)를 만든다.
- 완료 조건: 곡선별 경계값 단위 테스트. 같은 입력 2회 평가의 후보 점수 배열 비트 동일. 종 옵션 `select: weighted_random` 은 AI 스트림만 소비하고 기본은 max.
- 산출물: `MonsterAI/TDResponseCurve.h/.cpp`, `MonsterAI/TDUtilityScorer.h/.cpp`
- 검증: BUILD; TEST `TDGame.MonsterAI.Scorer` `M1-03`
- 참조: 결정 D1·D2; web-ai-architecture-comparison 결론 5·6; web-llm-authorable-tooling 결론 7
- 기록: (없음)

### M1-04 실행 FSM 5상태와 빙결 연결
- 상태: todo
- 우선순위: 높음
- 선행: M1-03
- 목표: Idle/Move/Cast/Sequence/Stagger 5상태 실행기를 만들고, 상태이상 빙결이 `UTDMonsterThinkSubsystem::SetFrozen(SimulationId, bIsFrozen)` 을 호출하도록 연결한다.
- 완료 조건: 빙결 중 사고·이동·공격 시간표가 모두 정지하고 해제 시 첫 스텝에 강제 사고. `TDGame.Combat.OverlappingFreezesPreserveExternalMovementChanges` 등 빙결 테스트 통과 유지.
- 산출물: `MonsterAI/TDMonsterActionExecutor.h/.cpp`(`FTDMonsterActionExecutor`), `Combat/TDCombatComponentStatus.cpp:434-442`(병행 호출)
- 검증: BUILD; TEST `TDGame.Combat` `M1-04`; TEST `TDGame.MonsterAI.Fsm` `M1-04b`
- 참조: 결정 D1; 2절 D38 외 변경 1건; engine-behaviortree-tick 상세 1-4·피할 것 5
- 기록: (없음)

### M1-05 UTDMonsterThinkSubsystem 슬롯 배열과 단일 틱 함수
- 상태: todo
- 우선순위: 높음
- 선행: M1-04
- 목표: 월드 서브시스템이 SoA(Structure of Arrays, 배열 구조체) 슬롯(SimulationId + 세대 번호)을 소유하고, TG_PrePhysics 고우선 틱 함수 하나가 SimulationId 오름차순으로 공간 해시 갱신 → 사고 → 이동 적분 → 공격 진행·판정 → `ExecuteRules` 를 처리한다(D15).
- 완료 조건: 틱 함수는 `World->Tick` 당 정확히 1회. `Step` 을 외부에서 부르는 공개 함수 없음. 시뮬에서는 슬롯 재사용 금지(단조 증가). 100마리 스폰 후 스텝당 사고 횟수가 `think_hz` 와 일치.
- 산출물: `MonsterAI/TDMonsterThinkSubsystem.h/.cpp`, `MonsterAI/TDMonsterBrainSlot.h`(`FTDMonsterBrainSlot`)
- 검증: BUILD; TEST `TDGame.MonsterAI.Subsystem` `M1-05`
- 참조: 결정 D14·D15·D21; engine-movement-anim-scale 결론 7; engine-determinism-headless 결론 1·3
- 기록: (없음)

### M1-06 ITDMonsterBody 와 시뮬 몸 ATDSimCombatant
- 상태: todo
- 우선순위: 높음
- 선행: M1-05
- 목표: 몸 인터페이스를 정의하고 시뮬용 수학 이동 액터(컨트롤러 없음, ASC(AbilitySystemComponent, 어빌리티 시스템 컴포넌트) 있음, `bSuppressGameplayCues=true`)를 만든다.
- 완료 조건: 픽스처 `SpawnCombatant` 방식으로 스폰. 직선 접근 + 반경 정지가 1/64 스텝에서 결정적. 어빌리티 활성이 컨트롤러 없이 성공.
- 산출물: `MonsterAI/TDMonsterBody.h`, `CombatSim/TDSimCombatant.h/.cpp`
- 검증: BUILD; TEST `TDGame.CombatSim.Body` `M1-06`
- 참조: 결정 D16·D18; engine-gas-determinism 결론 4·10·바꿔야 하는 것 3·4
- 기록: (없음)

### M1-07 정의 기반 공격 원시(CastAbility)
- 상태: todo
- 우선순위: 높음
- 선행: M1-06
- 목표: 몬스터 공격을 기존 `UTDDamageDefinition`(Area/Shockwave/Projectile, ActivationDelay=선딜, Cooldown)으로 판정하는 `CastAbility` 원시를 만든다. 고블린 공격 정의 `DA_TDGoblinSlash` 는 `TDDamageExamples::CreateExamples` 에 C++ Area 정의로 추가한다(02 §5.2, D19). 스켈레탈 메시 없이 성립한다. JSON 공격 파일은 없으며 Phase 3 시간표(`Content/MonsterAI/Timetables/`)만 파일이다.
- 완료 조건: 고블린이 사거리 안에서 `CastAbility` → 선딜 후 피해 발생. 피해 발생 스텝을 첫 실행에서 기록해 골든으로 고정한다(엔티티의 `SimulationTime` 누적(`TDDamageEntity.cpp:56`)과 GE 타이머의 엄격 초과 규칙(`TimerManager.cpp:1212`, 1/64 스텝에서 1.0초 → 65번째 스텝)이 달라 사전 단정 금지, 추정 N 또는 N+1). 화면 밖(LOD(Level of Detail, 세부 수준) 와 무관)에서도 판정 진행.
- 산출물: `MonsterAI/Primitives/TDPrimitive_CastAbility.cpp`, `MonsterAI/Primitives/TDPrimitive_MoveToward.cpp`(직선), `Combat/TDDamageExamples.cpp`(`DA_TDGoblinSlash` 추가)
- 검증: BUILD; TEST `TDGame.MonsterAI.Attack` `M1-07`
- 참조: 결정 D19·D21; project-current-combat-code 결론 6·10; engine-gas-determinism 결론 3·5
- 기록: (없음)

### M1-08 검증기 1·2단과 커맨드렛
- 상태: todo
- 우선순위: 높음
- 선행: M1-02
- 목표: (1) 엄격 스키마·미지 키 거부·등록표 참조 무결성·이름 유사도 힌트 (2) 정적 규칙(도달 불가 행동, 항상 0 고려사항, 페이즈 순환, 시퀀스 길이) 을 `-run=TDMonsterAIValidate` 와 `TDGame.MonsterAI.Validate` 로 노출한다.
- 완료 조건: 의도적 오류 파일 10종(`Content/MonsterAI/Definitions/_invalid/*.json`)을 전부 위치·허용값과 함께 거부(문법 오류는 파일:줄, 미지 키·형 불일치는 JSON 경로; 줄 번호까지 내는 2차 토크나이저 패스의 채택 여부를 여기서 결정, M1-01 참조). `bSimulatable=false` 원시 사용 경고. `-print-resolved` 동작.
- 산출물: `MonsterAI/TDMonsterAIValidator.h/.cpp`, `MonsterAI/TDMonsterAIValidateCommandlet.h/.cpp`, `MonsterAI/Tests/TDMonsterAIValidateTests.cpp`
- 검증: BUILD; TEST `TDGame.MonsterAI.Validate` `M1-08`; VALIDATE `Goblin_Melee` (`-only=Goblin_Melee -print-resolved`)
- 참조: 결정 D8; web-llm-authorable-tooling 결론 6·9; web-ml-generative-npc 결론 6
- 기록: (없음)

### M1-09 FTDCombatSimSession 고정 스텝 루프
- 상태: todo
- 우선순위: 높음
- 선행: M0-05, M1-05
- 목표: 세션 클래스 하나가 시나리오 → 새 월드 → 스텝 루프(1/64, `++GFrameCounter`, `FApp::SetDeltaTime/SetCurrentTime`, `World->Tick(LEVELTICK_All, Step)`, 상태 해시) → 결과 행 → `DestroyWorld + CollectGarbage` 를 수행한다. 종료 = 한쪽 전멸 ∨ MaxSteps ∨ N스텝 연속 피해 0(교착, N 은 시나리오 입력) 이고 단위는 초가 아니라 스텝이다(D27). 결과 행에 종료 사유 열(`end_reason`: player_dead / monsters_dead / max_steps / stalemate_no_damage)을 둔다(06 §1 11번).
- 완료 조건: 커맨드렛과 자동화 테스트가 같은 세션 클래스를 호출. `tick.AllowAsyncComponentTicks 0` 강제. 레이턴트 커맨드 미사용. 세션 중 정의 핫리로드 잠금.
- 산출물: `CombatSim/TDCombatSimSession.h/.cpp`
- 검증: BUILD; TEST `TDGame.CombatSim.Session` `M1-09`
- 참조: 결정 D25·D26·D27; engine-determinism-headless 결론 1·8·9·시사점 8·10; engine-gas-determinism 결론 2·바꿔야 하는 것 2·6; zz-completeness-critique §1 M12
- 기록: (없음)

### M1-10 커맨드렛 TDCombatSim 과 시나리오 JSON 최소판
- 상태: todo
- 우선순위: 높음
- 선행: M1-09
- 목표: 런타임 TDGame 모듈에 `UTDCombatSimCommandlet` 을 두고(`UTDDamageExamplesCommandlet` 선례) 시나리오 JSON(몬스터 종 × 마릿수, 시드 배열, MaxSteps, 맵 반경) → 결과 CSV 를 낸다.
- 완료 조건: 100시드 배치가 완주하고 CSV 에 시드·승패·생존 스텝·종료 사유·dh·빌드 해시 열 존재. 잡몹 10마리 기준 100시드가 60초 이내(추정, 실측 기록).
- 산출물: `CombatSim/TDCombatSimCommandlet.h/.cpp`, `CombatSim/TDCombatSimScenario.h`(`FTDCombatSimScenario`, 04 §2.3), `Content/CombatSim/Gate/goblin10_vs_rulebot.json`(게이트 겸용 시나리오)
- 검증: BUILD; SIM `Content/CombatSim/Gate/goblin10_vs_rulebot.json` 100; 결과 CSV 행 수 100 확인
- 참조: 결정 D13·D26·D33; engine-determinism-headless 결론 8
- 기록: (없음)

### M1-11 상태 해시 체인과 결정론 게이트 테스트
- 상태: todo
- 우선순위: 높음
- 선행: M1-10
- 목표: 스텝별 FNV-1a(Fowler–Noll–Vo 해시) 계층 해시(전투원 속성·위치·FSM 상태·활성 GE 수·엔티티·난수 스트림 현재 시드)를 만들고 `TDGame.CombatSim.Determinism` 이 같은 프로세스 2회 + 다른 프로세스 1회(커맨드렛 결과 파일) + 골든 해시를 비교한다. 스텝당 사고 횟수도 검사(이중 스텝 검출).
- 완료 조건: 세 비교 전부 일치. 다른 시드는 해시 상이. 골든 해시 갱신 절차(누가·언제·왜)와 `/fp:fast`·FMA·CPU 명령셋 주의가 [04 시뮬레이터](04-combat-simulator.md)에 기록.
- 산출물: `CombatSim/TDCombatStateHasher.h/.cpp`, `CombatSim/Tests/TDCombatSimDeterminismTests.cpp`, `Content/CombatSim/Golden/goblin10_vs_rulebot.hash`
- 검증: BUILD; SIM `Content/CombatSim/Gate/goblin10_vs_rulebot.json` 에 `-WriteGolden` 을 붙여 골든 생성(04 §7.3) 후 TEST `TDGame.CombatSim.Determinism` `M1-11`; 다른 프로세스 비교는 `python Tools/CombatSim/run_batch.py Content/CombatSim/Gate/goblin10_vs_rulebot.json --gate`(M2-05 전까지는 커맨드렛 2회 수동 실행으로 대체)
- 참조: 결정 D15·D31; web-balance-simulation-tools 결론 2·6; engine-gas-determinism 미확인 4
- 기록: (없음)

### M1-12 고블린 10마리 vs 규칙 봇 승률 산출
- 상태: todo
- 우선순위: 높음
- 선행: M1-07, M1-11
- 목표: 플레이어 대리 규칙 봇 최소판(같은 JSON 형식, 반응 지연·물약 임계)을 만들고 Phase 1 완료 시나리오의 승률을 낸다.
- 완료 조건: 200시드 승률과 표준오차가 기록 칸에 있음. 첫 공격 시각 < 3초, 행동 교체율 < 5회/초(결정 로그 최소 열로 계산).
- 산출물: `Content/MonsterAI/Definitions/Persona_Default.json`, 결과 CSV
- 검증: SIM `Content/CombatSim/Gate/goblin10_vs_rulebot.json` 200; 승률 열 평균 계산
- 참조: 결정 D12·D33; web-balance-simulation-tools 결론 9·10
- 기록: (없음)

### M1-13 PIE 300마리 실측과 예산표 갱신
- 상태: todo
- 우선순위: 중간
- 선행: M1-06
- 목표: `stat TDMonsterAI` 카테고리를 만들고 PIE 에서 시뮬 몸 300마리(임시)로 think/move/judge 비용을 실측해 [03 틱·규모](03-tick-and-scale.md)의 예산표를 "추정 → 실측"으로 갱신한다.
- 완료 조건: think/move/judge 채널의 마이크로초 단가 3회 평균과 PC 사양·조건(맵, 카메라)이 기록되고 [03 틱·규모](03-tick-and-scale.md) §6 표의 해당 행만 실측으로 갱신. 시뮬 몸에는 present·애니메이션이 없으므로 4.0ms KPI 판정은 여기서 하지 않고 M3-01 게임 몸으로 M3-03 에서 수행한다. 채널 단가가 추정의 2배를 넘으면 Phase 3 항목 우선순위를 높음으로 조정.
- 산출물: `MonsterAI/TDMonsterAIStats.h`(`STATGROUP_TDMonsterAI`, 03 §6.3), `Docs/MonsterAI_CombatSim/measurements/pie-300.md`
- 검증: 에디터 PIE 콘솔에서 `stat TDMonsterAI` 캡처. MCP(Model Context Protocol) 툴셋은 콘솔 명령을 지원하지 않으므로 PIE 콘솔에 직접 입력하고, 에디터 미실행 시 절차는 `AGENTS.md` 11절(에디터 백그라운드 실행 후 포트 8000 대기)을 따른다
- 참조: 결정 D24; web-mass-monster-performance 결론 1·2·6
- 기록: (없음)

### 3.4 Phase 2 — 밸런스 툴 완성

### M2-01 시나리오 입력 스키마와 사전 필터
- 상태: todo
- 우선순위: 높음
- 선행: M1-10
- 목표: 플레이어 조건(레벨, 장비 → GE 목록, 물약 정책, 버프 GE, 페르소나), 몬스터 조합(종 × 마릿수 × 배치 규칙, phase_override), 시나리오(맵 반경, MaxSteps, 시드 배열, virtual_camera·actor_pool 선택)를 USTRUCT+JSON 으로 완성하고 기대 DPS(초당 피해량)/EHP(유효 피격 허용량) 비율 계산기를 붙인다.
- 완료 조건: 스키마 문서와 예시 3개(1:1, 1:10 잡몹, 보스 2페이즈 시작). 미지 키 거부. 사전 필터가 비율을 CSV 에 기록.
- 산출물: `CombatSim/TDCombatSimScenario.h/.cpp`, `Content/CombatSim/Scenarios/*.json`(일반)·`Content/CombatSim/Gate/*.json`(게이트), `Docs/MonsterAI_CombatSim/schema/scenario.schema.json`
- 검증: BUILD; SIM 예시 3개; TEST `TDGame.CombatSim.Scenario` `M2-01`
- 참조: 결정 D23·D33; engine-gas-determinism 정의 권장 1~3; engine-misc-decision-tools 권장 1
- 기록: (없음)

### M2-02 결과 스키마·지표·스냅샷 저장
- 상태: todo
- 우선순위: 높음
- 선행: M2-01
- 목표: CSV/JSONL 출력에 승패, 생존 스텝, 총 피해·받은 피해, 물약 소비, 리썰 위험, 행동 점유율, 교체율, 분위수(p10/p50/p90), 승률 ± 표준오차, 종료 사유를 넣고 결과 헤더에 `schema_version`·엔진 버전·CPU 명령셋 클래스·빌드 해시를 찍으며(04 §7.3 골든 파일과 같은 지문, 06 §1 12번), 결과 옆에 정의 스냅샷(JSON + 해석된 등록표)을 저장한다.
- 완료 조건: n=400 배치의 표준오차 ±2.5%p 계산 일치. 스냅샷만으로 재실행 시 해시 동일. 결과 헤더 지문이 골든 파일 지문과 같은 열 이름.
- 산출물: `CombatSim/TDCombatSimResult.h/.cpp`, 결과 폴더 규약(MD-05 결정 전까지 `Saved/CombatSim/<날짜>/`)
- 검증: SIM 후 CSV 열 확인; TEST `TDGame.CombatSim.Result` `M2-02`
- 참조: 결정 D33; web-balance-simulation-tools 결론 8·10; zz-completeness-critique §1 M16·M18
- 기록: (없음)

### M2-03 결정 로그 JSONL
- 상태: todo
- 우선순위: 높음
- 선행: M1-11
- 목표: 한 사고 한 줄(스텝, SimulationId, 후보별 총점과 고려사항별 점수, 선택, 이유, draws, 상태 해시) + 이벤트 줄을 버퍼드 쓰기로 기록한다. 기본은 이벤트만, 결정 줄은 필터 옵션.
- 완료 조건: `-log-decisions=<SimulationId|all>` 옵션. 100시드 배치에서 로그 I/O 가 총 시간의 10% 미만(실측).
- 산출물: `CombatSim/TDDecisionLogWriter.h/.cpp`(`FTDDecisionLogWriter`)
- 검증: SIM `... -log-decisions=all`; JSONL 줄 수 = 사고 횟수 + 이벤트 수
- 참조: 결정 D34; web-llm-authorable-tooling 권장 3
- 기록: (없음)

### M2-04 분석 스크립트 4개
- 상태: todo
- 우선순위: 높음
- 선행: M2-02, M2-03
- 목표: `analyze_decisions.py`(행동 점유·0 원인), `diff_runs.py`(최초 이탈 스텝), `summarize_batch.py`(30줄 요약), `propose_tweaks.py`(규칙 힌트)를 만든다.
- 완료 조건: 각 스크립트가 예시 결과에서 동작. `diff_runs.py` 가 일부러 시드를 바꾼 두 로그의 최초 이탈 스텝을 정확히 보고. 요약이 30줄 이내.
- 산출물: `Tools/CombatSim/*.py`(위치는 MD-05 에서 확정)
- 검증: `python Tools/CombatSim/summarize_batch.py <결과폴더>` 출력 줄 수 ≤ 30
- 참조: 결정 D34; web-llm-authorable-tooling 결론 9
- 기록: (없음)

### M2-05 프로세스 팬아웃 런처
- 상태: todo
- 우선순위: 중간
- 선행: M1-10
- 목표: 파이썬 런처가 커맨드렛 N개를 띄우고(시드 = 기본 시드 + 시나리오 인덱스) 결과를 병합한다. 프로세스마다 `-UserDir=<경로>`(`Paths.cpp:1948`, Saved 전체 분리) 또는 `-saveddirsuffix=<접미사>`(`Paths.cpp:114`) 와 `-abslog=<파일>`(`GenericPlatformOutputDevices.cpp:87`) 을 준다. `-Saved=` 인자는 엔진에 없다. DDC(Derived Data Cache, 파생 데이터 캐시) 잠금 회피 여부는 미확인이다.
- 완료 조건: 1,000시드 × 8프로세스 완주 시간 기록. 병합 결과 해시가 단일 프로세스 순차 실행과 동일. `-UserDir=`/`-saveddirsuffix=` 로 Saved·DDC 잠금 충돌이 사라지는지 실측 기록(충돌 시 재현 절차 기록). `--gate` 옵션이 같은 시드를 워커 2개에 주어 `hash_chain.txt` 를 비교(04 §12).
- 산출물: `Tools/CombatSim/run_batch.py`(04 §12)
- 검증: `python Tools/CombatSim/run_batch.py Content/CombatSim/Gate/goblin10_vs_rulebot.json --count 1000 --procs 8 --out Saved/CombatSim/M2-05`
- 참조: 결정 D35; engine-determinism-headless 미확인 6; web-balance-simulation-tools 결론 5
- 기록: (없음)

### M2-06 검증기 3단(헤드리스 생존 시뮬)
- 상태: todo
- 우선순위: 높음
- 선행: M1-08, M1-09
- 목표: 정의 하나를 5초(320스텝) 돌려 첫 공격 < 3초, 교체율 < 5회/초, 대기 점유 < 60%, 2회 실행 해시 일치, "2초 내 사고 0회" 감시를 검사한다.
- 완료 조건: `TDGame.MonsterAI.Validate` 가 3단까지 수행. 잠든 정의(think_hz 0)를 넣으면 "사고 0회"로 거부.
- 산출물: `MonsterAI/TDMonsterAIValidator.cpp`(3단), `Content/MonsterAI/Definitions/_invalid/sleeping.json`
- 검증: BUILD; TEST `TDGame.MonsterAI.Validate` `M2-06`; VALIDATE 각 정의
- 참조: 결정 D8(자체 규칙). 참고: web-ue-5-6-to-5-8-ai-changes 결론 10 은 StateTree 5.6 "On Tick 트랜지션 미발화" 회귀 사례이며, 우리 검증기가 같은 종류의 "잠드는" 오류를 정의 층에서 잡는다는 비유일 뿐 근거는 아니다
- 기록: (없음)

### M2-07 스키마 덤프 커맨드렛과 LLM 루프 시연
- 상태: todo
- 우선순위: 높음
- 선행: M2-04, M2-06
- 목표: `UTDMonsterAISchemaDumpCommandlet` 이 USTRUCT 리플렉션과 등록표에서 `monster-definition.schema.json`, `inputs.md`, `actions.md` 를 재생성한다. `AGENTS.md` 에 "몬스터 AI 제작 8줄"(파일 위치, 스키마 경로, 검증·시뮬 명령, 로그 경로, 완료 조건)을 추가한다. LLM 이 새 잡몹 1종을 컴파일 0회로 만드는 시연 1회.
- 완료 조건: 커맨드렛 재실행 결과가 저장본과 diff 0. 시연 종이 검증 3단·200시드 배치·30줄 요약까지 통과. 프롬프트 토큰 수(약 3.5~5천, 추정) 기록.
- 산출물: `MonsterAI/TDMonsterAISchemaDumpCommandlet.h/.cpp`, `Docs/MonsterAI_CombatSim/schema/{monster-definition.schema.json,inputs.md,actions.md}`, `AGENTS.md` 8줄
- 검증: `UnrealEditor-Cmd.exe ... -run=TDMonsterAISchemaDump`; `git diff --stat Docs/MonsterAI_CombatSim/schema` 0; TEST `TDGame.MonsterAI.SchemaUpToDate` `M2-07`(02 §5.8)
- 참조: 결정 D9·D37; web-llm-authorable-tooling 결론 9·권장 5
- 기록: (없음)

### M2-08 플레이어 대리 페르소나 3종
- 상태: todo
- 우선순위: 중간
- 선행: M1-12, M2-01
- 목표: 반응 지연·물약 임계·회피 확률이 다른 페르소나 3종(신중/보통/공격적)을 JSON 으로 작성하고 PlayerProxy 스트림만 소비하게 한다.
- 완료 조건: 같은 시나리오에서 3종 승률이 단조(신중 ≥ 보통 ≥ 공격적, 추정)로 나오거나 아니면 원인 기록. 검증기 통과.
- 산출물: `Content/MonsterAI/Definitions/Persona_{Cautious,Default,Aggressive}.json`(02 §6, `kind: "player_proxy"`; 폴더는 02 미결 1 → MD-05)
- 검증: VALIDATE 3개; SIM 3회
- 참조: 결정 D12·D29; web-balance-simulation-tools 결론 9
- 기록: (없음)

### M2-09 원거리·보스 정의(시퀀스·페이즈)
- 상태: todo
- 우선순위: 중간
- 선행: M2-06
- 목표: `Archer.json`(거리 유지·사선), `Ogre_Boss.json`(페이즈 표·콤보 시퀀스)을 작성하고 `phase_override` 로 2페이즈부터 시작하는 시나리오를 검증한다.
- 완료 조건: 두 정의가 검증 3단 통과. 보스 페이즈 진입이 이벤트 로그에 기록. 2페이즈 시작 시나리오 결정론 게이트 통과.
- 산출물: `Content/MonsterAI/Definitions/{Archer,Ogre_Boss}.json`, `Content/CombatSim/Scenarios/ogre_phase2.json`
- 검증: VALIDATE 2개; SIM `ogre_phase2.json`; TEST `TDGame.CombatSim.Determinism` `M2-09`
- 참조: 결정 D2·D33; A §3.4·§3.5
- 기록: (없음)

### M2-10 기존 테스트의 1/64 스텝 이행
- 상태: blocked
- 우선순위: 낮음
- 선행: M0-05, M1-11(골든 안정 후)
- 목표: 28개 테스트 기대값을 "초"가 아니라 "스텝 수"로 재정의하고 `FTDHomingTestWorld`·`FTDScopedMeleeWorld` 를 공용 헤더로 통합한다(`TDMeleeAttackNotifyTests.cpp:171,180` 의 0.5초 요청은 실제 0.4초 진행).
- 완료 조건: 이행 시점은 D28 로 확정(Phase 2 별도 작업, MD-02 는 재확인만). 이행 후 28개 통과, 골든 해시 재생성.
- 산출물: `Combat/Tests/*.cpp`
- 검증: BUILD; TEST `TDGame.Combat` `M2-10`
- 참조: 결정 D28; project-current-combat-code 결론 3; engine-gas-determinism 결론 3·바꿔야 하는 것 2
- 기록: (없음)

### M2-11 게임·에디터 핫리로드(시뮬 세션 잠금)
- 상태: todo
- 우선순위: 낮음
- 선행: M1-01
- 목표: 콘솔 명령 `TD.MonsterAI.Reload` 로 정의를 다시 읽고 dh 를 갱신한다. 시뮬 세션 중에는 잠근다. 파일 감시는 `TDGameEditor` 모듈이 생긴 뒤로 미룬다.
- 완료 조건: PIE 에서 JSON 저장 → 명령 → 행동 변화. 세션 중 명령은 경고 후 무시.
- 산출물: `MonsterAI/TDMonsterDefinitionLibrary.cpp`(리로드, M1-01 과 같은 파일), 콘솔 명령
- 검증: PIE 수동 절차 + 로그
- 참조: 결정 D11·D13
- 기록: (없음)

### M2-15 난이도 축 시나리오 입력(스탯 배율·행동 풀)
- 상태: todo
- 우선순위: 중간
- 선행: M2-01, M2-02
- 목표: 난이도 등급·플레이어 레벨·몬스터 등급 배율(스탯 배율, 행동 풀 선택, AI 파라미터 오버라이드)을 시나리오 JSON 의 축으로 넣어 "어느 난이도에서 균형인가" 에 답할 수 있게 한다(06 §5-1). ID 는 06 의 제안 번호를 그대로 쓴다.
- 완료 조건: 같은 시나리오를 난이도 3단으로 돌린 승률 표가 요약에 들어감. 배율은 `TDDamageFormula::Compute` 입력으로만 들어가고 판정 코드는 무변경. 결정론 게이트 통과.
- 산출물: `CombatSim/TDCombatSimScenario.h`(난이도 필드), `Content/CombatSim/Scenarios/goblin10_difficulty_{easy,normal,hard}.json`
- 검증: SIM 3회; TEST `TDGame.CombatSim.Determinism`
- 참조: zz-completeness-critique §1 M5; [06 요구 밖 고려사항](06-beyond-the-ask.md) §5-1·미결 5(도입 시점은 06 미결 5 의 답에 따라 M2-02 이후로 미룰 수 있음)
- 기록: (없음)

### 3.5 Phase 3 — 게임 몸·규모·시간표

### M3-01 게임 몸 2종(APawn 잡몹, CMC(CharacterMovementComponent, 캐릭터 이동 컴포넌트) 정예)
- 상태: todo
- 우선순위: 높음
- 선행: M1-06, M1-13
- 목표: 잡몹 = `APawn` + `UFloatingPawnMovement`(캡슐 QueryOnly, 오버랩 이벤트 끔, 액터 틱 없음), 정예/보스 = `ATDMonsterCharacter`(CMC NavWalking, `bAlwaysCheckFloor=false`, `bEnablePhysicsInteraction=false`, RVO(Reciprocal Velocity Obstacles, 상호 속도 장애물 회피) 끔)로 `ITDMonsterBody` 를 구현한다. 정예 경로 추종 컴포넌트의 소유 주체(Pawn 소유 `UPathFollowingComponent` 또는 서브시스템의 `FindPathSync` 직접 호출)는 미확인이며 여기서 결정한다(변경 목록 #12).
- 완료 조건: PIE 에서 잡몹이 플레이어를 추적·공격. 잡몹 액터에 틱 함수 등록 0개(`DumpTicks` 로 확인). 정예가 내비메시 위를 NavWalking 으로 이동(경로는 M3-04 플로우 필드 전까지 직선 접근 + 분리 조향, D17; DetourCrowd 허용 여부는 MD-10).
- 산출물: `MonsterAI/TDMonsterPawn.h/.cpp`, `Combat/Characters/TDMonsterCharacter.cpp`(옵션 축소)
- 검증: BUILD; PIE 수동 + `DumpTicks` 로그; TEST `TDGame.Combat` 유지
- 참조: 결정 D16; engine-movement-anim-scale 결론 1·2·3·4; web-mass-monster-performance 결론 2
- 기록: (없음)

### M3-02 공간 해시와 분리 조향·근접 자리 토큰
- 상태: todo
- 우선순위: 높음
- 선행: M1-05
- 목표: `THierarchicalHashGrid2D`(셀 250cm, 이웃 ≤ 8)로 근접 탐색을 바꾸고 직선 접근 + 분리 조향 + 근접 자리 토큰(`ring_slot`)과 그 입력 함수 `RingSlotFree` 를 넣는다.
- 완료 조건: 300마리에서 탐색 비용이 O(N²) 이 아님(실측 표). 이웃 목록이 `(거리², SimulationId)` 정렬로 결정적. 시뮬 해시 게이트 유지.
- 산출물: `MonsterAI/TDNeighborGrid.h/.cpp`(클래스 `FTDNeighborGrid`, 02 §2), 입력 함수 `RingSlotFree`(02 §8, 06 §5-5)
- 검증: BUILD; TEST `TDGame.MonsterAI.Grid` `M3-02`; TEST `TDGame.CombatSim.Determinism`
- 참조: 결정 D17; engine-movement-anim-scale 결론 11; engine-behaviortree-tick 쓸 수 있는 것 4(EQS 대신 자체 격자 질의)
- 기록: (없음)

### M3-03 LOD 4단 주기표 스케줄러
- 상태: todo
- 우선순위: 높음
- 선행: M3-01
- 목표: `uint8 PeriodTable[4][4]`(think/move/judge/present × L0~L3)를 데이터로 두고 `Phase[Slot] = Slot % Period` 위상 분산, 프레임당 스텝 상한 4 누적기, 히스테리시스 10%, 승격 첫 스텝 강제 사고를 구현한다. 이동·판정 스텝은 LOD 로 바꾸지 않는다.
- 완료 조건: 화면 밖(L2) 몬스터 사고가 2Hz, 원거리(L3) 1Hz 로 줄고 판정은 주기표대로 진행. 시뮬 기본은 전원 L0. `virtual_camera` 시나리오에서 LOD 분포가 결과 행에 기록되고 해시 게이트 통과.
- 산출물: `MonsterAI/TDMonsterTickScheduler.h/.cpp`(02 §2 클래스 표 수록), `Content/MonsterAI/PeriodTable.json`, `Content/CombatSim/Scenarios/perf_300_camera.json`
- 검증: BUILD; TEST `TDGame.MonsterAI.Scheduler` `M3-03`; SIM `Content/CombatSim/Scenarios/perf_300_camera.json` 10; PIE 300마리 게임 몸(M3-01)으로 몬스터 몫 합 ≤ 4.0ms KPI 판정(1절 Phase 3 (a))
- 참조: 결정 D21·D22·D23; engine-mass-entity-ai 결론 6(랜덤 분산 대신 고정 위상); engine-movement-anim-scale 결론 6
- 기록: (없음)

### M3-04 방 단위 플로우 필드(MoveToward 구현체)
- 상태: todo
- 우선순위: 중간
- 선행: M3-02
- 목표: 목표당 1장, 2Hz, 4프레임 결정적 분할 재계산 플로우 필드를 `MoveToward` 원시의 게임 구현체로 붙인다. 시뮬은 `bSimulatable` 직선 구현 유지 또는 같은 필드 사용(시나리오 옵션).
- 완료 조건: 300마리 이동 비용이 유닛 수와 무관(실측 표). 재계산 프레임 분할이 시드·순서에 독립(해시 게이트 통과).
- 산출물: `MonsterAI/TDFlowField.h/.cpp`(02 §2 클래스 표 수록), `MonsterAI/Primitives/TDPrimitive_MoveToward.cpp`(게임 분기)
- 검증: BUILD; TEST `TDGame.MonsterAI.FlowField` `M3-04`; PIE 실측
- 참조: 결정 D17; web-mass-monster-performance 결론 8; B §4.4
- 기록: (없음)

### M3-05 애니메이션 예산 할당기와 표현 주기
- 상태: todo
- 우선순위: 중간
- 선행: M3-01
- 목표: `AnimationBudgetAllocator` 플러그인(정식) 활성, `a.Budget.BudgetMs 1.5`, `USkeletalMeshComponentBudgeted`, `VisibilityBasedAnimTickOption` 을 몸 2종에 적용한다. 헤드리스에는 `bEnableAnimation=false`.
- 완료 조건: PIE 300마리 애니메이션 비용이 1.5ms 예산 내(실측). 헤드리스 시뮬에서 애니메이션 틱 0.
- 산출물: `TDGame.uproject`(플러그인), `MonsterAI/TDMonsterPawn.cpp`
- 검증: PIE `stat Anim`·`stat TDMonsterAI` 캡처
- 참조: 결정 D24; engine-movement-anim-scale 결론 8·9; web-mass-monster-performance 결론 6
- 기록: (없음)

### M3-06 공격 시간표 추출과 B단계 정합 테스트
- 상태: todo
- 우선순위: 높음
- 선행: M3-01, M2-06
- 목표: 몽타주 주도 근접 몬스터 1종에 대해 커맨드렛이 `FTDAttackTimetable`(선딜·히트 창·형상 파라미터·소켓 궤적 바운딩·몽타주 source_hash)을 추출하고, 몬스터의 `bAuthoritativeHitJudgment=false` 로 노티파이를 표현·오라클 전용으로 강등한다. 같은 헤드리스 월드에서 실제 메시+몽타주 재생 vs 시간표 판정을 대조한다.
- 완료 조건: 적중 시각 ±1 스텝·총 피해 동일(`TDGame.CombatSim.Parity.<Id>`, 04 §8). 몽타주 변경 시 source_hash 불일치를 검증기가 거부. 플레이어 경로는 무변경.
- 산출물: `MonsterAI/TDAttackTimetable.h`, `MonsterAI/TDAttackTimetableExtractCommandlet.h/.cpp`, `Content/MonsterAI/Timetables/<Id>.json`(02 §1, 04 §6.2)
- 검증: BUILD; `UnrealEditor-Cmd.exe ... -run=TDAttackTimetableExtract -Montage=<경로>`(또는 `-All`); TEST `TDGame.CombatSim.Parity` `M3-06`
- 참조: 결정 D19·D20·D32; project-current-combat-code 결론 6; engine-determinism-headless 시사점 7; engine-movement-anim-scale 결론 8
- 기록: (없음)

### M3-07 JSON→C++ 상수표 되돌림 커맨드렛
- 상태: todo
- 우선순위: 중간
- 선행: M2-07
- 목표: 정의 JSON 을 C++ 상수표(`Source/TDGame/MonsterAI/Generated/TDMonsterDefinitions.gen.cpp`)로 생성하는 커맨드렛 `UTDMonsterAIBakeConstantsCommandlet` 을 만들어, 검증기가 못 잡는 런타임 오류 반복 또는 300마리 기준 곡선 해석 비용 > 1ms 일 때 형식은 유지한 채 컴파일 산출물로 전환할 수 있게 한다.
- 완료 조건: 구운 상수표의 dh 가 JSON 로더의 dh 와 같고(`TDGame.MonsterAI.BakeMatchesJson`, 02 §5.12), 생성물로 빌드한 실행이 JSON 로드 실행과 같은 상태 해시. 곡선 해석 비용 실측값 기록.
- 산출물: `MonsterAI/TDMonsterAIBakeConstantsCommandlet.h/.cpp`, `MonsterAI/Generated/TDMonsterDefinitions.gen.cpp`
- 검증: BUILD; `UnrealEditor-Cmd.exe ... -run=TDMonsterAIBakeConstants`; TEST `TDGame.MonsterAI.BakeMatchesJson` `M3-07`; TEST `TDGame.CombatSim.Determinism`
- 참조: 결정 D10; 02 §5.12(커맨드렛 이름은 02 미결 4 → MD-03 확정 전까지 02 표기); MD-03
- 기록: (없음)

### M3-08 무액터 L3 강등과 체력 미러 규칙
- 상태: blocked
- 우선순위: 낮음
- 선행: M3-03, M1-13(실측 KPI 초과 시에만 해제)
- 목표: 300마리 몬스터 몫 합이 4.0ms 를 넘을 때만 무액터 L3 슬롯·액터 풀(400)을 도입한다. SoA 체력은 미러 전용, 승격 시 ASC 값이 정본, 강등 금지(활성 GE·손상 체력·보스/엘리트).
- 완료 조건: 승격/강등 왕복 시 시뮬 해시 불변. PIE 1,000마리(L0 40 / L1 80 / L2 280 / L3 600, 03 §1.3 추정 분포) 합 ≤ 6.0ms(실측). 강등 금지 규칙 테스트.
- 산출물: `MonsterAI/TDMonsterActorPool.h/.cpp`(02 §2 클래스 표 수록)
- 검증: BUILD; TEST `TDGame.MonsterAI.Pool` `M3-08`; PIE 실측
- 참조: 결정 D18·D24; engine-gas-determinism 결론 4·9; B §8-4·§8-8
- 기록: (없음)

### M3-09 C단계 통계 등가 리포트
- 상태: todo
- 우선순위: 낮음
- 선행: M3-01, M2-08
- 목표: 내부 플레이테스트(사람) 승률과 시뮬 승률의 y=x 산포 리포트를 1회 작성한다.
- 완료 조건: 시나리오 ≥ 5개, 사람 플레이 ≥ 10회/시나리오, 산포도와 편차 원인 메모.
- 산출물: `Docs/MonsterAI_CombatSim/measurements/parity-c-<날짜>.md`
- 검증: `python Tools/CombatSim/summarize_batch.py` + 수기 표
- 참조: 결정 D32; web-balance-simulation-tools 결론 8·9
- 기록: (없음)

### M3-10 Docs/Tasks 통합 여부 정리
- 상태: decision
- 우선순위: 낮음
- 선행: M2-07
- 목표: 이 대장(M0~M4)을 `Docs/Tasks/` 에 Phase 파일로 옮길지 결정한다(MD-08). 옮긴다면 ID 는 유지하고 README 파일 표에 한 줄 추가.
- 완료 조건: 사용자 결정 기록.
- 산출물: (결정에 따름)
- 검증: 없음
- 참조: [Docs/Tasks/README.md](../Tasks/README.md)
- 기록: (없음)

### M3-13 CC·넉백 순수 함수 규약
- 상태: todo
- 우선순위: 중간
- 선행: M3-02, M3-03
- 목표: 군중제어(CC, Crowd Control)를 "AI 입력 차단 + 이동 오버라이드 + 타이머 유지" 규약으로 명문화하고, 넉백 궤적을 순수 함수(시작·속도·감쇠·충돌 반경)로 정의해 게임과 시뮬이 같은 코드를 쓴다(06 §5-4). 빙결(M1-04 `SetFrozen`)은 이 규약의 한 사례로 편입한다. ID 는 06 의 제안 번호다.
- 완료 조건: 넉백 중 공간 해시·자리 토큰·이동 적분이 같은 스텝 순서로 갱신되고 결정론 게이트 통과. 넉백 시작·종료 스텝이 골든에 고정. 정의 JSON 의 CC 저항 필드가 검증기 1단을 통과.
- 산출물: `MonsterAI/TDMonsterCrowdControl.h/.cpp`(02 §2 클래스 표 수록), `Content/CombatSim/Gate/knockback_ring.json`
- 검증: BUILD; TEST `TDGame.MonsterAI.CrowdControl` `M3-13`; TEST `TDGame.CombatSim.Determinism`
- 참조: zz-completeness-critique §1 M8; [06 요구 밖 고려사항](06-beyond-the-ask.md) §5-4; 결정 D15·D17
- 기록: (없음)

### M3-15 세이브/로드 슬롯 직렬화 왕복 해시 테스트
- 상태: todo
- 우선순위: 중간
- 선행: M1-05, M1-11
- 목표: `FTDMonsterBrainSlot` 배열(현재 행동, 쿨다운 스텝, 시퀀스 커서, 페이즈, 관성 잔여)과 몬스터별 AI 스트림 상태를 세이브 슬롯에 직렬화하고, 저장 직후 로드한 상태가 저장 전과 같은 상태 해시를 내는지 검사한다(06 §1 13번, 비평 B14). ID 는 06 의 제안 번호다.
- 완료 조건: 시뮬 세션을 스텝 K 에서 저장 → 로드 → 계속 실행한 해시 체인이 무중단 실행과 동일. 활성 GE 는 ASC 직렬화 규칙(미확인, 여기서 조사)에 따라 복원되거나 "저장 시점에 만료 처리" 로 규약화.
- 산출물: `MonsterAI/TDMonsterBrainSlot.h`(직렬화 함수), `CombatSim/Tests/TDCombatSimSaveLoadTests.cpp`
- 검증: BUILD; TEST `TDGame.CombatSim.SaveLoadRoundTrip` `M3-15`
- 참조: zz-completeness-critique §3 B14; [06 요구 밖 고려사항](06-beyond-the-ask.md) §1 13번; 결정 D14·D31
- 기록: (없음)

### 3.6 Phase 4 — 확장(조건부)

### M4-01 자체 C++ HTN(보스·무리)
- 상태: blocked
- 우선순위: 낮음
- 선행: M2-09; "3페이즈 이상 보스 안무 또는 무리 역할 배정" 콘텐츠 요구 확정
- 목표: JSON 도메인(composite/primitive/effects/replan_hz/max_iterations), 재귀 종료 정적 검사, 복원점에 태스크 스택 포함, 실패는 실패로 반환하는 총순서 전방 분해 HTN 을 `sequences` 상위에 추가한다. 엔진 HTNPlanner 플러그인은 켜지 않는다.
- 완료 조건: 보스 1 + 잡몹 300 시나리오 결정론 게이트 통과. 동시 HTN 개체 ≤ 10. 무한 재귀 도메인을 검증기가 거부.
- 산출물: `MonsterAI/HTN/*`, `Content/MonsterAI/Domains/*.json`
- 검증: BUILD; TEST `TDGame.MonsterAI.Htn` `M4-01`; TEST `TDGame.CombatSim.Determinism`
- 참조: 결정 D3; engine-htnplanner-plugin 결론 1·2·3·6·권장 2·3; web-ai-architecture-comparison 결론 2·3·4
- 기록: (없음)

### M4-02 행동 복제(BC) 플레이어 봇
- 상태: blocked
- 우선순위: 낮음
- 선행: M2-08; 사용자 착수 결정
- 목표: `LearningAgents, LearningCore, NNERuntimeBasicCpu` 활성, 규칙 페르소나 플레이를 `ULearningAgentsRecorder` 로 녹화 → `ULearningAgentsImitationTrainer` 학습(외부 파이썬, `PipInstall` 선행) → 추론 결정론(`MakePolicy(Seed)`, `RunInference(ActionNoiseScale=0)`, MemoryStateSize 0, 에이전트 ID 유지, CPU 고정).
- 완료 조건: 같은 정책·시드 추론 2회 바이트 동일. BC 봇 승률이 규칙 봇 대비 ±10%p 안(추정 기준, 실측 기록).
- 산출물: `CombatSim/ML/*`(05 §9.2 파일 목록), `Content/CombatSim/Policies/<name>.ubnne` 3개(인코더·정책·디코더 스냅샷) + `<name>.policy.json`(05 §3.3, LFS)
- 검증: BUILD; TEST `TDGame.CombatSim.PolicyDeterminism` `M4-02`
- 참조: 결정 D36; engine-learning-agents-ml 결론 1·3·4·5; web-ml-generative-npc 결론 3·4; web-ue-5-6-to-5-8-ai-changes 결론 3
- 기록: (없음)

### M4-03 CMA/PSO(Particle Swarm Optimization, 입자 군집 최적화) 파라미터 튜닝 루프
- 상태: blocked
- 우선순위: 낮음
- 선행: M2-07; 사용자 착수 결정
- 목표: JSON `tune:true` 잎 벡터를 `LearningCMAOptimizer`/`LearningPSOOptimizer` 로 탐색하고 목적 함수는 시뮬 승률 밴드(예 45~55%)로 둔다.
- 완료 조건: 튜닝 1회로 목표 밴드 진입 사례 1건과 JSON diff 산출. 튜닝 결과 정의가 검증 3단 통과.
- 산출물: `CombatSim/ML/TDMonsterAITuneCommandlet.h/.cpp`(`UTDMonsterAITuneCommandlet`, 05 §4.3·§9.2), `Content/CombatSim/Targets/goblin.json`(목표 밴드, 05 §4.2)
- 검증: `UnrealEditor-Cmd.exe ... -run=TDMonsterAITune -def=Goblin_Melee -targets=Content/CombatSim/Targets/goblin.json -generations=30 -workers=8`; 결과 `Goblin_Melee.tuned.json` 이 VALIDATE 3단 통과
- 참조: 결정 D36; engine-learning-agents-ml 결론 8(b)(CMA/PSO 블랙박스 최적화); web-balance-simulation-tools 결론 8
- 기록: (없음)

### M4-04 ISM/VAT 후열 표현
- 상태: blocked
- 우선순위: 낮음
- 선행: M3-05; 실측 KPI(단계 B 초과) 확인
- 목표: 화면 안 원거리 후열 몬스터 1~2종을 AnimToTexture(VAT) + ISM 으로 그린다.
- 완료 조건: PIE 목표 마릿수(추정 1,500, 화면 내 200 — 어느 결정·조사에도 없는 수치로, M1-13·M3-08 실측 후 확정)에서 60fps 유지 또는 병목 보고서.
- 산출물: `MonsterAI/Representation/*`, 베이크 에셋(LFS)
- 검증: PIE 실측 캡처
- 참조: 결정 D24; engine-movement-anim-scale 결론 10; web-mass-monster-performance 결론 3·6
- 기록: (없음)

### M4-05 Mass 이관 스파이크(선택)
- 상태: blocked
- 우선순위: 낮음
- 선행: M3-08; 엔진 5.8.1 이상(현재 5.8.2, MD-04)
- 목표: 슬롯 배열을 Mass 프래그먼트 1:1 로 옮기는 스파이크를 `mass.EntityCompaction 0`, `mass.FullyParallel 0` 조건에서 수행하고 결정론·비용 보고서를 쓴다.
- 완료 조건: 보고서 1편(해시 게이트 통과 여부, 비용 비교, 채택/보류 권고).
- 산출물: `Docs/MonsterAI_CombatSim/measurements/mass-spike.md`
- 검증: SIM + TEST `TDGame.CombatSim.Determinism`
- 참조: 결정 D24; engine-mass-entity-ai 결론 5·6·14·15; web-ue-5-6-to-5-8-ai-changes 결론 2·10; web-mass-monster-performance 결론 4·9
- 기록: (없음)

---

## 4. 사용자 결정 필요 항목(MD)

형식은 [Docs/Tasks/decisions.md](../Tasks/decisions.md)와 같다. 결정 전에는 영향 항목을 건드리지 않는다.

### MD-01 Phase 3 이후 플레이어 근접 판정 권위
- 질문: 플레이어 근접 공격도 몬스터처럼 시간표 권위(`bAuthoritativeHitJudgment=false`)로 바꿀까, 노티파이 스윕을 유지할까?
- 선택지: (a) 유지(현행, `TDMeleeAttackNotifyTests` 로 검증됨) (b) 시간표 권위로 전환 (c) 시뮬 대리만 시간표, 게임은 유지(현 결정 D20)
- 권장: (c) 를 Phase 3 끝까지 유지하고, B단계 정합 테스트가 ±1 스텝을 안정적으로 만족하면 (b) 재검토.
- 영향: M3-06
- 결정: (미정)

### MD-02 기존 테스트 28개의 1/64 스텝 이행 시점 — 결정 기록 확정(D28), 재확인만
- 질문: D28 은 "1/64 이행은 Phase 2 의 별도 작업" 으로 확정했다. 이 확정을 유지하는지만 재확인한다.
- 선택지: (a) D28 유지(Phase 2, M1-11 골든 안정 후) (b) D28 을 바꿔 Phase 3 이후로 미룸 — 결정 기록 개정이 필요
- 권장: (a). 골든 해시가 두 규약을 따로 유지하는 비용이 더 크다.
- 영향: M2-10(상태 blocked, 선행 M1-11)
- 결정: (D28 확정, 재확인 대기)

### MD-03 JSON 정본 유지 vs C++ 빌더 병행 — 결정 기록 확정(D10), 재확인만
- 질문: D10 은 "증분 빌드 60초 미만이면 C++ 빌더 진입점(`FTDMonsterDefinitionBuilder`)을 병행 유지" 로 확정했다. M0-08 실측값을 D10 규칙에 대입한 결과를 재확인한다. 함께 확정할 것: 되돌림 커맨드렛 이름 `UTDMonsterAIBakeConstantsCommandlet`·빌더 이름 `FTDMonsterDefinitionBuilder`(02 미결 4, 02 제안).
- 선택지: (a) D10 규칙 적용(60초 미만 → 병행, 이상 → M3-07 되돌림만) (b) D10 을 바꿔 JSON 만 — 결정 기록 개정이 필요
- 권장: (a). 이름은 02 제안을 그대로 채택.
- 영향: M0-08, M3-07
- 결정: (D10 확정, M0-08 실측 후 재확인)

### MD-04 엔진 버전 고정과 패치 업그레이드 시 골든 재생성 강제
- 질문: 설치된 엔진은 이미 5.8.2 다(`Engine/Build/Build.version`: MajorVersion 5, MinorVersion 8, PatchVersion 2, BranchName ++UE5+Release-5.8). 5.8.0 Mass 병렬 회귀는 해소된 상태다. 5.8.x 패치 업그레이드·다운그레이드 시 골든 해시 전량 재생성을 절차로 강제할까?
- 선택지: (a) 5.8.2 고정 + 엔진 버전 변경 시 골든 전량 재생성(04 §7.3 4항) (b) 버전 자유(골든 불일치를 그때그때 처리)
- 권장: (a). 골든 파일·결과 헤더가 엔진 버전을 기록하므로 절차만 강제하면 된다(web-ue-5-6-to-5-8-ai-changes 버전 고정 권고).
- 영향: M4-05, 골든 해시 전체, M2-02 결과 헤더
- 결정: (미정)

### MD-05 시뮬 결과·스크립트·페르소나 저장 위치
- 질문: 결과(CSV/JSONL/골든)를 `Saved/CombatSim/`(비추적)에 둘까 `Docs/Validation/`(추적)에 둘까? 파이썬은 `Tools/CombatSim/`(신설)인가? 페르소나 JSON 은 `Content/MonsterAI/Definitions/`(`kind: "player_proxy"`, 02 제안)에 둘까 `Content/CombatSim/Personas/` 로 분리할까(02 미결 1)?
- 선택지: (a) `Saved/CombatSim/` + 골든만 `Content/CombatSim/Golden/` 추적, 페르소나는 `Definitions/` (b) 전부 `Docs/Validation/CombatSim/` 추적 (c) 기타
- 권장: (a). 배치 결과는 크고 재현 가능하며, 골든과 요약(30줄)만 추적하면 된다. 스크립트는 `Tools/CombatSim/`. 페르소나를 같은 폴더에 두면 로더·검증기가 하나로 유지된다.
- 영향: M2-02, M2-04, M2-05, M2-08
- 결정: (미정)

### MD-06 StateTree 보스 연출 재검토
- 질문: 보스 연출·시각 디버깅에 엔진 StateTree 를 쓰는 선택지를 Phase 4 에서 열까?
- 선택지: (a) 열지 않음 (b) 연출 전용(전투 판정 밖)으로만 스파이크
- 권장: (a) 를 기본으로, HTN(M4-01) 이 연출 요구를 못 채울 때만 (b).
- 영향: M4-01
- 결정: (미정)

### MD-07 게임 내 think 병렬화 옵트인 — 결정 기록 확정(D25), 재확인만
- 질문: D25 는 "실측 병목 + 병렬/직렬 해시 동일 테스트 통과 후 게임에서만 옵트인" 으로 확정했다. Phase 3 실측 결과가 나오면 D25 조건 충족 여부만 재확인한다.
- 선택지: (a) D25 조건 충족 시 켬(`TDGame.MonsterAI.ParallelHashEquivalence` 통과 조건, 03 §9) (b) 조건 미충족 → 단일 스레드 유지
- 권장: 실측 전 (b). 시뮬레이터는 어느 경우에도 단일 스레드.
- 영향: M3-03
- 결정: (D25 확정, Phase 3 실측 후 재확인)

### MD-08 이 대장의 Docs/Tasks 통합
- 질문: M0~M4 를 `Docs/Tasks/` Phase 파일로 옮길까?
- 선택지: (a) 이 문서에 유지 (b) 월드젠 대장과 통합
- 권장: 월드젠 Phase 0 완료 후 (b). 그전에는 두 팀의 파일 충돌을 피하려 (a).
- 영향: M3-10
- 결정: (미정)

### MD-09 CI 골든 기준 CPU 명령셋 고정
- 질문: 골든 해시를 CI(Continuous Integration, 지속 통합) 머신 1대(명령셋 예 AVX2)에 고정할까, CPU 명령셋 클래스별로 골든을 여러 벌 둘까?(04 미결 2, 05 미결 6)
- 선택지: (a) CI 머신 1대 + 명령셋 클래스 1개 고정, 다른 머신은 통계 등가만 (b) 명령셋 클래스별 골든 여러 벌 (c) 핵심 수치 고정소수화로 머신 독립(비평 C13, 비용 큼)
- 권장: (a). 골든 파일과 결과 헤더에 명령셋 클래스를 기록(M2-02)하므로 불일치 원인을 바로 구분할 수 있다.
- 영향: M1-11, M2-02, M2-05, 위험표 #11
- 결정: (미정)

### MD-10 정예·보스(≤10) 에 DetourCrowd 허용 여부
- 질문: D17 은 잡몹의 DetourCrowd 를 금지했고 정예·보스는 정하지 않았다(03 미결 3). 허용할까?
- 선택지: (a) 허용(정예 경로에 내비 시스템 의존, 시뮬 몸과 이동이 달라져 B단계 등가 테스트 대상 확대) (b) 금지(정예도 직선 접근 + 분리 조향 → M3-04 플로우 필드)
- 권장: (b) 를 기본으로 두고, 정예 이동 품질 문제가 실측될 때만 (a) 재검토.
- 영향: M3-01, M3-04
- 결정: (미정)

---

## 5. 위험표(12)

A §8·B §8·C §8 을 통합했다. "감시 지표"는 어느 산출물의 어느 수치가 경보인지 적었다.

| # | 위험 | 완화 | 감시 지표 | 근거 |
|---|---|---|---|---|
| 1 | JSON 이 런타임에야 실패(형·이름 오류가 컴파일러를 못 거침) | 검증 3단 CI(지속 통합) 게이트, 엄격 파싱, 유사 이름 힌트, 로드 실패 시 `Idle` 정의 대체 + 화면 경고 | `TDGame.MonsterAI.Validate` 실패 수; 런타임 정의 오류 로그 건수(주간) | web-llm-authorable-tooling 결론 6·9 |
| 2 | 유틸리티 떨림(행동 교체 반복) | InertiaSwitchRatio + 최소 유지 스텝, 검증 3단 교체율 상한 | 결정 로그 교체율 > 5회/초 | web-ai-architecture-comparison 결론 4(킬존 "Continue current plan")·쓸 것 2 |
| 3 | 유틸리티로 못 그리는 다단계 조정(3페이즈 보스·무리) | 시퀀스·페이즈로 흡수, 부족하면 M4-01 HTN | 페이즈 진입률·시퀀스 완주율 | engine-htnplanner-plugin 결론 2·권장 2 |
| 4 | 결정론 이탈(전역 난수·병렬 틱·컨테이너 순회) | `-FixedSeed -onethread`, `tick.AllowAsyncComponentTicks 0`, `TMap/TSet` 순회 금지 리뷰 규칙, 해시 게이트, `diff_runs.py` | `TDGame.CombatSim.Determinism` 실패; 최초 이탈 스텝 | engine-determinism-headless 결론 3·4; web-balance-simulation-tools 결론 2·6 |
| 5 | 게임(가변 델타)과 시뮬(고정 스텝)이 비트 동일하지 않음 | 3단 일치 정의(A/B/C)로 약속 범위 명시, 두뇌·이동·시간표는 누적기로 같은 코드 | B단계 ±1 스텝 초과 건수; C단계 산포 | engine-gas-determinism 결론 3; 결정 D32 |
| 6 | 시간표 판정과 소켓 궤적 스윕의 "느낌" 차이 | B단계 정합 테스트 종별 강제, 형상 파라미터는 궤적 바운딩으로 초기화, 어긋남은 형상 수정으로만 | `TDGame.CombatSim.Parity.*` 실패; source_hash 불일치 거부 건수 | engine-movement-anim-scale 결론 8; project-current-combat-code 결론 6 |
| 7 | 예산표 마이크로초 단가가 추정(실측이 2~3배일 수 있음) | Phase 1 완료 조건에 300마리 실측, 표를 실측으로 갱신, 주기표 데이터 조정 | `stat TDMonsterAI` 합 > 4.0ms@300 | web-mass-monster-performance 결론 1·10; B §8-1 |
| 8 | LOD 가 사고 주기를 바꿔 시뮬 결과가 카메라에 종속 | 시뮬 기본 전원 L0, `virtual_camera`·`actor_pool` 을 시나리오 입력으로 명시, 결과 행에 LOD 분포 | LOD 분포 열 vs 승률 상관 | 결정 D23; B §8-3 |
| 9 | 무액터 L3 몬스터에 원거리 피해가 닿지 않음 / ASC·SoA 이중 경로 이탈 | 강등 금지 규칙(활성 GE·손상 체력·보스), 승격 시 ASC 값 정본, `TDDamageFormula::Compute` 공유 | 승격/강등 왕복 해시 불변 테스트 | engine-gas-determinism 결론 4·9; B §8-4·§8-8 |
| 10 | 커맨드렛 다중 프로세스의 Saved/DDC 잠금·GC 미동작·메모리 증가 | 프로세스마다 `-UserDir=<경로>`(`Paths.cpp:1948`) 또는 `-saveddirsuffix=<접미사>`(`Paths.cpp:114`) 와 `-abslog=<파일>`(`GenericPlatformOutputDevices.cpp:87`) 분리(`-Saved=` 는 엔진에 없음; DDC 잠금 회피는 미확인, M2-05 실측), 시나리오마다 `DestroyWorld + CollectGarbage`, 프로세스당 시나리오 상한 후 재기동 | 피크 메모리 로그; 잠금 오류 건수 | engine-determinism-headless 미확인 6; engine-gas-determinism 미확인 2 |
| 11 | 부동소수점이 CPU 명령셋·컴파일 옵션에 따라 달라짐 | 결정론 정의를 "같은 빌드·같은 머신"으로 제한, CI 머신 고정, 다른 머신은 통계 등가, `/fp:fast`·FMA 주의 | 다른 머신 골든 해시 불일치 | web-balance-simulation-tools 결론 2; web-ml-generative-npc 미확인 4·추가 고려사항 1; MD-09 |
| 12 | 병행 월드젠 작업과의 충돌(모듈·`Docs/Tasks`·에디터 전용 코드) / 엔진 회귀·실험 플러그인(5.8.0 Mass, Learning Agents 0.2) | 새 모듈 없음, 폴더 2개로 한정, `Docs/Tasks` 무수정, 에디터 편의는 `TDGameEditor` 생성 후 이동; 5.8.2 고정(MD-04, 엔진 버전 열로 감시), 실험 플러그인은 인터페이스 뒤 격리·Phase 4 이후 | `git status` 로 월드젠 소유 파일 변경 0건; 엔진 버전 열 | 결정 D13; web-ue-5-6-to-5-8-ai-changes 결론 3·10 |

---

## 6. 에이전트 작업 절차

[Docs/Tasks/README.md](../Tasks/README.md)의 규칙과 동일하다. 이 대장에 맞춘 문구만 바꿨다.

1. 시작 전에 `AGENTS.md`(7절 C++ 전용 로직, 11절 도구), 이 문서, [00 결정 기록](00-decision-record.md)(D1~D38, 저장소에 반영됨(2026-09-10)), [02 아키텍처](02-architecture-and-definition-format.md) §2 클래스 표를 읽는다. 클래스·파일·명령 이름은 02 §2 클래스 표와 04·05 의 명령 인자를 그대로 쓴다. 이 문서가 쓰는 이름(`TDMonsterAIStats`, `FTDMonsterTickScheduler`, `TDFlowField`, `TDMonsterActorPool`, `TDMonsterCrowdControl`)은 02 §2 클래스 표에 수록되어 있다.
2. 상태가 `todo` 이고 선행 항목이 모두 `done` 인 것 중 우선순위가 가장 높은 항목을 고른다. 고르면 상태를 `doing` 으로 바꾸고 기록 칸에 날짜와 세션을 적는다.
3. 완료 조건을 전부 만족했을 때만 `done` 으로 바꾼다. 검증 근거(테스트 로그 발췌, 해시 값, ms 수치, 승률 ± 표준오차)를 기록 칸에 남긴다. 일부만 됐으면 `doing` 을 유지하고 남은 조건을 적는다.
4. 결정이 필요하면 `decision` 으로 바꾸고 4절에 질문·선택지·권장안을 적은 뒤 다른 항목으로 넘어간다. 사용자 결정 없이 기존 에셋 삭제·덮어쓰기·설정 변경을 하지 않는다.
5. 새 소스 파일을 추가하면 BUILD 와 관련 자동화 테스트(`TDGame.Combat`, `TDGame.MonsterAI.*`, `TDGame.CombatSim.*`)를 통과시킨 뒤에만 `done`. 에디터 검증이 필요한 항목(PIE 실측)은 캡처나 로그를 `Docs/MonsterAI_CombatSim/measurements/` 에 남긴다(`Docs/Validation/` 은 MD-05 결정 전까지 쓰지 않는다).
6. 작업 중 발견한 새 할 일은 해당 Phase 절 끝에 `todo` 로 추가하고 참조를 단다. 범위 밖 문제(월드젠 소유 파일 포함)는 고치지 말고 항목으로만 남긴다.
7. 골든 해시를 바꿔야 하면 기록 칸에 "왜 바뀌었는가"(엔진 버전, 정의 dh, 코드 변경 커밋)를 먼저 적고 갱신한다.
8. 커밋은 사용자가 지시할 때만 한다.

---

## 7. 미결 사항(사용자 결정 필요)

4절 MD-01~MD-10 이 전부다. MD-02·MD-03·MD-07 은 결정 기록(D28·D10·D25)이 이미 확정한 사항이라 재확인만 필요하다. 착수 전에 답이 필요한 것은 MD-05(저장 위치·페르소나 폴더, M2-02 부터 영향) 뿐이고, MD-09(CI 명령셋)는 M1-11 골든을 처음 만들기 전까지, 나머지는 해당 Phase 진입 시점에 물으면 된다. 06 문서의 미결 8건(텔레메트리·KPI 소유자·난이도 축 시점 등)은 06 이 정본이며 답이 나오면 이 대장에 항목으로 옮긴다.

---

## 8. 근거 색인(인용한 조사 파일 목록)

| 조사 파일 | 인용한 절(조사 파일의 실제 절 이름) |
|---|---|
| research/project-current-combat-code.md | 결론 1·2·3·4·5·6·8·10, 상세 3절, 미확인 3 |
| research/engine-determinism-headless.md | 결론 1·3·4·6·8·9·10, 프로젝트 적용 시사점 5·7·8·10, 미확인 6 |
| research/engine-gas-determinism.md | 결론 2·3·4·5·7·9·10, 바꿔야 하는 것 1·2·3·4·6, 규모별 선택 지침, 상세 6) 정의 권장 1~3, 미확인 2·4·8 |
| research/engine-behaviortree-tick.md | 결론 11·12, 상세 1-4, 쓸 수 있는 것 4, 피할 것 5 |
| research/engine-movement-anim-scale.md | 결론 1·2·3·4·6·7·8·9·10·11 |
| research/engine-mass-entity-ai.md | 결론 5·6·14·15 |
| research/engine-htnplanner-plugin.md | 결론 1·2·3·6, 권장 2·3 |
| research/engine-learning-agents-ml.md | 결론 1·3·4·5·8(b) |
| research/engine-misc-decision-tools.md | R13, 권장 1 |
| research/web-ai-architecture-comparison.md | 결론 2·3·4·5, 쓸 것 2 |
| research/web-balance-simulation-tools.md | 결론 2·5·6·8·9·10 |
| research/web-llm-authorable-tooling.md | 결론 6·7·9, 권장 3·5 |
| research/web-mass-monster-performance.md | 결론 1·2·3·4·6·8·9·10 |
| research/web-ml-generative-npc.md | 결론 3·4·6, 미확인 4, 추가 고려사항 1 |
| research/web-ue-5-6-to-5-8-ai-changes.md | 결론 2·3·10(참고), 버전 고정 권고 |
| research/zz-completeness-critique.md | §1 M5·M8·M12·M16·M18, §2 C2·C13, §3 B14 |
| 묶음 문서 | 02 §1·§2·§5.2·§5.5·§5.7·§5.8·§5.12·§6·§8·미결 1·4; 03 §1.3·§6·§9·미결 3; 04 §1·§2.3·§6.2·§7.3·§8·§12·미결 2; 05 §3.3·§4.2·§4.3·§9.2·미결 6; 06 §1·§5-1·§5-4·§5-5·미결 5 |
| 엔진 소스(읽기 전용, 5.8.2) | `Engine/Build/Build.version`; `Source/Runtime/Core/Private/Misc/Paths.cpp:114,1948`; `Source/Runtime/Core/Private/GenericPlatform/GenericPlatformOutputDevices.cpp:87`; `Source/Runtime/JsonUtilities/Public/JsonObjectConverter.h:233-239`; `Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/GameplayAbilityTypes.cpp:107-126`; `Source/Runtime/Engine/Private/TimerManager.cpp:1212` |
| 프로젝트 소스(2026-09-09) | `Source/TDGame/Combat/TDCombatComponent.cpp:220-223`; `Source/TDGame/Combat/TDDamageEntity.cpp:56`; `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp`(테스트 19개); `Docs/TDGASFoundation.md:85` |
| 설계안·심사 | A §7·§8·§9, B §4.4·§7·§8·§9, C §7·§8·§9, D §7·§9, 심사 접목 목록, 심사 판정(B Phase 1 3,000줄 과다, D Phase 0 300줄) |

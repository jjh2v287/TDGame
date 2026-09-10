# TDGame 현재 전투 코드의 결정론 위험·틱 구조·헤드리스 시뮬 적합성 진단

작성일: 2026-09-09. 근거는 모두 `상대 경로:줄 번호` 로 표기한다. 프로젝트 경로는 `C:/Project/TDGame` 기준, 엔진 경로는 `C:/Program Files/Epic Games/UE_5.8/Engine` 기준(`Engine/...`)이다. 코드가 근거이며 문서(`Docs/*.md`)는 보조 근거로만 쓴다. 확인하지 못한 것은 "미확인"으로 적는다.

## 결론 요약

1. **현재 전투 코드는 "고정 스텝 `World->Tick` + 같은 입력"이면 대부분 재현 가능하지만, 전역 난수 두 곳이 재현성을 깨는 유일한 실질적 원인이다.** 치명타 판정 `FMath::FRand()`(`Source/TDGame/Combat/TDCombatComponent.cpp:220`)와 SpawnEntity 산포 `FMath::FRand()` 두 번(`Source/TDGame/Combat/TDDamageSubsystem.cpp:203-204`)이 C 표준 `rand()` 전역 스트림을 쓴다(`Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:609-617`). 이 스트림은 엔진의 다른 코드와 공유되므로 시뮬레이션 인스턴스별 시드가 불가능하다. 시뮬레이션용으로는 `FTDDamageContext`(또는 시뮬 세션)에 `FRandomStream`을 넣어 호출부 두 곳을 바꾸는 것이 필수다.
2. **시간 기반 로직은 월드 시간(`UWorld::GetTimeSeconds`)과 월드 타이머에만 의존하므로 벽시계 독립이다.** 상태이상 펄스는 `FTimerManager`(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:300`), GameplayEffect 지속/주기도 `FTimerManager`(`Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/GameplayEffect.cpp:4485,4507`), 타이머는 `World->Tick`에서 `DeltaTime`만큼 진행한다(`Engine/Source/Runtime/Engine/Private/LevelTick.cpp:1816`, `Engine/Source/Runtime/Engine/Private/TimerManager.cpp:1107,1160`). 데미지 엔티티는 자체 `SimulationTime`으로 이벤트 시각을 분할 처리한다(`Source/TDGame/Combat/TDDamageEntity.cpp:96-167`).
3. **단, `World->Tick`에 넘긴 델타는 `AWorldSettings::FixupDeltaSeconds`가 0.0005~0.4초로 조용히 클램프한다.** (`Engine/Source/Runtime/Engine/Private/LevelTick.cpp:1590-1601`, `Engine/Source/Runtime/Engine/Private/WorldSettings.cpp:334-341`, 기본값 `Engine/Config/BaseGame.ini:198-199`). 현재 근접 노티파이 테스트가 0.5초 스텝을 넘기는데(`Source/TDGame/Combat/Tests/TDMeleeAttackNotifyTests.cpp:171,180`) 실제로는 0.4초씩 진행된다. 고속 시뮬레이션에서 큰 스텝을 쓰려면 `MaxUndilatedFrameTime`을 시뮬 월드의 WorldSettings에서 올리거나, 시뮬 루프가 "요청 시간"이 아니라 "월드가 실제 진행한 시간"을 기준으로 종료 조건을 판단해야 한다.
4. **대상 순회 순서는 정렬되지 않은 `TSet<TWeakObjectPtr>` 등록 순서에 의존한다.** `GatherTargets`(`Source/TDGame/Combat/TDDamageSubsystem.cpp:247-270`)는 결과를 정렬하지 않고, `HitArea`는 그 순서대로 피해를 준다(`Source/TDGame/Combat/TDDamageEntity.cpp:542-573`). 동률 판정 tie-break는 `GetUniqueID()`(전역 UObject 인덱스)를 쓴다(`Source/TDGame/Combat/TDDamageEntity.cpp:305-307, 497-498`). 같은 프로세스에서 같은 순서로 생성하면 재현되지만, 에디터/커맨드렛/패키지 실행 간 또는 이전에 만든 오브젝트 수가 달라지면 tie-break 결과가 달라질 수 있다. 시뮬 소유의 결정적 ID(스폰 순번)를 도입해야 한다.
5. **헤드리스 픽스처 `FTDScopedCombatWorld`는 내비게이션 시스템·AI 시스템·게임모드가 없는 월드다.** `UWorld::CreateWorld` 기본 초기화값은 `CreateNavigation(Editor 전용)`, `CreateAISystem(Editor 전용)`, `ShouldSimulatePhysics(false)`, `EnableTraceCollision(true)`이다(`Engine/Source/Runtime/Engine/Private/World.cpp:2850`). 따라서 이 월드에서 `ATDMonsterCharacter`는 스폰·AI 컨트롤러 자동 소유·GAS 시전·피격은 되지만(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:654-676, 678-703`), `AAIController::MoveTo` 계열 이동은 내비 시스템 부재로 실패한다(`Engine/Source/Runtime/AIModule/Private/AIController.cpp:841-892`, 특히 888줄 "pathfinding-less movement requires presence of NavigationSystem"). 즉 **현재 헤드리스 세계에서 "몬스터가 다가와서 때린다"는 아직 성립하지 않는다.** 이동은 `UCharacterMovementComponent`에 직접 입력(`AddMovementInput`)을 주거나 시뮬 전용 이동으로 대체해야 한다.
6. **근접 공격 판정은 스켈레탈 메시·애님 인스턴스·몽타주가 있어야만 성립한다.** `UTDAnimNotifyState_MeleeAttack::InitializeSweepState`는 `MeshComp->GetSkinnedAsset()`과 소켓/본 해석이 실패하면 즉시 반환한다(`Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.cpp:149-168`). 헤드리스 테스트는 실제 마네킹 메시와 공격 시퀀스를 로드해서만 동작한다(`Source/TDGame/Combat/Tests/TDMeleeAttackNotifyTests.cpp:22-23,146-154`). 대량 몬스터 밸런스 시뮬에서 몬스터마다 애니메이션을 재생하는 것은 비용상 부적절하므로, 시뮬 경로는 "노티파이 시작~끝 시간 창 + 사거리/각도" 같은 수치 모델로 근접 공격을 대체해야 하고, 그 수치는 노티파이 데이터(`TriggerTime`, `EndTriggerTime`, 소켓 궤적)에서 파생해 저장해 두는 것이 좋다.
7. **GAS 예측/네트워크 코드는 없다.** ASC는 복제 안 함(`Source/TDGame/Combat/TDCombatComponent.cpp:44`), 어빌리티는 `LocalOnly`·`InstancedPerActor`(`Source/TDGame/Combat/GAS/TDDamageGameplayAbility.cpp:14-15`), 시전은 `HasAuthority()` 검사만 한다(`Source/TDGame/Combat/TDCombatComponent.cpp:309`). 결정론 관점의 GAS 위험은 예측이 아니라 "ASC 컴포넌트가 기본적으로 매 프레임 틱을 켠다"는 비용 문제다(`Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/AbilitySystemComponent.cpp:58`).
8. **틱 구조: 프로젝트 코드에는 틱 간격(`SetTickInterval`) 설정이 한 곳도 없다.** 데미지 엔티티만 필요할 때 틱을 켜고 끈다(`Source/TDGame/Combat/TDDamageEntity.cpp:13-14,72,655`). 몬스터 1마리는 `ACharacter`(Pawn 틱) + `UCharacterMovementComponent` + `USkeletalMeshComponent`(CMC 선행 조건 틱) + ASC(매 프레임 틱) + `AAIController` 액터로 구성되어, 500마리 규모에서는 이 조합 자체가 병목이 된다. 대량 시뮬에서는 `ACharacter` 기반 몬스터 대신 "전투 컴포넌트 + 단순 루트 컴포넌트" 액터(테스트 픽스처의 `SpawnCombatant` 방식, `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:49-69`)가 훨씬 가볍고 이미 검증되어 있다.
9. **플레이어 조건(장비·물약·버프)은 AttributeSet 9개 속성 + GameplayEffect로 표현할 수 있으나 인벤토리·장비 슬롯·소비 아이템 개념은 코드에 없다.** 지속 버프는 `MaxHealth`·공격력 등 비체력 속성에만 허용되고 지속형 Health 수정자는 거부된다(`Source/TDGame/Combat/TDCombatComponent.cpp:20-39`). 물약은 즉시(Instant) 또는 주기(Periodic) Health 효과로만 표현된다.
10. **DataAsset 12개는 C++ 함수 `TDDamageExamples::CreateExamples`가 원본이며(`Source/TDGame/Combat/TDDamageExamples.cpp:43-173`), 시뮬레이션은 `.uasset`을 열지 않고 `NewObject<UTDDamageDefinition>`으로 같은 그래프를 메모리에서 만들 수 있다.** 이미 테스트 21개 이상이 그렇게 한다. 다만 에디터에서 에셋을 편집하면 C++ 원본과 달라지고, 커맨드렛 `-ValidateOnly`는 유효성만 검사하고 차이는 비교하지 않는다(`Source/TDGame/Combat/TDDamageExamplesCommandlet.cpp:61-96`).
11. **Variant_TwinStick의 StateTree 사용법은 "C++ 컨트롤러가 `UStateTreeAIComponent`를 만들고, 어떤 StateTree 에셋을 쓰는지는 블루프린트/에셋에서 지정"하는 구조다**(`Source/TDGame/Variant_TwinStick/AI/TwinStickAIController.cpp:7-18`). 트리 자체는 바이너리 에셋이라 텍스트로 분석·수정할 수 없다. 반면 StateTree 런타임은 `FStateTreeExecutionContext`를 코드에서 직접 만들고 `Start(..., RandomSeed)`로 시드까지 줄 수 있어(`Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeExecutionContext.h:332-335,477-493`), 컴포넌트 없이 시뮬 매니저가 수동 틱하는 것은 엔진이 막지 않는다. 컴포넌트 경로도 5.8에서는 "예약 틱(sleep/interval)"을 지원한다(`Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Private/Components/StateTreeComponent.cpp:19-23,298-330`).

## 상세 조사

### 1) 비결정 요소 목록

#### 1-1. 난수

| 위치 | 코드 | 영향 | 근거 |
| --- | --- | --- | --- |
| 치명타 판정 | `Result.bWasCritical = bCanCrit && Chance > 0.f && (Chance >= 1.f \|\| FMath::FRand() < Chance);` | 피해량·처치 여부·Kill 연계까지 갈라짐 | `Source/TDGame/Combat/TDCombatComponent.cpp:220` |
| SpawnEntity 산포 | `Angle = FMath::FRand() * UE_TWO_PI; Distance = Sqrt(FRand()) * ScatterRadius` | 블리자드 낙하 위치(예제 `ScatterRadius = 190`) | `Source/TDGame/Combat/TDDamageSubsystem.cpp:203-205`, `Source/TDGame/Combat/TDDamageExamples.cpp:96-97` |
| TwinStick 드롭·스폰 지연 | `FMath::RandRange` | 템플릿 전용, 전투 코드 아님 | `Source/TDGame/Variant_TwinStick/AI/TwinStickNPC.cpp:104`, `Source/TDGame/Variant_TwinStick/AI/TwinStickSpawner.cpp:87` |

엔진 사실: `FMath::FRand()`는 `(Rand() & RandMax) / (float)RandMax`, `Rand()`는 C 표준 `rand()`이고 시드는 `RandInit(Seed) { srand(Seed); }` 전역 하나뿐이다(`Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMath.h:609-617`). 엔진 실행 인자 `-Deterministic`은 `-UseFixedTimeStep -FixedSeed`의 축약이며(`Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:2457-2462`), `FApp::bUseFixedSeed`는 예를 들어 `UCharacterMovementComponent`의 자체 `RandomStream`을 컴포넌트 이름으로 시드하는 데 쓰인다(`Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp:657`). 전투 코드는 `FApp::bUseFixedSeed`를 참조하지 않는다.

판단: 치명타 하나 때문에 시뮬 전체의 재현성이 무너진다. 밸런스 툴은 (a) 시드 고정 `FRandomStream`을 `FTDDamageContext`에 실어 시전 스냅샷과 함께 복사하거나, (b) 치명타를 "기대값(확률×배율)"로 치환하는 결정적 모드를 별도로 두는 두 가지가 모두 필요하다(승률 분포를 볼 때는 (a), 회귀 테스트는 (b)).

#### 1-2. 타이머(`FTimerManager`)

| 위치 | 용도 | 근거 |
| --- | --- | --- |
| 상태이상 다음 갱신 예약 | `SetTimer(StatusTimer, this, &UpdateStatuses, Delay, false)`; `Delay`는 `NextUpdateTime - World->GetTimeSeconds()`를 0.001 이상으로 클램프 | `Source/TDGame/Combat/TDCombatComponentStatus.cpp:265-301` |
| 상태이상 시각 기준 | `const double Now = World->GetTimeSeconds();` | `Source/TDGame/Combat/TDCombatComponentStatus.cpp:31,186` |
| GE 지속/주기 | 엔진 GAS가 `TimerManager.SetTimer(DurationHandle/PeriodHandle, ...)` | `Engine/.../GameplayEffect.cpp:4483-4507` |
| 데미지 엔티티 | 타이머 미사용. `SimulationTime`·`ExpirationTime`을 월드 시간에서 초기화하고 Tick에서 자체 분할 | `Source/TDGame/Combat/TDDamageEntity.cpp:54-57,96-167` |

엔진 사실: `UWorld::Tick`은 `GetTimerManager().Tick(DeltaSeconds)`를 호출하고(`Engine/Source/Runtime/Engine/Private/LevelTick.cpp:1816`), `FTimerManager::Tick`은 `InternalTime += DeltaTime`으로만 진행한다(`Engine/Source/Runtime/Engine/Private/TimerManager.cpp:1107,1160`). 벽시계를 읽지 않으므로 고정 스텝이면 결정적이다.

주의 1: 시간 델타 클램프. `UWorld::Tick`은 `DeltaSeconds *= Info->GetEffectiveTimeDilation()` 후 `Info->FixupDeltaSeconds(...)`로 `[MinUndilatedFrameTime, MaxUndilatedFrameTime]`에 클램프한다(`Engine/Source/Runtime/Engine/Private/LevelTick.cpp:1590-1601`, `Engine/Source/Runtime/Engine/Private/WorldSettings.cpp:334-341`). 기본값은 0.0005/0.4초다(`Engine/Config/BaseGame.ini:198-199`). 픽스처 `Tick(Duration, Step)`은 요청한 델타를 그대로 넘기므로(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:71-80`) Step이 0.4를 넘으면 누적 시간이 요청보다 짧아진다.

주의 2: 빙결은 `Owner->CustomTimeDilation = 0`과 액터/이동 컴포넌트 틱 비활성화로 구현되고(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:409-427`), GE 지속 시간은 월드 시간으로 진행한다(`Engine/.../GameplayEffect.cpp:2845-2846`). 이 조합은 결정적이지만, 시뮬에서 "액터 틱 비활성"이 어떤 로직을 멈추는지(AI 판단 포함)는 설계 시 명시해야 한다.

#### 1-3. 순회 순서 의존

| 위치 | 상태 | 근거 |
| --- | --- | --- |
| `GatherTargets` | `Combatants`(`TSet<TWeakObjectPtr<UTDCombatComponent>>`)를 순회하며 조건 통과 순으로 `OutTargets.Add`. 정렬 없음 | `Source/TDGame/Combat/TDDamageSubsystem.h:29`, `Source/TDGame/Combat/TDDamageSubsystem.cpp:247-270` |
| `HitArea` / `Pulse(Mine)` | `GatherTargets` 결과 순서대로 `HitTarget` → 처치 순서·Kill 연계 순서가 등록 순서에 의존 | `Source/TDGame/Combat/TDDamageEntity.cpp:337-376,542-573` |
| `MoveProjectile` | 적중 분율로 정렬, 동률은 `GetUniqueID()` 비교 → 결정적이지만 ID가 전역 오브젝트 인덱스 | `Source/TDGame/Combat/TDDamageEntity.cpp:490-499` |
| `AcquireHomingTarget` | 최근접, 동률은 `GetUniqueID()` 작은 쪽 | `Source/TDGame/Combat/TDDamageEntity.cpp:295-313` |
| 근접 스윕 | `SweepMultiByChannel` 결과 순서대로 적중 처리(같은 노티파이 창에서 대상당 1회) | `Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.cpp:335-375,377-409` |
| 테스트의 `TActorIterator` | 개수 세기용, 순서 무관 | `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:125-136` |

`TSet`은 원소를 희소 배열에 삽입 순서로 저장하므로 순회 순서는 "등록 순서 + 삭제 후 빈 슬롯 재사용"에 의존한다. 등록은 `UTDCombatComponent::BeginPlay`(`Source/TDGame/Combat/TDCombatComponent.cpp:61-64`), 해제는 `EndPlay`(`Source/TDGame/Combat/TDCombatComponent.cpp:74-80`)다. 같은 스폰·사망 순서면 재현되지만, 스폰 순서를 바꾸면 결과가 달라지는 "숨은 입력"이다. 시뮬 입력 명세에 "스폰 순서"를 포함하거나 `GatherTargets` 결과를 결정적 키(시뮬 ID)로 정렬해야 한다.

#### 1-4. 프레임 델타 의존 이동·수명

- 데미지 엔티티는 델타를 "다음 이벤트 시각"으로 분할해 이동한다(`Source/TDGame/Combat/TDDamageEntity.cpp:105-124,392-415`). 투사체는 한 구간을 선분 하나로 스윕하고(`417-489`), 호밍 회전은 `TurnRate × DeltaSeconds`로 구간마다 적분한다(`316-335`). 따라서 스텝 크기가 바뀌면 호밍 궤적과 곡선 이동의 적중 여부가 달라질 수 있다. 같은 스텝이면 재현된다.
- 프레임당 펄스 따라잡기 상한 8회(`Source/TDGame/Combat/TDDamageEntity.cpp:107-110`), 상태이상 펄스 상한 64회(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:240,257-260`) — 큰 스텝에서는 펄스가 생략되어 결과가 스텝에 의존한다.
- 근접 스윕은 `SampleIntervalSeconds`(기본 1/60) 간격으로 애니메이션 원본을 재샘플링하고 컴포넌트 트랜스폼은 프레임 사이를 선형 보간한다(`Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.cpp:300-333`, 헤더 `Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.h:67-68`). 정지 상태에서는 스텝 독립, 이동 중에는 보간 오차만큼 스텝 의존.
- `UCharacterMovementComponent` 이동은 델타 적분이며 별도 서브스텝 설정을 프로젝트가 건드리지 않는다(프로젝트 내 `SetTickInterval`/`MaxSimulationTimeStep` 설정 없음 — `grep` 결과 0건).

#### 1-5. GAS 예측/네트워크

- ASC 복제 비활성: `SetIsReplicatedByDefault(false)`(`Source/TDGame/Combat/TDCombatComponent.cpp:44`).
- 어빌리티: `InstancingPolicy = InstancedPerActor`, `NetExecutionPolicy = LocalOnly`(`Source/TDGame/Combat/GAS/TDDamageGameplayAbility.cpp:14-15`). 시전 진입은 `TriggerAbilityFromGameplayEvent`(`Source/TDGame/Combat/TDCombatComponent.cpp:340`)로, 예측 키를 만들지 않는다.
- 권한 검사: `GetOwner()->HasAuthority()`(`Source/TDGame/Combat/TDCombatComponent.cpp:309`). 헤드리스 `CreateWorld(EWorldType::Game)`에는 NetDriver가 없어 권한 있음으로 동작한다(테스트 `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:695`가 통과 전제).
- `bPredictionRejected`는 방어적으로만 검사(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:322`).
- 엔진: `UAbilitySystemGlobals::InitGlobalData()`는 모듈이 첫 `GetAbilitySystemGlobals()` 호출 시 자동 실행한다(`Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Private/GameplayAbilitiesModule.cpp:25-40`). 별도 초기화 없이 헤드리스에서 동작하는 이유다.

#### 1-6. 애니메이션 몽타주·노티파이 의존 공격 판정

- 진입: `NotifyBegin`이 `InitializeSweepState` 실패 시 상태를 만들지 않는다(`Source/TDGame/Combat/AnimNotify/TDAnimNotifyState_MeleeAttack.cpp:59-74`). 실패 조건: 몽타주 슬롯 안 시퀀스 배치(`135-141`), `WeaponBaseSocketName` 미설정(`143-147`), `Owner`/`SkinnedAsset` 없음(`149-154`), `UTDCombatComponent` 없음 또는 사망(`156-160`), 소켓/본 미해석(`163-168`).
- 포즈는 렌더 결과가 아니라 애니메이션 원본에서 `FCompactPose`로 재추출한다(`243-287`). 루트 모션 잠금 판단은 `UAnimInstance`가 없으면 `true`(`412-426`).
- 판정은 물리 스윕 `World->SweepMultiByChannel`(`365`)이고 적중 시 `UTDDamageSubsystem::ExecuteRules(HitRules, Hit, ...)`(`408`). 이 경로에는 `SourceEntity`가 없어서 `DelaySeconds > 0` 액션은 실행되지 않는다(`Source/TDGame/Combat/TDDamageSubsystem.cpp:97-103`, 문서 `Docs/TDDamageSystemGuide.md:114`).
- 헤드리스 검증: 테스트는 `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`과 `MM_Attack_01`을 로드하고(`Source/TDGame/Combat/Tests/TDMeleeAttackNotifyTests.cpp:22-23`), `USkeletalMeshComponent`에 `AlwaysTickPoseAndRefreshBones`·`AnimationSingleNode`를 지정한 뒤(`71-84`) 동적 몽타주를 만들어 재생한다(`100-122,168-172`). 물리 스윕이 성립하는 이유는 `CreateWorld` 기본값이 `EnableTraceCollision(true)`이기 때문이다(`Engine/Source/Runtime/Engine/Private/World.cpp:2850`).
- 결론: 스켈레탈 메시·애님 인스턴스 없이 근접 공격은 성립하지 않는다. 시뮬은 노티파이 기반 판정을 그대로 쓸 수 없고, 사거리·각도·시간 창으로 축약한 수치 모델이 필요하다.

### 2) 틱 구조

| 클래스 | 틱 설정 | 근거 |
| --- | --- | --- |
| `ATDDamageEntity` | `bCanEverTick = true`, `bStartWithTickEnabled = false`; `BeginPlay`에서 `SetActorTickEnabled(CanContinue())`; 종료 시 `SetActorTickEnabled(false)`. Tick은 `ProcessTimeline()` + 디버그 그리기 | `Source/TDGame/Combat/TDDamageEntity.cpp:13-14,72,378-390,655` |
| `ATDDamageTarget` | 항상 틱, 매 프레임 카메라를 향해 텍스트 회전 | `Source/TDGame/Combat/TDDamageTarget.cpp:14,60-70` |
| `ATDGameCharacter`(플레이어) | `bCanEverTick`·`bStartWithTickEnabled = true`, Tick 본문은 stub | `Source/TDGame/TDGameCharacter.cpp:53-54,89-93` |
| `ATDCombatCharacter`/`ATDMonsterCharacter` | 틱 설정 없음 → `APawn` 기본 `bCanEverTick = true` 상속 | `Source/TDGame/Combat/Characters/TDCombatCharacter.cpp:9-13`, `Engine/Source/Runtime/Engine/Private/Pawn.cpp:50` |
| `UTDCombatComponent`(ASC) | `TickComponent` 오버라이드 없음. 엔진 ASC가 `PrimaryComponentTick.bStartWithTickEnabled = true`로 시작 | `Engine/.../AbilitySystemComponent.cpp:58`; 부모 `UGameplayTasksComponent`는 `TG_DuringPhysics`, 시작 비활성(`Engine/Source/Runtime/GameplayTasks/Private/GameplayTasksComponent.cpp:53-55`) |
| ASC 틱 본문 | 몽타주 복제 데이터 갱신 + `ITickableAttributeSetInterface` 틱 | `Engine/.../AbilitySystemComponent_Abilities.cpp:139-158` |
| 빙결 시 | 소유 액터 틱·모든 `UMovementComponent` 틱 비활성, AI `Brain->PauseLogic` | `Source/TDGame/Combat/TDCombatComponentStatus.cpp:412,426,434-442` |
| 틱 간격 | 프로젝트 전체에서 `SetTickInterval`/`TickInterval` 사용 0건 | `grep -rn TickInterval Source/TDGame` 결과 없음 |

`ATDMonsterCharacter` 1마리의 구성 비용(코드 근거):
- `ACharacter` 상속 → 캡슐, 스켈레탈 메시(틱 그룹 `TG_PrePhysics`, CMC 선행 조건), `UCharacterMovementComponent`(`Engine/Source/Runtime/Engine/Private/Character.cpp:128,153-155`). CMC 비동기 물리 스레드 이동 `p.AsyncCharacterMovement` 기본 0(`Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp:262-265`).
- ASC(`UTDCombatComponent`)와 액터 소유 `UTDCombatAttributeSet`(`Source/TDGame/Combat/Characters/TDCombatCharacter.cpp:11-12,22-23`).
- `AutoPossessAI = PlacedInWorldOrSpawned`, `AIControllerClass = AAIController`(`Source/TDGame/Combat/Characters/TDMonsterCharacter.cpp:10-11`) → `APawn::PostInitializeComponents`가 컨트롤러 액터를 추가로 스폰한다(`Engine/Source/Runtime/Engine/Private/Pawn.cpp:146-157,384`). 두뇌(BrainComponent)는 없다.
- 데미지 엔티티 1개는 헤드리스에서도 `UStaticMeshComponent`와 `UNiagaraComponent`를 생성한다(`Source/TDGame/Combat/TDDamageEntity.cpp:17-24`). 예제 에셋은 `bDrawDebug = true`라 매 틱 `DrawShape`를 호출한다(`Source/TDGame/Combat/TDDamageExamples.cpp:12`, `Source/TDGame/Combat/TDDamageEntity.cpp:386-389`).

엔진 틱 순서 관련 콘솔 변수(`Engine/Source/Runtime/Engine/Private/TickTaskManager.cpp:54-79`): `tick.AllowAsyncComponentTicks`=1, `tick.AllowBatchedTicks`=0, `tick.AllowBatchedTicksUnordered`=0, `tick.AllowOptimizedPrerequisites`=1, `tick.AllowConcurrentTickQueue`=0. 프로젝트 전투 틱 함수는 모두 게임 스레드 틱(`bRunOnAnyThread` 미설정)이며, 같은 틱 그룹 안의 액터 간 실행 순서는 등록 순서에 의존하고 명시적 보장은 없다. 순서 의존 로직(예: 두 몬스터가 같은 프레임에 같은 대상을 공격)은 시뮬 매니저가 순서를 정해 호출하는 편이 안전하다. 프레임 단위 고정 스텝 실행 인자는 `-UseFixedTimeStep`, `-FPS=`(`Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:2460,4725-4730`), API는 `FApp::SetUseFixedTimeStep`/`SetFixedDeltaTime`(`Engine/Source/Runtime/Core/Public/Misc/App.h:662-675`)이다. 픽스처처럼 `World->Tick`을 직접 부르는 방식은 이 인자와 무관하게 결정적이다.

### 3) 헤드리스 적합성

픽스처 초기화 경로(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:30-47`, 동일 구조 `TDDamageHomingTests.cpp:15-33`, `TDMeleeAttackNotifyTests.cpp:24-42`):

```cpp
World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
World->InitializeActorsForPlay(FURL());
World->BeginPlay();
World->SetBegunPlay(true);
```

엔진에서 이 경로가 만드는 것/안 만드는 것:
- `CreateWorld` 기본 `InitializationValues`: 물리 씬 생성(시뮬레이션은 끔), 트레이스 콜리전 켬, 내비게이션·AI 시스템은 `EWorldType::Editor`일 때만 생성(`Engine/Source/Runtime/Engine/Private/World.cpp:2850`). `InitWorld`의 생성 분기 `Engine/Source/Runtime/Engine/Private/World.cpp:2485-2494`.
- `UWorld::BeginPlay`: 월드 서브시스템 `OnWorldBeginPlay` 호출 → `UTDDamageSubsystem` 사용 가능. `GameMode->StartPlay()`와 `AISystem->StartPlay()`는 게임모드가 있을 때만(`Engine/Source/Runtime/Engine/Private/World.cpp:6165-6180`). 게임모드가 없어 `bBegunPlay`를 테스트가 직접 `SetBegunPlay(true)`로 켠다.
- 결과: 내비게이션 시스템 없음, `UAISystem` 없음(EQS·행동 트리 매니저 없음), 게임모드/게임인스턴스 없음, 렌더 없음.

이 월드에서 실제로 되는 것(테스트가 증명):
- 순수 `AActor + USphereComponent + UTDCombatComponent` 전투원 스폰·피해·상태이상·사망(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:49-69` 및 대부분 테스트).
- `ATDCompanionCharacter`/`ATDMonsterCharacter` 스폰, ASC/AttributeSet 연결, 레벨 성장(`654-676`), GAS 어빌리티 시전과 쿨다운(`678-703`).
- `ACharacter` 스폰 후 CMC 이동 모드 변경/복원(`559-590`).
- 투사체 물리 스윕·벽 차단(`495-530`), 근접 노티파이 스윕(`TDMeleeAttackNotifyTests.cpp:146-183`).

되지 않거나 미검증인 것:
- AI 컨트롤러의 `MoveTo` 이동: `BuildPathfindingQuery`가 내비 시스템 없음으로 실패(`Engine/Source/Runtime/AIModule/Private/AIController.cpp:841-892`). 프로젝트 테스트 중 몬스터가 이동하는 테스트는 없다.
- StateTree/행동 트리 실행: `UAISystem`이 없는 월드에서 `UStateTreeAIComponent`·EQS 실행 가능 여부 미확인.
- 애니메이션 없는 근접 공격(1-6절).
- 시뮬 결과 수집 API(승패·시간·피해 로그): 현재는 `OnDamaged`/`OnDeath` 델리게이트(`Source/TDGame/Combat/TDCombatComponent.h:90-92`)를 테스트가 람다로 받는 수준.

자동화 테스트 현황: `IMPLEMENT_SIMPLE_AUTOMATION_TEST` 개수는 `TDDamageSystemTests.cpp` 19개, `TDDamageHomingTests.cpp` 8개, `TDMeleeAttackNotifyTests.cpp` 1개, 합계 28개(`grep -c` 결과). 문서의 "21개"는 GAS 전환 전 통과 수이고(`Docs/TDDamageSystemDesign.md:11-12`), GAS 전환 후 실행 결과는 문서상 미기록이다(`Docs/TDGASFoundation.md:85`). 커버 범위: 피해 공식·치명타 경계(`139`), 투사체→장판 연계·시전자 스냅샷(`187`), 냉기 누적·빙결·만료(`247`), 리셋/사망 취소(`277`), 쇼크웨이브(`304`), 팀 정책·공간 필터(`333`), 연계 예산(`360`), 잘못된 정의(`392`), 지뢰(`466`), 투사체 충돌(`495`), 재진입 사망(`532`), 빙결 복원(`559`), 소스 파괴(`592`), 기본 게임모드 에셋(`621`), GAS 5종(`654,678,705,727,749`), 호밍 8종(`TDDamageHomingTests.cpp:132,171,198,231,271,294,314,339`), 근접 스윕 1종. **AI 판단·이동·다수 몬스터·플레이어 조건 조합은 어떤 테스트도 덮지 않는다.**

### 4) 플레이어 조건(장비·물약·버프) 표현 수단

현재 속성(`Source/TDGame/Combat/GAS/TDCombatAttributeSet.h:20-52`): `Health, MaxHealth, Level, AttackPower, SpellPower, Armor, MagicResistance, CriticalChance, CriticalMultiplier`. 클램프 규칙은 `ClampAttribute`(`Source/TDGame/Combat/GAS/TDCombatAttributeSet.cpp:45-82`).

| 조건 | 현재 가능한 표현 | 근거 | 없는 것 |
| --- | --- | --- | --- |
| 장비(영구 스탯) | `Infinite` 또는 `HasDuration` GE의 Additive/Multiplicative 수정자를 `MaxHealth/AttackPower/SpellPower/Armor/MagicResistance/CriticalChance/CriticalMultiplier`에 적용. 시작 시 적용은 `StartupEffects` 배열 | `Source/TDGame/Combat/Characters/TDCombatCharacter.h:43-44`, `Source/TDGame/Combat/Characters/TDCombatCharacter.cpp:45-50` | 장비 슬롯·아이템 정의·인벤토리 클래스 없음(`grep -i Inventory\|Equipment\|Potion` 결과 0건) |
| 물약(즉시 회복) | Instant GE로 Health 양수 수정. `HandleHealthChanged`는 증가 시 사망 상태만 갱신 | `Source/TDGame/Combat/TDCombatComponent.cpp:243-254` | 소비 수량·쿨다운·사용 AI 정책 없음 |
| 물약(지속 회복) | Periodic GE(주기 있음)만 허용 | `Source/TDGame/Combat/TDCombatComponent.cpp:20-39` (`CanApplyEffect`: 주기 없는 지속형 Health 수정자 거부) | — |
| 버프(일시) | `HasDuration` GE로 비체력 속성 수정. `GetStats()`가 버프 반영 스냅샷 반환 | `Source/TDGame/Combat/TDCombatComponent.cpp:84-103`, `Docs/TDGASFoundation.md:39` | 이동 속도·공격 속도·쿨다운 감소·속성별 저항·흡혈 등의 속성 자체가 없음 |
| 스킬 구성 | `DamageSpells` 배열 + 정의별 쿨다운 GE | `Source/TDGame/Combat/Characters/TDCombatCharacter.h:37-38`, `Source/TDGame/Combat/GAS/TDDamageGameplayAbility.cpp:45-88` | 마나/자원 없음 |

시뮬 관점의 중요한 성질: 시전 시 `Context.Stats = Combatant->GetStats()`로 스냅샷을 복사한다(`Source/TDGame/Combat/TDDamageSubsystem.cpp:10-25`). 따라서 "시전 후 버프가 바뀌어도 날아가는 효과는 그대로"이며, 플레이어 조건은 "시전 시점 스탯 벡터"로 완전히 요약된다. 밸런스 툴은 GE를 실제 적용하는 대신 `FTDCombatStats`를 직접 만들어 `SetStats`(`Source/TDGame/Combat/TDCombatComponent.cpp:120-152`)로 주입해도 같은 결과를 얻는다(치명타 난수 제외). 단, 장비 효과가 "속성 이외의 규칙"(예: 적중 시 추가 투사체)을 가지면 현재 GE 모델로는 표현할 수 없고 `FTDDamageRule` 추가가 필요하다.

### 5) DataAsset 12개 구조와 시뮬 입력 재사용성

목록(`Docs/TDDamageSystemGuide.md:36-40`, 실제 파일 `Content/Combat/Examples/*.uasset` 12개 확인): 시작점 6개(`DA_TDFireball, DA_TDBlizzard, DA_TDMine, DA_TDShockwave, DA_TDMeteor, DA_TDDelayedHoming`), 후속 5개(`DA_TDFlameField, DA_TDIceShard, DA_TDMineExplosion, DA_TDFallingMeteor, DA_TDMeteorExplosion`), 상태이상 1개(`DA_TDFrostFreeze`). 이 외 `Content/Combat/Blueprints/` 3개는 설정 전용 블루프린트다.

C++ 정의:
- `UTDDamageDefinition : UDataAsset`(`Source/TDGame/Combat/TDDamageDefinition.h:13-92`): `Cooldown, CastRange, Mode(Projectile/Area/Mine/Shockwave), TargetPolicy, Lifetime, ActivationDelay, PulseInterval, Radius, InnerRadius, HalfHeight, ExpansionSpeed, ProjectileSpeed, ProjectileRadius, MaxHitsPerTarget, HitInterval, bDestroyOnHit, bRequireLineOfSight, Rules[]`, 표현용 `VisualEffect, Mesh, Material, VisualScale, bDrawDebug, DebugColor`. `ValidateDefinition`(`Source/TDGame/Combat/TDDamageDefinition.cpp:171-249`).
- `UTDStatusDefinition : UDataAsset`(`Source/TDGame/Combat/TDStatusDefinition.h:9-38`): `Duration, PulseInterval, bFreezesTarget, DamageThreshold, BuildupResetDelay, bRefreshDuration, Rules[]`.
- 규칙/액션(`Source/TDGame/Combat/TDDamageTypes.h:209-266`): `FTDDamageRule{Event, Actions[]}`, `FTDDamageAction{Type, DelaySeconds, Homing, Magnitude(FTDScaledValue), Element, bCanCrit, Status, Entity, SpawnAnchor, SpawnDirection, SpawnOffset, SpawnCount, ScatterRadius}`.
- 수치 공식은 `FTDScaledValue::Evaluate`와 `FTDCombatStats::Get*`(`Source/TDGame/Combat/TDDamageTypes.cpp:21-42`), 피해 최종식은 `RawDamage × 배율 × 100 / (100 + 저항)`(`Source/TDGame/Combat/TDCombatComponent.cpp:222-223`).

원본은 코드다: 12개 에셋은 `TDDamageExamples::CreateExamples`(`Source/TDGame/Combat/TDDamageExamples.cpp:43-173`)가 만든 오브젝트를 커맨드렛이 `/Game/Combat/Examples/` 패키지로 저장한 것이다(`Source/TDGame/Combat/TDDamageExamplesCommandlet.cpp:45-137`). 플레이어는 에셋 로드 실패 시 같은 함수로 메모리 생성한다(`Source/TDGame/TDGameCharacter.cpp:57-83`). 테스트는 항상 `NewObject`로 정의를 만든다.

시뮬 입력 재사용성 판단:
- 재사용 가능: 정의는 순수 값 구조이고, 표현 필드는 `ATDDamageEntity::BeginPlay`가 컴포넌트에 넘길 뿐 판정에 쓰지 않는다(`Source/TDGame/Combat/TDDamageEntity.cpp:60-67`). 시뮬은 `NewObject`로 만든 정의 또는 로드한 에셋을 그대로 `UTDDamageSubsystem::Cast/SpawnEntity`에 넣으면 된다.
- 주의: 에셋 편집본과 C++ 원본이 다를 수 있다. 커맨드렛 `-ValidateOnly`는 유효성만 본다(`Source/TDGame/Combat/TDDamageExamplesCommandlet.cpp:61-96`). "텍스트 우선" 원칙을 지키려면 에셋 → JSON(또는 C++ 표) 내보내기와 차이 검사가 필요하다. 이 기능은 현재 없다.
- 주의: 예제는 `bDrawDebug = true`(`Source/TDGame/Combat/TDDamageExamples.cpp:12`). 시뮬에서는 꺼야 한다.
- 몬스터 정의(체력·공격·스킬 목록·AI 파라미터)를 담는 DataAsset은 없다. 몬스터 스탯은 클래스 생성자 기본값(`Source/TDGame/Combat/Characters/TDMonsterCharacter.cpp:7-9`)과 블루프린트 설정에 의존한다.

### 6) Variant_TwinStick의 StateTree AI 사용 방식과 몬스터 확장 한계

사용 방식:
- `ATwinStickAIController : AAIController`가 생성자에서 `UStateTreeAIComponent`를 만들고 `bStartAILogicOnPossess = true`, `bAttachToPawn = true`(EQS용)로 설정한다(`Source/TDGame/Variant_TwinStick/AI/TwinStickAIController.h:22`, `Source/TDGame/Variant_TwinStick/AI/TwinStickAIController.cpp:7-18`). 어떤 StateTree 에셋을 쓰는지는 C++에 없고 블루프린트 파생 클래스/에셋에서 지정한다(클래스가 `abstract`, `.h:15`).
- C++ 태스크 예: `FStateTreeGetPlayerTask : FStateTreeTaskCommonBase`는 매 틱 `UGameplayStatics::GetPlayerPawn`을 호출하고 `Running`을 반환한다(`Source/TDGame/Variant_TwinStick/AI/TwinStickStateTreeUtility.h:33-46`, `Source/TDGame/Variant_TwinStick/AI/TwinStickStateTreeUtility.cpp:12-22`). 태스크 로직은 텍스트(C++)이지만 상태·전이 구조는 에셋에 있다.
- NPC는 `ACharacter` 파생으로 `ATDCombatCharacter`와 무관하고(`Source/TDGame/Variant_TwinStick/AI/TwinStickNPC.h:17-18`), 피해는 `NotifyHit`로 플레이어에 직접 전달한다(`Source/TDGame/Variant_TwinStick/AI/TwinStickNPC.cpp:73-81`). RVO 회피 켬(`Source/TDGame/Variant_TwinStick/AI/TwinStickNPC.cpp:35-37`). 스폰은 내비 시스템의 무작위 도달점 사용(`Source/TDGame/Variant_TwinStick/AI/TwinStickSpawner.cpp:73`).
- 모듈 의존은 이미 있다: `AIModule, NavigationSystem, StateTreeModule, GameplayStateTreeModule`(`Source/TDGame/TDGame.Build.cs:17-20`).

엔진 사실(확장 판단용):
- `UStateTreeComponent`는 `bStartWithTickEnabled = false`로 시작하고(`Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Private/Components/StateTreeComponent.cpp:45`), 로직 시작 후 `ScheduleTickFrame`이 `FStateTreeScheduledTick`에 따라 틱을 끄거나(`ShouldSleep`) 간격을 두어 켠다(`298-330`). 전역 스위치 `StateTree.Component.ScheduledTickEnabled`(`19-23`). 즉 "매 프레임 틱에 묶인다"는 우려는 5.8 컴포넌트 경로에서는 부분적으로 해소되어 있다. `UStateTreeAIComponent : UStateTreeComponent`(`Engine/Plugins/Runtime/GameplayStateTree/Source/GameplayStateTreeModule/Public/Components/StateTreeAIComponent.h:16`), `InitializeComponent` 주석 "Skipping UBrainComponent"(`StateTreeComponent.cpp:51-54`)로 `UBrainComponent` 계열임을 확인 → 프로젝트의 빙결 `Brain->PauseLogic`(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:437-442`)이 그대로 적용된다.
- 컴포넌트 없이도 `FStateTreeExecutionContext(Owner, StateTree, InstanceData)`를 만들어 `Start(..., RandomSeed)`/`Tick` 할 수 있다(`Engine/Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTreeExecutionContext.h:332-335,477-493`). 시뮬 매니저가 몬스터 배열을 순서대로 수동 틱하는 구조가 가능하다.

몬스터 확장 시 한계:
1. `ATDMonsterCharacter`는 두뇌 없는 `AAIController`를 쓴다(`Source/TDGame/Combat/Characters/TDMonsterCharacter.cpp:11`). StateTree를 붙이려면 전용 컨트롤러 클래스가 필요하고, 컨트롤러 액터가 몬스터 수만큼 늘어난다.
2. StateTree 에셋은 바이너리 → 생성형 AI가 텍스트로 읽고 고칠 수 없다. C++ 태스크/조건은 텍스트지만 그래프 배선은 아니다.
3. 헤드리스 픽스처에는 내비·AI 시스템이 없어 `MoveTo`·EQS 기반 태스크는 동작하지 않는다(3절). StateTree 자체의 실행 가능 여부는 미확인.
4. TwinStick 방식은 `GetPlayerPawn(0)` 같은 "플레이어 컨트롤러 존재"를 전제한다(`TwinStickStateTreeUtility.cpp:18`). 헤드리스에는 플레이어 컨트롤러가 없으므로 대상 선택은 팀/전투 컴포넌트 등록 집합(`UTDDamageSubsystem::Combatants`) 기반이어야 한다.

## 프로젝트 적용 시사점

무엇을 쓸지:
- 시뮬 월드는 지금의 `FTDScopedCombatWorld` 방식을 그대로 쓴다. 이미 28개 테스트가 GAS·피해·상태이상·투사체·근접 스윕을 이 월드에서 검증한다. 렌더·게임모드·내비 없는 월드에서 전투 규칙이 성립한다는 것이 확인되었다.
- 전투원은 `ACharacter`가 아니라 "루트 셰이프 + `UTDCombatComponent`" 액터로 만든다(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:49-69`). 몬스터 500마리 시뮬에 스켈레탈 메시·CMC·AIController를 붙일 이유가 없다. 이동은 시뮬 매니저가 `SetActorLocation`으로 적분한다(엔티티가 이미 `GetActorLocation` 기준으로 판정).
- 플레이어 조건은 `FTDCombatStats`(+GE 세트)로 명세하고 `SetStats`로 주입한다. 시전 스냅샷 구조 덕분에 "조건 = 스탯 벡터 + 스킬 목록 + 사용 정책"으로 닫힌다.
- 데미지 정의는 코드 생성(`CreateExamples`) 또는 에셋 로드 둘 다 가능하되, 시뮬 입력 기록에는 정의의 값 전체를 직렬화해서 남긴다(에셋 편집과 무관하게 재현).
- 몬스터 AI 실행기는 "매니저가 순서대로 수동 틱" 구조로 만든다. StateTree를 채택하더라도 `FStateTreeExecutionContext`를 매니저가 직접 틱하면 엔진 틱 순서·프레임 결합 문제가 사라지고 `RandomSeed`도 줄 수 있다. 판단 주기는 몬스터별 `NextThinkTime`으로 관리해 프레임 스텝과 분리한다.

무엇을 고칠지(필수, 작은 변경):
1. `FMath::FRand` 두 곳을 `FTDDamageContext`에 실린 `FRandomStream`(또는 시뮬 세션 스트림)으로 교체. 게임 실행에서는 시드를 시간으로, 시뮬에서는 입력 시드로.
2. `GatherTargets` 출력 정렬 또는 `Combatants`를 결정적 키(등록 순번)로 정렬. tie-break의 `GetUniqueID()`를 전투 컴포넌트가 가진 시뮬 순번으로 교체.
3. 시뮬 월드의 `AWorldSettings::MaxUndilatedFrameTime`을 시뮬 스텝 상한에 맞게 설정하고, 루프 종료 조건은 `World->GetTimeSeconds()` 기준으로 판단.
4. 시뮬용 결과 수집기: `OnDamaged/OnDeath` 구독 + 시전 이벤트 기록을 한 곳에 모으는 `UWorldSubsystem` 또는 매니저(현재 없음).
5. 예제 정의 `bDrawDebug` 끄기, 데미지 엔티티의 Niagara/StaticMesh 컴포넌트를 시뮬 모드에서 생성하지 않는 옵션(현재 무조건 생성).

무엇을 피할지:
- `ACharacter`+`AAIController`+`MoveTo`를 헤드리스 시뮬에 그대로 쓰는 것(내비 시스템 없음, 액터 2개/마리).
- 애니메이션 노티파이를 시뮬 근접 판정으로 쓰는 것(메시·애님 인스턴스 필수, 대량 재생 비용). 대신 노티파이 창·소켓 궤적을 미리 추출한 수치 모델을 쓰고, 실제 게임과의 일치는 별도 테스트로 확인.
- 스텝 크기를 바꾸면서 결과를 비교하는 것(호밍 적분·펄스 상한이 스텝 의존). 시뮬 스텝은 고정값 하나로 못 박고 입력 기록에 포함.
- 지속형 Health 수정자로 "보호막/추가 체력"을 표현하는 것(거부됨). 필요하면 `Shield` 속성을 추가해야 한다.

시뮬 입력·출력 명세 초안(현재 코드에서 "결과를 바꾸는 입력"을 빠짐없이 모은 것):

| 항목 | 현재 코드에서의 근거 | 명세에 넣어야 하는 이유 |
| --- | --- | --- |
| 난수 시드 | 치명타·산포 `FRand`(`Source/TDGame/Combat/TDCombatComponent.cpp:220`, `Source/TDGame/Combat/TDDamageSubsystem.cpp:203-204`) | 교체 후에도 시드가 입력이다 |
| 고정 스텝 크기 | 호밍 적분(`Source/TDGame/Combat/TDDamageEntity.cpp:330`), 펄스 상한(`107`), 상태 펄스 상한(`Source/TDGame/Combat/TDCombatComponentStatus.cpp:240`) | 스텝이 다르면 결과가 다르다 |
| 스폰 순서(전투원 등록 순서) | `Combatants` 순회(`Source/TDGame/Combat/TDDamageSubsystem.cpp:257`) | 범위 피해 적용 순서와 Kill 연계 순서를 정한다 |
| 전투원별 `FTDCombatStats`(레벨·팀·체력·공격·주문·방어·저항·치명타) | `SetStats` 정규화(`Source/TDGame/Combat/TDCombatComponent.cpp:120-137`) | 플레이어 조건과 몬스터 종류를 모두 이 벡터로 표현 |
| 시작 GE 목록(장비/버프) | `StartupEffects`(`Source/TDGame/Combat/Characters/TDCombatCharacter.cpp:39-48`) | 스탯 벡터로 환원 가능하나 기록은 남겨야 한다 |
| 스킬 목록과 각 정의의 전체 값 | `DamageSpells`(`Source/TDGame/Combat/Characters/TDCombatCharacter.h:37-38`), 정의 필드(`Source/TDGame/Combat/TDDamageDefinition.h:18-70`) | 에셋 편집과 무관하게 재현 |
| 초기 위치·팀 배치 | `GatherTargets` 반경·높이 판정(`Source/TDGame/Combat/TDDamageSubsystem.cpp:257-268`) | 공간 판정 입력 |
| 시전 정책(누가 언제 무엇을 시전) | 현재는 테스트가 직접 호출(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:695-701`) | AI 모델이 결정하는 부분. 결정적 AI가 아니면 시드 포함 |
| 종료 조건 | 없음(테스트는 고정 시간 틱) | 팀 전멸/시간 상한을 월드 시간 기준으로 |

출력은 최소한 `OnDamaged`(피해량·치명타·시전자 문맥), `OnDeath`(시각·가해자), 시전 성공/실패(`TryCastDamageDefinition` 반환값, `Source/TDGame/Combat/TDCombatComponent.cpp:306-347`), 엔티티 생성 수(`FTDDamageChainBudget` 소비량, `Source/TDGame/Combat/TDDamageTypes.h:180-184`)를 월드 시간 태그와 함께 기록하면 승패·시간·피해 분포를 모두 계산할 수 있다.

추가 고려사항(사용자가 언급하지 않은 것):
- 몬스터 정의 DataAsset이 없다. AI 파라미터·스탯·스킬 목록을 하나의 텍스트 친화 구조(C++ 표 또는 JSON→DataAsset 생성 커맨드렛)로 두면 생성형 AI 작업과 시뮬 입력을 동시에 해결한다. 이미 커맨드렛으로 "C++ → 에셋" 흐름이 있으므로 같은 패턴을 재사용할 수 있다.
- 프레임 델타 클램프(0.4초)와 펄스 따라잡기 상한(8/64)은 "빠른 시뮬 = 큰 스텝"을 막는 요소다. 고속화는 스텝을 키우는 게 아니라 "월드당 몬스터 수 × 병렬 월드 수"로 얻어야 한다. `UWorld::CreateWorld`는 여러 개 만들 수 있으나(테스트가 매번 새 월드 생성) 게임 스레드에서 순차 틱된다. 프로세스 병렬(커맨드렛 다중 실행)이 가장 단순하다.
- ASC는 몬스터마다 매 프레임 틱을 켠다(엔진 기본). 시뮬 전투원의 ASC에 `SetComponentTickEnabled(false)`를 시험해 볼 수 있으나, 어빌리티 태스크가 틱을 요구할 수 있어 검증이 필요하다(미확인).

## 미확인·미해결 질문

1. `FTDScopedCombatWorld`에서 `UStateTreeAIComponent`가 `UAISystem` 없이 `StartLogic` 가능한지, EQS 없는 트리라면 실행되는지 — 엔진 코드로 미확인.
2. `UTDCombatComponent`(ASC) 틱을 끈 상태에서 `UTDDamageGameplayAbility`·GE 타이머가 정상 동작하는지 — GE는 타이머 기반이라 가능성이 높지만 미검증.
3. GAS 전환 후 28개 자동화 테스트의 실제 통과 여부 — 문서상 "컴파일만 완료"(`Docs/TDGASFoundation.md:85`). 이 문서의 헤드리스 판단은 코드 경로 분석과 GAS 전환 전 통과 기록에 근거한다.
4. `MaxUndilatedFrameTime` 클램프가 근접 노티파이 테스트(0.5초 스텝) 결과에 실제로 영향을 줬는지 — 테스트 기대값이 0.4초 스텝에서도 성립하도록 쓰여 있어 통과 여부만으로는 구분 불가.
5. 같은 틱 그룹 내 액터 틱 실행 순서가 등록 순서와 정확히 일치하는지 — `FTickTaskManager` 내부 큐 구현은 이번 조사 범위 밖(콘솔 변수 기본값만 확인).
6. `TSet<TWeakObjectPtr>` 삭제 후 슬롯 재사용 순서 — 표준 `TSparseArray` 동작으로 추정하되 엔진 컨테이너 코드는 이번에 직접 확인하지 않았다.
7. 에셋 편집본 12개가 현재 C++ `CreateExamples`와 값이 같은지 — 에셋을 열지 않아 미확인(커맨드렛에 차이 검사 기능 없음).

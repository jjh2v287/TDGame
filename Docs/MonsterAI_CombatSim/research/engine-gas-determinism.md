# Gameplay Ability System 을 헤드리스·고정 스텝·결정론으로 돌릴 때의 조건

조사 대상: 언리얼 엔진 5.8 소스 (`C:/Program Files/Epic Games/UE_5.8/Engine`, 읽기 전용) 와 TDGame 프로젝트 소스 (`C:/Project/TDGame/Source/TDGame`).
조사 방법: 헤더 선언과 .cpp 구현을 직접 읽었다. 문서·기억이 아니라 코드가 근거다. 아래 경로 약어를 쓴다.

| 약어 | 실제 경로 |
|---|---|
| `GAS/` | `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/` |
| `Engine/` | `Engine/Source/Runtime/Engine/` |
| `Core/` | `Engine/Source/Runtime/Core/` |
| `GT/` | `Engine/Source/Runtime/GameplayTasks/` |
| `TD/` | `C:/Project/TDGame/Source/TDGame/` |

GAS 는 Gameplay Ability System(게임플레이 어빌리티 시스템), ASC 는 UAbilitySystemComponent(어빌리티 시스템 컴포넌트), GE 는 UGameplayEffect(게임플레이 이펙트)의 약자다.

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들)

1. **GE 의 지속시간·주기는 "월드 시간 비교"가 아니라 `FTimerManager` 타이머로 구동된다.** 적용 시 `Owner->GetWorld()->GetTimerManager().SetTimer(...)` 로 만료 콜백(`CheckDurationExpired`)과 주기 콜백(`ExecutePeriodicEffect`)을 등록한다 (`GAS/Private/GameplayEffect.cpp:4483-4507`). 만료 콜백 안에서 다시 `GetWorldTime()`(=`World->GetTimeSeconds()`)과 `StartWorldTime + Duration` 을 비교해 최종 판정한다 (`GAS/Private/GameplayEffect.cpp:5400-5408, 5363-5367`). 따라서 **고정 스텝 시뮬레이션은 `World->Tick()` 만 돌리면 되고, 별도의 수동 틱은 필요 없다.** 단, 타이머 매니저는 `GFrameCounter` 가 바뀌지 않으면 한 프레임에 한 번만 돈다.
2. **`FTimerManager::Tick` 은 `LastTickedFrame == GFrameCounter` 이면 즉시 반환한다** (`Engine/Public/TimerManager.h:466-469`, `Engine/Private/TimerManager.cpp:1136-1139`). 헤드리스 루프에서 `World->Tick()` 을 여러 번 부르려면 매 스텝 `++GFrameCounter` 가 **필수**다. 엔진 자체 GAS 테스트도 이렇게 한다 (`GAS/Private/Tests/GameplayEffectTests.cpp:761-772`). TDGame 픽스처 `FTDScopedCombatWorld::Tick` 은 이미 `++GFrameCounter; World->Tick(LEVELTICK_All, Delta);` 를 한다 (`TD/Combat/Tests/TDDamageSystemTests.cpp:71-80`). **이 규칙을 밸런스 툴 루프에도 반드시 복제해야 한다.**
3. **타이머 만료는 "누적 시간이 만료 시각을 엄격히 초과한 첫 스텝"에 발생한다** (`InternalTime > Top->ExpireTime`, `Engine/Private/TimerManager.cpp:1212`). 스텝 델타는 `float`, 내부 시간은 `double` 이므로 0.02f 같은 델타를 50번 더해도 1.0 에 못 미쳐 1.0 초 지속 효과는 51번째 스텝(게임 시간 1.02 초)에 만료된다. **결정론적이지만 한 스텝 늦다.** 같은 입력이면 항상 같은 스텝에서 만료되므로 재현성 자체는 깨지지 않는다.
4. **ASC 는 `TickComponent` 를 가지며 기본 틱 활성 상태로 시작하지만**(`GAS/Private/AbilitySystemComponent.cpp:58-61`), 틱 본문은 몽타주 복제 데이터 갱신·틱 태스크·틱 가능한 AttributeSet 만 처리한다 (`GAS/Private/AbilitySystemComponent_Abilities.cpp:139-160`). **GE 지속시간·쿨다운·속성 계산은 틱에 의존하지 않는다.** `GetShouldTick()` 은 몽타주 재생 중이거나 틱 태스크가 있거나 `ITickableAttributeSetInterface::ShouldTick()` 이 참일 때만 참을 돌려주며(`GAS/Private/AbilitySystemComponent_Abilities.cpp:223-247`), `UGameplayTasksComponent::UpdateShouldTick()` 이 `SetActive(false)` 로 틱을 꺼준다 (`GT/Private/GameplayTasksComponent.cpp:323-330`). 즉 몽타주와 틱 태스크를 쓰지 않으면 ASC 수백 개가 있어도 틱 비용은 "빈 TickComponent 호출" 수준이다. 완전히 없애려면 `PrimaryComponentTick.bCanEverTick=false` 를 서브클래스 생성자에서 설정한다(단, 틱 태스크·몽타주 복제·틱 가능 AttributeSet 을 포기해야 함).
5. **어빌리티 태스크 중 타이머를 쓰는 것은 4개(`WaitDelay`, `Repeat`, `WaitAttributeChangeRatioThreshold`, `VisualizeTargeting`), 애니메이션에 의존하는 것은 2개(`PlayMontageAndWait`, `PlayAnimAndWait`), 틱 태스크는 3개(`ApplyRootMotion_Base`, `MoveToLocation`, `WaitVelocityChange`)다.** `UAbilityTask_WaitDelay::Activate` 는 `World->GetTimerManager().SetTimer(...)` 를 쓰므로 고정 스텝에서 결정론적으로 동작한다 (`GAS/Private/Abilities/Tasks/AbilityTask_WaitDelay.cpp:26-41`). `PlayMontageAndWait` 는 `ActorInfo->GetAnimInstance()` 가 nullptr 이면 아무것도 재생하지 않고 실패 경로로 간다 (`GAS/Private/Abilities/Tasks/AbilityTask_PlayMontageAndWait.cpp:123-138`). **헤드리스 시뮬레이션에서는 몽타주 태스크를 쓰지 말고, "공격 판정 시각"을 `WaitDelay` 또는 자체 고정 스텝 스케줄러로 표현해야 한다.** TDGame 의 `UTDDamageGameplayAbility` 는 이미 태스크·몽타주 없이 동기적으로 활성화→커밋→종료한다 (`TD/Combat/GAS/TDDamageGameplayAbility.cpp:90-149`).
6. **Standalone(싱글, 리슨 서버 아님)에서는 예측 키·RPC 비용이 사실상 없다.** 액터는 `SpawnActor` 시 `ROLE_Authority` 를 받고(`Engine/Private/Actor.cpp:287`), `IsNetSimulating() = GetIsReplicated() && GetOwnerRole() != ROLE_Authority` 는 거짓이 되어(`Engine/Classes/Components/ActorComponent.h:1512-1515`) ASC 는 항상 권한자(`IsOwnerActorAuthoritative()`, `GAS/Private/AbilitySystemComponent.cpp:2115-2118`)로 동작한다. 클라이언트 예측 키는 권한자에서 생성되지 않으며(`GAS/Private/GameplayPrediction.cpp:223-231`), 어빌리티 활성화는 `LocalOnly || NetMode==ROLE_Authority` 분기에서 **동기 호출**로 `CallActivateAbility` 된다 (`GAS/Private/AbilitySystemComponent_Abilities.cpp:1872-1923`). 서버 측 활성화 키(`CreateNewServerInitiatedKey`, 전역 static 카운터 `GServerKey++`, `GAS/Private/GameplayPrediction.cpp:235-247`)만 생성되는데 이는 정수 증가 하나이며 게임 결과에 영향을 주지 않는다. `ReplicationMode` 는 기본 `Full` 이지만(`GAS/Private/AbilitySystemComponent.cpp:78`) 복제가 꺼져 있으면 의미가 없다. TDGame 은 이미 `SetIsReplicatedByDefault(false)` 를 호출한다 (`TD/Combat/TDCombatComponent.cpp:42-46`).
7. **GAS 내부 난수는 딱 한 곳, `UChanceToApplyGameplayEffectComponent::CanGameplayEffectApply` 의 `FMath::FRand()` 다** (`GAS/Private/GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.cpp:23-35`). `FMath::FRand()` 는 C 표준 `rand()` 기반이며 `FMath::RandInit(Seed)` 로 시드를 잡을 수 있다 (`Core/Public/GenericPlatform/GenericPlatformMath.h:608-617`). 그러나 전역 `rand()` 는 엔진의 다른 코드(파티클, AI 등)도 공유하므로 **결정론이 필요하면 확률 판정을 GAS 컴포넌트에 맡기지 말고, 시드 제어 가능한 `FRandomStream` 을 시뮬레이션 컨텍스트가 소유하고 `UCustomCanApplyGameplayEffectComponent` 또는 어빌리티 코드에서 직접 판정**해야 한다. TDGame 의 치명타 판정(`TD/Combat/TDCombatComponent.cpp:220`)과 산탄 위치(`TD/Combat/TDDamageSubsystem.cpp:203-204`)도 `FMath::FRand()` 를 쓰므로 같은 이유로 교체 대상이다.
8. **GE 정의를 C++ 서브클래스 CDO(Class Default Object, 클래스 기본 객체)로 만드는 방식은 엔진이 공식 지원한다.** `UGameplayEffect::CanApply` 는 `GEComponents` 배열만 순회하고(`GAS/Private/GameplayEffect.cpp:957-968`), 생성자에서 `CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>` 후 `GEComponents.Add` 하는 TDGame 방식(`TD/Combat/GAS/TDCombatGameplayEffects.cpp:7-22`)은 헤더 주석이 안내하는 `AddComponent/FindOrAddComponent` 경로와 동일하다 (`GAS/Public/GameplayEffect.h:2191, 2215, 2509`). 단 `ConvertXxxComponent()` 업그레이드 코드는 `PostCDOCompiled`(블루프린트 컴파일 경로)에서만 돌기 때문에(`GAS/Private/GameplayEffect.cpp:484-500`) **C++ 생성자에서는 deprecated 멤버(`ChanceToApplyToTarget_DEPRECATED`, `StackingType` 직접 대입 등)에 의존하지 말고 컴포넌트를 직접 추가**해야 한다.
9. **다수 ASC 의 비용은 "틱"이 아니라 "메모리와 타이머 개수"에서 나온다.** ASC 하나에는 `FActiveGameplayEffectsContainer`, `FGameplayAbilitySpecContainer`, 두 개의 `FGameplayTagCountContainer`, 두 개의 `FActiveGameplayCueContainer`, 복제용 맵들, `TSharedPtr<FGameplayAbilityActorInfo>` 등이 들어 있다 (`GAS/Public/AbilitySystemComponent.h:1668-1979` 멤버 목록). 활성 GE 하나는 `FGameplayEffectSpec` 전체(태그 집계 4개, 캡처 컨테이너, `TMap` 2개, `FGameplayEffectContextHandle`)를 값으로 내장한다 (`GAS/Public/GameplayEffect.h` 의 `FActiveGameplayEffect` 멤버 `Spec`). 지속 GE 마다 타이머 1~2개가 힙에 들어간다. **정확한 바이트 수는 미확인(실측 필요)**이나, 구조상 몬스터 300마리 × 지속 버프 3개면 활성 타이머 약 900~1800개가 한 힙에서 관리된다. 이는 `FTimerManager` 힙(O(log n))이 감당하는 수준이지만, 수천 마리 이상이면 경량 구조체 대안이 유리하다.
10. **헤드리스 커맨드렛 월드에서 ASC 초기화 조건**: `UAbilitySystemGlobals` 는 모듈이 첫 요청 시 자동으로 `InitGlobalData()` 를 호출하므로(`GAS/Private/GameplayAbilitiesModule.cpp:26-39`) 별도 초기화 호출이 필요 없다. ASC 는 `OnRegister` 에서 `AbilityActorInfo` 를 할당하고 `CacheIsNetSimulated()` 를 하며(`GAS/Private/AbilitySystemComponent.cpp:192-203`), `InitializeComponent` 에서 `InitAbilityActorInfo(Owner, Owner)` 를 자동 호출한다 (`GAS/Private/AbilitySystemComponent_Abilities.cpp:78-84`). `bWantsInitializeComponent = true` 이므로(`GAS/Private/AbilitySystemComponent.cpp:56`) `RegisterComponent()` 만으로 초기화가 진행된다. 게임플레이 큐는 `AvatarActor == nullptr || bSuppressGameplayCues` 이면 즉시 반환하므로(`GAS/Private/AbilitySystemComponent.cpp:1442-1452`) **시뮬레이션용 ASC 는 `bSuppressGameplayCues = true` 로 두면 큐 비용이 0** 이다. 월드에 `GameState` 가 없으면 `GetServerWorldTime()` 도 `World->GetTimeSeconds()` 로 떨어져(`GAS/Private/GameplayEffect.cpp:5351-5361`) 두 시간축이 일치한다.

---

## 상세 조사

### 1) ASC 의 틱, 활성 GE 의 지속시간·주기 처리, 만료 시점의 프레임 경계

#### 근거 표

| 사실 | 근거 (경로:줄) | 시그니처 / 발췌 |
|---|---|---|
| ASC 는 TickComponent 를 오버라이드한다 | `GAS/Public/AbilitySystemComponent.h:1638` | `virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;` |
| 생성자에서 틱을 켜고 AutoActivate 를 강제한다 | `GAS/Private/AbilitySystemComponent.cpp:58-61` | `PrimaryComponentTick.bStartWithTickEnabled = true; // FIXME! ... bAutoActivate = true;` |
| 틱 본문: 몽타주 복제 갱신 → 부모(태스크 틱) → 틱 가능 AttributeSet | `GAS/Private/AbilitySystemComponent_Abilities.cpp:139-160` | 아래 발췌 |
| 틱 필요 조건 | `GAS/Private/AbilitySystemComponent_Abilities.cpp:223-247` | `bool UAbilitySystemComponent::GetShouldTick() const` |
| 태스크 컴포넌트가 틱 필요 여부로 SetActive 를 토글 | `GT/Private/GameplayTasksComponent.cpp:309-330` | `bool UGameplayTasksComponent::GetShouldTick() const { return TickingTasks.Num() > 0; }` |
| 컴포넌트 기본 틱 그룹 | `Engine/Private/Components/ActorComponent.cpp:548` | `PrimaryComponentTick.TickGroup = TG_DuringPhysics;` |
| GE 지속시간 타이머 등록 | `GAS/Private/GameplayEffect.cpp:4481-4492` | `TimerManager.SetTimer(AppliedActiveGE->DurationHandle, Delegate, FinalDuration, false);` |
| GE 주기 타이머 등록(루프) + 적용 즉시 1회 실행 | `GAS/Private/GameplayEffect.cpp:4496-4508` | `TimerManager.SetTimer(AppliedActiveGE->PeriodHandle, Delegate, AppliedEffectSpec.GetPeriod(), true);` |
| 만료 판정은 월드 시간으로 재확인 | `GAS/Private/GameplayEffect.cpp:5369-5408` | `void FActiveGameplayEffectsContainer::CheckDuration(FActiveGameplayEffectHandle Handle)` |
| 월드 시간 정의 | `GAS/Private/GameplayEffect.cpp:5351-5367` | `float FActiveGameplayEffectsContainer::GetWorldTime() const { return World->GetTimeSeconds(); }` |
| 월드 시간은 double, GE 시작 시각은 float | `Engine/Classes/Engine/World.h:2010, 2847` / `GAS/Public/GameplayEffect.h` FActiveGameplayEffect | `double TimeSeconds;` vs `float StartWorldTime = 0.0f;` |
| 타이머 매니저 틱은 월드 틱 안, 물리 후·PostUpdateWork 전 | `Engine/Private/LevelTick.cpp:1778, 1812-1817, 1877` | `RunTickGroup(TG_PostPhysics); ... GetTimerManager().Tick(DeltaSeconds); ... RunTickGroup(TG_PostUpdateWork);` |
| 타이머 매니저 프레임 게이트 | `Engine/Public/TimerManager.h:466-469`, `Engine/Private/TimerManager.cpp:1136-1139` | `bool HasBeenTickedThisFrame() const { return (LastTickedFrame == GFrameCounter); }` |
| 만료 비교는 엄격 초과 | `Engine/Private/TimerManager.cpp:1212` | `if (InternalTime > Top->ExpireTime)` |
| 루프 타이머는 지연 누적 없이 `ExpireTime += CallCount * Rate` | `Engine/Private/TimerManager.cpp:1345-1351` | `Top->ExpireTime += CallCount * Top->Rate;` |
| 큰 델타면 한 틱에 여러 번 호출 | `Engine/Private/TimerManager.cpp:1236-1238` | `CallCount = bLoop ? TruncToInt((InternalTime - ExpireTime) / Rate) + 1 : 1;` |
| 힙 정렬 키는 ExpireTime 만 | `Engine/Private/TimerManager.cpp:327-336` | `return LhsData.ExpireTime < RhsData.ExpireTime;` |
| 틱 전에 등록된 타이머는 Pending 으로 갔다가 틱 끝에 활성화 | `Engine/Private/TimerManager.cpp:711-726, 1376-1390` | `NewTimerData.Status = ETimerStatus::Pending; PendingTimerSet.Add(...)` |
| GameInstance 가 없으면 월드 자체 타이머 매니저 | `Engine/Private/World.cpp:866, 8056-8059` | `TimerManager = new FTimerManager();` / `return (OwningGameInstance ? OwningGameInstance->GetTimerManager() : *TimerManager);` |
| 엔진 GAS 테스트가 GFrameCounter 를 수동 증가 | `GAS/Private/Tests/GameplayEffectTests.cpp:761-772` | `World->Tick(ELevelTick::LEVELTICK_All, ...); GFrameCounter++;` |

#### 발췌: ASC 틱 본문 (`GAS/Private/AbilitySystemComponent_Abilities.cpp:139-160`)

```cpp
void UAbilitySystemComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (IsOwnerActorAuthoritative()) { AnimMontage_UpdateReplicatedData(); }
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);   // UGameplayTasksComponent: TickingTasks 만 순회
	for (UAttributeSet* AttributeSet : GetSpawnedAttributes())
	{
		ITickableAttributeSetInterface* TickableSet = Cast<ITickableAttributeSetInterface>(AttributeSet);
		if (TickableSet && TickableSet->ShouldTick()) { TickableSet->Tick(DeltaTime); }
	}
}
```

#### 발췌: 만료 재판정 (`GAS/Private/GameplayEffect.cpp:5400-5408`)

```cpp
float CurrentTime = GetWorldTime();
if (((Effect.StartWorldTime + Duration) < CurrentTime) || FMath::IsNearlyZero(CurrentTime - Duration - Effect.StartWorldTime, KINDA_SMALL_NUMBER))
{
	switch(Effect.Spec.Def->GetStackExpirationPolicy()) { /* ClearEntireStack / RemoveSingleStackAndRefreshDuration / RefreshDuration */ }
}
else
{
	RefreshDurationTimer = true;   // 아직 안 끝났으면 (StartWorldTime + Duration) - CurrentTime 으로 타이머 재등록
}
```

#### 해석

- **한 프레임 안의 실행 순서**: 컴포넌트 틱(TG_DuringPhysics 등) → `FTimerManager::Tick`(만료 GE 제거, 주기 GE 실행) → `TG_PostUpdateWork`. 즉 **같은 프레임에서 어빌리티가 먼저 실행되고, 그 뒤에 도트(주기 피해)와 버프 만료가 처리된다.** 시뮬레이션 로그의 시각 해석 시 이 순서를 고정 규칙으로 문서화해야 한다.
- **주기 실행 순서**: 같은 ExpireTime 의 타이머 사이에는 정렬 기준이 없고(`ExpireTime <` 만 비교), 힙 연산은 결정론적이므로 "삽입 순서와 힙 구조"에 의해 정해진다. 같은 입력·같은 적용 순서면 같은 결과가 나오지만, **적용 순서(예: 몬스터 스폰 순서, 타깃 배열 순서)가 바뀌면 같은 틱 안의 도트 실행 순서가 바뀔 수 있다.** 밸런스 툴은 적용 순서를 고정(팀·ID 정렬)해야 한다.
- **주기 첫 실행**: `bExecutePeriodicEffectOnApplication`(기본 true, `GAS/Private/GameplayEffect.cpp:187`) 이면 `SetTimerForNextTick` 으로 다음 타이머 틱에 1회 실행되고, 이후 `Period` 마다 실행된다. 즉 적용 프레임이 아니라 **다음 타이머 틱**에 첫 도트가 들어간다.
- **만료 프레임 경계**: `SetTimer` 는 이미 틱된 프레임이면 `ExpireTime = InternalTime + Duration` 으로 활성 힙에 바로 넣고, 아니면 Pending 으로 두었다가 틱 끝에 `ExpireTime += InternalTime` 으로 활성화한다. 어느 쪽이든 발화 조건은 `InternalTime > ExpireTime`(엄격). float 델타 0.02f 는 double 로 0.019999999552... 이므로 50스텝 합은 0.99999998 < 1.0 → 51번째 스텝에서 발화. `CheckDuration` 은 이때 `StartWorldTime + 1.0 < 1.02` 로 만료를 확정한다. **결과: "N 초 지속" 은 헤드리스 고정 스텝에서 N 초 + 1스텝에 만료된다.** 델타를 정확히 표현 가능한 이진 소수(예: 1/64 = 0.015625, 1/32 = 0.03125)로 잡으면 누적 오차가 사라져 정확히 N 초 직후 스텝에 만료된다(그래도 엄격 초과 조건 때문에 만료 시각과 같은 스텝이 아니라 그 다음 스텝).
- **long-run 정밀도**: `StartWorldTime` 이 float 이므로 월드 시간이 수만 초를 넘으면 0.001 초 단위 분해능이 무너진다. 밸런스 시뮬레이션은 **매 시나리오마다 새 월드(시간 0부터)** 를 쓰는 것이 안전하다. TDGame 픽스처가 이미 그렇게 한다.

### 2) 어빌리티 태스크의 타이머·애니메이션 의존도, 몽타주 없이 진행하는 방법

#### 근거 표

| 사실 | 근거 (경로:줄) | 시그니처 / 발췌 |
|---|---|---|
| 타이머 사용 태스크 4개 | `GAS/Private/Abilities/Tasks/` 에서 `GetTimerManager|SetTimer` 검색 결과 | `AbilityTask_Repeat.cpp`, `AbilityTask_VisualizeTargeting.cpp`, `AbilityTask_WaitAttributeChangeRatioThreshold.cpp`, `AbilityTask_WaitDelay.cpp` |
| 애니메이션 의존 태스크 2개 | 같은 폴더 `AnimInstance|Montage_` 검색 | `AbilityTask_PlayAnimAndWait.cpp`, `AbilityTask_PlayMontageAndWait.cpp` |
| 틱 태스크 3개 (`bTickingTask = true`) | 같은 폴더 검색 | `AbilityTask_ApplyRootMotion_Base.cpp`, `AbilityTask_MoveToLocation.cpp`, `AbilityTask_WaitVelocityChange.cpp` |
| WaitDelay 는 월드 타이머로 구현 | `GAS/Private/Abilities/Tasks/AbilityTask_WaitDelay.cpp:26-41` | `void UAbilityTask_WaitDelay::Activate()` → `World->GetTimerManager().SetTimer(TimerHandle, this, &UAbilityTask_WaitDelay::OnTimeFinish, Time, false);` |
| 몽타주 태스크는 AnimInstance 없으면 재생 실패 | `GAS/Private/Abilities/Tasks/AbilityTask_PlayMontageAndWait.cpp:123-138` | `UAnimInstance* AnimInstance = ActorInfo->GetAnimInstance(); if (AnimInstance != nullptr) { if (ASC->PlayMontage(...) > 0.f) ...` |
| 태스크 틱은 GameplayTasksComponent 가 수행 | `GT/Private/GameplayTasksComponent.cpp:258-307` | `void UGameplayTasksComponent::TickComponent(...)` → `TickingTask->TickTask(DeltaTime);` |
| 태스크 전역 개수 상한 콘솔 변수 | `GAS/Private/Abilities/Tasks/AbilityTask.cpp:33-38` | `AbilitySystem.AbilityTask.MaxCount` 기본 1000 |
| EndAbility 가 어빌리티 객체의 타이머를 모두 지움 | `GAS/Private/Abilities/GameplayAbility.cpp:57-58, 832-834` | `AbilitySystem.ClearAbilityTimers` (기본 1) → `MyWorld->GetTimerManager().ClearAllTimersForObject(this);` |
| LocalOnly / 권한자 활성화는 동기 호출 | `GAS/Private/AbilitySystemComponent_Abilities.cpp:1872-1923` | `if (Ability->GetNetExecutionPolicy() == LocalOnly \|\| (NetMode == ROLE_Authority)) { ... AbilitySource->CallActivateAbility(Handle, ActorInfo, ActivationInfo, OnGameplayAbilityEndedDelegate, TriggerEventData); }` |
| TDGame 어빌리티는 태스크 없이 동기 완료 | `TD/Combat/GAS/TDDamageGameplayAbility.cpp:14-15, 90-149` | `InstancingPolicy = InstancedPerActor; NetExecutionPolicy = LocalOnly;` … `CommitAbility` → 피해 실행 → `EndAbility` |
| NonInstanced 는 5.5 부터 deprecated | `GAS/Public/Abilities/GameplayAbilityTypes.h:46-50` | `NonInstanced UE_DEPRECATED_FORGAME(5.5, ...)`, `InstancedPerActor UMETA(...)` |

#### 해석

- **타이머 기반 태스크(`WaitDelay`, `Repeat`)는 고정 스텝에서 그대로 결정론적**이다. 발화 시점은 1) 절과 같은 "엄격 초과" 규칙을 따른다.
- **몽타주 없이 어빌리티를 진행하는 세 가지 방법**
  1. TDGame 방식: `ActivateAbility` 안에서 `CommitAbility` → 즉시 피해/효과 실행 → `EndAbility`. 선딜(wind-up)이 없는 즉발 공격에 적합. 이미 구현됨.
  2. 선딜이 있는 공격: `UAbilityTask_WaitDelay::WaitDelay(this, WindUpSeconds)` 후 `OnFinish` 에서 피해 실행. 태스크 개수 상한(1000) 안에서 수백 마리가 동시에 써도 문제없으나, 태스크 UObject 생성·GC 비용이 매 공격마다 발생한다.
  3. 태스크 없는 대안: 어빌리티가 "판정 시각"만 계산해 프로젝트 소유의 고정 스텝 스케줄러(예: `UTDDamageSubsystem` 에 정렬된 이벤트 큐)에 넣고, 스케줄러가 `World->Tick` 에서 시각 순·삽입 순으로 실행. **UObject 생성이 없고 순서가 명시적이라 결정론 검증과 대량 처리에 가장 유리하다.** 애니메이션 노티파이(실제 게임)와 스케줄러(시뮬레이션)가 같은 "판정 시각 함수"를 공유하면 두 경로의 결과가 일치한다.
- **서버 전용 흐름**: Standalone 에서는 `NetExecutionPolicy` 가 `ServerOnly` 든 `LocalOnly` 든 `NetMode == ROLE_Authority` 분기로 동일하게 동기 실행된다. `ServerInitiated/ServerOnly` 이면 서버 키를 새로 만들지만 (`bCreateNewServerKey`, 1874-1882) 결과에는 영향 없다.
- **틱 태스크(루트 모션·이동)** 는 캐릭터 무브먼트 컴포넌트에 의존하므로 시뮬레이션에서 배제한다. 이동은 시뮬레이션 자체 모델(격자/거리 스칼라)로 대체하는 것이 결정론과 속도에 유리하다.

### 3) 예측 키·네트워크 코드의 Standalone 비용, ReplicationMode, 헤드리스 ASC 초기화

#### 근거 표

| 사실 | 근거 (경로:줄) | 시그니처 / 발췌 |
|---|---|---|
| 스폰 액터는 권한자 | `Engine/Private/Actor.cpp:287` | `SetRole(ROLE_Authority);` |
| 넷 시뮬레이션 판정 | `Engine/Classes/Components/ActorComponent.h:1512-1515` | `inline bool UActorComponent::IsNetSimulating() const { return GetIsReplicated() && GetOwnerRole() != ROLE_Authority; }` |
| ASC 는 캐시된 값으로 권한 판정 | `GAS/Private/AbilitySystemComponent.cpp:355-358, 2115-2118` | `void CacheIsNetSimulated() { bCachedIsNetSimulated = IsNetSimulating(); ...}` / `bool IsOwnerActorAuthoritative() const { return !bCachedIsNetSimulated; }` |
| GE 적용 권한 검사 | `GAS/Private/AbilitySystemComponent.cpp:455-458, 1015-1030` | `bool HasNetworkAuthorityToApplyGameplayEffect(FPredictionKey PredictionKey) const { return (IsOwnerActorAuthoritative() \|\| PredictionKey.IsValidForMorePrediction()); }` |
| 권한자에서는 클라이언트 예측 키를 만들지 않음 | `GAS/Private/GameplayPrediction.cpp:223-231` | `if(OwningComponent->GetOwnerRole() != ROLE_Authority) { NewKey.GenerateNewPredictionKey(); }` |
| 서버 활성화 키는 전역 static 카운터 | `GAS/Private/GameplayPrediction.cpp:235-247` | `static KeyType GServerKey = 1; NewKey.bIsServerInitiated = true; NewKey.Current = GServerKey++;` |
| 예측 창은 권한자에서 키를 바꿔 끼우는 것뿐 | `GAS/Private/GameplayPrediction.cpp:387-407` | `FScopedPredictionWindow::FScopedPredictionWindow(UAbilitySystemComponent*, FPredictionKey, bool)` → `AbilitySystemComponent->ScopedPredictionKey = InPredictionKey;` |
| 로컬 제어 판정: PlayerController 없고 Pawn 아니면 권한자면 참 | `GAS/Private/GameplayAbilityTypes.cpp:107-127, 139-151` | `bool FGameplayAbilityActorInfo::IsLocallyControlled() const { ... return IsNetAuthority(); }` |
| 서버 RPC 는 LocalPredicted 분기에서만 | `GAS/Private/AbilitySystemComponent_Abilities.cpp:1925-1941` | `else if (NetExecutionPolicy == LocalPredicted) { ... CallServerTryActivateAbility(...) }` |
| ReplicationMode 기본값 Full | `GAS/Private/AbilitySystemComponent.cpp:78` | `ReplicationMode = EGameplayEffectReplicationMode::Full;` |
| ReplicationMode 열거형 | `GAS/Public/AbilitySystemComponent.h:81-89` | `enum class EGameplayEffectReplicationMode : uint8 { Minimal, Mixed, Full };` |
| SetReplicationMode 는 복제 조건만 갱신 | `GAS/Private/AbilitySystemComponent.cpp:1982-1990` | `void SetReplicationMode(EGameplayEffectReplicationMode NewReplicationMode)` → `UpdateActiveGameplayEffectsReplicationCondition(); UpdateMinimalReplicationGameplayCuesCondition();` |
| 컴포넌트 등록 시 ActorInfo 할당 + 넷 캐시 | `GAS/Private/AbilitySystemComponent.cpp:192-203` | `void UAbilitySystemComponent::OnRegister()` → `AbilityActorInfo = TSharedPtr<FGameplayAbilityActorInfo>(UAbilitySystemGlobals::Get().AllocAbilityActorInfo()); CacheIsNetSimulated();` |
| InitializeComponent 가 InitAbilityActorInfo 자동 호출 + Outer 의 AttributeSet 수집 | `GAS/Private/AbilitySystemComponent_Abilities.cpp:78-108` | `InitAbilityActorInfo(Owner, Owner); ... GetObjectsWithOuter(Owner, ChildObjects, ...); SpawnedAttributes.AddUnique(Set);` |
| InitAbilityActorInfo 본문 | `GAS/Private/AbilitySystemComponent_Abilities.cpp:161-222` | `void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)` → `AbilityActorInfo->InitFromActor(...)`, `HandleDeferredGameplayCues`, `OnAvatarSet`, TagResponseTable 등록 |
| 전역 데이터는 첫 요청 시 자동 초기화 | `GAS/Private/GameplayAbilitiesModule.cpp:26-39` | `AbilitySystemGlobals = NewObject<UAbilitySystemGlobals>(...); AddToRoot(); AbilitySystemGlobals->InitGlobalData();` |
| InitGlobalData 가 큐 매니저를 만든다 | `GAS/Private/AbilitySystemGlobals.cpp:63-85, 612-641` | `GetGameplayCueManager();` → 설정된 이름/클래스 로드, 없으면 `NewObject<UGameplayCueManager>` |
| 큐 이벤트는 아바타 없거나 억제 시 즉시 반환 | `GAS/Private/AbilitySystemComponent.cpp:1442-1452, 1503-1509` | `if (ActorAvatar == nullptr \|\| bSuppressGameplayCues) { return; }` |
| 전역 큐 끄기 콘솔 변수 | `GAS/Private/GameplayCueManager.cpp:44-45, 178-183` | `AbilitySystem.DisableGameplayCues` → `bool UGameplayCueManager::ShouldSuppressGameplayCues(AActor*)` |
| GameState 없으면 서버 시간 = 월드 시간 | `GAS/Private/GameplayEffect.cpp:5351-5361` | `float GetServerWorldTime() const { ... if (GameState) return GameState->GetServerWorldTimeSeconds(); return World->GetTimeSeconds(); }` |
| TDGame ASC 는 복제 끔 + 자체 InitAbilityActorInfo | `TD/Combat/TDCombatComponent.cpp:42-52` | `SetIsReplicatedByDefault(false);` … `BeginPlay(){ Super::BeginPlay(); InitAbilityActorInfo(GetOwner(), GetOwner()); ... }` |
| TDGame 픽스처의 월드 생성 절차 | `TD/Combat/Tests/TDDamageSystemTests.cpp:32-46` | `UWorld::CreateWorld(EWorldType::Game, false, ...)` → `CreateNewWorldContext` → `InitializeActorsForPlay(FURL())` → `BeginPlay()` → `SetBegunPlay(true)` |

#### 해석

- **비용**: Standalone 에서 살아남는 네트워크 관련 연산은 (a) `CacheIsNetSimulated()` 의 bool 캐시, (b) 서버 키 정수 증가, (c) `FScopedPredictionWindow` 의 멤버 대입, (d) `FFastArraySerializer::MarkItemDirty` 의 키 증가(복제 안 해도 호출됨, `GAS/Private/GameplayEffect.cpp:4509-4512`) 정도다. 모두 상수 시간이며 RPC·직렬화는 발생하지 않는다. **ReplicationMode 를 Minimal 로 바꿔도 Standalone 성능에는 의미 있는 차이가 없다**(복제 조건 갱신 함수만 호출). 복제 자체를 끄는 `SetIsReplicatedByDefault(false)` 가 핵심이며 TDGame 은 이미 그렇게 한다.
- **헤드리스 초기화 체크리스트** (현 픽스처가 충족함)
  1. `UWorld::CreateWorld(EWorldType::Game, ...)` + `GEngine->CreateNewWorldContext` (타이머 매니저는 월드 자체 소유, `World.cpp:866`).
  2. `InitializeActorsForPlay` → `BeginPlay` → `SetBegunPlay(true)`.
  3. 액터 스폰 후 `RegisterComponent()` → `OnRegister`(ActorInfo 할당, 넷 캐시) → `InitializeComponent`(ActorInfo 초기화, AttributeSet 수집) → 컴포넌트 `BeginPlay`.
  4. 매 스텝 `++GFrameCounter; World->Tick(LEVELTICK_All, Delta);` (`LEVELTICK_TimeOnly` 이면 타이머 매니저가 돌지 않음, `LevelTick.cpp:1812`).
  5. 시뮬레이션용 ASC 는 `bSuppressGameplayCues = true` 또는 콘솔 변수 `AbilitySystem.DisableGameplayCues 1`.
  6. `UAbilitySystemGlobals` 는 `UGameplayAbilitiesDeveloperSettings` 의 `AbilitySystemGlobalsClassName` 을 로드한다. 커맨드렛도 프로젝트 Config 를 읽으므로 추가 설정 불필요(현 프로젝트 설정값은 미확인·기본값 추정).
- **주의**: `IsLocallyControlled()` 는 Owner 가 `APawn` 이고 컨트롤러가 붙어 있지만 로컬이 아니면 거짓을 돌려준다. 헤드리스에서 몬스터를 `APawn` 으로 만들고 `AAIController` 를 붙이면 `IsLocalController()` 판정에 따라 LocalOnly 어빌리티가 거부될 수 있다(엔진 코드 `GameplayAbilityTypes.cpp:113-123`). **시뮬레이션에서는 컨트롤러 없는 액터 또는 컨트롤러 없는 Pawn 을 쓰거나, `NetExecutionPolicy = ServerOnly` 로 두면 이 분기를 피한다**(`ServerOnly` 는 1764-1772 의 `!bIsLocal` 검사 대상이 아니다).

### 4) 난수: GAS 내부 랜덤과 시드 제어 경로

#### 근거 표

| 사실 | 근거 (경로:줄) | 시그니처 / 발췌 |
|---|---|---|
| 런타임 GAS 에서 난수를 쓰는 곳은 확률 적용 컴포넌트 하나 | `GAS/Private` 전체 `FRand|RandRange|FMath::Rand|FRandomStream` 검색 | 결과: `ChanceToApplyGameplayEffectComponent.cpp:29`, 그 외는 테스트 전용 AttributeSet(`AbilitySystemTestAttributeSet.cpp:47,66`), 큐 재생 확률(`GameplayCueNotifyTypes.cpp:165`), 태그 쿼리 테스트 |
| 확률 판정 코드 | `GAS/Private/GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.cpp:23-35` | 아래 발췌 |
| CanApply 는 컴포넌트 순회 | `GAS/Private/GameplayEffect.cpp:957-968` | `bool UGameplayEffect::CanApply(const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&) const` |
| 커스텀 판정 훅 | `GAS/Private/GameplayEffect.cpp:565-575` | `UCustomCanApplyGameplayEffectComponent& ApplicationComponent = FindOrAddComponent<UCustomCanApplyGameplayEffectComponent>();` |
| ASC 단위 적용 쿼리 훅 | `GAS/Public/AbilitySystemComponent.h:560` | `TArray<FGameplayEffectApplicationQuery> GameplayEffectApplicationQueries;` (TDGame 이 이미 사용: `TD/Combat/TDCombatComponent.cpp:45`) |
| FRand 는 C rand() 기반, RandInit 로 시드 | `Core/Public/GenericPlatform/GenericPlatformMath.h:608-617` | `static void RandInit(int32 Seed) { srand( Seed ); }` / `static inline float FRand() { return (Rand() & RandMax) / (float)RandMax; }` |
| deprecated 필드는 CDO 컴파일 경로에서만 컴포넌트로 변환 | `GAS/Private/GameplayEffect.cpp:484-500, 579-598` | `void UGameplayEffect::PostCDOCompiled(...)` → `ConvertChanceToApplyComponent();` |
| TDGame 의 난수 사용처 | `TD/Combat/TDCombatComponent.cpp:220`, `TD/Combat/TDDamageSubsystem.cpp:203-204` | 치명타 `FMath::FRand() < Chance`, 산탄 각도·거리 |

#### 발췌 (`ChanceToApplyGameplayEffectComponent.cpp:23-35`)

```cpp
bool UChanceToApplyGameplayEffectComponent::CanGameplayEffectApply(const FActiveGameplayEffectsContainer& ActiveGEContainer, const FGameplayEffectSpec& GESpec) const
{
	const float CalculatedChanceToApplyToTarget = ChanceToApplyToTarget.GetValueAtLevel(GESpec.GetLevel(), &ContextString);
	if ((CalculatedChanceToApplyToTarget < 1.f - SMALL_NUMBER) && (FMath::FRand() > CalculatedChanceToApplyToTarget))
	{
		return false;
	}
	return true;
}
```

#### 해석

- `FMath::RandInit(Seed)` 로 전역 `rand()` 를 시드하면 `ChanceToApply` 도 재현되지만, 전역 스트림은 엔진 어디서든 소비될 수 있어(`FRand` 를 쓰는 다른 엔진 코드가 같은 프레임에 끼어들면 순서가 어긋남) **시뮬레이션 결정론의 근거로 삼기에는 취약**하다.
- 권장 경로: 시뮬레이션 컨텍스트(예: `FTDCombatSimContext`)가 `FRandomStream` 을 소유하고, 확률이 필요한 판정을 다음 셋 중 하나로 옮긴다.
  1. `UCustomCanApplyGameplayEffectComponent` 의 계산 클래스에서 `FGameplayEffectSpec` 컨텍스트(예: 커스텀 `FGameplayEffectContext` 서브클래스에 스트림 포인터 또는 "이번 적용의 굴림값"을 실어 보냄)를 읽어 판정.
  2. ASC 의 `GameplayEffectApplicationQueries` 에서 판정(TDGame 이 이미 Health 지속 모디파이어 거부에 쓰는 훅).
  3. 가장 단순한 방법: **어빌리티/피해 코드에서 굴림을 먼저 하고, 성공했을 때만 GE 를 적용**. GE 는 확률 컴포넌트를 갖지 않는다. 이 방식이 "굴림 순서 = 코드 순서"라서 로그와 재현이 가장 쉽다.
- `FGameplayEffectSpec` 자체는 난수를 쓰지 않는다. 모디파이어 계산(`FAggregatorModChannel::EvaluateWithBase`, `GAS/Private/GameplayEffectAggregator.cpp:76-98`)은 `((Base + Additive) * Multiplicative / Division * CompoundMultiply) + FinalAdd` 로 고정 순서 합산이라 같은 적용 순서면 부동소수 결과도 동일하다. 채널 컨테이너(`ModChannelsMap`, 250-260)는 `TMap` 순회지만 삽입 순서를 보존하므로 결정론적이다.

### 5) 다수 ASC(수백 개)의 메모리·비용 구성, 경량 능력치 구조체 대체의 손익

#### 근거 표

| 사실 | 근거 (경로:줄) | 발췌 |
|---|---|---|
| ASC 의 주요 컨테이너 멤버 | `GAS/Public/AbilitySystemComponent.h:1668, 1855, 1859, 1863, 1866, 1876, 1923, 1932, 1943, 1953` | `FGameplayAbilitySpecContainer ActivatableAbilities; FActiveGameplayEffectsContainer ActiveGameplayEffects; FActiveGameplayCueContainer ActiveGameplayCues; FActiveGameplayCueContainer MinimalReplicationGameplayCues; FGameplayTagCountContainer BlockedAbilityTags; FGameplayTagCountContainer GameplayTagCountContainer; FMinimalReplicationTagCountMap MinimalReplicationTags; TArray<TObjectPtr<UAttributeSet>> SpawnedAttributes; FMinimalReplicationTagCountMap ReplicatedLooseTags; FReplicatedPredictionKeyMap ReplicatedPredictionKeyMap;` |
| 태그 카운트 컨테이너는 TMap 2개 + 컨테이너 + 델리게이트 | `GAS/Public/GameplayEffectTypes.h:1390-1399` | `TMap<FGameplayTag, FDelegateInfo> GameplayTagEventMap; TMap<FGameplayTag, int32> GameplayTagCountMap; FOnGameplayEffectTagCountChanged OnAnyTagChangeDelegate; FGameplayTagContainer ExplicitTags;` |
| 활성 GE 하나의 구성 | `GAS/Public/GameplayEffect.h` `struct FActiveGameplayEffect` 멤버 | `FActiveGameplayEffectHandle Handle; FGameplayEffectSpec Spec; FPredictionKey PredictionKey; TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles; float StartServerWorldTime; float CachedStartServerWorldTime; float StartWorldTime; ... FTimerHandle PeriodHandle; FTimerHandle DurationHandle; FActiveGameplayEffectEvents EventSet;` |
| 스펙 하나의 구성 | `GAS/Public/GameplayEffect.h` `struct FGameplayEffectSpec` 멤버 | `TArray<FGameplayEffectModifiedAttribute> ModifiedAttributes; FGameplayEffectAttributeCaptureSpecContainer CapturedRelevantAttributes; TArray<FGameplayEffectSpecHandle> TargetEffectSpecs; FTagContainerAggregator CapturedSourceTags; FTagContainerAggregator CapturedTargetTags; FGameplayTagContainer DynamicGrantedTags; FGameplayTagContainer DynamicAssetTags; TArray<FModifierSpec> Modifiers; TArray<FGameplayAbilitySpecDef> GrantedAbilitySpecs; TMap<FName,float> SetByCallerNameMagnitudes; TMap<FGameplayTag,float> SetByCallerTagMagnitudes; FGameplayEffectContextHandle EffectContext;` |
| 이펙트 컨텍스트는 스펙마다 힙 할당 | `GAS/Private/AbilitySystemGlobals.cpp:226-229` | `FGameplayEffectContext* UAbilitySystemGlobals::AllocGameplayEffectContext() const { return new FGameplayEffectContext(); }` |
| ActorInfo 는 ASC 마다 공유 포인터로 힙 할당 | `GAS/Private/AbilitySystemComponent.cpp:197-200` | `AbilityActorInfo = TSharedPtr<FGameplayAbilityActorInfo>(UAbilitySystemGlobals::Get().AllocAbilityActorInfo());` |
| 지속 GE 마다 타이머 1~2개 | `GAS/Private/GameplayEffect.cpp:4485, 4507` | `SetTimer(DurationHandle...)`, `SetTimer(PeriodHandle..., true)` |
| 타이머 힙은 ExpireTime 기준 이진 힙 | `Engine/Private/TimerManager.cpp:717, 1196-1231` | `ActiveTimerHeap.HeapPush(...)`, `HeapPop(...)` |
| 어빌리티 인스턴싱: InstancedPerActor 가 기본, NonInstanced deprecated | `GAS/Public/Abilities/GameplayAbilityTypes.h:46-50` | ASC 마다 어빌리티 UObject 인스턴스 1개씩 생성 |
| InitializeComponent 가 Owner 의 모든 하위 오브젝트를 순회 | `GAS/Private/AbilitySystemComponent_Abilities.cpp:95-104` | `GetObjectsWithOuter(Owner, ChildObjects, EGetObjectsFlags::None, RF_NoFlags, EInternalObjectFlags::Garbage);` |
| 태스크 전역 상한 | `GAS/Private/Abilities/Tasks/AbilityTask.cpp:33-38` | `AbilitySystem.AbilityTask.MaxCount = 1000` |
| TDGame AttributeSet: 속성 9개 | `TD/Combat/GAS/TDCombatAttributeSet.h:21-54` | `FGameplayAttributeData Health, MaxHealth, Level, AttackPower, SpellPower, Armor, MagicResistance, CriticalChance, CriticalMultiplier` |

#### 비용 구성 (정성적 결론, 바이트 수는 미확인)

| 항목 | ASC 방식 | 경량 구조체 방식 |
|---|---|---|
| 몬스터 1마리 고정 비용 | 컴포넌트 UObject + ActorInfo 힙 + AttributeSet UObject + 어빌리티 인스턴스 UObject(어빌리티 수만큼) + 빈 컨테이너 10여 개 | POD 구조체 1개 (수백 바이트), 배열 슬롯 |
| 지속 버프 1개 | `FActiveGameplayEffect`(스펙 값 포함, 태그 컨테이너 4개, TMap 2개) + 컨텍스트 힙 할당 + 타이머 1~2개 | `{정의 ID, 만료 스텝, 스택}` 12~16 바이트, 타이머 없음(만료 스텝 비교) |
| 피해 1회 | 스펙 생성(`MakeOutgoingSpec` → 컨텍스트 new, 태그 캡처) → CanApply → 애그리게이터 재평가 → 델리게이트 | 함수 호출 1회, 부동소수 연산 몇 개 |
| 틱 비용 | 몽타주·틱 태스크 없으면 `SetActive(false)` 로 틱 정지 가능; 켜져 있어도 빈 함수 수준 | 시뮬레이션 루프가 배열을 순차 처리 |
| 지속시간 처리 | 타이머 힙 O(log n), 만료 시 `CheckDuration` 이 내부 배열 선형 탐색(`GameplayEffects_Internal` 순회, 5374) | 정수 비교 |
| 태그 질의 | `FGameplayTagCountContainer` TMap 조회 | 비트마스크 |
| 얻는 것 | 실제 게임과 100% 같은 코드 경로(밸런스 수치의 신뢰성), 이미 검증된 스택·주기·면역·태그 로직, 자동화 테스트 재사용 | 수천 마리·수만 회 반복 가능한 속도, 완전한 결정론 통제(스텝 정수 시간), 병렬 실행 용이 |
| 잃는 것 | 속도(UObject 생성/GC, 타이머 힙, 태그 맵), 타이머 기반 만료의 "1스텝 지연", 전역 static(서버 키 카운터)·전역 rand 의존 | 게임 코드와 이중 구현 → 두 구현이 어긋날 위험(회귀 테스트로 상쇄 필요) |

#### 해석

- **수백 마리(≤ 500) 규모의 밸런스 툴**은 ASC 그대로 헤드리스로 돌려도 성립한다(엔진 GAS 테스트가 같은 방식). 가장 큰 비용은 (1) 스펙·컨텍스트 힙 할당, (2) 타이머 힙, (3) GC 다. 대량 반복(수천 시나리오 × 수천 스텝)에서는 GC 정지 시간이 지배적이므로 시나리오마다 월드를 새로 만들지 말고 **액터 풀을 재사용하거나, `CollectGarbage` 주기를 명시적으로 제어**해야 한다(미확인: 커맨드렛 루프의 GC 빈도는 실측 필요).
- **수천 마리 이상 또는 강화학습용 고속 롤아웃**에는 경량 구조체 시뮬레이터가 필요하다. 이때 핵심은 "피해 공식·버프 규칙"을 GAS 와 경량 시뮬레이터가 **같은 순수 함수**(예: `TDDamageFormula::Compute(const FTDStats&, const FTDStats&, ...)`)로 공유하는 것이다. TDGame 은 이미 `ReceiveDamage` 안에서 저항 공식을 double 로 계산하고(`TD/Combat/TDCombatComponent.cpp:222-224`) 그 결과를 SetByCaller 로 GE 에 넘기므로, 공식 부분을 정적 함수로 분리하면 두 경로가 동일 수치를 낸다.

### 6) 태그·이펙트를 C++ 로 정의하는 방법과 밸런스 툴의 장비/버프 표현

#### 근거 표

| 사실 | 근거 (경로:줄) | 발췌 |
|---|---|---|
| GE 는 컴포넌트 배열로 동작 정의 | `GAS/Public/GameplayEffect.h:2466` | `TArray<TObjectPtr<UGameplayEffectComponent>> GEComponents;` |
| 동적 GE 를 위한 공식 API | `GAS/Public/GameplayEffect.h:2191, 2196-2199, 2215, 2481-2520` | `template<typename GEComponentClass> const GEComponentClass* FindComponent() const;` / `template<typename GEComponentClass> GEComponentClass& FindOrAddComponent();` (헤더 주석: "If you're building your own dynamic GameplayEffect: @see AddComponent, FindOrAddComponent") |
| 생성자 기본값 | `GAS/Private/GameplayEffect.cpp:182-197` | `DurationPolicy = Instant; bExecutePeriodicEffectOnApplication = true; PeriodicInhibitionPolicy = NeverReset; StackingType = None; StackLimitCount = 0; ...` |
| PostLoad 가 OnGameplayEffectChanged 로 태그 캐시 재집계 | `GAS/Private/GameplayEffect.cpp:375-400` | `void UGameplayEffect::PostLoad() { Super::PostLoad(); OnGameplayEffectChanged(); ... }` |
| 업그레이드(deprecated→컴포넌트) 는 PostCDOCompiled 에서만 | `GAS/Private/GameplayEffect.cpp:484-500` | `void UGameplayEffect::PostCDOCompiled(const FPostCDOCompiledContext&)` → `ConvertAbilitiesComponent(); ... ConvertChanceToApplyComponent(); ...` (호출자는 `Source/Editor/KismetCompiler/Private/KismetCompiler.cpp` 뿐) |
| TDGame 의 C++ GE 정의 방식 | `TD/Combat/GAS/TDCombatGameplayEffects.cpp:7-63` | `CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>` → `GEComponents.Add` → `SetAndApplyAssetTagChanges`; `UTargetTagsGameplayEffectComponent::SetAndApplyTargetTagChanges` |
| TDGame 은 CDO 를 직접 적용 | `TD/Combat/TDCombatComponent.cpp:199` | `ApplyGameplayEffectToSelf(GetDefault<UTDDeadEffect>(), GetStats().Level, MakeEffectContext());` |
| 스펙 지속시간을 코드로 덮어쓰기 | `GAS/Public/GameplayEffect.h:1083`, `TD/Combat/GAS/TDDamageGameplayAbility.cpp:85` | `void SetDuration(float NewDuration, bool bLockDuration);` → `Spec.Data->SetDuration(Definition->Cooldown, true);` |
| SetByCaller 크기 전달 | `GAS/Public/GameplayEffect.h` FGameplayEffectSpec:266-267 | `TMap<FName,float> SetByCallerNameMagnitudes; TMap<FGameplayTag,float> SetByCallerTagMagnitudes;` |
| 쿨다운은 태그 보유 여부로 판정 | `GAS/Private/Abilities/GameplayAbility.cpp:1064-1080, 1106-1113` | `bool CheckCooldown(...)` → `AbilitySystemComponent->HasAnyMatchingGameplayTags(*CooldownTags)`; `void ApplyCooldown(...)` → `ApplyGameplayEffectToOwner(..., CooldownGE, Level)` |
| 쿨다운 무시 콘솔 변수 | `GAS/Private/AbilitySystemGlobals.cpp:39-40` | `AbilitySystem.IgnoreCooldowns`, `AbilitySystem.IgnoreCosts` (ECVF_Cheat) |
| 애그리게이터 합산 순서 | `GAS/Private/GameplayEffectAggregator.cpp:76-98` | `return ((InlineBaseValue + Additive) * Multiplicitive / Division * CompoundMultiply) + FinalAdd;` |

#### 권장 표현 방식 (밸런스 툴에서 장비·물약·버프)

1. **정적 정의는 C++ CDO 서브클래스**(현행 유지): 태그·지속 정책·모디파이어 "형태"만 정의하고, **수치는 SetByCaller 태그로 주입**한다. 장비 100종을 100개 클래스로 만들 필요가 없다. `UTDEquipmentStatEffect`(Infinite, Additive 모디파이어 N개, 각 모디파이어의 크기가 `Data.Equip.AttackPower` 같은 SetByCaller 태그) 하나면 모든 장비를 표현한다.
2. **수치 데이터는 텍스트(JSON/CSV/DataTable)** 로 두고, 시뮬레이션 로더가 `MakeOutgoingSpec(UTDEquipmentStatEffect::StaticClass(), Level, Context)` → `Spec.Data->SetSetByCallerMagnitude(Tag, Value)` → `ApplyGameplayEffectSpecToSelf` 로 적용한다. 생성형 AI 가 에디터 없이 읽고 고칠 수 있다.
3. **물약·일시 버프**는 `HasDuration` CDO + `SetDuration(Seconds, true)` 로 지속시간을 코드에서 덮어쓴다(TDGame 쿨다운이 이미 이 방식). 지속시간을 GE 클래스마다 고정하지 말 것.
4. **주기 효과(도트·재생)** 는 `Period` 를 생성자에 두되, `bExecutePeriodicEffectOnApplication` 의미(다음 타이머 틱에 첫 실행)를 문서화한다.
5. **Additive 와 Multiplicative 의 채널**: 기본 채널 하나만 쓰면 "모든 가산 → 모든 곱산" 순서가 강제된다(위 합산식). "장비 가산 후 버프 %" 같은 단계가 필요하면 `UAbilitySystemGlobals::ShouldAllowGameplayModEvaluationChannels` 로 채널을 켜야 한다(설정값은 미확인).
6. **주의**: 생성자에서 `ChanceToApplyToTarget_DEPRECATED`, `StackingType`(직접 대입은 가능하나 deprecated 경고), `InheritableOwnedTagsContainer` 같은 deprecated 필드를 쓰면 C++ CDO 경로에서는 컴포넌트로 변환되지 않아 **조용히 무시**된다. 반드시 `UChanceToApplyGameplayEffectComponent`, `UTargetTagsGameplayEffectComponent` 등 컴포넌트를 직접 추가한다. 스택은 `StackingType/StackLimitCount` 가 아직 컴포넌트화되지 않은 필드이므로 그대로 생성자에서 설정 가능(`GAS/Private/GameplayEffect.cpp:190-191`).

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

### 그대로 쓰는 것
- **헤드리스 픽스처 `FTDScopedCombatWorld`**: `CreateWorld → InitializeActorsForPlay → BeginPlay → SetBegunPlay`, 스텝마다 `++GFrameCounter; World->Tick(LEVELTICK_All, Delta)` (`TD/Combat/Tests/TDDamageSystemTests.cpp:30-80`). 이 절차가 엔진 GAS 테스트와 동일하므로 밸런스 툴 커맨드렛의 루프도 이 코드를 공용 헤더로 승격해 재사용한다. `TDDamageExamplesCommandlet` 이 이미 커맨드렛 골격을 갖고 있다 (`TD/Combat/TDDamageExamplesCommandlet.cpp:45`).
- **ASC 복제 끔** (`SetIsReplicatedByDefault(false)`), **C++ CDO GE** (`TDCombatGameplayEffects.cpp`), **태스크·몽타주 없는 동기 어빌리티** (`UTDDamageGameplayAbility`), **ASC 적용 쿼리 훅** (`GameplayEffectApplicationQueries`).

### 바꿔야 하는 것
1. **난수 3곳을 시드 스트림으로 교체**: `TDCombatComponent.cpp:220` 치명타, `TDDamageSubsystem.cpp:203-204` 산탄. `FTDDamageContext` 에 `FRandomStream*`(또는 시뮬레이션 컨텍스트 핸들)을 실어 보내고, 실제 게임에서는 월드 서브시스템이 소유한 스트림을, 시뮬레이션에서는 시나리오 시드로 만든 스트림을 넣는다. GE 의 `ChanceToApply` 컴포넌트는 사용하지 않는다.
2. **고정 스텝 델타를 이진 소수로**: 0.02f 대신 1/64(0.015625) 또는 1/32(0.03125) 를 쓰면 float 누적 오차가 사라져 만료·주기 발화 스텝이 "N 초 직후 스텝"으로 단순해진다. 델타를 바꾸면 기존 테스트의 기대값이 달라질 수 있으므로 기대값을 "스텝 수"로 다시 정의한다.
3. **시뮬레이션 ASC 는 `bSuppressGameplayCues = true`** (또는 커맨드렛 시작 시 `AbilitySystem.DisableGameplayCues 1`). 큐 매니저의 오브젝트 라이브러리 로딩(`InitializeRuntimeObjectLibrary`, `AbilitySystemGlobals.cpp:457`)은 `InitGlobalData` 에서 어차피 한 번 수행되므로 시작 시간 비용은 남는다(측정 미확인).
4. **컨트롤러 없는 시뮬레이션 액터**: 헤드리스에서 `AAIController` 를 붙이면 `IsLocallyControlled()` 분기(`GameplayAbilityTypes.cpp:113-123`)가 LocalOnly 어빌리티를 거부할 수 있다. 시뮬레이션은 컨트롤러 없이 어빌리티를 직접 `TryActivateAbility` 하거나, 몬스터 어빌리티를 `ServerOnly` 로 선언한다.
5. **적용 순서 고정**: 같은 스텝의 도트·만료 순서는 타이머 삽입 순서에 의존하므로, 시뮬레이션은 참가자를 결정적 순서(팀→ID)로 처리하고 대상 선택 결과도 정렬한다.
6. **GC 통제**: 다중 시나리오 반복 시 월드 재생성 대신 액터 재사용(`ResetForSim()` 같은 초기화 함수)을 도입하고, 필요 시 시나리오 사이에 명시적 `CollectGarbage(RF_NoFlags)` 를 호출해 정지 시점을 결정론적으로 만든다.

### 피할 것
- `UAbilityTask_PlayMontageAndWait`, 루트 모션·이동 태스크(틱 태스크 3종) 를 시뮬레이션 경로에서 사용하는 것. 시뮬레이션은 애니메이션 인스턴스가 없어 즉시 실패한다.
- `EGameplayEffectReplicationMode` 튜닝을 성능 대책으로 삼는 것(복제 자체가 꺼져 있으면 무의미).
- `NonInstanced` 어빌리티(5.5 부터 deprecated, `AbilitySystem.Fix.AllowNonInstancedAbilities` 로만 허용).
- `FMath::RandInit` 전역 시드에 결정론을 기대하는 것.
- 생성자에서 deprecated GE 필드로 동작을 정의하는 것(C++ CDO 경로에서는 변환되지 않음).

### 규모별 선택 지침
| 규모 | 권장 |
|---|---|
| 밸런스 툴, 몬스터 ≤ 수백, 시나리오 수천 | ASC 헤드리스 그대로 (실제 게임 코드 경로 100% 재사용) |
| 강화학습 롤아웃, 몬스터 수천, 스텝 수백만 | 경량 구조체 시뮬레이터 + GAS 와 **공식 함수 공유** + 두 경로 일치 회귀 테스트 |
| 실제 게임 화면의 대량 몬스터 | ASC 유지하되 몽타주·틱 태스크를 쓰지 않는 몬스터는 틱이 자동 정지됨(`UpdateShouldTick`). 추가로 지속 GE 개수를 줄여 타이머 힙 크기를 관리 |

---

## 미확인·미해결 질문

1. **`sizeof(UTDCombatComponent)`, `sizeof(FActiveGameplayEffect)`, `sizeof(FGameplayEffectSpec)` 실측값** — 코드 구조로만 "크다"고 판단했다. 자동화 테스트에서 `UE_LOG` 로 한 번 찍어 문서에 기록할 것.
2. **커맨드렛 루프의 GC 발생 빈도와 정지 시간** — `World->Tick` 만 돌리는 커맨드렛에서 GC 가 언제 도는지(엔진 루프 밖이므로 자동 GC 가 없을 가능성) 미확인. 메모리 증가 곡선 실측 필요.
3. **`UGameplayAbilitiesDeveloperSettings` 의 프로젝트 설정값**(`AbilitySystemGlobalsClassName`, `GlobalGameplayCueManagerClass`, 평가 채널 허용 여부) — Config 를 읽지 않았다(지시상 Config 수정 금지, 읽기는 가능하나 이번 조사 범위 밖).
4. **부동소수 재현성의 범위** — 같은 바이너리·같은 CPU 에서는 동일하지만, 다른 컴파일 옵션(FMA 사용 여부)이나 다른 CPU 에서 애그리게이터 결과가 비트 단위로 같은지는 미확인. "같은 빌드에서 같은 결과"를 결정론의 정의로 명시할 것.
5. **`FTimerManager` 의 동률 ExpireTime 처리 순서가 힙 구현 버전 간에 안정적인지** — 5.8 코드는 `ExpireTime <` 만 비교한다. 엔진 업그레이드 시 순서가 달라질 수 있으므로 골든 로그 테스트로 감시해야 한다.
6. **틱 함수 병렬 실행** — `tick.AllowAsyncComponentTicks` 기본 1 (`Engine/Private/TickTaskManager.cpp:55-56`). ASC 는 `bRunOnAnyThread` 를 켜지 않으므로 게임 스레드에서 돌지만, 프로젝트가 다른 컴포넌트에 비동기 틱을 켜면 순서 비결정성이 생길 수 있다. 시뮬레이션 커맨드렛에서 `tick.AllowAsyncComponentTicks 0` 강제를 검토.
7. **`UAbilitySystemGlobals::GetGameplayCueManager` 가 헤드리스에서 큐 에셋 라이브러리를 스캔하는 시간** — 프로젝트에 큐 에셋이 거의 없어 작을 것으로 보이나 실측 미확인.
8. **`IsLocallyControlled()` 경로가 실제 게임의 몬스터(AIController 부착 Pawn)에서 LocalOnly 어빌리티를 허용하는지** — `APawn::IsLocallyControlled()` 는 컨트롤러가 로컬이면 참을 돌려주므로 Standalone 의 AIController 는 로컬로 판정될 가능성이 높으나, 이번 조사에서는 `Pawn.cpp` 를 읽지 않았다(미확인).

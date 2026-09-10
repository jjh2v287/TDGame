# 대량 몬스터의 이동·애니메이션·틱 비용 절감 수단(CMC, Mover, 애니 예산, 시그니피컨스, ISM)

- 조사 대상 엔진: Unreal Engine 5.8 (`C:/Program Files/Epic Games/UE_5.8/Engine`, 읽기 전용)
- 아래 모든 경로는 엔진 루트(`Engine/`) 기준 상대 경로이며, 프로젝트 파일은 `TDGame/` 접두어로 구분한다.
- 약어: CMC = UCharacterMovementComponent(캐릭터 무브먼트 컴포넌트), ISM = Instanced Static Mesh(인스턴스드 스태틱 메시), URO = Update Rate Optimizations(애니메이션 갱신 주기 최적화), RVO = Reciprocal Velocity Obstacles(상호 속도 장애물 회피), NP = NetworkPrediction 플러그인, VAT = Vertex Animation Texture(정점 애니메이션 텍스처).
- "미확인"이라 표기한 항목은 코드로 확인하지 못한 것이다.

## 결론 요약

1. **CMC 는 기본값 그대로 두면 매 프레임 "바닥 스윕 + 라인 트레이스 + 이동 스윕 + 물리 상호작용 오버랩 순회"를 한다.** `bAlwaysCheckFloor=true`(cpp:798), `bEnablePhysicsInteraction=true`(cpp:770)가 기본이며, 정지 중이라도 바닥 검사를 생략하려면 `bAlwaysCheckFloor=false` 로 바꾸고 유효한 MovementBase 가 있어야 한다(cpp:7235~7266). 물리 상호작용은 `TickComponent` 끝에서 `ApplyDownwardForce/ApplyRepulsionForce` 를 호출하고, 후자는 `UpdatedPrimitive->GetOverlapInfos()` 를 매 프레임 순회한다(cpp:1804~1809, 11798~11802). 몬스터에는 둘 다 끄는 것이 맞다.
2. **`MOVE_NavWalking` 은 캡슐의 WorldStatic/WorldDynamic 충돌 응답을 Ignore 로 바꾸고(cpp:6471~6472) 바닥을 네비메시 `ProjectPoint`(cpp:6082~6095) 로 대체한다.** 투영 주기는 `NavMeshProjectionInterval=0.1초`(cpp:834)이며 스폰 시 무작위로 어긋나게 배치한다(cpp:6477). 이동 스윕은 `bSweepWhileNavWalking`(h:453) 으로 끌 수 있다(cpp:6064). 대량 몬스터의 지상 이동은 NavWalking 이 CMC 안에서 가장 싼 경로다.
3. **`bUseRVOAvoidance` 는 O(N²) 이다.** `UAvoidanceManager::GetAvoidanceVelocity_Internal` 이 `TMap<int32,FNavAvoidanceData> AvoidanceObjects` 전체를 순회한다(AvoidanceManager.cpp:361, 403; AvoidanceManager.h:204). 수백 마리에서는 쓰지 말고, 공간 해시 기반 자체 분리(separation) 또는 DetourCrowd(`UCrowdManager::Tick`, CrowdManager.cpp:231, 기본 `MaxAgents=50`, cpp:168) 를 쓴다.
4. **ACharacter 대신 APawn + UFloatingPawnMovement 로 바꾸면 길찾기는 그대로 된다.** `UPathFollowingComponent` 는 5.8 에서 `INavMovementInterface` 만 요구하고(PathFollowingComponent.h:286, cpp:1514 `FindComponentByInterface<INavMovementInterface>`), `UNavMovementComponent` 가 그 인터페이스를 구현한다(NavMovementComponent.h:26). 잃는 것은 바닥·중력·계단·루트모션 처리·`FFindFloorResult`·`MOVE_Falling` 등 CMC 고유 물리(FloatingPawnMovement.cpp:23~83 에는 바닥 검사 자체가 없다).
5. **Mover 플러그인은 5.8 에서도 `IsExperimentalVersion: true` 이며, README 가 "scaling/performance 에 시간을 거의 쓰지 않았다, 캐릭터 수가 적은 게임에 적합" 이라고 명시한다**(Mover.uplugin:15, README.md:180). 결정론·롤백은 NetworkPrediction 백엔드의 Fixed 틱 정책에 붙어 있고(MoverNetworkPredictionLiaison.cpp:79~100), 독립(Standalone) 백엔드는 월드 시간·`GFrameCounter`·가변 DeltaSeconds 를 그대로 쓴다(MoverStandaloneLiaison.cpp:404~440). 대량 몬스터의 이동 엔진으로는 부적합하고, 결정론 시뮬레이션 근거로 삼기에도 NP 의존이 크다.
6. **틱 간격(`TickInterval`) 은 "누적 델타"를 전달한다.** `FTickFunction::CalculateDeltaTime` 은 간격 틱이면 `현재 월드 시간 - 마지막 간격 틱 시간` 을 돌려준다(TickTaskManager.cpp:2825~2843). 스케줄링은 정렬된 쿨다운 연결 리스트로 관리되며 초과분을 다음 간격에서 보정한다(cpp:1474~1506 "Give credit for any overrun"). 따라서 액터마다 간격을 달리 주면 프레임 분산은 되지만, 고정 스텝 결정론은 보장하지 않는다(월드 시간 기반).
7. **엔진이 대량 객체에 쓰는 정석은 "액터 틱 끄고 한 틱 함수가 배열을 순회"다.** MassEntity 는 `FMassProcessingPhase : FTickFunction` 하나가 `UE::Mass::Executor::Run` 으로 모든 엔티티를 처리하고(MassProcessingPhaseManager.h:54, cpp:65, 161), 애니메이션 예산 할당기는 `OnWorldPreActorTick` 한 곳에서 모든 등록 컴포넌트의 틱 여부를 결정한다(AnimationBudgetAllocator.cpp:797~803). `USignificanceManager::Update` 는 엔진 어디에서도 호출되지 않아(전수 grep 결과 0건) 프로젝트가 직접 호출해야 한다(SignificanceManager.cpp:478).
8. **애니메이션 없이 로직만 돌리는 헤드리스에는 `USkeletalMeshComponent::bEnableAnimation=false` + `TickMontagesOnly` 경로가 5.8 에 있다.** `UAnimInstance::TickMontageOnly` 가 `UpdateMontage` 와 `TriggerQueuedMontageEvents` 를 호출하므로 그래프 없이 몽타주 시간·노티파이가 진행된다(AnimInstance.cpp:628, 653~654). 더 가볍게는 `UAnimMontage::GetSectionStartAndEndTime/GetSectionLength`(AnimMontage.h:812, 815) 와 `Notifies` 배열의 `FAnimNotifyEvent::GetTriggerTime/GetDuration`(AnimTypes.h:408, 413) 을 읽어 "몽타주 시간표" 데이터로 뽑아 순수 C++ 타이머로 대체할 수 있다.
9. **화면 밖 애니메이션은 세 층으로 줄일 수 있다.** (a) `VisibilityBasedAnimTickOption` 을 `OnlyTickMontagesWhenNotRendered` 이상으로(SkinnedMeshComponent.h:96~116), (b) URO 의 비렌더 갱신 주기 `BaseNonRenderedUpdateRate=4`(EngineTypes.h:2820), (c) 애니메이션 예산 할당기(`a.Budget.BudgetMs` 기본 1.0ms, `MaxTickRate=10`, 화면 밖 최대 4개만 틱, AnimationBudgetAllocatorParameters.h:19, 34, 94). ACharacter 기본값은 `AlwaysTickPose`(Character.cpp:124) 라 화면 밖에서도 애님 그래프가 돈다.
10. **군중 표현은 AnimToTexture(VAT) + ISM 이 엔진 내장 경로다.** 베이크는 에디터 모듈(`AnimationToTexture(DataAsset)`, AnimToTextureBPLibrary.h:31), 런타임은 ISM 의 커스텀 데이터 실수 배열에 `TimeOffset/PlayRate/StartFrame/EndFrame` 를 넣어 셰이더가 재생한다(AnimToTextureInstancePlaybackHelpers.h:27~59, cpp:29). 이동은 `BatchUpdateInstancesTransforms` 로 한 번에 갱신한다(InstancedStaticMeshComponent.h:391). 다만 플러그인은 Experimental 이며 히트 반응·블렌딩은 직접 만들어야 한다.
11. **주변 탐색은 `THierarchicalHashGrid2D<NumLevels,LevelRatio,ItemID>` 로 대체 가능하다.** 항목은 크기별 한 셀에만 들어가고, 쿼리는 상자를 확장해 거짓 양성을 허용한다(HierarchicalHashGrid2D.h:12~19, 342, 384). MassNavigation 이 회피 장애물 격자로 `<2,4>` 를 쓴다(MassNavigationSubsystem.h:33, 51). 오버랩 이벤트를 끄고 이 격자로 "누가 근처에 있나" 를 답하면 물리 브로드페이즈 비용을 없앨 수 있다.

## 상세 조사

### 1) UCharacterMovementComponent 의 프레임당 비용과 절감 옵션

핵심 파일: `Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h`(3351줄), `Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`(14025줄).

| 항목 | 근거(경로:줄) | 내용 |
|---|---|---|
| 틱 그룹 | MovementComponent.cpp:28, 35 (기반 클래스 생성자); CMC cpp:658~672 | 기반 `UMovementComponent` 생성자에서 `PrimaryComponentTick.TickGroup = TG_PrePhysics`, `bTickBeforeOwner = true`. CMC 는 추가로 `PostPhysicsTickFunction`(TG_PostPhysics) 과 비동기 모드용 `PrePhysicsTickFunction` 을 가짐 |
| 진입 조건 | cpp:1650, 1665 | `TickComponent(float, ELevelTick, FActorComponentTickFunction*)`; `ShouldSkipUpdate` 가 true 면 즉시 반환 |
| AI 캐릭터 경로 | cpp:1751~1757 | `IsLocallyControlled()` 또는 `!Controller && bRunPhysicsWithNoController` 이면 `ControlledCharacterMove` |
| 싱글플레이 분기 | cpp:6441~6461 | `ROLE_Authority` 면 `PerformMovement(DeltaSeconds)` 직접 호출, 네트워크 저장 무브(`ReplicateMoveToServer`) 는 클라이언트 전용 |
| 서브스텝 | cpp:698~699, h:748, 759 | `MaxSimulationTimeStep=0.05`, `MaxSimulationIterations=8` → 낮은 프레임에서 한 프레임에 최대 8회 물리 반복 |
| 바닥 검사 강제 | cpp:798, h:1046 | `bAlwaysCheckFloor = true` 기본. 헤더 주석: "정지 중이면 바닥 검사를 피하지만 이 값으로 강제" |
| 바닥 검사 생략 조건 | cpp:7235~7266 | `bAlwaysCheckFloor || !bCanUseCachedLocation || bForceNextFloorCheck || bJustTeleported` 면 `ComputeFloorDist`; 아니면 유효한 MovementBase 가 있어야만 `OutFloorResult = CurrentFloor` 로 생략 |
| 바닥 검사 비용 | cpp:7125, 7143, 7183 | 캡슐 스윕 `FloorSweepTest` 1회(관통 시 2회) + `LineTraceSingleByChannel` 1회 |
| PhysWalking 호출 | cpp:5686, 5774 | 반복마다 `FindFloor(..., bZeroDelta, NULL)` 호출 |
| 물리 상호작용 | cpp:770~771, 783, 1804~1809, 11784, 11798~11802 | `bEnablePhysicsInteraction=true`, `RepulsionForce=2.5` 기본. 매 틱 `ApplyDownwardForce`(바닥 컴포넌트가 물리 시뮬 중이면 힘 가산) + `ApplyRepulsionForce`(`GetOverlapInfos()` 전체 순회, 오버랩마다 바디 조회) |
| RVO 회피 | cpp:822, 1799~1802, 4105~4172; AvoidanceManager.cpp:361, 403; AvoidanceManager.h:204 | `bUseRVOAvoidance=false` 기본. 켜면 `CalcAvoidanceVelocity` → `GetAvoidanceVelocityForComponent` → 모든 `AvoidanceObjects` 순회(에이전트 수 N 에 대해 N²) |
| NavWalking 충돌 | cpp:6465~6480 | `SetNavWalkingPhysics(true)` 가 `ECC_WorldStatic/ECC_WorldDynamic` 응답을 `ECR_Ignore` 로 바꾸고 `NavMeshProjectionTimer` 를 `FRandRange(-Interval, 0)` 로 분산 |
| NavWalking 바닥 | cpp:6082~6095, 6112~6135, 834 | `FindNavFloor` 는 `NavData->ProjectPoint` 만 호출. `PhysNavWalking` 은 `CachedProjectedNavMeshHitResult` 가 같은 위치면 트레이스 생략, `NavMeshProjectionInterval=0.1` 주기로만 지오메트리 투영(`bProjectNavMeshWalking`, h:1118) |
| NavWalking 이동 스윕 | h:453, cpp:6064 | `SafeMoveUpdatedComponent(AdjustedDelta, ..., bSweepWhileNavWalking, HitResult)`; 스윕을 끄면 이동은 순수 텔레포트 |
| 렌더 안 될 때 스킵 | MovementComponent.h:127~129, MovementComponent.cpp:333~345 | `bUpdateOnlyIfRendered`: 최근 0.41초 안에 렌더되지 않으면 `ShouldSkipUpdate` true(전용 서버는 항상 스킵) |
| 스코프 무브 | h:479, cpp:817 | `bEnableScopedMovementUpdates=true` 기본: 한 틱의 여러 이동을 묶어 오버랩/트랜스폼 갱신을 마지막에 한 번만 |
| 포즈 틱 결합 | cpp:12027~12057 | `TickCharacterPose`: `bEnableAnimation` 이면 `TickPose`, 아니면 `TickMontagesOnly` — CMC 가 루트모션 때문에 메시 포즈 틱을 직접 구동 |

발췌(FindFloor 캐시 생략 조건, cpp:7235~7237):
```cpp
if ( bAlwaysCheckFloor || !bCanUseCachedLocation || bForceNextFloorCheck || bJustTeleported )
{
    MutableThis->bForceNextFloorCheck = false;
    ComputeFloorDist(CapsuleLocation, FloorLineTraceDist, FloorSweepTraceDist, OutFloorResult, ...);
}
```

발췌(물리 상호작용, cpp:1804~1809):
```cpp
if (bEnablePhysicsInteraction)
{
    SCOPE_CYCLE_COUNTER(STAT_CharPhysicsInteraction);
    ApplyDownwardForce(DeltaTime);
    ApplyRepulsionForce(DeltaTime);
}
```

**APawn + UFloatingPawnMovement 로 바꿀 때 잃는 것**

| 근거 | 내용 |
|---|---|
| FloatingPawnMovement.cpp:23~83 | `TickComponent` 는 `ShouldSkipUpdate` → `ApplyControlInputToVelocity` → `SafeMoveUpdatedComponent`(스윕 1회) → 충돌 시 `SlideAlongSurface` → `UpdateComponentVelocity`. 바닥·중력·계단·경사 처리 없음 |
| FloatingPawnMovement.h:22, PawnMovementComponent.h:42, NavMovementComponent.h:26 | `UFloatingPawnMovement : UPawnMovementComponent : UNavMovementComponent : UMovementComponent, INavMovementInterface` |
| PathFollowingComponent.h:283~286, cpp:1514 | `SetMovementComponent(UNavMovementComponent*)` 는 5.5 부터 폐기, `SetNavMovementInterface(INavMovementInterface*)` 사용. 폰에서 `FindComponentByInterface<INavMovementInterface>()` 로 자동 연결 |
| CrowdFollowingComponent.h:38 | `UCrowdFollowingComponent : UPathFollowingComponent, ICrowdAgentInterface` — DetourCrowd 는 CMC 없이도 동작(인터페이스 기반) |

잃는 것 정리: `MOVE_Falling/Walking/NavWalking` 상태 기계, `FFindFloorResult`/`CurrentFloor`, 계단 오르기(`MaxStepHeight`), 루트모션 적용(`TickCharacterPose`), `ACharacter::Jump/Crouch`, 캡슐 기반 바닥 스윕, 베이스(움직이는 발판) 추적. 자체 이동으로 가면 여기에 더해 충돌 스윕도 직접 선택해야 하지만, 탑다운 지상 몬스터라면 네비메시 투영(`ProjectPoint`)만으로 바닥 문제를 대부분 대체할 수 있다(CMC 의 NavWalking 이 실제로 그렇게 한다).

### 2) Mover 플러그인(5.8) — 결정론·롤백 구조와 대량 적합성

| 항목 | 근거 | 내용 |
|---|---|---|
| 상태 | `Plugins/Experimental/Mover/Mover.uplugin:15` | `"IsExperimentalVersion": true`; 의존 플러그인 NetworkPrediction, MotionWarping, PoseSearch, Water, ChaosVD(uplugin:39~60) |
| 공식 경고 | README.md:5, 180 | "Experimental. Many features incomplete" / "little time has been spent on scaling/performance. Single-player or games with smaller character counts will be more feasible." |
| 싱글플레이 권고 | README.md:182 | NP 틱 정책을 Independent 로 두거나 `MoverStandaloneLiaisonComponent` 를 백엔드로 지정해 NP 오버헤드 제거 |
| 틱 순서 제약 | README.md:191~195 | NP 시뮬레이션은 월드 틱 그룹보다 먼저 일정한 순서로 틱; 고정 틱 + 가변 렌더 프레임이면 한 프레임에 0회 또는 여러 회 시뮬 |
| 시간 스텝 | `Source/Mover/Public/MoverTypes.h:149~170` | `struct FMoverTimeStep { int32 ServerFrame; double BaseSimTimeMs; float StepMs; uint8 bIsResimulating:1; uint8 bIsFirstResimFrame:1; }` |
| 백엔드 구조 | MoverComponent.h:206, README.md:86 | `TSubclassOf<UActorComponent> BackendClass;` — MoverComponent 는 스스로 틱하지 않고 백엔드가 구동 |
| 독립 백엔드 시간 | MoverStandaloneLiaison.cpp:404~408, 424~440 | `CurrentSimTimeMs = World->GetTimeSeconds()*1000`, `CurrentSimFrame = GFrameCounter`, `StepMs = DeltaSeconds*1000` → 가변 스텝, 월드 시간 종속 |
| 독립 백엔드 틱 함수 | MoverStandaloneLiaison.cpp:61~81, 355~369 | ProduceInput / SimulateMovement / ApplyState 3개 `FTickFunction` 모두 `TG_PrePhysics`, 서로 prerequisite 로 순서 고정. `SetUseAsyncMovementSimulationTick`(h:150) 로 워커 스레드 실행 가능 |
| 롤백 | MoverNetworkPredictionLiaison.cpp:79~108 | `PreferredDefaultTickingPolicy()` 가 `Fixed` 면 `FFixedTickState::GetNextTimeStep()`, `Independent` 면 `FVariableTickState`; `RestoreFrame` 이 `Simulation->RollbackToPriorState(TimeStep, SyncState, AuxState)` 호출 |
| 시뮬 객체 | KinematicActorSimulation.h:45~46 | `DoSimulationTick(const FMoverTimeStep&, const FMoverTickStartData&, OUT FMoverTickEndData&)`, `RollbackToPriorState(...)` — 상태 입력/출력이 구조체로 분리되어 있어 원리상 순수 함수형 |
| 내비게이션 | NavMoverComponent.h:27, 145, 148 | `UNavMoverComponent : UActorComponent, INavMovementInterface, IRVOAvoidanceInterface`; `RequestDirectMove/RequestPathMove` 구현 → PathFollowing 연동 됨 |
| NavWalking 모드 | NavWalkingMode.h:55, 59, 97 | "contains some randomization to avoid navmesh look ups" 주석; `NavMeshProjectionInterval` 별도 보유 |

판단: Mover 의 "입력·동기 상태·보조 상태를 구조체로 넣고 `DoSimulationTick` 으로 새 상태를 내는" 설계는 결정론 시뮬레이터의 좋은 참고 모델이지만, 고정 스텝은 NP 백엔드에서만 제공되고 독립 백엔드는 월드 시간을 그대로 쓴다. 대량 몬스터에 적용하면 액터마다 3개 틱 함수 + MoverComponent + NavMoverComponent 가 붙어 CMC 보다 객체 수가 늘어난다. TDGame 은 참고만 하고 채택하지 않는 쪽이 맞다.

### 3) 애니메이션 — 예산 할당기, 가시성 옵션, URO, AnimToTexture, 헤드리스 몽타주 대체

**3-1. UAnimationBudgetAllocator (`Plugins/Runtime/AnimationBudgetAllocator`)**

| 항목 | 근거 | 내용 |
|---|---|---|
| 갱신 시점 | AnimationBudgetAllocator.cpp:797~803 | `OnWorldPreActorTick(UWorld*, ELevelTick, float)` 에서 `LEVELTICK_All` 일 때 `Update(DeltaSeconds)` — 액터 틱 전에 한 번 |
| 인터페이스 | IAnimationBudgetAllocator.h:52, 70, 82 | `SetComponentSignificance(USkeletalMeshComponentBudgeted*, float Significance, bool bNeverSkip, bool bTickEvenIfNotRendered, bool bAllowReducedWork, bool bForceInterpolate)`, `Update(float)`, `ForceNextTickThisFrame` |
| 대상 컴포넌트 | SkeletalMeshComponentBudgeted.h:19, 36, 52; cpp:23~24, 33 | `USkeletalMeshComponentBudgeted` 만 참여. `bAutoRegisterWithBudgetAllocator=true`, `bAutoCalculateSignificance=false` 기본, 전용 서버에서는 등록 안 함. 정적 델리게이트 `OnCalculateSignificance()` 로 프로젝트 중요도 함수 주입 |
| 틱 여부 판정 | cpp:254~262 | 최근 1초 렌더됨, 액터 렌더 플래그, `bTickEvenIfNotRendered`, `ShouldTickPose()`, `VisibilityBasedAnimTickOption` 이 AlwaysTick* 중 하나면 틱 후보 |
| 틱 주기 배정 | cpp:440, 472, 478 | `bTickThisFrame = ((GFrameCounter + FrameOffset) % TickRate) == 0`; `EnableExternalInterpolation(TickRate>1 && bInterpolate)`; `SetExternalTickRate(TickRate)` |
| 주기 계산식 | cpp:567, 588, 607, 628, 640 | 예산(ms)/평균 작업 단위 시간 → 전부 틱할 개수, 보간 개수(≤`MaxInterpolatedComponents`), 나머지는 `Lerp(2, MaxThrottleRate≤MaxTickRate, Alpha)` 로 N 프레임마다 1회 |
| 파라미터 기본값 | AnimationBudgetAllocatorParameters.h:19, 34, 65, 72, 94, 101, 108, 165 | `BudgetInMs=1.0`, `MaxTickRate=10`, `InterpolationMaxRate=6`, `MaxInterpolatedComponents=16`, `MaxTickedOffsreenComponents=4`, `StateChangeThrottleInFrames=30`, `BudgetFactorBeforeReducedWork=1.5`, `AutoCalculatedSignificanceMaxDistance=30000` |
| 콘솔 변수 | AnimationBudgetAllocatorCVars.cpp:10, 21, 30, 195, 208 | `a.Budget.Enabled`, `a.Budget.MaintainReduceWorkWhenOffScreen`, `a.Budget.ComponentsWithZeroSigAreNonRendered`, `a.Budget.BudgetMs`, `a.Budget.MinQuality` |
| 컴포넌트 쪽 수신 API | SkinnedMeshComponent.h:1018~1039 | `SetExternalTickRate(uint8)`, `EnableExternalInterpolation(bool)`, `SetExternalInterpolationAlpha(float)`, `EnableExternalEvaluationRateLimiting(bool)` |
| 외부 제어 반영 | SkeletalMeshComponent.cpp:1918~1927 | `ShouldTickAnimation()`: `bExternalTickRateControlled` 면 `bExternalUpdate` 반환, 아니면 URO 의 `ShouldSkipUpdate()` |

발췌(틱 프레임 결정, cpp:440):
```cpp
bool bTickThisFrame = (((GFrameCounter + ComponentDataToCheck.FrameOffset) % ComponentDataToCheck.TickRate) == 0);
```

**3-2. VisibilityBasedAnimTickOption 과 URO**

| 항목 | 근거 | 내용 |
|---|---|---|
| 열거형 | SkinnedMeshComponent.h:96~116 | `AlwaysTickPoseAndRefreshBones`, `AlwaysTickPose`, `OnlyTickMontagesAndRefreshBonesWhenPlayingMontages`, `OnlyTickMontagesWhenNotRendered`, `OnlyTickPoseWhenRendered` (순서가 곧 비교 기준) |
| 스켈레탈 판정 | SkeletalMeshComponent.cpp:1883~1915 | `bShouldTickBasedOnMontage`: 비렌더 + 몽타주 재생 중 + OnlyTickMontages* 옵션이면 틱 허용; `bShouldTickBasedOnVisibility`: 옵션 ≤ AlwaysTickPose 또는 `bRecentlyRendered` 또는 네트워크 루트모션 |
| 최근 렌더 정의 | SkinnedMeshComponent.cpp:1864 | `bRecentlyRendered = LastRenderTime > World->TimeSeconds - 1.0f` (1초 유예) |
| 캐릭터 기본값 | Character.cpp:124, 128 | `Mesh->VisibilityBasedAnimTickOption = AlwaysTickPose`, 메시 틱 그룹 `TG_PrePhysics` |
| URO 파라미터 | EngineTypes.h:2707, 2787, 2810~2820 | `FAnimUpdateRateParameters`: `BaseNonRenderedUpdateRate=4`, `bInterpolateSkippedFrames=false`, `LODToFrameSkipMap`(LOD별 프레임 스킵) |
| URO 규칙 | SkinnedMeshComponent.cpp:283~296 | 비렌더면 갱신·평가 주기를 `BaseNonRenderedUpdateRate`(4 프레임에 1회), 사람 조종 폰은 1 |
| URO 켜기 | SkinnedMeshComponent.h:933 | `bEnableUpdateRateOptimizations:1` |

**3-3. 애니메이션 없이 몽타주만 진행하기(헤드리스)**

| 항목 | 근거 | 내용 |
|---|---|---|
| 플래그 | SkeletalMeshComponent.h:852~857, cpp:499 | `bEnableAnimation`(기본 true): "false 면 외부 시스템이 메시를 애니메이션한다고 가정, 클로스도 안 돌고 컴포넌트 틱이 아무 스레드에서 가능" |
| 몽타주 전용 틱 | SkeletalMeshComponent.cpp:1788~1813 | `TickMontagesOnly(float DeltaTime, bool bNeedsValidRootMotion)`: `bEnableAnimation=false` 일 때만 `AnimScriptInstance->TickMontageOnly(DeltaTime)` |
| 인스턴스 쪽 | AnimInstance.cpp:628~665 | `UAnimInstance::TickMontageOnly(float)`: 유효하지 않은 몽타주 인스턴스 정리 → `UpdateMontage(DeltaSeconds)` → `TriggerQueuedMontageEvents()` (노티파이·블렌드아웃 이벤트 발화) |
| CMC 연결 | CharacterMovementComponent.cpp:12050~12057 | `bEnableAnimation` 이면 `TickPose`, 아니면 `TickMontagesOnly` → 루트모션 몽타주도 이 경로로 추출 |
| 시간표 데이터 API | AnimMontage.h:697, 808~827; AnimSequenceBase.h:43, 86, 108, 116 | `TArray<FCompositeSection> CompositeSections`, `GetSectionStartAndEndTime(int32, float&, float&)`, `GetSectionLength(int32)`, `GetSectionIndex(FName)`, `GetNumSections()`, `Notifies`, `GetPlayLength()`, `GetAnimNotifies(StartTime, DeltaTime, FAnimNotifyContext&)`, `GetAnimNotifiesFromDeltaPositions(Prev, Cur, Ctx)` |
| 노티파이 이벤트 | AnimTypes.h:276, 288, 298, 318, 322, 408~413 | `FAnimNotifyEvent : FAnimLinkableElement`; `TriggerTimeOffset`, `NotifyName`, `MontageTickType`(Queued/BranchingPoint), `NotifyTriggerChance`, `GetTriggerTime()`, `GetEndTriggerTime()`, `GetDuration()` |
| 블렌드 | AnimMontage.h:650, 659, 670 | `FAlphaBlend BlendIn/BlendOut`, `BlendOutTriggerTime` — "공격 동작 실제 종료 시각"을 계산할 때 필요 |
| 프로젝트 현황 | TDGame/Source/TDGame/Combat/Tests/TDMeleeAttackNotifyTests.cpp:54~61, 101~121, 92 | 이미 `AActor` + `USkeletalMeshComponent`(`AlwaysTickPoseAndRefreshBones`, `AnimationSingleNode`) 로 헤드리스 월드에서 동적 몽타주(`CreateSlotAnimationAsDynamicMontage`) 에 노티파이를 `Notifies.AddDefaulted_GetRef()`+`Link` 로 붙여 `World->Tick(LEVELTICK_All, Delta)` 로 검증 중 |

몽타주 시간표를 데이터로 뽑는 방법(제안, 위 API 근거):
1. 에디터/커맨드릿에서 `UAnimMontage` 를 로드해 `GetPlayLength()`, 각 섹션의 `GetSectionStartAndEndTime`, `BlendOut.GetBlendTime()`, `Notifies[i].GetTriggerTime()/GetDuration()/NotifyName/NotifyStateClass` 를 JSON 또는 DataTable 행으로 저장한다.
2. 헤드리스 시뮬레이션은 이 표만 읽어 "공격 시작 → 히트 창 시작(TriggerTime) → 히트 창 끝(TriggerTime+Duration) → 동작 종료(PlayLength 또는 섹션 끝 - BlendOutTriggerTime)" 를 고정 스텝 타이머로 진행한다. 노티파이 클래스는 실행하지 않고 이름·시간만 쓴다.
3. 정합성 검증은 기존 `TDMeleeAttackNotifyTests` 방식(실제 스켈레탈 메시 + `TickMontagesOnly` 또는 SingleNode 재생)으로 같은 시각에 같은 이벤트가 나오는지 자동화 테스트로 대조한다. 노티파이 인스턴스가 에셋당 1개로 공유된다는 제약(프로젝트 메모리 `tdgame-anim-notify-facts.md`) 은 시간표 방식에서는 사라진다.

**3-4. AnimToTexture(VAT) + ISM 군중 표현**

| 항목 | 근거 | 내용 |
|---|---|---|
| 상태 | `Plugins/Experimental/AnimToTexture/AnimToTexture.uplugin:15, 19~25` | `IsExperimentalVersion: true`; 모듈 `AnimToTexture`(Runtime), `AnimToTextureEditor`(Editor) |
| 베이크 | AnimToTextureBPLibrary.h:31 | `static bool AnimationToTexture(UAnimToTextureDataAsset* DataAsset)` — 에디터 전용 |
| 데이터 에셋 | AnimToTextureDataAsset.h:45~50, 112, 121, 133, 192, 104~107 | `EAnimToTextureMode { Vertex, Bone }`, `USkeletalMesh` → `UStaticMesh` 매핑, `Precision`(8/16비트), `FAnimToTextureAnimInfo { StartFrame, EndFrame }` |
| 런타임 재생 데이터 | AnimToTextureInstancePlaybackHelpers.h:13~24, 27~59 | `FAnimToTextureFrameData { float Frame; float PrevFrame; }`, `FAnimToTextureAutoPlayData { TimeOffset, PlayRate, StartFrame, EndFrame }` |
| ISM 연결 | 같은 파일 75, 82, 90; cpp:29 | `SetupInstancedMeshComponent(UISMC*, int32 NumInstances, bool bAutoPlay)` 가 `NumCustomDataFloats` 를 구조체 크기로 설정; `BatchUpdateInstancesAutoPlayData/FrameData` 로 일괄 갱신 |
| ISM 트랜스폼 | InstancedStaticMeshComponent.h:388~397, 330~331, 375 | `BatchUpdateInstancesTransforms(int32 Start, TArrayView<const FTransform>, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)`, `SetCustomData(Start, End, TConstArrayView<float>, bool)` |

의미: 몬스터 한 마리가 스켈레탈 메시가 아니라 ISM 인스턴스 1개 + 실수 4개(자동 재생) 또는 2개(프레임 지정) 가 되며, CPU 쪽 애니메이션 비용은 0 이 된다. 대신 (1) 상태 전환 블렌딩이 없고(프레임 점프), (2) 소켓·본 위치를 CPU 에서 알 수 없어 히트 판정은 캡슐/원판으로 해야 하며, (3) 베이크 텍스처 크기(정점 수 × 프레임 수)가 커진다. 원거리·후열 군중용 LOD 로 적합하다.

### 4) 틱 조절 API 와 커스텀 틱 매니저

| 항목 | 근거 | 내용 |
|---|---|---|
| 액터/컴포넌트 API | Actor.cpp:1767~1770, ActorComponent.cpp:1783~1791 | `SetActorTickInterval(float)` 은 `PrimaryActorTick.TickInterval` 대입만; `SetComponentTickIntervalAndCooldown(float)` 은 `UpdateTickIntervalAndCoolDown` 으로 현재 쿨다운까지 덮어씀 |
| 필드 정의 | EngineBaseTypes.h:256~258, 375, 464 | `float TickInterval` "≤0 이면 매 프레임"; `UpdateTickIntervalAndCoolDown(float)` 은 게임 스레드 전용; `GetLastIntervalTickGameTime()` |
| 큐 스케줄링 | TickTaskManager.cpp:1474~1506 | `QueueAllTicks()`: `TickInterval>0` 인 함수는 `AllEnabledTickFunctions` 에서 빼고 `RescheduleForInterval` 로 쿨다운 리스트로 이동. 쿨다운 리스트는 `RelativeTickCooldown` 누적으로 머리부터 소비하며 `TickInterval - (DeltaSeconds - CumulativeCooldown)` 로 초과분 보정 |
| 리스트 삽입 | cpp:1393~1466 | `ScheduleTickFunctionCooldowns()`: 상대 쿨다운 정렬 삽입(연결 리스트) — 간격 틱 수 M 에 대해 삽입 O(M) |
| 전달 델타 | cpp:2825~2843 | `FTickFunction::CalculateDeltaTime(float, const UWorld*)`: `bWasInterval` 이면 `CurrentWorldTime - LastIntervalTickSeconds` 반환(누적 델타), 첫 회는 월드 델타. 기준 시간은 `GetTimeSeconds()`(일시정지 무시 틱이면 `GetUnpausedTimeSeconds()`) |
| 틱 그룹 | EngineBaseTypes.h:86~107 | `TG_PrePhysics, TG_StartPhysics, TG_DuringPhysics, TG_EndPhysics, TG_PostPhysics, TG_PostUpdateWork, TG_LastDemotable, TG_NewlySpawned` |
| 틱 함수 플래그 | EngineBaseTypes.h:224, 227, 230 | `bAllowTickBatching`("다른 틱과 결합 허용"), `bHighPriority`("그룹 안에서 먼저 실행"), `bRunOnAnyThread`("게임 스레드 밖 병렬 실행") |
| 프리레퀴짓 예 | MoverStandaloneLiaison.cpp:355~369; CharacterMovementComponent.cpp:11976~11979 | `AddPrerequisite(Object, TickFunction)` 로 컨트롤러 → 입력 → 시뮬 → 적용 순서 고정 |

발췌(누적 델타, TickTaskManager.cpp:2837~2842):
```cpp
const double CurrentWorldTime = (bTickEvenWhenPaused ? TickingWorld->GetUnpausedTimeSeconds() : TickingWorld->GetTimeSeconds());
if (InternalData->LastIntervalTickSeconds >= 0.0)
{
    DeltaTime = CurrentWorldTime - InternalData->LastIntervalTickSeconds;
}
InternalData->LastIntervalTickSeconds = CurrentWorldTime;
```

**커스텀 틱 매니저 패턴의 엔진 예**

| 예 | 근거 | 구조 |
|---|---|---|
| MassEntity | MassProcessingPhaseManager.h:54, 64, 90; cpp:60~61, 65~90, 161, 203, 481~482 | `struct FMassProcessingPhase : public FTickFunction` 하나가 단계(Phase)별로 등록되어 `ExecuteTick` 안에서 `UE::Mass::Executor::Run(*PhaseProcessor, Context)` 로 모든 엔티티 처리. `bStartWithTickEnabled=false`, 틱 그룹은 `Initialize(..., ETickingGroup, ...)` 로 지정, 우선순위 높음 |
| 애니메이션 예산 할당기 | AnimationBudgetAllocator.cpp:797~803, 172~262 | 월드 프리 액터 틱 델리게이트에서 한 번 실행, 컴포넌트 배열을 정렬하고 각 컴포넌트의 틱 활성/주기를 밀어 넣음 |
| 시그니피컨스 매니저 | SignificanceManager.h:35~36, 118, 121, 176; cpp:478~513 | `RegisterObject(UObject*, FName Tag, TFunction<float(FManagedObjectInfo*, const FTransform&)>, EPostSignificanceType, TFunction<void(FManagedObjectInfo*, float, float, bool)>)`; `Update(TArrayView<const FTransform> Viewpoints)` 가 `ParallelFor` 로 중요도 계산 후 태그별 `StableSort`. **엔진 코드 어디에서도 `Update` 를 호출하지 않는다(프로젝트가 GameViewportClient 나 서브시스템에서 호출해야 함)** |
| DetourCrowd | CrowdManager.cpp:231~278, NavigationSystem.cpp:1834 | `UCrowdManager::Tick(float)` 이 `dtCrowd->updateStep*` 를 순차 호출; 호출자는 `UNavigationSystemV1`(네비 시스템 틱 한 곳) |

장점 정리(코드 근거로 도출): (1) 액터 N 개 × 틱 함수 큐잉/태스크 생성 비용이 사라지고 배열 순회로 바뀐다(MassEntity 가 정확히 이 형태), (2) 고정 스텝을 서브시스템이 직접 관리하므로 `TickInterval` 의 월드 시간 종속(cpp:2837)을 피해 결정론을 확보할 수 있다, (3) 중요도 정렬 후 "상위 K 개만 이번 프레임" 같은 정책을 한 곳에서 적용한다(예산 할당기 방식), (4) `bRunOnAnyThread`/`ParallelFor` 로 병렬화하기 쉽다(시그니피컨스 매니저 cpp:493).

### 5) 충돌·판정 — 서브시스템 공간 해시로 오버랩 대체

| 항목 | 근거 | 내용 |
|---|---|---|
| 클래스 | HierarchicalHashGrid2D.h:34~35 | `template <int32 InNumLevels = 3, int32 InLevelRatio = 4, typename InItemIDType = uint32> class THierarchicalHashGrid2D` (AIModule 공개 헤더) |
| 설계 원리 | h:12~19 | 항목은 크기에 맞는 레벨의 **한 셀**에만 들어가고 이웃 셀로 최대 반 셀만큼 겹칠 수 있음 → 쿼리 시 상자를 확장해 보정. 격자에 못 넣는 큰 항목은 spill 리스트로 가 모든 쿼리에 포함 |
| 생성/초기화 | h:125~136, 147 | `THierarchicalHashGrid2D(const float InCellSize = 500.f)`, `Initialize(const float InCellSize)` — 레벨마다 `CellSize *= LevelRatio` |
| 갱신 API | h:180, 231, 313~331 | `FCellLocation Add(ID, const FBox&)`, `Remove(ID, const FBox&)`, `FCellLocation Move(ID, OldLocation, NewBounds)` — 셀이 바뀔 때만 Remove+Add |
| 쿼리 API | h:341~375, 384~400 | `QuerySmall(const FBox&, OutT&)`(작은 상자, 모든 레벨 단순 순회), `Query(const FBox&, OutT&)`(레벨별 사각형을 깊이 우선으로 순회, 빈 상위 셀은 건너뜀). 둘 다 **거짓 양성 가능** → 결과에 대해 실제 거리 검사 필요 |
| 저장 구조 | h:93~96, 633 | `TSet<FCell> Cells`(해시 버킷) + `FItem { ID; int32 Next }` 연결 리스트 |
| 엔진 사용처 | MassNavigationSubsystem.h:33, 43~51; SmartObjectHashGrid.h; InstancedActorsSubsystem.h; MassLookAtTypes.h | `typedef THierarchicalHashGrid2D<2, 4, FMassNavigationObstacleItem> FNavigationObstacleHashGrid2D;` 를 회피 장애물 격자로 사용 |

비용 모델(코드 구조에서 도출): 삽입/삭제는 셀 해시 조회 O(1) + 연결 리스트 조작, 이동은 셀이 바뀔 때만 발생. 반경 r 쿼리는 (r/CellSize+1)² 개 셀 × 셀 안 항목 수. 몬스터 300 마리, 셀 200cm, 탐색 반경 600cm 이면 대략 16~25 셀을 훑는다. 엔진 오버랩(`GetOverlapInfos`) 은 물리 씬 브로드페이즈 + 컴포넌트마다 `FOverlapInfo` 배열 유지 + Begin/End 이벤트 델리게이트 비용이 붙고, CMC 의 `ApplyRepulsionForce` 처럼 매 프레임 순회하는 소비자가 있다. 격자 방식은 "누가 근처에 있나" 만 답하고 정확한 판정(캡슐 대 캡슐, 부채꼴)은 결과 집합에 대해 직접 계산하므로 결정론 시뮬레이션과도 궁합이 맞다(물리 씬 상태에 의존하지 않음).

### 6) 탑다운 화면 밖 몬스터의 표현·로직 LOD 정책 설계 근거

엔진이 이미 제공하는 축과 기본값:

| 축 | 근거 | 엔진이 제공하는 것 |
|---|---|---|
| 렌더 여부 | SkinnedMeshComponent.cpp:1864; MovementComponent.cpp:339 | "최근 1초 렌더됨"(`bRecentlyRendered`), "최근 0.41초 렌더됨"(`bUpdateOnlyIfRendered`) 두 임계값이 코드에 고정 |
| 애니 가시성 옵션 | SkinnedMeshComponent.h:96~116; Character.cpp:124 | 화면 밖에서 몽타주만 진행(`OnlyTickMontagesWhenNotRendered`) 가능. 캐릭터 기본은 `AlwaysTickPose` 로 항상 그래프 갱신 |
| 비렌더 갱신 주기 | EngineTypes.h:2820 | URO 기본 4 프레임에 1회 |
| 화면 밖 애니 상한 | AnimationBudgetAllocatorParameters.h:94 | `MaxTickedOffsreenComponents=4` |
| 중요도 거리 | AnimationBudgetAllocatorParameters.h:165 | 자동 중요도 최대 거리 30000cm(300m) — 탑다운에서는 너무 커서 프로젝트 함수로 대체 필요 |
| 이동 로직 | CharacterMovementComponent.cpp:6465~6480, 834 | NavWalking 은 이미 0.1초 주기 투영 + 스폰 시 무작위 위상 분산 |
| 중요도 정렬 | SignificanceManager.cpp:478~513 | 뷰포인트 배열 대비 중요도 계산·정렬만 제공, 정책은 프로젝트 몫 |

설계 근거에서 나오는 정책 초안(수치는 제안이며 프로파일링으로 조정):

| 계층 | 판정(탑다운) | 이동 | 애니메이션 | 판정/충돌 | 근거가 되는 엔진 사실 |
|---|---|---|---|---|---|
| L0 근접 전투 | 화면 안 + 플레이어 반경 R0 | 매 프레임, 스윕 켬 | 스켈레탈 풀 틱 또는 예산 할당기 최상위 | 격자 + 정밀 판정 매 프레임 | CMC 스윕·바닥 검사가 유일하게 필요한 곳 |
| L1 화면 안 원거리 | 화면 안, R0 밖 | NavWalking, 스윕 끔(`bSweepWhileNavWalking=false`) | 예산 할당기 보간 틱(2~6 프레임) | 격자, 판정 2~3 프레임 | cpp:6064, Parameters.h:65 |
| L2 화면 밖 근처 | 화면 밖, 시뮬 반경 안 | 서브시스템 고정 스텝(예: 10Hz)로 위치만 갱신 | `OnlyTickMontagesWhenNotRendered` 또는 `bEnableAnimation=false`+시간표 | 격자만 | SkeletalMeshComponent.cpp:1883~1915, 1788 |
| L3 화면 밖 원거리 | 시뮬 반경 밖 | 위치 갱신 정지 또는 1Hz | 표현 없음(ISM 도 숨김) | 없음 | `bUpdateOnlyIfRendered`(0.41초) 와 같은 발상 |

주의점: (1) 애니메이션 시간을 멈추면 공격 판정도 멈추므로 "로직 시간표" 를 애니메이션과 분리해야 화면 밖에서도 전투가 결정론적으로 진행된다(3-3 절), (2) 화면 안팎 전환 시 `a.Budget.ForceTickWhenComponentExitsOffScreen`(CVars.cpp:39) 처럼 첫 프레임 강제 갱신을 두어 튀는 포즈를 막는다, (3) 탑다운은 카메라 거리가 거의 일정하므로 "거리" 보다 "화면 안/밖 + 플레이어까지 2D 거리 + 전투 참여 여부(어그로)" 를 중요도 입력으로 쓰는 편이 맞다.

## 프로젝트 적용 시사점

**쓸 것**
1. 몬스터 이동은 두 갈래로 나눈다. (a) 근접 정예/보스: `ACharacter`+CMC 유지하되 `bEnablePhysicsInteraction=false`, `bAlwaysCheckFloor=false`, `bUseRVOAvoidance=false`, `GroundMovementMode=MOVE_NavWalking`(h:915~919), 필요 시 `bSweepWhileNavWalking=false`. (b) 다수 잡몹: `APawn` + 프로젝트 서브시스템(고정 스텝, 배열 순회)이 위치를 갱신하고 `INavMovementInterface` 구현체(`UFloatingPawnMovement` 또는 자체 `UNavMovementComponent` 파생) 로 길찾기만 연결. 두 방식 모두 PathFollowing 이 인터페이스로 붙는다(PathFollowingComponent.cpp:1514).
2. 결정론 전투 시뮬레이터는 액터 틱에 의존하지 않는다. 몬스터 상태(위치·속도·쿨다운·몽타주 시간표 진행도)를 구조체 배열로 두고 서브시스템이 `Step(FixedDt)` 를 호출한다. Mover 의 `FMoverTickStartData → DoSimulationTick → FMoverTickEndData` 형태(KinematicActorSimulation.h:45)를 API 모양의 참고로만 쓴다. 기존 `FTDScopedCombatWorld` 픽스처(`World->Tick(LEVELTICK_All, Delta)`)는 유지하되, 서브시스템 `Step` 을 월드 틱 대신 직접 호출하는 경로를 추가하면 애니메이션·렌더 없이 수천 회 반복이 가능하다.
3. 몽타주 시간표 추출 커맨드릿(또는 에디터 유틸리티) 을 만든다: `GetPlayLength`, `GetSectionStartAndEndTime`, `Notifies[i].GetTriggerTime/GetDuration/NotifyName`, `BlendOut` 을 JSON 으로. 시뮬레이터와 실기 모두 같은 표를 읽어 판정 창을 열고 닫는다. 실기 검증은 `TDMeleeAttackNotifyTests` 방식으로 유지.
4. 주변 탐색·어그로·근접 판정 후보 수집은 `THierarchicalHashGrid2D<2,4,uint32>`(셀 200~300cm) 를 전투 서브시스템에 두고, 몬스터 캡슐의 오버랩 이벤트(`SetGenerateOverlapEvents(false)`) 는 끈다. CMC 의 `bSweepWhileNavWalking` 은 `GetGenerateOverlapEvents()` 값으로 초기화되므로(cpp:1046) 함께 정리된다.
5. 애니메이션은 `USkeletalMeshComponentBudgeted` 로 바꾸고 `OnCalculateSignificance` 델리게이트에 "화면 안 여부 + 플레이어 2D 거리 + 어그로" 함수를 바인딩한다. `AnimationBudgetAllocator` 플러그인은 현재 uproject 에 없으므로 활성화가 필요하다(프로젝트 설정 변경은 이 조사 범위 밖).
6. 후열 군중(수백 마리 이상)이 실제로 필요해지면 AnimToTexture 로 잡몹 1~2종을 베이크해 ISM 으로 그리고, 히트 반응은 색/스케일 커스텀 데이터로 대체한다.

**피할 것**
1. Mover 를 이동 엔진으로 채택하는 것(Experimental, 성능 미검증, NP 의존, 액터당 틱 함수 3개).
2. `bUseRVOAvoidance`(N²) 와 `bEnablePhysicsInteraction`(매 프레임 오버랩 순회) 를 잡몹에 켜두는 것.
3. `SetActorTickInterval` 만으로 결정론을 기대하는 것 — 델타는 월드 시간 차이라 프레임 길이에 따라 값이 달라진다(TickTaskManager.cpp:2840).
4. 화면 밖에서 애니메이션 시간을 멈추면서 판정은 애니메이션 노티파이에 의존하는 구조 — 시간표 분리 없이는 화면 밖 전투가 멈추거나 튄다.
5. `USignificanceManager` 를 등록만 하고 `Update` 를 호출하지 않는 것(엔진은 호출하지 않는다).

## 미확인·미해결 질문

1. CMC 의 `bAlwaysCheckFloor=false` 를 NavWalking 모드에서 썼을 때 `bCachedLocationStillValid` 분기(cpp:6120)와 정지 몬스터의 바닥 갱신이 실제로 몇 번 생략되는지 — 프로파일러(`STAT_CharFindFloor`, cpp:74)로 실측 필요.
2. `a.Budget.*` 콘솔 변수 중 `AnimationBudgetAllocatorCVars.cpp:208` 이후(예: `a.Budget.MaxTickRate`, `a.Budget.MaxTickedOffsreenComponents`)의 전체 목록은 파일 450줄 중 앞부분만 확인했다. 이름은 파라미터 구조체와 1:1 일 것으로 보이나 미확인.
3. `USkeletalMeshComponent::bEnableAnimation=false` 상태에서 `TickMontagesOnly` 가 루트모션 몽타주의 노티파이(`MontageTickType=BranchingPoint`) 를 그래프 없이도 정상 발화하는지는 코드상 `TriggerQueuedMontageEvents` 호출로 유추했을 뿐 실행 검증은 하지 않았다. 기존 노티파이 테스트 픽스처로 확인 권장.
4. AnimToTexture 의 텍스처 메모리 상한과 5.8 에서의 Nanite/ISM 호환(정점 애니메이션 머티리얼이 Nanite 메시에서 동작하는지) 은 코드로 확인하지 않았다(미확인).
5. `UMoverStandaloneLiaisonComponent::SetUseAsyncMovementSimulationTick` 의 워커 스레드 실행이 네비메시 `ProjectPoint` 와 안전한지(NavWalkingMode 가 게임 스레드 전용 API 를 쓰는지) 는 미확인 — Mover 를 채택하지 않는 전제라 추적하지 않았다.
6. `UCrowdManager` 기본 `MaxAgents=50`(CrowdManager.cpp:168) 을 늘렸을 때 DetourCrowd 의 근접 격자 비용이 어떻게 변하는지는 Detour 소스(ThirdParty)를 읽지 않아 미확인.
7. 웹 자료(2024~2026 공식 문서·강연)는 이번 조사에서 인용하지 않았다. 엔진 코드로 충분히 뒷받침되는 항목만 결론에 넣었고, "대량 몬스터 실측 수치" 는 프로젝트 프로파일링으로 채워야 한다.

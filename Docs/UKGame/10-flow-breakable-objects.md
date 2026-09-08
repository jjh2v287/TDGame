[← 인덱스로](../UKGame_FeatureFileMap.md)

# 10. 상세 흐름: 부서지는 오브젝트 (Breakable · 폴리지 파손 · 생활 오브젝트)

코드 본문까지 읽고 정리한 내용입니다(2026-09-09). "데미지를 받아 부서지는 것"은 네 계열로 나뉘며 파괴 방식과 진입 경로가 다릅니다.

| 계열 | 클래스 | 대상 예 | 파괴 방식 |
|---|---|---|---|
| A. 카오스 파괴형 | `AUKBreakableActor` | 나무상자, 항아리, 창문, 가구, 울타리 | 지오메트리 컬렉션 + 필드 시스템으로 조각냄 |
| B. 폴리지 승격형 | `UUKFoliageActorManager` → `AUKBreakTreeActor` / `AUKBreakableActor` / `AUKPhysicalInteractiveFoliageActor` | 나무, 덤불, 풀 | 인스턴스 폴리지를 액터로 바꾼 뒤 파손 |
| C. 생활 오브젝트형 | `ALifeObjectActor` | 광석 등 채집물 | 체력 어트리뷰트, 도구 상호작용 |
| D. 스텁 | `AUKDestructibleActor`, `AUKAttackableActor` | 없음 | 빈 구현 |

관련 파일
- `Source/UKGame/Actors/Breakable/UKBreakableActor.h/.cpp`, `Source/UKGame/Actors/Breakable/UKBreakableDefine.h`
- `Source/UKGame/Components/PhysicsObject/UKAttackableObjectComponent.h/.cpp`
- `Source/UKGame/Subsystems/FoliageSystem/UKFoliageActorManager.h/.cpp`, `Source/UKGame/Subsystems/FoliageSystem/UKFoliageActorDefined.h/.cpp`
- `Source/UKGame/Actors/Foliage/UKBreakTreeActor.h/.cpp`, `Source/UKGame/DataTable/UKCollectibleFoliageData.h`
- `Plugins/UKInteractiveFoliage/Source/UKInteractiveFoliage/Private/Actors/UKPhysicalInteractiveFoliageActor.cpp`
- `Source/UKGame/Actors/LifeObjectActor.h/.cpp`, `Source/UKGame/BehaviorTree/BehaviorTaskLifeObject.h/.cpp`
- 트리거 측: `Source/UKGame/Actors/UKPlayerCharacter.cpp`(구르기·낙하), `Source/UKGame/Components/Parkour/Action/UKParkourAction_BreakWindow.cpp`, `Source/UKGame/GameFramework/UKPlayerController.cpp`(폴리지 상호작용), `Source/UKGame/AbilitySystem/Execution/UKDamageExecution.cpp`

### 10.1 공통 피격 판정: UUKAttackableObjectComponent

"맞았는가"를 판정하고 세 델리게이트로 알린다. `OnSuccessfullyAttacked`(매 타격), `OnHitCountConditionMet`(히트 카운트 소진), `OnNotAvailableAttackType`(무기 종류 불일치).

입력 경로 세 가지
- 이벤트 경로: UKEventSystem 리스너로 소유 액터에 오는 `UK.Event.OnAttacked`(페이로드 `FUKOnAttackedEventParam`: 가해자, HitResult, 무기 타입 태그, CC 종류)를 받는다. **C++에서 이 이벤트를 대상 액터에게 발신하는 코드는 없다.** `UKDamageExecution`은 공격자 ASC에 GAS 이벤트로만 올리므로, 실제 발신은 무기·어빌리티 블루프린트(`UKEventBlueprintLibrary`)에서 하는 것으로 추정.
- 물리 충돌 경로(`OnActorHit`): 상대가 Movable이고 물리 시뮬레이션 중이며, `NormalImpulse / 질량`이 `VelocityTolerance`(200) 이상이고 임펄스가 `ImpulseTolerance`(2000)를 넘으면 공격으로 인정. `bUseOnlyPlayerAttack`이면 차단.
- 크래시 경로: 자신 또는 상대 속도가 `CrashHitVelocity`(500) 이상이면 `OnCrashHit`만 방송(파괴로 이어지지 않음).

판정 규칙(`OnAttackedEvent`)
- 무기 타입 검사: `AttackableTypeTags`가 비어 있거나 가해자에게 ASC가 없으면 무조건 통과. 태그가 있으면 가해자 ASC가 그중 하나를 보유해야 한다.
- `CurrentHitCount`를 1 줄이고 0이 되면 `HitCountConditionMet`. `bRepeatable`이면 카운트를 되돌려 반복 파손 가능.

### 10.2 카오스 파괴형: AUKBreakableActor

구성: `GeometryCollectionComponent`(콜리전 프로필 `AttackableObject`, 오버랩 생성), `FieldSystemComponent`, ASC(어트리뷰트 없이 태그·GE 대상용), `UUKAttackableObjectComponent`, 필드 모양 시각화 컴포넌트(구/박스/평면, 방향 화살표). `FUKFieldSettings`는 카오스 마스터 필드 파라미터(외부 변형력, 선형·각속도, 노이즈, 내부 변형 감쇠, 힘/속도 스위치, 다이내믹 전환)를 옮긴 데이터.

파괴 트리거(6개)
1. 히트 카운트 소진(`HitCountConditionMet`): `ActivationType`이 Trigger면 즉시, Delay면 `ActivationDelay` 후 파괴.
2. 무기 메시 오버랩(`OnGeometryCollectionBeginOverlap`): 플레이어 장비 메시(Weapon/Weapon2)가 겹치면 Visibility 채널 스윕으로 확인하고 전투 히트 큐를 재생한 뒤 히트 카운트 처리를 직접 호출. 1회 후 바인딩 해제.
3. 구르기 충돌(`UKPlayerCharacter.cpp` Tick, `Status_Rolling`): 매 틱 `AttackableObject` 프로필 캡슐 트레이스로 닿은 것을 카운트 무시하고 즉시 파괴.
4. 낙하 충격(`ApplyFallingDamage`): `bReceiveCharacterFallingDamage`인 것 위로 착지하면 아래 방향 임펄스 후 파괴 처리.
5. 파쿠르 창문 깨기(`UKParkourAction_BreakWindow`): 매달린 지점 아래 200cm를 Hit 채널로 스윕해 찾은 Breakable을 애님 이벤트 시점에 파괴.
6. 카오스 자체 파손(`OnChaosBreakEvent`): 다른 이유로 조각이 떨어지면 파괴 후처리를 따라잡음.

파괴 실행 순서(`ActivationBreak` → `PostActivationBreak`)
1. 조각 오브젝트 타입을 `FragmentObjectChannel`(기본 GameTraceChannel13 "Bush", 기본 응답 Ignore)로 변경.
2. `bFieldActive = true`, 블루프린트 `OnTrigger` 호출. **실제 필드 적용은 C++ 게터 값을 읽는 블루프린트가 수행**한다. C++는 값 보관과 상태 전이만 담당.
3. `OnPlayBreakSound(BreakSoundKey, 위치)`.
4. 가해자가 마을 안(`Status_InTown`)이면 `GamePlayEvent_Dispatch_HitProp` 환경 이벤트 발송.
5. 보상 드랍: `Rewards` 중 아이템·재화는 그대로, `RewardData` 타입은 `UServerRewardSystem::BuildRewardData`로 펼쳐 `URewardManager::K2_DropRewards`.
6. 부속 메시: 컴포넌트 태그 `BreakDestroy`는 제거, `BreakPhysicsActive`는 물리 켬. `DestroyExtraActors` 파괴, `PhysicsExtraActors` 물리 활성.
7. 디더링 페이드: 게임 설정 `DitheringMaterialMap`에 등록된 머티리얼만 디더 머티리얼로 교체하고 커브로 `[Opacity] Master Level`을 내린 뒤, `AutoDestroyActor`면 `DestroyDelay` 후 Destroy. 매핑이 없으면 페이드 없이 즉시 파괴.

### 10.3 폴리지 승격형: UUKFoliageActorManager

인스턴스드 폴리지는 액터가 아니라 맞을 수 없으므로, 맞는 순간 그 인스턴스만 제거하고 자리에 액터를 스폰한다.

데이터: `DT-CollectibleFoliage`에 메시 경로별 `MeshBreakCount`(내구), `StaticMeshMap`(교체 액터 클래스), `FoliageRestoreTime`(재생, 기본 60초), 열매 소켓 `_Collect`의 드랍 확률·리스폰 시간, 채집 아이템·수량. 레벨 로드 시 폴리지 컴포넌트마다 인스턴스 배열로 `FUKFoliageMeshData`를 만들어 `ActiveMap`(폴리지 액터 이름 + 메시 이름 키)에 등록.

타격 흐름: 매니저가 전역 `UK.Event.OnAttacked`를 구독. HitResult 컴포넌트가 등록된 폴리지면 `SpawnFoliageActor`가 (1) `BreakTree` 클래스를 동기 로드해 지연 스폰, (2) `FUKInteractFoliageRecord`로 인스턴스 인덱스와 커스텀 데이터를 전달, (3) `BreakTreeActorMap`에 보상·복원 시간·내구 기록, (4) `RemoveInstance`로 폴리지 인스턴스 제거(마지막 인스턴스와 스왑, 인덱스 재매핑).

교체 액터 세 종류
- `AUKBreakTreeActor`: 스켈레탈 나무 + 그루터기 + 자식 스태틱 메시 조각. `DoDamage(1)`마다 내구 1 감소, `BreakCountPerHit`(조각 수 / 최대 내구)개 조각을 무작위로 숨김. 내구 0 → `OnBroken`(BP) → `BreakFoliage`가 남은 조각을 숨기고 `BrokenPhysicsAsset`으로 바꿔 루트 이하 본 물리를 켜 쓰러뜨림. 상호작용(도끼질) 경로: 플레이어 컨트롤러가 `IsBreakableFoliage`로 판정해 생명 게이지 UI를 띄우고, 상호작용 시작 시 `OnInteract`로 액터를 스폰해 대상을 교체한 뒤 `UBehaviorTaskLifeObject`가 도구 공격력만큼 `DoDamage`.
- `AUKBreakableActor`(덤불 등): `ApplyFoliageDamage`가 남은 내구를 한 번에 소진시켜 즉시 파괴하고 복원을 예약.
- `AUKPhysicalInteractiveFoliageActor`(플러그인): 풀처럼 밀리는 물리 반응용. 매니저가 `bBreakable = true`로 스폰하는데 이 액터의 `Tick`은 `bBreakable`이 참이면 아무것도 하지 않아 비활성·복귀 로직이 돌지 않는다.

복원: `OnBreakFoliageActor`가 열매 소켓의 수집 액터를 떼어내고 리스폰 타이머, `FoliageRestoreTime` 후 `RestoreFoliage`를 예약. 복원은 인스턴스를 다시 추가하고 교체 액터를 파괴. 레벨 재로드 시(`ActiveFoliageMesh`) 내구 0인 인스턴스를 다시 제거하고, 복원 대기 중이 아니면 `SetBroken` 상태의 그루터기 액터를 대신 세운다.

### 10.4 생활 오브젝트형: ALifeObjectActor

ASC의 Health 어트리뷰트를 체력으로 쓰고, `IUKLifeObjectInterface::DoDamage`가 HP를 깎은 뒤 HP 비율에 따라 메시를 교체(`UpdateMesh`). 상호작용 컴포넌트와 `UBehaviorTaskLifeObject`(도구 부착, 몽타주 반복, 도구 공격력만큼 DoDamage)로 채집한다. 나무(`AUKBreakTreeActor`)도 같은 인터페이스를 구현해 UI(생명 게이지)와 상호작용 태스크를 공유한다.

### 10.5 콜리전 설정

- `AttackableObject` 프로필(`Config/DefaultEngine.ini`): 오브젝트 타입 WorldStatic, Weapon 채널 Overlap, Hit·Projectile·Interaction Block, Camera·Building Ignore. 무기 오버랩 경로와 구르기 트레이스가 이 프로필에 의존.
- 조각 채널 `Bush`(GameTraceChannel13, 기본 Ignore), `BreakedMesh`(GameTraceChannel14, 기본 Block). `ECC_HIT`는 GameTraceChannel3(`Source/UKGame/Common/UKCollisionTypes.h`).

### 10.6 코드에서 확인된 위험 요소와 특이점

- **OnAttacked 발신처가 C++에 없음**: 무기 타입 검사와 히트 카운트가 걸린 정식 경로는 블루프린트 발신에 의존. 어떤 BP가 보내는지 확인 필요.
- **무기 오버랩 경로의 하드코딩**: `OptionalName = "Heroine_Combo_01_01"`이 TODO로 박혀 히트 큐가 항상 같은 연출로 재생. 첫 오버랩 후 바인딩을 해제하므로 `HitCount`가 2 이상인 오브젝트는 이 경로로 두 번째 타격을 받지 못함.
- **무기 타입 우회**: 가해자에게 ASC가 없으면(물리 충돌, 굴러온 바위) `AttackableTypeTags`가 무시됨.
- **구르기 즉시 파괴**: 히트 카운트·무기 타입을 모두 건너뛰고 캡슐에 닿는 모든 Breakable을 부숨. 의도 확인 필요.
- **PhysicalInteractiveFoliageActor의 빈 분기**: 밟힌 풀이 원래 인스턴스로 돌아가지 않고 액터로 남음(수명 관리 없음).
- **액터 이름 문자열 키**: `BreakTreeActorMap`·`ActiveMap`이 `GetName()`을 키로 사용. `UUKAssetInstanceManager` 풀링으로 액터가 재사용되면 이름 충돌 가능.
- **BreakTree 조각 계산**: `BreakCountPerHit`가 정수 나눗셈이라 조각 수가 내구보다 적으면 0(타격 중 시각 변화 없음), `MaxBreakCount`가 0이면 조각이 숨겨지지 않음(주석 처리된 ensure가 흔적).
- **DestructibleActor·AttackableActor는 스텁**: `OnImpact`는 빈 구현이고 호출처 없음. `AUKAttackableActor`는 `UKSimplePointMoveActor`의 부모로만 사용.
- **디더링 의존성**: 게임 설정 머티리얼 맵에 없는 머티리얼을 쓰는 오브젝트는 페이드 없이 즉시 사라짐.

[← 인덱스로](../UKGame_FeatureFileMap.md)

# 2. 어빌리티 · 전투 · 이동 액션

### 게임플레이 어빌리티 기반 구조 (GAS 코어)
- 역할: 프로젝트 전용 GameplayAbility/데이터에셋/이펙트컨텍스트 등 어빌리티 시스템 공통 토대
- 파일: `Source/UKGame/AbilitySystem/Ability/UKGameplayAbility.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameplayAbilityTypes.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameplayAbilityData.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameplayAbilityDataAsset.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameAbilitySystemGlobals.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameplayEffectContext.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameAbilitySourceInterface.h/.cpp` , `Source/UKGame/AbilitySystem/UKAbilityTargetTypes.h/.cpp` , `Source/UKGame/AbilitySystem/UKGameplayTagPropertyMap.h/.cpp` , `Source/UKGame/AbilitySystem/UKCustomApplicationRequirement_HitActor.h/.cpp`

### 어트리뷰트/스탯
- 역할: 체력·스태미나·각성치 등 캐릭터와 플레이어 상태 수치 정의
- 파일: `Source/UKGame/AbilitySystem/Attribute/UKAttributeSet.h/.cpp` , `Source/UKGame/AbilitySystem/Attribute/UKPlayerStateAttributeSet.h/.cpp`

### 게임플레이 이펙트 / 버프 UI 데이터
- 역할: 지속 효과(버프·디버프) 정의와 UI 표시용 부가 데이터
- 파일: `Source/UKGame/AbilitySystem/Effect/UKGameplayEffect.h/.cpp` , `Source/UKGame/AbilitySystem/Effect/UKGameplayEffectUIData.h/.cpp`

### 데미지 계산 (Execution)
- 역할: 피해량·그로기·경직·스태미나·각성 포인트 산출 계산식 모음
- 파일: `Source/UKGame/AbilitySystem/Execution/UKDamageExecution.h/.cpp` , `Source/UKGame/AbilitySystem/Execution/UKCombatExecution.h/.cpp` , `Source/UKGame/AbilitySystem/Execution/UKGroggyDamageExecution.h/.cpp` , `Source/UKGame/AbilitySystem/Execution/UKPhysicalCollisionDamageExecution.h/.cpp` , `Source/UKGame/AbilitySystem/Execution/UKCrowdControlExecution.h/.cpp` , `Source/UKGame/AbilitySystem/Execution/UKStaminaExecution.h/.cpp` , `Source/UKGame/AbilitySystem/Execution/UKAwakeningPointExecution.h/.cpp`

### 전투 피격 연출 (GameplayCue)
- 역할: 타격 시 이펙트·사운드 재생 큐 및 나이아가라 파라미터 데이터
- 파일: `Source/UKGame/AbilitySystem/UKGameplayCueNotify_CombatHit.h/.cpp` , `Source/UKGame/AbilitySystem/UKNiagaraData.h`

### 입력-어빌리티 바인딩 / 입력 버퍼
- 역할: DefaultInput 기반 입력 태그와 어빌리티 연결, 선입력 버퍼 데이터 구성
- 파일: `Source/UKGame/AbilitySystem/Input/UKAbilityInputData.h/.cpp`

### 기본 액션 어빌리티 (점프·질주·슬라이딩·사망 등)
- 역할: 플레이어가 직접 체감하는 이동·상태 계열 개별 어빌리티
- 파일: `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_Jump.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKInstancedAbility_Sprint.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_Sliding.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_Death.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_AFK.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_GnomePeek.h/.cpp` (특정 캐릭터 엿보기 연출, 추정)

### 공격 어빌리티 / 피격 반응
- 역할: 근접 공격, 스윕 판정, 각성, 그로기, 피격 경직(DamageBreak), 시간정지
- 파일: `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_Attack.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_SweepAttackCollision.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/UKAbility_Awakening.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/UKAbility_Groggy.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/UKAbility_DamageBreak.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_Timestop.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_ApplyAnimRateScale.h/.cpp`

### 스킬 어빌리티 / AI 공격 어빌리티
- 역할: 스킬 데이터 기반 어빌리티 베이스와 AI 전용 공격 어빌리티, AI 히트박스·원거리 데이터
- 파일: `Source/UKGame/AbilitySystem/Ability/UKSkillAbility.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/AI/UKSkillAbility_AIAttackBase.h/.cpp` , `Source/UKGame/AbilitySystem/UKAIAttackHitBoxData.h` , `Source/UKGame/AbilitySystem/UKAIRangeAttackData.h`

### 어빌리티 상태 머신 / 스플라인 이동
- 역할: 어빌리티 내부 상태 전이 관리와 스플라인 경로 강제 이동
- 파일: `Source/UKGame/AbilitySystem/Ability/UKAbility_StateMachine.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/UKAbility_SplineMove.h/.cpp`

### 오브젝트 밀기/당기기 상호작용
- 역할: 상자 등 물체를 밀고 당기는 상호작용 어빌리티
- 파일: `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_PushObject.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_PushObject_Act.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_PushPullObject.h/.cpp`

### 클라이밍 (암벽·로프 등반)
- 역할: 등반 진입·이동·웅크림·낙하·로프 탐지 등 클라이밍 전반 어빌리티와 공용 정의
- 파일: `Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/UKAbility_ClimbingBase.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/UKAbility_Climbing.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/UKAbility_ClimbingAction.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/UKAbility_ClimbingRope.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/UKClimbFunctionLibrary.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/UKClimbingDefined.h/.cpp` , 외 5쌍(`Source/UKGame/AbilitySystem/Ability/Instanced/Climbing/` — ClimbingWalk, ClimbingCrouch, ClimbingFalling, TraceRope, ClimbingActionStruct)

### 클라이밍 지형(레지) 컴포넌트
- 역할: 스플라인 기반 등반 가능 모서리 정의와 붙잡기 지점·반응 데이터 제공
- 파일: `Source/UKGame/Components/Climbing/UKClimbingLedgeComponent.h/.cpp`

### 파쿠르 이동 어빌리티
- 역할: 벽·사다리·봉·바(bar) 등 지형 지물 이동과 볼트(뛰어넘기) 어빌리티
- 파일: `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_ClimbMovement.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_WallMovement.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_LadderMovement.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_PoleMovement.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_MovementOnBar.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/Parkour/UKAbility_Vault.h/.cpp`

### 파쿠르 컴포넌트 / 액션 세트
- 역할: 파쿠르 감지·상태 관리 컴포넌트와 개별 동작(매달리기 이동, 매달림 착지, 방향 전환, 낙하, 창문 파괴 등) 액션 구현
- 파일: `Source/UKGame/Components/Parkour/UKParkourComponent.h/.cpp` , `Source/UKGame/Components/Parkour/ParkourTypeDefine.h/.cpp` , `Source/UKGame/Components/Parkour/Action/UKParkourAction.h/.cpp` (액션 베이스) , `Source/UKGame/Components/Parkour/Action/UKParkourAction_Shimmy.h/.cpp` , `Source/UKGame/Components/Parkour/Action/UKParkourAction_Magnetic.h/.cpp` , `Source/UKGame/Components/Parkour/Action/UKParkourAction_VerticalClimb.h/.cpp` , `Source/UKGame/Components/Parkour/Action/UKParkourAction_ExitJump.h/.cpp` , 외 17쌍(`Source/UKGame/Components/Parkour/Action/` — WallShimmy, PoleShimmy, HangLand, HangoverTurn, TurnAround, StandOn, Drop, DropLadder, SlideDown, SlideDownLadder, ClimbToVault, BreakWindow, ExplicitMove, ImplicitMove, ImplicitMoveBar, Magnetic_Wall, Magnetic_Vertical, Magnetic_Ladder)

### 엎드리기(Prone) 시스템
- 역할: 엎드림 자세 전용 캐릭터·애님 인스턴스 처리
- 파일: `Source/UKGame/AbilitySystem/ProneSystem/ProneSystemCharacter.h/.cpp` , `Source/UKGame/AbilitySystem/ProneSystem/ProneSystem_AnimInstance.h/.cpp`

### 어빌리티 태스크 공통/유틸
- 역할: 어빌리티 진행 중 지연·값 보간·태그 부여·몽타주 재생 등 비동기 태스크 베이스와 유틸
- 파일: `Source/UKGame/AbilitySystem/Task/UKAbilityTask.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_WaitDelay.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_AddGameplayTags.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_PlayMontageAndWaitWithBlendIn.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_RepeatAction.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_UpdateScalarParameter.h/.cpp` , 외 4쌍(`Source/UKGame/AbilitySystem/Task/` — AdditiveFloat, AdditiveInteger, AdditiveInterpFloat, ConditionalAdditiveFloat)

### 어빌리티 태스크 - 이동/회전
- 역할: 목표 지점 이동, 스플라인 이동, 아바타·액터 회전 갱신
- 파일: `Source/UKGame/AbilitySystem/Task/UKAbilityTask_MoveToLocation.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_MoveAlongSpline.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_UpdateAvatarRotation.h/.cpp` , 외 3쌍(`Source/UKGame/AbilitySystem/Task/` — RotateAvatarWithAngle, UpdateActorRotation, UpdateActorRotationToTarget)

### 어빌리티 태스크 - 지형/충돌 감지
- 역할: 장애물 붙잡기·미끄러짐 트레이스, 상호작용 트레이스, 지면 거리·속도·충돌 대기
- 파일: `Source/UKGame/AbilitySystem/Task/UKAbilityTask_UpdateObstacleTraceBase.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_WaitObstacleGrabTrace.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_UpdateObstacleGrab.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_WaitInteractionTrace.h/.cpp` , `Source/UKGame/AbilitySystem/Task/UKAbilityTask_WaitPrimitiveCollision.h/.cpp` , 외 3쌍(`Source/UKGame/AbilitySystem/Task/` — WaitObstacleSlidingTrace, UpdateObstacleSliding, WaitGroundDistance, WaitVelocityLessThan)

### 어빌리티 태스크 - 입력 대기
- 역할: 입력 태그 눌림·해제·이동 입력 방향 감지 등 입력 기반 태스크
- 파일: `Source/UKGame/AbilitySystem/Task/Input/UKAbilityTask_WaitInputTagTriggered.h/.cpp` , `Source/UKGame/AbilitySystem/Task/Input/UKAbilityTask_WaitMovementInput.h/.cpp` , 외 6쌍(`Source/UKGame/AbilitySystem/Task/Input/` — WaitInputPress, WaitInputTagEvent, WaitInputTagCompleted, WaitInverseMovementInput, WaitMovementInputEnd, RemoveMovementInput)

### 어빌리티 태스크 - 루트모션/밀어내기
- 역할: 일정 힘의 루트모션 적용 및 대상 액터 밀어내기 데이터 적용
- 파일: `Source/UKGame/AbilitySystem/Task/RootMotion/UKAbilityTask_ApplyRootMotionConstantForce.h/.cpp` , `Source/UKGame/AbilitySystem/Task/RootMotion/UKAbilityTask_ApplyRootMotionConstantForceToActor.h/.cpp` , `Source/UKGame/AbilitySystem/Task/RootMotion/UKAbilityTask_ApplyPushDataToActor.h/.cpp`

### 데미지 메시지 수신 태스크
- 역할: 피해 발생 메시지를 구독해 어빌리티 흐름에 연결
- 파일: `Source/UKGame/AbilitySystem/Task/UKAbilityTask_ListenForDamageMessage.h/.cpp`

### 애니메이션 커스텀 이벤트 데이터
- 역할: 애님 노티파이 기반 커스텀 이벤트(IK 등) 데이터 정의
- 파일: `Source/UKGame/AbilitySystem/CustomEventData/UKCustomEventData.h/.cpp`

### 스킬 컴포넌트 / 콤보 시스템
- 역할: 캐릭터 스킬 등록·쿨다운·타겟 탐색 관리와 콤보 트리 진행
- 파일: `Source/UKGame/Components/Skill/UKSkillComponent.h/.cpp` , `Source/UKGame/Components/Skill/UKSkillModule.h/.cpp` , `Source/UKGame/Components/Skill/UKComboModule.h/.cpp` , `Source/UKGame/Components/Skill/UKComboNode.h/.cpp`

### 전투 타겟 탐색 규칙 / 스윕 판정 데이터
- 역할: 범위·사각형·부채꼴 등 탐색 규칙 태스크와 공격 스윕 콜리전 형태 정의
- 파일: `Source/UKGame/Combat/UKSearchRuleTask.h/.cpp` , `Source/UKGame/Combat/SweepCollisionData.h`

### 스킬 오브젝트 (투사체·충격파·장판)
- 역할: 스킬로 생성되는 액터(투사체, 폭발, 충격파, 범위 장판)와 이를 발사하는 어빌리티
- 파일: `Source/UKGame/Actors/SkillObject/UKSkillObjectBase.h/.cpp` , `Source/UKGame/Actors/SkillObject/UKProjectileBase.h/.cpp` , `Source/UKGame/Actors/SkillObject/UKShockwaveBase.h/.cpp` , `Source/UKGame/Actors/SkillObject/UKAoEObjectBase.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/SkillObject/UKAbility_ProjectileAttack.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/SkillObject/UKAbility_ProjectileExplosionAttack.h/.cpp` , `Source/UKGame/AbilitySystem/Ability/Instanced/SkillObject/UKAbility_ShockwaveAttack.h/.cpp`

### 공격 토큰 시스템 (적 공격 순서 조율)
- 역할: 다수 적이 동시에 달려들지 않도록 공격 권한(토큰)을 배분하고 조건 충족 시 총공격 발동
- 파일: `Source/UKGame/Subsystems/AttackTokenSystem/UKCombatTokenManager.h/.cpp` , `Source/UKGame/Subsystems/AttackTokenSystem/UKAllOutAttackSystem.h/.cpp`

### AI 내비게이션 점프
- 역할: 내비링크 구간을 AI가 점프로 통과
- 파일: `Source/UKGame/AbilitySystem/Ability/Instanced/UKAbility_NavLinkJump.h/.cpp`

### 탑승(말) 이동 컴포넌트
- 역할: 말 이동 물리와 피벗 턴 요청 처리
- 파일: `Source/UKGame/Components/Movement/UKHorseMovementComponent.h/.cpp`

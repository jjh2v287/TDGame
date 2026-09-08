[← 인덱스로](../UKGame_FeatureFileMap.md)

# 3. AI · NPC · 스테이트 머신

### AI 캐릭터 기반 클래스 (몬스터/NPC 공통)
- 역할: 모든 AI 캐릭터의 베이스 폰과 컨트롤러. 비헤이비어 트리(BT), 스테이트 트리(ST), HTN(Hierarchical Task Network, 계층적 태스크 네트워크), 스테이트 머신 컴포넌트를 한 컨트롤러에 통합 보유
- 파일: `Source/UKGame/AI/UKAICharacter.h/.cpp` , `Source/UKGame/AI/UKAIController.h/.cpp` , `Source/UKGame/AI/UKAIDefine.h`

### AI 시야/감지 (플레이어 발각)
- 역할: 커스텀 시각 감지 센스와 인지 컴포넌트로 플레이어·오브젝트 발견 이벤트를 델리게이트로 전파
- 파일: `Source/UKGame/AI/UKAISense_Sight.h/.cpp` , `Source/UKGame/Components/AI/UKAIPerceptionComponent.h/.cpp`

### 어그로(적대도) 관리
- 역할: 피격·감지에 따라 AI의 어그로 모드와 타겟을 전환하고 변경 이벤트를 방송
- 파일: `Source/UKGame/Components/AI/UKAggroComponent.h/.cpp`

### AI 이동 (경로/스플라인/단순 이동)
- 역할: AI 전용 캐릭터 무브먼트와, 물리 없이 가볍게 굴리는 단순 이동(군중/소형 동물용) 제공
- 파일: `Source/UKGame/Components/AI/UKAIMovementComponent.h/.cpp` , `Source/UKGame/Components/AI/UKSimpleMovementComponent.h/.cpp`

### 군중 NPC (도시 배경 인구)
- 역할: 배경을 채우는 저비용 군중 캐릭터. 수동 틱 인터페이스로 업데이트 부하 분산
- 파일: `Source/UKGame/AI/UKCrowdCharacter.h/.cpp` , `Source/UKGame/Actors/Spawner/UKMassPedestrianSpawner.h/.cpp`

### NPC 업데이트 부하 분산 (LOD 틱)
- 역할: 다수 NPC의 틱을 매니저가 모아 분산 호출. 거리·중요도 기반 갱신 최적화
- 파일: `Source/UKGame/Subsystems/NPC/UKNPCUpdateManager.h/.cpp` , `Source/UKGame/Subsystems/NPC/UKNPCUpdateInterface.h` , `Source/UKGame/Subsystems/NPC/UKTestManualTickAI.h/.cpp`

### NPC 욕구 기반 일과 행동 (Needs)
- 역할: 배고픔·휴식 등 욕구 수치를 시간 단위로 갱신해 다음 행동(HTN 계획)을 선택·기록
- 파일: `Source/UKGame/Components/AI/UKNeedComponent.h/.cpp`

### NPC 최상위 의사결정 (스테이트 트리)
- 역할: NPC의 상위 상태(대기/전투/날씨 반응/인사)를 스테이트 트리로 결정하고 하위 BT·HTN을 기동
- 파일: `Source/UKGame/Components/AI/UKAIStateTreeComponent.h/.cpp` , `Source/UKGame/StateTree/Schema/UKStateTreeAIComponentSchema.h/.cpp` , `Source/UKGame/StateTree/UKStateTreeCommonData.h` , `Source/UKGame/StateTree/Task/UKStateTreeTaskBase.h/.cpp` , `Source/UKGame/StateTree/Task/UKStateTreeGlobalTask.h/.cpp`

### 스테이트 트리 상황 판정 (조건/평가자)
- 역할: 비 여부, 경비 직업의 전투 여부, 인사 대상 유무, 공포·외침 감지, 로딩 상태 등을 평가해 상태 전환 근거 제공
- 파일: `Source/UKGame/StateTree/Condition/UKSTCond_IsRain.h/.cpp` , `Source/UKGame/StateTree/Condition/UKSTCond_IsGuardJobCombat.h/.cpp` , `Source/UKGame/StateTree/Condition/UKSTCond_CheckTargetOnGreeting.h/.cpp` , `Source/UKGame/StateTree/Evaluator/UKSTEval_CheckOnFear.h/.cpp` , `Source/UKGame/StateTree/Evaluator/UKSTEval_CheckOnShout.h/.cpp` , `Source/UKGame/StateTree/Evaluator/UKSTEval_CheckOnGreeting.h/.cpp` , 외 2개(`Source/UKGame/StateTree/Evaluator/`)

### NPC 우천 반응 (우산 펴기/뛰기)
- 역할: 비가 오면 우산을 꺼내 펴고, 그치면 접어 숨기며, 상황에 따라 비를 피해 달리는 연출
- 파일: `Source/UKGame/StateTree/Task/UKSTTask_OpenUmbrella.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_FoldUmbrella.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_ShowUmbrella.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_HideUmbrella.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_RunInRain.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_CheckOnRain.h/.cpp`

### NPC 인사·상호 반응
- 역할: 플레이어나 다른 NPC를 향해 인사 모션을 재생하는 스테이트 트리 태스크
- 파일: `Source/UKGame/StateTree/Task/UKSTTask_Greeting.h/.cpp`

### 스테이트 트리 → 하위 로직 연결
- 역할: 상위 스테이트 트리에서 전투용 BT를 지정·실행하거나, 욕구 기반 행동 계획(HTN)을 시작시키는 브리지
- 파일: `Source/UKGame/StateTree/Task/UKSTTask_RunBehaviorTree.h/.cpp` , `Source/UKGame/StateTree/Evaluator/UKSTEval_SetCombatBehaviorTree.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_StartBehaviorPlanFromNeeds.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_AddGameplayTag.h/.cpp` , `Source/UKGame/StateTree/Task/UKSTTask_UKDelay.h/.cpp`

### NPC 행동 계획 (HTN)
- 역할: 상위 컴포지트 태스크가 하위 프리미티브 태스크 트리를 계획으로 전개. 이동·회전·몽타주·스플라인 이동·하위 BT 실행 등 실제 행동 단위 수행
- 파일: `Source/UKGame/HTN/Task/UKHTNCompositeTask.h/.cpp` , `Source/UKGame/HTN/Task/UKHTNPrimitiveTask.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/UKHTNTask_Move.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/UKHTNTask_MoveAround.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/UKHTNTask_Rotate.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/UKHTNTask_PlayMontage.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/UKHTNTask_SplineMove.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/UKHTNTask_RunSubBehaviorTree.h/.cpp`

### HTN 기반 스마트오브젝트 이용 (NPC 생활 행동)
- 역할: 욕구를 평가해 행동을 고르고, 적합한 스마트오브젝트(의자·모닥불 등)를 찾아 슬롯 위치로 이동해 사용하고 완료를 기록
- 파일: `Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/UKHTNTask_SelectBehaviorFromNeeds.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/UKHTNTask_EvaluationBehaviorFromNeeds.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/UKHTNTask_FindSmartObject.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/UKHTNTask_FindSmartObjectFromReserved.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/UKHTNTask_SetMoveTargetLocationToSlotLocation.h/.cpp` , `Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/UKHTNTask_WaitForSmartObjectDuration.h/.cpp` , 외 2개(`Source/UKGame/HTN/Task/PrimitiveTask/SmartObject/`)

### 스마트오브젝트 행동 정의 (상호작용 지점 데이터)
- 역할: 슬롯별로 어떤 행동(몽타주, BT, HTN, 점프, 대기, 아무것도 안 함, 점유 그룹, 클레임 차단)을 수행할지 정의하는 데이터 자산군
- 파일: `Source/UKGame/SmartObject/UKSmartObjectBehaviorBaseDefinition.h` , `Source/UKGame/SmartObject/UKSmartObjectCommonBehaviorDefinition.h` , `Source/UKGame/SmartObject/UKMontageBehaviorDefinition.h/.cpp` , `Source/UKGame/SmartObject/UKSmartObjectBehaviorTreeDefinition.h` , `Source/UKGame/SmartObject/UKHtnSmartObjectBehaviorDefinition.h/.cpp` , `Source/UKGame/SmartObject/UKSmartObjectJumpBehaviorDefinition.h` , 외 4개(`Source/UKGame/SmartObject/`)

### 몬스터 전투 행동 (비헤이비어 트리 코어)
- 역할: 전투 AI 실행 기반 클래스와 인터페이스. 배회·도주·태그 대기 등 캐릭터가 구현해야 할 계약 정의
- 파일: `Source/UKGame/BehaviorTree/BehaviorTreeBase.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTaskBase.h/.cpp` , `Source/UKGame/BehaviorTree/Decorators/UKBTDecoratorBase.h/.cpp` , `Source/UKGame/BehaviorTree/Services/UKBTServiceBase.h/.cpp` , `Source/UKGame/BehaviorTree/Interface/UKBehaviorTreeInterface.h/.cpp`

### 몬스터 전투 판단 (데코레이터/서비스)
- 역할: 타겟이 사거리·시야각 안인지, 전투 진입 조건인지, 사이 공간이 비었는지 검사하고 총공격 이벤트와 보유 태그를 갱신
- 파일: `Source/UKGame/BehaviorTree/Decorators/UKBTDecorator_TargetInDistance.h/.cpp` , `Source/UKGame/BehaviorTree/Decorators/UKBTDecorator_TargetInAngle.h/.cpp` , `Source/UKGame/BehaviorTree/Decorators/UKBTDecorator_ConditionsForBattle.h/.cpp` , `Source/UKGame/BehaviorTree/Decorators/UKBTDecorator_AllOutAttackEvent.h/.cpp` , `Source/UKGame/BehaviorTree/Decorators/UKBTDecorator_EmptyBetween.h/.cpp` , `Source/UKGame/BehaviorTree/Services/UKBTService_OwningTags.h/.cpp`

### 몬스터 이동/추적 BT 태스크
- 역할: 타겟 추적, 스폰 지점 복귀, 스플라인 이동, 높이 무시 이동, 회전, 배회, 플레이어로부터 도주 등 전투 이동 전반
- 파일: `Source/UKGame/BehaviorTree/Tasks/UKBTTask_MoveToTarget.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_HomingToTarget.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_MoveToSpawnPosition.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_SplineMove.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_Roam.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_RunAwayFromPlayer.h/.cpp` , 외 5개(`Source/UKGame/BehaviorTree/Tasks/`: MoveTo, MoveToIgnoreZ, MoveToClaimSlot, RotateTo, MoveAndLookAt)

### 전투 토큰 (동시 공격 인원 제한)
- 역할: 여러 몬스터가 동시에 달려들지 않도록 공격 권한 토큰을 요청·반납. 피격 시각 초기화 포함 (추정)
- 파일: `Source/UKGame/BehaviorTree/Tasks/UKBTTask_CombatTokenRequestAndRelease.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_ResetLastDamagedTime.h/.cpp`

### BT 기반 스마트오브젝트/가구 이용
- 역할: 스마트오브젝트 탐색·슬롯 점유·행동 트리 주입·종료 처리, 실내 가구 선택과 이용, 비 피하는 대피처 배회
- 파일: `Source/UKGame/BehaviorTree/Tasks/UKBTTask_FindSmartObject.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_OccupySmartObjectSlot.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_InjectSmartObjectBehaviorTreeFromBlackboard.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_FinishSmartObjectBehavior.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_FindBestFurniture.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_InteractingFurniture.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_WanderingShelter.h/.cpp`

### BT 연출/대기 태스크
- 역할: 몽타주 재생, 행동 정의로부터 몽타주 지정, 태그 조건 대기
- 파일: `Source/UKGame/BehaviorTree/Tasks/UKBTTask_PlayMontage.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_SetMontageFromBehaviorDefinition.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/UKBTTask_WaitWithTag.h/.cpp`

### NPC 전용 BT 서브트리 (일상 행동)
- 역할: 욕구 행동 기록·완료, 공통 행동 몽타주 획득, 슬롯으로 이동, 플레이어 쪽 회전, 대기·스마트오브젝트 지속시간 대기
- 파일: `Source/UKGame/BehaviorTree/Tasks/NPCBehavior/UKBTTask_RecordBehaviorFromNeeds.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/NPCBehavior/UKBTTask_CompleteBehaviorFromNeeds.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/NPCBehavior/UKBTTask_GetMontageFromCommonBehavior.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/NPCBehavior/UKBTTask_SetRotateTargetRotationToPlayer.h/.cpp` , 외 3개(`Source/UKGame/BehaviorTree/Tasks/NPCBehavior/`)

### 상호작용 오브젝트 행동
- 역할: 상호작용 준비 및 생명체형 오브젝트의 상호작용 반응 처리
- 파일: `Source/UKGame/BehaviorTree/BehaviorTaskInteraction.h/.cpp` , `Source/UKGame/BehaviorTree/BehaviorTaskLifeObject.h/.cpp` , `Source/UKGame/BehaviorTree/Tasks/BTTask_PrepareInteraction.h/.cpp`

### 캐릭터 이동 스테이트 머신 (Logic Driver)
- 역할: 접지/낙하/벽 슬라이드 등 이동 상태와 대화 상태를 노드 기반 스테이트 머신으로 관리. 어빌리티 발동·트리거 볼륨으로 전이
- 파일: `Source/UKGame/StateMachine/UKSMInstance.h/.cpp` , `Source/UKGame/StateMachine/SMState_Native.h/.cpp` , `Source/UKGame/StateMachine/SMState_Movement.h/.cpp` , `Source/UKGame/StateMachine/SMState_Grounded.h/.cpp` , `Source/UKGame/StateMachine/SMState_Falling.h/.cpp` , `Source/UKGame/StateMachine/SMState_WallSlide.h/.cpp` , `Source/UKGame/StateMachine/SMState_Talking.h/.cpp` , `Source/UKGame/StateMachine/SMTransition_ActivateAbility.h/.cpp` , `Source/UKGame/StateMachine/SMTransition_TriggerVolume.h/.cpp` , `Source/UKGame/StateMachine/SMStateCommon.h/.cpp` , `Source/UKGame/StateMachine/SMTransitionUtil.h`

### 보스 페이즈 진행
- 역할: 보스 전용 스테이트 머신으로 페이즈 상태와 전환을 정의하고, 매니저가 보스별 스테이트 머신 클래스를 로드·구동
- 파일: `Source/UKGame/StateMachine/BossPhase/UKSMInstanceBoss.h/.cpp` , `Source/UKGame/StateMachine/BossPhase/SMState_BossPhase.h/.cpp` , `Source/UKGame/StateMachine/BossPhase/SMTransition_PhaseTransitionBase.h/.cpp` , `Source/UKGame/Subsystems/BossPhaseManager/BossPhaseManager.h/.cpp`

### 퀘스트 진행 스테이트 머신
- 역할: 퀘스트 진행/달성/완료 상태와 순서·달성 카운트 전환, 몬스터 처치(개체·그룹·종족) 및 상호작용 대기, 조건 규칙 판정
- 파일: `Source/UKGame/StateMachine/Quest/SMState_Quest.h/.cpp` , `Source/UKGame/StateMachine/Quest/SMTransition_Quest.h/.cpp` , `Source/UKGame/StateMachine/Quest/SMTransition_Battle.h/.cpp` , `Source/UKGame/StateMachine/Quest/SMInstance_WaitInteraction.h/.cpp` , `Source/UKGame/StateMachine/Quest/SMInstance_ConditionRule.h/.cpp` , `Source/UKGame/StateMachine/UKSMTransitionInstance.h/.cpp`

### 퀘스트 보상·유저 데이터 조건
- 역할: 보상 지급과 아이템 회수 상태, 인벤토리·커스텀 데이터·프리즘 보유 조건 전환
- 파일: `Source/UKGame/StateMachine/UserData/SMState_Reward.h/.cpp` , `Source/UKGame/StateMachine/UserData/SMState_CustomData.h/.cpp` , `Source/UKGame/StateMachine/UserData/SMTransition_UserData.h/.cpp`

### 컷신·연출 스테이트 머신
- 역할: 대사 출력, 레벨 시퀀스 재생·정지, 지정 위치/타겟으로 이동시키는 연출 상태 노드
- 파일: `Source/UKGame/StateMachine/Directing/SMState_Dialog.h/.cpp` , `Source/UKGame/StateMachine/Directing/SMState_LevelSequence.h/.cpp` , `Source/UKGame/StateMachine/Directing/SMState_MoveTo.h/.cpp` , `Source/UKGame/StateMachine/UKSMInstanceDialog.h/.cpp`

### NPC 잡담·말풍선
- 역할: 주변 NPC 간 대화를 해시 그리드로 공간 검색해 매칭하고, 머리 위 위젯으로 말풍선 표시
- 파일: `Source/UKGame/Subsystems/AITalkSystem/UKAITalkSystem.h/.cpp` , `Source/UKGame/Components/AI/UKAITalkComponent.h/.cpp`

### AI 그룹 (무리 단위 활성화)
- 역할: AI들을 그룹으로 묶어 멤버를 등록·해제하고 그룹 단위 활성/비활성 상태를 틱으로 관리
- 파일: `Source/UKGame/Subsystems/AIGroupSystem/AIGroupSystem.h/.cpp`

### 스쿼드 진형 (다수 적 포위 배치)
- 역할: 스쿼드를 레이어·섹터로 나눠 멤버에게 중심/법선/랜덤 위치를 배정, 플레이어를 둘러싸는 전투 배치 형성
- 파일: `Source/UKGame/Subsystems/SquadSystem/SquadSystem.h/.cpp` , `Source/UKGame/Subsystems/SquadSystem/Squad.h/.cpp` , `Source/UKGame/Subsystems/SquadSystem/SquadLayer.h/.cpp` , `Source/UKGame/Subsystems/SquadSystem/SquadSector.h/.cpp` , `Source/UKGame/Subsystems/SquadSystem/SquadMember.h/.cpp`

### 파티 동료 (조작 캐릭터 전환·동행)
- 역할: 플레이어 파티 멤버를 등록·전환하고, 비조작 멤버는 전용 AI 컨트롤러로 따라다님
- 파일: `Source/UKGame/Subsystems/PartySystem/UKPartySystem.h/.cpp` , `Source/UKGame/Subsystems/PartySystem/UKPartyMember.h/.cpp` , `Source/UKGame/AI/UKPartyAIController.h/.cpp`

### 순찰 경로 관리
- 역할: 월드의 순찰 경로를 등록하고 위치 기준으로 가장 가까운 경로·지점을 조회
- 파일: `Source/UKGame/Subsystems/Patrol/UKPatrolPathManager.h/.cpp`

### AI 스포너 (몬스터/NPC/수중 몬스터)
- 역할: 배치된 스포너가 순찰 반경 설정과 함께 AI를 생성. NPC·수중 몬스터·소형 동물 볼륨 스폰으로 파생
- 파일: `Source/UKGame/Actors/Spawner/UKAISpawner.h/.cpp` , `Source/UKGame/Actors/Spawner/UKNPCSpawner.h/.cpp` , `Source/UKGame/Actors/Spawner/UKMonsterWaterSpawner.h/.cpp` , `Source/UKGame/Actors/Spawner/UKSmallAnimalSpawner.h/.cpp` , `Source/UKGame/Actors/Spawner/SpawnActorInterface.h/.cpp`

### 기타 배치·소품 스포너
- 역할: 굴러가는 타이어 생성기와 에디터 배치 확인용 트랜스폼 표시 액터
- 파일: `Source/UKGame/Actors/Spawner/UKTireSpawner.h/.cpp` , `Source/UKGame/Actors/Spawner/UKVisibleTransformActor.h/.cpp`

### 소형 동물 (배회·도주 생물)
- 역할: 플레이어 접근 시 도망치고 배회하는 소형 동물 캐릭터와 애니메이션, 몸에 붙는 부착물 액터
- 파일: `Source/UKGame/Actors/SmallAnimal/UKSmallAnimalCharacter.h/.cpp` , `Source/UKGame/Actors/SmallAnimal/UKSmallAnimalAttachActor.h/.cpp` , `Source/UKGame/Animation/SmallAnimal/UKSmallAnimalAnimInstance.h/.cpp`

### 슬라임 변형 표현
- 역할: 슬라임 몬스터의 머티리얼 워프 파라미터를 제어해 눌리고 늘어나는 변형 연출 (추정)
- 파일: `Source/UKGame/Animation/Slime/UKSlimeWarpMaterialControllerComponent.h/.cpp`

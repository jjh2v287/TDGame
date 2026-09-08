[← 인덱스로](../UKGame_FeatureFileMap.md)

# 4. 액터 · 컴포넌트 · 애니메이션 · 서브시스템

### 액터 공통 베이스 / 상태 저장 액터
- 역할: 모든 게임 액터의 기반 클래스와 상태(문 열림, 파괴됨, 체크포인트 등) 저장·복원
- 파일: `Source/UKGame/Actors/UKActor.h/.cpp` , `Source/UKGame/Actors/UKStatefulActor.h/.cpp` , `Source/UKGame/Actors/UKPlaceableActorBase.h/.cpp` , `Source/UKGame/Actors/UKAttackableActor.h/.cpp` , `Source/UKGame/Components/UKActorStateComponent.h/.cpp` , `Source/UKGame/Subsystems/ActorDataSystem/UKActorDataManager.h/.cpp` , `Source/UKGame/Subsystems/ActorDataSystem/UKActorDataDefined.h/.cpp`

### 플레이어 캐릭터 / 폰 / 탈것
- 역할: 플레이어와 NPC(비플레이어 캐릭터)의 폰 계층, 관전자 폰, 차량
- 파일: `Source/UKGame/Actors/UKCharacter.h/.cpp` , `Source/UKGame/Actors/UKPlayerCharacter.h/.cpp` , `Source/UKGame/Actors/UKPawn.h/.cpp` , `Source/UKGame/Actors/UKSpectatorPawn.h/.cpp` , `Source/UKGame/Actors/UKVehicle.h/.cpp`

### 상호작용 오브젝트
- 역할: 엘리베이터, 곤돌라, 밀 수 있는 상자, 크레인, 여닫이 오브젝트, 이동 발판 등 플레이어가 조작하는 장치
- 파일: `Source/UKGame/Actors/Interactable/UKElevatorBase.h/.cpp` , `Source/UKGame/Actors/Interactable/UKGondola.h/.cpp` , `Source/UKGame/Actors/Interactable/UKMovableBox.h/.cpp` , `Source/UKGame/Actors/Interactable/UKInteractiveMovingActor.h/.cpp` , `Source/UKGame/Actors/Interactable/UKClimbingEffectActor.h/.cpp` , `Source/UKGame/Actors/UKOpenableActor.h/.cpp` , `Source/UKGame/Actors/UKMovingPlatform.h/.cpp` , `Source/UKGame/Actors/UKInteractableCrane.h/.cpp` , `Source/UKGame/Components/UKInteractionComponent.h/.cpp` , `Source/UKGame/Components/UKRopeInteractionComponent.h/.cpp`

### 파괴 가능 오브젝트
- 역할: 타격으로 부서지는 오브젝트, 조각 분리형 파괴물, 쓰러지는 나무
- 파일: `Source/UKGame/Actors/Breakable/UKBreakableActor.h/.cpp` , `Source/UKGame/Actors/Breakable/UKBreakableDefine.h/.cpp` , `Source/UKGame/Actors/Destructible/UKDestructibleActor.h/.cpp` , `Source/UKGame/Actors/Foliage/UKBreakTreeActor.h/.cpp`
- 상세 흐름: [10-flow-breakable-objects.md](10-flow-breakable-objects.md) 참고 (`UKDestructibleActor`는 빈 구현 스텁)

### 채집물 / 식생 액터 시스템
- 역할: 줍거나 캘 수 있는 수집물과, 대량 폴리지를 액터로 승격·관리하는 시스템
- 파일: `Source/UKGame/Actors/Collectable/UKCollectableActor.h/.cpp` , `Source/UKGame/Subsystems/FoliageSystem/UKFoliageActorManager.h/.cpp` , `Source/UKGame/Subsystems/FoliageSystem/UKFoliageActorDefined.h/.cpp`

### 드롭 아이템 / 보상
- 역할: 월드에 떨어진 아이템·보상의 생성, 수거, 수명 관리
- 파일: `Source/UKGame/Actors/DroppedAction/UKDroppedActionBase.h/.cpp` , `Source/UKGame/Actors/UKDroppedReward.h/.cpp` , `Source/UKGame/Subsystems/DroppedAction/UKDroppedActionManager.h/.cpp` , `Source/UKGame/Subsystems/DroppedAction/UKDroppedActionDefined.h/.cpp`

### 매달리기 / 등반 지형
- 역할: 플레이어가 붙잡을 수 있는 벽·오브젝트와 스플라인 기반 잡기 경로
- 파일: `Source/UKGame/Actors/Grabbable/UKGrabbableObject.h/.cpp` , `Source/UKGame/Actors/Grabbable/UKGrabbableWall.h/.cpp` , `Source/UKGame/Actors/Grabbable/UKGrabSplineComponent.h/.cpp` , `Source/UKGame/Components/UKLedgeAttachmentSpawnerComponent.h/.cpp`

### 탈것(말) 시스템
- 역할: 탑승 가능한 말 액터
- 파일: `Source/UKGame/Actors/Mountable/UKHorse.h/.cpp`

### 생활 콘텐츠 / 낚시
- 역할: 낚시 등 생활형 오브젝트 상호작용과 낚시 가능 수역 지정
- 파일: `Source/UKGame/Actors/LifeObject/LifeObject_Fishing.h/.cpp` , `Source/UKGame/Actors/LifeObject/UKFishingVolume.h/.cpp` , `Source/UKGame/Actors/LifeObject/UKLifeObjectDefined.h/.cpp` , `Source/UKGame/Actors/LifeObjectActor.h/.cpp`

### 마커 시스템
- 역할: 월드 마커(공지, 스폰 지점, 고정 마커, 설치형 마커)의 배치·조회·관리
- 파일: `Source/UKGame/Actors/Marker/UKMarkerBaseActor.h/.cpp` , `Source/UKGame/Actors/Marker/UKPlaceableMarker.h/.cpp` , `Source/UKGame/Actors/Marker/UKSpawnMarkerActor.h/.cpp` , 외 3개(`Source/UKGame/Actors/Marker/`) , `Source/UKGame/Subsystems/MarkerSystem/UKMarkerManager.h/.cpp` , `Source/UKGame/Subsystems/MarkerSystem/UKMarkerDefined.h/.cpp` , `Source/UKGame/Subsystems/MarkerSystem/UKMarkerUtility.h/.cpp` , `Source/UKGame/Actors/UKMarkerTestActor.h/.cpp`

### 프리즘(월드맵 개방) 콘텐츠
- 역할: 프리즘 액터 활성화로 지역 맵 개방·시퀀스 재생 (추정)
- 파일: `Source/UKGame/Actors/UKPrismActor.h/.cpp` , `Source/UKGame/Actors/Marker/UKPrismMarker.h/.cpp`

### 패닉룸
- 역할: 별도 렌더 공간에서 캐릭터를 캡처해 보여주는 패닉룸 연출
- 파일: `Source/UKGame/Actors/PanicRoom/UKPanicRoomActor.h/.cpp` , `Source/UKGame/Components/PanicRoom/UKPanicRoomSceneCaptureComponent2D.h/.cpp` , `Source/UKGame/Components/PanicRoom/UKPanicRoomSkeletalMeshComponent.h/.cpp` , `Source/UKGame/Animation/PanicRoom/UKPanicRoomAnimInstance.h/.cpp`

### 물 시스템 / 부력
- 역할: 간이 수면·웅덩이·다리 수역 액터와 부력, 수면 데이터 조회
- 파일: `Source/UKGame/Actors/Water/UKSimpleWaterBodyActorBase.h/.cpp` , `Source/UKGame/Actors/Water/UKSimpleWaterBodyActor.h/.cpp` , `Source/UKGame/Actors/Water/UKSimplePuddleWaterBodyActor.h/.cpp` , `Source/UKGame/Actors/Water/UKSimpleWaterBodyBridgeActor.h/.cpp` , `Source/UKGame/Actors/Water/UKFluidFluxBodyActor.h/.cpp` , `Source/UKGame/Components/Water/UKSimpleBuoyancyComponent.h/.cpp` , `Source/UKGame/Components/Water/UKSimpleWaterDataComponent.h/.cpp`

### 작업장(WorkSite) / 상태 전파(Propagate)
- 역할: 작업 거점 액터와, 불·열 등 상태를 인접 오브젝트로 전파하고 수신해 반응(녹임 등)시키는 시스템
- 파일: `Source/UKGame/Actors/WorkSite/WorkSiteActor.h/.cpp` , `Source/UKGame/Components/Propagate/UKPropagateTransmitterComponent.h/.cpp` , `Source/UKGame/Components/Propagate/UKPropagateReceiverComponent.h/.cpp` , `Source/UKGame/Components/Propagate/WorkReceiver/UKMeltComponent.h/.cpp` , `Source/UKGame/Subsystems/Propagate/UKPropagateManager.h/.cpp` , `Source/UKGame/Subsystems/Propagate/UKPropagateDefined.h/.cpp`

### 액터 풀링 / 에셋 인스턴싱
- 역할: 액터 재사용 풀과 인스턴스 메시 기반 에셋 배치로 런타임 비용 절감
- 파일: `Source/UKGame/Actors/ActorPool/UKInstancePoolActor.h/.cpp` , `Source/UKGame/Subsystems/AssetInstanceSystem/UKAssetInstanceManager.h/.cpp` , `Source/UKGame/Subsystems/AssetInstanceSystem/UKAssetInstanceDefined.h/.cpp` , `Source/UKGame/Subsystems/AssetInstanceSystem/UKAssetInstanceBlueprintLibrary.h/.cpp`

### 트리거 볼륨 (카메라 연출)
- 역할: 진입 시 카메라 고정·궤도·흔들림·동작 오버라이드를 적용
- 파일: `Source/UKGame/Actors/TriggerVolume/UKFixedCameraVolume.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKFixedCameraTrack.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKCameraShakeVolume.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKCameraBehaviorOverrideVolume.h/.cpp`

### 트리거 볼륨 (환경·연출)
- 역할: 조명·안개·오디오·시퀀스·슬로모션 등 환경 연출 전환
- 파일: `Source/UKGame/Actors/TriggerVolume/UKAudioVolume.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKFogTrigger.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKDirectionalLightTrigger.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKLevelSequenceTrigger.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKSlowMotionVolume.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKDataLayerControlTrigger.h/.cpp`

### 트리거 볼륨 (게임플레이)
- 역할: 리스폰, 행동 제한, 태그 부여, 몽타주 강제 재생, 경로 추종, 마을·방문 판정, 게임플레이 이펙트 적용
- 파일: `Source/UKGame/Actors/TriggerVolume/UKRespawnVolume.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKRestrictActionTrigger.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKTagTriggerVolume.h/.cpp` , `Source/UKGame/Actors/TriggerVolume/UKMontageTriggerVolume.h/.cpp` , 외 5개(`Source/UKGame/Actors/TriggerVolume/`) , `Source/UKGame/Actors/UKSavePointVolume.h/.cpp` , `Source/UKGame/Actors/UKLocationVolume.h/.cpp`

### 시간대(TOD) / 날씨 연출
- 역할: 하루 시간 흐름과 지역별 시간대 오버라이드, 캐릭터의 날씨 반응 외형
- 파일: `Source/UKGame/Actors/UKTimeOfDayActor.h/.cpp` , `Source/UKGame/Actors/TODVolume.h/.cpp` , `Source/UKGame/Actors/TODOverrideActor.h/.cpp` , `Source/UKGame/Components/TODRegisterComponent.h/.cpp` , `Source/UKGame/Components/UKCharacterWeatherLookComponent.h/.cpp`

### 월드맵 캡처 / 미니맵 생성
- 역할: 레벨을 타일 단위로 촬영해 지도 텍스처 생성
- 파일: `Source/UKGame/Actors/UKMapCaptureActor.h/.cpp` , `Source/UKGame/Actors/UKMapCaptureBox.h/.cpp` , `Source/UKGame/Actors/UKMapHelper.h/.cpp`

### 퀘스트
- 역할: 퀘스트 전용 액터·스포너, 퀘스트 진행 상태 머신, 시퀀스 연동 트랙
- 파일: `Source/UKGame/Actors/UKQuestActor.h/.cpp` , `Source/UKGame/Actors/UKQuestActorSpawner.h/.cpp` , `Source/UKGame/Actors/QuestLevelSequenceTrack.h/.cpp` , `Source/UKGame/Components/UKQuestComponent.h/.cpp` , `Source/UKGame/Components/UKQuestStateMachineComponent.h/.cpp`

### 스폰 / 스케줄 / 순찰
- 역할: 액터 스폰, 시간표 기반 NPC(비플레이어 캐릭터) 행동 스케줄, 순찰 경로 이동
- 파일: `Source/UKGame/Actors/UKSpawner.h/.cpp` , `Source/UKGame/Actors/UKSchedulerActor.h/.cpp` , `Source/UKGame/Actors/UKScheduledBehaviorActor.h/.cpp` , `Source/UKGame/Actors/UKPatrolPathSpline.h/.cpp` , `Source/UKGame/Components/UKPatrolComponent.h/.cpp` , `Source/UKGame/Actors/UKSimplePointMoveActor.h/.cpp`

### 아르바이트(파트타임 잡) 시스템
- 역할: 지역별 아르바이트 등록·진행·보상 계산 및 작업자 배치
- 파일: `Source/UKGame/Actors/PartTimeJobRegion.h/.cpp` , `Source/UKGame/Subsystems/PartTimeJobManager/PartTimeJobManager.h/.cpp` , `Source/UKGame/Subsystems/PartTimeJobManager/PartTimeJobTaskObject.h/.cpp`

### 건축 / 가구 배치 / 스마트오브젝트
- 역할: 건축물 설치·해체 미리보기와 가구형 스마트오브젝트 사용
- 파일: `Source/UKGame/Components/BuildingComponent.h/.cpp` , `Source/UKGame/Actors/FurnitureActor.h/.cpp` , `Source/UKGame/Actors/UKSmartObject.h/.cpp` , `Source/UKGame/Components/UKSmartObjectComponent.h/.cpp` , `Source/UKGame/Components/UKShelterResidentComponent.h/.cpp`

### 사운드 매니저 / 오디오
- 역할: 배경음악, 내레이션, 오브젝트 사운드, UI(사용자 인터페이스) 사운드 재생과 메타사운드 커스텀 노드
- 파일: `Source/UKGame/Subsystems/SoundMananger/UKAudioEngineSubsystem.h/.cpp` , `Source/UKGame/Subsystems/SoundMananger/UKAudioCreateManager.h/.cpp` , `Source/UKGame/Subsystems/SoundMananger/UKAudioDataManager.h/.cpp` , `Source/UKGame/Subsystems/SoundMananger/UKBackgroundMusicManager.h/.cpp` , `Source/UKGame/Subsystems/SoundMananger/UKNarratorAudioManager.h/.cpp` , `Source/UKGame/Subsystems/SoundMananger/UISoundManager.h/.cpp` , 외 7개(`Source/UKGame/Subsystems/SoundMananger/`) , `Source/UKGame/Actors/UKBackgroundMusicPlayer.h/.cpp` , `Source/UKGame/Components/UKObjectSoundComponent.h/.cpp`

### 대사 / 내레이션 연출
- 역할: 대화 전용 카메라와 립싱크·음성 재생
- 파일: `Source/UKGame/Actors/DialogCameraActor.h/.cpp` , `Source/UKGame/Components/NarrationComponent.h/.cpp`

### 카메라 시스템
- 역할: 플레이어 카메라 매니저와 상황별 카메라 모디파이어(활공, 타겟 주시, 오프셋, 애니메이션 카메라 모션)
- 파일: `Source/UKGame/Camera/UKPlayerCameraManager.h/.cpp` , `Source/UKGame/Camera/UKCameraModifier_LookAtTarget.h/.cpp` , `Source/UKGame/Camera/UKCameraModifier_Gliding.h/.cpp` , `Source/UKGame/Camera/UKCameraModifier_CharacterAnimCameraMotion.h/.cpp` , 외 5개(`Source/UKGame/Camera/`) , `Source/UKGame/Camera/UKDebugCameraController.h/.cpp` , `Source/UKGame/Components/UKSpringArmComponent.h/.cpp`

### 캐릭터 이동 / 이동 보조
- 역할: 캐릭터 무브먼트 확장, 스플라인 이동, 모션 워핑, 캡슐 크기 조절, 방향 정렬
- 파일: `Source/UKGame/Components/UKCharacterMovementComponent.h/.cpp` , `Source/UKGame/Components/UKSplineMovementComponent.h/.cpp` , `Source/UKGame/Components/UKMotionWarpingComponent.h/.cpp` , `Source/UKGame/Components/UKCapsuleModifierComponent.h/.cpp` , `Source/UKGame/Components/UKFaceForwardUpdaterComponent.h/.cpp`

### 투사체 / 유도 이동
- 역할: 지면을 따라가는 투사체와 목표 추적 유도 이동
- 파일: `Source/UKGame/Components/UKGroundHuggingProjectileMovementComponent.h/.cpp` , `Source/UKGame/Components/UKHomingComponent.h/.cpp`

### 전투 / 어빌리티 / 장비
- 역할: 게임플레이 어빌리티 시스템 확장, 스킬, 장비 착용, 래그돌 전환
- 파일: `Source/UKGame/Components/UKAbilitySystemComponent.h/.cpp` , `Source/UKGame/Components/UKPlayerSkillComponent.h/.cpp` , `Source/UKGame/Components/UKEquipmentComponent.h/.cpp` , `Source/UKGame/Components/UKRagdollComponent.h/.cpp`

### 물리 오브젝트 조작
- 역할: 들기, 밀기, 염력, 타격 반응 등 물리 오브젝트 상호작용
- 파일: `Source/UKGame/Components/PhysicsObject/UKLiftableComponent.h/.cpp` , `Source/UKGame/Components/PhysicsObject/UKPushableObjectComponent.h/.cpp` , `Source/UKGame/Components/PhysicsObject/UKPsychokinesisObjectComponent.h/.cpp` , `Source/UKGame/Components/PhysicsObject/UKAttackableObjectComponent.h/.cpp` , `Source/UKGame/Components/UKPhysicsObjectComponent.h/.cpp`

### 메시 렌더링 / 성능 최적화
- 역할: 캐릭터·정적 메시 확장, 중요도 기반 업데이트 조절, 메시 갱신 제한
- 파일: `Source/UKGame/Components/UKCharacterSkeletalMeshComponent.h/.cpp` , `Source/UKGame/Components/UKStaticMeshComponent.h/.cpp` , `Source/UKGame/Components/UKSignificanceComponent.h/.cpp` , `Source/UKGame/Animation/UKMeshUpdateLimiterComponent.h/.cpp`

### 입력 / AI 브레인 / 내비게이션
- 역할: 향상된 입력 바인딩, AI 브레인 컴포넌트, 런타임 내비메시 생성
- 파일: `Source/UKGame/Components/UKInputComponent.h/.cpp` , `Source/UKGame/Components/UKBrainComponent.h/.cpp` , `Source/UKGame/Components/UKNavMeshGenerateComponent.h/.cpp`

### 기타 컴포넌트
- 역할: 소형 동물 추종, 머리 위 태그 위젯 등 보조 기능
- 파일: `Source/UKGame/Components/UKSmallAnimalFollowingComponent.h/.cpp` , `Source/UKGame/Components/UKTagWidgetComponent.h/.cpp`

### ECS(Entity Component System) 서브시스템
- 역할: 대량 에이전트를 엔티티·컴포넌트·시스템 구조로 병렬 처리하고 해시 그리드로 공간 질의
- 파일: `Source/UKGame/Subsystems/ECS/UKECSManager.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKECSEntity.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKECSComponentBase.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKECSSystemBase.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKECSAgentComponent.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKAgentHashGridManager.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKECSTestAgent.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKGridSpawner.h/.cpp`

### Redux 전역 상태 저장소
- 역할: 액션·리듀서 기반 단방향 상태 관리로 플레이어·월드·UI(사용자 인터페이스) 데이터를 일원화
- 파일: `Source/UKGame/Subsystems/ReduxSystem/UKGameStore.h/.cpp` , `Source/UKGame/Subsystems/ReduxSystem/UKGameAction.h/.cpp` , `Source/UKGame/Subsystems/ReduxSystem/UKRootReducer.h/.cpp` , `Source/UKGame/Subsystems/ReduxSystem/UKSubReducerBase.h/.cpp` , `Source/UKGame/Subsystems/ReduxSystem/UKPlayerReducer.h/.cpp` , `Source/UKGame/Subsystems/ReduxSystem/UKWorldReducer.h/.cpp` , `Source/UKGame/Subsystems/ReduxSystem/UKUIReducer.h/.cpp` , 외 4개 상태 객체(`Source/UKGame/Subsystems/ReduxSystem/`)

### 뷰모델 매니저
- 역할: 아이템 UI(사용자 인터페이스) 데이터 바인딩용 뷰모델 관리
- 파일: `Source/UKGame/Subsystems/ViewModelManager/ItemViewModelManager.h/.cpp`

### 캐릭터 애니메이션 인스턴스 / 로코모션 상태
- 역할: 플레이어·AI·군중·머리 애님 인스턴스와 지상/낙하/벽슬라이드 로코모션 상태 처리
- 파일: `Source/UKGame/Animation/UKCharacterAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKPCAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKAIAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKCrowdAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKHeadAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState_Grounded.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState_Falling.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState_WallSlide.h/.cpp`

### 애님 레이어 / 모션 매칭 / 파쿠르 애니메이션
- 역할: 애님 레이어 인터페이스, 모션 매칭 기반 이동, 등반·파쿠르·스킬 모션 재생
- 파일: `Source/UKGame/Animation/UKCharacterAnimLayersBase.h/.cpp` , `Source/UKGame/Animation/UKCharacterAnimLayerClimbingLocomotion.h/.cpp` , `Source/UKGame/Animation/UKMotionMatchingAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKParkourAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKAbilityMotionAnimInstance.h/.cpp`

### 트래젝토리(이동 예측 경로)
- 역할: 모션 매칭용 과거·미래 이동 궤적 생성
- 파일: `Source/UKGame/Animation/Trajectory/UKCharacterTrajectoryComponent.h/.cpp` , `Source/UKGame/Animation/Trajectory/UKCharacterTrajectoryLibrary.h/.cpp`

### 커스텀 애님 그래프 노드
- 역할: 본 채널 블렌드, 미러링, 조준 오프셋 룩앳, 피격 피드백 노드
- 파일: `Source/UKGame/Animation/UKAnimNode_BlendBoneByChannel.h/.cpp` , `Source/UKGame/Animation/UKAnimNode_Mirror.h/.cpp` , `Source/UKGame/AnimGraphRuntime/AnimNodes/UKAnimNode_AimOffsetLookAt.h/.cpp` , `Source/UKGame/AnimGraphRuntime/AnimNodes/UKAnimNode_HitFeedback.h/.cpp`

### 애님 노티파이 (전투·입력)
- 역할: 공격 판정 구간, 히트스캔, 무기 이펙트, 선입력 및 입력 구간 지정
- 파일: `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_Attack.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_HitScanAttack.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_WeaponEffect.h/.cpp` , `Source/UKGame/Animation/AnimNotify/AnimNotify_EarlyInput.h/.cpp` , `Source/UKGame/Animation/AnimNotify/AnimNotifyState_InputSection.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_BranchingPoint.h/.cpp`

### 애님 노티파이 (이펙트·사운드)
- 역할: 나이아가라 이펙트, 메타사운드·효과음 재생, 부착물 설정
- 파일: `Source/UKGame/Animation/AnimNotify/UKAnimNotify_PlayNiagaraEffect.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_TimedNiagaraEffectAdvanced.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_PlayMetasound.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_PlaySoundFX.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_SetTemporaryAttachment.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKNiagaraEffectNotifyDefined.h/.cpp` , 외 3개(`Source/UKGame/Animation/AnimNotify/`)

### 애님 노티파이 (화면 연출·이동)
- 역할: 포스트프로세스 머티리얼·머티리얼 파라미터 컬렉션 제어, 시간 감속, 카메라 모션, 재생 속도·스케일 조절, 이동 보정
- 파일: `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_PPMaterial.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_MPCParameter.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_SetWorldTimeDilation.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_CameraMotion.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_SkillMove.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_ClimbingVerticallyMove.h/.cpp` , 외 5개(`Source/UKGame/Animation/AnimNotify/`)

### 애님 노티파이 (이벤트·표정·IK)
- 역할: 게임플레이 태그·커스텀 이벤트 브로드캐스트, 표정 재생, 캐릭터 IK(역운동학) 제어
- 파일: `Source/UKGame/Animation/AnimNotify/UKAnimNotify_GameplayTag.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_Event.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_CustomEvent.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotifyState_Facial.h/.cpp` , `Source/UKGame/Animation/AnimNotify/UKAnimNotify_CharacterIKEvent.h/.cpp`

### 비동기 액션 (블루프린트 지연 노드)
- 역할: 비동기 오버랩·트레이스 질의, 스플라인 이동, 목표 회전 보간 완료 대기
- 파일: `Source/UKGame/AsyncAction/UKAsyncTask_Base.h/.cpp` , `Source/UKGame/AsyncAction/UKAsyncOverLap.h/.cpp` , `Source/UKGame/AsyncAction/UKAsyncTrace.h/.cpp` , `Source/UKGame/AsyncAction/UKAsyncTask_SplineMove.h/.cpp` , `Source/UKGame/AsyncAction/UKAsyncTask_RotateActorToTargetRotation.h/.cpp`

### 화면 왜곡 연출 액터
- 역할: 방사형 블러 등 화면 왜곡 효과 제어 (추정)
- 파일: `Source/UKGame/Actors/UKChameleon.h/.cpp`

### 디버그 / 테스트 액터
- 역할: 충돌 질의 테스트 등 개발용 검증 도구
- 파일: `Source/UKGame/Actors/UKCollisionQueryTestActor.h/.cpp`

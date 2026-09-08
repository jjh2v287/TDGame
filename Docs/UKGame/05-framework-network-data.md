[← 인덱스로](../UKGame_FeatureFileMap.md)

# 5. 게임 프레임워크 · 네트워크 · 데이터

### 계정 로그인 및 서버 통신 기반
- 역할: 자체 정의한 이진 패킷(REQ/ACK/NFY) 프로토콜로 게임 서버와 요청-응답을 주고받는 통신 계층. 실제 소켓/HTTP(HyperText Transfer Protocol) 코드는 없고 로컬 프록시가 서버 역할을 대행함 (추정: 오프라인/개발용 더미 서버 단계)
- 파일: `Source/UKGame/Subsystems/NetworkManager/NetworkManager.h/.cpp` , `Source/UKGame/Subsystems/NetworkManager/NetworkDefined.h` , `Source/UKGame/Common/UKMsgDefine.h`

### 서버 대행 프록시(오프라인 서버 시뮬레이션)
- 역할: REQ 패킷을 수신해 SQLite 질의로 데이터를 처리하고 ACK/NFY를 되돌려주는 인게임 가짜 서버. 로그인, 보상, 아이템, 상점, 알바, 파티, 은신처 등 모든 핸들러 보유
- 파일: `Source/UKGame/Subsystems/NetworkManager/GameServerProxy.h/.cpp` , `Source/UKGame/Common/UKSqliteQuery.h` , `Source/UKGame/Common/UKSqliteUtil.h`

### 서버 측 퀘스트/미션 처리
- 역할: 런타임 퀘스트 추가·진행 검증과 일일/주간 미션 갱신을 서버 쪽 로직으로 처리하고 Redis 저장 데이터로 보관
- 파일: `Source/UKGame/Subsystems/NetworkManager/ServerQuestSystem.h/.cpp` , `Source/UKGame/Subsystems/NetworkManager/RedisSaveGame.h/.cpp`

### 서버 측 보상/아이템 지급
- 역할: 몬스터 처치·채집물·퀘스트 보상 계산과 인벤토리 반영을 서버 쪽에서 검증
- 파일: `Source/UKGame/Subsystems/NetworkManager/ServerRewardSystem.h/.cpp` , `Source/UKGame/DataTable/RewardData.h/.cpp`

### 콘텐츠 해금(퍼미션)
- 역할: 진행도에 따라 기능/지역/시스템을 열어주는 권한 관리와 해금 알림(NFY_PERMISSION_ADD/REMOVE)
- 파일: `Source/UKGame/Subsystems/NetworkManager/PermissionSystem.h/.cpp` , `Source/UKGame/Common/PermissionTypes.h` , `Source/UKGame/Network/UserData_Permission.h/.cpp`

### 유저 데이터 저장소(클라이언트 캐시)
- 역할: 서버에서 내려받은 계정 데이터를 도메인별 UObject로 보관·갱신하는 모델 계층. 모두 `UUserData_Base` 파생
- 파일: `Source/UKGame/Network/UserData_Base.h/.cpp` , `Source/UKGame/Subsystems/UserDataManager.h/.cpp`

### 유저 데이터 - 캐릭터/육성
- 역할: 보유 플레이어 캐릭터, 레벨·경험치·강화, 파티 편성 정보
- 파일: `Source/UKGame/Network/UserData_PlayerInfo.h/.cpp` , `Source/UKGame/Network/UserData_Character.h/.cpp` , `Source/UKGame/Network/UserData_CharacterParty.h/.cpp`

### 유저 데이터 - 아이템/장비/창고
- 역할: 인벤토리, 장착 장비, 창고 보관, 재화 보유량
- 파일: `Source/UKGame/Network/UserData_Inventory.h/.cpp` , `Source/UKGame/Network/UserData_Equipment.h/.cpp` , `Source/UKGame/Network/UserData_Storage.h/.cpp` , `Source/UKGame/Network/UserData_Currency.h/.cpp`

### 유저 데이터 - 진행/성장 콘텐츠
- 역할: 퀘스트 진행, 연구(테크), 세력 평판, 몬스터 카드 수집, 아르바이트, 프리즘
- 파일: `Source/UKGame/Network/UserData_Quest.h/.cpp` , `Source/UKGame/Network/UserData_Research.h/.cpp` , `Source/UKGame/Network/UserData_Faction.h/.cpp` , `Source/UKGame/Network/UserData_MonsterCard.h/.cpp` , `Source/UKGame/Network/UserData_PartTimeJob.h/.cpp` , `Source/UKGame/Network/UserData_Prism.h/.cpp`

### 유저 데이터 - 월드 상태 저장
- 역할: 액터 상태(개폐·파괴 등), 스포너 상태, 대화 진행 상태, 은신처 가구 배치를 계정 단위로 유지
- 파일: `Source/UKGame/Network/UserData_ActorState.h/.cpp` , `Source/UKGame/Network/UserData_Spawner.h/.cpp` , `Source/UKGame/Network/UserData_Dialog.h/.cpp` , `Source/UKGame/Network/UserData_Shelter.h/.cpp`

### 게임 진입 및 세션 수명 주기
- 역할: 게임 인스턴스/모드/월드 세팅/에셋 매니저 등 부팅과 세션 전반 골격
- 파일: `Source/UKGame/UKGame.h/.cpp` , `Source/UKGame/UKGame.Build.cs` , `Source/UKGame/GameFramework/UKGameInstance.h/.cpp` , `Source/UKGame/GameFramework/UKGameMode.h/.cpp` , `Source/UKGame/GameFramework/UKGameWorldSettings.h/.cpp` , `Source/UKGame/GameFramework/UKGameAssetManager.h/.cpp`

### 플레이어 조작 주체
- 역할: 플레이어 컨트롤러와 플레이어 스테이트. 캐릭터 클래스 자체는 담당 범위 밖(`Source/UKGame/Actors` 계열)
- 파일: `Source/UKGame/GameFramework/UKPlayerController.h/.cpp` , `Source/UKGame/GameFramework/UKPlayerState.h/.cpp`

### 입력 처리(향상된 입력)
- 역할: 입력 액션 매핑 구성, 커스텀 입력 수정자/트리거, 플레이어 입력 객체 확장
- 파일: `Source/UKGame/GameFramework/UKInputConfig.h/.cpp` , `Source/UKGame/GameFramework/UKInputModifiers.h/.cpp` , `Source/UKGame/GameFramework/UKEnhancedPlayerInput.h/.cpp` , `Source/UKGame/EnhancedInput/UKInputTriggerNotChordedAction.h/.cpp`

### 이동/파쿠르 로코모션 지원
- 역할: 캐릭터 무브먼트용 구조체 정의와 루트모션 소스 확장
- 파일: `Source/UKGame/GameFramework/UKCharacterMovementStructures.h/.cpp` , `Source/UKGame/GameFramework/UKRootMotionSource.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKParkourLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKLocomotionAnimBlueprintLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKRopeLibrary.h/.cpp`

### 스킬 콤보 연계
- 역할: 스킬 데이터 기반 콤보 입력 판정 및 다음 스킬 연결
- 파일: `Source/UKGame/GameFramework/UKSkillCombo.h/.cpp` , `Source/UKGame/DataAsset/UKComboDataAsset.h` , `Source/UKGame/DataTable/SkillData.h/.cpp`

### 게임플레이 태그 및 메시지 전달
- 역할: 프로젝트 공용 게임플레이 태그 정의와 시스템 간 메시지 페이로드 규격
- 파일: `Source/UKGame/GameFramework/UKGameplayTags.h/.cpp` , `Source/UKGame/GameFramework/UKGameplayMessagePayload.h/.cpp`

### 컷신/레벨 시퀀스 연출
- 역할: 커스텀 레벨 시퀀스와 디렉터, 무비신 트랙 인스턴스, 시퀀스 동적 바인딩
- 파일: `Source/UKGame/GameFramework/UKLevelSequence.h/.cpp` , `Source/UKGame/GameFramework/UKLevelSequenceDirector.h/.cpp` , `Source/UKGame/GameFramework/UKMovieSceneTrackInstance.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKLevelSequenceLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKMovieSceneDynamicBinding.h/.cpp`

### 환경 사운드 트리거
- 역할: 특정 구역 진입 시 배경음/환경음을 재생·페이드
- 파일: `Source/UKGame/GameFramework/UKSoundTriggerBox.h/.cpp` , `Source/UKGame/GameFramework/UKFadeSoundTriggerBox.h/.cpp` , `Source/UKGame/DataTable/UKLocalBackgroundMusicData.h/.cpp` , `Source/UKGame/DataTable/UKSoundData.h/.cpp`

### 게임 옵션 설정
- 역할: 그래픽/사운드/조작 등 사용자 설정 데이터와 저장
- 파일: `Source/UKGame/GameFramework/UKGameSettings.h/.cpp`

### 치트/디버그 콘솔
- 역할: 개발용 치트 명령어 등록 및 실행
- 파일: `Source/UKGame/GameFramework/UKCheatManager.h/.cpp` , `Source/UKGame/GameFramework/UKCheatManager_Command.cpp`

### 현지화 및 텍스트
- 역할: 언어별 문자열 테이블 로드·전환과 커스텀 FText 확장
- 파일: `Source/UKGame/Subsystems/LocalizationManager.h/.cpp` , `Source/UKGame/GameFramework/UKText.h/.cpp` , `Source/UKGame/Common/StringUtils.h`

### 서브시스템 목록(Subsystems 최상위)
- 역할: 각 항목 한 줄 요약
- 파일:
  - `Source/UKGame/Subsystems/UKSubsystemInterface.h/.cpp` — 서브시스템 공통 인터페이스
  - `Source/UKGame/Subsystems/UserDataManager.h/.cpp` — 계정 데이터 총괄 접근점
  - `Source/UKGame/Subsystems/EntityManager.h/.cpp` — 게임플레이 엔티티(어빌리티 보유 객체) 등록·조회
  - `Source/UKGame/Subsystems/UKActorInstanceManager.h/.cpp` — 스포너가 생성한 액터 인스턴스 관리
  - `Source/UKGame/Subsystems/SaveGameManager.h/.cpp` — 로컬 세이브 파일 저장/로드
  - `Source/UKGame/Subsystems/RewardManager.h/.cpp` — 보상 지급 및 드롭 보상 액터 처리
  - `Source/UKGame/Subsystems/NPCStateManager.h/.cpp` — NPC 상태/성향 틱 갱신
  - `Source/UKGame/Subsystems/DinnerDialogSystem.h/.cpp` — 식사 대화 이벤트 진행(시퀀스+내레이션+위젯)
  - `Source/UKGame/Subsystems/TimeOfDayManager.h/.cpp` — 시간대·날씨 볼륨 및 조명 프로파일 제어
  - `Source/UKGame/Subsystems/UKCameraSystem.h/.cpp` — 상황별 카메라 전환 제어
  - `Source/UKGame/Subsystems/UKLockOnSubsystem.h/.cpp` — 전투 대상 락온 타겟 선정·유지
  - `Source/UKGame/Subsystems/UKEnvironmentEventSystem.h/.cpp` — 환경 이벤트 발생/핸들러 디스패치
  - `Source/UKGame/Subsystems/UKUIManager.h/.cpp` — HUD/팝업 UI 스크린 스택 관리
  - `Source/UKGame/Subsystems/VideoPlayer.h/.cpp` — 인게임 동영상 재생 및 사운드 믹스 제어

### 시간대/낮밤 순환
- 역할: 월드 시간 진행과 시간대별 이벤트 트리거
- 파일: `Source/UKGame/Common/UKTimeOfDayInitializer.h/.cpp` , `Source/UKGame/DataTable/UKWorldTimeEventData.h/.cpp` , `Source/UKGame/DataTable/UKScheduleData.h/.cpp`

### 공용 유틸/정의
- 역할: 충돌 채널, 로그 카테고리, 공용 구조체, 조건식 평가, 어빌리티 이펙트 컨텍스트, 엔티티 컨텍스트
- 파일: `Source/UKGame/Common/UKCollisionTypes.h` , `Source/UKGame/Common/UKCommonDefine.h` , `Source/UKGame/Common/UKCommonLog.h` , `Source/UKGame/Common/UKStructure.h/.cpp` , `Source/UKGame/Common/UKCondition.h/.cpp` , `Source/UKGame/Common/RuntimeAbilityEffectContext.h/.cpp` , `Source/UKGame/Common/EntityContext.h/.cpp`

### 블루프린트 노출 함수 라이브러리
- 역할: 기획/아티스트가 블루프린트에서 쓰는 기능별 정적 함수 모음
- 파일: `Source/UKGame/BlueprintLibrary/UKBlueprintLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/GameDataLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKAbilityBlueprintFunctionLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKAnimBlueprintLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKDialogLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKTargetingLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKSmartObjectLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKSMStateLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKUIBlueprintLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKMathLibrary.h/.cpp` , `Source/UKGame/BlueprintLibrary/UKMaterialUtilityFunctionLibrary.h/.cpp`

### 애니메이션 세트 데이터 애셋
- 역할: 상황별 몽타주/시퀀스 묶음을 데이터로 관리(공용 베이스 `AnimDataAssetBase`)
- 파일: `Source/UKGame/DataAsset/AnimDataAssetBase.h` , `Source/UKGame/DataAsset/LocomotionAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/CombatAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/AbilityAnimDataAsset.h` , `Source/UKGame/DataAsset/ActionAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/LifeActionAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/ParkourAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/UKVaultAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/UKGrabAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/UKGrabTransitionAnimDataAsset.h/.cpp` , `Source/UKGame/DataAsset/SwimAnimDataAsset.h` , `Source/UKGame/DataAsset/FacialAnimDataAsset.h`

### 시스템 설정 데이터 애셋
- 역할: 카메라 거동, 락온 규칙, 파티 시스템, 콤보 설정 등을 데이터로 분리
- 파일: `Source/UKGame/DataAsset/UKCameraBehaviourDataAsset.h` , `Source/UKGame/DataAsset/UKLockOnConfigDataAsset.h/.cpp` , `Source/UKGame/DataAsset/UKPartySystemConfig.h/.cpp` , `Source/UKGame/DataAsset/UKComboDataAsset.h`

### 데이터테이블 - 기반
- 역할: 프로젝트 공용 데이터테이블 베이스 및 전역 상수
- 파일: `Source/UKGame/DataTable/UKDataTable.h/.cpp` , `Source/UKGame/DataTable/GlobalConstant.h/.cpp`

### 데이터테이블 - 아이템/장비/재화
- 역할: 아이템 정의, 장비와 부옵션, 도구·재료·귀중품, 재화
- 파일: `Source/UKGame/DataTable/ItemDataTable.h/.cpp` , `Source/UKGame/DataTable/EquipmentData.h/.cpp` , `Source/UKGame/DataTable/EquipmentSubOption.h/.cpp` , `Source/UKGame/DataTable/CurrencyData.h/.cpp` 외 4개(ItemToolData, ItemMaterialData, ItemValuableData, AttachableMeshData)

### 데이터테이블 - 제작/강화/분해
- 역할: 레시피, 장비·도구 제작, 강화·인챈트·업그레이드, 분해
- 파일: `Source/UKGame/DataTable/RecipeDataTable.h/.cpp` , `Source/UKGame/DataTable/EnhanceData.h/.cpp` , `Source/UKGame/DataTable/EnchantData.h/.cpp` , `Source/UKGame/DataTable/ItemDismantleData.h/.cpp` 외 5개(EquipRecipeData, ToolRecipeData, EnhanceEffectData, ArtifactUpgradeData, CharacterUpgradeData)

### 데이터테이블 - 요리/음식
- 역할: 요리 레시피와 특수 요리, 음식 효과
- 파일: `Source/UKGame/DataTable/CookRecipeData.h/.cpp` , `Source/UKGame/DataTable/FoodData.h/.cpp` 외 1개(CookSpecialData)

### 데이터테이블 - 캐릭터/성장/스탯
- 역할: 플레이어 캐릭터, AI 캐릭터, 스탯, 경험치, IK 설정
- 파일: `Source/UKGame/DataTable/PlayerCharacterData.h/.cpp` , `Source/UKGame/DataTable/CharacterData.h/.cpp` , `Source/UKGame/DataTable/StatData.h/.cpp` , `Source/UKGame/DataTable/ExpData.h/.cpp` 외 2개(AICharacterData, UKCharacterIKData)

### 데이터테이블 - 전투/스킬/피격 연출
- 역할: 전투 액션, 스킬, 어빌리티 이펙트, 투사체, 피격 정지·시간 정지·부위 파괴·방어 판정
- 파일: `Source/UKGame/DataTable/UKCombatData.h/.cpp` , `Source/UKGame/DataTable/SkillData.h/.cpp` , `Source/UKGame/DataTable/AbilityEffectData.h/.cpp` , `Source/UKGame/DataTable/ProjectileData.h/.cpp` 외 5개(HitstopData, TimestopData, DamageBreakData, MonsterHitboxDefenceData, EffectData)

### 데이터테이블 - 이동/등반/애니메이션 세트
- 역할: 등반·클라이밍 로코모션과 몽타주, 애니메이션 세트 매핑
- 파일: `Source/UKGame/DataTable/UKClimbingData.h/.cpp` , `Source/UKGame/DataTable/AnimationSetData.h/.cpp` 외 2개(UKClimbingLocomotionData, UKClimbingMontageData)

### 데이터테이블 - 퀘스트/미션/연구
- 역할: 퀘스트 정의와 설명, 일일·주간 미션, 연구(테크) 트리
- 파일: `Source/UKGame/DataTable/QuestData.h/.cpp` , `Source/UKGame/DataTable/QuestDescriptionData.h/.cpp` , `Source/UKGame/DataTable/MissionData.h/.cpp` , `Source/UKGame/DataTable/ResearchData.h/.cpp`

### 데이터테이블 - 대화/내레이션/연출
- 역할: 대사, 식사 대화, 대화 카메라, 내레이션, 퀵타임 이벤트, 동영상 리소스
- 파일: `Source/UKGame/DataTable/DinnerDialogData.h/.cpp` , `Source/UKGame/DataTable/DialogCameraData.h/.cpp` , `Source/UKGame/DataTable/NarrationData.h/.cpp` , `Source/UKGame/DataTable/QuickTimeEventData.h/.cpp` 외 2개(UKEventNarratorData, VideoResourceData)

### 데이터테이블 - NPC 행동/스케줄
- 역할: NPC 상태 기본값·자연 변화, 성향, 스마트오브젝트 이용 시간, 활동 시간 보정, 행동 파라미터 매핑
- 파일: `Source/UKGame/DataTable/UKNPCStateDefaultData.h/.cpp` , `Source/UKGame/DataTable/UKNPCTraitData.h/.cpp` , `Source/UKGame/DataTable/UKScheduleData.h/.cpp` 외 4개(UKNPCStateNaturalChangeData, UKNPCActiveTimeAdjustData, UKNPCSmartObjectDurationData, UKNPCActionParameterMappingData)

### 데이터테이블 - NPC 잡담(AI Talk)
- 역할: 상황별 NPC 대사 풀과 대응 음성
- 파일: `Source/UKGame/DataTable/UKAITalkData.h/.cpp` , `Source/UKGame/DataTable/UKAITalkSoundData.h/.cpp`

### 데이터테이블 - 월드/지역/던전
- 역할: 월드·지역·구역 구획, 던전 로딩 통로 그룹, 패닉룸, 생활 오브젝트, 채집 폴리지
- 파일: `Source/UKGame/DataTable/WorldData.h/.cpp` , `Source/UKGame/DataTable/WorldRegionData.h/.cpp` , `Source/UKGame/DataTable/AreaData.h/.cpp` , `Source/UKGame/DataTable/LifeObjectData.h/.cpp` 외 4개(UKDungeonLoadingPassageGroupData, UKPanicRoomData, UKCollectibleFoliageData, UKAssetInstanceManagerSettingData)

### 데이터테이블 - 은신처/가구/보관
- 역할: 은신처 건설, 가구 배치, 창고 확장
- 파일: `Source/UKGame/DataTable/ShelterData.h/.cpp` , `Source/UKGame/DataTable/FurnitureData.h/.cpp` , `Source/UKGame/DataTable/StorageData.h/.cpp`

### 데이터테이블 - 상점/뽑기/보상
- 역할: 상점 그룹과 판매 목록, 가챠 캠페인, 보상 테이블, 아르바이트 보수
- 파일: `Source/UKGame/DataTable/ShopGroup.h/.cpp` , `Source/UKGame/DataTable/ShopItem.h/.cpp` , `Source/UKGame/DataTable/GachaCampaignData.h/.cpp` , `Source/UKGame/DataTable/RewardData.h/.cpp` 외 2개(PartTimeJobData, PartTimeJobConstantData)

### 데이터테이블 - 세력/평판/수집
- 역할: 세력 정의와 평판 구간, 몬스터 카드, 노움 수집물
- 파일: `Source/UKGame/DataTable/FactionData.h/.cpp` , `Source/UKGame/DataTable/FactionReputationData.h/.cpp` , `Source/UKGame/DataTable/MonsterCardData.h/.cpp` , `Source/UKGame/DataTable/GnomeData.h/.cpp`

### 데이터테이블 - 사운드/물리 재질/기타
- 역할: UI 사운드, 공용 사운드 매핑, 물리 재질별 반응, 드롭 액션
- 파일: `Source/UKGame/DataTable/UISoundData.h/.cpp` , `Source/UKGame/DataTable/UKSoundData.h/.cpp` , `Source/UKGame/DataTable/PhysicalMaterialDataTable.h/.cpp` , `Source/UKGame/DataTable/UKDroppedActionData.h/.cpp`

### 자동화 테스트
- 역할: USTRUCT 멤버 미초기화 검출 자동화 테스트
- 파일: `Source/UKGame/Test/UKAutomationTest_UninitializedScriptStructMembers.cpp`

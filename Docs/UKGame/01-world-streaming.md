[← 인덱스로](../UKGame_FeatureFileMap.md)

# 1. 월드 · 스트리밍 · 씬 흐름

### 심리스 던전 로딩 (Passage)
- 역할: 야외 → 던전 진입 통로에서 로딩 화면 없이 월드 파티션 그리드를 교차 스트리밍시켜 끊김 없는 이동을 제공
- 파일: `Source/UKGame/Actors/Passage/UKDungeonLoadingPassage.h/.cpp` , `Source/UKGame/Subsystems/PassageSystem/UKDungeonLoadingPassageManager.h/.cpp` , `Source/UKGame/Actors/StreamingSourceProvider/UKStreamingSourceProvider.h/.cpp` , `Source/UKGame/Actors/StreamingSourceProvider/UKDungeonPlaceableStreamingSourceProvider.h/.cpp`
- 클래스별 담당:
  - `AUKDungeonLoadingPassage`: 통로 액터. 스플라인(경로) + 바깥문/안쪽문 박스 두 개의 오버랩을 감지하고, 스플라인 상 진행률(`GetProgressRate`)을 계산해 매니저에 알림
  - `UUKDungeonLoadingPassageManager`: GameInstance 서브시스템(틱 사용). 통로 등록/해제, 진입·이탈 상태 머신(`EUKDungeonPassageState`: None → OutSideDoorEnter → PassageInProgress → InSideDoorEnter → InSide) 관리, 진행률에 따라 바깥/안쪽 스트리밍 소스 프로바이더를 스폰·활성·비활성·파괴하며 실제 심리스 로딩을 지휘
  - `AUKStreamingSourceProvider`: `UWorldPartitionStreamingSourceComponent`를 감싼 액터. 특정 위치·모양(반경/섹터)으로 월드 파티션 셀 로딩을 유발하는 "스트리밍 소스" 자체이며 Active/Deactive만 담당
  - `AUKDungeonPlaceableStreamingSourceProvider`: 레벨 디자이너가 맵에 직접 배치하는 스트리밍 소스. `PassageGroup` 값으로 매니저에 추가(Extra) 소스로 등록되어 해당 던전 그룹 진입 시 함께 동작
  - `UWorldManager`: 리전(맵) 단위의 상위 로딩 담당. 통로 방식과 별개로 `ChangeWorld`/`ChangeRegion`/`Teleport`로 리전을 바꾸고, 리전 키 기준 스트리밍 소스 데이터(`ApplyStreamingSourceData`)와 로딩 완료 시점을 관리
  - `UUKDataLayerStreamingProvider`: 데이터 레이어 묶음의 런타임 상태를 요청·감시하고, 모든 레이어 로딩 완료(또는 타임아웃) 시점을 상위 시스템에 통지하는 로딩 완료 판정기
- 상세 흐름: [09-flow-seamless-dungeon-loading.md](09-flow-seamless-dungeon-loading.md) 참고

### 데이터 레이어 스트리밍 및 월드·리전 전환
- 역할: 월드/리전 변경, 데이터 레이어 활성화, 플레이어 시작 위치 결정, 로딩 완료 판정까지의 흐름 제어
- 파일: `Source/UKGame/Subsystems/WorldManager/WorldManager.h/.cpp` , `Source/UKGame/Subsystems/WorldManager/UKDataLayerStreamingProvider.h/.cpp`

### NPC 스트리밍 소스 / NPC 밀집도 그리드
- 역할: NPC 위치를 500 단위 셀 해시 그리드로 관리해 밀집도를 계산하고, 그에 맞춰 스트리밍 소스를 배치 (추정: NPC 주변 레벨을 미리 로딩하기 위한 용도)
- 파일: `Source/UKGame/Actors/StreamingSourceProvider/UKNPCStreamingSourceProvider.h/.cpp`

### 월드 부트스트랩 (월드별 진입 로직)
- 역할: 월드 종류별 BeginPlay/EndPlay, 플레이어 스폰, 리전 로딩 시작·완료 처리, 로딩 중 캐릭터 정지/재개
- 파일: `Source/UKGame/World/UKWorldBase.h/.cpp` , `Source/UKGame/World/UKGameWorld.h/.cpp`

### PSO(파이프라인 상태 오브젝트) 캐시 프리캐시
- 역할: 최초 실행 시 전용 월드에서 메시·나이아가라 에셋을 대량 스폰해 셰이더 PSO를 미리 굽고 결과를 저장(첫 플레이 렉 제거)
- 파일: `Source/UKGame/World/UKPSOCacheWorld.h/.cpp` , `Source/UKGame/World/UKPSOAssetSpawner.h/.cpp` , `Source/UKGame/SaveGame/UKDynamicConfig.h/.cpp`

### 씬 흐름 (타이틀 → 패치 → 로그인 → 게임)
- 역할: 스타터/타이틀/로딩/게임 씬 전환과 타이틀 내부 상태(패치, 로그인, 데이터 로드, 게임 준비) 진행
- 파일: `Source/UKGame/Subsystems/SceneManager/UKSceneManager.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/UKSceneBase.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/UKStarterScene.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/UKTitleScene.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/UKLoadingScene.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/UKGameScene.h/.cpp`
- 파일(타이틀 상태): `Source/UKGame/Subsystems/SceneManager/Scene/SceneState/UKSceneStateBase.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/SceneState/UKTitlePatchState.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/SceneState/UKTitleLoginState.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/SceneState/UKTitleLoadDataState.h/.cpp` , `Source/UKGame/Subsystems/SceneManager/Scene/SceneState/UKTitleGameReadyState.h/.cpp`

### 월드 시간 / 낮밤 주기
- 역할: 게임 내 시간 흐름과 배속(정지·정상·가속), 실시간 ↔ 게임시간 변환, 낮밤 주기 및 시간 기반 이벤트 조율
- 파일: `Source/UKGame/Subsystems/WorldTimeManager/UKWorldTimeManager.h/.cpp` , `Source/UKGame/Subsystems/WorldTimeManager/TimeModule.h/.cpp`

### 시간 기반 스케줄러 (NPC 일과·정기 이벤트)
- 역할: 게임 시간표에 등록된 스케줄을 시각에 맞춰 실행하고 실행 횟수·최근 실행 시각을 추적
- 파일: `Source/UKGame/Subsystems/WorldTimeManager/Module/SchedulerModule.h/.cpp`

### 세이브 / 로드
- 역할: 세이브 오브젝트 공통 저장·삭제 기반과 항목별 로컬 저장 데이터 보관
- 파일: `Source/UKGame/SaveGame/UKSaveGame.h/.cpp` , `Source/UKGame/SaveGame/UKDynamicConfig.h/.cpp` , `Source/UKGame/SaveGame/TimeOfDaySave.h/.cpp` , `Source/UKGame/SaveGame/ContentsAlarmSave.h/.cpp` , `Source/UKGame/SaveGame/UKBackgroundMusicSave.h/.cpp` , `Source/UKGame/SaveGame/UKDinnerPartySave.h/.cpp`
- 세부: `TimeOfDaySave`는 리전별 낮밤 시간대, `ContentsAlarmSave`는 신규 콘텐츠 "N" 알림·가챠 카운터, `UKBackgroundMusicSave`는 마지막 배경음, `UKDinnerPartySave`는 만찬 이벤트 가능 여부

### 컷신 · 레벨 시퀀스 재생
- 역할: 레벨 시퀀스 재생/정지/일시정지/재개, 시네마틱 모드 판정, 카메라 컷 제어와 인스턴스 수명 관리
- 파일: `Source/UKGame/Subsystems/LevelSequence/LevelSequenceManager.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/UKLevelSequenceInstance.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/UKLevelSequencePlayer.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/UKLevelSequenceActor.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/UKLevelSequenceContext.h/.cpp`

### 컷신 내 대사 · 선택지 · QTE(퀵타임 이벤트) 트랙
- 역할: 시퀀서 타임라인 위에서 대사 출력, 선택지 분기, 퀵타임 이벤트를 재생하는 커스텀 트랙
- 파일: `Source/UKGame/Subsystems/LevelSequence/Track/UKBaseTrack.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/Track/DialogTrack.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/Track/UKDialogTrackInstance.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/Track/SelectDialogTrack.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/Track/SelectDialogTrackInstance.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/Track/QuickTimeEventTrack.h/.cpp` , `Source/UKGame/Subsystems/LevelSequence/Track/UKQuickTimeEventTrackInstance.h/.cpp`

### 샷컷 인게임 디버그 툴 (ImGui 독)
- 역할: 인게임에서 토글되는 ImGui 기반 도구 창 프레임워크와 자유 카메라·씬 정보 독 제공
- 파일: `Source/UKGame/Subsystems/ShotCutSystem/ShotCutSystem.h/.cpp` , `Source/UKGame/Subsystems/ShotCutSystem/ShotCutSystemInterface.h` , `Source/UKGame/Subsystems/ShotCutSystem/ShotCutDock.h/.cpp` , `Source/UKGame/Subsystems/ShotCutSystem/Dock/FreeCameraDock.h/.cpp` , `Source/UKGame/Subsystems/ShotCutSystem/Dock/SceneDock.h/.cpp`

### 공간 질의 (액터 해시 그리드)
- 역할: 액터를 해시 그리드에 등록/갱신하고 반경 기반 주변 액터 조회를 빠르게 처리
- 파일: `Source/UKGame/Subsystems/WorldSpatial/UKWorldSpatialManager.h/.cpp` , `Source/UKGame/Subsystems/WorldSpatial/UKWorldSpatialDefine.h`

### 가시성 그룹 숨김 관리
- 역할: 게임플레이 태그로 묶인 액터 그룹을 "숨김 사유" 단위로 감추고 해제(컷신·특정 연출 중 오브젝트 숨김)
- 파일: `Source/UKGame/Subsystems/VisibilityManager/VisibilityManager.h/.cpp`

### AI 내비게이션 (HPA 웨이포인트 길찾기)
- 역할: 웨이포인트 등록과 계층(HPA, Hierarchical Path-finding A*) 구축, 장거리 경로 탐색·스무딩, 수영 가능 영역 판정과 이동 요청 대행
- 파일: `Source/UKGame/Subsystems/NavigationManager/UKNavigationManager.h/.cpp` , `Source/UKGame/Navigation/UKWayPoint.h/.cpp` , `Source/UKGame/Navigation/UKNavigationDefine.h/.cpp` , `Source/UKGame/Navigation/UKNavArea_Swimmable.h/.cpp` , `Source/UKGame/Navigation/UKNavigationDebugActor.h/.cpp`

### 게임 피처 액션 (플러그인 단위 콘텐츠 주입)
- 역할: 게임 피처 활성화 시 입력 매핑, 레벨 인스턴스, 액터, 월드 싱글턴 시스템을 월드에 추가
- 파일: `Source/UKGame/GameFeatures/GameFeatureAction_WorldActionBase.h/.cpp` , `Source/UKGame/GameFeatures/GameFeatureAction_AddInputContextMapping.h/.cpp` , `Source/UKGame/GameFeatures/GameFeatureAction_AddLevelInstances.h/.cpp` , `Source/UKGame/GameFeatures/GameFeatureAction_AddSpawnedActors.h/.cpp` , `Source/UKGame/GameFeatures/GameFeatureAction_AddWorldSystem.h/.cpp`

### 레벨 디자인 조건 스크립트
- 역할: 레벨에 배치해 조건 성립 여부를 평가하는 스크립트 기반(예: 지정 스포너의 몬스터 전멸 시 조건 충족)
- 파일: `Source/UKGame/LevelDesign/LevelDesignConditionScript.h/.cpp` , `Source/UKGame/LevelDesign/LDCondition_MonsterDead.h/.cpp`

### 쉘터(거점) 시스템
- 역할: 쉘터 구역 진입/이탈 감지, 쉘터별 NPC·가구 액터 스폰과 표시 관리, 해금 여부 판정
- 파일: `Source/UKGame/Subsystems/Shelter/ShelterSystem.h/.cpp`

### 쉘터 거주 NPC 행동 (가구 상호작용·피로도)
- 역할: 거주 NPC를 등록하고 사용 가능한 가구를 탐색해 상호작용시키며 피로도를 회복·갱신 (추정: 아르바이트 상태 연동 포함)
- 파일: `Source/UKGame/Subsystems/Shelter/UKShelterResidentManager.h/.cpp`

### 프리즘 (지역 개방·안개 해제)
- 역할: 소형 프리즘 활성화를 그룹 단위로 누적해 중형 프리즘을 개방하고, 조건 충족 시 해당 지역을 드러냄
- 파일: `Source/UKGame/Subsystems/PrismManager/UKPrismManager.h/.cpp`

[← 인덱스로](../UKGame_FeatureFileMap.md)

# 7. 에디터 모듈(UKEditor) · 자체 플러그인

### UKEditor - 에디터 모듈 진입점
- 역할: 프로젝트 전용 에디터 모듈의 등록/해제 지점으로, 트랙 에디터·프로퍼티 커스터마이징·에셋 정의를 일괄 등록합니다.
- 파일: `Source/UKEditor/Public/UKEditor.h` + `Source/UKEditor/Private/UKEditor.cpp`, `Source/UKEditor/UKEditor.Build.cs`

### UKEditor - AnimGraph 커스텀 노드
- 역할: 에임 오프셋 룩앳, 본 채널 블렌드, 히트 피드백, 미러링용 자체 애님 그래프 노드의 에디터 표현을 제공합니다.
- 파일: `Source/UKEditor/AnimGraph/UKAnimGraphNode_AimOffsetLookAt.h/.cpp`, `.../UKAnimGraphNode_BlendBoneByChannel.h/.cpp`, `.../UKAnimGraphNode_HitFeedback.h/.cpp`, `.../UKAnimGraphNode_Mirror.h/.cpp`

### UKEditor - 레벨 시퀀스 커스텀 트랙
- 역할: 시퀀서에 대사(Dialog), 대사 선택지, QTE(Quick Time Event), 하위 레벨 시퀀스 트랙을 추가하는 트랙 에디터 모음입니다.
- 파일: `Source/UKEditor/LevelSequence/UKTrackBaseEditor.h/.cpp`(공통 기반), `.../UKDialogTrackEditor.h/.cpp`, `.../SelectDialogTrackEditor.h/.cpp`, `.../UKQuickTimeEventTrackEditor.h/.cpp`, `.../UKLevelSequenceTrackEditor.h/.cpp`, `.../UKLevelSequenceFactory.h/.cpp`, `.../MovieSceneSequenceEditor_UKLevelSequence.h`

### UKEditor - 시퀀스 자동화(파이썬/서브시스템)
- 역할: 컷신 시퀀스 생성·바인딩·이벤트 섹션 조작을 파이썬 및 에디터 서브시스템에서 일괄 처리합니다(캐릭터 데이터·게임데이터 매니저 연동).
- 파일: `Source/UKEditor/Public/UKLevelSequencePythonLibrary.h` + `Source/UKEditor/Private/UKLevelSequencePythonLibrary.cpp`, `Source/UKEditor/Public/UKEditorLevelSequenceSubsystem.h` + `.../Private/UKEditorLevelSequenceSubsystem.cpp`

### UKEditor - 에디터 블루프린트 유틸리티
- 역할: 애니메이션 에셋, 로케이션 볼륨, 랜드스케이프 등을 에디터 스크립트에서 조작하는 블루프린트 함수 라이브러리입니다.
- 파일: `Source/UKEditor/Public/UKBlueprintEditorLibrary.h` + `Source/UKEditor/Private/UKBlueprintEditorLibrary.cpp`

### UKEditor - 게임플레이 태그 참조 카운터
- 역할: 레퍼런스 뷰어 그래프를 이용해 게임플레이 태그별 참조 개수를 집계합니다.
- 파일: `Source/UKEditor/Public/UKGameplayTagReferenceCounter.h` + `Source/UKEditor/Private/UKGameplayTagReferenceCounter.cpp`

### UKEditor - 프로퍼티 타입 커스터마이징
- 역할: 지역 이름, 프리미티브 가시성/틱 인터벌 컨텍스트, 소프트 데이터테이블 행 핸들의 디테일 패널 UI를 커스터마이징합니다.
- 파일: `Source/UKEditor/PropertyTypeCustomization/UKRegionNamePropertyTypeCustomization.h/.cpp`, `.../UKPrimitiveVisibilityContextCustomization.h/.cpp`, `.../UKSoftDataTableRowHandlePropertyTypeCustomization.h/.cpp`

### UKEditor - 마커 데이터 처리 커맨드릿
- 역할: 개발용 마커(버그·이슈 핀) 데이터를 커맨드라인 배치로 가공합니다.
- 파일: `Source/UKEditor/Public/Commandlets/UKMarkerDataProcessCommandlet.h` + `Source/UKEditor/Private/Commandlets/UKMarkerDataProcessCommandlet.cpp`

### UKEditor - TOD 에셋 정의
- 역할: 시간대(TimeOfDay) 마스터 프로파일 및 파라미터 스냅샷 에셋의 콘텐츠 브라우저 표시·열기 동작을 정의합니다.
- 파일: `Source/UKEditor/Public/AssetDefinition_TODMasterProfile.h` + `Source/UKEditor/Private/AssetDefinition_TODMasterProfile.cpp`, `.../AssetDefinition_TODParameterSnapshot.h/.cpp`

---

### 런타임 게임 기능 플러그인

### ZzAction - 타임라인 기반 스킬/액션 에디터 (런타임 + 에디터)
- 역할: 타임라인 트랙(애님·커브·노티파이·링크) 위에 스킬 동작을 편집·재생하는 확장형 액션 시스템입니다.
- 파일(런타임 코어): `Plugins/ZzAction/Source/ZzAction/Public/ZzActionComponent.h`, `.../ZzActionInstance.h`, `.../ZzActionData.h`, `.../ZzActionBlueprint.h`, `.../Timeline/ZzActionTimelineRunner.h`, `.../Node/ZzActionNode.h` 외 20개(`Plugins/ZzAction/Source/ZzAction/`)
- 파일(액션 노드): `Plugins/ZzAction/Source/ZzActionNodes/Public/ZzNotifyNode_PlayNiagaraEffect.h`, `.../ZzNotifyNode_PlaySound.h`, `.../ZzNotifyNode_MotionWarping.h`, `.../ZzNotifyNode_DisableRootMotion.h` 외 8개(`Plugins/ZzAction/Source/ZzActionNodes/`)
- 파일(에디터 타임라인 UI): `Plugins/ZzAction/Source/ZzActionEditor/Private/Timeline/SActionTimelineView.h/.cpp`, `.../Timeline/ZzActionTimelineModel.cpp`, `.../Tracks/ActionAnimTrackEditor.h/.cpp` 외 45개(`Plugins/ZzAction/Source/ZzActionEditor/Private/Timeline/`, `.../Tracks/`)
- 파일(액션 링크 그래프): `Plugins/ZzAction/Source/ZzActionEditor/Private/Graph/ActionLinkGraph.h/.cpp`, `.../Graph/ActionLinkGraphSchema.h/.cpp` 외 19개(`Plugins/ZzAction/Source/ZzActionEditor/Private/Graph/`)
- 파일(프리뷰 씬·에셋 툴킷): `Plugins/ZzAction/Source/ZzActionEditor/Public/ZzActionBlueprintEditorToolkit.h`, `.../PreviewScene/ZzActionPreviewActor.h`, `.../PreviewAction/PreviewActionComponent.h` 외 12개(`Plugins/ZzAction/Source/ZzActionEditor/Private/PreviewScene/`, `.../PreviewAction/`)
- 파일(태그 프레임워크·유틸·K2 노드): `Plugins/ZzAction/Source/ZzActionFramework/Public/ZzGameplayTagContainerMap.h`, `.../ZzGameplayTagLibrary.h`, `Plugins/ZzAction/Source/ZzActionUtilities/Public/ZzActionEditorBlueprintFunctionLibrary.h`, `Plugins/ZzAction/Source/ZzActionUncooked/Public/K2Nodes/K2Node_RunAction.h`

### UKStoryGraph - 시나리오/시네마틱 그래프
- 역할: 레벨 시퀀스를 노드-엣지로 잇는 스토리 진행 그래프 에셋과 전용 그래프 에디터를 제공합니다.
- 파일(런타임): `Plugins/UKStoryGraph/Source/UKStoryGraph/Public/UKStoryGraph.h`, `.../UKStoryCinematicNode.h`, `.../UKStoryCinematicEdge.h`(각 Private에 .cpp 쌍)
- 파일(에디터): `Plugins/UKStoryGraph/Source/UKStoryGraphEditor/Public/AssetEditor/AssetEditor_StoryGraph.h`, `.../AssetGraphSchema_StoryGraph.h`, `.../EdNode_StoryGraphNode.h`, `.../SEdNode_StoryGraphNode.h`, `.../AutoLayout/UKStoryForceDirectedLayoutStrategy.h` 외 37개(`Plugins/UKStoryGraph/Source/UKStoryGraphEditor/`)

### UKInteraction - 월드 상호작용(변형/바람/버블)
- 역할: 캐릭터·오브젝트가 지나갈 때 초목·물·천 등을 변형시키는 상호작용 마스크를 GPU로 그려내는 런타임 시스템입니다.
- 파일: `Plugins/UKInteraction/Source/UKInteraction/Public/Manager/UKInteractionManager.h`, `.../UKInteractionWorldSubsystem.h`, `.../Components/UKInteractionDrawComponent.h`, `.../Components/UKInteractionDeformComponent.h`(+Character/TwoSocket 변형), `.../Components/UKInteractionWindComponent.h`, `.../Components/UKInteractionBubbleComponent.h`, `.../Types/UKInteractionProfileBase.h`, `.../UKInteractionSettings.h`, `.../UKInteractionBPLibrary.h`(각 Private에 .cpp 쌍)
- 파일(에디터): `Plugins/UKInteraction/Source/UKInteractionEditor/Public/K2Nodes/K2Node_GetUKInteractionManager.h/.cpp`

### UKTimeOfDay - 시간대/하늘 연출 시스템
- 역할: 시간대 키 기반으로 하늘·조명·머티리얼 파라미터를 보간하고, 지역 볼륨별 프로파일을 덮어쓰는 TOD 시스템입니다.
- 파일(런타임): `Plugins/UKTimeOfDay/Source/UKTimeOfDay/Public/TODMasterProfile.h`, `.../TODParameterSnapshot.h`, `.../TODSkyInterface.h`, `.../UKTimeOfDayUpdateActor.h`, `.../UKTimeOfDayVolumeActor.h`, `.../TimeOfDayLocationVolume.h`, `.../UKWorldShadowScenario.h`, `.../UKTexturePackDataAsset.h`, `.../UKTimeOfDayBlueprintLibrary.h`(각 Private에 .cpp 쌍)
- 파일(에디터): `Plugins/UKTimeOfDay/Source/UKTimeOfDayEditor/Public/TODEditorSubsystem.h/.cpp`, `.../UKBPFTimeOfDay.h/.cpp`, `.../UKTimeOfDayEditorUtilityLibrary.h/.cpp`, `.../UKWorldShadowScenarioFactory.h/.cpp`, `.../UKTexturePackDataAssetFactory.h/.cpp`

### UKEventSystem - 느슨한 결합 이벤트 버스
- 역할: 게임플레이 태그를 키로 사용하는 전역 이벤트 발행/구독 시스템(GameInstanceSubsystem)입니다.
- 파일: `Plugins/UKEventSystem/Source/UKEventSystem/Public/UKEventManager.h`, `.../UKEventHandler.h`, `.../UKEventContexts.h`, `.../UKEventBlueprintLibrary.h`(각 Private에 .cpp 쌍)

### UKGameDataManager - 정적 게임 데이터 서비스
- 역할: 기획 정적 데이터(테이블)를 태스크 시스템으로 병렬 로딩·캐싱하고 조회 API를 제공합니다(JSON 파서 KongRapidJson 사용).
- 파일: `Plugins/UKGameDataManager/Source/UKGameDataManager/Public/UKGameDataManager.h`, `.../UKGameDataTable.h`, `.../UKGameDataManagerModule.h`, `.../UKGameDataTest.h`(각 Private에 .cpp 쌍)

### UKSqliteSystem - SQLite 저장/조회 시스템
- 역할: SQLite 데이터베이스 연결, 비동기 쿼리 워커, 세이브게임 연동을 담당하는 런타임 서브시스템입니다.
- 파일: `Plugins/UKSqliteSystem/Source/UKSqliteSystem/Public/UKSqliteManager.h`, `.../UKSqliteDatabase.h`, `.../UKSqliteStatement.h`, `.../UKSqliteQueryTask.h`, `.../UKSqliteSaveGame.h`, `.../UKDatabaseSetting.h`, `.../FetchUtil.h`, `Plugins/UKSqliteSystem/Source/UKSqliteSystem/Private/UKSqliteQueryWorker.h/.cpp`

### UKFoundations - 공용 경로/파일 유틸리티
- 역할: 정적 데이터·영속 데이터·백업·마커 데이터 경로 해석과 파일 입출력 헬퍼를 제공하는 최하위 기반 모듈입니다.
- 파일: `Plugins/UKFoundations/Source/UKFoundations/Public/UKPathResolver.h/.cpp`, `.../UKFileHelper.h` + `.../Private/UKFileHelper.cpp`, `.../UKFileDefine.h`

### UKPhysicalMaterial - 확장 피지컬 머티리얼
- 역할: 표면별 발소리·이펙트·게임플레이 이펙트(GameplayEffect)를 묶은 커스텀 피지컬 머티리얼 에셋입니다.
- 파일: `Plugins/UKPhysicalMaterial/Source/UKPhysicalMaterial/Public/UKPhysicalMaterial.h/.cpp`, `.../UKPhysicalMaterialModule.h/.cpp`
- 파일(에디터): `Plugins/UKPhysicalMaterial/Source/UKPhysicalMaterialEditor/Public/UKPhysicalMaterialFactory.h`, `.../UKExtendedPhysicalMaterialAssetAction.h`, `.../UKAssetAction_UKPhysicalMaterial.h`(Private에 .cpp 쌍)

### UKInteractiveFoliage - 상호작용 식생
- 역할: 캐릭터 접근 시 인스턴스드 메시 식생을 물리 액터로 전환·반응시키는 런타임 기능입니다.
- 파일: `Plugins/UKInteractiveFoliage/Source/UKInteractiveFoliage/Public/Component/UKFoliageInteractionComponent.h`, `.../Actors/UKPhysicalInteractiveFoliageActor.h`, `.../Types/UKInteractiveFoliageAssetUserData.h`, `.../Types/UKInteractiveFoliage_Types.h`(각 Private에 .cpp 쌍)

### UKRopeComponent - 로프 시뮬레이션 컴포넌트
- 역할: 절차적 메시로 그려지는 로프의 물리 시뮬레이션과 렌더링을 담당합니다.
- 파일: `Plugins/UKRopeComponent/Source/UKRopePlugin/Public/RopeComponent.h/.cpp`, `.../RopeSimulation.h/.cpp`

### UKPlatformToolkit - 이동/발판 플랫폼 액터
- 역할: 발판 액터와 그룹 액터로 이동 플랫폼 구성을 만들고, 에디터에서 선택 액터를 일괄 플랫폼화합니다.
- 파일: `Plugins/UKPlatformToolkit/Source/UKPlatformToolkit/Public/UKPlatformActor.h`, `.../UKPlatformGroupActor.h`(Private에 .cpp 쌍)
- 파일(에디터): `Plugins/UKPlatformToolkit/Source/UKPlatformToolkitEditor/Public/UKPlatformToolUtils.h`, `.../UKPlatformToolkitEditorCommands.h`, `.../UKPlatformToolkitEditorStyle.h`(Private에 .cpp 쌍)

### UKVfxBpLibrary / UKVfxHelper - VFX 블루프린트 라이브러리와 블룸 연동
- 역할: 나이아가라 이펙트용 블루프린트 함수와, 포스트프로세스 블룸 값을 나이아가라 파라미터로 실시간 동기화하는 헬퍼입니다.
- 파일: `Plugins/UKVfxBpLibrary/Source/UKVfxBpLibrary/Public/UKVfxBpLibraryBPLibrary.h/.cpp`, `Plugins/UKVfxBpLibrary/Source/UKVfxHelper/Public/UKBloomUpdaterComponent.h`, `.../UKBloomUpdaterWorldSubsystem.h`, `.../UKBloomNiagaraActor.h`, `.../UKBloomNiagaraDataInterface.h`, `.../UKVfxHelperBpLibrary.h`(각 Private에 .cpp 쌍)
- 파일(에디터): `Plugins/UKVfxBpLibrary/Source/UKVfxHelperEditor/Public/UKBloomUpdaterEditorSubsystem.h/.cpp`

### UKCrashReporterSystem - 크래시 리포트 부가정보 수집
- 역할: 크래시 덤프 분석용 커스텀 키-값 데이터를 엔진 서브시스템에서 수집해 리포트에 첨부합니다.
- 파일: `Plugins/UKCrashReporterSystem/Source/UKCrashReporterSystem/Public/UKCrashReporterSubsystem.h/.cpp`

### UKPCGHelper - PCG 파라미터 보조 라이브러리
- 역할: PCG(Procedural Content Generation) 컴포넌트의 파라미터를 블루프린트에서 설정·재생성하는 함수 라이브러리입니다.
- 파일: `Plugins/UKPCGHelper/Source/UKPCGHelper/UKPCGHelperBPLibrary.h/.cpp`, `.../UKPCGHelper.h/.cpp`

### UKPaletteTextureTool - 팔레트 텍스처 컬러 배리에이션
- 역할: 메시 영역별 팔레트 매핑과 인스턴스 시드 랜덤화로 색상 배리에이션을 만듭니다(에디터 창 포함).
- 파일: `Plugins/UKPaletteTextureTool/Source/UKPaletteTextureTool/Public/UKPaletteRandomController.h`, `.../UKMeshRegionMappingUserData.h`(Private에 .cpp 쌍), `Plugins/UKPaletteTextureTool/Source/UKPaletteTextureToolEditor/Public/UKPaletteTextureToolEditorBPLib.h`, `.../Interface_PaletteTextureToolWindow.h`

### UKPipelineToolsRuntime - 파이프라인 런타임 보조 함수
- 역할: 스플라인 내부 판정 등 파이프라인 도구가 런타임에서도 쓰는 기하 계산 함수 라이브러리입니다.
- 파일: `Plugins/UKPipelineToolsRuntime/Source/UKPipelineToolsRuntime/Public/UKPipelineToolsRuntimeBFL.h` + `.../Private/UKPipelineToolsRuntimeBFL.cpp`

---

### 에디터 파이프라인 도구 플러그인

### UKPipelineTools - 나무/식생 에셋 파이프라인
- 역할: 나무 줄기·가지·잎 데이터 구조와 접합 관리, 스키닝→스태틱 메시 변환, 가짜 그림자 베이크, 애니메이션 텍스처 베이크를 담당하는 아트 파이프라인 도구 모음입니다.
- 파일(데이터·함수 라이브러리): `Plugins/UKPipelineTools/Source/UKPipelineTools/Public/UKPipelineBPFLibrary.h`, `.../UKTrunkData.h`, `.../UKTrunkBranchShapeData.h`, `.../UKLeafData.h`, `.../UKTrunkConnectionData.h`, `.../UKTrunkConnectionDataManager.h`, `.../UKSkinToMeshBFL.h`, `.../UKFakeTreeShadowmapBaker.h`, `.../UKPipelineBPF_Image.h`, `.../PCG/UKPipePCGsBPFLibrary.h` 외 24개(`Plugins/UKPipelineTools/Source/UKPipelineTools/`)
- 파일(트리 메시 전용 에셋 에디터): `Plugins/UKPipelineTools/Source/UKTreeMeshEditor/UKTreeMeshEditorToolkit.h/.cpp`, `.../UKTreeMeshEditorViewport.h/.cpp`, `.../UKTreeMeshLibrary.h/.cpp` 외 20개(`Plugins/UKPipelineTools/Source/UKTreeMeshEditor/`)
- 파일(뷰포트 편집 모드): `Plugins/UKPipelineTools/Source/UKTreeEditMode/UKTreeEditModeEditorMode.h/.cpp`, `.../Tools/UKTreeEditModeInteractiveTool.h/.cpp` 외 16개(`Plugins/UKPipelineTools/Source/UKTreeEditMode/`)

### RegionalParameterTool - 지역별 커스텀 파라미터/볼륨 베이크
- 역할: 지역 범위(구·박스·각도) 액터로 색상·그림자 등 파라미터를 지정하고 볼륨 텍스처로 베이크해 런타임에서 샘플링합니다.
- 파일(에디터 액터): `Plugins/RegionalParameterTool/Source/RegionalParameterTool/Public/Actor/UKRegionalParameterBaseActor.h`, `.../Actor/UKRpVolumeBakeActor.h`, `.../Actor/UKRpWorldColorMultiply.h`, `.../Actor/UKRpFoliageColorMultiply.h`, `.../Actor/UKRpFakeShadowmapBaker.h` 외 15개(`Plugins/RegionalParameterTool/Source/RegionalParameterTool/Public/Actor/`)
- 파일(범위 컴포넌트·인터페이스): `.../Public/Component/UKRpSphereRangeComponent.h`, `.../Component/UKRpBoxRangeComponent.h`, `.../Component/UKRpAngleRangeComponent.h`, `.../Component/UKRpDistanceRangeComponent.h` 외 12개(`Plugins/RegionalParameterTool/Source/RegionalParameterTool/`)
- 파일(런타임): `Plugins/RegionalParameterTool/Source/RegionalParameterToolRuntime/Public/Actor/UKRpVolume.h`, `.../Actor/UKRpVolumeTriggerBox.h`, `.../Data/UKRpFoliageAssetUserData.h`, `.../FunctionLibrary/UKRPFunctionLibraryRuntime.h`(각 Private에 .cpp 쌍)
- 파일(OpenVDB 연동, 추정): `Plugins/RegionalParameterTool/Source/RegionalParameterTool/Public/RegionalParameterToolOpenVDB.h`

### UKAssetHelper - 에셋 수집/재배치/청크 구성
- 역할: 에셋 참조를 수집하고 PrimaryAssetLabel·청크 설정을 생성·재배치하는 쿠킹 패키징 보조 도구(커맨드릿 + Slate 위젯)입니다.
- 파일: `Plugins/UKAssetHelper/Source/UKAssetHelper/Public/UKAssetCollector.h`, `.../UKAssetCollectCommandlet.h`, `.../UKAssetRedistributor.h`, `.../UKPALBuilder.h`, `.../UKChunkConfig.h`, `.../UKChunkConfigBuilder.h`, `.../UKOutlinerHierarchyHelper.h`, `.../Widgets/SUKAssetCollectWidget.h`, `.../Widgets/SUKChunkConfigWidget.h`, `.../Widgets/SUKPalRuleWidget.h`, `.../Widgets/SUKBulkPalCreateWidget.h`, `.../Widgets/SUKAssetRedistributorWidget.h`(각 Private에 .cpp 쌍)

### UKEditorToolbarButton - 사내 데이터 다운로드 툴바
- 역할: 에디터 툴바에서 기획 시트/CSV를 내려받아 데이터테이블로 임포트하고 유저 데이터를 조회하는 버튼 모음입니다.
- 파일: `Plugins/UKEditorToolbarButton/Source/UKEditorToolbarButton/Public/UKEditorToolbarButton.h`, `.../UKEditorToolbarData.h`, `.../DownloadDelegate/GameDataDelegateClass.h`, `.../DownloadDelegate/StringCSVDelegateClass.h`, `.../Widget/UKToolbarGameDataButtonWidget.h`, `.../Widget/UserDataViewerUtilityWidget.h`, `.../Widget/UserDataBackItemUIWidget.h`, `.../Widget/UKEditorToolbarUtilityWidget.h`, `Plugins/UKEditorToolbarButton/Source/UKEditorToolbarButton/Private/DownloadDelegate/UKDataTableImporterCSV.h/.cpp`

### UKWorldPartitionDevTool - 월드 파티션 데이터레이어 프리셋
- 역할: 데이터 레이어·로케이션 볼륨·리전을 프리셋으로 저장하고 목록 UI에서 일괄 토글하는 레벨 작업 보조 도구입니다.
- 파일: `Plugins/UKWorldPartitionDevTool/Source/UKWorldPartitionDevTool/UKWorldDataLayerPresetData.h`, `.../UKWorldPartitionDevTool.h/.cpp`, `.../Widget/UKWorldPartitionDevToolWidget.h/.cpp`, `.../Widget/UKWorldPresetListWidget.h/.cpp`, `.../Widget/UKWorldRegionListWidget.h/.cpp`, `.../Widget/UKWorldLocationVolumeListWidget.h/.cpp` 외 9개(`Plugins/UKWorldPartitionDevTool/Source/UKWorldPartitionDevTool/Widget/`)

### UKMarkerTool - 인게임 마커(이슈 핀) 관리
- 역할: 레벨에 남긴 개발 마커를 HTTP로 서버와 주고받으며 목록·상세 패널로 관리합니다.
- 파일: `Plugins/UKMarkerTool/Source/UKMarkerToolEditor/Public/UKEditorMarkerManager.h`, `.../SUKMarkerList.h`, `.../SUKMarker.h`, `.../SUKMarkerData.h`, `.../UKMarkerDataDetailsView.h`, `.../UKMarkerToolEditorModule.h`(각 Private에 .cpp 쌍)

### UKWorldMinimapUtility - 월드 미니맵 캡처
- 역할: 씬 캡처 액터로 월드 전경을 타일 병렬 캡처해 미니맵 텍스처를 생성합니다.
- 파일: `Plugins/UKWorldMinimapUtility/Source/UKWorldMinimapUtilityEditor/UKWorldMinimapCaptureActor.h/.cpp`, `.../CaptureWorldMapParallelTask.h/.cpp`, `.../WorldMapCaptureBPLib.h/.cpp`, `.../WorldMapCaptureParam.h`, `.../UKWorldMapCaptureUtility.h`

### UKAudioAnimNotifyTool - 오디오 애님 노티파이 일괄 편집
- 역할: 애니메이션의 사운드 노티파이를 목록으로 훑어보고 일괄 수정하는 에디터 창입니다.
- 파일: `Plugins/UKAudioAnimNotifyTool/Source/UKAudioAnimNotifyToolEditor/Public/SUKAudioAnimNotifyMain.h`, `.../SUKAudioAnimNotifyList.h`, `.../SUKAudioAnimNotifyDetail.h`, `.../UKAudioAnimNotifyToolEditorModule.h`(각 Private에 .cpp 쌍)

### UKBlendSpaceGenerateTool - 블렌드스페이스 자동 생성
- 역할: 템플릿 블렌드스페이스를 복제해 선택한 애님 시퀀스로 교체·생성합니다.
- 파일: `Plugins/UKBlendSpaceGenerateTool/Source/UKBlendSpaceGenerateTool/Public/SUKBlendSpaceGenerateMain.h`, `.../SUKBlendSpaceGenerateAnimList.h`, `.../UKBlendSpaceGenerateTool.h`(각 Private에 .cpp 쌍)

### UKPlayerCharacterGenerateTool - 플레이어 캐릭터 에셋 생성
- 역할: 플레이어 캐릭터용 에셋 세트를 위젯 입력값에 따라 일괄 생성합니다.
- 파일: `Plugins/UKPlayerCharacterGenerateTool/Source/UKPlayerCharacterGenerateTool/Public/UKPlayerCharacterGenerateToolWidget.h`, `.../UKPlayerCharacterGenerateTool.h`(각 Private에 .cpp 쌍)

### UKCreateMaterialInstance - 머티리얼 인스턴스 일괄 생성
- 역할: 선택한 머티리얼로부터 머티리얼 인스턴스를 규칙에 맞춰 대량 생성합니다.
- 파일: `Plugins/UKCreateMaterialInstance/Source/UKCreateMaterialInstanceTool/Public/UKCreateMaterialInstanceToolWidget.h`, `.../UKCreateMaterialInstanceToolModule.h`(각 Private에 .cpp 쌍)

### UKAutomation - 자동화 검수 테스트
- 역할: 레벨 시퀀스 유효성, 나이아가라 이펙트 규칙 위반 등을 검사하는 자동화 테스트 모음입니다.
- 파일: `Plugins/UKAutomation/Source/UKAutomation/Public/UKNiagaraCheckUtility.h`, `.../UKAutomationDefine.h`, `Plugins/UKAutomation/Source/UKAutomation/Private/UKAutomationTest.cpp`, `.../UKAutomationTest_CheckLevelSequence.cpp`

### LevelStreamingOptimizer - 레벨 스트리밍 예산 조정
- 역할: 슬라이더 하나로 레벨 스트리밍 관련 CVar/설정 묶음을 예산에 맞게 일괄 세팅하는 개발자 설정 도구입니다.
- 파일: `Plugins/LevelStreamingOptimizer/Source/LevelStreamingOptimizer/Public/LSO_Settings.h/.cpp`, `.../LevelStreamingOptimizer.h/.cpp`

### Dropper - 물리 기반 에셋 배치 에디터 모드
- 역할: 브러시로 에셋을 뿌린 뒤 물리로 안착시켜 배치하고 결과를 베이크하는 뷰포트 편집 모드입니다(외부 제작 플러그인으로 추정).
- 파일: `Plugins/Dropper/Source/Private/DropperEdMode.h/.cpp`, `.../DropperEdModeToolkit.h/.cpp`, `.../ADropperBrush.h/.cpp`, `.../ADroppableActor.h/.cpp`, `.../ABakedDroppables.h/.cpp`, `.../SDropperPalette.h/.cpp`, `.../DropperSettings.h` 외 11개(`Plugins/Dropper/Source/Private/`)

---

### 담당 범위 내 예외 사항

### UKGradientToolPlugin - 미구현(빈 모듈)
- 역할: 그라디언트 관련 도구로 보이나 소스가 빌드 스크립트 하나뿐이라 실제 구현이 없습니다(추정).
- 파일: `Plugins/UKGradientToolPlugin/Source/UKGradientToolPlugin/UKGradientToolPlugin.Build.cs`

### Plugins/Runtime - 자체 제작 아님(Adobe Substance 3D)
- 역할: 자체 플러그인이 아니라 서드파티 Substance 3D 플러그인이 `Plugins/Runtime/Substance` 경로에 들어 있는 것입니다.
- 파일: `Plugins/Runtime/Substance/Substance.uplugin`, `Plugins/Runtime/Substance/Source/SubstanceCore/`, `.../SubstanceEditor/`, `.../SubstanceEngine/`, `.../SubstanceConnector/`

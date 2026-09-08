[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: 영속 상태 · 에디터 툴 API · 배치 실행 (UE 5.8 엔진 소스 확인, 2026-09-09)

경로 기준 `Engine/Source`.

## 1. 영속 상태

### 1.1 안정 식별자 (검증 완료)
- `AActor::ActorGuid` 멤버는 `#if WITH_EDITORONLY_DATA`(`Runtime/Engine/Classes/GameFramework/Actor.h:1088`), 접근자 `GetActorGuid()/GetActorInstanceGuid()`는 `#if WITH_EDITOR`(`:1147` 블록, `:1180/1183`). **런타임 게임 코드에서 호출 불가.**
- 런타임 API는 `FActorInstanceGuid::GetActorInstanceGuid(const AActor&)` — `Runtime/Engine/Public/WorldPartition/ActorInstanceGuids.h:26`. 쿠킹 시 `AActor::Serialize`가 GUID 쌍을 별도 직렬화(`Actor.cpp:1047-1050`), 로드 시 전역 어노테이션에 저장(`ActorInstanceGuids.cpp:192-206`). 레벨 인스턴스 안이면 `FGuid::Combine(LevelInstanceGuid, ActorGuid)`(`:148-172`)로 인스턴스별 고유.
- 함정: `AActor::PostRegisterAllComponents()` 끝에서 어노테이션 해제(`Actor.cpp:4140`) → `PostRegisterAllComponents` 오버라이드에서 `Super` 호출 전에 읽어 자체 `UPROPERTY FGuid`에 캐시. PIE는 항상 유효해 이 버그를 못 잡는다.
- `FSoftObjectPath`로 월드 파티션 액터 참조는 쿠킹 후에도 해석되지만 **셀이 로드된 경우만**(`WorldPartitionLevelStreamingPolicy.cpp:285-306`). 레벨 인스턴스 내부 액터는 `_{ContainerID}` 접미사(`WorldPartitionLevelHelper.cpp:484-500`). 주 키로 부적합, 디버그 보조용.

### 1.2 로드/언로드 훅 (런타임)
1. `FWorldDelegates::LevelAddedToWorld / PreLevelRemovedFromWorld / LevelRemovedFromWorld` — `Runtime/Engine/Classes/Engine/World.h:4591-4601`. 월드 파티션 셀도 `ULevel`이라 발동. 가장 안정적.
2. 셀별 `ULevelStreaming::OnLevelLoaded/OnLevelUnloaded/OnLevelShown/OnLevelHidden` — `LevelStreaming.h:636-648`(`UWorldPartitionLevelStreamingDynamic`이 상속).
3. 액터 자신의 `PostRegisterAllComponents()` / `BeginPlay()`에서 상태 복원(가장 단순).
4. `UWorldPartitionSubsystem::OnStreamingStateUpdated()` — `WorldPartitionSubsystem.h:115-116`.
5. `UWorld::OnWorldPartitionInitialized()` — `World.h:2983`. `DataLayerManager`가 이 브로드캐스트보다 먼저 초기화되므로(`WorldPartition.cpp:773-908`) 세이브 복원 시점으로 적합.
- `UWorldPartition::OnCellShown/OnCellHidden`은 내부 함수, 공개 델리게이트 없음. `OnActorDescInstance*`는 에디터 전용.

### 1.3 세이브 직렬화
- `USaveGame`(빈 베이스), `UGameplayStatics::SaveGameToSlot/LoadGameFromSlot/AsyncSaveGameToSlot/SaveGameToMemory` — `Runtime/Engine/Classes/Kismet/GameplayStatics.h:1134-1211`.
- `FObjectAndNameAsStringProxyArchive` — `Runtime/CoreUObject/Public/Serialization/ObjectAndNameAsStringProxyArchive.h:21-53`(`bLoadIfFindFails`, `bResolveRedirectors`). `Ar.ArIsSaveGame = true`면 `FProperty::ShouldSerializeValue`(`Property.cpp:1043-1056`)가 `UPROPERTY(SaveGame)`만 직렬화하고, 쿠킹 빌드에서도 프로퍼티 GUID·코어 리다이렉트가 활성화돼(`Class.cpp:1621-1622`) 이름 변경 마이그레이션이 가능.
- 데이터 레이어 상태는 엔진이 저장하지 않음(`WorldDataLayers.h:288-305` 모두 Transient). 요청 상태를 애셋 경로 키로 저장, 월드 파티션 초기화 후 부모→자식 순으로 복원(datalayer.md 5절).

## 2. 에디터 툴 API

### 2.1 액터 생성·수정·저장
- `UEditorActorSubsystem::SpawnActorFromClass(TSubclassOf<AActor>, FVector, FRotator, bool bTransient)` — `Editor/UnrealEd/Public/Subsystems/EditorActorSubsystem.h:228`, `SpawnActorFromObject` `:218`, `DestroyActor(s)` `:236/244`, 선택 제어 `:181-200`.
- `GEditor->AddActor(ULevel*, UClass*, const FTransform&, bool bSilent, EObjectFlags, bool bSelectActor)` — `Editor/UnrealEd/Classes/Editor/EditorEngine.h:1270`.
- `FActorSpawnParameters`(에디터): `OverridePackage` `World.h:441`, `InitialActorLabel` `:444`, `bCreateActorPackage`(OFPA 액터 전용 패키지 생성) `:492`, `ObjectFlags = RF_Transactional`.
- `AActor::SetActorLabel(const FString&, bool bMarkDirty)` `Actor.h:2742`, `SetFolderPath` `:2783`, `Tags` `:1344`, `SetPackageExternal` `:1154`, `Modify()` `:2389`.
- 저장(무프롬프트): `UEditorLoadingAndSavingUtils::SavePackages(const TArray<UPackage*>&, bool bOnlyDirty)` — `Editor/UnrealEd/Public/FileHelpers.h:86`, `SaveMap(UWorld*, AssetPath)` `:75`, `SaveDirtyPackages(bSaveMapPackages, bSaveContentPackages)` `:108`. `FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave=false, …, bSkipExternalObjectSave=false)` `:401`. OFPA 레벨은 맵 패키지만 저장하면 액터가 저장되지 않으므로 `Actor->GetExternalPackage()`를 모아 함께 저장.
- 저수준: `UPackage::SavePackage(UPackage*, UObject*, const TCHAR*, const FSavePackageArgs&)` `Package.h:1204`, `FSavePackageArgs::bSlowTask=false`(커맨드릿).

### 2.2 레벨 인스턴스 배치
- 표준 흐름: `World->SpawnActor<ALevelInstance>(Transform)` → `SetWorldAsset(TSoftObjectPtr<UWorld>)`(에디터, `LevelInstanceActor.h:74`) → `ULevelInstanceSubsystem::RequestLoadLevelInstance(ILevelInstanceInterface*, bool bUpdate)` `LevelInstanceSubsystem.h:82` 또는 동기 `BlockLoadLevelInstance` `:147`. `LoadLevelInstance`는 private.
- `CreateLevelInstanceFrom(const TArray<AActor*>&, const FNewLevelInstanceParams&)` `:134`. `FNewLevelInstanceParams`(`LevelInstanceTypes.h:84`): `Type`(LevelInstance/PackedLevelActor), `PivotType`, `bAlwaysShowDialog`(**커맨드릿에서 false 필수**), `LevelPackageName`, `bExternalActors=true`.
- `GetLevelInstanceBounds` `:122`로 카메라 포커스.

### 2.3 트랜잭션·카메라·리포트·메뉴
- `FScopedTransaction(const FText& SessionName)` — `Editor/UnrealEd/Public/ScopedTransaction.h:27`(`UE_NODISCARD_CTOR`, 이름 있는 지역 변수로), `Cancel()` `:33`. 변경 전 `Modify()`.
- 카메라: `GEditor->MoveViewportCamerasToActor(AActor&, bool bActiveViewportOnly)` `EditorEngine.h:1147`, `MoveViewportCamerasToBox(const FBox&, bool, float)` `:1185`, `UUnrealEditorSubsystem::SetLevelViewportCameraInfo(FVector, FRotator)` `UnrealEditorSubsystem.h:42`, `FEditorViewportClient::FocusViewportOnBox` `EditorViewportClient.h:1137`.
- 메시지 로그: `FMessageLog(FName)` `Runtime/Core/Public/Logging/MessageLog.h:27`, `Error/Warning/Info(FText)` `:57-61`, `Open()` `:78`, `Notify()` `:88`. 토큰: `FUObjectToken::Create(const UObject*)` `Misc/UObjectToken.h:23`, `FActorToken::Create(const FString& ActorPath, const FGuid& ActorGuid, const FText&)` `Logging/TokenizedMessage.h:715`(언로드된 액터도 지목 가능). 카테고리 등록 `FMessageLogModule::RegisterLogListing` `Developer/MessageLog/Public/MessageLogModule.h:33`.
- 알림: `FNotificationInfo` `Widgets/Notifications/SNotificationList.h:127`, `FSlateNotificationManager::Get().AddNotification` `NotificationManager.h:96`.
- 메뉴: `UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools")`(`LevelEditorMenu.cpp:372`에서 등록됨), 툴바 `"LevelEditor.LevelEditorToolBar.User"`(SlimHorizontalToolBar). 탭: `FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FName, FOnSpawnTab)` `TabManager.h:1561`, 그룹 `WorkspaceMenu::GetMenuStructure().GetToolsCategory()`.
- Editor Utility Widget: `UEditorUtilitySubsystem::SpawnAndRegisterTab(UEditorUtilityWidgetBlueprint*)` `Editor/Blutility/Public/EditorUtilitySubsystem.h:88`(Blutility는 5.8에서 엔진 소스 모듈). Scriptable Tools는 `Plugins/Runtime/ScriptableToolsFramework` + `Plugins/Editor/ScriptableToolsEditorMode`, Beta, 기본 비활성.

## 3. 배치 실행 (커맨드릿)

### 3.1 월드 파티션 빌더
- `UWorldPartitionBuilderCommandlet` — `Editor/UnrealEd/Classes/Commandlets/WorldPartitionBuilderCommandlet.h:14`. `-Builder=` 파싱 `WorldPartitionBuilderCommandlet.cpp:168`, 맵은 첫 토큰(콤마 구분, `*` 전체), `-AutoSubmit`, `-SCCProvider=None`.
- `UWorldPartitionBuilder` — `Editor/UnrealEd/Public/WorldPartition/WorldPartitionBuilder.h`: `ELoadingMode { Custom, EntireWorld, IterativeCells, IterativeCells2D }` `:43-49`, `PreRun/RunInternal/PostRun` `:78-86`, 인자 헬퍼 `HasParam` `:116`, `GetParamValue` `:128`(생성자에서 읽음), `IterativeCellSize=102400` `:155`. 데이터 레이어 제어 `bLoadNonDynamicDataLayers/IncludedDataLayers/ExcludedDataLayers` `:164-169`.
- 월드 로드: 커맨드릿은 `LoadWorldPackageForEditor(FStringView)` `Editor/UnrealEd/Public/EditorWorldUtils.h:63` + `FScopedEditorWorld(UWorld*, InitializationValues)` `:19-26`을 쓴다(`FEditorFileUtils::LoadMap`은 UI 결합이 강해 피함). 빌더 기본 초기화 값은 내비·AI 시스템 off(`WorldPartitionBuilder.cpp:60-71`).
- 액터 로드: `FWorldPartitionHelpers::ForEachActorWithLoading(UWorldPartition*, TFunctionRef<bool(const FWorldPartitionActorDescInstance*)>, const FForEachActorWithLoadingParams&)` — `Runtime/Engine/Public/WorldPartition/WorldPartitionHelpers.h:131`(`ActorClasses`, `ActorGuids`, `bGCPerActor`). 로드 없이 순회는 `ForEachActorDescInstance` `:88-95`. 영역 로드는 `FLoaderAdapterShape(UWorld*, const FBox&, const FString&)` `LoaderAdapter/LoaderAdapterShape.h:12`. `UWorldPartition::LoadAllActors`는 없다(`UActorDescContainerInstance::LoadAllActors`가 대체). 커맨드릿 GC는 KeepFlags 없이 전량 수집하므로 참조는 `FWorldPartitionReference`로 보유.
- 자체 커맨드릿: `UTDWorldGenCommandlet`은 `UWorldPartitionBuilder` 파생(`RunInternal`에서 생성·검증·베이크)으로 만드는 편이 로딩·저장·소스 컨트롤 처리를 공짜로 얻는다.

### 3.2 내비 빌드
- `UWorldPartitionNavigationDataBuilder`(`IterativeCells2D`, 전용 인자 `-CleanPackages`) — `WorldPartitionNavigationDataBuilder.cpp:56-69, 410-471`: `FNavigationSystem::Build` 후 셀별로 `ANavigationDataChunkActor`를 OFPA 패키지로 스폰·저장. 전제: `ARecastNavMesh::bIsWorldPartitioned`(`RecastNavMesh.h:833-835`) 켜짐, `bFixedTilePoolSize/TilePoolSize` `:681-687`.
- 정적 맵은 `Static` + 월드 파티션 청크 사전 빌드가 정석. 인보커(`UNavigationInvokerComponent`)는 Dynamic 계열과 짝.

### 3.3 HLOD
- `UWorldPartitionHLODsBuilder` 인자: `-SetupHLODs -BuildHLODs -DeleteHLODs -RebuildHLODs -FinalizeHLODs -DumpStats -DistributedBuild -BuilderIdx= -BuilderCount= -BuildHLODLayer= -BuildSingleHLOD=` — `WorldPartitionHLODsBuilder.cpp:162-180`. `UHLODLayer`(`HLOD/HLODLayer.h:53`): `EHLODLayerType { Instancing, MeshMerge, MeshSimplify, MeshApproximate, Custom, CustomHLODActor }`, `CellSize`, `LoadingRange`, `ParentLayer`. 액터 지정 `AActor::HLODLayer` `Actor.h:1058`, `bEnableAutoLODGeneration` `:559`.
- 탑다운은 가시거리가 짧아 HLOD 필요성이 낮다. Phase 4에서 프로파일링 후 결정.

### 3.4 자동화 테스트
- `IMPLEMENT_SIMPLE_AUTOMATION_TEST(TClass, PrettyName, TFlags)` — `Runtime/Core/Public/Misc/AutomationTest.h:4297`. 5.8은 `enum class EAutomationTestFlags`(`:88`), 마스크는 별도 상수 `EAutomationTestFlags_ApplicationContextMask`(`:144`). 올바른 예: `EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter`.
- `AFunctionalTest` — `Developer/FunctionalTesting/Classes/FunctionalTest.h:249`(`AssertTrue` `:415`, `FinishTest` `:678`).
- 실행: `-ExecCmds="Automation RunTests TDGame"`(`AutomationCommandline.cpp:610`). 에디터 헬퍼 `FAutomationEditorCommonUtils::CreateScopedEditorWorld` `Editor/UnrealEd/Public/Tests/AutomationEditorCommon.h:38`, `LoadMap` `:176`.

## 이 프로젝트 결정
- 안정 ID: `UTDPersistentStateComponent`가 `PostRegisterAllComponents` 전반부에서 `FActorInstanceGuid::GetActorInstanceGuid`를 읽어 `UPROPERTY(SaveGame) FGuid`에 캐시. 생성기 배치 액터는 베이크 시 결정론 GUID를 직접 기록.
- 복원 훅: 액터 `BeginPlay`(자기 복원) + `FWorldDelegates::PreLevelRemovedFromWorld`(언로드 전 기록). 세이브는 `ArIsSaveGame` 프록시 아카이브 + 포맷 버전.
- 툴: 액터 `CallInEditor` + `FMessageLog`(`FActorToken`)로 시작, 커맨드릿은 `UWorldPartitionBuilder` 파생.

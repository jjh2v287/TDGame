# Lessons — editor (에디터 파이썬·MCP)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A.

### L-editor-01 `delete_all_material_expressions`는 커스텀 출력 노드를 안 지운다
- 증상: 머티리얼을 재구성해도 `LandscapeGrassOutput` 같은 커스텀 출력 노드가 남는다.
- 해결: 머티리얼은 삭제 후 재생성한다(`Tools/WorldGen/editor_make_landscape_material.py`).
- 범위: UE 5.8 `MaterialEditingLibrary`
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-02 에디터 파이썬에 없는 API 목록
- 증상: `Actor.add_component_by_class`, `set_is_spatially_loaded`, 랜드스케이프 생성, `LandscapeLayerInfoObject.layer_name` 설정이 파이썬에서 안 된다.
- 해결: `set_is_spatially_loaded` → `set_editor_property("is_spatially_loaded", …)`; 랜드스케이프 생성 → C++ `UTDLandscapeEditorLibrary`; 그 외는 C++ 에디터 함수(OP-20).
- 범위: UE 5.8 에디터 파이썬 3.11
- 증거: Tools/WorldGen/editor_build_open_world.py가 C++ 경로를 사용 — 미검증(별도 증거 없음)
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-03 화면 캡처는 `EditorAppToolset.CaptureViewport`만 신뢰한다
- 증상: `HighResShot`·`CaptureEditorImage`가 갱신되지 않은 화면을 찍는다.
- 해결: MCP `EditorAppToolset.CaptureViewport`에 `captureTransform`을 지정해 찍는다(`Tools/WorldGen/capture_views_mcp.py`).
- 범위: UE 5.8 MCP 플러그인
- 증거: Docs/Validation/ashen-vale/ 캡처가 이 경로로 생성됨
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-04 랜드스케이프 머티리얼 재생성 뒤 검게/체커로 보인다
- 증상: 머티리얼 스크립트를 다시 돌리면 랜드스케이프가 검게 또는 체커 무늬로 보인다.
- 원인: 스크립트가 `landscape_material`을 다시 지정해 셰이더가 재컴파일된다.
- 해결: 셰이더 컴파일 2~4분 뒤에 캡처한다.
- 범위: UE 5.8, Tools/WorldGen/editor_make_landscape_material.py
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-05 PCG 그래프 파이썬 스크립팅 실측
- 증상: 선택자 `set_attribute_name` 무시, 점 데이터로 bounds 제한 불가, 그래프 사용자 파라미터를 파이썬에서 못 만듦 등.
- 해결: 본문은 `Tools/WorldGen/README.md` 'PCG 그래프 스크립팅 실측' 절(정본, 이동하지 않음).
- 범위: UE 5.8 PCG, P3-07
- 증거: Docs/Validation/P3-07-pcg-biome.md
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-editor-06 PIE가 2~3fps: 백그라운드 스로틀과 편집기 뷰포트의 Navigation 표시 플래그
- 증상: PIE 프레임이 2~3fps로 고정되고 `t.MaxFPS 60`이 효과가 없다. `stat dumpframe`에서 게임 스레드 333ms 중 307ms가 `Game thread idle time`(엔진이 일부러 쉼). 이를 풀면 다음 프레임 400ms가 `NavMeshRenderingComponent … STAT_NavMesh_GatherDebugDrawingGeometry`.
- 원인 1: `UEditorEngine::ShouldThrottleCPUUsage`는 에디터가 포그라운드도 아니고 포커스도 없으면(예: Claude·Blender 창을 보며 PIE 관찰, MCP로 PIE 구동) 편집기 설정 `bThrottleCPUWhenNotForeground`(기본 True)에 따라 3fps로 제한하고 뷰포트 렌더도 끈다. `t.MaxFPS`보다 우선한다.
- 원인 2: `UNavMeshRenderingComponent::IsNavigationShowFlagSet`은 PIE 월드에서 게임 뷰포트뿐 아니라 **모든 편집기 뷰포트**의 `Navigation` 표시 플래그(P키)를 검사한다. 하나라도 켜져 있으면 동적 내비메시(`RuntimeGeneration=Dynamic`, 월드 파티션 스트리밍으로 타일이 계속 재생성됨)가 갱신될 때마다 전체 디버그 지오메트리를 게임 스레드에서 다시 만든다(이 맵에서 프레임당 약 400ms). `ShowFlag.Navigation 0` 콘솔 변수와 레벨 액터의 `bEnableDrawing`(Transient, 저장 안 됨)은 이 검사에 영향이 없다.
- 해결: ① 편집기 설정 `bThrottleCPUWhenNotForeground=False`. 클래스가 `config=EditorSettings`라 사용자 층은 프로젝트 `Saved/`가 아니라 사용자 전역 `%LOCALAPPDATA%/UnrealEngine/5.8/Saved/Config/WindowsEditor/EditorSettings.ini`이다. 이 파일에 `=True`가 명시돼 있으면 프로젝트 `Config/DefaultEditorSettings.ini`의 `False`를 덮으므로, 에디터를 닫고 그 줄을 False로 고친다(이후 에디터가 파일을 다시 쓰면 기본값과 같은 키는 빠지고 프로젝트 기본값이 적용된다; 2026-09-19 확인). 편집기 환경설정 UI(Performance > Use Less CPU when in Background)로 꺼도 같다 ② 편집기 주 뷰포트 Show > Navigation(P키) 끄기. 저장 위치는 `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`의 `EditorShowFlagsString` 안 `Navigation=1`이며 에디터를 닫은 뒤 바꿔야 유지된다. 결과 28ms(약 35fps).
- 진단 도구: `python Tools/pie_profile.py`.
- 범위: UE 5.8 에디터 PIE, LV_DarkFantasy_OpenWorld(동적 내비메시)
- 증거: `Saved/AgentOps/20260919/pie_profile.json`(수정 전 0.333s 고정 → 내비 드로잉 0.40s → 0.028s), 엔진 소스 `EditorEngine.cpp:5305`, `NavMeshRenderingComponent.cpp:1799`
- 날짜·상태: 2026-09-19 active
- 발견: claude

### L-editor-07 에디터 Python에서 월드 서브시스템을 가져올 함수가 없다
- 증상: `unreal.SubsystemBlueprintLibrary.get_world_subsystem(...)` → `AttributeError: module 'unreal' has no attribute 'SubsystemBlueprintLibrary'`. `unreal`에는 `get_editor_subsystem`·`get_engine_subsystem`과 `EditorSubsystemBlueprintLibrary`만 있고, UWorld 객체에도 subsystem 관련 메서드가 없다.
- 해결: 서브시스템 C++에 `UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject")) static UTDXxxSubsystem* GetXxxSubsystem(const UObject* WorldContextObject)`를 두고 Python에서 `unreal.TDXxxSubsystem.get_xxx_subsystem(world)`로 부른다(예: `UTDMonsterThinkSubsystem::GetMonsterThinkSubsystem`). 헤더 변경이라 재시작 빌드(L-build-01).
- 범위: UE 5.8 에디터 Python(PIE 월드 포함)
- 증거: 2026-09-24 `Tools/MonsterAI/editor_pie_monster_probe.py` 실패 → 정적 접근자 추가 후 PIE 검증 통과
- 날짜·상태: 2026-09-24 active
- 발견: claude

### L-editor-08 에디터 안(MCP) 자동화 테스트로 `TDGame.Combat` 전체를 돌리면 MegaMagic 잔상 테스트에서 에디터가 죽는다
- 증상: `TDGame.Combat.MegaMagic.VisualTailStopsGameplayAndExpires` 실행 중 `Assertion failed: (Index >= 0) & (Index < ArrayNum)` / `UNiagaraScript::GetLastGeneratedVMId() [NiagaraScript.cpp:985]` ← `UNiagaraComponent::ActivateInternal` ← `WaitForCompilationComplete`. 클라이언트에는 `ConnectionResetError: [WinError 10054]`만 보인다.
- 원인(추정): 테스트가 `NewObject<UNiagaraSystem>`로 만든 빈 시스템을 활성화하면 에디터의 Niagara 컴파일 관리자가 생성된 VM 아이디가 없는 스크립트를 컴파일 요청한다. 헤드리스 `UnrealEditor-Cmd -NullRHI` 실행에서는 같은 테스트가 통과한다.
- 해결: 전체 회귀는 헤드리스로 돌린다: `UnrealEditor-Cmd.exe TDGame.uproject -ExecCmds="Automation RunTests TDGame" -TestExit="Automation Test Queue Empty" -unattended -NullRHI -NoSplash -NoSound -log=<이름>.log`. 에디터 안에서는 `--filter`로 이 테스트를 피한다. 에디터가 죽으면 `python Tools/ue_editor.py ensure`.
- 범위: UE 5.8 에디터 + MCP AutomationTestToolset, 2026-09-24 기준 테스트 코드
- 증거: 2026-09-24 에디터 내 3회 재현(전체·단독), 헤드리스 통과(`Saved/Logs/TDMonsterAI_Headless.log`). 변경 전 상태에서의 재현은 미검증
- 날짜·상태: 2026-09-24 active
- 발견: claude

### L-editor-09 오픈월드 심리스 이동이 60초 넘게 준비되지 않는다: 레벨 RecastNavMesh의 `bDoFullyAsyncNavDataGathering=True`
- 증상: `TDGame.SeamlessTravel.RoundTripCompletesWithoutLoadingGap`·`TDGame.WorldState.ChestStateSurvivesStreaming` 실패, 로그 `SeamlessTravel: 'MainCrypt' still not ready after 60s`. 던전 지오메트리는 스트리밍되지만 목적지(305000,303400)에 내비 투영이 안 되고 `is_navigation_being_built_or_locked`가 계속 True다. 끝에 나오는 `LogCrowdFollowing: Unable to find RecastNavMesh instance while trying to create UCrowdManager instance`는 PIE 종료 때 내비 데이터가 해제되며 찍히는 로그라 원인이 아니다(통과하는 테스트에도 나온다). PIE 월드에 RecastNavMesh는 있다.
- 원인: `LV_DarkFantasy_OpenWorld`의 RecastNavMesh 외부 액터(`__ExternalActors__/.../7/FU/6R2JE7V130UWRIM4IVAA0P`)가 2026-09-19(커밋 06f6b0f)에 인스턴스 값 `bDoFullyAsyncNavDataGathering=True`로 저장됐다. `Config/DefaultEngine.ini`의 `False`보다 액터 인스턴스 값이 우선한다. True이면 타일 지오메트리를 게임 스레드 밖에서 모은다(`ShouldGatherDataOnGameThread`). 이 맵에서는 동적 생성 대기열이 60초 안에 비지 않았다. 대기열이 끝나지 않는 엔진 내부 경로는 미검증이다.
- 해결: `python Tools/run_in_editor.py Tools/WorldGen/editor_setup_navmesh_dynamic.py`를 실행한다. 이 도구가 이제 `do_fully_async_nav_data_gathering=False`도 설정하고 저장한다. 같은 편집기 상태에서 값만 바꿔 비교(A/B)하면 True는 이동 요청 후 35초 넘게 목적지 내비가 없었고, False는 7초 안에 생겼다. 인스턴스 재정의 값은 에디터 Python `get_editor_property`로 설정 파일과 대조한다(`force_rebuild_on_load=False`·`tile_pool_size=4096`·`agent_radius=35`도 인스턴스 값이지만 2026-09-12 통과 때부터 같았다).
- 범위: UE 5.8, 월드 파티션 + `RuntimeGeneration=Dynamic` 오픈월드 맵
- 증거: 수정 뒤 에디터 안 SeamlessTravel 1/1·WorldState 2/2 통과(`Saved/AgentOps/automation_tests_20260925-0108.json`, 준비 8.76s). 헤드리스 `-NullRHI` 3/3 통과(`Saved/Logs/TDNavFix_Headless.log`, 준비 0.41~7.77s)
- 날짜·상태: 2026-09-25 active
- 발견: claude


### L-editor-10 몽타주 `delete_asset`·`delete_loaded_asset`이 True를 돌려주고 "Force Deleting 1 Package(s)"를 찍는데 .uasset이 디스크·레지스트리에 남는다
- 증상: 방금 만들었거나 PIE에서 재생한 AnimMontage를 지우면 `does_asset_exist`가 계속 True이고, 같은 경로 재생성이 "Destination already exists on disk or in memory; overwriting is forbidden"으로 막힌다. 파일 읽기 전용·참조자는 없다.
- 해결: `asset.get_outermost()` → `EditorLoadingAndSavingUtils.unload_packages([package])` → `SystemLibrary.collect_garbage()` → `os.remove(파일)` → `AssetRegistry.scan_paths_synchronous([폴더], True)`. 그래도 메모리에 남으면(디스크엔 없음) 에디터를 재시작한다. 구현: `Tools/BlenderAnimation/import_sword_attack01.py` `remove_previous`.
- 범위: UE 5.8 에디터 Python, `EditorAssetLibrary`
- 증거: `Saved/Logs/TDGame.log` 2026.09.25-03.15 "Force Deleting 1 Package(s)" 뒤 on_disk=True, unload 경로로 on_disk=False
- 날짜·상태: 2026-09-25 active
- 발견: claude

### L-editor-11 UE 5.8 Python으로는 PIE 월드에 액터·컴포넌트를 만들 수 없고, 슬레이트 틱 콜백 안의 고해상도 스크린샷은 콜백을 재진입시킨다
- 증상: `Actor.add_component_by_class`, `GameplayStatics.begin_deferred_actor_spawn_from_class`가 없다(AttributeError). `EditorActorSubsystem.spawn_actor_from_class`는 편집기 월드에 만든다. PIE 월드 복사본의 StaticMeshActor를 소켓에 붙여도 캡처에 보이지 않았다. `register_slate_post_tick_callback` 안에서 `AutomationLibrary.take_high_res_screenshot`을 부르면 같은 라벨이 수십 번 추가되고 몽타주가 멈춘다.
- 해결: 게임 무기 부착은 C++에서 한다(현재 미구현). 캡처는 라벨을 목록에 먼저 넣은 뒤 스크린샷을 부르고 실행당 한 장만 찍는다(`Tools/BlenderAnimation/editor_pie_capture_sword_attack01.py`). 종료 판정은 경과 시간이 아니라 `montage_is_playing`으로 한다.
- 범위: UE 5.8 PythonScriptPlugin, PIE
- 증거: `Docs/Validation/BlenderAnimation/sword-attack01-pie.json`(첫 시도 몽타주 위치 0.2011 고정)
- 날짜·상태: 2026-09-25 active
- 발견: claude

### L-editor-12 에디터 바이너리 `-game`은 하위 프로세스로 띄우면 로그 없이 즉시 끝난다 — 프레임 측정은 쿠킹 스테이징본으로
- 증상: `UnrealEditor.exe TDGame.uproject /Game/Level/LV-Cambat -game -csvCaptureFrames=… -ExitAfterCsvProfiling`을 Python `subprocess.run`으로 실행하면 약 30초 뒤 종료 코드 0으로 끝나지만 `Saved/Logs`·`Saved/Profiling/CSV`에 아무것도 남지 않는다(2회 재현, 원인 미검증). 쿠킹본 CSV는 `_csv.Error: field larger than field limit (131072)`로 읽기 실패.
- 해결: `python Tools/cook_single_map.py --label <이름>`(BuildCookRun, LV-Cambat 약 7분) 후 `python Tools/measure_game_frames.py --label <이름>`로 `Saved/StagedBuilds/Windows/TDGame.exe`를 측정한다. CSV 끝의 메타데이터 칸 때문에 `csv.field_size_limit(2**31 - 1)`(sys.maxsize는 Windows에서 OverflowError).
- 범위: UE 5.8.2 런처 엔진, Windows, CSV 프로파일러
- 증거: `Saved/AgentOps/frames_baseline_staged.json`(게임 스레드 평균 7.23ms), `Saved/AgentOps/20260930/baseline.md`
- 날짜·상태: 2026-09-30 active(원인 미검증)
- 발견: claude

### L-editor-13 블루프린트 컴파일 오류가 있으면 MCP `StartPIE`가 모달 대화상자에 막혀 영원히 돌아오지 않는다
- 증상: `pie_check_*.py`가 시간 초과, 로그 끝이 `BlueprintLog: Warning: 블루프린트 컴파일에 실패했습니다. BP_TDCombatCharacter`에서 멈춤. 원인은 Mover 컴포넌트 템플릿의 `Walking 매핑된 … 무브먼트 모드에 필요한 CommonLegacyMovementSettings SharedSettingsClass가 없습니다`(Mover 공유 설정은 `PostLoad`·`PreSave`·`OnRegister`에서만 채워지는데 부모 교체 직후 저장 전에 컴파일함).
- 해결: 창 제목 `블루프린트 에셋 컴파일 N오류`인 창에 `WM_CLOSE`(PostMessage 0x0010)를 보내 PIE를 취소한다. 블루프린트는 저장 → 컴파일 → 저장 순서로 다시 저장한다(`Tools/Movement/editor_migrate_character_assets.py`). PIE 탐침은 호출 한 번에 약 1초가 걸리므로 빠른 동작은 `unreal.register_slate_post_tick_callback`으로 에디터 안에서 매 프레임 기록한다. `UAIBlueprintHelperLibrary`의 Python 이름은 `unreal.AIHelperLibrary`다. LV-Cambat에는 `NavMeshBoundsVolume`이 없어 경로 탐색이 항상 실패한다(클릭 이동 검증은 LV_TDMegaMagicArena).
- 범위: UE 5.8.2 에디터 + 공식 MCP, Mover 폰 블루프린트
- 증거: `Saved/Logs/TDGame.log`(2026-09-29 16:10~16:20 UTC), `Tools/Movement/editor_pie_player_probe.py`
- 날짜·상태: 2026-09-30 active
- 발견: claude

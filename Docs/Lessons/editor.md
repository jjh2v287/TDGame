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
- 보충(2026-10-31): 빌드 없는 대안으로 unreal.ObjectIterator(서브시스템 클래스) 중 get_world()==월드인 것을 고른다(pie_lookdev_fluid.py find_world_subsystem). 에디터 월드에는 CDO 만 있어 PIE 에서만 확인 가능 — PIE 확인 전 미검증

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

### L-editor-14 `MaterialEditingLibrary.set_material_instance_*_parameter_value`가 성공해도 False를 돌려준다
- 증상: UE 5.8 에디터 Python에서 벡터·스칼라 파라미터 설정 호출이 매번 `False`인데, 다시 읽으면 값이 바뀌어 있다.
- 해결: 반환값으로 판정하지 말고 부모의 `get_vector_parameter_names`·`get_scalar_parameter_names`로 이름 존재를 먼저 확인한 뒤 설정하고, 필요하면 `get_material_instance_*_parameter_value`로 다시 읽어 확인한다(`editor_build_realm.py` 방식).
- 범위: UE 5.8 에디터 Python
- 증거: 2026-10-14 claude 세션(`Tools/scratch/realm_lookdev.py` 출력 False → `realm_material_params.py` 재조회 값 반영)
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-editor-15 에디터 Python `set_collision_enabled(NO_COLLISION)`으로 끈 평면 충돌이 PIE에서는 살아나 폰을 막는다
- 증상: 에디터 월드에서 `get_collision_enabled()`는 NO_COLLISION인데, PIE에서 같은 StaticMeshActor(엔진 Plane 안개·바닥 평면)가 capsule trace에 block=True로 걸리고 폰이 몸통 높이 평면 가장자리에서 멈춘다(오르막에서만 재현).
- 원인: 충돌 프로필 이름이 기본값(BlockAll)으로 남아 있어, 저장·스트리밍 로드 뒤 프로필 값이 다시 적용된다(가정: 프로필 재적용 경로는 소스 미확인, 현상은 실측).
- 해결: `comp.set_collision_profile_name("NoCollision")`을 함께 호출한다(`editor_build_open_world.spawn_planes`에 반영). 이미 배치된 평면은 월드 파티션 액터를 로드한 뒤 프로필을 바꿔 저장한다(이번에 338개).
- 범위: UE 5.8 에디터 Python으로 만든 StaticMeshActor 전반
- 증거: `Saved/AgentOps/20261014/piewalk/`(PIE 탐침 출력 Fog_2·Floor_5 block=True), 수정 뒤 실제 이동 입력 걷기 6/6 통과
- 날짜·상태: 2026-10-14 active
- 발견: claude
- 재발(2026-10-31): FluidFX 무대 생성기 9개(fog·foliage·hvol·input·rain·track·water_particle·water_props·water)가 짝 없이 NO_COLLISION 만 호출 → 안개 무대 `TDGen_FogPassiveField`(200 m 정적 안개 상자)가 저장 맵에서 QUERY_AND_PHYSICS·BlockAll, 폰이 안에 갇혀 입력 걷기 0 cm(fog_f2). 에디터 월드 조회 `Saved/AgentOps/20261031/f2walk/query_blockers.py`. 재발 방지 검사 `python Tools/check_collision_disable.py`(생성기 수정 워크플로 wf_a4e5db9c-ee8)

### L-editor-16 재질 Python 편집 함정 세 가지: 잘못된 MaterialProperty 값은 크래시, 함수 표현식 일괄 삭제는 절반만, Vector4f는 import_text로
- 증상: ① `MaterialEditingLibrary.get_material_property_input_node(material, <유효하지 않은 MaterialProperty 값>)`로 열거형을 훑다가 에디터가 크래시(로그 `Saved/AgentOps/20261014/topdown/laneM/editor_crash_tail.log`) ② `delete_all_material_expressions_in_function`이 표현식의 약 절반만 지운다 ③ `MaterialExpressionFunctionInput.preview_value`(Vector4f)는 생성자 인수·속성 대입으로 값이 들어가지 않는다.
- 해결: ① 알려진 값(`MP_OPACITY_MASK` 등)만 넘기고 열거형 전체 순회 금지 ② `get_material_function_expressions` → `delete_material_expression_in_function`를 남은 것이 없을 때까지 반복(`editor_make_topdown_materials.clear_function`) ③ `v = unreal.Vector4f(); v.import_text("(X=1.0,Y=0.0,Z=0.0,W=0.0)")`.
- 범위: UE 5.8 에디터 Python 재질·재질 함수 편집
- 증거: P3-17 재질 갈래 실행(`Tools/WorldGen/editor_make_topdown_materials.py`), 크래시 로그 위 경로
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-editor-17 팩 마스터를 복제해 Masked로 바꿔도 `GetBlendMode()`가 Opaque — 저장된 `bCanMaskedBeAssumedOpaque`가 따라온다
- 증상: `/Game/DarkFantasyTopDown/.../M_Bark`(Opaque)를 `duplicate_asset`으로 복제해 `blend_mode`를 BLEND_MASKED로 바꾸고 OpacityMask를 연결·재컴파일해도, 컷아웃이 전혀 안 먹는다. `get_editor_property("blend_mode")`는 MASKED인데 `material.get_blend_mode()`와 자식 MI의 `get_blend_mode()`는 BLEND_OPAQUE. 블렌드 모드 토글·`update_material_instance`·재부모화로도 안 바뀐다.
- 원인: `UMaterial::GetBlendMode()`는 Masked라도 `bCanMaskedBeAssumedOpaque`가 참이면 Opaque를 돌려준다. 이 UPROPERTY는 옛 버전 변환(`VER_UE4_MATERIAL_MASKED_BLENDMODE_TIDY` 등) 때만 계산돼 저장되고 일반 편집으로는 다시 계산되지 않는다(엔진 `Material.cpp` 3079·3322·7442행). Python에는 노출되지 않는다.
- 해결: C++ 에디터 함수 `UTDWorldGenEditorLibrary::ClearMaskedAssumedOpaque(UMaterial*)`(공개 멤버를 false로 + PostEditChange)를 `editor_make_topdown_materials.py`가 마스터마다 부른다. 확인은 `material.get_blend_mode()`로 한다(속성값으로 확인하지 않는다).
- 범위: UE 5.8, 오래된 팩에서 복제한 재질을 Opaque→Masked로 바꿀 때
- 증거: `Saved/AgentOps/20261014/topdown/laneM/bark_w1.png`(줄기 안 잘림) ↔ `bark_w1_fixed.png`(잘림)
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-editor-18 `bGameCameraCutThisFrame`은 그리기 직후 지워진다 — latent 명령에서 읽으면 컷을 놓치고, 헤드리스에서는 한 번 켜지면 계속 남는다
- 증상: PIE 자동화 시험에서 "카메라 컷 0회"를 latent 명령으로 읽으면, 일부러 `SetGameCameraCutThisFrame`을 불러도 0으로 나온다. 헤드리스(뷰포트 그리기 없음)에서는 컷이 한 번 생기면 이후 매 틱 참으로 읽혀 과하게 센다.
- 원인: `APlayerController::SetGameCameraCutThisFrame`이 켠 값은 `LocalPlayer::CalcSceneView`에서 `bInCameraCut`으로 쓰이고 `GameViewportClient`가 그린 직후 끈다. latent 명령은 그 뒤에 돈다. 헤드리스는 그리기가 없어 끄는 쪽이 없다.
- 해결: `FWorldDelegates::OnWorldPostActorTick`(카메라 관리자 갱신 뒤, 그리기 전)에서 프레임마다 읽는다(`Source/TDGameEditor/Tests/TDRealmTravelPieTests.cpp`). 헤드리스 값은 상한(과대 계수)으로만 해석하고, 카메라 위치 점프(프레임당 50m 초과)를 함께 단언한다.
- 범위: UE 5.8 PIE·헤드리스 자동화 시험에서 카메라 컷·렌더 기록 초기화를 셀 때
- 증거: 엔진 `LevelTick.cpp`(UpdateCameraManager 뒤 OnWorldPostActorTick), 반박 검토에서 기존 단언이 컷을 못 잡는 것을 확인(2026-10-15 P3-19)
- 날짜·상태: 2026-10-15 active
- 발견: claude

### L-editor-19 PIE 성능·화면 측정: CSV 프로파일은 헤더가 파일 끝에 있고, 전역 노출이 수동이라 어두운 곳은 측정 전에 노출을 올려야 한다
- 증상: ① `csvprofile start/stop`으로 받은 CSV를 첫 줄 헤더로 읽으면 지점마다 표본이 8~47개로 끊기고 `GPUTime` 열이 없다고 판단된다. 측정 중 통계 열이 늘면서 행 길이가 378→379→381로 바뀐다. ② 협곡·갱도 경계 촬영은 표본 휘도가 0.002~0.05라, 10~25% 밝기 계단을 넣어도 계단 측정이 잡지 못한다.
- 원인: ① 최종 헤더는 메타데이터 행(`[HasHeaderRowAtEnd],1,...`) 바로 앞 줄이다. 열은 뒤에 덧붙고, `GPUTime`·`FrameTime`은 있다. 패스별 GPU 열(`GPU/*`)은 `r.GPUCsvStatsEnabled 1`일 때만 있다. ② 필드 PPV가 AEM_MANUAL·바이어스 10.4(`editor_build_open_world.py` LOOK)라 깊은 곳을 자동으로 밝히지 않는다.
- 해결: ① 끝 헤더로 열 이름을 정하고, 각 행은 앞쪽 열부터 맞춘다(`Tools/WorldGen/pie_perf_memory.py` `read_csv_columns`). ② 측정용 촬영만 `r.ExposureOffset`(치트 CVar, `pie_capture_spots.py --exposure-offset 3`)으로 일괄 올린다. 그림자 가장자리를 가리려면 `r.ShadowQuality 0`으로 A/B를 찍는다.
- 범위: UE 5.8 에디터 PIE에서 CSV 프로파일·화면 휘도 측정
- 증거: `Saved/WorldGen/PiePerf/p3-19_perf/summary.md`, `Saved/WorldGen/PieShots/p3-19_spots`(어두움) ↔ `p3-19_spots_ev3`·`p3-19_spots_ev3_noshadow`
- 날짜·상태: 2026-10-15 active
- 발견: claude

### L-editor-20 헤드리스 자동화 시험과 에디터 PIE 측정을 동시에 돌리면 둘 다 거짓 실패한다
- 증상: 헤드리스 `TDGame.SeamlessTravel.RealmConnectorWalk`(5000cm 넘는 이동 프레임·경계 통과 2회)와 `PreloadCancelAllowsOtherTarget`(기대 HollowCave, 실제 SunkenStarfieldSoutheast)가 실패했다(52/54). 같은 시각 에디터 PIE 도구는 `walk: ground not loaded under walk start after 60s`·`ride: no result within 255s`로 실패했고, 촬영 지점 2곳은 폰이 시작 위치에 남았다.
- 원인: 두 프로세스가 CPU·디스크를 나눠 쓰면서 스트리밍·프레임 시간 의존 단언이 시간 한도를 넘는다(미검증 추정, 코드 경로에는 변경 없음).
- 해결: 헤드리스 시험과 PIE 측정은 차례로 돌린다. 단독 재실행에서 54/54와 PIE 통과를 확인했다. 동료 세션의 헤드리스 실행이 있는지 먼저 본다(L-build-05와 같은 점검).
- 범위: 같은 기계에서 UnrealEditor-Cmd 시험과 GUI 에디터 PIE 도구(`pie_*.py`)를 함께 쓸 때
- 증거: `Saved/AgentOps/20261016/sun/headless.log`(52/54, 동시) ↔ `headless_solo.log`(54/54), `sun/walk.log`(동시 실패) ↔ `walk_solo.log`
- 날짜·상태: 2026-10-16 active
- 발견: claude

### L-editor-21 `TDGame.SeamlessTravel.RealmConnectorWalk`는 GUI 에디터 PIE에서 남서 관문 준비가 3~5초라 실패한다(헤드리스는 0초 통과)
- 증상: `python Tools/check_automation_tests.py --filter TDGame.SeamlessTravel.RealmConnectorWalk` → `Gate 'SunkenStarfieldSouthwest' walk into the realm: destination became ready 6.68 s after entering the connector (<= 1.0)`. 같은 바이너리의 헤드리스(`-NullRHI`) 실행은 두 관문 모두 0.00초로 통과한다.
- 원인(2026-10-18 실측): 셀이 아니라 내비다. `IsDestinationReady`의 두 조건을 매 틱 따로 기록하니 셀 Activated·Loaded는 매번 t=0이고, 목적지 `ProjectPointToNavigation`만 남서 5.73초 동안 실패했다(대기 타일 10985). 시험이 폰을 걷기 시작점으로 수백 m 순간이동하면 768 m 로딩 원의 셀이 한꺼번에 로드되어 동적 내비 타일 약 1만 개가 쌓인다(북동: 대기 0 → 순간이동 뒤 4840). 엔진은 틱당 (작업자×2)개까지만 타일을 시작하고 시간 예산이 없어 처리량이 프레임 수에 비례한다(`RecastNavMeshGenerator.cpp:5464`, `:5890`). 폰에서 가까운 타일부터 지으므로 272 m 떨어진 남서 도착점은 그 뒤에 선다. GUI(약 15프레임)는 초당 약 230개라 걷기 시작 때 목적지 타일이 남고, 헤드리스는 프레임이 높아 유지·정착 4초 안에 목적지까지의 가까운 타일을 다 짓는다(대기열 전체는 남을 수 있다: 헤드리스 남서 내비 대기 5.55초, 북동 0초). 엔진에는 특정 위치 타일을 먼저 짓는 공개 수단이 없다(유일한 확장점은 `GetSeedLocations` 재정의 = 내비 데이터 클래스 교체).
- 해결: 시험이 걷기 전에 내비 빌드가 끝날 때까지 기다린다(`HasDirtyAreasQueued()`·`IsNavigationBuildInProgress()` 둘 다 false, 한도 180초). 셀 스트리밍 완료 대기와 같은 "걸어서 도착한 상태" 전제다. GUI 대기는 남서 32~37초·북동 12~14초, 그 뒤 두 관문 모두 0.00초. 감속은 경계 앞 25 m에서만 걸려 순간이동 직후에도 실제 플레이 영향은 없다. 프레임에 기대는 준비 단언이 GUI에서만 실패하면, 먼저 두 조건을 따로 재는 탐침으로 어느 쪽인지 가른다.
- 범위: 이계 연결 통로 PIE 시험, GUI 에디터, 월드 파티션 + 동적 내비(D-06)
- 증거: 2026-10-17 `Saved/AgentOps/20261017/async-load/baseline-ab/run-log.txt`(변경 전 대조), 2026-10-18 `Saved/AgentOps/20261017/sw-gate/findings.md`·`gui-probe-run1-2.log`(탐침)·`gui-final-run1~3.json`(수정 후 GUI 3/3)
- 날짜·상태: 2026-10-17 발견 / 2026-10-18 해결
- 발견: claude

### L-editor-22 같은 에디터 프로세스에서 에셋을 다시 저장한 직후 에셋 레지스트리 의존 조회는 옛 하드 의존을 돌려준다
- 증상: 하드→소프트 UPROPERTY 변경 뒤 파이썬 커맨드렛에서 DA 21개를 `save_loaded_asset`으로 저장하고 `scan_paths_synchronous(..., True)` 뒤 `get_dependencies`(하드만)를 다시 읽었더니 21개 모두 Niagara 하드 의존이 그대로 나왔다.
- 원인: 같은 프로세스의 에셋 레지스트리가 저장 전 로드에서 모은 의존을 유지한다(강제 재검색으로도 갱신되지 않음, 엔진 코드 경로는 미확인). 파일 자체는 바뀌었다: 저장본 이름 표에 `NiagaraSystem`·`/Script/Niagara`가 없고 소프트 경로 문자열만 남았다.
- 해결: 저장 결과는 새 프로세스에서 확인한다 — `Tools/editor_resave_assets.py --dry-run`을 다시 실행하면 하드 의존 0으로 나온다.
- 범위: 에디터 Python·커맨드렛으로 에셋을 저장한 뒤 같은 프로세스에서 의존·참조를 검증할 때
- 증거: 2026-10-17 `Saved/AgentOps/20261017/soft-damage/resave.json`(저장 프로세스, 하드 21) ↔ `resave-dryrun-after.json`(새 프로세스, 하드 0)
- 날짜·상태: 2026-10-17 active
- 발견: claude

### L-editor-23 GUI PIE 자동화 시험 중 뷰포트 클릭은 순간이동 걷기와 싸워 연결로 방향을 매 프레임 뒤집는다
- 증상: `TDGame.SeamlessTravel.RealmConnectorWalk` GUI 1회에서만 `Gate 'SunkenStarfieldSouthwest' walk back to the field: boundary crossings' to be 1, but it was 3`, `passage pacing engagements with default settings' to be 0, but it was 13`, 최대 폰 이동 223.6 cm(평소 62 cm). 로그에 `Realm: replacing unrequested preload ... (entry)`/`(field return)`가 프레임마다 번갈아 나온다.
- 원인: 그 회차 로그에만 클릭 이동 표시 `FX_Cursor_Success ... initialized`가 3번 있었다(사람 클릭 → `SimpleMoveToLocation`). Mover 순간이동 효과(`FTeleportEffect`)는 속도를 유지하므로 시험이 매 프레임 순간이동해도 Mover가 클릭 목적지 쪽으로 v·dt만큼 더 민다. 프레임 길이가 고르면 드러나지 않지만 0.2/0.025 s처럼 들쭉날쭉하면 영역 서브시스템의 측정 속도 부호가 뒤집혀(`UpdatePassageDirection`) 예비 로드가 교체되고, 새 예비 로드가 그 틱에 준비 전이라 감속이 걸린다.
- 해결: 시험이 폰을 직접 움직이는 걷기 시작(`BeginRealmWalkPhase`)에서 `DisableInput`·`SetIgnoreMoveInput(true)`·`StopMovement()`. 헤드리스 재현(귀환 중 `AddMovementInput` 안쪽 밀기 + 프레임 0.262/0.063 s 교대): 차단 전 감속 33회·경계 교차 3회, 차단 후 0회·1회. GUI 시험 로그에서 `FX_Cursor_Success`가 보이면 사람 입력 개입부터 의심한다.
- 범위: GUI 에디터 PIE에서 시험이 순간이동으로 폰을 움직이는 자동화 시험(Mover 폰)
- 증거: 2026-10-18 `Saved/AgentOps/20261017/sw-gate/gui-probe-run1.log`(클릭 3회·진동), `headless-osc-A-push-hitch.log`(재현), `headless-osc-A2-guard.log`(차단 후), `findings.md` 6절
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-24 헤드리스 `UnrealEditor-Cmd`도 Python 원격 실행 노드를 열어, 첫 노드를 고르던 `run_in_editor`·`ue_editor.py stop`이 헤드리스를 겨눌 수 있었다
- 증상: GUI 에디터(PID 27380)와 헤드리스 시험(`UnrealEditor-Cmd ... -NullRHI`, PID 30188)을 함께 띄우면 원격 실행 노드가 2개(`project_root=C:/Project/TDGame/` 같음)로 보인다. 옛 `_find_node`는 `nodes[0]`을 그대로 써서, 발견 순서에 따라 `quit_editor()`나 편집 스크립트가 헤드리스로 갈 수 있었다.
- 원인: 프로젝트 설정 `bRemoteExecution=True`는 헤드리스 에디터에도 적용된다. 노드 정보(user·machine·engine_version·engine_root·project_root·project_name)에는 PID가 없어(`PythonScriptRemoteExecution.cpp:262~270`) 정보만으로는 GUI와 헤드리스를 가를 수 없다.
- 해결: `Tools/run_in_editor.py`가 같은 `project_root` 노드마다 `os.getpid()`를 물어 `UnrealEditor.exe` PID(`ue_editor.editor_pids()`)와 같은 노드만 고른다. 확인: 함께 띄운 상태에서 3회 실행 모두 GUI PID, `stop`은 GUI만 닫고 헤드리스 생존, 헤드리스만 있으면 `NO REMOTE NODES`로 거부.
- 범위: 같은 기계에서 GUI 에디터와 헤드리스 시험·커맨드렛을 함께 쓰는 여러 세션 작업
- 증거: 2026-10-18 이 세션 실측(노드 목록·PID 출력), `Saved/AgentOps/20261017/sw-gate/headless-node-check.log`(헤드리스)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-25 레벨을 연 직후 `is_navigation_being_built`는 빌드 시작 전에도 False라 내비 대기를 그냥 통과한다
- 증상: 에디터를 다시 띄운 직후 `python Tools/Movement/pie_check_player_movement.py` → `path_ready=false`(60.7 s), 클릭 이동 0 cm, PIE 로그 `Unable to find RecastNavMesh instance`, `passed=false`. 이미 켜진 에디터에서는 통과한다.
- 원인: 도구가 에디터 월드의 `unreal.NavigationSystemV1.is_navigation_being_built(world)`가 한 번 False면 내비가 준비됐다고 봤다. 레벨을 막 연 직후에는 빌드가 아직 시작되지 않아도 False가 나와, 내비 데이터 없이 PIE가 시작됐다(그날 빌드는 약 82초).
- 해결: 준비 판정을 '에디터 월드에 RecastNavMesh 액터 존재 + `is_navigation_being_built_or_locked` False + 모든 PlayerStart와 이동 목표점이 `project_point_to_navigation`(extent 50,50,250)으로 투영'으로 바꾸고, 5초 간격 연속 2회를 요구한다. 300초 안에 안 되면 측정 없이 종료 코드 2. 재기동 직후 실행에서 첫 확인은 빌드 중·투영 실패였고 13.8초 뒤 측정해 통과했다.
- 범위: 에디터 Python으로 내비에 기대는 PIE 측정 도구(클릭 이동·경로 질의)
- 증거: 2026-10-18 `Saved/AgentOps/20261018/player-move/runtime.txt`(실패), `Saved/AgentOps/20261018/nav-wait/run-cold.log`·`run-warm.log`·`run-warm2.log`(통과)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-26 에디터 Python이 `Failed to find property`·AttributeError로 멈추면 C++에 그 UPROPERTY가 없거나 함수가 UFUNCTION이 아니다
- 증상: `Exception: TDFluidPresetAsset: Failed to find property 'override_preset_defaults' for attribute 'override_preset_defaults' on 'TDFluidPresetAsset'`(Tools/FluidFX/editor_make_fluid_test_level.py:280). 다음 줄 `resolve_effective_config()`도 C++ 함수가 UFUNCTION이 아니라 호출 불가였다. 재질 3개만 저장되고 작업 세션이 멈췄다
- 원인: 스크립트를 C++ 선언보다 먼저 써서 없는 속성·리플렉션 밖 함수를 불렀다. 언리얼 Python은 UPROPERTY·UFUNCTION만 노출하고 bool의 b 접두를 뗀다(bOverridePresetDefaults → override_preset_defaults)
- 해결: 스크립트 작성 전에 `Intermediate/PythonStub/unreal.py`를 rg하거나 헤더의 UPROPERTY·UFUNCTION을 확인한다. 새 UPROPERTY·UFUNCTION은 라이브 코딩이 안 되므로 빌드 후 에디터 재시작(L-build-01). 시험은 FindFProperty만 보지 말고 CPF_Edit·CPF_BlueprintVisible·FUNC_BlueprintPure 플래그까지 단언한다
- 범위: C++ 클래스·데이터 에셋을 만지는 모든 에디터 Python 도구
- 증거: 2026-10-18 `Saved/Logs/TDGame-backup-2026.10.17-17.41.25.log:2512`, 분석 `Saved/AgentOps/20261018/fluidfx/analysis/critic.md`
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-27 `-nullrhi` 헤드리스 커맨드릿에서 스켈레탈 메시를 FBX로 내보내면 `Assertion failed: MeshObject`로 죽는다(애니 FBX는 된다)
- 증상: `UnrealEditor-Cmd ... -run=pythonscript -nullrhi`에서 AnimSequence FBX 7개는 내보낸 뒤 `AssetExportTask(SkeletalMesh)`에서 `Assertion failed: MeshObject [SkinnedMeshComponent.cpp:4987]`(GetCPUSkinnedVertices ← FMeshMergeHelpers::RetrieveMesh) 치명 오류
- 원인: 메시 내보내기가 렌더 메시 객체로 CPU 스키닝 정점을 읽는데 NullRHI에서는 MeshObject가 만들어지지 않는다
- 해결: 애니만 필요하면 헤드리스로 내보내고(골격은 애니 FBX에 들어 있음), 메시가 필요하면 GUI 에디터(`run_in_editor.py`)에서 내보낸다. 헤드리스 스크립트는 메시 내보내기를 마지막에 두거나 빼서 앞 산출물을 잃지 않게 한다
- 범위: UE 5.8 에디터 커맨드릿 FBX 내보내기
- 증거: 2026-10-18 `Saved/Logs/M327_ExportHitRefs.log`(애니 7개 성공 뒤 하이에나 메시에서 중단)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-28 스크립트로 새로 만든 맵을 열면 에디터 내비메시가 생성되지 않아 `project_point_to_navigation`이 몇 분이고 None이다
- 증상: `python Tools/FluidFX/pie_check_fluid.py` → `TD_NAV_STATE {"navmesh_actors": 1, "building_or_locked": false, "player_start_found": true, "projected": [false]}`가 300초 내내 반복. 바닥 충돌(QueryAndPhysics·CanEverAffectNavigation)·NavMeshBoundsVolume 범위·PlayerStart 높이는 정상이고 질의 범위를 500×500×2000으로 키워도 None
- 원인: RecastNavMesh(DYNAMIC)와 경계 볼륨은 있지만 에디터가 이 맵의 내비 타일 빌드를 시작하지 않았다(빌드 중·잠금도 아님). L-editor-25(빌드 시작 전 False)와 달리 기다려도 시작되지 않는다
- 해결: 내비가 준비되지 않았고 빌드 중도 아니면 에디터 월드에 `unreal.SystemLibrary.execute_console_command(world, 'RebuildNavigation')`을 한 번 보낸다. 8초 안에 투영 성공(`pie_check_fluid.py` `request_editor_navigation_rebuild`). 맵이 더러워지므로 저장하지 않을 거면 닫을 때 버린다
- 함께 본 것: `UnrealEditorSubsystem.get_editor_world()`는 PIE 중 None을 돌려주고 `LogUtils: Error: The Editor is currently in a play mode.`를 남긴다. PIE 상태 보고 함수는 `get_game_world()`를 먼저 보고 PIE가 아닐 때만 에디터 월드를 읽는다
- 범위: 에디터 Python으로 새 맵을 만든 직후 PIE·내비 질의를 하는 도구
- 증거: 2026-10-18 `Saved/AgentOps/20261018/fluidfx/pie-run1.log`(실패), RebuildNavigation 뒤 투영 `(-450, 0, 10)` 성공
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-29 `-NullRHI` 헤드리스 시험에서 `UNiagaraFunctionLibrary::SpawnSystemAttached`는 null을 돌려주고 나이아가라 `IsActive`는 늘 거짓이다
- 증상: FluidFX 프리셋 나이아가라 스폰 시험이 헤드리스(`build_and_test.py`)에서 소유 컴포넌트 null로 실패. 직접 만든 컴포넌트도 `Activate(true)` 뒤 `IsActive()` 거짓
- 원인: `NiagaraFunctionLibrary.cpp:71`이 `FApp::CanEverRender()`가 거짓이면 스폰을 건너뛴다. 활성화도 같은 조건에서 일찍 반환한다
- 해결: 컴포넌트가 꼭 있어야 하면 `NewObject<UNiagaraComponent>(Owner)`+`SetAsset`+`SetupAttachment`+`RegisterComponent`로 만든다. 활성 토글은 헤드리스에서 판별하지 말고(단언 건너뜀 + AddInfo) PIE 도구로 검증한다(`Tools/FluidFX/pie_profile_fluid.py` 나이아가라 단계)
- 범위: 헤드리스 자동화 시험에서 나이아가라 컴포넌트를 만들거나 활성 상태를 보는 코드
- 증거: 2026-10-18 `Saved/AgentOps/20261018/fluidfx-perf-cpp-red-tests.json`, PIE 통과 `Docs/Validation/FluidFX/D-48-pie-perf-2026-10-18.json`
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-30 `run_in_editor.py -c` 스니펫이 남긴 전역 변수(월드·패키지·액터 배열)가 옛 맵을 붙잡아, 다음 맵 전환이 `World Memory Leaks` 치명 오류로 에디터를 죽인다
- 증상: `unreal.EditorLoadingAndSavingUtils.new_blank_map(False)` 호출 중 원격 연결이 끊김(`ConnectionResetError 10054`), 로그 `Fatal error: [EditorServer.cpp] [Line: 1951] World Memory Leaks: 1 leaks objects and packages`, 참조 사슬 `GCObjectReferencer -> FPyReferenceCollector::AddReferencedObjects( Package /Game/Level/LV-Cambat)` ← `FPyWrapperArrayMetaData`
- 원인: 원격 실행의 `-c` 코드는 에디터 Python `__main__` 전역에서 돈다. 직전 스니펫의 `m = get_dirty_map_packages()`(패키지 배열)가 전역에 남아 GC 뿌리가 됐고, 맵을 바꾸며 옛 월드를 지우는 `CheckForWorldGCLeaks`가 치명 오류를 낸다. 월드·액터 변수(`w = get_game_world()`)나 `unreal` 모듈 속성에 둔 탐침 상태(생성기 지역 변수 포함)도 같은 위험이다
- 해결: 스니펫은 함수 안에서 돌리거나 끝에 `del`로 UObject 참조를 지운다. 탐침 상태를 `unreal` 모듈에 둘 때는 끝날 때 생성기·후크·월드 참조를 비운다. 맵 전환 전에는 저장 안 된 변경이 테스트용 임시 액터뿐인지 확인하고, 전환이 꼭 필요하면 에디터를 새로 띄운다(`python Tools/ue_editor.py ensure`)
- 범위: UE 5.8 PythonScriptPlugin 원격 실행 + 에디터 맵 전환(`new_blank_map`·`load_map`·`load_level`, 추정: PIE 종료 시 월드 정리도 같은 검사)
- 증거: `Saved/Logs/TDGame-backup-*.log`(2026-10-18 16:08 크래시, 3566행 근처), 이 세션 Worklog M3-14 완료 항목
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-31 PIE 디버그 선(DrawDebug*)을 스크린샷에 담으려면 `Shot`을 PIE 플레이어 컨트롤러로 보내야 한다(`HighResShot`엔 안 나오고, 컨트롤러 없이 보낸 `Shot`은 편집기 뷰포트를 찍는다)
- 증상: `TD.MonsterAI.Tactics.Debug 1`(DrawDebugCircle·Line)·`TD.MonsterAI.DebugDraw 1`(DrawDebugString)을 켠 PIE에서 `HighResShot 1920x1080 filename=…`은 게임 화면을 찍지만 디버그 선·글자가 없다(2026-09-30 `measurements/monster_pie_engage.png`도 같음). `execute_console_command(world, 'Shot')`(세 번째 인수 없음)은 PlayerStart·조명 아이콘이 보이는 레벨 편집기 뷰포트를 찍는다
- 해결: `unreal.SystemLibrary.execute_console_command(world, 'Shot', unreal.GameplayStatics.get_player_controller(world, 0))` → `Saved/Screenshots/WindowsEditor/ScreenShotNNNNN.png`(PIE 게임 뷰포트, 디버그 선 포함, 뷰포트 해상도). 새 파일을 찾아 옮긴다(`Tools/MonsterAI/pie_check_tactics.py` `capture_screenshot`)
- 범위: UE 5.8 GUI 에디터 PIE, 원격 Python 콘솔 명령. `HighResShot`이 디버그 선을 빼는 원인은 엔진 소스로 확정하지 못함(미검증)
- 증거: `Docs/MonsterAI_CombatSim/measurements/squad-tactics/pie-2026-10-18/01~05*.png`(선 있음), 같은 장면 HighResShot 1회차(선 없음, 덮어씀)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-32 Unreal Insights 명령줄 내보내기: 파일 트레이스는 에디터 메모리 꼬리를 앞에 붙이고, `ExportTimerStatistics`는 `-timers`를 무시한다
- 증상: PIE에서 `Trace.File <경로> cpu,frame` → 15초 → `Trace.Stop`으로 받은 .utrace에 스텝 1,829회(약 28.6초)가 들어 있어, 전술 끔 구간 트레이스에도 직전 전술 켬 구간의 `TDMonsterAI_Tactics` 618회가 섞였다. 마지막 게임 프레임의 끝이 무한대라 프레임 평균이 `Infinity`
- 원인: 에디터는 명령줄 없이도 기본 채널을 메모리에 기록하고, 파일 연결 때 그 꼬리를 먼저 보낸다(`TraceAuxiliary.cpp`). `TimingInsights.ExportTimerStatistics`는 `-timers=`·`-columns=`를 무시해 전체 타이머를 낸다(`TimingExporter.cpp:985`)
- 해결: `UnrealInsights.exe -OpenTraceFile=<utrace> -AutoQuit -NoUI -unattended -nullrhi -ExecOnAnalysisCompleteCmd=@=<rsp>`(rsp 한 줄 한 명령), 구간 값은 `TimingInsights.ExportTimingEvents <csv> -columns=ThreadName,TimerName,StartTime,EndTime,Duration -threads=GameThread,Game?Frames -timers=<접두사>*`로 받아 **마지막 프레임 끝에서 구간 실제 초만큼만** 잘라 계산하고, 끝이 무한인 행은 버린다(`Tools/MonsterAI/pie_check_tactics.py` `summarize_timing_events`). 이미 트레이스 중이면 `Trace.File`이 거부되므로 먼저 `Trace.Stop`
- 범위: UE 5.8 에디터 PIE CPU 트레이스·UnrealInsights 명령줄
- 증거: `Saved/MonsterAI/TacticsPie/crowd_tactics_*_events.csv`, `measurements/squad-tactics/pie-2026-10-18/report.json` `crowd_cost`
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-33 C++ PIE 자동화 시험의 `FScreenshotRequest::RequestScreenshot`이 절반쯤 레벨 편집기 뷰포트를 찍는다 → 요청 직후 PIE `UGameViewportClient::ProcessScreenShots`를 직접 부른다
- 증상: `TDGame.PIE.Combat.BossLoop` 1회차 스크린샷 8장 가운데 4장(02·04·05·06)이 PIE 게임 창(2888×1656, UI 포함)이 아니라 조명·PlayerStart 아이콘이 보이는 편집기 뷰포트(2113×957)였다. M3-27 FullLoop의 "스크린샷 01 편집기 뷰포트 흔들림"도 같은 현상
- 원인: 스크린샷 요청은 전역 플래그라 다음에 그려지는 뷰포트가 가져간다. 실시간 갱신 중인 레벨 편집기 뷰포트(`FEditorViewportClient::ProcessScreenShots`)가 PIE 게임 뷰포트보다 먼저 그리면 그쪽이 찍는다
- 해결: `RequestScreenshot(Path, true, …)` 바로 뒤에 `GetPieWorld()->GetGameViewport()->ProcessScreenShots(GameViewport->Viewport)`를 호출해 같은 틱에 게임 창(UI 포함)으로 처리한다(`Source/TDGameEditor/Tests/TDBossLoopPieTests.cpp` `RequestBossLoopPieScreenshot`). 이후 2회 실행 8/8 모두 게임 창
- 범위: UE 5.8 GUI 에디터 PIE(새 창 실행), C++ 자동화 시험. 디버그 선이 필요한 파이썬 탐침은 L-editor-31
- 증거: `Saved/AgentOps/20261018/boss-loop/pie/`(1회차 크기 1.26MB 4장 → 수정 뒤 전부 2888×1656), `Docs/MonsterAI_CombatSim/measurements/M3-36/`
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-34 새 예고 재질을 만든 같은 에디터 세션에서는 몬스터 공격 예고가 디버그 와이어로 나온다
- 증상: `editor_make_boss_telegraphs.py`로 `MI_TD_Telegraph_Safe` 등 18개를 만든 직후 같은 에디터에서 PIE를 돌리자 용 업화의 안전 지대가 청록 원판이 아니라 디버그 원기둥 와이어로 그려졌다(부채꼴·띠 예고도 같은 조건)
- 원인: `TDMonsterAttackCatalog::CreateAttacks`는 라이브러리 초기화 때 한 번 돌고, `ApplyTelegraph`가 그 시점에 `FPackageName::DoesPackageExist`로 재질 패키지를 검사해 없으면 `bDrawDebug = true`로 굳힌다. 카탈로그 정의는 런타임 임시 객체라 에셋이 생겨도 다시 만들지 않는다
- 해결: 예고 에셋을 새로 만들거나 경로를 바꾼 뒤에는 `python Tools/ue_editor.py restart`(또는 에디터 재시작) 후 PIE. 재시작 뒤 같은 탐침에서 원판·부채꼴·띠가 재질로 나옴
- 범위: C++ 카탈로그 정의(용·중간 보스·기본 몬스터 공격)의 예고 재질 전부. `.uasset` 주문은 해당 없음
- 증거: `Docs/MonsterAI_CombatSim/measurements/M3-35/pie-20261018/6_inferno.png`(재시작 뒤), 재시작 전 실행은 같은 폴더 덮어씀(Worklog M3-35 완료 항목)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-35 코드로 만든 지오메트리 컬렉션은 충돌 형상이 비어 깨지지 않고 통째로 움직인다
- 증상: `UTDFluidFractureEditorLibrary::CreateBrickGeometryCollection`(AppendStaticMesh 64개 + 루트)으로 만든 벽돌 블록이 PIE에서 `CrumbleActiveClusters()`를 불러도 `IsRootBroken()`이 계속 false, 조각 65개가 상대 위치를 유지한 채 한 몸으로 이동. 트리거 직후 `Ensure condition failed: Geometry != nullptr [PhysScene_Chaos.cpp:2396]`
- 원인: 새 UGeometryCollection의 기본 충돌 형상은 Convex인데 AppendStaticMesh는 볼록 껍질 데이터를 만들지 않아 잎·루트 Implicits가 전부 nullptr. 프록시가 형상 없는 잎을 입자로 만들지 않아 루트가 자식 없는 단일 강체가 되고, 부술 클러스터가 없다(엔진 GeometryCollectionPhysicsProxy.cpp:5239-5254·3207·1607)
- 해결: AddSingleRootNodeIfRequired 뒤 `SizeSpecificData[*].CollisionShapes[*].ImplicitType = Chaos_Implicit_Box`(벽돌) 또는 `UpdateConvexGeometry()`, 그다음 `EnsureDataIsCooked(false, false, false, false)`(PreSave와 같은 인수. bInitResources=true는 RebuildRenderData 뒤 `Assertion failed: !bIsInitialized` 크래시). 시험은 `FGeometryCollectionPhysicsProxy::NeedToInitializeSharedCollisionStructures`(속성 존재만 봄, 거짓 양성) 대신 `FGeometryDynamicCollection::ImplicitsAttribute` 값의 유효 개수를 센다(빨강 0/65 → 초록 65/65)
- 범위: UE 5.8, C++·파이썬으로 생성한 GC 전부. 프랙처 편집기로 만든 에셋은 해당 없음
- 증거: `Saved/AgentOps/20261018/fluidfx/gc-crumble-debug.md`, `Saved/AgentOps/20261018/fx3-gc-red-tests.json`·`fx3-gc-green-tests.json`, 탐침 `Docs/Validation/FluidFX/D-49-fx3-probe-2026-10-18.json`(퍼짐 1.0배 → 11.3배)
- 날짜·상태: 2026-10-18 active
- 발견: claude(td-debugger 2회)

### L-editor-36 반투명 재질의 속도 출력과 SceneDepth는 함께 못 쓰고, set_material_usage 반환값은 성공 여부가 아니다
- 증상: ① `editor_make_fluid_flame_material.py` 컴파일 실패 `(Node SceneDepth) Translucenct material with 'Output Velocity' enabled will write to depth buffer, therefore cannot read from depth buffer at the same time.` ② `MaterialEditingLibrary.set_material_usage(m, MATUSAGE_INSTANCED_STATIC_MESHES)`가 False를 돌려줘 스크립트가 실패로 판정했지만 `has_material_usage`는 True
- 해결: ① 깊이 페이드(SceneDepth)가 필요한 불꽃 카드는 `output_translucent_velocity=False` + `enable_responsive_aa=True`(움직이는 반투명 번짐 완화). ② 사용 플래그는 `has_material_usage`로만 판정
- 범위: UE 5.8 반투명·가산 재질, 파이썬 MaterialEditingLibrary
- 증거: `Saved/AgentOps/20261018/fluidfx/flamemat-run1.log`·`flamemat-run2.log`(실패), `flamemat-run3.log`(성공)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-editor-37 파이썬으로 물리 에셋 바디를 읽으면 `skeletal_body_setups`가 비고, 자기 충돌 응답 속성 이름은 `collision_reponse`(엔진 오타)다
- 증상: `physics_asset.get_editor_property("skeletal_body_setups")`가 실패해 모든 물리 에셋이 "bodies 0", `unreal.SkeletalBodySetup`은 `module 'unreal' has no attribute`, `get_editor_property("collision_response")`는 `Failed to find property`. 관절 `get_disable_collision`도 없음
- 해결: 바디는 `[o for o in unreal.ObjectIterator(unreal.BodySetup) if o.get_outer() == physics_asset]`(형식은 BodySetup으로 보임), 자기 충돌은 `collision_reponse`, 관절은 `physics_asset.get_constraints(False)` + `unreal.ConstraintInstanceBlueprintLibrary`(`get_angular_limits`·`get_disable_collsion` 오타, 반환 튜플 첫 원소는 접근자). 주의: `collision_reponse=BODY_COLLISION_DISABLED`는 같은 에셋끼리만이 아니라 바닥까지 충돌을 꺼 래그돌이 떨어진다. 바디 쌍만 끄는 표(CollisionDisableTable)는 파이썬 비노출
- 범위: UE 5.8 PythonScriptPlugin, PhysicsAsset·BodySetup·ConstraintInstance 읽기·메모리 재정의(`Tools/MonsterAI/editor_audit_ragdoll_physics.py`)
- 증거: 2026-10-19 `Saved/AgentOps/20261018/ragdoll-jitter/physics-audit.json`, 무효 실험 `Docs/MonsterAI_CombatSim/measurements/M3-38/exp5-selfcol-off-assist/report.json`(RMS 650~999cm = 낙하)
- 날짜·상태: 2026-10-19 active
- 발견: claude

### L-editor-38 `check_automation_tests.py`가 헤드리스 인스턴스에 붙으면 GUI 전용 PIE 시험이 0초 '통과'로 건너뛰어진다
- 증상: `python Tools/check_automation_tests.py --filter TDGame.PIE.Combat` 결과가 `SUMMARY passed=3 failed=0`인데 시험별 duration이 0.0~0.1초이고 경고에 `... needs a rendering editor and was skipped`. GUI 에디터 로그(`Saved/Logs/TDGame.log`)에는 그 시험 흔적이 없다
- 원인(추정, 정황 근거): 다른 세션의 헤드리스 `UnrealEditor-Cmd`(NullRHI)가 MCP 서버 포트 8000을 먼저 잡아, 도구가 GUI가 아닌 그 인스턴스에서 시험을 돌렸다(`FApp::CanEverRender()` 거짓). `ue_editor.py ensure`는 포트가 열려 있는지만 보고 주인은 확인하지 않는다
- 해결: 시험 직전 `netstat -ano`의 `:8000 ... LISTENING` 주인 PID가 `tasklist`의 GUI `UnrealEditor.exe` PID와 같을 때까지 기다린 뒤 실행하고, 결과 JSON의 duration이 1초를 넘는지 확인한다(스크립트 예: `Saved/AgentOps/...` 대신 세션 스크래치의 hitfeel_final.sh 패턴)
- 범위: GUI 전용 PIE 자동화 시험(TDGame.PIE.*), 여러 세션이 에디터를 번갈아 쓰는 환경
- 증거: 2026-10-18 22:32 실행 3건 0초 통과(`Saved/AgentOps/automation_tests_20261018-2232.json`) → 주인 확인 추가 뒤 22:40 실행 BossLoop 66초·FullLoop 18초·HitFeel 265초(`automation_tests_20261018-2240.json`)
- 날짜·상태: 2026-10-19 원인 미검증(현상과 해결은 검증)
- 발견: claude

### L-editor-39 메모리 실험으로 만든 물리 에셋을 `EditorAssetLibrary.rename_asset`으로 옮기면 저장하지 않아도 디스크에 파일이 생기고, 측정 도구의 종 플래그 복원은 역순이어야 한다
- 증상: ① `SkeletalMeshEditorSubsystem.create_physics_asset` 뒤 `rename_asset`만 하고 `save_loaded_asset`을 부르지 않았는데 `Content/MonsterAI/Physics/PA_TD_*_Ragdoll.uasset`이 생김(`git status` 미추적) ② PIE 측정 뒤 `DA_TDMonster_Griffon` 등 4종의 `ragdoll_after_death_clip`이 표(False)와 달리 True로 남음 — 그대로 저장하면 사족 종이 사망 클립 뒤 래그돌로 굳는다 ③ 바디 재정의(`--body`)가 다음 실행에서 원래 값으로 안 돌아옴
- 원인: ① 이름 바꾸기는 새 패키지를 저장한다 ② 같은 종을 여러 회차 강제하면 두 번째 기록의 원래 값이 이미 True라 앞에서부터 복원하면 마지막에 True가 남는다 ③ 시료기 모듈을 다시 올릴 때 종 플래그만 되돌리고 바디 재정의는 되돌리지 않았다
- 해결: ① 미채택이면 그 파일을 지운다(`TD_RAGDOLL_DRY=1`은 종 연결만 저장 안 함) ② ③ `editor_ragdoll_jitter_sampler.py`가 역순으로 복원하고, `pie_measure_ragdoll_jitter.py`가 모듈을 다시 올릴 때 `restore_overrides()`도 부른다(M3-39). 종 에셋을 저장하기 전에 `species_table.json`의 `ragdoll_after_death`와 대조한다
- 범위: UE 5.8 PythonScriptPlugin, `Tools/MonsterAI` 래그돌 측정·물리 에셋 도구
- 증거: 2026-10-19 M3-39 세션(dirty 패키지 목록, 표 대조 출력)
- 날짜·상태: 2026-10-19 active
- 발견: claude

### L-editor-40 단일층 물(SLW)이 "안 나온다"는 판단은 아래를 보는 격자 탓이었다 — SLW 는 이 설정에서 정상
- 증상: SLW 재질(레거시·Substrate·상수만 쓴 최소 재질)을 수면 격자에 씌우면 PIE·HighResShot 에 아무것도 안 보이고 ProfileGPU 에 SingleLayerWater 구간이 없다. Substrate SLW BSDF 만 넣으면 `RayTracingMaterialHitShaders.usf:560 error: use of undeclared identifier 'GetSingleLayerWaterMaterialOutput0'` 로 재질 컴파일 실패(SingleLayerWaterMaterialOutput 노드를 함께 넣으면 컴파일됨)
- 원인: 격자 메시의 면이 아래를 향해 위에서 보는 카메라에서 전부 컬링됐다(L-editor-41). 그릴 SLW 프리미티브가 없으니 패스도 없었다. r.Substrate=True·ProjectGBufferFormat=0·r.RayTracing=True 조합은 원인이 아니다
- 해결: 위를 보는 메시(엔진 평면 또는 새로 만든 C++ 격자)에 씌우면 SLW 가 그려진다. SLW 흡수·산란 계수는 **1/센티미터**다(엔진이 cm 깊이에 그대로 곱함: `SingleLayerWaterShading.ush` 206·221행 `exp(-Depth * ExtinctionCoeff)`). 예전 "1/미터"는 SLW 가 안 그려지던 때의 오판이며, ×100 하면 10cm 에서 불투명해진다(lookdev `20261019-121023` 불투명 대 `121241` 맑음, 맑은 호수 (0.006, 0.0016, 0.0011)/cm). SLW 수면은 그림자를 드리우므로 수면 컴포넌트 그림자를 끈다(단독 효과 미검증). 반투명 경로(`TLM_SURFACE_PER_PIXEL_LIGHTING`, 두께 = SceneDepth − PixelDepth)는 대체 경로로 남긴다
- 범위: UE 5.8, 이 프로젝트 렌더 설정
- 증거: `Saved/FluidFX/lookdev/20261019-114427`(엔진 평면 SLW·새 C++ 격자 SLW 둘 다 표시, 예전 격자는 불투명도 미표시), 과거 오판 `Saved/AgentOps/20261018/fluidfx/fxw/slw-invisible-debug.md`·`Saved/FluidFX/lookdev/20261019-104953`
- 날짜·상태: 2026-10-19 active(원인 확정·정정)
- 발견: claude

### L-editor-41 파이썬·OBJ 로 만든 수면 격자가 반투명·SLW 패스에서 안 그려진다
- 증상: ① `StaticMesh.build_from_static_mesh_descriptions` 로 만든 메시는 렌더 정점 0(원본 메시 설명이 소스 모델에 저장되지 않아 저장·재로드 뒤 비어 있음). ② 파이썬 `StaticMeshDescription.create_polygon(group, instances)` 은 `takes at most 1 argument` — 정점 인스턴스 배열을 출력 인자로 바인딩해 쓸 수 없음(삼각형은 `create_triangle(group, [i0,i1,i2])`). ③ OBJ 가져오기로 만든 격자는 재질 슬롯 0개라 컴포넌트에 반투명 재질을 덮어써도 반투명·SLW 패스에서 그려지지 않음(불투명은 그려짐), 슬롯을 넣어도 축 변환으로 면이 아래를 향해 단면 재질은 안 보임. ④ C++ `CreateMeshDescription`+`CommitMeshDescription`+`Build` 로 만든 격자도 GUI 에디터에서는 불투명조차 안 보임(헤드리스 시험의 렌더 데이터는 정상)
- 원인(④, 2026-10-19 확정): 같은 이름 에셋이 이미 있으면 C++ 가 `FindObject` 로 그 객체를 제자리 재사용했는데, 저장된 결과의 메시 설명은 예전 OBJ 가져오기 것(꼭짓점 (-800,800)부터, 첫 삼각형 cross(P2−P0,P1−P0).z<0 = 아래를 봄)으로 남아 있었다. 같은 C++ 함수로 새 이름에 만든 격자는 반투명·불투명·기본·SLW 재질 모두 그려진다
- 해결: `UTDFluidWaterMeshEditorLibrary::CreateGridStaticMesh` 가 기존 객체를 임시 패키지로 옮기고(`Rename(…, GetTransientPackage(), REN_DontCreateRedirectors)`, Public·Standalone 해제) 새 객체를 만든다. 판별은 파이썬 `get_static_mesh_description(0)` 첫 삼각형 감기를 엔진 평면(+z)과 비교한다(`Tools/scratch` 실험, 2026-10-19). 메시 렌더 여부는 같은 재질을 씌운 엔진 평면을 나란히 놓고 비교한다
- 범위: UE 5.8 에디터 파이썬·OBJ 가져오기(Interchange)·C++ 메시 설명 생성
- 증거: `Saved/FluidFX/lookdev/20261019-110724`(슬롯 0 격자 미표시)·`20261019-111049`(양면에서 뒷면 표시)·`20261019-112253`(엔진 평면 표시·C++ 격자 미표시)·`20261019-114427`(새 C++ 격자 표시, 재사용된 예전 격자 미표시)
- 조사(td-debugger, `Saved/AgentOps/20261018/fluidfx/fxw/grid-mesh-render-debug.md`): 감기 순서·LOD 화면 크기·나나이트·섹션 재질은 배제. `-NullRHI` 헤드리스는 InitResources 를 건너뛰어(StaticMeshBuild.cpp:660) 시험이 그리기를 증명하지 못한다. 다음 판별: PIE 에서 `log LogStaticMesh Verbose` 뒤 메시 재지정 → "Skipping CreateSceneProxy" 이유 확인, 후보 수정은 FindObject 제자리 재사용 대신 새 객체, UVChannelData 갱신(엔진 CreateStaticMeshUtil.cpp:59·117)
- 날짜·상태: 2026-10-19 active(④ 원인 확정·수정 빌드와 재생성 시험 통과 `fxw-grid-regen2`)
- 발견: claude

### L-editor-42 머티리얼 파이썬 이름 함정: 정적 스위치 핀은 True/False, 데칼 블렌드는 DBM_D_BUFFER_*, Surface ForwardShading 은 TLM_SURFACE_PER_PIXEL_LIGHTING
- 증상: `connect_material_expressions(..., "A")` 가 MaterialExpressionStaticSwitchParameter 에 실패, `unreal.DecalBlendMode.DBM_DBUFFER_COLOR_NORMAL_ROUGHNESS`·`TranslucencyLightingMode.TLM_SURFACE_FORWARD_SHADING` 이름 없음, 재질 `decal_blend_mode` 속성 없음, 같은 마스터에 SingleLayerWaterMaterialOutput 이 남아 "only one Single Layer Water Material node"(표현식 일괄 삭제로 안 지워짐)
- 해결: 정적 스위치 입력 이름 "True"/"False", 열거형은 `dir(unreal.<Enum>)` 로 확인(`DBM_D_BUFFER_COLOR_NORMAL_ROUGHNESS`, `TLM_SURFACE_PER_PIXEL_LIGHTING`), 5.8 디퍼드 데칼은 decal_blend_mode 없이 블렌드 모드·연결 출력으로 결정, 마스터를 다시 지을 때는 에셋을 지우고 새로 만들되 그 에셋을 쓰는 맵이 열려 있으면 다른 맵을 먼저 연다(메모리 참조로 삭제 실패·반쯤 삭제된 객체는 에디터 재시작)
- 범위: UE 5.8 에디터 파이썬 MaterialEditingLibrary
- 증거: `Saved/AgentOps/20261018/fluidfx/fxw/watermat-run1~20.log`
- 날짜·상태: 2026-10-19 active
- 발견: claude

### L-editor-43 런타임에 만든 스플라인 메시가 물 재질 대신 어두운 바둑판(기본 재질)으로 그려진다
- 증상: W-1 강 리본(USplineMeshComponent, RF_Transient·BeginPlay 비동기 생성)이 PIE 에서 어두운 바둑판으로 보임. 솔버·MID 수치 탐침은 모두 통과해서 수치 검사로는 못 잡음
- 원인: 재질에 bUsedWithSplineMeshes 사용 플래그가 없으면 런타임 생성 스플라인 메시에는 엔진이 기본 재질로 대체한다(편집기에서 배치·저장할 때만 자동으로 플래그가 켜짐)
- 해결: 재질 생성 스크립트에서 `used_with_spline_meshes = True` 를 설정하고 다시 컴파일(`Tools/FluidFX/editor_make_fluid_water_material.py` create_water_master). 런타임에 만드는 다른 메시 종류(나이아가라 메시 입자, 인스턴스 메시 등)도 같은 방식으로 사용 플래그를 먼저 켠다. 렌더 결과는 PIE 화면으로만 확인된다
- 범위: UE 5.8 재질 사용 플래그, 런타임 생성 컴포넌트
- 증거: `Saved/FluidFX/lookdev/20261019-180759`(바둑판) → `20261019-180907`(물)
- 날짜·상태: 2026-10-19 active
- 발견: claude

### L-editor-44 언리얼 파이썬에서 bool 을 반환하고 출력 인자가 있는 함수는 튜플이 아니라 출력값만 돌려준다(실패면 None)
- 증상: `component.sample_fluid_at_world_location(위치)`(C++ `bool SampleFluidAtWorldLocation(const FVector&, FTDFluidCellSample& OutSample) const`)를 `(성공, 표본)` 튜플로 받은 탐침이 모든 표본을 '없음'으로 처리해 판정 값이 전부 null
- 원인: 파이썬 바인딩은 bool 반환을 성공 신호로 해석해 참이면 출력 인자만(여러 개면 튜플), 거짓이면 None 을 돌려준다
- 해결: `outcome is None` 이면 실패, 튜플이면 첫 원소 검사, 아니면 출력값으로 받는 양쪽 처리(`Tools/FluidFX/pie_probe_fluid_water.py` read_cell). 원격 실행 출력은 긴 줄이 여러 줄로 나뉘고 줄마다 `[Info] ` 머리말이 붙으므로 결과 JSON 은 머리말을 지우고 이어 붙인 뒤 `json.JSONDecoder().raw_decode` 로 읽는다. 파이썬에서 `unreal.NavigationSystemV1` 정적 함수를 부르면 엔진이 CDO GetWorld ensure(`NavigationSystem.h:293`)를 한 번 낸다 — 무해, 로그 검사에서 이 블록만 제외
- 범위: UE 5.8 PythonScriptPlugin, Tools/run_in_editor.py 원격 실행
- 증거: `Docs/Validation/FluidFX/D-49-fxw1-probe-2026-10-19.json`(수정 전 전부 missing → 수정 뒤 연속 2회 통과)
- 날짜·상태: 2026-10-19 active
- 발견: claude

### L-editor-45 PIE 중 `HighResShot`은 프레임 하나를 약 0.45초로 늘려 게임 시간이 그만큼 건너뛴다(시간을 지정한 스크린샷이 밀리거나 겹친다)
- 증상: `pie_measure_ragdoll_jitter.py --screenshot-times 0,0.1,0.2,...`에서 t=0 촬영 직후 월드 프레임이 446ms였다. 래그돌 시작이 게임 시간 0.42초 밀렸고, +0.1초 사진은 0.44~0.49초에 찍혔으며, +0.2초 사진은 +0.35초와 같은 프레임에 겹쳐 빠졌다.
- 원인: 고해상도 스크린샷의 읽기 되돌림이 그 프레임의 실시간을 늘린다. 다음 프레임 dt가 그만큼(최대 dt까지) 커지면서 게임 시간이 건너뛴다.
- 해결: 촬영 작업 동안만 `t.OverrideFPS 60`(UnrealEngine.cpp:520·3203, Shipping 외)으로 고정 시간 간격을 쓰고 끝나면 이전 값으로 되돌린다(`--screenshot-fixed-fps`, 기본 60). 사진은 요청 시각 + 1프레임에 찍힌다. 촬영 실행과 지표·성능 실행은 나눈다. 열린 에셋 편집기 뷰포트가 대신 찍히므로 측정 전에 닫는다(도구가 물리 에셋·스켈레톤·애니·블루프린트 편집기를 닫는다).
- 범위: GUI 에디터 PIE에서 시간을 지정해 촬영하는 측정 도구
- 증거: 2026-10-19 `Saved/AgentOps/20261019/death-ragdoll/measure/run-log.md`(끊김), `measure2/run-log.md`(고정 간격 뒤 60/60장 +16.7ms)
- 날짜·상태: 2026-10-19 active
- 발견: claude
### L-editor-46 볼륨·디더 재질이 스크린샷에서 2×2 체커·사선 점묘로 찍히고, settle 기반 PIE 촬영 시각이 1.3~2.2초씩 늦다
- 증상: SOFT 볼륨 연기 가장자리에 주기 2열·약 3.2행의 사선 점묘(IGN 무늬)가 `HighResShot` 사진마다 그대로 남는다. 또 `pie_lookdev_fluid.py`에서 trigger 뒤 settle 0.3초로 찍은 '0.3초' 폭발 사진이 실제로는 약 1초 이상 지난 장면이었다(파일 시각 차 − settle = 장마다 고정 지연 1.33~2.25초).
- 원인: ① `HighResShot`은 가짜 뷰포트로 `r.HighResScreenshotDelay`(기본 4) 프레임을 Draw만 하고 월드를 틱하지 않는다(UnrealClient.cpp:1684-1717). 그래서 게임 시간(`ElapsedSeconds` = View.GameTime)에서 디더 위상을 뽑는 재질은 네 프레임 모두 같은 위상이 되고, TSR(시간 누적 업스케일러)이 평균할 재료가 없다. 일시정지·시간 감속에서도 디더가 멈춘다. ② 원격 Python 호출마다 왕복 지연이 있어 '적용 → sleep(settle) → 촬영' 순서는 게임 시간과 어긋난다.
- 해결: ① 디더 프레임은 `View.StateFrameIndexMod8`(재질 번역기도 쓰는 값)에서 뽑는다(`TDFluidVolumeRaymarch.hlsl`). ② 시각이 중요한 촬영은 한 번의 원격 호출 안에서 틱 콜백으로 게임 시간 기준 예약한다(`pie_lookdev_fluid.py` `schedule` 키, `t.OverrideFPS 60` 고정은 L-editor-45). 결과의 `shot_game_time`으로 라벨을 붙인다.
- 범위: UE 5.8 GUI 에디터 PIE 스크린샷, 시간·디더에 의존하는 재질과 측정 도구
- 증거: `Saved/AgentOps/20261020/fx8c/round2/smoke-light.md` 3절(스펙트럼 정점 IGN 일치, 엔진 코드), `round2/completeness.md` 1.1절(촬영 지연), `round3/lookdev-tool-verify.md`(예약 16장 모두 요청 시각 +1/60초)
- 날짜·상태: 2026-10-20 active
- 발견: claude(FX-8c 하위 에이전트 smoke-light·critic·completeness)
### L-editor-47 안개(또는 밝은 배경) 무대에서 반투명 불·연기의 색 지표(따뜻함·채도·중성 바닥·짙은 빨강·M1)가 배경색에 끌려 무효가 된다
- 증상: FX-8c 4차 폭발 64장 중 중성 바닥 G 60장·따뜻함 48장·채도 42장 불합격. 같은 셰이더에서 안개를 넣기 전보다 연기가 청회색 쪽으로 측정됐다.
- 원인: 반투명 연기·불꽃 카드 너머 청회색 높이 안개(G≈37)가 합성되어 색 평균을 끈다. LIVE-2 목표 장면은 붉은 환경이라 같은 지표를 그대로 비교할 수 없다.
- 해결: 색 지표는 같은 카메라의 안개 숨김 판(`pie_lookdev_fluid.py` `hide {"TDGen_FireStageFog": true}` 등)과 짝으로 재고, 휘도·대비는 안개 무대에서 잰다. 연기 마스크는 불·연기를 숨긴 깨끗한 판으로 만든다.
- 범위: FluidFX 룩데브 측정(`measure_fx_look.py --targets`)
- 증거: `Saved/AgentOps/20261020/fx8c/round4/judge.md` 4절 N3
- 날짜·상태: 2026-10-20 active
- 발견: claude(FX-8c judge)
### L-editor-48 비율 지표 관문(번짐/불 면적 등)은 기준을 정한 판의 분모 결함까지 함께 고정되어, 결함을 고치면 거짓 불합격이 난다
- 증상: FX-8c 굽기 확인에서 폭발 glow/flame 1.42~1.64가 기준 1.35(X10에서 정함)를 넘었다. 그런데 번짐 화소(분자)는 X10과 같았다(38~46k 대 41~47k).
- 원인: X10의 불 면적(분모)이 이미 기각한 결함(고리 도넛, 바닥 노란 먼지 언덕)으로 부풀어 있었다. 결함을 없애자 분모만 20% 줄었다. '되돌림 규칙'(원인 추정 값 복원)을 런타임으로 시험하니 오히려 2.5~2.8로 나빠졌다.
- 해결: 비율이 어긋나면 분자·분모·영역 띠(공중·바닥)를 따로 분해한다. 회귀 관문은 절대량(번짐 화소 ≤ 기준 판 최대)으로 두고, 목표 비율은 개선 과제의 수용 기준으로 분리한다. 되돌림 규칙은 그 전제를 측정으로 확인한 뒤에만 집행한다.
- 범위: FluidFX 룩데브 지표(`measure_fx_look.py --targets`)와 수치 관문
- 증거: `Saved/AgentOps/20261020/fx8c/round6/accept-judge.md` 0·2절, `scratch/accept-judge/glow_decompose.json`
- 날짜·상태: 2026-10-20 active
- 발견: claude(FX-8c accept-judge)
### L-editor-49 간격 틱(PrimaryActorTick.TickInterval)을 쓰는 액터의 DeltaSeconds 누적으로 '경과 시간'을 재면 틱 위상에 따라 최대 한 간격만큼 어긋난다
- 증상: 조각 발광 나이를 DeltaSeconds 합으로 재자, 무장 폴 간격 0.1초인 방출 액터에서 부서진 뒤 60프레임 나이가 1.033~1.067초가 되어 시험 하한(비율 0.677)을 0.626~0.652로 놓쳤다(부서지는 프레임 10·11·12 모사 모두 실패).
- 원인: UE 5.8 `TickTaskManager.cpp:2825-2846` CalculateDeltaTime은 bWasInterval이면 마지막 간격 틱 이후 시간을 돌려주고, bWasInterval은 1358-1368에서 참이 된 뒤 거짓으로 돌아오지 않는다. 그래서 간격을 끈 뒤 첫 DeltaSeconds에 이전 폴 이후 시간이 더해진다.
- 해결: 사건 이후 경과 시간은 사건 시각의 `GetWorld()->GetTimeSeconds()`를 저장하고 현재 월드 시간과의 차로 잰다(일시정지·전역 시간 배율 반영, 액터별 CustomTimeDilation만 다름).
- 범위: 틱 간격을 바꾸는 액터·컴포넌트의 시간 누적
- 증거: `Saved/AgentOps/20261020/fx8c/round10/frag-refuter.md` 1절, `scratch/frag-fixer/tick_phase_sim_world_time.json`, 시험 `TDGame.FluidFX.Fragment.GlowMaterialAppliesOnBreakAndCools` 초록
- 날짜·상태: 2026-10-21 active
- 발견: claude(FX-8c frag-refuter)
### L-editor-50 UE 5.8 나이아가라는 공식 NiagaraToolsets(MCP)로 편집할 수 있지만 저장·컴파일 요청 도구가 없고, NullRHI 헤드리스에서는 켜지지 않는다
- 증상: "나이아가라는 UFUNCTION이 없어 순정 에디터로는 에이전트가 편집 불가, 외부 브릿지 필요"라는 외부 문서 주장. 실제로 Python에는 시스템·이미터 편집 함수가 없다(NiagaraSystem.h·NiagaraEmitter.h UFUNCTION 0개).
- 사실: 엔진 `Plugins/Experimental/Toolsets/NiagaraToolsets`가 AllToolsets(TDGame.uproject 45행)로 켜져 MCP `list_toolsets`에 NiagaraToolset_System·Component·Blueprint·Info·Assets가 보인다(2026-10-21 `python Tools/uemcp.py call list_toolsets '{}'`). System 툴셋에 CreateNiagaraSystem·AddEmitter·AddModule·AddRenderer·SetStackInputData(HLSL 식 포함)·AddUserVariables·GetSystemCompileState·GetStackIssues가 있고 저장·RequestCompile은 없다.
- 해결: 편집 → 저장(run_in_editor Python, 저장 시 PreSave가 컴파일 요청) → GetSystemCompileState 순서. 편집 직후 상태 조회는 컴파일 대기로 120초 시간 초과가 날 수 있다(NiagaraToolset_System.cpp:485-517). 파티클 수치는 오프스크린·PIE에서 Sim Cache(CaptureNiagaraSimCacheImmediate·Read*Attribute, BlueprintCallable)로 읽는다. -NullRHI에서는 Activate가 즉시 반환한다(NiagaraComponent.cpp:1324-1333).
- 범위: UE 5.8 나이아가라 저작·시험 자동화
- 증거: `Saved/AgentOps/20261021/niagara-agent/authoring-api.md`·`testability.md`·`external-tools.md`·`refute.md`
- 날짜·상태: 2026-10-21 active(Sim Cache 수치 시험은 이 프로젝트에서 미실행)
- 발견: claude(나이아가라 문의 검증 워크플로)
### L-editor-51 나이아가라 GPU 이미터에서 "넓은 영역 균일 위치 + 밀도로 1회 거절" 스폰은 기대 생존 수가 스폰율 × 영역 평균 밀도라 거의 0이 될 수 있다
- 증상: N0 불티 NS 오프스크린 시험에서 시스템·이미터는 Active이고 SpawnRate SpawnOutputInfo는 2초에 19개인데 Sim Cache의 Ember 입자 수가 모든 프레임 0(시드 같아 매번 0, 다른 실행 한 번만 1개).
- 원인: 900cm 시뮬 창 전체에서 UV를 고르게 뽑고 불 선택 밀도(B 채널)로 한 번만 거절. 반지름 90cm 도장의 창 평균 b ≈ 0.0085~0.025라 초당 10개면 2초 생존 기대 0.1~0.5개. 거절된 입자는 Lifetime 0이라 같은 프레임에 죽는다. C++ ISM 불티는 같은 거절을 8회 재시도해 약 8배 남는다(TDFluidEmberComponent.cpp:13,183).
- 해결: 시험은 User.EmberSpawnRate를 1000으로 넣어 확인(도장 15개·없음 0개). 제품 전환 때는 스폰율을 밀도 면적에 비례시키거나 중요도 표본(SampleTexture 중요도 UV)으로 바꾼다. 기대 수 = 스폰율 × 수명 × 창 평균 밀도로 먼저 계산한다.
- 범위: 유체 RT를 표본해 거절하는 모든 나이아가라 스폰
- 증거: `Saved/AgentOps/20261021/round12/n0-debug.md`·`n0-debug-ns.md`, 시험 로그 `Saved/AgentOps/20261021/fx8c12-green2-rhi-tests.json`
- 날짜·상태: 2026-10-21 active
- 발견: claude(N0 입자 0 디버그 워크플로)

### L-editor-52 에디터 Python으로 머티리얼 DynamicParameter 노드 출력을 연결할 때 출력 이름은 매개변수 이름이 아니라 "R"·"G"·"B"·"A"다
- 증상: `MaterialEditingLibrary.connect_material_expressions(dynamic_node, "EmberHeat", custom_node, ...)`가 'MaterialExpressionDynamicParameter_0[EmberHeat]' 연결 실패로 끝나 M_TD_FluidEmberNiagara 생성이 중단됐다.
- 원인: DynamicParameter의 Outputs[]는 이름이 빈 문자열이고 R/G/B/A 마스크로만 구분된다. 연결 함수는 출력 이름을 마스크 문자로만 맞춘다.
- 해결: 출력 이름에 `"RGBA"[index]`를 넘긴다(`Tools/FluidFX/editor_make_fluid_flame_material.py` only ember_niagara 경로). param_names는 표시용으로만 설정.
- 범위: UE 5.8 에디터 Python 머티리얼 그래프 생성
- 증거: `Saved/AgentOps/20261021/round12/n0-author.md` 4절, 수정 뒤 연결 10/10·저장
- 날짜·상태: 2026-10-21 active
- 발견: claude(N0 저작 하위 에이전트)

### L-editor-53 `FRHIGPUTextureReadback`는 첫 복사 때 원본 크기로 스테이징을 만들고 이후 크기를 비교하지 않아, 원본이 커지면 D3D12 명령 목록 Close가 E_INVALIDARG로 죽는다
- 증상: PIE에서 GPU 유체 액터 수가 늘면(해상도·백엔드 재구성, 스냅숏 복원) 1~3프레임 안에 `D3D12CommandList.cpp:244 ... with error E_INVALIDARG` 치명 오류, 흔적 `TDFluidGpuSolverFrame`.
- 원인: 리드백 링 슬롯이 같은 `FRHIGPUTextureReadback`을 재사용하는데 엔진은 스테이징이 있으면 크기를 보지 않고 원본 전체를 복사한다(엔진 RHIGPUReadback.cpp:118-132). 아틀라스 폭 = 64×GPU 액터 수라 1개(64x64) 뒤 4개(256x64)면 범위 밖 복사가 기록된다.
- 해결: `FTDFluidGpuReadbackRing::Enqueue`에서 슬롯 Layout 아틀라스 크기가 이번 프레임과 다르면 리드백 객체를 새로 만든다(빈 슬롯만 교체라 경합 없음). 빨강 재현은 `TDGame.FluidFXRhi.ReadbackRingGrowsWithActorCount`·`LiveReconfigureKeepsReadbackValid`를 `--rhi offscreen`으로 필터 하나씩(먼저 도는 시험이 프로세스를 죽인다). `-RHIValidation`이면 "can't perform scaling operations"로 원인이 바로 보인다(미실측).
- 범위: 크기가 바뀔 수 있는 원본을 재사용 `FRHIGPUTextureReadback`/`FRHIGPUBufferReadback`으로 읽는 모든 코드
- 증거: `Saved/AgentOps/20261022/fx7p/crash-code.md`·`crash-spec.md`, 빨강 `Saved/AgentOps/20261022/fx7p-crash-red-A-tests.log`·`-red-B-tests.log`, 초록 RHI 31/31
- 날짜·상태: 2026-10-22 active
- 발견: claude(FX-7p 측정 중 크래시 2회 → 디버그 워크플로)

### L-editor-54 원격 실행(remote_execution) 출력은 print 한 번이라도 긴 인수(약 30KB)가 별도 출력 항목으로 쪼개져 표식과 JSON이 다른 줄에 온다
- 증상: `pie_profile_fluid.py --csv-gpu` S10(유체 액터 49개)에서 `configure 실패: 결과 표식 뒤 JSON을 읽지 못했다: TD_FLUID_PERF_RESULT ` 로 실행 종료(exit 2). 에디터 로그에도 표식 줄과 JSON 줄이 따로 찍힌다. 액터 16개(약 8KB)까지는 한 줄이었다.
- 원인: `print(MARKER, big_json)`의 인수별 쓰기가 에디터 stdout 리디렉터에서 따로 플러시되어 `run_command` 결과 output 항목이 `[Info] MARKER ` 와 `[Info] {json}` 둘로 나뉜다. `run_in_editor.run_code`는 항목마다 `[type] ` 접두어를 붙인다.
- 해결: 표식 뒤 텍스트와 이후 항목(접두어 `^\[[A-Za-z]+\] ` 제거)을 이어 `json.JSONDecoder().raw_decode`로 첫 JSON 값만 읽는다(`Tools/FluidFX/pie_profile_fluid.py` run_editor_action). 실제 에디터에 36KB 출력 시험으로 재현·확인.
- 범위: 원격 실행으로 큰 결과를 표식+JSON 한 줄로 돌려받는 모든 도구(같은 패턴의 다른 Tools 스크립트도 큰 결과에서 같은 실패 가능). 두 번째 발생: 같은 사실이 L-editor-44 해결 줄(`pie_probe_fluid_water.py`)에 묻혀 있어 검색에 안 걸렸으므로 별도 항목으로 승격
- 증거: `Saved/AgentOps/20261022/fx7p/run2.log`·`run2b.log`, 에디터 로그 표식 뒤 빈 줄(2026-10-22 15:01:06)
- 날짜·상태: 2026-10-22 active
- 발견: claude(FX-7p 2회차 측정)

### L-editor-55 에디터 Python `component.get_editor_property("<구조체 프로퍼티>")`는 사본이 아니라 객체 메모리 참조라, 나중에 되돌릴 원래 값으로 저장하면 이후 변경을 따라가고 객체가 사라지면 쓰레기 값이 된다
- 증상: `pie_profile_fluid.py --csv-gpu` 의 reset 이 `reapplied_config` 를 보고했는데 모닥불이 GPU 로 남음(재현 R1, 2026-10-22 22:08). PIE 종료 뒤 같은 스냅숏을 읽으면 `PythonizeEnumEntry: Cannot pythonize '179' (int64) as 'TDFluidSolverBackend'`. 1회차 SmokeStage 장면들이 grid 0·525 같은 비프리셋 값으로 돈 오염도 같은 기전[추정].
- 원인: UObject 의 USTRUCT 프로퍼티를 get_editor_property 로 받은 래퍼는 소유 객체 메모리를 가리킨다. 스냅숏에 넣은 뒤 configure 가 같은 프로퍼티를 바꾸면 스냅숏도 바뀌고, PIE 가 끝나 컴포넌트가 해제되면 매달린 참조가 된다.
- 해결: 저장할 때 `.copy()`(StructBase, 독립 사본 확인: 사본 수정이 원본에 안 미침)로 떼어 둔다(`Tools/FluidFX/pie_profile_fluid.py` fx7p_snapshot). 받은 즉시 고쳐 apply 하는 패턴(같은 호출 안)은 문제없다.
- 범위: 에디터 Python 으로 구조체 값을 보관했다가 나중에 되돌리는 모든 도구. 오프라인 가짜 unreal 하네스는 값 의미론이라 이 결함을 못 잡는다
- 증거: `Saved/AgentOps/20261022/fx7p/crash_r1_timeline.txt`(reset 뒤 DESCRIBE GPU128), 프로브 `probe_snapshot.py`·`probe_copy2.py` 결과(세션 스크래치), `Saved/FluidFX/fx7p/fx7p-crashR1-20261022220446`
- 날짜·상태: 2026-10-22 active
- 발견: claude(FX-7p 크래시 ② R1 재현)

### L-editor-56 NarshaMCP 0.13.7 `ue_manage_niagara` 오프라인(바이너리 파서) 읽기는 구조 목록은 맞지만 사용자 매개변수·값·일부 플래그·모듈 단계 배치가 틀려, 생성 결과의 정답 판정에 쓰면 안 된다
- 증상: 정답이 알려진 `/Game/FluidFX/Niagara/NS_TD_FluidEmber`(생성 스크립트 상수 + 에디터 정규화 덤프 `Saved/FluidFX/niagara/NS_TD_FluidEmber.dump.json`)를 읽혔더니 `get_parameters` User 3개(실제 14개, 벡터·텍스처 누락)·값 전부 0(실제 EmberSpawnRate 10·VelocityReferenceCmPerSec 600), `requires_persistent_ids: false`(실제 true), `SampleTexture.Mip Level` 형식 NiagaraInt32·값 23040(덤프는 NiagaraFloat), `get_execution_flow` 모듈이 단계 "unknown" 한 칸에 중복 제거돼 나옴(SampleTexture 2개 → 1개, 순서·단계 소속 없음), `performance_audit`는 asset_path를 무시하고 227개 전체를 훑으며 요약 `gpu_simulation_systems: 0`(이 시스템은 GPU)
- 맞은 것: GPU 시뮬·고정 경계 ±600·결정론·메시 렌더러·재질·`validate`(재질 사용 플래그 대조 포함)·SetVariables 노드 9개 수
- 해결: 생성·편집 결과의 정답은 에디터 쪽 정규화 덤프(공식 NiagaraToolset 조회, `editor_make_fluid_ember_niagara.py --dump-only`)로 판정하고, NarshaMCP 오프라인 읽기는 에디터 없이 하는 1차 점검(렌더러·재질 플래그·GPU에 CPU 전용 DI·경계)과 목록 조사에만 쓴다
- 범위: NarshaMCP 0.13.7 나이아가라 오프라인 작업 전체(UE 5.8, niagara_custom_version 93). 판올림 뒤 같은 에셋으로 다시 대조
- 증거: 이 세션 호출 결과(2026-10-23), 대조 근거 `Docs/AgentCollaboration/research/narsha-adk.md` 4절
- 날짜·상태: 2026-10-23 active
- 발견: claude(NarshaADK 활용 검토)

### L-editor-57 UE 높이 안개 투과율은 밑 2(`exp2`)라, 자연지수로 계산하면 안개 불투명도가 약 2배로 과대 추정된다
- 증상: 안개 산란 분석 초안의 불투명도 표(밀도 0.02, 탑다운 1.6~4.2%, 수평 1km 85.5%)를 반박자 2명이 엔진 식으로 다시 계산해 0.8~2.4%, 60.9%를 얻음
- 원인: 밀도 접힘 `Density/1000 · 2^(−Falloff/1000·(카메라Z−안개높이))`(`Renderer/Private/FogRendering.cpp:397-406`), 선적분 `(1−2^(−F))/F`(F→0이면 ln2, `Shaders/Private/HeightFogCommon.ush:207-214`), 투과율 `exp2(−선적분)`(394행). 수평 광선의 자연 광학두께 = (ln2)²·밀도/1000·거리(cm) ≈ 0.48배
- 해결: 안개 불투명도·광학두께(FSSS W 등)는 위 엔진 식 그대로 계산한다. 볼류메트릭 포그가 켜진 맵은 가까운 구간을 볼류메트릭 결과가 대체하므로(`HeightFogPixelShader.usf:184`) 높이 안개 단독 값임을 표기
- 범위: UE 5.8 Exponential Height Fog 수치 추정 전체
- 증거: `Saved/AgentOps/20261023/ssfs/refute-numbers.md` R1, `refute-engine.md` 치명 2, 정정본 `Docs/FogScattering/01-ssfs-fsss-feasibility.md` 5절
- 날짜·상태: 2026-10-23 active
- 발견: claude(SSFS·FSSS 가능성 분석, 반박에서 적발)

### L-editor-58 `L_TDWorld_Main`은 1단계 월드 파티션 시험 맵이다 — 실제 필드(마을·지점 목록·이계 연속 지형)는 `LV_DarkFantasy_OpenWorld`. 맵을 잘못 열고 "10-16 이후 월드 회귀"로 오판했다
- 증상: 2026-10-23~24 `L_TDWorld_Main` PIE에서 F01·F02·F03·F06·F07·B01 모두 파란 평면 + 흰 점 무늬 격자(지점 도구 `ground_z` 0.0). 깊이 진단(`r.TDFogScattering.DebugView 2`)에서 평면은 100m 이상(그려진 기하 없음). 같은 지점 도구의 2026-10-16 촬영(`Saved/WorldGen/PieShots/sun205_p65/F01_village.png`)에는 지형·모닥불·물웅덩이가 있어 회귀로 판정했었다
- 원인(확인 2026-10-24): 맵이 다르다. `L_TDWorld_Main`(외부 액터 605개, 09-17 이후 변경 없음)은 P1 심리스 스트리밍 시험 맵이고, 점 무늬는 PIE 시작 때 `SurfaceSampler` 포인트 초과 오류를 내는 PCG 시험 볼륨 쪽으로 추정. P3 메인 지역(마을·`Tools/WorldGen/topdown_spots.json` 좌표·이계 연속 지형 `43bc2f97`)은 `/Game/Level/LV_DarkFantasy_OpenWorld`이고, 지점 도구는 에디터에 열린 레벨로 PIE를 시작하므로 10-16에는 이 맵이 열려 있었다. 시작 맵 설정은 `LV-World`(GQ-11 미정)
- 해결: 필드·이계 측정 전에 `LevelEditorSubsystem.load_level('/Game/Level/LV_DarkFantasy_OpenWorld')`로 연다 → 같은 지점 도구가 10-16과 같은 화면(`Saved/WorldGen/PieShots/wr_field_off/`). "회귀"로 판정하기 전에 대상 맵의 `git log -- <맵·외부 액터 경로>`와 대장 산출물 경로부터 본다
- 범위: 화면·안개 측정 전부. 이 오판으로 SF-4 v1 필드 값(`DistanceScale` 32)이 `L_TDWorld_Main` 안개(밀도 0.005·감쇠 0.03)를 흉내 낸 장면에서 정해져 실제 필드에서는 과했다(위 띠 손실 0.94) → 실측 재결정(`Docs/FogScattering/04-sf4-spec.md` 8절)
- 증거: `Docs/FogScattering/measurements/SF-4/world_main_f01_regression.png`(오판 당시), `Saved/WorldGen/PieShots/wr_field_off|on/`, `Docs/FogScattering/measurements/SF-4/real/`
- 날짜·상태: 2026-10-23 발견 / 2026-10-24 resolved(회귀 아님, 맵 오선택)
- 발견: claude(SF-4 안개 산란 측정)

### L-editor-59 `pie_lookdev_fluid.py`의 `pawn: [x, y]`는 현재 z를 유지한다 — 높이가 다른 지형(하얀 고원 2438cm·별밭 −3391cm)에서는 폰이 바닥 아래나 공중에 놓여 측정이 무효가 된다
- 증상: 2026-10-24 P01·S01·F01로 옮긴 뒤 폰 z가 세 곳 모두 629(시작 위치 높이). 고원 P01 화면이 고원면 아래 구름층을 비추고, 안개 산란 끔/켬 차이가 `DistanceScale`과 무관하게 나왔다
- 원인: `pie_showcase_fluid.teleport_pawn`은 평평한 FluidFX 무대용이라 `set_actor_location(x, y, 현재 z)`만 한다
- 해결: `pawn: [x, y, z]`(2026-10-24 추가). z = `pie_capture_spots.build_ground_lookup` 지면 + 150cm. 측정 뒤 폰 높이를 읽어 확인한다
- 범위: `Tools/FluidFX/pie_lookdev_fluid.py`를 FluidFX 무대 밖 지형에 쓸 때
- 날짜·상태: 2026-10-24 active
- 발견: claude(SF-4 실측 재결정)

### L-editor-60 CSV 프로파일러의 `CSV_SCOPED_TIMING_STAT(범주, 이름)` 열은 `범주/<스레드>/이름`(예 `TDFluid/GameThread/WaterGatherStep`)으로 기록된다 — `범주/이름`으로 찾으면 '열 없음'
- 증상: FX-4b 예산 PIE 판정에서 게임 스레드 물 타이머 6개가 모든 장면에서 "필수 열 없음"(`TDFluid/WaterRasterDirtyCells` 등). CSV 머리글에는 `TDFluid/GameThread/WaterRasterDirtyCells`가 있었다
- 원인: 타이밍 통계는 스레드 이름이 가운데 붙는다. 사용자 정의 값(`CSV_CUSTOM_STAT`, 예 `TDFluidGpu/WaterRippleSubsteps`)과 범주 통계(`TDFluid/StepCount`)는 스레드 이름이 없다. 명세 표(spec-v3 2.8)도 `범주/이름`으로 적혀 있었다
- 해결: 분석기·계획의 열 이름을 `범주/GameThread/이름`으로 쓴다(`Tools/FluidFX/fx4b_budget_judge.py`). 측정 전 시운전 1블록으로 CSV 머리글을 대조한다
- 범위: CSV 블록을 읽는 모든 측정 도구(pie_profile_fluid.py extra_columns 포함)
- 증거: `Saved/FluidFX/fx7p/fx4b-budget-judge-20261024120142/csv/WQ5_r1_1A*.csv` 머리글 168~174열
- 날짜·상태: 2026-10-25 active
- 발견: claude(FluidFX FX-4b 수용 측정)

### L-editor-61 WPO를 연결한 직후·재컴파일 전에 `set_material_usage`를 부르면 DefaultLit 재질에서 셰이더 맵 어서션으로 에디터가 죽는다
- 증상: `editor_make_fluid_water_material.py`가 Lite 마스터에서 크래시(`ConnectionResetError 10054`). `Assertion failed: ShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId) [MaterialShader.cpp Line 2567] Material shader map M_TD_FluidWaterLite ... is missing expected shader type TShadowDepthPSCombined. DIFF [bHasVertexPositionOffsetConnected]: Conservative Parameters=false vs Compilation Parameters=true` ← `UMaterial::SetMaterialUsage`
- 원인: 사용 플래그를 켜면 즉시 셰이더 작업을 제출하는데, 캐시된 식 데이터(WPO 미연결)와 실제 번역(WPO 연결)이 어긋난다. 번역이 먼저 실패하는 재질(단일층 물 출력 노드 없음)은 우연히 통과한다. 결정론적이라 재실행해도 같다
- 해결: 사용 플래그는 노드 생성 전에 재질 속성(`used_with_instanced_static_meshes` 등 assignments)으로 켜거나 `compile_material` 뒤에 켠다. `has_material_usage` 확인도 컴파일 뒤에만(L-editor-36)
- 범위: 에디터 Python으로 재질 그래프를 만들고 사용 플래그를 켜는 모든 스크립트
- 증거: `Saved/AgentOps/20261026/wave1-int/assets-water/`(editor-crash-excerpt.txt, repro-ism), 크래시 덤프 `Saved/Crashes/UECC-Windows-D2B1031C4F1310F8E4BE0F9DCADA108E_0000`
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX 파동 1 물 재질 재생성)

### L-editor-62 나이아가라 사용자 정의 HLSL 식 하나가 Sample Texture 출력 두 개를 읽으면 GPU 구조체 멤버 하나가 빠져 GPU 컴파일이 실패한다
- 증상: 생성한 NS_TD_FluidDrive*·NS_TD_FluidEmberHeat 6종이 오프스크린 시험에서 `the owned Niagara component never appeared`, 에디터 로그에 `There were issues for compilation of script "<시스템>/...` 와 `FParamMap0_User`에 없는 멤버(Uniform·Gravity·EmberDensitySpawnWeight) 오류. CPU 컴파일 상태는 정상
- 원인(정적 판독 + 결정론 모의 6/6 일치, 직접 실험 전): `TNiagaraHlslTranslator::FinalResolveNamespacedTokens`(NiagaraHlslTranslator.cpp:8687·8691·8693)가 `UniqueParameterMapEntriesAliasesIntact`와 `UniqueParameterMapEntries`를 따로 `AddUnique`하고 `ProcessCustomHlsl`(9161)은 두 배열을 같은 첨자로 짝짓는다. Sample Texture 출력은 원래 별칭이 모두 `Particles.Module.SampledColor`라 별칭 배열이 하나 짧아지고, 그 식에서 마지막으로 처음 나온 이름공간 변수가 등록되지 않는다. 토큰 순서만 바꾸면 다른 변수(로컬 계열 `Engine.Owner.SystemWorldToLocal`)가 조용히 0이 될 수 있다
- 해결: 표본 단계마다 출력을 고유 이름 입자 속성(예 `Particles.TDDriveSampleDensity`)으로 복사하는 Set 단계를 두고, 한 식에 표본 출력이 둘 이상 들어가지 않게 생성한다. 검사기에 같은 규칙(식마다 빠지는 변수 0)을 둔다
- 범위: Sample Texture·Sample Grid 출력을 사용자 정의 HLSL 식에서 둘 이상 읽는 모든 나이아가라 시스템(UE 5.8)
- 증거: `Saved/AgentOps/20261026/wave1-int/assets-fx8n/`(index.md 10절, simulate_custom_hlsl_alias_drop.py, simulate-alias-drop.stdout)
- 날짜·상태: 2026-10-26 active(표본 출력을 고유 속성으로 복사하는 생성기로 7종 재생성 → GPU 로그 문제 0·Niagara 오프스크린 12/12, `Saved/AgentOps/20261026/wave1-assets/niagara/`)
- 발견: claude(FluidFX FX-8n 에셋 생성)

### L-editor-63 나이아가라 `GetSystemCompileState`가 UpToDate·오류 없음이어도 GPU 계산 셰이더는 따로 실패할 수 있다 — GPU 판정은 에디터 로그로 한다
- 증상: MCP NiagaraToolset_System `GetSystemCompileState`는 UpToDate·hasErrors=false인데 오프스크린 시험에서 시스템이 뜨지 않음. `GetStackIssues`는 늘 "compile in flight"(새 시스템) 또는 300초 시간 초과(재시작 뒤 불러온 시스템)
- 원인: 컴파일 상태는 CPU 스크립트만 본다. `GetStackIssues`는 외부 편집 문맥이 뷰모델을 만들며 컴파일 요청을 다시 세운다
- 해결: 생성·0b 시범 뒤 `Saved/Logs/TDGame.log`에서 `There were issues for compilation of script "<시스템>/` 줄이 0인지 grep하고, 최종 판정은 오프스크린 시험으로 한다
- 범위: 에디터 Python·MCP로 나이아가라 시스템을 만드는 모든 도구
- 증거: `Saved/AgentOps/20261026/wave1-int/assets-fx8n/index.md` 8·10절
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX FX-8n 에셋 생성)

### L-editor-64 UE 5.8 Python `unreal.MaterialProperty`에는 Hidden 항목(픽셀 깊이 오프셋·Displacement·CustomizedUVs)이 없다
- 증상: `editor_make_fluid_track_material.py` 시작 점검 `RuntimeError: 언리얼 Python 이름 없음: MaterialProperty.MP_PIXEL_DEPTH_OFFSET`
- 원인: `Engine/Source/Runtime/Engine/Public/SceneTypes.h:177·181`이 `MP_PixelDepthOffset UMETA(Hidden)` 등으로 숨겨 Python 열거에 노출되지 않는다(노출 18개, MP_MATERIAL_ATTRIBUTES·MP_FRONT_MATERIAL은 있음)
- 해결: MakeMaterialAttributes 노드에 PixelDepthOffset을 모으고 `use_material_attributes=True` 뒤 MP_MATERIAL_ATTRIBUTES 하나로 연결하거나, C++ 에디터 함수로 연결한다
- 범위: Python으로 PDO·Displacement·CustomizedUV를 연결하는 재질 스크립트(`Tools/WorldGen/editor_make_topdown_materials.py` 41행도 목록에 둠, 영향 미확인)
- 증거: `Saved/AgentOps/20261026/wave1-int/assets-fog-track/`(track-material.stdout, material-property-names.stdout)
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX FX-12r 자국 재질)

### L-editor-65 Python으로 볼륨 도메인 재질을 만들면 도메인 변경 직후 `Volume materials must use an Additive blend mode` 경고가 한 번 남는다 — 최종 컴파일 오류 0이면 무해, 블루프린트 CDO에서는 SCS 컴포넌트가 None
- 증상: ① 안개 볼륨 재질 생성 로그에 위 경고 한 줄 ② 교반기 블루프린트 기본 객체(CDO)에서 RotatingMovement를 찾으면 None
- 원인: ① 블렌드 모드를 바꾸기 전 중간 상태 검사 ② 컴포넌트가 SimpleConstructionScript에 있어 CDO에는 없다
- 해결: ① 로그 grep 판정에서 이 줄은 실패로 세지 않고 최종 재컴파일 오류 수로 판정 ② 컴포넌트 확인은 배치한 액터에서 하거나 SCS 노드를 읽는다
- 범위: FluidFX 안개·볼륨 재질 스크립트, 블루프린트 컴포넌트를 검사하는 에디터 스크립트
- 증거: `Saved/AgentOps/20261026/wave1-int/assets-fog-track/index.md` 11·12절
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX FX-10r 안개 무대)

### L-editor-66 UE 5.8 `set_material_instance_texture_parameter_value`(스칼라 판도)는 값을 설정해도 늘 False를 돌려준다
- 증상: `editor_make_fluid_track_material.py`가 MI_TD_FluidTrack_Sand 텍스처 연결 단계에서 실패로 멈춤. 에디터에서 다시 읽으면 값은 들어가 있다
- 원인: `Engine/Source/Editor/MaterialEditor/Private/MaterialEditingLibrary.cpp:1485·1507`이 값을 설정하고도 `bResult`를 true로 바꾸지 않는다
- 해결: 반환값 대신 `get_material_instance_*_parameter_value`로 다시 읽어 판정한다(L-editor-36의 set_material_usage 반환값과 같은 계열)
- 범위: 에디터 Python으로 재질 인스턴스 매개변수를 쓰는 모든 스크립트
- 증거: `Saved/AgentOps/20261026/wave1-assets/track/index.md` 13·60행
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX FX-12r 자국 재질)

### L-editor-67 L-editor-10 재발: 마스터 재질 `delete_asset`이 True인데 에디터가 .uasset 핸들을 쥐고 있어 디스크 삭제가 Error Code 5로 실패하고 재생성이 막힌다
- 증상: `LogFileManager: Warning: Error deleting file: .../Content/FluidFX/Materials/M_TD_FluidWater.uasset (Error Code 5)` 다음 `The asset 'M_TD_FluidWater' already exists ... CanCreateAsset cannot ask the user as the application is running unattended and will return false`
- 원인: 방금 컴파일·저장한 마스터 재질 패키지를 에디터 프로세스가 열어 둔 채라 삭제가 거부된다(L-editor-10은 몽타주에서 같은 증상)
- 해결: 에디터를 멈추고(미저장 0 확인) 이 세션이 만든 미커밋 파일만 디스크에서 지운 뒤 재실행했다. 스크립트 쪽 고침 권장: 삭제 뒤 디스크 잔존을 확인하고 L-editor-10 경로(unload_packages → collect_garbage → os.remove → scan)로 처리
- 범위: overwrite로 기존 재질·에셋을 지우고 다시 만드는 에디터 스크립트(`editor_make_fluid_water_material.py` prepare_master_material 등)
- 증거: `Saved/AgentOps/20261026/wave1-assets/water/index.md` 13행
- 날짜·상태: 2026-10-26 active(스크립트 고침은 후속)
- 발견: claude(FluidFX 파동 1 물 재질 재생성)

### L-editor-68 나이아가라 GPU 사용자 정의 식 입력에서 `Engine.ExecIndex`는 늘 0이고, sim cache JSON 덤프의 벡터는 성분 우선(x 전부 → y 전부 → z 전부)이다
- 증상: NS_TD_FluidDriveMeshArray 2048개가 한 점(-339.06,-339.06,0)에 모여 HeightFollowsDensity 실패. 덤프를 입자별 (x,y,z)로 읽으면 'x=y=z·원점 682개'처럼 보이는 착시
- 원인: GPU 식 입력의 실행 첨자가 0으로 들어온다(생성 단계 첨자를 쓰려면 다른 입력이 필요). sim cache 덤프는 성분별 배열을 이어 붙인다
- 해결: 격자 UV를 `Particles.UniqueID % 격자수²`로 만든다(`Tools/FluidFX/fluid_niagara_drive.py`). 덤프는 성분 우선으로 해석한다
- 범위: GPU 시뮬 나이아가라 이미터의 사용자 정의 식, sim cache 덤프를 읽는 측정 도구
- 증거: `Saved/AgentOps/20261026/wave1-assets/niagara/index.md` 18·26·29행
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX FX-8n 재생성)

### L-editor-69 `-nullrhi` 헤드리스 시험에서는 `FApp::CanEverRender()`가 거짓이라 FluidFX 출력 텍스처(밀도·수면)가 만들어지지 않는다 — 출력을 읽는 시험은 `TDGame.FluidFXRhi.*` + NonNullRHI로 둔다
- 증상: `TDGame.FluidFX.Track.Wetness.SourceBinding` 수면 출력 null(IB4 c2·c3), `TDGame.FluidFX.FogTile.CpuWrapVolumeParameters` "Expected 'The wrap source publishes a density pressure texture' to be true"(TDFluidFogInputTests.cpp:408). 둘 다 오프스크린에서는 통과
- 원인: `EnsureOutputTextures`(TDFluidLiveComponent.cpp:1617)가 `FApp::CanEverRender()`(엔진 App.h:405)가 참일 때만 텍스처를 만든다. 에셋 부재로 오인하기 쉽다(두 번째 발생)
- 해결: 시험을 `TDGame.FluidFXRhi.` 이름 + `EditorContext|NonNullRHI|EngineFilter`로 옮기고, RHI가 없으면 `AddError` 후 false(건너뜀 통과 금지). 선례 `TDFluidTrackInputTests.cpp` SourceBinding
- 범위: FluidFX 출력 텍스처·MID 매개변수·SRV를 읽는 모든 자동화 시험
- 증거: `Saved/AgentOps/20261026/wave2/ib4/`(SourceBinding), `Saved/AgentOps/20261027/wave2-assets/a7-tests/index.md` 1번
- 날짜·상태: 2026-10-27 active
- 발견: claude(FluidFX 파동 2 IB4·A7)

### L-editor-70 Python으로 만든 나이아가라 시스템이 UpToDate인데 영원히 활성화되지 않는다 — 시스템 Spawn·Update 스크립트의 VM 속성 집합이 다르면 `IsReadyToRunInternal`이 거짓, 편집기 빌드는 경고가 없다
- 증상: 질량 NS 3종이 시험에서 아무것도 산출하지 않음(R6 target_splats=0, R7 latency.json cpu·gpu -1, R8b 디스패치 프레임 0). 진단 빌드 결과 시스템 인스턴스 0·Complete 상태, Spawn 속성 20개 대 Update 19개(Spawn에만 `System.CurrentLoopDelay`, Update에만 `SpawnBurst_Instantaneous.SpawnBurst`)
- 원인: 스택 편집 뒤 부분 재컴파일만 되어 두 시스템 스크립트의 속성 표가 어긋난 채 저장된다. 에디터 저장·컴파일 상태 검사로는 드러나지 않는다(L-editor-63과 별개)
- 해결: 저장 전에 `RequestCompile(true)` + `WaitForCompilationComplete`로 강제 전체 재컴파일하고 두 스크립트 속성 집합이 같은지 확인한다(`TDFluidNiagaraModuleEditorLibrary` `RecompileNiagaraSystemFully`, 생성기 판정 ④). 재컴파일 뒤 19=19·22=22, R6 58368 스플랫
- 범위: `editor_make_fluid_driven_niagara.py`·`fluid_niagara_toolset.py`로 만드는 모든 나이아가라 시스템
- 증거: `Saved/AgentOps/20261027/wave2-fix/tests-fix/index.md`, `Saved/AgentOps/20261027/wave2-fix/tests/v1.md`
- 날짜·상태: 2026-10-27 active
- 발견: claude(FluidFX 파동 2 마무리)

### L-editor-71 MD_Volume 재질을 볼류메트릭 클라우드에 쓸 때 `used_with_volumetric_cloud`가 꺼져 있으면 PIE에서 `VolumetricCloudRendering.cpp:1434` assert로 에디터가 크래시한다(그림자 없는 시험 월드에서는 조용히 통과)
- 증상: `LV_TDFluidCloudStage` PIE 2/2 크래시. 먼저 ensure "Pass ApplyLowerHemisphereColor has a read dependency on SkyLight.LowResCloudTexture, but it was never written to.", 이어서 "Assertion failed: Material->GetMaterialDomain() == MD_Volume [VolumetricCloudRendering.cpp:1434]". `CloudLayers` RHI 시험 2개는 통과(비용 수치는 무효)
- 원인: 사용 플래그가 없어 구름 셰이더가 컴파일되지 않는다. 진입 검사(cpp:1974)는 도메인만 보므로 그림자 패스가 돌고 대체 재질이 표면 기본 재질이 되어 assert. 구름 그리기는 2477에서 조기 반환해 LowResCloudTexture가 쓰이지 않는다
- 해결: 생성기에서 블렌드 Additive를 먼저, 도메인 Volume을 뒤에 두고 노드 생성 전에 `used_with_volumetric_cloud=True`, 재컴파일 뒤 `has_material_usage(MATUSAGE_VOLUMETRIC_CLOUD)`가 거짓이면 실패(`editor_make_fluid_cloud_material.py`). PIE는 셰이더 컴파일이 끝난 뒤 시작한다(컴파일 중이면 같은 대체 경로로 assert 가능, 엔진 경합)
- 범위: 볼류메트릭 클라우드 컴포넌트에 넣는 모든 재질·MID
- 증거: `Saved/AgentOps/20261027/wave2-fix/diag-cloud.md`, `Saved/AgentOps/20261027/wave2-assets/a6-pie/index.md` 44·45행, `Saved/AgentOps/20261027/wave2-fix/editor-stages/index.md`(재생성 뒤 PIE 오류 0)
- 날짜·상태: 2026-10-27 active
- 발견: claude(FluidFX FX-11r 에셋)

### L-editor-72 에셋 존재를 판정하는 자동화 시험에서 `CreateTransientPreset`은 출력 재질 슬롯이 비어 있고, 동기 `RunTest` 안의 `RequestAsyncLoad` 완료 대리자는 `FlushAsyncLoading` 뒤에도 불리지 않는다
- 증상: HVOL 에셋이 다 있는데 `TDGame.FluidFXRhi.VolumeDriver.HeterogeneousDriven`·`OffscreenCost`가 "the HVOL slot assets are missing ... material ''"(TDFluidVolumeDriverRhiTests.cpp:436·496). DA를 읽게 고친 뒤에도 차가운 재질이 60스텝 동안 적용되지 않음(:488)
- 원인: ① 일시 프리셋(TDFluidPresetAsset.cpp:191~)은 `OutputMaterials`를 채우지 않는다 ② `s.StreamableDelegateDelayFrames` 기본 1이라 완료 대리자가 월드 없는 `FTickableGameObject`로 미뤄지고, 그 틱은 에디터 엔진 틱에서만 돈다
- 해결: 무대 DA(`DA_TDFluid_Hvol*`)를 불러 슬롯을 읽고, 시험 시작 때 슬롯 재질·볼륨 메시를 `WITH_DEV_AUTOMATION_TESTS` 가드 안에서 `LoadSynchronous`로 미리 불러 둔다(`PreloadVolumeDriverAssets`, check_sync_loads new 0)
- 범위: 런타임이 비동기 로드하는 에셋을 같은 시험 안에서 바로 쓰는 FluidFX 시험
- 증거: `Saved/AgentOps/20261027/wave2-fix/fix-code/index.md`, `Saved/AgentOps/20261027/wave2-assets/a7-tests/index.md` 3번
- 날짜·상태: 2026-10-27 active
- 발견: claude(FluidFX 파동 2 마무리)

### L-editor-73 UE 5.8 에디터 Python 이름·동작 함정 묶음(파동 2 생성기 6곳 수정)
- 증상·해결
  - 열거 이름은 단어마다 `_`: `MSM_TWOSIDED_FOLIAGE` 없음 → `MSM_TWO_SIDED_FOLIAGE`(editor_make_fluid_foliage_material.py)
  - `unreal.Class`에 `is_child_of` 없음 → `unreal.MathLibrary.class_is_child_of`(editor_bake_fluid_height_cache.py)
  - 높이 안개 속성은 `volumetric_fog`가 아니라 `enable_volumetric_fog`(editor_make_fluid_cloud_stage.py)
  - `EditorLevelLibrary`/`LevelEditorSubsystem` `new_level`은 맵을 바로 저장하므로 재실행에는 덮어쓰기 요청이 필요하다
  - C++ 라이브러리 함수가 성공이면 문자열, 실패면 None을 돌려줘 메시지가 버려졌다(editor_make_fluid_mass_input_assets.py `unpack_result`)
  - 나이아가라 CPU(VM) 시뮬 사용자 식은 정수 `%`를 거부 → `fmod`·`floor`(fluid_niagara_drive.py)
  - `run_in_editor.py`는 스크립트가 FAIL을 출력해도 SUCCESS를 낼 수 있다 → 출력 본문으로 판정
- 범위: Tools/FluidFX 에디터 생성기 전반
- 증거: `Saved/AgentOps/20261027/wave2-assets/{a3-hvol-foliage-cloud,a4-mass-input,a5-stages-1,a5-stages-2}/index.md`, `Saved/AgentOps/20261027/wave2-assets/refute-final.md` (e)절
- 날짜·상태: 2026-10-27 active
- 발견: claude(FluidFX 파동 2 에셋 단계)

### L-editor-74 나이아가라 `SetVariableTexture`·`SetVariableObject`는 선언되지 않은 사용자 매개변수를 추가하지 않는다(숫자형 setter는 추가한다)
- 증상: 헤드리스 null RHI 에서 늘 통과하던 `TDGame.FluidFX.Niagara.{TaggedActorReceivesCenterExtentsAndTextures, TagAddedLaterIsDrivenAfterCacheRefresh, DestroyedTaggedActorOrComponentKeepsDriving, PresetSystemSpawnsAttachedOwnedComponent}`가 실제 GPU(`-RenderOffScreen`)에서 "Tagged component DensityPressureTexture parameter is the output texture"로 실패(TDFluidPerfAndNiagaraTests.cpp:254-255)
- 원인: `UNiagaraComponent::SetVariableTexture`(NiagaraComponent.cpp:3000-3015)는 `FNiagaraParameterStore::SetUObject`(NiagaraParameterStore.h:633-641)를 거치는데, 저장소에 없는 변수는 아무것도 하지 않는다. `SetVariableVec3` 등은 `bAdd=true`로 추가한다. 시험 컴포넌트는 시스템이 없어 `User.DensityPressureTexture`가 선언되지 않았다. null RHI 에서는 출력 텍스처가 없어(L-editor-69) 단언 분기를 비켜 갔다
- 해결: 시스템 없는 컴포넌트로 텍스처 구동을 시험하면 `GetOverrideParameters().AddParameter(var, true)`로 먼저 선언한다. 제품 경로는 매개변수를 선언한 시스템에서만 텍스처가 들어간다(`TDGame.FluidFXRhi.Niagara.Drive.*`가 증명)
- 범위: UE 5.8 Niagara 사용자 매개변수 쓰기 전부(텍스처·오브젝트형)
- 증거: `Saved/AgentOps/20261030/wave3-assets/niagara/fix-r1.md`·`refute-r1.md`, 커밋 7e06b181
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 통합 확인)

### L-editor-75 Python으로 만든 반투명 입자 재질에서 `ParticleColor` 기본 출력은 RGB(float3)라 Custom HLSL 의 `.a`가 swizzle 오류
- 증상: `M_TD_FluidRainDrop` 컴파일 오류(Custom 노드 입력의 `.a` 접근에서 invalid swizzle)
- 해결: `connect_material_expressions`의 from_output_name 에 "RGBA"를 명시한다(생성기 `link(particle_color, custom, "ParticleColor", "RGBA")`, 비우면 기본 출력 RGB)
- 범위: UE 5.8 `MaterialEditingLibrary`, 여러 출력 핀을 가진 표현식(ParticleColor·VertexColor 등)
- 증거: `Saved/AgentOps/20261030/wave3-assets/a3-materials-2/index.md`, `Tools/FluidFX/editor_make_fluid_rain_assets.py`(커밋 47a548a1)
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 에셋 단계)

### L-editor-76 무대 PPV가 수동 노출만 덮어쓰고 물리 카메라 노출(기본 켬)을 끄지 않으면 EV100 9~10이 되어 어두운 주광 무대가 PIE에서 완전 검정
- 증상: `LV_TDFluidCloudStage` PIE HighResShot·Shot 모두 픽셀 0.0(시네 카메라·폰 시선), 오류·경고 0. 다른 무대 스크린숏은 정상. 파동 2 생성부터 있었으나 화면 확인 기록이 없었다
- 원인: 생성기 PPV가 `auto_exposure_method` 수동과 EV 값만 설정하고 `auto_exposure_apply_physical_camera_exposure`(기본 켬)를 덮어쓰지 않아 카메라 물리 노출이 적용됐다. 기본 10 lux 주광에서 검정
- 해결: PPV 설정에 `override_auto_exposure_apply_physical_camera_exposure=True`·`auto_exposure_apply_physical_camera_exposure=False`를 함께 넣는다. PIE 월드에서 이것만 끄면 휘도 0.03 → 34~72
- 범위: 수동 노출을 쓰는 모든 무대 생성기(FluidFX 무대·룩데브)
- 증거: `Saved/AgentOps/20261030/wave3-finish/cloud/index.md`, `Tools/FluidFX/editor_make_fluid_cloud_stage.py`(커밋 47a548a1)
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 마무리)

### L-editor-77 폰을 따라가는 FluidFX 물 액터에 시뮬 상태 캐시를 구우면 시작 창 상태만 담겨, 시작 창에 물이 없으면 전부 0인 캐시가 된다
- 증상: `pie_bake_fluid_sim_cache.py`로 `LV_TDFluidWaterStage` 강(`TDGen_WaterRiverSimulation`)을 구웠더니 10채널 CRC가 모두 0 바이트 값(2679683870), 머리 sim 시간 4.94 s
- 원인: 그 액터는 `bShouldFollowPlayerPawn` 참이고 플레이어 시작 (-2600,0) 창에 강이 없다. 씨앗은 중심 차가 반 칸을 넘으면 거부되므로(TDFluidSimStateCacheAsset.cpp:191-194) 폰을 옮겨 구워도 시작 때 쓰이지 않는다. 같은 맵의 `pie_probe_fluid_water.py`가 이 추종에 기대어 고정 창으로 바꿀 수도 없었다
- 해결: 시뮬 캐시는 고정 창 액터에만 굽는다(spec-fx16b 2.5-5). 굽기 요청 `backend`는 `TD.FluidFX.WaterGpu` 기본 0이라 `cpu`
- 범위: FX-16b 시뮬 상태 캐시, 폰 추종 FluidFX 액터 전부
- 증거: `Saved/AgentOps/20261030/wave3-assets/a6-bakes/index.md`, `Saved/AgentOps/20261030/wave3-finish/river/index.md`
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 에셋 단계)

### L-editor-78 GUI 에디터 MCP RunTests로 `TDGame.FluidFX.` 전체를 돌리면 600초 제한에 걸리고 장치 제거가 날 수 있다
- 증상: `python Tools/check_automation_tests.py --filter TDGame.FluidFX.`가 `Tools/uemcp.py:25` TimeoutError(600초), 같은 시각 에디터 로그 `DXGI_ERROR_DRIVER_INTERNAL_ERROR` 장치 제거(`Subsystem.CombatStampRoutesPerActorBasis` 도중). 같은 시험은 오프스크린에서 통과
- 해결: FluidFX 시험은 `build_and_test.py --rhi null`(헤드리스)과 `--rhi offscreen`(실제 GPU)으로만 돌린다. GUI 실행이 필요하면 필터를 묶음 단위로 좁힌다
- 범위: FluidFX 자동화 시험 490개 규모
- 증거: `Saved/AgentOps/20261029/wave3/int/r0/index.md` (4)절, `Saved/Logs/TDGame.log` 6537행(2026-10-30)
- 날짜·상태: 2026-10-30 active(장치 제거의 근본 원인은 미검증)
- 발견: claude(FluidFX 파동 3 통합 확인)

### L-editor-79 FluidFX 접촉·스탬프 시험의 탐침 위치는 스탬프 실효 반경과 입자 착수 반경 분포를 먼저 계산해 정한다
- 증상: `TDGame.FluidFXRhi.WaterParticleAssets.SplashContactReachesWater`가 "Whitewater rises at the splash center within 2 seconds (0.00000)"로 결정론적 실패(TDFluidWaterParticleRhiTests.cpp:1044), 접촉 점은 4개 들어옴
- 원인: 탐침이 진입점 칸 하나(칸 25cm)인데 접촉 스탬프 실효 반경은 max(반경, 칸 절반)=12.5cm(TDFluidGpuStampBins.cpp:340-344)이라 중심 칸에 닿으려면 30.2cm 안에 떨어져야 한다. 왕관 입자 착수 반경은 38~80cm, 물방울 86~131cm(프레임 적분)라 기하적으로 불가능
- 해결: 진입점 반경 200cm 안 밀도 출력 최대를 읽는다(실측 0이 아닌 칸 3개, 39.5~63.7cm, 최대 0.295). 명세 편차는 D-64 파동 3 편차에 적는다
- 범위: 입자·브러시가 물·연기 필드에 찍히는 것을 한 칸으로 확인하는 시험
- 증거: `Saved/AgentOps/20261030/wave3-finish/r3/{debug,fix,refute}.md`, 커밋 78fa1c1a
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 마무리)

### L-editor-80 UE 5.8 HVOL 라이브 셰이딩에서 재질의 ObjectPositionWS 는 (0,0,0), ObjectBounds 는 거대값이라 이 둘에 기댄 창 계산이 화면에서 사라진다
- 증상: FX-9 HVOL(이질 볼륨)이 PIE 화면에 전혀 안 보임. 컴포넌트 보임·바운즈 시야 안·슬롯 MID·시뮬 RT 정상, 소광 50 으로도 안 보이고 위치 무관 상수 밀도만 보임. 1차 창 시험은 한 장씩이라 결과가 서로 모순(같은 조건 반복 안 함)
- 원인: 엔진 LiveShadingPipeline.usf 269~276 이 빈 매개변수로 LWC 자료를 만들고(PrimitiveId 주석 처리, ObjectWorldPosition TODO) LiveShadingUtils.ush 14~20 은 LocalToWorld·LocalObjectBounds 만 채운다. 셰이더 덤프에서 VolObjectPosition=GetObjectWorldPosition, VolObjectBoundsCm=GetPrimitiveData(Parameters).ObjectBounds 확인. lerp(거대값, 정상값, 1) 은 x+1·(y−x) 소거로 정상값이 안 나온다
- 해결: 오브젝트 위치는 상수 0 을 TransformPosition(로컬→월드), 바운즈는 ObjectLocalBounds 반 크기를 Transform(로컬→월드) 후 Abs, HLSL 의 역할별 값 고르기는 lerp 대신 선택식(`조건 ? a : b`). WorldPosition·LocalPosition·CameraPosition 은 정상. 회전된 수동 볼륨은 AABB 처리 필요
- 범위: UE 5.8 Heterogeneous Volume(라이브 셰이딩, Preshading 0) 재질. Preshading 1 런타임 전환은 HVOL 렌더 중 RDG 접근 위반으로 에디터 충돌(미해결)
- 증거: `Saved/AgentOps/20261031/hvol/r2/`(탐침 재질 3회 반복 일치, 원본·코드 수정·코드+입력 수정 A/B), 커밋 abe87c40
- 날짜·상태: 2026-10-31 active(실제 에셋 재생성 뒤 확인은 cal-editor-ops 워크플로)
- 발견: claude(FluidFX 일괄 보정 frame)

### L-editor-81 에디터 Python 에서 블루프린트 액터 컴포넌트에 set_editor_property 를 여러 번 쓰면 첫 쓰기 뒤 구성 스크립트가 다시 돌아 나머지 값이 버려진 옛 컴포넌트에 들어간다
- 증상: 무대 생성기가 교반기 메시·배율·피벗·회전 속도를 차례로 썼는데 맵에 PivotTranslation·RotationRate 이름표가 없고 PIE 에서 블루프린트 기본값(90°/s, 배율 0.4)으로 돈다. 옛 핸들로 다시 읽으면 값이 맞아 검사가 거짓 통과
- 원인: set_editor_property → ActorComponent.cpp 1444 ConsolidatedPostEditChange → RerunConstructionScripts 로 컴포넌트를 새로 만든다. set_editor_properties 는 Pre/PostEditChange 를 한 번만 부른다(PyWrapperObject.cpp 977~985). 또 unreal.Rotator(0, v, 0) 위치 인수는 (roll, pitch, yaw) 라 v 가 pitch 가 된다
- 해결: 쓰기마다 get_component_by_class 로 핸들을 새로 받고 set_editor_properties 로 한 번에 쓴 뒤 새 핸들로 다시 읽어 다르면 FAIL(공용 set_component_properties, editor_make_fluid_particle_stage.py). Rotator 는 yaw= 키워드. unreal.load_class 결과(unreal.Class)는 __name__ 이 없어 get_name() 사용
- 범위: 구성 스크립트가 있는 블루프린트 액터의 컴포넌트 속성 편집(무대 생성기 전부)
- 증거: 2차 진단 D4(LV_TDFluidInputStage.umap 이름표·frame 픽셀), 3차 T3 가짜 unreal 핸들 재생성 시험 18/18, 커밋 e9161019·c117e536
- 날짜·상태: 2026-10-31 active(맵 재생성 뒤 PIE 확인 대기)
- 발견: claude(FluidFX 일괄 보정 frame)

### L-editor-82 후처리 볼륨 노출이 AEM_MANUAL 이면 엔진이 min/max brightness(exposure_ev100)를 무시해, 룩데브가 노출을 바꿨다고 기록해도 화면은 그대로다
- 증상: 안개 계획 exposure_ev100 1.0 이 촬영에 반영되지 않음(무대 PPV 수동·바이어스 0). 구름 무대(EV 12)에 바이어스 변환을 전역으로 걸자 −12 스톱 검은 화면 회귀
- 원인: PostProcessEyeAdaptation.cpp 652~655(수동 모드는 바이어스와 물리 카메라 EV100 만 씀)
- 해결: 수동 볼륨은 auto_exposure_bias = 원래 바이어스 + 물리 카메라 EV100 − 목표 EV100 이지만 무대마다 의도가 달라 계획 키 exposure_manual "bias" 를 줄 때만 적용하고 기본은 무시·기록(pie_lookdev_fluid.py)
- 범위: AEM_MANUAL 후처리 볼륨이 있는 모든 무대
- 증거: 2차 진단 D3, 3차 T1 반박(구름 회귀), 커밋 be874211
- 날짜·상태: 2026-10-31 active
- 발견: claude(FluidFX 일괄 보정 frame)

### L-editor-83 UPROPERTY(Transient) 만 붙은 C++ 멤버는 Python get_editor_property 로 읽을 수 없다
- 증상: 탐침이 `TDFluidLiveComponent: Failed to find property 'gpu_density_pressure_target'`
- 해결: BlueprintPure 게터(get_density_pressure_texture)를 부른다(probe.rt 의 function 키). 게터가 Texture2D(CPU 백엔드)를 주면 렌더 타깃 읽기 대상이 아니다
- 범위: 에디터 Python 의 C++ 객체 속성 접근
- 증거: frame 재촬영 cloud_g4_painter result.json, TDFluidLiveComponent.h 397~398
- 날짜·상태: 2026-10-31 active
- 발견: claude

### L-editor-84 FluidFX 대량 입력(GPU DI 스플랫) 종류는 SolverBackend 가 Gpu 여야 한다 — 비우면 StructDefaults Cpu, Auto 도 해상도 256 이하면 CPU 라 입력 0
- 증상: fx5 화염방사기 frame 5장에 화염 0, 로그 오류 0
- 원인: ShouldUseGpuSolver 는 Auto 일 때 해상도 >256 만 GPU. BuildMassInputTarget(TDFluidLiveComponent.cpp 2458)은 !bUsesGpuSolver 면 거부, PublishMassInputTargets 는 GPU 액터만 공개 → DI bHasTarget 거짓
- 해결: 정본 Kinds.Flamethrower·SnowAvalanche 에 "SolverBackend": "Gpu"(커밋 6f1b6a30). 새 대량 입력 종류도 같다
- 범위: NS 렌더러 없이 GPU DI 스플랫으로만 입력하는 종류
- 증거: 2차 진단 D2(Saved/FluidFX/calib/frames/fx5_flamethrower)
- 날짜·상태: 2026-10-31 active(재촬영 확인 대기)
- 발견: claude

### L-editor-85 에디터 C++ 도구가 디스크에 있지만 불러오지 않은 패키지에 CreatePackage 후 저장하면 '일부만 로드되어 저장할 수 없음' 치명 오류, delete_asset 뒤 남은 파일은 다음 저장에서 옛 지오메트리 컬렉션을 덮어 읽어 단언이 터진다
- 증상: 입력 무대 overwrite 재생성에서 에디터가 두 번 크래시 — ① TDFluidMeshFieldEditorLibrary.cpp 359 저장 중 부분 로드 패키지 치명 오류 ② EditorAssetLibrary.delete_asset 이 True 인데 GC_TD_FluidInputBricks.uasset 이 디스크에 남고, 다음 저장 때 FullyLoad 가 옛 GC 를 새 객체 위에 읽어 GeometryCollectionRenderData.cpp 711 !bIsInitialized
- 해결(임시): 메시 단면 텍스처를 미리 불러 두고 남은 GC 파일을 옮긴 뒤 다시 실행. 근본: CreatePackage 다음 Package->FullyLoad(), 삭제 뒤 디스크 잔존 확인(L-editor-67 와 같은 계열) — TDFluidMeshFieldEditorLibrary.cpp 359·TDFluidFractureEditorLibrary.cpp 76 수정 필요(C++, 재시작 빌드)
- 범위: 에디터 전용 C++ 라이브러리로 기존 에셋을 덮어쓰는 무대 생성기(입력 무대)
- 증거: Saved/Crashes 2026-10-31 01:35·01:36, `Saved/AgentOps/20261031/hvol/regen/`(backup 포함)
- 날짜·상태: 2026-10-31 active(C++ 미수정)
- 발견: claude(FluidFX 일괄 보정 무대 재생성)

### L-editor-86 Python 원격 실행은 호출마다 같은 전역 이름 공간에서 돈다 — 슬레이트 틱 콜백이 나중에 전역(PARAMS 등)을 읽으면 다른 호출의 값이다
- 증상: 룩데브 예약 촬영 중 schedule.events move 가 `KeyError: 'output'`(begin_pawn_walk 의 `PARAMS['output']`), 장면 무효. 가짜 unreal 하네스(호출마다 새 globals)에서는 통과
- 원인: 원격 실행 명령은 같은 `__main__` 전역에서 실행되어, 예약을 건 호출 뒤 다른 원격 호출(TD_NAV_STATE 점검·상태 조회)이 `PARAMS` 를 덮어쓴다. register_slate_post_tick_callback 콜백은 전역 이름을 실행 시점에 찾는다
- 해결: 콜백이 쓸 값은 등록할 때 상태 사전(state)에 복사해 두고 전역을 읽지 않는다(eb4c51dd, 예약 상태 output·tag). 하네스는 공유 이름 공간 모드(EDITOR_SHARED_NAMESPACE, F4c)로 재현
- 범위: run_in_editor·pie_showcase EDITOR_CODE 로 틱 콜백을 거는 모든 도구(pie_lookdev_fluid 예약·걷기 등)
- 증거: Saved/FluidFX/calib/frames/fog_f2_passive(2026-10-31 frame 2차), 하네스 수정 전 빨강·수정 뒤 275/275
- 날짜·상태: 2026-10-31 active
- 발견: claude(FluidFX 일괄 보정 frame)

### L-editor-87 원격 실행(run_in_editor·pie_showcase EDITOR_CODE) 코드 문자열 안에 '<이름>.py' 낱말이 있으면 UE 가 코드 전체를 파일 경로로 읽어 실행하지 않는다
- 증상: 병합 뒤 모든 룩데브 원격 호출이 '결과 해석 실패', 에디터 로그 'Could not load Python file …editor_make_fluid_fog_material.py'(오류 문구 안에 생성기 파일 이름을 넣은 줄 때문)
- 원인: UE Python 원격 실행(ExecuteFile 모드)이 명령 문자열에 '.py' 로 끝나는 낱말이 있으면 파일 실행으로 해석한다(추정: 명령 판별 규칙, 엔진 소스 미확인 — 현상은 재현)
- 해결: 원격 코드 문자열(오류 메시지·주석 포함)에 '.py' 를 쓰지 않는다 — 'editor_make_fluid_fog_material' 처럼 확장자를 뺀다(3425aa53)
- 범위: Tools/run_in_editor.py 로 보내는 모든 코드 문자열(pie_lookdev_fluid LOOKDEV_ACTIONS·pie_showcase EDITOR_CODE 등)
- 증거: Saved/AgentOps/20261031/integ/(g12 1차 실패·수정 뒤 통과), 통합 워크플로 wf_be2f48dc-5aa
- 날짜·상태: 2026-10-31 active
- 발견: claude(FluidFX 시간 결정론 통합)

### L-editor-88 UE 5.8 에디터 Python 에는 unreal.KismetMaterialLibrary 가 없다 — MPC 스칼라 읽기·쓰기는 unreal.MaterialLibrary
- 증상: 룩데브 키 오류 "module 'unreal' has no attribute 'KismetMaterialLibrary'"(MPC FogFlowOriginSeconds 설정)
- 해결: unreal.MaterialLibrary.set_scalar_parameter_value(world, collection, name, value)·get_scalar_parameter_value 를 쓴다(C++ UKismetMaterialLibrary 가 파이썬에서는 MaterialLibrary 로 노출). 가짜 unreal 하네스도 같은 이름으로(3425aa53)
- 범위: UE 5.8 에디터 Python 의 재질 매개변수 컬렉션 접근
- 증거: Saved/AgentOps/20261031/integ/f7/(1차 키 오류·수정 뒤 통과)
- 날짜·상태: 2026-10-31 active
- 발견: claude(FluidFX 시간 결정론 통합)

### L-editor-89 룩데브 한 PIE 세션의 첫 단계 s00 은 PIE 시작 직후 이력이 남아 뒤 단계와 다를 수 있다 — 대조 기준이 s00 이라 대조가 깨진다
- 증상 원문: K7 snow_groove "대조 실패 groove_line.groove_width_cm"(s00 7.951 대 8.06~8.12, 명암만 다름 — 눈 레벨 첫 PIE 약 27 s 뒤 렌더 예열), w1b sea_img16 사전 점검에서 s01↔s02 상관 0.992~0.996 인데 s00 만 0.93(조명 같고 수면 반짝임만 다름, 리셋을 통과한 상태가 남음, 층 미확정)
- 해결: 판정기 대조는 max|c_i − c_0| 라 첫 단계 하나가 실패를 만든다. 재실행 전에 calib_sweep --stage verify(v00 3단계, 별도 잠금·run-dir) 로 사전 점검하고 calib_judge 로 대조만 확인한다(지표에 영향이 없으면 그대로, groove 처럼 지표가 흔들리면 base camera 예열 = 걷기 + 45 게임초 1장). 화소 성분 수를 직접 세는 임시 판정은 반짝임을 공으로 세어 오판한다(판정기 측정기로 볼 것)
- 범위: pie_lookdev_fluid 단계 스윕(reset reinit·keep), 첫 PIE 의 렌더 예열·리셋 경로 밖 상태
- 증거: Saved/AgentOps/20261101/precheck/(judge.json·verify-judge.json), Saved/AgentOps/20261101/diag-w1b-s00.md, Saved/AgentOps/20261031/cal-issues.md groove 진단
- 날짜·상태: 2026-11-01 active(w1b 수면 누출 층은 미검증)
- 발견: claude(FluidFX 일괄 보정 K7·K9 재실행)

### L-editor-90 보정 허용 띠를 한 세션 안 잡음(median_floor·대조 단계)으로만 잡으면 다른 세션의 재측정이 고정 오프셋(기대의 약 1%)으로 실패한다
- 증상 원문: W11 최종 검증 verdict "재현 실패 — W 경로 점검 / 재현 근거 없음(유지)" 4건. #11 cap.atmosphere.p95_over_p5 기대 1.4681(±0.0079) 대 1.4556·1.4557, #17 derived.uw_blue_over_red 2.6132(±0.00078) 대 2.60545·2.6060 — 두 회차는 서로 0.00015~0.0005 안에서 일치
- 해결: 무작위 잡음이 아니라 세션 간 고정 오프셋이므로 재측정 실패를 W 경로 결함으로 읽기 전에 (1) 같은 세션 두 회차 일치 여부 (2) A/B v00·v01 이 여전히 민감하게 갈라지는지 (3) 정본·에셋 값 대조(W9)를 본다. 다음 사전 등록부터 재현 허용에 세션 간 편차 항(같은 장면을 다른 에디터 세션에서 2회 잰 차이)을 넣는다
- 범위: calib_sweep verify 단계 재현 판정(채택 후보 중앙값 ± max(2×median_floor, tolerance)), 에디터 재시작·재생성 뒤 다시 잰 화면 비율 지표
- 증거: Saved/FluidFX/calib/final/verdict-w11.json, Saved/AgentOps/20261106/w/w11-user-items-diag.md
- 날짜·상태: 2026-11-06 active(오프셋 원인 — 셰이더 캐시·재질 재생성·하드웨어 — 는 미검증)
- 발견: claude(FluidFX W11 최종 검증)

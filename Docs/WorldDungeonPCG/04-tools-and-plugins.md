[← 인덱스로](../WorldDungeonPCG_Plan.md)

# 4. 필요한 도구·플러그인·설정

## 4.1 프로젝트 플러그인 (uproject)

| 플러그인 | 상태(5.8) | 용도 | 언제 |
|---|---|---|---|
| PCG | 정식(5.7부터 production-ready) | 바이옴·도로·POI·룸 드레싱 그래프 | Phase 0에서 활성화 |
| PCGGeometryScriptInterop | 정식 | Mesh Sampler, Primitive Cross-Section(벽면 소품) | Phase 2 |
| PCGExternalDataInterop | Beta | Alembic 점군 로드. CSV는 없음 | 필요 시(현재 계획 없음) |
| PCGBiomeCore / PCGBiomeSample | Experimental, C++ 없음 | 구조 참고용. 코어 로직 종속 금지 | 참고만 |
| ScriptableToolsFramework + ScriptableToolsEditorMode | Beta, 기본 비활성 | 뷰포트 상호작용 툴. 1단계 툴에는 불필요 | 보류 |
| GameplayAbilities, StateTree 등 | 이미 활성 | 기존 전투 | — |

## 4.2 엔진 설정·월드 설정 체크리스트
- 월드 파티션: 새 월드 `L_TDWorld_Main`은 월드 파티션으로 생성, `bEnableStreaming` 켬(World Settings). 런타임 해시는 기본 RuntimeHashSet(셀 25600cm 기본) → 탑다운 가시거리에 맞춰 셀·로딩 범위를 프로파일링(설계서 R-12). 던전 아틀라스용 별도 그리드(`TargetGrids`)를 둘지는 Phase 1 실측 후 결정.
- 내비메시: `ARecastNavMesh::bIsWorldPartitioned` 켬, `RuntimeGeneration = Static`, `bFixedTilePoolSize/TilePoolSize` 조정, `AWorldSettings::NavigationDataChunkGridSize`를 런타임 셀 크기에 정렬. 빌드는 `WorldPartitionNavigationDataBuilder`.
- 플레이어 컨트롤러: `bEnableStreamingSource` 유지, `bStreamingSourceShouldBlockOnSlowStreaming`는 끄고(전환 연출로 흡수) 목적지 소스에서만 우선순위 Highest.
- 데이터 레이어: 퀘스트 상태용 레이어는 애셋 타입 Runtime, `InitialRuntimeState` 설정. 세이브 복원은 월드 파티션 초기화 후.
- PCG: 월드에 PCGWorldActor(파티션 그리드 크기), 바이옴 컴포넌트는 `bIsComponentPartitioned` + HiGen(그래프 설정). 룸 드레싱 컴포넌트는 비파티션 + Normal 모드. 결정론 위해 컴포넌트 `Seed`를 C++가 대입.
- 콘솔 변수(개발): `wp.Runtime.*` 디버그 표시, `pcg.RuntimeGeneration.EnableDebugOverlay`, `pcg.GraphExecution.DebugDrawGeneratedCells`.

## 4.3 소스 모듈과 빌드 설정

2026-09-12 현황: `Source/TDWorldGen`(Runtime; Public/Dungeon·Public/World 계약 헤더 + 알고리즘, 의존 Core/CoreUObject/Engine/DeveloperSettings/Json/JsonUtilities)과 `Source/TDGameEditor`(Editor; 랜드스케이프 함수, WorldGen 베이커·라이브러리·커맨드릿)가 uproject·Target에 등록되어 컴파일된다. PCG 의존은 아직 넣지 않았다(바이옴 그래프 참조는 `TSoftObjectPtr<UObject>`).
- `Source/TDWorldGen/TDWorldGen.Build.cs`: Runtime, 의존 `Core, CoreUObject, Engine, PCG`(데이터 타입 참조 시), `NavigationSystem`(검증 시 경로 질의).
- `Source/TDGameEditor/TDGameEditor.Build.cs`: Editor, 의존 `UnrealEd, LevelEditor, ToolMenus, EditorSubsystem, MessageLog, PCG, PCGEditor, TDGame, TDWorldGen`. uproject `Modules`에 `{"Name":"TDGameEditor","Type":"Editor","LoadingPhase":"PostEngineInit"}` 추가.
- Target: `TDGameEditor.Target.cs`에 두 모듈 포함. 새 소스 추가 후 `compile_commands.json` 재생성(AGENTS.md 11절).

## 4.4 에이전트 작업 도구

2026-09-12부터 프로젝트 안 `Tools/`(카탈로그 `Tools/README.md`, 제미나이 진입점 `GEMINI.md`)가 기준이다: `ue_editor.py`(에디터 수명주기·빌드·재시작), `run_in_editor.py`(에디터 Python 전체), `uemcp.py`(MCP 셸 호출), `WorldGen/`(야외 생성·검증·시드 선택·베이크), `DungeonGen/`(던전 생성·검증·후보·베이크), `templates/`(새 도구 규칙). 아래 표는 그 이전 기준이다.
| 작업 | 도구 | 비고 |
|---|---|---|
| 컴파일 | Build.bat(에디터 꺼짐) / 라이브 코딩(에디터 켜짐) | 메모리 `tdgame-build-and-test-workflow` |
| 자동화 테스트 | `UnrealEditor-Cmd -ExecCmds="Automation RunTests TDGame"` 또는 MCP AutomationTestToolset | 생성기·검증기 회귀 |
| 에디터 조작 | 공식 MCP(액터 배치·프로퍼티·PIE·자동화 테스트·설정) | 세션 시작 시 연결 실패면 `tools/uemcp.py`로 HTTP 직접 호출 |
| MCP 미지원 작업 | 에디터 Python 원격 실행(`tools/uepy.py`) | 월드 생성, 데이터 레이어·월드 파티션 설정, 레벨 인스턴스 생성, PCG 생성 호출 |
| 배치 생성·검증 | `-run=WorldPartitionBuilderCommandlet -Builder=TDWorldGenBuilder`(자체 빌더) | Phase 2 이후 |
| 내비·HLOD 빌드 | `-Builder=WorldPartitionNavigationDataBuilder`, `-Builder=WorldPartitionHLODsBuilder -SetupHLODs -BuildHLODs` | Phase 1(내비), Phase 4(HLOD) |
| 캡처 증거 | PIE `HighResShot`, MCP CaptureEditorImage | `Docs/Validation/`에 저장 |

## 4.5 만들어야 할 에디터 도구 (우선순위)
1. 던전 슬롯 액터 `ATDDungeonSlotAnchor`의 `CallInEditor` 버튼(Generate / Regenerate Unlocked / Validate / Bake)과 `CheckForErrors` 리포트 — Phase 2.
2. 월드 생성기 액터(또는 에디터 서브시스템 명령) `Generate Outdoor / Validate Outdoor / Regenerate PCG` — Phase 3.
3. 검증 리포트 메시지 로그 카테고리 `TDWorldGen` 등록 + 액터 토큰 클릭 이동 — Phase 2.
4. 커맨드릿(월드 파티션 빌더 파생) — Phase 2 말.
5. 노마드 탭 UI(잠금·필터·후보 목록) — Phase 3~4, 필요할 때만.

## 4.6 외부 참고 자료 (부록 A 보강)
- 설계서 R1~R5(Epic 공식 문서). 추가: Using PCG Generation Modes, PCG Biome Core 개요·레퍼런스, Level Instancing, Data Layers 문서(URL은 설계서 부록 A).
- 던전 기법: Dormans "Cyclic Dungeon Generation"(Unexplored), Gungeon·Isaac·Hades 분석(boristhebrave, kotaku), Dungeon Architect SGF 문서, BenPyton/ProceduralDungeon, Edgar. 상세 URL은 `research/dungeon-generation.md`.
- 커뮤니티 PCG 사례 2025~2026: `research/pcg-bake-data-community.md` 6절.

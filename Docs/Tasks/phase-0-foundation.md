# Phase 0 — 준비 (모듈 · 플러그인 · 규칙)

| 상태 | 개수 |
|---|---|
| todo | 7 |
| doing | 0 |
| done | 0 |
| decision | 2 |

선행 없음. 이 Phase가 끝나야 Phase 1 코드 작업을 시작한다.

### P0-01 에디터 모듈 `TDGameEditor` 신설
- 상태: todo
- 우선순위: 높음
- 선행: 없음
- 목표: 에디터 전용 코드(툴, 커맨드릿, 커스터마이징)를 담을 모듈을 만든다.
- 완료 조건:
  - `Source/TDGameEditor/TDGameEditor.Build.cs`, `TDGameEditor.h/.cpp`(모듈 클래스, `StartupModule`에서 메시지 로그 카테고리 `TDWorldGen` 등록)
  - `TDGame.uproject` Modules에 `{"Name":"TDGameEditor","Type":"Editor","LoadingPhase":"PostEngineInit"}` 추가, `Source/TDGameEditor.Target.cs`에 모듈 포함
  - Build.bat 컴파일 성공, 에디터 기동 시 모듈 로드 로그 확인
- 산출물: 위 파일들
- 검증: 컴파일 로그, `LogModuleManager`에 TDGameEditor 로드
- 참조: G-12, 03-architecture 3.1, 04-tools 4.3, research/persistence-editor-batch.md 2.3
- 기록: 2026-09-09 작성

### P0-02 생성 알고리즘 모듈 `TDWorldGen` 신설
- 상태: todo
- 우선순위: 높음
- 선행: 없음
- 목표: 월드·던전 생성·검증의 순수 C++ 계층과 정의 데이터 타입을 담는 런타임 모듈을 만든다.
- 완료 조건:
  - `Source/TDWorldGen/TDWorldGen.Build.cs`(Core, CoreUObject, Engine), 모듈 클래스, `Public/TDWorldGenTypes.h`에 `FTDSeedContext`, `TD_WORLDGEN_VERSION`, `FTDValidationReport` 뼈대
  - `TDGame.Build.cs`가 `TDWorldGen`에 의존, uproject Modules 등록
  - 시드 파생 단위 테스트 1개(`TDGame.WorldGen.SeedDerivationIsDeterministic`): 같은 (마스터 시드, 도메인, 인덱스) → 같은 스트림 첫 5개 값
- 산출물: 위 파일들, `Source/TDWorldGen/Private/Tests/TDWorldGenSeedTests.cpp`
- 검증: `Automation RunTests TDGame.WorldGen` 통과
- 참조: 03-architecture 3.1·3.2, research/dungeon-generation.md 4절
- 기록: 2026-09-09 작성

### P0-03 플러그인 활성화
- 상태: todo
- 우선순위: 높음
- 선행: 없음
- 목표: PCG, PCGGeometryScriptInterop을 프로젝트에 켠다.
- 완료 조건: `TDGame.uproject` Plugins에 두 항목 `Enabled: true`, 에디터 기동 후 PCG 그래프 에셋 생성 가능. 설정 변경은 사용자에게 한 줄 알림 후 진행.
- 검증: MCP PluginToolset `IsEnabled`, 에디터 로그
- 참조: G-08, 04-tools 4.1
- 기록: 2026-09-09 작성

### P0-04 콘텐츠 폴더·이름 규칙 문서화와 폴더 생성
- 상태: todo
- 우선순위: 중간
- 선행: 없음
- 목표: `Content/World`, `Content/PCG`, `Content/Dungeon`, `Content/POI`, `Content/Editor` 폴더와 접두어 규칙(`DA_TD`, `LI_TD`, `PCG_TD`, `L_TD`, `DL_TD`)을 확정한다.
- 완료 조건: 폴더 생성(MCP AssetTools create_folder), `Docs/WorldDungeonPCG/naming-and-folders.md` 작성, AGENTS.md 3절과 충돌 없음
- 참조: R-121, 03-architecture 3.6
- 기록: 2026-09-09 작성

### P0-05 clangd 데이터베이스 재생성 절차 확인
- 상태: todo
- 우선순위: 낮음
- 선행: P0-01, P0-02
- 목표: 새 모듈 추가 후 `compile_commands.json`을 재생성해 Serena 심볼 도구가 새 모듈을 인식하게 한다.
- 완료 조건: AGENTS.md 11절 명령 실행, `serena project health-check` 통과
- 참조: 메모리 `tdgame-mcp-preflight`
- 기록: 2026-09-09 작성

### P0-06 자동화 테스트 플래그 컨벤션
- 상태: todo
- 우선순위: 낮음
- 선행: P0-02
- 목표: 5.8의 `enum class EAutomationTestFlags` 조합(`EditorContext | ProductFilter`)과 테스트 이름 접두어(`TDGame.WorldGen.*`, `TDGame.Dungeon.*`, `TDGame.World.*`)를 정하고 기존 전투 테스트 픽스처 패턴을 문서화한다.
- 완료 조건: `Docs/WorldDungeonPCG/testing-conventions.md` 작성
- 참조: research/persistence-editor-batch.md 3.4, 메모리 `tdgame-build-and-test-workflow`
- 기록: 2026-09-09 작성

### P0-07 시작 맵 전환 계획
- 상태: todo
- 우선순위: 중간
- 선행: P1-01
- 목표: `GameDefaultMap`/`EditorStartupMap`을 새 월드로 바꿀지 결정된 뒤 Config를 갱신한다(결정 D-02 참조).
- 완료 조건: D-02 결정 반영, `Config/DefaultEngine.ini` 갱신, LV-Game은 전투 테스트용으로 유지
- 참조: 02-current-state-gap 2.4
- 기록: 2026-09-09 작성

### P0-D1 템플릿 잔재 정리 여부
- 상태: decision
- 우선순위: 낮음
- 목표: `Source/TDGame/Variant_Strategy`, `Variant_TwinStick`와 대응 콘텐츠를 삭제할지 결정한다.
- 참조: decisions.md D-01
- 기록: 2026-09-09 작성

### P0-D2 시작 맵 결정
- 상태: decision
- 우선순위: 중간
- 목표: 새 월드 파티션 월드를 시작 맵으로 할지, 별도 개발 맵으로 둘지 결정한다.
- 참조: decisions.md D-02
- 기록: 2026-09-09 작성

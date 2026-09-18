# 에이전트 공통 관리 규칙 (정본)

[← 인덱스로](AgentCollaboration_Plan.md)
종류: 규칙 · 작성: 2026-09-18 claude · 적용: Claude Code, Codex, Gemini CLI, Antigravity, Cursor, 사용자

이 문서는 `AGENTS.md` 15절이 요약한 규칙의 전문이다. 규칙 ID는 `OP-nn`이고 `rg -n "OP-12"`로 인용처를 찾는다. 통독하지 말고 행위 직전에 해당 규칙만 `rg -n "^### OP-<nn>" -A 8 Docs/AgentRules.md`로 읽는다.

## 1. 등급

- [A] 매 세션 필수 9개: OP-04·05·06·07·08·09·11·26·27. `AGENTS.md` 15절의 9줄과 같은 내용이다.
- [B] 행위 조건부 23개: 도구 생성·문서 신설·증거 저장·설정 변경·커밋 직전에 해당 규칙만 읽는다.
- [C] 보류(5절): 동시 세션·기계 2대가 실제로 생길 때만 활성화한다.

## 2. 규칙 전문

### OP-01 규칙 정본은 두 파일뿐
[B] 규칙은 `AGENTS.md`(요약·대장 지도·식별자)와 이 문서(전문·절차)에만 둔다. `CLAUDE.md`·`GEMINI.md`는 포인터와 해당 벤더 전용 운용 절(다른 벤더에 영향 없는 것, 예: Claude 서브에이전트 라우팅)만, 하위 폴더에 `AGENTS.md`·`CLAUDE.md`·`GEMINI.md`·`.agents/rules`를 만들지 않는다. `Tools/README.md`·`Docs/Tasks/README.md`·`Docs/Validation/README.md`·`SKILL.md`는 절차와 표만 갖고 규칙은 OP-ID로 인용한다. 규칙의 추가·변경·삭제는 사용자 승인 뒤에만 하고, 에이전트는 `Docs/Tasks/decisions.md` '관리 체계' 절에 `(미정)` 제안만 남긴다.

### OP-02 AGENTS.md 예산과 작성 규칙
[B] `AGENTS.md`는 20KiB(20,480바이트) 이하(권고 18KiB), 절 번호 불변, 코드 스팬 밖의 `@경로`와 HTML 주석 금지. 고친 뒤 완료 보고에 바이트·줄 수·bare `@` 0건·`<!--` 0건을 적는다. 저장소 문서 안에서는 줄 번호 대신 절·항 번호로 인용한다(줄 번호는 완료 보고에만). 규칙 ID 접두어는 `OP-`만 쓰고 저장소가 이미 쓰는 접두어(G-·R-·P-·M-·A~G-·D-·MD-·AD-·L-)를 규칙 ID로 쓰지 않는다.

### OP-03 전역 지침 충돌
[B] 사용자 전역 지침(`~/.codex/AGENTS.md`, `~/.gemini/GEMINI.md`)이 이 저장소 `AGENTS.md`와 모순되면 `Docs/Tasks/decisions.md` D-12·D-13이 근거다. 결정 전에는 첫 세션에서 사용자에게 어느 규칙을 따를지 묻고 답을 Worklog에 남긴다. 전역 파일은 사용자만 고친다.

### OP-04 세션 시작
[A] 3절 '시작 7단계'를 따른다. 요약: `AGENTS.md`(자동) → `Docs/NOW.md` → `Docs/Worklog/<이달>.md` 마지막 5항목 → 맡을 영역 대장의 doing·blocked·decision → (도구 필요 시) `Tools/README.md` 0절 → `Docs/Lessons` 검색. 계획서·연구·대장·규칙 문서의 전문 통독은 하지 않으며, 설계·연구 문서는 착수 항목 '참조' 칸이 지목한 절만 읽는다.

### OP-05 세션 종료
[A] 3절 '종료 8단계'를 따른다. 필수 네 가지: ① 대장 갱신 ② Worklog 항목 추가 ③ `Docs/NOW.md` 재작성 ④ 완료 보고 2줄. 질문만 한 세션은 ①③ 생략 가능, ②는 `검색` 항목 1개로 축약 가능, ④는 필수. 에디터 상태는 PIE 종료와 미저장 임시 변경 보고만 하고 액터를 삭제하지 않는다(`TDGen_` 라벨은 정식 생성물).

### OP-06 완료 보고 고정 2줄
[A] 첫 줄 `읽음: NOW(<헤더 날짜>) / Worklog 마지막 <항목ID> / 대장 <파일>`, 마지막 줄 `기록 갱신: 대장 ✓|– / Worklog <항목ID> / NOW ✓|– / Lessons <L-ID|–> / 등록 <도구|–>`. 같은 `읽음:` 줄을 Worklog 항목 안에도 적어 파일로 남긴다. 항목ID는 Worklog 헤더의 `[YYYY-MM-DD HH:MM] <agent>`(같은 분 두 번째는 `HH:MMb`). 전역 출력 형식이 강제되는 에이전트는 '설명' 절 맨 앞과 맨 뒤에 넣는다(D-14).

### OP-07 Docs/NOW.md
[A] 헤더 `갱신: YYYY-MM-DD HH:MM <agent>` + ① 진행 중(ID·담당·청구 날짜·경과일) ② 막힘·결정 대기(`경로#ID` 한 줄씩, 관리 체계 미결은 `→ Docs/Tasks/decisions.md '관리 체계'` 한 줄로 묶음, 기록 누락 의심 줄) ③ 다음 행동 3개(명령·파일까지). 40줄·2,000자 이하, 세션 종료 시 덮어쓴다. 재작성 직전 다시 읽어 남의 진행 중 항목을 보존한다. 헤더가 3일 이상 지났으면 대장 doing과 `git log -5 --oneline`으로 교차 확인한 뒤 갱신한다.

### OP-08 Docs/Worklog/YYYY-MM.md
[A] 항목 = `## [YYYY-MM-DD HH:MM] <agent> | <작업ID 또는 -> | <시작|완료|막힘|결정|교훈|도구|검색> | 제목` + 6줄·500자 이하(`읽음:` 줄, 변경 파일, 검증 명령·결과, 증거 경로, `검색:`/`적중: L-ID`/`추가: L-ID`, 다음 세션 주의). 파일 끝에만 추가하고 기존 항목은 수정·삭제하지 않는다. 월초에 이달 파일이 없거나 5항목 미만이면 전월 파일 꼬리를 이어 읽는다. Lessons 적중은 `적중: L-ID` 줄이 유일한 기록·집계 지점이다.

### OP-09 기록 누락 검출
[A] 시작 때 NOW 헤더 날짜가 Worklog 마지막 항목보다 오래되면, 또는 마지막 5항목 중 `시작` 뒤에 같은 agent·같은 작업ID의 `완료|막힘|결정`이 없으면(중단 세션), NOW ②절에 `기록 누락 의심: <항목ID>` 1줄과 자기 Worklog 항목 주의 줄에 같은 문구를 남긴다. 중단 세션의 담당 필드는 사용자 확인 뒤 비운다. 대장 담당 날짜는 청구 날짜이므로 누락 판정에 쓰지 않는다.

### OP-10 에이전트 식별자
[B] `claude | codex | gemini | antigravity | cursor | user` 여섯 개(소문자)를 NOW 헤더·Worklog·대장 담당·기록 칸·Lessons 발견·Validation 제목줄·커밋 접두어에 같은 어휘로 쓴다. 하위 에이전트는 부모와 같은 식별자를 쓰되 장부를 직접 쓰지 않는다. VS Code Copilot 사용 시 `copilot` 추가는 D-30.

### OP-11 작업 청구
[A] 대장 항목을 `상태: doing` + `- 담당: <agent>/<YYYY-MM-DD>`로 바꾸고 Worklog `시작` 항목을 쓰는 것이 청구다. 담당이 비어 있을 때만 청구하고 에이전트당 동시 doing은 1개. 담당 날짜 7일 초과 doing 회수·자원 잠금은 5절 보류 규칙이며, 그전에는 NOW ①절의 경과일 표기만 한다.

### OP-12 만들기 전 검색
[B] 새 스크립트·함수·툴셋을 만들기 전에 `rg -il "<동사>|<대상 명사>" Tools Content/Python Source/TDGameEditor --glob '!Tools/scratch/**' --glob '!**/__pycache__/**'`와 `rg -n "<키워드>" --glob 'README.md' --glob 'SKILL.md' Tools`를 실행하고, 결과를 Worklog `도구` 항목 `검색:` 줄에 인용한다. 동의어(create→make, bake→build, verify/test→validate/check, screenshot→capture)도 검색한다. 부분 일치가 있으면 그 도구에 옵션을 추가하고 새 파일을 만들지 않는다.

### OP-13 도구 위치
[B] 실행 도구는 `Tools/<영역>/` 또는 `Tools/` 루트(범용 도구·공용 라이브러리, 등록 필수)에만 둔다. `.gemini/ .codex/ .claude/ .cursor/ .vscode/ .agents/`와 사용자 홈에는 설정·스킬 스텁만, `Content/Python`에는 `register()/unregister()`를 가진 상주 모듈만(`init_unreal.py`는 register 호출만 하는 예외). 채택 전 위반(`.gemini/scripts` 13개, `Tools/Animation` 11개)은 2026-09-18 `Tools/_archive/2026-09/`로 보관했고 Claude 홈 tools는 `check_automation_tests.py`만 이관했다(D-15~D-17). `Tools/_archive/`의 도구는 실행하지 않는다(OP-21).

### OP-14 도구 이름
[B] 새 스크립트는 `<editor_|pie_|blender_|없음><동사>_<대상>.py`(PowerShell은 `동사-명사.ps1`). 접두어: `editor_` 에디터 안 파이썬(run_in_editor.py 경유), `pie_` PIE 검사, `blender_` Blender 안 bpy, 없음 = 시스템 파이썬 3.12. 금지 동의어 4개: create→make, bake→build, verify/test→validate/check. 권장 동사는 `Tools/README.md` 4절. 라이브러리 모듈(`uemcp.py` 등 import 전용)은 동사 규칙 대상이 아니다. 기존 파일은 개명하지 않고 검사 스크립트 도입 시 allowlist로 점진 축소한다(D-21).

### OP-15 docstring 4줄
[B] 모든 파이썬·PowerShell 도구의 첫 docstring은 `<환경>에서 실행: 목적` / `실행: 명령(셸 명시)` / `출력: 경로` / `상태: 현행|실험|보류|폐기(대체: 경로)` 4줄로 시작한다(`AGENTS.md` 5절의 명시 예외). 기존 파일은 수정할 때 채운다. 템플릿: `Tools/templates/editor_tool_template.py`.

### OP-16 등록
[B] 도구·SKILL·C++ 에디터 함수·상주 툴셋을 만들거나 상태를 바꾸면 같은 세션에 `Tools/README.md` 해당 절 표에 1행을 추가·갱신한다(스킬 폴더는 `SKILL.md` 스크립트 표에도). 등록되지 않은 도구는 존재하지 않는 것으로 간주한다.

### OP-17 실험·일회용 스크립트
[B] 실험·1회용·후보 버전 복제 스크립트는 `Tools/scratch/<YYYYMMDD>_<agent>_<주제>/`(git 제외)에만 둔다. 두 번째로 필요해지면 OP-14~OP-16대로 승격한다. 30일 지난 scratch는 만료 후보로 보고만 하고 삭제는 사용자 지시 때 한다.

### OP-18 산출물 위치
[B] 중간물·리포트·캡처 원본은 `Saved/<영역>/`, 검증 증거만 `Docs/Validation/`, 애니메이션 원본은 `AnimationSources/`. `Tools/` 안에 실행 결과·`__pycache__`를 커밋하지 않는다. 파일을 이동하면 같은 변경에서 그 경로를 가리키는 문서·대장·메모리 참조를 `rg`로 찾아 모두 고친다. 에셋·원본의 버전 복제(`<이름>_v<nn>`)는 현행 1개만 대장 산출물 칸에 적고, 옛 버전 삭제·이동은 사용자 승인 뒤(D-29).

### OP-19 경로
[B] 경로는 에디터 안에서 `unreal.SystemLibrary.get_project_directory()`, 밖에서 `Path(__file__).resolve().parents[n]`로 계산한다. `C:\Project\TDGame`·`C:\Users\<사용자>`·엔진·LLVM·Blender 실행 파일 절대 경로는 새 코드·새 문서에 쓰지 않는다(사용자 홈은 `~/` 표기 허용). 필요한 절대 경로는 `Tools/ue_editor.py` 상단 상수 한 곳에만 둔다.

### OP-20 언리얼 기능 노출 경로
[B] ① 에디터 파이썬 API가 있으면 `editor_*.py` ② 없으면 `Source/TDGameEditor`의 C++ `UBlueprintFunctionLibrary` ③ 다른 에이전트가 MCP 도구로 불러야 하면 C++ `UToolsetDefinition`(AICallable) ④ 에디터 상주 툴셋은 `Content/Python/td_<영역>_tools.py`. 선택 이유를 docstring 1행에 적는다.

### OP-21 폐기 표기
[B] 대체되거나 현 정책과 모순되는 도구는 `Tools/README.md` 상태 열에 `폐기 후보 · 실행 금지 · 대체: <경로>`로 표기한다. 이동은 사용자 결정 뒤 `Tools/_archive/<YYYY-MM>/`로 하고 `_archive/README.md`에 한 줄(원래 경로|이유|대체|날짜)을 남긴다.

### OP-22 스킬
[B] 스킬 정본은 `Tools/<영역>/SKILL.md`(name=소문자-하이픈, description 1,024자 이하·권장 200자, 본문 500줄 이하, '실행할 스크립트' 표와 '읽기용 참고' 절 분리). 발견용 스텁은 `.agents/skills/<name>/`(Codex·Gemini CLI·Antigravity·Cursor)과 `.claude/skills/<name>/`(Claude Code)에 같은 name·description + 정본 포인터로 둔다. 정본을 고치면 스텁 frontmatter를 같은 변경에서 복사한다. 사용자 홈(`~/.codex/skills` 등)에는 복사하지 않는다.

### OP-23 문서 신설
[B] 새 Docs 문서는 1행 `[← 인덱스로](상위 인덱스 경로)` + `종류: 계획|설계|리서치|가이드|대장|결정|외부참고|규칙` + `작성: 날짜·<agent>` 머리를 갖고, 정확히 1곳(묶음은 해당 `Docs/<주제>_Plan.md`, 단독은 `Docs/README.md`(신설 전까지 `AgentCollaboration_Plan.md` 5절))에 한 줄로 등록한다. 출처 없는 외부·생성 문서는 `외부참고 · 설계 기준 아님`으로 표시하고 결정 근거로 인용하지 않는다.

### OP-24 검증 증거
[B] 새 증거는 `Docs/Validation/<영역>/<작업ID 또는 L-ID>-<설명>-<YYYY-MM-DD>.<md|png|jpg|gif|json>`(하이픈, ID 앞, 날짜 뒤). 대장 항목이 경로를 지정했으면 그 경로가 우선하고, 몬스터 AI는 MD-05 결정 전까지 07 §6 5항의 `Docs/MonsterAI_CombatSim/measurements/`. md 증거는 제목줄 `# <ID> <제목> (<날짜>, <agent>)` + `교훈:`·`명령:` 2줄. png·jpg·gif는 LFS, 5MB 초과 gif는 `Saved/`에 두고 md에 수치만. 기존 파일은 개명하지 않는다.

### OP-25 수정 범위
[B] 무범위 '문서 전체 정리'는 하지 않으며 한 세션의 작업 대상 문서 수정은 5개 파일 이하다. 종료 절차가 요구하는 장부 파일(대장·Worklog·NOW·Lessons·`Tools/README.md`·SKILL 표·Validation 증거)은 이 수에 넣지 않고, 사용자가 지정한 이관 작업은 명시 예외다.

### OP-26 교훈 기록
[A] 해결에 30분 또는 1만 토큰 이상 들었거나, 엔진·도구 공식 문서에 없거나, 두 번째로 마주친 문제는 같은 세션에 `Docs/Lessons/<영역>.md`에 `### L-<영역>-<번호>` 항목을 쓴다(형식은 3절 A). 증거·재현이 없으면 `미검증`. 코드에서 도출 가능한 것·엔진 문서에 있는 것·1회용 우회책은 기록하지 않는다. `AGENTS.md` 15절로의 승격은 Worklog `적중:` 2회 이상(다른 세션·에이전트) 또는 비가역 손실 방지 항목만 사용자 승인 후 `(L-ID)` 역링크와 함께 6줄 이내(D-31). 반박되면 같은 변경에서 줄 삭제와 `superseded-by` 처리를 한다.

### OP-27 실패 시 교훈 먼저
[A] 명령·도구 호출이 실패하거나 예상 밖 출력이 나오면 플래그만 바꿔 재실행하기 전에 `rg -n "<오류 핵심 문구>" Docs/Lessons Tools/README.md`를 먼저 실행한다. 적중하면 그 해결을 따르고 Worklog에 `적중: L-ID`를 남기며, 없으면 관련 소스·문서를 읽는다.

### OP-28 결정 기록
[B] 결정은 항상 `경로#ID`로 인용하고 기존 번호(D-01~, D1~, MD-xx)는 재번호하지 않는다. 새 항목은 각 파일의 연번을 잇고 `상태: proposed|accepted|superseded-by <경로#ID>|deprecated`와 날짜 줄을 둔다. 에이전트는 `(미정)` 제안(질문/선택지/권장/영향)만 추가하고 결정은 사용자가 한다. 미결 결정을 선점하는 문서 수정은 하지 않는다.

### OP-29 할 일 대장 3곳
[B] 대장 3곳(P/M/A~G)은 파일을 합치지 않고, 상태 5값 소문자 평문·`- 담당:` 필드·`- 참조:` 칸·기록 칸 `YYYY-MM-DD 동사(<agent>): 내용`만 통일한다. 맨 위 상태표는 `python Tools/tasks_recount.py`가 생성·갱신하는 것이 정본이라 손으로 열을 바꾸지 않는다. 세 대장의 doing·blocked·decision은 NOW ①②절에 ID·담당·날짜로만 요약한다.

### OP-30 벤더 메모리
[B] Claude 자동 메모리·Codex memories·Antigravity knowledge·Serena 메모리에는 사용자 선호·절차 교정과 `L-ID` 포인터만 두고 프로젝트 실측 사실·명령·함정을 저장하지 않는다. 다른 에이전트가 알아야 할 사실은 `Docs/Lessons/`에만 쓴다.

### OP-31 MCP 설정 동기화
[B] MCP(Model Context Protocol, 모델 컨텍스트 프로토콜) 서버 주소·포트·Blender 런처를 바꾸면 같은 변경에서 서버 `Config/DefaultEditorPerProjectUserSettings.ini`와 클라이언트 5벌(`.mcp.json`·`.codex/config.toml`·`.cursor/mcp.json`·`.gemini/settings.json`·`.agents/mcp_config.json`)을 함께 고친다(`.vscode/mcp.json`은 Copilot 미사용 가정으로 제외, D-30). 사용자 홈 `~/.gemini/config/mcp_config.json`(Antigravity `~/.gemini/antigravity/mcp_config.json`은 이 파일의 심볼릭 링크)은 저장소 런처 방식과 같게 유지하며 변경은 완료 보고에 적는다(D-19·D-36). Blender MCP 서버는 `Tools/BlenderMCP/Run-BlenderMCP.ps1` 외의 방법(`uvx blender-mcp`·`mcp-for-blender`·pip 설치본)으로 띄우지 않는다.

### OP-32 커밋
[B] 커밋은 사용자 지시 때만 하고, 메시지 첫 줄은 `[<agent>] <작업ID 또는 -> <무엇을·왜>`, 벤더 기본 트레일러는 그대로 둔다. 커밋 전 `git status`에 `__pycache__`·`Tools/scratch`·`Saved`·의도치 않은 `.uasset`이 없는지 본다.

## 3. 절차 전문 (유일한 사본)

### 시작 7단계
0. (자동) 벤더가 `AGENTS.md`를 로드한다. Claude Code는 `CLAUDE.md`의 `@AGENTS.md` 임포트, Codex·Cursor·Antigravity는 루트 `AGENTS.md` 네이티브, Gemini CLI는 `.gemini/settings.json`의 `context.fileName`.
1. `Docs/NOW.md` 전체를 읽는다(≤2,000자).
2. `Docs/Worklog/<이달>.md` 마지막 5항목을 읽는다(PowerShell: `rg -n '^## \[' Docs/Worklog/<이달>.md | Select-Object -Last 5`로 줄 번호를 얻어 그 줄부터). 이달 파일이 없거나 5항목 미만이면 전월 파일 꼬리를 이어 5항목. OP-09 검출을 여기서 한다.
3. `git status`로 사용자 변경을 확인·보존한다. 사용자 변경 파일이 작업 대상과 겹치면 착수 전에 묻고, 겹치지 않으면 Worklog 시작 항목에 `사용자 변경: <파일 n개>`만 적는다. 사용자 지시가 영역을 정한다.
4. 영역이 정해지면 `AGENTS.md` 15절 표의 행을 따라 `rg -n "^- 상태: (doing|blocked|decision)" -B 4 --glob 'phase-*.md' Docs/Tasks`(몬스터 AI·애니메이션은 해당 파일을 인자로)로 ID·제목만 얻고, 후보 1~2개를 `rg -n -A 18 "^### <ID>" <대장 파일>`로 본문만 읽는다(기록 칸이 마지막 행). 항목 1개를 OP-11대로 청구한다. 항목의 '참조' 칸이 지목한 절만 부분 읽기(≤3,000자); 참조 칸이 없으면 해당 영역 인덱스 문서의 표에서 찾는다. 코드 폴더 대응: 월드·던전 `Source/TDGame/World`·`Source/TDGameEditor/{Landscape,WorldGen}`·`Source/TDWorldGen`·`Tools/{WorldGen,DungeonGen}`, 몬스터 AI·전투 `Source/TDGame/{AI,Combat,Performance}`, 애니메이션 `Source/TDGameEditor/Animation`·`Tools/{AnimationAuthoring,BlenderAnimation,BlenderMCP}`·`Content/Python`·`AnimationSources/`, 공통 `Tools/` 루트·`Source/TDGame/{Core,Framework}`.
5. 도구·에디터가 필요할 때만 `Tools/README.md` 0절을 따라 `python Tools/ue_editor.py ensure`를 실행하고 해당 영역 절(또는 스킬 정본 `SKILL.md`)만 읽는다. 환경 문제가 있을 때만 0b절.
6. `rg -n "<영역>|<핵심 키워드>" Docs/Lessons`로 적중 행만 보고 필요한 항목만 `rg -n -A 12 "^### L-<id>" Docs/Lessons/<영역>.md`로 읽는다. 조건부 규칙이 걸리는 행위 직전에 해당 OP만 읽는다. 결정 대기 ID가 NOW ②절에 있으면 그 항목만 `경로#ID`로 연다.
7. Worklog `시작` 항목의 `읽음:` 줄을 먼저 쓰고 작업 범위·성공 조건을 2~3줄로 정한 뒤 시작한다. 예외: 사용자가 '어디까지 왔는지'만 물으면 1~2단계만 읽고 `Docs/NOW.md`를 그대로 답한다.

### 종료 8단계
1. 대장 갱신(OP-11·OP-29): 상태(완료 조건 전부 충족 시만 `done` / 미완은 `doing` 유지 + 남은 조건 / 세션이 끊길 것 같으면 `todo`로 되돌리고 남은 조건 명시 / 결정 필요는 `decision` + 결정 파일에 5필드 `(미정)` 항목), 기록 칸 `YYYY-MM-DD 동사(<agent>): 내용, 검증 결과, 증거 경로`, 담당 필드(`done`이면 비움). 상단 상태표는 `python Tools/tasks_recount.py`로 갱신한다. 범위 밖 발견은 같은 대장 끝에 `todo`로.
2. Worklog 추가(OP-08): 파일이 없으면 만든다. 기존 항목은 손대지 않는다.
3. Lessons 기록(OP-26, 해당 시): 새 함정·엔진 사실은 `Docs/Lessons/<영역>.md` 끝에 항목. 기존 항목을 확인만 했으면 Worklog `적중:`만. 뒤집었으면 `superseded-by` 처리와 `AGENTS.md` 15절 줄 삭제(있다면)를 같은 변경에서.
4. 도구 등록(OP-15·OP-16, 해당 시): docstring 4줄 확인 후 `Tools/README.md` 해당 절 표 1행(+스킬 폴더면 `SKILL.md` 표). 실험 파일은 `Tools/scratch/`로. 대체한 도구는 상태 `폐기 후보(대체: 경로)`.
5. 증거·산출물(OP-18·OP-24): 증거는 대장이 지정한 경로 또는 `Docs/Validation/<영역>/…`, 중간물은 `Saved/`. `git status`로 `__pycache__`·Tools 안 결과물·의도치 않은 `.uasset`이 없는지 본다. 새 문서를 만들었으면 인덱스 1곳 등록과 머리 3줄(OP-23).
6. `Docs/NOW.md` 재작성(OP-07): 다시 읽는다 → 남의 진행 중 항목 보존 → 헤더 → ①②③ → 40줄 이내로 덮어쓴다. 에디터·Blender는 PIE 종료·미저장 임시 변경 보고만, 액터 삭제 없음. 에디터를 닫아야 하면 미저장 에셋 여부를 먼저 알린다.
7. 완료 보고(`AGENTS.md` 10절 + OP-06): 첫 줄 `읽음:`, 본문(변경한 파일은 장부 파일과 작업 대상 문서를 구분·구현·검증·남은 모호성, 도구를 만든 경우 `검색:` 결과, `AGENTS.md`를 고쳤으면 OP-02 실측치), 마지막 줄 `기록 갱신:`.
8. 커밋은 하지 않는다(사용자 지시 시만, OP-32). 벤더 메모리에는 사용자 선호·절차 교정과 L-ID 포인터만(OP-30). 저장소에 남는 검출 수단은 Worklog `읽음:` 줄과 NOW 헤더뿐이며 다음 세션이 OP-09·OP-07로 대조한다.

### 도구 생성 9단계
1. 검색(OP-12). 필요 시 `python Tools/uemcp.py list`도. 부분 일치면 옵션 추가로 끝낸다.
2. 노출 경로 선택(OP-20): `python Tools/run_in_editor.py -c "import unreal; print([n for n in dir(unreal) if '<키워드>' in n][:20])"`로 API 유무 확인 → `editor_*.py` → C++ `UBlueprintFunctionLibrary`(`Tools/templates/cpp_editor_function_template.md`, `ue_editor.py restart`) → MCP 직접 호출은 C++ `UToolsetDefinition` 또는 `Content/Python/td_<영역>_tools.py` → Blender 안 작업은 `blender_*.py`(`Tools/BlenderMCP/call_tool.py --code`). 오프라인 계산은 시스템 파이썬 3.12(에디터 내장 3.11과 다름).
3. 실험 단계(OP-17): 확신이 없거나 1회용이면 `Tools/scratch/<YYYYMMDD>_<agent>_<주제>/`에서 만들고 돌린다.
4. 위치·이름(OP-13·OP-14): `Tools/<영역>/<접두어><동사>_<대상>.py` 또는 범용이면 `Tools/` 루트. 영역 폴더가 없으면 만들고 `README.md`(절차가 3단계 이상이면 `SKILL.md` + 스텁 2쌍) 하나를 둔다.
5. 머리·경로(OP-15·OP-19): 템플릿에서 시작, docstring 4줄, 경로 계산, 인수는 argparse. 오프라인 생성기는 `numpy.random.default_rng([seed, 단계, 재시도])`·`generator_version`·종료 코드 0/1/2. 에디터 생성물은 `TDGen_` 라벨 멱등 재생성(레벨 재생성 스크립트만 `TDGen_*` 전체를 지우고 다시 만든다). 기존 에셋 덮어쓰기 금지.
6. 검증·증거(OP-18·OP-24): 최소 1회 실제 실행 결과(종료 코드·출력 경로)를 `Saved/<영역>/`에 남기고 docstring `출력:` 줄과 일치시킨다.
7. 등록(OP-16): 같은 세션에 `Tools/README.md` 표 1행 + Worklog `도구` 항목 + 완료 보고 `등록 <도구>`. 대체한 도구는 OP-21.
8. 함정 기록(OP-26): 만드는 과정에서 확인한 API 부재·엔진 제약은 `Docs/Lessons/<영역>.md` 항목으로, docstring에는 L-ID만.
9. 스킬(OP-22): 도구 묶음에 절차가 필요하면 `Tools/<영역>/SKILL.md` 정본 + `.agents/skills/<name>/`·`.claude/skills/<name>/` 스텁.

### 지식 기록 A~H
- A. 교훈 항목 형식(`Docs/Lessons/<영역>.md`, 영역 코드는 `AGENTS.md` 15절 표): `### L-<영역>-<nn> 제목` / `- 증상:` 오류 문구 원문 / `- 원인:` / `- 해결:` 명령 한 줄 또는 스크립트 경로 / `- 재현:`(없음 허용) / `- 범위:` UE 5.8.x·경로·도구·셸·에이전트·대장 ID / `- 증거:` `Docs/Validation` 경로·커밋, 없으면 `미검증` / `- 날짜·상태:` `YYYY-MM-DD active|superseded-by L-x|deprecated|미검증` / `- 발견:` `<agent>`. 필수 4필드: 증상·해결·범위·날짜. 번호 재사용 금지, 옛 항목은 지우지 않고 `superseded-by`. 색인은 `rg -n "^### L-" --glob '*.md' Docs/Lessons`.
- B. 사후 분석(`Docs/Lessons/postmortems/YYYY-MM-DD-<슬러그>.md`)은 데이터·에셋 손실, 1세션 이상 낭비, 에디터·레벨 상태 파괴 세 경우만(무엇이·왜·영향·복구·재발 방지·증거·날짜 7필드).
- C. 결정 기록: `Docs/Tasks/decisions.md`(D-xx, '관리 체계' 절 포함), `Docs/MonsterAI_CombatSim/00-decision-record.md`(D1~), 07 §4(MD-xx), `Docs/AnimationAuthoring_Tasks.md` D절(AD- 개명 예정)을 옮기지 않는다. 규약은 OP-28.
- D. 진행 상태: 정본은 세 대장의 상태·담당·참조·기록 필드와 상단 상태표(OP-29). 집계본은 `Docs/NOW.md`, 이력은 `Docs/Worklog/`. '어디까지 왔는지'는 NOW 한 파일로 답한다.
- E. 추적: `rg -n "P3-07" Docs/NOW.md Docs/Worklog Docs/Lessons Docs/Validation`으로 한 작업의 세션 이력·교훈·증거를 찾는다. 커밋 접두어 `[<agent>]`는 보조 신호.
- F. 승격·강등: `rg -c "적중: L-<id>" Docs/Worklog`로 2회 이상(다른 세션·에이전트) → 사용자 승인 후 `AGENTS.md` 15절 핵심 함정에 한 줄 + `(L-ID)`. 반박되면 같은 변경에서 삭제 + `superseded`. 적중 필드·별도 집계 파일을 만들지 않는다.
- G. 벤더 메모리(OP-30): Claude 자동 메모리의 프로젝트 사실은 Lessons로 옮기고 파일은 `L-ID 참조 + 한 줄`로 축소한다.
- H. 문서 등록·낡음(OP-23): 손 유지 표(`Tools/README.md` 0b절·3c절, 인덱스 문서)에는 `확인: YYYY-MM-DD <agent>` 한 줄을 두어 낡음을 눈으로 보이게 한다.

## 4. 셸·인코딩 주의

- PowerShell은 네이티브 실행 파일에 넘기는 와일드카드를 확장하지 않는다. `rg ... Docs/Tasks/phase-*.md`는 `os error 123`으로 실패하므로 `rg ... --glob 'phase-*.md' Docs/Tasks`처럼 디렉터리 + `--glob`을 쓴다(L-repo-02).
- Git Bash는 `/Game/...` 인수를 Windows 경로로 바꾼다. 언리얼 에셋 경로를 넘기는 명령은 PowerShell에서 실행한다(L-repo-01).
- 저장소 텍스트 파일은 UTF-8·BOM 없음이다. 장부 파일(NOW·Worklog·Lessons·대장)은 파일 편집 도구 또는 파이썬(`open(..., encoding="utf-8")`)으로 쓰고, PowerShell 5.1의 `Add-Content`/`Set-Content`/`>`는 쓰지 않는다(CP949·BOM이 섞인다, L-repo-03). 줄 끝은 `.gitattributes`로 LF 통일을 채택했으나(D-24) 재정규화 커밋 전까지는 미적용이므로 비교는 줄 끝 정규화 후 한다.
- bash 히어독과 파이썬 인라인 문자열에 Windows 경로(`\U`, `\P` 등)를 넣으면 이스케이프 오류가 난다. 긴 본문·경로가 든 스크립트는 파일로 쓴 뒤 실행한다(L-repo-07).

## 5. 보류 규칙 [C]

2026-09-18 D-32: 동시 세션 운용을 하지 않기로 결정. 아래 규칙은 보류 상태를 유지하며, 동시 운용을 시작할 때 `AgentCollaboration/05-concurrency-and-handoff.md` 4절 안으로 활성화한다.

- 동시 doing 회수: 담당 날짜 7일 초과 doing은 누구나 회수(회수 사실을 Worklog에 기록). 활성 조건: 동시 세션 또는 기계 2대(D-25). 소유자: 활성 조건을 처음 관측한 세션의 에이전트가 NOW ②절에 제안.
- 단일 자원 잠금(언리얼 에디터 MCP 8000, Blender MCP 9876): 활성 전에는 NOW에 자원 줄을 두지 않는다. 활성 시 `Saved/Locks/<자원>.lock/` mkdir 원자 잠금 + 10분 stale 회수(`Docs/Automation_Backlog.md` 11번).

## 6. 사용자 결정 대기

`Docs/Tasks/decisions.md` '관리 체계' 절 D-12~D-37은 2026-09-18 결정·적용됐다. 새 미결 항목은 같은 절 끝에 연번으로 추가하고, 결정 전에는 해당 항목을 선점하는 변경을 하지 않는다.

## 7. 읽기 예산(추정치, 드라이런 뒤 실측으로 갱신)

| 구간 | 내용 | 예산(문자) |
|---|---|---|
| 매 세션 고정 | `AGENTS.md`(≈10,700) + 진입 파일(≤450) + `Docs/NOW.md`(≤2,000) + Worklog 5항목(≤2,500) + 스킬 메타(≤600) | ≈16,000 |
| 영역 진입 | 대장 doing 목록(≤3,000) + 항목 본문 2개(≤3,000) + 참조 절(≤3,000) + `Tools/README.md` 0절·해당 절(≤2,600) + Lessons 적중(≤800) | ≈12,400 |
| 종료 쓰기 | NOW + Worklog 1항목 + 대장 기록 1~2줄 + 보고 2줄 | ≈1,000~2,500 |

확인: 2026-09-18 claude (추정치). 이 문서 실측 17,258자·181줄(2026-09-18). 동시 세션 관련 보강안은 `AgentCollaboration/05-concurrency-and-handoff.md` 4절, 채택은 D-32.

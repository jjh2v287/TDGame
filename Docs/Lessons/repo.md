# Lessons — repo (저장소·셸·에이전트 배선)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A.

### L-repo-01 Git Bash가 `/Game/...` 인수를 Windows 경로로 바꾼다
- 증상: `/Game/Characters/...`가 `C:/Program Files/Git/Game/Characters/...`로 바뀌어 도구가 경로 오류로 거부한다.
- 원인: MSYS 경로 변환.
- 해결: 언리얼 에셋 경로를 넘기는 명령은 PowerShell에서 실행한다.
- 범위: Windows Git Bash, Tools/AnimationAuthoring/*.py 등 에셋 경로 인수를 받는 모든 스크립트
- 증거: Docs/AnimationAuthoring_Tasks.md '실행 환경 주의'
- 날짜·상태: 2026-09-16 active
- 발견: claude

### L-repo-02 PowerShell에서 `rg` 경로 와일드카드가 실패한다
- 증상: `rg -n '...' Docs/Tasks/phase-*.md` → `rg: Docs/Tasks/phase-*.md: IO error ... (os error 123)`, 종료 코드 2.
- 원인: PowerShell은 네이티브 실행 파일 인수의 와일드카드를 확장하지 않는다.
- 해결: `rg -n '...' --glob 'phase-*.md' Docs/Tasks` (디렉터리 + `--glob`).
- 재현: 위 명령을 PowerShell 5.1에서 실행.
- 범위: Windows PowerShell 5.1, ripgrep
- 증거: 2026-09-18 반박 검증 세션에서 재현 — 미검증(증거 파일 없음)
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-repo-03 PowerShell 5.1의 `Add-Content`/`Set-Content`/`>`는 UTF-8을 깨뜨린다
- 증상: 한글 마크다운을 `Add-Content`로 쓰면 CP949, `>`·`Out-File -Encoding utf8`은 BOM이 붙고, `Get-Content`는 BOM 없는 UTF-8 한글을 잘못 읽어 줄 수가 달라진다.
- 해결: 장부 파일은 파일 편집 도구 또는 `python -c "open(p,'a',encoding='utf-8').write(...)"`로 쓴다. 읽을 때는 `Get-Content -Encoding UTF8` 또는 `rg`.
- 범위: Windows PowerShell 5.1(PowerShell 7은 기본 UTF-8)
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-repo-04 저장소 밖 경로를 `sys.path`에 하드코딩한 스크립트는 다른 에이전트에서 실패한다
- 증상: `Tools/WorldGen/capture_views_mcp.py`가 `C:\Users\jjh\.claude\...\tools`를 `sys.path`에 넣어 `import uemcp`를 했고, Codex·Gemini에서는 `ModuleNotFoundError`.
- 해결: `sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))`로 `Tools/`를 가리킨다(2026-09-18 수정). 규칙은 OP-19.
- 범위: Tools/**/*.py
- 증거: git diff Tools/WorldGen/capture_views_mcp.py (2026-09-18)
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-repo-05 에이전트별 규칙 파일 자동 로드 배선
- 증상: Claude Code는 `AGENTS.md`를 읽지 않고 `CLAUDE.md`만 읽는다. Gemini CLI는 설정 없이는 `AGENTS.md`를 읽지 않는다. Codex는 `~/.codex/AGENTS.md`(전역) 뒤에 프로젝트 `AGENTS.md`를 이어 붙인다(합계 32KiB 한도). Antigravity는 루트 `AGENTS.md`·`GEMINI.md`를 시작 시 파싱한다.
- 해결: 루트 `CLAUDE.md`에 `@AGENTS.md` 임포트(Windows는 심볼릭 링크 대신 임포트 권장), `.gemini/settings.json`에 `"context": {"fileName": ["AGENTS.md", "GEMINI.md"]}`. 전역 파일의 "AGENTS.md 참고 하지 않는다" 줄은 사용자가 지운다(D-12·D-13). 규칙 파일에서 코드 스팬 밖 `@경로`는 세 에이전트가 임포트로 해석하므로 백틱으로 감싼다.
- 범위: Claude Code(2026-09), Codex CLI 0.154.0, Gemini CLI 0.35.3, Antigravity 2.14.0 — 벤더 버전이 바뀌면 재확인
- 증거: Docs/AgentCollaboration/research/agent-wiring.md (공식 문서 URL·확인 날짜)
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-repo-06 벤더 메모리는 서로 보이지 않는다
- 증상: Claude 자동 메모리(`~/.claude/projects/...`)·Codex memories·Antigravity knowledge·Serena 메모리(`~/.serena/projects/TDGame/...`)는 각자 사용자 홈에 있어 다른 에이전트가 읽지 못한다.
- 해결: 다른 에이전트도 알아야 할 사실은 `Docs/Lessons/`에만 쓴다(OP-30).
- 범위: 2026-09 현재 세 벤더
- 증거: Docs/AgentCollaboration/research/shared-memory.md
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-repo-07 bash 히어독·파이썬 인라인 문자열에 Windows 경로를 넣으면 깨진다
- 증상: `python - <<'EOF'` 안의 `'C:\Users\...'` 문자열이 `SyntaxError: (unicode error) 'unicodeescape' codec can't decode bytes ... truncated \UXXXXXXXX escape`로 실패하고, 긴 한국어 히어독이 `unexpected EOF while looking for matching quote`로 실패한다.
- 원인: 파이썬 일반 문자열의 `\U`·`\P` 이스케이프, 셸 히어독의 인용 처리.
- 해결: 긴 본문과 경로가 든 스크립트는 파일 편집 도구로 `.py`/`.md` 파일에 먼저 쓰고 `python 파일.py`로 실행한다. 파이썬 안 Windows 경로는 `r'...'` 원시 문자열 또는 `pathlib`.
- 범위: Git Bash + Python 3.12, 에이전트 셸 도구 전반
- 증거: 미검증(2026-09-18 세션에서 4회 재현)
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-repo-08 대장 기록의 "구현·테스트 통과"만 보고 항목을 닫으면 완료 조건과 어긋난다 — 조건 문구와 한 줄씩 대조한다
- 증상: 2026-09-12에 "C++ 구현, 테스트 통과"로 기록된 채 doing으로 남은 월드·던전 7개(P2-02·04·05·06, P3-00·03·04)를 조건별로 대조하자 7개 모두 미충족 조건이 있었다(시드 1~200 vs 조건 1~500, BFS vs 조건 DFS, 주경로 비율 검사 누락, IsDataValid 테스트 없음, C++ 높이맵 생성기 대신 numpy 등).
- 해결: 항목당 감사자 1명이 완료 조건을 met/partial/unmet으로 판정(워크플로 `worldgen-a-items-audit`) → 작은 보완은 구현·테스트 후 닫고, 설계가 다른 것은 사용자 결정(decisions.md D-38~40)으로 조건 문구를 정정한 뒤 닫는다. 닫을 때 기록에 테스트 이름·수치를 남긴다.
- 범위: `Docs/Tasks/phase-*.md`, `Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md` 등 모든 대장
- 증거: `Saved/AgentOps/20260930/fix-dungeon.md`·`fix-world.md`, `Docs/Validation/P3-00-landscape.md`, 테스트 75/75(`Saved/Logs/AItemsFinal.log`)
- 날짜·상태: 2026-09-30 active
- 발견: claude

### L-repo-09 Windows PowerShell 5.1은 네이티브 프로그램 인수 안의 큰따옴표를 지운다 — JSON 인수는 파일로 넘긴다
- 증상: `python Tools/Cascadeur/cascadeur_bridge.py call ... --args '{"fbx_path": "..."}'`가 `json.decoder.JSONDecodeError`, `eval "exec(...)"`가 `SyntaxError: invalid syntax`(따옴표가 사라진 문자열). PowerShell `>` 리디렉션 결과 JSON은 `Unexpected UTF-8 BOM`으로 읽기 실패.
- 해결: JSON 인수는 `--args-file <파일>`로 넘기고 파일은 Python으로 만든다(`json.dump`). PowerShell 출력 파일은 `utf-8-sig`로 읽거나 `Out-File -Encoding utf8` 결과를 BOM 허용으로 읽는다(L-repo-03과 같은 계열).
- 범위: Windows PowerShell 5.1 → 시스템 Python 3.12 호출 전반
- 증거: 2026-10-08 세션 재현 2회, `cascadeur_bridge.py`의 `--args-file`
- 날짜·상태: 2026-10-08 active
- 발견: claude

### L-repo-10 Read 도구의 PDF 페이지 렌더가 pdftoppm 없음으로 실패하고, 파이썬 출력은 cp949 콘솔에서 깨진다
- 증상: Read 도구로 PDF를 `pages` 지정해 읽으면 `pdftoppm is not installed. Install poppler-utils`로 실패한다(페이지 지정 없이 읽으면 내용 없이 "PDF file read"만 나온다). 파이썬이 `µ`·`°` 등이 든 문자열을 출력하면 `UnicodeEncodeError: 'cp949' codec can't encode character '\xb5'`로 멈추고, 리디렉션 출력의 한글이 깨진다.
- 해결: PDF는 시스템 Python 3.12의 `pypdf`(`pdfminer`·`pdfplumber`도 설치됨)로 텍스트를 뽑아 파일로 저장한 뒤 읽는다(수식 글리프는 깨지므로 본문 설명으로 해석한다). 웹 PDF는 WebFetch가 본문을 변환하지 못해도 사본을 자동 저장하므로, 그 사본에서 pypdf로 텍스트만 뽑아 읽는다(따로 내려받지 않음, 2026-10-14 문헌 조사 하위 에이전트 3곳에서 재현). 출력은 `PYTHONIOENCODING=utf-8 python ...` 또는 파일에 `encoding="utf-8"`로 쓴다. 파이썬 문자열 안 `\2026…` 같은 백슬래시+숫자는 8진 이스케이프가 되어 경로가 깨진다(L-repo-07 계열) — 치환 스크립트는 파일로 쓰고 `chr()`로 문자를 만든다.
- 범위: 이 PC의 Claude Code Read 도구, Git Bash + 시스템 Python 3.12
- 증거: 2026-10-14 세션 재현(HZD 9장 PDF 11쪽 추출 24,391자, `Saved/AgentOps/20261014/hzd_ch09.txt`)
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-repo-11 파이썬 `open(path, 'w')`가 `OSError: [Errno 22] Invalid argument`로 간헐 실패한다
- 증상: 같은 세션에서 `Config/DefaultGame.ini`·`Tools/WorldGen/editor_build_open_world.py`·`Docs/Tasks/phase-3-outdoor-generator.md` 쓰기가 한 번씩 실패하고, 잠시 뒤 같은 쓰기는 성공했다. 읽기는 정상이다.
- 원인: 미검증. 에디터·다른 세션·백신 같은 다른 프로세스가 파일을 잠깐 잡는 것으로 추정한다(실패는 에디터가 켜져 있거나 다른 세션이 작업 중일 때만 났다).
- 해결: 같은 폴더의 임시 파일에 쓴 뒤 `os.replace(tmp, path)`로 바꾸고, 실패하면 2초 간격으로 몇 번 재시도한다. 열기 단계에서 실패하면 원본은 잘리지 않는다(크기 확인함).
- 범위: Windows 10, 시스템 Python 3.12, 저장소 안 텍스트 파일
- 증거: 2026-10-14 claude 세션(P3-15) 도구 출력 4회
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-repo-12 PowerShell 5.1에서 따옴표 없는 `-log=이름.log`가 `-log=이름`·`.log` 두 인수로 쪼개져 로그가 TDGame.log로 간다
- 증상: `& UnrealEditor-Cmd.exe ... -log=M325_Before.log`로 헤드리스 시험을 돌렸는데 `Saved/Logs/M325_Before.log`가 생기지 않고 결과가 `Saved/Logs/TDGame.log`에 남았다.
- 원인: Windows PowerShell 5.1이 `-`로 시작하고 `.`을 포함한 따옴표 없는 네이티브 인수를 마침표 앞에서 나눠 넘긴다(PowerShell 알려진 동작).
- 해결: 인수 전체를 따옴표로 감싼다: `"-log=M325_After.log"`. 또는 Python `subprocess`에 인수 목록으로 넘긴다. L-editor-08의 헤드리스 명령도 PowerShell에서는 이렇게 감싼다.
- 범위: Windows PowerShell 5.1 → UnrealEditor-Cmd 등 네이티브 프로그램 인수(`-key=값.확장자` 꼴)
- 증거: 2026-10-14 claude 세션(M3-25) 수정 전 실행이 TDGame.log로 감, 따옴표를 붙인 수정 후 실행은 M325_After.log 생성
- 날짜·상태: 2026-10-14 active
- 발견: claude

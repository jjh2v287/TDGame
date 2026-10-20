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

- 재발(2026-10-19): `python Tools/FluidFX/pie_lookdev_fluid.py --level /Game/FluidFX/Maps/LV_TDFluidWaterStage` 를 Git Bash 에서 실행하자 `레벨을 열지 못했다: {"exists": false, ...}` — PowerShell 로 다시 실행해 해결
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

### L-repo-13 Fab 브라우저의 AX 요소를 DOM 로케이터로 찾지 못할 때
- 증상 원문: `Playwright selector deadline exceeded` / `Locator diagnostics: {"kind":"no_matches"}`. Fab 카테고리의 AX row와 텍스트를 DOM 로케이터로 찾는 두 시도가 실패했다.
- 해결: 최신 `getAXState()`의 요소 번호로 `tab.click(번호)`를 실행해 카테고리를 바꾼 뒤 상태를 읽었다. 목록 이름·링크 읽기는 DOM 평가로 가능했다. 실패 원인은 미확인이다.
- 범위: 이번 Codex In-app Browser Fab 세션. 다른 브라우저·페이지 일반화는 미검증.
- 증거: 2026-10-15 Fab 조회 로그의 두 no_matches 및 AX 클릭 뒤 카테고리 URL·목록 변경.
- 날짜·상태: 2026-10-15 active
- 발견: codex

### L-repo-14 백그라운드 Bash 작업을 TaskStop해도 그 안에서 띄운 `bash 스크립트.sh`는 살아남아 계속 실행된다
- 증상: 대기 스크립트를 TaskStop으로 멈추고(`Successfully stopped task`) 새 대기 스크립트를 띄웠더니, 조건이 맞는 순간 두 개가 함께 `cook_single_map.py`를 시작했다. 늦은 쪽은 `A conflicting instance of AutomationTool is already running`으로 실패하고 공유 노트에 '실패'를 남겼지만, 멈춘 줄 알았던 쪽의 쿠킹은 계속 돌았다.
- 원인: TaskStop은 감싼 셸만 끝내고 자식 `bash.exe`(별도 프로세스)를 끝내지 않는다(Windows Git Bash). 고아가 된 스크립트는 수정 전 내용으로 계속 돈다. Monitor의 `tail -f | grep` 자식도 같이 남는다.
- 해결: 멈춘 뒤 `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match '<스크립트 이름>' }`으로 남은 프로세스를 찾아 내 것만 `taskkill //PID <id> //T //F`로 끝낸다. 오래 기다리는 스크립트는 처음부터 PID를 파일에 남기게 한다. 바이트가 섞인 로그를 Monitor로 볼 때는 `grep -a`를 쓴다(아니면 `Binary file (standard input) matches`만 나온다).
- 범위: Claude Code 백그라운드 Bash·Monitor 작업(Windows)
- 증거: 2026-10-17 고아 bash pid 11080·9116이 `python Tools/cook_single_map.py --label anim-cook`(pid 17596)을 23:47:40에 시작, `side-session-notes/.../anim-cook.md` 23:47~23:51 기록
- 날짜·상태: 2026-10-17 active
- 발견: claude

### L-repo-15 Bash 도구에서 긴 heredoc 명령이 `unexpected EOF while looking for matching` 오류로 통째로 실행되지 않는다
- 증상: 약 100줄짜리 명령(여러 `cat > 파일 <<'EOF'` 묶음 또는 `<<'PYEOF'` 파이썬 본문)이 `/usr/bin/bash: -c: line 103: unexpected EOF while looking for matching `''`로 실패하고 파일이 하나도 써지지 않는다. 같은 세션의 짧은 heredoc(20~40줄, 작은따옴표 포함)은 정상
- 원인: 미검증. 도구가 명령 문자열을 `bash -c`로 넘기는 과정에서 긴 본문의 따옴표 짝이 깨지는 것으로 보인다(두 번 모두 본문에 작은따옴표가 여러 개 있었다)
- 해결: 파일 내용은 Write 도구로 쓰고, Bash에서는 그 파일만 실행한다(`python <스크립트 경로>`). 한 번 실패하면 같은 heredoc을 다시 보내지 않는다
- 범위: Claude Code Bash 도구(Windows Git Bash)
- 증거: 2026-10-18 M3-33 세션 정의 JSON 8개 작성·종 표 확장 명령 2회 실패 → Write 도구로 대체해 성공
- 날짜·상태: 2026-10-18 미검증
- 발견: claude

### L-repo-16 `PYTHONUTF8=1`이면 `Tools/ue_editor.py`가 `'NoneType' object has no attribute 'splitlines'`로 죽어 에디터를 띄우지 못한다
- 증상: `PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Tools/ue_editor.py ensure|status` → `editor_pids()`의 `out.splitlines()`에서 `AttributeError: 'NoneType' object has no attribute 'splitlines'`. 이어지는 `check_automation_tests.py`는 `URLError [WinError 10061]`(에디터 없음)로 실패하지만 스크립트 종료 코드는 0처럼 보일 수 있다
- 원인: UTF-8 모드에서 `subprocess.run(..., text=True)`가 `tasklist`의 cp949 한글 출력("정보: ... 작업이 없습니다")을 UTF-8로 디코딩하다 Windows 읽기 스레드 안에서 실패하고, 예외 대신 `stdout=None`을 돌려준다
- 해결: 에디터·MCP 도구(`ue_editor.py`·`check_automation_tests.py`·`run_in_editor.py`)는 `PYTHONIOENCODING=utf-8`만 붙이고 `PYTHONUTF8=1`은 붙이지 않는다. `build_and_test.py`의 한글 출력 오류(L-repo-10)는 `PYTHONIOENCODING`만으로 충분했다
- 범위: Windows 한국어 로캘, 시스템 Python 3.12
- 증거: 2026-10-18 M3-37 GUI PIE 1회차 실패(같은 명령에서 `PYTHONUTF8` 유무만 바꿔 재현: 있으면 예외, 없으면 `editor running: False pids=[]`)
- 날짜·상태: 2026-10-18 검증
- 발견: claude

### L-repo-17 PowerShell에서 쉼표로 이은 인수(`--screenshot-times 0,0.1,0.2`)는 배열로 펼쳐져 파이썬 argparse가 `unrecognized arguments`로 끝난다
- 증상: `python Tools/MonsterAI/pie_measure_ragdoll_jitter.py ... --screenshot-times 0,0.1,0.2,0.35` → 종료 2, `unrecognized arguments: 0.1 0.2 0.35`
- 원인: PowerShell은 따옴표 없는 `a,b,c`를 배열 식으로 해석해 공백으로 나눈 여러 인수로 넘긴다.
- 해결: 작은따옴표로 감싼다(`'0,0.1,0.2,0.35'`). `Start-Process` 인수 문자열 안에서는 안쪽 따옴표가 그대로 전달되므로 빼야 한다.
- 범위: Windows PowerShell 5.1에서 쉼표 값을 받는 Tools 스크립트(`--screenshot-times`·`--spread`·`--assist` 등)
- 증거: 2026-10-19 `Saved/AgentOps/20261019/death-ragdoll/measure2/run-log.md`(sheet2 1회째 실패), `measure4/run-log.md`(Start-Process 안쪽 따옴표)
- 날짜·상태: 2026-10-19 검증
- 발견: claude
### L-repo-18 Bash 도구에서 `grep -c $'\r$' 파일`이 LF 파일의 모든 줄을 세어 "전부 CRLF"로 잘못 판정한다
- 증상: FX-8c G1 명세·반박이 "범위 파일이 모두 CRLF(줄 수 = CR 수)"라고 적었지만, 바이트로 세면 12개가 LF(CRLF 0)였다(`git ls-files --eol`도 `i/lf w/lf`). 같은 명령으로 28/28, 1700/1700을 받았다.
- 원인: 이 환경의 Bash 도구가 명령 문자열의 이중 백슬래시를 하나로 줄여 넘긴다(2026-10-20 확인: heredoc 파이썬의 이중 백슬래시 r·n이 실제 CR·LF가 되고, 이중 백슬래시 따옴표가 문자열을 끝냈다). 또 이 PC는 `core.autocrlf=true`라 `git apply`·`git checkout`으로 쓴 LF 파일이 CRLF로 바뀐다.
- 해결: 줄 끝은 파이썬으로 `data.count(b"\r\n")`·`data.count(b"\n")`를 세고, 명령 안에서 백슬래시가 필요하면 `chr(92)`·`bytes([13, 10])`로 만들거나 Write 도구로 스크립트 파일을 쓴 뒤 실행한다. 사본 통합은 `git apply` 대신 파일 복사 + sha256 확인으로 한다. 파이썬 `Path.read_text()`는 줄 끝을 바꾸므로 고쳐 쓸 때는 `read_bytes`/`write_bytes`를 쓴다.
- 범위: Windows Claude Code Bash 도구, 이 저장소의 혼합 줄 끝 파일
- 증거: `Saved/AgentOps/20261020/fx8c/round4/g1-builder.md` 6절, `round4/g1-refuter.md` M-1
- 날짜·상태: 2026-10-20 active
- 발견: claude(FX-8c g1-builder·g1-refuter)
### L-repo-19 작업 중 들어온 사용자 질문이 워크플로 하위 에이전트에게도 전달되어, 긴 실행 단계(빌드·에디터·PIE) 에이전트가 부모 과제를 거부한다
- 증상: FX-8c 9차 굽기 워크플로에서 bake9-runner가 "전달된 사용자 요청은 질문(나이아가라를 왜 안 쓰나) 하나이고, 지시의 빌드·에디터 종료와 충돌하므로 질문에만 답했다"며 빌드·PIE를 실행하지 않았다. 프롬프트에 "이미 부모가 답했으니 무시하라"를 넣고 재개해도 같은 이유로 거부했다. 편집만 하는 빌더·반박자는 진행했다.
- 원인: 하네스가 진행 중 턴에 들어온 사용자 메시지를 하위 에이전트 문맥에도 '현재 사용자 요청'으로 넣는다. 에이전트는 사용자 요청이 작업 지시보다 우선한다는 규칙으로 판단한다(미검증: 전달 방식의 세부).
- 해결: 사용자 질문이 들어온 턴에서는 되돌리기 어려운 단계(에디터 강제 종료·빌드·저장)를 하위 에이전트에 맡기지 말고 부모가 직접 명령으로 실행한다. 또는 질문에 답한 뒤 다음 사용자 턴에서 워크플로를 띄운다.
- 범위: Claude Code 워크플로·Agent 하위 에이전트, 진행 중 턴의 사용자 메시지
- 증거: `Saved/AgentOps/20261020/fx8c/round9/` 워크플로 wf_5c6abd39-ab7 두 번 실행의 runner 결과
- 날짜·상태: 2026-10-20 active
- 발견: claude

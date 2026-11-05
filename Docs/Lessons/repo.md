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

### L-repo-20 `git apply --cached --unidiff-zero`로 내 덩어리만 스테이징하면, 빈 줄 옆 삽입이 문맥 없이 옮겨져 다른 절(제목 아래)에 들어간다
- 증상: 장부 decisions.md에서 D-56 상태 줄 앞에 넣은 11차 줄이 커밋본에서는 상태 줄 뒤·D-57 제목 바로 앞에 붙고 빈 줄이 사라졌다(f4f3ff29). 12차에도 같은 방식으로 스테이징하자 12차 줄이 `### D-57` 제목 아래로 들어갔다(`git show :Docs/Tasks/decisions.md`로 발견).
- 원인: -U0 덩어리는 문맥 줄이 없어서, 같은 덩어리 안의 줄 이동(상태 줄 삭제 + 재추가)과 빈 줄 경계가 HEAD 줄 번호만으로 다시 놓인다. 다른 세션 덩어리를 빼고 적용하면 번호 기준이 어긋난다.
- 해결: 섞인 장부는 덩어리 적용 대신 "HEAD(또는 작업 트리) 텍스트에서 남의 줄만 빼거나 내 줄만 넣은 블롭"을 만들어 `git hash-object -w --path <파일> <블롭>` → `git update-index --cacheinfo`로 스테이징한다. 어떤 방식이든 커밋 전에 `git show :<파일>`로 바뀐 절 주변 구조를 눈으로 확인한다.
- 범위: 다른 세션 변경이 섞인 마크다운 장부(decisions·Lessons·NOW·README)의 부분 커밋
- 증거: 12차 커밋 전 `git show :Docs/Tasks/decisions.md` 395-409행 확인, f4f3ff29의 decisions.md D-56/D-57 경계
- 날짜·상태: 2026-10-21 active
- 발견: claude(FX-8c 12차 커밋)

### L-repo-21 서드파티 UE 에이전트 플러그인(NarshaMCP 0.13.7) 설치기는 첫 빌드와 에디터 시작마다 `.mcp.json`·`.claude/`·사용자 홈 설정을 다시 쓴다
- 증상: Fab 플러그인을 `Plugins/Marketplace/`에 넣고 빌드한 뒤 `.mcp.json`·`.cursor/mcp.json`이 키 순서가 바뀐 채 다시 쓰이고(끝 줄바꿈 제거), `.claude/settings.local.json`(플러그인 7개 활성·`enableAllProjectMcpServers: true`)·`.claude/agents/unreal-specialist.md`·`.claude/skills/routing.json`·`.antigravity/`가 생기며, 홈의 `~/.codex/config.toml`·`~/.gemini/antigravity/mcp_config.json`·`~/.claude/plugins`(프로젝트 에이전트 7개 바이트 동일 복사본 포함)가 바뀌었다.
- 원인: 에디터 모듈이 시작할 때마다 `narshamcp.exe setup`을 분리 프로세스로 실행한다(Plugins/Marketplace/NarshaMCP/Source/NarshaMCP/Private/CUModule.cpp:301-340). 무엇을 썼는지는 `Intermediate/NarshaMCP/auto_setup_build.log`에 남는다.
- 해결: 도입 전에 설치 로그를 먼저 읽고 바뀐 파일을 `git status`·홈 설정 diff로 확인한다. 설정을 끈 뒤에는 다음 에디터 시작 뒤 되살아나는지 다시 본다(미검증). 자세한 표는 `Docs/AgentCollaboration/research/narsha-adk.md` 2·6절.
- 범위: 에디터 시작 시 외부 실행 파일로 설치·설정을 하는 서드파티 에디터 플러그인
- 증거: `Saved/AgentOps/20261023/narsha-adk/package-audit.md` ①절, 이 세션 `git diff -- .mcp.json .cursor/mcp.json`
- 재확인(2026-10-23 18:18, 다른 세션이 에디터를 켬): `.mcp.json`·`.cursor/mcp.json` narshamcp env가 `RUST_LOG`만 남고(추가한 `NARSHA_TELEMETRY_DISABLED`·`NARSHAMCP_CONFIG_PATH`·`timeout` 삭제), `~/.codex/config.toml` NARSHA 항목 삭제, `.claude/settings.local.json` `enableAllProjectMcpServers`가 true로 복귀. 플러그인 false·서버 목록·Gemini 설정은 유지. 에디터 모듈은 조건 없이 매번 `setup`을 부르고(CUModule.cpp:105·301-340) setup에는 환경 변수 보존 옵션이 없다(`setup --help`).
- 조치(2026-10-23 23:05): `TDGame.uproject`에 `{"Name": "NarshaMCP", "Enabled": false}`를 넣어 에디터 모듈과 빌드 때 setup을 끔(오프라인 MCP 서버는 플러그인 폴더의 exe라 그대로 동작). 효과는 다음 에디터 시작 뒤 `.mcp.json`·`.claude/settings.local.json` 수정 시각으로 확인.
- 날짜·상태: 2026-10-23 active
- 발견: claude(NarshaADK 활용 검토)

### L-repo-22 NarshaMCP 0.13.7 데몬 연결이 흔들리면 도구 호출이 응답 없이 Claude Code MCP 유휴 제한(1800초)까지 멈춘다
- 증상: `ue_cache_control get_engine_config`가 "sent no response or progress for 1800s; aborting"으로 30분 뒤 끝났다. 같은 시간 `~/.narshamcp/logs/daemon-exits.jsonl`에 `daemon_eof`("connect bridge observed EOF or IO error")가 11:44~11:56(KST)에 177회(약 4초 간격) 쌓였고, 데몬(포트 19709)이 11:44에 새로 떠 있었다. 이후 호출은 정상.
- 해결: `.mcp.json` narshamcp에 서버별 `timeout`(밀리초, 180000)을 넣어 멈춤을 3분에 끊는다(효과 미검증, 오류 문구가 안내한 방법). 멈추면 같은 호출을 반복하기 전에 `daemon-exits.jsonl` 끝을 보고 흔들림이 멎었는지 확인한다. 상태 점검 `ue_check_health`는 10초 디스패처 제한으로 자주 실패하므로 가벼운 `ue_glob` 한 번으로 응답을 본다.
- 범위: NarshaMCP `connect` 브리지 + 데몬 구조의 모든 도구 호출
- 증거: 이 세션 오류 원문, `~/.narshamcp/logs/daemon-exits.jsonl`(2026-10-23T02:44:37Z~02:56:29Z)
- 날짜·상태: 2026-10-23 active
- 발견: claude(D-61 적용)

### L-repo-23 Fab 마켓플레이스 경로(`Plugins/Marketplace/NarshaMCP`)에 설치된 NarshaMCP는 설정 파일을 못 찾아 `set_engine_config`가 저장되지 않고, `ue_grep` 엔진 텍스트 검색도 켜지지 않는다
- 증상: `ue_cache_control set_engine_config {engine_source_scan: true, confirm: true}` 응답이 `"persisted": false`, `"persist_reason": "no config.ini at resolved path; mode applies for this session only"`. 엔진 호출 그래프는 만들어졌지만(390초, `Intermediate/NarshaMCP/engine_call_graph_*.bincode.zst` 87MB, `find_callers`의 call_graph_stats engine_call_graph_enabled true) `ue_grep scope=engine|all`은 계속 `engine_not_indexed ... state: project_only`.
- 원인(추정): exe가 찾는 경로는 `NARSHAMCP_CONFIG_PATH` 환경 변수, `Plugins/NarshaMCP/MCP/config/config.ini`, `Plugins/NarshaMCP/Config/config.ini` 등이고 Fab은 `Plugins/Marketplace/NarshaMCP/MCP/config/config.ini`에 설치한다(설치 로그 "Memory Mode: config.ini not found under C:\Project\TDGame"). 엔진 텍스트 계층이 저장된 설정을 데몬 시작 때만 읽는지는 미확인.
- 해결(미검증): narshamcp 서버 env에 `NARSHAMCP_CONFIG_PATH`를 실제 경로로 주고 데몬을 다시 띄운 뒤 다시 켠다. 저장되면 데몬이 뜰 때마다 스캔이 다시 돌 수 있으니 첫 재시작 비용을 잰다. 실측 비용(2026-10-23, UE 5.8·에셋 1.2만): 여유 메모리 최저 45.4→42.5GB, 390초.
- 범위: Fab으로 설치한 NarshaMCP 0.13.7의 설정 저장(엔진 설정·메모리 모드)
- 증거: 이 세션 응답 원문, `~/.narshamcp/logs/rust_mcp.log.2026-10-23` 46·63행, exe 문자열
- 재확인(2026-10-23 20:15~20:33): `NARSHAMCP_CONFIG_PATH`를 준 프로세스에서는 `persisted: true`("written to config.ini [EngineIndexing]")이고 새 프로세스가 `engine_source_scan: true`를 읽는다. 그 새 프로세스(재시작과 같음)는 3.2초에 초기화된 뒤 엔진 호출 그래프를 약 4.5분 동안 다시 만들고(새 캐시 파일, 이전 캐시 재사용 안 함) 메모리가 13.5GB까지 올랐다. `ue_grep scope=engine`은 15분 동안 계속 `engine_not_indexed`였다 — 설정을 저장해도 엔진 텍스트 검색은 켜지지 않고 호출 그래프만 늘어난다. 환경 변수 없이 다른 클라이언트·에디터가 띄운 공유 데몬은 저장된 설정을 못 읽는다.
- 날짜·상태: 2026-10-23 active
- 발견: claude(D-61 엔진 스캔)

### L-repo-24 `td-researcher`·`td-scout`에는 Write 도구가 없어 "보고서를 `Saved/AgentOps`에 쓰고 경로만 반환" 계약이 실패하고, `td-researcher`는 넓은 엔진 조사에서 maxTurns 40에 걸려 보고서 없이 멈춘다
- 증상: 2026-10-23 SSFS 분석에서 td-researcher 3회 모두 "Write 도구가 없어 파일을 만들지 못했다"며 전문을 반환(400자 한도 초과). 엔진 조사 1회는 "stopped at its 40-turn limit ... had produced no report"(도구 69회, 약 20만 토큰)
- 원인: `.claude/agents/td-researcher.md` tools `Read, Grep, Glob, WebSearch, WebFetch`·maxTurns 40, `td-scout.md` tools `Read, Grep, Glob`·maxTurns 25. 파일 쓰기 가능한 정의는 td-builder(Write)·td-refuter·td-debugger·td-test-runner(PowerShell)
- 해결: 보고서 파일이 필요한 조사는 td-refuter(PowerShell로 쓰기)나 general-purpose에 맡기고, td-researcher에는 "본문으로 반환, 질문별 구조" + "도구 호출 30회를 넘으면 탐색을 멈추고 보고"를 넣는다. 턴 한도로 멈추면 SendMessage로 "추가 10회 안에 보고"를 지시(이번에 그대로 회수됨)
- 범위: 이 프로젝트 `.claude/agents/` 정의. CLAUDE.md "전체 파일은 Saved/AgentOps에 두게 한 뒤 경로만 받는다"와 충돌하므로, 정의·규칙 조정은 사용자 승인 사항
- 증거: 이 세션 위임 4건의 반환문(2026-10-23)
- 날짜·상태: 2026-10-23 active
- 발견: claude(SSFS·FSSS 가능성 분석)

### L-repo-25 워크플로 하위 에이전트의 보고서 파일(`REPORT-*.md`) Write가 하네스에 "Subagents should return findings as text, not write report files"로 거부된다 — 같은 워크플로의 다른 에이전트 `.md` 쓰기는 성공한다
- 증상: 2026-10-23 무한 세계 2단 워크플로에서 벤치 에이전트 2개가 `bench/REPORT-static.md`·`REPORT-dynamic.md` 쓰기를 거부당해 '부분'으로 보고했다. 같은 실행의 설계·초안·반박 에이전트는 `02-design-*.md`·`02-draft.md`·`02-refute-*.md`를 썼다.
- 원인: 미확인(파일 이름·내용이 보고서처럼 보이면 거부하는 하네스 판정으로 추정).
- 해결: 측정 에이전트에는 표를 결과 json에서 다시 만드는 요약 스크립트(`summarize_*.py --out`)를 남기게 하고 서술은 구조화 출력에 담게 한다. 부모가 스크립트로 보고서를 만든다. 문서형 산출물 프롬프트에는 "쓰기가 거부되면 전문을 구조화 출력 `full_text`에 담아 반환"을 넣는다. 수만 자 워크플로 결과는 `python Tools/workflow_journal_dump.py <실행 폴더> <출력 폴더>`로 갈래별 파일로 나눠 읽는다.
- 범위: Claude Code Workflow 하위 에이전트(기본 워크플로 에이전트)
- 증거: 실행 wf_fa306c31-451 journal(bench-static·bench-dynamic caveats)
- 날짜·상태: 2026-10-23 active(원인 미확인)
- 발견: claude(무한 세계 가능성 검토)

### L-repo-24 서드파티 MCP 플러그인(NarshaMCP 0.13.7) 제거는 제조사 `uninstall --dry-run` 확인 → 모든 연결 프로세스 종료 → exe 사본으로 실행 → JSON 설정을 HEAD와 의미 비교해 복원 순서로 한다
- 증상: 데몬만 멈추면 다른 세션·에디터가 띄운 `connect` 연결 프로세스가 1초 안에 데몬을 다시 띄운다. 실행 중인 exe는 플러그인 폴더째 지울 수 없다. 제조사 `uninstall`은 `TDGame.uproject`·`.mcp.json`·`.cursor/mcp.json`을 키 정렬로 다시 써서 의미는 같아도 파일 전체 diff가 생긴다.
- 해결: ① `narshamcp.exe uninstall --dry-run --project-path <프로젝트> --plugin-dir <실제 경로>`로 대상 확인(이번에는 AGENTS.md·td-* 에이전트·사용자 스킬은 보존 대상이었음) ② `unregister-protocol` ③ 모든 `narshamcp.exe` 종료(`Stop-Process`) ④ exe를 임시 폴더에 복사해 그 사본으로 `--no-gui uninstall` ⑤ JSON 파일을 `git show HEAD:<파일>`과 의미 비교 후 같으면 `git checkout HEAD -- <파일>` ⑥ 남는 것(`~/.narshamcp`, `~/.claude/plugins/data/core-narsha-adk`, 설치기가 만든 `.claude/settings.local.json`, `~/.codex/skills`의 프로젝트 스킬 복사본)을 수동 삭제.
- 범위: NarshaMCP처럼 설치기가 프로젝트·홈 설정 여러 곳을 쓰는 서드파티 에이전트 도구 제거
- 증거: 이 세션 dry-run·실행 결과(16단계 전부 OK), `git status`에서 관련 파일 diff 없음
- 날짜·상태: 2026-10-23 active
- 발견: claude(NarshaADK 제거)

### L-repo-26 새 git 작업 트리(worktree)에는 무시된 시험 기준 파일 `Saved/FluidFX/fx4b-nonwater-baseline.json`이 없어 물 RHI 시험 하나가 실패한다
- 증상: 새 작업 트리 첫 오프스크린 실행에서 `TDGame.FluidFXRhi.Water.NonWaterMultigridMatchesHeadBaseline` 실패, `nonwater baseline JSON is missing outside capture mode`(Water 46/47)
- 원인: 기준 JSON이 `Saved/`(git 무시)에만 있어 `git worktree add`로 따라오지 않는다. 같은 종류의 `fx8b-gpu-baseline.json`은 첫 실행 때 자동 기록되지만 이 파일은 캡처 모드에서만 기록된다
- 해결: 본 트리 `Saved/FluidFX/fx4b-nonwater-baseline.json`(sha eb466c63…)을 새 작업 트리 같은 경로로 복사한 뒤 실행하면 47/47
- 범위: FluidFX 격리 작업 트리(파동마다 새로 만드는 `C:/Project/TDGame-wave*-wt`)
- 증거: `Saved/AgentOps/20261026/wave2/baseline/index.md`(w2-base-rhi-water2)
- 날짜·상태: 2026-10-26 active
- 발견: claude(FluidFX 파동 2 기준선)

### L-repo-27 FluidFX 파동 검사·생성 순서 맹점: 무대 뒤 재질 값 조각 병합은 모든 무대 보고를 STALE로, 날짜 박힌 Saved 조각 기본값은 병합 뒤 거짓 실패, 검사기 일부는 범위가 좁다
- 증상: A5-h에서 조각 1항목을 무대 생성 뒤 병합하자 HVOL·안개·자국·식생 무대 보고가 `material_look_sha256` 3d18192a ≠ 정본 706f803d로 STALE. `check_fluid_hvol_hlsl.py`가 병합 뒤 "FAIL 조각과 정본 키 서로소"로 종료 1
- 원인·맹점: ① 무대 보고는 재질 값 파일 전체 해시를 적는다 ② 검사기 기본값이 `Saved/AgentOps/<날짜>/…/look-fragment-*.json`을 자동으로 읽었다 ③ `check_fluid_value_independence.py` 헤더 목록에 `TDFluidVolumeDriverTypes.h`가 없어 그룹 2개를 건너뜀 ④ `check_fluid_gpu_shaders.py`는 ApplyStampsCS를 기본 순열로만 컴파일(MASS_INPUT 순열은 RHI 시험으로만 확인) ⑤ 골든 추출 정규식 `0x[0-9A-F]{8}u`가 `RhiActorKey = 0x4D415353ull`도 잡아 45로 셈(실제 44) ⑥ 새 시험 파일에 `WITH_DEV_AUTOMATION_TESTS` 가드가 없으면 check_sync_loads 실패
- 해결: 모든 조각 병합을 무대 생성 앞에 둔다(늦게 병합했으면 그 뒤 무대를 다시 만든다). ②③은 고침(커밋 e6a5e729), ④⑤는 판정 때 알고 쓴다, ⑥은 IB 확인에 check_sync_loads를 넣는다
- 범위: FluidFX 파동 에셋 단계 계획(cross-plan 5절 형식)과 Tools/FluidFX 검사기
- 증거: `Saved/AgentOps/20261027/wave2-assets/refute-final.md` (c)절, `Saved/AgentOps/20261027/wave2-int/main-check/index.md`, `Saved/AgentOps/20261026/wave2/ib2/`(골든 45)
- 날짜·상태: 2026-10-27 active
- 발견: claude(FluidFX 파동 2)

### L-repo-28 Workflow를 TaskStop하면 w3job 분리 실행기도 함께 죽어 빌드 보류가 남는다
- 증상: 파동 3 워크플로 `TaskStop` 뒤 작업 `w3-L3-cp-r0-rhi-replay` 상태가 `RUNNING`인데 `w3job.py _runner`·UnrealEditor 프로세스가 없고, `Saved/AgentOps/build_hold.json`은 owner `w3-L3`로 남았다.
- 원인: `DETACHED_PROCESS|CREATE_NEW_PROCESS_GROUP|CREATE_BREAKAWAY_FROM_JOB`로 띄워도 워크플로 중지가 실행기까지 끝냈다. 그래서 `finally`의 보류 해제가 돌지 않았다. 셸 백그라운드 작업의 자식이 살아남는 L-repo-14와 반대다.
- 해결: 워크플로를 멈춘 뒤 jobs 폴더에서 `RUNNING`·`WAITING_HOLD` 상태를 보고 실행기 프로세스가 없는지 확인한다. 그다음 `python Tools/build_and_test.py --release-hold <owner>`를 실행하고 상태 파일에 KILLED를 적는다.
- 범위: `Saved/AgentOps/20261029/wave3/prep/w3job.py`로 빌드 창을 나누는 워크플로 전부
- 증거: `Saved/AgentOps/20261029/wave3/jobs/w3-L3-cp-r0-rhi-replay-014431/`(out.log 0바이트, 01:47 시작)
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 일시 정지)

### L-repo-29 본 트리(`core.autocrlf=true`)에서는 바이트로 읽는 텍스트 검사기가 CRLF로 실패하고, Git Bash `sed -i`는 CRLF 파일을 LF로 바꾼다
- 증상: 작업 트리에서 통과한 `Tools/FluidFX/check_fluid_water_props_hlsl.py`가 본 트리 병합 뒤 새 HLSL 4개를 "CRLF 줄바꿈"으로 실패. `check_fluid_cloud_hlsl.py`도 L5 작업 트리에서 같은 이유로 실패. W3-F 빌더가 Git Bash `sed -i`로 고친 CRLF 소스가 통째로 LF로 바뀌어 diff 가 파일 전체가 됐다
- 원인: 체크아웃 줄바꿈은 트리마다 다르다(본 트리 autocrlf=true). 검사기가 `open(..., "rb")`나 `newline=""`로 읽고 CR을 금지했다. GNU sed 는 줄 끝 CR을 보존하지 않는다
- 해결: 검사기는 읽은 뒤 `\r\n`→`\n` 정규화 후 검사(LF 규칙은 저장소 기준 `git ls-files --eol`로 따로 본다). 파일 편집은 Edit 도구나 줄바꿈을 보존하는 파이썬(`newline=""`)으로 하고 `sed -i`는 쓰지 않는다
- 범위: Tools/FluidFX 검사기 전부, 여러 작업 트리를 쓰는 병렬 차선 워크플로
- 증거: `Saved/AgentOps/20261029/wave3/merge-L3.md`, `w3-f.md`, 병합 커밋 347c01f8
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 병합)

### L-repo-30 FluidFX 검사 도구 판독 주의 두 가지: 굽기 보고는 `--stage-report`에서 STALE로, 런타임 User 변수는 `compare`에서 차이로 센다
- 증상: `check_fluid_preset_data.py --stage-report Saved/FluidFX/fluid_dense_water_cache.json`(·`fluid_sim_cache.json`)이 STALE. `fluid_material_look.py compare`가 새 NS 4종에서 차이 45
- 원인: 굽기 보고에는 `preset_tuning_sha256`·`material_look_sha256` 필드가 없다. compare 는 굽지 않는 런타임 User 변수(명세 R7, `*_RUNTIME_USER_VARIABLES`)를 모양 값과 구분하지 않는다
- 해결: STALE 판정에는 무대 보고만 넣는다. compare 차이는 이름이 런타임 목록에 있는지 대조해 실질 차이를 센다(도구 개선은 CAL-0 때)
- 범위: D-64 에셋 단계 A7 대조
- 증거: `Saved/AgentOps/20261030/wave3-assets/a7-a8/index.md`, `a9-tests/index.md` 끝 절
- 날짜·상태: 2026-10-30 active
- 발견: claude(FluidFX 파동 3 에셋 단계)

### L-repo-31 `python Tools/ue_editor.py ensure` 가 에디터 복구 때 `Config/DefaultEngine.ini` 의 줄끝만 바꿔 작업 트리가 더러워진다
- 증상: 에디터 충돌 뒤 ensure 로 재기동하자 `git status` 에 ` M Config/DefaultEngine.ini`, `git diff --stat` 은 비어 있고 "LF will be replaced by CRLF" 경고
- 해결: 내용 차이가 없으므로 커밋하지 않고 두거나, 사용자 승인 뒤 되돌린다(`git checkout --` 는 승인 필요). 커밋할 때 경로를 명시해 섞이지 않게 한다
- 범위: ensure 가 원격 실행 설정을 다시 쓰는 경로(본 트리 core.autocrlf=true)
- 증거: 2026-10-31 HVOL 2차 진단(Saved/AgentOps/20261031/hvol/r2) 뒤 본 트리 git status
- 날짜·상태: 2026-10-31 active
- 발견: claude

### L-repo-32 갑작스런 재부팅 뒤 `fatal: index file corrupt`·`your current branch appears to be broken`: 직전에 쓴 git 인덱스·브랜치 참조·작업 파일이 0 바이트로 채워진다
- 증상: 재부팅 직후 `git status` 가 `error: bad signature 0x00000000` / `fatal: index file corrupt`, `git log` 가 `fatal: your current branch appears to be broken`. `.git/index`·`.git/refs/heads/main`·`COMMIT_EDITMSG` 가 같은 크기의 NUL 로만 채워지고, reflog 마지막 줄도 NUL 이다. 재부팅 직전에 커밋한 파일(작업 트리 `Docs/Tasks/decisions.md`)과 쓰던 파일(새 하네스)도 크기는 그대로인데 내용이 모두 NUL 이었다. 직전 커밋 객체(.git/objects 의 느슨한 객체)는 멀쩡했다
- 해결(작업 트리를 건드리지 않는 순서): ① 손상 파일을 `Saved/AgentOps/<날짜>/git-repair/` 에 복사 ② `find .git/objects -type f -newer <기준>` 과 `git cat-file -t` 로 재부팅 직전 커밋 객체를 찾고 `git cat-file -p` 로 부모가 reflog 마지막 정상 줄의 새 해시와 같은지 확인 ③ `git rev-list --objects <커밋> --max-count=1 | cut -d' ' -f1 | git cat-file --batch-check='%(objecttype)'` 에 missing 이 없는지 확인 ④ `.git/refs/heads/main` 에 그 해시를 쓰고 두 reflog 의 NUL 꼬리를 잘라 그 커밋 줄을 더함 ⑤ `.git/index` 를 지우고 `git read-tree HEAD` ⑥ 최근 수정 파일에서 전부 NUL 인 파일을 찾아 추적 파일은 `git show HEAD:<경로> > <경로>` 로(크기가 같으면 커밋 직후 데이터가 안 내려간 것), 추적 안 되는 파일은 다시 만든다 ⑦ `git fsck --no-dangling`. 원자적 쓰기의 `<파일>.tmp.<pid>.<난수>` 가 남아 있으면 그것이 마지막 의도한 내용이다(compile 확인 뒤 바꿔 넣음). 자동 메모리 파일도 꼬리가 NUL 일 수 있다
- 범위: Windows NTFS, Git for Windows, 쓰기 직후 전원 끊김. 에이전트가 쓰던 Saved 산출물도 같은 방식으로 깨질 수 있으므로 재개 때 최근 파일 NUL 검사부터 한다
- 증거: Saved/AgentOps/20261105/git-repair/(손상 사본), 복구 커밋 108a0cf1(부모 c4ad6c8b), git fsck 종료 0
- 날짜·상태: 2026-11-05 active
- 발견: claude(FluidFX W 설계 재개)

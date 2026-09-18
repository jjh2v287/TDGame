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

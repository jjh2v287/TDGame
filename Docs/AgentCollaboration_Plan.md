# 다중 에이전트 공통 관리 체계 (인덱스)

- 작성일: 2026-09-18 (claude). 사용자 요청: 사람 1명 + Claude Code·Codex·Gemini(Antigravity)가 같은 저장소에서 서로 무엇을 했는지 모르고 도구를 각자 만드는 문제를 규칙으로 해결하고, LLM Wiki 같은 방식이 맞는지 조사한다.
- 방법: 다중 에이전트 워크플로 25개(저장소 감사 3 → 웹 조사 6 → 독립 설계 3 → 심사 3 + 종합 → 반박 3렌즈 × 2회 + 보수 2 → 완전성 비평). 원문은 아래 2절 문서.
- 정본: 규칙은 [`AgentRules.md`](AgentRules.md)(요약은 `AGENTS.md` 15절), 결정 기록은 [`Tasks/decisions.md` '관리 체계'](Tasks/decisions.md) D-12~D-37(2026-09-18 결정·적용 완료; 월드·던전 D-02~D-05와 몬스터 AI MD-xx는 보류).

## 1. 결론 요약

1. **LLM Wiki는 그대로 쓰지 않고 요소 4개만 빌린다.** Karpathy의 LLM Wiki(gist 2026-04-04)는 외부 자료 수백 건을 LLM이 요약해 위키로 유지하는 개인 지식 베이스다. 코드 저장소는 코드·문서가 이미 진실의 원천이라 이중 요약이 되고, ingest 1건 약 3.5만 토큰·전체 lint 약 30만 토큰(2차 자료 추정)이 들며, 환각이 위키에 굳는 문제가 보고됐다. 빌린 것: 스키마 문서(=`AGENTS.md`), index(영역 표·등록표), append-only log(=`Docs/Worklog/`), lint(결정론 스크립트로 나중에). 근거: [research/llm-wiki.md](AgentCollaboration/research/llm-wiki.md).
2. **더 최신 대안(Beads, Mem0/Zep, Cline Memory Bank, Ruler, Spec Kit 등)도 배제.** 사람 1명·순차 세션·git 조건에서는 저장소 안 마크다운 파일이 유일하게 세 벤더가 공유하는 매체이고, 외부 서비스는 git diff로 검토할 수 없다. 근거: [research/shared-memory.md](AgentCollaboration/research/shared-memory.md), [research/coordination.md](AgentCollaboration/research/coordination.md).
3. **세션 시작 시 `AGENTS.md`를 세 에이전트가 모두 자동으로 읽게 하는 최소 변경**: 루트 `CLAUDE.md`에 `@AGENTS.md`(Claude Code 공식 권장), `.gemini/settings.json`에 `context.fileName` 배열(Gemini CLI 공식 예시), Codex·Antigravity·Cursor는 네이티브. 사용자 전역 파일 두 개의 "AGENTS.md 참고 하지 않는다" 줄은 2026-09-18 삭제됐다(D-12·D-13). 근거: [research/agent-wiring.md](AgentCollaboration/research/agent-wiring.md).
4. **관리 체계 = 장부 3개 + 규칙 2계층.** `Docs/NOW.md`(현재 상태 한 장, 덮어쓰기) · `Docs/Worklog/YYYY-MM.md`(추가 전용 작업 기록) · `Docs/Lessons/<영역>.md`(문제→해결 기록, `L-<영역>-<번호>`). 규칙은 `AGENTS.md` 15절(매 세션 9줄)과 `AgentRules.md`(전문 OP-01~32)만. 세 할 일 대장·두 결정 기록·기존 인덱스는 옮기지 않고 `담당` 필드·기록 형식만 통일.
5. **도구는 `Tools/`에만, 만들기 전 검색, 같은 세션에 등록, 실험은 `Tools/scratch/`.** 현재 도구 97개 중 등록 33개(34%), `.gemini/scripts` 13개·`Tools/Animation` 11개·Claude 홈 14개가 미등록·중복이다. 처분은 D-15~D-17. 근거: [01-current-state-audit.md](AgentCollaboration/01-current-state-audit.md).

6. **동시 작업과 죽은 세션 인수는 "부분 해결"이다(2차 딥리서치, 2026-09-18).** Windows에서 에디터가 열려 있으면 빌드가 불가하므로 이종 에이전트 동시 작업은 시분할이며, 코드 편집만 병렬이다. 필요한 것은 자원 임대 규칙과 잠금 스크립트, 대장 기록 칸의 `진행`·`인수` 줄이고, 메모리 서버(ai-memory·engram·basic-memory·claude-mem·Mem0 계열)와 관측 추적은 동시 쓰기·토큰·보안·검토 가능성에서 파일 규칙보다 못해 보류·배제했다. 즉시 조치는 Blender MCP 텔레메트리 차단(D-36). 근거: [05-concurrency-and-handoff.md](AgentCollaboration/05-concurrency-and-handoff.md).

## 2. 문서 구성

| 순서 | 파일 | 내용 |
|---|---|---|
| 1 | [01 현재 상태 감사](AgentCollaboration/01-current-state-audit.md) | 도구·문서·배선 감사 결과, 발견 사항(심각도), 인벤토리 |
| 2 | [research/llm-wiki.md](AgentCollaboration/research/llm-wiki.md) | LLM Wiki 원문 구조·한계·커뮤니티 구현·적합성 |
| 3 | [research/agent-wiring.md](AgentCollaboration/research/agent-wiring.md) | Claude Code·Codex·Gemini CLI·Antigravity·Cursor의 규칙 파일·스킬·훅·메모리 공식 규격 |
| 4 | [research/shared-memory.md](AgentCollaboration/research/shared-memory.md) | 공유 지식·메모리 방식 비교와 순위 |
| 5 | [research/tool-registry.md](AgentCollaboration/research/tool-registry.md) | 도구 등록표·이름·검색·격리 관례 |
| 6 | [research/coordination.md](AgentCollaboration/research/coordination.md) | 작업 기록·청구·자원 공유·커밋 식별 관례 |
| 7 | [research/lessons.md](AgentCollaboration/research/lessons.md) | 교훈 기록 형식·lint·승격 기준 |
| 7b | [research/claude-subagents.md](AgentCollaboration/research/claude-subagents.md) | Claude 한정: 서브에이전트로 상위 모델 사용량을 아끼는 라우팅·계약·반환 한도(레딧 글 + 공식 문서) |
| 7c | [research/memory-servers.md](AgentCollaboration/research/memory-servers.md) | MCP 메모리 서버 계열(ai-memory·engram·basic-memory·claude-mem·Serena) 동시 쓰기·인수·보안 평가 |
| 7d | [research/hooks-journal.md](AgentCollaboration/research/hooks-journal.md) | 벤더 훅으로 자동 작업 기록: 페이로드·발화 조건·죽은 세션에서의 한계 |
| 7e | [research/otel-traces.md](AgentCollaboration/research/otel-traces.md) | OpenTelemetry 관측 추적으로 벤더 중립 기록: 설정·수집기·한계 |
| 7f | [research/session-handoff.md](AgentCollaboration/research/session-handoff.md) | 벤더 간 세션 인수: 세션 파일 형식·재개 명령·핸드오프 관례 |
| 7g | [research/concurrency.md](AgentCollaboration/research/concurrency.md) | 같은 작업 트리 동시 작업: 파일 동시성·단일 자원 잠금·git |
| 7h | [research/vendor-native.md](AgentCollaboration/research/vendor-native.md) | 벤더 내장 연속성 기능(자동 메모리·재개·체크포인트)의 범위 |
| 7i | [research/academic-sota.md](AgentCollaboration/research/academic-sota.md) | 학계·업계 에이전트 메모리 최신 동향과 실무 적용 가능성 |
| 8 | [02 최종 설계안](AgentCollaboration/02-design-final.md) | 워크플로가 종합한 설계안 원문(규칙 32·파일 배치·절차·이관 11단계) |
| 9 | [03 설계안 3개와 심사](AgentCollaboration/03-design-alternatives-and-judging.md) | 최소 변경·LLM Wiki 중심·강제력 중심 안과 3렌즈 채점 |
| 10 | [04 반박 검증과 비평](AgentCollaboration/04-verification.md) | 반박 3렌즈 지적과 수정, 근거 정정 목록, 사용자 결정 목록 |
| 10b | [05 동시 작업과 죽은 세션 인수](AgentCollaboration/05-concurrency-and-handoff.md) | 딥리서치 2차 종합: 판정·선택지 비교·추천 조합·규칙 변경 제안·열린 질문 |
| 11 | [AgentRules.md](AgentRules.md) | 채택한 규칙 정본(OP-01~32)과 절차 전문 |
| 12 | [Automation_Backlog.md](Automation_Backlog.md) | 나중에 만들 검사·생성 스크립트 12종 사양 |

## 3. 이번에 적용한 것 (2026-09-18)

- 배선: `CLAUDE.md`(신설, `@AGENTS.md`), `.gemini/settings.json`(`context.fileName`), `.agents/mcp_config.json`(신설), 스킬 스텁 4개(`.agents/skills/`, `.claude/skills/`).
- 규칙: `AGENTS.md` 15절 신설 + 5·8·9·10·12·13절 한 줄씩, 11절의 clangd·MCP 상세를 `Tools/README.md` 0b절로 이동. `AgentRules.md` 신설.
- 장부: `NOW.md`, `Worklog/2026-09.md`, `Lessons/{build,editor,repo,anim}.md`(Tools/README.md 5절 6건 + 이번 세션 발견 이관). `Tasks/README.md`에 `담당` 필드·청구 규칙, `Tasks/decisions.md`에 '관리 체계' D-12~D-37.
- 도구: `Tools/README.md` 0b·3c·4·5절, `templates/editor_tool_template.py` docstring 4줄, `WorldGen/capture_views_mcp.py`의 Claude 홈 경로 의존 제거, `.gitignore`(`__pycache__`·`*.pyc`·`Tools/scratch/`), `Tools/scratch/.gitkeep`.
- Claude 한정(2026-09-18 추가): `.claude/agents/td-{scout,researcher,builder,refuter,debugger,test-runner}.md` 6개와 `CLAUDE.md` "Claude Code 전용" 절. 공용 규칙은 바뀌지 않았고 OP-01에 벤더 전용 운용 절 허용 문구만 추가.
- 결정 반영(2026-09-18 사용자 결정 D-12~D-37): `Tools/Animation`·`.gemini/scripts` 보관(`Tools/_archive/`), `.pyc` 추적 해제, 스모크 리포트를 `Docs/Validation/anim/`으로, GAS 전 증거 10개 보관, Claude 홈 `run_tests.py` → `Tools/check_automation_tests.py`, 홈 스킬 사본 삭제, 홈 MCP 설정 정렬, Gemini 전역 중단 문구 완화, AGENTS.md 핵심 함정 3줄, D-30 `.vscode` 동기화 제외. 동시 운용은 안 함(D-32). 미적용: D-24 줄 끝 규칙(사용자 정리 뒤).
- 하지 않은 것(D-27 순서 뒤): 기존 도구 개명(D-21, 개명 안 함), Claude 메모리 이관, `Docs/README.md` 전체 문서 인덱스, `tasks_recount.py` 확장, 검사 스크립트, D-24 줄 끝 규칙(사용자 미커밋 정리 뒤).

## 4. 다음 순서

1. 전역 파일 금지 줄은 삭제됨(D-12·D-13, 2026-09-18). 각 에이전트 첫 세션에서 배선 확인(agent-wiring.md 표의 확인 명령).
2. 각 에이전트 첫 세션(드라이런): `AGENTS.md` 15절대로 NOW → Worklog → 청구 → 종료 기록을 한 번 수행하고 마찰을 `Lessons/repo.md`에 기록. 배선 확인 명령은 [research/agent-wiring.md](AgentCollaboration/research/agent-wiring.md) 표.
3. 2주 뒤 Worklog 지표로 D-27(이관·스크립트 순서) 결정.
4. 남은 사용자 조치: GUI Blender 5.2 애드온 Allow Telemetry 끄기(D-36), 미커밋 작업 정리 뒤 D-24 줄 끝 규칙 적용 요청. 동시 운용은 안 함(D-32)이라 잠금 스크립트는 만들지 않는다.

## 5. 단독 문서 등록 (Docs/README.md 신설 전 임시)

| 문서 | 종류 | 언제 읽는지 |
|---|---|---|
| `Docs/NOW.md` | 상태 | 매 세션 |
| `Docs/Worklog/` | 기록 | 매 세션 마지막 5항목 |
| `Docs/Lessons/` | 교훈 | 실패 시·영역 작업 전 검색 |
| `Docs/Automation_Backlog.md` | 계획 | 스크립트 착수 시 |

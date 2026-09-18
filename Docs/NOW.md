# NOW — 현재 상태 한 장

갱신: 2026-09-18 11:56 claude
형식: `AGENTS.md` 15절 / `Docs/AgentRules.md` OP-07. 세션 종료 시 덮어쓴다(재작성 전에 다시 읽고 남의 항목을 보존).

## ① 진행 중 (대장 doing)

아래 P 항목은 담당 필드가 없고 마지막 기록이 2026-09-12(claude)라 6일 방치 상태다. 다음 세션은 사용자와 함께 `todo`로 되돌리거나 남은 조건을 확인한 뒤 청구한다(OP-11).

- 월드·던전(P): P2-02 P2-04 P2-05 P2-06 P2-07 P2-09 (`Docs/Tasks/phase-2-dungeon-vertical-slice.md`, 2026-09-12 claude, 담당 없음) · P3-00 P3-02 P3-03 P3-04 P3-05 P3-07 P3-08 P3-09 P3-11 (`Docs/Tasks/phase-3-outdoor-generator.md`, 2026-09-12 claude, 담당 없음)
- 몬스터 AI(M): doing 없음 (Phase 0 미착수, `Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md`)
- 애니메이션(A~G): doing 없음 (우선순위 높음 todo: A-02 A-03 A-04 C-01 F-01, `Docs/AnimationAuthoring_Tasks.md`)
- 공통: 관리 체계 도입 완료(2026-09-18 claude), Claude 서브에이전트 6개 정의, 딥리서치 2차 완료(`Docs/AgentCollaboration/05-concurrency-and-handoff.md`); 각 에이전트 드라이런 미실시

## ② 막힘·결정 대기

- 관리 체계 D-12~D-37은 2026-09-18 결정·적용됨. 남은 사용자 조치: D-24 재정규화 커밋, D-36 GUI Blender 애드온 텔레메트리 끄기, D-29 현행 버전 답. 월드·던전 D-02~D-05와 몬스터 AI MD-01~10은 보류
- `Docs/Tasks/decisions.md#D-02`~`#D-05` (미정), `Docs/Tasks/phase-0-foundation.md#P0-D2`, `Docs/Tasks/phase-2-dungeon-vertical-slice.md#P2-D3`
- `Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md#M3-10` (decision), `#MD-05` (증거 위치); blocked: M2-10 M3-08 M4-01~M4-05 (선행 Phase 미완)
- 사용자 미커밋 변경은 2026-09-18 11:51 커밋(f18d80a)으로 정리됨

## ③ 다음 행동

1. 각 에이전트 첫 세션에서 배선 확인(전역 금지 줄은 2026-09-18 삭제됨, D-12·D-13; `Docs/AgentCollaboration/research/agent-wiring.md` 표의 확인 명령: Claude `/context`, Codex "Summarize the current instructions.", Gemini CLI `/memory list`).
2. 다음 에이전트 세션: `AGENTS.md` 15절대로 시작 → P 대장 doing 15건을 사용자와 정리(todo 복귀 또는 담당 청구) → 종료 기록(Worklog·NOW).
3. 사용자: (a) `git add --renormalize .` 후 커밋(D-24 줄 끝 통일, 1회) (b) GUI Blender 5.2 애드온 Allow Telemetry 끄기(D-36) (c) D-29 현행 애니메이션 버전이 v04(문서·에셋)인지 v05(원본)인지 답하면 산출물 칸에 표기.
4. 2주 뒤: `Docs/Worklog/` 지표로 D-27(이관·스크립트 순서) 결정. 후보 1순위 `Tools/make_status_snapshot.py`(`Docs/Automation_Backlog.md` 1번).

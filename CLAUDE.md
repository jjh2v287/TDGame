@AGENTS.md

- 세션 시작·청구·종료·기록 규칙은 `AGENTS.md` 15절, 전문은 `Docs/AgentRules.md`.
- 자동 메모리에는 사용자 선호와 절차 교정만 남긴다. 프로젝트 사실·명령·함정은 `Docs/Lessons/`에 `L-<영역>-<번호>` 항목으로 적고 메모리에는 그 ID만 둔다.
- 하위 에이전트에는 `AGENTS.md` 15절 요약과 `Docs/NOW.md`를 전달하되, 장부(NOW·Worklog·대장·Lessons)는 부모 세션만 쓴다.

## Claude Code 전용: 서브에이전트 운용 (근거 `Docs/AgentCollaboration/research/claude-subagents.md`)

- 원칙: 이 세션의 모델은 오케스트레이터다. 계획·명세·위임·판정·통합만 직접 하고, 넓게 읽거나 오래 실행하는 일은 `.claude/agents/`의 싼 모델에 넘긴다.
- 라우팅: 위치 찾기 `td-scout`(haiku) 또는 내장 Explore · 조사 `td-researcher`(sonnet) · 명세대로 구현 `td-builder`(sonnet) · 독립 검증 `td-refuter`(opus) · 원인 조사 `td-debugger`(opus) · 빌드·테스트 실행 `td-test-runner`(haiku).
- 위임 체크리스트: 다음 다섯 가지를 모두 만족할 때만 위임한다. 시작 비용을 정당화할 양이 있다 / 긴 설명 없이 기술할 수 있다 / 싼 모델이나 격리의 이점이 있다 / 중간 정보를 내가 볼 필요가 없다 / 잦은 설계 결정이 필요 없다. 한 줄 수정, 파일 한두 개 읽기, 단순 검색은 직접 한다.
- 위임 프롬프트는 계약 형식으로 쓴다: Goal(결과 하나) / Scope(정확한 파일·폴더·심볼) / Allowed changes / Required verification(명령·수용 기준) / Do not / Known facts(이미 아는 것) / Output(보고 구조) / Output limit / Stop conditions.
- 반환 한도: 스카우트는 경로:줄범위 20행, 조사·디버거는 400자, 반박자는 300자, 빌더는 다섯 줄 형식. 전체 파일·diff·로그는 돌려받지 않고 `Saved/AgentOps/<YYYYMMDD>/`에 두게 한 뒤 경로만 받는다.
- 병렬: 읽기 전용 에이전트는 동시에 여러 개 띄운다. 같은 파일을 고치는 빌더는 한 번에 하나만.
- 구현 흐름: 내가 목표·수용 기준을 정한다 → 필요하면 scout·researcher → 내가 명세를 쓴다 → builder가 승인된 범위만 고친다 → refuter가 독립 검증한다 → 내가 판정하고 장부를 기록한다.

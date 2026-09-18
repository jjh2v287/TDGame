[← 인덱스로](../../AgentCollaboration_Plan.md)
# Claude Code 서브에이전트로 상위 모델 사용량을 아끼는 방법 (Claude 한정)

종류: 리서치 · 작성: 2026-09-18 claude · 적용 위치: `CLAUDE.md` "Claude Code 전용" 절, `.claude/agents/td-*.md`

## 출처

- 사용자가 지목한 레딧 글 r/ClaudeCode "how I use subagents without burning through Fable"(2026-09). 레딧 직접 접근은 이 환경에서 차단되어, 같은 글을 정리한 미러 글로 내용을 확인했다: [Zeniteq — How to use Claude subagents without burning through Fable 5.1](https://www.zeniteq.com/how-to-use-claude-subagents-without-burning-through-fable-5-1-qnu5nh) (확인 2026-09-18). 레딧 원문과 표현이 다를 수 있다.
- 공식 문서 [Claude Code — Create custom subagents](https://code.claude.com/docs/en/sub-agents) (확인 2026-09-18): frontmatter 필드, 모델 별칭, 부모 문맥 비상속, 내장 Explore·Plan은 CLAUDE.md를 건너뜀, `omitClaudeMd`는 v2.1.271 이상.
- 보조 [I built 100 Claude Code subagents, these are the 12 that earn their context](https://dev.to/suraj_khaitan_f893c243958/i-built-100-claude-code-subagents-these-are-the-12-that-actually-earn-their-context-10nn) (확인 2026-09-18): "서브에이전트는 페르소나가 아니라 문맥 방화벽", 적게 예리하게, description은 트리거 조건으로.

## 글의 핵심

1. 원칙: 상위 모델(Fable)은 오케스트레이터이지 노동자가 아니다. 계획·명세·위임·판정·통합만 하고, 읽기와 실행은 싼 모델에 넘긴다. 절감의 본질은 "어느 모델이 어떤 일을 받고, 얼마나 읽고, 무엇을 바꿀 수 있고, 얼마나 돌려주는가"를 통제하는 것이다.
2. 역할별 모델: 스카우트 Haiku(위치 탐색), 조사원 Sonnet(문서·소스 조사), 빌더 Sonnet(명세대로 구현·테스트), 반박자 Opus(독립 검증), 디버거 Opus(근본 원인), 오케스트레이터 Fable.
3. 위임 계약 템플릿: Goal / Scope / Allowed changes / Required verification / Do not / Known facts / Output / Output limit / Stop conditions.
4. 반환 형식: 스카우트는 경로:줄범위와 관련도만, 조사원은 검증된 사실과 가정 분리, 빌더는 변경 파일·테스트 결과·불확실, 반박자는 실패 증거와 판정, 디버거는 인과 사슬과 다음 단계. 큰 코드·로그는 부모에 돌려주지 않고 스크래치 파일에 둔다.
5. 병렬 규칙: 읽기 전용은 병렬, 같은 파일 편집은 순차.
6. 위임하지 않을 때: 시작 비용을 정당화할 양이 없거나, 긴 설명 없이 기술할 수 없거나, 싼 모델의 이점이 없거나, 중간 정보를 부모가 봐야 하거나, 잦은 설계 결정이 필요하면 상위 모델이 직접 한다. 한 줄 수정과 단순 검색은 직접.
7. 관측: 글쓴이는 High 모드로 오래 써도 5시간 한도에 닿지 않았고 대시보드에서 상위 모델과 전체 사용량이 거의 반반이었다고 했다. 개인 경험이지 보장은 아니다.

## 이 저장소에 적용한 방식

- `.claude/agents/`에 6개 정의: `td-scout`(haiku, 읽기 전용, CLAUDE.md 생략), `td-researcher`(sonnet), `td-builder`(sonnet, 편집 허용, 명세 필수), `td-refuter`(opus, 읽기+실행), `td-debugger`(opus, 읽기+실행), `td-test-runner`(haiku, 실행만, CLAUDE.md 생략). 모두 장부(NOW·Worklog·대장·Lessons)를 쓰지 않고 부모가 1회 기록한다(AGENTS.md 15절).
- `CLAUDE.md`에 "Claude Code 전용" 절: 라우팅 표, 위임 체크리스트, 계약 템플릿, 반환 한도, 병렬 규칙. 다른 벤더는 이 파일을 읽지 않으므로 공용 규칙(AGENTS.md)은 바뀌지 않는다.
- 큰 산출물 위치는 글의 `.ai/scratch/` 대신 저장소 관례에 맞춰 `Saved/AgentOps/<YYYYMMDD>/`(git 제외)로 정했다.

## 주의

- `omitClaudeMd`는 v2.1.271 이상에서만 동작하며 그 아래 버전은 필드를 무시한다. 무시되어도 동작에는 문제가 없고 스카우트가 AGENTS.md를 추가로 읽어 토큰만 더 든다.
- 내장 Explore 에이전트는 이미 CLAUDE.md를 건너뛰는 저비용 탐색기이므로 단순 파일 찾기는 `td-scout` 대신 Explore로도 충분하다. 둘의 차이는 모델 고정(haiku)과 반환 형식 강제뿐이다.
- 사용량 절감은 위임 프롬프트의 품질에 달려 있다. Scope와 Output limit이 없는 위임은 절감 효과를 없앤다.

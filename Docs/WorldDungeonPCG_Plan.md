# 월드 · 던전 · PCG 구축 계획 (인덱스)

- 기준 문서: `Docs/UE5_탑다운_ARPG_월드_던전_PCG_설계서.docx` (2026-09-09)
- 작성일: 2026-09-09. 이 묶음은 설계서 분석 → 현재 상태 대조 → 제안 아키텍처 → 리서치 → 도구 → 할 일 목록 순서로 읽는다.
- 원칙: 로직은 C++(AGENTS.md 7절), `TD` 접두어(3절). UKGame(`Docs/UKGame/`)은 과거 사례 참고용이며 설계 기준이 아니다.

## 문서 구성

| 순서 | 파일 | 내용 |
|---|---|---|
| 1 | [설계서 분석](WorldDungeonPCG/01-design-doc-analysis.md) | 요구사항 ID(R-xx) 목록, 신뢰도, 보강이 필요한 지점 |
| 2 | [현재 상태와 차이](WorldDungeonPCG/02-current-state-gap.md) | 있는 것, 없는 것(G-xx), 재사용 자산, 지금 당장 필요한 것 |
| 3 | [제안 아키텍처](WorldDungeonPCG/03-architecture.md) | 모듈 3개, 데이터 타입, 알고리즘, 런타임, 에디터, 폴더, 테스트, UKGame과의 차이, 리서치 반영 결정 |
| 4 | [도구·플러그인·설정](WorldDungeonPCG/04-tools-and-plugins.md) | 플러그인, 월드·내비·PCG 설정 체크리스트, 모듈 빌드 설정, 에이전트 도구, 만들 툴 |
| 5 | 리서치 (아래) | 엔진 소스 파일:줄 근거와 웹 사례 |
| 6 | [할 일 목록](Tasks/README.md) | Phase 0~4 작업 대장, 결정 대장, 에이전트 작업 규칙 |

## 리서치 파일 (`WorldDungeonPCG/research/`)

| 파일 | 주제 |
|---|---|
| [worldpartition-streaming.md](WorldDungeonPCG/research/worldpartition-streaming.md) | 스트리밍 소스 컴포넌트·제공자 인터페이스·완료 판정·로딩 범위 공개 API·선로딩 절차 |
| [datalayer.md](WorldDungeonPCG/research/datalayer.md) | 데이터 레이어 런타임 API, Effective 상태, 권한, 세이브·복원 방법 |
| [level-instance.md](WorldDungeonPCG/research/level-instance.md) | Embedded(Partitioned) 조건과 상속 규칙, 런타임 스폰 제약, Packed Level Actor |
| [actor-id-and-navmesh.md](WorldDungeonPCG/research/actor-id-and-navmesh.md) | 런타임 안정 식별자(`FActorInstanceGuid`)와 함정, 월드 파티션 내비메시 청크·준비 확인 |
| [persistence-editor-batch.md](WorldDungeonPCG/research/persistence-editor-batch.md) | 로드/언로드 훅, 세이브 직렬화, 에디터 액터·저장·트랜잭션·리포트 API, 커맨드릿·내비·HLOD·자동화 테스트 |
| [pcg-api-and-nodes.md](WorldDungeonPCG/research/pcg-api-and-nodes.md) | PCG 컴포넌트·서브시스템·파라미터·결정론 API, 배치 노드 목록, 목적별 매핑 |
| [pcg-bake-data-community.md](WorldDungeonPCG/research/pcg-bake-data-community.md) | 베이크(Clear PCG Link), 데이터 에셋·인터롭, Biome Core 구조, 커뮤니티 교훈 |
| [dungeon-generation.md](WorldDungeonPCG/research/dungeon-generation.md) | 접근 비교, 흐름 그래프·Key/Lock 알고리즘, 모듈 조립, 결정론·버전, 검증기, 탑다운 특화, 툴 형태 |
| [landscape-mcp-test.md](WorldDungeonPCG/research/landscape-mcp-test.md) | MCP로 랜드스케이프 생성·편집 실측(UI 자동화로 생성 성공, Python으로 높이맵·스플라인 편집 성공, 생성 API 부재) |

## 지금 당장 필요한 것 (요약)

1. 모듈 2개 신설(`TDGameEditor`, `TDWorldGen`)과 PCG 플러그인 활성화 — Phase 0
2. 월드 파티션 메인 월드 + 던전 아틀라스 더미 슬롯 — P1-01, P1-03
3. 심리스 이동 서브시스템·입구 액터 왕복 프로토타입 — P1-04, P1-05
4. 안정 ID·월드 상태 서브시스템 최소판과 세이브 — P1-06, P1-07
5. 룸 모듈 규격 확정(아트 선행) — P2-01

사용자 결정이 필요한 항목은 [decisions.md](Tasks/decisions.md) 4건(템플릿 잔재, 시작 맵, 룸 그리드 단위, 던전 전용 그리드).

## 메인 지역 프로토타입 (2026-09-12)

`LV_DarkFantasy_OpenWorld`를 코드로 다시 만드는 파이프라인이 `Tools/WorldGen/README.md`에 있다(numpy 생성기 → C++ 랜드스케이프 함수 → 에디터 Python 베이크). 검증은 `Docs/Validation/P3-11-ashen-vale.md`, 할 일은 P3-11.

## 유지 방법
- 리서치를 추가하면 `research/`에 파일을 만들고 이 표에 한 줄 추가한다.
- 할 일은 `Tasks/` 규칙대로 상태를 갱신한다. 아키텍처를 바꾸는 결정은 03 문서 3.10 표에 한 줄 추가한다.

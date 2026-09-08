# TDGame 할 일 목록 (월드 · 던전 · PCG)

이 폴더는 에이전트와 사람이 함께 쓰는 작업 대장이다. 기준 문서는 [설계서 분석·아키텍처 묶음](../WorldDungeonPCG_Plan.md)이며, 각 작업은 그 문서의 요구사항 ID(R-xx), 차이 ID(G-xx), 리서치 절을 참조한다.

## 파일

| 파일 | 내용 |
|---|---|
| [phase-0-foundation.md](phase-0-foundation.md) | 모듈·플러그인·규칙·툴 준비 (선행) |
| [phase-1-seamless-streaming.md](phase-1-seamless-streaming.md) | 월드 파티션 메인 월드, 던전 입구 왕복 프로토타입, 영속 상태 최소판 |
| [phase-2-dungeon-vertical-slice.md](phase-2-dungeon-vertical-slice.md) | 룸 모듈 규격, 흐름·조립·검증, 던전 베이커, 후보 생성 툴 |
| [phase-3-outdoor-generator.md](phase-3-outdoor-generator.md) | 월드·지역·바이옴 정의, 월드 그래프·도로 생성, PCG 바이옴, 월드 검증 |
| [phase-4-production-scale.md](phase-4-production-scale.md) | 규모 확장, HLOD·내비·라이팅 프로파일링, 시드 고정·베이크 |
| [decisions.md](decisions.md) | 사용자 결정이 필요한 항목과 결정 기록 |

## 작업 항목 형식

```
### P1-03 던전 입구 액터
- 상태: todo | doing | blocked | done | decision
- 우선순위: 높음 | 중간 | 낮음
- 선행: P1-02
- 목표: 한 문장
- 완료 조건: 검증 가능한 조건 목록
- 산출물: 파일·에셋 경로
- 검증: 어떻게 확인하는지(테스트 명령, PIE 절차, MCP 도구)
- 참조: R-61, G-03, 03-architecture 3.4.1, 04-research-worldpartition 2절
- 기록: 날짜 + 한 줄 진행 메모
```

## 에이전트 작업 규칙

1. 시작 전에 `AGENTS.md`(특히 7절 C++ 전용 로직, 11절 도구)와 이 README, 해당 Phase 파일을 읽는다. 아키텍처 문서(03)의 클래스·폴더 이름을 따른다.
2. 상태가 `todo`이고 선행 작업이 `done`인 항목 중 우선순위가 가장 높은 것을 고른다. 고르면 상태를 `doing`으로 바꾸고 기록에 날짜와 담당(세션)을 적는다.
3. 완료 조건을 전부 만족했을 때만 `done`으로 바꾼다. 검증 결과(테스트 출력, 로그 발췌, 체력·좌표 수치 등)를 기록에 남긴다. 일부만 됐으면 `doing`을 유지하고 남은 조건을 적는다.
4. 결정이 필요하면 `decision`으로 바꾸고 `decisions.md`에 질문·선택지·권장안을 적은 뒤 다른 작업으로 넘어간다. 사용자 결정 없이 기존 에셋 삭제·덮어쓰기·설정 변경을 하지 않는다.
5. 새 소스 파일을 추가하면 컴파일(Build.bat 또는 라이브 코딩)과 관련 자동화 테스트(`TDGame.*`)를 통과시킨 뒤에만 `done`으로 표시한다. 에디터 검증이 필요한 항목은 MCP·에디터 Python 절차로 확인하고 캡처나 로그를 `Docs/Validation/`에 남긴다.
6. 작업 중 발견한 새 할 일은 해당 Phase 파일 끝에 `todo`로 추가하고 참조를 단다. 범위 밖 문제는 고치지 말고 항목으로만 남긴다.
7. 커밋은 사용자가 지시할 때만 한다.

## 상태 요약

Phase별 완료 현황은 각 파일 맨 위 표를 갱신한다. 마지막 갱신: 2026-09-09 (초기 작성, 전 항목 todo).

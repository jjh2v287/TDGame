# 무리 전술층(공격 토큰·포위 자리)과 전투 디렉터 설계·근거 (M3-14·M3-28)

작성: 2026-10-18 claude. 대장 [07 M3-14·M3-28](../07-roadmap-and-tasks.md). 명세 정본 [measurements/squad-tactics/spec-v2.md](../measurements/squad-tactics/spec-v2.md) + [spec-v2.1-delta.md](../measurements/squad-tactics/spec-v2.1-delta.md). 시뮬 [measurements/squad-tactics/](../measurements/squad-tactics/). 문헌 [web-attack-token-systems.md](web-attack-token-systems.md), [web-squad-slots-and-directors.md](web-squad-slots-and-directors.md), [web-topdown-arpg-crowd-combat.md](web-topdown-arpg-crowd-combat.md).

## 1. 결론

- 사용자 요청 세 가지(스쿼드 자리, 어그로 비례 공격 토큰, AI 디렉터)는 설계 문서에만 있었다(06 §5-5, 08 SC-03·§6-5). 런타임에는 `TDMonsterThinkSubsystem`의 '대상당 동시 공격 3' 고정값만 있었다. 구 PJGame 이식 `AI/CombatToken`은 게임 인스턴스 전역 풀·월드 타이머·비헤이비어 트리 과제라 결정론 규약(D4·D29·D30)에 맞지 않고 호출처도 없다.
- 새 층은 두뇌(유틸리티 + FSM, D1) 밖의 조정층이다. 상태는 `UTDMonsterThinkSubsystem`이 소유하고(D14), 같은 고정 스텝 틱 안에서 사고 직전에 돈다(D15). 두뇌에는 `HasAttackToken`·`RingSlotFree` 스냅샷 입력(D7)과 실행기 문맥만 넘긴다. 종 JSON은 바꾸지 않았다.
- 구성: 대상별 기록(`FTDTargetTacticsRecord`) 하나에 ① 근접·원거리 토큰 풀 ② 대기 고리 2겹(12·16칸)·안쪽 공격 8칸·원거리 12칸 ③ 디렉터 상태(`FTDCombatDirectorState`)가 있다. 끄기 `TD.MonsterAI.Tactics 0`(기존 동작), 보기 `TD.MonsterAI.Tactics.Debug 1`.

## 2. 사용자 규칙과 실제 공식

- 사용자: "어그로 10 → 3분의 1인 3개". 구현: 근접 기본 토큰 = clamp(max((N + 1) / 3, min(N, 2)), 1, 상한). N 8~10 → 3으로 예시와 같다.
- 바꾼 두 곳과 근거(파이썬 결정론 시뮬 457+α회):
  - 소규모 바닥 min(N, 2): N=3에서 토큰 1개면 공격 시작이 기존의 30%(0.65/초 대 2.15)이고 시간의 43%는 아무도 공격하지 않았다. 바닥 2로 0.85~1.32/초가 됐다.
  - 상한 5(합): 시작 간격(0.25~0.5초) 때문에 동시 시전은 실제로 약 4가 한계라, 상한 8은 만료(달려왔다 멈칫)만 늘렸다(만료 157 → 43회).
- 근접·원거리 풀 분리(DOOM 종류별 토큰 사례), 보스 차선(L4D 보스 박자 제외 사례; 페이즈 0 체력 2000 이상, 임시 규칙 — 후속 JSON 역할 키로 교체).

## 3. 동작 요약

- 토큰 수명: 후보 정렬(보복 → 쿨다운 끝 → 대기 나이 → 거리 → SimulationId) → Reserved(만료 256스텝) → 시전 시작 순간 Committed(시퀀스 콤보 끝까지 유지) → 반납(다른 후보 없으면 바로 다시 Reserved). 반납이 생긴 스텝에 즉시 재배정, 대기 1순위는 OnDeck.
- 공격 시작 간격(가독성): 박자별 20/16/24/32스텝, 자비 창 +12. 같은 스텝 동시 시작은 SimulationId 순 처리로 원천 차단.
- 포위 자리: 대기열이 있을 때만(토큰보다 마릿수가 많을 때) 대기 고리를 켠다. 이전 칸 유지(오차 45°), 탐색 ±90°, 몸 폭만큼 칸 점유, 이동하는 대상의 뒤쪽 칸 가산, 막힌 칸 512스텝 금지, 원거리 남쪽(화면 아래) 칸 가산. 대기자는 표류(±6°)와 위협 발짝(반경 −40cm)을 하고, 대상이 다가오면 물러나지 않고 대치한다.
- 디렉터: 긴장도 0~1000(받은 피해 ‰×2, 근처 처치 +40, 공격 시작 +8, 2초 무피해 −40/0.5초) → BuildUp → SustainPeak(4초) → PeakFade(최대 4초) → Relax(4초). 배율 100/100/75/50%이되 기본 토큰보다 1개 넘게 줄이지 않는다. 자비 창(체력 25% 첫 진입 1회, 30초 잠금, 40% 회복 뒤 재무장), 큰 피해(1초 창 16%) 1초 동결, 조우 경계(연결 0명 2초면 기록 삭제), 개시 유예 1.5초(2마리 이상일 때만).

## 4. 검증

- 빨강(구현 전, [red-before-errors.txt](../measurements/squad-tactics/red-before-errors.txt)): 동시 시전 3 > 2, 같은 스텝 동시 시작 41쌍, 대기자 중앙 거리 182cm, 최장 무공격 20.2초(5개 중 4개 실패, 결정론 시험만 통과).
- 초록: `TDGame.MonsterAI.Tactics.*` 19/19. 동료 세션 독립 실행(2026-10-18 13:29, `Saved/AgentOps/20261018/m327-final-tests.json`) Combat·MonsterAI·Movement·FluidFX 175/176, 실패는 기준선 `CircleAvoidance.Scene.CrossingStreams`뿐. 이 세션 최종 실행 [green-final-tests.json](../measurements/squad-tactics/green-final-tests.json) MonsterAI·Movement 52/53(같은 기준선).
- 반박 이력: 명세 v1 3관점(48건) + 시뮬 → v2, 시뮬 재검증(규칙 결함 6) → v2.1, 구현 정적 반박 3관점(경미 11·시험 판별력 주요 1) → 수정, 수정 반박(경미 4) → 수정, 실행에서 드러난 도주 시험 전제 결함 → 순수 시나리오 시험으로 분리.
- 미검증: PIE 육안, 300마리 비용, 벽·복도 실제 지형(시뮬은 2D 평면).

## 5. 후속(별도 작업)

종 JSON 키(역할·토큰 비용·보스 표식, 로더 schema), 경보 연쇄 2단, 무리 사기·공포 전염, 역할 독점(돌진자 1명), 구 `AI/CombatToken` 삭제(TDMonsterCharacter 래퍼 포함), 정면 유지 옆걸음(Characters), 능력 선딜 기반 타격 간격, PIE 육안·300마리 비용 실측, 디렉터 긴장도의 음악·HUD 연결, D31 해시에 전술 상태 포함(명세 11절 목록).

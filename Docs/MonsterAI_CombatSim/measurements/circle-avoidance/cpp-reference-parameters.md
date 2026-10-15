# M3-02 참고 구현 정리 — circle_sim.py 권장 경로(V_yp + 보정)

정본: `Tools/scratch/20261014_claude_elongated_avoidance/circle_sim.py`(줄 번호는 그 파일). 권장 매개변수는 명세 2절.

## 1. 매개변수

| 이름 | 값 | 출처 | C++ 상수(FTDCircleAvoidance / FTDNeighborGrid) |
|---|---|---|---|
| 격자 셀 | 250cm | CELL :18 | FTDNeighborGrid::CellSize |
| 이웃 표면 간격 | 200cm | QUERY_GAP :19 | NeighborSurfaceGap |
| 이웃 상한 | 8 | MAX_NEIGHBORS :20 | MaxAvoidanceNeighbors |
| 결합 반경 여유 | 10cm | MARGIN :21 | CombinedRadiusMargin |
| 접촉 판정 | 표면 간격 < 0.5cm, 중심거리 > 1e-4 | TOUCH :22, touching_push :454 | TouchGap |
| 밀어내기 | 반경합+20, 가중 1.5, 방향만 | touching_push :457, sample_command :492-496 | SeparationExtraGap, SeparationWeight |
| 지평 H | 1.0초 | params horizon | TimeHorizonSeconds |
| w_ttc | 0.3 | params | TimeToCollisionWeight |
| w_smooth | 0.3 | params | SmoothnessWeight |
| w_stop | 0.2 (정지 후보에만) | :478 | StopPenalty |
| 이력 | 0.1 (cost[prev] ≤ cost[best]+0.1) | :480-482 | HysteresisCost |
| 각 | ±120 표 ANGLES_120 | :49 | CandidateAngleTable |
| 양보 TTC | 1.0초 | YIELD_TTC :30 | YieldTimeToCollisionSeconds |
| 양보 속력 | 0.5×vmax | YIELD_SCALE :31 | YieldSpeedRatio |
| 양보 대상 이웃 이동 판정 | 직전 명령 속력 > 1cm/s | :504 | YieldMovingSpeedThreshold |
| 사거리 안 교전 공격자 양보 | 안 함(yield_in_range=False → target<0 일 때만 양보) | :590 | CanYield(..., bIsHoldingEngagedAttacker) |

## 2. 후보 순서(40개, 인덱스 고정)

`candidate_table` :64-74 — 속력 비율 바깥 루프, 각 안쪽 루프, 마지막 정지.

| 인덱스 | 비율 | 각(°) |
|---|---|---|
| 0~12 | 1.0 | 0, +15, −15, +30, −30, +45, −45, +60, −60, +90, −90, +120, −120 |
| 13~25 | 0.6 | 같은 순서 |
| 26~38 | 0.3 | 같은 순서 |
| 39 | 0 | 정지 |

후보 = 희망 방향을 각만큼 회전 × 비율 × 희망 속력(:470-472). 회전은 +각 = 반시계(왼쪽).

## 3. 이동 의도 있는 몬스터(sample_command :462-497)

1. 장애물 = 격자 이웃 몬스터 상위 8(속도 = 이웃의 직전 명령 속도 prev_cmd) + 외부 몸(실제 속도, 자기 target 제외, 표면 간격 ≤ 200, 개수 제한 없음) :583-586, :442-446.
2. 장애물 없음 또는 희망 속도 TTC ≥ H → 희망 속도 그대로, prev_choice = 0(:485-490).
3. 아니면 40개 비용 = |c−pref|/vmax + w_ttc·max(0, 1/max(TTC,1e-3) − 1/H) + w_smooth·|c−prev_cmd|/vmax, 정지 +w_stop. argmin(동률 낮은 인덱스) → 이력 → prev_choice 기록.
4. TTC(:427-439): w = c − v_other, a=|w|², b=w·p(p = 상대 위치 other−self), c0=|p|²−R². 겹침(c0<0)이면 b>0 → 0, 아니면 ∞. 비겹침은 b>0, disc=b²−a·c0>0, a>1e-9 일 때 (b−√disc)/a, 아니면 ∞. 장애물 중 최소.
5. 밀어내기는 조기 통과 경로에도 적용(:491-496): 이웃 몬스터(외부 제외) 중 접촉한 것만 합산, 속력 > 1e-3 이고 합이 0이 아니면 방향만 바꿈.

## 4. 이동 의도 없는 몬스터(:587-592, yield_command :500-522)

- prev_choice = −1. 시전 중(frozen)은 그 전에 건너뜀(prev_choice 유지, 명령 0).
- 양보: 이웃 몬스터(격자 상위 8) 중 |prev_cmd| > 1 인 것만. "나는 정지" TTC(후보 0 속도) 최소 ≥ 1.0 → 0. 선택 = (TTC, 인덱스) 사전식 최소. 방향 = 그 이웃 진행 방향의 왼쪽 법선 × 부호(외적 v×(self−other) 부호, |외적|<1e-6 이면 인덱스 짝수 +1·홀수 −1) × 0.5·vmax.

## 5. 명령 적용(kernel_step :617-629)

- |cmd| > 1e-3 → 방향 = cmd/|cmd|, 배율 = min(|cmd|/vmax, 1), prev_cmd = 방향×배율×vmax. 아니면 방향 0, prev_cmd = 0.
- 모든 몬스터 명령을 다 구한 뒤 prev_cmd 갱신(처리 순서 무관).

## 6. C++ 대응과 차이(구현자 판단)

- 사거리 안 교전 공격자: 파이썬은 추격 계획 몬스터가 의도 0 이면 항상 사거리 안(target ≥ 0). C++ 은 `Engaged && Target 유효`인 의도 0 슬롯으로 대응(사거리 수치 검사 없음).
- 배율 상한: C++ SpeedScale 은 1을 넘을 수 있음(Flee ×1.1 등) → min(…,1) 하지 않음(현행 동작 보존).
- 정지 선택: C++ 은 Direction = 희망 방향, SpeedScale = 0(명세 3.3), 파이썬은 방향 0.
- 감속 대기(선택 속력 < 희망 속력 50%)·연속 스텝 수·귀환 막힘 3초 유예는 C++ 전용(파이썬 없음).

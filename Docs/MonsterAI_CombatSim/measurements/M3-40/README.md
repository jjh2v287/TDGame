# M3-40 사망 즉시 래그돌·타격 방향 날림 — 측정 근거 (2026-10-19 claude)

도구: `Tools/MonsterAI/pie_measure_ragdoll_jitter.py --task M3-40`(+`editor_ragdoll_jitter_sampler.py`), 맵 `LV_TDMegaMagicArena`. 지표 정의는 각 `report.json`의 `definitions`에 있다. 정착 지표는 [M3-38](../M3-38/README.md)·[M3-39](../M3-39/README.md)와 같다.

이 폴더에는 `report.json`과 회차별 `contact_sheet.png`만 넣었다. 개별 프레임 PNG 771장(약 1.3GB)은 `Saved/AgentOps/20261019/death-ragdoll/evidence-png/`로 옮겼고, 원본 매 프레임 표본은 `Saved/AgentOps/20261019/ragdoll-m3-40/<라벨>/raw/`에 있다. 판정 보고서는 `Saved/AgentOps/20261019/death-ragdoll/measure*/judge-*.md`에 있다.

## 회차

| 회차 | 라벨 | 코드 상태 | 요점 |
|---|---|---|---|
| 1 | weak·strong·strong-crit·pile·perf1·perf20·wave5·sheet·sheet-ab-off | 통합 직후 | 체력 0→래그돌 0.10~0.17초. 용·그리폰 날림 1.6배 과대(질량 중심 측정 순서), 하이에나 배 뒤집힘. 스크린샷 끊김(L-editor-45) |
| 2 | weak2·strong2·strong2-rerun8·hunter-*·pile-seq·wave5b·sheet2·wave5-sheet·sheet2-ab-off | 2~4차 수정 | 날림 과대 해소(용 H/설계 1.01), 보스 슬로모 시작 튐 1.05배, 옛 시체 이동 0.3cm. 바게스트 뒤집힘·0.6~1.6초 굳음 |
| 3 | r3-* | 5차 수정 | 다수 처치 시차 0.017~0.067초, 보스 첫 프레임 시간 배율 1.0(6/6), 착지 감지 규칙 98%. 바게스트 뒤집힘·굳음 남음 |
| 4 | r4-* | 6차 수정(렌더 자세 밀기·작은 수평 몸 굴림 상한) | 뼈 개정 번호 증가 3517/3517, 굳음 판정 0/36, 하이에나 순간 각 ≤125° 7/7, 미끄러짐 ≤61.7cm. 바게스트 계열 관문 불합격 → 클립 뒤 래그돌 |
| 5 | r5-* | 최종 | 동시 시체 16구 p95 +2.16~2.40ms(평균 2.26), 검사 착지 각 114~124°, BossLoop 54.7초 통과 |

## 대표 그림

- `r3-sheet/contact_sheet.png`: 렌더 수정 전. 0.6~1.0초가 처치 자세로 굳었다가 1.6초에 바뀐다(L-anim-23).
- `r4-sheet/contact_sheet.png`: 렌더 수정 뒤. 매 시각 물리 자세가 연속으로 그려진다.
- `sheet2-ab-off/contact_sheet.png`: `TD.Death.ImmediateRagdoll 0`(예전 방식) 대조.

## 결정·후속

- 기본값과 예외(그리폰 알파·용 지상 클립, 바게스트 계열 클립 뒤 래그돌, 동시 시체 비용 수용): 07#MD-62(proposed)
- 남은 것(끝 사슬 잠들기, 검사 떠 있음, 바게스트 즉시 래그돌 재시도, 동시 시체 렌더 비용): 07#M3-41

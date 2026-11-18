# 바람 섞기 식 B0 — 필수 판정선 값 표 (fog3d_blend_gates.py collect)

- 판정 문장은 쓰지 않는다. '통과' 열은 등록 4절 문턱을 기계적으로 댄 결과다.
- 문턱: BL-1 (가) ≤ 0.85 × 앞 구간, (나) ≤ 0.15 × 걷는 동안 / BL-2 걷기 ≤ 0.02, 칼 ≤ 0.1 / BL-3 ≤ 1.27 / BL-4 비 ∈ [0.0, 1.0](직전 |차| ≤ 0.001 뺌) / BL-5 ≤ 3.0 × B0 몸 2.0
- 모든 행 통과(값, 행 수): {bl1_a_pass: [False, 0], bl1_b_pass: [False, 0], bl2_pass: [False, 0], bl3_pass: [False, 0], bl4_pass: [False, 0], bl4_target_f32_pass: [False, 0], bl5_pass: [False, 2]}
- 오류 조각: 없음

- BL-4 는 두 읽기를 함께 적는다: '통과' = 목표를 64비트 G×목표 그대로 둔 비, '목표 f32' = 목표를 저장 형식(32비트)으로 둔 비. 넘김 글 ② 참고.
| 장면 | s | G | 초당 | BL-1 (가) 비 | 통과 | BL-1 (나) 몫 | 통과 | BL-2 | 통과 | BL-3 | 통과 | BL-4 최소·최대 | 밖 면(그중 f32 간격 안) | 통과 | BL-4 목표 f32 최소·최대 | 통과 | T 끝 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|

## BL-5 흔들기

| 초당 | 잰 걸음 | 후보 u·v·w | B0 몸 2.0 u·v·w | 비 | 통과 |
|---|---|---|---|---|---|
| 60 | 조각 없음 | | | | |
| 30 | 조각 없음 | | | | |

## 3절 자국 지표 T 사다리(초당 60걸음, 128칸, 걷기 끝 두 걸음·칼 휘두름 끝 두 걸음 평균)

- walk_stand: {T_B0: 1.542e+04, T: {1.0: 1.542e+04}, complete: False}
- blade: {T_B0: 503.8, T: {1.0: 503.8}, complete: False}

## 정보(5절)

- 1_time_match: {B0_hz60: {along_at_0p1s: 0.6173, face_max_over_speed: 2.47, half_time_s: None}, B0_hz30: {along_at_0p1s: 0.5476, face_max_over_speed: 3.477, half_time_s: 0.1}}
- 2_field_diff_vs_B0: {walk_stand_s3: {end: {69: {disp: 0, fill: 0}, 70: {disp: 0, fill: 0}}, after_0.5s: {99: {disp: 0, fill: 0}, 100: {disp: 0, fill: 0}}, after_1.5s: {159: {disp: 0, fill: 0}, 160: {disp: 0, fill: 0}}}, walk_stand_s1: {end: {69: {disp: 0, fill: 0}, 70: {disp: 0, fill: 0}}, after_0.5s: {99: {disp: 0, fill: 0}, 100: {disp: 0, fill: 0}}, after_1.5s: {159: {disp: 0, fill: 0}, 160: {disp: 0, fill: 0}}}, blade_s2.5: {end: {11: {disp: 0, fill: 0}, 12: {disp: 0, fill: 0}}, after_0.25s: {26: {disp: 0, fill: 0}, 27: {disp: 0, fill: 0}}, after_1s: {71: {disp: 0, fill: 0}, 72: {disp: 0, fill: 0}}}, blade_s1: {end: {11: {disp: 0, fill: 0}, 12: {disp: 0, fill: 0}}, after_0.25s: {26: {disp: 0, fill: 0}, 27: {disp: 0, fill: 0}}, after_1s: {71: {disp: 0, fill: 0}, 72: {disp: 0, fill: 0}}}}
- 3_strength: {s1_hz60: {face_max_moving: 529.9, along_at_0p1s: 0.4252, T_end: 1.29e+04}, s3_hz60: {face_max_moving: 1284, along_at_0p1s: 0.6173, T_end: 1.542e+04}, s3_hz30: {face_max_moving: 1808, along_at_0p1s: 0.5476, T_end: 1.447e+04}}
- 4_n256: {bl1_a: 1, bl1_a_pass: False, bl1_b: 0.5299, bl1_b_pass: False, bl2: 0.446, bl2_pass: False, bl3: 2.47, bl3_pass: False}
- 5_turn: {bl2: 0.3197, bl2_pass: False, bl3: 2.438, bl3_pass: False}
- 6_s2a_gain3: {}
- 7_dt144_bl1: {bl1_last: 1553, bl1_prev: 1553, bl1_a: 1, bl1_a_pass: False, bl1_b: 1.107, bl1_b_pass: False}
- T_info_times: {walk_stand_B0: {end: {steps: [69, 70], full: 1.542e+04, outside_120cm: 1.358e+04}, after_0.5s: {steps: [99, 100], full: 1.982e+04, outside_120cm: 1.655e+04}, after_1.5s: {steps: [159, 160], full: 2.15e+04, outside_120cm: 1.847e+04}}, blade_B0: {end: {steps: [11, 12], full: 503.8, outside_120cm: 288.6}, after_0.25s: {steps: [26, 27], full: 1105, outside_120cm: 626}, after_1s: {steps: [71, 72], full: 1934, outside_120cm: 1455}}}

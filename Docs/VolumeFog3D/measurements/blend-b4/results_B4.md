# 바람 섞기 식 B4 — 필수 판정선 값 표 (fog3d_blend_gates.py collect)

- 판정 문장은 쓰지 않는다. '통과' 열은 등록 4절 문턱을 기계적으로 댄 결과다.
- 문턱: BL-1 (가) ≤ 0.85 × 앞 구간, (나) ≤ 0.15 × 걷는 동안 / BL-2 걷기 ≤ 0.02, 칼 ≤ 0.1 / BL-3 ≤ 1.27 / BL-4 비 ∈ [0.0, 1.0](직전 |차| ≤ 0.001 뺌) / BL-5 ≤ 3.0 × B0 몸 2.0
- 모든 행 통과(값, 행 수): {bl1_a_pass: [True, 16], bl1_b_pass: [True, 16], bl2_pass: [True, 32], bl3_pass: [True, 32], bl4_pass: [False, 32], bl4_target_f32_pass: [True, 32], bl5_pass: [True, 2]}
- 오류 조각: 없음

- BL-4 는 두 읽기를 함께 적는다: '통과' = 목표를 64비트 G×목표 그대로 둔 비, '목표 f32' = 목표를 저장 형식(32비트)으로 둔 비. 넘김 글 ② 참고.
| 장면 | s | G | 초당 | BL-1 (가) 비 | 통과 | BL-1 (나) 몫 | 통과 | BL-2 | 통과 | BL-3 | 통과 | BL-4 최소·최대 | 밖 면(그중 f32 간격 안) | 통과 | BL-4 목표 f32 최소·최대 | 통과 | T 끝 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| blade | 1 | 1 | 60 | - | - | - | - | 0.04192 | True | 0.9231 | True | 0.1486·1 | 0(0) | True | 0.1486·1 | True | 335.4 |
| blade | 1 | 3 | 60 | - | - | - | - | 0.03206 | True | 0.8016 | True | 0.1486·1 | 0(0) | True | 0.1486·1 | True | 804.5 |
| blade | 2 | 1 | 60 | - | - | - | - | 0.02237 | True | 0.9637 | True | -1.827e-14·1 | 24(24) | False | -0·1 | True | 453.4 |
| blade | 2 | 3 | 60 | - | - | - | - | 0.01731 | True | 0.8858 | True | -1.003e-14·1 | 24(24) | False | -0·1 | True | 1090 |
| blade | 2.5 | 1 | 60 | - | - | - | - | 0.0235 | True | 0.9737 | True | -1.857e-14·1 | 32(32) | False | -0·1 | True | 491.5 |
| blade | 2.5 | 3 | 60 | - | - | - | - | 0.01781 | True | 0.8915 | True | -1.019e-14·1 | 32(32) | False | -0·1 | True | 1170 |
| blade | 3 | 1 | 60 | - | - | - | - | 0.02522 | True | 0.9845 | True | -1.916e-14·1 | 36(36) | False | -0·1 | True | 521.3 |
| blade | 3 | 3 | 60 | - | - | - | - | 0.02008 | True | 0.9067 | True | -1.035e-14·1 | 36(36) | False | -0·1 | True | 1234 |
| blade | 1 | 1 | 30 | - | - | - | - | 0.04063 | True | 0.9064 | True | 0.02208·1 | 0(0) | True | 0.02208·1 | True | 297.6 |
| blade | 1 | 3 | 30 | - | - | - | - | 0.02048 | True | 0.791 | True | 0.02208·1 | 0(0) | True | 0.02208·1 | True | 728.5 |
| blade | 2 | 1 | 30 | - | - | - | - | 0.03653 | True | 0.9381 | True | -2.518e-15·1 | 22(22) | False | -0·1 | True | 381.5 |
| blade | 2 | 3 | 30 | - | - | - | - | 0.01954 | True | 0.8287 | True | -1.722e-15·1 | 22(22) | False | -0·1 | True | 899.4 |
| blade | 2.5 | 1 | 30 | - | - | - | - | 0.04114 | True | 0.949 | True | -2.553e-15·1 | 24(24) | False | -0·1 | True | 405 |
| blade | 2.5 | 3 | 30 | - | - | - | - | 0.02489 | True | 0.828 | True | -1.712e-15·1 | 24(24) | False | -0·1 | True | 945.8 |
| blade | 3 | 1 | 30 | - | - | - | - | 0.04586 | True | 0.9622 | True | -2.56e-15·1 | 26(26) | False | -0·1 | True | 422.4 |
| blade | 3 | 3 | 30 | - | - | - | - | 0.03216 | True | 0.8496 | True | -1.71e-15·1 | 26(26) | False | -0·1 | True | 983.5 |
| walk_stand | 1 | 1 | 60 | 0.6621 | True | 0.04446 | True | 0.007557 | True | 1.019 | True | 0.0764·1 | 0(0) | True | 0.0764·1 | True | 1.29e+04 |
| walk_stand | 1 | 3 | 60 | 0.6657 | True | 0.0355 | True | 0.002886 | True | 0.9463 | True | 0.0764·1 | 0(0) | True | 0.0764·1 | True | 2.925e+04 |
| walk_stand | 2 | 1 | 60 | 0.659 | True | 0.04439 | True | 0.01009 | True | 1.06 | True | 0·1 | 0(0) | True | 0·1 | True | 1.461e+04 |
| walk_stand | 2 | 3 | 60 | 0.6667 | True | 0.03628 | True | 0.002291 | True | 0.9831 | True | -0·1 | 0(0) | True | -0·1 | True | 3.499e+04 |
| walk_stand | 2.5 | 1 | 60 | 0.6587 | True | 0.04449 | True | 0.01089 | True | 1.064 | True | 0·1 | 0(0) | True | 0·1 | True | 1.499e+04 |
| walk_stand | 2.5 | 3 | 60 | 0.6667 | True | 0.03692 | True | 0.002211 | True | 0.9824 | True | 0·1 | 0(0) | True | 0·1 | True | 3.663e+04 |
| walk_stand | 3 | 1 | 60 | 0.6585 | True | 0.04456 | True | 0.01111 | True | 1.065 | True | 0·1 | 0(0) | True | 0·1 | True | 1.527e+04 |
| walk_stand | 3 | 3 | 60 | 0.678 | True | 0.03889 | True | 0.001889 | True | 0.9818 | True | -0·1 | 0(0) | True | -0·1 | True | 3.802e+04 |
| walk_stand | 1 | 1 | 30 | 0.6586 | True | 0.04438 | True | 0.003686 | True | 1.045 | True | 0.005837·1 | 0(0) | True | 0.005837·1 | True | 1.284e+04 |
| walk_stand | 1 | 3 | 30 | 0.6663 | True | 0.03606 | True | 0.002049 | True | 0.9031 | True | 0.005837·1 | 0(0) | True | 0.005837·1 | True | 3.019e+04 |
| walk_stand | 2 | 1 | 30 | 0.656 | True | 0.04466 | True | 0.003515 | True | 1.085 | True | 0·1 | 0(0) | True | 0·1 | True | 1.441e+04 |
| walk_stand | 2 | 3 | 30 | 0.6635 | True | 0.03676 | True | 0.002013 | True | 0.9363 | True | -0·1 | 0(0) | True | -0·1 | True | 3.504e+04 |
| walk_stand | 2.5 | 1 | 30 | 0.6556 | True | 0.04487 | True | 0.003409 | True | 1.088 | True | 0·1 | 0(0) | True | 0·1 | True | 1.48e+04 |
| walk_stand | 2.5 | 3 | 30 | 0.6667 | True | 0.03709 | True | 0.001754 | True | 0.9367 | True | -0·1 | 0(0) | True | -0·1 | True | 3.644e+04 |
| walk_stand | 3 | 1 | 30 | 0.6553 | True | 0.04503 | True | 0.003385 | True | 1.088 | True | 0·1 | 0(0) | True | 0·1 | True | 1.507e+04 |
| walk_stand | 3 | 3 | 30 | 0.6679 | True | 0.03712 | True | 0.001567 | True | 0.9388 | True | -0·1 | 0(0) | True | -0·1 | True | 3.753e+04 |

## BL-5 흔들기

| 초당 | 잰 걸음 | 후보 u·v·w | B0 몸 2.0 u·v·w | 비 | 통과 |
|---|---|---|---|---|---|
| 60 | 148 | {u: 0.0005436, v: 0.0005245, w: 0.0001893} | {u: 0.0005035, v: 0.0004234, w: 0.0001841} | {u: 1.08, v: 1.239, w: 1.028} | {u: True, v: True, w: True} |
| 30 | 74 | {u: 0.0007048, v: 0.0003719, w: 0.0001962} | {u: 0.0004005, v: 0.0002289, w: 0.0001445} | {u: 1.76, v: 1.625, w: 1.358} | {u: True, v: True, w: True} |

## 3절 자국 지표 T 사다리(초당 60걸음, 128칸, 걷기 끝 두 걸음·칼 휘두름 끝 두 걸음 평균)

- walk_stand: {T_B0: 1.542e+04, T: {1.0: 1.527e+04, 1.25: 1.844e+04, 1.5: 2.086e+04, 1.75: 2.285e+04, 2.0: 2.485e+04, 2.5: 3.01e+04, 3.0: 3.802e+04}, abs_log_ratio: {1.0: 0.009542, 1.25: 0.1789, 1.5: 0.3021, 1.75: 0.3933, 2.0: 0.477, 2.5: 0.6689, 3.0: 0.9024}, spread: 0.9344, flat: False, rule_pick: 1, all_below_B0: False, complete: True}
- blade: {T_B0: 503.8, T: {1.0: 491.5, 1.25: 581, 1.5: 665, 1.75: 749.3, 2.0: 835, 2.5: 1004, 3.0: 1170}, abs_log_ratio: {1.0: 0.02468, 1.25: 0.1426, 1.5: 0.2776, 1.75: 0.3969, 2.0: 0.5052, 2.5: 0.6897, 3.0: 0.8427}, spread: 0.8643, flat: False, rule_pick: 1, all_below_B0: False, complete: True}

## 정보(5절)

- 1_time_match: {B0_hz60: {along_at_0p1s: 0.6173, face_max_over_speed: 2.47, half_time_s: None}, B0_hz30: {along_at_0p1s: 0.5476, face_max_over_speed: 3.477, half_time_s: 0.1}, B1_hz60: {along_at_0p1s: 0.6157, face_max_over_speed: 1.065, half_time_s: 0.3833}, B1_hz30: {along_at_0p1s: 0.4995, face_max_over_speed: 1.085, half_time_s: 0.4333}, B4_hz60: {along_at_0p1s: 0.6157, face_max_over_speed: 1.065, half_time_s: 0.3833}, B4_hz30: {along_at_0p1s: 0.5612, face_max_over_speed: 1.088, half_time_s: 0.4333}}
- 2_field_diff_vs_B0: {walk_stand_s3: {end: {69: {disp: 0.03409, fill: 0}, 70: {disp: 0.03077, fill: 0}}, after_0.5s: {99: {disp: 0.03219, fill: 0}, 100: {disp: 0.03088, fill: 0}}, after_1.5s: {159: {disp: 0.03273, fill: 0}, 160: {disp: 0.0318, fill: 0}}}, walk_stand_s1: {end: {69: {disp: 0, fill: 0}, 70: {disp: 0, fill: 0}}, after_0.5s: {99: {disp: 0, fill: 0}, 100: {disp: 0, fill: 0}}, after_1.5s: {159: {disp: 0, fill: 0}, 160: {disp: 0, fill: 0}}}, blade_s2.5: {end: {11: {disp: 0.05748, fill: 0.06099}, 12: {disp: 0.05593, fill: 0.06091}}, after_0.25s: {26: {disp: 0.04085, fill: 0.08103}, 27: {disp: 0.04059, fill: 0.07844}}, after_1s: {71: {disp: 0.03923, fill: 0.05242}, 72: {disp: 0.03924, fill: 0.05244}}}, blade_s1: {end: {11: {disp: 0, fill: 0}, 12: {disp: 0, fill: 0}}, after_0.25s: {26: {disp: 0, fill: 0}, 27: {disp: 0, fill: 0}}, after_1s: {71: {disp: 0, fill: 0}, 72: {disp: 0, fill: 0}}}}
- 3_strength: {s1_hz60: {face_max_moving: 529.9, along_at_0p1s: 0.4252, T_end: 1.29e+04}, s1_hz30: {face_max_moving: 543.5, along_at_0p1s: 0.4122, T_end: 1.284e+04}, s2_hz60: {face_max_moving: 551.1, along_at_0p1s: 0.5566, T_end: 1.461e+04}, s2_hz30: {face_max_moving: 564.1, along_at_0p1s: 0.5141, T_end: 1.441e+04}, s3_hz60: {face_max_moving: 554, along_at_0p1s: 0.6157, T_end: 1.527e+04}, s3_hz30: {face_max_moving: 565.9, along_at_0p1s: 0.5612, T_end: 1.507e+04}}
- 4_n256: {bl1_a: 0.6585, bl1_a_pass: True, bl1_b: 0.04457, bl1_b_pass: True, bl2: 0.01111, bl2_pass: True, bl3: 1.065, bl3_pass: True}
- 5_turn: {bl2: 0.02259, bl2_pass: False, bl3: 1.373, bl3_pass: False}
- 6_s2a_gain3: {s2a_walk: {finite: True, opposite_max: None, max_wind: 214.1, run_max_wind: None}, s2a_g4: {finite: True, opposite_max: 0, max_wind: 242.2, run_max_wind: None}}
- 7_dt144_bl1: {bl1_last: 23.93, bl1_prev: 36.18, bl1_a: 0.6615, bl1_a_pass: True, bl1_b: 0.04355, bl1_b_pass: True}
- T_info_times: {walk_stand_B4: {end: {steps: [69, 70], full: 1.527e+04, outside_120cm: 1.348e+04}, after_0.5s: {steps: [99, 100], full: 1.962e+04, outside_120cm: 1.643e+04}, after_1.5s: {steps: [159, 160], full: 2.133e+04, outside_120cm: 1.835e+04}}, walk_stand_B0: {end: {steps: [69, 70], full: 1.542e+04, outside_120cm: 1.358e+04}, after_0.5s: {steps: [99, 100], full: 1.982e+04, outside_120cm: 1.655e+04}, after_1.5s: {steps: [159, 160], full: 2.15e+04, outside_120cm: 1.847e+04}}, blade_B4: {end: {steps: [11, 12], full: 491.5, outside_120cm: 280.1}, after_0.25s: {steps: [26, 27], full: 1096, outside_120cm: 617.1}, after_1s: {steps: [71, 72], full: 1924, outside_120cm: 1434}}, blade_B0: {end: {steps: [11, 12], full: 503.8, outside_120cm: 288.6}, after_0.25s: {steps: [26, 27], full: 1105, outside_120cm: 626}, after_1s: {steps: [71, 72], full: 1934, outside_120cm: 1455}}}

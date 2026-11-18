# 2a 덧붙임 2 판정 결과 (후보 R5)

- 시운전(단계 줄임): False
- 잠긴 파일 지문 일치: True
- 코드·등록 지문: fog3d_terrain_add2.py=3f444a66787c0e1987be7cf4017083d90a765fb595edc2a84c184648b4598cd2, fog3d_gates_s2a_add2.py=909e3c13211f422b8e65817184c3d191d85aa8d5e46c5304a5a8ed9cf3ac9bb7, prereg-2a-design-add2.md=1cf873be38fafc0ef3c25578b691f50ac34bbd9250e7d7712709677a85094c43, prereg-2a-design-add1.md=9953966c31f29942a8aae76ed60ce2d539496b907322706515d2eb67fb33e1ae, prereg-2a-design.md=0e530367244fb43c4fcf35585d7db2b122526bba3797993f286072a1ee777e90
- 고른 것: R5 (R5 가 2A-G1 ~ 2A-G13 을 모두 만족하고 2A-G14 에 R5 의 실패가 없음(덧붙임 2 F절 1))
- 평지 값(REF 조각)은 R5 를 F0 에서 돌린 값이다(덧붙임 2 E절). 2A-G4 는 R5 에서 필수 선이다
- 한 단계 시간(128칸, 걷기 조각 중앙값): 2.167초

| 후보 | 판정선 | 종류 | 값 | 문턱 | 통과 |
|---|---|---|---|---|---|
| R5 | G1 | required | F0=0, F1=0 | F0=1e-05, F1=0.0001 | True |
| R5 | G2 | required | 0 | 0 | True |
| R5 | G3 | deciding | smooth=0.2557, cliff=0 | 3 | True |
| R5 | G4 | required | 0 | 1e-06 | True |
| R5 | G5 | deciding | up=0.9594, down=1.107 | 0.5 | True |
| R5 | G6 | required | S30_k1_60=0.5218, S30_k2_60=0.6667, S30_k3_60=0.6667, S45_k1_60=0.5591, S45_k2_60=0.6667, S45_k3_60=0.6667, S60_k1_60=0.5814, S60_k2_60=0.6667, S60_k3_60=0.6667, S60_k2_30=0.6667, S60_k2_144=0.6667, ROUGH_k2_60=0.6514 | wind_over_limit=1, energy=0.01 | True |
| R5 | G6 · S30_k1_60 | 장면 | max_wind=1565, converted=2000, flat_control=1545, M=2000, ratio=0.5218, energy=0.0006162, finite=True, info9_original_ratio=0.5218, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S30_k2_60 | 장면 | max_wind=2856, converted=2309, flat_control=2856, M=2856, ratio=0.6667, energy=0.0004933, finite=True, info9_original_ratio=0.8246, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.237 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S30_k3_60 | 장면 | max_wind=2849, converted=2309, flat_control=2849, M=2849, ratio=0.6667, energy=0.0002046, finite=True, info9_original_ratio=0.8226, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.234 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S45_k1_60 | 장면 | max_wind=1677, converted=2000, flat_control=1545, M=2000, ratio=0.5591, energy=0.0006169, finite=True, info9_original_ratio=0.5591, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S45_k2_60 | 장면 | max_wind=2996, converted=2828, flat_control=2996, M=2996, ratio=0.6667, energy=0.000401, finite=True, info9_original_ratio=0.7062, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.059 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S45_k3_60 | 장면 | max_wind=2984, converted=2828, flat_control=2984, M=2984, ratio=0.6667, energy=2.718e-05, finite=True, info9_original_ratio=0.7035, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.055 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S60_k1_60 | 장면 | max_wind=1744, converted=2000, flat_control=1545, M=2000, ratio=0.5814, energy=0.0006199, finite=True, info9_original_ratio=0.5814, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S60_k2_60 | 장면 | max_wind=5523, converted=4217, flat_control=5523, M=5523, ratio=0.6667, energy=0.0003154, finite=True, info9_original_ratio=0.8733, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.31 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S60_k3_60 | 장면 | max_wind=5538, converted=4217, flat_control=5538, M=5538, ratio=0.6667, energy=3.954e-06, finite=True, info9_original_ratio=0.8756, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.313 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S60_k2_30 | 장면 | max_wind=5341, converted=4217, flat_control=5341, M=5341, ratio=0.6667, energy=0.0002285, finite=True, info9_original_ratio=0.8444, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.267 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · S60_k2_144 | 장면 | max_wind=4721, converted=4217, flat_control=4721, M=4721, ratio=0.6667, energy=0.0003924, finite=True, info9_original_ratio=0.7464, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.12 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G6 · ROUGH_k2_60 | 장면 | max_wind=2802, converted=2351, flat_control=2867, M=2867, ratio=0.6514, energy=0.0002516, finite=True, info9_original_ratio=0.7946, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.22 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R5 | G7 | deciding | S20=3.029e-07, S30=3.029e-07 | 0.15 | True |
| R5 | G8 | required | S10_r45=1, S10_r60=1, S20_r45=1, S20_r60=1, S30_r45=1, S30_r60=1, S45_r45=1, S45_r60=1, S60_r45=1, S60_r60=1 | [0.95, 1.05] | True |
| R5 | G9 | required | HILL=ratio_1=0.002363, ratio_2=0.003703, ratio_3=1.349e-05, MESA=ratio_1=0.002505, ratio_2=0.003755, ratio_3=1.285e-05 | [0.05, 0.05, 0.01] | True |
| R5 | G10 | deciding | 2.672e-12 | 0.5 | True |
| R5 | G11 | deciding | HILL_hill=finite=True, value_over_flat=1.062, run_max_wind_over_flat=0.9909, FOOT30_up=finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011, FOOT30_down=finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011, TOP30_up=finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872, TOP30_down=finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872 | value_over_flat=[0.5, 2], run_max_wind_over_flat=1.5 | True |
| R5 | G11 · HILL_hill | 걷기 | measure=S, finite=True, value_over_flat=1.062, run_max_wind_over_flat=0.9909 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R5 | G11 · FOOT30_up | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R5 | G11 · FOOT30_down | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R5 | G11 · TOP30_up | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R5 | G11 · TOP30_down | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R5 | G12 | required | walk_S20_up=0, walk_S20_down=0, walk_S30_up=0, walk_S30_down=0, walk_S30_contour=0, walk_S30Y_contour=0, walk_S30Y_ydown=0, g6_S30_k2_60=0, g6_S30_k3_60=0, g6_S45_k2_60=0, g6_S45_k3_60=0, g6_S60_k2_60=0, g6_S60_k3_60=0, g6_S60_k2_30=0, g6_S60_k2_144=0 | 0.001 | True |
| R5 | G12 · walk_S20_up | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · walk_S20_down | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · walk_S30_up | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · walk_S30_down | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · walk_S30_contour | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · walk_S30Y_contour | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · walk_S30Y_ydown | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S30_k2_60 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S30_k3_60 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S45_k2_60 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S45_k3_60 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S60_k2_60 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S60_k3_60 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S60_k2_30 | 값 | 0 | ≤ 0.001 | True |
| R5 | G12 · g6_S60_k2_144 | 값 | 0 | ≤ 0.001 | True |
| R5 | G13 | deciding | S30CURB100=1, S30DROP100=1 | ratio=[0.8, 1.25], zero=1e-06 | True |
| R5 | G13 · S30CURB100 | 장면 | value=124.6, r3=124.6, ratio=1, rule=0.8 ≤ R5÷R3 ≤ 1.25, passed=True | 0.8 ≤ R5÷R3 ≤ 1.25 | True |
| R5 | G13 · S30DROP100 | 장면 | value=457.7, r3=457.7, ratio=1, rule=0.8 ≤ R5÷R3 ≤ 1.25, passed=True | 0.8 ≤ R5÷R3 ≤ 1.25 | True |
| R5 | G14 | deciding | 가_S30CURB100_along_cliff=pass, 나_S30DROP100_along_cliff=pass, 다_FOOT45_cross_x=pass, 라_TOP45_cross_x=pass, 마_FOOT30_diagonal=pass, 바_FOOT45_along_fold=pass | value_over_flat=[0.5, 2], run_max_wind_over_M=1.5, rule=R5 의 실패가 없으면 통과(덧붙임 2 F절 2) | True |
| R5 | G14 · 가_S30CURB100_along_cliff | 걷기(M=1646) | R5: holds=True, finite=True, value_over_flat=0.7484, run_max_wind_over_M=0.9245 / R4: holds=True, finite=True, value_over_flat=0.7779, run_max_wind_over_M=0.9245 → pass(R5 가 절대 조건을 지킴) | 유한, 최대 바람 ≤ 1.5 × M | True |
| R5 | G14 · 나_S30DROP100_along_cliff | 걷기(M=1546) | R5: holds=True, finite=True, value_over_flat=0.6766, run_max_wind_over_M=0.8951 / R4: holds=True, finite=True, value_over_flat=0.6818, run_max_wind_over_M=0.8951 → pass(R5 가 절대 조건을 지킴) | 유한, 최대 바람 ≤ 1.5 × M | True |
| R5 | G14 · 다_FOOT45_cross_x | 걷기(M=1284) | R5: holds=True, finite=True, value_over_flat=0.9254, run_max_wind_over_M=1.02 / R4: holds=True, finite=True, value_over_flat=0.9254, run_max_wind_over_M=1.021 → pass(R5 가 절대 조건을 지킴) | 유한, 최대 바람 ≤ 1.5 × M, 0.5 ≤ 값÷평지 ≤ 2.0 | True |
| R5 | G14 · 라_TOP45_cross_x | 걷기(M=1284) | R5: holds=True, finite=True, value_over_flat=1.086, run_max_wind_over_M=0.9872 / R4: holds=True, finite=True, value_over_flat=1.086, run_max_wind_over_M=0.9872 → pass(R5 가 절대 조건을 지킴) | 유한, 최대 바람 ≤ 1.5 × M, 0.5 ≤ 값÷평지 ≤ 2.0 | True |
| R5 | G14 · 마_FOOT30_diagonal | 걷기(M=671.3) | R5: holds=True, finite=True, value_over_flat=0.9807, run_max_wind_over_M=1.148 / R4: holds=True, finite=True, value_over_flat=0.9807, run_max_wind_over_M=1.148 → pass(R5 가 절대 조건을 지킴) | 유한, 최대 바람 ≤ 1.5 × M, 0.5 ≤ 값÷평지 ≤ 2.0 | True |
| R5 | G14 · 바_FOOT45_along_fold | 걷기(M=1284) | R5: holds=True, finite=True, value_over_flat=0.952, run_max_wind_over_M=0.6475 / R4: holds=True, finite=True, value_over_flat=0.952, run_max_wind_over_M=0.6475 → pass(R5 가 절대 조건을 지킴) | 유한, 최대 바람 ≤ 1.5 × M, 0.5 ≤ 값÷평지 ≤ 2.0 | True |

| 후보 | 상태 | 떨어진 선(종류) | 빠진 선 |
|---|---|---|---|
| R5 | all_passed | 없음 | 없음 |

- 알려진 한계 걷기(고른 설계의 알려진 한계(F절 4): 2A-G14 에서 '함께 가진 한계'인 걷기): []
- 정보 ⑯ R5 의 알려진 한계 표시(F절 6): S30CURB45=False, S30CURB60=True, S30CURB80=False, S30CURB130=False, S30DROP60=True

## 정보

- 1_curb: CURB45=up=0.6747, down=0.8291, CURB60=up=0.6507, down=1.208
- 2_slope_walks: ratios=S10_up=S=17, over_flat=1, signed_sum=-5413, S10_down=S=17, over_flat=1, signed_sum=-5413, S10_contour=S=17, over_flat=1, signed_sum=-5413, S20_up=S=17, over_flat=1, signed_sum=-5413, S20_down=S=17, over_flat=1, signed_sum=-5413, S20_contour=S=17, over_flat=1, signed_sum=-5413, S30_up=S=17, over_flat=1, signed_sum=-5413, S30_down=S=17, over_flat=1, signed_sum=-5413, S30_contour=S=17, over_flat=1, signed_sum=-5413, S45_up=S=17, over_flat=1, signed_sum=-5413, S45_down=S=17, over_flat=1, signed_sum=-5413, S45_contour=S=17, over_flat=1, signed_sum=-5413, A=S10=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S20=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S30=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S45=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413
- 7_world_wind: S30_k1_60=world_wind_max=1562, over_flat=1.014, S30_k2_60=world_wind_max=2744, over_flat=1.051, S30_k3_60=world_wind_max=2737, over_flat=1.048, S45_k1_60=world_wind_max=1668, over_flat=1.083, S45_k2_60=world_wind_max=3127, over_flat=1.198, S45_k3_60=world_wind_max=2825, over_flat=1.082, S60_k1_60=world_wind_max=1738, over_flat=1.129, S60_k2_60=world_wind_max=5925, over_flat=2.27, S60_k3_60=world_wind_max=3283, over_flat=1.258, S60_k2_30=world_wind_max=5159, over_flat=1.739, S60_k2_144=world_wind_max=5409, over_flat=2.088, ROUGH_k2_60=world_wind_max=2667, over_flat=1.022, ROUGH_k3_60=world_wind_max=2678, over_flat=1.026
- 9_g6_original_limit: S30_k1_60=limit=3000, wind_over_limit=0.5218, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S30_k2_60=limit=3464, wind_over_limit=0.8246, wind_passed=True, gate_passed=True, flat_over_converted=1.237, S30_k3_60=limit=3464, wind_over_limit=0.8226, wind_passed=True, gate_passed=True, flat_over_converted=1.234, S45_k1_60=limit=3000, wind_over_limit=0.5591, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S45_k2_60=limit=4243, wind_over_limit=0.7062, wind_passed=True, gate_passed=True, flat_over_converted=1.059, S45_k3_60=limit=4243, wind_over_limit=0.7035, wind_passed=True, gate_passed=True, flat_over_converted=1.055, S60_k1_60=limit=3000, wind_over_limit=0.5814, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S60_k2_60=limit=6325, wind_over_limit=0.8733, wind_passed=True, gate_passed=True, flat_over_converted=1.31, S60_k3_60=limit=6325, wind_over_limit=0.8756, wind_passed=True, gate_passed=True, flat_over_converted=1.313, S60_k2_30=limit=6325, wind_over_limit=0.8444, wind_passed=True, gate_passed=True, flat_over_converted=1.267, S60_k2_144=limit=6325, wind_over_limit=0.7464, wind_passed=True, gate_passed=True, flat_over_converted=1.12, ROUGH_k2_60=limit=3526, wind_over_limit=0.7946, wind_passed=True, gate_passed=True, flat_over_converted=1.22
- 11_cliff_x_minus_20_opposite_max: 0
- 12_rough_k3: wind_over_limit=0.6532, max_wind=2802, converted_input_speed=2351, M=2859, limit=4289, energy_end_over_peak=0.000475, finite=True, passed=True, info9_original=limit=3526, wind_over_limit=0.7946, wind_passed=True, gate_passed=True, flat_over_converted=1.216
- 15_comparison: G13_R4=S30CURB100=R4=272, R3=124.6, R4_over_R3=2.183, S30DROP100=R4=457.5, R3=457.7, R4_over_R3=0.9997, G14_R3_c_to_f=다_FOOT45_cross_x=measure=crossing_abs_mean, value=27.72, run_max_wind=1155, finite=True, 라_TOP45_cross_x=measure=crossing_abs_mean, value=14.87, run_max_wind=1180, finite=True, 마_FOOT30_diagonal=measure=crossing_abs_mean, value=13.07, run_max_wind=773.1, finite=True, 바_FOOT45_along_fold=measure=S, value=16.13, run_max_wind=1408, finite=True
- 16_curb_height_sweep_opposite_max: S30CURB45=R3=342.8, R4=364.2, R5=363.9, R4_over_R3=1.062, R5_over_R3=1.061, r5_known_limit=False, S30CURB60=R3=323.1, R4=344.1, R5=346.4, R4_over_R3=1.065, R5_over_R3=1.072, r5_known_limit=True, S30CURB80=R3=251.3, R4=331.6, R5=251.3, R4_over_R3=1.32, R5_over_R3=1, r5_known_limit=False, S30CURB130=R3=0, R4=45.79, R5=0, R4_over_R3=None, R5_over_R3=None, r5_known_limit=False, S30DROP60=R3=452.4, R4=461.2, R5=470.8, R4_over_R3=1.019, R5_over_R3=1.041, r5_known_limit=True
- 17_blend_counts_c_samples: WALK_CURB30_up=full=46314, partial=98, zero=4, level_frame=44660, WALK_CURB30_down=full=46300, partial=98, zero=4, level_frame=44658, G6_ROUGH_k2_60=full=1020, partial=0, zero=0, level_frame=0, G9_HILL_follow=full=21960, partial=0, zero=0, level_frame=0, G11_HILL_hill=full=46372, partial=0, zero=0, level_frame=0, G11_FOOT30_up=full=46292, partial=0, zero=0, level_frame=34052, G11_FOOT30_down=full=46292, partial=0, zero=0, level_frame=10606, G11_TOP30_up=full=46368, partial=0, zero=0, level_frame=34060, G11_TOP30_down=full=46368, partial=0, zero=0, level_frame=10608, G13_S30CURB100_a_x-20=full=34977, partial=0, zero=752, level_frame=0, G13_S30DROP100_a_x-20=full=26099, partial=0, zero=10958, level_frame=0, G13_S30CURB45_a_x-20=full=35361, partial=3747, zero=0, level_frame=0, G13_S30CURB60_a_x-20=full=34977, partial=3514, zero=0, level_frame=0, G13_S30CURB80_a_x-20=full=34977, partial=422, zero=1324, level_frame=0, G13_S30CURB130_a_x-20=full=34977, partial=0, zero=0, level_frame=0, G13_S30DROP60_a_x-20=full=26099, partial=14890, zero=0, level_frame=0, G14_S30CURB100_along_cliff=full=34355, partial=0, zero=752, level_frame=0, G14_S30DROP100_along_cliff=full=25481, partial=0, zero=10958, level_frame=0, G14_FOOT45_cross_x=full=46260, partial=34, zero=0, level_frame=34056, G14_TOP45_cross_x=full=46378, partial=34, zero=0, level_frame=34068, G14_FOOT30_diagonal=full=45316, partial=0, zero=0, level_frame=32799, G14_FOOT45_along_fold=full=46016, partial=0, zero=0, level_frame=0
- 18_cost_add2: per_input=기준 기둥 높이 읽기 1번(m 을 품은 기둥 번호, 창 밖이면 가장자리로 자름), per_sample=평면 편차 e = Hs − Hr − Scx·(x − rx) − Scy·(y − ry)(곱셈 2번·뺄셈 4번), 몫 g 자르기, 0 < g < 1 또는 g = 0 인 표본이 있으면 세계 틀의 band_weight·dwell_amount 한 번 더와 섞기(곱셈 2번), memory=더 드는 메모리 없음(기둥 높이·면 높이는 R3 가 이미 가짐)

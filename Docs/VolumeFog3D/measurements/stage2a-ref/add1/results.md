# 2a 덧붙임 1 판정 결과 (후보 R4)

- 시운전(단계 줄임): False
- 잠긴 파일 지문 일치: True
- 코드·등록 지문: fog3d_terrain_add1.py=51726afd38cb3776, fog3d_gates_s2a_add1.py=38792f921b914a06, prereg-2a-design-add1.md=9953966c31f29942, prereg-2a-design.md=0e530367244fb43c
- 고른 후보: R4 (R4 가 2A-G1 ~ 2A-G12 를 모두 만족(덧붙임 F절))
- 평지 값(REF 조각)은 R4 를 F0 에서 돌린 값이다(덧붙임 E절)
- 한 단계 시간(128칸, 걷기 조각 중앙값): 1.937초

| 후보 | 판정선 | 종류 | 값 | 문턱 | 통과 |
|---|---|---|---|---|---|
| R4 | G1 | required | F0=0, F1=0 | F0=1e-05, F1=0.0001 | True |
| R4 | G2 | required | 0 | 0 | True |
| R4 | G3 | deciding | smooth=0.2557, cliff=0 | 3 | True |
| R4 | G4 | deciding | 0 | 1e-06 | True |
| R4 | G5 | deciding | up=0.9633, down=1.108 | 0.5 | True |
| R4 | G6 | required | S30_k1_60=0.5218, S30_k2_60=0.6667, S30_k3_60=0.6667, S45_k1_60=0.5591, S45_k2_60=0.6667, S45_k3_60=0.6667, S60_k1_60=0.5814, S60_k2_60=0.6667, S60_k3_60=0.6667, S60_k2_30=0.6667, S60_k2_144=0.6667, ROUGH_k2_60=0.6514 | wind_over_limit=1, energy=0.01 | True |
| R4 | G6 · S30_k1_60 | 장면 | max_wind=1565, converted=2000, flat_control=1545, M=2000, ratio=0.5218, energy=0.0006162, finite=True, info9_original_ratio=0.5218, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S30_k2_60 | 장면 | max_wind=2856, converted=2309, flat_control=2856, M=2856, ratio=0.6667, energy=0.0004933, finite=True, info9_original_ratio=0.8246, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.237 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S30_k3_60 | 장면 | max_wind=2849, converted=2309, flat_control=2849, M=2849, ratio=0.6667, energy=0.0002046, finite=True, info9_original_ratio=0.8226, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.234 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S45_k1_60 | 장면 | max_wind=1677, converted=2000, flat_control=1545, M=2000, ratio=0.5591, energy=0.0006169, finite=True, info9_original_ratio=0.5591, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S45_k2_60 | 장면 | max_wind=2996, converted=2828, flat_control=2996, M=2996, ratio=0.6667, energy=0.000401, finite=True, info9_original_ratio=0.7062, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.059 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S45_k3_60 | 장면 | max_wind=2984, converted=2828, flat_control=2984, M=2984, ratio=0.6667, energy=2.718e-05, finite=True, info9_original_ratio=0.7035, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.055 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S60_k1_60 | 장면 | max_wind=1744, converted=2000, flat_control=1545, M=2000, ratio=0.5814, energy=0.0006199, finite=True, info9_original_ratio=0.5814, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S60_k2_60 | 장면 | max_wind=5523, converted=4217, flat_control=5523, M=5523, ratio=0.6667, energy=0.0003154, finite=True, info9_original_ratio=0.8733, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.31 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S60_k3_60 | 장면 | max_wind=5538, converted=4217, flat_control=5538, M=5538, ratio=0.6667, energy=3.954e-06, finite=True, info9_original_ratio=0.8756, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.313 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S60_k2_30 | 장면 | max_wind=5341, converted=4217, flat_control=5341, M=5341, ratio=0.6667, energy=0.0002285, finite=True, info9_original_ratio=0.8444, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.267 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · S60_k2_144 | 장면 | max_wind=4721, converted=4217, flat_control=4721, M=4721, ratio=0.6667, energy=0.0003924, finite=True, info9_original_ratio=0.7464, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.12 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G6 · ROUGH_k2_60 | 장면 | max_wind=2802, converted=2351, flat_control=2867, M=2867, ratio=0.6514, energy=0.0002516, finite=True, info9_original_ratio=0.7946, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.22 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R4 | G7 | deciding | S20=3.029e-07, S30=3.029e-07 | 0.15 | True |
| R4 | G8 | required | S10_r45=1, S10_r60=1, S20_r45=1, S20_r60=1, S30_r45=1, S30_r60=1, S45_r45=1, S45_r60=1, S60_r45=1, S60_r60=1 | [0.95, 1.05] | True |
| R4 | G9 | required | HILL=ratio_1=0.002363, ratio_2=0.003703, ratio_3=1.349e-05, MESA=ratio_1=0.002505, ratio_2=0.003755, ratio_3=1.285e-05 | [0.05, 0.05, 0.01] | True |
| R4 | G10 | deciding | 2.672e-12 | 0.5 | True |
| R4 | G11 | deciding | HILL_hill=finite=True, value_over_flat=1.062, run_max_wind_over_flat=0.9909, FOOT30_up=finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011, FOOT30_down=finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011, TOP30_up=finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872, TOP30_down=finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872 | value_over_flat=[0.5, 2], run_max_wind_over_flat=1.5 | True |
| R4 | G11 · HILL_hill | 걷기 | measure=S, finite=True, value_over_flat=1.062, run_max_wind_over_flat=0.9909 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R4 | G11 · FOOT30_up | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R4 | G11 · FOOT30_down | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=0.9549, run_max_wind_over_flat=1.011 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R4 | G11 · TOP30_up | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R4 | G11 · TOP30_down | 걷기 | measure=crossing_abs_mean, finite=True, value_over_flat=1.052, run_max_wind_over_flat=0.9872 | 0.5 ≤ 값÷평지 ≤ 2.0, 최대 바람 ≤ 1.5 × 평지 | True |
| R4 | G12 | required | walk_S20_up=0, walk_S20_down=0, walk_S30_up=0, walk_S30_down=0, walk_S30_contour=0, walk_S30Y_contour=0, walk_S30Y_ydown=0, g6_S30_k2_60=0, g6_S30_k3_60=0, g6_S45_k2_60=0, g6_S45_k3_60=0, g6_S60_k2_60=0, g6_S60_k3_60=0, g6_S60_k2_30=0, g6_S60_k2_144=0 | 0.001 | True |
| R4 | G12 · walk_S20_up | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · walk_S20_down | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · walk_S30_up | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · walk_S30_down | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · walk_S30_contour | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · walk_S30Y_contour | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · walk_S30Y_ydown | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S30_k2_60 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S30_k3_60 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S45_k2_60 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S45_k3_60 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S60_k2_60 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S60_k3_60 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S60_k2_30 | 값 | 0 | ≤ 0.001 | True |
| R4 | G12 · g6_S60_k2_144 | 값 | 0 | ≤ 0.001 | True |

| 후보 | 상태 | 떨어진 선(종류) | 빠진 선 |
|---|---|---|---|
| R4 | all_passed | 없음 | 없음 |

## 정보

- 1_curb: CURB45=up=0.6747, down=0.8291, CURB60=up=0.6507, down=1.208
- 2_slope_walks: ratios=S10_up=S=17, over_flat=1, signed_sum=-5413, S10_down=S=17, over_flat=1, signed_sum=-5413, S10_contour=S=17, over_flat=1, signed_sum=-5413, S20_up=S=17, over_flat=1, signed_sum=-5413, S20_down=S=17, over_flat=1, signed_sum=-5413, S20_contour=S=17, over_flat=1, signed_sum=-5413, S30_up=S=17, over_flat=1, signed_sum=-5413, S30_down=S=17, over_flat=1, signed_sum=-5413, S30_contour=S=17, over_flat=1, signed_sum=-5413, S45_up=S=17, over_flat=1, signed_sum=-5413, S45_down=S=17, over_flat=1, signed_sum=-5413, S45_contour=S=17, over_flat=1, signed_sum=-5413, A=S10=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S20=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S30=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S45=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413
- 7_world_wind: S30_k1_60=world_wind_max=1562, over_flat=1.014, S30_k2_60=world_wind_max=2744, over_flat=1.051, S30_k3_60=world_wind_max=2737, over_flat=1.048, S45_k1_60=world_wind_max=1668, over_flat=1.083, S45_k2_60=world_wind_max=3127, over_flat=1.198, S45_k3_60=world_wind_max=2825, over_flat=1.082, S60_k1_60=world_wind_max=1738, over_flat=1.129, S60_k2_60=world_wind_max=5925, over_flat=2.27, S60_k3_60=world_wind_max=3283, over_flat=1.258, S60_k2_30=world_wind_max=5159, over_flat=1.739, S60_k2_144=world_wind_max=5409, over_flat=2.088, ROUGH_k2_60=world_wind_max=2667, over_flat=1.022, ROUGH_k3_60=world_wind_max=2678, over_flat=1.026
- 9_g6_original_limit: S30_k1_60=limit=3000, wind_over_limit=0.5218, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S30_k2_60=limit=3464, wind_over_limit=0.8246, wind_passed=True, gate_passed=True, flat_over_converted=1.237, S30_k3_60=limit=3464, wind_over_limit=0.8226, wind_passed=True, gate_passed=True, flat_over_converted=1.234, S45_k1_60=limit=3000, wind_over_limit=0.5591, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S45_k2_60=limit=4243, wind_over_limit=0.7062, wind_passed=True, gate_passed=True, flat_over_converted=1.059, S45_k3_60=limit=4243, wind_over_limit=0.7035, wind_passed=True, gate_passed=True, flat_over_converted=1.055, S60_k1_60=limit=3000, wind_over_limit=0.5814, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S60_k2_60=limit=6325, wind_over_limit=0.8733, wind_passed=True, gate_passed=True, flat_over_converted=1.31, S60_k3_60=limit=6325, wind_over_limit=0.8756, wind_passed=True, gate_passed=True, flat_over_converted=1.313, S60_k2_30=limit=6325, wind_over_limit=0.8444, wind_passed=True, gate_passed=True, flat_over_converted=1.267, S60_k2_144=limit=6325, wind_over_limit=0.7464, wind_passed=True, gate_passed=True, flat_over_converted=1.12, ROUGH_k2_60=limit=3526, wind_over_limit=0.7946, wind_passed=True, gate_passed=True, flat_over_converted=1.22
- 10_R3_G11: value=HILL_hill=finite=True, value_over_flat=0.8304, run_max_wind_over_flat=0.894, FOOT30_up=finite=True, value_over_flat=1.22, run_max_wind_over_flat=0.9478, FOOT30_down=finite=True, value_over_flat=0.9237, run_max_wind_over_flat=1.013, TOP30_up=finite=True, value_over_flat=0.8309, run_max_wind_over_flat=0.8982, TOP30_down=finite=True, value_over_flat=1.08, run_max_wind_over_flat=0.9883, threshold=value_over_flat=[0.5, 2], run_max_wind_over_flat=1.5, passed=True, detail=walks=HILL_hill=measure=S, finite=True, value_over_flat=0.8304, run_max_wind_over_flat=0.894, value=14.12, value_flat=17.01, run_max_wind=1148, run_max_wind_flat=1284, checks=finite=True, value_in_band=True, wind_ok=True, passed=True, FOOT30_up=measure=crossing_abs_mean, finite=True, value_over_flat=1.22, run_max_wind_over_flat=0.9478, value=25.34, value_flat=20.77, run_max_wind=1217, run_max_wind_flat=1284, checks=finite=True, value_in_band=True, wind_ok=True, passed=True, FOOT30_down=measure=crossing_abs_mean, finite=True, value_over_flat=0.9237, run_max_wind_over_flat=1.013, value=19.18, value_flat=20.77, run_max_wind=1301, run_max_wind_flat=1284, checks=finite=True, value_in_band=True, wind_ok=True, passed=True, TOP30_up=measure=crossing_abs_mean, finite=True, value_over_flat=0.8309, run_max_wind_over_flat=0.8982, value=17.26, value_flat=20.77, run_max_wind=1154, run_max_wind_flat=1284, checks=finite=True, value_in_band=True, wind_ok=True, passed=True, TOP30_down=measure=crossing_abs_mean, finite=True, value_over_flat=1.08, run_max_wind_over_flat=0.9883, value=22.43, value_flat=20.77, run_max_wind=1269, run_max_wind_flat=1284, checks=finite=True, value_in_band=True, wind_ok=True, passed=True
- 11_cliff_x_minus_20_opposite_max: 0
- 12_rough_k3: wind_over_limit=0.6532, max_wind=2802, converted_input_speed=2351, M=2859, limit=4289, energy_end_over_peak=0.000475, finite=True, passed=True, info9_original=limit=3526, wind_over_limit=0.7946, wind_passed=True, gate_passed=True, flat_over_converted=1.216
- 13_cost_add1: per_input=틀 기울기 읽기 1번: Sx·Sy 를 이중선형 보간(기둥 네 개, 비율 0~1 로 자름)으로, 입력 하나에 한 번, per_sample=z̃ = z − Scx·(x − mx) − Scy·(y − my): 곱셈 2번·뺄셈 2번(+ 표본 자리와 가운데의 차 2번), memory=더 드는 메모리 없음(기둥 기울기는 R3 가 이미 가짐)
- 14_slope_curb_opposite_max: R3=124.6, R4=272, R4_over_R3=2.183

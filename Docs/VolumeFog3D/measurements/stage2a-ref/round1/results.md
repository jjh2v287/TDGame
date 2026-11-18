# 2a 판정 결과

- 시운전(단계 줄임): False
- 잠긴 파일 지문 일치: True
- 코드·등록 지문: fog3d_terrain.py=7a375bb4669b1da5, fog3d_gates_s2a.py=067957a533391361, prereg-2a-design.md=0e530367244fb43c
- 고른 후보: None (모든 후보 탈락)
- 한 단계 시간(128칸, 걷기 조각 중앙값): 1.958초

| 후보 | 판정선 | 종류 | 값 | 문턱 | 통과 |
|---|---|---|---|---|---|
| R0 | G3 | deciding | smooth=873, cliff=450 | 3 | False |
| R1 | G4 | deciding | 507.2 | 1e-06 | False |
| R2 | G7 | deciding | S20=0.5626, S30=0.5148 | 0.15 | False |
| R2 | G10 | deciding | 300.2 | 0.5 | False |
| R3 | G1 | required | F0=0, F1=0 | F0=1e-05, F1=0.0001 | True |
| R3 | G2 | required | 0 | 0 | True |
| R3 | G3 | deciding | smooth=0.2557, cliff=0 | 3 | True |
| R3 | G4 | deciding | 0 | 1e-06 | True |
| R3 | G5 | deciding | up=0.858, down=1.131 | 0.5 | True |
| R3 | G6 | required | S30_k1_60=0.5109, S30_k2_60=0.6773, S30_k3_60=0.679, S45_k1_60=0.5454, S45_k2_60=0.8106, S45_k3_60=0.8236, S60_k1_60=0.533, S60_k2_60=0.879, S60_k3_60=0.8872, S60_k2_30=0.9137, S60_k2_144=1.007, ROUGH_k2_60=0.6516 | wind_over_limit=1, energy=0.01 | False |
| R3 | G6 · S30_k1_60 | 장면 | max_wind=1533, converted=2000, flat_control=1545, M=2000, ratio=0.5109, energy=0.0006695, finite=True, info9_original_ratio=0.5109, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S30_k2_60 | 장면 | max_wind=2902, converted=2309, flat_control=2856, M=2856, ratio=0.6773, energy=0.0004821, finite=True, info9_original_ratio=0.8377, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.237 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S30_k3_60 | 장면 | max_wind=2902, converted=2309, flat_control=2849, M=2849, ratio=0.679, energy=0.0001954, finite=True, info9_original_ratio=0.8377, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.234 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S45_k1_60 | 장면 | max_wind=1636, converted=2000, flat_control=1545, M=2000, ratio=0.5454, energy=0.0007276, finite=True, info9_original_ratio=0.5454, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S45_k2_60 | 장면 | max_wind=3643, converted=2828, flat_control=2996, M=2996, ratio=0.8106, energy=0.0003807, finite=True, info9_original_ratio=0.8586, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.059 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S45_k3_60 | 장면 | max_wind=3687, converted=2828, flat_control=2984, M=2984, ratio=0.8236, energy=2.925e-05, finite=True, info9_original_ratio=0.869, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.055 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S60_k1_60 | 장면 | max_wind=1599, converted=2000, flat_control=1545, M=2000, ratio=0.533, energy=0.0007826, finite=True, info9_original_ratio=0.533, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=0.7724 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S60_k2_60 | 장면 | max_wind=7282, converted=4217, flat_control=5523, M=5523, ratio=0.879, energy=0.0002729, finite=True, info9_original_ratio=1.151, info9_original_wind_passed=False, info9_original_gate_passed=False, info9_flat_over_converted=1.31 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S60_k3_60 | 장면 | max_wind=7370, converted=4217, flat_control=5538, M=5538, ratio=0.8872, energy=3.704e-06, finite=True, info9_original_ratio=1.165, info9_original_wind_passed=False, info9_original_gate_passed=False, info9_flat_over_converted=1.313 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S60_k2_30 | 장면 | max_wind=7319, converted=4217, flat_control=5341, M=5341, ratio=0.9137, energy=0.0002343, finite=True, info9_original_ratio=1.157, info9_original_wind_passed=False, info9_original_gate_passed=False, info9_flat_over_converted=1.267 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G6 · S60_k2_144 | 장면 | max_wind=7130, converted=4217, flat_control=4721, M=4721, ratio=1.007, energy=0.0003516, finite=True, info9_original_ratio=1.127, info9_original_wind_passed=False, info9_original_gate_passed=False, info9_flat_over_converted=1.12 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | False |
| R3 | G6 · ROUGH_k2_60 | 장면 | max_wind=2802, converted=2351, flat_control=2867, M=2867, ratio=0.6516, energy=0.0002518, finite=True, info9_original_ratio=0.7948, info9_original_wind_passed=True, info9_original_gate_passed=True, info9_flat_over_converted=1.22 | 최대 바람 ≤ 1.5 × M, 끝 에너지 ≤ 0.01 | True |
| R3 | G7 | deciding | S20=0.1407, S30=0.2123 | 0.15 | False |
| R3 | G8 | required | S10_r45=1, S10_r60=1, S20_r45=1, S20_r60=1, S30_r45=1, S30_r60=1, S45_r45=1, S45_r60=0.9997, S60_r45=0.9968, S60_r60=0.9646 | [0.95, 1.05] | True |
| R3 | G9 | required | HILL=ratio_1=0.002783, ratio_2=0.004456, ratio_3=1.576e-05, MESA=ratio_1=0.002505, ratio_2=0.003755, ratio_3=1.285e-05 | [0.05, 0.05, 0.01] | True |
| R3 | G10 | deciding | 2.672e-12 | 0.5 | True |

| 후보 | 상태 |
|---|---|
| R0 | status=eliminated, failed=[G3] |
| R1 | status=eliminated, failed=[G4] |
| R2 | status=eliminated, failed=[G10, G7] |
| R3 | status=eliminated, failed=[G7] |

## 정보

- 1_curb: CURB45=up=0.6747, down=0.8291, CURB60=up=0.6507, down=1.208
- 2_slope_walks: ratios=S10_up=S=15.85, over_flat=0.9322, signed_sum=-5099, S10_down=S=18.23, over_flat=1.072, signed_sum=-5757, S10_contour=S=16.96, over_flat=0.9974, signed_sum=-5390, S20_up=S=14.75, over_flat=0.8673, signed_sum=-4807, S20_down=S=19.58, over_flat=1.151, signed_sum=-6150, S20_contour=S=16.88, over_flat=0.9928, signed_sum=-5358, S30_up=S=13.62, over_flat=0.801, signed_sum=-4513, S30_down=S=20.96, over_flat=1.233, signed_sum=-6583, S30_contour=S=16.63, over_flat=0.9777, signed_sum=-5264, S45_up=S=11.51, over_flat=0.6771, signed_sum=-3955, S45_down=S=22.97, over_flat=1.351, signed_sum=-7308, S45_contour=S=15.47, over_flat=0.91, signed_sum=-4902, A=S10=S_up=15.85, S_down=18.23, A=0.0698, signed_up=-5099, signed_down=-5757, S20=S_up=14.75, S_down=19.58, A=0.1407, signed_up=-4807, signed_down=-6150, S30=S_up=13.62, S_down=20.96, A=0.2123, signed_up=-4513, signed_down=-6583, S45=S_up=11.51, S_down=22.97, A=0.3323, signed_up=-3955, signed_down=-7308
- 3_R2_vs_R3_field_diff: S20_up=u=388.8, v=327.9, w=1236, dpx=476.6, dpy=68.66, dpz=97.24, g=0, S20_down=u=492.1, v=515.6, w=1250, dpx=405.7, dpy=65.38, dpz=68.22, g=0, S30_up=u=417.4, v=678.6, w=937.6, dpx=489.4, dpy=83.08, dpz=103.8, g=0, S30_down=u=413.3, v=231.4, w=1219, dpx=418.7, dpy=93.46, dpz=84.12, g=0
- 4_source_resolution: S10_src31.25=2.842e-14, S10_src62.5=2.842e-14, S20_src31.25=5.684e-14, S20_src62.5=1.137e-13, S30_src31.25=1.137e-13, S30_src62.5=1.137e-13, S45_src31.25=1.137e-13, S45_src62.5=1.137e-13, HILL_src31.25=1.07, HILL_src62.5=4.22, CLIFF_src31.25=98.44, CLIFF_src62.5=267.2, DIAG_src31.25=86.13, DIAG_src62.5=232.5, MESA_src31.25=86.13, MESA_src62.5=232.5
- 5_cost: extra_reads_per_cell=input_spheres=u·v 면은 양쪽 기둥 높이 2번, w 면·칸 가운데는 1번(입력 구 상자 안 칸만), top_target_transform=기둥 기울기 2개(Sx, Sy) 읽기 1번(입력 구 상자 안 w 면만), drawing=재질 표본마다 높이 그림 가로 선형 보간 4번, extra_memory_bytes=column_height_f32=262144, slopes_f32x2=524288, total=786432, state_bytes_ref=38797312, bake_reads=per_column=높이 5번(자기+이웃 4, 창 밖 한 칸 고리 포함) → 벽·기울기, per_bake=327680, when=창 이동·다시 시작·물체 목록 변경 때만
- 7_world_wind: S30_k1_60=world_wind_max=1528, over_flat=0.9924, S30_k2_60=world_wind_max=2686, over_flat=1.029, S30_k3_60=world_wind_max=2684, over_flat=1.028, S45_k1_60=world_wind_max=1630, over_flat=1.059, S45_k2_60=world_wind_max=2848, over_flat=1.091, S45_k3_60=world_wind_max=2699, over_flat=1.034, S60_k1_60=world_wind_max=1593, over_flat=1.035, S60_k2_60=world_wind_max=6004, over_flat=2.3, S60_k3_60=world_wind_max=3583, over_flat=1.373, S60_k2_30=world_wind_max=6149, over_flat=2.072, S60_k2_144=world_wind_max=5117, over_flat=1.975, ROUGH_k2_60=world_wind_max=2667, over_flat=1.022
- 8_ground_frame_sphere: R3=G7=S20=S_up=14.75, S_down=19.58, A=0.1407, signed_up=-4807, signed_down=-6150, S30=S_up=13.62, S_down=20.96, A=0.2123, signed_up=-4513, signed_down=-6583, G4_cliff_a_x_minus_20=0, R3G=G7=S20=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, S30=S_up=17, S_down=17, A=3.029e-07, signed_up=-5413, signed_down=-5413, G4_cliff_a_x_minus_20=373.2
- 9_g6_original_limit: S30_k1_60=limit=3000, wind_over_limit=0.5109, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S30_k2_60=limit=3464, wind_over_limit=0.8377, wind_passed=True, gate_passed=True, flat_over_converted=1.237, S30_k3_60=limit=3464, wind_over_limit=0.8377, wind_passed=True, gate_passed=True, flat_over_converted=1.234, S45_k1_60=limit=3000, wind_over_limit=0.5454, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S45_k2_60=limit=4243, wind_over_limit=0.8586, wind_passed=True, gate_passed=True, flat_over_converted=1.059, S45_k3_60=limit=4243, wind_over_limit=0.869, wind_passed=True, gate_passed=True, flat_over_converted=1.055, S60_k1_60=limit=3000, wind_over_limit=0.533, wind_passed=True, gate_passed=True, flat_over_converted=0.7724, S60_k2_60=limit=6325, wind_over_limit=1.151, wind_passed=False, gate_passed=False, flat_over_converted=1.31, S60_k3_60=limit=6325, wind_over_limit=1.165, wind_passed=False, gate_passed=False, flat_over_converted=1.313, S60_k2_30=limit=6325, wind_over_limit=1.157, wind_passed=False, gate_passed=False, flat_over_converted=1.267, S60_k2_144=limit=6325, wind_over_limit=1.127, wind_passed=False, gate_passed=False, flat_over_converted=1.12, ROUGH_k2_60=limit=3526, wind_over_limit=0.7948, wind_passed=True, gate_passed=True, flat_over_converted=1.22

# 탑다운 개념 증명 (카메라 top, 옆빛) — 숫자

C = 세로 [0.25H,0.75H) x 가로 [0.25W,0.75W). Fv = C 안 |Y(S 기준 장 S8.3) - Y(top_hidden 의 S8.3)| 평균. Rs = C 안 |Y(S2.5) - Y(S8.3)| 가운데 큰 쪽 2% 화소(올림) 평균. Rn = 같은 식, S8.3 과 S8.8. Rw = 같은 식, W12.1 과 W19.4, 걷기 잡음 Rn_walk = W19.4 와 W19.9. 8비트 밝기 단위

성공 기준: Fv >= 10 이고 Rs >= 12 이며 Rs >= 3 x Rn. 어느 한 구성이라도 충족: **예**

구역 숨김 장(top_hidden): 유효 예, 로그 사용 예, 제출/거부/경고 1753/0/0, C 안 자체 잡음(S8.3 대 S8.8) 0.283

| 구성 | 바꾼 값 | Fv | Rs | Rn | Rs ÷ Rn | Rw | 걷기 잡음 | Rw ÷ 걷기 잡음 | Fv≥10 | Rs≥12 | Rs≥3Rn | 모두 충족 | 유효 | 로그 사용 | 로그(제출/거부/경고) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| T0 | 기본값 | 11.18 | 14.26 | 4.69 | 3.04 | 7.41 | 6.38 | 1.16 | 예 | 예 | 예 | **예** | 예 | 예 | 1074/0/0 |
| T1 | BaseDensity 4 | 36.76 | 32.19 | 4.14 | 7.77 | 12.56 | 6.15 | 2.04 | 예 | 예 | 예 | **예** | 예 | 예 | 1074/0/0 |
| T2 | BaseDensity 8 | 49.66 | 34.14 | 3.96 | 8.63 | 9.68 | 5.86 | 1.65 | 예 | 예 | 예 | **예** | 예 | 예 | 1075/0/0 |
| T3 | BaseDensity 16 | 58.02 | 26.35 | 3.68 | 7.16 | 7.53 | 5.40 | 1.39 | 예 | 예 | 예 | **예** | 예 | 예 | 1075/0/0 |
| T4 | BaseDensity 8, BaseThicknessCm 150, BaseTopBlurCm 120 | 54.27 | 15.99 | 2.71 | 5.91 | 5.65 | 3.84 | 1.47 | 예 | 예 | 예 | **예** | 예 | 예 | 1076/0/0 |
| T5 | BaseDensity 16, BaseThicknessCm 150, BaseTopBlurCm 120 | 60.24 | 11.43 | 2.24 | 5.10 | 4.30 | 3.03 | 1.42 | 예 | 아니오 | 예 | **아니오** | 예 | 예 | 1075/0/0 |
| T6 | BaseDensity 8, BaseThicknessCm 150, BaseTopBlurCm 120, FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30 | 54.28 | 48.53 | 2.71 | 17.93 | 12.08 | 3.80 | 3.17 | 예 | 예 | 예 | **예** | 예 | 예 | 1076/0/0 |
| T7 | BaseDensity 16, BaseThicknessCm 150, BaseTopBlurCm 120, FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30 | 60.29 | 40.82 | 2.22 | 18.36 | 7.90 | 3.05 | 2.59 | 예 | 예 | 예 | **예** | 예 | 예 | 1074/0/0 |
| T8 | BaseDensity 16, BaseThicknessCm 150, BaseTopBlurCm 120, FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.6, StainNoiseSpacingCm 400 | 59.89 | 60.89 | 1.99 | 30.54 | 12.16 | 3.14 | 3.87 | 예 | 예 | 예 | **예** | 예 | 예 | 1074/0/0 |
| T7_gridz | BaseDensity 16, BaseThicknessCm 150, BaseTopBlurCm 120, FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30 | 60.33 | 41.67 | 2.63 | 15.83 | 7.89 | 3.38 | 2.33 | 예 | 예 | 예 | **예** | 예 | 예 | 1074/0/0 |

## 뒤 카메라 실행(눈으로 보는 용도, 등록본 1c 의 뒤 카메라 숫자)

| 실행 | 바꾼 값 | Wk(12.1) | Sk(2.5) | Sk2(2.5) | Tx2 | N_W | N_S | 유효 | 로그 사용 | 로그(제출/거부/경고) |
|---|---|---|---|---|---|---|---|---|---|---|
| T4_back | BaseDensity 8, BaseThicknessCm 150, BaseTopBlurCm 120 | 0.58 | 0.65 | 2.29 | 0.36 | 0.31 | 0.30 | 예 | 예 | 1075/0/0 |
| T7_back | BaseDensity 16, BaseThicknessCm 150, BaseTopBlurCm 120, FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30 | 2.11 | 1.77 | 3.77 | 0.36 | 0.32 | 0.31 | 예 | 예 | 1075/0/0 |
| base_d | 기본값 | 1.28 | 4.57 | 12.26 | 0.39 | 0.23 | 0.29 | 예 | 예 | 1073/0/0 |

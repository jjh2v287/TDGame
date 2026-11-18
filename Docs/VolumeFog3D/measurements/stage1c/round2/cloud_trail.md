# 구름 무늬와 걷기 자국 이어짐 (중 값 위, 뒤 카메라) — 숫자

구름 무늬: Tx2 > 0.45 이고 Sk2(2.5) >= 0.8 x s3 의 Sk2(2.5). 걷기 자국 이어짐: Wk(17.6) / Wk(12.1) >= 0.4 이고 8 <= Wk(12.1) <= 14. 고르는 것은 감독이 그림을 보고 한다

중 값 기준(s3, 조합 탐색): Tx2 0.4488, Sk2(2.5) 26.03, Wk(12.1) 10.63, Wk(17.6) 2.40

| 실행 | 정본 대비 바꾼 값 | Tx2 | Sk2(2.5) | Sk2 ÷ s3 | Wk(12.1) | Wk(17.6) | Wk(17.6) ÷ Wk(12.1) | Tx2>0.45 | Sk2≥0.8×s3 | 구름 조건 | 이어짐 비≥0.4 | Wk(12.1) 8~14 | 이어짐 조건 | 유효 | 로그 사용 | 로그 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| s3 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, bStainNoiseEnabled True, StainNoiseAmplitude 0.6, StainNoiseSpacingCm 400, BladeRadiusCm 30 | 0.449 | 26.03 | 1.00 | 10.63 | 2.40 | 0.23 | 아니오 | 예 | **아니오** | 아니오 | 예 | **아니오** | 예 | 예 | 1075/0/0 |
| C1 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.75, StainNoiseSpacingCm 400 | 0.580 | 25.30 | 0.97 | 11.05 | 2.69 | 0.24 | 예 | 예 | **예** | 아니오 | 예 | **아니오** | 예 | 예 | 1073/0/0 |
| C2 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.75, StainNoiseSpacingCm 600 | 0.447 | 21.40 | 0.82 | 9.03 | 1.23 | 0.14 | 아니오 | 예 | **아니오** | 아니오 | 예 | **아니오** | 예 | 예 | 1073/0/0 |
| C3 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.85, StainNoiseSpacingCm 500 | 0.482 | 21.57 | 0.83 | 10.80 | 1.97 | 0.18 | 예 | 예 | **예** | 아니오 | 예 | **아니오** | 예 | 예 | 1074/0/0 |
| C4 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.75, StainNoiseSpacingCm 400, BaseThicknessCm 110, BaseTopBlurCm 70 | 0.425 | 20.63 | 0.79 | 11.26 | 2.80 | 0.25 | 아니오 | 아니오 | **아니오** | 아니오 | 예 | **아니오** | 예 | 예 | 1076/0/0 |
| R1 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.6, StainNoiseSpacingCm 400, DisplacementRefillHorizontalPerSec 0.05, DisplacementRefillVerticalPerSec 0.15 | 0.440 | 27.62 | 1.06 | 11.96 | 6.61 | 0.55 | 아니오 | 예 | **아니오** | 예 | 예 | **예** | 예 | 예 | 1076/0/0 |
| R2 | FootRadiusCm 45, HandRadiusCm 45, PelvisRadiusCm 60, BodyWindStrength 2, BladeRadiusCm 30, bStainNoiseEnabled True, StainNoiseAmplitude 0.6, StainNoiseSpacingCm 400, DisplacementRefillHorizontalPerSec 0.05, DisplacementRefillVerticalPerSec 0.15, WindRefillPerSec 0.5 | 0.445 | 31.36 | 1.20 | 13.58 | 8.23 | 0.61 | 아니오 | 예 | **아니오** | 예 | 예 | **예** | 예 | 예 | 1075/0/0 |

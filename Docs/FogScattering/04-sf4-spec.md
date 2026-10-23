# SF-4 명세 — 탑다운 보정(거리 배율·가중 지수·이계별 강도) + 사용자 확인 화면

갱신: 2026-10-24 claude · 상태: v2(실제 필드 실측 재결정, 8절) · 상위 D-62(탑다운 보정 채택, 기본 꺼짐 = SSFS 동등)

## 1. 배경 수치

- 게임 필드는 `LV_DarkFantasy_OpenWorld`(P3 메인 지역, L-editor-58). 실제 필드 안개 `TDGen_HeightFog`: 밀도 0.022·감쇠 0.35·최대 불투명도 0.92·시작 600cm·높이 −200cm·두 번째 층 0.06/1.8(−260cm)·볼류메트릭 포그 거리 9000cm·소멸 배율 2.2. v1(6절)은 `L_TDWorld_Main` 안개(밀도 0.005·감쇠 0.03·높이 −6850cm)를 필드로 잘못 보고 정했다.
- 이계(`Tools/WorldGen/generate_realm.py`): 가라앉은 별밭 밀도 0.02·감쇠 0.25·시작 거리 2500cm·두 번째 층 0.10/2.0, 하얀 고원 밀도 0.03·감쇠 0.6·시작 거리 800cm·두 번째 층 0.25/3.0. 별밭은 시작 거리 25m가 탑다운 시야(≤ 27m)를 거의 덮는다.

## 2. 플러그인 변경 (SSFS 동등 기본값 유지)

| 항목 | 내용 |
|---|---|
| `r.TDFogScattering.DistanceScale` | 높이 안개 선적분과 **볼류메트릭 포그 투과율**(`T^k`) 모두에 적용. 0이면 안개 기여 없음, 1이면 엔진과 같음 |
| `r.TDFogScattering.WeightPower`(신규) | 기본 1, 0.1~4. `w = pow(saturate(Intensity × (1 − T_height × T_media)), WeightPower)` — 1 미만이면 옅은 안개를 끌어올림 |
| 블루프린트 | `SetFogScatteringWeightPower`·`GetFogScatteringWeightPower` 추가(설정자 우선순위 `SetByConsole`) |

## 3. 게임 연결

- `FTDRealmAtmosphere`(TDWorldGen)에 `FogScatteringIntensityMultiplier`·`FogScatteringDistanceMultiplier`·`FogScatteringWeightPowerMultiplier`(기본 1, 필드 = 1).
- `UTDRealmSubsystem`: 시작 시 플러그인 현재 값(=프로젝트 설정)을 필드 기준으로 읽고, 이계 블렌딩 값 = 필드 기준 × 블렌딩된 배율을 바뀔 때만 적용, 종료(Deinitialize) 시 필드 기준으로 복원(콘솔 변수는 에디터 전역이라 PIE 뒤 남지 않게).
- `TDGame.Build.cs`: 비공개 의존 `TDFogScattering`.
- 필드 기준(탑다운 프로젝트 기본값): `Config/DefaultEngine.ini` `[SystemSettings]`의 `r.TDFogScattering.DistanceScale`·`r.TDFogScattering.WeightPower`(8절 실측으로 4·1.5). 3인칭으로 바꾸면 이 두 줄을 지운다.
- 이계 배율 정본: `generate_realm.py` 이계 설정의 `fog_scattering_*_multiplier` 키 + 기존 이계 에셋 값 갱신(에디터 Python, 저장 알림).

## 4. 값 결정 절차

1. 필드 F01 마을(지면 582.9cm → 캐릭터 z 680)과 F03 묘지(366.4 → 516)에서 시간 정지·측정용 AA 끔으로 `DistanceScale` {1, 2, 4, 8, 16} × `WeightPower` {1, 0.7} 쓸기 → 띠별 선명도 손실·휘도비.
2. 목표: 위(먼) 띠 손실 0.15~0.25, 아래(캐릭터 주변) 띠 ≤ 0.10, 휘도비 0.98~1.02. 가까운 곳이 먼 곳보다 덜 흐려지는 기울기 유지(캐릭터 가독성).
3. 이계: 하얀 고원 P01(지면 2438)·별밭 지점에서 같은 기준값으로 측정해 목표 띠에 맞도록 배율 결정. 이계 지형이 순간이동으로 로드되지 않으면 분석식 추정값으로 두고 `proposed` 표시.
4. 사용자 확인 화면: TSR 켠 기본 렌더(시간 정상)로 필드 탑다운 끔/켬, 3인칭 끔/켬, (가능하면) 이계 켬. 촬영기 `HighResShot`과 콘솔 `Shot` 둘 다.

## 5. 검증

오프라인 셰이더 검사, `BuildPlugin`, 공용 `TDGameEditor` 빌드, 블루프린트 점검(가중 지수 포함), PIE에서 이계 진입 시 값 변경·PIE 종료 시 복원(콘솔 변수 읽기), 정적 반박.

## 6. 결과 v1 (2026-10-23, 폐기 — 다른 맵 안개를 흉내 낸 장면 기준, 8절 v2로 대체)

**정한 값**

| 대상 | DistanceScale | WeightPower | Intensity | 정본 위치 |
|---|---|---|---|---|
| 필드(프로젝트 기본) | 32 | 1 | 1 | `Config/DefaultEngine.ini` `[SystemSettings] r.TDFogScattering.DistanceScale=32` |
| 하얀 고원 | ×1.75 → 56 | ×1 | ×1 | `generate_realm.py` + `DA_TDRealm_PaleHeights` |
| 가라앉은 별밭 | ×1/32 → 1 | ×1 | ×0.35 | `generate_realm.py` + `DA_TDRealm_SunkenStarfield` |

- 근거(시간 정지·측정용 AA 끔, 끔 대비 띠별 선명도 손실 위/중간/아래, 목표 위 0.15~0.25·아래 ≤ 0.10·휘도 ±2%): 필드 흉내 k32 0.160/0.116/0.097·휘도 +1.2%(k24 0.125/0.088/0.074, `WeightPower` 0.7은 기울기를 평평하게 해 아래 띠가 먼저 넘침 → 1 유지), 하얀 고원 흉내 k48 0.136/0.098/0.087·k64 0.176/0.130/0.113 → 사이값 k56, 별밭 흉내는 보정 없이도 0.28~0.40으로 포화 → k1·강도 0.35에서 0.213/0.218/0.137(장면 잡음 0.05 제외 시 약 0.16/0.08).
- 흉내 방법: `LV_DarkFantasy_OpenWorld`(실제 지형) 안개 `TDGen_HeightFog`에 각 환경 밀도·감쇠·최대 불투명도·시작 거리·두 번째 층·볼류메트릭 포그 거리를 넣고, 안개면 높이를 "지면 대비"가 같게 옮김(필드 −72.5m, 고원 −54.4m, 별밭 +32m). `L_TDWorld_Main` 직접 측정은 L-editor-58(촬영기 경로 PIE에 월드 콘텐츠 미로드)로 불가.
- 이계 연결 실측(`L_TDWorld_Main` PIE): 하얀 고원 안(가중치 1.0)에서 `DistanceScale` 32→56, 별밭 안에서 `Intensity` 0.35·`DistanceScale` 1, PIE 종료 뒤 32·1로 복원. 에디터 시작 시 ini 값 32 확인.
- 빌드·검사: 플러그인 `BuildPlugin` 3대상 통과, 오프라인 셰이더 20/20, 공용 `TDGameEditor` 빌드 통과, 블루프린트 7검사(가중 지수 포함) OK.
- 사용자 확인 화면(TSR 켠 기본 렌더): `measurements/SF-4/showcase_top_field.png`·`toggle_top_field.gif`(탑다운 필드), `showcase_top_pale.png`, `showcase_third.png`·`toggle_third.gif`(3인칭 자연 안개·SSFS 동등 기본값). `showcase_top_star.png`는 안개 색을 바꾸지 않은 흉내라 화면이 하얗게 덮여 참고용 — 실제 별밭은 어두운 남색 안개(`Saved/WorldGen/PieShots/p3-19_spots/S06_se_slot_end_plaza.png`).
- 한계: 값은 흉내 장면 기준이라 실제 `L_TDWorld_Main` 화면 확인이 남음(월드 콘텐츠 로드 문제 해결 뒤), 아레나 확대(암 ×1.4)는 더 멀어 효과가 조금 커짐(미측정). 3인칭으로 바꾸면 ini 한 줄을 지운다.
- 정적 반박: 치명 0·중요 1(이계 `layout.json`에 배율 키 없음 → `editor_build_realm.py` 재실행 시 1로 회귀 — 두 `Saved/WorldGen/Realms/*/layout.json`에 키 추가로 해소)·경미 6(복원은 현재 값이 내가 적용한 값일 때만, 볼류메트릭 포그 거리 배율을 강도 앞에 적용, 명세 문구 정정 반영 / 비 Win64 타깃 의존·`DistanceScale 32`의 반정밀도 투과율 양자화로 움직임 중 깜박임 가능성은 기록). 수정 뒤 공용 빌드·이계 진입 56·복원 32 재확인.

## 7. 후속 확인 (2026-10-24)

- 게임 타깃: `Build.bat TDGame Win64 Development` 성공(플러그인 모듈 의존 링크 정상) — 반박 6번 해소(Win64 한정).
- 실제 월드 화면: 지점 도구(`pie_capture_spots.py --ids F01,F03,F06`, 효과 끔/켬 두 실행)로도 `L_TDWorld_Main`에 지형이 로드되지 않음 → 10-16 이후 월드 회귀로 판정(L-editor-58 재정정, 근거 `measurements/SF-4/world_main_f01_regression.png`). 실제 월드 확인은 그 회귀가 풀린 뒤. → 2026-10-24 정정: 회귀가 아니라 맵 오선택(실제 필드는 `LV_DarkFantasy_OpenWorld`, L-editor-58). 8절에서 실제 필드·이계로 재측정.
- 움직임 깜박임(반박 7번): 필드 흉내·`DistanceScale 32`·시간 정상·측정용 AA 끔에서 가중치 진단 4장 연속 → 화소 변동 평균 0.82/255, 상위 1% 4.6/255(같은 시간 장면 자체 변화 평균 4.5/255보다 작음), 큰 변동은 바람에 흔들리는 풀 위치뿐 → 양자화 깜박임 우려 낮음(`measurements/SF-4/flicker-metrics.json`).

## 8. 실제 월드 실측 재결정 v2 (2026-10-24, 사용자 위임 "권장으로 진행")

| 대상 | DistanceScale | WeightPower | Intensity | 정본 위치 |
|---|---|---|---|---|
| 필드(프로젝트 기본) | 4 | 1.5 | 1 | `Config/DefaultEngine.ini` `[SystemSettings]` 두 줄 |
| 하얀 고원 | ×10 → 40 | ×1 → 1.5 | ×1 | `generate_realm.py` + `DA_TDRealm_PaleHeights`(+`Saved/WorldGen/Realms/*/layout.json`) |
| 가라앉은 별밭 | ×1.5 → 6 | ×1 → 1.5 | ×1 | `generate_realm.py` + `DA_TDRealm_SunkenStarfield` |

- 방법: `LV_DarkFantasy_OpenWorld` 실제 안개(값 덮어쓰기 없음), 측정용 AA 끔·시간 정지, 폰 = 높이맵 지면 + 150cm(L-editor-59), 끔 대비 띠별 선명도 손실 위/중간/아래·휘도비, 끔·끔 잡음 ≤ 0.003. 목표는 4절과 같다. 계획 `Tools/FogScattering/lookdev_fog_scattering_sf4_field4.json`(필드·이계 쓸기)·`_pale`(고원 큰 값)·`_verify`(게임 연결 그대로). `_field`~`_field3`은 폰 높이 오류 실행이라 필드 경향 참고만.
- v1 값의 실제 필드 결과: k32·p1 F01 0.941/0.857/0.782·휘도 +5.8%, F06 0.959/0.908/0.859·+8.5% → 과함(`metrics_sweep_z629.json`).
- 필드 후보(F01/F03/F06 위 띠·아래 띠): k2·p1 0.176·0.097 / 0.181·0.125 / 0.183·0.119, k3·p1.25 0.169·0.082 / 0.174·0.107 / 0.174·0.101, **k4·p1.5 0.162·0.072 / 0.169·0.091 / 0.167·0.087**, k5·p1.5 0.212·0.096 / 0.220·0.123 / 0.219·0.117 → 세 곳 모두 아래 띠 ≤ 0.10은 k4·p1.5뿐. `WeightPower` > 1은 옅은(가까운) 곳을 더 줄여 기울기를 키운다(v1의 0.7 관찰과 같은 방향).
- 하얀 고원(고원면 위, p1.5): k4까지 위 띠 ≤ 0.010(고원면 위 안개가 매우 옅음). k32 P01/P04/P02 위 0.136/0.151/0.110·아래 ≤ 0.060, k56 위 0.272/0.297/0.220·아래 0.127/0.126/0.113 → 사이값 k40(`metrics_pale.json`).
- 별밭(p1.5): k4 S01/S03 위 0.097/0.090, k6 위 0.168/0.154·아래 0.081/0.079 → k6.
- 게임 연결 검증(콘솔 덮어쓰기 없음, `metrics_verify.json`): F01 0.162/0.103/0.073 +0.9%, F03 0.168/0.123/0.094 +1.0%, F06 0.165/0.117/0.086 +1.2%, P01 0.182/0.124/0.083 +0.8%, P02 0.147/0.074/0.071 +0.7%, P04 0.198/0.129/0.080 +0.8%, S01 0.169/0.101/0.083 +1.0%, S03 0.152/0.090/0.081 +1.1% → 8곳 중 7곳 목표 안, P02 위 띠 0.147(하한 0.15 근소 미달)은 수용. 고원 0.18은 k4로는 나올 수 없어 이계 배율 적용도 함께 확인된다. PIE 이계 진입 콘솔 읽기(중간 배율 0.5·0.75 단계)에서 4×배율 적용, PIE 종료 뒤 4/1.5/1 복원 확인.
- 사용자 확인 화면(TSR 켠 기본 렌더, 지점 도구): `measurements/SF-4/real/showcase_real_world.png`(F01·F06·P01·S01 끔/켬), `toggle_real_field_f01.gif`. 원본 `Saved/WorldGen/PieShots/sf4_real_off|on/`(7곳). 6절 화면(`showcase_top_*.png`·`toggle_top_field.gif`)은 흉내 장면이라 참고용.
- 남은 것: 이계 경계 블렌딩 구간(배율 선형 보간, k 4→40)과 아레나 확대는 미측정. 3인칭으로 바꾸면 ini 두 줄 삭제.

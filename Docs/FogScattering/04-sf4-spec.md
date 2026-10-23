# SF-4 명세 — 탑다운 보정(거리 배율·가중 지수·이계별 강도) + 사용자 확인 화면

갱신: 2026-10-23 claude · 상태: v1 · 상위 D-62(탑다운 보정 채택, 기본 꺼짐 = SSFS 동등)

## 1. 배경 수치

- 게임 필드(`L_TDWorld_Main`) 안개: 밀도 0.005·감쇠 0.03·높이 −6850cm·볼류메트릭 포그 켬(거리 6000cm). 탑다운 시야 지면 9.6~26.6m는 전부 볼류메트릭 포그 구간이라 엔진은 분석 높이 안개를 빼고 볼류메트릭 포그만 쓴다 → SF-1의 `DistanceScale`(높이 안개만)은 필드에서 효과가 없다.
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
- 필드 기준(탑다운 프로젝트 기본값): `Config/DefaultEngine.ini` `[SystemSettings]`의 `r.TDFogScattering.DistanceScale`(4절 측정으로 32, `WeightPower`는 기본 1 유지라 줄 없음). 3인칭으로 바꾸면 이 한 줄만 지운다.
- 이계 배율 정본: `generate_realm.py` 이계 설정의 `fog_scattering_*_multiplier` 키 + 기존 이계 에셋 값 갱신(에디터 Python, 저장 알림).

## 4. 값 결정 절차

1. 필드 F01 마을(지면 582.9cm → 캐릭터 z 680)과 F03 묘지(366.4 → 516)에서 시간 정지·측정용 AA 끔으로 `DistanceScale` {1, 2, 4, 8, 16} × `WeightPower` {1, 0.7} 쓸기 → 띠별 선명도 손실·휘도비.
2. 목표: 위(먼) 띠 손실 0.15~0.25, 아래(캐릭터 주변) 띠 ≤ 0.10, 휘도비 0.98~1.02. 가까운 곳이 먼 곳보다 덜 흐려지는 기울기 유지(캐릭터 가독성).
3. 이계: 하얀 고원 P01(지면 2438)·별밭 지점에서 같은 기준값으로 측정해 목표 띠에 맞도록 배율 결정. 이계 지형이 순간이동으로 로드되지 않으면 분석식 추정값으로 두고 `proposed` 표시.
4. 사용자 확인 화면: TSR 켠 기본 렌더(시간 정상)로 필드 탑다운 끔/켬, 3인칭 끔/켬, (가능하면) 이계 켬. 촬영기 `HighResShot`과 콘솔 `Shot` 둘 다.

## 5. 검증

오프라인 셰이더 검사, `BuildPlugin`, 공용 `TDGameEditor` 빌드, 블루프린트 점검(가중 지수 포함), PIE에서 이계 진입 시 값 변경·PIE 종료 시 복원(콘솔 변수 읽기), 정적 반박.

## 6. 결과 (2026-10-23, 사용자 위임 "모든 작업 권장으로, 묻지 않음")

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
- 실제 월드 화면: 지점 도구(`pie_capture_spots.py --ids F01,F03,F06`, 효과 끔/켬 두 실행)로도 `L_TDWorld_Main`에 지형이 로드되지 않음 → 10-16 이후 월드 회귀로 판정(L-editor-58 재정정, 근거 `measurements/SF-4/world_main_f01_regression.png`). 실제 월드 확인은 그 회귀가 풀린 뒤.
- 움직임 깜박임(반박 7번): 필드 흉내·`DistanceScale 32`·시간 정상·측정용 AA 끔에서 가중치 진단 4장 연속 → 화소 변동 평균 0.82/255, 상위 1% 4.6/255(같은 시간 장면 자체 변화 평균 4.5/255보다 작음), 큰 변동은 바람에 흔들리는 풀 위치뿐 → 양자화 깜박임 우려 낮음(`measurements/SF-4/flicker-metrics.json`).

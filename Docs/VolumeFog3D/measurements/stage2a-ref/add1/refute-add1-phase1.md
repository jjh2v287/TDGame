# 덧붙임 1 (후보 R4) 독립 반박 1단계: 코드와 덧붙임 대조, 2026-11-15

판정: pass (판정을 바꿀 수 있는 어긋남 없음). official_add1/ 폴더는 읽지도 쓰지도 않았다.

## a. 잠금
덧붙임 9953966c…e1ae 는 lock_add1.txt 첫 줄과 같다. 본 등록 0e530367…e90, fog3d_terrain_add1.py 51726afd…, fog3d_gates_s2a_add1.py 38792f92…, fog3d_terrain.py 7a375bb4…, fog3d_gates_s2a.py 067957a5… 도 lock_add1.txt 와 같다(sha256 전체로 비교).

## 대조 표

| 항목 | 덧붙임 문장 | 코드 줄 | 같음/다름 |
|---|---|---|---|
| D1 입력 하나 = 쓸고 가는 구 하나, 캡슐은 나눈 구마다 | D 1 | 물려받은 `TerrainSim._apply_inputs`(terrain.py 268–301)가 입력마다 `_input_frame` 을 불러 틀을 다시 정한다(terrain_add1 73–78) | 같음 |
| D1 가운데 m = (a+b)/2 | D 1 | 76–77. 기울기는 기준 칸 기준 자리, 틀은 지역 자리. 둘이 맞는다 | 같음 |
| D2 틀 기울기: 기둥 기울기를 이중선형 보간, 벽 기둥 값 그대로, 비율 0~1 로 자름 | D 2 | `frame_slope` 63–67: 기둥 가운데 범위로 자른 뒤 `sample_columns`(벽 포함 slope_x/y) | 같음 |
| D3 z̃ = z − Scx(x−mx) − Scy(y−my), 두 끝도 같은 식, 가로 자리 그대로 | D 3 | `_tilted_point` 69–71, `_sample_height` 80–83(z 는 world_height: 면은 두 기둥 평균, 윗면·칸 가운데는 기둥 높이) | 같음 |
| D4 무게·깎기·touched·input_weight_sum 모두 같은 틀 | D 4 | 바람 섞기와 깎기는 `_apply_inputs`(279–292)의 `_sample_height` 를, `touched_columns`(311–323)는 `_input_frame` 뒤 `_sample_height` 를 쓴다. `input_weight_sum` 은 재정의(85–91) | 같음 |
| D5 윗면 목표 = 표본 기둥 기울기 | D 5 | `_input_frame` 이 slope=None 을 돌려주므로 `_wind_target` 은 기둥 slope_x/y 를 쓴다(terrain.py 238–244). R3 설정(목표값 변환 켬) | 같음 |
| D6 평지에서 R3 과 같음 | D 6 | Sc = 0 이면 z̃ = z 가 정확히 성립 | 같음(독립 계산 3) |
| E 2A-G1~G10 을 R4 로 | E | `install` 이 `base.make_sim` 을 바꾼다(55–66). 잠긴 조각 함수는 모듈 전역 make_sim 을 부른다. 자식 프로세스에서도 모듈을 import 하거나 `__mp_main__` 을 다시 실행할 때 install 이 돈다(CLI 로 R4 G8 조각이 자식에서 돌았다. 바뀌지 않았다면 'R4' KeyError 가 났을 것) | 같음 |
| E 평지 값 = R4 를 F0 에서 | E | REF 는 TerrainSimR4 (56–57). G5·G8·정보 ⑦·G11·G12 의 분모가 여기서 나온다. G8 은 piece_g8 이 같은 후보(R4)로 F0 을 돌린다 | 같음 |
| E 평지 대조(2A-G6) = 잠긴 v2 | E | piece_g6 의 `flat_control_g6` 은 core2.Sim 이다(바뀌지 않음) | 같음 |
| 새 지형 넷 | E·G ⑭ | 17–48: S30Y = tan30·y, FOOT30 = x<0 이면 −tan30·x, TOP30 = x<0 이면 tan30·x, S30CURB100 = x<0 이면 tan30·x 아니면 100(거리는 x) | 같음 |
| 2A-G11 걷기 다섯 | E 표 | 42·45: HILL (−50,0) +x 값 S, FOOT30·TOP30 의 up (−300,0) +x / down (+300,0) −x 값 crossing_abs_mean(300 ~ 걸은 거리−50, 옆 ±100), 평지는 같은 출발·방향의 F0 R4 | 같음 |
| 2A-G11 문턱 | E 표 | 228–233: 끝 상태 유한, 0.5 ≤ 비 ≤ 2.0, 계산마다 잰 max_wind 최댓값(98–99, NaN 은 inf) ≤ 1.5 × 평지 | 같음 |
| 2A-G12 (가) 걷기 일곱 | E 표 | 47: S20·S30 up·down, S30 contour (0,−300) +y, S30Y contour = (0,−300) +y 오르막, S30Y ydown (0,+300) −y. 분모는 REF F0 같은 길의 S | 같음 |
| 2A-G12 (나) 여덟 장면 | E 표 | 48: S30·S45·S60 의 k2·k3 1/60초, S60 k2 1/30·1/144초. 분모는 flat_control.max_wind. (ㄱ) 없음 | 같음 |
| 2A-G12 문턱, 분모 0 | E 표 | 249–268: 분모가 0 이하이거나 없으면 inf → 실패, 열다섯 값 모두 ≤ 1e-3 | 같음 |
| F 떨어진 선 모두 종류와 함께 | F | `candidate_status` 286–293 은 실패를 전부 종류와 함께 모은다. `select` 296–302 는 모두 통과일 때만 R4 를 고른다 | 같음(1회차처럼 필수 선 실패를 빠뜨리는 구조 아님) |
| G 정보 ⑪ ⑫ ⑭ | G | 146–148: CLIFF x=−20 (가), ROUGH k3, S30CURB100 x=−20 +y 걷기 + CLIFF 칼, R3·R4. 반대편은 x > 31.25(잠긴 opposite_mask, 거리 x) | 같음 |

## f. 실행 중 바꾸는 부분
- 지형 표, g4_headings·g4_blades, WALK_PATHS 는 `setdefault` 라 새 이름만 더하고 기존 값은 바꾸지 않는다.
- make_sim 은 R4·REF 가 아닌 후보를 원래와 같이 `TerrainSim(candidate_settings(cand))` 로 만든다.
- 뜻이 바뀌는 것은 REF 하나다(1회차에서는 R3 설정을 F0 에서, 지금은 R4 를 F0 에서). R3 의 G5·G12 는 `requested_specs` 가 건너뛴다(199–201). 정보 ⑩(R3 G11)의 평지 값은 R3 F0 조각이라 섞이지 않는다.
- 잠긴 판정 함수(eval_g1~g10)는 그대로 부른다.
- 새 run_piece 는 G4 CLIFF a 의 npz 를 저장하지 않는다(그림용일 뿐이라 판정과 무관).

## e. 독립 계산 (`refute/add1_checks.py`, 출력 `add1_checks_out.txt`)
1. 쓸고 가는 구 하나(세기 3)의 u·v·w 면 무게. 내 식(내 기둥 높이·기울기·이중선형 Sc·z̃ 로 core.band_weight)과 코드(R4 `_apply_inputs` 를 0 장에 적용)를 비교했다. S30(Sc 0.5774, 0), S30Y(0, 0.5774), FOOT30 꺾이는 곳 근처(Sc −0.4809), HILL(0.3012, −0.0924) 모두 최대 차 0.0 이고, Sc 도 비트까지 같다.
2. 창 밖 자르기: HILL 에서 (5000, 30), (−5000, −5000), (창 끝 기둥 가운데 + 3, −3) 세 자리. 코드의 frame_slope 가 내 자르기 + 이중선형 값과 같고, 모서리에서는 그 기둥의 기울기와 같다.
3. 평지 F0·F1: 구 둘(깎기 포함) 12번 계산 뒤 R4 와 R3 의 모든 장이 비트까지 같다(해시 a6b96081b41b9f9f).
4. 새 실행기 CLI 로(`--only G10 --candidates R3 --jobs 1`, `refute/rerun_add1/`) R3_G10_S30 을 돌렸다. 1회차 공식 조각과 values 가 완전히 같다(2.6716e-12). 같은 경로로 R4_G8_all 이 자식 프로세스에서 돌았고, 조각에 적힌 지문 넷이 잠금과 같으며 locked=true 다. 이 조각은 판정 값 하나를 보여 준다(R4 의 2A-G8 비는 모두 1.0).

## 판정에 영향 없는 것
- 1회차에서 적은 eval_g1 의 막힘 같음 미반영은 그대로 물려받는다(R4 는 F0·F1 에서 R3 과 비트까지 같아 영향 없음).

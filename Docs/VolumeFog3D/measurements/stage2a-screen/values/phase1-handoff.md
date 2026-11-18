# 2a-3 다시 찍기 1부 넘김 글 (2026-11-16, 판정 낱말 없음)
장부·정본 설정·Source·다른 에셋은 건드리지 않았다. 빌드 안 함. 에디터는 켜 둠, PIE 없음, 저장 안 된 패키지 0, `TD.Fog3D.Quality` −1.

## ① 고친 도구
- `fog3d_ground_stage.py`: `make_box` 기본·`make_ramp` 재질을 floor 로(모양·자리·태그 불변). 새 길 `retexture_main()`: 요청 파일 `{"retexture_only": true}` 일 때 기존 맵을 열어 조각 재질만 바꾸고 전후 경계 상자·태그·액터 수를 맞춰 보고 저장(`Saved/FluidFX/fog3d_ground_stage_retexture.json`, 복사본 `stage_retexture_report.json`).
- `fog3d_ground_variants.py`: 모든 실행 시작 콘솔 `TD.Fog3D.Quality -1` + 값 읽기, 끝 직전 읽기 이벤트(quality_end), run.json `quality_cvar`; 걷기에 구역 숨김 이벤트(14.4초)와 장 `G15.0`(역할 hidden); 정지 `s_F_high_on`·끔 4개(`s_F_side_off`·`s_F_high_off`·`s_ST_side_off`·`s_CB_side_off`); 묶음 `trial_off`(15개). **편차**: `s_HO_side_on` 을 `static` 묶음에 되살렸다(어제 색인에 있었으나 현재 도구에는 없어, 2부 정지 켬 10개를 맞추려고. 원치 않으면 `all_specifications` 첫 줄 삭제).
- `fog3d_ground_run.py`: 진행 기록을 묶음별 폴더(`progress/<묶음>/`)로, 실행 뒤 run.json `quality_cvar.log_values` 기록.
- `fog3d_ground_report.py`: 걸은 거리 백분율 분모=요청 거리, 품질 값 줄 검사(없거나 2줄 미만이거나 −1 아니면 무효), 정지 표 '평지 옆 분모 비'·'같은 카메라 분모 비'(켬은 켬 분모, 끔은 끔 분모), 걷기 표 기준 장 차·V(기준 장 G13.4 − 숨김 장 G15.0, 같은 영역·평균 절대 차)·Wk÷V·그 평지(W01) 대비 비.
- 자체 점검 4종 통과, `check_sync_loads.py` new=0(도구라 해당 없음, 위반 증가 없음).

## ② 무대 저장 전후
액터 54→54, 땅 태그 28→28, 54개 경계 상자 어제 보고와 최대 차 0, 바뀐 재질 24개(능선·상자·비탈 → 평지 바닥 재질, 벽 재질 남은 것 0). `git status --short Content` 전후 같음(BOM 차이뿐, 이 맵은 미추적 한 줄, `__ExternalActors__` 새 파일 없음).

## ③ 정렬 재확인
어제와 같은 19자리 읽기: 유효 14, 무효 5(평지 0 + 구멍 4, 어제와 같음). 차 최대 0.022cm(오르막 a), 계단 단 0.000, 턱 −0.02. 전부 어제와 동일. `readback/readback_table.json`.

## ④ 시운전 값(trial_off, 15개 모두 유효·시도 1회, 거부·경고·ensure 0)
정지 끔(켬−숨김 밝기 차 255 기준 / 평지 옆 분모 비 / 같은 카메라 분모 비 / 어제 낮은 품질 값):
- F side 23.35 / 1.000 / 1.000 / –; F high 13.31 / 0.570 / 1.000 / –
- S30 side 9.98 / 0.427 / 0.427 / 9.86; S30 high 5.91 / 0.253 / 0.445 / 5.83
- CL side 12.01 / 0.514 / 0.514 / 11.40; CL high 7.71 / 0.330 / 0.580 / 7.50
- ST side 17.69 / 0.758 / 0.758 / –; CB side 18.98 / 0.813 / 0.813 / –
걷기 끔(Wk / 기준 장 차 / V / Wk÷V / Wk÷V의 W01 대비 / 어제 Wk·기준 장 차):
- W01 5.83 / −2.63 / 19.40 / 0.301 / 1.00 / 6.50·−2.88
- W02 0.20 / 0.02 / 0.22 / 0.909 / 3.02 / 0.26·0.03
- W03 0.19 / −0.00 / 0.11 / 1.676 / 5.57 / 0.21·−0.00
- W04 0.12 / −0.01 / 0.08 / 1.465 / 4.87 / 0.14·−0.01
- W08 7.17 / −4.66 / 20.08 / 0.357 / 1.19 / 8.26·−5.27
- W09 0.20 / 0.01 / 0.13 / 1.471 / 4.89 / 0.22·−0.00
달리기 끔 R02 side: 앞뒤 장 차 중앙 2.70·최대 3.63(최대/중앙 1.3). 달리기는 기준 장이 없어 V 등은 없음. 걸은 거리 100.1~101.2%.
(재질을 맞추기 전·후 정지 값이 거의 같다: 끔 상태 안개 켬−숨김 차는 땅 재질에 덜 민감.)

## ⑤ 품질 확인 근거
15개 모두 로그에 `TD.Fog3D.Quality = "-1"` 세 줄(시작 설정·읽기·끝 읽기, 전부 −1), run.json `quality_cvar.log_values` [-1,-1,-1]. 걸음 손확인: W02 끔 제출 693 ÷ 계산 켠 게임 시간 약 10.5초(시작~1.0초 + 2.9~12.4초) ≈ 초당 66(어제 낮은 품질 350 ≈ 초당 33). 정지 s_F_side_off 제출 1203. 주의: W01 은 어제도 이번에도 제출 수가 다른 걷기의 정확히 2배(1367, 어제 705)로 원인 미확인(첫 걷기 실행이라 요약이 앞 실행 걸음을 포함하는지 확인 필요, 값 표의 W01 분모에는 영향 없음으로 보이나 미검증).

## ⑥ 겪은 문제·교훈 후보
- Bash 도구 heredoc 안의 `"\n"` 이 실제 줄바꿈으로 바뀌어 파이썬 문법 오류가 났다(Edit 도구로 고침). 도구 파일을 쓸 때 Edit 사용.
- 빈 콘솔 변수 이름 한 줄이 로그(`TD.Fog3D.Quality = "-1"  LastSetBy: Console`)에 찍힌다 — 확인됨.

## ⑦ 2부(켠 실행 30개) 명령 (PowerShell, 에디터 켜짐, `python -B`)
1. `python -B Tools/FluidFX/fog3d_ground_variants.py --batch main` (정지 켬 10·걷기 12·옆 걷기 5·달리기 3 + 끔 4; 켬 30 만 쓰려면 `--only` 로 켬 이름)
2. `python -B Tools/FluidFX/fog3d_ground_run.py --batch static walk walk_side run` (진행 기록 `Saved/FluidFX/fog3d_ground/progress/static+walk+walk_side+run/`)
3. `python -B Tools/FluidFX/fog3d_ground_report.py --batch trial_off static walk walk_side run` (끔 15개는 이미 찍힘, 같은 카메라 분모 열을 위해 같이 넣는다)
어제 자료: `Saved/FluidFX/fog3d_ground_q0/`. 전체 표: `trial_off-summary.md`. 켠 실행은 아직 한 번도 찍지 않았다.

## ⑧ 추가 확인 (W01 제출 걸음 두 배)
- 보존: 시운전 W01 끔 = `Saved/FluidFX/fog3d_ground/w_W01_back_off_first/`, 다시 돌린 것 = `w_W01_back_off/`(보고 도구가 읽는 쪽). W02 끔은 덮어씀(시운전 값은 ④절에 있음).
- 로그: W01(시운전) PIE 생성 직전 마지막 `TD.Fog3D.Mode` = 2(앞 실행 s_CB_side_off, 03.04.10), 시운전 W02 직전 = 0(03.05.01, W01 의 끝). 다시 돌릴 때 W02 직전 앞 실행은 달리기 끔(Mode 2 로 끝남)이었고 W01 직전은 W02(Mode 0).
- 값(품질 로그 전부 −1, 전부 유효):
  - W01 시운전: 제출 1367, Wk 5.83, 기준 장 차 −2.63, V 19.40, Wk÷V 0.301
  - W01 다시(앞이 Mode 0): 제출 694, Wk 5.84, 기준 장 차 −2.59, V 19.40, Wk÷V 0.301
  - W02 시운전: 제출 693, Wk 0.20, 기준 장 차 0.02, V 0.22, Wk÷V 0.909
  - W02 다시(앞이 Mode 2): 제출 1367, Wk 0.20, 기준 장 차 0.01, V 0.21, Wk÷V 0.957
- 해석 없이: 제출 수는 앞 실행의 Mode 를 따라 두 배가 되고(W01 시운전·W02 다시), 자국·V 는 두 경우 모두 거의 같다.

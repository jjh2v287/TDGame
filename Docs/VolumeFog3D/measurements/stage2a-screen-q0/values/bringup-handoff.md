# 2a-3 화면 확인 첫 실측 넘김 글 (2026-11-15 22:30~23:00, 에디터 작업자)

장부는 쓰지 않았다. `Source/`·등록 문서·문턱은 고치지 않았다. 합격·불합격은 적지 않는다(값과 그림만). 에디터는 띄워 둔 채(PID 17104, 열린 레벨 `LV_TDFog3DGroundStage`, PIE 꺼짐, 저장 안 된 패키지 없음).

## 1. 만든 에셋과 git 상태

- 재질 `/Game/FluidFX/Materials/M_TD_Fog3DVolumeGround`(땅 변형, include `/TDFluidGpu/Private/TDFluidGpuFog3DDensityGround.ush`, Custom 코드 sha256 a07a8137…, 보고 `Saved/FluidFX/fog3d_material_ground.json`). 통계 VS 272·PS 86·샘플러 2. 높이 텍스처 기본값 `/Engine/EngineResources/Black`, 샘플러 형식 경고·MID 경고 없음. 로그의 `Failed to compile Material … Volume materials must use an Additive blend mode` 한 줄은 블렌드 모드를 바꾸기 전 중간 상태 경고로 무해(적중 **L-editor-65**). 요청 파일 `Saved/FluidFX/fog3d_material_request.json` 은 실행 뒤 지웠다.
- 무대 `/Game/FluidFX/Maps/LV_TDFog3DGroundStage`(액터 54, 태그 28, 경계 상자 불일치 없이 한 번에 저장, 보고 `Saved/FluidFX/fog3d_ground_stage.json`). 외부 액터 폴더는 생기지 않았다(월드 파티션 아님).
- `git status --short` 의 `.uasset`·`.umap`: 새로 생긴 것은 위 둘뿐(`LV_TDFog3DStage.umap`·`M_TD_Fog3DVolume.uasset` 은 시작 전부터 미추적). 해시 불변: `M_TD_Fog3DVolume` 902CDF30…, `LV_TDFog3DStage` B87A9182…, 정본 `TDFluidPresetTuning.json` FEF5BA34…(시작 때와 같음).
- 코드 확인: `TD.Fog3D.GroundReadback`·`TD.Fog3D.GroundForceRecapture` 가 에디터에 올라와 있음(DLL 22:18/22:21 빌드).

## 2. 가정 열셋 확인 표 (screen-tools-handoff ⑥)

| # | 가정 | 결과 | 근거 |
|---|---|---|---|
| 1 | 기울인 판 pitch 방향·경계 상자 | 맞음 | 스크립트 중단 없음, 되읽기에서 30도 판 높이가 무대 식과 0.02cm 안 |
| 2 | 카메라 속성 이름, 구역 높이 1000 이 안개를 안 자름 | 맞음 | 카메라 전환 정상. S30 high 에서 꼭대기(450) 위 안개 층이 잘리지 않음 |
| 3 | 폰 걷기 한계 | 맞음(W01·W02 만 봄) | 걸은 거리 W01 607·W02 606·W02 옆 607cm(요청 600). 40도·턱·계단은 아직 안 찍음 |
| 4 | 순간이동 높이·알려진 ensure | 맞음 | W01 에 알려진 내비 ensure 2줄(L-editor-105 꼴), 새 ensure 0 |
| 5 | 계산 끈 기준 장이 땅 따라가기 모양 유지 | 맞음 | W02 기준 장(13.4·13.9초)에도 비탈 위 폰 둘레 안개가 남음. '정지 마지막 장 − 기준 장' W01 −2.63, W02 −3.23(비 1.23) |
| 6 | 뒤 카메라 pitch −12 + 비탈각 | 부분 어긋남(고치지 않음) | W02 의 G4.0(평지 출발 직후, pitch +18) 장은 카메라가 바닥 밑으로 들어가 아래 1/3 이 검다. 숫자에 쓰는 정지 장 6.1 이후는 정상(폰·땅이 평지와 같은 자리). 움직임 장은 정보용이라 그대로 둠 |
| 7 | 땅 재질 비동기 교체 | 문제 안 보임 | W02 G4.0 Wk 57.8 은 6번(카메라 바닥 밑) 탓 |
| 8 | 구역 숨김을 call 로 | 맞음 | 정지 실행 모두 켬/끔 장이 갈림 |
| 9 | 구멍 칸 모습 | **어긋남 → 고침** | `side` 카메라(z=150)가 안개 층(240) 안이라 구멍이 전혀 안 보임 → 정지 묶음의 HO 를 `high` 카메라로 바꿈(아래 3절). high 에서는 구멍 테두리가 안개 아래 흐리게 보이고, 구멍 위 안개는 둘레와 같은 높이로 떠 보임 |
| 10 | 격자 세계 원점 정렬 | 맞음 | 되읽기 창 최소 칸이 모두 16의 배수, 비탈 텍셀 가운데 높이가 0.02cm 안(반 텍셀 어긋남이면 30도에서 4.5cm) |
| 11 | 비용은 예약 촬영 없이 | 맞음, 그리고 중요 | 예약 촬영(0.1초 간격 HighResShot) 중에는 변환 지연이 7·10·46 프레임까지 늘어남(R01 순서 로그). 그래서 순서도 촬영 없는 비용 실행 로그로 쟀다(5절) |
| 12 | 정본 해시 기록 | 맞음 | 실행 기록 sha256 = fef5ba34… |
| 13 | 정지 장 영역 비율 | 대체로 맞음 | 모든 정지에서 켬−끔 차가 잡음의 180~900배. 단 CB·ST side 카메라(z 150·200)도 안개 층 안이라 가까운 안개 장막이 화면 대부분을 덮는다(값은 F 의 0.80·0.71). 평지 F side 도 z 250 으로 층 바로 위. 비교 가능성을 위해 고치지 않았다 — 감독 판단 |

## 3. 도구를 고친 곳

- `Tools/FluidFX/fog3d_ground_variants.py`: `STATIC_MAIN_CAMERA_BY_STATION = {"HO": "high"}` 추가, 정지 기본 묶음의 HO 실행이 `s_HO_high_on` 이 됨(실행 수 8 그대로). 까닭: 가정 9. 자체 점검(variants·run·report) 통과. 맵은 다시 저장하지 않았다(카메라 액터는 이미 맵에 있음).
- 그 밖의 도구·맵은 고치지 않았다. 일회용 스크립트는 `screen/readback/`(되읽기 계획 생성·캡처 컴포넌트 질의)에 있다.

## 4. 첫 촬영 그림과 본 것 (실행 10개, 모두 유효·Fog3D 경고 0·RDG 오류 0·거부 0)

대표 여섯 장:
1. `Saved/FluidFX/fog3d_ground/s_S30_high_on/capture/grid.png` — 위 두 장(켬)에서 안개가 30도 판 윗면·꼭대기 평지를 얇게 덮고 판의 모서리 윤곽이 그대로 보인다. 평지 높이(0)에 깔린 평평한 층이 비탈을 파묻는 모습이 아니다 → **비탈을 따라 깔려 보인다**.
2. `Saved/FluidFX/fog3d_ground/s_S30_side_on/capture/grid.png` — 옆에서 보면 능선 윤곽(오르막·꼭대기·내리막) 바로 위에 안개 띠가 같은 모양으로 번진다. 판 밑 삼각 그늘이 켬에서 뿌옇게 보이는데, 카메라와 능선 사이 평지 층(240cm) 때문인지 판 밑 안개인지는 이 구도로 못 가른다(삼각 꼭대기 ≈ 217cm < 240).
3. `Saved/FluidFX/fog3d_ground/w_W02_back_on/report/strip.png` — 30도 오르막. 정지 장에서 폰 둘레 자국이 평지(W01)와 비슷하게 보이고, 기준 장에도 비탈 위 안개가 남는다. Wk 4.88(W01 5.84, 비 0.84).
4. `Saved/FluidFX/fog3d_ground/w_W02_side_on/report/strip.png` — 옆 카메라. 비탈 위 안개는 보이나 자국 대비가 작다(Wk 0.86, 잡음의 4배).
5. `Saved/FluidFX/fog3d_ground/s_HO_high_on/capture/grid.png` — 구멍 위 안개는 둘레 평지와 같은 높이로 덮여 구멍이 흐리게만 보인다.
6. `Saved/FluidFX/fog3d_ground/s_ST_side_on/capture/grid.png` — 계단 착지(200) 위에 안개 층이 혹처럼 올라앉은 윤곽이 보인다.
(구멍 칸 고치기 전 그림: `Saved/FluidFX/fog3d_ground/s_HO_side_on/capture/grid.png`. 숫자 표: `Saved/FluidFX/fog3d_ground/report/summary.md`)

정지 켬−끔 밝기 차(255 기준, 정보): F side 23.28 / S30 side 11.65 / S30 high 10.85 / ST side 16.50 / CB side 18.74 / HO side 22.31 / HO high 13.48.

## 5. 2A-S2 캡처 정렬 (PIE, 주 렌더러 안 캡처)

주 렌더러 안 확인: PIE 중 구역의 캡처 컴포넌트 `render_in_main_renderer=True`, `r.SceneCapture.AllowRenderInMainRenderer=true`, 보여 줄 목록 28 액터(태그 액터 수와 같음, 태그 없는 구멍 바닥 빠짐), 직교 6000cm. 요청→변환 2프레임. 자리마다 텍셀 가운데 좌표로 읽었다(로그 값은 소수 둘째 자리까지라 차 해상도 ±0.005cm). 원자료 `screen/readback/readback_table.json`, 실행 `screen/readback/variants.json`.

| 자리 | 세계 (x, y) | 무대 높이 | 읽은 높이 | valid | 차 | 가장 가까운 모서리(cm) |
|---|---|---|---|---|---|---|
| 평지 F | (195.31, 7.81) | 0 | −9999 | 0 | 잴 수 없음 | 100+ |
| 계단 3단 | (−304.69, 6007.81) | 60 | 60.00 | 1 | 0.000 | 15.5 |
| 계단 5단 | (−226.56, 6007.81) | 100 | 100.00 | 1 | 0.000 | 13.5 |
| 계단 8단 | (−101.56, 6007.81) | 160 | 160.00 | 1 | 0.000 | 18.5 |
| 계단 착지 | (648.44, 6007.81) | 200 | 200.00 | 1 | 0.000 | 100+ |
| 턱 30 윗면 | (5195.31, 6007.81) | 30 | 29.98 | 1 | −0.020 | 45.5 |
| 턱 60 윗면 | (6007.81, 6007.81) | 60 | 59.98 | 1 | −0.020 | 42.5 |
| 턱 100 윗면 | (6804.69, 6007.81) | 100 | 100.00 | 1 | 0.000 | 45.5 |
| 절벽 위 | (5304.69, 7.81) | 450 | 450.00 | 1 | 0.000 | 100+ |
| 30도 오르막 가운데 a | (−6898.44, 7.81) | 104.492 | 104.47 | 1 | −0.022 | 100+ |
| 30도 오르막 가운데 b | (−6492.19, 7.81) | 339.040 | 339.03 | 1 | −0.010 | 100+ |
| 능선 꼭대기 | (−5992.19, 7.81) | 450 | 450.00 | 1 | 0.000 | 100+ |
| 30도 내리막 | (−5304.69, 7.81) | 221.766 | 221.75 | 1 | −0.016 | 100+ |
| S20 착지 | (6601.56, −5992.19) | 300 | 300.00 | 1 | 0.000 | 100+ |
| S40 착지 | (−5398.44, 6007.81) | 450 | 450.00 | 1 | 0.000 | 100+ |

- 유효 14곳 최대 |차| **0.022cm**(30도 판), 평평한 면은 0~0.02.
- 알아 둘 것(판정은 감독): ① 평지(높이 0)는 `valid=0` — 되읽기는 값이 `GroundMissingHeightCm`(기본 0)이면 무효로 찍으므로 땅 높이 0 과 '땅 없음'을 못 가른다(probe-cpp 넘김 글이 예고한 한계). ② 계단 단은 깊이 40cm 라 등록의 "모서리에서 31cm 넘게"를 만족하는 기둥이 없다(가운데가 13.5~18.5cm). 등록 자리 목록과 조건이 서로 맞지 않는다.
- **구멍 칸(정보, 감독 덧붙임)**: (7.81,−5992.19)·(−492.19,−5601.56)·(507.81,−6398.44)·(304.69,−5492.19) 네 곳 모두 `height_cm=-9999.00 valid=0`. 창 안(최소 칸 (−304,−576), 크기 384)이므로 '창 밖'이 아니다 → 값이 못 찾음 값(0)이거나 유한하지 않은 값. 얕은 양수·음수 높이는 나오지 않았다(그런 값이면 valid=1 로 찍힌다). 캡처는 **주 렌더러 안**이었다(위 확인, 같은 PIE 세션 종류). 기대 "못 찾음 값으로 읽힌다"와 어긋나는 증거 없음. 단 0 과 NaN 은 이 명령으로 구분되지 않는다.

## 6. 2A-S4 캡처 순서

조건: 촬영 없는 비용 실행(`cost/plan_walk_verbose.json`: 초속 520 걷기 3000cm, 블록 8개 + 예열, 블록마다 조절 표 다시 읽기·출발점 순간이동) 의 Verbose 로그 `screen/cost_walk_log_slice.txt`, 집계 `screen/order_costwalk.json`. 설정 지연 2.

| 항목 | 품질 2(창 384) | 품질 0(창 256) |
|---|---|---|
| 걷는 동안 요청→변환 지연(프레임) 분포 | 2: 23, 3: 4 | 2: 9, 3: 10, 4: 7 |
| 최대 지연 | 3 | 4 |
| 16칸 창 이동 뒤 보류 걸음 | 0 | 0 |
| 블록 시작(조절 표 다시 읽기, 사유 settings/enter) 보류 | entering 2 걸음씩(블록마다) | entering 2 걸음씩 |
| 출발점 순간이동(3000cm 뒤로) 보류 | 두 품질 합쳐 waiting_capture 2 걸음 × 2회, teleport 2 걸음 × 1회(품질별로 가르지 않았다) | (왼쪽에 합침) |

- 걷는 중 창 이동 재캡처 51회 사이 간격 중앙 35.5 프레임. 보류 20 걸음은 모두 블록 시작·순간이동 때다.
- 품질 0 의 3~4 프레임 지연은 변환이 다음 걸음까지 기다리기 때문으로 보인다(강제 재캡처 블록에서 변환 열이 0 아닌 행: 품질 2 196/240, 품질 0 92/240 = 걸음이 2~3 프레임에 한 번) — 추정, 코드로 확인 안 함.
- 예약 촬영이 든 순서 묶음(`--batch order`, `Saved/FluidFX/fog3d_ground/r_R01_back_on_log`·`w_W02_back_on_log`)은 촬영 프레임 사이에서 지연 7·10·46 프레임, 창 이동 직후 waiting_capture 보류 2가 나왔다(R01 시작 직후 순간이동 1건 포함). HighResShot 프레임 때문으로 보여 판정 자료로는 위 표를 권한다. `fog3d_ground_cost.py order` 결과는 `screen/order.json`.
- 끝 요약 줄 `deferred=` 는 비용 실행 로그에서 읽지 못함(요약 줄이 PIE 끝에 찍혔는데 그 PIE 는 다음 실행이 `--fresh-pie` 로 닫음 — 로그 조각에 없음). 줄 단위 합계 20.

## 7. 2A-S3 캡처 비용 (GPUTime ms, 앞 60행 버림, 블록당 240행)

조건: 뷰포트 **1612×715**(계획의 1749×924 아님 — 에디터 창을 맞추지 않았다), r.ScreenPercentage 113.3, 격자 8/128, r.TDFogScattering 1. 기계: 에디터 외 무거운 GPU 일 없음(Chrome·다른 Claude 세션은 떠 있음). 원자료 `screen/cost_walk_summary.json`·`screen/cost_forced_summary.json`, CSV `Saved/Profiling/CSV/Profile(20261115_2251…–2254…)` 8개(걷기)와 그다음 8개(강제).

| 품질 | 다시 찍는 프레임: 강제 − 보통(평균) | p95 차 | 같은 조건 블록 간격(강제 / 보통) | 걷는 동안: 켬 − 끔(평균) | p95 차 | 같은 조건 블록 간격(켬 / 끔) |
|---|---|---|---|---|---|---|
| 2 | **0.145** | 0.184 | 0.024 / 0.058 | **0.098** | 0.129 | 0.227 / 0.042 |
| 0 | **0.063** | 0.070 | 0.013 / 0.002 | **0.006** | −0.015 | 0.028 / 0.011 |

- 강제 블록은 매 프레임 캡처 + 걸음 프레임마다 변환·다시 굽기(변환 GPU 열 0.015~0.016ms, 품질 2 에서 82% 프레임, 품질 0 에서 38%). 게임 스레드 `GroundCaptureRequest` 가 강제 때 프레임당 0.13ms(CPU).
- 품질 2 걷기의 0.098 은 켬 블록끼리의 간격(0.227, 첫 켬 블록 13.575 가 튐)보다 작다. 걷기 중 캡처 관련 열은 240행 중 7행만 0 이 아니다(요청 평균 0.004ms).
- `GPU/TDFog3DGroundCapture` 열은 없다(probe-cpp 넘김 글대로 엔진이 따로 안 냄).

## 8. 겪은 문제와 교훈

- 적중: L-editor-65(볼륨 재질 중간 상태 경고). 그 밖의 명령 실패 없음(보고 도구에 실행 이름을 위치 인수로 줬다가 `--runs` 로 고침 — 사용법 문제).
- 교훈 후보(감독 판단): ① 촬영 도구의 0.1초 간격 HighResShot 중에는 주 렌더러 안 캡처의 변환이 수십 프레임 늦어진다 — 캡처 순서·지연은 촬영 없는 실행으로 잰다. ② 되읽기 `valid=0` 은 높이 0 땅과 땅 없음을 못 가른다. ③ 정지 카메라가 안개 층 높이(240) 안에 있으면 가까운 장막이 화면을 덮어 구조물이 안 보인다. ④ `pie_lookdev_fluid.py` 는 `tuning_reload` 를 쓰면 끝에 정본을 다시 읽어 땅 자원을 놓으므로, 캡처 컴포넌트 상태를 보려면 정착 시간 동안 다른 원격 호출로 질의해야 한다. ⑤ PIE 뷰포트 크기는 `GameplayStatics.get_player_controller(w,0).get_viewport_size()` 로 읽힌다(`WidgetLayoutLibrary.get_viewport_size` 는 PIE 전 (1,1)).

## 9. 다음 작업자 명령 (PowerShell, `C:\Project\TDGame`, 에디터 앞창 유지 L-editor-06)

```
python -B Tools/FluidFX/fog3d_ground_variants.py --batch main
python -B Tools/FluidFX/fog3d_ground_run.py --batch static follow_off walk walk_side run
python -B Tools/FluidFX/fog3d_ground_report.py --batch static follow_off walk walk_side run
python -B Tools/FluidFX/fog3d_ground_variants.py --batch walk_off run_off
python -B Tools/FluidFX/fog3d_ground_run.py --batch walk_off run_off
python -B Tools/FluidFX/fog3d_ground_report.py --batch static follow_off walk walk_side run walk_off run_off
```
- `--only` 으로 만든 변형이 색인을 덮으므로 반드시 `--batch main` 부터. 이미 찍은 10실행은 같은 이름이면 다시 찍힌다(`s_HO_side_on` 은 이제 묶음에 없음).
- 선택: `--batch walk_p2 static_p2`(같은 세 단계). 2A-S3 를 계획 뷰포트로 다시 재려면 에디터 창을 1749×924 뷰포트로 맞춘 뒤 `python -B Tools/FluidFX/pie_lookdev_hires.py --variants Saved/FluidFX/fog3d_ground/cost/plan_forced_verbose.json --level /Game/FluidFX/Maps/LV_TDFog3DGroundStage --fresh-pie --stop-pie` → `python -B Tools/FluidFX/fog3d_ground_cost.py summarize --latest 8 --labels q2_normal_a q2_forced_a q2_forced_b q2_normal_b q0_normal_a q0_forced_a q0_forced_b q0_normal_b` (걷기는 `plan_walk_verbose.json`, 라벨 `q2_on_a q2_off_a q2_off_b q2_on_b q0_on_a q0_off_a q0_off_b q0_on_b`).
- 맵 열기 전 내비 대기가 실행기 밖에서는 약 2분 걸린다(`pie_lookdev_fluid.py` 를 직접 부를 때).

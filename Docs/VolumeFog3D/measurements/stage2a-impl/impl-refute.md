# 2a 구현 독립 반박 (2A-I1·I2·I3) — 2026-11-15 claude 반박자

판정: **합격** (I1·I3 통과, I2 는 1단계 등록 예외 그대로 통과 — 아래 3절 문구 문제 하나 감독 판단 필요). 고친 코드 없음. 대상 파일 sha256 시작 = 끝.

## 1. 잠금 확인

| 항목 | 결과 |
|---|---|
| 등록 문서 `prereg-2a-impl.md` sha256 | 415526…fcc67 = lock_impl.txt |
| 숫자 파일 sha256 | 13374142…6d07d = lock_impl.txt. default relative 0.002(세 묶음), slope 1e-6, 틀 자리 1e-3cm, 틀 기울기 1e-5, dumps 별 덮어쓰기 0개 |
| 기준 덤프 목록 | manifest 229파일 sha256 모두 일치, 목록 sha256 `ad6f20f0…` 규칙('이름 바이트수 sha256' 이름순)대로 다시 계산해 일치, 덤프 npz 38개 |
| 재생 파일 | step2-handoff 5절의 22개(json 12 + ground.bin 10) sha256 모두 일치. 기준 모의 코드 지문 7개 일치 |
| `fog3d_compare_s2a.py --self-test` | 10개 모두 통과, 끝 코드 0 |
| 도구 대 등록 2절 ①~⑤ | ① fluid_mismatch 0 ✓ ② v2.group_diffs 묶음별 ≤ 숫자 ✓(기준 0 묶음은 GPU 정확히 0 — 자체 점검 확인) ③ ground_h uint32 비트 비교, wall 같음, slope ≤1e-6 ✓ ④ 틀 자리 열(0,1,2,3,4,5,6,8,9,12,13) ≤1e-3, 기울기 열(10,11) ≤1e-5 ✓ ⑤ NaN 검사·GPU 덤프 없음 실패 ✓ |
| **불일치(잠재)** | ④ "틀 파일은 구가 있는 덤프마다 있어야 한다" 인데 도구는 GPU 틀 파일이 없으면 **통과**로 적고 건너뜀(자체 점검이 그 동작을 '통과'로 확인함, 줄 160-164). 기준 쪽 기둥·틀 파일이 없으면 조용히 건너뜀(줄 131-132, 155-156). 이번 보고서들은 건너뜀 0개라 결과에는 영향 없음. 틀 열 7·14·15 는 판정 안 함(내 계산으로 차 0) |

## 2. 2A-I1 독립 재실행 (D3D12)

| 항목 | 결과 |
|---|---|
| `build_and_test.py --rhi offscreen --filter TDGame.FluidFXRhi.Fog3D --label s2a-refute-base` | 25/25 통과, D3D12 SM6, 덤프 38개 새로 씀 |
| 대조 `i1-rerun.json` | 끝 코드 0, 검사 470, 실패 0, 틀 건너뜀 0, 가장 가까운 것 s2a_box_on_slope__end wind 비 0.00831792 (주장 D3D12 보고와 같은 값 — 실행 사이 비트 결정적) |
| 내 계산 `indep.py`(대조 도구 import 없음, 원시 파일만) | 38덤프 유체 불일치 0. 묶음별 상대 L2 최대 wind 1.66e-5(box_on_slope end), disp 3.12e-6(cliff_at_edge end), fill 1.32e-6(frame_at_edge end) — 도구 값과 같음. 기둥: ground_h 비트 차 0, wall 차 0, 기울기 차 ≤1.2e-7. **기둥을 ground.bin 에서 내 식(f32, 벽 단차 30cm)으로 직접 다시 구워 GPU 기둥과 비교**: 38덤프 모두 높이 비트 0·wall 0·기울기 ≤1.2e-7. 틀: 자리 ≤1.22e-4cm, 기울기 ≤7.0e-7 |
| D3D11 | 다시 돌리지 않음(주장 보고서만 읽음: 470/0, 비 0.00732) |

## 3. 2A-I2 (1단계 뒷걸음)

| 명령 | 끝 코드 | 결과 |
|---|---|---|
| `fog3d_compare.py --gpu …/gpu --ref …/ref_v2 --shake …/ref_v2_shake --numbers stage1d-ref/prereg-1d-numbers.json --extras`(1d 계약 125줄 꼴) | 0 | 86검사 모두 통과 |
| `fog3d_compare_add1.py --gpu …/gpu_add1 --ref …/ref_add1_v2 --numbers prereg-1d-numbers.json`(1d 계약 126줄 꼴) | **1** | 12검사 중 실패 1 = ADD-a(넷째 단계 1+g 평균, 기준·GPU 모두 NaN, 잴 칸 0개). 1d 판정(progress.md 564줄)에서 이미 빼기로 한 예외 그대로. 나머지 11 통과 |
| `fog3d_compare_v2.py e1 / e2 / e3` | 0 / 0 / 0 | 558 / 78 / 10 검사 실패 0 |
| `GroundOffMatchesStage1` | 통과 | |
| 참고 | README 의 인수 없는 꼴(1a 숫자·옛 기준)은 22개·6개 실패 — 1d 에서 기준 모의가 바뀌었기 때문이며 1d 등록 꼴로 돌려야 함 |

**감독 판단 필요(문구)**: 등록 2A-I2 는 "세 도구 모두 끝 코드 0(1단계 등록 기준 그대로)". 글자 그대로면 add1 끝 코드 1 이라 미충족, "1단계 등록 기준 그대로"를 1d 예외 포함으로 읽으면 통과. 코드 결함은 아님.

## 4. 2A-I3 판별력 — 변이 일곱 (D3D12, 하나씩, 매번 원복 후 sha256 확인)

원복 sha256(세 파일 모두 매번): usf a9c5884f…, Common.ush 49acbf22…, cpp b1fb41f6… = 시작값.

| 변이 | 넣은 자리 (TDFluidGpuFog3D.usf) | 단위 시험 실패 | 대조(실패 수 / 최대 비 = 차 ÷ 한계) | 판정 |
|---|---|---|---|---|
| (가) 기울인 높이 부호 | :975 `z - sx·(x-mx) - sy·(y-my)` → `+`,`+` | GroundInputFrames, GroundUniformSlopeNearFlat | 79 / box_on_slope end wind 499, 틀 자리 9.02cm(9020) | 잡힘 |
| (나) 윗면 목표에 틀 기울기 | :1036 `ColumnSlope` → `Frame.MidpointCmFrameSlope.zw` | 없음 | 30 / hill_run_window mv4 disp 113 (curb30·frame_at_edge·hill) | 잡힘(대조만) |
| (다) 면 높이 한쪽 기둥 | :945·:949 `0.5*(자기+이웃)` → `자기` | GroundUniformSlopeNearFlat | 62 / box_on_slope end wind 422 | 잡힘 |
| (라) 벽 `>=` | :210 `>` → `>=` | GroundBakeColumns | 12 / curb30_cross: wall 128기둥, 유체 1024칸, disp 430 | 잡힘 |
| (마) 고리 안 읽음 | :184 `LocalColumn` → `clamp(LocalColumn,0,Res-1)` | GroundBakeColumns, GroundConvertCapture, GroundInputFrames | 63 / slope_x 0.471(47만), 틀 기울기 7128, cliff_at_edge fill 201 | 잡힘 |
| (바) x·y 기울기 맞바꿈 | :242 `(H,SlopeX,SlopeY,…)` → `(H,SlopeY,SlopeX,…)` | GroundBakeColumns, GroundInputFrames, GroundUniformSlopeNearFlat | 162 / slope 2.11(211만), frame_at_edge wind 602 | 잡힘 |
| (사) 틀 기울기 자리 안 자름 | :1119 `ClampedCm = clamp(...)` → `MidpointCm` | 없음 | 13 / frame_at_edge 한 장면만: 틀 기울기 0.0224(2241), 틀 자리 8.6, wind 5.47 | 잡힘(대조만, 한 장면) |

일곱 모두 잡힘. 약한 곳: (나)·(사)는 단위 시험이 못 잡고 재생 대조만 잡는다. (사)는 s2a_frame_at_edge 하나뿐이며 장 차이 여유는 5.5배(틀 비교는 2241배).

## 5. 등록 밖 변이 (정보)

| 변이 | 자리 | 단위 시험 | 대조 | 판정 |
|---|---|---|---|---|
| x1 깎는 양만 기울이지 않음 | :1055 `TiltFog3DSamplePositionCm(CellWorldCm, Frame)` → `CellWorldCm` | 없음 | fill 10개 실패 / frame_at_edge w70 fill 104 (hill·slope_curb도) | 잡힘(대조만) |
| x2 윗면 목표 기울기를 옆 기둥(+1,0)에서 | :970 `LocalCell.xy` → `LocalCell.xy + int2(1,0)` | 없음 | 30 / frame_at_edge end fill 169 | 잡힘(대조만) |
| x3 고리 텍셀 높이를 0으로 씀 | :231 `float4(Height,0,0,0)` → `float4(0,0,0,0)` | GroundBakeColumns, GroundConvertCapture | **실패 0, 끝 코드 0 (비 0.00832 = 변이 없을 때와 같음)** | 대조로는 살아남음, 단위 시험이 잡음 |

x3 의 뜻: 기둥 덤프가 창 안 n×n 만 담고, 창 가장자리 바깥쪽 면은 닫혀 있어 고리 높이는 재생 장면 결과에 전혀 안 나타난다. 대조로 잡으려면 기둥 덤프에 고리(n+2)²를 포함하거나, 고리 높이를 쓰는 경로(창 끝 열린 면 등)를 가진 장면이 있어야 한다. 지금은 GroundBakeColumns·GroundConvertCapture 가 유일한 방어선.
실행해 보지 않은 경로: `DoesFog3DGroundCoverWindow` 의 고리 경계(재생 장면은 땅이 언제나 창+고리 딱 130칸), 캡처 변환의 못 찾은 땅 값 경계(`<` 대 `<=`) — 재생 장면이 업로드만 써서 대조로는 못 가른다(정보).

## 6. 끝 상태

| 항목 | 결과 |
|---|---|
| 세 파일 sha256 | `sha256sum -c start-sha.txt` 모두 OK (시작 전·마지막 두 번) |
| 변이 없는 재실행 `s2a-refute-final` | 25/25 통과, 덤프 38 새로 씀, I1 대조 끝 코드 0(470/0, 비 0.00831792), v2 e1 558/0 |
| `Saved/FluidFX/Fog3D/gpu*` | 마지막 실행이 변이 없는 덤프로 덮어씀 |

## 확인 못 한 것

- D3D11 에서 I1·I3 재실행(D3D12 만 함).
- I4~I9(범위 밖).
- 변이 실행 동안(19:38~20:01) 작업 트리 셰이더에 변이가 들어가 있었다: 그 사이 다른 세션이 Fog3D 시험·덤프를 썼다면 변이 결과일 수 있다(빌드 보류는 걸지 않았다).

파일: 사본·스크립트 `mut.py`·`runmut.sh`·`indep.py`·`summ.py`, 결과 `mut-*.txt`·`cmp-*.json`·`indep-*.txt`·`i1-rerun.json`·`i1-final.json`·`i2-*.txt`.

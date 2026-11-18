# 2a R5 최종 코드 판별력 재측정 (2A-I3(R5), 등록 prereg-2a-impl-add1.md) — 2026-11-15 claude 반박자

판정: **합격**. 등록된 변이 아홉이 모두 잡혔다. (자)는 대조로는 겨우 잡혔고(비 1.02, 덤프 하나), 단위 시험 `GroundPlaneBlendCases` 가 확실히 잡는다. 등록 밖 변이 y2 는 살아남았다(정보, 5절). 고친 코드는 없다.

## 1. 변이 없는 상태 (D3D12)

| 항목 | 결과 |
|---|---|
| 보류 | `--acquire-hold s2a-impl-refuter` 획득(끝 코드 0). 모든 실행에 `--holder s2a-impl-refuter` |
| 시작 sha256 | usf 82c989cd…f9bb, Common.ush 49acbf22…fa3f, cpp 4d2d6389…fcd3 (사본은 `r5/`) |
| 시험 `s2a-refute-r5-base` | 29/29 통과, 덤프 41개 새로 씀 |
| 대조 `r5/i1-r5-base.json` | 끝 코드 0, **검사 506, 실패 0, 틀 없음(건너뜀) 0**, 가장 가까운 것 s2a_box_on_slope__end wind 비 0.00832 (주장과 같음) |
| 내 계산 `r5/indep.py` | 41덤프 유체 불일치 0. 상대 L2 최대: wind 1.66e-5, disp 3.12e-6, fill 1.35e-6(slope_curb60 end). 기둥을 ground.bin 에서 따로 구워 비교한 결과 비트 일치, 기울기 차 ≤1.2e-7 |

## 2. 변이 아홉 (하나씩 넣고, 매번 사본으로 원복한 뒤 sha256 = 시작값 확인)

모든 자리는 `Source/TDFluidGpu/Shaders/Private/TDFluidGpuFog3D.usf`. 원복 sha256 은 아홉 번 모두 usf 82c989cd…, ush 49acbf22…, cpp 4d2d6389… 이다.

| 변이 | 자리·바꾼 글자 | 실패한 단위 시험 | 대조 결과 (실패 수 / 최대 비 = 장 차이 ÷ 한계) | 판정 |
|---|---|---|---|---|
| (가) 기울인 높이 부호 | :977 `- sx…- sy…` → `+`,`+` | GroundInputFrames, GroundPlaneBlendCases, GroundUniformSlopeNearFlat | 87 / box_on_slope end wind 499, 틀 자리 9.02cm | 잡힘 |
| (나) 윗면 목표에 틀 기울기 | :1089 `ColumnSlope` → `Frame.MidpointCmFrameSlope.zw` | GroundPlaneBlendCases | 36 / hill_run_window mv4 disp 113 | 잡힘 |
| (다) 면 높이를 한쪽 기둥으로 | :948·:952 `0.5*(자기+이웃)` → `자기` | GroundPlaneBlendCases, GroundUniformSlopeNearFlat | 70 / box_on_slope end wind 422 | 잡힘 |
| (라) 벽 `>=` | :213 | GroundBakeColumns | 12 / curb30_cross: wall 128기둥, disp 430 | 잡힘 |
| (마) 고리 안 읽음 | :187 `LocalColumn` → `clamp(LocalColumn,0,Res-1)` | GroundBakeColumns, GroundConvertCapture, GroundInputFrames | 66 / slope_x 0.471, cliff_at_edge end fill 201 | 잡힘 |
| (바) x·y 기울기 맞바꿈 | :245 출력 SlopeX↔SlopeY | GroundBakeColumns, GroundInputFrames, GroundPlaneBlendCases, GroundUniformSlopeNearFlat | 178 / frame_at_edge end wind 601 | 잡힘 |
| (사) 틀 기울기 읽는 자리 안 자름 | :1172 `ClampedCm = clamp(…)` → `MidpointCm` | 없음 | 13 / frame_at_edge 한 장면만: 틀 기울기 비 2241, wind 5.47 | 잡힘(대조만) |
| (아) 몫 g 를 늘 1 로 | :1032 `return saturate(…)` → `return 1.0f` | GroundPlaneBlendCases | 19 / slope_curb w70 disp 53.1 (curb30·slope_curb·slope_curb60) | 잡힘 |
| (자) 깎는 양에만 섞기 안 함 | :1057 `Share = ComputeFog3DTiltShare(…)` → `1.0f` | GroundPlaneBlendCases | **1** / slope_curb60 blade fill **1.021**(그다음 end 0.635) | 잡힘(대조는 아슬아슬) |

약한 곳:
- (자): 대조의 여유가 2%뿐이고 덤프 하나에서만 잡힌다. 실질적인 방어선은 단위 시험이다.
- (사): 단위 시험은 못 잡고, 재생 장면 하나(frame_at_edge)가 잡는다.

## 3. 등록 밖 변이 (정보)

| 변이 | 자리 | 단위 시험 | 대조 | 판정 |
|---|---|---|---|---|
| y1 띠를 T~2T 에서 T~3T 로 | :1032 `(2T−|e|)/T` → `(3T−|e|)/(2T)` | GroundPlaneBlendCases | 16 / slope_curb60 w70 disp 13.6 | 잡힘 |
| **y2 평면 편차에 면 표본도 기둥 가운데 높이를 씀** | :1031 `GetFog3DSampleGroundHeightCm(LocalCell, SampleKind)` → `(…, Fog3DSampleCenter)` | **없음** | **실패 0, 비 0.0083(변이 없을 때와 같음)** | **살아남음** |

y2 가 살아남는 까닭(추정): u·v 면의 표본 높이가 두 기둥 평균이냐 한 기둥이냐는, 턱을 걸친 면에서만 몫 g 를 바꾼다.
- 비탈에서는 |e| 가 약 4.5cm 라 g = 1 로 그대로다.
- 60cm 턱에서는 걸친 면의 평균 편차가 약 30cm 라 g 는 정확히 1, 한 기둥으로 쳐도 편차가 약 0 이라 g = 1 이다. 그래서 차이가 안 난다.
- 100cm 턱(slope_curb)에서는 걸친 면이 g 약 0.33 대 1 로 갈리지만, 그 장면의 구는 턱 앞에서 멈춰 그 면에 닿지 않는다.

잡으려면 걸친 면의 평균 편차가 30~60cm(턱 높이 약 60~120cm, 예: 90cm)인 턱 위로 붓을 실제로 지나가게 하는 장면이 필요하다. 예를 들면 S30CURB90 걷기·칼 장면이나, `GroundPlaneBlendCases` 에 90cm 턱 경우를 더하는 것이다.

## 4. 끝 상태

| 항목 | 결과 |
|---|---|
| sha256 | `sha256sum -c r5/start-sha.txt` 세 파일 모두 OK (마지막 실행 앞뒤) |
| 변이 없는 재실행 `s2a-refute-r5-final` | 29/29 통과, 덤프 41 새로 씀, 대조 끝 코드 0 (506/0, 건너뜀 0) — gpu_s2a_r5 는 깨끗한 덤프로 덮어씀 |
| 보류 | `--release-hold s2a-impl-refuter` 끝 코드 0, `build_hold.json` 없음 |

## 확인 못 한 것

- D3D11 에서 다시 재지 않았다(D3D12 만).
- 이번 차례에는 2A-I2(R5 최종 코드의 뒷걸음)를 다시 돌리지 않았다. 구현자의 step7 보고에만 있다.

파일: `refute/r5/` 아래 `mut.py`·`runmut.sh`·`summ.py`·`indep.py`, `mut-*.txt`·`cmp-*.json`·`i1-r5-base.json`·`i1-r5-final.json`.

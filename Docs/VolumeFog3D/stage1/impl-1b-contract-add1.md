# 1b 구현 계약 덧붙임 1 — 독립 반박 뒤 바꾼 것과 시험 이름 목록

확정: 2026-11-13 21:15 claude(감독·결정). 마감 고침(코드·시험)이 끝난 뒤, 최종 검증과 화면 촬영을 돌리기 전에 썼다. 계약 본문 `impl-1b-contract.md` 는 고치지 않고, 본문과 다른 곳만 여기 적는다. 명세 `spec.md` v1 과 통과 기준 `prereg-1b.md`·`prereg-1b-b8-method.md` 는 그대로다.

근거: 독립 반박 4건 `Saved/AgentOps/20261113/fog3d/stage1b/v10-{module,density-data,layer,actor}-review.md`, 고침 넘김 글 `fxa-handoff.md`·`fxb-handoff.md`.

## 1. 본문과 달라진 동작

| 번호 | 무엇 | 본문 | 지금 | 까닭 |
|---|---|---|---|---|
| 1 | 계산 박자(본문 2절 DecideClock) | 지난 계산 뒤 흐른 시간 ≥ 간격이면 계산하고 시간을 0 으로 | 층이 "제출 뒤 흐른 시간"과 "계산 몫" 둘을 든다. 몫 ≥ 간격 × (1 − 1e-3) 이면 계산. 제출이 받아들여진 때에만 몫 = clamp(몫 − 간격, 0, 간격), 흐른 시간 = 0. 몫은 간격의 2배에서 자른다 | 본문 식은 프레임 시간이 0.1ms 만 흔들려도 화면 60Hz·초당 60 단계가 초당 약 42회로 줄었다(반박의 모사 실측). 명세 3.10("초당 횟수는 상한, 화면이 느리면 프레임마다")은 그대로 지킨다. 제출이 거절된 다음 계산의 입력 속도 과대도 함께 없어진다 |
| 2 | 씨앗 범위(본문 1절) | 0 ~ 16777215 | 0 ~ 8388607 | 셰이더가 `uint(값 + 0.5)` 로 읽어 2²³ 이상 홀수 씨앗이 1 큰 씨앗이 된다. 셰이더는 그대로 두고 범위를 줄였다 |
| 3 | 창 따라가기 여유의 검사 | 8 ~ 56 | 그 범위에 더해 여유 ≤ (세 품질 해상도 가운데 가장 작은 값) ÷ 2 − 띠 칸 수 − 8 | 작은 해상도와 큰 여유가 만나면 플레이어가 창 밖이나 가장자리 띠 안에 든다 |
| 4 | 폰이 바뀌거나 없다가 다시 생김 | 정하지 않음 | 안개 상태는 그대로 두고 입력의 지난 위치만 잊는다(다음 계산의 구는 지난 위치 = 지금 위치). 층 `ForgetInputHistory`, 액터가 지난 틱의 폰을 약한 참조로 기억 | 옛 자리에서 새 자리까지 긴 쓸림 줄이 그어지는 것을 막는다(명세 3.7 이 처음·다시 시작 계산에서 막으려던 것과 같은 일) |
| 5 | 요약 줄 | 없음 | 대표 구역 액터가 EndPlay 에서 렌더 명령을 비운 뒤 `Fog3D zone summary: submitted=<수> rejected=<수>` 를 Display 로 한 줄 남긴다 | 화면 판정 B8(3) 이 요구한다 |
| 6 | 첫 제출 전의 재질 창 값 | 제출 뒤에만 씀 | 상수 파라미터를 쓸 때 창 값의 높이(= −구역 반높이)와 해상도를 함께 쓴다 | 첫 제출 전에는 평소 안개 높이가 구역 중심 기준이 되어 두께가 약 240cm 로 그려졌다 |
| 7 | 볼륨 안개 관문(본문 3절 차례 5) | `bEnableVolumetricFog`·보임 | 그려짐 ∧ 볼륨 안개 켬 ∧ 밀도 > 0 ∧ 최대 불투명도 > 0 ∧ 볼륨 안개 거리 > 0 인 높이 안개 컴포넌트를 고른다. 잡은 것이 조건을 잃으면 1초에 한 번 이하로 다시 찾는다 | 엔진이 실제로 볼륨 안개를 그리는 조건에 맞춘다 |
| 8 | 꺼 둔 구역(bEnabled 거짓) | 정하지 않음 | 층 설정과 렌더 타깃 만들기를 건너뛴다 | 꺼 둔 구역이 GPU 메모리를 잡지 않게 |
| 9 | 정리 패스 | 유체가 된 칸만 0 | 막힌 칸의 압력도 0 으로 쓴다 | 본문 "물체 목록 변경" 라. 의 글자 그대로(모드 1·압력 끔에서도) |
| 10 | 제출 전 출력 타깃 확인 | 크기 | 크기 + PF_FloatRGBA + UAV 지원 | 잘못 만든 타깃이면 매 계산 거부 → 매 계산 다시 시작이 말없이 돌았다 |
| 11 | 지난 창 확인에서 해상도가 바뀐 호출 | 문장에 없음 | "새 해상도 = 첫 계산"으로 보고 거부하지 않는다 | 옛 상태를 버리고 새로 시작하므로 지킬 연속성이 없다 |

## 2. 시험 이름 목록 (통과 기준 B3 의 "계약의 목록 수")

본문은 14개 이름을 적었다. 구현과 반박을 거치며 늘었으므로, 최종 검증 전에 여기서 목록을 고정한다. B3 의 판정은 "찾아진 시험 = 아래 23개 모두, 더도 덜도 없이"다. 본문 14개와 수가 다르다는 사실은 결정 기록에 남긴다.

`TDGame.FluidFX.Fog3D.*` (널 RHI, 23개)
BladeSamplesFromSocket, BladeSpheres, BodySamplesOrder, ClockCadence, FrameInputAssembly, LayerStepPath, MaterialWindowParameters, ModeAndQualityResolve, ObstacleChangeSignal, ObstacleSelectAndPack, QualityTierResolve, ResetReasons, SettingsTable, SettingsValidation, SpherePacking, SubsystemObstacleRead, WindowSnapAndFollow, ZoneActorLifecycle, ZoneInside, ZoneMaterialParameters, ZoneTickGates, ZoneTickTracking, ZoneVolumeScale

`TDGame.FluidFXRhi.Fog3D.*` (GPU, 15개 — 수는 정보)
AddendumReplayDumps, BlockedCellDrawFill, BlockedCellDrawFillAtWindowEdge, ClosedFaces, DensityMatchesReference, Determinism, FarOrigin, LayerOutputFrames, ModuleSmoke, PreviousWindowCheck, ReplayDumps, RestStaysZero, SealedColumn, StepTiming, WindowShiftOutput

## 3. 이번에 하지 않고 다음 등록으로 넘기는 것

1. 창 이동과 물체 목록 변경이 같은 계산에 겹치는 경우, 모드 1 의 물체 변경: GPU 와 기준 모의를 맞대어 본 적이 없다(코드 읽기로는 저장이 세계 칸 기준이라 같은 결과). 다음 등록 때 장면으로 넣는다.
2. 그리는 식의 "높이를 0 에서 자르기"는 지금 맞대기 자료가 가르지 못한다(얼룩 세기가 커서 두께가 흐림 폭보다 얇아지는 경우에만 결과가 달라진다). 다음 자료를 만들 때 그 표본을 넣는다.
3. 구역 안에서 창 크기보다 짧게 순간이동하면(점멸 같은 기술) 옛 자리에서 새 자리까지 쓸고 지나간 것으로 그려진다. 속도 상한은 세기만 줄인다. 입력별 순간이동 판정은 다음 단계 후보다.
4. 출력 시험(LayerOutputFrames)의 내려찍기 확인 구간(프레임 32~50)은 구현자가 첫 실행 결과를 본 뒤 정했다. 판정 값(−0.05)과 등록 문장 B4 는 그대로이고, 이 구간은 본문 7절의 세부다.

## 4. 덧붙임 뒤의 정정 (2026-11-13 22:20, 마감 고침 반박 `v11-fix-review.md` 를 반영)

- 1절 7번의 조건을 엔진의 실제 조건으로 고쳤다: (안개 밀도 + 둘째 안개 데이터의 밀도) × 1000 > 아주 작은 값, 최대 불투명도 > 아주 작은 값, 컴포넌트가 등록돼 있고 그려짐, 볼륨 안개 켬, 볼륨 안개 거리 > 0. 앞 문장("밀도 > 0")대로면 밀도 0 에 둘째 안개만 켠 장면에서 엔진은 볼륨 안개를 그리는데 3D 안개가 쉬었다.
- 1절 8번의 한계: 처음부터 꺼 둔 구역만 텍스처를 만들지 않는다. 실행 중에 끄면 이미 만든 텍스처와 층 상태는 놓지 않고 숨기기만 한다(조절 표가 실행 중 바뀌는 일은 드물어 고치지 않았다).
- 2절의 시험 이름 목록은 그대로다(널 RHI 23개, GPU 15개). 이번 고침의 새 단언은 모두 기존 시험 안에 넣었다.
- 이 문서 머리의 시각과 진행 기록의 일부 시각은 어림으로 적은 것이다. 파일 시각으로는 이 덧붙임의 처음 확정이 21:11, 화면 판정 재는 법 등록이 19:07, 화면 판정 덧붙임 2 등록이 21:36 이다.

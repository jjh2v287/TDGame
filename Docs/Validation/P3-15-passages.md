# P3-15 이계 통로: 걸어서 넘어가는 숨은 이음매 (2026-10-14, claude)
교훈: L-worldgen-03(태양 회전 보간·검증은 실제 이동 입력), L-editor-15(평면 충돌 프로필), L-worldgen-01(떠난 쪽 붙잡기)
명령: 헤드리스 `UnrealEditor-Cmd.exe TDGame.uproject -ExecCmds="Automation RunTests TDGame.WorldGen+TDGame.SeamlessTravel+TDGame.WorldState" -TestExit="Automation Test Queue Empty" -unattended -NullRHI` / PIE 실제 걷기 당시 실험 도구(`Tools/scratch/pie_walk_driver.py`, 기록 `Saved/AgentOps/20261014/piewalk/`) → 정식 도구 `python Tools/WorldGen/pie_walk_passages.py --run <이름>`(P3-17)

- 결정 D-42(숨은 이음매, 남서·남동 입구 → 별밭 하강, 북동 입구 → 고원 상승, 메인 부분 재베이크). 대장 `Docs/Tasks/phase-3-outdoor-generator.md` P3-15.
- 명세·조사·하위 에이전트 결과: `Saved/AgentOps/20261013/seam-spec.md`, `seam-understand.md`, `Saved/AgentOps/20261014/seam-implement-result.md`

## 바뀐 것
- 포탈(관문 상자 → 암전 → 순간이동) 제거. 외곽 입구에서 통로 A(필드 90~140m)를 걸어가다 이음매를 넘으면 같은 모양의 이계 통로 B(32~45m)로 평행이동한다. 걸음·속도·회전·카메라가 그대로 이어지고 화면 전환은 없다.
- 걷는 동안 반대편을 선로딩하고, 분위기(태양 세기·색·회전, 스카이라이트, 대기, 안개, 이계 후처리 가중치)를 통로 진행도로 섞는다. 이음매 부근은 전역 안개를 짙게 올려 가린다.

| GateId | 필드 이음매(cm) | 이계 이음매(cm) | 평행이동 거리 |
|---|---|---|---|
| `SunkenStarfieldSouthwest` | (−45600, −24100, −66) | (−280800, 308500, 475) | 약 4,074m |
| `SunkenStarfieldSoutheast` | (36500, −33700, 955) | (−324000, 306500, 319) | 약 4,957m |
| `PaleHeightsNortheast` | (45000, 45000, 1492) | (−324000, −300000, 398) | 약 5,051m |

## 형상 일치
- 생성기 검증: 핵심 구간(|s|≤30m, |l|≤16m)에서 필드 `height.r16`과 이계 높이 차이 = dz 보정 뒤 0/128m(세 관문, 1911~1921칸). 통로 소품 이계·필드 사본 상대 위치 차이 ≤0.0625cm.
- 에디터 캡처로 이음매 6m 앞을 양쪽에서 같은 각도로 비교: 벽 바위 모양·바닥이 같다(`P3-15-passages/seam_*_field.jpg` ↔ `seam_*_realm.jpg`). 에디터 캡처의 색 차이는 이계 쪽에만 걸린 후처리 상자 때문이며, 실행 중에는 이계 후처리가 양쪽 같은 가중치(이음매 0.5)로 걸린다.

## 자동화 테스트(헤드리스 NullRHI)
- 31/31 Success(`Saved/Logs/P3-15-parent-tests-3.log`). 신규 `TDGame.SeamlessTravel.RealmSeamWalk`: 관문 3곳 들어가기·나오기 모두 이음매 통과, 통과 전후 통로 좌표 동일(예: 56.3→56.3cm, −59.5→−59.5cm), 이계 통로 끝 가중치 1.0, 필드 복귀 ≤0.05. 기존 MainCrypt 던전 왕복·선로딩 취소·WorldState·정의 단위 테스트 회귀 없음.

## PIE 실제 이동 입력 걷기(`add_movement_input`, Mover 보행)
| 통로 | 들어가기 | 나오기 |
|---|---|---|
| 별밭 남서 | 15.1s에 통과, 속도 599.5cm/s 유지, w 0.003→1.0 | 15.0s에 통과, w 1.0→0.001 |
| 별밭 남동 | 16.8s 통과, 600cm/s | 13.8s 통과 |
| 고원 북동 | 23.3s 통과, 600cm/s | 12.6s 통과 |
- 통과 순간 앞뒤 0.5초 표본의 속도가 모두 599.5~600cm/s, 가중치 0.49→0.50→0.50으로 연속. 통과 직후 상태 READY(되돌아갈 쪽이 이미 준비됨).
- 처음 실행에서 나오기(오르막)가 이계 쪽 s=+24.4m에서 멈췄다. PIE 탐침 결과 안개·바닥 덮개 평면이 PIE에서 충돌을 가졌다(L-editor-15). 평면 338개 충돌 프로필 수정 뒤 6/6 통과.

## 화면(PIE 게임 카메라, `P3-15-passages/*.jpg`)
- 별밭 남서: `_1_mouth`(필드 협곡 입구, 따뜻한 빛) → `_2_field_seam_minus12m`(어두워지는 협곡) → `_3_realm_seam_plus12m`(짙은 청색 안개) → `_4_realm_end`(달빛 아래 푸른 이계 풀밭).
- 고원 북동: `_1_mouth`(산길 시작 등불) → `_2_`·`_3_`(이음매 앞뒤 같은 조명의 어두운 협곡, 오른쪽 위 벽만 햇빛) → `_4_realm_end`(창백한 바위·마른 풀·밝은 햇빛).
- 이음매 직후 조명 튐(고원)이 처음 있었다: 태양 회전을 이음매에서 한 번에 바꿨기 때문. 진행도 보간으로 고쳐 `_2_`↔`_3_`이 같은 조명이 됐다(L-worldgen-03).

## 남은 것
- 통로 핵심 구간이 깊은 협곡이라 어둡다(특히 이음매 앞뒤). 분위기 연출로는 의도에 맞지만 전투 가독성을 위해 통로 조명·벽 높이 조정 여지가 있다. 좁은 협곡에서 카메라가 벽 소품에 가려지는 장면이 있다.
- RealmSeamWalk 단언 (a)는 앞 단계 선로딩으로도 참이 된다(약한 단언), 시간 제한은 단계별 90초.
- 쿠킹 빌드·저사양 디스크에서 준비 시간은 미측정(P1-09·P1-10·P4-03).
- 메인 필드 부분 재베이크로 통로 띠 밖 산포는 이전과 같다(난수 흐름 보존). 지형 교체 뒤 PCG 숲을 다시 생성·저장했다.

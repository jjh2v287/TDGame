# 회전 팝핑 측정 before

- 맵 /Game/Combat/Maps/LV_TDMegaMagicArena, 종마다 3마리·30.0초, 고정 FPS 60.0, cvar {'TD.Monster.SmoothTurn': '0'}

| 라벨 | 종 | 마리 | 표본 분 | 논리 최대/p99 도/초 | 보이는 몸 최대/p99 | 골반 최대/p99 | 720 초과 프레임 논리/보임 | 급회전 논리/보임 | 각속도 급변/분 논리/보임 | 흔들림/분 논리/보임 | 몰아 돌기 논리/보임 | 몸-논리 어긋남 p95/최대 | 골반-몸 p95 | 최대 델타 ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| before | Barghest | 3 | 1.50 | 4653/540 | 4653/540 | 4653/540 | 8/8 | 8/8 | 282.0/282.0 | 194.67/194.67 | 3/3 | 0.0/0.0 | 0.0 | 16.67 |
| before | Goblin | 3 | 1.50 | 6017/540 | 6017/540 | 7002/4225 | 17/17 | 17/17 | 220.12/220.12 | 134.74/134.74 | 0/0 | 0.0/0.0 | 94.19 | 16.67 |
| before | Hyena | 3 | 1.50 | 4431/540 | 4431/540 | 4431/540 | 19/19 | 19/19 | 292.83/292.83 | 167.43/167.43 | 0/0 | 0.0/0.0 | 0.0 | 16.67 |
| before | StoneGolem | 3 | 1.50 | 3456/540 | 3456/540 | 8485/684 | 1/1 | 1/1 | 290.83/290.83 | 204.12/204.12 | 1/1 | 0.0/0.0 | 67.02 | 16.67 |

## 급회전 상위(보이는 몸)

- Barghest TDMonsterCharacter_0 t=18.496 Δ77.55° 4653.0도/초 상태 Cast 행동 Pounce
- Barghest TDMonsterCharacter_1 t=35.979 Δ43.85° 2631.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_2 t=32.462 Δ34.28° 2057.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_0 t=29.146 Δ-27.2° -1632.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_2 t=22.212 Δ23.44° 1406.0도/초 상태 Cast 행동 Bite
- Goblin TDMonsterCharacter_4 t=59.297 Δ100.28° 6017.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_5 t=71.964 Δ85.84° 5150.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_3 t=74.964 Δ-61.4° -3684.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_5 t=60.714 Δ59.72° 3583.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_3 t=61.18 Δ55.99° 3359.0도/초 상태 Cast 행동 Slash
- Hyena TDMonsterCharacter_8 t=90.072 Δ-73.86° -4431.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_6 t=97.106 Δ53.85° 3231.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_7 t=100.472 Δ50.48° 3029.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_8 t=96.806 Δ43.05° 2583.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_7 t=96.089 Δ-39.54° -2372.0도/초 상태 Cast 행동 Bite
- StoneGolem TDMonsterCharacter_9 t=128.702 Δ-57.6° -3456.0도/초 상태 Cast 행동 Stomp

## 정의

- logic: 액터 요(Mover 가 정하는 논리 방향). 이동 중 회전 한도는 TurningRate(몬스터 540도/초)
- visual: 메시 컴포넌트 월드 요(화면에 보이는 몸 방향, 종 MeshYaw 상수 포함)
- pose: 골반(없으면 척추) 뼈 월드 요. 애니메이션이 몸을 돌리는 몫까지 포함
- rate: 연속 두 표본의 요 차(−180~180 감싸기) ÷ 그 프레임 월드 델타
- over_rate_frames: 각속도 > 720도/초 프레임(Mover 한도 540의 1.33배, 정상 회전으로는 나올 수 없음)
- snaps: 초과 프레임 가운데 한 프레임 요 차 ≥ 10도(한 번에 돌아가는 팝핑)
- rate_jumps: 연속 두 프레임 부호 있는 각속도 차 ≥ 400도/초(두 프레임 모두 델타 ≤ 1/30초). 회전이 가속 없이 바로 서거나 바로 최고 속도가 되는 끊김
- reversals: |각속도| ≥ 90도/초 회전 프레임의 부호가 직전 회전 프레임과 반대이고 간격 ≤ 0.25초(좌우로 흔들림)
- hold_jumps: 가운데 프레임 요 차 < 0.05도, 양옆 ≥ 1도 같은 방향(갱신이 건너뛴 뒤 몰아 도는 뚝뚝 끊김, 틱 예산 의심)
- visual_offset: wrap(메시 요 − 액터 요 − 중앙값) 절대값(보이는 몸이 논리 방향에서 벗어난 각)
- pose_offset: wrap(골반 요 − 메시 요 − 중앙값) 절대값(애니메이션이 몸을 돌리는 몫)
- per_minute: 개수 ÷ 살아 있는 표본 시간(분)

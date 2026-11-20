# 회전 팝핑 측정 after-off

- 맵 /Game/Combat/Maps/LV_TDMegaMagicArena, 종마다 3마리·30.0초, 고정 FPS 60.0, cvar {'TD.Monster.VisualTurn': '0'}

| 라벨 | 종 | 마리 | 표본 분 | 논리 최대/p99 도/초 | 보이는 몸 최대/p99 | 골반 최대/p99 | 720 초과 프레임 논리/보임 | 급회전 논리/보임 | 각속도 급변/분 논리/보임 | 흔들림/분 논리/보임 | 몰아 돌기 논리/보임 | 몸-논리 어긋남 p95/최대 | 골반-몸 p95 | 최대 델타 ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| after-off | Barghest | 3 | 1.50 | 2499/540 | 2499/540 | 2499/540 | 6/6 | 6/6 | 254.14/254.14 | 161.42/161.42 | 1/1 | 0.0/0.0 | 0.0 | 16.67 |
| after-off | Goblin | 3 | 1.50 | 6973/540 | 6973/540 | 6606/4303 | 17/17 | 17/17 | 204.78/204.78 | 126.07/126.07 | 0/0 | 0.0/0.0 | 95.84 | 16.67 |
| after-off | Hyena | 3 | 1.50 | 10622/540 | 10622/540 | 10622/540 | 20/20 | 20/20 | 314.18/314.18 | 196.78/196.78 | 1/1 | 0.0/0.0 | 0.0 | 16.67 |
| after-off | StoneGolem | 3 | 1.50 | 540/540 | 540/540 | 825/654 | 0/0 | 0/0 | 365.54/365.54 | 287.5/287.5 | 0/0 | 0.0/0.0 | 65.43 | 16.67 |

## 급회전 상위(보이는 몸)

- Barghest TDMonsterCharacter_2 t=24.314 Δ41.64° 2499.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_2 t=35.581 Δ39.43° 2366.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_1 t=37.114 Δ22.62° 1357.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_0 t=17.314 Δ20.78° 1247.0도/초 상태 Cast 행동 Pounce
- Barghest TDMonsterCharacter_0 t=38.914 Δ-19.15° -1149.0도/초 상태 Cast 행동 Bite
- Goblin TDMonsterCharacter_4 t=57.65 Δ116.21° 6973.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_4 t=52.5 Δ-103.15° -6189.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_5 t=66.284 Δ-49.97° -2998.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_3 t=64.3 Δ-43.34° -2600.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_5 t=53.9 Δ-37.26° -2236.0도/초 상태 Cast 행동 Slash
- Hyena TDMonsterCharacter_7 t=89.323 Δ-177.04° -10622.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_7 t=101.906 Δ-144.24° -8654.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_7 t=96.356 Δ-139.19° -8351.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_7 t=92.99 Δ58.91° 3535.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_6 t=94.006 Δ55.34° 3320.0도/초 상태 Cast 행동 Bite

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

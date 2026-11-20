# 회전 팝핑 측정 baseline

- 맵 /Game/Combat/Maps/LV_TDMegaMagicArena, 종마다 3마리·30.0초, 고정 FPS 60.0, cvar {}

| 라벨 | 종 | 마리 | 표본 분 | 논리 최대/p99 도/초 | 보이는 몸 최대/p99 | 골반 최대/p99 | 720 초과 프레임 논리/보임 | 급회전 논리/보임 | 각속도 급변/분 논리/보임 | 흔들림/분 논리/보임 | 몰아 돌기 논리/보임 | 몸-논리 어긋남 p95/최대 | 골반-몸 p95 | 최대 델타 ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline | Barghest | 3 | 1.50 | 3291/540 | 3291/540 | 3291/540 | 8/8 | 8/8 | 264.15/264.15 | 169.43/169.43 | 1/1 | 0.0/0.0 | 0.0 | 16.67 |
| baseline | Goblin | 3 | 1.50 | 3732/540 | 3732/540 | 6828/4130 | 10/10 | 10/10 | 248.14/248.14 | 168.1/168.1 | 0/0 | 0.0/0.0 | 89.89 | 16.67 |
| baseline | Hyena | 3 | 1.50 | 10603/540 | 10603/540 | 10603/540 | 19/19 | 19/19 | 270.15/270.15 | 162.09/162.09 | 1/1 | 0.0/0.0 | 0.0 | 16.67 |
| baseline | StoneGolem | 3 | 1.50 | 540/540 | 540/540 | 789/607 | 0/0 | 0/0 | 123.4/123.4 | 47.36/47.36 | 0/0 | 0.0/0.0 | 65.34 | 16.67 |

## 급회전 상위(보이는 몸)

- Barghest TDMonsterCharacter_2 t=29.731 Δ-54.85° -3291.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_1 t=26.381 Δ37.44° 2246.0도/초 상태 Cast 행동 Bite
- Barghest TDMonsterCharacter_2 t=24.615 Δ-22.58° -1355.0도/초 상태 Cast 행동 Pounce
- Barghest TDMonsterCharacter_1 t=31.115 Δ-22.07° -1324.0도/초 상태 Cast 행동 Pounce
- Barghest TDMonsterCharacter_0 t=28.165 Δ20.92° 1255.0도/초 상태 Cast 행동 Bite
- Goblin TDMonsterCharacter_4 t=56.43 Δ62.2° 3732.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_5 t=66.263 Δ-51.7° -3102.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_4 t=64.196 Δ-51.65° -3099.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_4 t=60.93 Δ45.31° 2718.0도/초 상태 Cast 행동 Slash
- Goblin TDMonsterCharacter_5 t=60.546 Δ-44.58° -2675.0도/초 상태 Cast 행동 Slash
- Hyena TDMonsterCharacter_8 t=111.698 Δ-176.71° -10603.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_6 t=86.164 Δ-66.09° -3965.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_7 t=95.614 Δ-55.88° -3353.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_6 t=101.398 Δ-46.64° -2799.0도/초 상태 Cast 행동 Bite
- Hyena TDMonsterCharacter_6 t=89.598 Δ-41.42° -2485.0도/초 상태 Cast 행동 Bite

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

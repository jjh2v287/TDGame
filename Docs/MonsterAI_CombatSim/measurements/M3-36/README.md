# M3-36 전투 한 사이클(아레나 조우) 검증 증거 — 2026-10-18 claude

## 무엇을 확인했나
- 맵 `/Game/MonsterAI/Maps/LV_TDBossArena`(생성 `Tools/MonsterAI/editor_make_boss_arena.py`, 반경 1400·지름 2800 = 산룡 몸길이 약 670cm(축척 0.5)의 약 4배), 조우 정의 `/Game/MonsterAI/Encounters/DA_TDArena_DragonLair`.
- 웨이브 4: 고블린 4 → 고블린 3+하이에나 3 → 중간 보스 켄타우로스 사냥꾼(`DA_TDMonster_CentaurHunter`, M3-34, 50%에 검 자세·사냥개 소환) → 보스 산룡(`DA_TDMonster_MountainDragon`, M3-35, 70/40/25% 3페이즈).

## GUI PIE `TDGame.PIE.Combat.BossLoop` (2026-10-18 19:1x, 통과, `pie-automation-result.json`)
| 단계 | 결과 | 스크린샷 |
|---|---|---|
| 진입 구역 → 봉쇄·웨이브 배너 | WaveDelay, 입구문 닫힘, 배너 "졸개 웨이브 1 / 산룡의 둥지" | `01_arena_sealed.png` |
| 첫 웨이브 교전 | 고블린 4 스폰(도전자 기준 호) | `02_wave_one.png` |
| 플레이어 사망 | 즉시 Failed, 패배 배너 + 사망 화면 | `03_defeat.png` |
| 초기화·부활 | 3초 뒤 Idle·아레나 안 몬스터 0·봉쇄 해제, 4초 뒤 체크포인트 부활(거리 0cm, PlayerStart는 500cm 떨어져 있어 판별됨) | `04_respawn_checkpoint.png` |
| 재입장 → 웨이브 2 | 시도 2, 빠른 처치(시험이 플레이어 명의 치명 피해) | — |
| 중간 보스 | 보스바(몬스터가 보고한 눈금 1개), 50% 아래로 깎자 페이즈 1 진입·알림 1 | `05_midboss_bar.png` |
| 보스 산룡 | 눈금 3개, 70/40/25% 단계 피해 뒤 각 페이즈 진입을 기다려 페이즈 3·알림 3, "N단계" 배너 | `06_boss_bar.png`, `07_boss_low.png`(업화·안전 지대·소환 졸개) |
| 승리 | 승리 배너·보상 문구, 입구·출구 열림, 보스바 접힘, 처치 12 = 정의 합계, 체크포인트 해제 | `08_victory.png` |

- 보고 원본 `boss-loop-pie.json`: 시도 2·웨이브 4·처치 12·스크린샷 8/8·오류 0, 웨이브 시간 0.84/0.87/7.25/18.39초(보스 페이즈 대기 포함).
- 페이즈 전환은 보스가 공격·시퀀스를 끝내고 생각할 때만 일어나서 피해 직후가 아니라 수 초 뒤에 온다. 시험은 눈금마다 페이즈 진입을 최대 60초 기다린다.
- 스크린샷은 원본(2888×1656, `Saved/AgentOps/20261018/boss-loop/pie/`)을 절반 크기로 줄인 것이다. 대역 종(StoneGolem·BookHead_Brute)으로 돌린 앞선 통과 실행(17:55·18:03)도 있었다.

## 헤드리스
- `headless-regression-tests.json`(라벨 m336-final, 최종 트리): Encounter·Combat·MonsterAI·Movement·Smoke 177/178, 실패 = 기준선 `CircleAvoidance.Scene.CrossingStreams` 1건. 새 조우 시험 8개(웨이브 진행·결정론·사망 초기화·소멸 도전자·빈 웨이브 실패·스폰 위치·진입 구역·보스 처치 시 소환 졸개 정리) 통과. 이후 PIE 시험 진단 줄만 바뀜.
- OP-33: `check_sync_loads` new 0, `check_cook_coverage` 종료 0(MISSING `/Game/MonsterAI/Attacks` 1줄은 기존).

## 직접 해 보기
- 에디터에서 `/Game/MonsterAI/Maps/LV_TDBossArena`를 열고 PIE → 입구 복도에서 아레나 안으로 걸어 들어가면(진입 구역) 문이 닫히고 웨이브가 시작된다. 조우 동안 카메라가 1.4배 물러난다.
- 개발용 콘솔: `TD.Arena.KillWave`(현재 웨이브 즉시 처치), `TD.Arena.Reset`(조우 초기화·몬스터 정리). 죽으면 3초 뒤 아레나가 비고 4초 뒤 입구 밖에서 부활, 다시 걸어 들어가면 처음부터.
- 종 교체: `Tools/MonsterAI/editor_make_boss_arena.py` 위 `SPECIES`·이름·눈금을 고치고 다시 실행하면 맵은 두고 정의 에셋만 갱신된다.

## 알려진 남은 것·미검증
- GUI `TDGame.PIE.Combat.FullLoop`이 18:00·19:13 실행에서 "Killed goblin simulates physics within 1 s"로 실패: 831aa720(M3-33)이 고블린 종에 사망 클립을 넣은 뒤의 변화로 보이며 이 작업 변경과 무관(원인 미검증, M3-33에 전달).
- 미검증: 실제 손 조작으로 끝까지 깬 플레이(시험은 피해를 직접 넣음), 로드 완료 전 진입(Loading 경로), 저장·불러오기 뒤 승리 상태 복원, 카메라 당김 체감, 콘솔 명령, 쿠킹·패키지 실행. 06에서 서 있는 사냥개가 살아 있는지(소환 정리)는 화면만으로 확인하지 못함.

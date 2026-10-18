# 무리 행동 3종 시험 빨강 기준선(구현 전)

작성: 2026-10-18 claude(시험 빌더 T). 명세 `Saved/AgentOps/20261018/pack-behaviors/spec-v2.md` 4절·계약 7.9.

## 실행 조건

- 시험 정본: `Saved/AgentOps/20261018/pack-behaviors/staging/Source/TDGame/MonsterAI/Tests/TDPackBehaviorTests.cpp`(17개, 접두 `TDGame.MonsterAI.Pack.`). 새 심볼 0개 — 디버그 줄 문자열(`alarm=`·`morale=`), 역할 리터럴 3·4, 공개 API, 파일 안 접근자 `FTDPackBehaviorTestAccess`만 사용.
- 공용 트리: M3-29 통합본(`TDMonsterThinkSubsystem.h` md5 `d1e1a0c0924e87d65f25cb634203a50a` = `base.md5`). 시험 파일 복사 + 헤더 friend 한 줄(`friend struct FTDPackBehaviorTestAccess;`)만 추가. 동료 세션이 같은 시각에 고친 `TDCombatTacticsTests.cpp`·`TDMonsterAITests.cpp`(M3-29 시험 보강)는 그대로 포함.
- 명령: `PYTHONIOENCODING=utf-8 python Tools/build_and_test.py --holder pack-behaviors --label pack-red --filter TDGame.MonsterAI.Pack` → 빌드 성공(컴파일 오류 0, 첫 시도) → `--no-build --label pack-red-regress --filter TDGame.MonsterAI`.
- 보류 파일: `Saved/AgentOps/build_hold.json` owner `pack-behaviors`(15:14 생성, 원복 뒤 삭제).
- 원본 로그: `Saved/AgentOps/20261018/pack-red-tests.log`·`pack-red-tests.json`(= 이 폴더의 `red-before-tests.json`), 회귀 `pack-red-regress-tests.json`.

## 결과: 17개 중 빨강 12/12 실패, 보호 5/5 통과

| ID | 이름 | 기대 | 실제 | 일치 | 실패 단언 첫 줄 |
|---|---|---|---|---|---|
| A1 | AlarmRipplesTwoHops | 빨강 | 실패 | 일치 | B engages 8..16 steps after A (A at 0, B at 0) |
| A2 | OnlyFirstDiscovererCalls | 빨강 | 실패 | 일치 | exactly one alarm=caller line ... (256 bad steps, first at step 0) |
| A3 | TacticsOffKeepsInstantAlarm | 보호 | 통과 | 일치 | - |
| A4 | OneShotKillAlertsNeighbors | 빨강 | 실패 | 일치 | the neighbor engages within 16 steps of the one-shot kill (ticks after kill -1) |
| B1 | LeaderDeathScattersPack | 빨강 | 실패 | 일치 | at least two goblins show morale=panic within 1 s of the leader dying (0) |
| B2 | BurstLossesPanicSmallPack | 빨강 | 실패 | 일치 | at least two of the three survivors panic within 1 s of the second loss (0) |
| B3 | LargePackHoldsNerve | 보호 | 통과 | 일치 | - |
| B3b | SlowLossesDoNotPanic | 보호 | 통과 | 일치 | - |
| B4 | TimedRetreatEndsOnTime | 빨강 | 실패 | 일치 | a 1.5 s retreat ... (10 cm per step) ends within 1 of step 96 (idle at step -1) |
| B5 | TimedRetreatEndsWhenCornered | 빨강 | 실패 | 일치 | a retreat that cannot gain distance ends between steps 24 and 26 (idle at step -1) |
| B6 | LowHealthFleeDoesNotLoop | 빨강 | 실패 | 일치 | the goblin never gives up and turns toward home: expected 0, was 712 |
| C1 | ChargersTakeTurns | 빨강 | 실패 | 일치 | chargers: at most one ... holds a token on every step (max 2, first at step 0) |
| C2 | SupportCastsAlone | 빨강 | 실패 | 일치 | support: at most one ... holds a token on every step (max 2, first at step 0) |
| C3 | RangedHoldsBandBehindMeleeRing | 빨강 | 실패 | 일치 | the archer median distance after 6 s is at least 640 cm (540) |
| C4 | PackDefinitionKeys | 빨강 | 실패 | 일치 | role Charger resolves without errors (... unknown role 'Charger' ...) |
| G1 | Deterministic | 보호 | 통과 | 일치 | - |
| G2 | EventOrderInvariant | 보호 | 통과 | 일치 | - |

빨강이 통과한 시험 없음(강화 불필요). 보호 시험은 자리 지키기 단언(사망으로 줄 수 감소·생존자 교전 유지·귀환 0·최종 줄 수)이 함께 통과해 공허한 통과가 아니다.

## 구현 전 실측([pack] 정보 줄)

- A1: 교전 첫 표본 A 0, B 0, C 없음, D 없음(즉시 1단 경보). A3(전술 끔)도 같음.
- A2: 둘 다 0, 잠자던 고블린도 0(즉시 경보), `alarm=` 필드 없음.
- A4: 한 방 처치 뒤 이웃 교전 없음(-1).
- B1·B2: `morale=` 필드 없음 → 공황 0, 귀환 0, 재교전 단언은 공허하게 통과(공황이 없으므로 구현 뒤에만 의미).
- B3·B3b: 공황 0, 생존자 교전 유지(10/10, 3/3), 귀환 0.
- B4·B5: 두 표본(10 cm·5 cm)과 고정 모두 200스텝 안에 Idle 없음(-1) — 시퀀스 밖 MoveAway가 끝나지 않음.
- B6: 체력 0.24, Returning 712스텝, Flee 비율 0.596, 최장 Flee 578스텝, 시전 시작 0.
- C1: 돌진자 리터럴 4, 토큰 보유 최대 2(첫 스텝), 동시 시전 최대 2(120스텝), 시전 시작 13·12·12·13.
- C2: 지원 리터럴 3, 토큰 보유 최대 2(첫 스텝), 동시 시전은 1, 시전 시작 9·10·9.
- C3: 궁수 거리 중앙값 540, 610 미만 표본 100%(540에서 안 움직임).
- C4: 오류 1건씩 — `unknown role 'Charger'`·`unknown role 'Support'`·`elite: unknown key`.
- G1: 656스텝 해시 차이 0, 최종 7줄. G2: 392스텝 해시 차이 0, 피격 체력 손실 4.8.

## 회귀

`--no-build --filter TDGame.MonsterAI`: 55/68 통과(완료 68, 엔진 종료 0). 실패 13 = 기준선 `CircleAvoidance.Scene.CrossingStreams` 1 + Pack 빨강 12. 기존 시험 51개 중 50 통과 = friend 한 줄과 새 파일이 기존 시험을 깨지 않았다. 통과 55 = 기존 50 + Pack 보호 5.

## 시험 설계 메모(구현자·반박자용)

- 공황 측정: `morale=panic` 이면서 `state=Move`가 한 몬스터에서 연속 ≥ 32스텝이고 그동안 플레이어까지 평면 거리가 늘어남. 최장 연속 ≤ 147스텝(B2).
- 재교전: 그 몬스터의 마지막 `panic` 표본 뒤 창(사건 + 256스텝) 안에서 시전(`state=Cast`) 또는 플레이어 거리 ≤ 400. 창이 끝날 때까지 공황이면 미재교전.
- 창: 공황 시작 = 사건 + 64스텝 안, 재교전 = 사건 + 256스텝 안, 사건 = 마지막 치명 피해 직전 표본 번호.
- 경보: 교전 첫 표본 번호 차이로 홉 지연 측정(8~16). 호출자 = 교전 첫 표본부터 끝까지 매 스텝 `alarm=caller` 정확히 1줄이며 SimulationId가 가장 낮은 발견자, 둘째 발견자 `silent`, 경보로 깬 이웃은 교전 시점 `hop1`.
- 토큰 보유 = 접근자로 `Tactics[슬롯].Role == 리터럴 && TokenState != None`을 매 스텝 센다. 역할 리터럴은 스폰 직후 첫 틱 전에 접근자로 쓴다.
- C3 방위: 궁수 45°·105°(원거리 칸 중심 15+30k°이며 남쪽 120~240° 밖), 골렘 4마리는 200~320° 방향 600~700 cm.

## 확인 못 한 것

- 구현 뒤(초록) 동작은 미실행. B1·B2의 "사건 + 256스텝 안 재교전"은 우두머리 공황(대기 최대 64 + 후퇴 최대 192)과 재접근(700 cm 정지 거리에서 400 cm까지 약 46스텝)을 합치면 최악 경우 약 22스텝 모자랄 수 있다.
- 새 .cpp는 UBT 캐시 makefile(`Makefile.bin`)이 "유효"로 판정해 처음에는 컴파일 대상에 들어가지 않았다(시험 0건 발견). `Intermediate/Build/Win64/x64/TDGameEditor/Development/Makefile.bin` 삭제 뒤 정상 포함. 통합 때 새 파일(`TDPackAlarm.cpp`·`TDPackMorale.cpp`·이 시험)을 넣을 때도 같은 조치가 필요하다.

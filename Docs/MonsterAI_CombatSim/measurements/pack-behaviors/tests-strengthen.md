# 시험 판별력 보강(R2 후속) — 사본 staging2, 컴파일 안 함

작성: 2026-10-18 td-builder. 대상 사본 `staging2/Source/TDGame/MonsterAI/Tests/TDPackBehaviorTests.cpp`(1319줄 → 약 1790줄, 줄 끝 LF 유지, 시험 17개 → 22개). 기준 `base2/...` 와의 차이는 `tests-strengthen.diff`.
공용 트리 심볼 확인: `FTDCombatTactics::ComputeRangedBandFloor`(TDCombatTactics.h:194, 식 `Clamp(바닥+100+몸, 0, Max(0, Min(사거리-100, 700)))`, 바닥 ≤ 0이면 0), `FTDPackMorale::MakeFragment`(리더 = boss|elite, 면역 = 리더|Support), `FTDMonsterMoraleFragment::bIsLeader·bIsImmune`, `FTDResolvedMonsterDefinition::bIsElite`, `UTDMonsterDefinitionLibrary::Get/FindDefinition`, `FTDMonsterTacticsFragment::TokenState`, `ETDTacticsTokenState`, `ETDTacticsRole::{Melee,Charger,Support}`, 디버그 줄 `alarm=hop2`·`morale=steady|fear|panic|rallied`.

## 새 시험 5개

| 이름(`TDGame.MonsterAI.Pack.` 접두) | 장면 | 핵심 단언 |
|---|---|---|
| LoneSurvivorHoldsNerve (B7 i) | 고블린 3 링 470, 2 s 뒤 16스텝 간격 2마리 치명 | 남은 1마리 `morale≠steady` 표본 0, 끝까지 교전, 살아 있는 줄 1 |
| RalliedPackPanicsOnce (B7 ii) | 고블린 6 링 470, 2마리 치명(16스텝 간격) → 생존 4 | 4마리 모두 1초 안 공황(==4), 5초 뒤 4마리 모두 `rallied`, 이어 같은 교전에서 2마리 더 치명 → 남은 2의 fear·panic 표본 0, 교전 유지 |
| ChargerCapDoesNotBlockMelee (C1 혼합) | 돌진자 2(링 550, 0°·180°) + 근접 2(링 650, 90°·270°), 20 s | 돌진자 보유 ≤ 1, 돌진자 보유 1인 스텝 > 0, 그중 근접 보유 ≥ 1인 비율 ≥ 50%, 근접 둘 다 시전 ≥ 1 |
| ChargerRetaliationAllowsSecondHolder (C1 보복) | 돌진자 4(링 600), 첫 시전 시작(보유 1)에 토큰 없는 돌진자 전원 `PackProbeHitDamage` 피격 | 피격 전 보유 ≤ 1, 피격 후 64스텝 창 안 최대 보유 == 2, 64+320스텝 뒤 ≤ 1 |
| RangedBandFloorFormula (C3 단위) | 월드 없음 | (650,40,900)==700, (650,40,750)==650, (400,40,900)==540, (0,40,900)==0, (-20,40,900)==0 |

## 바뀐 단언

| 시험 | 바뀐 단언 | 수치 근거 |
|---|---|---|
| A1 AlarmRipplesTwoHops | A→B, B→C 지연 ∈ [13,15] (이전 [8,16]) | `ComputeHopDelaySteps(700)` = 8 + 8×700/900 = 14 |
| A4 OneShotKillAlertsNeighbors | 이웃 교전까지 틱 ∈ [9,16] (하한 9 추가) | 600 cm → 13 + 표본 보정 1 = 14 |
| B1 LeaderDeathScattersPack | 공황 개체마다 최장 런 ∈ [126,194]; 그 런의 거리 이득 ≥ 150 cm; 공황 표본 대상 거리 최대 ≤ 760; 줄 매칭 0 | 우두머리 공포 128~192, 움츠림 700 |
| B2 BurstLossesPanicSmallPack | 개체마다 최장 런 ∈ [62,130](상한 147 대체); 공황 표본 거리 ≤ 760; 첫·둘째 처치 사이 생존 4마리 `≠steady` 표본 0; 줄 매칭 0 | 일반 공포 64~128 |
| B3 LargePackHoldsNerve | `panic` 표본 → `morale≠steady` 표본(첫 처치부터 끝까지) 0; `Tactics==1`; 줄 매칭 0 | |
| B3b SlowLossesDoNotPanic | 간격 4 s → 96스텝(1.5 s); 관측은 둘째 처치 뒤 4 s까지; `≠steady` 0; `Tactics==1`; 줄 매칭 0 | 창이 96 이상으로 늘어난 회귀는 잡고, 81~95 회귀는 놓침 |
| B4 TimedRetreatEndsOnTime | 1 cm/스텝 표본 추가 → Idle ∈ [24,26] | 진행 문턱 30 cm 못 넘어 정체 24 |
| B6 LowHealthFleeDoesNotLoop | 줄 매칭 0 | |
| C1/C2 (CheckPackExclusiveRole) | `MaxHolderCount == 1` 관측 단언 추가, `Tactics==1`, 줄 매칭 0, 역할 `static_cast<ETDTacticsRole>(3|4)` → `ETDTacticsRole::Charger|Support`. 기존 `≤ 1` 보유·시전 단언은 유지(시전은 판별력 없음, 회귀 감시용) | |
| C3 RangedHoldsBand | 줄 매칭 0 | |
| C4 PackDefinitionKeys | elite 인라인: `bIsElite`·`bIsLeader`·`bIsImmune` 모두 참; Support: 면역 참·우두머리 거짓·elite 거짓; Charger: 셋 다 거짓; 출하 StoneGolem·BookHead_Brute: 셋 다 참 | |
| G1 Deterministic | 장면: 링 5 중 첫째를 StoneGolem으로 교체(9마리 유지, 7마리 생존 단언 유지), 골렘 → 고블린 순 처치. 단언: fear·panic 표본 > 0, `alarm=hop2` 표본 > 0, 돌진자 보유 최대 ≥ 1, 두 실행의 세 관측값 동일, 줄 매칭 0, `Tactics==1` | 골렘 사망 우두머리 충격으로 공황이 확정 |
| G2 EventOrderInvariant | 장면: 고블린 6(x=2000, y=-1000..1000 간격 400), 같은 프레임 비치명 2(1·4) + 치명 2(2·3)를 정순(비·비·치·치)과 역순(치·치·비·비); 4마리 생존, 비치명 둘 다 체력 감소 > 0, 줄 매칭 0, `Tactics==1` | |

## 돌연변이 대응

| 돌연변이(R2) | 잡는 시험 |
|---|---|
| 경보 지연 상수(8 또는 16)·원점 대신 대상 거리 계산 | A1 [13,15] |
| 사망 경보 지연 0 | A4 하한 9 |
| 혼자 남은 개체 공황 허용 | B7 i LoneSurvivorHoldsNerve |
| 결속(bHasRallied) 무시 | B7 ii RalliedPackPanicsOnce |
| 손실 창 81~255 | B3b 96스텝 간격 |
| 우두머리 충격에 일반 공포 사용 / BeginRetreat가 Steps 무시 | B1 최장 런 [126,194] |
| 움츠림 거리 700 무시 | B1·B2 공황 표본 거리 ≤ 760 |
| 공포 표시만 켜고 공황으로 안 세는 도주 | B3·B3b `≠steady` 0 |
| 첫 처치만으로 공포(MinLoss 1) | B2 첫·둘째 사이 `≠steady` 0 |
| 진행 문턱 30 cm → 0 | B4 1 cm 표본 |
| 상한 걸린 후보가 하위 후보를 막음(break) | ChargerCapDoesNotBlockMelee |
| 보복 +1 무시 | ChargerRetaliationAllowsSecondHolder (창 안 == 2) |
| 띠 바닥 상한 700 무시·몸 반경·간격 누락 | RangedBandFloorFormula |
| elite 무시, 면역 규칙 | C4 플래그 단언 |
| 해시 시험이 공허 | G1 관측 단언, G2 확대 표본 |
| 전술 꺼짐·줄 불일치로 공허 통과 | 각 시험 `Tactics==1`, `UnmatchedLineCount==0` |

## 확실하지 않은 수치(구현 실측 없이 정함)

- B1 이득 ≥ 150: 골렘 우두머리 충격 때 고블린이 근접 위치(대상 거리 약 160~550)에 있다는 가정, 공황이 700에서 멈추므로 시작 거리가 550을 넘으면 실패.
- B1 [126,194]·B2 [62,130]: 런이 이웃에 막혀 24스텝 정체(`bWasRetreatCornered`)로 일찍 끝나거나 경직으로 끊기면 실패.
- B2 6마리 링(B7 ii) 첫 물결: 가중 4 ≥ 지지 4 경계(등호)로 공황. 구현 식(`ShouldPanic`)상 확정이지만 지지 가중 상수가 바뀌면 깨짐.
- C1 혼합 ≥ 50%: 지휘자 단계가 BuildUp(예산 100%)이라 근접 풀 토큰 2개 가정. Relax(50%)면 1개라 비율이 내려감.
- C1 보복: 첫 시전 시작 순간에 피격시키고 창 안 보유 2를 기대, 창 밖 허용 여유 320스텝(예약 만료 256 + 시전)은 임의 값.
- G1: 골렘 사망 충격이 연쇄 장면(A~D)의 `hop2` 개체에도 닿아도 문제 없다고 가정.

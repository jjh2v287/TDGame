# M3-35 측정 요약 — 공용 보스 기반과 3페이즈 용 보스(2026-10-18 claude)

## 결정론 시험 `TDGame.MonsterAI.Boss.*`(7개, `Source/TDGame/MonsterAI/Tests/TDBossFoundationTests.cpp`)
| 시험 | 구현 전(빈 구현 빌드) | 구현 뒤 |
|---|---|---|
| ConeLineRingShapes | 실패: 원 판정이라 반각 밖 (400,300)·뒤 (−200,0)이 맞음 | 통과 |
| TakeOffHoldsAltitudeThenLands | 실패: `TakeOff` 미등록 원시 | 통과 |
| SummonMinionsSpawnsEngagedRing | 실패: `SummonMinions` 미등록 원시 | 통과 |
| DangerAttacksNeverOverlap | 실패: `danger` 미지 키 | 통과(대조 실행은 겹침 ≥2) |
| PhaseEnteredNotifiesAndQueries | 실패: 알림 0건 | 통과 |
| WindupDelaysInstantSpell | 실패: `windup` 미지 키 | 통과 |
| DefinitionKeysResolve | 실패: 새 키 7개 오류 | 통과 |

- 빨강: `red-before.json`(M3-36 헤드리스 3회차, 같은 트리, 166개 중 157 통과 = Boss 7 + 기준선 CrossingStreams + 당시 M3-36 시험 1).
- 초록: `green-tests.json`(라벨 m335-green1, 필터 MonsterAI·Combat·Movement·Encounter·Smoke) 165/166, 실패 = 기준선 `CircleAvoidance.Scene.CrossingStreams` 1건. `check_sync_loads` new 0, `check_cook_coverage` uncovered 0(MISSING `/Game/MonsterAI/Attacks`는 기존).

## PIE `pie-20261018/`(`Tools/MonsterAI/pie_check_dragon_boss.py`, LV_TDMegaMagicArena)
- 3회 실행 모두 통과. 기록은 마지막(3회차) 것: 페이즈 0→1 16.1초, 1→2 46.8초, 2→3 51.0초. 비행 19.3→43.4초(24.1초, 고도 337cm, 목표 350). 졸개 3. 위험 공격 동시 최대 1. 처치 확인.
- 관측된 용 공격: 화염 방사(보라 부채꼴)·화염구·잔불·포효·공중 화염구 14회·운석 비·착지 충격·업화+안전 지대. 0.5초 표본이라 물기·할퀴기처럼 짧은 근접은 잡히지 않음(탐침 플레이어가 900cm에 서 있어 원거리 위주).
- 피해 넣기: 용 체력은 탐침이 플레이어 명의 `TryApplyDamage`로 66·38·24·0%까지 깎는다(실제 손 조작 아님). 플레이어는 시험용으로 최대 체력 1000(최저 625, 회복 0회). 2회차는 체력 100인 플레이어가 공중 단계에서 죽어 대상 상실 → 귀환 착지(규칙대로)였고, 1회차는 체력 100 + 탐침 회복 3회로 통과.
- 스크린샷: 1_ground(보라 부채꼴 화염 방사), 2_sky_minions, 3_sky_attack(운석 비 보라 원 5), 4_landed, 5_enraged, 6_inferno(빨간 업화 영역 + 용 주변 청록 안전 지대), 7_death.
- 교훈 L-editor-34: 예고 재질을 만든 같은 에디터 세션에서는 카탈로그 예고가 디버그 와이어로 굳는다 → 재시작 뒤 재측정(1회차 6_inferno는 와이어였음).

## 용 실측(축척 1, `Saved/MonsterAI/dragon_measure.json`)
- 경계 상자 반크기 (947, 781, 467)cm, 몸 길이 약 1345cm, 머리 +Y(MeshYaw −90). 걷기 지면 속도 444, 달리기 795cm/s. 이륙 클립 상승 616cm/2.17초. 2연 할퀴기 타격 0.74·1.79초(간격 1.05초 → 카탈로그 반영).
- 종 표 `MountainDragon`: 축척 0.5, 캡슐 반경 150·반높이 160.

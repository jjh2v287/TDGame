# 무리 행동 3종(경보 연쇄·사기와 공포 전염·역할 독점) 설계·근거 (M3-30·M3-31·M3-32)

작성: 2026-10-18 claude. 대장 [07 M3-30~32·MD-48·MD-43](../07-roadmap-and-tasks.md). 명세 정본 [measurements/pack-behaviors/spec-v2.md](../measurements/pack-behaviors/spec-v2.md)(10절 v2.1)·[contract-v2.md](../measurements/pack-behaviors/contract-v2.md). 시뮬 [measurements/squad-tactics/sim_pack.py](../measurements/squad-tactics/sim_pack.py)·[sim_pack_tables.md](../measurements/squad-tactics/sim_pack_tables.md). 반박 [measurements/pack-behaviors/refutations/](../measurements/pack-behaviors/refutations/). 문헌 [web-topdown-arpg-crowd-combat.md](web-topdown-arpg-crowd-combat.md) §3.3~3.7·§6 순위 2~4, [web-squad-slots-and-directors.md](web-squad-slots-and-directors.md) A2·A3.

## 1. 결론

- M3-14 조사의 추가 가치 순위 ②~④를 결정론 규약(고정 스텝 1/64초, SimulationId 순, 전역 난수·TMap 순회·월드 타이머 금지, 슬롯 배열 소유 D14, 유틸리티+FSM D1) 안에서 넣었다. 커널 순서: Sense → 교전(경보) → **사기** → 전술(토큰·자리) → 사고·실행.
- 규칙은 새 파일의 정적 순수 함수(`MonsterAI/TDPackAlarm.*`·`TDPackMorale.*`, `FTDCombatTactics` 역할 함수)에, 서브시스템에는 접착만 두었다. `TD.MonsterAI.Tactics 0`이면 세 기능 모두 꺼지고 기존 즉시 1단 경보로 돌아간다(시험 `Pack.TacticsOffKeepsInstantAlarm`).

## 2. 경보 연쇄(M3-30, TLOU 비명·추종)

- 발견자(근접 발견·피격, 한 방에 죽은 Home 몬스터 포함)마다 900cm 안 같은 팀 Home 동료를 `8 + 8×거리/900`스텝 뒤(8~16) 깨우고, 깨어난 동료가 한 번 더 전파한다(최대 2단). 가까운 동료가 먼저 깨어 파문이 퍼진다. 시뮬 S1: 덩어리 12마리(500 간격) 기상 8 → 12, 3단 이상 0.
- 호출자 표시는 무리당 첫 발견자 1명(같은 종·같은 대상·1800cm·3초). 표현층 진입점 `GetMonsterAlarmRole`(외침·머리 위 아이콘 자리, 아직 연결 안 함), 디버그 줄 `alarm=caller|silent|hop1|hop2`, `TD.MonsterAI.Tactics.Debug 1`이면 "!".

## 3. 무리 사기·공포 전염(M3-31, Days Gone 단순화·Halo·Diablo II Fallen)

- 단계: 평상 → 공황(1~2초 후퇴, 우두머리 사망은 2~3초) → 결속(이번 교전 동안 면역) → 귀환하면 평상.
- 판정(정수): 1.25초(80스텝) 안 목격 손실 ≥ 2, 동료 1 이상, `2×손실 + 공황 동료 ≥ 1 + 비공황 동료 지지`(정예·보스 2배). 우두머리(정예·보스) 사망은 1600cm 안·다른 우두머리가 없을 때 바로 공황. 보스·정예·지원 역할은 면역. 사망·공황 시작이 있는 스텝만 스냅샷 판정(순서 무관), 공황 진행은 매 스텝.
- 후퇴: 실행기의 시간 제한 후퇴(속도 1.2, 대상 700cm에서 움츠림), 24스텝 동안 30cm를 못 벌면 '막힘' → 즉시 싸움, 경직·빙결로 끊기면 남은 16스텝 이상일 때 재개. 공황 중에는 토큰·자리에서 빠진다.
- 손실 창 근거(시뮬 v2, 구성 7 × 처치 모형 3): 창 80은 꾸준 처치(1.5초당 1) 공황 무리 0/7·범위 처치(1초 3) 6/7·느림(4초당 1) 0(우두머리 충격 제외), 공황 가시 1.34초. v1의 '8초 연쇄'와 창 192는 꾸준 6~7/7로 매 전투 1/3 지점에서 무리가 흩어졌다(반박 B-1). 공황은 '폭발 처치의 보상'으로만 나온다.
- MD-43 정리: 저체력 Flee도 같은 시간 제한 후퇴를 쓴다(`seconds` 1.5·`cooldown` 6·대상 근접 한정·체력 약 32% 미만). 시뮬 S3: 구 규칙 공격 0·60초에 귀환 3~12회 → 신 규칙 귀환 0(정지·카이팅), 도망 시간 ≤ 25%.

## 4. 역할 독점(M3-32, TLOU 전투 조정자·Diablo IV 역할)

- `role`에 `Charger`(근접 차선)·`Support`(원거리 차선, 사기 면역)를 더했다. 대상마다 그 역할의 토큰 보유자(예약·확정)는 1명, 보복 중인 후보만 +1. 상한에 걸린 후보는 건너뛰고(예산 부족 중단과 다름) 낮은 순위 다른 역할은 계속 받으며, 막힌 돌진자가 있으면 대기 고리를 켜 선회시킨다.
- 원거리 띠: 원거리 차선의 띠 바닥을 근접 대기자 바깥(고리+몸 최대 + 100 + 자기 몸, 상한 min(사거리−100, 700))으로 둔다. 시뮬·엔진: 골렘 4 + 궁수 2에서 궁수 중앙 거리 540 → 641cm. 고블린만이면 바닥 530이라 그대로.
- 처리량 비용: 상한 1은 초당 공격 시작을 하이에나 4마리 0.74배·6마리 0.67배로 줄인다(시뮬 v2 S4) → Hyena는 `Melee` 유지, 돌진자 지정은 MD-48 결정 대기.

## 5. 검증

- 빨강(구현 전, [red-before-summary.md](../measurements/pack-behaviors/red-before-summary.md)): `TDGame.MonsterAI.Pack.*` 17개 중 빨강 12/12 실패·보호 5/5 통과, 기존 MonsterAI 50/51(기준선 CrossingStreams).
- 초록: 정본 실행 결과는 07 M3-30~32 기록 참조.
- 반박: 명세 v1 3관점(치명 3·주요 20·경미 25) → v2, 시뮬 v1·v2 재검증 → v2.1(손실 창 80, C3 합격선), 구현 정적 반박 2관점(코드·시험 판별력).

## 6. 알려진 한계·후속

- 연출 없음: 호출자 외침·공황 움츠림 포즈·아이콘은 진입점만 있다(표현층 후속). 반박 B-4: 기본 카메라에서 경보 파문은 대부분 화면 밖이다.
- 띠 바닥은 이동 띠만 옮기고 점수기 거리 곡선(JSON KeepDistance 중심 648)은 그대로라 궁수가 바닥 670이 아니라 641에서 멈춘다. 카이팅 중에는 바닥이 유지되지 않는다(시뮬 610 미만 54~62%).
- 하이에나 Flee는 무리 안에서 거의 발동하지 않는다(단독·저체력·225cm 안).
- 사기 상승(Days Gone 정지 플레이어·처치한 적 힘), 우두머리 오라(순위 6), 공황 개체 전용 피격 반응은 범위 밖.

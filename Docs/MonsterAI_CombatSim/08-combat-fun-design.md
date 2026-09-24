[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 08. 탑다운 전투 재미 설계와 몬스터 AI·공동 제작 방식(제안)

종류: 설계
작성: 2026-09-24·claude
상태: **proposed(사용자 승인 전).** 구속력 있는 결정은 00이며 결정 후보는 승인 뒤 00에 D39 이후로 옮긴다. 07 §4의 MD-01~10은 2026-09-18 사용자가 관리 체계 정리 뒤로 보류했으므로, 이 문서는 선점하지 않고 V0를 막는 결정을 MD-11·14로 줄였다.

입력: 참고 대화(ChatGPT), 조사 노트 5편, 설계안 A(관계·반응)·B(가독성·동사)·C(제작·검증), 비평 3편(결함 반영 위치는 부록).
표기: [확인] 파일·1차 자료 / [가정] 계산·추론·초안 수치 / [제안] 이 문서의 설계. 줄 번호는 2026-09-24 작업 트리(미커밋 포함).
약어: FSM(유한 상태 기계), SoA(Structure of Arrays, 배열 구조체), GAS(Gameplay Ability System), GE(Gameplay Effect), DA(Data Asset, 여기서는 공격 정의 `UTDDamageDefinition`), CMC(캐릭터 이동 컴포넌트), LOD(세부 수준), FOV(시야각), VFX(시각 효과), PIE(에디터 내 플레이), TTK(처치 소요 시간), HUD(화면 정보 표시).
ID 접두어(신설): SC 화면 계약 / CG 전투 문법 헌장 / VR 검증기 규칙 / IX 상호작용 / EN 조우 / PS 페르소나 봇. D(00 결정)·MD(07 §4 사용자 결정)·M(07 할 일)은 기존.

> 쉬운 말로: 몬스터를 많이 죽이는 게임이 아니라 **몬스터를 밀고 당겨 서로·벽·장판에 부딪히게 만드는 게임**으로 만든다. 몬스터 두뇌(유틸리티 AI + 상태 기계)는 지금 결정대로 단순하게 두고, 부딪힘·장판·원한 같은 규칙은 두뇌 밖의 규칙표 하나(`CombatGrammar.json`)에 모은다. 사람은 "재미있나"를 판정하고 에이전트는 JSON 작성·검증·시뮬 측정을 맡는다. 첫 단계는 회색 상자 맵에서 "밀기 자체가 재밌나"를 1~1.5주 안에 손으로 확인하는 것이다.

## 이 문서가 답하는 질문

1. 러시가 광역기 반복으로 수렴하지 않는 전투를 무엇으로 만드나. 참고 대화에서 무엇을 차용하고, 더 나은 축은 없나.
2. 고정 -60도·800cm·무회전 카메라의 제약과 이용법은.
3. 맞는 몬스터 AI 모델은. D1(유틸리티 AI + FSM)로 충분한가.
4. 1인 개발자와 AI 에이전트가 싸게 함께 만들고 검증하는 절차는.
5. 무엇부터 만들고 무엇으로 판정하나.

## 결론 요약(결정 후보 문장)

1. 재미의 단위는 처치가 아니라 **예고를 읽고 → 적을 옮기고 → 부딪힘으로 정산하고 → 바닥에 남은 결과를 다음에 쓰는** 3~8초 [가정] 한 바퀴다. 러시는 그 탄약이다.
2. 복잡성은 두뇌가 아니라 몬스터끼리·바닥과의 **닫힌 충돌 규칙표**에 둔다. 새 종은 태그만 달면 기존 상호작용을 물려받는다.
3. **위협은 예산으로 묶고 혼돈은 플레이어에게 준다.** 판정 정보는 모두 바닥에, 몸에 가리지 않게(SC-01~11).
4. 짧은 남쪽 시야(약 391cm)는 완화 규칙과 V0 카메라 비교로 다룬다.
5. 몬스터 AI는 **D1 그대로**다. 두뇌 밖에 충돌 반응층과 공격 순번 조정자만 붙이고, 차례가 아닌 다수는 떼를 '탄약 모양'으로 만든다(§6-8).
6. **플레이어 강인도**를 먼저 정한다. 잔피해는 콤보를 끊지 않는다.
7. **동사 먼저**: V0(1~1.5주) '밀기 자체가 재밌나', V1(1주) '적끼리 재료가 되나'를 사람이 판정한 뒤 시뮬에 투자한다. V0 실패면 커밋·패링 축으로(§3-0).
8. 공동 제작은 '문법·재미는 사람, 콘텐츠·측정은 에이전트'. 종당 컴파일 0회는 공격 형상 팔레트(C++ 1회, §7-1)가 전제다. 지표는 결정론 봇 5종으로 재되 판정 30건 전엔 경고로만.

### 30초 플레이 서사(EN-02, [제안])

1. 방에 들어서면 1.5초 동안 아무도 공격하지 않는다. 북쪽 벽 뒤 오크 궁수 둘, 고블린 넷, 슬라임 셋.
2. 오크가 바닥에 붉은 조준선을 긋는다. 0.8초 뒤 쏜다.
3. E(갈고리)로 고블린을 끌어와 조준선에 세운다.
4. 화살이 고블린 등에 박히고 "!"가 뜬다. 고블린이 오크에게 달려간다(원한).
5. 다가오는 슬라임에 Q(밀쳐내기)를 길게 눌러 궤적을 본다. 실선 끝이 다른 오크에 닿는다.
6. 떼면 슬라임이 오크에 부딪혀 터지고, 오크 발밑 초록 고리(점액 묻음)가 사격을 늦춘다.
7. 터진 자리 웅덩이 위로 돌진한 고블린이 미끄러져 벽에 박힌다.
8. 광역기 없이 셋이 쓰러졌고, 다음 10초는 웅덩이가 좌우한다.

### 지금 할 일 3개(사용자)

1. **MD-11**: 미커밋 변경(데미지 파일, `TDGameCharacter.cpp`, `TDGamePlayerController.cpp/h`, `TDCombatCharacter.h`) 정리 시점을 정한다.
2. **MD-14**: V0 착수 승인(기본 동사 Q 밀쳐내기·E 갈고리, 강인도는 V0 폰 전용 플래그).
3. V0 PIE 5~10분 × 2회 판정. MD-12·13·17은 그 결과로, 나머지 MD는 해당 단계에서 정한다.

---

## 1. 참고 대화 분석: 차용·수정·기각 표

진단(역할 조합은 수렴)과 처방(몬스터 간 관계·전장 상태)은 조사와 일치한다. 빠진 것은 예고 문법·페이싱·손맛·측정이다. 동사 12개·5축을 한꺼번에 넣으면 PoE(Path of Exile) Archnemesis식 과복잡이 된다(GGG 2022).

| 아이디어 | 판정 | 이유 |
|---|---|---|
| 러시는 광역기 반복으로 수렴 | 차용 | PoE2 '선제형 → 반응형' 진단(maxroll 2023). 러시는 탄약 |
| 방패·궁수·지휘관·폭탄 역할 조합 | 수정 | 대화도 수렴 인정. 구성 역할 4종(§5) |
| Push/Pull/Launch/Tether/Swap | 수정 | 1급은 밀기·당기기. Launch는 높이가 안 읽힘 |
| 벽꽝·충돌 스턴·폭발몹·얼린 몸 파쇄 | 차용, 파쇄 보류 | 기존 파쇄는 빙결 만료 피해라[확인: `TDDamageExamples.cpp:75-76`] 충돌 파쇄는 신규 |
| 시체가 전장 상태 | 수정 | 슬라이스 1은 사망 시 웅덩이만 |
| 강한 스킬 → Heat → 난입 | 수정 | 전투 중 벌점은 빌드를 벌한다. Hades Heat는 탈출 시작 전 Pact of Punishment로 정하는 런 단위 계약이고 방 입구 단위 변형은 [제안](§5) |
| 40마리 중 8마리만 공격 | 차용 | 가중 위협 예산(DOOM 2016 토큰, SC-03) |
| 태그 Reaction Rule 표 / 군중 조각 | 차용·수정 | 전역 표 1개, 층별 1행, 난수 0(§6-4) / 두꺼비 볼링 규칙에서 나오는지 잰다 |
| Combat Grammar 5축 | 수정 | 공간·관계·시간은 문법, 목표 규칙은 조우 변형 EN-05. 한 번에 한 축 |
| 주변 상태가 역할을 정한다 | 수정 | 고려사항 종당 1개, 바닥 예고 |
| 피해 공유망 / 졸개 먹는 보스 | 보류 | 피해 흐름이 안 보임 / 갑충으로 축소 검증 |
| 지휘관 역할 재부여 / 진화하는 조우 | 기각 | 보이지 않는 배정(D3 HTN(계층적 태스크 네트워크) 조건) / 카운터 AI는 불공정 인식(Švelch 2020) |
| 관계 동사 12개 먼저 | 수정 | 능동 관계 행동 종당 1개 이하. Merge·Possess 보류, 나머지 4개 기각 |
| 슬라임·고블린·오크 5개 / 개별 AI 단순 | 수정 / 차용 | 3개 차용·2개 수정(§4-2) / 두뇌 예산 VR-02, 토큰 없는 행동 §6-8 |

---

## 2. 고정 탑다운 시점(-60도, 800cm, 회전 없음)의 제약과 이용법

### 2-1. 사실과 기하

| 항목 | 값 | 근거 |
|---|---|---|
| 스프링암 | 800cm, 피치 -60도, 절대 회전. 화면 위 = +X(북), 오른쪽 = +Y | [확인] `TDGameCharacter.cpp:56-62` |
| FOV | 코드 미지정 → 90도(카메라 화면비 1.777778 기준 수평). 엔진 기본 `AspectRatio_MaintainYFOV`라 세로 약 58.7도 고정 | [확인] `CameraComponent.cpp:78-81`, `BaseEngine.ini:2899-2900`, `CameraStackTypes.cpp:326-333`. 블루프린트 덮어쓰기 미확인 |
| 보이는 지면 | 북 약 932 / 남 약 391cm(화면비 무관), 16:9 폭은 북 2,700·남 1,375 | [가정] 피벗 96cm(research/web-topdown-readability.md §0) |
| 높이 착시·가림 | 높이 h가 약 0.58h 북쪽 바닥을 가림(플레이어 약 1m, 오우거 약 1.7m) | 같음 |

남쪽 끝의 1,500cm/s 투사체는 약 0.26초에 닿아 반응 시간 중앙값 273ms(Human Benchmark)보다 빠르다.

### 2-2. 제약 → 규칙

- **남쪽 사각**: SC-05 완화형(화면 밖 개시 금지, 남쪽 4분의 1 개시 시 선딜 +0.3초, 남쪽 스폰 금지). 전면 금지는 북벽에 붙어 원거리를 봉인하는 치즈를 만든다(PS-05로 검사).
- **밀린 적이 남쪽 밖으로**(Light 450 > 391cm): 가장자리 화살표 + 결과 아이콘, 모루는 북쪽 띠(VR-09).
- **높이**: 판정은 XY 평면, 띄우기 없음, 남쪽 높은 벽 금지. **몸이 예고를 가림**: SC-11.

### 2-3. 이점

방향 불변, 바닥 캔버스, 결정론 미리보기(Into the Breach '완전 정보'의 실시간판), 북쪽 볼링 레인(모루를 북쪽 약 400~900cm에). **논리 시야 `FTDTopDownView`**는 고정 상수(세로 FOV + 지원 최소 화면비 16:9)로 계산하는 순수 함수라 AI 입력·지표·리플레이가 공유하고 게임과 시뮬이 같게 행동한다(D21·D23).

### 2-4. 카메라 대안(MD-17, 세로 FOV 58.7도 기준 [가정])

| 안 | 남 / 북 | 장단점 |
|---|---|---|
| 현행 + SC-05 | 391 / 932cm | 코드 0, 사각 잔존 |
| 세로 FOV 고정 명시 | 같음 | 기본값을 `DefaultEngine.ini` `[/Script/Engine.LocalPlayer]`에 명시(변경 대비), 카메라 단위는 `bOverrideAspectRatioAxisConstraint`(`CameraComponent.h:113-126`) |
| 타깃 오프셋 남 150 / 250cm | 541 / 782, 641 / 682cm | 한 줄. 캐릭터가 위로 치우치거나 북쪽 레인 단축 |
| 커서 리드 | 조준 쪽 증가 | 멀미 위험, 논리 시야 불포함 |

이전 초안의 '21:9 남 306cm'는 수평 FOV 유지를 가정한 오류다. 현행 설정에서 21:9는 동서만 넓어지고, 16:9보다 좁은 창(4:3·세로)은 동서가 논리 시야보다 좁아져 `bConstrainAspectRatio` 레터박스 안을 MD-17에 넣는다. V0 PIE에서 21:9·4:3 창과 오프셋 150cm를 확인한다.

---

## 3. 전투 재미 설계: 원칙 → 시스템 → 규칙

### 3-0. 재미 축 대안 비교

조작 축은 가설 H1 하나에 걸려 있어 대화 밖 축과 비교하고 전환 경로를 미리 둔다.

| 축 | 탑다운 적합도 / 비용 | 기존 결정 | 판정 |
|---|---|---|---|
| 조작(밀기·당기기·충돌) | 높음(바닥 예고, 방향 불변) / 풀이기·반응층 | D19 확장 1건 | **채택**, V0가 반증 시험 |
| 커밋·패링(No Rest for the Wicked, 2024) | 중간(느린 탑뷰 사례, 적 수가 적어야 해 러시와 충돌) / 패리 창 | 그대로 | **V0 실패 때 1순위.** 화면 계약·강인도·토큰·봇 재사용, 풀이기는 넉백만 |
| 빌드·광역 성장(PoE류) | 높음 / 주문 10종 있음 | 호환 | 단독 불채택(수렴의 원점), 기세로 조작에 묶음 |
| 목표형 조우(Into the Breach 생존·보호) | 높음 / 조우 규칙만 | 호환 | 결합형. Davis의 '조작 > 처치'는 승리 조건을 생존·보호로 바꾼 뒤 나왔으므로 EN-05로 시험 |

### 3-1. 원칙

1. 읽힘이 먼저다. 화면 계약을 어기는 재미는 채택하지 않는다.
2. 조작이 곧 공격: "Killing enemies isn't as fun as manipulating them"(Davis, GDC 2019). 동사 피해는 낮고 충돌이 값을 낸다.
3. 결정 순간에 무작위가 없고 충돌 피해에 치명타가 없다.
4. 적은 서로의 재료다.
5. **위협은 예산, 혼돈은 플레이어 몫**(Magicka는 중반 저하를 '혼돈 부족'으로 진단, 2011).
6. 박자는 진폭이 아니라 빈도(L4D(Left 4 Dead), Booth 2009).
7. **몬스터는 반응을 노리지 않는다**(1단계 반사만, CG-09).

### 3-2. 화면 계약 SC-01~11(수치 [가정])

| # | 규칙 | 초안 |
|---|---|---|
| SC-01 | 피해 공격은 지면 예고, 판정은 예고 끝. 형상별 값은 `screen_contract`에서 유도해 VR-03·`unreadable_hit_share`가 공유 | 최소 선딜: 접촉 0.3, 부채꼴 0.45, 돌진 화살표 0.6, 조준선 0.8, 원 1.0초 |
| SC-02 | 예고 의미 4종, 색 + 무늬 | 피해 붉은 채움, 변위 파란 화살, 제어 보라, 관계 청록 점선 |
| SC-03 | 동시 위협 가중 예산(토큰이 강제) | 쉬움 2 / 보통 3 / 어려움 4. 비용: 접촉 0.25, 부채꼴 0.75, 나머지 1.0 |
| SC-04 | 동시 예고 모양 3종 이상이면 새 예고 +0.15초 | 힉의 법칙(Proctor & Schneider 2018) |
| SC-05~07 | 남쪽 완화형 / 관계는 몬스터당 1·화면 안 3 이하 / 장판 종류 2·화면 안 6 이하 | +0.3초 / 0.5초 전 예고 / 새것이 대체 |
| SC-08·09 | 실루엣 크기 = 질량 / 공정성은 플레이어 쪽으로 | 피격 판정은 시각의 80%, 받는 충돌 피해 ×0.3 |
| SC-10·11 | 첫 1.5초 개시 금지 / 예고·장판 외곽선은 몸 위에, 겹친 몸은 실루엣 페이드 | 스텐실 외곽선 또는 깊이 검사 끈 반투명 |

근접 0.45초: 273ms(단일 자극 최상 조건)에 선택 반응·입력 지연을 더했다(초견 테스트로 조정).

### 3-3. 플레이어 동사와 키트 예산

[확인] 현재 능력은 콤보 1~3, Q(스태미나 20), E(35), 구르기, 점프(`TDCombatActionAbility.cpp:431`), 숫자 키에 시험용 광역 주문 10종(`TDGamePlayerController.cpp:413`)이다. 광역 반복이 지배하지 않도록 **활성 동사는 5개 이하**다.

| 동사 | 입력(V0 기본값, MD-12) | 수치 [가정] | 역할 |
|---|---|---|---|
| 베기 1·2 / 마무리 밀기 | 콤보 1·2 / 콤보 3 | 밀림 50cm / 질량별 50~250cm | 손맛 / 콤보가 배치 도구(검 콤보 작업 뒤) |
| 밀쳐내기 | Q | 부채꼴 90도, 120~450cm | 주 조작 |
| 갈고리 | E | 직선 700cm | 당기기·이동기 |
| 구르기 / 마무리기 | 기존 / 숫자 키 1칸 | 무적 / 기존 주문 1종 | 회피 / 광역 기준선(PS-02) |

**[제안·가설] 기세**: 조작 처치 +3, 직접 처치 +1, 마무리기 30 소모로 광역기를 조작의 정산으로 만든다(DOOM '처치가 자원', Game Developer 2017 / PoE2 '후반의 신이 된 느낌', maxroll 2023). 슬라이스 1 뒤 A/B.

### 3-4. 질량과 충돌

| 등급 | Q | 마무리 | E 갈고리 | 벽꽝 배수 | 예 |
|---|---|---|---|---|---|
| Light | 450 | 250 | 내 뒤 150cm로 넘김 | 0.6 | 슬라임 |
| Medium | 300 | 150 | 내 앞 120cm | 1.0 | 고블린·두꺼비 |
| Heavy | 120 | 50 | 내가 끌려감 | 1.5 | 오크 |
| Anchored | 0(면역 아이콘) | 0 | 내가 끌려감 | 움직이는 벽 | 오우거·보스 |

충돌 결과 [가정]: **벽꽝**(벽 선분) 원인 뿌리 공격력 × 0.8 × 벽꽝 배수 × 잔여 속도 비, 경직 0.8초 / **볼링**(다른 몸) 둘 다 경직 0.5초, 맞은 쪽 공격력 × 0.5, 같거나 가벼우면 잔여 이동 60% 이어받음(깊이 3) / **장판**(점액 웅덩이) 밀림·돌진 거리 ×1.5. 충돌 피해가 원인 뿌리(동사를 건 쪽)의 공격력 계수라 성장 후반에도 광역기에 밀리지 않는지 `TDSetCasterLevel` 1/5/10에서 검사한다.

**플레이어 몸**(`player_body`): 충돌 판정은 Medium, 볼링 이어받기 면역, 스윕 질의 포함(돌진 적중). 받는 변위('내가 끌려감' 포함)는 입력 큐 → CMC 루트 모션 소스 [가정], 문법 상태는 GE 정본(§6-5).

### 3-5. 몬스터 설계 규칙

**종 카드 1-1-1+2**(CG-02): 극단 속성 1, 관계 1 이하, 상황 전환 1 이하, 비피해 동사 응답 2 이상(VR-04). 역할 대신 상황(고려사항, 예고 부호). 성격 한 줄(오크는 아군 사선을 검사하지 않는다). 표시 없는 면역 금지(VR-06).

### 3-6. 플레이어 강인도와 손맛

[확인] `UTDSkillComponent::HandleDamageReceived`는 피해가 0보다 크면 무조건 버퍼 삭제·콤보 리셋·피격 반응을 하고(`TDSkillComponent.cpp:335-345`), 플레이어(`TDGameCharacter.cpp:71`)와 몬스터(`TDMonsterCharacter.cpp:28`)가 함께 쓴다. 30마리 조우에서 칩이 콤보와 조작 창을 계속 끊는다. 강인도 규칙(MD-13) [제안]: (1) 문턱·하이퍼 아머는 컴포넌트 프로퍼티(예: `bUsesPlayerPoise` 기본 false)로 두고 플레이어만 블루프린트 기본값으로 켠다. 몬스터 피격 반응의 권위는 FSM `Stagger`(SoA)뿐이다. (2) 칩·지속·접촉·충돌 피해는 콤보를 끊지 않고, 피격 반응은 예고 있는 공격이면서 최대 체력 8% 이상일 때만 [가정]. (3) CG-07: 상태 효과는 이동만 바꾸고 입력 반응성은 바꾸지 않는다.

손맛: 몬스터 피격자 히트스톱은 FSM `Stagger` 앞부분(0.06~0.12초), 몬스터 공격자 정지는 메시만(D19)(Sakurai 2015). **플레이어 공격자는 몽타주를 멈추지 않고 카메라·VFX 정지로만 표현한다**: 0.06초(약 4스텝) 멈추면 노티파이 창이 밀려 판정이 바뀌고(D20) 시뮬 시간표와의 B단계 오차(±1스텝, D32)를 넘는다. 흔들림은 trauma² 모델(Eiserloh 2016), 최대 1도, 끄기 옵션.

### 3-7. 미리보기(약속 범위 한정)

'미리보기 = 결과'는 다른 몸이 멈춘 세계에서만 참이다. 탭은 즉시 발동 + 30도 원뿔 보정(Venturelli 2015) + 모루 자석. 길게 누르면 정적 벽 궤적은 실선(확정), 움직이는 몸과의 첫 접촉은 반투명(예상). 뗀 다음 스텝에 입력 큐로 발동. `PreviewMatchesOutcome` 정지 픽스처 100%가 필요조건, 실전은 `preview_drift` 보고.

### 3-8. 지배 전략과 장기 재미

밀기 난사는 `decision_value`·단일 동사 비중으로 감시하고 Q 비용 → Light 거리 순으로 조정한다. 정답 암기는 태그 상속·질문 단위 조우·깊이 시험(CG-10)으로, 성장 인플레이션은 공격력 계수 충돌 피해로, 보상 도태는 조우 단위 보상(Archnemesis 교체 원칙 2022)으로 막는다.

---

## 4. 예시: 슬라임·고블린 도끼병·오크 궁수의 상호작용 + 추가 몬스터 + 조우 예시

### 4-1. 슬라이스 1 종 카드(방마다 새 종 1개)

| 종 | 몸 태그 | 극단 속성 | 예고(형상) | 관계·반응 |
|---|---|---|---|---|
| 슬라임 | Light · Slime · Ooze | 아주 잘 밀림, 느림 | 접촉 0.3초 | 사망 시 웅덩이. Heavy에 부딪히면 터져 '점액 묻음' |
| 고블린 도끼병 | Medium · Flesh · Goblin | 돌진(자기 변위 500cm) | 부채꼴 0.45, 화살표 0.6초 | 다른 family에게 맞으면 원한 4초 |
| 오크 궁수 | Heavy · Flesh · Orc | 강한 조준 사격, 아군 사선 무시 | 조준선 0.8초 | 오사(아군 오인 사격) 유발 |
| 부풀 두꺼비 | Medium · Volatile | 자폭 | 원 1.0초 | 밀려 부딪히면 0.6초 뒤 폭발(250cm, 편 무관) |
| 오우거 방벽꾼 | Anchored | 아주 단단함 | 원 1.2초 | 움직이는 벽. **종 JSON만으로 추가(형상 팔레트 재사용, 새 C++ 0줄·컴파일 0회)** 시험 |

바닥 상태 '점액 웅덩이'와 몸 상태 '점액 묻음'(발밑 초록 고리) 하나씩이다(CG-04).

### 4-2. 상호작용 IX-01~07

상호작용은 데이터(반응 행 id + 인과 사슬 cause 패턴)이고 정적 행렬과 로그 집계가 같은 C++ 매처를 쓴다. 대화의 5개 중 3개 차용, 2개 수정.

| ID | 상호작용 → 플레이어 이용 | 행 / cause |
|---|---|---|
| IX-01 | 슬라임 사망 → 웅덩이(150cm, 8초), 위에서 밀림·돌진 ×1.5 → 어디서 죽일지 고른다 | `on_death` |
| IX-02 | 돌진 화살표가 웅덩이를 지나면 늘어나고 벽에 닿으면 자기 벽꽝 → 벽 앞에서 유인 | `WallSlam` / `charge:goblin>WallImpact` |
| IX-03 | 오크 화살(`Everyone`)이 조준선 첫 몸을 맞히고 맞은 고블린은 원한 → 갈고리로 고블린을 조준선에 | `FriendlyGrudge` / `shot:orc(target=player)>AllyHit` |
| IX-04 | 화살 맞은 슬라임은 터져 웅덩이가 되고 조준선이 끊긴다 → 살아 있는 엄폐물 | 새 행 없음 |
| IX-05 | 밀린 슬라임이 Heavy에 부딪혀 터지고 대상 '점액 묻음' 4초(이동 ×0.7, 몬스터는 선딜 ×1.3) | `SlimeSplat` |
| IX-06 | 밀려 부딪힌 두꺼비는 0.6초 뒤 폭발 → 떼 한가운데로 차 넣기 | `Bowling` + `VolatileFuse` |
| IX-07 | 돌진 경로의 슬라임은 볼링 핀 → 떼 앞에서 돌진 유도 | `Bowling` |

반응 행 5개, `on_death` 1개, 웅덩이 물리 1개로 7개가 나온다. 종 전용 새 원시는 `Charge` 하나이고 공격 판정은 형상 팔레트 재사용이다(§7-1). `Charge`는 플레이어 동사와 같은 풀이기라 미끄러짐·벽꽝·볼링이 따라온다. 돌진 적중은 풀이기 접촉 판정이다. 엔티티 모드와 스폰 기준점(EventLocation·Target·CastTarget)에 시전자를 따라 움직이는 판정이 없어서다(`TDDamageTypes.h:42-64`). D19의 두 번째 판정 경로라 D41에 적는다. A의 '끈적 화살'은 기각(날아가는 코팅은 안 읽힘).

### 4-3. 후속 후보(슬라이스 2 이후, CG-10 통과 시)

방벽병(`GuardBetween`) / 결속술사(링크 2개, 면역 대신 밀림 감소, 갈고리로 끊김) / 불씨 코볼트(Fire 자극) / 시체먹이 갑충(D14 확장) / 충돌 파쇄(06 §5-4에 '빙결 = 자기 이동 정지, 강제 변위 허용' 정의 뒤).

### 4-4. 조우 EN-01~05(약 12분)

| 조우 | 질문 | 새 개념 / 새 종 | 구성 | 가설(경고) |
|---|---|---|---|---|
| EN-01 복도 2분 | 어디로 밀까? | 밀기·벽꽝 / 슬라임·고블린 | 슬라임 6, 도끼병 3 | 조작 처치 비율 ≥0.3 |
| EN-02 사선 3분 | 궁수를 무엇으로 멈출까? | 조준선·원한 / 오크 | 오크 2(북쪽 벽 뒤), 도끼병 4, 슬라임 3 | IX-03~05 실현율 ≥0.7 |
| EN-03 골목 2.5분 | 두꺼비를 어디로 차 넣을까? | 폭발 / 두꺼비 | 슬라임 12, 두꺼비 2, 도끼병 3 | `decision_value` ≥1.2 |
| EN-04 볼링장 2.5분 | 떼가 탄약이 되나? | 움직이는 모루 / 오우거 | 슬라임 24, 두꺼비 3, 오우거 1, 오크 2 | `strategy_gap` ≤0.8, PS-02 승률 ≥0.4 |
| EN-05 제단 2분 | 지키면서 무엇을 옮길까? | 목표 규칙 / 없음 | 제단(20초 보호), 슬라임 10, 도끼병 4, 오크 1 | 목표 달성률 ≥0.6(TTK 대신) |

---

## 5. 조우(전투 한 판) 설계 문법과 페이싱

- **조우 = 질문 + 가설 + 변형 축 1개.** '더 많은 몹'이 아니라 '다른 질문'. 새 메커닉은 기존 문법으로 못 쓰는 질문이 3개 쌓일 때만.
- **구성 역할 4종(태그로 자동 분류)**: 탄약(Light·Volatile), 모루(벽·Heavy·Anchored), 재료(`on_death`), 위협(피해 예고). 조우마다 각 1종 이상. 방마다 새 개념·새 종 1개.
- **목표 규칙**: 전멸형 외에 보호·생존·지점 처치 변형, 지표는 목표 달성률(EN-05).
- **박자**: 읽기 1.5초 → 조작 10~25초 → 정산 → 숨 5~10초 [가정]. 예산은 형성 2.0 → 절정 3.0 → 감쇠 1.5 → 숨 0(난수 없음). 별도 디렉터는 MD-21 뒤.
- **Heat·보상**: 런 단위 Heat(Hades Pact of Punishment)를 방 입구 단위로 옮긴 변형 [제안]. 효과는 관계 해금·빈도·예고 단축(체력 배수 금지), 슬라이스 제외. 보상은 조우 단위(CG-11).
- **배치(VR-09)**: 남쪽 스폰 금지, 북쪽 띠 모루 1개 이상, 초기 장판 6개 이하.

---

## 6. 몬스터 AI 모델: D1 유지와 두뇌 밖 확장, 결정론·300마리 비용

### 6-1. 판정: D1 유지

유틸리티 AI + 실행 FSM 5상태(Idle/Move/Cast/Sequence/Stagger)를 그대로 쓴다. 근거: (1) 관계가 모두 1단계 반사라 HTN·GOAP(목표 지향 행동 계획)가 얻을 것이 없다. (2) 결정 로그(D34)가 후보별 점수로 '왜 그랬나'를 설명한다. (3) 평면 표 JSON이라 LLM(대규모 언어 모델)이 쓰기 쉽고 종당 컴파일 0회다(D6·D37). 단 공격 판정 DA가 C++이라 형상 팔레트(§7-1)를 한 번 컴파일한 뒤에만 성립한다. (4) SoA·단일 틱(D14·D15)에서 결정론과 300마리 비용이 성립한다. 두뇌 예산(VR-02): 행동 6, 관계 행동 1, 고려사항 4 이하. 쓰지 않는 것: 비헤이비어 트리·StateTree·GOAP·Mass 두뇌(D4), 규칙 DB 두뇌('가장 구체적 규칙 승리'만 차용, Ruskin 2012), 카운터 AI, 신경망 두뇌·런타임 LLM(D36·D37).

### 6-2. 층 구조

```
[표현] 데칼·미리보기·카메라 히트스톱·소리          ← D31 해시 대상 아님
[조정층] 공격·관계 토큰, 포위 슬롯, 박자            ← 06 §5-5 확장
[두뇌층 D1] 유틸리티 → FSM 5상태 / 원시 + Charge(신규)
[세계층] 변위 풀이기 → 충돌 사건 → 반응 표 → 결과 원시(SoA 쓰기, 피해는 TryApplyDamage)
```

모든 상태는 `UTDMonsterThinkSubsystem`에 있고 두 번째 틱은 없다. 반응층은 상태만 쓰고 행동은 고르지 않는다.

### 6-3. 틱 안의 순서(D15 확장, D40 후보)

0. **스텝 밖 입력 큐**: 플레이어 행동이 몬스터 SoA에 쓰는 요청(동사 변위·`Stagger`·문법 상태)과 구르기 해제만 넣고, 다음 스텝 시작에 (스텝, 출처 SimulationId, 순번) 순으로 소비한다. **피해는 큐에 넣지 않는다**: 노티파이 스윕·GAS 콜백 피해는 지금처럼 즉시 `TryApplyDamage`다(D20 그대로, `TDMeleeAttackNotifyTests` 전제 유지). 피해까지 미루면 'D20 개정'이라 MD-01과 묶어 사용자에게 넘긴다.
1. 공간 해시 → 2. 사고(스냅샷만) → 3. 조정자(32스텝마다) → 4. **이동 적분**: 변위 중인 몸은 `FTDDisplaceSolver`가 덮어쓰고 첫 접촉은 경로가 지나는 셀 스윕 질의로 찾는다(플레이어 캡슐 포함, 두뇌용 이웃 8개는 경로 위 몸을 놓친다) → 5. 충돌 수집 → 6. 공격 판정·`ExecuteRules` → 7. **반응 해소**(반응이 만든 충돌·이월 피해는 다음 스텝) → 8. 세계 갱신 → 9. 해시.

### 6-4. 반응 해소 의미론(D42 후보)

- **층별 1행**: 사건마다 층(물리·재질·관계)별로 조건이 가장 많은 행 1개, 같은 층 동률은 검증 오류. '사건당 1행'이면 `Bowling`이 `VolatileFuse`를 가린다.
- **연쇄 상한(CG-06)**: 계보 id·깊이를 스텝 너머로 전달. 깊이 3, 재진입 1초 잠금, 스텝당 64행(그중 `Damage` 8). 기존 `FTDDamageChainBudget`은 Cast마다 새로 생겨(`TDDamageSubsystem.cpp:32`) 스텝 사이 연쇄를 못 막는다.
- **결과 원시 7개(난수 0)**: `Damage`·`Stagger`·`ApplyStatus`·`SpawnField`·`Kill`·`CastAbility`·`SetGrudge`. `who: Player`는 `Damage`·`Stagger`(강인도 적용)·`ApplyStatus`(GE)만.
- **스폰 금지**: 엔티티는 자체 액터 틱이라(`TDDamageEntity.cpp:389`) 스텝 순서 밖이다. 1회성 두꺼비 폭발만 D19 경로.

### 6-5. 상태 정본·토큰·아군 피해

- **상태 정본 경계(D40)**: 문법 상태(`Slimed`)와 변위 상태만 SoA 정수 만료 스텝이 정본이다. 적분기가 배율을 SoA에서 바로 읽어야 하고, 게임 누적기(03 §1.5)는 프레임당 4스텝 상한이라 월드 시간(GE 타이머)과 스텝 수가 어긋날 수 있다. 기존 `UTDStatusDefinition` 상태(빙결·지속 피해)는 GE 정본 유지(`TDCombatComponentStatus.cpp:114-116`, 06 §5-4 규약 (3)), 빙결은 M1-04대로 `SetFrozen` 거울(`:397`). 게임의 플레이어는 슬롯이 없어(02 §3) 문법 상태도 GE 정본, 시뮬 대리는 SoA이며 차이는 D32 B단계(±1스텝)로 본다.
- **공격 토큰**: 06 §5-5의 `HasAttackToken`과 배정 순서(32스텝마다, 거리 오름차순, 동률 SimulationId)를 그대로 쓴다. 가중 비용은 부여 때 그 종의 최대 비용 능력으로 **예약**, 개시 때 실제 비용으로 **정산**해 차액을 돌려준다. 로더가 피해 능력 행동에 `HasAttackToken`을 자동으로 붙이고(`-print-resolved`에 보임) `CastAbility`·`Charge`도 개시 때 재검사해 SC-03이 강제된다. 몬스터를 노리는 원한 보복은 관계 토큰을 쓴다.
- **아군 피해·원한(CG-08)**: 인과 사슬에 플레이어 동사가 있거나(player_caused) 플레이어를 겨냥한 방향 공격(투사체·돌진)일 때만(player_targeted) 허용, 둘의 합이 player_rooted. 포위 떼 베기는 `Enemies`, 상호 원한 금지, 쿨다운 6초. 논리 시야는 LOD와 무관(D23).

### 6-6. 결정론 점검표

- **난수**: 변위·반응·조정자·봇 탐색 모두 0(D29 스트림 불변). **순서**: 충돌 (시각, SimulationId) / 반응 (스텝, 계보, SimulationId) / 입력 큐 (스텝, 출처, 순번) / 이월 피해 (계보, 순번). 미리보기와 실행은 같은 함수.
- **해시(D31 확장)**: 장판, 문법 상태 만료 스텝, 원한, 계보 잠금, 토큰 보유·예약, 이월 피해, 전역 규칙 정의 해시. M1-11 해시 체인 정의 때 예약. 세이브 왕복 해시(M3-15)에도 포함.
- **벽 정본**: 방 로드 때 2D 선분을 정렬 추출해 변위·투사체·시뮬이 공유(M3-16). 현재 투사체는 `ECC_Visibility` 스윕(`TDDamageEntity.cpp:477`).

### 6-7. 300마리 비용

[확인: 03 §6.2 추정치, 실측 전] 기준선 4.0~4.7ms가 목표 4.0ms 경계라, 아래는 **다른 항목에서 깎아야 할 비용**이다 [가정]. (1) 변위 스윕: 밀리는 몸 30 × 통과 셀 2~3 × 셀당 8 ≈ 스텝당 720회. M3-02 전 전수 검사는 약 9천 회라 볼링 폭주 실측은 M3-02 뒤. (2) 반응 `Damage`는 GAS 경로라 1회 약 30µs(03 §6.1 judge 행)로, 64행을 모두 피해로 채우면 스텝당 약 1.9ms(목표의 6배)다. 그래서 **`Damage` 결과는 스텝당 8회(약 0.25ms) 이하, 초과분은 계보 순서대로 다음 스텝 이월**. (3) 장판 원 목록 32개 이하, 스냅샷 입력 약 6개 × 300. 목표는 반응층 추가 **프레임 평균 0.3ms, 스텝 피크 0.5ms 이하**이고 넘치면 표현을 줄인다(D21). 깎을 후보: 공간 해시 갱신은 셀이 바뀔 때만이라 03 추정이 상한(03 §4.4·§6.1), L1 present 2프레임 약 −0.16ms(03 §1.3). M1-13·M3-03 실측에 볼링 폭주 시나리오를 넣는다.

### 6-8. 재미를 만드는 행동 규칙(토큰 없는 다수, [제안])

예산 3.0이면 30마리 중 대부분은 토큰이 없다. 이들이 떼를 '탄약 모양'으로 만드는 것이 AI의 재미 기여다. 06 §5-5의 `when_token_denied: "hold_ring"`을 종별로 넓혀 `HasAttackToken` `invert` 고려사항 1~2개짜리 행동으로 쓴다 [가정: 인자].

| 종 | 토큰 없을 때 | 만드는 기회 |
|---|---|---|
| 슬라임 | 느린 무리 이동(`MoveToward` `speed_scale` 0.6) | 줄 → 밀기 한 번에 볼링 |
| 고블린 | 링에서 돌진 레인 정렬(`MoveToBand` 400~600) | 돌진 경로를 슬라임·웅덩이 위로 유인 |
| 오크 | 북쪽 띠 유지(`MoveToBand` + `InSouthQuarter` invert) | 사선이 늘 화면 위쪽, 갈고리로 사선 채우기 |

**커밋**: `CastAbility`는 개시 때 방향·대상을 고정하고 `Charge`는 경로를 확정한다(예고 = 결과라야 유인이 성립). **분리 반경 상한**: M2-23 분리 조향이 떼를 흩으면 볼링 기회가 준다. Light 지름 × 1.2 이하 [가정].

---

## 7. 데이터 형식 초안(JSON 조각과 C++ 등록표·서브시스템 후보)

02 §5.1 문체(snake_case 키, 시간은 초)를 따른다. **값 표기**: 등록표 이름·닫힌 열거는 PascalCase, 기존 모드 문자열(`kind`·`select`·`when_token_denied`)은 소문자 유지, 새 열거 `who`·`hook`·`layer`·`ai_during_cc`는 PascalCase. 결과 원시와 `on_death`는 02 규칙 4·11처럼 `{ "do", "args" }`로 써서 같은 `FTDActionArgSchema` 검사기를 쓴다. 새 키는 `schema: 2`(D39). 06 §5-4 `cc_response`의 스텝 키는 초로, `knockback`·`immune`은 질량 등급과 VR-06 감쇠로 대체한다(MD-22).

### 7-1. 몬스터 공격 형상 팔레트와 종 JSON

[확인] 공격 판정은 C++ DA다. 07 M1-07은 `DA_TDGoblinSlash`를 `TDDamageExamples::CreateExamples`에 C++로 추가하고 'JSON 공격 파일은 없다'고 적었다(D19). `TDDamageExamples.cpp` 정의 11개에 몬스터용은 없고 `ActivationDelay` > 0은 Mine(0.8)뿐이며(미추적 MegaMagic 파일 제외) 부채꼴 모드도 없다(Area = 원, Shockwave = 고리). 이대로면 슬라이스 1에 새 정의 약 6개와 컴파일이 필요하고 선딜 조정마다 컴파일해 D37이 깨진다.

[제안] **형상 팔레트 + JSON 덮어쓰기(D39)**: (1) M1-07을 확장해 `telegraph_shape` 5종(Contact·Cone·Arrow·Line·Circle)마다 C++ 기본 DA를 한 번에 추가·컴파일한다(`TDDamageExamples.h` 미커밋이라 MD-11 뒤). Cone은 Area에 부채꼴 반각을 더하는 변경, Arrow는 엔티티 없이 선딜·쿨다운·피해 수치만 공급한다(D41). (2) 종 JSON `abilities.<이름>`에 `telegraph_shape`와 덮어쓰기 잎 `activation_delay_seconds`·`radius`·`damage_coef`·`target_policy`를 둔다. `spell`이 없으면 로더가 팔레트 DA를 복제해 덮어쓰며, 값은 정의 해시(D11)에 들어가고 `tune` 잎이 될 수 있다. `target_policy` 기본은 Line·Arrow = `Everyone`, 나머지 `Enemies`(CG-08). (3) 그 뒤 새 종은 C++ 0줄이고 오우거 시험은 '팔레트 재사용만'을 조건으로 판정한다.

`Slime.json` 예: `"cc_response": { "stagger_seconds": 0.3, "ai_during_cc": "Blocked" }`, `"on_death": [ { "do": "SpawnField", "args": { "field": "SlimePuddle" } } ]`.

`Goblin_Axe.json`(02 §5.2 `Goblin_Melee`와 다른 부분만, 상속된 `Slash`는 `Cleave`로 교체):
```json
{ "schema": 2, "id": "Goblin_Axe", "extends": "Goblin_Melee",
  "body": { "family": "Goblin", "mass": "Medium", "materials": ["Flesh"] },
  "abilities": {
    "Cleave": { "telegraph_shape": "Cone", "range": 170, "cooldown": 1.4, "activation_delay_seconds": 0.45 },
    "Charge": { "telegraph_shape": "Arrow", "range": 500, "cooldown": 5.0, "activation_delay_seconds": 0.6, "damage_coef": 1.2 } },
  "actions": [
    { "id": "Slash", "remove": true },
    { "id": "Cleave", "do": "CastAbility", "args": { "ability": "Cleave" }, "weight": 2.0, "considerations": [
        { "input": "AbilityReady", "args": { "ability": "Cleave" }, "curve": "Binary" },
        { "input": "DistanceToTarget", "curve": "Logistic", "m": -12, "c": 0.14, "range": [0, 1200] } ] },
    { "id": "ChargeLane", "do": "Charge", "args": { "ability": "Charge", "target": "Player" }, "weight": 2.5, "considerations": [
        { "input": "AbilityReady", "args": { "ability": "Charge" }, "curve": "Binary" },
        { "input": "InLogicalView", "curve": "Binary" },
        { "input": "DistanceToTarget", "curve": "Gaussian", "m": 0.1, "c": 0.35, "range": [0, 1200] } ] } ] }
```

원한 보복 `Retaliate`(`Grudge`에게 이동 → `Cleave`, `"relation": true`)도 같은 형식이다. `HasAttackToken`은 로더가 자동으로 붙인다(§6-5). **schema 1 → 2 이행(D39)**: 로더가 schema 1 부모에 기본 `body`(`Medium`, `Flesh`)를 붙이고 VR-01은 schema 2 이상에만 적용해 02 예시 3종이 무효가 되지 않는다. VR-03은 행동이 참조하는 능력에만 적용한다(상속만 된 `Slash` 제외).

### 7-2. 전역 규칙 파일 `Content/MonsterAI/World/CombatGrammar.json`(새 파일 유형 1개, D39, MD-16)

종 사이 규칙이라 태그만 쓴다(CG-03). 수치 하나가 모든 종의 골든 해시를 바꾸므로 편집은 사람 승인 + 전체 회귀로만 하고 `tune` 벡터(D36)에서 뺀다. 아래는 일부다. 나머지 질량 등급, `SlimeSplat`(재질 층: 슬라임이 Heavy·Anchored에 부딪히면 대상 `ApplyStatus Slimed` + 자신 `Kill`), `FriendlyGrudge`(관계 층, `requires.player_rooted`, `SetGrudge` 4초·쿨다운 6초), `Bowling`·`VolatileFuse`도 같은 형식이다.
```json
{ "schema": 1, "id": "CombatGrammar",
  "mass_classes": { "Light": { "shove": 450, "finisher": 250, "hook": "PassBehind", "wall_scale": 0.6 } },
  "player_body": { "impact_mass": "Medium", "bowling_carry": false, "displace_by": "RootMotionSource",
                   "status_authority": "GameplayEffect", "received_collision_scale": 0.3 },
  "statuses": { "Slimed": { "seconds": 4.0, "move_scale": 0.7, "windup_scale": 1.3, "applies_windup_to_player": false } },
  "reactions": [
    { "id": "WallSlam", "layer": "Physical", "on": "WallImpact", "self": { "mass": ["Light", "Medium", "Heavy"] },
      "then": [ { "do": "Damage", "args": { "who": "Self", "power_coef": 0.8, "scale": "WallScaleTimesSpeed" } },
                { "do": "Stagger", "args": { "who": "Self", "seconds": 0.8 } } ] } ],
  "limits": { "lineage_depth_max": 3, "reentry_lock_seconds": 1.0, "reactions_per_step_max": 64, "damage_results_per_step_max": 8 },
  "screen_contract": {
    "logical_view": { "vfov_degrees": 58.7, "min_aspect": 1.7778, "arm_cm": 800, "pitch_degrees": -60, "target_offset_cm": 0 },
    "threat_budget": { "Easy": 2.0, "Normal": 3.0, "Hard": 4.0 } } }
```

### 7-3. 시나리오: intent 블록과 벽 선분(04 §9.1 확장)

모든 좌표는 월드 [X, Y](X = 북·화면 위, Y = 동)로 04 §9.1의 `spawn.at`·`virtual_camera`와 같다. 화면 방향 변환은 ASCII 판 변환 도구만 맡고, 04 §9.1 표에 좌표 순서 한 줄을 넣자고 MD-22에 제안한다.
```json
{ "schema": 2, "id": "EN_03_ToadLane",
  "arena": { "radius": 1400, "obstacles": [ { "a": [900, -900], "b": [900, 900] }, { "a": [900, -900], "b": [-300, -900] } ] },
  "intent": { "question": "두꺼비를 어디로 차 넣을까?", "expected_interactions": ["IX-06", "IX-07"],
    "hypotheses": [ { "metric": "decision_value", "compare": ["PushSpam", "Manipulator"], "pass": { "op": ">=", "value": 1.2 } } ],
    "guardrails": { "unreadable_hit_share": 0.05, "unrooted_infight_share_max": 0.15 } } }
```

### 7-4. C++ 후보(D13: `MonsterAI/`·`CombatSim/`·기존 `Combat/`만)

- `MonsterAI/TDMonsterCrowdControl.h`(02 §2·07 M3-13 산출물 이름): `FTDDisplaceSolver`(요청 + 벽 선분 + 몸 질의 → 궤적·첫 접촉·끝점, 미리보기·실행 공용)가 스텝 적분에 `FTDKnockback::Integrate`(06 §5-4)를 호출한다. 세 이름은 한 파일의 두 층이다.
- `FTDTopDownView`, `FTDArenaSegments`, `FTDBattlefieldState`, `FTDReactionResolver`. D7 등록표 추가: 입력 `HasRelationToken`·`InLogicalView`·`InSouthQuarter`·`HasGrudge`·`StandingInField`·`SelfStatus`, 원시 `Charge`, 선택자 `Player`·`Grudge`.
- 형상 팔레트 DA 5개와 `ETDDamageActionType::Displace`(변위 요청을 입력 큐에)는 데미지 파일이라 MD-11 뒤.
- 표현·검증: `UTDVerbPreviewComponent`, 사인 맞히기 HUD(원인 3지선다를 cause 사슬과 대조), 테스트 `RowConformance`·`PreviewMatchesOutcome`. V0 전용(`MonsterAI/Prototype/`, 폐기 예정): `UTDVerbPrototypeComponent`(임시 능력·입력·카메라 오프셋), `UTDVerbPrototypeApplierComponent`(더미 위치 쓰기).
- [확인] `FTDNeighborEntry` 필드는 미정의(02 §5.5), M1-02에서 `MassClass`·`FamilyId` 등을 포함해 확정한다.

---

## 8. 사람과 AI 에이전트의 공동 제작 방식

### 8-1. 역할 분담

- **사람만**: 의도 5줄, intent 승인, 판정 카드, 주 1회 PIE 20분, 문법 어휘·화면 계약 수치·봇·골든 해시·결정 승인. 재미 항목은 에이전트가 `done`으로 닫지 않는다(MD-19).
- **오케스트레이터**: 질문 3개 이하 인터뷰, intent 작성, 변형 축 선택, 판정 종합, 장부 1회. **하위 에이전트**: td-builder(sonnet)는 구조 변형 JSON만, td-test-runner(haiku)는 배치, td-refuter(opus)는 keep 승격 때만.

### 8-2. 사람 입력 양식(최대 세 가지)

(1) **의도 5줄**(대상 / 느낌 / 동사 최대 2 / 몬스터가 만드는 상황 / 피하고 싶은 것) + 선택 ASCII 판. (2) **intent 승인**(1분). (3) **판정 카드**: `<변형> keep|tweak|cut #안읽힘|불공정|지루|과함|좋음 — 이유 → 다음 축`. 모호어 치환: '재밌게' → 가설 + 지표, '강하게' → 승률·TTK 밴드, '자주' → 쿨다운 초.

### 8-3. 제작 루프(종·조우 1건, 컴파일 0회 — 형상 팔레트 전제, D37 확장)

의도 → intent 승인 → 정적 행렬로 `expected_interactions` 도달 확인(불가하면 시뮬 전에 거부) → 변형 2~3개(수치는 스크립트) → 검증기 + VR(규칙 번호와 고칠 값) → 봇 5종 × 같은 시드 50쌍 → `verdict.json`과 종료 코드 → 모먼트 상위 5·도식 GIF → 사람 판정(**도식은 가독성·공정성 태그만, keep/cut은 PIE에서만**). 새 입력·원시·반응·형상이 필요할 때만 C++ 등록 + 컴파일 1회(사람 승인).

### 8-4. 소유권: 실수 방지(강제 수단 아님)

사람 소유 경로(`CombatGrammar.json`, 봇 정의, 골든 해시, 승인 해시 파일)는 (1) 벤더별 편집 금지 설정(Claude Code `permissions.deny`, 다른 벤더는 확인 필요 [가정], 설정 변경은 사용자 승인)과 (2) `check_ownership.py`의 승인 해시(사람만 갱신) 대조로 지킨다. 이전 초안의 `--approved-by user` 플래그는 에이전트도 쓸 수 있어 강제가 아니었다. 목적은 루프가 봇까지 고쳐 가설을 '통과'시키는 실수를 줄이는 것이다.

### 8-5. 토큰 절약 도구(`Tools/CombatSim/` 명령줄 도구)

검색 먼저(OP-12), 기존 도구 옵션 확장 우선, 조우별 일회성 스크립트 금지. 1인 개발 과설계를 피해 단계별로만 만든다: **V0·V1**은 PIE CSV 텔레메트리(동사·처치 원인·콤보 끊김)만 / **슬라이스 시뮬(M2-25~28)**은 검증기 `-interactions`, `run_batch.py --vary`(D35), `summarize_batch.py --fun`(15줄 + `verdict.json` + 종료 코드, D34) / **게임판(M3-17) 이후** 도식 GIF·`find_moments.py`·비교 페이지·ASCII 판 변환·`calibrate_fun_metrics.py`·`check_ownership.py`. 에이전트는 GIF를 읽지 않는다(시각-언어 모델의 몰입 예측은 기준선 수준, arXiv 2026).

### 8-6. 토큰·지연 예산은 실측한다

[확인] td-builder·td-refuter는 기동마다 `AGENTS.md`(19,608바이트)를 싣는다. 수치 변형은 스크립트, opus는 승격 때만 쓰고 반복마다 토큰·시간을 `loop-cost.jsonl`에 남겨 '1시간·3만 토큰' [가정]을 실측한다.

### 8-7. ID 충돌 회피

접두어는 OP-02 예약어와 저장소 사용을 `rg`로 확인했다 [확인]. 06 제안 M0-09·M2-16~21을 피해 M0-10·11, M2-22~28, M3-16·17을 쓴다.

---

## 9. 재미를 측정·검증하는 방법

### 9-1. 검증 사다리(아래를 통과해야 위로 간다)

자동: 1 검증기 1·2단 + VR → 2 정적 상호작용 행렬(도달 가능 IX, 가려진 행) → 3 **행 적합성**(`RowConformance`: 반응 행마다 몸 2~3개 + 벽 + 동사를 자동 생성해 기대 반응 id·cause 단언, 정적 도달 ≠ 물리적 도달) → 4 해시 게이트(프로세스 안 2회 + 밖 1회 + 골든)·5초 생존(D8 3단 개정안)·정지 `PreviewMatchesOutcome` → 5 봇 배치·실현율·`verdict.json`. 사람: 6 비교 페이지(가독성·공정성 태그만) → 7 PIE(keep/cut, 사인 맞히기, 외부 초견 플레이어). 단계 2와 5는 같은 C++ 매처를 쓴다.

**D8 3단 개정(D43 후보)**: 현행 기준(첫 공격 < 3초, 대기 점유 < 60%, 02 §5.7)은 SC-10·원 예고 1.0~1.2초·가중 예산과 겹쳐 규칙대로 만든 느린 모루형(오우거·두꺼비)과 토큰을 못 받은 종을 떨어뜨린다(조정자가 없으면 `HasAttackToken`이 0이라 `ChargeLane`도 0점). 채택안: (a) 3단은 단독 1~3마리, 조정자 켬·예산 ≥ 마릿수, SC-10 끔. (b) '첫 공격'은 Cast 진입 스텝. (c) 무리 시나리오는 토큰 대기를 대기 점유에서 빼고 `token_wait_share`로 보고. `TDGame.MonsterAI.Validate` 기대값: 슬라이스 1의 5종이 시드 3개 모두 첫 개시 < 3초, 대기 점유 < 60%, 해시 일치.

### 9-2. 페르소나 봇(D12 형식, 사람 승인 뒤 버전 해시로 동결)

| ID | 이름 | 행동 | 용도 |
|---|---|---|---|
| PS-01 | Manipulator | **결정론 미리보기 탐색 봇.** 동사 × 16방향을 `FTDDisplaceSolver`로 풀어 예측 사건에 점수를 매겨 최고를 고름. `reaction_delay_seconds` 0.25 [가정] | 조작 가설 |
| PS-02 | AoeSpam | 마무리기·베기 반복 | 광역 기준선 |
| PS-03 | PushSpam | Q가 준비되면 가장 가까운 적 쪽으로 조준 없이 민다 | `decision_value` 분모 |
| PS-04 / 05 | Kiter / NorthWallHugger | 물러나며 공격 / 북벽에 붙어 싸움 | 도망 퇴화 / 남쪽 치즈 검사 |

PS-01은 전지적 탐색이라 반응 지연 0과 0.25초 결과를 함께 보고하고, V0부터 모은 사람 텔레메트리로 보정한다(M2-08 보강).

### 9-3. 지표(판정 30건 전까지 전부 경고)

| 지표 | 정의 | 초안 문턱 [가정] |
|---|---|---|
| `manipulation_kill_share` | player_caused(인과 사슬에 플레이어 동사)이고 cause ≠ direct인 처치 비율. player_targeted만인 사고는 뺀다 | PS-01 ≥0.35 |
| `strategy_gap` / `decision_value` | TTK p50: PS-01 ÷ PS-02 / PS-03 ÷ PS-01 | 방 4개 중 3개 ≤0.8 / ≥1.2 |
| `single_verb_share` | PS-01의 최다 동사 비중 | ≤0.7 |
| `unreadable_hit_share` | 예고 길이가 그 형상의 SC-01 최소 선딜보다 짧게 적중한 피해 비율 | ≤0.05 |
| `offscreen_damage_share` | 가해자가 논리 시야 밖인 피해 비율 | ≤0.05 |
| `unrooted_infight_share` | player_caused도 player_targeted도 아닌 적끼리 피해 비율 | ≤0.15 |
| `realization_rate` / 목표 달성률 | 도달 가능 IX 중 실제 발생 비율 / EN-05 목표를 지킨 시드 비율 | ≥0.7 / ≥0.6 |
| 두 overflow / 동시 메커니즘 / 성장 안정성 | 상한 초과 스텝 / 화면 안 동시 종류 / 레벨 1·5·10 조작 처치 비율 변화 | 0 / ≤4 / ≤0.1 |
| 콤보 끊김·`preview_drift`·`token_wait_share` / 치즈 | — / PS-05가 PS-01보다 빠르거나 안전하면 실패 | 보고 / — |

로그 필드(처치 `cause`, `lineage`, `reaction_id`, `telegraph_start_step`, 위치 샘플, `ev:"displace"|"collide"|"react"`)는 결정 로그 소관인 M2-03(D34) 명세에 넣는다. 로그는 D31 해시 대상이 아니므로 골든 재생성을 피하려면 해시 대상 상태(§6-6)를 M1-11 해시 체인 정의 때 예약한다.

### 9-4. 사람 플레이테스트와 게이트 승격

주 1회 PIE 20분, 사인 맞히기 정답률 70% 이상. **1인 설계자 편향 대책**: 외부 초견 플레이어 3~5명 최소 1회. 판정은 처음 30건 전수, 이후 5개 중 1개. 게이트 승격은 사람 승인과 명시 기준(30건 이상, 순위 상관)으로만. 증거는 `Docs/MonsterAI_CombatSim/measurements/`(OP-24).

### 9-5. 헌장 CG와 검증기 규칙 VR

**CG**: 01 어휘 상한(질량 4, 충돌 사건 5 = WallImpact·UnitImpact·FieldEnter·AllyHit·Death, 결과 원시 7, 반응 층 3, 바닥 상태 2, 공격 형상 5. 추가는 사람 승인 + D항목) / 02 종 예산 1-1-1+2 / 03 태그로만 참조 / 04 효과 = 이름 = 지면 부호 하나 / 05 상태 자기 확산·변환 금지 / 06 연쇄 상한 / 07 플레이어 입력 반응성 불변 / 08 아군 피해·원한 제한 / 09 두뇌는 반응을 노리지 않음 / 10 새 어휘는 도달 IX와 `strategy_gap`·`decision_value` 개선이 측정될 때만(Into the Breach 'Complex rules, but not deep') / 11 보상은 조우 단위, 성장은 체력 배수 대신 동사 강화 / 12 가독성 실패 시 어휘를 줄인다.

**VR**: 01 `body` 필수(schema 2 이상) / 02 행동 6·관계 행동 1·고려사항 4 이하 / 03 참조 능력의 `ActivationDelay` ≥ 형상별 최소 선딜 / 04 비피해 동사 응답 2 이상 / 05 사거리 500 이상 공격은 `InLogicalView` 필수 / 06 표시 없는 면역 금지 / 07 같은 층 동률·가려진 행·도달 불가·방출 순환·허용 안 된 `who: Player`는 오류 / 08 intent 이름 검사 / 09 남쪽 스폰 금지·북쪽 모루·역할 4종·방마다 새 개념 1 / 10 종 id 참조 금지.

---

## 10. 첫 수직 슬라이스(약 12분 프로토타입) 범위·성공 판정과 07 로드맵 연결

### 10-0. 착수 전제

[확인] 미커밋 변경: 데미지 파일(`TDDamageSubsystem.cpp`, `TDDamageEntity.cpp/h`, `TDDamageDefinition.cpp/h`, `TDDamageExamples.h` 등), `TDGameCharacter.cpp`(Q·E 부여 `:445-446`, 카메라 붐 `:56-62`), `TDGamePlayerController.cpp/h`, `TDCombatCharacter.h`. M0-02·03, 형상 팔레트, V0가 겹치므로 AGENTS.md 9절에 따라 **첫 행동은 사용자에게 정리 요청**(MD-11)이고, 그 전에는 이 파일들을 건드리지 않는 일만 한다. 콤보 3타 밀기는 검 콤보 작업 정리 뒤다.

### 10-1. V0 동사 회색 상자(M0-10 제안, 1~1.5주 상한 [가정])와 V1

| 항목 | 내용 |
|---|---|
| 목적 | 가설 H1을 가장 싸게 반증 |
| 포함 | 회색 상자 맵(상자 벽 임시 선분), Q(탭·길게), E, 구르기, 벽꽝·볼링(깊이 2), 카메라·VFX 히트스톱·흔들림·충돌음, 강인도 플래그(V0 폰만), 카메라 대안, SC-11 |
| 경로 | 미커밋 파일을 건드리지 않는다. `UTDVerbPrototypeComponent`를 시험 맵 전용 폰 블루프린트 하위 클래스에 구성으로 붙이고, 우선순위 높은 입력 매핑 컨텍스트로 Q·E를 가로채 임시 능력(풀이기 호출)을 부여한다[가정: Enhanced Input 우선순위 소비는 첫날 확인]. 카메라 오프셋은 그 블루프린트 기본값이나 콘솔 변수로. 더미 `ATDMonsterCharacter`는 블루프린트 기본값으로 AI 자동 빙의(`TDMonsterCharacter.cpp:25-26`)를 끈다. 막히면 **V0도 MD-11 뒤** |
| 남김 / 버림 | 풀이기·논리 시야·텔레메트리는 남기고 임시 컴포넌트·능력은 폐기 날짜를 대장에 |
| 관문 | 사람 5~10분 × 2회 go/no-go(밀기 재미, 탭·길게, 미리보기 어긋남, 조준 부담, 카메라, 몸 뒤 예고 식별). no-go면 한 축 변형 1회, 그래도 no-go면 §3-0 전환안을 사용자에게 |

**V1(M0-11 제안, 1주 [가정])**: V0 go 직후 같은 임시 적용기로 웅덩이(IX-01)·두꺼비 폭발(IX-06) 반응 2행을 PIE에 더해 '적은 서로의 재료'도 시뮬 투자 전에 사람이 판정한다.

### 10-2. 슬라이스 1 범위(약 12분)

종 5, EN-01~05, 동사 5 + 마무리기, 장판 1, 반응 행 5 + `on_death` 1, IX-01~07, SC-01~11, 형상 팔레트 5, 봇 5. 제외: Heat, 디렉터, 시체, 후속 후보 종, 보스, 합체·빙의.

### 10-3. 순서와 새 할 일 후보 ID(07 반영 전 효력 없음)

| 순서 | ID | 내용(주 [가정]) | 선행 | 관문 |
|---|---|---|---|---|
| 0 | — | 사용자: MD-11 정리, MD-14 승인 | — | — |
| 1 | M0-10 / M0-11 | V0(1~1.5) / V1 반응 2행(1) | MD-14(미커밋 파일을 피하면 MD-11과 병행) | 사람 판정 |
| 2 | M0-01~08 | Phase 0(1, M0-02·03은 정리 뒤) | MD-11 | 기존 |
| 3 | M1-02·11·M2-03 보강, M1-07 확장 | 이웃 필드·해시 대상 예약·입력 큐·로그 필드 / 형상 팔레트 5개(Phase 1, 2~3) | Phase 0 | 골든 재생성 회피, VR-03 |
| 4 | M2-22 | 강제 이동 시뮬판(스윕 질의, 선분, `cc_response`) | M1-05·10 | 정지 미리보기 100% |
| 5 | M2-23·24 | 분리 조향 최소판(반경 상한), 전장 상태·반응층 | M2-22 | `RowConformance` |
| 6 | M2-25·26·27, M2-08 보강 | 검증기 확장(D8 개정), 저작 도구, 조정자 최소판(06 제안 M3-14), 봇 동결 | M2-24, M2-05, M1-05 | overflow 0, 사람 승인 |
| 7 | M2-28 | 슬라이스 1 시뮬판(4~7은 Phase 2, 2~3 + 확장 약 2) | 4~6 | §10-4 기계 |
| 8 | M1-13·M3-03 보강 | 볼링 폭주 실측 | M3-02 | 평균 0.3ms·피크 0.5ms |
| 9 | M3-16, M3-13 축소, M3-17 | 방 벽 선분, 게임 몸 넉백·B단계 정합(D32), 슬라이스 1 게임판(8~9는 Phase 3, 3~4) | M3-01·M2-22 | §10-4 사람 |

달력 [가정]: V0·V1 약 2.5주, Phase 0~3 기존 추정 8~11주 + 확장 약 2주로 M3-17까지 약 10~13주다. 사람이 관계 재미를 처음 만지는 시점이 늦어 V1을 앞에 둔다. 플레이어 쪽 작업은 대장이 없는 전투 기반 영역이라 추적 위치는 사용자가 정한다(MD-13).

### 10-4. 성공 판정

- **기계 필수**: 해시 게이트 3종, `RowConformance`·정지 `PreviewMatchesOutcome` 100%, 두 overflow 0, 볼링 폭주(M3-02 뒤) 반응층 평균 +0.3ms·피크 0.5ms 이하. **기계 경고**: §9-3 문턱(사람 판정과 어긋나면 지표를 먼저 의심).
- **제작 수용**: 형상 팔레트 뒤 오우거를 종 JSON만으로 추가(새 C++ 0줄·컴파일 0회), 반복 3회·사람 개입 10분 이하, 1시간·3만 토큰(실측).
- **사람(결정)**: PIE 5개 방 중 4개 keep, 사인 맞히기·초견 사망 원인 즉답 70% 이상, 콤보가 '안 끊긴다'. 실패 시 한 축 변형 3개로 재측정, 가드레일은 완화하지 않고 가독성 실패면 어휘를 줄인다.

---

## 11. 결정 기록과의 관계

### 11-1. 결정 번호별

| 관계 | 결정 번호와 내용 |
|---|---|
| 유지 | D1~D5(새 FSM 상태 없음, HTN 조건 비해당), D10, D25~D28 |
| 준수 | D13, D19(몬스터 판정은 DA, 예고 = `ActivationDelay`, 돌진만 D41 확장), D20(플레이어 노티파이 피해는 즉시, 큐는 SoA 쓰기 요청만), D21~D23(논리 시야는 개시만 제한·LOD 무관), D29(새 스트림 없음), D36(머신러닝은 플레이어 대리 봇 + `tune` 잎 CMA/PSO(진화 전략·입자 군집 최적화) 블랙박스 튜닝만, 두뇌 신경망 없음. `CombatGrammar` 수치는 튜닝 벡터에서 뺌) |
| 확장 | D6·D11(D39), D7·D9(입력 6·원시 1·선택자 2), D8·D12·D33~D35·D37(D43), D14·D15·D18(D40), D19(D41), D30·D31(D42), D32(넉백·플레이어 변위 B단계 정합) |
| 순서 변경·위험 | D16·D17(분리 조향 Phase 2로, MD-15), D24(반응층 비용은 다른 항목에서 깎음), D37(팔레트 전엔 공격 추가에 컴파일 필요), D38(M0-02·03이 미커밋 파일을 고침, MD-11) |
| 정정 제안(MD-22) | 06 §5-4·§5-5, 04 §9.1: 스텝 키 → 초, `cc_response.knockback`·`immune` 폐지(질량 등급·VR-06 대체), 빙결과 강제 변위, 토큰 개수 → 가중 예산(`HasAttackToken`·정렬 키 유지), 시나리오 좌표 [X, Y] |

### 11-2. 새 결정 후보(모두 상태: proposed, 2026-09-24 claude 제안, 승인 전 효력 없음)

| ID | 내용 | 대안 | 영향 |
|---|---|---|---|
| D39 전투 문법 | `CombatGrammar.json` 1개, schema 2 키·능력 덮어쓰기 잎, 형상 팔레트 DA 5개, schema 1 → 2 이행, 결과 원시 `{ "do", "args" }`, 값 표기, 사람 승인 + 전체 회귀, 튜닝 벡터 제외 | 종 파일 분산 | 02 §5.1·§5.11, M1-07 |
| D40 전장 상태와 틱 | think 서브시스템 소유, 입력 큐(SoA 쓰기 요청만, 피해 즉시라 D20 불변), 반응 해소 단계, 상태 정본 경계(문법·변위 = SoA, 기존 상태·게임 플레이어 = GE) | 전 상태 GE | 02 §3, M1-04 |
| D41 변위·벽·시야 | 풀이기 단일 경로, D19 확장(돌진 적중 = 풀이기 접촉, 피해 = Arrow DA 수치로 `Damage`, 엔티티 없음), 경로 스윕(플레이어 포함), 벽 선분 정본, 논리 시야(세로 FOV + 최소 화면비), `player_body` | 돌진을 DA 엔티티로 신규 구현 | D19, M3-13·16 |
| D42 반응 의미론 | 층별 1행, 계보·재진입·스텝 상한, `Damage` 스텝당 8과 이월, 스폰 금지, 난수 0, 원한 제한, 공격 토큰(`HasAttackToken`, 06 정렬 키, 예약·정산) | 사건당 1행 | 06 §5-5, D30·D31 |
| D43 측정 | 인과 사슬 로그(M2-03), 경고 전용 재미 지표, 봇 동결(반응 지연 병기), `verdict.json`, D8 3단 개정(§9-1) | 사람 판정만 | 02 §5.7, M1-08·M2-03·M2-08 |

---

## 미결 사항(사용자 결정 필요)

모두 07 §4 형식의 `(미정)` 제안(상태: proposed, 2026-09-24). MD-01~10처럼 관리 체계 정리 뒤로 미뤄도 되며 V0를 막는 것은 MD-11·14뿐이다. 권장은 **굵게**.

| ID | 질문 | 선택지 | 영향 / 시점 |
|---|---|---|---|
| MD-11 | 미커밋 변경(§10-0) 정리 시점 | **(a) 지금** (b) V0 뒤 | M0-02·03, 팔레트, V0 / 지금 |
| MD-12 | 동사 배치, 대형 주문 9종 역할, 기세 A/B | **(a) Q 밀쳐내기·E 갈고리·3타 밀기** (b) 다른 배치 | §3-3 / V0 뒤 |
| MD-13 | 강인도·히트스톱·플레이어 작업 대장 | **(a) §3-6(플래그로 플레이어만, 공격자 정지는 카메라·VFX)** (b) 현행 | `TDSkillComponent` / V0 뒤 |
| MD-14 | V0 착수·기간·폐기 기록 | **(a) 1~1.5주 상한** (b) 보류 | M0-10·11 / 지금 |
| MD-15 | 07 선행 변경 | **(a) M3-13 → M2-22, M3-02 일부 → M2-23, 06 제안 M3-14 → M2-27** (b) 유지 | 07 §1 / 슬라이스 시뮬 |
| MD-16 | 전역 규칙 파일·schema 2 키·팔레트 | **(a) `CombatGrammar.json` 1개 + D39** (b) 분산 | 02 §5.1 / Phase 1 |
| MD-17 | 남쪽 시야·FOV·화면비 | **(a) 오프셋 150cm + SC-05, `MaintainYFOV` 명시, 16:9 미만 레터박스** (b) 현행 | §2-4 / V0 뒤 |
| MD-18 | 화면 계약 초기 수치 | **(a) §3-2 초안 + 초견 테스트** (b) 다른 값 | `screen_contract` / 슬라이스 시뮬 |
| MD-19 | 재미 판정 대기를 대장 `decision`으로 겸용 | (a) 겸용(OP-01 변경) (b) 별도. 규칙 변경이라 권장 없음 | 07 상태값 / 슬라이스 시뮬 |
| MD-20 | 의도·판정 카드·`verdicts.jsonl` 위치 | **(a) MD-05와 함께 `measurements/`** (b) 별도 | OP-24 / M2-26 |
| MD-21 | 조우 소유 경계·디렉터 시점 | (a) 월드젠 `UTDEncounterSet` (b) 시나리오 id. 권장 없음 | P2-11 / 디렉터 도입 |
| MD-22 | 06·04 정정(§11-1) | **(a) 일괄** (b) 항목별 | 04·06 / Phase 1 |
| MD-23 | ID 네임스페이스 SC·CG·VR·IX·EN·PS | **(a) 채택** (b) 기존 접두어 아래로 | OP-02 / 07 반영 |
| MD-24 | 외부 초견 플레이어 3~5명 | **(a) M3-17 때 1회** (b) V0 때도 | §9-4 / M3-17 |

미검증 가설: H1 실시간 조준에서도 조작이 처치보다 재밌다(V0가 반증, 반증 시 §3-0) / H2 탭 + 길게 누름이 흐름을 안 끊는다 / H3 예산 3.0·읽기 창 1.5초가 스펙터클을 해치지 않는다 / H4 탐색 봇이 사람의 조작을 근사한다 / H5 기세 / H6 반응층 평균 0.3ms 이하. 보스는 밀기 면역이라 문법이 약하고 게임패드 조준은 미검증.

---

## 근거 색인

조사 노트(이 문서의 근거, `research/`): [web-combat-fun-design.md](research/web-combat-fun-design.md) · [web-topdown-readability.md](research/web-topdown-readability.md) · [web-relational-enemy-ai.md](research/web-relational-enemy-ai.md) · [project-combat-fun-hooks.md](research/project-combat-fun-hooks.md) · [project-human-agent-cowork.md](research/project-human-agent-cowork.md). 설계안 A·B·C와 비평은 중간 산출물이라 저장소에 넣지 않았다.

외부 출처(연도):
- Davis, GDC 2019 — https://media.gdcvault.com/gdc2019/presentations/Into%20the%20Breach%20Postmortem%20Final.pdf
- Booth, L4D AI, 2009 — https://steamcdn-a.akamaihd.net/apps/valve/2009/ai_systems_of_l4d_mike_booth.pdf
- GGG, Archnemesis, 2022 — https://www.pathofexile.com/forum/view-thread/3267228
- maxroll 2023 — https://maxroll.gg/poe/news/pax-west-path-of-exile-2-interview-with-jonathan-rogers
- Hades 위키 Pact of Punishment(게임 2020) — https://hades.fandom.com/wiki/Pact_of_Punishment
- PlayStation Blog 2024 — https://blog.playstation.com/2024/03/01/no-rest-for-the-wicked-revealing-new-details-on-combat-crafting-town-building/
- DOOM, Game Developer 2017 — https://www.gamedeveloper.com/design/-make-me-think-make-me-move-new-i-doom-i-s-deceptively-simple-design ; AI of DOOM 2016, 2018 — https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-
- Magicka, 2011 — https://www.gamedeveloper.com/business/postmortem-arrowhead-game-studios-i-magicka-i-
- Švelch, Game Studies 2020 — https://gamestudies.org/2002/articles/jaroslav_svelch
- Sakurai, Source Gaming 2015 — https://sourcegaming.info/2015/11/11/thoughts-on-hitstop-sakurais-famitsu-column-vol-490-1/
- Eiserloh, GDC 2016 — https://archive.org/details/GDC2016Eiserloh
- Venturelli, Game Developer 2015 — https://www.gamedeveloper.com/design/everything-i-learned-about-dual-stick-shooter-controls
- Proctor & Schneider, 2018 — https://journals.sagepub.com/doi/abs/10.1080/17470218.2017.1322622
- Ruskin, GDC 2012 — https://gdcvault.com/play/1015528/AI-driven-Dynamic-Dialog-through
- Human Benchmark(연도 없음) — https://humanbenchmark.com/tests/reactiontime/statistics
- Wang 외, arXiv 2026 — https://arxiv.org/abs/2603.18480

## 부록: 설계안·비평 결함 반영 위치

A(관계·반응): §1, §4-1, §4-2, §6-5, §7-1, §7-2, §8-3 / B(가독성·동사): §3-7, §6-3, §6-5, §6-7, §10-1 / C(제작·검증): §2-2, §3-2, §4-3, §5, §6-4, §9-2 / 공통: §3-4, §3-6, §6-6, §9-1, §10-0.

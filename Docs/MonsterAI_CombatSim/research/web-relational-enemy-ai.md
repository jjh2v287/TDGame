# 관계형·시스템형 몬스터 AI 아키텍처 웹 조사 (ai-arch-web, 2026-09-23)

용어: IAUS(Infinite Axis Utility System, 무한 축 유틸리티 시스템), FSM(Finite State Machine, 유한 상태 기계), SoA(Structure of Arrays, 배열 구조체), GAS(Gameplay Ability System), ASC(Ability System Component), GE(Gameplay Effect), DDA(Dynamic Difficulty Adjustment, 동적 난이도 조절), LLM(Large Language Model, 대규모 언어 모델).
표기: [검증]=원문 확인, [2차]=2차 자료·요약, [가정]=추론·추정, [미확인]=확인 못함.
평가 축: 결정론(D) / 300마리 비용(C) / JSON 정의(J) / LLM 작성·검증(L) / D1(유틸리티+FSM)과의 관계(R).

## 0. 결론

1. **D1 은 바꿀 필요가 없다.** 조사한 7개 방식은 모두 D1 의 바깥에 붙는 층(세계 규칙·지각·사건 반응·무리 조정·디렉터)이거나 D1 내부의 입력·대상 후보 확장이다. 대체가 필요한 방식은 없었다.
2. **'관계의 동사'의 핵심 장치 = 어포던스 광고(심즈) + 행동×대상 쌍 점수(IAUS).** 동료 몬스터·시체·장판이 "밀 수 있음/먹을 수 있음/빙의 가능"을 광고하고, 두뇌는 광고를 대상 후보로 받아 쌍 점수를 매긴다. 새 관계 = 광고 1행 + 행동 1행이며 C++ 컴파일이 필요 없다(동사 원시가 이미 등록돼 있을 때).
3. **반응(화학)은 AI 와 분리한다.** 야숨처럼 세계 규칙 표가 상태를 바꾸고 AI 는 바뀐 상태를 입력으로 읽기만 한다. 몬스터 몸도 '재질 상태'(젖음·기름·불탐)를 가지면 몬스터 간 관계가 규칙 표에서 저절로 생긴다.
4. **사건 반응은 Valve 규칙 DB(사실·기준·규칙, 가장 구체적 규칙 승리)를 쓰되 '쓰기 전용'으로 제한**한다. 행동 선택권을 주면 두뇌가 둘이 된다.
5. **무리 조정**은 기존 토큰·포위 슬롯(06 §5-5)에 가중치·용량(Amalur)과 '관계 사건 토큰'을 더하고, 바닥 흔적(스티그머지)은 감쇠하는 영향력 지도 층으로 구현한다.
6. **디렉터**는 L4D 식 강도 박자 + 관계 사건 예산만 채택한다. 플레이어 전략 카운터와 몬스터에게 감지 불가 정보를 주는 방식은 배제한다(Alien: Isolation 반발).

## 1. 방식별 근거와 평가

### 1-1 IAUS 확장: 쌍 점수·영향력 지도
- 대상이 있는 고려사항은 가능한 대상 집합마다 평가한다[2차: Tony Nguyen 포트폴리오, 연도 미표기]. IAUS 는 입출력만 연결하면 프로그래밍 지원이 거의 필요 없는 데이터 주도 구조[2차: gameai.com IAUS, 2012~2015].
- 영향력 지도(Dave Mark, Game AI Pro 2 30장, 2015)[검증: 원문 텍스트 추출]:
  - 진영마다 기본 지도 2종: 근접(어디에 곧 도달 가능)·위협(어디를 위협 가능, 원거리는 고리 모양).
  - 매 갱신 0 초기화 후 재계산, 원문 사례는 1초 주기. 거리·곡선 계산 대신 사전 계산한 **템플릿을 도장 찍듯 더함**. 예: 셀 1m·최고 속도 10m/s·갱신 1초 → 21×21 템플릿.
  - 에이전트 주변만 복사한 **작업 지도**에서 더하기·곱하기·반전(1-값)·정규화로 질의. 적 위협×아군 위협 = 전선, 아군 근접(자기 제외)을 빼면 간격 유지, 반전 적 근접 = 도주 지점.
  - 길드워 2 HoT 는 유틸리티 + 영향력 지도 엔진으로 대규모 운영(GDC 2015, 기존 조사 web-ai-architecture-comparison).
- 평가: D 높음(격자 덧셈. 도장 순서를 SimulationId 로 고정해야 부동소수 합이 같다). C 낮음(4절). J 높음(질의 = [지도, 연산, 가중치] 목록). L 높음(명명된 짧은 레시피). R = 확장(입력 함수 '지도 샘플', Move 목적지 선택기).
- 판정: **채택.** 쌍 점수의 대상 후보는 공간 해시(250cm) 이웃·광고 범주별 K개로 제한한다.

### 1-2 스마트 오브젝트·어포던스
- 심즈: 오브젝트가 효용을 광고, 점수 = 광고값 × 현재 동기, 상위 몇 개 중 무작위[2차: GMTK 2023, 기존 조사].
- F.E.A.R.: FSM 3상태(이동·애니메이션·스마트 오브젝트 사용) + 계획기, 스마트 오브젝트 = 환경 주석[2차: Orkin GDC 2006 요약].
- UE Smart Objects: 전역 DB + 공간 분할, 슬롯 claim, 사용자·활동 태그 필터[검증: UE 5.8 공식 문서 개요].
- 평가: 개념이 D1 과 정확히 맞물린다(광고 = 대상 후보 생성기, 유틸리티 = 쌍 선택). 엔진 서브시스템은 액터·Mass 연동이고 고정 스텝 시뮬·헤드리스 안에서의 결정론 보장이 [미확인] → 자체 SoA 광고표로 재현하고 claim 의미만 차용. J·L 매우 높음.
- 판정: **채택(자체 구현).** 광고 행 = {광고자, 동사 태그, 허용 종 태그, 슬롯 수, 수명, 조건}.

### 1-3 Valve 규칙 DB (Ruskin, GDC 2012)[검증: 발표 원문 텍스트]
- 사실 = 세계 상태 키-값. 질의 = 사건·화자·세계·기억 사실을 합친 연관 배열(수백 항목). 기준 = 사실 하나의 참/거짓 검사. 규칙 = 기준 묶음(전부 참이면 일치). 응답 = 실행물.
- **점수 = 규칙의 기준 개수 → 가장 구체적 규칙 승리, 동점은 무작위.**
- 개념(concept)·화자(who)로 해시 분할 → 버킷 약 50규칙, 1만 규칙을 마이크로초 단위로 질의.
- 응답이 사실을 되쓰기(만료 시간 포함)하고 후속 질의(then)를 연쇄 → 사실상 튜링 완전, 원문도 게임 로직·AI 에 쓴다고 언급.
- 작성: 텍스트 문법, 엑셀(Dota), FoxPro(L4D). 시각 도구는 작가들이 "너무 제한적"이라 거부.
- 평가: D 높음(규칙 ID 순 정렬로 동점 해소, 무작위는 이름 있는 스트림). C 낮음(틱마다가 아니라 사건 때만 질의). J·L 매우 높음(행마다 독립, 우선순위 숫자 없이 "더 구체적이면 이김" → 일반 규칙을 건드리지 않고 특수 사례만 추가). 위험: 연쇄 폭주, 가려진 규칙, 두뇌와의 경쟁.
- 판정: **조건부 채택 — '사건 반응 층'.** 응답은 사실 쓰기(만료)·개체 유대 생성·광고 생성·연출 신호로 제한하고 행동 결정은 유틸리티가 한다. 검증기: 가려진 규칙, 도달 불가 기준, 연쇄 깊이 상한(예: 2).

### 1-4 반응(화학) 규칙 표 분리
- 야숨 화학 엔진: 요소(불·얼음·바람 등)와 재질, 3규칙 — 요소는 재질 상태를 바꾼다 / 요소끼리 상태를 바꾼다 / **재질끼리는 서로 바꾸지 못한다**[2차: GDC 2017 발표 보도. 원 영상 미열람].
- Divinity OS2: 지표면(불·기름·독·물·전기) 조합 → 폭발·감전[2차: 위키·가이드]. 내부 격자 구현은 [미확인].
- GAS `UGameplayTagReponseTable`(원문 철자): 태그 개수 → 응답 GE, 양·음 태그 합산, `SoftCountCap`, ASC 마다 태그 이벤트 등록[검증: UE 5.8 `GameplayAbilities/Public/GameplayTagResponseTable.h`].
- 평가: 요소×상태 행렬은 J·L 최상(닫힌 행렬이라 빈 칸·순환을 기계 검출). 시뮬 스텝 안 순수 함수면 D 높음. GAS 태그 응답표는 ASC 이벤트·GE 적용 순서가 시뮬 순서와 별개 → 표현(시각 효과·태그 거울)에만[가정]. C 는 활성 셀·엔티티 수 비례, 사건 구동이면 낮음.
- 판정: **채택 — 두뇌 밖 세계 규칙 층.** 발산 방지: 스텝당 연쇄 깊이 상한, A→B→A 순환 검출.

### 1-5 관계 그래프
- 네메시스(섀도우 오브 모르도르·워): 절차 생성 오크의 특성·강약점, 결의형제(함께 싸우면 보너스, 한쪽이 다치면 복수), 경쟁자(서로 공격 우선)[2차: 위키·가이드]. 플레이어가 너무 잘하면 배신 사건 주입, 예상 밖 행동을 버그 대신 대사로 흡수[검증: Game Developer 2017-09-18].
- Rain World: 개체별 RelationshipTracker, 관계 종류 Ignores·Eats·Afraid·Attacks·AgressiveRival·Uncomfortable·Antagonizes·Pack·SocialDependent 등, 강도 0~1, 평판·성격(공격성·용맹·지배 등)이 개체별 조정[2차: 공식 위키]. 제작자 스스로 "자기를 돌보는 적"은 좌절과 진행 불가를 낳을 수 있다고 인정[검증: Game Developer 2017-03-28].
- 몬헌 영역 다툼: 두 몬스터가 만나면 연출 싸움, 양측 피해·아이템 드롭, 약 10분 쿨다운[2차: 팬 위키].
- 드워프 포트리스: 규칙을 다 기억하기 어려워 캐릭터가 예상 밖 행동[2차: 인터뷰 요약].
- 평가: 종×종 관계 행렬(Rain World 식 동사 열거)은 J·L 매우 높음, C = 종 수². 개체 유대(네메시스 식)는 희소 간선(몬스터당 ≤2)이면 C 낮음·D 높음. 몬헌 식 쌍 연출은 FSM Sequence 상태 + 쿨다운으로 바로 표현. 오프스크린 생태계·DF 식 깊은 사회 관계는 가독성·비용 모두 불리.
- 판정: **채택(종 관계 행렬 + 희소 개체 유대 + 쌍 연출).** 배제(생태계 전체 시뮬, 깊은 사회 관계).

### 1-6 무리 조정
- Doom 2016: 공격 종류별 토큰 수 제한(난이도별), 요청 후 공격·해제, 거리·가시성상 더 적합한 악마가 토큰을 빼앗음, 글로리 킬 중 새 공격 시작 금지[2차: Game Developer 2018-08-06, T. Thompson].
- Kingdoms of Amalur '쿵푸 원': 슬롯 격자, 생물의 격자 가중치 ≤ 플레이어 격자 용량이어야 배정, 공격도 가중치·용량, 상태 추적·대기·공격·후퇴[2차: Game AI Pro 1(2013) 28장 요약].
- 호라이즌 제로 던: 무리가 개체에 역할 배정, 홀로 남은 개체는 무리 가입 요청, HTN(계층적 태스크 네트워크) + 유틸리티[2차: Guerrilla 발표 요약 2017, Game Developer].
- 스티그머지: 환경에 남긴 상태로 간접 협업, 증발(감쇠)이 음의 되먹임[학술: arXiv 2601.08129, 2026]. 출시 게임 사례는 [미확인].
- 평가: 토큰·가중치·용량은 D·C 최상(정수 카운터). 무리 역할 배정은 "역할 대신 주변 상태가 기능을 결정" 원칙과 반대 방향 → 조정자는 역할 지정 없이 토큰·광고 claim 만 관리[가정]. 흔적 층(시체 냄새·공포·피 자국·집결 표식)은 메시지 없이 협업을 만들고 통신 비용 0.
- 판정: **채택(토큰 + 가중치·용량 + 관계 사건 토큰 + 흔적 층).** 보류(역할 배정자).

### 1-7 조우 디렉터
- L4D(Booth 2009)[검증: 발표 원문 텍스트]: 생존자 강도는 피해에 비례·무력화·근처 감염자 사망(거리 반비례)으로 오르고 시간에 따라 감쇠하되 교전 중엔 감쇠 없음. Build Up → Sustain Peak(3~5초) → Peak Fade → Relax(30~45초). 보스는 박자 조절 대상 외. '구조화된 예측 불가성': 무리 90~180초 간격, 생존자 뒤쪽 스폰.
- Alien: Isolation: 디렉터가 위협 게이지로 외계인에게 대략 위치를 알려줌 → "위치를 아는 상대와 숨바꼭질"이라는 불공정 인식. 단계적 행동 해금은 거의 논란 없음[검증: Game Studies 20(2), 2020].
- RE4 숨은 DDA 등 적응 AI 일반론[2차: 위키].
- 평가: 조우당 1개라 C 무시 가능, 상태를 시뮬 상태·해시에 넣으면 D 유지. J 높음(임계값 표). 카운터 AI 는 '조작으로 상황 만들기'의 재미를 벌로 바꾼다.
- 판정: **채택 — 강도 박자 + 관계 사건 예산(화면 안 동시 관계 사건 수) + 조합 선택.** 배제 — 기술 카운터, 정보 부정행위.

## 2. 채택 후보 구성 요소 (층 배치)

| 층 | 구성 요소 | 데이터(제안) | C++ 등록 | 비고 |
|---|---|---|---|---|
| L0 세계 규칙(두뇌 밖, 시뮬 스텝) | 반응 행렬(요소×재질 상태→결과), 장판 격자, 시체·잔해 엔티티, 몬스터 몸 재질 상태 | `Content/MonsterAI/World/Reactions.json` | 결과 원시(점화·확산·폭발·상태 부여) | 연쇄 깊이 상한·순환 검출 |
| L1 지각·지식 | 영향력 지도(진영 근접·위협 + 감쇠 흔적 층), 광고 레지스트리, 관계 저장소(종 행렬 + 개체 유대), 개체 사실 기억(만료) | `World/Relations.json`, 종 파일 메타(광고 목록) | 지도 종류·템플릿, `FTDBrainInputs` 확장 입력 함수 | 사고 전에 스냅샷 |
| L2 사건 반응 규칙 | Valve 식 규칙 DB(개념×종 분할) | 종 파일 5번째 표 또는 `World/Rules.json`(결정 필요) | 기준 연산자·응답 원시 등록표 | 응답은 쓰기 전용 |
| L3 개체 두뇌(D1) | 행동×대상 쌍 점수, 위치 선택 레시피 | 기존 actions/considerations + 대상 열 | 대상 생성기(광고 범주·K 최근접) | FSM 5상태 그대로 |
| L4 무리 조정 | 종류별 토큰 + 가중치·용량, 관계 사건 토큰, 광고 claim | 종 파일 메타(가중치), 조우 설정 | `UTDMonsterThinkSubsystem` | 06 §5-5 확장 |
| L5 조우 디렉터 | 강도 박자, 관계 사건 예산, 조합 | 조우 JSON | 조우당 1인스턴스 | 정보 부정행위 금지 |

D-기록 영향: 종 파일 '평면 표 4개'에 대상 열·광고 목록이 추가되고 세계 단위 파일 2~3개가 생긴다 → 00-decision-record 에 새 결정 항목이 필요하다(D1~D38 을 반박하는 것은 아님).

## 3. 배제·보류와 이유

| 대상 | 판정 | 이유 |
|---|---|---|
| 엔진 Smart Objects 서브시스템 | 개념만 차용 | 액터·Mass 연동, 고정 스텝·헤드리스 결정론 [미확인] |
| GOAP/F.E.A.R. 계획기 | 배제 | D-기록이 배제. 관계 동사는 1단계 행동이라 계획이 불필요 |
| 규칙 DB 를 주 두뇌로 | 배제 | 튜링 완전 연쇄, 두뇌 이중화, 디버그 난도 |
| GAS 태그 응답표로 화학 | 표현 거울만 | ASC 이벤트 순서가 시뮬 순서와 별개 |
| Rain World 식 오프스크린 생태계, DF 식 사회 시뮬 | 배제 | 비용·가독성, 진행 불가 위험(제작자 인정) |
| 무리 역할 배정자(HZD) | 보류 | "주변 상태가 기능 결정" 원칙과 충돌, 광고+토큰으로 대체 가능 |
| 카운터 AI·정보 부정행위 디렉터 | 배제 | 불공정 인식(Alien), 조작 재미 훼손 |
| 장주기 연출 쿨다운(몬헌 약 10분) | 조정 | 액션 전투 템포에 맞춰 짧게, 관계 사건 예산으로 제어[가정] |

## 4. 300마리 비용 추정 [가정, 실측 필요]

- 쌍 점수: 행동 12개(대상 행동 4개) × 후보 K=8 → 사고당 약 40쌍 × 고려사항 4 = 곡선 평가 160회. 300마리 × 5Hz = 초당 24만 회. 곡선 1회 20ns 가정 → 초당 약 5ms(60fps 기준 프레임당 0.1ms 미만). 싼 고려사항 먼저·0 이면 조기 종료(기존 조사의 Lewis 원칙).
- 영향력 지도: 셀 1m, 전장 100×100m = 1만 셀 × 층 7(진영 2 × 근접·위협 + 흔적 3). 300 × 21×21 도장 = 갱신당 13만 덧셈, 4Hz → 초당 53만. 작업 지도는 위치 선택 때만.
- 규칙 DB: 사건당 버킷 약 50규칙 선형 검사(Valve 원문), 초당 사건 수백 → 무시 가능.
- 반응 행렬: 활성 셀·엔티티 수 비례, 사건 구동.
- 결정론 주의: 부동소수 덧셈 순서 고정(SimulationId 순 도장), 동점은 ID, 무작위는 이름 있는 스트림(`rules`, `director`).

## 5. LLM·사람 공동 작성 방식

- 모든 층을 '평면 표 + 명명된 C++ 원시'로 통일: 광고 행, 관계 행렬 칸, 규칙 행, 반응 칸, 지도 레시피([지도, 연산, 가중치]).
- 검증기 추가 항목: 반응 행렬 빈 칸·순환·연쇄 깊이 / 규칙 가려짐·중복 기준·도달 불가 / 광고 동사에 대응 행동이 없는 고아 / 종 관계 비대칭(의도 표기 요구).
- 시드 배치 30줄 요약에 지표 추가: 관계 사건 발생 수, 최대 연쇄 깊이, 광고 사용률, 토큰 대기 시간.
- 규칙 DB 의 '가장 구체적 규칙 승리'는 LLM 이 우선순위 숫자를 조정하지 않고 특수 사례만 덧붙이게 해 편집 충돌이 적다[가정].

## 6. 미확인·질문

- 야숨 화학 엔진 원 발표 영상과 Divinity 지표면 격자 구현은 직접 확인하지 못했다.
- IAUS 보정 계수(compensation factor) 정확한 식은 GDC 2015 영상에만 있음 [미확인]. 프로젝트는 기하평균을 쓰므로 영향 작음.
- 엔진 Smart Objects 의 헤드리스·결정론 동작 [미확인].
- 결정 필요: 사건 반응 규칙을 종 파일 안에 둘지 세계 파일로 둘지, 몬스터 몸 재질 상태를 GAS 태그와 SoA 중 어디에 정본으로 둘지.

## 7. 출처 (연도)

- Dave Mark, Modular Tactical Influence Maps, Game AI Pro 2 30장 (2015): https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter30_Modular_Tactical_Influence_Maps.pdf
- IAUS 소개 (2012~2015): https://www.gameai.com/iaus.php
- 대상별 고려사항 평가 (연도 미표기): https://www.tonynguyen.games/project/utility-ai/
- Tom Looman, Utility AI in UE (2026): https://tomlooman.com/unreal-engine-utility-ai-part1/
- Elan Ruskin, Rule Databases for Contextual Dialog and Game Logic (GDC 2012): https://archive.org/stream/valve-publications/2012/GDC2012_Ruskin_Elan_DynamicDialog_djvu.txt , https://gdcvault.com/play/1015528/AI-driven-Dynamic-Dialog-through
- Jeff Orkin, Three States and a Plan (GDC 2006): https://www.gamedevs.org/uploads/three-states-plan-ai-of-fear.pdf
- UE 5.8 Smart Objects 개요: https://dev.epicgames.com/documentation/unreal-engine/smart-objects-in-unreal-engine---overview
- 야숨 화학 엔진 보도 (GDC 2017): https://zeldauniverse.net/2017/03/09/zelda-team-discusses-breath-of-the-wilds-development-and-2d-zelda-prototype-at-gdc/ , https://www.gamedeveloper.com/design/video-designing-i-zelda-breath-of-the-wild-i-s-unconventional-mechanics
- Divinity OS2 지표면 (위키): https://divinity.fandom.com/wiki/Environmental_Effects_(Original_Sin_2)
- UE 5.8 엔진 소스: `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/GameplayTagResponseTable.h`
- 네메시스 개선 (2017-09-18): https://www.gamedeveloper.com/design/upgrading-the-nemesis-system-for-i-middle-earth-shadow-of-war-i-
- 네메시스 관계 (위키): https://shadowofwar.fandom.com/wiki/Nemesis
- Rain World 생태계 (2017-03-28): https://www.gamedeveloper.com/design/crafting-the-complex-chaotic-ecosystem-of-i-rain-world-i-
- Rain World 관계 종류 (위키): https://rainworld.miraheze.org/wiki/Behavior
- 몬헌 영역 다툼 (위키): https://monsterhunter.fandom.com/wiki/Turf_War
- 드워프 포트리스 인터뷰: https://www.gamedeveloper.com/design/q-a-dissecting-the-development-of-i-dwarf-fortress-i-with-creator-tarn-adams
- Doom 2016 AI (2018-08-06): https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-
- Beyond the Kung-Fu Circle, Game AI Pro 1 28장 (2013): http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter28_Beyond_the_Kung-Fu_Circle_A_Flexible_System_for_Managing_NPC_Attacks.pdf , 요약 https://gamedevelopment.tutsplus.com/tutorials/battle-circle-ai-let-your-player-feel-like-theyre-fighting-lots-of-enemies--gamedev-13535
- Horizon Zero Dawn AI (2017): https://www.guerrilla-games.com/read/the-ai-of-horizon-zero-dawn , https://www.gamedeveloper.com/design/behind-the-ai-of-horizon-zero-dawn-part-1-
- 스티그머지 압력장·감쇠 (2026): https://arxiv.org/pdf/2601.08129
- Mike Booth, The AI Systems of Left 4 Dead (2009): https://steamcdn-a.akamaihd.net/apps/valve/2009/ai_systems_of_l4d_mike_booth.pdf
- Švelch, Should the Monster Play Fair? (Game Studies 20(2), 2020): https://gamestudies.org/2002/articles/jaroslav_svelch
- DDA 일반 (위키): https://en.wikipedia.org/wiki/Dynamic_game_difficulty_balancing

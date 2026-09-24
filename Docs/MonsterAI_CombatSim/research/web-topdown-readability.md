# 탑다운 고정 시점 전투 — 웹 조사 노트 (topdown-web)

작성 2026-09-23 · 조사자 topdown-web · 대상 카메라: 스프링암 800cm, 피치 -60도, 회전 없음(`TDGameCharacter.cpp:56-62`)
표기: **[검증]** 1차 출처 원문 확인 · **[2차]** 2차 기사·위키·커뮤니티 · **[가정]** 계산이나 추론(측정 필요)

---

## 0. 이 카메라의 기하 (직접 계산 [가정])

전제: 코드에 `FieldOfView` 지정이 없으므로 UE(언리얼 엔진) 기본값 수평 90도, 화면비 16:9, 스프링암 피벗 = 캡슐 중심(지면 위 96cm). 블루프린트에서 FOV를 바꿨다면 다시 계산해야 한다.

| 항목 | 값 |
|---|---|
| 카메라 높이 / 피벗 뒤 거리 | 약 789cm / 400cm |
| 세로 FOV | 58.7도 |
| 화면 위쪽(북) 끝 지면 | 캐릭터 앞 약 **932cm**, 폭 약 2,700cm |
| 화면 중앙 지면 | 캐릭터 앞 약 55cm, 폭 약 1,820cm |
| 화면 아래쪽(남) 끝 지면 | 캐릭터 뒤 약 **391cm**, 폭 약 1,375cm |
| 높이 h의 착시 | 화면상 약 0.58h만큼 "더 북쪽 지면"처럼 보임 (100cm→58cm, 150cm→87cm) |
| 카메라 1도 회전 흔들림 | 피벗에서 약 14cm 이동 |

**함의**: 남쪽 시야는 북쪽의 42%다. 남쪽 화면 끝에서 600cm/s로 달려오는 적은 0.65초, 1,500cm/s 투사체는 0.26초 만에 도달한다. 0.26초는 사람의 단순 시각 반응 중앙값(273ms)보다 짧다. **남쪽이 구조적 사각지대**다.

---

## 1. 공격 예고(텔레그래프)

- **반응 시간 기준** [검증] Human Benchmark 누적 8,100만 회 클릭의 중앙값은 273ms, 평균은 284ms다. 격투 게임 분석(Infil, KI 가이드)에서는 60fps 기준 16프레임(약 265ms) 이상의 선딜레이부터 "꾸준히 반응 가능"으로 본다. 시각 신호가 늦게 드러나면 선딜레이가 길어도 반응할 수 없다. 여러 위협이 겹치면 반응 능력이 크게 떨어진다.
- **선택지 수와 반응 시간** [검증] 힉의 법칙(Hick's law): 선택지가 늘면 결정 시간은 로그 비율로 는다(Proctor & Schneider 2018 리뷰). 동시에 예고되는 공격 종류가 많을수록 예고 시간이 더 필요하다.
- **예고의 구성 요소** [검증] Mike Stout(2015)는 공격 직전의 지연이 곧 "지금 간다"는 신호이며, 애니메이션·충전음·입자·음성·진동을 겹쳐 쓴다고 정리했다. 개별 예고를 알아보기 어려운 것이 아니라, 잘 전달된 질문 여러 개가 **겹치는 것**이 난이도를 만든다.
- **일관성** [검증] Kubodera(2022): 같은 빛깔(예: 붉은 발광)은 언제나 같은 결과를 뜻해야 한다. 위험해 보이는 공격은 실제로 아파야 한다.
- **바닥 표시 사례**
  - Diablo III 비전 강화(Arcane Enchanted): 보라색 점이 약 1초 동안 커진 뒤 칼날이 생긴다 [2차, diablowiki].
  - FFXIV(파이널 판타지 14): 판정 시점은 주황 장판이 **사라지는 순간**이다. 고난도에서는 장판이 늦게 뜨므로 모션과 시전명을 읽어야 한다 [2차].
  - Lost Ark: 장판 색이 빨강·파랑·노랑·보라 4종이고 색마다 대응법이 다르다 [2차].
  - Hades II: 대부분 큰 붉은 원을 쓴다. 크로노스 전투에서는 노란 무대 위에 노란 예고를 써서 구분이 어렵다는 불만이 나왔다 [2차, Steam 토론]. **배경 팔레트와 예고 색이 충돌하면 안 된다.**
- **아이콘 예고** Arkham 시리즈는 공격하려는 적의 머리 위에 번개 아이콘을 띄운다. 고난도 모드에서는 이를 끈다 [2차, Arkham Wiki]. God of War(2018)는 막을 수 있는 강공격에 노란 고리, 막을 수 없는 공격에 빨간 고리를 쓴다 [2차].
- **색만으로 전달 금지** [검증] 게임 접근성 지침(Game Accessibility Guidelines)과 Xbox 접근성 지침(XAG)은 필수 정보를 색 하나로만 전달하지 말고 모양·무늬·아이콘을 함께 쓰라고 한다.
- **소리** [2차] Left 4 Dead(2008)는 특수 감염체마다 고유 음형을 쓰고, 현악은 먼 곳·피아노는 가까운 곳을 뜻한다. Vermintide 2는 특수 적이 동시에 나올 때 신호가 누락된다는 불만이 컸고, 확장(Winds of Magic, 2019)에서 보스·기습 무리용 짧은 신호를 추가했다. **소리 신호는 동시 발생 시 합치거나 우선순위를 두어야 한다.**

## 2. 동시 공격자 상한과 공격 토큰

- **DOOM(2016)** [검증, Thompson 2018 / GDC 2018 Loudy·Campbell] 근접·원거리·돌진 등 공격 종류별로 토큰 수가 제한되고, 난이도마다 토큰 수 표가 다르다. 더 적합한 악마가 토큰을 **빼앗을 수 있다**(앞줄이 놀지 않게). 처형(글로리 킬) 중에는 새 공격을 시작하지 못한다. 원거리 악마는 숨지 않고, 엄폐물 근처이면서 플레이어에게 **잘 보이는** 자리를 고른다. 피격 반응은 약한 움찔 → 행동을 끊는 휘청·밀림 → 처형 가능한 기절 순으로 단계가 나뉜다. 정확한 토큰 수는 공개 자료에서 찾지 못했다 [미확인].
- **Arkham 시리즈** [2차, Bloomberg 2021 관찰] 보통 2~3명까지만 동시에 공격한다. Bloomberg 본인의 시제품은 공격마다 "무게값"을 매기고 합계 상한으로 제어해, 8명이 동시 공격하던 것을 최대 3명으로 줄였다.
- **근접 전투 설계 인터뷰 종합** [2차, Vossen 2015] God of War 3 설계자는 평균 2~3초 간격으로 공격이 오도록 조율했고, 화면 밖 공격을 피했다. DmC는 가까운 무리 2~3명만 공격하고 먼 무리는 대기하거나 드물게 원거리 공격만 한다. Aztez는 같은 범주의 공격 둘이 동시에 오지 않게 막는다.
- **ARPG와의 차이** [검증, Blizzard 2020 D4 분기 보고] Diablo IV의 몰려드는 잡몹(swarmer)은 **광역기를 기분 좋게 만드는 역할**로 설계됐다. 무리 전체가 붙는 것은 장르의 즐거움이다. 따라서 [가정] 상한은 머릿수가 아니라 **위협 가중치**로 걸어야 한다. 약한 접촉 공격은 비용이 낮고, 예고가 있는 강공격은 비용이 높다.

## 3. 화면 밀도·실루엣·색 대비

- **Diablo III 아트(GDC 2012)** [검증, Game Developer 기사] 배경·중경(전투)·전경(UI)의 3층 위계를 둔다. 배경은 양식화하고 어둡게 하며, 캐릭터에는 대비가 높은 조명을 준다. 괴물은 기본 실루엣을 먼저 확정한 뒤 색과 장식을 다듬는다.
- **Diablo IV 몬스터 계열** [검증, 2020] 같은 계열 안에서 역할마다 무기와 실루엣·자세를 크게 다르게 한다.
- **League of Legends 명확성(2021)** [검증] 그림자만 보고도 식별되고 바라보는 방향을 한눈에 알아야 한다. 덩치가 역할을 말한다. 시각적 비중은 피해량·군중 제어·회피 가능성에 비례해야 하고, 잡음은 최소로 둔다.
- **투사체** [검증, Slynyrd 2020] 투사체가 화면에서 가장 선명해야 한다. 채도를 높이고 윤곽선을 넣으며(밝은색+어두운색 이중 윤곽이 안전), 쏜 유닛의 색과 맞추고 깜빡임으로 생동감을 준다.
- **Path of Exile 2 패치 0.1.1** [2차] 처치 시 발동 효과 예고를 강화하고 폭발 식물을 더 크고 밝게 바꿨다. **죽음 이후의 위협**도 읽혀야 한다.

## 4. 탑다운의 정보 손실

- **높이** [가정, 0절 계산] 피치 -60도에서는 높이 150cm의 투사체가 지면 위치보다 약 87cm 북쪽에 있는 것처럼 보인다. 판정은 지면 기준으로 하고, 투사체 아래에 그림자나 지면 표지를 둔다. 포물선 투사체에는 착지 원을 쓴다(Unity 토론의 통상 해법 [2차]).
- **가림** [2차] Diablo 계열은 캐릭터 앞 벽을 잘라내거나 투명하게 한다(cutaway). UE에서는 카메라→캐릭터 추적으로 가리는 물체만 페이드하는 방식이 흔하다(Mladucky). Baldur's Gate 3는 투시(x-ray) 방식을 쓴다.
- **화면 밖**
  - [2차, Cooper 2024] 화면 밖 공격에 대한 태도는 셋이다: 화면 안에서만 공격, 화면 안 적 우선, 무관. 화살표는 공격 종류도 거리도 알려주지 못한다.
  - God of War(2018)는 화면 밖 적을 화살표로 표시한다: 흰색은 근처의 적, 빨강은 근접 공격 임박, 보라는 원거리 공격 [2차].
  - God of War 3는 화면 밖 공격 자체를 피했다(Vossen 2015).

## 5. 타격감

- **히트스톱(타격 정지)**
  - [검증, Sakurai 2015 칼럼] 공격자와 피격자가 같이 멈추고, 피해가 클수록 오래 멈춘다. 투사체는 짧게, 전기 속성은 길게 준다. 난전에서는 제3자가 이용하지 못하도록 상한을 둔다. 피격자는 흔들리지만 피격 판정 상자는 제자리에 둔다.
  - [검증, SmashWiki] 대난투 얼티밋의 공식은 대략 ⌊(피해×0.65+6)×배율⌋ 프레임이고 **상한은 30프레임**이다. 멈추는 것은 당사자 둘뿐이고 세계 전체가 아니다. 공격자 쪽 정지가 더 짧다.
  - [2차] 일반적인 범위는 3~12프레임(약 50~200ms)이다.
- **화면 흔들림** [검증, Eiserloh GDC 2016] 충격(trauma) 값을 0~1로 누적하고 선형으로 감쇠시킨다. 흔들림 크기는 trauma²(또는 ³)으로 0.3→3%, 0.6→22%, 0.9→73%다. 난수 대신 펄린 노이즈를 쓴다. 3D에서는 회전 흔들림이 낫다. [검증, XAG 117] 흔들림을 끄는 옵션을 제공해야 한다.
- **넉백과 지형**
  - [2차, Hades 위키] 벽에 부딪히면 운동량·거리·지역에 비례한 벽 충돌 피해를 입는다. 함정은 적에게 더 아프다. 보스는 넉백에 면역이다.
  - [검증, Vlambeer 2013 발표 요약] 적 넉백, 사격 반동, 시체·탄피를 남기는 "영속성"을 강조했다.
  - 탑다운 넉백 거리의 공개 수치는 찾지 못했다 [미확인]. 피격 반응 단계는 DOOM의 3단계(2절)를 참고한다.

## 6. 입력

- **쌍스틱(트윈스틱)** [검증, Venturelli 2015] 조준은 절대 순간 이동하면 안 된다. 60fps에서 프레임당 12도 회전 보간이 좋았다. 자동 조준 허용각은 약 30도이고, 거리에 따라 각도 차가 줄어드는 점을 보정한다. 조준 중 쓰는 명령은 2~4개로 제한하고 트리거·숄더 버튼에 배치한다. 데드존은 축별이 아니라 원형으로 둔다.
- **Hades** [2차] 컨트롤러 자동 조준이 강하다(커뮤니티 추정 70~90도, [미확인]).
- **Diablo III 콘솔판**은 조작 체계를 새로 만들었다 [2차]. **Path of Exile 2**는 시작할 때 WASD 이동과 클릭 이동 중 하나를 고르게 했고(2024), **Diablo IV**는 WASD를 출시 후 추가했다 [2차].
- **마우스 조준의 시차** [가정] 커서를 지면(z=0)에 투영한 뒤 높이 100cm에서 발사하면 약 58cm의 방향 오차가 생긴다. 조준 평면을 발사 높이로 두어야 한다.

## 7. 탑다운의 이점과 몬스터 "관계" 시각화

- **Into the Breach(GDC 2019, Davis)** [검증] 모든 적의 공격을 미리 보여주고 명중·빗나감 확률이 없다. 플레이어 턴 동안 결과가 결정론적이다. 적마다 **목표와 공격 종류를 아이콘으로 표시**하고 공격 종류를 3개로 제한했다. 핵심 교훈: "Killing enemies isn't as fun as manipulating them."
- **관계 표시 사례**
  - WoW(월드 오브 워크래프트): 유닛 사이 연결선(테더), 소용돌이 장판, 원뿔, 수직 광선("여기로") 같은 표준 시각 어휘가 있다 [2차, 2026 가이드].
  - Diablo III 보호막(Shielding): 녹색 구체 5초, 한 번에 한 마리이며 **발동 전 짧은 경고 모션**이 있다 [2차, maxroll].
  - Diablo IV 억제자(Suppressor): 보라 돔이 바깥의 원거리 공격을 막는다. 체력바 아래에 이름과 방벽 아이콘도 표시해 **색·모양·글자 3중 부호화**를 한다 [2차].
  - Diablo II 타락 주술사: 근처에서 죽은 동족을 되살린다. 그래서 "주술사부터 잡는다"는 우선순위가 생긴다 [2차].
  - Diablo IV: 근접병이 원거리병의 방패 역할을 하고, 거인형(bruiser)의 기절+잡몹 포위가 **위치 딜레마**를 만든다 [검증, 2020].
  - Diablo III 체력 연결(Health Link): 약 한 화면 범위 안에서 피해를 나눈다. 연결선 같은 시각 표시는 자료에서 확인하지 못했다 [미확인].
- **지형 활용** Hades의 벽 충돌·함정 피해(5절)와 Into the Breach의 밀기가 탑다운 조망의 이점을 직접 쓴다.

---

## 8. 탑다운 고정 시점 설계 규칙 (16개)

| # | 규칙 | 수치(초안) | 근거 |
|---|---|---|---|
| R1 | 피해를 주는 모든 공격은 예고 창을 가진다. 판정은 **예고가 끝나는 순간**에 한다 | 근접 최소 350ms, 기본 450~600ms, 광역·바닥 800~1,200ms [가정] | HumanBench 273ms, Infil 16f, FFXIV(판정 시점) |
| R2 | 동시에 예고 중인 공격 종류가 늘면 예고를 늘린다 | 종류 2개 이상이면 +150ms [가정] | Hick 법칙, Stout 2015 |
| R3 | 예고 색은 고정된 의미 팔레트를 따르고, 모양·아이콘과 함께 쓴다 | 의미 4종 이하(피해·밀림/당김·제어·관계) | Kubodera 2022, Lost Ark, 접근성 지침 |
| R4 | 예고 색은 지역 배경 팔레트와 겹치지 않게 지역마다 검사한다 | 명도 대비 검사 [가정] | Hades II 크로노스 사례 |
| R5 | 공격 토큰은 머릿수가 아니라 위협 가중치 합으로 제한한다. 잡몹 접촉타는 싸고, 예고 강공격은 비싸다 | 강공격 동시 1~2, 근접 유효 2~3, 원거리 1~2 [가정] | DOOM 2016, Arkham, Vossen, D4 swarmer |
| R6 | 토큰은 빼앗을 수 있고, 처형·연출 중에는 새 공격 시작을 막는다 | 가까운 순위부터 [가정] | DOOM 2016 |
| R7 | 원거리 적은 **화면 안이면서 플레이어에게 보이는** 자리에서만 발사한다. 남쪽 가장자리는 추가로 제한한다 | 남쪽 391cm 이내에서는 발사 금지 또는 예고 +300ms [가정] | 0절 계산, GoW3, DOOM 위치 선정 |
| R8 | 화면 밖 위협은 가장자리 표시 + 종류별 소리로 알리고, 동시 신호는 합친다 | 공격 임박 시에만 빨강 [가정] | GoW 2018, L4D, Vermintide 2 |
| R9 | 판정은 지면 평면에서 하고, 공중 물체에는 그림자·착지 원을 붙인다 | 높이 100cm당 약 58cm 착시 | 0절 계산 |
| R10 | 가림 물체는 카메라→캐릭터 추적으로 페이드하고, 가려진 적은 윤곽선으로 보인다 | 불투명도 20~30% [가정] | Diablo 벽 잘라내기, UE 페이드 사례 |
| R11 | 3층 위계를 둔다: 배경은 채도·명도를 낮추고, 적 투사체가 가장 선명하며, 윤곽선이 있다 | 투사체 이중 윤곽 | D3 GDC 2012, Slynyrd 2020 |
| R12 | 실루엣은 **위에서 봤을 때** 역할과 방향이 읽혀야 한다: 무기 모양·어깨 폭·덩치 | 역할마다 크기 비율 차이 [가정] | LoL 명확성 2021, D4 계열 |
| R13 | 히트스톱은 당사자에게만 적용하고, 피해에 비례하되 상한을 둔다. 광역타는 공격자 쪽을 한 번만 멈춘다 | 약 40~60ms, 강 80~120ms, 마무리 150~200ms, 상한 200ms [가정] | Sakurai 2015, SmashWiki |
| R14 | 화면 흔들림은 trauma² 모델에 펄린 노이즈, 방향성 있는 짧은 흔들림으로 하고 끌 수 있게 한다 | 최대 약 1도(피벗 기준 14cm) [가정] | Eiserloh 2016, XAG 117 |
| R15 | 넉백은 지형과 연결한다: 벽 충돌·함정·다른 적과의 충돌 피해. 보스는 면역이거나 감쇠시킨다 | 약 100~150cm, 강 300~500cm [가정] | Hades 벽 충돌, Into the Breach |
| R16 | 몬스터 관계는 지면 연결선 + 발밑 고리 + 체력바 아이콘의 3중 부호로 보여주고, 관계가 **발동하기 전**에 예고한다 | 관계 종류 4개 이하 [가정] | WoW 테더, D3 Shielding, D4 Suppressor, ItB 아이콘 |

보조 규칙(입력): WASD 이동에 마우스 조준을 기본으로 한다. 조준 평면은 발사 높이로 두고, 근접은 30도 원뿔 자석형 보정, 컨트롤러는 회전 보간 12도/프레임을 쓴다(Venturelli 2015). 클릭 이동은 밀기·당기기 방향을 정밀하게 조작하기에 불리하다 [가정].

**프로젝트 결정과의 연결 [가정]**: FSM(유한 상태 기계)의 Cast 상태 지속 시간 = 예고 창이다. 종 JSON의 행동 표에 `windup_ms`·`telegraph_shape`·`telegraph_class`·`token_cost`를 두면 검증기가 R1·R3·R5를 결정론적으로 검사할 수 있다(최소 예고 미달, 팔레트 밖 색, 토큰 비용 누락). 사람과 AI 에이전트가 같은 규칙표를 공유하게 된다.

---

## 9. 출처 목록

- Tommy Thompson, AI of DOOM 2016, Game Developer, 2018 — https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-
- Loudy & Campbell, GDC 2018 — https://www.gdcvault.com/play/1024940/Embracing-Push-Forward-Combat-in
- Sam Bloomberg, 2021 — https://redxdev.com/2021/03/19/designing-a-combat-system/
- Bart Vossen, Game Developer, 2015 — https://www.gamedeveloper.com/design/enemy-design-and-enemy-ai-for-melee-combat-systems
- Mike Stout, Game Developer, 2015 — https://www.gamedeveloper.com/design/enemy-attacks-and-telegraphing
- Alex Kubodera, Game Developer, 2022 — https://www.gamedeveloper.com/game-platforms/designing-for-difficulty-readability-in-arpgs
- Human Benchmark — https://humanbenchmark.com/tests/reactiontime/statistics
- Infil KI Guide — https://ki.infil.net/reaction.html
- Proctor & Schneider, 2018 — https://journals.sagepub.com/doi/abs/10.1080/17470218.2017.1322622
- Matthew Davis, GDC 2019 — https://media.gdcvault.com/gdc2019/presentations/Into%20the%20Breach%20Postmortem%20Final.pdf
- Diablo III 아트, GDC 2012 — https://www.gamedeveloper.com/design/gdc-2012-diablo-iii-s-art-director-shows-off-design-process
- Blizzard D4 분기 보고, 2020 — https://news.blizzard.com/en-us/article/23308274/diablo-iv-quarterly-updatefebruary-2020
- Riot, 2021 — https://www.leagueoflegends.com/en-us/news/dev/clarity-in-league/
- Slynyrd, 2020 — https://www.slynyrd.com/blog/2020/12/14/pixelblog-31-shmup-sprite-design
- Sakurai, Source Gaming, 2015 — https://sourcegaming.info/2015/11/11/thoughts-on-hitstop-sakurais-famitsu-column-vol-490-1/
- SmashWiki Hitlag — https://www.ssbwiki.com/Hitlag
- Eiserloh, GDC 2016 — https://archive.org/details/GDC2016Eiserloh
- XAG 117 — https://learn.microsoft.com/en-us/gaming/accessibility/xbox-accessibility-guidelines/117
- Game Accessibility Guidelines(색 단독 금지) — https://gameaccessibilityguidelines.com/ensure-no-essential-information-is-conveyed-by-a-fixed-colour-alone/
- Isaiah Cooper, Signals and Light, 2024 — https://signalsandlight.substack.com/p/how-do-enemy-attacks-work-with-the
- Mark Venturelli, Game Developer, 2015 — https://www.gamedeveloper.com/design/everything-i-learned-about-dual-stick-shooter-controls
- Hades Wiki — https://hades.fandom.com/wiki/Gameplay_mechanics
- Hades II Steam 토론 — https://steamcommunity.com/app/1145350/discussions/0/4327475551419791707/
- maxroll D3 — https://maxroll.gg/d3/resources/elite-affixes
- Prima Games D4 — https://primagames.com/tips/diablo-4-suppressor-affix-explained
- D2 타락 주술사 — https://diablo.fandom.com/wiki/Fallen_Shaman_(Diablo_II)
- norumu WoW, 2026 — https://norumu.com/wow-visual-mechanics-guide/
- L4D — https://left4dead.fandom.com/wiki/Audio_Cues
- Vermintide 2 — https://forums.fatsharkgames.com/t/sound-cue-improvements/66497
- GoW 2018 — https://www.digitaltrends.com/gaming/god-of-war-combat-guide/
- Arkham — https://arkhamcity.fandom.com/wiki/Counter
- FFXIV — https://ffxiv.consolegameswiki.com/wiki/Area_of_Effect
- D3 비전 강화 — https://www.diablowiki.net/Arcane_Enchanted
- PoE2 0.1.1 — https://www.rpgstash.com/blog/path-of-exile-2-patch-011-here-are-the-biggest-changes
- PoE2 WASD — https://www.pcgamesn.com/path-of-exile-2/wasd-movement
- Vlambeer 요약 — https://romanluks.eu/blog/how-can-i-implement-game-feel-in-my-game/
- UE 페이드 — https://gregmladucky.com/articles/unreal-engine-4-lets-make-fading-scenery/

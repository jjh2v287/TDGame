# 조사 노트: 탑다운 ARPG 전투 재미 설계 원리 (fun-web)

- 작성: 2026-09-23, fun-web(claude 하위 에이전트).
- 표기: [검증]=1차 자료(슬라이드·개발사 글·인터뷰 원문), [부분]=2차 기사·검색 요약만, [미확인]=근거 없음.
- 약어: ARPG(액션 롤플레잉 게임), AoE(Area of Effect, 광역기), GDC(Game Developers Conference), FSM(유한 상태 기계), LLM(대규모 언어 모델), MF(Magic Find, 아이템 발견 확률 빌드).

## 0. 결론 한 줄

재미는 "많이 죽이기"가 아니라 **읽을 수 있는 위협을 플레이어가 조작해 해결하는 순간**에서 나온다. 대량 러시는 그 순간을 만드는 무대(연료)로만 쓰고, 상황을 만드는 소수 몬스터·관계·지형·페이싱 디렉터를 따로 설계한다.

## 1. 사례별 근거

### 1-1. 대량 러시가 단조로워지는 이유
- **Path of Exile 1 vs 2** [검증, maxroll PAX West 인터뷰 2023-09]: 게임 디렉터 Jonathan Rogers는 PoE1을 한 기술로 화면을 쓸어버리는 **선제형**, PoE2를 보고 반응하는 **반응형**으로 구분. 몬스터 체력·저항·방어·크기를 극단적으로 벌리는 "extremization"으로 여러 기술 사용을 강제. 다만 후반에 신이 된 느낌은 여전히 필요하다고 함.
- **Vampire Survivors** [부분, 위키·분석글]: 동사가 "이동" 하나라 재미가 **위치 선정**에 몰림. 스웜 이벤트는 정해진 초에 발생(예측 가능). 플레이어 힘이 적 밀도보다 빨리 오르는 권력 판타지 → 개별 전투가 아니라 빌드·공간 압박의 재미.
- **Diablo IV 몬스터 패밀리** [검증, Blizzard 2020-02, Candace Thomas]: 역할(브루저·스워머·근접·원거리·보스). 근접이 원거리를 가려 **근접을 재배치해야** 원거리를 잡음. Cannibal은 원거리를 일부러 빼 난전 강제. 실루엣·무기로 구분.
- **Diablo IV Infernal Hordes** [부분, Blizzard 2024]: 웨이브마다 이득+불이익 묶음 3개 중 택1, 누적 → 러시에 **의사결정 지점** 삽입.
- **Last Epoch 타락도** [부분, 위키]: 몬스터 체력·피해와 보상을 함께 올리는 다이얼(난이도 스케일링).
- **No Rest for the Wicked** [부분, PlayStation Blog 2024-03]: 스태미나 비용·패리 신호 학습의 **느린 탑뷰 전투**. 수보다 개별 위협의 무게.

### 1-2. 시스템 상호작용·창발
- **야생의 숨결 화학 엔진** [검증, GDC 2017 Takuhiro Dohta 보도]: ① 원소→물질 상태 변경 ② 원소→원소 상태 변경 ③ 물질끼리는 불변. 단순 요소를 곱하는 "multiplicative gameplay". 물리는 반응성 위해 단순화하되 일관성 유지.
- **DOS2 지표면** [부분, 위키]: 물+전기=감전, 불+기름·독=폭발, 물+냉기=빙판, 불+물=증기. 조합 규칙이 스킬 설명에 적혀 학습 가능. **BG3**는 지표면 비중 축소(Swen Vincke, PC Gamer [부분], 이유 미확인).
- **Magicka** [검증, 포스트모템 2011]: 8원소, 반대 원소 결합 불가, 아군 피해 유지, 전 원소 즉시 개방. 중반 재미 저하 원인을 "혼돈 부족"으로 보고 주문 위력과 적 내구를 **함께** 올림.
- **Noita** [부분, GDC 2019 Petri Purho]: 픽셀 단순 규칙 시뮬. 적 시체가 등불을 깨 기름에 불이 붙는 연쇄. 플레이어도 같은 규칙에 죽음.
- **Into the Breach** [검증, GDC 2019 Matthew Davis 슬라이드]: 모든 적 공격 사전 표시, 명중 확률 없음, 플레이어 턴 동안 완전 결정론. 원문: "Killing enemies isn't as fun as manipulating them." 승리 조건을 전멸에서 **턴 제한 생존·건물 보호**로 바꾸자 비살상 무기가 재밌어짐. 공격 유형 3개(포격·근접·투사체)+직교 방향, 적 스펙 고정. 턴 순서 실험은 "Complex rules, but not deep"으로 폐기. 결정론 설계에 무작위(전력망 방어 확률)를 넣자 불만. 난이도 경계는 절벽("풀 수 없음").
- **Hades** [검증, Kotaku 2020-10 Ed Gorinstein]: 손으로 만든 방 재조합. 타르타로스는 중간 크기 벽 방이라 **벽꽝** 기회가 많고, 아스포델 용암 군도에선 적마다 용암 건너는 고유 이동 → **지형이 적 설계를 결정**. [부분, 위키] 넉백이 벽꽝·함정을 연결, 두 벽 사이 추가 피해(Cornered), 함정은 적에게 훨씬 큰 피해, 보스 넉백 면역. 엘리트는 노란 윤곽 **아머**(깨지기 전 경직 불가)+변형 공격.

### 1-3. 적 역할과 조합
- **Doom·Doom Eternal** [검증, Gamasutra 2017 Stratton·Martin]: "Make me think, make me move", 전투 체스, 전진형 전투(처치=체력·탄약), 외형·행동으로 즉시 판독. 경기장·AI를 조정해 플레이어가 먼저 교전하게 함. [부분] Eternal 약점 파괴로 주 공격 제거. Marauder는 무기 교체 교육 의도였으나 흐름 단절 논란(PC Gamer 2020).
- **Enter the Gungeon** [검증, Gamasutra 2020-05 Dave Crooks]: 원형 충돌체로 조금 겹쳐도 안 맞게(플레이어 유리 편향). 패턴이 구르기 유도용이라는 주장은 [미확인].
- **Monster Hunter 영역 다툼** [부분, 위키·PlayStation Blog 2024-06]: 대형 몬스터 둘이 만나면 **쌍별 연출** 전투, 플레이어가 이용. Wilds는 다른 무리 영역으로 유인 가능. 설계 기준은 "그 환경에 살 법한가".
- **Rain World** [검증, Gamasutra 2017]: 생물이 자기 목표(먹이·귀소)로 움직이고 화면 밖에서도 사냥·다툼. 대가: 초반 무승 상황, 플레이어별 난이도 편차 → 진행 제한 보정.
- **Diablo II Fallen** [부분, 위키, 2000]: 근처 처치 시 1~2초 도주 후 복귀(막히면 계속 공격). 샤먼은 **같은 혈통**만 부활, 부활체는 보상 없음. Diablo IV도 유지 [검증 2020].

### 1-4. Path of Exile Archnemesis 실패
- [검증, GGG 공지 2022-05-15] 문제: 모디파이어 작동을 오해(도넛형 범위 등), 특정 빌드를 강하게 처벌, 보상이 난이도를 정당화하지 못함.
- [부분, Massively OP 2022-08] 방어형(단단해지는) 모디파이어 너프.
- [부분, PCGamesN 2022-11, 3.20 교체] 외워야 하는 테마 이름, 복잡한 효과, 모디파이어별 보상 → 대부분을 무시하고 특정 희귀만 잡는 MF 도태 메타. 교체 원칙: "do one specific thing", 명확한 이름, 보상 분리, 단순형은 자주·반응형은 드물게.

### 1-5. 밀기·당기기·환경 살상
- Into the Breach(밀어서 적끼리·지형에 충돌), Hades(벽꽝·Cornered·함정), Wizard of Legend [부분, TV Tropes·위키: 적을 구덩이로 날리면 즉시 무력화, 함정·폭발통은 양날의 검]. Ravenswatch 넉백 설계 근거는 [미확인].

### 1-6. 위험-보상 자기 난이도
- **Hades Heat** [부분, 위키·Inverse 2020]: 첫 클리어 후 해금, 불이익 항목 합산, 무기별 목표 Heat 달성 시 보스가 희귀 재화 지급. Kasavin: 난이도의 방식·정도를 플레이어에게 맡김.
- **Returnal** [부분, 리뷰 2021]: 무피격 연속 처치로 아드레날린 상승, 피격 시 초기화. 악성 아이템=이득+오작동 위험.
- **PoE 맵 모디파이어** [부분]: 모디파이어 수↑=보상↑. 반사 모디파이어의 빌드 봉쇄 불만, PoE2 부재 주장은 [미확인].

### 1-7. 페이싱 디렉터
- **Left 4 Dead AI 디렉터** [검증, Michael Booth 2009 슬라이드 원문]:
  - 강도: 받은 피해 비례, 무력화·낭떠러지, **근처 적 사망 시 거리 반비례**로 상승. 0으로 감쇠하되 교전 중 감쇠 없음. 4명 중 최대값.
  - 상태: Build Up(최대 위협) → Sustain Peak(정점 후 3~5초) → Peak Fade(자연 휴지까지) → Relax(30~45초 또는 일정 거리까지 최소 위협). 보스는 제외.
  - "Amplitude (difficulty) is not changed, frequency (pacing) is". 강도 추정은 조잡해도 작동.
  - 구조화된 예측불가: 범위 지정 무작위 함수의 중첩(몹 러시 90~180초, 75%는 뒤에서). 정적 배치는 암기 게임.
- **Diablo III 아트** [부분, GDC 2012]: 최우선은 판독성. 아이소메트릭에서 거대 악마 깊이 표현을 대비·조명으로 해결.

## 2. 우리 게임에 쓸 설계 원리 (16개)

| # | 원리 | 대표 사례·출처·연도 | 우리 게임 적용 |
|---|---|---|---|
| P1 | 처치보다 조작: 밀기·당기기·교환으로 적을 벽·함정·다른 적에 부딪혀 죽이는 경로를 1급 피해 경로로 | Into the Breach GDC 2019, Hades 2020, Wizard of Legend | `FTDKnockback::Integrate`가 충돌 사건(벽·적·구덩이)을 돌려주고 피해는 `TryApplyDamage`로. 시뮬 지표 "조작 처치 비율" |
| P2 | 완전 정보 텔레그래프가 위협을 퍼즐로 바꾼다 | Into the Breach 2019, Doom 2017, PoE2 2023 | 모든 공격은 **지면 표시**(탑다운은 바닥이 가장 잘 보임). 결정은 시전 시작에 확정, 결과에 무작위 없음 |
| P3 | 공격 문법을 소수 모양으로 제한 | Into the Breach 3유형+직교, Gungeon 패턴 | 모양 열거형(선·부채꼴·원·고리 등, 개수는 가정)을 C++ 등록표로. JSON은 조합만 |
| P4 | 역할은 극단화된 소수 속성으로 | PoE2 extremization 2023, Diablo IV 패밀리 2020 | 종마다 튀는 속성 1~2개(아주 느림·아주 단단함·원거리 전용). 평균형 스탯 금지 |
| P5 | 조합이 위치 퍼즐을 만든다(근접이 원거리를 가림) | Diablo IV 2020, Doom 2017 | 조우 템플릿=역할 조합. 포위 슬롯·공격 토큰(06 §5-5)과 결합 |
| P6 | 몬스터 간 관계는 **쌍 단위로 명시 저작** | Diablo II Fallen/샤먼 2000, Diablo IV 2020, Monster Hunter 영역 다툼 | (보는 태그, 대상 태그, 동사) 형태. 입력 함수가 스냅샷의 이웃 태그를 읽고 행동 원시가 실행 — 기존 표 4개로 표현 가능(가정) |
| P7 | 공포·사기 같은 약한 관계가 큰 체감을 준다 | Diablo II Fallen 도주 | "근처 처치 시 N초 도주, 막히면 계속 공격" 같은 짧은 반응. 구현 비용 낮음 |
| P8 | 작은 규칙표, 곱셈적 상호작용, 모두에게 같은 규칙 | 야생의 숨결 GDC 2017, Magicka 2011, Noita 2019 | 전장 상태는 3~4종(가정). "원소→상태, 원소→원소, 상태끼리 불변" 규칙을 검증기로 강제. 플레이어·몬스터 동일 적용 |
| P9 | 지형이 적을, 적이 지형 사용을 정한다 | Hades Kotaku 2020, Doom 2017 | 월드 생성(PCG)에 벽·구덩이·함정 밀도를 "조작 기회" 수치로 요구. 종마다 지형 대응 이동 1개 |
| P10 | 페이싱은 진폭이 아니라 빈도로 | L4D 2009 | 디렉터는 몬스터 두뇌 밖의 별도 층. 강도=피해+근처 처치(거리 반비례), 교전 중 감쇠 금지. 4상태 순환, 보스 제외 |
| P11 | 구조화된 예측불가: 무작위는 준비 단계에만 | L4D 2009, Into the Breach 전력망 교훈 2019 | 스폰 간격·방향만 시드 스트림으로. 결정 순간(명중·회피)은 결정론. 결정론 시뮬 규약과 일치 |
| P12 | 러시는 연료, 상황은 소수 몬스터가 만든다 | PoE2 2023, Archnemesis 교체 원칙 2022 | 떼(청소 대상)와 상황 제작자(반응 요구)를 분리. 상황 제작자는 드물게 |
| P13 | 처치가 자원을 주면 전진한다 | Doom 2016 | 관계를 끊는 처치(샤먼·방패병)에 즉시 보상(자원·경직) |
| P14 | 효과 하나=이름 하나 | Archnemesis 교체 2022 | 행동 원시·모디파이어 이름은 효과 서술형(예: 이웃 당기기). 테마 이름은 표시 문자열로만. LLM 저작 오류도 줄어듦 |
| P15 | 위험-보상 다이얼은 플레이어가 고른다 | Hades Heat 2020, Diablo IV Infernal Offers 2024, Returnal 2021 | Heat 항목은 체력 배수보다 **행동 변형**(관계 동사 추가·텔레그래프 단축) 위주, JSON 덧씌우기로 |
| P16 | 공정성 편향: 플레이어 쪽으로 기울인다 | Exit the Gungeon 2020, Hades 함정 | 플레이어 피격 판정은 시각보다 작게, 환경 피해는 적에게 더 크게. 탑다운 깊이 모호성 때문에 판정은 XY 평면 기준 |

## 3. 피해야 할 함정 (8개)

| # | 함정 | 근거 | 대응 |
|---|---|---|---|
| X1 | 불투명한 모디파이어 누적(Archnemesis) | GGG 2022 | 한 몬스터당 관계·모디파이어 상한, 효과 하나=이름 하나 |
| X2 | 보상을 특정 몬스터 변형에 묶어 나머지를 무시하게 만듦 | PCGamesN 2022 | 보상은 조우 단위로, 변형과 분리 |
| X3 | 단단함으로 난이도 올리기(체력 스펀지) | Archnemesis 방어형 너프 2022, L4D 진폭 원칙 | 난이도는 빈도·조합·텔레그래프로 |
| X4 | 특정 빌드 봉쇄·단 하나의 정답 강요 | Archnemesis 2022, PoE 반사[부분], Marauder 논란 2020 | 카운터는 현재 기본 조작 안에서 항상 가능해야 함 |
| X5 | 지표면·장판 수프(겹쳐서 안 읽힘) | DOS2→BG3 축소[부분] | 동시 전장 상태 수 상한, 고정 카메라에서 겹침 금지 규칙 |
| X6 | 결정 순간의 무작위, 시드에 따른 무승 상황 | Into the Breach 2019, Rain World 2017 | 무작위는 준비 단계만. 시뮬로 시드별 무승 비율 측정 |
| X7 | 쉬지 않는 전투, 또는 긴 공백 | L4D 2009 | 디렉터 Relax 단계 |
| X8 | 복잡하지만 깊지 않은 규칙 | Into the Breach 턴 순서 실험 | 새 규칙은 "선택이 늘었나"로 판정, 아니면 삭제 |

## 4. 탑다운 고정 시점(암 800cm·피치 -60도) 특화 함의

- 바닥이 가장 넓게 보인다: 텔레그래프·전장 상태·관계 표시(샤먼→부하 연결선 등)는 모두 지면에 그린다.
- 수직성은 약하다: 높이 차 공격·점프 회피보다 평면 밀기·구덩이·벽이 잘 읽힌다(Hades·Wizard of Legend가 같은 구조).
- 화면 밖 위협: L4D "뒤에서 75%"를 그대로 쓰면 안 보이는 곳에서 맞는다. 피치 -60도라 화면 아래쪽 시야가 짧을 수 있음(가정) → 가장자리 표시·소리로 예고.
- 실루엣과 대비: 크기·색 윤곽(Hades 아머 노란 윤곽)으로 역할을 구분. Diablo III도 깊이 표현을 대비·조명으로 해결.

## 5. 몬스터 AI 모델·공동 개발 함의 (D1~D38과 충돌 없음 확인)

- 관계 동사 = 입력 함수(이웃 태그·거리·시체 수) + 행동 원시. 기존 유틸리티+FSM으로 충분. 행동 트리·GOAP 불필요 — L4D 디렉터도 "조잡한 추정으로 충분"을 보여줌.
- 페이싱 디렉터는 개별 두뇌가 아닌 별도 결정론 층(이름 있는 난수 스트림 사용). 몬스터는 디렉터가 준 "허용 강도"만 입력으로 읽음(설계 제안, 가정).
- 영역 다툼식 연출은 쌍별 시퀀스(`sequences` 표)로 저작 — Monster Hunter도 쌍별 연출이지 완전 시뮬이 아님.
- LLM 저작 적합성: 효과 서술형 이름, 공격 모양 열거형, 관계 상한은 모두 검증기 규칙으로 기계 검사 가능 → 사람은 "재미 판정", 에이전트는 "JSON 작성·시드 배치·지표 요약"으로 분업.
- 전투 시뮬 지표 제안: 조작 처치 비율, 강도 곡선 정점 간격, 시드별 무승 비율, 텔레그래프 최소 반응 시간 위반 수, 동시 전장 상태 수 최대값.

## 6. ChatGPT 대화 주장 대조

- 지지: 러시→광역기 수렴(PoE2), 관계·보호·부활(Diablo II/IV), 밀기 중심(Into the Breach·Hades), Heat(Hades·Diablo IV, 단 X3 주의).
- 부분 지지: "주변 상태가 기능 결정"(Diablo IV 엄호, Hades 지형→적), 과하면 X1·X5. 합체·빙의는 사례 근거 [미확인].
- 보강: 대화에 **페이싱 디렉터**와 **공정성 편향·텔레그래프 문법**이 빠짐.

## 7. 출처 목록

1. Into the Breach Design Postmortem 슬라이드, GDC 2019 — https://media.gdcvault.com/gdc2019/presentations/Into%20the%20Breach%20Postmortem%20Final.pdf
2. The AI Systems of Left 4 Dead, Michael Booth, 2009 — https://steamcdn-a.akamaihd.net/apps/valve/2009/ai_systems_of_l4d_mike_booth.pdf
3. GDC 17 Breath of the Wild, Thumbsticks, 2017 — https://www.thumbsticks.com/gdc-17-breath-of-the-wild-science-lies/
4. BotW 화학 엔진, VentureBeat, 2017 — https://venturebeat.com/games/the-legend-of-zelda-breath-of-the-wild-makes-chemistry-just-as-important-as-physics/
5. Make me think, make me move (Doom), Gamasutra, 2017 — https://www.gamedeveloper.com/design/-make-me-think-make-me-move-new-i-doom-i-s-deceptively-simple-design
6. Doom Eternal, Wikipedia — https://en.wikipedia.org/wiki/Doom_Eternal
7. Marauder, PC Gamer, 2020 — https://www.pcgamer.com/doom-eternals-director-says-the-marauder-is-good-actually/
8. What's Next for Archnemesis Part 3, GGG, 2022-05-15 — https://www.pathofexile.com/forum/view-thread/3267228
9. PoE 3.20 replaces Archnemesis, PCGamesN, 2022-11 — https://www.pcgamesn.com/path-of-exile/3-20-archnemesis
10. Archnemesis 방어형 너프, Massively OP, 2022-08 — https://massivelyop.com/2022/08/25/path-of-exile-outlines-nerfs-to-archnemesis-defensive-modifiers-promises-to-explain-kalandra-balance-changes-soon/
11. PoE2 Jonathan Rogers 인터뷰, maxroll, 2023-09 — https://maxroll.gg/poe/news/pax-west-path-of-exile-2-interview-with-jonathan-rogers
12. Diablo IV Quarterly Update, Blizzard, 2020-02 — https://news.blizzard.com/en-us/diablo4/23308274/diablo-iv-quarterly-update-february-2020
13. Season of the Infernal Hordes, Blizzard, 2024 — https://news.blizzard.com/en-gb/article/24119591/slay-endless-demons-in-season-of-the-infernal-hordes
14. Fallen (Diablo II), PureDiablo 위키 — https://www.purediablo.com/d2wiki/Fallen
15. Hades Level Design, Kotaku, 2020-10 — https://kotaku.com/hades-level-design-is-less-random-than-it-seems-1845254545
16. Hades 위키(넉백·함정·아머·Pact) — https://hades.fandom.com/wiki/Gameplay_mechanics , https://hades.fandom.com/wiki/Armored_enemies , https://hades.fandom.com/wiki/Pact_of_Punishment
17. Hades God Mode 인터뷰, Inverse, 2020 — https://www.inverse.com/gaming/hades-god-mode-interview
18. Magicka Postmortem, Gamasutra, 2011 — https://www.gamedeveloper.com/business/postmortem-arrowhead-game-studios-i-magicka-i-
19. DOS2 Environmental Effects, Fextralife 위키 — https://divinityoriginalsin2.wiki.fextralife.com/Environmental+Effects
20. BG3 vs DOS2, PC Gamer — https://www.pcgamer.com/how-baldurs-gate-3s-compares-to-divinity-original-sin-2/
21. Exploring the Tech and Design of Noita, GDC 2019 — https://www.gdcvault.com/play/1025695/Exploring-the-Tech-and-Design ; 80.lv — https://80.lv/articles/noita-a-game-based-on-falling-sand-simulation
22. Rain World 생태계, Gamasutra, 2017 — https://www.gamedeveloper.com/design/crafting-the-complex-chaotic-ecosystem-of-i-rain-world-i-
23. Turf War, Monster Hunter 위키 — https://monsterhunter.fandom.com/wiki/Turf_War ; Wilds 인터뷰, PlayStation Blog, 2024-06 — https://blog.playstation.com/2024/06/13/monster-hunter-wilds-interview-how-capcom-is-evolving-its-apex-franchise/
24. Exit the Gungeon 제작, Gamasutra, 2020-05 — https://www.gamedeveloper.com/design/building-i-enter-the-gungeon-i-s-dungeon-climbing-spin-off-i-exit-the-gungeon-i-
25. Wizard of Legend, TV Tropes — https://tvtropes.org/pmwiki/pmwiki.php/VideoGame/WizardOfLegend
26. Returnal 리뷰, Tom's Guide, 2021 — https://www.tomsguide.com/reviews/returnal-review
27. Monolith of Fate, Last Epoch 위키 — https://lastepoch.fandom.com/wiki/Monolith_of_Fate
28. Vampire Survivors 위키(적·스웜) — https://vampire.survivors.wiki/w/Enemies ; 분석 https://teemo.dev/game-design/vampire-survivors/
29. No Rest for the Wicked 전투, PlayStation Blog, 2024-03 — https://blog.playstation.com/2024/03/01/no-rest-for-the-wicked-revealing-new-details-on-combat-crafting-town-building/
30. GDC 2012 Diablo III 아트, Gamasutra, 2012 — https://www.gamedeveloper.com/design/gdc-2012-diablo-iii-s-art-director-shows-off-design-process

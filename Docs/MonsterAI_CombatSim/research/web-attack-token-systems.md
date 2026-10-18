> 출처: 2026-10-18 claude M3-14·M3-28 조사(하위 에이전트 1회 조사). 문헌 주장의 [검증] 표기는 원문 독립 대조를 거치지 않았으므로 설계 근거로 인용할 때 원문을 다시 확인한다.

# R1. AAA 근접 게임의 공격 토큰·티켓·공격 슬롯 조사 (읽기 전용 조사 보고)

작성: 2026-10-18, 조사 하위 에이전트(웹 조사 + 저장소 읽기만, 파일 수정 없음)

## 0. 결론 요약

1. **조사한 출시작 가운데 "어그로 수에 비례해 토큰 수를 정한" 사례는 하나도 찾지 못했다.** 확인된 사례는 모두 *난이도별로 고정된 용량*을 쓰고, 그 용량을 공격 종류별 또는 가중 비용으로 나눈다(DOOM 2016: 공격 종류별 토큰 수가 난이도마다 다름 / Kingdoms of Amalur: 격자 용량·공격 용량을 난이도로 키움 / God of War 2018: 동시에 공격 태세인 적 2명, 난이도는 적 쪽 토큰 수로 조절[요약 출처, 미검증]).
2. 비례 규칙의 장점(무리가 클수록 압박이 커짐)은 살리되, **상·하한으로 묶고 가중 비용으로 정산**하는 것이 AAA 관행과 이 프로젝트의 기존 결정(SC-03 가중 위협 예산)에 모두 맞는다. 권장식은 5절: `예산 = clamp(Min_d + (N_교전 − 1) × 1/3, Min_d, Max_d)`(가중 단위), 보통 난이도에서 N=10이면 비용 1.0짜리 공격 3개 = 사용자 예시(10 → 3)와 같은 값이 나온다.
3. 사용자 예시 "10마리 → 3토큰"은 `ceil(10/3)=4`가 아니라 `floor(10/3)=3`이다. 문서화할 때 올림/내림을 확정해야 한다(5-1절).
4. 저장소에는 이미 비례 토큰 풀이 있다: PJGame 이식본 `TDCombatTokenSubsystem`이 `max(floor(N × TokenControlRatio), 1)`로 풀 크기를 정한다(`Source/TDGame/AI/CombatToken/TDCombatTokenSubsystem.cpp:203-221`). 그러나 월드 타이머 기반·게임 인스턴스 전역 풀·비헤이비어 트리 태스크라 결정론 규약(D4/D44, 고정 스텝)에 맞지 않으므로 **식만 옮기고 클래스는 쓰지 않는 것**을 권한다.
5. 현재 런타임 토큰은 "대상별 Cast 상태 몬스터 수 < 3" 하나다(`Source/TDGame/MonsterAI/TDMonsterThinkSubsystem.h:64`, `.cpp:766-791`). 접근 중 예약·가중 비용·난이도·순번 공정성·화면 밖 억제가 없다.

출처 등급 표기
- **[1차-확인]**: 개발자 본인이 쓴 문서(Game AI Pro 장, 강연 슬라이드)를 직접 읽음.
- **[2차-확인]**: 강연을 요약한 기사(AI and Games/Game Developer 등)를 직접 읽음. 기사 문장은 확인했으나 원 강연과 대조하지 않음.
- **[요약-미검증]**: 제3자 자동 요약 페이지 또는 검색 엔진 요약에서만 본 내용.
- **[미검증]**: 출처를 찾지 못함. 추론은 **[추론]**으로 따로 표시.

---

## 1. 게임별 조사 결과

### 1-1. Kingdoms of Amalur: Reckoning — "Belgian AI" (가장 구체적인 1차 출처) [1차-확인]
출처: Michael Dawe, "Beyond the Kung-Fu Circle: A Flexible System for Managing NPC Attacks", *Game AI Pro* 28장, 369~375쪽. http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter28_Beyond_the_Kung-Fu_Circle_A_Flexible_System_for_Managing_NPC_Attacks.pdf

- **쿵후 원(Kung-Fu Circle) 비판**: 한 번에 한 명만 공격하게 하는 방식은 빠른 전투에 "너무 엄격"하고, "두세 명 동시 허용" 같은 단순안은 "적 종류나 공격 강도를 반영하지 못한다"(28.2).
- **두 겹의 용량**: 플레이어 중심의 월드 정렬 격자(틱택토처럼 슬롯 8개)가 *격자 용량(grid capacity)*과 *공격 용량(attack capacity)* 두 변수를 가진다. 모든 생물은 *격자 무게*, 모든 공격은 *공격 무게*를 가지며 각 합이 용량보다 작아야 한다(28.3).
- **실제 수치 예**: 플레이어 격자 용량 12, 병사 무게 4, 트롤 무게 8 → 병사+트롤이 들어오면 0이 되어 두 번째 병사는 격자 밖에서 대기. 공격 용량 10, 트롤 돌진 6 / 곤봉 4, 병사 찌르기 5 / 베기 3 → 트롤이 돌진(6)을 고르면 남은 4로 병사는 찌르기(5) 불가, 베기(3)만 가능(28.3).
- **중앙 집중**: 개별 생물에는 공간 추론을 두지 않고 *스테이지 매니저(stage manager)*가 요청을 다음 갱신에 처리하고 가장 가까운 빈 슬롯을 배정한다(28.3).
- **안쪽·바깥쪽 원**: 바깥 "접근 원"(격자 용량 통과 시 진입), 안쪽 "공격 원"(특정 공격 허가 시 진입, 반경 = 근접 공격 최소~최대 거리). 허가 없는 개체는 접근 원 밖에서 대기 → 플레이어가 "누가 즉시 위협인지" 구분 가능(28.3.1).
- **토큰 없는 개체의 행동**: 허가 없이 바깥 원 안에 있으면 "가능한 빨리" 나간다. 대기 개체는 *비어 있는 슬롯의 위치*를 받아 그 앞에 선다 → 다른 개체를 몰라도 자연스러운 측면 포위가 생긴다(28.4).
- **보유 시간**: 공격을 *개시한 직후 즉시* 슬롯을 반납 → 순번이 돌고 한 개체가 독점하지 못함. 소규모 전투면 반납 즉시 재배정 가능(28.4).
- **난이도**: 개별 무게는 그대로 두고 플레이어의 격자 용량·공격 용량만 키운다(28.5).
- **가로채기(stealing)**: 플레이어가 대기 중인 병사 쪽으로 구르면 허가를 그쪽으로 옮긴다. 다만 실제로는 "허가 받은 개체는 건드리지 않되, 허가 받은 개체가 격자 밖에 있고 다른 개체가 격자 안에 있을 때만" 옮기도록 조정. 공격 중인 개체는 배정을 *잠금(lock)*해 용량 초과를 막는다. 슬롯 재배정은 전체 이동 시간을 줄이는 방향(28.6.1, 의사 코드 포함).
- **같은 공격 몰림 방지**: 용량만으로는 같은 종류 공격이 동시에 몰리는 것을 못 막으므로 *개체별·종(種)별·전역* 쿨다운을 추가, 최고 난이도에서 광역 공격이 겹치지 않게 했다(28.6.2).

### 1-2. DOOM (2016) [2차-확인] / DOOM Eternal [미검증]
- 출처 A: Tommy Thompson(AI and Games), "Cyber Demons: The AI of DOOM (2016)", Game Developer. https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-
  - "근접·원거리·차지 등 *공격 종류마다* 한 시점에 쓸 수 있는 토큰 수가 제한"되고, 악마는 공격 전에 토큰을 요청하고 끝나면 반납.
  - "난이도마다 공격 종류별 토큰 수가 다르다."
  - "악마들은 자기가 더 적합하다고 판단하면 서로 토큰을 *훔칠* 수 있다" — 목적은 플레이어 앞의 악마가 멍하니 서 있지 않게 하는 것.
  - 글로리 킬 중 플레이어는 무적이고 AI는 새 공격 행동을 시작할 수 없다. 설계 의도: 플레이어가 대응 못 할 때 공격하는 것은 불공정.
  - 기사에 **없는 것**: 구체적 토큰 수, 거리 우선순위, 토큰 없을 때 행동.
- 출처 B(강연을 듣고 쓴 개발자 데브로그, 간접): https://serfofcinder.itch.io/archetype-portal-to-hell/devlog/738495/enemy-ai-tech-talk
  - 토큰을 뺏기면 그 적은 공격을 취소. 주된 경우는 *화면 안 적이 화면 밖 적의 토큰을 빼앗는* 것(플레이어가 눈치 못 챔). 난이도가 높을수록 풀이 크다.
  - 이 개발자 본인 구현: 근접 풀·원거리 풀 2개, "적 공격성" 옵션으로 각 1/2/3개, 획득 제한 시간(시간 초과 시 실패)을 둔 데코레이터로 이동→공격→대기 전체를 감싸 토큰을 보유.
- 원 강연: "Bringing Hell to Life: AI and Full Body Animation in DOOM"(GDC 2017, Jake Campbell) https://gdcvault.com/play/1024186/Bringing-Hell-to-Life-AI , "Embracing Push Forward Combat in DOOM"(GDC 2018, Kurt Loudy·Jake Campbell) https://gdcvault.com/play/1024940/Embracing-Push-Forward-Combat-in — 본문·슬라이드는 직접 확인 못함 [미검증].
- **DOOM Eternal의 토큰 수·종별 비용, "슬레이어 게이트" 관련 토큰 규칙**: 출처를 찾지 못함 [미검증]. 슬레이어 게이트는 레벨 설계 요소(닫힌 경기장에 강한 악마 배치)로만 확인됨 https://doomwiki.org/wiki/Slayer_Gate . 과제 문구의 "slayer gate"가 토큰 규칙이라는 근거는 없음.

### 1-3. The Last of Us — 전투 조정자(Combat Coordinator)와 역할 티켓 [1차-확인]
출처: Travis McIntosh, "Human Enemy AI in The Last of Us", *Game AI Pro 2* 34장, 427~428쪽. http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter34_Human_Enemy_AI_in_The_Last_of_Us.pdf
- "한 시점에 플레이어를 쏘는 NPC는 *한 명이면 충분*하고, 나머지는 엄폐·측면 포위 등에 시간을 쓴다."
- 전역 객체 *Combat Coordinator*가 역할(Flanker, Approacher, Investigator, StayUpAndAimer, OpportunisticShooter)을 관리. `RequestRole()` 성공 → `AcknowledgeRole()` → 반납 전까지 다른 NPC는 그 역할 불가(= 역할별 토큰 1개).
- 배정 방식이 역할마다 다름: OpportunisticShooter는 *선착순*(빨리 채우는 것이 목적, 획득 즉시 하던 동작을 애니메이션 중간에라도 끊고 사격), Flanker는 매 프레임 각 NPC의 측면 경로 비용을 평가해 *이상적인 한 명*만 허용하고 나머지 요청은 거절.
- 측면 경로 비용: 최근 사격으로 가중한 NPC 평균 위치로 "전투 벡터"를 만들고 그 중심선에 가까울수록 비용을 높이는 고정 모양 비용 함수.
- The Last of Us Part II 근접(2차-확인, https://www.gamedeveloper.com/design/how-naughty-dog-defined-melee-attacks-and-behaviors-in-i-the-last-of-us-part-ii-i- ): 공격마다 시작·종료 조건(거리, 바라보는 방향, 이동선)이 있고 조건 불충족 시 시작 불가, 텔레그래프를 더 길고 분명하게, 회피 추가. **근접 토큰을 썼다는 문장은 이 기사에 없음** — StraySpark 블로그가 그렇게 주장하지만 [미검증] https://www.strayspark.studio/blog/attack-token-system-ue5-group-combat-that-feels-fair .
- Uncharted 시리즈: 출처 못 찾음 [미검증]. [추론] 같은 스튜디오라 역할 조정자 계열일 가능성.

### 1-4. God of War (2018) [요약-미검증]
출처: GDC 2019 "Evolving Combat in 'God of War' for a New Perspective"(Mihir Sheth) 강연의 제3자 자동 요약 https://videohighlight.com/v/hE5tWF-Ou2k (강연 원본 https://gdcvault.com/play/1026085/Evolving-Combat-in-God-of 는 로그인 필요). 요약 페이지 스스로 부정확할 수 있다고 밝힘.
- "동시에 공격적(aggressive)일 수 있는 적은 *두 명*"(39:02).
- 공격적 적이 피격으로 끊겨도 토큰을 *잠시 유지* → 비공격 적이 그 틈에 끼어들지 못함(38:39). 공중 피격 반응이 이 유지 시간을 늘림(39:25).
- 난이도: 어려울수록 더 많은 적이 동시에 교전, 적 쪽 공격 토큰 수를 바꾸고 플레이어 쪽 토큰 수는 고정(57:33~58:39).
- 공격권 점수: 잠재 공격성, 디자이너 우선순위, *플레이어가 그 적을 조준 중인지*, 크레이토스 대비 행동 순위(08:46).
- 배치: 공격적 적은 크레이토스 앞쪽 *초승달 모양* 위치 제약(카메라에 보이게), 비공격 적은 원형 대형·더 넓은 분리 반경(14:13~14:35). 화면 밖 적은 마지막 사분면을 기억하고 화살표로 표시(빨강 = 공격 임박, 흰색 = 대기)(16:48~20:07).
- 산타모니카 공식 출처에서 토큰 수를 직접 확인하지 못했으므로 전부 [요약-미검증].

### 1-5. Marvel's Spider-Man (Insomniac) [2차-확인]
출처: "Designing AI to do anything a spider can in Marvel's Spider-Man", Game Developer(GDC 2019 Adam Noonchester 강연 요약). https://www.gamedeveloper.com/programming/designing-ai-to-do-anything-a-spider-can-in-i-marvel-s-spider-man-i-
- 무리 안에서 *공격 토큰을 돌려가며* 넘겨 너무 몰리거나 너무 드물게 공격하지 않게 함.
- 토큰 보유자가 스파이더맨에게서 너무 멀어지면 이미 공격 준비된 다른 적에게 넘김(거리 기반 이양).
- 플레이어가 회피하면 짧은 *적 대기 타이머*가 돌아 플레이어가 몇 대 때릴 틈을 줌.
- 플레이테스트에서 "싼 타격(cheap hits)" 불만 → *화면 안 적의 공격을 우선*.
- 화면 밖 원거리 적은 발사까지 더 오래 걸리게 해 스파이더 센스 경고에 시간을 더 줌.
- 위치 최적화에 Hill Climber 알고리즘을 썼다는 목차만 확인(https://podwise.ai/episodes/1458953 ) — 내용 [미검증].

### 1-6. Batman Arkham 시리즈 [미검증]
- 공격 대기열·토큰을 설명하는 Rocksteady 1차 출처를 찾지 못함.
- 관찰형 기사: 적이 "겉보기에 무작위 간격"으로 공격해 동시 공격이 드물다 https://www.gamedeveloper.com/design/artificial-intelligence-in-game-design (작성자 분석, 설계 문서 아님).
- Arkham City에서 다중 반격(최대 3명 동시 반격)이 추가됨 https://www.siliconera.com/the-improvements-to-batman-arkham-citys-combat-system/ [2차, 검색 요약 기준].
- GDC 2026 "Punching Across the Room in Batman: Arkham Shadow"(Camouflaj) https://gdcvault.com/play/1035681/Punching-Across-the-Room-in — 내용 미확인.

### 1-7. Middle-earth: Shadow of Mordor / Sifu / Assassin's Creed [미검증]
- Shadow of Mordor: 네메시스 계층 관련 인터뷰만 있고 공격 조정 설명은 없음 https://godisageek.com/2014/06/middle-earth-shadow-mordor-michael-de-plater .
- Sifu: Sloclap의 공격 조정 설명 출처 없음. 프리뷰는 "항상 둘러싸인다"는 체감만 기술 https://www.theouterhaven.net/2021/12/sifu-hands-on-preview/ .
- Assassin's Creed: Ubisoft 출처 없음. GameDev.net 글은 제3 개발자 자기 구현(동시 공격자 약 2명, 매 틱 허가 요청) https://gamedev.net/forums/topic/701048-tactical-circle-system/ . AC Valhalla 전투 강연(https://www.gdcvault.com/play/1027359/Fighting-with-Data-Learnings-from ) 내용 미확인.

### 1-8. Ghost of Tsushima [2차-확인, 일부]
출처: PlayStation Blog "Honoring the blade: The lethality contract and combat balance in Ghost of Tsushima" https://blog.playstation.com/?p=344024
- "한 적을 경직시키는 데 걸리는 시간이 곧 다른 적이 공격할 수 있는 시간" → 동시 위협의 창을 *경직 시간*으로 조절.
- 무리 공격성(group aggressiveness)과 콤보 길이가 조정 레버.
- 난이도는 적 체력을 올리지 않고 방어·공격 속도·콤보 길이·막기/패리/회피 판정 창을 바꿈.
- 동시 공격 인원 상한·토큰 언급은 없음 [미검증]. "공격이 겹쳐 여러 적이 동시에 공격할 수 있다"는 Sucker Punch 발언은 검색 요약에서만 봄 [요약-미검증].

### 1-9. Halo 3 (Damian Isla) — 목표(Objectives) 시스템 [1차-확인]
출처: Damian Isla, "Building a Better Battle: The Halo 3 AI Objectives System" 슬라이드(GDC 2008, WPI 강의 사본) https://web.cs.wpi.edu/~rich/courses/imgd4000-d10/lectures/halo3.pdf
- 공격 토큰이 아니라 *분대→과제* 배분이지만 디렉터·스쿼드 설계에 직접 관련.
- 과제마다 우선순위·활성 조건·*용량*. "분대를 위에서 부어 지금 가장 중요한 과제로 걸러 내려가게" 하는 플링코 비유.
- 배분 = "과제 용량 제약 준수 + 비용 함수 H(F) 최소화". 탐욕 알고리즘: 용량을 넘지 않는 (분대, 과제) 쌍 중 H 최소를 반복 배정, 용량 차감. H는 이동 거리·협동·트리 균형·플레이어 근접을 가중.
- 필터(특정 종만, 차량 탑승 여부, 저격수)는 H를 무한대로 반환하는 식으로 구현. 활성 래치·소진(사망 수/생존 수) 규칙.
- Halo의 공격 동시성 제한(예: 수류탄 투척 제한)은 출처 미확인 [미검증].

### 1-10. 일반 설계 문헌 [2차-확인]
- "What IS an enemy attack?"(Signals and Light) https://signalsandlight.substack.com/p/what-is-an-enemy-attack
  - 용어: attack ticketing / cookies / slots / costs.
  - 대상마다 *예약 칸*이 있고 공격자는 공격 비용만큼 티켓을 내고 칸을 점유. 예: 칸 5개 → 1티켓 공격 5명 또는 5티켓 공격 1명.
  - 티켓은 보통 *공격을 고를 때*(사거리 진입 전) 낸다.
  - 비용은 위치에 따라 변할 수 있음: 플레이어 *뒤*에서 하면 더 비싸게, 거리에 따라 변동.
  - 특수 적은 *아직 시작하지 않은* 공격의 예약을 빼앗을 수 있음. 일부 특수 공격은 예약 없이 시작.
  - 티켓 없는 적은 주기적으로 재질의하며 대기·재배치.

---

## 2. 메커니즘별 비교 표

| 메커니즘 | 확인된 사례 | 등급 |
|---|---|---|
| 토큰 수: 난이도별 고정 | DOOM(공격 종류별, 난이도마다 다름), Amalur(용량을 난이도로 확대), GoW(공격적 2명, 난이도로 적 쪽 수 조절) | 2차 / 1차 / 요약 |
| 토큰 수: 교전 수에 비례 | 출시작 사례 없음. 저장소의 PJGame 이식본만 비례(`TDCombatTokenSubsystem.cpp:209`) | — |
| 가중 비용 | Amalur(생물 무게 + 공격 무게 두 겹), Signals(공격별 티켓 수, 위치·거리로 변동) | 1차 / 2차 |
| 별도 풀 | DOOM(근접·원거리·차지 등 종류별), TLOU(역할별 1개), Amalur(격자 용량과 공격 용량 분리) | 2차 / 1차 / 1차 |
| 배정 우선순위 | Amalur(가장 가까운 빈 슬롯, 전체 이동 시간 최소화), Spider-Man(화면 안 우선, 너무 멀면 이양), DOOM(더 적합한 쪽이 훔침, 화면 안이 화면 밖에서), GoW(플레이어 조준 여부·디자이너 우선순위·잠재 공격성), TLOU(측면 경로 비용 최소 1명 / 선착순) | 1차·2차·요약 |
| 보유·반납 시점 | Amalur(개시 직후 반납, 공격 중 잠금), GoW(끊겨도 잠시 유지), Signals(완료·중단 시 회수), 데브로그(획득 제한 시간) | 1차 / 요약 / 2차 / 2차 |
| 가로채기 | DOOM(시작된 공격도 취소시키며), Amalur(허가자가 격자 밖 + 대기자가 격자 안일 때만, 공격 중은 잠금), Signals(시작 전만) | 2차 / 1차 / 2차 |
| 화면 밖 억제 | Spider-Man(화면 안 우선, 화면 밖 원거리 지연), DOOM(화면 밖에서 빼앗음), GoW(초승달 앞쪽 배치, 화살표 표시) | 2차 / 2차 / 요약 |
| 텔레그래프 겹침 방지(위험 공격 1개씩) | 직접 출처 없음. 가장 가까운 것: Amalur의 종별·전역 쿨다운으로 광역 공격 겹침 방지 | 1차(간접) / 나머지 [추론] |
| 플레이어 행동 뒤 숨 고르기 | Spider-Man(회피 후 적 대기 타이머), DOOM(글로리 킬 중 새 공격 금지), Tsushima(경직 시간 = 타 적 공격 창) | 2차 |
| 토큰 없는 적의 행동 | Amalur(바깥 원 밖 대기, 빈 슬롯 앞에 서서 자연 포위, 안쪽 원에서 즉시 이탈), GoW(원형 대형에서 틈 대기), TLOU(엄폐·측면 역할), Signals(대기·재배치·재질의). 도발·페인트는 출처 없음 [추론] | 1차 / 요약 / 1차 / 2차 |

---

## 3. TDGame 현재 상태 (코드 근거)

### 3-1. 런타임(몬스터 사고 서브시스템)
- 상한 상수: `MaxConcurrentAttackersPerTarget = 3` — `Source/TDGame/MonsterAI/TDMonsterThinkSubsystem.h:64`.
- 집계: 스텝마다 `CountAttackersByTarget()`가 대상별로 *Cast 상태인* 몬스터 수만 센다 — `TDMonsterThinkSubsystem.cpp:766-777`, 호출은 `:350`.
- 판정: `HasAttackToken()` = 자기 자신을 뺀 Cast 수 < 3 — `:780-791`.
- 같은 스텝 안 경합: `OrderedSlots` 순서로 사고하며 새로 Cast에 들어간 개체를 즉시 더한다 — `:351-363`. 따라서 같은 스텝에 4명이 동시에 통과하는 경합은 없다(확인). 우선권은 순회 순서(= 선착순)다.
- 입력 연결: 대상이 살아 있으면 `Context.bHasAttackToken = HasAttackToken(SlotIndex)` — `:928`. 기본값은 `true` — `TDMonsterActionExecutor.h:16`. 능력 시전은 쿨다운 또는 토큰 없음이면 실패 — `TDMonsterActionExecutor.cpp:231`.
- 이 구조의 한계(코드에서 바로 보이는 것):
  1. *예약이 없다*: Cast 상태만 세므로 접근 중인 개체는 토큰을 쥐지 않는다 → 3명 이상이 동시에 사거리로 달려와서 2명이 막판에 거절당하는 "달려왔다 멈칫"이 생길 수 있다(Amalur·Signals는 접근 시점에 예약). [추론: 실제 플레이 관찰은 안 함]
  2. *가중치 없음*: 접촉 공격과 광역 부채꼴이 같은 1명으로 계산 — SC-03 미구현.
  3. *비례·난이도 없음*: 고정 3.
  4. *공정성 없음*: 순회 순서가 고정이면 같은 개체가 반복해서 먼저 통과할 수 있다(Amalur는 개시 직후 반납 + 재배정으로 순번을 돌림). 순회 순서가 SimulationId 오름차순이라는 점은 과제가 준 사실이며 이 보고에서 재확인하지 않음.
  5. 화면 안 우선·회피 후 숨 고르기·위험 공격 단독 규칙 없음.

### 3-2. PJGame 이식본 `TDCombatTokenSubsystem` (사용자 제안과 거의 같은 식이 이미 있음)
- 풀 크기: `NewMaxTokens = FMath::Max(FMath::FloorToInt(MonsterCount * TokenControlRatio), 1)`, `MonsterCount = AggroMonsters.Num()` — `Source/TDGame/AI/CombatToken/TDCombatTokenSubsystem.cpp:203-221`.
- 기본값: `TokenControlRatio = 1.0`(0~1 제한), `TokenCooldownDuration = 10.0`초, `TokenInUseTimeout = 0` — `TDCombatTokenSubsystem.h:90-97`. 사용자 규칙은 비율 1/3 + 내림이면 이 식과 같다.
- 쓰면 안 되는 이유: 게임 인스턴스 서브시스템(전역 단일 풀, 대상별 아님) — `.h:54`; 월드 `TimerManager`로 사용 중 시간 초과·쿨다운 — `.cpp:118-120`, `:282-284`(가변 프레임 시간 → 고정 스텝 1/64초 결정론과 불일치); 비헤이비어 트리 태스크 `TDBTTask_CombatTokenRequestAndRelease` — `AI/CombatToken/TDBTTask_CombatTokenRequestAndRelease.cpp:29-60`(D4/D44 금지).

### 3-3. 설계 문서의 기존 결정(재도출하지 않고 위치만 표시)
- SC-03 가중 위협 예산 쉬움 2 / 보통 3 / 어려움 4, 비용 접촉 0.25 / 부채꼴 0.75 / 나머지 1.0 — `Docs/MonsterAI_CombatSim/08-combat-fun-design.md:142`.
- 배정 순서: 32스텝마다, 거리 오름차순, 동률 SimulationId, 부여 때 최대 비용으로 예약·개시 때 실제 비용으로 정산 — 같은 파일 `:285`.
- 조우 박자 예산 형성 2.0 → 절정 3.0 → 감쇠 1.5 → 숨 0, 별도 디렉터는 MD-21 뒤 — 같은 파일 `:247`, MD-21 `:551`.
- 무리 전술 층(그룹 조정자 2Hz, 토큰 K개·역할) — `Docs/MonsterAI_CombatSim/06-beyond-the-ask.md:41`, §5-5 `:269`.

---

## 4. 사용자 규칙 평가: "토큰 = 어그로 수의 1/3"

### 4-1. 올림·내림 문제
- `ceil(N/3)`: N=10 → 4. 사용자 예시(10 → 3)와 다르다.
- `floor(N/3)`: N=10 → 3(예시와 일치), 그러나 N=1·2 → 0이라 하한 1이 필요 → `max(1, floor(N/3))` = 이식본 식에 비율 1/3을 넣은 것.

| N(어그로) | 1 | 2 | 3 | 4 | 6 | 9 | 10 | 15 | 30 |
|---|---|---|---|---|---|---|---|---|---|
| max(1, floor(N/3)) | 1 | 1 | 1 | 1 | 2 | 3 | 3 | 5 | 10 |
| ceil(N/3) | 1 | 1 | 1 | 2 | 2 | 3 | 4 | 5 | 10 |

### 4-2. 장점
- 무리 크기가 곧 압박으로 느껴진다(3마리와 30마리의 체감 차이). 설명이 쉽다.
- "토큰 있으면 공격, 없으면 다른 행동"이라는 질의 구조 자체는 DOOM·Amalur·TLOU와 같다.

### 4-3. 출시작 관행과 비교한 문제
1. **상한이 없다**: 30마리 → 동시 공격 10. 확인된 출시작은 모두 *플레이어가 읽을 수 있는 동시 위협량*에 고정 상한을 둔다(DOOM 종류별 토큰, GoW 2명, TLOU 사격자 1명, Amalur 용량). 탑다운 시점에서 텔레그래프 10개가 겹치면 판독 불가. [추론: 판독 한계 수치는 측정 안 함]
2. **가중치가 없다**: 트롤 돌진과 고블린 할퀴기가 같은 1토큰. Amalur는 이 문제 때문에 "두세 명 허용" 단순안을 버렸다고 명시(1-1절). 프로젝트도 SC-03에서 이미 가중 예산을 택했다.
3. **소규모 전투가 수동적**: N=2~5에서 토큰 1개 → 2~5마리가 한 명씩만 공격(쿵후 원). Amalur가 "빠른 전투에 너무 엄격"하다고 비판한 형태.
4. **N이 출렁인다**: 몬스터가 죽거나 새로 어그로되면 토큰 수가 즉시 바뀌어 공격 리듬이 흔들린다(이식본은 풀이 줄면 사용 중 토큰을 반납시킴 — `TDCombatTokenSubsystem.cpp:224-240` 부근, 세부 동작 미검증). 히스테리시스·회수 금지 규칙이 필요.
5. **"어그로" 정의가 넓다**: 멀리 있거나 화면 밖 개체까지 세면 실제로 공격할 수 없는 개체가 토큰 수만 부풀린다 → "교전 반경 안에 있고 대상이 같은" 개체만 세야 한다.

---

## 5. 권장안 (식·상하한·난이도·배정·보유)

### 5-1. 예산식 (사용자 비율 1/3을 살리고 SC-03 상한으로 묶음)
정수 결정론을 위해 비용을 1/4 단위(SC-03의 0.25가 최소 단위)로 저장한다: 접촉 1, 부채꼴 3, 나머지 4.

```
N_eng  = 같은 대상을 노리고 교전 반경 안에 있는 살아 있는 몬스터 수 (SimulationId 순 집계)
Budget = clamp( Min_d + ((N_eng - 1) * 4) / 3 , Min_d , Max_d )      // 1/4 단위 정수, 정수 나눗셈
Heads  = clamp( 1 + (N_eng - 1) / 3 , 1 , HeadMax_d )                  // 동시 공격 '인원' 상한(접촉 공격 다수 방지)
```

| 난이도 | Min_d(단위) | Max_d(단위) = SC-03 | HeadMax_d |
|---|---|---|---|
| 쉬움 | 4 (1.0) | 8 (2.0) | 2 |
| 보통 | 6 (1.5) | 12 (3.0) | 3 |
| 어려움 | 8 (2.0) | 16 (4.0) | 4 |

보통 난이도 결과(비용 4 = 1.0 공격 기준 동시 수):

| N_eng | 1 | 2 | 3 | 4 | 5 | 6 | 10 | 30 |
|---|---|---|---|---|---|---|---|---|
| Budget(단위) | 6 | 7 | 8 | 10 | 11 | 12 | 12 | 12 |
| 1.0 공격 동시 수 | 1 | 1 | 2 | 2 | 2 | 3 | 3 | 3 |
| Heads | 1 | 1 | 1 | 2 | 2 | 2 | 3 | 3 |

- N=10에서 1.0 공격 3개 = 사용자 예시와 같다. 남는 단위(예: Budget 7 − 4 = 3)는 접촉·부채꼴 같은 싼 공격이 쓴다.
- Heads와 Budget 중 더 엄격한 쪽이 적용된다. N=3에서 Budget은 1.0 공격 2개를 허용하지만 Heads=1이면 1명만 — 수치 조율은 시뮬레이터 측정 대상[추론, 미측정]. 대안: `Heads = clamp(1 + N_eng/3, 1, HeadMax_d)`로 한 칸 앞당기기.
- 난이도는 Amalur·DOOM처럼 *용량만* 바꾸고 능력 비용은 그대로 둔다(1-1, 1-2절).
- 박자 연동: 08:247의 형성 2.0 / 절정 3.0 / 감쇠 1.5 / 숨 0을 Max_d에 곱하는 대신 *Max_d의 상한 교체*로 쓰는 것을 권장(곱셈은 비정수). 디렉터 도입(MD-21)까지는 고정.

### 5-2. 갱신·히스테리시스
- 계산 주기: 기존 결정대로 32스텝(2Hz) 조정자 단계(08:285).
- 감소는 늦게: N_eng이 줄어든 상태가 2주기(1초) 연속일 때만 Budget을 낮춘다. 낮출 때도 *이미 개시한 공격은 회수하지 않고* 새 부여만 막는다(Amalur의 잠금, 1-1절).
- 증가는 즉시.

### 5-3. 별도 풀
1. 가중 예산(위 Budget) — 모든 피해 능력.
2. 인원 상한(Heads) — 근접 접촉이 싼 비용으로 무더기 통과하는 것 방지.
3. **위험 공격 단독 풀 1개**: 잡기·막기 불가·대형 광역 텔레그래프는 대상당 동시 1개. 근거는 Amalur의 종별·전역 쿨다운(1-1절)이 같은 목적을 가졌다는 점뿐이고 "한 번에 1개" 규칙 자체는 [추론].
4. 원거리 별도 상한(선택): DOOM 종류별 토큰, 데브로그 근접/원거리 분리(1-2절).

### 5-4. 배정 우선순위(결정론 정렬 키)
기존 "거리 오름차순, 동률 SimulationId"(08:285)에 두 키를 앞에 추가하는 안:
1. 화면 안(카메라 절두체 안) 우선 — Spider-Man·DOOM(1-2, 1-5절). 탑다운에서는 대부분 화면 안이므로 효과가 작을 수 있음[추론].
2. 마지막 공격 후 경과 스텝 내림차순(오래 기다린 개체 우선) — Amalur의 순번 회전 목적(1-1절)을 정렬 키로 구현[추론].
3. 거리 오름차순 → 4. SimulationId.
- 플레이어가 조준 중인 대상 가산점(GoW, [요약-미검증])은 탑다운 조준 개념이 확정된 뒤 검토.

### 5-5. 보유·반납·가로채기
- 부여(예약)는 *공격을 고를 때*(접근 시작 전) — Signals·Amalur. 부여 때 그 종의 최대 비용으로 예약, 개시 때 실제 비용으로 정산(08:285 기존 결정과 동일).
- 획득 제한: 부여 후 일정 스텝(예: 128스텝 = 2초) 안에 개시하지 못하면 반납 — 데브로그의 획득 제한 시간, Spider-Man의 "너무 멀면 이양"(1-2, 1-5절). 수치는 [추론].
- 잠금: 와인드업(텔레그래프) 시작부터 잠금, 가로채기 불가 — Amalur.
- 반납: 활성 프레임 종료(회복 시작) 시 — Amalur는 "개시 직후" 반납. 탑다운 텔레그래프 판독을 위해 한 박자 늦게 반납하는 쪽을 권장[추론].
- 가로채기: *시작 전 예약만*, 그리고 Amalur 조건(보유자가 접근 반경 밖 + 후보가 안)일 때만. 시작한 공격을 취소시키는 DOOM식 가로채기는 시각적 끊김이 생기므로 비권장[추론].
- 플레이어 숨 고르기: 회피 성공 직후 N스텝 동안 *새 개시* 금지(Spider-Man), 처형·피니시 연출 중 새 개시 금지(DOOM). 수치 [추론].

### 5-6. 토큰 없는 몬스터의 행동
- 접근 반경 밖 대기 + *비어 있는 링 슬롯 앞에 서기*(Amalur → 자연 포위). 기존 D17 `ring_slot`과 결합 가능.
- 공격 반경 안에 토큰 없이 있으면 즉시 이탈(Amalur).
- 원거리 개체는 화면 안·플레이어 앞쪽 초승달로 재배치(GoW [요약-미검증]).
- 좌우 이동(strafe)·도발·페인트는 시각적 다양성 용도로 유효하나 출시작 1차 근거는 찾지 못함[추론].

---

## 6. 검증 상태 정리

확인(직접 읽음): Amalur Belgian AI 전 항목, TLOU Combat Coordinator, Halo 3 Objectives 용량·탐욕 배분, DOOM 2016 종류별·난이도별 토큰과 가로채기(2차 기사), Spider-Man 토큰 이양·화면 안 우선·회피 대기(2차 기사), Tsushima 경직 창·난이도 레버(2차), Signals and Light 티켓 일반론, TDGame 코드 인용 전부.

미검증: DOOM 구체 토큰 수와 원 강연 대조, DOOM Eternal 토큰·슬레이어 게이트 관련 규칙, God of War 전 항목(자동 요약), Batman Arkham 공격 대기열, Shadow of Mordor, Sifu, Assassin's Creed, Uncharted, TLOU2 근접 토큰, Halo 공격 동시성 제한, 텔레그래프 단독 규칙의 출시작 근거, 이식본 풀 축소 시 반납 세부(`TDCombatTokenSubsystem.cpp:224-240`).

## 7. 출처 목록
- Game AI Pro 28장(Amalur): http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter28_Beyond_the_Kung-Fu_Circle_A_Flexible_System_for_Managing_NPC_Attacks.pdf
- Game AI Pro 2 34장(TLOU): http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter34_Human_Enemy_AI_in_The_Last_of_Us.pdf
- Halo 3 Objectives 슬라이드: https://web.cs.wpi.edu/~rich/courses/imgd4000-d10/lectures/halo3.pdf
- DOOM AI 기사: https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-
- DOOM 강연 기반 데브로그: https://serfofcinder.itch.io/archetype-portal-to-hell/devlog/738495/enemy-ai-tech-talk
- DOOM GDC 2017: https://gdcvault.com/play/1024186/Bringing-Hell-to-Life-AI / GDC 2018: https://gdcvault.com/play/1024940/Embracing-Push-Forward-Combat-in
- Slayer Gate: https://doomwiki.org/wiki/Slayer_Gate
- TLOU2 근접: https://www.gamedeveloper.com/design/how-naughty-dog-defined-melee-attacks-and-behaviors-in-i-the-last-of-us-part-ii-i-
- God of War 강연 요약: https://videohighlight.com/v/hE5tWF-Ou2k / 원본 https://gdcvault.com/play/1026085/Evolving-Combat-in-God-of
- Spider-Man: https://www.gamedeveloper.com/programming/designing-ai-to-do-anything-a-spider-can-in-i-marvel-s-spider-man-i- / 목차 https://podwise.ai/episodes/1458953
- Tsushima: https://blog.playstation.com/?p=344024
- Arkham: https://www.gamedeveloper.com/design/artificial-intelligence-in-game-design , https://www.siliconera.com/the-improvements-to-batman-arkham-citys-combat-system/ , https://gdcvault.com/play/1035681/Punching-Across-the-Room-in
- Signals and Light: https://signalsandlight.substack.com/p/what-is-an-enemy-attack
- StraySpark(마케팅성, TLOU2 주장 미검증): https://www.strayspark.studio/blog/attack-token-system-ue5-group-combat-that-feels-fair
- 기타: https://godisageek.com/2014/06/middle-earth-shadow-mordor-michael-de-plater , https://www.theouterhaven.net/2021/12/sifu-hands-on-preview/ , https://gamedev.net/forums/topic/701048-tactical-circle-system/

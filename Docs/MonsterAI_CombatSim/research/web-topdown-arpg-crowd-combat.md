> 출처: 2026-10-18 claude M3-14·M3-28 조사(하위 에이전트 1회 조사). 문헌 주장의 [검증] 표기는 원문 독립 대조를 거치지 않았으므로 설계 근거로 인용할 때 원문을 다시 확인한다.

# r3 — 탑다운·아이소메트릭 ARPG·핵앤슬래시 군중 전투 AI 조사 (토큰·스쿼드·디렉터 외 추가 시스템)

작성: 2026-10-18, 읽기 전용 조사(하위 에이전트). 파일 수정 없음(이 보고서만 작성).
약어: ARPG(Action Role-Playing Game, 액션 RPG), AOE(Area of Effect, 범위 공격), Hz(초당 횟수), LOD(Level of Detail, 거리별 세부 수준), GAP(Game AI Pro 책), GDC(Game Developers Conference).

## 0. 출처 등급 표기

- **[1차]**: 원문(PDF 본문·슬라이드 텍스트·발표 전사)을 이 세션에서 직접 읽고 확인함.
- **[2차]**: 기사·위키·블로그·검색 요약을 통해 확인. 원 발표는 직접 못 봄.
- **[추론]**: 출처들을 근거로 한 이 보고서의 판단. 사실이 아님.
- **[미검증]**: 찾으려 했으나 확인 못 함.
- 코드 주장은 모두 `파일:줄`로 표기(현재 작업 트리 기준, 2026-10-18 읽음).

## 1. 결론 요약

1. 출하된 액션 게임 중 "어그로 수에 비례한 토큰 수(10마리 → 3개)"를 그대로 쓴 1차 출처는 찾지 못했다 [미검증]. 출처가 확인된 방식은 모두 **대상(플레이어)마다 고정 용량 + 몬스터·공격마다 무게**이고 난이도는 용량만 바꾼다(Kingdoms of Amalur [1차], DOOM 2016 [2차]). 사용자의 비례 규칙은 이 용량을 정하는 공식으로 얹으면 된다 [추론].
2. 토큰만으로는 "동시에 몇 마리"만 막고 **같은 순간 예고(텔레그래프) 겹침·같은 종류 범위 공격 동시 발동**은 막지 못한다. Amalur는 이를 개별·종족·전역 쿨다운으로 따로 막았다 [1차].
3. ARPG 몬스터 설계에서 공통으로 보이는 패턴은 **역할 분담**(돌격 자폭, 포위, 뒤에서 원거리·부활, 방패 기절, 강타)이다(Diablo IV 블리즈컨 2019 [2차]).
4. 군중의 "살아 있는" 느낌을 가장 싸게 주는 장치는 **사기·공포 전염**(우두머리 사망 시 도주, 과장된 연출)이다(Halo [2차], Diablo II Fallen [2차], Days Gone 자신감 체계 [1차]).
5. 현재 코드는 대상당 동시 시전 3개 고정(`TDMonsterThinkSubsystem.h:64`), 경보는 한 단계만 전파(`TDMonsterThinkSubsystem.cpp:461-484`), 역할 필드 없음(MonsterAI 헤더에서 role/archetype 문자열 검색 결과 없음).

## 2. 현재 코드 기준선 (훅 위치)

| 항목 | 위치 | 내용 |
|---|---|---|
| 토큰 판정 | `Source/TDGame/MonsterAI/TDMonsterThinkSubsystem.cpp:780-791` | 같은 대상을 `Cast` 상태로 공격 중인 다른 몬스터 수 < `MaxConcurrentAttackersPerTarget`이면 참. 공격 무게·비례·예약 없음 |
| 상한 상수 | `TDMonsterThinkSubsystem.h:64` | `MaxConcurrentAttackersPerTarget = 3` 고정 |
| 시전 수 집계 | `TDMonsterThinkSubsystem.cpp:770-778` | `Cast` 상태만 대상별 계수 |
| 스텝 순서 | `TDMonsterThinkSubsystem.cpp:348-365` | `RebuildNeighborGrid` → `UpdateEngagement` → `CountAttackersByTarget` → `OrderedSlots` 순 `ThinkAndAct`, 시전 진입 즉시 계수 증가(357-363). 순서 고정이므로 선착 순 결정론적 배정 |
| 토큰이 능력 마스크를 막음 | `TDMonsterThinkSubsystem.cpp:895-901` | 토큰 없으면 `ReadyAbilityMask`가 0 |
| 실행기 이중 차단 | `Source/TDGame/MonsterAI/TDMonsterActionExecutor.cpp:231` | 쿨다운 또는 `!Context.bHasAttackToken`이면 시전 거부 |
| 문맥 생성 | `TDMonsterThinkSubsystem.cpp:917-931` | `bHasAttackToken = HasAttackToken(SlotIndex)` (928) |
| 경보 전파 | `TDMonsterThinkSubsystem.cpp:461-484`, 반경 `h:60` (900) | 새로 교전한 몬스터 주변 900 이내 `Home` 몬스터가 같은 대상으로 교전. 경보로 깨어난 몬스터는 `NewlyEngaged`에 추가되지 않으므로 **연쇄 없음(한 단계)** |
| 피격 교전 | `TDMonsterThinkSubsystem.cpp:192-208` | 피격 시 경보 대기열 추가 + 교전 고정 `DamageEngageSeconds`(h:62, 6초) |
| 끈(leash) | `h:56-59,70`, `cpp:409-428` | 끈 3000, 최대 추격 4500, 대상 상실 3500, 포기 2초 |
| 대상 선택 | `cpp:832-875` | 플레이어 우선(840-844), 아니면 `EngageRadius` 안 최근접 + 위치 순 동률 처리 |
| 이웃 입력 | `cpp:903-913`, 반경 `h:61`(1200) | 같은 팀 이웃 목록을 두뇌 입력으로 → 무리 사기 입력에 재사용 가능 |
| 경직 | `h:63` `StaggerHealthRatio = 0.12` | 경직 존재(세부 미확인) |
| 프레임당 스텝 | `h:54` `MaxStepsPerFrame = 4` | 고정 스텝 따라잡기 상한 |

## 3. 게임별 조사 결과

### 3.1 Kingdoms of Amalur: Reckoning — "Belgian AI" (GAP 1권 28장, Michael Dawe) [1차]
URL: http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter28_Beyond_the_Kung-Fu_Circle_A_Flexible_System_for_Managing_NPC_Attacks.pdf

- 한 명씩만 공격하는 "쿵푸 서클"은 빠른 전투와 범위 공격 기회를 막아 너무 엄격하다고 보고, "2~3마리 허용"도 몬스터 종류·공격 강도를 반영 못 한다고 지적(28.2).
- 플레이어 중심 월드 정렬 격자, **슬롯 8개**, 두 값: **격자 용량**(접근 허가 마릿수 제한)·**공격 용량**(공격 종류·수 제한). 몬스터마다 **격자 무게**, 공격마다 **공격 무게**(28.3).
- 예시 수치: 격자 용량 12, 병사 4, 트롤 8 → 두 번째 병사는 격자 밖 대기. 공격 용량 10, 트롤 돌진 6·곤봉 4, 병사 찌르기 5·베기 3 → 트롤이 돌진하면 병사는 베기만 가능(28.3).
- 개별 몬스터에 공간 추론을 두지 않고 중앙 **무대 관리자(stage manager)**가 다음 갱신 때 요청을 처리(28.3).
- **접근 원·공격 원** 2중 원: 허가 받으면 접근 원, 특정 공격 허가 시 공격 원으로 진입, 미허가는 접근 원 밖 대기 → 플레이어가 즉시 위협을 구분(28.3.1).
- 미허가 몬스터가 바깥 원 안에 있으면 "가능한 빨리" 나가야 함. 미허가 대기자에게 **비어 있는 슬롯 위치**를 주고 그 앞에 서게 하여 서로를 몰라도 자연스러운 측면 포위가 생김(28.4).
- **공격을 시작하자마자 슬롯 반납** → 대기열이 회전, 한 마리가 독점하지 못함(28.4).
- 난이도는 몬스터 무게를 건드리지 않고 **용량만** 올림(28.5).
- 플레이어가 구르면 배정이 낡음 → 관리자가 매 프레임 재배정, 위치가 더 좋은 대기자에게 허가를 **빼앗아 줌**, 단 공격 중인 몬스터는 배정을 **잠금**(28.6.1).
- 공격 용량만으로는 같은 종류 공격이 몰리는 것을 못 막으므로 **개별·종족 단위·전역 쿨다운**으로 일정 시간 안 같은 공격 반복을 막음. 특히 최고 난이도에서 범위 공격 동시 사용 방지에 "크게" 도움(28.6.2).

### 3.2 DOOM (2016) — 공격 종류별 토큰 [2차]
URL: https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-

- 공격 종류(근접·원거리·충전 등)마다 한정된 토큰 풀, 공격 전 요청·끝나면 반납. 더 좋은 위치의 악마가 토큰을 **빼앗을** 수 있음. 토큰 수는 난이도별(구체 수치는 기사에 없음 → [미검증]).
- 원거리 악마는 가능하면 거리 유지, 엄폐는 숨기용이 아니라 노출된 위치 선택. 플레이어가 빠를수록 명중률 하향.

### 3.3 Diablo IV — 몬스터 가족·역할 [2차]
- 블리즈컨 2019 전사(https://diablo.blizzplanet.com/blog/comments/blizzcon-2019-diablo-iv-systems-and-features-panel-transcript/5): Fallen 광신자는 달려와 자폭, Fallen 졸개는 **포위 시도**, Fallen 주술사는 **뒤에서** 화염구·졸개 **부활**. 해골: 방패병 기절, 양날도끼 내려찍기, 석궁병 **중거리**, 발리스타는 화면 여러 개 거리 원거리 압박으로 "교전 유지 vs 회피" 선택을 강요. 정예 "강화 접사"는 기본 능력을 확장(다중 사격 주술사는 여럿 부활). 보스는 제어 효과가 경직 게이지를 채움.
- 2020-02 분기 업데이트 보도(https://gamingbolt.com/diablo-4-inventory-and-action-bar-changes-monster-families-and-more-revealed, https://rpgamer.com/2020/02/diablo-iv-gets-quarterly-development-update/): 가족마다 전투 스타일, 원형(archetype)은 bruiser·ranged·melee·swarmer 등, Cannibal 가족은 **의도적으로 원거리 없음**, 구성원마다 다른 실루엣·자세(가독성).
- 시즌 11 개편(2025-11-05 Icy Veins, https://wp-prod.icy-veins.com/?p=108751): 고정 쿨다운 순환 대신 **난이도에 따라 조정되는 행동 풀**에서 선택, 정예·챔피언 행동 복잡화, 해골은 인지가 낮아 즉시 알아채지 않음, 무리 구성 변화(전원 원거리 무리 등). 개발자 Colin·Zaven 인터뷰 기반 기사 요약.
- 동시 공격 마릿수 제한 수치: [미검증] (찾지 못함).

### 3.4 Diablo II Fallen — 짧은 공포 [2차, 팬 위키]
URL: https://diablo2.diablowiki.net/Fallen
- 근처에서 누가 죽이면 Fallen 전원이 **1~2초** 도망 후 복귀, 길이 막히면 계속 싸움, **주술사는 도망 안 감**. Diablo III에서는 감독관의 함성이 겁을 상쇄(https://diablo-archive.fandom.com/wiki/Fallen_Grunt, 팬 위키).

### 3.5 Halo — 우두머리 사망 시 도주 [2차]
URL: https://www.cs.du.edu/~sturtevant/s13-ai/lecture2.pdf (GDC 2002 "The Illusion of Intelligence" 인용 강의), https://gamedeveloper.com/programming/abandoning-subtlety-to-create-immersive-ai-encounters
- 엘리트가 죽으면 그런트가 도망. 처음엔 아무도 눈치 못 채서, 출시 때는 **매번 모든 그런트가 과장된 공포 애니메이션**으로 도망. "미묘함은 플레이어 머리 위로 지나간다"는 Damian Isla의 주장.

### 3.6 Days Gone — 스쿼드·자신감(사기) (GAP 온라인판 2021 12장) [1차]
URL: http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter12_Squad_Coordination_in_Days_Gone.pdf
- 스쿼드는 거리로 자동 합병·분리(그림 1).
- 자신감 5단계: 공황·걱정·중립·자신·영웅. 스쿼드 자신감 = 구성원 평균을 중립 쪽으로 반올림. 공격·버티기·후퇴 결정을 좌우(3장).
- 계산: (자기 힘 + 아군 힘×아군 자신감 보정 + 처치한 적 힘) / (적 힘×적 자신감 보정 + 잃은 아군 힘)을 임계값과 비교(공식 1).
- **공황 전염**: 공황 상태 캐릭터의 힘은 자기 편이 아니라 **적 편에 더함** → 스쿼드 붕괴(3.1).
- 처치·손실 효과는 전투보다 훨씬 긴 시간에 걸쳐 감쇠(3.1). 몇 번 갱신 안에 안정값으로 수렴(3.2).
- 플레이어 특례: **공격적인 플레이어에겐 소극적, 소극적인 플레이어에겐 압박**. 정지·엄폐 시 AI 자신감 상승, 돌격·근접·조준 보조 대상이면 하락(3.3).
- 역할은 개별 행동 계층의 정해진 지점에 끼우는 하위 행동 묶음, 개인은 상황상 역할 거부 가능(서론). 측면 포위는 "가장 효과적"이 아니라 "보기 좋은 전투" 목표로 일정 시간 허용(7.2).

### 3.7 The Last of Us 감염자 (GAP 2권 33장, Mark Botta) [1차]
URL: http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter33_Infected_AI_in_The_Last_of_Us.pdf
- 발견 시 돌아서서 **비명**(플레이어에게 들켰음을 알림), 추격 중 가끔 멈춰 재탐색(플레이어 대응 창)(33.4.2).
- 감염자끼리 정보 공유 없음 → 너무 둔감해 보여 **추종(follow) 스킬** 추가: 추격 중인 개체를 근처 개체가 정보 없이 따라감, 처음 알아챈 개체가 가장 먼저 도착(33.4.3).

### 3.8 Left 4 Dead AI 디렉터 (Michael Booth, 2009) [1차, 슬라이드 텍스트]
URL: https://steamcdn-a.akamaihd.net/apps/valve/2009/ai_systems_of_l4d_mike_booth.pdf
- 생존자별 감정 강도: 피해량 비례 증가, 무력화·낭떠러지 밀림 시 증가, **근처 감염자 사망 시 거리 반비례 증가**, 시간에 따라 0으로 감쇠, 단 **교전 중이면 감쇠 안 함**.
- 4단계: 강도 정점까지 전체 위협(Build Up) → 정점 후 **3~5초** 유지(Sustain Peak) → 최소 위협으로 바꾸고 자연스러운 휴지까지 대기(Peak Fade) → **30~45초** 최소 위협(Relax).
- 휴식 중 배회자·무리·특수 감염자 생성 중지(이미 있는 특수는 공격 가능), **보스는 적응 페이싱 대상 아님**.
- 무리 생성 90~180초 무작위 간격, 생존자 "뒤", 무리의 75%는 뒤에서.

### 3.9 무쌍(Dynasty Warriors)·Samurai Warriors — 지휘관과 사기 [2차, 저권위]
URL: https://TVTropes.org/pmwiki/pmwiki.php/Main/MoraleMechanic
- 목표 달성·실패, 지휘관 격파, 거점 점령으로 군 사기 변화. DW6는 지휘관이 쓰러지면 병사 흩어짐, Warriors Orochi는 지휘관 격파 또는 짧은 시간 다수 처치 시 전의 상실, Samurai Warriors는 기수(깃발병) 처치 시 도주. "졸개가 차례를 기다린다"는 구체 구현 출처는 [미검증].

### 3.10 Path of Exile — 희귀 몬스터 오라 [2차, 위키]
URL: https://www.poewiki.net/wiki/Soul_Conduit (몬스터 수정자 목록 사본), https://www.poe2wiki.net/wiki/Rare
- 희귀 몬스터는 접사 3개 중 하나가 오라일 수 있음(위키 자체가 인용 필요 표시). 예: Benevolent 오라는 근처 아군에게 Intervention 버프 **5초, 재사용 6초**. PoE2 위키: 희귀 수정자 일부는 주변 몬스터를 강화하는 오라·버프.

### 3.11 카메라·화면 밖 공격 [2차, 블로그]
URL: https://signalsandlight.substack.com/p/how-do-enemy-attacks-work-with-the (Isaiah Everin Cooper, 2024-06-01)
- 근접 게임의 "전투 원" 지점으로 균등 분산, 원거리는 거점 점유.
- 화면 밖 처리 3종: 화면 밖 공격 금지(Bayonetta 2009 규칙으로 소개), **화면 밖 공격은 토큰을 더 비싸게**, 무관.
- God of War(2018) 발표: Kratos 주변을 사분면으로 나눈 위치 관리자로 적이 대략 같은 구역 유지. 경고 표시는 예비동작부터 발동까지 단계형.
- God of War 발표 요약(https://videohighlight.com/v/hE5tWF-Ou2k): 화면 밖 적에 불안 → 위치·조준 여부로 적 공격성 점수화 [2차].

### 3.12 Hades II 가독성 비판 [2차, 의견 글]
URL: https://nex-3.com/blog/hades-ii-has-a-legibility-problem/ (Natalie Weizenbaum, 2025-10-04)
- Hades 1 보스는 근접 판정과 바깥으로 나가는 투사체 중심, 큰 범위 공격은 예외. Hades II는 바닥이 붉게 변하는 범위 공격·여러 겹 원·먼지에 가려 약 1초 늦게 보이는 경고·흐린 예고색이 문제, "화면 위 상호작용의 밀도와 혼란"을 원인 중 하나로 지목.

### 3.13 대규모 군중 이동 — Supreme Commander 2 흐름장 (GAP 1권 23장, Elijah Emerson) [1차, 일부]
URL: http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter23_Crowd_Pathfinding_and_Steering_Using_Flow_Field_Tiles.pdf
- 수백~수천 개체 이동 문제 해결, 1m 격자, 10×10 격자 섹터, 섹터 경계 포털. 고정 A* 경로는 충돌 시 정지·대기로 게임이 멈춤.

### 3.14 찾지 못한 것 [미검증]
Lost Ark, Last Epoch, Torchlight, No Rest for the Wicked, Vampire Survivors류의 몬스터 협동·토큰 1차 자료, Hades 웨이브 동시 적 상한 필드(EncounterData)는 이번 예산 안에서 확인 못 함. Diablo IV의 동시 공격 상한 수치도 없음.

## 4. 교차 패턴 [추론]

1. **대상당 예산 + 무게 + 난이도는 용량만**: Amalur·DOOM 공통. 무게 덕에 "돌진은 한 번에 하나"가 규칙 없이 생김(돌진 6/용량 10).
2. **허가 없는 몬스터를 보이는 위치에서 기다리게**: Amalur 2중 원, God of War 사분면, Signals and Light 전투 원. 대기 위치 자체가 포위·측면 연출.
3. **반납 즉시·빼앗기 허용·공격 중 잠금**: 회전과 위치 반응성의 핵심.
4. **같은 종류 동시 발동 금지는 별도 장치**: 토큰과 독립(Amalur 28.6.2, Hades II 비판).
5. **역할은 가족 단위로 설계하고 실루엣으로 읽히게**: Diablo IV.
6. **사기·공포는 과장해야 읽힌다**: Halo, D2 Fallen(1~2초), Days Gone(공황 전염).
7. **경보는 소리·추종으로 퍼지고, 첫 발견자가 먼저 도착**: TLOU.
8. **플레이어 공격성에 반응하는 압박**: Days Gone, Diablo IV 시즌 11의 "카이팅 대기" 감소 의도.

## 5. 요청 3종에 바로 얹을 보강 규칙 (조사 근거)

| 대상 | 규칙 | 값(초기 권장) | 근거 |
|---|---|---|---|
| 토큰 수 | 비례 용량 `Tokens = clamp(floor(AggroCount/3), 1, DifficultyCap)` 정수 계산, 무게 예산과 결합 시 `Budget = Tokens × 1.0` | 쉬움 2·보통 3·어려움 4 상한(기존 SC-03) | 사용자 요구 + Amalur 28.5 [추론] |
| 토큰 회전 | 공격 **시작 시 반납**(정산은 시작 시점) | — | Amalur 28.4 [1차], SC-03 "부여 시 예약·시작 시 정산"과 일치 |
| 빼앗기 | 대기자가 허가자보다 대상에 가깝고 허가자가 공격 원 밖이면 이전, 공격 중이면 잠금 | 조정기 주기마다 | Amalur 28.6.1 [1차], DOOM [2차] |
| 대기 위치 | 미허가 몬스터는 공격 원 밖, 빈 슬롯 앞에 서기 | 접근 원 = 근접 최대 사거리 + 1.5~2.5m [추론] | Amalur 28.3.1·28.4 [1차] |
| 화면 밖 | 화면 밖(탑다운 뷰포트 밖) 공격은 무게 ×2 또는 금지 | ×2 [추론] | Signals and Light [2차] |
| 디렉터 | 강도 = 받은 피해 비례 + 근처 몬스터 사망 거리 반비례, 교전 중 비감쇠 / 4단계, 유지 3~5초·휴식 30~45초, 보스는 제외 | L4D 값 그대로 시작 | L4D [1차] |

## 6. 추가 추천 시스템 — 체감 가치 ÷ 구현 비용 순

전제: C++ 고정 스텝 1/64초, `SimulationId` 오름차순 처리, 전역 난수 금지(개체별 시드 스트림), 조정기는 32스텝(2Hz) 주기(기존 06 문서 5-5). 모든 시간 값은 정수 스텝.

### 순위 1. 예고 겹침 제한기·공격 시작 간격 (Telegraph Overlap Limiter)
- 체감: 매우 높음(군중 가독성의 핵심) / 비용: 낮음.
- 메커니즘: 대상마다 최근 공격 시작 스텝 기록. 새 시작은 ①직전 시작과 `MinStartSpacingSteps` 이상 떨어져야 하고 ②진행 중인 바닥 예고(범위 공격) 수가 `MaxGroundTelegraphs` 미만이어야 하며 ③같은 공격 계열 태그의 전역 창(`FamilyWindowSteps`) 안에 같은 계열 시작이 없어야 함. 조건 실패 시 토큰은 유지한 채 다음 스텝 재시도.
- 값 [추론]: 시작 간격 12~16스텝(0.19~0.25초), 바닥 예고 동시 2개(어려움 3), 같은 계열 창 32스텝(0.5초).
- 근거: Amalur 개별·종족·전역 쿨다운 [1차], Hades II 밀도 비판 [2차].
- 훅: `TDMonsterThinkSubsystem.cpp:895-901`(능력 마스크), `TDMonsterActionExecutor.cpp:231`(시전 거부). 공격 계열 태그는 공격 카탈로그 쪽이 필요 → `TDMonsterAttackCatalog.*`는 M3-27 세션 편집 중이므로 조율 필요.

### 순위 2. 경보 연쇄·비명·추종 (Alert Ripple)
- 체감: 높음(무리가 깨어나는 파문이 보임) / 비용: 낮음(기존 코드 확장).
- 메커니즘: 현재 한 단계 경보(`cpp:461-484`)를 홉(hop) 수 제한 연쇄로. 경보로 깨어난 개체를 `HopsRemaining-1`로 다음 대기열에 넣되 `HopDelaySteps` 뒤 처리. 첫 발견자만 비명 연출. 추종자는 대상 정보 없이 발견자를 따라가 늦게 도착(TLOU). 가족별 인지 계수(해골은 낮음, Diablo IV 시즌 11).
- 값 [추론]: 최대 2홉, 홉 지연 8~16스텝, 경보 반경 900 유지(`h:60`), 끈 검사(`cpp:476`) 유지.
- 근거: TLOU 33.4.2~33.4.3 [1차], Diablo IV 시즌 11 [2차].

### 순위 3. 역할 정책 (Archetype Role Policy)
- 체감: 높음 / 비용: 중간(정의 JSON 필드 + 조정기 규칙).
- 역할 6종과 규칙:
  - 돌격(자폭·돌진): 대상당 동시 1 — 무게 0.75~1.0으로 자연 제한(Amalur 돌진 6/10).
  - 포위(근접 졸개): 빈 슬롯 앞 대기, 측면 우선.
  - 강타(브루저): 무게 1.0, 예고 길게.
  - 원거리: 거리 띠 유지(예: 대상과 6~10m [추론]), 근접 전열보다 뒤, 화면 안 위치 우선. DOOM 거리 유지 [2차], D4 석궁 중거리 [2차].
  - 지원(부활·버프): 근접 전열 뒤 일정 거리, 부활은 무리당 쿨다운·동시 1 [추론], D4 주술사 [2차].
  - 소환: 무리 전체 생존 수 상한과 연동 [추론].
- 정의 정본 `Content/MonsterAI/Definitions/<Id>.json`에 `Role` 추가가 필요(현재 헤더에 역할 필드 없음). 정의 라이브러리(`TDMonsterDefinitionLibrary.*`)는 M3-27 편집 중 → 조율 후 추가.

### 순위 4. 무리 사기·공포 전염 (Pack Morale)
- 체감: 높음(살아 있는 군중) / 비용: 중간.
- 메커니즘(Days Gone 단순화): 무리 단위 정수 사기 = (아군 힘 + 처치한 적 힘) 대 (적 힘 + 잃은 아군 힘) 비율을 5단계로 양자화. 공황 개체 힘은 적 편에 더해 전염. 우두머리 사망 시 큰 충격(Halo), 근처 처치 목격 시 1~2초 겁먹음 후 복귀, 막히면 계속 싸움, 지원 역할·감독관 함성은 면역·상쇄(D2·D3 Fallen). 연출은 과장(Halo).
- 값 [추론]: 공포 64~128스텝(1~2초), 사기 갱신은 조정기 2Hz, 처치·손실 기억 감쇠는 전투보다 길게(예: 20초 반감).
- 입력 재사용: 이웃 목록 `cpp:903-913`, 피격 알림 `cpp:192-208`.
- 근거: Days Gone 3장 [1차], Halo [2차], D2 Fallen [2차], 무쌍 지휘관 [2차].

### 순위 5. 화면 밖·등 뒤 공격 억제 (Camera-aware Attack Cost)
- 체감: 중상 / 비용: 낮음.
- 메커니즘: 공격자 위치가 뷰포트 밖이면 토큰 무게 ×2 또는 금지, 등 뒤 공격은 예고 시간 연장. 결정론을 위해 시뮬 쪽에는 "카메라 사각형"을 스텝 시작 시 고정 입력으로 받거나, 시뮬러에서는 대상 중심 고정 사각형으로 근사 [추론].
- 근거: Signals and Light(Bayonetta 규칙·토큰 가중) [2차], God of War 위치 관리 [2차].

### 순위 6. 우두머리 오라·연결 (Pack Leader Link)
- 체감: 중상(정예 무리 의미 부여) / 비용: 중간(전투 버프 경로 필요).
- 메커니즘: 정예가 반경 안 졸개에게 오라(이동·공격 속도 등) 부여, 졸개는 정예 접사 일부 상속, 우두머리 사망 시 오라 소멸 + 사기 충격(순위 4와 연결).
- 값: PoE Benevolent 5초 지속·6초 재사용 [2차]를 출발점으로.
- 근거: PoE 위키 [2차], Diablo IV 강화 접사 [2차]. 버프 적용은 `UTDCombatComponent`·GAS 경로라 `Combat/Damage/*` 편집 중인 M3-27과 조율.

### 순위 7. 플레이어 공격성 반응 압박 (Initiative Response)
- 체감: 중 / 비용: 중간.
- 메커니즘: 플레이어가 최근 N초 몰아치면 몬스터 사기·공격 예산 일시 하향(반격 창), 정지·카이팅이면 상향·원거리 압박 증가. 디렉터 강도와 함께 입력.
- 근거: Days Gone 3.3 [1차], Diablo IV 시즌 11의 "카이팅 대기" 감소 의도 [2차].

### 순위 8. 대량 성능 계층 (Crowd LOD)
- 체감: 100마리 미만에서는 낮음, 150~300마리에서 높음 / 비용: 중상.
- 메커니즘: 대상별 조정기 2Hz 유지, 화면 밖·원거리 몬스터는 생각 주기 1/2~1/4(정수 스텝 배수, `SimulationId` 위상 분산), 150마리 이상 같은 대상 추격 시 대상 중심 흐름장 1장 공유.
- 근거: Supreme Commander 2 흐름장 [1차 일부]. 생각 주기 값은 [추론].

## 7. 결정론 주의 [추론]

- 비례 토큰 공식·무게 합은 정수(무게를 4배 정수로: 접촉 1, 원뿔 3, 기타 4)로 다뤄 부동소수 누적 차이를 피함.
- 빼앗기·재배정은 조정기 주기에만, `SimulationId` 오름차순 + 거리 동률 시 ID 작은 쪽.
- 공포 지속·홉 지연의 흔들림이 필요하면 개체별 시드 스트림에서 뽑음(전역 `FMath::FRand` 금지).
- 화면(카메라) 입력은 게임 전용이므로 시뮬과 게임이 같은 결과를 내려면 고정 사각형 근사가 필요.

## 8. 조율 제약

- M3-27 세션 편집 파일(`Characters/TDGameCharacter.*`, `Combat/Damage/*`, `MonsterAI/TDMonsterAttackCatalog.*`, `MonsterAI/TDMonsterDefinitionLibrary.*`)은 손대지 않음. 순위 1(공격 계열 태그)·3(역할 필드)·6(오라 버프)은 이 파일들과 닿으므로 그 세션 완료 후 또는 메시지 조율 후.
- 순위 2·4·5·8과 토큰 조정기는 `TDMonsterThinkSubsystem.*` 중심이라 충돌이 적음.
- 비헤이비어 트리 기반 `AI/CombatToken` 과제는 D4/D44로 전투 코어에 못 씀(주어진 사실).

## 9. 출처 목록

- GAP1 28장 Amalur: http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter28_Beyond_the_Kung-Fu_Circle_A_Flexible_System_for_Managing_NPC_Attacks.pdf
- GAP 온라인 2021 12장 Days Gone: http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter12_Squad_Coordination_in_Days_Gone.pdf
- GAP2 33장 TLOU 감염자: http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter33_Infected_AI_in_The_Last_of_Us.pdf
- GAP1 23장 흐름장: http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter23_Crowd_Pathfinding_and_Steering_Using_Flow_Field_Tiles.pdf
- L4D 디렉터: https://steamcdn-a.akamaihd.net/apps/valve/2009/ai_systems_of_l4d_mike_booth.pdf
- DOOM 2016: https://www.gamedeveloper.com/design/cyber-demons-the-ai-of-doom-2016-
- Diablo IV 블리즈컨 2019: https://diablo.blizzplanet.com/blog/comments/blizzcon-2019-diablo-iv-systems-and-features-panel-transcript/5
- Diablo IV 분기 업데이트 보도: https://gamingbolt.com/diablo-4-inventory-and-action-bar-changes-monster-families-and-more-revealed , https://rpgamer.com/2020/02/diablo-iv-gets-quarterly-development-update/
- Diablo IV 시즌 11: https://wp-prod.icy-veins.com/?p=108751
- Diablo II Fallen: https://diablo2.diablowiki.net/Fallen , Diablo III: https://diablo-archive.fandom.com/wiki/Fallen_Grunt
- Halo: https://www.cs.du.edu/~sturtevant/s13-ai/lecture2.pdf , https://gamedeveloper.com/programming/abandoning-subtlety-to-create-immersive-ai-encounters
- 무쌍 사기: https://TVTropes.org/pmwiki/pmwiki.php/Main/MoraleMechanic
- PoE: https://www.poewiki.net/wiki/Soul_Conduit , https://www.poe2wiki.net/wiki/Rare
- 카메라·화면 밖: https://signalsandlight.substack.com/p/how-do-enemy-attacks-work-with-the , God of War 요약: https://videohighlight.com/v/hE5tWF-Ou2k
- Hades II 가독성: https://nex-3.com/blog/hades-ii-has-a-legibility-problem/
- 일반 UE5 토큰 해설(저권위, 참고만): https://www.strayspark.studio/blog/attack-token-system-ue5-group-combat-that-feels-fair

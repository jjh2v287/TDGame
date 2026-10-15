# 웹: 비원형(길쭉한) 에이전트 국소 회피와 회전 처리 문헌 조사

조사 일자: 2026-10-14. 작성: claude. 범위: 직사각형·다각형·타원·캡슐·다중 원 몸의 국소 회피에서 '회전'을 다루는 방법(로봇·군중 학계, 연속 충돌 검출, 게임 업계 공개 자료). 대상 설계: `Docs/MonsterAI_CombatSim/research/project-elongated-monster-avoidance.md`(이하 설계 문서)의 C·D_esc·M4·M_arc.
방법: 웹 조사(WebSearch → WebFetch로 본문 열람) + 원문 대조 반박(갈래 3개마다 독립 검증자가 원문을 다시 열어 대조, 지적 33건 전부 반영, 7절). 1차 노트 `Saved/AgentOps/20261014/avoid-literature.md`의 문헌 부분은 요지와 출처를 옮겼고(2차 재검증에서 빠진 세부·URL을 보강, 7절), 그 노트의 프로젝트 사실·후보 설계·열린 질문은 설계 문서로 이관되어 생략한다. 갈래 노트: 같은 폴더 `lit-primary2.md`·`lit-academic.md`·`lit-crowd-games.md`(git 제외). PDF는 WebFetch가 자동 저장한 사본에서 pypdf로 텍스트만 뽑아 읽었다(내려받기 명령 없음, 그림·수식 배치가 깨졌을 수 있음). 코드는 `gh api` 표준 출력으로만 읽었다.

표기: [W#] 웹 출처(6절) · [E] 엔진 소스 파일:줄(UE 5.8 읽기 전용) · 확인 수준 [원문] 본문 직접 읽음 / [원문·반박] 반박 단계에서 본문 확인 / [초록] 초록·소개만 / [2차] 다른 문헌·검색 요약 경유 / [미확인] · 보조 [코드] 공개 코드 / [확인] 로컬 파일 직접 읽음 / [추론] 계산·해석 / [제안] 미측정 설계 제안. 수치는 원문 단위·조건 그대로이며 하드웨어가 모두 달라 비율로 읽는다.

약어: VO(Velocity Obstacle, 속도 장애물) · RVO(Reciprocal VO, 상호 VO) · HRVO(Hybrid RVO, 혼합 상호 VO) · ORCA(Optimal Reciprocal Collision Avoidance, 반평면 + 선형 계획) · RRVO(Reciprocally-Rotating VO, 상호 회전 VO) · GVO(Generalized VO) · NH-(Non-Holonomic, 비홀로노믹) · ERVO(Elliptical RVO) · CTMAT(Continuous Medial Axis Transform, 연속 매체축 변환) · OBB(방향 있는 경계 상자) · AABB(축 정렬 경계 상자) · SAT(분리축 정리) · TTC(Time To Collision, 충돌까지 시간) · TOI(Time Of Impact, 충돌 시각) · CA(Conservative Advancement, 보수적 전진) · GJK(Gilbert–Johnson–Keerthi 거리 알고리즘) · LP·QP·MIQP(선형·2차·혼합 정수 2차 계획) · MPC·NMPC((비선형) 모델 예측 제어) · OBCA(Optimization-Based Collision Avoidance) · CBF·NCBF(제어 장벽 함수·비평활 CBF) · CLF(제어 리아푸노프 함수) · IPOPT(내점법 최적화기) · SVM(서포트 벡터 머신) · MSDE(변까지 최소 부호 거리) · GVD(일반화 보로노이 도형) · PBD(Position-Based Dynamics, 위치 기반 동역학) · PD(비례-미분 제어) · ADL(애니메이션 주도 이동) · LOD(세부 수준) · UAF(Unreal Animation Framework) · GAP(Game AI Pro 책) · TR(기술 보고서) · HZD/HFW(Horizon Zero Dawn/Forbidden West) · AC(Assassin's Creed) · JWE(Jurassic World Evolution) · RDR2(Red Dead Redemption 2). 학회·학술지 약칭은 발표처로만 쓴다.
설계 기호(설계 문서 그대로): C = HZD식 직사각형 VO(하이에나) · D_esc = 회전 인지 샘플링 + 끼임 탈출(대형) · M_arc = 의도 방향을 현재 yaw ±20°로 자르는 호 선회 · M4 = 슬랩 TTC + 최대 선회량 쓸어낸 상자(벤치 변형, 품질 미측정).

## 0. 결론 요약

1. 회전 처리 계보는 다섯 갈래: ① 현재 자세 고정(HZD 2017·Huang 2023·Disney 2021) ② 보수적 쓸어냄(ERVO-F 2016의 한 스텝 각도 δt×최대 각속도 쓸어냄·Box2D CA. CTMAT 2018의 쓸어낸 튜플은 사전 계산 표의 각도 칸 덮개이고 회전 무충돌은 Narang 2017에 위임) ③ 회전을 이산 결정 변수로 표본(RRVO 2014) ④ 자세를 최적화·제어 상태로(OBCA·분산 OBCA·다각형 MPC는 표본 시각, NCBF는 각속도 1차 반영) ⑤ 회전을 회피와 분리해 규칙·제어기로(Torso Crowds·FDS+Evac·FbCrowd).
2. 회피 속도를 고른 뒤의 방향 변화를 연속 검사하는 실시간 군중 방법은 못 찾았다. ②는 보수(과대), ③은 표본 사이 낙관(과소), ④는 몸 기하는 정확하나 회전은 표본 시각(OBCA·MPC) 또는 1차 변화율(NCBF)로만 다뤄 표본 사이는 보장하지 않고(2.4.3·2.4.4), 1~4대에 평균 21ms~십수 초(최악 약 98초)다(3절 표, 같은 행의 원형 VOCBF 9.6ms는 ④ 밖).
3. 실시간 비용: HZD 개체당 약 60µs(PS4), RRVO δ=2 에이전트당 39~62µs(4스레드 벽시계, 하한), 설계 문서 D_esc(M3p) 10.7~20.6µs/마리·스텝(단일 스레드 벤치). 형상 회피 몫은 몬스터 AI 전체 4ms가 아니라 03 근접 처리 두 행 1.06~1.46ms/프레임(설계 문서 4.8, 300마리면 마리당 프레임당 약 3.5~4.9µs)이다. µs 단위인 C·M4·D_esc도 전원 적용은 불가(전원 M2/M4/M3 1.56~7.03ms/프레임 [측정], 설계 문서 4.8 표)이고, 등급제로 소수(대형 20 D_esc 등)에만 쓸 때 들어간다. ms 단위인 ④ 최적화 계열은 소수에도 불가.
4. 업계: 회전 예측 회피의 공개 사례는 이번 범위에서 미발견(검색 제한). HZD는 방향 인식(현재 자세) VO다. 주류는 크기별 정적 팽창 + 우선순위·물리 밀기 + 회전 불변 외피다.
5. TDGame 결론은 그대로다. C는 HZD·Huang과 같은 계열이다. D_esc는 자기 회전을 시간축으로 적분하고 이웃도 선회율로 예측해(프로토 [확인]) RRVO보다 정확할 것으로 본다 [추론, 같은 장면 비교 측정 없음]. M4의 '1.5초 최대 선회 상자'는 600×220에서 선회 38° 이상이면 폭 축이 외접원 반경에 닿고, 135°면 정확한 쓸어낸 영역도 외접원이 된다 [추론, 4.3] → 비채택 유지. M_arc(회전 = 운동 제약)는 Torso Crowds의 RRVO 옆 미끄러짐 비판과 같은 방향이다.
6. 가져올 부품(제안·미측정): Huang식 꼭짓점 쌍 외적 접선(C), RRVO 정면 투영 배제(C, 단독 효과 측정 필요), Box2D 양방향 전진 TOI(D_esc 상위 후보 재검증), Torso 비켜 주기 규칙, FbCrowd 도달 가능 속도 다각형.

## 1. 용어

- 현재 자세 OBB 민코프스키 합: 지금 방향의 두 몸으로 A⊕(−B)를 만들고 원점 접선 원뿔을 VO로 쓴다. 길쭉해도 나란히 붙을 수 있지만, 회피 속도를 고른 뒤 몸이 돌면 VO가 틀린다(HZD·Huang).
- 회피 중 방향 변화: 고른 속도를 따라 몸이 도는 동안의 자세 변화. 넣는 방법은 쓸어낸 모양으로 묶기·자세 표본 검사·자세를 최적화 변수로 두기 셋이다.
- 쓸어낸 모양: 일정 시간·각도 동안 몸이 차지한 영역의 합집합 또는 볼록 껍질. 시간 정보를 버려 회전각이 크면 외접원으로 수렴한다(360°면 원). 표본 시각 검사는 시간을 유지하지만 표본 사이를 안 본다.
- 홀로노믹/비홀로노믹: 몸 방향과 무관하게 즉시 아무 방향으로나 이동(RRVO·HZD 속도 선택의 가정) / 속도 방향이 몸 방향에 묶임(차량, M_arc).
- 상호성: 상대도 같은 규칙으로 피한다고 보고 책임을 나눔(RVO·ORCA·RRVO). 규칙이 다른 개체가 섞이면 깨진다. 보수/낙관 근사: 실제보다 크게(교착↑)/작게(관통↑) 잡음.

## 2. 방법 카드

형식: 출처·확인 수준 / 몸 / 회전 / 방법 / 비용·결과 / 한계 / 적합(탑다운, 300마리, 1/64초 고정 스텝, 단일 스레드, 자체 C++).

### 2.1 직사각형·다각형 VO

#### 2.1.1 HZD 9장 직사각형 VO [원문] [W1] (줄은 `Saved/AgentOps/20261014/hzd_ch09.txt`)
- 몸: 직사각형, 원이 섞이면 직사각형으로 바꿈(:329-334).
- 회전: 현재 자세(처음 방향) 직사각형 두 개의 민코프스키 합 VO를 장애물 속도만큼 평행 이동, 시간 절단 없이 선택 단계 충돌 시간 임계로 거름(:14-18·146-155·179-184). 고른 뒤의 방향 변화는 VO에 넣지 않고(첫 시제품 가정 그대로 출시) 장애물 근처 회전 점수 벌점으로 관통을 줄임. 출시판도 '아주 가끔' 관통, 주로 밀집·다동작(:26-46).
- 방법: 보수 충돌 시간 = (중심 거리 − 외접원 반경 합)/(내 최대 속력 + 상대 속도 접근 성분) 순 ≤5, 뒤쪽 120°·낮은 우선순위·목적지 너머 제외, 높은 등급은 낮은 등급 무시·정지한 낮은 등급도 위험 구역에서 비킴(:55-110). 결합 VO 외곽 꼭짓점·변 교점·최근접점·속력 원 교점 후보 → 경험식 점수(회전·속도 변화·각속도 역행 벌점) → 없으면 정지 후 희망 속도, 장애물 속도 0.5초 평균(:186-304).
- 비용: 밀집 20마리 개체당 약 60µs(PS4), 이동 CPU의 약 1/3(:352-354). 한계: 정적 지형 무시(2.9), 경로 추종과 싸우고 움직임이 기계적으로 보일 수 있음(:371-381).
- 적합: C의 원형. 탑다운은 발자국이 늘 보여, 플레이어 시선이 다른 데 있어 눈에 덜 띈다는 논거가 약하다 [추론].

#### 2.1.2 RRVO — Giese·Latypov·Amato [원문] [W2 ICRA 판, W3 TR 판, W4]
- 서지: ICRA 2014 3234–3241쪽, DOI 10.1109/ICRA.2014.6907324. TR13-009(2013-10-28, 16쪽) 별도 판. 초록의 δ 비용이 TR '선형', ICRA '2차'.
- 몸: 볼록 다각형, 홀로노믹, 위치·θ·ω_max·희망 속도·희망 방향(TR 3절, ICRA III). 실험은 길이:폭 약 1.83 직사각형.
- 회전: VO는 HZD와 같은 민코프스키 합 G_a⊕G_b를 시간 지평 τ로 절단한 ORCA 반평면. 회전이 결정 변수이고 Δθ_max = τω_max는 지평 τ 동안의 상한이다(TR 알고리즘 2의 2행). 실제 스텝당 회전은 ω_max·Δt로 제한되어, 고른 방향에 한 프레임에 못 닿으면 최대 각속도로 돈다(ICRA IV-B). 후보 Δθ_a는 Δθ_max/(2δ) 간격(TR), ICRA는 이웃 방향 δ개로 서술(판본마다 다름). 이웃은 '나의 후보 회전량 이하'로만 돈다고 가정(정의 5): 이웃 표본마다 제자리 겹침 검사 → 안 겹치면 ORCA 제약 추가, 처음 겹치면 그 방향 탐색 중단. 연속 쓸어낸 영역 대신 표본 근사(TR 5.1, 그림 4). TR 의사코드 9행(±Δθ_max)과 본문(±Δθ_a) 불일치.
- 선택: TR은 최소 부호 |u|가 가장 큰 방향 → LP(알고리즘 2 18~24행). ICRA도 max-min u로 가지치기한 LP만 풀고 완화 없음 > 몸이 속도 방향을 봄 > 희망 속도 근접 순 결합 순위(IV-B). 정면 교착 대책: 투영점이 상대 속도와 공선이면 배제(TR 알고리즘 3 10행, 그림 3(b)). 저자는 이 규칙만으로는 같은 우회 방향 보장이 없어 교착을 못 막고 회전과 함께 써야 효과적이라 함(TR 5.1 끝). ICRA에서도 δ>0이 δ=0(규칙만)보다 완료율 유의하게 높음(Lines p<0.005, Circle p<0.001, V-C-2). 충돌 중엔 최근접점 거리로 탈출 제약(TR 5.3).
- 비용·결과: C++(RVO2 개조, OpenMP 유지), i5-2520M(논문 표기 quad-core, 일반 사양 2코어 4스레드 [미확인])·4GB, 50개, PAPI, 렌더 제외(ICRA V-B·그림 7). FPS(δ=0/2/4/6/8/10) Lines 1053.1/325.4/158.1/91.6/59.5/41.7, Circle 1610.1/515.5/288.9/185.9/123.6/86.6. Lines 완료율 δ=2 약 96% 대 ORCA 44%, Circle ORCA 96% 대 δ=0 98%, 모든 δ에서 완료가 빠름, Circle δ=8·10 충돌 유의 증가(33회, Welch t). TR 표 1 평균 완료 프레임 δ=2 6261.71·4 5232.62·8 5554.58·16 6526.27·32 5643.98, RVO는 시행 완료 0(20,000프레임 상한), 정규화 FPS 0.1484~0.0025. TR(시행 전원 도착)과 ICRA(도착 에이전트 비율)는 지표가 달라 비교 불가. 에이전트당 [추론: 1/FPS/50] δ=0 12~19µs, δ=2 39~62µs, δ=10 231~480µs — 4스레드 벽시계라 단일 스레드는 최대 스레드 수 배까지 커질 수 있어 하한. τ·스텝·ω_max·몸 크기 미기재.
- 한계: δ가 작으면 낙관(관통). 회전·병진 동시 구간 쓸어냄 미검사, 상호 가정은 같은 규칙 집단만 [추론]. 회전 장애물·지형·전역 경로 없음(TR 5.2). Torso Crowds가 비스듬한 미끄러짐을 비판(2.7.1).
- 적합: 전원 적용 낮음(δ² 비용, 홀로노믹 + 독립 방향이라 M_arc와 충돌). 부분 차용은 4.1·4.2.

#### 2.1.3 Huang 외 2023, 다면체 VO [원문] [코드] [W5, W6]
- 서지: Huang·Zeng(공동 1저자)·Chi·Sreenath·Liu·Su, IEEE RA-L 2023 승인(arXiv 주석), v2 2024-06-10. 8(6):3502–3509는 [2차].
- 몸: 2D 볼록 다면체, 꼭짓점이 관측 상태(II-A). 실험 1.0×0.6 m·0.8×0.5 m, 여유 0.15 m.
- 회전: 없음. 현재 꼭짓점으로만 VO, 자기·이웃 회전 예측 없음(III-A). 외바퀴형 변환 v=‖v‖cosς, ω=−ς/η(η=0.2 s)에서만 방향이 들어감. 여유 0.15 m의 원문 이유는 적분 오차 허용·최소 안전 거리(IV-A)이고 회전 흡수 해석은 [추론]. 코드는 여유를 중심→꼭짓점 방사 방향으로 늘림 [코드 `polygon_robot.py` 145~146행].
- 방법: 충돌 원뿔을 민코프스키 합으로 정의(II-B), 모든 꼭짓점 쌍 벡터 x축 각의 최대·최소가 좌우 경계(식 3), 원뿔 정점은 장애물 속도 v_O(RVO·HRVO는 정점만 이동, 식 6), 시간 절단 없음(τ는 탐지 거리 l에만, 식 4). 꼭짓점 쌍 차이 집합(직사각형끼리 16점)의 볼록 껍질이 O⊕(−R)이고 극값 각은 껍질 꼭짓점에서 나오므로 합 없이 접선만 구하는 셈 [추론]. 해가 없으면 φ/tc+‖v−v_pref‖ 최소(식 8), 식 7 희망 속도 부호는 오기로 보임. 코드: 현재 속도 ±0.5를 0.05 m/s 격자(약 400표본), 각도 폭 >π 분기, 겹치면 ±π/2 반평면 [코드].
- 결과: Python, i7-9750H, Δt 0.1 s, v_max 1.5 m/s, ω_max 1.0 rad/s, 무작위 8대 100회(표 III 완료/교착 %). 배율 1.0: VO_c 85·VO_p 77, RVO 95·95, HRVO_c 82·HRVO_p 94. 1.2: HRVO_p 84/11 대 _c 54/45, RVO_p 69/20 대 _c 33/66, VO_p 62 대 _c 50. 1.4: 최고 HRVO_p 42/35. 저자는 ≤1.0에서 다면체판이 약간 낮다(비고 3)고 하나 표에서 그런 것은 0.4~0.8과 1.0의 VO뿐. 계산 시간 미보고.
- 한계: 전역 계획기 없으면 정적 장애물 밀집 시 교착(비고 2), 8대 규모, ±π 경계 처리가 본문에 없음.
- 적합: 중상(C 구현 단순화, 4.1). '몸이 공간에 비해 클수록 원보다 낫다'는 근거. 대형에는 HZD와 같은 회전 약점.

#### 2.1.4 다각형 VO 계산 기초 [원문]·[추론] [W7, W8, W4]
- 볼록 m·n각형 민코프스키 합은 최대 m+n각형, O(m+n) → 직사각형 둘은 최대 8각형 [원문 W7](16은 꼭짓점 쌍 수).
- 합의 변 법선은 두 상자 축 4개뿐 → 그 속도의 TTC(= VO 포함 판정)는 4축 SAT 슬랩 진입·이탈 구간 교집합 [추론]. 근거 Eberly 등속 OBB 문서(W8)는 403 [미확인].
- 캡슐 둘의 합 = 둥글린 평행사변형(선형 4 + 2차 4 닫힌 식), 원 대 상자는 모서리 (√2−1)r 과대, 선분 대 상자 = 6각형·축 3 [추론]. 다각형 ORCA는 교착할 수 있음 [초록 W4].

### 2.2 타원·캡슐 ORCA

#### 2.2.1 ERVO — Best·Narang·Manocha, ICRA 2016 [원문] [W9]
- 몸: 타원 → 접선 다각형(m=100) 보수 근사. 회전: 자세 먼저 갱신, 그 자세로 속도(분리, V절). 표 K는 타원을 θi→θi+1(5° 칸)로 돌린 덮개라 칸 안 어떤 방향도 보수적으로 포함(IV절). ERVO-F는 방향을 바꾸는 에이전트에만 θi에서 ±δt×α_max만큼 쓸어낸 볼록 껍질 K′를 쓴다. δt는 시뮬레이션 시간 간격이라 쓸어냄은 지평 τ가 아니라 한 스텝 분량이다(IV절, 본문은 α_max를 최대 각가속도라 쓰나 그림 2 설명은 최대 각속도). 이것으로 회전 중 무충돌을 주장(V절, 그림 2). 2차원 LP가 실패한 에이전트는 방향을 유지하고 3차원 LP로 가장 안전한 속도(V절).
- 비용·결과: 5° 간격 72개 합 표(약 120MB)로 40배 가속, 이때 원 ORCA의 4~5배이지만 수천 마리 단일 코어 대화형, 표 없으면 두 자릿수 배 느림(표 I, 그림 6). 0.4~0.7 m 복도 회전 통과(원 ORCA 실패), 대척점 원 40% 빠름.
- 한계: 자세·속도 동시 최적화 아님, 자세 간격 선택 어려움(VII). 적합: 쓸어낸 껍질은 '회전 무시'를 메우는 가장 직접적 근거이나 스텝 단위(1/64초 × 90°/s ≈ 1.4°)라 작다. 직사각형·캡슐은 닫힌 식이라 표 불필요 [추론]. 회전각이 크면 원으로 수렴(4.3).

#### 2.2.2 Narang·Best·Manocha, J. Stat. Mech. 2017 [초록] [W10]
- ERVO 학술지 확장(DOI 10.1088/1742-5468/aa58ab). 방향을 속도에 연결해 선회·옆걸음·뒷걸음·어깨 비틀기, 수백 명 단일 코어 대화형. 회전 예측·ms [미확인]. CTMAT 6절이 회전 동작의 무충돌 보장 방법으로 이 논문(참고문헌 [33])을 인용한다 [원문 W11]. 적합: '방향 = 속도 연결' 규칙만 새 가치.

#### 2.2.3 CTMAT·MATRVO — Ma·Manocha·Wang, AAMAS 2018 [원문] [W11]
- 몸: 매체축 튜플(반경 다른 두 원 + 공통 접선) 묶음, 하나면 캡슐. 회전: 진행 방향 몸 폭(볼록 껍질 평행 접선 거리)을 360° 구간별 함수 표로 미리 계산해 틈 통과 최소 회전각을 찾고, 매 스텝 자세를 갱신한다(6절, 그림 8). 쓸어낸 튜플(그림 7)은 민코프스키 합 표가 모든 방향을 덮도록 튜플을 θi→θi+1(θE = π/36 = 5° 칸)로 돌린 덮개이고, 실행 중에는 θi ≤ θnow < θi+1로 찾는다(5절). 회전 동작의 무충돌 보장은 Narang 외 2017(2.2.2)의 방법을 쓴다고만 하고 세부는 저자 기술 보고서로 미룬다(6절 끝, 참고문헌 [33]·[51]). 즉 이 논문 자체의 쓸어냄은 표 칸 덮개 수준이다.
- 비용·결과: 튜플 쌍마다 반평면 → LP(실패 시 최소 침투 LP), 비용은 튜플 수 곱(식 5), 사전 계산 표가 실행 시간을 2배 개선(튜플 1개, 5절 끝), 사전 계산 시 ORCA의 ≤2배(튜플 1개, 7절). 대척점 50마리 거짓 양성 ORCA 47~60%, ERVO 31~46%, MATRVO 8.5~9.8%(표 1).
- 한계: ORCA 공통 함정(2.5.1). 적합: 정확도·비용 근거 최상, 사족은 몸통 + 머리·꼬리 튜플 2~3개 [추론].

#### 2.2.4 Lee·Jeon·Oh, 타원 홀로노믹 로봇 VO [2차]·[초록] [W12]
- Autonomous Robots 41(6):1347–1363, 온라인 2016-06-09(Crossref). 전신 Jeon·Lee, Electronics Letters 50(18):1279–1281, 2014(DOI 10.1049/el.2014.1592).
- 최소 면적 타원. AR 판은 타원 VO 밖 병진을 먼저 고르고 필요할 때 각속도를 고르는 조건부 구조로 보임 [2차]. EL 판은 회전 속도로 다음 스텝 VO를 점진 재형성 [초록]. RRVO(TR 회전 먼저, ICRA 결합)와 순서가 다르다. 본문 미열람, 적합 미판정.

### 2.3 쓸어낸 모양·연속 충돌 검출

#### 2.3.1 보수 쓸어낸 껍질 계열 [원문] [W9, W11, W4]
- ERVO-F는 한 시뮬레이션 스텝 각도(±δt×α_max, δt = 시뮬레이션 시간 간격)만큼 쓸어낸 볼록 껍질로 VO를 만든다(W9 IV·V절). 지평 τ 전체의 회전이 아니라 스텝 단위다.
- ERVO 표 K와 CTMAT 쓸어낸 튜플은 사전 계산 표의 각도 칸 하나(θi→θi+1, 5°)를 덮는 보수 근사다(W9 IV절, W11 5절·그림 7). CTMAT의 회전 무충돌은 Narang 2017에 위임(W11 6절).
- 360°면 원이라 원 ORCA가 특수 경우(W4). 'swept volume velocity obstacle'이라는 독립 계열은 못 찾음. M4는 이 계열이나 쓸어냄 각이 1.5초 지평 기준이라 문헌의 스텝 기준보다 훨씬 크다(4.3).

#### 2.3.2 보수적 전진·양방향 전진 TOI — Mirtich 1996 → Catto GDC 2013, Box2D [원문] [W13, W14]
- 판: 공식 v2.4.1(2019, MIT) `b2_time_of_impact.cpp`, v3(2023, MIT) `distance.c`, v2.3 이하 사본은 zlib.
- 몸: 2D 볼록 다각형·선분·점(+반경). 회전: 스텝 동안 중심 선형 + 각 선형(v2)/(cos, sin) nlerp(v3) = 등속 직선 + 등각속도 경로 위 첫 접촉. CA 상한 Δt = ‖d‖/((v2−v1)·d̂ + ‖ω1‖r1 + ‖ω2‖r2), r = 질량 중심~도형 점 최대 거리(슬라이드 14~15).
- 방법: GJK 최근접 특징 → 분리 함수 → 가장 깊은 점을 근 찾기로 밀어 올림(이분법과 할선법(v2 주석)·거짓 위치법(v3) 교대, 구간 유지). 바깥 루프 ≤20, 근 찾기 ≤50, target = max(slop, R−3·slop)(v3 R−slop), tolerance 0.25·slop.
- 비용·한계: 순수 CA는 근접 시 반복 폭증(수백 회, 슬라이드 12·17~20), 호출당 시간 미보고. 한 스텝 안 스치는 회전 충돌 누락(저자는 게임에 무방, 슬라이드 40), 반복 소진 시 실패(51).
- 적합: 높음(부품). CA는 회전 포함 TTC 하한이나 600×220은 r ≈ 319.5cm라 90°/s면 회전 항만 약 502cm/s(1/64초당 7.8cm)로 매우 보수적 [추론]. 양방향 전진은 무작위 없음·고정 상한이라 결정론적(4.2).

### 2.4 비홀로노믹·차량

#### 2.4.1 GVO — Wilkie·van den Berg·Manocha, IROS 2009 [원문] [W15]
- 원판, 제어 공간 VO: 제어 u를 일정 시간 유지한 궤적과 장애물 궤적의 최소 거리 < 반경 합이면 금지, 제어 n개 표본, 시간 상한 tlim(IV, Algorithm 1). 수치 거리라 여유 필요·확률적(VI). 3.2GHz에서 장애물 10~15개 넘으면 실시간 불가(그림 5).
- 적합: D_esc의 직계 개념(제어 표본 궤적). 표본 시각마다 OBB 검사로 바꾸면 회전 반영 [추론].

#### 2.4.2 NH-ORCA·B-ORCA·NH-TTC [원문]·[초록] [W16]
- NH-ORCA(DARS 2010) [원문]: 홀로노믹 궤적을 추적 오차 E 안에서 따라가게 하고 반경을 E만큼 키운 원 ORCA + 추적 가능 속도 집합을 볼록 다각형 LP 제약으로(1·3절). e-puck 14대 무충돌, 정지 로봇 접근 시 교착 가능(Remark 5).
- B-ORCA(ICRA 2012, 자전거 모형) [초록], NH-TTC(2019, 임의 운동 모형 TTC 비용 하위 경사, 에이전트당 '몇 ms') [초록]. 모두 원판.
- 적합: 반경 = 추적 오차, 도달 가능 속도로 후보 제한이 M_arc를 정당화 [추론].

#### 2.4.3 OBCA — Zhang·Liniger·Borrelli [원문] [W17]
- arXiv 1711.03449, 정식판 IEEE TCST 29(3):972–983, 2021. E(x)=R(x)B+t(x)(식 5b), 회전 행렬이 쌍대 제약(식 15) 안 → 시각 k마다 자세가 결정 변수. 표본 사이 회전 쓸어냄은 미보장 [추론, 제약이 각 k에만].
- 주차(표 2, 4.7×2 m, 2013 MBP i7 2.6GHz) 평균/최대 0.6046/2.7166·0.8682/3.9885 s, 부호 거리판 1.0344/4.4840·1.6703/6.7266 s, 쿼드콥터 평균 14.97/14.40 s(표 1).
- 한계: 비볼록 → 국소 최적·초기해 의존(5.4·6.3절, 7절은 초기화가 문제마다 다르다고만), 주차 예제는 Hybrid A* 초기해. 적합: 낮음. 오프라인 정답 궤적용(표본 ≤1/64초 또는 사후 TOI 검증).

#### 2.4.4 분산 다차량 — Firoozi·Ferranti·Zhang·Nejadnik·Borrelli 2020 [원문] [W18]
- 직사각형 4.5×1.8 m, A(z)는 R(ψ), 쌍대 제약 식 5a~5c, dmin 0.5 m. ψ가 NMPC 지평 상태(자전거, Δt 0.05 s, 지평 0.75 s, V-C5), 표본 시각만 [추론]. 차량별 NMPC(IPOPT)와 쌍별 QP(Gurobi) 교대·궤적 통신(Algorithm 1).
- 표 I(i7-7700HQ, MATLAB) 스텝당 중앙/분산: 2대 1.3851/0.1301, 3대 2.7680/0.1514, 4대 12.8331/0.1757 s(약 73배, 원문의 '두 자릿수 이상'은 인용 안 함).
- 한계: 덜 빽빽한 기동(V-C6), 시뮬레이션만(VI), 수렴 증명은 본문에 없음. 적합: 낮음. 쌍별 분리 법선 재사용 = SAT 분리축 캐시 [추론].

#### 2.4.5 다각형 인식 MPC — Kojima·Honda·Okuda·Suzuki, ITSC 2025 [원문·반박] [W19]
- 상태 [px, py, v, θ, δ](III-A)로 요가 MPC 상태, 다각형 '또는' 제약을 SVM·MSDE로 '그리고'로. IPOPT, Δt 0.2 s. 표 III SVM 평균 100/최악 511 ms, MSDE 54.0/224 ms. 실차 RC(V-F) SVM 3.19/97.8 s, MSDE 139/689 ms. 정적 장애물·차량 1대만(VI). 적합: 낮음, MSDE를 벌점 항으로 [추론].

#### 2.4.6 다면체 NCBF — Thirugnanam·Zeng·Sreenath, ACC 2022 [원문] [W20]
- 서지: ACC 2022 2239–2246쪽, DOI 10.23919/ACC53348.2022.9867246 [2차: arXiv 초록 페이지의 학술지 참조·DOI 칸, W20. 열람한 ar5iv 본문에는 학회명·쪽수 없음]. 아래 내용은 [원문]. L자 소파(1m×0.1m 팔 둘), 1 m 복도, |v|≤0.3 m/s, |ω|≤0.2 rad/s. 각속도가 리 미분으로 쌍대 거리 변화율에 1차 반영(식 10·13·18), 지평 없음.
- 표 I 거리 QP 1.07 ms, NCBF-QP 14.5 ms, 합 평균 21.1/최대 41.6 ms, 50 Hz(IPOPT, MATLAB, 2.2GHz i7 가상 머신).
- 한계: 모퉁이 교착(IV-B4), 겹치면 복귀 불가(IV-C4), 회전 중 도함수 불연속(IV-B3), 구 근사는 보수적(IV-C3). 적합: 낮음. 연속 회전 처리로도 모퉁이 교착이 남는다는 근거.

#### 2.4.7 VO 기반 CBF — Huang 외, arXiv 2503.00606(TCST 승인) [원문] [W21]
- 원형, 가속도 제어 외바퀴형, θ는 차축 앞 오프셋 점(식 3)과 상대 속도 항으로만. 방향 CLF + VO CBF를 MIQP, 장애물 M개면 3^M개 QP, 결정 네트워크로 축소.
- IV절(Python, i9-13900HX, Δt 0.05 s, 반경 0.3 m) 표 III 스텝당 평균(장애물 1/2/3): MIQP 63.1/173.3/477.8 ms, QPs 25.7/72.1/309.7, DecNet 9.6/10.4/10.8. 표 IV 600회: QPs 완료 91%·교착 3%, DecNet 85%·2%, HOCBF 86%·3%. 가정 1·2가 특정 조건에서 깨짐(비고 3).
- 적합: 낮음(ms 단위). 같은 저자 다면체 계열이 회전 쓸어냄으로 이어지지 않았다는 확인.

#### 2.4.8 곁가지 [초록]·[2차] [W22]
- Guthrie 외 ACC 2022(원 로봇 대 다면체 합 닫힌 식 바깥 근사, 자동차 4.8배·쿼드콥터 8.7배 가속, 회전 없음) [초록]. Boldrer 외 비홀로노믹 VO 피드백 선형화 [2차]. 학습 기반(Long 외 ICRA 2018, Sensors 2020)은 모양 모델 미확인·결정론 검증 곤란으로 제외 [초록].

### 2.5 TTC·효용 샘플링·맥락 조향

#### 2.5.1 RVO2·ORCA·HRVO 기준선과 함정 [원문]·[초록] [W23, W24]
- RVO2 [원문]: C++98, Apache-2.0, OpenMP. 정적 장애물은 반시계 다각형, `processObstacles()` 뒤 추가분 무시, 장애물 ORCA 선을 이웃 선보다 먼저. `linearProgram2/3`은 무작위 섞기 없이 인덱스 순서 → 이웃 순서가 결정적이면 결과 결정적. 이웃은 제곱 거리 삽입 정렬로 `maxNeighbors`에서 자름(장애물은 상한 없음). ORCA '수천 마리 몇 ms', HRVO 진동 없음 [초록].
- Sunshine-Hill(GAP3 19장 19.5~19.9) [원문]: 반평면이라 3마리만으로 가능 속도 0 → 선형 완화 '최악 중 최선', 통과 방향 고정 → 모퉁이에서 멀리 돌거나 멈춤, 벽 뒤 목표 문제(19.7.3: 원래 RVO 라이브러리는 최대 정지 거리 밖 정적 장애물을 무시, 다른 구현은 최대 TTC 밖 예측 충돌을 무시. 저자는 둘 다 임의적이라 정적 장애물과 에이전트 사이에 끼이면 충돌할 수 있다고 함). 실전은 금지 제약보다 TTC 벌점을 더한 효용식·통과 방향 변경 벌점·에너지 효용이 낫고, 애니메이션까지 넣은 자동 시험대를 권함.
- 몸·회전: 원, 없음. 적합: 기준선·결정론 근거(엔진 RVO는 00#D17로 금지).

#### 2.5.2 TTC 멱법칙과 실용판 [원문]·[초록] [W25, W26]
- Karamouzas 2014: E(τ) = k/τ²·e^(−τ/τ0), τ0 ≈ 3초, 거리만 쓰는 힘은 τ 의존 재현 못 함. Guy·Karamouzas(GAP2 19장 19.2.2~19.4.2): 원판 TTC 2차식, tH 예 3초, 힘 크기 (tH−τ)/τ 상한 20, 겹치면 그 스텝만 반경 축소, 이웃 찾기가 병목(감지 반경 + 격자 + 상한). RVO 합집합 밖 고르기는 무작위 표본(원래 RVO 라이브러리) 또는 임계점 전수(ClearPath·HRVO), ORCA는 선형 근사로 한 자릿수 배 가속하고, 과제약이면 먼 이웃 제약부터 버리는 선택지(그 스텝은 무충돌 보장 상실, 실제로는 짧은 경미 충돌)(19.4.2) [원문], ClearPath는 수십만 에이전트 몇 ms 주장 [초록].
- 회전 없음. OBB 쌍 TTC(2.1.4)로 τ만 바꾸면 모양 무관 [추론]. 적합: 잡몹 최적, 충돌 보장 없음.

#### 2.5.3 Detour 적응 샘플링 (엔진) [원문]
- 고정 패턴(0 속도 + 고리 nr × 분할 nd, 반 칸 엇갈림)을 깊이 d번 최적 표본 중심으로 좁힘, 무작위 없음, 상한 분할 32·고리 4. UE 품질 단계 표본 11/22/45/66(분할 5·5·7·7, 고리 2·2·2·3, 깊이 1·2·3·3), 벌점 희망·현재 속도·측면·ToI + 오른쪽 통과 편향. [E] `DetourObstacleAvoidance.h:82-99`, `...\Navmesh\Private\DetourCrowd\DetourObstacleAvoidance.cpp:556-592`, `...\AIModule\Private\Navigation\CrowdManager.cpp:182-207`
- 이웃 상한: HZD 5, Mass 충돌체 6, RVO2 `maxNeighbors`, TDGame 계획 8. 적합: D_esc 고정 패턴의 선례(원, 회전 없음).

#### 2.5.4 맥락 조향 — Fray, GAP2 18장 [원문] [W27]
- 관심·위험 방향 슬롯 map, 최저 위험보다 위험한 슬롯 가림, 남은 관심 최대 슬롯, 기울기 보간·이전 map 섞기. 무상태라 SIMD 쉬움(F1 출시, 4,000줄 감소, 차 폭 + '치마' 위험), 비용은 해상도 선형(18.1·18.3~18.7).
- 회전: 슬롯마다 '그 방향의 자기 OBB'로 위험을 쓰면 반영, 도달 불가 슬롯 가림 = 차량형 제약 [추론]. 상대 속도를 몰라 TTC 위험이면 속력 고리 1개 표본 VO [추론]. 적합: 매우 높음 [추론], 탑다운 공개 사례는 튜토리얼 수준만 찾음 [초록: gamedev.net 글은 존재만 확인, W27 둘째 URL].

### 2.6 다중 원판·볼록 껍질

#### 2.6.1 UE 5.8 Mass 알약 (엔진) [원문]
- 알약 충돌체를 전방 축 ±HalfLength 원 2개로, 충돌체 최대 6이라 알약 하나가 2칸, 후보 격자 결과 6×4=24, 회피 주체는 원(AgentRadius). [E] `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\AI\MassAI\Source\MassNavigation\Private\MassAvoidanceProcessors.cpp:50-52,833-885`, `...\Public\MassNavigationFragments.h:123-166`
- 회전: 꼬리 원이 휘둘리고 자세 고정 문제는 그대로 [추론]. 적합: 분리 보조(Mass 두뇌는 금지).

#### 2.6.2 다중 원 차량 근사 [2차] [W29]
- 원 3~5개, 3원이 5원보다 덮개 9.02% 넓음(검색 요약, 원문 미확인). ORCA와 결합하면 반평면 과다로 과제약 [추론]. 원판이 길쭉한 몸에 과대라는 점은 ERVO(I절)·CTMAT(1절) 공통 지적 [원문]. 적합: 주 표현으로는 낮음.

#### 2.6.3 AC 신디케이트 볼록 껍질 조향 — Martel, GAP3 15장 [원문] [W28]
- 마차를 여러 타원으로 두면 말·차체 사이 오목 틈에서 진동·끼임 → 볼록 껍질(15.2.3). 차체·말 OBB를 2D로 합쳐 오목 꼭짓점 제거, 사람 폭 절반만큼 키워 사람을 점으로, 직선 경로가 껍질과 교차하면 각도 차가 가장 작은 꼭짓점으로, 감지는 물리 엔진 AABB 쌍, 껍질은 게으른 평가·주변 캐시 공유. 움직이는 마차는 뒤로 지나가려는 경향을 넣으면 사람처럼 보인다. 외접 타원은 너무 멀리 돌았고, '거대 로봇 뱀이면 꼬리 위치 예측 필요'(15.2~15.6).
- 회전: 껍질 갱신만. 적합: 높음(작은 쪽이 대형을 비켜 갈 때, HZD 우선순위와 짝).

### 2.7 군중 이방성 몸

#### 2.7.1 Torso Crowds — Stüvel·Magnenat-Thalmann·Thalmann·van der Stappen·Egges [원문] [W30]
- IEEE TVCG 23(7):1823–1837, 2017, DOI 10.1109/TVCG.2016.2545670(Crossref). 열람본은 저자 원고(머리글 '… 2016'), 절 위치가 정식판과 다를 수 있음.
- 몸: 캡슐(선분 ℓ ⊕ 반경 r), 배치 (위치, 방향)(III).
- 회전(능동, IV-B): 위치 먼저, 자세 따로. c = 경로 첫 변 위 점에서 가장 가까운 캡슐까지의 최소 여유(대략 반틈), w = ℓ/2 + r(반폭). Tt = 0(c−r ≥ w) / arccos((c−r)/w) / π/2(c−r ≤ 0). To = Th ± Tt 중 몸통 법선과 목표의 각이 작은 쪽(사람: 90% 시간 90° 이내, 120° 초과 없음). PD 추종, 출발 시 제자리 회전. 쓸어냄 검사·충돌 응답 없음(IX).
- 회전(수동, V-C·V-D): 캡슐 거리 0.4r 안 가장 가까운 K=4, 멀어지는 상대 무시, 중심축을 상대 속도와 평행하게 하는 최소 회전의 합 + 수직으로 비킴, 부호가 같을 때만 자리 찾기 회전과 합산. 저자는 합산이 단순하다며 가장 위험한 1명 우선을 향후 과제로.
- 비용·결과: 공유 GVD 위 경로 점수(표 I wc 2.30, wg 1.41), 1 m²당 3명 미만이면 RVO2 원으로 전환. Python + VRONI, 단일 i7 4GHz, 44명 23 fps(VII-B). 위상적으로 같은 경로 85%(초록·VII-A). 평균 0.4 m/s는 선행 관찰(III).
- 한계: 고밀도·사람 전용, 수동 매개변수. II절에서 RRVO가 비스듬히 미끄러진다고 비판.
- 적합: 본체 낮음(GVD). 조각은 4.4·4.5(틈 각 식은 재유도·검증 전 사용 금지).

#### 2.7.2 Generalized, Dynamic Multi-agent Torso Crowds — Talukdar·Weiss [초록] [W31, W32]
- PACMCGIT 8(1) Article 4, 2025-05-22, DOI 10.1145/3728303. 2D 캡슐 + PBD 캡슐 동역학 새 제약, 원판은 바닥 투영 과대·회전 정보 없음, 희소~밀집에서 원판보다 낫다는 주장(수치 없음). 논문 초록엔 회전 미명시. 박사논문(Talukdar, 'Perceptual and geometric advances in crowd simulation', NJIT 2026-05-31) 초록에 병진·회전 회피 제약, 방향·어깨 정렬·공간 점유 면적(spatial footprint) 조정 [2차].
- 열람: ACM 403, NJIT는 Open Access 표기, 박사논문 PDF(29.1MB) 공개(미열람). 선행 Weiss 외 MIG 2017(DOI 10.1145/3136457.3136462, SCA 2017 포스터, C&G 2019 확장): 원판에 단거리·장거리 위치 제약 [2차, 공개 PDF 미열람].
- 적합: 고정 반복·SimulationId 순서면 결정론이 쉬워 `ApplySeparation()`을 '캡슐 겹침 해소 + 회전 보정'으로 바꾸는 후보 [추론], 본문 전 보류.

#### 2.7.3 FDS+Evac 역류 모형 — Heliövaara·Korhonen·Hostikka·Ehtamo [원문] [W33]
- 'Counterflow model for agent-based simulation of crowd dynamics', Building and Environment 48(2012) 89–100(저자 판). mkor10.pdf는 Korhonen 제1저자 Safety Science 투고 전 원고(2010-03-22). 'PED 2010' 판 [미확인].
- 몸: 겹친 원 3개(몸통 + 어깨 2), 성인 R_d 0.255±0.035 m, 비율 0.5882/0.3725/0.6275(표 1).
- 회전: 회전 운동방정식(관성, 접촉·사회력·구동 토크 + 무작위 토크). 식 (9) M = (I_z/τ_z)(Δφ·ω0/π − ω)는 풀면 스프링+감쇠(PD) 꼴이라 진동이 없으려면 ζ = ½√(π/(ω0τ_z)) ≥ 1, 곧 ω0τ_z ≤ π/4 [추론, 반박 검증자 유도·미검증]. 이전 스프링형은 진동해서 바꿈(식 9 뒤), mkor10 초록은 기여를 '감쇠 항'으로 밝힘. ω0 수치는 본문·표에 없음. 역류 시 어깨 먼저, τ_z 최소 0.05 s(표 2). 예측 없음.
- 방법·결과: 평균 초당 10회 직진·좌·우 3부채꼴(앞 3 m·옆 1.5 m) 이웃 점수, 사회력 위. IMO 시험 8은 100 m² 방 둘을 2×10 m 복도로 잇고 각 방 100명(4절), 5.3절은 100명 대 1명. 결과는 매개변수 정확값에 민감하지 않음(4절 끝). 연산 시간 없음.
- 적합: 중 [추론]. TDGame은 Mover 선회율 상한(1차 운동학)이라 이 진동 문제가 없다. 토크형이면 과감쇠 조건·무작위 제거·고정 주기 필요. 3부채꼴은 잡몹 최저 LOD 후보.

#### 2.7.4 Hughes·Ondřej·Dingliana, SCA 2014 [2차] [W34]
- DOI 10.2312/sca.20141128. 진행·몸 방향 분리로 옆걸음·뒷걸음 허용, 옆걸음은 주로 TTC가 짧은 복잡한 상황에 한정(2017 박사논문 초록 경유). 몸 모양·비용 [미확인](403).
- 적합: 'TTC 짧을 때만 옆걸음' 관문 규칙, 사족 클립 존재가 선결(설계 문서 MD-31) [추론].

#### 2.7.5 FbCrowd — Narang·Randhavane·Best·Manocha(UNC) [원문] [W35]
- 학회 [미확인](인용 최신 2016). 원판, 위치·방향을 골격 루트에 동기화(3.4).
- 회전(간접): 클립을 (속력, 선회율 ω_t, 옆걸음률)로 매개화, 옆걸음 0 클립을 v=(cos ω_t, sin ω_t)·v_f로 국소 속도 공간에 찍어 볼록 껍질 변을 반평면 제약(4.1, 식 2~3). 원문이 껍질이 DB 밖 속도(|ω_t|>90°)를 포함한다고 인정해 90° 초과 선회에 반평면을 동적 추가. 착지 반평면(4.2), 옆걸음 누적 시간 한도(4.5).
- 방법·비용: ORCA ∩ 동작 반평면 LP(4.4), 실패 시 3D LP·충돌까지 시간 순 우선(부록 9.4). C++, Xeon E5-1620 v3 4코어, 렌더 제외 40명+ 30 fps(대부분 동작 합성, 그림 6), 클립 19개, 언리얼 통합.
- 한계: 원판, 쓸어냄 없음, 껍질 근사라 도달 불가 속도가 남음. 적합: 개념 높음(4.4). 식 2의 극각을 계획 시간 선회각 τ·ω_max로 재정의해야 함.

#### 2.7.6 라야와 마지막 드래곤 군중 — Nghiem, SIGGRAPH 2021 Talks(Disney) [원문] [W36]
- DOI 10.1145/3450623.3464650, 2쪽. 타원체(공분산 계량 G). 현재 방향 G′=OᵀGO는 보이드 힘·감지 반경에 명시. PBD 충돌은 타원체 민코프스키 합 내부·외부 타원 추정(Kurzhanski·Varaiya 2002, 식 1·2)이며 회전 계량 적용 여부는 명시 없음. 회전 예측 없음.
- Houdini OpenCL 실제 샷 PBD 14~34 fps(개체 수 미기재), 이방성은 3×3 행렬 곱 몇 번 추가(2절 Discussion). 적합: 겹침 해소에 중, 회전 회피에 낮음.

### 2.8 게임 업계 실무

#### 2.8.1 HZD 계열 공개 자료 [W37]
- 육상 내비메시 크기별 4종 + 수영 + 탑승 = 6종, 플레이어 주변 실행 중 생성, 루트 본 워핑 [원문]. 2017 세션은 '크기 다양한 에이전트 동적 항법' 주제만(슬라이드 이미지) [초록], GDC 2018 25종+ 항법·애니 재작업 [초록].
- HFW: 비행·수영 기계 AI(2024-11-25) 회피·크기 언급 없음 [원문], GDC 2022 세션 8개에 항법·회피 없음 [원문], Oud 강연 개요는 애니 수준 [초록]. HZD는 방향 인식 VO이고 후속 회피 자료 없음.

#### 2.8.2 사전 계획 이동 회피 — Anguelov, GAP1 22장(Hitman: Absolution) [원문] [W38]
- ADL은 접지 때만 바뀌어 조향 회피와 충돌 → 경로를 따라 구를 2초 앞까지 밀어 충돌 탐지, 감속·대기 또는 경로 수정, 같은 방향 동료 거리 유지 감속. 진동 없음, 30마리 0.3~0.5 ms(PS3, 프레임 1~1.5%)(22.1~22.6).
- 회전: 경로 접선(구 대신 OBB면 반영 [추론]). 적합: 루트 모션 대형 보스에 중, 잡몹 낮음.

#### 2.8.3 Supreme Commander 2 — Emerson, GAP1 23장 [원문] [W39]
- 1 m 칸·10 m 섹터, 이동 유형별 비용장·포털, 대형용 '벽 쿠션', 섞인 선택은 가장 제한적인 유형(23.11), 유닛끼리 물리 밀기·초대형 로봇이 탱크 백 대를 밂(23.13). 모양 인식 회피 없음.
- 적합: 크기별 정적 팽창 = HZD 4종 결론. 밀기는 우선순위의 가장 싼 구현이나 결정론엔 자체 분리기 필요 [추론].

#### 2.8.4 0 A.D.(오픈소스 결정론 RTS) [원문: 포럼] [W40]
- 2010-04-23 설계 글: 이동 유닛은 크기와 무관하게 회전하지 않는 축 정렬 정사각형, 회전 정사각형은 돌 때마다 꼭짓점 집합이 무효라 배제, 배에는 부적합 자인. 건물만 회전 직사각형. 반크기 팽창 가시성 그래프 A*(최악 O(n³), 근거리). 우선권(4월 24일)·약 1초 대기(4월 28일)는 설계 토론 제안. 여유 폭 등급·진형 [미확인](pathfinder.pdf·trac 3538 접근 거부).
- 적합: '이동 유닛은 회전 불변 외피, 회전 상자는 정적만' 타협의 실무 근거 [추론].

#### 2.8.5 A* Pathfinding Project [원문] [W41]
- 포럼(2018-02-19 질문, 2:1·5:2 직사각형 차): 02-20 'RVO는 원형 가정', 02-22 답은 특정 영역 전용 회피를 따로 두는 편이 더 쉽다고 함. 문서(4.2.18): 속도를 빨리 못 바꾸는 차량에 부적합. 이 미들웨어에는 비원형 회피 없음.

#### 2.8.6 Age of Empires IV — Cheng, GDC 2022 [초록] [W42]
- 흐름장 + 계층 A* + 조향, 유닛 크기 처리(초록). 길쭉한 유닛 처리 [미확인]. 볼트 'free content' 꼬리표, 접근 등급 [미확인] → 1순위 열람 대상.

#### 2.8.7 사족·대형 동물 [W43]
- JWE(Powell, 2019-04-02) [원문]: 흐름 시뮬레이션 점 열 → 매끄러운 경로, 속력·곡률 범위를 애니 혼합으로 커버, 사족 다리 과신장 방지 정렬, 공룡끼리 회피는 다른 글로.
- RDR2 말(Kleanthous, GDC 2021) [2차: Game Developer 2021-11-18]: 선회 중심이 앞발, 느릴수록 선회각 작음, 옆 속도 혼합 축, 옆 이동으로 비켜 가기. 적합: '곡률 범위 = 클립 범위' 관행, 사족 회전 중심은 4.4.

#### 2.8.8 그 밖(대부분 음성) [W44]
- CoD 좀비(Treyarch, GDC 2024, Hoyt): 사람 크기 가정 엔진(반경 15인치 캡슐)에서 거대 AI는 같은 내비메시 + 반경 증가로 풀고 이후 NavPower로 감 [2차: 검색 요약]. MHW(CEDEC 2018): 충돌 편집기·행동 범위 내비메시, 선회 구현 없음 [원문 기사]. 디아블로 4·PoE·SC2 [미확인](디아블로 4는 팬 위키 수준, PoE는 팬 회고만, SC2는 gamedev.net 403). 토탈 워 '원기둥 충돌'은 사용자 주장 [2차]. Sonar Avoidance·ITS v2는 소개·판매 문구뿐 [2차]. 오픈 월드 차량 회피 강연 미발견.

### 2.9 정적 지형
- HZD: 정적 지형 무시, 좁은 게임은 내비메시 모서리 = 선분 VO(HRVO식) 권고 [원문 W1, `hzd_ch09.txt:335-351`]. RVO2: 정적 다각형 + `timeHorizonObst`, 장애물 선 우선 [원문 W23]. RRVO: 정적 장애물은 회전 안 하는 에이전트로 책임 전부, 회전 장애물은 미처리 [원문 W3 5.2].
- UE Mass: 벽 모서리 `FMassNavigationEdgesFragment` 최대 16(`MaxEdgesCount = 16`), ZoneGraph 차선 경계 [E] `...\MassNavigation\Public\Avoidance\MassAvoidanceFragments.h:202` [원문 W46]. Detour: `dtObstacleCircle` + `dtObstacleSegment` [E] `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Navmesh\Public\DetourCrowd\DetourObstacleAvoidance.h:28-37`.
- 교착 잔존(Huang 비고 2, NCBF IV-B4, Sunshine-Hill) → 크기별 통과는 경로 계층(HZD 4종, SupCom2 벽 쿠션, AC 정지 차량 내비 패치) [원문]·[추론].

## 3. 비교 표

결정론 적합 = 무작위·가변 반복 없이 고정 순서로 만들 수 있는가. TDGame 행은 설계 문서 4.3·4.8 벤치 측정.

| 방법 | 회전 처리 | 비용 근거 | 결정론 | 구현 | 탑다운 적합 |
|---|---|---|---|---|---|
| HZD 직사각형 VO | 현재 자세 + 회전 벌점 | 60µs/개체(PS4) | 높음 | 높음 | 중상(대형 소수) |
| RRVO | 이산 회전 결정 변수, 이웃 상호(낙관) | δ=2 39~62µs(4스레드 하한) | 중 | 매우 높음 | 낮음(미끄러짐) |
| Huang 다면체 VO | 없음 | 미보고 | 높음 | 낮음~중 | 중상(C 구현) |
| ERVO-F·CTMAT | ERVO-F 한 스텝 각도 쓸어낸 껍질 · CTMAT 표 각도 칸 덮개(회전 무충돌은 Narang 2017 위임) | ORCA 4~5배·≤2배 | 중 | 높음 | 중 |
| Box2D CA·TOI | 등각속도 스윕(스침 누락) | 반복 ≤20/50 | 높음 | 중 | 높음(부품) |
| GVO·NH-ORCA 계열 | 제어 표본·도달 가능 집합 | 장애물 10~15 한계(2009) | 중 | 높음 | 개념 |
| OBCA·NCBF·MPC·VOCBF | 표본 시각 자세 변수·1차 | ④ 평균 21ms~십수 초(1~4대, 최악 약 98초) · 원형 VOCBF 9.6~478ms | 낮음 | 매우 높음 | 낮음 |
| RVO2·Detour(원) | 없음 | 수천 마리 수 ms·표본 11~66 | 높음 | 낮음 | 기준선(금지) |
| TTC 멱법칙 | 없음 | 가장 쌈 | 높음 | 낮음 | 잡몹 최적 |
| 맥락 조향 | 슬롯 = 자세 | 해상도 선형 | 높음 | 낮음~중 | 매우 높음 |
| 다중 원판(Mass) | 없음(꼬리 휘둘림) | 원 수 배 | 높음 | 낮음 | 분리 보조 |
| AC 볼록 껍질 | 껍질 갱신 | 캐시 공유 | 높음 | 중 | 높음(작은 쪽) |
| Torso Crowds | 여유 → 목표 자세, PD | 44명 23 fps(Python) | 중 | 높음 | 조각만 |
| FDS+Evac | 회전 동역학(PD 꼴) | 미보고 | 중 | 중 | 조각만 |
| FbCrowd | 도달 가능 속도 반평면 | 40명+ 30 fps(합성 포함) | 높음(굽기) | 중 | 높음(개념) |
| 캡슐·타원 PBD | 위치 제약·현재 자세 계량 | 미확인·샷 14~34 fps | 높음 | 중 | 겹침 해소 |
| Anguelov 사전 계획 | 경로 접선 | 30마리 0.3~0.5ms(PS3) | 높음 | 중 | 보스 소수 |
| 업계 정적 팽창·밀기 | 회전 배제 | — | 높음 | 낮음 | 경로 계층 |
| TDGame C | 현재 자세(= HZD) | 4.5~5.8µs/마리·스텝 | 확인 | 중상 | 채택(하이에나) |
| TDGame D_esc | 자기 12표본 적분 + 이웃 선회 예측 | 10.7~20.6µs/마리·스텝 | 확인 | 중상 | 채택(대형) |
| TDGame M4 | 최대 선회 쓸어낸 상자(보수) | M2의 약 2배 | 높음 | 중 | 비채택(4.3) |

## 4. TDGame 설계에 주는 함의(설계 문서 대응, 변경은 제안만)

### 4.1 C(하이에나 직사각형 VO)
- HZD·Huang·RRVO δ=0과 같은 '현재 자세 민코프스키 원뿔'이다. 설계 문서 C-3(합 꼭짓점 접선, 겹치면 최소 겹침 축 반평면)은 Huang 코드의 겹침 처리와 같은 발상이다 [추론].
- [제안] 접선을 16개 꼭짓점 쌍 차이의 극값으로 구하되 Atan2 대신 기준선 외적 부호로 비교한다. 같은 원뿔을 결합 VO 외곽 구성·±π 감김 없이 얻는다. 계산 경로가 바뀌어 float 비트 일치는 보장되지 않으므로 기존 골든(56상태)과 명령 일치(파이썬과 1cm/s 이내 100%, 설계 문서 4.7-6) 대조로 검증하고, 결정론(같은 입력 2회·별도 프로세스 비트 일치)은 새 경로에서 따로 잰다.
- [제안] RRVO 정면 투영 배제는 비용이 거의 없다. 단독으로는 교착을 못 막는다는 원문 단서와 δ=0 결과 때문에 적합도는 중(측정 필요)이다 → 설계 문서 M3-18 (e)에 H1·H4·S7 교착 수 비교로 넣는 안.

### 4.2 D_esc(대형 회전 인지 샘플링)
- GVO(제어 표본 궤적)의 직계이고, 표본 시각 검사라 OBCA·Box2D TOI와 같은 '표본 사이 미검사' 약점이 있다.
- [확인] 프로토는 이웃을 32스텝 평균 속도로 등속 이동시키고, 방향은 평균 속도 쪽으로 이웃 선회율만큼 돌린다고 예측한다(20cm/s 미만은 회전 없음): `Tools/scratch/20261014_claude_elongated_avoidance/avoid_sim_game.py:297-301`, `avoid_sim.py:582-586`. RRVO가 상호 가정으로 다룬 이웃 회전 불확실성을 운동 모형으로 다루는 셈이다. 설계 문서 4.3 D_esc 절차(160행)에 이 사실이 없어 명시를 제안한다.
- [제안] 이웃 방향 원천을 '평균 속도 방향'에서 '이웃의 직전 스텝 의도(스냅샷)'로 바꾸면 같은 스텝 코드 결정론의 이점을 쓴다. 규칙이 다른 개체(플레이어)만 보수 처리. L1~L4 재측정 조건.
- 표본 사이 [추론 계산]: 하위 스텝 0.125초, 90°/s면 표본당 11.25°, 꼭짓점 호 처짐 R(1−cos(Δ/2)) ≈ 1.5cm로 작다. 병진은 첫 접촉 시각이 0.125초 단위로 양자화(상대 600cm/s면 75cm). [제안] 비용 상위 1~3 후보만 양방향 전진 TOI(고정 상한, 실패는 '위험')로 재검증하는 안을 비용과 함께 측정한다.
- 비용은 RRVO δ=2·HZD보다 작지만 하드웨어가 달라 순위만 참고 [추론].

### 4.3 M4(최대 선회량 쓸어낸 상자)
- 정의 [확인]: 슬랩 TTC에서 '현재 자세 ~ 후보 방향 또는 최대 선회(선회율×1.5초) 자세'를 덮도록 축별 지지 거리에 현 길이 2R·sin(Δ/2)를 더하고 외접원 반경으로 상한(`Docs/MonsterAI_CombatSim/measurements/elongated-avoidance/bench-results.md:30`). 문헌의 보수적 쓸어냄 계열(가장 거친 것은 CA)이고 RRVO와 반대 방향 근사다.
- 600×220(반길이 a = 300, 반폭 b = 110), 현재 자세에서 한쪽으로 Δ 선회할 때 폭 축 반길이(cm) [추론 계산, R = 319.5, 모서리 각 20.1°]. 기준 축이 열마다 다르다: M4와 '현재 축 정확값'은 현재 자세의 폭 축, '중간 자세 정확값'은 Δ/2 돈 중간 자세의 폭 축(그 자세 기준 ±Δ/2 쓸어냄)이다.

| Δ | M4(현재 축, b + 2R·sin(Δ/2), 상한 R) | 현재 축 정확값(구간 [0, Δ]의 지지 함수 최대 = a·sinΔ + b·cosΔ, Δ ≥ 69.9°면 R) | 중간 자세 정확값(= 쓸어낸 볼록 껍질 최소 반폭, a·sin(Δ/2) + b·cos(Δ/2)) | 호 처짐 R(1−cos(Δ/2)) |
|---|---|---|---|---|
| 10° | 165.7 | 160.4 | 135.7 | 1.2 |
| 22.5° | 234.7 | 216.4 | 166.4 | 6.1 |
| 45° | 319.5(상한) | 289.9 | 216.4 | 24.3 |
| 90° | 319.5 | 319.5 | 289.9 | — |
| 135° | 319.5 | 319.5 | 319.3 | — |

- 해석 [추론]: 같은 현재 축에서 보면 M4의 과대분은 10°·22.5°·45°에서 5.3·18.2·29.6cm로 작고, 중간 자세 열과의 차(30.0·68.3·103.1cm)는 대부분 기준 축 선택(중간 자세 OBB)에서 온다. M4 폭 축은 Δ ≥ 약 38°, 길이 축은 Δ ≥ 약 3.5°에서 상한에 닿아 반변 R 정사각형(외접원보다 큼)이 된다. 현재 축 정확값도 Δ ≥ 69.9°면 폭 축이 R이고, 135°(1.5초 × 90°/s)에서는 중간 자세 정확값도 외접원과 0.3cm 이내라, 시간을 버리는 쓸어냄은 어떤 식이든 원 VO로 퇴화한다. 시간을 유지하는 D_esc만 이를 피한다. 단 벤치에서 M4는 M3와 47/56(84%) 같은 명령이라(같은 파일 64행) 실제 보수성은 상태에 따라 다르다.
- 결론: M4 비채택 유지(근거 보강). [제안] 다시 쓸 경우 (a) 현 길이 가산 대신, 중간 자세(Δ/2 회전) OBB 축에서 지지 함수 구간 최대(폭 a·sin(Δ/2) + b·cos(Δ/2), 길이 a·cos(Δ/2) + b·sin(Δ/2), Δ/2 ≥ 20.1°면 길이 R) (b) 쓸어냄 각을 지평 기준(τ·ω_max)과 스텝 기준(ω_max·Δt) 중 무엇으로 할지 별도 결정하고 짧은 지평을 쓴다. 문헌의 보수 쓸어냄은 스텝 기준이다(ERVO-F K′ = ±δt×α_max, δt = 시뮬레이션 시간 간격, W9 IV절 [원문]; CTMAT 쓸어낸 튜플은 5° 표 칸 덮개, W11 5절 [원문]). 1/64초 × 90°/s ≈ 1.4°인데 M4의 1.5초 지평 쓸어냄은 135°로 약 96배 크다 [추론]. RRVO Δθ_max도 지평 상한이고 실제 회전은 스텝당 제한이다 (c) 표본 자세 볼록 껍질 + 호 처짐 여유 (d) 측정 기준선으로 CA 상한판.

### 4.4 M_arc(호 선회)와 운동 모델
- 문헌 다수가 회전을 운동 제약으로 다룬다(NH-ORCA 추적 가능 집합, FbCrowd 클립 반평면, JWE 곡률 = 클립 범위, Torso의 RRVO 미끄러짐 비판). M_arc ±20° 자르기는 그 가장 단순한 판이다 [추론].
- [제안] 대형 D_esc 후보를 FbCrowd식 도달 가능 속도 다각형(선회각 축 = τ·ω_max) 안으로 제한하면 표본이 줄고 '고른 뒤 못 도는 후보'가 준다. 껍질 근사라 도달 불가 속도가 남는다는 단서를 함께 적용.
- 통로 진입 판정 [추론, 미검증]: Torso 식을 그대로 뒤집으면 |α| ≤ asin((c−r)/w), 길쭉한 몸으로 재유도하면 |α| ≤ asin((c−r)/(ℓ/2))(틈에 수직인 반폭 (ℓ/2)|sinα| + r ≤ c, 몸 길이 전체의 최소 여유를 c로 쓰는 곧은 통로에서만). 예: 대형을 캡슐 r=110·ℓ=380으로 보면 400cm 복도(c=200)에서 |α| ≤ 약 28°. 원 식 자체가 근사(c 측정 위치에 따라 최대 r 어긋남)라 재유도·검증 전엔 M4·M_arc에 쓰지 않는다. 쓸어냄과는 무관.
- 회전 중심 [추론]: RDR2 말은 앞발 축으로 돈다. TDGame은 속도 구동 Mover라 회전 중심이 캡슐 중심(원점)으로 고정되어 지금은 해당 없다. 애니 주도 선회를 넣으면 발자국 예측과 시각이 어긋날 수 있다. Mover 선회율 상한은 1차 운동학이라 FDS+Evac의 진동 문제도 없다.

### 4.5 양보·경로 계층·결정론·업계 정렬
- [제안] 대형이 지나갈 때 하이에나·잡몹은 Torso 수동 규칙을 축약: 가까운 대형 K개(예 4), 멀어지는 상대 무시, 긴 축을 대형 속도와 평행하게 최소 회전 + 수직으로 비킴. HZD 위험 구역 양보(설계 문서 M3-24)와 짝이고 대형 외피는 AC처럼 한 번 계산해 공유.
- 정확한 방법도 모퉁이·밀집 교착을 보고하므로 경로 계층이 진입 자세를 예고해야 한다(설계 문서 4.4-3·MD-39와 같은 방향) [추론].
- 결정론: RVO2 인덱스 순 LP, Box2D 고정 상한, Detour 고정 패턴, FbCrowd 굽기, PBD 고정 순서는 맞고 FDS+Evac 무작위 토크·평균 10Hz, 원래 RVO 무작위 표본은 바꿔야 한다. 부동소수는 같은 컴파일러·명령 집합이면 결정적, 다른 컴파일러·플랫폼·FMA(융합 곱셈-덧셈)는 보장 어려움 [원문 W45].
- 업계에 회전 예측 회피 공개 사례가 없으므로 D_esc는 대형 소수에만, 나머지는 회전 불변 외피 + 우선순위 + 도달 가능 속도 제한이 관행과 맞다 [추론].

## 5. 찾지 못한 것·미확인

- Giese 석사 논문(https://oaktrust.library.tamu.edu/bitstream/1969.1/152671/1/GIESE-THESIS-2014.pdf) 403, RRVO 연구 페이지(https://parasollab.web.illinois.edu/research/rrvo/) 404, RRVO τ·스텝·ω_max·몸 크기·ORCA 절대 FPS 미기재, ICRA 그림 8·11 수치 추출 불가.
- Huang RA-L 최종판·계산 시간, Talukdar 본문·박사논문 PDF, Weiss MIG 2017 PDF, 타원 홀로노믹 VO 본문(Springer·Semantic Scholar 비공개), Narang J. Stat. Mech.·Hughes SCA 본문(IOP·403).
- Eberly 회전 OBB TOI·등속 OBB 문서(IITK 403), van Toll·Pettré CGF 2021 서베이(EG 403·HAL 차단), Stüvel 박사논문·MIG 2015, Torso Utrecht 판, 서울대 타원 비등방 로봇 학위 논문(https://s-space.snu.ac.kr/handle/10371/119250 'Loading...'만).
- 0 A.D. `pathfinder.pdf`·trac 3538, AoE IV 영상, RDR2·HFW Oud 강연 본문, SC2·토탈 워 공식 자료, GAP1 'Animation-Driven Locomotion with Locomotion Planning'(404), Sonar Avoidance 원문, 오픈 월드 차량 회피 강연.
- 수치 미확인: Box2D 호출당 시간, FDS+Evac ω0, FbCrowd 학회, 다중 원 9.02% 원문. 선분 에이전트 전용 ORCA, 'swept volume velocity obstacle' 독립 방법은 못 찾음.

## 6. 출처 목록

| ID | 출처 |
|---|---|
| W1 | HZD 9장 http://www.gameaipro.com/GameAIProOnlineEdition2021/GameAIProOnlineEdition2021_Chapter09_Obstacle_avoidance_for_robots_of_multiple_sizes_and_forms_in_Horizon_Zero_Dawn.pdf |
| W2 | RRVO ICRA https://zenodo.org/records/1271471 |
| W3 | RRVO TR https://parasollab.web.illinois.edu/assets/papers/agiese-tr13-009-2013.pdf |
| W4 | https://parasollab.web.illinois.edu/papers/Giese-rvo-2014/ , https://parasollab.web.illinois.edu/research/dynamic-obstacles/ |
| W5 | Huang 2023 https://arxiv.org/abs/2304.07954 , https://arxiv.org/pdf/2304.07954v2 , https://ar5iv.labs.arxiv.org/html/2304.07954 |
| W6 | https://github.com/HybridRobotics/vo-polytope |
| W7 | https://en.wikipedia.org/wiki/Minkowski_addition |
| W8 | https://www.cse.iitk.ac.in/users/amit/courses/RMP/presentations/dslamba/presentation/dynacoll.pdf , .../obbangv.pdf |
| W9 | ERVO http://gamma-web.iacs.umd.edu/EORCA/files/EORCA_ICRA16.pdf |
| W10 | https://gamma.umd.edu/publication/538/ , https://catalog.comses.net/publications/226719 |
| W11 | CTMAT https://gamma.cs.unc.edu/CTMAT/files/Yuexin_AAMAS_2018.pdf , https://arxiv.org/abs/1804.02512 |
| W12 | https://link.springer.com/doi/10.1007/s10514-016-9580-2 , https://api.crossref.org/works/10.1007/s10514-016-9580-2 |
| W13 | https://box2d.org/files/ErinCatto_ContinuousCollision_GDC2013.pdf |
| W14 | https://raw.githubusercontent.com/erincatto/box2d/v2.4.1/src/collision/b2_time_of_impact.cpp , https://raw.githubusercontent.com/erincatto/box2d/main/src/distance.c |
| W15 | GVO http://gamma-web.iacs.umd.edu/NHRVO/WilkieIROS09.pdf |
| W16 | https://la.disneyresearch.com/wp-content/uploads/alonsomora10dars_paper1.pdf , https://la.disneyresearch.com/wp-content/uploads/alonsomora12icra_paper1.pdf , https://arxiv.org/abs/1907.05945 |
| W17 | OBCA https://ar5iv.labs.arxiv.org/html/1711.03449 |
| W18 | https://ar5iv.labs.arxiv.org/html/2006.11492 |
| W19 | https://arxiv.org/abs/2505.04935 , https://arxiv.org/html/2505.04935 |
| W20 | https://ar5iv.labs.arxiv.org/html/2107.08360 , 서지 https://export.arxiv.org/abs/2107.08360 |
| W21 | https://arxiv.org/html/2503.00606v2 |
| W22 | https://arxiv.org/abs/2203.15977 , https://iris.unitn.it/bitstream/11572/286846/1/main_RRT_RVO1.pdf , https://ar5iv.arxiv.org/html/1709.10082 |
| W23 | https://github.com/snape/RVO2 (src/RVOSimulator.h, src/Agent.cc), https://gamma.cs.unc.edu/ORCA/ , https://gamma.cs.unc.edu/HRVO/ , https://github.com/snape/HRVO |
| W24 | http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter19_RVO_and_ORCA_How_They_Really_Work.pdf |
| W25 | https://ar5iv.labs.arxiv.org/html/1412.1082 , https://link.aps.org/doi/10.1103/PhysRevLett.113.238701 |
| W26 | http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter19_Guide_to_Anticipatory_Collision_Avoidance.pdf , https://diglib.eg.org/handle/10.2312/14974 |
| W27 | http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter18_Context_Steering_Behavior-Driven_Steering_at_the_Macro_Scale.pdf , https://gamedev.net/articles/programming/artificial-intelligence/smarter-steering-using-context-for-direction-r3149 |
| W28 | http://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter15_Steering_against_Complex_Vehicles_in_Assassin's_Creed_Syndicate.pdf |
| W29 | https://arxiv.org/pdf/2304.08252 , https://arxiv.org/pdf/2501.10636 |
| W30 | https://stuvel.eu/files/publication/IEEE-TVCG-Torso_crowds.pdf |
| W31 | https://researchwith.njit.edu/en/publications/generalized-dynamic-multi-agent-torso-crowds/ , https://digitalcommons.njit.edu/dissertations/1889 |
| W32 | https://www.math.ucla.edu/~cffjiang/research/crowd/weiss2017crowd.pdf |
| W33 | https://sarjaweb.vtt.fi/julkaisut/muut/2012/HeliovaaraKorhonen_BE_v2.pdf , https://sal.aalto.fi/publications/pdf-files/mkor10.pdf |
| W34 | https://diglib.eg.org/items/2888c14e-8072-42dc-b147-b06714324801/full |
| W35 | https://gamma.cs.unc.edu/pedvr/files/FbCrowd-main.pdf |
| W36 | https://media.disneyanimation.com/technology/publications/2021/mathematical-tricks-for-scalable-and-appealing-crowds-in-walt-disney-animation-studios-raya-and-the-last-dragon.pdf |
| W37 | https://www.gamedeveloper.com/design/behind-the-ai-of-horizon-zero-dawn-part-2- , https://www.guerrilla-games.com/read/the-ai-of-horizon-zero-dawn , https://www.guerrilla-games.com/read/guerrilla-at-gdc-2022 , https://www.guerrilla-games.com/read/building-the-ai-behaviors-for-flying-and-swimming-machines-for-horizon-forbidden-west , GDC 2018 https://gdconf.com/news/get-inside-look-ai-driving-horizon-zero-dawn-gdc-2018 |
| W38 | http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter22_Collision_Avoidance_for_Preplanned_Locomotion.pdf |
| W39 | http://www.gameaipro.com/GameAIPro/GameAIPro_Chapter23_Crowd_Pathfinding_and_Steering_Using_Flow_Field_Tiles.pdf |
| W40 | https://wildfiregames.com/forum/topic/13042-aom-like-pathfindingmovement/ |
| W41 | https://forum.arongranberg.com/t/non-circular-agent-local-avoidance/4895 , https://arongranberg.com/astar/documentation/4_2_18_c6e217b3/localavoidance.html |
| W42 | https://gdcvault.com/play/1027659/Pathing-in-Age-of-Empires |
| W43 | https://mcvuk.com/?p=30444 , https://www.gamedeveloper.com/design/rockstar-s-quest-for-the-ultimate-video-game-horse |
| W44 | https://www.activision.com/cdn/research/AndrewHoytGDC24_Presentation_ppt_Main_16-9.pdf , https://www.famitsu.com/news/201808/25162916.html , https://steamcommunity.com/app/364360/discussions/0/490125103628379627/?ctp=6 , 디아블로 4 https://diablo.fandom.com/wiki/Pathing_AI , PoE https://www.pathofexile.com/forum/view-thread/3410899 |
| W45 | https://gafferongames.com/post/floating_point_determinism/ , https://gafferongames.com/post/deterministic_lockstep/ |
| W46 | https://dev.epicgames.com/documentation/unreal-engine/mass-avoidance-overview-in-unreal-engine |

[E] 경로는 UE 5.8 `C:\Program Files\Epic Games\UE_5.8\Engine\` 아래이고 `...`는 같은 플러그인·모듈 루트다.

## 7. 검증 기록

- 갈래 3개(primary2 = RRVO·Huang 정독, academic = 학계, crowd-games = 군중·업계)의 카드를 독립 검증자가 원문 대조. 지적 33건(primary2 12·academic 10·crowd-games 11, major 10·minor 23)을 모두 반영, 거절 0건.
- 부모 세션 추가 확인: D_esc 이웃 회전 예측(primary2 미확인 항목)을 프로토 코드로 확인(4.2), M4 정의를 벤치 문서로 확인하고 쓸어냄 기하를 계산(4.3).
- primary2 반영: RRVO 비용은 4스레드 하한 · 투영 배제 단독 효과 제한·적합도 중 · τω_max는 지평 상한 · TR·ICRA 지표 차이 · ICRA max-min 가지치기·표본 수 서술 차이 · Huang 여유 이유·방사 확장 · 비고 3 대 표 III · 꼭짓점 쌍은 상위 집합 · Torso 85% [원문]·저자 원고·DOI · Talukdar 회전 출처 구분·공개본 · VOCBF 실험 수치 · 타원 VO 저자·'RRVO처럼' 삭제(2.1.2·2.1.3·2.2.4·2.4.7·2.7.1·2.7.2·4.1).
- academic 반영: Raya 14~34 fps·계량 적용 범위 · Kojima 전문 수치·정적 한정 · spatial footprint · Weiss MIG 2017 · OBCA 한계 위치·TCST 2021 · OBCA·Firoozi 표본 시각 한정 · Box2D 라이선스·판·근 찾기 명칭 · Torso 역식 분모·성립 조건 · Huang 원뿔 정점 v_O · Firoozi 절 위치·73배(2.1.3·2.3.2·2.4.3~2.4.5·2.7.2·2.7.6·4.4).
- crowd-games 반영: FDS 100명·'매우 큰 효과' 삭제 · 식 (9) PD 꼴·과감쇠 조건·무작위·10Hz · Torso c = 여유·근사·쓸어냄 무관 · RDR2 [2차]·2021·앞발 선회 · FbCrowd 껍질 근사·각도 재정의·부록 9.4 · HZD는 방향 인식, SC2 [미확인]·토탈 워 사용자 주장 · 0 A.D. 이동 유닛 크기 무관·날짜·제안 성격·진형 [미확인] · FDS 서지·mkor10·ω0 없음 · A* PP 근거 위치·일반화 축소 · AoE IV 접근 등급 · Torso '단순'·0.4 m/s(0절·2.7.1·2.7.3·2.7.5·2.8.1·2.8.4~2.8.8·4.4·4.5).
- 반영 범위 단서: Weiss 공개 PDF는 지적만 반영하고 열람하지 않아 [2차] 유지. FDS 과감쇠 조건은 검증자 유도라 [추론·미검증].

### 7.1 2차 재검증(2026-10-14, 지적 9건: major 2·minor 7, 전부 반영·거절 0)
- 원문 재열람(WebFetch 자동 저장 사본 → pypdf 텍스트): CTMAT arXiv 1804.02512v1 5절·6절·그림 7·참고문헌 [33]·[51], ERVO PDF IV·V절·그림 2, GAP3 19장 19.7.3, GAP2 19장 19.4.2. NCBF 서지는 arXiv 초록 페이지(export.arxiv.org/abs/2107.08360)의 학술지 참조·DOI 칸.
- 1(major) CTMAT·ERVO-F 회전: 원문대로 CTMAT 쓸어낸 튜플 = 표 각도 칸(θE = π/36) 덮개, 회전 무충돌은 Narang 2017 위임(6절 끝), ERVO-F K′ = ±δt×α_max(δt = 시뮬레이션 시간 간격)로 고침(0절 1·2.2.1~2.2.3·2.3.1·3절 표·4.3 (b)). 단서: ERVO IV절 본문은 α_max를 최대 각가속도라 쓰고 그림 2 설명은 최대 각속도라 쓴다. 지적의 최대 각속도 해석은 그림 2와 차원(각도 = 시간 × 각속도)에 맞아 그대로 따랐고, 본문 표기 차이를 2.2.1에 적었다. 1차 노트 2.3·2.4의 같은 오류는 Saved 노트라 고치지 않았다.
- 2(major) 예산: 0절 3을 03 근접 두 행 1.06~1.46ms/프레임 기준으로 바꾸고, 전원 적용 불가(1.56~7.03ms)·등급제 소수 적용을 적었다.
- 3 M4 표: '현재 축 정확값' 열을 더하고 열마다 기준 축을 밝혔다. 검산(파이썬, 구간 1만 등분 최대): 160.4·216.4·289.9·319.5·319.5, 과대분 5.3·18.2·29.6cm. (a)에 중간 자세 OBB 축과 길이 식을 적었다.
- 4 0절 2·5: ④를 '몸 기하는 정확, 회전은 표본 시각·1차'로 고치고 비용 범위를 표와 맞췄다(VOCBF는 원형이라 ④ 밖으로 분리, 상한은 OBCA 쿼드콥터 평균 14.97 s·Kojima 실차 최악 97.8 s를 반영해 '십수 초, 최악 약 98초'). D_esc 대 RRVO 비교에 [추론]을 붙였다.
- 5 인용: HZD 3건 → 1건(2.1.1 관통 빈도), Sunshine-Hill 2건 → 1건(2.5.1 선형 완화), A* PP 포럼 2건 → 1건. 설계 문서 3.1의 HZD 식 4 저자 평도 따옴표 없이 요약(설계 문서 HZD 인용은 관통 빈도 1건). 남은 작은따옴표는 논문·자료 제목, 우리 용어, 출처당 1회 인용이다.
- 6 누락: gamedev.net r3149(W27), Guy·Karamouzas 19.4.2(원문 재확인), Sunshine-Hill 벽 뒤 목표 대책(원문 19.7.3 재확인), AC AABB 쌍 감지·마차 뒤로 지나가기, CoD 15인치 캡슐·NavPower, CTMAT 사전 계산 2배(원문 5절 끝 재확인), Gaffer 결정론 락스텝, GDC 2018·디아블로 4·PoE·HRVO 코드·PRL URL을 보강하고 4행을 '요지와 출처를 옮겼다'로 낮췄다.
- 7 NCBF 서지: [2차]로 낮추고 DOI 10.23919/ACC53348.2022.9867246과 근거 URL을 붙였다.
- 8 설계 문서: 8절 색인 '(커밋)' → '(Docs, git 미추적·커밋 예정)', 7절 미검증에 D_esc 하위 스텝 사이 접촉 1줄 추가. 같은 8절 '측정(커밋 사본)' 줄의 `measurements/elongated-avoidance/`도 현재 git 미추적(??)이나 이번 수정분 밖이라 고치지 않았다.
- 9 4.1: '비트 대조' → '56상태 명령 일치(1cm/s 이내) 대조', 새 경로 결정론은 따로 잰다.

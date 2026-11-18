# 07. 게임 선례 조사: 플레이어에 반응하는 볼륨 안개·바람·연기

작성: 2026-11-12 / 읽기 전용 조사 보조 / 구현 없음
표기: [확인: URL] = 원문 페이지·문서·슬라이드를 직접 열어 읽음. [요약만: URL] = 검색 결과 요약만 봄. [추정] = 내 추론. "미확인" = 찾지 못함.
주의: 발표 원문(GDC Vault 유료 영상)은 열람 불가였다. 그래서 God of War, Returnal, Counter-Strike 2 는 2차 자료가 섞여 있다. 숫자는 출처와 조건을 같이 적었고, 조건이 없으면 "조건 미공개"라고 적었다.

---

## 0. 한 장 요약 (쉬운 말)

세 가지 질문에 대한 짧은 답이다.

(a) 탑다운 게임에서 "진짜 3D 시뮬레이션"을 쓴 선례: 이번 조사에서 못 찾았다(미확인). 가장 가까운 선례는 이소메트릭(비스듬한 위에서 내려보는) 시점 인디 게임 Ignitement 의 2D 격자 + 카메라 따라가기 방식이다. [확인: https://unity.com/blog/real-time-fluid-simulation-fire-vfx-ignitement-breakdown]

(b) "플레이어 주변만 3D 로 계산하고 먼 곳은 정적 안개": 일반적인 구조가 맞다. 다만 한 개의 기법이 아니라 두 겹이다. 안쪽은 플레이어를 따라다니는 작은 격자, 바깥은 카메라에 붙은 화면 기준 격자(froxel, 아래 설명) 또는 계산식 안개다.

(c) 칼·총알·폭발 같은 빠른 사건: 공통 요령은 "그 순간의 모양(구·원기둥·캡슐)을 격자에 한 번 밀어 넣는다(주입)"이다. 푸는 방식은 "어떤 속도로 밀어 넣어도 터지지 않는" 안정 풀이(semi-Lagrangian)를 쓴다. 파편은 입자를 격자에 찍어 넣는 방식이 있다. 반응이 늦는 문제(꼬리 끌림)는 시간 평균을 약하게 쓰는 것이 원인이자 해결 열쇠다.

용어 풀이:
- 격자 = 공간을 작은 칸으로 쪼갠 표. 칸마다 "안개 농도"와 "움직이는 속도"를 적는다.
- froxel(프록셀) = frustum(카메라가 보는 사다리꼴 공간) + voxel(3D 칸). 카메라 앞 공간을 화면 모양 그대로 쪼갠 3D 칸. 카메라가 어디를 보든 화면에 보이는 부분만 계산하므로, 1인칭·3인칭·탑다운 어느 시점이든 같은 방식으로 붙는다.
- semi-Lagrangian = 유체 풀이 방식. "이 칸의 값은 한 걸음 전에 어디서 왔나"를 거꾸로 따라가 가져온다. 시간 간격이 커도 숫자가 폭주하지 않는다(무조건 안정).

---

## 1. 사례별 정리

### 1-1. God of War (2018, Sony Santa Monica) 3D 바람 격자
- 시뮬레이션 차원: 3D GPU 유체 시뮬레이션. GDC 2019 "Wind Simulation in God of War"(Rupert Renard) 소개문이 "full 3D GPU fluid simulation"이라고 쓴다. [확인: https://gdcvault.com/play/1025972/Wind-Simulation-in-God-of] 같은 소개문은 격자 크기·비용을 적지 않으며 본 발표는 회원 전용이라 열람 불가.
- 격자 크기·범위: 제3자(mamoniem.com)가 GPU 캡처를 뜯어본 결과, 속도 격자 32x32x16(R16G16B16A16_FLOAT), 난류 격자 64x32x64(R10G10B10A2_UNORM), 한 칸 약 1m³. 메모리는 약 128KB와 512KB(글쓴이 계산). 글쓴이도 "소금 한 줌 감안하라"고 적었다. [확인: https://www.mamoniem.com/behind-the-pretty-frames-god-of-war/ (공식 아님, 역공학 분석)]
- 플레이어 중심 여부: 위 글은 중심 이동을 적지 않았다. 32m x 32m 크기에 1m 칸이므로 월드 전체를 덮을 수 없다는 점에서 플레이어 부근을 덮는 구조일 가능성이 높다 [추정].
- 풀이 단순화: 미공개. 압력 풀이를 했는지 여부도 미확인.
- 그리는 방법: 안개를 그리는 것이 아니라 바람 필드를 만들어 머리카락·입자·식생·천·소리·게임플레이가 읽는다(바람 계산이 프레임의 첫 컴퓨트 작업). [확인: mamoniem 글]
- 공개 비용: 미확인(어느 자료에도 ms 없음).
- 시점: 3인칭(어깨 뒤).
- 플레이어 주입: 별도 발표(Sean Feeley, "Interactive Wind and Vegetation")가 캐릭터 충돌·바람 모터/수신기를 다룬다고 소개되나 구체 방식은 소개문에 없다. mamoniem 글은 "Kratos 위치에 구를 두어 식생에 영향"이라는 발표 내용을 인용만 한다(2차). [확인: https://80.lv/articles/santa-monica-studio-showed-how-interactive-wind-and-vegetation-were-made-for-god-of-war (주제 목록만)] [요약만 수준: 구 주입]
- 우리 쪽 시사점: 칸 1m 의 매우 거친 3D 격자도 "움직이는 캐릭터가 힘을 느끼게" 하는 데는 쓰였다. 눈에 보이는 안개 질감은 이 격자가 아니라 다른 층이 맡는다 [추정].

### 1-2. Counter-Strike 2 (Valve, 2023) 반응형 연막
- 차원: 3D 복셀(칸) 채우기. 연막이 하나의 3D 볼륨 객체로서 수류탄·총알에 밀리고 지형 모양대로 공간을 채우며 빛에 반응한다. [요약만: Valve Developer Community wiki 검색 결과 https://developer.valvesoftware.com/wiki/CS2 (직접 열기는 HTTP 403)]
- 칸 채우기 방식: 커뮤니티 해설과 재현 글이 "복셀을 물 채우듯 흘러 채우는(flood fill) 방식"이라고 설명한다. 재현자(Crescent, Unity URP)는 "복셀 전용 flood 알고리즘 + 채워진 복셀 안에 입자 생성 + 단순한 레이마칭(광선 따라가기)"으로 만들었다. [확인: https://80.lv/articles/counter-strike-2-style-responsive-smoke-set-up-in-unity-urp (Valve 구현이 아니라 재현 글)]
- 격자 크기·비용·총알이 구멍을 내는 방식: Valve 공식 수치 미확인. 한 수류탄이 약 8,000 복셀이라는 수치는 교육 사이트 한 곳만의 주장이라 [요약만, 신뢰 낮음].
- 중요한 성격: 이 방식은 "유체 방정식 풀이"가 아니라 "경계가 있는 작은 볼륨 + 채우기 규칙"일 가능성이 높다 [추정, 근거: 재현 글들이 모두 flood fill 이라고 설명]. 연막 하나가 닫힌 공간에서 수 초 안에 정해진 모양으로 차는 것이 게임 규칙이기 때문에 가능한 단순화다.
- 시점: 1인칭.
- 우리 쪽 시사점: "연막 한 덩이"처럼 개수가 적고 범위가 제한된 연기에는 좋지만, 넓은 지면 안개가 칼질에 끊임없이 흩어지는 용도의 직접 선례는 아니다 [추정].

### 1-3. Returnal (Housemarque, 2021) 유체 + 볼륨 안개
- 차원: "플레이어 주위의 전역 유체 시뮬레이션". 반단계 이상의 3D 구조(복셀화·3D 인덱스 복셀 안개)가 문서에 나오므로 3D로 보인다 [추정, 격자 차원 수치는 비공개]. 
- 풀이: Jos Stam 의 Stable Fluids 계열 semi-Lagrangian 격자 풀이, "무조건 안정". 밀도는 이류(흐름을 따라 이동)하고 입자가 밀도를 더한다. 힘은 원기둥 같은 해석적 모양으로 주고 "겹치는 부피 비율"만큼 가중한다. 장애물은 부호 거리장(SDF, 표면까지 거리를 저장한 필드)과 애니메이션 몸체를 복셀화한 결과로 넣는다. 선택적으로 소용돌이(vorticity) 계산으로 curl noise 디테일을 더한다. [확인: https://housemarque.com/news/2021/9/15/returnal-vfx-breakdown]
- 안개: 아티스트가 놓은 안개 볼륨의 복셀이 바닥·벽 근처에서 농도를 더 내고 가장자리에서 옅어진다(주변에 맞춰 자동 적응). 안개는 전역 유체에 실려 이동하므로 바람·총알·플레이어 행동이 안개를 움직인다. 보스는 몸체를 복셀화해 안개를 뿜는다. [확인: 같은 페이지]
- 복셀화 방법: 단일 패스 GPU 복셀화(Eisemann & Décoret), 닫힌 메시 필요, 깊이 축 최대 256칸. [확인: 같은 페이지]
- 범위·격자 크기·ms: 미공개(페이지에 없음). GDC 2022 "Can We Do It with Particles?" 발표가 "실시간 유체로 구동되는 볼륨 안개"를 다룬다고 소개된다. [요약만: https://gdcvault.com/play/1027742/Visual-Effects-Summit-Can-We]
- 후속 증언: PlayStation 블로그의 Saros 글이 "Returnal 의 볼륨 안개는 이미 반응형이었지만 주파수가 낮고 강한 시간 필터 때문에 세밀함이 없었다"고 썼다. [요약만: https://blog.playstation.com/2026/06/17/the-vfx-tech-behind-saros-from-ngp-to-graphite/]
- 시점: 3인칭(약간 뒤 위). 
- 우리 쪽 시사점: 우리가 원하는 장면(바닥에서 더 짙은 안개, 총알·플레이어·보스가 안개를 흔듦)과 가장 많이 겹치는 선례다. 그러나 격자 크기·비용이 비공개라 숫자 근거로는 못 쓴다.

### 1-4. Ignitement (인디, Unity) 2D 유체, 카메라 따라가기
- 차원: 2D. 밀도 1024x1024(RGBA half), 속도 512x512(RG half), 온도 1024x1024. 위·아래 방향 움직임은 근사. 시점은 이소메트릭 스크린샷 설명이 있다. [확인: https://unity.com/blog/real-time-fluid-simulation-fire-vfx-ignitement-breakdown]
- 범위: 시뮬레이션 영역이 월드 고정이 아니라 카메라(플레이어)를 따라 움직여 작은 영역만 계산한다. 영역 이동 때 이음매·튐을 조심해야 한다고 적었다. [확인: 같은 페이지]
- 그리는 법: 연기·안개는 온도 값에서 셰이더로 파생, 불은 높이맵처럼 보이게 하는 시차 효과로 가짜 3D. 불씨 입자는 속도장을 읽는다. [확인]
- 주입: 매 프레임 "발생원과 장애물을 텍스처에 그려 넣는" 방식(코드에 RenderEmittersAndObstaclesToTexture). [확인]
- 비용: 후처리 몇 개 수준이라고만 서술, ms 미공개. 게임플레이용 데이터는 비동기로 CPU에 읽는다. [확인]
- 우리 쪽 시사점: 탑다운·이소메트릭에서 2D(또는 층이 적은 2.5D) + 플레이어 따라가기 영역이 실제 출시 사례로 존재한다. 단, 이 게임은 인디이고 지형 높낮이·물체 위로 넘는 안개를 다루는지는 글에 없다.

### 1-5. 카메라 절두체 격자(froxel) 안개의 원전과 후속
- Wronski 2014, Assassin's Creed IV (Ubisoft). 격자 160x90x64 또는 160x90x128(플랫폼에 따라). 화면 가로세로는 정규화 좌표, 깊이는 지수 분포(가까운 곳을 촘촘히). 효과 범위는 50~128m 사이로 설정. 해상도가 낮아도 3D 보간으로 가장자리 번짐이 없다고 설명. 비용은 약 1.1ms, 해상도를 두 배로 하면 1.6ms(플랫폼은 추출 텍스트에 명시 없음, 당시 콘솔 [추정]). 밀도는 바람으로 움직이는 Perlin noise 한 옥타브 + 지면 쪽 농도 증가. 후속 확장으로 시간 재투영(reprojection)·지터를 소개하며 "3D 에서는 2D 보다 재투영이 쉽다"고 설명. 밀도를 입자 시스템·힘 필드로 동적으로 넣을 수 있다고 언급(구현 아님). [확인: https://www.advances.realtimerendering.com/s2014/wronski/bwronski_volumetric_fog_siggraph2014.pdf (PDF 내려받아 텍스트 추출)]
- Hillaire 2015, Frostbite (EA DICE). 기본 8x8 화면 타일 x 깊이 64장. PS4 900p 합계 8x8 타일 2.95ms, 16x16 타일 0.83ms(구간별: 재질 복셀화 0.45/0.15, 빛 산란 2.00/0.50, 누적 0.40/0.08ms). 입자 1,000개를 소멸 부피 격자에 찍는 비용: 기본 0.03ms, 고품질 0.25ms. 입자는 uint 격자에 InterlockedAdd(여러 스레드가 안전하게 더하기)로 쓰고 Float16 격자로 변환한다. 시간 필터는 이전 프레임을 5% 비율로 섞는 지수 이동 평균이며 움직이는 재질에 꼬리 끌림이 생긴다고 인정한다. [확인: https://www.slideshare.net/DICEStudio/physically-based-and-unified-volumetric-rendering-in-frostbite (페이지 요약)]
- 이 기법은 "화면 기준" 격자라서 카메라에 맞춰 따라오고, 시뮬레이션(움직임 계산)은 하지 않는다. 안개가 밀리게 하려면 입자/주입으로 밀도를 바꿔 주는 별도 계산이 필요하다 [추정].
- The Last of Us Part II (Naughty Dog, SIGGRAPH 2020, Kovalovs): 새 볼륨 안개를 froxel 격자로 만들었고 격자 특유의 결함 줄이기·성능 예산 맞추기를 다룬다. 해상도·ms·입자 주입 방식은 슬라이드(약 1.2GB 파일)에 있을 것으로 보이나 열지 않았으므로 미확인. 대상 기종은 PlayStation 4. [확인(요약): https://history.siggraph.org/learning/volumetric-effects-of-the-last-of-us-part-two-by-kovalovs] [요약만: https://www.naughtydog.com/blog/naughty_dog_at_siggraph_2020]
- Remedy Northlight(Quantum Break 2016~): Quantum Break 에 "froxel 기반 참여 매질"이 이미 있었다. 2024 엔진 아키텍처 슬라이드에는 투명 물체 처리 설명 중 "froxel 밀도를 입자가 쓴다(Froxel density written by particles)"가 있다. 격자 크기·ms 는 없음. [확인: https://enginearchitecture.org/downloads/REAC_2024_Remedy.pdf (직접 내려받아 텍스트 검색)] Alan Wake 2 와의 직접 연결은 슬라이드에서 확인하지 못했다(Northlight 엔진 일반 기능으로만 서술).
- Red Dead Redemption 2: PC 옵션에 "가까운 볼륨 해상도"와 "먼 볼륨 해상도"가 따로 있고, GameGPU 시험은 가까운 해상도 쪽이 프레임에 약 21.9% 영향, 먼 쪽은 거의 0이라고 보고. froxel 여부와 격자 크기는 미확인. 가까운 곳과 먼 곳을 따로 둔다는 구조가 시사점이다. [요약만: https://en.gamegpu.com/test-gpu/action-fps-tps/red-dead-redemtion-2-obzor-i-sravnenie-graficheskikh-nastroek]

### 1-6. Ghost of Tsushima, Horizon Forbidden West 의 바람과 식생
- Ghost of Tsushima (Sucker Punch): GDC 2021 "Blowing from the West"(Bill Rockenbeck). 바람을 격자 유체가 아니라 "vorticle(보이지 않는 바람 입자)"을 입자들이 샘플링하는 방식으로 했고, 정확성을 양보하고 PS4 연산 예산의 작은 몫으로 많은 입자를 돌렸다는 요약. 지형 높이맵을 읽어 땅에 붙는다. [요약만: https://gdconf.com/news/see-how-ghost-tsushima’s-guiding-wind-came-life-gdc-2021, https://gamedeveloper.com/design/using-vorticles-to-simulate-wind-in-i-ghost-of-tsushima-i-] 차원은 격자 시뮬이 아님. 풀 단위 반응(칼날)은 별도 발표(Procedural Grass)에서 다루며 내용 미확인.
- Horizon Forbidden West (Guerrilla): 식생은 "전역 바람 힘 필드"로 구동. 플레이어·기계에 반응시키려 했으나 하드웨어와 렌더 예산 때문에 주로 바람에만 반응하도록 줄였다는 GDC 요약. [요약만: https://www.neogaf.com/threads/decima-appreciation-thread.1525464/] 필드의 형태(격자인지)는 미확인.

### 1-7. 탑다운 액션 RPG (Diablo IV, Path of Exile 2, Lost Ark, V Rising)
- 안개·연기가 플레이어·칼질에 반응하는 기술 해설: 못 찾음(미확인). 검색 결과에 기술 글이 없었다. 발표 자료가 공개되지 않았을 가능성이 높다 [추정]. 이 네 게임에 대해서는 "무엇을 했는지 모른다"가 정직한 답이다.

### 1-8. 언리얼 엔진 쪽 (Niagara Fluids, 그리고 Hogwarts Legacy 등)
- Niagara Fluids 3D Gas: 격자는 "월드 크기 상자 + 최대 축 해상도"로 정하고, 입자 속성 읽기(Particle Attribute Reader)로 밀도·온도·속도를 주입하며, "Collide Against"로 액터와 충돌시킨다. 문서는 3D 시뮬이 2D 보다 메모리·GPU 시간이 더 들어 "주인공 효과·시네마틱에 적합하고 구워서 쓰기도 한다"고 명시한다. [확인: https://dev.epicgames.com/documentation/unreal-engine/niagara-fluids-reference-in-unreal-engine] 시뮬 상자를 플레이어에 붙여 따라다니게 하는 방법은 이 페이지에 없음(미확인).
- Hogwarts Legacy, Black Myth: Wukong, Hellblade II 의 안개 반응 기술: 공개 자료 못 찾음(미확인). 이 게임들이 Niagara Fluids 를 안개에 썼다는 증거도 없다.
- 블로그 한 곳이 64³ 3D 연기 비용(UE5.7.1)을 적었다고 하나 한 스튜디오의 미검증 수치라 쓰지 않는다. [요약만]

---

## 2. 결론 (a)(b)(c)

### (a) 탑다운 게임에서 3D 시뮬레이션을 쓴 선례가 있는가? 없다면 2D·2.5D 로 무엇을 했는가
- 이번 조사에서 탑다운 상용 게임이 안개를 3D 로 푼 선례는 못 찾았다(미확인). 위 Diablo IV 등은 자료 자체가 없다.
- 확인된 2D 선례: Ignitement. 이소메트릭, 2D 격자(밀도 1024², 속도 512²), 플레이어/카메라를 따라 이동하는 작은 영역, 불·연기를 높이맵과 시차로 3D 처럼 보이게 함. [확인]
- 확인된 다른 우회: 보여 주는 층과 계산하는 층을 나눈다. Wronski 안개는 계산(밀도 + 조명)이 아니고 움직임(이류)은 하지 않으며, 밀도는 바람으로 움직이는 노이즈였다. [확인]
- 탑다운에서 3D 가 덜 필요한 이유 [추정]: 카메라가 위에서 내려다보므로 안개 두께(높이)가 시각적으로 압축된다. 반면 우리 장면은 "상자·바위 위로 안개가 넘는다/막힌다"가 핵심이라 높이 정보는 필요하다. 그러므로 층 수가 적은(예: 몇 겹) 3D 가 후보이고 풀 3D 가 필수인지는 이번 선례만으로는 판단 불가.

### (b) 3인칭·1인칭에서 "플레이어 주변만 3D, 먼 곳은 정적 안개"는 일반적인가?
- 일반적이다. 근거는 세 층의 합이다.
  1. 근처만 도는 격자: God of War(32x32x16, 1m 칸이라 월드 전체를 덮을 수 없음 [확인(제3자)]), Returnal(플레이어 주위 전역 유체 [확인]), Ignitement(카메라 따라 이동 [확인]).
  2. 먼 곳은 화면 기준 격자 또는 계산식: AC4 froxel 50~128m [확인], Frostbite 8x8 타일/깊이 64 [확인], RDR2 가까운/먼 해상도 분리 옵션 [요약만].
  3. 먼 곳 안개는 시뮬이 아니라 노이즈 + 높이 감쇠 같은 정적 식: Wronski 의 밀도 [확인].
- 이음매(안쪽 격자 가장자리)의 처리: Returnal 은 안개 볼륨 가장자리에서 농도를 옅게 한다고 설명(볼륨 자체의 가장자리) [확인]. Ignitement 는 영역 이동 때 이음매와 튐에 주의하라고 적음 [확인]. 그 외 구체 기법은 미확인.
- 1인칭에서 어려운 점 [추정]: 카메라가 사물과 가까워서 격자 칸(1m 급)이 눈에 보이므로 "시뮬 격자가 아니라 시각 격자(froxel, 보간)"가 보여 주는 역할을 맡는 구조가 필요. God of War 의 1m 칸이 바람 용도였다는 점과 일치한다.

### (c) 칼·총알·폭발 같은 빠른 사건에 볼륨이 반응하도록 만든 요령
확인된 것:
- 해석적 모양으로 힘 주입: 원기둥 등의 모양을 만들고 격자 칸과 "겹치는 부피 비율"로 가중. 장애물은 부호 거리장과 복셀화한 몸체. [확인: Housemarque]
- 사건이 방사 충격(radial impulse)을 발생: 적의 점프 공격 착지 같은 게임플레이 사건이 잎과 불꽃을 흩뜨린다. [확인: Housemarque]
- 안정한 풀이: semi-Lagrangian("무조건 안정"). 큰 충격을 한 번에 줘도 터지지 않는 것이 핵심 이점. [확인]
- 입자 → 격자 주입: 입자를 격자에 한꺼번에 더하는 방식(InterlockedAdd, 1,000 입자 0.03ms @Frostbite) [확인], Niagara 의 Particle Attribute Reader 로 밀도·온도·속도 주입 [확인], Ignitement 는 발생원·장애물을 텍스처에 그려 넣음 [확인], Remedy 는 froxel 밀도를 입자가 쓴다 [확인].
- 반응 지연/꼬리 끌림: 시간 평균(Frostbite 5% 지수 이동 평균)은 움직이는 매질에 꼬리를 남긴다고 저자가 인정. Returnal 후속 글도 강한 시간 필터가 세부를 죽였다고 평가. [확인/요약만] 따라서 "빠른 사건은 시간 필터를 약하게 또는 끄고 주입 경로는 필터 밖으로" 둬야 한다 [추정].
- 상용에서 빠른 칼 궤적을 어떻게 끊김 없이 주입했는지(프레임 사이를 훑는 캡슐 등): 공개 자료 못 찾음(미확인). 칼의 이전~현재 위치를 이은 캡슐로 주입하면 터널링(빠른 물체가 칸을 건너뛰는 문제)을 막을 수 있다는 것은 내 추론 [추정].
- 파편: Returnal 보스 몸체 복셀화·장애물 SDF 가 가장 가까운 선례이며 날아다니는 파편이 안개를 미는 구체 사례는 미확인.

---

## 3. 선례 표

| 게임 | 차원 | 범위·격자 | 그리는 방법 | 시점 | 출처 |
|---|---|---|---|---|---|
| God of War (2018) | 3D (GPU 유체, 바람) | 속도 32x32x16, 난류 64x32x64, 1칸 약 1m (제3자 캡처 분석) | 바람 필드를 식생·입자·천이 읽음(안개 아님) | 3인칭 | gdcvault.com/play/1025972, mamoniem.com/behind-the-pretty-frames-god-of-war |
| Counter-Strike 2 (2023) | 3D 복셀 채우기 | 미공개(공식 수치 없음) | 복셀 flood fill + 입자 + 레이마칭(재현 글 기준) | 1인칭 | developer.valvesoftware.com/wiki/CS2 (요약), 80.lv CS2 Unity 재현 글 |
| Returnal (2021) | 3D 추정 (semi-Lagrangian 전역 유체) | 플레이어 주위, 격자·범위 비공개 | 유체에 실린 복셀 안개, SDF·복셀화 장애물 | 3인칭 | housemarque.com/news/2021/9/15/returnal-vfx-breakdown |
| Ignitement (인디) | 2D | 밀도 1024², 속도 512², 카메라 따라가기 | 온도·밀도에서 셰이더로 연기, 높이맵 시차 | 이소메트릭 | unity.com/blog/real-time-fluid-simulation-fire-vfx-ignitement-breakdown |
| Assassin's Creed IV (2013) | 3D 화면 기준 격자(froxel), 시뮬 아님 | 160x90x64 또는 x128, 50~128m, 지수 깊이 | 컴퓨트로 밀도·조명·산란 누적 후 화면에 적용, 약 1.1ms | 3인칭 | advances.realtimerendering.com/s2014/wronski |
| Frostbite (Battlefield 등) | 3D froxel | 8x8 타일, 깊이 64장, PS4 900p 합 2.95ms(16x16 타일 0.83ms) | 입자 복셀화(1k 입자 0.03ms) + 산란 + 시간 5% 혼합 | 1·3인칭 | slideshare.net/DICEStudio (Hillaire 2015) |
| The Last of Us Part II (2020) | 3D froxel | 해상도·ms 미확인, PS4 대상 | 새 볼륨 안개, 투명과 합성 | 3인칭 | history.siggraph.org (Kovalovs) |
| Quantum Break / Northlight | 3D froxel | 크기·ms 미공개 | froxel 밀도를 입자가 씀 (REAC 2024 슬라이드) | 3인칭 | enginearchitecture.org/downloads/REAC_2024_Remedy.pdf |
| Red Dead Redemption 2 | (froxel 여부 미확인) | 가까운/먼 볼륨 해상도 분리 옵션 | 근거리 해상도가 프레임 약 21.9% 영향(GameGPU) | 3인칭 | gamegpu.com (요약만) |
| Ghost of Tsushima | 격자 아님 (vorticle 입자 + 높이맵) | 미공개 | 바람 입자가 다른 입자를 밀음 | 3인칭 | gdconf.com, gamedeveloper.com (요약만) |
| Horizon Forbidden West | 전역 바람 힘 필드, 형태 미확인 | 미공개 | 식생이 바람에만 주로 반응(예산 이유) | 3인칭 | neogaf.com 요약 (요약만) |
| Diablo IV / Path of Exile 2 / Lost Ark / V Rising | 미확인 | 미확인 | 미확인 | 탑다운 | 기술 자료 못 찾음 |
| Niagara Fluids 3D Gas (엔진 기능) | 3D | 월드 크기 상자 + 최대 축 해상도 | 밀도 렌더, 입자 주입, 액터 충돌 | 시점 무관 | dev.epicgames.com/documentation/unreal-engine/niagara-fluids-reference-in-unreal-engine |

---

## 4. 이 단계에서 쓸 수 있는 시사점 (구현 아님, 추정 포함)

- 안개 "움직임 계산"과 "그리기"를 분리하는 것이 선례 대부분의 공통 구조다: 작은 격자가 움직임을 계산하고, 화면 기준 격자나 레이마칭이 그린다.
- 선례들의 계산 격자는 대체로 낮은 해상도다(God of War 1m 칸). 칼질 같은 세밀한 반응은 격자 해상도가 아니라 "주입 모양"과 "그리는 쪽 디테일 노이즈"로 낸다 [추정].
- 시점 3종 지원 전략 [추정]: 계산 격자는 "플레이어 중심 월드 고정 방향"(시점과 무관)으로 두고, 그리는 쪽만 시점별로 바꾸는 구조가 선례와 맞다. 계산은 시점과 독립, 그리기는 시점별.
- 가장 큰 공백: 탑다운에서 3D 계산 + 지형/물체 점유 반영 + 칼 궤적 주입을 한 번에 한 사례는 못 찾았다. 이 부분은 선례가 아니라 자체 시제품(프로토타입) 측정으로 확인해야 한다.
- 개발 PC(RTX 2060 6GB)에서의 비용을 직접 보여 주는 공개 선례 수치는 없다. 있는 숫자는 PS4 계열(Hillaire 2.95ms/0.83ms @900p) 뿐이며 같은 조건이 아니다.

## 5. 확인하지 못한 것 (정직 목록)
- God of War 발표 본문(격자 크기·압력 풀이 여부·플레이어 주입 방식): GDC Vault 회원 전용.
- Counter-Strike 2 의 Valve 공식 기술 설명, 격자 크기, 비용.
- Returnal 격자 크기·범위·ms (GDC 2022 영상 미시청).
- The Last of Us Part II 해상도·ms (슬라이드 1.2GB 미열람).
- Diablo IV, Path of Exile 2, Lost Ark, V Rising, Hogwarts Legacy, Black Myth: Wukong, Hellblade II 의 안개·연기 반응 기술.
- Alan Wake 2 의 froxel 격자 크기. (Remedy 2024 슬라이드의 froxel 기능이 Alan Wake 2 에 해당하는지 직접 연결 못 함.)

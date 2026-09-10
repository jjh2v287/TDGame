# 웹: 결정론적 전투 시뮬레이션·밸런스 툴 산업 사례와 언리얼 헤드리스 시뮬레이션 방법

작성일: 2026-09-09. 조사 방법: WebSearch + WebFetch 로 본문을 직접 열어 확인(열람 실패는 "미확인"으로 표기), 엔진 사실은 로컬 엔진 소스(`C:/Program Files/Epic Games/UE_5.8/Engine`, 이하 `Engine/`)를 grep/sed 로 확인해 파일:줄 번호를 붙였다. 약어는 처음 나올 때 풀어 쓴다.

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들)

1. **산업계 밸런스 툴은 두 부류다: "계산기형"과 "몬테카를로 시뮬레이터형".** Path of Building(패스 오브 엑자일 빌드 계산기)은 장비·패시브·조건 토글(저주 여부, 플라스크 사용 여부)을 입력으로 받아 DPS(초당 피해량)와 EHP(Effective Hit Pool, 유효 피격 허용량)를 수식으로 즉시 계산하는 결정론적 계산기다(pathofbuilding.net, 2026). 반면 하스스톤 전장 시뮬레이터(twanvl)와 Bob's Buddy(HSReplay)는 같은 전투를 1,000~10,000회 반복해 승/무/패 확률·평균/중앙값 피해·0~100% 백분위 분포를 낸다(twanvl GitHub; HSReplay 2020). TDGame 요구(플레이어 조건 × 몬스터 조합 → 승패·시간·피해)는 두 번째 부류이며, 첫 번째 부류의 "기대 DPS/EHP 요약치"를 사전 필터로 곁들이는 것이 좋다.
2. **결정론은 "같은 실행 파일·같은 CPU 명령 집합·같은 컴파일 옵션·단일 스레드 순서" 조건에서 부동소수점으로도 달성된다.** 다른 하드웨어·컴파일러·`/fp:fast`·FMA(Fused Multiply-Add, 곱셈-덧셈 융합 명령)·스레드 순서가 깨뜨린다(gafferongames 2010; gamedeveloper.com 2015). 고정소수점은 크로스 플랫폼 록스텝 멀티플레이 용도이며, 싱글 플레이 밸런스 툴(같은 PC, 같은 빌드)에는 불필요하다.
3. **언리얼 5.8 은 엔진 수준 고정 스텝·고정 시드 플래그를 이미 갖고 있다.** `-Deterministic` 은 `-UseFixedTimeStep -FixedSeed` 의 단축이며(`Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:2457-2462`), 고정 스텝이면 `FApp::SetDeltaTime(FixedDeltaTime)` 으로 논리 시간을 실제 시간과 무관하게 진행한다(`Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:3005-3024`). `-FPS=` 로 고정 델타를 지정할 수 있다(`LaunchEngineLoop.cpp:4726-4731`). Learning Agents 트레이너도 정확히 같은 API(`FApp::SetUseFixedTimeStep`, `FApp::SetFixedDeltaTime`, `t.MaxFPS=0`)로 "실시간보다 빠른 훈련"을 구현한다(`Engine/Plugins/Experimental/LearningAgents/Source/LearningAgentsTraining/Private/LearningAgentsTrainer.cpp:82-100`, 헤더 주석 `LearningAgentsTrainer.h:31-36`).
4. **가장 빠르고 결정론적인 실행 형태는 엔진 메인 루프를 거치지 않고 `UWorld::CreateWorld` + `World->Tick(LEVELTICK_All, 고정 Delta)` 를 직접 도는 것이며, 프로젝트 픽스처 FTDScopedCombatWorld 가 이미 이 형태다**(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:35,77`). 이를 자동화 테스트 실행기(`-ExecCmds="Automation RunTests ..." -nullrhi -unattended -nopause -nosound -testexit="Automation Test Queue Empty"`) 또는 커맨드렛 안에서 돌리면 렌더·입력·프레임 제한 없이 CPU 한계 속도로 돈다(`FApp::IsUnattended` 는 `Engine/Source/Runtime/Core/Private/Misc/App.cpp:274-279`; `-testexit=` 파싱은 `LaunchEngineLoop.cpp:1923`).
5. **병렬화는 프로세스 단위로 한다.** 언리얼 월드 하나를 여러 스레드에서 동시에 틱하는 것은 결정론과 안정성 모두 위험하고, 업계도 프로세스 병렬을 쓴다: Ubisoft For Honor 는 게임 인스턴스 5개 × 인스턴스당 10 매치 × 2배속(Ubisoft La Forge 2024), King 은 32 CPU 병렬 시뮬(Gudmundsson 2018), Gauntlet 은 `Parallel` 옵션으로 테스트를 동시 실행한다(`Engine/Source/Programs/AutomationTool/Gauntlet/Framework/Gauntlet.TestExecutor.cs:31,178,258`). 시드는 프로세스별로 분할(기본 시드 + 시나리오 인덱스)하면 결과 병합 순서와 무관하게 재현된다.
6. **결정론 검증은 "틱별 상태 해시 + 리플레이 재실행 비교"가 표준이다.** Supreme Commander 는 1초마다 전체 게임 상태를 해시(forrestthewoods 2011), OpenTTD 는 게임 로직 난수 생성기 상태를 체크섬으로 쓰고 `-d desync=2/3` 로 틱별 캐시 검증·주기적 스냅샷을 남긴다(OpenTTD desync.md). 흔한 원인: 미초기화 변수, 댕글링 포인터, 해시맵 순회 순서, 안정 정렬 미보장(Bugnet 2024~2025; forrestthewoods 2011).
7. **Chaos 물리는 결정론 옵션이 있으나 밸런스 시뮬에서는 물리 자체를 피하는 것이 안전하다.** `p.Chaos.Solver.Deterministic`(기본 -1=설정 따름, `Engine/Source/Runtime/Experimental/Chaos/Private/PBDRigidsSolver.cpp:345-346`)이 켜지면 병렬 충돌 검출 순서를 추가 정렬한다(`PBDRigidsEvolutionGBF.cpp:1316-1323`). 언리얼 기반 CARLA 를 분석한 논문은 "게임 엔진은 좀처럼 결정론적이지 않다"며 허용 분산 영역을 실측으로 찾는 방법을 제안한다(arXiv 2104.06262, 2021/2022). TDGame 전투 시뮬은 이동·피격 판정을 물리 엔진 대신 수학(거리·원/캡슐 겹침)으로 처리하는 편이 결정론 비용이 낮다.
8. **밸런스 목표는 "지표 밴드"로 정의하고, 파라미터 탐색은 시뮬 승률을 목적 함수로 돌린다.** Riot 은 평균 구간 승률 49~54.5%(밴율에 따라 52.5%까지 축소), 엘리트/프로는 밴율·프레즌스(픽+밴) 밴드를 쓴다(2019, 2020 갱신). Blizzard 하스스톤 아레나는 예측 승률을 직업 무관 50%에 맞추되 카드 가중치 변경을 ±30% 이내로 제약한 최적화를 돌린다(2018). 연구 쪽은 진화 알고리즘(Volz 2016), 강화학습 밸런싱 에이전트(Rupp, IEEE ToG 2024 / AIIDE 2025), 베이지안 최적화(AlphaGo 2018, Khajah 2016)로 승률 목표에 맞춘다.
9. **플레이어 대리 에이전트는 "규칙 봇 → 모방학습 봇 → 강화학습 봇" 순으로 올린다.** King 은 인간 플레이 데이터로 지도학습한 CNN 봇의 성공률을 이항 회귀로 사람 성공률에 사상했고, MCTS(Monte Carlo Tree Search, 몬테카를로 트리 탐색) 보다 상관이 높고 계산 시간은 일부였다(2018). EA SEED 는 모방학습 20분 vs 강화학습 5시간을 보고했다(SEED 2025). 규칙 봇은 결정론과 해석 가능성이 최고이므로 밸런스 툴 1차 대리 플레이어로 적합하며, "인간 성공률과의 사상(회귀)"은 실 플레이 데이터가 쌓인 뒤 붙인다.
10. **반복 횟수는 승률 표준오차로 정한다.** 이항 표준오차 sqrt(p(1-p)/n) 계산상 n=400 → ±2.5%p, n=1,000 → ±1.6%p, n=10,000 → ±0.5%p (p=0.5 기준, 산술 계산). 업계 실측치도 1,000(twanvl 기본), 10,000(Bob's Buddy) 사이다. 결정론 시뮬이면 "시드 i 의 결과는 고정"이므로 필요한 n 만큼 시드 배열을 늘리면 된다.

---

## 상세 조사

### 1) 게임 밸런스 시뮬레이션 산업 사례

#### 1-1. 근거 표

| 사례 | 입력 | 출력·통계 | 결정론·시드·반복 수 | 출처(URL, 연도) |
|---|---|---|---|---|
| Blizzard 하스스톤 아레나 밸런스 | 모든 아레나 게임의 카드 드로우·결과 데이터 | 머신러닝 모델이 "드로우된 카드 기준 승률" 예측; 카드 파워 버킷; 등장 가중치(2.0 = 2배 등장) | 목표: 직업 무관 예측 승률 50%; 가중치 변경 ±30% 제약, 제로섬 조정 | https://hearthstone.blizzard.com/en-us/news/22788308/developer-insights-arena-balance-through-science (2018-11-29) |
| Riot 챔피언 밸런스 프레임워크 | 티어별(평균/숙련/엘리트/프로) 승률·밴율·프레즌스 | 승률 밴드: 평균 49%~54.5%(밴율이 평균 밴율 ~7%의 5배면 52.5%), 숙련 49%~54%, 엘리트 밴율 45%/프레즌스 5%, 프로 프레즌스 90%(또는 연속 패치 80%)/5% | 프로는 패치당 약 200경기라 승률 대신 프레즌스 사용 | https://www.leagueoflegends.com/en-us/news/dev/dev-champion-balance-framework/ (2019-05-30); 갱신: https://www.leagueoflegends.com/en-us/news/dev/dev-balance-framework-update/ (2020-06-30, OP 밴드 0.5% 축소, 엘리트 상위 0.5%로 확장, 버프가 절반 넘으면 상위권 선제 너프) |
| King 캔디 크러시 인간형 봇 | 레벨 1~2,150 에서 레벨당 5,500 상태-행동 쌍(약 1.2×10^7 샘플) | 봇 성공률 → 이항 회귀 → 인간 성공률 예측; 800 레벨 학습, 200 레벨 예측; 지표 MAE, 95% 예측 밴드 이탈 비율 | CCS 는 비결정론 매치3; 봇 시뮬은 32 CPU 병렬; CNN 훈련 24시간(6 CPU + K80); MCTS 는 1,000 레벨이 한계(시간) | https://gwern.net/doc/reinforcement-learning/imitation-learning/2018-gudmundsson.pdf (IEEE CIG 2018) |
| King 현업 발언 | 신규 레벨 | "봇으로 대량 플레이를 시뮬해 출시 전 난이도 추정"; 봇은 이상치·통과율 예측에 강하나 "느낌"은 못 잡음 | — | https://www.pocketgamer.biz/crafting-candy-crushs-difficulty-blockers-level-design-ai-and-the-complexity-staircase/ (2026-01-16) |
| EA SEED 자동 테스트 | Battlefield V 601개 기능(수동 약 50만 시간) | 모방학습(IL) 20분 vs 강화학습(RL) 5시간 훈련; 호기심 기반 RL 로 상태 커버리지; MultiGAIL 로 여러 페르소나 | — | https://www.ea.com/seed/news/seed-ml-research-aaa-game-testing (IEEE CoG, 관련 뉴스 2025~2026); IL 논문 https://arxiv.org/abs/2208.07811 (2022); 커버리지 https://arxiv.org/abs/2103.13798 (2021); 배포 난점 https://arxiv.org/abs/2307.11105 (2023, BF2042/Dead Space) |
| Ubisoft La Forge For Honor | 1대1 결투 | 자기 대전(self-play) DRL 봇 | 인스턴스당 10매치 × 인스턴스 5개 × 2배속 = 동시 50매치 | https://www.ubisoft.com/en-us/studio/laforge/news/6cY2C5m3H5RkFAAsiPYpRh/... (2024-01-12) |
| Ubisoft Rainbow Six Siege 봇 | 실제 경기 리플레이(텔레메트리보다 밀도 높음) | 전통 AI 프레임워크 + ML 프레임워크 병행; 오프라인 훈련으로 공격/수비 승률 평가 가능성 언급 | — | https://news.ubisoft.com/en-au/article/1MlKnolSLJFuJDnATWiorr/how-rainbow-six-siege-developed-ai-that-acts-like-real-players (2023-02-21) |
| Path of Building (PoE) | 패시브 트리·장비·젬 링크·조건 토글 | DPS(타입별 (min+max)/2 합산, 치명타 = 기본 × [1 + 치명타율 × (배율−100)/100]), EHP | 결정론적 오프라인 계산기(시뮬 아님) | https://pathofbuilding.net/the-4-stages-of-path-of-building-pob/ (2026-07 갱신); EHP 오류 이슈 https://github.com/PathOfBuildingCommunity/PathOfBuilding/issues/8983 |
| twanvl 하스스톤 전장 시뮬레이터 | 텍스트 보드 정의(`board`/`vs`, `* [공/체] 이름, 버프`, `level`/`health`) | 승/무/패 %, 평균·중앙값 점수(피해 차), 0~100% 백분위 | 기본 1,000회, `run (n)` 로 조정; C++ 코어 + Emscripten 웹 | https://github.com/twanvl/hearthstone-battlegrounds-simulator |
| Bob's Buddy (HSReplay) | 실전 보드 | 승/무/패 %, 리썰/탈락 확률 | 전투당 최대 10,000회, "눈 깜짝할 새"; 수만 라운드 실전 결과와 y=x 비교로 검증 | https://articles.hsreplay.net/2020/04/24/introducing-bobs-buddy/ (2020-04-24) |
| D3Planner / Last Epoch Tools / Grim Tools | 장비·스킬·패시브 | 스탯 계산기(빌드 비교·공유) | 내부 계산 방식 본문 미확인 | https://maxroll.gg/d3/d3planner, https://www.lastepochtools.com/planner/, https://www.grimtools.com/calc/ (모두 Dammitt 제작, 랜딩 페이지만 확인) |

#### 1-2. 시뮬레이터형 입력 서식 발췌 (twanvl, 텍스트 기반 시나리오 정의의 좋은 본보기)

```
board
* 4/5 Cave Hydra, taunt
* 2/2 Rat Pack
level 6
health 30
vs
* 6/6 Security Rover
run 1000
```
(형식은 README 설명을 재구성한 예시. 미니언 지정은 인덱스 1~7, 이름, 종족, `all` 지원.)

#### 1-3. 해석
- **출력 통계의 공통분모**: 승/무/패 비율, 기대 피해(평균·중앙값), 분위수 분포. King 은 "성공률"을, Riot/Blizzard 는 "승률 밴드"를 목표 지표로 둔다. TDGame 은 "승률, 전투 시간(생존 시간) 분위수, 받은/준 피해, 물약 소모, 스킬 사용 횟수"를 기본 출력으로 두면 산업 관행과 맞는다.
- **대리 플레이어**: King 은 "인간 데이터로 학습한 봇"이 MCTS(강한 봇)보다 예측력이 높다는 것을 보였다. 즉 밸런스 툴에서는 "최강 봇"이 아니라 "사람처럼 실수하는 봇"이 유용하다. 초기에는 규칙 봇에 "반응 지연, 회피 확률" 같은 인간 파라미터를 넣는 것이 King 방식의 값싼 근사다.
- **검증 루프**: Bob's Buddy 는 시뮬 예측과 실전 결과의 y=x 산포로 정확도를 증명했다. TDGame 도 "시뮬 승률 vs 실제 플레이테스트 승률" 산포도를 툴의 채택 기준으로 삼아야 한다.

---

### 2) 언리얼 헤드리스·고속 시뮬레이션 방법

#### 2-1. 근거 표

| 방법 | 핵심 사실 | 근거 |
|---|---|---|
| 자동화 테스트 헤드리스 실행 | `UnrealEditor-Cmd.exe Proj.uproject -ExecCmds="Automation RunTests <필터>" -unattended -nopause -NullRHI -nosound -nosplash -testexit="Automation Test Queue Empty" -log -ReportExportPath=<경로>` | 공식 문서(5.8) 는 `-ExecCmds="Automation RunTest A+B;Quit"`, `-ReportExportPath`, `-ResumeRunTest` 를 명시: https://dev.epicgames.com/documentation/en-us/unreal-engine/run-automation-tests-in-unreal-engine ; CI 예시: https://www.emidee.net/ue4/2018/11/13/UE4-Unit-Tests-in-Jenkins.html (2018), https://blog.squareys.de/ue4-automation-tool/ ; 엔진: `-testexit=` 파싱 `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:1923`, `UNATTENDED` 파싱 `Engine/Source/Runtime/Core/Private/Misc/App.cpp:278` |
| NullRHI | `-nullrhi` 로 GPU 없이 실행(컨테이너에서도 가능) | https://unrealcontainers.com/docs/use-cases/machine-learning ; 엔진 `Engine/Source/Runtime/RHI/Private/DynamicRHI.cpp:60-78` (`InitNullRHI`, `GUsingNullRHI = true`) |
| 고정 델타·고정 시드 | `-Deterministic` = `-UseFixedTimeStep -FixedSeed`; `-BENCHMARK` 도 고정 시드·고정 스텝을 켬; `-FPS=X` 로 고정 델타 지정; 고정 스텝이면 논리 시간 = 누적 고정 델타 | `LaunchEngineLoop.cpp:2450-2462`, `4726-4731`; `Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:3005-3024`; 해설 블로그 https://unrealution.com/automation/reliable-automated-testing-with-deterministic-builds/ (연도 미표기) |
| 프레임 상한 해제 | `t.MaxFPS 0`, VSync 해제 | Learning Agents 트레이너가 훈련 중 자동 적용: `LearningAgentsTrainer.cpp:98-100`(MaxFPS 0), `104-108`(VSync) |
| Learning Agents "실시간보다 빠른 훈련" | `bUseFixedTimeStep=true`, `FixedTimeStepFrequency=60`, `bSetMaxPhysicsStepToFixedTimeStep=true`, `bDisableMaxFPS=true` | `Engine/Plugins/Experimental/LearningAgents/Source/LearningAgentsTraining/Public/LearningAgentsTrainer.h:31-52`; 적용 코드 `LearningAgentsTrainer.cpp:82-100`; 공식 "Headless Training & Network Snapshots (5.5)" 튜토리얼 https://dev.epicgames.com/community/learning/tutorials/DPDd/unreal-engine-headless-training-network-snapshots-5-5 (본문은 JS 렌더링이라 미확인, 제목만 확인) |
| 월드 직접 틱(엔진 루프 우회) | `UWorld::CreateWorld(EWorldType::Game, ...)` 후 `World->Tick(LEVELTICK_All, Delta)` 반복 | `Engine/Source/Runtime/Engine/Classes/Engine/World.h:3418` `UE_API void Tick( ELevelTick TickType, float DeltaSeconds );`; 프로젝트 픽스처 `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:35,77`, `TDDamageHomingTests.cpp:20,81`, `TDMeleeAttackNotifyTests.cpp:30,92` |
| 커맨드렛 | 게임/클라이언트 코드·레벨 없이 "raw" 환경에서 실행; 기본은 엔진 틱을 돌리지 않으므로 필요하면 직접 틱 루프를 감싼다 | https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCommandlet ; https://en.imzlp.com/posts/27475/ ; https://www.oneoddsock.com/2020/07/08/ue4-how-to-write-a-commandlet/ (2020) |
| 전용 서버 빌드 | `<Proj>Server.Target.cs` → `<Proj>Server.exe -log`; 렌더 없음 | https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-dedicated-servers-in-unreal-engine (5.8) |
| Gauntlet | RunUAT RunUnreal → TestExecutor 가 `Parallel` 옵션으로 동시 실행, 로그/크래시 파싱 유틸 | https://dev.epicgames.com/documentation/unreal-engine/gauntlet-automation-framework-overview-in-unreal-engine (5.8); `Engine/Source/Programs/AutomationTool/Gauntlet/Framework/Gauntlet.TestExecutor.cs:31 public int Parallel;`, `:178 if (Options.Parallel > 1)`, `:258 if (InProgressCount < Options.Parallel` |
| 프로세스 병렬 사례 | For Honor 5 인스턴스 × 10 매치 × 2배속; King 32 CPU 병렬 시뮬; Unreal-MAP 은 시간 팽창 + 다중 프로세스 | Ubisoft 2024(위 표); Gudmundsson 2018; https://arxiv.org/html/2503.15947v1 (2025) |
| CARLA(언리얼 기반) 고정 스텝 | 동기 모드 + `fixed_delta_seconds`(예 0.05); 물리 서브스텝 최대 10회, 0.01s; 결정론 재현 조건: 동기 모드+고정 델타, 월드 로드 전 설정, 반복마다 월드 재로드, `apply_batch_sync` | https://carla.readthedocs.io/en/latest/adv_synchrony_timestep/ |

#### 2-2. 코드·시그니처 발췌

```cpp
// Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:2457-2462
// "-Deterministic" is a shortcut for "-UseFixedTimeStep -FixedSeed"
bool bDeterministic = FParse::Param(FCommandLine::Get(), TEXT("Deterministic"));
FApp::SetUseFixedTimeStep(bDeterministic || FParse::Param(FCommandLine::Get(), TEXT("UseFixedTimeStep")));
FApp::bUseFixedSeed = bDeterministic || FApp::IsBenchmarking() || FParse::Param(FCommandLine::Get(), TEXT("FixedSeed"));
// :2476-2477  FMath::RandInit(Seed1); FMath::SRandInit(Seed2);

// Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:3015-3024 (UEngine::UpdateTimeAndHandleMaxTickRate)
if( bUseFixedTimeStep ) {
    bTimeWasManipulated = true;
    const float FrameRate = FApp::GetFixedDeltaTime();
    FApp::SetDeltaTime(FrameRate);
    FApp::SetCurrentTime(FApp::GetCurrentTime() + FApp::GetDeltaTime());
}
```

```cpp
// Engine/Plugins/Experimental/LearningAgents/.../LearningAgentsTrainer.cpp:82-100 (ApplyGameSettings)
FApp::SetUseFixedTimeStep(Settings.bUseFixedTimeStep);
FApp::SetFixedDeltaTime(1.0f / Settings.FixedTimeStepFrequency);
PhysicsSettings->MaxPhysicsDeltaTime = 1.0f / Settings.FixedTimeStepFrequency;   // bSetMaxPhysicsStepToFixedTimeStep
if (Settings.bDisableMaxFPS && MaxFPSCVar) { MaxFPSCVar->Set(0); }
```
헤더 주석(`LearningAgentsTrainer.h:31-34`): "fixed time step mode ... This can enable faster than real-time training if your game runs quickly."

#### 2-3. 해석
- 엔진은 "고정 스텝 + 프레임 상한 해제 + NullRHI" 조합으로 실시간보다 빠르게 도는 것을 공식적으로 지원하며(Learning Agents 가 그 소비자), 이 조합은 밸런스 시뮬에 그대로 쓸 수 있다.
- 다만 엔진 메인 루프(`FEngineLoop::Tick`)는 슬레이트·입력·오디오·GC 등 부수 작업을 매 프레임 수행한다. 프로젝트 픽스처처럼 **`UWorld::Tick` 을 직접 반복**하면 부수 작업이 빠져 더 빠르고, 프레임당 델타를 코드가 완전히 통제하므로 결정론에도 유리하다. 자동화 테스트 실행기 안에서 돌리면 위 커맨드라인 플래그(NullRHI/unattended)의 이점을 함께 얻는다.
- CARLA 문서의 "반복마다 월드 재로드" 조건은 "월드를 재사용하지 말고 시나리오마다 새 월드를 만들라"는 실무 규칙으로 옮길 수 있다. 픽스처가 이미 시나리오마다 `CreateWorld` 를 하므로 부합한다.

---

### 3) 결정론 확보 기법

#### 3-1. 근거 표

| 기법/사실 | 내용 | 근거(URL, 연도) |
|---|---|---|
| 부동소수점 결정론 조건 | "같은 명령 집합과 같은 컴파일러면 float 는 결정론적"(Gas Powered Games 인용). 깨는 요인: 최적화 수준/`/fp:fast`, x87 초월함수(fsin 등) 구현 차이, FMA(PowerPC vs Intel), D3D·프린터 드라이버·사운드 라이브러리의 FPU 모드 변경, CPU 속도 의존 반복 횟수, 대부분의 병렬화 | https://gafferongames.com/post/floating_point_determinism/ (2010-02-24) |
| 크로스 플랫폼 RTS | 프로세서마다 float 계산이 다름 → 64비트 정수 + 12비트 시프트 고정소수점으로 전부 교체(서드파티 물리·길찾기까지) | https://www.gamedeveloper.com/programming/cross-platform-rts-synchronization-and-floating-point-indeterminism (2015-01-07) |
| 상태 해시 검증 | SupCom: 1초마다 전체 게임 상태 해시, 불일치면 즉시 종료; 흔한 원인은 미초기화 변수·댕글링 포인터 | https://www.forrestthewoods.com/blog/synchronous_rts_engines_and_a_tale_of_desyncs/ (2011-07-09) |
| RNG 상태 체크섬 | OpenTTD: 게임 로직 난수 생성기 상태가 체크섬; 발산이 RNG 에 영향을 줄 때까지 검출 안 됨; `-d desync=2` 틱별 캐시 검증, `-d desync=3` 주기적 세이브 + 명령 로그로 재생 비교 | https://github.com/OpenTTD/OpenTTD/blob/master/docs/desync.md |
| 틱별 해시·계층 해시 | 매 틱 CRC32/XOR 해시 교환은 비용 무시 가능; 엔티티/서브시스템별 해시로 드릴다운; 리플레이는 입력만 저장, 재실행 후 전체 상태 덤프 비교; 원인: float 연산, 미초기화, 해시맵 순회, 불안정 정렬 | https://bugnet.io/blog/how-to-debug-desync-in-deterministic-lockstep-games |
| 언리얼 시드 난수 | `FRandomStream`: "Implements a thread-safe SRand based RNG"(하위 비트 품질 나쁨, `%` 금지); `Initialize(int32)`, `Reset()`, `GetInitialSeed()`, `GetCurrentSeed()`, 내부 `MutateSeed()` | `Engine/Source/Runtime/Core/Public/Math/RandomStream.h:15,19,63,92,97,166,362`; 문서 https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FRandomStream (5.8) |
| 엔진 전역 시드 | `-FixedSeed`/`-BENCHMARK`/`-Deterministic` 이면 `FMath::RandInit/SRandInit` 시드 고정 | `LaunchEngineLoop.cpp:2462-2477`; `Engine/Source/Runtime/Core/Public/Misc/App.h:834-835` |
| Chaos 결정론 | `p.Chaos.Solver.Deterministic` "Override determinism. 0: disabled; 1: enabled; -1: use config"; 켜면 병렬 충돌 검출 결과를 정렬하고 IslandManager 의 TSparseArray free-list 를 유지 | `Engine/Source/Runtime/Experimental/Chaos/Private/PBDRigidsSolver.cpp:345-346`; `Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp:1316-1323`; 요약 위키 https://indxzero.github.io/ue544cvarwiki/articles/p.chaos.solver.deterministic/ |
| 비동기 고정 물리 틱 | `UPhysicsSettings::bTickPhysicsAsync`, `AsyncFixedTimeStepSize`, `bSubstepping`, `MaxPhysicsDeltaTime` | `Engine/Source/Runtime/Engine/Classes/PhysicsEngine/PhysicsSettings.h:321-337` |
| 게임 엔진 결정론 실측 | 언리얼 기반 CARLA: "game engines are seldom deterministic"; 반복 실행 분산을 측정해 "허용 분산 영역"을 찾는 일반 방법 제안 | https://arxiv.org/abs/2104.06262 (2021, IEEE T-ITS 2022) |
| Chaos 크로스 플랫폼 | 플랫폼 간 자동 결정론 아님; float, 충돌 순서, 슬립, 스레딩, 초기 겹침, 네트워크 보정이 결과를 바꿈 | https://www.seeles.ai/resources/blogs/unreal-engine-chaos-physics-guide (연도 미표기, 2차 자료) |
| Mover 롤백 | "Network Prediction / Preferred Ticking Policy: Fixed" 로 렌더 프레임과 다른 고정 시뮬 레이트; 싱글플레이는 "Independent" 또는 `MoverStandaloneLiaisonComponent` 로 NPP(Network Prediction Plugin) 오버헤드 제거; Rollback Blackboard 로 롤백 시 데이터 복원 | `Engine/Plugins/Experimental/Mover/README.md:15-19,140,166,184,193,207`; 문서 https://dev.epicgames.com/documentation/en-us/unreal-engine/mover-in-unreal-engine (5.8, 결정론 세부는 README 참조하라고만 기술) |
| 네트워크 물리 예측 | `UNetworkPhysicsComponent` + Resimulation 모드, `FNetworkPhysicsData` 상속(ApplyData/BuildData); 저자는 UE 5.7+ 에서 이 방식이 deprecated 라고 표기 | https://vorixo.github.io/devtricks/phys-prediction-use/ (2024-05-04) |

#### 3-2. 시그니처 발췌

```cpp
// Engine/Source/Runtime/Core/Public/Math/RandomStream.h
struct FRandomStream {                 // :19  "Implements a thread-safe SRand based RNG." (:15)
    void Initialize( int32 InSeed )    // :63
    void Reset() const                 // :92
    int32 GetInitialSeed() const       // :97
    int32 GetCurrentSeed() const       // :166
    void MutateSeed() const            // :362
};
// Engine/Source/Runtime/Experimental/Chaos/Private/Chaos/PBDRigidsEvolutionGBF.cpp:1316-1323
void FPBDRigidsEvolutionGBF::SetIsDeterministic(const bool bInIsDeterministic) {
    // We detect collisions in parallel, so order is non-deterministic without additional processing
    CollisionConstraints.SetIsDeterministic(bInIsDeterministic);
    // IslandManager uses TSparseArray which requires free-list maintenance for determinism
    IslandManager.SetIsDeterministic(bInIsDeterministic);
}
```

#### 3-3. 해석 (TDGame 맥락)
- TDGame 은 싱글 플레이이고 밸런스 툴은 같은 PC·같은 빌드에서 돈다. 따라서 **고정소수점은 불필요**하고, 지켜야 할 것은 (a) 고정 델타, (b) 시나리오 시드에서 파생한 `FRandomStream` 만 사용(`FMath::RandRange`/`FMath::FRand` 전역 난수 금지 — 엔진·플러그인 내부 전역 난수 호출은 `-FixedSeed` 로 방어), (c) 액터 처리 순서 고정(정렬 키 = 스폰 순번), (d) 병렬 틱 그룹·비동기 태스크 결과를 게임 상태에 반영하지 않기, (e) `TMap`/`TSet` 순회 결과를 게임 로직에 쓰지 않기, (f) 틱별 상태 해시 기록이다.
- 언리얼 `TSparseArray` free-list 문제(Chaos 주석)는 프로젝트 코드에도 그대로 해당한다: 제거·재삽입이 잦은 컨테이너는 순회 순서가 이력에 의존한다.
- GAS(Gameplay Ability System) 의 `FGameplayEffectSpec` 은 스칼라 float 연산 위주라 같은 빌드에서 결정론적이지만, 예측 키·타이머(`FTimerManager`)는 월드 시간을 쓰므로 고정 델타로 `World->Tick` 하면 결정론이 유지된다(픽스처가 이미 이 방식으로 테스트를 통과시키고 있음).

---

### 4) 시뮬레이션 결과를 밸런스 조정에 연결하는 방법

#### 4-1. 근거 표

| 방법 | 내용 | 근거 |
|---|---|---|
| 목표 승률 밴드 (라이브 데이터) | Riot: 티어별 승률·밴율·프레즌스 밴드; 프로는 표본 부족(패치당 ~200경기)이라 승률 대신 프레즌스 | Riot 2019/2020 (1절 표) |
| 제약 최적화 (라이브 데이터) | Blizzard 아레나: 예측 승률 50% 목표, 가중치 변경 ±30%, 제로섬 | Blizzard 2018 |
| 진화 알고리즘 + 시뮬 목적 | Top Trumps 덱을 다목적 진화(승률·트릭 수)로 생성, 출판 덱 이상 성능 | https://arxiv.org/abs/1603.03795 (Volz et al. 2016) |
| 하스스톤 덱 진화 | EA(Evolutionary Algorithm, 진화 알고리즘) 로 덱 탐색, AI 대 인간 설계 덱 대전 | https://www.sciencedirect.com/science/article/abs/pii/S0950705118301953 (Knowledge-Based Systems 2018) |
| 강화학습 밸런싱 에이전트 | 생성기 + 밸런싱 에이전트 + 보상 시뮬 3부 구조; 목표 "모든 플레이어 동일 승률"; 스왑 표현으로 플레이 가능성 강건화 | https://arxiv.org/abs/2503.18748 (IEEE ToG 2024, arXiv 2025); 박사 컨소시엄 요약 https://ojs.aaai.org/index.php/AIIDE/article/view/36856 (AIIDE 2025, "시뮬레이션은 계산 비용이 크고 환경 특화라 이식이 어렵다", RQ3 "사람이 시뮬 밸런스를 어떻게 지각하는가") |
| 베이지안 최적화 | AlphaGo 하이퍼파라미터를 BO 로 반복 튜닝, 자기 대전 승률 50%→66.5%; Khajah 등은 Flappy Bird/Spring Ninja 의 파이프 간격 등을 BO 로 "자발적 플레이 시간" 최대화; MCTS 백프로파게이션 BO 는 승률 평가에 400 게임 사용(검색 요약, 본문 미확인) | https://arxiv.org/abs/1812.06855 (2018); Khajah CHI 2016 (ACM 403, 검색 요약만); https://arxiv.org/abs/2001.09325 (2020) |
| 몬테카를로 시뮬레이션 밸런싱 | Go 플레이아웃 정책 파라미터를 시뮬로 튜닝해 승률 69%→78%(Erica vs Fuego) | https://www.remi-coulom.fr/CG2010-Simulation-Balancing/SimulationBalancing.pdf (2010) |
| 비대칭 게임 자동 밸런싱 | 몬테카를로로 피해 전략을 시뮬·시각화하고 속성을 반복 조정해 근사 균형 | https://ieeexplore.ieee.org/document/7860432 (Beau & Bakkes, IEEE CIG 2016; 초록 본문 미확인, 검색 요약) |
| 인간형 대리 플레이어 | King: 인간 데이터 지도학습 봇 → 이항 회귀로 인간 성공률 사상; "봇과 사람은 난이도 증가에 대한 민감도가 다르고 선형이 아닐 수 있다"; 1년 이상 1,000개 신규 레벨에 안정적으로 사용 | Gudmundsson 2018 |
| 규칙 봇 vs 학습 봇 선택 | EA: IL 20분 vs RL 5시간; IL 은 ML 지식 없이 디자이너가 훈련; RL 은 커버리지·탐색에 강함; 배포 난점은 "개발 환경과 최종 제품의 차이" | SEED 2025; arXiv 2208.07811; 2307.11105 |
| 반복 수 경험칙 | 1,000회에서 안정화 시작, 10,000회가 정확도/속도 절충; 500회 넘으면 한계 이득 급감 | twanvl(1,000 기본), Bob's Buddy(10,000), 검색 요약(https://dev.to/euneua/building-a-risk-battle-simulator-when-board-games-meet-monte-carlo-59o6 등, 2차 자료) |

#### 4-2. 해석
- **탐색 알고리즘 선택 기준**: 파라미터가 5개 이하이고 평가가 싸면(결정론 시뮬 수천 회가 초 단위) 그리드/라틴 하이퍼큐브가 단순하고 재현 가능하다. 파라미터가 10개 이상이거나 평가가 비싸면 베이지안 최적화가 표본 효율이 좋다(AlphaGo 사례). 진화 알고리즘은 "덱/조합"처럼 이산 구조 탐색에 맞다(Volz, 하스스톤).
- **목적 함수는 "밴드 위반 벌점 + 곡선 오차"**로 두는 것이 Riot/Blizzard 관행과 맞는다: 예) 장비 티어 t, 몬스터 티어 m 에 대해 목표 승률 곡선 W*(t,m) 을 표로 두고, Σ (W_sim − W*)² + 제약 위반 벌점(스탯 변경폭 ±30% 등).
- **대리 플레이어**: 규칙 봇은 결정론·해석 가능·즉시 사용 가능하다. 학습 봇은 "사람 같은 실수"를 재현하려면 실제 플레이 데이터가 필요하고(King), 그 전엔 규칙 봇에 인간 파라미터(반응 지연 0.2~0.4초, 회피 성공률, 물약 사용 임계 체력)를 넣어 페르소나 여러 개를 만드는 것이 EA MultiGAIL 의 "여러 페르소나" 목적을 값싸게 달성한다.

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

### A. 권장 구조: "결정론 전투 커널 + 시나리오 파일 + 프로세스 팬아웃 + 통계 집계"

```
[시나리오 JSON/CSV]  --->  [SimRunner (자동화 테스트 or 커맨드렛, -nullrhi -unattended)]
  플레이어 조건(장비/물약/버프)          |  UWorld::CreateWorld → 고정 Delta 로 World->Tick 반복
  몬스터 종류×마릿수, 시드, 최대 시간     |  FRandomStream(시드) 만 사용, 틱별 상태 해시 기록
                                        v
                                  [결과 CSV/JSON 1행 = 1시뮬]
  승패, 종료 틱(시간), 준/받은 피해, 물약 사용, 스킬별 피해, 해시 체인 최종값
                                        v
[집계 스크립트(파이썬)]  승률±표준오차, 생존 시간 분위수(p10/p50/p90), DPS, 목표 곡선 오차 → 파라미터 탐색
```

1. **실행 형태**: 프로젝트 픽스처 `FTDScopedCombatWorld` 를 확장해 "시나리오 → 결과 행" 함수를 만들고, 자동화 테스트(`Automation RunTests TDGame.Sim.*`) 로 감싸 `-nullrhi -unattended -nopause -nosound -testexit="Automation Test Queue Empty"` 로 실행한다. 커맨드렛은 엔진 틱을 돌리지 않으므로(imzlp 2020) 월드 직접 틱 방식과 궁합이 좋지만, 자동화 테스트 실행기는 이미 검증된 CI 경로가 있어 우선 채택한다. 근거: 2절 표.
2. **시간 진행**: 엔진 `-UseFixedTimeStep` 에 의존하지 말고 `World->Tick(LEVELTICK_All, 1/60 또는 1/30)` 을 코드가 직접 호출한다. `FApp::SetFixedDeltaTime` 도 병행 설정해 타이머·애님 노티파이가 참조하는 `FApp::GetDeltaTime` 과 일치시킨다(`UnrealEngine.cpp:3019-3021` 의 동작을 픽스처가 대신 수행). AI 판단 주기(예: 10Hz)는 시뮬 틱의 정수배로 두어 실게임과 동일한 "판단 격자"를 유지한다(Learning Agents 헤더 주석: 훈련 스텝이 추론 스텝과 다르면 일반화가 깨진다, `LearningAgentsTrainer.h:41-43`).
3. **난수**: 시나리오당 `FRandomStream Master(Seed)`; 하위 스트림은 `Master.GetUnsignedInt()` 로 파생(전투/AI/드롭 분리). `FMath::Rand*` 전역 호출은 코드 리뷰로 금지하고, 방어선으로 프로세스 커맨드라인에 `-FixedSeed` 를 붙인다(`LaunchEngineLoop.cpp:2462`). 하위 비트 품질이 나쁘므로 `%` 대신 `RandRange`/`FRand` 를 쓴다(FRandomStream 문서).
4. **물리 회피**: 밸런스 시뮬 월드에는 Chaos 리지드 바디를 두지 않는다. 이동은 수학적 이동, 피격은 거리·캡슐 겹침 계산으로 판정한다. 부득이 물리를 쓰면 `p.Chaos.Solver.Deterministic 1` + 비동기 고정 물리 틱을 켜고, 실측 분산이 0인지 해시로 확인한다(arXiv 2104.06262 의 "허용 분산 영역" 방법).
5. **결정론 검증 게이트**: 같은 시나리오·시드를 2회(가능하면 다른 프로세스에서) 돌려 틱별 해시 체인이 동일한지 CI 에서 확인한다. 해시는 엔티티별(HP, 위치, 상태 태그, 쿨다운)로 계층화해 불일치 지점을 드릴다운한다(Bugnet; OpenTTD `-d desync=2` 발상). 컴파일 옵션 변경(예: `/fp:fast`)은 결과가 달라질 수 있으므로 "골든 해시" 갱신 절차를 둔다(gafferongames).
6. **병렬**: 시나리오 배열을 N 프로세스로 분할해 각 프로세스가 독립 결과 파일을 쓴다. Gauntlet `Parallel` 옵션(`TestExecutor.cs:31`) 또는 파이썬 런처(`subprocess`) 어느 쪽이든 되며, 초기에는 파이썬 런처가 단순하다. 프로세스당 메모리(에디터 Cmd 로 띄우면 수 GB)를 감안해 물리 코어 수의 절반부터 시작한다.
7. **출력 지표**(산업 관행 정렬): 승률과 표준오차, 전투 시간 p10/p50/p90, 플레이어 잔여 HP 분포, 총 준/받은 피해, 스킬별 피해 비중, 물약 사용 횟수, "리썰 위험(한 번에 최대 피격/최대 HP)". PoB 의 EHP 개념을 "몬스터 조합별 기대 최대 피격량 / 플레이어 EHP" 비율로 사전 계산해 시뮬 전에 명백한 불균형을 걸러낸다.
8. **밸런스 루프**: 목표 승률 곡선 W*(장비 티어, 몬스터 티어·마릿수) 를 CSV 로 두고, 그리드 탐색(파라미터 ≤5) → 베이지안 최적화(그 이상) 순으로 올린다. 변경폭 제약(±30%)과 "버프 편중 시 상위 너프" 규칙(Riot 2020)을 목적 함수 벌점으로 넣는다.
9. **대리 플레이어**: 1차는 규칙 봇(페르소나 파라미터화). 2차는 실제 플레이 로그로 모방학습(Learning Agents Imitation Trainer) 봇을 만들고, King 방식으로 "봇 승률 → 사람 승률" 회귀를 맞춘다. 강화학습 봇은 "탐색·버그 발견·최강 전략 상한" 용도로 분리 운용한다(EA 사례).

### B. 피할 것
- 엔진 메인 루프 + 렌더 창 띄운 채 `slomo`/시간 팽창으로 빠르게 돌리기: 프레임 상한·입력·GC 가 섞여 속도와 결정론 모두 손해. 고정 스텝 + NullRHI 또는 월드 직접 틱을 쓴다.
- 한 프로세스 안에서 여러 월드를 다중 스레드로 틱: 언리얼 게임 스레드 가정 위반. 프로세스 병렬로 대체.
- 결정론을 "실행해 보니 같더라"로 판정: 해시 체인 비교를 CI 게이트로 둔다.
- 고정소수점 전면 도입: 싱글 플레이·동일 빌드 조건에서는 비용만 크다(gamedeveloper 2015 의 사례는 크로스 플랫폼 록스텝).
- 최강 봇으로 밸런스 판단: King 결과처럼 사람 같은 봇이 예측력이 높다. 최강 봇은 상한 검증용.
- 시뮬 결과만으로 "느낌" 판단: King 현업 발언대로 통과율·이상치 탐지에는 강하나 체감은 사람 테스트로 보완.

### C. 반복 수 산정표 (이항 표준오차 sqrt(p(1-p)/n), p=0.5 최악 기준, 산술 계산)

| 시뮬 횟수 n | 승률 표준오차 | 95% 신뢰구간 폭(±1.96σ) | 용도 |
|---|---|---|---|
| 100 | 5.0%p | ±9.8%p | 개발 중 빠른 스모크 |
| 400 | 2.5%p | ±4.9%p | 파라미터 탐색 내부 평가(BO-MCTS 논문 수준, 미확인) |
| 1,000 | 1.6%p | ±3.1%p | twanvl 기본값; 일일 밸런스 리포트 |
| 10,000 | 0.5%p | ±1.0%p | Bob's Buddy 수준; 릴리스 전 확정 |

Riot 의 밴드 폭이 49%~54.5%(5.5%p) 이므로, 밴드 위반을 판정하려면 최소 1,000회(±3.1%p) 이상이 필요하고, 0.5%p 단위 조정(Riot 2020 갱신 폭)을 검증하려면 10,000회급이 필요하다.

### D. 시나리오·결과 스키마 초안 (텍스트 우선, 생성형 AI 가 읽고 쓰기 쉬운 형태)

```json
{ "id": "T3_sword_vs_goblin_x8", "seed": 1234, "max_seconds": 120, "tick_hz": 60, "ai_hz": 10,
  "player": { "equipment": ["Sword_T3", "Armor_T2"], "potions": { "HP": 2 }, "buffs": ["Haste_S"],
              "persona": "normal", "reaction_delay": 0.3 },
  "monsters": [ { "type": "Goblin", "count": 8, "ai": "Goblin_Melee_v2" } ] }
```
결과 행(CSV): `id, seed, result(win|lose|timeout), end_tick, dmg_dealt, dmg_taken, potions_used, max_single_hit_taken, state_hash_final`.
twanvl 시뮬레이터가 텍스트 보드 정의를 입력으로 받는 것과 같은 발상이며, 시드와 최종 해시를 결과에 남겨 리플레이·결정론 검증에 재사용한다.

### E. 초기 작업 단위(제안)
1. `FTDScopedCombatWorld` 에 시드·고정 델타·해시 기록 기능 추가 → 결정론 게이트 테스트 1개.
2. 시나리오 JSON 스키마(플레이어 장비/물약/버프, 몬스터 목록, 시드, 최대 시간) + 결과 CSV 스키마 확정.
3. 규칙 봇 페르소나 3종(신중/보통/공격적) 구현.
4. 파이썬 런처(프로세스 팬아웃) + 집계(승률·분위수·목표 곡선 오차) 스크립트.
5. 그리드 탐색 1회 실전 적용 후 베이지안 최적화 도입 여부 결정.

---

## 미확인·미해결 질문

1. **Learning Agents "Headless Training & Network Snapshots (5.5)" 튜토리얼 본문**: 페이지가 JS 렌더링이라 제목만 확인. 커맨드라인 예시는 미확인이며, 대신 엔진 소스(`LearningAgentsTrainer.cpp:82-100`)로 동일 기능을 확인했다.
2. **Blizzard 디아블로 내부 전투 시뮬레이터**: 공개 자료를 찾지 못했다(GDC 검색 결과 없음). 하스스톤 아레나 ML 사례만 확인.
3. **Supercell 클래시 로얄 공식 봇 시뮬**: 공식 자료 없음(커뮤니티 봇·API 분석만). 미확인.
4. **닌텐도(젤다)·소니 자동 플레이테스트**: 젤다는 절차적 생성 자동화만 확인, 자동 플레이테스트 봇 자료 없음. 소니 자료 없음. 미확인.
5. **King 관련 2차 기사**(computerweekly, aibusiness): HTTP 403 으로 본문 미확인. "수동 조정 95% 감소, 하루 1.8억 예측" 같은 수치는 검색 요약에만 있어 미확인으로 둔다. 1차 논문(Gudmundsson 2018)과 PocketGamer 2026 인터뷰만 근거로 채택.
6. **Beau & Bakkes 2016, Khajah 2016 초록 본문**: 페이지 비어 있음/403. 검색 요약만 존재.
7. **BO-MCTS 논문의 "승률 평가에 400 게임"**: 초록에는 없고 검색 요약에만 있음. 미확인.
8. **D3Planner/Last Epoch Tools/Grim Tools 내부 계산 방식**: 랜딩 페이지만 확인. 미확인.
9. **Chaos `p.Chaos.Solver.Deterministic` 기본값**: 엔진 소스는 -1(설정 따름). 2차 위키는 "기본 1"이라 했으나 소스와 불일치 → 소스(-1)를 채택. 실제 프로젝트 설정값(`UPhysicsSettings` 의 해당 필드)은 이번 조사 범위 밖.
10. **GAS 내부에서 전역 난수를 쓰는 지점이 있는가**(예: `FGameplayEffectSpec` 의 확률 적용): 이번 조사에서 확인하지 않았다. 밸런스 커널 구현 전에 `Engine/Plugins/Runtime/GameplayAbilities` 를 grep 해 `FMath::FRand`/`RandRange` 호출을 목록화해야 한다.
11. **UWorld 직접 틱 시 `FApp::GetDeltaTime` 과 `World->GetDeltaSeconds` 의 불일치가 타이머·노티파이에 영향을 주는가**: 픽스처가 통과하는 것으로 보아 문제없어 보이나, 결정론 게이트 테스트로 확정 필요.

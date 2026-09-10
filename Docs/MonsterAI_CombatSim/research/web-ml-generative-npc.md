# 웹: 2024~2026 머신러닝·생성형 AI 로 NPC/몬스터를 학습·제어하는 최신 기법과 언리얼 적용 사례

- 조사일: 2026-09-09
- 조사 방법: WebSearch 로 후보를 찾고 WebFetch 로 본문을 직접 열어 확인(총 40여 개 출처, 그중 본문 확인 34개). 엔진 사실은 로컬 설치본 `C:/Program Files/Epic Games/UE_5.8/Engine` 의 파일:줄 번호로 확인.
- 표기 규칙: `[웹]` 은 본문을 연 웹 출처, `[엔진]` 은 로컬 엔진 소스, `[미확인]` 은 접근 실패(HTTP 403, 자바스크립트 렌더링으로 본문 없음)로 검색 요약만 본 것.
- 용어: RL(Reinforcement Learning, 강화학습), IL(Imitation Learning, 모방학습), BC(Behavior Cloning, 행동 복제), PPO(Proximal Policy Optimization, 근접 정책 최적화), LLM(Large Language Model, 대규모 언어 모델), SLM(Small Language Model, 소형 언어 모델), BT(Behavior Tree, 행동 트리), HTN(Hierarchical Task Network, 계층적 태스크 네트워크), GOAP(Goal Oriented Action Planning, 목표 지향 행동 계획), NNE(Neural Network Engine, 언리얼 신경망 추론 계층), MCP(Model Context Protocol, 모델 컨텍스트 프로토콜), ONNX(Open Neural Network Exchange, 신경망 교환 형식), ISPC(Intel SPMD Program Compiler, 인텔 SIMD 컴파일러).

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들)

1. **언리얼 Learning Agents 는 5.8 에서도 "Experimental, 버전 0.2" 이며 추론은 NNERuntimeBasicCpu(버전 0.1, Experimental) 로만 돈다.** 즉 런타임 몬스터 두뇌를 여기에 걸면 두 개의 실험 플러그인에 의존한다. 근거: `Engine/Plugins/Experimental/LearningAgents/LearningAgents.uplugin:4,16` (`"VersionName": "0.2"`, `"IsExperimentalVersion": true`), `Source/LearningAgents/LearningAgents.Build.cs:18,24` (`"NNERuntimeBasicCpu"`, `"NNE"`), `Engine/Plugins/Experimental/NNERuntimeBasicCpu/NNERuntimeBasicCpu.uplugin:4,16`. 웹 문서도 동일: Epic 5.8 플러그인 색인 "Learning Core, Python ML Package, NNERuntimeBasicCpu 에 의존, Experimental" (https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/LearningAgents , 2026 열람).
2. **학습은 엔진 밖 Python(PyTorch) 자식 프로세스가 공유 메모리 또는 소켓으로 붙어서 수행한다.** 쿠킹된 빌드에서는 Python·학습 스크립트가 제거되므로 별도 경로를 지정해야 헤드리스 학습이 가능하다. 근거: `LearningAgentsCommunicator.h:28,53,135`, `LearningAgentsTrainer.h:123-136,157-158`, `LearningCore/Content/Python/learning_core/train_ppo.py:59-60`(`np.random.seed(seed)`, `torch.manual_seed(seed)`). → TDGame 의 헤드리스 전투 시뮬레이터와 결합하려면 "쿠킹 없는 -game 실행 또는 Python 경로 지정" 을 설계에 넣어야 한다.
3. **추론 결정론은 "정책 시드 고정 + ActionNoiseScale=0 + 같은 빌드/같은 CPU 명령셋" 세 조건으로만 보장된다.** `ULearningAgentsPolicy::MakePolicy(..., const int32 Seed = 1234)` (`LearningAgentsPolicy.h:117`), `DecodeAndSampleActions(const float ActionNoiseScale = 1.0f)` 의 주석 "Set this to zero to always sample the mean (expected) action" (`LearningAgentsPolicy.h:183-186`). 단 NNERuntimeBasicCpu 는 `#define NNE_RUNTIME_BASIC_ENABLE_ISPC INTEL_ISPC` (`NNERuntimeBasicCpuModel.cpp:9`) 로 ISPC SIMD 경로를 쓰므로, 부동소수점 비결합성 때문에 다른 명령셋/빌드 간 비트 동일성은 보장되지 않는다(비결합성·배치 불변성 논의: https://thinkingmachines.ai/blog/defeating-nondeterminism-in-llm-inference/ , 2025).
4. **업계 실무는 RL 을 "적 두뇌" 보다 "플레이어 대리·QA·밸런스 추정" 에 먼저 쓴다.** EA SEED: 모방학습 봇이 RL 대비 "training took 20 minutes compared to 5 hours" (https://www.ea.com/seed/news/seed-ml-research-aaa-game-testing , 2023), 텐센트 Juewu: 자기대전 수백만 판으로 신규 영웅·장비 승률을 "95% accuracy" 로 사전 추정(https://www.wetest.net/blog/game-ai-automated-testing-technology-evolution-market-analysis-1171.html , 2026). 적으로 출하한 사례(GT Sophy, Rainbow Six Siege 봇, Roller Champions)는 모두 대규모 인프라(1,000대 이상 PS4)와 전담 연구팀이 있었다(https://ai.sony/blog/gran-turismo-sophy-five-years-on-from-nature-cover-to-open-frontier , 2026).
5. **수백 마리 신경망 추론 비용은 CPU 에서 마리당 약 0.2ms 이하이며 병목은 추론이 아니라 길찾기다.** UE 5.5 + NNERuntimeORT, 207k 파라미터 MLP(256-256-128) 로 64 마리에서 "mean per-call latency 183–202 µs", "the hard ceiling is NavMesh FindPath saturation, not policy inference"(https://arxiv.org/html/2605.23652v1 , 2026). Arm 도 Pixel 7 Pro CPU 에서 100 마리 MLP/LSTM 이 60 FPS 근처(https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/neural-network-models-for-multi-agent-games-on-arm , 2023). → "소형 MLP 는 수백 마리 가능, 그러나 틱 분산·길찾기 예산이 진짜 설계 변수".
6. **생성형 AI 가 몬스터 행동을 쓰기 가장 좋은 형식은 "제약된 텍스트 DSL(도메인 특화 언어) + 정적 검증기 + 시뮬레이션 피드백 루프" 이며, 노드 에디터 에셋이 아니다.** BTGenBot-2 는 10억 파라미터 모델이 XML BT 를 제로샷 90.38%, 원샷 98.07% 성공률로 생성(https://arxiv.org/abs/2602.01870 , 2026); Real-Time World Crafting 은 LLM 출력을 제한된 DSL 로만 받아 ECS 를 구성해 엔진 직접 접근을 차단(https://arxiv.org/abs/2510.16952 , 2025). Epic 의 공식 Unreal MCP 는 5.8 에 Experimental 로 들어왔고 `UFUNCTION(meta = (AICallable))` 로 C++ 도구를 노출할 수 있으나 HTTP 루프백 전용·인증 없음(https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor , 2026).
7. **온디바이스 SLM(NVIDIA ACE) 기반 "생각하는 NPC" 는 RTX GPU 전제·소수 캐릭터(동료 1명, 보스 1명) 용도이며, 탑다운 대량 몬스터에는 부적합하다.** PUBG Ally 는 Mistral-Nemo-Minitron-8B, 2B 모델도 약 1.5GB VRAM, "Players make about 8-13 micro decisions a second" (https://www.nvidia.com/en-us/geforce/news/nvidia-ace-autonomous-ai-companions-pubg-naraka-bladepoint/ , 2025). MIR5 의 AI 보스 Asterion 은 단일 보스 사례.
8. **세계 모델(Microsoft Muse/WHAM, DeepMind Genie 3)과 범용 에이전트(SIMA 2)는 2025~2026 시점에 "아이디어 발상·연구 프리뷰" 단계이고 게임 런타임 몬스터 제어 도구가 아니다.** Muse 는 300×180 해상도, 단일 게임 학습(https://www.microsoft.com/en-us/research/blog/introducing-muse-our-first-generative-ai-model-designed-for-gameplay-ideation/ , 2025); Genie 3 는 720p 24fps, 약 1분 기억, 제한 연구 프리뷰(https://deepmind.google/blog/genie-3-a-new-frontier-for-world-models/ , 2025); SIMA 2 는 제한 연구 프리뷰(https://deepmind.google/blog/sima-2-an-agent-that-plays-reasons-and-learns-with-you-in-virtual-3d-worlds/ , 2025).

### 이 프로젝트에 맞는 ML 적용 우선순위 3개

| 순위 | 적용 대상 | 왜 이것부터인가 | 핵심 근거 |
|---|---|---|---|
| 1 | **플레이어 대리 봇(모방학습·행동 복제)** 을 결정론적 헤드리스 전투 시뮬레이터의 "플레이어 측 입력" 으로 사용 | 밸런스 툴은 플레이어 행동 분포가 있어야 의미가 있고, IL 은 RL 보다 20~30배 빨리 수렴하며 설계자가 직접 시연을 녹화하면 된다. 추론은 소형 MLP 라 수백 회 반복에 부담 없음 | EA SEED 2023(20분 vs 5시간), Learning Agents `ULearningAgentsRecorder`/`ULearningAgentsImitationTrainer`(`LearningAgentsImitationTrainer.h:53,79`), 시드·노이즈 0 으로 결정론 |
| 2 | **LLM 이 텍스트 DSL 로 몬스터 행동(HTN/유틸리티 규칙)을 생성 → 정적 검증기 → 헤드리스 시뮬레이터 자동 평가** 파이프라인 | 사용자 요구 4번(생성형 AI 가 코드/텍스트로 제작·분석)에 직접 부합. BT XML 생성 성공률 90% 이상이 소형 모델로도 가능하며, 노드 에셋 대신 텍스트라 토큰 비용이 낮다 | BTGenBot-2 2026, LLM-as-BT-Planner 2024/2025, Real-Time World Crafting 2025, Unreal MCP 5.8 |
| 3 | **RL 은 밸런스 탐색·QA 봇(자기대전, 커리큘럼)에 한정** 하고, 출하용 몬스터 두뇌로는 "파라미터 튜닝(유틸리티 가중치·공격 주기)" 만 학습 결과를 반영 | 불투명성·밸런스 예측 불가·재현성 문제를 피하면서 RL 의 탐색 능력을 얻는다. 필요 시 상위 몬스터 소수에만 페르소나 조건부 공유 정책 적용 | 텐센트 Juewu 95% 승률 추정, Ubisoft Roller Champions "1–4 days to train" (https://arxiv.org/abs/2012.06031 , 2020), One Policy Infinite NPCs 2026(183–202 µs/마리) |

---

## 상세 조사

### 1) 언리얼 Learning Agents 5.4~5.8 변화, 채택 사례, 학습 속도·하드웨어, 모방 학습 워크플로

#### 1-1. 버전별 변화 (웹 + 엔진 소스 교차 확인)

| 버전 | 변화 | 출처 |
|---|---|---|
| 5.3 (2023) | 최초 공개. "train your NPCs via reinforcement & imitation learning". Epic 담당자 Brendan Mulcahy. 게임이 데이터를 모아 Python 프로세스로 넘기는 구조(ML Adapter 와 반대 방향) | [웹] https://forums.unrealengine.com/t/tutorial-learning-agents-introduction/835859 (2023-03-31, 2023-04-04 답글) |
| 5.4 (2024-04) | "Structured Observations and Actions"(신경망 입출력을 구조적으로 기술), TensorBoard 연동, **헤드리스 학습 모드(CLI)**, 모방학습 튜토리얼 추가 | [웹] https://mike.gold/notes/x-bookmarks/unreal/ue-54-learning-agents-update (2024-04-23, Daniel Holden 게시 인용) |
| 5.5 (2024-11) | "bring your own algorithm"(사용자 정의 트레이너) 추가. 튜토리얼·코스가 5.5 기준으로 갱신되었고 2026 현재도 5.5 문서가 최신. 개발자 스스로 문서 이전이 "quite the burdensome process" 라고 언급 | [웹] https://forums.unrealengine.com/t/tutorial-learning-agents-5-5/2122953 (2024~2026 스레드) |
| 5.5 공식 튜토리얼 본문 | 자바스크립트 렌더링으로 제목만 수신 | [미확인] https://dev.epicgames.com/community/learning/tutorials/bZnJ/unreal-engine-learning-agents-5-5 , https://dev.epicgames.com/community/learning/tutorials/jBa4/unreal-engine-what-s-new-in-learning-agents-5-5 |
| 5.5/5.8 릴리스 노트 | Learning Agents 항목 없음(둘 다 본문 확인). 5.8 릴리스 노트에는 MCP 플러그인(Experimental), StateTree 시작 상태·컴파일러 매니저, Mass 개편(Mass Signals 코어 편입, 게임 스레드 밖 엔티티 생성, 희소 프래그먼트, MassCore 모듈)만 기재 | [웹] https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-5-release-notes , https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes (2025~2026) |
| 5.8 로컬 소스 | 모듈 4개: LearningAgents, LearningAgentsTraining, LearningAgentsTrainingEditor, **LearningAgentsReplay**. 헤더에 **FlowMatching**(`LearningAgentsFlowMatching.h`, `LearningAgentsFlowMatchingTrainer.h`), **Gym**(`LearningAgentsGym.h:22` "handles the start and reset of entities training in a single gym"), **EntitiesManagerComponent**(`LearningAgentsEntitiesManagerComponent.h:56` "handles the spawn and reset of multiple entity types during training"), DepthMapComponent 존재. 어느 버전에서 추가되었는지는 릴리스 노트에 없어 [미확인] | [엔진] `Engine/Plugins/Experimental/LearningAgents/LearningAgents.uplugin:17-37`, `Source/LearningAgents/Public/*.h`, `Source/LearningAgentsTraining/Public/*.h` |

핵심 시그니처 발췌(엔진, 10줄 이내):

```cpp
// LearningAgentsPolicy.h:107-117
static UE_API ULearningAgentsPolicy* MakePolicy(
    UPARAM(ref) ULearningAgentsManager*& InManager, UPARAM(ref) ULearningAgentsInteractor*& InInteractor,
    TSubclassOf<ULearningAgentsPolicy> Class, const FName Name = TEXT("Policy"),
    ULearningAgentsNeuralNetwork* EncoderNeuralNetworkAsset = nullptr, /* Policy, Decoder 동일 */ ...,
    const FLearningAgentsPolicySettings& PolicySettings = FLearningAgentsPolicySettings(),
    const int32 Seed = 1234);
// LearningAgentsPolicy.h:186,192
UE_API void DecodeAndSampleActions(const float ActionNoiseScale = 1.0f); // 0 이면 항상 평균 행동
UE_API void RunInference(const float ActionNoiseScale = 1.0f);
```

```cpp
// LearningAgentsTrainer.h:157-158  기본 트레이너 = 행동 복제 파이썬 모듈
FString TrainerFileName = TEXT("learning_core.train_behavior_cloning");
// LearningAgentsPPOTrainer.h:73,101,107,241
int32 NumberOfIterations = 1000000;  int32 PolicyBatchSize = 1024;  int32 CriticBatchSize = 4096;  bool bUseTensorboard = false;
// LearningAgentsImitationTrainer.h:37,53,79
float TrainerCommunicationTimeout = 10.0f;  int32 NumberOfIterations = 1000000;  uint32 BatchSize = 128;
// LearningAgentsManager.h:233  기본 최대 에이전트 수
int32 MaxAgentNum = 1;   // SetMaxAgentNum(...) 로 확장 (:54)
```

#### 1-2. 학습 방식·하드웨어

- 학습 프로세스: `MakeSharedMemoryTrainingProcess` 가 "local python training sub-process which will communicate via shared memory" 를 띄운다(`LearningAgentsCommunicator.h:130-135`). 소켓 통신 설정도 별도 존재(`:53 FLearningAgentsSocketCommunicatorSettings`) → 원격 학습 서버 가능.
- Python 스크립트 위치: `Engine/Plugins/Experimental/LearningCore/Content/Python/learning_core/train_ppo.py`, `train_behavior_cloning.py`; PyTorch 사용(`NNERuntimeBasicCpu/Content/Python/nne_runtime_basic_cpu_pytorch.py:22 import torch`). 시드는 `train_ppo.py:59-60` 에서 `np.random.seed(seed)`, `torch.manual_seed(seed)` 로 고정.
- 쿠킹 빌드 학습: "in cooked, non-editor builds ... we wont have access to python and the LearningAgents training scripts - these are editor-only things and are stripped during the cooking process. However, running training in non-editor builds can be very important - we probably want to disable rendering and sound while we are training" (`LearningAgentsTrainer.h:123-129`). `NonEditorEngineRelativePath` 로 경로를 지정하면 가능.
- 하드웨어·학습 시간: Epic 공식 문서에 수치 없음([미확인]). 포럼에서는 "training can likely take hours", Mac 에서 타임아웃 조정 필요, "IL can only learn to do things that you have given them demonstrations of" (https://forums.unrealengine.com/t/tutorial-learning-agents-5-5/2122953). 커뮤니티 정리본(UE 5.3 이상, 최대 32 에이전트 병렬, 모방 데이터 과적합 주의, "the plugin is still in beta and there aren't many resources on it yet"): https://github.com/XanderBert/Unreal-Engine-Learning-Agents-Learning-Environment (2024). Jørgensen 의 Medium 글은 HTTP 403 → [미확인].
- 대안 플러그인: **AMD Schola v2**(2025-12-02, UE 5.5~5.6, `UNNEPolicy` 로 NNE 네이티브 ONNX 추론, SimpleStepper/PipelinedStepper, Stable Baselines 3·Ray RLlib·Gymnasium 연동) https://gpuopen.com/learn/announcing-amd-schola-v2-nextgen-rl-unreal-engine/ . **MLAdapter**(엔진 내장, Experimental, "RPC interface through which an external process can query game state ... run in-engine via neural networks loaded from ONNX models", `Engine/Plugins/AI/MLAdapter/MLAdapter.uplugin:6,16`). **Unreal-MAP**(2025-03, 다중 에이전트 RL 연구 플랫폼) https://arxiv.org/abs/2503.15947 .

#### 1-3. 실제 채택 사례

- Learning Agents 로 출하한 상용 게임 사례: 검색·문서에서 확인되지 않음 → [미확인]. Epic 공개 로드맵 페이지는 본문이 잘려 상태 확인 실패([미확인] https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap/c/1654-learning-agents).
- UE5 + NNE 로 RL 정책을 실제 배치한 학술 사례: One Policy, Infinite NPCs(2026, UE 5.5, 64 마리) — 5절 참조.

#### 1-4. 모방 학습 워크플로(엔진 소스 기준)

1. `ULearningAgentsRecorder` 로 관측·행동 쌍을 `ULearningAgentsRecording` 에셋으로 기록(`LearningAgentsRecorder.h`, `LearningAgentsRecording.h`).
2. `ULearningAgentsImitationTrainer` 가 기본 트레이너 `learning_core.train_behavior_cloning` 를 띄워 BC 학습(`LearningAgentsTrainer.h:158`).
3. 결과는 `ULearningAgentsNeuralNetwork : public UDataAsset` (`LearningAgentsNeuralNetwork.h:75`) 에 저장, 활성함수는 ReLU/ELU/TanH/GELU(`:35-38`).
4. 런타임은 `ULearningAgentsPolicy::RunInference(0.0f)` 로 결정적 실행.
5. 5.8 에는 `LearningAgentsReplay` 모듈(`AsyncAction_LearningAgentsQueryReplays.h`, `LearningAgentsReplaySubsystem.h`)이 있어 리플레이 질의 기반 데이터 수집이 가능해 보이나 문서 미비 → [미확인].

---

### 2) 업계 사례

| 사례 | 연도 | 사실 | 출처 |
|---|---|---|---|
| 소니 GT Sophy | 2023~2026 | 2.0(2023-11) 340+ 차량·9 트랙 상시 기능, 2.1(2025-03) 500+ 차량·19 트랙 커스텀 레이스, 3.0(2025-12) 50 레이스 DLC. 학습은 **1,000대 이상 PS4** 분산 시스템 "Dart". 원본은 트랙 기하·상대 위치 등 "privileged information" 사용. 스포츠맨십이 "the hardest problem". 2025 비전 기반 에이전트는 카메라·IMU 만으로 챔피언급(https://arxiv.org/abs/2504.09021 , RA-L 2025). LLM 이 텍스트로 보상 함수 생성(NeurIPS 2025 워크숍) | [웹] https://ai.sony/blog/gran-turismo-sophy-five-years-on-from-nature-cover-to-open-frontier (2026-07-01) |
| EA SEED | 2023~2026 | CoG 2023 논문 5편: RL 배치 난제(Battlefield 2042, Dead Space 2023), 모방학습 설계 검증("training took 20 minutes compared to 5 hours"), CCPT(호기심+모방), MultiGAIL(복수 페르소나). "Battlefield V requires testing of 601 different features ... ~300 work years". 2024 WCCI: 모방학습 데이터 증강으로 일반화 개선(https://www.ea.com/seed/news/wcci2024-generalization-game-agents , 2024-07-05). 2026 CoG 비전 논문 "Augmenting Game AI with Deep RL": 병목·난제 정리(https://arxiv.org/abs/2606.20210 , 2026-06) | [웹] https://www.ea.com/seed/news/seed-ml-research-aaa-game-testing (2023), https://arxiv.org/abs/2307.11105 (2023) |
| 유비소프트 La Forge | 2020~2023 | Roller Champions: "1–4 days to train a new model following gameplay changes", 접속 끊긴 플레이어 대체 + 밸런싱 보조(https://arxiv.org/abs/2012.06031 , 2020). Rainbow Six Siege Defender AI: 전통 AI + 리플레이 학습 + RL 루프, "roughly one percent of rounds played" 수집, 신규 오퍼레이터는 내부 2주 데이터, 난이도 관리 필요(https://news.ubisoft.com/en-us/article/1MlKnolSLJFuJDnATWiorr/how-rainbow-six-siege-developed-ai-that-acts-like-real-players , 2023-02-21). 2024~2025 신규 배치 사례는 검색에서 확인 안 됨 → [미확인] | [웹] |
| 텐센트 | 2024~2026 | GDC 2024: GiiNEX 게임 AI 엔진 공개, MoreFun 스튜디오 격투게임(나루토 모바일) 대규모 RL 로 "save up to 90% of the time and resources"(http://www.tencent.com/tencent-games-shares-insights-and-technologies-at-gdc-2024/ , 2024-04-02). Juewu(왕자영요) 자기대전 밸런스 추정 95%(wetest, 2026-02-25). 2026-05-28: 9개 게임 AI 응용(Lingbao 동료 AI, Xiaotian 대형모델 동료 등), GDC 2026 에서 21개 AI 강연(https://eu.36kr.com/en/p/3827636412224386 , 2026) | [웹] |
| 넷이즈 | 2025~2026 | Fuxi 랩 "Intelligent Task Regression Testing": RPG 퀘스트를 순차 결정 문제로 모델링, 500+ 퀘스트, 테스트 주기 주→시간(wetest, 2026). NARAKA: BLADEPOINT 모바일 PC 판 ACE 로컬 추론 AI 동료(2025-03). "400 intelligent NPCs" 무협 세계는 검색 요약만 → [미확인] | [웹] |
| 엔비디아 ACE | 2025~2026 | CES 2025: PUBG Ally(Mistral-Nemo-Minitron-8B), inZOI Smart Zoi, NARAKA, MIR5 보스. SLM 2B 는 약 1.5GB VRAM. "8-13 micro decisions a second". 2025-10: Qwen3-8B 를 IGI SDK 로 온디바이스(https://developer.nvidia.com/blog/nvidia-ace-adds-open-source-qwen3-slm-for-on-device-deployment-in-pc-games/). 2026-06-16: Game Agent SDK + UE5 플러그인(Qwen 3.5 4B, 음성인식 120M, TTS 350M, 함수 호출), PUBG Ally 오픈 베타, Total War: PHARAOH 실험(https://developer.nvidia.com/blog/build-on-device-ai-companions-with-the-nvidia-ace-game-agent-sdk-and-unreal-engine-5-plugins/) | [웹] https://www.nvidia.com/en-us/geforce/news/nvidia-ace-autonomous-ai-companions-pubg-naraka-bladepoint/ (2025-01-06) |
| 마이크로소프트 Muse/WHAM | 2025 | WHAM-1.6B, Bleeding Edge 10억 장 이상 이미지·조작("more than 7 years of continuous human gameplay"), 300×180 해상도, 용도는 "gameplay ideation". 한계: 단일 게임, 저해상도, 희귀 메커닉 누락. Nature 논문 | [웹] https://www.microsoft.com/en-us/research/blog/introducing-muse-our-first-generative-ai-model-designed-for-gameplay-ideation/ (2025-02-19) |
| 구글 딥마인드 Genie 3 / SIMA 2 | 2025 | Genie 3: 720p 24fps, 약 1분 시각 기억, "limited research preview", 다중 에이전트 상호작용 약함(2025-08-05). SIMA 2: Gemini 기반, 화면 픽셀 + 가상 키보드/마우스, 자기개선, Valheim·No Man's Sky 등 평가, "very long-horizon" 과제 취약, 제한 프리뷰(2025-11-13). MIT Tech Review: 장기 기억 제거로 반응성 확보, 인간 수준 조작 미달 | [웹] https://deepmind.google/blog/genie-3-a-new-frontier-for-world-models/ , https://deepmind.google/blog/sima-2-an-agent-that-plays-reasons-and-learns-with-you-in-virtual-3d-worlds/ , https://www.technologyreview.com/2025/11/13/1127921/google-deepmind-is-using-gemini-to-train-agents-inside-goat-simulator-3/ |
| Unity ML-Agents | 2024~2025 | Release 22(2024-10-05, v3.0.0, Sentis 2.0), Release 23(2025-09-02, v4.0.0, Inference Engine 2.2.1, Unity 6000.0 최소, 확장 패키지 통합). 유지보수 지속, 종료 선언 없음. 4.0 에서 M 시리즈 Mac 추론 5~7배 느려짐 보고 | [웹] https://github.com/Unity-Technologies/ml-agents/releases , https://discussions.unity.com/t/ml-agents-4-0-0-is-now-available/1681770 |
| MIR5 AI 보스 | 2025 | 위메이드 넥스트 + NVIDIA ACE, 과거 전투를 기억해 전술에 적응하는 보스 "Asterion". 보도자료 본문은 403 → 검색 요약만 [미확인] | https://www.businesswire.com/news/home/20250106300797/en (2025-01-06) |

업계 사례에서 읽히는 공통 패턴: (1) RL 이 "적" 으로 출하된 경우는 레이싱·대전 등 규칙이 고정되고 대규모 학습 인프라가 있는 경우, (2) 대다수 스튜디오는 IL/RL 을 QA·밸런스·플레이어 대리에 사용, (3) 생성형 언어 모델 NPC 는 GPU 전제·소수 캐릭터.

---

### 3) LLM 으로 BT·HTN·GOAP 도메인을 생성하는 연구·도구

| 연구/도구 | 연도 | 표현 형식 | 결과 | 출처 |
|---|---|---|---|---|
| BTGenBot | 2024(IROS) | BT(XML, BehaviorTree.CPP 계열) | 7B 이하 경량 모델을 GPT-3.5 생성 데이터셋(약 600 BT)으로 파인튜닝, 정적 문법 검사 + 검증기 + 시뮬레이터 + 실로봇으로 평가 | [웹] https://arxiv.org/abs/2403.12761 |
| BTGenBot-2 | 2026-02 | XML BT, 액션 원시 목록 입력 | 10억 파라미터 오픈 모델, 52개 과제(Isaac Sim) 벤치마크, 제로샷 90.38%, 원샷 98.07%, GPT-5·Claude Opus 4.1 보다 우수, 추론 16배 빠름, 런타임 오류 복구 | [웹] https://arxiv.org/abs/2602.01870 |
| LLM-BT | 2024(ICRA) | 가변 BT | ChatGPT 가 단계 추론, 환경 변화에 새 액션 추가 | 검색 요약 https://arxiv.org/abs/2404.05134 [미확인: 본문 미열람] |
| LLM-as-BT-Planner | 2024-09(ICRA 2025) | BT | 4가지 인컨텍스트 학습 + 소형 모델 파인튜닝으로 BT 생성 성공률 향상, "modularity and flexibility" | [웹] https://arxiv.org/abs/2409.10444 |
| Dendron | 2024-04 | BT 가 LLM 에이전트의 구조적 프로그래밍 틀 | "behavior trees provide a unifying framework for combining language models with classical AI and traditional programming" | [웹] https://arxiv.org/abs/2404.07439 |
| Real-Time World Crafting | 2025-10 | **제약된 DSL → ECS 구성** | 자연어 → DSL → 런타임 ECS, LLM 출력을 엔진에서 격리. Gemini/GPT/Claude 비교: 복잡한 DSL 은 few-shot 필수, 창의성은 Chain-of-Thought | [웹] https://arxiv.org/abs/2510.16952 |
| LLMs as Planning Formalizers(서베이) | 2025 | PDDL/HTN 등 형식 계획 모델 | LLM 을 "planning formalizer" 로 두고 기성 플래너를 신뢰 실행기로 쓰는 신경-기호 파이프라인 정리 | [웹] https://arxiv.org/abs/2503.18971 |
| HTN + LLM 휴리스틱(ChatHTN 등) | 2025~2026 | HTN | LLM 근사 계획과 기호 HTN 을 교차 | 검색 요약 https://arxiv.org/pdf/2605.07707 [미확인] |
| R-HTN | 2026-02 | HTN + 내장 지시(directive) | 지시 위반 시 재계획하는 "intelligent disobedience", 게임 NPC 안전·성격 제약 | [웹] https://arxiv.org/abs/2602.00951 |
| Hybrid HTN-GOAP for Adaptive NPCs | 2024(IEEE ToG) | HTN+GOAP | GOAP 의 비반복성 + HTN 의 설계자 지정 시퀀스 결합 | 검색 요약 [미확인: 본문 미열람] |
| Enhancing Game AI Behaviors with LLMs and Agentic AI | 2025(FSE) | — | HTTP 403 | [미확인] https://dl.acm.org/doi/10.1145/3696630.3728553 |

언리얼 쪽 도구:

- **Unreal MCP(공식, 5.8 Experimental)**: 에디터 프로세스 안에 MCP 서버, `http://127.0.0.1:8000/mcp`, HTTP 전용(WebSocket/stdio 미지원), 루프백 전용·인증 없음. 도구 추가는 `UToolsetDefinition` 상속 + `UFUNCTION(meta = (AICallable))`(C++) 또는 `@toolset_registry.tool_call`(Python), 또는 `IModelContextProtocolTool` 구현 후 `IModelContextProtocolModule::GetChecked().AddTool()`. Live Coding 으로 새 함수 선언은 반영 안 됨(에디터 재시작). https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor (2026). UEFN 에도 배포(https://www.fortnite.com/news/unreal-mcp-is-now-available-in-uefn 은 403 → [미확인]).
- **Epic Developer Assistant**: 2025-09-24 공지, 5.6 은 웹 포털, 5.7 에서 에디터 내 슬라이드 패널·F1 컨텍스트 도움말(Experimental). 문서 질의·C++ 코드 생성·린팅. 에셋 직접 편집 기능은 문서상 없음. 커뮤니티에서 정확도 우려. https://forums.unrealengine.com/t/the-epic-developer-assistant-ai-powered-developer-assistant-for-unreal-engine-5-6/2659525 . 5.7 공식 뉴스 본문은 403 → 검색 요약만 [미확인].
- 커뮤니티 MCP 서버(100+ 명령, BT 편집 포함)는 존재하나 검증되지 않음(https://github.com/DeVoe09/UnrealMCP 등, 본문 미열람 [미확인]).

**생성형 AI 가 몬스터 행동을 쓰기 좋은 표현 형식에 대한 결론**

1. 성공한 연구는 예외 없이 **텍스트 직렬화 가능한 트리/규칙(XML BT, DSL)** 을 출력으로 삼고, **정적 검증기 → 시뮬레이터 → 재생성** 루프를 붙였다(BTGenBot, BTGenBot-2, Real-Time World Crafting).
2. 노드 에디터 에셋(언리얼 BT/StateTree .uasset)은 LLM 이 MCP 로 조작할 수는 있지만, 한 번의 편집에 여러 도구 호출이 필요하고 결과 검증이 스크린샷·에셋 덤프에 의존하므로 토큰이 많이 든다. 사용자의 우려와 일치.
3. 따라서 TDGame 은 **"C++ 액션 원시(primitive) 목록 + 텍스트 도메인 파일(HTN 메서드/유틸리티 규칙)"** 을 정본으로 두고, 필요하면 그 텍스트를 StateTree/BT 로 컴파일하는 방향이 유리하다. LLM 에 주는 것은 "원시 목록 + 스키마 + 예시 2~3개(few-shot)" 로 충분하다는 것이 Real-Time World Crafting 의 관찰.

---

### 4) 강화학습 몬스터의 문제와 실무 권장

| 문제 | 근거 | 대응 |
|---|---|---|
| 불투명성·설계자 통제 상실 | GT Sophy 도 스포츠맨십(공정 주행)이 "the hardest problem" 이며 시연 1주 전까지 보상 공학 반복(Sony AI 2026). Rainbow Six 봇은 "elite-player strategies" 와 초보용 전략을 분리해야 난이도가 튀지 않음(Ubisoft 2023) | 출하 몬스터는 규칙 기반 뼈대를 유지하고 RL 은 파라미터·소수 결정에 한정 |
| 밸런스 예측 불가 | EA SEED: "generalization ... remains an unsolved challenge for game AI"(2024). Learning Agents 포럼: "IL can only learn to do things that you have given them demonstrations of" | 밸런스 툴은 RL 을 "탐색기(밸런스 추정)" 로 쓰고 결과를 수치(가중치)로만 반영 |
| 재현성(결정론) | 딥 RL 비결정 요인 연구: 각 요인이 "can substantially impact the performance of agent"(https://arxiv.org/abs/1809.05676 , 2018/2019). 추론 비결정의 주원인은 배치 크기 의존(배치 불변성 실패)과 부동소수점 비결합성(Thinking Machines 2025). ONNX Runtime GPU 비결정 이슈(https://github.com/microsoft/onnxruntime/issues/4611 , 본문 미열람 [미확인]) | (a) 시드 고정: `MakePolicy(Seed)`, `train_ppo.py:59-60`; (b) 노이즈 0: `RunInference(0.0f)`; (c) CPU 추론 고정(NNERuntimeBasicCpu 또는 ORT CPU), 배치 크기 고정; (d) 동일 빌드·동일 명령셋(ISPC 경로 `NNERuntimeBasicCpuModel.cpp:9`) 또는 ISPC 비활성 빌드로 검증; (e) 시뮬레이터에서 정책 출력을 로그로 남겨 회귀 비교 |
| 학습 비용·인프라 | GT Sophy 1,000+ PS4; Roller Champions 1~4일/모델; Unreal-MAP 논문 검색 요약에 "8×RTX3090 서버로 50 과제(각 20 에이전트) 100k 에피소드 3시간" [미확인: 본문에 수치 없음] | 헤드리스·렌더링 끔(`LearningAgentsTrainer.h:126-127` 권고)·시간 팽창(time dilation) 병렬 인스턴스 |
| 실무 권장 | EA(IL 20분 vs RL 5시간), 텐센트(자기대전 밸런스 추정), 넷이즈(퀘스트 회귀 테스트), Ubisoft(경로·경계 검증 봇), 시장 보고서: 밸런스 테스트 침투율 43%(탐색 단계) vs 기능 테스트 72~78%(wetest 2026) | 우선순위 표(결론 요약)와 동일 |

#### 4-1. 결정론 유지 절차(제안, 엔진 사실 기반)

| 단계 | 조치 | 근거 |
|---|---|---|
| 1 | 학습 시드 고정: 트레이너 설정의 seed 를 밸런스 시나리오 ID 에서 유도 | `train_ppo.py:59-60` (`np.random.seed`, `torch.manual_seed`) |
| 2 | 정책 생성 시드 고정: `MakePolicy(..., Seed)` 를 시나리오 시드로 | `LearningAgentsPolicy.h:117` |
| 3 | 추론 노이즈 0: `RunInference(0.0f)` — 주석 "always sample the mean (expected) action" | `LearningAgentsPolicy.h:183-192` |
| 4 | 추론 백엔드 CPU 고정, 배치 크기 고정(에이전트 수가 달라도 패딩으로 동일 배치) | NNE 배치 권장, Thinking Machines 2025 배치 불변성 |
| 5 | 같은 머신 검증: 같은 입력 2회 → 정책 출력 바이트 동일 확인(자동화 테스트) | 프로젝트 자동화 테스트 픽스처 `FTDScopedCombatWorld` 재사용 |
| 6 | 다른 머신 검증: ISPC 경로 차이 가능성 → 허용 오차 통계 비교(repeatability) 또는 ISPC 비활성 빌드로 비트 동일성 확인 | `NNERuntimeBasicCpuModel.cpp:9-10` (`INTEL_ISPC` 매크로, 주석 처리된 `0` 대안) |
| 7 | 정책 파일·시드·장비 조건·엔진 빌드 해시를 리포트에 함께 기록 | 재현성 논문(Nagarajan 2018)의 "same code + same machine + same seed" 정의 |

#### 4-2. 헤드리스 학습 실행 체크리스트(엔진 사실 기반)

1. `-game` 실행 또는 별도 커맨드릿에서 `SetMaxAgentNum` 으로 병렬 에이전트 수 확장(`LearningAgentsManager.h:54,233` 기본값 1).
2. 쿠킹 빌드면 `NonEditorEngineRelativePath`, `NonEditorIntermediateRelativePath` 지정(`LearningAgentsTrainer.h:139-149`).
3. 렌더링·사운드 비활성(`LearningAgentsTrainer.h:126-127` 권고), 시간 팽창으로 경험 수집 가속.
4. 통신 방식 선택: 로컬은 공유 메모리(지연 최소), 원격 학습 서버는 소켓(`LearningAgentsCommunicator.h:28,53`).
5. `bUseTensorboard = true` 로 학습 곡선 기록(`LearningAgentsPPOTrainer.h:241`).
6. 모방학습은 `TrainerCommunicationTimeout`(기본 10초, `LearningAgentsImitationTrainer.h:37`) 을 대형 기록에 맞춰 상향.

추가로 2025~2026 연구 흐름: 인간 데이터 없이 행동 벡터(공격성·협력성)를 조건으로 넣어 **제어 가능한 다양성** 을 얻는 PPO 프레임워크(https://arxiv.org/abs/2512.10835 , 2025-12, Unity 커스텀 게임), 호기심 기반 다중 에이전트 RL 테스트 cMarlTest(https://arxiv.org/abs/2502.14606 , 2025) 등 — 모두 "플레이어 대리·QA" 축.

---

### 5) 신경망 추론을 런타임 CPU 에서 수백 마리에 돌린 사례와 비용

| 사례 | 연도 | 환경 | 수치 | 출처 |
|---|---|---|---|---|
| One Policy, Infinite NPCs(PCSP) | 2026-05 | **UE 5.5, NNE/NNERuntimeORT, standalone -game**, 207k 파라미터 MLP(256-256-128), 페르소나 임베딩 조건부 공유 정책 | "mean per-call latency 183–202 µs through n=64", 프레임 오버헤드 약 0.27ms/마리(추론 외 요인), n>96 에서 NavMesh FindPath 포화, n=128 에서 BT 중단율 44.9%, 64 마리 실패율 0.0% | [웹] https://arxiv.org/html/2605.23652v1 |
| Arm Candy Clash | 2023-09 | Unity ML-Agents, Pixel 7 Pro CPU, 100 마리 | MLP 대비 LSTM "approximately 1.6 times slower", 둘 다 60 FPS 근처, LSTM 승률 63.6~90.9% | [웹] https://developer.arm.com/community/arm-community-blogs/b/ai-blog/posts/neural-network-models-for-multi-agent-games-on-arm |
| Google Research Football 다중 에이전트 | 2023 | 배치 정책 추론 | 실행 시간 중 게임 엔진 73%, 배치 정책 추론 9.5% | 검색 요약 https://arxiv.org/pdf/2305.09458 [미확인] |
| 언리얼 NNE 공식 지침 | 2026 열람 | INNERuntimeCPU: 게임 스레드 동기 또는 비동기 태스크; 권장 "batching of input data since it's more performant to evaluate a batch than to run multiple individual calls" | 수치 없음 | [웹] https://dev.epicgames.com/documentation/unreal-engine/neural-network-engine-overview-in-unreal-engine |
| Intel OpenVINO NNE 런타임 | 2025 | INNERuntimeCPU 구현, CPU/GPU/NPU, ONNX·IR, Windows/Linux | 수치 없음 | [웹] https://www.intel.com/content/www/us/en/developer/articles/guide/accelerated-ai-inference-for-unreal-engine-5.html |

해석: 200k 파라미터급 MLP 는 마리당 0.2ms 수준이므로 300 마리를 매 틱 돌리면 약 60ms — 불가능. 그러나 **결정 주기를 5~10 틱에 한 번(마리당 0.02~0.04ms/틱 환산)으로 낮추고 배치 추론** 하면 예산 안에 든다. 실제 병목은 길찾기·BT 중단이므로 TDGame 의 "틱 조절·대량 최적화" 설계는 신경망 유무와 무관하게 **결정 주기 분산 + 길찾기 요청 상한** 이 핵심이다.

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

### 쓰기

1. **결정론적 전투 시뮬레이터의 플레이어 대리로 행동 복제(BC) 정책**
   - 데이터: `FTDScopedCombatWorld` 픽스처 위에서 사람이 조작한 전투를 `ULearningAgentsRecorder` 로 기록(장비·물약·버프 조건별로 태그).
   - 학습: `ULearningAgentsImitationTrainer`(기본 `learning_core.train_behavior_cloning`), 헤드리스(-game, 렌더링 끔) + `NonEditorEngineRelativePath` 지정.
   - 추론: `RunInference(0.0f)`, `MakePolicy(..., Seed 고정)`, NNERuntimeBasicCpu. 동일 입력 → 동일 출력 검증 테스트를 자동화 테스트에 추가하고, CI 머신 명령셋(AVX2 등)을 고정.
   - 이유: 밸런스 데이터는 "실제 플레이어 분포" 가 있어야 의미 있고, IL 은 설계자가 직접 가르칠 수 있으며 학습이 분 단위다(EA SEED).
2. **LLM 텍스트 DSL → 검증기 → 시뮬레이터 루프로 몬스터 행동 제작·분석**
   - C++ 에 액션 원시(접근, 후퇴, 스킬 사용, 대기 등)와 조건 원시를 등록하고 이름·인자 스키마를 JSON 으로 덤프하는 커맨드릿을 둔다.
   - 몬스터 행동은 텍스트(HTN 메서드 또는 유틸리티 규칙)로 정본화. LLM 프롬프트는 "스키마 + few-shot 2~3개" (Real-Time World Crafting 관찰).
   - 검증기: 스키마·참조 무결성 → 헤드리스 시뮬레이터 N 회 실행 → 승률·시간·피해 리포트를 LLM 에 되돌림(BTGenBot 류 루프).
   - Unreal MCP 는 "시뮬레이터 실행·리포트 조회·테스트 실행" 도구를 `AICallable` 로 노출하는 데만 쓰고, 노드 에셋 편집에는 쓰지 않는다.
3. **RL 은 밸런스 탐색·QA 에 한정**
   - 자기대전/커리큘럼으로 "이 장비 세트로 이 몬스터 10마리를 이길 수 있는가" 를 탐색(텐센트 Juewu 방식의 축소판).
   - 결과는 유틸리티 가중치·쿨다운 같은 **파라미터** 로만 몬스터에 반영하여 설계자 통제와 결정론을 유지.
   - 상위 몬스터(보스·엘리트) 소수에 한해 페르소나 조건부 공유 정책(PCSP) 검토 — 마리당 0.2ms, 64 마리 실증.

### 피하기

- **수백 마리 전부를 신경망 정책으로**: 추론 자체보다 길찾기·BT 중단이 먼저 무너진다(PCSP 2026). 규칙 기반 + 주기 분산이 기본.
- **온디바이스 SLM(NVIDIA ACE) 로 몬스터 사고**: RTX GPU 전제, 2B 모델도 1.5GB VRAM, 동료 1명·보스 1명 규모.
- **세계 모델·범용 에이전트(Muse, Genie 3, SIMA 2)** 를 런타임 도구로 기대: 2025~2026 모두 연구 프리뷰·발상 도구.
- **Learning Agents 를 출하 경로의 유일한 두뇌로**: Experimental 0.2 + NNERuntimeBasicCpu 0.1 + 5.5 이후 공식 문서 정체. 학습 파이프라인은 교체 가능하게(ONNX 로 내보내 NNERuntimeORT/OpenVINO 로도 실행) 설계.
- **GPU 추론으로 결정론 기대**: 배치 크기·커널 선택에 따라 결과가 바뀐다(Thinking Machines 2025). 시뮬레이터는 CPU 고정.

### 사용자가 생각하지 못했을 추가 고려사항

1. **ISPC 경로 때문에 CPU 간 비트 동일성이 깨질 수 있다** — 시뮬레이터 결정론 검증은 "같은 머신" 과 "다른 머신" 두 단계로 나누고, 후자는 허용 오차 기반 통계 비교(repeatability)로 정의해야 한다.
2. **Mass 개편(5.8)** 이 대량 몬스터 최적화의 실제 도구다 — 릴리스 노트: 게임 스레드 밖 엔티티 생성, 희소 프래그먼트, MassCore. 신경망 결정도 Mass 프로세서에서 배치로 돌리면 NNE 권장(배치 추론)과 맞아떨어진다.
3. **StateTree 5.8 의 시작 상태·컴파일러 매니저** 는 텍스트 DSL → StateTree 자동 컴파일의 발판이 될 수 있다(릴리스 노트 확인, 상세 API 는 별도 조사 필요).
4. **LLM 보상 함수 생성** (Sony AI 2025)은 RL 밸런스 탐색에서 "설계 의도를 텍스트로 쓰면 보상 코드가 나오는" 워크플로로 재사용 가능.
5. **데이터 거버넌스**: 플레이어 시연 기록(BC 데이터)은 버전·시드·장비 조건과 함께 아카이브해야 밸런스 리포트가 재현된다.

---

## 미확인·미해결 질문

1. Learning Agents 의 FlowMatching·Gym·EntitiesManager·Replay 모듈이 각각 어느 버전(5.6/5.7/5.8)에서 추가되었는지 — 릴리스 노트에 없고 공식 튜토리얼 페이지는 자바스크립트 렌더링으로 본문을 못 받았다. 로컬 헤더 존재만 확인.
2. Learning Agents 로 출하한 상용 게임 사례 유무 — 검색으로는 발견되지 않았다.
3. Epic 공개 로드맵의 Learning Agents 상태(제품화 예정 여부) — 페이지 본문 잘림.
4. NNERuntimeBasicCpu 의 ISPC 경로가 실제로 AVX2/AVX-512 간 결과 차이를 내는지 — 실측 필요(소스에서 ISPC 사용만 확인).
5. Unreal-MAP 의 학습 처리량 수치(8×RTX3090, 3시간)는 검색 요약에만 있고 초록에는 없었다.
6. MIR5 Asterion 보스의 실제 학습 방식(온디바이스 메모리 기반 적응 vs 사전 학습) — 보도자료 403.
7. Epic Developer Assistant 의 기반 모델(GPT-4.1 이라는 2차 출처 주장)과 에셋 편집 가능 여부 — 공식 본문 403.
8. 넷이즈 "400 intelligent NPCs" 사례의 기술 구성(LLM 인지 RL 인지) — 검색 요약만.
9. LLM 이 BT 대신 **HTN 도메인** 을 생성했을 때의 정량 성공률 — 서베이(2025)는 파이프라인만 정리, 게임용 수치는 미발견.
10. ONNX Runtime CPU 의 세션 옵션으로 완전 결정론을 보장하는지 — 관련 이슈만 검색, 본문 미열람.

### 출처 신뢰도 등급

| 등급 | 뜻 | 해당 출처 |
|---|---|---|
| A | 1차 출처 본문 확인(공식 문서·논문·엔진 소스) | 엔진 소스 전부, Epic 문서, arXiv 논문, Sony AI·EA·NVIDIA·Microsoft·DeepMind 공식 블로그 |
| B | 1차 출처이나 요약 성격(포럼·뉴스룸) | Epic 포럼 스레드, Ubisoft 뉴스, 텐센트 GDC 기사, Unity 포럼 |
| C | 2차 출처(시장 보고서·매체) | wetest 2026, 36kr 2026, MIT Tech Review 2025, mike.gold 노트 |
| D | 검색 요약만(본문 미열람) | [미확인] 표기 항목 전부 — 설계 결정 근거로 쓰지 말 것 |

### 열람한 출처 목록(본문 확인 34개)

Epic: 5.5 릴리스 노트, 5.8 릴리스 노트, Unreal MCP 문서, NNE 개요, Learning Agents 5.8 플러그인 색인, 포럼(Learning Agents 소개 2023, 5.5 튜토리얼 스레드, Epic Developer Assistant 2025) / Sony AI 5주년(2026), GT7 비전 에이전트(arXiv 2025) / EA SEED 3건(2023 테스트 연구, 2023 설계 검증, 2024 WCCI) + arXiv 2307.11105, 2606.20210 / Ubisoft Rainbow Six(2023), Roller Champions(arXiv 2020) / 텐센트 GDC 2024, 36kr 2026, wetest 2026 / NVIDIA 3건(2025-01, 2025-10, 2026-06) / Microsoft Muse(2025) / DeepMind Genie 3, SIMA 2, MIT Tech Review(2025) / Unity 포럼 4.0, GitHub 릴리스 / BTGenBot(2024), BTGenBot-2(2026), LLM-as-BT-Planner(2024), Dendron(2024), Real-Time World Crafting(2025), Planning Formalizers 서베이(2025), R-HTN(2026) / One Policy Infinite NPCs(2026), Arm 블로그(2023), Intel OpenVINO / AMD Schola v2(2025), Unreal-MAP(2025), cMarlTest(2025), Nagarajan 결정론(2018), Thinking Machines(2025), XanderBert GitHub(2024), mike.gold 5.4 노트(2024).

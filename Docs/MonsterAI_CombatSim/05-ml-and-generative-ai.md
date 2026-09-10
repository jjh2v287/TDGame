[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 05. 머신러닝과 생성형 AI 통합

용어: ML(Machine Learning, 머신러닝), RL(Reinforcement Learning, 강화학습), BC(Behavior Cloning, 행동 복제 = 모방학습), PPO(Proximal Policy Optimization, 근접 정책 최적화), LLM(Large Language Model, 대규모 언어 모델), SLM(Small Language Model, 소형 언어 모델), NNE(Neural Network Engine, 언리얼 신경망 추론 계층), ONNX(Open Neural Network Exchange, 신경망 교환 형식), MCP(Model Context Protocol, 모델 컨텍스트 프로토콜), CMA(Covariance Matrix Adaptation, 공분산 행렬 적응 최적화), PSO(Particle Swarm Optimization, 입자 군집 최적화), ISPC(Intel SPMD Program Compiler, 인텔 SIMD 컴파일러), DSL(Domain Specific Language, 도메인 특화 언어), CI(Continuous Integration, 지속 통합), GE(Gameplay Effect, 게임플레이 효과), GRU(Gated Recurrent Unit, 게이트 순환 유닛), ODE(Ordinary Differential Equation, 상미분 방정식), LFS(Git Large File Storage, 대용량 파일 저장소), PIE(Play In Editor, 에디터 내 실행), MLP(Multi-Layer Perceptron, 다층 퍼셉트론), ORT(ONNX Runtime, ONNX 실행기), ISA(Instruction Set Architecture, 명령어 집합 구조), CUDA(NVIDIA 병렬 연산 플랫폼), VRAM(Video RAM, 그래픽 메모리), UHT(Unreal Header Tool, 리플렉션 코드 생성기).

## 이 문서가 답하는 질문

1. 2024~2026 산업은 몬스터를 생성형 AI·ML 로 어떻게 다루고 있고, 왜 TDGame 은 몬스터 두뇌 자체를 신경망으로 만들지 않는가?
2. 언리얼 5.8 Learning Agents 는 어떤 구조이며 우리가 쓸 부분과 쓰지 않을 부분은 무엇인가?
3. ML 을 적용하는 두 지점(플레이어 대리 봇, `tune` 잎 튜닝)은 구체적으로 어떻게 설계·실행·재현하는가?
4. LLM(생성형 AI 에이전트)이 몬스터를 제작·분석하는 루프는 정확히 어떤 명령 순서이고 정확도 근거는 무엇인가?
5. 학습이 끼어들어도 [04 시뮬레이터](04-combat-simulator.md)의 결정론을 어떻게 지키는가?

## 결론 요약(결정 문장)

- 몬스터 두뇌는 [01 문서](01-ai-model-decision.md)의 코드 정의 유틸리티 AI + FSM 이며 **신경망·SLM·세계 모델로 대체하지 않는다**(결정 기록 D36). 이유는 텍스트 제작성 상실, 대량 비용, 결정론, 두 개의 실험 플러그인 의존, 설계자 통제 상실 다섯 가지다.
- ML 은 두 지점에만 쓴다(D36). (1) **플레이어 대리 봇** 규칙 페르소나 → BC(`ULearningAgentsRecorder` → `ULearningAgentsImitationTrainer`) → PPO 는 상한 탐색. (2) **JSON `tune:true` 잎 벡터의 CMA/PSO 블랙박스 튜닝**(`UE::Learning::FCMAOptimizer`/`FPSOOptimizer`), 목적 함수는 시뮬 승률 밴드.
- 학습은 외부 파이썬 프로세스에서 돌고 비결정을 허용한다. **추론과 시뮬은 결정적**이어야 하며, 정책 파일 해시(ph)를 정의 해시(dh)·빌드 해시와 함께 모든 결과에 기록한다.
- LLM 제작 루프는 "스키마 덤프 읽기 → JSON 작성 → 검증 3단 → 100~200 시드 배치 → 30줄 요약 → 수정"이며 종당 컴파일 0회다(D37). MCP 툴셋은 같은 세션 코드를 부르는 보조 채널이고 `TDGameEditor` 모듈이 생긴 뒤에만 추가한다(D13). Epic AIAssistant 는 쓰지 않는다.
- MLAdapter 는 채택하지 않는다. MLflow 는 실험 추적용 옵션이다.

---

## 1. 산업 현황 2024~2026 과 "왜 두뇌를 신경망화하지 않는가"

### 1.1 현황 요약(조사 web-ml-generative-npc 결론 1~8)

| 주제 | 사실 | 우리에게 주는 뜻 | 근거 |
|---|---|---|---|
| 언리얼 Learning Agents | 5.8 에서도 Experimental 버전 0.2, 추론은 NNERuntimeBasicCpu(0.1, Experimental) 전용 | 출하 두뇌를 여기에 걸면 실험 플러그인 두 개에 의존 | web-ml 결론 1, `LearningAgents.uplugin:4,16`, `NNERuntimeBasicCpu.uplugin:4,16` |
| 학습 위치 | 엔진 밖 파이썬(PyTorch) 자식 프로세스, 공유 메모리 또는 소켓 | 헤드리스 시뮬과 결합하려면 파이썬 경로 설계가 필요 | web-ml 결론 2, `LearningAgentsCommunicator.h:28,53,135` |
| 추론 결정론 | 시드 고정 + ActionNoiseScale 0 + 같은 빌드·같은 CPU 명령셋 세 조건 | ISPC 경로 때문에 다른 명령셋 간 비트 동일은 미보장 | web-ml 결론 3, `LearningAgentsPolicy.h:117,183-186`, `NNERuntimeBasicCpuModel.cpp:9` |
| 업계 실무 | EA SEED 모방학습 20분 vs 강화학습 5시간(2023); 텐센트 자기대전으로 승률 95% 정확도 사전 추정(2026, 2차 출처); RL 을 적으로 출하한 GT Sophy 는 PS4 1,000대 이상 인프라(2026) | RL 은 "적 두뇌"보다 "플레이어 대리·QA·밸런스 추정"에 먼저 쓴다 | web-ml 결론 4 |
| 신경망 추론 비용 | UE 5.5 + NNERuntimeORT, 207k 파라미터 MLP 64마리에서 호출당 183~202µs; 병목은 추론이 아니라 NavMesh 길찾기(2026) | 소형 MLP 는 수백 마리 가능하나 사고 주기 분산·길찾기 예산이 진짜 변수 | web-ml 결론 5 |
| 생성형 AI 제작 형식 | BTGenBot-2 가 XML BT 를 제로샷 90.38%·원샷 98.07% 생성(2026); Real-Time World Crafting 은 LLM 출력을 제한 DSL 로만 받음(2025) | "제약된 텍스트 DSL + 정적 검증기 + 시뮬 피드백" 이 정답 형식 | web-ml 결론 6 |
| 온디바이스 SLM(NVIDIA ACE) | RTX GPU 전제, 2B 모델도 약 1.5GB VRAM, 동료 1명·보스 1명 규모(2025) | 탑다운 대량 몬스터에 부적합 | web-ml 결론 7 |
| 세계 모델·범용 에이전트 | Muse 300×180 단일 게임, Genie 3 약 1분 기억, SIMA 2 제한 연구 프리뷰(2025) | 런타임 몬스터 제어 도구가 아님 | web-ml 결론 8 |

### 1.2 몬스터 두뇌를 신경망화하지 않는 다섯 가지 이유

| # | 이유 | 수치·근거 | 구분 |
|---|---|---|---|
| 1 | **텍스트 제작성 상실** — 가중치는 LLM 이 읽고 고칠 수 없다. [02 문서](02-architecture-and-definition-format.md)의 "JSON 한 파일이 정본" 원칙(D6)과 D37 의 컴파일 0회 루프가 깨진다 | engine-learning-agents-ml 6절 권장 3(b) "디버깅·수정이 텍스트로 불가능" | 사실 |
| 2 | **대량 비용** — 마리당 약 0.2ms 이면 300마리 매 스텝 약 60ms. 10Hz 사고로 낮춰도 초당 600ms, 60fps 기준 프레임당 평균 약 10ms(1/64 스텝 기준 약 9.4ms)로 [03 문서](03-tick-and-scale.md) 단계 A 의 몬스터 몫 상한 4.0ms(D24)를 넘는다 | web-ml 결론 5, 5절 해석 | 추정(산술) |
| 3 | **결정론** — 추론은 같은 빌드·같은 명령셋에서만 비트 동일. 시뮬-실기 A단(비트 동일, D32)이 CPU 종류에 종속된다 | web-ml 결론 3, engine-learning-agents-ml 2절 "다른 ISA 간 미확인" | 사실 + 미확인 |
| 4 | **실험 플러그인 의존** — Learning Agents 0.2 + NNERuntimeBasicCpu 0.1, 5.6~5.8 변경은 유지보수 수준, 5.8 Add/Remove 버그에 Epic 답변 없음 | web-ml 결론 1, web-ue-5-6-to-5-8-ai-changes 결론 3 | 사실 |
| 5 | **설계자 통제·밸런스 예측 상실** — GT Sophy 도 스포츠맨십이 가장 어려웠고, EA SEED 는 일반화를 "미해결"로 보고. 밸런스 툴은 몬스터 행동이 설명 가능해야 승률 변화를 원인까지 추적한다 | web-ml 4절 표 | 사실 |

심사 3인은 "신경망 몬스터 두뇌 미채택, BC 플레이어 봇 + CMA/PSO 튜닝, RL 은 탐색" 으로 수렴한 네 설계안에 동의했다(심사 판정 "ML 몬스터 두뇌"). 보스 소수 신경망은 Phase 4 이후 선택지로만 남긴다(8절).

---

## 2. Learning Agents 5.8 구조표(engine-learning-agents-ml 결론 1~8)

| 항목 | 사실 | 우리 결정 | 근거(엔진 파일:줄) |
|---|---|---|---|
| 학습 구조 | 게임 프로세스가 `python.exe train.py ... SharedMemory ...` 를 스폰, 리플레이 버퍼·가중치를 공유 메모리 또는 소켓으로 교환 | 학습은 항상 별도 프로세스에서 | 결론 1, `LearningExternalTrainer.cpp:80-91` |
| 파이썬 선행 조건 | 실행 파일이 `<프로젝트>/Intermediate/PipInstall/Scripts/python.exe` 로 고정, 에디터의 PythonScriptPlugin 이 pip 로 생성 | 에디터 1회 실행으로 PipInstall 선행(`-ForcePipInstallOnInit`) | 결론 1, `LearningTrainer.cpp:1640-1644`, `PipInstall.cpp:183`, `PythonScriptPlugin.cpp:1361-1368` |
| 비에디터 학습 | 학습 모듈은 Runtime 타입, `-game` 에서도 `NonEditor*Path` 를 채우면 가능한 코드 경로가 있다(실행 검증은 미확인, engine-learning-agents-ml 미확인 1·6). 또는 `bUseExternalTrainingProcess=true` + Socket 으로 외부에서 미리 띄운 트레이너에 접속 | CI 는 Socket + 외부 트레이너(파이썬 경로 하드코딩 회피) | 결론 1, `LearningAgentsTrainer.h:139,147`, `LearningAgentsCommunicator.h:73` |
| 관측·행동 스키마 | `ULearningAgentsInteractor` 의 `Specify/Gather/Perform` 이 `BlueprintNativeEvent` → C++ `_Implementation` 오버라이드. 빌더는 정적 함수 | 전부 C++, 에디터 노드 없음(3.2절) | 결론 2, `LearningAgentsInteractor.h:81-82,92,116-117,128`, `LearningAgentsObservations.h:398,645`, `LearningAgentsActions.h:329` |
| 추론 런타임 | `GetRuntime<INNERuntimeCPU>("NNERuntimeBasicCpu")`; 에이전트별 uint32 시드; `MakePolicy(..., Seed=1234)`; `RunInference(0.0f)` 면 연속 = 평균, 이산 = argmax 로 난수 미사용 | CPU 고정, 노이즈 0 | 결론 3, `LearningNeuralNetwork.cpp:162-168`, `LearningAgentsPolicy.cpp:427-429,776-786`, `Learning.ispc:305-311` |
| 배치 추론·주기 | `RunInference()` 1회로 전 에이전트를 한 텐서로 평가; 매니저 `TickComponent` 는 리스너 알림만 하고 추론 시점은 게임 코드가 결정 | 우리 틱 함수의 대리 봇 단계에서 호출(3.4절) | 결론 4, `LearningAgentsPolicy.cpp:649-712`, `LearningAgentsManager.cpp:37-49` |
| 스냅샷 형식 | `ULearningAgentsNeuralNetwork`(UDataAsset) + 바이너리 스냅샷(매직 0x1e9b0c80, 버전 1), 파일 타입 `ubnne`. **ONNX 가 아니다**. ONNX 는 별도 `NNERuntimeORT`(베타) | 스냅샷 파일을 정본으로, ORT 는 옵션(9절) | 결론 5, `LearningAgentsNeuralNetwork.h:75,95,103,130`, `NNERuntimeBasicCpu.cpp:20` |
| Replay 모듈 | `LearningAgentsReplay` 는 DemoNetDriver 데모 리플레이 래퍼이지 RL 리플레이 버퍼가 아님. 학습 데이터 녹화는 `ULearningAgentsRecorder/Recording`(매직 0x06b5fb26) | Replay 모듈 미사용, Recorder 사용 | 결론 6, `LearningAgentsReplaySubsystem.h:58,78-90`, `LearningAgentsRecording.cpp:27-28` |
| MLAdapter | 버전 0.0.1 정체, rpclib + 구형 `gym`, 수동 틱은 `Sleep(0)` 바쁜 대기, 월드 틱을 멈추지 못함 | 채택하지 않음(D4·D36) | 결론 7, `MLAdapterManager.cpp:263-274`, `MLAdapterManager_Server.cpp:35-46` |
| 최적화기 | LearningCore `FCMAOptimizer`/`FPSOOptimizer`: `Resize → Reset(OutSamples, InitialGuess) → Update(InOutSamples, Losses)` 인터페이스, 파이썬·GPU 불필요 | `tune` 잎 튜닝 엔진(4절) | 결론 8(b), `LearningOptimizer.h:13-25`, `LearningCMAOptimizer.h:30-32` |
| 5.8 Add/Remove 버그 | `RemoveAgent` 후 `AddAgent` 하면 재추가 에이전트의 경험이 영구히 버려짐(2026-08-17 포럼, Epic 답변 없음) | 에이전트 ID 를 에피소드마다 유지하고 상태만 리셋 | web-ue-5-6-to-5-8-ai-changes 결론 3 |
| 트레이너의 고정 스텝 강제 | 학습 시작 시 `bUseFixedTimeStep=true`, `FixedTimeStepFrequency=60.0f` 를 적용하고 종료 시 복원 | 세션이 `FApp` 시간을 소유한다(D27, 매 스텝 `FApp::SetDeltaTime`). 학습 모드에서는 `bUseFixedTimeStep=false`·`bSetMaxPhysicsStepToFixedTimeStep=false` 로 트레이너의 전역 변경(고정 스텝·물리 최대 스텝·MaxFPS)을 끄고, 켜야 할 경우에만 64.0f 로 [04 문서](04-combat-simulator.md)의 1/64 스텝과 맞춘다(전역 소유 순서 확인은 Phase 4 과제) | `LearningAgentsTrainer.h:36,44,46-50` (직접 확인) |
| 정책 기본 구조 | 은닉 1×128 + GRU 메모리 64. `NoMemoryCell` 은 `MemoryStateSize=0` 과 동치 | `MemoryStateSize=0` (D36) | `LearningAgentsPolicy.h:48-54` (직접 확인) |

---

## 3. 적용 지점 1 — 플레이어 대리 봇 3단

### 3.1 3단 구성

| 단 | 두뇌 | 용도 | 결정론 | 도입 시점(로드맵 [07](07-roadmap-and-tasks.md)) |
|---|---|---|---|---|
| 1단 규칙 페르소나 | `FTDPlayerPersona`(반응 지연, 물약 임계, 회피 확률, 공격성)를 D12 대로 몬스터와 **같은 JSON 형식**으로 작성 | 기본 대리 플레이어. King 방식의 "사람처럼 실수하는 봇" 값싼 근사 | 완전 결정(PlayerProxy 스트림만 사용, D29) | Phase 1 최소판 `Persona_Default.json`(M1-12), Phase 2 3종(M2-08) |
| 2단 BC 정책 | 기획자 플레이 녹화 → `ULearningAgentsImitationTrainer` → 스냅샷 | "사람 같은" 기준선. 인간 승률과의 회귀 사상은 실 플레이 데이터가 쌓인 뒤 | 추론 결정(3.5절) | Phase 4 |
| 3단 PPO 정책 | `ULearningAgentsPPOTrainer` 로 같은 시나리오에서 "최적 플레이" 상한 | 2단과 3단 승률 사이를 난이도 밴드로 사용 | 추론 결정, 학습 비결정 | Phase 4 조건부 |

근거: web-balance-simulation-tools 결론 9(규칙 → 모방 → 강화 순서, King 2018·EA SEED), engine-learning-agents-ml 결론 8(a)(c).

세 단 모두 같은 인터페이스 `ITDPlayerProxyBrain::Think(const FTDBrainInputs&, FTDPlayerCommand&)` 를 구현하고, `UTDMonsterThinkSubsystem` 의 단일 틱 함수(D15) 안 "대리 봇 사고" 단계에서 호출된다. think 주기는 페르소나 JSON 의 `think_hz`([02 문서](02-architecture-and-definition-format.md) 규칙 15, §6 예시는 16Hz = 4스텝)를 따르고, 생략 시 [03 문서](03-tick-and-scale.md) 1.3절 주기표의 L0 think 값(기본 6스텝, ≈10.7Hz)이다. 대리 봇은 `SimulationId 0` 슬롯으로 몬스터와 같은 주기표 경로를 쓴다([04 문서](04-combat-simulator.md) 2.3절). 대리 봇 전용 주기를 따로 두려면 주기표에 `player_think` 항목을 추가하고 03·04 문서에 반영한다. 시뮬 러너는 Step 을 직접 부르지 않는다.

### 3.2 관측·행동 스키마(C++, `FTDBrainInputs` 재사용)

서브시스템은 스텝 시작 시 플레이어 엔티티에도 `FTDBrainInputs` 스냅샷(자기 체력 비율·물약 수·어빌리티 준비 여부·가까운 몬스터 N마리의 상대 위치·체력 비율·FSM 상태·종 ID)을 채운다. 규칙 페르소나, BC 정책, 녹화기가 **같은 스냅샷**을 읽으므로 입력 표면이 하나다(D7 의 "입력 함수는 스냅샷만 읽는다" 규칙을 대리 봇에도 적용).

```cpp
UCLASS()
class UTDPlayerProxyInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()
public:
	static constexpr int32 MaxObservedMonsters = 16;
	static constexpr int32 MaxAbilitySlots = 6;
	virtual void SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutElement, ULearningAgentsObservationSchema* Schema) override;
	virtual void GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutElement, ULearningAgentsObservationObject* Object, const int32 AgentId) override;
	virtual void SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutElement, ULearningAgentsActionSchema* Schema) override;
	virtual void PerformAgentAction_Implementation(const ULearningAgentsActionObject* Object, const FLearningAgentsActionObjectElement& Element, const int32 AgentId) override;
	const FTDBrainInputs* Inputs = nullptr;
	FTDPlayerCommand* OutCommand = nullptr;
};

void UTDPlayerProxyInteractor::SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutElement, ULearningAgentsObservationSchema* Schema)
{
	TMap<FName, FLearningAgentsObservationSchemaElement> Self;
	Self.Add(TEXT("HealthRatio"), ULearningAgentsObservations::SpecifyContinuousObservation(Schema, 1));
	Self.Add(TEXT("PotionCount"), ULearningAgentsObservations::SpecifyContinuousObservation(Schema, 1));
	Self.Add(TEXT("AbilityReady"), ULearningAgentsObservations::SpecifyContinuousObservation(Schema, MaxAbilitySlots));
	TMap<FName, FLearningAgentsObservationSchemaElement> Monster;
	Monster.Add(TEXT("Offset"), ULearningAgentsObservations::SpecifyLocationObservation(Schema));
	Monster.Add(TEXT("HealthRatio"), ULearningAgentsObservations::SpecifyContinuousObservation(Schema, 1));
	Monster.Add(TEXT("FsmState"), ULearningAgentsObservations::SpecifyExclusiveDiscreteObservation(Schema, 5));
	const FLearningAgentsObservationSchemaElement MonsterStruct = ULearningAgentsObservations::SpecifyStructObservation(Schema, Monster);
	TMap<FName, FLearningAgentsObservationSchemaElement> Root;
	Root.Add(TEXT("Self"), ULearningAgentsObservations::SpecifyStructObservation(Schema, Self));
	Root.Add(TEXT("Monsters"), ULearningAgentsObservations::SpecifySetObservation(Schema, MonsterStruct, MaxObservedMonsters));
	OutElement = ULearningAgentsObservations::SpecifyStructObservation(Schema, Root);
}

void UTDPlayerProxyInteractor::SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutElement, ULearningAgentsActionSchema* Schema)
{
	TArray<float> Prior;
	Prior.Init(1.0f / (MaxAbilitySlots + 2), MaxAbilitySlots + 2);
	TMap<FName, FLearningAgentsActionSchemaElement> Root;
	Root.Add(TEXT("Slot"), ULearningAgentsActions::SpecifyExclusiveDiscreteAction(Schema, MaxAbilitySlots + 2, Prior));
	Root.Add(TEXT("MoveDir"), ULearningAgentsActions::SpecifyDirectionAction(Schema));
	OutElement = ULearningAgentsActions::SpecifyStructAction(Schema, Root);
}
```

| 설계 규칙 | 이유 | 근거 |
|---|---|---|
| `Slot` 이산 행동 = {없음, 물약, 어빌리티 0~5} 8칸 고정 | 스키마 호환 해시가 바뀌면 기존 정책·녹화가 무효화되므로 칸 수를 고정 | `LearningAgentsInteractor.h:199-203` (`OutObservationCompatibilityHash`) |
| `Monsters` 집합 `MaxNum=16`, 거리 오름차순(동률은 SimulationId 오름차순, D30)으로 16마리를 고른 뒤 SimulationId 오름차순으로 집합에 넣는다 | 집합 관측은 MaxNum 고정 필수, 선택 순서가 결정적이어야 해시가 안정 | `LearningAgentsObservations.h:645`, D30 |
| 쓸 수 없는 어빌리티는 `MakeAgentActionModifier` 로 마스크 | 쿨다운 중 스킬을 고르는 낭비 제거 | engine-learning-agents-ml 1절(`SampleDistributionMultinoulliMasked`, `LearningAction.cpp:2329`) |
| `Offset` 은 플레이어 기준 상대 위치, 2D(z=0) | 탑다운, 회전 불변 | 설계 결정 |
| 스키마 버전 문자열을 `Content/CombatSim/Policies/<name>.policy.json` 에 기록 | 호환 해시 변경 시 어떤 녹화·정책이 무효인지 추적 | engine-learning-agents-ml 추가 고려사항 1 |

### 3.3 녹화 워크플로(기획자 플레이 → Recorder → ImitationTrainer)

| 단계 | 명령·동작 | 비고 |
|---|---|---|
| 0 | 에디터 1회 실행 `UnrealEditor.exe TDGame.uproject -ForcePipInstallOnInit` | PipInstall 생성(torch 2.5.1+cu124 등). 재실행 불필요 | 
| 1 | 기획자가 PIE 또는 `-game` 으로 시나리오 맵을 플레이. `UTDPlayerProxyRecorderComponent` 가 think 격자(페르소나 `think_hz`, 3.1절)마다 `Interactor->GatherObservations()` 로 `FTDBrainInputs` 스냅샷에서 관측을 채우고, 사람의 실제 입력(눌린 스킬 슬롯·이동 방향)을 행동 벡터로 변환해 `SetActionVector(Vec, ActionCompatibilityHash, AgentId)` 로 넣은 뒤 `Recorder->AddExperience()`(전체 에이전트, 인자 없음) | `GetObservationVector` 는 이미 버퍼된 벡터를 읽을 뿐 관측을 채우지 않는다. `AddExperience` 는 "GatherObservations 다음, 사람 시연이면 EvaluateAgentController 다음" 호출이 요구된다. `LearningAgentsInteractor.h:195-203,250-256`, `LearningAgentsRecorder.h:133-141` |
| 2 | 세션 종료 시 `ULearningAgentsRecording::SaveRecordingToFile` → `Saved/CombatSim/Recordings/<persona>_<date>.bin` + 옆에 `.meta.json`(장비 GE 목록, 물약 정책, 시나리오, 정의 해시 dh, 빌드 해시, 스키마 버전) | 바이너리는 Git LFS 또는 외부 저장(engine-learning-agents-ml 추가 고려사항 2) |
| 3 | `python Tools/CombatSim/train_player_proxy.py --mode bc --recordings Saved/CombatSim/Recordings/*.bin --out Content/CombatSim/Policies/human_normal` — 내부에서 (a) `train.py ... -m learning_core.train_behavior_cloning ... Socket 127.0.0.1:48491 <tmp>` (소켓 모드 인자 형식은 추정. 공유 메모리 모드의 실제 형식은 `LearningExternalTrainer.cpp:80-91`; 포트 48491 은 `LearningAgentsCommunicator.h:65` 확인; Phase 4 착수 시 `train.py --help` 로 확정) 를 먼저 띄우고 (b) `UnrealEditor-Cmd.exe TDGame.uproject -run=TDCombatSim -mode=train_bc -nullrhi -unattended -onethread -recordings=...` 를 띄운다 | `bUseExternalTrainingProcess=true`, `TrainerCommunicationTimeout` 을 대형 녹화에 맞춰 상향(`LearningAgentsImitationTrainer.h:37`) |
| 4 | 산출: `<name>.ubnne` 스냅샷 3개(인코더·정책·디코더) + `<name>.policy.json`(ph = 스냅샷 SHA-256, 녹화 목록, MLflow run id) | `LearningAgentsNeuralNetwork.h:103` `SaveNetworkToSnapshot` |
| 5 | 게이트: `TDGame.CombatSim.PolicyDeterminism` 통과 후 커밋 | 3.5절 |

녹화 격자와 추론 격자를 같게 두는 이유: 훈련 스텝과 추론 스텝이 다르면 일반화가 깨진다는 엔진 헤더 주석(web-balance-simulation-tools 시사점 2, `LearningAgentsTrainer.h:41-43`).

데이터 양 추정: 30분 플레이 × 8Hz(예시 가정, 실제는 페르소나 `think_hz`) = 약 14,400 샘플/세션, 페르소나당 3~5세션(추정). BC 기본 `BatchSize=128`, `Window=64`(`LearningAgentsImitationTrainer.h:79,87`).

### 3.4 학습 모드의 스텝 순서(이중 스텝 금지 유지)

`-run=TDCombatSim -mode=train_bc|train_ppo` 도 `FTDCombatSimSession` 을 그대로 쓴다(D26). 차이는 대리 봇 두뇌 구현체 하나뿐이다.

| 두뇌 구현체 | `Think()` 안에서 하는 일 | 모드 |
|---|---|---|
| `FTDRulePersonaBrain` | 페르소나 규칙 평가, PlayerProxy 스트림에서 draws 소비 | 시뮬 기본 |
| `FTDPolicyBrain` | `Interactor->Inputs = &Snapshot; Policy->RunInference(0.0f);` → `PerformAgentAction` 이 `FTDPlayerCommand` 에 기록 | 시뮬(BC/PPO 정책 재생) |
| `FTDTrainerBrain` | `PPOTrainer->RunTraining(...)` 또는 `ImitationTrainer->RunTraining(...)` — 내부에서 보상 수집 → 경험 처리 → `RunInference` | 학습 전용 |

`RunTraining` 은 매 프레임 호출을 전제로 하고(`LearningAgentsPPOTrainer.cpp:675-711`), 리플레이 버퍼가 차면 게임 스레드가 트레이너 응답을 블로킹 대기한다(공유 메모리 경로 `LearningSharedMemoryTraining.cpp:44-58` 확인; 소켓 경로 `LearningSocketTraining.cpp` 도 같은 방식으로 추정, 미확인). 학습 프로세스는 플레이 가능한 프레임을 기대하지 않는 전용 프로세스이므로 허용한다. 모든 호출이 틱 함수의 대리 봇 단계 안에 있으므로 "스텝당 사고 횟수" 결정론 게이트(D15)가 학습 모드에도 그대로 적용된다.

PPO 환경 `UTDPlayerProxyEnvironment : ULearningAgentsTrainingEnvironment` 는 보상(승리 +1, 사망 −1, 스텝당 준 피해·받은 피해 비율 소량 셰이핑)과 완료(사망·전멸·MaxSteps)를 C++ `_Implementation` 으로 쓴다(`LearningAgentsTrainingEnvironment.h:64,87`). 병렬 경험 수집은 **한 월드 안에 K개 아레나**(예: 32개, 5,000cm 간격, 각 아레나에 대리 봇 1 + 몬스터 조합 1)를 두고 `SetMaxAgentNum(K)` 로 한다(`LearningAgentsManager.h:54,233`). 공간 해시(D17)가 아레나를 자연 분리하고 스폰 순서는 아레나 인덱스 → SimulationId 다. 에피소드 재시작은 `RemoveAgent/AddAgent` 없이 위치·체력·쿨다운만 리셋한다(2절 버그 회피).

### 3.5 추론 결정론 절차(체크리스트)

| # | 조치 | 근거 |
|---|---|---|
| 1 | `MakePolicy(..., Seed = static_cast<int32>(HashCombine(MasterSeed, PlayerProxyStreamId) & 0x7fffffff))` — D29 의 PlayerProxy 스트림과 같은 파생 규칙. `Seed` 는 `int32`, `HashCombine` 은 `uint32` 라 캐스트 규칙을 고정한다 | `LearningAgentsPolicy.h:105-117` |
| 2 | `RunInference(0.0f)` — 연속 = 평균, 이산 = argmax, 난수 미사용 | `Learning.ispc:305-311`, `LearningRandom.cpp:413-436` |
| 3 | `FLearningAgentsPolicySettings::MemoryStateSize = 0` | `LearningAgentsPolicy.h:48-54` |
| 4 | `bUseParallelEvaluation = false` (시뮬은 단일 스레드, D25; 행 독립이라 결과에는 영향 없음) | `LearningNeuralNetwork.cpp:329-355` |
| 5 | 에이전트 ID 는 세션 동안 유지, `AddAgent` 순서 = 아레나 인덱스 순 | `LearningAgentsPolicy.cpp:427-429,476,495,514` (에이전트 시드는 `SampleIntArray(Seeds, GlobalSeed, AgentIds)` 로 GlobalSeed 와 AgentId 에서 파생되므로 AgentId 배정이 같아야 함; `RunInference(0.0f)` 에서는 시드가 소비되지 않아 재현에는 무관) |
| 6 | 추론 백엔드 `NNERuntimeBasicCpu` 고정, GPU 미사용 | web-ml 피하기 "GPU 추론으로 결정론 기대" |
| 7 | 정책 파일 해시 ph 를 결과 CSV/JSONL 과 결정 로그 헤더에 기록(dh·빌드 해시와 함께) | D11·D33 확장 |
| 8 | 게이트 `TDGame.CombatSim.PolicyDeterminism`: 같은 프로세스 2회 + 다른 프로세스 1회 상태 해시 일치(D31 과 같은 3중 검사) | D31 (engine-determinism-headless 결론 1 의 고정 스텝 루프 위에서) |
| 9 | 다른 CPU 명령셋 간 비트 동일은 약속하지 않는다(ISPC). 시뮬-실기 3단(D32)에서 A단은 "같은 빌드·같은 명령셋" 조건부, 교차 머신은 C단(통계 등가)로 내려간다 | `NNERuntimeBasicCpuModel.cpp:9`, web-ml 결론 3 |

---

## 4. 적용 지점 2 — JSON `tune` 잎 → CMA/PSO 목적 함수 루프

### 4.1 벡터 추출

[02 문서](02-architecture-and-definition-format.md)의 정의 JSON 에서 규칙 14 의 값 객체 `{ "value": v, "tune": [min, max] }` 로 표시된 숫자 잎(응답 곡선 m·k·b·c, 가중치, 쿨다운, 관성 유지 스텝 등)을 파일 내 **정의 순서**로 모아 벡터 x 를 만든다(결정 기록 D36 의 "tune:true" 는 이 값 객체의 요약 표기). `tune` 배열의 두 값이 곧 min/max 범위이고(스키마 덤프에 포함) 최적화기는 [0,1] 정규화 공간에서 움직인다. 정의 순서를 쓰는 이유는 동률·순회 규칙을 SimulationId 와 정의 순서 하나로 통일한 D1·D30 과 같다.

### 4.2 목적 함수

```
L(x) = Σ_s w_s · (WinRate_s(x) − Target_s)²            시나리오 s 별 목표 승률
     + λ_band · Σ_s max(0, |WinRate_s(x) − Target_s| − HalfBand_s)²   밴드 이탈 벌점
     + λ_delta · Σ_i max(0, |x_i − x0_i| / |x0_i| − 0.30)²           원본 대비 ±30% 초과 벌점
     + λ_chatter · Σ_s max(0, SwitchRate_s(x) − 5/s)                행동 교체율 상한(검증 3단과 동일 기준)
```

| 항 | 출처 | 값 |
|---|---|---|
| 승률 밴드 | Riot 평균 구간 49~54.5%(2019/2020) 를 본떠 시나리오별 `Target ± HalfBand` 표를 `Content/CombatSim/Targets/*.json` 에 둔다 | web-balance-simulation-tools 결론 8 |
| ±30% 제약 | Blizzard 하스스톤 아레나: 예측 승률 50% 목표, 카드 가중치 변경 ±30% 이내(2018) | web-balance-simulation-tools 결론 8, 1절 표 |
| 승률 표준오차 | 후보당 시드 100 → ±5.0%p, 400 → ±2.5%p(p=0.5 산술) | web-balance-simulation-tools 결론 10, D33 |

### 4.3 루프 실행 형태

`-run=TDMonsterAITune -def=<Id> -targets=<file> -generations=30 -workers=8` 커맨드렛 하나가 **오래 살면서** 최적화기 상태를 메모리에 들고, 세대마다 후보 집단을 임시 정의 파일로 쓰고 `Tools/CombatSim/run_batch.py`([04 문서](04-combat-simulator.md) 12절) 를 자식 프로세스로 띄워 팬아웃(D35)한 뒤 손실을 읽어 `Update` 한다. 최적화기 내부 상태(`Mean`, `Covariance`, `PathSigma` 등)는 비공개 멤버라 프로세스 간 직렬화 경로가 없으므로(`LearningCMAOptimizer.h` 직접 확인) 이 "장수 프로세스 + 자식 팬아웃" 형태가 D35 와 양립하는 유일한 형태다.

```cpp
int32 UTDMonsterAITuneCommandlet::Main(const FString& Params)
{
	FTDTuneJob Job;
	if (!Job.Parse(Params)) return 1;
	const int32 Dim = Job.Leaves.Num();
	const int32 SampleNum = UE::Learning::FCMAOptimizer::DefaultSampleNum(Dim);
	UE::Learning::FCMAOptimizer Optimizer(Job.Seed);
	Optimizer.Resize(SampleNum, Dim);
	TLearningArray<2, float> Samples({ SampleNum, Dim });
	TLearningArray<1, float> Losses({ SampleNum });
	Optimizer.Reset(Samples, Job.InitialGuessNormalized());
	for (int32 Gen = 0; Gen < Job.Generations; ++Gen)
	{
		Job.WriteCandidateDefinitions(Samples, Gen);
		if (!Job.RunBatchFanOut(Gen)) return 2;
		if (!Job.ReadLosses(Gen, Losses)) return 3;
		Job.AppendHistory(Gen, Samples, Losses);
		Optimizer.Update(Samples, Losses);
		if (Job.HasConverged(Losses)) break;
	}
	Job.WriteBestDefinition();
	return 0;
}
```

| 단계 | 산출물 | 비고 |
|---|---|---|
| 후보 작성 | `Saved/CombatSim/Tune/<Id>/gen<N>/cand<k>.json` = 원본 정의에 벡터를 되써 넣은 완전한 정의 파일(`extends` 해석 후) | 각 후보는 검증 1~2단(D8)을 통과해야 배치에 들어간다. 탈락 후보는 손실 = 벌점 상한 |
| 팬아웃 | `Tools/CombatSim/run_batch.py --defs gen<N>/*.json --scenarios <targets> --seeds 0..S-1 --workers 8` | 시드 = 기본 시드 + 시나리오 인덱스(D35). 세대 초반은 S=100, 수렴 후 S=400 으로 올리는 시드 일정 |
| 손실 | `gen<N>/losses.csv`(후보, 시나리오별 승률, 표준오차, 교체율, 손실) | `Tools/CombatSim/summarize_batch.py` 가 계산 |
| 이력 | `history.jsonl` 한 세대 한 줄(벡터, 손실, 최고 후보) | 재현: 같은 시드·같은 빌드·같은 정의 해시면 세대 이력이 비트 동일(시뮬 결정 + CMA 시드) |
| 결과 | `Content/MonsterAI/Definitions/<Id>.json` 에 **되쓰기 후보**로 `<Id>.tuned.json` 을 내고 사람이 diff 검토 후 채택 | 텍스트 정본 원칙(D6) 유지. 채택 후 검증 3단 + 골든 해시 갱신 |

선택 기준(추정, Phase 4 실측으로 확정): 잎 수 ≤ 5 면 최적화기 없이 그리드 탐색(web-balance-simulation-tools 4절 선택 기준), 그 이상은 CMA(기본 표본 수 `DefaultSampleNum(Dim)`)를 기본으로 하고 손실 지형이 거칠면 PSO(`LearningPSOOptimizer.h:31-33`)로 바꾼다. 40 같은 구체 문턱은 실측 전 두지 않는다(engine-learning-agents-ml 결론 8(b) 는 둘 다 권장할 뿐 분기 기준이 없다).

비용 추정(추정, Phase 1 실측으로 갱신): 표본 16 × 시나리오 4 × 시드 100 = 6,400 시뮬/세대. 시뮬 1회(10마리 vs 봇, 1,920스텝 = 30초)를 0.5초로 가정하면 세대당 약 53분 단일 프로세스, 8 프로세스 팬아웃 시 약 7분, 30세대 약 3.5시간. 이 수치는 [04 문서](04-combat-simulator.md)의 스텝당 비용 실측이 나온 뒤 다시 계산한다.

---

## 5. 강화학습을 밸런스 탐색·QA 봇에 한정하는 이유와 필요 자원

### 5.1 이유

| 이유 | 근거 |
|---|---|
| 출하 몬스터에 RL 을 쓴 사례는 규칙이 고정된 레이싱·대전 + 대규모 인프라(GT Sophy PS4 1,000대 이상) 뿐이며, 대다수 스튜디오는 IL/RL 을 QA·밸런스·플레이어 대리에 쓴다 | web-ml 결론 4, 2절 "공통 패턴" |
| RL 정책은 밸런스 변화에 대한 일반화가 미해결이라 예측에 쓰기 어렵다: EA SEED 는 일반화를 "미해결 과제"로 보고(2024), 모방학습은 "시연한 것만 배운다"(Epic 포럼) | web-ml 4절 표 |
| 학습은 비결정(GPU 커널·수집 타이밍)이라 시뮬레이터의 재현 약속(D31·D32)과 충돌한다 | engine-learning-agents-ml 6절 제약, `LearningAgentsPPOTrainer.h:221` |
| PPO 상한 봇은 "이 장비로 이 조합을 이길 수 있는가"의 상한을 주고, BC 봇은 사람 기준선을 준다. 둘 사이가 난이도 밴드가 되므로 RL 결과는 **몬스터 수치(4절 튜닝 목표)** 로만 반영한다 | web-ml 우선순위 3, 텐센트 자기대전 축소판 |

RL 의 두 번째 용도는 QA 봇이다: 시나리오마다 PPO 봇이 찾은 "예상 밖 승리 경로"(예: 특정 원거리 몬스터 무한 카이팅)를 결정 로그(D34)로 남기고 `propose_tweaks.py` 가 규칙 힌트로 바꾼다.

### 5.2 필요 하드웨어·시간(모두 추정, 실측 전)

| 항목 | 추정 | 근거·가정 |
|---|---|---|
| BC 학습 | CPU 만으로 10~30분/정책 | EA SEED 20분(2023, 다른 게임·다른 규모), `torch.set_num_threads(1)` 기본(`train_behavior_cloning.py:72`, PPO 도 `train_ppo.py:61`)이므로 스레드 수를 올려야 함 |
| PPO 학습 | GPU 1대(8GB 이상, CUDA 12.4 호환)에서 2~6시간/정책, CPU 전용은 2~4배 | EA SEED 5시간, Ubisoft Roller Champions 1~4일/모델(2020) 사이. 32 아레나 × 실시간 대비 50~100배 헤드리스 가속 가정 |
| 헤드리스 가속 | `-nullrhi -unattended -onethread`, 렌더·사운드 끔, 1/64 고정 스텝을 CPU 한계 속도로 | `LearningAgentsTrainer.h:126-127` 권고, engine-determinism-headless 결론 8 |
| 메모리 | 학습 프로세스 4~8GB + 파이썬 트레이너 2~4GB | 에디터 Cmd 프로세스 규모(web-balance 시사점 6) |
| 디스크 | 녹화 세션당 수십 MB, 스냅샷 수 MB | 바이너리 → LFS |
| torch 호환성 | `torch==2.5.1+cu124` 가 개발 머신 드라이버와 맞는지 미확인 | `PythonMLPackages.uplugin:45`, engine-learning-agents-ml 미확인 5 |

---

## 6. LLM 제작 파이프라인(D37 상세)

### 6.1 루프 한 바퀴

| 단계 | 명령 | 읽는 것·쓰는 것 | 토큰 추정 |
|---|---|---|---|
| 1 스키마 읽기 | (파일 읽기) `AGENTS.md` 몬스터 AI 절 8줄 + `Docs/MonsterAI_CombatSim/schema/monster-definition.schema.json` + `inputs.md` + `actions.md` + 예시 1개 | `UTDMonsterAISchemaDumpCommandlet` 이 코드에서 재생성한 문서(D9) | 3.5~5천 |
| 2 작성 | `Content/MonsterAI/Definitions/<Id>.json` (+ 필요 시 시나리오 JSON 1개) | 평면 표 4개 + 메타(D6) | 0.6천 |
| 3 검증 3단 | `UnrealEditor-Cmd.exe TDGame.uproject -run=TDMonsterAIValidate -only=<Id> -print-resolved` | 스키마·미지 키 거부·이름 유사도 힌트 → 정적 규칙 → 5초 생존 시뮬·2회 해시 일치(D8) | 출력 0.3천 |
| 4 배치 | `python Tools/CombatSim/run_batch.py --scenario Duel_<Id> --count 200` → `-run=TDCombatSim` 프로세스 팬아웃([04 문서](04-combat-simulator.md) 12절) | CSV/JSONL(D33) + 결정 로그(D34) | 0 |
| 5 요약 | `python Tools/CombatSim/summarize_batch.py <run_dir>` → 30줄(승률±표준오차, 생존 스텝 분위수, 행동 점유율, 교체율, 대기 점유, 0점 원인 상위 3개) | `analyze_decisions.py` 가 0점 원인 표를 붙임 | 0.8천 |
| 6 수정 | 3으로 돌아감. `propose_tweaks.py` 의 규칙 힌트는 참고만 | | |
| 7 완료 조건 | 검증 통과 + 목표 밴드 안 + `TDGame.CombatSim.*` 결정론 게이트 통과 → JSON + 요약 md 커밋 | | |

반복당 약 5천 토큰(추정, 설계안 A §6.2). 종당 컴파일 0회 — 새 입력 함수·행동 원시가 필요할 때만 C++ 등록표(D7)를 만지고, 그때 스키마 덤프를 다시 돌린다. 이 루프는 BTGenBot-2·Real-Time World Crafting 의 "제한된 표면 + 정적 검증 + 시뮬 피드백"(web-ml 결론 6)과 같은 구조다.

### 6.2 LLM 정확도 근거

| 사실 | 수치 | 근거 |
|---|---|---|
| 제한된 JSON DSL 을 문서와 함께 프롬프트에 넣으면 문법 유효율 98~100%(Spell DSL), 76~100%(Automata DSL), 파인튜닝 없음 | arXiv 2510.16952 (2025) | web-llm-authorable-tooling 결론 6 |
| 저자원 언어(Lua)는 7B 양자화 모델에서 50% 미만; 대형 모델의 Lua 정확도는 미확인 | arXiv 2410.14766 (2024) | web-llm-authorable-tooling 결론 6, 2-2절 주의 |
| XML BT 생성 제로샷 90.38%, 원샷 98.07%(10억 파라미터 모델) | arXiv 2602.01870 (2026) | web-ml 결론 6 |
| 모델 선택이 품질의 최강 예측 변수(p<.001) | arXiv 2510.16952 (2025), 1행과 같은 논문 | web-llm 2-2절 |

따라서 정본은 JSON(고자원 형식) + C++(고자원 언어)이고 Lua·Angelscript·UnrealSharp 는 도입하지 않는다(web-llm 결론 "피함").

### 6.3 MCP 툴셋 보조 채널

| 사실 | 근거 |
|---|---|
| 생성형 AI 의 실제 접점은 `ToolsetRegistry` + `ModelContextProtocol` 어댑터이며 `UToolsetDefinition` 서브클래스의 `meta=(AICallable)` 정적 UFUNCTION 이 곧 MCP 툴이 된다. 프로젝트 uproject 는 이미 `ModelContextProtocol`·`AllToolsets` 를 에디터 타깃으로 켜 두었다 | engine-misc-decision-tools R8, `ToolsetDefinition.h:12-14,32`, `TDGame.uproject:23-31` |
| 5.8 MCP 서버는 Experimental, HTTP+SSE 전용, `http://127.0.0.1:8000/mcp` 루프백 전용, 인증 없음, 툴 호출은 게임 스레드 직렬 실행, 툴셋 레지스트리 수동 활성 필요(둘 다 2차 출처) | web-ue-5-6-to-5-8-ai-changes 결론 6 |
| `ToolsetRegistry` 는 에디터 모듈이라 MCP 툴은 에디터 프로세스에서만 노출된다. 헤드리스 커맨드렛 경로(CLI)가 1차다 | engine-misc-decision-tools 시사점 "MCP 툴은 에디터 프로세스에서만" |

설계: `UTDMonsterAIToolset : UToolsetDefinition` 에 `AICallable` 3개 — `ValidateDefinition(Id)`, `RunSimBatch(Scenario, SeedCount)`, `SummarizeRun(RunDir)` — 를 두되 **CLI 와 같은 `FTDCombatSimSession`·검증기 코드를 호출**한다. 이 클래스는 에디터 모듈이 필요하므로 D13 에 따라 `TDGameEditor` 가 병행 작업 쪽에서 생긴 뒤에 그 모듈로 추가한다. 그 전까지 LLM 은 CLI 만 쓴다. MCP 세션과 CLI 가 같은 프로세스에서 동시에 시뮬을 돌리지 않도록 세션은 파일 락(`Saved/CombatSim/.lock`)을 잡는다.

### 6.4 Epic AIAssistant 를 쓰지 않는 이유

Epic Developer Assistant 웹앱을 에디터 안 브라우저에 내장한 에디터 전용·실험 플러그인이고, 툴은 `GetProjectContext/GetDockedContext` 둘뿐이며 콘솔 명령 실행 기능이 없다. LLM 은 Epic 클라우드 쪽에 있고 엔진은 UI 셸이다(engine-misc-decision-tools R7, `AIAssistant.uplugin:16,22-24`, `AIAssistantToolset.h:78-90`). 우리 파이프라인의 프로그래밍 가능한 접점이 아니므로 켜지 않는다.

---

## 7. 결정론 유지 규약(ML 개입 시)

| 규약 | 내용 | 근거 |
|---|---|---|
| R1 학습은 비결정 허용 | PPO·BC 학습 결과의 재현을 요구하지 않는다. 시드(`RandomSeed`, `torch.manual_seed`)는 고정하되 "같은 정책이 다시 나온다"고 약속하지 않는다 | engine-learning-agents-ml 6절, `train_ppo.py:59-61` |
| R2 추론·시뮬은 결정 | 3.5절 체크리스트 전부. 정책을 쓰는 시뮬은 규칙 봇 시뮬과 같은 해시 게이트를 통과해야 한다 | D31 |
| R3 네 값 기록 | 모든 결과·로그에 정의 해시(dh) + 빌드 해시 + 정책 해시(ph, 스냅샷 SHA-256; 규칙 봇이면 페르소나 JSON 해시) + 시나리오 시드 | D11·D33 확장 |
| R4 정책 버전 잠금 | 시뮬 세션 중 정책 스냅샷 교체 금지(D11 의 핫리로드 잠금과 같은 규칙) | D11 |
| R5 스키마 호환 해시 | 관측/행동 호환 해시가 `.policy.json` 의 값과 다르면 세션 시작을 거부 | `LearningAgentsInteractor.h:199-203` |
| R6 튜닝 이력 재현 | `TDMonsterAITune` 은 CMA 시드·정의 해시·빌드 해시가 같으면 `history.jsonl` 이 비트 동일. 시드 일정 변경은 이력에 기록 | 4.3절 |
| R7 전역 난수 금지 | `Source/TDGame/CombatSim/ML/` 도 `TDGame.MonsterAI.NoGlobalRandom` grep 대상에 포함 | D29 |
| R8 교차 머신 | ISPC 경로로 다른 명령셋 간 비트 차이가 날 수 있으므로 골든 해시는 CI 머신 명령셋(AVX2 등)을 명시하고, 교차 머신 검증은 통계 등가(D32 C단)로 정의 | web-ml 추가 고려 1, engine-learning-agents-ml 미확인 3 |

---

## 8. 향후 선택지와 도입 조건

| 선택지 | 판단 | 도입 조건(전부 충족 시에만) | 근거 |
|---|---|---|---|
| 보스·엘리트 소수 신경망 정책 | 보류. Phase 4 이후 재검토 | (1) 동시 ≤ 10마리, (2) 사고 주기 ≥ 8스텝 + 배치 추론, (3) `RunPolicy` 행동 원시가 `FTDBrainInputs` 만 읽고 `bSimulatable` 을 만족, (4) 정책을 바꿔도 결정 로그·해시 게이트가 그대로 통과, (5) 프로파일에서 마리당 0.3ms 이하 실측 | web-ml 결론 5(64마리 실증), engine-learning-agents-ml 6절 권장 3(b) |
| LLM 실시간 NPC(NVIDIA ACE 류) | 채택하지 않음 | 탑다운 대량 몬스터와 구조적으로 불일치(RTX GPU 전제, 동료 1·보스 1 규모, 초당 8~13회 미세 결정) | web-ml 결론 7 |
| Flow Matching 정책(5.8 신규) | 채택하지 않음 | 추론이 ODE 스텝 수(기본 8)만큼 디노이저를 반복 평가해 비용이 약 8배로 추정(미실측) | engine-learning-agents-ml 추가 고려 4, `LearningAgentsFlowMatching.h:38,51,106` |
| 세계 모델·범용 에이전트(Muse, Genie 3, SIMA 2) | 발상 도구로만 | 연구 프리뷰 단계 | web-ml 결론 8 |
| LLM 보상 함수 생성 | 옵션 | PPO 보상을 텍스트 의도 → C++ `_Implementation` 으로 생성(컴파일 필요). Sony AI 워크플로 참고 | web-ml 추가 고려 4 |
| ONNX 외부 모델 | 옵션 | `NNERuntimeORT`(베타) 활성, `IntraOpNumThreads=1`, `ExecutionMode=SEQUENTIAL` 고정. `ubnne` → ONNX 공식 내보내기는 엔진에 없음(미확인) | engine-learning-agents-ml 5절, `NNERuntimeORTSettings.h:33-45`, 미확인 7 |
| AMD Schola v2 | 미채택 | UE 5.5~5.6 대상, 5.8 호환 미확인 | web-ml 1-2절 |

---

## 9. 플러그인·Build.cs·MLflow

### 9.1 활성 목록(Phase 4 시작 시점에 추가, 그 전에는 불필요)

| 플러그인 | 타깃 | 용도 | 현재 uproject |
|---|---|---|---|
| `LearningAgents` | 전체 | 인터랙터·정책·트레이너·레코더 | 없음 |
| `LearningCore` | 전체 | 신경망·난수·CMA/PSO 최적화기(`Learning` 모듈) | 없음 |
| `NNERuntimeBasicCpu` | 전체 | 정책 추론 런타임 | 없음 |
| `PythonMLPackages` | Editor | PyTorch pip 설치 | 없음 |
| `MLflow` | Editor | 옵션: 실험 추적(`mlflow-skinny 2.20.2`) | 없음 |
| `NNERuntimeORT` | 전체 | 옵션: ONNX 모델 | 없음 |
| `ModelContextProtocol`, `AllToolsets` | Editor | MCP 보조 채널 | 이미 활성(`TDGame.uproject:23-31`) |

현재 `TDGame.uproject` 는 GameplayAbilities, ModelContextProtocol, AllToolsets, ModelingToolsEditorMode, StateTree, GameplayStateTree 만 켜져 있다(직접 확인).

### 9.2 Build.cs 의존

```cs
PublicDependencyModuleNames.AddRange(new string[] { "Json", "JsonUtilities" });
if (Target.ProjectDefinitions.Contains("TD_WITH_LEARNING_AGENTS=1"))
{
	PrivateDependencyModuleNames.AddRange(new string[] { "LearningAgents", "LearningAgentsTraining", "Learning" });
}
```

- `Json`, `JsonUtilities` 는 D13 의 결정이며 Phase 1 부터 필요하다.
- ML 세 모듈은 `TD_WITH_LEARNING_AGENTS` 정의가 있을 때만 링크한다. 목적은 Phase 0~3 빌드가 실험 플러그인에 묶이지 않게 하는 것이다. 정의는 `TDGame.Target.cs` 의 `ProjectDefinitions` 로 켠다.
- **리플렉션(UCLASS/USTRUCT) 헤더는 프로젝트 매크로 `#if` 로 감싸지 않는다.** UHT 는 `0/1/CPP/WITH_EDITOR/WITH_EDITORONLY_DATA/WITH_ENGINE/UE_VERSION_*` 만 인식하고, 그 밖의 조건 블록은 `Unrecognized` 로 분류해 통째로 건너뛰므로 `.generated.h` 에 해당 클래스 코드가 생성되지 않는다(`UhtHeaderFileParser.cs:1046,1049-1112,1187-1193`). `#if TD_WITH_LEARNING_AGENTS` 로 감싼 `UCLASS()` 헤더는 매크로를 켠 빌드에서 `GENERATED_BODY()` 미정의로 컴파일이 실패한다. 대신 둘 중 하나를 택한다(미결 8). (a) ML 소스를 모듈 루트 밖 `Source/TDGame_ML/` 에 두고 Build.cs 에서 정의가 켜졌을 때만 `ConditionalAddModuleDirectory`(`ModuleRules.cs:2029`) 로 같은 TDGame 모듈에 편입한다(D13 새 모듈 금지와 양립). (b) 플러그인을 Phase 4 시작 시 uproject 에 켜고 항상 링크한다. `.cpp` 내부의 비리플렉션 코드만 매크로로 감쌀 수 있다. (a) 를 택하면 R7 의 grep 대상과 07 M4-02 산출물 경로도 그 폴더로 바꾼다.
- `LearningAgentsTraining` 은 `bBuildEditor` 일 때만 `UnrealEd` 를 링크하는 Runtime 모듈이므로 커맨드렛(런타임 TDGame 모듈, D13)에서 링크해도 된다(`LearningAgentsTraining.Build.cs:33-36`).
- 새 모듈은 만들지 않는다(D13). `CombatSim/ML/` 폴더 파일: `TDPlayerProxyInteractor.h/.cpp`, `TDPlayerProxyEnvironment.h/.cpp`, `TDPlayerProxyBrains.h/.cpp`(Rule/Policy/Trainer 세 구현체), `TDPlayerProxyRecorderComponent.h/.cpp`, `TDMonsterAITuneCommandlet.h/.cpp`.

### 9.3 MLflow 옵션

MLflow 플러그인은 코드 모듈 없이 파이썬 패키지 설치 선언만 하며, 트레이너 설정 `bUseMLflow`, `MLflowTrackingUri` 를 켜면 LearningCore 파이썬 트레이너가 `mlflow.set_tracking_uri` 로 지표를 기록한다(engine-misc-decision-tools R9, `LearningAgentsPPOTrainer.h:251-257`, `train_common.py:1028-1036,1070`). 로컬 `mlflow server` 를 띄우고 run id 를 `.policy.json` 에 적는다. 시뮬 배치 결과(승률·시간)를 MLflow 에 넣는 것은 별도 파이썬 스크립트(`Tools/CombatSim/log_batch_to_mlflow.py`) 로 가능하나 엔진 기능은 아니다. TensorBoard(`bUseTensorboard`)도 대안이다.

---

## 미결 사항(사용자 결정 필요)

1. **BC 녹화 주체와 분량**: 기획자 1인 30분 × 페르소나 3종(신중/보통/공격적)으로 시작할지, 실 플레이테스트 로그를 기다릴지.
2. **PPO 하드웨어**: GPU 1대를 배정할지, CPU 전용(2~4배 느림, 추정)으로 갈지. `torch==2.5.1+cu124` 드라이버 호환성은 미확인.
3. **튜닝 되쓰기 정책**: `<Id>.tuned.json` 을 사람이 diff 검토 후 채택(권장)할지, 밴드 안이면 자동 채택할지.
4. **MCP 툴셋 시점**: `TDGameEditor` 모듈이 생길 때 곧바로 `UTDMonsterAIToolset` 을 넣을지, CLI 만으로 충분하면 미룰지.
5. **정책 저장소**: 스냅샷·녹화 바이너리를 Git LFS 에 둘지 외부 저장(S3 경로는 `smart_open` 지원)에 둘지.
6. **CI 머신 명령셋 고정**: → [04 문서](04-combat-simulator.md) 미결 2 와 통합, [07 문서](07-roadmap-and-tasks.md) MD-09(신규).
7. **보스 신경망 재검토 시점**: 8절 조건 5개를 Phase 4 종료 시 평가할지, 콘텐츠 요청이 있을 때만 평가할지.
8. **ML 소스 편입 방식**: 9.2절의 (a) `Source/TDGame_ML/` + `ConditionalAddModuleDirectory` 조건 편입과 (b) Phase 4 부터 플러그인 상시 링크 중 무엇을 택할지.

## 근거 색인(인용한 조사 파일 목록)

| 파일 | 인용 결론·절 |
|---|---|
| research/web-ml-generative-npc.md | 결론 1~8, 우선순위 표, 1-2·2·4·4-1·5절, 쓰기/피하기, 추가 고려 1·4 |
| research/engine-learning-agents-ml.md | 결론 1~8, 1절(스키마·파이썬·헤드리스), 2절(추론), 3절(Replay), 4절(MLAdapter), 5절(NNE), 6절(제약, 권장 3(b)), 추가 고려 1·2·4, 미확인 1·3·5·6·7 |
| research/engine-misc-decision-tools.md | R7(AIAssistant), R8(ToolsetRegistry·MCP), R9(MLflow), R13(FJsonObjectConverter), 시사점 3·5 |
| research/web-ue-5-6-to-5-8-ai-changes.md | 결론 3(Learning Agents 상태·Add/Remove 버그), 결론 6(MCP 5.8), 4절 회귀 표 |
| research/web-llm-authorable-tooling.md | 결론 6(JSON DSL 98~100%, Lua 저정확), 2-2절, "피함" |
| research/web-balance-simulation-tools.md | 결론 8(승률 밴드·±30% 하스스톤), 결론 9(규칙 → 모방 → 강화), 결론 10(표준오차), 시사점 2·6, 4절 선택 기준 |
| research/engine-determinism-headless.md | 결론 1·8(고정 스텝 직접 Tick·커맨드렛 실행) |
| 엔진 소스 직접 확인(2026-09-09~10, UE 5.8) | `LearningAgentsTrainer.h:36,44,46-50`(bUseFixedTimeStep·FixedTimeStepFrequency·물리 스텝·MaxFPS), `LearningAgentsPolicy.h:27,48-54,66,117`, `LearningAgentsPolicy.cpp:427-429,476`, `LearningAgentsInteractor.h:195-203,250-256`, `LearningAgentsRecorder.h:133-141`, `LearningAgentsObservations.h:398,410,491,645,826`, `LearningAgentsActions.h:329,436,810`, `LearningAgentsFlowMatching.h:50-51`, `LearningAgentsCommunicator.h:65`, `LearningExternalTrainer.cpp:80-91`, `LearningSharedMemoryTraining.cpp:44-58`, `LearningOptimizer.h:13-40`, `LearningCMAOptimizer.h:30-60`, `LearningCore/Content/Python/learning_core/train_behavior_cloning.py:71-72`, `train_ppo.py:59-61`, `Engine/Source/Programs/Shared/EpicGames.UHT/Parsers/UhtHeaderFileParser.cs:1046,1049-1112,1187-1193`, `UnrealBuildTool/Configuration/Rules/ModuleRules.cs:2029`, `TDGame.uproject`, `Source/TDGame/TDGame.Build.cs` |
| 다른 최종 문서 | [02](02-architecture-and-definition-format.md) 규칙 14·15, §6 페르소나 예시; [03](03-tick-and-scale.md) 1.3절 주기표; [04](04-combat-simulator.md) 2.3절·11.2절·12절·미결 2; [07](07-roadmap-and-tasks.md) M1-12·M2-08·M4-02·MD 목록 |
| 설계안(세션 임시 산출물, 저장소 미포함; 요지는 [00 결정 기록](00-decision-record.md)에 반영) | A §6.1~6.3(3단 봇·루프·토큰 추정), B §6.1(관측 집합 설계), C §6.1(±30% 벌점), D §6.1(정책 실행 격리) |
| 심사 판정 | "ML 몬스터 두뇌" 합의(신경망 두뇌 미채택, BC 봇 + CMA/PSO, RL 은 탐색, 보스 소수는 Phase 4 이후) |

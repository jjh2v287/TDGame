# Learning Agents·MLAdapter·NNE 로 몬스터를 학습·추론하는 파이프라인

조사 기준: UE 5.8 엔진 소스(`C:/Program Files/Epic Games/UE_5.8/Engine`, 아래 상대 경로는 `Engine/` 기준). 모든 "파일:줄" 근거는 2026-09-09 에 해당 파일을 직접 읽어 확인한 것이다. 웹 자료는 Epic 개발자 커뮤니티 문서만 참조했으며, 본문을 받아오지 못한 페이지는 "미확인"으로 표시했다.

용어: RL(Reinforcement Learning, 강화학습), PPO(Proximal Policy Optimization, 근접 정책 최적화), BC(Behavior Cloning, 행동 복제 = 모방학습), NNE(Neural Network Engine, 엔진 신경망 추론 계층), ONNX(Open Neural Network Exchange, 신경망 교환 포맷), ORT(ONNX Runtime), GRU(Gated Recurrent Unit, 순환 신경망 메모리 셀), RPC(Remote Procedure Call, 원격 함수 호출), ISPC(Intel SPMD Program Compiler, SIMD 벡터화 컴파일러).

---

## 결론 요약

1. **Learning Agents 는 "게임 프로세스 안에서 경험 수집 + 외부 파이썬(PyTorch) 프로세스에서 학습" 구조다.** 게임이 `python.exe train.py ... SharedMemory ...` 를 직접 스폰하고(`Plugins/Experimental/LearningCore/Source/LearningTraining/Private/LearningExternalTrainer.cpp:80-91`), 공유 메모리 또는 소켓으로 리플레이 버퍼와 가중치를 주고받는다. 파이썬 실행 파일은 `<프로젝트>/Intermediate/PipInstall/Scripts/python.exe` 로 고정되어 있고(`LearningTrainer.cpp:1640-1644`), 이 디렉터리는 에디터의 PythonScriptPlugin 이 pip 로 만든다(`Plugins/Experimental/PythonScriptPlugin/Source/PythonScriptPlugin/Private/PipInstall.cpp:183`). 따라서 **학습은 최소 1회 에디터 실행(패키지 설치)이 선행돼야 하며, 그 후에는 `-game`/서버 빌드에서도 `NonEditor*Path` 를 채우면 학습 가능**하다(`LearningAgentsTrainer.h:139,147`, `LearningAgentsTrainer.cpp:186-216`). 학습 모듈은 `bBuildEditor` 일 때만 `UnrealEd` 를 링크하므로 런타임 모듈로 빌드된다(`LearningAgentsTraining.Build.cs:33-36`).

2. **관측·행동 스키마는 C++ 로 완전히 정의 가능하다.** `ULearningAgentsInteractor` 의 `SpecifyAgentObservation / GatherAgentObservation / SpecifyAgentAction / PerformAgentAction` 이 `BlueprintNativeEvent` 이므로 C++ 서브클래스에서 `_Implementation` 을 override 하면 된다(`LearningAgentsInteractor.h:81-82, 92, 116-117, 128`). 스키마 빌더는 정적 함수(`SpecifyContinuousObservation`, `SpecifySetObservation(MaxNum, 어텐션 크기…)`, `SpecifyExclusiveDiscreteAction` 등, `LearningAgentsObservations.h:398, 645`, `LearningAgentsActions.h:315, 329`)로 텍스트(C++)만으로 기술된다. 에디터 노드 편집이 필요 없다.

3. **추론은 자체 CPU 런타임(NNERuntimeBasicCpu)이며 결정적으로 만들 수 있다.** 정책 네트워크는 `UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeBasicCpu"))` 로 로드된다(`LearningCore/Source/Learning/Private/LearningNeuralNetwork.cpp:162-168`). 행동 샘플링 난수는 **에이전트별 uint32 시드**를 쓰고(`LearningAgentsPolicy.cpp:427-429, 776-786`), 시드는 `MakePolicy(..., Seed=1234)` 로 고정된다(`LearningAgentsPolicy.h:105-117`). `RunInference(ActionNoiseScale=0.0f)` 로 호출하면 연속 행동은 평균, 이산 행동은 argmax 가 선택되어 **난수를 전혀 쓰지 않는다**(`Learning.ispc:305-311`, `LearningRandom.cpp:413-424`). 병렬 평가(`bUseParallelEvaluation`, 16개 초과 배치에서 ParallelFor)는 행(에이전트) 단위로 독립이라 결과에 영향이 없다(`LearningNeuralNetwork.cpp:329-355`).

4. **N마리 배치 추론은 한 번의 `RunInference()` 로 모든 에이전트가 한 텐서로 평가된다.** `ULearningAgentsManager::MaxAgentNum` 만큼 버퍼를 사전 할당하고(`LearningAgentsManager.h:233`), `EvaluatePolicy()` 는 유효 에이전트 집합을 슬라이스로 만들어 한 번에 평가한다(`LearningAgentsPolicy.cpp:649-712`). 추론 주기는 엔진 틱에 묶이지 않는다: `ULearningAgentsManager::TickComponent` 는 리스너에 `OnAgentsManagerTick` 만 전달하고(`LearningAgentsManager.cpp:37-49`), 실제 추론은 우리가 원하는 시점에 `Policy->RunInference()` 를 호출하면 된다(`LearningAgentsPolicy.cpp:795-810`). 즉 "몬스터 AI 를 몇 틱마다 돌릴지"는 게임 코드가 전적으로 결정한다.

5. **학습된 정책의 저장 형식은 `ULearningAgentsNeuralNetwork`(UDataAsset) 안의 `ULearningNeuralNetworkData` + 바이너리 스냅샷(매직 0x1e9b0c80, 버전 1)** 이다(`LearningAgentsNeuralNetwork.h:75, 95, 103, 130`, `LearningNeuralNetwork.cpp:45-46, 49, 85`). 스냅샷은 파이썬 쪽에서도 같은 포맷으로 저장·로드된다(`train_common.py:142-151`). 모델 파일 타입은 `ubnne`(`NNERuntimeBasicCpu.cpp:20`). **ONNX 가 아니다.** 외부 ONNX 모델을 쓰려면 별도 플러그인 `NNERuntimeORT`(베타, ORT 1.24.1, Win64/Linux/Mac) 를 켜고 NNE 인터페이스로 직접 호출해야 한다.

6. **LearningAgentsReplay 모듈은 강화학습 "리플레이 버퍼"가 아니라 엔진 데모 리플레이(DemoNetDriver) 녹화/재생 래퍼다.** `ULearningAgentsReplaySubsystem : UGameInstanceSubsystem` 이 `GameInstance->StartRecordingReplay / PlayReplay` 를 감싼다(`LearningAgentsReplaySubsystem.h:58, 78-90`, `.cpp:50, 58, 68, 108`). 의존 모듈은 `NetworkReplayStreaming`(`LearningAgentsReplay.Build.cs`). 학습 데이터 녹화는 별도로 `ULearningAgentsRecorder / ULearningAgentsRecording`(관측·행동 float 배열, 매직 0x06b5fb26)이 담당한다.

7. **MLAdapter 는 사실상 정체된 실험 플러그인(버전 0.0.1)이며 TDGame 에 권장하지 않는다.** rpclib(msgpack RPC, 2.2.1) 서버를 게임 안에 띄우고 파이썬 클라이언트가 `add_agent / get_observations / act / request_world_tick` 등을 호출한다(`MLAdapter.uplugin:3,16`, `MLAdapterManager_Server.cpp:30-47`). 파이썬 클라이언트는 구형 `gym`(gymnasium 아님)과 `msgpack-rpc-python` 에 의존한다(`Source/python/setup.py:27`, `core.py:6`). 수동 틱은 `Manager::Tick` 에서 세션의 Sense/Think/Act 만 게이팅할 뿐 월드 틱 자체를 멈추지 않으며(`MLAdapterManager.cpp:263-274`), `request_world_tick` 은 `Sleep(0)` 바쁜 대기다(`MLAdapterManager_Server.cpp:35-46`). 5.8 에도 남아 있으나 Learning Agents 와 달리 신규 기능·문서 갱신 흔적이 없다.

8. **TDGame 적용 권장안:** (a) 밸런스 시뮬레이터 안에서 **플레이어 대리 에이전트**를 PPO 또는 BC 로 만든다(고정 스텝 `World->Tick(LEVELTICK_All, Step)` 픽스처와 `RunInference(0.0f)` 조합 → 결정적 재현). (b) 몬스터 본체 AI 는 규칙 기반(코드 정의)으로 두고, **행동 파라미터 튜닝은 LearningCore 의 CMA/PSO 블랙박스 최적화기**(`LearningCMAOptimizer.h:30-32`, `LearningPSOOptimizer.h:31-33`)로 시뮬레이터 결과(손실)를 최소화하는 방식이 학습 시간·설명 가능성 면에서 유리하다. (c) 기획자 플레이 복제는 `ULearningAgentsRecorder` → `ULearningAgentsImitationTrainer` (BC) 경로가 준비되어 있다.

---

## 상세 조사

### 1) Learning Agents 구성

#### 플러그인·모듈 구조

| 항목 | 사실 | 근거 |
|---|---|---|
| 플러그인 버전 | 0.2, `IsExperimentalVersion: true` | `Plugins/Experimental/LearningAgents/LearningAgents.uplugin` |
| 모듈 | `LearningAgents`(Runtime), `LearningAgentsTraining`(Runtime), `LearningAgentsTrainingEditor`(Editor), `LearningAgentsReplay`(Runtime) | 같은 파일 Modules 배열 |
| 의존 플러그인 | `LearningCore`, `PythonMLPackages`, `NNERuntimeBasicCpu` | 같은 파일 Plugins 배열 |
| 코어 라이브러리 | `LearningCore/Source/Learning`(배열·난수·신경망·최적화기), `LearningCore/Source/LearningTraining`(경험 버퍼·외부 트레이너·공유메모리/소켓) | `Plugins/Experimental/LearningCore/Source/*/Public/*.h` |
| 학습 모듈 의존 | `AIModule, GameplayTags, NavigationSystem, Learning, LearningAgents, LearningTraining, Projects`; `UnrealEd` 는 `Target.bBuildEditor` 일 때만 | `LearningAgentsTraining.Build.cs:14-36` |
| 통신 모듈 의존 | `Sockets, Networking` | `LearningCore/Source/LearningTraining/LearningTraining.Build.cs:43-44` |

#### 핵심 클래스

| 클래스 | 역할 | 시그니처 근거 |
|---|---|---|
| `ULearningAgentsManager : UActorComponent` | 에이전트(UObject) 등록·ID 부여, 리스너에 이벤트 전달 | `LearningAgentsManager.h:29-31`; `int32 AddAgent(UObject* Agent)` :60; `void SetMaxAgentNum(const int32)` :53-54; `int32 MaxAgentNum = 1` :233 |
| `ULearningAgentsManagerListener : UObject` | Interactor/Policy/Critic/Trainer 의 공통 부모. `OnAgentsAdded/Removed/Reset/ManagerTick` | `LearningAgentsManagerListener.h:29, 71` |
| `ULearningAgentsInteractor` | 관측·행동 스키마 정의 및 수집·수행 | `LearningAgentsInteractor.h:30-31` |
| `ULearningAgentsPolicy` | 인코더→정책(GRU 메모리)→디코더 3개 네트워크로 추론 | `LearningAgentsPolicy.h:71`, `MakePolicy(...)` :105-117 |
| `ULearningAgentsCritic` | 할인 보상 추정(PPO 용) | `LearningAgentsCritic.h:47`, `EvaluateCritic()` :124 |
| `ULearningAgentsTrainingEnvironment` | 보상·완료(Completion)·에피소드 리셋 정의 | `LearningAgentsTrainingEnvironment.h:64, 87` (BlueprintNativeEvent) |
| `ULearningAgentsPPOTrainer` | PPO 학습 루프 | `LearningAgentsPPOTrainer.h:263, 347, 378` |
| `ULearningAgentsImitationTrainer` | BC 학습 | `LearningAgentsImitationTrainer.h:225, 319` |
| `ULearningAgentsFlowMatching` (5.8 신규) | 플로우 매칭 정책(행동 청크, ODE 스텝) | `LearningAgentsFlowMatching.h:38, 51, 55, 106` |
| `ULearningAgentsRecorder / ULearningAgentsRecording` | 관측·행동 궤적 녹화/저장 | `LearningAgentsRecorder.h:46, 113, 117, 141`, `LearningAgentsRecording.h:108, 142, 146, 206` |
| `ALearningAgentsGymBase / ALearningAgentsGymsManager / ULearningAgentsEntitiesManagerComponent` (5.8 신규) | 다중 학습장(Gym) 스폰, 시드 있는 `FRandomStream`, 엔티티 랜덤 배치 | `LearningAgentsGym.h:25, 56-59`, `LearningAgentsGymsManager.h:41, 50, 74`, `LearningAgentsEntitiesManagerComponent.h:59, 76` |

#### 관측/행동 스키마를 C++ 로 정의하는 방식

`LearningAgentsInteractor.h` 의 오버라이드 지점(모두 `UFUNCTION(BlueprintNativeEvent, ... ForceAsFunction)`):

```cpp
// LearningAgentsInteractor.h:81-82
UE_API void SpecifyAgentObservation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement, ULearningAgentsObservationSchema* InObservationSchema);
// :92
UE_API void GatherAgentObservation(FLearningAgentsObservationObjectElement& OutObservationObjectElement, ULearningAgentsObservationObject* InObservationObject, const int32 AgentId);
// :116-117
UE_API void SpecifyAgentAction(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema);
// :128
UE_API void PerformAgentAction(const ULearningAgentsActionObject* InActionObject, const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId);
```

스키마 빌더(정적 함수, 모두 `LearningAgentsObservations.h` / `LearningAgentsActions.h`):

| 종류 | 함수 | 줄 |
|---|---|---|
| 연속 관측 | `SpecifyContinuousObservation(Schema, Size, Normalization…)` | Observations.h:398 |
| 배타 이산 관측 | `SpecifyExclusiveDiscreteObservation(Schema, Size)` | :410 |
| 구조체 관측 | `SpecifyStructObservation(Schema, TMap<FName, Element>)` | :491 |
| **가변 집합 관측(적 N마리)** | `SpecifySetObservation(Schema, Element, MaxNum, AttentionEncodingSize=32, AttentionHeadNum=4, ValueEncodingSize=32)` | :645 |
| 정적 배열 | `SpecifyStaticArrayObservation(Schema, Element, Num)` | :627 |
| 위치/방향 등 기하 | `SpecifyLocationObservation` :826, `SpecifyDirectionObservation` 등 | :826 이후 |
| 연속 행동 | `SpecifyContinuousAction(Schema, Size…)` | Actions.h:315 |
| 배타 이산 행동(스킬 선택) | `SpecifyExclusiveDiscreteAction(Schema, Size)` / `SpecifyNamedExclusiveDiscreteAction` | :329, :356 |
| 열거형 행동 | `SpecifyEnumAction` | :586 |
| 방향 행동 | `SpecifyDirectionAction` | :810 |

관측·행동 벡터를 직접 읽고 쓰는 비-블루프린트 API 도 있어, 우리 시뮬레이터가 float 배열을 직접 채우는 것도 가능하다: `GetObservationVector(TArray<float>&, int32& Hash, AgentId)` :203, `SetObservationVector(...)` :234, `SetActionVector(...)` :256.

행동 마스킹: `MakeAgentActionModifier(...)` 오버라이드로 "지금 쓸 수 없는 스킬" 을 마스크할 수 있다(`LearningAgentsInteractor.h` MakeAgentActionModifier; 샘플링 시 `SampleDistributionMultinoulliMasked` 사용, `LearningAction.cpp:2329`).

#### 정책 네트워크 설정

```cpp
// LearningAgentsPolicy.h:34-66  FLearningAgentsPolicySettings
int32 HiddenLayerNum = 1;  int32 HiddenLayerSize = 128;
ELearningAgentsMemoryCell MemoryCell = ELearningAgentsMemoryCell::LearningAgentsGRU;
int32 MemoryStateSize = 64;  float InitialEncodedActionScale = 0.1f;
ELearningAgentsActivationFunction ActivationFunction = ELU;  bool bUseParallelEvaluation = true;
```
기본이 GRU 메모리(64) 포함 순환 정책이라는 점에 주의: 메모리 상태는 `GetMemoryState / SetMemoryState(AgentId, TArray<float>)` 로 읽고 쓸 수 있어(`LearningAgentsPolicy.h:201, 210`) 재현 시 상태 복원이 가능하다.

#### 학습이 어디서 도는가 — 외부 파이썬 프로세스

| 사실 | 근거 |
|---|---|
| 게임이 파이썬 프로세스를 `FPlatformProcess::CreateProc` 로 스폰 | `LearningCore/Source/LearningTraining/Private/LearningTrainer.cpp:22-38` (`FSubprocess::Launch`) |
| 명령줄: `python.exe "<Engine>/Plugins/Experimental/LearningCore/Content/Python/train.py" "<TaskName>" -p "<CustomTrainerPath>" -m "<TrainerModule>" --nne-cpu-path "<BasicCpu Python>" SharedMemory "<Intermediate>/<Task><Id>" -g "<GUID>"` | `LearningExternalTrainer.cpp:80-91` |
| 파이썬 실행 파일 = `<Intermediate>/PipInstall/Scripts/python.exe` (Windows) | `LearningTrainer.cpp:1640-1644` |
| site-packages = `Engine/Plugins/Experimental/PythonFoundationPackages/Content/Python/Lib/<Platform>/site-packages` | `LearningTrainer.cpp:1649-1651` |
| 트레이너 파이썬 모듈: `learning_core.train_ppo`(기본), `learning_core.train_behavior_cloning`, `learning_core.train_flow_matching` | `train.py:44`, `LearningCore/Content/Python/learning_core/*.py`, `LearningAgentsTrainer.h:159` |
| 통신 방식 두 가지: `SharedMemory`, `Socket` | `train.py:50, 58`; `FSharedMemoryTrainer` / `FSocketTrainer` `LearningExternalTrainer.h:227, 399` |
| 소켓 설정 기본: `127.0.0.1:48491`, `bUseExternalTrainingProcess`(외부에서 미리 띄운 트레이너에 접속) | `LearningAgentsCommunicator.h:61, 65, 73` |
| 학습 루프: 리플레이 버퍼가 차면 `SendReplayBuffer` 후 `ReceiveNetworks` 로 **게임 스레드가 블로킹 대기**(1ms 슬립 폴링, 타임아웃) | `LearningAgentsPPOTrainer.cpp:614, 624`; `LearningSharedMemoryTraining.cpp:44-58` |
| 파이썬 쪽: `torch.manual_seed(seed); torch.set_num_threads(1)`; 디바이스 `GPU` 요청 시 CUDA 없으면 CPU 로 폴백 | `train_ppo.py:60-61`; `train_common.py:1102-1112` |
| PPO 설정: `MaxEpisodeStepNum=512`, `IterationsPerGather=32`, `RandomSeed=1234`, `DiscountFactor=0.99`, `Device=GPU`, `bSaveSnapshots=false` | `LearningAgentsPPOTrainer.h:44, 121, 221, 228, 232, 245` |
| BC 설정: `BatchSize=128`, `Window=64`, `ObservationNoiseScale=0`, `RandomSeed=1234` | `LearningAgentsImitationTrainer.h:79, 87, 106, 110` |
| 게임 설정 강제: 학습 시작 시 고정 타임스텝(기본 60Hz), `t.MaxFPS` 해제 등을 적용하고 종료 시 복원 | `LearningAgentsTrainer.h:36, 44`; `LearningAgentsTrainer.cpp:45-60`; `LearningAgentsPPOTrainer.cpp:279` |

`RunTraining()` 은 매 프레임 호출하도록 설계되어 있다: 첫 호출에 `BeginTraining` + `RunInference`, 이후 호출마다 `GatherCompletions → GatherRewards → ProcessExperience → RunInference` (`LearningAgentsPPOTrainer.cpp:675-711`). 비-에디터 빌드에서 학습이 실패하면 프로세스를 종료 코드 99 로 끝낸다(`:691-693`, `#if !WITH_EDITOR`).

#### 필요한 파이썬 패키지

| 출처 | 패키지 | 근거 |
|---|---|---|
| PythonFoundationPackages | `numpy==1.26.4`, `scipy==1.11.4`, `protobuf`, `requests`, `sympy` 등 | `PythonFoundationPackages.uplugin:26-…` |
| PythonMLPackages | Windows/Linux `torch==2.5.1+cu124`(+torchvision, torchaudio, triton), Mac `torch==2.1.0` | `PythonMLPackages.uplugin:45, 57, 68` |
| LearningAgents 자체 | `boto3`, `botocore`, `smart_open`(S3 스냅샷 경로용), `wrapt` 등 | `LearningAgents.uplugin` PythonRequirements; `train_common.py:12` `from smart_open import open` |
| pip 설치 트리거 | 에디터 시작 시 `bRunPipInstallOnStartup` 또는 `-ForcePipInstallOnInit` | `PythonScriptPlugin.cpp:1361-1368` |
| PythonScriptPlugin 자체는 에디터 타깃 전용 | `PythonScriptPlugin.uplugin:35-36` (`TargetAllowList: Editor`) |

#### 헤드리스/서버 빌드에서 학습 가능성

- 코드 근거: 학습 모듈은 Runtime 타입이고 `UnrealEd` 는 에디터 빌드에서만 링크(`LearningAgentsTraining.Build.cs:33-36`). 비-에디터에서는 `NonEditorEngineRelativePath / NonEditorIntermediateRelativePath / NonEditorCustomTrainerModulePath` 를 반드시 채워야 하고 비어 있으면 경고만 내고 진행한다(`LearningAgentsTrainer.h:139, 147`, `LearningAgentsTrainer.cpp:186-216`).
- 파이썬 실행 파일은 여전히 `<Intermediate>/PipInstall` 을 가리키므로(`LearningTrainer.cpp:1644`), **패키지 빌드에서도 에디터가 만든 PipInstall 폴더(또는 동일 구조의 venv)에 접근 가능해야** 한다. 또는 `bUseExternalTrainingProcess=true` + `Socket` 통신으로 외부에서 직접 `train.py … Socket <addr> <tmpdir>` 를 띄우면 게임 쪽은 파이썬 경로가 필요 없다(`LearningAgentsCommunicator.h:73`, `LearningAgentsCommunicator.cpp:140`).
- 커맨드렛: 엔진 코드에 Learning Agents 전용 커맨드렛은 없다(`LearningAgents/Source` 에 `Commandlet` 문자열 없음 — grep 결과 없음). `RunTraining` 이 프레임 루프를 전제로 하므로 우리 쪽에서 `World->Tick` 루프를 도는 커맨드렛/자동화 테스트 안에서 매 스텝 `RunTraining()` 을 호출하면 된다(설계 제안, 엔진이 보장하는 사실은 아님).
- 웹: Epic 커뮤니티에 "Headless Training & Network Snapshots (5.5)" 튜토리얼이 존재한다(URL: https://dev.epicgames.com/community/learning/tutorials/DPDd/unreal-engine-headless-training-network-snapshots-5-5, 2024~2025년 게시). 본문은 SPA 렌더링이라 도구로 받아오지 못했다 → 구체 명령줄은 **미확인**.

### 2) 추론

| 질문 | 사실 | 근거 |
|---|---|---|
| 저장 형식 | `ULearningAgentsNeuralNetwork : UDataAsset` 이 `ULearningNeuralNetworkData* NeuralNetworkData` 를 보유. `LoadNetworkFromSnapshot(FFilePath)` / `SaveNetworkToSnapshot(FFilePath)` / `LoadNetworkFromAsset` / `SaveNetworkToAsset` | `LearningAgentsNeuralNetwork.h:75, 95, 103, 130` |
| 스냅샷 바이너리 | 매직 `0x1e9b0c80`, 버전 `1`, 이어서 파일데이터. 파이썬 `save_snapshot_to_file` 과 호환 | `LearningNeuralNetwork.cpp:45-46, 49-67, 85-89`; `train_common.py:142-151` |
| 모델 데이터 타입 | `UNNEModelData`, 파일 타입 문자열 `ubnne` | `NNERuntimeBasicCpu.cpp:20` |
| 런타임 경로 | `GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeBasicCpu"))` → `CreateModelCPU(ModelData)` → 인스턴스 `CreateModelInstanceCPU()`; 배치 크기 `MaxBatchSize` 로 텐서 형태 고정 | `LearningNeuralNetwork.cpp:162-168, 278-294` |
| 배치 추론 | `FNeuralNetworkInference::Evaluate(Output[Batch,Out], Input[Batch,In])`; `Batch > MinParallelBatchSize(16)` 이면 `ParallelFor` 로 인스턴스 분할, 각 인스턴스 `RunSync` | `LearningNeuralNetwork.h:59-62, 88`; `.cpp:321-355` |
| 정책 평가 순서 | `RunInference`: `GatherObservations → MakeActionModifiers → EncodeObservations → EvaluatePolicy → DecodeAndSampleActions(Noise) → PerformActions` | `LearningAgentsPolicy.cpp:795-810` |
| 커널 구현 | 레이어별 ISPC 커널(SIMD), 스레드 없음 → 같은 CPU·같은 ISA 타깃이면 결정적. (다른 ISA(AVX2 vs AVX512) 간 부동소수 합 순서 차이 가능성은 미확인) | `NNERuntimeBasicCpuModel.cpp:235-986` 의 `ispc::…` 호출 |
| 샘플링 난수 | `SampleVectorFromDistributionVector(uint32& InOutRandomState, …, ActionNoiseScale)`; 에이전트별 `Seeds[AgentId]` 사용 | `LearningAction.h:820-823`; `LearningAgentsPolicy.cpp:776-786` |
| 시드 결정 | `GlobalSeed = Seed`(MakePolicy 인자, 기본 1234), 에이전트 추가/제거/리셋 시 `Random::SampleIntArray(Seeds, GlobalSeed, AgentIds)` 로 파생 → **에이전트 추가 순서가 같으면 시드가 같다** | `LearningAgentsPolicy.cpp:427-429, 476, 495, 514`; `LearningRandom.h:291-294` |
| 노이즈 0 = 완전 결정 | 정규분포: `Scale < UE_SMALL_NUMBER` 면 `Output = Mean`; 다항(Multinoulli): 같은 조건에서 argmax, 아니면 Gumbel-max 샘플링; 베르누이도 동일 분기 | `Learning.ispc:305-311, 331, 354`; `LearningRandom.cpp:413-436` |
| 난수 생성기 성질 | 상태를 해시 조합하는 카운터 방식(`State ^ 0x… ^ Int(ElementIdx ^ …)`) → 호출 순서 무관, 스레드 안전 | `LearningRandom.cpp:431` |

**비용 추정 근거:** 정책은 MLP(은닉 1×128) + GRU(64) 규모가 기본(`LearningAgentsPolicy.h:41-54`). 평가는 배치 텐서 1회 `RunSync` 이며 에이전트 수만큼 행이 늘어난다. 인코더/디코더가 관측 스키마에 따라 추가되고, `SpecifySetObservation` 은 어텐션(헤드 4, 인코딩 32)을 포함하므로 "적 N마리 집합" 관측이 커질수록 비용이 늘어난다(`LearningAgentsObservations.h:645`). 구체 ms 수치는 엔진 소스에 없다 → **미확인(프로파일 필요)**.

### 3) Replay 모듈(LearningAgentsReplay)

| 사실 | 근거 |
|---|---|
| `ULearningAgentsReplaySubsystem : UGameInstanceSubsystem` | `LearningAgentsReplaySubsystem.h:58` |
| API: `DoesPlatformSupportReplays()`, `PlayReplay(Entry)`, `StopRecordingReplay()`, `RecordClientReplay(APlayerController*)`, `SeekInActiveReplay(float)`, `GetReplayLengthInSeconds()`, `GetReplayCurrentTime()` | `.h:78-90` 및 인접 |
| 구현은 `GameInstance->StartRecordingReplay / StopRecordingReplay / PlayReplay`, `World->GetDemoNetDriver()` 위임 | `LearningAgentsReplaySubsystem.cpp:50, 58, 68, 108` |
| `UAsyncAction_LearningAgentsQueryReplays : UBlueprintAsyncActionBase` 로 리플레이 목록 조회 | `AsyncAction_LearningAgentsQueryReplays.h:13` |
| 의존: `NetworkReplayStreaming` | `LearningAgentsReplay.Build.cs` |

즉 이 모듈은 **네트워크 데모 리플레이를 학습 데이터 수집/검토용으로 쓰기 위한 편의 래퍼**다. 관측·행동 궤적 녹화는 `ULearningAgentsRecorder`(`BeginRecording/AddExperience/EndRecording`, `LearningAgentsRecorder.h:113, 141, 117`)와 `ULearningAgentsRecording`(`SaveRecordingToFile/LoadRecordingFromFile/BuildReplayBuffer`, `LearningAgentsRecording.h:146, 142, 206`; 파일 매직 `0x06b5fb26` 버전 1, `LearningAgentsRecording.cpp:27-28`)이 담당한다. 녹화에는 관측/행동 스키마 JSON 이 함께 저장된다(`FLearningAgentsSchema{ObservationSchemaJson, ActionSchemaJson}`, `LearningAgentsRecording.h` 구조체).

### 4) MLAdapter

| 항목 | 사실 | 근거 |
|---|---|---|
| 상태 | 버전 1 / "0.0.1", `IsExperimentalVersion: true`, 모듈 `MLAdapter`(Runtime, PreDefault), `MLAdapterTestSuite` | `Plugins/AI/MLAdapter/MLAdapter.uplugin:3-4, 16, Modules` |
| 의존 플러그인 | `GameplayAbilities`, `EnhancedInput`; 모듈 의존 `AIModule, GameplayAbilities, NNE, RPCLib(Win64/Mac/Linux, WITH_RPCLIB=1)` | `MLAdapter.uplugin Plugins`; `MLAdapter.Build.cs:39-44` |
| RPC 스택 | rpclib 2.2.1(msgpack-rpc, `Engine/Source/ThirdParty/rpclib`); 서버 `new FRPCServer(Port)`; 기본 포트 15151, `-MLAdapterPort=` 로 변경 | `MLAdapterManager.cpp:160-190`; `MLAdapterSettings.h:35`; `MLAdapterManager.cpp:395` |
| 매니저 | `UMLAdapterManager : UObject, FTickableGameObject, FSelfRegisteringExec`; `StartServer(uint16 Port, EMLAdapterServerMode, uint16 ServerThreads=1)`; `SetManualWorldTickEnabled(bool)` | `MLAdapterManager.h:41, 94, 135` |
| 세션 | `UMLAdapterSession : UObject`; `Tick(float)`, `AddAgent()`, `ResetWorld(AgentID)`, `SetManualWorldTickEnabled` | `MLAdapterSession.h:37, 75, 91, 103, 119` |
| 에이전트 | `UMLAdapterAgent`: `Sense / Think / Act / DigestActions(FMLAdapterMemoryReader&) / GetReward / IsDone / GetObservations(FMLAdapterMemoryWriter&)`; 센서·액추에이터 배열 보유 | `MLAdapterAgent.h:74, 91-103, 124, 127, 161` |
| 센서/액추에이터 | `UMLAdapterSensor::GetObservations(...) PURE_VIRTUAL`, `SenseImpl(float)`; `UMLAdapterActuator::Act(float)`, `DigestInputData(...)`; 내장 센서 `AIPerception, Attribute(GAS AttributeSet), Camera, EnhancedInput, Input, Movement`, 액추에이터 `Camera, EnhancedInput, InputKey` | `MLAdapterSensor.h:59, 67`; `MLAdapterActuator.h:28, 31`; `MLAdapterSensor_Attribute.h:35`; `Public/Sensors`, `Public/Actuators` 파일 목록 |
| 추론 에이전트 | `UMLAdapterAgent_Inference`: `UNNEModelData ModelData`, `IModelInstanceCPU Brain`; `GetRuntime<INNERuntimeCPU>` → `CreateModelCPU → CreateModelInstanceCPU` → `RunSync` | `MLAdapterAgent_Inference.h:18, 30, 33`; `.cpp:21, 28, 112` |
| RPC 함수 목록 | `act, add_agent, batch_act, batch_get_observations, batch_get_rewards, batch_is_finished, close_session, configure_agent, create_agent, desc_action_space, desc_observation_space, disconnect, enable_action_duration, enable_manual_world_tick, exit, get_agent_config, get_description, get_name, get_observations, get_recent_agent, get_reward, is_agent_ready, is_finished, is_ready, list_actuator_types, list_functions, list_sensor_types, ping, request_world_tick, reset, wait_for_action_duration` | `Private/**/*.cpp` 의 `Server.bind("…")` 전수 grep |
| 수동 틱 실체 | `Manager::Tick`: 실시간이 아니면 `StepsRequested>0` 일 때만 `Session->Tick(1/WorldFPS)`; 월드 자체는 계속 틱된다. `request_world_tick` 은 `while(StepsRequested>0) Sleep(0)` 바쁜 대기 | `MLAdapterManager.cpp:263-274`; `MLAdapterManager_Server.cpp:35-46` |
| 파이썬 클라이언트 | `Source/python/unreal/mladapter/*`; `install_requires=['gym','msgpack-rpc-python','numpy']`; `import gym` (구형 OpenAI Gym) | `setup.py:27`; `core.py:6` |
| 5.8 유지 상태 | 소스는 남아 있고 컴파일되며 API 문서(5.7)도 존재하나, 버전 문자열 0.0.1 그대로, gymnasium 미이관. 명시적 Deprecated 마크는 소스에서 발견하지 못함 | 웹: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/MLAdapter/FMLAdapter__NewObject (5.7 문서); 공식 폐기 공지는 **미확인** |

### 5) NNE(Neural Network Engine)

| 항목 | 사실 | 근거 |
|---|---|---|
| 위치 | 엔진 런타임 모듈 `Engine/Source/Runtime/NNE` (플러그인 아님) | `Source/Runtime/NNE/Public/NNE.h` 등 |
| 런타임 조회 | `UE::NNE::GetRuntime(const FString& Name)`; 템플릿 `GetRuntime<INNERuntimeCPU>(Name)`; `GetAllRuntimeNames<T>()` | `NNE.h:52, 62-63, 77-78` |
| CPU 인터페이스 | `INNERuntimeCPU::CanCreateModelCPU(UNNEModelData*)`, `CreateModelCPU(UNNEModelData*) → IModelCPU`; `IModelCPU::CreateModelInstanceCPU() → IModelInstanceCPU` | `NNERuntimeCPU.h:61, 75, 87, 31, 45` |
| 실행 | `IModelInstanceRunSync::SetInputTensorShapes(TConstArrayView<FTensorShape>)`, `RunSync(TConstArrayView<FTensorBindingCPU> In, TConstArrayView<FTensorBindingCPU> Out)`; `GetInputTensorDescs/Shapes` | `NNERuntimeRunSync.h:30, 44, 51, 60, 70, 82, 96` |
| 모델 데이터 | `UNNEModelData::Init(const FString& Type, TConstArrayView64<uint8> Buffer, AdditionalBuffers)`, `GetFileType()`, `GetModelData(RuntimeName)`, `SetTargetRuntimes` | `NNEModelData.h:73, 120, 145, 224, 135` |
| ONNX 런타임 플러그인 | `Plugins/NNE/NNERuntimeORT`: 베타(`IsBetaVersion: true`), 플랫폼 `Win64, Linux, LinuxArm64, Mac`; 런타임 이름 `NNERuntimeORTCpu`, `NNERuntimeORTDml`; ORT 1.24.1 | `NNERuntimeORT.uplugin:15, 23`; `NNERuntimeORT.cpp` TEXT 상수; ThirdParty `.tps` "ort.1.24.1" |
| ORT 스레드/결정론 설정 | 프로젝트 설정 `FThreadingOptions{ bUseGlobalThreadPool=false, IntraOpNumThreads=0(기본), InterOpNumThreads=0, ExecutionMode=SEQUENTIAL }` → 결정적 재현을 원하면 `IntraOpNumThreads=1` 권장(0 은 ORT 기본 스레드 수) | `NNERuntimeORTSettings.h:33, 39, 45` |
| 배치 | 배치는 입력 텐서 첫 차원으로 표현하고 `SetInputTensorShapes` 를 형태 변경 때마다 다시 호출. Epic 문서도 "개별 호출보다 배치가 성능상 유리" 명시 | `LearningNeuralNetwork.cpp:346-347`(사용 예); 웹: https://dev.epicgames.com/documentation/unreal-engine/neural-network-engine-overview-in-unreal-engine (5.8 문서, 게시 연도 표기 없음) |
| BasicCpu 빌더 | `UE::NNE::RuntimeBasic::FModelBuilder`: `MakeMLP`, `MakeGRUCell`, `MakeAggregateSet`(집합 어텐션), `MakeSparseMixtureOfExperts`, `MakeConv1d/2d`, `MakeLayerNorm` 등 — C++ 코드로 네트워크 구조를 만들어 `ubnne` 로 직렬화 | `NNERuntimeBasicCpuBuilder.h:125, 274, 428, 491, 529, 577, 543` |
| BasicCpu ↔ PyTorch | `Content/Python/nne_runtime_basic_cpu.py`(매직 `0x0BA51C01`), `nne_runtime_basic_cpu_pytorch.py`(`create_pytorch_module_from_nne_data`) 로 상호 변환 | `nne_runtime_basic_cpu.py:57`; `nne_runtime_basic_cpu_pytorch.py:864` |
| 결정론 일반 | NNE 인터페이스 자체는 결정론 보장을 문서화하지 않음(웹 문서에도 언급 없음). BasicCpu 는 ISPC 단일 스레드 커널, ORT 는 스레드 수 설정에 의존 | 위 근거 종합 |

### 6) 현실적 제약과 TDGame 권장 사용 방식

**제약 (근거 기반)**

| 제약 | 근거 |
|---|---|
| 학습은 파이썬/PyTorch 외부 프로세스 필수. CUDA 없으면 CPU 학습으로 폴백(느림) | `train_common.py:1102-1112` |
| 학습 중 게임 스레드가 트레이너 응답을 블로킹 대기 → 학습 세션은 별도 프로세스로 돌리는 게 맞음 | `LearningSharedMemoryTraining.cpp:44-58`; `LearningAgentsPPOTrainer.cpp:614-624` |
| 파이썬 실행 경로가 `<Intermediate>/PipInstall` 로 하드코딩 → CI/서버 머신에도 같은 폴더 필요(또는 외부 트레이너 + 소켓) | `LearningTrainer.cpp:1644`; `LearningAgentsCommunicator.h:73` |
| 학습(PPO) 자체는 비결정: GPU 커널, 경험 수집 타이밍. 시드는 고정되지만(`RandomSeed`, `torch.manual_seed`) 재현 보장은 없음 | `LearningAgentsPPOTrainer.h:221`; `train_ppo.py:60-61` |
| 추론은 결정적으로 만들 수 있음(노이즈 0 + 고정 시드 + 고정 에이전트 추가 순서) | 2) 절 |
| 보상·완료 설계는 C++ 로 직접 작성해야 하며 헬퍼는 위치/회전/시간 계열만 제공 | `LearningAgentsRewards.h:35, 59, 85`; `LearningAgentsCompletions.h:90, 113, 138` |
| 정책 기본 구조에 GRU 메모리가 포함 → 상태 복원 없이는 같은 관측에도 다른 행동. 밸런스 툴에서는 `MemoryCell=NoMemoryCell` 또는 `MemoryStateSize=0` 권장 | `LearningAgentsPolicy.h:49-54` |
| 플러그인이 Experimental(0.2) → 버전 간 API 변경 잦음(5.8 에서 Gym/FlowMatching/Entities 추가) | `LearningAgents.uplugin`; 5.8 신규 헤더 목록 |
| 어텐션 기반 집합 관측(적 N마리)은 `MaxNum` 을 고정해야 하며 크기에 따라 추론 비용 증가 | `LearningAgentsObservations.h:645` |

**TDGame 권장안**

1. **플레이어 대리 에이전트(밸런스 툴용)** — 가장 가치가 높다. 시뮬레이터 픽스처(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:71-79`, `World->Tick(LEVELTICK_All, Step)`)는 이미 고정 스텝이며 `ULearningAgentsManager::TickComponent` 가 `LEVELTICK_All` 에서만 리스너를 호출하므로(`LearningAgentsManager.cpp:42`) 그대로 호환된다. 관측 = 자기 HP/자원/쿨다운 + `SpecifySetObservation` 으로 몬스터 집합(위치·HP·상태), 행동 = `SpecifyExclusiveDiscreteAction`(스킬/이동/물약) + `SpecifyDirectionAction`. 첫 단계는 **BC**(기획자 플레이를 `ULearningAgentsRecorder` 로 녹화 → `ULearningAgentsImitationTrainer`)로 "사람 같은" 기준선을 만들고, 두 번째 단계에서 PPO 로 "최적 플레이" 상한선을 얻어 둘 사이를 난이도 밴드로 쓴다.
2. **몬스터 행동 파라미터 튜닝** — 몬스터 AI 는 규칙 기반(C++/텍스트 데이터)으로 두고, 규칙의 수치(공격 거리, 스킬 확률, 후퇴 HP 임계 등)를 벡터로 보고 `UE::Learning::FCMAOptimizer`/`FPSOOptimizer`(`IOptimizer::Reset(OutSamples, InitialGuess)` → 시뮬레이션 → `Update(InOutSamples, Losses)`, `LearningOptimizer.h:13-25`)로 목표 지표(승률 50%, 평균 전투 시간 등)에 맞춘다. 파이썬·GPU·외부 프로세스가 전혀 필요 없고 결정적 시뮬레이터와 결합하면 재현 가능하다. 이것이 사용자 우려(생성형 AI 가 분석·수정하기 쉬운 텍스트 정의)와 가장 잘 맞는다.
3. **몬스터 자체를 신경망 정책으로 구동**하는 것은 보스 등 소수 개체에 한정 권장. 이유: (a) 대량 몬스터는 규칙 기반 + 주기 분산이 더 싸고 설명 가능, (b) 신경망 정책은 디버깅·수정이 텍스트로 불가능(가중치), (c) 행동 다양성은 `ActionNoiseScale` 로 조절 가능하나 결정성 요구와 충돌.
4. **MLAdapter 는 사용하지 않는다.** 이유: 정체된 버전, 구형 gym 의존, 바쁜 대기 기반 수동 틱, Learning Agents 와 기능 중복.
5. **ONNX 외부 모델(예: 파이썬에서 학습한 임의 모델)** 이 필요하면 `NNERuntimeORT` 를 켜고 `INNERuntimeCPU` 로 직접 호출하되, 프로젝트 설정에서 `IntraOpNumThreads=1`, `ExecutionMode=SEQUENTIAL` 로 고정해 결정성을 확보한다.

---

## 프로젝트 적용 시사점

**쓸 것**
- `LearningAgents`, `LearningCore`, `NNERuntimeBasicCpu`, `PythonMLPackages`(에디터에서만 필요) 플러그인 활성화. 현재 uproject 에는 없다(`TDGame.uproject`: GameplayAbilities, ModelContextProtocol, AllToolsets, ModelingToolsEditorMode, StateTree, GameplayStateTree 만 활성). `TDGame.Build.cs` 에 `LearningAgents`, `LearningAgentsTraining`, `Learning` 모듈 추가 필요(현재 `Core, CoreUObject, Engine, InputCore, EnhancedInput, AIModule` 만 의존, `TDGame.Build.cs:12-17`).
- 시뮬레이터 전용 `UTDSimInteractor : ULearningAgentsInteractor`(C++), `UTDSimEnvironment : ULearningAgentsTrainingEnvironment`(보상·완료), 정책 자산 `ULearningAgentsNeuralNetwork` 3개(인코더/정책/디코더) — 모두 코드와 DataAsset 으로 관리되며 에디터 그래프 편집이 없다.
- 결정성 체크리스트: `MakePolicy(Seed 고정)`, 에이전트 `AddAgent` 순서 고정, `RunInference(0.0f)`, `MemoryStateSize=0`(또는 에피소드 시작 시 `SetMemoryState` 로 0 초기화), `bUseParallelEvaluation` 은 켜도 무방(행 독립).
- 헤드리스 학습: 자동화 테스트/커맨드렛에서 `World->Tick` 루프 + 매 스텝 `PPOTrainer->RunTraining(...)`. 트레이너는 `Socket` + `bUseExternalTrainingProcess=true` 로 별도 셸에서 `python train.py Task -m learning_core.train_ppo --nne-cpu-path … Socket 127.0.0.1:48491 <tmp>` 를 미리 띄우는 편이 CI 에서 다루기 쉽다(파이썬 경로 하드코딩 회피).

**피할 것**
- MLAdapter 전체.
- 게임 프로세스 안에서 학습을 돌리며 동시에 플레이 가능한 프레임을 기대하는 것(블로킹 대기).
- 대량 몬스터 각각에 신경망 정책 + 집합 어텐션 관측을 붙이는 것(비용·설명 불가).
- 학습 결과의 재현을 요구하는 것(학습은 비결정, 추론만 결정).

**추가 고려사항(사용자가 언급하지 않은 것)**
- 관측/행동 스키마 해시(`OutObservationCompatibilityHash`, `LearningAgentsInteractor.h:203`)가 바뀌면 기존 정책·녹화가 무효화된다 → 스키마 버전 관리 규약 필요.
- 녹화(`ULearningAgentsRecording`)와 스냅샷은 바이너리라 Git 에 바로 올리기 부적합 → LFS 또는 외부 저장(`smart_open` 으로 S3 경로 지원, `train_common.py:12`).
- 5.8 의 `ALearningAgentsGymsManager`(시드 `RandomSeed=1234`, `LearningAgentsGymsManager.h:50`)는 한 레벨에 여러 학습장을 복제하는 용도이며, 우리 시뮬레이터가 `UWorld::CreateWorld` 로 독립 월드를 만든다면 굳이 쓸 필요가 없다.
- `ULearningAgentsFlowMatching`(행동 청크 `ActionChunkSize`, `ODEStepsNum=8`)은 BC 의 대안이지만 추론 시 ODE 스텝 수만큼 디노이저를 반복 평가하므로 비용이 8배 수준으로 커진다(`LearningAgentsFlowMatching.h:38, 51, 106`). 밸런스 툴에는 과하다.

---

## 미확인·미해결 질문

1. Epic "Headless Training & Network Snapshots (5.5)" 튜토리얼의 실제 명령줄(`-game -nullrhi` 등)과 5.8 에서의 변경 여부 — 페이지 본문 미수신.
2. MLAdapter 의 공식 폐기(Deprecated) 공지 여부 — 소스에는 마크 없음, 웹 공지 미확인.
3. NNERuntimeBasicCpu ISPC 커널이 서로 다른 ISA 타깃(SSE4/AVX2/AVX512)에서 비트 동일 결과를 내는지 — 소스로는 확인 불가, 실측 필요.
4. 정책 규모별(관측 크기, Set MaxNum) 몬스터 N마리 추론 ms — 프로파일 필요.
5. `PythonMLPackages` 의 torch 2.5.1+cu124 가 현재 개발 머신 GPU 드라이버와 호환되는지 — 환경 의존.
6. 패키지(비-에디터) 빌드에서 `NonEditor*Path` 설정 후 학습이 실제로 동작하는지 — 코드 경로는 존재하나 실행 검증 안 함.
7. 학습된 정책을 `ubnne` → ONNX 로 내보내는 공식 경로 — 엔진 소스에 없음(파이썬 `nne_runtime_basic_cpu_pytorch.py` 로 PyTorch 모듈 복원 후 수동 `torch.onnx.export` 가능성만 있음, 미검증).

[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 04. 결정론 전투 시뮬레이터(밸런스 툴)

## 이 문서가 답하는 질문

1. 시뮬레이터를 어떤 실행 형태로 만들고, 왜 커맨드렛과 자동화 테스트가 같은 세션 클래스를 공유하는가?
2. 고정 스텝 루프가 비트 단위로 재현되려면 어떤 줄이 빠지면 안 되는가(`++GFrameCounter`, 난수 스트림, 순서 규약)?
3. 물리·내비게이션·애니메이션을 시뮬에서 빼고도 "게임과 같은 판정"을 어떻게 보증하는가?
4. 입력(플레이어 조건 × 몬스터 조합)과 출력(승률·분위수·리썰 위험)의 정확한 스키마는 무엇인가?
5. 결정론이 깨졌을 때 최초 이탈 스텝을 어떻게 찾고, 골든 해시는 언제 어떻게 갱신하는가?

## 결론 요약(결정 문장)

- 세션 클래스 `FTDCombatSimSession` 하나를 커맨드렛 `-run=TDCombatSim`(배치)과 자동화 테스트 `TDGame.CombatSim.*`(CI 게이트)이 공유한다. 커맨드렛은 런타임 `TDGame` 모듈에 둔다(D13·D26).
- 스텝은 1/64초 고정. 매 스텝 `++GFrameCounter` → `FApp` 시간 갱신 → `World->Tick(LEVELTICK_All, Step)` → 상태 해시 순이며, 시뮬 러너는 두뇌 서브시스템의 `Step` 을 직접 부르지 않는다(D15·D27).
- 난수는 시나리오 마스터 시드에서 `HashCombine` 으로 파생한 이름 있는 `FRandomStream` 만 쓴다. 전역 `FMath::FRand` 계열은 grep 자동화 테스트로 금지한다(D29).
- 순서는 `SimulationId` 하나로 통일한다. 액터 이름·`GetUniqueID` 를 키로 쓰지 않는다(D30).
- 물리·내비·애니메이션은 기본 배제한다. 몬스터 공격은 Phase 0~2 에서 `UTDDamageDefinition`, Phase 3 부터 `FTDAttackTimetable` 이 권위이고 애님 노티파이는 표현·오라클 전용이다(D19~D21).
- 상태 해시는 스텝별 FNV-1a 64비트 계층 체인이며 난수 스트림 현재 시드를 포함한다. 게이트는 같은 프로세스 2회 + 다른 프로세스 1회 + 골든 해시다(D31).
- "게임 = 시뮬 비트 동일"은 약속하지 않는다. A 비트 동일 / B 이벤트 등가(±1 스텝) / C 통계 등가 3단으로 정의한다(D32).
- 병렬은 프로세스 팬아웃, 지표는 승률 ± 표준오차·분위수·리썰 위험·행동 점유율·교체율, 로그는 JSONL 한 사고 한 줄이다(D33~D35).

---

## 1. 실행 형태 비교와 결정(D26)

| 형태 | 엔진 루프 | 속도·통제 | 부작용 | 판정 |
|---|---|---|---|---|
| (a) 커맨드렛 `UCommandlet::Main` + `UWorld::CreateWorld` + 직접 `World->Tick` | 없음(`Main` 1회) | 스텝 비용 = `UWorld::Tick` 순수 비용, 실시간 상한 없음 | 렌더 NullRHI, GameMode 없음 | **주력(배치)** |
| (b) 자동화 테스트 `FAutomationTestBase` + 직접 Tick | 에디터 프로세스 안 | `RunTest` 안에서 직접 루프를 돌면 (a) 와 같은 속도 | 레이턴트 커맨드는 프레임당 1회·벽시계 대기 | **CI 게이트(레이턴트 금지)** |
| (c) `-game -nullrhi -ExecCmds` | 있음 | `-UseFixedTimeStep -FPS=` 로 대기 없이 돌지만 렌더·오디오·슬레이트·자동화 워커 오버헤드 포함 | GameMode/GameState 의존 | 불채택 |
| (d) 서버 타깃 빌드 | 있음(단일 스레드) | 스레딩 비활성은 유리 | 서버 넷모드가 GAS 예측·리플리케이션 경로, `bTriggerOnDedicatedServer` 노티파이 필터 | 불채택 |
| (e) Gauntlet | C# 오케스트레이션 | 다중 프로세스 실행·수집 | 별도 툴 체인 | 후순위(파이썬 런처로 대체, §12) |

근거: engine-determinism-headless 결론 8·§6 표(`LaunchEngineLoop.cpp:4017-4055`(엔진 클래스 생성), `:4110`(`Commandlet->Main` 1회 호출), `:4125`, `AutomationWorkerModule.cpp:66-68`, `AutomationCommon.cpp:797-803`), web-balance-simulation-tools 결론 4·5.

결정 사항의 구체 형태:

| 항목 | 값 |
|---|---|
| 세션 클래스 | `FTDCombatSimSession` (`Source/TDGame/CombatSim/`) — 시나리오 1개 × 시드 1개를 `Run` 하고 `FTDCombatSimResult` 반환 |
| 커맨드렛 | `UTDCombatSimCommandlet`, 런타임 `TDGame` 모듈. `UCommandlet` 은 Engine 모듈이며 기존 `UTDDamageExamplesCommandlet` 이 `Source/TDGame/Combat/` 에 있는 선례를 따른다(project-current-combat-code 결론 10) |
| 배치 명령 | `UnrealEditor-Cmd.exe TDGame.uproject -run=TDCombatSim -Scenario=<json> -SeedStart=N -SeedCount=M -Out=<dir> -nullrhi -unattended -nosplash -nosound -FixedSeed -onethread -abslog=<file>` |
| 게이트 명령 | `UnrealEditor-Cmd.exe TDGame.uproject -ExecCmds="Automation RunTests TDGame.CombatSim" -TestExit="Automation Test Queue Empty" -unattended -NullRHI -NoSplash -NoSound -log=<name>.log` |
| 시작 시 강제 콘솔 변수 | `tick.AllowAsyncComponentTicks 0`, `tick.AllowConcurrentTickQueue 0`(기본값 유지 확인), `AbilitySystem.DisableGameplayCues 1` |

`-FixedSeed` 는 엔진 내부 전역 난수의 방어선일 뿐 결정론의 근거가 아니다(engine-determinism-headless 결론 4). `-onethread` 가 커맨드렛에서 태스크 그래프 워커 수를 실제로 줄이는지는 미확인(같은 조사 미확인 7)이므로, 결정론은 §2 의 단일 틱 함수 구조와 §7 의 해시 게이트로 확보한다.

---

## 2. 고정 스텝 루프(D27, D15, D28)

### 2.1 픽스처 승격

기존 `FTDScopedCombatWorld`(`Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:29-89`, 익명 네임스페이스)를 `Source/TDGame/CombatSim/TDScopedCombatWorld.h` 공용 헤더로 옮긴다. 기존 `Tick(Duration, Step = 0.02f)` 은 그대로 남겨 27개 테스트(`TDDamageSystemTests` 19개 + `TDDamageHomingTests` 8개)가 0.02 스텝을 유지하게 하고(D28), 세션은 새 `StepOnce()` 만 쓴다. 나머지 1개(`FTDMeleeAttackNotifySweepTest`)는 별도 픽스처 `FTDScopedMeleeWorld`(`TDMeleeAttackNotifyTests.cpp:25`, `Tick(Duration, Step)` 기본값 없음)를 쓰며 0.5초 스텝을 요청한다 — 이 두 번째 픽스처의 처리는 §2.4·§15 에 둔다.

```cpp
struct FTDScopedCombatWorldParams
{
	float StepSeconds = 1.f / 64.f;
	bool bIsFixedStep = false;
	bool bCreateNavigation = false;
	bool bCreateAISystem = false;
	bool bSimulatePhysics = false;
};

FTDScopedCombatWorld::FTDScopedCombatWorld(const FTDScopedCombatWorldParams& Params)
	: StepSeconds(Params.StepSeconds)
{
	UWorld::InitializationValues Values;
	Values.CreatePhysicsScene(true).ShouldSimulatePhysics(Params.bSimulatePhysics).EnableTraceCollision(true)
		.CreateNavigation(Params.bCreateNavigation).CreateAISystem(Params.bCreateAISystem);
	const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("TDCombatSimWorld"));
	World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage(), true, ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	if (Params.bIsFixedStep)
	{
		AWorldSettings* Settings = World->GetWorldSettings();
		Settings->MaxUndilatedFrameTime = StepSeconds;
		Settings->MinUndilatedFrameTime = StepSeconds;
	}
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->SetBegunPlay(true);
}

void FTDScopedCombatWorld::StepOnce()
{
	++GFrameCounter;
	FApp::SetDeltaTime(StepSeconds);
	FApp::SetCurrentTime(FApp::GetCurrentTime() + StepSeconds);
	World->Tick(LEVELTICK_All, StepSeconds);
	++StepIndex;
}
```

### 2.2 각 줄이 필요한 이유

| 줄 | 이유 | 근거 |
|---|---|---|
| `++GFrameCounter` | 틱 함수 큐잉이 `GFrameCounter` 하위 32비트로 "이번 프레임 방문 여부"를 판정하므로 카운터가 멈추면 두 번째 스텝부터 아무것도 틱되지 않는다. `FTimerManager::Tick` 도 `LastTickedFrame == GFrameCounter` 면 즉시 반환해 GE 지속시간·쿨다운이 멈춘다 | engine-determinism-headless 결론 1(`TickTaskManager.cpp:621-624`), engine-gas-determinism 결론 2(`TimerManager.cpp:1136-1139`) |
| `FApp::SetDeltaTime/SetCurrentTime` | GC 주기와 코어 티커가 `FApp` 시간을 누적한다. 갱신하지 않으면 직접 루프에서 GC 가 사실상 돌지 않고, 갱신하면 아래 "시나리오마다 새 월드" 행처럼 게임 시간 기준으로 돈다 | engine-determinism-headless 결론 10(c)(`UnrealEngine.cpp:2263-2286`), §7 표 "FApp 시간 미갱신" |
| 스텝 1/64초 | 이진 소수라 `float` 델타를 `double` 내부 시간에 누적해도 오차가 없고, 만료 스텝이 항상 `N/스텝 + 1` 로 고정된다. 타이머 발화 조건이 엄격 초과(`InternalTime > ExpireTime`, `TimerManager.cpp:1212`)라 1/64 에서도 1초 GE 는 64번째가 아니라 65번째 스텝에 만료된다. 0.02f 도 같은 +1(51번째)이지만 누적 오차 때문에 값이 흔들릴 수 있다. 골든·기대값·`cooldown_steps`·`duration_steps` 해석은 모두 N+1 로 고정한다 | engine-gas-determinism 결론 3·상세 "만료 프레임 경계"(`TimerManager.cpp:1212`), [06 §3 C2](06-beyond-the-ask.md) |
| `MaxUndilatedFrameTime`·`MinUndilatedFrameTime` = 스텝 | `AWorldSettings::FixupDeltaSeconds` 가 델타를 기본 0.0005~0.4초로 조용히 클램프한다(기본값 0.4초, `BaseGame.ini:198-199`). 1/64 는 클램프에 걸리지 않지만 두 값을 스텝으로 고정해 "요청 스텝 ≠ 실제 진행" 상황을 원천 차단한다 | project-current-combat-code 결론 3(`LevelTick.cpp:1590-1601`, `WorldSettings.cpp:334-341`) |
| 월드 초기화값 내비·AI·물리 끔 | 시뮬 몸은 컨트롤러 없는 수학 이동 `ATDSimCombatant`(D16)이고 근접 탐색은 격자(D17)라 내비·AI 시스템이 필요 없다. 내비메시 비동기 타일 빌드는 결정론 위험이다 | engine-determinism-headless 결론 7·9(`World.cpp:2850`) |
| 종료 조건 = `MaxSteps` | 초 단위 종료는 클램프·누적 오차에 취약하다. 120초 = 7,680스텝 | D27 |
| 시나리오마다 새 월드 + `CollectGarbage(RF_NoFlags)` | GAS `StartWorldTime` 이 float 이라 장시간 월드는 분해능이 무너진다. `StepOnce` 가 `FApp` 시간을 갱신하므로 `UWorld::Tick` 끝의 `ConditionalCollectGarbage` 가 델타를 누적해 자동 GC 는 게임 시간 60초(`gc.TimeBetweenPurgingPendingKillObjects` 기본값)를 넘긴 첫 스텝, 즉 약 3,840스텝째에 시뮬 도중에도 돈다. 시점이 스텝으로 고정되므로 결정적이나, GC 정지 시간이 벽시계 지표에 섞이지 않게 시나리오 사이에 `CollectGarbage(RF_NoFlags)` 를 추가로 호출한다 | engine-gas-determinism §1 해석, engine-determinism-headless 결론 10(c)·§7 표 "GC 시점"(`LevelTick.cpp:1970`, `UnrealEngine.cpp:2265`, `:1777-1780`) |

### 2.3 세션 루프와 이중 스텝 금지

```cpp
FTDCombatSimResult FTDCombatSimSession::Run(const FTDCombatSimScenario& Scenario, int32 Seed)
{
	FTDScopedCombatWorldParams Params;
	Params.bIsFixedStep = true;
	Params.bSimulatePhysics = Scenario.bNeedsPhysics;
	FTDScopedCombatWorld Sim(Params);
	UTDDamageSubsystem* Damage = Sim.World->GetSubsystem<UTDDamageSubsystem>();
	Damage->InitializeRandomStreams(Seed);
	SpawnPlayerProxy(Sim, Scenario.Player);
	SpawnMonstersInOrder(Sim, Scenario.Monsters);
	FTDCombatStateHasher Hasher;
	FTDCombatSimResult Result(Scenario.Id, Seed);
	while (Sim.GetStepIndex() < Scenario.MaxSteps && !HasOutcome(Sim))
	{
		Sim.StepOnce();
		Hasher.AppendStep(Sim);
		if (Log)
		{
			Log->WriteStep(Sim, Hasher);
		}
	}
	Result.Finalize(Sim, Hasher);
	return Result;
}

int32 UTDCombatSimCommandlet::Main(const FString& Params)
{
	for (int32 Seed = SeedStart; Seed < SeedStart + SeedCount; ++Seed)
	{
		Results.Add(Session.Run(Scenario, Seed));
		CollectGarbage(RF_NoFlags);
	}
	return WriteOutputs(Results) ? 0 : 1;
}
```

루프 안에 `UTDMonsterThinkSubsystem::Step` 호출이 없다는 점이 핵심이다. 두뇌·이동·판정은 서브시스템의 단일 고우선 틱 함수(TG_PrePhysics)가 `World->Tick` 안에서 정확히 1회 처리한다(D15, [03 틱과 규모](03-tick-and-scale.md)). 러너가 `Step` 을 부르고 다시 `World->Tick` 이 틱 함수를 돌리면 스텝당 사고가 2회 발생하고 게임과 시뮬의 스텝 의미가 갈라진다. 서브시스템은 `LastSteppedFrame == GFrameCounter` 면 `ensure` 로 거부하고, 게이트 테스트 `TDGame.CombatSim.Determinism.OneStepPerFrame` 이 "`RunKernelStep`(커널 스텝) 호출 횟수 == `GFrameCounter` 증가량" 을 검사한다. 사고 횟수가 아니라 커널 스텝 횟수인 이유는 사고가 L0 에서도 10Hz(6~7스텝마다 1회, D22)라 스텝 수와 같지 않기 때문이다. 게임의 `AccumulateAndStep` 은 프레임당 최대 4스텝을 허용하므로([03](03-tick-and-scale.md) `MaxStepsPerFrame = 4`) 이 1:1 등식은 프레임 델타 = 1/64 인 시뮬에서만 성립한다. 플레이어 대리 봇도 같은 JSON 두뇌 형식(D12)이라 `SimulationId 0` 슬롯으로 같은 틱 함수 안에서 사고한다.

### 2.4 기존 테스트의 스텝(D28)

기존 28개 자동화 테스트는 현재 스텝을 유지한 채 Phase 0 에서 실제 통과부터 확인한다(GAS 전환 후 미실행 상태). 27개는 `FTDScopedCombatWorld` 0.02 스텝이고, 근접 노티파이 테스트 1개는 `FTDScopedMeleeWorld`(`TDMeleeAttackNotifyTests.cpp:25`) 0.5초 스텝이다. `TDMeleeAttackNotifyTests.cpp:171,180` 의 0.5초 스텝은 실제로는 0.4초씩 진행되지만(project-current-combat-code 결론 3) 결과가 결정적이므로 그대로 둔다. `FTDScopedMeleeWorld` 를 공용 헤더로 함께 승격해 B단계 정합 테스트(§8)가 재사용할지는 결정 항목이다(§15). 1/64 이행은 Phase 2 에서 기대값을 "초"가 아니라 "스텝 수"로 재정의하는 별도 작업이다([07 로드맵](07-roadmap-and-tasks.md)).

---

## 3. 난수 소유권(D29)

### 3.1 스트림 파생

```cpp
struct FTDCombatRandomStreams
{
	void Initialize(int32 InMasterSeed)
	{
		MasterSeed = InMasterSeed;
		Combat.Initialize(Derive(1));
		PlayerProxy.Initialize(Derive(2));
		Spawn.Initialize(Derive(3));
		BrainStreams.Reset();
	}

	FRandomStream& GetBrainStream(int32 SimulationId)
	{
		while (BrainStreams.Num() <= SimulationId)
		{
			BrainStreams.Emplace(Derive(1000 + BrainStreams.Num()));
		}
		return BrainStreams[SimulationId];
	}

	FRandomStream Combat;
	FRandomStream PlayerProxy;
	FRandomStream Spawn;
	TArray<FRandomStream> BrainStreams;

private:
	int32 Derive(int32 Salt) const
	{
		return static_cast<int32>(HashCombine(static_cast<uint32>(MasterSeed), static_cast<uint32>(Salt)));
	}

	int32 MasterSeed = 0;
};
```

| 스트림 | 소유자 | 소비처 | 시드 |
|---|---|---|---|
| `Combat` | `UTDDamageSubsystem` | 치명타 판정, 산포 위치 — `FTDDamageContext` 의 `FRandomStream*` 로 전달 | `Hash(Master, 1)` |
| `PlayerProxy` | `UTDDamageSubsystem` | `PersonaDodgeRoll` 입력(회피 판정, 사고당 1 draw)만 소비. 반응 지연은 고정 스텝 지연, 물약 임계는 곡선 `c` 치환이라 결정적 값이다([02 §6](02-architecture-and-definition-format.md)) | `Hash(Master, 2)` |
| `Spawn` | `UTDDamageSubsystem`(시나리오 로더가 빌려 씀) | 배치 산포(`jitter` > 0 일 때만) | `Hash(Master, 3)` |
| `Brain[SimulationId]` | `UTDDamageSubsystem`(두뇌 서브시스템이 `GetBrainStream(SimulationId)` 로 빌려 씀) | 종 단위 "상위 N개 가중 무작위 선택"(D2) 만 소비. 기본 최고점 선택은 난수를 쓰지 않는다 | `Hash(Master, 1000 + SimulationId)` |

`FTDCombatRandomStreams` 전체의 소유자는 `UTDDamageSubsystem` 하나다. 다른 서브시스템·로더는 참조로 빌려 쓰며 자체 스트림을 만들지 않는다(해시 코드 §7.1 의 `Damage->GetRandomStreams()` 와 일치).

`SimulationId` 가 스폰 순번이며 재사용되지 않으므로(D14) 배열 인덱스 = SimulationId 로 두고 `TMap` 순회를 피한다. 게임은 시작 시 시간 기반 시드, 시뮬은 시나리오 시드로 `Initialize` 한다. 호출은 항상 지역 변수로 순차화한다(`F(Stream.FRand(), Stream.FRand())` 처럼 인자 평가 순서에 맡기지 않는다). 사고 1회당 소비 횟수(`draws`)를 결정 로그에 남기고(§11), 스트림 현재 시드(`GetCurrentSeed`)를 상태 해시에 넣는다(§7, OpenTTD 방식 — web-balance-simulation-tools 결론 6).

### 3.2 기존 코드 교체 두 곳(D38)

| 위치 | 현재 | 변경 |
|---|---|---|
| `Source/TDGame/Combat/TDCombatComponent.cpp:220` | `FMath::FRand() < Chance` (치명타) | `Context.RandomStream->FRand() < Chance` |
| `Source/TDGame/Combat/Damage/TDDamageSubsystem.cpp:203-204` | `FMath::FRand()` 2회 (산포 각도·거리) | `const float Angle = Stream.FRand() * UE_TWO_PI; const float Distance = FMath::Sqrt(Stream.FRand()) * ...` |

두 곳이 현재 전투 코드에서 재현성을 깨는 유일한 실질 원인이다(project-current-combat-code 결론 1). `FMath::FRand` 는 C 런타임 `rand()` 전역 스트림이라 인스턴스별 시드가 불가능하다(engine-determinism-headless 결론 4, `GenericPlatformMath.h:603-620`).

### 3.3 NoGlobalRandom 테스트

자동화 테스트 `TDGame.MonsterAI.NoGlobalRandom` 이 `Source/TDGame/Combat`, `Source/TDGame/MonsterAI`, `Source/TDGame/CombatSim` 의 `.cpp/.h` 를 읽어 정규식 `FMath::(S?F?Rand\w*|VRand\w*)\b` 가 나오면 파일:줄과 함께 실패시킨다. 접두 일치로 잡는 이유는 이름을 나열하면 `FRandRange`(엔진에서 가장 흔한 전역 난수 호출 — engine-misc-decision-tools R2 Chooser, [03](03-tick-and-scale.md) CMC 투영 타이머), `Rand32`, `RandBool`, `RandPointInCircle`, `RandHelper64`(`UnrealMathUtility.h:281-348`) 같은 변형을 놓치기 때문이다. D29 의 목록(`FRand/Rand/RandRange/SRand`)은 이 정규식의 부분집합이다. 빌드 도구 의존이 없고 에디터 테스트 러너에서 1초 안에 끝난다. 예외가 필요하면 같은 줄에 `TD_ALLOW_GLOBAL_RANDOM` 표식을 두되 리뷰에서 근거를 요구한다.

### 3.4 엔진 쪽 난수 함정 요약

| 시스템 | 함정 | 대응 |
|---|---|---|
| GAS `UChanceToApplyGameplayEffectComponent` | `CanGameplayEffectApply` 가 `FMath::FRand()` 를 쓴다(GAS 내부 난수는 이 한 곳) | 확률 판정을 GE 컴포넌트에 맡기지 않고 어빌리티·피해 코드에서 `Combat` 스트림으로 먼저 굴린다 — engine-gas-determinism 결론 7(`ChanceToApplyGameplayEffectComponent.cpp:23-35`) |
| StateTree | 시드를 안 주면 `FPlatformTime::Cycles()` 로 자체 스트림을 만든다(`StateTreeExecutionContext.cpp:1513`). 일부 조건·태스크의 전역 난수 사용 여부는 두 조사가 엇갈린다 | 전투 코어에서 배제(D4)라 영향 없음 — engine-statetree-runtime 결론 3, engine-determinism-headless 결론 4 |
| Chooser Randomize 컬럼 | 컨텍스트에 `FRandomStream*` 이 없으면 전역 `FMath::FRandRange` | 미사용 — engine-misc-decision-tools R2 |
| Mass | 엔티티 압축이 벽시계 예산으로 순서를 바꾸고, MassAI 조향·시선 태스크가 `FMath::RandRange` 를 직접 호출해 `-FixedSeed` 를 무시 | 전투 코어에서 배제(D4). 단계 D 스파이크 때만 `mass.EntityCompaction 0`·`mass.FullyParallel 0` — engine-mass-entity-ai 결론 5·6 |
| BehaviorTree 대기·서비스 | 전역 `FMath::FRand` | 배제(D4) — engine-determinism-headless 결론 4 |

---

## 4. 순서 결정성(D30)

| # | 지점 | 현재 | 변경 | 근거 |
|---|---|---|---|---|
| 1 | 전투원 등록·대상 수집 | `GatherTargets` 가 `TSet<TWeakObjectPtr>` 등록 순서대로 결과를 채우고 `HitArea` 가 그 순서로 피해를 준다(`TDDamageSubsystem.cpp:247-270`, `TDDamageEntity.cpp:542-573`) | `UTDCombatComponent::SimulationId` 를 서브시스템이 스폰 순번으로 부여하고 `GatherTargets` 결과를 `SimulationId` 오름차순 정렬 | project-current-combat-code 결론 4 |
| 2 | 투사체·호밍 동률 tie-break | `GetUniqueID()` 전역 UObject 인덱스(`TDDamageEntity.cpp:305-307`, `497-498`) — 프로세스 간·이전 생성 오브젝트 수에 따라 달라짐 | `SimulationId` 비교로 교체 | project-current-combat-code 결론 4 |
| 3 | 스윕 다중 히트 동시간 | 엔진이 `OutHits.Sort(FCompareFHitResultTime())` 로 시간순 정렬하지만 같은 `Time` 사이 순서는 불안정 정렬이라 보장 없음 | 호출 측에서 `(Time, SimulationId)` 안정 정렬 후 처리 | engine-determinism-headless 결론 6(`CollisionConversions.cpp:526-527`) |
| 4 | 오버랩 결과 | `ConvertOverlapResults` 가 가속 구조 순회 순서 그대로, 정렬 코드 없음 | 시뮬 경로는 물리 오버랩을 쓰지 않는다(격자 후보를 `SimulationId` 순). 플레이어 노티파이 스윕 경로가 오버랩을 쓰면 호출 측에서 `SimulationId` 정렬 | engine-determinism-headless 결론 6(`SceneQuery.cpp:1097-1166`) |
| 5 | 타이머 동률 만료 | 같은 만료 시각의 타이머는 힙 삽입 순서에 의존(GE 만료·도트) | 1·6 으로 삽입 순서가 결정되므로 별도 조치 없음. 같은 스텝에 두 몬스터가 같은 대상을 때리면 `SimulationId` 순으로 GE 가 적용된다 | engine-gas-determinism 결론 1 해석 |
| 6 | 스폰 순서 = 틱 순서 | 테스트마다 다름 | 시나리오 로더가 플레이어(`SimulationId 0`) → 몬스터(`spawn.at_step` 오름차순, 같은 스텝이면 `monsters` 배열 순, `count` 순번 순 — §9.1)로 고정. 늦게 스폰되는 웨이브도 이 순서로 `SimulationId` 를 이어 받는다. 같은 틱 그룹 안 틱 순서는 등록 이력에만 의존한다 | engine-determinism-headless 결론 3(`TickTaskManager.cpp:1481-1487`) |
| 7 | 액터 이름 | 전역 카운터로 만들어 실행마다 달라질 수 있음 | 로그·리플레이·해시 키는 `SimulationId` 만. 액터 이름을 어디에도 키로 쓰지 않는다 | engine-determinism-headless 결론 10(e)(`UObjectGlobals.cpp:2703-2715`) |

두뇌·이동·판정 순회 순서는 슬롯 배열 인덱스 = `SimulationId` 라 표에 없는 추가 규약이 필요 없다(D14·D15). `tick.AllowConcurrentTickQueue` 는 절대 켜지 않는다(설명에 "순서를 바꿀 수 있음" 명시 — engine-determinism-headless 결론 3, `TickTaskManager.cpp:54-88`).

미확인: `ETDDamageEntityMode::Projectile` 의 이동 판정이 물리 스윕을 쓰는지 Phase 0 에서 확인하고, 쓰면 3번 규약을 적용한다.

---

## 5. 물리·내비게이션·애니메이션 배제와 예외

### 5.1 배제 이유

| 시스템 | 배제 이유 | 근거 |
|---|---|---|
| Chaos 물리 | 강체 시뮬을 켜면 브로드페이즈 가속 구조의 비동기 재구축 교체 시점이 `IsComplete()` 벽시계 타이밍에 의존한다. 이동·판정을 수학(2D 거리·원/부채꼴 겹침)으로 하면 물리 없이 성립한다 | engine-determinism-headless 결론 5(`PBDRigidsEvolution.cpp:802-845`), web-balance-simulation-tools 결론 7 |
| 내비게이션 | 내비메시 타일은 워커 스레드로 병렬 생성되고 완료 검사 순서에 따라 타일 인덱스·솔트가 실행마다 달라진다. `FindPathAsync` 도 타이밍 의존 | engine-determinism-headless 결론 7(`RecastNavMeshGenerator.cpp:7184-7217`) |
| 스켈레탈 애니메이션 | 헤드리스에서 `bRecentlyRendered` 가 항상 false 라 `OnlyTickPoseWhenRendered` 면 틱이 꺼진다. 몽타주 노티파이는 델타 구간 스캔이라 스텝 크기에 종속. 300마리 애니메이션 재생은 비용상 부적절 | engine-determinism-headless 결론 10(a), §7 표; project-current-combat-code 결론 6 |
| AIController·CMC 이동 | 헤드리스 월드에서 `AAIController::MoveTo` 는 내비 시스템 부재로 실패한다. 시뮬 몸은 컨트롤러 없는 `ATDSimCombatant` | project-current-combat-code 결론 5(`AIController.cpp:888`) |

시뮬 경로가 쓰는 것은 `GatherTargets` 자체 순회, `THierarchicalHashGrid2D`, 수학 이동, `UTDDamageSubsystem::ExecuteRules`, GAS 속성·GE·타이머뿐이다. LOD 는 시뮬 기본이 전원 L0 사고이며(D23), 이동·판정 스텝은 LOD 와 무관하게 진행된다(D21). 그래서 밸런스 결과가 가상 카메라·풀 크기에 종속되지 않는다.

### 5.2 예외

| 예외 시나리오 | 켜는 것 | 추가 규약 | 근거 |
|---|---|---|---|
| B단계 정합 테스트(§8) | 실제 스켈레탈 메시 + 몽타주 재생, 같은 헤드리스 월드 | `VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`(기본값 유지), URO(Update Rate Optimization, 갱신 빈도 최적화) 끔, 1/64 스텝은 노티파이 최소 간격보다 작다. B단계 몸 = `ATDSimCombatant` + 스켈레탈 메시 컴포넌트(이동은 수학, CMC 없음 — D16, [03](03-tick-and-scale.md) 시뮬 몸 규약). CMC 스텝 분할 사실은 게임 쪽 03 문서에만 둔다 | engine-determinism-headless 결론 10(a)·(d), §7 표 |
| 물리 반응이 판정에 필요한 시나리오(넉백 등, 현재 없음) | `bSimulatePhysics = true` | `bEnableEnhancedDeterminism = true`(또는 `p.Chaos.Solver.Deterministic 1`), 바디 생성 순서 = `SimulationId` 순, 비동기 물리·서브스테핑 기본 꺼짐 유지 | engine-determinism-headless 결론 5(`PBDRigidsEvolutionGBF.cpp:1316-1323`) |
| 내비 경로가 필요한 시나리오(플로우 필드로 대체 예정, 현재 없음) | `bCreateNavigation = true` | `SetMaxSimultaneousTileGenerationJobsCount(1)` → `Build()` → `EnsureBuildCompletion()` 동기 빌드, `FindPathSync` 만 사용 | engine-determinism-headless 결론 7(`RecastNavMesh.cpp:549`, `:3619`) |

미확인: 비에디터(패키지) 빌드에서 `ShouldSimulatePhysics(false)` 월드의 쿼리 전용 콜리전이 갱신되는지(engine-determinism-headless 미확인 1). 커맨드렛은 에디터 바이너리로 돌고 시뮬 경로가 물리 쿼리를 쓰지 않으므로 지금은 영향이 없다.

---

## 6. 공격 판정 단일 경로(D19~D21)

### 6.1 단계별 판정 원천

| 단계 | 몬스터 공격 | 플레이어(사람 조작) | 시뮬의 플레이어 대리 |
|---|---|---|---|
| Phase 0~2 | 기존 `UTDDamageDefinition`(`Mode` Area/Shockwave/Projectile, `ActivationDelay` = 선딜, `Lifetime`, `Cooldown` — `TDDamageDefinition.h:19-34`). 스켈레탈 메시 없이 성립 | `UTDAnimNotifyState_MeleeAttack` 소켓 스윕 유지(`TDMeleeAttackNotifyTests` 로 검증됨 — GAS 전환 전 통과 기록이며 Phase 0 재실행으로 재확인, §2.4) | 플레이어 몽타주에서 추출한 시간표(§6.2)로 판정 |
| Phase 3 이후 | 몽타주 주도 근접 몬스터가 생기면 `FTDAttackTimetable` 이 권위. 노티파이는 표현·오라클 전용(`bAuthoritativeHitJudgment` 게이트) | 유지. 시간표 권위로 전환할지는 결정 항목(§15) | 시간표 |

근거: project-current-combat-code 결론 6·10(근접 판정은 메시·애님 인스턴스·몽타주가 있어야만 성립, 예제 정의는 `NewObject` 로 메모리 생성 가능), engine-movement-anim-scale 결론 8(몽타주 시간표 추출 API).

### 6.2 시간표 형식과 추출 커맨드렛

`UTDAttackTimetableExtractCommandlet`(`-run=TDAttackTimetableExtract -Montage=<path>|-All`)이 `GetPlayLength`, `GetSectionStartAndEndTime`, `Notifies[i].GetTriggerTime/GetDuration/NotifyName`, `BlendOut` 을 읽어 `Content/MonsterAI/Timetables/<Id>.json` 으로 저장한다. 소켓 궤적은 노티파이 구간을 1/64 간격으로 샘플링해 궤적 바운딩(부채꼴 반지름·반각·높이)으로 초기화하고, 그 값이 형상 파라미터가 된다(심사 판정: 궤적 손실 완화). 미확인: 소켓 궤적 샘플링은 몽타주 메타데이터만으로는 불가능하고 스켈레탈 메시 로드 + 포즈 평가(`GetBoneTransform`·`GetSocketTransform` 류)가 필요하다. 조사(engine-movement-anim-scale 결론 8)는 `GetSectionStartAndEndTime`·`GetTriggerTime/GetDuration` 만 확인했으므로 사용 API 와 비용은 Phase 3 착수 시 확인한다.

```json
{
  "id": "Goblin_Slash",
  "montage": "/Game/Monsters/Goblin/AM_Slash",
  "source_hash": "3f9e1c7a",
  "play_length_steps": 70,
  "windup_steps": 19,
  "hit_window_steps": [19, 27],
  "recovery_end_steps": 54,
  "shape": { "type": "Arc", "radius": 180, "half_angle_deg": 60, "height": 120 },
  "max_hits_per_target": 1,
  "rules_ref": "DA_TDGoblinSlashRules"
}
```

| 규칙 | 내용 |
|---|---|
| 단위 | 시간은 초가 아니라 스텝 정수(1/64 격자 반올림). 판정은 `hit_window_steps` 안의 매 스텝, 대상당 `max_hits_per_target` 회 |
| `source_hash` | 몽타주의 노티파이 목록(이름·시각·길이)과 `PlayLength` 의 해시. 검증기(`TDMonsterAIValidate`, [02 정의 형식](02-architecture-and-definition-format.md))가 현재 몽타주와 비교해 다르면 재추출을 요구한다 |
| 판정 경로 | 창 안 스텝마다 격자 후보(`SimulationId` 순) → 형상 안 여부(2D 각도·거리 + 높이) → `UTDDamageSubsystem::ExecuteRules(HitRules, ...)`. 기존 규칙 경로를 그대로 탄다 |
| `bAuthoritativeHitJudgment` | `UTDAnimNotifyState_MeleeAttack` 의 프로퍼티. 몬스터 노티파이는 false(표현·오라클), 플레이어 노티파이는 true 유지. false 면 스윕은 돌되 `ExecuteRules` 를 호출하지 않고 오라클 이벤트만 발행 |
| 화면 밖 | 시간표 진행·판정은 LOD 와 무관하게 매 스텝(D21). 화면 밖에서 애니메이션이 멈춰도 판정은 멈추지 않는다 |

플레이어 대리의 시간표는 플레이어 몽타주에서 같은 커맨드렛으로 뽑는다. 게임의 플레이어 노티파이 스윕과 시뮬 대리의 시간표 판정이 같은 대상을 같은 창에서 맞히는지는 B단계 정합 테스트(§8)가 종별로 고정한다.

---

## 7. 상태 해시·리플레이 검증(D31)

### 7.1 해시 대상과 체인

| 대상 | 무엇을 | 순서 |
|---|---|---|
| 전투원(`UTDCombatComponent`) | `SimulationId`, 속성 9개(`Health, MaxHealth, Level, AttackPower, SpellPower, Armor, MagicResistance, CriticalChance, CriticalMultiplier`) float 비트, 액터 위치 벡터 비트, 활성 GE 수, 상태이상 서명 | `SimulationId` 오름차순 |
| 두뇌 슬롯(SoA) | FSM 상태, 현재 행동 인덱스, 공격 시간표 진행 스텝, 속도 벡터 비트, 페이즈 | 슬롯 인덱스 |
| 데미지 엔티티 | 위치 비트, `SimulationTime`, 남은 수명, 대상 적중 기록 수 | 엔티티 `SimulationId` |
| 난수 | `Combat`·`PlayerProxy`·`Spawn`·`Brain[i]` 의 `GetCurrentSeed()` | 고정 순서 |

```cpp
uint64 FTDCombatStateHasher::HashStep(const FTDScopedCombatWorld& Sim) const
{
	uint64 Hash = FnvBasis64;
	const UTDDamageSubsystem* Damage = Sim.World->GetSubsystem<UTDDamageSubsystem>();
	const UTDMonsterThinkSubsystem* Think = Sim.World->GetSubsystem<UTDMonsterThinkSubsystem>();
	for (const UTDCombatComponent* Combatant : Damage->GetCombatantsSortedBySimulationId())
	{
		Hash = Mix(Hash, Combatant->GetSimulationId());
		Hash = MixFloatBits(Hash, Combatant->GetAttributeValues());
		Hash = MixVectorBits(Hash, Combatant->GetOwner()->GetActorLocation());
		Hash = Mix(Hash, Combatant->GetActiveEffectCount());
		Hash = Mix(Hash, Combatant->GetStatusEffectSignature()); // 활성 GE 태그 집합의 정렬 해시
	}
	for (int32 Slot = 0; Slot < Think->GetSlotCount(); ++Slot)
	{
		Hash = Mix(Hash, Think->GetFsmState(Slot));
		Hash = Mix(Hash, Think->GetCurrentAction(Slot));
		Hash = Mix(Hash, Think->GetAttackStep(Slot));
		Hash = MixVectorBits(Hash, Think->GetVelocity(Slot));
		Hash = Mix(Hash, Think->GetPhase(Slot));
	}
	for (const ATDDamageEntity* Entity : Damage->GetEntitiesSortedBySimulationId())
	{
		Hash = MixVectorBits(Hash, Entity->GetActorLocation());
		Hash = MixFloatBits(Hash, Entity->GetSimulationTime());
		Hash = MixFloatBits(Hash, Entity->GetRemainingLifetime());
		Hash = Mix(Hash, Entity->GetHitRecordCount());
	}
	const FTDCombatRandomStreams& Streams = Damage->GetRandomStreams();
	Hash = Mix(Hash, Streams.Combat.GetCurrentSeed());
	Hash = Mix(Hash, Streams.PlayerProxy.GetCurrentSeed());
	Hash = Mix(Hash, Streams.Spawn.GetCurrentSeed());
	for (const FRandomStream& Brain : Streams.BrainStreams)
	{
		Hash = Mix(Hash, Brain.GetCurrentSeed());
	}
	return Hash;
}
```

- 체인: `Chain[s] = Mix(Chain[s-1], HashStep(s))`. 게이트 모드는 매 스텝, 배치 모드는 64스텝마다와 종료 시.
- 계층: 전투원별·서브시스템별(전투/두뇌/엔티티/난수) 부분 해시를 결정 로그의 `h` 필드 옆에 남겨 최초 이탈 스텝뿐 아니라 최초 이탈 개체까지 드릴다운한다(web-balance-simulation-tools 결론 6, Supreme Commander·Bugnet 사례).
- 값은 정수화하지 않고 비트 그대로 섞는다. 반올림은 "다른 값을 같은 해시로" 만들어 이탈을 늦게 잡는다.

### 7.2 게이트 = 자동화 테스트 3종 + CI 단계 1

D31 의 "같은 프로세스 2회 + 다른 프로세스 1회 + 골든 해시" 중 다른 프로세스 비교만 에디터 자동화 테스트가 아니라 CI 스크립트 단계다. 자동화 테스트 하나가 자식 프로세스 2개를 띄우지 않는다.

| 게이트 | 종류 | 내용 | 통과 기준 |
|---|---|---|---|
| `TDGame.CombatSim.Determinism.SameProcess` | 자동화 테스트 | 게이트 시나리오를 같은 프로세스에서 새 월드로 2회 | 체인 전 구간 동일 |
| `TDGame.CombatSim.Determinism.Golden` | 자동화 테스트 | `Content/CombatSim/Golden/<scenario>.hash` 와 비교 | 동일, 또는 §7.3 절차로 갱신된 커밋 |
| `TDGame.CombatSim.Determinism.OneStepPerFrame` | 자동화 테스트 | `RunKernelStep` 호출 횟수 == `GFrameCounter` 증가량(§2.3) | 동일(이중 스텝 검출, D15) |
| `run_batch.py --gate`(CI 단계, §12) | CI 스크립트 | 같은 시드를 커맨드렛 프로세스 2개에 주고 `hash_chain.txt` 를 비교 | 동일 |

불일치 시 `diff_runs.py a.jsonl b.jsonl` 이 최초 이탈 스텝과 그 스텝의 두 로그 줄을 나란히 출력한다. 리플레이 파일은 따로 없다. 입력(시나리오 JSON + 시드 + 정의 스냅샷 + 빌드 해시)만 저장하고 재실행해 이벤트 JSONL 을 diff 한다.

### 7.3 골든 해시 갱신 절차

1. 게이트 실패 시 `diff_runs.py` 로 최초 이탈 스텝·개체를 확인하고, 이탈이 "의도한 변경(공식·정의·판정 규칙)" 인지 "규약 위반(순서·난수·미초기화)" 인지 판정한다.
2. 의도한 변경이면 `-run=TDCombatSim -Scenario=Content/CombatSim/Scenarios/<gate>.json -WriteGolden` 으로 재생성한다(게이트 시나리오도 일반 시나리오와 같은 `Content/CombatSim/Scenarios/` 폴더, 골든은 `Content/CombatSim/Golden/<scenario>.hash` — [07 M1-11](07-roadmap-and-tasks.md)). 골든 파일에는 체인 최종값, 64스텝 체크포인트 배열, 정의 해시(`dh`), 빌드 해시, 엔진 버전, CPU 명령셋 클래스를 함께 기록한다.
3. 골든 갱신은 원인이 된 코드·정의 변경과 같은 커밋에 넣고, 커밋 메시지에 최초 이탈 스텝과 이벤트 diff 요약을 적는다.
4. 엔진 업그레이드·컴파일러·컴파일 옵션 변경 시 전체 골든을 재생성하고 C단계 통계 등가(§8)로 밸런스 결과가 유지되는지 확인한다.

부동소수점 주의(web-balance-simulation-tools 결론 2·6): 결정론의 정의는 "같은 빌드·같은 CPU 명령 집합·같은 컴파일 옵션·단일 스레드 순서" 다. `/fp:fast`, FMA(Fused Multiply-Add, 곱셈-덧셈 융합 명령) 사용 여부, x87 초월함수 구현 차이가 결과를 바꾼다. CI 머신을 고정하고 골든에 CPU 명령셋 클래스를 적는 이유다. UnrealBuildTool 의 기본 부동소수점 모드는 미확인이며, 값 자체보다 "옵션이 바뀌면 골든을 재생성한다" 는 규칙이 중요하다. 다른 머신 사이는 비트 동일이 아니라 통계 등가로만 비교한다.

---

## 8. 시뮬-실기 일치 3단 정의(D32)

| 단계 | 구성 | 기준 | 테스트 | 약속 범위 |
|---|---|---|---|---|
| A 비트 동일 | 시뮬 vs 시뮬(같은 프로세스·다른 프로세스) | 해시 체인 전 구간 동일 | `TDGame.CombatSim.Determinism.*` | 항상 |
| B 이벤트 등가 | 같은 헤드리스 월드에서 "실제 메시 + 몽타주 재생 + 노티파이(오라클 모드)" vs "시간표 판정", 둘 다 1/64 스텝 | 피해 이벤트 집합 동일, 근접 적중 시각 ±1 스텝, 총 피해 동일 | `TDGame.CombatSim.Parity.<Id>`(예 `Parity.Goblin_Melee`; 기존 `TDMeleeAttackNotifyTests` 확장), 시간표를 가진 종마다 1개 — [07 M3-06](07-roadmap-and-tasks.md) 과 같은 표기 | Phase 3 부터, 시간표를 가진 종마다 |
| C 통계 등가 | 시뮬 승률 vs 실제 플레이테스트(사람) 승률 | 산포도 y=x 회귀, Bob's Buddy 방식 | 수동 리포트 | 실 플레이 데이터가 쌓인 뒤 |

"게임 = 시뮬 비트 동일"은 약속하지 않는다. 게임은 가변 프레임이고 플레이어 입력·렌더 종속 표현이 섞이며, 플레이어 근접은 노티파이 권위를 유지하기 때문이다(D20). B단계가 시뮬이 게임을 대표한다는 핵심 근거이고, C단계가 밸런스 툴의 채택 기준이다(web-balance-simulation-tools 결론 1, Bob's Buddy 2020).

---

## 9. 입력 스키마(D33)

### 9.1 시나리오 JSON

`FTDCombatSimScenario` USTRUCT 와 `FJsonObjectConverter` 로 왕복한다(engine-misc-decision-tools R13: `TMap` 키는 문자열·`FName`·열거형·`ExportTextItem` 을 가진 구조체(`FGameplayTag` 포함)만, UObject 참조 대신 문자열 ID, `SkipStandardizeCase` 로 C++ 필드명과 1:1).

키 이름 규칙(결정): 시나리오·정의 USTRUCT 의 프로퍼티는 JSON 키와 같은 snake_case 로 선언한다(예 `int32 max_steps`). `SkipStandardizeCase` 는 첫 글자 소문자화만 끄고 별도 매핑이 없으며(`JsonObjectConverter.h:33·63`), JSON → 구조체 변환은 프로퍼티의 authored name 으로 키를 찾으므로(`JsonObjectConverter.cpp:1338-1339`) `MaxSteps` 프로퍼티는 `max_steps` 키를 받지 못한다. 엔진 명명 규칙 예외를 이 데이터 구조체에만 허용하고 커스텀 변환기는 만들지 않는다. [02 §5.1](02-architecture-and-definition-format.md) 의 "JSON 키를 USTRUCT 프로퍼티 이름과 직접 대조" 도 같은 전제이며, 02 §5.1 규칙 18 이 같은 문장이다.

```json
{
  "schema": 1,
  "id": "T3_sword_vs_goblin_x8",
  "max_steps": 7680,
  "seeds": { "base": 1000, "count": 400 },
  "arena": { "radius": 1500, "obstacles": [] },
  "player": {
    "level": 12,
    "equipment_effects": [
      { "effect": "TDEquipmentStatEffect", "set_by_caller": { "Data.Equip.AttackPower": 12, "Data.Equip.Armor": 8 } }
    ],
    "potions": { "HP": { "count": 2, "heal": 300, "cooldown_steps": 512, "use_below_ratio": 0.35 } },
    "buffs": [ { "effect": "TDHasteBuffEffect", "duration_steps": 1280 } ],
    "spells": [ "DA_TDFireball", "DA_TDBlizzard" ],
    "persona": { "definition": "Persona_Default", "override": { "reaction_delay_seconds": 0.3 } }
  },
  "monsters": [
    { "definition": "Goblin_Melee", "count": 8, "spawn": { "ring_radius": 900, "start_angle_deg": 0, "jitter": 0 } },
    { "definition": "Goblin_Melee", "count": 4, "spawn": { "at_step": 1920, "ring_radius": 1200, "start_angle_deg": 45, "jitter": 0 } },
    { "definition": "Ogre_Boss", "count": 1, "spawn": { "at": [0, 1200] }, "phase_override": { "start_phase": "Enraged" } }
  ],
  "end": { "stalemate_steps": 640 },
  "virtual_camera": null,
  "record": { "events": true, "decisions": "sampled", "hash_every_steps": 64 }
}
```

| 필드 | 규칙 |
|---|---|
| `max_steps`, `*_steps` | 모든 시간은 스텝 정수. 초를 쓰지 않는다(D27). 예외는 `persona.override` 뿐이다(아래) |
| `seeds` | `base + i` 가 i번째 시뮬의 마스터 시드. 시드 i 의 결과는 어느 프로세스에서 돌아도 같다. 정본은 이 필드이며 커맨드렛 `-SeedStart/-SeedCount`(§1)와 런처 `--seed-base/--count`(§12)는 이 범위의 부분집합만 지정할 수 있다(범위 밖이면 커맨드렛이 거부). 스냅샷(§10.4)에는 실제 실행된 `SeedStart/SeedCount` 를 기록한다 |
| `player.persona` | 플레이어 대리 봇 두뇌는 몬스터와 같은 JSON 형식(D12). `definition` 은 `Content/MonsterAI/Definitions/<Id>.json` 의 `kind: player_proxy` 파일(예 `Persona_Default`, [07 M2-08](07-roadmap-and-tasks.md)). `override` 는 정의 파일의 `persona` 블록과 같은 키·단위(`reaction_delay_seconds` 초 등, 로더가 `round(초 × 64)` 스텝으로 변환 — [02 §6](02-architecture-and-definition-format.md)). 확률 파라미터 중 `dodge_probability` 만 `PlayerProxy` 스트림을 소비한다(§3.1) |
| `monsters[].definition` | `Content/MonsterAI/Definitions/<Id>.json`(D6). `phase_override` 로 보스 페이즈 시작점을 강제해 페이즈 단위 밸런스를 따로 잰다 |
| `monsters[].spawn.at_step` | 스폰 스텝(기본 0). 같은 종을 여러 항목으로 나눠 웨이브를 표현한다. 스폰 순서는 `at_step` 오름차순 → `monsters` 배열 순 → `count` 순번 순이며 `SimulationId` 는 그 순서로 부여된다(§4 표 6번) |
| `end.stalemate_steps` | 교착 판정(기본 640 = 10초). 이 스텝 수 동안 양측 피해 이벤트가 0 이면 `result = stalemate` 로 종료한다. 종료 사유는 `win / lose / timeout / stalemate` 네 가지(§10.1) |
| `virtual_camera` | 기본 `null` = 전원 L0 사고(D23). 성능 검증 시나리오만 `{ "center": [0,0], "half_extent": [1600, 900], "actor_pool": 400 }` 을 명시해 LOD 영향을 재현 가능하게 한다 |
| `record.decisions` | `"all"` / `"sampled"`(SimulationId 0 과 종별 첫 개체) / `"none"`. 300마리 × 10Hz × 120초 = 36만 줄이라 기본은 `sampled` |

### 9.2 장비·물약·버프를 GameplayEffect 로 표현하는 규칙

플레이어 조건은 "시전 시점 스탯 벡터 + GE 세트 + 스킬 목록 + 사용 정책" 으로 닫힌다. 시전 시 `Context.Stats = Combatant->GetStats()` 로 스냅샷을 복사하므로 시전 후 버프가 바뀌어도 날아가는 효과는 그대로다(project-current-combat-code 결론 9·§4).

| 조건 | GE 형태 | 수치 주입 | 근거 |
|---|---|---|---|
| 장비(영구 스탯) | `UTDEquipmentStatEffect` C++ CDO 하나: `Infinite`, Additive 수정자 N개, 크기는 `Data.Equip.<속성>` SetByCaller 태그 | `MakeOutgoingSpec` → `SetSetByCallerMagnitude(Tag, Value)` → `ApplyGameplayEffectSpecToSelf`. 장비 100종을 100개 클래스로 만들지 않는다 | engine-gas-determinism 결론 8·§6 권장 1~2 |
| 물약(즉시) | `Instant` Health 양수 수정자 | `heal` 값 SetByCaller | project-current-combat-code §4 |
| 물약(지속 회복) | `Periodic` Health 수정자만 허용. 주기 없는 지속형 Health 수정자는 `CanApplyEffect` 가 거부한다(`TDCombatComponent.cpp:20-39`) | `Period` 는 생성자, 첫 실행은 다음 타이머 틱 | project-current-combat-code 결론 9 |
| 버프(일시) | `HasDuration` CDO + `Spec.Data->SetDuration(Steps * StepSeconds, true)` 로 지속시간을 코드에서 덮어쓴다(쿨다운 GE 가 이미 이 방식). 지속시간·쿨다운 GE 는 `Steps + 1` 번째 스텝에 만료된다(엄격 초과, §2.2) — `duration_steps: 1280` 은 1281번째, `cooldown_steps: 512` 는 513번째 스텝에 풀리며 골든 테스트로 고정한다 | 비체력 속성만 | engine-gas-determinism §6 권장 3, 결론 3 |
| 확률 효과 | `UChanceToApplyGameplayEffectComponent` 금지. 어빌리티 코드에서 `Combat` 스트림으로 먼저 굴린다 | — | engine-gas-determinism 결론 7 |

주의: C++ 생성자에서 deprecated 멤버(`ChanceToApplyToTarget_DEPRECATED` 등)에 값을 넣으면 컴포넌트로 변환되지 않아 조용히 무시된다. 컴포넌트를 직접 추가한다(engine-gas-determinism 결론 8, `GameplayEffect.cpp:484-500`). 장비가 "속성 이외의 규칙"(적중 시 추가 투사체 등)을 가지면 현재 모델로 표현할 수 없고 `FTDDamageRule` 추가가 필요하다(§15).

### 9.3 사전 필터: 기대 DPS/EHP 계산기

시뮬 전에 명백한 불균형을 거르는 계산기형 단계(Path of Building 방식 — web-balance-simulation-tools 결론 1). `FTDCombatPrefilter::Estimate` 가 `TDDamageFormula::Compute`(D18, GAS 경로와 같은 순수 공식)로 플레이어 기대 DPS(초당 피해량 = Σ 스킬 피해 × 치명타 기대값 / 쿨다운)와 몬스터 조합 EHP(Effective Hit Pool, 유효 피격 허용량 = Σ 체력 / 피해 감소 계수), 반대 방향(몬스터 DPS / 플레이어 EHP)을 구해 `ttk_player`, `ttk_monsters` 비율을 낸다. 비율이 `[0.2, 5.0]` 밖이면 배치에서 제외하고 사전 필터 결과 CSV 에만 남긴다. 커맨드렛 옵션 `-PrefilterOnly` 로 시뮬 없이 계산만 출력한다. 이 값은 지표가 아니라 필터이며, 실제 승률은 항상 시뮬로 잰다.

---

## 10. 출력 스키마·지표·통계(D33)

### 10.1 결과 행(CSV, 1시뮬 1행)

`scenario_id, seed, build_hash, dh, result(win|lose|timeout|stalemate), end_step, dmg_dealt, dmg_taken, max_single_hit_taken, min_health_ratio, potions_used, casts_by_ability(json), action_share(json), switch_rate_per_sec, first_attack_step, monsters_killed, rng_draws(json), hash_final`

### 10.2 지표 정의

| 지표 | 정의 | 용도 |
|---|---|---|
| 승률 ± 표준오차 | `p ± sqrt(p(1-p)/n)`, Wilson 95% 구간 병기 | 밴드 위반 판정 |
| 생존·전투 시간 | `end_step` 의 p10/p50/p90 | 지루함·급사 감지 |
| 피해 | `dmg_dealt`, `dmg_taken` 분포, 스킬별 비중 | 스킬 편중 |
| 리썰 위험 | `min_health_ratio` = 전투 중 최저 체력 / 최대 체력(주 지표), `max_single_hit_taken / MaxHealth`(보조) | 원샷 위험 |
| 물약 소비 | `potions_used` 평균·최댓값 | 소모 경제 |
| 행동 점유율 | 사고 수 대비 행동별 선택 비율 | 대기·도주 과다 |
| 교체율 | 초당 행동 변경 횟수(관성 검증, 검증기 3단 기준 < 5회/초) | 떨림 |
| 첫 공격까지 | `first_attack_step`(검증기 기준 < 3초 = 192스텝) | 두뇌 결함 |

### 10.3 반복 수와 표준오차(p = 0.5 최악 기준, 산술 계산)

| n | 표준오차 | 95% 구간 폭 | 용도 |
|---|---|---|---|
| 100 | 5.0%p | ±9.8%p | 개발 중 스모크, LLM 제작 루프 1회 배치(D37: 100~200 시드, [05](05-ml-and-generative-ai.md) `--seeds 0-199`) |
| 400 | 2.5%p | ±4.9%p | 파라미터 탐색 내부 평가([05](05-ml-and-generative-ai.md) CMA 수렴 후 S=400) |
| 1,000 | 1.6%p | ±3.1%p | 일일 밸런스 리포트 |
| 10,000 | 0.5%p | ±1.0%p | 릴리스 전 확정 |

Riot 의 밴드 폭이 49~54.5%(5.5%p)이므로 밴드 위반 판정은 최소 1,000회, 0.5%p 단위 조정 검증은 10,000회급이 필요하다(web-balance-simulation-tools 결론 10·§C).

### 10.4 정의 스냅샷 저장

결과 디렉터리 `Saved/CombatSim/<run>/` 에 `results.csv`, `events.jsonl`, `decisions.jsonl`, `hash_chain.txt` 와 함께 `snapshot/` 을 둔다. 스냅샷 = 시나리오 JSON 사본 + 사용된 몬스터·페르소나 정의 JSON 사본 + `-print-resolved` 병합 결과(D8) + 해석된 등록표 덤프(`actions.md`, `inputs.md`, D9) + 빌드 해시 + 엔진 버전. 정의 해시 `dh` 는 로드 시 수치를 float 비트(uint32)로 덤프해 계산한다(D11).

---

## 11. 결정 로그와 분석 스크립트(D34)

### 11.1 JSONL 예시(사고 1줄 + 이벤트 2줄)

```json
{"t":612,"sim":7,"def":"Goblin_Melee","dh":"3c9e01aa","ph":0,"lod":0,"cand":[["Slash",0.71,[1.0,0.95,0.75,1.0]],["Approach",0.12,[0.12]],["Flee",0.0,[0.1,0.0]],["Idle",0.05,[]]],"pick":"Slash","why":"max","hold":0,"draws":0,"h":"9f3a1c02"}
{"t":641,"ev":"damage","src":7,"dst":0,"amount":11.4,"crit":false,"ability":"Slash","h":"77b0e4d9"}
{"t":700,"ev":"potion","sim":0,"kind":"HP","heal":300,"health_ratio_before":0.31,"h":"a1c53f10"}
```

| 필드 | 뜻 |
|---|---|
| `t` | 스텝 |
| `sim` | `SimulationId`(플레이어 대리 = 0) |
| `cand` | 후보별 `[행동, 총점, 고려사항별 점수(비용 등급 순)]`. 총점 = 행동 `weight × Π(고려사항 점수)`(D1, [02 §5.1](02-architecture-and-definition-format.md) 규칙 5). `Flee` 의 두 번째 고려사항이 0 이라 총점 0 — "왜 0 이었는가" 가 한 줄에 있다. `Idle` 은 고려사항이 비어 있어 총점 = `weight` 0.05(상시 양수 대기 행동은 `weight` 로만 표현, 02 §5.1 `Constant` 행) |
| `why` | `max` / `inertia`(관성 유지) / `sequence`(콤보 진행) / `phase`(페이즈 강제) |
| `draws` | 이 사고에서 `Brain[sim]` 스트림 소비 횟수 |
| `h` | 이 스텝의 체인 해시(계층 부분 해시는 `-log=hash-detail` 일 때) |

이벤트 줄은 `OnDamaged/OnDeath/TryCast` 구독과 물약·페이즈 전이에서 찍는다. 비주얼 로거(.bvlog)는 에디터에서 사람이 볼 때만 이중 송출하며, 에디터 프로세스의 타임스탬프가 벽시계이므로 `SetGetTimeStampFunc` 로 스텝 시간을 주입하고 스텝마다 `Flush()` 한다(engine-misc-decision-tools R11). LLM 분석의 정본은 JSONL 이다(web-llm-authorable-tooling 결론 8).

### 11.2 분석 스크립트 4개(`Tools/CombatSim/`)

| 스크립트 | 입력 → 출력 |
|---|---|
| `analyze_decisions.py decisions.jsonl --def Goblin_Melee` | 행동 점유율, 평균 점수 마진(1위-2위), 교체율/초, 대기 비율, 첫 공격 스텝, 고려사항별 "0 을 만든 횟수" 상위 → 마크다운 표 |
| `diff_runs.py a.jsonl b.jsonl` | 해시 최초 이탈 스텝·개체, 그 스텝의 두 로그 줄 나란히 |
| `summarize_batch.py results.csv --target curve.csv` | 승률 ± 표준오차, 분위수, 리썰 위험, 목표 곡선 오차, 밴드 위반 목록 → 30줄 요약(LLM 이 읽는 단위, D37) |
| `propose_tweaks.py summary.md` | LLM 없이 규칙 기반 힌트 5문장("Flee 점유 41% — SelfHealthRatio 곡선 c 를 0.1 낮추기"). LLM 이 읽을 시작점 |

---

## 12. 프로세스 팬아웃 런처(D35)

한 프로세스 안에서 여러 월드를 스레드로 돌리지 않는다. 업계도 프로세스 병렬을 쓴다(For Honor 인스턴스 5개 × 10매치, King 32 CPU — web-balance-simulation-tools 결론 5). 런처 이름은 `Tools/CombatSim/run_batch.py` 가 정본이며 [02](02-architecture-and-definition-format.md)·[05](05-ml-and-generative-ai.md)·[07 M2-05](07-roadmap-and-tasks.md) 도 같은 이름을 쓴다. 옵션도 여기 코드가 정본이다: `scenario`(위치 인자) `--seed-base --count --procs --out --gate`(05 §4.3 의 `--seeds 0..S-1`·`--workers 8` 표기는 `--seed-base 0 --count S`·`--procs 8` 로 읽는다).

```python
import argparse, csv, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

UE_CMD = r"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
PROJECT = r"C:\Project\TDGame\TDGame.uproject"

def run_worker(index, scenario, seed_start, seed_count, out_dir):
    worker_dir = out_dir / f"w{index:02d}"
    worker_dir.mkdir(parents=True, exist_ok=True)
    args = [UE_CMD, PROJECT, "-run=TDCombatSim", f"-Scenario={scenario}",
            f"-SeedStart={seed_start}", f"-SeedCount={seed_count}", f"-Out={worker_dir}",
            "-nullrhi", "-unattended", "-nosplash", "-nosound", "-FixedSeed", "-onethread",
            f"-abslog={worker_dir / 'run.log'}"]
    return subprocess.run(args).returncode

def merge(out_dir):
    rows = []
    for path in sorted(out_dir.glob("w*/results.csv")):
        with path.open(newline="", encoding="utf-8") as f:
            rows.extend(csv.DictReader(f))
    rows.sort(key=lambda r: int(r["seed"]))
    with (out_dir / "results.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("scenario"); p.add_argument("--seed-base", type=int, default=0)
    p.add_argument("--count", type=int, default=400); p.add_argument("--procs", type=int, default=4)
    p.add_argument("--out", default="Saved/CombatSim/run")
    p.add_argument("--gate", action="store_true")  # 같은 시드를 워커 2개에 주고 hash_chain.txt 비교(§7.2)
    a = p.parse_args()
    out_dir, per = Path(a.out), -(-a.count // a.procs)
    jobs = [(i, a.scenario, a.seed_base + i * per, min(per, a.count - i * per), out_dir)
            for i in range(a.procs) if a.count - i * per > 0]
    if a.gate:  # 같은 시드 범위를 워커 0·1 에 그대로 준다
        jobs = [(0, a.scenario, a.seed_base, a.count, out_dir), (1, a.scenario, a.seed_base, a.count, out_dir)]
    with ThreadPoolExecutor(max_workers=a.procs) as pool:
        codes = list(pool.map(lambda j: run_worker(*j), jobs))
    if any(codes):
        sys.exit(1)
    if a.gate:
        chains = [(out_dir / f"w{i:02d}" / "hash_chain.txt").read_bytes() for i in (0, 1)]
        sys.exit(0 if chains[0] == chains[1] else 2)  # 2 = 프로세스 간 해시 불일치
    merge(out_dir)

if __name__ == "__main__":
    main()
```

| 규칙 | 내용 |
|---|---|
| 시드 분할 | 워커 i 는 `base + i·per` 부터 `per` 개. 시드별 결과가 독립이라 병합 순서와 무관 |
| 프로세스 수 | 물리 코어의 절반부터 시작해 실측으로 올린다. 에디터 바이너리 하나가 1~2GB 메모리를 쓸 수 있다(추정, Phase 2 실측) |
| 로그 분리 | `-abslog` 로 워커별 로그. `Saved/`·DDC 공유 잠금 충돌은 미확인(engine-determinism-headless 미확인 6) — 첫 실행에서 충돌이 나면 워커별 `-Saved=` 분리를 시험한다 |
| 게이트 모드 | `--gate` 는 같은 시드를 워커 2개에 주고 `hash_chain.txt` 를 비교한다(§7.2 의 CI 단계 — 자동화 테스트가 아니라 CI 스크립트가 커맨드렛 프로세스 2개를 띄운다) |
| 결과 병합 | `results.csv` 병합 후 `summarize_batch.py` 를 자동 호출 |

---

## 13. 산업 사례 대조

| 사례 | 부류 | 이 설계에서의 대응물 | 근거(web-balance-simulation-tools) |
|---|---|---|---|
| Path of Building(패스 오브 엑자일 빌드 계산기, pathofbuilding.net 2026) | 계산기형: 장비·패시브·조건 토글 → DPS·EHP 즉시 수식 계산 | §9.3 사전 필터 `FTDCombatPrefilter`. 시뮬을 대체하지 않고 앞단에서 명백한 불균형을 거른다 | 결론 1 |
| twanvl 하스스톤 전장 시뮬레이터(GitHub), Bob's Buddy(HSReplay 2020) | 몬테카를로형: 텍스트 보드 정의 → 1,000~10,000회 반복 → 승/무/패 %, 평균·중앙값, 백분위 | §9 텍스트 시나리오 → §12 배치 → §10 승률·분위수. 반복 수 1,000(일일)/10,000(확정)도 같은 밴드 | 결론 1·10 |
| Riot 챔피언 밸런스 프레임워크(2019, 2020 갱신) | 티어별 승률 밴드 49~54.5% 를 목표 지표로 | `summarize_batch.py --target curve.csv` 의 "장비 티어 × 몬스터 티어 → 목표 승률 곡선" 과 밴드 위반 목록. 밴드 폭 5.5%p 판정에 n ≥ 1,000 | 결론 8·§C |
| Blizzard 하스스톤 아레나(2018) | 예측 승률 50% 목표, 가중치 변경 ±30% 제약 최적화 | `tune:true` 잎 CMA/PSO 튜닝의 목적 함수 = 곡선 제곱 오차 + 변경폭 벌점([05 머신러닝](05-ml-and-generative-ai.md)) | 결론 8 |
| King 캔디 크러시 봇(Gudmundsson, IEEE CIG 2018) | 인간 데이터 학습 봇 → 이항 회귀로 인간 성공률 사상. "최강 봇" 보다 "사람처럼 실수하는 봇" 이 예측력이 높음 | 규칙 페르소나(반응 지연·물약 임계·회피 확률) → BC 봇([05](05-ml-and-generative-ai.md)). C단계 통계 등가가 King 의 회귀에 해당 | 결론 9 |
| Supreme Commander(2011), OpenTTD desync.md | 틱별 상태 해시, 난수 상태 체크섬, 리플레이 = 입력 재실행 | §7 해시 체인에 스트림 현재 시드 포함, 입력만 저장 | 결론 6 |

---

## 14. 성능 목표(추정, Phase 1 실측)

모든 수치는 추정이며 Phase 1 의 "고블린 10마리 vs 규칙 봇" 시나리오와 300마리 시나리오에서 실측해 이 표를 갱신한다([07 로드맵](07-roadmap-and-tasks.md)). 벽시계는 지표에만 쓰고 로직에는 쓰지 않는다.

| 규모 | 스텝당 비용(추정) | 2분 전투(7,680스텝) | 실시간 대비 배속(스텝당 비용에서 산술 환산) | 1,000시드 × 8프로세스(프로세스당 125회) |
|---|---|---|---|---|
| 전투원 10 | 0.2~0.5ms | 1.5~4초 | 31~78배(목표 ≥ 30배) | 3~8분 |
| 전투원 100(전원 L0) | 1~3ms | 8~23초 | 5~16배(목표 ≥ 5배) | 17~48분 |
| 전투원 300(전원 L0) | 3~8ms | 23~61초 | 2~5배(목표 ≥ 2배) | 약 1~2시간(추정) |

배속은 스텝 실시간 15.625ms(1/64초) ÷ 스텝당 비용이다. 목표는 "스텝당 비용" 열이며 배속 열은 그 환산값이다. 배속을 더 올리려면 스텝당 비용을 내려야 하고(예 10명 ≥ 500배는 0.03ms/스텝), 그 수단은 아래 실측 항목 뒤의 순서를 따른다.

근거 없는 추정을 피하기 위한 실측 항목: (1) 빈 월드 `World->Tick` 기본 비용(엔진 고정 비용, 미확인), (2) 전투원당 사고·이동·판정 비용(`stat TDMonsterAI`, D24), (3) ASC 활성 GE·타이머 수에 따른 `FTimerManager` 비용(engine-gas-determinism 결론 9: 300마리 × 지속 버프 3개 = 타이머 900~1,800개), (4) 프로세스당 메모리. 목표에 못 미치면 순서대로 결정 로그 `sampled` 기본화 → 해시 64스텝 간격 → 무액터 L3 를 시뮬에도 적용([03](03-tick-and-scale.md))을 검토한다. 강화학습 롤아웃(수백만 스텝)에는 이 처리량이 부족하다는 점을 [05](05-ml-and-generative-ai.md) 가 전제로 삼는다.

---

## 15. 미결 사항(사용자 결정 필요)

1. Phase 3 이후 사람이 조작하는 플레이어 근접 판정을 시간표 권위로 전환할지 → [07 MD-01](07-roadmap-and-tasks.md).
2. 골든 해시의 CI 머신·CPU 명령셋 고정 → [05 미결 6](05-ml-and-generative-ai.md) 과 하나로 합쳐 07 에 MD-09(CI 명령셋 고정)로 올리도록 요청한다(07 수정 필요).
3. 장비의 "속성 이외 규칙"(적중 시 추가 투사체 등)을 `FTDDamageRule` 확장으로 받을지, Phase 2 범위에서 제외할지.
4. 결정 로그 기본값 `sampled` 의 표본 규칙(플레이어 대리 + 종별 첫 개체)으로 충분한지, 시드별 전체 로그를 몇 개 남길지.
5. 배치 결과 보관 정책(`Saved/CombatSim/` 크기 상한, 스냅샷 보존 기간) → [07 MD-05](07-roadmap-and-tasks.md)(= 06 미결 8).
6. 사전 필터 제외 구간 `[0.2, 5.0]` 의 값.
7. 난이도 축: 난이도 등급(스탯 배율·행동 풀·AI 파라미터 오버라이드)을 시나리오 파라미터로 넣어 시뮬 매트릭스의 축으로 삼을지(zz-completeness-critique M5). 현재 스키마(§9.1)에는 없다.
8. `FTDScopedMeleeWorld`(`TDMeleeAttackNotifyTests.cpp:25`)를 `FTDScopedCombatWorld` 와 함께 공용 헤더로 승격해 B단계 정합 테스트가 재사용할지, 정합 테스트를 `FTDScopedCombatWorld` + 스켈레탈 메시 옵션으로 통합할지(§2.4).

## 근거 색인(인용한 조사 파일 목록)

| 조사 파일 | 인용 결론 |
|---|---|
| `research/engine-determinism-headless.md` | 결론 1·3·4·5·6·7·8·9·10, §6 표, §7 표, 미확인 1·6·7 |
| `research/engine-gas-determinism.md` | 결론 1·2·3·7·8·9·10, §1 해석, §6 권장 1~3 |
| `research/project-current-combat-code.md` | 결론 1·3·4·5·6·9·10, §4 표 |
| `research/web-balance-simulation-tools.md` | 결론 1·2·4·5·6·7·8·9·10, §C 표 |
| `research/engine-misc-decision-tools.md` | R2, R11, R13 |
| `research/engine-statetree-runtime.md` | 결론 3 |
| `research/engine-mass-entity-ai.md` | 결론 5·6 |
| `research/engine-movement-anim-scale.md` | 결론 8(소켓 궤적 샘플링 API 는 미확인) |
| `research/web-llm-authorable-tooling.md` | 결론 8 |
| `research/zz-completeness-critique.md` | M5(난이도 축), M9(스폰 시각·웨이브), M12(교착 판정), C2(GE 만료 스텝) |
| 형제 문서 | [02 §5.1·§6](02-architecture-and-definition-format.md)(점수 결합·페르소나 키·JSON 키 대조), [03](03-tick-and-scale.md)(`AccumulateAndStep`, 시뮬 몸 규약), [05](05-ml-and-generative-ai.md)(시드 규모, 미결 6), [06 §3 C2](06-beyond-the-ask.md), [07](07-roadmap-and-tasks.md)(M1-11·M2-05·M2-08·M3-06, MD-01·MD-05) |
| 엔진 소스(읽기 전용 재확인) | `Engine/Private/TimerManager.cpp:1212`(엄격 초과), `Engine/Private/LevelTick.cpp:1970`·`Engine/Private/UnrealEngine.cpp:1777-1780·2265`(자동 GC 누적), `Launch/Private/LaunchEngineLoop.cpp:4110`(`Commandlet->Main`), `JsonUtilities/Public/JsonObjectConverter.h:33·63`·`Private/JsonObjectConverter.cpp:1338-1339`(키 매칭), `Core/Public/Math/UnrealMathUtility.h:281-348`(전역 난수 변형) |
| 프로젝트 코드 | `Source/TDGame/Combat/Tests/TDDamageSystemTests.cpp:29-89`, `TDMeleeAttackNotifyTests.cpp:25·86·171·180`, `TDCombatComponent.cpp:220`, `TDDamageSubsystem.cpp:203-204·247-270`, `TDDamageEntity.cpp:305-307·497-498`, `TDDamageDefinition.h:19-34`, `TDDamageExamplesCommandlet.h:7-14` |
| 결정 기록 | D1·D2·D6·D8·D9·D11·D12·D13·D14·D15·D16·D17·D18·D19·D20·D21·D22·D23·D24·D26·D27·D28·D29·D30·D31·D32·D33·D34·D35·D37·D38 |

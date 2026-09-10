[← 인덱스로](../MonsterAI_CombatSim_Plan.md)

# 03. 틱 제어와 대량 몬스터 규모 전략

## 이 문서가 답하는 질문

1. "어떤 AI 를 몇 마리에 어떤 주기로 돌리는가" — 채널 4개 × LOD(Level of Detail, 세부 수준) 4단의 주기표와 그 기본값은 무엇인가.
2. 한 프레임에 누가 사고(think)하는지를 난수 없이 어떻게 결정하고, 프레임이 길어질 때 스텝 폭주를 어떻게 막는가.
3. 몬스터 액터 한 마리의 비용은 무엇으로 구성되고, 무엇을 떼어내야 300마리 이상이 가능한가.
4. 몸(Body) 3구현과 이동·근접 탐색은 어떻게 하며, 규모 단계 A/B/C/D 는 어떤 실측 KPI(Key Performance Indicator, 핵심 성과 지표)로 넘어가는가.
5. Mass 를 지금 쓰지 않는 이유와, 나중에 이관할 수 있게 SoA(Structure of Arrays, 배열 구조체)를 어떤 모양으로 두는가.

## 결론 요약(결정 문장)

- 몬스터 상태는 `UTDMonsterThinkSubsystem` 의 슬롯 배열이 정본이고, 단일 고우선 `TG_PrePhysics` 틱 함수 하나가 SimulationId 오름차순으로 네 채널(think/move/judge/present)을 처리한다(D14·D15).
- 채널별 주기는 `uint8 PeriodTable[4][4]`(채널 × LOD) 데이터이며 기본값은 think 6/13/32/64 스텝(≈10.7/4.9/2/1Hz), move·judge 는 모든 LOD 에서 매 스텝, present 는 1/1/6/0 프레임(D21·D22). 전역 기본 표는 `Content/MonsterAI/PeriodTable.json`, 종별 오버라이드는 정의 JSON 최상위 키 `lod_periods` 다. LOD 는 사고 주기와 표현만 바꾸고 이동·판정은 바꾸지 않는다.
- 위상은 `Phase[Slot] = Slot % Period` 로 슬롯 번호에서 결정적으로 파생한다. 난수를 쓰지 않는다(D22). 게임의 think/move/judge 위상은 커널 스텝 카운터, present 위상만 렌더 프레임 카운터를 쓴다.
- 게임은 프레임 델타를 누적해 1/64초 스텝을 최대 4회 돌린다. 시뮬은 `World->Tick` 한 번에 스텝 1회다(D15·D27).
- 잡몹 몸은 `APawn` + `UFloatingPawnMovement`, AIController 없음, 액터 틱 없음, `UFloatingPawnMovement` 컴포넌트 틱도 끔(컨트롤러가 없으면 어차피 이동하지 않는다 — FloatingPawnMovement.cpp:37-38). 위치는 슬롯 배열이 정본이고 present 채널이 스윕 없이 트랜스폼을 쓴다. 물리 오버랩·RVO(Reciprocal Velocity Obstacles, 상호 속도 장애물 회피)·DetourCrowd 없음. 근접 탐색은 `THierarchicalHashGrid2D`(셀 250cm) 다(D16·D17). 정예 `ATDMonsterCharacter` 는 컨트롤러 없이 CMC 를 돌리므로 `bRunPhysicsWithNoController=true` 가 필수다.
- 규모 단계는 A(≤300 전원 액터, 몬스터 몫 ≤ 4.0ms) → B(300~1,000, 무액터 L3 + 액터 풀 400, ≤ 6.0ms) → C(ISM/VAT 후열) → D(Mass 이관 선택지) 이며 실측 KPI 로만 넘어간다(D24). move·judge 를 LOD 불변으로 재계산한 1,000마리 추정 합계는 8.5~10.3ms 로 6.0ms 를 넘으므로, [미결 1](#미결-사항사용자-결정-필요)(L3 move 완화)은 단계 B 진입 전 **결정 필요** 항목이다.
- 게임·시뮬 모두 단일 스레드로 시작한다. think 병렬화는 게임에서만, 병목 실측 + 해시 동일 테스트 통과 뒤 옵트인한다(D25).
- 프레임 예산 표의 수치는 전부 "추정"이며 Phase 1 의 `stat TDMonsterAI` 실측으로 갱신하는 살아 있는 표다(D24).

AI 모델 자체(유틸리티 + FSM)는 [01 AI 모델 결정](01-ai-model-decision.md), 슬롯 배열과 정의 형식은 [02 아키텍처와 정의 형식](02-architecture-and-definition-format.md), 고정 스텝 루프와 해시 게이트는 [04 시뮬레이터](04-combat-simulator.md), 작업 순서는 [07 로드맵](07-roadmap-and-tasks.md)에 있다. 이 문서는 "언제 누가 얼마나 자주 도는가" 만 다룬다.

---

## 1. 어떤 AI 를 몇 마리에 어떤 주기로 — 채널 × LOD 주기표

### 1.1 네 채널

| 채널 | 하는 일 | 왜 따로 도는가 | 근거 |
|---|---|---|---|
| think | 유틸리티 채점·타깃 선정·FSM 전이 요청 | 가장 비싸고 낮은 빈도로 충분(킬존 2 개인 5Hz, 드래곤 에이지 인퀴지션 "필요한 만큼") | web-ai-architecture-comparison 결론 4 |
| move | 2D 이동 적분·분리 조향·공간 해시 갱신 | 스텝을 건너뛰면 결과가 스텝 의존이 된다 → 모든 LOD 에서 매 스텝 | D21, 심사 "판정 = C" 판정 |
| judge | 공격 시간표 진행·형상 판정·`UTDDamageSubsystem::ExecuteRules` | 화면 밖에서도 판정이 멈추면 승률이 카메라에 종속된다 | D21, engine-movement-anim-scale §6 주의점 1 |
| present | 액터 트랜스폼 쓰기·애니 상태 태그·ISM 갱신 | 게임 스레드 최대 비용 항목(Mass 번역기 500마리 3ms) | web-mass-monster-performance 결론 3 |

### 1.2 LOD 4단 판정(탑다운 전용 입력)

| 등급 | 판정 조건(우선순위 순) | 의미 |
|---|---|---|
| L0 교전 | 화면 안 ∧ (플레이어 2D 거리 ≤ 1,200cm ∨ 최근 3초(192스텝) 안에 피격·공격) | 전투 중 |
| L1 화면 안 | 화면 안 ∧ L0 아님 | 보이지만 멀다 |
| L2 화면 밖 근처 | 화면 밖 ∧ 거리 ≤ 4,000cm | 곧 들어온다 |
| L3 원거리·휴면 | 거리 > 4,000cm 또는 시뮬 반경 밖 | 상태만 유지 |

거리 임계(1,200 / 4,000cm)는 B §4.2 의 제안값이며 "추정"이다. 화면 안팎 판정 방법은 [§10](#10-탑다운-화면-안팎-판정) 에 있다. 시뮬레이터 기본은 전원 L0 이고, 성능 검증 시나리오만 `virtual_camera { center, half_extent, actor_pool }` 입력을 명시한다(D23, 형식은 [04 시뮬레이터](04-combat-simulator.md) §9.1 이 정본).

### 1.3 주기표 기본값 `PeriodTable[Channel][Lod]`

값은 "스텝 수"(1/64초 단위)이며 0 은 "이 채널을 돌리지 않음"이다. Hz 는 참고 환산이다. 전역 기본값의 정본은 `Content/MonsterAI/PeriodTable.json`([07 로드맵](07-roadmap-and-tasks.md) M3-03)이고, 종별 정의 JSON 의 최상위 키 `lod_periods` 가 행 단위로 덮어쓴다(02 규칙 15).

| 채널 \ LOD | L0 교전 | L1 화면 안 | L2 화면 밖 근처 | L3 원거리 |
|---|---|---|---|---|
| think | 6 (≈10.7Hz) | 13 (≈4.9Hz) | 32 (2Hz) | 64 (1Hz) |
| move | 1 (매 스텝) | 1 | 1 | 1 |
| judge | 1 (매 스텝) | 1 | 1 | 1 |
| present | 1 프레임 | 1 프레임 | 6 프레임 | 0 (없음) |

읽는 법과 규칙:

| 규칙 | 내용 |
|---|---|
| 종별 오버라이드 | 정의 JSON 의 `think_hz`(D6)가 L0 think 를 정한다. `Period = round(64 / think_hz)`, 하한 2. L1~L3 는 기본 표 값(13/32/64)을 L0 값에 비례해 반올림한 값을 쓰되, JSON 최상위 키 `lod_periods`(02 규칙 15, 예 `{ "think": [6, 13, 32, 64] }`, 채널별 스텝 주기 4개)가 있으면 그 값을 그대로 쓴다. `lod` 같은 다른 키 이름은 검증기 1단이 미지 키로 거부한다(02 규칙 2). |
| move·judge 는 LOD 불변 | 기본 표에서 1 이다. 값을 바꾸면 시뮬 결과가 바뀌므로, 바꾼 표는 시나리오 입력에 그대로 기록되어야 한다(D23). L3 이동 완화(예: 8스텝)는 [미결 1](#미결-사항사용자-결정-필요) 이다. |
| present 는 프레임 단위 | present 만 커널 스텝이 아니라 렌더 프레임(`GFrameCounter`)에 묶인다. 시뮬에는 present 가 없다(헤드리스). L1 을 1프레임으로 두는 이유: move 가 매 스텝이라 화면 안 몬스터의 위치는 매 스텝 새 값이고, 2프레임으로 줄이면 30Hz 갱신이 눈에 띈다(B §4.2 의 "L1 2프레임" 은 move 32Hz + 외삽 전제였으므로 근거가 사라졌다). 비용 차이는 약 0.16ms(추정, [§6.1](#61-1000마리단계-b-l0-40--l1-80--l2-280--l3-600)). |
| think 주기의 상한 | `uint8` 이므로 최대 255 스텝(≈4초). 기본 L3 는 64(1Hz, D22)이며, 완전 휴면(think 0)은 종별 `lod_periods` 로만 명시적으로 켠다. think 0 인 슬롯도 승격 시 첫 스텝 강제 사고([§1.6](#16-히스테리시스승격강등-규칙))로 복귀한다. |

"몇 마리에" 의 답: 300마리(단계 A, 분포 L0 40 / L1 80 / L2 180 추정)일 때 스텝당 think 는 `40/6 + 80/13 + 180/32 ≈ 18.5회`, 프레임당(60fps, 1.07 스텝) 약 20회다. 1,000마리(단계 B, L0 40 / L1 80 / L2 280 / L3 600)는 `6.7 + 6.2 + 8.8 + 9.4 ≈ 31회/스텝` 이다. 사고 1회 ≈ 5µs(추정, B §4.6 단가, [§6 예산 표](#6-프레임-예산-표추정--phase-1-실측))이면 think 채널은 0.1~0.2ms 로, 규모의 병목은 사고가 아니라 매 스텝 도는 move·공간 해시와 present·애니메이션이다.

### 1.4 위상 분산 공식

```text
Phase[Slot]  = Slot % Period[Channel][Lod[Slot]]
bIsDue(Slot) = ((StepIndex + Phase[Slot]) % Period) == 0
```

- `Slot` 은 SimulationId 이며 스폰 순번이다. 난수를 쓰지 않으므로 같은 스폰 순서면 같은 위상이다. Mass 의 가변 틱은 첫 틱 시각을 `Rand(0, TickRate)` 로 흩뜨리고 결정론 모드에서만 고정하는데(engine-mass-entity-ai 결론 6, MassLODTickRateController.h:101-156), 우리는 항상 고정이다.
- `StepIndex` 는 커널 스텝 카운터다. 시뮬에서는 매 스텝 `++GFrameCounter` 를 하므로(D27) `GFrameCounter` 와 같이 증가한다. 게임에서는 한 프레임에 스텝이 0~4회 돌므로 프레임 카운터와 다르며, 서브시스템이 자체 카운터를 갖는다. D22 의 "프레임 카운터 버킷" 은 시뮬 기준(스텝 = 프레임)의 표현이고, 게임에서는 think/move/judge 에 커널 스텝 카운터를 쓴다 — 스텝 기준이어야 게임과 시뮬의 사고 시점(몇 번째 스텝에 사고하는가)이 같다. present 만 프레임 카운터를 쓴다.
- LOD 가 바뀌면 `Period` 가 바뀌므로 `Phase` 를 다시 계산한다. 이때 [§1.6](#16-히스테리시스승격강등-규칙) 의 "승격 시 첫 스텝 강제 사고"가 같이 적용된다.
- present 의 위상은 애니메이션 예산 할당기와 같은 식 `((GFrameCounter + FrameOffset) % TickRate) == 0`(engine-movement-anim-scale §3-1, AnimationBudgetAllocator.cpp:440)이며 `FrameOffset = Slot % Period` 다.

### 1.5 프레임당 스텝 상한 누적기(게임 전용)

```cpp
void UTDMonsterThinkSubsystem::AccumulateAndStep(float FrameDeltaSeconds)
{
	static constexpr float StepSeconds = 1.0f / 64.0f;
	static constexpr int32 MaxStepsPerFrame = 4;
	Accumulator = FMath::Min(Accumulator + FrameDeltaSeconds, StepSeconds * MaxStepsPerFrame);
	int32 StepsThisFrame = 0;
	while (Accumulator >= StepSeconds && StepsThisFrame < MaxStepsPerFrame)
	{
		Accumulator -= StepSeconds;
		RunKernelStep(StepSeconds);
		++StepsThisFrame;
	}
	PresentAlpha = Accumulator / StepSeconds;
}
```

| 항목 | 규칙 | 이유 |
|---|---|---|
| 스텝 크기 | 1/64초 고정 | 이진 소수라 float 누적 오차가 없다(D27). 정예의 CMC(Character Movement Component, 캐릭터 이동 컴포넌트)는 게임 전용이며 커널 스텝이 아니라 프레임 델타로 컴포넌트 틱한다(1/64 와 무관). 시뮬 몸 `ATDSimCombatant` 에는 CMC 가 없다 |
| 상한 4 | 누적기를 `4 × Step` 으로 클램프 | 긴 프레임(로딩 히치)에서 폭주·죽음의 나선을 막는다. 상한에 걸리면 시뮬 시간이 실시간보다 느려질 뿐 커널 순서는 유지된다(B §4.3) |
| 시계 불일치(게임 전용) | 상한에 걸린 프레임에서는 커널 시간(스텝 수 × 1/64) < 월드 시간 | GE(Gameplay Effect) 지속시간·쿨다운은 `FTimerManager` 월드 시간으로 진행되고(engine-gas-determinism 결론 1, D18) judge 의 공격 시간표는 스텝으로 진행되므로, 예를 들어 100ms 프레임에서 커널은 62.5ms 만 흐르고 GE 타이머는 100ms 흘러 게임에서만 쿨다운이 먼저 끝난다. 시뮬은 1스텝 = `World->Tick` 1회라 어긋나지 않는다(D15·D27). 이 편차는 게임 전용이며 D32 C 단계(통계 등가)로만 감시한다. 버린 시간을 `DroppedKernelSeconds` 누적 통계로 `stat TDMonsterAI` 에 찍는다. 월드 델타 자체를 같이 클램프할지는 [미결 7](#미결-사항사용자-결정-필요) |
| 잔여 시간 | 다음 프레임으로 이월, `PresentAlpha` 로 표현 보간 | 60fps 에서 스텝은 프레임당 1.07회라 1회/2회가 번갈아 온다. 보간 없이는 위치가 튄다 |
| 시뮬 | 이 함수를 쓰지 않는다 | 시뮬 러너는 `World->Tick(LEVELTICK_All, 1/64)` 만 부르고 틱 함수가 스텝 1회를 돈다(D15). 이중 스텝은 결정론 게이트가 "스텝당 사고 횟수"로 검출한다 |

시뮬 월드에서 이 누적기가 절대 돌지 않도록 `bIsFixedStepWorld` 를 세션이 서브시스템에 세팅하고, 세팅되면 `AccumulateAndStep` 은 조기 리턴하고 `RunKernelStep(1/64)` 만 틱 함수에서 호출한다. 연결: 세션 픽스처 파라미터 `FTDScopedCombatWorldParams::bIsFixedStep`(04 §2.1)이 true 일 때 세션이 서브시스템의 `bIsFixedStepWorld` 를 세팅한다 — 두 플래그는 서로 다른 객체(세션 / 서브시스템)의 것이다.

### 1.6 히스테리시스·승격·강등 규칙

| 규칙 | 값 | 근거 |
|---|---|---|
| 거리 히스테리시스 | 승격 임계 R, 강등 임계 R × 1.10 | Mass `BufferHysteresisOnDistancePercentage=10` 관행(engine-mass-entity-ai §2-1, MassSimulationLOD.h:83-104) |
| 교전 플래그 유지 | 마지막 피격·공격 후 192스텝(3초) | B §4.2 제안값(추정) |
| LOD 평가 주기 | 32스텝(2Hz), 스텝 시작 시 입력 스냅샷으로 계산 | 시그니피컨스 갱신 2Hz(B §4.2). `USignificanceManager::Update` 는 엔진이 호출하지 않으므로 어차피 직접 돌려야 한다 — 입력이 둘뿐이라 자체 정렬을 쓴다(engine-movement-anim-scale 결론 7, SignificanceManager.cpp:478) |
| 승격 시 첫 스텝 강제 사고 | `NextThinkStep = StepIndex`. `CollectDue(Think)` 는 위상과 무관하게 `NextThinkStep <= StepIndex` 인 슬롯을 포함한다([§2](#2-스케줄러-설계)) | 튀는 행동·포즈 방지(engine-movement-anim-scale §6 주의점 2, `a.Budget.ForceTickWhenComponentExitsOffScreen`, AnimationBudgetAllocatorCVars.cpp:36-39) |
| 승격 순서 | 액터 풀 여유가 있을 때 (거리², SimulationId) 오름차순으로 상위 K | 예산 할당기의 "상위 K" 방식(engine-movement-anim-scale §3-1) |
| 프레임당 승격 상한(L3→L2, 단계 B) | 프레임당 K = 8 마리(추정) 또는 액터 활성화 시간 예산 0.5ms(추정) 중 먼저 닿는 쪽. 초과분은 다음 프레임으로 이월 | 풀에서 꺼내 메시·ASC 를 초기화하는 비용이 한 프레임에 몰리면 히치가 된다. Mass 도 액터 스폰을 프레임당 1.5ms 예산으로 시간 분할한다(engine-mass-entity-ai 결론 9). 탑다운은 한 번에 수십 마리가 화면에 들어오므로 상한이 필요하다 |
| 강등 금지(L2→L3, 무액터 L3 도입 이후) | 활성 GE(Gameplay Effect) 있음 ∨ 체력 < 최대 ∨ 보스/엘리트 | D18·D24. 무액터로 가면 ASC(Ability System Component) 가 사라지므로 상태가 있는 개체는 내리지 않는다 |
| 승격 시 값의 정본 | ASC 값이 정본, SoA 체력 미러는 L3 에서만 유효 | D18 |

무액터 L3 는 단계 B 에서만 존재한다. 단계 A 에서는 L3 도 액터를 유지하고 틱만 끄므로 강등 금지 규칙은 "표현 끔"에만 적용된다.

---

## 2. 스케줄러 설계

B §4.3 의 `CollectDue` 버킷을 프로젝트 이름 규칙으로 옮긴 것이다. 서브시스템의 틱 함수가 채널 순서(think → move → judge → present)로 이 함수를 부른다. 스케줄러는 상태를 따로 갖지 않는다 — LOD·위상·생존 비트·`NextThinkStep` 의 정본은 [§8.2](#82-이관-가능한-soa-모양) 의 `FTDMonsterBrainSlot` 프래그먼트 배열이고(D14), 스케줄러는 주기표와 그 배열을 읽는 함수 묶음이다.

```cpp
enum class ETDTickChannel : uint8 { Think, Move, Judge, Present, Count };
enum class ETDMonsterLod : uint8 { Engaged, OnScreen, NearOffScreen, Far, Count };

struct FTDMonsterTickScheduler
{
	uint8 PeriodTable[(int32)ETDTickChannel::Count][(int32)ETDMonsterLod::Count]; // Content/MonsterAI/PeriodTable.json 에서 로드

	uint8 GetPeriod(ETDTickChannel Channel, ETDMonsterLod InLod) const
	{
		return PeriodTable[(int32)Channel][(int32)InLod];
	}

	static uint8 MakePhase(int32 Slot, uint8 Period)
	{
		return Period == 0 ? 0 : (uint8)(Slot % Period);
	}

	// LOD 전환: think 위상(스텝 기준)과 present 위상(프레임 기준)을 따로 다시 계산한다.
	void SetLod(FTDMonsterBrainSlot& Slots, int32 Slot, ETDMonsterLod NewLod, uint64 StepIndex) const
	{
		FTDMonsterLodFragment& L = Slots.Lod[Slot];
		const bool bPromoted = NewLod < L.Lod;
		L.Lod = NewLod;
		L.ThinkPhase = MakePhase(Slot, GetPeriod(ETDTickChannel::Think, NewLod));
		L.PresentPhase = MakePhase(Slot, GetPeriod(ETDTickChannel::Present, NewLod));
		if (bPromoted) { Slots.Brain[Slot].NextThinkStep = StepIndex; } // 승격 시 첫 스텝 강제 사고(§1.6)
	}

	// Counter: think/move/judge 는 커널 StepIndex, present 는 GFrameCounter
	void CollectDue(const FTDMonsterBrainSlot& Slots, ETDTickChannel Channel, uint64 Counter, TArray<int32>& OutSlots) const
	{
		OutSlots.Reset();
		for (int32 Slot = 0; Slot < Slots.Lod.Num(); ++Slot)
		{
			if (!Slots.bIsAlive[Slot]) continue;
			const FTDMonsterLodFragment& L = Slots.Lod[Slot];
			const uint8 Period = GetPeriod(Channel, L.Lod);
			if (Channel == ETDTickChannel::Think && Slots.Brain[Slot].NextThinkStep <= Counter)
			{
				OutSlots.Add(Slot); // 강제 사고: 위상·Period 0 과 무관하게 포함
				continue;
			}
			if (Period == 0) continue;
			const uint8 Phase = Channel == ETDTickChannel::Present ? L.PresentPhase : L.ThinkPhase;
			if ((Counter + Phase) % Period != 0) continue;
			OutSlots.Add(Slot);
		}
	}
};
```

think 채널이 실제로 사고를 마치면 `NextThinkStep = StepIndex + Period` 로 되돌려 강제 분기가 한 번만 걸리게 한다. move·judge 는 Period 가 1 이라 위상이 무의미하며, 위상 배열은 think 와 present 두 개만 둔다(`NextThinkStep` 은 `StepIndex` 와 같은 `uint64`).

| 설계 포인트 | 내용 |
|---|---|
| 순회 순서 | 슬롯 인덱스 오름차순 = SimulationId 오름차순 고정(D30). `OutSlots` 는 정렬이 필요 없다 |
| 슬롯 재사용 | 게임은 프리리스트 후입선출 + 세대 번호, 시뮬은 재사용 없이 단조 증가(D14) |
| 비용 | 슬롯 수 N 에 대해 O(N) 순회. 1,000 슬롯 × 4채널이면 4,000회 분기 + `uint64` 나머지 연산이라 수십 µs 대(추정). [§6.1](#61-1000마리단계-b-l0-40--l1-80--l2-280--l3-600) 에 '스케줄러 순회' 행으로 잡는다. 정렬·힙이 필요 없다 |
| 틱 함수 등록 | `FTickFunction` 하나, `TickGroup = TG_PrePhysics`, `bHighPriority = true`, `bAllowTickBatching = false`. 같은 그룹 안에서 고우선 배열이 먼저 해제되므로 기본 TG_PrePhysics 인 `ATDDamageEntity`(TDDamageEntity.cpp:13-14, 틱 그룹 미지정) 보다 먼저 도는 경향이 있지만(engine-determinism-headless 결론 3, TickTaskManager.cpp:1170-1205), 이는 보장이 약하다. 투사체 틱과 judge 의 상대 순서가 결과에 영향을 주므로 `ATDDamageEntity` 스폰 시 `PrimaryActorTick.AddPrerequisite(Subsystem, SubsystemTickFunction)` 을 걸어 순서를 명시한다(engine-determinism-headless 프로젝트 시사점 3) |
| 액터 `TickInterval` 을 쓰지 않는 이유 | 간격 틱은 "누적 월드 시간"을 델타로 주므로 결정론 도구가 아니다(engine-movement-anim-scale 결론 6, TickTaskManager.cpp:2825-2843) |
| present 는 별도 카운터 | `CollectDue(Present, GFrameCounter, …)` 로 프레임 카운터를 넘긴다. 나머지 채널은 `StepIndex` |

이 형태는 엔진이 대량 객체에 쓰는 정석("액터 틱 끄고 한 틱 함수가 배열 순회") 과 같다 — MassEntity 의 `FMassProcessingPhase : FTickFunction`, 애니메이션 예산 할당기의 `OnWorldPreActorTick` 한 곳 갱신(engine-movement-anim-scale 결론 7, MassProcessingPhaseManager.h:54, AnimationBudgetAllocator.cpp:797-803).

---

## 3. 액터 한 마리의 비용 구성과 제거 목록

풀 AI 캐릭터 1마리는 프레임당 0.13~0.41ms 이며 100마리면 최악 41ms 로 60fps 예산을 넘는다(web-mass-monster-performance 결론 1, StraySpark 2026). 아래는 그 비용을 항목별로 떼어내는 목록이다.

| # | 비용 항목 | 기본 동작 | 우리 조치 | 근거 |
|---|---|---|---|---|
| 1 | `AAIController` 액터 틱 | `AController` 생성자가 `bCanEverTick=true`, `AAIController::Tick` 은 `UpdateControlRotation` 만 호출 | 잡몹·정예 모두 AIController 없음. `ATDMonsterCharacter` 의 AutoPossessAI/AIControllerClass 제거(D38) | engine-behaviortree-tick 결론 11, Controller.cpp:62, AIController.cpp:58-63 |
| 2 | 몬스터 액터 자체 틱 | 액터·컴포넌트마다 틱 함수 큐잉·태스크 생성 | `PrimaryActorTick.bCanEverTick=false`. 몬스터 액터 틱은 없고 AI 는 서브시스템 틱 함수 하나가 돈다. 남는 컴포넌트 틱: 정예 CMC 1개, ASC(조건부 자동 비활성, 10행). 잡몹 `UFloatingPawnMovement` 틱은 끈다([§4.1](#41-itdmonsterbody-3구현d16)) | engine-movement-anim-scale 결론 7, FloatingPawnMovement.cpp:23 |
| 3 | CMC 바닥 스윕 + 라인 트레이스 | `bAlwaysCheckFloor=true` 기본, 매 프레임 캡슐 스윕 1회(관통 시 2회) + 라인 트레이스 1회 | 잡몹은 CMC 없음. 정예·보스는 `bAlwaysCheckFloor=false` + `MOVE_NavWalking` + `bRunPhysicsWithNoController=true`(컨트롤러가 없으면 PhysWalking·물리 루프·PhysicsRotation 이 이 플래그 없이는 조기 리턴 — CharacterMovementComponent.cpp:5662·5686·6637, .h:502) | engine-movement-anim-scale 결론 1·2, CharacterMovementComponent.cpp:798·7125-7183·6082-6095 |
| 4 | CMC 물리 상호작용 | `bEnablePhysicsInteraction=true` 기본, 틱 끝에 `ApplyRepulsionForce` 가 `GetOverlapInfos()` 전체 순회 | 정예·보스 `bEnablePhysicsInteraction=false` | engine-movement-anim-scale 결론 1, cpp:770·1804-1809·11798-11802 |
| 5 | RVO 회피 | `UAvoidanceManager` 가 `AvoidanceObjects` 전체 순회 → O(N²) | 쓰지 않음. 공간 해시 분리 조향으로 대체 | engine-movement-anim-scale 결론 3, AvoidanceManager.cpp:361·403 |
| 6 | DetourCrowd | `MaxAgents` 기본 50, 고정 상한 | 잡몹에 쓰지 않음(D17). 정예·보스 ≤10 은 허용 검토([미결 3](#미결-사항사용자-결정-필요)) | web-mass-monster-performance 결론 8, CrowdManager.cpp:168 |
| 7 | 물리 오버랩 이벤트 | 브로드페이즈 + 컴포넌트마다 `FOverlapInfo` 배열 + Begin/End 델리게이트 | 캡슐은 `QueryOnly`, `bGenerateOverlapEvents=false`. 탐지는 공간 해시 | engine-movement-anim-scale §5, web-mass-monster-performance 결론 2("충돌 스피어 1개 추가 → 1.5fps") |
| 8 | 애님 그래프 화면 밖 틱 | `ACharacter` 기본 `AlwaysTickPose`, URO(Update Rate Optimization, 갱신률 최적화) 비렌더 기본 4프레임 | LOD 별 `VisibilityBasedAnimTickOption` + 예산 할당기([§7](#7-애니메이션표현-계층)) | engine-movement-anim-scale 결론 9, Character.cpp:125, EngineTypes.h:2820 |
| 9 | BT(Behavior Tree, 비헤이비어 트리) 마리당 UObject 3~4개 | `UBehaviorTreeComponent`·`UBlackboardComponent` 등 + 인스턴스 메모리 | BT 미사용(D4). 유틸리티 점수기는 슬롯 배열 위에서 배치 실행 | engine-behaviortree-tick 결론 6, web-mass-monster-performance §2-3(BT→자체 로직 22~24 → 26~28fps) |
| 10 | ASC 틱 | 기본 틱 활성이지만 몽타주·틱 태스크·틱 가능 AttributeSet 이 없으면 `GetShouldTick()` 이 false | 몬스터 ASC 는 몽타주·틱 태스크 미사용 → 틱 비용 0 근사(D18) | engine-gas-determinism 결론 4, AbilitySystemComponent_Abilities.cpp:139-160 |

떼어낸 뒤 남는 마리당 비용은 "캡슐 1개(QueryOnly) + 스켈레탈 메시(예산 할당기 관리) + ASC 메모리·GE 타이머" 다. 커뮤니티 실측은 이 구성(CMC·Character 없는 Pawn)으로 1,000마리 약 60fps, 2,000마리 약 46fps 였다(web-mass-monster-performance 결론 2, 2025). 우리 목표(단계 A 300마리 ≤ 4.0ms)는 그보다 보수적이다.

---

## 4. 몸 3구현과 이동·근접 탐색

### 4.1 `ITDMonsterBody` 3구현(D16)

| 구현 | 대상 | 이동 | 충돌 | 컨트롤러 | 액터 틱 | 남는 컴포넌트 틱 |
|---|---|---|---|---|---|---|
| `APawn` + `UFloatingPawnMovement` | 게임 잡몹 | `UFloatingPawnMovement` 컴포넌트 틱은 끈다(`PrimaryComponentTick.bCanEverTick=false`). 컨트롤러가 없으면 `TickComponent` 가 `Controller && Controller->IsLocalController()` 게이트(FloatingPawnMovement.cpp:37-38) 안에서만 속도 적용·`SafeMoveUpdatedComponent`(cpp:64)를 하므로 켜 두어도 움직이지 않는다. 위치는 슬롯 배열이 정본이고 present 채널이 `SetActorLocationAndRotation(…, bSweep=false)` 로 쓴다. 컴포넌트는 `INavMovementInterface` 자리로만 남기거나 제거한다 | 캡슐 `QueryOnly`, 오버랩 이벤트 끔 | 없음 | 없음 | 없음(ASC 는 조건부 비활성) |
| `ATDMonsterCharacter` | 게임 정예·보스(≤ 수십) | CMC `MOVE_NavWalking`, `bRunPhysicsWithNoController=true` 필수(cpp:5662·5686·6637 컨트롤러 게이트), `bAlwaysCheckFloor=false`, `bEnablePhysicsInteraction=false`, RVO 끔, `bSweepWhileNavWalking` 은 L0 만. 서브시스템 move 채널이 슬롯 속도를 `CMC->Velocity`/`RequestDirectMove` 로 넘기고 CMC 가 프레임 델타로 적분한다 | 캡슐 | 없음(이동 요청은 서브시스템이 직접, D38) | 없음 | CMC 1개(프레임 델타), ASC(조건부 비활성) |
| `ATDSimCombatant` | 시뮬 | 수학 이동(픽스처 `SpawnCombatant` 방식) | 없음(형상 판정은 수학) | 없음 | 없음 | 없음 |

- 내비메시 길찾기(`UPathFollowingComponent`)는 쓰지 않는다. 그 컴포넌트는 `AAIController` 가 생성·소유하는데(engine-behaviortree-tick 결론 6) 이 설계는 AIController 를 없앴고(D38), 이동은 직선 접근 + 분리 조향 + 플로우 필드라 경로 추종이 없다(D17). 조사 결론 4(engine-movement-anim-scale, `INavMovementInterface` 만 요구)는 AIController 전제의 문장이라 여기서는 성립하지도 필요하지도 않다. 잃는 것은 바닥·중력·계단·루트모션이며 탑다운 지상 몬스터는 내비메시 `ProjectPoint` 투영과 플로우 필드로 대체한다. 나중에 경로가 필요하면 서브시스템이 `UNavigationSystemV1::FindPathSync` 를 직접 부른다.
- 정예의 NavWalking 은 캡슐의 WorldStatic/WorldDynamic 응답을 Ignore 로 바꾸고 0.1초 주기로만 투영한다(engine-movement-anim-scale 결론 2, CharacterMovementComponent.cpp:6471-6472·834). 투영 타이머의 스폰 시 무작위 분산(cpp:6477)은 `FRandRange` 이므로 시뮬에서는 이 몸을 쓰지 않는다(시뮬 몸은 `ATDSimCombatant` 만).
- 세 구현 모두 "위치·속도·FSM 상태의 정본은 슬롯 배열"이고, 몸은 present 채널에서 트랜스폼을 받아 쓰기만 한다. 게임 잡몹은 **스윕을 하지 않는다**: 스윕 결과(벽에 막힘)를 슬롯 위치에 되먹이면 게임 위치가 시뮬과 갈라지고, 되먹이지 않으면 스윕이 무의미하기 때문이다. 장애물은 [§4.3](#43-phase-3-플로우-필드b-44) 플로우 필드의 도달성 마스크와 [§4.2](#42-이동d17-직선-접근--분리-조향--근접-자리-토큰) 격자 높이값으로만 처리한다. 스윕 결과를 "장애물 보정 입력"으로 다음 스텝에 넣는 대안(게임 전용 편차, D32 B 단계 허용 오차)은 정예 CMC 경로에만 존재한다.

### 4.2 이동(D17): 직선 접근 + 분리 조향 + 근접 자리 토큰

| 단계 | 규칙 | 결정성 보장 |
|---|---|---|
| 목표 방향 | `MoveToward` 원시: 타깃 위치 - 내 위치의 2D 정규화 | 스텝 시작 스냅샷만 읽는다(D7) |
| 분리 조향 | 공간 해시 반경 200cm 이웃 최대 8, 역제곱 반발, 합력을 `MaxSeparation` 으로 클램프 | 이웃은 `(거리², SimulationId)` 오름차순 정렬 후 상위 8(제곱 거리 비교, 제곱근 없음 — [§10](#10-탑다운-화면-안팎-판정), 07 M3-02 와 동일 표기) |
| 근접 자리 토큰(`ring_slot`) | 타깃마다 반경 R(종별, 예 150cm)에 K개(예 8) 자리. 자리는 SimulationId 오름차순으로 청구하고 사망·L3 강등·타깃 변경 시 반납 | 청구 순서가 스폰 순서라 프로세스 간 동일 |
| 적분 | `Pos += Vel × Step`, 속도는 종별 `MoveSpeed` 로 클램프 | 1/64 이진 스텝 |
| 바닥 Z | L0/L1: 8스텝마다 내비메시 `ProjectPoint`(게임만). L2/L3·시뮬: 격자 높이값 또는 평면 | 시뮬 월드는 기본 `CreateNavigation(false)`(D27) 이므로 시뮬은 내비메시를 읽지 않는다 |

자리 토큰이 없으면 잡몹이 한 점에 겹치고 분리 조향만으로는 진동한다. 토큰은 "링에 서 있는 놈이 공격권을 갖는다"는 ARPG 관행을 데이터로 표현한 것이다(추정 근거, 조사 없음 — [미결 2](#미결-사항사용자-결정-필요)).

### 4.3 Phase 3 플로우 필드(B §4.4)

| 항목 | 값 |
|---|---|
| 격자 | 방 단위, 셀 100cm, 방 하나 64×64 = 4,096셀 |
| 목표 | 플레이어(+ 보스 소환 지점 등 소수), 목표당 1장 |
| 재계산 | BFS 비용 격자를 2Hz(32스텝마다) 재계산. 재계산은 스텝 인덱스 기준 4스텝으로 결정적으로 분할(행 범위 고정). D17 과 07 M3-04 의 "4프레임 결정적 분할" 은 커널 스텝 4회를 뜻한다 — 시뮬에는 프레임이 없으므로 스텝 인덱스 기준으로 고정한다(게임에서 프레임 기준으로 나누면 히치 프레임에서 게임·시뮬의 필드 갱신 시점이 갈린다) |
| 도달성 | 격자 생성 시 내비메시 `ProjectPoint` 로 1회만 표시(게임). 시뮬은 시나리오가 준 장애물 마스크 |
| 접목 지점 | `MoveToward` 원시의 구현체 교체. 정의 JSON 은 바뀌지 않는다 |
| 근거 | 플로우 필드는 200유닛 단순 지도에서 1.6ms 대 내비 6.3ms, 미로형에서는 불리 → 방 단위 격자로 상쇄(web-mass-monster-performance 결론 8, UE 5.3.2 기준) |

### 4.4 공간 해시 `THierarchicalHashGrid2D`(셀 250cm)

| 항목 | 내용 | 근거 |
|---|---|---|
| 타입 | `THierarchicalHashGrid2D<2, 4, uint32>`(AIModule 공개 헤더, 템플릿이라 AI 시스템 생성 불필요) | engine-movement-anim-scale 결론 11, HierarchicalHashGrid2D.h:34-35·125 |
| 셀 | 250cm(레벨 0), 레벨 1 은 1,000cm | D17 |
| 갱신 | move 채널이 위치를 바꾼 슬롯만 `Move(ID, OldLoc, NewBounds)`. 셀이 바뀔 때만 Remove+Add | h:180·231·313-331 |
| 질의 | 분리 반경 200cm → 약 4셀, 공격 후보 반경 500cm → 약 9셀(`(r/Cell + 1)²`, 추정) | §5 비용 모델 |
| 거짓 양성 | 격자 결과에 실제 2D 거리 검사를 반드시 한다 | h:342·384 |
| 규약 | "갱신은 스텝 끝, 질의는 스텝 시작 스냅샷" — 한 스텝 지연을 규약으로 고정해 순회 순서와 무관하게 같은 결과 | C §4.3 |
| 시뮬 공유 | 격자는 서브시스템 소유라 세 몸 모두 같은 코드 경로 | D17 |

---

## 5. 규모별 단계 A/B/C/D 와 전환 KPI(D24)

| 단계 | 동시 활성 | 구성 | 넘어가는 KPI(실측) |
|---|---|---|---|
| A 전원 액터 | ≤ 300 | 슬롯 배열 + 액터 전원(잡몹 `APawn`), L3 도 액터 유지(present 0) | `stat TDMonsterAI` 몬스터 몫 합 > 4.0ms(300마리) 또는 동시 활성(살아 있는 슬롯) > 300 — 누적 스폰 수가 아니다 |
| B 하이브리드 | 300~1,000 | L3 무액터(SoA 만), 액터 풀 400 고정(D24), 승격/강등 + 강등 금지 규칙 + 프레임당 승격 상한([§1.6](#16-히스테리시스승격강등-규칙)) | 화면 내 동시 스켈레탈 > 120 또는 합 > 6.0ms(1,000마리) |
| C ISM/VAT 후열 | 1,000~2,000 또는 화면 내 ≥ 200 | L1 이하를 ISM(Instanced Static Mesh) + VAT(Vertex Animation Texture) 인스턴스로, 스켈레탈은 L0 상위 60 | 팀이 Mass 표현 파이프라인 재구현보다 도입을 택할 때 |
| D Mass(선택) | 위 조건 + 5.8.1 이상 | SoA → 프래그먼트 1:1 이관, `MassEntity` + `MassCommon/LOD/Representation` 만 | — |

- 단계 A 는 Phase 1 의 "300마리 PIE 실측"으로 검증하고, B 는 Phase 3 에서 실측이 KPI 를 넘을 때만 들어간다([07 로드맵](07-roadmap-and-tasks.md)).
- 액터 풀 400 은 D24 의 값이며 [§6.1](#61-1000마리단계-b-l0-40--l1-80--l2-280--l3-600) 분포(L0 40 + L1 80 + L2 280)의 합과 정확히 같아 여유가 0 이다. 여유 20%(≈480, 추정)로 늘릴지는 [미결 5](#미결-사항사용자-결정-필요) 에서 거리 임계와 함께 결정한다. 풀이 비면 L3→L2 승격은 대기하고, 대기 순서는 (거리², SimulationId) 다.
- 커뮤니티 기준으로 200~300 마리에서는 액터가 Mass(번역기 포함)보다 나을 수 있다(web-mass-monster-performance 결론 3, 2025). 단계 A 를 Mass 없이 시작하는 근거다. 단계 구분(≤300 액터 + 수동 틱 매니저 → 300~1,000 하이브리드 → ≥1,000 Mass/ISM) 자체도 같은 조사의 제안(web-mass-monster-performance 결론 10, §A, §B)을 따른다.

---

## 6. 프레임 예산 표(추정 → Phase 1 실측)

B §4.6 의 마이크로초 단가를 쓰되, move·judge 는 LOD 불변(D21, 심사 "판정 = C")으로 **재계산**했다. B §4.6 원표는 move 를 LOD 별 64/32/10/2Hz(이동 슬롯 147개), judge 를 L0 40마리만으로 계산한 표라 이 문서의 [§1.3](#13-주기표-기본값-periodtablechannellod) 과 맞지 않는다. 단가는 조사 실측 비율에서 유도한 **추정**이며 실측 열은 비어 있다. 기준: 60fps 16.67ms, 프레임당 1.07 스텝, 몬스터 몫 목표 단계 A 4.0ms / 단계 B 6.0ms.

### 6.1 1,000마리(단계 B, L0 40 / L1 80 / L2 280 / L3 600)

| 항목 | 계산(추정) | 프레임당 추정 | 실측 | 근거 |
|---|---|---|---|---|
| 스케줄러 순회 | 1,000 슬롯 × 4채널 = 4,000 분기 + `uint64` 나머지 연산 | 0.02~0.04ms | — | [§2](#2-스케줄러-설계) 비용 행(추정) |
| think | 사고당 약 5µs(8행동 × 4고려, 조기 종료) × 약 31~33회/스텝 × 1.07 | 0.17~0.18ms | — | 추정(B §4.6 단가; 조기 종료 원칙은 web-ai-architecture-comparison 결론 5 — 결론 5 에 시간 수치는 없다) |
| move 적분 | 슬롯당 약 2µs × 1,000 슬롯(모든 LOD 매 스텝) × 1.07 | 2.14ms | — | SoA 순차 연산, D21. B 원표 0.30ms 는 이동 슬롯 147개 기준 |
| 분리 조향 | 이웃 8 × 0.1µs × 1,000 슬롯 × 1.07 | 0.86ms | — | 이웃 상한 8. B 원표 0.13ms 는 167 슬롯 기준 |
| 공간 해시 갱신·질의 | (갱신 1,000 + 질의 1,000) × 1.07 ≈ 2,140회 × 1.3~1.9µs(B 원표 0.4~0.6ms ÷ 314회에서 역산) | 2.8~4.1ms(상한 추정 — 갱신은 셀이 바뀔 때만 Remove+Add 이므로 실제는 더 낮을 가능성) | — | engine-movement-anim-scale §5 |
| 플로우 필드(Phase 3) | 4,096셀 BFS ≈ 0.3ms 를 4스텝 분할, 2Hz | 0.02~0.08ms(피크 0.1) | — | web-mass-monster-performance 결론 8 |
| judge | 시간표 진행 1,000 × 0.1µs × 1.07 + 히트 창 활성 슬롯 ≤ 100(추정) × 후보 8 × 판정 0.3µs × 1.07 + 피해 파이프라인(초당 약 40회 × 30µs) | 0.3~0.4ms | — | engine-gas-determinism §5. B 원표 0.05ms 는 L0 40 기준 |
| present(액터 트랜스폼) | L0 40 + L1 80 + L2 280/6 ≈ 167 × 4µs. 잡몹은 스윕 없음(`bSweep=false`) 이라 스윕 비용 0 | 0.6~0.9ms | — | Mass 번역기 500마리 3ms 의 절반 이하 가정(web-mass 결론 3). L1 2프레임이면 0.16ms 감소 |
| 애니메이션 | 예산 할당기 고정 | 1.5ms | — | `a.Budget.BudgetMs` |
| ASC 틱 | 몽타주·틱 태스크 없음 | ≈ 0 | — | engine-gas-determinism 결론 4 |
| GE 타이머 | 400 액터 × 지속 GE ≤ 3 = 1,200 타이머, O(log n) | < 0.1ms | — | engine-gas-determinism 결론 9 |
| **합계** | | **8.5~10.3ms**(목표 6.0ms 초과 2.5~4.3ms) | — | |

합계가 단계 B 목표 6.0ms 를 넘는다. 초과분은 전부 "1,000 슬롯이 매 스텝 도는" move·분리 조향·공간 해시 세 행(5.8~7.1ms)에서 나오고, 그중 60% 가 L3 600마리 몫이다. [미결 1](#미결-사항사용자-결정-필요)(L3 move 8스텝 완화)을 적용하면 세 행이 약 3.0~3.7ms 줄어 합계 약 5.4~6.6ms(추정) 로 목표 언저리가 된다. 따라서 미결 1 은 단계 B 진입 전 **결정 필요** 항목이다. 단, 단가(2D 적분 2µs, 해시 1회 1.3~1.9µs)는 B 가 보수적으로 잡은 값이라 Phase 1 실측이 절반이면 완화 없이도 들어간다 — 실측 전에는 표를 바꾸지 않는다.

### 6.2 300마리(단계 A, L0 40 / L1 80 / L2 180, 전원 액터)

같은 단가로 재계산하면 스케줄러 0.01 + think 0.10 + move 0.64 + 분리 조향 0.26 + 공간 해시 0.8~1.2 + judge 0.15~0.2 + present 0.5~0.7(150 × 4µs) + 애니 1.5 + GE < 0.1 ≈ **4.0~4.7ms**(추정, 플로우 필드는 Phase 3 이라 0). 단계 A 목표 4.0ms 의 경계에 있으며, 넘는 쪽은 공간 해시 단가에 달려 있다. 최악 케이스(L0 120 동시 교전)는 think 0.1ms 증가, present 0.3ms 증가, 애니 초과분은 예산 할당기가 보간으로 흡수한다. 가장 큰 변수는 매 스텝 도는 공간 해시·move 와 present·애니메이션이며 AI 모델(think)이 아니다.

### 6.3 실측 갱신 절차와 `stat TDMonsterAI`

| 항목 | 내용 |
|---|---|
| 통계 그룹 | `DECLARE_STATS_GROUP(TEXT("TDMonsterAI"), STATGROUP_TDMonsterAI, STATCAT_Advanced)`. 프로젝트에 기존 통계 그룹은 없다(Source grep 0건) |
| 카운터 | 위 표의 행마다 `DECLARE_CYCLE_STAT` 1개(Scheduler/Think/Move/Separation/HashGrid/FlowField/Judge/Present) + 슬롯 수·LOD 별 인원·프레임당 스텝 수·프레임당 승격 수 `DECLARE_DWORD_COUNTER_STAT` + 상한에 걸려 버린 시간 `DroppedKernelSeconds` `DECLARE_FLOAT_ACCUMULATOR_STAT`([§1.5](#15-프레임당-스텝-상한-누적기게임-전용)) |
| 측정 시나리오 | Phase 1: 고블린 300마리 PIE, 카메라 고정, 30초, `stat TDMonsterAI` + `stat unit`. 결과를 `Docs/MonsterAI_CombatSim/perf/<날짜>-<빌드해시>.csv` 로 남긴다 |
| 표 갱신 규칙 | 실측 열이 채워지면 추정 열은 지우지 않고 남겨 오차를 기록한다. 추정 대비 2배 이상 벗어난 행은 [07 로드맵](07-roadmap-and-tasks.md) 의 할 일로 올린다 |
| 시뮬 쪽 | 커맨드렛은 `-stat` 이 없으므로 세션이 스텝당 채널별 `FPlatformTime::Cycles64` 를 합산해 요약(30줄)에 "스텝당 평균 µs" 를 찍는다. 이 값은 결과 해시에 들어가지 않는다 |

---

## 7. 애니메이션·표현 계층

판정은 데이터(시간표)가 권위이므로(D19) 애니메이션을 얼마나 줄여도 전투 결과는 바뀌지 않는다. 이 절은 순전히 게임 스레드 비용 문제다.

| 층 | 수단 | LOD 적용 | 근거 |
|---|---|---|---|
| 1. 예산 할당기 | 몬스터 메시는 `USkeletalMeshComponentBudgeted`, `a.Budget.BudgetMs=1.5`, `OnCalculateSignificance` 정적 델리게이트에 프로젝트 중요도 함수(LOD 등급 → 0~1) 바인딩 | L0 최상위(bNeverSkip 아님), L1 보간 틱, L2 이하 최저 | engine-movement-anim-scale 결론 9·§3-1, AnimationBudgetAllocatorParameters.h:19·34·94, IAnimationBudgetAllocator.h:52 |
| 2. 가시성 옵션 | `VisibilityBasedAnimTickOption` | L0/L1 `AlwaysTickPose`, L2 `OnlyTickMontagesWhenNotRendered`, L3 액터 없음 또는 `bEnableAnimation=false` | engine-movement-anim-scale 결론 8·9, SkinnedMeshComponent.h:96-116, SkeletalMeshComponent.cpp:1883-1915 |
| 3. URO | `bEnableUpdateRateOptimizations=true`, 비렌더 `BaseNonRenderedUpdateRate=4` 기본 유지 | 예산 할당기가 외부 틱률을 잡으면 URO 는 무시된다(`bExternalTickRateControlled`) | SkeletalMeshComponent.cpp:1918-1927 |
| 4. 전환 튐 방지 | `a.Budget.ForceTickWhenComponentExitsOffScreen=1` + 승격 시 첫 스텝 강제 사고 | L2→L1 | AnimationBudgetAllocatorCVars.cpp:36-39 |
| 5. ISM + VAT(단계 C) | AnimToTexture 로 후열 1~2종 베이크, ISM 커스텀 데이터 실수 4개(`TimeOffset/PlayRate/StartFrame/EndFrame`) 로 셰이더 재생, `BatchUpdateInstancesTransforms` 로 일괄 이동 | L1 이하 | engine-movement-anim-scale 결론 10, AnimToTextureInstancePlaybackHelpers.h:27-59, InstancedStaticMeshComponent.h:391 |

- 예산 할당기 기본 `MaxTickedOffsreenComponents=4`, 자동 중요도 최대 거리 30,000cm 는 탑다운에 맞지 않으므로 프로젝트 함수로 대체한다(engine-movement-anim-scale §6 표).
- VAT 는 CPU 애니 비용을 0 으로 만들지만 블렌딩이 없고 소켓 위치를 알 수 없다. 우리 판정은 캡슐·부채꼴 수학이라 소켓이 필요 없어 후열에 적합하다. 다만 AnimToTexture 는 Experimental 이다(AnimToTexture.uplugin:15).
- 마리당 CPU 스켈레탈 0.05~0.15ms 대 VAT 0.001~0.005ms(web-mass-monster-performance 결론 6, StraySpark 2026)가 단계 C 의 근거다.

---

## 8. Mass 를 지금 쓰지 않는 이유와 나중에 이관 가능한 구조

### 8.1 지금 쓰지 않는 이유

| 이유 | 근거 |
|---|---|
| 이득은 액터를 만들지 않을 때만 나온다. 우리는 L3 를 이미 무액터 SoA 로 두므로 Mass 가 추가로 주는 것은 아키타입 관리·병렬 실행기·표현 파이프라인뿐이다 | web-mass-monster-performance 결론 3, engine-mass-entity-ai 대안 비교 메모 |
| 결정론 함정: 엔티티 압축이 `FPlatformTime::Seconds()` 벽시계 예산으로 청크 간 엔티티를 옮긴다. `mass.EntityCompaction 0` 이 필수 | engine-mass-entity-ai 결론 5, MassSimulationSubsystem.cpp:263-282, MassEntityManager.cpp:586-612 |
| Epic: "결정론적인 것은 프로세서 실행 순서뿐". 5.8.0 에 `ParallelForEach` 강제 인라인 회귀, 5.8.1 수정 | web-ue-5-6-to-5-8-ai-changes 결론 2 |
| 게임플레이 스택(MassGameplay/MassAI/MassCrowd)은 5.8 에서도 `IsExperimentalVersion: true`. GAS 는 Mass 로 이식되지 않는다(Epic 2025) | engine-mass-entity-ai 결론 14, web-mass-monster-performance 결론 4 |
| 커맨드렛에서 `UMassAgentComponent` 는 등록을 거부하고, 페이즈 틱은 `OnWorldBeginPlay` 에서만 시작한다 | engine-mass-entity-ai 결론 15, MassAgentComponent.cpp:99-130 |
| 마리당 ASC 를 유지하는 D18 과 "엔티티당 ASC 없음" 커뮤니티 패턴이 충돌한다 | web-mass-monster-performance 결론 9 |

### 8.2 이관 가능한 SoA 모양

슬롯 배열 컨테이너의 이름은 02 §2 의 `FTDMonsterBrainSlot` 이며, 그 내부를 처음부터 "프래그먼트 하나 = 구조체 하나 = 배열 하나" 6종으로 자른다. 단계 D 는 각 배열을 `FMassFragment` 파생으로 바꾸고 `FMassEntityManager` 에 넣는 기계적 작업이 된다.

```cpp
struct FTDMonsterTransformFragment { FVector2f Position; FVector2f Velocity; float Yaw; float GroundZ; };
struct FTDMonsterBrainFragment { uint8 CurrentAction; uint8 FsmState; uint16 HoldSteps; uint64 NextThinkStep; };
struct FTDMonsterLodFragment { ETDMonsterLod Lod; uint8 ThinkPhase; uint8 PresentPhase; uint64 LastEngagedStep; };
struct FTDMonsterAttackFragment { uint8 TimetableIndex; uint16 TimetableStep; uint8 RingSlot; };
struct FTDMonsterHealthMirrorFragment { float Health; float MaxHealth; };
struct FTDMonsterBodyFragment { TWeakObjectPtr<AActor> Actor; uint16 Generation; };

// 02 §2 의 SoA 컨테이너. 프래그먼트 배열 6종 + 생존 비트가 전부이며, 스케줄러(§2)는 이 배열만 읽고 쓴다.
struct FTDMonsterBrainSlot
{
	TArray<FTDMonsterTransformFragment> Transform;
	TArray<FTDMonsterBrainFragment> Brain;
	TArray<FTDMonsterLodFragment> Lod;
	TArray<FTDMonsterAttackFragment> Attack;
	TArray<FTDMonsterHealthMirrorFragment> HealthMirror;
	TArray<FTDMonsterBodyFragment> Body;
	TBitArray<> bIsAlive;
	// AI 난수 스트림은 여기에 두지 않는다. 슬롯은 FTDCombatRandomStreams::GetBrainStream(SimulationId) 를 참조한다(04 §3.1, D29).
};
```

| 이관 규칙 | 내용 |
|---|---|
| 난수 스트림 단일 저장 | 몬스터별 AI 스트림의 저장소는 `UTDDamageSubsystem` 이 소유한 `FTDCombatRandomStreams::BrainStreams` 하나다(04 §3.1). 슬롯에 별도 배열을 두면 시드 상태가 둘로 갈리고 상태 해시(04 §7 은 `BrainStreams` 를 읽는다)와 어긋난다. Mass 이관 시에도 스트림은 프래그먼트가 아니라 서브시스템 소유로 남긴다 |
| 프래그먼트에 포인터·UObject 금지 | `Body` 만 예외(액터 껍데기). Mass 의 `FMassActorFragment` 에 1:1 대응 |
| 채널 = 프로세서 | think/move/judge/present 각각이 `UMassProcessor` 하나가 된다. 실행 순서는 `ExecutionOrder` 로 고정(engine-mass-entity-ai 결론 3) |
| 헤드리스 실행 경로 | `FMassEntityManager(nullptr)` + 자체 `FMassProcessingPhaseManager` 파생 + `TriggerPhase(Phase, 1/64)` 수동 호출 — 엔진 테스트가 쓰는 방식(engine-mass-entity-ai 결론 1·2, MassProcessingPhaseManager.cpp:378-388, MassTestTypes.cpp:200-210) |
| 시뮬 전제 CVar | `mass.EntityCompaction 0`, `mass.FullyParallel 0`, `mass.AllowQueryParallelFor 0`(D24), `-FixedSeed`(D26 커맨드렛 인자; engine-mass-entity-ai 결론 4·5·6) |
| 버전 | 5.8.1 이상(web-ue-5-6-to-5-8-ai-changes 결론 2) |
| 하지 않는 것 | MassAI/MassCrowd/ZoneGraph 의존, 엔티티당 ASC 동기화, 커맨드 버퍼 제출 순서 의존(engine-mass-entity-ai 피할 것 1·3·4·5) |

---

## 9. 단일 스레드 원칙과 병렬 옵트인 조건(D25)

| 항목 | 규칙 |
|---|---|
| 기본 | 게임·시뮬 모두 단일 스레드. 틱 함수는 `bRunOnAnyThread=false` |
| 시뮬 | 항상 단일 스레드. 실행 인자 `-onethread`, `tick.AllowAsyncComponentTicks 0`, `tick.AllowConcurrentTickQueue` 는 켜지 않는다(engine-determinism-headless 프로젝트 시사점 3) |
| 병렬 후보 | think 채널만. move·judge·present 는 슬롯 간 쓰기 의존(격자·자리 토큰·피해)이 있어 후보가 아니다 |
| 옵트인 조건 1 | Phase 1 실측에서 think 가 몬스터 몫의 30% 이상 또는 절대값 1.0ms 이상(추정 임계, [미결 4](#미결-사항사용자-결정-필요)) |
| 옵트인 조건 2 | 자동화 테스트 `TDGame.MonsterAI.ParallelHashEquivalence` 가 통과: 같은 시나리오를 `ParallelFor` 와 직렬로 각각 돌려 스텝별 상태 해시(D31)가 전부 같음 |
| 병렬 형태 | `ParallelFor(DueSlots.Num(), …)`. 슬롯은 자기 프래그먼트만 쓰고 이웃은 스텝 시작 스냅샷(읽기 전용)만 읽는다. 이웃 질의 결과는 `(거리², SimulationId)` 정렬로 스레드 수와 무관. AI 난수는 SimulationId 별 스트림 `FTDCombatRandomStreams::GetBrainStream(SimulationId)`(04 §3.1, D29)이라 슬롯 간 공유 상태가 없다(스트림 배열의 크기 확장은 병렬 구간 전에 끝낸다) |
| 로그 | 결정 로그(D34)는 슬롯별 버퍼에 쓰고 병렬 구간 뒤 SimulationId 순으로 합친다 |
| 게임에서만 | 시뮬 세션은 `bAllowParallelThink=false` 를 강제한다. 게임 값은 콘솔 변수 `TD.MonsterAI.ParallelThink`(기본 0, 접두는 02 의 `TD.MonsterAI.Reload` 와 통일). 옵트인 조건 2 의 테스트 이름은 07 MD-07 선택지 (a) 에도 같은 이름으로 적혀야 한다 |

병렬로 얻는 것은 think 0.1~0.2ms(추정)뿐이다. 심사가 지적했듯 "해시 비교 테스트가 있어도 시뮬만큼 확신할 수 없다"(심사 판정, B 감점 (c)). 그래서 순서를 "실측 병목 확인 → 테스트 → 게임 옵트인"으로 고정한다.

---

## 10. 탑다운 화면 안팎 판정

LOD 입력은 세 가지다: 가상 카메라 직사각형, 플레이어 2D 거리, 교전 플래그. 게임과 시뮬이 같은 함수를 쓰고 입력 출처만 다르다.

| 입력 | 게임 출처 | 시뮬 출처 | 계산 |
|---|---|---|---|
| 가상 카메라 직사각형 | `APlayerCameraManager` 의 뷰 프러스텀 네 모서리 광선을 플레이어 발 높이 평면(Z = 플레이어 Z)과 교차시켜 XY AABB(Axis-Aligned Bounding Box, 축 정렬 경계 상자)를 만들고 10% 확장. 32스텝(2Hz)마다 갱신, 스텝 시작 스냅샷에 저장 | 시나리오 `"virtual_camera": { "center": [x, y], "half_extent": [x, y], "actor_pool": N }`(예 `{ "center": [0,0], "half_extent": [1600, 900], "actor_pool": 400 }`, 04 §9.1 과 동일 형식). `center` 는 좌표만 허용하고 `"player"` 같은 문자열은 쓰지 않는다(시뮬의 플레이어 대리 위치는 시나리오가 정하므로 좌표로 적으면 된다). `actor_pool` 은 `virtual_camera` 안에 중첩된다. `null`/미지정이면 무한(전원 화면 안 = 전원 L0, D23) | `bIsOnScreen = Rect.Contains(Position2D)` |
| 플레이어 2D 거리 | 플레이어 슬롯 위치 | 동일 | `DistSq2D`, 임계 제곱과 비교(제곱근 없음) |
| 교전 플래그 | `LastEngagedStep`(피격·공격 시 갱신) | 동일 | `StepIndex - LastEngagedStep ≤ 192` |

- 탑다운은 카메라 거리가 거의 일정하므로 "거리"보다 "화면 안/밖 + 2D 거리 + 교전" 이 맞는 입력이다(engine-movement-anim-scale §6 주의점 3). 엔진의 `bRecentlyRendered`(1초 유예, SkinnedMeshComponent.cpp:1864) 는 렌더 스레드 결과라 결정론 입력으로 쓰지 않는다.
- 사각형(사다리꼴이 아닌 AABB)으로 근사하는 이유: 판정 비용이 비교 4회이고, 확장 10% 가 사다리꼴 모서리 오차를 덮는다(추정). 정확한 사다리꼴 판정은 필요해지면 교체한다.
- 플레이어가 여럿이 되는 일은 없다(싱글 플레이). 카메라 컷(텔레포트) 시 히스테리시스를 무시하고 즉시 재평가하되, 강등 금지 규칙은 유지한다.
- LOD 계산은 스텝 시작 스냅샷의 순수 함수이므로 시뮬에서도 같은 `virtual_camera` 입력이면 같은 LOD 열이 나온다. 결정 로그에 LOD 전환 이벤트 줄을 남긴다(D34).

---

## 미결 사항(사용자 결정 필요)

1. **L3 move 주기 완화 여부(결정 필요, 단계 B 진입 전)** — 기본은 모든 LOD 에서 매 스텝(D21). [§6.1](#61-1000마리단계-b-l0-40--l1-80--l2-280--l3-600) 재계산에서 1,000마리 추정 합계 8.5~10.3ms 가 목표 6.0ms 를 넘고, 초과분의 60% 가 L3 600마리의 move·분리 조향·공간 해시 몫이다. L3 이동을 8스텝마다로 완화하면 그 세 행의 L3 몫이 약 1/8 이 되어 합계 약 5.4~6.6ms(추정) 로 내려오지만, 시뮬 결과가 표현 설정(가상 카메라)에 종속된다. 선택지: (a) Phase 1 실측이 단가의 절반 이하면 완화 없이 유지, (b) 완화하되 "L3 는 플로우 필드 추종만, 공격 불가, 시나리오 입력에 완화 표 기록(D23)" 로 한정, (c) L3 를 무액터 SoA 에서도 이동 정지(휴면)로 두고 승격 시 위치 보정. 실측 전에는 (a) 가 기본이다.
2. **근접 자리 토큰(ring_slot) 의 자리 수·반경 기본값** — 종별 JSON 값(예 8자리, 150cm)으로 둘지, 전역 기본값으로 둘지. 조사에 근거가 없어 추정이다.
3. **정예·보스(≤10) 에 DetourCrowd 허용 여부** — B §4.4 는 허용, D17 은 잡몹만 금지. 허용하면 정예 경로에 내비 시스템 의존이 생기고 시뮬 몸과 이동이 달라진다(B단계 등가 테스트 대상 확대).
4. **think 병렬 옵트인 임계** — "몬스터 몫의 30% 또는 1.0ms" 는 추정이다. Phase 1 실측 뒤 확정.
5. **거리 임계 1,200 / 4,000cm 와 액터 풀 400** — 전부 B §4.2·§4.5 제안값. 맵 크기·카메라 높이가 정해지면 재조정. 풀 400 은 D24 의 값이지만 §6.1 분포 합(40+80+280)과 같아 여유가 0 이므로, 여유 20%(≈480, 추정)를 둘지 D24 개정과 함께 결정한다.
6. **present 주기 L2 = 6프레임, L1 = 1프레임** — L2 는 화면 밖이라 눈에 보이지 않지만, L2→L1 승격 직후 첫 프레임 위치 튐이 허용 범위인지 플레이테스트로 확인. L1 은 이 문서에서 1프레임(추정)으로 두었다. present 실측이 예산을 넘으면 L1 을 2프레임으로 되돌리되, 그 경우 "present 를 건너뛰는 프레임에는 이전 속도로 외삽한 트랜스폼을 쓴다" 를 함께 넣어야 한다(B §4.2 의 원래 전제).
7. **히치 프레임의 월드 델타 클램프** — [§1.5](#15-프레임당-스텝-상한-누적기게임-전용) 의 시계 불일치(커널 ≤ 4 × 1/64 = 62.5ms, GE 타이머는 월드 델타 전체)를 없애려면 게임의 `MaxUndilatedFrameTime` 을 4 × Step 으로 낮춰 월드 델타도 같이 클램프해야 한다. 그러면 히치 시 게임 전체가 슬로모션이 된다(플레이어 애니·물리 포함). 편차를 허용(D32 C 단계 감시)할지, 월드 델타를 클램프할지 결정이 필요하다.

## 근거 색인(인용한 조사 파일 목록)

| 조사 파일 | 인용 결론·절 |
|---|---|
| research/engine-movement-anim-scale.md | 결론 1·2·3·4·6·7·8·9·10·11, §1, §3-1, §3-4, §5 비용 모델, §6 정책 초안·주의점 |
| research/engine-behaviortree-tick.md | 결론 6·11 |
| research/engine-mass-entity-ai.md | 결론 1·2·3·4·5·6·9·14·15, §2-1, 대안 비교 메모, 피할 것 1·3·4·5 |
| research/web-mass-monster-performance.md | 결론 1·2·3·4·6·8·9·10, §2-3, §A, §B |
| research/web-ue-5-6-to-5-8-ai-changes.md | 결론 2 |
| research/web-ai-architecture-comparison.md | 결론 4·5(조기 종료 원칙만, 시간 수치 없음) |
| research/engine-determinism-headless.md | 결론 3(TickTaskManager.cpp:1170-1205 고우선 배열 선 해제), 프로젝트 시사점 3(`AddPrerequisite`) |
| research/engine-gas-determinism.md | 결론 1(GE 타이머는 `FTimerManager` 월드 시간)·4·9 |
| 엔진 직접 확인 | FloatingPawnMovement.cpp:23-64(`TickComponent` 의 `Controller && IsLocalController()` 게이트 37-38, `SafeMoveUpdatedComponent` 64); CharacterMovementComponent.cpp:5662·5686·6637 및 .h:502(`bRunPhysicsWithNoController` 컨트롤러 게이트); Character.cpp:125(`VisibilityBasedAnimTickOption = AlwaysTickPose`); AnimationBudgetAllocatorCVars.cpp:36-39(`ForceTickWhenComponentExitsOffScreen`); AnimationBudgetAllocator.cpp:440(위상 식); MassLODTickRateController.h:101-156(첫 틱 무작위 분산); SkeletalMeshComponent.cpp:1918-1927(`bExternalTickRateControlled`); HierarchicalHashGrid2D.h:35·125(클래스·생성자); TickTaskManager.cpp:1170-1205(고우선 배열 먼저 해제); 프로젝트 Source/TDGame/Combat/TDDamageEntity.cpp:13-14(틱 그룹 미지정) |
| 다른 최종 문서 | 02 §2(`FTDMonsterBrainSlot`)·규칙 2·15(`lod_periods`)·`TD.MonsterAI.Reload`; 04 §2.1(`bIsFixedStep`)·§3.1(`FTDCombatRandomStreams::BrainStreams`)·§9.1(`virtual_camera` 형식); 07 M3-02(`(거리², SimulationId)`)·M3-03(`Content/MonsterAI/PeriodTable.json`)·M3-04(4프레임 = 4스텝)·MD-07 |
| 결정 기록 | D6·D7·D14·D15·D16·D17·D18·D19·D21·D22·D23·D24·D25·D26·D27·D29·D30·D31·D32·D34·D38 |
| 설계안 | B §4.1~§4.6(§4.6 은 move·judge 를 LOD 불변으로 재계산), A §4.1~§4.4, C §4.1~§4.5, 심사 "판정 = C(이동·판정 스텝은 LOD 무관)" 및 B 감점 (c) |

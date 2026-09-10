# 웹: 언리얼 5 대량 몬스터(수백~수천) 전투 성능 사례와 Mass·틱·애니메이션 최적화 기법

조사 일자: 2026-09-09. 조사 방법: WebSearch 로 후보를 찾고 WebFetch 로 본문을 직접 열어 확인. 엔진 사실은 로컬 엔진(UE 5.8, `C:/Program Files/Epic Games/UE_5.8/Engine`)의 파일:줄 번호로 검증. 열지 못한 출처(403, DNS 실패, 동영상 본문)는 "미확인"으로 표기.

표기 규칙
- [W] = 웹 출처(URL·연도), [E] = 엔진 소스(파일:줄), [P] = 프로젝트 소스.
- 수치는 출처가 실측했다고 밝힌 것만 적음. 출처의 하드웨어·조건이 다르므로 절대값보다 비율과 경향으로 읽을 것.

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들)

1. **액터 1마리당 예산은 0.13~0.41ms(풀 AI 캐릭터 기준)로, 액터 기반 몬스터는 "동시 활성 100~300마리"가 현실적 상한이다.** 스켈레탈 메시+애님 블루프린트+CharacterMovementComponent(캐릭터 이동 컴포넌트)+비헤이비어 트리+캡슐 충돌을 모두 갖춘 캐릭터 1개가 프레임당 0.13~0.41ms(StraySpark, 2026). 100마리면 최악 41ms로 60fps 예산(16.67ms)을 넘는다. [W1]
2. **CharacterMovementComponent 와 Character 클래스를 버리고 "Pawn + 최소 충돌 + 자체 이동"으로 바꾸면 액터 기반으로도 1,000마리 ~60fps, 2,000마리 ~46fps 까지 갔고, 3,000마리에서 12~16fps 로 무너졌다(커뮤니티 실측, 2025).** 두 번째 충돌 스피어를 추가하자 1.5fps 로 급락 — **충돌·탐지는 물리 컴포넌트가 아니라 자체 공간 격자(spatial grid)로 해야 한다.** [W4]
3. **Mass 엔티티는 시뮬레이션 자체는 1만 마리를 4.6~9.1ms(GPU 인스턴싱 렌더 포함, StraySpark 2026)로 돌리지만, 엔티티를 액터로 되돌리는 "번역기(Translator)" 비용이 500마리에 ~3ms(게임 스레드)로 크다.** 커뮤니티 결론: 200~300마리 수준이면 액터가 Mass 보다 나을 수 있다(2025). **Mass 의 이득은 액터를 만들지 않을 때만 나온다.** [W1][W7]
4. **Epic 직원(James Keeling, 2025)의 공식 발언: "BehaviorTree 나 GAS 같은 것들은 Mass 로 이식되지 않을 가능성이 높다."** 또 Mass 내비메시 지원은 "매우 실험적/디버그 버전"이며, 플러그인 프로덕션 준비 시점의 ETA 는 없다. 로컬 5.8 에서도 `MassGameplay`, `MassAI`, `MassCrowd` 플러그인은 `IsExperimentalVersion: true` 다. **따라서 TDGame 이 이미 가진 GAS 전투(UTDCombatComponent 등)를 Mass 엔티티에 그대로 얹을 수는 없고, 다리(bridge)를 직접 짜야 한다.** [W20][E5][E6][E7]
5. **Mass 코어(MassEntity)는 5.8 에서 `Engine/Source/Runtime/MassEntity` 로 승격된 엔진 런타임 모듈이며, 처리 단계는 월드 틱 그룹에 1:1 로 묶여 있다(`TG_PrePhysics … TG_LastDemotable`).** 그러나 `FMassProcessingPhaseManager::TriggerPhase(Phase, DeltaTime, …)` 와 `Start(TSharedRef<FMassEntityManager>)`, `FMassEntityManager(UObject* InOwner = nullptr)` 가 공개되어 있어 **월드 틱 없이 수동으로 단계 실행이 가능하다.** 결정론 시뮬레이터에서 활용 가능하나, `mass.FullyParallel`(기본값 `!UE_SERVER` = 클라이언트 빌드에서 켜짐)과 `mass.AllowQueryParallelFor`(기본 true)를 끄지 않으면 병렬 실행 순서가 결과에 섞일 수 있다. [E1][E2][E3][E4]
6. **애니메이션이 진짜 병목이다.** CPU 스켈레탈 애니메이션은 마리당 0.05~0.15ms, GPU 인스턴싱(버텍스 애니메이션 텍스처, VAT)은 0.001~0.005ms(StraySpark 2026). City Sample 은 가까이는 스켈레탈, 멀리는 AnimToTexture 로 구운 ISM(Instanced Static Mesh) 을 쓴다(2023). Animation Budget Allocator 는 게임 스레드 애니메이션 예산을 `a.Budget.BudgetMs`(기본 1.0ms)로 고정하고 초과분을 자동으로 보간·저품질로 낮춘다. [W1][W16][E8]
7. **탑다운 ARPG 상용작은 "몬스터를 줄이고 보상을 올리는" 쪽으로 성능을 확보했다.** Path of Exile 2 패치 0.4.0(2025-12): 코어 시스템을 멀티코어로 재작성해 CPU 바운드에서 fps 25% 이상 향상, 동시에 엔드게임 몬스터 수를 줄이고 마리당 경험치·드랍·체력 +40%. Diablo IV 시즌 11(2025): 몬스터 행동을 "고정 쿨다운 순환"에서 "공격 속도 기반 + 무작위 행동 선택"으로 재작성, 인지 범위는 종류별로 다르게(언데드는 근접해야 반응). **AI 갱신 주기·화면 밖 처리에 대한 직접 발언은 미확인.** [W21][W22]
8. **내비게이션: Detour Crowd 는 `MaxAgents` 기본 50(엔진 소스)이며 고정 상한 방식.** 플로우 필드(Flow Field) 구현(UE 5.3.2 기준)은 200 유닛·단순 지도에서 1.6ms 대 기본 내비 6.3ms(3.9배 빠름)지만, 미로형 지도에서는 6ms 대 5.1ms 로 오히려 느렸다. **"다수 유닛이 하나의 목표(플레이어)로 몰리는" 탑다운 ARPG 는 플로우 필드가 유리한 전형이다.** Mass 의 기본 내비게이션은 내비메시 경로가 아니라 "MoveTarget 으로 직선 조향 + 장애물 회피"이고, Off LOD 엔티티는 목표로 순간이동시킨다(5.5 기준). [E9][W10][W11]
9. **Mass + GAS 커뮤니티 사례는 "엔티티는 경량 체력 프래그먼트, 플레이어(액터)만 ASC" 패턴 하나로 수렴한다.** DigiLogicLabs Mass Units 플러그인(UE 5.7.2 검증): 엔티티에 Health/MaxHealth/데미지/사거리/쿨다운 프래그먼트, GAS 대상은 "액터의 ASC 를 찾아 GameplayEffect 적용", 엔티티당 ASC 없음, 최대 유닛 10,000, 갱신 예산 라운드로빈 500/프레임. [W12]
10. **단계별 선택 기준(제안):** 동시 활성 몬스터 ≤300 → 액터 + 수동 틱 매니저(Significance 기반 주기 분배, Pawn 기반, 자체 공간 격자). 300~1,000 → 액터 유지하되 원거리 개체는 "무액터 경량 상태"(데이터 배열)로 강등하고 근거리만 액터 승격(하이브리드). ≥1,000 또는 "한 화면에 수백이 동시에 전투" → Mass 엔티티 + ISM/VAT 표현 + 자체 전투 프래그먼트, GAS 는 플레이어·엘리트·보스 액터에만.

---

## 상세 조사

### 1) 언리얼 5 에서 수백~수천 적을 다룬 사례

#### 1-1. 근거 표

| # | 사례 | 접근 | 마릿수 | 프레임 비용 / fps | 하드웨어·조건 | 출처(연도) |
|---|---|---|---|---|---|---|
| W1 | StraySpark "Building Crowds and Traffic in UE5: Mass AI and 10,000 NPCs at 60fps" | Mass + 4단계 LOD(스켈레탈 0~25m / VAT 정적 25~60m / ISM 60~150m / 임포스터 150m+) + ZoneGraph + MassAvoidance | 1,000 / 5,000 / 10,000 / 20,000 | 1.2 / 2.8 / 4.6 / 8.2 ms (RTX 4080), 2.1 / 5.2 / 9.1 / 16.5 ms (PS5/XSX) | 도시 환경, 1920×1080 | https://www.strayspark.studio/blog/crowd-traffic-simulation-ue5-mass-ai (2026-03-25) |
| W1 | 같은 글, 표준 액터 AI 캐릭터 1개 | 스켈레탈+ABP+CMC+BT+캡슐 | 1 | 0.13~0.41 ms (애님 0.05~0.15, BT 0.02~0.08, 이동 0.03~0.10, 렌더 0.02~0.05, 물리 0.01~0.03) | — | 같음 |
| W2 | 80.lv "Simulating 5,000 Zombies Using Mass In UE5's ECS" (개발자 Faisal) | Mass + 내비메시 기반 이동 + 인지 + 중력 + 회피 | 5,000 | 100 fps (출시 빌드) | i5-11600K, Radeon 7800 XT | https://80.lv/articles/simulating-5-000-zombies-using-mass-in-unreal-engine-5 (2025) |
| W3 | Epic 포럼 "Mass 5.5, Mass Entity, thousands of units" | Mass + 시각화 트레이트(원거리 정적 메시 전환) | 목표 2,000+, 실제 ~1,000 에서 프레임 저하 | 수치 없음(스폰 시 스파이크, 팝핑) | 5.5 | https://forums.unrealengine.com/t/mass-5-5-mass-entity-thousands-of-units/2487745 (2025-05) |
| W4 | Epic 포럼 "Managing MASSIVE enemy crowds with Ai" | **액터(Pawn, CMC 없음)** + 자체 Crowd Manager 컨트롤러 + 단일 재질·그림자 끔 + 틱 최소화 + 4정점 큐브 충돌 + BP 로직(BT 대신) | 1,000 / 2,000 / 3,000 | ~60 / ~46 / 12~16 fps; 3,000 에 충돌 스피어 1개 추가 시 1.5 fps; BT→자체 BP 로직 전환 시 22~24 → 26~28 fps | 미기재 | https://forums.unrealengine.com/t/managing-massive-enemy-crowds-with-ai/2491440 (2025-05, 후속 2026-06) |
| W5 | Days Gone (Bend Studio, UE4) | 액터 기반 무리(horde) | 바닐라 50~500, 모드 최대 670 | 수치 없음 | PS4/PC | https://www.dsogaming.com/mods/days-gone-mod-makes-hordes-more-challenging-with-up-to-600-zombies-on-screen/ (2021) |
| W6 | City Sample 군중 | Mass + ZoneGraph + 근거리 스켈레탈(ABP_Crowd_C) / 원거리 ISM + AnimToTexture 구운 애니메이션(164 정적 메시, 155 데이터 에셋), 액터 없음 | 미기재 | 미기재 | — | https://vrealmatic.com/unreal-engine/city-sample/crowd (2023-10-24) |
| W7 | Epic 포럼 "Synchronising and updating actor transforms in Mass Entity performance" | Mass + 액터 표현(캡슐·스켈레탈 동기화) | 500 (후속 200~300) | `MassTransformToActorCapsuleTranslator` 에 게임 스레드 ~3ms; `SetBodyTransform` 은 게임 스레드 전용이라 병렬화 불가 | 5.5.4 (5.6 언급) | https://forums.unrealengine.com/t/synchronising-and-updating-actor-transforms-in-mass-entity-performance/2573360 (2025) |
| W8 | UE 5.8 릴리스 노트 "Mass Framework: Faster, Modular, and Built for Scale" | Mass Signals 코어 엔진 편입, 게임 스레드 밖 엔티티 생성, 희소/가상 프래그먼트, 프로세서 실행·의존성 해석 전면 개편(멀티코어), MassCore 모듈 분리; MetaHuman Crowd(실험) = Instanced Skeletal Mesh 로 "수십~수천" | — | 수치 없음 | 5.8 | https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes (2026) |
| W9 | StraySpark "MetaHuman Crowd in UE 5.8" | MetaHuman Crowd (배경 군중 전용, 게임플레이 NPC 아님), Mass 를 보완 | — | 표준 AI 캐릭터 0.13~0.41ms 재인용 | 5.8 (2026-06-17 출시) | https://www.strayspark.studio/blog/metahuman-crowd-ue5-8-guide (2026) |

**'Mass Entity production ready 5.7' 여부**: 5.7 공식 릴리스 노트/뉴스 본문은 WebFetch 가 목차만 반환하거나 403 이라 **미확인**. 확인된 사실만 적으면 (a) x157 개발 노트: "5.0 도입, 5.2 에서 production-ready 표기, 5.8 현재도 활발히 변경 중, API 가 5.2 이후 상당히 바뀜"(https://x157.github.io/UE5/Mass/ , 2025~2026). (b) Epic 공개 로드맵에 "MassEntity (Beta)"(c/862)와 "MassEntity (Experimental)"(c/507), "Mass Entity Builder (Experimental)"(c/1928) 항목이 존재(본문은 403 으로 미확인). (c) **로컬 5.8 엔진에서 코어는 런타임 모듈로 승격됐지만, 게임플레이 계층 플러그인 3종은 모두 실험(Experimental) 플래그다** — 아래 [E5][E6][E7]. (d) Epic 직원 Keeling(2025): "플러그인이 언제 production-ready 가 될지 ETA 를 알지 못한다." [W20]

#### 1-2. 엔진 근거 발췌

[E1] Mass 처리 단계 ↔ 월드 틱 그룹 매핑 (`Engine/Source/Runtime/MassEntity/Private/MassProcessingPhaseManager.cpp:46-51`)
```cpp
ETickingGroup::TG_PrePhysics,    // EMassProcessingPhase::PrePhysics
ETickingGroup::TG_StartPhysics,  // EMassProcessingPhase::StartPhysics
ETickingGroup::TG_DuringPhysics, // EMassProcessingPhase::DuringPhysics
ETickingGroup::TG_EndPhysics,    // EMassProcessingPhase::EndPhysics
ETickingGroup::TG_PostPhysics,   // EMassProcessingPhase::PostPhysics
ETickingGroup::TG_LastDemotable, // EMassProcessingPhase::FrameEnd
```

[E2] 수동 단계 실행 API (`Engine/Source/Runtime/MassEntity/Public/MassProcessingPhaseManager.h:201,207,212,213`)
```cpp
MASSENTITY_API const FGraphEventRef& TriggerPhase(const EMassProcessingPhase Phase, const float DeltaTime, const FGraphEventRef& MyCompletionGraphEvent /*...*/);
MASSENTITY_API void Start(UWorld& World);
MASSENTITY_API void Start(const TSharedRef<FMassEntityManager>& InEntityManager);
MASSENTITY_API void Stop();
```
`Engine/Source/Runtime/MassEntity/Public/MassEntityManager.h:138`: `UE_API explicit FMassEntityManager(UObject* InOwner = nullptr);` — 엔티티 매니저는 월드 없이도 생성 가능.

[E3] 병렬 실행 스위치 (`MassProcessingPhaseManager.cpp:30-36`, `MassProcessingTypes.h:14`, `MassEntityQuery.cpp:26-31`)
```cpp
#define MASS_DO_PARALLEL !UE_SERVER
bool bFullyParallel = MASS_DO_PARALLEL;
{TEXT("mass.FullyParallel"), bFullyParallel, TEXT("Enables mass processing distribution to all available thread (via the task graph)")},
bool bAllowParallelExecution = true;
{ TEXT("mass.AllowQueryParallelFor"), bAllowParallelExecution, TEXT("Controls whether EntityQueries are allowed to utilize ParallelFor construct"), ECVF_Cheat }
```

[E4] 쿼리 병렬 API 와 게임 스레드 강제 플래그 (`MassEntityQuery.h:113`, `MassProcessor.h:259`)
```cpp
UE_API void ParallelForEachEntityChunk(FMassExecutionContext& ExecutionContext /*...*/);
uint8 bRequiresGameThreadExecution : 1 = false;
```
참고: 2023년 포럼(https://forums.unrealengine.com/t/mass-entity-processors-not-processing-in-parallel/1297162)은 "5.1 에서 `ParallelForEachEntityChunk` 가 제거되어 쿼리는 항상 직렬"이라고 보고했으나, **5.8 소스에는 복원되어 있다.** 오래된 커뮤니티 글의 병렬성 결론은 그대로 믿지 말 것.

[E5][E6][E7] 플러그인 상태 플래그
- `Engine/Plugins/Runtime/MassGameplay/MassGameplay.uplugin:15-16` → `"IsBetaVersion": false, "IsExperimentalVersion": true`
- `Engine/Plugins/AI/MassAI/MassAI.uplugin:15-16` → 동일(실험)
- `Engine/Plugins/AI/MassCrowd/MassCrowd.uplugin:15-16` → 동일(실험)
- 반면 `Engine/Plugins/Runtime/AnimationBudgetAllocator/AnimationBudgetAllocator.uplugin:24`, `Engine/Plugins/Runtime/SignificanceManager/SignificanceManager.uplugin:24` → `"IsExperimentalVersion": false`

#### 1-3. 사례 해석

- 액터 기반 상한은 "무엇을 떼어내느냐"로 정해진다. W4 는 CMC·Character·BT·물리 충돌을 다 떼고서야 1,000 마리 60fps 를 얻었다. 이는 W1 의 "마리당 0.13~0.41ms" 중 이동(0.03~0.10)·BT(0.02~0.08)·물리(0.01~0.03)를 없앤 결과와 정합한다.
- Mass 는 "시뮬레이션+인스턴싱 렌더"일 때만 1만 마리급이다. 액터로 되돌리는 순간(W7) 게임 스레드 병목(물리 바디 트랜스폼)이 돌아온다. W3 의 "1,000 에서 프레임 저하"도 시각화 트레이트를 액터 블루프린트로 둔 탓이었고, 정적 메시 인스턴싱으로 바꾸자 해결됐다.
- 5.8 의 Mass 개편(W8)은 "게임 스레드 밖 엔티티 생성, 프로세서 의존성 해석 개편"으로 W3 이 겪은 스폰 스파이크 문제를 직접 겨냥한다. 다만 릴리스 노트에 수치는 없다.

### 2) 액터 기반 최적화 기법과 벤치마크

#### 2-1. 근거 표

| 기법 | 핵심 사실 | 출처 |
|---|---|---|
| 틱 간격 | 액터·컴포넌트는 "매 프레임, 최소 간격, 또는 아예 안 함"으로 틱 설정 가능(`SetActorTickInterval`). 1,000 액터 벤치마크 수치는 **미확인**. | https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine (5.7 문서) |
| Significance Manager | `Update` 는 자동으로 돌지 않으며 "대부분 매 프레임 한 번" 직접 호출(예: `UGameViewportClient` 오버라이드). Post-significance 콜백은 Concurrent(스레드 안전 필요, 병렬) / Sequential(정렬 순서로 순차) 두 모드. 성능 자체를 올리는 게 아니라 "AI 갱신 빈도 낮추기" 같은 프로젝트 정책의 뼈대. | https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine (5.8 문서) ; [E10] |
| Animation Budget Allocator | 게임 스레드 애니메이션 예산을 ms 로 고정(`a.Budget.BudgetMs` 기본 1.0). 모드: Hi / Lo / Interpolating. 메시 컴포넌트를 `SkeletalMeshComponentBudgeted` 로 바꿔야 함. `SetComponentSignificance()` 로 우선순위 지정 → 틱 레이트 동적 제어. | https://dev.epicgames.com/documentation/unreal-engine/animation-budget-allocator-in-unreal-engine (5.8 문서) ; [E8] |
| Animation Sharing 플러그인 | 리더 메시 1개가 애니메이션을 평가해 버킷의 자식들에 포즈 전달. "100 캐릭터와 1000 캐릭터의 차이가 최소 비용 증가". 블렌드마다 인스턴스 생성 비용, BP 상태 프로세서는 네이티브보다 느림. | https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sharing-plugin-in-unreal-engine (5.8 문서) |
| AnimToTexture / VAT | 스켈레탈 메시+애니메이션 세트 → 정적 메시+텍스처. City Sample 에서 출발, 5.1 부터 엔진 동봉, 5.4 에서 플러그인 변경·문서 부족(커뮤니티 튜토리얼 검색 요약; 본문은 제목만 반환되어 **부분 미확인**). GPU 인스턴싱 애니메이션 비용 0.001~0.005ms/마리(W1). | https://dev.epicgames.com/community/learning/tutorials/3xKm/unreal-engine-animtotexture-plugin-how-to-use-it-to-make-vertex-animation-textures-for-crowds ; W1 (2026) |
| 인스턴스 메시 | Mass 기본 시각화가 ISM 이며 "정적 메시면 수천을 무리 없이 처리"(커뮤니티 발언). HISM 을 컴포넌트에 잘못 쓰면 팝핑 발생(W3). | W3 (2025) |
| 내비 회피 끄기 / Detour Crowd 상한 | Detour Crowd 는 프로젝트 설정에 고정 최대 에이전트 수. RVO 는 CMC 내장, 내비메시 미사용이라 이탈 가능. 둘 중 하나만 써야 함. | https://dev.epicgames.com/documentation/unreal-engine/using-avoidance-with-the-navigation-system-in-unreal-engine (5.8 문서) ; [E9] |
| Mover 2.0 vs CMC | 5.4 실험 도입, 5.7 "late-experimental / beta". 모듈형 데이터 지향 이동. **마리당 비용 비교 수치는 없음.** 커뮤니티는 "Mover + 단순 Pawn" 으로 군중 최적화 시도(W4 후속). | https://www.strayspark.studio/blog/mover-2-0-vs-character-movement-component-ue5-7-2026 (2026) ; W4 |
| 거리별 AI LOD | Mass LOD 는 High/Medium/Low/Off 4단계, 시각화·시뮬레이션·복제 각각 별도 LOD 계산, "거리뿐 아니라 가시성"도 반영. | https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-gameplay-in-unreal-engine (5.8 문서) |

#### 2-2. 엔진 근거 발췌

[E8] `Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Private/AnimationBudgetAllocatorCVars.cpp:195-198`
```cpp
TEXT("a.Budget.BudgetMs"),
GBudgetParameters.BudgetInMs,
TEXT("Values > 0.1, Default = 1.0\n")
TEXT("The time in milliseconds that we allocate for skeletal mesh work to be performed.\n")
```

[E9] `Engine/Source/Runtime/AIModule/Classes/Navigation/CrowdManager.h:294` `int32 MaxAgents;` / `Private/Navigation/CrowdManager.cpp:168` `MaxAgents = 50;`

[E10] `Engine/Plugins/Runtime/SignificanceManager/Source/SignificanceManager/Public/SignificanceManager.h:36,118,121`
```cpp
typedef TFunction<void(UObject*, float, float, bool)> FPostSignificanceFunction;
UE_API virtual void Update(TArrayView<const FTransform> Viewpoints);
UE_API virtual void RegisterObject(UObject* Object, FName Tag, FManagedObjectSignificanceFunction SignificanceFunction,
    EPostSignificanceType InPostSignificanceType = EPostSignificanceType::None, FManagedObjectPostSignificanceFunction InPostSignificanceFunction = nullptr);
```

#### 2-3. 커뮤니티 벤치마크 정리 (CharacterMovementComponent 1개당 비용 등)

- CMC 단독 비용의 정밀 벤치마크는 **미확인**. 확인된 것은 W1 의 분해치(이동/길찾기 0.03~0.10ms/마리, 2026)와 W4 의 "CMC·Character 를 버린 Pawn 으로 1,000 마리 60fps"(2025)뿐이다.
- W4 의 가장 중요한 교훈은 **"충돌 컴포넌트 1개 추가로 16fps → 1.5fps"**. 즉 3,000 마리 규모에서 물리 충돌 기반 탐지는 불가능하고, 탐지·피격 판정은 자체 격자/브로드페이즈로 해야 한다.
- BT(비헤이비어 트리)를 자체 BP 로직으로 바꾸자 22~24 → 26~28fps(W4). BT 자체의 오버헤드가 수천 마리에서 측정 가능한 수준임을 시사(C++ 규칙 기반이면 더 낮아질 것으로 보이나 **수치 미확인**).

### 3) 탑다운 ARPG 상용작의 몬스터 밀도·AI 갱신·화면 밖 처리

| 게임 | 확인된 발언·사실 | 출처(연도) |
|---|---|---|
| Path of Exile 2 (GGG) | 패치 0.4.0(2025-12-12): "코어 시스템 여러 개를 재작성해 멀티코어에 작업 분산", CPU 바운드에서 fps 최소 25%↑, 프레임 스파이크 감소, 콘솔에서 특히 효과. 동시에 엔드게임 인카운터 몬스터 수 감소 + 마리당 경험치/드랍/체력 +40%. | https://www.poe-vault.com/poe2/news/poe2-major-performance-upgrade-patch-0-4-0 (2025) |
| Path of Exile 2 | "몬스터 속도와 무리 밀도는 검토 중" (Zizaran 인터뷰 대응). 화면 밖 AI 갱신 주기 관련 발언 **미확인**. | https://www.poe-vault.com/poe2/news/poe2-ggg-addresses-zizaran-interview-and-community-feedback (2025, 검색 요약) |
| Diablo IV (Blizzard) | 시즌 11(2025): 출시 후 거의 변하지 않았던 몬스터 행동을 전면 재작성. "고정 쿨다운 순환 대신 공격 속도와 무작위 행동 선택에 따라 행동". 엘리트/챔피언은 체력·행동 옵션 확대, 일반 몹은 빠르게 유지. 인지 범위는 종류별(언데드는 근접해야 반응). 엔진 갱신 주기·화면 밖 처리 발언 **미확인**. | https://www.icy-veins.com/d4/news/diablo-4-devs-talk-future-plans-season-11-and-the-games-true-identity/ (2025) |
| Diablo IV | 시즌 1 전 "엔드게임 몬스터·엘리트 밀도 증가 작업 중" | https://www.icy-veins.com/d4/news/blizzard-working-on-monster-density-increases-for-diablo-4-season-1/ (2023, 검색 요약) |
| Last Epoch (Unity 6) | HP 성능 가이드 수준의 정보만 존재(밀집 전투에서 CPU/GPU 부하). 개발자 AI 발언 **미확인**. | https://www.hp.com/us-en/shop/tech-takes/last-epoch-performance-guide-omen (2025~2026) |
| Hades II (Supergiant) | "게임이 곧 설계 문서", 반복 개발 스튜디오라는 발언만 확인. 적 수·AI 갱신 발언 **미확인**. | https://www.gamesradar.com/games/hades/the-game-is-the-design-document-hades-2-devs-dont-have-long-elaborate-plans-... (2025) |
| Days Gone (Bend, UE4) | 무리 50~500 마리(모드 670). GDC 2018 세션: 스폰 방식, 분대 형성, 전선(Front Line) 계산, 확신도(Confidence) 기반 역할 배정. 애니메이션 공유 세부는 기사에 없음. 커뮤니티 발언(W3): "10~20 마리 풀에 리더 1 이 애니메이션 공유" — **1차 출처 미확인**. | https://gamerant.com/days-gone-zombie-ai-gdc/ (2018) ; W5 (2021) |

해석: 상용 ARPG 가 공개한 것은 "행동 규칙(공격 속도·무작위 선택·종류별 인지 범위)"과 "밀도-보상 트레이드오프"이지 내부 틱 주기가 아니다. TDGame 은 (a) 규칙 기반 행동에 확률·공격 속도 파라미터를 두는 설계, (b) 밀도를 낮추고 마리당 가치를 올리는 밸런스 레버를 시뮬레이터 입력으로 넣는 설계, 두 가지를 직접 채택할 수 있다.

### 4) 내비게이션: 플로우 필드/벡터 필드/단순 조향 vs 내비메시+Detour Crowd

| 항목 | 사실 | 출처 |
|---|---|---|
| Detour Crowd 상한 | `MaxAgents` 기본 50, 프로젝트 설정에서 상향 가능. "고정 최대 에이전트 수" 방식. | [E9] ; 5.8 회피 문서 |
| 플로우 타일(Flow-tile) UE 구현 | Elijah Emerson(Supreme Commander) 방식. UE 5.3.2, i7-13700H. 단순 지도: 1유닛 991.6µs(기본 342.2µs), 50유닛 982.5µs(기본 2.1ms), 200유닛 1.6ms(기본 6.3ms, 3.9배). 미로 지도: 200유닛 6ms(기본 5.1ms, 더 느림). `ConvertFlowTilesToPath` 로 `PathFollowingComponent` 와 연결. 최종 갱신 연도 **미확인**. | https://github.com/yoreei/crowd_pathfinder |
| Mass 기본 내비 | `FMassMoveTargetFragment` 를 주면 "직선으로 목표를 향해 조향하며 장애물 회피"; `UMassObstacleAvoidanceTrait` 는 2D 장애물 격자 사용; **Off LOD 는 이동 시간 무시하고 순간이동**; 5.5 기준 "문서 부족, 대규모 개발 중". | https://x157.github.io/UE5/Mass/Navigation.html (5.5) |
| Mass 내비메시 | Epic Keeling(2025): "매우 실험적/디버그 버전의 내비메시 지원이 엔진에 있다." | W20 |
| Mass 그룹 경로 공유 | Mass Units 플러그인: "엔티티당 경로 질의 대신 그룹당 내비메시 회랑(corridor) 1개" 공유로 요청 수 절감. | W12 |
| 플로우 필드 서바이버(Unity, 학사 논문) | "대량 적에 플로우 필드 적용 가능성" 시연. 수치 없음. 엔진이 Unity 라 참고만. | https://nicolasbrueckner.itch.io/flow-field-survivors (연도 미기재) |
| Detour Crowd 이슈 | 플레이어 회피 문제와 해결(2025). 에이전트 수 성능 수치 없음. 본문 **403 미확인**. | https://www.stevestreeting.com/2025/03/13/agent-player-avoidance-in-ue/ |

해석: 탑다운 ARPG 몬스터의 목표는 거의 항상 "플레이어 1명(또는 소수 목표)"이므로, "한 목표로 다수 유닛"이라는 플로우 필드 최적 조건에 정확히 부합한다. 던전이 미로형이면 플로우 필드 계산 비용이 오르지만, 그래도 "유닛 수에 무관하게 목표당 1회 계산"이라는 성질은 유지된다(위 표에서 200유닛 미로 6ms 는 유닛 수가 늘어도 거의 고정). 반면 내비메시는 유닛 수에 비례해 질의 비용이 는다.

### 5) Mass 와 GAS 를 함께 쓰는 커뮤니티 사례

| 항목 | 사실 | 출처 |
|---|---|---|
| Epic 공식 입장 | "BehaviorTree 나 GAS 같은 것들은 Mass 로 이식되지 않을 가능성이 높다." (James Keeling, 2025). 5.6 에서 MassStateTree 성능 개선(게임 스레드 밖 처리). 직렬화·복제는 향후 과제. | https://forums.unrealengine.com/t/mass-entity-roadmap-vision-and-more-questions/2527030 (2025) [W20] |
| UE6 방향 | Epic(ZhiKangShao, 2026-06): "GAS 는 액터 컴포넌트와 블루프린트에 강하게 묶여 있다", UE6 에서는 GAS 대체 시스템 개발 중이며 GAS 는 결국 폐기 예정; **Mover 와 Mass 는 UE6 미래 스택의 일부**. | https://forums.unrealengine.com/t/gameplay-ability-system-gas-future-in-unreal-engine-6/2738426 (2026) |
| Mass Units 플러그인 (DigiLogicLabs) | UE 5.7.2 검증. 엔티티에 Health/MaxHealth/데미지/사거리/쿨다운 프래그먼트. "GAS 플레이어 대상이면 Gameplay Effect To Target 지정 → 대상 액터의 ASC 를 찾아 권한 측에서 적용". "승격되지 않은 Mass 유닛은 네이티브 체력이 진실의 원천". 최대 유닛 10,000, 프레임당 군중 갱신 500(라운드로빈), 스켈레탈 풀 기본 100마리/300cm, ISM 인스턴스에 8개 float(애님 인덱스, 시간, LOD, 팀, 색, 체력%). | https://github.com/DigiLogicLabs/ue5-mass-units-plugin [W12] |
| "중앙 액터에 ASC 모으기" 아이디어 | W7 스레드 답글: "모든 GAS 컴포넌트를 담는 중앙 액터 하나를 두고 각 컴포넌트를 엔티티에 매핑". 구현·수치 없음. | W7 (2025) |
| GAS 를 대량 액터에 쓰는 문제 | "피해를 입을 수 있는 모든 액터에 ASC 를 요구하는 것은 무거운 접근". 결론: 복잡한 액터만 GAS, 단순 파괴물은 경량 체력 컴포넌트. | https://forums.unrealengine.com/t/using-gas-exclusively-for-damage-health-in-larger-scale-game/1270092 (2023~2024) |
| Niagara 를 적으로 쓰기 | 2022 스레드: 파티클별 체력·상호작용은 "어떻게 할지 모르겠다", BP 로 감싸는 우회만 제시. 최신 Niagara Data Channels 기반 사례는 **미확인**. | https://forums.unrealengine.com/t/is-this-possible-with-niagara-particles-as-enemies/619028 (2022) |

해석: 커뮤니티·Epic 모두 "엔티티마다 ASC" 를 부정한다. 실제 동작하는 유일한 패턴은 **엔티티 측 경량 전투 프래그먼트 + 액터(플레이어·엘리트) 측 GAS + 경계에서 GameplayEffect 로 변환**이다. TDGame 은 이미 `UTDDamageSubsystem`/`UTDDamageDefinition`(DataAsset) 으로 데미지 정의를 데이터화해 두었으므로[P], 같은 정의를 "액터 경로(GAS)"와 "엔티티 경로(프래그먼트 직접 연산)" 두 실행기가 공유하는 구조가 자연스럽다.

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

### A. 단계 선택 기준 (액터+수동 틱 매니저 vs Mass 엔티티)

| 조건 | 선택 | 근거 |
|---|---|---|
| 동시 활성 몬스터 ≤ 300, 전원 GAS 능력치 필요 | **액터 + 수동 틱 매니저** | W7: 200~300 에서는 액터가 Mass(번역기 포함)보다 나을 수 있음. W1: 액터 1마리 0.13~0.41ms → 300마리 최악 123ms 이므로 반드시 아래 B 항목의 감량 필요. |
| 300~1,000, 화면 안 전투는 수십~백 단위 | **하이브리드**: 근거리·전투 중 개체만 액터(GAS), 원거리는 액터 없는 경량 상태(구조체 배열) | W1 "가장 효과적인 최적화 = 스켈레탈 메시 개체 수 제한(카메라 30m 내 20마리면 총 수와 무관하게 고정 비용)". W4 3,000 마리 한계. |
| ≥ 1,000 또는 서바이버류 밀도 | **Mass 엔티티 + ISM/VAT + 자체 전투 프래그먼트**, GAS 는 플레이어·엘리트·보스만 | W1 1만 마리 4.6ms, W2 5,000 마리 100fps, W12 패턴. Epic: GAS 는 Mass 로 안 옮겨짐(W20). |

TDGame 은 "한 화면에 많은 몬스터"이지만 서바이버류가 아닌 탑다운 ARPG 이므로 **2행(하이브리드)을 기본 목표로, 1행에서 시작**하는 것을 제안한다. 단, 시작 단계부터 몬스터 상태를 "액터에 붙은 컴포넌트"가 아니라 **매니저가 소유한 연속 배열(SoA)** 로 두고 액터는 표현·충돌 껍데기로만 쓰면, 3행(Mass)으로 옮길 때 프래그먼트로 1:1 이식이 된다.

### B. 액터 기반 단계에서 반드시 할 것 (W1·W4 근거)

1. `ACharacter`/`UCharacterMovementComponent` 를 몬스터에 쓰지 않는다. `APawn` + 자체 이동(또는 Mover 2.0 은 5.7 베타이므로 관찰만). [W4][Mover 블로그 2026]
2. 몬스터 간 탐지·피격 판정에 물리 충돌 컴포넌트를 쓰지 않는다. 매니저가 소유한 2D 격자(해시)로 브로드페이즈. W4 의 "충돌 스피어 1개 추가 → 1.5fps" 가 근거.
3. 비헤이비어 트리 대신 C++ 규칙/유틸리티 평가기를 매니저가 배치(batch) 실행. W4 의 BT→자체 로직 fps 상승과 사용자 요구(코드 정의, 수동 틱)와 정합. 액터 `Tick` 은 끄고 매니저 1개만 틱.
4. Significance Manager 로 거리·가시성 기반 갱신 주기(예: 근거리 매 프레임, 중거리 4프레임, 화면 밖 16프레임 + 위치만 외삽)를 정하고, `Update` 는 매 프레임 1회 직접 호출. [E10]
5. Animation Budget Allocator 를 켜고 `a.Budget.BudgetMs` 를 플랫폼별로 고정, 몬스터 메시는 `SkeletalMeshComponentBudgeted`. [E8]
6. 동일 종류 몬스터가 많으면 Animation Sharing 플러그인(리더 포즈 공유) 또는 원거리 VAT 전환을 검토. 블렌드마다 비용이 붙으므로 "상태 버킷" 수를 제한.

### C. 결정론 시뮬레이터와의 관계

- 액터 기반 매니저는 `FTDScopedCombatWorld`(고정 스텝 `World->Tick`)[P] 위에서 매니저 순서만 고정하면 결정론을 지키기 쉽다.
- Mass 로 가더라도 `FMassProcessingPhaseManager::TriggerPhase` 와 `Start(TSharedRef<FMassEntityManager>)` [E2] 로 수동 단계 실행이 가능하다. 단 헤드리스 결정론 실행 시 `mass.FullyParallel=0`, `mass.AllowQueryParallelFor=0` 으로 병렬 순서 비결정성을 제거하고[E3], 프로세서 실행 순서를 `ExecuteBefore/After` 로 명시해야 한다. 이 설정에서의 처리량은 **미측정**.

### D. 피할 것

- Mass 엔티티를 매 프레임 액터 트랜스폼으로 동기화(번역기) 하는 설계 — 500마리에 3ms(W7). 액터 승격은 근거리 소수만.
- Detour Crowd 를 수백 마리에 그대로 적용(기본 50 상한[E9], 상한을 올린 비용 수치 **미확인**). 대량 개체는 플로우 필드 + 단순 분리 조향, 소수 엘리트만 Detour Crowd.
- HISM 을 Mass 시각화 컴포넌트에 사용(W3 팝핑 사례).
- 오래된(2023 이전) Mass 병렬성·API 글을 그대로 신뢰(5.8 소스와 다름, [E4]).

---

## 미확인·미해결 질문

1. **UE 5.7/5.8 공식 릴리스 노트의 MassEntity "Production-Ready/Beta" 라벨 원문** — 5.7 노트·뉴스, 5.8 뉴스, Epic 공개 로드맵(productboard c/862 "MassEntity (Beta)") 모두 WebFetch 가 목차만 반환하거나 403. 로컬 플러그인 플래그(실험)와 Epic 직원 발언(ETA 없음)으로 대체.
2. **Unreal Fest 2024~2026 Mass 강연 본문** — "How to Build Scalable MetaHuman Crowds in Unreal Engine (Unreal Fest Chicago 2026)" YouTube, "Large Numbers of Entities with Mass in UE5 (State of Unreal 2022)" 는 본문·수치 추출 실패. 세션 페이지도 제목만 반환.
3. **CharacterMovementComponent 단독 마리당 비용 벤치마크** — W1 의 분해 추정치(0.03~0.10ms) 외 정밀 실측 없음. TDGame 에서 직접 계측 필요(예: Pawn 자체 이동 vs CMC 300마리).
4. **탑다운 ARPG 개발사의 "AI 갱신 주기·화면 밖 처리" 직접 발언** — Diablo IV, PoE2, Last Epoch, Hades II 모두 미확인. 확인된 것은 행동 규칙 재설계(D4)와 멀티코어·밀도 축소(PoE2)뿐.
5. **Days Gone 애니메이션 공유(10~20 마리 풀에 리더 1) 의 1차 출처** — 포럼 답글의 재인용만 존재. GDC 강연 본문 미확인.
6. **Mass 내비메시 지원의 현재 상태(5.8)** — Epic 발언은 2025 "매우 실험적". 5.8 에서 개선됐는지 미확인.
7. **Detour Crowd `MaxAgents` 를 수백으로 올렸을 때의 비용 곡선** — 수치 출처 없음.
8. **AnimToTexture 5.4 이후 워크플로 변경 세부** — 커뮤니티 튜토리얼 본문 추출 실패(검색 요약만).
9. **Mass 결정론(병렬 끔) 상태의 처리량** — 어떤 출처도 측정하지 않음. 밸런스 시뮬레이터 요구와 직결되므로 자체 벤치 필요.
10. **접근 불가 출처 목록**: codemattergames.com(DNS 실패), yelzkizi.org·stevestreeting.com·unrealengine.com 뉴스(403), Epic 지식베이스 DetourCrowd 문서·Epic 커뮤니티 튜토리얼(본문 미반환).

---

## 출처 색인

- [W1] https://www.strayspark.studio/blog/crowd-traffic-simulation-ue5-mass-ai (2026-03-25)
- [W2] https://80.lv/articles/simulating-5-000-zombies-using-mass-in-unreal-engine-5 (2025)
- [W3] https://forums.unrealengine.com/t/mass-5-5-mass-entity-thousands-of-units/2487745 (2025)
- [W4] https://forums.unrealengine.com/t/managing-massive-enemy-crowds-with-ai/2491440 (2025, 2026)
- [W5] https://www.dsogaming.com/mods/days-gone-mod-makes-hordes-more-challenging-with-up-to-600-zombies-on-screen/ (2021)
- [W6] https://vrealmatic.com/unreal-engine/city-sample/crowd (2023)
- [W7] https://forums.unrealengine.com/t/synchronising-and-updating-actor-transforms-in-mass-entity-performance/2573360 (2025)
- [W8] https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes (2026)
- [W9] https://www.strayspark.studio/blog/metahuman-crowd-ue5-8-guide (2026)
- [W10] https://github.com/yoreei/crowd_pathfinder (UE 5.3.2, 연도 미기재)
- [W11] https://x157.github.io/UE5/Mass/Navigation.html (5.5) ; https://x157.github.io/UE5/Mass/ (5.8 언급)
- [W12] https://github.com/DigiLogicLabs/ue5-mass-units-plugin (UE 5.7.2 검증)
- [W13] https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-gameplay-in-unreal-engine (5.8)
- [W14] https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-entity-in-unreal-engine (5.8)
- [W15] https://dev.epicgames.com/documentation/unreal-engine/using-avoidance-with-the-navigation-system-in-unreal-engine (5.8)
- [W16] https://dev.epicgames.com/documentation/unreal-engine/animation-budget-allocator-in-unreal-engine (5.8)
- [W17] https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine (5.8)
- [W18] https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sharing-plugin-in-unreal-engine (5.8)
- [W19] https://github.com/Megafunk/MassSample (5.6+, 2024~2025)
- [W20] https://forums.unrealengine.com/t/mass-entity-roadmap-vision-and-more-questions/2527030 (2025, Epic James Keeling)
- [W21] https://www.poe-vault.com/poe2/news/poe2-major-performance-upgrade-patch-0-4-0 (2025)
- [W22] https://www.icy-veins.com/d4/news/diablo-4-devs-talk-future-plans-season-11-and-the-games-true-identity/ (2025)
- [W23] https://forums.unrealengine.com/t/gameplay-ability-system-gas-future-in-unreal-engine-6/2738426 (2026, Epic ZhiKangShao)
- [W24] https://forums.unrealengine.com/t/using-gas-exclusively-for-damage-health-in-larger-scale-game/1270092 (2023~2024)
- [W25] https://forums.unrealengine.com/t/massai-replication-state-in-5-7-and-future-roadmap/2684012 (2025~2026, Epic James Keeling)
- [W26] https://www.yawlighthouse.com/blog/ue-mass-processors/ (2026, 5.8)
- [W27] https://forums.unrealengine.com/t/mass-entity-processors-not-processing-in-parallel/1297162 (2023)
- [W28] https://www.strayspark.studio/blog/mover-2-0-vs-character-movement-component-ue5-7-2026 (2026)
- [W29] https://kyleqiliu.com/blog/unreal/massframework/handsonunrealmasssystem101/ (2024-12)
- [W30] https://gamerant.com/days-gone-zombie-ai-gdc/ (2018)
- [W31] https://forums.unrealengine.com/t/is-this-possible-with-niagara-particles-as-enemies/619028 (2022)
- [W32] https://gamefromscratch.com/unreal-engine-5-7-released/ (2025)
- [E1] `Engine/Source/Runtime/MassEntity/Private/MassProcessingPhaseManager.cpp:46-51`
- [E2] `Engine/Source/Runtime/MassEntity/Public/MassProcessingPhaseManager.h:201,207,212,213` ; `Engine/Source/Runtime/MassEntity/Public/MassEntityManager.h:138`
- [E3] `Engine/Source/Runtime/MassEntity/Private/MassProcessingPhaseManager.cpp:30-36` ; `Engine/Source/Runtime/MassEntity/Public/MassProcessingTypes.h:14` ; `Engine/Source/Runtime/MassEntity/Private/MassEntityQuery.cpp:26-31`
- [E4] `Engine/Source/Runtime/MassEntity/Public/MassEntityQuery.h:113` ; `Engine/Source/Runtime/MassEntity/Public/MassProcessor.h:259`
- [E5] `Engine/Plugins/Runtime/MassGameplay/MassGameplay.uplugin:15-16`
- [E6] `Engine/Plugins/AI/MassAI/MassAI.uplugin:15-16`
- [E7] `Engine/Plugins/AI/MassCrowd/MassCrowd.uplugin:15-16`
- [E8] `Engine/Plugins/Runtime/AnimationBudgetAllocator/Source/AnimationBudgetAllocator/Private/AnimationBudgetAllocatorCVars.cpp:10-13,195-198` ; `AnimationBudgetAllocator.uplugin:24`
- [E9] `Engine/Source/Runtime/AIModule/Classes/Navigation/CrowdManager.h:294` ; `Engine/Source/Runtime/AIModule/Private/Navigation/CrowdManager.cpp:168`
- [E10] `Engine/Plugins/Runtime/SignificanceManager/Source/SignificanceManager/Public/SignificanceManager.h:36,118,121` ; `SignificanceManager.uplugin:24`
- [P] `Source/TDGame/Combat/` (UTDCombatComponent, UTDDamageSubsystem, UTDDamageDefinition, Tests/FTDScopedCombatWorld) — 과제 배경에서 주어진 정보, 본 조사에서는 열지 않음.

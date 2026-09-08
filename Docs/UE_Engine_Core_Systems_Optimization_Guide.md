# 언리얼 엔진 코어 시스템 심층 최적화 가이드
(Unreal Engine Core Systems Deep Optimization Guide: UI · GC · GAS · Render Scene · AI)

- 대상 엔진: Unreal Engine 5.x
- 목적: 애니메이션/물리 외에 대규모 프로젝트 런타임에서 프레임 드랍(Hitch)과 CPU/GPU 병목을 유발하는 엔진 코어 서브시스템(UI, 가비지 컬렉션, GAS/네트워크, 렌더 씬/VSM, AI/네비게이션)의 숨겨진 메커니즘을 분석하고 실전 해결책을 제공합니다.

---

## 목차
1. [UI (UMG/Slate): 글로벌 무효화와 함수 바인딩 트랩](#1-ui-umgslate-글로벌-무효화와-함수-바인딩-트랩)
2. [메모리 (GC & UObject): 도달 가능성 분석 스톨과 GC 클러스터링](#2-메모리-gc--uobject-도달-가능성-분석-스톨과-gc-클러스터링)
3. [네트워크/GAS: 푸시 모델(Push Model)과 GameplayTag 복제 최적화](#3-네트워크gas-푸시-모델push-model과-gameplaytag-복제-최적화)
4. [렌더 씬 & 섀도: FPrimitiveSceneInfo 등록 비용과 VSM 캐시 파괴](#4-렌더-씬--섀도-fprimitivesceneinfo-등록-비용과-vsm-캐시-파괴)
5. [AI & 시스템: 비동기 네비게이션 경로 탐색과 시야 트레이스 분산](#5-ai--시스템-비동기-네비게이션-경로-탐색과-시야-트레이스-분산)
6. [실전 엔진 설정(INI/CVar) 및 점검 체크리스트](#6-실전-엔진-설정inicvar-및-점검-체크리스트)

---

## 1. UI (UMG/Slate): 글로벌 무효화와 함수 바인딩 트랩

### 1.1 병목 메커니즘
- **레이아웃 프리패스(Prepass) 전수 순회**: Slate의 기본 동작은 매 프레임 전체 위젯 트리를 재귀적으로 순회하며 크기, 정렬, 렌더 배치를 계산(`Prepass`)합니다. 위젯 수가 수백 개에 달하는 복잡한 HUD/인벤토리에서는 이 Prepass만으로 게임 스레드가 5~10ms 이상 소모됩니다.
- **블루프린트 함수 바인딩(Binding Function)의 치명적 함정**: UMG 디자이너에서 텍스트나 가시성 프로퍼티 옆의 '바인딩(Bind)' 버튼을 눌러 블루프린트 함수를 연결하면, 엔진은 해당 위젯의 틱마다 UFunction 리플렉션 호출을 수행합니다. 이로 인해 해당 위젯과 부모 패널의 Slate 캐시가 매 프레임 강제로 파괴(Invalidate)되어 무효화 시스템이 무력화됩니다.

### 1.2 해결 패턴: 글로벌 무효화 + 이벤트 주도 UI
- `Slate.EnableGlobalInvalidation 1`을 적용하여 화면 전체를 단일 가상 무효화 패널로 캐싱합니다.
- 위젯 프로퍼티는 절대 매 프레임 폴링 바인딩하지 않고, 실제 데이터가 바뀔 때만 C++ 또는 이벤트 그래프에서 세터(`SetText`, `SetVisibility`)를 단 1회 호출하는 이벤트 주도(Event-Driven) 구조로 전환합니다.

```cpp
// Context: 이벤트 주도 방식의 UI 프로퍼티 갱신 (Slate Prepass 낭비 원천 차단)
void UMyPlayerHUDWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    
    // 데이터 변경 델리게이트에만 1회 바인딩
    if (APlayerState* PS = GetOwningPlayerState())
    {
        if (UMyAttributeComponent* AttrComp = PS->FindComponentByClass<UMyAttributeComponent>())
        {
            AttrComp->OnHealthChanged.AddDynamic(this, &ThisClass::HandleHealthChanged);
        }
    }
}

void UMyPlayerHUDWidget::HandleHealthChanged(float NewHealth, float MaxHealth)
{
    // 값이 실제로 변했을 때만 1회 세터를 호출하여 Slate Invalidation 캐시 보존
    HealthProgressBar->SetPercent(NewHealth / MaxHealth);
    HealthTextBlock->SetText(FText::AsNumber(FMath::RoundToInt(NewHealth)));
}
```

---

## 2. 메모리 (GC & UObject): 도달 가능성 분석 스톨과 GC 클러스터링

### 2.1 병목 메커니즘
- 언리얼 엔진의 가비지 컬렉터는 메모리를 해제하기 전, 루트 오브젝트(Root Objects)로부터 살아있는 모든 객체를 역추적하는 **도달 가능성 분석(Reachability Analysis - Mark Phase)**을 수행합니다.
- 오픈월드나 군중 전투 등에서 월드 내 UObject 수가 50,000~100,000개를 넘어가면, GC 주기마다 수천 개의 포인터를 순회하느라 메인 스레드가 10~50ms 멈추는 극심한 히치(GC Hitch)가 발생합니다.

### 2.2 해결 패턴: GC 클러스터링(GC Clustering)
- 액터와 해당 액터가 소유한 모든 컴포넌트를 하나의 덩어리인 'GC 클러스터'로 묶어 처리합니다.
- GC 순회 시 수십 개의 컴포넌트를 개별 검사하지 않고, 액터 하나만 유효하면 내부 컴포넌트 전체를 검사 완료된 것으로 간주하여 도달 가능성 분석 시간을 80% 이상 단축합니다.

```ini
# Config/DefaultEngine.ini
[SystemSettings]
; 레벨 내 액터 및 컴포넌트를 단일 노드로 클러스터링
gc.CreateGCClusters=1

; UObject 소멸자 호출 및 렌더 리소스 정리를 백그라운드 워커 스레드로 분산
gc.MultithreadedDestructionEnabled=1

; 한 프레임에 모든 대기 오브젝트를 지우지 않고 60초 주기로 점진적 정리
gc.TimeBetweenPurgingPendingKillObjects=60
```

---

## 3. 네트워크/GAS: 푸시 모델(Push Model)과 GameplayTag 복제 최적화

### 3.1 병목 메커니즘
- **전통적인 DOREPLIFETIME의 풀 스캔 오버헤드**: 기본 네트워크 리플리케이션은 매 넷 틱(Net Tick)마다 등록된 모든 프로퍼티의 메모리를 이전 프레임과 바이트 단위(`memcmp`)로 비교합니다. 수십 개의 스탯을 가진 캐릭터가 수십 명 모이면 CPU가 메모리 비교에 소모됩니다.
- **GameplayTag 복제 패킷 낭비**: 기본 `FGameplayTagContainer` 복제는 태그 딕셔너리의 전체 인덱스를 직렬화하므로 빈번한 태그 토글 시 대역폭이 낭비됩니다.

### 3.2 해결 패턴: 푸시 모델(Push-Based Replication) & Fast Tag Replication
- 엔진의 푸시 모델(`Net Push Model`)을 활성화하여, C++에서 실제로 값이 변경되었을 때만 `MARK_PROPERTY_DIRTY`를 호출하여 비교 연산을 $O(1)$로 단축합니다.
- 어트리뷰트 세트의 복제 파라미터에 `bIsPushBased = true`를 지정합니다.

```cpp
// Context: Source/MyProject/GAS/MyAttributeSet.cpp
#include "Net/UnrealNetwork.h"

void UMyAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    FDoRepLifetimeParams PushParams;
    PushParams.bIsPushBased = true;
    PushParams.Condition = COND_None;

    // 값이 실제로 변경되었을 때만 네트워크 직렬화 큐에 주입 (매 틱 memcmp 전수 검사 제거)
    DOREPLIFETIME_WITH_PARAMS_FAST(UMyAttributeSet, Health, PushParams);
    DOREPLIFETIME_WITH_PARAMS_FAST(UMyAttributeSet, Mana, PushParams);
}

void UMyAttributeSet::SetHealth(float NewVal)
{
    Health.SetCurrentValue(NewVal);
    // 푸시 모델 더티 플래그 마킹
    MARK_PROPERTY_DIRTY_FROM_TOP(UMyAttributeSet, Health);
}
```

---

## 4. 렌더 씬 & 섀도: FPrimitiveSceneInfo 등록 비용과 VSM 캐시 파괴

### 4.1 병목 메커니즘
- **씬 프록시 동기 락 (`CreateSceneProxy`)**: 액터를 런타임에 동적으로 `SpawnActor`하거나 가시성을 토글(`SetVisibility`)하면, 렌더 씬(`FScene`)에 지오메트리를 등록하기 위해 `FScene::AddPrimitive` 및 `CreateSceneProxy`가 호출되며 렌더 스레드와 동기화 대기가 발생합니다.
- **가상 섀도 맵(VSM) 캐시 무효화 트랩**: UE5의 VSM(Virtual Shadow Map)은 움직이지 않는 정적 지형/물체의 그림자 페이지를 물리 캐시에 저장하여 재사용합니다. 그러나 바람에 흔들리는 풀, 나뭇잎, 미세 회전하는 프랍에 월드 포지션 오프셋(WPO)을 무분별하게 켜두면, **매 프레임 그림자 페이지 캐시가 전부 파괴되어 섀도 패스 렌더링 비용이 수 배로 폭증**합니다.

### 4.2 해결 패턴: 액터 풀링 + WPO 거리 제한 및 VSM 캐시 가드
- 런타임 동적 스폰/파괴 대신 비활성 상태로 숨겨두는 **액터 풀링(Actor Pooling)**을 적용하여 `FPrimitiveSceneInfo` 재등록 오버헤드를 회피합니다.
- 풀/나뭇잎 머티리얼의 WPO는 카메라 일정 거리 밖에서 비활성화(`bEvaluateWorldPositionOffset = false`)하거나, VSM 캐시 무효화 대상에서 정적 프랍을 분리합니다.

```ini
# Config/DefaultEngine.ini
[SystemSettings]
; 카메라에서 일정 거리 이상 떨어진 메시의 월드 포지션 오프셋(WPO) 평가를 꺼서 VSM 캐시 보존
r.Shadow.Virtual.WPO.Culling=1
r.Shadow.Virtual.WPO.CullingRadius=3000
```

---

## 5. AI & 시스템: 비동기 네비게이션 경로 탐색과 시야 트레이스 분산

### 5.1 병목 메커니즘
- **동기 A\* 길찾기 메인 스레드 스톨**: AIController가 `MoveToLocation`을 호출할 때 내부적으로 `FindPathSync`가 사용되면, 복잡한 지형이나 장거리 경로 탐색 시 수천 개의 폴리곤을 메인 스레드에서 직접 계산하느라 순간적으로 게임 프레임이 정지합니다.
- **AIPerception 시야 레이캐스트 스파이크**: `UAIPerceptionComponent`의 시야 감각(Sight Sense)은 매 감지 틱마다 등록된 모든 자극원(Stimuli Source)을 대상으로 가시선(Line of Sight) 동기 트레이스를 수행하여 피직스 씬 쿼리를 잠식합니다.

### 5.2 해결 패턴: 비동기 경로 질의(`FindPathAsync`) & 퍼셉션 주기 분산
- 경로 탐색은 언리얼의 `UNavigationSystemV1::FindPathAsync`를 사용하여 태스크 그래프(Task Graph)의 워커 스레드로 완전히 이전합니다.
- AI 시야 감각은 `bAutoRegisterAsSourceWithAll`을 끄고, 플레이어만 자극원으로 등록하며, 시야 갱신 주기(`AIPerceptionComponent->SetComponentTickInterval`)를 중요도에 따라 분산시킵니다.

```cpp
// Context: Source/MyProject/AI/MyAIController.cpp
void AMyAIController::MoveToLocationOptimized(const FVector& DestLocation)
{
    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!NavSys) return;

    FPathFindingQuery Query;
    Query.StartLocation = GetPawn()->GetActorLocation();
    Query.EndLocation = DestLocation;
    Query.NavAgentProperties = GetNavAgentPropertiesRef();
    Query.NavData = NavSys->GetDefaultNavDataInstance();

    // 메인 스레드 정지 없이 백그라운드 스레드에서 A* 탐색 후 콜백 호출
    NavSys->FindPathAsync(GetNavAgentPropertiesRef(), Query,
        FNavPathQueryDelegate::CreateUObject(this, &ThisClass::OnAsyncPathComplete));
}

void AMyAIController::OnAsyncPathComplete(uint32 PathId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path)
{
    if (Result == ENavigationQueryResult::Success && Path.IsValid())
    {
        // 완료된 비동기 경로를 PathFollowingComponent에 주입하여 이동 개시
        GetPathFollowingComponent()->RequestMove(FAIMoveRequest(), Path);
    }
}
```

---

## 6. 실전 엔진 설정(INI/CVar) 및 점검 체크리스트

| 분야 | 핵심 설정 및 코드 패턴 | 효과 |
|---|---|---|
| **UI (UMG)** | `Slate.EnableGlobalInvalidation=1` | 위젯 트리 Prepass 지오메트리 전수 순회 80% 이상 절감 |
| **UI (UMG)** | UMG 디자이너 '함수 바인딩(Bind)' 제거 → C++ 이벤트 세터 전환 | 매 틱 리플렉션 호출 및 Slate 캐시 강제 파괴 방지 |
| **메모리 (GC)** | `gc.CreateGCClusters=1`, `gc.MultithreadedDestructionEnabled=1` | UObject 수만 개 도달 가능성 검사 스톨(GC Hitch) 차단 |
| **네트워크 (GAS)** | `DOREPLIFETIME_WITH_PARAMS_FAST` + `Params.bIsPushBased = true` | 매 넷 틱 수백 개 어트리뷰트 memcmp 전수 비교 제거 |
| **렌더링 (VSM)** | `r.Shadow.Virtual.WPO.Culling=1`, WPO 거리 제한 | 풀/나뭇잎 WPO로 인한 매 프레임 VSM 섀도 캐시 파괴 방지 |
| **렌더링 (Scene)** | 런타임 빈번한 스폰 대신 액터 풀링(`Actor Pooling`) 사용 | `FScene::AddPrimitive` 및 씬 프록시 동기 생성 락 제거 |
| **AI (Navigation)** | `NavSys->FindPathAsync` 비동기 경로 탐색 | 장거리 네비메시 A* 계산으로 인한 메인 게임 스레드 멈춤 방지 |
| **AI (Perception)** | `UAIPerceptionComponent` 틱 주기 분산 및 자극원 최소화 | 매 프레임 수십 회의 동기 가시선(LOS) 레이캐스트 스파이크 차단 |

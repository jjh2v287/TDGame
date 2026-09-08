# 언리얼 엔진 월드 파티션 심층 최적화 가이드
(Unreal Engine World Partition Deep Optimization Guide: Streaming Hitches · Async Evaluation · Top-Down Camera · NavMesh · Memory)

- 대상 엔진: Unreal Engine 5.x / 5.8
- 목적: 대규모 오픈월드 및 탑다운 ARPG 환경에서 월드 파티션(World Partition) 셀 로드/언로드 시 발생하는 게임 스레드 히치(Hitch), 메모리 누수, 시야 끝자락 팝인(Pop-in) 현상을 엔진 소스 및 런타임 아키텍처 수준에서 분석하고 완벽한 실전 최적화 솔루션을 제공합니다.

---

## 목차
1. [월드 파티션 런타임 히치(Hitch)의 핵심 원인 분석](#1-월드-파티션-런타임-히치hitch의-핵심-원인-분석)
2. [UE 5.8 비동기 스트리밍 평가 (Async Update Streaming)](#2-ue-58-비동기-스트리밍-평가-async-update-streaming)
3. [가비지 컬렉션(GC) 및 비동기 로딩 타임 슬라이싱](#3-가비지-컬렉션gc-및-비동기-로딩-타임-슬라이싱)
4. [탑다운(Top-Down) 카메라 환경 특화 스트리밍 소스 최적화](#4-탑다운top-down-카메라-환경-특화-스트리밍-소스-최적화)
5. [그리드 크기(CellSize)와 로딩 범위(LoadingRange) 최적 비율](#5-그리드-크기cellsize와-로딩-범위loadingrange-최적-비율)
6. [메모리 상주 함정: bIsSpatiallyLoaded와 HLOD 전략](#6-메모리-상주-함정-bisspatiallyloaded와-hlod-전략)
7. [내비메시(NavMesh) 청크 스트리밍과 비동기 데이터 수집](#7-내비메시navmesh-청크-스트리밍과-비동기-데이터-수집)
8. [실전 엔진 설정 (DefaultEngine.ini) 종합 레퍼런스](#8-실전-엔진-설정-defaultengineini-종합-레퍼런스)

---

## 1. 월드 파티션 런타임 히치(Hitch)의 핵심 원인 분석

월드 파티션은 기존의 수동 서브레벨 스트리밍을 자동화한 강력한 시스템이지만, 기본 설정 상태에서는 이동 중 잦은 프레임 드랍(Spike)을 유발합니다. 그 주원인은 다음과 같습니다:

1. **셀 언로드 시의 동기 강제 GC**: 셀이 플레이어 시야 밖으로 벗어나 언로드(Unload)될 때 엔진 기본 동작은 즉각적인 전체 GC를 호출하여 수십 ms 동안 게임 스레드를 정지시킵니다.
2. **게임 스레드 동기 스트리밍 상태 평가**: 매 프레임 게임 스레드에서 모든 스트리밍 소스와 월드 내 수백~수천 개 셀의 거리를 계산하고 해시를 비교합니다.
3. **액터 스폰 및 컴포넌트 등록 폭증**: 새로운 셀이 로드될 때 셀 내부의 수십 개 액터가 단일 프레임에 `RegisterComponent()`, `PostRegisterAllComponents()`, `BeginPlay()`를 동시 실행하여 프레임 타임을 초과합니다.
4. **내비메시 동기 리빌드**: 셀이 로드되면서 지오메트리 콜리전이 등록될 때 내비메시 타일이 게임 스레드에서 동기식으로 재계산됩니다.

---

## 2. UE 5.8 비동기 스트리밍 평가 (Async Update Streaming)

### 2.1 메커니즘 (`WorldPartitionStreamingPolicy.cpp`)
UE 5.8 엔진 소스에서 `UWorldPartitionStreamingPolicy::UpdateStreamingState()`는 기본적으로 게임 스레드에서 실행되지만, 비동기 태스크 시스템(`UE::Tasks::TTask<void>`)을 지원합니다.

```cpp
// Engine/Source/Runtime/Engine/Private/WorldPartition/WorldPartitionStreamingPolicy.cpp:68-72
bool UWorldPartitionStreamingPolicy::IsAsyncUpdateStreamingStateEnabled = false;
FAutoConsoleVariableRef UWorldPartitionStreamingPolicy::CVarAsyncUpdateStreamingStateEnabled(
    TEXT("wp.Runtime.UpdateStreaming.EnableAsyncUpdate"),
    UWorldPartitionStreamingPolicy::IsAsyncUpdateStreamingStateEnabled,
    TEXT("Set to enable asynchronous World Partition UpdateStreamingState."),
    ECVF_Default
);
```

### 2.2 해결책
- `wp.Runtime.UpdateStreaming.EnableAsyncUpdate 1`을 적용합니다.
- 셀 가시성 판정, 거리 계산, 상태 해시 갱신이 백그라운드 워커 스레드에서 비동기로 분산 실행되어 게임 스레드의 CPU 점유율을 0에 가깝게 낮춥니다.
- 추가로 `wp.Runtime.UpdateStreaming.EnableOptimization 1`을 켜면 플레이어가 정지해 있거나 소스 이동이 미미할 때 불필요한 재평가를 건너뜁니다.

---

## 3. 가비지 컬렉션(GC) 및 비동기 로딩 타임 슬라이싱

### 3.1 셀 언로드 시 강제 GC 차단
엔진 소스 `CoreSettings.cpp`의 주석에는 다음과 같이 명시되어 있습니다:
```cpp
// TEXT("Whether to force a GC after levels are streamed out to instantly reclaim the memory at the expensive of a hitch.")
static FAutoConsoleVariableRef CVarForceGCAfterLevelStreamedOut(
    TEXT("s.ForceGCAfterLevelStreamedOut"),
    GLevelStreamingForceGCAfterLevelStreamedOut, ... // 기본값: 1
);
```
- **해결책**: `s.ForceGCAfterLevelStreamedOut=0` 설정.
- 셀이 언로드되더라도 즉시 동기 GC를 호출하지 않고, 언리얼 엔진의 백그라운드 점진적 GC(Incremental GC) 큐로 넘겨 프레임 드랍을 완전히 제거합니다.

### 3.2 프레임당 비동기 로딩 및 액터 등록 시간 제한
- `s.AsyncLoadingTimeLimit`: 프레임당 비동기 에셋 로딩에 할당할 최대 시간 (기본 5.0ms → **2.0~2.5ms**로 제한 권장).
- `s.LevelStreamingActorsUpdateTimeLimit`: 스트리밍된 셀의 액터들을 초기화하고 월드에 등록하는 데 쓸 최대 시간 (기본 5.0ms → **2.0ms**로 제한 권장).
- `s.LevelStreamingComponentsRegistrationGranularity`: 한 번에 등록할 컴포넌트 묶음 단위 (기본 10 → **10~20** 유지).

이 타임 슬라이싱을 통해 셀 로딩이 발생하더라도 매 프레임 정해진 예산(Budget) 내에서만 분할 처리되므로 60FPS(16.6ms) 환경을 안정적으로 방어합니다.

---

## 4. 탑다운(Top-Down) 카메라 환경 특화 스트리밍 소스 최적화

### 4.1 탑다운 시점의 치명적 함정
- 언리얼 엔진의 기본 스트리밍 소스는 플레이어의 `Pawn` 위치에 부착됩니다.
- 하지만 탑다운 ARPG는 카메라가 1000~2000 유닛 높이에서 -45~-60도 각도로 내려다봅니다.
- 캐릭터가 화면 하단(남쪽)으로 이동하면, 카메라는 북쪽에 위치한 채 남쪽을 넓게 비춥니다.
- 만약 폰(Pawn) 위치를 기준으로만 셀을 로드하면, **화면 상단이나 측면 가장자리에서 배경이 늦게 로드되어 시야에 검은 공간이나 팝인이 노출**되는 현상이 발생합니다.

### 4.2 해결 패턴: 카메라 지면 교차점(Ground Intersection) 기반 스트리밍 소스
폰의 발밑이 아니라 **카메라 시선 레이가 바닥 평면(Pawn 높이 Z)과 교차하는 중심점**을 실시간 계산하여 스트리밍 소스의 위치로 등록합니다.

```cpp
// File: Source/TDGame/World/TDTopDownStreamingSourceComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "TDTopDownStreamingSourceComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDTopDownStreamingSourceComponent : public UWorldPartitionStreamingSourceComponent
{
	GENERATED_BODY()

public:
	UTDTopDownStreamingSourceComponent();

	virtual bool GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) const override;
};
```

```cpp
// File: Source/TDGame/World/TDTopDownStreamingSourceComponent.cpp
#include "TDTopDownStreamingSourceComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"

UTDTopDownStreamingSourceComponent::UTDTopDownStreamingSourceComponent()
{
	Priority = EStreamingSourcePriority::High;
	TargetState = EStreamingSourceTargetState::Activated;
}

bool UTDTopDownStreamingSourceComponent::GetStreamingSource(FWorldPartitionStreamingSource& OutStreamingSource) const
{
	if (!Super::GetStreamingSource(OutStreamingSource))
	{
		return false;
	}

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController()))
		{
			if (const APlayerCameraManager* CamManager = PC->PlayerCameraManager)
			{
				const FVector CamLoc = CamManager->GetCameraLocation();
				const FVector CamFwd = CamManager->GetCameraRotation().Vector();
				const float GroundZ = OwnerPawn->GetActorLocation().Z;

				// 카메라 시선이 지면과 만나는 투영 지점을 스트리밍 중심으로 보정
				if (!FMath::IsNearlyZero(CamFwd.Z))
				{
					const float Distance = (GroundZ - CamLoc.Z) / CamFwd.Z;
					if (Distance > 0.0f)
					{
						OutStreamingSource.Location = CamLoc + CamFwd * Distance;
					}
				}
			}
		}
	}

	return true;
}
```

---

## 5. 그리드 크기(CellSize)와 로딩 범위(LoadingRange) 최적 비율

월드 파티션 런타임 해시(`WorldPartitionRuntimeHashSet`) 설정 시 탑다운 장르에 맞는 그리드 튜닝이 필수적입니다.

| 구분 | 기본값 (UE Default) | 탑다운 ARPG 권장값 | 이유 |
| :--- | :--- | :--- | :--- |
| **셀 크기 (`CellSize`)** | 25,600 (256m) | **6,400 ~ 12,800 (64m~128m)** | 기본값 256m는 너무 커서 셀 로드시 한 번에 스폰되는 액터 수가 과도함. 탑다운은 화면 시야가 좁고 밀도가 높으므로 작은 단위로 쪼개야 부하 분산에 유리함 |
| **로딩 범위 (`LoadingRange`)** | 51,200 (512m) | **12,800 ~ 19,200 (128m~192m)** | 카메라 줌아웃 최대 거리 + 이동 예측 버퍼(20~30m)를 포함한 최적 범위. 불필요한 원거리 액터 상주 방지 |
| **클립맵 수 (`NumClipmaps`)** | 4 | **2 ~ 3** | 원거리 지평선이 보이지 않는 탑다운 특성상 다단계 원거리 클립맵은 메모리 낭비 |

---

## 6. 메모리 상주 함정: bIsSpatiallyLoaded와 HLOD 전략

### 6.1 비공간 액터 (`bIsSpatiallyLoaded = false`) 트랩
- 월드 파티션 레벨에 배치된 액터의 `Is Spatially Loaded` 체크박스가 꺼져 있으면, 그 액터는 **플레이어와의 거리와 무관하게 맵 전체가 언로드될 때까지 메모리에 항상 상주**합니다.
- 레벨 디자이너의 부주의로 몬스터 스폰 지점, 일반 데코레이션 프랍, 포인트 라이트가 Non-Spatially Loaded로 설정되면 메모리가 수 기가바이트 단위로 폭증합니다.
- **원칙**: 오직 `GameMode`, `GameState`, 맵 전체를 비추는 단일 `DirectionalLight`, 글로벌 BGM 트리거를 제외한 모든 액터는 반드시 `bIsSpatiallyLoaded = true`여야 합니다.

### 6.2 탑다운 최적화 HLOD 전략
- **Instanced Mesh (ISM) HLOD 권장**:
  - 나무, 바위, 타일형 건물 등 반복 소품에 Merged Mesh HLOD를 쓰면 고유한 대형 텍스처와 메쉬를 별도로 구워 메모리를 낭비합니다.
  - ISM HLOD Layer를 사용하면 원거리에서도 기존 인스턴스 버퍼를 재활용하여 메모리 추가 비용 없이 드로우콜을 1~2개로 압축합니다.
- **HLOD 웜업 활성화 (`wp.Runtime.HLOD.WarmupEnabled 1`)**:
  - 셀이 언로드되고 HLOD로 전환되는 순간 텍스처와 기하가 늦게 로드되어 깜빡이는 현상을 방지하기 위해 5프레임 전 미리 워밍업합니다.

---

## 7. 내비메시(NavMesh) 청크 스트리밍과 비동기 데이터 수집

### 7.1 내비메시 동기 갱신 병목
- 런타임에 셀이 로드될 때 내비메시를 실시간 동적 생성(`RuntimeGeneration = Dynamic`)하면, 콜리전 복합체 검사로 인해 게임 스레드가 멈춥니다.
- **해결 패턴 (정적 베이크 청크 스트리밍)**:
  - `AWorldSettings::NavigationDataChunkGridSize`를 월드 파티션 셀 크기(예: 12800)와 1:1로 일치시킵니다.
  - 에디터에서 `UWorldPartitionNavigationDataBuilder` 커맨드릿으로 내비메시 청크(`ANavigationDataChunkActor`)를 미리 베이크하여 셀과 함께 스트리밍합니다.
- **불가피하게 Dynamic 내비메시를 사용할 경우**:
  - `bDoFullyAsyncNavDataGathering=True`를 필수 적용하여 지오메트리 수집 작업을 워커 스레드로 완전히 격리합니다.

---

## 8. 실전 엔진 설정 (DefaultEngine.ini) 종합 레퍼런스

```ini
; File: Config/DefaultEngine.ini
[/Script/Engine.CoreEngineSettings]
; ====================================================================
; 1. 셀 언로드 히치 원천 차단 (강제 GC 비활성화)
; ====================================================================
s.ForceGCAfterLevelStreamedOut=0

; ====================================================================
; 2. 비동기 로딩 및 액터 스폰 타임 슬라이싱 (프레임 드랍 방어)
; ====================================================================
; 프레임당 비동기 에셋 로딩 시간 예산 (기본 5.0ms -> 2.5ms로 슬라이싱)
s.AsyncLoadingTimeLimit=2.5
; 프레임당 스트리밍 액터 초기화/등록 시간 예산 (기본 5.0ms -> 2.0ms)
s.LevelStreamingActorsUpdateTimeLimit=2.0
; 한 프레임에 등록할 컴포넌트 묶음 수 제한
s.LevelStreamingComponentsRegistrationGranularity=10
s.LevelStreamingAddPrimitiveGranularity=120

[/Script/NavigationSystem.RecastNavMesh]
; ====================================================================
; 3. 내비메시 지오메트리 완전 비동기 수집 (게임 스레드 블로킹 방지)
; ====================================================================
bDoFullyAsyncNavDataGathering=True

[SystemSettings]
; ====================================================================
; 4. UE 5.8 비동기 월드 파티션 스트리밍 평가 (워커 스레드 오프로딩)
; ====================================================================
wp.Runtime.UpdateStreaming.EnableAsyncUpdate=1
wp.Runtime.UpdateStreaming.EnableOptimization=1

; ====================================================================
; 5. HLOD 전환 웜업 (셀 로드/언로드 경계 팝인 및 플리커링 차단)
; ====================================================================
wp.Runtime.HLOD.WarmupEnabled=1
wp.Runtime.HLOD.WarmupNumFrames=5
```

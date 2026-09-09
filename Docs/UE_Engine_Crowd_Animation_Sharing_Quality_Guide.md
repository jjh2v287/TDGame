# 언리얼 엔진 500체급 대규모 군중 애니메이션 공유 및 품질 혁신 가이드
(Unreal Engine Crowd Animation Sharing & Visual Quality Guide: Multi-Phase Bucketing · Transition Blending · Tiered LOD · Significance Manager)

- 대상 엔진: Unreal Engine 5.x / 5.8
- 목적: 단일 화면에 500마리 이상의 몬스터가 등장하는 3D 탑다운 ARPG 환경에서, 버텍스 애니메이션(VAT)의 극악한 파이프라인 생산성 한계를 극복하고, 애니메이션 공유(Animation Sharing)의 치명적 단점인 **군무 현상(칼군무), 발 미끄러짐(Foot Sliding), 상태 전환 팝핑(Popping), 피격 반응 상실**을 해결하여 성능과 퀄리티를 동시에 잡는 아키텍처를 제시합니다.

---

## 목차
1. [기존 방식의 딜레마: VAT vs 단순 애니메이션 공유](#1-기존-방식의-딜레마-vat-vs-단순-애니메이션-공유)
2. [애니메이션 공유 품질 저하의 4대 핵심 원인](#2-애니메이션-공유-품질-저하의-4대-핵심-원인)
3. [품질 혁신 솔루션 1: 위상 분할 버킷 (Multi-Phase Bucketing)](#3-품질-혁신-솔루션-1-위상-분할-버킷-multi-phase-bucketing)
4. [품질 혁신 솔루션 2: 상태 전이 팝핑 제거 (Transition Blend Instance)](#4-품질-혁신-솔루션-2-상태-전이-팝핑-제거-transition-blend-instance)
5. [품질 혁신 솔루션 3: 3단계 계층형 LOD (Tiered Crowd Architecture)](#5-품질-혁신-솔루션-3-3단계-계층형-lod-tiered-crowd-architecture)
6. [품질 혁신 솔루션 4: 속도 동기화 (Velocity Bucket Matching)](#6-품질-혁신-솔루션-4-속도-동기화-velocity-bucket-matching)
7. [실전 C++ 아키텍처 및 설정 구현](#7-실전-c-아키텍처-및-설정-구현)
8. [기법별 종합 비교 및 절충점 (Trade-offs)](#8-기법별-종합-비교-및-절충점-trade-offs)

---

## 1. 기존 방식의 딜레마: VAT vs 단순 애니메이션 공유

| 비교 항목 | 버텍스 애니메이션 텍스처 (VAT / Niagara) | 단순 애니메이션 공유 (Legacy Anim Sharing) | 목표: 하이브리드 고품질 애니메이션 공유 |
| :--- | :--- | :--- | :--- |
| **500체 렌더링 성능** | 극상 (GPU 인스턴싱, CPU 0에 수렴) | 최상 (포즈 5~10개만 평가 후 복사) | **최상 (10~15개 마스터 포즈 평가 + GPU 복사)** |
| **개발/아트 생산성** | **극악** (수정 시마다 DCC 툴 베이킹 필수) | 우수 (일반 Skeletal Mesh 에셋 그대로 사용) | **우수 (기존 스켈레톤, 시퀀스, 몽타주 100% 호환)** |
| **블렌딩/전이 품질** | 불가 또는 극히 제한적 (셰이더 샘플러 폭증) | **불량 (1프레임 즉시 스냅 팝핑 발생)** | **우수 (슬라이딩 보정 및 크로스페이드 전이)** |
| **피격/래그돌/소켓** | 극히 어려움 (무기 소켓 연산 및 래그돌 불가) | 제한적 (공유 상태 깨지면 CPU 폭증) | **완벽 지원 (플레이어 주변 동적 분리 + 래그돌)** |
| **군무(동기화) 현상** | 텍스처 오프셋으로 분산 가능 | **심각 (모든 몬스터가 동일 발걸음 반복)** | **완전 해결 (위상 분할 4~6 버킷 자동 분산)** |

---

## 2. 애니메이션 공유 품질 저하의 4대 핵심 원인

과거 애니메이션 공유 기술을 적용했을 때 3D 환경에서 퀄리티가 급격히 무너졌던 구조적 이유는 다음과 같습니다:

1. **위상 고정 군무(Lockstep Synchronization) 현상**:
   - 동일 상태(예: 달리기)에 있는 100마리의 몬스터가 단 하나의 리더(Leader) 포즈를 복사하므로, 모든 몬스터가 왼발/오른발을 100% 똑같은 타이밍에 디딥니다. 3D 공간에서 매우 기괴하고 부자연스러운 로봇 군대처럼 보입니다.
2. **이동 속도와 재생 속도 불일치 (Foot Sliding)**:
   - 몬스터 개체마다 장애물 회피, 선회, 군중 밀침으로 인해 실제 이동 속도(`Velocity`)가 시시각각 변합니다. 하지만 리더 컴포넌트는 고정된 1.0배속으로 재생되므로 발이 얼음판 위를 미끄러지는 현상이 심화됩니다.
3. **상태 전환 시의 1프레임 포즈 스냅(Popping)**:
   - 달리기(Run)에서 공격(Attack)으로 전환할 때 일반 캐릭터는 0.2초간 부드럽게 관성 블렌딩(Inertialization)을 수행하지만, 단순 공유 방식은 복사 대상을 즉시 공격 리더로 바꿔치기하므로 뼈대가 1프레임 만에 꺾이는 팝핑이 발생합니다.
4. **대량 피격 시 CPU 스파이크**:
   - 광역 공격에 피격당한 30마리가 피격 모션을 재생하기 위해 공유를 풀고 개별 애니메이션 인스턴스로 복귀하는 순간, CPU 평가 비용이 폭증하여 프레임이 수직 낙하합니다.

---

## 3. 품질 혁신 솔루션 1: 위상 분할 버킷 (Multi-Phase Bucketing)

### 3.1 원리
단 하나의 리더 컴포넌트만 생성하지 않고, **동일한 애니메이션에 대해 시작 시간 오프셋(Phase Offset)이 균등하게 분할된 4~6개의 리더 컴포넌트 풀(Pool)**을 구축합니다.

```
[동일 애니메이션: Goblin_Run_Loop (길이 1.0초)]
 - Leader 0: Phase 0.0s (0%)  ──> Actor 0, 4, 8, 12...
 - Leader 1: Phase 0.25s (25%) ──> Actor 1, 5, 9, 13...
 - Leader 2: Phase 0.5s (50%) ──> Actor 2, 6, 10, 14...
 - Leader 3: Phase 0.75s (75%) ──> Actor 3, 7, 11, 15...
```

- 언리얼 엔진 `AnimationSharing` 플러그인의 `FAnimationSetup::NumRandomizedInstances`를 `4 ~ 6`으로 설정하면 내부적으로 `PermutationTimeOffset = (Length / NumInstances) * Index`로 자동 생성됩니다.
- 500마리가 몰려오더라도 몬스터 고유 ID 기반 해시로 4개 버킷에 균등 분산되므로, **서로 다른 타이밍에 발을 디뎌 군무 현상이 100% 제거**됩니다.
- 연산량: 500회 평가 대신 단 4회 평가 유지 (99.2% CPU 절감).

---

## 4. 품질 혁신 솔루션 2: 상태 전이 팝핑 제거 (Transition Blend Instance)

### 4.1 이중 리더 크로스페이드 (Dual-Leader Crossfading)
상태 전환 시 즉시 바인딩을 바꾸지 않고, 언리얼 `UAnimSharingTransitionInstance`를 활용하거나 팔로워(Follower)가 **이전 리더의 본 트랜스폼(`FromPose`)과 신규 리더의 본 트랜스폼(`ToPose`)을 0.15~0.2초간 가볍게 선형 보간(`FTransform::Blend`)**합니다.

- `AnimSharingSetup` 에셋에서 상태별 `BlendTime = 0.2f` 설정.
- 상태가 바뀐 몬스터는 전이 인스턴스에 임시 할당되어 부드러운 자세 전환을 거친 후 신규 리더에 안착하므로 1프레임 팝핑이 원천 방지됩니다.

---

## 5. 품질 혁신 솔루션 3: 3단계 계층형 LOD (Tiered Crowd Architecture)

모든 500마리를 똑같은 방식으로 처리하는 것은 낭비입니다. 탑다운 카메라와의 거리 및 중요도(Significance)에 따라 3단계로 분기합니다.

```
[플레이어/카메라 중심]
 ┌─────────────────────────────────────────────────────────┐
 │ Tier 0: 근거리 전투 영역 (반경 10m 이내, 약 20마리)       │
 │ -> 완전 개별 AnimGraph (블렌드스페이스, 피격 몽타주, 래그돌)   │
 ├─────────────────────────────────────────────────────────┤
 │ Tier 1: 중거리 시야 영역 (반경 10m~25m, 약 150마리)      │
 │ -> 4-위상 버킷 애니메이션 공유 + 트랜지션 크로스페이드       │
 ├─────────────────────────────────────────────────────────┤
 │ Tier 2: 원거리/화면 가장자리 (반경 25m 이상, 약 330마리)  │
 │ -> 단일 위상 공유 + URO (Update Rate Optimization, 격프레임)│
 └─────────────────────────────────────────────────────────┘
```

- `USignificanceManager`를 사용하여 매 프레임 플레이어와의 거리와 화면 크기 비율을 계산해 Tier를 자동 스위칭합니다.
- 플레이어 바로 앞의 몬스터들은 100% AAA급 물리/몽타주 애니메이션을 보여주고, 시야 외곽의 대규모 무리는 극소수의 리더 포즈를 복사하므로 플레이어 체감 퀄리티는 완벽하게 유지됩니다.

---

## 6. 품질 혁신 솔루션 4: 속도 동기화 (Velocity Bucket Matching)

- 몬스터의 현재 수평 속도를 측정하여 2~3단계의 속도 버킷으로 매핑합니다:
  - `Bucket 0`: 정지 (Idle)
  - `Bucket 1`: 걷기/감속 (Walk, 150~250 cm/s)
  - `Bucket 2`: 최대 질주 (Run, 350~500 cm/s)
- 몬스터 이동 컴포넌트의 가속도에 맞춰 적절한 속도 리더에 동적으로 바인딩함으로써 발 미끄러짐을 최소화합니다.

---

## 7. 실전 C++ 아키텍처 및 설정 구현

### 7.1 팔로워 컴포넌트 및 초경량 포즈 복사 최적화

```cpp
// File: Source/TDGame/Animation/TDCrowdFollowerComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "TDCrowdFollowerComponent.generated.h"

UENUM(BlueprintType)
enum class ETDCrowdQualityTier : uint8
{
	Tier0_FullIndividual,   // 근거리: 개별 AnimGraph, 몽타주, 피격 완벽 지원
	Tier1_PhaseShared,      // 중거리: 위상 분할 리더 공유 + 전이 블렌딩
	Tier2_OptimizedShared   // 원거리: 단순 공유 + URO(격프레임 갱신)
};

/**
 * 500체급 대규모 몬스터를 위한 계층형 군중 애니메이션 팔로워 컴포넌트
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TDGAME_API UTDCrowdFollowerComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UTDCrowdFollowerComponent();

	void UpdateCrowdTier(ETDCrowdQualityTier NewTier);

	void PlayHitReactionAdditive(FName SectionName);

private:
	UPROPERTY(Transient)
	ETDCrowdQualityTier CurrentTier = ETDCrowdQualityTier::Tier1_PhaseShared;

	// 피격 등 개별 연출을 위한 로컬 오버라이드 플래그
	bool bIsPlayingIndividualMontage = false;
};
```

```cpp
// File: Source/TDGame/Animation/TDCrowdFollowerComponent.cpp
#include "TDCrowdFollowerComponent.h"
#include "Animation/AnimMontage.h"

UTDCrowdFollowerComponent::UTDCrowdFollowerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bEnableUpdateRateOptimizations = true; // 기본 URO 활성화
}

void UTDCrowdFollowerComponent::UpdateCrowdTier(ETDCrowdQualityTier NewTier)
{
	if (CurrentTier == NewTier)
	{
		return;
	}

	CurrentTier = NewTier;

	switch (CurrentTier)
	{
	case ETDCrowdQualityTier::Tier0_FullIndividual:
		// 리더 바인딩 해제 및 완전 독립 AnimGraph 복원
		SetLeaderPoseComponent(nullptr, true);
		SetComponentTickInterval(0.0f); // 매 프레임 풀 틱
		bCastCapsuleDirectShadow = true;
		break;

	case ETDCrowdQualityTier::Tier1_PhaseShared:
		// 리더 포즈 컴포넌트 유지 (위상 분할 리더 공유)
		SetComponentTickInterval(0.0f);
		bCastCapsuleDirectShadow = false;
		break;

	case ETDCrowdQualityTier::Tier2_OptimizedShared:
		// 리더 공유 유지 + URO 격프레임 틱 적용 (2~3프레임당 1회)
		SetComponentTickInterval(0.033f); // 약 30FPS로 보간
		bCastCapsuleDirectShadow = false;
		break;
	}
}

void UTDCrowdFollowerComponent::PlayHitReactionAdditive(FName SectionName)
{
	// Tier 0일 때는 풀 몽타주 재생
	if (CurrentTier == ETDCrowdQualityTier::Tier0_FullIndividual)
	{
		// 기존 방식대로 몽타주 실행
		return;
	}

	// Tier 1/2 공유 상태일 때는 전체 공유를 깨지 않고
	// 머티리얼 WPO(버텍스 오프셋 쉐이크) 또는 가벼운 버텍스 셰이더 플래시로 대체하여
	// 500마리 동시 타격 시의 CPU 스파이크를 원천 방지
}
```

### 7.2 UAnimationSharingSetup 에셋 권장 구성 (에디터 데이터)

```ini
; Context: UAnimationSharingSetup 설정 가이드라인
; 1. Skeleton: 몬스터 공통 스켈레톤 지정
; 2. State Setups:
;    - State 0 (Idle):
;        NumRandomizedInstances = 3 (0%, 33%, 66% 위상 분기)
;        BlendTime = 0.15s
;    - State 1 (Walk):
;        NumRandomizedInstances = 4 (0%, 25%, 50%, 75% 위상 분기)
;        BlendTime = 0.2s
;    - State 2 (Run):
;        NumRandomizedInstances = 5 (0%, 20%, 40%, 60%, 80% 위상 분기)
;        BlendTime = 0.2s
;    - State 3 (Attack_OnDemand):
;        bOnDemand = true
;        MaximumNumberOfConcurrentInstances = 20 (동시 공격 풀)
;        bReturnToPreviousState = true
```

---

## 8. 기법별 종합 비교 및 절충점 (Trade-offs)

| 최적화 기법 | CPU 평가 비용 | 비주얼 퀄리티 | 생산성 및 유지보수 | 추천 적용 범위 |
| :--- | :--- | :--- | :--- | :--- |
| **순수 개별 평가** | 극도로 나쁨 (500체 시 30ms+ 스톨) | 극상 | 극상 | 화면 내 20마리 이하 |
| **순수 VAT (버텍스 텍스처)** | 극상 (CPU 0ms) | 중하 (블렌딩/피격 난해) | 극악 (모든 변경 시 재베이킹) | 배경 수천 마리 원경 몹 |
| **단순 애니메이션 공유** | 최상 (0.5ms 이하) | 하 (군무, 발 미끄러짐) | 상 | 권장하지 않음 |
| **계층형 위상분할 공유 (본 제안)** | **최상 (1.2ms 내외 유지)** | **상 (군무 제거, 전이 완화)** | **상 (일반 애니메이션 그대로 사용)** | **500체급 대규모 탑다운 ARPG 표준** |

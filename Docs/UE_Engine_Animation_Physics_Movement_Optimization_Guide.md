# 언리얼 엔진 내부 애니메이션 · 물리 · 이동 병목 및 최적화 아키텍처 가이드
(Unreal Engine Internal Animation, Physics, and Movement Bottlenecks & Optimization Guide)

- 대상 엔진: Unreal Engine 5.x (Chaos Physics & Parallel Animation Evaluation 기반)
- 목적: 엔진 코드 깊숙이 숨겨져 있어 프로파일링 전까지 발견하기 어려운 애니메이션/물리/이동 간의 상호작용 병목 메커니즘을 규명하고, 실전 해결 패턴을 코드와 함께 정리합니다.

---

## 목차
1. [이동 시점 vs 애니메이션 평가 시점의 본 피직스바디 이중 갱신 (Double Physics Body Update)](#1-이동-시점-vs-애니메이션-평가-시점의-본-피직스바디-이중-갱신)
2. [이동 서브스텝 중복 오버랩 및 물리 전파 병목 (Redundant Movement Overlaps)](#2-이동-서브스텝-중복-오버랩-및-물리-전파-병목)
3. [병렬 애니메이션 평가 중 게임 스레드 블로킹 (Parallel Eval Stall)](#3-병렬-애니메이션-평가-중-게임-스레드-블로킹)
4. [다중 파츠 스켈레탈 메시의 중복 애님 평가 (Leader-Follower Pose Optimization)](#4-다중-파츠-스켈레탈-메시의-중복-애님-평가)
5. [군중 AI 무브먼트의 불필요한 물리 상호작용 부하 (Physics Interaction Overhead)](#5-군중-ai-무브먼트의-불필요한-물리-상호작용-부하)
6. [스켈레탈 메시의 본 단위 오버랩 스팸 (Skeletal Mesh Overlap Collision)](#6-스켈레탈-메시의-본-단위-오버랩-스팸)
7. [URO(업데이트 레이트 최적화)와 루트 모션 충돌 트랩 (Root Motion vs URO Desync)](#7-uro업데이트-레이트-최적화와-루트-모션-충돌-트랩)
8. [화면 밖 액터의 몽타주/노티파이 정지 트랩 (VisibilityBasedAnimTickOption Pitfall)](#8-화면-밖-액터의-몽타주노티파이-정지-트랩)
9. [실전 적용 체크리스트](#9-실전-적용-체크리스트)

---

## 1. 이동 시점 vs 애니메이션 평가 시점의 본 피직스바디 이중 갱신

### 1.1 병목 메커니즘
스켈레탈 메시에 피직스 애셋(Physics Asset)이 연결되어 있을 때, 캐릭터가 이동하고 애니메이션을 재생하면 **한 프레임에 피직스바디 동기화가 최소 2회 중복 호출**됩니다.

```
[CharacterMovement Tick]
   └─ SafeMoveUpdatedComponent (캡슐 이동)
        └─ USkeletalMeshComponent::OnUpdateTransform (자식 메시 위치 갱신)
             └─ UpdateKinematicBonesToAnim() ───▶ [1차 물리 갱신: 구(Old) 포즈로 월드 위치만 이동]

[SkeletalMeshComponent Tick]
   └─ EvaluateAnimation (애니메이션 그래프 계산)
        └─ USkeletalMeshComponent::PostAnimEvaluation (평가 완료)
             └─ UpdateKinematicBonesToAnim() ───▶ [2차 물리 갱신: 신(New) 포즈로 물리 바디 재배치]
```

- **1차 갱신 (`OnUpdateTransform`)**: 이동 컴포넌트에 의해 메시의 월드 위치가 변경되면 엔진은 "피직스바디들을 새 월드 좌표로 옮겨야 한다"고 판단하여 `UpdateKinematicBonesToAnim`을 즉시 호출합니다. 그러나 이때는 새 애니메이션 포즈가 계산되기 전이므로 **이전 프레임의 낡은 포즈 상태로 위치만 옮기는 완전한 낭비 연산**입니다.
- **2차 갱신 (`PostAnimEvaluation`)**: 이후 애니메이션 평가가 끝나면 새로 계산된 본 포즈에 맞춰 피직스바디를 물리 씬에 다시 동기화합니다.
- 캐릭터 수가 많을 경우 `STAT_UpdateRBBones` 비용이 매 프레임 2배로 폭증하여 심각한 CPU 병목을 유발합니다.

### 1.2 해결 패턴: `FScopedMeshBoneUpdateOverride`
이동 처리 구간 동안 메시의 `KinematicBonesUpdateType`을 `SkipAllBones`로 임시 변경하여 1차 호출을 조기 탈출(`return`)시키고, 애니메이션 평가 완료 시점에 1회만 물리 바디를 갱신합니다.

```cpp
// RAII 스코프 가드
struct FScopedMeshBoneUpdateOverride
{
    FScopedMeshBoneUpdateOverride(USkeletalMeshComponent* InMesh, EKinematicBonesUpdateToPhysics::Type InOverride)
        : Mesh(InMesh)
    {
        if (Mesh)
        {
            SavedSetting = Mesh->KinematicBonesUpdateType;
            Mesh->KinematicBonesUpdateType = InOverride;
        }
    }

    ~FScopedMeshBoneUpdateOverride()
    {
        if (Mesh)
        {
            Mesh->KinematicBonesUpdateType = SavedSetting;
        }
    }

private:
    USkeletalMeshComponent* Mesh = nullptr;
    EKinematicBonesUpdateToPhysics::Type SavedSetting;
};

// 이동 틱에서 사용
void UMyMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    // 이동 처리 동안 1차 OnUpdateTransform의 피직스바디 갱신을 원천 스킵
    FScopedMeshBoneUpdateOverride ScopedNoMeshBoneUpdate(Mesh, EKinematicBonesUpdateToPhysics::SkipAllBones);

    PerformMovement(DeltaTime); // 또는 SafeMoveUpdatedComponent / RootMotion
}
```

---

## 2. 이동 서브스텝 중복 오버랩 및 물리 전파 병목

### 2.1 병목 메커니즘
`CharacterMovementComponent`가 지면을 걷거나 장애물을 미끄러질 때(`SlideAlongSurface`), 계단을 오를 때(`StepUp`) 한 틱 안에서 `MoveUpdatedComponent`가 2~4회 이상 분할 호출됩니다.

- 매 분할 호출마다 `UpdateOverlaps()`가 실행되어 주변 콜리전과의 오버랩을 전수 검사합니다.
- 매 스텝마다 `PropagateTransformUpdate`가 호출되어 자식 컴포넌트들의 물리 바디 인스턴스(`BodyInstance`)를 개별적으로 동기화합니다.
- 100마리 군중이 밀집 이동할 경우, 프레임당 수백~수천 회의 불필요한 중간 오버랩/물리 질의가 발생합니다.

### 2.2 해결 패턴: `FScopedMovementUpdate`
언리얼 엔진의 `FScopedMovementUpdate`를 사용하면 스코프가 끝날 때까지 오버랩 검사, 바운드 갱신(`UpdateBounds`), 자식 트랜스폼 전파를 지연시키고 최종 도달 위치에서 단 1회만 처리합니다.

```cpp
void UMyMovementComponent::PerformComplexMovement()
{
    // 이동 스코프 생성: 내부의 모든 이동 연산의 오버랩/물리 전파를 지연
    FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, EScopedUpdate::DeferredUpdates);

    // 1단계: 전방 이동
    MoveUpdatedComponent(DeltaMove1, NewRotation, true);

    // 2단계: 표면 슬라이딩
    SlideAlongSurface(DeltaMove2, TimeRemaining, Normal, Hit);

    // 3단계: 계단 스텝업
    StepUp(GravDir, StepDownDelta, Hit);

    // 스코프가 소멸하면서 최종 위치에서 1회만 UpdateOverlaps 및 트랜스폼 확정!
}
```

---

## 3. 병렬 애니메이션 평가 중 게임 스레드 블로킹

### 3.1 병목 메커니즘
UE5는 스켈레탈 메시의 애니메이션 그래프 평가를 워커 스레드(`FParallelAnimationEvaluationTask`)에 비동기로 분산합니다.
하지만 게임 스레드에서 아직 워커 스레드가 작업 중인 스켈레탈 메시의 데이터를 직접 조회하면 엔진 내부에서 강제 대기(Stall)가 발생합니다.

- 게임 스레드에서 `USkeletalMeshComponent::GetSocketTransform()`, `GetBoneTransform()`, `GetBoneMatrix()` 호출
- 게임 스레드에서 `UAnimInstance::GetProxyOnGameThread()` 호출
- 위 함수들은 내부적으로 `HandleExistingParallelEvaluationTask(true)`를 트리거하며, 이는 `FTaskGraphInterface::Get().WaitUntilTaskCompletes()`로 메인 스레드를 정지시킵니다.
- 프로파일러 상에서 `USkeletalMeshComponent::BlockOnParallelEvaluationTask` 스파이크가 나타나며 멀티스레딩 효과가 상실됩니다.

### 3.2 해결 패턴
1. **소켓/본 트랜스폼 조회를 `TG_PostPhysics` 틱 그룹으로 지연**:
   - `TickGroup = TG_PostPhysics` 컴포넌트에서 소켓/본을 조회하면 이미 병렬 평가 및 포즈 완료가 끝난 상태이므로 블로킹이 발생하지 않습니다.
2. **이동 완료 이벤트 시점 캐싱**:
   - `Character->OnCharacterMovementUpdated` 델리게이트 등 애니메이션 틱 이후 시점에 필요한 본 위치를 단 1회 읽어서 구조체에 캐싱해 둡니다.
3. **AnimGraph 내부 Fast-Path 보장**:
   - AnimGraph에서 참조하는 C++ 헬퍼 함수는 반드시 `meta = (BlueprintThreadSafe)`를 선언하여 게임 스레드로 복귀하지 않고 워커 스레드 내에서 직접 변수를 읽도록 만듭니다.

---

## 4. 다중 파츠 스켈레탈 메시의 중복 애님 평가

### 4.1 병목 메커니즘
모듈형 캐릭터(베이스 바디 + 상의 + 하의 + 헬멧 + 장갑) 구현 시, 각 파츠를 `USkeletalMeshComponent`로 만들어 부착하고 동일한 AnimInstance나 복사 노드를 돌리면:
- 파츠 수(5개)만큼 매 프레임 독립적인 애니메이션 그래프 평가, 본 행렬 계산, 스키닝 연산이 중복 실행됩니다.
- 10마리 캐릭터만 있어도 50개의 스켈레탈 메시가 CPU를 점유합니다.

### 4.2 해결 패턴: `SetLeaderPoseComponent`
동일한 스켈레톤 본 구조를 공유하는 파츠 메시는 `SetLeaderPoseComponent`(구 MasterPoseComponent)를 설정합니다.

```cpp
void AMyCharacter::AttachModularPart(USkeletalMeshComponent* ClothingMesh)
{
    ClothingMesh->SetupAttachment(GetMesh());

    // 자식 메시는 자신의 AnimInstance를 전혀 돌리지 않고, 리더 메시의 본 행렬을 그대로 참조
    ClothingMesh->SetLeaderPoseComponent(GetMesh(), true, true);
}
```
- 자식 메시는 CPU 애니메이션 틱(`TickPose`)과 그래프 계산을 100% 생략합니다.
- 렌더 스레드/GPU 스키닝 단계에서 리더의 본 트랜스폼 버퍼를 직접 공유하므로 CPU 비용이 0으로 수렴합니다.

---

## 5. 군중 AI 무브먼트의 불필요한 물리 상호작용 부하

### 5.1 병목 메커니즘
`UCharacterMovementComponent`의 기본 프로퍼티에는 `bEnablePhysicsInteraction = true`가 설정되어 있습니다.
- 캐릭터가 걸을 때마다 매 틱 `ApplyDownwardForce`가 실행되어 바닥 물리 바디에 충격을 가합니다.
- 다른 오브젝트나 액터와 스칠 때마다 `ApplyImpactPhysicsForces`, `ApplyRepulsionForce`가 호출되어 물리 씬(Chaos)에 락을 걸고 힘을 주입합니다.
- 50~100마리의 군중 NPC가 서로 뭉치거나 바닥을 걸을 때 이 물리 상호작용 연산이 누적되어 거대한 물리 스레드 락 및 게임 스레드 지연을 발생시킵니다.

### 5.2 해결 패턴
플레이어가 조종하는 캐릭터를 제외한 모든 AI, 몬스터, 군중 캐릭터의 무브먼트 컴포넌트에서는 물리 상호작용을 완전히 끕니다.

```cpp
// AI 캐릭터 무브먼트 생성자 또는 BeginPlay
UCharacterMovementComponent* MoveComp = GetCharacterMovement();
if (MoveComp)
{
    MoveComp->bEnablePhysicsInteraction = false; // 물리 씬 임펄스 주입 완전 차단
    MoveComp->bPushForceScaledToMass = false;
    MoveComp->bScalePushForceToVelocity = false;
}
```

---

## 6. 스켈레탈 메시의 본 단위 오버랩 스팸

### 6.1 병목 메커니즘
기본 설정의 `USkeletalMeshComponent`에 피직스 애셋이 있고 `bGenerateOverlapEvents = true`인 경우:
- 캐릭터가 이동하거나 애니메이션으로 본이 회전할 때마다, 피직스 애셋에 정의된 수십 개의 본 바디 셰이프들이 월드 내의 트리거 볼륨, 물, 기타 오버랩 대상과 충돌 검사(`UpdateOverlaps`)를 수행합니다.
- 캡슐 컴포넌트 하나로만 오버랩을 받아도 충분한 게임플레이 환경에서 엄청난 쿼리 낭비가 일어납니다.

### 6.2 해결 패턴
스켈레탈 메시의 오버랩 이벤트를 차단하고, 충돌/트리거 판정은 오직 루트 캡슐 컴포넌트(`UCapsuleComponent`)에만 일원화합니다.

```cpp
GetMesh()->SetGenerateOverlapEvents(false); // 스켈레탈 메시 본 단위 오버랩 전면 비활성화
GetCapsuleComponent()->SetGenerateOverlapEvents(true); // 캡슐만 오버랩 감지
```

---

## 7. URO(업데이트 레이트 최적화)와 루트 모션 충돌 트랩

### 7.1 병목 메커니즘
원거리 캐릭터 최적화를 위해 스켈레탈 메시의 URO(`bEnableUpdateRateOptimizations = true`)를 켜서 2~5프레임마다 한 번씩 포즈를 갱신하게 설정했을 때, **루트 모션(Root Motion)이 포함된 몽타주를 재생하면 심각한 버그**가 발생합니다.

- URO가 틱을 건너뛰는 프레임에서는 루트 본의 변위 추출(`ConsumeRootMotion`)이 0이 되거나, 보간 프레임에서 비정상적으로 왜곡된 델타 벡터가 반환됩니다.
- 이로 인해 캐릭터가 순간이동하거나, 지면 아래로 꺼지거나, 공중으로 솟구치는 현상이 일어납니다.

### 7.2 해결 패턴: 상태 기반 URO 가드
루트 모션을 소비하는 어빌리티나 몽타주가 실행 중일 때는 URO를 동적으로 끄고, 완료 시 다시 켭니다.

```cpp
void AMyCharacter::OnMontageStarted(UAnimMontage* Montage)
{
    if (Montage && Montage->HasRootMotion())
    {
        // 루트모션 몽타주 재생 중에는 URO 임시 비활성화
        GetMesh()->bEnableUpdateRateOptimizations = false;
    }
}

void AMyCharacter::OnMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    if (Montage && Montage->HasRootMotion())
    {
        // 몽타주 종료 후 거리 기반 URO 복원
        GetMesh()->bEnableUpdateRateOptimizations = true;
    }
}
```

---

## 8. 화면 밖 액터의 몽타주/노티파이 정지 트랩

### 8.1 병목 메커니즘
스켈레탈 메시 렌더링 비용을 줄이기 위해 `VisibilityBasedAnimTickOption`을 `OnlyTickPoseWhenRendered`로 설정하면:
- 카메라 시야 밖(화면 뒤, 벽 너머)으로 나간 액터는 애니메이션 틱과 포즈 갱신이 완전히 멈춥니다.
- 만약 해당 액터가 패턴 공격 몽타주를 실행 중이었다면, **몽타주 종료 노티파이(`AnimNotify`)나 브랜칭 포인트가 발송되지 않아 AI 상태 머신이 영구적으로 멈추는 데드락**에 빠집니다.
- 루트 모션을 쓰는 보스 몬스터가 화면 밖에서 제자리에 멈춰 서 있는 원인이 됩니다.

### 8.2 해결 패턴
1. **공격/스킬을 사용하는 전투 액터**:
   - `OnlyTickMontagesWhenNotRendered` 사용: 화면 밖에서는 뼈대 렌더링/스킨 포즈 계산은 끄되, 몽타주 재생 시간과 노티파이 이벤트만 진행시켜 게임플레이 로직 연속성을 보장합니다.
2. **단순 군중/배경 NPC**:
   - 상태 머신이나 전투 로직이 없는 단순 군중만 `OnlyTickPoseWhenRendered`를 적용합니다.

---

## 9. 실전 적용 체크리스트

| 최적화 대상 | 점검 항목 및 권장 설정 | 기대 효과 |
|---|---|---|
| **이동 중 본 물리 갱신** | `FScopedMeshBoneUpdateOverride`로 이동 틱 동안 `SkipAllBones` 적용 | 이동 시 이전 포즈 기준 헛수고 물리 갱신 1회 제거 (물리 동기화 비용 50% 절감) |
| **이동 서브스텝** | 복수 이동 연산 구간을 `FScopedMovementUpdate`로 래핑 | 계단 오르기/표면 슬라이딩 시 중간 오버랩 및 바운드 갱신 제거 |
| **병렬 애니메이션 평가** | 게임 스레드의 소켓/본 직접 조회를 `TG_PostPhysics`로 지연 | `BlockOnParallelEvaluationTask` 메인 스레드 정지(Stall) 완전 차단 |
| **모듈형 파츠 메시** | 동일 스켈레톤 파츠에 `SetLeaderPoseComponent` 적용 | 자식 메시의 CPU 애니메이션 그래프 연산 100% 제거 |
| **AI 물리 상호작용** | AI/군중의 `CharacterMovementComponent->bEnablePhysicsInteraction = false` | 수백 개 AI의 불필요한 Chaos 물리 씬 임펄스 주입 제거 |
| **메시 오버랩** | `SkeletalMeshComponent->SetGenerateOverlapEvents(false)` | 본 수십 개 단위의 불필요한 월드 오버랩 전수 질의 제거 |
| **루트 모션** | 루트모션 실행 시 `bEnableUpdateRateOptimizations = false` 동적 해제 | URO 프레임 스킵으로 인한 이동 왜곡 및 공중 솟구침 방지 |
| **시야 밖 애니메이션** | 전투 AI는 `OnlyTickMontagesWhenNotRendered`로 설정 | 시야 밖에서 노티파이 누락으로 인한 AI 상태 머신 멈춤 방지 |

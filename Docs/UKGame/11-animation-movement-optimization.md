[← 인덱스로](../UKGame_FeatureFileMap.md)

# 11. 애니메이션 · 캐릭터 이동 최적화

`C:\Project\UKGame` 프로젝트의 애니메이션 및 캐릭터 이동 관련 최적화 코드 분석 결과입니다(2026-09-09 기준).  
기존 문서 양식에 맞춰 **파일 경로 - 기능 설명** 구조로 정리되었습니다.

---

## 1. 중요도 기반 틱 및 URO 최적화 (Significance & URO)

### 액터 중요도 관리 컴포넌트
- 파일: `Source/UKGame/Components/UKSignificanceComponent.h/.cpp`
- 기능:
  - 언리얼 엔진의 `USignificanceManager`를 활용하여 뷰포인트(플레이어 카메라)와의 거리 및 FOV 각도(내적)를 바탕으로 객체 중요도(`Significance`)를 0~3단계로 산출
  - **컴포넌트 틱 간격 동적 제어 (`SetComponentTickInterval`)**:
    - 중요도 단계에 따라 `UCharacterMovementComponent`, `USkeletalMeshComponent`, `UPathFollowingComponent`, `UBehaviorTreeComponent`, `UStateTreeComponent`, `UHTNComponent`, `UUKRagdollComponent`, `UUKCapsuleModifierComponent`의 틱 간격을 점진적으로 완화(기본 0초 → 0.033초 → 0.116초 → 0.333초)
    - 최원거리 도달 시 `SetTickEnabled(false)`로 주요 틱을 완전 중단
  - **스켈레탈 메시 URO (Update Rate Optimizations) 제어**:
    - `bDistanceURO` 옵션을 통해 메시의 `bEnableUpdateRateOptimizations = true` 활성화
    - `AnimUpdateRateParams->bShouldUseLodMap = true` 지정으로 메시 LOD와 갱신율 연동
    - `MaxEvalRateForInterpolation = 10` 설정 (1프레임 평가 후 최대 9프레임 건너뛰며 보간)
    - `BaseNonRenderedUpdateRate = 6` 설정 (화면 밖 비렌더링 메시 갱신율 제한)
    - 중요도별 `AnimationFrameSkipCount` 설정 (`LODToFrameSkipMap`)
  - 플레이어가 AI 시야 내에 들어올 경우(`UKAIPerceptionComponent::GetVisiblePlayers`) 즉시 최고 중요도(0단계)로 복귀하여 반응성 보장

### AI 캐릭터 및 소형 동물 자동 최적화 등록
- 파일: `Source/UKGame/AI/UKAICharacter.h/.cpp` , `Source/UKGame/Actors/SmallAnimal/UKSmallAnimalCharacter.h/.cpp`
- 기능:
  - 생성자에서 `UUKSignificanceComponent`를 기본 서브오브젝트로 생성 및 부착
  - 모든 몬스터/NPC 및 월드 동물 액터가 스폰 시 자동으로 전역 중요도 최적화 파이프라인에 편입되도록 구성

---

## 2. 대규모 군중 틱 예산 분배 및 컬링 (Crowd Tick Budgeting)

### 군중 NPC 틱 예산 관리 서브시스템
- 파일: `Source/UKGame/Subsystems/NPC/UKNPCUpdateManager.h/.cpp`
- 기능:
  - 대규모 NPC 군중의 이동/애니메이션 틱을 일괄 스케줄링하는 월드 서브시스템 (`UTickableWorldSubsystem`)
  - 등록 액터의 자체 엔진 틱을 강제 해제(`InActor->SetActorTickEnabled(false)`)하여 언리얼 틱 오버헤드 원천 차단
  - **2단계 우선순위 분할 및 예산 관리 (Tick Budgeting)**:
    - **VIP 그룹 (`HighPriorityGroup`)**: 플레이어와 가장 가까운 10마리는 프레임 예산과 무관하게 매 프레임 무조건 `ManualTick` 실행
    - **저우선순위 그룹 (`LowPriorityGroup`)**: 남은 원거리 NPC들은 지정된 프레임 시간 예산(`FrameBudgetMs = 8.0ms`) 내에서 라운드로빈 커서(`LowGroupCursor`)를 회전하며 순차 갱신
    - 예산 초과로 이번 프레임에 실행되지 못한 액터는 누적 델타 타임(`AccumulatedTime`)을 다음 틱으로 이월하여 물리/이동 연속성 보장
  - 0.5초 주기(`RebalanceInterval`)로만 거리를 재계산하여 그룹을 재배치함으로써 정렬 비용 최소화
  - 초기 등록 시 랜덤 오프셋(`FRandRange(0, 0.05)`)을 부여하여 특정 프레임에 갱신이 집중되는 CPU 스파이크 방지

### 수동 틱 인터페이스
- 파일: `Source/UKGame/Subsystems/NPC/UKNPCUpdateInterface.h`
- 기능:
  - 서브시스템의 예산 분배에 따라 호출되는 표준화된 수동 틱 인터페이스 (`ManualTick(float DeltaTime)`)

### 군중 캐릭터 및 비렌더링 메시 틱 스킵
- 파일: `Source/UKGame/AI/UKCrowdCharacter.h/.cpp` , `Source/UKGame/Subsystems/NPC/UKTestManualTickAI.h/.cpp`
- 기능:
  - `BeginPlay`에서 액터, 캐릭터 무브먼트, 스켈레탈 메시, AI 컨트롤러 틱을 일괄 정지하고 `UKNPCUpdateManager`에 등록
  - **화면 렌더링 기반 메시 틱 스킵 (Frustum Culling)**:
    - `ManualTick` 실행 시 `GetMesh()->WasRecentlyRendered(0.2f)`를 확인
    - 카메라 시야 내에 렌더링 중일 때만 스켈레탈 메시 컴포넌트 틱(`TickComponent`)을 호출하고, 시야 밖(화면 뒤, 차폐)인 경우 메시 애니메이션 연산을 스킵
    - `VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered` 지원

---

## 3. 캐릭터 무브먼트 연산 최적화 (Movement Component Caching)

### 지면 판정 프레임 캐싱 (Ground Info Caching)
- 파일: `Source/UKGame/Components/UKCharacterMovementComponent.h/.cpp`
- 기능:
  - `GetGroundInfo()`: `GFrameCounter == CachedGroundInfo.LastUpdateFrame` 검사를 수행하여 동일 프레임 내 중복 호출 시 캐시된 `FUKCharacterGroundInfo`를 즉시 반환
  - 한 프레임 내에서 애니메이션, 착지 판정, 낙하 데미지 등 여러 시스템이 지면 높이를 조회할 때 발생하는 반복적인 `LineTraceSingleByChannel` 레이캐스트 낭비를 제거
  - `SCENE_QUERY_STAT(LyraCharacterMovementComponent_GetGroundInfo)` 네임스페이스 통계 기반 쿼리 사용

### 포즈 틱 제어 및 루트모션 변경 감지 최소화
- 파일: `Source/UKGame/Components/UKCharacterMovementComponent.h/.cpp`
- 기능:
  - `TickComponent` 시작 시 `bTickCharacterPose = false`로 강제 설정하여 이동 컴포넌트에 의한 불필요한 포즈 틱을 억제
  - `bPreHasRootMotion != HasAnimRootMotion()` 상태 비교를 통해서만 루트모션 상태 전환 델리게이트(`OnChangeRootMotionState`)를 발송하여 불필요한 이벤트 브로드캐스트 방지

### 지면 데이터 구조체
- 파일: `Source/UKGame/GameFramework/UKCharacterMovementStructures.h`
- 기능:
  - `FUKCharacterGroundInfo`: `LastUpdateFrame`, `GroundHitResult`, `GroundDistance`를 묶어 프레임 단위 캐싱 지원

### 이동 중 본 피직스바디 갱신 차단 및 지연 갱신 (Kinematic Bones Update Override)
- 파일: `Source/UKGame/Components/AI/UKSimpleMovementComponent.h/.cpp`
- 기능:
  - **엔진 내부 이중 물리 갱신(Double Physics Body Update) 병목 원인**:
    1. **1차 갱신 (이동 시점 `USkeletalMeshComponent::OnUpdateTransform`)**:
       - `CharacterMovementComponent`가 캡슐을 이동시킬 때 자식인 스켈레탈 메시의 월드 트랜스폼이 변경되며 `OnUpdateTransform`이 트리거됨
       - 엔진은 메시가 이동했으므로 본에 연결된 피직스바디들을 월드 좌표로 옮기기 위해 `UpdateKinematicBonesToAnim(GetComponentSpaceTransforms(), ...)`을 호출
       - 이때는 아직 이번 프레임의 새 애니메이션 포즈 평가 전이므로, 이전 프레임의 낡은 본 포즈를 가지고 단순히 월드 위치만 이동시키는 헛수고 연산이 발생함
    2. **2차 갱신 (애니메이션 평가 후 시점 `USkeletalMeshComponent::PostAnimEvaluation`)**:
       - 스켈레탈 메시의 애니메이션 그래프 평가가 완료되면 새 포즈를 물리 씬에 반영하기 위해 `UpdateKinematicBonesToAnim(GetEditableComponentSpaceTransforms(), ...)`을 다시 호출함
       - 결과적으로 이동하고 애니메이션을 재생하는 캐릭터 1체당 프레임당 최소 2회의 피직스바디 물리 동기화(`STAT_UpdateRBBones`)가 중복 발생하여 군중 밀집 시 극심한 CPU 병목 유발
  - **최적화 해결책 (`FUKScopedMeshBoneUpdateOverride`)**:
    - `UUKSimpleMovementComponent::TickComponent` 이동 처리 시작 시 RAII 스코프 가드 `FUKScopedMeshBoneUpdateOverride ScopedNoMeshBoneUpdate(Mesh, EKinematicBonesUpdateToPhysics::SkipAllBones)` 적용
    - 이동 연산(`SafeMoveUpdatedComponent`, 루트모션 등)이 진행되는 동안 메시의 `KinematicBonesUpdateType`을 `SkipAllBones`로 임시 변경
    - 엔진 `UpdateKinematicBonesToAnim` 내부의 `if (!bUpdateKinematics) return;` 탈출 조건을 유도하여 **1차 이동 시점의 불필요한 피직스바디 갱신을 원천 차단**
    - 스코프 종료 시 원래 설정(`SkipSimulatingBones`)으로 자동 복원되며, 이후 2차 애니메이션 평가 시점(`PostAnimEvaluation`)에 최종 위치와 새 포즈가 단 1회만 물리 씬에 정상 반영됨
  - **이동 업데이트 지연 스코프 (`FUKScopedMeshMovementUpdate`, `FUKScopedCapsuleMovementUpdate`)**:
    - `FScopedMovementUpdate` 기반 `EScopedUpdate::DeferredUpdates`를 래핑하여 이동 도중 발생하는 물리·오버랩 이벤트를 누적했다가 최종 위치 도달 시 1회만 일괄 처리
  - **군중/AI 전용 경량화 이동 로직 (`SimpleWalkingUpdate`, `RootMotionUpdate`)**:
    - 표준 캐릭터 무브먼트의 무거운 물리 시뮬레이션 대신 단순 지면 스냅(`FindGround`) 및 저비용 평면 이동 루틴 제공

---

## 4. 메시 갱신 주기 제한 및 스켈레탈 메시 최적화 (Mesh Limiter & LOD)

### 메시 트랜스폼 갱신 제한 컴포넌트
- 파일: `Source/UKGame/Animation/UKMeshUpdateLimiterComponent.h/.cpp`
- 기능:
  - `USkinnedMeshComponent`의 트랜스폼 갱신 빈도를 고정 프레임율(`UpdatePerSecond`, 기본 30fps/90fps)로 제한
  - `TG_PostPhysics` 틱 그룹에서 실행되며, 부착 타겟(`ComponentToAttach`)의 본/소켓 트랜스폼과의 상대 변환을 역산(`Inverse`) 및 보정하여 프레임 레이트 저하 없이 위치를 정확히 유지
  - 오너 액터가 숨겨진 상태(`IsHidden()`)일 경우 계산 완전 스킵

### 캐릭터 스켈레탈 메시 컴포넌트 확장
- 파일: `Source/UKGame/Components/UKCharacterSkeletalMeshComponent.h/.cpp`
- 기능:
  - `SetMeshForcedLOD`: 본체 스켈레탈 메시뿐만 아니라 자식 컴포넌트(`GetChildrenComponents`)의 모든 스켈레탈 메시까지 재귀적으로 동일한 강제 LOD(`SetForcedLOD`)를 전파 적용하여 파츠 메시 분할 캐릭터의 드로우콜 및 버텍스 처리 최적화
  - `RuntimeAnimRateScale`: 엔진 전역 `GlobalAnimRateScale` 변수 오염 및 로직 충돌을 방지하기 위해 `TickPose` 전후로만 로컬 배속을 임시 스왑 적용
  - `CacheLinkedAnimInstances` / `RestoreAnimInstance`: 동적으로 연동되는 애님 레이어 클래스를 캐싱하여 런타임 재생성 비용 절감

---

## 5. 병렬 애니메이션 평가 및 Fast-Path (Thread-Safe Animation)

### 워커 스레드 애니메이션 평가
- 파일: `Source/UKGame/Animation/UKCharacterAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKPCAnimInstance.h/.cpp` , `Source/UKGame/Animation/UKMotionMatchingAnimInstance.h/.cpp`
- 기능:
  - 언리얼 엔진 5의 병렬 애니메이션 평가(Parallel Animation Evaluation)를 위해 메인 게임 스레드의 `NativeUpdateAnimation` 대신 워커 스레드에서 실행되는 `NativeThreadSafeUpdateAnimation` 적극 사용
  - 이동 데이터(`UpdateLocationData`, `UpdateRotationData`, `UpdateVelocityData`, `UpdateAccelerationData`) 및 로코모션 상태 머신 평가를 워커 스레드에서 비동기 병렬 처리하여 메인 스레드 병목 해소

### 스레드 세이프 로코모션 상태 머신
- 파일: `Source/UKGame/Animation/UKCharacterLocomotionState.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState_Grounded.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState_Falling.h/.cpp` , `Source/UKGame/Animation/UKCharacterLocomotionState_WallSlide.h/.cpp`
- 기능:
  - 상태 머신 로직을 별도 UObject로 분리하고 `OnNativeThreadSafeUpdateAnimation`을 통해 지상/낙하/벽슬라이드 이동 방향 및 속도 계산을 스레드 안전하게 수행

### 애님 그래프 Fast-Path 보장 인터페이스
- 파일: `Source/UKGame/Animation/UKCharacterAnimLayersBase.h/.cpp`
- 기능:
  - 애님 인스턴스 접근자(`GetUKCharacter`, `GetMainAnimInstance`, `GetCharacterMovementComponent` 등)에 `meta = (BlueprintThreadSafe)`를 지정하여 AnimGraph 평가 중 게임 스레드로의 컨텍스트 스위칭 없는 순수 Fast-Path 처리 보장

---

## 6. 모션 매칭 궤적 예측 최적화 (Trajectory Optimization)

### 이벤트 기반 궤적 컴포넌트
- 파일: `Source/UKGame/Animation/Trajectory/UKCharacterTrajectoryComponent.h/.cpp`
- 기능:
  - 모션 매칭용 과거/미래 이동 궤적(Trajectory) 생성 컴포넌트
  - **자체 틱 비활성화 (`PrimaryComponentTick.bCanEverTick = false`)**: 매 프레임 불필요한 폴링 틱 제거
  - `Character->OnCharacterMovementUpdated` 델리게이트를 바인딩하여 실제 이동 입력/변위가 발생했을 때만 이벤트 기반으로 갱신
  - **프레임 중복 연산 방지 가드**: `LastUpdateFrameNumber == GFrameNumber` 검사를 통해 서브스텝이나 다중 이벤트 호출 시 동일 프레임 내 중복 궤적 연산을 즉시 조기 반환(`return`)
  - Z축 이동 발판(`GetMovementBase`) 탑승 여부를 확인하여 불필요한 발 착지 예측(`UpdateFeetPrediction`) 조기 스킵

### 궤적 샘플링 라이브러리
- 파일: `Source/UKGame/Animation/Trajectory/UKCharacterTrajectoryLibrary.h/.cpp`
- 기능:
  - 궤적 히스토리 압축 변환 및 시뮬레이션 기반 예측 수학 함수 집합

---

## 7. 대규모 경로 탐색 최적화 (Hierarchical Pathfinding A*)

### 계층적 네비게이션 매니저
- 파일: `Source/UKGame/Subsystems/NavigationManager/UKNavigationManager.h/.cpp`
- 기능:
  - 대규모 오픈월드에서 장거리 이동 시 네비메시 A*를 직접 탐색할 때의 심각한 CPU 병목을 방지하는 HPA* (Hierarchical Pathfinding A*) 서브시스템
  - 월드를 클러스터 단위로 분할하고 입구(Entrance) 간의 매크로 경로를 선행 계산한 후 세부 경로를 질의하여 탐색 범위 대폭 축소
  - `UCrowdFollowingComponent`와 결합하여 대규모 군중의 회피 및 최적 이동 경로 처리

### HPA 데이터 구조 및 계층 해시 그리드
- 파일: `Source/UKGame/Navigation/UKNavigationDefine.h/.cpp` , `Source/UKGame/Navigation/UKWayPoint.h/.cpp`
- 기능:
  - `FHPACluster`, `FHPAEntrance` 구조체를 통해 클러스터 간 전이 비용 캐싱
  - 2D 계층 해시 그리드(`HierarchicalHashGrid2D`)를 사용하여 반경 내 최근접 웨이포인트 검색을 $O(1)$에 가깝게 최적화

---

## 8. 대규모 군집 이동 ECS 시스템 (Mass Agent Flocking & Hash Grid)

### ECS 에이전트 회피 이동 시스템
- 파일: `Source/UKGame/Subsystems/ECS/UKECSManager.h/.cpp` , `Source/UKGame/Subsystems/ECS/UKECSSystemBase.h/.cpp`
- 기능:
  - 대량의 에이전트 이동 시 언리얼 액터/컴포넌트 오버헤드를 배제하고 데이터 지향 ECS 아키텍처로 일괄 틱 처리
  - `UECSAvoidMove`: 최단 접근 시간(`ComputeClosestPointOfApproach`), 예측 회피력(`CalculatePredictiveAvoidanceForce`), 분리력(`CalculateSeparationForce`)을 순수 수학 연산으로 처리하여 물리 시뮬레이션 없이 자연스러운 군집 회피 구현

### 에이전트 공간 해시 그리드 매니저
- 파일: `Source/UKGame/Subsystems/ECS/UKAgentHashGridManager.h/.cpp`
- 기능:
  - 월드 공간을 셀 단위 그리드로 분할하여 주변 에이전트 질의를 무거운 물리 충돌 검사(Sweep/Overlap) 없이 해시 룩업으로 즉시 해결

---

## 9. GPU 버텍스 애니메이션 텍스처 (Vertex Animation Texture - VAT)

### VAT 인스턴스 렌더링 및 런타임 제어
- 파일: `Plugins/VertexAnimationManager/Source/VertexAnimationManager/Public/VAISMController.h` , `VAHISMController.h` , `VARuntimeComponent.h` , `VAWorldSubsystem.h`
- 기능:
  - 스켈레탈 메시의 CPU 본 트랜스폼 연산과 스키닝 비용을 완전히 제거하고, GPU 버텍스 셰이더에서 텍스처를 샘플링하여 애니메이션 재생
  - 인스턴스 스태틱 메시(ISM / HISM)와 결합하여 원거리 군중이나 대규모 배경 액터를 단일 드로우콜 수준으로 렌더링하여 애니메이션 CPU 비용을 0으로 최적화

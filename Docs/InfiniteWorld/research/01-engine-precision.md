# 01. 원점에서 먼 좌표의 엔진 정밀도 한계와 '실용적 무한' 안전 반경 (UE 5.8)

작성: 2026-10-23, 조사 갈래 1 (엔진 소스 직접 확인). 표기: [검증] = 원문을 직접 읽음, [추정] = 원문 미확인 또는 추론.
엔진 경로 접두어 `E:` = `C:/Program Files/Epic Games/UE_5.8/Engine/`, 프로젝트 경로는 저장소 기준.

## 0. 결론 (먼저)

1. 엔진의 '세계 가장자리'는 제약이 아니다. 기본 `WORLD_MAX` = 8,796,093,022,208 cm(가로 약 8,796만 km), 경계 검사는 원점에서 약 4,398만 km에서 일어난다. 100시간 걸어도 2,160 km이므로 도달 불가. [검증]
2. 시뮬레이션 계열(Chaos 물리, Recast/Detour 내비, Mover, PCG 점, GAS 대상 데이터, 오디오, 월드 파티션 격자, 네트워크 직렬화)은 모두 double 이거나 큰 값에서 전체 정밀도로 물러난다. 2,160 km 에서 double 최소 단위는 약 3×10⁻⁸ cm. [검증]
3. 렌더링·나이아가라는 '타일(2,097,152 cm ≈ 20.97 km) + float 오프셋' 또는 카메라 상대 좌표로 큰 좌표를 처리한다. 위험은 머티리얼에서 큰 좌표를 float 로 떨어뜨리는(demote) 경로와, 나이아가라 월드 공간 시스템이 오래 살아 타일 경계를 넘는 경우에만 남는다. [검증, 일부 추정]
4. 실제 '단단한 한계'는 정밀도가 아니라 int32 다: 원점 재설정 좌표 `FIntVector`(±21,474.8 km), PCG 위치 기반 시드의 `static_cast<int>`(같은 ±21,474.8 km). [검증]
5. 월드 원점 재설정(`SetNewWorldOrigin`)은 5.8 에 남아 있지만 이 프로젝트 구성(월드 파티션 + Chaos + Mover)과 맞지 않는다: 자동 재설정 설정은 월드 파티션 맵에서 숨겨지고, 월드 파티션 코드는 원점 이동을 모르며, Chaos 는 원점 이동을 지원하지 않고, Mover 는 원점 이동 처리기가 없다. 쓰지 않는 것을 권한다. [검증 + 결과 동작은 추정]
6. 권장 정책: 원점 재설정 없이 double 월드 좌표를 그대로 쓰고, 설계 상한을 축마다 ±10,000 km(10⁹ cm)로 둔다(일직선 보행 약 463시간, int32 한계의 절반 이하 여유). float 를 쓰는 모든 코드(셰이더·나이아가라·프로젝트 GPU 코드)는 반경 10.49 km(2²⁰ cm) 이내의 지역 원점 기준 좌표만 받게 한다.

## 1. 큰 월드 좌표(LWC, Large World Coordinates) 상수와 경계 밖 동작

| 항목 | 값 | 근거 |
|---|---|---|
| `UE_OLD_WORLD_MAX` | 2,097,152 cm (UE4 시절) | `E:Source/Runtime/Engine/Public/EngineDefines.h:37` [검증] |
| `UE_LARGE_WORLD_MAX` | 8,796,093,022,208 cm (2⁴³), 주석: 원점에서 약 4,398만 km | `EngineDefines.h:41` [검증] |
| `UE_USE_UE4_WORLD_MAX` | 기본 0 → `WORLD_MAX = UE_LARGE_WORLD_MAX` | `EngineDefines.h:45-54` [검증] |
| `HALF_WORLD_MAX` | `WORLD_MAX*0.5` = 4.398×10¹² cm | `EngineDefines.h:56` [검증] |
| `UE_FLOAT_HUGE_DISTANCE` | 1,048,576 cm (10.49 km): float 로 1/16 cm 정밀도를 지키는 최대 거리 | `EngineDefines.h:59` [검증] |
| `UE_DOUBLE_HUGE_DISTANCE` | 562,949,953,421,312 cm: double 로 1/16 cm 를 지키는 최대 거리 | `EngineDefines.h:60` [검증] |
| 월드 설정 `bEnableLargeWorlds` | 폐기(DEPRECATED), 메시지 "5.1부터 모든 월드가 큼" | `E:Source/Runtime/Engine/Classes/GameFramework/WorldSettings.h:1087-1088` [검증] |
| 설정 변경 방법 | `EngineDefines.h` 의 `UE_USE_UE4_WORLD_MAX` 를 바꾸는 엔진 수정뿐(런처 설치판에서는 사실상 불가) | `EngineDefines.h:45-49` [검증], 설치판 제약은 [추정] |

경계 밖 동작 — `AActor::CheckStillInWorld` (`E:Source/Runtime/Engine/Private/Actor.cpp:2300-2350`) [검증]:
- 권한(authority) 또는 비네트워크 액터만 검사(2313), 월드 설정의 경계 검사 활성 여부(`AreWorldBoundsChecksEnabled`)가 꺼져 있으면 통과(2321).
- `Z < KillZ` 이면 `FellOutOfWorld`(2326-2330).
- 루트 컴포넌트 경계 상자가 `±HALF_WORLD_MAX` 를 벗어나면 경고 로그 → `OutsideWorldBounds()` 호출 → 충돌 끄기·물리 시뮬레이션 끄기(2336-2345). `OutsideWorldBounds` 의 기본 동작이 액터 파괴인지는 원문 미확인 [추정: 기본 구현은 Destroy].
- 결론: 경계는 4,398만 km 이므로 실용적 무한 설계에서 고려 대상이 아니다. KillZ 만 지형 최저 높이 아래로 두면 된다.

## 2. 하위 시스템별 정밀도

| 하위 시스템 | 좌표 표현 | 근거 | 2,160 km 판정 | 대책 |
|---|---|---|---|---|
| Chaos 물리 | `Chaos::FReal = double`. 솔버 바디 위치 `X`·`P` 는 `FVec3`(double), 보정량 `DP`·`DQ` 만 `FSolverReal`(저정밀 빌드 옵션이면 float, 상대값) | `E:Source/Runtime/Experimental/ChaosCore/Public/Chaos/Real.h:13-22`, `E:Source/Runtime/Experimental/Chaos/Public/Chaos/Evolution/SolverBody.h:37-43, 473-505` [검증] | 안전 | 없음 |
| Chaos 장면 질의 | 가속 구조 AABB(축 정렬 경계 상자) 트리 잎이 `FReal` | `E:Source/Runtime/Experimental/Chaos/Public/Chaos/AABBTree.h:583` [검증] | 안전 | 없음 |
| Chaos 원점 이동 | `SupportsOriginShifting()` 이 `false`, `ApplyWorldOffset` 은 `check(InOffset.Size()==0)` | `E:Source/Runtime/Engine/Private/PhysicsEngine/Experimental/PhysScene_Chaos.cpp:2262-2270` [검증] | 원점 이동 시 바디마다 순간이동으로 처리(`SceneComponent.cpp:3721-3722`) [검증] | 원점 재설정 회피 |
| 렌더링 | 타일 크기 `UE_LWC_RENDER_TILE_SIZE = 2,097,152` cm, 위치 = 정수 타일 + float 오프셋(타일 중심 기준, 최대 ±10.49 km → 오프셋 최소 단위 ≤ 1/16 cm) | `E:Source/Runtime/Core/Public/Misc/LargeWorldRenderPosition.h:14, 30-33, 73-78` [검증] | 안전 | 없음. 카메라 상대(translated world) 공간 렌더링은 [추정](SceneView 원문 미확인) |
| 머티리얼: 절대 월드 위치 | LWC 형(WS 형, double-float 쌍)으로 유지. `Frac` 은 `WSFracDemote`(정밀도 보존 후 float 화) | `E:Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp:5779-5783` [검증] | 안전(정밀도 보존 노드만 쓸 때) | 월드 정렬 텍스처는 나눗셈·`Frac` 을 LWC 상태에서 먼저 하도록 구성 |
| 머티리얼: 암묵적 float 변환 | LWC → float 형 변환은 `WSDemote(...)` 로 정밀도 손실(절대 float 와 같아짐) | `HLSLMaterialTranslator.cpp:4937-4938` [검증], 손실 크기는 [추정] | 위험: 100 km 에서 1 cm, 2,160 km 에서 16 cm 단위로 계단화 → 사인·커스텀 HLSL·바람(WPO, World Position Offset)·월드 좌표 UV 를 그대로 텍스처에 넣으면 떨림·번짐 | 카메라 상대 위치 또는 청크 지역 위치(절대 위치 − 청크 원점 매개변수)를 입력으로 사용 |
| 머티리얼: Noise 노드 | LWC 위치를 `abs(65536 × DFFracDemote(위치/65536))` 로 접어서 사용 → 655.36 m 마다 거울 반복 | `E:Shaders/Private/Common.ush:1836-1840` [검증] | 정밀도는 안전, 대신 655.36 m 주기 거울 이음매가 생김 | 탑다운 지면 노이즈는 청크 시드 기반 텍스처나 자체 해시 노이즈로 대체, 또는 카메라 상대 입력은 월드로 되돌려지므로(11994-11998) 사용 금지 |
| 나이아가라 | 시스템별 LWC 타일을 활성화 시 부착 컴포넌트 위치로 계산, 셰이더 매개변수 `EngineLWCTile`(W = 타일 크기) | `E:Plugins/FX/Niagara/Source/Niagara/Private/NiagaraSystemInstance.cpp:828-830, 2203-2204` [검증] | 짧은 효과 안전. 플레이어에 붙어 오래 사는 월드 공간 시스템이 타일 경계(20.97 km)를 넘으면 `ResetSimulation`/`Rebase`/`RebaseOrResetSimulation` 중 하나가 일어남(Rebase 는 데이터 인터페이스·행렬 속성을 못 옮긴다고 명시) | `E:Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraSystem.h:213-233` [검증]. 오래 사는 효과는 로컬 공간으로 |
| Mover(이동) | 동기 상태 `FVector Location`(double). 네트워크 직렬화 `SerializePackedVector<100,30>` → 현행 경로는 `SerializeQuantizedVector<100>`(0.01 cm, 범위 초과 시 전체 정밀도) | `E:Plugins/Experimental/Mover/Source/Mover/Public/MoverDataModelTypes.h:154`, `.../Private/MoverDataModelTypes.cpp:207`, `E:Source/Runtime/Engine/Classes/Engine/NetSerialization.h:264-271`, `E:Source/Runtime/Net/Core/Private/Net/Core/Serialization/QuantizedVectorSerialization.cpp:46-105` [검증] | 안전 | 없음 |
| Mover 와 원점 이동 | 매 프레임 상태의 월드 위치로 `UpdatedComponent->SetWorldTransform` 을 덮어씀. 엔진 플러그인 중 `ApplyWorldOffset`/원점 이동 대리자를 다루는 곳은 7개 파일(나이아가라·케이블·가상 높이장 메시·Avalanche)뿐이고 Mover·네트워크 예측·PCG 는 없음 | `E:Plugins/Experimental/Mover/Source/Mover/Private/MoverComponent.cpp:965-991` [검증], 플러그인 전체 검색 [검증] | 원점 재설정 시 다음 프레임에 옛 절대 좌표로 되돌아갈 가능성 높음 [추정] | 원점 재설정 회피 |
| 애니메이션(UAF) | 뼈 변환은 지역·컴포넌트 공간, 루트 모션은 상대량 | 원문 미확인 [추정] | 안전 [추정] | 없음 |
| 오디오 | 공간화 매개변수 `ListenerPosition`·`EmitterPosition`(청자 상대)·`EmitterWorldPosition` 모두 `FVector`(double), 거리만 float(상대값) | `E:Source/Runtime/AudioExtensions/Public/IAudioExtensionPlugin.h:68-98` [검증] | 안전 | 없음 |
| 내비게이션 Recast/Detour | `rcReal = double`, `dtReal = double`(비활성 매크로 기본 0) | `E:Source/Runtime/Navmesh/Public/Recast/RecastLargeWorldCoordinates.h:8-16`, `E:Source/Runtime/Navmesh/Public/Detour/DetourLargeWorldCoordinates.h:7-15` [검증] | 안전 | 없음 |
| `ARecastNavMesh` 원점 | `NavMeshOriginOffset` 속성 존재, 원점 이동 시 `dtNavMesh::applyWorldOffset` 호출 | `E:Source/Runtime/NavigationSystem/Public/NavMesh/RecastNavMesh.h:790`, `E:Source/Runtime/NavigationSystem/Private/NavMesh/PImplRecastNavMesh.cpp:3587-3595` [검증] | 안전 | 타일 색인 정수 범위·타일 풀 상한은 다른 갈래(동적 내비) 과제 [추정] |
| PCG 점 데이터 | `FPCGPoint::Transform` 은 `FTransform`(double) | `E:Plugins/PCG/Source/PCG/Public/PCGPoint.h:44` [검증] | 안전 | 없음 |
| PCG 위치 시드 | `ComputeSeedFromPosition` = `static_cast<int>(위치.X/Y/Z)` | `E:Plugins/PCG/Source/PCG/Private/Helpers/PCGHelpers.cpp:50-53` [검증] | 2,160 km 안전. ±2³¹ cm(21,474.8 km) 밖에서는 double→int 범위 초과(정의되지 않은 동작, x64 에서는 보통 최솟값 고정) → 시드 붕괴 [추정] | 설계 상한 ±10,000 km |
| GAS(게임플레이 능력 시스템) 대상 데이터 | `LiteralTransform` 은 `FTransform`, 벡터 페이로드 `FVector_NetQuantize100`, 적중 결과 위치는 `FVector_NetQuantize` | `E:Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/Abilities/GameplayAbilityTargetTypes.h:365, 698`, `E:Source/Runtime/Engine/Classes/Engine/HitResult.h:45-83` [검증] | 안전: 모든 `WritePackedVector` 는 `WriteQuantizedVector` 로 가고 범위 초과 시 전체 정밀도로 물러남(`NetSerialization.h:207-217`) [검증]. `NetQuantize` 의 배율 1(1 cm 반올림)은 이름 관례상 [추정] | 없음 |
| 월드 파티션 격자 | 셀 좌표 `FGridCellCoord = FInt64Vector3` | `E:Source/Runtime/Engine/Public/WorldPartition/WorldPartitionRuntimeSpatialHash.h:18` [검증] | 안전 | 런타임 생성 콘텐츠를 월드 파티션 셀로 넣을 수 있는지는 다른 갈래 과제 |
| 복제 이동(FRepMovement) | 송신 시 0 원점 기준으로, 수신 시 지역 원점으로 변환 | `E:Source/Runtime/Engine/Private/ActorReplication.cpp:280, 528` [검증] | 안전 | 없음 |

## 3. 월드 원점 재설정 (World Origin Rebasing)

- 남아 있음: `UWorld::RequestNewWorldOrigin`/`SetNewWorldOrigin` (`E:Source/Runtime/Engine/Private/World.cpp:8066-8170`), 레벨 틱에서 요청이 있으면 적용 (`E:Source/Runtime/Engine/Private/LevelTick.cpp:1631-1635`). [검증]
- 이동 대상: 렌더 장면, (지원 시) 물리 장면, 보이는 레벨, 내비게이션, 소유자 없는 컴포넌트, 선 그리기, 물리 장, 레벨 블루프린트 알림 (`World.cpp:8090-8166`). 숨은 서브레벨은 보일 때 이동. [검증]
- 원점 좌표형: `FIntVector OriginLocation`(int32) → 원점 자체를 옮길 수 있는 범위가 ±2³¹ cm = ±21,474.8 km (`E:Source/Runtime/Engine/Classes/Engine/World.h:2031-2037`). [검증]
- 자동 재설정은 월드 컴포지션 경로에서만 호출 (`E:Source/Runtime/Engine/Private/WorldComposition.cpp:759-761`). 설정 `bEnableWorldOriginRebasing` 은 `EditCondition="bEnableWorldComposition"`, `bEnableWorldComposition` 은 `EditCondition="WorldPartition == nullptr"` → 월드 파티션 맵에서는 편집기에서 켤 수 없음 (`WorldSettings.h:430-442`). [검증]
- 월드 파티션 쪽 코드(WorldPartition·StreamingSource·LevelStreaming 이름 파일)에는 `OriginLocation`·`ApplyWorldOffset`·rebase 언급이 0건 → 스트리밍 소스(원점 이동된 좌표)와 셀 경계(원래 좌표)가 어긋날 것 [검증: 0건 / 동작은 추정].
- Chaos 는 원점 이동 미지원(위 2절), Mover·네트워크 예측·PCG 는 원점 이동 처리기 없음(위 2절). [검증]
- 폐기 예정 표시: 읽은 구현 줄에는 폐기 표식이 없음. 헤더 선언의 `UE_DEPRECATED` 여부는 미확인 [추정: 폐기 표식 없음, 다만 월드 컴포지션과 함께 사실상 유지보수 모드].
- 멀티플레이: 복제 이동은 0 원점 ↔ 지역 원점 변환을 거치므로 클라이언트별 원점은 가능. 서버는 하나의 원점을 가지며 여러 클라이언트가 흩어진 무한 세계에서는 서버 쪽 정밀도를 원점 재설정으로 해결할 수 없다 [추정]. 그러나 LWC(double) 덕분에 서버도 원점 재설정이 필요 없다.
- 판정: 이 프로젝트(월드 파티션 + Chaos + Mover + 네트워크 예측)에서는 쓰지 않는다. 필요도 없다(2,160 km 에서 double 은 충분).

## 4. float32 최소 단위(ULP, Unit in the Last Place) 표

계산식: 거리 d 가 [2ᵏ, 2ᵏ⁺¹) cm 이면 float32 최소 단위 = 2ᵏ⁻²³ cm, 최대 반올림 오차 = 그 절반. 600 cm/s·60 프레임/초 → 프레임당 10 cm 이동 기준 속도 양자화 오차. (numpy `spacing` 으로 계산) [검증: 계산]

| 원점 거리 | float32 최소 단위 | 최대 오차 | 프레임당 10 cm 이동의 속도 오차 | double 최소 단위 | 비고 |
|---|---|---|---|---|---|
| 1 km | 0.0078 cm | 0.0039 cm | 0.08 % | 1.5×10⁻¹¹ cm | |
| 5.24~10.49 km (2¹⁹~2²⁰ cm) | 0.0625 cm | 0.031 cm | 0.6 % | 2.3×10⁻¹⁰ cm | 엔진 `UE_FLOAT_HUGE_DISTANCE` 경계 |
| 10.49~20.97 km | 0.125 cm | 0.0625 cm | 1.25 % | | 렌더 타일 반경 안 |
| 21.6 km (보행 1시간) | 0.25 cm | 0.125 cm | 2.5 % | 4.7×10⁻¹⁰ cm | |
| 41.9~83.9 km | 0.5 cm | 0.25 cm | 5 % | | |
| 83.9 km (2²³ cm)부터 | 1 cm | 0.5 cm | 10 % | 1.9×10⁻⁹ cm | 최소 단위 1 cm 시작 |
| 167.8 km (2²⁴ cm)부터 | 2 cm | 1 cm | 20 % | | 최대 오차 1 cm 시작 |
| 1,000 km | 8 cm | 4 cm | 80 % | 1.5×10⁻⁸ cm | |
| 2,160 km (보행 100시간) | 16 cm | 8 cm | 160 % (정지·순간이동처럼 보임) | 3.0×10⁻⁸ cm | |
| 21,474.8 km (2³¹ cm) | 128 cm | 64 cm | — | 2.4×10⁻⁷ cm | int32 cm 한계 |

하위 시스템별 '1 cm 이상 오차 시작 거리':

| 하위 시스템 | 1 cm 이상 오차 시작 | 이유 |
|---|---|---|
| Chaos·Recast/Detour·Mover·PCG 점·GAS·오디오·월드 파티션 격자 | 사실상 없음(double 기준 1/16 cm 유지 한계 5.6×10¹⁴ cm) | double [검증] |
| 렌더링 정점·나이아가라 입자(타일 상대) | 없음(타일 오프셋 ≤ 10.49 km → ≤ 1/16 cm) | 타일+오프셋 [검증] |
| 머티리얼 중 float 로 떨어뜨리는 경로(`WSDemote`) | 83.9 km(최소 단위), 167.8 km(최대 오차) | 절대 float 와 같음 [검증 경로 / 추정 크기] |
| 머티리얼 Noise 노드 | 없음(대신 655.36 m 거울 이음매) | `Common.ush:1838-1839` [검증] |
| PCG 위치 시드 | 21,474.8 km 에서 정수 범위 초과 | `PCGHelpers.cpp:52` [검증] |
| 원점 재설정 좌표 | 21,474.8 km 너머로 원점 이동 불가 | `FIntVector` [검증] |
| 프로젝트의 float 좌표 코드(있다면) | 83.9 km / 167.8 km, 부드러운 이동은 10~20 km 부터 저하 | 절대 float [추정] |

## 5. 안전 반경과 대책 (결정안)

| 반경 | 의미 | 정책 |
|---|---|---|
| R_float = 10.49 km (2²⁰ cm) | float 좌표가 1/16 cm 를 지키는 한계(엔진 상수와 일치) | float 를 쓰는 모든 계산(셰이더 입력, 나이아가라 월드 공간 시뮬, 프로젝트 GPU 텍스처 창, `FVector3f`)은 이 반경 안의 지역 원점 기준 좌표만 받는다 |
| R_design = ±10,000 km/축 (10⁹ cm) | 실용적 무한의 설계 상한. 600 cm/s 로 일직선 약 463시간 | 원점 재설정 없음. 생성기·저장 좌표는 int64 청크 색인 + double. 상한 근처에 자연 장벽(바다·폭풍)을 두거나 그냥 도달 불가로 간주 |
| R_hard = ±21,474.8 km (2³¹ cm) | int32 cm 변환이 깨지는 곳(PCG 시드, 원점 좌표, 프로젝트의 `FMath::FloorToInt(cm)` 류) | R_design 이 2배 여유를 둠 |
| WORLD_MAX/2 = 4,398만 km | 엔진 경계 검사 | 무시 |

대책 요약:
1. 원점 재설정을 쓰지 않는다(3절 근거). 연속 보행 요구(D-42)와 순간이동·암전 없는 이동과도 맞다.
2. 머티리얼 규칙: 월드 위치는 LWC 상태에서 나눗셈→`Frac` 순서로 처리하고, 사인·커스텀 HLSL·WPO 바람에는 카메라 상대 또는 청크 지역 좌표를 넣는다. Noise 노드는 655.36 m 거울 반복을 받아들이거나 대체한다.
3. 나이아가라: 플레이어에 붙어 오래 사는 시스템은 로컬 공간 시뮬레이션, 또는 타일 갱신 모드를 명시적으로 고른다.
4. 프로젝트 코드 점검: `Source/TDGame` 의 `FVector3f`·`FIntPoint`·`FIntVector` 사용 324건이 전부 `Source/TDGame/FluidFX/` 31개 파일에 있다(예: `Source/TDGame/FluidFX/TDFluidGpuSolverProxy.cpp`, `TDFluidGrid2DSolver.cpp`) [검증: 개수]. 이것이 격자 크기·지역 좌표인지, 월드 절대 좌표를 float 로 셰이더에 넘기는지는 미확인 [추정: 창 이동형 GPU 격자이므로 창 원점 기준이면 안전]. 무한 세계 채택 시 첫 점검 대상.
5. 생성기 시드: PCG 위치 시드에 의존하지 말고, 청크 int64 색인으로 결정론 시드를 만든다(프로젝트의 시뮬 전역 난수 금지 규칙과도 맞음).
6. 저장 형식: 변경 기록은 청크 int64 색인 + 청크 지역 좌표(float 가능, 청크 ≤ 10 km 이면 1/16 cm 이하)로 저장하면 크기와 정밀도를 동시에 잡는다 [추정: 설계 제안].

## 6. 미검증·열린 질문

- 렌더러의 카메라 상대 변환(`PreViewTranslation`)과 Lumen·가상 그림자 맵·거리장의 LWC 처리는 원문 미확인 [추정: 5.0~5.1 부터 LWC 대응].
- `AActor::OutsideWorldBounds` 기본 구현, `SetNewWorldOrigin` 헤더의 폐기 표식, `WSDemote` 셰이더 구현(`DoubleFloat.ush`)은 미확인.
- Mover 가 원점 재설정 뒤 실제로 옛 좌표로 되돌아가는지는 실행 검증 필요(쓰지 않을 것이므로 우선순위 낮음).
- UAF 포즈 경로가 월드 좌표에 의존하는 곳이 있는지 미확인.
- 멀티플레이 계획 여부(서버 한 원점에서 플레이어가 수천 km 흩어질 때 월드 파티션 서버 스트리밍 비용)는 다른 갈래 과제.
- 월드 파티션이 런타임 생성 청크를 담을 수 있는지(에디터 시점 셀 생성 구조)는 이 갈래 범위 밖.

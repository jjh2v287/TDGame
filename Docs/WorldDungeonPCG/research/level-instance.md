[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: 레벨 인스턴스와 룸 모듈 (UE 5.8 엔진 소스 확인, 2026-09-09)

경로 기준 `Engine/Source/Runtime/Engine`.

## 1. 런타임 동작 열거형
- `ELevelInstanceRuntimeBehavior { None(Hidden), Embedded_Deprecated(Hidden), Partitioned(표시명 "Embedded"), LevelStreaming(표시명 "Standalone") }` — `Public/LevelInstance/LevelInstanceTypes.h:55-65`. **C++ 이름(`Partitioned`)과 UI 라벨(Embedded)이 다르다.**
- `ALevelInstance::DesiredRuntimeBehavior` — `Public/LevelInstance/LevelInstanceActor.h:50`(에디터 전용 데이터, 기본 `Partitioned`). `APackedLevelActor`의 기본은 `None`.

## 2. Embedded(Partitioned)는 월드 파티션 스트리밍 생성 단계 기능
- 임베디드 레벨 인스턴스 액터 자체는 셀에 들어가지 않고 자식 컨테이너로 재귀 전개돼 **각 액터가 메인 그리드 셀에 개별 배치**된다(`Private/WorldPartition/WorldPartitionStreamingGeneration.cpp:960-967, 1305-1348`).
- 조건: 소스 레벨이 OFPA(외부 액터) 또는 ActorsDescs 메타데이터를 가져야 한다(`LevelInstanceActorDesc.cpp:238-241`, 맵 체크 오류 `WorldAssetDontContainActorsMetadata`).
- 비 월드 파티션 맵에서는 이 경로가 없어 사실상 Standalone처럼 로드된다(명시적 강제 치환 코드는 미확인).
- 임베디드 내부에 External Data Layer/Content Bundle 미지원.

## 3. 상속 규칙 (룸 모듈 설계에 중요)
- `bIsSpatiallyLoaded`는 부모와 AND 결합, `RuntimeGrid`는 부모 값 우선, 데이터 레이어는 누적 — `WorldPartitionStreamingGeneration.cpp:1224-1259`.
- 룸 단위 원자성이 필요하면(방 전체가 함께 로드) Standalone(LevelStreaming) 모드를 쓰거나, 룸 액터들의 그리드·공간 로딩 설정을 통일한다. 이 프로젝트는 던전 슬롯이 아틀라스에 공간적으로 분리돼 있어 임베디드 + 같은 셀 크기로 충분하다(방보다 셀이 크면 방 전체가 한 셀에 들어감).
- 레벨 인스턴스 내부 액터에 데이터 레이어 지정은 가능하나 해석은 소유 월드의 `DataLayerManager`가 한다(`DataLayerManager.cpp:189-193`).

## 4. 런타임 스폰·로드
- `SpawnActor<ALevelInstance>`는 지원되며 `PostRegisterAllComponents`에서 GUID 발급·등록(`LevelInstanceActor.cpp:84-101`). 로드는 `ULevelStreamingLevelInstance::LoadInstance` → `ULevelStreamingDynamic::LoadLevelInstance`(`LevelInstanceLevelStreaming.cpp:389-445`).
- **런타임에 월드 애셋을 바꿀 공개 API가 없다**: `SetWorldAsset`은 에디터 전용, `CookedWorldAsset`은 세터 없음. 런타임 조립이 필요하면 `ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr`(BlueprintCallable, `Classes/Engine/LevelStreamingDynamic.h:88-96`)를 직접 쓴다.
- 런타임 로드된 레벨 인스턴스의 소스가 월드 파티션이면 자체 스트리밍을 생성해 호스트 셀에 병합되지 않는다(`LevelInstanceLevelStreaming.cpp:559-568`).
- 런타임 스폰본의 `FLevelInstanceID`는 실행마다 달라 세이브 키로 쓰면 안 된다.
- → 이 프로젝트는 **에디터 베이크(임베디드)**를 기본으로 하고 런타임 조립은 하지 않는다(설계서 R-05와 일치).

## 5. APackedLevelActor
- 소스 레벨의 스태틱 메시를 ISM/HISM으로 병합한 블루프린트 액터(`PackedLevelActorISMBuilder.cpp:26-92`). 월드 애셋을 쿠킹하지 않고 레벨 스트리밍도 없어 비용이 낮다.
- **패킹되지 않는 컴포넌트는 버려진다**(`PackedLevelActorBuilder.cpp:274-292`). 문·함정·스폰 포인트 같은 로직 액터가 있는 룸 모듈에는 부적합. 순수 장식 키트에만 적합.

## 6. 안정 GUID
- `FLevelInstanceActorGuid` — `Public/LevelInstance/LevelInstanceActorGuid.h:11-35`: 에디터에서는 `GetActorGuid()`, 쿠킹 런타임에서는 직렬화된 GUID를 반환. 같은 룸 레벨을 여러 번 배치해도 `FLevelInstanceID`는 조상 GUID 해시로 인스턴스별로 다르다.

## 7. 런타임 이벤트
- `ULevelInstanceSubsystem`의 델리게이트는 모두 에디터 전용. 런타임 훅은 `ILevelInstanceInterface::OnLevelInstanceLoaded()` 오버라이드(`LevelInstanceSubsystem.cpp:380-384`) 또는 `GetLevelStreaming()`의 `OnLevelLoaded/OnLevelShown`(`Classes/Engine/LevelStreaming.h:633-647`).
- 로드는 스트리밍 틱에서 처리된다. 동기 대기 `BlockOnLoading()`은 private.

## 이 프로젝트 적용
- 룸 모듈 = OFPA 소스 레벨의 `ALevelInstance`(임베디드). 로직 액터(문, 스포너, 상자)는 그대로 유지된다. 장식만 있는 모듈은 Packed Level Actor 후보.
- 베이커가 룸 레벨 인스턴스를 배치할 때 `RuntimeGrid`를 던전용 그리드(또는 메인 그리드)로 통일하고, 방 하나가 셀 하나에 들어가도록 아틀라스 슬롯 위치를 셀 경계에 정렬한다.

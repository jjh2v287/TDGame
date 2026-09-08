[← 인덱스로](../UKGame_FeatureFileMap.md)

# 9. 상세 흐름: 심리스 던전 로딩

코드 본문까지 읽고 정리한 흐름입니다(2026-09-09). 핵심 아이디어는 "로딩 화면 대신 스트리밍 소스를 갈아끼운다"입니다. 야외(OutSide) 리전과 던전(InSide) 리전은 같은 월드 파티션 맵 안의 서로 다른 그리드·데이터 레이어이고, 플레이어가 통로(Passage) 스플라인을 걷는 동안 야외 스트리밍 반경을 점점 줄이고 던전 쪽 스트리밍 소스를 미리 켜 두어 두 영역이 겹쳐 로드된 상태로 문을 통과합니다.

### 9.1 구성 요소와 데이터

**통로 액터** `Source/UKGame/Actors/Passage/UKDungeonLoadingPassage.h/.cpp`
- 컴포넌트 3개: `WaySplineComponent`(통로 경로), `OutSideDoor` / `InSideDoor` 박스(콜리전 프로필 `OverlapOnlyPlayer`). 틱은 꺼져 있고 오버랩 이벤트만 사용.
- `GetProgressRate`: 플레이어 위치에 가장 가까운 스플라인 키를 찾아 "스플라인 거리 / 전체 길이"로 0~1 진행률 계산. 스플라인 시작(0)이 바깥문, 끝이 안쪽문.
- 바깥문 오버랩은 조종 중인 플레이어 캐릭터이면서 `bAwakeCharacterAfterLoading`이 아닐 때만 매니저에 전달. 이 플래그는 로딩 직후 캐릭터를 깨우는 순간에만 true라서, 로딩 복귀 시 문 박스와 겹쳐 있어도 진입으로 오인하지 않게 막는다.

**에디터 시점 데이터 굽기** (같은 파일 `WITH_EDITOR` 블록)
- 액터마다 `PassageGuid` 생성(`PostActorCreated`, `PostDuplicate`). 레벨 저장 시 `PostSaveRoot`가 데이터테이블 `DT-DungeonLoadingPassageGroupData`의 `PassageGroup` 행에 `PassageGuid → {OutSideDoorLocation, InSideDoorLocation, SharedData}`를 기록.
- `PreSaveRoot`는 `InSideStreamInRange`를 스플라인 끝점과 안쪽문 거리 + 50cm 이상으로 자동 보정.
- 액터 삭제·패키지 삭제 시 테이블에서 해당 GUID 제거.
- 테이블에 굽는 이유: 던전 안에서 아직 스트리밍되지 않은 다른 출구 통로의 문 위치와 리전 키를 알아야 하기 때문.

**SharedData** `Source/UKGame/DataTable/UKDungeonLoadingPassageGroupData.h`

| 필드 | 의미 |
|---|---|
| InSideWorldRegionKey / OutSideWorldRegionKey | 던전 리전 / 야외 리전 (WorldRegionData 행 키) |
| MinOutSideStreamingRadiusRate (기본 0.2) | 통로 끝에서 야외 스트리밍 반경을 원래의 몇 배까지 줄일지 |
| InSideStreamInRange | 던전 안에서 통로 스트리밍 소스를 유지할 거리 |
| OutSideCheckDirectionDot (0.7) / InSideCheckDirectionDot (-0.7) | 문을 나갈 때 "통로 쪽으로 나갔는지" 판정 임계값 |

**스트리밍 소스 프로바이더** `Source/UKGame/Actors/StreamingSourceProvider/UKStreamingSourceProvider.h/.cpp`
- `UWorldPartitionStreamingSourceComponent` 하나를 감싼 액터. `Active` / `Deactive`는 컴포넌트의 EnableStreamingSource / DisableStreamingSource 호출뿐. 월드 파티션에 "이 위치·반경·그리드를 로드해 달라"고 알리는 점 하나다.
- `AUKDungeonPlaceableStreamingSourceProvider`(같은 폴더)는 디자이너가 던전 안에 직접 놓는 파생형. BeginPlay에서 `PassageGroup` 이름으로 매니저에 추가(Extra) 소스로 등록된다.

**매니저** `Source/UKGame/Subsystems/PassageSystem/UKDungeonLoadingPassageManager.h/.cpp`
- GameInstance 서브시스템. 상태 `EUKDungeonPassageState`가 `None`이 아닐 때만 틱(`IsTickable`).
- 상태: `None → OutSideDoorEnter → PassageInProgress → InSideDoorEnter → InSide` (나갈 때는 역순).

### 9.2 런타임 흐름 (단계별)

**0단계: 통로 액터 스트리밍 인 → `RegisterPassage`**
- 통로 GUID로 프로바이더 두 묶음을 스폰(`SpawnStreamingSourceProvider`). InSide 묶음은 안쪽문 위치에 던전 리전의 `StreamingSourceData.TargetGridDataMap`(모양 → 대상 그리드) 항목마다 하나씩, OutSide 묶음은 바깥문 위치에 야외 리전 것으로 생성. `bUseGridLoadingRange`면 그리드별로 따로 만든다. 스폰은 `UUKAssetInstanceManager`를 통해 풀링.
- 스폰 직후 OutSide 묶음은 비활성화(던전 안에서 스폰된 경우 `SpawnByInside` 예외). InSide 묶음은 끄지 않으므로 **던전 쪽 스트리밍 소스는 통로가 보이는 순간부터 켜져 있다**.
- 던전 리전 키를 `UWorldManager::AddRegion`으로 활성 리전 집합에 추가(같은 리전을 쓰는 통로가 여럿이면 `AddRegionCountMap` 카운트만 증가). AddRegion은 활성 리전들의 데이터 레이어 상태를 합쳐 `SetDataLayerInstanceRuntimeState`를 호출한다.

**1단계: 바깥문 박스 진입 → `EnterOutSidePassage`, 상태 `OutSideDoorEnter`**
- 다른 통로를 진행 중이면 무시(한 번에 하나).
- 이 통로의 OutSide 프로바이더 활성화. 야외 로딩은 플레이어 자신의 소스가 아니라 바깥문에 고정된 프로바이더가 담당하게 된다.
- `UWorldManager::ApplyStreamingSourceData(InSideWorldRegionKey, Actor)`: 플레이어 컨트롤러의 추가 스트리밍 소스(`AUKPlayerController::ActiveExtraStreamingSources`)를 던전 리전 설정으로 전환.

**2단계: 바깥문 박스 이탈 → `LeaveOutSidePassage`**
- `CheckDirection`: 스플라인 시작점−문 벡터와 플레이어−문 벡터의 내적이 `OutSideCheckDirectionDot`(0.7)보다 크면 통로 안쪽으로 들어간 것.
- 들어갔으면 상태 `PassageInProgress`, `SetActiveWorldEnvironment(false)`로 야외 수면 액터(`IsWorldGrids`인 물)를 숨기고 물 영역을 끈다.
- 되돌아 나갔으면 `None`으로 리셋, OutSide 프로바이더 비활성, 플레이어 추가 소스를 야외 리전으로 복원, 물 다시 켬.

**3단계: 통로 걷는 동안 → `Tick` + `UpdateOutsideStreamingSource`**
- 월드매니저가 로딩 중(`UWorldManager::IsLoading`)이면 아무것도 하지 않음.
- 매 틱 진행률을 구해 바뀐 경우에만 야외 프로바이더 반경을 다시 쓴다.

```
Gap    = R0 - R0 * MinOutSideStreamingRadiusRate   // 줄일 수 있는 최대량
Radius = R0 - Gap * ProgressRate                   // 진행률에 비례해 선형 축소
Radius = max(Radius, 5000)                         // 하한 50m
```
  R0는 그리드 로딩 범위(`bUseGridLoadingRange`면 `GetRuntimePartitionStreamingData`가 런타임 해시셋에서 리플렉션으로 꺼낸 `GetLoadingRange`) 또는 데이터의 Radius. 축소된 모양은 `bUseGridLoadingRange=false` 고정 반경이 된다.
- 결과: 통로 끝에 가까울수록 야외 셀은 바깥문 주변 최소 반경만 남고 언로드되며, 던전 셀은 안쪽문 프로바이더와 플레이어 소스로 계속 로드된다. 이 교차가 심리스의 실체다.
- 같은 틱에서 던전 안에서 예약된 다른 통로의 프로바이더 파기(`PendingDestroyStreamingSourceMap`)도 처리.

**4단계: 안쪽문 진입 → `EnterInSidePassage`, 상태 `InSideDoorEnter`**
- 방문자가 같은지만 확인. 던전 안(`InSide`)에서 다른 출구 문에 닿으면 `CurrentPassage`를 그 통로로 교체.

**5단계: 안쪽문 이탈 → `LeaveInSidePassage`**
- 스플라인 끝점 기준 `CheckDirection`(임계 -0.7). 통로 방향으로 되돌아갔으면 `PassageInProgress`로 복귀, 메인 리전을 야외로 두고 Extra 소스를 끈다.
- 던전 쪽으로 나갔으면 상태 `InSide`, `CurrentPassageGroup` 확정, `SetMainRegion(InSideWorldRegionKey)`(활성 집합에 있을 때만 현재 리전 포인터 변경), 그룹의 Extra 프로바이더 전부 활성화.

### 9.3 던전 안에서의 관리 (`UpdatePassageGroup`)

던전 안에서는 통로 액터가 아니라 데이터테이블의 그룹 전체를 기준으로 동작한다. 방문자가 1cm 이상 움직였을 때만 갱신.
- 그룹 내 각 통로의 안쪽문과 거리를 잰다. `InSideStreamInRange` 안이면 프로바이더가 없을 때 스폰(`SpawnByInside` 표시), 야외 프로바이더를 진행률 1.0(최소 반경)으로 세팅한 뒤 그 통로의 야외 리전을 `AddRegion`. 출구에 가까워지면 바깥 세계가 미리 로드된다.
- 범위를 벗어나면 즉시 지우지 않고 `PendingDestroyStreamingSourceMap`에 넣어 3초 유예 후, 여전히 멀면 프로바이더를 파기하고 `RemoveRegion`으로 야외 데이터 레이어를 내린다(문 앞에서 왔다 갔다 할 때의 요동 방지).
- 물 액터는 `Source/UKGame/Actors/Water/UKSimpleWaterBodyActorBase.cpp`에서 스스로 `IsInDungeon()`을 확인해 야외 수면이면 숨긴다.

### 9.4 나가기 · 텔레포트 · 저장 복원

- 통로로 걸어 나가는 경우는 2~5단계를 역방향으로 거친다. 안쪽문 이탈이 통로 방향이면 `PassageInProgress`, 바깥문 이탈이 야외 방향이면 `None`으로 리셋되며 야외 프로바이더가 꺼지고 플레이어 추가 소스가 야외 리전으로 돌아간다.
- 마커(세이브 포인트) 텔레포트: `Source/UKGame/Subsystems/MarkerSystem/UKMarkerManager.cpp`가 마커의 `bDungeon`에 따라 `SetInSideDungeon`(그룹·방문자 설정, Extra 소스 켬, 모든 야외 프로바이더 끔) 또는 `SetOutDungeon`을 먼저 호출한 뒤 `UWorldManager::Teleport`.
- 저장: `Source/UKGame/Subsystems/UserDataManager.cpp`가 던전 마커 저장 시 `DungeonPassageState=InSide`와 `PassageGroup`을 플레이어 정보(`UserData_PlayerInfo`)에 기록. "그룹/상태" 문자열로 직렬화.
- 복원: `OnEvent_ReadyToPlay`가 월드매니저 준비 완료 이벤트에서 현재 리전이 `bDungeon`이고 저장 상태가 `InSide`면 `SetInSideDungeon`으로 상태를 되살린다.
- 파티 캐릭터 교체 시 `OnPartyMemberChanged`가 방문자 포인터를 새 캐릭터로 교체.

### 9.5 코드에서 확인된 특이점과 위험 요소

- **던전 InSide 프로바이더 상시 활성**: 스폰 후 끄는 코드가 없어 통로 액터가 로드된 동안 던전 입구 그리드는 상시 로드된다. 의도된 선로딩으로 보이나 메모리 예산에서 인지 필요.
- **`UnRegisterPassage`의 널 역참조 가능성**: `AddRegionCountMap.Find` 결과를 확인 없이 `(*Count)`로 읽는다. `RegisterPassage`에서 `GetPassageData`가 실패해 카운트가 추가되지 않은 통로(테이블에 없는 GUID)가 언로드되면 크래시.
- **엔진 내부 필드 리플렉션 접근**: `GetRuntimePartitionStreamingData`가 `UWorldPartitionRuntimeHashSet`의 비공개 `RuntimeStreamingData`를 프로퍼티 이름으로 찾아 캐스팅. 엔진 버전이 바뀌면 조용히 nullptr를 돌려주고, 축소 기준 반경이 데이터의 Radius(기본 10000)로 대체된다.
- **한 번에 한 통로만**: `EnterOutSidePassage`는 진행 중인 통로가 있으면 무시하므로 두 통로의 바깥문 박스가 겹치면 두 번째는 반응하지 않는다.
- **방향 판정이 문 박스 이탈 순간 위치에만 의존**: 박스 안에서 넉백·컷신 등으로 순간이동해 빠져나가면 내적 판정이 어긋나 `PassageInProgress`에 갇힐 수 있다. 상태를 강제로 푸는 경로는 마커 텔레포트의 `SetOutDungeon`뿐이다.

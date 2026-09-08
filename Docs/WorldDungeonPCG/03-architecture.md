[← 인덱스로](../WorldDungeonPCG_Plan.md)

# 3. 제안 아키텍처

설계서(01)의 요구사항을 이 프로젝트 규칙(AGENTS.md: 로직은 C++, 블루프린트는 구성용, `TD` 접두어)에 맞춰 구체화한 것이다. UKGame의 방식은 참고 사례일 뿐 기준이 아니며, 어디가 어떻게 다른지는 3.9절에 정리했다. 설계 원칙은 세 가지다: **결정론(같은 정의+시드 → 같은 결과), 데이터와 알고리즘의 분리(순수 C++ 알고리즘 + UObject 데이터 에셋), 런타임·에디터·테스트가 같은 코드를 공유**.

## 3.1 모듈 구성

| 모듈 | 종류 | 책임 | 의존 |
|---|---|---|---|
| `TDGame` | Runtime | 게임플레이(전투, 캐릭터, 컨트롤러), 던전 입구·심리스 이동, 영속 상태 서브시스템, 세이브 | Engine, GAS, `TDWorldGen` |
| `TDWorldGen` | Runtime | 월드·던전 **생성·검증 알고리즘과 정의 데이터 타입**. 게임플레이에 의존하지 않는 순수 계층 | Engine, PCG(데이터 타입만) |
| `TDGameEditor` | Editor | 생성기 오케스트레이션(에디터 서브시스템), 베이크(액터·레벨 인스턴스 배치), 툴 UI 호출 함수, 검증 리포트, 커맨드릿, 디테일 커스터마이징 | `TDGame`, `TDWorldGen`, UnrealEd, PCG 에디터 |

`TDWorldGen`을 따로 두는 이유: 생성·검증 알고리즘이 에디터 모듈에 있으면 자동화 테스트와 런타임 디버그 명령(예: PIE에서 시드 재생성)이 같은 코드를 쓸 수 없다. 반대로 `TDGame`에 섞어 두면 전투 코드와 생성 코드가 서로를 끌어들인다. 알고리즘 계층은 `UObject`가 아닌 구조체와 자유 함수로 작성해 월드 없이도 테스트할 수 있게 한다.

## 3.2 핵심 데이터 타입 (`TDWorldGen`)

정의(입력)와 결과(출력)를 분리한다. 정의는 디자이너가 편집하는 데이터 에셋, 결과는 생성기가 만든 순수 데이터다. 결과에는 항상 시드와 생성기 버전이 붙는다.

```
// 시드 파생: 하나의 마스터 시드에서 영역별로 독립 스트림을 만든다.
struct FTDSeedContext
{
    int32 MasterSeed;
    FRandomStream Derive(FStringView Domain, int32 Index = 0) const; // Hash(MasterSeed, Domain, Index)
};
constexpr int32 TD_WORLDGEN_VERSION = 1;   // 알고리즘이 바뀌면 올린다. 결과에 함께 저장.

// 정의 에셋
UTDWorldDefinition      : 지역 목록(UTDRegionDefinition), 마을·메인 던전·랜드마크 수작업 앵커 참조, 던전 아틀라스 정의 참조
UTDRegionDefinition     : 바이옴, 사이드 던전 수, POI 원형별 수량 범위, 이벤트 영역 수, 도로 규칙
UTDBiomeDefinition      : 나무·바위·덤불·잔해 에셋 세트, 밀도, 최소 간격, 최대 경사, 도로/POI 이격, PCG 그래프 참조
UTDPoiArchetype         : POI 종류(Camp/Shrine/Ruin/Graveyard/EventArea), 레벨 인스턴스 후보, 배제 반경, 전투 공간 요구
UTDDungeonTheme         : 룸 모듈 목록(각 모듈: 레벨 인스턴스 에셋, 바운드, 도어 소켓 배열, 역할 태그), 드레싱 PCG 그래프, 인카운터 세트
UTDDungeonFlowTemplate  : 흐름 규칙(Linear/Branch/Loop/Hub/KeyLock), 방 수 범위, 필수 역할(Entrance/Boss), 분기·루프 확률
UTDDungeonAtlasDefinition: 슬롯 격자 원점·간격·행렬 수, 슬롯별 테마/흐름 할당

// 결과(순수 데이터, UPROPERTY로 직렬화 가능)
FTDWorldLayout   : 지역 폴리곤, POI 배치, 도로 스플라인 점, 던전 입구 배치, 배제 영역, 시드, 버전
FTDDungeonLayout : 흐름 그래프(노드=방 역할, 엣지=연결), 방 배치(모듈 인덱스, 트랜스폼), 도어 연결, 시드, 버전, 검증 결과
FTDValidationReport : 항목(심각도, 코드, 메시지, 월드 위치, 관련 ID) 배열 + 점수
```

도어 소켓 규격(룸 모듈 조립의 전제): 모듈 로컬 공간의 위치·방향(4방향 중 하나), 폭 등급(예: 1칸=400cm), 태그(일반/보스 전용). 모듈은 그리드 단위(예: 400cm)의 정수 배 바운드를 갖는다. 규격은 `Docs/WorldDungeonPCG/room-module-spec.md`로 확정한 뒤 아트를 시작한다(할 일 P2-01).

## 3.3 생성·검증 알고리즘 (`TDWorldGen`)

| 클래스(순수 C++) | 입력 → 출력 | 요점 |
|---|---|---|
| `FTDWorldGraphGenerator` | 월드 정의 + 수작업 앵커 + 시드 → 지역 그래프, 던전 입구 후보, POI 후보 | 수작업 앵커를 먼저 고정하고 빈 자리에만 후보를 놓는다. 포아송 디스크 샘플링으로 최소 간격 보장 |
| `FTDRoadGenerator` | 앵커·POI → 도로 스플라인 점 | 마을·메인 POI 간 최소 신장 트리 + 보조 연결. 경사 비용 가중 |
| `FTDDungeonFlowGenerator` | 흐름 템플릿 + 시드 → 미션 그래프 | 그래프 문법 규칙 적용. Key/Lock은 "Key 노드가 Lock 노드보다 앞선 위상 순서"를 생성 단계에서 보장 |
| `FTDDungeonLayoutSolver` | 미션 그래프 + 테마 → 방 배치 | 도어 소켓 매칭 + 바운드 겹침 검사 + 백트래킹(시도 상한). 실패 시 다른 모듈/회전 시도 |
| `FTDDungeonValidator` | 던전 레이아웃 → 리포트 | 연결성, 필수 방, 겹침, 방 수, 막다른 길 비율, Key/Lock 순서. 내비 검사는 에디터 단계에서 추가(3.7) |
| `FTDWorldValidator` | 월드 레이아웃 + 아틀라스 → 리포트 | 입구 간격, POI 밀도, 도로 도달성, 슬롯 로딩 범위 중첩, 무콘텐츠 구간 |
| `FTDCandidateSelector` | N개 시드 → 점수순 후보 | 검증 실패는 탈락, 통과는 가중 점수(막다른 길 적음, 경로 길이 적정 등) |

원칙: 알고리즘은 월드·액터를 만지지 않는다. 월드 배치는 3.5의 베이커가 담당한다. 그래서 같은 알고리즘을 자동화 테스트가 수천 시드로 돌릴 수 있다.

## 3.4 런타임: 심리스 이동과 영속 상태 (`TDGame`)

### 3.4.1 던전 입구와 이동
- `ATDDungeonEntrance`(액터): `DungeonId`, `TargetEntryTransform`, `PreloadDistance`, 귀환 정보. 상태 머신은 갖지 않고 진입 요청만 서브시스템에 넘긴다.
- `UTDSeamlessTravelSubsystem`(월드 서브시스템): 목적지 선로딩과 이동을 담당. 스트리밍 소스는 액터를 스폰하지 않고 서브시스템이 `IWorldPartitionStreamingSourceProvider`를 직접 구현해 임시 소스를 등록한다(액터 풀·이름 키 불필요). 상태: `Idle → Preloading → ReadyToTravel → Traveling → Settling → Idle`. 각 전이는 델리게이트로 알려 연출(암전·통로·입력 제한)은 블루프린트가 구독한다.
- 완료 판정은 월드 파티션 서브시스템의 스트리밍 완료 질의를 쓰고, 타임아웃(설정값)이 지나면 "완료 대기 연출을 연장"하되 절대 미완료 상태로 이동하지 않는다(R-62).
- 이동 후 플레이어 자신의 스트리밍 소스가 던전 셀을 유지하고, 임시 소스는 해제한다. 필드 셀은 거리로 언로드된다.
- 던전 슬롯의 진입·퇴출 위치는 `UTDDungeonAtlasDefinition`에서 읽는다. 런타임 생성은 없다(베이크된 결과만 사용).

### 3.4.2 안정 ID와 영속 상태
- `FTDStableId`(FGuid 래퍼). 수작업 액터는 `UTDPersistentStateComponent`가 에디터에서 생성한 GUID를 보관하고, 생성기가 배치한 액터는 `Hash(DungeonId 또는 RegionId, 생성 경로 인덱스)`로 결정론적 GUID를 부여한다. 액터 이름·라벨은 절대 키로 쓰지 않는다.
- `ITDPersistentActor` 인터페이스: `WriteState(FTDActorStateRecord&)`, `ReadState(const FTDActorStateRecord&)`. 상자·문·보스 스포너가 구현한다.
- `UTDWorldStateSubsystem`(게임 인스턴스 서브시스템): `TMap<FTDStableId, FTDActorStateRecord>`, `TMap<FName, FTDDungeonState>`(완료·보스 처치·이벤트), 데이터 레이어 런타임 상태 스냅샷. 액터가 로드되어 `BeginPlay`에 들어올 때 컴포넌트가 상태를 조회해 적용하고, 언로드 전에 기록한다.
- 세이브: `UTDSaveGame`에 월드 상태 서브시스템 데이터, 마스터 시드, 생성기 버전, 세이브 포맷 버전을 저장. 포맷 버전별 마이그레이션 함수 표를 둔다.

### 3.4.3 데이터 레이어
- 퀘스트·진행 상태 전환용으로만 사용(R-71). `UTDWorldStateSubsystem`이 "상태 태그 → 데이터 레이어 상태" 표(데이터 에셋)를 읽어 적용한다. 던전 로딩 제어에는 쓰지 않는다.

## 3.5 에디터: 오케스트레이션·베이크·툴 (`TDGameEditor`)

- `UTDWorldGenEditorSubsystem`(에디터 서브시스템): 생성 요청을 받아 `TDWorldGen` 알고리즘을 실행하고 결과를 보관. 부분 재생성(지역·슬롯 단위), 잠금(잠긴 요소는 결과에서 고정), 시드·버전 관리, 검증 리포트 발행.
- `FTDWorldBaker` / `FTDDungeonBaker`: 결과 데이터를 에디터 월드에 반영. POI 앵커 액터, 도로 스플라인 액터, 배제 볼륨, 룸 모듈 레벨 인스턴스, 던전 입구 액터를 트랜잭션 안에서 배치하고 외부 패키지(OFPA)로 저장. 생성물에는 태그와 안정 ID를 기록해 재생성 시 같은 것을 갱신·삭제할 수 있게 한다.
- PCG 연계: 베이커가 놓은 앵커·스플라인·볼륨을 PCG 그래프가 "액터 데이터 가져오기(태그 기준)"로 읽는다. 바이옴 파라미터는 `UTDBiomeDefinition` 값을 PCG 그래프 파라미터로 밀어 넣는다(C++에서 그래프 인스턴스 파라미터 설정). 최종 베이크 시 PCG 결과를 정적 액터로 고정하고 컴포넌트를 정리한다.
- 툴 UI: 에디터 유틸리티 위젯(블루프린트)은 버튼·입력 필드만 두고, 모든 동작은 `UTDWorldGenEditorLibrary`(C++ 함수 라이브러리)를 호출한다. 리포트는 메시지 로그에 내고 항목 클릭 시 뷰포트 카메라를 옮긴다.
- 커맨드릿 `UTDWorldGenCommandlet`: `-Seed= -Slots= -Validate -Bake` 옵션으로 무인 실행. CI 성격의 회귀 확인에 쓴다.

## 3.6 콘텐츠 폴더와 이름

```
Content/World/Maps/L_TDWorld_Main          (World Partition, 시작 맵으로 전환)
Content/World/Definitions/DA_TDWorld_Main, DA_TDRegion_Forest, DA_TDBiome_Forest, DA_TDDungeonAtlas_Main
Content/World/DataLayers/DL_*
Content/PCG/Biomes/PCG_TDBiome_Forest, Roads/PCG_TDRoadside, POI/PCG_TDPoiDressing, Common/PCG_TDClutter
Content/Dungeon/Themes/DA_TDTheme_Crypt, Flows/DA_TDFlow_Linear|Branch|Loop|Hub|KeyLock
Content/Dungeon/Rooms/Crypt/LI_TDRoom_Crypt_Entrance … (레벨 인스턴스)
Content/Dungeon/Encounters/DA_TDEncounter_*
Content/POI/LevelInstances/LI_TDPoi_Camp_A …, POI/Definitions/DA_TDPoi_Camp
Content/Editor/WorldGenerator/EUW_TDWorldGenerator (UI만)
```
접두어: 데이터 에셋 `DA_TD`, 레벨 인스턴스 `LI_TD`, PCG 그래프 `PCG_TD`, 맵 `L_TD`, 데이터 레이어 `DL_TD`. C++는 AGENTS.md 3절.

## 3.7 검증·테스트 전략

- 단위: `TDWorldGen` 알고리즘을 시드 범위(예: 1~500)로 돌려 검증기가 통과 비율·평균 방 수 등을 보고. 회귀: 고정 시드의 결과 해시가 변하지 않는지 확인(생성기 버전이 바뀔 때만 갱신).
- 에디터 통합: 커맨드릿으로 "생성 → 검증 → 리포트"를 실행하고 리포트 파일을 `Saved/WorldGen/`에 남긴다.
- 내비: 베이크 후 에디터에서 내비 데이터를 빌드하고, 주요 경로(입구→보스)를 경로 탐색 API로 검사하는 후처리 검증 단계를 둔다(리서치 04 참고).
- 런타임: PIE 자동화 테스트로 입구 왕복(선로딩 완료 → 이동 → 귀환)과 상태 복구(상자 열림 후 언로드/재로드)를 확인. MCP로 실행.

## 3.8 단계별 도입 순서 (설계서 14장과 대응)

| Phase | 이 아키텍처에서 만드는 것 |
|---|---|
| 0 준비 | 모듈 2개 신설(`TDWorldGen`, `TDGameEditor`), 플러그인 활성화, 폴더·이름 규칙, 할 일 운영 규칙 |
| 1 심리스 | `L_TDWorld_Main`, `UTDSeamlessTravelSubsystem`, `ATDDungeonEntrance`, 더미 던전 슬롯, 안정 ID·상태 서브시스템 최소판, PIE 검증 |
| 2 던전 | 룸 모듈 규격, 테마·흐름 정의, 흐름 생성기·레이아웃 솔버·검증기, 던전 베이커, 후보 10개 생성 툴, 드레싱 PCG |
| 3 야외 | 월드·지역·바이옴 정의, 월드 그래프·도로 생성기, 월드 검증기, 월드 베이커, 바이옴 PCG, 후보 선택 |
| 4 프로덕션 | 규모 확장, HLOD·내비·라이팅 프로파일링, 잠금·버전·베이크 고정, 세이브 마이그레이션 |

## 3.9 UKGame 방식과의 차이 (왜 다르게 하는가)

| 주제 | UKGame(과거) | 이 설계 | 이유 |
|---|---|---|---|
| 스트리밍 소스 | 프로바이더 액터를 풀에서 스폰해 활성/비활성 | 서브시스템이 소스 제공자 인터페이스를 직접 구현 | 액터 수명·이름 관리가 사라지고 테스트가 쉬움 |
| 로딩 범위 조회 | 엔진 비공개 필드를 리플렉션으로 읽음 | 공개 API만 사용, 없으면 프로젝트 설정값 | 엔진 업데이트에 조용히 깨지지 않게 |
| 식별자 | 액터 이름 문자열, 에디터 저장 시 데이터테이블에 굽기 | GUID 기반 안정 ID + 결정론적 파생 | 풀링·복제·리네임에 안전 |
| 상태 머신 | 통로 액터 오버랩과 내적 판정으로 진입 방향 추정 | 명시적 전환 상태 + 완료 질의 + 타임아웃 | 순간이동·넉백으로 갇히는 문제 제거 |
| 생성 | 런타임 로직 없음(수작업 배치) | 순수 알고리즘 계층 + 베이커 분리 | 결정론과 회귀 테스트 |
| 툴 로직 | 블루프린트·파이썬 혼재 | C++ 함수 라이브러리 + UI만 블루프린트 | 프로젝트 규칙(7절) |

## 3.10 리서치 반영 결정 (2026-09-09, 근거는 `research/` 폴더)

| 주제 | 결정 | 근거 파일 |
|---|---|---|
| 스트리밍 완료 판정 | `UWorldPartitionSubsystem::IsStreamingCompleted(const IWorldPartitionStreamingSourceProvider*)`로 목적지 소스 기준 판정 + 내비 `ProjectPointToNavigation` 폴링. 둘 다 타임아웃 | worldpartition-streaming.md 3·7절, actor-id-and-navmesh.md B-3 |
| 로딩 범위 조회 | `UWorldPartitionRuntimeHashSet::ResolveRuntimePartition(GridName)->LoadingRange` 공개 API. 리플렉션 금지 | worldpartition-streaming.md 6절 |
| 안정 ID | `FActorInstanceGuid::GetActorInstanceGuid`를 `PostRegisterAllComponents`의 `Super` 호출 전에 읽어 `UPROPERTY(SaveGame) FGuid`에 캐시. `AActor::GetActorGuid()`는 에디터 전용이라 런타임 코드에서 쓰지 않음 | actor-id-and-navmesh.md A절, persistence-editor-batch.md 1.1 |
| 언로드 전 상태 기록 | `FWorldDelegates::PreLevelRemovedFromWorld`에서 해당 레벨의 영속 액터 상태를 기록. 로드 후 복원은 액터 `BeginPlay` | persistence-editor-batch.md 1.2 |
| 세이브 형식 | `ArIsSaveGame` 프록시 아카이브 + `UPROPERTY(SaveGame)` + 포맷 버전. 데이터 레이어는 요청 상태를 애셋 경로 키로 별도 저장, 월드 파티션 초기화 후 부모→자식 순 복원 | persistence-editor-batch.md 1.3, datalayer.md 5절 |
| 내비 | `Static` + `bIsWorldPartitioned`로 베이크 청크 스트리밍. 청크 그리드를 런타임 셀 크기에 정렬. 검증기는 에디터에서 `UNavigationSystemV1::Build()` 후 `TestPathSync` | actor-id-and-navmesh.md B절, dungeon-generation.md 5절 |
| 룸 모듈 표현 | OFPA 소스 레벨의 `ALevelInstance` 임베디드. 장식 전용은 Packed Level Actor. 런타임 조립 없음 | level-instance.md |
| 룸 조립 알고리즘 | 정수 격자(기본 400cm) + 4방향 도어 소켓 + 90도 회전, 복도는 모듈. 셀 집합으로 충돌 검사. 시도 상한·백트랙 깊이 상한·재시드 상한 | dungeon-generation.md 3절 |
| 흐름 생성 | 고정 패턴 규칙 4종(분기·루프·잠금 삽입·열쇠 후퇴) + Key/Lock 구성적 보장(순서 인덱스) + 보유 키 BFS 검증 | dungeon-generation.md 2절 |
| 검증 2단계 | 순수 데이터 검증(하드 제약 거부, 소프트 점수) → 상위 후보만 베이크 후 내비 검증 | dungeon-generation.md 5절 |
| PCG 베이크 | Normal 편집 모드 생성 + 저장(파티션 액터에 ISM 유지). 수동 보정 대상은 Spawn Actor 또는 `ClearPCGLink`. 바이옴 파라미터는 `UPCGGraphParametersHelpers`로 그래프 인스턴스에 주입 | pcg-bake-data-community.md 2·3절, pcg-api-and-nodes.md C절 |
| PCG 입력 | 생성기 결과(POI 앵커·도로 스플라인·배제 볼륨)는 태그 액터로 베이크하고 그래프가 Get Actor Data(ByTag)로 읽음. CSV 노드는 없으므로 외부 데이터 인터롭은 쓰지 않음 | pcg-api-and-nodes.md G·I절 |
| 에디터 툴 1단계 | 던전 슬롯 액터 `CallInEditor` 버튼 + `CheckForErrors` → `FMessageLog("TDWorldGen")` + `FActorToken` 클릭 이동. 2단계에서 노마드 탭 | persistence-editor-batch.md 2.3, dungeon-generation.md 8절 |
| 커맨드릿 | `UWorldPartitionBuilder` 파생 `UTDWorldGenBuilder`로 생성·검증·베이크 무인 실행 | persistence-editor-batch.md 3.1 |
| 자동화 테스트 플래그 | 5.8은 `enum class EAutomationTestFlags`, 예 `EditorContext | ProductFilter` | persistence-editor-batch.md 3.4 |
| 탑다운 벽 가림 | 룸 모듈 규격에 남쪽 벽 높이 상한과 회전 정책(0° 기본)을 포함 | dungeon-generation.md 7절 |

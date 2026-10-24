# 01. 프로젝트 적합성: '유한·사전 제작 세계' 가정과 무한 샌드박스 재사용 지점

작성: 2026-10-23, 조사 갈래 01(읽기 전용, 코드·설정·장부 변경 없음). 표기: [검증] = 원문을 직접 읽음, [추정] = 계산·추론·간접 근거.
용어: 무한 = 가장자리 없이 플레이어 주변에 생성(실용적 무한). 샌드박스 = 변경이 저장·유지. 변형 수준 (A) 무한 생성·지형 불변, (B) 물체 설치·철거·건물, (C) 지형 변형(복셀 등).

## 0. 결론

1. 지금 세계는 "에디터에서 한 번 굽는 약 1.5 km²의 유한 세계"다. 생성은 에디터 밖 numpy(지형) + 에디터 Python·C++ 베이크이고, 런타임에 새로 생성하는 경로는 0이다. 무한 세계를 막는 핵심 가정은 다섯 가지다: ① 지형 = 랜드스케이프(런타임 생성 불가) ② numpy 생성기가 배열 전체 연산·순차 난수(위치로 평가 불가) ③ 배치 생성기가 고정 경계 상자 전체에서 푸아송·전역 길찾기 ④ 영속 ID = 에디터 배치 GUID, 세이브 = 단일 슬롯 전체 메모리 ⑤ 내비 = 경계 볼륨 기반 동적 생성(인보커 없음).
2. 재사용 가치가 큰 것: 시드 파생(`FTDSeedContext`), 순수 데이터 결정론 생성기(런타임 모듈, 에디터 의존 0), 지형 샘플러 추상(`FTDTerrainSampler`), 세이브의 시드·생성기 버전·마이그레이션 골격, 준비 완료 대기 원칙(미완료 상태로 절대 이동 안 함), 지역 대기 혼합(양자화 단계), 슬롯 배열 몬스터 AI·이름 있는 난수 스트림, 탑다운 컷아웃(위치 무관), HISM 액터.
3. 탑다운이 무한 세계에 유리한 점: 실측 카메라(암 1100cm, 피치 −50°, 시야각 90°, 16:9 가정)의 지면 시야는 폰 기준 뒤 5.5m ~ 앞 15.3m, 폭 15~42m 사다리꼴이다[추정, 아래 1절 계산]. 600cm/s 이동이면 생성 지연 2초 기준 필요 생성 반경은 약 38m다. 지금 월드 파티션 로딩 범위 768m의 약 1/20이라, 원거리 지형·HLOD 없이 작은 청크 고리만 돌리면 된다.
4. 결정 충돌: D-05(랜드스케이프)·D-40(numpy 정본)·3.10 "룸 런타임 조립 없음"·GQ-08(빠른 이동 없음)은 무한 세계와 정면 충돌하거나 재검토가 필요하다. D-45(연속 보행)·D-06(동적 내비)·D-47(비동기)·D-46(역광 규칙)·D29(이름 있는 난수)·GQ-02(영속 월드)·GQ-22(싱글 전용)는 그대로 맞는다.

## 1. 기준 수치

| 항목 | 값 | 근거 |
|---|---|---|
| 기본 이동 속도 | 600 cm/s | `Source/TDGame/Characters/TDGameCharacter.cpp:122` [검증] |
| 카메라 | 공통 맥락 실측 1100cm/−50°/요 45°/시야각 90°. C++ 생성자 기본값은 800cm/−60°(`TDGameCharacter.cpp:142-143`)로 다르고, 1100은 `Combat/Feedback/TDCombatFeedbackSettings.h:269` 기준 암 길이 | [검증](코드 값), 런타임 1100의 출처(블루프린트 기본값 등)는 [추정] |
| 지면 시야 | 카메라 높이 842.6cm, 수평 후퇴 707cm, 세로 반시야 29.36°(16:9) → 앞 끝 +1530cm, 뒤 끝 −549cm, 폭 근단 14.9m·원단 41.7m, 폰→모서리 최대 약 25.8m | [추정](기하 계산, 화면비 16:9 가정) |
| 필요 생성 반경 | 25.8m + 6m/s × 생성 지연. 지연 2초 → 약 38m, 5초 → 약 56m | [추정] |
| 현재 세계 크기 | 필드 1008m(`FieldBoundsCm` ±50400cm, `Source/TDWorldGen/Public/World/TDWorldDefinitions.h:167`) + 이계 504m×2(D-41·D-45). 문서상 "약 1.5km², 할 일 0" | [검증] `Docs/Gameplay_Fun_And_Systems_Analysis.md:81` |
| 월드 파티션 | 셀 252m, 로딩 범위 768m | [검증] `Tools/WorldGen/README.md:88` |
| 좌표 상한 | LWC(Large World Coordinates, 대형 월드 좌표) 최대 8,796,093,022,208 cm | [검증] `UE_5.8/Engine/Source/Runtime/Engine/Public/EngineDefines.h:41,53` |
| 실용 무한 거리 | 600cm/s 직진 1시간 = 21.6km. 100km 도달에 4.6시간 | [추정](계산) |

## 2. 시스템별 적합성 표

작업량 표기[전부 추정]: 작음 = 1~3일, 중간 = 1~2주, 큼 = 3~6주, 매우 큼 = 2개월 이상(에이전트 보조 1인 기준). 영향: 그대로 / 수정 / 재작성 / 해당 없음.

### 2.1 생성 알고리즘·도구

| 시스템 | 현재 가정 | 무한 세계 영향 | 근거 파일:줄 | 작업량 |
|---|---|---|---|---|
| `TDWorldGen` 모듈 성격 | 런타임 모듈. 의존성 Core·CoreUObject·Engine·DeveloperSettings·Json·JsonUtilities뿐(에디터 모듈 0) | 그대로(런타임 호출 가능) | `TDGame.uproject:17-18` [검증], `Source/TDWorldGen/TDWorldGen.Build.cs:9-17` [검증] | – |
| 실제 호출처 | 생성기 호출은 전부 에디터 모듈(`TDGameEditor`)의 라이브러리·커맨드릿. 게임 코드 호출 0 | 수정(런타임 호출 진입점 신설) | `Source/TDGameEditor/WorldGen/TDWorldGenEditorLibrary.cpp:142,174,237`, `TDWorldGenCommandlet.cpp:221` [검증] | 작음 |
| 시드 파생 `FTDSeedContext` | `Hash(CRC(도메인), MasterSeed, int32 Index)` → `FRandomStream`/GUID | 수정: 청크 좌표 2개(X,Y)를 받는 파생 추가. GUID는 `FGuid(A=시드,B=CRC,C=Index,D=해시)`라 청크 좌표+지역 인덱스를 담으려면 구성 변경 | `Source/TDWorldGen/Public/TDWorldGenTypes.h:10-23`, `Private/TDWorldGenTypes.cpp:8-23` [검증]. `FCrc::StrCrc32(Domain.GetData())`는 널 종료 문자열 가정(현재 리터럴만 전달) | 작음 |
| 던전 생성기(흐름·배치 해결·검증) | 순수 데이터, 시드 결정론, 시도·재시드 상한 | 그대로(런타임 호출 가능). 단 시험은 `EditorContext` 전용 | `Private/Dungeon/TDDungeonGenerator.cpp:205-215`, `TDDungeonFlowGenerator.cpp:325-328`, `TDDungeonLayoutSolver.cpp:850` [검증], 시험 `Private/Tests/TDDungeonGenerationTests.cpp:6,95` [검증] | 작음(런타임 시험 추가) |
| 던전 실체화(룸 모듈 → 레벨) | 에디터 베이커가 레벨 인스턴스를 굽는다. 결정 "런타임 조립 없음" | 재작성(런타임 조립: `ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr`로 룸 모듈 레벨을 위치에 로드) | `Docs/WorldDungeonPCG/03-architecture.md` 3.10 '룸 모듈 표현' 행·3.4.1 마지막 줄(`03-architecture.md:68`) [검증], 엔진 `Runtime/Engine/Classes/Engine/LevelStreamingDynamic.h:92` [검증](선언), 런타임 조립 성능은 [추정] | 중간 |
| 던전 아틀라스 | 고정 원거리 슬롯 원점 (3000m, 3000m), 로딩 범위 128m | 수정: 무한 지형이 그 좌표에 닿으므로 슬롯을 수직 분리(먼 Z)하거나 별도 공간으로 | `Public/Dungeon/TDDungeonDefinitions.h:126,135,141` [검증] | 작음 |
| 야외 배치 생성기(POI·입구) | 고정 경계 상자 전체에 푸아송 원반 + 전역 재시도 | 재작성(청크 국소 푸아송 + 이웃 청크 경계 결정론 처리). 점수·검증 규칙은 재사용 | `Public/World/TDWorldGeneration.h:8-19`, `Private/World/TDWorldGraphGenerator.cpp:74,390-393,422-423` [검증] | 중간 |
| 도로 생성기 | 레이아웃 전체 격자(400cm) 위 쌍별 경로 탐색 | 재작성(거친 지역 그래프 → 청크 국소 경로의 계층식) | `TDWorldGeneration.h:21-34`, `Private/World/TDRoadGenerator.cpp:51,213,246` [검증] | 큼 |
| 야외 검증기 | 경계 안 개수 범위(POI 6~12)·간격·빈 공간 거리 | 수정(전체 개수 대신 청크 밀도 규칙으로) | `TDWorldGeneration.h:36-51` [검증] | 작음 |
| 지형 샘플러 `FTDTerrainSampler` | `TFunction` 높이·경사 함수. 지금은 에디터에서 랜드스케이프로 채움 | 그대로(노이즈 함수를 꽂으면 됨) — 재사용 1순위 | `Public/World/TDWorldTypes.h:214-223` [검증], `TDWorldGenEditorLibrary.cpp:94` [검증] | 작음 |
| 높이맵 생성기(numpy) | 1009×1009 배열 전체. 값 노이즈가 `rng.random` 순차 소비 + `ndimage.zoom`, 거리 변환·가우시안·강 되메움 등 전역 연산 | 재작성: 좌표 해시 노이즈로 C++ 이식(위치로 평가 가능해야 함). numpy는 오프라인 미리보기·동등성 기준으로 남길 수 있음 | `Tools/WorldGen/generate_ashen_vale.py:1-3,37,104-122,164,286,341` [검증] | 큼 |
| 이계 생성기(numpy) | 필드 공유 변 높이를 복사해 이음매 없이 붙임 | 수정: "공유 변 복사"는 청크·수작업 구역 경계 봉합 기법으로 재사용 | `Tools/WorldGen/generate_realm.py:1` [검증] | 중간 |
| 필드 테두리 | 남·동 테두리 30m 급경사 띠(세계 끝) | 수정(절차 영역이 붙는 변을 연다) | `generate_ashen_vale.py:1` [검증] | 작음(생성기) + 재베이크 |
| 생성기 액터 | `CallInEditor` 버튼만, `#if WITH_EDITOR` | 해당 없음(에디터 도구) | `Source/TDGame/World/Generation/TDWorldGeneratorActor.h:29-44` [검증] | – |
| 에디터 브리지·베이커·커맨드릿 | 에디터 전용 생성·굽기 | 해당 없음(수작업 허브 제작용으로 유지) | `Public/TDWorldGenEditorBridge.h:10-18`, `Source/TDGameEditor/WorldGen/*` [검증] | – |
| PCG | 플러그인 기본 활성(uproject에 없음), `TDGame` 비공개 의존성. 지금은 에디터 재생성·베이크(랜드스케이프 데이터 입력) | 수정: 엔진에 런타임 생성(`GenerateAtRuntime`, 생성 반경, 파티션 액터 풀 스케줄러)이 있으나 그래프 입력을 랜드스케이프 → 런타임 지형으로 바꿔야 함 | `UE_5.8/Engine/Plugins/PCG/PCG.uplugin:13` [검증], `Source/TDGame/TDGame.Build.cs:36` [검증], `TDWorldGeneratorActor.cpp:6,74-76` [검증], 엔진 `Plugins/PCG/Source/PCG/Public/PCGComponent.h:334-338`, `Public/RuntimeGen/PCGRuntimeGenScheduler.h:72` [검증], 입력 교체 난이도 [추정] | 중간 |
| 생물군 정의 | 나무·바위·덤불·잡초 세트 소프트 참조 | 그대로(런타임 산포 입력) | `Public/World/TDWorldDefinitions.h:43-61` [검증] | – |
| `ATDInstancedMeshActor` | HISM 컴포넌트 하나 감싼 얇은 액터 | 그대로(청크 산포 그릇) | `Source/TDGame/World/Generation/TDInstancedMeshActor.h:9-17` [검증] | – |

### 2.2 지형·월드 구조·스트리밍

| 시스템 | 현재 가정 | 무한 세계 영향 | 근거 파일:줄 | 작업량 |
|---|---|---|---|---|
| 지형 표현(D-05) | 랜드스케이프. 생성은 에디터 모듈 C++ 함수 | 재작성(A부터): 랜드스케이프 `Import`는 `#if WITH_EDITOR` 안이라 쿠킹 빌드 런타임 생성 불가 → 런타임 메시 지형(다이내믹 메시 등) 또는 미리 구운 타일 조합 | `Docs/Tasks/decisions.md:36-43` [검증], `Source/TDGameEditor/Landscape/TDLandscapeEditorLibrary.*` [검증], 엔진 `Runtime/Landscape/Classes/LandscapeProxy.h:1326`(가드 시작)·`:1421`(`Import`) [검증], 대체 수단의 성능은 [추정] | 매우 큼 |
| 월드 파티션 | 에디터 배치 액터를 셀(252m)·로딩 범위(768m)로 스트리밍 | 수정: 런타임 생성 액터는 월드 파티션 셀이 관리하지 않음 → 절차 영역은 자체 청크 관리자 필요. 수작업 허브(현 필드·이계)는 월드 파티션 그대로 | `Tools/WorldGen/README.md:88` [검증], 런타임 생성 액터 비관리는 [추정] | 큼(청크 관리자) |
| 연속 세계(D-45) | 순간이동 0회, 이계를 실제 지형으로 붙임 | 그대로(무한 세계는 이 방향의 연장) | `Docs/Tasks/decisions.md:321-327` [검증] | – |
| `UTDSeamlessTravelSubsystem` | 스트리밍 소스 제공자 직접 구현, 준비 = 월드 파티션 스트리밍 완료 + 내비 투영, 시한 초과 시 대기 연장(미완료 이동 금지) | 수정: 원칙·상태 기계는 재사용, 판정 대상을 "청크 생성 완료 + 내비 타일 완료"로 확장. 던전 페이드 이동 부분은 해당 없음 | `Source/TDGame/World/Streaming/TDSeamlessTravelSubsystem.h:26,74`, `.cpp:186,432,455,490` [검증], 페이드 이동 `Gameplay_Fun_And_Systems_Analysis.md:168` [검증] | 중간 |
| `UTDRealmSubsystem`(이계) | 이계 = 고정 상자(`CenterCm`·`HalfExtentCm` 25200cm)의 설정 목록 2개, 대기는 상자 포함·관문 가중으로 혼합(양자화 단계) | 수정: "상자 포함" 대신 생물군 가중 함수로 대기 혼합. 양자화·태양 단계 기계와 `FTDRealmAtmosphere` 자료형은 재사용 | `Public/World/TDRealmDefinitions.h:16-73,197-221` [검증], `Config/DefaultGame.ini:59-60` [검증], `TDRealmSubsystem.cpp:823-840` [검증] | 중간 |
| 이계 데이터 레이어 | 이계마다 게임플레이 데이터 레이어 활성화 | 해당 없음(절차 청크) / 허브에는 그대로 | `TDRealmDefinitions.h:218`, `TDRealmSubsystem.cpp:680-716` [검증] | – |
| 승강기·갱도 라이너 | 관문 정의에서 런타임 스폰되는 연출 세트 | 해당 없음(허브 연출) | `TDRealmSubsystem.cpp:351-374` [검증] | – |
| 낙하 복구 | 이계별 낙하 높이 + 최근접 관문 복귀 | 수정(청크 기준 안전 지점) | `TDRealmDefinitions.h:209`, `TDRealmSubsystem.cpp:1047,1105-1111` [검증] | 작음 |

### 2.3 영속·세이브(샌드박스 핵심)

| 시스템 | 현재 가정 | 무한 세계 영향 | 근거 파일:줄 | 작업량 |
|---|---|---|---|---|
| 안정 ID 해석 | 게임 월드에서 `FActorInstanceGuid`(에디터 배치 GUID)만 씀. 없으면 경고 후 영속 안 함 | 수정(A·B 필수): 청크 생성기가 `BeginPlay` 전에 결정론 GUID(청크 좌표 + 생성 인덱스)를 주입하는 경로 | `Source/TDGame/World/Persistence/TDPersistentStateComponent.cpp:22-54`(특히 34-43) [검증]. 설계서는 "생성기 배치 액터 = Hash(지역, 경로 인덱스)"라 했으나 실제로는 베이크 때 태그만 붙임 `03-architecture.md:71` [검증], `Source/TDGameEditor/WorldGen/TDWorldBaker.cpp:60-62,129-143` [검증] | 작음 |
| 상태 기록 형식 | `StableId` + Int·Bool·Float 맵. 클래스·변환 없음 | A: 그대로(생성물의 상태·파괴 표시는 Bool로 가능). B: 수정 — 설치물 기록(클래스 ID, 양자화 변환, 청크 좌표)과 청크별 철거 목록 신설 | `World/Persistence/TDPersistentActor.h:8-22` [검증] | B: 중간 |
| 저장소 | 게임 인스턴스 서브시스템이 전 세계 기록을 `TMap` 두 개로 메모리에 유지, 단일 슬롯 동기 저장 | 수정(A)→재작성(B): 무한 증가하므로 청크(또는 지역 묶음) 단위 분할 파일 + 비동기 저장 + 미방문 청크는 기록 0(시드로 재생성) | `TDWorldStateSubsystem.h:41-63,116-120` [검증], `.cpp:157,170` [검증] | B: 큼 |
| 세이브 머리 | `MasterSeed`·`GeneratorVersion`·포맷 버전·마이그레이션 | 그대로 + 수정: 생성기 버전을 청크마다 찍어야 방문한 청크가 코드 변경 뒤에도 같게 유지 | `TDWorldStateSubsystem.h:49-56,114`, `.cpp:9,67,127` [검증], `TDWorldGenTypes.h:7`(`TD_WORLDGEN_VERSION = 1`) [검증] | 작음 |
| 언로드 전 기록 | 컴포넌트 `EndPlay`(제거·파괴)에서 기록 | 그대로(청크 언로드 = 액터 제거 경로) | `TDPersistentStateComponent.cpp:87-100` [검증] | – |
| 던전 상태 | `FName DungeonId` 키 | 수정(시드 던전은 좌표·시드 기반 ID) | `TDWorldStateSubsystem.h:29-39,120` [검증] | 작음 |
| POI 앵커 | `PoiId`·`StableId` 필드 보유 | 그대로(데이터 그릇) | `World/Generation/TDPoiAnchor.h:17-29` [검증] | – |
| 아레나 조우 | 정의를 런타임에 바꿀 수 있고 출현 위치 계산이 정적 함수 | 수정: 청크 스폰용 ID 공급(조우 ID 규칙은 미확인) | `World/Generation/TDArenaEncounter.h:40,52,181` [검증], ID 규칙 [추정] | 작음 |

### 2.4 내비·AI·전투 시뮬

| 시스템 | 현재 가정 | 무한 세계 영향 | 근거 파일:줄 | 작업량 |
|---|---|---|---|---|
| 내비메시 | `RuntimeGeneration=Dynamic`, 타일 1000uu, 타일 상한 1,048,576, 로드 시 재빌드. 레벨은 경계 볼륨 기반·비파티션 | 수정: 경계 볼륨 대신 인보커 방식 필요(엔진 기본값 꺼짐), 프로젝트 코드에 인보커 0 | `Config/DefaultEngine.ini:12,24,42-46` [검증], `Tools/WorldGen/editor_setup_navmesh_dynamic.py:3,17` [검증], 엔진 `Runtime/NavigationSystem/Public/NavigationSystem.h:391`, `Private/NavigationSystem.cpp:936` [검증], `RecastNavMesh.cpp:551` [검증] | 작음~중간 |
| D-06 동적 내비 | 정적 청크는 준비 판정 60초 초과 실패, 동적은 입장 0.3~0.5초 | 그대로(무한 세계에 필요한 쪽이 이미 채택·실측됨) | `Docs/Tasks/decisions.md:45-50` [검증] | – |
| 몬스터 두뇌 슬롯 | SoA 슬롯 배열, `SimulationId` 단조 증가, 위치 `FVector`(double) | 수정: 청크 언로드 때 슬롯 해제·동면 경로 필요. 좌표 정밀도는 문제없음 | `Source/TDGame/MonsterAI/TDMonsterBrainSlot.h:12-14` [검증], `Docs/MonsterAI_CombatSim/00-decision-record.md:30`(D14) [검증] | 작음 |
| 이웃 격자 | `THierarchicalHashGrid2D`, 셀 250cm | 그대로(정수 셀 범위상 사실상 무한) | `MonsterAI/TDNeighborGrid.h:21,36` [검증], 범위 계산 [추정] | – |
| 틱 예산·LOD | 단계 A ≤300마리 4.0ms, 거리 LOD 4단 | 그대로(플레이어 주변만 활성이라 무한과 무관) | `Docs/MonsterAI_CombatSim/03-tick-and-scale.md:16,20` [검증] | – |
| 난수 규약(D29) | 마스터 시드에서 `HashCombine`으로 이름 있는 스트림, 전역 난수 금지 | 그대로: 청크 스폰 시드 = Hash(세계 시드, 청크 X, Y)로 같은 규약 확장 | `00-decision-record.md:54` [검증] | 작음 |
| 결정론 전투 시뮬 | 시나리오 단위 헤드리스 | 그대로(세계 크기와 무관) | `00-decision-record.md:31,49-59` [검증] | – |

### 2.5 탑다운 시각·UI·게임플레이 결정

| 시스템 | 현재 가정 | 무한 세계 영향 | 근거 파일:줄 | 작업량 |
|---|---|---|---|---|
| 가림 컷아웃 | 폰 위치를 매 프레임 MPC(Material Parameter Collection, 재질 파라미터 모음)에 float로 기록 | 그대로. float 정밀도는 원점 100km에서 약 1cm 단위[추정]라 실용 거리 안에서 문제없음 | `Source/TDGame/Framework/TDTopDownOcclusionSubsystem.cpp:52,169-173` [검증] | – |
| 역광 태양 규칙(D-46) | 모든 지역 태양 수평 방향 = 카메라 반대 ±30° | 그대로(전역 규칙, 생물군 대기에도 적용) | `Docs/Tasks/decisions.md:329-334` [검증] | – |
| 탑다운 재질 체계(D-43) | 프로젝트 마스터·인스턴스, 지형은 랜드스케이프 레이어 재질 | 수정: 런타임 메시 지형용 혼합(정점 색 등) 재질 필요 | `Docs/Tasks/decisions.md:309-313` [검증], `Tools/WorldGen/editor_make_landscape_material.py`(존재) [검증], 대체 방식 [추정] | 중간 |
| 미니맵·지도 | 소스에 미니맵·씬 캡처 0. 슬라이스에서 미니맵 제외, GP-37 지도·발견은 P2 | 신규: 무한 세계에선 방향감 필수. 생성 데이터가 2D 필드라 저비용 지도 가능[추정] | 소스 검색 0건 [검증], `Gameplay_Fun_And_Systems_Analysis.md:50,267` [검증] | 중간 |
| GQ-02 장기 구조 | 권장 (a) 영속 월드 + 반복 단위 | 그대로(A·B와 맞음) | `Gameplay_Fun_And_Systems_Analysis.md:230` [검증] | – |
| GQ-08 빠른 이동 | 권장 (a) 없음, 지름길로 | 충돌: 무한 세계에서 귀환 거리가 무한히 늘어남 → 재검토 필요 | `Gameplay_Fun_And_Systems_Analysis.md:236` [검증] | 결정 |
| GQ-22 목표 시간·싱글 | 본편 6~8시간 + 반복 10시간 이상, 멀티 없음 | 그대로: 싱글 전용이라 샌드박스 복제(리플리케이션) 비용 0 | `Gameplay_Fun_And_Systems_Analysis.md:250` [검증] | – |
| GP-55 시드 던전 '심연' | 무한 반복 단위, 생성은 편집기 전용, GP-49 선행 | 수정: 무한 야외의 가장 가까운 기존 계획. 런타임 조립(2.1)이 공통 선행 | `Gameplay_Fun_And_Systems_Analysis.md:175` [검증] | 2.1에 포함 |
| D-47 비동기 로드 | 런타임 동기 로드 금지, 생성 결과에 쓰는 데이터는 시작 전에 모두 로드 | 그대로: 생물군 에셋 세트를 청크 생성 전에 미리 요청하는 규칙으로 직결. 검사 도구 재사용 | `Docs/Tasks/decisions.md:336-346` [검증], `AGENTS.md` 7절 [검증] | 작음 |
| D-40 numpy 정본 | 같은 시드 바이트 재현을 근거로 C++ 이식 대신 numpy 정본 | 충돌: 런타임 생성은 C++ 생성기 필요 → 재개 | `Docs/Tasks/decisions.md:97-102` [검증] | 결정 + 큼 |

## 3. 변형 수준별 영향 묶음

| 수준 | 반드시 바꿀 것(위 표의 재작성·수정) | 재사용 | 작업량 합[추정] |
|---|---|---|---|
| (A) 무한 생성·지형 불변 | 런타임 지형(랜드스케이프 대체)·C++ 좌표 해시 노이즈 생성기·청크 관리자·청크 국소 배치·인보커 내비·준비 판정 확장·생물군 대기 혼합·결정론 GUID 주입·던전 슬롯 분리 | 시드 파생, 지형 샘플러, 순수 데이터 생성기, HISM, PCG 런타임 생성, 몬스터 슬롯·난수 규약, 컷아웃, 세이브 머리 | 3~5개월 |
| (B) + 설치·철거·건물 | (A) + 설치물 기록 형식·청크 분할 비동기 저장소·철거 목록·설치 시 내비 갱신(동적 내비라 경로 존재)·설치 규칙(격자/자유) | `EndPlay` 기록, 마이그레이션 골격, Bool 상태 | (A) + 1~2개월 |
| (C) + 지형 변형 | (A)의 런타임 지형을 복셀·편집 가능 메시로 다시 설계, 청크 메시 재생성·충돌·내비 재빌드·지형 편집 기록 | 프로젝트 안에 직접 재사용할 지형 편집 자산 없음(동적 내비만 유효) | (A) + 3개월 이상 |

## 4. 권장 형태(이 갈래의 판단)

- 혼합 구조[추정]: 지금 굽는 약 1.5km²(필드 + 이계)는 월드 파티션 "수작업 허브"로 유지하고, 남·동 테두리 띠를 열어 그 밖을 런타임 청크(권장 시작값 64m 청크, 반경 2~3 고리)로 무한 확장한다. 허브–절차 경계는 `generate_realm.py`의 공유 변 복사 방식으로 봉합한다.
- 순서[추정]: ① C++ 좌표 해시 노이즈 + `FTDTerrainSampler` 연결 + 오프라인 numpy 동등성 시험 ② 런타임 지형 메시 청크(A의 최대 위험, 엔진 갈래 조사 결과 필요) ③ 청크 관리자 + 준비 판정 + 인보커 내비 ④ 결정론 GUID·청크 국소 배치·PCG 런타임 산포 ⑤ (B) 기록 형식·분할 저장소.
- 탑다운 이점 활용: 생성 반경 40~60m로 충분하므로 원거리 표현(HLOD 등)은 범위 밖. 다만 몬스터 L3 거리·소리·그림자 반경이 시야보다 클 수 있어 반경은 시야가 아니라 이들 중 최대값으로 정한다[추정].

## 5. 확인하지 못한 것

- 런타임 카메라 1100cm 값의 실제 출처(블루프린트 기본값인지 카메라 리그인지). 코드 기본값은 800cm/−60°.
- 메인 맵의 월드 파티션 실제 설정(`.umap` 안). README 기록값만 확인.
- `ATDArenaEncounter::GetEncounterId`의 키 규칙, PCG 그래프가 랜드스케이프 외 입력을 받도록 바꾸는 비용, 런타임 메시 지형의 프레임 비용 — 다른 갈래(엔진·성능) 결과로 판정해야 함.
- FluidFX의 지형 트레이스가 런타임 메시 지형에서도 같은 결과를 내는지(최근 커밋에서 지형 트레이스 비동기화)[추정].

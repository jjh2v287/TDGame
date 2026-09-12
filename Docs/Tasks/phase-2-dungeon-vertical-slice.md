# Phase 2 — 던전 버티컬 슬라이스

| 상태 | 개수 |
|---|---|
| todo | 1 |
| doing | 6 |
| done | 5 |
| decision | 1 |

목표(설계서 Phase 2): 테마 1개, 룸 모듈 8개, 흐름 3개로 버튼 한 번에 서로 다른 던전 후보 10개를 생성·검증·베이크한다.

### P2-01 룸 모듈 규격 문서
- 상태: done
- 우선순위: 높음
- 선행: P0-04
- 목표: 아트가 룸 모듈을 만들 때 지켜야 할 규격을 확정한다(코드보다 먼저).
- 완료 조건: `Docs/WorldDungeonPCG/room-module-spec.md`에 그리드 단위(기본 400cm, 결정 D-03), 모듈 바운드는 단위의 정수 배, 도어 소켓(위치는 바운드 가장자리 셀 중앙, 방향 N/E/S/W, 폭 등급 1칸, 태그 Normal/Boss/Locked), 회전 정책(기본 0°, 회전 허용 모듈은 X=Y), 카메라 방향(남쪽) 벽 높이 상한, 소켓 표현 방식(`UTDRoomDoorSocketComponent` 씬 컴포넌트), 스폰 마커(`UTDEncounterMarkerComponent`: 역할·반경), 레벨 인스턴스 소스 레벨은 OFPA, 최소 전투 공간 산식
- 참조: R-41, 03-architecture 3.2, research/dungeon-generation.md 3·7절, research/level-instance.md
- 기록: 2026-09-09 작성 / 2026-09-12 완료(Claude): `Docs/WorldDungeonPCG/room-module-spec.md` — 셀 400cm, 축·회전·소켓 규약, Crypt 카탈로그 12종(C++ `FillCryptPlaceholderModules`와 동일), 카메라 남쪽 벽 120cm 제한, 파일 이름 규칙. 소켓·인카운터 컴포넌트는 예정 표기.

### P2-02 테마·흐름 정의 에셋 타입
- 상태: doing
- 우선순위: 높음
- 선행: P0-02, P2-01
- 목표: `UTDDungeonTheme`, `UTDDungeonFlowTemplate`, `FTDRoomModuleDefinition`(레벨 인스턴스 소프트 참조, 셀 크기, 도어 소켓 배열, 역할 태그, 회전 허용)을 `TDWorldGen`에 만든다.
- 완료 조건: 데이터 에셋 생성 가능, `DA_TDTheme_Crypt`, `DA_TDFlow_Linear/Branch/KeyLock` 3개 작성(모듈은 P2-03 후 채움), 에셋 유효성 검사(`IsDataValid`)로 필수 역할 누락 경고
- 참조: R-40, R-41, 03-architecture 3.2
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDDungeonTheme`(모듈 카탈로그·IsDataValid·`FillCryptPlaceholderModules`)·`UTDDungeonFlowTemplate`(`ApplyKindDefaults`)·`FTDRoomModuleDefinition` 구현, `DA_TDTheme_Crypt`·`DA_TDFlow_5종` 생성 스크립트 `Tools/WorldGen/editor_make_definitions.py`.

### P2-03 룸 모듈 8개 제작(플레이스홀더 지오메트리)
- 상태: done
- 우선순위: 높음
- 선행: P2-01
- 목표: Crypt 테마 모듈 8개(Entrance, Straight, Corner, T_Junction, Large, DeadEnd, Treasure, Boss)를 규격대로 OFPA 레벨 + 레벨 인스턴스로 만든다. 아트 완성도는 무관, 규격 준수가 목적.
- 완료 조건: `Content/Dungeon/Rooms/Crypt/LI_TDRoom_Crypt_*` 8개, 각 레벨에 도어 소켓 컴포넌트·스폰 마커·내비 볼륨, 테마 에셋에 등록
- 검증: 규격 검사 커맨드(P2-08)로 통과
- 참조: R-41, R-110
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): 플레이스홀더 레벨 생성 스크립트 `Tools/DungeonGen/editor_make_room_modules.py`(12모듈, 바닥·벽·문 마커·내비 볼륨, 테마 LevelAsset 연결). 규격 검사 커맨드는 남음. / 2026-09-12: `Content/Dungeon/Rooms/Crypt/LI_TDRoom_Crypt_*` 12개 레벨 생성, 테마 `LevelAsset` 연결 완료. 규격 검사 커맨드는 남음. / 2026-09-12: 규격 검사 `UTDWorldGenEditorLibrary::ValidateRoomModuleLevels`(바닥·문 마커 가장자리·내비 볼륨) 12모듈 OK(`Tools/WorldGen/editor_place_generator_actors.py`).

### P2-04 흐름 그래프 생성기 `FTDDungeonFlowGenerator`
- 상태: doing
- 우선순위: 높음
- 선행: P2-02
- 목표: 흐름 템플릿 + 시드 → 미션 그래프(`FTDDungeonFlowGraph`: 노드 역할·순서 인덱스, 간선 잠금 ID).
- 완료 조건:
  - 규칙 4종: 분기 추가, 루프 추가(잠긴 간선 우회 금지), 잠금 삽입(간선 i→i+1), 열쇠 배치(인덱스 < i 노드)
  - Linear/Branch/Loop/Hub/KeyLock 템플릿 파라미터로 방 수 범위·분기 수·루프 수 제어
  - 결정론 테스트: 고정 시드 3개의 그래프 해시가 변하지 않음. 시드 1~500에서 Key/Lock 순서 위반 0
- 산출물: `Source/TDWorldGen/Public/Dungeon/TDDungeonFlowGenerator.h/.cpp`, 테스트
- 참조: R-40, research/dungeon-generation.md 2절
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 프로토타입 `Tools/DungeonGen/`로 선행 구현 — 흐름 5종(Linear/Branch/Loop/Hub/KeyLock)·크기 3종 생성기, 검증기 7항목, 후보 배치 툴(시드 1~100×3크기×5흐름 1,500건 통과율 100%, 같은 시드 해시 동일), 아틀라스 슬롯 베이크(`editor_build_dungeon.py`, 슬롯 원점 (300000+K×30000, 300000)cm). C++ 이식·데이터 에셋·실제 룸 모듈 아트는 남음. / 2026-09-12(Claude) C++: `TDDungeonFlowGenerator.cpp` 구현, 테스트 `TDGame.WorldGen.Dungeon.KeyLockSeeds1To200KeyBeforeLock` 등 통과.

### P2-05 레이아웃 솔버 `FTDDungeonLayoutSolver`
- 상태: doing
- 우선순위: 높음
- 선행: P2-04, P2-02
- 목표: 미션 그래프 + 테마 → 정수 격자 위 방 배치(`FTDDungeonLayout`).
- 완료 조건:
  - 노드 순서 DFS 배치, 후보 (모듈 × 도어 × 회전) 시도 상한(설정, 기본 16), 백트랙 깊이 상한(기본 4), 초과 시 파생 시드로 재생성(기본 200회), 실패는 리포트로 반환
  - 충돌 검사는 `TSet<FIntVector>` 셀 점유, 0 두께 접촉 허용. 복도는 도어 2개 모듈로 취급
  - 결과에 시드·생성기 버전·사용 모듈 GUID 기록
  - 테스트: 시드 1~500 성공률 보고(목표 ≥ 90%, 미달 시 리포트에 실패 사유 집계)
- 참조: research/dungeon-generation.md 3절
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 프로토타입 `Tools/DungeonGen/`로 선행 구현 — 흐름 5종(Linear/Branch/Loop/Hub/KeyLock)·크기 3종 생성기, 검증기 7항목, 후보 배치 툴(시드 1~100×3크기×5흐름 1,500건 통과율 100%, 같은 시드 해시 동일), 아틀라스 슬롯 베이크(`editor_build_dungeon.py`, 슬롯 원점 (300000+K×30000, 300000)cm). C++ 이식·데이터 에셋·실제 룸 모듈 아트는 남음. / 2026-09-12(Claude) C++: `TDDungeonLayoutSolver.cpp`(BFS 부착·복도·루프 봉합·재시작) 구현, 5흐름×3크기×시드 1~100 = 1,500건 통과.

### P2-06 던전 검증기 `FTDDungeonValidator`
- 상태: doing
- 우선순위: 높음
- 선행: P2-05
- 목표: 레이아웃을 검사해 `FTDValidationReport`를 만든다.
- 완료 조건: 연결성(BFS), 필수 방(Entrance/Boss), 겹침, 방 수 범위, 막다른 길 비율, Key/Lock(보유 키 BFS), 시작~보스 최단 경로 비율(≥ 60% 기본). 하드 실패는 Error, 소프트는 Warning+점수. 각 항목에 월드 위치와 관련 방 ID
- 참조: R-90, research/dungeon-generation.md 5절
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 프로토타입 `Tools/DungeonGen/`로 선행 구현 — 흐름 5종(Linear/Branch/Loop/Hub/KeyLock)·크기 3종 생성기, 검증기 7항목, 후보 배치 툴(시드 1~100×3크기×5흐름 1,500건 통과율 100%, 같은 시드 해시 동일), 아틀라스 슬롯 베이크(`editor_build_dungeon.py`, 슬롯 원점 (300000+K×30000, 300000)cm). C++ 이식·데이터 에셋·실제 룸 모듈 아트는 남음. / 2026-09-12(Claude) C++: `TDDungeonValidator.cpp` 7검사, 문 제거 시 실패 테스트 통과.

### P2-07 후보 선택기 `FTDCandidateSelector`
- 상태: doing
- 우선순위: 중간
- 선행: P2-06
- 목표: N개 시드를 생성·검증해 점수순 후보 목록을 만든다.
- 완료 조건: 실패 탈락, 소프트 점수 가중치는 데이터 에셋(`UTDValidationScoring`)으로, 결과에 후보별 시드·점수·리포트 요약
- 참조: R-92
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 프로토타입 `Tools/DungeonGen/`로 선행 구현 — 흐름 5종(Linear/Branch/Loop/Hub/KeyLock)·크기 3종 생성기, 검증기 7항목, 후보 배치 툴(시드 1~100×3크기×5흐름 1,500건 통과율 100%, 같은 시드 해시 동일), 아틀라스 슬롯 베이크(`editor_build_dungeon.py`, 슬롯 원점 (300000+K×30000, 300000)cm). C++ 이식·데이터 에셋·실제 룸 모듈 아트는 남음. / 2026-09-12(Claude) C++: `TDCandidateSelector.cpp`(점수·순위), 커맨드릿 `-run=TDWorldGen -Seeds=1-5` 리포트에서 상위 3 추천 확인.

### P2-08 던전 슬롯 앵커 액터와 에디터 툴 1단계
- 상태: done
- 우선순위: 높음
- 선행: P0-01, P2-06
- 목표: 슬롯마다 놓는 `ATDDungeonSlotAnchor`(에디터 전용 로직은 `TDGameEditor`의 디테일 커스터마이징/서브시스템에 두고 액터는 데이터만)와 버튼을 만든다.
- 완료 조건:
  - 프로퍼티: DungeonId, Theme, Flow, Seed, Candidate 개수, 잠금 목록(방 GUID)
  - `CallInEditor` 버튼: Generate Candidates, Regenerate Unlocked, Validate, Bake Selected
  - `CheckForErrors`가 리포트를 `FMessageLog("TDWorldGen")`에 `FActorToken`으로 출력, 클릭 시 위치 이동
  - 규격 검사 명령(모듈 도어 소켓이 바운드 가장자리인지 등)
- 참조: R-100, R-101, 04-tools 4.5, research/persistence-editor-batch.md 2.3
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `ATDDungeonSlotAnchor`(`Source/TDGame/World/`, 데이터 + CallInEditor Generate Candidates/Regenerate Unlocked/Validate/Bake Selected, 에디터 로직은 `ITDWorldGenEditorBridge` 구현체 `TDGameEditor/WorldGen/TDWorldGenEditorBridgeImpl`), `CheckForErrors` → 메시지 로그 `TDWorldGen`, 규격 검사 `ValidateRoomModuleLevels`. 슬롯 3개에 앵커 배치 후 버튼 함수 호출 확인(`editor_place_generator_actors.py`, Python은 `call_method`로 호출).

### P2-09 던전 베이커 `FTDDungeonBaker`
- 상태: doing
- 우선순위: 높음
- 선행: P2-08
- 목표: 선택된 레이아웃을 에디터 월드에 레벨 인스턴스로 배치·저장한다.
- 완료 조건:
  - 트랜잭션 안에서 `ALevelInstance` 스폰 → `SetWorldAsset` → `RequestLoadLevelInstance`, 폴더 `Dungeons/<DungeonId>`, 태그 `TDGenerated`, 생성물 GUID(결정론 파생) 기록
  - 재베이크 시 같은 DungeonId의 이전 생성물만 제거
  - 입구·출구 `ATDDungeonEntrance` 배치와 아틀라스 정의 갱신
  - OFPA 외부 패키지 포함 저장(`SavePackages`)
- 검증: 베이크 후 PIE로 입구 왕복(P1 코드 재사용)
- 참조: 03-architecture 3.5, research/persistence-editor-batch.md 2.1·2.2, research/level-instance.md
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 프로토타입 `Tools/DungeonGen/`로 선행 구현 — 흐름 5종(Linear/Branch/Loop/Hub/KeyLock)·크기 3종 생성기, 검증기 7항목, 후보 배치 툴(시드 1~100×3크기×5흐름 1,500건 통과율 100%, 같은 시드 해시 동일), 아틀라스 슬롯 베이크(`editor_build_dungeon.py`, 슬롯 원점 (300000+K×30000, 300000)cm). C++ 이식·데이터 에셋·실제 룸 모듈 아트는 남음. / 2026-09-12(Claude) C++: `Source/TDGameEditor/WorldGen/TDDungeonBaker.*` + `UTDWorldGenEditorLibrary::GenerateAndBakeDungeon`(트랜잭션, 슬롯 정의 갱신) 구현. / 2026-09-12: 에디터에서 C++ 베이커로 3슬롯 베이크·저장 확인(캡처 `Docs/Validation/ashen-vale/cpp_dungeon_slot0*.jpg`).

### P2-10 내비 후처리 검증
- 상태: done
- 우선순위: 중간
- 선행: P2-09, P1-02
- 목표: 베이크된 던전의 입구→보스 경로가 내비메시에서 끊기지 않는지 확인한다.
- 완료 조건: 에디터에서 `UNavigationSystemV1::Build()` 후 `TestPathSync`로 주요 경로 검사, 실패를 리포트에 추가
- 참조: R-90, research/dungeon-generation.md 5-2
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): `UTDWorldGenEditorLibrary::ValidateDungeonNavigation`(내비 Build 후 `TestPathSync` 입구→보스) 슬롯0 통과(6170cm, 12점). 내비메시는 Dynamic 런타임 생성(D-06).

### P2-11 룸 드레싱 PCG와 인카운터 데이터
- 상태: todo
- 우선순위: 중간
- 선행: P2-03, P0-03
- 목표: 방 내부 소품을 PCG로, 적 배치를 인카운터 데이터로 변형한다.
- 완료 조건:
  - `PCG_TDRoomDressing` 그래프: Get Volume Data → Volume Sampler/Create Points Grid → Attribute Partition → Point Match And Set → Static Mesh Spawner. 비파티션, Normal 모드
  - 베이커가 방 인스턴스별 시드를 컴포넌트 `Seed`에 대입하고 `GenerateLocal(true)` 후 저장
  - `UTDEncounterSet`(깊이 필터·가중치·난이도 예산)과 스폰 마커 매칭 로직(C++), 기존 전투 몬스터 클래스 스폰 연결
- 참조: R-41, R-42, research/pcg-api-and-nodes.md I절, research/dungeon-generation.md 7-3
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): C++ `UTDEncounterSet`/`FTDEncounterEntry`/`FTDEncounterResolver::Resolve`(깊이 필터·가중치 룰렛·난이도 예산, `FTDSeedContext` 결정론), 런타임 `ATDEncounterSpawner`(`TDSpawnMarker` 위치 스폰), 테스트 `TDGame.WorldGen.EncounterResolveIsDeterministic`/`EncounterRespectsDepthAndBudget` 통과. PCG 그래프 에셋 `Content/Dungeon/PCG/PCG_TDRoomDressing` 생성(Python). 남음: 베이커의 방별 시드 주입·`GenerateLocal`, 룸 레벨 적용, 인카운터 에셋 작성과 몬스터 클래스(`ATDMonsterCharacter`) 연결.

### P2-12 던전 10개 후보 생성 데모와 문서
- 상태: done
- 우선순위: 중간
- 선행: P2-07, P2-09, P2-11
- 목표: 버튼 한 번으로 후보 10개를 만들고 상위 3개를 베이크해 플레이 확인한다.
- 완료 조건: 시드·점수 표와 캡처를 `Docs/Validation/P2-12-candidates.md`에 기록, 설계서 R-43 흐름대로 수동 선택 기록
- 참조: R-43, R-92
- 기록: 2026-09-09 작성 / 2026-09-12(Claude): Python 스윕 10시드(Branch/Medium, 통과 10/10)와 C++ 커맨드릿 스윕(KeyLock/Medium 5시드) 표, 슬롯 3개 채택 시드 7/3/5와 근거, 플레이 확인을 `Docs/Validation/P2-12-candidates.md`에 기록. HollowCave/SunkenCrypt 걸어서 확인은 P2-D3 결정 후.

### P2-D3 그리드 단위 결정
- 상태: decision
- 우선순위: 높음
- 목표: 룸 그리드 단위(400 vs 500cm)와 캐릭터 이동 속도·카메라 거리 관계를 확인해 결정한다.
- 참조: decisions.md D-03
- 기록: 2026-09-09 작성

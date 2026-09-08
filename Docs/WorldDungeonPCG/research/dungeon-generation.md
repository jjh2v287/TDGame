[← 인덱스로](../../WorldDungeonPCG_Plan.md)

# 리서치: 던전 자동 생성 기법 (웹 + 엔진 소스, 2026-09-09)

## 1. 접근 비교

| 접근 | 요점 | 적용 판단 |
|---|---|---|
| Dungeon Architect Snap Grid Flow | 추상 그래프(Create Grid 3D → Create Main Path → Create Path(분기·루프) → Create Key Lock → Finalize) → 고정 청크 격자 노드 → 모듈 스냅. 도어는 청크 경계에만, 회전은 X=Y일 때. 자물쇠는 간선, 열쇠는 노드 마커. 실패 시 새 레이아웃 재시도, 300회 생성 통계 창 | 3단 분리 구조가 이 설계와 동형. 소스 비공개 상용이므로 구조만 참고 |
| Epic 실험 플러그인 Wave Function Collapse | `UWaveFunctionCollapseSubsystem : UEditorSubsystem`(에디터 전용), 6방향 인접 규칙, 백트래킹 없이 재시도 | 전역 순서(Key→Lock, 경로 길이)를 표현 못 해 방 조립에 부적합. 베이크 단계의 방 내부 타일 장식 2차 패스로만 후보 |
| PCG 기반 던전 | Epic 공식 던전 샘플 없음(미확인). 커뮤니티는 출구 포인트에 서브그래프로 다음 방을 붙이는 방식 | PCG는 레이아웃 확정 후 장식·스캐터에 적합 |
| BenPyton/ProceduralDungeon(오픈소스) | 방 = 서브레벨 + RoomData(정수 RoomUnit 크기, 도어 정의). DFS/BFS 성장, 도어 호환 필터, `TryPlaceRoom` 겹침 검사, 검증 실패 시 재생성(`MaxGenerationTry=500`, 도어당 `MaxRoomPlacementTry=10`) | "도어 소켓 + 겹침 검사 + 재시드" 골격 참고. 흐름 그래프 층은 위에 얹어야 함 |
| Edgar(.NET, MIT) | 레벨 그래프 + 방 템플릿 도어, 담금질(simulated annealing) 배치, 30방 이하 권장 | 루프 포함 배치 참고. 구현 부담 큼 |

## 2. 흐름 그래프 알고리즘
- Dormans(Unexplored) Cyclic Dungeon Generation: 큰 원형 루프를 두 호로 나누고 24종 사이클 패턴(잠긴 문+열쇠 우회, 위험/안전, 허브 등)을 적용, 부 사이클과 막다른 길 삽입. 1인 구현은 4~6종 사이클 템플릿만 하드코딩해도 충분.
- 그래프 문법(Dormans & Bakkes 2011): 규칙 4종 — 선형 과제 재배열(분기), 열쇠·자물쇠 삽입, 자물쇠 앞으로 이동, 열쇠 뒤로 이동. 일반 부분그래프 매칭은 과하므로 "노드 타입 1~2개 + 인접 간선" 고정 패턴 매칭으로 제한.
- Key/Lock 보장(구성적): 주 경로에 순서 인덱스 → 자물쇠는 간선(i→i+1), 열쇠는 인덱스 < i 노드에만 배치. 루프 추가 시 잠긴 간선을 우회하는 간선 금지(양 끝 노드의 필요 키 집합이 같을 때만 허용, metazelda 규칙). 최종 검증은 "보유 키 집합"을 갱신하는 BFS.
- Loop/Hub: 격자 스냅 방식에서는 루프 봉합이 "두 노드가 격자상 인접하고 양쪽 모듈에 도어가 있는가"로 단순화. 허브 = 도어 3개 이상 모듈 카테고리 + 분기 시작점.

## 3. 모듈 조립
- 도어 소켓: 방향 4개 열거형(N/E/S/W) + 폭 등급 + 타입 태그. 반대 방향 소켓끼리 정합, 정수 격자 변환으로 부동소수 오차 없음(BenPyton `FRoomTransform{FIntVector, EDoorDirection}`). 호환 표를 데이터 에셋화하면 잠긴 문 타입도 같은 메커니즘.
- 그리드 vs 자유 배치: Edgar·SGF·BenPyton 모두 "정수 그리드 + 90도 회전". 충돌 검사 = 셀 집합(`TSet<FIntVector>`) 비교. 0 두께 접촉은 충돌 아님(반개구간 비교).
- 백트래킹: 노드별 (후보 방 × 도어 × 회전) 시도 상한 10~20, 백트랙 깊이 상한 3~5, 초과 시 시드 파생값을 바꿔 전체 재생성(상한 수백). 베이크 파이프라인에서 무한 재시도 금지, 실패는 리포트.
- 복도: 간선 하나 = 복도 모듈 하나(도어 2개짜리 방)로 취급하면 Key-Lock 간선에 잠긴 문 복도를 직접 매핑. Loop 간선만 A* 폴백(하이브리드).

## 4. 결정론과 시드 (엔진 소스)
- `FRandomStream` — `Runtime/Core/Public/Math/RandomStream.h`: 32비트 LCG(`MutateSeed`), 플랫폼 간 동일. `Initialize(FName)`은 `GetTypeHash(FString)`(= `FCrc::Strihash_DEPRECATED`) 사용 → 파생 시드는 자체 고정 해시(CityHash64WithSeed/xxhash 하위 32비트)로.
- 비결정 원인: TMap/TSet 순회, 비동기 완료 순서, 표현식 내 함수 호출 순서, 동률 정렬, 에셋 로드 순서. 정수화로 임계값 비교 회피.
- 버전 관리 관행(Minecraft/Spelunky 2/DCSS): 결과에 `GeneratorVersion` 저장, 출시용은 시드가 아니라 베이크된 레이아웃 데이터로 고정, 시드 재생성은 동일 빌드 내 디버그·데일리 용도, CI 골든 시드 회귀 테스트. 방 참조는 이름이 아니라 GUID/PrimaryAssetId.

## 5. 검증기
- 그래프·기하 검사는 방 수십 개 규모에서 O(V+E)~O(V²), 후보 수백 개도 ms 단위. 하드 제약(연결성, 필수 방, 겹침, Key-Lock)은 거부, 소프트 지표(경로 길이 범위, 막다른 길 비율, 루프 수, 방 다양성)는 점수. 초기에는 거부 샘플링만, 지표 로그를 쌓은 뒤 가중치 도입.
- 내비 검사(엔진): `UNavigationSystemV1::FindPathSync`/`TestPathSync`(`NavigationSystem.h:650, 658, 679`), `ProjectPointToNavigation` `:712/718`. 에디터 베이크는 `UNavigationSystemV1::Build()`(`NavigationSystem.cpp:4441`, `EnsureBuildCompletion`으로 동기 대기) 후 검사. 후보마다 레벨 로드가 필요하므로 기하 검증으로 먼저 걸러 상위 1~3개만 내비 검사(2단계).
- 상용 사례(Hades/Spelunky/Gungeon/Dead Cells)는 모두 합격/불합격 재시도 방식. "후보 N개 점수 선택"은 학술(Search-Based PCG) 근거.

## 6. 방 단위 표현 (요약, 상세는 level-instance.md)
- 권장: 검증은 순수 데이터 → 시드 고정 → 에디터에서 `ALevelInstance`(임베디드)로 베이크 → `Build()`로 내비 동기 검증. 런타임 재조립이 필요하면 `ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr`(비동기, 라이팅 베이크 불가). 장식 전용 껍데기는 Packed Level Actor.

## 7. 탑다운 ARPG 특화
- 벽 가림: 방 모듈 규격에 "카메라 방향(남쪽) 벽 높이 ≤ N" 고정 + 회전 0°만 허용(또는 벽을 방향별 부속 메시로 분리). 보조로 높은 소품에만 디더 페이드.
- 방 크기: 셀 정수 배(예 400~500cm) + 도어를 셀 경계에. 최소 전투 공간은 "대시 거리×2 + 최대 적 반경×N"으로 자체 산정해 검증기 규칙화. Hades처럼 방 크기를 진행 난이도에 매핑.
- 인카운터 분리(Hades 모델): 방에는 스폰 마커(태그·반경·역할)와 `LegalEncounterTags`만, 인카운터 테이블은 깊이 필터 → 가중치 선택 → 난이도 예산으로 웨이브 구성. 방·인카운터·보상 3계층 분리.
- 특수방: Gungeon 모델(흐름 템플릿이 배치를 사전 결정) + Isaac식 거리 규칙("시작~보스 최단 경로 ≥ 전체 노드의 60%") 검증. 보물방은 사이드 브랜치 리프 + Key-Lock 뒤.

## 8. 에디터 도구 형태 (1인 개발 권장)
- 1단계: 액터 프로퍼티 + `CallInEditor` 버튼(Generate / Regenerate Unlocked / Bake) + `CheckForErrors`로 `FMessageLog` 리포트(`FActorToken` 클릭 이동). Undo·저장·베이크가 액터 프로퍼티 흐름과 일치.
- 2단계: 리포트 필터·부분 재생성 UI가 커지면 에디터 모듈 + `UToolMenus` + `SDockTab`(노마드 탭)으로 승격. Editor Utility Widget은 UI 에셋 diff 문제로 보조 용도.
- Interactive Tools Framework(UEdMode)는 뷰포트 드래그 편집이 필요할 때만.

## 이 프로젝트 결정
- 정수 격자(셀 400cm 기본, 데이터로 조정) + 4방향 도어 소켓 + 90도 회전, 복도는 모듈. 흐름 생성은 고정 패턴 규칙 4종 + Key-Lock 구성적 보장 + BFS 검증. 검증 2단계(기하 → 내비). 툴은 1단계(액터 + CallInEditor + MessageLog)로 시작.

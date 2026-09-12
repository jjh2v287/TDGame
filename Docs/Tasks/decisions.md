# 결정 대장

사용자 결정이 필요한 항목. 에이전트는 결정이 나기 전에는 해당 항목을 건드리지 않고 다른 작업을 진행한다. 결정되면 "결정" 칸에 날짜와 내용을 적고 관련 작업의 상태를 `todo`로 되돌린다.

### D-01 템플릿 잔재 정리
- 질문: `Source/TDGame/Variant_Strategy`, `Variant_TwinStick`(소스·콘텐츠·맵)을 삭제할까?
- 선택지: (a) 삭제 (b) 보존 (c) 별도 브랜치로 보존 후 삭제
- 권장: (a). 새 월드 작업과 무관하고 TD 접두어 규칙에도 어긋나며, 빌드 시간과 검색 잡음을 늘린다. 삭제는 git으로 되돌릴 수 있다.
- 영향: P0-D1
- 결정: (미정)

### D-02 시작 맵
- 질문: 새 월드 파티션 월드 `L_TDWorld_Main`을 `GameDefaultMap`/`EditorStartupMap`으로 바꿀까, LV-Game을 유지할까?
- 선택지: (a) 새 월드를 시작 맵으로, LV-Game은 전투 테스트 맵으로 유지 (b) LV-Game 유지, 새 월드는 별도 열어서 개발
- 권장: (a). 심리스 이동·저장 테스트가 시작 맵 기준으로 돌아야 자동화 테스트가 단순해진다. 전투 테스트 명령(`TDSpawnDamageTargets`)은 새 월드에서도 동작한다.
- 영향: P0-07
- 결정: (미정)

### D-03 룸 그리드 단위
- 질문: 룸 모듈 그리드 단위를 400cm로 할까 500cm로 할까?
- 선택지: (a) 400cm (b) 500cm (c) 기타
- 권장: (a) 400cm. 현재 캐릭터 캡슐(반지름 34cm)과 근접 공격 사거리를 고려하면 1칸 통로에서 회피 공간이 부족하지 않고, 방 크기 조합이 더 세밀하다. 최종 확정은 P1-09 프로파일링에서 측정한 카메라 가시 폭(대략 셀 몇 칸이 화면에 들어오는지)을 보고 한다.
- 영향: P2-D3, P2-01
- 결정: (미정)

### D-04 던전 아틀라스 전용 런타임 그리드
- 질문: 던전 슬롯 액터를 메인 그리드와 다른 `TargetGrids`(예: 셀 작고 로딩 범위 짧은 그리드)에 둘까?
- 선택지: (a) 메인 그리드 하나로 시작(설계서 R-12) (b) 던전 전용 그리드
- 권장: (a)로 시작하고 P1-09 결과에서 던전 안 동시 로드 셀이 과하면 (b)로 전환.
- 영향: P1-01, P1-09
- 결정: (미정)

### D-05 야외 3km×3km 바탕: 랜드스케이프 vs 평면 메시
- 질문: 야외 필드 바탕을 랜드스케이프로 만들지, 큰 평면 스태틱 메시 위에 PCG 식생·마을만 올릴지?
- 선택지: (a) 랜드스케이프(월드 파티션 스트리밍 프록시로 분할), 초기엔 거의 평평하게 두고 지역별 완만한 기복·절벽만 추가 (b) 평면 메시 + 높이 변화는 전부 메시로 (c) 평면으로 시작해 나중에 랜드스케이프로 교체
- 권장: (a). 랜드스케이프는 "평면"을 포함하는 상위 선택지다. 평평하게 시작해도 비용이 거의 없고, 나중에 바이옴 마스크(레이어 가중치 → PCG가 읽음), 도로·강 스플라인 변형, 내비메시 스트리밍, 수면·절벽 표현을 그대로 얻는다. (c)는 PCG 그래프·내비·콜리전 설정을 두 번 만들게 된다.
- 권장 파라미터(초기): 쿼드 1개 = 200cm(스케일 200), 컴포넌트 63쿼드 → 컴포넌트 한 변 126m, 3km는 24×24 컴포넌트(576개, 해상도 1513×1513). 탑다운 가시거리(수십 m)에 2m 해상도면 충분. 절벽·바위 같은 급격한 형태는 메시로, 랜드스케이프 경사는 이동 가능 범위(예 30° 이하)로 제한. 월드 파티션 셀 크기(P1-09)와 프록시 크기를 정렬.
- 영향: P3-00(생성 함수), P3-07(바이옴 PCG는 Get Landscape Data 기반), P1-09
- 결정: (미정) — 2026-09-12 메인 지역 프로토타입(P3-11)은 권장안 (a)로 진행함: 1008m×1008m, 쿼드 1m(스케일 100), 16×16 컴포넌트, 월드 파티션 그리드 2. 사용자 확정 필요(확장 시 2m 쿼드·큰 컴포넌트로 전환 가능).

### D-06 내비메시 런타임 생성 방식
- 질문: 월드 파티션 레벨의 내비메시를 정적(월드 파티션 청크 빌드)으로 둘지, 런타임 동적 생성으로 둘지?
- 선택지: (a) `RuntimeGeneration=Static` + `bIsWorldPartitioned` + `WorldPartitionNavigationDataBuilder` 청크 액터 (b) `RuntimeGeneration=Dynamic`(셀 로드 시 타일 생성)
- 실측(2026-09-12): (a)는 청크 60개 빌드·에디터 경로 검사는 통과했으나 PIE에서 심리스 이동 준비 판정(스트리밍 완료 + 목적지 내비 투영)이 60초 넘게 실패. (b)는 입장 0.3~0.5초, 귀환 4~12초로 왕복 성공, 자동화 3종 통과.
- 결정: (b) Dynamic 채택 — 2026-09-12 사용자 지시. 스크립트 `Tools/WorldGen/editor_setup_navmesh_dynamic.py`. (a) 재시도는 P1-09 셀 크기 프로파일링 뒤, 청크가 던전 슬롯 셀과 같이 로드되는지(`NavigationDataChunkGridSize` 정렬) 확인하는 조건으로 남김.
- 영향: P1-02, P1-09, P2-10

### D-07 월드 베이커 마커 액터 클래스
- 질문: 베이커가 POI/입구/배제 마커를 `ATargetPoint`+구조화 태그로 계속 놓을지, `ATDPoiAnchor` 등 전용 클래스로 바꿀지?
- 선택지: (a) TargetPoint + 태그(`TDPoi`, `TDGuid:<32자>`, `TDSeed:<n>`, `TDLocked`) 유지 (b) 전용 앵커 클래스로 전환
- 결정: (a) — 2026-09-12(Claude). 기존 베이크 액터·PCG Get Actor Data 태그 그래프와 호환. 클래스 전환은 별도 마이그레이션 작업으로 남김.

### D-08 부분 재생성의 도로 귀속
- 질문: 지역 단위 재생성(`BakeWorldLayoutInRegion`)에서 지역 경계를 걸치는 도로를 어느 쪽에 귀속할지?
- 결정: 도로 점 하나라도 지역 볼륨 안이면 그 지역 소속으로 삭제·재생성, 완전히 밖인 도로는 손대지 않는다(경계 밖 도로가 오래된 상태로 남을 수 있음). 2026-09-12(Claude).

### D-09 빌더 커맨드릿 검증 판정과 저장 범위
- 결정: `UTDWorldGenBuilder -Validate`는 생성 시 지형 검증(`Layout.Validation`)과 지형 없는 `ValidateWorldLayout` 둘 다 통과해야 PASS, 실패면 반환 `false`(종료 코드 ≠ 0). `-Bake` 시 `GetDirtyWorldPackages` 전부 저장하고 빈 패키지(삭제된 OFPA 액터)는 `DeletePackages`. 2026-09-12(Claude).

### D-10 입구 지형 적합성 검사 방식
- 질문: 던전 입구는 의도적으로 봉분(마운드) 안에 묻히므로 사방 표본 경사 검사가 항상 실패한다. 어떻게 검사할지?
- 선택지: (a) 입구는 검사 제외 (b) 입구 전방(접근로) 반원 표본만 검사 (c) 반경 축소
- 결정: (b) — 2026-09-12(Claude). `FTDWorldAnchor::YawDeg`(손수 배치 앵커의 향, `ATDWorldAnchorActor`는 액터 회전)를 입구 `Yaw`로 넘기고, 검증기는 입구 대상에 대해 전방 0.5R·R·1.5R과 ±40° R 표본으로 경사·수면을 본다. 메인 던전 앵커 yaw=135(생성기 facing과 동일). 스크립트 `Tools/WorldGen/editor_set_anchor_yaw.py`.
- 영향: P3-05, P3-10, `editor_make_definitions.py`

### D-11 PJGame(이전 프로젝트) 코드 이식 방침
- 질문: `C:\Project\PJGame`의 전투·AI·성능 코드를 TDGame에 어떻게 옮길지? (사용자 지시: 근접 노티파이 높이 고정 추가, 그 외 필요한 클래스 전부 "빌드만 되게" 이식, TDGame 소스 폴더 정리)
- 결정(2026-09-12, Claude):
  - ASC·어트리뷰트셋·데미지 이펙트·실행 계산은 이식하지 않는다. `UTDCombatComponent`가 유일한 ASC이며, 필요한 스태미나(`Stamina/MaxStamina`, `ConsumeStamina`)와 SetByCaller 쿨다운(`UTDActionCooldownEffect`, `Data.Cooldown.Duration`)만 TD 쪽에 추가했다.
  - 팀은 `FTDCombatStats.TeamId`(int32) 하나로 통일. `UPJTeamComponent`/`EPJTeamId`/`IPJDamageable`/`IPJInteractable`/`UPJFeatureToggleSettings`는 이식하지 않음(중복·미사용).
  - 데미지 진입점은 `UTDCombatLibrary::TryApplyDamage(FTDDamageSpec)` → `UTDCombatComponent::ReceiveDamage`. 데미지 타입 태그(`Damage.*`) 대신 `ETDDamageElement`.
  - 메시지 버스는 유지: PJ 프로젝트 플러그인 `GameplayMessageRouter`를 `Plugins/`로 복사해 활성화(`Event.Damage.Applied`, `Event.Actor.Death`, `Event.Caravan.Destroyed`).
  - 게임플레이 태그는 `Core/TDGameplayTags.h` 한 곳에 네이티브 매크로로 통합(`TAG_` 접두어 없음).
  - 근접 노티파이는 TD 것(`UTDAnimNotifyState_MeleeAttack`)을 유지하고 PJ의 높이 고정만 `bLockHeightToOwner`/`LockedHeightOffset` 플래그로 이식(테스트 `TDGame.Combat.MeleeAttackNotifyLocksBladeHeightToOwner`).
  - BehaviorTree 태스크(`UTDBTTask_CombatTokenRequestAndRelease`)는 컴파일용으로만 이식. TD AI는 StateTree이므로 실제 사용 시 StateTree 태스크로 다시 만든다.
- 폴더 정리: `Source/TDGame/{Core,Characters,Framework,Combat/{Damage,GAS,AnimNotify,Skills,Tests},AI,Performance,Actors,World/{Streaming,Persistence,Generation}}`. 클래스 이름은 바꾸지 않았으므로 블루프린트 참조는 유지된다.
- 영향: `Docs/MonsterAI_CombatSim/*`의 소스 경로 참조는 새 경로로 치환함(줄 번호는 변화 없음).

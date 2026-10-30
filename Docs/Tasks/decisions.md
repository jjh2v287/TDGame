# 결정 대장

사용자 결정이 필요한 항목. 에이전트는 결정이 나기 전에는 해당 항목을 건드리지 않고 다른 작업을 진행한다. 결정되면 "결정" 칸에 날짜와 내용을 적고 관련 작업의 상태를 `todo`로 되돌린다.

### D-01 템플릿 잔재 정리
- 질문: `Source/TDGame/Variant_Strategy`, `Variant_TwinStick`(소스·콘텐츠·맵)을 삭제할까?
- 선택지: (a) 삭제 (b) 보존 (c) 별도 브랜치로 보존 후 삭제
- 권장: (a). 새 월드 작업과 무관하고 TD 접두어 규칙에도 어긋나며, 빌드 시간과 검색 잡음을 늘린다. 삭제는 git으로 되돌릴 수 있다.
- 영향: P0-D1
- 결정: 2026-09-17 (a) 삭제. 소스 40개 파일, 콘텐츠 56개 + 외부 액터 183개를 제거하고 `TDGame.Build.cs`의 변형 인클루드 경로 6줄을 지웠다. 빌드·자동화 테스트 45건 통과.

### D-02 시작 맵
- 질문: 새 월드 파티션 월드 `L_TDWorld_Main`을 `GameDefaultMap`/`EditorStartupMap`으로 바꿀까, LV-Game을 유지할까?
- 선택지: (a) 새 월드를 시작 맵으로, LV-Game은 전투 테스트 맵으로 유지 (b) LV-Game 유지, 새 월드는 별도 열어서 개발
- 권장: (a). 심리스 이동·저장 테스트가 시작 맵 기준으로 돌아야 자동화 테스트가 단순해진다. 전투 테스트 명령(`TDSpawnDamageTargets`)은 새 월드에서도 동작한다.
- 영향: P0-07
- 결정: (미정)
- 보류: 2026-09-18 사용자 지시로 미룸(관리 체계 정리 뒤 재개).

### D-03 룸 그리드 단위
- 질문: 룸 모듈 그리드 단위를 400cm로 할까 500cm로 할까?
- 선택지: (a) 400cm (b) 500cm (c) 기타
- 권장: (a) 400cm. 현재 캐릭터 캡슐(반지름 34cm)과 근접 공격 사거리를 고려하면 1칸 통로에서 회피 공간이 부족하지 않고, 방 크기 조합이 더 세밀하다. 최종 확정은 P1-09 프로파일링에서 측정한 카메라 가시 폭(대략 셀 몇 칸이 화면에 들어오는지)을 보고 한다.
- 영향: P2-D3, P2-01
- 결정: (미정)
- 보류: 2026-09-18 사용자 지시로 미룸(관리 체계 정리 뒤 재개).

### D-04 던전 아틀라스 전용 런타임 그리드
- 질문: 던전 슬롯 액터를 메인 그리드와 다른 `TargetGrids`(예: 셀 작고 로딩 범위 짧은 그리드)에 둘까?
- 선택지: (a) 메인 그리드 하나로 시작(설계서 R-12) (b) 던전 전용 그리드
- 권장: (a)로 시작하고 P1-09 결과에서 던전 안 동시 로드 셀이 과하면 (b)로 전환.
- 영향: P1-01, P1-09
- 결정: (미정)
- 보류: 2026-09-18 사용자 지시로 미룸(관리 체계 정리 뒤 재개).

### D-05 야외 3km×3km 바탕: 랜드스케이프 vs 평면 메시
- 질문: 야외 필드 바탕을 랜드스케이프로 만들지, 큰 평면 스태틱 메시 위에 PCG 식생·마을만 올릴지?
- 선택지: (a) 랜드스케이프(월드 파티션 스트리밍 프록시로 분할), 초기엔 거의 평평하게 두고 지역별 완만한 기복·절벽만 추가 (b) 평면 메시 + 높이 변화는 전부 메시로 (c) 평면으로 시작해 나중에 랜드스케이프로 교체
- 권장: (a). 랜드스케이프는 "평면"을 포함하는 상위 선택지다. 평평하게 시작해도 비용이 거의 없고, 나중에 바이옴 마스크(레이어 가중치 → PCG가 읽음), 도로·강 스플라인 변형, 내비메시 스트리밍, 수면·절벽 표현을 그대로 얻는다. (c)는 PCG 그래프·내비·콜리전 설정을 두 번 만들게 된다.
- 권장 파라미터(초기): 쿼드 1개 = 200cm(스케일 200), 컴포넌트 63쿼드 → 컴포넌트 한 변 126m, 3km는 24×24 컴포넌트(576개, 해상도 1513×1513). 탑다운 가시거리(수십 m)에 2m 해상도면 충분. 절벽·바위 같은 급격한 형태는 메시로, 랜드스케이프 경사는 이동 가능 범위(예 30° 이하)로 제한. 월드 파티션 셀 크기(P1-09)와 프록시 크기를 정렬.
- 영향: P3-00(생성 함수), P3-07(바이옴 PCG는 Get Landscape Data 기반), P1-09
- 결정: (미정) — 2026-09-12 메인 지역 프로토타입(P3-11)은 권장안 (a)로 진행함: 1008m×1008m, 쿼드 1m(스케일 100), 16×16 컴포넌트, 월드 파티션 그리드 2. 사용자 확정 필요(확장 시 2m 쿼드·큰 컴포넌트로 전환 가능).
- 보류: 2026-09-18 사용자 지시로 미룸(관리 체계 정리 뒤 재개).

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
  - 근접 노티파이는 TD 것(`UTDAnimNotifyState_MeleeAttack`)을 유지하고 PJ의 높이 고정만 `bLockHeightToOwner`/`LockedHeightOffset` 플래그로 이식(테스트 `TDGame.Combat.MeleeAttackNotifyLocksBladeHeightToOwner`). (2026-09-30 D44로 대체: 이 노티파이 클래스는 삭제됐고 스윕 코어는 `FTDMeleeSweep`(`Combat/TDMeleeSweep.*`)로 옮겨 능력 태스크가 시간표로 구동한다. 테스트는 `TDGame.Combat.MeleeSweep.LocksBladeHeightToOwner`.)
  - BehaviorTree 태스크(`UTDBTTask_CombatTokenRequestAndRelease`)는 컴파일용으로만 이식. TD AI는 StateTree이므로 실제 사용 시 StateTree 태스크로 다시 만든다. (2026-09-30 D44로 대체: TD AI는 C++ 유틸리티 + FSM이며 StateTree는 쓰지 않는다. 토큰 요청·반납은 몬스터 FSM 행동 원시로 다시 만든다.) (2026-10-18 claude M3-29: `AI/CombatToken/` 전체와 `ATDMonsterCharacter` 토큰 래퍼 삭제, 대체는 `MonsterAI/TDCombatTactics.*`. 제안(규칙 문서 변경이라 사용자 승인 필요): `AGENTS.md` 14절 폴더 목록 `AI/{CombatToken,NPC}/` → `AI/NPC/`)
- 폴더 정리: `Source/TDGame/{Core,Characters,Framework,Combat/{Damage,GAS,AnimNotify,Skills,Tests},AI,Performance,Actors,World/{Streaming,Persistence,Generation}}`. 클래스 이름은 바꾸지 않았으므로 블루프린트 참조는 유지된다. (2026-09-30 D44로 대체: `Combat/AnimNotify`와 `Framework/ThirdPerson`은 삭제됐다.)
- 영향: `Docs/MonsterAI_CombatSim/*`의 소스 경로 참조는 새 경로로 치환함(줄 번호는 변화 없음).

### D-38 P2-04 흐름 그래프 생성기 완료 조건 해석
- 질문: 조건은 템플릿 파라미터로 방 수·분기 수·루프 수 제어, 열쇠 인덱스 < i인데 구현은 방 수 = Size 프리셋(소·중·대), MaxBranches = 부착점당 용량, 루프 최대 1(잠금과 비공존), 열쇠 ≤ i(잠금 앞쪽). 현재 동작을 인정할까, 조건대로 보강할까?
- 선택지: (a) 현재 동작 인정·조건 문구 정정 (b) 템플릿 필드·다중 루프·열쇠 < i로 보강(결과·해시 변경)
- 권장: (a). 1,500건 통과·결정론 해시가 유지되고 버티컬 슬라이스에 충분하다.
- 영향: P2-04
- 결정: 2026-09-30 사용자 (a). 작은 보완(시드 1~500 키·잠금 테스트)만 하고 닫는다.

### D-39 P2-05 레이아웃 솔버 완료 조건 해석
- 질문: 조건은 DFS 배치·깊이 4 백트랙·`TSet<FIntVector>`·모듈 GUID인데 구현은 BFS 부착·누적 4회 백트랙·단층 `FIntPoint` 셀·모듈 이름(FName). 현재 방식을 인정할까, 조건대로 재구현할까?
- 선택지: (a) 현재 방식 인정·조건 문구 정정, GUID는 실제 룸 아트가 생기는 P4로 미룸 (b) DFS·깊이 백트랙·FIntVector·GUID 재구현(결과·해시·골든 변경)
- 권장: (a). 근거 문서(dungeon-generation.md)는 DFS/BFS 모두 허용하고, 던전은 단층이다.
- 영향: P2-05
- 결정: 2026-09-30 사용자 (a). 작은 보완(시드 1~500 성공률 ≥90% 판정·실패 사유 집계 리포트)만 하고 닫는다.

### D-40 P3-00 높이맵 생성기 정본
- 질문: C++ `FTDHeightmapGenerator`·배열 입력 `CreateLandscape`·`ImportHeightmapFromTexture` 대신 쓰고 있는 numpy 생성기(`Tools/WorldGen/generate_ashen_vale.py`) + raw 입력 C++ `CreateLandscapeFromRawFiles`를 정본으로 인정할까?
- 선택지: (a) numpy + raw 경로 정본 인정·조건 문구 정정 (b) C++ 생성기 이식(큼)
- 권장: (a). 같은 시드 바이트 단위 재현이 확인되고 파이프라인이 이미 운영 중이다.
- 영향: P3-00
- 결정: 2026-09-30 사용자 (a). 해시 검증·에디터 측정·절차 문서화만 하고 닫는다(`Docs/Validation/P3-00-landscape.md`).

## 관리 체계 (다중 에이전트 공통 규칙, 2026-09-18 제안)

인용은 `Docs/Tasks/decisions.md#D-12`처럼 `경로#ID`. 새 항목부터 `상태:` 줄을 둔다. 근거: `Docs/AgentCollaboration_Plan.md`. 결정 전에는 에이전트가 해당 항목을 선점하는 변경을 하지 않는다(OP-28).

### D-12 Codex 전역 지침의 "AGENTS.md 참고 하지 않는다"
- 질문: `~/.codex/AGENTS.md` 마지막 줄 "AGENTS.md 참고 하지 않는다"를 지울까? Codex는 전역 파일 뒤에 프로젝트 `AGENTS.md`를 이어 붙이므로 지금은 모순된 지시 두 개가 동시에 들어간다.
- 선택지: (a) 그 줄 삭제 (b) "프로젝트 AGENTS.md가 있으면 그것을 우선한다"로 교체 (c) 유지
- 권장: (a). 다른 프로젝트에서도 프로젝트 AGENTS.md를 무시할 이유가 없다. 사용자 파일이므로 사용자가 직접 고친다.
- 영향: OP-03, 모든 Codex 세션
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) 삭제 — 사용자가 직접 지움(2026-09-18 확인: 해당 줄 없음). (2026-09-18 사용자 결정, claude 적용)

### D-13 Gemini/Antigravity 전역 지침 충돌
- 질문: `~/.gemini/GEMINI.md`(Gemini CLI와 Antigravity가 공유) 마지막 줄 "AGENTS.md 참고 하지 않는다"를 지울까? 같은 파일의 "불분명한 부분이 있다면 작업을 중단하세요"는 프로젝트 `AGENTS.md` 1절('중요한 모호성만 질문, 그 외 가정 명시')과 충돌한다. 루트 `GEMINI.md`는 유지할지(현재 도구 절차 위주) 삭제할지.
- 선택지: (a) 금지 줄 삭제 + 중단 규칙을 "중대한 모호성만"으로 완화 + 루트 GEMINI.md 유지 (b) 금지 줄만 삭제 (c) 유지
- 권장: (a). 루트 GEMINI.md는 Antigravity가 AGENTS.md와 함께 읽으므로 규칙을 두지 않고 도구 절차만 남긴다(이번에 머리 1줄 추가함).
- 영향: OP-03, Gemini CLI·Antigravity 세션
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) — 금지 줄은 사용자가 지웠고, "불분명하면 중단" 문장은 claude가 백업(`GEMINI.md.bak-20260918`) 후 Codex 전역과 같은 "중대한 모호성만 질문" 문구로 완화(그 한 문장만 변경, 줄 끝 CRLF 유지). 루트 GEMINI.md 유지. (2026-09-18 사용자 결정, claude 적용)

### D-14 전역 출력 형식과 완료 보고 2줄의 관계
- 질문: Codex·Gemini 전역 지침의 '요약/접근 방식/코드 구현/설명' 4절 형식과 `AGENTS.md` 10절의 고정 2줄(`읽음:`·`기록 갱신:`)을 어떻게 겹칠까? `~/.claude/CLAUDE.md`의 "프로젝트의 AGENTS.md을 참고 하세요"(이제 CLAUDE.md 임포트로 대체됨)와 "메모리에 저장"(프로젝트 사실은 Lessons로)을 고칠까?
- 선택지: (a) 2줄을 '설명' 절 맨 앞·맨 뒤에 넣는다(현행 OP-06) (b) 전역 형식에 '기록' 절을 추가 (c) 전역 형식을 프로젝트에서는 해제
- 권장: (a). 전역 파일 수정 없이 동작한다.
- 영향: OP-06
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) 2줄을 설명 절 안에. 전역 파일 변경 없음. (2026-09-18 사용자 결정, claude 적용)

### D-15 `Tools/Animation/` 11개(Kimodo text-to-motion 파이프라인) 처분
- 질문: 어느 문서에서도 참조되지 않고 현 정책(text-to-motion 서비스를 가정하지 않음)과 모순되는 실험 스크립트 11개를 어떻게 할까? `C:\Tools\kimodo` 설치 여부와 재사용 의사는 사용자만 안다.
- 선택지: (a) `Tools/_archive/2026-09/`로 이동 (b) 삭제 (c) 유지하되 상태 '보류·실행 전 사용자 확인' 표기(현재)
- 권장: (a). 이동 시 참조 0건이라 문서 수정 불필요.
- 영향: OP-13·OP-21, Tools/README.md 3c절
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) `Tools/_archive/2026-09/Animation/`으로 이동(git mv). 참조 0건. (2026-09-18 사용자 결정, claude 적용)

### D-16 `.gemini/scripts/` 13개 처분
- 질문: `unreal_mcp.py`(=`Tools/uemcp.py`), `take_screenshot.py`(=`capture_views_mcp.py`), `build_*_world.py` 6개(WorldGen 베이크와 같은 레벨을 대상으로 해 결과를 서로 지움)는 중복이고, `check_editor_connection.py`·`populate_*.py` 3개·`setup_real_landscape.py`는 이관 후보다.
- 선택지: (a) 중복 8개 `_archive`, 나머지 5개 `Tools/WorldGen/`으로 이름 규칙대로 이관·등록 (b) 전부 `_archive` (c) 유지
- 권장: (a). Gemini의 brain/knowledge가 이 경로를 참조할 수 있으니 이동 전 `rg`로 확인.
- 영향: OP-13, Tools/README.md 3c절
- 상태: accepted (2026-09-18 사용자)
- 결정: 조정안: 13개 전부 `Tools/_archive/2026-09/gemini-scripts/`로 보관, 이관 0개(populate·setup 스크립트는 현행 월드 생성 파이프라인과 충돌, check_editor_connection은 ue_editor.py status와 중복). (2026-09-18 사용자 결정, claude 적용)

### D-17 Claude 홈 `~/.claude/projects/C--Project-TDGame/tools/` 14개 이관
- 질문: 저장소 밖 Claude 전용 폴더의 스크립트(`run_tests.py` 자동화 테스트 러너, `save_editor_capture.py`, `make_montage.py` 등 저장소에 대응물 없는 것 포함)를 `Tools/`로 옮길까?
- 선택지: (a) 대응물 없는 것만 이름 규칙대로 이관·등록, 중복(`uemcp.py` 동일본 등)은 삭제 (b) 전부 이관 (c) 방치
- 권장: (a).
- 영향: OP-13·OP-19
- 상태: accepted (2026-09-18 사용자)
- 결정: 조정안: `run_tests.py`만 `Tools/check_automation_tests.py`로 이관·등록하고 홈 원본과 `uemcp.py` 동일본은 삭제. 나머지 12개 일회용은 홈에 유지. (2026-09-18 사용자 결정, claude 적용)

### D-18 추적 중인 생성물 해제
- 질문: git이 추적 중인 `__pycache__/*.pyc` 14개와 `Tools/AnimationAuthoring/smoke-report.json`을 `git rm --cached`로 해제할까(.gitignore 규칙은 이번에 추가함)? 스모크 리포트는 `Saved/AnimationAuthoring/`로 옮기고 대장 참조를 고친다.
- 선택지: (a) 다음 커밋에서 해제·이동 (b) 유지
- 권장: (a).
- 영향: OP-18
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) `.pyc` 14개 추적 해제(git rm --cached). 스모크 리포트는 `Docs/Validation/anim/A-01-smoke-report-2026-09-16.json`으로 이동(git mv)하고 참조 4문서 수정. 이후 실행 결과는 `Saved/AnimationAuthoring/`. (2026-09-18 사용자 결정, claude 적용)

### D-19 Antigravity MCP 설정 위치
- 질문: 작업 공간 `.agents/mcp_config.json`을 신설했다(Run-BlenderMCP.ps1 방식). 사용자 홈 `~/.gemini/config/mcp_config.json`(blender를 `uvx blender-mcp`로 실행, 저장소 방식과 다름)은 삭제할지 유지할지. Antigravity 데스크톱이 작업 공간 파일을 인식하는지는 실측이 필요하다.
- 선택지: (a) 작업 공간 파일 인식 확인 후 홈 파일 삭제 (b) 홈 파일을 저장소 방식으로 맞춤 (c) 유지
- 권장: (a).
- 영향: OP-31
- 상태: accepted (2026-09-18 사용자)
- 결정: (b) 정렬 — 홈 `~/.gemini/config/mcp_config.json`(antigravity/는 심볼릭 링크)의 blender 항목을 저장소 런처 방식으로 교체(백업 `mcp_config.json.bak-20260918`). D-36과 통일. (2026-09-18 사용자 결정, claude 적용)

### D-20 `~/.codex/skills/td-combat-animation-quality` 수동 사본 삭제
- 질문: 저장소에 `.agents/skills/` 스텁을 두었으므로 홈 사본은 같은 name이 두 번 노출된다. 삭제할까?
- 권장: 삭제. Codex는 같은 name 스킬을 병합하지 않는다.
- 영향: OP-22
- 상태: accepted (2026-09-18 사용자)
- 결정: 삭제 — 정본과 내용 동일(줄 끝만 다름) 확인 후 `~/.codex/skills/td-combat-animation-quality` 제거. (2026-09-18 사용자 결정, claude 적용)

### D-21 기존 도구 개명
- 질문: 접두어·동사 규칙(OP-14)에 맞지 않는 기존 파일 약 30개(`Tools/Animation/step1_*`, `anim_report.py`, 루트 `tasks_recount.py`·`ue_editor.py` 등)를 개명할까? `pie_*.py`는 규칙에 포함시켰다.
- 선택지: (a) 개명하지 않고 allowlist로 점진 축소 (b) 한 번에 개명 + 참조 일괄 수정
- 권장: (a). 문서·메모리의 경로 참조가 깨진다.
- 영향: OP-14
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) 개명하지 않음. allowlist는 검사 스크립트(Backlog 2번) 도입 시. (2026-09-18 사용자 결정, claude 적용)

### D-22 공용 MCP 클라이언트 분리
- 질문: `Tools/AnimationAuthoring/smoke_test.py` 안의 `TDMcpAnimationClient`를 `Tools/BlenderAnimation`이 import한다. `Tools/uemcp.py` 또는 `Tools/lib/`로 분리할까(애니메이션 대장 F-01과 같은 항목)?
- 권장: 분리(F-01 진행 시).
- 영향: OP-13
- 상태: accepted (2026-09-18 사용자)
- 결정: F-01 진행 시 분리. (2026-09-18 사용자 결정, claude 적용)

### D-23 `Docs/Validation/` 옛 증거 처분
- 질문: GAS 전환 전 PIE 검사 이미지·JSON(README가 '현재 검증으로 쓰지 않는다'고 명시)과 어느 md에서도 참조되지 않는 미디어를 `Saved/` 또는 `_archive`로 옮길까? `landscape-mcp-test-*.png` 2개는 GAS와 무관한 실측이므로 구분.
- 선택지: (a) 참조 없는 옛 증거만 이동 (b) 유지
- 권장: (a). 저장소 55MB 중 상당수.
- 영향: OP-24
- 상태: accepted (2026-09-18 사용자)
- 결정: 조정안: GAS 전환 전 미참조 10개만 `Docs/Validation/_archive/pre-gas/`로 이동. ashen-vale 캡처 24개는 P3-11 증거로 유지, weighty-v03 5개는 v04 문서화 뒤 판단. (2026-09-18 사용자 결정, claude 적용)

### D-24 줄 끝·인코딩 규칙
- 질문: `core.autocrlf=true`이고 `.gitattributes`에 eol 규칙이 없어 체크아웃마다 줄 끝이 바뀔 수 있다. `*.md text eol=lf` 같은 규칙과 'UTF-8·BOM 없음' 고정을 넣을까?
- 권장: `*.md`·`*.py`·`*.json`에 `text eol=lf` + UTF-8 고정. 적용 시 한 번 재정규화 커밋이 생긴다.
- 영향: OP-02, Docs/AgentRules.md 4절
- 상태: accepted (2026-09-18 사용자)
- 결정: 채택. 2026-09-18 11:51 사용자 커밋으로 작업 트리가 정리된 뒤 `.gitattributes`에 `*.md *.py *.json text eol=lf` 추가(claude). 재정규화(`git add --renormalize .`)와 커밋은 사용자가 1회 실행. (2026-09-18 사용자 결정, claude 적용)

### D-25 동시 세션 운용 여부
- 질문: 한 기계에서 Claude Code·Codex·Antigravity를 동시에 돌릴 계획이 있는가? 있으면 `Docs/AgentRules.md` 5절 보류 규칙(7일 회수·자원 잠금)을 활성화해야 한다.
- 권장: 순차 세션이면 현행 유지('에이전트당 동시 doing 1개'만).
- 영향: OP-11, AgentRules 5절
- 상태: accepted (2026-09-18 사용자)
- 결정: 순차 세션. 동시 운용 안 함(D-32). (2026-09-18 사용자 결정, claude 적용)

### D-26 하위 에이전트 정책
- 질문: 하위 에이전트(서브에이전트)는 장부를 쓰지 않고 부모가 1회 기록한다(현행 15절). 별도 식별자(`claude-sub` 등)가 필요한가?
- 권장: 불필요. 부모 식별자로 통일.
- 영향: OP-10
- 상태: accepted (2026-09-18 사용자)
- 결정: 불필요. 부모 식별자로 통일. (2026-09-18 사용자 결정, claude 적용)

### D-27 도입 범위와 이관 순서
- 질문: 이번에 핵심(15절·규칙 정본·NOW/Worklog/Lessons·배선·결정 대장)만 넣었다. 남은 이관(Claude 메모리 8개 → Lessons, `Docs/README.md` 문서 인덱스, `Docs/Animation_Plan.md`, `Tools/tasks_recount.py` 세 대장 확장, 검사 스크립트 12종)을 언제 어떤 순서로 할까? `Docs/Automation_Backlog.md` 참조.
- 권장: 2주 운영 뒤 Worklog 지표(세션 수·NOW 갱신율·등록 누락·`적중:` 수)로 착수 순서를 정한다. 예상 1순위 `make_status_snapshot.py`.
- 상태: accepted (2026-09-18 사용자)
- 결정: 2주 운영 뒤 Worklog 지표로 순서 결정. (2026-09-18 사용자 결정, claude 적용)

### D-28 미커밋 `Tools/BlenderAnimation` grip 스크립트 5개
- 질문: `author_sword_grip.py`·`inspect_grip.py`·`prepare_grip_revision.py`·`render_grip_review.py`·`validate_grip_revision.py`는 어느 문서에도 없다. 등록·커밋 / `Tools/scratch/` / `AnimationSources/<이름>/scripts/` 중 어디로?
- 권장: 재사용 가능하면 docstring 4줄 채워 등록·커밋, 1회용이면 scratch.
- 영향: OP-16·OP-17
- 상태: accepted (2026-09-18 사용자)
- 결정: 조정안: 5개를 `Tools/README.md` 3c절과 `Tools/BlenderAnimation/SKILL.md` 스크립트 표에 등록. docstring 4줄은 사용자 커밋(11:51) 뒤 claude가 추가(2026-09-18). (2026-09-18 사용자 결정, claude 적용)

### D-29 에셋·원본 버전 복제 정책
- 질문: `AnimationSources/`·`Content/.../Anims/Blender/`의 `_v03`·`_v04`·`_v05` 복제본(LFS 26MB+) 중 현행 1개만 대장 산출물 칸에 적고 옛 버전은 삭제·보관 중 무엇으로?
- 권장: 현행 1개 표기 + 옛 버전은 사용자 지시 시 삭제.
- 영향: OP-18
- 상태: accepted (2026-09-18 사용자)
- 결정: 정책 채택(현행 1개만 표기, 옛 버전은 지시 시 삭제). (2026-09-18 사용자 결정, claude 적용)
- 갱신 2026-09-19: 사용자 지시로 v02~v05·RToL_Blender·`Anims/Sword/AS_Sword_Slash_01`(언리얼 10개)과 `AnimationSources/Player` 원본 15개를 삭제(`git rm` 스테이징, 커밋은 사용자). 현행은 `/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_SwordSlash_RToL`·`AM_…_SwordSlash_RToL`, 원본 `AnimationSources/Player/AS_TD_Player_Attack01_SwordSlash_RToL.{blend,fbx,json}`, 기록 `Docs/AnimationQuality.md` 첫 절.

### D-30 VS Code Copilot 실사용 여부
- 질문: `.vscode/mcp.json`이 있다. VS Code Copilot을 실제로 쓰면 식별자 `copilot`을 추가하고, 안 쓰면 `.vscode/mcp.json`을 OP-31 동기화 대상에서 뺀다.
- 상태: accepted (2026-09-18 사용자)
- 결정: 미사용 가정 — `.vscode/mcp.json`은 두되 OP-31 동기화 대상에서 제외. Copilot을 쓰게 되면 식별자 `copilot` 추가와 함께 재결정. (2026-09-18 사용자 결정, claude 적용)

### D-31 `AGENTS.md` 15절 핵심 함정 초기 승격
- 질문: 이미 2곳 이상에 중복 기록됐던 함정(Git Bash `/Game/` 경로 변환 L-repo-01, 새 C++ 모듈 라이브 코딩 불가 L-build-01, 캡처는 CaptureViewport만 L-editor-03, PowerShell rg 와일드카드 L-repo-02)을 15절에 한 줄씩 넣을까(약 600바이트)?
- 권장: L-repo-01·L-build-01·L-repo-02 세 줄만.
- 영향: OP-26
- 상태: accepted (2026-09-18 사용자)
- 결정: 3줄 승격(L-repo-01·L-build-01·L-repo-02). 예산 확보를 위해 11절의 도구 검색 절차 문장을 `Tools/README.md` 0b절로 이동. (2026-09-18 사용자 결정, claude 적용)

### D-32 동시 작업의 재정의와 자원 임대 규칙 활성화 (딥리서치 2차 결과, D-25의 구체안)
- 질문: 같은 작업 트리에서 Codex(애니메이션, 에디터 필요)와 Claude(전투 C++, 빌드 필요)를 "동시에" 돌리는 것은 Windows에서 성립하지 않는다. 에디터가 열려 있으면 라이브 코딩 뮤텍스와 DLL 잠금 때문에 Build.bat이 거부되고(`Tools/ue_editor.py`의 cmd_build도 에디터 실행 중이면 종료 코드 2), 라이브 코딩은 디스크 DLL을 갱신하지 않아 헤드리스 테스트가 옛 코드를 돈다. 따라서 코드 편집만 병렬이고 빌드·테스트·PIE·MCP 호출·재시작은 에디터 자원 하나를 시분할한다. 이 전제 아래 `Docs/AgentRules.md` 5절 보류 규칙을 행위 조건부([B])로 올리고 자원 임대 스크립트(`Docs/Automation_Backlog.md` 11번 수정본)를 만들까?
- 선택지: (a) 5절 활성 + 잠금 스크립트 2층까지 채택 (b) 규칙만 활성(스크립트는 첫 충돌 관측 뒤) (c) 동시 운용을 하지 않고 현행 유지
- 권장: 동시 세션을 실제로 돌릴 계획이면 (a). 규칙 내용은 `Docs/AgentCollaboration/05-concurrency-and-handoff.md` 4절 두 번째 항목. AGENTS.md는 0바이트 변경, 세션당 토큰은 동시 세션일 때만 약 1,000자 추가.
- 영향: OP-11·OP-18·OP-29·OP-31·OP-32 제자리 보강, AgentRules 5절, Backlog 11번, `Tools/ue_editor.py`·`uemcp.py`·`run_in_editor.py`·`BlenderMCP/call_tool.py` 진입부 검사
- 상태: accepted (2026-09-18 사용자)
- 결정: (c) 동시 운용 안 함. AgentRules 5절은 보류 유지, 잠금 스크립트 미제작. (2026-09-18 사용자 결정, claude 적용)

### D-33 진짜 병렬 빌드(별도 체크아웃) 실험 여부
- 질문: 에디터가 열린 동안 다른 에이전트가 실제로 빌드·테스트하려면 별도 체크아웃(수동 git worktree 또는 사본, 자체 Binaries·Intermediate·Saved, LFS 수 GB·DDC 재생성)과 에디터 쪽 라이브 코딩 끄기가 필요하다. 선결 실험은 `UnrealEditor-Cmd -nullrhi` 헤드리스 자동화 테스트가 Content 없이 전투 테스트를 통과하는지(30~60분). 실험할까?
- 선택지: (a) 실험 후 가능하면 도입 (b) 시분할로 만족, 실험 안 함
- 권장: 동시 운용 빈도가 주 1회 미만이면 (b).
- 상태: accepted (2026-09-18 사용자)
- 결정: 안 함(D-32에 따라). (2026-09-18 사용자 결정, claude 적용)

### D-34 죽은 세션 인수 경로
- 질문: 할당량 소진은 Codex 앱을 죽이지 않으므로, 사용자가 Codex Desktop 스레드의 마지막 메시지를 읽어 대장 기록 칸에 `YYYY-MM-DD 진행(codex): 완료 …, 다음 …, 검증 …` 3줄로 옮기는 무비용 경로가 먼저다. `~/.codex/sessions`의 rollout JSONL을 파싱해 초안을 만드는 `Tools/draft_session_handoff.py`(Backlog 13번, 모델 호출 0, 파일 순서·명령·마지막 발화·추론 요약 제목 복원, 상세 추론은 암호화라 불가)는 그 경로가 실패한 죽은 세션이 1회 관측된 뒤에만 착수할까?
- 선택지: (a) 관측 뒤 착수(권장) (b) 지금 착수 (c) 파서 없이 규칙만
- 영향: OP-29 기록 칸 `진행`·`인수` 동사(형식 변경 없음, 새 Worklog 유형 없음)
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) 죽은 세션 1회 관측 + 앱 스레드 복원 실패 뒤 착수. (2026-09-18 사용자 결정, claude 적용)

### D-35 훅 자동 저널(Claude 1벌) 착수 조건
- 질문: Claude Code 훅(PostToolUse·PostToolUseFailure·Stop·StopFailure·SessionStart)으로 `Saved/AgentOps/journal/`에 토큰 0의 결정론 기록(도구 이름·저장소 상대 경로·종료 코드·시각)을 남기는 `check_agent_ops.py --journal`(Backlog 10번 통합 사양)을 언제 만들까? Codex Desktop·Antigravity Windows에서는 훅 발화가 미확인이라 Claude 1벌부터.
- 선택지: (a) 첫 동시 세션 충돌 1회 또는 기록 누락 2회 관측 뒤(권장) (b) 지금
- 상태: accepted (2026-09-18 사용자)
- 결정: (a) 첫 충돌 1회 또는 기록 누락 2회 관측 뒤 착수. (2026-09-18 사용자 결정, claude 적용)

### D-36 Blender MCP 텔레메트리 차단 (보안, 즉시 권장)
- 질문: GUI Blender 5.2에 설치된 사용자 애드온 `blender_mcp.py`는 텔레메트리 동의 설정을 갖고 있고 상류 약관은 프롬프트·생성 코드·장면 메타데이터·뷰포트 캡처를 AI 학습·공개 데이터셋 용도로 수집한다고 명시한다. 저장소 런처(`Tools/BlenderMCP/Run-BlenderMCP.ps1`)는 `DISABLE_TELEMETRY=true`로 띄우지만, 홈 `~/.gemini/config/mcp_config.json`(Antigravity `~/.gemini/antigravity/mcp_config.json`은 이 파일의 심볼릭 링크)은 `uvx blender-mcp`로 띄워 이 보호가 없다. GUI Blender의 애드온 환경설정에서 telemetry_consent를 끄고 저장하거나 애드온을 비활성화하고, 홈 설정의 blender 항목을 저장소 방식으로 맞출까(D-19 (b))?
- 권장: 둘 다 지금. 확인은 Blender 파이썬 콘솔에서 `bpy.context.preferences.addons['blender_mcp'].preferences.telemetry_consent` 값 캡처.
- 영향: D-19, OP-31에 '저장소 런처 외 방법(`uvx blender-mcp`·`mcp-for-blender`·pip 설치본)으로 Blender MCP를 띄우지 않는다' 1문장
- 상태: accepted (2026-09-18 사용자)
- 결정: 둘 다 — 홈 mcp_config blender 항목은 claude가 정렬(D-19). GUI Blender 5.2 애드온 Allow Telemetry는 2026-09-18 claude가 Blender 백그라운드 실행으로 껐다(userpref.blend 백업 `.bak-20260918`; 결과: 애드온 활성 상태, consent true→false, userpref 저장, 재실행 읽기 False 확인). (2026-09-18 사용자 결정, claude 적용)

### D-37 Serena 메모리 검토 가능화와 허용 외부 채널
- 질문: (1) `.serena/`는 `.gitignore`로 제외되어 Serena 메모리가 git diff로 검토되지 않는 유일한 교차 에이전트 메모리 채널이다(현재 비어 있음). `.gitignore`에 `!.serena/memories/` 예외를 둘까, 아니면 검사 스크립트로 비어 있음을 확인만 할까? (2) 장부 내용이 나갈 수 있는 외부 채널(Codex 전역 설정의 Notion MCP·openaiDeveloperDocs, Claude 전역 설정의 Notion 플러그인, Cursor 클라우드 인덱싱, 각 벤더 모델 API)을 "알고 수용"으로 기록할까?
- 권장: (1) 예외 규칙 추가 (2) 수용 목록으로 기록.
- 상태: accepted (2026-09-18 사용자)
- 결정: 지금 유지 — `.serena/` 제외 유지, 외부 채널 목록 미작성. (2026-09-18 사용자 결정, claude 적용)

### D-41 외곽 전투 공간을 벽 복도 던전 대신 이계 오픈 지역으로
- 질문: 메인 지역 외곽 입구 3곳의 목적지를 무엇으로 할지, 이계 지역 크기는?
- 선택지: 입구 (a) 3곳 모두 이계 관문 (b) 2곳 관문 + 1곳 던전 유지 (c) 던전 입구 유지 + 관문 2곳 추가 / 크기 504m·756m·1,008m
- 결정: (a) 이계 2곳 — 지하 세계풍 '가라앉은 별밭'(관문 2곳, 도착점 분리)·천공풍 '창백한 고원'(관문 1곳), 각 504m×504m. 던전 생성기 코드·아틀라스 슬롯은 지우지 않고 콘솔 진입으로 남김 — 2026-10-13 사용자 결정
- 영향: P3-13, P1-05(입구 액터), P2 던전 대장(외곽 연결 해제)

### D-42 이계 연결을 포탈에서 '숨은 이음매' 통로로
- 질문: P3-13 포탈형(관문 상자 → 암전 → 순간이동)을 걸어서 넘어가는 방식으로 바꿀 때 연결·배치·높이·필드 변경 방식은?
- 선택지: 연결 (a) 이계를 필드 가장자리에 물리적으로 붙임 (b) 숨은 이음매(이계는 먼 좌표 유지, 같은 모양 통로 중간에서 암전 없이 위치만 옮김) / 배치 (a) 별밭 남쪽(남서·남동 입구)·고원 북동(북동 입구) (b) 입구마다 이계 (c) 2곳만 / 높이 (a) 별밭 내려감·고원 올라감 (b) 같은 높이 / 필드 변경 (a) 부분 재베이크 (b) 전체 재생성
- 결정: 연결 (b), 배치 (a) — 남서·남동 입구 → 별밭(내려가는 협곡), 북동 입구 → 고원(올라가는 산길), 높이 (a), 필드 변경 (a)(지형·생성 산포만 교체, 던전 슬롯·PCG·상자·내비 설정 보존) — 2026-10-14 사용자 결정. D-41의 입구–이계 짝(북동→별밭, 남동→고원)을 바꾼다.
- 영향: P3-15, P3-13 결과물(포탈 관문 액터 제거), 메인 생성기·빌더

### D-43 탑다운 시각 정리: 프로젝트 전용 탑다운 재질 체계와 재베이크
- 질문: 고정 탑다운 카메라의 가림을 처리하려고 팩(DarkFantasyTopDown) 마스터 재질 5개에 디더 컷아웃을 넣을지, 배치 수정을 위해 메인·이계를 다시 구울지?
- 선택지: 재질 (a) 팩 마스터에 직접 삽입 (b) 프로젝트 전용 마스터를 만들고 그 자식 인스턴스로 교체 (c) 재질 손대지 않음 / 재베이크 (a) 진행 (b) 통로만
- 결정: 재질 (b)의 확장 — "프로젝트 전용 마스터 재질을 만들고, 마스터를 부모로 두고 개별적으로 다르게 하는 탑다운 재질의 기초 설계"(`Content/World/Materials/TopDown/`, MPC_TD·MF_TD·M_TD·MI_TD). 팩 원본은 고치지 않는다. 재베이크 (a) 진행 — 2026-10-14 사용자 결정
- 영향: P3-17, 메인 생성기·이계 생성기·통로 모듈·베이크, PCG 숲 재질

### D-44 엘든링식 동적 승강기 로딩과 걷는 통로의 동적 속도 조절
- 질문: 이계 연결 구간이 로딩 시간에 맞춰 늘어나지 않는다(고정 길이 + 이음매 뒤 대기). 엘든링 승강기처럼 로딩 동안 계속 움직이다 완료되면 멈추고 열리는 방식을 어디에, 어떤 모양으로 넣고 걷는 통로도 동적으로 만들지?
- 선택지: 적용 범위 (a) 별밭 남동 1곳 (b) 고원 북동 1곳 (c) 3곳 모두 (d) 승강기 없이 통로만 동적화 / 걷는 통로 (a) 동적(준비 늦으면 이음매 앞 감속·안개) (b) 그대로 / 형태 (a) 절벽 옆 열린 승강대 (b) 닫힌 승강 통 (c) 넓은 원형 갱도
- 결정: 적용 (a) 남동 입구 → 별밭 하강 승강기, 걷는 통로 (a) 남서·북동 동적 속도 조절, 형태 (a) 절벽 옆 열린 승강대(카메라 쪽 −X−Y가 트임) — 2026-10-14 사용자 결정. 숨은 이음매 기법(D-42)을 승강로에도 쓴다.
- 영향: P3-18, 통로 모듈·두 생성기·이계 베이크, C++ 이계 서브시스템·관문 정의, 이음매 테스트

### D-45 순간이동(숨은 이음매) 폐지, 이계를 필드에 실제로 붙인 연속 세계로
- 질문: 숨은 이음매의 순간이동이 '퍽' 하고 배경이 바뀌어 부자연스럽다(원인: 이계 지형 색 불일치, 100m 넘는 이동 시 렌더 기록 초기화·노출 튐, 도착지 캐시 비어 있음). 어느 방향으로 고칠지?
- 선택지: (a) 단계형 — 이음매 유지 + 렌더 기록 보존(OriginOffsetThisFrame)·색 중립화·예열, 미달 시 승격 (b) 연속 세계 — 이계를 필드 가장자리에 실제 지형으로 붙이고 월드 파티션이 이어서 불러옴, 순간이동 0회 (c) 닫힌 전이 키트 반복 / 승강기 대기 화면 (a) 바위벽 고리 반복(덱은 멈추고 벽이 계속 흐름, c→d→d→d→e) (b) 정지 대기 / 걷는 통로 안전망 (a) 감속은 드문 안전망으로만(발동 기록) (b) 감속 없이 보이지 않는 벽
- 결정: 방향 (b) 연속 세계, 승강기 (a) 고리 반복, 통로 (a) — 2026-10-15 사용자 결정. D-42의 연결 (b)(숨은 이음매)와 P3-15·P3-18의 이음매 부분을 대체한다. 데이터 레이어는 지역 구분·재빌드 격리·게임플레이 활성화에만 쓰고 대기(태양·안개)는 위치 기반 혼합을 유지한다.
- 배치(같은 날 사용자 결정): 별밭은 필드 남쪽 낮은 분지(504m, 중심 약 (−55,−756)m), 고원은 동쪽 높은 메사(중심 약 (756,452)m). 별밭 입구 두 곳은 남쪽 테두리로 이전(남서 협곡 시작 약 (−250,−440)m, 남동 승강기 축 약 (140,−478)m), 북동은 기존 협곡 끝에 바위 다리(약 94m). 중간 커밋은 하지 않음(기준점 b499d332).
- 근거: `Saved/AgentOps/20261015/transition/redesign.json`(튐 원인·데이터 레이어·반복 공간·참고 조사 + 설계 3안 + 심사)
- 영향: P3-19(신설), 두 생성기·통로 모듈·빌더 삭제 기준(레이어·태그), C++ 이계 서브시스템(이음매·카메라 컷 제거), 승강 덱(고리 반복), 시험

### D-46 태양 빛 진행 방향을 카메라 시선 반대로(역광)
- 질문: 필드 태양의 빛 진행 방향(yaw 38°)이 카메라 시선(yaw 45°)과 거의 같아, 컷아웃으로 비운 가림물의 그림자만 플레이어 위에 남는다(사용자: "그림자만 보이는데 부자연스럽다").
- 선택지: (a) 태양을 카메라 반대로 (b) 컷아웃을 그림자 패스에도 적용(그림자에 움직이는 구멍) (c) 측광
- 결정: (a) — 2026-10-16 사용자 요청. 규칙: 모든 지역 태양의 빛 진행 수평 방향은 카메라 시선 반대(yaw 225°) ±30° 안. 필드 (−70°, 205°), 별밭 (−70°, 200°) 유지, 고원 (−40°, 120°) → (−55°, 210°). 높은 고도는 역광에서 벽·절벽 그림자가 화면을 덮는 것을 줄인다.
- 근거: 기하 모델(폰 300cm 안 가림물 그림자 88.9% → (−70°, 200~210°)에서 약 20%, 모델 권고는 (−50°, 210°) 7%)과 PIE 다섯 후보 촬영(필드 15지점 평균 어두운 비율: 지금 0.218, (−55°,225°) 0.259, (−65°,225°) 0.212, (−65°,205°) 0.215, (−70°,205°) 0.193). 낮은 역광은 벽·절벽 그림자가 화면을 덮어 −70°를 골랐다. 혼합 회전: 필드→별밭 1.7°, 필드→고원 15.2°(32단계 중 한 단계 최대 0.47°), 빛은 항상 아래를 향한다. 기록 `Docs/Validation/P3-20-sun.md`
- 영향: P3-20, 필드 태양 외부 액터 1개·DA_TDRealm_PaleHeights, 원천 `editor_build_open_world.py` 태양 생성값·`generate_realm.py` 고원 대기, `05-topdown-visuals.md` 5.1·5.7

### D-47 에셋 로드 비동기 기본
- 질문: 프로젝트 에셋 로드가 동기인가 비동기인가? 사용자는 지금과 앞으로의 코드·에셋 작업 모두 비동기 로드를 기본으로 하고, 프로젝트 설정과 에이전트가 이를 알고 작업하길 원한다(2026-10-17).
- 선택지: (a) 엔진 기본값(쿠킹 빌드 비동기 로더)만 확인하고 종료 (b) 설정 명시 고정 + 런타임 동기 로드 전환 + 정적 검사 도구·개발 빌드 동기 로드 경고 + 규칙(`AGENTS.md` 7절·OP-33) (c) (b) + `UTDDamageDefinition` 표현 필드 소프트화·DA 재저장
- 결정: (b) — 2026-10-17 사용자 요청. 세부(claude 판정, 사용자 이견 시 이 항목 갱신): (c)는 별도 후속 작업(이번에는 하드 참조 타입 불변), 개발 빌드 동기 로드 경고는 축소판으로 넣음, MegaMagic 절차 생성 대체는 에디터 전용으로 남김, `/Game/MonsterAI/Attacks` 후보 경로는 존재 확인 후 유지, 기존 시전 길이의 게임·시뮬 차이는 보고만 함(대장 변경 없음). 동기 로드 허용은 에디터 모듈, `WITH_EDITOR`·`WITH_EDITORONLY_DATA` 블록, `Tests` 폴더 파일의 자동화 시험 블록, 에디터 Python, `Tools/check_sync_loads.py` 기준 목록(생성자 기본값 2, 저작 공장 1, 결정론 관문 대기 2)뿐이며 기준 목록은 줄이기만 한다. 기준 목록 추가는 사용자 승인 뒤 이 항목에 근거를 덧붙인다.
- 상태: accepted (2026-10-17, 방향은 사용자 요청·세부는 claude 판정)
- 근거: `Saved/AgentOps/20261017/async-load/spec-final.md`(조사 보고 8개·반박 3개 반영)
- 영향: `AGENTS.md` 7절, OP-33, `Tools/check_sync_loads.py`, `Config/DefaultEngine.ini`·`DefaultGame.ini`, 캐릭터·전투 GAS·몬스터 AI·이계 스트리밍·월드 생성 런타임 코드와 시험, 개발 빌드 동기 로드 경고 서브시스템
- 후속(2026-10-17 claude): (c) 완료 — `UTDDamageDefinition`의 `VisualEffect`·`Mesh`·`Material`을 소프트 참조로 바꾸고 엔티티(패턴 A)·플레이어·몬스터 정의 라이브러리(패턴 B)에서 비동기로 요청, 꼬리 수명은 경로 기준. MegaMagic DA 21개 재저장(사용자 승인, `Tools/editor_resave_assets.py`). 기준 목록에서 저작 공장 1을 지워 4개(생성자 기본값 2, 결정론 관문 대기 2). 근거 `Saved/AgentOps/20261017/soft-damage/`
- 후속(2026-10-18 claude): 쿠킹 보장 — 코드 안 경로로만 여는 마네킹 `Anims/Unarmed`·`Anims/Sword` 폴더를 `DefaultGame.ini` 항상 쿠킹에 추가(쿠킹 게임 SkipPackage 0, 근접 공격 재생 확인), 런타임 C++의 `/Game` 경로 쿠킹 근거 검사 `Tools/check_cook_coverage.py`(종료 0, 근거 없는 경로 0). 제안(규칙 변경이라 사용자 승인 필요): `AGENTS.md` 7절과 OP-33의 판정 명령에 `python Tools/check_cook_coverage.py`를 `check_sync_loads.py`와 함께 넣는다. 근거 `Saved/AgentOps/20261018/cook-path-audit/`
- 후속(2026-10-18 사용자 승인, claude 반영): `AGENTS.md` 7절과 OP-33의 판정 명령에 `python Tools/check_cook_coverage.py`를 `check_sync_loads.py`와 함께 넣었다. OP-33에는 코드 안 `/Game` 경로의 쿠킹 보장 문장(L-build-07)을 더했다.

### D-48 FluidFX(FluidNinja LIVE-2 역공학) 연산 백엔드·채널·전투 연동
- 질문: 설계서 `Docs/TDFluidLiveSystemDesign.md` 4절 방식 A(C++ 솔버 + 동적 UTexture2D)·B(RDG 컴퓨트 셰이더) 중 무엇인가? 3.1절 브러시 채널 4개와 3.4절 출력 밀도 2개(R 주·B 선택)가 어긋난다. 전투 피격은 어느 경로로 받나?
- 선택지: 백엔드 (a) A (b) B. 채널 (a) 2개 유지(Mud·Water→주, Blood·Fire→선택) (b) 4채널로 확장. 전투 (a) 델리게이트만 (b) 메시지만 (c) 둘 다 받아 대상·프레임당 1회로 합침
- 결정: 백엔드 A(사용자 승인 설계서의 권장안, 2026-10-18 구현), 채널 (a)(3.4절 출력 그대로, 확장은 요청 시), 전투 (c)(확정 원소·피격 위치 우선, 상태이상 펄스 제외) — claude 판정, 사용자 이견 시 이 항목 갱신
- 상태: accepted (2026-10-18, 백엔드는 사용자 승인 설계서·세부는 claude 판정)
- 근거: `Saved/AgentOps/20261018/fluidfx/analysis/critic.md`, 헤드리스 `TDGame.FluidFX.*`(`Saved/AgentOps/20261018/fluidfx-final-*-tests.json`)
- 영향: `Source/TDGame/FluidFX/`, `Tools/FluidFX/`, `Content/FluidFX/`, 설계서 4·7절

### D-49 FluidFX v2: FluidNinja LIVE-2 동등 화면을 목표로 렌더러 우선 → 자체 GPU 솔버
- 질문: 사용자가 결과(바닥 평면 반투명)를 LIVE-2와 다르다고 거부하고 '완전 동일 수준'을 요구했다(2026-10-18). LIVE-2는 에픽 NiagaraFluids 기반이 아니라 자체 나이아가라 시뮬레이션 단계(GPU) + 블루프린트 래퍼 프로젝트다.
- 선택지: 방식 (a) 공개 원리로 자체 재구현 (b) 원본 구매·도입. 백엔드 (a) 렌더러 먼저, 이후 자체 C++ RDG 컴퓨트 GPU 솔버 (b) CPU만 (c) NiagaraFluids 그래프. 7절 해석 (a) 렌더링 HLSL·나이아가라 출력 전용 허용 (b) HLSL만
- 결정: 방식 (a), 백엔드 (a), 7절 해석 (a), 첫 장면 '폰이 가르는 볼륨 연기', 단계마다 PIE 화면을 사용자에게 보여 육안 승인. 사용자: "앞으로 물어보지 말고 당신이 권장사항으로 다 진행합니다. 목표는 Fluid Ninja LIVE 2.0과 동일할 정도"
- 상태: accepted (2026-10-18 사용자, 세부는 claude 판정). D-48 백엔드 A는 기준 구현·헤드리스 시험·대체 경로로 유지
- 7절 해석 세부: 시뮬레이션·입력 판단은 C++·컴퓨트 셰이더, 재질 Custom 노드·.usf HLSL은 렌더링 전용, 나이아가라 에셋은 텍스처 샘플·색·이동 출력 전용(솔버 금지). AGENTS.md 본문 변경은 하지 않음(용량 한도)
- 저작권: 원본 셰이더·텍스처·에셋·튜닝값을 복제하지 않는다. 원본 데모·홍보 화면은 공개 저장소(Docs/)에 두지 않고 Saved/에서만 비교한다
- 근거: Saved/AgentOps/20261018/fluidfx/live2-parity/(조사 3·설계·반박 3), 설계서 8절

### D-52 FluidFX: LIVE-2 전 영역(물·불·연기·안개·구름·모래·눈·식생·점성·입력·운용) 품질·기능 동등, 성능 동일 이상
- 질문: 사용자 "물이 아닌 불 안개 구름 기타 등등 Fluid Ninja LIVE 2.0과 동일한 품질과 기능, 성능도 동일, 더 좋으면 좋다"(2026-10-19). D-49 단계(FX-1~6·FX-W)는 연기·불·폭발·물만 덮는다.
- 결정: 대조표 213행(있음 18·부분 84·없음 83·다름 7·해당 없음 21)과 단계 로드맵을 채택(설계서 9절). 순서 = 측정 기반(FX-7a·7d·7p) → W-1 마무리 → FX-4a GPU 코어 솔버(프레임당 그래프 1개·텍스처 배열 일괄, 512² 스텝 ≤0.45ms) → 불·연기·물 외형 → GPU 물 계층 → 엔진 볼륨 구동(FX-9) → 안개·대량 입력·모래·눈·자국·식생·구름·점성 → 운용·최종 동등 감사(FX-P). 렌더 부분은 병렬, 코드분은 한 사슬로 직렬. FX-6 은 다른 단계로 흡수. 합격 지표 1차는 GPU 병목 장면의 효과 차분 프레임 시간(중앙값·p95), 시뮬 비용은 스텝당 비교
- 상태: accepted (2026-10-19, D-49 "권장안으로 진행" 지시로 claude 판정). 제안 P-1~P-36 중 사용자 결정 필요는 P-4(LIVE-2 보유·같은 기계 측정 = '동일 이상'의 유일한 증명 경로), 측정 뒤 P-27 안티에일리어싱 전역값·P-29 볼류메트릭 포그 격자, 승인 필요 P-28(FluidFX 대장·AGENTS 행·Lessons 코드). 그 밖은 권장안
- 후속: D-50·D-51 은 다른 명세가 번호를 예약해 둔 제안이다(FX-3 명세 '나이아가라는 FX-6' → FX-6 흡수로 개정 대상, W-1 명세 지형 높이 출처 → P-10 권장 (a) 월드 생성 높이맵 굽기). P-24 Aux 세 번째 출력은 D-48 채널 결정의 개정 대상
- 저작권: D-49 그대로. 대응표 `Docs/FluidFX/live2_coverage.json` 에는 ID·짧은 이름만, LIVE-2 수치는 프리셋에 이식하지 않음(P-26·P-35)
- 근거: `Saved/AgentOps/20261019/fluidfx/parity2/`(조사 7·로드맵·반박 3·최종본 roadmap-final.md), 사용자 제공 고해상도 이미지 24장(커밋 금지)
- 재확인(2026-10-22): 게임플레이 분석의 GQ-26(D-52 잔여 보류·유체 작업을 게임 연결 묶음으로 전환)에 사용자 "GQ-26은 아니오입니다. LIVE-2 동등 로드맵(D-52)을 계속 진행해 주세요." → D-52 그대로 진행.

### D-53 FX-W W-1 구현 편차(명세 `Saved/AgentOps/20261018/fluidfx/water/final-spec.md` 대비)
- 편차: ① 잔물결 이류 clamp 를 2×2 최소·최대 대신 4×4 탭 범위 + 범위폭 25% 여유(`TDFluidGrid2DSolver.cpp` AdvectRipples, 진폭 시험 0.547→0.806) ② 시선 앞당김을 GetDynamicCenterOffsetPlane 대신 ResolvePlaneAnchorWorldLocation 재정의(월드 평면 경로가 오프셋을 더하지 않음) ③ ATDFluidLiveActor 내부 수면 그림자 끔이 모든 월드 평면 라이브 액터에 적용(단일층 물이 바닥에 그림자, L-editor-40) ④ ATDFluidWaterActor 는 내부 평면을 항상 숨기고 태그 메시만 물을 그림 ⑤ 꺼진 경로 부수 변경(경사 계산의 마른 셀 갱신 표시, 비유한 잔물결 도장 거부, 꺼진 상태 안내선·수역 보관) — 체크섬·골든 해시 불변 ⑥ 강 프리셋 WhitewaterFromVorticity 0.15→0.3(PIE 탐침 바위 하류 흰 물보라 0.16~0.19 < 수용 0.3, 2026-10-19). 결함 수정(편차 아님): 액터 브러시가 ActorBrushRippleImpulseCm 를 스탬프에 옮기지 않아 PIE 잔물결 0 → `ApplyActorBrushSettings` 한 줄, 탐침 결과 해석·거품 측정 시점·잡음 문턱(95백분위)
- 상태: proposed (2026-10-19 claude, 사용자 지시 "권장안으로 진행"). ①은 단조 계단 필드 장기 이류에서 새 극값이 없는지 시험을 더한 뒤 확정, 남은 낮은 위험(지형 표본 끔이면 물 계층 알파 0 고정, 안내선 전체 상자 판정, 스트리밍마다 재수집)은 W-2 전 처리 후보
- 근거: `Saved/AgentOps/20261019/fluidfx/fxw/b1-report.md`, 마무리 워크플로 wf_b88994dc-b74(A1 정적 반박·B1 반박), 시험 `Saved/AgentOps/20261019/fxw-w1-b1-*-tests.json`
- 확정(2026-10-23): ① 기준을 '새 극값 없음'에서 '넘침 유계(범위폭 25% 여유 안)·주기별 넘침 비증가'로 바꿔 확정. 4×4 범위 + 25% 여유는 Catmull-Rom 넘침보다 넓어 clamp가 실제로 걸리지 않으며(동작 = clamp 없는 CR), 원 명세의 진폭 기준 0.79도 clamp 없는 값이라 명세 내부 모순이었다. 여유 0(엄격)은 진폭 시험 0.57로 실패. 시험 `TDGame.FluidFX.Water.Ripple.StepAdvectionOvershootStaysBounded`(720스텝 넘침 0.206→0.145→0.120cm, 성장 변이 1.02배로 실패 확인, 4565a39f). W-1 성능 관문 P1~P6 통과(`Docs/Validation/FluidFX/D-49-fxw1-perf-2026-10-23.md`, 창 이동 p95/평균 1.43~1.48: 창 원점 이동·장애물 SDF·기울기 병렬화, 지형 트레이스 비동기, InteractionVolume WorldStatic 제외 — 7b173813). 남은 W-1 관문: 사용자 화면 승인(`Saved/FluidFX/showcase/w1-river-20261023-021209/`). 상태: proposed (2026-10-23 claude, 권장안으로 진행)

### D-54 FX-4a GPU 코어 솔버(TDFluidGpu) 설계 선택
- 결정(권장안): ① 해상도 등급 = 2의 거듭제곱 올림(192 프리셋은 GPU 에서 256², 출력 RT 는 등급 크기여야 하고 아니면 건너뜀) ② 백엔드 Auto = 요청 해상도 >256 일 때만 GPU(임시 문턱, FX-7p 실측 뒤 조정), CVAR `TD.FluidFX.SolverBackend` ③ 지형 미지 칸 = -1e30 표식, 경사는 이웃이 미지면 한쪽 차분 ④ 배치 키 = (등급, 압력 반복 수, 모드), 키가 바뀌면 상태 리셋 ⑤ 패스 순서는 CPU 와 같게 이동·리셋 → 장애물 → 지형 → 스탬프 → 힘 …(명세 4.7 표의 스탬프·장애물 순서 정정) ⑥ 속도 상한 고정은 다음 프레임 스탬프 패스로 이동(이류가 CPU 처럼 고정 전 속도를 씀) ⑦ 셰이더 클래스 13종(지형 갱신 추가), 512 이상 등급은 정확한 슬라이스 용량 ⑧ 단순 페인터 장애물은 해석식(텍스처 없음), 페인터 표면 변위는 0.5×주 밀도 근사(지우기 스탬프 칸은 CPU 와 다름, B8 패리티에서 판정) ⑨ 스탬프 상한 버림은 안정 정렬(CPU 의 불안정 정렬과 동률·경계 사례에서 비트 차이, 통계 허용폭 안) ⑩ RHI 시험은 NonNullRHI 플래그(L-build-11)
- 묶음 B 편차(사본 `Saved/AgentOps/20261019/fluidfx/fx4a/staging/bundle-b-contract.md` 4절): ⑪ 풀 반환 출력 지우기는 RDG 지우기 패스 대신 RT UpdateResourceImmediate ⑫ 지형 표본이 끝날 때까지 GPU 전송 지연(CPU 의미 유지) ⑬ 압력 반복 수 변경 = GPU 상태 리셋 ⑭ 풀 반환 때 슬라이스 해제(재사용 시 새로 받음) ⑮ 세대 번호 = CPU·GPU 세대 합 ⑯ GPU 처리 점 수 상한 16000(CPU 512) ⑰ 16·32 등급 리드백은 칸 복제 ⑱ B6 발산 판정은 시뮬 해상도 같은 인코딩 속도로(64² 상자 평균은 정보용), 비용 곡선·다중 격자 측정은 후속 ⑲ 게터 반환형 UTexture2D* → UTexture*(C++ 호출처 1곳 수정, 블루프린트 에셋 참조 0건 확인)
- 다중 격자(B6 512² Jacobi 미수렴 대응, `Saved/AgentOps/20261019/fluidfx/fx4a/multigrid/`): MG-1 자동 다중 격자 문턱 512(256 이하는 CPU 패리티 계약대로 Jacobi 비트 동일) · MG-4 계층은 최하층 32²까지(512는 5단, 최하층 연결 성분별 rhs 평균 빼기·액터별 ω_opt 적흑 SOR 48회) · MG-5 닫힌 주머니 상수 표류는 MG 웜 스타트 압력 프레임당 (1−1/32) 감쇠(CVAR TD.FluidFX.GpuPressureWarmStartDecayFrames, 3600프레임 시험 유계) · MG-6 예산: RTX 2060 실측 512² 스텝 중앙값 0.767ms·p95 1.0ms(V(2,2) 1사이클, B6 비 0.90) > 제안 예산 0.45ms → 최적화 워크플로로 예산 충족 조합을 찾고, 못 찾으면 실측 기반으로 예산을 고친다. 현재 프리셋은 모두 256 이하라 게임 영향 없음. 최적화 결과(`multigrid/perf-optimize.md`): 영역 적재·소유 칸 레지스터화, V(2,2)→V(1,2), 최하층 레지스터·라벨 캐시·SOR 횟수 ω 기반(등방 34회), 상승 288스레드, 평균 타일 합 접기 → 512² 1액터 중앙값 0.768→0.475ms(예산 0.45 미달 0.025), 4액터 2.471→1.468ms(예산 1.6 충족), B6 0.908. p95 는 창 이동 프레임(+0.10)·외부 GPU 선점 돌출이 정해 정상 프레임 p95 = 중앙값+0.02 → p95 는 이동 프레임을 따로 재는 정의로 바꾸고, 남은 0.025ms 는 FX-16(최하층 단일 디스패치·다중 격자 전용 패스 합치기·이동 복사 접기)에서 처리
- 상태: proposed (2026-10-19 claude, "권장안으로 진행"). 묶음 A 통합 완료(TDFluidGpu 등록, 헤드리스 168, 오프스크린 A4~A8 5/5, 에디터 시작 정상). 묶음 B 통합 완료(2026-10-19, 오프스크린 GPU 18/18·헤드리스 173/173·CPU 골든 비트 동일)
- 근거: `Saved/AgentOps/20261019/fluidfx/fx4a/core-contract.md`, `Saved/AgentOps/20261019/fluidfx/parity2/roadmap-final.md` 4절, 워크플로 wf_49547c3c-a57

### D-55 FX-8b 불·연기 열 채널·투과율·Aux 출력(D-48 채널 개정 포함)
- 결정(권장안): ① D-48 채널 개정(P-24 채택): 출력 2장(밀도·압력 / 속도) 유지 + 선택적 Aux RGBA16F(R 열·G 장애물 거리 예약·BA 휨 예약), 기본 꺼짐, 켰을 때 칸당 +8B, 꺼지면 골든 해시 불변. 선택 밀도(B)는 Blood·Fire 사상 그대로 재사용하지 않는다 ② 폭발 P0 원인(온도를 밀도에서 뽑아 밝기 ∝ 밀도^2.1, 지속 버스트가 밀도를 줄임) → 독립 열 채널(이류·냉각·부력), 발광은 열 기반 ③ 투과율은 RGBA16F 높이 4단에 광학 깊이 τ 저장(색 흡수·3옥타브), 대칭 볼륨은 부호 있는 높이로 상자 전체 두께 ④ D-54 ④ 배치 키에 열 사용 여부 추가, GPU 액터 매개변수 240→272바이트(Padding0 자리를 열이 씀) ⑤ CPU·GPU 패리티는 비트 동일이 아니라 통계 패리티(열은 Aux RT FP16 비교) ⑥ 경계 반사 v_n−(1+b)max(v_n,0)를 속도 이류 직후 한 곳에서, 소용돌이 반지름 항은 반지름 밀기 경로의 표류로 ⑦ CpuFallbackResolution 추가, 멈춘 조각 스탬프에 밀도 동반
- 구현 편차(명세 `Saved/AgentOps/20261019/fluidfx/fx8b/spec.md` 14.6): E1 폭발 멈춘 조각 열 8초·1.0·추적 8초(명세 5초·0.55로는 t=5초 발광 면적 단언 불가) E2 RHI 시험 7개는 묶음 B 하네스 파일 끝에, Heat.DisabledKeepsGpuOutputs 는 별도 파일(통합 전 기준선 기록용) E3 Fire.ExtraCardsShareSimulation → ExtraCardCountFromPreset(헤드리스 불가), Transmittance.PassBudget 미구현 E4 HotCoreAreaTimeline 면적은 버스트 중심 600×600cm, 문턱 0.05·0.30·0.01 잠정 E5 GPU 경로 광원 상태가 한 프레임 늦을 수 있음 E6 VolumeBoxScale 은 평면 xy만 E7 장작 재질 경로 미반영 E8 proto_heat.py 생략
- 반박 남은 일(medium): XY VolumePositionOffsetCm 이 상자만 옮기고 밀도 UV는 그대로(기본 0 무영향) · Heat.GoldenChecksums 기대값 0u → 통합 첫 빌드 2회 일치 값으로 채움 · GPU 기준선은 통합 전 기록 필수 · 예산 측정 경로(PassBudget·열 켬 128² 스텝·조각 추적 8초 게임 스레드) 미구현
- 상태: proposed (2026-10-20 claude, "권장안으로 진행"). 사본 정적 검사(cl /Zs 43단위 0건·GPU 셰이더 66·불 HLSL 21)·반박 pass_with_notes, 통합 진행 중
- 근거: `Saved/AgentOps/20261019/fluidfx/fx8b/`(spec.md·integration.md), 워크플로 wf_c1c5ea04-7cb
### D-56 FX-8c 불·폭발·연기 외형 1차(LIVE-2 수치 목표 기반)
- 근거: 진단 4갈래 `Saved/AgentOps/20261020/fx8c/`(live2-targets: LIVE-2 불 화소 짙은 빨강 38~57% 대 우리 0%, 가장자리 6~15배 날카로움, 배경 휘도 15~51 대 0~4 / flame-shading: 흑체 휘도 정규화 + (T/Tmax)^1.5 바닥 밝기로 경사에 빨강 없음, 흐름맵 τ 최대 0.8초 접힘 76~97%가 대리석 결 / explosion-shape: 0.3초 크기는 버스트 발자국 지름 510cm, 창 아래 130cm 바닥 밑·조각 꼬리·닫힌 경계 고임 / smoke-volume: 붉은 직사각형 = 불꽃 카드 발광 바닥값, 연기는 SLAB·판 중앙 광원·앰비언트 0.05), 워크플로 wf_b37ceec0-e86
- 결정(권장안, PIE 런타임 덮어쓰기로 먼저 확인): ① 불꽃 MI 650~1750K·밝기 지수 4·열 밀도 지수 1·흐름 속도 0.25, 세기 횃불 5.5·모닥불 20·폭발 8 ② 셰이더 발광 바닥값 제거 pow(max(d−1e−4,0),e) ③ 폭발 버스트 핵 140·고리 90×24·지속 0.2·중심 +160, 열 냉각 0.7(밀도 소산 0.9와 구분)·열 부력 2000·흐름 잡음 300/120 ④ 모닥불 소용돌이 1.5·열 냉각 0.6·부력 1600·잡음 300 ⑤ 무대 BREAK_FORCE 650·폭발 불 창 중심 0.45·땅먼지 높이 200·연기 버스트 배율 2.5(기본 대상 1.5는 유지)·연기 재질 새 SOFT 인스턴스 SmokeBillow(앰비언트 0.2·표면 부드러움 24·침식 0.3) ⑥ 시험: 바닥 밑 열 피치 0°·−60° ≤0.02, 0.3초 발광 폭 ≤ 창의 0.5, 2.5초까지 테두리 5% 띠 발광 <0.05, 폭발 열 골든 재기준(0x2FE1B4F5/0x1A9D5122, 2회 일치)
- 결과: 헤드리스 202/202·오프스크린 25/25, 폭발 0.3초 폭 0.34·바닥 밑 0(이전 0.34~0.42)·1.2초 연기 0.48. PIE 측면: 폭발이 벽에서 솟는 불기둥으로, 모닥불 불혀·연기 기둥 생김. 남음: 횃불 위치·색, 땅먼지 평평한 층, 불 꺼진 뒤 연기 어둠, 연기 가장자리 점묘, 배경 어둠 → 2차
- 2~4차(같은 날): 측정·비평·완결성 워크플로로 촬영 시각 오류(L-editor-46)·G1(카메라 1° 회전마다 카메라 향 시뮬 소거, 게임 탑다운 카메라는 고정이라 잠복)을 찾았다. ⑦ 셰이더 매개변수 10개(기본값 비트 보존) + 디더 프레임 View.StateFrameIndexMod8 ⑧ 무대 높이 안개(볼류메트릭 끔)·달 3 lux(연기 휘도·배경 p20 LIVE-2 범위 진입) ⑨ G1 (d′) 피벗 보존 재정렬 통합(평면 좌표 오프셋 k, 헤드리스 평면 시험·RHI 26/26) ⑩ 흑체 3파장으로는 짙은 빨강(H<12°)·M1≤0.38 불가 → 해석식 4정점 색 램프(기본 끔) 채택 진행. 폭발 도넛 = 대칭 고리 0.2초 재주입, 추가 카드 CardScaleStep 0.92 사각 가장자리 버그. 근거 `Saved/AgentOps/20261020/fx8c/round2~4/`
- 5~6차 굽기(같은 날): ⑪ 불꽃 색 램프 모드 1(폭발·횃불 차가운 정점 (1,0.03,0.03), 모닥불 마스터 정점) ⑫ 폭발 트레일 고리 스탬프 0·핵 200(PlasmaExplosion도 같은 분기), 연기·먼지 버스트 배율 1.75·1.12(반지름 350·224cm 유지), ExplosionSmoke 열 발광색 (1,0.06,0.01)·표면 열 배율 1 ⑬ 추가 불꽃 카드 축소 0.92→1.0(사각 가장자리 버그) ⑭ 모닥불 깊이 보정 40→10, 땅먼지 SLAB 전용 MI·z=0, 횃불 막대 실측 축·불씨 a·50, 횃불 열 게이트·카드 연기 0 ⑮ 기각: 폭발 부력 900(C4), 냉각 0.45(C1), 강도 16~20(C2), Tmin 900K(C3), 흑체 Tmin 하향 격자(X9) ⑯ 굽기 회귀 관문 교체 제안: 폭발 glow/flame ≤1.35 → 번짐 화소 3회 중앙 ≤47.1k(비율 ≤1.15는 번짐·부엽 과제 기준, L-editor-48). 근거 `Saved/AgentOps/20261020/fx8c/round5~6/`
- 7~9차 굽기(같은 날): ⑰ 폭발 연기 전용 MI_TD_FluidVolume_ExplosionBillow(soft-abs ε 0.04·평균 보정 0.08456, 잉크 윤곽 −30~−57%) ⑱ 폭발 트레일 핵 224·부엽 0, 연기·먼지 버스트 배율 1.5625·1.0(반지름 350·224cm 유지). 비대칭 부엽 기능(FTDFluidFragmentTrailSettings.BurstLobe*, 기본 0)은 PIE에서 부엽 6·3엽 모두 '발바닥·위성 공'으로 LIVE-2에서 멀어져 프리셋에서 쓰지 않음 — 코드 유지 여부는 사용자 결정 대기(U2, 권장: 이번은 유지) ⑲ ExplosionSmoke 열 발광 지수 3(번짐/불 면적 1.30→0.78) ⑳ 폭발 불빛 색온도 1100/1400K(번짐 짙은 빨강 0.92~0.93, 대가: M1 0.20 안팎으로 LIVE-2 최솟값 0.228 아래 — 측정 도구 범위 0.178 기준 채택, 사용자 결정 대기 U1) ㉑ 관문 개정 제안 P1~P4(p95 0.3초 같은 세션 대조 −2.5 이내, 안개 잉크 6회 중앙, LIVE-2 유래 관문은 도구 범위로 통일). 근거 `Saved/AgentOps/20261020/fx8c/round7~9/`
- 10차 굽기(2026-10-21): ㉒ 폭발 조각 발광 — FTDFluidFragmentGlowSettings(세기 4, 차가운 (1,0.012,0.03)·뜨거운 (1,0.03,0.035) 진홍, C++가 월드 시간 파괴 나이로 열 비율·색·세기를 MID에 넣음, 불 열 필드 등록), 렌더 전용 TDFluidFragmentGlow.hlsl·M_TD_FluidFragmentGlow·MI_TD_FluidFragmentGlow_Explosion(기본 재질 미지정이면 옛 동작) ㉓ 폭발 MI 침식 0.35·FlameRampHotPosition 0.66 ㉔ 횃불·모닥불 불빛 색온도 1100/1400K(세 불 모두 같은 띠) ㉕ 보류: 횃불 정점 변경(분홍은 휘도 130 문턱 근처 잡음과 안개 합성, 지표·관문부터 수정), 조각 꼬리 반지름 30(효과 없음). 근거 `Saved/AgentOps/20261020/fx8c/round10/`
- 11차(2026-10-21): ㉖ 기본값 0 실험 설정 — 불티 EmberTintBlend·Green·Blue, 조각 FacingContrast·KeyCenter(EdgeChar* 는 C++가 넣음), ATDBreakableActor BreakDirectionOffsetCm, 불티 BurstUpwardRatio·EmberBurstHeightOffsetCm, 불꽃 FlameColdRidgeContrast(헤드리스 226/226, G1 골든 해시 3실행 일치, dxc 0 접기 = HEAD) ㉗ 굽기: 모닥불 불티 base_scale 0.025, 횃불 0.010, MI_TD_FluidEmber_Default EmberIntensity 4.5·EmberTintBlend 1(수용 PIE 같은 세션 R0 대비 밝은 불씨 색상 25→3°, 큰 덩어리 0) ㉘ measure_fx_look 분홍 지표 2개·kind_metrics(횃불 ≤0.076·폭발 ≤0.042). 횃불 안개 pink_hi 0.093은 P8 위라 지표로 닫지 않고 격차로 둠 ㉙ 불합격 유지: 조각 면 음영(CV 0.124 < 0.15), 파괴 힘 1000(코어 노랑 붕괴), 늦은 결(미세 대비 +0.011 < +0.015), 불티 위로(바닥 뭉침). 근거 `Saved/AgentOps/20261021/fx8c/round11/judge11a·b·c.md`
- 12차(2026-10-21): ㉚ 0.1초 코어 붕괴 원인은 버스트 코어와 트레일이 FireHeatStrength·FireDensityStrength를 공유하는 것(파괴 힘·시계·Field 아님, AP2 분리 팔 0.257≈K 0.260) ㉛ 기본값 불변 설정: FTDFluidFragmentTrailSettings FireTrailHeatScale·FireTrailDensityScale(1), bShouldTrackWithWorldClock(false), DriveNiagaraComponent 평면 기저·오프셋·속도 기준 User 변수 6개 ㉜ 굽기(동작 변경, 권장안 적용): 폭발 프리셋 트레일 BurstSustainSeconds 0.2→0.14 + bShouldTrackWithWorldClock true(WC14, BK 12/12·수용 nf 22/23, 화면 동일, 실행 간 흩어짐은 줄지 않음) ㉝ 불수용: 트레일 열 0.4·0.7, 트레일 밀도 0.4, 파괴 힘 1000(0.1~0.3초 노랑·불 면적). 꼬리 국소 냉각(19파일 사본 `round12/stage/cooling`)과 보로노이 파쇄는 보류. 근거 `Saved/AgentOps/20261021/round12/judge-r12·judge-bk·judge-acc12.md`
- 13차(2026-10-21): ㉞ 기본값 불변 설정 FireTrailStemHeatScale·FireTrailStemDensityScale·FireTrailHeadRadiusScale(1, 버스트 유지 종료 뒤 불 트레일에만: 머리 i==N 반지름×h, 줄기 i<N 열·밀도×배율)과 BlueprintPure 계수기 3개(유지 뒤 불 트레일·줄기·정지 도장 수) ㉟ 굽기 없음: ST 29회에서 유지 뒤 줄기 도장 0(조각 속도 600cm/s 미만)이라 줄기 배율 무작용, 머리 h<1은 0.8초 진홍↑·노랑↓이나 luma 대비 손실·pink로 불수용. 트레일 열 전체 배율은 0.85~0.7에서 0.1초 코어가 꺾임(PB 9회) ㊱ K 머리 몫 0.100 = LIVE-2 17-3 0.0996이라 남은 꼬리 격차는 몸통 주황(0.39 대 0.17). 다음은 정지 도장 RestingHeatStrength/Seconds 런타임 팔. 근거 `Saved/AgentOps/20261021/round13/judge-st.md`
- 14차(2026-10-22): ㊲ 굽기 없음 — 정지 도장 RestingHeatStrength 0.5·0.25·0, RestingHeatSeconds 2, 머리 0.5 결합(RS 23회). 양성 대조 RH0(정지 도장 0)에서도 0.5·0.8초 꼬리 지표 big 0이라 사전 등록대로 세션 무효, 정지 열은 꼬리 몸통 주황의 출처가 아님(13차 추정 반증). K 0.8초 주황 0.408은 LIVE-2 17-2(0.399)와 같고 17-3(0.174)과 다름. 근거 `Saved/AgentOps/20261022/round14/judge-rs.md`
- 15차(2026-10-22): ㊳ 굽기 없음 — 주황 출처 진단 OR 20회(트레일 열 0·0.5, 버스트 유지 0, 불 열 세기 0.5). 0.8초 주황 big 감소 팔 0, 트레일 열 0에서도 주황 화소 Δ −3이라 트레일 열·버스트 유지·코어 세기는 꼬리 몸통 주황의 출처가 아님. 트레일 열은 노랑→진홍 레버(진홍 화소 +4.8k)지만 luma·0.2초 코어·pink 보호 실패. LIVE-2 17-3과의 격차는 진홍 리본 면적(12.3k 대 35.6k 화소)[추정] → 조각 비행 우선. 근거 `Saved/AgentOps/20261022/round15/judge-or.md`
- 16차(2026-10-22): ㊴ 굽기 없음 — 조각 비행 세션 FL 20회(파괴 힘 1000 단독·+줄기 열 0.3·+유지 0.07·+트레일 열 0.5). 파괴 힘 1000이면 유지 뒤 줄기 도장이 생김(K 0 → 878~1052). BF1000+트레일 열 0.5가 0.8초 진홍 화소 35.8k로 LIVE-2 17-3(35.6k) 규모에 도달했으나 머리 도장(유지 뒤 약 85%)에도 트레일 배율이 걸려 머리 몫 .003·luma 1.76, mx8 1.17~1.34·0.2초 노랑·pink 보호 실패. 다음은 중간 배율·머리 반경 조합, 실패 시 머리 열을 트레일 배율에서 떼는 C++ 레버. 근거 `Saved/AgentOps/20261022/round16/judge-fl.md`
- 17차(2026-10-22): ㊵ 굽기 없음 — 머리·리본 분리 HR 20회(파괴 힘 1000 + 트레일 열 0.7·머리 반경 0.5·조합 0.7+0.5·0.85+0.5). 최근접 T085H05(0.8초 진홍 화소 26.6k, 머리 몫 .104, luma 2.41)도 주황 감소·luma·mx8·pink 보호 실패. 남은 1위 격차는 리본 몸통 과밝음(중앙 휘도 63~85 대 LIVE-2 17-3 34). 머리 열만 떼는 레버는 기존 트레일·줄기 배율 조합으로 이미 표현돼 만들지 않음. 다음 후보는 지연 냉각(spec12b A2-3)과 대리 냉각 세션. 사용자 지시로 18차 전 일시 정지. 근거 `Saved/AgentOps/20261022/round17/judge-hr.md`
- 18차(2026-10-22): ㊶ 굽기 없음 — 냉각 대리 CP 17회(기반 BF1000T085H05 + HeatCoolingPerSec 3·4, 무대 0.7). 0.8초 리본 p50만 big 감소하고 p95·머리·0.3초 코어 노랑이 함께 꺼져 전역 냉각률 JSON 굽기 기각, 꼬리 국소 냉각 단독 통합 보류. 새 원인: 저열 진홍 띠 휘도가 LIVE-2 17-3의 1.8배(54~60 대 30), 진홍/노랑 휘도비 .30~.33 대 .14~.19 → 다음은 불꽃 열→색·휘도 곡선. FX-8c 종결 기준(proposed): 0.8초 luma ≥2.49·ring ≥1.5·진홍/노랑 ≤.20·머리 몫 .07~.15·진홍 절대 ≥25k·코어 보호 통과면 종결, 결합 레버가 두 번 연속 진전 없으면 남은 격차를 구조 문제로 문서화하고 종결. 근거 `Saved/AgentOps/20261022/round18/judge-cp.md`
- 19차(2026-10-22): ㊷ 굽기 없음 — 저열 곡선·열 문 MID 세션 LC 20회(기반 BF1000T085H05 + FlameBrightnessExponent 6·FlameIntensity 9.75 / FlameHeatGate .7·Low .1·High .6 / 결합). 0.8초 진홍/노랑 휘도비 .31~.33(K범위 안), 코어 보호는 통과. 진홍 sRGB 중앙 우리 R179·G27·B28 대 LIVE-2 17-3 R103·G8·B20이라 격차는 R 53%·G 45%, 곡선·열 문은 R만 낮춤 → '곡선·열 문 MID 소진', 램프 색·위치 MID는 미시험. 종결 기준 6개 중 2개 통과, 결합 레버 진전 없음 1회째. 근거 `Saved/AgentOps/20261022/round19/judge-lc.md`
- 20차(2026-10-22): ㊸ 굽기 없음 — 램프 색·위치 MID 세션 RC 24회(기반 BF1000T085H05 + 램프 Cold·Warm [1,.008,.035] / Warm 위치 .55 / 램프+밝기 지수 6 / 카드 밝기 0 진단). 최근접 램프+밝기 지수 6(0.8초 비 .280·luma 2.69·진홍 31.3k·코어 보호 통과)도 머리 몫 .053으로 탈락. 셰이더가 램프 색 휘도를 1로 정규화해 G를 낮추면 R이 올라 R 몫(우리 R180 대 LIVE-2 103)은 MID로 못 닿음, 카드 밖 진홍은 9%. 결합 레버 진전 없음 2회 연속 → ㊶에 따라 FX-8c 종결(proposed), 무대·JSON·MI는 현 굽기 상태 유지. 남은 격차(구조): 진홍 R 성분, 나이 분리(luma 2.3~2.7 대 5.57·ring), 평면 부채 형상, 밝기·머리 맞바꿈, 파괴 힘 1000의 코어 부작용, 주황 몫. 후속 FX-8c-2(todo): 셰이더 FlameRampColdLuminanceScale(기본 1) + 꼬리 국소 냉각(spec12b A2-3, round12/stage/cooling 3원 병합)을 한 빌드로, 기반 BF1000·트레일 .85·머리 .5 + 램프 [1,.008,.035](밝기 지수 6은 머리 손실 재현으로 제외), 보로노이 파쇄(A2-6)는 별도. 근거 `Saved/AgentOps/20261022/round20/judge-rc.md`
- 상태: proposed (2026-10-20 claude, "권장안으로 진행")

### D-57 나이아가라 저작·시험 경로(에이전트 작업용)
- 배경: 사용자 문의(2026-10-21) — Gemini 문서의 C++ 브릿지·외부 MCP 방식으로 에이전트가 나이아가라를 만들고 시험할 수 있는가. 검증 `Saved/AgentOps/20261021/niagara-agent/answer.md`·`refute.md`.
- 사실: UE 5.8 공식 NiagaraToolsets(실험)가 AllToolsets로 켜져 있고 이 프로젝트 MCP에 툴셋 5개가 노출된다(System: CreateNiagaraSystem·AddEmitter·AddModule·AddRenderer·SetStackInputData·AddUserVariables·GetSystemCompileState 등). 저장·컴파일 요청 도구는 없다. 입력 단위 HLSL 식 주입은 되고 Custom HLSL 노드 그래프·새 모듈 에셋은 어렵다. NullRHI 헤드리스에서는 나이아가라가 켜지지 않는다(L-editor-29).
- 제안: 외부 MCP 플러그인(unreal-niagara-mcp는 보관·무라이선스, Monolith·UE-MCP는 제3자 C++·포트 노출)은 들이지 않는다. 공식 툴셋 + run_in_editor 저장 + 부족분 TDGameEditor C++ 래퍼(컴파일 요청·저장·Sim Cache 비교)로 간다. D-49(나이아가라 출력 전용) 유지. 첫 걸음은 로드맵 N0 관문(불티 나이아가라 시스템 스크립트 생성·재현·오프스크린 Sim Cache 수치 시험), 통과 시 P-16·P-30 '나이아가라 기본, ISM 대체' 확정.
- 대가: 헤드리스 시험 → 오프스크린·PIE, 비트 결정론 → 통계 판정, uasset 리뷰 → 생성 스크립트·JSON 덤프.
- N0 관문 결과(2026-10-21 12차): `Tools/FluidFX/editor_make_fluid_ember_niagara.py`가 MCP로 `/Game/FluidFX/Niagara/NS_TD_FluidEmber`(GPU, SampleTexture 밀도 거절·속도 끌림, User 14, 구 메시 + `MI_TD_FluidEmberNiagara_Default`)를 생성, 같은 세션 두 번·재시작 뒤 한 번 생성의 정규화 덤프 11절 sha256 동일, 반증(크기 값 하나)은 UserVariables 절만 다름. 오프스크린 `TDGame.FluidFXRhi.Niagara.Ember.*` 3종 통과(도장 15개·없음 0개, 결합 방향, 같은 시드 11=11·시드 반증 10). 남음: 재시작 뒤 툴셋 컴파일 상태 조회 시간 초과, 거절 1회 스폰이라 C++ ISM(재시도 8회) 대비 수용 약 1/8 — P-16 전환 때 스폰율을 밀도 면적 비례로 맞춤(proposed), PIE A/B·성능 미측정. 재현성 사본 `/Game/FluidFX/Niagara/Repro/` 5개는 커밋하지 않고 삭제는 사용자 승인 대기.
- 상태: proposed (2026-10-21 claude, "권장안으로 진행")
### D-58 FluidFX 프리셋 굽기 값 JSON 정본화(FX-8c)
- 질문: 프리셋 굽기 값(시뮬·분출기·조각 꼬리·볼륨 렌더·불꽃 카드)을 C++ 리터럴에 둘 것인가, 컴파일 없이 바꿀 데이터로 뺄 것인가. 사용자 "네 권장으로 진행"(2026-10-22, 굽기 무컴파일 제안에 대한 답).
- 선택지: (A) 무대 스크립트 표(override False DA는 런타임에 C++ 빌더를 다시 불러 무시되므로 기각) (B) 데이터 에셋 정본(이진 diff·빌더의 에셋 로드 필요로 기각) (C) 텍스트 JSON 정본 + 엔진 서브시스템이 읽고 빌더 5개는 적용만.
- 결정(권장안): (C). 정본 `Content/FluidFX/PresetData/TDFluidPresetTuning.json`(StructDefaults → Groups → Kinds, 같은 필드는 한 곳에서만, 폭발은 Groups.ExplosionPair), `UTDFluidPresetTuningSubsystem`(콘솔 `TD.FluidFX.ReloadPresetTuning`, 첫 읽기 실패 시 에디터 ensure·쿠킹 빌드 Fatal, 다시 읽기 실패 시 이전 표 유지), 검사 `Tools/check_fluid_preset_data.py`, `Config/DefaultGame.ini` NonUFS 스테이징, 무대 스크립트 4개는 시작 때 검사·다시 읽기·보고 sha256. 굽기 절차 정본은 `Docs/TDFluidLiveSystemDesign.md` 9.1절.
- 근거: `Saved/AgentOps/20261022/preset-data/`(spec-pd-b·build-report·refute-code·refute-rules), 변경 전·후 빌더 덤프 비트 동일, 민감도(ExplosionPair 유지 0.15) 2칸만 변화, 시험 235/235·RHI 29/29.
- 위험·후속: 값 회귀 시험은 빌더 = JSON 자기 참조라 의도치 않은 값 변경은 JSON diff와 덤프 대조로 잡음. 패키징 빌드 NonUFS 스테이징·첫 읽기 미검증, 첫 읽기 실패 뒤 PIE는 ensure 1회 후 헤더 기본값으로 조용히 돎, C++ 검증기는 Clamp·대소문자만 다른 중복 키를 보지 않음(Python 검사기가 잡음), 물·옛 종류는 회귀 시험 숫자 고정이라 굽기 때 C++ 시험 갱신.
- 상태: proposed (2026-10-22 claude, "권장안으로 진행")
### D-59 FX-7p 성능 기준선 부분 종결과 재측정 FX-7p-r(D-52·D-54)
- 질문: FX-7p(RM:452) 측정 결과를 어떻게 닫고 D-54②·MG-6·FX-8a '+10%'·측정 체제·FX-4b 순서를 어떻게 정할 것인가. 사용자 "묻지 말고 권장안으로 진행", "커밋까지 알아서"(2026-10-22).
- 결과: PIE 32장면 합본(측정 4회, 2회차는 리드백 링 수정 바이너리). 20행 = 사전 통과 5(J4·J5.x4·J8.transmittance·J8.explosion·J9)·사전 미달 1(J3.solver 49×256² GPU 7.515>4.0)·사후 참고 미달 쪽 7(J2·J3.game·J3.render·J3 기울기 2·J5.x1·J5.p95)·판정 불가 7(J1·J6·J7 전부·J8.campfire_tick). 주원인은 장면 FrameTime 단일 안정 관문과 GT 묶임 PIE, 1회차 10장면 설정 오염(P1). 기준선 33·참고값 23.
- 결정(권장안): (a) FX-7p 부분 기준선 종결, 재측정 FX-7p-r(규칙 사전 등록: judge-final 5.1절 + judge-closure 2절, 체제 관문 A·B 두 측) (b) MG-6 0.45 유지(미달 폭 안정값 오프스크린 무표식 0.462 기준 0.012), 사전 지표 0.488(변동 39%)의 spec-b:68 매핑 0.55 개정에서 벗어난 편차(512 소비 프리셋 없음·FX-16 먼저) (c) D-54② Auto '>256' 유지·판정 불가(프리셋이 Auto를 안 씀), 프리셋별 GPU 지정은 FX-7p-r·RM:904 화면 승인 뒤 JSON으로 (d) FX-8a '+10%'는 소급 판정 불가, 절대 예산, 회차 간 재현성 확인 뒤 사용 (e) 측정 체제: PIE 체제 관문(SP 상향) → 미달이면 패키지 Test 빌드 → 그래도 안 되면 J1·J2 판정 불가로 닫고 D-52 지표 개정을 사용자 결정으로 (f) FX-4b는 설계·사본 구현까지, 통합·수용 전에 FX-4a 수용 3 재판정(RM:532 순서 편차) (g) 측정 편차 기록: 셰이더 관문 3%→10%, F1·F2·F4 1200프레임, J6 지표 FT→tick·GT (h) GPU 리드백 링 크래시 ①·② 종결: 커밋 9cdea12e, 판별 시험 G(GPU 타일 2→4)가 수정 전 판에서 같은 서명으로 크래시·수정 판 3/3 무사고.
- 근거: `Saved/AgentOps/20261022/fx7p/run2-judge/judge-final.md`·`judge-closure.md`(3판, 우선)·`refute-*`, `Saved/AgentOps/20261022/fx7p/crash-r1.md`, 증거 `Docs/Validation/FluidFX/FX-7p-baseline-2026-10-22.json`·`FX-4a-gpu-2026-10-22.json`(공식·정정 값 병기). 도구 결함 L-editor-54·55.
- 위험·후속: 1회차 단일 회차 기준선 값은 다른 회차 재현 전, GPU 병목 체제 FT는 미측정, 모닥불 CPU tick 1.99~2.22 경계, MG p95 정의 미확정. FX-7p-r 묶음 R-A1~R-E(약 1.5시간, 클럭 잠금 불필요).
- FX-7p-r 1부 결과(2026-10-23, 사용자 "FX-7p-r(FX-4a 수용 3 재판정)을 권장안대로 진행하고, 끝나면 FX-4b 통합까지"·"질문하지 않고 모두 권장으로, 커밋 포함"): 명세 `Saved/AgentOps/20261023/fx7pr/spec-fx7pr-a.md`(사전 등록, 반박 2회 v2, 해시 잠금 `prereg-lock.txt`), 분석기 `Tools/FluidFX/fx7pr_judge.py`. 체제 관문(A·B 두 측 GPUTime−GT ≥1.0ms) 시운전으로 S6 SP 130·F1·F2 SP 100(이번 회차는 SP 100에서도 GPU 병목). 판정(한 에디터 프로세스, 5라운드, `r.TDFogScattering 0`): REF Sol 0.4929 유효, **J2 미달** — S6(512²×4 GPU 보임) 효과 차분 FT 6쌍 구간 [2.66, 2.74]ms·SP100 환산 [2.42, 2.49]ms > 1.7(Sol A 1.846·GPUTime d 2.64), **J1 미달(근소)** — F2 GPU d p50 1.060 > 한도 1.026(F1 CPU d 0.845 + 잡음 0.181), p95 통과. 따라서 **FX-4a 수용 3 1차 미충족**(proposed). 사전 등록 귀결(명세 2절): FX-4b는 기본 꺼짐(`TD.FluidFX.WaterGpu` 0)으로 통합하고 FX-4b 수용(기본 켜짐·화면 승인)은 수용 3 충족이나 사용자 결정 뒤로 미룬다(RM:532 편차 E2). 권장 후속: ① J2는 솔버만으로 1.846 > 1.7이라 FX-4a 4×512² 비용 절감(MG·패스 융합 레버) 또는 D-52 예산 개정이 필요 — 사용자 결정 대기, 권장은 FX-4b 통합 뒤 레버 측정 먼저 ② J1 미달은 모닥불 프리셋이 이미 CPU 기본이라 기본 동작 영향 없음(GPU 병목에서 GPU 경로가 0.2ms 비쌈을 기록). 편차 E1~E9(1부·2부 분리, Test 빌드 불가 → 대체 미착수, FireStage 유효성 (d) 0.046 등). 2부(J3·J5~J8·프리셋 GPU 지정 규칙)는 뒤로 미룸, 분석기 7.4(b) 보완 먼저. 증거 `Docs/Validation/FluidFX/FX-7p-r-a-2026-10-23.json`.
- 상태: proposed (2026-10-23 claude, 권장안으로 진행)
### D-60 FX-4b GPU 물 계층 설계(사본 단계)와 물 → 물체 상호작용 후속
- 질문: FX-4b(RM:525-540)를 어떤 구조로 만들고, 통합·수용은 언제 하며, 물 → 물체 상호작용(부력·물살 끌림)은 어디에 둘 것인가. 사용자 "W-1 종결 관문과 FX-4b 설계·사본 구현을 권장안대로", "물 → 물체 상호 작용 뒤 덧붙여 진행"(2026-10-23).
- 결정(권장안, 근거 `Saved/AgentOps/20261023/fx4b/decision-brief.md` D1~D14, 명세 `spec-v3.md`): ① 물 배치는 정확 등급(64의 배수·256 이하)이라 GPU 192²가 실제로 존재(D-54① 편차) ② 압력: 256 이하 CPU와 같은 열린 경계 Jacobi, 512 이상 ½ 열린 경계 다중 격자(기본)와 ½ 커널 피드백(GPU 전용 선택, 묶음 B4) — 기본값은 통합 뒤 사전 등록 측정 ③ 축소 하한: 필드는 256 이하 전 해상도·512 ½·1024 이상 ¼, 압력 ½은 512 이상만(RM ①② 편차) ④ SM5라 FP16 UAV 읽기 금지, 256 이하 속도·잔물결 RG32F ⑤ 안내선·수역 래스터를 솔버 밖 순수 함수로 꺼내 CPU 비트 동일 유지 ⑥ 출력 3장 인코딩은 CPU와 같음 ⑦ 지점 표본 리드백(64점·버퍼 링, CPU·GPU 공통 함수, 프레임 나이 포함)만 넣어 나중 물 → 물체가 같은 함수를 씀 ⑧ 잔물결 이류는 CPU 규칙 그대로(D-53①) ⑨ 물 GPU는 콘솔 변수 `TD.FluidFX.WaterGpu`(기본 0) 뒤 — 기본 동작·기존 시험 불변 ⑩ 물 패스 GPU 통계는 기존 솔버 통계와 형제(FX-7p 열 의미 보존) ⑪ 빨강·골든·사본 빌드는 격리 git 작업 트리에서(PD-F, 본 트리 무변경) ⑫ 통합·수용은 FX-4a 수용 3 재판정(FX-7p-r) 뒤(D-59 f).
- 물 → 물체: 지금 작업(W-1 관문·FX-4b) 뒤 별도 단계. 범위(소품만·캐릭터 포함)는 그 단계 시작 때 사용자에게 묻는다. 규칙에 걸리는 판정은 결정론 안내선 유속, 시뮬 값은 연출에 쓰는 구성을 권장(게임플레이 분석 '유체는 판정 불가'와 일치). 창 밖 대체값·부력·물리 연결은 그 단계.
- 위험·후속: 예상 비용 192² PIE 0.17~0.40ms(예산 0.10)·1024² 0.85~1.52ms(예산 0.9, 레버 L1~L5 적용 뒤)로 초과 가능성이 높다 — 통합 뒤 실측으로 판정하고 넘으면 예산 개정을 사용자 결정으로 올린다. 명세 미확인 14개(spec-v3 11절).
- 사본 검증(2026-10-23): 명세 `Saved/AgentOps/20261023/fx4b/spec-v3.md`(3관점 반박 2회), 사본 `stage/`(B0 빨강 시험 4·B1 모듈 자료·B2 셰이더 7+GPU 패스·B3 게임 연결, 도구 integrate·worktree·정적 검사). 격리 작업 트리 `C:/Project/TDGame-fx4b-wt`(git worktree, 7b173813)에서 빨강 유효(등록 32개 결과·첫 문구 일치), 초록 헤드리스 250/250(골든 H5 CPU 비트 동일)·기존 RHI 27+단독 2 회귀 0·물 RHI 31/33. 수정 편차: DEV-23 ≥512 속도 RG32F 기본(D4 사전 등록 G2 규칙, 1024 유량 비 FP16 0.926·0.969 → 1.003·1.004), DEV-24·25 시험 장면(S6 분사 프레임, S11 CPU 팔 기준 동등성 띠), F1 닫힌 항목 ½ 우변 평균 빼기(S8 닫힌 사이클 4 발산비 5.8e-2 → 1.7e-4), F2 ½ 뒤 전 해상도 Jacobi 후평활(콘솔 변수 `TD.FluidFX.WaterGpuHalfPostSmoothIterations` 기본 4, 1024 둑 띠 0.384 → 0.274). 판별력 변형 V1~V9 모두 필수 실패 충족(7개 등록표 그대로, V5·V6은 예측보다 많은 시험이 잡음 — 판별력 문제 아님). 결정 대기(proposed): PD-Q1 S8 열린 경계 '유입=유출 1%'는 전 해상도 정확해(CG)도 불균형 2.88%라 도달 불가 → 기준을 '정확해 불균형 + 0.5%p 이하'로 재정의(GPU 2.78%) 권장; PD-Q2 R22b 1024 둑 띠 0.274(한도 CPU192×1.5=0.260, 후평활 16회도 0.269)는 1024 필드 ¼ 해상도(D3) 탓 — 비용 때문에 ¼ 유지하고 편차 수용, 통합 뒤 화면으로 둑 누설 확인 권장. 후속: FX-4a 다중 격자 셰이더가 SM5 FXC에서 컴파일되지 않음(기준 커밋부터, D3D11 경로) — 별도 수정 항목. 예산(192² 0.10·1024² 0.9ms) 판정은 통합 뒤 측정.
- 사용자 확정(2026-10-23, "PD-Q1은 S8 기준을 '정확해 불균형 +0.5%p 이하'로 바꾸고, PD-Q2는 1024 ¼ 필드 편차를 수용하는 권장안으로 확정"): PD-Q1·PD-Q2 confirmed. 반영(사본·작업 트리 W): S8 열린 경계 = GPU 불균형 ≤ 같은 계의 전 해상도 CPU CG 정확해 불균형 + 0.5%p(사이클 1 0.0278 대 한도 0.0338, 사이클 4 0.0288 대 0.0338), 부모 추가로 정확해 정상 범위 ≤ 4%·CG 반복 목표 0.5배(문턱 근처 오판 제거). R22b 1024(¼ 필드) 한도 CPU192 × 1.75와 같은 실행 512 × 1.3(부모 선택 사후 문턱, 실측 1.585배·1.226배), 512는 × 1.5 유지. 원인은 ¼ 필드 해상도와 지표의 해상도 편향이 겹쳐 미분리. 결과: 물 RHI 33/33·기존 RHI 27/27·헤드리스 250/250, 판별 유지(후평활 끔 → R22b 512·1024 실패, 변형 V6 → S8 실패), 반박 4회(3관점 + 최종) 합격. 편차 DEV-26~29 `Saved/AgentOps/20261023/fx4b/stage/deviations-green.md`. 남은 것: 통합(FX-7p-r 뒤), 통합 때 V1~V9 새 기준 재실행과 V5·V6 허용 집합 보정, 1024 둑 누설 화면 확인, FX-4a 다중 격자 셰이더 SM5 후속.
- 통합(2026-10-24, D-59 (f)·FX-7p-r 1부 뒤, 사용자 "끝나면 FX-4b 통합까지"): spec-v3 7.4대로 본 트리에 red·module·game 41파일 적용(재기반 문제 0, `TDFluidGrid2DSolver.h`·`TDFluidTypes.h`는 작업 트리 줄 끝만 CRLF라 LF로 맞춤, 내용 동일), 빌드 성공, 헤드리스 `TDGame.FluidFX` 250/250(N1 비물 기준선·골든 H5 포함), 물 RHI 33/33, 물 단독 2, 기존 RHI 27/27·리드백 단독 2·측정 2 통과, 새 바이너리 에디터 기동 셰이더 오류 0. `TD.FluidFX.WaterGpu` 기본 0이라 기본 동작 불변. FX-4a 수용 3 1차 미충족(FX-7p-r 1부)이므로 FX-4b **수용**(기본 켜짐·RM:533 1024² 화면 승인·192²≤0.10/1024²≤0.9ms 예산 실측·7.5 도구 묶음)은 뒤로 미룸. 격리 작업 트리 TDGame-fx4b-wt는 정리 대상.
- 성능 레버(2026-10-24, 사용자 "질문하지 않고 모두 권장으로 빠르게, 커밋 포함"): 통합 뒤 오프스크린 `PerfWater.PassTimingProfile`(RTX 2060, 무표식 중앙값)이 192² 0.148ms(예산 0.10의 1.48배)·1024² 1.495ms(0.9의 1.66배)라 test-strategy 11.1 규칙(1.5배 초과 → PIE 전 설계)대로 레버를 적용. 설계 3갈래 `Saved/AgentOps/20261024/fx4b-acc/design-{projection,fields,small}.md`. 적용(각 콘솔 변수 `TD.FluidFX.WaterGpu*` 뒤, 기본 켬): 사전 등록 L4 투영 1회/스텝(≥512)·L5 계열(잔물결 짝수 복사 생략, 열린 경계 평균 조기 종료, 마른 Jacobi 타일 생략)·P4 F2 둑 띠 타일만, **목록 밖 PD-E2(proposed)**: 열린 경계 스캔 1024스레드·고체 먼저 읽기·스탬프+힘+와도 융합·½ 개구 재사용(모두 출력 비트 동일, 시험 `Water.Module.OptimizationsMatchLegacy`·`Water.OptimizationsKeepOutputs`). 기본 끔: 압력 꼬리 융합(192 +0.005ms 손해)·MG 비활성 타일 생략(손해). 결과 192² 0.135·512² 0.318·1024² 0.982ms(출력 포함 1.088·창 이동 1.214), 품질 R22b 1024 띠 0.2837(한도 0.3029, 여유 6.4%)·S8·R4a·R4b 통과. **L1 잔물결 ½ 해상도는 보류**: 넣어도 1024 출력 포함 약 1.0ms로 예산에 닿지 않고 잔물결 해상도만 낮아짐(LIVE-2 품질 동등 우선). 다음: PIE 예산 판정(`spec-fx4b-budget.md`, 분석기 `Tools/FluidFX/fx4b_budget_judge.py`) 뒤 미달이면 예산 개정 proposed.
- 수용 측정(2026-10-24~25): PIE 예산 판정(명세 `Saved/AgentOps/20261024/fx4b-acc/spec-fx4b-budget.md` 사전 등록·해시 잠금, 분석기 `Tools/FluidFX/fx4b_budget_judge.py`, 커밋 11b943cd 바이너리, 한 에디터 프로세스) — WQ0 유효(1.224ms, 기준 1.088의 0.8~1.5배), **B192 미달** 명명 GPU d [0.131, 0.132] > 0.10, **B1024 미달** [1.227, 1.228] > 0.9(PIE가 오프스크린보다 약 13% 높음), G192·G1024 통과 0.003ms·GW 게임 스레드 0.017ms 통과(G행은 CSV 열 이름이 `TDFluid/GameThread/<이름>`이라 판정을 연 뒤 열 대응만 정정한 사후 판정, L-editor-60), GW 이동 프레임 명명 GPU p95 2.156 > 평균×1.5 2.104(2% 초과), WQ4 2048은 하위 스텝 >2라 PD-H 통과 0%(기록). 탐침(`pie_probe_fluid_water.py`) CPU 192·GPU 192·GPU 1024 모두 연속 2회 통과, 192 쌍은 유속·여울 비 2.150 대 2.146·흰 물보라 0.491 대 0.495·물결 52.5 대 54.7cm로 같은 판정·값 차 ≤4%, 거품 하류 표류는 GPU 탐침점 한도(64) 때문에 생략하고 헤드리스 R7로 갈음, 깨어 있는 틱 CPU 1.55 대 GPU 0.11ms. 화면 시트 `Saved/FluidFX/showcase/fx4b-river-triptych-20261025-025943/`(CPU 192·GPU 192·GPU 1024 21장, 같은 카메라·시각) **사용자 승인 대기(RM:533)**. **예산 개정 proposed(권장)**: 192² ≤0.15ms·1024² ≤1.3ms(PIE 명명 GPU)·이동 프레임 p95 ≤ 평균×1.6 — 근거: 레버 적용 뒤에도 남은 레버(L1 잔물결 ½·L3 이동 복사 접기·S5)를 다 넣어도 1024 PIE 약 1.05·192 약 0.12로 원 예산에 못 닿고 L1은 품질을 낮춤, GPU 경로가 CPU 192 경로보다 게임 스레드를 약 1.7ms 덜 쓰는 교환이 PIE GT 묶임에서 유리. 대안: 원 예산 유지 시 L3(이동 프레임)·S5(192)·L1 순 추가 레버. 개정 승인과 화면 승인 전까지 `TD.FluidFX.WaterGpu` 기본 0 유지.
- W-2s-a(2026-10-25, D-64 새 방식): 명세 `Saved/AgentOps/20261025/w2s/spec-w2s-a-v3.md`(조사 5·반박 3·재검증). 젖음 갱신(CPU·GPU R32F, 원천 물 칸·흰 물 이웃 팽창·밀려 올라옴·둑 높이, 건조 τ), 흰 물 전용 소산(Sea 0.12, 끄면 기존과 비트 동일), 젖은 띠 데칼(C++ 매 틱 배치, 비동기 재질, 높이 관문 HLSL, WetOpacity 0.20), 해변 무대 `LV_TDFluidCoastStage`, 끄개 `TD.FluidFX.WaterWetness`. 고리 스탬프·고리 폭 판정은 W-7로 이관(DEV-14). 본 트리 헤드리스 265·물 RHI 46(새 GPU 젖음 12 포함)·기존 27 통과. 화면·성능 판정은 일괄 단계(D-64).
- 상태: proposed (2026-10-23 claude, 권장안으로 진행)
### D-61 NarshaADK(NarshaMCP) 도입 범위와 나이아가라 저작 경로 보강(D-57)
- 질문: 사용자가 설치한 NarshaADK(에디터 플러그인 NarshaMCP 0.13.7 + MCP 서버 + Claude Code 플러그인 7개, 2026-10-23)를 어디까지 쓰고, 에이전트 나이아가라 제작에 어떻게 넣을 것인가. 사용자 문의(2026-10-23) "활용 방안 분석, 궁극적으로 나이아가라를 AI 에이전트가 만들 수 있는지 검토".
- 사실: 나이아가라 저작은 빈 시스템 생성·기존 이미터 복사·동적 입력·이벤트·사용자 매개변수·시뮬 대상·경계·렌더러 재질/메시뿐이고 모듈·렌더러 추가와 입력 상수·HLSL 식 설정이 없다(C++ 0건). 오프라인 읽기는 구조는 맞지만 사용자 매개변수 3/14·값 0·영속 ID 반대·모듈 단계 손실(L-editor-56). 플러그인 7개(스킬 115·SessionStart 훅·프로젝트 에이전트 복사본)는 AGENTS 7·11·15절과 충돌하고, 설치기가 에디터 시작마다 `.mcp.json`·`.claude/`·홈 설정을 다시 쓴다(L-repo-21). WebSocket 127.0.0.1·인증 없음, 텔레메트리 동의 disabled, 라이선스 조건 미확인.
- 제안(권장안): ① MCP 서버 `narshamcp`만 켜고 플러그인 7개 끄기 ② `enableAllProjectMcpServers` 끄고 서버 이름 목록만 허용 ③ `.mcp.json` narshamcp env에 `NARSHA_TELEMETRY_DISABLED=1` ④ `Plugins/Marketplace/`는 `.gitignore`, 설치기가 만든 `.claude/agents/unreal-specialist.md`·`.claude/skills/routing.json`·`.antigravity/`는 커밋하지 않고 삭제는 승인 뒤 ⑤ 나이아가라 정본 경로는 D-57 그대로(공식 NiagaraToolset + 저장 + 정규화 덤프 + 오프스크린 Sim Cache), NarshaMCP는 에디터 없는 1차 점검(`validate`·`find_asset_consumers`)·`get_preview`·`measured_cost`·이벤트 배선 편의(덤프 대조 뒤)로만 ⑥ 다음 단계 불티 전용 생성기를 "JSON 명세 → 나이아가라" 범용 도구로 일반화. AGENTS.md 11절에 세 번째 MCP를 적는 개정은 ①~③ 적용 뒤 사용자 승인으로.
- 근거: `Docs/AgentCollaboration/research/narsha-adk.md`, `Saved/AgentOps/20261023/narsha-adk/package-audit.md`.
- 위험·후속: NarshaMCP 에디터 저작 작업은 미실측(빌드 창 fx4b-wt 보유로 에디터 미기동, 시험안은 연구 문서 8절). 설치기가 끈 설정을 되살리는지 다음 에디터 시작 뒤 확인. 실험 판 API라 판올림 때 4절 대조 재실행.
- 적용(2026-10-23 12:17, 사용자 "D-61 권장대로 정리해 줘"): ①②③ 적용 — `.claude/settings.local.json` 플러그인 7개 false·`enableAllProjectMcpServers` false·서버 5개 명시, 텔레메트리 환경 변수는 `.mcp.json`·`.cursor/mcp.json`·`~/.codex/config.toml`·`~/.gemini/antigravity/mcp_config.json`. 추가로 `.mcp.json` narshamcp `timeout` 180000(L-repo-22). 사용자 요청 "엔진 소스 검색 켜기"는 보류 — 켜는 방법 모두 최대 약 46GB RSS·5분(RAM 64GB, 다른 세션 빌드·시험 중), 엔진 심볼은 이미 동작하고 소스 텍스트는 rg로 대체. ④⑥ 미적용.
- 엔진 소스 스캔(2026-10-23 13:38, 사용자 "빌드·시험 끝나 메모리가 비면 켜 줘"): fx4b-wt 보류가 3시간 규칙으로 만료되고 언리얼·컴파일러 프로세스 2분 부재·여유 45.4GB에서 보류 없이 실행. 엔진 호출 그래프 생성 390초(엔진 파일 32,153·함수 606,515), 여유 메모리 최저 42.5GB(문서의 40GB 감소보다 훨씬 작음). `ue_grep` 엔진 텍스트 계층은 여전히 project_only, 설정은 `persisted: false`(L-repo-23)라 데몬 재시작 시 꺼짐.
- 설정 저장(2026-10-23 14:08, 사용자 "NARSHAMCP_CONFIG_PATH를 추가해 저장되게, 다음 세션에서 재시작 비용과 ue_grep 엔진 검색 확인"): narshamcp 설정 4곳에 `NARSHAMCP_CONFIG_PATH=C:/Project/TDGame/Plugins/Marketplace/NarshaMCP/MCP/config/config.ini` 추가(미검증). 확인 절차는 `Docs/AgentCollaboration/research/narsha-adk.md` 8절.
- 저장 확인(2026-10-23 20:33, 사용자가 이 세션에서 하기로 선택): 저장은 환경 변수가 있는 프로세스에서만 됨, 재시작마다 호출 그래프 재생성 약 4.5분·메모리 13.5GB, `ue_grep` 엔진 텍스트 검색은 저장해도 켜지지 않음(L-repo-23). 18:18 에디터 시작 때 설치기가 env·자동 승인 끄기를 되돌려 다시 적용함(L-repo-21). 제안: 저장된 엔진 스캔 끄기, 되돌림을 끝내려면 `TDGame.uproject`에서 NarshaMCP 에디터 플러그인 끄기(오프라인 MCP는 유지).
- 정리(2026-10-23 23:05, 사용자 "저장된 엔진 스캔을 끄고 NarshaMCP 에디터 플러그인도 꺼 줘(오프라인 MCP는 유지)"): `set_engine_config engine_source_scan=false`를 환경 변수 준 프로세스로 저장(`config.ini` [EngineIndexing] false), 공유 데몬도 세션 값 false. `TDGame.uproject` Plugins에 `NarshaMCP` Enabled false 추가 — 에디터 시작 때 설치기·WebSocket 서버(127.0.0.1:30011~)·기기 지문 기록이 멈추고 오프라인 MCP(`narshamcp.exe`)는 유지(확인: `ue_glob` 정상). 효과(에디터가 플러그인을 안 올리고 설정을 안 되돌리는지)는 다음 에디터 시작 때 확인.
- 제거(2026-10-23 23:11, 사용자 "NarshaMCP 그냥 사용을 하지 않게 해주세요 삭제도 좋습니다"): 제조사 `uninstall`(dry-run 확인 뒤, exe 사본으로 실행)으로 `.mcp.json`·`.cursor/mcp.json` 항목, `unreal-specialist.md`, `routing.json`, `Intermediate/NarshaMCP`(4.2GB), `.antigravity/`, uproject 항목, Codex·Antigravity 전역 항목, Claude 플러그인 마켓플레이스, `Plugins/Marketplace/NarshaMCP`를 지움. 추가로 `narshamcp://` 프로토콜 해제, `~/.narshamcp`, `~/.claude/plugins/data/core-narsha-adk`·표식 파일, `.claude/settings.local.json`(설치기 생성), `~/.codex/skills/td-*` 복사본 3개(AgentRules OP 80행 위반, 원본과 동일) 삭제. `TDGame.uproject`·`.mcp.json`·`.cursor/mcp.json`·`.gitignore`는 HEAD와 의미가 같아 HEAD로 복원(diff 없음). 남김: `~/.claude.json` pluginUsage 통계 7건(Claude Code 관리 파일, 동작 없음), 검토 문서·교훈·`Saved/AgentOps/20261023/narsha-adk/`.
- 상태: superseded (2026-10-23 사용자, NarshaADK 제거)
### D-62 안개 화면 공간 산란: SSFS 동등 클린룸 플러그인 대 엔진 FSSS(5.8 실험)
- 질문: 사용자 요청(2026-10-23) "Fab Screen Space Fog Scattering(SSFS)과 완전 동일하게 개발, 지금은 탑다운이지만 3인칭 전환·다른 프로젝트 재사용 가능성, 먼저 가능성 분석" + "엔진 베타 기능도 같이 분석". 어느 경로로 갈 것인가.
- 사실: 기능 동등은 UE 5.8 독립 플러그인으로 가능(BeforeDOF 구독·SceneColor 교체는 공개 API), 안개 매개변수·볼류메트릭 포그 텍스처는 Renderer 비공개 `FViewInfo`에만 있어 비공개 헤더 의존. 화소 동일은 원본 없이 증명 불가(구조 가설 2개·합성식 미확정). 엔진 FSSS는 안개 패스 안 물리 PSF 블러, 기본 꺼짐·Epic 품질 이상만·Set 함수 없음. 탑다운(카메라 약 939cm, 지면 9.6~26.6m)·밀도 0.02에서 안개 불투명도 0.9~2.4% → 어느 방식도 약하고 FSSS 기본 밉 지수 0.005 이하(추정). 기본 반투명(After DOF)은 두 방식 모두 흐리지 않음. Fab 표기 "AI 사용 허용: 아니요".
- 제안(권장안): ① SF-0 측정 장치 + FSSS 실측(코드 0, `LV-World` 탑다운·3인칭 카메라 켜기/끄기 화면·ms) ② 클린룸 플러그인 `Plugins/TDFogScattering`(AGENTS 14절 범위 밖 첫 자체 프로젝트 플러그인, TDGame 모듈 비의존, 콘솔 변수 `r.TDFogScattering.*`로 SSFS 13개와 의미·기본값 1:1) SF-1 프로토, 첫 작업은 비공개 헤더 컴파일 탐침 ③ 지원 엔진은 5.8 한정 ④ "완전 동일" 검증이 필요하면 원본 1좌석 구입 후 블랙박스 지표 대조(원본 셰이더·소스는 에이전트 읽기 대상에서 제외) ⑤ 탑다운 보정(카메라별 강도)은 SSFS에 없는 기능이라 별도 결정.
- 근거: `Docs/FogScattering/01-ssfs-fsss-feasibility.md`, 반박 `Saved/AgentOps/20261023/ssfs/refute-engine.md`·`refute-numbers.md`(초안 불투명도 2배 오류·반투명 주장 정정, L-editor-57).
- 위험·후속: 비공개 헤더 컴파일 가능성·`FViewInfo` 배치, 볼류메트릭 포그 텍스처 수명, FSSS 실측 미시행(에디터 미기동), Fab 약관 원문 미확인(403).
- 결정(2026-10-23 사용자): 경로 "플러그인 형태", 원본 "구입하지 않음, 할 수 있는 최대한으로", 탑다운 보정 "좋네요"(추가). 나머지는 권장안 적용 — 지원 엔진 5.8 한정, `Plugins/TDFogScattering`·`r.TDFogScattering.*`, 탑다운 보정은 기본 꺼짐(꺼짐 = SSFS 동등 모드). ① FSSS 실측은 경로 판단용에서 비교 기준선(선택)으로 축소하고 측정 장치는 SF-1 앞에 둔다. 원본 없는 수용 기준: 콘솔 변수 13개 대응표 검사, 공개 비교 이미지 8쌍과 정성 대조, RTX 2060 1080p 패스 시간(공개 RTX 3060 0.38·GTX 1660 0.66ms 사이), 탑다운·3인칭 화면 사용자 승인.
- 상태: accepted (2026-10-23 사용자, 화소 동일은 범위 밖)
- 탑다운 보정 값 v2(2026-10-24 실측 재결정, 사용자 위임 "권장으로 진행"): 필드 `DistanceScale` 4·`WeightPower` 1.5(`Config/DefaultEngine.ini` `[SystemSettings]`), 하얀 고원 거리 ×10(→40), 가라앉은 별밭 거리 ×1.5(→6)·강도 ×1(`generate_realm.py`·이계 에셋). 실제 필드 `LV_DarkFantasy_OpenWorld`·이계 8곳 게임 연결 검증에서 위 띠 0.147~0.198·아래 띠 ≤ 0.094·휘도 +0.7~1.2%. 경계 블렌딩 통로 18곳·카메라 1.4배 확대도 목표 근처(넘침 없음). 근거 `Docs/FogScattering/04-sf4-spec.md` 8절. v1(2026-10-23: 필드 32·고원 ×1.75·별밭 ×1/32·강도 ×0.35)은 `L_TDWorld_Main`을 필드로 오인해 그 안개를 흉내 낸 값이라 폐기(L-editor-58).

### D-63 무한 샌드박스 세계: 허브 바깥 런타임 청크 무한 확장을 할 것인가
- 질문: 사용자 문의(2026-10-23) "탑다운 게임이지만 무한한 샌드박스형 세계 구현에 대해서 가능성 검토 된다면 어떻게?" 검토 결과에 따라 착수할지, 어떤 형태·수준으로 할지.
- 사실: 월드 파티션은 런타임 셀을 못 만들고 랜드스케이프는 런타임 생성·편집이 안 된다(WITH_EDITOR) → 월드 파티션 밖 C++ 청크 스트리머 + 동적 메시 지형이 필요. 분리 벤치(IWBench, 편집기 -game·NullRHI) 64m 청크 게임 스레드 약 0.8~1.0ms, 최소 미준비 거리 약 88m, `/fp:precise` 생성 비트 일치(Game `/fp:fast`는 46~48% 비트 차이, L-build-14). 미측정·미달: GPU·렌더 스레드·패키지 빌드, ISM 충돌(x15 프레임 CPU p99 9.0ms)·내비 반영 튐(최대 38.9ms). 콘텐츠(자연 메시 약 30개)·단조로움이 실제 병목.
- 제안(권장안): 허브(월드 파티션 + 랜드스케이프) 유지 + 테두리 밖 64m 청크 무한 확장(수준 A) → 구획 건설(B) → 2.5차원 높이 편집(C, 선택), 완전 복셀 비권장. 먼저 IWQ-01(GQ-22 싱글 확정)·IWQ-02(D-05 보류 해제·범위)를 사용자에게 묻고, 답과 무관하게 TDGame 무변경 G0 벤치(6~9작업일)·게임 타깃 해시 시험층(IW-06)은 진행 가능. 나머지 IWQ-03~20은 문서 6절 권장안.
- 근거: `Docs/InfiniteWorld_Plan.md`, `Docs/InfiniteWorld/01-feasibility.md`(반박 3건 반영·재검증), 측정 `Docs/InfiniteWorld/measurements/`, 도구 `Tools/InfiniteWorld/`.
- 위험·후속: D-05(사용자 보류)·D-06(인보커 전역 전환)·D-40·D17(M3-04 창 플로우 필드, AGENTS 13절 절차)·GP-08·GP-09(저장·부활 통합)와 얽힘. 기간은 슬라이스 2단계 완료 후부터 수준 A 허브 연결판 약 2.2~3.7개월(추정).
- 상태: decision 대기 (제안 2026-10-23 claude). 2026-10-24 사용자: "검토 결과만 받고 여기서 멈춥니다. 착수는 나중에 결정" → G0 벤치 포함 착수 보류, IWQ-01·02 미결

### D-64 FluidFX 진행 방식 전환: 기능 먼저·값은 프리셋 데이터·보정과 판정은 일괄(D-52)
- 질문: 기능마다 조사·명세·반박·사전 등록 통계·화면 판정을 다 하는 방식이 느리다(W-2s-a 반나절 이상). 사용자 "먼저 기능 개발 다 하고 파라미터만 데이터로 따로 빼서 검증하는 단계로 … 모두 바꿔서 진행"(2026-10-25).
- 결정(사용자 확정): ① 기능 개발은 LIVE-2 매뉴얼·엔진 코드 분석으로 묶어서 하고, 명세는 짧은 계약 + 파라미터 표, 반박 1회 ② 조절 값은 전부 프리셋 JSON 필드(기본값은 LIVE-2 유추) ③ 기능마다 검증은 빌드·기존 회귀·결정론 골든·오프스크린 비용 1회 ④ 값 맞추기는 장면×파라미터 일괄 보정 도구로 ⑤ 성능 PIE 판정과 사용자 화면 승인은 영역별 한 번씩.
- 근거·계획: 파라미터 마스터 표 `Saved/AgentOps/20261025/master/master-table.json`(240행: code 61·both 119·data 60, 제안 필드 884). 표가 밝힌 전제 공백(재질 값 JSON 밖·검증기 새 키 거부·실행 중 재적용 없음·나이아가라·컴포넌트 값 다리 없음) 때문에 첫 파동은 '데이터 다리'(`spec-wave0-data-bridge.md`)다. 그 뒤 기능 파동(코드 사슬 순서), 일괄 보정, 일괄 판정.
- 적용: W-2s-a부터. W-2s-a는 빨강·초록·회귀까지 했고 화면 지표(젖은 띠 휘도·레이스 3m·5초 50%)·성능 증분 판정은 일괄 단계로 넘겼다. 사전 등록 PIE 통계(FX-7p-r·FX-4b 방식)는 일괄 판정 때만 쓴다.
- 파동 0 데이터 다리(2026-10-25 통합): 명세 `Saved/AgentOps/20261025/master/spec-wave0-data-bridge-v2.md`(반박 1회 M1~M8 반영). 프리셋 표 등록 12구조체(새 InputBrush·RuntimeOps 등), 솔버·GPU 튜닝 상수 → 설정 필드(기본값 = 옛 값, 골든 44 불변), 전역 CVAR '모양' 값 → 프리셋 필드(CVAR는 -1 기본·0 이상이면 덮어쓰기, R4), GPU 매개변수 272→288B, 재적재 시 실행 중 액터 재적용·PIE 종료 때 정본 복귀(FWorldDelegates::OnWorldCleanup, 명세의 EndPIE 대신 — UnrealEd 의존 회피), 재질 값 정본 `TDFluidMaterialLook.json`(MI 35·나이아가라 1, 덤프 차이 0), 사본 → JSON 전환(불일치 42→0, 횃불 SourceOffsetCm Kinds.Torch로), 검사기 C++·파이썬 같은 규칙, 일괄 보정 도구(calib_sweep·calib_judge·prereg_lock·fluid_tuning_patch). 이름 규칙 N1~N4(브러시 상수 InputBrush, 솔버 상수 SimulationConfig, 재질 값 모양 파일, UENUM은 TDGame)와 R2 실효 값 규칙은 proposed. 본 트리 헤드리스 275·물 RHI 46·기존 27 통과, 값 독립(×1.01, 213필드) 통과.
- 파동 1(2026-10-26 소스 통합, 에셋 일부): 명세 `Saved/AgentOps/20261025/wave1/spec-{w2s-bc,w7,fx8n,fx10r-fx12r}.md`(반박 1회씩), 구현 반박 F1~F3 수정(`Saved/AgentOps/20261026/wave1-int/`). W-2s-b·c 감싸기 영역(기본 Clamp)·타일 합성(TileWaves·TileMap, 마스터 기본 0)·LOD 메시(`TD.FluidFX.WaterLodGrid`, 기본 꺼짐), W-7 근접 잔물결 층(256²·600cm, River·Sea 켬, `TD.FluidFX.NearRipple` −1/0/1)·Aux 장애물 거리 G·고리 스탬프(비율 0 = 꺼짐), FX-8n 나이아가라 구동 7종 생성기·검사기, FX-10r 안개 볼륨·FX-12r 자국 재질과 무대 스크립트. 재질 값 조각 5개는 `fluid_material_look.py merge`(이름 단위 깊은 병합, 충돌 거부)로 정본에 병합(새 항목 28·충돌 0). 본 트리 헤드리스 302·물 RHI 47·단독 5·WrapArea 3·NearRipple 4·기존 묶음 28 통과(나이아가라 9는 에셋 대기), 골든 44 불변, 값 독립 220필드 통과. 비용 기록: 근접 층 중앙값 0.061ms(1/60)·0.080ms(1/30), 감싸기 타일 128² 0.178·256² 0.332ms(참고치 초과, 일괄 판정 몫).
- 파동 1 편차(proposed): ① `WrapArea.CpuGpuParity` 기준을 '운동 에너지 차 ±10%'에서 '같은 장면 Clamp 대조 차 대비 ±10%'로(Clamp 경로도 이 감쇠 장면에서 18.6% 차) ② 근접 층 메모리 1 MiB는 `EstimateWaterActorMemory` 선택 인수로 보고(런타임 호출처 없음, 동작 불변) ③ 창 이동 불변 시험은 c·T 대신 이산 의존 범위 32칸 + 가장자리 띠 38칸 제외 ④ RingStampMatchesCpu·PointSourceDispersion·TileMapTargetParameters 시험과 띠 폭 수치 판정은 일괄 보정 몫 ⑤ 무대 스크립트 상수(안개 상자 높이·광원)·LOD 예산 0.85·Wrap 유령 ×4는 JSON 밖 ⑥ 보정 계획 `Look:` 경로 적용(G1)·metrics.sources(P3)는 보정 착수 때 도구 확장.
- 파동 1 에셋(2026-10-26 마무리): 생성기 결함 3건 수정 뒤 재생성 — 물 재질 ISM 사용 플래그를 노드 생성 전 속성으로(L-editor-61), 나이아가라 표본 출력을 고유 입자 속성으로 복사·열거 입력 형식 NiagaraExt_StackInputData_Enum·격자 UV는 UniqueID(L-editor-62·68), 자국 PDO는 MakeMaterialAttributes(L-editor-64). 나이아가라 시험은 Smoke 기본이 CPU 해법기를 골라 실패하던 것을 시험 프리셋 override(GPU)로 고침. 본 트리 헤드리스 303·물 47·기존 RHI 묶음 36·Niagara 12·WrapArea 4(새 TileMapTargetParameters)·NearRipple 4 통과, 골든 44 불변, 값 독립 303. 후속: 물 무대 보고 material_look_sha256·docstring TileWaves, 물 마스터 덮어쓰기 디스크 잔존 처리(L-editor-67), MeshArray 이중 연사·높이 차 2.78cm(일괄 보정).
- 파동 2(2026-10-27, 커밋 b3294e86 소스·e6a5e729 수정·78a8cea4 에셋): 명세 `Saved/AgentOps/20261026/wave2/spec-{fx9,fx5,fx10c,fx12c,fx13r-fx11r}.md`(반박 1회씩)·교차 계획 `cross-plan.md`, 격리 트리 IB1~IB4 뒤 본 트리 통합. FX-9 이질 볼륨 구동·높이 캐시, FX-5 대량 입력(GPU 스플랫·데이터 채널), FX-10c 안개 입력 규칙·타일, FX-12c 모래·눈 자국·변형 메시, FX-13r 식생 흐름, FX-11r 구름 층. 확인: 헤드리스 345/345, 오프스크린 묶음 37·MassInput 9·Niagara 12·VolumeDriver 3, 골든 44 불변, 무대 7 STALE 0·PIE 10초 오류 0, 반박 통과(`Saved/AgentOps/20261027/wave2-fix/refute-final.md`).
- 파동 2 편차(proposed): ① 명세 0.1절 DEV 항목(DEV-FX9-1~13·DEV-FX5-1~7 등) 그대로 ② `Config/DefaultGame.ini` 쿠킹 폴더 `/Game/FluidFX/Niagara/DataChannels`(check_cook_coverage uncovered 0) ③ 높이 캐시 굽기 요청 `assign_actor`로 구역 액터 지정·저장(cross-plan A-02 순환 해소) ④ WrapArea RHI 3개·FogTile.CpuWrapVolumeParameters를 `FluidFXRhi`+NonNullRHI로(헤드리스 수 감소, L-editor-69) ⑤ `TrySampleTerrainHeightCm` 호출처 없음(X-16 지면 2순위, 무대는 평지라 파동 2 영향 없음) ⑥ 일괄 보정·판정 대상: 스탬프 1만6천 중앙값 0.276~0.378 ms(제안 0.3), R7 GPU 반영 6프레임(목표 1~2), 구름 비용 1.6 ms ⑦ `calib/plans/fx5_flamethrower.json` 지표 이름이 측정 키 형식과 어긋남(FX-8n G1 때 고침) ⑧ 해변 물+안개 동시 표시(X4)는 사용자 승인 뒤. 남은 확인: 질량 입력·구동 나이아가라 생성기의 덮어쓰기·재컴파일 경로 처음부터 끝까지 실행, NS 재컴파일 뒤 질량 무대 PIE·덤프 재대조.
- 파동 3(2026-10-30, 뼈대 f4d3680f, 병합 18f67f26·64542109·347c01f8·d93d0c15·cb671ddf, W3-F 13201d7c, 시험 수정 7e06b181·78fa1c1a, 도구·값 47a548a1, 에셋 15d430b0): 명세 `Saved/AgentOps/20261029/wave3/spec-{fx11c,fx13c,fx14,fx15a,fx15b,fx16a,fx16b,w1b,w3,w5,w6}.md`(반박 1회씩)·교차 계획 `cross-plan.md`. 사용자 속도 요청으로 작업 트리 5차선(L1 fx14·fx15b, L2 fx15a·fx13c, L3 w6·w5, L4 w1b·w3, L5 fx16a·fx16b·fx11c)을 한 워크플로 안에서 동시 구현 → 순차 병합 → W3-F → 에셋 A0~A9. 확인: 헤드리스 490/490, 오프스크린 `TDGame.FluidFXRhi.` 181/181(에셋 의존 포함), 골든 44 불변, 무대 보고 11 STALE 0, PIE 17맵 오류 0, 통합·최종 반박 통과(`Saved/AgentOps/20261030/wave3-finish/refute-final.md`).
- 파동 3 편차(proposed): ① W3-0 공유 계약 뼈대를 먼저 커밋하고 차선을 나눈 것(D-64 진행 방식 안의 편차) ② 5차선 구성, "FX-14 → W-6 같은 차선" 대신 병합 순서로 처리 ③ X-03 캐시 우선순위(밀집 물 캐시 > 시뮬 상태 캐시, 밀집이면 포착·씨앗 거부) ④ X-06 `UsesTerrainBase` 식 ⑤ X-18·R6-M1 장면마다 맵(밀집 물 6맵, 입자 WP1·WP2 분리) ⑥ CAL-0 보정 도구 확장 연기(일괄 보정 앞) ⑦ FX-14 M4 `LavaFlow` 평지화 ⑧ FX-11c M3 (b), FX-13c M5 (b) ⑨ 각 명세 0.1절 DEV 목록 그대로, DEV-W3-10(근접 잔물결 마스크 .b→.r 시험 4줄) ⑩ DEV-W1B-R3: R3 "중심 흰 물>0"을 "진입점 반경 200cm 안 흰 물 최대>0"으로(착수 반경 38~131cm, 스탬프 실효 반경 12.5cm라 중심 칸 도달 불가, L-editor-79) ⑪ FX-16b 6c 강 시뮬 캐시 미완: 폰 추종 강이라 시작 창에 강이 없어 0 캐시(L-editor-77) → 제외하고 Saved로, 다시 굽기는 C++ 씨앗 재중심·별도 고정 강 액터(+탐침 도구 액터 선택)·플레이어 시작 이동 중 결정 필요 ⑫ 6c 요청 `backend`는 gpu가 아니라 cpu(`TD.FluidFX.WaterGpu` 기본 0) ⑬ FX-15a 기준 DA는 명세의 `DA_TDFluid_Smoke`가 없어 `SmokeSoft` ⑭ 구름 무대 물리 카메라 노출 끔(파동 2부터 PIE 검정, L-editor-76) ⑮ 일괄 보정·판정 대상: W-3 RA1 생존 3916(명세 4500~6000, 시험은 계산 기대값 3918.75와 대조), W-1b L26-l 물방울 수명<비행 시간·L26-w 접촉 세기 0.2 미달, 비용 기록 9파일(`Saved/AgentOps/20261030/wave3-assets/a9-tests/index.md` 2절) ⑯ 다음 파동 도구 개선: `build_and_test.py` 작업 트리 간 공유 보류 파일 옵션, 헤드리스 null 시험의 트리 간 동시 실행, 에셋 의존 RHI 묶음을 A4 직후 한 번(A9 전에 값·탐침 결함 발견). 넘길 것 12개는 `cross-plan.md` 7절.
- 일괄 보정 착수(2026-10-30, proposed): 실행 계획 `Saved/AgentOps/20261030/cal0/plan-batch-v2.md`(계획 51개 분류·블록 K1~K10·W 되쓰기·성능·승인 시트), 도구 명세 `spec-cal0-v2.md`(CAL-0: 계획 v2 모르는 키 거부, 단계 frame·screen·confirm·verify, N장 중앙값, 덧씌움 누적, 잠금 v2 닫힌 세계, 실행기 `Tools/FluidFX/calib_batch.py`). 사전 등록 규칙 P-1~P-28(결과를 연 뒤 불변): P-1 채택 = screen·confirm에서 같은 변형이 둘 다 유의·대조 통과 / P-2 강한 효과 면제 없음 / P-3 단계 지표 = N장 중앙값 / P-4 채택값은 덧씌움에 누적, 정본 되쓰기·재생성은 마지막 1회 / P-5 Globals·StructDefaults 채택은 같은 커밋에서 C++ 헤더 기본값도 / P-6 비용 지표는 ⑤ 영역 예산 표로 / P-7 Look 변형은 실행 중 적용(MID·나이아가라 사용자 변수) / P-8 정본에 없는 Look 키는 계획 base로, 채택 때 정본에 추가 / P-9 fog_f7 광원은 actor_prop(무대 상수) / P-10 계획 정리(g5→g11 등) / P-11 LIVE-2 이미지 없는 목표는 명세 [제안] 범위로 규칙 판정, FX-P에 '동등 증명 아님' / P-12 비교 시트 목표는 갤러리 측정 먼저 / P-13 화면 승인은 진행을 막지 않음 / P-14 보정 중 골든 재생성 금지 / P-15 FX-P 성능은 PIE SP 체제 / P-16 갈대는 거울 시험 먼저(통과) / P-17 draft_status 없는 계획만 실행 / P-18 영역 틀은 frame 대조 이미지로만, 잠금은 screen 전 / P-19 viscous_blood 헤더 기본값 끝값 제외 / P-20 계획당 1라운드 1채택, 민감 손잡이가 남으면 K10 탐욕 2라운드 / P-21 판정 상수(민감 2.0·표류 2×median_floor·대조≥3·표본≥3·다장면 gain 합 최대·악화 금지) / P-22 `base_tuning_ships` 거짓은 K10 출하 조건 확인 / P-23 비용 손잡이는 비용이 오르지 않는 방향만 / P-24 w6_dense_creek settle 90 / P-25 승인 없이 진행하되 영역 '동등 확인'·D-52 종결은 승인 기록 필요 / P-26 일괄 실행은 calib_batch로만 / P-27 시험 분류표 사전 등록(설계 단언 실패 = 채택 거부) / P-28 0b 재분류(측정 출처 없는 계획은 blocked, P-9로 풀린 결함은 cal0). 막힌 계획 5개(foliage_k5·w6 creek·ship·w5 액체 구·폭포)는 C++ 통계 게터가 생기면 복귀. 버린 손잡이·목표는 `Tools/FluidFX/calib/structural-gaps.json`·`approval-only.json`.
- 상태: confirmed (2026-10-25 사용자)

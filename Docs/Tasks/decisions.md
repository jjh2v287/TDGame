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

### D-53 FX-W W-1 구현 편차(명세 `Saved/AgentOps/20261018/fluidfx/water/final-spec.md` 대비)
- 편차: ① 잔물결 이류 clamp 를 2×2 최소·최대 대신 4×4 탭 범위 + 범위폭 25% 여유(`TDFluidGrid2DSolver.cpp` AdvectRipples, 진폭 시험 0.547→0.806) ② 시선 앞당김을 GetDynamicCenterOffsetPlane 대신 ResolvePlaneAnchorWorldLocation 재정의(월드 평면 경로가 오프셋을 더하지 않음) ③ ATDFluidLiveActor 내부 수면 그림자 끔이 모든 월드 평면 라이브 액터에 적용(단일층 물이 바닥에 그림자, L-editor-40) ④ ATDFluidWaterActor 는 내부 평면을 항상 숨기고 태그 메시만 물을 그림 ⑤ 꺼진 경로 부수 변경(경사 계산의 마른 셀 갱신 표시, 비유한 잔물결 도장 거부, 꺼진 상태 안내선·수역 보관) — 체크섬·골든 해시 불변 ⑥ 강 프리셋 WhitewaterFromVorticity 0.15→0.3(PIE 탐침 바위 하류 흰 물보라 0.16~0.19 < 수용 0.3, 2026-10-19). 결함 수정(편차 아님): 액터 브러시가 ActorBrushRippleImpulseCm 를 스탬프에 옮기지 않아 PIE 잔물결 0 → `ApplyActorBrushSettings` 한 줄, 탐침 결과 해석·거품 측정 시점·잡음 문턱(95백분위)
- 상태: proposed (2026-10-19 claude, 사용자 지시 "권장안으로 진행"). ①은 단조 계단 필드 장기 이류에서 새 극값이 없는지 시험을 더한 뒤 확정, 남은 낮은 위험(지형 표본 끔이면 물 계층 알파 0 고정, 안내선 전체 상자 판정, 스트리밍마다 재수집)은 W-2 전 처리 후보
- 근거: `Saved/AgentOps/20261019/fluidfx/fxw/b1-report.md`, 마무리 워크플로 wf_b88994dc-b74(A1 정적 반박·B1 반박), 시험 `Saved/AgentOps/20261019/fxw-w1-b1-*-tests.json`

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
- 상태: proposed (2026-10-20 claude, "권장안으로 진행")

### D-57 나이아가라 저작·시험 경로(에이전트 작업용)
- 배경: 사용자 문의(2026-10-21) — Gemini 문서의 C++ 브릿지·외부 MCP 방식으로 에이전트가 나이아가라를 만들고 시험할 수 있는가. 검증 `Saved/AgentOps/20261021/niagara-agent/answer.md`·`refute.md`.
- 사실: UE 5.8 공식 NiagaraToolsets(실험)가 AllToolsets로 켜져 있고 이 프로젝트 MCP에 툴셋 5개가 노출된다(System: CreateNiagaraSystem·AddEmitter·AddModule·AddRenderer·SetStackInputData·AddUserVariables·GetSystemCompileState 등). 저장·컴파일 요청 도구는 없다. 입력 단위 HLSL 식 주입은 되고 Custom HLSL 노드 그래프·새 모듈 에셋은 어렵다. NullRHI 헤드리스에서는 나이아가라가 켜지지 않는다(L-editor-29).
- 제안: 외부 MCP 플러그인(unreal-niagara-mcp는 보관·무라이선스, Monolith·UE-MCP는 제3자 C++·포트 노출)은 들이지 않는다. 공식 툴셋 + run_in_editor 저장 + 부족분 TDGameEditor C++ 래퍼(컴파일 요청·저장·Sim Cache 비교)로 간다. D-49(나이아가라 출력 전용) 유지. 첫 걸음은 로드맵 N0 관문(불티 나이아가라 시스템 스크립트 생성·재현·오프스크린 Sim Cache 수치 시험), 통과 시 P-16·P-30 '나이아가라 기본, ISM 대체' 확정.
- 대가: 헤드리스 시험 → 오프스크린·PIE, 비트 결정론 → 통계 판정, uasset 리뷰 → 생성 스크립트·JSON 덤프.
- N0 관문 결과(2026-10-21 12차): `Tools/FluidFX/editor_make_fluid_ember_niagara.py`가 MCP로 `/Game/FluidFX/Niagara/NS_TD_FluidEmber`(GPU, SampleTexture 밀도 거절·속도 끌림, User 14, 구 메시 + `MI_TD_FluidEmberNiagara_Default`)를 생성, 같은 세션 두 번·재시작 뒤 한 번 생성의 정규화 덤프 11절 sha256 동일, 반증(크기 값 하나)은 UserVariables 절만 다름. 오프스크린 `TDGame.FluidFXRhi.Niagara.Ember.*` 3종 통과(도장 15개·없음 0개, 결합 방향, 같은 시드 11=11·시드 반증 10). 남음: 재시작 뒤 툴셋 컴파일 상태 조회 시간 초과, 거절 1회 스폰이라 C++ ISM(재시도 8회) 대비 수용 약 1/8 — P-16 전환 때 스폰율을 밀도 면적 비례로 맞춤(proposed), PIE A/B·성능 미측정. 재현성 사본 `/Game/FluidFX/Niagara/Repro/` 5개는 커밋하지 않고 삭제는 사용자 승인 대기.
- 상태: proposed (2026-10-21 claude, "권장안으로 진행")

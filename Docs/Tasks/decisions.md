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
  - 근접 노티파이는 TD 것(`UTDAnimNotifyState_MeleeAttack`)을 유지하고 PJ의 높이 고정만 `bLockHeightToOwner`/`LockedHeightOffset` 플래그로 이식(테스트 `TDGame.Combat.MeleeAttackNotifyLocksBladeHeightToOwner`).
  - BehaviorTree 태스크(`UTDBTTask_CombatTokenRequestAndRelease`)는 컴파일용으로만 이식. TD AI는 StateTree이므로 실제 사용 시 StateTree 태스크로 다시 만든다.
- 폴더 정리: `Source/TDGame/{Core,Characters,Framework,Combat/{Damage,GAS,AnimNotify,Skills,Tests},AI,Performance,Actors,World/{Streaming,Persistence,Generation}}`. 클래스 이름은 바꾸지 않았으므로 블루프린트 참조는 유지된다.
- 영향: `Docs/MonsterAI_CombatSim/*`의 소스 경로 참조는 새 경로로 치환함(줄 번호는 변화 없음).

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

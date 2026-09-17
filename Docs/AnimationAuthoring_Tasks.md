# 애니메이션 제작 도구 — 남은 작업 대장

기준 문서: [AnimationAuthoring.md](AnimationAuthoring.md), [작업 절차](../Tools/AnimationAuthoring/references/workflow.md)
작성: 2026-09-16 (감사 시점 기준)

## 현재 상태 요약

2026-09-16 기준. 도구 7개가 **최초로 종단 검증을 통과했다**(스모크 테스트 25/25). 그 전까지는 등록만 되고 한 번도 호출된 적이 없었다.

| 항목 | 상태 | 근거 |
|---|---|---|
| C++ 도구 7개 구현 | 작성 완료 | `Source/TDGameEditor/Animation/` |
| 컴파일·링크 | 통과 | `Build.bat TDGameEditor Win64 Development` |
| MCP 툴셋 등록 | 동작 확인 | 로그의 `Registered animation authoring toolset: TD*` 3건 |
| 도구 실제 호출 | **통과** | 스모크 테스트가 7개 도구를 전부 호출 |
| 스모크 테스트 | **25/25 통과** | `Tools/AnimationAuthoring/smoke-report.json` (`success: true`) |
| UE 5.8 API 사용 | 이상 없음 | deprecated 미사용 |
| git 커밋 | 미커밋 | `?? Source/TDGameEditor/Animation/`, `?? Tools/AnimationAuthoring/`, `?? Docs/AnimationAuthoring*.md` |

### 최초 실행에서 드러난 것

스모크 테스트는 **한 번도 통과한 적이 없었고, 통과할 수 없는 상태였다.** 실제 결함 2건이 있었다.

1. 몽타주 요청에 `slot` 필드가 아예 없었다. 음성 테스트에만 `slot`을 붙이고 정상 호출에는 붙이지 않아 항상 "slot must be a nonempty string"으로 실패했다. → `InspectSkeleton`이 보고한 실제 슬롯(`DefaultSlot`)을 쓰도록 수정.
2. FK 컨트롤 이름을 `<본>_ctrl`로 가정했으나 UE 5.8 `FKControlRig`의 실제 이름은 **`<본>_CONTROL`**이다. → 수정.

### 실행 환경 주의

`/Game/...` 인수를 **Git Bash에서 전달하면 안 된다.** MSYS 경로 변환이 `/Game/Characters/...`를 `C:/Program Files/Git/Game/Characters/...`로 바꿔 도구가 경로 오류로 거부한다. 이 스크립트들은 PowerShell에서 실행한다.

```powershell
python Tools/ue_editor.py ensure
python Tools/AnimationAuthoring/smoke_test.py --mesh /Game/Characters/Mannequins/Meshes/SKM_Manny_Simple --output Tools/AnimationAuthoring/smoke-report.json
```

---

## A. 검증 (최우선)

### A-01 스모크 테스트 최초 실행
- 상태: **done (2026-09-16)**
- 우선순위: 높음
- 목표: 7개 도구가 실제 MCP 왕복에서 동작하는지 한 번에 확인한다.
- 절차:
  ```powershell
  python Tools/ue_editor.py ensure
  python Tools/AnimationAuthoring/smoke_test.py --mesh /Game/Characters/Mannequins/Meshes/SKM_Manny_Simple --output Tools/AnimationAuthoring/smoke-report.json
  ```
- 완료 조건: 보고서의 `success: true`. **충족** — 25개 항목 전부 통과.
- 기록: 최초 실행에서 스모크 테스트 자체의 결함 2건(몽타주 `slot` 누락, FK 컨트롤 접미사 `_ctrl`→`_CONTROL`)을 고친 뒤 25/25 통과. 상태 요약의 "최초 실행에서 드러난 것" 참조.
- 비고: 에디터가 꺼져 있으면 포트 8000 연결이 거부된다. `smoke_test.py`가 `ue_editor.py ensure`를 호출하지 않으므로 수동 선행이 필요하다(→ B-04에서 해소).

### A-02 스모크 테스트 진단력 보강
- 상태: todo
- 우선순위: 높음
- 선행: A-01
- 목표: 실패 시 원인을 보고서만 보고 알 수 있게 한다.
- 완료 조건:
  - `check()`가 도구 응답의 `error` 문자열을 보고서에 기록한다(`smoke_test.py:83-91`은 현재 버린다).
  - 첫 실패에서 중단하지 않고 남은 항목을 계속 검사한 뒤 실패 목록을 모아 보고한다(`:168`의 fail-fast 제거).
  - 스키마에 없는 인수를 만나면 맨 `KeyError` 대신 "이 도구에 그 인수가 없다"는 메시지를 낸다(`:63`).
  - 보고서에 `총 N개 중 M개 통과`를 기록한다.

### A-03 생성 에셋 시각 검증 절차 고정
- 상태: todo
- 우선순위: 중간
- 선행: A-01
- 목표: 발 접지·손 위치·관통·루프 연결을 매번 같은 방법으로 확인한다.
- 완료 조건: 프레임 0/중간/끝을 캡처하는 스크립트 1개. 기존 자산 재사용 — `Tools/WorldGen/capture_views_mcp.py`와 `EditorToolset.EditorAppToolset.CaptureViewport`.
- 비고: `workflow.md:175`는 현재 "가능한 캡처 도구를 검색하라"고만 적혀 있어 매 실행 탐색 비용이 든다.

### A-04 C++ 자동화 테스트 추가
- 상태: todo
- 우선순위: 중간
- 목표: 에디터·MCP 없이도 회귀를 잡는다.
- 완료 조건: `Source/TDGameEditor/Tests/`에 JSON 파싱·검증 분기(fps 소수 거부, 목적지 충돌, 슬롯 부재, 한도 초과) 테스트 추가. `Automation RunTests TDGame` 로 실행.
- 비고: 현재 검증 수단은 에디터가 떠 있어야 하는 `smoke_test.py`가 유일하다.

---

## B. 차단 결함 수정

### B-01 실패 시 빈 패키지 누수 — 동일 경로 재시도 영구 차단
- 상태: **done (2026-09-16)**
- 우선순위: **높음 (가장 시급)**
- 증상: 에셋 생성 도중 실패하면 메모리에 빈 `UPackage`가 남는다. `CheckDestination`이 `FindPackage`로 메모리 내 패키지도 거부하므로(`TDAnimationAuthoringTools.cpp:201`), **에디터를 재시작하기 전까지 같은 `asset_path`로 다시 시도할 수 없다.** 에이전트는 실패 후 같은 경로로 재시도하는 것이 자연스러운 행동이라 반드시 밟게 된다.
- 원인: `DiscardCreation`(`:270`)이 트랙 검증 실패 경로 2곳(`:470`, `:581`)에서만 호출된다. 나머지 실패 지점은 정리하지 않는다.
- 조치: `TDAnimationAuthoringCommon.h`에 RAII 가드 `FTDAuthoringPackageScope`를 추가하고 4개 생성 경로 전부에 적용했다. `Commit()` 없이 스코프를 벗어나면 패키지와 에셋을 폐기한다. 기존 `DiscardCreation` 헬퍼는 대체되어 삭제.
- **추가로 밝혀진 것:** `MarkAsGarbage()`만으로는 부족했다. 가비지 표시된 패키지도 다음 가비지 컬렉션 전까지 오브젝트 해시에 이름이 남아 `FindPackage`가 계속 찾아낸다. 즉 **원래의 `DiscardCreation`도 실효가 없었다.** 폐기할 때 패키지와 에셋을 고유한 이름(`*_TDDiscarded`)으로 **개명**해야 이름이 풀린다.
- 검증: 목적지 폴더 자리를 파일로 선점해 저장을 실패시킨 뒤 같은 `asset_path`로 재요청. 수정 전 `Destination package already exists`로 차단됨 → 수정 후 성공. 재현·검증 스크립트는 일회성이라 저장소에 남기지 않았다(항구적 회귀 방지는 A-04).

### B-02 로그 전무
- 상태: **done (2026-09-16)**
- 우선순위: 높음
- 증상: 4개 `.cpp` 전체에 `UE_LOG` 0건. 그런데 오류 메시지는 "에디터 로그를 확인하라"고 안내한다(`Tools.cpp:265`, `Sequencer.cpp:166`, `ControlRig.cpp:234`). 안내대로 해도 아무것도 없다.
- 조치: `LogTDAnimAuthoring` 카테고리 신설. 7개 도구 전부에 진입(요청 앞 1024자)·성공(에셋 경로)·실패(사유) 로그를 넣었다. 도구 이름은 `FTDAuthoringToolScope`가 붙인다. 서브시스템은 `IsAvailable()`과 `IsToolsetClassRegistered()`로 등록 결과를 확인해 기록한다.
- 효과(실측): A-01 최초 실행의 첫 실패 원인(Git Bash 경로 변환)을 이 로그 한 줄로 즉시 특정했다. 그 전에는 `AssertionError: inspect skeleton`뿐이었다.

### B-03 입력 검증 비일관
- 상태: **부분 완료 (2026-09-16)** — 경로·PIE·중복 검사는 처리, 한도 통일은 남음
- 우선순위: 중간
- 내용:
  - **[완료]** `InspectSequence`/`BakeAnimation`의 `sequence`·`skeletal_mesh`에 `/Game` 검증이 없었다. `CheckReadPath`를 추가해 두 도구 모두에 적용.
  - **[완료]** `CreateFKSequence`만 PIE 가드가 없었다. `GEditor->PlayWorld` 검사를 추가.
  - **[완료]** `CreateFKSequence`에 메모리 내 동일 객체 `FindObject` 검사가 없었다. 추가.
  - **[남음]** 한도가 도구마다 다르다: fps 상한 120 vs 240, 프레임 1..36000 vs 2..18000, 요청 크기 4MB / 16384자 / 65536자. 한 곳에 상수로 모으고 문서 표와 맞춘다.

### B-04 사전 점검 자동화
- 상태: todo
- 우선순위: 중간
- 목표: 에디터 꺼짐으로 인한 스택트레이스 실패를 없앤다.
- 완료 조건: `smoke_test.py`가 시작 시 `Tools/ue_editor.py ensure`를 호출하거나, 연결 실패를 "에디터를 먼저 켜라"는 메시지로 바꾼다. `workflow.md`·`SKILL.md` 0단계에도 명시.

---

## C. 기능 공백

### C-01 AnimNotify / NotifyState 배치 — **가장 큰 공백**
- 상태: todo
- 우선순위: **높음**
- 증상: 애니메이션 폴더 전체에 노티파이 관련 코드가 0건이다(`Controller.NotifyPopulated()` 1건은 무관한 API). 그런데 이 프로젝트의 전투는 `Source/TDGame/Combat/AnimNotify/`의 `TDAnimNotifyState_MeleeAttack`·`AbilityTagWindow`·`InputBufferWindow`·`JumpCapsuleModifier` 4종에 의존한다.
- 결론: **이 도구로 만든 몽타주는 히트박스도 입력 버퍼도 없어 실제 전투에 투입할 수 없다.** 현재 유일한 수단은 Python 원격 실행(`make_montage.py` 레시피)이다.
- 완료 조건: `AddMontageNotifies` 같은 8번째 도구. 노티파이 트랙 생성 + `AnimNotifyState` 클래스·시작·길이·프로퍼티(JSON) 지정. 검증된 레시피는 `AnimationLibrary.add_animation_notify_track` / `add_animation_notify_state_event` 경로이며, 구조체는 `TDDamageRule`·`TDDamageAction`·`TDScaledValue`다.
- 주의: `AnimNotifyEvent`에는 `track_index` 속성이 없다(읽으면 예외).

### C-02 기존 에셋 수정 경로 부재
- 상태: todo
- 우선순위: 높음
- 증상: 생성 도구 4개 전부 목적지가 존재하면 실패한다. 기존 AnimSequence/Montage에 키·섹션·노티파이를 **추가**하는 경로가 하나도 없다.
- 영향: 에이전트가 "한 번에 완벽히 만들거나 통째로 버리거나" 하는 단발성 워크플로에 갇힌다. 반복 개선이 불가능하다.
- 완료 조건: 명시적 `overwrite` 또는 `mode: "append" | "replace"` 인수를 받는 수정 도구. 덮어쓰기 금지 기본값은 유지.

### C-03 `InspectAnimation`의 읽기 범위
- 상태: todo
- 우선순위: 중간
- 증상: 본당 first/middle/last **3샘플만** 반환한다(`Tools.cpp:620-623`). 커브·노티파이·싱크마커·루트모션 플래그·압축 설정은 보고하지 않는다.
- 영향: C-02와 겹쳐 **읽기 → 수정 → 쓰기 왕복이 원천 불가능**하다.
- 완료 조건: 프레임 범위·본 필터 인수 추가, 노티파이·커브 목록 보고. 응답 크기 상한과 함께.
- 부수: `SKILL.md:28`·`workflow.md:174`가 "샘플/트랙 수 검증"을 지시하면서 3샘플 제한을 어디에도 적지 않아 오해를 부른다 — 문서에 명시.

### C-04 나머지 공백 (필요 시점에 판단)
- **[완료 2026-09-16]** 루트 모션: `CreateBoneAnimation` 에 `root_motion:{enable,root_lock,force_root_lock}` 인수를 추가했다. 응답에 `root_motion_enabled` 가 나온다. 루트 본 translation 키로 이동을 만든다. 몽타주의 `bEnableRootMotionTranslation/Rotation` 은 4.5부터 폐기되어 시퀀스가 제어하므로 별도 설정이 필요 없다.
- 커브(float/vector 애님 커브): 없음. `AddBoneCurve`(`Tools.cpp:464`)는 본 트랙 생성 API이지 커브가 아니다.
- 블렌드: 시간만 지원(`Tools.cpp:589-590`). 블렌드 프로파일·옵션·`BlendOutTriggerTime` 없음.
- 몽타주 브랜칭 포인트: 없음. 섹션·`next` 링크는 구현됨(`Tools.cpp:529-588`).
- 리타게팅·어디티브: 명시적 거부(`Tools.cpp:504-505`). 동일 Skeleton만 허용.
- 몽타주 슬롯 생성: 불가, 기존 슬롯만(`Tools.cpp:528`).
- AnimComposite / BlendSpace / AnimBlueprint: 없음.
- Undo/트랜잭션: 모든 컨트롤러 호출이 `bShouldTransact=false`(`Tools.cpp:441-467`).
- scale 최소 0.0001(`Tools.cpp:335`)이라 음수 스케일(미러) 불가.

---

## D. 문서 오류 (즉시 수정 — 실제 호출 실패를 유발)

✅ 표시는 2026-09-16에 수정 완료. 나머지는 남아 있다.

| # | 위치 | 내용 |
|---|---|---|
| D-01 ✅ | `workflow.md` | "`set_transform`의 생략 값은 0, **스케일은 1**" — `set_transform`에 **스케일 인수가 없다**. 실제 인수는 location_x/y/z, rotation_pitch/yaw/roll, set_key뿐(엔진 `controlrig_sequencer.py:1651-1659`). 스케일은 `set_scale`/`set_euler_transform`으로만 가능. 존재하지 않는 인수를 보내 실패한다. |
| D-02 ✅ | `workflow.md` | `add_spawnable_from_instance(sequence, object_to_spawn)` — 실제 인수명은 `obj`(엔진 `sequencer.py:614-617`). |
| D-03 ✅ | `workflow.md` | `key_controls_at_frames(section, control_names, frames)`(엔진 `controlrig_sequencer.py:661`) 누락. 컨트롤 N개 × 프레임 M개를 한 번에 키잉하는 유일한 대량 도구인데 빠져 있어 `set_transform` 개별 호출(N×M회)로 유도된다. |
| D-04 | `workflow.md:119` ↔ `ControlRigTools.cpp:215` | 입력 `asset_path`는 패키지 경로인데 응답의 같은 키 `asset_path`는 오브젝트 경로(`/Game/X/LS_A.LS_A`)다. 키 이름이 같고 형식이 다르다. |
| D-05 | `ControlRigTools.cpp:219` | 응답 필드 `control_rig_asset_path`가 실제로는 클래스 경로 `/Script/ControlRig.FKControlRig`다. 에셋 경로가 아니다 — 필드명 변경 또는 문서 명시. |
| D-06 | `workflow.md:74-85` | `CreateBoneAnimation` 필드 표에 툴팁의 하드 리밋(fps 1..120, frames 1..36000, 600초, 200만 샘플, 4MB)이 빠져 있다. |
| D-07 ✅ | `workflow.md` | 6단계 "현재 도구 목록에서 에셋 저장 도구를 찾아" — 도구는 확정되어 있다: `EditorToolset.AssetTools.save_assets(asset_paths)`. 이름을 못 박지 않아 매 실행 탐색 비용이 든다. |
| D-08 | `Docs/AnimationAuthoring.md` | `AnimationAssistantToolset`이 `TDGame.uproject`에 직접 나열되어 있지 않고 `AllToolsets` 경유로만 켜진다. 누군가 `AllToolsets`를 끄면 Control Rig 키잉 경로가 조용히 사라진다. uproject에 명시하거나 문서에 의존 관계를 적는다. |
| D-09 | `Sequencer.cpp:121` | `ExportAnimation`이 에디터 월드에 `ALevelSequenceActor`를 임시 스폰한다. 문서의 "영구 레벨 액터를 변경하지 않는다"는 진술은 FK 도구에만 검증된 것이다. Transient 스폰 여부를 확인하고 문서를 맞춘다. |

---

## E. 배포 — 스킬이 어느 에이전트에도 등록되어 있지 않다

MCP 서버 배관은 3중으로 갖춰졌다(`.mcp.json`, `.gemini/settings.json`, `.codex/config.toml` 모두 `http://127.0.0.1:8000/mcp`). 그런데 **스킬 자체는 어느 클라이언트에서도 자동 발견되지 않는다.**

| 경로 | 상태 |
|---|---|
| Claude Code 스킬 | 미설치. 프로젝트에 `.claude/skills/` 없음 |
| Codex 스킬 | 미설치. `~/.codex/skills/`에 `td-animation-authoring` 없음 |
| Gemini 확장 | 미설치. `~/.gemini/extensions` 없음, `GEMINI.md`에 애니메이션 언급 0건 |
| `Tools/README.md` | AnimationAuthoring 항목 0건 — `Tools/README.md:71`의 자체 규칙("만든 도구는 README 표에 한 줄 추가") 위반. 제미나이·Codex는 이 파일을 도구 색인으로 읽으므로 **애니메이션 도구를 발견하지 못한다** |
| `AGENTS.md` | 165줄 중 애니메이션 언급 0건 |

### E-01 `Tools/README.md`·`AGENTS.md`에 항목 추가
- 상태: todo / 우선순위: 높음 / 비용: 낮음
- 다른 에이전트의 유일한 발견 경로다. 가장 싸고 효과가 큰 작업.

### E-02 스킬 설치 스크립트
- 상태: todo / 우선순위: 중간
- `install_skill.py` 하나로 Claude(`.claude/skills/`)·Codex(`$CODEX_HOME/skills/`)·Gemini에 동시 배포. 현재는 `Docs/AnimationAuthoring.md:26`이 수동 복사를 문장으로 설명한다.
- `SKILL.md`에 frontmatter(`name`, `description`)가 이미 있어 복사만 하면 Claude·Codex 양쪽에서 유효하다.

---

## F. 토큰 절감 (결정론적 작업의 스크립트화)

### F-01 공용 MCP 클라이언트 분리 — **절감 효과 최대**
- 상태: todo / 우선순위: 높음
- `smoke_test.py:28-70`의 `TDMcpAnimationClient`(검색 모드 자동 대응 + 인수명 대소문자·언더스코어 정규화 + JSON 언래핑)가 스모크 테스트 안에 갇혀 있다.
- `Tools/anim_client.py`로 빼면 에이전트가 매 세션 `list_toolsets → describe_toolset` 왕복과 스키마 읽기를 반복하지 않아도 된다.

### F-02 확정 툴셋 이름 표 고정
- 상태: todo / 우선순위: 높음
- 문서가 "런타임에서 찾아라"만 반복한다(`SKILL.md:12`, `workflow.md:13,52,64`). 아래는 전부 결정론적으로 확정 가능하다. 표로 못 박고 "불일치 시에만 재탐색" 규칙으로 바꾼다.
  ```
  TDGameEditor.TDAnimationAuthoringTools.{InspectSkeleton, CreateBoneAnimation, CreateMontage, InspectAnimation}
  TDGameEditor.TDControlRigTools.CreateFKSequence
  TDGameEditor.TDSequencerAnimationTools.{InspectSequence, BakeAnimation}
  animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools
  animation_toolset.toolsets.sequencer.SequencerTools
  animation_toolset.toolsets.import_export.SequencerImportExportTools
  editor_toolset.toolsets.asset.AssetTools.save_assets
  ```
  이름 규칙은 `패키지 마지막 세그먼트.클래스명.함수명`이다(엔진 `FunctionLibraryToolset.cpp:78-87, 210-249`).

### F-03 대량 키잉으로 전환
- 상태: todo / 우선순위: 중간 / 선행: D-03
- 현재 `set_transform`을 프레임마다 개별 호출한다(`smoke_test.py:144-150`). 컨트롤 다수 × 키 다수면 호출 수가 곱으로 늘고 매 호출이 대화 라운드트립이다. `key_controls_at_frames` 도입 + 포즈 배열을 한 번에 키잉하는 스크립트로 접는다.

### F-04 저장·Git/LFS 검증 스크립트
- 상태: todo / 우선순위: 낮음
- `workflow.md:176`이 매 작업마다 `git status --short`, `git check-attr filter`, `git lfs status` 3종을 수동 지시한다. `verify_assets.py` 한 개로 끝난다.

### F-05 스모크 테스트 뒷정리
- 상태: todo / 우선순위: 낮음
- `--save` 실행 시 `/Game/Tests/AnimationAuthoring/Run_<id>`가 계속 쌓인다. 삭제 루틴 추가.
- 함께: `workflow.md:26,31`이 `describe-args.json`·`call-args.json`을 **저장소 안**에 쓰도록 지시하는데 `.gitignore`에 없다.

---

## G. 정리

### G-01 불필요한 빌드 의존성 제거
- 상태: todo / 우선순위: 낮음
- `TDGameEditor.Build.cs`에서 참조 0건: `ControlRigEditor`, `SequencerScripting`, `SequencerScriptingEditor`. `AnimationDataController`도 `IAnimationDataController.h`가 Engine 모듈에 있어 제거 가능성이 있다.
- 컴파일을 깨지는 않지만 에디터 빌드 시간과 모듈 로드 그래프를 키운다. 제거 후 빌드로 확인.

### G-02 사소한 일관성
- 상태: todo / 우선순위: 낮음
- `UTDControlRigTools`만 `TDGAMEEDITOR_API` 누락(`TDControlRigTools.h:8`).
- `TDAnimationAuthoringTools.h`가 `CoreMinimal.h`를 직접 포함하지 않고 `ToolsetDefinition.h`에 의존.
- `NormalizeReadPath`가 설정한 구체적 오류를 일반 메시지로 덮어쓴다(`Tools.cpp:196-199`).
- `ShouldCreateSubsystem` 미오버라이드 — Live Coding 핫리로드 후 툴셋이 구 클래스 포인터로 남을 수 있다.
- 반환값이 "JSON을 문자열로 직렬화한 값"이라 MCP `outputSchema`가 단순 `string`이 된다. 에이전트가 2중 파싱해야 하고 스키마 검증 이점이 사라진다.

### G-03 커밋
- 상태: todo / 우선순위: 중간 / 선행: A-01
- 현재 전부 미커밋이다. 스모크 테스트가 통과한 뒤 코드·문서·보고서를 함께 커밋한다.

---

### 실측 메모 (2026-09-16 작업 중 확인)

- UE 5.8 Python `AssetEditorSubsystem` 에는 `open_editor_for_assets` 와 `close_all_editors_for_asset` 만 있다. `close_all_asset_editors` 와 `get_all_edited_assets` 는 **없다**. 이걸 모르고 쓰면 조용히 실패해서 에셋 에디터 탭이 안 바뀌고, 캡처가 엉뚱한 에셋을 찍는다.
- `EditorAssetLibrary.delete_asset` 은 True 를 반환해도 무언가 참조 중이면 `.uasset` 파일이 디스크에 남는다. 확실히 지우려면 에디터를 끄고 파일을 지운 뒤 다시 켠다.
- 툴팁(ToolTip 메타)은 **1024자 제한**이 있다. 넘으면 `String constant exceeds maximum of 1024 characters` 로 빌드가 실패한다.
- 무거운 오픈월드 레벨을 연 채 에셋 에디터를 반복해서 열고 닫으면 GPU 크래시(D3D12 MMU fault)가 났다. 미리보기 작업은 빈 레벨에서 한다.

## 권장 순서 (갱신)

1~3번(로그, 재시도 차단, 문서 인수 오류)과 최초 실행 검증은 2026-09-16에 끝났다. 다음은:

1. **E-01** `Tools/README.md`·`AGENTS.md` 등재 — 비용이 거의 없고, 다른 에이전트가 이 도구를 발견하는 유일한 경로다.
2. **A-02** 스모크 테스트 진단력 보강 — 이번 실행에서 `AssertionError: <이름>`만으로는 원인을 알 수 없어 매번 에디터 로그를 따로 봐야 했다.
3. **C-01** AnimNotify 도구 — 이것이 없으면 산출물이 게임에서 쓸모가 없다. 기능 작업 중 1순위.
4. **F-01, F-02** 공용 클라이언트와 확정 툴셋 이름 표 — 이후 모든 세션의 토큰을 줄인다.
5. **A-04** C++ 자동화 테스트 — B-01 같은 결함의 회귀를 에디터 없이 잡는다.
6. **G-03** 커밋 — 스모크 테스트가 통과했으므로 지금 커밋할 수 있다.
7. C-02·C-03(읽기→수정→쓰기 왕복), B-03의 한도 통일, 나머지.

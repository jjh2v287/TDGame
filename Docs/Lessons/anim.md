# Lessons — anim (애니메이션 저작·Blender)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A.

### L-anim-01 애니메이션 도구 실측 메모의 정본 위치
- 증상: 몽타주 `slot` 필드 누락, FK 컨트롤 이름은 `<본>_CONTROL`, `delete_asset` 잔존, 툴팁 1024자 한도 등 실측 함정이 대장 문서 안에 흩어져 있다.
- 해결: 본문은 `Docs/AnimationAuthoring_Tasks.md` '최초 실행에서 드러난 것'·'실측 메모' 절(정본, 이관 전). 새 함정은 이 파일에 L-anim-02부터 적는다.
- 범위: UE 5.8, Tools/AnimationAuthoring, Source/TDGameEditor/Animation
- 증거: Docs/Validation/anim/A-01-smoke-report-2026-09-16.json (25/25 통과, 2026-09-16)
- 날짜·상태: 2026-09-18 active
- 발견: claude

### L-anim-02 Blender에서 참고 FBX를 애니메이션 포함으로 가져오면 씬 fps가 바뀐다
- 증상: 언리얼 FBX 가져오기 실패 `FBXImport: Error: 애니메이션 길이 1.56이 임포트 프레임 레이트 30 fps(서브 프레임 0.8)|hpp(과,와) 호환되지 않습니다. 애니메이션이 프레임 보더에 정렬되어야 합니다.` 도구 반환은 `Expected one animation; generated unsaved assets: []`.
- 원인: `bpy.ops.import_scene.fbx(..., use_anim=True)`가 참고 모션(`MM_Attack_01.fbx`)을 읽으며 `scene.render.fps`를 30→25로 바꿨다. 이후 40프레임(39간격)을 25fps로 내보내 1.56초가 됐다.
- 해결: 모든 FBX 가져오기 뒤, 베이크·내보내기 직전에 `scene.render.fps = 30; scene.render.fps_base = 1.0`을 다시 설정한다(`author_sword_slash.py`). 검사: `bpy.context.scene.render.fps`.
- 범위: Blender 5.2.2 FBX 애드온, UE 5.8 legacy FbxFactory
- 증거: `Saved/Logs/TDGame.log` 2026.09.18-15.00.27 항목, 재내보내기 후 가져오기 성공(`Saved/BlenderAnimation/SwordSlash/import-result.json`)
- 날짜·상태: 2026-09-19 active
- 발견: claude

### L-anim-03 언리얼 MCP `call_tool`의 `tool_name`은 툴셋 접두어를 뺀 짧은 이름이다
- 증상: `call_tool` 인수 `{'toolset_name': 'EditorToolset.EditorAppToolset', 'tool_name': 'EditorToolset.EditorAppToolset.StartPIE'}` → `Unknown tool EditorToolset.EditorAppToolset.StartPIE`.
- 해결: `tool_name`에는 `StartPIE`처럼 마지막 마디만 준다(`Tools/AnimationAuthoring/smoke_test.py`의 `rsplit('.', 1)`과 같다). `Tools/BlenderAnimation/validate_sword_slash_pie.py` 참조.
- 범위: UE 5.8 ModelContextProtocol 플러그인, 도구 검색(discovery) 모드
- 증거: 수정 후 PIE 시작·정지 성공(`Docs/Validation/BlenderAnimation/sword-slash-pie.json`)
- 날짜·상태: 2026-09-19 active
- 발견: claude

### L-anim-04 UE 5.8 IK 리타기팅 Python: 기본 작업 추가가 재실행 때 중복되고, 본 위치 샘플 API가 메시 배율을 무시한다
- 증상 1: `IKRetargeterController.add_default_ops()`를 두 번 부르면 문서 설명과 달리 `Pelvis Motion_0` 같은 작업이 중복 추가된다.
- 증상 2: `unreal.AnimationLibrary.get_bone_poses_for_time`으로 골렘 애니메이션을 재면 골반이 기준 자세보다 약 1.7배 낮게 나와 "찌그러짐"으로 오판한다(스켈레톤 에셋 기준 자세로 평가, 메시 기준 자세 아님).
- 증상 3: 편집기 월드 SkeletalMeshActor에 `override_animation_data`만 부르면 캡처에 기준 자세만 찍힌다.
- 해결: 1) 재실행 도구는 `remove_all_ops` 뒤 `add_default_ops`. 2) 포즈 수치는 `AnimPoseExtensions`(메시 기준)로 잰다 — 배치 액터 값과 일치. 3) `visibility_based_anim_tick_option=ALWAYS_TICK_POSE_AND_REFRESH_BONES` + `set_animation` + `set_play_rate(0)` + `play(False)` + `set_position(t)` 후 다음 틱에 캡처.
- 범위: UE 5.8 에디터 Python, 작업 스택형 리타기터
- 증거: `Saved/AgentOps/20260925/retarget-report.md` 7절, 도구 `Tools/MonsterAI/editor_retarget_monster_anims.py` 3회 재실행 동일 결과
- 날짜·상태: 2026-09-25 active
- 발견: claude


### L-anim-05 칼을 HandGrip_R에 회전 0으로 붙이면 권총처럼 손가락 방향으로 뻗어 모든 검 애니메이션이 틀려 보인다
- 증상: 칼날이 손 긴 축(손목→가운뎃손가락 뿌리)과 6.1°로 거의 평행하다. Greystone PrimaryA 원본을 붙이면 대기 칼끝이 바닥 −13 cm·팔로스루 −30.6 cm로 파고들고, 날 정렬 평균이 136°(이상 90°)이며, 손목을 바로잡을수록 칼이 아래팔 연장처럼 굳는다. 이전 후보들(09-17~09-23)의 "손목·칼 품질 불량"의 공통 원인이다.
- 해결: 칼자루가 주먹 공간(손 축 9 cm, 손바닥 쪽 2.8 cm)을 지나고 칼날이 엄지 쪽으로 손 축과 73°, 참날이 손가락 마디 쪽, 검지가 코등이에 닿게 쥐기를 다시 정의한다(`Tools/BlenderAnimation/sword_attack01_scene.py` `fist_grip_local`). 같은 원본이 칼끝 최저 27.9 cm·날 정렬 86°가 된다. 게임 부착값은 HandGrip_R 기준 SM_Sword 상대 위치 (−5.052, 9.119, 25.857) cm, 회전 roll −83.734°·pitch −15.888°·yaw 94.203°(`editor_solve_sword_attachment.py`, 정합 잔차 0.0001 cm).
- 범위: UE5 Manny SK_Mannequin 소켓 HandGrip_R, `/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword`. 다른 무기는 같은 도구로 다시 푼다.
- 증거: `Saved/BlenderAnimation/SwordAttack01/measure_TD_Ref_Attack_PrimaryA.json`(쥐기 수정 전후), `Saved/BlenderAnimation/SwordAttack01/sword-attachment.json`
- 날짜·상태: 2026-09-25 active
- 발견: claude

### L-anim-06 에이전트가 키 포즈·IK만으로 전투 동작을 만들면 로봇처럼 보인다 — AAA 원본 동작을 편집한다
- 증상: 절차적 키 포즈(09-19 Attack01), 확장 툴 보간(09-23 Attack02), CMU 모캡(손 최고속 2.6 m/s, 게임 공격의 1/10)이 모두 "품질이 안 좋다"는 판정을 받았다. 폴리시 단계에서도 프레임마다 여러 목표(손목·날 정렬·팔꿈치)를 동시에 최적화하면 날 롤이 한 프레임에 60° 뒤집히고 팔꿈치가 38 cm 튄다.
- 해결: 이미 가진 AAA 동작(PJGame의 Manny 리타기팅 Paragon Greystone 공격 20종)에서 요청 궤적에 맞는 클립을 도구로 분류해 고른다(`editor_analyze_attack_candidates.py`). 그 위에 루트·발·손목 보정만 얹는다. 손목은 원본 관절 파라미터(회내·굴곡·편위)를 유지하고 한계 초과분만 빼며, 보정량만 평활한다. 월드 회전 보간·포즈 덮어쓰기로 예비 동작을 바꾸지 말고 시간 재매핑으로 읽히는 자세를 조절한다.
- 범위: `Tools/BlenderAnimation/sword_attack01_*.py`, Blender 5.2, UE5 Manny
- 증거: `Saved/AgentOps/20260925/review-v3/*.md`(1차 비평), `Saved/BlenderAnimation/SwordAttack01/author-result.json`
- 날짜·상태: 2026-09-25 active
- 발견: claude

### L-anim-07 UAF 5.8.2로 캐릭터를 구동할 때의 필수 조건과 엔진 함정
- 증상: ① 모든 주입이 실패하고 `PlayAction`이 0을 돌려줌, 로그 `FInjectionRequest::ValidateArgs: Missing injection site` ② 첫 주입마다(프로세스당 1회) `Ensure condition failed: TimelineTrait.GetState(Context, ChildState) ... InjectionSiteTrait.cpp] [Line: 268]` + 콜스택 수집 멈춤(헤드리스 약 4.6초) ③ 빙결(CustomTimeDilation 0) 중에도 애니메이션이 계속 재생 ④ `SetAssetFromObject`·`Activate/Deactivate`를 게임 코드에서 호출 불가(private).
- 해결: ① `FInjectionSite()` 기본값은 거부된다. 자동 시스템의 사이트는 `/Script/UAFAnimGraph.AnimNextInjectionSiteTraitSharedData`의 `Graph` 변수 참조로 만든다(`TDCharacterAnimationComponent.cpp` `GetDefaultInjectionSite`). ② 엔진 결함(새 주입 그래프나 Uninject로 복귀한 원본 그래프의 첫 PreUpdate에서 자식 플레이어가 아직 OnBecomeRelevant 전). 개발 빌드에서 첫 발생 때 약 0.7초 멈춤. 코드로 회피 불가, 주입 테스트는 해당 줄만 `AddExpectedError`. 게임 동작 영향 없음. ③ UAF는 시간 배율을 무시한다 → 빙결은 `UUAFComponent::SetActive(false)`. ④ 기본 자산 교체는 `UnregisterComponent → SetAsset(TInstancedStruct) → RegisterComponent`, 교체마다 시스템이 새로 생기므로 Mover 틱 연결(`AddSubsequent`)을 다시 건다. 메시는 `SetEnableAnimation(false)` 필수이며, 그 결과 물리 바디가 포즈를 따라가지 않아 래그돌 전에 `UpdateKinematicBonesToAnim(GetComponentSpaceTransforms(), TeleportPhysics, true)`를 부른다. URO·애니 예산 할당기는 UAF에 없다(화면 밖 몬스터도 매 프레임 평가 — 후속 LOD 과제). UAF는 `UAnimSequence::RateScale`을 곱해 재생하므로 C++ 시간표 진행에도 곱한다. 메시를 `bEnableAnimation=false`로 두면 클로스도 돌지 않는다(쿠킹본 클로스 틱 4개 메시).
- 범위: `Source/TDGame/Characters/TDCharacterAnimationComponent.cpp`, UE 5.8.2 UAF·UAFAnimGraph(Experimental)
- 증거: `Saved/AgentOps/20260930/impl-A.md`·`impl-E.md`, 테스트 `TDGame.Movement.*`(루트모션 36.00cm, 빙결 중 0.000cm)
- 날짜·상태: 2026-09-30 active
- 발견: claude

### L-anim-08 Cascadeur Free(basic)는 FBX·DAE 내보내기와 UE Live Link가 막혀 있다 — UE 왕복은 Indie 이상
- 증상: `csc.app.get_application().is_export_available()` → `False`, `is_pro_features_available()` → `False`, 로그 `Actual license type: basic`. 파이프라인 내보내기 단계 결과 `{"exported": false, "reason": "Cascadeur license does not allow FBX export (Free/basic exports .casc only; Indie or higher is required)."}`.
- 해결: 무료 등급에서는 가져오기·퀵 리깅·키 쓰기·렌더·AI 모션 생성·.casc 저장까지만 자동화하고 내보내기 단계에서 멈춘다(`csc_export.py`가 먼저 검사). Python으로 관절 값을 읽어 FBX를 직접 만드는 것은 유료 기능 제한 우회라 하지 않는다. Indie(연매출 10만 달러 미만 상업 가능)·Pro 활성화 뒤 `--stages export,import`만 다시 돌린다. 무료 등급은 비상업 전용이다.
- 범위: Cascadeur 2026.2.3, `Tools/Cascadeur/`
- 증거: `Docs/Validation/Cascadeur/H-01-cascadeur-pipeline-2026-10-08.md`, cascadeur.com/plans(2026-10-08 확인)
- 날짜·상태: 2026-10-08 active
- 발견: claude

### L-anim-09 Cascadeur 내장 MCP 서버 자동 기동: 사용자 settings.json의 목록 키는 내장 목록을 "대체"한다
- 증상: 내장 MCP 스크립트 서버(`scripts/mcp`, 8765)는 Commands 메뉴의 `MCP.Start script server`를 눌러야 켜진다. 사용자 `%LOCALAPPDATA%/Nekki Limited/Cascadeur/settings.json`의 `Python.Events`에 `td_events`만 적으면 내장 `events`가 빠져 `scene_idle`이 안 돌고 `/run`이 "Timed out waiting for Cascadeur idle event"가 된다(정적 분석). 파일은 첫 실행 때 읽기 전용 속성으로 생긴다.
- 해결: `python Tools/Cascadeur/setup_cascadeur.py`가 읽기 전용을 풀고 원본을 `settings.json.td-backup`으로 1회 백업한 뒤 `Python.Path`에 `Tools/Cascadeur/csc_scripts`, `Python.Events`에 `["events", "td_events"]`를 적는다. `td_events/scene_idle/td_autostart_mcp.py`가 첫 scene_idle에서 `scripts.mcp.start_server.run(scene)`을 한 번 부른다. 반영은 Cascadeur 재시작 뒤(`cascadeur_bridge.py restart`). 사용자 패키지 이름은 내장(`commands`, `scripts`, `events` 등)과 겹치지 않게 `td_` 접두어.
- 범위: Cascadeur 2026.2.3(`presenter::SettingsJson`), Windows
- 증거: 재시작 뒤 `GET /health` → `{"ok": true, "version": "0.3.1", "idle_subscription": 0}`, `Saved/AgentOps/20261008/cascadeur-app-integration.md` 1.2·2.4절
- 날짜·상태: 2026-10-08 active
- 발견: claude

### L-anim-10 Cascadeur Python 키 쓰기·렌더 함정 6가지
- 증상: ① 마지막 프레임에 `scene_updater.run_update(...)` → 로그 `RuntimeError: flat_map::at key not found`, `modify_update`가 `False`, 애니메이션 길이 1로 되돌아감 ② 다음 키가 없는 마지막 구간의 `change_section(...).interval.interpolation`을 바꾸자 Cascadeur가 `domain::scene::getVelocities`에서 비정상 종료 ③ 타임라인 끝 표식 구간(`set_section(Section(), 마지막+1)`)에 값을 안 쓰면 그 키로 접선이 끌려 중간 프레임이 크게 튐(20cm 이동 키에서 60cm 넘게 솟음) ④ 같은 스크립트 안의 `set_current_frame` + `RenderToFile.take_image`는 프레임 번호만 바뀌고 자세는 이전 그대로, 그림은 비동기로 생성 ⑤ root 조인트 `local_position`은 FBX Z-up 부모 좌표라 (0,0,20)이 위로 20cm ⑥ 현재 탭이 빈 무제 탭이면 `DataSourceManager.load_scene`이 새 탭 없이 그 탭에 불러옴.
- 해결: ① 끝 표식은 마지막 키+1에 두고 그 프레임에는 마지막 키 값을 그대로 써서(유지 키) `run_update`는 건너뛴다 ② 마지막 구간은 보간을 바꾸지 않는다 ③ 위와 같음 ④ 프레임 설정과 렌더를 서로 다른 브리지 호출로 나누고 파일 생성을 기다린다(`csc_capture.py`) ⑤ 부모 회전 = root 전역 회전 × 로컬 회전ᵀ로 변환해 쓴다(`csc_motion.TDRootWriter`) ⑥ 탭 수가 아니라 경로로 확인. 탭을 닫을 때는 빈 새 탭을 먼저 현재 탭으로 만든 뒤 저장된 파이프라인 탭만 닫는다(미저장 탭이 있으면 정상 종료가 저장 대화상자에 막힌다).
- 범위: Cascadeur 2026.2.3 Python API(`csc.domain`, `csc.layers`, `csc.tools.RenderToFile`)
- 증거: `Saved/AgentOps/20261008/cascadeur-authoring-api.md` 4절 + 이 세션 로그(`cascadeur_log.log` 2026-10-08 11:09·11:26), `Tools/Cascadeur/csc_scripts/csc_motion.py`
- 날짜·상태: 2026-10-08 active
- 발견: claude

### L-anim-11 Cascadeur AI 인비트위닝: 보간 종류를 AI로 바꾸는 것만으로는 아무 일도 안 하고, 모션 생성 동작은 선택 구간의 키를 덮어쓴다
- 증상: 구간 보간을 `Interpolation.AI`(+문장 설명)로 바꾸고 자동 갱신(`InbetweeningTool.get_update_parameter()`=True)인 상태에서도 중간 프레임이 clamped Bezier와 완전히 같았다. `call_action("View.Inbetweening_RunMotionGeneration")`(25~44프레임 선택)은 무료 등급에서 51초 동안 돌며 로그 `Command: RootMotion / Root motion: done`을 남기고 동작을 새로 만들었으나 키 프레임까지 16~27cm 바꿔 경계 튐(칼끝 22 → 38cm/프레임)·끝 미정지(14cm/프레임)·발 미끄러짐(17.7cm/프레임)이 생겼다.
- 해결: 키가 촘촘한 전투 동작에는 쓰지 않는다. 키가 드문 요청에서만 명세 `ai_motion_generation: [{"frames": [a, b]}]`로 켜고, 구간 경계와 끝 자세를 `check_cascadeur_animation.py`로 다시 확인한다.
- 범위: Cascadeur 2026.2.3 basic, `tool_inbetweening.dll`
- 증거: `Docs/Validation/Cascadeur/H-01-cascadeur-pipeline-2026-10-08.md` '해석과 한계', 비교 수치(같은 키 Bezier 씬 대비)
- 날짜·상태: 2026-10-08 active
- 발견: claude

### L-anim-12 텍스트 → 키 포즈 풀이에서 칼 쥐기가 손목을 지배하고, 큰 회전 호는 키 보간으로 직선이 된다
- 증상: ① HandGrip_R+SM_Sword 쥐기는 칼날이 손 축과 약 100°라 칼날·날 방향만 정하면 손목 꺾임 110~150°, 아래팔 비틀림 ±173°처럼 불가능한 자세가 나온다 ② 감기(칼 오른쪽 뒤) → 타격(칼 왼쪽 앞) 사이 키 2프레임 간격·관절 구면 보간으로는 칼이 등 뒤로 도는 긴 호 대신 앞쪽으로 질러가고(방위각 108 → 62 → −43°) 칼끝이 59cm까지 떨어졌다 ③ 발 명세 병합에서 `pivot`이 위치 필드와 배타 묶음이 아니어서 이후 키의 새 발 위치가 무시됐다.
- 해결: ① `build_motion_keys.py`의 손목 인지 탐색(날 허용각 `edge_tolerance`, 손 위치 탐색 `position_search`, 팔꿈치 자동, 비틀림 75° 초과·팔꿈치 75° 미만 벌점)과 손목 보고를 쓴다. 백핸드 타격 순간 아래팔은 진행 방향(바깥)을 향하고 날은 아래팔 방향(손가락 마디 쪽)이다 ② 회전량이 큰 타격 구간은 매 프레임 키를 쓰고 칼 방위각을 회전 방향으로 단조 증가시키며 날은 `edge: "sweep_cw"`로 진행 방향을 향하게 한다 ③ `pivot`을 위치 배타 묶음에 넣었다.
- 범위: `Tools/Cascadeur/build_motion_keys.py`, UE5 Manny + SM_Sword 쥐기(L-anim-05)
- 증거: `Saved/Cascadeur/AS_TD_Player_SwordSpinSlash01/keys.solve.json`(최종 손목 꺾임 최대 66.6°·비틀림 75.3°), `check.json`(방위각 단조·정면 통과 19프레임)
- 날짜·상태: 2026-10-08 active
- 발견: claude
### L-anim-13 Cascadeur 리그 데이터 함정 2: 손가락 박스는 위치도 써야 하고, 공식 샘플의 clavicle_l `Position`은 0으로 읽힌다
- 증상: ① 손가락 박스(`index_01_Box_r` 등, FK 조작자)에 전역 `Rotation`만 키로 쓰자 손가락이 기준 자세 자리에 남아 손만 이동해 메시가 갈퀴처럼 늘어났다(첫 마디 5.9cm → 0프레임 31.5cm, 19프레임 63.3cm), 사용자 보고 "팔꿈치랑 손가락 등이 이상" ② 공식 샘플 `samples/UE5_Manny.casc`(사용자가 복사해 만든 `AnimationSources/Cascadeur/UE5_Manny.casc` 포함)에서 `data_viewer.get_data_id(clavicle_l, "Position")` 값이 모든 프레임 (0,0,0)인데 조작점·메시는 정상이다. 이 값을 기준 자세로 쓰면 왼쪽 쇄골 점이 140cm 지렛대로 계산돼 어깨가 틀어진다.
- 해결: ① FK 박스는 `Position`(= 관절 위치)과 `Rotation`을 함께 쓴다(`build_motion_keys.controller_values`, `csc_motion.write_keys`) ② 관절 전역 변환은 `Transform` 동작의 `global_position`·`global_rotation`으로 읽는다(`csc_inspect.read_global_transform`) ③ `check_cascadeur_animation.py`의 뼈 길이 유지 검사(`bone_stretch`, 허용 1cm, ik_*·interaction·center_of_mass 제외)와 손 근접 렌더로 확인한다. 전신 렌더·막대 인형만으로는 손가락 문제를 못 본다.
- 범위: Cascadeur 2026.2.3 UE5 퀵 리깅/공식 UE5_Manny 리그
- 증거: 수정 전 `bone_stretch`·근접 렌더(`Saved/Cascadeur/Diagnose/old_closeups/sheet.png`), 수정 후 최대 변화 0.07cm·`Saved/Cascadeur/AS_TD_Player_SwordSpinSlash01/closeups/hand-sheet.png`
- 날짜·상태: 2026-10-08 active
- 발견: claude(사용자 시각 확인에서 시작)
### L-anim-14 기존 검사를 모두 통과한 Cascadeur v2에 팔꿈치 몸통 관통·위팔 비틀림·발목 교차가 있었다 — 검사기가 해부학 항목을 재지 않는다
- 증상: 사용자 "팔꿈치가 몸통을 파고들고 다리가 꼬인다". `AS_TD_Player_SwordSpinSlash01` v2는 `check_cascadeur_animation.py` 전 항목(손목 29.5°, 뼈 길이 0.07cm, 미끄러짐 ≤0.95cm/프레임)을 통과했다. 같은 명세를 다시 풀어 재 보니 오른팔꿈치–몸통 캡슐 근사 −7~−10cm(0~3·34~44프레임), 왼 위팔 축 비틀림 140~177°(17~24), 골반 기준 발목 교차 28cm(22~32, 무릎은 교차 안 함)였다.
- 원인: 에이전트가 명세에 골반·척추·발 요, 손 목표, `elbow_body`를 몸 모형 없이 썼다(16프레임 손 목표 56.5cm/프레임 점프, 0키 `elbow_body`가 `merge_component`로 끝까지 잔존, `*_body`가 가슴이 아니라 facing 기준으로 풀림). `build_motion_keys.py:280-298` 팔꿈치 폴 비용에 몸통 거리·연속 항이 없다. 검사기에 캡슐 관통·스윙-트위스트 가동 범위·골반 기준 다리 순서·폴 튐 항목이 없다. Cascadeur 쓰기 경로(`csc_motion.py:184`)에는 쿼터니언 반구 정렬도 없다(Blender 작성기에는 있음).
- 해결(미구현·제안): 검출 전용 해부학 게이트(캡슐 부호거리, 스윙-트위스트, 골반 좌표 다리 순서, 프레임당 폴 방향 변화, 쿼터니언 반구)를 구운 모든 프레임에 걸고 최악 프레임으로 판정한다. 에이전트 자체 PASS를 품질 근거로 쓰지 않는다. 상세 `Docs/AIAnimation_AgentPractice_Research.md` 1·5·7절.
- 범위: `Tools/Cascadeur/build_motion_keys.py`, `check_cascadeur_animation.py`, UE5 Manny. Blender 절차적 경로의 사용자 보고 증상은 미측정(문서 7.4절)
- 증거: `Saved/AgentOps/20261011/aianim-scratch/measure-result.json`, `aianim-codebase-audit.md`
- 날짜·상태: 2026-10-12 active(해결 미구현)
- 발견: claude(사용자 보고에서 시작)

### L-anim-15 UAF 기본 자산으로 도는 대기는 시퀀스 bLoop을 따르고, 대기를 주입으로 돌리면 `Event.SerialNumber != 0` ensure가 난다
- 증상: ① 몬스터 대기가 한 번 재생된 뒤 마지막 포즈로 멈춘다(시험 `TDGame.Movement.MonsterIdleKeepsLooping`: 첫 주기 7.36cm 움직임 → 1.5주기 뒤 0.000cm). 시스템 공개 변수(`FSequencePlayerData`)의 `LoopMode`를 `ForceLoop`로 바꿔도 그대로다. ② 대기를 걷기·달리기처럼 `ForceLoop` 주입으로 바꾸면 반복은 되지만 `LogOutputDevice: Error: Ensure condition failed: Event.SerialNumber != 0 [File:...\UAFAnimGraph\Private\Injection\InjectionSiteTrait.cpp] [Line: 88]`가 오류로 남아 시험이 실패한다.
- 원인: ① 실제 대기를 재생하는 그래프는 주입 지점이 `Graph.Asset`(UObject)의 기본값으로 따로 만들기 때문에, 반복 여부가 시퀀스 자산의 `bLoop`(Auto 루프 모드)을 따른다. 쓰던 대기 시퀀스 4개(MM_Idle·AS_TD_Goblin_Idle·Skeleton_Guard·Stone_Golem ThirdPersonIdle)가 모두 `bLoop=False`였다. ② 미검증(모듈 주입 컴포넌트를 거치지 않은 주입 이벤트로 추정).
- 해결: 대기는 기본 자산 그대로 두고 쓰는 시퀀스의 `loop`를 켠다(`Tools/MonsterAI/editor_make_loop_clips.py`가 종 자산 전부를 읽어 켬, 재실행 안전). 종 시험 `TDGame.MonsterAI.Species.LocomotionClipsLoop`이 대기 bLoop을 단언한다. 이동 클립은 C++이 `StartPosition 0 + ForceLoop`로 자산 전체를 주입하므로 종 자산의 Start/End를 쓰지 말고 반복 구간만 담은 자산을 쓴다(UAF 5.8 `FPlayAnimArgs`에 끝 위치 없음).
- 범위: `Source/TDGame/Characters/TDCharacterAnimationComponent.cpp`(ApplyBaseAsset·UpdateLocomotionClips), UE 5.8.2 UAFAnimGraph(Experimental)
- 증거: `Saved/Logs/M326_Before2.log`·`M326_After2.log`(2769줄)·`M326_After4.log`, 시도 패치 `Saved/AgentOps/20261016/m3-26/idle_injection_attempt.patch`
- 날짜·상태: 2026-10-16 active
- 발견: claude

### L-anim-17 UAF로 포즈를 쓰는 메시를 래그돌로 바꾸면 물리는 도는데 화면 포즈가 굳어 한 덩어리로 미끄러진다
- 증상: 사망 래그돌에서 몬스터 메시가 마지막 애니 포즈 그대로 날아가거나 미끄러진다. PIE 측정 골반은 203cm 이동, 골반 대비 손·머리 위치 변화 1e-13cm(포즈 고정)
- 원인: 물리 블렌드(`PerformBlendPhysicsBones`)는 편집 버퍼에 쓰고 `FinalizeBoneTransform → FlipEditableSpaceBases`가 `bNeedToFlipSpaceBaseBuffers`일 때만 읽기 버퍼로 넘긴다. 그 플래그는 애님BP 경로(`SkeletalMeshComponent.cpp:3327·3798`)에서만 서서, `SetEnableAnimation(false)`+UAF 메시는 래그돌 결과가 렌더·`GetBoneLocation`에 안 보인다(`PhysAnim.cpp:420-460`, `SkinnedMeshComponent.cpp:4900`)
- 해결: 래그돌 시작 때 `SetAllBodiesSimulatePhysics` 전에 `MeshComponent->SetComponentSpaceTransformsDoubleBuffering(false)`(`ATDCombatCharacter::StartRagdoll`). 사망 래그돌은 되돌리지 않으므로 복원 불필요. 손가락처럼 바디 없는 뼈는 참조 포즈로 돌아간다
- 범위: UE 5.8 UAF + `bEnableAnimation=false` 스켈레탈 메시의 물리 시뮬레이션·물리 블렌드 전부
- 증거: 2026-10-18 `Saved/AgentOps/20261018/combat-full/pie/combat-pie.json`(수정 전 pose_max_change 0.0 → 수정 후 46~57cm), `Docs/MonsterAI_CombatSim/measurements/M3-27/02_monster_ragdoll.png`
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-anim-18 런타임에 스폰한 캐릭터의 첫 주입에서 `Event.SerialNumber != 0`(InjectionSiteTrait.cpp:88) ensure가 난다
- 증상: PIE 시험 중 몬스터를 스폰한 다음 프레임 `Ensure condition failed: Event.SerialNumber != 0 [...UAFAnimGraph\Private\Injection\InjectionSiteTrait.cpp] [Line: 88]`(콜스택 OnInjectEvent ← RaiseTraitEvents ← UpdateGraph ← FUAFSimplePrePhysicsGraphComponent::PrePhysics). 자동화 시험이 Fail, 개발 빌드는 첫 발생 때 멈칫
- 원인: `ApplyBaseAsset`(Unregister→SetAsset→Register)로 새 UAF 시스템이 생긴 뒤 첫 PrePhysics 전에 큐에 든 주입(이동·액션)은 주입 컴포넌트가 아직 없어 번호 0으로 도달한다(`AnimNextModuleInjectionComponent.cpp:80`, `ModuleTickFunction.cpp:220`). L-anim-15(대기 ForceLoop 주입)와 같은 줄, 다른 경로
- 해결: `UTDCharacterAnimationComponent`가 `ApplyBaseAsset` 뒤 자기 틱 2회 동안 이동 주입과 PlayAction·PlayDeath 주입을 보류(마지막 요청 1개 보관, 반환 길이는 정상)했다가 내보낸다. 수정 뒤 PIE 6회 :88 0건. `:268`(L-anim-07)은 여전히 프로세스 첫 주입에 1회 나서 PIE 시험은 그 줄만 `AddExpectedError`
- 범위: UE 5.8.2 UAF, 런타임 스폰·기본 자산 교체 직후 주입
- 증거: `Saved/AgentOps/20261018/combat-full/pie/editor-pie-run1.log`(4065줄), `review-code/uaf_ensure.md`
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-anim-19 루트 뼈가 Hips인 Mixamo 스켈레톤으로 리타깃한 클립에 루트 고정을 켜면 엉덩이가 굳어 사망 클립이 쓰러지지 않는다
- 증상: Manny → `SKM_Man`(Mixamo, `Skeleton_Man`) 리타깃 결과 검사에서 `pelvis_z 0.0`, `foot_*_buried`·`head_below_pelvis` 플래그가 모든 시각에 뜬다. `force_root_lock`을 켠 사망 클립은 엉덩이가 서 있는 높이에 고정되어 상체만 접힌다(관찰: 리타깃 검사 수치, PIE 육안은 `measurements/monsterize/pie-2026-10-18/` 참조)
- 원인: Mixamo 스켈레톤은 뼈 0번이 `Hips`라 `force_root_lock`이 골반의 이동·회전을 기준 포즈로 잠근다. Manny 클립의 전진(루트 모션)도 리타기터가 Hips로 옮기므로 잠그지 않으면 걷기·달리기가 한 주기마다 앞으로 갔다 되돌아간다
- 해결: 이동·피격 클립은 잠금 유지(엉덩이 흔들림이 사라지는 제한을 감수), 사망만 잠그지 않는다 — 종 표 리타깃 대상 키 `unlocked_clips: ["Death"]`(`Tools/MonsterAI/editor_retarget_monster_anims.py`). Mixamo 자체 공격은 루트가 움직이지 않는 제자리판(`A_NS_*`)을 쓰고 `A_Combo_*`(Hips 235cm 이동)는 쓰지 않는다. 근본 해결은 Hips 위에 root 뼈를 넣어 다시 가져오기(Blender)
- 범위: UE 5.8.2 IK 리타기터 일괄 리타깃, 루트 뼈 없는 Mixamo 메시
- 증거: 2026-10-18 리타깃 로그(`check /Game/MonsterAI/Animations/Swordsman/AS_TD_Swordsman_*`), `Saved/MonsterAI/hand_peaks.json`(`A_NS_01:root = Hips`), `Saved/MonsterAI/species_measure.json`(A_Combo root_travel 234.9cm)
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-anim-20 재생 중인 캐릭터에 `UseLocomotionClips`를 부르면(UAF 시스템 해제·재등록) 같은 프레임의 워커 UAF 틱이 사라진 모듈을 참조해 에디터가 크래시한다
- 증상: PIE에서 켄타우로스 사냥꾼이 페이즈 1에 들어간 직후 `Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000178`, 콜스택 `TSharedFromThis<FAnimNextModuleInstance,1>::AsShared() ← FAnimNextModuleInstance::GetReference() ← FBlendStackCoreTrait::OnBlendInitiated() ← ... FInjectionSiteTrait::PreUpdate() ← FUAFSimplePrePhysicsGraphComponent::PrePhysics()`(Foreground Worker 스레드).
- 원인: `UTDCharacterAnimationComponent::UseLocomotionClips`는 BeginPlay 이후면 `ApplyBaseAsset`으로 UAF 컴포넌트를 Unregister → SetAsset → Register 한다. 몬스터 생각 단계(페이즈 진입 알림)에서 이를 부르면 이미 예약·실행 중인 PrePhysics UAF 틱이 옛 모듈 인스턴스를 본다. 시작 때(BeginPlay)에는 틱이 없어 안전하다.
- 해결: 런타임 교체는 시스템을 다시 만들지 않는 `ReplaceWalkAndRunClips`(걷기·달리기 주입 클립과 기준 속도만 바꾸고 현재 이동 자세를 None으로 돌려 다음 갱신에 다시 주입)를 쓴다. 대기(기준 자산) 교체는 런타임에 하지 않는다. 회귀 시험 `TDGame.MonsterAI.MidBoss.CentaurSpeciesSwapsToSwordWhileAnimating`(실제 종 에셋으로 페이즈 전환 — 헤드리스에서 옛 코드가 크래시하는지는 미확인).
- 범위: `UseLocomotionClips`·`ApplyBaseAsset`을 BeginPlay 이후에 부르는 모든 경로(페이즈 외형, 장비 교체, 형태 변환)
- 증거: 2026-10-18 `Saved/Logs/TDGame-backup-2026.10.18-09.34.25.log`(크래시 콜스택, UTC 기준 이름), 수정 뒤 PIE `Docs/MonsterAI_CombatSim/measurements/M3-34/pie-20261018/report.json` 전 단계 통과
- 날짜·상태: 2026-10-18 active
- 발견: claude

### L-anim-21 래그돌이 바닥에서 계속 꿈틀대며 잠들지 않고, 일부 종은 시작 순간 팔다리가 튄다
- 증상: PIE 측정(`pie_measure_ragdoll_jitter.py` 기준선)에서 12종 대부분이 가라앉기 전까지 깨어 있음(깨어 있는 비율 1.0), 정착 창 위치 RMS Goblin 1.0~1.3·StoneGolem 1.7~4.1·Wukong 5.6~27.5cm, 최대 각속도 3801도/초. 래그돌 첫 스텝에 말단 바디가 1300~2300cm/s·5000~7800도/초로 튐
- 원인: ① 팩 물리 에셋 바디의 `AngularDamping` 기본 0과 엔진 기본 잠들기 문턱(선 1cm/s·각 0.05rad/s·4틱, 섬 단위)이라 미세한 잔여 회전이 섬 전체를 깨워 둔다 ② StoneGolem(손·발이 62×89cm급 상자로 서로 겹침)·Paragon `*_Extents`(손·발·머리 바디가 골반에 회전 자유 관절로 직결, 경계 계산용)처럼 래그돌용이 아닌 물리 에셋은 사망 포즈에서 겹침·한계 위반을 한 스텝에 풀며 튄다
- 해결: ① 래그돌 시작 때 바디 `AngularDamping` 1.0 + 정착 재우기(래그돌 1초 뒤 질량 가중 평균 속도 15cm/s 이하가 0.3초 이어지면 `PutAllRigidBodiesToSleep`, 가라앉기 시작 때 중지) — `ATDCombatCharacter::UpdateRagdollSettle` ② 불량 팩 에셋은 `SkeletalMeshEditorSubsystem.create_physics_asset`로 메시에서 다시 만들어 종 에셋 `RagdollPhysicsAsset`으로 교체(`Tools/MonsterAI/editor_make_ragdoll_physics_assets.py`, 팩 원본 유지). 메모리 실험에서 생성 에셋으로 StoneGolem 첫 속도 2000→750cm/s·잠들기 1.7~1.9초, Wukong 1.5초
- 범위: UE 5.8 Chaos 전신 래그돌(UAF 메시), 마켓 팩 자동 생성·경계용 물리 에셋
- 증거: `Docs/MonsterAI_CombatSim/measurements/M3-38/`(baseline·exp1·exp3·exp4·exp6·exp7 report.json), 엔진 근거 `Saved/AgentOps/20261018/ragdoll-jitter/engine-research.md`(IslandManager.cpp:2752-2756, BodyInstance.cpp:367-411)
- 날짜·상태: 2026-10-19 active(빌드 뒤 최종 측정은 M3-38 기록)
- 발견: claude

### L-anim-22 보스 처치 슬로모(전역 시간 0.2배) 중 래그돌이 시작되면 몸이 튀고, 팩 물리 에셋은 3~4m 던져진 뒤 끝없이 떤다
- 증상: 비행 중 처치한 GriffonAlpha·MountainDragon(보스라 처치 0.05초 뒤 0.2배 슬로모)에서 래그돌 시작 순간 최대 바디 속도가 슬로모 없을 때의 약 3배(GriffonAlpha 팩 에셋 2100~2400 대 700~780cm/s, 생성 에셋 1330~1600 대 611). 질량 중심 속도는 처치 속도(410·160cm/s) 그대로인데 내부 속도만 프레임마다 커짐(1952→3435cm/s). 팩 에셋(SK_Griffon_Physics)은 루트 바디가 +265~390cm 튀고 착지 뒤 꼬리·허벅지가 230~420cm/s로 6초까지 떨며 평균 26cm/s로 기어가 잠들지 않음(3회 중 2회)
- 원인(정황): 래그돌 첫 단계들의 관절 위치 보정이 Δx/dt 속도가 되는데, 시간 늘이기 0.2배면 dt가 5배 작아 같은 보정이 5배 속도가 된다. 그 에너지가 바닥 접촉에서 전신 운동으로 바뀐다. 팩 에셋은 사망 자세에서 관절 위반이 커 증폭이 더 크다
- 해결: 불량 팩 물리 에셋을 생성 에셋으로 교체(`Tools/MonsterAI/editor_make_ragdoll_physics_assets.py`, M3-39 Griffon·GriffonAlpha·MountainDragon) → 끝없는 떨림 사라짐, 3회 모두 정착. 남은 증폭(루트 +160~180cm, 슬로모 없으면 +33~37)은 진입 시점 문제라 M3-40(사망 시작 경로)에 넘김. 효과 없음: 래그돌 첫 0.4초 매 프레임 질량 중심 대비 상대 속도 600cm/s 제한(흉내), 바디 최대 각속도 720도/초(메모리 재정의) — 튐은 물리 단계 안에서 생긴다
- 범위: UE 5.8 Chaos 전신 래그돌, 전역 시간 늘이기 1 미만에서 시작하는 래그돌(M3-37 보스 처치·승리·페이즈 슬로모), 서브스텝 꺼짐
- 증거: `Docs/MonsterAI_CombatSim/measurements/M3-39/`(flight·flight-noslowmo·gen-flight·gen-flight-noslowmo·emu-flight·emu-angcap-flight report.json), 원본 `Saved/AgentOps/20261019/ragdoll-m3-39/`
- 날짜·상태: 2026-10-19 부분 해결(물리 에셋), 증폭 자체는 미해결(M3-40)
- 발견: claude

### L-anim-23 애니메이션을 끈 스켈레탈 메시의 래그돌은 물리 자세가 화면에 그려지지 않아, 처치 순간 자세가 골반에 붙은 채 굳어 보이다가 1.5초쯤 한꺼번에 바뀐다
- 증상: M3-40 근접 시트에서 0.6초와 1.0초 사진이 거의 같다가 1.6초에 자세가 바뀌었다(스톤골렘 '앉은 자세', 켄타우로스 상체 '늦게 펼쳐짐', 바게스트 '다리가 하늘'). 그러나 게임 스레드 뼈는 물리를 그대로 따랐다(소켓과 바디 COM 어긋남 0). 사진은 '처치 자세를 골반 바디에 강체로 붙인 자세'와 맞았다(실루엣 Dice).
- 원인: 뼈 개정 번호는 `FlipEditableSpaceBases`가 `bNeedToFlipSpaceBaseBuffers`일 때만 올린다(SkinnedMeshComponent.cpp 4900-4949). 이 플래그는 애니메이션 평가(`SetEnableAnimation(false)`라 꺼짐)와 UAF 출력(처치 정지·`HoldPoseForDeath`로 `SetActive(false)`)만 켜고, 물리 블렌드 경로(PhysAnim.cpp 420-523)는 켜지 않는다. GPU 스킨 캐시는 (뼈 버퍼, 개정 번호)가 같으면 이전 스키닝을 재사용한다(GPUSkinCache.cpp 1639-1663). 그래서 컴포넌트만 골반을 따라간다. 히트 플래시 오버레이 해제(`SetOverlayMaterial`→렌더 상태 재생성)가 처치 + 약 1.5초에 한 번 다시 스키닝하는 것으로 추정한다.
- 해결: 래그돌을 시작할 때 `RegisterOnBoneTransformsFinalizedDelegate`로 매 프레임 `Mesh->ForceMotionVector()`를 부른다. 보일 때만 부르고, 잠든 뒤 1회 더 부르며, `EndPlay`에서 해제한다(`ATDCombatCharacter::PushRagdollPoseToRenderer`, M3-40). 검증: 깨어 있는 프레임 3517/3517에서 개정 번호가 늘었고, 굳은 자세 판정(RIGID)은 0/36이다. 헤드리스는 `--rhi offscreen`으로 `TDGame.Combat.DeathRagdoll.World.PushesPose*`를 돌린다.
- 범위: UE 5.8, 애니메이션을 끈 메시의 물리 블렌드 래그돌(몬스터·플레이어·추종 메시). M3-38·M3-39의 육안 판정 일부는 이 착시였을 수 있다.
- 증거: 2026-10-19 `Saved/AgentOps/20261019/death-ragdoll/investigate6/posture.md`(사건표·Dice·겹침 그림), `Docs/MonsterAI_CombatSim/measurements/M3-40/r3-sheet`(수정 전)와 `r4-sheet`(수정 뒤), `Saved/AgentOps/20261019/m340-int4-rhi-tests.json`
- 날짜·상태: 2026-10-19 active
- 발견: claude

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

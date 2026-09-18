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

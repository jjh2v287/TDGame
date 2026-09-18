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

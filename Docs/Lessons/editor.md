# Lessons — editor (에디터 파이썬·MCP)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A.

### L-editor-01 `delete_all_material_expressions`는 커스텀 출력 노드를 안 지운다
- 증상: 머티리얼을 재구성해도 `LandscapeGrassOutput` 같은 커스텀 출력 노드가 남는다.
- 해결: 머티리얼은 삭제 후 재생성한다(`Tools/WorldGen/editor_make_landscape_material.py`).
- 범위: UE 5.8 `MaterialEditingLibrary`
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-02 에디터 파이썬에 없는 API 목록
- 증상: `Actor.add_component_by_class`, `set_is_spatially_loaded`, 랜드스케이프 생성, `LandscapeLayerInfoObject.layer_name` 설정이 파이썬에서 안 된다.
- 해결: `set_is_spatially_loaded` → `set_editor_property("is_spatially_loaded", …)`; 랜드스케이프 생성 → C++ `UTDLandscapeEditorLibrary`; 그 외는 C++ 에디터 함수(OP-20).
- 범위: UE 5.8 에디터 파이썬 3.11
- 증거: Tools/WorldGen/editor_build_open_world.py가 C++ 경로를 사용 — 미검증(별도 증거 없음)
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-03 화면 캡처는 `EditorAppToolset.CaptureViewport`만 신뢰한다
- 증상: `HighResShot`·`CaptureEditorImage`가 갱신되지 않은 화면을 찍는다.
- 해결: MCP `EditorAppToolset.CaptureViewport`에 `captureTransform`을 지정해 찍는다(`Tools/WorldGen/capture_views_mcp.py`).
- 범위: UE 5.8 MCP 플러그인
- 증거: Docs/Validation/ashen-vale/ 캡처가 이 경로로 생성됨
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-04 랜드스케이프 머티리얼 재생성 뒤 검게/체커로 보인다
- 증상: 머티리얼 스크립트를 다시 돌리면 랜드스케이프가 검게 또는 체커 무늬로 보인다.
- 원인: 스크립트가 `landscape_material`을 다시 지정해 셰이더가 재컴파일된다.
- 해결: 셰이더 컴파일 2~4분 뒤에 캡처한다.
- 범위: UE 5.8, Tools/WorldGen/editor_make_landscape_material.py
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-editor-05 PCG 그래프 파이썬 스크립팅 실측
- 증상: 선택자 `set_attribute_name` 무시, 점 데이터로 bounds 제한 불가, 그래프 사용자 파라미터를 파이썬에서 못 만듦 등.
- 해결: 본문은 `Tools/WorldGen/README.md` 'PCG 그래프 스크립팅 실측' 절(정본, 이동하지 않음).
- 범위: UE 5.8 PCG, P3-07
- 증거: Docs/Validation/P3-07-pcg-biome.md
- 날짜·상태: 2026-09-18 active
- 발견: claude

# 에이전트 도구 계층·던전 생성기 검증 (2026-09-12)

## 도구 계층 (`Tools/README.md`, `GEMINI.md`)
- `python Tools/ue_editor.py status` → 에디터 실행·MCP 포트 8000·Python 원격 실행 True 확인.
- `python Tools/run_in_editor.py -c "import unreal; print(unreal.SystemLibrary.get_project_directory())"` → SUCCESS.
- `python Tools/uemcp.py call call_tool '{"toolset_name":"editor_toolset.toolsets.scene.SceneTools","tool_name":"get_current_level","arguments":{}}'` → `/Game/Level/LV_DarkFantasy_OpenWorld`.
- `python Tools/run_in_editor.py Tools/editor_inspect_level.py` → 액터 3,046개 히스토그램 JSON(`Saved/Inspect/LV_DarkFantasy_OpenWorld.json`).
- 제미나이 연결: `.gemini/settings.json`의 `unreal-mcp`(http://127.0.0.1:8000/mcp) + 루트 `GEMINI.md`(작업 지침·도구 카탈로그·도구 생성 규칙).

## 월드 검증기·시드 선택 (`Tools/WorldGen/validate_world.py`, `select_seed.py`)
- 첫 실행 71.9점: 부두가 도로에서 41m(기준 40m), 뼈 구덩이가 강 위(−3.2m), 던전 슬롯 간격 200m < 로딩 반경 합 256m, 무콘텐츠 도로 3구간(최장 275m).
- 수정: 부두 (30,−8)m, 뼈 구덩이 (145,310)m, 슬롯 간격 300m, 길제단 POI 3곳, 전투 공터는 `Arena` 마커로 분리 → **100/100 통과**(입구 간격 663m, POI 11개·최소 간격 160m, 도로 도달 17/17, 지형 18/18, 무콘텐츠 최장 36m).
- 레벨 재베이크·저장(캡처 `wayshrine_south.jpg`, `bonepit.jpg`).

## 던전 생성기 (`Tools/DungeonGen/`)
- 흐름 5종 × 크기 3종 × 시드 1~100 = 1,500건 생성·검증 통과율 100%, 같은 시드 해시 동일.
- 슬롯 3개 베이크(`editor_build_dungeon.py`): slot0 KeyLock Medium seed7(방 15, 바닥 셀 66, 벽 110, 소품 50, 조명 8), slot1 Branch Small seed3, slot2 Loop Medium seed5. 저장 완료.
- 캡처: `dungeon_slot0.jpg`(전체), `dungeon_slot0_boss.jpg`(보스 방·잠긴 문·붉은 빛). 아틀라스 원점 (300000+K×30000, 300000)cm, 각 슬롯 NavMeshBoundsVolume 포함.

## 남은 것
- C++ 이식(`FTDWorldValidator`, `FTDDungeonFlowGenerator/LayoutSolver/Validator/Baker`), 정의 데이터 에셋(P3-01·P2-02), 실제 룸 모듈 아트(P2-03), 에디터 버튼 UI(P2-08·P3-08), 심리스 입구 액터(P1-03~05).

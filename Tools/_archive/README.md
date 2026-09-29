# Tools/_archive — 폐기·보관 도구

규칙: `Docs/AgentRules.md` OP-21. 보관된 도구는 실행하지 않으며 검색(OP-12)에서도 제외한다(`--glob '!Tools/_archive/**'`). 다시 쓰려면 이름 규칙(OP-14)·docstring 4줄(OP-15)에 맞춰 `Tools/<영역>/`으로 승격하고 `Tools/README.md`에 등록한다.

| 원래 경로 | 보관 위치 | 이유 | 대체 | 날짜·결정 |
|---|---|---|---|---|
| `Tools/Animation/` (11개) | `2026-09/Animation/` | Kimodo text-to-motion → BVH → Blender → UnrealEditor-Cmd 실험 파이프라인. 어느 문서에서도 참조되지 않고 현 정책(text-to-motion 서비스 미가정, `Tools/BlenderAnimation/SKILL.md`)과 모순 | `Tools/BlenderAnimation/`, `Tools/AnimationAuthoring/` | 2026-09-18 D-15 |
| `.gemini/scripts/` (13개) | `2026-09/gemini-scripts/` | `unreal_mcp.py`는 `Tools/uemcp.py`와, `take_screenshot.py`는 `Tools/WorldGen/capture_views_mcp.py`와 중복. `build_*_world.py`·`populate_*.py`·`setup_real_landscape.py`는 현행 월드 생성 파이프라인(`Tools/WorldGen/`, 레벨을 통째로 재생성) 이전 방식이라 같은 레벨에 직접 액터를 심어 결과와 충돌. `check_editor_connection.py`는 `ue_editor.py status`와 중복 | `Tools/uemcp.py`, `Tools/WorldGen/*`, `Tools/ue_editor.py` | 2026-09-18 D-16 |
| `Tools/BlenderAnimation/` 몽타주 의존 6개(`create_player_montage.py`, `editor_pie_capture_sword_slash.py`, `import_sword_slash.py`, `ue_import_attack02.py`, `validate_weighty_pie.py`, `validate_sword_slash_pie.py`) | `2026-09/BlenderAnimation/` | 삭제된 옛 공격(SwordSlash·Attack02·weighty)의 몽타주 생성·AnimInstance 재생 경로. 몽타주·AnimBP 폐기(D44)로 실행 불가 | `Tools/BlenderAnimation/import_sword_attack01.py`, `validate_sword_attack01_pie.py`, `Tools/Movement/pie_check_player_movement.py` | 2026-09-30 D44(M3-16) |

Claude 홈 `~/.claude/projects/C--Project-TDGame/tools/` 14개는 저장소 밖이라 여기 두지 않는다. 재사용 가치가 있던 `run_tests.py`만 `Tools/check_automation_tests.py`로 이관했고 나머지 일회용(문서 분할·폴더 정리·PIE 1회 검사)은 홈에 남겼다(2026-09-18 D-17).

검증 증거 중 GAS 전환 전(2026-09-08 이전) 기록 10개는 `Docs/Validation/_archive/pre-gas/`에 있다(2026-09-18 D-23).

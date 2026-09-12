# Tools — AI 에이전트(제미나이·Claude·Codex 공용) 언리얼 에디터 작업 도구

모든 도구는 `C:\Project\TDGame`에서 `python Tools/<파일>`로 실행한다(시스템 Python 3.12, numpy·scipy·PIL 설치됨).
에디터 안에서 도는 스크립트는 파일명이 `editor_*.py`이며 반드시 `run_in_editor.py`로 실행한다.
규칙은 `AGENTS.md`(로직은 C++, 이름 접두어 `TD`, 커밋은 사용자 지시 시에만).

## 0. 시작 전 점검 (매 세션)
```bash
python Tools/ue_editor.py ensure     # 에디터 실행 + MCP 포트 8000 + Python 원격 실행 확인/자동 복구
```

## 1. 범용 도구 (언리얼의 모든 기능에 접근하는 세 갈래)
| 도구 | 용도 | 예 |
|---|---|---|
| `run_in_editor.py` | 에디터 안에서 임의 Python(`unreal` 모듈 전체) 실행. 에셋 생성·레벨 편집·설정·저장·PIE 조회 등 **에디터 Python API 전부** | `python Tools/run_in_editor.py -c "import unreal; print(unreal.EditorAssetLibrary.list_assets('/Game/World'))"` |
| `uemcp.py` | 공식 MCP 서버를 셸에서 직접 호출(툴셋 목록·설명·호출). 액터 배치·프로퍼티·PIE·자동화 테스트·설정·뷰포트 캡처·UI 자동화 | `python Tools/uemcp.py call describe_toolset '{"toolset_name":"EditorToolset.EditorAppToolset"}'` |
| `ue_editor.py` | 에디터 상태/시작/종료/빌드/재시작, Python 원격 실행 on/off | `python Tools/ue_editor.py restart` |
| `editor_inspect_level.py` | 현재 레벨 액터 요약 JSON(`Saved/Inspect/`) | `python Tools/run_in_editor.py Tools/editor_inspect_level.py` |
| `templates/` | 새 도구 템플릿(에디터 Python, C++ 에디터 함수 절차) | 복사해서 시작 |

MCP 툴셋 이름은 `python Tools/uemcp.py call list_toolsets '{}'`로 본다. 자주 쓰는 것: `editor_toolset.toolsets.scene.SceneTools`(액터 스폰·검색), `...object.ObjectTools`(프로퍼티), `...asset.AssetTools`, `EditorToolset.EditorAppToolset`(StartPIE/StopPIE/CaptureViewport/SetCameraTransform), `AutomationTestToolset.AutomationTestToolset`, `ConfigSettingsToolset.ConfigSettingsToolset`, `EditorToolset.LogsToolset`(GetLogEntries), `SlateInspectorToolset`(UI 자동화).

## 2. 월드 생성 도구 (`WorldGen/`, 설계서 4·5·11·12장)
| 도구 | 용도 |
|---|---|
| `WorldGen/generate_ashen_vale.py --seed N [--out 폴더]` | 메인 지역 높이맵·레이어·배치 생성(에디터 밖) → `Saved/WorldGen/AshenVale/` |
| `WorldGen/editor_build_open_world.py` | 레벨에 베이크(랜드스케이프·대기·인스턴스·액터·조명·마커·저장) |
| `WorldGen/editor_make_landscape_material.py` | 레이어 머티리얼(텍스처 봄빙)·풀 타입 재생성 |
| `WorldGen/editor_relight.py` + `Saved/WorldGen/look.json` | 재빌드 없이 조명·노출·안개 조정 |
| `WorldGen/validate_world.py` | 월드 검증(입구 간격·POI 밀도·도로 도달성·지형 적합성·스트리밍·플레이 밀도) → report.md |
| `WorldGen/select_seed.py --seeds 1-5` | 시드 후보 생성·검증·점수·추천 |
| `WorldGen/capture_views_mcp.py 이름=x,y,pitch,yaw,dist` | 지점별 뷰포트 캡처 PNG |
| `WorldGen/pie_check.py` | PIE 중 폰 위치·지면·내비메시 확인 |
| `WorldGen/editor_make_pcg_biome.py` | P3-07 Forest 바이옴 PCG 그래프(메인+서브 5)·룸 드레싱 그래프 생성, `TDGen_PCG_BiomeForest` PCGVolume(파티션+HiGen) 배치·생성 |
| `WorldGen/editor_check_pcg_determinism.py` | PCG 같은 시드 cleanup→generate 2회, ISM 수·위치 해시 비교 → `Saved/WorldGen/pcg_determinism.json` |
| `WorldGen/rebuild_all.sh [--material] [--seed N] [--wait 초] [캡처...]` | 전체 파이프라인 한 줄 |
자세한 규격: `WorldGen/README.md`.

## 3. 던전 생성 도구 (`DungeonGen/`, 설계서 6·7·11.1장)
| 도구 | 용도 |
|---|---|
| `DungeonGen/generate_dungeon.py --theme Crypt --flow Branch --size Medium --seed N --slot K` | 흐름 그래프 → 룸 모듈 격자 조립 → layout.json + preview.png |
| `DungeonGen/validate_dungeon.py <layout.json>` | 연결성·필수 룸·충돌·길이·분기·Key/Lock 검증 → report.md |
| `DungeonGen/batch_dungeons.py --seeds 1-20 --flow ... --size ...` | 후보 N개 생성·검증·점수·추천 |
| `DungeonGen/editor_build_dungeon.py` | 레이아웃을 던전 아틀라스 슬롯에 임시 지오메트리로 베이크 |
자세한 규격: `DungeonGen/README.md`.


## 3b. C++ 도구 (에디터 Python/블루프린트에서 호출)
| 호출 | 용도 |
|---|---|
| `unreal.TDWorldGenEditorLibrary.generate_and_bake_dungeon(world, theme, flow, size, seed, atlas, slot, dungeon_id)` | C++ 흐름 생성→솔버→검증→슬롯 베이크(+아틀라스 슬롯 정의 갱신) |
| `...generate_dungeon_layout / bake_dungeon_to_slot / export_dungeon_layout_json` | 단계별 호출 |
| `...generate_world_layout(world, region, anchors, bounds, seed, landscape, atlas)` → `validate_world_layout` → `bake_world_layout` | C++ 월드 그래프·도로 생성(랜드스케이프 샘플러) → 검증 → 마커 베이크 |
| `...log_report_to_message_log(report, title)` | 메시지 로그 `TDWorldGen`에 리포트 |
| 커맨드릿 `UnrealEditor-Cmd.exe TDGame.uproject -run=TDWorldGen -Flow=KeyLock -Size=Medium -Seeds=1-20 -Report=Saved/WorldGen/sweep.md` | 무인 시드 스윕·추천 |
| 빌더 `UnrealEditor-Cmd.exe TDGame.uproject /Game/Level/LV_DarkFantasy_OpenWorld -run=WorldPartitionBuilderCommandlet -Builder=TDWorldGenBuilder -Seed=7 -Validate [-Bake] [-Region=이름] [-Report=경로] -unattended` (에디터 닫고 실행) | 월드 생성 → 검증 → 리포트(→ 베이크), 실패 시 종료 코드 1 |
| `unreal.TDWorldGenEditorLibrary.generate_world_layout_with_locked / bake_world_layout_in_region / collect_locked_layout_elements` | 잠금(`TDLocked`) 유지·지역 단위 부분 재생성 |
| `Tools/WorldGen/editor_set_anchor_yaw.py` | 월드 정의 앵커 향(YawDeg) 갱신 |
| 콘솔(PIE) `TDTravelToDungeon <Id>` / `TDTravelToField` / `TDSaveWorldState <slot>` / `TDLoadWorldState <slot>` | 심리스 이동·세이브 |
| 내비메시 설정 `Tools/WorldGen/editor_setup_navmesh_dynamic.py`(채택, D-06) / `editor_setup_navmesh_wp.py`(정적 월드 파티션, 보류), 영속 상자 배치 `editor_place_test_chest.py`, PIE 걸어서 입장 `pie_walkin_check.py`, PIE 상자 상태 `pie_chest_check.py` | P1 검증 스크립트 |
| `python Tools/WorldGen/pie_p1_checks.py` (에디터 밖) | PIE 켜고 걸어 들어가기 왕복 ×3 + 상자 열림·세이브/로드 검사 → `Saved/WorldGen/pie_p1_checks.md` |
| `python Tools/tasks_recount.py` | 할 일 대장 상태 표 재계산 |
| 정의 에셋 `Tools/WorldGen/editor_make_definitions.py`, 룸 모듈 레벨 `Tools/DungeonGen/editor_make_room_modules.py`, C++ 베이크 `Tools/DungeonGen/editor_bake_dungeons_cpp.py`, 입구 배치 `Tools/WorldGen/editor_place_dungeon_entrances.py`, PIE 왕복 `Tools/WorldGen/pie_travel_check.py` | 에디터 스크립트 |

## 4. 도구가 없을 때 만드는 규칙 (에이전트 공통)
1. 먼저 에디터 Python으로 되는지 확인한다: `python Tools/run_in_editor.py -c "import unreal; print([n for n in dir(unreal) if 'Landscape' in n][:20])"`.
2. 되면 `templates/editor_tool_template.py`를 복사해 `Tools/<영역>/editor_<동사>_<대상>.py`로 만든다. 생성물 라벨 `TDGen_`, 폴더 지정, 마지막에 한 번 저장.
3. Python에 API가 없으면 `templates/cpp_editor_function_template.md` 절차로 `Source/TDGameEditor`에 C++ 함수를 추가하고 `python Tools/ue_editor.py restart`.
4. 오프라인 계산(생성·검증)은 `Tools/<영역>/<동사>_<대상>.py`(numpy)로 만들고 시드 결정론을 지킨다.
5. 만든 도구는 이 README 표에 한 줄 추가하고, 검증 캡처·로그를 `Docs/Validation/`에 남긴다.

## 5. 실측된 함정
- 새 C++ 모듈·클래스는 라이브 코딩이 안 된다 → `ue_editor.py restart`.
- 랜드스케이프 머티리얼을 다시 만들면 랜드스케이프가 검게/체커로 보인다 → 머티리얼 스크립트가 `landscape_material`을 다시 지정함. 셰이더 컴파일 2~4분 후 캡처.
- `MaterialEditingLibrary.delete_all_material_expressions`는 커스텀 출력 노드(LandscapeGrassOutput)를 안 지운다 → 머티리얼은 삭제 후 재생성.
- 에디터 Python에 없는 것: `Actor.add_component_by_class`, `set_is_spatially_loaded`(→ `set_editor_property("is_spatially_loaded", …)`), 랜드스케이프 생성(→ C++ `UTDLandscapeEditorLibrary`), `LandscapeLayerInfoObject.layer_name` 설정.
- 에디터 화면 캡처는 `EditorAppToolset.CaptureViewport`(captureTransform 지정)만 신뢰한다. `HighResShot`·`CaptureEditorImage`는 갱신 안 된 화면을 찍는다.
- 컴파일 오류 원인은 `Saved/Logs/TDGame.log`의 `LogMaterial`/`LogPython` 줄 다음 들여쓴 줄에 있다.

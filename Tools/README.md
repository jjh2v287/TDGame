# Tools — AI 에이전트(제미나이·Claude·Codex 공용) 언리얼 에디터 작업 도구

모든 도구는 `C:\Project\TDGame`에서 `python Tools/<파일>`로 실행한다(시스템 Python 3.12, numpy·scipy·PIL 설치됨).
에디터 안에서 도는 스크립트는 파일명이 `editor_*.py`이며 반드시 `run_in_editor.py`로 실행한다.
규칙은 `AGENTS.md`(로직은 C++, 이름 접두어 `TD`, 커밋은 사용자 지시 시에만). 세션·도구 생성·기록 규칙은 `AGENTS.md` 15절, 전문은 `Docs/AgentRules.md`(OP-12~OP-22가 도구 규칙). 이 파일은 절차와 등록표만 갖는다.

## 0. 시작 전 점검 (매 세션)
```bash
python Tools/ue_editor.py ensure     # 에디터 실행 + MCP 포트 8000 + Python 원격 실행 확인/자동 복구
```
빌드·테스트: `python Tools/ue_editor.py restart`(에디터 닫고 Build.bat 후 재기동; 새 모듈·클래스는 라이브 코딩 불가 L-build-01). 엔진·LLVM 절대 경로는 `Tools/ue_editor.py` 상단 상수에만 둔다(OP-19). PowerShell에서 `rg`는 디렉터리 + `--glob`(L-repo-02), 에셋 경로 `/Game/...` 인수는 PowerShell에서 실행(L-repo-01).

## 0b. 환경·설정 (문제가 있을 때만 읽는다)

Serena(코드 심볼 MCP 서버) — C++ 심볼 도구는 clangd가 켜져야 동작한다. 조건 두 가지: `~/.serena/projects/TDGame/.serena/project.yml`의 `language_servers`에 `cpp`가 있고, 프로젝트 루트에 `compile_commands.json`이 있어야 한다. 이 파일은 다음 명령으로 만들며 소스 파일 추가·모듈 의존성 변경 뒤 다시 만든다(생성물, 커밋 금지):
`"<UE_5.8>/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe" -mode=GenerateClangDatabase -project="<프로젝트 루트>/TDGame.uproject" TDGameEditor Win64 Development -OutputDir="<프로젝트 루트>"` (경로 상수는 `Tools/ue_editor.py` 상단).
Serena가 내려받는 clangd 19.1.2는 UE 5.8 엔진 헤더를 파싱하다 종료되므로 같은 project.yml의 `ls_specific_settings.cpp.ls_path`에 LLVM clangd 22.1.2(`C:/Program Files/LLVM/bin/clangd.exe`)를 지정한다(2026-09-18 확인: 설정 실재). 점검: `uvx --from git+https://github.com/oraios/serena serena project health-check`. 심볼 도구가 "Active language servers: []"로 실패하면 위 조건을 확인하고, 그래도 안 되면 `search_for_pattern`과 파일 도구로 대체한다. Serena 메모리(`~/.serena/projects/TDGame/.serena/memories/`)에는 프로젝트 사실을 두지 않는다(OP-30).

언리얼 MCP — 서버 설정 `Config/DefaultEditorPerProjectUserSettings.ini`(포트 8000, 경로 `/mcp`). 클라이언트 설정 5벌: `.mcp.json`(Claude Code), `.codex/config.toml`(Codex), `.cursor/mcp.json`, `.gemini/settings.json`(Gemini CLI), `.agents/mcp_config.json`(Antigravity 작업 공간). 모두 `http://127.0.0.1:8000/mcp`. 주소·포트·Blender 런처를 바꾸면 서버 ini와 5벌을 함께 고친다(OP-31). `.vscode/mcp.json`은 VS Code Copilot 미사용 가정으로 동기화에서 제외(D-30). 사용자 홈 `~/.gemini/config/mcp_config.json`(Antigravity `~/.gemini/antigravity/mcp_config.json`은 이 파일의 심볼릭 링크)은 2026-09-18 저장소 런처 방식으로 정렬함(D-19·D-36). MCP는 도구 검색이 켜져 있어 도구 목록이 통째로 로드되지 않으므로 필요한 작업을 키워드로 검색해 도구를 찾은 뒤 호출한다. Blender MCP 서버는 `Tools/BlenderMCP/Run-BlenderMCP.ps1` 외의 방법(`uvx blender-mcp`·`mcp-for-blender`·pip 설치본)으로 띄우지 않는다(텔레메트리 보호, OP-31).
에디터 기동: `python Tools/ue_editor.py start`(내부에서 `tasklist`로 중복 실행을 피하고 포트 8000이 열릴 때까지 기다린다). 에디터가 켜진 상태에서는 Build.bat 대신 라이브 코딩 도구로 컴파일하고, 헤더 변경이 큰 작업은 `ue_editor.py restart`. 에디터를 닫을 때는 저장되지 않은 에셋이 있는지 사용자에게 먼저 알린다(`ue_editor.py stop`이 중단한다).
확인: 2026-09-18 claude


## 1. 범용 도구 (언리얼의 모든 기능에 접근하는 세 갈래)
| 도구 | 용도 | 예 |
|---|---|---|
| `run_in_editor.py` | 에디터 안에서 임의 Python(`unreal` 모듈 전체) 실행. 에셋 생성·레벨 편집·설정·저장·PIE 조회 등 **에디터 Python API 전부** | `python Tools/run_in_editor.py -c "import unreal; print(unreal.EditorAssetLibrary.list_assets('/Game/World'))"` |
| `uemcp.py` | 공식 MCP 서버를 셸에서 직접 호출(툴셋 목록·설명·호출). 액터 배치·프로퍼티·PIE·자동화 테스트·설정·뷰포트 캡처·UI 자동화 | `python Tools/uemcp.py call describe_toolset '{"toolset_name":"EditorToolset.EditorAppToolset"}'` |
| `ue_editor.py` | 에디터 상태/시작/종료/빌드/재시작, Python 원격 실행 on/off | `python Tools/ue_editor.py restart` |
| `editor_inspect_level.py` | 현재 레벨 액터 요약 JSON(`Saved/Inspect/`) | `python Tools/run_in_editor.py Tools/editor_inspect_level.py` |
| `check_automation_tests.py` | MCP AutomationTestToolset으로 `TDGame.*` 자동화 테스트 발견·실행, 실패만 요약(`Saved/AgentOps/automation_tests_*.json`) | `python Tools/check_automation_tests.py --filter TDGame.Combat` |
| `pie_profile.py` (+`editor_pie_profile.py`) | PIE 11초 프레임 간격 측정 + `stat dumpframe`·`ProfileGPU` 로그 발췌. "PIE가 느리다"를 스로틀·게임 스레드·GPU 중 어디인지 가른다(L-editor-06) | `python Tools/pie_profile.py` |
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

## 3c. 애니메이션·기타 도구 (상태·등록 대기)
| 위치 | 내용 | 상태 |
|---|---|---|
| `AnimationAuthoring/` | Unreal MCP C++ 툴셋 기반 저작. 정본 스킬 `SKILL.md`(td-animation-authoring), `smoke_test.py`(MCP 클라이언트 `TDMcpAnimationClient` 보유, D-22), `author_slash.py`·`author_swing.py`·`dump_animation.py`·`anim_report.py`·`pose_kinematics.py`. PowerShell에서 실행(L-repo-01) | 현행 |
| `BlenderAnimation/` | Blender MCP bpy 편집 + Unreal 재가져오기. 정본 스킬 `SKILL.md`(td-combat-animation-quality, 스크립트 표 포함). `author_*`·`prepare_*`·`render_*`·`validate_*`·`inspect_*`·`create_player_montage.py` 등 14개 + grip 5개 + 검 횡베기 7개(`*_sword_slash*.py`, 2026-09-19 등록; 현행 공격은 이 묶음만 대상이고 앞선 19개는 삭제된 후보의 예제) | 현행 |
| `BlenderMCP/` | Blender MCP 서버 실행(`Run-BlenderMCP.ps1`), `call_tool.py --code`로 bpy 실행, `smoke_test.py` | 현행 |
| `Content/Python/td_blender_animation_tools.py` | 에디터 상주 툴셋(`export_fbx`·`import_animation_fbx`·`sample_animation_poses`), `init_unreal.py`가 register 호출 | 현행 |
| `Animation/` (11개) | Kimodo text-to-motion 실험 파이프라인. 2026-09-18 `Tools/_archive/2026-09/Animation/`으로 보관(D-15) | 보관 · 실행 금지 |
| `.gemini/scripts/` (13개) | 중복·구식 스크립트. 2026-09-18 `Tools/_archive/2026-09/gemini-scripts/`로 보관(D-16). 이유는 `Tools/_archive/README.md` | 보관 · 실행 금지 |
| Claude 홈 `tools/` | `run_tests.py`는 `Tools/check_automation_tests.py`로 이관 후 홈 원본 삭제, `uemcp.py` 동일본 삭제. 나머지 일회용 12개는 홈에 유지(D-17) | 이관 완료 |
| `_archive/` | 폐기·보관 도구. 목록과 이유는 `Tools/_archive/README.md` | 실행 금지 |
확인: 2026-09-18 claude

## 4. 도구가 없을 때 만드는 규칙 (에이전트 공통)
규칙 전문은 `Docs/AgentRules.md` OP-12(만들기 전 검색)·OP-13(위치)·OP-14(이름)·OP-15(docstring 4줄)·OP-16(등록)·OP-17(`Tools/scratch/`)·OP-19(절대 경로 금지)·OP-20(노출 경로). 접두어: `editor_`(에디터 안, run_in_editor.py), `pie_`(PIE 검사), `blender_`(Blender 안 bpy), 없음(시스템 파이썬 3.12). 권장 동사: generate build make validate check inspect capture export import author render prepare place setup select find. 금지 동의어: create→make, bake→build, verify/test→validate/check.
1. 먼저 에디터 Python으로 되는지 확인한다: `python Tools/run_in_editor.py -c "import unreal; print([n for n in dir(unreal) if 'Landscape' in n][:20])"`.
2. 되면 `templates/editor_tool_template.py`를 복사해 `Tools/<영역>/editor_<동사>_<대상>.py`로 만든다. 생성물 라벨 `TDGen_`, 폴더 지정, 마지막에 한 번 저장.
3. Python에 API가 없으면 `templates/cpp_editor_function_template.md` 절차로 `Source/TDGameEditor`에 C++ 함수를 추가하고 `python Tools/ue_editor.py restart`.
4. 오프라인 계산(생성·검증)은 `Tools/<영역>/<동사>_<대상>.py`(numpy)로 만들고 시드 결정론을 지킨다.
5. 만든 도구는 같은 세션에 이 README 표에 한 줄 추가하고(OP-16), 검증 캡처·로그를 `Docs/Validation/`에 남긴다(OP-24). 실험·1회용은 `Tools/scratch/<YYYYMMDD>_<agent>_<주제>/`(git 제외).

## 5. 실측된 함정
실측 함정은 `Docs/Lessons/`로 옮겼다(L-build-01·02, L-editor-01~05, L-repo-01~06, L-anim-01). 새 함정은 그곳에 `### L-<영역>-<번호>` 항목으로 적고(OP-26), 명령이 실패하면 먼저 `rg -n "<오류 문구>" Docs/Lessons Tools/README.md`를 실행한다(OP-27).

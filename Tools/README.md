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

언리얼 MCP — 서버 설정 `Config/DefaultEditorPerProjectUserSettings.ini`(포트 8000, 경로 `/mcp`). 클라이언트 설정 5벌: `.mcp.json`(Claude Code), `.codex/config.toml`(Codex), `.cursor/mcp.json`, `.gemini/settings.json`(Gemini CLI), `.agents/mcp_config.json`(Antigravity 작업 공간). 모두 `http://127.0.0.1:8000/mcp`(Cascadeur 서버 `cascadeur`는 `http://127.0.0.1:8765/mcp`, 3f절). 주소·포트·Blender 런처를 바꾸면 서버 ini와 5벌을 함께 고친다(OP-31). `.vscode/mcp.json`은 VS Code Copilot 미사용 가정으로 동기화에서 제외(D-30). 사용자 홈 `~/.gemini/config/mcp_config.json`(Antigravity `~/.gemini/antigravity/mcp_config.json`은 이 파일의 심볼릭 링크)은 2026-09-18 저장소 런처 방식으로 정렬함(D-19·D-36). MCP는 도구 검색이 켜져 있어 도구 목록이 통째로 로드되지 않으므로 필요한 작업을 키워드로 검색해 도구를 찾은 뒤 호출한다. Blender MCP 서버는 `Tools/BlenderMCP/Run-BlenderMCP.ps1` 외의 방법(`uvx blender-mcp`·`mcp-for-blender`·pip 설치본)으로 띄우지 않는다(텔레메트리 보호, OP-31).
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
| `check_sync_loads.py` | 런타임 C++(`Source/`·`Plugins/*/Source/`)의 동기 에셋 로드(LoadSynchronous·LoadObject·ConstructorHelpers·TryLoad·FlushAsyncLoading·WaitUntilComplete 등)를 찾아 허용 범위(에디터 모듈, WITH_EDITOR·WITH_EDITORONLY_DATA 블록, Tests 폴더의 자동화 시험 블록)와 기준 목록 밖이면 종료 코드 1(OP-33). 에디터 불필요 | `python Tools/check_sync_loads.py --list-all` |
| `check_cook_coverage.py` | 런타임 C++의 `/Game` 문자열 경로마다 쿠킹 근거(DefaultGame.ini 항상 쿠킹 폴더·런타임 설정 ini 값·ConstructorHelpers 시작 로드·그 두 근거에 속한 패키지의 직접 참조·예외 목록)를 판정해 근거가 없거나 `DirectoriesToNeverCook` 안이면 종료 코드 1(OP-33, L-build-07). 이어붙이기·Printf·Format·Combine 경로와 없는 패키지 이름의 접두는 폴더 단위. 에디터·쿠킹 불필요. 여러 줄 원시 문자열·FName 숫자 접미·AssetManager 쿠킹 규칙은 못 봄 | `python Tools/check_cook_coverage.py --list-all` |
| `Damage/editor_make_megamagic_arena.py` | MegaMagic 주문용 전투 테스트 맵 신규 생성(기존 목적지 보존) | `python Tools/run_in_editor.py Tools/Damage/editor_make_megamagic_arena.py` |
| `Damage/editor_validate_megamagic_pie.py` | 실행 중인 테스트 맵에서 10종 시전·피해·아군 제외·VFX·정리 검사, `Saved/Damage/megamagic-pie.json` | `python Tools/run_in_editor.py Tools/Damage/editor_validate_megamagic_pie.py` |
| `TDDamageExamples -MegaMagic` 명령렛 | 실제 VFX를 참조하는 주문 그래프를 `/Game/Combat/MegaMagic`에 신규 저장, `-ValidateOnly`로 저장 결과 검사 | 에디터 종료 후 `UnrealEditor-Cmd TDGame.uproject -run=TDDamageExamples -MegaMagic -unattended -NullRHI` |
| `pie_profile.py` (+`editor_pie_profile.py`) | PIE 11초 프레임 간격 측정 + `stat dumpframe`·`ProfileGPU` 로그 발췌. "PIE가 느리다"를 스로틀·게임 스레드·GPU 중 어디인지 가른다(L-editor-06) | `python Tools/pie_profile.py` |
| `cook_single_map.py` | RunUAT BuildCookRun으로 Win64 Development 게임을 빌드하고 맵 하나만 쿠킹·스테이징·pak(에디터 꺼짐 필요, LV-Cambat 약 7분). 결과 `Saved/AgentOps/cook_<label>.json` | `python Tools/cook_single_map.py --label <이름>` |
| `measure_game_frames.py` | 쿠킹 스테이징 실행 파일로 맵을 띄워 CSV 프로파일러 1,500프레임 → 워밍업 뒤 프레임·게임 스레드·렌더·GPU 평균/p50/p95와 크래시 줄 요약(`Saved/AgentOps/frames_<label>.json`). 에디터 바이너리 `-game`은 이 환경에서 로그 없이 즉시 종료돼 쓰지 않는다 | `python Tools/measure_game_frames.py --label <이름>` |
| `templates/` | 새 도구 템플릿(에디터 Python, C++ 에디터 함수 절차) | 복사해서 시작 |

MCP 툴셋 이름은 `python Tools/uemcp.py call list_toolsets '{}'`로 본다. 자주 쓰는 것: `editor_toolset.toolsets.scene.SceneTools`(액터 스폰·검색), `...object.ObjectTools`(프로퍼티), `...asset.AssetTools`, `EditorToolset.EditorAppToolset`(StartPIE/StopPIE/CaptureViewport/SetCameraTransform), `AutomationTestToolset.AutomationTestToolset`, `ConfigSettingsToolset.ConfigSettingsToolset`, `EditorToolset.LogsToolset`(GetLogEntries), `SlateInspectorToolset`(UI 자동화).

## 2. 월드 생성 도구 (`WorldGen/`, 설계서 4·5·11·12장)
| 도구 | 용도 |
|---|---|
| `WorldGen/generate_ashen_vale.py --seed N [--out 폴더]` | 메인 지역 높이맵·레이어·배치 생성(에디터 밖) → `Saved/WorldGen/AshenVale/` |
| `WorldGen/editor_build_open_world.py` | 레벨에 베이크(랜드스케이프·대기·인스턴스·액터·조명·마커·저장). `Saved/WorldGen/open_world_build_request.json` {"mode":"partial","previous_layout":...}이면 부분 모드(지형·생성 산포만 교체, 던전·PCG·상자·이계 보존) |
| `WorldGen/generate_realm.py --realm SunkenStarfield·PaleHeights·all [--seed N]` | 연속 세계(D-45, P3-19) 이계 지역 생성: 필드 옆 자리(`realm_passages.REALM_PLACEMENT`)에 높이맵·레이어·배치, 공유 변 필드 값 복사·45m 경계 띠, 연결로 이계 구간·연결로 소품(`TDRealm_Connector_<GateId>_*`)·관문 `gates[]`·지형 MI 지역 혼합 값, 검증(에디터 밖, 필드 생성 뒤) → `Saved/WorldGen/Realms/<RealmId>/` |
| `WorldGen/editor_build_realm.py` | 이계 베이크(지형·재질 인스턴스·인스턴스·액터·연결로 소품·내비 경계·후처리·`DA_TDRealm_*`(관문·승강 lift·`gameplay_data_layer`)·저장). 굽기 전에 layout을 검사하고, 랜드스케이프는 태그 `TDRealm:<R>`, 그 밖은 `DL_TDRegion_<R>` 소속·이전 접두로 지운다 |
| `WorldGen/realm_passages.py` | 연속 세계 공유 정의(이계 배치 `REALM_PLACEMENT`, 걷는 연결로 `CONNECTOR_ROWS`, 승강 갱도 `SHAFT_ROWS`, 운행 `LIFT_MOTION`, 단면·스탬프·소품, 난수 없음). `generate_ashen_vale.py`·`generate_realm.py`·`check_continuous_world.py`가 가져다 쓴다(직접 실행 시 자체 점검) |
| `WorldGen/check_continuous_world.py [--no-rerun] [--report <json>]` | 연속 세계 결정론 검사(필드·두 이계 생성기 출력을 한 월드 격자로 이어붙여 K2 결정론·K3 공유 경계·K4 경사·K5 시야 누출(16:9·21:9)·K6 거리 보장·K7 능선 차단·K8 강 끝·라이너 부등식·재질 지역 혼합·배치·옛 키), 실패 시 종료 코드 1 → `Saved/WorldGen/ContinuousCheck/report.json` |
| `WorldGen/editor_setup_data_layers.py` (PowerShell, `run_in_editor.py`) | 데이터 레이어 5개(`DL_TDRegion_{Field,SunkenStarfield,PaleHeights}` Editor, `DL_TDGameplay_{SunkenStarfield,PaleHeights}` Runtime 초기 Loaded)와 레벨 인스턴스를 멱등 생성, 빌더용 DL 문맥 함수 제공 → `Saved/WorldGen/data_layers_report.json` |
| `WorldGen/editor_make_landscape_material.py` | 레이어 머티리얼(텍스처 봄빙)·풀 타입 재생성 |
| `WorldGen/editor_relight.py` + `Saved/WorldGen/look.json` | 재빌드 없이 조명·노출·안개 조정. `Saved/WorldGen/relight_target.json` `{"realm": "<RealmId>"}`이면 이계 대기 미리보기(저장 안 함), 파일을 지우고 다시 실행하면 복원 |
| `WorldGen/editor_set_sun.py` + `Saved/WorldGen/sun_request.json` (PowerShell, PIE 종료) | 태양 회전만 바꾼다(D-46: 빛 진행 방향을 카메라 반대). `{"field": [pitch, yaw], "realms_from_layout": true, "save": false}`는 메모리만 바꿔 다음 PIE 촬영에 반영되는 미리보기, `"save": true`는 필드 `TDGen_Sun` 외부 액터 패키지 하나와 바뀌었거나 미저장인 `DA_TDRealm_*`만 저장하고 look.json을 맞춘다. 이계 값은 `generate_realm.py` → layout.json에서 읽는다. 카메라 반대(yaw 225)에서 수평 30° 넘게 벗어나면 바꾸기 전에 거부하고, 저장 실패는 오류로 끝난다. 미리보기를 되돌리려면 sun_result.json의 before 값으로 다시 미리보기를 건다(미리보기 상태로 `save_dirty_packages`를 쓰는 다른 도구를 돌리면 그 값이 저장된다) → `Saved/WorldGen/sun_result.json` (P3-20) |
| `WorldGen/validate_world.py` | 월드 검증(입구 간격·POI 밀도·도로 도달성·지형 적합성·스트리밍·플레이 밀도) → report.md |
| `WorldGen/select_seed.py --seeds 1-5` | 시드 후보 생성·검증·점수·추천 |
| `WorldGen/capture_views_mcp.py 이름=x,y,pitch,yaw,dist` | 지점별 뷰포트 캡처 PNG(지면 높이는 높이맵, 이계 상자 안이면 Zoff 반영) |
| `WorldGen/pie_check.py` | PIE 중 폰 위치·지면·내비메시 확인 |
| `WorldGen/editor_make_pcg_biome.py` | P3-07 Forest 바이옴 PCG 그래프(메인+서브 5)·룸 드레싱 그래프 생성, `TDGen_PCG_BiomeForest` PCGVolume(파티션+HiGen) 배치·생성 |
| `WorldGen/editor_check_pcg_determinism.py` | PCG 같은 시드 cleanup→generate 2회, ISM 수·위치 해시 비교 → `Saved/WorldGen/pcg_determinism.json` |
| `WorldGen/rebuild_all.sh [--material] [--realms] [--seed N] [--wait 초] [캡처...]` | 전체 파이프라인 한 줄(`--realms`: 메인 베이크 뒤 이계 베이크) |
| `WorldGen/editor_make_topdown_materials.py` | 탑다운 재질 체계(D-43) `/Game/World/Materials/TopDown/`: MPC_TD_TopDown·MF_TD_OcclusionCutout·마스터 5(팩 마스터 복제 + 컷아웃 삽입)·거울 MI_TD_*, 이계 MI 재부모화(재실행 안전) → `Saved/WorldGen/topdown_materials_report.json` |
| `WorldGen/editor_apply_topdown_materials.py` (옵션 파일 `Saved/WorldGen/topdown_apply_request.json` `{"dry_run":…, "patch_pcg":…}`) | 레벨 배치물(베이크·이계·마을·PCG 출력) 재질 슬롯을 거울 MI로 교체, `patch_pcg`는 PCG 그래프 메시 항목 재질 덮어쓰기 패치. 순서: 베이크 → patch_pcg → PCG 재생성 → apply → 저장 → `Saved/WorldGen/topdown_apply_report.json` |
| `WorldGen/topdown_rules.py --measure --label <이름>` | 탑다운 카메라(폰 기준 (−500,−500,+843)cm) 결정론 배치 후처리 규칙 R1~R3·R5 공용 모듈(두 생성기가 import, 난수 없음)과 걷는 칸 가림%·카메라 매몰·카메라 지형 아래 수치 → `Saved/WorldGen/TopDownMetrics/`. 생성기 `--no-topdown`은 규칙 끄기(이전 출력 바이트 재현) |
| `WorldGen/topdown_materials.py` | 팩 재질 → 거울 경로 대응 규칙(에디터 밖 import 가능) |
| `WorldGen/pie_capture_spots.py --run <이름> [--ids B01,B02,B03] [--compare <이전 run>] [--spots 파일] [--exposure-offset EV]` (PowerShell, 에디터 앞에 둠) | PIE 게임 카메라로 지점 목록(기본 `WorldGen/topdown_spots.json` 36곳: 연결로 입구 C01~C03·경계 통과점 B01~B03(K15)·갱도 상단 F12/하단 S05 포함, 모두 teleport, 이계 높이 Zoff 반영) 촬영·가림 액터·카메라 매몰·어둠 비율, 전후 비교 표 → `Saved/WorldGen/PieShots/<run>/` (P3-17, P3-19). `--exposure-offset`은 PIE에서 r.ExposureOffset을 두고 끝나면 0으로(수동 노출이라 어두운 곳 K15 측정용) |
| `WorldGen/pie_walk_passages.py --run <이름> [--gates …] [--simulate-delay 초] [--offline]` (PowerShell, 에디터 앞에 둠) | 연결로 SW·NE 지선 끝↔이계 도착 실제 이동 입력 왕복과 SE 승강기 왕복(지선→상단 협곡→덱→출구 슬롯). 프레임별 폰·카메라 이동·w·준비·감속, 덱 높이·링 속도·트레드밀 단계, 에디터 로그(감속 발동·DL 활성화·트레드밀·준비 시간)로 K1·K11~K14·K17 참고 판정과 프레임 시간 → `Saved/WorldGen/PieWalk/<run>/`. 감속 강제 `--simulate-delay 30`, 트레드밀 강제 `15`. 시작 배치 뒤 폰 둘레 격자 칸 Loaded·카메라 정지까지 기다린 뒤 기록(`settle_s`, 순간 배치 산물 제외, L-worldgen-05). 첫 프레임이 이미 PRELOADING이면 K12는 상태 시간으로, 지연 모의의 K13 감속·K11 트레드밀은 실행 전체로 센다. `--offline`은 저장된 결과로 summary.md만 다시 만든다 |
| `WorldGen/check_boundary_luminance.py --run <촬영 run> [--ids B01,B02,B03] [--self-test] [--out 폴더]` (에디터 불필요) | `pie_capture_spots.py` 경계 지점 촬영(PNG·카메라 JSON)에 공유 경계선을 높이맵 지면으로 투영해, 경계를 가로지르는 단면 17개의 휘도 계단 ÷ 같은 단면 인접 차이 중앙값을 잰다(K15 ≤ 2.0). `--self-test`는 경계 바깥쪽 휘도 ×1.10·×1.25 합성으로 측정 민감도를 확인한다. 출력 `boundary_luminance.json·.md`·`<지점>_boundary_overlay.png` (P3-19) |
| `WorldGen/pie_perf_memory.py --run <이름> [--settle 8] [--sample 8] [--offline]` (PowerShell 또는 Git Bash, 에디터 앞에 둠) | 필드 기준 4곳·연결로 8곳에 서서 `csvprofile`(r.GPUCsvStatsEnabled 1, `GPUTime` 열, 파일 끝 헤더 처리)로 GPU p99를 비교하고 필드 중앙·남쪽 테두리·두 이계 중앙에서 `memreport -full` 물리 메모리를 비교한다(K16: GPU p99 비율 ≤ 1.2, 메모리 +30% 표시) → `Saved/WorldGen/PiePerf/<run>/` (P3-19) |
| `WorldGen/editor_pie_probe.py` | 위 두 드라이버가 요청 파일 `Saved/WorldGen/pie_probe_request.json`로 부르는 PIE 안 동작(move·shot·occluders·walk(경유점 따라 걷기)·ride(접근·출구 경로, 링 속도)·console), 50m 점프·카메라 점프 기록. 페이드는 Python으로 잴 수 없다(UE 5.8 `APlayerCameraManager::FadeAmount`가 UPROPERTY가 아니라 `fade_amount`를 읽지 못해 결과가 늘 "측정 불가"). 페이드·카메라 컷 0회는 C++ PIE 시험 `TDGame.SeamlessTravel.RealmConnectorWalk`·`RealmLiftRide`·`RealmLiftTreadmill`이 판정한다 |
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
| `python Tools/WorldGen/pie_p1_checks.py [--ids MainCrypt,<관문 ID>…]` (에디터 밖) | PIE 켜고 목적지마다 걸어 들어가기 왕복 ×3(이계 관문은 이계 상자, 승강 관문은 하단 착지 근처로 판정, 귀환은 필드 상자 ±50400cm) + 상자 열림·세이브/로드 검사 → `Saved/WorldGen/pie_p1_checks.md` |
| `python Tools/tasks_recount.py` | 할 일 대장 상태 표 재계산 |
| 정의 에셋 `Tools/WorldGen/editor_make_definitions.py`, 룸 모듈 레벨 `Tools/DungeonGen/editor_make_room_modules.py`, C++ 베이크 `Tools/DungeonGen/editor_bake_dungeons_cpp.py`, 던전 안 출구 배치 `Tools/WorldGen/editor_place_dungeon_entrances.py`(D-41: 필드 입구는 만들지 않음), PIE 왕복 `Tools/WorldGen/pie_travel_check.py` | 에디터 스크립트 |

## 3c. 애니메이션·기타 도구 (상태·등록 대기)
| 위치 | 내용 | 상태 |
|---|---|---|
| `AnimationAuthoring/` | Unreal MCP C++ 툴셋 기반 저작. 정본 스킬 `SKILL.md`(td-animation-authoring), `smoke_test.py`(MCP 클라이언트 `TDMcpAnimationClient` 보유, D-22), `author_slash.py`·`author_swing.py`·`dump_animation.py`·`anim_report.py`·`pose_kinematics.py`. PowerShell에서 실행(L-repo-01) | 현행 |
| `BlenderAnimation/` | Blender MCP bpy 편집 + Unreal 재가져오기. 정본 스킬 `SKILL.md`(td-combat-animation-quality, 스크립트 표 포함). `author_*`·`prepare_*`·`render_*`·`validate_*`·`inspect_*` 등(몽타주 의존 6개는 2026-09-30 `_archive/2026-09/BlenderAnimation/`로 보관, D44) 14개 + grip 5개 + 검 횡베기 7개(`*_sword_slash*.py`, 대상 에셋 2026-09-25 삭제) + 공통 접지 QA `validate_contacts.py`(2026-09-19 등록) + 검 공격 01 15개(`blender_run.py`, `sword_attack01_*.py`, `*_sword_attack01*.py`, `editor_analyze_attack_candidates.py`, `editor_export_reference_fbx.py`, `editor_solve_sword_attachment.py`, 2026-09-25 등록, 표는 SKILL.md) | 현행 |
| `BlenderMCP/` | Blender MCP 서버 실행(`Run-BlenderMCP.ps1`), `call_tool.py --code`로 bpy 실행, `smoke_test.py` | 현행 |
| `Content/Python/td_blender_animation_tools.py` | 에디터 상주 툴셋(`export_fbx`·`import_animation_fbx`·`sample_animation_poses`), `init_unreal.py`가 register 호출 | 현행 |
| `Animation/` (11개) | Kimodo text-to-motion 실험 파이프라인. 2026-09-18 `Tools/_archive/2026-09/Animation/`으로 보관(D-15) | 보관 · 실행 금지 |
| `.gemini/scripts/` (13개) | 중복·구식 스크립트. 2026-09-18 `Tools/_archive/2026-09/gemini-scripts/`로 보관(D-16). 이유는 `Tools/_archive/README.md` | 보관 · 실행 금지 |
| Claude 홈 `tools/` | `run_tests.py`는 `Tools/check_automation_tests.py`로 이관 후 홈 원본 삭제, `uemcp.py` 동일본 삭제. 나머지 일회용 12개는 홈에 유지(D-17) | 이관 완료 |
| `_archive/` | 폐기·보관 도구. 목록과 이유는 `Tools/_archive/README.md` | 실행 금지 |
확인: 2026-09-18 claude

## 3d. 몬스터 AI 도구 (`MonsterAI/`, 대장 `Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md`)
| 도구 | 용도 | 실행 |
|---|---|---|
| `MonsterAI/editor_make_monster_species.py` | 표(`Tools/MonsterAI/species_table.json`)의 종마다 종 에셋 `DA_TDMonster_*`(메시·추종 메시·애니메이션 구간·Hit·Death·정의 ID·시체 값)와 루트 고정 공격 애니메이션 복제본 생성·갱신(재실행 안전). 종마다 스켈레톤 호환·능력 키 대조(`ABILITY MISMATCH`)·없는 에셋(`missing asset`, 그 종은 저장 안 함) 로그와 요약 한 줄. `TD_ONLY_SPECIES=['Goblin']`로 일부 종만. 표 검사(에디터 불필요): `python Tools/MonsterAI/check_species_table.py`(종료 0=통과, `--root`·`--table`). 옛 `Combat/editor_set_species_hit_clips.py`는 이 도구의 `hit`로 흡수(M3-33). 선택 키 `phases`(정의 페이즈 id → 이동·피격·사망 클립·추종 메시, C++ `PhasePresentations`)·`takeoff/fly_idle/land/fly_hit`(공중 클립)·`channel/stun`(끊기 채널·긴 기절, C++ `ChannelClip`·`StunClip`)은 그 C++ 필드가 없으면 경고만 내고 건너뜀 | `python Tools/run_in_editor.py Tools/MonsterAI/editor_make_monster_species.py` (PowerShell) |
| `MonsterAI/editor_make_loop_clips.py` | 표의 `loop_clips` 항목마다(현재 Fab 하이에나 16.5초) 앞부분만 움직이고 뒤가 정지 포즈로 채워진 원본에서 동작 구간(원본 프레임 무변경)만 남기고, 끝과 시작 사이의 빠진 틈 G프레임을 C1 Hermite(회전은 로그 공간)로 채워 루프 자산 `AS_TD_Hyena_{Idle,Walk,Run}`을 만든다. G = 몸통 뼈 틈 거리÷경계 속도의 가중 중앙값−1(경계가 정지면 12). 지면 속도·종 자산 갱신(`update_species_run_speed` 항목만), 종 자산이 쓰는 대기 시퀀스 loop 켜기(기본 자산 대기는 bLoop을 따름, L-anim-15). 결과가 같으면 저장 안 함, 출력 `Saved/MonsterAI/loop_clips.json`(`entries` 배열, M3-26·M3-33), `TD_ONLY_SPECIES=['Hyena']`로 일부만 | `python Tools/run_in_editor.py Tools/MonsterAI/editor_make_loop_clips.py` (PowerShell) |
| `MonsterAI/editor_retarget_monster_anims.py` | 표의 `retarget` 절에 적힌 원천(Manny) 애니메이션을 대상(고블린·골렘·경비병·책머리·Greystone·Kwang·Wukong·Mixamo SKM_Man)으로 IK 리타기팅(IK 리그·리타기터 자동 캐릭터화, 결과 루트 고정, 루트가 Hips인 Mixamo는 L-anim-19, 뼈 이름은 `bone_names` 사상). 자기가 만든 에셋(`TDGeneratedBy` 태그)만 갱신, `TD_ONLY_TARGETS`·`TD_ONLY_CLIPS`로 일부만, 약 11초 | `python Tools/run_in_editor.py Tools/MonsterAI/editor_retarget_monster_anims.py` (PowerShell) |
| `MonsterAI/check_species_table.py` | 에디터 없이 종 표(`species_table.json`)를 검사: 필수 키·숫자 범위·이름 중복·`/Game` 경로의 `.uasset` 존재(도구가 만들 경로는 WARN)·정의 JSON 존재·능력 키 대조(extends 사슬)·리타깃 클립·루프 항목. ERROR가 있으면 종료 1 | `python Tools/MonsterAI/check_species_table.py [--table <json>] [--root <프로젝트>]` |
| `MonsterAI/editor_measure_species_clips.py` | 새 종 값 결정용 측정(읽기만): 메시 바운드·물리 에셋·정면 단서(머리 뼈 방향), 클립 길이·루트 이동(`_RM` 판이면 실제 지면 속도)·종 시험과 같은 루프 이음매·정지 꼬리·걸음 비·디딤발 추정 지면 속도·빠른 손/무기/머리 뼈 최고 속도 시각(타격 시각 후보) → `Saved/MonsterAI/species_measure.json`. 입력 `{"groups": [{"name","mesh","clips":{역할: 경로}}]}` | `python Tools/run_in_editor.py Tools/MonsterAI/editor_measure_species_clips.py` (PowerShell, 입력 `Saved/MonsterAI/species_measure_input.json`, `TD_MEASURE_INPUT`·`TD_MEASURE_OUTPUT`로 바꿈) |
| `MonsterAI/pie_check_species.py` | 종마다 PIE에서 `TDSpawnMonster <종> 1 1000` → 이동 거리·공격(피해 엔티티 생성)·피격(최대 체력 30% 피해)·사망(3배 피해)을 판정하고 단계별 `Shot` 스크린샷 4장 → `Docs/MonsterAI_CombatSim/measurements/monsterize/pie-<날짜>/report.json`·`<종>_{1_move,2_attack,3_hit,4_death}.png`. 맵 저장 안 함, 에디터 앞창 필요 | `python Tools/MonsterAI/pie_check_species.py [--species A B] [--map /Game/Combat/Maps/LV_TDMegaMagicArena]` (PowerShell) |
| `MonsterAI/pie_check_mid_bosses.py` | 중간 보스 2종(CentaurHunter·GriffonAlpha)을 PIE에서 `TDSpawnMonster <보스> 1 900`으로 한 마리씩 불러 페이즈별로 관측하고 처치까지 단계 스크린샷: 켄타우로스 `1_bow`(화살 엔티티)→체력 48%→`2_draw_sword`(페이즈 1·졸개 Barghest 2)→졸개 처치→`3_sword`(Charge·SwordCombo·SwordSlash)→`4_death`, 그리폰 `1_ground`(Bite·Claw·RakeTwo)→체력 58%→`2_takeoff`(Z가 시작 대비 +80cm)→`3_air_attack`(DiveClaw·GlideSwoop)→`4_landed`(Z +30cm 이하)→`5_death`. 공격은 피해 엔티티 정의 이름, 체력은 `TryApplyDamage`로 깎음 → `Docs/MonsterAI_CombatSim/measurements/M3-34/pie-<날짜>/report.json`(단계별 통과·경과 초·표본 수·페이즈 전이 시각·관측 정의 이름)·`<보스>_<단계>.png`. 종료 코드 0 전부 통과/1 일부 실패/2 환경 문제. 맵 저장 안 함, 에디터 앞창 필요 | `python Tools/MonsterAI/pie_check_mid_bosses.py [--boss CentaurHunter GriffonAlpha] [--map /Game/Combat/Maps/LV_TDMegaMagicArena] [--distance 900] [--level 30] [--out <폴더>]` (PowerShell) |
| `MonsterAI/editor_make_boss_telegraphs.py` | 보스 예고 재질·인스턴스 생성(재실행 안전, 덮어쓰기 거부): `M_TD_TelegraphCone`(반각 파라미터)·`M_TD_TelegraphBand`(띠) + `MI_TD_Telegraph_{Unblockable,Safe}`·`MI_TD_TelegraphCone_{Danger,Unblockable}_{20,30,35,45,60,90}`·`MI_TD_TelegraphBand_{Danger,Unblockable}`. Plane UV 축을 정점에서 계산해 출력. 만든 같은 세션의 카탈로그는 디버그 대체로 남으니 에디터 재시작(L-editor-34) | `python Tools/run_in_editor.py Tools/MonsterAI/editor_make_boss_telegraphs.py` (PowerShell) |
| `MonsterAI/pie_check_dragon_boss.py` | PIE에서 `TDSpawnMonster MountainDragon` → 체력을 66·38·24%·0으로 깎으며 페이즈 1→2→3·비행 구간·졸개 3·착지·업화·처치·위험 공격 동시 수를 관측, 단계 스크린샷 7장 → `Docs/MonsterAI_CombatSim/measurements/M3-35/pie-<날짜>/report.json`. 플레이어 최대 체력 1000으로 시험용 설정. 종료 0=전부 통과 | `python Tools/MonsterAI/pie_check_dragon_boss.py [--map ...] [--distance 900]` (PowerShell) |
| `MonsterAI/editor_place_monsters.py` | LV-Cambat에 몬스터 무리 7개(25마리) 배치·저장. 이전 `TDGen_Monster_` 액터는 지우고 다시 만듦 | `python Tools/run_in_editor.py Tools/MonsterAI/editor_place_monsters.py` (PowerShell, PIE 종료·미저장 없음 필요) |
| `MonsterAI/pie_check_monsters.py` (+`editor_pie_monster_probe.py`) | PIE 켜고 하이에나 무리 옆으로 이동 → 교전·예고·타격·경직·사망 기록 → `Docs/MonsterAI_CombatSim/measurements/monster-pie-check.json`(+스크린샷), 판정 `passed` | `python Tools/MonsterAI/pie_check_monsters.py` |
| `MonsterAI/pie_check_tactics.py [--suite behavior\|crowd\|all] [--wall-x X --corridor-x X --corridor-y Y] [--offline]` (+`editor_pie_tactics_probe.py`, PowerShell, 에디터 앞창) | 무리 전술층(M3-14·M3-28) PIE 검증. behavior: `Tactics.Debug 1` + 탑다운 카메라(팔 1800), 고블린 10 포위 → 배치 25마리 끌어오기·서 있기 → 원 카이팅(이동 입력) → 걸어서 임시 벽·복도로 이동 후 현지 고블린 스폰(저장 없이 `TDProbe_Tactics_*` 3개, 끝나면 삭제) → 반격, 3프레임마다 `GetMonsterDebugLines`·`GetTacticsDebugLines` 표본, 게임 뷰포트 `Shot`(PIE 컨트롤러 경유라 디버그 선 포함, L-editor-31) 5장. crowd: `TDSpawnMonster` 20줄로 300마리 → `Trace.File cpu,frame` 15초 전술 켬/끔 → UnrealInsights 명령줄로 `TDMonsterAI_*` 스텝·프레임 비용(메모리 꼬리 잘라냄, L-editor-32). 플레이어 체력 50% 미만이면 채움. 끝나면 LV-Cambat이 미저장(임시 벽) 상태이니 저장하지 말고 `ue_editor.py stop --force` 또는 그대로 둔다(맵 전환 금지 L-editor-30) → `Docs/MonsterAI_CombatSim/measurements/squad-tactics/pie-<날짜>/report.json`·PNG·1Hz 표본, 원본 `Saved/MonsterAI/TacticsPie/` | `python Tools/MonsterAI/pie_check_tactics.py` |
| `MonsterAI/tactics_pie_analysis.py <표본.jsonl> [--out 파일]` (에디터 불필요) | 위 표본에서 장면별 동시 공격 대 토큰 예산·토큰 없는 시전, 대기 고리 포위(최대 각 간격·사분면)·표류 각폭·위협 발짝, 막힌 칸 수·대기 칸 바뀜·끼임, 디렉터 박자 순환, 몬스터별 최장 무공격 간격 | `python Tools/MonsterAI/tactics_pie_analysis.py Saved/MonsterAI/TacticsPie/samples_behavior.jsonl` |
| 콘솔 `TD.MonsterAI.Reload` / `TD.MonsterAI.DebugDraw 1` | JSON 정의 다시 읽기(이후 스폰분부터) / 몬스터 머리 위 상태·행동 표시 | PIE 콘솔 |
| 자동화 테스트 `TDGame.MonsterAI.*` 7개 | 곡선·정의 검증·오류 안내·점수기·예고 후 타격·경직 취소·전역 난수 금지 | `python Tools/check_automation_tests.py --filter TDGame.MonsterAI` |
확인: 2026-09-24 claude

| `MonsterAI/editor_make_boss_arena.py` | M3-36 원형 보스 아레나 `/Game/MonsterAI/Maps/LV_TDBossArena`(반경 1400 바닥·링 벽 32·입구/출구 복도·봉쇄문·출구문·`ATDArenaEncounter`·내비)와 조우 정의 `/Game/MonsterAI/Encounters/DA_TDArena_DragonLair`(웨이브 4: 졸개 2·중간 보스·보스) 생성. 맵이 있으면 정의 웨이브만 갱신, 종 대역은 파일 위 `SPECIES` 표에서 교체 | `python Tools/run_in_editor.py Tools/MonsterAI/editor_make_boss_arena.py` (PowerShell, PIE 종료·미저장 없음 필요, 새 C++ 클래스라 에디터 재시작 뒤) |
## 3e. 이동·애니메이션 도구 (`Movement/`, 대장 M3-16, 결정 D44)
| 도구 | 용도 | 실행 |
|---|---|---|
| `Movement/editor_migrate_character_assets.py` | Mover·UAF 전환 뒤 플레이어 블루프린트 메시의 AnimClass 참조 제거(저장 → 컴파일 → 저장: Mover 공유 설정은 저장 때 채워진다), 참조 없는 옛 ABP·몽타주 삭제 | `python Tools/run_in_editor.py Tools/Movement/editor_migrate_character_assets.py` (PowerShell) |
| `Movement/pie_check_player_movement.py` (+`editor_pie_player_probe.py`) | 내비메시가 있는 LV_TDMegaMagicArena를 열고 에디터 내비 준비(RecastNavMesh 존재·빌드·잠금 끝·PlayerStart와 목표점 투영, 연속 2회, 최대 300초, 미준비면 종료 코드 2)를 기다린 뒤 PIE → 클릭 이동 도착 오차·방향·발 뼈 움직임, 점프 높이, `TDPlayMeleeAction` 루트모션 이동량을 매 프레임 기록 → `Docs/Validation/Movement/player-pie-check.json`, 판정 `passed` | `python Tools/Movement/pie_check_player_movement.py` |
| 콘솔 `TDPlayMeleeAction [시퀀스 경로]` | 플레이어 폰에 공격 시퀀스를 UAF 행동(루트모션)으로 재생(기본 `AS_TD_Player_SwordAttack01`) | PIE 콘솔 |
| 자동화 테스트 `TDGame.Movement.*` 3개 + `TDGame.Combat.ActionTimelineHitsOnceInWindow` | 루트모션 36±2cm, 빙결 정지, 몬스터 이동 의도, 시간표 타격 1회 | `python Tools/check_automation_tests.py --filter TDGame.Movement` 또는 헤드리스 |
확인: 2026-09-30 claude

## 3f. Cascadeur 제작 도구 (`Cascadeur/`, 대장 AnimationAuthoring_Tasks H-01, 스킬 td-cascadeur-animation)
Cascadeur 2026.2.3 내장 MCP 스크립트 서버(`http://127.0.0.1:8765/mcp`, 도구 `run_script`)를 MCP 설정 5벌에 `cascadeur`로 등록했다. Cascadeur가 켜져 있어야 연결된다. 접두어 `csc_` = Cascadeur 안 Python(`csc_scripts/`, 브리지가 불러 씀). Free(basic) 라이선스는 FBX 내보내기가 막혀 `export` 단계에서 멈춘다(L-anim-08).
| 도구 | 용도 | 실행 |
|---|---|---|
| `Cascadeur/setup_cascadeur.py` | 사용자 settings.json에 `csc_scripts`·`td_events` 등록 → Cascadeur 시작 때 MCP 서버 자동 기동(재실행 안전, 1회 백업, L-anim-09) | `python Tools/Cascadeur/setup_cascadeur.py [--check]` |
| `Cascadeur/cascadeur_bridge.py` | Cascadeur 실행·재시작·상태, `csc_*` 함수 호출(JSON 결과, 30초 넘는 작업 대기) | `python Tools/Cascadeur/cascadeur_bridge.py health\|ensure\|restart\|call <모듈>.<함수> --args-file <JSON>` |
| `Cascadeur/build_motion_keys.py` | 동작 명세(`*.motion.json`) → FK·2관절 IK·손목 인지 칼 쥐기 탐색 → 리그 점·손가락·루트 키 JSON과 보고 | `python Tools/Cascadeur/build_motion_keys.py describe\|solve --rest <기준 자세> --weapon Tools/Cascadeur/weapons/SM_Sword.json [--spec … --out …]` |
| `Cascadeur/build_cascadeur_animation.py` | 단계 실행: character(UE FBX·퀵 리깅·기준 자세) → keys → scene(.casc) → capture(3방향) → export(라이선스 확인) → import(UE AnimSequence 루트모션) | `python Tools/Cascadeur/build_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json [--stages …] [--replace]` (PowerShell) |
| `Cascadeur/check_cascadeur_animation.py` | Cascadeur가 푼 관절 + 칼 → 칼끝 궤적·속도·회전 방향·정면 통과·발 미끄러짐·루트 수치, 막대 인형 GIF·시트 | `python Tools/Cascadeur/check_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json` |
| `Cascadeur/csc_scripts/` | `csc_character`(가져오기·퀵 리깅), `csc_motion`(키·보간·탭·AI 모션 생성), `csc_capture`(렌더), `csc_export`(FBX), `csc_inspect`(읽기), `csc_runtime`(작업 실행기), `td_events/scene_idle/td_autostart_mcp.py` | 브리지 `call`로만 |
| `Cascadeur/weapons/SM_Sword.json` | HandGrip_R 소켓·칼 부착값·칼 특징점(쥐기 좌표의 정본, L-anim-05) | 데이터 |
확인: 2026-10-08 claude

## 3g. 전투 표현 도구 (`Combat/`, 대장 M3-27)
| 명령 | 내용 | 상태 |
|---|---|---|
| `python Tools/Combat/generate_feedback_sounds.py [--out AudioSources/Combat]` (에디터 밖, numpy) | 치명 강조음·처치 쿵·쓰러짐·묵직한 타격음 WAV 4개 결정론 합성(같은 실행 = 같은 바이트) | 현행 (2026-10-18) |
| `python Tools/run_in_editor.py Tools/Combat/editor_make_combat_assets.py` (PowerShell, `TD_STEPS` 로 단계 선택) | 합성음 가져오기 `/Game/Combat/Feedback/Audio`, 번쩍임 오버레이 `M_TD_HitFlashOverlay`(FlashColor·FlashIntensity), 입력 `IA_TDRoll`·`IMC_TDCombat`(스페이스), 콤보 4개·피격 애니 FBX 가져오기(60fps), 있으면 건너뜀 | 현행 (2026-10-18) |
| `python Tools/run_in_editor.py Tools/MonsterAI/editor_make_monster_species.py` (표의 `hit` 항목) | 종 자산의 Hit 클립을 표에서 연결(옛 `Combat/editor_set_species_hit_clips.py` 삭제, 스켈레톤 호환은 종마다 로그) | 현행 (2026-10-18, M3-33) |
| `Tools/MonsterAI/editor_retarget_monster_anims.py` + `TD_ONLY_CLIPS=['HitReact']` | 맨손 피격을 고블린·골렘·해골 경비병으로 리타깃(그 클립만, 바뀐 에셋만 저장, `TD_ONLY_TARGETS`로 대상 선택 가능) | 현행 (2026-10-18 확장) |
| `Tools/BlenderAnimation/CombatHit/` 9개(`hit_scene`·`hit_author`·`hit_review`·`hit_export`·`hyena_author`·`hyena_review`·`hyena_export`·`shield_attach`·`roundtrip`, `blender_run.py`로 실행) | 피격 모션 저작: 원본 MM_HitReact 모캡 몸통·다리 보존 + 칼·방패/맨손 팔 교체, 하이에나 0.45초 반동, 측정·시트·FBX 내보내기·왕복 검증, 방패 부착값 계산 | 현행 (2026-10-18) |

## 4. 도구가 없을 때 만드는 규칙 (에이전트 공통)
규칙 전문은 `Docs/AgentRules.md` OP-12(만들기 전 검색)·OP-13(위치)·OP-14(이름)·OP-15(docstring 4줄)·OP-16(등록)·OP-17(`Tools/scratch/`)·OP-19(절대 경로 금지)·OP-20(노출 경로). 접두어: `editor_`(에디터 안, run_in_editor.py), `pie_`(PIE 검사), `blender_`(Blender 안 bpy), 없음(시스템 파이썬 3.12). 권장 동사: generate build make validate check inspect capture export import author render prepare place setup select find. 금지 동의어: create→make, bake→build, verify/test→validate/check.
1. 먼저 에디터 Python으로 되는지 확인한다: `python Tools/run_in_editor.py -c "import unreal; print([n for n in dir(unreal) if 'Landscape' in n][:20])"`.
2. 되면 `templates/editor_tool_template.py`를 복사해 `Tools/<영역>/editor_<동사>_<대상>.py`로 만든다. 생성물 라벨 `TDGen_`, 폴더 지정, 마지막에 한 번 저장.
3. Python에 API가 없으면 `templates/cpp_editor_function_template.md` 절차로 `Source/TDGameEditor`에 C++ 함수를 추가하고 `python Tools/ue_editor.py restart`.
4. 오프라인 계산(생성·검증)은 `Tools/<영역>/<동사>_<대상>.py`(numpy)로 만들고 시드 결정론을 지킨다.
5. 만든 도구는 같은 세션에 이 README 표에 한 줄 추가하고(OP-16), 검증 캡처·로그를 `Docs/Validation/`에 남긴다(OP-24). 실험·1회용은 `Tools/scratch/<YYYYMMDD>_<agent>_<주제>/`(git 제외).

## 5. 실측된 함정
실측 함정은 `Docs/Lessons/`로 옮겼다(L-build-01·02, L-editor-01~05, L-repo-01~06, L-anim-01). 새 함정은 그곳에 `### L-<영역>-<번호>` 항목으로 적고(OP-26), 명령이 실패하면 먼저 `rg -n "<오류 문구>" Docs/Lessons Tools/README.md`를 실행한다(OP-27).

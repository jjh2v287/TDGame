[← 인덱스로](../AgentCollaboration_Plan.md)
# 01. 현재 상태 감사 (2026-09-18)

종류: 리서치 · 작성: 2026-09-18 claude(다중 에이전트 워크플로 결과를 정리) · 웹 사실은 URL과 확인 날짜, 저장소 사실은 경로 기준

## 도구 자산

C:\Project\TDGame의 도구 자산은 파이썬 97개(Tools 82 + .gemini/scripts 13 + Content/Python 2), PowerShell 3, bash 1, C++ 에디터 라이브러리 4계열, SKILL.md 2, 템플릿 2로 확인됐다. Tools/README.md 표에 등록된 스크립트는 33개(약 34%)이며 나머지는 미등록이거나 Docs/*.md 산문에만 흩어져 있다. 가장 심각한 구조 문제는 (1) 공용 규칙 AGENTS.md에 Tools/README.md·run_in_editor.py 등 도구 진입점 언급이 전혀 없고 GEMINI.md에만 있다는 점, (2) 저장소 밖 Claude 전용 폴더(~/.claude/projects/C--Project-TDGame/tools/, 14개)에 도구가 존재하며 저장소 스크립트 capture_views_mcp.py가 그 경로를 하드코딩해 의존한다는 점, (3) __pycache__/*.pyc 15개와 smoke-report.json이 커밋됐고 루트 .gitignore에 파이썬 생성물 규칙이 없다는 점, (4) MCP 클라이언트·화면 캡처·FBX 교환·월드 빌드가 각각 2~4벌 중복 구현돼 있고 .gemini/scripts와 Tools/Animation은 현재 파이프라인과 모순되거나 미참조라는 점이다. 실행 방식은 다섯 가지(에디터 안 Python, 바깥 Python, 바깥 MCP 클라이언트, Blender 안 bpy, PowerShell/bash)인데 파일명 접두어 규칙은 editor_*만 있고 WorldGen/DungeonGen에서만 잘 지켜진다. 세 애니메이션 폴더는 세대가 다른 세 접근(헤드리스 Blender+Kimodo → Unreal MCP C++ 툴셋 → Blender MCP bpy)이 공존하며 BlenderAnimation이 AnimationAuthoring/smoke_test.py의 클래스를 라이브러리로 import한다.

### 발견 사항
| 심각도 | 영역 | 사실 | 문제 | 근거 |
|---|---|---|---|---|
| high | 진입점·등록 | AGENTS.md에는 'Tools'라는 문자열이 140행(uproject 플러그인 설명) 한 곳뿐이며 Tools/README.md, run_in_editor.py, ue_editor.py, templates/ 언급이 없다. 도구 목록·세션 시작 절차는 GEMINI.md('도구 목록과 사용법은 Tools/README.md', '세션 시작 절차')에만 있다. | 사용자 요구 (a) '모든 에이전트가 세션 시작 때 AGENTS.md를 읽고 도구를 안다'가 구조적으로 불가능하다. Claude·Codex는 AGENTS.md를 읽어도 Tools/README.md 존재를 모른다. 공통 규칙에 도구 등록표 경로와 세션 시작 명령을 넣어야 한다. | grep -n "Tools" C:\Project\TDGame\AGENTS.md → 140행 1건; C:\Project\TDGame\GEMINI.md 12~22행; C:\Project\TDGame\Tools\README.md 7~10행(세션 점검) |
| high | 등록표 누락 | Tools/README.md 표에 등록된 스크립트 33개(루트 5, WorldGen 22, DungeonGen 6). 미등록: Tools/Animation 11, AnimationAuthoring 6, BlenderAnimation 19, BlenderMCP 6, Content/Python 2, .gemini/scripts 13, 루트 check_orphan_external_actors.py·find_unused_content.py·editor_regenerate_pcg.py, WorldGen/editor_capture_views.py·editor_cpp_world_layout.py·editor_place_generator_actors.py, DungeonGen/editor_relink_room_modules.py. | README §4-5 '만든 도구는 이 README 표에 한 줄 추가' 규칙이 9-17 애니메이션 작업(4df0823 커밋)에서 전혀 지켜지지 않았다. 등록표가 하나가 아니라 README 표 + 하위 README + Docs 산문 + SKILL.md로 분산돼 에이전트가 존재를 알 방법이 없다. 등록을 강제하거나 자동 검사하는 결정론적 스크립트가 필요하다. | C:\Project\TDGame\Tools\README.md 전체(grep 'Animation\|Blender\|Content/Python\|check_orphan\|find_unused\|editor_regenerate_pcg' → 0건); 애니메이션 도구는 C:\Project\TDGame\Docs\BlenderAnimationWorkflow.md 143·149행, Docs\AnimationAuthoring.md 표, Docs\AnimationQuality.md 59행 산문에만 일부 언급 |
| high | 저장소 밖 도구 의존 | Claude 전용 폴더 C:\Users\jjh\.claude\projects\C--Project-TDGame\tools\ 에 14개 스크립트가 있다(uemcp.py는 Tools/uemcp.py와 바이트 동일, uepy.py는 run_in_editor.py 전신, run_tests.py 자동화 테스트 러너·save_editor_capture.py·make_montage.py 등은 저장소에 대응물 없음). Tools/WorldGen/capture_views_mcp.py 15행이 이 경로를 sys.path.insert로 하드코딩하고 import uemcp 하며, editor_capture_views.py 3행은 존재하지 않는 tools/save_editor_capture.py를 가리킨다. | Codex·Gemini·다른 PC에서는 README §2에 등록된 capture_views_mcp.py가 ModuleNotFoundError로 실패한다. 도구는 저장소 안에만 두고 sys.path는 __file__ 기준 상대 경로만 허용하는 규칙이 필요하다(pie_p1_checks.py 257~259행이 올바른 예). | C:\Project\TDGame\Tools\WorldGen\capture_views_mcp.py:15; C:\Project\TDGame\Tools\WorldGen\editor_capture_views.py:3; ls C:\Users\jjh\.claude\projects\C--Project-TDGame\tools; cmp 결과 IDENTICAL; import 재현 시 uemcp가 Claude 폴더에서 로드됨 |
| high | 생성물 커밋 | git에 __pycache__/*.pyc 15개(Tools 루트 3, WorldGen 3, DungeonGen 2, AnimationAuthoring 3, Animation 1, .gemini/scripts 2; 그중 1개는 cpython-311 = 에디터 내장 Python이 저장소에 쓴 것)와 Tools/AnimationAuthoring/smoke-report.json이 추적된다. 루트 .gitignore에 __pycache__/·*.pyc 규칙이 없고 Content/Python/.gitignore와 Tools/BlenderMCP/.gitignore에만 있다. | 실행 흔적이 커밋되어 diff가 오염되고 에디터 Python 버전별 .pyc가 섞인다. 루트 .gitignore에 파이썬 생성물 규칙을 추가하고 추적 해제해야 하며, 스모크 리포트 같은 실행 결과의 보관 위치(Saved/ vs Docs/Validation/)를 규칙으로 정해야 한다. | git ls-files \| grep pyc (15건); C:\Project\TDGame\.gitignore(C++/C#/UE 항목만); C:\Project\TDGame\Content\Python\.gitignore; C:\Project\TDGame\Tools\BlenderMCP\.gitignore |
| high | 기능 중복 | 같은 기능이 여러 벌: (1) Unreal MCP 클라이언트 3벌 + 래퍼 1(Tools/uemcp.py 함수형, .gemini/scripts/unreal_mcp.py 클래스형 UnrealMcpClient, Claude 폴더 uemcp.py 동일본, AnimationAuthoring/smoke_test.py의 TDMcpAnimationClient); (2) 화면 캡처 4벌(capture_views_mcp.py CaptureViewport, editor_capture_views.py, .gemini/take_screenshot.py Win32 창 캡처, Claude save_capture.py); (3) FBX 교환 3벌(Content/Python/td_blender_animation_tools.py 툴셋, Tools/Animation/ue_blender_anim_bridge.py, Tools/Animation/step1_*·step3_*); (4) 월드 빌드: .gemini/scripts build_*(6)·populate_*(3)·setup_real_landscape.py가 Tools/WorldGen 파이프라인과 같은 레벨 LV_DarkFantasy_OpenWorld를 대상으로 하며, editor_build_open_world.py는 KEEP_CLASSES 외 모든 액터를 지우고 재생성한다. | 에이전트마다 자기 폴더에 클라이언트를 다시 만들었다. 공용 라이브러리 위치(Tools/ 루트)와 '기존 도구 검색 후 생성' 절차, 대체된 도구의 폐기·이동 규칙이 없다. .gemini/scripts 월드 빌드와 WorldGen 베이크를 섞어 실행하면 서로의 결과를 지운다. | C:\Project\TDGame\Tools\uemcp.py; C:\Project\TDGame\.gemini\scripts\unreal_mcp.py 8~65행; C:\Project\TDGame\Tools\AnimationAuthoring\smoke_test.py 28~72행; C:\Project\TDGame\Tools\WorldGen\editor_build_open_world.py 7~9행(LEVEL_PATH, KEEP_CLASSES); C:\Project\TDGame\Tools\README.md 78행(CaptureViewport만 신뢰); C:\Project\TDGame\Tools\WorldGen\README.md 36행 |
| high | 모순·미참조 폴더 | Tools/Animation 11개는 저장소 어디에서도 참조되지 않고(Docs·AGENTS·README grep 0건), 전부 UTF-8 BOM·모듈 docstring 없음. kimodo_ue_processor.py는 외부 text-to-motion 모델(C:\Tools\kimodo\.venv 하드코딩)→BVH→blender.exe→UnrealEditor-Cmd -run=pythonscript 파이프라인인데, Docs/BlenderAnimationWorkflow.md 7행과 BlenderAnimation/SKILL.md·AnimationAuthoring/SKILL.md는 'text-to-motion 서비스를 설치·가정하지 않는다'고 명시한다. step1_export_*·step3_import_*·test_import_kimodo·test_load·check_*는 경로가 하드코딩된 1회용이며 test_import_kimodo.py와 step3_import_kimodo_manny.py는 같은 내용이다. | 에이전트가 이 폴더를 읽으면 현재 정책과 반대 방향(외부 모션 생성)을 따를 수 있다. 실험 폴더의 수명 규칙(예: Saved/Experiments 또는 Tools/_archive, 30일 후 삭제)과 '실험 스크립트는 docstring에 실험 표시'를 정해야 한다. | C:\Project\TDGame\Tools\Animation\kimodo_ue_processor.py 9~12행, 63행; C:\Project\TDGame\Docs\BlenderAnimationWorkflow.md 7행; C:\Project\TDGame\Tools\BlenderAnimation\SKILL.md 3·8행; BOM 검사 9/9 파일; git log → 4df0823(2026-09-17) 단일 커밋 |
| medium | 실행 방식 접두어 | 실행 방식 5종이 공존한다: 에디터 안 Python(run_in_editor 경유, import unreal) 30개, 바깥 Python(numpy 등) 12개, 바깥 MCP 클라이언트 Python 약 25개(.gemini 포함), Blender 안 bpy(call_tool.py --code 또는 blender.exe --python) 16개, PowerShell 3·bash 1. 접두어 규칙은 'editor_*.py'(README 4행) 하나뿐이며 WorldGen/DungeonGen/루트에서는 준수하지만 pie_*.py 6개(에디터 안 PIE), BlenderAnimation/validate_weighty_pie.py(import unreal), Tools/Animation step1_*·step3_*·test_*·check_*(import unreal)는 접두어가 없다. Blender 안 스크립트와 바깥 MCP 스크립트는 BlenderAnimation 한 폴더에 섞여 있고 이름으로 구분되지 않는다. 셸도 갈린다: AnimationAuthoring 3개는 'PowerShell 에서 실행할 것(Git Bash는 /Game/ 경로를 바꾼다)', rebuild_all.sh는 bash 전용. | 파일명만 보고 어디서 실행할지 알 수 없어 에이전트가 잘못된 인터프리터로 실행하거나 실행 방법을 찾느라 토큰을 쓴다. 접두어 체계를 editor_/pie_/blender_/mcp_ 등으로 확장하고 docstring 첫 줄 형식('에디터 안에서 실행:' 등)과 함께 규칙화해야 한다. | C:\Project\TDGame\Tools\README.md 4행; C:\Project\TDGame\Tools\WorldGen\pie_check.py 1행; C:\Project\TDGame\Tools\BlenderAnimation\validate_weighty_pie.py 6행; C:\Project\TDGame\Tools\AnimationAuthoring\author_swing.py 2~3행; C:\Project\TDGame\Tools\WorldGen\rebuild_all.sh; C:\Project\TDGame\Docs\BlenderAnimationWorkflow.md 51~55행(call_tool.py --code) |
| medium | SKILL.md 형식 | Agent Skills 표준 frontmatter(name/description)를 쓰는 곳은 Tools/AnimationAuthoring/SKILL.md(name: td-animation-authoring, references/workflow.md 190행 동반)와 Tools/BlenderAnimation/SKILL.md(name: td-combat-animation-quality) 2곳뿐이다. WorldGen·DungeonGen은 README.md, BlenderMCP·Animation·루트는 안내 문서가 없다. 폴더 이름과 스킬 name이 다르고, 표준 탐색 위치(.claude/skills, .codex/skills, .agents/skills)가 프로젝트에 없어 Docs/AnimationAuthoring.md 26행처럼 '~/.codex/skills 로 복사' 수동 절차나 경로 명시 프롬프트가 필요하다. 두 SKILL.md 모두 폴더 안 스크립트 목록을 갖지 않는다. | 스킬이 자동 발견되지 않아 세 에이전트 공통 진입이 안 된다. 폴더당 안내 파일 형식(SKILL.md vs README.md)과 위치, 스킬 name = 폴더 이름 규칙, SKILL.md에 '스크립트 표' 필수 섹션을 정해야 한다. | C:\Project\TDGame\Tools\AnimationAuthoring\SKILL.md 1~4행; C:\Project\TDGame\Tools\BlenderAnimation\SKILL.md 1~4행; ls .claude .codex/skills .agents → 없음; C:\Project\TDGame\Docs\AnimationAuthoring.md 20·26행 |
| medium | 애니메이션 폴더 3개 | 역할: Tools/Animation = 1세대(헤드리스 blender.exe --python + Kimodo, Saved/TempAnim, 미참조); Tools/AnimationAuthoring = 2세대(Unreal MCP의 C++ UToolsetDefinition 툴셋 TDAnimationAuthoringTools·TDControlRigTools·TDSequencerAnimationTools로 본 키프레임·몽타주 저작, 순수 계산 pose_kinematics.py, 덤프·리포트); Tools/BlenderAnimation = 3세대(Blender MCP execute_blender_code로 bpy 저작, Content/Python 툴셋으로 FBX 교환, Unreal에서 수치·PIE 검증). BlenderAnimation의 import_player_sample.py·create_player_montage.py·validate_*.py 5개가 AnimationAuthoring/smoke_test.py에서 TDMcpAnimationClient를 import한다(테스트 파일이 공용 라이브러리 역할). 중복: blender_author_sword_slash.py ≈ author_player_slash.py, ue_blender_anim_bridge.py ≈ td_blender_animation_tools.py. | 세 폴더가 세대 순서와 현재 유효 여부를 표시하지 않아 어느 것을 써야 하는지 문서 3개(AnimationAuthoring.md, BlenderAnimationWorkflow.md, AnimationQuality.md)를 다 읽어야 안다. 공용 클라이언트를 smoke_test.py에서 분리해 Tools/ 루트 라이브러리로 올리고, 폴더별 상태(현행/보류/폐기)를 등록표 열로 두어야 한다. | C:\Project\TDGame\Tools\BlenderAnimation\validate_player_sample.py 6~7행; C:\Project\TDGame\Tools\AnimationAuthoring\smoke_test.py 28행; C:\Project\TDGame\Docs\BlenderAnimationWorkflow.md 12~19행 구성표; C:\Project\TDGame\Source\TDGameEditor\Animation\TDAnimationAuthoringTools.h 7~22행 |
| medium | 버전 복제 패턴 | BlenderAnimation은 후보 버전마다 스크립트를 복제한다: author_weighty_slash.py(v04 하드코딩) → author_sword_grip.py(v05), render_weighty_preview.py → render_grip_review.py, validate_weighty_slash.py → validate_grip_revision.py. 미커밋 5개가 모두 이 v05 세대다. 19개 중 docstring이 있는 파일 0개, argparse 0개, 절대 경로 root = Path('C:/Project/TDGame') 하드코딩 다수. | 파라미터화된 재사용 도구가 아니라 실행 기록에 가깝다. 이런 파일은 도구가 아니라 '작업 스크립트 기록'으로 분류해 Saved/ 또는 AnimationSources/<이름>/scripts/에 산출물과 함께 두고, 재사용 로직만 Tools/에 남기는 분리 규칙이 필요하다. | C:\Project\TDGame\Tools\BlenderAnimation\author_sword_grip.py 8~10행; author_weighty_slash.py 8~10행; git status → ?? 5개; SKILL.md 15행('특정 캐릭터의 예제') |
| medium | 산출물 위치 | 도구 산출물 위치가 셋이다: Saved/WorldGen·DungeonGen·BlenderAnimation·Inspect(gitignore, 휘발), Docs/Validation/(커밋, 55MB, validate_*.py가 직접 씀, README.md는 옛 GAS 내용), AnimationSources/Player/(커밋 26MB, .blend/.fbx LFS). smoke-report.json은 Tools/ 안에 커밋. | 무엇을 커밋하고 무엇을 Saved/에 둘지 기준이 없어 저장소가 커진다. '검증 증거 = Docs/Validation/<과제ID>-…(md + 소형 json, 이미지는 LFS)', '중간물 = Saved/'로 규칙화하고 Docs/Validation/README.md를 인덱스로 갱신해야 한다. | du -sh → Docs/Validation 55M, AnimationSources 26M; C:\Project\TDGame\Tools\BlenderAnimation\validate_grip_revision.py(Docs/Validation/BlenderAnimation 출력); C:\Project\TDGame\Docs\Validation\README.md 1~12행; C:\Project\TDGame\.gitattributes(png/gif/fbx/blend LFS) |
| medium | .gemini/scripts | .gemini/scripts 13개는 모듈 docstring 0개, 등록 0, __pycache__ 2개 커밋, Gemini만 보는 위치. 6개는 '# File: .gemini/scripts/…' 헤더 관례를 쓰며 Tools/Animation/ue_blender_anim_bridge.py·kimodo_ue_processor.py도 같은 '# File:' 헤더를 쓴다(같은 작성 주체로 추정). check_editor_connection.py는 ue_editor.py status와, take_screenshot.py는 capture_views_mcp.py와 기능이 겹친다. | 에이전트별 전용 폴더에 도구를 두면 다른 에이전트는 존재를 모르고 다시 만든다. '에이전트 전용 폴더(.gemini/.codex/.claude)에는 설정만, 스크립트는 Tools/'를 규칙으로 정해야 한다. | C:\Project\TDGame\.gemini\scripts\*.py 1행; grep -l '^# File:' 결과; git ls-files .gemini |
| low | 템플릿 채택 | templates/editor_tool_template.py의 [TDTool] log 헬퍼는 editor 스크립트 21개 중 2개만 사용. 반면 docstring 첫 줄 '에디터 안에서 실행:' 22개, TDGen_ 라벨 8개는 정착. Blender 안·바깥 MCP·PowerShell 스크립트용 템플릿은 없다. | 템플릿이 실제 관례와 어긋나면 무시된다. 실제로 지켜지는 최소 형식(docstring 첫 줄 = 실행 위치·명령·출력)을 템플릿의 핵심으로 줄이고 실행 방식별 템플릿을 추가하는 편이 낫다. | grep 결과(TDTool 2/21, '에디터 안에서 실행' 22, TDGen_ 8); C:\Project\TDGame\Tools\templates\ 2개 파일 |
| low | 하드코딩 경로 | 절대 경로 하드코딩: C:\Project\TDGame(editor_build_open_world.py 7행, editor_relight.py 11~12행, editor_capture_views.py 6행, BlenderAnimation 13개 파일의 root = Path('C:/Project/TDGame')), UE_5.8 플러그인 경로(run_in_editor.py 11행), Blender 5.2 exe(Start-BlenderMCP.ps1 2행, Tools/Animation 2개), C:\Tools\kimodo(kimodo_ue_processor.py 9행). | 저장소 이동·다른 PC·에이전트 샌드박스에서 깨진다. 에디터 안에서는 unreal.SystemLibrary.get_project_directory()(editor_regenerate_pcg.py 9행, pie_*.py가 이미 사용), 밖에서는 __file__ 기준 ROOT 계산을 규칙으로 통일해야 한다. | 위 각 파일·행 |
| low | MCP 설정 5벌·노출 경로 4종 | MCP 클라이언트 설정이 .mcp.json·.codex/config.toml·.cursor/mcp.json·.gemini/settings.json·.vscode/mcp.json 5벌이며 모두 Tools/BlenderMCP/Run-BlenderMCP.ps1 절대 경로와 127.0.0.1:8000/mcp를 가리키고 AGENTS.md 141행이 수동 동기화를 요구한다(serena는 .mcp.json에만). 언리얼 기능 노출 경로도 4종: C++ UBlueprintFunctionLibrary(Python용, Landscape/WorldGen), C++ UToolsetDefinition+AICallable(MCP용, Animation), Python unreal.ToolsetDefinition+toolset_registry(Content/Python, init_unreal.py가 시작 시 등록), 에디터 Python 스크립트. README §4와 cpp 템플릿은 첫 번째만 안내한다. | 새 기능을 어느 경로로 노출할지 판단 기준이 문서에 없어 에이전트마다 다른 방식을 고른다. 설정 5벌은 생성 스크립트 하나로 동기화하는 편이 사용자의 '결정론적 작업은 프로그램으로' 원칙에 맞는다. | C:\Project\TDGame\.mcp.json; C:\Project\TDGame\.codex\config.toml; C:\Project\TDGame\AGENTS.md 141행; C:\Project\TDGame\Content\Python\td_blender_animation_tools.py 42~239행; C:\Project\TDGame\Source\TDGameEditor\Animation\TDAnimationAuthoringTools.h 7~12행; C:\Project\TDGame\Tools\templates\cpp_editor_function_template.md |

### 규칙으로 승격할 만한 기존 관례
- editor_*.py = 에디터 안에서 실행, 반드시 `python Tools/run_in_editor.py <파일>`(Tools/README.md 4행); WorldGen 16개·DungeonGen 4개·루트 2개가 준수 → 접두어 체계를 pie_/blender_/mcp_ 등으로 확장해 규칙으로 승격
- 모듈 docstring 첫 줄 형식 '에디터 안에서 실행: <한 줄 목적>' / '에디터 밖에서 실행' + '실행: python …' + '출력: Saved/…' (22개 준수, 예: Tools/WorldGen/editor_check_pcg_determinism.py 1~7행, Tools/ue_editor.py 1~11행) → 등록표 자동 생성의 근거로 삼을 수 있음
- 생성물 라벨 접두어 TDGen_ / 태그 TDGen / 아웃라이너 폴더 TDGen/… , 재실행 시 같은 라벨 액터·같은 경로 에셋을 지우고 재생성하는 멱등 패턴(editor_build_open_world.py, editor_make_pcg_biome.py 5행, editor_place_test_chest.py, templates/editor_tool_template.py)
- 오프라인 생성기 결정론 규약: numpy.random.default_rng([seed, 단계, 재시도]) 만 사용, generator_version 기록, 같은 인자→같은 해시, 종료 코드 0=통과/1=검증 실패/2=배치 실패(Tools/DungeonGen/README.md 16·20행, generate_dungeon.py 5행)
- 산출물 폴더 규약: 중간물·리포트는 Saved/<영역>/…(gitignore), 검증 증거는 Docs/Validation/<과제ID>-<이름>.md(+json, 이미지는 LFS)(Tools/README.md §4-5, WorldGen/README.md 40행, Docs/Validation/P3-07-pcg-biome.md)
- 영역 폴더마다 README.md에 실행 명령·파일 표·좌표 규격·검증 항목·한계·실측 함정 기록(Tools/WorldGen/README.md, Tools/DungeonGen/README.md) → SKILL.md와 통일할 형식의 원형
- 도구가 없을 때 3단계 승격: 에디터 Python 가능 여부 확인 → templates/editor_tool_template.py 복사 → C++ UBlueprintFunctionLibrary 추가 후 ue_editor.py restart(Tools/README.md §4, templates/cpp_editor_function_template.md)
- 실측 함정을 코드가 아니라 README 절에 축적(Tools/README.md §5, WorldGen/README.md 'PCG 그래프 스크립팅 실측') → 공유 교훈 기록의 씨앗
- 외부 의존성 고정: Tools/BlenderMCP는 업스트림 리비전 해시+zip SHA256+uv.lock+requires-python 고정, 폴더 자체 .gitignore(.venv/.runtime/__pycache__), 포트 소유 프로세스 검사(Start-BlenderMCP.ps1 12~19행)
- 기존 에셋·파일 덮어쓰기 금지·새 후보 이름 강제: 생성 스크립트가 같은 이름이 있으면 FileExistsError로 거부(author_weighty_slash.py 10행, author_sword_grip.py 10행), C++ 툴셋도 'without overwriting'(TDAnimationAuthoringTools.h 15·18행)
- 에디터 프로세스로 환경 변수가 전달되지 않으므로 단계 인자는 Saved/WorldGen/pie_travel_step.txt 같은 상태 파일로 전달(pie_*.py 3개, editor_regenerate_pcg.py 3행) — 실측 기반 우회 관례
- 셸 지정 명시: '/Game/…' 인수는 Git Bash가 경로로 바꾸므로 PowerShell에서 실행할 것을 docstring에 적음(Tools/AnimationAuthoring/author_swing.py 2~3행, dump_animation.py 2행) → 실행 셸을 docstring 필수 항목으로
- MCP 클라이언트 설정 5벌이 모두 같은 Run-BlenderMCP.ps1 절대 경로·같은 unreal-mcp 주소를 가리키고 AGENTS.md 141행이 동시 변경을 요구 → 설정 생성 스크립트로 결정론화 가능
- Agent Skills 표준 frontmatter(name/description)와 references/ 하위 문서 구조(Tools/AnimationAuthoring/SKILL.md, Tools/BlenderAnimation/SKILL.md)
- sys.path는 __file__ 기준 상대 계산(Tools/WorldGen/pie_p1_checks.py 257~259행, Tools/DungeonGen/batch_dungeons.py, BlenderAnimation validate_*.py 6행) — capture_views_mcp.py 15행만 예외

### 열린 질문
- Tools/Animation(Kimodo text-to-motion 파이프라인, 11개)은 폐기 대상인가? C:\Tools\kimodo가 실제로 설치돼 있는가? 현 정책(text-to-motion 미사용)과 모순되므로 사용자 결정 필요
- .gemini/scripts 13개(1세대 월드 빌드·식생 스폰)는 Tools/WorldGen 파이프라인으로 대체된 것으로 보이는데 삭제·보관·Tools/_archive 이동 중 무엇을 택할지
- Claude 전용 폴더(~/.claude/projects/C--Project-TDGame/tools/)의 run_tests.py(자동화 테스트 러너)·make_montage.py·save_editor_capture.py 등을 저장소 Tools/로 이관할지, 그리고 capture_views_mcp.py 15행 하드코딩을 즉시 고칠지
- pie_*.py 6개와 validate_weighty_pie.py를 editor_pie_*.py로 개명할지, 아니면 접두어 체계를 editor_/pie_/blender_/mcp_로 확장해 현행 이름을 인정할지
- Docs/Validation(55MB)·AnimationSources(26MB)·smoke-report.json의 커밋 정책: 어떤 산출물을 저장소에 남기고 어떤 것을 Saved/로 보낼지 기준
- SKILL.md 표준 위치(.claude/skills·.codex/skills·.agents/skills)를 채택할지, 세 에이전트 모두 자동 발견을 지원하는지는 별도 웹 조사 담당 결과에 의존
- '보류' 상태 스크립트(editor_setup_navmesh_wp.py, editor_capture_views.py)의 보관 규칙: 파일 안 표시·등록표 상태 열·폴더 이동 중 어느 것으로 할지
- Tools/AnimationAuthoring/smoke_test.py 안의 TDMcpAnimationClient를 Tools/ 루트 공용 라이브러리로 분리할 때 .gemini/scripts/unreal_mcp.py의 클래스형 인터페이스와 Tools/uemcp.py의 함수형 중 어느 API를 표준으로 삼을지
- Content/Python(Python ToolsetDefinition → MCP 노출)을 공식 4번째 노출 경로로 인정하고 README §4·템플릿에 추가할지, 아니면 C++ UToolsetDefinition으로만 제한할지
- Source/TDGameEditor/Tests의 실행 절차가 저장소 문서에 없고 Claude 메모리(tdgame-build-and-test-workflow)에만 있는데, 이를 Tools/README 또는 AGENTS.md로 옮길지

### 인벤토리
| 경로 | 종류 | 용도 | 등록 위치 | 중복 | 비고 |
|---|---|---|---|---|---|
| C:\Project\TDGame\Tools\README.md | 등록표(README) | 에이전트 공용 도구 표(§0 세션 점검, §1 범용, §2 WorldGen, §3 DungeonGen, §3b C++, §4 도구 생성 규칙, §5 실측 함정) | 자체 |  | GEMINI.md만 이 파일을 가리킴; AGENTS.md는 미언급. 애니메이션·BlenderMCP·Content/Python·.gemini 전부 미포함 |
| C:\Project\TDGame\Tools\run_in_editor.py | 바깥 Python(에디터 원격 실행 런처) | PythonScriptPlugin remote_execution으로 에디터 안에서 파일/-c 코드 실행, --ensure | Tools/README.md §1 | ~/.claude/projects/C--Project-TDGame/tools/uepy.py(전신, 저장소 밖) | UE_5.8 플러그인 경로 하드코딩(11행) |
| C:\Project\TDGame\Tools\uemcp.py | 바깥 Python(MCP 클라이언트 CLI·라이브러리) | Streamable HTTP MCP list/desc/call (정정 2026-09-18: SESSION_FILE 상수는 정의만 있고 쓰이지 않음, 호출마다 새 세션) | Tools/README.md §1 | .gemini/scripts/unreal_mcp.py(클래스형 동일 기능), ~/.claude/…/tools/uemcp.py(바이트 동일) |  |
| C:\Project\TDGame\Tools\ue_editor.py | 바깥 Python | 에디터 status/start/stop/build/restart/python on\|off/ensure | Tools/README.md §0·§1 | .gemini/scripts/check_editor_connection.py(status 부분) |  |
| C:\Project\TDGame\Tools\editor_inspect_level.py | 에디터 안 Python | 현재 레벨 액터 요약 JSON → Saved/Inspect/ | Tools/README.md §1 |  |  |
| C:\Project\TDGame\Tools\editor_regenerate_pcg.py | 에디터 안 Python | 레벨 PCG 컴포넌트 재생성(Saved/WorldGen/pcg_filter.txt 라벨 필터) | 미등록 |  |  |
| C:\Project\TDGame\Tools\check_orphan_external_actors.py | 바깥 Python(정리) | __ExternalActors__/__ExternalObjects__ 잔재 폴더 점검·삭제 | 미등록(Claude 메모리 tdgame-external-actors-cleanup.md에만) |  |  |
| C:\Project\TDGame\Tools\find_unused_content.py | 바깥 Python(정리) | 문자열 의존 그래프로 미참조 에셋 보고 | 미등록(Claude 메모리에만) |  |  |
| C:\Project\TDGame\Tools\tasks_recount.py | 바깥 Python(문서 정리) | Docs/Tasks/phase-*.md 상태 표 재계산 | Tools/README.md §3b(63행) |  |  |
| C:\Project\TDGame\Tools\templates\editor_tool_template.py | 템플릿(에디터 안) | editor_<동사>_<대상>.py 시작점([TDTool] log, setp, TDGen_ 라벨, 마지막 저장) | Tools/README.md §1·§4 |  | [TDTool] 헬퍼 채택 2/21 |
| C:\Project\TDGame\Tools\templates\cpp_editor_function_template.md | 템플릿(C++ 절차) | UBlueprintFunctionLibrary로 에디터 기능 노출 → ue_editor.py restart | Tools/README.md §1·§4 |  | UToolsetDefinition(MCP)·Python ToolsetDefinition 경로는 미안내 |
| C:\Project\TDGame\Tools\__pycache__\*.pyc (및 WorldGen·DungeonGen·Animation·AnimationAuthoring·.gemini/scripts __pycache__) | 생성물(커밋됨) | 파이썬 바이트코드 15개(cpython-312 14, cpython-311 1) | 해당 없음 |  | 루트 .gitignore에 규칙 없음 |
| C:\Project\TDGame\Tools\WorldGen\README.md | 하위 README | AshenVale 파이프라인 실행·규격·검증·PCG 스크립팅 실측 | Tools/README.md §2 |  |  |
| C:\Project\TDGame\Tools\WorldGen\generate_ashen_vale.py | 바깥 Python(numpy·scipy·PIL, 1295행) | 시드 결정론 높이맵·레이어·layout.json·preview.png → Saved/WorldGen/AshenVale/ | Tools/README.md §2 + WorldGen/README.md |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_build_open_world.py | 에디터 안 Python(354행) | layout.json을 LV_DarkFantasy_OpenWorld에 베이크(C++ 랜드스케이프, HISM, 조명, 저장); KEEP_CLASSES 외 전부 삭제 | Tools/README.md §2 + WorldGen/README.md |  | C:\Project\TDGame 절대 경로 기본값(7행) |
| C:\Project\TDGame\Tools\WorldGen\editor_make_landscape_material.py | 에디터 안 Python | M_TD_Landscape·MI·풀 타입 생성 | Tools/README.md §2 + WorldGen/README.md |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_relight.py | 에디터 안 Python | Saved/WorldGen/look.json으로 조명·노출·안개만 재적용 | Tools/README.md §2 |  |  |
| C:\Project\TDGame\Tools\WorldGen\validate_world.py | 바깥 Python(numpy·scipy) | R-91 여섯 항목 검증 → report.md·validation.json, 종료코드 0/1 | Tools/README.md §2 + WorldGen/README.md |  |  |
| C:\Project\TDGame\Tools\WorldGen\select_seed.py | 바깥 Python(서브프로세스) | 시드 후보 생성·검증·추천 → Saved/WorldGen/candidates.md | Tools/README.md §2 + WorldGen/README.md |  |  |
| C:\Project\TDGame\Tools\WorldGen\capture_views_mcp.py | 바깥 Python(MCP 클라이언트) | CaptureViewport로 지점별 PNG → Saved/WorldGen/Captures/ | Tools/README.md §2 |  | 15행 sys.path에 Claude 전용 폴더 절대 경로 하드코딩 → 다른 에이전트·PC에서 import uemcp 실패 |
| C:\Project\TDGame\Tools\WorldGen\editor_capture_views.py | 에디터 안 Python | 카메라 배치 후 밖에서 CaptureEditorImage 캡처(구 방식) | 미등록 | Tools/WorldGen/capture_views_mcp.py | README §5가 CaptureEditorImage를 불신; 3행이 존재하지 않는 tools/save_editor_capture.py 참조(Claude 전용 폴더에만 존재) |
| C:\Project\TDGame\Tools\WorldGen\editor_make_pcg_biome.py | 에디터 안 Python(508행) | P3-07 Forest 바이옴 PCG 그래프·볼륨 생성 | Tools/README.md §2 + WorldGen/README.md |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_check_pcg_determinism.py | 에디터 안 Python(틱 콜백) | 같은 시드 2회 생성 후 ISM 해시 비교 → Saved/WorldGen/pcg_determinism.json | Tools/README.md §2 + WorldGen/README.md |  |  |
| C:\Project\TDGame\Tools\WorldGen\rebuild_all.sh | bash | 생성→(머티리얼)→베이크→대기→캡처 한 줄 | Tools/README.md §2 |  | 유일한 bash 스크립트; AnimationAuthoring은 PowerShell 필수라 셸 혼재 |
| C:\Project\TDGame\Tools\WorldGen\editor_set_anchor_yaw.py | 에디터 안 Python | DA_TDWorld_Main 앵커 YawDeg 갱신(값 하드코딩) | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_setup_navmesh_dynamic.py | 에디터 안 Python | 내비메시 Dynamic 설정(채택 D-06) | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_setup_navmesh_wp.py | 에디터 안 Python | 월드 파티션 정적 내비메시 설정(보류) | Tools/README.md §3b |  | 보류 상태를 파일이 아니라 README 괄호로만 표시 |
| C:\Project\TDGame\Tools\WorldGen\editor_place_test_chest.py | 에디터 안 Python | 영속 테스트 상자 배치(P1-06) | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\WorldGen\pie_check.py | 에디터 안 Python(PIE 중) | 폰 위치·지면·내비 투영 출력 | Tools/README.md §2 |  | 에디터 안이지만 editor_ 접두어 없음 |
| C:\Project\TDGame\Tools\WorldGen\pie_walkin_check.py | 에디터 안 Python(PIE 중) | 입구 선로딩·이동 상자 왕복 흉내(P1-05), 단계는 Saved/WorldGen/pie_travel_step.txt | Tools/README.md §3b |  | 접두어 없음 |
| C:\Project\TDGame\Tools\WorldGen\pie_chest_check.py | 에디터 안 Python(PIE 중) | 영속 상자 open/status/save/load(P1-06/07) | Tools/README.md §3b |  | 접두어 없음 |
| C:\Project\TDGame\Tools\WorldGen\pie_travel_check.py | 에디터 안 Python(PIE 중) | 심리스 이동 왕복 콘솔 명령 시험(P1-04/08) | Tools/README.md §3b |  | 접두어 없음 |
| C:\Project\TDGame\Tools\WorldGen\pie_p1_checks.py | 바깥 Python(MCP + run_in_editor 서브프로세스) | PIE 켜고 P1-05/06/07 자동 수행 → Saved/WorldGen/pie_p1_checks.md | Tools/README.md §3b |  | sys.path를 __file__ 기준으로 계산하는 올바른 예(257~259행) |
| C:\Project\TDGame\Tools\WorldGen\editor_make_definitions.py | 에디터 안 Python | 월드·지역·바이옴·아틀라스·POI·테마·흐름 정의 데이터 에셋 생성 | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_place_dungeon_entrances.py | 에디터 안 Python | 슬롯별 필드 입구·던전 출구 배치(P1-05) | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\WorldGen\editor_cpp_world_layout.py | 에디터 안 Python | C++ 월드 그래프·도로 생성기·검증기 시험 → Saved/WorldGen/cpp_world_layout.md | 미등록(증거 Docs/Validation/cpp-world-layout-seed7.md) | Tools/DungeonGen/editor_bake_dungeons_cpp.py(월드 검증 부분) |  |
| C:\Project\TDGame\Tools\WorldGen\editor_place_generator_actors.py | 에디터 안 Python | 생성기 액터·슬롯 앵커 배치 후 버튼 함수 호출 확인 | 미등록 |  |  |
| C:\Project\TDGame\Tools\DungeonGen\README.md | 하위 README | 결정론 던전 생성기 사용법·규격·모듈 카탈로그·알고리즘·스키마·검증·한계 | Tools/README.md §3 |  |  |
| C:\Project\TDGame\Tools\DungeonGen\generate_dungeon.py | 바깥 Python(numpy·PIL, 862행) | 흐름 그래프→룸 조립→layout.json·preview.png·report.md, 종료코드 0/1/2 | Tools/README.md §3 + DungeonGen/README.md |  |  |
| C:\Project\TDGame\Tools\DungeonGen\validate_dungeon.py | 바깥 Python | layout.json 검증(validate() 함수 재사용) | Tools/README.md §3 + DungeonGen/README.md |  |  |
| C:\Project\TDGame\Tools\DungeonGen\batch_dungeons.py | 바깥 Python | 시드 범위 생성·검증·채점·추천 | Tools/README.md §3 + DungeonGen/README.md |  |  |
| C:\Project\TDGame\Tools\DungeonGen\editor_build_dungeon.py | 에디터 안 Python(216행) | Saved/DungeonGen/slot*/layout.json을 슬롯에 임시 지오메트리 베이크 | Tools/README.md §3 |  |  |
| C:\Project\TDGame\Tools\DungeonGen\editor_make_room_modules.py | 에디터 안 Python | Crypt 룸 모듈 12개 플레이스홀더 레벨 생성·테마 연결(P2-03) | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\DungeonGen\editor_bake_dungeons_cpp.py | 에디터 안 Python | C++ 라이브러리로 아틀라스 슬롯 전부 생성·검증·베이크 → Saved/WorldGen/cpp_dungeon_<id>.md | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Tools\DungeonGen\editor_relink_room_modules.py | 에디터 안 Python | 기존 룸 모듈 레벨을 테마 에셋에 재연결 | 미등록 |  |  |
| C:\Project\TDGame\Tools\Animation\ue_blender_anim_bridge.py | 에디터 안 Python(import unreal, 실패 시 None) + blender.exe --python 서브프로세스 | 메시 FBX 내보내기 → 헤드리스 Blender 애니 생성 → AnimSequence 임포트 | 미등록(저장소 내 참조 0) | Content/Python/td_blender_animation_tools.py(export_fbx/import_animation_fbx) | BOM, '# File:' 헤더, Blender 5.2 경로 하드코딩 |
| C:\Project\TDGame\Tools\Animation\kimodo_ue_processor.py | 바깥 Python(argparse; 외부 Kimodo venv·blender.exe·UnrealEditor-Cmd -run=pythonscript) | 텍스트→모션(Kimodo) BVH→FBX→UE 임포트 전체 파이프라인 | 미등록 |  | Docs/BlenderAnimationWorkflow.md 7행·두 SKILL.md의 'text-to-motion 미사용' 정책과 모순; C:\Tools\kimodo 하드코딩 |
| C:\Project\TDGame\Tools\Animation\blender_author_sword_slash.py | Blender 안 Python(blender.exe --python 헤드리스) | Saved/TempAnim FBX 임포트 후 spine/arm 키프레임 검 베기 생성·내보내기 | 미등록 | Tools/BlenderAnimation/author_player_slash.py |  |
| C:\Project\TDGame\Tools\Animation\step1_export_anim.py | 에디터 안 Python(1회용) | AS_Sword_Slash_01 → Saved/TempAnim FBX 내보내기(경로 하드코딩) | 미등록 |  | 실험 스크립트(step 번호), BOM, docstring 없음 |
| C:\Project\TDGame\Tools\Animation\step1_export_mesh.py | 에디터 안 Python(1회용) | SKM_Manny_Simple FBX 내보내기 | 미등록 |  | 실험 스크립트 |
| C:\Project\TDGame\Tools\Animation\step3_import_anim.py | 에디터 안 Python(1회용) | AS_Sword_Slash_Blender.fbx 임포트 | 미등록 |  | 실험 스크립트 |
| C:\Project\TDGame\Tools\Animation\step3_import_kimodo_manny.py | 에디터 안 Python(1회용) | Kimodo_Slash_Manny.fbx 임포트 | 미등록 | Tools/Animation/test_import_kimodo.py |  |
| C:\Project\TDGame\Tools\Animation\test_import_kimodo.py | 에디터 안 Python(1회용) | Kimodo_Slash.fbx 임포트 시험 | 미등록 | Tools/Animation/step3_import_kimodo_manny.py |  |
| C:\Project\TDGame\Tools\Animation\test_load.py | 에디터 안 Python(3행 연결 시험) | 에디터 Python 기동·메시 로드 확인 | 미등록 |  | 1회용 |
| C:\Project\TDGame\Tools\Animation\check_fbx_options.py | 에디터 안 Python(API 탐색) | FbxImportUI 속성 존재 확인 | 미등록 |  | 1회용 |
| C:\Project\TDGame\Tools\Animation\check_interchange.py | 에디터 안 Python(API 탐색) | InterchangeManager 메서드 나열 | 미등록 |  | 1회용 |
| C:\Project\TDGame\Tools\AnimationAuthoring\SKILL.md | SKILL(Agent Skills frontmatter name/description) | td-animation-authoring: Unreal MCP C++ 툴셋으로 키프레임·몽타주 저작 절차 | Docs/AnimationAuthoring.md 표 |  | 폴더 이름과 name 불일치; 스크립트 표 없음 |
| C:\Project\TDGame\Tools\AnimationAuthoring\references\workflow.md | SKILL 참고 문서(190행) | MCP 검색 모드·도구 스키마·베이크 절차 상세 | SKILL.md 15행 |  |  |
| C:\Project\TDGame\Tools\AnimationAuthoring\smoke_test.py | 바깥 Python(MCP 클라이언트; CLI + 라이브러리) | TD 애니 툴셋 스모크 테스트 → --output JSON; 클래스 TDMcpAnimationClient·unwrap 제공 | 미등록 |  | BlenderAnimation 5개 스크립트가 라이브러리로 import → 테스트 파일이 공용 클라이언트 역할 |
| C:\Project\TDGame\Tools\AnimationAuthoring\smoke-report.json | 생성물(커밋됨) | smoke_test.py 실행 결과 | 해당 없음 |  | Tools/ 안에 커밋된 실행 결과 |
| C:\Project\TDGame\Tools\AnimationAuthoring\dump_animation.py | 바깥 Python(짧은 에디터 코드를 생성해 run_in_editor로 전달) | AnimSequence 본 포즈 JSON 덤프(PowerShell 필수) | 미등록(자체 docstring만) |  |  |
| C:\Project\TDGame\Tools\AnimationAuthoring\anim_report.py | 바깥 Python(PIL) | 덤프 JSON의 손 속도 타이밍 프로파일·스틱 피겨 PNG | 미등록 |  |  |
| C:\Project\TDGame\Tools\AnimationAuthoring\author_slash.py | 바깥 Python(MCP/에디터 경유 생성, PowerShell 필수) | 기존 공격 애니 위에 오른팔만 검 궤적으로 재작성해 새 AnimSequence | 미등록 |  |  |
| C:\Project\TDGame\Tools\AnimationAuthoring\author_swing.py | 바깥 Python(PowerShell 필수) | 손·발 목표 위치 + pose_kinematics 역산으로 휘두르기 생성 | 미등록 |  |  |
| C:\Project\TDGame\Tools\AnimationAuthoring\pose_kinematics.py | 바깥 Python 라이브러리(순수 계산) | 레퍼런스 포즈 기반 본 위치·쿼터니언 계산(에디터 왕복 없음) | 미등록 |  | validate_grip_revision.py가 import |
| C:\Project\TDGame\Tools\BlenderAnimation\SKILL.md | SKILL(Agent Skills frontmatter) | td-combat-animation-quality: Blender MCP·Unreal MCP 전투 애니 품질 절차 | Docs/BlenderAnimationWorkflow.md 3행, Docs/AnimationQuality.md |  | 폴더 이름과 name 불일치; 스크립트를 '특정 캐릭터 예제'로 규정 |
| C:\Project\TDGame\Tools\BlenderAnimation\author_player_slash.py | Blender 안 Python(bpy; call_tool.py --code 경유; author(request) 함수) | 메시 FBX에서 제자리 횡베기 .blend/.fbx/.json 생성 | Docs/BlenderAnimationWorkflow.md 149행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\create_player_sample.py | Blender 안 Python(author_player_slash.py를 exec) | 샘플 요청 값으로 author() 호출 | Docs/BlenderAnimationWorkflow.md 149행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\import_player_sample.py | 바깥 Python(MCP, TDMcpAnimationClient) | TDBlenderAnimationTools.import_animation_fbx 호출 | Docs/BlenderAnimationWorkflow.md 149행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\create_player_montage.py | 바깥 Python(MCP) | TDAnimationAuthoringTools.CreateMontage 호출 | Docs/BlenderAnimationWorkflow.md 149행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\open_player_source.py | Blender 안 Python | 샘플 .blend 열기 | Docs/BlenderAnimationWorkflow.md 149행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\validate_player_sample.py | 바깥 Python(MCP) | sample_animation_poses로 Blender↔Unreal 오차 검증 → Docs/Validation/BlenderAnimation | Docs/BlenderAnimationWorkflow.md 143행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\render_preview.py | Blender 안 Python(render_preview 함수) | 포즈 프레임 미리보기 렌더 → Saved/BlenderAnimation/Preview | 미등록 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\save_preview_source.py | Blender 안 Python | 샘플 .blend 저장(버전 파일 0) | 미등록 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\review_reference_motion.py | Blender 안 Python | MM_Attack_01~03 참고 모션 렌더·요약 → Saved/BlenderAnimation/Reference/Preview | 미등록 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\prepare_weighty_workspace.py | Blender 안 Python | 참고 라이브러리 저장 후 빈 문서 준비 | 미등록 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\author_weighty_slash.py | Blender 안 Python(v04 이름 하드코딩) | MM_Attack_01 기반 무게감 횡베기 v04 생성 | 미등록(Docs/AnimationQuality.md 산문) |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\render_weighty_preview.py | Blender 안 Python | v04 세 방향 렌더(SM_Sword 미리보기 부착) → Saved/BlenderAnimation/HeavyV04 | 미등록 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\validate_weighty_slash.py | 바깥 Python(MCP) | v04 수치 검증 → Docs/Validation/BlenderAnimation/weighty-* | Docs/AnimationQuality.md 59행 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\validate_weighty_pie.py | 에디터 안 Python(PIE 중, import unreal) | PIE에서 v04 몽타주 재생·카메라 검증 → Docs/Validation/BlenderAnimation/weighty-*-pie.json | 미등록 |  | Blender 폴더 안의 Unreal 스크립트, editor_ 접두어 없음 |
| C:\Project\TDGame\Tools\BlenderAnimation\prepare_grip_revision.py | Blender 안 Python | 현재 세션 스냅샷 저장 후 빈 문서(v05 준비) | 미등록·미커밋 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\author_sword_grip.py | Blender 안 Python(v05 이름 하드코딩) | 검 그립 수정 후보 v05 생성 | 미등록·미커밋 | Tools/BlenderAnimation/author_weighty_slash.py(버전 복제) |  |
| C:\Project\TDGame\Tools\BlenderAnimation\inspect_grip.py | Blender 안 Python | 손·손가락 본 방향 표본 출력 → Saved/BlenderAnimation/grip | 미등록·미커밋 |  |  |
| C:\Project\TDGame\Tools\BlenderAnimation\render_grip_review.py | Blender 안 Python | v05 그립 근접 렌더 → Saved/BlenderAnimation/GripV05 | 미등록·미커밋 | Tools/BlenderAnimation/render_weighty_preview.py |  |
| C:\Project\TDGame\Tools\BlenderAnimation\validate_grip_revision.py | 바깥 Python(MCP + pose_kinematics) | v05 수치 검증 → Docs/Validation/BlenderAnimation | 미등록·미커밋 | Tools/BlenderAnimation/validate_weighty_slash.py |  |
| C:\Project\TDGame\Tools\BlenderMCP\Install-BlenderMCP.ps1 | PowerShell | ahujasid/blender-mcp 고정 리비전 zip 다운로드·SHA256 검증·uv sync --frozen | Docs/BlenderAnimationWorkflow.md 29~31행 |  |  |
| C:\Project\TDGame\Tools\BlenderMCP\Start-BlenderMCP.ps1 | PowerShell | Blender 5.2를 --factory-startup --python bootstrap_blender.py로 숨김 실행, 포트 9876 소유 검사 | Docs/BlenderAnimationWorkflow.md |  |  |
| C:\Project\TDGame\Tools\BlenderMCP\Run-BlenderMCP.ps1 | PowerShell(MCP stdio 서버 런처) | 필요 시 Start 후 .venv/Scripts/blender-mcp.exe 실행; 5개 클라이언트 설정이 이 파일을 가리킴 | .mcp.json·.codex/config.toml·.cursor/mcp.json·.gemini/settings.json·.vscode/mcp.json |  |  |
| C:\Project\TDGame\Tools\BlenderMCP\bootstrap_blender.py | Blender 안 Python(시작 스크립트) | 고정 리비전 addon.py 로드·등록, 포트 9876, 외부 연동 끄기 | Start-BlenderMCP.ps1 |  |  |
| C:\Project\TDGame\Tools\BlenderMCP\call_tool.py | 바깥 Python(.venv, mcp 패키지; stdio 클라이언트 CLI) | --list / --tool / --code <파일> --output <json> 로 execute_blender_code 실행 | Docs/BlenderAnimationWorkflow.md 44~55행 |  |  |
| C:\Project\TDGame\Tools\BlenderMCP\smoke_test.py | Blender 안 Python | 키프레임 프로브 생성·삭제로 세션 동작 확인 JSON | 미등록 |  |  |
| C:\Project\TDGame\Tools\BlenderMCP\pyproject.toml / uv.lock / .gitignore | 설정 | Python 3.12 고정 의존성, .venv·.runtime·__pycache__ 무시 | Docs/BlenderAnimationWorkflow.md |  | 자체 .gitignore가 있는 유일한 도구 폴더 |
| C:\Project\TDGame\.gemini\scripts\unreal_mcp.py | 바깥 Python(MCP 클라이언트 클래스) | UnrealMcpClient: initialize/list_toolsets/describe_toolset/call_tool | 미등록 | Tools/uemcp.py |  |
| C:\Project\TDGame\.gemini\scripts\check_editor_connection.py | 바깥 Python(MCP) | 에디터 연결·현재 레벨 확인 | 미등록 | Tools/ue_editor.py status |  |
| C:\Project\TDGame\.gemini\scripts\take_screenshot.py | 바깥 Python(ctypes Win32 창 캡처 + PIL) | 언리얼 창을 OS 수준에서 캡처 | 미등록 | Tools/WorldGen/capture_views_mcp.py | README §5 '뷰포트 캡처는 CaptureViewport만 신뢰'와 상충 |
| C:\Project\TDGame\.gemini\scripts\build_dark_fantasy_level.py | 바깥 Python(MCP SceneTools add_to_scene) | LV_DarkFantasy_OpenWorld 조명·안개·액터 배치(1세대 월드 빌드) | 미등록 | Tools/WorldGen/editor_build_open_world.py | docstring 없음 |
| C:\Project\TDGame\.gemini\scripts\build_3km_open_world.py | 바깥 Python(MCP) | 바닥 3km 확장·조명 재조정 | 미등록 | Tools/WorldGen 파이프라인 |  |
| C:\Project\TDGame\.gemini\scripts\build_ash_ruin_world.py | 바깥 Python(MCP) | 'Ashen Ruin' 조명 톤 재조정·배치 | 미등록 | Tools/WorldGen 파이프라인 |  |
| C:\Project\TDGame\.gemini\scripts\build_full_arpg_world.py | 바깥 Python(MCP) | spawn_asset 헬퍼로 ARPG 월드 액터 배치 | 미등록 | Tools/WorldGen 파이프라인 |  |
| C:\Project\TDGame\.gemini\scripts\build_dense_3km_world.py | 바깥 Python(MCP execute_tool_script 대량 스폰, 707행) | 산맥 링·PCG 볼륨·대량 배치 | 미등록 | Tools/WorldGen/editor_build_open_world.py + editor_make_pcg_biome.py |  |
| C:\Project\TDGame\.gemini\scripts\build_world_from_landscape.py | 바깥 Python(MCP, 663행) | 기존 랜드스케이프 위 월드 구성 | 미등록 | Tools/WorldGen/editor_build_open_world.py |  |
| C:\Project\TDGame\.gemini\scripts\setup_real_landscape.py | 바깥 Python(MCP) | 랜드스케이프 설정·바닥 정리 | 미등록 | Source/TDGameEditor/Landscape/TDLandscapeEditorLibrary + editor_build_open_world.py |  |
| C:\Project\TDGame\.gemini\scripts\populate_foliage.py / populate_dense_nature.py / populate_outer_wilderness.py | 바깥 Python(MCP execute_tool_script) | 식생·바닥 카펫·외곽 황야 스폰(3개) | 미등록 | Tools/WorldGen/generate_ashen_vale.py 산포 + editor_make_pcg_biome.py |  |
| C:\Project\TDGame\.gemini\settings.json | 설정(Gemini MCP) | blender(Run-BlenderMCP.ps1)·unreal-mcp 연결 | AGENTS.md 141행 |  |  |
| C:\Project\TDGame\Content\Python\init_unreal.py | 에디터 자동 시작 훅(UE가 시작 시 실행) | td_blender_animation_tools.register() | Docs/BlenderAnimationWorkflow.md 61행 |  |  |
| C:\Project\TDGame\Content\Python\td_blender_animation_tools.py | 에디터 안 Python MCP 툴셋(unreal.ToolsetDefinition + toolset_registry) | TDBlenderAnimationTools: export_fbx, import_animation_fbx, sample_animation_poses(프로젝트 내부 .fbx만 허용) | Docs/BlenderAnimationWorkflow.md 61행, BlenderAnimation/SKILL.md 14행 | Tools/Animation/ue_blender_anim_bridge.py(구세대) | 네 번째 기능 노출 경로; README §4·템플릿에 없음 |
| C:\Project\TDGame\Source\TDGameEditor\Landscape\TDLandscapeEditorLibrary.h/.cpp | C++ UBlueprintFunctionLibrary(Python용) | CreateLandscapeFromRawFiles, TryGetLandscapeHeightAtLocation, DestroyAllLandscapeActors | Tools/WorldGen/README.md 27행, Tools/README.md §5 |  |  |
| C:\Project\TDGame\Source\TDGameEditor\WorldGen\TDWorldGenEditorLibrary.h/.cpp | C++ UBlueprintFunctionLibrary | 던전 생성·베이크·JSON, 월드 레이아웃 생성·검증·베이크·잠금 유지·지역 부분 베이크, 메시지 로그 | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Source\TDGameEditor\WorldGen\TDWorldGenCommandlet.h/.cpp | C++ 커맨드릿(-run=TDWorldGen) | 무인 시드 스윕·리포트 | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Source\TDGameEditor\WorldGen\TDWorldGenBuilder.h/.cpp | C++ WorldPartitionBuilder | 월드 생성→검증→리포트(→베이크), 실패 시 종료코드 1 | Tools/README.md §3b |  |  |
| C:\Project\TDGame\Source\TDGameEditor\WorldGen\TDDungeonBaker / TDWorldBaker / TDWorldGenEditorBridgeImpl | C++ 내부 구현 | 베이크·브리지 구현(라이브러리가 호출) | 미등록(내부) |  |  |
| C:\Project\TDGame\Source\TDGameEditor\Animation\TDAnimationAuthoringTools.h/.cpp | C++ UToolsetDefinition(meta AICallable → MCP 도구) | InspectSkeleton, CreateBoneAnimation, CreateMontage, InspectAnimation | Docs/AnimationAuthoring.md 표, AnimationAuthoring/SKILL.md |  |  |
| C:\Project\TDGame\Source\TDGameEditor\Animation\TDControlRigTools.h/.cpp | C++ UToolsetDefinition(MCP) | CreateFKSequence(FK Control Rig + LevelSequence) | Docs/AnimationAuthoring.md |  |  |
| C:\Project\TDGame\Source\TDGameEditor\Animation\TDSequencerAnimationTools.h/.cpp | C++ UToolsetDefinition(MCP) | InspectSequence, BakeAnimation | Docs/AnimationAuthoring.md |  |  |
| C:\Project\TDGame\Source\TDGameEditor\Animation\TDAnimationAuthoringSubsystem.h/.cpp | C++ UEditorSubsystem | 애니 저작 툴셋 지원 서브시스템 | 미등록(내부) |  |  |
| C:\Project\TDGame\Source\TDGameEditor\Tests\ | C++ 자동화 테스트(PIE) | TDSeamlessTravelPieTests, TDWorldStatePieTests, TDTravelStateRecorder, TDPieWaitUntilCommand | Claude 메모리 tdgame-build-and-test-workflow(저장소 문서 미확인) |  |  |
| C:\Users\jjh\.claude\projects\C--Project-TDGame\tools\ (저장소 밖, Claude 전용) | 바깥/에디터 안 Python 혼합 14개 | uemcp.py(동일본), uepy.py(run_in_editor 전신), run_tests.py(MCP 자동화 테스트 러너), save_capture.py, save_editor_capture.py, showtool.py, make_montage.py, place_actors.py, cleanup_actors.py, pie_attack.py, pie_after.py, reorg_tdgame.py, split_doc.py, append_section.py | 미등록(Claude만 접근) | Tools/uemcp.py, Tools/run_in_editor.py | Tools/WorldGen/capture_views_mcp.py 15행이 이 경로에 의존; run_tests.py 등은 저장소에 대응물 없음 |

## 문서·작업 기록

저장소 C:\Project\TDGame 의 Docs 감사 결과(2026-09-17, 읽기 전용). Docs는 마크다운 86개(1,902,670바이트 ≈ 약 100만 자)와 미디어·JSON 54개(Docs/Validation 57.2MB, gif 6개가 약 46MB, png/gif/jpg는 .gitattributes 20~22행으로 LFS 추적)로 이루어진다. 문서는 사실상 세 묶음이다: (A) 월드·던전·PCG(인덱스 WorldDungeonPCG_Plan.md → WorldDungeonPCG/ 5 + research 9 + Tasks/ 7), (B) 몬스터 AI·전투 시뮬(인덱스 MonsterAI_CombatSim_Plan.md → 00~07 + research 17), (C) 애니메이션 제작(AnimationAuthoring.md, AnimationAuthoring_Tasks.md, BlenderAnimationWorkflow.md, AnimationQuality.md — 인덱스 없음, AGENTS.md·Tools/README.md 어디서도 링크되지 않음). 그 밖에 전투 기반 문서 3개(TDDamageSystemDesign/Guide, TDGASFoundation), 이식 지도(PJGame_PortMap), 외부 참고(UKGame 11 + FeatureFileMap, 설계서 docx), 출처 미표기 엔진 가이드 5개(UE_Engine_*), 대형 연구 2개(TDUAF, TDMonsterAnimationOptimization_500)가 최상위에 평평하게 놓여 있다.

## (1) 문서 종류 분류
- 인덱스/계획: Docs/WorldDungeonPCG_Plan.md(4.4KB), Docs/MonsterAI_CombatSim_Plan.md(50KB, §2 결정 압축·§5 절차·§6 용어집·§7 미결 총괄·§8 방법 기록 포함), Docs/Tasks/README.md, Docs/UKGame_FeatureFileMap.md(외부 참고 인덱스).
- 아키텍처/설계: WorldDungeonPCG/01~04·room-module-spec, MonsterAI_CombatSim/01~06, TDDamageSystemDesign.md(2026-09-06), TDGASFoundation.md, PJGame_PortMap.md.
- 리서치: WorldDungeonPCG/research 9편(엔진 소스 확인 + landscape-mcp-test 실측), MonsterAI_CombatSim/research 17편(결론 번호·파일:줄·정정/미확인 절 구조), TDUAF(67KB), TDMonsterAnimationOptimization_500(65KB).
- 할 일 대장 3곳: Docs/Tasks/phase-0~4(P), MonsterAI_CombatSim/07 §3(M) + §4(MD), AnimationAuthoring_Tasks.md(A~G).
- 결정 기록 2곳 + 미결 목록 2곳: Docs/Tasks/decisions.md(D-01~D-11), MonsterAI_CombatSim/00-decision-record.md(D1~D38), 07 §4 MD-01~10, MonsterAI 인덱스 §7(20행, 07·06·01~05 미결 병합).
- 검증 증거: Docs/Validation(README + md 10 + png 9·json 2 + BlenderAnimation 17 + ashen-vale 24).
- 가이드: TDDamageSystemGuide, AnimationAuthoring, BlenderAnimationWorkflow, AnimationQuality, UE_Engine_* 5(작성일·출처·근거 없음, "제공합니다" 문체).
- 외부 참고: Docs/UKGame/ 11편, Docs/UE5_탑다운_ARPG_월드_던전_PCG_설계서.docx.
- 사용자 전용 메모: 루트 사용자용_할일_목록.md(Docs 밖, "AI는 읽지 않습니다").

## (2) 할 일 대장 3곳 비교
| 항목 | Docs/Tasks/phase-*.md | MonsterAI_CombatSim/07 §3 | AnimationAuthoring_Tasks.md |
|---|---|---|---|
| ID | `P<phase>-<n>` + 결정용 변형 `P0-D1`·`P2-D3`, `P3-00` | `M<phase>-<n>`(06 제안 번호 유지로 M2-12~14 등 빈 번호) | `<영역문자>-<n>`: A 검증·B 결함·C 공백·D 문서오류(표)·E 배포·F 토큰·G 정리 |
| 상태 값 | todo/doing/done/decision 사용(blocked 정의만) | todo/blocked/decision 사용(doing·done 0) | 자유 서식: `todo`, `todo / 우선순위: 낮음`, `**done (2026-09-16)**`, `**부분 완료 (…)**`, 표에서는 ✅ |
| 필드 | 9필드 정의(README 형식). 실제 phase-1: 산출물 1/10, 검증 7/10 | 9필드 50/50 완비 + 검증 명령 약어 BUILD/TEST/SIM/VALIDATE(§3.1) | 상태·우선순위 외 증상·조치·비고·절차·함께·주의 등 항목마다 다름, 완료 조건 8/21 |
| 상태 집계표 | 파일 맨 위 `\| 상태 \| 개수 \|` (5개 파일 모두 실제 수와 일치 확인) | 없음(결론 요약에 "50개(M0 8·M1 13·M2 12·M3 12·M4 5)" 고정 문장) | 없음("현재 상태 요약" 근거 표로 대체) |
| 기록 칸 | `날짜 작성 / 날짜 완료(Claude): 내용 / …` 연쇄 | 50개 전부 `(없음)` | 기록 필드 없음, 상태 문장에 날짜, 별도 "실측 메모"·"권장 순서" 절 |
| 결정 연결 | decision → `참조: decisions.md D-02` | decision → 같은 파일 §4 MD-xx | 없음 |
| 증거 위치 규칙 | Docs/Validation/ (README 규칙 5) | Docs/MonsterAI_CombatSim/measurements/(폴더 미생성, MD-05 전까지 Validation 금지) | Tools/AnimationAuthoring/smoke-report.json, Docs/Validation/BlenderAnimation/ |
| 담당 표기 | (Claude) 46회, Codex·Gemini 0회 | 없음 | 없음 |
| 현황 | 53항목: done 18 / doing 15 / todo 18 / decision 2 | 50항목: todo 42 / blocked 7 / decision 1 → 구현 미착수 | ### 22 + D표 9행: done 3+4✅ / 부분 1 / todo 17+5 |
| 에이전트 절차 | Tasks/README 7조 | 07 §6 8조("Tasks/README와 동일" + 골든 해시 조항), 인덱스 §5 표로 재요약 | 없음(권장 순서만) |
Tools/tasks_recount.py: `Docs/Tasks/phase-*.md`만 대상으로 `^- 상태: (\w+)` 줄을 세어 맨 위 `| 상태 | 개수 |` 표를 다시 쓴다(ORDER = todo, doing, done, decision, blocked). 07·AnimationAuthoring에는 그 표가 없어 적용 불가하고, 인자로 넘겨도 표가 없으면 아무것도 바꾸지 않는다.

## (3) 결정 기록 2곳 비교
| | Docs/Tasks/decisions.md | MonsterAI_CombatSim/00-decision-record.md |
|---|---|---|
| 성격 | 사용자 결정 대기 + 에이전트 자체 결정(D-07~D-10 "(Claude)") 혼재 | 심사 합의로 확정된 설계 결정만, "구속력" 선언(01~07보다 우선) |
| ID | D-01~D-11 | D1~D38 (하이픈 없음) |
| 항목 형식 | 질문/선택지/권장/영향/결정(날짜·담당). D-06은 실측 결과, D-11은 다항 결정 | 번호 한 문장 + 근거(research 파일명·결론 번호), 11절, §11 정오표(이전/수정/이유) |
| 상태 전이 | `(미정)` → 날짜+선택. 결정되면 관련 P 작업을 todo로 되돌림 | 전이 없음. 바꾸려면 research 근거를 먼저 반박 |
| 미결 처리 | 같은 파일 | 07 §4 MD-01~10(decisions.md와 같은 5필드) + 인덱스 §7 총괄표(20행)로 분산 |
| 충돌 | `D-01`이 AnimationAuthoring_Tasks.md D절(문서 오류 D-01~D-09)과 동명 | D1~D38 vs D-01~D-11 접두어 구분 없음 |

## (4) 실측·함정·교훈 기록이 흩어진 위치(절 제목 grep + 키워드 grep)
1. Tools/README.md:73 `## 5. 실측된 함정`(6건, GEMINI.md가 "실측 함정은 여기에 추가"라고 지정한 유일한 규칙)
2. Tools/WorldGen/README.md:49 `## PCG 그래프 스크립팅 실측 (UE 5.8, P3-07)`
3. Docs/AnimationAuthoring_Tasks.md:27 `### 실행 환경 주의`(Git Bash 경로 변환), :252 `### 실측 메모`(4건: AssetEditorSubsystem 부재 API, delete_asset 잔존, 툴팁 1024자, GPU 크래시)
4. Docs/Tasks/decisions.md D-06(내비메시 Static/Dynamic 실측 수치), D-10
5. Docs/WorldDungeonPCG/research/landscape-mcp-test.md(제목에 "실측"), pcg-bake-data-community.md:39 `## 6. 커뮤니티 교훈`, actor-id-and-navmesh.md:13(수명 함정)
6. Docs/MonsterAI_CombatSim/04-combat-simulator.md:221 `### 3.4 엔진 쪽 난수 함정 요약`, 03:288·315(실측 갱신 절차)
7. Docs/MonsterAI_CombatSim/research/*.md 16편의 '정정/미확인/피할 것' 절
8. Docs/Validation/*.md 본문(P1-pie-checks: BeginPlay 델리게이트 중복 바인딩 ensure 수정, tools-gemini-dungeon: 첫 검증 71.9점→수정 경위)
9. Docs/BlenderAnimationWorkflow.md:158 `## 연결 문제 확인`, Docs/UE_Engine_World_Partition_Optimization_Guide.md:80·169(함정, 출처 미표기)
10. 저장소 밖: Claude 자동 메모리(예: tdgame-anim-notify-facts.md, tdgame-pcg-python-scripting.md)에만 있는 실측이 있음(전제 6).
키워드(실측|함정|주의|안 된다|되지 않는다|교훈|실패한다|거부한다) 건수 상위: 07-roadmap 36, 03-tick 27, TDMonsterAnimationOptimization_500 18, 04-combat-simulator 18, project-current-combat-code 17, AnimationAuthoring_Tasks 15 — 대부분 설계 문서 안의 서술이라 "교훈 대장"으로 재사용하기 어렵다.

## (5) Docs/Validation 명명 규칙과 README 최신성
- md 이름 패턴 5가지 혼재: `P<phase>-<n>-<주제>[-날짜].md` 5개(P1-pie-checks-2026-09-12, P2-12-candidates, P3-07-pcg-biome, P3-09-vertical-slice, P3-11-ashen-vale), `<주제>-<날짜>.md` 3개(phase1-3-cpp-…, tools-gemini-dungeon-…, worldgen-builder-seed7-…), 무날짜 `<주제>.md` 2개(commandlet-dungeon-sweep, cpp-world-layout-seed7: 커맨드릿 출력 그대로). 날짜는 파일명 3개·본문 제목 7개로 갈림. 에이전트 표기는 P3-07 제목 "(2026-09-12, Claude)" 하나뿐.
- 미디어: GAS 전 `<효과>-<상태>.png` 9 + `<주제>-observations.json` 2, BlenderAnimation/`<이름>-v<nn>-<종류>.{png,gif,json}` 17(v03·v04 세트 + player-attack01 + environment/pie-validation), ashen-vale/`<장소>.jpg` 24(snake_case).
- README.md(959B, git 최종 2026-09-08)는 "GAS 전환 전 공식 Unreal MCP PIE 검사" 5건만 설명하고 "현재 검증으로 쓰지 않는다"고 적음. 현재 폴더 파일 79개 중 그 설명에 해당하는 것은 png 9·json 2뿐이며, P1~P3·Blender 기록은 언급이 없다. README는 어떤 문서에서도 링크되지 않는 고아다.
- 54개 미디어·JSON 중 33개가 어느 md에서도 파일명이 언급되지 않음(ashen-vale jpg 16, GAS 전 png 9·json 2, weighty-v03 세트 5, environment-validation.json).

## (6) 링크 상태(스크립트로 Docs 86 + AGENTS/GEMINI/Tools README/SKILL 링크·코드참조 수집)
- 고아(인바운드 0) 12개: TDUAF_Architecture_Usage_And_Transfer_Research.md, UE_Engine_Animation_Physics_Movement_Optimization_Guide.md, UE_Engine_Core_Systems_Optimization_Guide.md, UE_Engine_Crowd_Animation_Sharing_Quality_Guide.md, UE_Engine_Lumen_Rendering_Quality_Guide.md, UE_Engine_World_Partition_Optimization_Guide.md, Validation/README.md, Validation/cpp-world-layout-seed7.md, Validation/phase1-3-cpp-2026-09-12.md, Validation/tools-gemini-dungeon-2026-09-12.md, Validation/worldgen-builder-seed7-2026-09-12.md, (사실상) UKGame_FeatureFileMap.md(하위 11편의 백링크만 있고 상위 인덱스·AGENTS.md는 폴더 `Docs/UKGame/`만 언급).
- 인덱스에서 안 보이고 서로만 링크 13개: AnimationAuthoring.md, AnimationAuthoring_Tasks.md, AnimationQuality.md, BlenderAnimationWorkflow.md, TDDamageSystemDesign.md, TDDamageSystemGuide.md, TDGASFoundation.md, TDMonsterAnimationOptimization_500_Research.md, WorldDungeonPCG/room-module-spec.md(phase-2·03에서 코드 참조만), Validation/P1-pie-checks·P2-12·P3-07·P3-09·commandlet-dungeon-sweep(phase 파일 기록 칸의 코드 참조만).
- AGENTS.md가 직접 가리키는 Docs: WorldDungeonPCG_Plan, Tasks/README, Tasks/decisions, UKGame/, MonsterAI_CombatSim_Plan, 00-decision-record, 07-roadmap, research/, PJGame_PortMap(9개). 애니메이션·전투 기반·Validation은 0건.

## (7) 새 세션 "어디까지 왔는지" 파악 경로와 비용(문자 수, 토큰은 한글 위주 마크다운 0.6~0.9토큰/자 가정)
- 단일 현황 파일이 없다. 영역별로 읽어야 하는 최소 경로:
  - A 월드젠: AGENTS.md(8,503자) + WorldDungeonPCG_Plan(2,817) + Tasks/README(1,864) + phase-2(9,558) + phase-3(9,291) + decisions(5,479) + Tools/README(6,556) + Tools/WorldGen/README(5,466) = 8파일 49,534자 ≈ 3.0만~4.5만 토큰.
  - B 몬스터 AI: AGENTS + 인덱스(33,875) + 00(12,864) + 07(51,896) + 02(45,751, §2 클래스 표를 07 §6이 요구) = 5파일 152,889자 ≈ 9.2만~13.8만 토큰. 07 §6 절차 1항이 이 네 문서를 "시작 전에 읽는다"로 지정.
  - C 애니메이션: AGENTS(언급 없음) + AnimationAuthoring_Tasks(13,794) + AnimationAuthoring(3,495) + BlenderAnimationWorkflow(10,782) + AnimationQuality(4,735) + SKILL.md 2개(7,007) + Tools/README = 8파일 54,872자 ≈ 3.3만~4.9만 토큰. 단, 발견 경로가 없어 git status나 Tools 폴더 탐색으로만 도달.
  - D 프로젝트 전체 현황(세 대장 상태부 전부): 9파일 109,979자 ≈ 6.6만~9.9만 토큰. 현재는 phase 파일 5개의 상태표 + 07 결론 요약 + AnimationAuthoring_Tasks 상태 요약 표를 각각 열어야 한다.

### 발견 사항
| 심각도 | 영역 | 사실 | 문제 | 근거 |
|---|---|---|---|---|
| high | 온보딩 경로 | 프로젝트 전체 진행 상태를 요약한 단일 파일이 없다. 세 대장의 상태부를 모두 읽으면 9파일 109,979자(≈6.6만~9.9만 토큰)이고, 몬스터 AI 영역만 해도 07 §6이 지정한 4문서가 152,889자다. | 매 세션 수만 토큰을 상태 파악에 쓰고, 에이전트마다 다른 파일을 읽어 현황 인식이 갈린다. 상태 집계는 결정론적이므로 스크립트가 한 파일로 뽑아 주는 편이 맞다. | 문자 수 계산(scratchpad 스크립트): Docs/Tasks/phase-0~4 + 07-roadmap + AnimationAuthoring_Tasks + AGENTS.md; Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md:756-760 '시작 전에 AGENTS.md, 이 문서, 00 결정 기록, 02 §2 클래스 표를 읽는다' |
| high | 발견 가능성 | 애니메이션 묶음 4개(AnimationAuthoring.md, AnimationAuthoring_Tasks.md, BlenderAnimationWorkflow.md, AnimationQuality.md)는 AGENTS.md·Tools/README.md·어느 인덱스에서도 링크되지 않고 서로와 Tools/BlenderAnimation/SKILL.md에서만 링크된다. 전투 기반 3개(TDDamageSystemDesign/Guide, TDGASFoundation)도 인덱스 미등록. | Codex·Gemini는 AGENTS.md와 Tools/README.md를 통해서만 문서를 찾으므로 애니메이션 도구·대장·품질 기준을 발견하지 못한다. 사용자 요구 (a)의 직접적 위반 지점. | 링크 그래프 스크립트 결과 LINKED-ONLY; AGENTS.md grep 'Docs/' 결과 9경로에 애니메이션·전투 기반 0건; Docs/AnimationAuthoring_Tasks.md:172-182 E절이 스스로 'AGENTS.md 165줄 중 애니메이션 언급 0건'이라고 기록 |
| high | 할 일 대장 형식 | 세 대장의 ID 체계(P/M/A~G), 상태 값(5값 소문자 vs 자유 서식 굵은 글씨·날짜 포함), 필드(9필드 완비 vs 항목마다 다름), 기록 방식(연쇄 기록 vs 전부 '(없음)' vs 기록 필드 없음), 증거 위치 규칙(Validation vs measurements vs smoke-report)이 서로 다르다. Tools/tasks_recount.py는 Docs/Tasks/phase-*.md의 '\| 상태 \| 개수 \|' 표만 재계산해 나머지 두 대장에는 적용되지 않는다. | 상태를 기계로 세거나 '다음 할 일'을 고르는 규칙을 스크립트화할 수 없고, 에이전트가 대장마다 다른 서식을 학습해야 한다. 07 §6·인덱스 §5·Tasks/README가 같은 절차를 세 번 적고 있어 수정 시 세 곳을 고쳐야 한다. | 상태 값 집계 grep: AnimationAuthoring_Tasks.md에 '**done (2026-09-16)**', 'todo / 우선순위: 낮음', '**부분 완료 (2026-09-16)**' 등 9가지 서식; 필드명 집계: phase-1 산출물 1/10, 07 9필드 50/50, AnimationAuthoring 증상 5·조치 2·비고 3; Tools/tasks_recount.py:14-20 정규식 |
| medium | 결정 기록 ID | 'D-01'이 Docs/Tasks/decisions.md(템플릿 잔재 정리, 2026-09-17 결정)와 Docs/AnimationAuthoring_Tasks.md D절(workflow.md set_transform 스케일 인수 오류, ✅)에서 다른 뜻으로 쓰이고, 00-decision-record는 'D1~D38'로 하이픈만 다르다. 사용자 결정 대기 목록은 decisions.md·07 §4 MD-01~10·인덱스 §7 총괄표(20행) 세 곳이다. | 문서 간 참조('D-01 참조')가 모호해지고, 결정을 검색·집계하는 스크립트를 만들 수 없다. 미결이 세 곳에 있어 어느 것이 정본인지 인덱스 §7이 따로 병합 설명을 달아야 한다. | Docs/Tasks/decisions.md:5; Docs/AnimationAuthoring_Tasks.md:160; Docs/MonsterAI_CombatSim/00-decision-record.md:11; Docs/MonsterAI_CombatSim_Plan.md:206-233 |
| medium | 실측·함정·교훈 기록 | 실측 기록이 최소 9곳(Tools/README.md §5, Tools/WorldGen/README.md PCG 절, AnimationAuthoring_Tasks 실행 환경 주의·실측 메모, decisions.md D-06, WorldDungeonPCG research 3편, MonsterAI 04 §3.4·03 §6, research 16편 정정 절, Validation md 본문)과 저장소 밖 Claude 메모리에 흩어져 있다. 저장소 안 규칙은 GEMINI.md의 '실측 함정은 Tools/README.md 5절에 추가' 한 줄뿐이며 애니메이션 실측은 그 규칙을 따르지 않았다. | 같은 함정(예: 에디터 Python에 없는 API)이 도구 README·대장·메모리에 중복 기록되거나 다른 에이전트에게 전달되지 않는다. 사용자 요구 (d) '문제 해결 기록 메모리'의 저장 위치·형식이 없다. | 절 제목 grep 결과 목록(요약 (4)); GEMINI.md '도구가 없을 때' 4항; Docs/AnimationAuthoring_Tasks.md:252-257 |
| medium | 검증 증거 폴더 | Docs/Validation은 명명 패턴 5가지가 섞여 있고, README.md는 2026-09-08 GAS 전환 전 내용으로 현재 파일 79개 중 11개만 설명하며 고아다. 미디어·JSON 54개 중 33개는 어느 md에서도 참조되지 않는다. 07 §6은 Validation 대신 존재하지 않는 Docs/MonsterAI_CombatSim/measurements/를 쓰라고 한다. | 증거를 대장 항목과 연결해 찾을 수 없고(대장 기록 칸의 코드 참조가 유일한 연결), 57MB 폴더에 무엇이 유효한지 README가 말해 주지 않는다. 묶음별로 증거 위치 규칙이 달라 통합 규칙이 필요하다. | 파일 목록(요약 (5)); git log -1 Docs/Validation/README.md = 2026-09-08; 미참조 검사 결과 33건; Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md:762; ls Docs/MonsterAI_CombatSim/measurements → 없음 |
| medium | 고아 문서 | 인바운드 링크 0인 md가 12개: 대형 연구 TDUAF(67KB), UE_Engine_* 가이드 5개(72KB, 작성일·출처·근거 없음), Validation/README 및 Validation md 4개, 사실상 UKGame_FeatureFileMap(AGENTS.md는 폴더만 언급). 인덱스에 없고 서로만 링크하는 md가 13개 더 있다. | 인덱스만 읽는 에이전트는 이 문서들의 존재를 모르고, 반대로 폴더를 통째로 읽는 에이전트는 신뢰도 표시가 없는 외부 가이드를 프로젝트 결정과 같은 무게로 읽을 수 있다. | 링크 그래프 스크립트 ORPHAN/LINKED-ONLY 태그; AGENTS.md:154 'Docs/UKGame/' |
| medium | 대장 최신성 | Docs/Tasks/README.md 마지막 줄은 '마지막 갱신: 2026-09-09 (초기 작성, 전 항목 todo)'이지만 phase 파일에는 done 18·doing 15가 있다(git 최종 수정 2026-09-09). doing 15건의 마지막 기록은 2026-09-12이며 phase-3은 9건이 doing이다. 07 대장은 50건 모두 기록 '(없음)'. | 새 세션은 doing이 '진행 중'인지 '세션이 끊겨 방치'인지 구분할 수 없어 같은 항목을 중복 착수하거나 건드리지 않는다. 상태 요약 갱신을 사람 손에 맡긴 결과다. | Docs/Tasks/README.md:36; 상태 집계 grep; Docs/Tasks/phase-3-outdoor-generator.md 기록 칸 날짜 |
| low | 담당·출처 추적 | 기록 칸의 에이전트 표기는 '(Claude)' 46회뿐이고 Codex·Gemini 표기는 0회다. Validation 제목에 에이전트가 적힌 것은 P3-07 하나. tools-gemini-dungeon-2026-09-12.md는 'Gemini 도구 검증'인지 'Gemini가 작성'인지 파일명으로 구분되지 않는다. | 전제 7(커밋 메시지로 추적 불가)과 겹쳐 어느 에이전트가 무엇을 했는지 문서에서도 알 수 없다. 기록 칸 '날짜(에이전트)' 관행은 있으나 규칙이 아니라 Claude만 지킨다. | grep 결과(요약 (2) 담당 표기 행); Docs/Validation/P3-07-pcg-biome.md:1; Docs/Validation/tools-gemini-dungeon-2026-09-12.md:1-5 |
| low | 문서 구조 편차 | MonsterAI 01~07과 WorldDungeonPCG 하위 문서는 '[← 인덱스로]' 백링크와 '이 문서가 답하는 질문/결론 요약/미결/근거 색인' 골격으로 통일돼 있으나, 최상위 단독 문서·애니메이션 묶음·Validation md에는 백링크·작성일·근거 규약이 없다. 02 문서는 절 번호가 7→13→8로 어긋난다. | 문서 종류를 기계적으로 판별하거나 인덱스를 자동 생성하려면 최소 메타(종류·작성일·상위 인덱스)가 파일 머리에 있어야 하는데 절반만 갖추고 있다. | Docs/MonsterAI_CombatSim/02-architecture-and-definition-format.md:488-524 절 제목; Docs/UE_Engine_*_Guide.md 머리말(대상 엔진·목적만 있고 작성일 없음) |
| low | 중복 서술 | 에이전트 작업 절차가 Docs/Tasks/README.md(7조), 07 §6(8조, '동일하다'고 명시), MonsterAI 인덱스 §5(표), GEMINI.md 세션 시작 절차, AGENTS.md §9·§12·§13에 다섯 번 적혀 있다. 핵심 결정도 00 전문·인덱스 §2 12문장·01~07 각 '결론 요약'에 세 층으로 반복된다. | 규칙을 바꾸면 다섯 곳을 고쳐야 하고, 새 세션은 같은 절차를 여러 번 읽어 토큰을 쓴다. 인덱스 §2·§5는 요약이라 '표현이 다르면 00·07이 우선'이라는 단서를 달아야 하는 구조다. | Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md:758 'Docs/Tasks/README.md의 규칙과 동일하다'; Docs/MonsterAI_CombatSim_Plan.md:21-36, 73-91 |

### 규칙으로 승격할 만한 기존 관례
- Docs/Tasks/README.md 항목 형식(상태·우선순위·선행·목표·완료 조건·산출물·검증·참조·기록 9필드)과 상태 5값(todo/doing/blocked/done/decision), 에이전트 규칙 7조 — 07 대장이 그대로 채택했으므로 사실상 공용 표준. 세 번째 대장(AnimationAuthoring)만 맞추면 된다.
- ID 접두어로 대장을 구분하는 규칙: P<phase>-<n>(월드젠), M<phase>-<n>(몬스터 AI). 결정 ID도 같은 식으로 접두어(예: WD-/MA-/AA-)만 붙이면 충돌이 사라진다.
- 기록 칸 연쇄 형식 '2026-09-09 작성 / 2026-09-12 완료(Claude): 내용 / …' — 날짜·담당·근거를 한 줄에 쌓는 관행(Docs/Tasks/phase-2 기록 칸). 담당 표기를 의무화하면 git 추적 공백을 메운다.
- 결정 항목 5필드(질문/선택지/권장/영향/결정(날짜·담당))와 '(미정)→결정' 전이 + 결정 시 관련 작업을 todo로 되돌리는 규칙(Docs/Tasks/decisions.md 머리말) — 07 §4 MD도 같은 형식이라 통합 가능.
- 00-decision-record의 '결정 한 문장 + 근거(research 파일·결론 번호) + 구속력 선언 + 정오표(이전/수정/이유)' 구조 — 확정 결정 기록의 모범.
- 리서치 문서 규약: 결론 번호, 파일:줄 또는 URL·연도 근거, '정정/미확인/피할 것' 절, 상단 '[← 인덱스로]' 백링크(WorldDungeonPCG/research, MonsterAI research 16편 공통).
- 인덱스 문서의 '유지 방법' 절(리서치 추가 시 표에 한 줄, 결정 변경 시 결정 기록에 한 줄) — WorldDungeonPCG_Plan·MonsterAI 인덱스 둘 다 있음. 여기에 애니메이션 묶음과 Validation을 같은 방식으로 등록하면 된다.
- AGENTS.md §12·§13의 '영역별 대장 포인터' 패턴(기준 문서·대장·결정 기록·상태 표기를 한 절에) — 애니메이션용 §15 한 절만 추가하면 발견 문제가 풀린다.
- GEMINI.md의 '새 도구는 Tools/README.md 표에 등록, 실측 함정은 같은 파일 5절에 추가' 규칙 — 유일한 교훈 저장 규칙이므로 이를 확장(또는 별도 교훈 대장으로 승격)하는 것이 최소 변경.
- Tools/tasks_recount.py — 상태표 재계산을 결정론적 스크립트로 처리하는 선례. 대상 glob과 표 유무만 일반화하면 세 대장·전체 현황 파일 생성기로 확장 가능.
- Docs/AnimationAuthoring_Tasks.md '현재 상태 요약' 표(항목/상태/근거 3열)와 '권장 순서' 절 — 새 세션이 가장 빨리 현황을 읽는 형태. 전체 현황 파일의 서식 후보.
- Validation md의 '스크립트 경로 + 실행 순서 + 결과 수치 표 + 로그 경로' 구성(P3-07, P3-11, P1-pie-checks)과 파일명 'P<phase>-<n>-<주제>-<날짜>.md' 패턴(5개가 이미 사용) — 명명 규칙으로 고정 가능.
- MonsterAI 인덱스 §8 '조사·설계 방법 기록과 재사용 절차'(조사→검증→비평→설계안→심사→결정 기록→병렬 작성→검토) — 다음 대형 주제에 재사용할 다중 에이전트 절차가 이미 문서화됨.

### 열린 질문
- Docs/Tasks의 doing 15건(phase-3 9건, 마지막 기록 2026-09-12)이 실제 진행 중인지 세션 종료로 방치된 것인지 — 규칙에 '세션 종료 시 doing을 todo로 되돌리거나 남은 조건을 적는다'를 넣을지.
- MD-05(시뮬 결과·증거 저장 위치)와 MD-08/M3-10(07 대장의 Docs/Tasks 통합)은 사용자 결정 대기 상태다. 이번 관리 규칙 제정에서 함께 닫을지, 아니면 '증거는 Docs/Validation/<영역>/' 같은 상위 규칙만 정하고 세부는 남길지.
- UE_Engine_* 가이드 5개와 TDUAF·TDMonsterAnimationOptimization_500 연구의 작성 주체·출처(에이전트 생성? 외부 자료?)와 신뢰도 표시를 어떻게 붙일지, 또는 Docs/Reference/ 같은 하위 폴더로 옮길지.
- Docs/Validation의 GAS 전환 전 png 9·json 2(README가 '현재 검증으로 쓰지 않는다'고 명시)를 보관·삭제·이동 중 무엇으로 할지.
- 결정 ID 충돌(D-01 두 곳, D1~D38)을 재번호할지 접두어로만 분리할지 — 재번호 시 01~07 문서의 D 참조가 대량 수정된다.
- 사용자용_할일_목록.md('AI는 읽지 않습니다')를 AGENTS.md에 명시적 제외 항목으로 적을지.
- Docs/AnimationAuthoring_Tasks.md는 감사 보고서와 대장이 한 파일이다. 대장 부분만 표준 형식으로 분리할지, 감사 결과는 교훈 대장으로 옮길지.
- 07 대장에 상태표를 넣어 tasks_recount를 확장할지, 아니면 세 대장을 읽어 하나의 현황 파일을 생성하는 새 스크립트로 갈지(사용자 선호: 결정론적 작업은 프로그램).

### 인벤토리
| 경로 | 종류 | 용도 | 등록 위치 | 중복 | 비고 |
|---|---|---|---|---|---|
| C:\Project\TDGame\Docs\WorldDungeonPCG_Plan.md | 인덱스/계획 | 월드·던전·PCG 묶음 인덱스(문서 순서표, 리서치 표, 당장 필요한 것, 유지 방법) | AGENTS.md §12, GEMINI.md, Docs/Tasks/README.md, Docs/MonsterAI_CombatSim_Plan.md |  | 4.4KB로 가장 잘 짜인 인덱스. 2026-09-12 메인 지역 프로토타입 절 추가됨 |
| C:\Project\TDGame\Docs\WorldDungeonPCG\ | 아키텍처/분석 | 01 설계서 분석(R-xx), 02 현재 상태 차이(G-xx), 03 아키텍처(3.10 리서치 반영 결정), 04 도구·플러그인, room-module-spec(P2-01 확정 규격) | Docs/WorldDungeonPCG_Plan.md 표(01~04). room-module-spec은 미등록(phase-2·03에서 코드 참조만) |  |  |
| C:\Project\TDGame\Docs\WorldDungeonPCG\research\ | 리서치(엔진 소스 확인·실측) | 9편: 월드 파티션·데이터 레이어·레벨 인스턴스·액터 ID·영속/커맨드릿·PCG API·PCG 베이크·던전 생성·랜드스케이프 MCP 실측 | Docs/WorldDungeonPCG_Plan.md 리서치 표 |  | landscape-mcp-test.md와 pcg-bake-data-community.md §6은 실측·교훈 성격 |
| C:\Project\TDGame\Docs\Tasks\ | 할 일 대장 + 결정 기록 | README(형식·에이전트 규칙 7조), phase-0~4(P<phase>-<n> 53항목), decisions.md(D-01~D-11) | AGENTS.md §12, Docs/WorldDungeonPCG_Plan.md, GEMINI.md |  | README '마지막 갱신 2026-09-09 전 항목 todo'는 오래됨. Tools/tasks_recount.py가 상태표 재계산 |
| C:\Project\TDGame\Docs\MonsterAI_CombatSim_Plan.md | 인덱스/계획 | 몬스터 AI·결정론 전투 시뮬 인덱스(질문 6개, 결정 12문장, 문서 지도, 조사 17편 표, 절차, 용어집, 미결 총괄 20행, 조사 방법 기록) | AGENTS.md §13, 하위 01~07 백링크 | Docs/MonsterAI_CombatSim/00-decision-record.md(§2), 07 §6(§5), 07 §4(§7) | 50KB로 인덱스치고 큼. §2는 00의 압축, §5는 07 §6의 압축, §7은 07 §4·06 §6의 병합 |
| C:\Project\TDGame\Docs\MonsterAI_CombatSim\00-decision-record.md | 결정 기록(구속력) | D1~D38 확정 결정, 심사 합의, 정오표 | AGENTS.md §13, Docs/MonsterAI_CombatSim_Plan.md, 01·05·06·07 |  |  |
| C:\Project\TDGame\Docs\MonsterAI_CombatSim\01-ai-model-decision.md ~ 06-beyond-the-ask.md | 아키텍처/설계 | AI 모델 결정, 아키텍처·JSON 정의 형식, 틱·규모, 시뮬레이터, ML·생성형 AI, 요구 밖 고려사항(각 45~64KB) | Docs/MonsterAI_CombatSim_Plan.md §3 문서 지도 |  | 공통 골격: 이 문서가 답하는 질문/결론 요약/본문/미결/근거 색인 + 백링크 |
| C:\Project\TDGame\Docs\MonsterAI_CombatSim\07-roadmap-and-tasks.md | 할 일 대장 + 미결 결정 | Phase 0~4 로드맵, D38 코드 변경 15건, M0~M4 50항목, MD-01~10, 위험표, 에이전트 절차 8조 | AGENTS.md §13, Docs/MonsterAI_CombatSim_Plan.md | Docs/Tasks/README.md 에이전트 규칙(§6) | 78KB, 전 항목 기록 '(없음)', 상태표 없음. 증거 위치를 measurements/(미생성)로 지정 |
| C:\Project\TDGame\Docs\MonsterAI_CombatSim\research\ | 리서치(엔진 소스·프로젝트 코드·웹) | 16편 + 완전성 비평 1편, 결론 번호·파일:줄·정정/미확인 절 구조(34~82KB씩) | Docs/MonsterAI_CombatSim_Plan.md §4 표, AGENTS.md §13(폴더) |  |  |
| C:\Project\TDGame\Docs\AnimationAuthoring.md | 가이드(도구 사용) | C++ 에디터 애니메이션 도구 7개(TD* 툴셋)와 MCP 사용법 | 미등록 — Docs/AnimationAuthoring_Tasks.md, Tools/BlenderAnimation/SKILL.md에서만 링크 |  |  |
| C:\Project\TDGame\Docs\AnimationAuthoring_Tasks.md | 할 일 대장(감사 결과 겸용) | 2026-09-16 감사 기준 남은 작업 A~G 31항목, 현재 상태 요약 표, 실행 환경 주의, 실측 메모, 권장 순서 | 미등록 — Docs/AnimationAuthoring.md에서만 링크 |  | 자유 서식 상태 값, D절이 decisions.md와 D-01 충돌 |
| C:\Project\TDGame\Docs\AnimationQuality.md | 가이드/작업 기록 | 무게감 있는 액션 스타일 기준, 첫 결과 문제 원인, 적용 방법, 후보 v04 검증, 재사용 스킬 | 미등록 — Docs/BlenderAnimationWorkflow.md에서만 링크 |  | git 수정 중(M) |
| C:\Project\TDGame\Docs\BlenderAnimationWorkflow.md | 가이드(작업 흐름) | Blender MCP ↔ Unreal 애니메이션 왕복 절차, 자연어 요청 예시, 판단 기준, 첫 예제 검증 기록, 연결 문제 확인 | 미등록 — Tools/BlenderAnimation/SKILL.md에서만 링크 |  | git 수정 중(M) |
| C:\Project\TDGame\Docs\PJGame_PortMap.md | 아키텍처(이식 지도) | PJGame→TDGame 클래스 대응표, 옮기지 않은 것, 폴더 구조(2026-09-12) | AGENTS.md §14 |  |  |
| C:\Project\TDGame\Docs\TDDamageSystemDesign.md | 아키텍처(전투, 2026-09-06) | 데미지 오브젝트 시스템 설계, GAS 전환 전 검증 동작 | 미등록 — TDDamageSystemGuide 링크, WorldDungeonPCG/02 코드 참조 |  |  |
| C:\Project\TDGame\Docs\TDDamageSystemGuide.md | 가이드(전투 사용법) | LV-Game 실행, 에셋 편집, 공격 구성, 노티파이, 검증 | 미등록 — TDDamageSystemDesign에서만 링크 |  | '에디터 테스트 보류' 상태 서술이 오래됨(git 2026-09-08) |
| C:\Project\TDGame\Docs\TDGASFoundation.md | 아키텍처(GAS 기반) | ASC·어트리뷰트·실행 경로·상태이상 태그·검증 상태(컴파일만) | 미등록 — Design/Guide 링크, WorldDungeonPCG/02 코드 참조. 07-roadmap이 ':85'를 인용 |  | 07 M0-01의 '28개 테스트 미실행' 근거 문서 |
| C:\Project\TDGame\Docs\TDMonsterAnimationOptimization_500_Research.md | 리서치(65KB) | 500마리 몬스터 애니메이션 성능·품질 연구(Instanced Skinned Mesh 등) | 미등록 — TDUAF 연구에서만 링크 |  |  |
| C:\Project\TDGame\Docs\TDUAF_Architecture_Usage_And_Transfer_Research.md | 리서치(67KB) | UAF(Unreal Animation Framework) 구조·사용법·이전 연구 | 고아(인바운드 0) |  |  |
| C:\Project\TDGame\Docs\UE_Engine_*_Guide.md (5개) | 외부 참고/가이드(출처 미표기) | 애니·물리·이동, 코어 시스템, 군중 애니 공유, 루멘, 월드 파티션 최적화 가이드(합 72KB) | 고아(인바운드 0) |  | 작성일·근거 파일:줄·작성 주체 없음, 문체가 프로젝트 문서와 다름 |
| C:\Project\TDGame\Docs\UE5_탑다운_ARPG_월드_던전_PCG_설계서.docx | 원본 설계서(외부 입력) | 2026-09-09 설계서, 17장+부록 2 | Docs/WorldDungeonPCG_Plan.md, GEMINI.md, WorldDungeonPCG/01 |  |  |
| C:\Project\TDGame\Docs\UKGame_FeatureFileMap.md | 외부 참고 인덱스 | UKGame 기능→코드 파일 맵(2026-09-08), 11개 절 문서 목차 | 사실상 고아 — 하위 11편 백링크만. AGENTS.md §12는 폴더 'Docs/UKGame/'만 언급 |  |  |
| C:\Project\TDGame\Docs\UKGame\ | 외부 참고(과거 설계) | 01~11 절: 스트리밍·어빌리티·AI·액터·프레임워크·UI·에디터·플러그인·상세 흐름 2·애니 최적화 | Docs/UKGame_FeatureFileMap.md, AGENTS.md §12(폴더) |  | '설계 기준 아님' 단서가 두 인덱스·AGENTS.md에 반복 |
| C:\Project\TDGame\Docs\Validation\ (최상위) | 검증 증거 | md 10(P1~P3 검증, 커맨드릿 스윕, C++ 이식, 도구 계층) + README + GAS 전 png 9·json 2 | 인덱스 없음. P3-11만 WorldDungeonPCG_Plan 코드 참조, P1/P2-12/P3-07/P3-09/commandlet은 phase 파일 기록 칸 코드 참조, 나머지 4 md는 고아 |  | README(2026-09-08)는 GAS 전 11개 파일만 설명 |
| C:\Project\TDGame\Docs\Validation\BlenderAnimation\ | 검증 증거(애니메이션) | environment/pie-validation json, player-attack01 세트, weighty-v03·v04 세트(poses png, slow/three-views/front gif, validation/pie json) | BlenderAnimationWorkflow·AnimationQuality에서 v04·player-attack01 일부만 참조. v03 세트 5개·environment-validation.json 미참조 |  | gif 6개 약 46MB(LFS). v04 5개 파일 미커밋(??) |
| C:\Project\TDGame\Docs\Validation\ashen-vale\ | 검증 증거(캡처) | 메인 지역 장소별 캡처 jpg 23 + layout_preview.png | P3-11-ashen-vale.md에서 8개만 파일명 언급, 16개 미참조 |  |  |
| C:\Project\TDGame\사용자용_할일_목록.md | 사용자 전용 메모(Docs 밖, 루트) | 사용자 개인 할 일, 첫 줄 'AI는 읽지 않습니다' | 미등록(의도적). AGENTS.md에 제외 규칙 없음 |  |  |

## 에이전트 진입점·설정·메모리 배선

## 에이전트별 진입점·설정·메모리 배선 (감사 기준일 2026-09-17)

용어: MCP(Model Context Protocol, 모델 컨텍스트 프로토콜), LFS(Git Large File Storage, 대용량 파일 저장소), CRLF/LF(줄 끝 문자 종류), PCG(Procedural Content Generation, 절차적 콘텐츠 생성).

### 표 1. 새 세션에서 자동으로 읽는 것 vs 사람이 지정해야 읽는 것

| 에이전트 | 자동 로드(규칙) | 자동 로드(MCP 설정) | 자동 로드(메모리·스킬) | 사람이 지정해야 읽는 것 | 근거 |
|---|---|---|---|---|---|
| Claude Code | `~/.claude/CLAUDE.md`(19줄)만. 프로젝트 `CLAUDE.md`·`.claude/CLAUDE.md`·`.claude/rules/` 없음. **AGENTS.md는 네이티브로 읽지 않음** — `~/.claude/CLAUDE.md:16` "프로젝트의 AGENTS.md을 참고 하세요" 문장을 모델이 따를 때만 읽음(강제 아님) | `C:\Project\TDGame\.mcp.json`(blender, unreal-mcp, serena 3개) | `~/.claude/projects/C--Project-TDGame/memory/MEMORY.md`(14줄 색인)만 매 세션 로드, 주제 파일 13개는 요청 시 로드. 스킬은 `~/.claude/skills/karpathy-guidelines` 1개뿐, 프로젝트 `.claude/skills/` 없음 | AGENTS.md, GEMINI.md, Tools/README.md, Tools/*/SKILL.md, Docs/* 전부 | 공식 문서 "Claude Code reads CLAUDE.md, not AGENTS.md"; MEMORY.md 첫 200줄/25KB만 로드, 주제 파일은 on-demand — https://code.claude.com/docs/en/memory (2026-09-17 확인) |
| OpenAI Codex | `~/.codex/AGENTS.md`(전역, 26줄) → 프로젝트 `AGENTS.md`(166줄, 15,694바이트) 순으로 **이어붙여** 로드(합계 32 KiB 한도, 현재 17.8 KB로 통과). 전역 마지막 줄 "AGENTS.md 참고 하지 않는다"와 프로젝트 파일이 같은 프롬프트에 동시 존재 | 전역 `~/.codex/config.toml`(serena `--context codex`, notion, playwright, node_repl 등) + 프로젝트 `.codex/config.toml`(unreal-mcp, blender). 프로젝트 설정은 `[projects.'c:\project\tdgame'] trust_level="trusted"`(전역 config.toml:83-84) 덕분에 로드됨 | 스킬 `~/.codex/skills/` 28개 폴더(td-combat-animation-quality 사본 포함). 메모리는 Codex 전용 | GEMINI.md, Tools/README.md, Docs/*, `Tools/AnimationAuthoring/SKILL.md`(미설치) | 전역 AGENTS.override.md/AGENTS.md → 루트에서 cwd까지 root-down 연결, 32 KiB 기본 — https://learn.chatgpt.com/docs/agent-configuration/agents-md.md ; 프로젝트 config는 신뢰 시만 로드 — https://learn.chatgpt.com/docs/config-file/config-reference ; 스킬 탐색 경로는 문서상 `.agents/skills`·`$HOME/.agents/skills`·`/etc/codex/skills`·시스템 — https://learn.chatgpt.com/docs/build-skills (2026-09-17 확인) |
| Gemini CLI | `~/.gemini/GEMINI.md`(전역, 29줄, frontmatter "적용: 항상", 마지막 줄 "AGENTS.md 참고 하지 않는다") + 프로젝트 `GEMINI.md`(37줄) 이어붙임. **AGENTS.md 자동 로드 없음** — `.gemini/settings.json`·`~/.gemini/settings.json` 어디에도 `context.fileName` 설정 없음. 프로젝트 GEMINI.md:4 "규칙의 원본은 AGENTS.md(전부 적용)"은 문장일 뿐 | `.gemini/settings.json`(blender, unreal-mcp `httpUrl`) | 없음 | AGENTS.md, Tools/README.md(GEMINI.md:20이 가리킴), Docs/* | 전역+워크스페이스 GEMINI.md 연결, `context.fileName`에 `["AGENTS.md","GEMINI.md"]` 배열 가능, `@file.md` import — https://geminicli.com/docs/cli/gemini-md/ (2026-09-17 확인) |
| Antigravity | 전역 규칙 = **Gemini CLI와 같은 파일** `~/.gemini/GEMINI.md`; 워크스페이스 규칙 `.agents/rules/`(또는 `.agent/rules/`) — 프로젝트에 없음; 루트 `GEMINI.md`·`AGENTS.md` 자동 읽기(공식 changelog 2.11.0이 "AGENTS.md 안 @path 인라인" 지원을 명시하므로 읽는 것은 확실, 우선순위는 미문서) | **저장소 밖** `~/.gemini/config/mcp_config.json`(unreal-mcp `serverUrl`, blender `uvx blender-mcp`) — AGENTS.md 11절의 5개 목록에 없음 | `~/.gemini/antigravity/knowledge/`(lock 파일만), `brain/` 대화 57개 — Antigravity 전용 | Tools/README.md, Docs/* | 규칙 위치 — https://antigravity.google/docs/rules-workflows/ ; changelog 2.11.0(2026-08-26) — https://antigravity.google/changelog ; AGENTS.md 지원 시작 1.20.5(2차 출처, 2026-08-05) — https://thepromptshelf.dev/blog/google-antigravity-agents-md-rules-guide-2026/ ; 전역 파일 공유 충돌 — https://github.com/google-gemini/gemini-cli/issues/16058 (모두 2026-09-17 확인) |
| Cursor | 루트 `AGENTS.md` 자동 + `.cursor/rules/*.mdc`(없음) | `.cursor/mcp.json`(blender, unreal-mcp) | 없음 | 나머지 전부 | https://cursor.com/docs/context/rules (2026-09-17 확인) |
| VS Code Copilot | 루트 `AGENTS.md` 자동(`chat.useAgentsMdFile`) + `.github/copilot-instructions.md`(없음) | `.vscode/mcp.json`(blender, unreal-mcp) | 없음 | 나머지 전부 | https://code.visualstudio.com/docs/copilot/customization/custom-instructions (2026-09-17 확인) |

결론: 여섯 클라이언트가 **공통으로 자동 로드할 수 있는 유일한 파일은 루트 AGENTS.md**뿐이며, 지금은 Codex·Cursor·VS Code·Antigravity가 자동 로드하고, Claude Code와 Gemini CLI는 "문장 포인터"에 의존한다. 그런데 Codex·Gemini·Antigravity의 전역 파일이 AGENTS.md를 무시하라고 하고, AGENTS.md 자체는 도구 카탈로그(`Tools/README.md`)와 빌드 명령을 한 번도 가리키지 않는다.

### 표 2. AGENTS.md 14절 구조와 길이 (총 166줄, 8,503자, 15,694바이트, CRLF)

| 절 | 자 수 | 비율 | 분류 |
|---|---|---|---|
| 머리말 | 100 | 1.2% | 모든 세션 |
| 1 적용 우선순위 | 212 | 2.5% | 모든 세션 |
| 2 프로젝트 범위 | 289 | 3.4% | 모든 세션 |
| 3 클래스·파일 이름 | 541 | 6.4% | 코드 작업 세션 |
| 4 제어 흐름 | 248 | 2.9% | 코드 작업 세션 |
| 5 주석 | 174 | 2.0% | 코드 작업 세션 |
| 6 설계 원칙 | 291 | 3.4% | 코드 작업 세션 |
| 7 로직 구현 정책(C++ 전용) | 565 | 6.6% | 모든 세션(핵심 정책) |
| 8 Git·LFS | 448 | 5.3% | 모든 세션 |
| 9 작업 절차 | 449 | 5.3% | 모든 세션 |
| 10 완료 보고 | 98 | 1.2% | 모든 세션 |
| 11 Serena·언리얼 MCP | **3,291** | **38.7%** | 특정 작업 상세(에디터·심볼 도구). 이 중 clangd 경로·UBT 명령줄·에디터 기동 절차(131-134, 142-143행)는 절차 매뉴얼 |
| 12 월드·던전·PCG 대장 | 377 | 4.4% | 특정 작업 포인터 |
| 13 몬스터 AI·전투 시뮬 대장 | 712 | 8.4% | 특정 작업 포인터+규칙 |
| 14 소스 폴더·PJGame 이식 | 708 | 8.3% | 폴더 구조(164행)는 코드 세션 공통, PJGame 이식(165행)은 특정 작업 |

집계: 모든 세션 필요(머리말·1·2·7·8·9·10) 2,161자(25%), 코드 세션 공통(3·4·5·6·14 전반) 1,600자 내외(19%), 특정 작업 상세(11·12·13·14 후반) 4,700자 내외(56%).

### 표 3. Claude 자동 메모리 14개 분류 (질문 4)

`type: feedback`(개인 취향·강조) 1개: `ukgame-is-legacy-design.md` — 다만 내용은 AGENTS.md:154가 이미 공유 규칙으로 담고 있음.

`type: project`(다른 에이전트도 알아야 할 프로젝트 사실) 13개. 이 가운데 저장소 어디에도 없는 사실을 담은 파일 **8개**:
1. `tdgame-build-and-test-workflow.md` — Build.bat 컴파일 명령과 `UnrealEditor-Cmd -ExecCmds="Automation RunTests TDGame.Combat"` 명령. AGENTS.md·GEMINI.md·Tools/README.md 어디에도 이 명령 없음(AGENTS.md:143은 "Build.bat"라는 단어만, Tools/ue_editor.py 안에만 2회 존재).
2. `tdgame-anim-notify-facts.md` — AnimNotifyState 공유 인스턴스·시간 좌표·루트모션 락 등 엔진 사실. Docs 전체 grep 결과 없음.
3. `tdgame-mcp-preflight.md` — 소스 줄 끝 LF 규칙(21행). 저장소 문서에 CRLF/LF 규칙 없음. 나머지(포트 8000 점검·에디터 자동 기동)는 AGENTS.md 11절·Tools/ue_editor.py와 중복.
4. `tdgame-unreal-mcp-toolsets.md` — Python 원격 실행 대체 절차, 몽타주 생성 레시피, PIE 테스트 레시피, 랜드스케이프 Slate 자동화. 툴셋 이름 목록은 Tools/README.md 1절과 중복.
5. `tdgame-worldgen-pipeline.md`(8,166바이트) — 조명 기준값, 텍스처 바밍 해법, 커맨드릿·에디터 재시작 함정, UHT 이름 충돌 등 실측 함정 10여 개. Tools/README.md 5절에는 일부(머티리얼·캡처·add_component)만 있음.
6. `tdgame-animation-authoring-status.md` — 패키지 `_DEADPACKAGE` 재이름 규칙, 스켈레톤 축 방향, 품질 향상 방법(기존 공격 애니메이션 위에 오른팔만 재계산). `_CONTROL` 이름·Git Bash 경로 함정은 Docs/AnimationAuthoring_Tasks.md:25,29에 있음.
7. `tdgame-external-actors-cleanup.md` — `Tools/check_orphan_external_actors.py`·`find_unused_content.py` 사용법과 보존 목록. Tools/README.md·Docs에 미등재.
8. `ukgame-reference-docs.md` — 절 추가 절차와 Claude 전용 스크립트(split_doc.py) 위치.

저장소 문서와 대체로 중복(포인터만 남겨도 되는 것) 5개: `tdgame-worldgen-task-board.md`(AGENTS.md 12절+Docs/Tasks/README.md), `tdgame-monster-ai-combat-sim-docs.md`(AGENTS.md 13절; 다중 에이전트 조사 절차만 새 내용), `tdgame-source-layout-and-pjgame-port.md`(AGENTS.md 14절), `tdgame-pcg-python-scripting.md`(Tools/WorldGen/README.md:51-52·Docs/Validation/P3-07-pcg-biome.md), `tdgame-mcp-preflight.md`의 점검 부분.

### 표 4. 스킬 배포 방식 불일치 (질문 5)

| 스킬 | 저장소 원본 | 설치본 | 상태 |
|---|---|---|---|
| td-combat-animation-quality | `Tools/BlenderAnimation/SKILL.md`(9,643 B, CRLF, 커밋 4df0823) | `~/.codex/skills/td-combat-animation-quality/SKILL.md`(9,580 B, LF, 2026-09-17 09:50) | `diff --strip-trailing-cr` 결과 내용 동일, 줄 끝만 다름. 수동 복사이며 동기화 장치 없음(Docs/AnimationQuality.md:52-53이 두 사본을 명시). Claude·Gemini·Antigravity에는 미설치 |
| td-animation-authoring | `Tools/AnimationAuthoring/SKILL.md`(4,553 B, CRLF) + `references/workflow.md` | 없음 | Docs/AnimationAuthoring_Tasks.md:178-179 "Claude·Codex 모두 미설치". Docs/AnimationAuthoring.md:26이 `$CODEX_HOME/skills`로 복사하라고 안내하지만 현재 Codex 공식 문서는 `.agents/skills`·`$HOME/.agents/skills`만 나열 |

MCP 설정 중복(질문 3): AGENTS.md:141은 "다섯 파일 모두 `http://127.0.0.1:8000/mcp`"라고 하며 이는 사실이다(.mcp.json:11, .codex/config.toml:2, .cursor/mcp.json:9, .gemini/settings.json:10, .vscode/mcp.json:11). 그러나 (a) Antigravity가 쓰는 여섯 번째 파일 `~/.gemini/config/mcp_config.json`이 목록에 없고, (b) Blender MCP 서버가 다섯 파일 모두에 있는데 AGENTS.md에는 "blender"라는 단어가 한 번도 없으며, (c) Antigravity의 blender 항목만 `uvx blender-mcp`로 저장소의 `Tools/BlenderMCP/Run-BlenderMCP.ps1` 방식과 다르다. Serena는 Claude(.mcp.json)·Codex(전역)만 갖고 있어 11절 Serena 지침은 Gemini·Antigravity·Cursor·VS Code에 적용 불가.

### 발견 사항
| 심각도 | 영역 | 사실 | 문제 | 근거 |
|---|---|---|---|---|
| high | 전역 지침 vs 프로젝트 지침 충돌 | `~/.codex/AGENTS.md:27`과 `~/.gemini/GEMINI.md:30` 마지막 줄이 "AGENTS.md 참고 하지 않는다"이다. 반면 프로젝트 `AGENTS.md:3`은 "Codex, Claude 및 기타 AI 에이전트가 따라야 하는 기본 규칙", `GEMINI.md:4`는 "규칙의 원본은 AGENTS.md(전부 적용)", `~/.claude/CLAUDE.md:16`은 "프로젝트의 AGENTS.md을 참고 하세요"이다. | Codex는 프로젝트 AGENTS.md가 컨텍스트에 들어가 있는데도 전역 지침이 무시하라고 하고, Gemini CLI·Antigravity는 GEMINI.md가 AGENTS.md를 원본이라 하면서 전역이 금지하므로 모델이 임의로 한쪽을 고른다. 사용자 요구 (a) '모든 에이전트가 세션 시작 때 AGENTS.md를 읽는다'가 구조적으로 불가능하다. | C:/Users/jjh/.codex/AGENTS.md:27, C:/Users/jjh/.gemini/GEMINI.md:30, C:\Project\TDGame\AGENTS.md:3, C:\Project\TDGame\GEMINI.md:4, C:/Users/jjh/.claude/CLAUDE.md:16. Codex는 전역과 프로젝트 AGENTS.md를 이어붙여 한 프롬프트에 넣는다(https://learn.chatgpt.com/docs/agent-configuration/agents-md.md, 2026-09-17 확인). Antigravity 전역 규칙도 같은 `~/.gemini/GEMINI.md`이다(https://antigravity.google/docs/rules-workflows/). |
| high | AGENTS.md가 도구 카탈로그를 가리키지 않음 | AGENTS.md 전문에 `Tools/`, `Tools/README.md`, `ue_editor.py`, `run_in_editor.py`, `uemcp.py`라는 문자열이 없다. 빌드 명령(Build.bat)은 143행에 단어만 있고 실제 명령줄은 없다. 도구 카탈로그는 GEMINI.md:20(Gemini만 자동 로드)과 Claude 자동 메모리(Claude만 봄)에서만 도달할 수 있다. | AGENTS.md를 자동 로드하는 Codex·Cursor·VS Code·Antigravity조차 어떤 도구가 있는지, 어떻게 컴파일·테스트하는지 알 수 없어 매 세션 탐색 토큰을 쓰거나 중복 도구를 만든다(.gemini/scripts 13개 중복이 그 결과). 사용자 요구 (a)의 '필요한 코드·도구를 알고 작업'이 충족되지 않는다. | C:\Project\TDGame\AGENTS.md 전체 읽기(1-166행), grep "Build.bat\|RunTests\|UnrealEditor-Cmd" → AGENTS.md:143 단어만, Tools/README.md:56-57은 커맨드릿 예시만. 실제 컴파일 명령은 C:/Users/jjh/.claude/projects/C--Project-TDGame/memory/tdgame-build-and-test-workflow.md:15,19과 Tools/ue_editor.py 안에만 있음. |
| high | 스킬 배포 방식 불일치 | `Tools/BlenderAnimation/SKILL.md`(CRLF, 9,643 B)와 `~/.codex/skills/td-combat-animation-quality/SKILL.md`(LF, 9,580 B)는 내용이 동일하고 줄 끝만 다른 수동 복사본이다. `Tools/AnimationAuthoring/SKILL.md`(td-animation-authoring)는 어느 클라이언트에도 설치되지 않았다. Claude는 `~/.claude/skills/karpathy-guidelines` 1개뿐이고 프로젝트 `.claude/skills/`·`.agents/skills/`·`.agent/`가 없다. Codex 공식 문서는 스킬 경로를 `.agents/skills`, `$HOME/.agents/skills`, `/etc/codex/skills`, 시스템으로 나열하고 `~/.codex/skills`는 언급하지 않는다(단, Codex가 2026-09-14에 `~/.codex/skills/.system/`을 직접 썼으므로 구 경로가 아직 스캔될 가능성이 높음). | 원본을 고쳐도 설치본은 갱신되지 않고, 어느 쪽이 정본인지 규칙이 없다. Gemini·Antigravity·Claude는 스킬을 자동 발견하지 못해 사람이 경로를 지정해야 한다. 사용자 요구 (b) '새 도구·리소스를 공통으로 관리'가 스킬에는 적용되지 않는다. | diff --strip-trailing-cr 결과 동일; file 명령 결과 CRLF vs LF; ls C:/Users/jjh/.codex/skills/ (28개, td-combat-animation-quality 2026-09-17 09:50); Docs/AnimationQuality.md:52-53; Docs/AnimationAuthoring.md:26; Docs/AnimationAuthoring_Tasks.md:178-179,190-191; https://learn.chatgpt.com/docs/build-skills (2026-09-17 확인). |
| medium | 완료 보고 형식 충돌 | `~/.codex/AGENTS.md:3-14`와 `~/.gemini/GEMINI.md:7-17`은 모든 답변을 '요약/접근 방식/코드 구현/설명' 네 절(`###` 헤딩, 코드 블록 첫 줄 파일명 주석)로 강제한다. 프로젝트 `AGENTS.md:108-115` 10절은 '변경한 파일/구현한 동작/수행한 검증과 결과/남은 모호성' 형식을 요구한다. | 두 템플릿이 서로 다른 항목을 요구해 Codex·Gemini의 보고에서 '검증 결과·남은 위험'이 빠지거나, 보고가 두 형식으로 중복돼 토큰이 낭비된다. | C:/Users/jjh/.codex/AGENTS.md:1-14, C:/Users/jjh/.gemini/GEMINI.md:5-17, C:\Project\TDGame\AGENTS.md:108-115 |
| medium | MCP 설정 중복과 11절 유지 규칙 | AGENTS.md:141은 클라이언트 설정 파일을 5개로 명시하고 주소 변경 시 '다섯 파일과 에디터 설정을 함께' 바꾸라고 한다. 실제로 Antigravity는 저장소 밖 `~/.gemini/config/mcp_config.json`을 쓰며(unreal-mcp `serverUrl`, blender `uvx blender-mcp`), 이 파일은 목록에 없다. 또 Blender MCP는 다섯 파일 모두에 등록돼 있으나 AGENTS.md에 'blender'가 한 번도 나오지 않고, Antigravity의 blender 실행 방식만 저장소의 `Tools/BlenderMCP/Run-BlenderMCP.ps1`와 다르다. Serena는 Claude(.mcp.json:13-25)와 Codex(전역 config.toml:18-20)에만 있다. | 주소·포트를 바꿀 때 Antigravity만 옛 값으로 남고, Blender MCP는 규칙 문서에 존재 자체가 없어 새 에이전트가 발견하지 못한다. 11절의 Serena 지침(3,291자 중 상당 부분)은 네 클라이언트에 적용 불가능한 내용이 매 세션 로드된다. | C:\Project\TDGame\AGENTS.md:127,141; .mcp.json:3-12; .codex/config.toml:1-16; .gemini/settings.json:3-11; .cursor/mcp.json:3-10; .vscode/mcp.json:3-12; C:/Users/jjh/.gemini/config/mcp_config.json(전체 12줄); C:/Users/jjh/.gemini/antigravity/mcp_config.json → ../config/mcp_config.json 심볼릭 링크; grep -i blender AGENTS.md GEMINI.md → 0건. |
| medium | 프로젝트 사실이 Claude 전용 메모리에 격리됨 | Claude 자동 메모리 14개 중 13개가 `type: project`이고, 그중 8개는 저장소 어디에도 없는 사실(빌드·테스트 명령줄, AnimNotify 엔진 사실, 소스 LF 규칙, Python 원격 실행 레시피, 월드 생성 함정 10여 개, 패키지 재이름 규칙, 정리 스크립트 사용법, UKGame 문서 추가 절차)을 담는다. `~/.claude/CLAUDE.md:20`은 'AI 에이전트 사용 프로세서를 간략하게 메모리에 저장'하라고 지시해 격리를 강화한다. | Codex·Gemini·Antigravity는 같은 함정을 다시 밟고 같은 조사를 반복한다(메모리 파일 스스로 '재조사 시 12만~1,300만 토큰'이라고 기록). 사용자 요구 (d) '문제 해결 기록 메모리'가 Claude 한 곳에만 존재한다. | C:/Users/jjh/.claude/projects/C--Project-TDGame/memory/ 14개 파일(합계 약 44 KB); grep 결과: Build.bat 명령·CRLF/LF 규칙·bExtractRootMotion·check_orphan_external_actors가 AGENTS.md·GEMINI.md·Tools/README.md·Docs/*.md에 0건; 주제 파일은 세션 시작 시 로드되지 않고 on-demand(https://code.claude.com/docs/en/memory, 2026-09-17 확인). |
| medium | 저장소 밖 Claude 전용 스크립트 | `~/.claude/projects/C--Project-TDGame/tools/`에 스크립트 15개(uemcp.py, uepy.py, make_montage.py, pie_attack.py, pie_after.py, run_tests.py, place_actors.py, cleanup_actors.py, reorg_tdgame.py, split_doc.py, append_section.py 등)가 있고 메모리 파일 4개가 이 경로를 절차의 일부로 가리킨다. 이 중 uemcp.py는 `Tools/uemcp.py`로 복사됐지만 나머지는 저장소에 없다. | 다른 에이전트는 몽타주 생성·PIE 테스트·문서 분할 같은 검증된 절차를 재구현해야 하며, 사용자 요구 (b)·(c)의 '공통 관리 규칙'에 어긋난다. | ls C:/Users/jjh/.claude/projects/C--Project-TDGame/tools/ (15개); memory/tdgame-mcp-preflight.md:17, tdgame-unreal-mcp-toolsets.md:20-23, ukgame-reference-docs.md:13, tdgame-source-layout-and-pjgame-port.md:10; Tools/ 목록에 uemcp.py만 존재. |
| medium | GEMINI.md의 역할 혼선 | 프로젝트 GEMINI.md(37줄)는 '세션 시작 절차', '언리얼 접근 경로 세 갈래', '도구가 없을 때' 규칙 등 에이전트 공통 내용을 담고 있으며 Tools/README.md 0·1·4절과 거의 같다. 그러나 Gemini CLI만 자동 로드하고, Claude Code·Codex·Cursor·VS Code는 읽지 않는다. GEMINI.md:4는 AGENTS.md를 '전부 적용'한다고 쓰지만 Gemini CLI는 AGENTS.md를 자동 로드하지 않는다(`context.fileName` 미설정). | 같은 절차가 두 파일에 있어 한쪽만 갱신되면 어긋나고, 공통 내용이 Gemini 전용 이름 아래 있어 다른 에이전트가 찾지 않는다. | C:\Project\TDGame\GEMINI.md:3-4,15-20,27-31; Tools/README.md:7-25,66-71; C:\Project\TDGame\.gemini\settings.json(mcpServers만), C:/Users/jjh/.gemini/settings.json(security·ide만); https://geminicli.com/docs/cli/gemini-md/ (2026-09-17 확인). |
| medium | AGENTS.md 11절 비대 | 11절 '개발 도구: Serena와 언리얼 MCP'가 3,291자로 전체 8,503자의 38.7%다. 131-134행(clangd 19.1.2 종료 문제, LLVM clangd 22.1.2 경로, UnrealBuildTool -mode=GenerateClangDatabase 명령줄)과 142-143행(에디터 기동 명령·1~3분 대기)은 규칙이 아니라 절차 매뉴얼이다. 12·13·14절까지 합치면 특정 작업 상세가 전체의 56%다. | 모든 세션에 절차 매뉴얼이 실려 토큰이 낭비되고, 규칙이 늘수록 Codex 32 KiB 한도(현재 전역 2.2 KB + 프로젝트 15.7 KB)에 가까워진다. 사용자가 중시하는 토큰 절약과 반대 방향이다. | C:\Project\TDGame\AGENTS.md:117-148 (파이썬으로 절별 문자 수 계산: 11절 3,291자, 12절 377자, 13절 712자, 14절 708자); Codex 32 KiB 합산 한도(https://learn.chatgpt.com/docs/agent-configuration/agents-md.md); Claude 문서 '200줄 이하 권장'(https://code.claude.com/docs/en/memory). |
| low | 모호성 처리 규칙 충돌 | `~/.gemini/GEMINI.md:25`는 '불분명한 부분이 있다면, 작업을 중단하세요', `~/.codex/AGENTS.md:22`는 '중대한 모호성이 있을 때만 질문하고 그 외에는 명시적 가정하에 진행', 프로젝트 `AGENTS.md:11`은 '중요한 모호성이 있으면 질문하거나 가정을 명시'이다. | Gemini·Antigravity는 사소한 불명확성에도 중단하도록 되어 있어 자율 작업(GEMINI.md의 '필요한 도구가 없으면 직접 만들어서 쓴다')과 상충한다. | C:/Users/jjh/.gemini/GEMINI.md:25, C:/Users/jjh/.codex/AGENTS.md:22, C:\Project\TDGame\AGENTS.md:11 |
| low | .gitignore 누락 | `.gitignore`에 `__pycache__/`·`*.pyc` 규칙이 없어 컴파일 캐시 10개가 추적 중이다(`.gemini/scripts/__pycache__/` 2개, `Tools/Animation/`·`Tools/AnimationAuthoring/`·`Tools/DungeonGen/`·`Tools/WorldGen/` 아래 8개). | 파이썬 도구를 만들수록 캐시가 커밋되고, 파이썬 버전이 다른 에이전트 간 diff 잡음이 생긴다. | C:\Project\TDGame\.gitignore 전체(1-95행, 언리얼 4개 폴더와 Serena 생성물만); `git ls-files \| grep -i pycache` 10건. |
| low | 줄 끝 정책 부재 | Claude 메모리(tdgame-mcp-preflight.md:21)는 소스 줄 끝을 LF로 유지하라고 하지만 저장소 문서에는 규칙이 없고, AGENTS.md·GEMINI.md·두 SKILL.md는 CRLF다. `.gitattributes`에는 LFS 규칙만 있고 `eol` 규칙이 없다. 스킬 사본은 LF로 복사돼 63바이트 차이가 났다. | 에이전트마다 다른 줄 끝으로 저장해 diff 전체가 바뀌거나 스킬 동기화 비교가 실패한다. | file 명령 결과(AGENTS.md, GEMINI.md, Tools/*/SKILL.md CRLF; ~/.codex/skills/.../SKILL.md LF); C:\Project\TDGame\.gitattributes 1-28행; grep -i "CRLF\|autocrlf\|줄 끝" AGENTS.md Tools/README.md Docs/*.md → 0건. |
| low | Claude MCP 승인 상태 이중 키 | `~/.claude.json`의 projects에 `C:\Project\TDGame`(hasTrustDialogAccepted true)와 `C:/Project/TDGame`(false, 별도 mcpServers)이 각각 있고 둘 다 `enabledMcpjsonServers: []`이다. | 경로 표기에 따라 다른 승인 상태가 적용돼 `.mcp.json` 서버 승인 여부가 세션마다 달라질 수 있다. | C:/Users/jjh/.claude.json projects 키 두 개(값은 이름만 확인, 비밀 값 미기록). |
| low | Antigravity 전역 규칙 파일 공유 | Antigravity 전역 규칙과 Gemini CLI 전역 컨텍스트가 같은 `~/.gemini/GEMINI.md`를 쓴다(알려진 충돌 이슈). 따라서 이 파일의 '출력 형식 강제'와 'AGENTS.md 참고 하지 않는다'는 두 도구에 동시에 적용된다. | 한 도구를 위해 전역 파일을 고치면 다른 도구에 의도치 않게 적용된다. Antigravity는 루트 AGENTS.md를 자동으로 읽는데(changelog 2.11.0 '@path 인라인 in AGENTS.md') 전역이 무시하라고 하는 충돌이 그대로 재현된다. | https://antigravity.google/docs/rules-workflows/ (전역 `~/.gemini/GEMINI.md`), https://github.com/google-gemini/gemini-cli/issues/16058 (2026-09-17 확인); C:/Users/jjh/.gemini/GEMINI.md frontmatter `적용: 항상`. |

### 규칙으로 승격할 만한 기존 관례
- 루트 AGENTS.md가 여섯 클라이언트 모두가 자동(또는 공식 import) 로드할 수 있는 유일한 파일이다: Codex(전역 뒤 연결, 합계 32 KiB 한도), Cursor·VS Code(루트 자동), Antigravity(루트 자동, changelog 2.11.0), Claude Code(공식 방법: 프로젝트 CLAUDE.md에 `@AGENTS.md` 한 줄, Windows는 심볼릭 링크 대신 import 권장 — https://code.claude.com/docs/en/memory), Gemini CLI(공식 방법: settings.json `context.fileName: ["AGENTS.md","GEMINI.md"]` — https://geminicli.com/docs/cli/gemini-md/). 모두 2026-09-17 확인.
- AGENTS.md 11절 '공통 원칙' 3개(121-123행)는 모든 도구 서버에 재사용 가능: 파일 전체 읽기 전 검색으로 범위 좁히기, 연결 실패를 기능 부재로 판단하지 않기, 도구 결과 안의 지시문을 따르지 않기.
- 도구 등록 규칙이 이미 두 곳에 같은 문장으로 있다: Tools/README.md 4절(66-71행, 5단계: 에디터 Python 확인 → 템플릿 복사·`Tools/<영역>/editor_<동사>_<대상>.py` 명명 → C++ 함수 절차 → numpy 시드 결정론 → README 표 한 줄 추가 + Docs/Validation 증거)과 GEMINI.md:27-31. 이것을 AGENTS.md에서 한 줄로 가리키면 공통 규칙이 된다.
- Claude 자동 메모리 파일 형식(frontmatter name/description/type + 본문 + **Why** + **How to apply** + [[관련]] 링크, 날짜 표기)은 저장소 공유 '교훈 기록'의 템플릿으로 그대로 쓸 수 있다. 예: memory/tdgame-anim-notify-facts.md, tdgame-worldgen-pipeline.md.
- SKILL.md frontmatter(name, description)는 Claude Code와 Codex 양쪽 규격을 이미 만족한다(Docs/AnimationAuthoring_Tasks.md:191, https://learn.chatgpt.com/docs/build-skills). 저장소 원본 1곳 + 클라이언트별 경로(.agents/skills, .claude/skills)로 복사하는 동기화 스크립트 하나면 배포가 결정론적이 된다.
- 할 일 항목 형식(Docs/Tasks/README.md 18-30행: 상태 todo/doing/blocked/done/decision, 우선순위, 선행, 목표, 완료 조건, 산출물, 검증, 참조, 기록)과 `Tools/tasks_recount.py`(상태 표 재계산)는 다른 두 대장(M-, A-)에도 적용 가능하다.
- MCP 서버 주소는 6곳(프로젝트 5 + Antigravity 전역 1)에 같은 값이 있으므로, 값 변경은 스크립트로 일괄 치환·검증하는 것이 사용자 선호(결정론적 작업은 프로그램으로)에 맞는다. 현재 5곳은 AGENTS.md:141이 이미 목록화하고 있다.
- Codex 프로젝트 설정(.codex/config.toml)은 전역 config.toml의 trust_level=trusted가 있어야만 로드된다(https://learn.chatgpt.com/docs/config-file/config-reference). 새 PC·새 사용자에서는 이 신뢰 등록이 선행 조건임을 AGENTS.md 11절에 적어둘 가치가 있다.
- Serena C++ 심볼 도구의 3조건(project.yml language_servers cpp, compile_commands.json, LLVM clangd 22.1.2 경로)은 AGENTS.md:131-134와 실제 ~/.serena/projects/TDGame/.serena/project.yml:38-39,77-78이 일치함을 확인했다. 이 절차는 Serena를 쓰는 Claude·Codex에만 해당하므로 '조건부 절'로 분리할 수 있다.

### 열린 질문
- 전역 파일 수정 여부: `~/.codex/AGENTS.md:27`과 `~/.gemini/GEMINI.md:30`의 'AGENTS.md 참고 하지 않는다'를 삭제하거나 'TDGame 등 AGENTS.md가 있는 저장소에서는 따른다'로 조건화할지. 두 파일은 다른 프로젝트(pjgame, gp, stock 등)에도 적용되므로 사용자만 결정할 수 있다.
- 전역 출력 형식(요약/접근 방식/코드 구현/설명 4절 강제)을 TDGame 작업에서도 유지할지, AGENTS.md 10절 완료 보고(변경 파일/동작/검증/남은 모호성)와 하나로 합칠지. 합친다면 어느 쪽 파일에 둘지.
- Claude Code 진입점: 저장소에 `CLAUDE.md`(내용 `@AGENTS.md` 한 줄)를 커밋해 공식 import 방식으로 바꿀지, 지금처럼 `~/.claude/CLAUDE.md:16` 문장에 의존할지. 커밋하면 저장소에 파일이 하나 늘고, 의존하면 다른 PC에서는 안 읽힌다.
- Gemini CLI: `.gemini/settings.json`에 `context.fileName: ["AGENTS.md","GEMINI.md"]`를 추가해 AGENTS.md를 자동 로드시킬지. 그러면 전역 GEMINI.md의 금지 문장과 즉시 충돌하므로 1번 결정과 묶인다.
- GEMINI.md의 처리: 공통 내용(세션 시작 절차·접근 경로·도구 규칙)을 Tools/README.md 또는 AGENTS.md로 옮기고 GEMINI.md는 'AGENTS.md와 Tools/README.md를 읽어라' 3줄로 줄일지, 아니면 Gemini 전용 절차를 계속 별도 유지할지.
- Antigravity MCP 설정(`~/.gemini/config/mcp_config.json`)을 저장소 방식(Run-BlenderMCP.ps1)으로 맞출지, 그리고 AGENTS.md 11절 목록에 사용자 홈 경로를 6번째로 적을지(개인 경로가 공용 문서에 들어감).
- 스킬 정본과 배포 경로: 저장소 원본(Tools/*/SKILL.md)을 정본으로 하고 `.agents/skills/`(Codex 현재 공식 경로)와 `.claude/skills/`에 동기화 스크립트로 복사할지, 심볼릭 링크(Windows 관리자 권한 필요)로 할지. `~/.codex/skills`가 아직 스캔되는지는 Codex에서 `/skills`로 사용자가 확인해야 한다. Gemini·Antigravity용 스킬 경로는 공식 문서에 없어 '문서 경로 지정' 방식으로 남길지 결정 필요.
- Claude 자동 메모리 13개 project 파일 중 저장소에 없는 사실 8개를 공유 기록(예: Docs/Lessons/ 또는 Tools/README.md 5절 확장)으로 옮길지, 옮긴 뒤 Claude 메모리는 포인터만 남길지. 또한 `~/.claude/CLAUDE.md:20` '메모리에 저장' 문장을 '저장소 공유 기록에 저장'으로 바꿀지(전역 파일 수정).
- `~/.claude/projects/C--Project-TDGame/tools/` 15개 중 저장소에 없는 14개(make_montage.py, pie_attack.py, run_tests.py, split_doc.py, reorg_tdgame.py 등)를 Tools/로 옮겨 README에 등록할지, 일회성으로 보고 폐기할지.
- `.gitignore`에 `__pycache__/`·`*.pyc`를 추가하고 추적 중인 .pyc 10개를 `git rm --cached`로 뺄지(커밋이 필요하므로 사용자 지시 사항).
- 줄 끝 정책: 소스는 LF(Claude 메모리 기록), 마크다운·SKILL.md는 현재 CRLF. `.gitattributes`에 `*.md text eol=crlf` 또는 `*.py text eol=lf` 같은 규칙을 둘지, 두지 않을지.
- AGENTS.md 11절(3,291자)에서 절차 매뉴얼 부분(clangd 경로·UBT 명령·에디터 기동 절차)을 Tools/README.md 또는 스킬로 옮기고 11절에는 규칙과 포인터만 남길지. 옮기면 Codex·Cursor·VS Code의 매 세션 토큰이 줄지만 Claude·Gemini는 포인터를 따라가야 한다.
- `~/.claude.json`의 프로젝트 키 두 개(`C:\Project\TDGame` trusted, `C:/Project/TDGame` untrusted)를 정리할지 — Claude Code 설정 파일이라 사용자가 `/mcp` 또는 직접 편집으로만 처리 가능.

### 인벤토리
| 경로 | 종류 | 용도 | 등록 위치 | 중복 | 비고 |
|---|---|---|---|---|---|
| C:\Project\TDGame\AGENTS.md | 공용 규칙(14절, 166줄, 15,694 B, CRLF) | 모든 에이전트의 기본 규칙. 7절 C++ 전용 정책, 8절 Git·LFS, 11절 Serena·언리얼 MCP, 12·13절 작업 대장 포인터, 14절 폴더 구조 | GEMINI.md:4, Tools/README.md:5, ~/.claude/CLAUDE.md:16(문장 포인터). 자동 로드: Codex(전역 뒤에 연결), Cursor, VS Code, Antigravity. 미로드: Claude Code, Gemini CLI |  | Tools/README.md·빌드 명령·Blender MCP·스킬을 한 번도 가리키지 않음. 11절이 38.7% |
| C:\Project\TDGame\GEMINI.md | Gemini 진입점(37줄, CRLF) | 세션 시작 절차, 언리얼 접근 경로 3갈래, 도구 없을 때 규칙, 도구 등록 규칙(20행) | Gemini CLI·Antigravity 자동 로드. Claude·Codex·Cursor·VS Code는 미로드 | Tools/README.md 0·1·4절 | 내용은 에이전트 공통인데 파일명이 Gemini 전용 |
| C:\Project\TDGame\.mcp.json | Claude Code MCP 설정 | blender(Run-BlenderMCP.ps1), unreal-mcp(http 8000), serena(uvx --context claude-code) | AGENTS.md:127,141 |  | 6개 클라이언트 중 Serena를 가진 둘 중 하나 |
| C:\Project\TDGame\.codex\config.toml | Codex 프로젝트 MCP 설정(16줄) | unreal-mcp, blender(승인 prompt, 타임아웃) | AGENTS.md:141; 전역 config.toml:83-84 trust_level=trusted로 로드 허용 |  | Serena는 전역 config.toml:18-20에 있음 |
| C:\Project\TDGame\.gemini\settings.json | Gemini CLI 프로젝트 설정 | blender, unreal-mcp(httpUrl) | AGENTS.md:141 |  | context.fileName 없음 → AGENTS.md 자동 로드 안 됨 |
| C:\Project\TDGame\.cursor\mcp.json | Cursor MCP 설정 | blender, unreal-mcp(url) | AGENTS.md:141 |  | .cursor/rules 없음 |
| C:\Project\TDGame\.vscode\mcp.json | VS Code MCP 설정(servers 키) | blender, unreal-mcp(http) | AGENTS.md:141 |  | .github/copilot-instructions.md 없음 |
| C:/Users/jjh/.gemini/config/mcp_config.json | Antigravity MCP 설정(저장소 밖, 12줄) | unreal-mcp(serverUrl 8000), blender(uvx blender-mcp) | 미등록(AGENTS.md 11절 목록에 없음) | 위 5개 프로젝트 MCP 파일 | ~/.gemini/antigravity/mcp_config.json이 이 파일로의 심볼릭 링크. blender 실행 방식이 저장소와 다름 |
| C:\Project\TDGame\.gitignore | Git 제외 규칙(95줄) | 언리얼 생성물 4폴더, compile_commands.json, .serena/, .clangd | AGENTS.md 2·8절 취지와 일치 |  | __pycache__/·*.pyc 없음 → .pyc 10개 추적 중 |
| C:\Project\TDGame\.gitattributes | Git LFS 규칙(28줄) | uasset·umap·fbx·blend·png·gif 등 LFS | AGENTS.md 8절 |  | eol(줄 끝) 규칙 없음 |
| C:/Users/jjh/.claude/CLAUDE.md | Claude 사용자 전역 지침(19줄) | 깊이 사고·하위 에이전트·토큰 절약·축약어 풀어쓰기; 16행 AGENTS.md 포인터; 20행 '메모리에 저장' | Claude Code 자동 로드 |  | AGENTS.md를 읽게 하는 유일한 장치가 이 문장 하나 |
| C:/Users/jjh/.claude/settings.json | Claude 사용자 설정 | env CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1, Notion 플러그인, autoUpdatesChannel | Claude Code 자동 로드 |  | hooks·permissions·autoMemory 설정 없음 |
| C:/Users/jjh/.codex/AGENTS.md | Codex 사용자 전역 지침(26줄) | 출력 형식 4절 강제, 설계 원칙, 금지 사항(27행 'AGENTS.md 참고 하지 않는다') | Codex 자동 로드(프로젝트 AGENTS.md 앞에 연결) |  | 프로젝트 AGENTS.md와 직접 충돌 |
| C:/Users/jjh/.codex/config.toml | Codex 사용자 전역 설정(152줄) | 모델·추론 강도, MCP(notion, openaiDeveloperDocs, playwright, serena --context codex, node_repl), features multi_agent·child_agents_md, 신뢰 프로젝트 목록(tdgame 포함), 플러그인 | Codex 자동 로드 |  | 민감 값(auth·파이프 경로 등)은 기록하지 않음 |
| C:/Users/jjh/.gemini/GEMINI.md | Gemini CLI 전역 + Antigravity 전역 규칙(같은 파일, 29줄, frontmatter 적용: 항상) | 출력 형식 4절 강제, 설계 원칙(25행 불분명하면 중단), 금지 사항(30행 'AGENTS.md 참고 하지 않는다') | Gemini CLI·Antigravity 자동 로드 |  | 프로젝트 GEMINI.md:4·AGENTS.md와 충돌 |
| C:/Users/jjh/.gemini/settings.json | Gemini CLI 사용자 설정(10줄) | security.auth gateway, ide.hasSeenNudge | Gemini CLI 자동 로드 |  | context.fileName 없음 |
| C:/Users/jjh/.claude/projects/C--Project-TDGame/memory/MEMORY.md | Claude 자동 메모리 색인(14줄) | 주제 파일 13개 색인 | Claude Code 매 세션 자동 로드(첫 200줄/25KB) |  | 주제 파일은 on-demand. project 13 + feedback 1 |
| C:/Users/jjh/.claude/projects/C--Project-TDGame/memory/*.md (13개) | Claude 자동 메모리 주제 파일(합계 약 42 KB) | 빌드·테스트, AnimNotify 사실, MCP 사전 점검, 툴셋, UKGame 문서, 월드 대장, 몬스터 AI, 월드 파이프라인, PCG, 소스 구조, 애니메이션 저작 현황, 정리 스크립트 | MEMORY.md에만 |  | 8개 파일이 저장소에 없는 사실 보유(요약 표 3) |
| C:/Users/jjh/.claude/projects/C--Project-TDGame/tools/ (15개 .py) | Claude 전용 스크립트 | uemcp.py, uepy.py, make_montage.py, pie_attack.py, pie_after.py, run_tests.py, place_actors.py, cleanup_actors.py, reorg_tdgame.py, split_doc.py, append_section.py, save_capture.py 등 | Claude 메모리 4개 파일에서만 참조 | uemcp.py → Tools/uemcp.py | 나머지 14개는 저장소에 없음 |
| C:\Project\TDGame\Tools\BlenderAnimation\SKILL.md | 스킬 원본 td-combat-animation-quality(9,643 B, CRLF, 커밋 4df0823) | Blender MCP·Unreal MCP 전투 애니메이션 품질 절차 | Docs/AnimationQuality.md:53, Docs/BlenderAnimationWorkflow.md:3. Tools/README.md 미등재 |  | frontmatter name·description 있음 |
| C:/Users/jjh/.codex/skills/td-combat-animation-quality/SKILL.md | 스킬 설치본(9,580 B, LF, 2026-09-17 09:50) | Codex용 복사 | Docs/AnimationQuality.md:52 | Tools/BlenderAnimation/SKILL.md | 내용 동일, 줄 끝만 다름. Codex 현재 문서상 스킬 경로는 .agents/skills |
| C:\Project\TDGame\Tools\AnimationAuthoring\SKILL.md (+references/workflow.md 18,202 B) | 스킬 원본 td-animation-authoring(4,553 B, CRLF) | Unreal MCP로 시퀀스·몽타주 저작 | Docs/AnimationAuthoring.md:14,22,26; Docs/AnimationAuthoring_Tasks.md:178-179(미설치 명시). Tools/README.md 미등재 |  | 어느 클라이언트에도 설치되지 않음 |
| C:/Users/jjh/.claude/skills/karpathy-guidelines | Claude 사용자 스킬 | 코딩 행동 지침 | Claude Code 자동 발견 |  | 프로젝트 스킬 폴더(.claude/skills) 없음 |
| C:/Users/jjh/.serena/projects/TDGame/.serena/project.yml | Serena 프로젝트 설정 | language_servers: [cpp](38-39행), ls_specific_settings.cpp.ls_path = C:/Program Files/LLVM/bin/clangd.exe(77-78행) | AGENTS.md:131,133 |  | AGENTS.md 11절 기술과 일치. 저장소 루트 compile_commands.json 존재(gitignore) |
| C:\Project\TDGame\Tools\README.md | 도구 카탈로그(0~5절) | 세션 시작 점검(0절), 범용 도구 3갈래(1절), WorldGen·DungeonGen·C++ 도구 표, 도구 생성 규칙(4절), 실측 함정(5절) | GEMINI.md:20,31. AGENTS.md 미참조 |  | Tools/Animation·AnimationAuthoring·BlenderAnimation·BlenderMCP·check_orphan_external_actors.py·find_unused_content.py·editor_regenerate_pcg.py 미등재(tasks_recount.py는 63행에 있음) |
| C:\Project\TDGame\Docs\Tasks\README.md | 할 일 대장 규칙 | 항목 형식(상태/우선순위/선행/목표/완료 조건/산출물/검증/참조/기록) | AGENTS.md:152 |  | 13절 대장·AnimationAuthoring_Tasks.md와 형식이 조금 다름(전제 5) |
| C:/Users/jjh/.claude.json (projects 항목) | Claude Code 프로젝트 승인 상태 | hasTrustDialogAccepted, enabledMcpjsonServers | Claude Code 자동 |  | `C:\Project\TDGame`(trusted)과 `C:/Project/TDGame`(untrusted) 두 키. 비밀 값 미기록 |


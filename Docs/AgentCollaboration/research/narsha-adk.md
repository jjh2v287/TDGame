[← 인덱스로](../../AgentCollaboration_Plan.md)
# NarshaADK(NarshaMCP) 활용 검토와 에이전트의 나이아가라 제작 가능성

상태: **2026-10-23 23:11 사용자 지시로 NarshaADK를 제거함**(D-61 superseded, 절차 L-repo-24). 이 문서는 재도입 판단용 기록으로 남긴다.

종류: 리서치 · 작성: 2026-10-23 claude · 근거: 이 세션 실측, 패키지 감사 `Saved/AgentOps/20261023/narsha-adk/package-audit.md`, 이전 조사 `Saved/AgentOps/20261021/niagara-agent/answer.md`, 결정 D-57. 제품 소개 문구(검색 속도 배수·자동 수정률)는 검증하지 않았다.

약어: MCP(Model Context Protocol, 모델 문맥 프로토콜) · PIE(Play In Editor, 에디터 안 플레이) · DI(Data Interface, 나이아가라 데이터 인터페이스) · PDB(Program Database, 디버그 심볼 파일) · HLSL(High-Level Shading Language, 셰이더 언어).

## 1. 결론

1. **에이전트는 나이아가라를 만들 수 있다. 이미 이 프로젝트에서 한 번 증명됐다.** 2026-10-21 N0 관문에서 `Tools/FluidFX/editor_make_fluid_ember_niagara.py`가 엔진 공식 NiagaraToolset(MCP 포트 8000)으로 GPU 불티 시스템을 처음부터 조립했다. 모듈 추가, HLSL 식 입력, 사용자 매개변수 14개, 메시 렌더러를 넣었고, 세 번 생성한 결과가 sha256까지 같았으며 오프스크린 수치 시험 3종을 통과했다(D-57). 한계는 도구가 아니라 두 곳에 있다. 새 모듈 스크립트와 Custom HLSL 노드는 공식 도구가 없고(4단계), "보기 좋게" 맞추는 일은 판단 루프가 병목이다(5단계, FX-8c 20회차 사례).
2. **NarshaMCP는 나이아가라를 "만드는" 주 도구로는 부족하고, 에디터 없이 점검하는 보조 도구로 쓸 만하다.**
   - 소스로 확인한 한계: `create_system`은 템플릿 없는 빈 시스템을 만들고, `add_system_emitter`는 기존 이미터 에셋을 복사만 한다. 모듈 추가는 이벤트 생성기 한 종류뿐이다. 렌더러 추가와 모듈 입력 상수·HLSL 식 설정은 C++에 아예 없다(AddModule·AddRenderer·SetStackInputData 0건).
   - 실익: 에디터가 꺼져 있거나 다른 세션이 빌드 창을 잡고 있을 때도 `.uasset`을 직접 읽는다. 이 프로젝트처럼 세션이 동시에 도는 환경에서는 이 점이 쓸모 있다.
3. **오프라인 읽기는 정답 판정에 쓰면 안 된다(L-editor-56).** 정답이 알려진 `NS_TD_FluidEmber`로 대조해 보니 구조(GPU·경계·렌더러·재질)는 맞았다. 그러나 사용자 매개변수는 14개 중 3개만 읽었고, 값은 모두 0이었으며, 영속 ID 플래그가 반대로 나왔고, 모듈의 단계 배치도 사라졌다(4절).
4. 코드 탐색 도구(`ue_analyze_symbols`·`ue_grep`)는 정확했다. PDB와 소스를 섞은 호출자 찾기, 블루프린트 교차 참조처럼 Serena가 못 하는 `.uasset` 쪽을 보완한다(첫 호출 때 호출 그래프를 만드는 데 65초가 걸린다).
5. **들이는 방식은 "MCP 서버만 켜고 플러그인 7개는 끈다"를 권한다(7절).** 스킬 115개와 시작 훅은 프로젝트 규칙과 부딪친다. 블루프린트 그래프 생성, AnimBP·몽타주 저작을 권하고, Serena 우선 탐색 순서를 뒤집는다. 또 프로젝트 에이전트 7개의 복사본을 같은 이름으로 한 벌 더 만든다. 이것들을 끄면 매 요청에 실리는 스킬 목록 토큰도 줄어든다.

## 2. 설치된 것

| 구성 요소 | 위치 | 하는 일 |
|---|---|---|
| 에디터 플러그인 NarshaMCP 0.13.7(Next Stage Inc.) | `Plugins/Marketplace/NarshaMCP/`(git 미추적, 파일 1113개) | 에디터 안 WebSocket 서버(127.0.0.1:30011~30025, 인증 없음, `execute_python` 포함), 에디터 저작 처리기 |
| MCP 서버 `narshamcp.exe`(Rust, 약 126MB) | 같은 폴더 `Source/ThirdParty/bin/` | 도구 60개. `.uasset` 바이너리 파서, PDB 호출 그래프, 에디터 원격 호출 |
| Claude Code 플러그인 7개(마켓플레이스 `narsha-adk`) | `~/.claude/plugins/cache/narsha-adk/`, 활성은 `.claude/settings.local.json` | 스킬 115개, 에이전트 7개, core의 SessionStart 훅 |
| 설치기 `narshamcp.exe setup` | 첫 빌드 + **에디터 시작마다** 재실행(CUModule.cpp:301-340) | `.mcp.json`·`.cursor/mcp.json`·`.claude/agents/unreal-specialist.md`·`.claude/skills/routing.json`·`.claude/settings.local.json`·`.antigravity/`와 홈의 `~/.codex/config.toml`·`~/.gemini/antigravity/mcp_config.json`·Codex 스킬 3개·`narshamcp://` 프로토콜 핸들러를 씀 |

- 텔레메트리(Firebase/GA4)와 로그인·설정 동기화·자동 갱신 코드가 exe에 있다. 동의 파일은 `disabled`(2026-10-23 02:01Z)다. 끄는 환경 변수는 `NARSHA_TELEMETRY_DISABLED`이다.
- 라이선스 파일이 패키지에 없다. 조건은 Fab 페이지에서 확인해야 한다(미확인).
- 상세 줄 번호는 `Saved/AgentOps/20261023/narsha-adk/package-audit.md`에 있다.

## 3. 나이아가라 제작 단계별 판정

| 단계 | 내용 | 판정 | 쓰는 도구 | 근거 |
|---|---|---|---|---|
| 0 | 기존 시스템 읽기·1차 점검 | 가능(에디터 없이) | NarshaMCP `get_structure`·`validate`·`find_asset_consumers` | 이 세션 실측. 정확도 한계는 4절 |
| 1 | 기존 값 바꾸기(사용자 매개변수 기본값·렌더러 재질/메시·시뮬 대상·고정 경계·이미터 켜기) | 가능 | 공식 `SetStackInputData`·`SetEmitterData`(실측) / NarshaMCP 저작 작업(에디터 필요, 미실측) | D-57, 도구 문서 |
| 2 | 이미터 조립(새 시스템 + 이미터) | 가능 | 공식 `CreateNiagaraSystem`·`AddEmitter`(실측) / NarshaMCP `create_system`·`add_system_emitter`(미실측) | N0 |
| 3 | 모듈 스택 저작(엔진 모듈 추가, 입력 상수·링크·동적 입력·HLSL 식) | 가능·재현 확인 | 공식 `AddModule`·`SetStackInputData`·`AddRenderer`·`AddUserVariables` + 에디터 Python 저장 | N0, sha256 3회 동일 |
| 3a | 동적 입력 중첩·이벤트 생성기/처리기 배선 | 가능(편의) | NarshaMCP `attach_dynamic_input`(깊이 16·노드 256 제한, 에디터 전 사전 형식 검사)·`add_event_generator`·`add_event_handler` | 도구 문서, 미실측 |
| 4 | 새 모듈 스크립트·Custom HLSL 노드·시뮬레이션 스테이지 그래프 | 공식·NarshaMCP 모두 도구 없음 | `Source/TDGameEditor`에 C++ 래퍼(외부 편집 API `UNiagaraExternalEditUtilities` 등)를 두면 가능 | D-57 [추정 1세션 규모] |
| 4a | Grid2D·Grid3D 유체를 엔진 NiagaraFluids 템플릿으로 생성·조정 | 가능(플러그인 켜기 필요) | 공식 `CreateNiagaraSystem(TemplateSystem)`·`AddEmitter(TemplateEmitter)` + 노출된 사용자·이미터 매개변수 조정 | NiagaraFluids(베타)는 엔진에 있으나 이 프로젝트에서 꺼져 있음(`TDGame.uproject`에 없음). 스택 참조가 스테이지 GUID 없이 찾아(`FindScriptGroup(Usage, FGuid())`, NiagaraExternalSystemEditorUtilities.cpp:941-942) 시뮬레이션 스테이지 안 모듈 입력은 못 가리킴[추정] |
| 4b | 완전 커스텀 그리드 솔버(시뮬레이션 스테이지 + Custom HLSL 모듈) | 지금 도구로는 불가, C++ 래퍼로 길은 있음 | 공식 툴셋·NarshaMCP 모두 스테이지 추가·스크래치 패드·Custom HLSL 노드 작성 없음. 엔진에는 `UNiagaraEmitter::AddSimulationStage`(NIAGARA_API, NiagaraEmitter.h:988)와 편집 가능 프로퍼티 `CustomHlsl`(NiagaraNodeCustomHlsl.h:88-89)이 있으나 노드 클래스가 MinimalAPI라 핀·배선은 래퍼가 직접 구성 | 여러 세션 규모[추정]. D-49(accepted)는 "나이아가라 출력 전용·솔버 금지"라 게임 적용 전 재결정 필요 |
| 5 | 참고 영상에 맞는 보기 좋은 결과 | 도구는 충분, 판단 루프가 병목 | PIE 촬영 + `measure_fx_look.py` 수치 + 사람 승인 | FX-8c 20회차: 매개변수 레버만으로는 구조 격차를 못 넘고 종결 |

검증 경로:
- `-NullRHI` 헤드리스에서는 나이아가라가 켜지지 않는다(L-editor-29).
- 오프스크린·PIE에서는 Sim Cache 수치 시험이 된다(N0의 `TDGame.FluidFXRhi.Niagara.Ember.*`).
- 화면 비교도 되지만 허용 오차를 두는 통계 판정이어야 한다.
- NarshaMCP `get_preview`(나이아가라 편집기 뷰포트 캡처)와 `capture_simcache`(PIE)는 이 경로를 짧게 만든다(미실측).

## 4. NarshaMCP 오프라인 읽기 정확도 실측 (2026-10-23)

대상은 `/Game/FluidFX/Niagara/NS_TD_FluidEmber`다. 정답은 생성 스크립트 상수와 에디터 정규화 덤프 `Saved/FluidFX/niagara/NS_TD_FluidEmber.dump.json`이다.

| 항목 | 정답 | NarshaMCP 0.13.7 | 판정 |
|---|---|---|---|
| 시뮬 대상 | GPUComputeSim | GPUComputeSim | 맞음 |
| 고정 경계 | ±600 | ±600 | 맞음 |
| 결정론·시드 | 켬·1 | true·1 | 맞음 |
| 렌더러·메시·재질 | 메시, Sphere, `MI_TD_FluidEmberNiagara_Default` | 같음 | 맞음 |
| `validate` | 결함 없음 | 결함 0, 재질 사용 플래그 대조 수행 | 맞음 |
| SetVariables 노드 수 | 9 | 9 | 맞음 |
| 사용자 매개변수 | 14개(벡터·텍스처 포함) | 3개(실수만) | 틀림 |
| 사용자 매개변수 값 | EmberSpawnRate 10, VelocityReferenceCmPerSec 600 | 모두 0 | 틀림 |
| 영속 ID 필요 | true | false | 틀림 |
| SampleTexture Mip Level | NiagaraFloat | NiagaraInt32, 값 23040 | 틀림 |
| 모듈 단계·순서 | 단계별 스택(SampleTexture 2개) | 단계 "unknown" 한 칸, 중복 제거(1개) | 정보 손실 |
| `performance_audit` 범위 | 이 시스템만 요청 | 227개 전체를 훑고 요약 GPU 시스템 0 | 범위 무시·요약 오류 |

`compare_systems`로 재현성 사본(ReproA 대 ReproScale)을 대조하는 시험은 하지 못했다. 사본이 이미 지워져 있었다. 사용자 매개변수 값을 0으로 읽으므로, 값 하나만 다른 두 시스템은 구별하지 못할 것으로 본다[추정].

## 5. 역할 분담

| 일 | 정본 도구 | NarshaMCP의 자리 |
|---|---|---|
| 나이아가라 생성·스택 편집 | 공식 NiagaraToolset + 에디터 Python 저장(`run_in_editor`) + 생성 스크립트 | 동적 입력 중첩·이벤트 배선 편의(정답 대조 뒤 채택) |
| 생성 결과 정답 판정 | 공식 툴셋 정규화 덤프 sha256 | 쓰지 않음(L-editor-56) |
| 에디터 없이 하는 1차 점검 | 없음 | `validate`(GPU에 CPU 전용 DI, 재질 사용 플래그, 렌더러 재질 누락)·`find_asset_consumers` |
| 시각 확인 | PIE 촬영 + `measure_fx_look.py` | `get_preview`(편집기 뷰포트, 미실측) |
| 수치 시험 | 오프스크린 Sim Cache C++ 시험 | `capture_simcache`(PIE, 미실측) |
| 성능 | PIE 측정 도구, `fx.Niagara.Debug.Hud` | `measured_cost`(`.utrace` CPU 타이머 결합) |
| C++ 심볼·호출 | Serena | `ue_analyze_symbols`(PDB 호출 그래프, C++↔블루프린트 교차 참조) |
| 프로젝트·엔진 소스 검색 | `rg` | `ue_grep`(플러그인·엔진 범위, 짧은 출력) |

## 6. 위험과 프로젝트 규칙 충돌

| 규칙 | NarshaADK 항목 | 영향 | 권장 |
|---|---|---|---|
| AGENTS 7절 블루프린트 로직 금지 | dev `blueprint-generator`·`bp-to-cpp`, `ue_manage_blueprint add_node`, 자동 연결 트리거 "BP 만들어"·"BP 노드 추가해줘" | 그래프 로직 생성 경로가 열림 | dev 끄기 |
| 7·13절 UAF만, AnimBP·몽타주·StateTree·BT 금지(D44) | content `animation-workflow`, 몽타주·BehaviorTree 저작 C++, `ue_manage_ai` | "몽타주"·"상태 머신" 요청이 금지 에셋 저작으로 연결됨 | content 끄기 |
| 11절 Serena + 공식 MCP | core SessionStart 훅("내장 Grep 대신 ue_grep", "C++ 클래스는 ue_generate_code") | 탐색 순서가 뒤집히고, 생성 코드가 5절(새 코드 주석 금지)과 어긋날 수 있음[추정] | core 끄기(훅 제거), MCP 서버만 유지 |
| CLAUDE.md 서브에이전트 라우팅 | core 에이전트 7개가 프로젝트 `.claude/agents/`의 바이트 동일 복사본(`core:td-builder` 등) | 같은 에이전트가 두 이름으로 보이고, 한쪽만 고치면 갈라짐 | core 끄기 |
| 15절 실행 도구를 `.claude/`·홈에 두지 않음 | 홈 설정 3곳 쓰기, 플러그인 캐시의 exe 사본, `.claude/agents/unreal-specialist.md`·`routing.json` | 설치기가 에디터 시작마다 다시 씀 | 끈 뒤 다음 에디터 시작에서 되살아나는지 확인[추정 위험] |
| 15절 HTML 주석 금지 | 설치 설정의 `<!-- NarshaMCP:START -->` 마커 | 지금은 주입이 폐기돼 충돌 없음 | `AGENTS.md`·`CLAUDE.md` diff 감시 |
| 8절 Git | `Plugins/Marketplace/`(바이너리 포함), 설치기가 다시 쓴 `.mcp.json`·`.cursor/mcp.json`(키 순서 변경, 끝 줄바꿈 제거) | 대용량 실수 커밋, 반복 diff | 플러그인 폴더는 `.gitignore`(기기별 Fab 설치), `.mcp.json`은 설치기 형식 그대로 한 번 커밋해 diff 고정 |
| 보안 | `.claude/settings.local.json`의 `enableAllProjectMcpServers: true`, 인증 없는 로컬 WebSocket과 `execute_python` | 프로젝트 MCP 서버 전부 자동 승인. 같은 PC의 다른 프로세스가 에디터를 조작할 수 있음(공식 포트 8000과 같은 등급) | 자동 승인 끄고 서버 이름 목록만 허용 |

`NarshaMCP.uplugin`의 `"Enabled": false` 목록(Niagara·PCG·PythonScriptPlugin 등)은 프로젝트에서 켠 플러그인을 끄지 않는다. 엔진 PluginManager가 비활성 참조를 건너뛸 뿐이다(PluginManager.cpp:2425-2429·2597).

## 7. 권장 활용 방안

**설정(사용자 승인 뒤 적용, D-61 proposed)**
1. `.claude/settings.local.json`: 플러그인 7개 끄기, `enableAllProjectMcpServers`를 false로 바꾸고 `enabledMcpjsonServers`에 쓸 서버만 적기. `narshamcp` MCP 서버는 켜 둔다(도구는 지연 로드라 평소 토큰 부담이 작다).
2. `.mcp.json` `narshamcp.env`에 `NARSHA_TELEMETRY_DISABLED=1` 추가.
3. 다음 에디터 시작 뒤 `git status`·`settings.local.json`을 다시 본다. 설치기가 끈 설정을 되살리면 Fab 플러그인 설정이나 제작사 문서에서 자동 설치를 끄는 방법을 찾는다.
4. `Plugins/Marketplace/`는 `.gitignore`에 넣는다. `.claude/agents/unreal-specialist.md`·`.claude/skills/routing.json`·`.antigravity/`는 커밋하지 않고, 삭제는 사용자 승인 뒤에 한다.

**나이아가라 제작(D-57 유지, NarshaMCP는 보조)**
1. 정본 경로: 명세 → 공식 NiagaraToolset 생성 스크립트 → 에디터 Python 저장 → 정규화 덤프 sha256 → 오프스크린 Sim Cache 시험 → PIE 촬영·`measure_fx_look.py`.
2. 반복 작업은 결정론 도구로 만든다. 지금의 불티 전용 생성기(984줄)에서 공통부(MCP 클라이언트, 스택 참조, 입력 데이터 형식, 저장, 덤프)를 떼어 "JSON 명세 → 나이아가라 시스템" 범용 도구로 만들면, 새 효과마다 에이전트가 할 일은 명세 작성뿐이다.
3. NarshaMCP 자리:
   - 에디터 없이 하는 1차 점검: `validate`(GPU 이미터의 CPU 전용 DI, 재질 사용 플래그).
   - 공유 재질·메시를 고치기 전 영향 조사: `find_asset_consumers`.
   - 편의 작업(공식 덤프 대조 뒤 채택): 이벤트 생성기·처리기 배선, 동적 입력 중첩.
   - 기타: `get_preview` 시각 확인, `.utrace` 기반 `measured_cost`.
4. 4단계(새 모듈 스크립트·Custom HLSL 노드)가 필요해지면 D-57의 `TDGameEditor` C++ 래퍼를 만든다. NarshaMCP로는 이 단계를 대체할 수 없다.

**코드 탐색**
- `ue_analyze_symbols`(find_callers·find_cpp_to_bp·blueprint_dependencies)는 Serena가 못 보는 C++↔블루프린트 교차 참조에 쓴다.
- C++ 심볼 탐색은 계속 Serena를 먼저 쓴다(11절).

## 8. 미확인·다음 시험

현재 설정 상태(2026-10-23 11:40 확인):
- 에디터 없이 되는 것은 동작함: 심볼 654만 개 색인, 에셋 11,572개·설정 5,895개 색인, 호출자 찾기·소스 검색·나이아가라 읽기 실측.
- 엔진 소스 텍스트 검색은 기본 꺼짐(`ue_grep` index_status engine `project_only`).
  - 켜는 방법(`set_engine_config` engine_source_scan 또는 engine_source_mode core·extended·full)은 모두 같은 비용이 듦: 최대 약 46GB RSS, 가용 메모리 약 40GB 감소, CPU 평균 274%로 약 5분(도구 문서 표).
  - 이 PC는 RAM 64GB라 다른 세션이 빌드·시험 중일 때는 켜지 않음.
  - 엔진 심볼 검색(PDB core+runtime)은 켜지 않아도 동작함(`AActor` 메서드 조회 확인). 엔진 소스 텍스트는 `rg`로 충분함.
  - 13:31 실제로 켬(다른 세션 보류 만료·프로세스 부재 확인 뒤).
    - 결과: 엔진 호출 그래프 390초(엔진 파일 32,153·함수 606,515). 여유 메모리 최저 42.5GB로, 실측 감소는 약 3GB였음.
    - 한계: `persisted: false`라 데몬이 다시 뜨면 꺼짐. `ue_grep` 엔진 텍스트 검색은 켜지지 않음(L-repo-23, Fab 설치 경로와 설정 파일 탐색 경로 불일치).
  - 14:08 `NARSHAMCP_CONFIG_PATH`를 narshamcp 설정 4곳(`.mcp.json`·`.cursor/mcp.json`·`~/.codex/config.toml`·`~/.gemini/antigravity/mcp_config.json`)에 추가함. 값은 `C:/Project/TDGame/Plugins/Marketplace/NarshaMCP/MCP/config/config.ini`.
    - 변경 전 설정 파일 사본: `Saved/AgentOps/20261023/narsha-adk/config.ini.before`(sha256 앞 16자 b7e80e8468a11a36).
    - 지금 떠 있는 데몬과 이 세션의 연결 프로세스는 이 환경 변수 없이 떴으므로, 확인은 새 세션에서 함.

다음 세션 확인 절차(D-61):
1. `Plugins/Marketplace/NarshaMCP/Source/ThirdParty/bin/narshamcp.exe status`로 데몬을 확인하고 `... stop`으로 정상 종료한다. 다른 클라이언트의 연결도 끊겼다가 다시 붙는다. 다른 세션의 빌드·시험 중에는 하지 않는다.
2. `ue_glob` 한 번으로 새 데몬을 띄운다. 이 데몬은 새 세션 연결 프로세스의 환경 변수를 물려받는다. 뜨는 시간과 데몬 메모리, 여유 메모리 최저치를 잰다.
3. `ue_cache_control set_engine_config {engine_source_scan: true, confirm: true}`를 호출해 `persisted: true`인지 본다. 사본과 diff해 무엇이 저장됐는지 확인한다.
4. `ue_grep query=FindScriptGroup domain=source scope=engine`이 엔진 결과(`NiagaraExternalSystemEditorUtilities.cpp`)를 내는지 본다.
5. 다시 `stop` → `ue_glob`으로 재시작 비용을 잰다. 저장된 설정으로 스캔이 다시 도는지, 캐시 `Intermediate/NarshaMCP/engine_call_graph_*.bincode.zst`만 읽는지 본다.
6. 에디터를 다음에 켠 뒤 설치기 setup이 `config.ini`·환경 변수·플러그인 끄기를 되돌리는지 `git status`와 diff로 본다.

확인 결과(2026-10-23 20:15~20:33, 같은 세션):
- 1~2단계는 경합으로 실패함. 데몬을 멈추자 다른 연결 프로세스가 1초 안에 환경 변수 없는 새 데몬을 띄웠음. 그래서 3~5단계는 환경 변수를 준 독립 `--stdio` 서버로 확인함.
- 저장: 됨. 결과는 `persisted: true`, `config.ini`에 `[EngineIndexing] engine_source_scan=true`. 새 프로세스가 이 값을 읽음.
- 재시작 비용: 초기화 3.2초. 이어서 엔진 호출 그래프를 약 4.5분 동안 다시 만들고(새 캐시 파일), 메모리는 13.5GB까지 오름. 여유 메모리 최저는 21.1GB(공유 데몬 9.6GB와 같이 떠 있던 상태).
- `ue_grep` 엔진 텍스트 검색: 15분 동안 계속 `engine_not_indexed`. 저장해도 켜지지 않음.
- 6단계는 실제로 일어남. 18:18 다른 세션이 에디터를 켜자 설치기가 env·timeout·자동 승인 끄기를 되돌림. 이후 다시 적용함(L-repo-21).
- 판단: 엔진 스캔 저장은 이득(엔진 텍스트 검색)이 없고 재시작마다 비용만 생김. 끄는 것을 권함. 설치기 되돌림을 끝내려면 `TDGame.uproject`에서 NarshaMCP 에디터 플러그인을 끔. 오프라인 MCP(`narshamcp.exe`)는 에디터 플러그인 없이도 동작함.
- 적용(2026-10-23 23:05, 사용자 승인):
  - 저장된 엔진 스캔 끔: `config.ini` [EngineIndexing] `engine_source_scan=false`. 공유 데몬의 세션 값도 끔.
  - 에디터 플러그인 끔: `TDGame.uproject`에 `NarshaMCP` Enabled false.
  - 오프라인 MCP는 정상 동작함. 다음 에디터 시작 때 설치기가 다시 돌지 않는지 확인함(`.mcp.json`·`.claude/settings.local.json` 수정 시각).
- 2026-10-23 12:17 D-61 적용 완료.
  - `.claude/settings.local.json`: 플러그인 7개 false, `enableAllProjectMcpServers` false, 서버 5개 명시.
  - `NARSHA_TELEMETRY_DISABLED=1` 추가: `.mcp.json`·`.cursor/mcp.json`·`~/.codex/config.toml`·`~/.gemini/antigravity/mcp_config.json`.
  - `.mcp.json` narshamcp에 `timeout` 180000 추가(L-repo-22).
  - 플러그인 끄기는 새 세션부터 적용됨. 설치기가 에디터 시작 때 되돌리는지는 미확인.
- `ue_check_health`는 두 번 모두 10초 디스패처 제한에 걸려 실패함.
- 에디터 모듈은 설치 뒤 아직 한 번도 로드되지 않음. `Saved/Logs/TDGame.log`에는 02:00 UTC Fab 재배치 기록만 있음. 에디터 저작 작업은 재시작 뒤에야 쓸 수 있고, 아직 미실측.
- NarshaMCP의 Python 실행은 컴파일 단계에서 빠짐. `WITH_NARSHAMCP_PYTHONSCRIPTPLUGIN 0`이면 `ExecutePythonCode`가 "PythonScriptPlugin is not enabled" 오류를 돌려줌(CUWebSocketEditorSubsystem.cpp:2585-2589). 원인은 `TDGame.uproject`가 PythonScriptPlugin을 직접 적지 않은 것임. 프로젝트 자체 Python 원격 실행은 영향 없음. 살리려면 `.uproject`에 명시하고 플러그인을 다시 빌드해야 함.

- NarshaMCP 에디터 저작 작업은 실측하지 못했다. 2026-10-23 11시대에 다른 세션이 빌드 창(`build_hold.json` fx4b-wt)을 잡고 컴파일 중이었다. 시험안(에디터 약 20분): `create_system` → `add_system_emitter` → `attach_dynamic_input` → `add_user_parameter` → `set_sim_target`·`set_fixed_bounds` → 저장 → 공식 툴셋 덤프로 정답 대조 → `get_preview`. 시험 대상은 `/Game/AgentScratch/` 아래에만 둔다.
- 엔진 판올림이나 NarshaMCP 판올림 뒤에는 4절 표를 같은 에셋으로 다시 대조한다.

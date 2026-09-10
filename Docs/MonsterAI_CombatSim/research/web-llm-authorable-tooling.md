# 웹: 생성형 AI 에이전트가 게임 AI 를 코드/텍스트로 제작·분석하기 좋은 도구 형식과 언리얼 스크립팅 선택지

- 작성일: 2026-09-09
- 조사 방법: WebSearch 로 후보를 찾고 WebFetch 로 본문을 직접 열어 확인했다. 엔진 사실은 로컬 UE 5.8 소스(`C:/Program Files/Epic Games/UE_5.8/Engine`)를 grep/sed 로 열어 파일:줄 번호를 적었다. 열지 못한 출처(HTTP 403, PDF 파싱 실패)는 "미확인"으로 남겼다.
- 용어: LLM(Large Language Model, 대규모 언어 모델), MCP(Model Context Protocol, 모델 컨텍스트 프로토콜), BT(Behavior Tree, 비헤이비어 트리), ST(StateTree, 언리얼 스테이트 트리), DSL(Domain Specific Language, 도메인 특화 언어), IAUS(Infinite Axis Utility System, 무한 축 유틸리티 시스템), PIE(Play In Editor, 에디터 내 플레이), JSONL(JSON Lines, 한 줄에 JSON 객체 하나씩 쌓는 형식), LRPL(Low-Resource Programming Language, 학습 데이터가 적은 프로그래밍 언어), ACI(Agent-Computer Interface, 에이전트-컴퓨터 인터페이스).

---

## 결론 요약 (설계 결정에 바로 쓸 수 있는 문장들, 근거 표기)

### 권장안

| 순위 | 몬스터 AI 정의 형식 | 한 줄 이유 |
|---|---|---|
| **1순위** | **C++ 빌더(플루언트) API 로 "행동 카탈로그·전이 규칙" 을 코드로 등록 + 밸런스 파라미터(고려사항 표·응답 곡선 4파라미터·가중치·쿨다운)는 JSON/CSV 표(→ UDataTable 또는 자체 로더)** | (1) 언리얼 5.8 의 공식 MCP 툴셋은 StateTree/BT 를 "검사(inspection)" 만 할 수 있고 생성·수정 도구가 없다(엔진 소스 확인). (2) LLM 은 범용 코드보다 "제한된 JSON DSL" 을 낼 때 문법 유효율이 98~100% 로 높았다(arXiv 2510.16952, 2025). (3) 유틸리티 AI 는 업계가 이미 "행·열 표 + 4파라미터 곡선" 으로 정의한다(IAUS, 2025 해설). (4) C++ 는 이 프로젝트의 유일한 로직 언어이고, 컴파일러·자동화 테스트가 곧 LLM 의 검증 루프가 된다(Anthropic Claude Code 모범 사례, 2026). |
| **2순위** | **제한된 JSON DSL(트리/규칙 정의) + 런타임 로더(자체 파서)** — 1순위의 C++ 빌더가 종류 폭발로 무거워질 때 행동 구조까지 데이터로 내리는 확장안 | BehaviorTree.CPP 가 XML DSL 을 런타임 로드하는 방식(v4.9, 2026)과 동일. 파서·스키마 검증기를 한 번만 만들면 LLM 이 에디터 없이 트리를 쓴다. 단, 스키마 검증기·핫리로드·디버거를 직접 만들어야 하는 비용이 있다. |
| 보조 | 언리얼 StateTree 에셋 | 시각 디버깅·디자이너 편집이 꼭 필요한 소수 보스에만. Python 으로 노드(FInstancedStruct) 를 채우는 경로는 미확인이고, 에디터 없이는 컴파일(`UStateTreeCompilerManager::CompileSynchronously`)이 불가능하다. |
| 피함 | Angelscript(엔진 포크 필요), UnrealSharp(.NET 런타임 추가·결정론 미문서화), Lua(LLM 정확도 낮음), Verse(UEFN 전용), T3D(재수입 불가) | 아래 상세 근거 |

### 설계 결정 문장

1. **UE 5.8 에는 공식 MCP 플러그인이 있고 이 프로젝트에 이미 켜져 있다(`ModelContextProtocol`, `AllToolsets`).** 서버는 에디터 프로세스 안에서 `http://127.0.0.1:8000/mcp` 로 뜨고 HTTP 전송만 지원하며 인증이 없다. 클라이언트 이름은 `ClaudeCode, Cursor, VSCode, Gemini, Codex, All`. (Epic 문서 "Unreal MCP in Unreal Editor", 2026; 로컬 `TDGame.uproject` 23~31행)
2. **그러나 공식 StateTree/BT 툴셋은 읽기 전용이다.** `StateTreeToolset.uplugin` 의 설명이 "Toolset for StateTree Inspection" 이고 실험(Experimental)·기본 비활성이며, `state_tree.py`(165줄)의 도구는 `get_editor_data, get_root_states, get_children, get_tasks, get_enter_conditions, get_transitions, get_global_tasks, get_evaluators, get_node_description` 뿐이다. `behavior_tree.py`(150줄)도 `get_*/list_nodes` 만 있다. → **에이전트가 MCP 로 StateTree 를 "만드는" 공식 경로는 5.8 에 없다.**
3. **Python 편집기 스크립트로 StateTree 를 만드는 것도 반쯤만 열려 있다.** `UStateTreeEditorData::GetEditorData(UStateTree*)` 는 BlueprintCallable static 이고 `SubTrees` 는 `Instanced, BlueprintReadOnly` 라 읽기는 되지만, 상태의 `Tasks/EnterConditions/Considerations` 는 `TArray<FStateTreeEditorNode>`(안에 FInstancedStruct) 이고 C++ 편의 함수 `AddTask<T>()/AddEnterCondition<T>()` 는 템플릿이라 Python/Blueprint 에 노출되지 않는다. Python 이 FInstancedStruct 노드를 채울 수 있는지는 **미확인**. Python 자체도 에디터 전용(패키지 빌드·PIE 불가)이다(Epic 문서, 5.8).
4. **커뮤니티 MCP 서버는 BT 생성까지는 하지만 StateTree 는 거의 없다.** DeVoe09/UnrealMCP 는 `create_behavior_tree, add_bt_composite_node, add_bt_decorator_node, add_bt_service_node`(UE 5.7+, Win64, TCP JSON-RPC)를 제공하나 StateTree 는 없다. ChiR24/Unreal_mcp 만 "State Tree operations" 를 표방(내용 미검증). 모두 실험 단계이고 에디터 프로세스가 떠 있어야 한다(본 세션에서도 `unreal-mcp` 연결이 거부됨).
5. **Epic 의 5.7 AI Assistant 는 "설명은 하지만 만들지는 못하는" 대화형 도구다.** "The assistant can tell you how to set up a material instance, but it cannot create one."(StraySpark, 2026-03-24) → AI 정의 형식 결정에 영향 없음.
6. **LLM 은 학습 데이터가 적은 언어(Lua 등)에서 정확도가 낮다.** 7B 양자화 모델은 Lua 에서 50% 미만(arXiv 2410.14766, 2024-10). LRPL/DSL 111편 조사(arXiv 2410.03981, TOSEM 2025)는 데이터 희소·벤치마크 부재를 핵심 장애로 든다. 반면 JSON DSL 을 문서와 함께 프롬프트에 넣으면 문법 유효율 98~100%(arXiv 2510.16952, 2025-10). → **C++(고자원 언어) + JSON 표** 조합이 LLM 정확도 면에서 가장 안전하다.
7. **유틸리티 AI 의 표준 데이터 형식은 "입력 1개 + 곡선 종류 + 파라미터 4개(m, k, b, c) + 가중치" 이다.** 곡선은 Linear/Quadratic(Polynomial)/Logistic/Logit/Gaussian(Normal)(+Sine, Constant, Binary), 점수 결합은 곱 또는 기하평균, 행=행동, 열=축 스프레드시트로 프로토타입 가능(tonogameconsultants 2025; Curvature 위키). 언리얼 StateTree 도 `FStateTreeConsiderationResponseCurve{FRuntimeFloatCurve CurveInfo}` + `UStateTreeState::Weight` 로 같은 모델을 내장한다(엔진 소스). → TDGame 표 스키마는 이 5개 열을 그대로 쓴다.
8. **언리얼 Visual Logger 파일(.bvlog)은 바이너리이고 텍스트/JSON 내보내기가 공식 문서에 없다.** `#define VISLOG_FILENAME_EXT TEXT("bvlog")`, `FVisualLoggerHelpers::Serialize(*FileArchive, FrameCache)` 로 직렬화, 콘솔 `VISLOG record/stop`. Gameplay Debugger 도 화면 오버레이 전용. → **자체 JSONL 결정 로그**(틱, 몬스터 ID, 후보 점수, 선택, 상태 해시)를 시뮬레이터에서 직접 쓰는 것이 LLM 분석의 정답이다. 결정론 검증은 "틱별 상태 해시 + 구조화 텍스트 덤프 diff"(Bugnet, 2026-04).
9. **에이전트 토큰 효율의 원칙은 "가장 작은 고신호 토큰 집합", "필요할 때 꺼내 읽기(파일 시스템을 컨텍스트로)", "검증 수단 제공", "도구 응답 절제(기본 25k 토큰 상한)" 이다**(Anthropic 2025-09-29, 2025-09-11; Claude Code 모범 사례 2026; AGENTS.md 는 6만+ 프로젝트 채택). → 몬스터별 작은 파일, 스키마 문서 1장, 검증 스크립트 1개, 헤드리스 테스트 명령을 `AGENTS.md`/`CLAUDE.md` 에 명시.

---

## 상세 조사

### 1) 언리얼 StateTree·BT 에셋을 텍스트로 생성·내보내는 방법과 한계, MCP·AI Assistant 사례

#### 1-1. UE 5.8 공식 MCP 플러그인 (엔진 내장)

| 사실 | 근거 |
|---|---|
| 5.8 에 실험적 MCP 플러그인 신설: "we bring a new experimental MCP (Model Context Protocol) plugin for the Unreal Editor. This plugin enables agentic AI systems to connect to the Unreal Engine editor." | https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes (2026) |
| 서버는 에디터 프로세스 내장, 기본 `http://127.0.0.1:8000/mcp`, HTTP 전송만. 기본 노출 툴셋: SceneTools, ActorTools, MaterialInstanceTools, ObjectTools, GASToolsets(실험·기본 꺼짐). 확장: Python `unreal.ToolsetDefinition` + `@toolset_registry.tool_call`, C++ `UToolsetDefinition` + `UFUNCTION(meta=(AICallable))`. 지원 클라이언트 `ClaudeCode, Cursor, VSCode, Gemini, Codex, All`. "only accepts connections from the same machine, has no authentication layer" | https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor (2026) |
| 플러그인 위치 `Engine/Plugins/Experimental/ModelContextProtocol`, uplugin 설명 "Anthropic MCP (Model Context Protocol) server implementation for Unreal Engine." | 로컬 엔진 `ModelContextProtocol.uplugin:6` |
| 툴셋 27종: AIModuleToolset, StateTreeToolset, AutomationTestToolset, GASToolsets, EditorToolset, LiveCodingToolset, SemanticSearchToolset, PCGToolset, DataRegistryToolset 등 | 로컬 `Engine/Plugins/Experimental/Toolsets/` 디렉터리 목록 |
| StateTreeToolset: "Toolset for StateTree Inspection", `IsExperimentalVersion: true`, `EnabledByDefault: false` | `StateTreeToolset.uplugin:6,16,19` |
| StateTree 도구 목록(모두 읽기): get_editor_data, get_root_states, get_children, get_tasks, get_enter_conditions, get_transitions, get_global_tasks, get_evaluators, get_node_description | `StateTreeToolset/Content/Python/state_tree_toolset/toolsets/state_tree.py:12~140` |
| BT 도구 목록(모두 읽기): get_blackboard, get_root_decorators, list_nodes, get_node_depth, get_node_depths, get_children, get_subtree. `set_/add_/create_` 문자열 없음 | `AIModuleToolset/Content/Python/aimodule_toolset/toolsets/behavior_tree.py:47~136` |
| 이 프로젝트는 `ModelContextProtocol`, `AllToolsets` 를 이미 켜 둠 | `C:/Project/TDGame/TDGame.uproject:23~31` |
| 2차 보도: "localhost-only by default, there is no authentication layer, and Epic is explicit that this is experimental" | https://byteiota.com/unreal-engine-5-8-ships-mcp-server-ai-agents-can-now-drive-the-editor/ (2026-06-18) |

발췌(StateTree 툴셋, 읽기 전용임을 보여 주는 부분):
```python
class StateTreeTools(unreal.ToolsetDefinition):
    """Inspect StateTree (ST) assets."""
    @toolset_registry.tool_call
    @staticmethod
    def get_root_states(state_tree: unreal.StateTree) -> list[unreal.StateTreeState]:
        ed = StateTreeTools._get_ed(state_tree)
        return list(ed.sub_trees)
```

#### 1-2. StateTree 를 Python/코드로 만들 수 있는가 (엔진 API 사실)

| 사실 | 근거 |
|---|---|
| `UStateTree::EditorData` 는 `UPROPERTY()`(편집 플래그 없음, `WITH_EDITORONLY_DATA`) → Python `get_editor_property` 대상 아님 | `Plugins/Runtime/StateTree/Source/StateTreeModule/Public/StateTree.h:381~384` |
| `UStateTree::DebugInternalLayoutAsString()` — 컴파일된 트리 내부를 문자열로 덤프(`WITH_EDITOR || WITH_STATETREE_DEBUG`) | `StateTree.h:377` |
| `UStateTreeEditorData` 는 `UCLASS(MinimalAPI, BlueprintType, EditInlineNew, ...)`, `static UStateTreeEditorData* GetEditorData(UStateTree*)` 가 `UFUNCTION(BlueprintCallable)` | `StateTreeEditorModule/Public/StateTreeEditorData.h:64, 73~74` |
| `SubTrees` 는 `UPROPERTY(Instanced, BlueprintReadOnly)` `TArray<TObjectPtr<UStateTreeState>>` | `StateTreeEditorData.h:436~437` |
| `UStateTreeState` 의 `EnterConditions/Tasks/Considerations` 는 `EditDefaultsOnly` `TArray<FStateTreeEditorNode>`, `Transitions` 는 `TArray<FStateTreeTransition>`, `Children` 은 `Instanced, BlueprintReadOnly`, `Weight`(Utility, ClampMin 0) | `StateTreeEditorModule/Public/StateTreeState.h:475~496` |
| C++ 빌더 함수: `AddChildState(FName, EStateTreeStateType)`, `template AddEnterCondition<T>(...)`, `template AddTask<T>(...)`, `AddTransition(Trigger, Type, State)`, `AddTransition(Trigger, FGameplayTag, Type, State)` — 템플릿은 리플렉션 비노출 | `StateTreeState.h:282, 289, 315, 337, 342` |
| 컴파일 진입점: `UStateTreeCompilerManager::CompileSynchronously(TNotNull<UStateTree*>)`, `UStateTreeEditingSubsystem::CompileStateTree(...)`, `FStateTreeCompiler::Compile(UStateTree&)` — 모두 에디터 모듈 | `StateTreeCompilerManager.h:43~44`, `StateTreeEditingSubsystem.h:37`, `StateTreeCompiler.h:64~67` |
| 5.8 릴리스 노트: "new Compiler Manager helps streamline iteration, and the editor now tracks when trees are out of date" | 5.8 릴리스 노트 URL 위와 동일 |
| Python 은 `EditAnywhere/VisibleAnywhere` 속성을 `set_editor_property()` 로, `BlueprintReadWrite` 는 직접 속성으로 노출. Python 은 "에디터 전용, 패키지 게임·PIE·스탠드얼론에서 불가". 헤드리스: `-run=pythonscript -script=<file>` | https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python (5.8) |
| `FStateTreeEditorNode`(FInstancedStruct 포함)를 Python 으로 채우는 사례 | **미확인**(검색·문서에서 발견 못 함) |
| T3D/.COPY 텍스트: 포럼 다수가 "can't import COPY or T3D files back to the engine" | https://forums.unrealengine.com/t/question-uasset-copy-and-t3d/277319 (연도 미표기, 구 포럼) — StateTree 적용 여부 **미확인** |

해석: C++ 에서는 `UStateTreeState::AddTask<T>()` 로 프로그램적 생성이 가능하나 이는 **에디터 모듈** 코드이고 컴파일도 에디터에서만 된다. 즉 "에디터 없는 헤드리스 시뮬레이터가 텍스트로부터 StateTree 를 만들어 쓰는" 경로는 5.8 에 없다. 반대로 `DebugInternalLayoutAsString()` 과 MCP 검사 도구 덕분에 **기존 StateTree 를 텍스트로 읽어 분석** 하는 것은 가능하다.

#### 1-3. 커뮤니티 MCP 서버 비교 (2025~2026)

| 서버 | AI 관련 도구 | 버전·전송 | 상태 | 근거 |
|---|---|---|---|---|
| chongdashu/unreal-mcp | 액터·블루프린트·노드 그래프·에디터 뷰포트. BT/ST 없음 | UE 5.5+, C++ TCP(55557) + Python FastMCP | "EXPERIMENTAL", MIT | https://github.com/chongdashu/unreal-mcp |
| DeVoe09/UnrealMCP | `create_behavior_tree`(BT+Blackboard), `add_blackboard_key`, `add_bt_composite_node`, `add_bt_decorator_node`, `add_bt_service_node`, `run_behavior_tree`, `configure_ai_perception`. StateTree 없음 | UE 5.7+, Win64, TCP JSON-RPC 2.0 newline-delimited | 에디터 전용 | https://github.com/DeVoe09/UnrealMCP |
| remiphilippe/mcp-unreal | 헤드리스 `build_project/cook_project/run_tests/list_tests/get_test_log`, `blueprint_query/modify`, GAS·DataTable 도구. BT/ST 없음 | UE 5.7 전용, Go 단일 바이너리, Remote Control API(30010)+자체 플러그인(8090) | — | https://github.com/remiphilippe/mcp-unreal |
| ChiR24/Unreal_mcp | `manage_ai`(AI 컨트롤러, BT 그래프 조작, EQS, 퍼셉션, "State Tree operations"), `execute_python` | UE 5.0~5.8, HTTP/SSE 또는 WebSocket, capability token 인증 기본 켜짐 | 23개 게이트웨이 도구 | https://github.com/ChiR24/Unreal_mcp |

운영 주의: 이 세션에서도 프로젝트에 설정된 `unreal-mcp` 서버가 연결 거부(ConnectionRefused) 상태였다. 에디터 프로세스가 떠 있어야 하는 MCP 경로는 **자동화 파이프라인의 단일 실패점** 이 된다.

#### 1-4. Epic AI Assistant (5.6 웹 → 5.7 에디터 내장)

| 사실 | 근거 |
|---|---|
| 2025-09 UE 5.6 문서 Q&A 와 C++ 코드 생성(웹), "Unreal Engine 5.7 will bring the Assistant directly into the Unreal Editor itself". 사용자 우려: "an llm told them to do something that did not exist" | https://forums.unrealengine.com/t/the-epic-developer-assistant-ai-powered-developer-assistant-for-unreal-engine-5-6/2659525 (2025) |
| 5.7 어시스턴트는 "fundamentally read-only for editor operations", "No editor automation." MCP 는 "does not just talk about what you could do—it does it." | https://www.strayspark.studio/blog/ue57-ai-assistant-vs-mcp-comparison (2026-03-24) |

### 2) 코드/텍스트 기반 AI 정의 형식의 장단점

#### 2-1. 스크립팅 언어 선택지 (언리얼)

| 선택지 | 핫리로드 | 성능 | 결정론 | LLM 작성 정확도 | 도입 비용·위험 | 근거 |
|---|---|---|---|---|---|---|
| **C++ (현 프로젝트 기본)** | Live Coding(엔진 기본, `LiveCodingToolset` MCP 툴셋 존재) | 최고 | 프로젝트가 직접 제어(고정 스텝, 시드 RNG) | 고자원 언어(Python/JS/Java 급 50~75% pass@1 대비 LRPL ≤30%; 검색 요약, 아래 조사 참조) | 없음 | 로컬 `Engine/Plugins/Experimental/Toolsets/LiveCodingToolset` |
| Hazelight Angelscript | 저장 즉시 반영, "non-structural changes ... reloaded without having to exit the play session" | "significantly better than blueprint ... approaches native C++ performance when using transpiled scripts in a shipping build" | 문서 없음(**미확인**) | Angelscript 는 LRPL(연구 없음, **미확인**) | "a set of engine modifications and a plugin" → 엔진 포크 필요. "Hazelight does not guarantee any maintenance or support". Split Fiction 170만 줄/1.6만 파일 실적 | https://angelscript.hazelight.se/ , https://angelscript.hazelight.se/project/development-status/ (2025~2026) |
| UnrealSharp (C#) | "Recompile and reload C# code without restarting the editor" | 미측정(**미확인**) | .NET 런타임·GC 개입, 결정론 문서 없음(**미확인**) | C# 은 고자원 언어 | UE 5.6~5.8, .NET 10.0.5+, Windows/macOS, MIT, "C++ projects strongly recommended" | https://github.com/UnrealSharp/UnrealSharp (2026) |
| UnLua (Lua) | 문서 발췌에서 확인 못 함(**미확인**) | UFUNCTION 호출 캐시·컨테이너 직접 접근 최적화 | Lua VM 자체는 결정론적이나 문서 없음 | **낮음**: 7B 양자화 모델 Lua 50% 미만 | UE 4.17~5.x, MIT | https://github.com/Tencent/UnLua ; https://arxiv.org/abs/2410.14766 (2024-10-18) |
| Verse | — | — | — | — | "the only way to compile and run Verse programs is within the UEFN"; UE6 얼리 액세스 2027년 말(2차 출처) | https://ludusengine.com/blog/verse-unreal-engine-what-you-need-to-know , https://tech-insider.org/unreal-engine-6-state-of-unreal-2026/ (2026; 1차 출처 **미확인**) |
| Python (에디터) | 해당 없음 | 해당 없음 | 해당 없음 | 최고 수준 | 런타임 사용 불가("not available in packaged games, PIE, standalone") | Epic Python 문서 (5.8) |

#### 2-2. LLM 작성 정확도 연구

| 사실 | 근거 |
|---|---|
| Lua 를 "저자원 언어" 로 선정("to avoid models' biases related to high-resource languages"), 7B 양자화 모델 전반 50% 미만, 4-bit 가 최적 절충 | https://arxiv.org/abs/2410.14766 (2024-10-18) |
| 27,000편 중 111편 체계적 조사(2020~2024): LRPL/DSL 은 "data scarcity", "specialized syntax that is poorly represented", 대부분 벤치마크 부재 | https://arxiv.org/abs/2410.03981 (TOSEM 2025 채택) |
| 게임 행동 생성에 JSON DSL 을 중간층으로 사용: "Instead of generating general-purpose code, which is unconstrained and error-prone, the LLM's role is strictly limited to generating structured data in a custom JSON-based DSL." Spell DSL 문법 유효율 98~100%, Automata DSL 76~100%. 모델 선택이 품질의 최강 예측 변수(p<.001), Claude 4 Sonnet 최고. DSL 문서를 프롬프트에 주입(fine-tuning 없음) | https://arxiv.org/html/2510.16952 (2025-10-19) |
| BT 를 LLM 에이전트 구조화 틀로 제안(Dendron): LLM 은 "frequently brittle ... require significant scaffolding" | https://arxiv.org/abs/2404.07439 (2024-04-11) |
| 프롬프트 엔지니어링이 LLM 합성 BT 에 미치는 영향(SBP-BRiMS 2025) | https://sbp-brims.org/2025/papers/working-papers/2025_SBP-BRiMS_paper_62.pdf — PDF 본문 **미확인** |

주의: Lua 연구는 7B 급 소형 모델 대상이다. 최신 대형 모델(Claude/GPT 급)의 Lua/Angelscript 정확도는 **미확인**. 다만 "학습 데이터가 적은 문법일수록 오류율이 높다" 는 일반 결론과 "제한된 JSON DSL 이 유효율을 끌어올린다" 는 결과는 서로 일관된다.

#### 2-3. 텍스트 DSL 의 산업 참조 형식

| 형식 | 특징 | 근거 |
|---|---|---|
| BehaviorTree.CPP XML | "Trees are defined using a Domain Specific scripting language (based on XML), and can be loaded at run-time." v4.9, MIT. Groot2 편집기가 XML 을 실시간 미리보기 | https://github.com/BehaviorTree/BehaviorTree.CPP (2026); https://www.behaviortree.dev/docs/learn-the-basics/xml_format/ (HTTP 403, 본문 **미확인**) |
| ReasonablePlanningAI(언리얼) | `RpaiComposerBehavior` DataAsset 에 Goals(거리 함수·유틸리티 가중치)·Actions(적용 조건·상태 변이) 정의, 유틸리티로 목표 선택 + A* 계획. UE 4.27/5.0+ | https://github.com/hollsteinm/ReasonablePlanningAI |
| 언리얼 StateTree C++ 빌더 | `AddChildState/AddTask<T>/AddEnterCondition<T>/AddTransition` (에디터 모듈 한정) | `StateTreeState.h:282~342` |

#### 2-4. 형식별 장단점 정리 (본 조사 종합)

| 형식 | 장점 | 단점 |
|---|---|---|
| C++ 빌더 API(플루언트) | 컴파일 타임 검증, IDE·Live Coding, 결정론·성능은 프로젝트 통제, LLM 정확도 최고(고자원 언어) | 행동 구조 변경마다 컴파일. 종류가 수백이 되면 파일 수 관리 필요(→ 몬스터당 1파일 규칙으로 완화) |
| JSON/CSV 규칙 표(UDataTable) | 밸런스 값의 diff 가 명확, 스프레드시트 호환, LLM 유효율 높음, 핫리로드(에셋 재수입) 가능 | 구조(분기·시퀀스)를 표로 표현하면 가독성 급락 → 구조는 코드, 수치는 표로 분리 |
| JSON DSL + 자체 파서 | 에디터 불필요, 런타임 로드, 스키마 검증기로 LLM 오류를 기계적으로 잡음 | 파서·검증기·디버거를 직접 유지. 타입 오류가 런타임으로 밀림 |
| 스크립트 언어(Angelscript/C#/Lua) | 핫리로드 | 엔진 포크 또는 외부 런타임, 결정론·성능 미문서화, Lua 는 LLM 정확도 낮음 |
| 에디터 에셋(StateTree/BT) | 시각 디버깅, 디자이너 친화 | LLM 이 만들 공식 경로 없음(5.8), 에디터 없이는 컴파일 불가, 검사만 가능 |

### 3) 유틸리티 AI 를 표(곡선·가중치)로 정의하는 사례

| 사실 | 근거 |
|---|---|
| IAUS 축(axis) = "one input + one curve type + four parameters". 입력은 [0,1] 정규화. 파라미터 m(기울기/폭), k(지수/수직 배율), b(수직 이동), c(수평 이동). 곡선 5종: Linear, Quadratic, Logistic, Logit, Gaussian. 결합은 기하평균(n제곱근)으로 "fairly balances axis no matter how many there are". 행동 가중치 1(보통)/2~3(중요)/5(긴급). 스프레드시트 "rows for actions, columns for axis, formulas for scores" 로 프로토타입 | https://tonogameconsultants.com/infinite-axis-utility-systems/ (2025-10-20) |
| Curvature(Dave Mark IAUS 편집기): 곡선 linear, polynomial, logistic, logit, normal, sine; 공통 파라미터 Slope, Exponent, X-shift, Y-shift; "within the unit square (0,0)-(1,1)". 구조: Knowledge-base → Inputs → Considerations → Behaviors → Behavior Sets → Archetypes | https://github.com/apoch/curvature/wiki/Response-Curve-Design ; https://github.com/apoch/curvature |
| 언리얼 StateTree 내장 유틸리티: `FStateTreeConsiderationResponseCurve::Evaluate(NormalizedInput)` 가 `FRuntimeFloatCurve CurveInfo` 를 평가(비어 있으면 입력 그대로 반환). `FStateTreeFloatInputConsiderationInstanceData{ float Input; FFloatInterval Interval(0,1) }`, Enum 입력 고려사항(`EnumValueScorePairs`), `UStateTreeState::Weight`("Weight used to scale the normalized final utility score") | `Plugins/Runtime/StateTree/Source/StateTreeModule/Public/Considerations/StateTreeCommonConsiderations.h:45~108, 148, 175`; `StateTreeState.h:474~476` |
| BTUtility(언리얼 BT 확장): 곡선 프리셋 "Constant, Binary, Linear, Polynomial, Logistic, Logit, Normal and Sine" + `RuntimeFloatCurve` 로 커스텀 곡선. 고려사항은 C++ `UBTUUtilityConsideration::GetInputValue()` 상속 또는 Blueprint | https://github.com/ric-fm/BTUtility |
| Athena AI(Fab): 검색 요약은 "curves stored in a Curve Table" 이라 하나 페이지 403 | https://www.fab.com/listings/bfaa16e9-e569-4a5b-9b61-deba6f395ea1 — **미확인** |
| Utility Intelligence(Unity IAUS) 고려사항 문서 | https://uintel-go.utilityworlds.com/... — HTTP 403 **미확인** |

엔진 발췌(StateTree 응답 곡선, 표 스키마의 참조 구현):
```cpp
// StateTreeCommonConsiderations.h:45~70
struct FStateTreeConsiderationResponseCurve {
    float Evaluate(float NormalizedInput) const { /* CurveInfo 비어 있으면 입력 반환 */ }
    UPROPERTY(EditAnywhere, Category = Default, DisplayName = "Curve")
    FRuntimeFloatCurve CurveInfo;
};
// StateTreeState.h:475~476
UPROPERTY(EditDefaultsOnly, Category = "Utility", meta=(ClampMin=0))
float Weight = 1.f;
```

TDGame 표 스키마 제안(위 사례의 교집합): `action_id | consideration_id | input(정규화 함수 이름) | curve(Linear/Poly/Logistic/Logit/Gauss/Custom) | m | k | b | c | cost_rank(싼 것 먼저) | weight(행동 단위)`. `FRuntimeFloatCurve` 대신 4파라미터 수식으로 두면 표 한 줄이 곧 곡선이며, 부동소수점 결정론도 수식이 단순해 유리하다.

### 4) AI 디버깅·분석을 텍스트로 뽑는 방법

| 사실 | 근거 |
|---|---|
| Visual Logger 파일 확장자 `bvlog`: `#define VISLOG_FILENAME_EXT TEXT("bvlog")` | `Source/Runtime/Engine/Classes/VisualLogger/VisualLoggerBinaryFileDevice.h:11` |
| 기록 시작 시 `FPaths::ProjectLogDir()` 에 임시 파일 생성, 종료 시 `FVisualLoggerHelpers::Serialize(*FileArchive, FrameCache)` 로 바이너리 직렬화. ini `[VisualLogger] UseCompression`, `FrameCacheLenght` | `Source/Runtime/Engine/Private/VisualLogger/VisualLoggerBinaryFileDevice.cpp:16~22, 41~45, 57` |
| 콘솔 `VISLOG record` / `VISLOG stop`: 에디터에서는 `SetIsRecording`, 비에디터에서는 `SetIsRecordingToFile` | `Source/Runtime/Engine/Private/VisualLogger/VisualLogger.cpp:1374~1399` |
| 대체 장치: Trace 채널 "Allows reviewing visual log data in Unreal Insights instead of a standalone .vlog file." | `VisualLoggerTraceDevice.cpp:12` |
| 공식 문서는 매크로(`UE_VLOG`, `UE_VLOG_UELOG`, `UE_VLOG_SEGMENT/BOX/CONE/...`)와 `IVisualLoggerDebugSnapshotInterface::GrabDebugSnapshot` 만 설명하고 텍스트/JSON 내보내기·파일 형식은 설명 없음 | https://dev.epicgames.com/documentation/unreal-engine/visual-logger-in-unreal-engine (5.8) |
| 커뮤니티 튜토리얼: `UE_VLOG_EVENT_WITH_DATA`, `UE_VLOG_HISTOGRAM` 사용례, "Save the recording to an external file, perfect for attaching to bug tickets" | https://unreal-garden.com/tutorials/visual-logger/ (2022) |
| Gameplay Debugger: 카테고리 Navmesh/Basic/Behavior Tree/EQS/Perception, 커스텀은 `FGameplayDebuggerCategory::CollectData(APlayerController*, AActor*)`/`DrawData(...)` 오버라이드. 문서에 텍스트 내보내기 언급 없음 | `Source/Runtime/GameplayDebugger/Public/GameplayDebuggerCategory.h:48~59`; https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-gameplay-debugger-in-unreal-engine |
| StateTree 컴파일 결과 텍스트 덤프 `DebugInternalLayoutAsString()`; `LastCompiledEditorDataHash` 는 "detect mismatching events from recorded traces" 용 | `StateTree.h:377, 397~398` |
| 결정론 디버깅 표준 절차: 틱마다 전체 상태 체크섬(FNV-1a/CRC32), 서브시스템별 해시로 최초 이탈 지점 특정, "structured text format with one line per variable" 덤프를 diff, 입력 해시와 상태 해시 분리. 비결정 원인: 부동소수점(`-fno-fast-math`/고정소수점), 미초기화 메모리, 해시맵 순회 순서, RNG 분리(게임플레이 vs 연출), 정렬 키, 멀티스레드 | https://bugnet.io/blog/how-to-debug-desync-in-deterministic-lockstep-games (2026-04-10) |
| 에이전트 실행 재현에 JSONL 추적(run id, step, 도구명, 인자, 출력 해시)을 append-only 로 남기고 재생 시 기록값을 그대로 대체, 기록에 없는 호출은 "fails loudly" | https://dev.to/apprs_6334/diff-every-tool-call-replaying-agent-runs-from-a-jsonl-trace-2b75 (검색 요약, 본문 **미확인**) |

해석: 언리얼 내장 도구는 "사람이 화면으로 보는" 용도이며 LLM 이 읽을 텍스트를 내지 않는다. 밸런스 시뮬레이터가 이미 `UWorld::CreateWorld` + 고정 스텝(`FTDScopedCombatWorld`) 이므로, 거기서 **한 틱당 한 줄 JSONL**(tick, monster_id, 후보 행동별 점수, 선택, 이유 코드, 상태 해시)을 쓰고, 별도 Python/C++ 스크립트가 요약(승패·시간·피해·결정 분포)을 만드는 구조가 가장 싸다. Visual Logger 는 사람 확인용 보조로만 두고, 자체 로그를 `UE_VLOG` 로 이중 송출하면 두 용도를 함께 만족한다.

### 5) 코드베이스에서 LLM 에이전트 토큰 효율을 높이는 구조

| 권고 | 근거 |
|---|---|
| "find the smallest set of high-signal tokens that maximize the likelihood of your desired outcome". Just-in-time 검색(가벼운 식별자만 들고 필요 시 도구로 읽기), 파일 시스템(계층·이름·타임스탬프)을 컨텍스트 신호로, 점진적 노출, 서브에이전트가 요약만 반환, 구조화된 노트 | https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents (2025-09-29) |
| 도구 통합(`list_users+list_events+create_event` → `schedule_event`), 응답에 UUID 대신 자연어 식별자, `response_format` enum(concise/detailed), 네임스페이스 접두사, 페이지네이션·절단 기본값, "Claude Code restricts tool responses to 25,000 tokens by default", 모호한 파라미터명 금지(`user` → `user_id`), 평가 기반 반복 | https://www.anthropic.com/engineering/writing-tools-for-agents (2025-09-11) |
| "finding the simplest solution possible, and only increasing complexity when needed". ACI: 예시·경계 사례·입력 형식 문서화, poka-yoke(절대 경로 강제 등) | https://www.anthropic.com/engineering/building-effective-agents (2024-12-19) |
| CLAUDE.md 는 짧게, 각 줄에 "Would removing this cause Claude to make mistakes?"; 포함: 추측 불가한 명령, 테스트 러너, 프로젝트 고유 아키텍처 결정, 환경 특이점 / 제외: 코드로 알 수 있는 것, API 문서, 파일별 설명. **검증 수단(테스트·빌드 종료 코드·스크립트 diff)** 제공이 핵심. 서브에이전트로 탐색 격리, 훅은 결정론적 보장, `claude -p` 헤드리스·JSON 출력 | https://code.claude.com/docs/en/best-practices (2026) |
| AGENTS.md: Linux Foundation 산하 Agentic AI Foundation 관리, 빌드/테스트 명령·코드 스타일·테스트 지침 권장, 중첩 시 "The closest AGENTS.md to the edited file wins", 6만+ 프로젝트 채택 | https://agents.md/ (2025~2026) |
| OpenAI "A practical guide to building agents" | https://cdn.openai.com/business-guides-and-resources/a-practical-guide-to-building-agents.pdf — PDF 파싱 실패 **미확인** |

---

## 프로젝트 적용 시사점 (TDGame 에서 무엇을 어떻게 쓰고 무엇을 피할지)

### 쓴다

1. **몬스터 정의 = "C++ 1파일 + JSON/CSV 표 1개" 규칙.** `Source/TDGame/AI/Monsters/<MonsterId>.cpp` 에 행동 카탈로그·전이 규칙을 플루언트 빌더로 등록하고(예: `Def.Action("Charge").Cost(2).Consider("DistanceToTarget", Curve::Logistic, m,k,b,c)...`), 수치는 `Content/Data/AI/<MonsterId>.csv`(UDataTable) 로 둔다. 근거: 컨텍스트 창 절약(작은 파일, just-in-time 읽기), 컴파일러가 LLM 오류를 기계적으로 잡음, 유틸리티 표 형식이 업계 표준(IAUS 4파라미터).
2. **스키마 문서 1장 + 검증 스크립트 1개.** `Docs/MonsterAI_CombatSim/schema/monster-ai-table.md`(열 정의·허용값·예시 3개) 와 `Tools/validate_ai_tables.py`(열·범위·참조 무결성·곡선 파라미터 범위 검사). arXiv 2510.16952 가 보여 준 "DSL 문서를 프롬프트에 주입 + 사전 검증" 을 그대로 재현한다.
3. **결정 로그는 JSONL 로 시뮬레이터가 직접 쓴다.** 한 틱 한 줄: `{"t":tick,"id":monster,"cands":[["Charge",0.71],["Idle",0.10]],"pick":"Charge","why":"utility","hash":"..."}`. 틱별 상태 해시로 결정론을 회귀 테스트(같은 시드·같은 입력 → 같은 해시 열). `UE_VLOG` 로 같은 내용을 이중 송출해 사람도 본다.
4. **StateTree 는 "읽기" 용도로만 MCP 를 쓴다.** 5.8 툴셋이 검사 전용이므로 기존 템플릿(Variant_TwinStick)의 StateTree 를 분석할 때는 `StateTreeToolset` 을 켜서 `get_tasks/get_transitions` 로 텍스트화하거나 `DebugInternalLayoutAsString()` 을 덤프한다.
5. **AGENTS.md / CLAUDE.md 에 헤드리스 검증 명령을 명시**(컴파일 명령, 자동화 테스트 필터, `validate_ai_tables.py`, 시뮬레이터 실행과 JSONL 경로). 각 줄은 "빠지면 실수가 나는가" 기준으로 유지.
6. **MCP 서버(공식)는 보조 채널.** 에디터가 떠 있을 때 에셋 조회·자동화 테스트 실행(`AutomationTestToolset`)에 쓰되, 파이프라인의 필수 경로로 두지 않는다(연결 실패가 잦고 인증 없음).

### 피한다

1. **StateTree/BT 에셋을 LLM 이 직접 생성하는 설계.** 공식 도구 없음, Python 의 FInstancedStruct 채우기 미확인, 에디터 없이는 컴파일 불가. 3중 위험.
2. **Angelscript/UnrealSharp/Lua 도입.** 각각 엔진 포크, .NET 런타임·결정론 미문서화, LLM 정확도 낮음. 이미 C++ 전용 프로젝트이므로 얻는 것은 핫리로드 하나뿐이고 Live Coding 이 그 역할을 부분 대체한다.
3. **T3D/.COPY 텍스트 경로.** 재수입 불가.
4. **`FRuntimeFloatCurve` 를 표의 곡선 표현으로 쓰는 것.** 키프레임 배열은 LLM 이 읽고 쓰기 어렵고 diff 가 지저분하다. 4파라미터 수식(m,k,b,c)을 1차로, 커스텀 곡선은 예외 열로.
5. **비대한 CLAUDE.md·파일별 설명.** Anthropic 지침대로 코드로 알 수 있는 것은 적지 않는다.

### 2순위(JSON DSL + 런타임 로더)로 넘어가는 조건

- 몬스터 종류가 100 종을 넘어 C++ 파일 컴파일 대기가 반복 실험을 막을 때.
- 디자이너(비개발자)가 행동 구조까지 손대야 할 때.
- 이때도 파서는 C++ 로 두고, 스키마 검증기는 1순위에서 만든 것을 확장한다.

---

## 미확인·미해결 질문

1. **Python `set_editor_property` 로 `TArray<FStateTreeEditorNode>`(FInstancedStruct) 를 채워 StateTree 를 만들 수 있는가?** 문서·포럼에서 사례를 찾지 못했다. 에디터를 띄워 `unreal.StateTreeState` 의 `tasks` 속성에 쓰기 시도로 확인 필요.
2. **ChiR24/Unreal_mcp 의 "State Tree operations" 실제 범위**(생성인지 조회인지). README 요약만 확인, 소스 미열람.
3. **Athena AI(Fab)·Utility Intelligence 의 데이터 스키마** — 페이지 403. IAUS/Curvature/StateTree 소스로 대체했다.
4. **BehaviorTree.CPP XML 스키마 문서 본문**(403). GitHub README 의 "XML 기반 DSL, 런타임 로드" 문장만 확인.
5. **최신 대형 모델(Claude/GPT 급)의 Angelscript·Lua 작성 정확도.** 연구는 7B 양자화 모델 기준. 프로젝트에서 소규모 벤치(같은 과제를 C++/JSON/Lua 로 10회 생성 → 컴파일·검증 통과율)로 직접 측정할 가치가 있음.
6. **OpenAI 에이전트 가이드(2025) PDF** 파싱 실패. Anthropic 지침으로 대체.
7. **SBP-BRiMS 2025 "Prompt Engineering on LLM-Synthesized Behavior Trees"** PDF 파싱 실패 — BT 텍스트 형식별 유효율 수치가 있으면 2순위 DSL 설계에 참고할 수 있음.
8. **Verse 의 UE6 이관 시점** 은 2차 보도(2027년 말 얼리 액세스)만 확인. Epic 1차 발표문 미열람.
9. **UnLua 핫리로드 지원 여부**와 결정론 관련 문서 미확인(어차피 채택 대상 아님).
10. **Visual Logger `.bvlog` 를 텍스트로 변환하는 공개 도구** 존재 여부 미확인. `FVisualLoggerHelpers::Serialize` 역직렬화로 자체 변환기를 만들 수는 있으나 우선순위 낮음.

---

## 부록 A. 1순위 형식의 구체 예시 (조사 결과를 그대로 옮긴 초안)

고려사항 표(CSV, 한 몬스터 한 파일). 열 구성은 IAUS(입력·곡선·m,k,b,c)와 StateTree `Weight`, Lewis 의 "싼 고려사항 먼저" 원칙을 합친 것이다.

```csv
action_id,consideration_id,input,curve,m,k,b,c,cost_rank,weight
Charge,DistToTarget,dist_norm_0_1200,Logistic,-0.8,1.0,0.0,0.5,1,2.0
Charge,SelfHpRatio,hp_ratio,Linear,1.0,1.0,0.0,0.0,1,2.0
Charge,LineOfSight,los_bool,Binary,1.0,1.0,0.0,0.0,3,2.0
Retreat,SelfHpRatio,hp_ratio,Logit,-1.0,1.0,1.0,0.0,1,3.0
```

결정 로그(JSONL, 시뮬레이터가 한 틱 한 줄). 상태 해시 열이 결정론 회귀 테스트의 비교 키다.

```json
{"t":120,"id":"Goblin_07","cands":[["Charge",0.71],["Retreat",0.12],["Idle",0.05]],"pick":"Charge","why":"utility_max","hash":"9f3a1c02"}
```

검증 스크립트가 잡아야 할 규칙(최소): 열 누락·형 불일치, `curve` 허용값 집합 밖, `m,k,b,c` 범위 이탈, `input` 함수 이름이 C++ 등록표에 없음, `cost_rank` 가 비싼 입력(레이캐스트 계열)에 1 로 지정됨, 같은 `action_id` 의 `weight` 불일치.

## 부록 B. 열어 본 출처 목록 (연도)

| 분류 | 출처 |
|---|---|
| Epic 공식 | Unreal MCP in Unreal Editor(2026), 5.8 Release Notes(2026), Scripting the Unreal Editor Using Python(5.8), Visual Logger(5.8), Gameplay Debugger 문서, Epic Developer Assistant 포럼 공지(2025) |
| 엔진 소스(로컬 5.8) | StateTree.h, StateTreeEditorData.h, StateTreeState.h, StateTreeCompiler*.h, StateTreeCommonConsiderations.h, VisualLoggerBinaryFileDevice.h/.cpp, VisualLogger.cpp, VisualLoggerTraceDevice.cpp, GameplayDebuggerCategory.h, Toolsets/StateTreeToolset·AIModuleToolset(uplugin, Python) |
| MCP 커뮤니티 | chongdashu/unreal-mcp, DeVoe09/UnrealMCP, remiphilippe/mcp-unreal, ChiR24/Unreal_mcp, StraySpark 비교 글(2026-03), byteiota(2026-06) |
| 스크립팅 | angelscript.hazelight.se(메인·개발 현황), UnrealSharp/UnrealSharp, Tencent/UnLua, Verse 2차 보도 2건(2026) |
| LLM 연구 | arXiv 2410.14766(2024), arXiv 2410.03981/TOSEM(2025), arXiv 2510.16952(2025), arXiv 2404.07439(2024) |
| 유틸리티 AI | tonogameconsultants IAUS(2025), apoch/curvature 위키, ric-fm/BTUtility, hollsteinm/ReasonablePlanningAI, BehaviorTree/BehaviorTree.CPP(2026) |
| 디버깅 | bugnet.io 결정론 디싱크 디버깅(2026-04), unreal-garden Visual Logger(2022) |
| 에이전트 설계 | Anthropic: Building effective agents(2024-12), Writing tools for agents(2025-09), Effective context engineering(2025-09), Claude Code best practices(2026); agents.md |

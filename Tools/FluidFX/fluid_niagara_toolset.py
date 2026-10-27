"""실행 환경: 시스템 Python 3 모듈(editor_make_fluid_driven_niagara.py·check_fluid_niagara_specs.py 가 가져온다). 언리얼 MCP 8000(NiagaraToolsets)·원격 실행은 실제 클라이언트를 만들 때만 필요하고, 가짜 클라이언트와 정규화 함수는 에디터 없이 돈다.
실행 명령: 직접 실행하지 않는다(import fluid_niagara_toolset).
출력: 없음. 공용 MCP 호출(TDNiagaraToolsetClient)·호출 기록 가짜 클라이언트(TDFakeNiagaraToolsetClient)·스택 조작(Set Parameters·모듈·동적 입력·표본 출력 연결·텍스처 바인딩 왕복·열거 입력)·렌더러 교체·덤프 정규화(v1 editor_make_fluid_ember_niagara.py 188-760 을 이미터 이름 인수로 옮긴 사본, 덤프 형식 sections.UserVariables.userVariables 동일)·컴파일 상태 문제 목록·에디터 로그(FLUSHLOG 뒤 오프셋 구간) 컴파일 문제 줄 집계를 준다.
상태: 2026-10-27 editor_recompile_system_fully(저장 전 강제 전체 재컴파일, 시스템 Spawn·Update 속성 집합 일치·실행 준비 보고) 추가. 이전: 2026-10-26 FX-5 추가(미검증, 가짜 클라이언트만): 데이터 인터페이스 입력 모듈(속성 값 쓰기·재읽기 왕복 + 연결·식 입력), KillParticlesInVolume 열거(Kill Shape, 표시 이름 Box 재읽기), 렌더러 전부 제거(none). 이전: 2026-10-26 수정·검증(에디터 0b 7회·생성 7회에서 동작, Niagara 오프스크린 12/12). 열거 입력은 엔진 형식 NiagaraExt_StackInputData_Enum{enum, enumName}(NiagaraExternalSystemEditorUtilities.h:556-571·.cpp:2985-2993, 0b 덤프 재읽기와 같은 꼴)으로 확정. 색·정수 값·GetEmitterInputValues 구조는 0b 확정. 로그 경로 표지 탐색·컴파일 문제 줄 집계는 생성 7회에서 GPU 문제 줄 0으로 동작 확인.
"""
import copy
import json
import math
import pathlib
import re
import sys
import uuid

TOOLS_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_ROOT = TOOLS_ROOT.parent
sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(TOOLS_ROOT / "FluidFX"))
import compare_niagara_dumps

SYSTEM_TOOLSET_SUFFIX = "NiagaraToolset_System"
SAVE_TOOL_SUFFIX = "save_assets"
COMPILE_WAIT_TIMEOUT_SECONDS = 300

FLOAT_TYPE = "/Script/Niagara.NiagaraFloat"
INT_TYPE = "/Script/Niagara.NiagaraInt32"
VECTOR2_TYPE = "/Script/CoreUObject.Vector2f"
VECTOR3_TYPE = "/Script/CoreUObject.Vector3f"
VECTOR4_TYPE = "/Script/CoreUObject.Vector4f"
COLOR_TYPE = "/Script/CoreUObject.LinearColor"
POSITION_TYPE = "/Script/Niagara.NiagaraPosition"
TEXTURE_CLASS = "/Script/Engine.Texture"
OBJECT_VALUE_STRUCT = "/Script/NiagaraEditor.NiagaraExt_VariableValue_Object"
LINKED_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
EXPRESSION_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"
DYNAMIC_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
DATA_INTERFACE_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface"
ENUM_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
LIFE_CYCLE_ENUM = "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode"
LOOP_BEHAVIOR_ENUM = "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions"
KILL_SHAPE_ENUM = "/Niagara/Enums/ENiagaraKillVolumeOptions.ENiagaraKillVolumeOptions"
COMPONENT_NAMES_BY_TYPE = {
    VECTOR2_TYPE: ("x", "y"),
    VECTOR3_TYPE: ("x", "y", "z"),
    POSITION_TYPE: ("x", "y", "z"),
    VECTOR4_TYPE: ("x", "y", "z", "w"),
    COLOR_TYPE: ("r", "g", "b", "a"),
}
ZERO_BY_TYPE = {FLOAT_TYPE: 0.0, INT_TYPE: 0, VECTOR2_TYPE: (0.0, 0.0), VECTOR3_TYPE: (0.0, 0.0, 0.0), POSITION_TYPE: (0.0, 0.0, 0.0),
                VECTOR4_TYPE: (0.0, 0.0, 0.0, 0.0), COLOR_TYPE: (0.0, 0.0, 0.0, 0.0)}
LOOK_PLACEHOLDER = "<look>"

TEXTURE_BINDING_PROPERTY = "TextureUserParameter"
RANDOM_MINIMUM_INPUT = "Minimum"
RANDOM_MAXIMUM_INPUT = "Maximum"
SAMPLE_TEXTURE_INPUT = "Texture"
SAMPLE_UV_INPUT = "UV"
PLANE_UV_ATTRIBUTE = "Particles.TDFluidPlaneUv"

UP_TO_DATE_STATUSES = ("UpToDate", "UpToDateWithWarnings", "ComputeUpToDateWithWarnings")
DROPPED_KEY_FRAGMENTS = ("guid", "changeid")
DROPPED_KEYS = ("registeredtypeindex",)
SORTED_LIST_SECTIONS = ("UserVariables", "SystemDependencies", "StackIssues")
SECTION_DUPLICATE_KEYS = {"SystemSummary": ("uservariables",)}
GUID_PATTERN = re.compile(r"[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}")
FLOAT_DIGITS = 6
MAX_RENDERER_PROBE = 8

FAKE_MODULE_INPUTS = {
    "SampleTexture": ("Module.Texture", "Module.UV"),
    "SpawnRate": ("Module.SpawnRate",),
    "SpawnBurst_Instantaneous": ("Module.Spawn Count",),
    "Collision": ("Module.Restitution", "Module.Friction"),
    "NM_TD_FluidMassSplat": ("Module.SplatInterface", "Module.bExecute", "Module.Position", "Module.Velocity", "Module.RadiusCm",
                             "Module.DensityStrength", "Module.HeatStrength"),
    "NM_TD_FluidNdcAppendPoint": ("Module.DataChannelWriter", "Module.bExecute", "Module.Position", "Module.Velocity", "Module.RadiusCm",
                                  "Module.DensityStrength", "Module.HeatStrength"),
    "KillParticlesInVolume": ("Module.Kill Shape", "Module.Volume Origin", "Module.Box Size", "Module.Position"),
}
FAKE_ENUM_INPUTS = {"Module.Kill Shape": KILL_SHAPE_ENUM}
FAKE_STATE_INPUTS = (("Module.Life Cycle Mode", LIFE_CYCLE_ENUM), ("Module.Loop Behavior", LOOP_BEHAVIOR_ENUM), ("Module.Loop Duration", ""))
FAKE_TEMPLATE_EMITTER = "Fountain"

COMPILE_ISSUE_TEXT = 'There were issues for compilation of script "'
COMPILE_LINE_TEXT = "Compiling System NiagaraSystem "
SHADER_ERROR_TEXT = ": error:"
LOG_MARKER = "TD_NIAGARA_LOG_MARK"
LOG_RECENT_SECONDS = 600
LOG_TAIL_BYTES = 4 * 1024 * 1024
MAX_REPORTED_LOG_LINES = 20


class TDNiagaraToolError(Exception):
    pass


class TDEditorConnectionError(Exception):
    pass


class TDPreconditionError(Exception):
    pass


def unwrap_tool_text(value):
    while True:
        if isinstance(value, str):
            try:
                value = json.loads(value)
                continue
            except json.JSONDecodeError:
                return value
        if isinstance(value, dict) and len(value) == 1:
            key = next(iter(value))
            if key.lower().replace("_", "") == "returnvalue":
                value = value[key]
                continue
        return value


class TDNiagaraToolsetClient:
    def __init__(self):
        import uemcp
        self.uemcp = uemcp
        try:
            self.session = uemcp.connect()
        except OSError as error:
            raise TDEditorConnectionError(f"MCP 연결 실패: {error}")
        self.toolset_listing = self.invoke_meta("list_toolsets", {})
        self.toolset_names = re.findall(r"^- ([^:\n]+):", str(self.toolset_listing), re.MULTILINE)
        matches = [name for name in self.toolset_names if name.endswith(SYSTEM_TOOLSET_SUFFIX)]
        if len(matches) != 1:
            raise TDNiagaraToolError(f"{SYSTEM_TOOLSET_SUFFIX} 툴셋 1개 기대, 찾음 {matches}")
        self.system_toolset = matches[0]
        self.calls = []

    def invoke_meta(self, name, arguments):
        try:
            response = self.uemcp.call_tool(self.session, name, arguments)
        except OSError as error:
            raise TDEditorConnectionError(f"MCP 호출 실패 {name}: {error}")
        if not response or "error" in response:
            raise TDNiagaraToolError(f"{name}: {json.dumps(response, ensure_ascii=False)[:2000]}")
        result = response.get("result", {})
        texts = [entry.get("text", "") for entry in result.get("content", []) if entry.get("type") == "text"]
        if result.get("isError"):
            raise TDNiagaraToolError(f"{name}: {' '.join(texts)[:2000]}")
        if not texts:
            return result.get("structuredContent")
        return unwrap_tool_text("\n".join(texts))

    def describe_toolset(self, toolset_name):
        return self.invoke_meta("describe_toolset", {"toolset_name": toolset_name})

    def call(self, tool_name, **arguments):
        self.calls.append(tool_name)
        return self.invoke_meta("call_tool", {"toolset_name": self.system_toolset, "tool_name": tool_name, "arguments": arguments})


class TDFakeNiagaraToolsetClient:
    def __init__(self):
        self.records = []
        self.calls = []
        self.emitters = []
        self.module_count = 0
        self.renderer_count = 1
        self.data_interface_values = {}
        self.added_modules = []

    def call(self, tool_name, **arguments):
        self.calls.append(tool_name)
        self.records.append({"tool": tool_name, "arguments": copy.deepcopy(arguments)})
        handler = getattr(self, "fake_" + tool_name, None)
        return handler(arguments) if handler else {"ok": True}

    def next_module_name(self, base):
        self.module_count += 1
        return f"{base}{self.module_count:03d}"

    def fake_CreateNiagaraSystem(self, arguments):
        self.emitters = [FAKE_TEMPLATE_EMITTER]
        return {"ok": True}

    def fake_GetSystemSummary(self, arguments):
        return {"emitters": [{"emitterName": name} for name in self.emitters]}

    def fake_RemoveEmitter(self, arguments):
        name = arguments["emitterToRemove"]["emitterName"]
        self.emitters = [item for item in self.emitters if item != name]
        return {"ok": True}

    def fake_AddEmitter(self, arguments):
        self.emitters.append(arguments["emitterName"])
        return {"ok": True}

    def fake_GetEmitterTopology(self, arguments):
        return {"renderers": [{"index": index} for index in range(self.renderer_count)]}

    def fake_GetEmitterInputValues(self, arguments):
        inputs = [{"name": name, "value": enum_value(enum_path, "") if enum_path else {}} for name, enum_path in FAKE_STATE_INPUTS]
        modules = [{"moduleName": "EmitterState", "scriptName": "EmitterUpdateScript", "inputs": inputs}]
        for module_name, base in self.added_modules:
            module_inputs = [{"name": name, "value": enum_value(FAKE_ENUM_INPUTS[name], "") if name in FAKE_ENUM_INPUTS else {}}
                             for name in FAKE_MODULE_INPUTS.get(base, ())]
            modules.append({"moduleName": module_name, "scriptName": "ParticleUpdateScript", "inputs": module_inputs})
        return {"modules": modules}

    def fake_AddSetParametersModule(self, arguments):
        names = [parameter["variable"]["name"] for parameter in arguments["parameters"]]
        return {"moduleName": self.next_module_name("SetParameters"), "inputs": [{"name": name} for name in names]}

    def fake_AddModule(self, arguments):
        base = arguments["moduleAsset"]["refPath"].rsplit(".", 1)[-1]
        module_name = self.next_module_name(base)
        self.added_modules.append((module_name, base))
        return {"moduleName": module_name, "inputs": [{"name": name} for name in FAKE_MODULE_INPUTS.get(base, ())]}

    def fake_SetStackInputData(self, arguments):
        data = arguments["inputData"]
        if data["struct"]["refPath"] == DATA_INTERFACE_INPUT_STRUCT:
            self.data_interface_values[canonical_ref(arguments["stackInputRef"])] = json.loads(data["value"]["propertyValues"])
        return {"ok": True}

    def fake_GetStackInputData(self, arguments):
        key = canonical_ref(arguments["stackInputRef"])
        values = self.data_interface_values.get(key, {TEXTURE_BINDING_PROPERTY: {"Parameter": {"Name": "None"}}})
        return {"propertyValues": json.dumps(values, ensure_ascii=False)}

    def fake_AddRenderer(self, arguments):
        self.renderer_count = 1
        return {"index": 0}

    def fake_RemoveRenderer(self, arguments):
        self.renderer_count = 0
        return {"ok": True}


def canonical_ref(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True)


def format_call_records(records):
    return "[\n" + ",\n".join(json.dumps(record, ensure_ascii=False, sort_keys=True, allow_nan=False) for record in records) + "\n]\n"


def run_editor_code(code):
    import run_in_editor
    result = run_in_editor.run_code(code)
    if not result.get("connected"):
        raise TDEditorConnectionError("원격 실행 연결 실패: " + " | ".join(result.get("lines", [])))
    if not result.get("success"):
        raise TDNiagaraToolError("에디터 Python 실패: " + " | ".join(result.get("lines", []))[:2000])
    return result.get("lines", [])


def read_marker(lines, marker):
    for line in lines:
        position = line.find(marker + " ")
        if position >= 0:
            return line[position + len(marker) + 1:].strip()
    return ""


def editor_asset_exists(path):
    lines = run_editor_code(f"import unreal\nprint('TD_EXISTS', unreal.EditorAssetLibrary.does_asset_exist({path!r}))\n")
    return read_marker(lines, "TD_EXISTS") == "True"


def editor_save_asset(path):
    code = (
        "import unreal\n"
        f"unreal.SystemLibrary.execute_console_command(None, 'NiagaraToolset.CompileWaitTimeoutSeconds {COMPILE_WAIT_TIMEOUT_SECONDS}')\n"
        f"asset = unreal.EditorAssetLibrary.load_asset({path!r})\n"
        "print('TD_SAVED', bool(asset) and unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False))\n"
    )
    if read_marker(run_editor_code(code), "TD_SAVED") != "True":
        raise TDNiagaraToolError(f"저장 실패: {path}")


def parse_recompile_report(text):
    return dict(item.split("=", 1) for item in text.split() if "=" in item)


def editor_recompile_system_fully(path):
    code = (
        "import unreal\n"
        f"asset = unreal.EditorAssetLibrary.load_asset({path!r})\n"
        "print('TD_RECOMPILE', unreal.TDFluidNiagaraModuleEditorLibrary.recompile_niagara_system_fully(asset))\n"
    )
    report = parse_recompile_report(read_marker(run_editor_code(code), "TD_RECOMPILE"))
    if not report:
        raise TDNiagaraToolError(f"강제 전체 재컴파일 보고 없음: {path}")
    return report


def is_system_attributes_match(report):
    return isinstance(report, dict) and report.get("attributes_match") == "1" and report.get("ready_to_run") == "1"


def object_path(asset_path):
    return f"{asset_path}.{asset_name(asset_path)}"


def asset_name(asset_path):
    return asset_path.rstrip("/").rsplit("/", 1)[1]


def asset_directory(asset_path):
    return asset_path.rstrip("/").rsplit("/", 1)[0]


def reference(path):
    return {"refPath": path}


def stack_ref(system_path, emitter="", script="", module="", renderer=-1, inputs=()):
    return {
        "system": reference(object_path(system_path)),
        "emitterName": emitter,
        "scriptName": script,
        "moduleName": module,
        "rendererIndex": renderer,
        "inputNameStack": list(inputs),
    }


def type_definition(type_path):
    return {"classStructOrEnum": reference(type_path)}


def plain_value(type_path, value):
    if type_path == FLOAT_TYPE:
        return {"value": float(value)}
    if type_path == INT_TYPE:
        return {"value": int(value)}
    components = COMPONENT_NAMES_BY_TYPE[type_path][: len(value)]
    return {name: float(component) for name, component in zip(components, value)}


def instanced_value(type_path, value):
    if type_path == TEXTURE_CLASS:
        return {"struct": reference(OBJECT_VALUE_STRUCT), "value": {"objectClass": reference(TEXTURE_CLASS), "object": reference("")}}
    if value == LOOK_PLACEHOLDER:
        return {"struct": reference(type_path), "value": LOOK_PLACEHOLDER}
    return {"struct": reference(type_path), "value": plain_value(type_path, value)}


def enum_value(enum_path, enum_name):
    return {"struct": reference(ENUM_INPUT_STRUCT), "value": {"enum": reference(enum_path), "enumName": enum_name}}


def linked_input(variable_name, type_path):
    return {"struct": reference(LINKED_INPUT_STRUCT), "value": {"linkedVariable": {"name": variable_name, "type": type_definition(type_path)}}}


def expression_input(expression):
    return {"struct": reference(EXPRESSION_INPUT_STRUCT), "value": {"hlslExpression": expression}}


def dynamic_input(asset):
    return {"struct": reference(DYNAMIC_INPUT_STRUCT), "value": {"dynamicInputAsset": reference(asset)}}


def data_interface_input(property_values):
    return {"struct": reference(DATA_INTERFACE_INPUT_STRUCT), "value": {"propertyValues": json.dumps(property_values, ensure_ascii=False)}}


def field(mapping, name):
    if not isinstance(mapping, dict):
        return None
    wanted = name.lower()
    for key, value in mapping.items():
        if key.lower() in (wanted, "b" + wanted):
            return value
    return None


def find_first(value, name):
    if isinstance(value, dict):
        direct = field(value, name)
        if direct is not None:
            return direct
        children = value.values()
    elif isinstance(value, list):
        children = value
    else:
        return None
    for child in children:
        found = find_first(child, name)
        if found is not None:
            return found
    return None


def parse_json_text(value):
    if isinstance(value, str):
        try:
            return json.loads(value)
        except json.JSONDecodeError:
            return value
    return value


def input_names_of(topology):
    inputs = find_first(topology, "inputs") or []
    return [str(field(item, "name")) for item in inputs if isinstance(item, dict) and field(item, "name") is not None]


def resolve_input_name(topology, wanted):
    names = input_names_of(topology)
    if wanted in names:
        return wanted
    normalized_wanted = wanted.split(".")[-1].replace(" ", "").lower()
    matches = [name for name in names if name.split(".")[-1].replace(" ", "").lower() == normalized_wanted]
    if len(matches) != 1:
        raise TDNiagaraToolError(f"입력 '{wanted}' 1개 기대, 찾음 {matches} / 전체 {names}")
    return matches[0]


def require_module_name(topology, label):
    name = find_first(topology, "moduleName")
    if not name:
        raise TDNiagaraToolError(f"{label}: 모듈 이름을 받지 못함: {json.dumps(topology, ensure_ascii=False)[:500]}")
    return str(name)


def find_module_entries(value, module_fragment):
    found = []
    if isinstance(value, dict):
        name = field(value, "moduleName")
        if isinstance(name, str) and module_fragment.lower() in name.lower():
            found.append(value)
        children = value.values()
    elif isinstance(value, list):
        children = value
    else:
        return found
    for child in children:
        found.extend(find_module_entries(child, module_fragment))
    return found


class TDStepRecorder:
    def __init__(self, strict):
        self.strict = strict
        self.steps = []

    def run(self, name, action):
        try:
            result = action()
        except TDEditorConnectionError:
            raise
        except Exception as error:
            self.steps.append({"step": name, "ok": False, "error": str(error)[:2000]})
            if self.strict:
                raise TDNiagaraToolError(f"{name}: {error}")
            return None
        self.steps.append({"step": name, "ok": True})
        return result

    def failed_steps(self):
        return [step["step"] for step in self.steps if not step["ok"]]


def create_system(client, system_path, template):
    return client.call(
        "CreateNiagaraSystem", assetName=asset_name(system_path), assetPath=asset_directory(system_path), templateSystem=reference(template)
    )


def sampled_color_attribute(sample_module_name):
    return f"Particles.{sample_module_name}.SampledColor"


def replace_placeholders(expression, placeholders):
    for placeholder in sorted(placeholders, key=len, reverse=True):
        expression = expression.replace(placeholder, placeholders[placeholder])
    return expression


def set_entry_input(client, system_path, emitter, script, module_name, topology, entry, placeholders, sample_sources):
    variable_name, type_path, kind, payload = entry
    if kind == "value":
        return
    input_name = resolve_input_name(topology, variable_name)
    base = stack_ref(system_path, emitter, script, module_name, inputs=(input_name,))
    if kind == "expression":
        client.call("SetStackInputData", stackInputRef=base, inputData=expression_input(replace_placeholders(payload, placeholders)))
        return
    if kind == "sample_link":
        if payload not in sample_sources:
            raise TDNiagaraToolError(f"{variable_name}: 표본 {payload} 의 모듈 출력이 없음(앞 표본 단계 실패)")
        client.call("SetStackInputData", stackInputRef=base, inputData=linked_input(sample_sources[payload], type_path))
        return
    asset, minimum, maximum = payload
    client.call("SetStackInputData", stackInputRef=base, inputData=dynamic_input(asset))
    for nested_name, nested_value in ((RANDOM_MINIMUM_INPUT, minimum), (RANDOM_MAXIMUM_INPUT, maximum)):
        nested = stack_ref(system_path, emitter, script, module_name, inputs=(input_name, nested_name))
        client.call("SetStackInputData", stackInputRef=nested, inputData=instanced_value(type_path, nested_value))


def add_set_parameters(client, system_path, emitter, step, placeholders, sample_sources):
    script = step["script"]
    parameters = [
        {"variable": {"name": name, "type": type_definition(type_path)},
         "defaultValue": instanced_value(type_path, payload if kind == "value" else ZERO_BY_TYPE[type_path])}
        for name, type_path, kind, payload in step["entries"]
    ]
    topology = client.call("AddSetParametersModule", moduleLocationRef=stack_ref(system_path, emitter, script), parameters=parameters)
    module_name = require_module_name(topology, step["label"])
    for entry in step["entries"]:
        set_entry_input(client, system_path, emitter, script, module_name, topology, entry, placeholders, sample_sources)
    return module_name


def add_module(client, system_path, emitter, step):
    topology = client.call("AddModule", moduleLocationRef=stack_ref(system_path, emitter, step["script"]), moduleAsset=reference(step["module"]))
    return require_module_name(topology, step["label"]), topology


def add_expression_module(client, system_path, emitter, step):
    module_name, topology = add_module(client, system_path, emitter, step)
    input_name = resolve_input_name(topology, step["input"])
    client.call(
        "SetStackInputData",
        stackInputRef=stack_ref(system_path, emitter, step["script"], module_name, inputs=(input_name,)),
        inputData=expression_input(step["expression"]),
    )
    return module_name


def add_linked_inputs_module(client, system_path, emitter, step):
    module_name, topology = add_module(client, system_path, emitter, step)
    for wanted, (variable_name, type_path) in step["links"]:
        input_name = resolve_input_name(topology, wanted)
        client.call(
            "SetStackInputData",
            stackInputRef=stack_ref(system_path, emitter, step["script"], module_name, inputs=(input_name,)),
            inputData=linked_input(variable_name, type_path),
        )
    return module_name


def read_input_data(client, input_ref):
    return client.call("GetStackInputData", stackInputRef=input_ref)


def find_input_entry(module_entry, input_name):
    for item in field(module_entry, "inputs") or []:
        if isinstance(item, dict) and str(field(item, "name")) == input_name:
            return item
    return None


def read_enum_path(value):
    return field(find_first(value, "enum"), "refPath")


def set_state_enums(client, system_path, emitter, step, state_report):
    values = client.call("GetEmitterInputValues", emitterRef=stack_ref(system_path, emitter))
    entries = find_module_entries(values, step["module_fragment"])
    if len(entries) != 1:
        raise TDNiagaraToolError(f"{step['label']}: '{step['module_fragment']}' 모듈 1개 기대, 찾음 {len(entries)}")
    module_name = str(field(entries[0], "moduleName"))
    for wanted, enum_path, enum_name, display_name in step["enums"]:
        input_name = resolve_input_name(entries[0], wanted)
        current_enum = read_enum_path(find_input_entry(entries[0], input_name))
        if current_enum != enum_path:
            raise TDNiagaraToolError(f"{step['label']}: 입력 '{input_name}' 의 열거 형식 {current_enum!r} ≠ 기대 {enum_path!r}")
        input_ref = stack_ref(system_path, emitter, step["script"], module_name, inputs=(input_name,))
        client.call("SetStackInputData", stackInputRef=input_ref, inputData=enum_value(enum_path, enum_name))
        stored = read_input_data(client, input_ref)
        state_report.append({"step": step["label"], "input": input_name, "requested": enum_name, "requested_display": display_name,
                             "stored_enum_name": find_first(stored, "enumName"), "stored_display": find_first(stored, "displayName"),
                             "stored_text": json.dumps(stored, ensure_ascii=False)[:500]})
    return module_name


def is_state_roundtrip_ok(state_report, expected_count):
    return len(state_report) == expected_count and all(
        item["stored_enum_name"] == item["requested"] and item["stored_display"] == item["requested_display"] for item in state_report)


def read_data_interface_values(client, input_ref):
    data = read_input_data(client, input_ref)
    values = parse_json_text(find_first(data, "propertyValues"))
    if not isinstance(values, dict):
        raise TDNiagaraToolError(f"데이터 인터페이스 값(propertyValues)을 읽지 못함: {json.dumps(data, ensure_ascii=False)[:500]}")
    return values


def read_binding_name(values):
    parameter = field(field(values, TEXTURE_BINDING_PROPERTY), "Parameter")
    return field(parameter, "Name")


def write_binding_name(values, binding_name):
    binding = field(values, TEXTURE_BINDING_PROPERTY)
    parameter = field(binding, "Parameter")
    if not isinstance(parameter, dict):
        raise TDNiagaraToolError(f"{TEXTURE_BINDING_PROPERTY}.Parameter 가 없음: {json.dumps(values, ensure_ascii=False)[:500]}")
    name_key = next((key for key in parameter if key.lower() == "name"), "Name")
    parameter[name_key] = binding_name
    return {next(key for key in values if key.lower() == TEXTURE_BINDING_PROPERTY.lower()): binding}


def add_sample_texture(client, system_path, emitter, step, sample_module_asset, binding_report):
    module_name, topology = add_module(client, system_path, emitter, {"script": step["script"], "module": sample_module_asset, "label": step["label"]})
    texture_input = resolve_input_name(topology, SAMPLE_TEXTURE_INPUT)
    uv_input = resolve_input_name(topology, SAMPLE_UV_INPUT)
    texture_ref = stack_ref(system_path, emitter, step["script"], module_name, inputs=(texture_input,))
    before = read_data_interface_values(client, texture_ref)
    client.call("SetStackInputData", stackInputRef=texture_ref, inputData=data_interface_input(write_binding_name(copy.deepcopy(before), step["texture"])))
    after = read_data_interface_values(client, texture_ref)
    stored = read_binding_name(after)
    binding_report.append({"step": step["label"], "requested": step["texture"], "stored": stored, "before": before, "after": after})
    if stored != step["texture"]:
        raise TDNiagaraToolError(f"{step['label']}: 텍스처 바인딩 왕복 불일치 {stored!r} ≠ {step['texture']!r}")
    client.call(
        "SetStackInputData",
        stackInputRef=stack_ref(system_path, emitter, step["script"], module_name, inputs=(uv_input,)),
        inputData=linked_input(PLANE_UV_ATTRIBUTE, VECTOR2_TYPE),
    )
    return module_name


def is_binding_roundtrip_ok(binding_report, expected_count):
    return len(binding_report) == expected_count and all(item["stored"] == item["requested"] for item in binding_report)


def add_data_interface_module(client, system_path, emitter, step, data_interface_report):
    module_name, topology = add_module(client, system_path, emitter, step)
    interface_input_name = resolve_input_name(topology, step["data_interface_input"])
    data_interface_ref = stack_ref(system_path, emitter, step["script"], module_name, inputs=(interface_input_name,))
    requested = step["data_interface_values"]
    client.call("SetStackInputData", stackInputRef=data_interface_ref, inputData=data_interface_input(requested))
    stored = read_data_interface_values(client, data_interface_ref)
    data_interface_report.append({"step": step["label"], "requested": requested, "stored": stored})
    if not is_data_interface_value_stored(requested, stored):
        raise TDNiagaraToolError(f"{step['label']}: 데이터 인터페이스 값 왕복 불일치 요청 {requested} / 저장 {json.dumps(stored, ensure_ascii=False)[:500]}")
    for wanted, (variable_name, type_path) in step["links"]:
        input_name = resolve_input_name(topology, wanted)
        client.call("SetStackInputData", stackInputRef=stack_ref(system_path, emitter, step["script"], module_name, inputs=(input_name,)),
                    inputData=linked_input(variable_name, type_path))
    set_expression_inputs(client, system_path, emitter, step, module_name, topology)
    return module_name


def set_expression_inputs(client, system_path, emitter, step, module_name, topology):
    for wanted, expression in step["expressions"]:
        input_name = resolve_input_name(topology, wanted)
        client.call("SetStackInputData", stackInputRef=stack_ref(system_path, emitter, step["script"], module_name, inputs=(input_name,)),
                    inputData=expression_input(expression))


def stored_text_of(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value).casefold()


def is_data_interface_value_stored(requested, stored):
    for key, value in requested.items():
        actual = field(stored, key)
        if actual is None or stored_text_of(value) not in stored_text_of(actual):
            return False
    return True


def is_data_interface_roundtrip_ok(data_interface_report, expected_count):
    return len(data_interface_report) == expected_count and all(
        is_data_interface_value_stored(item["requested"], item["stored"]) for item in data_interface_report)


def add_kill_volume_module(client, system_path, emitter, step, state_report):
    module_name, topology = add_module(client, system_path, emitter, step)
    set_state_enums(client, system_path, emitter, dict(step, module_fragment=module_name), state_report)
    set_expression_inputs(client, system_path, emitter, step, module_name, topology)
    return module_name


def remove_all_renderers(client, system_path, emitter):
    topology = client.call("GetEmitterTopology", emitterRef=stack_ref(system_path, emitter))
    renderers = find_first(topology, "renderers") or []
    for _ in renderers:
        client.call("RemoveRenderer", rendererToRemove=stack_ref(system_path, emitter, renderer=0))
    remaining = find_first(client.call("GetEmitterTopology", emitterRef=stack_ref(system_path, emitter)), "renderers") or []
    if remaining:
        raise TDNiagaraToolError(f"렌더러 제거 실패: 남음 {len(remaining)}")
    return -1


def replace_renderer(client, system_path, emitter, renderer_class, renderer_data):

    client.call("RemoveRenderer", rendererToRemove=stack_ref(system_path, emitter, renderer=0))
    added = client.call("AddRenderer", newRendererLocation=stack_ref(system_path, emitter), rendererClass=reference(renderer_class))
    index = find_first(added, "index")
    renderer_index = int(index) if isinstance(index, (int, float)) else 0
    client.call(
        "SetRendererData",
        renderer=stack_ref(system_path, emitter, renderer=renderer_index),
        rendererData={"propertyValues": json.dumps(renderer_data, ensure_ascii=False)},
    )
    return renderer_index


def list_emitter_names(client, system_path):
    summary = client.call("GetSystemSummary", system=reference(object_path(system_path)))
    return [str(field(item, "emitterName")) for item in as_list(summary, "emitters") if isinstance(item, dict)]


def remove_template_emitters(client, system_path, emitter):
    removed = [name for name in list_emitter_names(client, system_path) if name != emitter]
    for name in removed:
        client.call("RemoveEmitter", emitterToRemove=stack_ref(system_path, name))
    remaining = list_emitter_names(client, system_path)
    if any(name != emitter for name in remaining):
        raise TDNiagaraToolError(f"템플릿 이미터 제거 실패: 남음 {remaining}")
    return removed


def round_float(value):
    if math.isnan(value) or math.isinf(value):
        return str(value)
    rounded = round(value, FLOAT_DIGITS)
    return 0.0 if rounded == 0 else rounded


def self_name_patterns(system_path):
    names = sorted({object_path(system_path), system_path, asset_name(system_path)}, key=len, reverse=True)
    return [re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])") for name in names]


def normalize_text(text, patterns):
    stripped = text.strip()
    if stripped[:1] in ("{", "["):
        try:
            return json.loads(stripped), True
        except json.JSONDecodeError:
            pass
    text = GUID_PATTERN.sub("<GUID>", text)
    for pattern in patterns:
        text = pattern.sub("<SELF>", text)
    return text, False


def is_dropped_key(key):
    lowered = key.lower()
    return lowered in DROPPED_KEYS or any(fragment in lowered for fragment in DROPPED_KEY_FRAGMENTS)


def normalize_value(value, patterns, sort_lists):
    if isinstance(value, dict):
        return {key: normalize_value(item, patterns, sort_lists) for key, item in value.items() if not is_dropped_key(key)}
    if isinstance(value, list):
        items = [normalize_value(item, patterns, sort_lists) for item in value]
        return sorted(items, key=compare_niagara_dumps.canonical_json) if sort_lists else items
    if isinstance(value, bool) or value is None or isinstance(value, int):
        return value
    if isinstance(value, float):
        return round_float(value)
    if isinstance(value, str):
        text, was_json = normalize_text(value, patterns)
        return normalize_value(text, patterns, sort_lists) if was_json else text
    return str(value)


def drop_section_duplicates(name, raw):
    duplicated = SECTION_DUPLICATE_KEYS.get(name, ())
    if not isinstance(raw, dict) or not duplicated:
        return raw
    return {key: value for key, value in raw.items() if key.lower() not in duplicated}


def normalize_sections(raw_sections, system_path):
    patterns = self_name_patterns(system_path)
    return {
        name: normalize_value(drop_section_duplicates(name, raw), patterns, name in SORTED_LIST_SECTIONS)
        for name, raw in raw_sections.items()
    }


def call_for_section(client, tool, arguments):
    try:
        return client.call(tool, **arguments)
    except TDNiagaraToolError as error:
        return {"error": str(error)}


def collect_raw_sections(client, system_path, emitter):
    system = reference(object_path(system_path))
    emitter_ref = stack_ref(system_path, emitter)
    prefix = f"Emitter.{emitter}."
    calls = (
        ("SystemSummary", "GetSystemSummary", {"system": system}),
        ("SystemData", "GetSystemData", {"system": system}),
        ("UserVariables", "GetUserVariables", {"system": system}),
        ("SystemDependencies", "GetSystemDependencies", {"system": system}),
        (prefix + "Summary", "GetEmitterSummary", {"emitterRef": emitter_ref}),
        (prefix + "Topology", "GetEmitterTopology", {"emitterRef": emitter_ref}),
        (prefix + "Data", "GetEmitterData", {"emitterRef": emitter_ref}),
        (prefix + "InputValues", "GetEmitterInputValues", {"emitterRef": emitter_ref}),
        ("CompileState", "GetSystemCompileState", {"system": system}),
        ("StackIssues", "GetStackIssues", {"system": system}),
    )
    sections = {section: call_for_section(client, tool, arguments) for section, tool, arguments in calls}
    renderers = find_first(sections[prefix + "Topology"], "renderers")
    count = len(renderers) if isinstance(renderers, list) else MAX_RENDERER_PROBE
    for index in range(count):
        data = call_for_section(client, "GetRendererData", {"rendererRef": stack_ref(system_path, emitter, renderer=index)})
        if not isinstance(renderers, list) and isinstance(data, dict) and "error" in data:
            break
        sections[f"{prefix}Renderer.{index}"] = data
    return sections


def build_dump(raw_sections, system_path, tool_name):
    sections = normalize_sections(raw_sections, system_path)
    hashes = compare_niagara_dumps.hash_sections(sections)
    return {
        "asset": system_path,
        "tool": tool_name,
        "sections": sections,
        "sha256": hashes,
        "sha256_all": compare_niagara_dumps.hash_all_sections(hashes),
    }


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=True) + "\n", encoding="utf-8")


def dump_system(client, system_path, emitter, output_dir, tool_name):
    dump = build_dump(collect_raw_sections(client, system_path, emitter), system_path, tool_name)
    path = output_dir / f"{asset_name(system_path)}.dump.json"
    write_json(path, dump)
    for name, digest in dump["sha256"].items():
        print(f"SHA256 {name} {digest}")
    print(f"SHA256 ALL {dump['sha256_all']}")
    print("덤프: " + str(path))
    return dump


def read_property_values(result):
    values = parse_json_text(find_first(result, "propertyValues"))
    return values if isinstance(values, dict) else {}


def values_match(actual, expected):
    if isinstance(expected, dict):
        return isinstance(actual, dict) and all(values_match(field(actual, key), item) for key, item in expected.items())
    if isinstance(expected, list):
        return isinstance(actual, list) and all(any(values_match(candidate, item) for candidate in actual) for item in expected)
    if isinstance(expected, bool):
        return bool(actual) == expected if isinstance(actual, (bool, int)) else False
    if isinstance(expected, (int, float)):
        return isinstance(actual, (int, float)) and abs(float(actual) - float(expected)) <= 1e-3
    return str(actual).split("::")[-1] == str(expected)


def as_list(section, key):
    if isinstance(section, list):
        return section
    found = find_first(section, key)
    return found if isinstance(found, list) else []


def collect_compile_statuses(compile_state):
    statuses = {}
    for item in as_list(compile_state, "scripts"):
        if not isinstance(item, dict):
            continue
        script = str(field(item, "scriptName")).split("::")[-1]
        emitter = str(field(item, "emitterName"))
        key = script if emitter in ("None", "") else f"{emitter}.{script}"
        statuses[key] = str(field(item, "lastCompileStatus")).split("::")[-1]
    return statuses


def find_compile_problems(compile_state, required_scripts):
    statuses = collect_compile_statuses(compile_state)
    problems = [] if statuses else ["스크립트 컴파일 상태 없음"]
    problems.extend(f"{key} 없음" for key in required_scripts if key not in statuses)
    problems.extend(f"{key} = {status}" for key, status in sorted(statuses.items()) if status not in UP_TO_DATE_STATUSES)
    if find_first(compile_state, "hasErrors") is not False:
        problems.append(f"hasErrors = {find_first(compile_state, 'hasErrors')!r}")
    for item in as_list(compile_state, "scripts"):
        summary = field(item, "errorSummary") if isinstance(item, dict) else None
        if summary:
            problems.append(f"{field(item, 'emitterName')}.{field(item, 'scriptName')}: {str(summary)[:300]}")
    return problems


def editor_log_path():
    marker = f"{LOG_MARKER} {uuid.uuid4().hex}"
    code = (
        "import os, time, unreal\n"
        f"unreal.log({marker!r})\n"
        "unreal.SystemLibrary.execute_console_command(None, 'FLUSHLOG')\n"
        "folder = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_log_dir())\n"
        "default = os.path.normpath(os.path.join(folder, unreal.SystemLibrary.get_game_name() + '.log'))\n"
        "paths = [os.path.normpath(os.path.join(folder, name)) for name in os.listdir(folder) if name.lower().endswith(('.log', '.txt'))]\n"
        f"recent = sorted((path for path in paths if time.time() - os.path.getmtime(path) < {LOG_RECENT_SECONDS}), key=lambda path: path != default)\n"
        "def has_marker(path):\n"
        "    with open(path, 'rb') as handle:\n"
        f"        handle.seek(max(0, os.path.getsize(path) - {LOG_TAIL_BYTES}))\n"
        f"        return {marker!r}.encode() in handle.read()\n"
        "found = [path for path in recent if has_marker(path)]\n"
        "print('TD_LOG_PATH', found[0] if found else '')\n"
    )
    path = read_marker(run_editor_code(code), "TD_LOG_PATH")
    if not path or not pathlib.Path(path).is_file():
        raise TDNiagaraToolError(f"에디터 로그 파일을 표지로 찾지 못함(ProjectLogDir 최근 {LOG_RECENT_SECONDS}초 .log): {path!r}")
    return path


def editor_log_size(path):
    run_editor_code("import unreal\nunreal.SystemLibrary.execute_console_command(None, 'FLUSHLOG')\n")
    return pathlib.Path(path).stat().st_size


def read_editor_log_since(path, offset):
    size = editor_log_size(path)
    if size < offset:
        raise TDNiagaraToolError(f"에디터 로그가 생성 전 오프셋 {offset} 보다 짧음({size}, 재시작·회전): {path}")
    with open(path, "rb") as handle:
        handle.seek(offset)
        return handle.read(size - offset).decode("utf-8", errors="replace")


def summarize_compile_log(text, system_path):
    lines = text.splitlines()
    issue_prefix = f"{COMPILE_ISSUE_TEXT}{asset_name(system_path)}/"
    compile_prefix = f"{COMPILE_LINE_TEXT}{object_path(system_path)} "
    issues = [line.strip() for line in lines if issue_prefix in line]
    errors = [line.strip() for line in lines if SHADER_ERROR_TEXT in line]
    return {"issue_count": len(issues), "compile_count": sum(1 for line in lines if compile_prefix in line),
            "issue_lines": issues[:MAX_REPORTED_LOG_LINES], "error_lines": errors[:MAX_REPORTED_LOG_LINES]}


def is_compile_log_clean(log_report):
    return isinstance(log_report, dict) and log_report.get("compile_count", 0) > 0 and log_report.get("issue_count") == 0


def probe_call(client, label, tool, arguments, results):
    try:
        results[label] = {"ok": True, "result": client.call(tool, **arguments)}
    except TDNiagaraToolError as error:
        results[label] = {"ok": False, "error": str(error)}


def find_save_tool(client):
    for name in client.toolset_names:
        if "asset" not in name.lower():
            continue
        try:
            description = client.describe_toolset(name)
        except TDNiagaraToolError:
            continue
        tools = description.get("tools", []) if isinstance(description, dict) else []
        found = [tool.get("name") for tool in tools if str(tool.get("name", "")).endswith(SAVE_TOOL_SUFFIX)]
        if found:
            return found[0]
    return ""

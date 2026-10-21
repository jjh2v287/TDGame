"""실행 환경: 시스템 Python 3. 에디터가 이 프로젝트를 열고 MCP 8000(NiagaraToolsets)·원격 실행이 살아 있어야 한다. 공식 NiagaraToolset_System 으로 GPU 불티 나이아가라 시스템(D-57 N0: 결정론 시드·고정 경계·영속 ID, User 변수 14개, SampleTexture 밀도 거절·속도 끌림, 구 메시 렌더러 + MI_TD_FluidEmberNiagara_Default)을 상수표 하나와 고정 호출 순서로 만들고, 저장(run_in_editor)·컴파일·스택 확인 뒤 정규화 덤프를 남긴다. 덮어쓰기·삭제는 하지 않는다.
실행 명령: python -B Tools/FluidFX/editor_make_fluid_ember_niagara.py [--probe] [--dump-only] [--asset <경로>] [--ember-base-scale 0.025] (--probe 단독 = 단계 0a 쓰기 없음, --probe --asset <새 경로> = 단계 0b 시범 생성·바인딩 왕복, 생성은 0b 통과와 editor_make_fluid_flame_material.py 의 {"only": ["ember_niagara"]} 실행 뒤)
출력: Saved/FluidFX/niagara/probe.json(0a·0b), --asset(기본 /Game/FluidFX/Niagara/NS_TD_FluidEmber) 의 나이아가라 시스템, Saved/FluidFX/niagara/<에셋 이름>.dump.json(정규화 절 + 절별 sha256)·<에셋 이름>.report.json(단계·확인). 종료 0 = 통과, 1 = 단계·확인 실패, 2 = 에디터·MCP 연결 실패, 3 = 선행 조건(에셋 있음·0b 미통과·재질 없음)
상태: 검증됨(2026-10-21 FX-8c 12차 N0-a 에디터 실행: 0a 실패 호출 0, 0b 실패 단계 0·바인딩 왕복 2/2, 생성 확인 9/9, ReproA=B=C 절별 해시 같음·ReproScale 은 UserVariables 만 다름). 빈 템플릿은 거부되어 DefaultSystem 템플릿의 Fountain 이미터를 지운다. 에디터 재시작 뒤 불러온 시스템은 GetSystemCompileState·GetStackIssues 가 컴파일 대기 시간 초과(NeedsRequestCompile, 미해결)."""
import argparse
import copy
import json
import math
import pathlib
import re
import sys

TOOLS_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_ROOT = TOOLS_ROOT.parent
sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(TOOLS_ROOT / "FluidFX"))
import uemcp
import compare_niagara_dumps

OUTPUT_DIR = PROJECT_ROOT / "Saved" / "FluidFX" / "niagara"
PROBE_PATH = OUTPUT_DIR / "probe.json"
DEFAULT_ASSET_PATH = "/Game/FluidFX/Niagara/NS_TD_FluidEmber"
ALLOWED_ASSET_ROOT = "/Game/FluidFX/Niagara/"
SYSTEM_TOOLSET_SUFFIX = "NiagaraToolset_System"
SAVE_TOOL_SUFFIX = "save_assets"
COMPILE_WAIT_TIMEOUT_SECONDS = 300

EMITTER_NAME = "Ember"
SYSTEM_RANDOM_SEED = 1
EMITTER_RANDOM_SEED = 1
FIXED_BOUNDS_HALF_EXTENT_CM = 600.0
DEFAULT_EMBER_BASE_SCALE = 0.025

EMPTY_TEMPLATE_SYSTEM = ""
DEFAULT_TEMPLATE_SYSTEM = "/Niagara/DefaultAssets/DefaultSystem.DefaultSystem"
MINIMAL_EMITTER = "/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"
SAMPLE_TEXTURE_MODULE = "/Niagara/Modules/Update/Textures/SampleTexture.SampleTexture"
SPAWN_RATE_MODULE = "/Niagara/Modules/Emitter/SpawnRate.SpawnRate"
SOLVE_FORCES_MODULE = "/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"
INITIALIZE_PARTICLE_MODULE = "/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle"
RANDOM_RANGE_FLOAT = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"
RANDOM_RANGE_VECTOR2D = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeVector2D.RandomRangeVector2D"
TEXTURE_DATA_INTERFACE_CLASS = "/Script/Niagara.NiagaraDataInterfaceTexture"
MESH_RENDERER_CLASS = "/Script/Niagara.NiagaraMeshRendererProperties"
SPHERE_MESH = "/Engine/BasicShapes/Sphere.Sphere"
EMBER_MATERIAL_ASSET = "/Game/FluidFX/Materials/MI_TD_FluidEmberNiagara_Default"

FLOAT_TYPE = "/Script/Niagara.NiagaraFloat"
VECTOR2_TYPE = "/Script/CoreUObject.Vector2f"
VECTOR3_TYPE = "/Script/CoreUObject.Vector3f"
VECTOR4_TYPE = "/Script/CoreUObject.Vector4f"
POSITION_TYPE = "/Script/Niagara.NiagaraPosition"
TEXTURE_CLASS = "/Script/Engine.Texture"
OBJECT_VALUE_STRUCT = "/Script/NiagaraEditor.NiagaraExt_VariableValue_Object"
LINKED_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
EXPRESSION_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_HlslExpression"
DYNAMIC_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
DATA_INTERFACE_INPUT_STRUCT = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DataInterface"

EMITTER_UPDATE_SCRIPT = "EmitterUpdateScript"
PARTICLE_SPAWN_SCRIPT = "ParticleSpawnScript"
PARTICLE_UPDATE_SCRIPT = "ParticleUpdateScript"
GPU_COMPUTE_SCRIPT = "ParticleGPUComputeScript"
UP_TO_DATE_STATUSES = ("UpToDate", "UpToDateWithWarnings", "ComputeUpToDateWithWarnings")

SPAWN_RATE_INPUT = "SpawnRate"
SAMPLE_TEXTURE_INPUT = "Texture"
SAMPLE_UV_INPUT = "UV"
TEXTURE_BINDING_PROPERTY = "TextureUserParameter"
RANDOM_MINIMUM_INPUT = "Minimum"
RANDOM_MAXIMUM_INPUT = "Maximum"
SAMPLED_COLOR = "Particles.SampleTexture.SampledColor"

USER_VARIABLES = (
    ("User.SimCenterWorld", VECTOR3_TYPE, (0.0, 0.0, 0.0)),
    ("User.SimExtentsCm", VECTOR2_TYPE, (0.0, 0.0)),
    ("User.DensityPressureTexture", TEXTURE_CLASS, None),
    ("User.VelocityTexture", TEXTURE_CLASS, None),
    ("User.AuxTexture", TEXTURE_CLASS, None),
    ("User.WaterSurfaceTexture", TEXTURE_CLASS, None),
    ("User.SimCenterPlane", VECTOR2_TYPE, (0.0, 0.0)),
    ("User.PlaneAxisU", VECTOR3_TYPE, (1.0, 0.0, 0.0)),
    ("User.PlaneAxisV", VECTOR3_TYPE, (0.0, 1.0, 0.0)),
    ("User.PlaneAxisN", VECTOR3_TYPE, (0.0, 0.0, 1.0)),
    ("User.PlaneCoordinateOffsetCm", VECTOR2_TYPE, (0.0, 0.0)),
    ("User.VelocityReferenceCmPerSec", FLOAT_TYPE, 600.0),
    ("User.EmberSpawnRate", FLOAT_TYPE, 10.0),
    ("User.EmberBaseScale", FLOAT_TYPE, DEFAULT_EMBER_BASE_SCALE),
)

PLANE_UV_EXPRESSION = (
    "saturate((float2(dot(Particles.Position, User.PlaneAxisU), dot(Particles.Position, User.PlaneAxisV))"
    " + User.PlaneCoordinateOffsetCm - User.SimCenterPlane) / max(User.SimExtentsCm, float2(1.0, 1.0)) + 0.5)"
)
SPAWN_POSITION_EXPRESSION = (
    "User.SimCenterWorld + User.PlaneAxisU * ((Particles.TDEmberSpawnUv.x - 0.5) * User.SimExtentsCm.x)"
    " + User.PlaneAxisV * ((Particles.TDEmberSpawnUv.y - 0.5) * User.SimExtentsCm.y)"
)
SPAWN_SCALE_EXPRESSION = "float3(User.EmberBaseScale, User.EmberBaseScale, User.EmberBaseScale)"
SPAWN_REJECT_EXPRESSION = f"Particles.TDEmberRejectRoll < {SAMPLED_COLOR}.b ? Particles.Lifetime : 0.0"
VELOCITY_EXPRESSION = (
    f"lerp(Particles.Velocity, (User.PlaneAxisU * ({SAMPLED_COLOR}.r * 2.0 - 1.0) + User.PlaneAxisV * ({SAMPLED_COLOR}.g * 2.0 - 1.0))"
    " * User.VelocityReferenceCmPerSec, 1.0 - exp(-6.0 * Engine.DeltaTime))"
    " + float3(0.0, 0.0, (250.0 * Particles.TDEmberHeat - 400.0 * (1.0 - Particles.TDEmberHeat)) * Engine.DeltaTime)"
)
COOLING_EXPRESSION = "Particles.TDEmberHeat - 0.6 * Engine.DeltaTime"
SLOW_SECONDS_EXPRESSION = "length(Particles.Velocity) < 20.0 ? Particles.TDEmberSlowSeconds + Engine.DeltaTime : 0.0"
KILL_EXPRESSION = (
    "(Particles.TDEmberHeat <= 0.0 || Particles.TDEmberSlowSeconds >= 0.25) ? min(Particles.Age, Particles.Lifetime) : Particles.Lifetime"
)
SCALE_EXPRESSION = (
    "float3(User.EmberBaseScale * (1.0 + 0.004 * length(Particles.Velocity)), User.EmberBaseScale, User.EmberBaseScale)"
)
FADE_EXPRESSION = (
    "float4(saturate(Particles.TDEmberHeat), saturate(min(Particles.TDEmberHeat, Particles.Lifetime - Particles.Age) * 4.0), 0.0, 0.0)"
)

STACK_PLAN = (
    {"script": EMITTER_UPDATE_SCRIPT, "kind": "linked_module", "label": "spawn_rate", "module": SPAWN_RATE_MODULE,
     "input": SPAWN_RATE_INPUT, "variable": ("User.EmberSpawnRate", FLOAT_TYPE)},
    {"script": PARTICLE_SPAWN_SCRIPT, "kind": "set", "label": "spawn_random", "entries": (
        ("Particles.TDEmberSpawnUv", VECTOR2_TYPE, "random", (RANDOM_RANGE_VECTOR2D, (0.0, 0.0), (1.0, 1.0))),
        ("Particles.Lifetime", FLOAT_TYPE, "random", (RANDOM_RANGE_FLOAT, 1.2, 2.2)),
        ("Particles.TDEmberRejectRoll", FLOAT_TYPE, "random", (RANDOM_RANGE_FLOAT, 0.0, 1.0)),
    )},
    {"script": PARTICLE_SPAWN_SCRIPT, "kind": "set", "label": "spawn_place", "entries": (
        ("Particles.Position", POSITION_TYPE, "expression", SPAWN_POSITION_EXPRESSION),
        ("Particles.TDEmberHeat", FLOAT_TYPE, "value", 1.0),
        ("Particles.TDEmberSlowSeconds", FLOAT_TYPE, "value", 0.0),
        ("Particles.Scale", VECTOR3_TYPE, "expression", SPAWN_SCALE_EXPRESSION),
        ("Particles.DynamicMaterialParameter", VECTOR4_TYPE, "value", (1.0, 1.0, 0.0, 0.0)),
    )},
    {"script": PARTICLE_SPAWN_SCRIPT, "kind": "set", "label": "spawn_plane_uv", "entries": (
        ("Particles.TDFluidPlaneUv", VECTOR2_TYPE, "expression", PLANE_UV_EXPRESSION),
    )},
    {"script": PARTICLE_SPAWN_SCRIPT, "kind": "sample", "label": "spawn_density_sample", "texture": "User.DensityPressureTexture"},
    {"script": PARTICLE_SPAWN_SCRIPT, "kind": "set", "label": "spawn_reject", "entries": (
        ("Particles.Lifetime", FLOAT_TYPE, "expression", SPAWN_REJECT_EXPRESSION),
    )},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "set", "label": "update_plane_uv", "entries": (
        ("Particles.TDFluidPlaneUv", VECTOR2_TYPE, "expression", PLANE_UV_EXPRESSION),
    )},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "sample", "label": "update_velocity_sample", "texture": "User.VelocityTexture"},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "set", "label": "update_motion", "entries": (
        ("Particles.Velocity", VECTOR3_TYPE, "expression", VELOCITY_EXPRESSION),
        ("Particles.TDEmberHeat", FLOAT_TYPE, "expression", COOLING_EXPRESSION),
    )},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "set", "label": "update_slow", "entries": (
        ("Particles.TDEmberSlowSeconds", FLOAT_TYPE, "expression", SLOW_SECONDS_EXPRESSION),
    )},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "set", "label": "update_kill", "entries": (
        ("Particles.Lifetime", FLOAT_TYPE, "expression", KILL_EXPRESSION),
    )},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "set", "label": "update_shape", "entries": (
        ("Particles.Scale", VECTOR3_TYPE, "expression", SCALE_EXPRESSION),
        ("Particles.DynamicMaterialParameter", VECTOR4_TYPE, "expression", FADE_EXPRESSION),
    )},
    {"script": PARTICLE_UPDATE_SCRIPT, "kind": "module", "label": "solve_forces", "module": SOLVE_FORCES_MODULE},
)

SYSTEM_DATA = {"bDeterminism": True, "RandomSeed": SYSTEM_RANDOM_SEED, "bFixedTickDelta": False}
EMITTER_DATA = {
    "SimTarget": "GPUComputeSim",
    "bLocalSpace": False,
    "CalculateBoundsMode": "Fixed",
    "FixedBounds": {
        "Min": {"X": -FIXED_BOUNDS_HALF_EXTENT_CM, "Y": -FIXED_BOUNDS_HALF_EXTENT_CM, "Z": -FIXED_BOUNDS_HALF_EXTENT_CM},
        "Max": {"X": FIXED_BOUNDS_HALF_EXTENT_CM, "Y": FIXED_BOUNDS_HALF_EXTENT_CM, "Z": FIXED_BOUNDS_HALF_EXTENT_CM},
        "IsValid": 1,
    },
    "bDeterminism": True,
    "RandomSeed": EMITTER_RANDOM_SEED,
    "bRequiresPersistentIDs": True,
    "AttributesToPreserve": ["Particles.UniqueID"],
}

DROPPED_KEY_FRAGMENTS = ("guid", "changeid")
DROPPED_KEYS = ("registeredtypeindex",)
SORTED_LIST_SECTIONS = ("UserVariables", "SystemDependencies", "StackIssues")
SECTION_DUPLICATE_KEYS = {"SystemSummary": ("uservariables",)}
GUID_PATTERN = re.compile(r"[0-9A-Fa-f]{8}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{4}-?[0-9A-Fa-f]{12}")
FLOAT_DIGITS = 6
MAX_RENDERER_PROBE = 8


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
            response = uemcp.call_tool(self.session, name, arguments)
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
    components = ("x", "y", "z", "w")[: len(value)]
    return {name: float(component) for name, component in zip(components, value)}


def instanced_value(type_path, value):
    if type_path == TEXTURE_CLASS:
        return {"struct": reference(OBJECT_VALUE_STRUCT), "value": {"objectClass": reference(TEXTURE_CLASS), "object": reference("")}}
    return {"struct": reference(type_path), "value": plain_value(type_path, value)}


def default_for_type(type_path):
    return {FLOAT_TYPE: 0.0, VECTOR2_TYPE: (0.0, 0.0), VECTOR3_TYPE: (0.0, 0.0, 0.0), POSITION_TYPE: (0.0, 0.0, 0.0), VECTOR4_TYPE: (0.0, 0.0, 0.0, 0.0)}[type_path]


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


def resolve_input_name(topology, wanted):
    inputs = find_first(topology, "inputs") or []
    names = [str(field(item, "name")) for item in inputs if isinstance(item, dict) and field(item, "name") is not None]
    if wanted in names:
        return wanted
    matches = [name for name in names if name.split(".")[-1] == wanted.split(".")[-1]]
    if len(matches) != 1:
        raise TDNiagaraToolError(f"입력 '{wanted}' 1개 기대, 찾음 {matches} / 전체 {names}")
    return matches[0]


def require_module_name(topology, label):
    name = find_first(topology, "moduleName")
    if not name:
        raise TDNiagaraToolError(f"{label}: 모듈 이름을 받지 못함: {json.dumps(topology, ensure_ascii=False)[:500]}")
    return str(name)


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


def set_entry_input(client, system_path, script, module_name, topology, entry, sample_module_name):
    variable_name, type_path, kind, payload = entry
    if kind == "value":
        return
    input_name = resolve_input_name(topology, variable_name)
    base = stack_ref(system_path, EMITTER_NAME, script, module_name, inputs=(input_name,))
    if kind == "expression":
        if sample_module_name:
            payload = payload.replace(SAMPLED_COLOR, sampled_color_attribute(sample_module_name))
        client.call("SetStackInputData", stackInputRef=base, inputData=expression_input(payload))
        return
    asset, minimum, maximum = payload
    client.call("SetStackInputData", stackInputRef=base, inputData=dynamic_input(asset))
    for nested_name, nested_value in ((RANDOM_MINIMUM_INPUT, minimum), (RANDOM_MAXIMUM_INPUT, maximum)):
        nested = stack_ref(system_path, EMITTER_NAME, script, module_name, inputs=(input_name, nested_name))
        client.call("SetStackInputData", stackInputRef=nested, inputData=instanced_value(type_path, nested_value))


def add_set_parameters(client, system_path, step, sample_module_names):
    script = step["script"]
    parameters = [
        {"variable": {"name": name, "type": type_definition(type_path)},
         "defaultValue": instanced_value(type_path, payload if kind == "value" else default_for_type(type_path))}
        for name, type_path, kind, payload in step["entries"]
    ]
    topology = client.call("AddSetParametersModule", moduleLocationRef=stack_ref(system_path, EMITTER_NAME, script), parameters=parameters)
    module_name = require_module_name(topology, step["label"])
    for entry in step["entries"]:
        set_entry_input(client, system_path, script, module_name, topology, entry, sample_module_names.get(script, ""))
    return module_name


def add_module(client, system_path, step):
    topology = client.call("AddModule", moduleLocationRef=stack_ref(system_path, EMITTER_NAME, step["script"]), moduleAsset=reference(step["module"]))
    return require_module_name(topology, step["label"]), topology


def add_linked_module(client, system_path, step):
    module_name, topology = add_module(client, system_path, step)
    variable_name, type_path = step["variable"]
    input_name = resolve_input_name(topology, step["input"])
    client.call(
        "SetStackInputData",
        stackInputRef=stack_ref(system_path, EMITTER_NAME, step["script"], module_name, inputs=(input_name,)),
        inputData=linked_input(variable_name, type_path),
    )
    return module_name


def read_data_interface_values(client, input_ref):
    data = client.call("GetStackInputData", stackInputRef=input_ref)
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


def add_sample_texture(client, system_path, step, binding_report):
    module_name, topology = add_module(client, system_path, {"script": step["script"], "module": SAMPLE_TEXTURE_MODULE, "label": step["label"]})
    texture_input = resolve_input_name(topology, SAMPLE_TEXTURE_INPUT)
    uv_input = resolve_input_name(topology, SAMPLE_UV_INPUT)
    texture_ref = stack_ref(system_path, EMITTER_NAME, step["script"], module_name, inputs=(texture_input,))
    before = read_data_interface_values(client, texture_ref)
    client.call("SetStackInputData", stackInputRef=texture_ref, inputData=data_interface_input(write_binding_name(copy.deepcopy(before), step["texture"])))
    after = read_data_interface_values(client, texture_ref)
    stored = read_binding_name(after)
    binding_report.append({"step": step["label"], "requested": step["texture"], "stored": stored, "before": before, "after": after})
    if stored != step["texture"]:
        raise TDNiagaraToolError(f"{step['label']}: 텍스처 바인딩 왕복 불일치 {stored!r} ≠ {step['texture']!r}")
    client.call(
        "SetStackInputData",
        stackInputRef=stack_ref(system_path, EMITTER_NAME, step["script"], module_name, inputs=(uv_input,)),
        inputData=linked_input("Particles.TDFluidPlaneUv", VECTOR2_TYPE),
    )
    return module_name


def build_user_variables(ember_base_scale):
    variables = []
    for name, type_path, value in USER_VARIABLES:
        default = ember_base_scale if name == "User.EmberBaseScale" else value
        variables.append({"name": name, "type": type_definition(type_path), "description": "", "defaultValue": instanced_value(type_path, default)})
    return variables


def build_mesh_renderer_data(material_path):
    data = {"Meshes": [{"Mesh": SPHERE_MESH}], "FacingMode": "Velocity"}
    if material_path:
        data["bOverrideMaterials"] = True
        data["OverrideMaterials"] = [{"ExplicitMat": object_path(material_path)}]
    return data


def replace_mesh_renderer(client, system_path, material_path):
    client.call("RemoveRenderer", rendererToRemove=stack_ref(system_path, EMITTER_NAME, renderer=0))
    added = client.call("AddRenderer", newRendererLocation=stack_ref(system_path, EMITTER_NAME), rendererClass=reference(MESH_RENDERER_CLASS))
    index = find_first(added, "index")
    renderer_index = int(index) if isinstance(index, (int, float)) else 0
    client.call(
        "SetRendererData",
        renderer=stack_ref(system_path, EMITTER_NAME, renderer=renderer_index),
        rendererData={"propertyValues": json.dumps(build_mesh_renderer_data(material_path), ensure_ascii=False)},
    )
    return renderer_index


def list_emitter_names(client, system_path):
    summary = client.call("GetSystemSummary", system=reference(object_path(system_path)))
    return [str(field(item, "emitterName")) for item in as_list(summary, "emitters") if isinstance(item, dict)]


def remove_template_emitters(client, system_path):
    removed = [name for name in list_emitter_names(client, system_path) if name != EMITTER_NAME]
    for name in removed:
        client.call("RemoveEmitter", emitterToRemove=stack_ref(system_path, name))
    remaining = list_emitter_names(client, system_path)
    if any(name != EMITTER_NAME for name in remaining):
        raise TDNiagaraToolError(f"템플릿 이미터 제거 실패: 남음 {remaining}")
    return removed


def build_system(client, system_path, template, material_path, ember_base_scale, recorder, should_create=True):
    binding_report = []
    sample_module_names = {}
    if should_create:
        recorder.run("1 시스템 생성", lambda: create_system(client, system_path, template))
    removed_emitters = recorder.run("1 템플릿 이미터 제거", lambda: remove_template_emitters(client, system_path))
    recorder.run("1 시스템 데이터", lambda: client.call(
        "SetSystemData", system=reference(object_path(system_path)), systemData={"propertyValues": json.dumps(SYSTEM_DATA)}))
    recorder.run("2 이미터 추가", lambda: client.call(
        "AddEmitter", system=reference(object_path(system_path)), templateEmitter=reference(MINIMAL_EMITTER), emitterName=EMITTER_NAME))
    recorder.run("2 이미터 데이터", lambda: client.call(
        "SetEmitterData", emitter=stack_ref(system_path, EMITTER_NAME), emitterData={"propertyValues": json.dumps(EMITTER_DATA)}))
    topology = recorder.run("2 이미터 토폴로지", lambda: client.call("GetEmitterTopology", emitterRef=stack_ref(system_path, EMITTER_NAME)))
    recorder.run("3 User 변수 14", lambda: client.call(
        "AddUserVariables", system=reference(object_path(system_path)), variablesToAdd=build_user_variables(ember_base_scale)))
    builders = {
        "set": lambda step: add_set_parameters(client, system_path, step, sample_module_names),
        "module": lambda step: add_module(client, system_path, step)[0],
        "linked_module": lambda step: add_linked_module(client, system_path, step),
        "sample": lambda step: add_sample_texture(client, system_path, step, binding_report),
    }
    module_names = {}
    for step in STACK_PLAN:
        module_names[step["label"]] = recorder.run(f"4 {step['script']} {step['label']}", lambda step=step: builders[step["kind"]](step))
        if step["kind"] == "sample" and module_names[step["label"]]:
            sample_module_names[step["script"]] = module_names[step["label"]]
    recorder.run("5 메시 렌더러", lambda: replace_mesh_renderer(client, system_path, material_path))
    return {"template_topology": topology, "removed_template_emitters": removed_emitters, "module_names": module_names,
            "sampled_color_attributes": {script: sampled_color_attribute(name) for script, name in sample_module_names.items()},
            "binding": binding_report}


def round_float(value):
    if math.isnan(value) or math.isinf(value):
        return str(value)
    rounded = round(value, FLOAT_DIGITS)
    return 0.0 if rounded == 0 else rounded


def self_name_patterns(system_path):
    names = sorted({object_path(system_path), system_path, asset_name(system_path)}, key=len, reverse=True)
    return [re.compile(r"(?<![A-Za-z0-9_])" + re.escape(name) + r"(?![A-Za-z0-9_])") for name in names]


def normalize_text(text, patterns):
    parsed = None
    stripped = text.strip()
    if stripped[:1] in ("{", "["):
        try:
            parsed = json.loads(stripped)
        except json.JSONDecodeError:
            parsed = None
    if parsed is not None:
        return parsed, True
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


def collect_raw_sections(client, system_path):
    system = reference(object_path(system_path))
    emitter = stack_ref(system_path, EMITTER_NAME)
    prefix = f"Emitter.{EMITTER_NAME}."
    calls = (
        ("SystemSummary", "GetSystemSummary", {"system": system}),
        ("SystemData", "GetSystemData", {"system": system}),
        ("UserVariables", "GetUserVariables", {"system": system}),
        ("SystemDependencies", "GetSystemDependencies", {"system": system}),
        (prefix + "Summary", "GetEmitterSummary", {"emitterRef": emitter}),
        (prefix + "Topology", "GetEmitterTopology", {"emitterRef": emitter}),
        (prefix + "Data", "GetEmitterData", {"emitterRef": emitter}),
        (prefix + "InputValues", "GetEmitterInputValues", {"emitterRef": emitter}),
        ("CompileState", "GetSystemCompileState", {"system": system}),
        ("StackIssues", "GetStackIssues", {"system": system}),
    )
    sections = {}
    for section, tool, arguments in calls:
        sections[section] = call_for_section(client, tool, arguments)
    renderers = find_first(sections[prefix + "Topology"], "renderers")
    count = len(renderers) if isinstance(renderers, list) else MAX_RENDERER_PROBE
    for index in range(count):
        data = call_for_section(client, "GetRendererData", {"rendererRef": stack_ref(system_path, EMITTER_NAME, renderer=index)})
        if not isinstance(renderers, list) and isinstance(data, dict) and "error" in data:
            break
        sections[f"{prefix}Renderer.{index}"] = data
    return sections


def call_for_section(client, tool, arguments):
    try:
        return client.call(tool, **arguments)
    except TDNiagaraToolError as error:
        return {"error": str(error)}


def build_dump(raw_sections, system_path):
    sections = normalize_sections(raw_sections, system_path)
    hashes = compare_niagara_dumps.hash_sections(sections)
    return {
        "asset": system_path,
        "tool": "editor_make_fluid_ember_niagara",
        "sections": sections,
        "sha256": hashes,
        "sha256_all": compare_niagara_dumps.hash_all_sections(hashes),
    }


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=True) + "\n", encoding="utf-8")


def dump_system(client, system_path):
    dump = build_dump(collect_raw_sections(client, system_path), system_path)
    path = OUTPUT_DIR / f"{asset_name(system_path)}.dump.json"
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


def count_error_issues(stack_issues):
    if not isinstance(stack_issues, (dict, list)) or field(stack_issues, "error") is not None:
        return -1
    issues = as_list(stack_issues, "issues")
    counted = sum(1 for item in issues if isinstance(item, dict) and str(field(item, "severity")).split("::")[-1] == "Error")
    declared = find_first(stack_issues, "numErrors")
    return max(counted, int(declared)) if isinstance(declared, (int, float)) else counted


def verify_system(dump, raw_emitter_data, raw_system_data, binding_report, material_path):
    sections = dump["sections"]
    statuses = collect_compile_statuses(sections.get("CompileState"))
    user_names = [str(field(item, "name")) for item in as_list(sections.get("UserVariables"), "userVariables") if isinstance(item, dict)]
    renderer_texts = [json.dumps(value) for key, value in sections.items() if ".Renderer." in key]
    emitter_names = [str(field(item, "emitterName")) for item in as_list(sections.get("SystemSummary"), "emitters") if isinstance(item, dict)]
    expected_renderer_paths = [SPHERE_MESH] + ([object_path(material_path)] if material_path else [])
    checks = {
        "이미터 = Ember 하나(템플릿 이미터 제거)": emitter_names == [EMITTER_NAME],
        "이미터 데이터 재읽기 = 요청(GPU·월드·고정 ±600·결정론·시드·영속 ID·UniqueID 보존)": values_match(read_property_values(raw_emitter_data), EMITTER_DATA),
        "시스템 데이터 재읽기 = 요청(결정론·시드·bFixedTickDelta false)": values_match(read_property_values(raw_system_data), SYSTEM_DATA),
        "GPU 계산 스크립트 UpToDate": statuses.get(f"{EMITTER_NAME}.{GPU_COMPUTE_SCRIPT}") in UP_TO_DATE_STATUSES,
        "컴파일 오류 없음": find_first(sections.get("CompileState"), "hasErrors") is False,
        "스택 오류 0": count_error_issues(sections.get("StackIssues")) == 0,
        "User 변수 ≥ 12": len(user_names) >= 12,
        "SampleTexture 바인딩 왕복 2개": is_binding_roundtrip_ok(binding_report),
        "메시 렌더러 Sphere·재질": any(all(path in text for path in expected_renderer_paths) for text in renderer_texts),
    }
    return [{"check": name, "passed": bool(passed)} for name, passed in checks.items()], statuses


def read_probe():
    if not PROBE_PATH.is_file():
        return {}
    return json.loads(PROBE_PATH.read_text(encoding="utf-8"))


def require_stage0b():
    stage = read_probe().get("stage0b") or {}
    if "template_system" not in stage:
        raise TDPreconditionError("단계 0b 결과 없음: --probe --asset <새 경로> 를 먼저 실행")
    if stage.get("failed_steps") or not stage.get("binding_roundtrip_ok"):
        raise TDPreconditionError(f"단계 0b 미통과(failed_steps={stage.get('failed_steps')}, binding_roundtrip_ok={stage.get('binding_roundtrip_ok')}): 상수를 고치고 새 경로로 0b 재실행, 바인딩만 실패면 방식 C")
    return stage["template_system"]


def validate_asset_path(path):
    if not path.startswith(ALLOWED_ASSET_ROOT) or path.endswith("/") or "." in asset_name(path):
        raise TDPreconditionError(f"에셋 경로는 {ALLOWED_ASSET_ROOT} 아래 패키지 경로여야 함: {path}")


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


def run_stage0a(client):
    description = client.describe_toolset(client.system_toolset)
    tools = description.get("tools", []) if isinstance(description, dict) else []
    tool_names = sorted(str(tool.get("name", "")).rsplit(".", 1)[-1] for tool in tools)
    create_tool = next((tool for tool in tools if str(tool.get("name", "")).endswith("CreateNiagaraSystem")), {})
    results = {}
    for label, asset in (("module_sample_texture", SAMPLE_TEXTURE_MODULE), ("module_spawn_rate", SPAWN_RATE_MODULE),
                         ("module_solve_forces", SOLVE_FORCES_MODULE), ("module_initialize_particle", INITIALIZE_PARTICLE_MODULE)):
        probe_call(client, label, "GetModuleSchemaFromAsset", {"moduleAsset": reference(asset)}, results)
    for label, asset in (("dynamic_random_vector2d", RANDOM_RANGE_VECTOR2D), ("dynamic_random_float", RANDOM_RANGE_FLOAT)):
        probe_call(client, label, "GetDynamicInputSchemaFromAsset", {"dynamicInputAsset": reference(asset)}, results)
    probe_call(client, "data_interface_texture", "GetDataInterfaceSchema", {"dataInterfaceClass": reference(TEXTURE_DATA_INTERFACE_CLASS)}, results)
    probe_call(client, "emitter_schema", "GetEmitterSchema", {}, results)
    probe_call(client, "system_schema", "GetSystemSchema", {}, results)
    probe_call(client, "mesh_renderer_schema", "GetRendererSchema", {"rendererClass": reference(MESH_RENDERER_CLASS)}, results)
    probe_call(client, "dynamic_inputs_vector2", "GetAvailableDynamicInputs", {"type": type_definition(VECTOR2_TYPE)}, results)
    return {
        "toolsets": client.toolset_names,
        "system_toolset": client.system_toolset,
        "system_tools": tool_names,
        "save_assets_tool": find_save_tool(client),
        "create_template_required": "templateSystem" in (create_tool.get("inputSchema", {}).get("required") or []),
        "editor_remote_exec": read_marker(run_editor_code("print('TD_PING', 1)\n"), "TD_PING") == "1",
        "ember_material_exists": editor_asset_exists(EMBER_MATERIAL_ASSET),
        "calls": results,
        "failed_calls": sorted(label for label, item in results.items() if not item["ok"]),
    }


def is_binding_roundtrip_ok(binding_report):
    return len(binding_report) == 2 and all(item["stored"] == item["requested"] for item in binding_report)


def run_stage0b(client, probe_asset, ember_base_scale):
    if editor_asset_exists(probe_asset):
        raise TDPreconditionError(f"0b 경로에 에셋이 이미 있음(덮어쓰기 안 함): {probe_asset}")
    template = EMPTY_TEMPLATE_SYSTEM
    empty_template_error = ""
    try:
        create_system(client, probe_asset, EMPTY_TEMPLATE_SYSTEM)
    except TDNiagaraToolError as error:
        empty_template_error = str(error)
        template = DEFAULT_TEMPLATE_SYSTEM
        if editor_asset_exists(probe_asset):
            raise TDPreconditionError(f"빈 템플릿 실패 뒤 에셋이 남음, 새 경로로 다시: {probe_asset}")
    recorder = TDStepRecorder(strict=False)
    material = EMBER_MATERIAL_ASSET if editor_asset_exists(EMBER_MATERIAL_ASSET) else ""
    built = build_system(client, probe_asset, template, material, ember_base_scale, recorder, should_create=bool(empty_template_error))
    recorder.run("6 저장", lambda: editor_save_asset(probe_asset))
    emitter_data = call_for_section(client, "GetEmitterData", {"emitterRef": stack_ref(probe_asset, EMITTER_NAME)})
    system_data = call_for_section(client, "GetSystemData", {"system": reference(object_path(probe_asset))})
    dump = dump_system(client, probe_asset)
    checks, statuses = verify_system(dump, emitter_data, system_data, built["binding"], material)
    return {
        "asset": probe_asset,
        "template_system": template,
        "empty_template_error": empty_template_error,
        "material_used": material,
        "steps": recorder.steps,
        "failed_steps": recorder.failed_steps(),
        "binding_roundtrip_ok": is_binding_roundtrip_ok(built["binding"]),
        "checks": checks,
        "compile_statuses": statuses,
        "built": built,
        "sha256": dump["sha256"],
    }


def run_probe(client, probe_asset, ember_base_scale):
    probe = read_probe()
    probe["stage0a"] = run_stage0a(client)
    write_json(PROBE_PATH, probe)
    if probe_asset:
        probe["stage0b"] = run_stage0b(client, probe_asset, ember_base_scale)
        write_json(PROBE_PATH, probe)
    print("프로브: " + str(PROBE_PATH))
    stage0b = probe.get("stage0b") if probe_asset else None
    passed = not probe["stage0a"]["failed_calls"] and (stage0b is None or (not stage0b["failed_steps"] and stage0b["binding_roundtrip_ok"]))
    print(json.dumps({"stage0a_failed_calls": probe["stage0a"]["failed_calls"],
                      "stage0b_failed_steps": stage0b["failed_steps"] if stage0b else None,
                      "binding_roundtrip_ok": stage0b["binding_roundtrip_ok"] if stage0b else None}, ensure_ascii=False))
    return 0 if passed else 1


def run_create(client, system_path, ember_base_scale):
    template = require_stage0b()
    if editor_asset_exists(system_path):
        raise TDPreconditionError(f"에셋이 이미 있음(덮어쓰기 안 함, 새 경로 사용): {system_path}")
    if not editor_asset_exists(EMBER_MATERIAL_ASSET):
        raise TDPreconditionError(f"재질 없음: {EMBER_MATERIAL_ASSET}(editor_make_fluid_flame_material.py only 요청 먼저)")
    recorder = TDStepRecorder(strict=True)
    report = {"asset": system_path, "template_system": template, "ember_base_scale": ember_base_scale}
    try:
        built = build_system(client, system_path, template, EMBER_MATERIAL_ASSET, ember_base_scale, recorder)
        recorder.run("6 저장", lambda: editor_save_asset(system_path))
        emitter_data = recorder.run("7 이미터 데이터 재읽기", lambda: client.call("GetEmitterData", emitterRef=stack_ref(system_path, EMITTER_NAME)))
        system_data = recorder.run("7 시스템 데이터 재읽기", lambda: client.call("GetSystemData", system=reference(object_path(system_path))))
        dump = dump_system(client, system_path)
        checks, statuses = verify_system(dump, emitter_data, system_data, built["binding"], EMBER_MATERIAL_ASSET)
        report.update({"built": built, "checks": checks, "compile_statuses": statuses, "sha256": dump["sha256"], "sha256_all": dump["sha256_all"]})
    finally:
        report["steps"] = recorder.steps
        write_json(OUTPUT_DIR / f"{asset_name(system_path)}.report.json", report)
    for check in report["checks"]:
        print(("PASS " if check["passed"] else "FAIL ") + check["check"])
    return 0 if all(check["passed"] for check in report["checks"]) else 1


def parse_arguments(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", action="store_true")
    parser.add_argument("--dump-only", action="store_true")
    parser.add_argument("--asset", default="")
    parser.add_argument("--ember-base-scale", type=float, default=DEFAULT_EMBER_BASE_SCALE)
    arguments = parser.parse_args(argv)
    if arguments.probe and arguments.dump_only:
        parser.error("--probe 와 --dump-only 는 함께 쓸 수 없음")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    system_path = arguments.asset or ("" if arguments.probe else DEFAULT_ASSET_PATH)
    try:
        if system_path:
            validate_asset_path(system_path)
        client = TDNiagaraToolsetClient()
        if arguments.probe:
            return run_probe(client, system_path, arguments.ember_base_scale)
        if arguments.dump_only:
            dump_system(client, system_path)
            return 0
        return run_create(client, system_path, arguments.ember_base_scale)
    except TDEditorConnectionError as error:
        print("연결 실패: " + str(error))
        return 2
    except TDPreconditionError as error:
        print("선행 조건: " + str(error))
        return 3
    except TDNiagaraToolError as error:
        print("실패: " + str(error))
        return 1


if __name__ == "__main__":
    sys.exit(main())

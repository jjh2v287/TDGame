"""실행 환경: TDGame 언리얼 에디터 Python(PIE 아님, 미저장 작업 없음). 무대 맵 5개(STAGE_MAP_PATHS)를 차례로 열어 액터 값을 읽으므로 현재 맵이 바뀌고, 끝나면 원래 맵으로 되돌린다. 맵 안 DA 는 그 맵이 열려 있을 때 값을 다 읽고 UObject 를 붙잡지 않는다(L-editor-30)
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_dump_fluid_preset_builders.py
출력: Saved/FluidFX/preset_builder_dump.json·preset_tuning_generated.json(앞 두 개는 기존 형식 그대로), preset_stage_values.json(presets=/Game/FluidFX 전체 DA, fire_stage_actors=기존 형식, 새 키 referenced_presets·stage_maps), preset_copy_mismatches.json(R2 로 무시되기 시작할 사본의 '빌더 값 ≠ 사본' 필드와 같은 종류 충돌, spec-wave0-data-bridge-v2 2.6. 새 구조체의 빌더 값은 TDFluidPresetAsset.h 에 반환 형식으로 선언된 Build*ForPresetKind, 없으면 헤더 기본값, summary.builder_sources 에 출처)
상태: 기존 2개 출력은 검증됨(2026-10-22 FX-8c 프리셋 데이터: 변경 전 바이너리에서 두 번 실행해 세 파일 바이트 동일·생성 JSON = 정본, 새 바이너리에서 빌더 덤프 = 변경 전 golden 비트 동일), 확장분(2026-10-25 T0)과 파동 2 고정표(GROUP_TABLE 7그룹·TDFluidVolumeDriverTypes.h)는 2026-10-27 에디터 1회 실행(종류 41, 기존 22종류×5구조체 2904값 중 파동 2 차이 0, 생성 조절 JSON 은 Globals 없는 옛 부분집합)
"""
import decimal
import importlib.util
import json
import os
import re
import struct

TUNED_STRUCT_BUILDERS = (
    ("TDFluidSimulationConfig", "build_config_for_preset_kind"),
    ("TDFluidSourceEmitterSettings", "build_source_emitter_for_preset_kind"),
    ("TDFluidFragmentTrailSettings", "build_fragment_trail_for_preset_kind"),
    ("TDFluidVolumeRenderSettings", "build_volume_render_for_preset_kind"),
    ("TDFluidFlameCardLayerSettings", "build_flame_card_layers_for_preset_kind"),
)
GROUP_TABLE = (
    ("FireFamily", ("Torch", "Campfire", "CampfireSmoke", "Explosion", "ExplosionSmoke", "ExplosionGroundDust", "Candle", "Fireball", "PlasmaExplosion", "SteamPuff", "Vortex", "InvertedVortex", "PortalVortex", "VortexHeterogeneous", "Flamethrower")),
    ("WaterLayer", ("River", "Sea")),
    ("VortexTrio", ("Vortex", "InvertedVortex", "PortalVortex", "VortexHeterogeneous")),
    ("ExplosionPair", ("Explosion", "PlasmaExplosion")),
    ("HeterogeneousStage", ("SmokeHeterogeneous", "VortexHeterogeneous", "SmokeCameraFacing", "FloorSmokeCapture")),
    ("TrackPainter", ("SnowTrack", "SandTrack")),
    ("TrackDust", ("SnowDust", "SandDust")),
)
COMPONENT_TYPES = {
    "FVector2D": (("X", "Y"), "double"),
    "FVector": (("X", "Y", "Z"), "double"),
    "FLinearColor": (("R", "G", "B", "A"), "float"),
}
OUTPUT_DIRECTORY = ("Saved", "FluidFX")
BUILDER_DUMP_NAME = "preset_builder_dump.json"
GENERATED_TUNING_NAME = "preset_tuning_generated.json"
STAGE_VALUES_NAME = "preset_stage_values.json"
COPY_MISMATCH_NAME = "preset_copy_mismatches.json"
FIRE_STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
STAGE_MAP_PATHS = (
    "/Game/FluidFX/Maps/LV_TDFluidCoastStage",
    FIRE_STAGE_MAP_PATH,
    "/Game/FluidFX/Maps/LV_TDFluidSmokeStage",
    "/Game/FluidFX/Maps/LV_TDFluidTestArena",
    "/Game/FluidFX/Maps/LV_TDFluidWaterStage",
)
FLUID_ROOT = "/Game/FluidFX"
PRESET_ASSET_CLASS_NAME = "TDFluidPresetAsset"
ACTOR_STRUCT_PROPERTIES = ("source_emitter", "flame_card_layers", "trail_settings")
ACTOR_EXTRA_STRUCT_PROPERTIES = ("downstream_feed", "glow_settings")
LEGACY_ACTOR_KEYS = ("flame_card_layers", "live_active_preset", "live_simulation_config", "source_emitter", "trail_settings")
PRESET_COPY_PROPERTIES = (
    ("simulation_config", "TDFluidSimulationConfig", True),
    ("volume_render", "TDFluidVolumeRenderSettings", False),
    ("plane_settings", "TDFluidPlaneSettings", False),
)
ACTOR_COPY_PROPERTIES = (
    ("source_emitter", "TDFluidSourceEmitterSettings", False),
    ("flame_card_layers", "TDFluidFlameCardLayerSettings", False),
    ("downstream_feed", "TDFluidDownstreamFeedSettings", False),
    ("trail_settings", "TDFluidFragmentTrailSettings", True),
    ("glow_settings", "TDFluidFragmentGlowSettings", True),
)
COMPONENT_COPY_PROPERTIES = (
    ("ember_component", "TDFluidEmberComponent"),
    ("fire_light_component", "TDFluidFireLightComponent"),
)
COMPONENT_COPY_NAMES = tuple(component_name for _, component_name in COMPONENT_COPY_PROPERTIES)
COPY_ONLY_NAMES = ("TDFluidPlaneSettings", "TDFluidDownstreamFeedSettings", "TDFluidFragmentGlowSettings") + COMPONENT_COPY_NAMES
EXTRA_BUILDER_RETURN_TYPES = {
    "TDFluidPlaneSettings": "TDFluidPlaneSettings",
    "TDFluidDownstreamFeedSettings": "TDFluidDownstreamFeedSettings",
    "TDFluidFragmentGlowSettings": "TDFluidFragmentGlowSettings",
    "TDFluidEmberComponent": "TDFluidEmberSettings",
    "TDFluidFireLightComponent": "TDFluidFireLightSettings",
}
PRESET_ASSET_HEADER_PATH = ("Source", "TDGame", "FluidFX", "TDFluidPresetAsset.h")
BUILDER_DECLARATION_PATTERN = re.compile(r"UFUNCTION\((?:[^()]|\([^()]*\))*\)\s*static\s+F(\w+)\s+(Build\w+ForPresetKind)\s*\(\s*ETDFluidPresetKind\b")
COPY_HEADER_RELATIVE_PATHS = (
    ("Source", "TDGame", "FluidFX", "TDFluidTypes.h"),
    ("Source", "TDGame", "FluidFX", "TDFluidFireTypes.h"),
    ("Source", "TDGame", "FluidFX", "TDFluidPlaneTypes.h"),
    ("Source", "TDGame", "FluidFX", "TDFluidEmberComponent.h"),
    ("Source", "TDGame", "FluidFX", "TDFluidFireLightComponent.h"),
    ("Source", "TDGame", "FluidFX", "TDFluidVolumeDriverTypes.h"),
)
SUPPORTED_SCALAR_TYPES = ("bool", "int32", "float", "double")
ENUM_PATTERN = re.compile(r"UENUM\([^)]*\)\s*enum\s+class\s+(\w+)\s*:\s*\w+\s*\{(.*?)\};", re.DOTALL)
COPY_PROPERTY_PATTERN = re.compile(r"UPROPERTY\((?:[^()]|\([^()]*\))*\)\s*\n\s*([\w:<>]+)\s+(\w+)\s*(?:=[^;]+)?;")
CUSTOM_PRESET_STATE = {"path": None, "kind": "Custom", "override": False}
INDENT = "  "


def to_float32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def fixed_point(text):
    result = format(decimal.Decimal(text), "f")
    return result if "." in result else result + ".0"


def format_float32(value):
    target = struct.pack("<f", value)
    for precision in range(1, 10):
        text = "%.*g" % (precision, value)
        if struct.pack("<f", float(text)) == target:
            return fixed_point(text)
    raise ValueError(f"float32 왕복 표기 실패: {value!r}")


def format_double(value):
    return fixed_point(repr(float(value)))


def format_scalar(field_type, value):
    if field_type == "bool":
        return "true" if value else "false"
    if field_type == "int32":
        return str(int(value))
    if field_type == "float":
        return format_float32(value)
    if field_type == "double":
        return format_double(value)
    return json.dumps(value)


def format_object(entries, depth):
    if not entries:
        return "{}"
    inner = INDENT * (depth + 1)
    lines = [f"{inner}{json.dumps(key)}: {text}" for key, text in entries]
    return "{\n" + ",\n".join(lines) + "\n" + INDENT * depth + "}"


def format_field_value(field, value, depth):
    if field["type"] in COMPONENT_TYPES:
        component_names, component_type = COMPONENT_TYPES[field["type"]]
        return format_object([(name, format_scalar(component_type, value[name])) for name in component_names], depth)
    return format_scalar(field["type"], value)


def format_struct_entries(schema, entries, depth):
    formatted = []
    for struct_name, fields in entries.items():
        declared = {field["name"]: field for field in schema.fields(struct_name)}
        formatted.append((struct_name, format_object([(name, format_field_value(declared[name], value, depth + 2)) for name, value in fields.items()], depth + 1)))
    return formatted


def format_kind_list(kinds, depth):
    inner = INDENT * (depth + 1)
    return "[\n" + ",\n".join(inner + json.dumps(kind) for kind in kinds) + "\n" + INDENT * depth + "]"


def format_tuning_document(schema, document):
    groups = []
    for group_name, group in document["Groups"].items():
        entries = [("Kinds", format_kind_list(group["Kinds"], 3))]
        entries.extend(format_struct_entries(schema, {key: value for key, value in group.items() if key != "Kinds"}, 2))
        groups.append((group_name, format_object(entries, 2)))
    top = [
        ("StructDefaults", format_object(format_struct_entries(schema, document["StructDefaults"], 1), 1)),
        ("Groups", format_object(groups, 1)),
        ("Kinds", format_object([(kind, format_object(format_struct_entries(schema, entries, 2), 2)) for kind, entries in document["Kinds"].items()], 1)),
    ]
    return format_object(top, 0) + "\n"


def build_tuning_document(schema, defaults, values_by_kind):
    kinds = schema.kinds()
    groups = {group_name: {"Kinds": [kind for kind in kinds if kind in members]} for group_name, members in GROUP_TABLE}
    kind_entries = {kind: {} for kind in kinds}
    for struct_name, _ in TUNED_STRUCT_BUILDERS:
        for field in schema.fields(struct_name):
            field_name = field["name"]
            default = defaults[struct_name][field_name]
            assigned = set()
            for group_name, members in GROUP_TABLE:
                if assigned.intersection(members):
                    continue
                values = [values_by_kind[kind][struct_name][field_name] for kind in members]
                if values[0] != default and all(value == values[0] for value in values):
                    groups[group_name].setdefault(struct_name, {})[field_name] = values[0]
                    assigned.update(members)
            for kind in kinds:
                value = values_by_kind[kind][struct_name][field_name]
                if kind not in assigned and value != default:
                    kind_entries[kind].setdefault(struct_name, {})[field_name] = value
    return {
        "StructDefaults": {struct_name: dict(defaults[struct_name]) for struct_name, _ in TUNED_STRUCT_BUILDERS},
        "Groups": groups,
        "Kinds": {kind: entries for kind, entries in kind_entries.items() if entries},
    }


def hex_value(field_type, value):
    if field_type in COMPONENT_TYPES:
        component_names, component_type = COMPONENT_TYPES[field_type]
        return {name: hex_value(component_type, value[name]) for name in component_names}
    if field_type in ("float", "double"):
        return float(value).hex()
    return value


def format_builder_dump(schema, values_by_kind):
    dump = {}
    for kind in schema.kinds():
        dump[kind] = {}
        for struct_name, _ in TUNED_STRUCT_BUILDERS:
            dump[kind][struct_name] = {field["name"]: hex_value(field["type"], values_by_kind[kind][struct_name][field["name"]]) for field in schema.fields(struct_name)}
    return json.dumps(dump, sort_keys=True, indent=1) + "\n"


def normalize_cpp_name(name, is_bool):
    if is_bool and len(name) > 1 and name[0] == "b" and name[1].isupper():
        name = name[1:]
    return name.replace("_", "").upper()


def normalize_python_name(name):
    return name.replace("_", "").upper()


def match_python_name(cpp_name, is_bool, candidates, owner):
    wanted = normalize_cpp_name(cpp_name, is_bool)
    matches = [candidate for candidate in candidates if normalize_python_name(candidate) == wanted]
    if len(matches) != 1:
        raise RuntimeError(f"Python 이름 대응 실패: {owner}.{cpp_name} 후보 {matches}")
    return matches[0]


def public_names(owner):
    return [name for name in dir(owner) if not name.startswith("_")]


class UnrealSchemaBinding:
    def __init__(self, unreal, schema, struct_names):
        self.unreal = unreal
        self.schema = schema
        self.enum_values = {}
        for enum_name, cpp_names in schema.enums.items():
            enum_type = getattr(unreal, enum_name[1:], None)
            if enum_type is None:
                continue
            candidates = public_names(enum_type)
            self.enum_values[enum_name] = [(cpp_name, getattr(enum_type, match_python_name(cpp_name, False, candidates, enum_name))) for cpp_name in cpp_names]
        self.field_names = {}
        for struct_name in struct_names:
            candidates = public_names(getattr(unreal, struct_name))
            self.field_names[struct_name] = {field["name"]: match_python_name(field["name"], field["type"] == "bool", candidates, struct_name) for field in schema.fields(struct_name)}

    def kind_value(self, kind_name):
        return dict(self.enum_values["ETDFluidPresetKind"])[kind_name]

    def enum_name(self, enum_name, value):
        for cpp_name, unreal_value in self.enum_values[enum_name]:
            if unreal_value == value:
                return cpp_name
        raise RuntimeError(f"{enum_name} 값 대응 실패: {value!r}")

    def neutral_value(self, field_type, value):
        if field_type == "bool":
            return bool(value)
        if field_type == "int32":
            return int(value)
        if field_type == "float":
            return to_float32(float(value))
        if field_type == "double":
            return float(value)
        if field_type in COMPONENT_TYPES:
            component_names, component_type = COMPONENT_TYPES[field_type]
            return {name: self.neutral_value(component_type, getattr(value, name.lower())) for name in component_names}
        return self.enum_name(field_type, value)

    def neutral_struct(self, struct_name, struct_value):
        return {field["name"]: self.neutral_value(field["type"], struct_value.get_editor_property(self.field_names[struct_name][field["name"]])) for field in self.schema.fields(struct_name)}


def load_checker(project_directory):
    checker_path = os.path.join(project_directory, "Tools", "check_fluid_preset_data.py")
    module_spec = importlib.util.spec_from_file_location("check_fluid_preset_data", checker_path)
    if module_spec is None or module_spec.loader is None:
        raise RuntimeError(f"프리셋 정본 검사기 없음: {checker_path}")
    checker = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(checker)
    return checker


def collect_builder_values(unreal, binding):
    preset_asset = unreal.TDFluidPresetAsset
    values_by_kind = {}
    for kind in binding.schema.kinds():
        kind_value = binding.kind_value(kind)
        values_by_kind[kind] = {struct_name: binding.neutral_struct(struct_name, getattr(preset_asset, builder_name)(kind_value)) for struct_name, builder_name in TUNED_STRUCT_BUILDERS}
    return values_by_kind


def collect_struct_defaults(unreal, binding):
    return {struct_name: binding.neutral_struct(struct_name, getattr(unreal, struct_name)()) for struct_name, _ in TUNED_STRUCT_BUILDERS}


def read_editor_property(owner, property_name):
    try:
        return True, owner.get_editor_property(property_name)
    except Exception:
        return False, None


def parse_enumerator_names(body):
    names = []
    for entry in body.split(","):
        entry = re.sub(r"UMETA\(.*?\)", "", entry).strip()
        if entry:
            names.append(re.match(r"\w+", entry).group(0))
    return names


def read_copy_header_text(project_directory):
    parts = []
    for relative_path in COPY_HEADER_RELATIVE_PATHS:
        header_path = os.path.join(project_directory, *relative_path)
        if not os.path.isfile(header_path):
            raise RuntimeError(f"헤더 없음: {header_path}")
        with open(header_path, encoding="utf-8") as header_file:
            parts.append(header_file.read().replace("\r\n", "\n"))
    return "\n".join(parts)


def parse_copy_fields(header_text, copy_name, known_enums):
    declaration = re.search(r"(?:struct|class)\s+(?:\w+_API\s+)?[FU]" + copy_name + r"\b[^{;]*\{(.*?)\n\};", header_text, re.DOTALL)
    if declaration is None:
        raise RuntimeError(f"헤더에서 선언을 찾지 못함: {copy_name}")
    fields = []
    for type_name, field_name in COPY_PROPERTY_PATTERN.findall(declaration.group(1)):
        if type_name in SUPPORTED_SCALAR_TYPES or type_name in COMPONENT_TYPES or type_name in known_enums:
            fields.append({"name": field_name, "type": type_name})
    return fields


class CopySchema:
    def __init__(self, base_schema, extra_fields, extra_enums):
        self.base_schema = base_schema
        self.extra_fields = extra_fields
        self.enums = {**base_schema.enums, **extra_enums}

    def kinds(self):
        return self.base_schema.kinds()

    def fields(self, struct_name):
        if struct_name in self.extra_fields:
            return self.extra_fields[struct_name]
        return self.base_schema.fields(struct_name)


def build_copy_schema(project_directory, base_schema):
    header_text = read_copy_header_text(project_directory)
    parsed_enums = {name: parse_enumerator_names(body) for name, body in ENUM_PATTERN.findall(header_text)}
    known_enums = {**parsed_enums, **base_schema.enums}
    extra_fields = {copy_name: parse_copy_fields(header_text, copy_name, known_enums) for copy_name in COPY_ONLY_NAMES}
    used_types = {field["type"] for fields in extra_fields.values() for field in fields}
    extra_enums = {name: names for name, names in parsed_enums.items() if name in used_types and name not in base_schema.enums}
    return CopySchema(base_schema, extra_fields, extra_enums)


def read_default_extra_value(unreal, binding, copy_name):
    copy_type = getattr(unreal, copy_name)
    source = unreal.get_default_object(copy_type) if copy_name in COMPONENT_COPY_NAMES else copy_type()
    return binding.neutral_struct(copy_name, source)


def to_python_function_name(cpp_name):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", cpp_name).lower()


def read_declared_builders(project_directory):
    header_path = os.path.join(project_directory, *PRESET_ASSET_HEADER_PATH)
    if not os.path.isfile(header_path):
        raise RuntimeError(f"헤더 없음: {header_path}")
    with open(header_path, encoding="utf-8") as header_file:
        header_text = header_file.read()
    return {return_type: to_python_function_name(function_name) for return_type, function_name in BUILDER_DECLARATION_PATTERN.findall(header_text)}


def find_extra_builder_name(preset_asset, declared_builders, copy_name):
    builder_name = declared_builders.get(EXTRA_BUILDER_RETURN_TYPES[copy_name])
    if builder_name is None:
        return None
    if not hasattr(preset_asset, builder_name):
        raise RuntimeError(f"헤더에 선언된 빌더가 에디터 파이썬에 없음(에디터 바이너리가 헤더보다 오래됨): TDFluidPresetAsset.{builder_name}")
    return builder_name


def collect_extra_builder_values(unreal, binding, declared_builders):
    kinds = binding.schema.kinds()
    values_by_kind = {kind: {} for kind in kinds}
    sources = {}
    preset_asset = unreal.TDFluidPresetAsset
    for copy_name in COPY_ONLY_NAMES:
        builder_name = find_extra_builder_name(preset_asset, declared_builders, copy_name)
        sources[copy_name] = builder_name or "default_object"
        default_value = None if builder_name else read_default_extra_value(unreal, binding, copy_name)
        for kind in kinds:
            if builder_name:
                values_by_kind[kind][copy_name] = binding.neutral_struct(copy_name, getattr(preset_asset, builder_name)(binding.kind_value(kind)))
            else:
                values_by_kind[kind][copy_name] = default_value
    return values_by_kind, sources


def jsonable_value(field_type, value):
    if field_type in COMPONENT_TYPES:
        component_names, component_type = COMPONENT_TYPES[field_type]
        return {name: jsonable_value(component_type, value[name]) for name in component_names}
    if field_type == "float":
        return float(format_float32(value))
    return value


def jsonable_struct(schema, struct_name, values):
    types = {field["name"]: field["type"] for field in schema.fields(struct_name)}
    return {name: jsonable_value(types[name], value) for name, value in values.items()}


def canonical_text(value):
    return json.dumps(value, sort_keys=True)


class PresetDirectory:
    def __init__(self, binding, collector):
        self.binding = binding
        self.collector = collector
        self.states = {}
        self.exports = {}

    def describe(self, preset):
        if preset is None:
            return None
        path = preset.get_path_name()
        if path not in self.states:
            state = {
                "path": path,
                "kind": self.binding.enum_name("ETDFluidPresetKind", preset.get_editor_property("preset_kind")),
                "override": bool(preset.get_editor_property("override_preset_defaults")),
            }
            self.states[path] = state
            self.exports[path] = export_preset(preset, state)
            add_preset_copy_records(self.binding, self.collector, preset, state)
        return self.states[path]


class CopyRecordCollector:
    def __init__(self):
        self.records = []
        self.unresolved = []

    def add(self, owner, struct_name, property_name, values, preset_state, is_ignored_today):
        if preset_state is None:
            self.unresolved.append({"owner": owner, "property": property_name, "struct": struct_name})
            return
        self.records.append({
            "owner": owner,
            "property": property_name,
            "struct": struct_name,
            "kind": preset_state["kind"],
            "override": preset_state["override"],
            "is_ignored_today": is_ignored_today,
            "values": values,
        })


def list_preset_assets(unreal):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    presets = []
    for data in registry.get_assets_by_path(FLUID_ROOT, recursive=True):
        if str(data.asset_class_path.asset_name) != PRESET_ASSET_CLASS_NAME:
            continue
        preset = unreal.EditorAssetLibrary.load_asset(str(data.package_name))
        if isinstance(preset, unreal.TDFluidPresetAsset):
            presets.append(preset)
    return sorted(presets, key=lambda preset: preset.get_path_name())


def export_preset(preset, state):
    return {
        "kind": state["kind"],
        "override": state["override"],
        "simulation_config": preset.get_editor_property("simulation_config").export_text(),
        "volume_render": preset.get_editor_property("volume_render").export_text(),
        "plane_settings": preset.get_editor_property("plane_settings").export_text(),
        "sort": int(preset.get_editor_property("translucency_sort_priority")),
    }


def add_preset_copy_records(binding, collector, preset, state):
    for property_name, struct_name, is_ignored_today in PRESET_COPY_PROPERTIES:
        values = binding.neutral_struct(struct_name, preset.get_editor_property(property_name))
        collector.add(state["path"], struct_name, property_name, values, state, is_ignored_today)


def find_unsaved_package_names(unreal):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return sorted({package.get_name() for package in dirty})


def resolve_live_preset_state(directory, actor):
    found, live_component = read_editor_property(actor, "live_component")
    if not found or live_component is None:
        return None
    return directory.describe(live_component.get_editor_property("active_preset"))


def resolve_fire_target_state(directory, actor):
    found, target = read_editor_property(actor, "fire_target_actor")
    if not found:
        return None
    if target is None:
        return CUSTOM_PRESET_STATE
    return resolve_live_preset_state(directory, target)


def read_actor(binding, directory, actor):
    entry = {}
    copies = []
    live_state = resolve_live_preset_state(directory, actor)
    fire_target_state = resolve_fire_target_state(directory, actor)
    for property_name in ACTOR_STRUCT_PROPERTIES + ACTOR_EXTRA_STRUCT_PROPERTIES:
        found, value = read_editor_property(actor, property_name)
        if found and value is not None:
            entry[property_name] = value.export_text()
    for property_name, struct_name, uses_fire_target in ACTOR_COPY_PROPERTIES:
        found, value = read_editor_property(actor, property_name)
        if found and value is not None:
            copies.append((struct_name, property_name, binding.neutral_struct(struct_name, value), fire_target_state if uses_fire_target else live_state))
    found, live_component = read_editor_property(actor, "live_component")
    if found and live_component is not None:
        active_preset = live_component.get_editor_property("active_preset")
        entry["live_active_preset"] = active_preset.get_path_name() if active_preset is not None else None
        entry["live_simulation_config"] = live_component.get_editor_property("simulation_config").export_text()
    found, target = read_editor_property(actor, "fire_target_actor")
    if found:
        entry["fire_target_actor"] = target.get_actor_label() if target is not None else None
    for property_name, component_name in COMPONENT_COPY_PROPERTIES:
        found, component = read_editor_property(actor, property_name)
        if found and component is not None:
            values = binding.neutral_struct(component_name, component)
            entry[property_name] = jsonable_struct(binding.schema, component_name, values)
            copies.append((component_name, property_name, values, live_state))
    if entry:
        entry["actor_class"] = actor.get_class().get_name()
    return entry, copies


def make_unique_key(entries, label):
    key = label
    suffix = 1
    while key in entries:
        suffix += 1
        key = f"{label}#{suffix}"
    return key


def collect_level_actors(unreal, binding, directory, collector, map_path):
    map_name = map_path.rsplit("/", 1)[1]
    entries = {}
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        entry, copies = read_actor(binding, directory, actor)
        if not entry:
            continue
        key = make_unique_key(entries, actor.get_actor_label())
        entries[key] = entry
        for struct_name, property_name, values, preset_state in copies:
            collector.add(f"{map_name}:{key}", struct_name, property_name, values, preset_state, False)
    return dict(sorted(entries.items()))


def read_current_map_package(unreal):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None:
        return None
    package_path = world.get_path_name().split(".")[0]
    return None if package_path.startswith("/Temp/") else package_path


def collect_stage_maps(unreal, binding, directory, collector):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    original_map = read_current_map_package(unreal)
    stage_maps = {}
    try:
        for map_path in STAGE_MAP_PATHS:
            if not levels.load_level(map_path):
                raise RuntimeError(f"무대 레벨 열기 실패: {map_path}")
            stage_maps[map_path] = collect_level_actors(unreal, binding, directory, collector, map_path)
    finally:
        if original_map is not None:
            levels.load_level(original_map)
    return stage_maps


def build_legacy_fire_stage_actors(stage_maps):
    legacy = {}
    for label, entry in stage_maps.get(FIRE_STAGE_MAP_PATH, {}).items():
        legacy_entry = {key: entry[key] for key in LEGACY_ACTOR_KEYS if key in entry}
        if legacy_entry:
            legacy[label] = legacy_entry
    return legacy


def build_conflict_entry(kind, struct_name, field_name, group):
    owners_by_value = {}
    for owner, value in group["copies"].items():
        owners_by_value.setdefault(canonical_text(value), {"value": value, "owners": []})["owners"].append(owner)
    values = [owners_by_value[key] for key in sorted(owners_by_value)]
    for item in values:
        item["owners"].sort()
    return {"kind": kind, "struct": struct_name, "field": field_name, "values": values}


def build_copy_mismatch_report(schema, expected_values, collector, builder_sources, context):
    copies = []
    groups = {}
    kept_count = 0
    already_ignored_count = 0
    for record in collector.records:
        if record["kind"] == "Custom" or record["override"]:
            kept_count += 1
            continue
        if record["is_ignored_today"]:
            already_ignored_count += 1
            continue
        copy_json = jsonable_struct(schema, record["struct"], record["values"])
        builder_json = jsonable_struct(schema, record["struct"], expected_values[record["kind"]][record["struct"]])
        mismatches = {name: {"builder": builder_json[name], "copy": copy_json[name]} for name in sorted(copy_json) if canonical_text(copy_json[name]) != canonical_text(builder_json[name])}
        copies.append({"owner": record["owner"], "property": record["property"], "struct": record["struct"], "kind": record["kind"], "mismatches": mismatches})
        for name in copy_json:
            group = groups.setdefault((record["kind"], record["struct"], name), {"builder": builder_json[name], "copies": {}})
            group["copies"][record["owner"]] = copy_json[name]
    by_kind = {}
    conflicts = []
    for (kind, struct_name, field_name), group in sorted(groups.items()):
        distinct_values = {canonical_text(value) for value in group["copies"].values()}
        if distinct_values == {canonical_text(group["builder"])}:
            continue
        by_kind.setdefault(kind, {}).setdefault(struct_name, {})[field_name] = {"builder": group["builder"], "copies": group["copies"], "distinct_copy_values": len(distinct_values)}
        if len(distinct_values) > 1:
            conflicts.append(build_conflict_entry(kind, struct_name, field_name, group))
    return {
        "summary": {
            "map_count": context["map_count"],
            "preset_count": context["preset_count"],
            "copies_kept_custom_or_override": kept_count,
            "copies_already_ignored_today": already_ignored_count,
            "copies_newly_ignored": len(copies),
            "copies_with_mismatch": sum(1 for copy in copies if copy["mismatches"]),
            "mismatch_field_count": sum(len(copy["mismatches"]) for copy in copies),
            "same_kind_conflict_count": len(conflicts),
            "unresolved_count": len(collector.unresolved),
            "builder_sources": builder_sources,
        },
        "copies": sorted(copies, key=lambda copy: (copy["owner"], copy["property"])),
        "by_kind": by_kind,
        "conflicts": conflicts,
        "unresolved": sorted(collector.unresolved, key=lambda item: (item["owner"], item["property"])),
    }


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as output_file:
        output_file.write(text)


def main():
    import unreal

    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if editor.get_game_world() is not None:
        raise RuntimeError("PIE를 종료한 뒤 실행하세요.")
    unsaved_names = find_unsaved_package_names(unreal)
    if unsaved_names:
        raise RuntimeError("미저장 작업을 보존합니다(맵을 바꾸기 전): " + ", ".join(unsaved_names))
    project_directory = unreal.SystemLibrary.get_project_directory()
    checker = load_checker(project_directory)
    schema = checker.load_schema(project_directory)
    copy_schema = build_copy_schema(project_directory, schema)
    binding = UnrealSchemaBinding(unreal, copy_schema, [struct_name for struct_name, _ in TUNED_STRUCT_BUILDERS] + list(COPY_ONLY_NAMES))
    values_by_kind = collect_builder_values(unreal, binding)
    defaults = collect_struct_defaults(unreal, binding)
    extra_values_by_kind, builder_sources = collect_extra_builder_values(unreal, binding, read_declared_builders(project_directory))
    output_directory = os.path.join(project_directory, *OUTPUT_DIRECTORY)
    builder_dump_path = os.path.join(output_directory, BUILDER_DUMP_NAME)
    generated_path = os.path.join(output_directory, GENERATED_TUNING_NAME)
    stage_values_path = os.path.join(output_directory, STAGE_VALUES_NAME)
    copy_mismatch_path = os.path.join(output_directory, COPY_MISMATCH_NAME)
    write_text(builder_dump_path, format_builder_dump(schema, values_by_kind))
    write_text(generated_path, format_tuning_document(schema, build_tuning_document(schema, defaults, values_by_kind)))
    generated_errors = checker.check_tuning_file(generated_path, project_directory)
    collector = CopyRecordCollector()
    directory = PresetDirectory(binding, collector)
    registry_paths = [directory.describe(preset)["path"] for preset in list_preset_assets(unreal)]
    registry_presets = {path: directory.exports[path] for path in registry_paths}
    stage_maps = collect_stage_maps(unreal, binding, directory, collector)
    referenced_presets = {path: directory.exports[path] for path in sorted(set(directory.states) - set(registry_presets))}
    stage_values = {
        "presets": registry_presets,
        "referenced_presets": referenced_presets,
        "fire_stage_actors": build_legacy_fire_stage_actors(stage_maps),
        "stage_maps": stage_maps,
    }
    write_text(stage_values_path, json.dumps(stage_values, ensure_ascii=False, sort_keys=True, indent=1) + "\n")
    expected_values = {kind: {**values_by_kind[kind], **extra_values_by_kind[kind]} for kind in schema.kinds()}
    context = {"map_count": len(stage_maps), "preset_count": len(directory.states)}
    copy_report = build_copy_mismatch_report(copy_schema, expected_values, collector, builder_sources, context)
    write_text(copy_mismatch_path, json.dumps(copy_report, ensure_ascii=False, sort_keys=True, indent=1) + "\n")
    print(json.dumps({
        "status": "ok" if not generated_errors else "ok_generated_tuning_is_legacy_subset",
        "builder_dump": builder_dump_path,
        "generated_tuning": generated_path,
        "generated_tuning_errors": generated_errors,
        "stage_values": stage_values_path,
        "preset_count": len(registry_presets),
        "fire_stage_actor_count": len(stage_values["fire_stage_actors"]),
        "copy_mismatches": copy_mismatch_path,
        "copy_summary": copy_report["summary"],
    }, ensure_ascii=False))


if __name__ == "__main__":
    main()

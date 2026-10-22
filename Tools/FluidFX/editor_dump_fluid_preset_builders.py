"""실행 환경: TDGame 언리얼 에디터 Python(PIE 아님, 미저장 작업 없음). LV_TDFluidFireStage 를 열어 액터 값을 읽으므로 현재 맵이 바뀐다
실행 명령: python Tools/run_in_editor.py Tools/FluidFX/editor_dump_fluid_preset_builders.py
출력: Saved/FluidFX/preset_builder_dump.json(종류×빌더 5개 필드 전부, 실수는 float.hex), Saved/FluidFX/preset_tuning_generated.json(정본 JSON 후보, spec-pd-b 2절 형식), Saved/FluidFX/preset_stage_values.json(/Game/FluidFX/Presets DA 값과 불 무대 액터 설정 export_text)
상태: 검증됨(2026-10-22 FX-8c 프리셋 데이터: 변경 전 바이너리에서 두 번 실행해 세 파일 바이트 동일·생성 JSON = 정본, 새 바이너리에서 빌더 덤프 = 변경 전 golden 비트 동일, 민감도 ExplosionPair 유지 0.15 에서 2칸만 다름)
"""
import decimal
import importlib.util
import json
import os
import struct

TUNED_STRUCT_BUILDERS = (
    ("TDFluidSimulationConfig", "build_config_for_preset_kind"),
    ("TDFluidSourceEmitterSettings", "build_source_emitter_for_preset_kind"),
    ("TDFluidFragmentTrailSettings", "build_fragment_trail_for_preset_kind"),
    ("TDFluidVolumeRenderSettings", "build_volume_render_for_preset_kind"),
    ("TDFluidFlameCardLayerSettings", "build_flame_card_layers_for_preset_kind"),
)
GROUP_TABLE = (
    ("FireFamily", ("Torch", "Campfire", "CampfireSmoke", "Explosion", "ExplosionSmoke", "ExplosionGroundDust", "Candle", "Fireball", "PlasmaExplosion", "SteamPuff", "Vortex", "InvertedVortex", "PortalVortex")),
    ("WaterLayer", ("River", "Sea")),
    ("VortexTrio", ("Vortex", "InvertedVortex", "PortalVortex")),
    ("ExplosionPair", ("Explosion", "PlasmaExplosion")),
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
PRESET_DIRECTORY = "/Game/FluidFX/Presets"
FIRE_STAGE_MAP_PATH = "/Game/FluidFX/Maps/LV_TDFluidFireStage"
FLUID_ROOT = "/Game/FluidFX"
ACTOR_STRUCT_PROPERTIES = ("source_emitter", "flame_card_layers", "trail_settings")
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
    def __init__(self, unreal, schema):
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
        for struct_name, _ in TUNED_STRUCT_BUILDERS:
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


def collect_preset_assets(unreal, binding):
    presets = {}
    assets = unreal.EditorAssetLibrary
    for asset_path in sorted(assets.list_assets(PRESET_DIRECTORY, recursive=False, include_folder=False)):
        preset = assets.load_asset(asset_path)
        if not isinstance(preset, unreal.TDFluidPresetAsset):
            continue
        presets[asset_path] = {
            "kind": binding.enum_name("ETDFluidPresetKind", preset.get_editor_property("preset_kind")),
            "override": bool(preset.get_editor_property("override_preset_defaults")),
            "simulation_config": preset.get_editor_property("simulation_config").export_text(),
            "volume_render": preset.get_editor_property("volume_render").export_text(),
            "plane_settings": preset.get_editor_property("plane_settings").export_text(),
            "sort": int(preset.get_editor_property("translucency_sort_priority")),
        }
    return presets


def find_unsaved_package_names(unreal):
    dirty = list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    dirty.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    return sorted({package.get_name() for package in dirty})


def collect_fire_stage_actors(unreal):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.load_level(FIRE_STAGE_MAP_PATH):
        raise RuntimeError(f"불 무대 레벨 열기 실패: {FIRE_STAGE_MAP_PATH}")
    values = {}
    for actor in actors.get_all_level_actors():
        entry = {}
        for property_name in ACTOR_STRUCT_PROPERTIES:
            found, value = read_editor_property(actor, property_name)
            if found and value is not None:
                entry[property_name] = value.export_text()
        found, live_component = read_editor_property(actor, "live_component")
        if found and live_component is not None:
            preset = live_component.get_editor_property("active_preset")
            entry["live_active_preset"] = preset.get_path_name() if preset is not None else None
            entry["live_simulation_config"] = live_component.get_editor_property("simulation_config").export_text()
        if not entry:
            continue
        label = actor.get_actor_label()
        key = label
        suffix = 1
        while key in values:
            suffix += 1
            key = f"{label}#{suffix}"
        values[key] = entry
    return dict(sorted(values.items()))


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
    binding = UnrealSchemaBinding(unreal, schema)
    values_by_kind = collect_builder_values(unreal, binding)
    defaults = collect_struct_defaults(unreal, binding)
    output_directory = os.path.join(project_directory, *OUTPUT_DIRECTORY)
    builder_dump_path = os.path.join(output_directory, BUILDER_DUMP_NAME)
    generated_path = os.path.join(output_directory, GENERATED_TUNING_NAME)
    stage_values_path = os.path.join(output_directory, STAGE_VALUES_NAME)
    write_text(builder_dump_path, format_builder_dump(schema, values_by_kind))
    write_text(generated_path, format_tuning_document(schema, build_tuning_document(schema, defaults, values_by_kind)))
    generated_errors = checker.check_tuning_file(generated_path, project_directory)
    stage_values = {
        "presets": collect_preset_assets(unreal, binding),
        "fire_stage_actors": collect_fire_stage_actors(unreal),
    }
    write_text(stage_values_path, json.dumps(stage_values, ensure_ascii=False, sort_keys=True, indent=1) + "\n")
    print(json.dumps({
        "status": "ok" if not generated_errors else "generated_tuning_invalid",
        "builder_dump": builder_dump_path,
        "generated_tuning": generated_path,
        "generated_tuning_errors": generated_errors,
        "stage_values": stage_values_path,
        "preset_count": len(stage_values["presets"]),
        "fire_stage_actor_count": len(stage_values["fire_stage_actors"]),
    }, ensure_ascii=False))
    if generated_errors:
        raise RuntimeError("생성한 정본 JSON 이 검사를 통과하지 못함: " + " | ".join(generated_errors))


if __name__ == "__main__":
    main()

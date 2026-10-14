"""에디터 안에서 실행: 탑다운 재질 체계(D-43)를 만든다 — MPC_TD_TopDown, MF_TD_OcclusionCutout, 마스터 5개(팩 복제 후 OpacityMask 앞에 컷아웃 삽입), 팩 재질의 거울 MI_TD_*, 이계 MI 재부모화.
실행: python Tools/run_in_editor.py Tools/WorldGen/editor_make_topdown_materials.py (PowerShell). 재실행 안전: 이미 있는 것은 건너뛰고 차이만 고친다. 팩 원본(/Game/DarkFantasyTopDown)은 읽기만 한다.
출력: /Game/World/Materials/TopDown/{Parameters,Functions,Masters,Instances}/ 에셋, /Game/World/Realms/*/Materials/MI_TDRealm_* 부모 교체, Saved/WorldGen/topdown_materials_report.json
상태: 현행(P3-17). 적용은 editor_apply_topdown_materials.py, 이름 규약은 topdown_materials.py
"""
import importlib
import json
import os
import sys
import time

import unreal

PROJECT_DIR = unreal.SystemLibrary.get_project_directory()
WORLDGEN_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "WorldGen")
if WORLDGEN_TOOLS_DIR not in sys.path:
    sys.path.insert(0, WORLDGEN_TOOLS_DIR)

td = importlib.reload(importlib.import_module("topdown_materials"))

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
REPORT_PATH = os.path.join(PROJECT_DIR, "Saved", "WorldGen", "topdown_materials_report.json")
FUNCTION_REVISION = "TD TopDown occlusion cutout r1"
DITHER_FUNCTION_PATH = "/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA"
OVERRIDE_BASE_PROPERTIES = ("two_sided", "dithered_lod_transition", "opacity_mask_clip_value", "shading_model")
t0 = time.time()
report = {"created": [], "changed": [], "unchanged": [], "skipped": [], "errors": [], "compile_errors": {}, "reparented": []}


def log(message):
    print(f"[TDTopDown {time.time() - t0:6.1f}s] {message}")


def note(bucket, text):
    report[bucket].append(text)
    log(f"{bucket}: {text}")


def ensure_directory(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def create_asset(package_path, asset_class, factory):
    folder, name = package_path.rsplit("/", 1)
    ensure_directory(folder)
    asset = ASSET_TOOLS.create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError(f"create_asset failed: {package_path}")
    return asset


def save(asset):
    if not EAL.save_loaded_asset(asset, False):
        note("errors", f"save failed {asset.get_path_name()}")


def ensure_collection():
    path = td.COLLECTION_PATH
    created = not EAL.does_asset_exist(path)
    collection = create_asset(path, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew()) if created else EAL.load_asset(path)
    scalars = list(collection.get_editor_property("scalar_parameters"))
    vectors = list(collection.get_editor_property("vector_parameters"))
    known_scalars = {str(item.get_editor_property("parameter_name")) for item in scalars}
    known_vectors = {str(item.get_editor_property("parameter_name")) for item in vectors}
    added = []
    for name, default in td.COLLECTION_SCALARS:
        if name in known_scalars:
            continue
        item = unreal.CollectionScalarParameter()
        item.set_editor_property("parameter_name", name)
        item.set_editor_property("default_value", default)
        scalars.append(item)
        added.append(name)
    for name, default in td.COLLECTION_VECTORS:
        if name in known_vectors:
            continue
        item = unreal.CollectionVectorParameter()
        item.set_editor_property("parameter_name", name)
        item.set_editor_property("default_value", unreal.LinearColor(*default))
        vectors.append(item)
        added.append(name)
    if not added and not created:
        note("unchanged", path)
        return collection
    collection.set_editor_property("scalar_parameters", scalars)
    collection.set_editor_property("vector_parameters", vectors)
    save(collection)
    note("created" if created else "changed", f"{path} parameters {added}")
    return collection


class FunctionBuilder:
    def __init__(self, function):
        self.function = function

    def add(self, expression_class, x, y, **properties):
        node = MEL.create_material_expression_in_function(self.function, expression_class, x, y)
        if node is None:
            raise RuntimeError(f"create expression failed {expression_class}")
        for key, value in properties.items():
            node.set_editor_property(key, value)
        return node

    def link(self, source, target, input_name="", output_name=""):
        names = [str(n) for n in MEL.get_material_expression_input_names(target)]
        if input_name and input_name not in names:
            raise RuntimeError(f"{target.get_class().get_name()} has no input {input_name}: {names}")
        if not MEL.connect_material_expressions(source, output_name, target, input_name):
            raise RuntimeError(f"connect failed {source.get_class().get_name()} -> {target.get_class().get_name()}.{input_name} {names}")

    def binary(self, expression_class, x, y, first, second):
        node = self.add(expression_class, x, y)
        self.link(first, node, "A")
        self.link(second, node, "B")
        return node

    def step(self, x, y, edge, value):
        node = self.add(unreal.MaterialExpressionStep, x, y)
        self.link(edge, node, "Y")
        self.link(value, node, "X")
        return node

    def mask(self, x, y, source, channels):
        return self.add(unreal.MaterialExpressionComponentMask, x, y, r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)

    def constant(self, x, y, value):
        return self.add(unreal.MaterialExpressionConstant, x, y, r=float(value))

    def parameter(self, x, y, collection, name):
        return self.add(unreal.MaterialExpressionCollectionParameter, x, y, collection=collection, parameter_name=name)


def masked(builder, x, y, source, channels):
    node = builder.mask(x, y, source, channels)
    builder.link(source, node)
    return node


def make_opaque_preview_value():
    value = unreal.Vector4f()
    value.import_text("(X=1.0,Y=0.0,Z=0.0,W=0.0)")
    return value


def build_cutout_function(function, collection):
    b = FunctionBuilder(function)
    existing = b.add(unreal.MaterialExpressionFunctionInput, -2200, -400, input_name="ExistingMask", input_type=unreal.FunctionInputType.FUNCTION_INPUT_SCALAR,
                     preview_value=make_opaque_preview_value(), use_preview_value_as_default=True, sort_priority=0)
    pixel = b.add(unreal.MaterialExpressionWorldPosition, -2200, 0, world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    camera = b.add(unreal.MaterialExpressionCameraPositionWS, -2200, 250)
    focus = b.parameter(-2200, 450, collection, "OcclusionFocus")
    focus_xyz = masked(b, -1950, 450, focus, "rgb")
    focus_weight = masked(b, -1950, 600, focus, "a")

    focus_relative = b.add(unreal.MaterialExpressionTruncateLWC, -1450, 350)
    b.link(b.binary(unreal.MaterialExpressionSubtract, -1700, 350, focus_xyz, camera), focus_relative)

    focus_length_squared = b.add(unreal.MaterialExpressionMax, -1000, 450)
    b.link(b.binary(unreal.MaterialExpressionDotProduct, -1200, 450, focus_relative, focus_relative), focus_length_squared, "A")
    b.link(b.constant(-1200, 600, 1.0), focus_length_squared, "B")
    along = b.binary(unreal.MaterialExpressionDivide, -750, 300, b.binary(unreal.MaterialExpressionDotProduct, -1000, 250, pixel, focus_relative), focus_length_squared)
    scaled_focus = b.binary(unreal.MaterialExpressionMultiply, -550, 400, focus_relative, along)
    lateral = b.binary(unreal.MaterialExpressionSubtract, -350, 300, pixel, scaled_focus)
    distance = b.add(unreal.MaterialExpressionLength, -150, 300)
    b.link(lateral, distance)

    radius = b.parameter(-700, 700, collection, "OcclusionRadius")
    softness = b.parameter(-700, 850, collection, "OcclusionSoftness")
    inner_radius = b.binary(unreal.MaterialExpressionSubtract, -450, 750, radius, softness)
    smooth = b.add(unreal.MaterialExpressionSmoothStep, -150, 700)
    b.link(inner_radius, smooth, "Min")
    b.link(radius, smooth, "Max")
    b.link(distance, smooth, "Value")
    inside_radius = b.add(unreal.MaterialExpressionOneMinus, 100, 700)
    b.link(smooth, inside_radius)

    after_camera = b.step(-350, 0, b.constant(-600, -50, 0.02), along)
    before_focus = b.step(-350, -150, along, b.constant(-600, -200, 0.97))

    camera_height = masked(b, -1950, 800, camera, "b")
    feet = b.parameter(-1700, 800, collection, "OcclusionFeetZ")
    keep_base = b.parameter(-1700, 950, collection, "OcclusionKeepBase")
    base_height = b.binary(unreal.MaterialExpressionAdd, -1450, 850, feet, keep_base)
    base_relative = b.add(unreal.MaterialExpressionTruncateLWC, -1000, 850)
    b.link(b.binary(unreal.MaterialExpressionSubtract, -1250, 850, base_height, camera_height), base_relative)
    pixel_height = masked(b, -1450, 1050, pixel, "b")
    above_base = b.step(-350, 900, base_relative, pixel_height)

    cut = b.binary(unreal.MaterialExpressionMultiply, 350, 200, focus_weight, after_camera)
    cut = b.binary(unreal.MaterialExpressionMultiply, 550, 200, cut, before_focus)
    cut = b.binary(unreal.MaterialExpressionMultiply, 750, 200, cut, inside_radius)
    cut = b.binary(unreal.MaterialExpressionMultiply, 950, 200, cut, above_base)
    max_cut = b.parameter(750, 400, collection, "OcclusionMaxCut")
    cut = b.binary(unreal.MaterialExpressionMultiply, 1150, 250, cut, max_cut)
    opacity = b.add(unreal.MaterialExpressionOneMinus, 1350, 250)
    b.link(cut, opacity)

    dither = b.add(unreal.MaterialExpressionMaterialFunctionCall, 1550, 250, material_function=unreal.load_asset(DITHER_FUNCTION_PATH))
    b.link(opacity, dither, "Alpha Threshold")
    blended = b.add(unreal.MaterialExpressionMin, 1800, 100)
    b.link(existing, blended, "A")
    b.link(dither, blended, "B", "Result")
    ray_switch = b.add(unreal.MaterialExpressionRayTracingQualitySwitch, 2050, 100)
    b.link(blended, ray_switch, "Normal")
    b.link(existing, ray_switch, "RayTraced")
    shadow_switch = b.add(unreal.MaterialExpressionShadowReplace, 2300, 100)
    b.link(ray_switch, shadow_switch, "Default")
    b.link(existing, shadow_switch, "Shadow")
    output = b.add(unreal.MaterialExpressionFunctionOutput, 2550, 100, output_name="OpacityMask", sort_priority=0)
    b.link(shadow_switch, output)


def clear_function(function):
    for _ in range(6):
        remaining = list(MEL.get_material_function_expressions(function))
        if not remaining:
            return
        for expression in remaining:
            MEL.delete_material_expression_in_function(function, expression)
    raise RuntimeError("could not clear function expressions")


def function_is_current(function):
    if str(function.get_editor_property("description")) != FUNCTION_REVISION:
        return False
    expressions = MEL.get_material_function_expressions(function)
    inputs = [e for e in expressions if isinstance(e, unreal.MaterialExpressionFunctionInput)]
    outputs = [e for e in expressions if isinstance(e, unreal.MaterialExpressionFunctionOutput)]
    return len(inputs) == 1 and len(outputs) == 1 and str(inputs[0].get_editor_property("input_name")) == "ExistingMask"


def ensure_function(collection):
    path = td.FUNCTION_PATH
    created = not EAL.does_asset_exist(path)
    function = create_asset(path, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew()) if created else EAL.load_asset(path)
    if not created and function_is_current(function):
        note("unchanged", path)
        return function
    clear_function(function)
    build_cutout_function(function, collection)
    function.set_editor_property("description", FUNCTION_REVISION)
    MEL.update_material_function(function)
    save(function)
    note("created" if created else "changed", path)
    return function


def cutout_call_nodes(material, function):
    nodes = []
    for expression in MEL.get_material_expressions(material):
        if not isinstance(expression, unreal.MaterialExpressionMaterialFunctionCall):
            continue
        used = expression.get_editor_property("material_function")
        if used is not None and used.get_name() == function.get_name():
            nodes.append(expression)
    return nodes


def find_pack_opacity_source(master, pack_path):
    pack = EAL.load_asset(pack_path)
    source = MEL.get_material_property_input_node(pack, unreal.MaterialProperty.MP_OPACITY_MASK)
    if source is None:
        return None, ""
    output_name = MEL.get_material_property_input_node_output_name(pack, unreal.MaterialProperty.MP_OPACITY_MASK)
    for expression in MEL.get_material_expressions(master):
        if expression.get_name() == source.get_name():
            return expression, output_name
    return None, ""


def cutout_is_wired(master, call, upstream):
    root = MEL.get_material_property_input_node(master, unreal.MaterialProperty.MP_OPACITY_MASK)
    if root is None or root.get_name() != call.get_name():
        return False
    fed = list(MEL.get_inputs_for_material_expression(master, call))
    first = fed[0] if fed else None
    if first is None:
        return False
    if upstream is None:
        return isinstance(first, unreal.MaterialExpressionConstant)
    return first.get_name() == upstream.get_name()


def remove_cutout_calls(master, calls, upstream):
    for call in calls:
        for fed in MEL.get_inputs_for_material_expression(master, call):
            if isinstance(fed, unreal.MaterialExpressionConstant) and (upstream is None or fed.get_name() != upstream.get_name()):
                MEL.delete_material_expression(master, fed)
        MEL.delete_material_expression(master, call)


def insert_cutout(master, function, pack_path):
    upstream, upstream_output = find_pack_opacity_source(master, pack_path)
    calls = cutout_call_nodes(master, function)
    if len(calls) == 1 and cutout_is_wired(master, calls[0], upstream):
        return False
    remove_cutout_calls(master, calls, upstream)
    position_x, position_y = (-300, 0)
    if upstream is not None:
        position_x, position_y = MEL.get_material_expression_node_position(upstream)
        position_x += 260
    call = MEL.create_material_expression(master, unreal.MaterialExpressionMaterialFunctionCall, position_x, position_y)
    call.set_editor_property("material_function", function)
    source, source_output = upstream, upstream_output
    if source is None:
        source, source_output = MEL.create_material_expression(master, unreal.MaterialExpressionConstant, position_x - 200, position_y), ""
        source.set_editor_property("r", 1.0)
    if not MEL.connect_material_expressions(source, source_output, call, "ExistingMask"):
        raise RuntimeError(f"connect existing mask failed {master.get_name()}")
    if not MEL.connect_material_property(call, "OpacityMask", unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError(f"connect opacity mask failed {master.get_name()}")
    return True


def ensure_usage(master, usage):
    if MEL.has_material_usage(master, usage):
        return False
    MEL.set_base_material_usage(master, usage, True)
    return MEL.has_material_usage(master, usage)


def ensure_master(pack_path, master_name, function):
    destination = td.master_path(master_name)
    created = not EAL.does_asset_exist(destination)
    if created:
        ensure_directory(td.MASTERS_DIR)
        if EAL.duplicate_asset(pack_path, destination) is None:
            raise RuntimeError(f"duplicate failed {pack_path}")
    master = EAL.load_asset(destination)
    inserted = insert_cutout(master, function, pack_path)
    forced = master_name in td.FORCE_MASKED_MASTERS and master.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_MASKED
    if forced:
        master.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    usage_added = ensure_usage(master, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    assumed_opaque_cleared = unreal.TDWorldGenEditorLibrary.clear_masked_assumed_opaque(master)
    if not (created or inserted or forced or usage_added or assumed_opaque_cleared):
        note("unchanged", destination)
        return master
    errors = [str(item) for item in MEL.recompile_material(master) or []]
    report["compile_errors"][master_name] = errors
    save(master)
    note("created" if created else "changed", f"{destination} inserted={inserted} forcedMasked={forced} usageISM={usage_added} assumedOpaqueCleared={assumed_opaque_cleared} compileErrors={len(errors)}")
    return master


def read_base_overrides(instance):
    overrides = instance.get_editor_property("base_property_overrides")
    return {key: overrides.get_editor_property(key) for key in OVERRIDE_BASE_PROPERTIES if overrides.get_editor_property("override_" + key)}


def collect_chain(original):
    chain = []
    current = original
    for _ in range(16):
        if not isinstance(current, unreal.MaterialInstance):
            break
        chain.append(current)
        current = current.get_editor_property("parent")
    return list(reversed(chain))


def desired_base_overrides(original):
    desired = {}
    for instance in collect_chain(original):
        desired.update(read_base_overrides(instance))
    return desired


def apply_base_overrides(mirror, desired):
    overrides = mirror.get_editor_property("base_property_overrides")
    changed = False
    for key in OVERRIDE_BASE_PROPERTIES:
        wanted = key in desired
        if overrides.get_editor_property("override_" + key) != wanted:
            overrides.set_editor_property("override_" + key, wanted)
            changed = True
        if wanted and overrides.get_editor_property(key) != desired[key]:
            overrides.set_editor_property(key, desired[key])
            changed = True
    if changed:
        mirror.set_editor_property("base_property_overrides", overrides)
    return changed


PARAMETER_WRITERS = {
    "scalar": lambda mirror, name, value: MEL.set_material_instance_scalar_parameter_value(mirror, name, value),
    "vector": lambda mirror, name, value: MEL.set_material_instance_vector_parameter_value(mirror, name, unreal.LinearColor(*value)),
    "texture": lambda mirror, name, value: MEL.set_material_instance_texture_parameter_value(mirror, name, EAL.load_asset(value) if value else None),
    "switch": lambda mirror, name, value: MEL.set_material_instance_static_switch_parameter_value(mirror, name, value),
}


def sync_parameters(original, mirror, master):
    wanted_snapshot = td.parameter_snapshot(original, master)
    current_snapshot = td.parameter_snapshot(mirror, master)
    changed = []
    for key, wanted in wanted_snapshot.items():
        if current_snapshot.get(key) == wanted:
            continue
        kind, name = key.split(":", 1)
        PARAMETER_WRITERS[kind](mirror, name, float(wanted) if kind == "scalar" else wanted)
        changed.append(key)
    return changed


def ensure_mirror(pack_path, td_masters):
    original = EAL.load_asset(pack_path)
    top_path = td.top_master_package_path(original)
    master_name = td.topdown_master_name(top_path) if top_path else None
    if master_name is None:
        note("skipped", f"{pack_path} top master {top_path}")
        return None
    mirror_path = td.topdown_material_path(pack_path)
    created = not EAL.does_asset_exist(mirror_path)
    mirror = create_asset(mirror_path, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()) if created else EAL.load_asset(mirror_path)
    master = td_masters[master_name]
    changes = []
    if mirror.get_editor_property("parent") != master:
        MEL.set_material_instance_parent(mirror, master)
        changes.append("parent")
    if isinstance(original, unreal.MaterialInstance):
        changes.extend(sync_parameters(original, mirror, master))
        if apply_base_overrides(mirror, desired_base_overrides(original)):
            changes.append("base_property_overrides")
    if not changes and not created:
        report["unchanged"].append(mirror_path)
        return mirror
    MEL.update_material_instance(mirror)
    save(mirror)
    note("created" if created else "changed", f"{mirror_path} {changes}")
    return mirror


def verify_mirror(pack_path, mirror, master):
    original = EAL.load_asset(pack_path)
    if not isinstance(original, unreal.MaterialInstance):
        return []
    differences = td.snapshot_differences(td.parameter_snapshot(original, master), td.parameter_snapshot(mirror, master))
    return [f"{mirror.get_name()} {key}" for key in differences]


def main():
    collection = ensure_collection()
    function = ensure_function(collection)
    td_masters = {}
    for pack_path, master_name in td.MASTER_TABLE.items():
        td_masters[master_name] = ensure_master(pack_path, master_name, function)
    mismatches = []
    mirrored = 0
    for pack_path in td.list_pack_material_paths():
        mirror = ensure_mirror(pack_path, td_masters)
        if mirror is None:
            continue
        mirrored += 1
        master_name = td.topdown_master_name(td.top_master_package_path(EAL.load_asset(pack_path)))
        mismatches.extend(verify_mirror(pack_path, mirror, td_masters[master_name]))
    report["mirror_count"] = mirrored
    report["mirror_value_mismatches"] = mismatches
    report["reparented"] = td.reparent_realm_instances()
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=1)
    changed_total = len(report["created"]) + len(report["changed"]) + sum(1 for item in report["reparented"] if item["status"].startswith("reparented"))
    log(f"DONE created={len(report['created'])} changed={len(report['changed'])} unchanged={len(report['unchanged'])} skipped={len(report['skipped'])} "
        f"mirrors={mirrored} mismatches={len(mismatches)} errors={len(report['errors'])} changes_total={changed_total}")


main()

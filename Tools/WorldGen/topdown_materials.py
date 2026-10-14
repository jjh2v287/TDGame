"""탑다운 재질 체계(D-43)의 이름 규약·대상 마스터 표·MPC 매개변수 표. 에디터 밖에서도 import 가능(순수 함수), unreal이 필요한 함수는 함수 안에서 import한다.
실행: 직접 실행하지 않는다. editor_make_topdown_materials.py·editor_apply_topdown_materials.py가 가져다 쓰고, python Tools/WorldGen/topdown_materials.py 는 자체 점검만 한다.
출력: 없음(자체 점검은 표준 출력에 PASS/FAIL).
상태: 현행(P3-17)
"""
import sys

PACK_MATERIAL_ROOT = "/Game/DarkFantasyTopDown/Materials/"
PACK_MASTER_ROOT = PACK_MATERIAL_ROOT + "MasterMaterials/"
TOPDOWN_ROOT = "/Game/World/Materials/TopDown"
PARAMETERS_DIR = TOPDOWN_ROOT + "/Parameters"
FUNCTIONS_DIR = TOPDOWN_ROOT + "/Functions"
MASTERS_DIR = TOPDOWN_ROOT + "/Masters"
INSTANCES_DIR = TOPDOWN_ROOT + "/Instances"
COLLECTION_NAME = "MPC_TD_TopDown"
COLLECTION_PATH = PARAMETERS_DIR + "/" + COLLECTION_NAME
FUNCTION_NAME = "MF_TD_OcclusionCutout"
FUNCTION_PATH = FUNCTIONS_DIR + "/" + FUNCTION_NAME
REALM_MATERIAL_GLOB = "/Game/World/Realms"
REALM_INSTANCE_PREFIX = "MI_TDRealm_"

COLLECTION_SCALARS = (
    ("OcclusionRadius", 260.0),
    ("OcclusionSoftness", 120.0),
    ("OcclusionKeepBase", 40.0),
    ("OcclusionMaxCut", 0.92),
    ("OcclusionFeetZ", 0.0),
)
COLLECTION_VECTORS = (
    ("OcclusionFocus", (0.0, 0.0, 0.0, 0.0)),
)

MASTER_TABLE = {
    PACK_MASTER_ROOT + "M_MasterMaterialBase": "M_TD_Prop",
    PACK_MASTER_ROOT + "M_RocksMasterMaterial": "M_TD_Rock",
    PACK_MASTER_ROOT + "M_MasterMaterialBaseAnimated": "M_TD_PlantAnimated",
    PACK_MASTER_ROOT + "M_Branch": "M_TD_Leaf",
    PACK_MASTER_ROOT + "M_Bark": "M_TD_Bark",
}
FORCE_MASKED_MASTERS = ("M_TD_Bark",)
SKIPPED_BASE_PROPERTIES = ("blend_mode",)


def to_package_path(asset_path):
    text = str(asset_path or "").strip().strip("'\"")
    if "'" in text:
        text = text.split("'")[1]
    if not text.startswith("/"):
        return ""
    last_slash = text.rfind("/")
    dot = text.find(".", last_slash)
    return text if dot < 0 else text[:dot]


def asset_name(asset_path):
    return to_package_path(asset_path).rsplit("/", 1)[-1]


def is_topdown_path(asset_path):
    return to_package_path(asset_path).startswith(TOPDOWN_ROOT + "/")


def master_path(master_name):
    return f"{MASTERS_DIR}/{master_name}"


def topdown_master_name(pack_master_path):
    return MASTER_TABLE.get(to_package_path(pack_master_path))


def topdown_material_path(original_object_path):
    package_path = to_package_path(original_object_path)
    if not package_path.startswith(PACK_MATERIAL_ROOT):
        return None
    name = package_path.rsplit("/", 1)[-1]
    if package_path in MASTER_TABLE:
        return f"{INSTANCES_DIR}/MI_TD_{name[len('M_'):]}_Default"
    if name.startswith("MI_TD_"):
        return None
    if name.startswith("MI_"):
        return f"{INSTANCES_DIR}/MI_TD_{name[len('MI_'):]}"
    return None


def top_master_package_path(material_interface):
    import unreal

    current = material_interface
    for _ in range(16):
        if current is None:
            return None
        if isinstance(current, unreal.MaterialInstance):
            current = current.get_editor_property("parent")
            continue
        return to_package_path(current.get_path_name())
    return None


def list_pack_material_paths():
    import unreal

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    paths = []
    for data in registry.get_assets_by_path(PACK_MATERIAL_ROOT.rstrip("/"), recursive=True):
        class_name = str(data.asset_class_path.asset_name)
        if class_name not in ("MaterialInstanceConstant", "Material"):
            continue
        package_path = str(data.package_name)
        if topdown_material_path(package_path) is None:
            continue
        paths.append(package_path)
    return sorted(paths)


def _scalar_value(instance, master, name):
    import unreal

    library = unreal.MaterialEditingLibrary
    if isinstance(instance, unreal.MaterialInstance):
        return float(library.get_material_instance_scalar_parameter_value(instance, name))
    return float(library.get_material_default_scalar_parameter_value(master, name))


def _vector_value(instance, master, name):
    import unreal

    library = unreal.MaterialEditingLibrary
    if isinstance(instance, unreal.MaterialInstance):
        color = library.get_material_instance_vector_parameter_value(instance, name)
    else:
        color = library.get_material_default_vector_parameter_value(master, name)
    return (float(color.r), float(color.g), float(color.b), float(color.a))


def _texture_value(instance, master, name):
    import unreal

    library = unreal.MaterialEditingLibrary
    if isinstance(instance, unreal.MaterialInstance):
        texture = library.get_material_instance_texture_parameter_value(instance, name)
    else:
        texture = library.get_material_default_texture_parameter_value(master, name)
    return texture.get_path_name() if texture else ""


def _switch_value(instance, master, name):
    import unreal

    library = unreal.MaterialEditingLibrary
    if isinstance(instance, unreal.MaterialInstance):
        return bool(library.get_material_instance_static_switch_parameter_value(instance, name))
    return bool(library.get_material_default_static_switch_parameter_value(master, name))


def parameter_names(master):
    import unreal

    library = unreal.MaterialEditingLibrary
    return {
        "scalar": [str(n) for n in library.get_scalar_parameter_names(master) or []],
        "vector": [str(n) for n in library.get_vector_parameter_names(master) or []],
        "texture": [str(n) for n in library.get_texture_parameter_names(master) or []],
        "switch": [str(n) for n in library.get_static_switch_parameter_names(master) or []],
    }


def parameter_snapshot(instance, master):
    readers = {"scalar": _scalar_value, "vector": _vector_value, "texture": _texture_value, "switch": _switch_value}
    snapshot = {}
    for kind, names in parameter_names(master).items():
        for name in names:
            value = readers[kind](instance, master, name)
            snapshot[f"{kind}:{name}"] = round(value, 5) if isinstance(value, float) else value
    return snapshot


def snapshot_differences(before, after):
    return sorted(key for key in set(before) | set(after) if before.get(key) != after.get(key))


def reparent_realm_instances(dry_run=False):
    import unreal

    library = unreal.EditorAssetLibrary
    material_library = unreal.MaterialEditingLibrary
    results = []
    for path in library.list_assets(REALM_MATERIAL_GLOB, recursive=True, include_folder=False):
        package_path = to_package_path(path)
        if not asset_name(package_path).startswith(REALM_INSTANCE_PREFIX):
            continue
        instance = library.load_asset(package_path)
        if not isinstance(instance, unreal.MaterialInstanceConstant):
            continue
        parent = instance.get_editor_property("parent")
        parent_path = to_package_path(parent.get_path_name()) if parent else ""
        entry = {"asset": package_path, "parent": parent_path, "status": ""}
        results.append(entry)
        mirror_path = topdown_material_path(parent_path)
        if mirror_path is None:
            entry["status"] = "kept" if is_topdown_path(parent_path) else "skipped: parent is not a mirrorable pack material"
            continue
        if not library.does_asset_exist(mirror_path):
            entry["status"] = "skipped: mirror missing " + mirror_path
            continue
        mirror = library.load_asset(mirror_path)
        master = mirror.get_editor_property("parent")
        before = parameter_snapshot(instance, master)
        if dry_run:
            entry["status"] = "would reparent to " + mirror_path
            continue
        material_library.set_material_instance_parent(instance, mirror)
        material_library.update_material_instance(instance)
        differences = snapshot_differences(before, parameter_snapshot(instance, master))
        if differences:
            material_library.set_material_instance_parent(instance, parent)
            material_library.update_material_instance(instance)
            entry["status"] = "reverted: values changed " + ",".join(differences)
            continue
        library.save_loaded_asset(instance)
        entry["status"] = "reparented to " + mirror_path
    return results


def self_check():
    cases = {
        PACK_MATERIAL_ROOT + "Props/MI_Props1.MI_Props1": INSTANCES_DIR + "/MI_TD_Props1",
        PACK_MATERIAL_ROOT + "Props/MI_Props1": INSTANCES_DIR + "/MI_TD_Props1",
        PACK_MASTER_ROOT + "M_Bark.M_Bark": INSTANCES_DIR + "/MI_TD_Bark_Default",
        PACK_MASTER_ROOT + "M_RocksMasterMaterial": INSTANCES_DIR + "/MI_TD_RocksMasterMaterial_Default",
        PACK_MASTER_ROOT + "M_LightBeam.M_LightBeam": None,
        "/Game/World/Realms/PaleHeights/Materials/MI_TDRealm_PaleHeights_PaleRock": None,
        INSTANCES_DIR + "/MI_TD_Props1": None,
        "": None,
    }
    failures = [(source, expected, topdown_material_path(source)) for source, expected in cases.items()
                if topdown_material_path(source) != expected]
    for failure in failures:
        print("FAIL", failure)
    print("PASS" if not failures else "FAILED")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(self_check())

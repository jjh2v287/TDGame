"""실행 환경: 언리얼 에디터 내장 Python(에디터가 TDGame을 연 상태, PIE 아님). 에셋을 바꾸거나 저장하지 않는다.
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_audit_ragdoll_physics.py (환경변수 TD_AUDIT_OUT로 출력 경로 변경 가능)
출력: Saved/AgentOps/20261018/ragdoll-jitter/physics-audit.json — 종 표 메시·플레이어 메시의 물리 에셋마다 바디(모양 수·크기·물리 종류·자기 충돌 응답·감쇠·잠들기 계열·반복 수·최대 각속도·질량 재정의)와 관절(뼈 쌍·스윙·비틀기 한계·소프트 여부·투영·충돌 끄기), 끝에 한 줄 요약 출력
상태: 현행(M3-38). 메시 목록은 Tools/MonsterAI/species_table.json의 mesh와 GlobalDefaultGameMode 기본 폰의 메시
"""
import json
import os

import unreal

PROJECT_DIRECTORY = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
TABLE_PATH = os.path.join(PROJECT_DIRECTORY, "Tools", "MonsterAI", "species_table.json")
OUTPUT_PATH = os.environ.get("TD_AUDIT_OUT", os.path.join(PROJECT_DIRECTORY, "Saved", "AgentOps", "20261018", "ragdoll-jitter", "physics-audit.json"))
GAME_MODE_CLASS_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode.BP_TDCombatGameMode_C"


def read_property(owner, name, default=None):
    try:
        return owner.get_editor_property(name)
    except Exception:
        return default


def enum_name(value):
    return str(value).split(".")[-1].split(":")[0].strip("<> ") if value is not None else None


def vector_list(vector):
    return [round(vector.x, 2), round(vector.y, 2), round(vector.z, 2)] if vector is not None else None


def describe_geometry(aggregate):
    rows = []
    for sphere in read_property(aggregate, "sphere_elems", []) or []:
        rows.append({"type": "sphere", "radius": round(read_property(sphere, "radius", 0.0), 2), "center": vector_list(read_property(sphere, "center"))})
    for box in read_property(aggregate, "box_elems", []) or []:
        rows.append({"type": "box", "size": [round(read_property(box, axis, 0.0), 2) for axis in ("x", "y", "z")], "center": vector_list(read_property(box, "center"))})
    for capsule in read_property(aggregate, "sphyl_elems", []) or []:
        rows.append({"type": "capsule", "radius": round(read_property(capsule, "radius", 0.0), 2), "length": round(read_property(capsule, "length", 0.0), 2),
                     "center": vector_list(read_property(capsule, "center"))})
    for capsule in read_property(aggregate, "tapered_capsule_elems", []) or []:
        rows.append({"type": "tapered", "radius0": round(read_property(capsule, "radius0", 0.0), 2), "radius1": round(read_property(capsule, "radius1", 0.0), 2),
                     "length": round(read_property(capsule, "length", 0.0), 2)})
    for _ in read_property(aggregate, "convex_elems", []) or []:
        rows.append({"type": "convex"})
    return rows


def describe_body(setup):
    instance = read_property(setup, "default_instance")
    material = read_property(setup, "phys_material") or read_property(instance, "phys_material_override")
    return {
        "bone": str(read_property(setup, "bone_name")),
        "shapes": describe_geometry(read_property(setup, "agg_geom")),
        "physics_type": enum_name(read_property(setup, "physics_type")),
        "collision_response": enum_name(read_property(setup, "collision_reponse")),
        "linear_damping": read_property(instance, "linear_damping"),
        "angular_damping": read_property(instance, "angular_damping"),
        "sleep_family": enum_name(read_property(instance, "sleep_family")),
        "position_iterations": read_property(instance, "position_solver_iteration_count"),
        "velocity_iterations": read_property(instance, "velocity_solver_iteration_count"),
        "override_max_angular_velocity": read_property(instance, "override_max_angular_velocity"),
        "override_mass": read_property(instance, "override_mass"),
        "mass_override_kg": read_property(instance, "mass_in_kg_override"),
        "inertia_tensor_scale": vector_list(read_property(instance, "inertia_tensor_scale")),
        "physical_material": material.get_name() if material else None,
    }


def drop_accessor(value):
    if isinstance(value, tuple):
        values = [item for item in value if not isinstance(item, unreal.ConstraintInstanceAccessor)]
        return values[0] if len(values) == 1 else values
    return value


def describe_constraint(accessor):
    library = unreal.ConstraintInstanceBlueprintLibrary
    angular = drop_accessor(library.get_angular_limits(accessor))
    soft_swing = drop_accessor(library.get_angular_soft_swing_limit_params(accessor))
    soft_twist = drop_accessor(library.get_angular_soft_twist_limit_params(accessor))
    return {
        "bones": [str(name) for name in drop_accessor(library.get_attached_body_names(accessor))],
        "swing1": [enum_name(angular[0]), angular[1]],
        "swing2": [enum_name(angular[2]), angular[3]],
        "twist": [enum_name(angular[4]), angular[5]],
        "swing_soft": soft_swing[0] if isinstance(soft_swing, list) else soft_swing,
        "twist_soft": soft_twist[0] if isinstance(soft_twist, list) else soft_twist,
        "linear": [enum_name(value) if not isinstance(value, float) else value for value in drop_accessor(library.get_linear_limits(accessor))],
        "disable_collision": drop_accessor(library.get_disable_collsion(accessor)),
        "projection": drop_accessor(library.get_projection_params(accessor)),
        "parent_dominates": drop_accessor(library.get_parent_dominates(accessor)),
        "mass_conditioning": drop_accessor(library.get_mass_conditioning_enabled(accessor)),
        "angular_drive": enum_name(drop_accessor(library.get_angular_drive_mode(accessor))),
    }


def find_body_setups(physics_asset):
    return [candidate for candidate in unreal.ObjectIterator(unreal.BodySetup) if candidate.get_outer() == physics_asset]


def describe_physics_asset(physics_asset):
    bodies = [describe_body(setup) for setup in find_body_setups(physics_asset)]
    constraints = [describe_constraint(accessor) for accessor in physics_asset.get_constraints(False)]
    return {
        "path": physics_asset.get_path_name(),
        "body_count": len(bodies),
        "constraint_count": len(constraints),
        "bodies": bodies,
        "constraints": constraints,
    }


def resolve_player_mesh():
    game_mode_class = unreal.load_class(None, GAME_MODE_CLASS_PATH)
    pawn_class = read_property(unreal.get_default_object(game_mode_class), "default_pawn_class") if game_mode_class else None
    pawn = unreal.get_default_object(pawn_class) if pawn_class else None
    mesh_component = read_property(pawn, "mesh") if pawn else None
    return mesh_component.get_skeletal_mesh_asset() if mesh_component else None


def main():
    with open(TABLE_PATH, encoding="utf-8") as stream:
        table = json.load(stream)
    meshes = {}
    for entry in table["species"]:
        meshes.setdefault(entry["mesh"], []).append(entry["name"])
    owners = {}
    for mesh_path, names in meshes.items():
        mesh = unreal.load_asset(mesh_path)
        owners[mesh_path] = (mesh, names)
    player_mesh = resolve_player_mesh()
    if player_mesh:
        owners[player_mesh.get_path_name()] = (player_mesh, ["Player"])
    report = []
    for mesh_path, (mesh, names) in owners.items():
        physics_asset = read_property(mesh, "physics_asset") if mesh else None
        row = {"mesh": mesh_path, "owners": names, "physics_asset": describe_physics_asset(physics_asset) if physics_asset else None}
        report.append(row)
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=1, default=str)
    for row in report:
        asset = row["physics_asset"]
        if not asset:
            print("TD_AUDIT", ",".join(row["owners"]), "no physics asset")
            continue
        enabled = sum(1 for body in asset["bodies"] if body["collision_response"] != "BODY_COLLISION_DISABLED")
        disabled_pairs = sum(1 for constraint in asset["constraints"] if constraint["disable_collision"])
        angular = sorted({body["angular_damping"] for body in asset["bodies"]})
        print("TD_AUDIT", ",".join(row["owners"]), asset["path"].split(".")[-1], "bodies", asset["body_count"], "constraints", asset["constraint_count"],
              "self_collision_on", enabled, "joint_collision_off", disabled_pairs, "angular_damping", angular)
    print("TD_AUDIT_OUT", OUTPUT_PATH)


main()

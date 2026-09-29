"""에디터 안에서 실행: UE5 Manny 애니메이션을 고블린·스톤 골렘 스켈레톤으로 IK 리타기팅해 몬스터용 AnimSequence(루트 모션 끔·루트 잠금)를 만들고 클립별 검사 수치를 출력한다.
실행: python Tools/run_in_editor.py Tools/MonsterAI/editor_retarget_monster_anims.py (PowerShell; Git Bash는 PYTHONIOENCODING=utf-8 을 앞에 붙인다)
출력: /Game/MonsterAI/Retarget/{IK_TD_*,RTG_TD_*}, /Game/MonsterAI/Animations/{Goblin,Golem}/AS_TD_*, 로그의 [TDTool] check 줄(길이·이상 플래그·접지 속도·타격 시각)
상태: 현행

규칙: 에셋 메타데이터 TDGeneratedBy 가 이 도구 이름인 에셋만 갱신·덮어쓴다. 원본 에셋과 레벨은 저장하지 않는다.
"""
import math
import time

import unreal

t0 = time.time()
EAL = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
SOURCE = unreal.RetargetSourceOrTarget.SOURCE
TARGET = unreal.RetargetSourceOrTarget.TARGET

OWNER_TAG = "TDGeneratedBy"
OWNER_NAME = "editor_retarget_monster_anims"
RETARGET_FOLDER = "/Game/MonsterAI/Retarget"
ANIMATION_FOLDER = "/Game/MonsterAI/Animations"
MANNY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
MANNY_ANIMS = "/Game/Characters/Mannequins/Anims"
MANNY_CLIPS = {
    "Idle": "Unarmed/MM_Idle",
    "Walk": "Unarmed/Walk/MF_Unarmed_Walk_Fwd",
    "Run": "Unarmed/Jog/MF_Unarmed_Jog_Fwd",
    "Attack01": "Unarmed/Attack/MM_Attack_01",
    "Slash": "Sword/AS_TD_Player_SwordAttack01",
}
TARGETS = {
    "Goblin": {"mesh": "/Game/Fab/Stylized_Goblin_Minion_-_Free/SK_Goblin", "clips": ["Idle", "Walk", "Run", "Attack01", "Slash"]},
    "Golem": {"mesh": "/Game/Stone_Golem/mesh/SKM_Stone_Golem", "clips": ["Attack01"]},
}
LOCOMOTION_CLIPS = {"Walk", "Run"}
ATTACK_CLIPS = {"Attack01", "Slash"}
CHECK_BONES = ["pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r", "ball_l", "ball_r"]


def log(msg):
    print(f"[TDTool {time.time() - t0:6.1f}s] {msg}")


def is_owned(asset):
    return EAL.get_metadata_tag(asset, OWNER_TAG) == OWNER_NAME


def mark_owned(asset):
    EAL.set_metadata_tag(asset, OWNER_TAG, OWNER_NAME)


def load_owned_or_create(folder, name, asset_class, factory):
    path = f"{folder}/{name}"
    if not EAL.does_asset_exist(path):
        asset = asset_tools.create_asset(name, folder, asset_class, factory)
        mark_owned(asset)
        return asset, True
    asset = EAL.load_asset(path)
    if not is_owned(asset):
        log(f"skip (not created by this tool): {path}")
        return None, False
    return asset, False


def ensure_ik_rig(name, mesh_path):
    rig, is_new = load_owned_or_create(RETARGET_FOLDER, name, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    if rig is None:
        return None
    controller = unreal.IKRigController.get_controller(rig)
    controller.set_skeletal_mesh(EAL.load_asset(mesh_path))
    if is_new or not controller.get_retarget_chains():
        has_definition = controller.apply_auto_generated_retarget_definition()
        has_fbik = controller.apply_auto_fbik()
        log(f"{name}: auto retarget definition {has_definition}, auto FBIK {has_fbik}")
    log(f"{name}: chains {len(controller.get_retarget_chains())}, solvers {controller.get_num_solvers()}, root {controller.get_retarget_root()}")
    return rig


def ensure_retargeter(name, source_rig, target_rig, target_mesh_path):
    retargeter, _ = load_owned_or_create(RETARGET_FOLDER, name, unreal.IKRetargeter, unreal.IKRetargetFactory())
    if retargeter is None:
        return None
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    controller.set_ik_rig(SOURCE, source_rig)
    controller.set_ik_rig(TARGET, target_rig)
    controller.set_preview_mesh(SOURCE, EAL.load_asset(MANNY_MESH))
    controller.set_preview_mesh(TARGET, EAL.load_asset(target_mesh_path))
    controller.remove_all_ops()
    controller.add_default_ops()
    controller.assign_ik_rig_to_all_ops(SOURCE, source_rig)
    controller.assign_ik_rig_to_all_ops(TARGET, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    op_names = [str(controller.get_op_name(i)) for i in range(controller.get_num_retarget_ops())]
    log(f"{name}: ops {op_names}")
    return retargeter


def retarget_clip(retargeter, target_key, target_mesh, clip):
    source_path = f"{MANNY_ANIMS}/{MANNY_CLIPS[clip]}"
    source_name = source_path.split("/")[-1]
    output_name = f"AS_TD_{target_key}_{clip}"
    output_folder = f"{ANIMATION_FOLDER}/{target_key}"
    output_path = f"{output_folder}/{output_name}"
    if not EAL.does_asset_exist(source_path):
        log(f"skip {output_name}: source missing {source_path}")
        return None
    if EAL.does_asset_exist(output_path) and not is_owned(EAL.load_asset(output_path)):
        log(f"skip {output_name}: existing asset not created by this tool")
        return None
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget", [EAL.find_asset_data(source_path)])
    inputs.set_editor_property("source_mesh", EAL.load_asset(MANNY_MESH))
    inputs.set_editor_property("target_mesh", target_mesh)
    inputs.set_editor_property("ik_retarget_asset", retargeter)
    inputs.set_editor_property("search", source_name)
    inputs.set_editor_property("replace", output_name)
    inputs.set_editor_property("target_path", output_folder)
    inputs.set_editor_property("use_source_path", False)
    inputs.set_editor_property("include_referenced_assets", False)
    inputs.set_editor_property("overwrite_existing_files", True)
    unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    sequence = EAL.load_asset(output_path)
    if not isinstance(sequence, unreal.AnimSequence):
        log(f"FAIL {output_name}: batch retarget produced no sequence at {output_path}")
        return None
    sequence.set_editor_property("enable_root_motion", False)
    sequence.set_editor_property("force_root_lock", True)
    mark_owned(sequence)
    return sequence


def make_evaluation_options(mesh):
    options = unreal.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", mesh)
    options.set_editor_property("should_retarget", True)
    return options


def sample_pose(sequence, options, time_seconds):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, time_seconds, options)
    root = unreal.AnimPoseExtensions.get_bone_pose(pose, "root", unreal.AnimPoseSpaces.WORLD)
    positions = {"root": root.translation}
    for bone in CHECK_BONES:
        world = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
        positions[bone] = unreal.MathLibrary.make_relative_transform(world, root).translation
    return positions


def distance(a, b):
    return math.sqrt((a.x - b.x) ** 2 + (a.y - b.y) ** 2 + (a.z - b.z) ** 2)


def find_pose_flags(samples, height):
    flags = []
    count = len(samples) - 1
    for fraction in (0.0, 0.25, 0.5, 0.75):
        pose = samples[int(round(fraction * count))]
        if any(math.isnan(c) for v in pose.values() for c in (v.x, v.y, v.z)):
            flags.append(f"nan@{fraction}")
        if pose["head"].z < pose["pelvis"].z:
            flags.append(f"head_below_pelvis@{fraction}")
        for limb in ("hand_l", "hand_r", "foot_l", "foot_r"):
            if distance(pose[limb], pose["pelvis"]) > 1.2 * height:
                flags.append(f"{limb}_far@{fraction}")
        for foot in ("foot_l", "foot_r"):
            if pose[foot].z < -0.03 * height:
                flags.append(f"{foot}_buried@{fraction}")
    return flags


def measure_planted_foot_speed(samples, step, height):
    speeds = []
    for toe in ("ball_l", "ball_r"):
        heights = [s[toe].z for s in samples]
        lowest = min(heights)
        for threshold in (0.003 * height, 0.006 * height, 0.012 * height):
            velocities = sorted(-(samples[i + 1][toe].y - samples[i - 1][toe].y) / (2 * step) for i in range(1, len(samples) - 1) if heights[i] < lowest + threshold)
            if len(velocities) >= 4:
                speeds.append(round(velocities[len(velocities) // 2]))
                break
    return speeds


def measure_hit_time(samples, times, step):
    best = None
    for hand in ("hand_l", "hand_r"):
        for i in range(1, len(samples) - 1):
            speed = distance(samples[i + 1][hand], samples[i - 1][hand]) / (2 * step)
            if best is None or speed > best[2]:
                best = (hand, times[i], speed)
    hand = best[0]
    reach_index = max(range(len(samples)), key=lambda i: samples[i][hand].y - samples[i]["pelvis"].y)
    return {"hand": hand, "peak_speed_time": round(best[1], 3), "peak_speed": round(best[2]), "max_reach_time": round(times[reach_index], 3)}


def check_clip(sequence, mesh, clip):
    height = mesh.get_bounds().box_extent.z * 2
    length = sequence.get_play_length()
    frames = max(2, int(length * 120))
    step = length / frames
    times = [length * i / frames for i in range(frames + 1)]
    options = make_evaluation_options(mesh)
    samples = [sample_pose(sequence, options, t) for t in times]
    report = {
        "skeleton": sequence.get_editor_property("skeleton").get_name(),
        "length": round(length, 3),
        "root_motion": sequence.get_editor_property("enable_root_motion"),
        "root_lock": sequence.get_editor_property("force_root_lock"),
        "flags": find_pose_flags(samples, height),
        "min_foot_z": round(min(min(s["foot_l"].z, s["foot_r"].z) for s in samples), 1),
        "pelvis_z": round(samples[0]["pelvis"].z, 1),
        "head_z": round(samples[0]["head"].z, 1),
    }
    if clip in LOCOMOTION_CLIPS:
        report["planted_foot_speed"] = measure_planted_foot_speed(samples, step, height)
        report["root_track_speed"] = round(distance(samples[-1]["root"], samples[0]["root"]) / length)
        report["pelvis_drift_y"] = round(samples[-1]["pelvis"].y - samples[0]["pelvis"].y, 1)
    if clip in ATTACK_CLIPS:
        report["hit"] = measure_hit_time(samples, times, step)
    return report


def main():
    source_rig = ensure_ik_rig("IK_TD_Manny", MANNY_MESH)
    if source_rig is None:
        log("FAIL: IK_TD_Manny unavailable")
        return
    to_save = [source_rig]
    checks = []
    for target_key, target in TARGETS.items():
        target_rig = ensure_ik_rig(f"IK_TD_{target_key}", target["mesh"])
        retargeter = ensure_retargeter(f"RTG_TD_MannyTo{target_key}", source_rig, target_rig, target["mesh"]) if target_rig else None
        if retargeter is None:
            log(f"FAIL: {target_key} rig or retargeter unavailable")
            continue
        to_save += [target_rig, retargeter]
        target_mesh = EAL.load_asset(target["mesh"])
        for clip in target["clips"]:
            sequence = retarget_clip(retargeter, target_key, target_mesh, clip)
            if sequence is None:
                continue
            to_save.append(sequence)
            checks.append((sequence, target_mesh, clip))
    saved = EAL.save_loaded_assets(to_save, False)
    log(f"saved {len(to_save)} assets: {saved}")
    for sequence, mesh, clip in checks:
        log(f"check {sequence.get_path_name().split('.')[0]} {check_clip(sequence, mesh, clip)}")


main()

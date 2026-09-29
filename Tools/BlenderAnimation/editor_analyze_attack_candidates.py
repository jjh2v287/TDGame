"""에디터 안에서 실행: 폴더 안 공격 AnimSequence를 30fps로 샘플링해 오른손 휘두름 방향·높이·양손 여부·루트 이동·발 스텝을 캐릭터 좌표(전방·오른쪽·위, cm)로 분류한다.
실행: python Tools/run_in_editor.py Tools/BlenderAnimation/editor_analyze_attack_candidates.py (폴더 변경: -c "TD_ANALYZE_FOLDER='/Game/...'; exec(open(...).read())", 기본 /Game/Characters/Paragon/Greystone/Attack)
출력: Saved/AgentOps/<날짜>/attack-candidates.json, 로그의 [TDTool] candidate 줄
상태: 현행 (2026-09-25, UE5 Manny 스켈레톤 전용)
"""
import json
import math
import os
import time

import unreal

FOLDER = globals().get("TD_ANALYZE_FOLDER", "/Game/Characters/Paragon/Greystone/Attack")
MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
FPS = 30.0
BONES = ["root", "pelvis", "spine_05", "head", "hand_r", "hand_l", "lowerarm_r", "foot_l", "foot_r", "ball_l", "ball_r"]


def vector_of(transform):
    location = transform.translation
    return (location.x, location.y, location.z)


def subtract(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def length(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def to_character(root_transform, world_point, right_sign):
    local = root_transform.inverse_transform_location(unreal.Vector(*world_point))
    return (local.y, right_sign * local.x, local.z)


def evaluation_options(mesh):
    options = unreal.AnimPoseEvaluationOptions()
    options.optional_skeletal_mesh = mesh
    options.evaluation_type = unreal.AnimDataEvalType.RAW
    options.should_retarget = False
    options.extract_root_motion = False
    options.incorporate_root_motion_into_pose = True
    return options


def sample_frames(animation, options):
    duration = unreal.AnimationLibrary.get_sequence_length(animation)
    frame_count = int(round(duration * FPS)) + 1
    frames = []
    for index in range(frame_count):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, min(index / FPS, duration), options)
        frames.append({bone: unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD) for bone in BONES})
    return duration, frames


def find_swing_window(speeds):
    peak_index = max(range(len(speeds)), key=lambda index: speeds[index])
    threshold = speeds[peak_index] * 0.35
    start = peak_index
    while start > 0 and speeds[start - 1] >= threshold:
        start -= 1
    end = peak_index
    while end < len(speeds) - 1 and speeds[end + 1] >= threshold:
        end += 1
    return start, peak_index, end


def describe_direction(delta_right, delta_up):
    horizontal = "R->L" if delta_right < 0 else "L->R"
    if abs(delta_up) < 0.35 * abs(delta_right):
        return horizontal + " flat"
    return horizontal + (" descending" if delta_up < 0 else " ascending")


def analyze(animation, options, right_sign):
    duration, frames = sample_frames(animation, options)
    start_root = frames[0]["root"]
    hand_path = [to_character(start_root, vector_of(frame["hand_r"]), right_sign) for frame in frames]
    speeds = [0.0] + [length(subtract(hand_path[i], hand_path[i - 1])) * FPS for i in range(1, len(hand_path))]
    swing_start, swing_peak, swing_end = find_swing_window(speeds)
    swing_delta = subtract(hand_path[swing_end], hand_path[swing_start])
    hand_gap = [length(subtract(vector_of(frame["hand_r"]), vector_of(frame["hand_l"]))) for frame in frames[swing_start:swing_end + 1]]
    root_end = to_character(start_root, vector_of(frames[-1]["root"]), right_sign)
    pelvis_start = to_character(start_root, vector_of(frames[0]["pelvis"]), right_sign)
    pelvis_end = to_character(start_root, vector_of(frames[-1]["pelvis"]), right_sign)
    feet = {}
    for bone in ("ball_l", "ball_r"):
        path = [to_character(start_root, vector_of(frame[bone]), right_sign) for frame in frames]
        feet[bone] = {
            "moved_cm": round(length(subtract(path[-1], path[0])), 1),
            "max_lift_cm": round(max(point[2] for point in path) - path[0][2], 1),
            "end_forward_cm": round(path[-1][0], 1),
        }
    return {
        "asset": animation.get_path_name().split(".")[0],
        "duration_s": round(duration, 3),
        "frames": len(frames),
        "root_motion_cm": [round(value, 1) for value in root_end],
        "pelvis_travel_cm": [round(value, 1) for value in subtract(pelvis_end, pelvis_start)],
        "swing_frames": [swing_start, swing_peak, swing_end],
        "swing_peak_speed_cm_s": round(speeds[swing_peak]),
        "swing_direction": describe_direction(swing_delta[1], swing_delta[2]),
        "hand_r_swing_start": [round(value, 1) for value in hand_path[swing_start]],
        "hand_r_swing_end": [round(value, 1) for value in hand_path[swing_end]],
        "hand_r_first": [round(value, 1) for value in hand_path[0]],
        "hand_r_last": [round(value, 1) for value in hand_path[-1]],
        "two_handed_min_gap_cm": round(min(hand_gap), 1),
        "feet": feet,
    }


def main():
    mesh = unreal.load_asset(MESH_PATH)
    options = evaluation_options(mesh)
    reference_pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.get_editor_property("skeleton"))
    hand_reference_x = unreal.AnimPoseExtensions.get_bone_pose(reference_pose, "hand_r", unreal.AnimPoseSpaces.WORLD).translation.x
    right_sign = 1.0 if hand_reference_x > 0 else -1.0
    results = []
    for asset_path in sorted(unreal.EditorAssetLibrary.list_assets(FOLDER, recursive=False)):
        animation = unreal.load_asset(asset_path)
        if not isinstance(animation, unreal.AnimSequence):
            continue
        if animation.get_editor_property("skeleton") != mesh.get_editor_property("skeleton"):
            unreal.log_warning(f"[TDTool] skip (skeleton differs) {asset_path}")
            continue
        result = analyze(animation, options, right_sign)
        results.append(result)
        unreal.log(
            f"[TDTool] candidate {result['asset'].split('/')[-1]} len={result['duration_s']} swing={result['swing_direction']} "
            f"frames={result['swing_frames']} start={result['hand_r_swing_start']} end={result['hand_r_swing_end']} "
            f"gap={result['two_handed_min_gap_cm']} root={result['root_motion_cm']}"
        )
    output_folder = os.path.join(unreal.Paths.project_saved_dir(), "AgentOps", time.strftime("%Y%m%d"))
    os.makedirs(output_folder, exist_ok=True)
    output_path = os.path.join(output_folder, "attack-candidates.json")
    with open(output_path, "w", encoding="utf-8") as handle:
        json.dump({"folder": FOLDER, "right_sign_mesh_x": right_sign, "axes": "forward,right,up cm relative to start root", "results": results}, handle, indent=1)
    unreal.log(f"[TDTool] wrote {output_path} ({len(results)} animations)")


main()

"""실행 환경: 언리얼 에디터 안(PythonScriptPlugin 원격 실행). 에셋은 읽기만 하고 저장하지 않는다.
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_measure_species_clips.py (PowerShell, L-repo-01). 입력·출력 바꾸기: -c "TD_MEASURE_INPUT='<json>'; TD_MEASURE_OUTPUT='<json>'; exec(open('C:/Project/TDGame/Tools/MonsterAI/editor_measure_species_clips.py', encoding='utf-8').read())"
출력: Saved/MonsterAI/species_measure.json — 메시(바운드·물리 에셋·정면 방향 단서)와 클립(길이·루트 이동·종 시험과 같은 루프 이음매·정지 꼬리·디딤발 지면 속도·빠른 뼈 순간)
상태: 현행(M3-33). 입력 형식 {"groups": [{"name": 종, "mesh": 경로, "clips": {역할: 경로}}]}, 기본 입력 Saved/MonsterAI/species_measure_input.json
"""
import json
import math
import os
import statistics
import time

import unreal

EDITOR_ASSETS = unreal.EditorAssetLibrary
POSE_LIBRARY = unreal.AnimPoseExtensions
ANIMATION_LIBRARY = unreal.AnimationLibrary
SAVED_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
INPUT_PATH = globals().get("TD_MEASURE_INPUT") or os.path.join(SAVED_DIR, "MonsterAI", "species_measure_input.json")
OUTPUT_PATH = globals().get("TD_MEASURE_OUTPUT") or os.path.join(SAVED_DIR, "MonsterAI", "species_measure.json")
STATIC_STEP_CM = 0.05
MINIMUM_INTERIOR_STEP_CM = 0.5
EXTREMITY_KEYWORDS = ("head", "jaw", "hand", "claw", "finger", "foot", "toe", "weapon", "sword", "bow", "staff")
IGNORED_KEYWORDS = ("tail", "ponytail", "twist", "ik_", "_end", "nub", "fx")
started = time.time()


def print_progress(message):
    print(f"[TDMeasure {time.time() - started:6.1f}s] {message}")


def make_pose_options():
    options = unreal.AnimPoseEvaluationOptions()
    options.extract_root_motion = False
    options.should_retarget = False
    raw_type = getattr(getattr(unreal, "AnimDataEvalType", None), "RAW", None)
    if raw_type is not None:
        options.evaluation_type = raw_type
    return options


POSE_OPTIONS = make_pose_options()


def subtract(left, right):
    return (left[0] - right[0], left[1] - right[1], left[2] - right[2])


def length(vector):
    return math.sqrt(vector[0] ** 2 + vector[1] ** 2 + vector[2] ** 2)


def read_bone_names(skeleton):
    reference = POSE_LIBRARY.get_reference_pose(skeleton)
    return [str(name) for name in POSE_LIBRARY.get_bone_names(reference)], reference


def read_pose_positions(pose, bones):
    points = []
    for bone in bones:
        position = POSE_LIBRARY.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
        points.append((position.x, position.y, position.z))
    return points


def measure_reference_pose(skeleton):
    bones, reference = read_bone_names(skeleton)
    points = read_pose_positions(reference, bones)
    root = points[0]
    relative = [subtract(point, root) for point in points]
    lows = [min(point[axis] for point in relative) for axis in range(3)]
    highs = [max(point[axis] for point in relative) for axis in range(3)]
    size = max(high - low for low, high in zip(lows, highs))
    head_index = next((index for index, bone in enumerate(bones) if "head" in bone.lower() and not any(word in bone.lower() for word in ("ponytail", "end", "nub"))), None)
    pelvis_index = next((index for index, bone in enumerate(bones) if any(word in bone.lower() for word in ("pelvis", "hips", "spine"))), 1 if len(bones) > 1 else 0)
    head_direction = None
    if head_index is not None:
        offset = subtract(relative[head_index], relative[pelvis_index])
        head_direction = {"bone": bones[head_index], "x": round(offset[0], 1), "y": round(offset[1], 1), "z": round(offset[2], 1)}
    return {"bones": len(bones), "pose_size_cm": round(size, 1), "low": [round(v, 1) for v in lows], "high": [round(v, 1) for v in highs],
            "head_from_pelvis": head_direction}, bones


def measure_mesh(mesh_path):
    mesh = EDITOR_ASSETS.load_asset(mesh_path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        return None, {"error": f"missing mesh {mesh_path}"}
    bounds = mesh.get_bounds()
    imported = mesh.get_imported_bounds()
    skeleton = mesh.get_editor_property("skeleton")
    physics = mesh.get_editor_property("physics_asset")
    reference, _ = measure_reference_pose(skeleton)
    report = {
        "path": mesh_path, "skeleton": skeleton.get_path_name() if skeleton else None,
        "physics_asset": physics.get_path_name() if physics else None,
        "bounds_origin": [round(bounds.origin.x, 1), round(bounds.origin.y, 1), round(bounds.origin.z, 1)],
        "bounds_extent": [round(bounds.box_extent.x, 1), round(bounds.box_extent.y, 1), round(bounds.box_extent.z, 1)],
        "imported_origin": [round(imported.origin.x, 1), round(imported.origin.y, 1), round(imported.origin.z, 1)],
        "imported_extent": [round(imported.box_extent.x, 1), round(imported.box_extent.y, 1), round(imported.box_extent.z, 1)],
        "reference_pose": reference,
    }
    return mesh, report


def sample_rows(sequence, bones, frame_count, play_length):
    rows, roots = [], []
    for frame in range(frame_count + 1):
        sample_time = play_length if frame == frame_count else play_length * frame / frame_count
        pose = POSE_LIBRARY.get_anim_pose_at_time(sequence, sample_time, POSE_OPTIONS)
        points = read_pose_positions(pose, bones)
        roots.append(points[0])
        rows.append([subtract(point, points[0]) for point in points])
    return rows, roots


def measure_seam(rows, bones, pose_size):
    frame_count = len(rows) - 1
    interior = [0.0] * len(bones)
    end_steps = [0.0] * len(bones)
    motion_end = 0
    for frame in range(1, frame_count + 1):
        steps = [length(subtract(rows[frame][index], rows[frame - 1][index])) for index in range(len(bones))]
        target = end_steps if frame == frame_count else interior
        for index in range(1, len(bones)):
            target[index] = max(target[index], steps[index])
        if max(steps[1:] or [0.0]) > STATIC_STEP_CM:
            motion_end = frame
    seam_offsets = [length(subtract(rows[0][index], rows[-1][index])) for index in range(len(bones))]
    seam_index = max(range(1, len(bones)), key=lambda index: seam_offsets[index]) if len(bones) > 1 else 0
    ratio, ratio_bone = 0.0, None
    for index in range(1, len(bones)):
        if interior[index] < MINIMUM_INTERIOR_STEP_CM:
            continue
        candidate = end_steps[index] / interior[index]
        if candidate > ratio:
            ratio, ratio_bone = candidate, bones[index]
    return {"motion_end_frame": motion_end, "static_tail": round((frame_count - motion_end) / frame_count, 4),
            "seam_cm": round(seam_offsets[seam_index], 3), "seam_percent": round(100.0 * seam_offsets[seam_index] / pose_size, 3) if pose_size > 0 else None,
            "seam_bone": bones[seam_index], "seam_step_ratio": round(ratio, 3), "seam_step_bone": ratio_bone}


def is_extremity(bone):
    lower = bone.lower()
    return any(word in lower for word in EXTREMITY_KEYWORDS) and not any(word in lower for word in IGNORED_KEYWORDS)


def measure_fast_bones(rows, bones, play_length):
    frame_count = len(rows) - 1
    step_seconds = play_length / frame_count
    peaks = []
    for index, bone in enumerate(bones):
        if index == 0 or not is_extremity(bone):
            continue
        best_speed, best_frame = 0.0, 0
        for frame in range(1, frame_count):
            speed = length(subtract(rows[frame + 1][index], rows[frame - 1][index])) / (2.0 * step_seconds)
            if speed > best_speed:
                best_speed, best_frame = speed, frame
        peaks.append((best_speed, bone, best_frame * step_seconds))
    peaks.sort(reverse=True)
    return [{"bone": bone, "peak_cm_s": round(speed), "time_s": round(seconds, 3)} for speed, bone, seconds in peaks[:6]]


def find_principal_axis(points):
    mean_x = sum(point[0] for point in points) / len(points)
    mean_y = sum(point[1] for point in points) / len(points)
    spread_xx = sum((point[0] - mean_x) ** 2 for point in points)
    spread_yy = sum((point[1] - mean_y) ** 2 for point in points)
    spread_xy = sum((point[0] - mean_x) * (point[1] - mean_y) for point in points)
    angle = 0.5 * math.atan2(2.0 * spread_xy, spread_xx - spread_yy)
    return math.cos(angle), math.sin(angle)


def measure_stance_speed(trajectory, axis, frames_per_second):
    along = [point[0] * axis[0] + point[1] * axis[1] for point in trajectory]
    heights = [point[2] for point in trajectory]
    count = len(along)
    velocities = [(along[(index + 1) % count] - along[(index - 1) % count]) * frames_per_second / 2.0 for index in range(count)]
    lowest, highest = min(heights), max(heights)
    threshold = lowest + max(1.5, 0.2 * (highest - lowest))
    planted = [height <= threshold for height in heights]
    swing = [velocities[index] for index in range(count) if not planted[index]]
    forward = math.copysign(1.0, statistics.median(swing)) if swing else 1.0
    backward = [velocities[index] for index in range(count) if planted[index] and velocities[index] * forward < 0]
    return -statistics.median(backward) * forward if backward else None


def find_ground_bones(reference_rows, bones):
    heights = [(reference_rows[index][2], index) for index in range(1, len(bones)) if not any(word in bones[index].lower() for word in ("tail", "ponytail", "ik_", "fx"))]
    heights.sort()
    if not heights:
        return []
    lowest = heights[0][0]
    span = max(row[2] for row in reference_rows) - lowest
    return [index for height, index in heights if height <= lowest + 0.06 * span][:8]


def measure_ground_speed(rows, bones, frames_per_second):
    ground = find_ground_bones(rows[0], bones)
    if not ground:
        return None, {}
    cycle = rows[:-1]
    stacked = []
    for index in ground:
        points = [row[index] for row in cycle]
        mean_x = sum(point[0] for point in points) / len(points)
        mean_y = sum(point[1] for point in points) / len(points)
        stacked.extend((point[0] - mean_x, point[1] - mean_y) for point in points)
    axis = find_principal_axis(stacked)
    speeds = {bones[index]: measure_stance_speed([row[index] for row in cycle], axis, frames_per_second) for index in ground}
    valid = [speed for speed in speeds.values() if speed is not None]
    return (round(statistics.median(valid), 1) if valid else None), {bone: (round(speed, 1) if speed is not None else None) for bone, speed in speeds.items()}


def measure_clip(role, clip_path, mesh, pose_size):
    sequence = EDITOR_ASSETS.load_asset(clip_path)
    if not isinstance(sequence, unreal.AnimSequence):
        return {"role": role, "path": clip_path, "error": "missing or not an AnimSequence"}
    skeleton = sequence.get_editor_property("skeleton")
    bones, _ = read_bone_names(skeleton)
    play_length = sequence.get_play_length()
    frame_count = max(ANIMATION_LIBRARY.get_num_frames(sequence), 1)
    frames_per_second = frame_count / play_length if play_length > 0 else 30.0
    rows, roots = sample_rows(sequence, bones, frame_count, play_length)
    root_travel = subtract(roots[-1], roots[0])
    report = {
        "role": role, "path": clip_path, "skeleton": skeleton.get_path_name() if skeleton else None,
        "same_skeleton_as_mesh": bool(mesh and mesh.get_editor_property("skeleton") == skeleton),
        "length_s": round(play_length, 4), "frames": frame_count, "fps": round(frames_per_second, 2),
        "loop": sequence.get_editor_property("loop"), "enable_root_motion": sequence.get_editor_property("enable_root_motion"),
        "force_root_lock": sequence.get_editor_property("force_root_lock"),
        "root_travel_cm": [round(value, 1) for value in root_travel],
        "seam": measure_seam(rows, bones, pose_size),
        "fast_bones": measure_fast_bones(rows, bones, play_length),
    }
    if role.lower().startswith(("walk", "run", "trot", "gallop")):
        report["ground_speed_cm_s"], report["ground_speed_bones"] = measure_ground_speed(rows, bones, frames_per_second)
    return report


def main():
    with open(INPUT_PATH, encoding="utf-8") as handle:
        request = json.load(handle)
    groups = []
    for group in request["groups"]:
        mesh, mesh_report = measure_mesh(group["mesh"]) if group.get("mesh") else (None, None)
        pose_size = mesh_report["reference_pose"]["pose_size_cm"] if mesh_report and "reference_pose" in mesh_report else 0.0
        clips = [measure_clip(role, path, mesh, pose_size) for role, path in group.get("clips", {}).items()]
        groups.append({"name": group["name"], "mesh": mesh_report, "clips": clips})
        print_progress(f"{group['name']}: {len(clips)} clips")
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as handle:
        json.dump({"groups": groups}, handle, ensure_ascii=False, indent=1)
    print_progress(f"wrote {OUTPUT_PATH}")


main()

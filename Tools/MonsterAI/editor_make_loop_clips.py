"""실행 환경: 언리얼 에디터 안(PythonScriptPlugin 원격 실행). Tools/MonsterAI/species_table.json 의 loop_clips 항목마다 원본(Fab 등)은 읽기만 하고 루프 복제본·종 자산(update_species_run_speed 항목만)·종 대기 클립(loop 속성만)을 저장한다.
실행 명령: python Tools/run_in_editor.py Tools/MonsterAI/editor_make_loop_clips.py (PowerShell, L-repo-01). 일부 종만: -c "TD_ONLY_SPECIES=['Hyena']; exec(open('C:/Project/TDGame/Tools/MonsterAI/editor_make_loop_clips.py', encoding='utf-8').read())"
출력: 표의 clips 대상 자산(예: /Game/MonsterAI/Animations/Hyena/AS_TD_Hyena_{Idle,Walk,Run}, 원본 동작 프레임 + C1 Hermite 틈 채우기), 종 자산 갱신, 종 대기 클립 loop 켜기, Saved/MonsterAI/loop_clips.json(entries 배열)
상태: 현행(M3-26, M3-33 표 전환, 재실행 안전: 계산 결과가 자산과 같으면 쓰기·저장을 건너뜀)
"""
import json
import importlib
import math
import os
import statistics
import sys
import time

import unreal

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
TABLE_TOOLS_DIR = os.path.join(PROJECT_DIR, "Tools", "MonsterAI")
if TABLE_TOOLS_DIR not in sys.path:
    sys.path.insert(0, TABLE_TOOLS_DIR)
import species_table

importlib.reload(species_table)

TABLE = species_table.load_table()
ONLY_SPECIES = globals().get("TD_ONLY_SPECIES")
EDITOR_ASSETS = unreal.EditorAssetLibrary
POSE_LIBRARY = unreal.AnimPoseExtensions
ANIMATION_LIBRARY = unreal.AnimationLibrary
SPECIES_FOLDER = TABLE["paths"]["species_folder"]
OUTPUT_PATH = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "MonsterAI", "loop_clips.json")
MAXIMUM_GAP_FRAMES = 12
MINIMUM_BOUNDARY_SPEED_CM = 0.2
STATIC_STEP_CM = 0.05
MINIMUM_INTERIOR_STEP_CM = 0.5
UNCHANGED_SOURCE_TOLERANCE_CM = 0.01
KEY_MATCH_TOLERANCE = 0.001
ROTATION_MATCH_TOLERANCE_RADIANS = 0.005
MINIMUM_PLAY_RATE = 0.5
MAXIMUM_PLAY_RATE = 2.0
started = time.time()


def print_progress(message):
    print(f"[TDLoopClips {time.time() - started:6.1f}s] {message}")


def make_pose_options():
    options = unreal.AnimPoseEvaluationOptions()
    options.extract_root_motion = False
    options.should_retarget = False
    raw_type = getattr(getattr(unreal, "AnimDataEvalType", None), "RAW", None)
    if raw_type is not None:
        options.evaluation_type = raw_type
    return options


POSE_OPTIONS = make_pose_options()


def add_vectors(left, right):
    return tuple(left[axis] + right[axis] for axis in range(3))


def subtract_vectors(left, right):
    return tuple(left[axis] - right[axis] for axis in range(3))


def scale_vector(vector, factor):
    return tuple(component * factor for component in vector)


def multiply_vectors(left, right):
    return tuple(left[axis] * right[axis] for axis in range(3))


def measure_distance(left, right):
    return math.sqrt(sum((left[axis] - right[axis]) ** 2 for axis in range(3)))


def multiply_quaternions(left, right):
    lx, ly, lz, lw = left
    rx, ry, rz, rw = right
    return (lw * rx + lx * rw + ly * rz - lz * ry,
            lw * ry - lx * rz + ly * rw + lz * rx,
            lw * rz + lx * ry - ly * rx + lz * rw,
            lw * rw - lx * rx - ly * ry - lz * rz)


def invert_quaternion(quaternion):
    return (-quaternion[0], -quaternion[1], -quaternion[2], quaternion[3])


def normalize_quaternion(quaternion):
    length = math.sqrt(sum(component * component for component in quaternion))
    return tuple(component / length for component in quaternion)


def align_to_shortest_path(quaternion):
    return tuple(-component for component in quaternion) if quaternion[3] < 0.0 else quaternion


def take_quaternion_logarithm(quaternion):
    x, y, z, w = align_to_shortest_path(normalize_quaternion(quaternion))
    vector_length = math.sqrt(x * x + y * y + z * z)
    if vector_length < 1e-12:
        return (2.0 * x, 2.0 * y, 2.0 * z)
    factor = 2.0 * math.atan2(vector_length, w) / vector_length
    return (x * factor, y * factor, z * factor)


def exponentiate_rotation_vector(rotation_vector):
    angle = math.sqrt(sum(component * component for component in rotation_vector))
    if angle < 1e-12:
        return normalize_quaternion((rotation_vector[0] * 0.5, rotation_vector[1] * 0.5, rotation_vector[2] * 0.5, 1.0))
    factor = math.sin(angle * 0.5) / angle
    return (rotation_vector[0] * factor, rotation_vector[1] * factor, rotation_vector[2] * factor, math.cos(angle * 0.5))


def rotate_vector(quaternion, vector):
    rotated = multiply_quaternions(multiply_quaternions(quaternion, (vector[0], vector[1], vector[2], 0.0)), invert_quaternion(quaternion))
    return rotated[:3]


def interpolate_hermite(start, start_tangent, end, end_tangent, parameter):
    squared = parameter * parameter
    cubed = squared * parameter
    start_weight = 2.0 * cubed - 3.0 * squared + 1.0
    start_tangent_weight = cubed - 2.0 * squared + parameter
    end_weight = -2.0 * cubed + 3.0 * squared
    end_tangent_weight = cubed - squared
    return tuple(start[axis] * start_weight + start_tangent[axis] * start_tangent_weight + end[axis] * end_weight + end_tangent[axis] * end_tangent_weight for axis in range(3))


def fill_loop_gap(frames, gap_frames):
    last = len(frames) - 1
    span = gap_frames + 1
    first_position, first_rotation, first_scale = frames[0]
    end_position, end_rotation, end_scale = frames[last]
    position_end_tangent = scale_vector(subtract_vectors(end_position, frames[last - 1][0]), span)
    position_start_tangent = scale_vector(subtract_vectors(frames[1][0], first_position), span)
    scale_end_tangent = scale_vector(subtract_vectors(end_scale, frames[last - 1][2]), span)
    scale_start_tangent = scale_vector(subtract_vectors(frames[1][2], first_scale), span)
    relative_first_rotation = multiply_quaternions(invert_quaternion(end_rotation), first_rotation)
    rotation_target = take_quaternion_logarithm(relative_first_rotation)
    rotation_end_tangent = scale_vector(take_quaternion_logarithm(multiply_quaternions(invert_quaternion(frames[last - 1][1]), end_rotation)), span)
    first_step = take_quaternion_logarithm(multiply_quaternions(invert_quaternion(first_rotation), frames[1][1]))
    rotation_start_tangent = scale_vector(rotate_vector(normalize_quaternion(relative_first_rotation), first_step), span)
    filled = list(frames)
    for gap_index in range(1, gap_frames + 1):
        parameter = gap_index / span
        position = interpolate_hermite(end_position, position_end_tangent, first_position, position_start_tangent, parameter)
        scale = interpolate_hermite(end_scale, scale_end_tangent, first_scale, scale_start_tangent, parameter)
        rotation_offset = interpolate_hermite((0.0, 0.0, 0.0), rotation_end_tangent, rotation_target, rotation_start_tangent, parameter)
        rotation = normalize_quaternion(multiply_quaternions(end_rotation, exponentiate_rotation_vector(rotation_offset)))
        filled.append((position, rotation, scale))
    filled.append(frames[0])
    return filled


def read_skeleton_bones(animation):
    reference = POSE_LIBRARY.get_reference_pose(animation.get_editor_property("skeleton"))
    bones = [str(name) for name in POSE_LIBRARY.get_bone_names(reference)]
    parents = {}
    for bone in bones:
        path = [str(name) for name in ANIMATION_LIBRARY.find_bone_path_to_root(animation, bone)]
        parents[bone] = path[1] if len(path) > 1 else None
    reference_locals = {}
    for bone in bones:
        transform = POSE_LIBRARY.get_bone_pose(reference, bone, unreal.AnimPoseSpaces.LOCAL)
        reference_locals[bone] = convert_transform(transform)
    return bones, parents, reference_locals


def convert_transform(transform):
    position, rotation, scale = transform.translation, transform.rotation, transform.scale3d
    return ((position.x, position.y, position.z), normalize_quaternion((rotation.x, rotation.y, rotation.z, rotation.w)), (scale.x, scale.y, scale.z))


def read_local_keys(animation, tracks, last_frame):
    keys = {track: [] for track in tracks}
    for frame in range(last_frame + 1):
        pose = POSE_LIBRARY.get_anim_pose_at_frame(animation, frame, POSE_OPTIONS)
        for track in tracks:
            keys[track].append(convert_transform(POSE_LIBRARY.get_bone_pose(pose, track, unreal.AnimPoseSpaces.LOCAL)))
    return keys


def compute_root_relative_positions(keys, frame, bones, parents, reference_locals):
    world = {}
    for bone in bones:
        local = keys[bone][frame] if bone in keys else reference_locals[bone]
        parent = parents[bone]
        if parent is None:
            world[bone] = local
            continue
        parent_position, parent_rotation, parent_scale = world[parent]
        position = add_vectors(parent_position, rotate_vector(parent_rotation, multiply_vectors(parent_scale, local[0])))
        world[bone] = (position, multiply_quaternions(parent_rotation, local[1]), multiply_vectors(parent_scale, local[2]))
    root = world[bones[0]][0]
    return [subtract_vectors(world[bone][0], root) for bone in bones]


def read_root_relative_positions(animation, bones, frames):
    rows = []
    for frame in frames:
        pose = POSE_LIBRARY.get_anim_pose_at_frame(animation, frame, POSE_OPTIONS)
        points = []
        for bone in bones:
            position = POSE_LIBRARY.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
            points.append((position.x, position.y, position.z))
        rows.append([subtract_vectors(point, points[0]) for point in points])
    return rows


def is_body_bone(bone, helper_prefixes):
    return not bone.startswith(tuple(helper_prefixes)) and "ponytail" not in bone and not bone.endswith("_end")


def measure_largest_steps(rows, first_frame, last_frame, bone_count):
    largest = [0.0] * bone_count
    for frame in range(first_frame + 1, last_frame + 1):
        for bone_index in range(1, bone_count):
            largest[bone_index] = max(largest[bone_index], measure_distance(rows[frame - 1][bone_index], rows[frame][bone_index]))
    return largest


def score_gap(gap_steps, interior_steps, bones, body_only, helper_prefixes):
    score = 0.0
    for bone_index in range(1, len(bones)):
        if interior_steps[bone_index] <= STATIC_STEP_CM:
            continue
        if body_only and not is_body_bone(bones[bone_index], helper_prefixes):
            continue
        score = max(score, gap_steps[bone_index] / interior_steps[bone_index])
    return score


def find_weighted_median(values, weights):
    pairs = sorted(zip(values, weights))
    half_weight = sum(weights) * 0.5
    cumulative_weight = 0.0
    for value, weight in pairs:
        cumulative_weight += weight
        if cumulative_weight >= half_weight:
            return value
    return pairs[-1][0]


def round_half_up(value):
    return int(math.floor(value + 0.5))


def find_quartiles(values):
    if len(values) < 2:
        return [round(values[0], 2)] * 3
    return [round(quartile, 2) for quartile in statistics.quantiles(sorted(values), n=4, method="inclusive")]


def measure_boundary_speed(rows, bone_index, motion_end_frame):
    end_step = measure_distance(rows[motion_end_frame - 1][bone_index], rows[motion_end_frame][bone_index])
    start_step = measure_distance(rows[0][bone_index], rows[1][bone_index])
    return (end_step + start_step) * 0.5


def estimate_gap_frames(original_rows, bones, motion_end_frame, helper_prefixes):
    needed_frames, weights = [], []
    for bone_index in range(1, len(bones)):
        if not is_body_bone(bones[bone_index], helper_prefixes):
            continue
        boundary_speed = measure_boundary_speed(original_rows, bone_index, motion_end_frame)
        if boundary_speed < MINIMUM_BOUNDARY_SPEED_CM:
            continue
        gap_distance = measure_distance(original_rows[0][bone_index], original_rows[motion_end_frame][bone_index])
        needed_frames.append(gap_distance / boundary_speed)
        weights.append(boundary_speed)
    if not needed_frames:
        return MAXIMUM_GAP_FRAMES, {"gap_basis": "정지 경계 대체", "rule": "no body bone reaches the boundary speed limit, so the longest gap is used",
                                    "bone_count": 0, "needed_frames_weighted_median": None, "needed_frames_quartiles": None, "needed_frames_range": None}
    weighted_median = find_weighted_median(needed_frames, weights)
    gap_frames = min(max(round_half_up(weighted_median) - 1, 1), MAXIMUM_GAP_FRAMES)
    return gap_frames, {"gap_basis": "속도 일치 추정", "rule": "clamp(round_half_up(weighted_median(gap_distance / boundary_speed, weight boundary_speed)) - 1, 1, 12)",
                        "bone_count": len(needed_frames), "needed_frames_weighted_median": round(weighted_median, 3),
                        "needed_frames_quartiles": find_quartiles(needed_frames),
                        "needed_frames_range": [round(min(needed_frames), 2), round(max(needed_frames), 2)]}


def fill_tracks(keys, gap_frames):
    return {track: fill_loop_gap(frames, gap_frames) for track, frames in keys.items()}


def compute_gap_rows(keys, gap_frames, bones, parents, reference_locals, motion_end_frame):
    filled = fill_tracks(keys, gap_frames)
    return [compute_root_relative_positions(filled, frame, bones, parents, reference_locals) for frame in range(motion_end_frame, motion_end_frame + gap_frames + 2)]


def pick_extreme(rows, key, use_largest):
    candidates = [row for row in rows if row[key] is not None]
    if not candidates:
        return None
    chosen = max(candidates, key=lambda row: row[key]) if use_largest else min(candidates, key=lambda row: row[key])
    return {"bone": chosen["bone"], key: chosen[key]}


def measure_gap_speed_ratios(gap_rows, original_rows, interior_steps, bones, motion_end_frame, helper_prefixes):
    rows = []
    for bone_index in range(1, len(bones)):
        if not is_body_bone(bones[bone_index], helper_prefixes) or interior_steps[bone_index] <= STATIC_STEP_CM:
            continue
        steps = [measure_distance(gap_rows[frame - 1][bone_index], gap_rows[frame][bone_index]) for frame in range(1, len(gap_rows))]
        boundary_speed = measure_boundary_speed(original_rows, bone_index, motion_end_frame)
        mean_ratio = (sum(steps) / len(steps)) / boundary_speed if boundary_speed >= MINIMUM_BOUNDARY_SPEED_CM else None
        rows.append({"bone": bones[bone_index], "largest_step_over_interior_max": round(max(steps) / interior_steps[bone_index], 3),
                     "mean_step_over_boundary_speed": round(mean_ratio, 3) if mean_ratio is not None else None})
    return {"body_bone_count": len(rows),
            "largest_step_over_interior_max_highest": pick_extreme(rows, "largest_step_over_interior_max", True),
            "largest_step_over_interior_max_lowest": pick_extreme(rows, "largest_step_over_interior_max", False),
            "mean_step_over_boundary_speed_highest": pick_extreme(rows, "mean_step_over_boundary_speed", True),
            "mean_step_over_boundary_speed_lowest": pick_extreme(rows, "mean_step_over_boundary_speed", False)}


def choose_gap_frames(keys, bones, parents, reference_locals, motion_end_frame, helper_prefixes):
    original_rows = [compute_root_relative_positions(keys, frame, bones, parents, reference_locals) for frame in range(motion_end_frame + 1)]
    interior_steps = measure_largest_steps(original_rows, 0, motion_end_frame, len(bones))
    gap_frames, estimate = estimate_gap_frames(original_rows, bones, motion_end_frame, helper_prefixes)
    search = []
    for candidate in range(1, MAXIMUM_GAP_FRAMES + 1):
        gap_steps = measure_largest_steps(compute_gap_rows(keys, candidate, bones, parents, reference_locals, motion_end_frame), 0, candidate + 1, len(bones))
        search.append({"gap_frames": candidate,
                       "body_score": round(score_gap(gap_steps, interior_steps, bones, True, helper_prefixes), 3),
                       "all_score": round(score_gap(gap_steps, interior_steps, bones, False, helper_prefixes), 3)})
    chosen = next(row for row in search if row["gap_frames"] == gap_frames)
    gap_rows = compute_gap_rows(keys, gap_frames, bones, parents, reference_locals, motion_end_frame)
    estimate.update({"gap_frames": gap_frames, "body_score": chosen["body_score"], "all_score": chosen["all_score"], "gap_search_scores_report_only": search,
                     "gap_speed": measure_gap_speed_ratios(gap_rows, original_rows, interior_steps, bones, motion_end_frame, helper_prefixes)})
    return estimate


def find_seam_step_ratios(rows, bones):
    last = len(rows) - 1
    interior_steps = measure_largest_steps(rows, 0, last - 1, len(bones))
    ratios = []
    for bone_index in range(1, len(bones)):
        if interior_steps[bone_index] < MINIMUM_INTERIOR_STEP_CM:
            continue
        end_step = measure_distance(rows[last - 1][bone_index], rows[last][bone_index])
        ratios.append({"bone": bones[bone_index], "ratio": round(end_step / interior_steps[bone_index], 3),
                       "end_step_cm": round(end_step, 3), "interior_max_step_cm": round(interior_steps[bone_index], 3)})
    return sorted(ratios, key=lambda row: -row["ratio"])[:5]


def find_largest_bone_offset(left_row, right_row, bones):
    largest, largest_bone = 0.0, None
    for bone_index in range(1, len(bones)):
        offset = measure_distance(left_row[bone_index], right_row[bone_index])
        if offset > largest:
            largest, largest_bone = offset, bones[bone_index]
    return largest, largest_bone


def measure_static_tail(rows, bones):
    motion_end = 0
    for frame in range(1, len(rows)):
        if find_largest_bone_offset(rows[frame - 1], rows[frame], bones)[0] > STATIC_STEP_CM:
            motion_end = frame
    frame_count = len(rows) - 1
    return motion_end, (frame_count - motion_end) / frame_count


def measure_foot_heights(rows, bones, motion_end_frame, feet):
    heights = {}
    for bone in feet:
        if bone not in bones:
            continue
        index = bones.index(bone)
        original_lowest = min(row[index][2] for row in rows[:motion_end_frame + 1])
        gap_rows = rows[motion_end_frame + 1:-1]
        gap_lowest = min(row[index][2] for row in gap_rows) if gap_rows else None
        heights[bone] = {"original_lowest_cm": round(original_lowest, 2), "gap_lowest_cm": round(gap_lowest, 2) if gap_lowest is not None else None,
                         "gap_below_original_cm": round(original_lowest - gap_lowest, 2) if gap_lowest is not None else None}
    return heights


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
    is_planted = [height <= threshold for height in heights]
    swing_velocities = [velocities[index] for index in range(count) if not is_planted[index]]
    forward = math.copysign(1.0, statistics.median(swing_velocities)) if swing_velocities else 1.0
    backward = [velocities[index] for index in range(count) if is_planted[index] and velocities[index] * forward < 0]
    return -statistics.median(backward) * forward if backward else None


def measure_ground_speed(rows, bones, frames_per_second, feet, feet_tips):
    cycle = rows[:-1]
    names = [bone for bone in feet + feet_tips if bone in bones]
    indices = {bone: bones.index(bone) for bone in names}
    stacked = []
    for bone in names:
        points = [row[indices[bone]] for row in cycle]
        mean_x = sum(point[0] for point in points) / len(points)
        mean_y = sum(point[1] for point in points) / len(points)
        stacked.extend((point[0] - mean_x, point[1] - mean_y) for point in points)
    axis = find_principal_axis(stacked)
    speeds = {bone: measure_stance_speed([row[indices[bone]] for row in cycle], axis, frames_per_second) for bone in feet if bone in indices}
    valid = [speed for speed in speeds.values() if speed is not None]
    return (statistics.median(valid) if valid else None), {bone: (round(speed, 1) if speed is not None else None) for bone, speed in speeds.items()}


def are_keys_equal(left, right):
    for left_key, right_key in zip(left, right):
        if measure_distance(left_key[0], right_key[0]) > KEY_MATCH_TOLERANCE or measure_distance(left_key[2], right_key[2]) > KEY_MATCH_TOLERANCE:
            return False
        dot = abs(sum(left_key[1][axis] * right_key[1][axis] for axis in range(4)))
        if 2.0 * math.acos(min(1.0, dot)) > ROTATION_MATCH_TOLERANCE_RADIANS:
            return False
    return len(left) == len(right)


def is_target_current(target, tracks, filled, frame_count):
    if ANIMATION_LIBRARY.get_num_frames(target) != frame_count:
        return False
    if not target.get_editor_property("loop") or target.get_editor_property("enable_root_motion"):
        return False
    current = read_local_keys(target, tracks, frame_count)
    return all(are_keys_equal(current[track], filled[track]) for track in tracks)


def bind_controller(animation):
    controller = animation.get_editor_property("controller")
    model = animation.get_editor_property("data_model_interface")
    if controller is None or model is None:
        raise RuntimeError(f"{animation.get_path_name()}: controller or data model missing")
    is_rebound = controller.get_model_interface() != model
    if is_rebound:
        controller.set_model(model)
    return controller, model, is_rebound


def write_keys(animation, filled, frame_count):
    controller, model, is_rebound = bind_controller(animation)
    controller.open_bracket(unreal.Text("TD loop clip"), False)
    if model.get_number_of_frames() != frame_count:
        controller.set_number_of_frames(unreal.FrameNumber(frame_count), False)
    for track, frames in filled.items():
        positions = [unreal.Vector(*position) for position, _, _ in frames]
        rotations = [unreal.Quat(*rotation) for _, rotation, _ in frames]
        scales = [unreal.Vector(*scale) for _, _, scale in frames]
        if not controller.set_bone_track_keys(track, positions, rotations, scales, False):
            raise RuntimeError(f"{animation.get_path_name()}: set_bone_track_keys failed for {track}")
    controller.close_bracket(False)
    animation.set_editor_property("loop", True)
    animation.set_editor_property("enable_root_motion", False)
    return is_rebound


def load_or_duplicate_target(spec, target_folder):
    if not EDITOR_ASSETS.does_directory_exist(target_folder):
        EDITOR_ASSETS.make_directory(target_folder)
    is_created = not EDITOR_ASSETS.does_asset_exist(spec["target"])
    if is_created and EDITOR_ASSETS.duplicate_asset(spec["source"], spec["target"]) is None:
        raise RuntimeError(f"duplicate failed {spec['source']} -> {spec['target']}")
    return EDITOR_ASSETS.load_asset(spec["target"]), is_created


def make_clip(spec, entry):
    source = EDITOR_ASSETS.load_asset(spec["source"])
    if source is None:
        raise RuntimeError(f"missing source {spec['source']}")
    source_model = source.get_editor_property("data_model_interface")
    frames_per_second = source_model.get_frame_rate().numerator / source_model.get_frame_rate().denominator
    tracks = [str(track) for track in source_model.get_bone_track_names()]
    bones, parents, reference_locals = read_skeleton_bones(source)
    motion_end_frame = spec["motion_end_frame"]
    keys = read_local_keys(source, tracks, motion_end_frame)
    gap_choice = choose_gap_frames(keys, bones, parents, reference_locals, motion_end_frame, entry["helper_prefixes"])
    gap_frames = gap_choice["gap_frames"]
    filled = fill_tracks(keys, gap_frames)
    frame_count = motion_end_frame + gap_frames + 1

    target, is_created = load_or_duplicate_target(spec, entry["target_folder"])
    is_written = is_created or not is_target_current(target, tracks, filled, frame_count)
    is_rebound = False
    if is_written:
        is_rebound = write_keys(target, filled, frame_count)
        EDITOR_ASSETS.save_loaded_asset(target, False)

    target_frames = ANIMATION_LIBRARY.get_num_frames(target)
    new_rows = read_root_relative_positions(target, bones, range(target_frames + 1))
    source_rows = read_root_relative_positions(source, bones, range(motion_end_frame + 1))
    unchanged_offset = max(find_largest_bone_offset(source_rows[frame], new_rows[frame], bones)[0] for frame in range(motion_end_frame + 1))
    seam, seam_bone = find_largest_bone_offset(new_rows[0], new_rows[-1], bones)
    motion_end, tail_fraction = measure_static_tail(new_rows, bones)
    ground_speed, feet_speeds = measure_ground_speed(new_rows, bones, frames_per_second, entry["feet"], entry["feet_tips"]) if spec["role"] != "idle" else (None, {})
    result = {
        "role": spec["role"], "source": spec["source"], "target": spec["target"], "created": is_created, "written_and_saved": is_written,
        "controller_rebound": is_rebound, "closing": "c1_hermite_gap_fill", "motion_end_frame": motion_end_frame,
        "gap_frames": gap_frames, "gap_choice": gap_choice,
        "motion_length_s": round(motion_end_frame / frames_per_second, 4),
        "period_growth_ratio": round((motion_end_frame + gap_frames + 1) / motion_end_frame - 1.0, 4),
        "track_count": len(tracks), "fps": frames_per_second,
        "length_s": round(target.get_play_length(), 4), "num_frames": target_frames, "num_keys": ANIMATION_LIBRARY.get_num_keys(target),
        "loop": target.get_editor_property("loop"), "enable_root_motion": target.get_editor_property("enable_root_motion"),
        "source_frames_max_offset_cm": round(unchanged_offset, 4), "source_frames_unchanged": unchanged_offset <= UNCHANGED_SOURCE_TOLERANCE_CM,
        "seam_cm": round(seam, 4), "seam_bone": seam_bone,
        "seam_step_ratio_top5": find_seam_step_ratios(new_rows, bones),
        "feet_lowest_height": measure_foot_heights(new_rows, bones, motion_end_frame, entry["feet"]),
        "ground_speed_four_feet_median_cm_s": round(ground_speed, 1) if ground_speed is not None else None, "ground_speed_feet_cm_s": feet_speeds,
        "motion_end_frame_after": motion_end, "static_tail_fraction": round(tail_fraction, 4),
    }
    print_progress(f"{spec['target']}: gap {gap_frames} ({gap_choice['gap_basis']}, needed median {gap_choice['needed_frames_weighted_median']}, body score {gap_choice['body_score']}), frames {target_frames}, seam {result['seam_cm']} cm, "
                   f"source offset {result['source_frames_max_offset_cm']} cm, written {is_written}")
    return target, result


def compute_run_threshold(walk_speed, run_speed):
    walk_top_speed = walk_speed * MAXIMUM_PLAY_RATE
    run_bottom_speed = run_speed * MINIMUM_PLAY_RATE
    if walk_top_speed < run_bottom_speed:
        return math.sqrt(walk_top_speed * run_bottom_speed)
    return (walk_speed + run_speed) * 0.5


def load_species_assets():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    species_assets = []
    for asset_data in sorted(registry.get_assets_by_path(SPECIES_FOLDER, True), key=lambda data: str(data.asset_name)):
        asset = EDITOR_ASSETS.load_asset(str(asset_data.package_name))
        if isinstance(asset, unreal.TDMonsterSpeciesAsset):
            species_assets.append((str(asset_data.asset_name), asset))
    return species_assets


def collect_run_thresholds():
    rows = []
    for name, asset in load_species_assets():
        scale = asset.get_editor_property("mesh_scale")
        walk_speed = asset.get_editor_property("walk_clip_speed") * scale
        run_speed = asset.get_editor_property("run_clip_speed") * scale
        rows.append({"species": name, "walk_cm_s": round(walk_speed, 2), "run_cm_s": round(run_speed, 2),
                     "midpoint_threshold": round((walk_speed + run_speed) * 0.5, 1), "new_threshold": round(compute_run_threshold(walk_speed, run_speed), 1)})
    return rows


def describe_clip(clip):
    animation = clip.get_editor_property("animation")
    return {"animation": animation.get_path_name() if animation else None, "start_seconds": clip.get_editor_property("start_seconds"),
            "end_seconds": clip.get_editor_property("end_seconds"), "play_rate": clip.get_editor_property("play_rate")}


def update_species_locomotion(entry, targets, run_clip_speed):
    if not entry["update_species_run_speed"]:
        return {}, False
    if run_clip_speed is None:
        raise RuntimeError(f"{entry['species']}: update_species_run_speed needs a run clip")
    species_path = species_table.species_asset_path(TABLE, entry["species"])
    species = EDITOR_ASSETS.load_asset(species_path)
    before = {"run_clip_speed": species.get_editor_property("run_clip_speed")}
    is_changed = before["run_clip_speed"] != float(run_clip_speed)
    for role, animation in targets.items():
        clip = species.get_editor_property(role)
        before[role] = describe_clip(clip)
        desired = {"animation": animation.get_path_name(), "start_seconds": 0.0, "end_seconds": 0.0, "play_rate": 1.0}
        if before[role] == desired:
            continue
        is_changed = True
        clip.set_editor_property("animation", animation)
        clip.set_editor_property("start_seconds", 0.0)
        clip.set_editor_property("end_seconds", 0.0)
        clip.set_editor_property("play_rate", 1.0)
        species.set_editor_property(role, clip)
    if is_changed:
        species.set_editor_property("run_clip_speed", float(run_clip_speed))
        EDITOR_ASSETS.save_loaded_asset(species, False)
        print_progress(f"saved {species_path}: run_clip_speed {before['run_clip_speed']} -> {run_clip_speed}")
    return before, is_changed


def enable_idle_loops():
    rows, seen_paths = [], set()
    for name, asset in load_species_assets():
        animation = asset.get_editor_property("idle").get_editor_property("animation")
        if not isinstance(animation, unreal.AnimSequence):
            continue
        path = animation.get_path_name()
        loop_before = animation.get_editor_property("loop")
        is_saved = not loop_before and path not in seen_paths
        if is_saved:
            animation.set_editor_property("loop", True)
            EDITOR_ASSETS.save_loaded_asset(animation, False)
            print_progress(f"idle loop enabled: {path}")
        seen_paths.add(path)
        rows.append({"species": name, "idle": path, "loop_before": loop_before, "saved": is_saved, "loop_after": animation.get_editor_property("loop")})
    return rows


def process_entry(entry):
    targets, results = {}, []
    for spec in entry["clips"]:
        target, result = make_clip(spec, entry)
        targets[spec["role"]] = target
        results.append(result)
    run_result = next((result for result in results if result["role"] == "run"), None)
    run_clip_speed = int(round(run_result["ground_speed_four_feet_median_cm_s"])) if run_result else None
    species_before, is_species_saved = update_species_locomotion(entry, targets, run_clip_speed)
    return {
        "species": entry["species"], "clips": results, "run_clip_speed": run_clip_speed, "species_before": species_before, "species_saved": is_species_saved,
        "all_seams_zero": all(result["seam_cm"] <= UNCHANGED_SOURCE_TOLERANCE_CM for result in results),
        "all_source_frames_unchanged": all(result["source_frames_unchanged"] for result in results),
    }


def main():
    thresholds_before = collect_run_thresholds()
    entry_reports = [process_entry(entry) for entry in TABLE["loop_clips"] if not ONLY_SPECIES or entry["species"] in ONLY_SPECIES]
    idle_loops = enable_idle_loops()
    saved_count = sum(1 for report in entry_reports for result in report["clips"] if result["written_and_saved"]) + sum(1 for report in entry_reports if report["species_saved"]) + sum(1 for row in idle_loops if row["saved"])
    report = {
        "entries": entry_reports, "idle_loops": idle_loops, "saved_asset_count": saved_count,
        "run_thresholds_before_species_update": thresholds_before, "run_thresholds_after_species_update": collect_run_thresholds(),
        "all_seams_zero": all(entry_report["all_seams_zero"] for entry_report in entry_reports),
        "all_source_frames_unchanged": all(entry_report["all_source_frames_unchanged"] for entry_report in entry_reports),
        "dirty_packages_after": [package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    }
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    with open(OUTPUT_PATH, "w", encoding="utf-8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=1)
    print_progress(f"wrote {OUTPUT_PATH}; saved {saved_count}; seams zero {report['all_seams_zero']}; source unchanged {report['all_source_frames_unchanged']}; dirty {report['dirty_packages_after']}")


main()

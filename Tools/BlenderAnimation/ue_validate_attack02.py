# File: Tools/BlenderAnimation/ue_validate_attack02.py
import json
import math
import sys
from pathlib import Path
import unreal

ROOT = Path(r"C:\Project\TDGame")
JSON_PATH = ROOT / "AnimationSources/Player/AS_TD_Player_Attack02_SwordSlash_LToR.json"
ANIM_PATH = "/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack02_SwordSlash_LToR"
MESH_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
OUT_PATH = ROOT / "Docs/Validation/BlenderAnimation/sword-slash-ltor-validation.json"

source = json.loads(JSON_PATH.read_text(encoding="utf-8"))
records = source["records"]
fps = source["summary"]["fps"]

anim = unreal.EditorAssetLibrary.load_asset(ANIM_PATH)
if not anim:
    raise RuntimeError(f"Animation not found: {ANIM_PATH}")

mesh = unreal.EditorAssetLibrary.load_asset(MESH_PATH)
if not mesh:
    raise RuntimeError(f"Mesh not found: {MESH_PATH}")

bones = ["root", "pelvis", "hand_r", "hand_l", "foot_r", "foot_l", "ball_r", "ball_l"]

options = unreal.AnimPoseEvaluationOptions()
options.optional_skeletal_mesh = mesh
options.evaluation_type = unreal.AnimDataEvalType.RAW
options.should_retarget = False
options.extract_root_motion = False
options.incorporate_root_motion_into_pose = True

samples = []
for row in records:
    time = row["frame"] / fps
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, time, options)
    bone_data = {}
    for bone in bones:
        t = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
        bone_data[bone] = [t.x, t.y, t.z]
    samples.append({"frame": row["frame"], "time": time, "bones": bone_data})

errors = {}
for row, sample in zip(records, samples):
    root_fwd = row["root_fwd_cm"]
    expected = {
        "pelvis": row["pelvis"],
        "hand_r": row["hand_r"],
        "hand_l": row["hand_l"],
        "foot_r": row["feet"]["r"]["ankle"],
        "foot_l": row["feet"]["l"]["ankle"],
        "ball_r": row["feet"]["r"]["ball"],
        "ball_l": row["feet"]["l"]["ball"]
    }
    root = sample["bones"]["root"]
    errors.setdefault("root_fwd", []).append(abs(root[1] - root_fwd))
    for bone, point in expected.items():
        observed = sample["bones"][bone]
        unreal_point = [-point[1], point[0], point[2]]
        errors.setdefault(bone, []).append(math.dist(unreal_point, observed))

ball_l = [sample["bones"]["ball_l"] for sample in samples]
ball_r = [sample["bones"]["ball_r"] for sample in samples]
roots = [sample["bones"]["root"] for sample in samples]

plant_r = max(math.dist(point[:2], ball_r[14][:2]) for point in ball_r[14:])
plant_l_early = max(math.dist(point[:2], ball_l[0][:2]) for point in ball_l[:21])
plant_l_late = max(math.dist(point[:2], ball_l[27][:2]) for point in ball_l[27:])

report = {
    "animation": ANIM_PATH,
    "fps": fps,
    "sample_count": len(records),
    "duration_seconds": (len(records) - 1) / fps,
    "max_blender_unreal_bone_position_error_cm": {bone: round(max(values), 4) for bone, values in errors.items()},
    "root_travel_cm": round(math.dist(roots[0], roots[-1]), 3),
    "expected_step_cm": source["summary"]["step_cm"],
    "right_ball_plant_xy_drift_cm": {"frames_14_39": round(plant_r, 4)},
    "left_ball_plant_xy_drift_cm": {"frames_0_20": round(plant_l_early, 4), "frames_27_39": round(plant_l_late, 4)},
    "sword_tip_min_height_cm": round(min(row["sword_tip"][2] for row in records), 2),
    "max_wrist_twist_deg": source["summary"]["max_wrist_twist_deg"],
    "max_ik_error_cm": max(source["summary"]["max_right_arm_error_cm"], source["summary"]["max_left_arm_error_cm"], source["summary"]["max_leg_error_cm"]),
    "visual_review_is_separate": True,
    "user_visual_approval": False
}

report["technical_checks_passed"] = bool(
    max(report["max_blender_unreal_bone_position_error_cm"].values()) < 0.15 and
    abs(report["root_travel_cm"] - report["expected_step_cm"]) < 0.1 and
    report["right_ball_plant_xy_drift_cm"]["frames_14_39"] < 0.15 and
    report["left_ball_plant_xy_drift_cm"]["frames_27_39"] < 0.15 and
    report["sword_tip_min_height_cm"] > 10.0
)

OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
OUT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
print("VALIDATION_REPORT:")
print(json.dumps(report, indent=2))

if not report["technical_checks_passed"]:
    sys.exit(1)

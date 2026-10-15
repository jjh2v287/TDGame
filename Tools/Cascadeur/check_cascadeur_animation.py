"""시스템 Python에서 실행: Cascadeur가 푼 애니메이션(현재 열린 .casc)의 모든 프레임 관절 변환을 읽어 칼(무기 프로필)을 손에 붙인 위·정면 막대 인형 GIF·접촉 시트와 칼끝 궤적·속도·수평도·회전 방향·발 미끄러짐·루트 이동 수치를 만든다.
실행: python Tools/Cascadeur/check_cascadeur_animation.py --spec AnimationSources/Cascadeur/<이름>.motion.json [--weapon Tools/Cascadeur/weapons/SM_Sword.json]  (Cascadeur MCP 서버 켜짐 필요)  (PowerShell)
출력: Saved/Cascadeur/<이름>/check/(<이름>-skeleton.gif, -skeleton-sheet.png, check.json), 표준 출력 요약 JSON
상태: 현행
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools/Cascadeur"))
import build_motion_keys as motion  # noqa: E402
import cascadeur_bridge as bridge  # noqa: E402

BONES = [("pelvis", "spine_01"), ("spine_01", "spine_03"), ("spine_03", "spine_05"), ("spine_05", "neck_01"), ("neck_01", "head"),
         ("spine_05", "upperarm_l"), ("upperarm_l", "lowerarm_l"), ("lowerarm_l", "hand_l"), ("spine_05", "upperarm_r"), ("upperarm_r", "lowerarm_r"), ("lowerarm_r", "hand_r"),
         ("pelvis", "thigh_l"), ("thigh_l", "calf_l"), ("calf_l", "foot_l"), ("foot_l", "ball_l"), ("pelvis", "thigh_r"), ("thigh_r", "calf_r"), ("calf_r", "foot_r"), ("foot_r", "ball_r")]
PANEL = 360
SCALE = 1.25
GROUND_CONTACT_HEIGHT_CM = 3.0
SLIDE_TOLERANCE_CM_PER_FRAME = 2.0
BONE_STRETCH_TOLERANCE_CM = 1.0
UNDRIVEN_HELPER_PREFIXES = ("ik_", "interaction", "center_of_mass")


def read_joint_frames(frame_count):
    record = bridge.call("csc_inspect.read_pose", {"frames": list(range(frame_count)), "types": ["Joint"]}, 600)
    return bridge.require_success(record)["objects"]


def frame_arrays(objects, frame_count):
    positions = {name: np.array([objects[name]["frames"][str(frame)]["position"] for frame in range(frame_count)]) for name in objects}
    rotations = {name: [motion.quaternion_wxyz_to_matrix(objects[name]["frames"][str(frame)]["quaternion_wxyz"]) for frame in range(frame_count)] for name in ("hand_r", "spine_05")}
    return positions, rotations


def chest_azimuths(rotations, rest_objects, frame_count):
    rest_rotation = motion.quaternion_wxyz_to_matrix(rest_objects["spine_05"]["frames"]["0"]["quaternion_wxyz"])
    azimuths = []
    for frame in range(frame_count):
        forward = motion.to_character(rotations["spine_05"][frame] @ rest_rotation.T @ np.array((0.0, 0.0, 1.0)))
        azimuths.append(math.degrees(math.atan2(forward[0], forward[2])))
    return np.degrees(np.unwrap(np.radians(azimuths)))


def project(point, view, center):
    character = motion.to_character(point) - center
    if view == "top":
        return PANEL / 2 + character[0] * SCALE, PANEL * 0.62 - character[2] * SCALE
    return PANEL / 2 + character[0] * SCALE, PANEL * 0.92 - character[1] * SCALE


def draw_panel(view, frame, positions, blade, center, label):
    image = Image.new("RGB", (PANEL, PANEL), (36, 38, 42))
    draw = ImageDraw.Draw(image)
    for offset in range(-150, 151, 50):
        if view == "top":
            draw.line([project(motion.to_cascadeur(center + np.array((offset, 0, -150))), view, center), project(motion.to_cascadeur(center + np.array((offset, 0, 200))), view, center)], fill=(58, 60, 66))
            draw.line([project(motion.to_cascadeur(center + np.array((-150, 0, offset))), view, center), project(motion.to_cascadeur(center + np.array((150, 0, offset))), view, center)], fill=(58, 60, 66))
    if view == "front":
        draw.line([(0, PANEL * 0.92), (PANEL, PANEL * 0.92)], fill=(80, 80, 86), width=2)
    for first, second in BONES:
        color = (235, 120, 90) if second.endswith("_r") or first.endswith("_r") else ((110, 170, 240) if second.endswith("_l") or first.endswith("_l") else (220, 220, 220))
        draw.line([project(positions[first][frame], view, center), project(positions[second][frame], view, center)], fill=color, width=3)
    draw.line([project(blade["guard_center"][frame], view, center), project(blade["tip"][frame], view, center)], fill=(255, 230, 90), width=4)
    draw.line([project(blade["guard_left"][frame], view, center), project(blade["guard_right"][frame], view, center)], fill=(255, 230, 90), width=2)
    head = project(positions["head"][frame], view, center)
    draw.ellipse([head[0] - 7, head[1] - 7, head[0] + 7, head[1] + 7], outline=(220, 220, 220), width=2)
    draw.text((6, 6), label, fill=(255, 255, 255))
    return image


def blade_points(positions, rotations, grip, frame_count):
    points = {name: [] for name in grip.local}
    for frame in range(frame_count):
        for name, local in grip.local.items():
            points[name].append(positions["hand_r"][frame] + rotations["hand_r"][frame] @ local)
    return {name: np.array(values) for name, values in points.items()}


def bone_stretch(objects, rest_objects, positions, frame_count):
    worst = {"bone": None, "frame": None, "rest_cm": 0.0, "length_cm": 0.0, "change_cm": 0.0}
    for name, item in objects.items():
        parent = item["parent"]
        if parent not in positions or parent not in rest_objects or name not in rest_objects or parent == "root" or name.startswith(UNDRIVEN_HELPER_PREFIXES):
            continue
        if rest_objects[parent].get("type") != "Joint":
            continue
        rest_length = float(np.linalg.norm(np.array(rest_objects[name]["frames"]["0"]["position"]) - np.array(rest_objects[parent]["frames"]["0"]["position"])))
        lengths = np.linalg.norm(positions[name] - positions[parent], axis=1)
        frame = int(np.argmax(np.abs(lengths - rest_length)))
        change = float(abs(lengths[frame] - rest_length))
        if change > worst["change_cm"]:
            worst = {"bone": name, "frame": frame, "rest_cm": round(rest_length, 2), "length_cm": round(float(lengths[frame]), 2), "change_cm": round(change, 2)}
    worst["within_tolerance"] = worst["change_cm"] <= BONE_STRETCH_TOLERANCE_CM
    return worst


def contact_slides(positions, frame_count, joint):
    heights = positions[joint][:, 1]
    planted = [frame for frame in range(frame_count) if heights[frame] < GROUND_CONTACT_HEIGHT_CM]
    worst, worst_frame = 0.0, None
    for frame in planted:
        if frame - 1 in planted:
            slide = float(np.linalg.norm((positions[joint][frame] - positions[joint][frame - 1])[[0, 2]]))
            if slide > worst:
                worst, worst_frame = slide, frame
    return {"planted_frames": len(planted), "worst_slide_cm_per_frame": round(worst, 2), "frame": worst_frame, "within_tolerance": worst <= SLIDE_TOLERANCE_CM_PER_FRAME}


def analyze(spec, weapon_path, folder):
    from build_cascadeur_animation import DEFAULT_CHARACTER, character_paths

    frame_count = int(spec["frame_count"])
    rest = json.loads(character_paths({**DEFAULT_CHARACTER, **spec.get("character", {})})["rest"].read_text(encoding="utf-8-sig"))
    rest = rest.get("result", rest)
    grip = motion.TDWeaponGrip(motion.TDRestRig(rest), json.loads(weapon_path.read_text(encoding="utf-8")))
    objects = read_joint_frames(frame_count)
    positions, rotations = frame_arrays(objects, frame_count)
    blade = blade_points(positions, rotations, grip, frame_count)
    fps = float(spec.get("fps", 30))
    tip_character = np.array([motion.to_character(point) for point in blade["tip"]])
    hand_character = np.array([motion.to_character(point) for point in positions["hand_r"]])
    tip_speed = np.r_[0.0, np.linalg.norm(np.diff(tip_character, axis=0), axis=1) * fps / 100.0]
    azimuth = np.degrees(np.unwrap(np.arctan2(tip_character[:, 0] - hand_character[:, 0], tip_character[:, 2] - hand_character[:, 2])))
    first, last = spec["events"]["hit_window_frames"]
    window = slice(first, last + 1)
    root_character = np.array([motion.to_character(point) for point in positions["root"]])
    summary = {
        "frames": frame_count,
        "hit_window_frames": [first, last],
        "tip_speed_peak_m_per_s": round(float(tip_speed.max()), 1), "tip_speed_peak_frame": int(tip_speed.argmax()),
        "hit_window_tip_height_cm": [round(float(tip_character[window, 1].min()), 1), round(float(tip_character[window, 1].max()), 1)],
        "hit_window_blade_azimuth_degrees": [round(float(value), 1) for value in azimuth[window]],
        "hit_window_sweep_direction": "left_to_right" if azimuth[last] > azimuth[first] else "right_to_left",
        "front_crossing_frame": next((frame for frame in range(1, frame_count) if (azimuth[frame - 1] % 360 > 180) and (azimuth[frame] % 360 < 90)), None),
        "total_blade_turn_degrees": round(float(azimuth.max() - azimuth.min()), 1),
        "root_start_end_character_cm": [[round(value, 1) for value in root_character[0]], [round(value, 1) for value in root_character[-1]]],
        "foot_slide": {joint: contact_slides(positions, frame_count, joint) for joint in ("ball_l", "ball_r", "foot_l", "foot_r")},
        "lowest_joint_height_cm": round(float(min(positions[name][:, 1].min() for name in ("ball_l", "ball_r", "foot_l", "foot_r"))), 2),
    }
    summary["foot_slide_within_tolerance"] = all(item["within_tolerance"] for item in summary["foot_slide"].values())
    summary["slide_tolerance_cm_per_frame"] = SLIDE_TOLERANCE_CM_PER_FRAME
    summary["bone_stretch"] = bone_stretch(objects, rest["objects"], positions, frame_count)
    chest = chest_azimuths(rotations, rest["objects"], frame_count)
    relative = azimuth - chest
    summary["hit_window_chest_azimuth_degrees"] = [round(float(value), 1) for value in chest[window]]
    summary["hit_window_chest_turn_degrees"] = round(float(chest[last] - chest[first]), 1)
    summary["hit_window_blade_turn_degrees"] = round(float(azimuth[last] - azimuth[first]), 1)
    summary["hit_window_blade_relative_to_chest_range_degrees"] = round(float(relative[window].max() - relative[window].min()), 1)
    folder.mkdir(parents=True, exist_ok=True)
    gif_frames, sheet_frames = [], []
    authored = {int(key["frame"]) for key in spec["keys"]}
    for frame in range(frame_count):
        center = np.array((0.0, 0.0, root_character[frame][2]))
        label = f"f{frame}" + (" HIT" if first <= frame <= last else "")
        top = draw_panel("top", frame, positions, blade, center, label + " top")
        front = draw_panel("front", frame, positions, blade, center, label + " front")
        combined = Image.new("RGB", (PANEL * 2, PANEL))
        combined.paste(top, (0, 0))
        combined.paste(front, (PANEL, 0))
        gif_frames.append(combined)
        if frame in authored or first - 2 <= frame <= last + 2:
            sheet_frames.append((frame, top))
    gif = folder / f"{spec['name']}-skeleton.gif"
    gif_frames[0].save(gif, save_all=True, append_images=gif_frames[1:], duration=int(1000 / fps) * 2, loop=0)
    columns = 8
    rows = (len(sheet_frames) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * PANEL // 2, rows * PANEL // 2), (20, 20, 20))
    for index, (frame, image) in enumerate(sheet_frames):
        sheet.paste(image.resize((PANEL // 2, PANEL // 2)), ((index % columns) * PANEL // 2, (index // columns) * PANEL // 2))
    sheet_path = folder / f"{spec['name']}-skeleton-sheet.png"
    sheet.save(sheet_path)
    summary.update({"gif": str(gif), "sheet": str(sheet_path)})
    (folder / "check.json").write_text(json.dumps({"summary": summary, "tip_speed_m_per_s": [round(float(value), 2) for value in tip_speed],
                                                   "blade_azimuth_degrees": [round(float(value), 1) for value in azimuth],
                                                   "tip_character_cm": [[round(float(value), 1) for value in row] for row in tip_character]}, indent=1), encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", required=True)
    parser.add_argument("--weapon", default="Tools/Cascadeur/weapons/SM_Sword.json")
    arguments = parser.parse_args()
    spec_path = ROOT / arguments.spec if not Path(arguments.spec).is_absolute() else Path(arguments.spec)
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    bridge.require_success(bridge.call("csc_motion.open_scene_tab_for_bridge", {"scene_path": spec_path.with_suffix("").with_suffix(".casc").as_posix()}))
    summary = analyze(spec, ROOT / arguments.weapon, ROOT / "Saved/Cascadeur" / spec["name"] / "check")
    print(json.dumps(summary, indent=1, ensure_ascii=False))


if __name__ == "__main__":
    main()

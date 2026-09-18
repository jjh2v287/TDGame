"""시스템 Python 또는 Blender 안에서 실행: 애니메이션의 지정 발 접지 구간(또는 자동 감지 구간)에서 접지 오차(XY drift, Z 침투/부유, 순간 미끄러짐)를 수학적으로 정밀 계산하고 감사(audit)한다.
실행: python Tools/BlenderAnimation/validate_contacts.py [--source-json <경로>] [--blend <경로>] [--windows <bone:start-end,...>]
출력: Docs/Validation/BlenderAnimation/contact-audit-report.json 또는 stdout JSON
상태: 현행 (2026-09-19)
"""
import argparse
import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Any

ROOT = Path(__file__).resolve().parents[2]


def parse_window_argument(window_args: List[str]) -> Dict[str, List[Tuple[int, int]]]:
    result = {}
    if not window_args:
        return result
    for entry in window_args:
        if ':' not in entry:
            continue
        bone, spans = entry.split(':', 1)
        bone = bone.strip()
        result.setdefault(bone, [])
        for span in spans.split(','):
            span = span.strip()
            if not span:
                continue
            if '-' in span:
                start_s, end_s = span.split('-', 1)
                result[bone].append((int(start_s), int(end_s)))
            else:
                frame = int(span)
                result[bone].append((frame, frame))
    return result


def extract_trajectory_from_json(source_data: dict, bone_name: str) -> List[Tuple[float, float, float]]:
    records = source_data.get('records', [])
    trajectory = []

    for row in records:
        pos = None
        if 'feet' in row:
            side = 'l' if bone_name.endswith('_l') or bone_name.startswith('l_') else 'r'
            part = 'ball' if 'ball' in bone_name else ('ankle' if 'ankle' in bone_name or 'foot' in bone_name else 'toe')
            if side in row['feet'] and part in row['feet'][side]:
                pos = row['feet'][side][part]

        if pos is None and 'bones_world_cm' in row and bone_name in row['bones_world_cm']:
            pos = row['bones_world_cm'][bone_name]

        if pos is None:
            break

        trajectory.append((float(pos[0]), float(pos[1]), float(pos[2])))

    return trajectory


def detect_contact_windows(trajectory: List[Tuple[float, float, float]],
                           height_threshold_cm: float = 3.0,
                           speed_threshold_cm_per_frame: float = 1.0,
                           min_duration_frames: int = 4) -> List[Tuple[int, int]]:
    if len(trajectory) < 2:
        return []

    is_planted = []
    for i in range(len(trajectory)):
        z = trajectory[i][2]
        if i == 0:
            speed = math.dist(trajectory[0][:2], trajectory[1][:2])
        else:
            speed = math.dist(trajectory[i][:2], trajectory[i - 1][:2])
        is_planted.append(z <= height_threshold_cm and speed <= speed_threshold_cm_per_frame)

    windows = []
    in_window = False
    start_f = 0
    for frame, planted in enumerate(is_planted):
        if planted and not in_window:
            in_window = True
            start_f = frame
        elif not planted and in_window:
            in_window = False
            if (frame - start_f) >= min_duration_frames:
                windows.append((start_f, frame - 1))

    if in_window and (len(trajectory) - start_f) >= min_duration_frames:
        windows.append((start_f, len(trajectory) - 1))

    return windows


def audit_bone_contact(trajectory: List[Tuple[float, float, float]],
                       bone_name: str,
                       start_frame: int,
                       end_frame: int,
                       max_xy_threshold_cm: float = 0.5,
                       max_z_penetration_threshold_cm: float = 0.3,
                       max_z_floating_threshold_cm: float = 1.0) -> Dict[str, Any]:
    total_frames = len(trajectory)
    if start_frame < 0 or end_frame >= total_frames or start_frame > end_frame:
        return {
            'bone': bone_name,
            'window': [start_frame, end_frame],
            'status': 'ERROR',
            'reason': f'Window [{start_frame}, {end_frame}] out of range (total frames: {total_frames})'
        }

    anchor = trajectory[start_frame]
    ground_z = anchor[2]

    xy_drifts = []
    z_penetrations = []
    z_floatings = []
    slip_speeds = []

    for frame in range(start_frame, end_frame + 1):
        curr = trajectory[frame]
        xy_dist = math.dist(curr[:2], anchor[:2])
        xy_drifts.append((xy_dist, frame))

        z_diff = curr[2] - ground_z
        if z_diff < 0:
            z_penetrations.append((abs(z_diff), frame))
            z_floatings.append((0.0, frame))
        else:
            z_penetrations.append((0.0, frame))
            z_floatings.append((z_diff, frame))

        if frame > start_frame:
            prev = trajectory[frame - 1]
            slip_speeds.append((math.dist(curr[:2], prev[:2]), frame))

    worst_xy_drift, worst_xy_frame = max(xy_drifts, key=lambda item: item[0])
    worst_z_pen, worst_z_pen_frame = max(z_penetrations, key=lambda item: item[0])
    worst_z_flt, worst_z_flt_frame = max(z_floatings, key=lambda item: item[0])
    worst_slip_speed, worst_slip_frame = max(slip_speeds, key=lambda item: item[0]) if slip_speeds else (0.0, start_frame)
    mean_xy_drift = sum(item[0] for item in xy_drifts) / len(xy_drifts)

    passed = (
        worst_xy_drift <= max_xy_threshold_cm and
        worst_z_pen <= max_z_penetration_threshold_cm and
        worst_z_flt <= max_z_floating_threshold_cm
    )

    violations = []
    if worst_xy_drift > max_xy_threshold_cm:
        violations.append(f"XY drift {worst_xy_drift:.3f}cm exceeds {max_xy_threshold_cm:.2f}cm at frame {worst_xy_frame}")
    if worst_z_pen > max_z_penetration_threshold_cm:
        violations.append(f"Z penetration {worst_z_pen:.3f}cm exceeds {max_z_penetration_threshold_cm:.2f}cm at frame {worst_z_pen_frame}")
    if worst_z_flt > max_z_floating_threshold_cm:
        violations.append(f"Z floating {worst_z_flt:.3f}cm exceeds {max_z_floating_threshold_cm:.2f}cm at frame {worst_z_flt_frame}")

    guidelines = []
    if not passed:
        guidelines.append(
            f"Fix {bone_name} contact window [{start_frame}, {end_frame}]: "
            f"Lock horizontal translation to ({anchor[0]:.2f}, {anchor[1]:.2f})cm. "
            f"Clamp vertical height Z >= {ground_z:.2f}cm. "
            f"Preserve frames 0-{max(0, start_frame - 1)} and {min(total_frames - 1, end_frame + 1)}-{total_frames - 1}."
        )

    return {
        'bone': bone_name,
        'window': [start_frame, end_frame],
        'status': 'PASS' if passed else 'FAIL',
        'anchor_location_cm': [round(v, 3) for v in anchor],
        'max_xy_drift_cm': round(worst_xy_drift, 4),
        'worst_xy_drift_frame': worst_xy_frame,
        'mean_xy_drift_cm': round(mean_xy_drift, 4),
        'max_z_penetration_cm': round(worst_z_pen, 4),
        'worst_z_penetration_frame': worst_z_pen_frame,
        'max_z_floating_cm': round(worst_z_flt, 4),
        'worst_z_floating_frame': worst_z_flt_frame,
        'max_instant_slip_cm_per_frame': round(worst_slip_speed, 4),
        'worst_slip_frame': worst_slip_frame,
        'violations': violations,
        'correction_guidelines': guidelines
    }


def main():
    parser = argparse.ArgumentParser(description="수학적 발 접지 오차(Drift/Penetration) QA 감사 도구")
    parser.add_argument('--source-json', help="애니메이션 레코드 JSON 파일 경로")
    parser.add_argument('--armature', default='Armature', help="Blender Armature 오브젝트 이름")
    parser.add_argument('--windows', nargs='*', help="접지 윈도우 명시 (예: foot_l:14-39 ball_r:0-21,27-39)")
    parser.add_argument('--max-xy-drift', type=float, default=0.5, help="허용 최대 XY 드리프트 (cm)")
    parser.add_argument('--max-z-penetration', type=float, default=0.3, help="허용 최대 Z 지면 침투 (cm)")
    parser.add_argument('--max-z-floating', type=float, default=1.0, help="허용 최대 Z 부유 (cm)")
    parser.add_argument('--output', help="검증 결과 JSON 저장 경로")
    args = parser.parse_args()

    explicit_windows = parse_window_argument(args.windows) if args.windows else {}
    target_bones = list(explicit_windows.keys()) if explicit_windows else ['ball_l', 'ball_r', 'foot_l', 'foot_r']

    results = []
    source_name = ""

    if args.source_json:
        json_path = Path(args.source_json)
        if not json_path.is_absolute():
            json_path = ROOT / json_path
        if not json_path.exists():
            print(f"Error: JSON file not found at {json_path}", file=sys.stderr)
            sys.exit(2)

        data = json.loads(json_path.read_text(encoding='utf-8'))
        source_name = data.get('summary', {}).get('name', json_path.stem)

        for bone in target_bones:
            traj = extract_trajectory_from_json(data, bone)
            if not traj:
                continue

            windows = explicit_windows.get(bone)
            if not windows:
                windows = detect_contact_windows(traj)

            for start_f, end_f in windows:
                audit = audit_bone_contact(
                    traj, bone, start_f, end_f,
                    max_xy_threshold_cm=args.max_xy_drift,
                    max_z_penetration_threshold_cm=args.max_z_penetration,
                    max_z_floating_threshold_cm=args.max_z_floating
                )
                results.append(audit)

    else:
        try:
            import bpy
            armature_obj = bpy.data.objects.get(args.armature)
            if not armature_obj or armature_obj.type != 'ARMATURE':
                print(f"Error: Armature '{args.armature}' not found in current Blender context.", file=sys.stderr)
                sys.exit(2)

            scene = bpy.context.scene
            action = armature_obj.animation_data.action if armature_obj.animation_data else None
            source_name = action.name if action else "ActiveScene"
            frame_start = int(scene.frame_start if not action else action.frame_range[0])
            frame_end = int(scene.frame_end if not action else action.frame_range[1])

            for bone_name in target_bones:
                pose_bone = armature_obj.pose.bones.get(bone_name)
                if not pose_bone:
                    continue

                traj = []
                for f in range(frame_start, frame_end + 1):
                    scene.frame_set(f)
                    bpy.context.view_layer.update()
                    world_matrix = armature_obj.matrix_world @ pose_bone.matrix
                    pos_cm = world_matrix.to_translation() * 100.0
                    traj.append((pos_cm.x, pos_cm.y, pos_cm.z))

                windows = explicit_windows.get(bone_name)
                if not windows:
                    windows = detect_contact_windows(traj)

                for start_f, end_f in windows:
                    audit = audit_bone_contact(
                        traj, bone_name, start_f, end_f,
                        max_xy_threshold_cm=args.max_xy_drift,
                        max_z_penetration_threshold_cm=args.max_z_penetration,
                        max_z_floating_threshold_cm=args.max_z_floating
                    )
                    results.append(audit)

        except ImportError:
            print("Error: Specify --source-json when running in standard Python environment without bpy.", file=sys.stderr)
            sys.exit(2)

    has_failure = any(r['status'] == 'FAIL' for r in results)
    overall_status = 'FAIL' if has_failure else ('PASS' if results else 'NO_CONTACTS_AUDITED')

    report = {
        'source': source_name,
        'overall_status': overall_status,
        'thresholds': {
            'max_xy_drift_cm': args.max_xy_drift,
            'max_z_penetration_cm': args.max_z_penetration,
            'max_z_floating_cm': args.max_z_floating
        },
        'audits': results
    }

    out_text = json.dumps(report, indent=2)
    print(out_text)

    if args.output:
        out_path = Path(args.output)
        if not out_path.is_absolute():
            out_path = ROOT / out_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(out_text, encoding='utf-8')
    else:
        default_out = ROOT / 'Docs/Validation/BlenderAnimation/contact-audit-report.json'
        default_out.parent.mkdir(parents=True, exist_ok=True)
        default_out.write_text(out_text, encoding='utf-8')

    if has_failure:
        sys.exit(1)


if __name__ == '__main__':
    main()

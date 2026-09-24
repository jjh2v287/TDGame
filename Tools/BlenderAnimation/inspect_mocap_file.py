# File: Tools/BlenderAnimation/inspect_mocap_file.py
import argparse
import math
from pathlib import Path

def parse_bvh(file_path):
    path = Path(file_path)
    if not path.exists():
        raise FileNotFoundError(f"File not found: {path}")

    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    bones = []
    in_motion = False
    frame_count = 0
    frame_time = 0.033333
    motion_data = []

    for idx, line in enumerate(lines):
        striped = line.strip()
        if striped.startswith("ROOT") or striped.startswith("JOINT"):
            bones.append(striped.split()[1])
        elif striped.startswith("Frames:"):
            frame_count = int(striped.split()[1])
        elif striped.startswith("Frame Time:"):
            frame_time = float(striped.split(":")[1].strip())
            in_motion = True
        elif in_motion and striped:
            parts = [float(x) for x in striped.split()]
            if parts:
                motion_data.append(parts)

    duration = frame_count * frame_time
    hips_travel = 0.0
    if len(motion_data) > 1 and len(motion_data[0]) >= 3:
        start_pos = motion_data[0][:3]
        end_pos = motion_data[-1][:3]
        hips_travel = math.dist(start_pos, end_pos)

    info = {
        "file": path.name,
        "frames": frame_count,
        "fps": round(1.0 / frame_time, 2) if frame_time > 0 else 0,
        "duration_seconds": round(duration, 2),
        "bone_count": len(bones),
        "bones_sample": bones[:8],
        "estimated_hips_travel": round(hips_travel, 2)
    }
    return info

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file", help="Path to BVH mocap file")
    args = parser.parse_args()
    result = parse_bvh(args.file)
    for key, val in result.items():
        print(f"{key}: {val}")

if __name__ == "__main__":
    main()

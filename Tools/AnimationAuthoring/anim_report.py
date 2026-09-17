# -*- coding: utf-8 -*-
"""덤프한 애니메이션의 타이밍 프로파일을 출력하고 스틱 피겨를 그린다 (에디터 불필요).

    python Tools/AnimationAuthoring/dump_animation.py  (에디터 안에서, run_in_editor 경유)
    python Tools/AnimationAuthoring/anim_report.py --dump <json> --image <png>

에디터 뷰포트 캡처는 탭 전환이 신뢰되지 않아 엉뚱한 에셋을 찍는다. 에셋의 본 데이터를
직접 읽어 그리는 편이 확실하고, 참고 애니메이션과 나란히 비교할 수 있다.

타이밍 프로파일은 프레임별 손 속도다. 실제 공격 애니메이션은 감기 구간이 느리고 타격
2~3프레임에 속도가 몰린다. 등속으로 퍼져 있으면 로봇처럼 보인다.
"""
import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pose_kinematics import quaternion_multiply, quaternion_rotate

SPINE = ['pelvis', 'spine_01', 'spine_02', 'spine_03', 'spine_04', 'spine_05', 'neck_01', 'head']
ARM_R = ['spine_05', 'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r']
ARM_L = ['spine_05', 'clavicle_l', 'upperarm_l', 'lowerarm_l', 'hand_l']
LEG_R = ['pelvis', 'thigh_r', 'calf_r', 'foot_r']
LEG_L = ['pelvis', 'thigh_l', 'calf_l', 'foot_l']


def component_positions(entry, parents, order):
    """덤프한 로컬 트랜스폼을 컴포넌트 공간 위치로 합성한다."""
    out = {}
    for name in order:
        if name not in entry:
            continue
        lx, ly, lz, qx, qy, qz, qw = entry[name]
        parent = parents.get(name)
        if parent and parent in out:
            parent_location, parent_rotation = out[parent]
            rotated = quaternion_rotate(parent_rotation, (lx, ly, lz))
            location = tuple(parent_location[i] + rotated[i] for i in range(3))
            rotation = quaternion_multiply(parent_rotation, (qx, qy, qz, qw))
        else:
            location, rotation = (lx, ly, lz), (qx, qy, qz, qw)
        out[name] = (location, rotation)
    return out


def load_skeleton_order(mesh):
    """부모 관계와 본 순서를 스켈레톤에서 가져온다."""
    from smoke_test import TDMcpAnimationClient
    client = TDMcpAnimationClient()
    result = client.call('.TDAnimationAuthoringTools.InspectSkeleton', SkeletalMeshPath=mesh)
    return ({b['name']: b['parent'] for b in result['bones']}, [b['name'] for b in result['bones']])


def timing_profile(frames, parents, order, fps):
    """프레임별 손 속도와 루트 전진을 돌려준다."""
    keys = sorted(frames, key=int)
    rows, previous = [], None
    for key in keys:
        placed = component_positions(frames[key], parents, order)
        hand = placed.get('hand_r', ((0, 0, 0), None))[0]
        root = placed.get('root', ((0, 0, 0), None))[0]
        speed = 0.0
        if previous is not None:
            speed = math.dist(hand, previous) * fps / 100.0  # m/s
        rows.append({'frame': int(key), 'hand': hand, 'root': root, 'speed': speed})
        previous = hand
    return rows


def print_profile(name, rows, fps):
    peak = max(r['speed'] for r in rows) or 1.0
    print('\n== %s ==  %d프레임 @%dfps = %.2f초   손 최고속도 %.1f m/s' % (
        name, rows[-1]['frame'], fps, rows[-1]['frame'] / fps, peak))
    for row in rows:
        bar = '#' * int(round(row['speed'] / peak * 44))
        print('  f%-3d %5.1f m/s |%-44s| 루트앞%+6.1f' % (row['frame'], row['speed'], bar, row['root'][1]))


def draw(entries, parents, order, out_path, columns=10):
    from PIL import Image, ImageDraw
    width, height, scale = 250, 330, 1.25
    rows_of = {}
    for name, data in entries.items():
        keys = sorted(data['frames'], key=int)
        step = max(1, len(keys) // columns)
        rows_of[name] = [keys[i] for i in range(0, len(keys), step)][:columns]
    sheet = Image.new('RGB', (width * columns, height * len(entries)), (250, 250, 250))
    draw_handle = ImageDraw.Draw(sheet)
    for row_index, (name, data) in enumerate(entries.items()):
        for col, key in enumerate(rows_of[name]):
            placed = component_positions(data['frames'][key], parents, order)
            ox, oy = col * width + width // 2, row_index * height + height - 40
            def point(bone):
                location = placed[bone][0]
                return (ox + location[1] * scale, oy - location[2] * scale * 0.92)  # 측면: 가로=앞, 세로=높이
            ground = oy - 8.2 * scale * 0.92
            draw_handle.line([(col * width + 6, ground), (col * width + width - 6, ground)], fill=(150, 185, 150), width=2)
            for chain, colour, thickness in ((LEG_R, (150, 110, 110), 4), (LEG_L, (110, 110, 155), 4),
                                             (SPINE, (55, 55, 65), 5), (ARM_L, (125, 125, 135), 4),
                                             (ARM_R, (200, 40, 40), 6)):
                pts = [point(b) for b in chain if b in placed]
                if len(pts) > 1:
                    draw_handle.line(pts, fill=colour, width=thickness, joint='curve')
            if 'hand_r' in placed:
                x, y = point('hand_r')
                draw_handle.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(230, 60, 40))
            for foot, colour in (('foot_l', (60, 60, 200)), ('foot_r', (170, 70, 70))):
                if foot in placed:
                    x, y = point(foot)
                    draw_handle.ellipse([x - 4, y - 4, x + 4, y + 4], fill=colour)
            draw_handle.text((col * width + 8, row_index * height + 8), '%s f%s' % (name, key), fill=(20, 20, 20))
            draw_handle.line([(col * width, row_index * height), (col * width, (row_index + 1) * height)],
                             fill=(220, 220, 225), width=1)
    sheet.save(out_path)
    print('\n이미지:', out_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dump', required=True)
    parser.add_argument('--image')
    parser.add_argument('--mesh', default='/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    parser.add_argument('--columns', type=int, default=10)
    args = parser.parse_args()

    entries = json.loads(Path(args.dump).read_text(encoding='utf-8'))
    parents, order = load_skeleton_order(args.mesh)
    for name, data in entries.items():
        rows = timing_profile(data['frames'], parents, order, data.get('fps', 30))
        print_profile(name, rows, data.get('fps', 30))
    if args.image:
        draw(entries, parents, order, args.image, args.columns)
    return 0


if __name__ == '__main__':
    sys.exit(main())

import bpy
import math

bpy.ops.wm.read_factory_settings(use_empty=True)

input_fbx = r'C:\Project\TDGame\Saved\TempAnim\AS_Sword_Slash_01.fbx'
output_fbx = r'C:\Project\TDGame\Saved\TempAnim\AS_Sword_Slash_Blender.fbx'

# 1. Import FBX from Unreal
bpy.ops.import_scene.fbx(filepath=input_fbx, use_anim=True)

armature = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE'][0]
bpy.context.view_layer.objects.active = armature
bpy.ops.object.mode_set(mode='POSE')

# 2. Clear existing animation or create new action
action = bpy.data.actions.new(name='AS_Sword_Slash_Blender')
if not armature.animation_data:
    armature.animation_data_create()
armature.animation_data.action = action

pose_bones = armature.pose.bones
spine = pose_bones.get('spine_02') or pose_bones.get('spine_01')
clavicle = pose_bones.get('clavicle_r')
upperarm = pose_bones.get('upperarm_r')
lowerarm = pose_bones.get('lowerarm_r')
hand = pose_bones.get('hand_r')

total_frames = 30
bpy.context.scene.frame_start = 0
bpy.context.scene.frame_end = total_frames

# (frame, (spine_x, spine_y, spine_z), (arm_x, arm_y, arm_z), (elbow_x, elbow_y, elbow_z), (hand_x, hand_y, hand_z))
keyframes = [
    # Frame 0: Guard (준비 자세)
    (0,  (0, 0, 0),        (10, -15, 10),     (0, 45, 0),    (0, 0, 0)),
    # Frame 8: Wind-up (우측 상단으로 크게 당기는 와인드업)
    (8,  (-5, -5, 28),     (-20, -45, -35),   (0, 85, 0),    (10, 15, -20)),
    # Frame 14: Contact / Slash (우->좌 수평 가르기 최대 타격 순간)
    (14, (12, 5, -38),     (35, 25, 45),      (0, 15, 0),    (-15, -10, 25)),
    # Frame 20: Overshoot (타격 관통 후 좌측 어깨 앞까지의 탄성 오버슛)
    (20, (8, 3, -44),      (38, 20, 52),      (0, 25, 0),    (-10, -5, 20)),
    # Frame 30: Settle (준비 자세로 정착)
    (30, (0, 0, 0),        (10, -15, 10),     (0, 45, 0),    (0, 0, 0))
]

def deg_to_rad(euler_deg):
    return tuple(math.radians(v) for v in euler_deg)

for frame, s_rot, a_rot, e_rot, h_rot in keyframes:
    bpy.context.scene.frame_set(frame)
    if spine:
        spine.rotation_mode = 'XYZ'
        spine.rotation_euler = deg_to_rad(s_rot)
        spine.keyframe_insert(data_path='rotation_euler', frame=frame)
    if upperarm:
        upperarm.rotation_mode = 'XYZ'
        upperarm.rotation_euler = deg_to_rad(a_rot)
        upperarm.keyframe_insert(data_path='rotation_euler', frame=frame)
    if lowerarm:
        lowerarm.rotation_mode = 'XYZ'
        lowerarm.rotation_euler = deg_to_rad(e_rot)
        lowerarm.keyframe_insert(data_path='rotation_euler', frame=frame)
    if hand:
        hand.rotation_mode = 'XYZ'
        hand.rotation_euler = deg_to_rad(h_rot)
        hand.keyframe_insert(data_path='rotation_euler', frame=frame)

# 3. Export to FBX
bpy.ops.object.mode_set(mode='OBJECT')
bpy.ops.export_scene.fbx(
    filepath=output_fbx,
    use_selection=False,
    bake_anim=True,
    bake_anim_use_all_bones=True,
    bake_anim_use_nla_strips=False,
    bake_anim_use_all_actions=False,
    bake_anim_step=1.0,
    add_leaf_bones=False
)
print('Blender slash animation generated successfully ->', output_fbx)
import bpy

bpy.ops.import_anim.bvh(filepath="AnimationSources/Mocap/02_07.bvh")
bvh = bpy.data.objects["02_07"]
tgt = bpy.data.objects["root"]
scene = bpy.context.scene
scene.rsl_retargeting_armature_source = bvh
scene.rsl_retargeting_armature_target = tgt
bpy.ops.rsl.build_bone_list()
bpy.ops.rsl.retarget_animation()

print("RETARGETED ACTION:", tgt.animation_data.action.name)
for f in [1440, 1466, 1485]:
    bpy.context.scene.frame_set(f)
    p_loc = tgt.pose.bones["pelvis"].matrix.to_translation()
    h_loc = tgt.pose.bones["head"].matrix.to_translation()
    r_loc = tgt.pose.bones["hand_r"].matrix.to_translation()
    print(f"Frame {f}: Pelvis={p_loc}, Head={h_loc}, Hand_R={r_loc}")

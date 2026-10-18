"""에디터 안에서 실행: M3-27 전투 표현 자산(합성 효과음·번쩍임 오버레이·피격 비네트 머티리얼·구르기 입력·콤보·피격 애니 가져오기)을 만든다. 이미 있는 자산은 건너뛰고, 이 도구가 만든 자산에는 메타데이터 TDGeneratedBy를 단다.
실행: python Tools/run_in_editor.py Tools/Combat/editor_make_combat_assets.py (PowerShell, L-repo-01). 단계 선택: -c "TD_STEPS=['sounds','material','vignette','input','anims']; exec(open('C:/Project/TDGame/Tools/Combat/editor_make_combat_assets.py', encoding='utf-8').read())"
출력: /Game/Combat/Feedback/Audio/SW_TD_*, /Game/Combat/Feedback/{M_TD_HitFlashOverlay,M_TD_DamageVignette}, /Game/Combat/Input/{IA_TDRoll,IMC_TDCombat}, /Game/Characters/Mannequins/Anims/{Sword,Reaction}/AS_TD_*, /Game/MonsterAI/Animations/Hyena/AS_TD_Hyena_Hit, 로그의 [TDTool] 줄
상태: 현행 (2026-10-18, M3-27)
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
PROJECT_DIR = os.path.abspath(unreal.Paths.project_dir())
OWNER_TAG = "TDGeneratedBy"
OWNER_NAME = "Tools/Combat/editor_make_combat_assets.py"
STEPS = globals().get("TD_STEPS", ["sounds", "material", "vignette", "input", "anims"])

MANNY_SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
HYENA_SKELETON = "/Game/Fab/Hyena_A1/Hyenas_A1_AllMotion_Skeleton"
SOUND_FOLDER = "/Game/Combat/Feedback/Audio"
SOUND_NAMES = ["SW_TD_CritAccent", "SW_TD_KillThud", "SW_TD_BodyFall", "SW_TD_HeavyImpact"]
MATERIAL_FOLDER = "/Game/Combat/Feedback"
MATERIAL_NAME = "M_TD_HitFlashOverlay"
VIGNETTE_NAME = "M_TD_DamageVignette"
INPUT_FOLDER = "/Game/Combat/Input"
ANIMATIONS = [
    ("AnimationSources/Player/AS_TD_Player_SwordCombo01.fbx", "/Game/Characters/Mannequins/Anims/Sword", MANNY_SKELETON, True),
    ("AnimationSources/Player/AS_TD_Player_SwordCombo02.fbx", "/Game/Characters/Mannequins/Anims/Sword", MANNY_SKELETON, True),
    ("AnimationSources/Player/AS_TD_Player_SwordCombo03.fbx", "/Game/Characters/Mannequins/Anims/Sword", MANNY_SKELETON, True),
    ("AnimationSources/Player/AS_TD_Player_SwordCombo04.fbx", "/Game/Characters/Mannequins/Anims/Sword", MANNY_SKELETON, True),
    ("AnimationSources/Player/AS_TD_Player_HitReact_Light.fbx", "/Game/Characters/Mannequins/Anims/Reaction", MANNY_SKELETON, False),
    ("AnimationSources/Player/AS_TD_Player_HitReact_Heavy.fbx", "/Game/Characters/Mannequins/Anims/Reaction", MANNY_SKELETON, False),
    ("AnimationSources/Monster/AS_TD_Manny_HitReact_Unarmed.fbx", "/Game/Characters/Mannequins/Anims/Reaction", MANNY_SKELETON, False),
    ("AnimationSources/Monster/AS_TD_Hyena_Hit.fbx", "/Game/MonsterAI/Animations/Hyena", HYENA_SKELETON, False),
]
ANIMATION_SAMPLE_RATE = 60


def log(message):
    unreal.log(f"[TDTool] {message}")


def mark_owned(asset):
    EAL.set_metadata_tag(asset, OWNER_TAG, OWNER_NAME)


def ensure_folder(folder):
    if EAL.does_directory_exist(folder):
        return
    if not EAL.make_directory(folder):
        raise RuntimeError(f"make_directory failed: {folder}")


def make_sounds():
    ensure_folder(SOUND_FOLDER)
    for name in SOUND_NAMES:
        target = f"{SOUND_FOLDER}/{name}"
        if EAL.does_asset_exist(target):
            log(f"sound skip (exists) {target}")
            continue
        source = os.path.join(PROJECT_DIR, "AudioSources", "Combat", name + ".wav")
        if not os.path.isfile(source):
            log(f"sound skip (no wav) {source}")
            continue
        task = unreal.AssetImportTask()
        task.filename = source
        task.destination_path = SOUND_FOLDER
        task.destination_name = name
        task.replace_existing = False
        task.automated = True
        task.save = False
        ASSET_TOOLS.import_asset_tasks([task])
        sound = EAL.load_asset(target)
        if not isinstance(sound, unreal.SoundWave):
            raise RuntimeError(f"sound import failed: {target}")
        mark_owned(sound)
        EAL.save_loaded_asset(sound, only_if_is_dirty=False)
        log(f"sound {target} duration={sound.get_editor_property('duration'):.3f}")


def material_node(material, cls, x, y, **properties):
    expression = MEL.create_material_expression(material, cls, x, y)
    if expression is None:
        raise RuntimeError("create failed " + cls.__name__)
    for key, value in properties.items():
        expression.set_editor_property(key, value)
    return expression


def material_link(source, target, target_input="", source_output=""):
    if not MEL.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f"connect failed {source.get_name()} -> {target.get_name()}.{target_input}")


def make_material():
    target = f"{MATERIAL_FOLDER}/{MATERIAL_NAME}"
    if EAL.does_asset_exist(target):
        log(f"material skip (exists) {target}")
        return
    ensure_folder(MATERIAL_FOLDER)
    material = ASSET_TOOLS.create_asset(MATERIAL_NAME, MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_skeletal_mesh", True)
    color = material_node(material, unreal.MaterialExpressionVectorParameter, -900, -120,
                          parameter_name="FlashColor", default_value=unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    intensity = material_node(material, unreal.MaterialExpressionScalarParameter, -900, 120,
                              parameter_name="FlashIntensity", default_value=0.0)
    fresnel = material_node(material, unreal.MaterialExpressionFresnel, -900, 300, exponent=2.5, base_reflect_fraction=0.0)
    rim = material_node(material, unreal.MaterialExpressionLinearInterpolate, -640, 300, const_a=0.45, const_b=1.0)
    material_link(fresnel, rim, "Alpha")
    glow = material_node(material, unreal.MaterialExpressionMultiply, -420, 120)
    material_link(intensity, glow, "A")
    material_link(rim, glow, "B")
    emissive = material_node(material, unreal.MaterialExpressionMultiply, -220, -60)
    material_link(color, emissive, "A")
    material_link(glow, emissive, "B")
    emissive_boost = material_node(material, unreal.MaterialExpressionMultiply, -40, -60, const_b=4.0)
    material_link(emissive, emissive_boost, "A")
    opacity = material_node(material, unreal.MaterialExpressionClamp, -220, 200, min_default=0.0, max_default=0.85)
    material_link(glow, opacity, "")
    MEL.connect_material_property(emissive_boost, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(material)
    mark_owned(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    log(f"material {target}")


def make_vignette():
    target = f"{MATERIAL_FOLDER}/{VIGNETTE_NAME}"
    if EAL.does_asset_exist(target):
        log(f"material skip (exists) {target}")
        return
    ensure_folder(MATERIAL_FOLDER)
    material = ASSET_TOOLS.create_asset(VIGNETTE_NAME, MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    color = material_node(material, unreal.MaterialExpressionVectorParameter, -900, -200,
                          parameter_name="VignetteColor", default_value=unreal.LinearColor(0.75, 0.02, 0.02, 1.0))
    intensity = material_node(material, unreal.MaterialExpressionScalarParameter, -900, 260,
                              parameter_name="VignetteIntensity", default_value=0.0)
    uv = material_node(material, unreal.MaterialExpressionTextureCoordinate, -1300, 40)
    center = material_node(material, unreal.MaterialExpressionConstant2Vector, -1300, 160, r=0.5, g=0.5)
    distance = material_node(material, unreal.MaterialExpressionDistance, -1100, 80)
    material_link(uv, distance, "A")
    material_link(center, distance, "B")
    ring = material_node(material, unreal.MaterialExpressionSmoothStep, -900, 80, const_min=0.32, const_max=0.72)
    material_link(distance, ring, "Value")
    alpha = material_node(material, unreal.MaterialExpressionMultiply, -640, 160)
    material_link(ring, alpha, "A")
    material_link(intensity, alpha, "B")
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(material)
    mark_owned(material)
    EAL.save_loaded_asset(material, only_if_is_dirty=False)
    log(f"material {target}")


def make_input():
    ensure_folder(INPUT_FOLDER)
    action_path = f"{INPUT_FOLDER}/IA_TDRoll"
    context_path = f"{INPUT_FOLDER}/IMC_TDCombat"
    if EAL.does_asset_exist(action_path):
        action = EAL.load_asset(action_path)
        log(f"input skip (exists) {action_path}")
    else:
        action = ASSET_TOOLS.create_asset("IA_TDRoll", INPUT_FOLDER, unreal.InputAction, unreal.InputAction_Factory())
        action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
        mark_owned(action)
        EAL.save_loaded_asset(action, only_if_is_dirty=False)
        log(f"input {action_path}")
    if EAL.does_asset_exist(context_path):
        log(f"input skip (exists) {context_path}")
        return
    context = ASSET_TOOLS.create_asset("IMC_TDCombat", INPUT_FOLDER, unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    key = unreal.Key()
    key.set_editor_property("key_name", "SpaceBar")
    context.map_key(action, key)
    mark_owned(context)
    EAL.save_loaded_asset(context, only_if_is_dirty=False)
    log(f"input {context_path} mappings={len(context.get_editor_property('default_key_mappings').get_editor_property('mappings'))}")


def import_animation(relative_file, folder, skeleton_path, use_root_motion):
    source = os.path.join(PROJECT_DIR, relative_file)
    name = os.path.splitext(os.path.basename(relative_file))[0]
    target = f"{folder}/{name}"
    if EAL.does_asset_exist(target):
        log(f"anim skip (exists) {target}")
        return
    if not os.path.isfile(source):
        log(f"anim skip (no fbx) {source}")
        return
    ensure_folder(folder)
    skeleton = EAL.load_asset(skeleton_path)
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError(f"not a skeleton: {skeleton_path}")
    options = unreal.FbxImportUI()
    for key, value in {
        "automated_import_should_detect_type": False,
        "mesh_type_to_import": unreal.FBXImportType.FBXIT_ANIMATION,
        "original_import_type": unreal.FBXImportType.FBXIT_ANIMATION,
        "import_as_skeletal": True,
        "import_mesh": False,
        "import_animations": True,
        "import_materials": False,
        "import_textures": False,
        "create_physics_asset": False,
        "skeleton": skeleton,
        "override_animation_name": name,
    }.items():
        options.set_editor_property(key, value)
    animation_options = options.get_editor_property("anim_sequence_import_data")
    for key, value in {
        "animation_length": unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME,
        "use_default_sample_rate": False,
        "custom_sample_rate": ANIMATION_SAMPLE_RATE,
        "import_bone_tracks": True,
        "import_custom_attribute": False,
        "add_curve_metadata_to_skeleton": False,
        "preserve_local_transform": True,
        "convert_scene": True,
        "convert_scene_unit": True,
        "force_front_x_axis": False,
        "import_uniform_scale": 1.0,
    }.items():
        animation_options.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    task.filename = source
    task.destination_path = folder
    task.destination_name = name
    task.replace_existing = False
    task.replace_existing_settings = False
    task.automated = True
    task.save = False
    task.factory = unreal.FbxFactory()
    task.options = options
    ASSET_TOOLS.import_asset_tasks([task])
    animation = EAL.load_asset(target)
    if not isinstance(animation, unreal.AnimSequence):
        raise RuntimeError(f"animation import failed: {target} {list(task.get_editor_property('imported_object_paths'))}")
    if animation.get_editor_property("skeleton") != skeleton:
        raise RuntimeError(f"unexpected skeleton on {target}")
    animation.set_editor_property("enable_root_motion", use_root_motion)
    if use_root_motion:
        animation.set_editor_property("root_motion_root_lock", unreal.RootMotionRootLock.REF_POSE)
        animation.set_editor_property("force_root_lock", False)
    else:
        animation.set_editor_property("force_root_lock", True)
    mark_owned(animation)
    EAL.save_loaded_asset(animation, only_if_is_dirty=False)
    log(f"anim {target} length={unreal.AnimationLibrary.get_sequence_length(animation):.3f} frames={unreal.AnimationLibrary.get_num_frames(animation)} root_motion={use_root_motion}")


def make_animations():
    for relative_file, folder, skeleton_path, use_root_motion in ANIMATIONS:
        import_animation(relative_file, folder, skeleton_path, use_root_motion)


STEP_FUNCTIONS = {"sounds": make_sounds, "material": make_material, "vignette": make_vignette, "input": make_input, "anims": make_animations}
for step in STEPS:
    STEP_FUNCTIONS[step]()
log(f"done steps={STEPS}")

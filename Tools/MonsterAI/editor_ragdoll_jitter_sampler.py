"""실행 환경: 언리얼 에디터 내장 Python(PIE 중). 직접 실행하지 않고 Tools/MonsterAI/pie_measure_ragdoll_jitter.py가 원격 실행으로 sys.modules['td_ragdoll_sampler']에 올려 쓴다.
실행 명령: (바깥 도구가 호출) PIE 시작 전 close_asset_editors() → start_job(...) → status() 폴링(game_seconds로 게임 시간 진행 확인) → 끝나면 out_path JSON
출력: 대상 하나(measure_all이면 함께 처치한 몬스터 전부)의 매 프레임 표본(물리 바디 뼈마다 게임 스레드 뼈 위치(GetSocketLocation, 화면 스키닝 아님)·질량 중심·선속도·각속도, 질량 상위 몸통 바디 3개의 앞·위 축, 컴포넌트 단위 깨어 있음, 메시 뼈 개정 번호 rev(C++ GetMeshBoneRevisionForTest, 빌드 전이면 None), 컴포넌트 위치, 전역·액터 시간 늘이기, 가라앉기 여부, 시체 더미면 다른 몬스터마다 래그돌·깨어 있음·최대 바디 속도), 처치 순간 기하(캡슐 반높이·지면 높이·질량 중심·가해자→피해자 수평 방향 R), 매 틱 월드 실시간과 표본기 비용, 시간 지정 스크린샷(명령 시각, 실제로 그려진 다음 월드 프레임의 처치·래그돌 기준 게임 시각, 경로), 고정 시간 간격·카메라 적용 값과 복구 결과(fixed_step·camera·restored), --spread 배치(spread_placed: 처치 순서·계획 거리·실제 3D 거리·각), 정적 정보(물리 에셋·바디 질량·몸 구조·누운 높이 비율)
상태: 현행(M3-40). close_asset_editors는 닫기 확인 창이 없는 편집기(물리 에셋·스켈레톤·애니메이션·블루프린트, 로드된 에셋마다 close_all_editors_for_asset)만 닫는다: HighResShot은 그 프레임에 먼저 그려지는 실시간 뷰포트를 찍으므로 열린 편집기 뷰포트가 대신 찍히고 부하를 더한다. 메시·머티리얼·나이아가라 편집기는 닫을 때 적용·저장 확인 창이 원격 실행을 막을 수 있어 닫지 않고 left_open_prompting_classes로만 알린다. 닫아도 더티 상태는 메모리에 남고 저장하지 않는다. 처치는 TDCombatLibrary.TryApplyDamage(먼저 남은 체력을 kill_fraction까지 깎고 kill_delay 뒤 마무리 일격, kill_strength strong은 최대 체력 이상 한 방, critical은 그 일격 동안 가해자 치명 확률 1·피해는 치명 배율로 나눔). 몬스터 종 DA play_death_clip_before_ragdoll을 False로(force_ragdoll이면 ragdoll_after_death_clip도 True로) 메모리 강제하고 (종, 속성, 값)으로 되돌림, 속성이 없으면(C++ 빌드 전) 경고만. 래그돌 시작 뒤 shot_offsets 시각에 Shot, screenshots.clock(ragdoll 첫 시뮬 프레임·kill 처치 틱) 게임 시간 screenshots.times 시각에 HighResShot 1 filename=(앞 사진이 그려진 뒤에만 다음 명령). screenshots.fixed_fps면 작업 동안 t.OverrideFPS로 월드 프레임 간격을 1/fps로 고정해 촬영 끊김에도 게임 시간이 건너뛰지 않게 하고, camera_arm_cm이면 플레이어 SpringArm 팔 길이·지연 끔·target_offset으로 대상(처치 뒤는 처치 위치)을 화면 가운데에 두며, 둘 다 틱 등록 직전에 켜고(작업을 STATE에 올린 뒤의 준비 — 다른 몬스터 뼈·함께 측정 대상·빙결·켜기·틱 등록 — 가 실패하면 되돌리고 작업을 끝냄으로 표시해 남은 틱 핸들도 건너뛰게 한 뒤 예외를 다시 올림) _finish와 restore_fixed_step·restore_camera에서 항목마다 따로 원래 값으로 되돌림(설정이 성공해야 저장값을 비우므로 실패하면 다음 복구가 다시 시도, restore_fixed_step은 다시 읽은 값이 다르면 {read_back, expected, kept_for_retry} 반환). scenario: flight_hold·freeze·pile_spacing_scale·pile_gaps·spread_cm(처치 틱에 다른 몬스터들→대상의 처치 순서대로 가해자와의 수평 거리를 MIN→MAX로 늘려 대상 방향 중심 25° 간격 부채꼴로 옮기고 가해자를 바라보게 함, 물결 시차 측정용)·kill_strength·critical·measure_all(함께 처치한 몬스터도 전체 표본)·frame_time_only(바디 표본 생략)·screenshots·camera_arm_cm·emulate_launch_clamp·emulate_firm_damping(C++ 수정 전 메모리 실험용)
"""
import json
import math
import time

import unreal

STATE = {"handle": None, "job": None, "species_restore": [], "override_restore": [], "fixed_step_restore": None, "camera_restore": None}
GAME_MODE_CLASS_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode.BP_TDCombatGameMode_C"
ENUM_PROPERTIES = {"sleep_family": unreal.SleepFamily}
PILE_DROP_HEIGHT_CM = 40.0
GROUND_TRACE_CM = 300.0
DEFAULT_CAPSULE_HALF_HEIGHT_CM = 90.0
TORSO_BODY_COUNT = 3
HIGH_RES_SHOT_COMMAND = "HighResShot 1 filename={path}"
FIXED_STEP_VARIABLE = "t.OverrideFPS"
FIXED_STEP_RESTORE_TOLERANCE_FPS = 0.01
CAMERA_PROPERTIES = ("target_arm_length", "target_offset", "enable_camera_lag")
NO_NOTIFY = unreal.PropertyAccessChangeNotifyMode.NEVER
SHOT_TIME_TOLERANCE_SECONDS = 0.001
PROMPT_FREE_EDITOR_CLASS_NAMES = ("PhysicsAsset", "Skeleton", "AnimationAsset", "Blueprint")
PROMPTING_EDITOR_CLASS_NAMES = ("SkeletalMesh", "StaticMesh", "MaterialInterface", "NiagaraSystem")
SPREAD_STEP_DEGREES = 25.0


def close_asset_editors():
    editors = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem)
    closed = []
    failed = {}
    for class_name in PROMPT_FREE_EDITOR_CLASS_NAMES:
        asset_class = getattr(unreal, class_name, None)
        if asset_class is None:
            continue
        try:
            closed.extend(asset.get_path_name() for asset in unreal.ObjectIterator(asset_class) if editors.close_all_editors_for_asset(asset) > 0)
        except Exception as error:
            failed[class_name] = repr(error)
            unreal.log_warning(f"td_ragdoll_sampler: closing {class_name} editors failed {error!r}")
    return {"closed": closed, "failed": failed, "left_open_prompting_classes": list(PROMPTING_EDITOR_CLASS_NAMES)}


def _body_setups(physics_asset):
    return [setup for setup in unreal.ObjectIterator(unreal.BodySetup) if setup.get_outer() == physics_asset]


def resolve_physics_assets(mesh_paths, include_player, species_names=()):
    meshes = [unreal.load_asset(path) for path in mesh_paths]
    if include_player:
        game_mode_class = unreal.load_class(None, GAME_MODE_CLASS_PATH)
        pawn_class = unreal.get_default_object(game_mode_class).get_editor_property("default_pawn_class")
        meshes.append(unreal.get_default_object(pawn_class).get_editor_property("mesh").get_skeletal_mesh_asset())
    assets = []
    for mesh in meshes:
        physics_asset = mesh.get_editor_property("physics_asset") if mesh else None
        if physics_asset and physics_asset not in assets:
            assets.append(physics_asset)
    for name in species_names:
        species = unreal.load_asset(f"/Game/MonsterAI/Species/DA_TDMonster_{name}")
        physics_asset = species.get_editor_property("ragdoll_physics_asset") if species else None
        if physics_asset and physics_asset not in assets:
            assets.append(physics_asset)
    return assets


def apply_overrides(mesh_paths, include_player, body_values, self_collision_off, species_names=()):
    restore_overrides()
    changed = []
    for physics_asset in resolve_physics_assets(mesh_paths, include_player, species_names):
        for setup in _body_setups(physics_asset):
            instance = setup.get_editor_property("default_instance")
            originals = {name: instance.get_editor_property(name) for name in body_values}
            for name, value in body_values.items():
                instance.set_editor_property(name, getattr(ENUM_PROPERTIES[name], value) if name in ENUM_PROPERTIES else value)
            setup.set_editor_property("default_instance", instance, unreal.PropertyAccessChangeNotifyMode.NEVER)
            original_response = setup.get_editor_property("collision_reponse")
            if self_collision_off:
                setup.set_editor_property("collision_reponse", unreal.BodyCollisionResponse.BODY_COLLISION_DISABLED, unreal.PropertyAccessChangeNotifyMode.NEVER)
            STATE["override_restore"].append((setup, originals, original_response))
        changed.append(physics_asset.get_path_name())
    return {"overridden": changed}


def restore_overrides():
    for setup, originals, original_response in reversed(STATE["override_restore"]):
        instance = setup.get_editor_property("default_instance")
        for name, value in originals.items():
            instance.set_editor_property(name, value)
        setup.set_editor_property("default_instance", instance, unreal.PropertyAccessChangeNotifyMode.NEVER)
        setup.set_editor_property("collision_reponse", original_response, unreal.PropertyAccessChangeNotifyMode.NEVER)
    restored = len(STATE["override_restore"])
    STATE["override_restore"] = []
    return restored


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _round_vector(vector):
    return [round(vector.x, 3), round(vector.y, 3), round(vector.z, 3)]


def _console_world():
    return _world() or unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _set_console_float(name, value):
    unreal.SystemLibrary.execute_console_command(_console_world(), f"{name} {value:g}", None)
    return unreal.SystemLibrary.get_console_variable_float_value(name)


def _enable_fixed_step(fps):
    if STATE["fixed_step_restore"] is None:
        STATE["fixed_step_restore"] = unreal.SystemLibrary.get_console_variable_float_value(FIXED_STEP_VARIABLE)
    applied = _set_console_float(FIXED_STEP_VARIABLE, float(fps))
    if applied < 0.001:
        unreal.log_warning(f"td_ragdoll_sampler: {FIXED_STEP_VARIABLE} {fps} did not apply; screenshots will skip game time on hitches")
    return {"console_variable": FIXED_STEP_VARIABLE, "requested_fps": fps, "applied_fps": applied, "before": STATE["fixed_step_restore"]}


def restore_fixed_step():
    before = STATE["fixed_step_restore"]
    if before is None:
        return None
    applied = _set_console_float(FIXED_STEP_VARIABLE, before)
    if abs(applied - before) > FIXED_STEP_RESTORE_TOLERANCE_FPS:
        unreal.log_warning(f"td_ragdoll_sampler: {FIXED_STEP_VARIABLE} restore to {before:g} read back {applied:g}; kept for retry")
        return {"read_back": applied, "expected": before, "kept_for_retry": True}
    STATE["fixed_step_restore"] = None
    return applied


def _hold_camera_focus(job):
    boom = job.get("camera_boom")
    if boom is None or not unreal.SystemLibrary.is_valid(boom):
        return
    if not job["killed"] and unreal.SystemLibrary.is_valid(job["actor"]):
        job["camera_focus"] = job["actor"].get_actor_location()
    boom.set_editor_property("target_arm_length", job["camera_arm_cm"], NO_NOTIFY)
    boom.set_editor_property("target_offset", job["camera_focus"] - boom.get_world_location(), NO_NOTIFY)


def _apply_camera(job, world, arm_length_cm):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    boom = pawn.get_component_by_class(unreal.SpringArmComponent) if pawn else None
    if boom is None:
        unreal.log_warning("td_ragdoll_sampler: player pawn has no SpringArmComponent; camera left unchanged")
        return None
    restore_camera()
    originals = {name: boom.get_editor_property(name) for name in CAMERA_PROPERTIES}
    STATE["camera_restore"] = (boom, originals)
    boom.set_editor_property("enable_camera_lag", False, NO_NOTIFY)
    job.update({"camera_boom": boom, "camera_arm_cm": float(arm_length_cm), "camera_focus": job["actor"].get_actor_location()})
    _hold_camera_focus(job)
    rotation = boom.get_world_rotation()
    return {"arm_length_cm": float(arm_length_cm), "original_arm_length_cm": round(originals["target_arm_length"], 1),
            "original_target_offset": _round_vector(originals["target_offset"]), "original_camera_lag": bool(originals["enable_camera_lag"]),
            "pitch_deg": round(rotation.pitch, 1), "yaw_deg": round(rotation.yaw, 1), "focus": "target actor until kill, then kill location"}


def restore_camera():
    saved = STATE["camera_restore"]
    if saved is None:
        return None
    boom, originals = saved
    if not unreal.SystemLibrary.is_valid(boom):
        STATE["camera_restore"] = None
        return False
    for name, value in originals.items():
        boom.set_editor_property(name, value, NO_NOTIFY)
    STATE["camera_restore"] = None
    return True


def _enable_job_environment(job, world, screenshots, scenario):
    if screenshots.get("fixed_fps"):
        job["info"]["fixed_step"] = _enable_fixed_step(screenshots["fixed_fps"])
    if scenario.get("camera_arm_cm"):
        job["info"]["camera"] = _apply_camera(job, world, scenario["camera_arm_cm"])


def _restore_job_environment(job):
    restored = {}
    for name, restore in (("fixed_step_fps", restore_fixed_step), ("camera", restore_camera)):
        try:
            restored[name] = restore()
        except Exception as error:
            restored[name] = {"error": repr(error)}
            unreal.log_warning(f"td_ragdoll_sampler: {name} restore failed {error!r}")
    job["info"]["restored"] = restored


def _abandon_job(job, error):
    _restore_job_environment(job)
    job.update({"done": True, "result": "error: start failed " + repr(error), "camera_boom": None})


def _body_bones(mesh):
    physics_asset = mesh.get_editor_property("physics_asset_override")
    skeletal_mesh = mesh.get_skeletal_mesh_asset()
    if not physics_asset and skeletal_mesh:
        physics_asset = skeletal_mesh.get_editor_property("physics_asset")
    if not physics_asset:
        return None, []
    bones = [setup.get_editor_property("bone_name") for setup in _body_setups(physics_asset)]
    bones = sorted((bone for bone in bones if mesh.get_bone_index(bone) >= 0), key=mesh.get_bone_index)
    return physics_asset, bones


def _apply_damage(target, source, amount, can_crit=False):
    spec = unreal.TDDamageSpec()
    spec.set_editor_property("amount", amount)
    spec.set_editor_property("can_crit", can_crit)
    spec.set_editor_property("instigator", source)
    spec.set_editor_property("causer", source)
    return unreal.TDCombatLibrary.try_apply_damage(target, spec)


def _apply_critical_damage(target, source, raw_amount):
    source_combat = source.get_component_by_class(unreal.TDCombatComponent)
    original_stats = source_combat.get_stats()
    forced_stats = source_combat.get_stats()
    forced_stats.set_editor_property("critical_chance", 1.0)
    source_combat.set_stats(forced_stats, False)
    try:
        return _apply_damage(target, source, raw_amount / max(original_stats.get_editor_property("critical_multiplier"), 1.0), True)
    finally:
        source_combat.set_stats(original_stats, False)


def _raw_damage_for(target, applied_amount):
    combat = target.get_component_by_class(unreal.TDCombatComponent)
    armor = max(combat.get_stats().get_editor_property("armor"), 0.0)
    return applied_amount * (100.0 + armor) / 100.0


def _find_target(world, target_kind):
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    monsters = [actor for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)
                if actor.get_component_by_class(unreal.TDCombatComponent).is_alive()]
    if target_kind == "player":
        return pawn, (monsters[0] if monsters else pawn)
    return (monsters[0] if monsters else None), pawn


def _species_of(actor):
    return actor.get_species() if hasattr(actor, "get_species") else None


def _species_property(species, name):
    if not species:
        return None
    try:
        return species.get_editor_property(name)
    except Exception:
        return None


def _force_species_property(species, name, value):
    original = _species_property(species, name)
    if original is None:
        unreal.log_warning(f"td_ragdoll_sampler: {species.get_name()} has no {name} (build C++ and restart the editor first); left unchanged")
        return False
    STATE["species_restore"].append((species, name, original))
    species.set_editor_property(name, value)
    return True


def _force_species_ragdoll(actor):
    species = _species_of(actor)
    return _force_species_property(species, "ragdoll_after_death_clip", True) if species else False


def _force_immediate_ragdoll(actor):
    species = _species_of(actor)
    return _force_species_property(species, "play_death_clip_before_ragdoll", False) if species else None


def restore_species():
    for species, name, value in reversed(STATE["species_restore"]):
        species.set_editor_property(name, value)
    STATE["species_restore"] = []


def _apply_freeze(actor):
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    combat.apply_gameplay_effect_to_self(unreal.TDFreezeStatusEffect.static_class(), 1.0, combat.make_effect_context())
    return {"frozen": combat.is_frozen(), "dilation": actor.get_editor_property("custom_time_dilation")}


def _capsule_radius(actor):
    capsule = actor.get_component_by_class(unreal.CapsuleComponent)
    return capsule.get_scaled_capsule_radius() if capsule else 50.0


def _capsule_half_height(actor):
    capsule = actor.get_component_by_class(unreal.CapsuleComponent)
    return capsule.get_scaled_capsule_half_height() if capsule else DEFAULT_CAPSULE_HALF_HEIGHT_CM


def _away_axes(actor, source):
    away = actor.get_actor_location() - source.get_actor_location()
    away.z = 0.0
    away = away.normal() if away.length() > 1.0 else unreal.Vector(1.0, 0.0, 0.0)
    return away, unreal.Vector(-away.y, away.x, 0.0)


def _mass_center(mesh, bones, masses):
    total_mass = sum(masses)
    if not bones or total_mass <= 0.0:
        return mesh.get_world_location()
    center = unreal.Vector(0.0, 0.0, 0.0)
    for bone, mass in zip(bones, masses):
        center = center + mesh.get_center_of_mass(bone) * (mass / total_mass)
    return center


def _ground_height(world, actor, origin, ignored):
    fallback = actor.get_actor_location().z - _capsule_half_height(actor)
    try:
        object_types = [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1, unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2]
        hit = unreal.SystemLibrary.line_trace_single_for_objects(world, origin, origin - unreal.Vector(0.0, 0.0, GROUND_TRACE_CM), object_types, False, ignored,
                                                                 unreal.DrawDebugTrace.NONE, True)
        if hit is None:
            return fallback, "capsule"
        return hit.to_tuple()[5].z, "trace"
    except Exception as error:
        unreal.log_warning(f"td_ragdoll_sampler: ground trace failed {error!r}, using capsule bottom")
        return fallback, "capsule"


def _geometry(world, actor, source, mesh, bones, masses, ignored):
    center = _mass_center(mesh, bones, masses)
    ground_z, ground_source = _ground_height(world, actor, center, ignored)
    away, _ = _away_axes(actor, source)
    return {
        "capsule_half_height_cm": round(_capsule_half_height(actor), 2), "ground_z": round(ground_z, 2), "ground_source": ground_source,
        "kill_com": _round_vector(center), "attacker_location": _round_vector(source.get_actor_location()),
        "victim_location": _round_vector(actor.get_actor_location()), "attack_direction": _round_vector(away),
    }


def _torso_indices(masses):
    if not any(mass > 0.0 for mass in masses):
        return list(range(min(TORSO_BODY_COUNT, len(masses))))
    return sorted(sorted(range(len(masses)), key=lambda index: -masses[index])[:TORSO_BODY_COUNT])


def _subject_info(actor, mesh, physics_asset, bones):
    species = _species_of(actor)
    masses = [round(mesh.get_bone_mass(bone, True), 3) for bone in bones]
    torso = _torso_indices(masses)
    return {
        "actor": actor.get_name(), "species": species.get_name() if species else None,
        "physics_asset": physics_asset.get_path_name() if physics_asset else None, "bones": [str(bone) for bone in bones],
        "bone_mass_kg": masses, "torso_indices": torso, "torso_bones": [str(bones[index]) for index in torso],
        "horizontal_body_plan": _species_property(species, "has_horizontal_body_plan"),
        "corpse_rest_height_ratio": _species_property(species, "corpse_rest_height_ratio"),
    }


def _make_subject(actor):
    mesh = actor.get_editor_property("mesh")
    physics_asset, bones = _body_bones(mesh)
    info = _subject_info(actor, mesh, physics_asset, bones)
    return {"actor": actor, "mesh": mesh, "bones": bones, "torso_indices": info["torso_indices"], "ragdoll": False}, info


def _kill_style(job):
    return job["scenario"].get("kill_strength"), bool(job["scenario"].get("critical"))


def _kill_amount(combat, strength):
    if strength == "strong":
        return max(combat.get_max_health(), combat.get_current_health()) + 1.0
    return combat.get_current_health() + 1.0


def _kill(actor, source, strength=None, critical=False):
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    raw_amount = _raw_damage_for(actor, _kill_amount(combat, strength))
    if critical:
        _apply_critical_damage(actor, source, raw_amount)
    else:
        _apply_damage(actor, source, raw_amount)
    return combat.is_alive()


def _pile_simultaneous(job):
    actor = job["actor"]
    spacing = job["scenario"]["pile_spacing_scale"] * _capsule_radius(actor)
    away, side = _away_axes(actor, job["source"])
    origin = actor.get_actor_location()
    offsets = [away * spacing, away * (spacing * 0.5) + side * spacing, away * (spacing * 0.5) - side * spacing]
    placed = []
    for other, offset in zip(job["others"], offsets):
        location = origin + offset
        other.teleport_pawn(location, other.get_actor_rotation())
        placed.append([round(location.x, 1), round(location.y, 1), round(location.z, 1)])
    job["info"]["pile_spacing_cm"] = round(spacing, 1)
    job["info"]["pile_placed"] = placed


def _spread_by_distance(job):
    nearest_cm, farthest_cm = job["scenario"]["spread_cm"]
    actor, source = job["actor"], job["source"]
    away, side = _away_axes(actor, source)
    origin = source.get_actor_location()
    kill_order = list(job["others"]) + [actor]
    middle = (len(kill_order) - 1) * 0.5
    placed = []
    for index, monster in enumerate(kill_order):
        distance = nearest_cm + (farthest_cm - nearest_cm) * index / max(len(kill_order) - 1, 1)
        angle = math.radians(SPREAD_STEP_DEGREES * (index - middle))
        planar = away * (math.cos(angle) * distance) + side * (math.sin(angle) * distance)
        location = unreal.Vector(origin.x + planar.x, origin.y + planar.y, monster.get_actor_location().z)
        monster.teleport_pawn(location, unreal.MathLibrary.find_look_at_rotation(location, origin))
        placed.append({"actor": monster.get_name(), "kill_order": index, "planned_distance_cm": round(distance, 1),
                       "distance_cm": round(monster.get_distance_to(source), 1), "angle_deg": round(math.degrees(angle), 1)})
    job["camera_focus"] = actor.get_actor_location()
    job["info"]["spread_placed"] = placed


def _drop_next_onto_corpse(job, now):
    other = job["others"][len(job["pile_dropped"])]
    corpse = job["mesh"].get_center_of_mass(job["bones"][0]) if job["bones"] else job["actor"].get_actor_location()
    location = unreal.Vector(corpse.x, corpse.y, other.get_actor_location().z + PILE_DROP_HEIGHT_CM)
    other.teleport_pawn(location, other.get_actor_rotation())
    _kill(other, job["source"], *_kill_style(job))
    job["pile_dropped"].append(round(now - job["t_kill"], 4))


def _sample_others(job):
    rows = []
    for other, bones in zip(job["others"], job["other_bones"]):
        if not unreal.SystemLibrary.is_valid(other):
            rows.append([0, 0, 0.0, 1])
            continue
        mesh = other.get_editor_property("mesh")
        simulating = 1 if bones and mesh.is_simulating_physics(bones[0]) else 0
        speed = max((mesh.get_physics_linear_velocity(bone).length() for bone in bones), default=0.0) if simulating else 0.0
        sinking = mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_WORLD_STATIC) == unreal.CollisionResponseType.ECR_IGNORE
        rows.append([simulating, 1 if mesh.is_any_rigid_body_awake() else 0, round(speed, 1), 1 if sinking else 0])
    return rows


def start_job(label, target_kind, out_path, kill_fraction, kill_delay, observe_seconds, shot_offsets, force_ragdoll, assist=None, kill_others=False, scenario=None):
    world = _world()
    if world is None:
        return {"error": "no PIE world"}
    actor, source = _find_target(world, target_kind)
    if actor is None:
        return {"error": "no target"}
    mesh = actor.get_editor_property("mesh")
    physics_asset, bones = _body_bones(mesh)
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    max_health = combat.get_max_health()
    pre_amount = combat.get_current_health() - max_health * kill_fraction
    if pre_amount > 0.0:
        _apply_damage(actor, source, _raw_damage_for(actor, pre_amount))
    forced = _force_species_ragdoll(actor) if force_ragdoll else False
    immediate = _force_immediate_ragdoll(actor) if target_kind == "monster" else None
    scenario = dict(scenario or {})
    screenshots = scenario.get("screenshots") or {}
    others = [other for other in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)
              if other != actor and other.get_component_by_class(unreal.TDCombatComponent).is_alive()] if target_kind == "monster" else []
    tracked_others = others if (kill_others or scenario.get("pile_gaps")) else []
    now = unreal.GameplayStatics.get_time_seconds(world)
    info = _subject_info(actor, mesh, physics_asset, bones)
    info.update({"max_health": max_health, "kill_fraction": kill_fraction, "forced_ragdoll": forced, "forced_immediate_ragdoll": immediate,
                 "assist": assist, "scenario": scenario})
    job = {
        "label": label, "target_kind": target_kind, "out_path": out_path, "actor": actor, "source": source, "mesh": mesh,
        "bones": bones, "torso_indices": info["torso_indices"], "observe": observe_seconds, "kill_at": now + kill_delay, "killed": False,
        "t_kill": None, "rt_kill": None, "t_ragdoll": None, "shot_offsets": list(shot_offsets), "shots_fired": [], "frames": [], "done": False,
        "result": None, "assist": assist, "assist_low_since": None, "assist_slept": None, "kill_others": kill_others, "others_killed": 0,
        "scenario": scenario, "start_z": actor.get_actor_location().z, "t_start": now, "t_flying": None, "pile_dropped": [],
        "others": tracked_others, "other_bones": [], "subjects": [], "frame_clock": [], "last_tick_ms": 0.0,
        "screenshots": sorted(screenshots.get("times", [])), "screenshot_clock": screenshots.get("clock", "ragdoll"), "timed_shots": [], "info": info,
        "camera_boom": None, "camera_arm_cm": None, "camera_focus": None, "game_seconds": now,
    }
    try:
        STATE["job"] = job
        job["other_bones"] = [_body_bones(other.get_editor_property("mesh"))[1] for other in tracked_others]
        if scenario.get("measure_all"):
            made = [_make_subject(other) for other in tracked_others]
            job["subjects"] = [runtime for runtime, _ in made]
            info["subjects"] = [subject_info for _, subject_info in made]
        if scenario.get("freeze"):
            info["freeze_applied"] = _apply_freeze(actor)
        _enable_job_environment(job, world, screenshots, scenario)
        if STATE["handle"] is None:
            STATE["handle"] = unreal.register_slate_post_tick_callback(_tick)
    except Exception as error:
        _abandon_job(job, error)
        raise
    return {"started": label, "bones": len(bones), "physics_asset": info["physics_asset"], "subjects": len(job["subjects"]),
            "forced_immediate_ragdoll": immediate}


def _finish(job, reason):
    job["done"] = True
    job["result"] = reason
    job["info"]["pile_dropped_seconds"] = job["pile_dropped"]
    _restore_job_environment(job)
    job["camera_boom"] = None
    payload = {"label": job["label"], "target_kind": job["target_kind"], "result": reason, "t_kill": job["t_kill"], "rt_kill": job["rt_kill"],
               "t_ragdoll": job["t_ragdoll"], "t_assist_sleep": job["assist_slept"], "shots": job["shots_fired"], "timed_shots": job["timed_shots"],
               "frame_clock": job["frame_clock"], "info": job["info"], "frames": job["frames"]}
    with open(job["out_path"], "w", encoding="utf-8") as stream:
        json.dump(payload, stream)
    job["actor"] = job["mesh"] = job["source"] = None
    job["others"] = []
    job["subjects"] = []
    if STATE["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(STATE["handle"])
        STATE["handle"] = None


def _torso_axes(mesh, bone):
    rotation = mesh.get_socket_rotation(bone)
    return [_round_vector(rotation.get_forward_vector()), _round_vector(rotation.get_up_vector())]


def _bone_revision(actor):
    read_revision = getattr(actor, "get_mesh_bone_revision_for_test", None)
    return read_revision() if read_revision else None


def _sample_subject(actor, mesh, bones, torso_indices, is_light):
    sinking = mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_WORLD_STATIC) == unreal.CollisionResponseType.ECR_IGNORE
    subject = {
        "actor_dilation": round(actor.get_editor_property("custom_time_dilation"), 4),
        "sim": 1 if bones and mesh.is_simulating_physics(bones[0]) else 0, "sink": 1 if sinking else 0,
        "awake": 1 if mesh.is_any_rigid_body_awake() else 0, "rev": _bone_revision(actor),
    }
    if is_light:
        return subject
    subject["component"] = _round_vector(mesh.get_world_location())
    subject["actor"] = _round_vector(actor.get_actor_location())
    subject["bones"] = [[
        _round_vector(mesh.get_socket_location(bone)),
        _round_vector(mesh.get_center_of_mass(bone)),
        _round_vector(mesh.get_physics_linear_velocity(bone)),
        _round_vector(mesh.get_physics_angular_velocity_in_degrees(bone)),
    ] for bone in bones]
    subject["torso"] = [_torso_axes(mesh, bones[index]) for index in torso_indices]
    return subject


def _sample(job, world, now, real_now):
    frame = {"t": round(now, 5), "rt": round(real_now, 5), "dilation": round(unreal.GameplayStatics.get_global_time_dilation(world), 4)}
    frame.update(_sample_subject(job["actor"], job["mesh"], job["bones"], job["torso_indices"], bool(job["scenario"].get("frame_time_only"))))
    frame["others"] = _sample_others(job) if job["others"] else []
    frame["subjects"] = [_sample_subject(subject["actor"], subject["mesh"], subject["bones"], subject["torso_indices"], False)
                         if unreal.SystemLibrary.is_valid(subject["actor"]) else None for subject in job["subjects"]]
    return frame


def _refresh_subject_masses(job, frame):
    for index, (subject, row) in enumerate(zip(job["subjects"], frame["subjects"])):
        if subject["ragdoll"] or not row or not row["sim"]:
            continue
        subject["ragdoll"] = True
        job["info"]["subjects"][index]["bone_mass_kg"] = [round(subject["mesh"].get_bone_mass(bone, True), 3) for bone in subject["bones"]]


def _screenshot_origin(job):
    return job["t_kill"] if job["screenshot_clock"] == "kill" else job["t_ragdoll"]


def _note_captured_shots(job, now, real_now):
    for shot in job["timed_shots"]:
        if shot["captured_game_seconds_after_kill"] is not None or real_now <= shot["issued_world_real_seconds"]:
            continue
        shot["captured_game_seconds_after_kill"] = round(now - job["t_kill"], 4)
        shot["captured_game_seconds_after_ragdoll"] = round(now - job["t_ragdoll"], 4) if job["t_ragdoll"] is not None else None


def _is_previous_shot_pending(job):
    return bool(job["timed_shots"]) and job["timed_shots"][-1]["captured_game_seconds_after_kill"] is None


def _take_timed_screenshot(job, world, now, real_now):
    times = job["screenshots"]
    origin = _screenshot_origin(job)
    if not times or origin is None or now + SHOT_TIME_TOLERANCE_SECONDS < origin + times[0] or _is_previous_shot_pending(job):
        return
    offset = times.pop(0)
    settings = job["scenario"]["screenshots"]
    path = f"{settings['directory']}/{settings['stem']}_{job['screenshot_clock']}{round(offset * 1000):04d}ms.png"
    unreal.SystemLibrary.execute_console_command(world, HIGH_RES_SHOT_COMMAND.format(path=path), unreal.GameplayStatics.get_player_controller(world, 0))
    job["timed_shots"].append({"offset": offset, "clock": job["screenshot_clock"], "issued_game_seconds": round(now - origin, 4),
                               "issued_world_real_seconds": real_now, "real_seconds_after_kill": round(real_now - job["rt_kill"], 4),
                               "captured_game_seconds_after_kill": None, "captured_game_seconds_after_ragdoll": None, "path": path})


def _advance_assist(job, frame, now):
    masses = job["info"]["bone_mass_kg"]
    total_mass = max(sum(masses), 1e-6)
    momentum = [sum(mass * row[2][axis] for mass, row in zip(masses, frame["bones"])) for axis in range(3)]
    mean_speed = sum(value * value for value in momentum) ** 0.5 / total_mass
    if mean_speed > job["assist"]["speed_cm_s"]:
        job["assist_low_since"] = None
        return
    if job["assist_low_since"] is None:
        job["assist_low_since"] = now
    if now - job["t_ragdoll"] < job["assist"]["min_seconds"] or now - job["assist_low_since"] < job["assist"]["hold_seconds"]:
        return
    mesh = job["mesh"]
    if hasattr(mesh, "put_all_rigid_bodies_to_sleep"):
        mesh.put_all_rigid_bodies_to_sleep()
    else:
        for bone in job["bones"]:
            mesh.put_rigid_body_to_sleep(bone)
    job["assist_slept"] = round(now, 5)


def _is_kill_ready(job, now):
    hold = job["scenario"].get("flight_hold")
    if hold is None:
        return True
    if not job["actor"].is_monster_flying():
        job["t_flying"] = None
        return False
    if job["t_flying"] is None:
        job["t_flying"] = now
    return now - job["t_flying"] >= hold


def _kill_target(job, world, now, real_now):
    actor = job["actor"]
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    strength, critical = _kill_style(job)
    if job["scenario"].get("pile_spacing_scale"):
        _pile_simultaneous(job)
    if job["scenario"].get("spread_cm"):
        _spread_by_distance(job)
    ignored = [actor] + list(job["others"])
    job["info"]["geometry"] = _geometry(world, actor, job["source"], job["mesh"], job["bones"], job["info"]["bone_mass_kg"], ignored)
    for subject, subject_info in zip(job["subjects"], job["info"].get("subjects", [])):
        subject_info["geometry"] = _geometry(world, subject["actor"], job["source"], subject["mesh"], subject["bones"], subject_info["bone_mass_kg"], ignored)
    if job["kill_others"]:
        for other in job["others"]:
            if other.get_component_by_class(unreal.TDCombatComponent).is_alive():
                _kill(other, job["source"], strength, critical)
                job["others_killed"] += 1
        job["info"]["others_killed"] = job["others_killed"]
    job["info"]["at_kill"] = {
        "frozen": combat.is_frozen(), "actor_dilation": actor.get_editor_property("custom_time_dilation"),
        "flying": hasattr(actor, "is_monster_flying") and actor.is_monster_flying(), "height_cm": round(actor.get_actor_location().z - job["start_z"], 1),
        "wait_seconds": round(now - job["t_start"], 3), "flying_seconds": round(now - job["t_flying"], 3) if job["t_flying"] is not None else None,
        "kill_strength": strength, "critical": critical, "health_before_kill": round(combat.get_current_health(), 2),
        "attacker_location": job["info"]["geometry"]["attacker_location"], "attack_direction": job["info"]["geometry"]["attack_direction"],
    }
    job["info"]["alive_after_kill"] = _kill(actor, job["source"], strength, critical)
    job["killed"] = True
    job["t_kill"] = now
    job["rt_kill"] = real_now


def _mean_velocity(job):
    masses = job["info"]["bone_mass_kg"]
    total_mass = max(sum(masses), 1e-6)
    velocities = [job["mesh"].get_physics_linear_velocity(bone) for bone in job["bones"]]
    mean = unreal.Vector(0.0, 0.0, 0.0)
    for mass, velocity in zip(masses, velocities):
        mean = mean + velocity * (mass / total_mass)
    return mean, velocities


def _emulate_launch_clamp(job, now):
    clamp = job["scenario"].get("emulate_launch_clamp")
    if not clamp or now - job["t_ragdoll"] > clamp["seconds"]:
        return
    mesh = job["mesh"]
    mean, velocities = _mean_velocity(job)
    clamped = 0
    for bone, velocity in zip(job["bones"], velocities):
        relative = velocity - mean
        if relative.length() > clamp["relative_cm_s"]:
            mesh.set_physics_linear_velocity(mean + relative * (clamp["relative_cm_s"] / relative.length()), False, bone)
            clamped += 1
        angular = mesh.get_physics_angular_velocity_in_degrees(bone)
        if angular.length() > clamp["angular_deg_s"]:
            mesh.set_physics_angular_velocity_in_degrees(angular * (clamp["angular_deg_s"] / angular.length()), False, bone)
            clamped += 1
    job["info"]["emulated_clamps"] = job["info"].get("emulated_clamps", 0) + clamped


def _emulate_firm_damping(job, now, delta_seconds):
    firm = job["scenario"].get("emulate_firm_damping")
    mesh = job["mesh"]
    if not firm or now - job["t_ragdoll"] < firm["start"] or not mesh.is_any_rigid_body_awake():
        return
    scale = 1.0 / (1.0 + max(delta_seconds, 0.0) * firm["extra_damping"])
    for bone in job["bones"]:
        mesh.set_physics_linear_velocity(mesh.get_physics_linear_velocity(bone) * scale, False, bone)
        mesh.set_physics_angular_velocity_in_degrees(mesh.get_physics_angular_velocity_in_degrees(bone) * scale, False, bone)


def _tick(delta_seconds):
    job = STATE["job"]
    if not job or job["done"]:
        return
    started = time.perf_counter()
    try:
        world = _world()
        if world is None:
            _finish(job, "pie ended")
            return
        if not unreal.SystemLibrary.is_valid(job["actor"]):
            _finish(job, "actor destroyed")
            return
        now = unreal.GameplayStatics.get_time_seconds(world)
        real_now = unreal.GameplayStatics.get_real_time_seconds(world)
        job["game_seconds"] = now
        job["frame_clock"].append([round(real_now, 6), round(job["last_tick_ms"], 3)])
        _hold_camera_focus(job)
        if not job["killed"] and now >= job["kill_at"] and _is_kill_ready(job, now):
            _kill_target(job, world, now, real_now)
        if not job["killed"]:
            if now - job["t_start"] > job["scenario"].get("kill_timeout", 1e9):
                _finish(job, "kill condition not met")
            return
        gaps = job["scenario"].get("pile_gaps") or []
        if len(job["pile_dropped"]) < min(len(gaps), len(job["others"])) and now - job["t_kill"] >= gaps[len(job["pile_dropped"])]:
            _drop_next_onto_corpse(job, now)
        frame = _sample(job, world, now, real_now)
        job["frames"].append(frame)
        if job["subjects"]:
            _refresh_subject_masses(job, frame)
        if job["t_ragdoll"] is None and frame["sim"]:
            job["t_ragdoll"] = now
            job["info"]["bone_mass_kg"] = [round(job["mesh"].get_bone_mass(bone, True), 3) for bone in job["bones"]]
        _note_captured_shots(job, now, real_now)
        _take_timed_screenshot(job, world, now, real_now)
        if job["t_ragdoll"] is not None and job["shot_offsets"] and now >= job["t_ragdoll"] + job["shot_offsets"][0]:
            job["shot_offsets"].pop(0)
            unreal.SystemLibrary.execute_console_command(world, "Shot", unreal.GameplayStatics.get_player_controller(world, 0))
            job["shots_fired"].append(round(now, 4))
        if job["assist"] and job["t_ragdoll"] is not None and job["assist_slept"] is None and "bones" in frame:
            _advance_assist(job, frame, now)
        if job["t_ragdoll"] is not None and len(job["frames"]) > 1:
            game_delta = now - job["frames"][-2]["t"]
            _emulate_launch_clamp(job, now)
            _emulate_firm_damping(job, now, game_delta)
        if now - job["t_kill"] >= job["observe"]:
            _finish(job, "ok")
    except Exception as error:
        _finish(job, "error: " + repr(error))
    finally:
        job["last_tick_ms"] = (time.perf_counter() - started) * 1000.0


def status():
    job = STATE["job"]
    if not job:
        return {"job": None}
    return {"label": job["label"], "done": job["done"], "result": job["result"], "frames": len(job["frames"]),
            "t_kill": job["t_kill"], "t_ragdoll": job["t_ragdoll"], "shots": len(job["shots_fired"]), "timed_shots": len(job["timed_shots"]),
            "subjects": len(job["subjects"]), "game_seconds": job["game_seconds"]}

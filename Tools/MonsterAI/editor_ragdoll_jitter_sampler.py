"""실행 환경: 언리얼 에디터 내장 Python(PIE 중). 직접 실행하지 않고 Tools/MonsterAI/pie_measure_ragdoll_jitter.py가 원격 실행으로 sys.modules['td_ragdoll_sampler']에 올려 쓴다.
실행 명령: (바깥 도구가 호출) start_job(...) → status() 폴링 → 끝나면 out_path JSON
출력: 대상 하나의 매 프레임 표본(물리 바디 뼈마다 렌더 위치·질량 중심·선속도·각속도, 컴포넌트 단위 깨어 있음, 컴포넌트 위치, 전역·액터 시간 늘이기, 가라앉기 여부, 시체 더미면 다른 몬스터마다 래그돌·깨어 있음·최대 바디 속도)과 정적 정보(물리 에셋·바디 질량)
상태: 현행(M3-39). 처치는 TDCombatLibrary.TryApplyDamage(먼저 남은 체력을 kill_fraction까지 깎고 kill_delay 뒤 마무리 일격), 래그돌 시작 뒤 shot_offsets 시각에 Shot 연사. scenario: flight_hold(비행 확인 뒤 그 초만큼 날면 처치)·freeze(처치 전 빙결 GE)·pile_spacing_scale(처치 순간 다른 몬스터를 캡슐 반지름 배수 간격으로 대상 뒤·옆에 붙임)·pile_gaps(대상 처치 뒤 그 초마다 다른 몬스터를 대상 시체 위로 옮겨 처치)·emulate_launch_clamp(래그돌 첫 seconds 동안 매 프레임 질량 중심 대비 바디 선속도·각속도 상한)·emulate_firm_damping(start초부터 잠들 때까지 매 프레임 추가 감쇠 흉내) — C++ 수정 전 메모리 실험용
"""
import json

import unreal

STATE = {"handle": None, "job": None, "species_restore": [], "override_restore": []}
GAME_MODE_CLASS_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode.BP_TDCombatGameMode_C"
ENUM_PROPERTIES = {"sleep_family": unreal.SleepFamily}
PILE_DROP_HEIGHT_CM = 40.0


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


def _apply_damage(target, source, amount):
    spec = unreal.TDDamageSpec()
    spec.set_editor_property("amount", amount)
    spec.set_editor_property("instigator", source)
    spec.set_editor_property("causer", source)
    return unreal.TDCombatLibrary.try_apply_damage(target, spec)


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


def _force_species_ragdoll(actor):
    species = actor.get_species() if hasattr(actor, "get_species") else None
    if not species:
        return False
    STATE["species_restore"].append((species, species.get_editor_property("ragdoll_after_death_clip")))
    species.set_editor_property("ragdoll_after_death_clip", True)
    return True


def restore_species():
    for species, value in reversed(STATE["species_restore"]):
        species.set_editor_property("ragdoll_after_death_clip", value)
    STATE["species_restore"] = []


def _apply_freeze(actor):
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    combat.apply_gameplay_effect_to_self(unreal.TDFreezeStatusEffect.static_class(), 1.0, combat.make_effect_context())
    return {"frozen": combat.is_frozen(), "dilation": actor.get_editor_property("custom_time_dilation")}


def _capsule_radius(actor):
    capsule = actor.get_component_by_class(unreal.CapsuleComponent)
    return capsule.get_scaled_capsule_radius() if capsule else 50.0


def _away_axes(actor, source):
    away = actor.get_actor_location() - source.get_actor_location()
    away.z = 0.0
    away = away.normal() if away.length() > 1.0 else unreal.Vector(1.0, 0.0, 0.0)
    return away, unreal.Vector(-away.y, away.x, 0.0)


def _kill(actor, source):
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    _apply_damage(actor, source, _raw_damage_for(actor, combat.get_current_health() + 1.0))
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


def _drop_next_onto_corpse(job, now):
    other = job["others"][len(job["pile_dropped"])]
    corpse = job["mesh"].get_center_of_mass(job["bones"][0]) if job["bones"] else job["actor"].get_actor_location()
    location = unreal.Vector(corpse.x, corpse.y, other.get_actor_location().z + PILE_DROP_HEIGHT_CM)
    other.teleport_pawn(location, other.get_actor_rotation())
    _kill(other, job["source"])
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
    scenario = dict(scenario or {})
    others = [other for other in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter)
              if other != actor and other.get_component_by_class(unreal.TDCombatComponent).is_alive()] if target_kind == "monster" else []
    now = unreal.GameplayStatics.get_time_seconds(world)
    STATE["job"] = {
        "label": label, "target_kind": target_kind, "out_path": out_path, "actor": actor, "source": source, "mesh": mesh,
        "bones": bones, "observe": observe_seconds, "kill_at": now + kill_delay, "killed": False, "t_kill": None,
        "t_ragdoll": None, "shot_offsets": list(shot_offsets), "shots_fired": [], "frames": [], "done": False, "result": None,
        "assist": assist, "assist_low_since": None, "assist_slept": None, "kill_others": kill_others, "others_killed": 0,
        "scenario": scenario, "start_z": actor.get_actor_location().z, "t_start": now, "t_flying": None, "pile_dropped": [],
        "others": others if (kill_others or scenario.get("pile_gaps")) else [], "other_bones": [],
        "info": {
            "actor": actor.get_name(), "species": actor.get_species().get_name() if hasattr(actor, "get_species") and actor.get_species() else None,
            "physics_asset": physics_asset.get_path_name() if physics_asset else None, "bones": [str(bone) for bone in bones],
            "bone_mass_kg": [round(mesh.get_bone_mass(bone, True), 3) for bone in bones], "max_health": max_health,
            "kill_fraction": kill_fraction, "forced_ragdoll": forced, "assist": assist, "scenario": scenario,
        },
    }
    STATE["job"]["other_bones"] = [_body_bones(other.get_editor_property("mesh"))[1] for other in STATE["job"]["others"]]
    if scenario.get("freeze"):
        STATE["job"]["info"]["freeze_applied"] = _apply_freeze(actor)
    if STATE["handle"] is None:
        STATE["handle"] = unreal.register_slate_post_tick_callback(_tick)
    return {"started": label, "bones": len(bones), "physics_asset": STATE["job"]["info"]["physics_asset"]}


def _finish(job, reason):
    job["done"] = True
    job["result"] = reason
    job["info"]["pile_dropped_seconds"] = job["pile_dropped"]
    payload = {"label": job["label"], "target_kind": job["target_kind"], "result": reason, "t_kill": job["t_kill"],
               "t_ragdoll": job["t_ragdoll"], "t_assist_sleep": job["assist_slept"], "shots": job["shots_fired"], "info": job["info"], "frames": job["frames"]}
    with open(job["out_path"], "w", encoding="utf-8") as stream:
        json.dump(payload, stream)
    job["actor"] = job["mesh"] = job["source"] = None
    job["others"] = []
    if STATE["handle"] is not None:
        unreal.unregister_slate_post_tick_callback(STATE["handle"])
        STATE["handle"] = None


def _sample(job, world, now):
    mesh = job["mesh"]
    bones = job["bones"]
    root_bone = bones[0] if bones else unreal.Name("None")
    sinking = mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_WORLD_STATIC) == unreal.CollisionResponseType.ECR_IGNORE
    rows = []
    for bone in bones:
        rows.append([
            _round_vector(mesh.get_socket_location(bone)),
            _round_vector(mesh.get_center_of_mass(bone)),
            _round_vector(mesh.get_physics_linear_velocity(bone)),
            _round_vector(mesh.get_physics_angular_velocity_in_degrees(bone)),
        ])
    return {
        "t": round(now, 5), "rt": round(unreal.GameplayStatics.get_real_time_seconds(world), 5),
        "dilation": round(unreal.GameplayStatics.get_global_time_dilation(world), 4),
        "actor_dilation": round(job["actor"].get_editor_property("custom_time_dilation"), 4),
        "sim": 1 if bones and mesh.is_simulating_physics(root_bone) else 0, "sink": 1 if sinking else 0,
        "awake": 1 if mesh.is_any_rigid_body_awake() else 0,
        "component": _round_vector(mesh.get_world_location()), "actor": _round_vector(job["actor"].get_actor_location()),
        "bones": rows,
        "others": _sample_others(job) if job["others"] else [],
    }


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


def _kill_target(job, now):
    actor = job["actor"]
    combat = actor.get_component_by_class(unreal.TDCombatComponent)
    if job["scenario"].get("pile_spacing_scale"):
        _pile_simultaneous(job)
    if job["kill_others"]:
        for other in job["others"]:
            if other.get_component_by_class(unreal.TDCombatComponent).is_alive():
                _kill(other, job["source"])
                job["others_killed"] += 1
        job["info"]["others_killed"] = job["others_killed"]
    job["info"]["at_kill"] = {
        "frozen": combat.is_frozen(), "actor_dilation": actor.get_editor_property("custom_time_dilation"),
        "flying": hasattr(actor, "is_monster_flying") and actor.is_monster_flying(), "height_cm": round(actor.get_actor_location().z - job["start_z"], 1),
        "wait_seconds": round(now - job["t_start"], 3), "flying_seconds": round(now - job["t_flying"], 3) if job["t_flying"] is not None else None,
    }
    job["info"]["alive_after_kill"] = _kill(actor, job["source"])
    job["killed"] = True
    job["t_kill"] = now


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
    try:
        world = _world()
        if world is None:
            _finish(job, "pie ended")
            return
        if not unreal.SystemLibrary.is_valid(job["actor"]):
            _finish(job, "actor destroyed")
            return
        now = unreal.GameplayStatics.get_time_seconds(world)
        if not job["killed"] and now >= job["kill_at"] and _is_kill_ready(job, now):
            _kill_target(job, now)
        if not job["killed"]:
            if now - job["t_start"] > job["scenario"].get("kill_timeout", 1e9):
                _finish(job, "kill condition not met")
            return
        gaps = job["scenario"].get("pile_gaps") or []
        if len(job["pile_dropped"]) < min(len(gaps), len(job["others"])) and now - job["t_kill"] >= gaps[len(job["pile_dropped"])]:
            _drop_next_onto_corpse(job, now)
        frame = _sample(job, world, now)
        job["frames"].append(frame)
        if job["t_ragdoll"] is None and frame["sim"]:
            job["t_ragdoll"] = now
            job["info"]["bone_mass_kg"] = [round(job["mesh"].get_bone_mass(bone, True), 3) for bone in job["bones"]]
        if job["t_ragdoll"] is not None and job["shot_offsets"] and now >= job["t_ragdoll"] + job["shot_offsets"][0]:
            job["shot_offsets"].pop(0)
            unreal.SystemLibrary.execute_console_command(world, "Shot", unreal.GameplayStatics.get_player_controller(world, 0))
            job["shots_fired"].append(round(now, 4))
        if job["assist"] and job["t_ragdoll"] is not None and job["assist_slept"] is None:
            _advance_assist(job, frame, now)
        if job["t_ragdoll"] is not None and len(job["frames"]) > 1:
            game_delta = now - job["frames"][-2]["t"]
            _emulate_launch_clamp(job, now)
            _emulate_firm_damping(job, now, game_delta)
        if now - job["t_kill"] >= job["observe"]:
            _finish(job, "ok")
    except Exception as error:
        _finish(job, "error: " + repr(error))


def status():
    job = STATE["job"]
    if not job:
        return {"job": None}
    return {"label": job["label"], "done": job["done"], "result": job["result"], "frames": len(job["frames"]),
            "t_kill": job["t_kill"], "t_ragdoll": job["t_ragdoll"], "shots": len(job["shots_fired"])}

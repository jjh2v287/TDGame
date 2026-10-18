"""실행 환경: 언리얼 에디터 내장 Python(PIE 중). 직접 실행하지 않고 Tools/MonsterAI/pie_measure_ragdoll_jitter.py가 원격 실행으로 sys.modules['td_ragdoll_sampler']에 올려 쓴다.
실행 명령: (바깥 도구가 호출) start_job(...) → status() 폴링 → 끝나면 out_path JSON
출력: 대상 하나의 매 프레임 표본(물리 바디 뼈마다 렌더 위치·질량 중심·선속도·각속도, 컴포넌트 단위 깨어 있음, 컴포넌트 위치, 시간 늘이기, 가라앉기 여부)과 정적 정보(물리 에셋·바디 질량)
상태: 현행(M3-38). 처치는 TDCombatLibrary.TryApplyDamage(먼저 남은 체력을 kill_fraction까지 깎고 kill_delay 뒤 마무리 일격), 래그돌 시작 뒤 shot_offsets 시각에 Shot 연사
"""
import json

import unreal

STATE = {"handle": None, "job": None, "species_restore": [], "override_restore": []}
GAME_MODE_CLASS_PATH = "/Game/Combat/Blueprints/BP_TDCombatGameMode.BP_TDCombatGameMode_C"
ENUM_PROPERTIES = {"sleep_family": unreal.SleepFamily}


def _body_setups(physics_asset):
    return [setup for setup in unreal.ObjectIterator(unreal.BodySetup) if setup.get_outer() == physics_asset]


def resolve_physics_assets(mesh_paths, include_player):
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
    return assets


def apply_overrides(mesh_paths, include_player, body_values, self_collision_off):
    restore_overrides()
    changed = []
    for physics_asset in resolve_physics_assets(mesh_paths, include_player):
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
    for setup, originals, original_response in STATE["override_restore"]:
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
    for species, value in STATE["species_restore"]:
        species.set_editor_property("ragdoll_after_death_clip", value)
    STATE["species_restore"] = []


def start_job(label, target_kind, out_path, kill_fraction, kill_delay, observe_seconds, shot_offsets, force_ragdoll, assist=None, kill_others=False):
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
    now = unreal.GameplayStatics.get_time_seconds(world)
    STATE["job"] = {
        "label": label, "target_kind": target_kind, "out_path": out_path, "actor": actor, "source": source, "mesh": mesh,
        "bones": bones, "observe": observe_seconds, "kill_at": now + kill_delay, "killed": False, "t_kill": None,
        "t_ragdoll": None, "shot_offsets": list(shot_offsets), "shots_fired": [], "frames": [], "done": False, "result": None,
        "assist": assist, "assist_low_since": None, "assist_slept": None, "kill_others": kill_others, "others_killed": 0,
        "info": {
            "actor": actor.get_name(), "species": actor.get_species().get_name() if hasattr(actor, "get_species") and actor.get_species() else None,
            "physics_asset": physics_asset.get_path_name() if physics_asset else None, "bones": [str(bone) for bone in bones],
            "bone_mass_kg": [round(mesh.get_bone_mass(bone, True), 3) for bone in bones], "max_health": max_health,
            "kill_fraction": kill_fraction, "forced_ragdoll": forced, "assist": assist,
        },
    }
    if STATE["handle"] is None:
        STATE["handle"] = unreal.register_slate_post_tick_callback(_tick)
    return {"started": label, "bones": len(bones), "physics_asset": STATE["job"]["info"]["physics_asset"]}


def _finish(job, reason):
    job["done"] = True
    job["result"] = reason
    payload = {"label": job["label"], "target_kind": job["target_kind"], "result": reason, "t_kill": job["t_kill"],
               "t_ragdoll": job["t_ragdoll"], "t_assist_sleep": job["assist_slept"], "shots": job["shots_fired"], "info": job["info"], "frames": job["frames"]}
    with open(job["out_path"], "w", encoding="utf-8") as stream:
        json.dump(payload, stream)
    job["actor"] = job["mesh"] = job["source"] = None
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
        "sim": 1 if bones and mesh.is_simulating_physics(root_bone) else 0, "sink": 1 if sinking else 0,
        "awake": 1 if mesh.is_any_rigid_body_awake() else 0,
        "component": _round_vector(mesh.get_world_location()), "actor": _round_vector(job["actor"].get_actor_location()),
        "bones": rows,
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
        if not job["killed"] and now >= job["kill_at"]:
            if job["kill_others"]:
                for other in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDMonsterCharacter):
                    other_combat = other.get_component_by_class(unreal.TDCombatComponent)
                    if other != job["actor"] and other_combat.is_alive():
                        _apply_damage(other, job["source"], _raw_damage_for(other, other_combat.get_current_health() + 1.0))
                        job["others_killed"] += 1
                job["info"]["others_killed"] = job["others_killed"]
            combat = job["actor"].get_component_by_class(unreal.TDCombatComponent)
            _apply_damage(job["actor"], job["source"], _raw_damage_for(job["actor"], combat.get_current_health() + 1.0))
            job["info"]["alive_after_kill"] = combat.is_alive()
            job["killed"] = True
            job["t_kill"] = now
        if not job["killed"]:
            return
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

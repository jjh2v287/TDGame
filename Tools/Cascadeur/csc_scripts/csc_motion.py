"""Cascadeur 안 Python에서 실행: 리그된 캐릭터 .casc를 새 탭으로 열고, 시스템 쪽 동작 풀이(build_motion_keys.py)가 만든 프레임별 리그 점 위치·루트 위치를 키로 쓰고, 키 사이 구간 보간(베지어·선형·AI 인비트위닝+문장 설명)을 지정해 새 .casc로 저장한다(라이브러리).
실행: python Tools/Cascadeur/cascadeur_bridge.py call csc_motion.build_animation_scene --args-file <{"rig_scene_path", "keys_path", "output_scene_path"} JSON>  (PowerShell)
출력: output_scene_path(.casc), 결과 JSON(키 수·애니메이션 길이·키 프레임 역읽기 오차)
상태: 현행
"""
import json

import numpy as np

import csc

DEFAULT_INTERPOLATION = "clamped_bezier"
AI_MOTION_GENERATION_ACTION = "View.Inbetweening_RunMotionGeneration"
INTERPOLATIONS = {
    "bezier": csc.layers.layer.Interpolation.BEZIER,
    "clamped_bezier": csc.layers.layer.Interpolation.CLAMPED_BEZIER,
    "linear": csc.layers.layer.Interpolation.LINEAR,
    "step": csc.layers.layer.Interpolation.STEP,
    "fixed": csc.layers.layer.Interpolation.FIXED,
    "ai": csc.layers.layer.Interpolation.AI,
}


def load_scene_into_new_tab(scene_path):
    scene_manager = csc.app.get_application().get_scene_manager()
    view_scene = scene_manager.create_application_scene()
    scene_manager.set_current_scene(view_scene)
    csc.app.ProjectLoader.load_from(scene_path.replace("\\", "/"), view_scene.domain_scene())
    return view_scene


def load_scene_copy_for_bridge(scene, scene_path):
    view_scene = load_scene_into_new_tab(scene_path)
    return {"tab": view_scene.name(), "animation_size": view_scene.domain_scene().model_viewer().data_viewer().get_animation_size()}


def open_scene_tab(scene_path, reuse_open_tab=False):
    application = csc.app.get_application()
    scene_manager = application.get_scene_manager()
    if reuse_open_tab:
        wanted = scene_path.replace("\\", "/").lower()
        for view_scene in reversed(scene_manager.scenes()):
            if (view_scene.get_path_name() or "").replace("\\", "/").lower() == wanted:
                scene_manager.set_current_scene(view_scene)
                return view_scene
    if not application.get_data_source_manager().load_scene(scene_path.replace("\\", "/")):
        raise RuntimeError(f"Cascadeur could not load {scene_path}")
    view_scene = scene_manager.current_scene()
    if view_scene.get_path_name().replace("\\", "/").lower() != scene_path.replace("\\", "/").lower():
        raise RuntimeError(f"Loaded scene is not the current tab: {view_scene.get_path_name()}")
    return view_scene


def make_empty_tab(scene):
    scene_manager = csc.app.get_application().get_scene_manager()
    view_scene = scene_manager.create_application_scene()
    scene_manager.set_current_scene(view_scene)
    return {"tabs": len(scene_manager.scenes())}


def close_pipeline_tabs(scene, close_paths):
    scene_manager = csc.app.get_application().get_scene_manager()
    current = scene_manager.current_scene()
    wanted = [path.replace("\\", "/").lower() for path in close_paths]
    closed = []
    for view_scene in list(scene_manager.scenes()):
        path = (view_scene.get_path_name() or "").replace("\\", "/")
        matches = any(path.lower() == item or (item.endswith("/") and path.lower().startswith(item)) for item in wanted)
        if path and matches and view_scene.name() != current.name():
            scene_manager.remove_application_scene(view_scene)
            closed.append(path)
    return {"closed": closed, "tabs": len(scene_manager.scenes())}


def close_untitled_tabs(scene):
    scene_manager = csc.app.get_application().get_scene_manager()
    current = scene_manager.current_scene()
    closed = []
    for view_scene in list(scene_manager.scenes()):
        if not view_scene.get_path_name() and view_scene.name().startswith("untitled") and view_scene.name() != current.name():
            closed.append(view_scene.name())
            scene_manager.remove_application_scene(view_scene)
    return {"closed": closed, "tabs": len(scene_manager.scenes())}


def root_constraint_settings(domain_scene):
    import common.behavior_operations as behavior_operations

    model_viewer = domain_scene.model_viewer()
    behaviour_viewer = model_viewer.behaviour_viewer()
    data_viewer = model_viewer.data_viewer()
    settings = {}
    for object_id in model_viewer.get_objects():
        if not model_viewer.get_object_name(object_id).startswith("root_constraint"):
            continue
        for behaviour in behaviour_viewer.get_behaviours(object_id):
            if behaviour_viewer.get_behaviour_name(behaviour) != "Dynamic" or behavior_operations.dynamic_behavior_name(behaviour_viewer, data_viewer, behaviour) != "Constraint":
                continue
            for setting in behaviour_viewer.get_behaviour_settings_range(behaviour, "settingsWithSameNames"):
                settings.setdefault(data_viewer.get_setting(setting).name, []).append(setting)
    return settings


def configure_root_constraint(domain_scene, follow_rotation):
    settings = root_constraint_settings(domain_scene)
    if not settings:
        return {"present": False}
    data_viewer = domain_scene.model_viewer().data_viewer()
    before = {name: [data_viewer.get_setting_value(item) for item in items] for name, items in settings.items()}

    def modify(model, update, scene_updater):
        for setting in settings.get("Follow rotation", []):
            model.data_editor().set_setting_value(setting, bool(follow_rotation))

    changed = domain_scene.modify_update("TD root constraint follow rotation", modify)
    after = {name: [data_viewer.get_setting_value(item) for item in items] for name, items in root_constraint_settings(domain_scene).items()}
    return {"present": True, "changed": changed, "before": before, "after": after}


def open_scene_tab_for_bridge(scene, scene_path):
    view_scene = open_scene_tab(scene_path, reuse_open_tab=True)
    return {"tab": view_scene.name(), "animation_size": view_scene.domain_scene().model_viewer().data_viewer().get_animation_size()}


def object_id_by_name(model_viewer, name):
    object_ids = model_viewer.get_objects(name)
    if len(object_ids) != 1:
        raise RuntimeError(f"Expected one object named {name}, found {len(object_ids)}")
    return object_ids[0]


class TDRootWriter:
    def __init__(self, domain_scene, root_name):
        model_viewer = domain_scene.model_viewer()
        behaviour_viewer = model_viewer.behaviour_viewer()
        data_viewer = model_viewer.data_viewer()
        root_id = object_id_by_name(model_viewer, root_name)
        transform = behaviour_viewer.get_behaviour_by_name(root_id, "Transform")
        if transform.is_null():
            raise RuntimeError(f"{root_name} has no Transform behaviour")
        self.local_position_data = behaviour_viewer.get_behaviour_data(transform, "local_position")
        local_rotation = data_viewer.get_data_value(behaviour_viewer.get_behaviour_data(transform, "local_rotation"), 0).to_rotation_matrix()
        global_rotation = data_viewer.get_data_value(data_viewer.get_data_id(root_id, "Rotation"), 0).to_rotation_matrix()
        global_position = np.array(data_viewer.get_data_value(data_viewer.get_data_id(root_id, "Position"), 0), dtype=np.float64)
        local_position = np.array(data_viewer.get_data_value(self.local_position_data, 0), dtype=np.float64)
        self.parent_rotation = np.array(global_rotation, dtype=np.float64) @ np.array(local_rotation, dtype=np.float64).T
        self.parent_position = global_position - self.parent_rotation @ local_position

    def local_value(self, global_position):
        local = self.parent_rotation.T @ (np.array(global_position, dtype=np.float64) - self.parent_position)
        return local.astype(np.float32)


def write_keys(domain_scene, keys):
    model_viewer = domain_scene.model_viewer()
    data_viewer = model_viewer.data_viewer()
    layers_viewer = domain_scene.layers_viewer()
    last_frame = max(int(key["frame"]) for key in keys["keys"])
    point_data = {name: data_viewer.get_data_id(object_id_by_name(model_viewer, name), "Position") for name in keys["point_names"]}
    box_data = {name: (data_viewer.get_data_id(object_id_by_name(model_viewer, name), "Position"), data_viewer.get_data_id(object_id_by_name(model_viewer, name), "Rotation"))
                for name in keys.get("box_names", [])}
    root_writer = TDRootWriter(domain_scene, keys.get("root_name", "root"))
    layer_ids = [layer_id for layer_id in layers_viewer.all_layer_ids() if not layer_id.is_null()]

    final_key = max(keys["keys"], key=lambda key: int(key["frame"]))
    hold_key = dict(final_key, frame=last_frame + 1)

    def modify(model, update, scene_updater):
        layers_editor = model.layers_editor()
        data_editor = model.data_editor()
        for layer_id in layer_ids:
            layers_editor.set_section(csc.layers.layer.Section(), last_frame + 1, layer_id)
        model.fit_animation_size_by_layers()
        scene_updater.generate_update()
        for key in sorted(keys["keys"], key=lambda key: int(key["frame"])) + [hold_key]:
            frame = int(key["frame"])
            written = set()
            for name, position in key["points"].items():
                data_editor.set_data_value(point_data[name], frame, np.array(position, dtype=np.float32))
                written.add(point_data[name])
            for name, box in key.get("boxes", {}).items():
                position_data, rotation_data = box_data[name]
                data_editor.set_data_value(position_data, frame, np.array(box["position"], dtype=np.float32))
                data_editor.set_data_value(rotation_data, frame, csc.math.Rotation.from_quaternion(*box["quaternion_wxyz"]))
                written.update((position_data, rotation_data))
            if "root_position" in key:
                data_editor.set_data_value(root_writer.local_position_data, frame, root_writer.local_value(key["root_position"]))
                written.add(root_writer.local_position_data)
            for layer_id in layer_ids:
                layers_editor.set_fixed_interpolation_or_key_if_need(layer_id, frame, True)
            model.set_fixed_interpolation_if_need(written, frame)
            if frame <= last_frame:
                scene_updater.run_update(written, frame)
        layers_editor.normalize_sections(domain_scene)

    return domain_scene.modify_update("TD keys from motion spec", modify)


def set_interval_interpolations(domain_scene, keys):
    layer_ids = [layer_id for layer_id in domain_scene.layers_viewer().all_layer_ids() if not layer_id.is_null()]

    def modify(model, update, scene_updater):
        layers_editor = model.layers_editor()
        for key in keys["keys"]:
            interpolation = INTERPOLATIONS[key.get("interpolation", DEFAULT_INTERPOLATION)]
            description = key.get("ai_description", "")

            def change(section, interpolation=interpolation, description=description):
                section.interval.interpolation = interpolation
                if interpolation == csc.layers.layer.Interpolation.AI:
                    section.interval.description_for_ai_interpolation = description

            for layer_id in layer_ids:
                layers_editor.change_section(int(key["frame"]), layer_id, change)
        layers_editor.normalize_sections(domain_scene)
        interpolator = scene_updater.get_interpolator()
        interpolator.reload()
        interpolator.interpolate()

    return domain_scene.modify_update("TD interval interpolation", modify)


def read_back_errors(domain_scene, keys):
    model_viewer = domain_scene.model_viewer()
    data_viewer = model_viewer.data_viewer()
    worst = {"centimeters": 0.0, "frame": None, "point": None}
    for key in keys["keys"]:
        for name, position in key["points"].items():
            value = data_viewer.get_data_value(data_viewer.get_data_id(object_id_by_name(model_viewer, name), "Position"), int(key["frame"]))
            error = float(np.linalg.norm(np.array(value, dtype=np.float64) - np.array(position, dtype=np.float64)))
            if error > worst["centimeters"]:
                worst = {"centimeters": round(error, 4), "frame": int(key["frame"]), "point": name}
    return worst


def inbetweening_editor():
    application = csc.app.get_application()
    return application.get_tools_manager().get_tool("InbetweeningTool").editor(application.get_scene_manager().current_scene())


def set_ai_inbetweening_update(scene, enabled):
    editor = inbetweening_editor()
    previous = editor.get_update_parameter()
    editor.set_update_parameter(bool(enabled))
    return {"previous": previous, "now": editor.get_update_parameter()}


def select_frames_on_all_layers(domain_scene, first_frame, last_frame):
    layer_ids = [layer_id for layer_id in domain_scene.layers_viewer().all_layer_ids() if not layer_id.is_null()]

    def modify(model, update, scene_updater, session):
        session.take_layers_selector().set_full_selection_by_parts(layer_ids, int(first_frame), int(last_frame))

    return domain_scene.modify_with_session("TD select frames", modify)


def call_action(scene, action, first_frame=None, last_frame=None):
    domain_scene = csc.app.get_application().get_scene_manager().current_scene().domain_scene()
    selected = select_frames_on_all_layers(domain_scene, first_frame, last_frame) if first_frame is not None else None
    csc.app.get_application().get_action_manager().call_action(action)
    return {"action": action, "selected": selected}


def run_ai_motion_generation(scene, scene_path, first_frame, last_frame):
    view_scene = open_scene_tab(scene_path, reuse_open_tab=True)
    result = call_action(scene, AI_MOTION_GENERATION_ACTION, first_frame, last_frame)
    view_scene.save(scene_path.replace("\\", "/"))
    return dict(result, frames=[int(first_frame), int(last_frame)], scene_path=scene_path)


def build_animation_scene(scene, rig_scene_path, keys_path, output_scene_path):
    with open(keys_path, encoding="utf-8") as handle:
        keys = json.load(handle)
    view_scene = load_scene_into_new_tab(rig_scene_path)
    domain_scene = view_scene.domain_scene()
    data_viewer = domain_scene.model_viewer().data_viewer()
    root_constraint = configure_root_constraint(domain_scene, follow_rotation=keys.get("root_follow_rotation", False))
    if root_constraint["present"]:
        keys = dict(keys, keys=[{name: value for name, value in key.items() if name != "root_position"} for key in keys["keys"]])
    report = {"root_constraint": root_constraint, "keys_written": write_keys(domain_scene, keys), "size_after_keys": data_viewer.get_animation_size()}
    report["interpolation_set"] = set_interval_interpolations(domain_scene, keys)
    report["animation_size"] = data_viewer.get_animation_size()
    if not report["keys_written"] or report["animation_size"] <= max(int(key["frame"]) for key in keys["keys"]):
        raise RuntimeError(f"Key writing failed (see Cascadeur log): {report}")
    report["worst_key_error"] = read_back_errors(domain_scene, keys)
    view_scene.save(output_scene_path.replace("\\", "/"))
    report["output_scene_path"] = output_scene_path
    return report

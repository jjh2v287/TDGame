"""Cascadeur 안 Python에서 실행: 앱(버전·라이선스·도구 목록)과 현재 씬(객체 종류별 이름·애니메이션 길이·레이어)을 읽기 전용으로 요약한다(라이브러리).
실행: python Tools/Cascadeur/cascadeur_bridge.py call csc_inspect.describe_application | call csc_inspect.describe_scene_tabs | call csc_inspect.describe_scene --args '{"types": ["Point", "Box"]}'  (PowerShell)
출력: 표준 출력 JSON(cascadeur_bridge.py 결과의 result)
상태: 현행
"""
import csc


def describe_application(scene):
    application = csc.app.get_application()
    tools = [tool.name() if hasattr(tool, "name") else type(tool).__name__ for tool in application.get_tools_manager().tools()]
    return {
        "version": str(csc.SystemVariables.git_version()) if hasattr(csc, "SystemVariables") else None,
        "is_pro_features_available": application.is_pro_features_available(),
        "is_export_available": application.is_export_available(),
        "tools": sorted(tools),
    }


def list_tabs(scene):
    scene_manager = csc.app.get_application().get_scene_manager()
    current = scene_manager.current_scene()
    return [{"name": view_scene.name(), "path": view_scene.get_path_name(), "current": view_scene.name() == current.name(),
             "animation_size": view_scene.domain_scene().model_viewer().data_viewer().get_animation_size()} for view_scene in scene_manager.scenes()]


def describe_scene_tabs(scene):
    scene_manager = csc.app.get_application().get_scene_manager()
    current = scene_manager.current_scene()
    return {
        "tab_count": len(scene_manager.scenes()),
        "current_tab_name": current.name() if current else None,
        "current_tab_path": current.get_path_name() if current else None,
        "idle_scene_is_current_tab": bool(current) and current.domain_scene().model_viewer().get_objects() == scene.model_viewer().get_objects(),
        "current_tab_object_count": len(current.domain_scene().model_viewer().get_objects()) if current else 0,
    }


def describe_object(scene, name):
    domain_scene = csc.app.get_application().get_scene_manager().current_scene().domain_scene()
    model_viewer = domain_scene.model_viewer()
    behaviour_viewer = model_viewer.behaviour_viewer()
    data_viewer = model_viewer.data_viewer()
    object_ids = model_viewer.get_objects(name)
    if len(object_ids) != 1:
        return {"error": f"{len(object_ids)} objects named {name}"}
    behaviours = {}
    for behaviour in behaviour_viewer.get_behaviours(object_ids[0]):
        properties = {}
        for property_name in behaviour_viewer.get_behaviour_property_names(behaviour):
            for reader in (lambda: data_viewer.get_setting_value(behaviour_viewer.get_behaviour_setting(behaviour, property_name)),
                           lambda: repr(data_viewer.get_data_value(behaviour_viewer.get_behaviour_data(behaviour, property_name), 0)),
                           lambda: model_viewer.get_object_name(behaviour_viewer.get_behaviour_object(behaviour, property_name)),
                           lambda: behaviour_viewer.get_behaviour_string(behaviour, property_name)):
                try:
                    properties[property_name] = reader()
                    break
                except Exception:
                    continue
        behaviours[behaviour_viewer.get_behaviour_name(behaviour)] = properties
    return behaviours


def read_global_transform(data_viewer, object_id, frame, behaviour_viewer=None):
    transform_behaviour = behaviour_viewer.get_behaviour_by_name(object_id, "Transform") if behaviour_viewer is not None else None
    if transform_behaviour is not None and not transform_behaviour.is_null():
        position = data_viewer.get_data_value(behaviour_viewer.get_behaviour_data(transform_behaviour, "global_position"), frame)
        quaternion = data_viewer.get_data_value(behaviour_viewer.get_behaviour_data(transform_behaviour, "global_rotation"), frame).to_quaternion()
        return {"position": [float(value) for value in position], "quaternion_wxyz": [quaternion.w(), quaternion.x(), quaternion.y(), quaternion.z()]}
    transform = {}
    position_id = data_viewer.get_data_id(object_id, "Position")
    if not position_id.is_null():
        transform["position"] = [float(value) for value in data_viewer.get_data_value(position_id, frame)]
    rotation_id = data_viewer.get_data_id(object_id, "Rotation")
    if not rotation_id.is_null():
        quaternion = data_viewer.get_data_value(rotation_id, frame).to_quaternion()
        transform["quaternion_wxyz"] = [quaternion.w(), quaternion.x(), quaternion.y(), quaternion.z()]
    return transform


def read_pose(scene, frames=(0,), types=("Point", "Joint", "Box")):
    scene = csc.app.get_application().get_scene_manager().current_scene().domain_scene()
    model_viewer = scene.model_viewer()
    data_viewer = model_viewer.data_viewer()
    behaviour_viewer = model_viewer.behaviour_viewer()
    pose = {}
    for object_id in model_viewer.get_objects():
        type_name = model_viewer.get_object_type_name(object_id)
        if type_name not in types:
            continue
        parent_name = None
        basic = behaviour_viewer.get_behaviour_by_name(object_id, "Basic")
        if not basic.is_null():
            parent_id = behaviour_viewer.get_behaviour_object(basic, "parent")
            parent_name = None if parent_id.is_null() else model_viewer.get_object_name(parent_id)
        pose[model_viewer.get_object_name(object_id)] = {
            "type": type_name,
            "parent": parent_name,
            "frames": {str(frame): read_global_transform(data_viewer, object_id, frame, behaviour_viewer if type_name == "Joint" else None) for frame in frames},
        }
    return {"animation_size": data_viewer.get_animation_size(), "objects": pose}


def describe_scene(scene, types=None, maximum_names=400):
    scene = csc.app.get_application().get_scene_manager().current_scene().domain_scene()
    model_viewer = scene.model_viewer()
    behaviour_viewer = model_viewer.behaviour_viewer()
    names_by_type = {}
    for object_id in model_viewer.get_objects():
        type_name = model_viewer.get_object_type_name(object_id)
        names_by_type.setdefault(type_name, []).append(model_viewer.get_object_name(object_id))
    joint_names = sorted(model_viewer.get_object_name(behaviour_viewer.get_behaviour_owner(behaviour)) for behaviour in behaviour_viewer.get_behaviours("Joint"))
    selected_types = set(types) if types else set()
    layers_viewer = scene.layers_viewer()
    return {
        "object_count": sum(len(names) for names in names_by_type.values()),
        "counts_by_type": {type_name: len(names) for type_name, names in sorted(names_by_type.items())},
        "names_by_type": {type_name: sorted(names)[:maximum_names] for type_name, names in sorted(names_by_type.items()) if type_name in selected_types},
        "joint_names": joint_names[:maximum_names],
        "animation_size": model_viewer.data_viewer().get_animation_size(),
        "current_frame": scene.get_current_frame(),
        "layer_count": len(list(layers_viewer.all_layer_ids())),
    }

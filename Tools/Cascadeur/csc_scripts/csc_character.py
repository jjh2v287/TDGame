"""Cascadeur 안 Python에서 실행: 언리얼에서 내보낸 캐릭터 FBX를 새 씬 탭으로 가져오고 UE5 퀵 리깅 템플릿으로 리그를 만든 뒤 .casc로 저장한다(라이브러리).
실행: python Tools/Cascadeur/cascadeur_bridge.py call csc_character.build_rigged_scene --args '{"fbx_path": "...", "scene_path": "...", "template_path": "..."}'  (PowerShell)
출력: scene_path(.casc), 결과 JSON(조인트·점·박스 수, 단계별 성공 여부)
상태: 현행
"""
import csc

RIG_POINT_COLOR = [0.0, 0.5, 0.0]


def current_view_scene():
    return csc.app.get_application().get_scene_manager().current_scene()


def make_scene_tab():
    scene_manager = csc.app.get_application().get_scene_manager()
    view_scene = scene_manager.create_application_scene()
    scene_manager.set_current_scene(view_scene)
    return view_scene


def count_behaviours(domain_scene, behaviour_name):
    return len(domain_scene.model_viewer().behaviour_viewer().get_behaviours(behaviour_name))


def count_objects_of_type(domain_scene, type_name):
    model_viewer = domain_scene.model_viewer()
    return sum(1 for object_id in model_viewer.get_objects() if model_viewer.get_object_type_name(object_id) == type_name)


def save_current_scene(scene, scene_path):
    view_scene = current_view_scene()
    view_scene.save(scene_path.replace("\\", "/"))
    return {"scene_path": scene_path, "tab_name": view_scene.name(), "tab_path": view_scene.get_path_name()}


def import_fbx_scene(view_scene, fbx_path):
    loader = csc.app.get_application().get_tools_manager().get_tool("FbxSceneLoader").get_fbx_loader(view_scene)
    loader.import_scene(fbx_path.replace("\\", "/"))
    return count_behaviours(view_scene.domain_scene(), "Joint")


def turn_on_rig_mode_for_all_joints(domain_scene):
    import rig_mode.on as rig_mode_on

    behaviour_viewer = domain_scene.model_viewer().behaviour_viewer()
    joint_ids = [behaviour_viewer.get_behaviour_owner(behaviour) for behaviour in behaviour_viewer.get_behaviours("Joint")]
    undo_context = rig_mode_on.cc.Camera_context()
    redo_context = rig_mode_on.cc.Camera_context()

    def modify(model, update, scene_updater, session):
        behaviour_editor = model.behaviour_editor()
        rig_info_object = update.root().create_object("Rig info").object_id()
        rig_info = behaviour_editor.add_behaviour(rig_info_object, "RigInfo")
        behaviour_editor.set_behaviour_model_objects_to_range(rig_info, "related_joints", joint_ids)
        scene_updater.generate_update()
        session.take_selector().select({rig_info_object}, rig_info_object)
        rig_mode_on.on(model, update, scene_updater, session, RIG_POINT_COLOR, None, rig_info_object, undo_context, redo_context)

    return domain_scene.modify_update_with_session("Rig mode on", modify)


def apply_quick_rig_template(view_scene, template_path):
    rigging_window = csc.app.get_application().get_tools_manager().get_tool("RiggingToolWindowTool").editor(view_scene)
    rigging_window.set_is_create_autoposing(True)
    rigging_window.create_from_qrt_by_fileName(template_path.replace("\\", "/"))
    return rigging_window.is_create_qrt()


def turn_off_rig_mode(domain_scene):
    import rig_mode.off as rig_mode_off

    return rig_mode_off.run(domain_scene, True)


def build_rigged_scene(scene, fbx_path, scene_path, template_path):
    view_scene = make_scene_tab()
    domain_scene = view_scene.domain_scene()
    report = {"joints_after_import": import_fbx_scene(view_scene, fbx_path)}
    report["rig_mode_on"] = turn_on_rig_mode_for_all_joints(domain_scene)
    report["quick_rig_created"] = apply_quick_rig_template(view_scene, template_path)
    report["rig_mode_off"] = turn_off_rig_mode(domain_scene)
    report["points"] = count_objects_of_type(domain_scene, "Point")
    report["boxes"] = count_objects_of_type(domain_scene, "Box")
    report["rig_infos"] = count_behaviours(domain_scene, "RigInfo")
    view_scene.save(scene_path.replace("\\", "/"))
    report["scene_path"] = scene_path
    return report

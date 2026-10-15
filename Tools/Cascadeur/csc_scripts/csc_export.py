"""Cascadeur 안 Python에서 실행: 애니메이션 .casc를 새 탭으로 열고 라이선스가 내보내기를 허용하면 관절 애니메이션을 FBX(바이너리·베이크·오일러 필터)로 내보낸다. Free(basic) 라이선스는 FBX 내보내기가 막혀 있어 exported=false와 이유를 돌려준다(라이브러리).
실행: python Tools/Cascadeur/cascadeur_bridge.py call csc_export.export_animation_fbx --args-file <{"scene_path", "fbx_path"} JSON>  (PowerShell)
출력: fbx_path(.fbx), 결과 JSON(exported·reason·license)
상태: 현행
"""
import os

import csc

import csc_motion


def export_animation_fbx(scene, scene_path, fbx_path, up_axis="Y"):
    application = csc.app.get_application()
    license_state = {"is_export_available": application.is_export_available(), "is_pro_features_available": application.is_pro_features_available()}
    if not license_state["is_export_available"]:
        return {"exported": False, "reason": "Cascadeur license does not allow FBX export (Free/basic exports .casc only; Indie or higher is required).", "license": license_state}
    view_scene = csc_motion.open_scene_tab(scene_path, reuse_open_tab=True)
    settings = csc.fbx.FbxSettings()
    settings.mode = csc.fbx.FbxSettingsMode.Binary
    settings.up_axis = getattr(csc.fbx.FbxSettingsAxis, up_axis)
    settings.bake_animation = True
    settings.apply_euler_filter = True
    loader = application.get_tools_manager().get_tool("FbxSceneLoader").get_fbx_loader(view_scene)
    loader.set_settings(settings)
    os.makedirs(os.path.dirname(fbx_path), exist_ok=True)
    if os.path.isfile(fbx_path):
        os.remove(fbx_path)
    loader.export_joints(fbx_path.replace("\\", "/"))
    return {"exported": os.path.isfile(fbx_path), "fbx_path": fbx_path, "license": license_state,
            "animation_size": view_scene.domain_scene().model_viewer().data_viewer().get_animation_size()}

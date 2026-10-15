"""Cascadeur 안 Python에서 실행: 현재 씬 탭의 활성 뷰포트를 메시만 보이는 모드와 지정 카메라로 맞추고, 프레임 설정과 PNG 렌더(RenderToFile)를 별도 호출로 수행한다(같은 호출 안의 프레임 변경은 렌더에 반영되지 않는다, 검증 캡처용 라이브러리).
실행: cascadeur_bridge.py call csc_capture.prepare_capture → call csc_capture.set_frame → call csc_capture.render_image 순서(build_cascadeur_animation.py capture 단계가 자동으로 부른다)  (PowerShell)
출력: render_image의 path(.png, 비동기로 생성되므로 호출 쪽이 파일 생성을 기다린다)
상태: 현행
"""
import os

import numpy as np

import csc


def current_view_scene():
    return csc.app.get_application().get_scene_manager().current_scene()


def prepare_capture(scene, camera_position, camera_target, viewport_mode="View"):
    viewport = current_view_scene().active_viewport().domain_viewport()
    previous_mode = str(viewport.mode_visualizers())
    viewport.set_mode_visualizers(getattr(csc.view.ViewportMode, viewport_mode))
    camera = csc.view.SphericalCameraStruct()
    camera.position = np.array(camera_position, dtype=np.float32)
    camera.target = np.array(camera_target, dtype=np.float32)
    camera.type = viewport.camera_struct().type
    viewport.set_camera_struct(camera)
    return {"previous_mode": previous_mode, "tab": current_view_scene().name()}


def set_frame(scene, frame):
    domain_scene = current_view_scene().domain_scene()
    domain_scene.set_current_frame(int(frame))
    return {"frame": domain_scene.get_current_frame()}


def render_image(scene, path, width=720, height=720):
    parameters = csc.tools.RenderParameters()
    parameters.width = int(width)
    parameters.height = int(height)
    parameters.samples = 1
    os.makedirs(os.path.dirname(path), exist_ok=True)
    csc.app.get_application().get_tools_manager().get_tool("RenderToFile").take_image(current_view_scene(), parameters, path.replace("\\", "/"))
    return {"path": path}

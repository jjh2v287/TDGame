"""Cascadeur 안 Python에서 실행: 첫 scene_idle 이벤트에서 내장 MCP 스크립트 서버(127.0.0.1:8765)를 한 번 켠다(사용자가 Stop으로 끈 뒤에는 다시 켜지 않는다).
실행: 직접 실행하지 않는다. python Tools/Cascadeur/setup_cascadeur.py 가 사용자 settings.json의 Python.Events에 td_events를 등록하면 Cascadeur가 부른다  (PowerShell)
출력: Cascadeur 이벤트 로그 "[Cascadeur MCP] listening at ..."
상태: 현행
"""
import traceback

_has_attempted_start = False


def run(scene):
    global _has_attempted_start
    if _has_attempted_start:
        return
    _has_attempted_start = True
    try:
        import scripts.mcp.start_server as start_server
        start_server.run(scene)
    except Exception:
        scene.error("TD autostart MCP failed: " + traceback.format_exc())

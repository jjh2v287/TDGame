"""실행 중인 언리얼 에디터 안에서 Python 코드를 실행한다 (PythonScriptPlugin 원격 실행 사용).

사용:
  python Tools/run_in_editor.py <script.py>          # 파일 실행, 에디터 stdout을 그대로 출력
  python Tools/run_in_editor.py -c "import unreal; print(unreal.SystemLibrary.get_project_directory())"
  python Tools/run_in_editor.py --ensure <script.py> # 에디터가 꺼져 있으면 켜고, 원격 실행이 꺼져 있으면 켠 뒤 실행

원격 실행이 꺼져 있으면 자동으로 켜려고 시도한다(MCP ConfigSettings → DefaultEngine.ini에 기록됨).
필요한 것: 에디터가 이 프로젝트를 열고 있을 것. 반환 코드 0 = 성공, 1 = 스크립트 예외, 2 = 에디터 연결 실패.
스크립트 안에서 `unreal` 모듈 전체를 쓸 수 있다(에셋 생성·레벨 편집·저장·PIE 조회 등).
"""
import os
import sys
import time

sys.path.insert(0, r"C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import remote_execution as re_mod  # noqa: E402


def _find_node(rex, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        nodes = rex.remote_nodes
        if nodes:
            return nodes[0]
        time.sleep(0.5)
    return None


def run_code(code, timeout=12, try_enable=True):
    rex = re_mod.RemoteExecution()
    rex.start()
    node = _find_node(rex, timeout)
    if node is None and try_enable:
        rex.stop()
        try:
            import ue_editor
            ue_editor.set_remote_execution(True)
        except Exception as e:
            return {"success": False, "lines": [f"NO REMOTE NODES and enabling failed: {e}"], "connected": False}
        time.sleep(3)
        rex = re_mod.RemoteExecution()
        rex.start()
        node = _find_node(rex, timeout)
    if node is None:
        rex.stop()
        return {"success": False, "lines": ["NO REMOTE NODES (editor not running or remote execution disabled)"], "connected": False}
    rex.open_command_connection(node["node_id"])
    try:
        res = rex.run_command(code, exec_mode=re_mod.MODE_EXEC_FILE)
    finally:
        rex.close_command_connection()
        rex.stop()
    lines = [f"[{o.get('type')}] {o.get('output')}" for o in res.get("output", [])]
    if res.get("result") not in (None, "None", ""):
        lines.append("RESULT: " + str(res.get("result")))
    return {"success": bool(res.get("success")), "lines": lines, "connected": True}


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    ensure = "--ensure" in args
    if ensure:
        args.remove("--ensure")
        import ue_editor
        if ue_editor.cmd_ensure() != 0:
            return 2
    if args[0] == "-c":
        code = " ".join(args[1:]) if len(args) > 2 else args[1]
    else:
        code = open(args[0], encoding="utf-8").read()
    res = run_code(code)
    for line in res["lines"]:
        print(line)
    if not res["connected"]:
        return 2
    print("SUCCESS" if res["success"] else "FAILED")
    return 0 if res["success"] else 1


if __name__ == "__main__":
    sys.exit(main())

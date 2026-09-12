"""언리얼 에디터 수명주기 도구 (에디터 밖에서 실행).

사용:
  python Tools/ue_editor.py status              # 에디터 프로세스·MCP 포트(8000)·Python 원격 실행 상태
  python Tools/ue_editor.py start [--wait 240]  # 에디터 실행 후 MCP 포트가 열릴 때까지 대기
  python Tools/ue_editor.py stop                # 에디터 안에서 quit_editor() 호출 후 종료 대기 (저장 안 된 패키지가 있으면 목록 출력 후 중단, --force 로 강제)
  python Tools/ue_editor.py build               # Build.bat TDGameEditor Win64 Development (에디터가 켜져 있으면 중단)
  python Tools/ue_editor.py restart             # stop → build → start (새 C++ 모듈/클래스 추가 후 표준 절차)
  python Tools/ue_editor.py python on|off       # Python 원격 실행(remote execution) 켜기/끄기 (MCP ConfigSettings 사용)
  python Tools/ue_editor.py ensure              # 꺼져 있으면 start, 원격 실행이 꺼져 있으면 켠다

종료 코드 0 = 성공. 모든 경로는 이 파일 기준 상대 경로에서 계산한다.
"""
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = r"C:\Program Files\Epic Games\UE_5.8"
UPROJECT = os.path.join(ROOT, "TDGame.uproject")
EDITOR_EXE = os.path.join(ENGINE, "Engine", "Binaries", "Win64", "UnrealEditor.exe")
BUILD_BAT = os.path.join(ENGINE, "Engine", "Build", "BatchFiles", "Build.bat")
MCP_PORT = 8000
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def editor_pids():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq UnrealEditor.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return [line.split('","')[1].strip('"') for line in out.splitlines() if line.startswith('"UnrealEditor.exe"')]


def port_open():
    out = subprocess.run(["netstat", "-ano"], capture_output=True, text=True).stdout
    return any(f":{MCP_PORT} " in line and "LISTENING" in line for line in out.splitlines())


def remote_execution_enabled():
    ini = os.path.join(ROOT, "Config", "DefaultEngine.ini")
    if not os.path.exists(ini):
        return False
    text = open(ini, encoding="utf-8", errors="replace").read()
    return re.search(r"bRemoteExecution\s*=\s*True", text) is not None


def mcp_call(toolset, tool, args):
    import uemcp
    sid = uemcp.connect()
    return uemcp.call_tool(sid, "call_tool", {"toolset_name": toolset, "tool_name": tool, "arguments": args})


def set_remote_execution(enabled):
    res = mcp_call("ConfigSettingsToolset.ConfigSettingsToolset", "SetSectionProperties",
                   {"containerName": "Project", "categoryName": "Plugins", "sectionName": "Python", "propertiesJson": json.dumps({"bRemoteExecution": bool(enabled)})})
    ok = "true" in json.dumps(res).lower()
    print("remote execution", "enabled" if enabled else "disabled", "->", "ok" if ok else res)
    return ok


def cmd_status():
    pids = editor_pids()
    print(f"editor running: {bool(pids)} pids={pids}")
    print(f"mcp port {MCP_PORT} listening: {port_open()}")
    print(f"python remote execution (ini): {remote_execution_enabled()}")
    return 0


def wait_port(timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if port_open():
            return True
        time.sleep(3)
    return False


def cmd_start(timeout=240):
    if editor_pids():
        print("editor already running")
        return 0 if wait_port(timeout) else 1
    subprocess.Popen([EDITOR_EXE, UPROJECT], creationflags=getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0), close_fds=True)
    print("editor launched, waiting for MCP port...")
    if wait_port(timeout):
        print("MCP port open")
        return 0
    print("timeout waiting for MCP port")
    return 1


def run_in_editor(code):
    import run_in_editor
    return run_in_editor.run_code(code)


def cmd_stop(force=False):
    if not editor_pids():
        print("editor not running")
        return 0
    check = ("import unreal\n"
             "d=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]+[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]\n"
             "print('DIRTY:' + ','.join(d))\n")
    res = run_in_editor(check)
    dirty = []
    for line in res.get("lines", []):
        if "DIRTY:" in line:
            dirty = [x for x in line.split("DIRTY:")[1].strip().split(",") if x]
    if dirty and not force:
        print("unsaved packages, refusing to stop (use --force):", dirty)
        return 2
    run_in_editor("import unreal\nunreal.SystemLibrary.quit_editor()\n")
    deadline = time.time() + 120
    while time.time() < deadline:
        if not editor_pids():
            print("editor closed")
            return 0
        time.sleep(2)
    print("editor still running after 120s")
    return 1


def cmd_build():
    if editor_pids():
        print("editor is running; stop it first (build needs the editor closed for new modules/classes)")
        return 2
    proc = subprocess.run([BUILD_BAT, "TDGameEditor", "Win64", "Development", f"-Project={UPROJECT}", "-WaitMutex", "-NoHotReload"], capture_output=True, text=True, errors="replace")
    log = proc.stdout + proc.stderr
    log_path = os.path.join(ROOT, "Saved", "Logs", "TDGame_Build.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    open(log_path, "w", encoding="utf-8").write(log)
    for line in log.splitlines():
        if "error" in line.lower() or line.startswith("Result:") or "Total execution time" in line:
            print(line[:300])
    ok = "Result: Succeeded" in log
    print("build", "succeeded" if ok else f"FAILED (log: {log_path})")
    return 0 if ok else 1


def cmd_restart(force=False):
    rc = cmd_stop(force)
    if rc != 0:
        return rc
    rc = cmd_build()
    if rc != 0:
        return rc
    return cmd_start()


def cmd_ensure():
    if not editor_pids() or not port_open():
        rc = cmd_start()
        if rc != 0:
            return rc
    if not remote_execution_enabled():
        set_remote_execution(True)
        time.sleep(3)
    return cmd_status()


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    cmd = args[0]
    force = "--force" in args
    if cmd == "status":
        return cmd_status()
    if cmd == "start":
        timeout = int(args[args.index("--wait") + 1]) if "--wait" in args else 240
        return cmd_start(timeout)
    if cmd == "stop":
        return cmd_stop(force)
    if cmd == "build":
        return cmd_build()
    if cmd == "restart":
        return cmd_restart(force)
    if cmd == "ensure":
        return cmd_ensure()
    if cmd == "python" and len(args) > 1:
        return 0 if set_remote_execution(args[1].lower() == "on") else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())

"""시스템 Python에서 실행: Cascadeur 내장 MCP 스크립트 서버(127.0.0.1:8765)로 Cascadeur 안의 Tools/Cascadeur/csc_scripts/csc_*.py 함수를 실행하는 클라이언트(라이브러리 겸 명령줄).
실행: python Tools/Cascadeur/cascadeur_bridge.py health | ensure | restart | call <csc_모듈>.<함수> [--args '<JSON>' | --args-file <JSON 파일>] [--timeout 초] | eval "<식>"  (PowerShell 5.1은 --args의 따옴표를 지우므로 --args-file)
출력: 표준 출력 JSON(작업 결과 {ok, result, stdout, seconds}), 실패 시 종료 코드 1
상태: 현행
"""
import argparse
import ast
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

TOOLS_CASCADEUR = Path(__file__).resolve().parent
CSC_SCRIPTS = TOOLS_CASCADEUR / "csc_scripts"
sys.path.insert(0, str(TOOLS_CASCADEUR.parent))
from ue_editor import CASCADEUR_EXE  # noqa: E402

SERVER_URL = "http://127.0.0.1:8765"
SERVER_QUEUE_TIMEOUT_SECONDS = 30.0
STARTUP_WAIT_SECONDS = 180.0
CLOSE_WAIT_SECONDS = 30.0


class TDCascadeurBridgeError(RuntimeError):
    pass


def read_health(timeout_seconds=3.0):
    try:
        with urllib.request.urlopen(f"{SERVER_URL}/health", timeout=timeout_seconds) as response:
            return json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, OSError, ValueError):
        return None


def post_code(code):
    body = json.dumps({"code": code}).encode("utf-8")
    request = urllib.request.Request(f"{SERVER_URL}/run", data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=SERVER_QUEUE_TIMEOUT_SECONDS + 30.0) as response:
        return json.loads(response.read().decode("utf-8"))


def is_cascadeur_running():
    listing = subprocess.run(["tasklist", "/FI", "IMAGENAME eq cascadeur.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return any(line.startswith('"cascadeur.exe"') for line in listing.splitlines())


def ensure_server(wait_seconds=STARTUP_WAIT_SECONDS):
    if read_health():
        return {"running": True, "started_cascadeur": False}
    started = False
    if not is_cascadeur_running():
        subprocess.Popen([CASCADEUR_EXE], cwd=str(Path(CASCADEUR_EXE).parent))
        started = True
    deadline = time.time() + wait_seconds
    while time.time() < deadline:
        if read_health():
            return {"running": True, "started_cascadeur": started}
        time.sleep(2.0)
    hint = ("Cascadeur is running but the MCP script server is off: run python Tools/Cascadeur/setup_cascadeur.py and restart Cascadeur, "
            "or click Commands > MCP > Start script server once.")
    raise TDCascadeurBridgeError(hint)


def restart_cascadeur():
    if is_cascadeur_running():
        subprocess.run(["taskkill", "/IM", "cascadeur.exe"], capture_output=True, text=True)
        deadline = time.time() + CLOSE_WAIT_SECONDS
        while is_cascadeur_running() and time.time() < deadline:
            time.sleep(1.0)
        if is_cascadeur_running():
            raise TDCascadeurBridgeError("Cascadeur did not close within 30 s (unsaved scene dialog?). Save or close it by hand, then run ensure.")
    return ensure_server()


def runtime_expression(call_expression):
    tools = str(CSC_SCRIPTS)
    return (f"(__import__('sys').path.insert(0, {tools!r}) if {tools!r} not in __import__('sys').path else None, "
            f"__import__('csc_runtime').{call_expression})[1]")


def decode_value(response):
    if not response.get("ok"):
        return None
    value = response.get("value") or ""
    if not value:
        return None
    return json.loads(ast.literal_eval(value))


def evaluate(expression):
    response = post_code(expression)
    if not response.get("ok"):
        raise TDCascadeurBridgeError(response.get("error") or json.dumps(response))
    return response.get("value")


def call(target, arguments=None, timeout_seconds=900.0):
    module_name, function_name = target.rsplit(".", 1)
    job_id = uuid.uuid4().hex[:12]
    arguments_json = json.dumps(arguments or {})
    response = post_code(runtime_expression(f"run_job({job_id!r}, {module_name!r}, {function_name!r}, {arguments_json!r}, scene)"))
    record = decode_value(response)
    if record is not None:
        return record
    if "Timed out" not in (response.get("error") or ""):
        raise TDCascadeurBridgeError(response.get("error") or json.dumps(response))
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        record = decode_value(post_code(runtime_expression(f"read_job({job_id!r})")))
        if record and record.get("state") == "done":
            return record
        if record and record.get("state") == "unknown":
            raise TDCascadeurBridgeError("Job was cancelled before it started (Cascadeur idle event did not run within 30 s).")
        time.sleep(1.0)
    raise TDCascadeurBridgeError(f"Job {job_id} did not finish in {timeout_seconds} s.")


def require_success(record):
    if not record.get("ok"):
        raise TDCascadeurBridgeError(record.get("error") or json.dumps(record))
    return record.get("result")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("health")
    commands.add_parser("ensure")
    commands.add_parser("restart")
    call_parser = commands.add_parser("call")
    call_parser.add_argument("target")
    call_parser.add_argument("--args", default="{}")
    call_parser.add_argument("--args-file", help="JSON file with the arguments (PowerShell 5.1 strips quotes from --args)")
    call_parser.add_argument("--timeout", type=float, default=900.0)
    eval_parser = commands.add_parser("eval")
    eval_parser.add_argument("expression")
    arguments = parser.parse_args()
    try:
        if arguments.command == "health":
            output = {"health": read_health(), "cascadeur_running": is_cascadeur_running()}
        elif arguments.command == "ensure":
            output = ensure_server()
        elif arguments.command == "restart":
            output = restart_cascadeur()
        elif arguments.command == "eval":
            output = {"value": evaluate(arguments.expression)}
        else:
            call_arguments = json.loads(Path(arguments.args_file).read_text(encoding="utf-8") if arguments.args_file else arguments.args)
            output = call(arguments.target, call_arguments, arguments.timeout)
    except (TDCascadeurBridgeError, urllib.error.URLError, OSError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=False))
        raise SystemExit(1)
    print(json.dumps(output, ensure_ascii=False, indent=1, default=str))
    if isinstance(output, dict) and output.get("ok") is False:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

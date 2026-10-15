"""Cascadeur 안 Python에서 실행: cascadeur_bridge.py가 보낸 작업(모듈.함수 + JSON 인수)을 실행하고 결과를 JSON 문자열로 돌려준다(라이브러리, 30초 대기 초과 작업은 JOBS에서 다시 읽는다).
실행: 직접 실행하지 않는다. python Tools/Cascadeur/cascadeur_bridge.py call <csc_모듈>.<함수> 가 불러 쓴다  (PowerShell)
출력: 작업 결과 JSON 문자열
상태: 현행
"""
import contextlib
import importlib
import io
import json
import time
import traceback

JOBS = {}
MAXIMUM_STDOUT_CHARACTERS = 20000


def run_job(job_id, module_name, function_name, arguments_json, scene):
    started = time.time()
    JOBS[job_id] = {"state": "running"}
    stdout = io.StringIO()
    try:
        module = importlib.reload(importlib.import_module(module_name))
        with contextlib.redirect_stdout(stdout):
            result = getattr(module, function_name)(scene, **json.loads(arguments_json))
        record = {"ok": True, "result": result}
    except Exception:
        record = {"ok": False, "error": traceback.format_exc()}
    record.update(state="done", stdout=stdout.getvalue()[-MAXIMUM_STDOUT_CHARACTERS:], seconds=round(time.time() - started, 3))
    JOBS[job_id] = record
    return json.dumps(record, default=str)


def read_job(job_id):
    return json.dumps(JOBS.get(job_id, {"state": "unknown"}), default=str)

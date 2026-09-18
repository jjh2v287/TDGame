"""에디터 밖에서 실행: 언리얼 MCP AutomationTestToolset으로 자동화 테스트를 발견·실행하고 실패 항목만 요약한다.
실행: python Tools/check_automation_tests.py [--filter TDGame.Combat] [--timeout 600] (PowerShell 또는 Git Bash; 에디터가 열려 있어야 하므로 먼저 python Tools/ue_editor.py ensure)
출력: Saved/AgentOps/automation_tests_<YYYYMMDD-HHMM>.json (전체 결과), 표준 출력에 SUMMARY 한 줄과 FAIL 목록
상태: 현행 (2026-09-18 Claude 홈 tools/run_tests.py에서 이관, D-17)
"""
import argparse
import datetime
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uemcp  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLSET = "AutomationTestToolset.AutomationTestToolset"


def call(sid, tool, args):
    res = uemcp.call_tool(sid, "call_tool", {"toolset_name": TOOLSET, "tool_name": tool, "arguments": args})
    result = res.get("result", {})
    text = "".join(c.get("text", "") for c in result.get("content", []))
    if result.get("isError") or "error" in res:
        raise SystemExit(f"{tool} failed: {text or res.get('error')}")
    try:
        parsed = json.loads(text)
    except Exception:
        return text
    return parsed.get("returnValue", parsed) if isinstance(parsed, dict) else parsed


def as_obj(value):
    if isinstance(value, str):
        try:
            return json.loads(value)
        except Exception:
            return value
    return value


def is_finished(status):
    if not isinstance(status, dict):
        return False
    if status.get("state") in ("Idle", "Ready", "Finished", "Complete", "Completed"):
        return True
    return status.get("activeTests", status.get("active", 1)) == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--filter", default="TDGame.", help="테스트 이름 접두어 (기본 TDGame.)")
    parser.add_argument("--timeout", type=int, default=600, help="완료 대기 초")
    args = parser.parse_args()

    sid = uemcp.connect()
    call(sid, "DiscoverTests", {"bForceRediscover": False})
    listed = as_obj(call(sid, "ListTests", {"nameFilter": args.filter, "tagFilter": "", "limit": 0}))
    tests = listed.get("tests", []) if isinstance(listed, dict) else listed
    if not tests:
        print(f"SUMMARY passed=0 failed=0 total=0 (필터 '{args.filter}'에 해당하는 테스트 없음)")
        return 1

    started = time.time()
    call(sid, "RunTests", {"testNames": tests})
    status = None
    while time.time() - started < args.timeout:
        status = as_obj(call(sid, "GetTestStatus", {}))
        if is_finished(status):
            break
        time.sleep(1)

    results = as_obj(call(sid, "GetTestResults", {}))
    out_dir = os.path.join(ROOT, "Saved", "AgentOps")
    os.makedirs(out_dir, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M")
    out_path = os.path.join(out_dir, f"automation_tests_{stamp}.json")
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump({"filter": args.filter, "status": status, "results": results}, handle, ensure_ascii=False, indent=1)

    items = results.get("tests", results.get("results", [])) if isinstance(results, dict) else results
    if not isinstance(items, list):
        items = []
    passed = failed = 0
    for test in items:
        state = str(test.get("state", test.get("status", "")))
        ok = state.lower() in ("success", "passed", "pass", "succeeded")
        passed += ok
        failed += not ok
        if not ok:
            detail = str(test.get("errors", test.get("entries", "")))[:300]
            print(f"FAIL: {test.get('name', test.get('path'))} | {state} | {detail}")
    print(f"SUMMARY passed={passed} failed={failed} total={len(items)} -> {os.path.relpath(out_path, ROOT)}")
    return 0 if failed == 0 and items else 1


if __name__ == "__main__":
    sys.exit(main())

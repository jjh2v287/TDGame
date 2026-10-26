"""실행 환경: 시스템 Python 3(에디터 불필요)
실행 명령: python -B Tools/FluidFX/look_merge_fixtures/run_fixtures.py [<fixture 폴더>]
출력: 하위 폴더(canonical.json·조각·expect.json·expected.json)마다 fluid_material_look.py merge 를 임시 폴더에서 돌려 종료 코드·충돌·형식 문제·변경·새 항목·결과 바이트를 대조하고, 종료 0 사례는 두 번 실행 바이트 동일·결과를 정본으로 한 재병합 무변경·--out 없는 제자리 쓰기·--dry-run 미기록·check 통과를, 실패 사례는 결과 미기록·정본 불변을 함께 본다(expect.json 의 style 은 정본·기대 결과를 그 들여쓰기·줄 끝·끝 줄바꿈으로 바꿔 시험). PASS·FAIL 줄과 SUMMARY. 종료 0 = 전부 통과, 1 = 실패 있음, 2 = fixture 입력 오류
상태: 검증됨(2026-10-26 파동 1 F2 작성, 10/10 통과, merge 를 일부러 망가뜨린 사본 8개(충돌 무시·정렬 삽입 끔·서식 무시·--dry-run 무시·최상위 키 필수·정규화 끔·전체 정렬·Parent 미병합)를 모두 실패로 검출)
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FIXTURES_ROOT = Path(__file__).resolve().parent
TOOL_PATH = FIXTURES_ROOT.parent / "fluid_material_look.py"
DEFAULT_STYLE = {"indent": " ", "newline": "\n", "has_final_newline": True}
REPORT_LIST_KEYS = {"conflicts": "conflicts", "changes": "changes", "added_entries": "added_entries", "problems": "problems"}


class TDFixtureInputError(Exception):
    pass


def restyle_text(text, style):
    body = text[:-1] if text.endswith("\n") else text
    lines = []
    for line in body.split("\n"):
        content = line.lstrip(" ")
        lines.append(style["indent"] * (len(line) - len(content)) + content)
    restyled = style["newline"].join(lines)
    return restyled + style["newline"] if style["has_final_newline"] else restyled


def run_merge(work_dir, arguments):
    environment = dict(os.environ, PYTHONIOENCODING="utf-8")
    completed = subprocess.run([sys.executable, "-B", str(TOOL_PATH), *arguments], cwd=work_dir, capture_output=True, env=environment, check=False)
    stdout = completed.stdout.decode("utf-8", errors="replace")
    try:
        report = json.loads(stdout)
    except ValueError:
        report = None
    return completed.returncode, report, stdout


def report_paths(report, key):
    items = report.get(key, [])
    return sorted(item["at"] if isinstance(item, dict) else item for item in items)


def check_report_lists(expect, report, failures):
    for expect_key, report_key in REPORT_LIST_KEYS.items():
        if expect_key in expect and report_paths(report, report_key) != sorted(expect[expect_key]):
            failures.append(f"{report_key}: 기대 {sorted(expect[expect_key])} ≠ 실제 {report_paths(report, report_key)}")


def check_success_case(work_dir, expect, expected_bytes, canonical_bytes, failures):
    fragments = expect["fragments"]
    out_bytes = (work_dir / "out.json").read_bytes()
    if expected_bytes is not None and out_bytes != expected_bytes:
        failures.append("결과 바이트가 기대 결과와 다름")
    exit_code, report, _ = run_merge(work_dir, ["merge", "canonical.json", *fragments, "--out", "again.json"])
    if exit_code != 0 or (work_dir / "again.json").read_bytes() != out_bytes:
        failures.append("두 번째 실행 결과 바이트가 다름")
    exit_code, report, _ = run_merge(work_dir, ["merge", "out.json", *fragments, "--out", "rerun.json"])
    if exit_code != 0 or (work_dir / "rerun.json").read_bytes() != out_bytes:
        failures.append("결과를 정본으로 한 재병합 바이트가 다름")
    elif report["changed_count"] or report["added_leaf_count"] or report["added_entry_count"]:
        failures.append("결과를 정본으로 한 재병합에 변경·추가가 남음")
    shutil.copyfile(work_dir / "canonical.json", work_dir / "inplace.json")
    exit_code, report, _ = run_merge(work_dir, ["merge", "inplace.json", *fragments])
    if exit_code != 0 or (work_dir / "inplace.json").read_bytes() != out_bytes:
        failures.append("--out 없는 제자리 쓰기 결과가 다름")
    exit_code, report, _ = run_merge(work_dir, ["merge", "canonical.json", *fragments, "--dry-run", "--out", "dry.json"])
    if exit_code != 0 or (work_dir / "dry.json").exists() or report["output_sha256"] != hashlib.sha256(out_bytes).hexdigest():
        failures.append("--dry-run 이 결과를 썼거나 sha256 이 다름")
    if (work_dir / "canonical.json").read_bytes() != canonical_bytes:
        failures.append("--out 지정 실행이 정본을 바꿈")
    check = subprocess.run([sys.executable, "-B", str(TOOL_PATH), "check", "out.json"], cwd=work_dir, capture_output=True, check=False)
    if check.returncode != 0:
        failures.append("결과가 check 를 통과하지 못함")


def run_case(case_dir):
    expect_path = case_dir / "expect.json"
    if not expect_path.is_file():
        raise TDFixtureInputError(f"expect.json 없음: {case_dir}")
    expect = json.loads(expect_path.read_text(encoding="utf-8"))
    style = expect.get("style", DEFAULT_STYLE)
    failures = []
    with tempfile.TemporaryDirectory(prefix="look-merge-") as temporary:
        work_dir = Path(temporary)
        canonical_bytes = restyle_text((case_dir / "canonical.json").read_text(encoding="utf-8"), style).encode("utf-8")
        (work_dir / "canonical.json").write_bytes(canonical_bytes)
        for fragment in expect["fragments"]:
            shutil.copyfile(case_dir / fragment, work_dir / fragment)
        expected_bytes = None
        if "output" in expect:
            expected_bytes = restyle_text((case_dir / expect["output"]).read_text(encoding="utf-8"), style).encode("utf-8")
        exit_code, report, stdout = run_merge(work_dir, ["merge", "canonical.json", *expect["fragments"], "--out", "out.json"])
        if exit_code != expect["exit"]:
            failures.append(f"종료 코드 기대 {expect['exit']} ≠ 실제 {exit_code}: {stdout.strip()[:300]}")
        elif exit_code == 2:
            if (work_dir / "out.json").exists():
                failures.append("읽기 실패인데 결과를 씀")
        elif report is None:
            failures.append(f"보고 JSON 이 아님: {stdout.strip()[:300]}")
        else:
            check_report_lists(expect, report, failures)
            if exit_code == 0:
                check_success_case(work_dir, expect, expected_bytes, canonical_bytes, failures)
            elif (work_dir / "out.json").exists() or (work_dir / "canonical.json").read_bytes() != canonical_bytes:
                failures.append("거부 사례가 결과를 썼거나 정본을 바꿈")
    return failures


def main(argv=None):
    arguments = list(sys.argv[1:] if argv is None else argv)
    fixtures_root = Path(arguments[0]).resolve() if arguments else FIXTURES_ROOT
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    try:
        cases = sorted(path for path in fixtures_root.iterdir() if path.is_dir() and path.name != "__pycache__")
        if not cases:
            raise TDFixtureInputError(f"fixture 없음: {fixtures_root}")
        failed = 0
        for case_dir in cases:
            failures = run_case(case_dir)
            failed += bool(failures)
            print(("FAIL " if failures else "PASS ") + case_dir.name + ("" if not failures else ": " + " | ".join(failures)))
    except (TDFixtureInputError, OSError, ValueError, KeyError) as error:
        print(f"fixture 입력 오류: {error}")
        return 2
    print(f"SUMMARY fixtures={len(cases)} failed={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

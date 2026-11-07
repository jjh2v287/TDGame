"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), PYTHONIOENCODING=utf-8.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py
출력: 같은 폴더 fixtures_*.py(정렬, 고정 목록 없음 = 없는 모듈 허용)마다 run(임시 루트) → (통과, 실패, 줄), 줄을 찍고 마지막 줄 'fxp fixture 통과 n/m'. 종료 0 = 사례 1개 이상·실패 0, 1 = 그 밖. fixture 쪽 도우미(write_json·write_text·run_audit·FixtureLog)도 여기 둔다.
상태: 2026-11-07 FX-P X1a(B1) 구현. 임시 루트는 시스템 임시 폴더(끝나면 지움), 도구는 저장소 코드·데이터는 --root 사본.
"""
import importlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL_DIRECTORY = Path(__file__).resolve().parent
AUDIT_PATH = TOOL_DIRECTORY / "fxp_audit.py"
FIXTURE_PATTERN = "fixtures_*.py"
SUMMARY_PREFIX = "fxp fixture 통과"


def write_json(root, relative, value):
    target = Path(root) / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(value, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return target


def write_text(root, relative, text):
    target = Path(root) / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    return target


class AuditRun:
    def __init__(self, code, stdout, stderr, json_path, md_path):
        self.code = code
        self.stdout = stdout
        self.stderr = stderr
        self.json_bytes = json_path.read_bytes() if json_path.is_file() else None
        self.md_bytes = md_path.read_bytes() if md_path.is_file() else None
        self.document = json.loads(self.json_bytes.decode("utf-8")) if self.json_bytes else None
        self.markdown = self.md_bytes.decode("utf-8") if self.md_bytes else ""


def run_audit(root, subcommand, out_name, extra=(), audit_path=AUDIT_PATH):
    out_directory = Path(root) / "fixture-out"
    out_directory.mkdir(parents=True, exist_ok=True)
    json_path, md_path = out_directory / f"{out_name}.json", out_directory / f"{out_name}.md"
    for path in (json_path, md_path):
        if path.exists():
            path.unlink()
    command = [sys.executable, "-B", str(audit_path), subcommand, "--root", str(root), "--out", str(json_path), "--md", str(md_path), *extra]
    environment = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONDONTWRITEBYTECODE="1")
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", env=environment)
    return AuditRun(completed.returncode, completed.stdout, completed.stderr, json_path, md_path)


class FixtureLog:
    def __init__(self, module_name):
        self.module_name = module_name
        self.passed = 0
        self.failed = 0
        self.lines = []

    def check(self, case, condition, detail=""):
        if condition:
            self.passed += 1
            self.lines.append(f"통과 {self.module_name} {case}")
        else:
            self.failed += 1
            self.lines.append(f"실패 {self.module_name} {case}: {detail}")
        return condition

    def result(self):
        return self.passed, self.failed, self.lines


def fixture_module_names():
    return sorted(path.stem for path in TOOL_DIRECTORY.glob(FIXTURE_PATTERN))


def run_fixture_module(name):
    temporary = Path(tempfile.mkdtemp(prefix=f"fxp-{name}-"))
    try:
        module = importlib.import_module(name)
        return module.run(temporary)
    except Exception as error:
        return 0, 1, [f"실패 {name}: 실행 오류 {type(error).__name__}: {error}"]
    finally:
        shutil.rmtree(temporary, ignore_errors=True)


def main():
    sys.dont_write_bytecode = True
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    if str(TOOL_DIRECTORY) not in sys.path:
        sys.path.insert(0, str(TOOL_DIRECTORY))
    passed_total, failed_total = 0, 0
    for name in fixture_module_names():
        passed, failed, lines = run_fixture_module(name)
        for line in lines:
            print(line)
        passed_total += passed
        failed_total += failed
    total = passed_total + failed_total
    print(f"{SUMMARY_PREFIX} {passed_total}/{total}")
    return 0 if total and failed_total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

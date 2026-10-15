"""시스템 Python에서 실행: Cascadeur 사용자 settings.json에 Tools/Cascadeur/csc_scripts 경로와 td_events 이벤트 패키지를 등록해 내장 MCP 스크립트 서버(8765)가 Cascadeur 시작 때 자동으로 켜지게 한다(재실행 안전, 최초 1회 원본 백업).
실행: python Tools/Cascadeur/setup_cascadeur.py [--check]  (반영은 Cascadeur 재시작 뒤: python Tools/Cascadeur/cascadeur_bridge.py restart)  (PowerShell)
출력: %LOCALAPPDATA%/Nekki Limited/Cascadeur/settings.json(+ settings.json.td-backup), 표준 출력 JSON
상태: 현행
"""
import argparse
import json
import os
import shutil
import stat
from pathlib import Path

CSC_SCRIPTS = Path(__file__).resolve().parent / "csc_scripts"
BUILTIN_EVENTS_PACKAGE = "events"
PROJECT_EVENTS_PACKAGE = "td_events"


def settings_file():
    return Path(os.environ["LOCALAPPDATA"]) / "Nekki Limited" / "Cascadeur" / "settings.json"


def read_settings(path):
    if not path.is_file():
        return {"ScriptsDir": "", "Python": {"Path": [], "Commands": ["commands"], "Scripts": ["scripts"]}}
    return json.loads(path.read_text(encoding="utf-8"))


def configured_settings(settings):
    python_settings = settings.setdefault("Python", {})
    scripts_path = CSC_SCRIPTS.as_posix()
    search_paths = [entry for entry in python_settings.get("Path", []) if entry]
    if scripts_path not in search_paths:
        search_paths.append(scripts_path)
    python_settings["Path"] = search_paths
    events = [entry for entry in python_settings.get("Events", []) if entry]
    for package in (BUILTIN_EVENTS_PACKAGE, PROJECT_EVENTS_PACKAGE):
        if package not in events:
            events.append(package)
    python_settings["Events"] = events
    return settings


def is_configured(settings):
    python_settings = settings.get("Python", {})
    return (CSC_SCRIPTS.as_posix() in python_settings.get("Path", [])
            and BUILTIN_EVENTS_PACKAGE in python_settings.get("Events", [])
            and PROJECT_EVENTS_PACKAGE in python_settings.get("Events", []))


def write_settings(path, settings):
    path.parent.mkdir(parents=True, exist_ok=True)
    backup = path.with_name(path.name + ".td-backup")
    if path.is_file() and not backup.exists():
        shutil.copy2(path, backup)
    if path.is_file():
        os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
    path.write_text(json.dumps(settings, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    return backup


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    path = settings_file()
    settings = read_settings(path)
    report = {"settings_file": str(path), "csc_scripts": CSC_SCRIPTS.as_posix(), "configured_before": is_configured(settings)}
    if not arguments.check and not report["configured_before"]:
        report["backup"] = str(write_settings(path, configured_settings(settings)))
    report["configured_now"] = is_configured(read_settings(path))
    report["python_settings"] = read_settings(path).get("Python")
    print(json.dumps(report, ensure_ascii=False, indent=1))
    if not report["configured_now"] and not arguments.check:
        raise SystemExit(1)


if __name__ == "__main__":
    main()

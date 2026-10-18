r"""에디터 Python(파이썬 스크립트 커맨드렛 또는 원격 실행)에서 실행: 하드→소프트 UPROPERTY 변경 뒤 지정 폴더의 지정 클래스 에셋을 다시 저장해 옛 하드 임포트를 없앤다(OP-33).
실행: (PowerShell, L-repo-01, 에디터 DLL 빌드 후) & "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" --% "C:\Project\TDGame\TDGame.uproject" -run=pythonscript -script="C:/Project/TDGame/Tools/editor_resave_assets.py --path /Game/Combat/MegaMagic --class TDDamageDefinition" -unattended -nullrhi -nosplash -nosound
출력: Saved/AgentOps/resave_assets.json(에셋별 저장 여부와 저장 전 하드 의존 패키지), 표준 출력 요약 1줄. 같은 프로세스의 에셋 레지스트리는 저장 뒤에도 옛 의존을 보이므로, 저장 결과는 새 프로세스에서 --dry-run(저장 없이 하드 의존만 기록)으로 확인한다
상태: 현행
"""
import argparse
import json
import os
import shlex
import sys

import unreal


def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", action="append", required=True, help="콘텐츠 폴더(/Game/...), 여러 번 지정 가능")
    parser.add_argument("--class", dest="class_name", required=True, help="unreal 모듈의 클래스 이름(예: TDDamageDefinition)")
    parser.add_argument("--recursive", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--report", default=os.path.join(unreal.Paths.project_saved_dir(), "AgentOps", "resave_assets.json"))
    arguments = sys.argv[1:]
    if len(arguments) == 1 and " " in arguments[0]:
        arguments = shlex.split(arguments[0])
    return parser.parse_args(arguments)


def find_hard_dependencies(asset_registry, package_name):
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=False,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False)
    dependencies = asset_registry.get_dependencies(unreal.Name(package_name), options) or []
    return sorted(str(dependency) for dependency in dependencies if not str(dependency).startswith("/Script/"))


def list_package_names(folders, recursive):
    package_names = []
    for folder in folders:
        for object_path in unreal.EditorAssetLibrary.list_assets(folder, recursive=recursive, include_folder=False):
            package_name = object_path.split(".")[0]
            if package_name not in package_names:
                package_names.append(package_name)
    return package_names


def main():
    arguments = parse_arguments()
    asset_class = getattr(unreal, arguments.class_name, None)
    if asset_class is None:
        print(f"RESAVE ERROR class {arguments.class_name} is not exposed to Python")
        return 2

    asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
    asset_registry.scan_paths_synchronous(arguments.path, True)
    entries = []
    for package_name in list_package_names(arguments.path, arguments.recursive):
        asset = unreal.EditorAssetLibrary.load_asset(package_name)
        if not isinstance(asset, asset_class):
            continue
        entry = {"package": package_name, "hard_before": find_hard_dependencies(asset_registry, package_name), "saved": False}
        if not arguments.dry_run:
            entry["saved"] = bool(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False))
        entries.append(entry)

    os.makedirs(os.path.dirname(arguments.report), exist_ok=True)
    with open(arguments.report, "w", encoding="utf-8") as report_file:
        json.dump({"class": arguments.class_name, "paths": arguments.path, "dry_run": arguments.dry_run, "assets": entries}, report_file, ensure_ascii=False, indent=2)

    saved_count = sum(1 for entry in entries if entry["saved"])
    failed_count = 0 if arguments.dry_run else sum(1 for entry in entries if not entry["saved"])
    print(f"RESAVE SUMMARY assets={len(entries)} saved={saved_count} failed={failed_count} report={arguments.report}")
    return 1 if failed_count else 0


if main() != 0:
    raise RuntimeError("editor_resave_assets failed; see the RESAVE line and the report")

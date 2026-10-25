"""실행 환경: 시스템 Python 3(에디터 불필요, PowerShell 또는 Git Bash). 조절 정본 TDFluidPresetTuning.json 을 한 곳 규칙의 소유 위치(Kinds·Groups·StructDefaults)에서 고친 사본을 Saved 아래에 만들고, 묶음(Groups) 소유 필드를 한 종류만 바꿀 때는 split_group 일 때만 회원 종류로 펼친다(spec-wave0-data-bridge-v2 3.9).
실행 명령: python Tools/FluidFX/fluid_tuning_patch.py variant --out-dir Saved/FluidFX/calib/<run>/v<k> [--set <JSON 경로>=<JSON 값>]... [--set-split <JSON 경로>=<JSON 값>]... | write-canonical --backup-dir Saved/FluidFX/calib/<run>/canonical_backup --set ... | restore --backup-dir <폴더>. JSON 경로 = Kinds.<종류>.<구조체>.<필드> | Groups.<묶음>.<구조체>.<필드> | StructDefaults.<구조체>.<필드>
출력: variant 는 <out-dir>/TDFluidPresetTuning.json·TDFluidMaterialLook.json(모양은 정본 그대로)·variant.json(할당·소유 위치·영향 종류·sha256), write-canonical 은 정본을 고치기 전 <backup-dir> 에 원본과 backup.json 을 두고 검사 실패면 되돌린다. 모든 사본은 check_fluid_preset_data.py --tuning --look 종료 0 이어야 한다. 종료 코드 0 성공 / 1 검사 실패·소유 규칙 위반 / 2 입력 오류
상태: 오프라인 검사만(2026-10-25 정본 왕복 바이트 동일, Kinds 직접·묶음 펼침·묶음 소유 거부·StructDefaults·되쓰기 복원). 실제 PIE 스윕 미실행.
"""
import argparse
import copy
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
import check_fluid_preset_data  # noqa: E402

TUNING_FILE_NAME = "TDFluidPresetTuning.json"
LOOK_FILE_NAME = "TDFluidMaterialLook.json"
VARIANT_MANIFEST_NAME = "variant.json"
BACKUP_MANIFEST_NAME = "backup.json"
CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")
STRUCT_DEFAULTS_SECTION = "StructDefaults"
GROUPS_SECTION = "Groups"
KINDS_SECTION = "Kinds"
GROUP_MEMBERS_KEY = "Kinds"
CUSTOM_KIND = "Custom"
EXIT_CHECK_FAILED = 1
EXIT_INPUT = 2


class PatchError(Exception):
    pass


def canonical_tuning_path(root=ROOT):
    return Path(root).joinpath(*check_fluid_preset_data.TUNING_RELATIVE_PATH)


def canonical_look_path(root=ROOT):
    return Path(root).joinpath(*check_fluid_preset_data.LOOK_RELATIVE_PATH)


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dump_tuning_text(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def write_text_lf(path, text):
    Path(path).write_bytes(text.encode("utf-8"))


def parse_json_path(json_path):
    parts = json_path.split(".") if isinstance(json_path, str) else []
    if len(parts) == 3 and parts[0] == STRUCT_DEFAULTS_SECTION:
        return parts[0], None, parts[1], parts[2]
    if len(parts) == 4 and parts[0] in (KINDS_SECTION, GROUPS_SECTION):
        return parts[0], parts[1], parts[2], parts[3]
    raise PatchError(f"JSON 경로 {json_path!r}: Kinds.<종류>.<구조체>.<필드>, Groups.<묶음>.<구조체>.<필드>, StructDefaults.<구조체>.<필드> 중 하나여야 한다")


def check_field_exists(schema, struct_name, field_name, json_path):
    if struct_name not in schema.registry:
        raise PatchError(f"{json_path}: 등록되지 않은 구조체 {struct_name}")
    if field_name not in {field["name"] for field in schema.fields(struct_name)}:
        raise PatchError(f"{json_path}: 구조체 {struct_name} 에 필드 {field_name} 없음")


def struct_default_value(document, struct_name, field_name):
    return document[STRUCT_DEFAULTS_SECTION].get(struct_name, {}).get(field_name)


def kind_entries(document, kind_name):
    return document[KINDS_SECTION].get(kind_name, {})


def kind_owns_field(document, kind_name, struct_name, field_name):
    return field_name in kind_entries(document, kind_name).get(struct_name, {})


def find_owner_group(document, kind_name, struct_name, field_name):
    for group_name, group in document[GROUPS_SECTION].items():
        if kind_name in group.get(GROUP_MEMBERS_KEY, []) and field_name in group.get(struct_name, {}):
            return group_name
    return None


def kinds_following_struct_default(document, schema, struct_name, field_name):
    return [kind for kind in schema.kinds()
            if kind != CUSTOM_KIND
            and not kind_owns_field(document, kind, struct_name, field_name)
            and find_owner_group(document, kind, struct_name, field_name) is None]


def assign_struct_default(document, schema, struct_name, field_name, value, json_path):
    entries = document[STRUCT_DEFAULTS_SECTION].get(struct_name)
    if not isinstance(entries, dict) or field_name not in entries:
        raise PatchError(f"{json_path}: StructDefaults 에 이 필드가 없다")
    affected = kinds_following_struct_default(document, schema, struct_name, field_name)
    base_value = entries[field_name]
    entries[field_name] = value
    return {"owner": f"{STRUCT_DEFAULTS_SECTION}.{struct_name}", "base_value": base_value, "affected_kinds": affected, "split_from": None}


def assign_group(document, struct_name, field_name, value, group_name, json_path):
    group = document[GROUPS_SECTION].get(group_name)
    if not isinstance(group, dict):
        raise PatchError(f"{json_path}: 묶음 {group_name} 없음")
    members = list(group.get(GROUP_MEMBERS_KEY, []))
    claimers = [kind for kind in members if kind_owns_field(document, kind, struct_name, field_name)]
    if claimers:
        raise PatchError(f"{json_path}: 회원 종류 {', '.join(claimers)} 가 이미 이 필드를 정한다(한 곳 규칙)")
    entries = group.setdefault(struct_name, {})
    base_value = entries.get(field_name, struct_default_value(document, struct_name, field_name))
    entries[field_name] = value
    return {"owner": f"{GROUPS_SECTION}.{group_name}", "base_value": base_value, "affected_kinds": members, "split_from": None}


def split_group_field(document, group_name, struct_name, field_name):
    group = document[GROUPS_SECTION][group_name]
    group_value = group[struct_name].pop(field_name)
    if not group[struct_name]:
        del group[struct_name]
    for member in group.get(GROUP_MEMBERS_KEY, []):
        document[KINDS_SECTION].setdefault(member, {}).setdefault(struct_name, {})[field_name] = copy.deepcopy(group_value)
    return group_value


def assign_kind(document, schema, struct_name, field_name, value, kind_name, split_group, json_path):
    if kind_name not in schema.kinds() or kind_name == CUSTOM_KIND:
        raise PatchError(f"{json_path}: 알 수 없는 종류 {kind_name}(Custom 은 DA 사본을 쓰므로 표로 바꿀 수 없다)")
    split_from = None
    if kind_owns_field(document, kind_name, struct_name, field_name):
        base_value = kind_entries(document, kind_name)[struct_name][field_name]
    else:
        owner_group = find_owner_group(document, kind_name, struct_name, field_name)
        if owner_group is not None and not split_group:
            raise PatchError(f"{json_path}: Groups.{owner_group} 소유 필드다. split_group: true 로 회원 종류에 펼치거나 Groups.{owner_group} 경로를 쓴다")
        if owner_group is not None:
            base_value = split_group_field(document, owner_group, struct_name, field_name)
            split_from = owner_group
        else:
            base_value = struct_default_value(document, struct_name, field_name)
    document[KINDS_SECTION].setdefault(kind_name, {}).setdefault(struct_name, {})[field_name] = value
    return {"owner": f"{KINDS_SECTION}.{kind_name}", "base_value": base_value, "affected_kinds": [kind_name], "split_from": split_from}


def apply_assignment(document, schema, assignment):
    json_path = assignment["json_path"]
    section, name, struct_name, field_name = parse_json_path(json_path)
    check_field_exists(schema, struct_name, field_name, json_path)
    split_group = bool(assignment.get("split_group", False))
    if split_group and section != KINDS_SECTION:
        raise PatchError(f"{json_path}: split_group 은 Kinds 경로에서만 쓴다")
    value = copy.deepcopy(assignment["value"])
    if section == STRUCT_DEFAULTS_SECTION:
        info = assign_struct_default(document, schema, struct_name, field_name, value, json_path)
    elif section == GROUPS_SECTION:
        info = assign_group(document, struct_name, field_name, value, name, json_path)
    else:
        info = assign_kind(document, schema, struct_name, field_name, value, name, split_group, json_path)
    return dict(info, json_path=json_path, struct=struct_name, field=field_name, value=value, split_group=split_group)


def describe_assignment(document, schema, assignment):
    return apply_assignment(copy.deepcopy(document), schema, assignment)


def load_canonical_document(root=ROOT):
    return json.loads(canonical_tuning_path(root).read_text(encoding="utf-8"))


def patch_document(document, schema, assignments):
    patched = copy.deepcopy(document)
    applied = [apply_assignment(patched, schema, assignment) for assignment in assignments]
    return patched, applied


def run_checker(tuning_path, look_path, root=ROOT):
    command = [sys.executable, str(Path(root).joinpath(*CHECKER_RELATIVE_PATH)), "--root", str(root), "--tuning", str(tuning_path), "--look", str(look_path)]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return completed.returncode, (completed.stdout + completed.stderr).strip()


def require_under_saved(path, root=ROOT):
    resolved = Path(path).resolve()
    saved = (Path(root) / "Saved").resolve()
    if saved != resolved and saved not in resolved.parents:
        raise PatchError(f"{resolved}: 사본은 {saved} 아래에만 둔다(ReloadPresetTuningFromPaths 가 ProjectSavedDir 밖을 거부)")
    return resolved


def write_variant(assignments, out_dir, root=ROOT, schema=None):
    out_dir = require_under_saved(out_dir, root)
    schema = schema or check_fluid_preset_data.load_schema(root)
    tuning_source = canonical_tuning_path(root)
    look_source = canonical_look_path(root)
    canonical_bytes = tuning_source.read_bytes()
    document = json.loads(canonical_bytes.decode("utf-8"))
    patched, applied = patch_document(document, schema, assignments)
    out_dir.mkdir(parents=True, exist_ok=True)
    tuning_path = out_dir / TUNING_FILE_NAME
    look_path = out_dir / LOOK_FILE_NAME
    if applied:
        write_text_lf(tuning_path, dump_tuning_text(patched))
    else:
        tuning_path.write_bytes(canonical_bytes)
    shutil.copyfile(look_source, look_path)
    exit_code, checker_output = run_checker(tuning_path, look_path, root)
    if exit_code != 0:
        raise PatchError(f"사본 검사 실패(종료 {exit_code}): {checker_output[-1200:]}")
    manifest = {
        "assignments": applied,
        "tuning": tuning_path.as_posix(),
        "look": look_path.as_posix(),
        "tuning_sha256": file_sha256(tuning_path),
        "look_sha256": file_sha256(look_path),
        "canonical_tuning_sha256": file_sha256(tuning_source),
        "canonical_look_sha256": file_sha256(look_source),
        "checker": checker_output.splitlines()[-1] if checker_output else "",
    }
    write_text_lf(out_dir / VARIANT_MANIFEST_NAME, json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    return manifest


def write_canonical(assignments, backup_dir, root=ROOT, schema=None):
    if not assignments:
        raise PatchError("되쓰기할 --set 이 없다")
    backup_dir = require_under_saved(backup_dir, root)
    schema = schema or check_fluid_preset_data.load_schema(root)
    tuning_path = canonical_tuning_path(root)
    document = json.loads(tuning_path.read_text(encoding="utf-8"))
    patched, applied = patch_document(document, schema, assignments)
    backup_dir.mkdir(parents=True, exist_ok=True)
    backup_path = backup_dir / TUNING_FILE_NAME
    if backup_path.exists():
        raise PatchError(f"{backup_path}: 백업이 이미 있다. 먼저 restore 하거나 다른 --backup-dir 을 쓴다")
    shutil.copyfile(tuning_path, backup_path)
    backup = {"canonical": tuning_path.as_posix(), "sha256": file_sha256(backup_path), "assignments": applied}
    write_text_lf(backup_dir / BACKUP_MANIFEST_NAME, json.dumps(backup, ensure_ascii=False, indent=2) + "\n")
    write_text_lf(tuning_path, dump_tuning_text(patched))
    exit_code, checker_output = run_checker(tuning_path, canonical_look_path(root), root)
    if exit_code != 0:
        shutil.copyfile(backup_path, tuning_path)
        raise PatchError(f"정본 검사 실패(종료 {exit_code}), 원본으로 되돌림: {checker_output[-1200:]}")
    return dict(backup, written_sha256=file_sha256(tuning_path), checker=checker_output.splitlines()[-1] if checker_output else "")


def restore_canonical(backup_dir, root=ROOT):
    backup_dir = Path(backup_dir).resolve()
    backup = json.loads((backup_dir / BACKUP_MANIFEST_NAME).read_text(encoding="utf-8"))
    backup_path = backup_dir / TUNING_FILE_NAME
    if file_sha256(backup_path) != backup["sha256"]:
        raise PatchError(f"{backup_path}: 백업 sha256 이 backup.json 과 다르다")
    tuning_path = canonical_tuning_path(root)
    shutil.copyfile(backup_path, tuning_path)
    return {"canonical": tuning_path.as_posix(), "sha256": file_sha256(tuning_path)}


def parse_assignment(text, split_group):
    json_path, separator, value_text = text.partition("=")
    if not separator:
        raise PatchError(f"--set {text!r}: <JSON 경로>=<JSON 값> 꼴이어야 한다")
    try:
        value = json.loads(value_text)
    except ValueError as error:
        raise PatchError(f"--set {text!r}: 값이 JSON 이 아니다({error})") from error
    return {"json_path": json_path.strip(), "value": value, "split_group": split_group}


def collect_assignments(arguments):
    return [parse_assignment(text, False) for text in arguments.set] + [parse_assignment(text, True) for text in arguments.set_split]


def parse_arguments():
    parser = argparse.ArgumentParser(description="FluidFX 조절 JSON 사본·되쓰기(spec-wave0-data-bridge-v2 3.9 2·7단계)")
    parser.add_argument("--root", default=str(ROOT))
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("variant", "write-canonical"):
        command = commands.add_parser(name)
        command.add_argument("--set", action="append", default=[], help="<JSON 경로>=<JSON 값>, 묶음 소유 필드면 거부")
        command.add_argument("--set-split", action="append", default=[], help="<Kinds 경로>=<JSON 값>, 묶음 소유 필드를 회원 종류로 펼친 뒤 이 종류만 바꾼다")
    commands.choices["variant"].add_argument("--out-dir", required=True, type=Path)
    commands.choices["write-canonical"].add_argument("--backup-dir", required=True, type=Path)
    restore = commands.add_parser("restore")
    restore.add_argument("--backup-dir", required=True, type=Path)
    return parser.parse_args()


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    arguments = parse_arguments()
    root = Path(arguments.root).resolve()
    try:
        if arguments.command == "restore":
            print(json.dumps(restore_canonical(arguments.backup_dir, root), ensure_ascii=False))
            return 0
        assignments = collect_assignments(arguments)
        if arguments.command == "variant":
            print(json.dumps(write_variant(assignments, root / arguments.out_dir if not arguments.out_dir.is_absolute() else arguments.out_dir, root), ensure_ascii=False, indent=2))
            return 0
        backup_dir = root / arguments.backup_dir if not arguments.backup_dir.is_absolute() else arguments.backup_dir
        print(json.dumps(write_canonical(assignments, backup_dir, root), ensure_ascii=False, indent=2))
        print("다음: PowerShell 에서 python Tools/run_in_editor.py Tools/FluidFX/editor_dump_fluid_preset_builders.py 로 빌더 덤프를 기준선과 비교하고, python Tools/check_fluid_preset_data.py --stage-report <무대 보고서> 가 STALE 0 인지 본다. 커밋은 별도 허가")
        return 0
    except check_fluid_preset_data.InputError as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    except (OSError, ValueError) as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    except PatchError as error:
        print(f"거부: {error}", file=sys.stderr)
        return EXIT_CHECK_FAILED


if __name__ == "__main__":
    sys.exit(main())

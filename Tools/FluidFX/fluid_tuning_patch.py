"""실행 환경: 시스템 Python 3(에디터 불필요, PowerShell 또는 Git Bash). 조절 정본 TDFluidPresetTuning.json 과 모양 정본 TDFluidMaterialLook.json 을 한 곳 규칙의 소유 위치(Kinds·Groups·StructDefaults·Globals, 모양은 Look:<구획>.<패키지>.<값 구역>.<이름>)에서 고친 사본을 Saved 아래에 만들고, 묶음(Groups) 소유 필드를 한 종류만 바꿀 때는 split_group 일 때만 회원 종류로 펼친다(spec-wave0-data-bridge-v2 3.9). 채택값은 덧씌움 overlay.json v2(spec-cal0-v2 C7)에 누적한다.
실행 명령: python Tools/FluidFX/fluid_tuning_patch.py [--root <루트>] variant --out-dir Saved/FluidFX/calib/<run>/v<k> [--set <JSON 경로>=<JSON 값>]... [--set-split <Kinds 경로>=<JSON 값>]... | write-canonical --backup-dir <Saved 아래 폴더> [--set ...] [--from-overlay <overlay.json>] [--acknowledge-header] | restore --backup-dir <폴더> | accept --judge <confirm judge.json> [--overlay <overlay.json>] | overlay-remove --json-path <경로> --reason <사유> [--overlay ...] | overlay-replace --json-path <경로> --judge <judge.json> [--overlay ...] | materialize --out <Saved 아래 폴더> [--overlay ...]. JSON 경로 = Kinds.<종류>.<구조체>.<필드> | Groups.<묶음>.<구조체>.<필드> | StructDefaults.<구조체>.<필드> | Globals.<구조체>.<필드> | Look:Materials.<패키지>.<Scalars|Vectors>.<이름> | Look:NiagaraSystems.<패키지>.<Floats|Ints|Bools|Vectors2|Vectors3|Colors>.<이름>(정본 모양에 없는 키는 할당에 base 필수, Switches 거부). 덧씌움 기본 경로 Saved/FluidFX/calib/accepted/overlay.json
출력: variant·materialize 는 <out>/TDFluidPresetTuning.json·TDFluidMaterialLook.json(바꾸지 않은 쪽은 정본 바이트 그대로)·variant.json(할당·scope kind|global|look·소유 위치·기준 값·sha256, materialize 는 overlay·overlay_sha256 추가). write-canonical 은 두 정본을 <backup-dir> 에 백업(backup.json)한 뒤 쓰고 검사 실패면 둘 다 되돌린다. StructDefaults·Globals 경로는 --acknowledge-header 없으면 거부, StructDefaults.TDFluidSimulationConfig 는 늘 거부. accept·overlay-* 는 덧씌움을 원자적으로 다시 쓰고 history 에 add|remove|replace 를 남긴다. 사본·정본은 check_fluid_preset_data.py --tuning --look 과 fluid_material_look 형식 검사가 0 이어야 한다. 종료 코드 0 성공 / 1 검사 실패·소유 규칙 위반·adopt 거짓 / 2 입력 오류(경로 문법·정본에 없는 키·옛 형식 판정·잠금 재검증 실패·덧씌움 sha 불일치)
상태: 오프라인 검사만(2026-10-25 정본 왕복·묶음 펼침·되쓰기 복원, 2026-10-30 CAL-0 B1 하네스 check_cal0_patch: Look·Globals 사본, 임시 루트 write-canonical·restore, accept·overlay-remove·overlay-replace·materialize, effective_value). effective_value 해석 순서 Globals 한 층, 아니면 Kinds → Groups → StructDefaults 는 C++ TDFluidPresetTuningSubsystem.cpp 에서 확인(StructDefaults :773, Groups :788, Kinds :806 순으로 층을 쌓고 :912-915 가 차례로 덮어씀, Globals :901). 정본 조절 JSON 은 들여쓰기가 고르지 않아(Kinds 한 구간) 되쓰기 때 그 구간 들여쓰기도 정규화된다. 실제 PIE 스윕·정본 되쓰기 미실행.
"""
import argparse
import copy
import hashlib
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools"))
sys.path.insert(0, str(ROOT / "Tools" / "FluidFX"))
import check_fluid_preset_data  # noqa: E402
import fluid_material_look  # noqa: E402
import prereg_lock  # noqa: E402

TUNING_FILE_NAME = "TDFluidPresetTuning.json"
LOOK_FILE_NAME = "TDFluidMaterialLook.json"
VARIANT_MANIFEST_NAME = "variant.json"
BACKUP_MANIFEST_NAME = "backup.json"
CHECKER_RELATIVE_PATH = ("Tools", "check_fluid_preset_data.py")
STRUCT_DEFAULTS_SECTION = "StructDefaults"
GROUPS_SECTION = "Groups"
KINDS_SECTION = "Kinds"
GLOBALS_SECTION = "Globals"
GROUP_MEMBERS_KEY = "Kinds"
CUSTOM_KIND = "Custom"
LOOK_PREFIX = "Look:"
LOOK_SECTIONS = {"Materials": ("Scalars", "Vectors"), "NiagaraSystems": ("Floats", "Ints", "Bools", "Vectors2", "Vectors3", "Colors")}
STATIC_SWITCH_SECTION = "Switches"
HEADER_DEFAULT_SECTIONS = (STRUCT_DEFAULTS_SECTION, GLOBALS_SECTION)
GOLDEN_SNAPSHOT_STRUCT = "TDFluidSimulationConfig"
KIND_SCOPE = "kind"
GLOBAL_SCOPE = "global"
LOOK_SCOPE = "look"
DEFAULT_OVERLAY_RELATIVE_PATH = ("Saved", "FluidFX", "calib", "accepted", "overlay.json")
OVERLAY_VERSION = 2
OVERLAY_LIST_KEYS = ("assignments", "actor_props", "history")
JUDGE_VERSION = 2
KNOB_ENTRY = "knob"
BASE_TUNING_ENTRY = "base_tuning"
EXIT_CHECK_FAILED = 1
EXIT_INPUT = 2


class PatchError(Exception):
    pass


class InputRejected(PatchError):
    pass


def canonical_tuning_path(root=ROOT):
    return Path(root).joinpath(*check_fluid_preset_data.TUNING_RELATIVE_PATH)


def canonical_look_path(root=ROOT):
    return Path(root).joinpath(*check_fluid_preset_data.LOOK_RELATIVE_PATH)


def default_overlay_path(root=ROOT):
    return Path(root).joinpath(*DEFAULT_OVERLAY_RELATIVE_PATH)


def file_sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def dump_tuning_text(document):
    return json.dumps(document, indent=2, ensure_ascii=False) + "\n"


def write_text_lf(path, text):
    Path(path).write_bytes(text.encode("utf-8"))


def is_look_path(json_path):
    return isinstance(json_path, str) and json_path.startswith(LOOK_PREFIX)


def parse_look_path(json_path):
    parts = json_path[len(LOOK_PREFIX):].split(".", 3)
    if len(parts) != 4 or not parts[3]:
        raise InputRejected(f"{json_path}: Look:<Materials|NiagaraSystems>.<패키지>.<값 구역>.<이름> 이어야 한다")
    table, package, section, name = parts
    if table not in LOOK_SECTIONS:
        raise InputRejected(f"{json_path}: Look 구획 {table!r} 는 {', '.join(LOOK_SECTIONS)} 중 하나여야 한다")
    if not fluid_material_look.is_package_path(package):
        raise InputRejected(f"{json_path}: 패키지 {package!r} 는 /Game/ 로 시작하고 '.' 이 없는 패키지 경로여야 한다")
    if section == STATIC_SWITCH_SECTION:
        raise InputRejected(f"{json_path}: Switches 는 정적 스위치라 실행 중 바꿀 수 없어 거부한다")
    if section not in LOOK_SECTIONS[table]:
        raise InputRejected(f"{json_path}: 값 구역 {section!r} 는 {table} 에서 허용되지 않는다(허용 {', '.join(LOOK_SECTIONS[table])}, 패키지 경로에는 '.' 을 쓸 수 없다)")
    return LOOK_PREFIX + table, package, section, name


def parse_json_path(json_path):
    if is_look_path(json_path):
        return parse_look_path(json_path)
    parts = json_path.split(".") if isinstance(json_path, str) else []
    if len(parts) == 3 and parts[0] in (STRUCT_DEFAULTS_SECTION, GLOBALS_SECTION):
        return parts[0], None, parts[1], parts[2]
    if len(parts) == 4 and parts[0] in (KINDS_SECTION, GROUPS_SECTION):
        return parts[0], parts[1], parts[2], parts[3]
    raise InputRejected(f"JSON 경로 {json_path!r}: Kinds.<종류>.<구조체>.<필드>, Groups.<묶음>.<구조체>.<필드>, StructDefaults.<구조체>.<필드>, Globals.<구조체>.<필드>, Look:<구획>.<패키지>.<값 구역>.<이름> 중 하나여야 한다")


def reject_golden_snapshot_path(json_path):
    section, _, struct_name, _ = parse_json_path(json_path)
    if section == STRUCT_DEFAULTS_SECTION and struct_name == GOLDEN_SNAPSHOT_STRUCT:
        raise InputRejected(f"{json_path}: StructDefaults.{GOLDEN_SNAPSHOT_STRUCT} 는 골든 스냅숏 입력(헤더 기본값과의 차이)을 바꾸므로 거부한다(spec-cal0-v2 C2)")


def check_field_exists(schema, struct_name, field_name, json_path):
    if struct_name not in schema.registry:
        raise InputRejected(f"{json_path}: 등록되지 않은 구조체 {struct_name}")
    if field_name not in {field["name"] for field in schema.fields(struct_name)}:
        raise InputRejected(f"{json_path}: 구조체 {struct_name} 에 필드 {field_name} 없음")


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


def effective_value(document, kind_name, struct_name, field_name):
    global_entries = document.get(GLOBALS_SECTION, {}).get(struct_name)
    if isinstance(global_entries, dict) and field_name in global_entries:
        return global_entries[field_name]
    if kind_owns_field(document, kind_name, struct_name, field_name):
        return kind_entries(document, kind_name)[struct_name][field_name]
    owner_group = find_owner_group(document, kind_name, struct_name, field_name)
    if owner_group is not None:
        return document[GROUPS_SECTION][owner_group][struct_name][field_name]
    defaults = document[STRUCT_DEFAULTS_SECTION].get(struct_name, {})
    if field_name not in defaults:
        raise InputRejected(f"{kind_name}.{struct_name}.{field_name}: Kinds·Groups·StructDefaults·Globals 어디에도 값이 없다")
    return defaults[field_name]


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


def assign_global(document, schema, struct_name, field_name, value, json_path):
    if schema.registry.get(struct_name) != check_fluid_preset_data.GLOBAL_SCOPE:
        raise InputRejected(f"{json_path}: {struct_name} 은 Globals 등록 구조체가 아니다")
    entries = document[GLOBALS_SECTION].get(struct_name)
    if not isinstance(entries, dict) or field_name not in entries:
        raise InputRejected(f"{json_path}: 정본 Globals 에 이 필드가 없다")
    base_value = entries[field_name]
    entries[field_name] = value
    return {"owner": f"{GLOBALS_SECTION}.{struct_name}", "base_value": base_value, "affected_kinds": [], "split_from": None}


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
    json_path = assignment.get("json_path")
    section, name, struct_name, field_name = parse_json_path(json_path)
    if section.startswith(LOOK_PREFIX):
        raise InputRejected(f"{json_path}: Look 경로는 모양 문서와 함께 patch_documents·apply_look_assignment 로 적용한다")
    check_field_exists(schema, struct_name, field_name, json_path)
    split_group = bool(assignment.get("split_group", False))
    if split_group and section != KINDS_SECTION:
        raise PatchError(f"{json_path}: split_group 은 Kinds 경로에서만 쓴다")
    value = copy.deepcopy(assignment["value"])
    if section == STRUCT_DEFAULTS_SECTION:
        info = assign_struct_default(document, schema, struct_name, field_name, value, json_path)
    elif section == GLOBALS_SECTION:
        info = assign_global(document, schema, struct_name, field_name, value, json_path)
    elif section == GROUPS_SECTION:
        info = assign_group(document, struct_name, field_name, value, name, json_path)
    else:
        info = assign_kind(document, schema, struct_name, field_name, value, name, split_group, json_path)
    scope = GLOBAL_SCOPE if section == GLOBALS_SECTION else KIND_SCOPE
    return dict(info, json_path=json_path, scope=scope, struct=struct_name, field=field_name, value=value, split_group=split_group)


def apply_look_assignment(look, assignment):
    json_path = assignment.get("json_path")
    table_key, package, section, name = parse_json_path(json_path)
    if not table_key.startswith(LOOK_PREFIX):
        raise InputRejected(f"{json_path}: Look 경로가 아니다")
    if assignment.get("split_group"):
        raise InputRejected(f"{json_path}: split_group 은 Kinds 경로에서만 쓴다")
    table = table_key[len(LOOK_PREFIX):]
    entry = look[table].get(package)
    if not isinstance(entry, dict):
        raise InputRejected(f"{json_path}: 모양 정본 {table} 에 패키지 {package} 가 없다")
    value = copy.deepcopy(assignment["value"])
    base = assignment.get("base")
    owner = f"{table}.{package}"
    problems = fluid_material_look.find_section_problems(owner, section, {name: value})
    if base is not None:
        problems += fluid_material_look.find_section_problems(owner + "(base)", section, {name: base})
    if problems:
        raise InputRejected(f"{json_path}: " + "; ".join(problems))
    values = entry.setdefault(section, {})
    has_key = name in values
    if not has_key and base is None:
        raise InputRejected(f"{json_path}: 모양 정본에 키가 없다. 부모 기본값을 base 로 준다")
    if has_key and base is not None and fluid_material_look.normalize_section_value(section, base) != fluid_material_look.normalize_section_value(section, values[name]):
        raise InputRejected(f"{json_path}: base {base!r} 가 정본 값 {values[name]!r} 과 다르다")
    base_value = copy.deepcopy(values[name]) if has_key else copy.deepcopy(base)
    values[name] = value
    return {"json_path": json_path, "scope": LOOK_SCOPE, "owner": f"{LOOK_PREFIX}{owner}", "table": table, "package": package, "section": section,
            "struct": None, "field": name, "value": value, "base_value": base_value, "added_key": not has_key, "affected_kinds": [], "split_from": None, "split_group": False}


def patch_documents(tuning, look, schema, assignments):
    patched_tuning = copy.deepcopy(tuning)
    patched_look = copy.deepcopy(look)
    applied = []
    for assignment in assignments:
        if not isinstance(assignment, dict) or "json_path" not in assignment:
            raise InputRejected(f"할당 {assignment!r}: json_path 가 있는 객체여야 한다(actor_prop 은 JSON 사본에 들어가지 않는다)")
        if is_look_path(assignment["json_path"]):
            applied.append(apply_look_assignment(patched_look, assignment))
        else:
            applied.append(apply_assignment(patched_tuning, schema, assignment))
    return patched_tuning, patched_look, applied


def describe_assignment(document, schema, assignment, look=None):
    if is_look_path(assignment.get("json_path")):
        return apply_look_assignment(copy.deepcopy(look if look is not None else load_canonical_look()), assignment)
    return apply_assignment(copy.deepcopy(document), schema, assignment)


def load_canonical_document(root=ROOT):
    return json.loads(canonical_tuning_path(root).read_text(encoding="utf-8"))


def load_canonical_look(root=ROOT):
    return json.loads(canonical_look_path(root).read_text(encoding="utf-8"))


def patch_document(document, schema, assignments):
    patched = copy.deepcopy(document)
    applied = [apply_assignment(patched, schema, assignment) for assignment in assignments]
    return patched, applied


def run_checker(tuning_path, look_path, root=ROOT):
    command = [sys.executable, str(Path(root).joinpath(*CHECKER_RELATIVE_PATH)), "--root", str(root), "--tuning", str(tuning_path), "--look", str(look_path)]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return completed.returncode, (completed.stdout + completed.stderr).strip()


def check_copies(tuning_path, look_path, root=ROOT):
    exit_code, checker_output = run_checker(tuning_path, look_path, root)
    look_problems = fluid_material_look.find_look_problems(json.loads(Path(look_path).read_text(encoding="utf-8")))
    if exit_code != 0 or look_problems:
        raise PatchError(f"검사 실패(check_fluid_preset_data 종료 {exit_code}, 모양 형식 문제 {len(look_problems)}): {checker_output[-1200:]} {'; '.join(look_problems[:10])}")
    return checker_output.splitlines()[-1] if checker_output else ""


def require_under_saved(path, root=ROOT):
    resolved = Path(path).resolve()
    saved = (Path(root) / "Saved").resolve()
    if saved != resolved and saved not in resolved.parents:
        raise PatchError(f"{resolved}: 사본은 {saved} 아래에만 둔다(ReloadPresetTuningFromPaths 가 ProjectSavedDir 밖을 거부)")
    return resolved


def has_scope(applied, is_look):
    return any((item["scope"] == LOOK_SCOPE) == is_look for item in applied)


def write_variant(assignments, out_dir, root=ROOT, schema=None, manifest_extra=None):
    out_dir = require_under_saved(out_dir, root)
    schema = schema or check_fluid_preset_data.load_schema(root)
    tuning_source = canonical_tuning_path(root)
    look_source = canonical_look_path(root)
    tuning_bytes = tuning_source.read_bytes()
    look_bytes = look_source.read_bytes()
    patched_tuning, patched_look, applied = patch_documents(json.loads(tuning_bytes.decode("utf-8")), json.loads(look_bytes.decode("utf-8")), schema, assignments)
    out_dir.mkdir(parents=True, exist_ok=True)
    tuning_path = out_dir / TUNING_FILE_NAME
    look_path = out_dir / LOOK_FILE_NAME
    if has_scope(applied, False):
        write_text_lf(tuning_path, dump_tuning_text(patched_tuning))
    else:
        tuning_path.write_bytes(tuning_bytes)
    if has_scope(applied, True):
        write_text_lf(look_path, fluid_material_look.format_look_text(patched_look))
    else:
        look_path.write_bytes(look_bytes)
    checker = check_copies(tuning_path, look_path, root)
    manifest = {
        "assignments": applied,
        "tuning": tuning_path.as_posix(),
        "look": look_path.as_posix(),
        "tuning_sha256": file_sha256(tuning_path),
        "look_sha256": file_sha256(look_path),
        "canonical_tuning_sha256": file_sha256(tuning_source),
        "canonical_look_sha256": file_sha256(look_source),
        "checker": checker,
    }
    manifest.update(manifest_extra or {})
    write_text_lf(out_dir / VARIANT_MANIFEST_NAME, json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    return manifest


def check_canonical_writable(assignments, acknowledge_header):
    header_paths = []
    for assignment in assignments:
        json_path = assignment.get("json_path") if isinstance(assignment, dict) else None
        reject_golden_snapshot_path(json_path)
        if parse_json_path(json_path)[0] in HEADER_DEFAULT_SECTIONS:
            header_paths.append(json_path)
    if header_paths and not acknowledge_header:
        raise InputRejected(f"{', '.join(header_paths)}: 헤더 기본값 동시 변경 필요(PresetData.StructDefaultsMatchHeader). 같은 커밋에서 헤더도 바꿀 때만 --acknowledge-header 로 쓴다")
    return header_paths


def write_canonical(assignments, backup_dir, root=ROOT, schema=None, acknowledge_header=False):
    if not assignments:
        raise InputRejected("되쓰기할 --set·--from-overlay 항목이 없다")
    header_paths = check_canonical_writable(assignments, acknowledge_header)
    backup_dir = require_under_saved(backup_dir, root)
    schema = schema or check_fluid_preset_data.load_schema(root)
    tuning_path = canonical_tuning_path(root)
    look_path = canonical_look_path(root)
    tuning_bytes = tuning_path.read_bytes()
    look_bytes = look_path.read_bytes()
    patched_tuning, patched_look, applied = patch_documents(json.loads(tuning_bytes.decode("utf-8")), json.loads(look_bytes.decode("utf-8")), schema, assignments)
    backup_dir.mkdir(parents=True, exist_ok=True)
    backup_tuning = backup_dir / TUNING_FILE_NAME
    backup_look = backup_dir / LOOK_FILE_NAME
    if backup_tuning.exists() or backup_look.exists():
        raise PatchError(f"{backup_dir}: 백업이 이미 있다. 먼저 restore 하거나 다른 --backup-dir 을 쓴다")
    backup_tuning.write_bytes(tuning_bytes)
    backup_look.write_bytes(look_bytes)
    backup = {"canonical": tuning_path.as_posix(), "sha256": file_sha256(backup_tuning), "look_canonical": look_path.as_posix(), "look_sha256": file_sha256(backup_look),
              "assignments": applied, "header_fields": header_paths}
    write_text_lf(backup_dir / BACKUP_MANIFEST_NAME, json.dumps(backup, ensure_ascii=False, indent=2) + "\n")
    if has_scope(applied, False):
        write_text_lf(tuning_path, dump_tuning_text(patched_tuning))
    if has_scope(applied, True):
        write_text_lf(look_path, fluid_material_look.format_look_text(patched_look))
    try:
        checker = check_copies(tuning_path, look_path, root)
    except PatchError as error:
        tuning_path.write_bytes(tuning_bytes)
        look_path.write_bytes(look_bytes)
        raise PatchError(f"정본 {error}, 원본으로 되돌림") from error
    return dict(backup, written_sha256=file_sha256(tuning_path), written_look_sha256=file_sha256(look_path), checker=checker)


def restore_canonical(backup_dir, root=ROOT):
    backup_dir = Path(backup_dir).resolve()
    backup = json.loads((backup_dir / BACKUP_MANIFEST_NAME).read_text(encoding="utf-8"))
    pairs = [(backup_dir / TUNING_FILE_NAME, backup["sha256"], canonical_tuning_path(root))]
    if "look_sha256" in backup:
        pairs.append((backup_dir / LOOK_FILE_NAME, backup["look_sha256"], canonical_look_path(root)))
    for backup_path, expected, _ in pairs:
        if file_sha256(backup_path) != expected:
            raise PatchError(f"{backup_path}: 백업 sha256 이 backup.json 과 다르다")
    for backup_path, _, canonical_path in pairs:
        shutil.copyfile(backup_path, canonical_path)
    return {"restored": [{"canonical": canonical_path.as_posix(), "sha256": file_sha256(canonical_path)} for _, _, canonical_path in pairs]}


def empty_overlay():
    return {"version": OVERLAY_VERSION, "assignments": [], "actor_props": [], "history": []}


def read_overlay(path, is_missing_allowed=False):
    path = Path(path)
    if not path.is_file():
        if is_missing_allowed:
            return empty_overlay()
        raise InputRejected(f"{path}: 덧씌움 파일이 없다")
    overlay = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(overlay, dict) or overlay.get("version") != OVERLAY_VERSION or any(not isinstance(overlay.get(key), list) for key in OVERLAY_LIST_KEYS):
        raise InputRejected(f"{path}: 덧씌움 v2 형식이 아니다(version 2, {'·'.join(OVERLAY_LIST_KEYS)} 목록)")
    return overlay


def overlay_sha256(path):
    return file_sha256(path) if Path(path).is_file() else None


def write_overlay(path, overlay):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    write_text_lf(temporary, json.dumps(overlay, ensure_ascii=False, indent=2) + "\n")
    os.replace(temporary, path)


def read_adopting_judge(judge_path):
    judge = json.loads(Path(judge_path).read_text(encoding="utf-8"))
    adopt = judge.get("adopt") if isinstance(judge, dict) else None
    if not isinstance(adopt, dict) or judge.get("version") != JUDGE_VERSION:
        raise InputRejected(f"{judge_path}: adopt 가 있는 v2 판정만 받는다(옛 형식 accepted 는 채택 근거가 아니다)")
    if adopt.get("decision") is not True:
        raise PatchError(f"{judge_path}: adopt.decision 거짓, 채택하지 않는다: {'; '.join(str(reason) for reason in adopt.get('reasons') or [])}")
    return judge, adopt


def judge_field(judge, name):
    if name in judge:
        return judge[name]
    evidence = judge["adopt"].get("evidence")
    return evidence.get(name) if isinstance(evidence, dict) else None


def verify_judge_lineage(judge, judge_path):
    lock_text, lock_sha, plan_text = (judge_field(judge, name) for name in ("lock", "lock_sha256", "plan"))
    if not lock_text or not lock_sha or not plan_text:
        raise InputRejected(f"{judge_path}: 판정에 lock·lock_sha256·plan 이 있어야 한다")
    lock_path = prereg_lock.project_path(lock_text)
    is_matching, problems, lock = prereg_lock.verify_lock(lock_path)
    if not is_matching:
        raise InputRejected(f"{judge_path}: 잠금 재검증 실패: {'; '.join(problems)}")
    if file_sha256(lock_path) != lock_sha:
        raise InputRejected(f"{judge_path}: 잠금 파일 sha256 이 판정의 lock_sha256 과 다르다")
    plan_path = prereg_lock.project_path(plan_text)
    if prereg_lock.relative_text(plan_path) not in lock["files"]:
        raise InputRejected(f"{judge_path}: 잠금에 계획 {plan_text} 이 없다")
    plan_sha = judge_field(judge, "plan_sha256")
    if plan_sha is not None and file_sha256(plan_path) != plan_sha:
        raise InputRejected(f"{judge_path}: 계획 sha256 이 판정과 다르다")
    return plan_path


def plan_levels(plan, scene_names):
    scenes = plan.get("scenes") or []
    chosen = [scene for scene in scenes if not scene_names or scene.get("name") in scene_names]
    return list(dict.fromkeys(scene.get("level") for scene in chosen if scene.get("level")))


def describe_actor_prop(actor_prop):
    target = ".".join(part for part in (actor_prop.get("label"), actor_prop.get("component"), actor_prop.get("property")) if part)
    return f"actor_prop:{actor_prop.get('level')}/{target}"


def adopted_entries(judge_path, judge, adopt, plan):
    source = {"plan": judge_field(judge, "plan"), "plan_sha256": judge_field(judge, "plan_sha256"), "screen_judge": judge_field(judge, "screen_result"),
              "confirm_judge": prereg_lock.relative_text(judge_path), "judge_sha256": file_sha256(judge_path), "variant": adopt.get("variant"),
              "lock_sha256": judge_field(judge, "lock_sha256")}
    knobs = []
    actor_props = []
    for item in adopt.get("assignments") or []:
        if isinstance(item.get("actor_prop"), dict):
            target = {key: item["actor_prop"][key] for key in ("label", "property", "component") if key in item["actor_prop"]}
            levels = [item["level"]] if item.get("level") else plan_levels(plan, item.get("scenes"))
            if not levels:
                raise InputRejected(f"{judge_path}: actor_prop 채택 {target} 의 레벨을 계획 장면에서 찾지 못했다")
            actor_props.extend(dict(target, level=level, value=item.get("value"), **{"from": source}) for level in levels)
            continue
        paths = item.get("json_paths") or [item.get("json_path")]
        for json_path in paths:
            entry = {"json_path": json_path, "value": item.get("value"), "split_group": bool(item.get("split_group", False)), "kind": KNOB_ENTRY, "from": source}
            if "base" in item:
                entry["base"] = item["base"]
            knobs.append(entry)
    if not knobs and not actor_props:
        raise InputRejected(f"{judge_path}: adopt.assignments 가 비었다")
    return knobs, actor_props, source


def base_tuning_entries(plan, source):
    base_tuning = plan.get("base_tuning") or []
    if not base_tuning:
        return [], []
    ships = plan.get("base_tuning_ships")
    if not isinstance(ships, bool):
        raise InputRejected(f"계획 {plan.get('id')}: base_tuning 이 있으면 base_tuning_ships(불)가 필요하다")
    if not ships:
        return [], [item["json_path"] for item in base_tuning]
    entries = [{"json_path": item["json_path"], "value": item["value"], "split_group": bool(item.get("split_group", False)), "kind": BASE_TUNING_ENTRY, "from": source}
               for item in base_tuning]
    return entries, []


def validate_overlay_assignments(assignments, root, schema):
    paths = [entry["json_path"] for entry in assignments]
    duplicates = sorted({path for path in paths if paths.count(path) > 1})
    if duplicates:
        raise InputRejected(f"덧씌움에 같은 경로가 둘 이상: {', '.join(duplicates)}(overlay-replace 를 쓴다)")
    for path in paths:
        reject_golden_snapshot_path(path)
    schema = schema or check_fluid_preset_data.load_schema(root)
    tuning, look, _ = patch_documents(load_canonical_document(root), load_canonical_look(root), schema, assignments)
    errors, _ = check_fluid_preset_data.check_document(schema, tuning)
    errors = errors + fluid_material_look.find_look_problems(look)
    if errors:
        raise PatchError("덧씌움 적용 결과 검사 실패: " + "; ".join(errors[:10]))


def validate_actor_props(actor_props):
    keys = [tuple(entry.get(key) for key in ("level", "label", "component", "property")) for entry in actor_props]
    duplicates = sorted({describe_actor_prop(entry) for entry, key in zip(actor_props, keys) if keys.count(key) > 1})
    if duplicates:
        raise InputRejected(f"덧씌움 actor_props 에 같은 속성이 둘 이상: {', '.join(duplicates)}")


def history_entries(operation, entries, reason, at):
    return [{"op": operation, "json_path": entry["json_path"] if "json_path" in entry else describe_actor_prop(entry), "at": at, "reason": reason, "from": entry.get("from")}
            for entry in entries]


def read_plan_for_judge(judge_path):
    judge, adopt = read_adopting_judge(judge_path)
    plan_path = verify_judge_lineage(judge, judge_path)
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    knobs, actor_props, source = adopted_entries(judge_path, judge, adopt, plan)
    bases, measured_paths = base_tuning_entries(plan, source)
    for knob in knobs:
        if measured_paths:
            knob["measured_under_base"] = measured_paths
    return judge, knobs, actor_props, bases


def accept_judge(judge_path, overlay_path, root=ROOT, schema=None):
    overlay_path = require_under_saved(overlay_path, root)
    judge, knobs, actor_props, bases = read_plan_for_judge(judge_path)
    current_sha = overlay_sha256(overlay_path)
    if judge_field(judge, "overlay_sha256") != current_sha:
        raise InputRejected(f"현재 덧씌움 sha256 {current_sha} 이 판정의 overlay_sha256 {judge_field(judge, 'overlay_sha256')} 과 다르다(채택 사이 끼워 넣기 차단)")
    overlay = read_overlay(overlay_path, is_missing_allowed=True)
    added = bases + knobs
    validate_overlay_assignments(overlay["assignments"] + added, root, schema)
    validate_actor_props(overlay["actor_props"] + actor_props)
    at = datetime.now().isoformat(timespec="seconds")
    overlay["assignments"].extend(added)
    overlay["actor_props"].extend(actor_props)
    overlay["history"].extend(history_entries("add", added + actor_props, "accept", at))
    write_overlay(overlay_path, overlay)
    return {"overlay": overlay_path.as_posix(), "overlay_sha256": file_sha256(overlay_path), "added": [entry["json_path"] for entry in added], "actor_props_added": len(actor_props)}


def remove_overlay_entry(overlay_path, json_path, reason, root=ROOT, schema=None):
    overlay_path = require_under_saved(overlay_path, root)
    if not reason:
        raise InputRejected("--reason 이 필요하다")
    overlay = read_overlay(overlay_path)
    removed = [entry for entry in overlay["assignments"] if entry["json_path"] == json_path]
    if not removed:
        raise InputRejected(f"{json_path}: 덧씌움에 없다")
    remaining = [entry for entry in overlay["assignments"] if entry["json_path"] != json_path]
    validate_overlay_assignments(remaining, root, schema)
    overlay["assignments"] = remaining
    history = history_entries("remove", removed, reason, datetime.now().isoformat(timespec="seconds"))
    history[0]["value"] = removed[0]["value"]
    overlay["history"].extend(history)
    write_overlay(overlay_path, overlay)
    return {"overlay": overlay_path.as_posix(), "overlay_sha256": file_sha256(overlay_path), "removed": removed[0]}


def replace_overlay_entry(overlay_path, json_path, judge_path, root=ROOT, schema=None):
    overlay_path = require_under_saved(overlay_path, root)
    overlay = read_overlay(overlay_path)
    previous = next((entry for entry in overlay["assignments"] if entry["json_path"] == json_path), None)
    if previous is None:
        raise InputRejected(f"{json_path}: 덧씌움에 없다")
    judge, knobs, actor_props, bases = read_plan_for_judge(judge_path)
    previous_source = previous.get("from") or {}
    if judge_field(judge, "plan") != previous_source.get("plan"):
        raise InputRejected(f"{judge_path}: 원래 채택의 계획 {previous_source.get('plan')} 과 다른 계획의 판정이다")
    original_judge_path = prereg_lock.project_path(previous_source.get("confirm_judge") or "")
    if not original_judge_path.is_file() or file_sha256(original_judge_path) != previous_source.get("judge_sha256"):
        raise InputRejected(f"{json_path}: 원래 채택 판정 {previous_source.get('confirm_judge')} 이 없거나 바뀌었다")
    original_judge = json.loads(original_judge_path.read_text(encoding="utf-8"))
    if not isinstance(original_judge.get("adopt"), dict) or judge_field(judge, "overlay_sha256") != judge_field(original_judge, "overlay_sha256"):
        raise InputRejected(f"{judge_path}: 원래 채택과 다른 덧씌움에서 잰 판정이다(재판정만 교체할 수 있다)")
    remaining = [entry for entry in overlay["assignments"] if entry["json_path"] != json_path]
    kept_bases = {(entry["json_path"], json.dumps(entry["value"], sort_keys=True)) for entry in remaining if entry.get("kind") == BASE_TUNING_ENTRY}
    added = [entry for entry in bases if (entry["json_path"], json.dumps(entry["value"], sort_keys=True)) not in kept_bases] + knobs
    validate_overlay_assignments(remaining + added, root, schema)
    validate_actor_props(overlay["actor_props"] + actor_props)
    at = datetime.now().isoformat(timespec="seconds")
    overlay["assignments"] = remaining + added
    overlay["actor_props"].extend(actor_props)
    history = history_entries("replace", added + actor_props, f"overlay-replace {prereg_lock.relative_text(judge_path)}", at)
    for entry in history:
        entry["previous"] = {"json_path": json_path, "value": previous["value"], "from": previous_source}
    overlay["history"].extend(history)
    write_overlay(overlay_path, overlay)
    return {"overlay": overlay_path.as_posix(), "overlay_sha256": file_sha256(overlay_path), "replaced": json_path, "added": [entry["json_path"] for entry in added]}


def materialize_overlay(overlay_path, out_dir, root=ROOT, schema=None):
    overlay = read_overlay(overlay_path)
    extra = {"overlay": Path(overlay_path).resolve().as_posix(), "overlay_sha256": file_sha256(overlay_path), "actor_props_not_in_copy": len(overlay["actor_props"])}
    return write_variant(overlay["assignments"], out_dir, root, schema, manifest_extra=extra)


def parse_assignment(text, split_group):
    json_path, separator, value_text = text.partition("=")
    if not separator:
        raise InputRejected(f"--set {text!r}: <JSON 경로>=<JSON 값> 꼴이어야 한다")
    try:
        value = json.loads(value_text)
    except ValueError as error:
        raise InputRejected(f"--set {text!r}: 값이 JSON 이 아니다({error})") from error
    return {"json_path": json_path.strip(), "value": value, "split_group": split_group}


def collect_assignments(arguments):
    return [parse_assignment(text, False) for text in arguments.set] + [parse_assignment(text, True) for text in arguments.set_split]


def resolve_path(path, root):
    path = Path(path)
    return path if path.is_absolute() else Path(root) / path


def parse_arguments():
    parser = argparse.ArgumentParser(description="FluidFX 조절·모양 JSON 사본·되쓰기·덧씌움(spec-wave0-data-bridge-v2 3.9, spec-cal0-v2 C2·C7)")
    parser.add_argument("--root", default=str(ROOT))
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("variant", "write-canonical"):
        command = commands.add_parser(name)
        command.add_argument("--set", action="append", default=[], help="<JSON 경로>=<JSON 값>, 묶음 소유 필드면 거부")
        command.add_argument("--set-split", action="append", default=[], help="<Kinds 경로>=<JSON 값>, 묶음 소유 필드를 회원 종류로 펼친 뒤 이 종류만 바꾼다")
    commands.choices["variant"].add_argument("--out-dir", required=True, type=Path)
    commands.choices["write-canonical"].add_argument("--backup-dir", required=True, type=Path)
    commands.choices["write-canonical"].add_argument("--from-overlay", type=Path, help="덧씌움 v2 의 assignments 를 --set 뒤에 순서대로 쓴다")
    commands.choices["write-canonical"].add_argument("--acknowledge-header", action="store_true", help="StructDefaults·Globals 경로: 헤더 기본값도 같은 커밋에서 바꾼다")
    restore = commands.add_parser("restore")
    restore.add_argument("--backup-dir", required=True, type=Path)
    for name in ("accept", "overlay-remove", "overlay-replace", "materialize"):
        commands.add_parser(name).add_argument("--overlay", type=Path)
    commands.choices["accept"].add_argument("--judge", required=True, type=Path)
    commands.choices["overlay-remove"].add_argument("--json-path", required=True)
    commands.choices["overlay-remove"].add_argument("--reason", required=True)
    commands.choices["overlay-replace"].add_argument("--json-path", required=True)
    commands.choices["overlay-replace"].add_argument("--judge", required=True, type=Path)
    commands.choices["materialize"].add_argument("--out", required=True, type=Path)
    return parser.parse_args()


def run_command(arguments, root):
    if arguments.command == "restore":
        return restore_canonical(resolve_path(arguments.backup_dir, root), root)
    if arguments.command == "variant":
        return write_variant(collect_assignments(arguments), resolve_path(arguments.out_dir, root), root)
    if arguments.command == "write-canonical":
        assignments = collect_assignments(arguments)
        actor_props = []
        if arguments.from_overlay:
            overlay = read_overlay(resolve_path(arguments.from_overlay, root))
            assignments += overlay["assignments"]
            actor_props = overlay["actor_props"]
        result = write_canonical(assignments, resolve_path(arguments.backup_dir, root), root, acknowledge_header=arguments.acknowledge_header)
        return dict(result, actor_props_not_written=[describe_actor_prop(item) for item in actor_props])
    overlay_path = resolve_path(arguments.overlay, root) if arguments.overlay else default_overlay_path(root)
    if arguments.command == "accept":
        return accept_judge(resolve_path(arguments.judge, root), overlay_path, root)
    if arguments.command == "overlay-remove":
        return remove_overlay_entry(overlay_path, arguments.json_path, arguments.reason, root)
    if arguments.command == "overlay-replace":
        return replace_overlay_entry(overlay_path, arguments.json_path, resolve_path(arguments.judge, root), root)
    return materialize_overlay(overlay_path, resolve_path(arguments.out, root), root)


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    arguments = parse_arguments()
    root = Path(arguments.root).resolve()
    try:
        print(json.dumps(run_command(arguments, root), ensure_ascii=False, indent=2))
    except (InputRejected, check_fluid_preset_data.InputError, OSError, ValueError, KeyError, TypeError) as error:
        print(f"입력 오류: {error}", file=sys.stderr)
        return EXIT_INPUT
    except PatchError as error:
        print(f"거부: {error}", file=sys.stderr)
        return EXIT_CHECK_FAILED
    if arguments.command == "write-canonical":
        print("다음: PowerShell 에서 python Tools/run_in_editor.py Tools/FluidFX/editor_dump_fluid_preset_builders.py 로 빌더 덤프를 기준선과 비교하고, python Tools/check_fluid_preset_data.py --stage-report <무대 보고서> 가 STALE 0 인지 본다. 커밋은 별도 허가")
    return 0


if __name__ == "__main__":
    sys.exit(main())

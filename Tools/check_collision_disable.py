"""실행 환경: 시스템 Python 3(표준 라이브러리만), 에디터 불필요. Tools/ 아래 editor_*.py(scratch·_archive 폴더 제외)를 AST 로 훑어 set_collision_enabled(...NO_COLLISION) 호출마다 같은 함수의 같은 블록 또는 바깥 블록에서 같은 대상(수신 식 문자열이 같고 두 호출 사이에 그 이름 재대입 없음)에 set_collision_profile_name("NoCollision") 짝이 있는지 본다(L-editor-15: 충돌 열거만 끄면 PIE 에서 프로필 BlockAll 이 되살아난다).
실행 명령: python Tools/check_collision_disable.py [--root <프로젝트 루트>] (PowerShell 또는 Git Bash)
출력: 표준 출력에 짝 없는 호출마다 ERROR <경로>:<줄>, 허용 목록 적중은 ALLOWED, 허용 목록 항목의 파일이 있는데 짝 없는 호출이 없으면 STALE, 마지막 SUMMARY 한 줄. 종료 코드 0 위반 없음 / 1 ERROR 또는 STALE / 2 입력 오류(루트·Tools 폴더 없음, 구문 오류)
상태: 현행(2026-10-31 작성, --root fixture 로 짝 있음·없음·다른 변수·여러 줄·재대입·다른 분기·모듈 상수·허용·STALE 확인)
"""
import argparse
import ast
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATTERN = "editor_*.py"
EXCLUDED_FOLDER_NAMES = {"scratch", "_archive", "__pycache__"}
DISABLE_METHOD_NAME = "set_collision_enabled"
PROFILE_METHOD_NAME = "set_collision_profile_name"
PROFILE_KEYWORD_NAME = "collision_profile_name"
NO_COLLISION_ENUM_NAME = "NO_COLLISION"
NO_COLLISION_PROFILE_NAME = "NoCollision"
MODULE_SCOPE_NAME = "<module>"
ALLOWED_UNPAIRED_CALLS = {
    ("Tools/WorldGen/editor_build_open_world.py", "spawn_pcg_exclusion_box"): "TriggerBox PCG 제외 상자: PCG 제외 판정이 기본 Trigger 프로필 충돌에 기대는지 미검증이라 바꾸지 않음",
}


def collect_script_paths(tools_folder):
    script_paths = []
    for path in sorted(tools_folder.rglob(SCRIPT_PATTERN)):
        relative_parts = path.relative_to(tools_folder).parts[:-1]
        if any(part in EXCLUDED_FOLDER_NAMES for part in relative_parts):
            continue
        script_paths.append(path)
    return script_paths


def collect_module_constants(tree):
    constants = {}
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if isinstance(target, ast.Name):
            constants[target.id] = statement.value
    return constants


def mentions_no_collision_enum(node, constants):
    for child in ast.walk(node):
        if isinstance(child, ast.Attribute) and child.attr == NO_COLLISION_ENUM_NAME:
            return True
        if isinstance(child, ast.Name) and child.id == NO_COLLISION_ENUM_NAME:
            return True
        if isinstance(child, ast.Name) and child.id in constants and mentions_no_collision_enum(constants[child.id], {}):
            return True
    return False


def names_no_collision_profile(node, constants):
    for child in ast.walk(node):
        if isinstance(child, ast.Constant) and child.value == NO_COLLISION_PROFILE_NAME:
            return True
        if isinstance(child, ast.Name) and child.id in constants:
            value = constants[child.id]
            if isinstance(value, ast.Constant) and value.value == NO_COLLISION_PROFILE_NAME:
                return True
    return False


def find_profile_argument(call):
    if call.args:
        return call.args[0]
    return next((keyword.value for keyword in call.keywords if keyword.arg == PROFILE_KEYWORD_NAME), None)


def find_receiver_root_name(receiver):
    node = receiver
    while True:
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Attribute) or isinstance(node, ast.Subscript):
            node = node.value
            continue
        if isinstance(node, ast.Call):
            node = node.func
            continue
        return None


def make_scope(name):
    return {"name": name, "disable_calls": [], "profile_calls": [], "assignment_lines": {}}


def scan_scope(name, statements, scopes, constants):
    scope = make_scope(name)
    scopes.append(scope)
    scan_block(statements, (), scope, scopes, constants)


def join_scope_name(parent_name, child_name):
    if parent_name == MODULE_SCOPE_NAME:
        return child_name
    return f"{parent_name}.{child_name}"


def scan_block(statements, parent_path, scope, scopes, constants):
    block_path = parent_path + (id(statements),)
    for statement in statements:
        if isinstance(statement, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            scope["assignment_lines"].setdefault(statement.name, []).append(statement.lineno)
            scan_scope(join_scope_name(scope["name"], statement.name), statement.body, scopes, constants)
            continue
        scan_statement_parts(statement, block_path, scope, scopes, constants)


def scan_statement_parts(node, block_path, scope, scopes, constants):
    for _, value in ast.iter_fields(node):
        items = value if isinstance(value, list) else [value]
        if items and isinstance(items[0], ast.stmt):
            scan_block(items, block_path, scope, scopes, constants)
            continue
        for item in items:
            if isinstance(item, (ast.excepthandler, ast.match_case)):
                scan_statement_parts(item, block_path, scope, scopes, constants)
            elif isinstance(item, ast.AST):
                scan_expression(item, block_path, scope, constants)


def scan_expression(node, block_path, scope, constants):
    for child in ast.walk(node):
        if isinstance(child, ast.Name) and isinstance(child.ctx, ast.Store):
            scope["assignment_lines"].setdefault(child.id, []).append(child.lineno)
        if not isinstance(child, ast.Call) or not isinstance(child.func, ast.Attribute):
            continue
        method_name = child.func.attr
        if method_name == DISABLE_METHOD_NAME and is_no_collision_disable(child, constants):
            scope["disable_calls"].append(make_call_site(child, block_path))
        elif method_name == PROFILE_METHOD_NAME:
            argument = find_profile_argument(child)
            if argument is not None and names_no_collision_profile(argument, constants):
                scope["profile_calls"].append(make_call_site(child, block_path))


def is_no_collision_disable(call, constants):
    arguments = list(call.args) + [keyword.value for keyword in call.keywords]
    return any(mentions_no_collision_enum(argument, constants) for argument in arguments)


def make_call_site(call, block_path):
    receiver = call.func.value
    return {"line": call.lineno, "receiver": ast.unparse(receiver), "root_name": find_receiver_root_name(receiver), "block_path": block_path}


def is_reassigned_between(scope, root_name, first_line, second_line):
    if root_name is None:
        return False
    low_line, high_line = sorted((first_line, second_line))
    return any(low_line < line < high_line for line in scope["assignment_lines"].get(root_name, []))


def has_profile_pair(scope, disable_call):
    for profile_call in scope["profile_calls"]:
        if profile_call["receiver"] != disable_call["receiver"]:
            continue
        profile_path = profile_call["block_path"]
        if disable_call["block_path"][:len(profile_path)] != profile_path:
            continue
        if is_reassigned_between(scope, disable_call["root_name"], disable_call["line"], profile_call["line"]):
            continue
        return True
    return False


def check_script(root, path):
    relative_path = path.relative_to(root).as_posix()
    tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=relative_path)
    constants = collect_module_constants(tree)
    scopes = []
    scan_scope(MODULE_SCOPE_NAME, tree.body, scopes, constants)
    findings = []
    for scope in scopes:
        for disable_call in scope["disable_calls"]:
            is_paired = has_profile_pair(scope, disable_call)
            findings.append({"path": relative_path, "scope": scope["name"], "line": disable_call["line"], "receiver": disable_call["receiver"], "is_paired": is_paired})
    return findings


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=PROJECT_ROOT, help="프로젝트 루트(기본: 이 스크립트의 상위 폴더)")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    tools_folder = root / "Tools"
    if not tools_folder.is_dir():
        print(f"INPUT_ERROR Tools 폴더 없음: {tools_folder}")
        return 2

    script_paths = collect_script_paths(tools_folder)
    findings = []
    input_errors = []
    for path in script_paths:
        try:
            findings.extend(check_script(root, path))
        except SyntaxError as error:
            input_errors.append(f"{path.relative_to(root).as_posix()}:{error.lineno} {error.msg}")

    allowed_keys_used = set()
    error_count = 0
    allowed_count = 0
    for finding in findings:
        if finding["is_paired"]:
            continue
        message = f"{finding['receiver']}.{DISABLE_METHOD_NAME}(NO_COLLISION) 에 같은 대상 {PROFILE_METHOD_NAME}(\"{NO_COLLISION_PROFILE_NAME}\") 짝 없음(함수 {finding['scope']})"
        allow_key = (finding["path"], finding["scope"])
        if allow_key in ALLOWED_UNPAIRED_CALLS:
            allowed_keys_used.add(allow_key)
            allowed_count += 1
            print(f"ALLOWED {finding['path']}:{finding['line']} {message}: {ALLOWED_UNPAIRED_CALLS[allow_key]}")
            continue
        error_count += 1
        print(f"ERROR {finding['path']}:{finding['line']} {message}")

    stale_count = 0
    for allow_key, reason in ALLOWED_UNPAIRED_CALLS.items():
        if allow_key in allowed_keys_used or not (root / allow_key[0]).is_file():
            continue
        stale_count += 1
        print(f"STALE {allow_key[0]} 함수 {allow_key[1]}: 허용 목록 항목에 짝 없는 호출이 더는 없다, ALLOWED_UNPAIRED_CALLS 에서 지운다({reason})")

    for input_error in input_errors:
        print(f"INPUT_ERROR {input_error}")

    paired_count = sum(1 for finding in findings if finding["is_paired"])
    print(f"SUMMARY files={len(script_paths)} disable_calls={len(findings)} paired={paired_count} allowed={allowed_count} errors={error_count} stale={stale_count} input_errors={len(input_errors)}")
    if input_errors:
        return 2
    if error_count or stale_count:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

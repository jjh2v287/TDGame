"""시스템 Python 3.12(PowerShell·Git Bash, 에디터 불필요): 여러 세션이 함께 쓰는 작업 트리에서 JSON 명세에 적은 '내 변경'만 개인 인덱스(GIT_INDEX_FILE)로 검사·커밋하고, 공유 인덱스는 내 경로가 옛 HEAD 상태일 때만 새 커밋에 맞춘다(L-repo-20·L-repo-40).
실행: python Tools/commit_own_changes.py <명세.json> [--dry-run] [--root <저장소>] · 명세 형식은 --help · 자체 시험 python Tools/commit_own_changes.py --self-test (임시 폴더 fixture 저장소만 씀)
출력: UTF-8 표준 출력. --dry-run 은 검사 결과·개인 인덱스 --stat·부분 경로 패치, 실제 실행은 공유 인덱스 동기화 결과·새 커밋 줄·--stat·명세 경로 git status --porcelain. 종료 0 통과 / 1 거부(아무것도 커밋 안 함, 자체 시험 실패) / 2 명세 오류 / 3 커밋 뒤 이상(경합·트리 불일치·동기화 실패, 수동 명령 안내)
상태: 현행(2026-11-21)
"""
import argparse
import contextlib
import io
import json
import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
from collections import Counter
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parent.parent
SPEC_KEYS = ("message", "full_paths", "deleted_paths", "blocks", "lines", "rows", "expect_numstat")
PARTIAL_KINDS = (("rows", "prefix"), ("lines", "prefix"), ("blocks", "header"))
REPOSITORY_LOCATION_VARIABLES = ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE", "GIT_PREFIX")
SEQUENCER_STATE_NAMES = ("MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply")
REGULAR_FILE_MODES = ("100644", "100755")
NEW_FILE_MODE = "100644"
INTENT_TO_ADD_FLAG = 0x20000000
HEADING_PATTERN = re.compile(r"(#+) ")
FENCE_PATTERN = re.compile(r" {0,3}(`{3,}|~{3,})(.*)")
HUNK_HEADER_PATTERN = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", re.MULTILINE)
COMMIT_SUMMARY_PATTERN = re.compile(r"^\[.*? ([0-9a-f]{4,64})\] ", re.MULTILINE)
DEBUG_FLAGS_PATTERN = re.compile(r"flags: ([0-9a-f]+)")
INDEX_LOCK_RETRY_COUNT = 40
INDEX_LOCK_RETRY_SECONDS = 0.25
PATH_ARGUMENT_CHARACTER_LIMIT = 24000
SPEC_HELP = """명세 JSON 키(경로는 저장소 기준 상대 경로, / 구분):
  message         커밋 메시지 전문(트레일러 포함). 첫 줄은 AGENTS.md 8절 형식
  full_paths      ["경로", ...] 작업 트리 파일 통째(새 파일 포함, 폴더는 펼치지 않음)
  deleted_paths   ["경로", ...] HEAD 에 있고 작업 트리에서 지운 파일
  blocks          [{"path", "header"}] header 로 시작하는 마크다운 제목 줄부터 같은·윗단계 제목 앞까지를 떼어
                  HEAD 끝에 빈 줄 하나를 두고 붙인다(Worklog "## [" 항목, Lessons "### L-" 항목).
                  코드 펜스 안에 제목 모양 줄이 있거나 펜스가 닫히지 않으면 거부
  lines           [{"path", "prefix"}] prefix 로 시작하는 작업 트리 줄(유일)을 넣는다. 작업 트리에서 그 바로 위
                  줄은 HEAD 에서 바뀌지 않았고 HEAD 에 한 번만 있으며 같은 제목 아래인 줄이거나, 같은 명세의
                  rows·lines 줄이어야 하고 그 줄 뒤에 들어간다(줄 맞춤은 git diff). prefix 로 시작하는 줄이 HEAD
                  에 이미 있으면 거부(rows 를 쓴다)
  rows            [{"path", "prefix"}] HEAD 의 prefix 행(유일)을 작업 트리의 prefix 행(유일)으로 바꾼다
  expect_numstat  선택 {"경로": [더한 줄, 지운 줄]}, 이진 파일은 ["-", "-"]
한 경로를 통째(full_paths·deleted_paths)와 부분(blocks·lines·rows)에 함께 쓰지 않는다.
부분 경로는 rows → lines(작업 트리 순서) → blocks(작업 트리 순서) 순으로 HEAD 본문에 적용한다."""


class SpecError(Exception):
    exit_code = 2
    label = "SPEC ERROR"


class RefusedError(Exception):
    exit_code = 1
    label = "REFUSED (nothing committed)"


class AfterCommitError(Exception):
    exit_code = 3
    label = "AFTER COMMIT"


def configure_utf8_output():
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="replace")


def git_environment(index_file=None):
    environment = {key: value for key, value in os.environ.items() if key not in REPOSITORY_LOCATION_VARIABLES}
    environment["GIT_LITERAL_PATHSPECS"] = "1"
    if index_file is not None:
        environment["GIT_INDEX_FILE"] = str(index_file)
    return environment


def run_git(root, arguments, environment, data=None, check=True):
    try:
        result = subprocess.run(["git", "-c", "core.quotepath=off", *arguments], cwd=root, env=environment, input=data, capture_output=True)
    except OSError as error:
        raise RefusedError(f"cannot run git {shorten(' '.join(arguments), 160)}: {error}") from error
    if check and result.returncode != 0:
        raise RefusedError(f"git {shorten(' '.join(arguments), 160)} failed: {result.stderr.decode('utf-8', 'replace').strip()}")
    return result


def git_text(root, arguments, environment, data=None):
    return run_git(root, arguments, environment, data).stdout.decode("utf-8", "replace")


def chunk_paths(paths):
    chunk, size = [], 0
    for path in paths:
        if chunk and size + len(path) + 3 > PATH_ARGUMENT_CHARACTER_LIMIT:
            yield chunk
            chunk, size = [], 0
        chunk.append(path)
        size += len(path) + 3
    if chunk:
        yield chunk


def git_text_for_paths(root, arguments, environment, paths):
    return "".join(git_text(root, [*arguments, "--", *chunk], environment) for chunk in chunk_paths(paths))


def shorten(text, limit=60):
    return text if len(text) <= limit else text[:limit - 3] + "..."


def load_spec(spec_path):
    try:
        spec = json.loads(Path(spec_path).read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        raise SpecError(f"cannot read spec {spec_path}: {error}") from error
    if not isinstance(spec, dict):
        raise SpecError("spec must be a JSON object")
    unknown_keys = sorted(set(spec) - set(SPEC_KEYS))
    if unknown_keys:
        raise SpecError(f"unknown spec keys {unknown_keys}; allowed {list(SPEC_KEYS)}")
    message = spec.get("message")
    if not isinstance(message, str) or not message.strip():
        raise SpecError("message must be a non-empty string")
    full_paths = [normalize_path(raw, "full_paths") for raw in read_list(spec, "full_paths", str)]
    deleted_paths = [normalize_path(raw, "deleted_paths") for raw in read_list(spec, "deleted_paths", str)]
    partial_operations = read_partial_operations(spec)
    whole_paths = full_paths + deleted_paths
    repeated = sorted(path for path, count in Counter(whole_paths).items() if count > 1)
    if repeated:
        raise SpecError(f"paths listed more than once in full_paths/deleted_paths: {repeated}")
    mixed = sorted(set(whole_paths) & set(partial_operations))
    if mixed:
        raise SpecError(f"paths listed both whole and partial: {mixed}")
    all_paths = set(whole_paths) | set(partial_operations)
    if not all_paths:
        raise SpecError("spec lists no change")
    expect_numstat = read_expect_numstat(spec)
    outside = sorted(set(expect_numstat) - all_paths)
    if outside:
        raise SpecError(f"expect_numstat names paths outside the spec: {outside}")
    return {"message": message, "full_paths": full_paths, "deleted_paths": deleted_paths, "partial_operations": partial_operations, "expect_numstat": expect_numstat}


def read_list(spec, key, item_type):
    value = spec.get(key, [])
    if not isinstance(value, list) or not all(isinstance(item, item_type) for item in value):
        raise SpecError(f"{key} must be a list of {'strings' if item_type is str else 'objects'}")
    return value


def read_partial_operations(spec):
    operations = {}
    for kind, anchor_key in PARTIAL_KINDS:
        for item in read_list(spec, kind, dict):
            if set(item) != {"path", anchor_key}:
                raise SpecError(f"{kind} items need exactly the keys path and {anchor_key}: {item}")
            path = normalize_path(item["path"], kind)
            anchor = item[anchor_key]
            if not isinstance(anchor, str) or not anchor or "\n" in anchor or "\r" in anchor:
                raise SpecError(f"{kind} {path}: {anchor_key} must be a non-empty single-line string")
            anchors = operations.setdefault(path, {"rows": [], "lines": [], "blocks": []})[kind]
            if anchor in anchors:
                raise SpecError(f"{kind} {path}: {anchor_key} {anchor!r} listed twice")
            anchors.append(anchor)
    return operations


def read_expect_numstat(spec):
    value = spec.get("expect_numstat", {})
    if not isinstance(value, dict):
        raise SpecError("expect_numstat must be an object {path: [added, removed]}")
    expected = {}
    for raw_path, counts in value.items():
        path = normalize_path(raw_path, "expect_numstat")
        is_valid = isinstance(counts, list) and len(counts) == 2 and all(count == "-" or (type(count) is int and count >= 0) for count in counts)
        if not is_valid:
            raise SpecError(f"expect_numstat {path}: value must be [added, removed] integers, or [\"-\", \"-\"] for a binary file")
        expected[path] = (str(counts[0]), str(counts[1]))
    return expected


def normalize_path(raw, field):
    if not isinstance(raw, str) or not raw.strip():
        raise SpecError(f"{field}: path must be a non-empty string")
    path = raw.replace("\\", "/")
    while path.startswith("./"):
        path = path[2:]
    parts = path.split("/")
    if re.match(r"[A-Za-z]:", path) or any(part in ("", ".", "..") for part in parts) or parts[0].lower() == ".git":
        raise SpecError(f"{field}: use a repository-relative file path without '.', '..', empty parts or .git: {raw!r}")
    return path


def spec_paths(spec):
    return sorted(set(spec["full_paths"]) | set(spec["deleted_paths"]) | set(spec["partial_operations"]))


def commit_own_changes(spec, root, is_dry_run):
    top = resolve_top_level(root)
    shared_environment = git_environment()
    refuse_when_sequencer_busy(top, shared_environment)
    base_head = resolve_head(top, shared_environment)
    workspace = Path(tempfile.mkdtemp(prefix="commit_own_changes_"))
    try:
        private_index = workspace / "private.index"
        private_environment = git_environment(private_index)
        prepare_private_index(top, shared_environment, private_environment, private_index, base_head)
        base_entries = read_tree_entries(top, shared_environment, base_head, spec_paths(spec))
        new_entries, expected_changes = stage_private_index(top, spec, base_entries, shared_environment, private_environment, len(base_head))
        verified_tree = verify_private_index(top, spec, base_head, base_entries, new_entries, expected_changes, shared_environment, private_environment)
        report_private_index(top, spec, base_head, private_environment, is_dry_run)
        if is_dry_run:
            print("DRY-RUN OK: checks passed, nothing committed")
            return 0
        ensure_head_unchanged(top, shared_environment, base_head)
        commit_reference = commit_private_index(top, shared_environment, private_environment, spec["message"], workspace, base_head, verified_tree)
        finish_after_commit(top, shared_environment, base_head, commit_reference, verified_tree, base_entries, new_entries, spec_paths(spec))
        return 0
    finally:
        shutil.rmtree(workspace, ignore_errors=True)


def resolve_top_level(root):
    if not Path(root).is_dir():
        raise SpecError(f"--root is not a directory: {root}")
    result = run_git(root, ["rev-parse", "--show-toplevel"], git_environment(), check=False)
    if result.returncode != 0:
        raise SpecError(f"--root is not inside a git work tree: {root}")
    return Path(result.stdout.decode("utf-8").strip())


def resolve_git_path(top, raw_path):
    path = Path(raw_path)
    return path if path.is_absolute() else top / path


def refuse_when_sequencer_busy(top, environment):
    arguments = ["rev-parse"] + [option for name in SEQUENCER_STATE_NAMES for option in ("--git-path", name)]
    for name, raw_path in zip(SEQUENCER_STATE_NAMES, git_text(top, arguments, environment).splitlines()):
        if resolve_git_path(top, raw_path).exists():
            raise RefusedError(f"{name} exists: a merge, cherry-pick, revert or rebase is in progress, so git commit would record it")


def resolve_head(top, environment):
    result = run_git(top, ["rev-parse", "--verify", "--quiet", "HEAD^{commit}"], environment, check=False)
    if result.returncode != 0:
        raise RefusedError("HEAD has no commit yet")
    return result.stdout.decode("ascii").strip()


def prepare_private_index(top, shared_environment, private_environment, private_index, base_head):
    shared_index = resolve_git_path(top, git_text(top, ["rev-parse", "--git-path", "index"], shared_environment).strip())
    is_reused = copy_file_if_present(shared_index, private_index) and run_git(top, ["read-tree", "--reset", "-i", base_head], private_environment, check=False).returncode == 0
    if not is_reused:
        private_index.unlink(missing_ok=True)
        run_git(top, ["read-tree", base_head], private_environment)
    if run_git(top, ["diff-index", "--cached", "--quiet", base_head], private_environment, check=False).returncode != 0:
        raise RefusedError("private index differs from the base commit right after read-tree")


def copy_file_if_present(source, target):
    try:
        shutil.copyfile(source, target)
    except OSError:
        return False
    return True


def read_tree_entries(top, environment, commit, paths):
    entries = {}
    for record in git_text_for_paths(top, ["ls-tree", "-z", "--full-tree", commit], environment, paths).split("\0"):
        if not record:
            continue
        description, path = record.split("\t", 1)
        mode, kind, object_id = description.split()
        entries[path] = (mode, kind, object_id)
    return {path: entries[path] for path in paths if path in entries}


def stage_private_index(top, spec, base_entries, shared_environment, private_environment, object_id_length):
    new_entries = {}
    expected_changes = {}
    partial_paths = sorted(spec["partial_operations"])
    new_paths = check_full_paths(top, spec["full_paths"], base_entries, private_environment)
    for path in partial_paths:
        require_regular_base_entry(path, base_entries.get(path), "partial path")
        if not (top / path).is_file():
            raise RefusedError(f"{path}: working tree file is missing")
    for path in spec["deleted_paths"]:
        check_deleted_path(top, path, base_entries.get(path))
        new_entries[path] = None
    work_object_ids = hash_work_files(top, spec["full_paths"] + partial_paths, shared_environment)
    for path in spec["full_paths"]:
        new_entries[path] = (NEW_FILE_MODE if path in new_paths else base_entries[path][0], work_object_ids[path])
    for path in partial_paths:
        mode, _, base_object_id = base_entries[path]
        operations = spec["partial_operations"][path]
        base_text = decode_text(path, "HEAD", read_blob(top, shared_environment, base_object_id))
        work_text = decode_text(path, "working tree", read_blob(top, shared_environment, work_object_ids[path]))
        aligned_head_indices = map_unchanged_lines(top, shared_environment, base_object_id, work_object_ids[path], base_text, work_text) if operations["lines"] else {}
        new_text, added, removed = build_partial_text(path, base_text, work_text, operations, aligned_head_indices)
        new_entries[path] = (mode, hash_text(top, path, new_text, shared_environment))
        expected_changes[path] = (added, removed)
    write_index_entries(top, private_environment, new_entries, object_id_length)
    return new_entries, expected_changes


def check_full_paths(top, paths, base_entries, private_environment):
    for path in paths:
        work_file = top / path
        if work_file.is_symlink() or not work_file.is_file():
            raise RefusedError(f"full_paths {path}: not a regular file in the working tree (folders are not expanded; deleted files go to deleted_paths)")
        if path in base_entries:
            require_regular_base_entry(path, base_entries[path], "full_paths")
    new_paths = [path for path in paths if path not in base_entries]
    untracked = set(git_text_for_paths(top, ["ls-files", "-z", "--others", "--exclude-standard"], private_environment, new_paths).split("\0"))
    for path in new_paths:
        if path not in untracked:
            raise RefusedError(f"full_paths {path}: not in HEAD and not an untracked, non-ignored file with exactly this name (check letter case and .gitignore)")
    return set(new_paths)


def check_deleted_path(top, path, base_entry):
    require_regular_base_entry(path, base_entry, "deleted_paths")
    if os.path.lexists(top / path):
        raise RefusedError(f"deleted_paths {path}: still exists in the working tree")


def require_regular_base_entry(path, base_entry, field):
    if base_entry is None:
        raise RefusedError(f"{field} {path}: not in HEAD")
    mode, kind, _ = base_entry
    if kind != "blob" or mode not in REGULAR_FILE_MODES:
        raise RefusedError(f"{field} {path}: HEAD entry is {kind} {mode}; only regular files are supported")
    return base_entry


def hash_work_files(top, paths, environment):
    if not paths:
        return {}
    object_ids = git_text(top, ["hash-object", "-w", "--stdin-paths"], environment, data="".join(f"{path}\n" for path in paths).encode("utf-8")).split()
    if len(object_ids) != len(paths):
        raise RefusedError(f"hash-object returned {len(object_ids)} ids for {len(paths)} paths")
    return dict(zip(paths, object_ids))


def hash_text(top, path, text, environment):
    return git_text(top, ["hash-object", "-w", "--stdin", f"--path={path}"], environment, data=text.encode("utf-8")).strip()


def read_blob(top, environment, object_id):
    return run_git(top, ["cat-file", "blob", object_id], environment).stdout


def decode_text(path, source, data):
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        raise RefusedError(f"{path}: {source} text is not UTF-8 ({error})") from error
    if "\0" in text:
        raise RefusedError(f"{path}: {source} text contains NUL bytes; partial changes need a text file")
    return text


def write_index_entries(top, environment, entries, object_id_length):
    records = []
    for path, entry in sorted(entries.items()):
        mode, object_id = entry if entry is not None else ("0", "0" * object_id_length)
        records.append(f"{mode} {object_id}\t{path}\0")
    run_git(top, ["update-index", "-z", "--index-info"], environment, data="".join(records).encode("utf-8"))


def map_unchanged_lines(top, environment, base_object_id, work_object_id, base_text, work_text):
    base_count, work_count = len(split_lines(base_text)[0]), len(split_lines(work_text)[0])
    patch = git_text(top, ["diff", "-U0", "--inter-hunk-context=0", "--diff-algorithm=myers", "--no-color", "--no-ext-diff", "--no-textconv", base_object_id, work_object_id], environment)
    aligned = {}
    head_next = work_next = 0
    for match in HUNK_HEADER_PATTERN.finditer(patch):
        head_start, head_length, work_start, work_length = (int(value) if value is not None else 1 for value in match.groups())
        head_before = head_start - 1 if head_length else head_start
        work_before = work_start - 1 if work_length else work_start
        if head_before - head_next != work_before - work_next:
            raise RefusedError(f"cannot align HEAD and working tree lines at {match.group(0)}")
        aligned.update((work_next + offset, head_next + offset) for offset in range(head_before - head_next))
        head_next, work_next = head_before + head_length, work_before + work_length
    if base_count - head_next != work_count - work_next:
        raise RefusedError("cannot align HEAD and working tree lines after the last change")
    aligned.update((work_next + offset, head_next + offset) for offset in range(base_count - head_next))
    return aligned


def split_lines(text):
    pieces = text.split("\n")
    if pieces[-1] == "":
        return pieces[:-1], True
    return pieces, False


def join_lines(lines, has_final_newline):
    text = "\n".join(lines)
    return text + "\n" if lines and has_final_newline else text


def build_partial_text(path, base_text, work_text, operations, aligned_head_indices):
    base_lines, has_final_newline = split_lines(base_text)
    work_lines = split_lines(work_text)[0]
    draft = {"lines": list(base_lines), "origins": [("head", index) for index in range(len(base_lines))], "claims": set(), "placed": {}}
    row_added, row_removed = replace_rows(path, draft, work_lines, operations["rows"])
    line_added = insert_lines(path, draft, base_lines, has_final_newline, work_lines, aligned_head_indices, operations["lines"])
    block_added = append_blocks(path, draft, base_lines, has_final_newline, work_lines, operations["blocks"])
    return join_lines(draft["lines"], has_final_newline), row_added + line_added + block_added, row_removed


def find_unique_line(lines, prefix, label, source):
    matches = [index for index, line in enumerate(lines) if line.startswith(prefix)]
    if len(matches) != 1:
        raise RefusedError(f"{label}: {len(matches)} lines start with {prefix!r} in the {source}; need exactly 1")
    return matches[0]


def claim_lines(claims, indices, label):
    if claims & indices:
        raise RefusedError(f"{label}: overlaps a working tree line already taken by another rows/lines/blocks item")
    claims.update(indices)


def replace_rows(path, draft, work_lines, prefixes):
    lines = draft["lines"]
    added, removed = [], []
    for prefix in prefixes:
        work_index = find_unique_line(work_lines, prefix, f"rows {path}", "working tree")
        claim_lines(draft["claims"], {work_index}, f"rows {path} {prefix!r}")
        base_index = find_unique_line(lines, prefix, f"rows {path}", "HEAD")
        if lines[base_index] == work_lines[work_index]:
            raise RefusedError(f"rows {path}: the row starting with {prefix!r} is unchanged")
        removed.append(lines[base_index])
        added.append(work_lines[work_index])
        lines[base_index] = work_lines[work_index]
        draft["placed"][work_index] = draft["origins"][base_index]
    return added, removed


def insert_lines(path, draft, base_lines, has_final_newline, work_lines, aligned_head_indices, prefixes):
    lines, origins = draft["lines"], draft["origins"]
    added = []
    located = sorted((find_unique_line(work_lines, prefix, f"lines {path}", "working tree"), prefix) for prefix in prefixes)
    for work_index, prefix in located:
        claim_lines(draft["claims"], {work_index}, f"lines {path} {prefix!r}")
        if any(line.startswith(prefix) for line in base_lines):
            raise RefusedError(f"lines {path}: HEAD already has a line starting with {prefix!r}; use rows to replace it")
        if work_index == 0:
            raise RefusedError(f"lines {path}: {prefix!r} is the first line, so there is no line above it to anchor on")
        target = draft["placed"].get(work_index - 1) or resolve_head_anchor(path, prefix, base_lines, work_lines, aligned_head_indices, work_index - 1)
        position = origins.index(target)
        if position == len(lines) - 1 and not has_final_newline:
            raise RefusedError(f"lines {path}: the anchor is the last line of HEAD and has no newline")
        lines.insert(position + 1, work_lines[work_index])
        origins.insert(position + 1, ("work", work_index))
        draft["placed"][work_index] = ("work", work_index)
        added.append(work_lines[work_index])
    return added


def resolve_head_anchor(path, prefix, base_lines, work_lines, aligned_head_indices, anchor_index):
    anchor = work_lines[anchor_index]
    head_index = aligned_head_indices.get(anchor_index)
    if head_index is None:
        raise RefusedError(f"lines {path}: the line above {prefix!r} ({shorten(anchor)!r}) is not an unchanged HEAD line (another session's uncommitted or changed line?)")
    occurrences = base_lines.count(anchor)
    if occurrences != 1:
        raise RefusedError(f"lines {path}: the line above {prefix!r} ({shorten(anchor)!r}) appears {occurrences} times in HEAD; it must appear exactly once")
    work_heading = find_section_heading(work_lines, anchor_index)
    head_heading = find_section_heading(base_lines, head_index)
    is_same_section = work_heading == head_heading if work_heading is None or head_heading is None else aligned_head_indices.get(work_heading) == head_heading
    if not is_same_section:
        raise RefusedError(f"lines {path}: the line above {prefix!r} ({shorten(anchor)!r}) lines up with HEAD line {head_index + 1}, but its section heading differs between the working tree and HEAD, so the match is ambiguous")
    return ("head", head_index)


def find_section_heading(lines, index):
    for position in range(index, -1, -1):
        if HEADING_PATTERN.match(lines[position]):
            return position
    return None


def append_blocks(path, draft, base_lines, has_final_newline, work_lines, headers):
    lines, origins = draft["lines"], draft["origins"]
    added = []
    located = sorted((find_unique_line(work_lines, header, f"blocks {path}", "working tree"), header) for header in headers)
    if located and lines and not has_final_newline:
        raise RefusedError(f"blocks {path}: HEAD text does not end with a newline")
    for start, header in located:
        if any(line.startswith(header) for line in base_lines):
            raise RefusedError(f"blocks {path}: HEAD already has a line starting with {header!r}")
        block = extract_block(path, work_lines, start)
        claim_lines(draft["claims"], set(range(start, start + len(block))), f"blocks {path} {header!r}")
        if lines and lines[-1].strip():
            lines.append("")
            origins.append(("separator", start))
            added.append("")
        lines.extend(block)
        origins.extend(("work", start + offset) for offset in range(len(block)))
        added.extend(block)
    return added


def extract_block(path, work_lines, start):
    heading = HEADING_PATTERN.match(work_lines[start])
    if heading is None:
        raise RefusedError(f"blocks {path}: line {start + 1} is not a Markdown heading ('# ', '## ', ...)")
    level = len(heading.group(1))
    end = start + 1
    open_fence = None
    while end < len(work_lines):
        line = work_lines[end]
        fence = FENCE_PATTERN.fullmatch(line)
        if open_fence is None and fence is not None and not (fence.group(1)[0] == "`" and "`" in fence.group(2)):
            open_fence = fence.group(1)
        elif open_fence is not None and fence is not None and fence.group(1)[0] == open_fence[0] and len(fence.group(1)) >= len(open_fence) and not fence.group(2).strip():
            open_fence = None
        elif is_heading_at_or_above(line, level):
            if open_fence is None:
                break
            raise RefusedError(f"blocks {path}: line {end + 1} looks like a heading inside a code fence, so the block boundary is ambiguous; commit this file another way")
        end += 1
    if open_fence is not None:
        raise RefusedError(f"blocks {path}: a code fence opened in the block is not closed")
    block = work_lines[start:end]
    while block and not block[-1].strip():
        block.pop()
    return block


def is_heading_at_or_above(line, level):
    heading = HEADING_PATTERN.match(line)
    return heading is not None and len(heading.group(1)) <= level


def verify_private_index(top, spec, base_head, base_entries, new_entries, expected_changes, shared_environment, private_environment):
    numstat = read_numstat(top, private_environment, base_head)
    unexpected = sorted(set(numstat) - set(new_entries))
    if unexpected:
        raise RefusedError(f"private index changes paths outside the spec: {unexpected}")
    unchanged = sorted(set(new_entries) - set(numstat))
    if unchanged:
        raise RefusedError(f"spec paths with no change against HEAD (already committed or wrong path?): {unchanged}")
    for path, expected in sorted(spec["expect_numstat"].items()):
        if numstat[path] != expected:
            raise RefusedError(f"expect_numstat {path}: expected {list(expected)}, staged {list(numstat[path])} (edits outside the spec in this file?)")
    for path, (expected_added, expected_removed) in sorted(expected_changes.items()):
        added, removed = parse_changed_lines(git_text(top, ["diff-index", "--cached", "-p", "-U0", "--no-color", "--no-ext-diff", "--no-textconv", "--no-renames", base_head, "--", path], private_environment))
        if Counter(added) != Counter(expected_added) or Counter(removed) != Counter(expected_removed):
            raise RefusedError(f"{path}: staged diff (+{len(added)} -{len(removed)}) is not exactly the rows/lines/blocks content (+{len(expected_added)} -{len(expected_removed)})")
    refuse_line_ending_rewrites(top, shared_environment, base_entries, new_entries)
    return git_text(top, ["write-tree"], private_environment).strip()


def read_numstat(top, environment, base_head):
    numstat = {}
    for record in git_text(top, ["diff-index", "--cached", "--numstat", "-z", "--no-renames", base_head], environment).split("\0"):
        if record:
            added, removed, path = record.split("\t", 2)
            numstat[path] = (added, removed)
    return numstat


def parse_changed_lines(patch):
    added, removed = [], []
    is_inside_hunk = False
    for line in patch.split("\n"):
        if line.startswith("diff --git "):
            is_inside_hunk = False
        elif line.startswith("@@"):
            is_inside_hunk = True
        elif is_inside_hunk and line.startswith("+"):
            added.append(line[1:])
        elif is_inside_hunk and line.startswith("-"):
            removed.append(line[1:])
    return added, removed


def refuse_line_ending_rewrites(top, environment, base_entries, new_entries):
    pairs = [(path, base_entries[path][2], entry[1]) for path, entry in sorted(new_entries.items()) if entry is not None and path in base_entries]
    if not pairs:
        return
    blobs = read_blobs(top, environment, [object_id for _, old_id, new_id in pairs for object_id in (old_id, new_id)])
    for path, old_id, new_id in pairs:
        if b"\r\n" in blobs[old_id] and b"\r\n" not in blobs[new_id]:
            raise RefusedError(f"{path}: HEAD stores CRLF but the new blob has none, so the commit would rewrite every line (hash-object ignores safer autocrlf, L-repo-40); normalize this file in a separate commit")


def read_blobs(top, environment, object_ids):
    unique_ids = list(dict.fromkeys(object_ids))
    output = run_git(top, ["cat-file", "--batch"], environment, data="".join(f"{object_id}\n" for object_id in unique_ids).encode("ascii")).stdout
    blobs = {}
    position = 0
    for object_id in unique_ids:
        header_end = output.index(b"\n", position)
        header = output[position:header_end].split()
        if len(header) != 3:
            raise RefusedError(f"cannot read object {object_id}: {output[position:header_end]!r}")
        size = int(header[2])
        blobs[object_id] = output[header_end + 1:header_end + 1 + size]
        position = header_end + 1 + size + 1
    return blobs


def report_private_index(top, spec, base_head, environment, is_dry_run):
    print(f"base {base_head[:12]}: private index checks passed for {len(spec_paths(spec))} paths")
    if not is_dry_run:
        return
    print(git_text(top, ["diff-index", "--cached", "--stat=200", "--stat-count=40", "--no-color", "--no-renames", base_head], environment).rstrip())
    partial_paths = sorted(spec["partial_operations"])
    if partial_paths:
        print(git_text_for_paths(top, ["diff-index", "--cached", "-p", "-U2", "--no-color", "--no-ext-diff", "--no-textconv", "--no-renames", base_head], environment, partial_paths).rstrip())


def ensure_head_unchanged(top, environment, base_head):
    current_head = resolve_head(top, environment)
    if current_head != base_head:
        raise RefusedError(f"HEAD moved from {base_head[:12]} to {current_head[:12]} while staging; rerun to rebuild on the new HEAD")


def commit_private_index(top, shared_environment, private_environment, message, workspace, base_head, verified_tree):
    message_file = workspace / "message.txt"
    message_file.write_bytes((message if message.endswith("\n") else message + "\n").encode("utf-8"))
    commit_environment = {key: value for key, value in private_environment.items() if key != "GIT_LITERAL_PATHSPECS"}
    result = run_git(top, ["-c", "gc.auto=0", "-c", "maintenance.auto=false", "commit", "-F", str(message_file)], commit_environment, check=False)
    if result.returncode != 0:
        detail = shorten((result.stderr + result.stdout).decode("utf-8", "replace").strip(), 600)
        current_head = resolve_head(top, shared_environment)
        if current_head != base_head and is_commit_on(top, shared_environment, current_head, base_head, verified_tree):
            raise AfterCommitError(f"git commit reported failure after making commit {current_head[:12]}: {detail}")
        moved_note = "" if current_head == base_head else f" (HEAD moved meanwhile to {current_head[:12]} by someone else; rerun)"
        raise RefusedError(f"git commit failed: {detail}{moved_note}")
    summaries = COMMIT_SUMMARY_PATTERN.findall(result.stdout.decode("utf-8", "replace"))
    return summaries[-1] if summaries else "HEAD"


def is_commit_on(top, environment, commit, parent, tree):
    parents = git_text(top, ["rev-list", "--parents", "-n", "1", commit], environment).split()[1:]
    return parents == [parent] and git_text(top, ["rev-parse", f"{commit}^{{tree}}"], environment).strip() == tree


def finish_after_commit(top, environment, base_head, commit_reference, verified_tree, base_entries, new_entries, paths):
    new_head = commit_reference
    stage = "resolving the new commit"
    try:
        new_head = git_text(top, ["rev-parse", "--verify", f"{commit_reference}^{{commit}}"], environment).strip()
        stage = "checking the new commit"
        check_new_commit(top, environment, base_head, new_head, verified_tree)
        stage = "syncing the shared index"
        sync_shared_index(top, environment, base_entries, new_entries, len(base_head))
        stage = "checking HEAD after the sync"
        check_head_after_sync(top, environment, new_head, new_entries)
        stage = "reporting"
        report_commit(top, environment, new_head, paths)
    except AfterCommitError:
        raise
    except Exception as error:
        raise AfterCommitError(describe_failure_after_commit(new_head, base_head, stage, error, new_entries)) from error


def describe_failure_after_commit(new_head, base_head, stage, error, new_entries):
    description = f"commit {shorten(new_head, 12)} was made, then {type(error).__name__} while {stage}: {error}."
    if stage in ("reporting", "checking HEAD after the sync"):
        return description + " The shared index was already synced."
    return description + f" The shared index may not be synced. If git log -2 shows the new commit's parent is {base_head[:12]}, run in Git Bash for each path whose shared index entry is still the old HEAD version: " + " ; ".join(sync_commands(new_entries))


def sync_commands(entries):
    commands = []
    for path, entry in sorted(entries.items()):
        if entry is None:
            commands.append(f"git update-index --force-remove -- {shlex.quote(path)}")
        else:
            commands.append(f"git update-index --add --cacheinfo {shlex.quote(f'{entry[0]},{entry[1]},{path}')}")
    return commands


def check_new_commit(top, environment, base_head, new_head, verified_tree):
    parents = git_text(top, ["rev-list", "--parents", "-n", "1", new_head], environment).split()[1:]
    if parents != [base_head]:
        raise AfterCommitError(f"RACE: new commit {new_head[:12]} has parents {[parent[:12] for parent in parents]}, expected [{base_head[:12]}]; its tree was built on {base_head[:12]} and may undo commits made meanwhile. Shared index not synced. Stop and inspect (git log -3 --stat) before any other git command")
    tree = git_text(top, ["rev-parse", f"{new_head}^{{tree}}"], environment).strip()
    if tree != verified_tree:
        raise AfterCommitError(f"commit {new_head[:12]} tree {tree[:12]} differs from the verified tree {verified_tree[:12]} (did a hook change the index?). Shared index not synced. Inspect with git show --stat {new_head[:12]}")


def sync_shared_index(top, environment, base_entries, new_entries, object_id_length):
    current_entries = read_index_entries(top, environment, sorted(new_entries))
    updates, already, left = {}, [], []
    for path, new_entry in sorted(new_entries.items()):
        entries = current_entries.get(path, [])
        target = [] if new_entry is None else [(new_entry[0], new_entry[1], "0")]
        if entries == target:
            already.append(path)
            continue
        base_entry = base_entries.get(path)
        base_state = [] if base_entry is None else [(base_entry[0], base_entry[2], "0")]
        if entries != base_state:
            left.append(path)
            continue
        updates[path] = new_entry
    if updates:
        update_shared_index(top, environment, updates, object_id_length)
    left_note = f" {left}" if left else ""
    print(f"shared index: synced {len(updates)}, already matching {len(already)}, left as staged by someone else {len(left)}{left_note}")


def read_index_entries(top, environment, paths):
    entries = {}
    for chunk in chunk_paths(paths):
        pieces = git_text(top, ["ls-files", "--stage", "--debug", "-z", "--", *chunk], environment).split("\0")
        current = pieces[0]
        for piece in pieces[1:]:
            debug, _, following = piece.rpartition("\n")
            flags = DEBUG_FLAGS_PATTERN.search(debug)
            if flags is None:
                raise RefusedError(f"cannot read index flags from ls-files --debug output: {shorten(piece, 120)!r}")
            description, path = current.split("\t", 1)
            mode, object_id, stage_number = description.split()
            if not int(flags.group(1), 16) & INTENT_TO_ADD_FLAG:
                entries.setdefault(path, []).append((mode, object_id, stage_number))
            current = following
    return entries


def update_shared_index(top, environment, updates, object_id_length):
    detail = ""
    for _ in range(INDEX_LOCK_RETRY_COUNT):
        try:
            write_index_entries(top, environment, updates, object_id_length)
            return
        except RefusedError as error:
            detail = str(error)
            if "index.lock" not in detail:
                break
            time.sleep(INDEX_LOCK_RETRY_SECONDS)
    raise AfterCommitError(f"commit made, but the shared index was not synced ({detail}). Run these in Git Bash so the next commit by anyone does not undo it: " + " ; ".join(sync_commands(updates)))


def check_head_after_sync(top, environment, new_head, new_entries):
    current_head = resolve_head(top, environment)
    if current_head == new_head:
        return
    paths = sorted(new_entries)
    mine = read_tree_entries(top, environment, new_head, paths)
    now = read_tree_entries(top, environment, current_head, paths)
    changed = sorted(path for path in paths if mine.get(path) != now.get(path))
    if changed:
        raise AfterCommitError(f"HEAD moved from my commit {new_head[:12]} to {current_head[:12]} right after it, and that commit changes my paths {changed} (possibly undoing them from a stale shared index, or a deliberate follow-up). Inspect git log -3 --stat before anything else; the shared index now stages my versions")
    print(f"note: HEAD moved from my commit {new_head[:12]} to {current_head[:12]} right after it; my paths are unchanged there")


def report_commit(top, environment, new_head, paths):
    print(git_text(top, ["log", "-1", "--no-color", "--format=%h %s", new_head], environment).strip())
    print(git_text(top, ["show", "--stat=200", "--stat-count=40", "--no-color", "--format=", new_head], environment).rstrip())
    status = git_text_for_paths(top, ["status", "--porcelain"], environment, paths).rstrip()
    print("git status --porcelain (spec paths):")
    print(status if status else "(clean)")


SELF_TEST_MESSAGE = "[claude] - commit_own_changes 자체 시험\n\nCo-Authored-By: Self Test <self-test@example.invalid>\n"
SELF_TEST_FILES = {".gitattributes": "*.md text eol=lf\n*.py text eol=lf\n*.json text eol=lf\n", ".gitignore": "Saved/\n"}


def run_self_test():
    cases = (
        ("1 Worklog 블록 둘, 남의 항목이 앞·가운데·뒤에 낌", case_worklog_blocks_between_others),
        ("2 남의 줄 사이에 낀 내 줄 둘(연속, 빈 줄+제목 앞)", case_lines_between_others),
        ("3 표 행 교체 + 바로 아래 내 새 행", case_table_row_replace),
        ("4 CRLF 작업 파일(통째·부분)·HEAD 가 CRLF 인 파일 거부", case_crlf_files),
        ("5 명세 밖 변경이 끼면 거부(남이 바꾼 기준 줄·줄 맞춤 동률 포함)", case_refuse_out_of_spec_change),
        ("6 HEAD 이동 거부·커밋 경합·커밋 뒤 실패·직후 남의 커밋·실패 보고 뒤 커밋 감지", case_head_moves),
        ("7 공유 인덱스에 남이 올린 항목 보존·add -N 항목 동기화", case_shared_index_entries_kept),
        ("8 훅 실행·훅 실패 거부·훅의 인덱스 변경 감지", case_hooks),
        ("9 모호하거나 잘못된 명세·병합 중 거부", case_refuse_bad_specs),
        ("10 --dry-run 은 HEAD·공유 인덱스를 바꾸지 않음", case_dry_run_changes_nothing),
        ("11 경로 1,600개(추적 800·새 800, 명령줄 한도 넘음, 마지막 묶음에 남의 항목)", case_many_long_paths),
        ("12 LFS 파일(포인터 커밋·LFS 훅 실행)", case_lfs_file),
        ("13 블록 속 코드 펜스(들여쓴 닫기·짧은 안쪽 펜스·정보 문자열 백틱·펜스 속 # 줄·안 닫힘)", case_block_code_fences),
        ("14 공유 인덱스 잠금(index.lock) 재시도·실패 안내", case_shared_index_lock_retry),
    )
    workspace = Path(tempfile.mkdtemp(prefix="commit_own_changes_selftest_"))
    failed_count = skipped_count = 0
    started = time.monotonic()
    try:
        for number, (title, case) in enumerate(cases, 1):
            try:
                problems = case(workspace / f"case{number}")
            except Exception as error:
                problems = [f"exception {type(error).__name__}: {error}"]
            is_skipped = len(problems) == 1 and problems[0].startswith("SKIP")
            skipped_count += is_skipped
            failed_count += bool(problems) and not is_skipped
            print(f"{'SKIP' if is_skipped else 'PASS' if not problems else 'FAIL'} {title}")
            for problem in problems:
                print(f"    - {problem}")
    finally:
        remove_tree(workspace)
    passed_count = len(cases) - failed_count - skipped_count
    print(f"SELF-TEST {'PASS' if not failed_count else 'FAIL'} {passed_count}/{len(cases)}, skipped {skipped_count} ({time.monotonic() - started:.1f}s)")
    return 0 if not failed_count else 1


def remove_tree(path):
    def make_writable_and_retry(function, target, _error):
        os.chmod(target, stat.S_IWRITE)
        function(target)
    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=make_writable_and_retry)
    else:
        shutil.rmtree(path, onerror=make_writable_and_retry)


def fixture_git(repository, *arguments, data=None):
    result = subprocess.run(["git", *arguments], cwd=repository, env=git_environment(), input=data, capture_output=True)
    if result.returncode != 0:
        raise AssertionError(f"fixture git {' '.join(arguments)}: {result.stderr.decode('utf-8', 'replace').strip()}")
    return result.stdout.decode("utf-8", "replace")


def make_fixture(repository, files):
    hooks = repository.with_name(repository.name + "-hooks")
    hooks.mkdir(parents=True)
    repository.mkdir(parents=True)
    fixture_git(repository, "init", "-q", "-b", "main")
    settings = {"user.name": "Self Test", "user.email": "self-test@example.invalid", "core.autocrlf": "true", "commit.gpgsign": "false", "core.hooksPath": hooks.as_posix()}
    for key, value in settings.items():
        fixture_git(repository, "config", key, value)
    write_files(repository, {**SELF_TEST_FILES, **files})
    fixture_git(repository, "add", "-A")
    fixture_git(repository, "commit", "-q", "-m", "base")
    return current_head(repository)


def write_files(folder, files):
    for path, content in files.items():
        target = folder / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content if isinstance(content, bytes) else content.encode("utf-8"))


def committed_bytes(repository, path, revision="HEAD"):
    result = subprocess.run(["git", "cat-file", "blob", f"{revision}:{path}"], cwd=repository, env=git_environment(), capture_output=True)
    return result.stdout if result.returncode == 0 else None


def shared_entry(repository, path):
    fields = fixture_git(repository, "ls-files", "-s", "--", path).split()
    return fields[1] if fields else None


def object_id_of(repository, revision_path):
    return fixture_git(repository, "rev-parse", revision_path).strip()


def text_object_id(repository, content):
    return fixture_git(repository, "hash-object", "--stdin", data=content.encode("utf-8")).strip()


def current_head(repository):
    return fixture_git(repository, "rev-parse", "HEAD").strip()


def worktree_changed_lines(repository, path):
    return parse_changed_lines(fixture_git(repository, "diff", "-U0", "--no-color", "--", path))


def run_tool(repository, spec, *options):
    spec_file = repository.with_name(repository.name + "-spec.json")
    spec_file.write_text(json.dumps(spec, ensure_ascii=False), encoding="utf-8")
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        exit_code = main([str(spec_file), "--root", str(repository), *options])
    return exit_code, output.getvalue()


@contextlib.contextmanager
def patched(name, make_replacement):
    original = globals()[name]
    globals()[name] = make_replacement(original)
    try:
        yield
    finally:
        globals()[name] = original


def expect(problems, condition, description):
    if not condition:
        problems.append(description)


def expect_committed(problems, repository, base_head, exit_code, output):
    expect(problems, exit_code == 0, f"exit {exit_code}: {output.strip()[-300:]}")
    parents = fixture_git(repository, "rev-list", "--parents", "-n", "1", "HEAD").split()[1:]
    expect(problems, parents == [base_head], f"new commit parents {parents} != [{base_head[:12]}]")
    expect(problems, fixture_git(repository, "log", "-1", "--format=%B").strip() == SELF_TEST_MESSAGE.strip(), "commit message differs from the spec")


def expect_refused(problems, repository, base_head, exit_code, output, expected_code, phrase):
    expect(problems, exit_code == expected_code, f"exit {exit_code}, want {expected_code}: {output.strip()[-300:]}")
    expect(problems, phrase in output, f"output lacks {phrase!r}: {output.strip()[-300:]}")
    expect(problems, current_head(repository) == base_head, f"HEAD moved after a refused run ({phrase})")


def expect_shared_matches_head(problems, repository, paths):
    for path in paths:
        expect(problems, shared_entry(repository, path) == object_id_of(repository, f"HEAD:{path}"), f"shared index entry of {path} is not the new HEAD blob")


def case_worklog_blocks_between_others(repository):
    path = "Docs/Worklog/2026-11.md"
    base = "# Worklog 2026-11\n\n## [2026-11-20 10:00] claude | - | 완료 | 첫 항목\n- 내용 A\n\n## [2026-11-20 11:00] codex | - | 완료 | 둘째 항목\n- 내용 B\n"
    other_before = "## [2026-11-21 09:00] codex | - | 시작 | 남의 항목(앞)\n- 남의 줄 1\n"
    mine_start = "## [2026-11-21 09:10] claude | T-1 | 시작 | 내 항목\n- 내 줄 1\n- 내 줄 2\n"
    other_middle = "## [2026-11-21 09:20] gemini | - | 완료 | 남의 항목(가운데)\n- 남의 줄 2\n"
    mine_done = "## [2026-11-21 09:30] claude | T-1 | 완료 | 내 항목 끝\n- 내 줄 3\n"
    other_after = "## [2026-11-21 09:40] codex | - | 완료 | 남의 항목(뒤)\n- 남의 줄 3\n"
    base_head = make_fixture(repository, {path: base})
    working = base + "\n" + "\n".join((other_before, mine_start, other_middle, mine_done, other_after))
    write_files(repository, {path: working})
    spec = {"message": SELF_TEST_MESSAGE, "blocks": [{"path": path, "header": "## [2026-11-21 09:30] claude"}, {"path": path, "header": "## [2026-11-21 09:10] claude"}], "expect_numstat": {path: [7, 0]}}
    exit_code, output = run_tool(repository, spec)
    problems = []
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, committed_bytes(repository, path) == (base + "\n" + mine_start + "\n" + mine_done).encode("utf-8"), "committed Worklog is not HEAD + my two blocks in working tree order")
    expect(problems, (repository / path).read_bytes() == working.encode("utf-8"), "working tree file changed")
    expect_shared_matches_head(problems, repository, [path])
    others = [line for block in (other_before, other_middle, other_after) for line in block.splitlines()] + ["", "", ""]
    added, removed = worktree_changed_lines(repository, path)
    expect(problems, Counter(added) == Counter(others) and not removed, f"uncommitted remainder is not exactly the others' entries: +{added} -{removed}")
    return problems


def case_lines_between_others(repository):
    path = "Docs/Tasks/board.md"
    base_lines = ["# 대장", "", "### T-1 첫 작업", "- 상태: done", "- 기록: 2026-11-20 claude — 첫 기록", "", "### T-2 둘째 작업", "- 상태: doing", "- 기록: 2026-11-20 codex — 둘째 기록"]
    mine = "- 기록: 2026-11-21 claude — 내 기록"
    mine_second = "- 기록: 2026-11-21 claude — 둘째 기록"
    other_above = "- 남의 줄 A(codex 미커밋)"
    other_below = "- 남의 줄 B(gemini 미커밋)"
    work_lines = base_lines[:3] + [other_above] + base_lines[3:5] + [mine, mine_second, other_below] + base_lines[5:]
    base_head = make_fixture(repository, {path: "\n".join(base_lines) + "\n"})
    write_files(repository, {path: "\n".join(work_lines) + "\n"})
    spec = {"message": SELF_TEST_MESSAGE, "lines": [{"path": path, "prefix": "- 기록: 2026-11-21 claude — 둘째"}, {"path": path, "prefix": "- 기록: 2026-11-21 claude — 내"}], "expect_numstat": {path: [2, 0]}}
    exit_code, output = run_tool(repository, spec)
    problems = []
    expect_committed(problems, repository, base_head, exit_code, output)
    expected = base_lines[:5] + [mine, mine_second] + base_lines[5:]
    expect(problems, committed_bytes(repository, path) == ("\n".join(expected) + "\n").encode("utf-8"), "my lines are not right after their anchors in order, or other lines came along")
    expect_shared_matches_head(problems, repository, [path])
    added, removed = worktree_changed_lines(repository, path)
    expect(problems, sorted(added) == sorted([other_above, other_below]) and not removed, f"uncommitted remainder is not exactly the two other lines: +{added} -{removed}")
    return problems


def case_table_row_replace(repository):
    path = "Tools/README.md"
    header = ["| 도구 | 용도 | 예 |", "|---|---|---|"]
    row_a = "| `a.py` | 옛 설명 A | `python a.py` |"
    row_b = "| `b.py` | 옛 설명 B | `python b.py` |"
    row_c = "| `c.py` | 옛 설명 C | `python c.py` |"
    other_a = "| `a.py` | 남이 고친 설명 A | `python a.py` |"
    mine_b = "| `b.py` | 새 설명 B(내 변경) | `python b.py --new` |"
    mine_e = "| `e.py` | 내 새 도구 E | `python e.py` |"
    other_d = "| `d.py` | 남의 새 도구 D | `python d.py` |"
    base_head = make_fixture(repository, {path: "\n".join(header + [row_a, row_b, row_c]) + "\n"})
    write_files(repository, {path: "\n".join(header + [other_a, mine_b, mine_e, row_c, other_d]) + "\n"})
    spec = {"message": SELF_TEST_MESSAGE, "rows": [{"path": path, "prefix": "| `b.py`"}], "lines": [{"path": path, "prefix": "| `e.py`"}], "expect_numstat": {path: [2, 1]}}
    exit_code, output = run_tool(repository, spec)
    problems = []
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, committed_bytes(repository, path) == ("\n".join(header + [row_a, mine_b, mine_e, row_c]) + "\n").encode("utf-8"), "committed table is not HEAD with only my row replaced and my new row below it")
    expect_shared_matches_head(problems, repository, [path])
    added, removed = worktree_changed_lines(repository, path)
    expect(problems, sorted(added) == sorted([other_a, other_d]) and removed == [row_a], f"uncommitted remainder is not exactly the other rows: +{added} -{removed}")
    return problems


def case_crlf_files(repository):
    partial, whole, legacy = "Source/Foo.cpp", "Source/Bar.cpp", "Source/Legacy.cpp"
    make_fixture(repository, {partial: b"int A;\r\nint B;\r\nint C;\r\n", whole: b"void Bar();\r\n"})
    write_files(repository, {legacy: b"old;\r\n"})
    fixture_git(repository, "-c", "core.autocrlf=false", "add", "--", legacy)
    fixture_git(repository, "commit", "-q", "-m", "legacy CRLF blob")
    base_head = current_head(repository)
    problems = []
    line_endings = fixture_git(repository, "ls-files", "--eol", "--", partial, legacy)
    expect(problems, "i/lf" in line_endings and "w/crlf" in line_endings and "i/crlf" in line_endings, f"fixture line endings are not as planned: {line_endings.strip()}")
    partial_working = b"int A;\r\nint Mine = 1;\r\nint B;\r\nint C;\r\nint Other = 2;\r\n"
    write_files(repository, {partial: partial_working, whole: b"void Bar();\r\nvoid BarMine();\r\n", legacy: b"old;\r\nnew;\r\n"})
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": [legacy]})
    expect_refused(problems, repository, base_head, exit_code, output, 1, "HEAD stores CRLF")
    spec = {"message": SELF_TEST_MESSAGE, "full_paths": [whole], "lines": [{"path": partial, "prefix": "int Mine"}], "expect_numstat": {partial: [1, 0], whole: [1, 0]}}
    exit_code, output = run_tool(repository, spec)
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, committed_bytes(repository, partial) == b"int A;\nint Mine = 1;\nint B;\nint C;\n", "partial CRLF file is not committed as LF with only my line")
    expect(problems, committed_bytes(repository, whole) == b"void Bar();\nvoid BarMine();\n", "whole CRLF file is not committed as LF")
    expect(problems, (repository / partial).read_bytes() == partial_working, "working CRLF file changed")
    expect(problems, fixture_git(repository, "status", "--porcelain", "--", whole).strip() == "", "whole file is not clean after the shared index sync")
    expect(problems, fixture_git(repository, "diff", "--numstat", "--", partial).split()[:2] == ["1", "0"], "remaining working change of the partial file is not just the other line")
    return problems


def case_refuse_out_of_spec_change(repository):
    whole, partial, decisions, board = "Tools/mine.py", "Docs/notes.md", "Docs/decisions.md", "Docs/Tasks/board.md"
    base_decisions = "# 결정\n\n### D-56 첫 결정\n- 상태: 결정됨\n- 근거: 가\n\n### D-57 둘째 결정\n- 상태: 검토 중\n- 근거: 나\n"
    work_decisions = "# 결정\n\n### D-56 첫 결정\n- 상태: 결정됨\n- 근거: 가\n\n### D-57 둘째 결정\n- 상태: 결정됨\n- 12차: claude 내 기록\n- 근거: 나\n"
    base_board = "# 대장\n### T-1 첫 작업\n- 상태: todo\n### T-2 둘째 작업\n- 상태: done\n"
    work_board = "# 대장\n### T-1 첫 작업\n- 상태: done\n- 기록: 2026-11-21 claude 끝냄\n### T-2 둘째 작업\n- 상태: doing\n"
    base_head = make_fixture(repository, {whole: "a = 1\nb = 2\n", partial: "# 노트\n- 하나\n- 둘\n", decisions: base_decisions, board: base_board})
    write_files(repository, {whole: "a = 1\nb = 3\nc = 4\n", partial: "# 노트\n- 하나\n- 남의 새 줄\n- 내 새 줄\n- 둘\n", decisions: work_decisions, board: work_board})
    shared_index = repository / ".git" / "index"
    index_before = shared_index.read_bytes()
    problems = []
    attempts = (
        ({"full_paths": [whole], "expect_numstat": {whole: [1, 0]}}, "expect_numstat"),
        ({"lines": [{"path": partial, "prefix": "- 내 새 줄"}]}, "not an unchanged HEAD line"),
        ({"lines": [{"path": decisions, "prefix": "- 12차: claude"}], "expect_numstat": {decisions: [1, 0]}}, "not an unchanged HEAD line"),
        ({"lines": [{"path": board, "prefix": "- 기록: 2026-11-21 claude"}], "expect_numstat": {board: [1, 0]}}, "section heading differs"),
    )
    for fields, phrase in attempts:
        exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, **fields})
        expect_refused(problems, repository, base_head, exit_code, output, 1, phrase)
    expect(problems, shared_index.read_bytes() == index_before, "shared index changed by a refused run")
    return problems


def case_head_moves(repository):
    path = "a.txt"
    make_fixture(repository, {path: "a\n"})
    write_files(repository, {path: "a\nmine\n"})
    spec = {"message": SELF_TEST_MESSAGE, "full_paths": [path]}
    moved = {}
    problems = []

    def commit_other(name):
        write_files(repository, {name: "other\n"})
        fixture_git(repository, "add", "--", name)
        fixture_git(repository, "commit", "-q", "-m", f"other {name}")
        return current_head(repository)

    def move_head_after_staging(original):
        def wrapper(*arguments):
            result = original(*arguments)
            moved["staging"] = commit_other("other1.txt")
            return result
        return wrapper

    with patched("stage_private_index", move_head_after_staging):
        exit_code, output = run_tool(repository, spec)
    expect(problems, exit_code == 1 and "HEAD moved" in output, f"staging race not refused: exit {exit_code} {output.strip()[-200:]}")
    expect(problems, current_head(repository) == moved.get("staging"), "a commit was made after HEAD moved")

    def move_head_before_commit(original):
        def wrapper(*arguments):
            moved["commit"] = commit_other("other2.txt")
            return original(*arguments)
        return wrapper

    with patched("commit_private_index", move_head_before_commit):
        exit_code, output = run_tool(repository, spec)
    expect(problems, exit_code == 3 and "RACE" in output, f"commit race not reported: exit {exit_code} {output.strip()[-200:]}")
    parents = fixture_git(repository, "rev-list", "--parents", "-n", "1", "HEAD").split()[1:]
    expect(problems, parents == [moved.get("commit")], "race scenario did not land on the other commit as simulated")
    expect(problems, shared_entry(repository, path) == text_object_id(repository, "a\n"), "shared index was synced despite the race")

    def fail_after_commit(original):
        def wrapper(*arguments):
            raise RefusedError("simulated failure after the commit")
        return wrapper

    write_files(repository, {path: "a\nmine\nmore\n"})
    before_failure = current_head(repository)
    with patched("read_index_entries", fail_after_commit):
        exit_code, output = run_tool(repository, spec)
    expect(problems, exit_code == 3 and "AFTER COMMIT" in output and "was made" in output and "git update-index --add --cacheinfo" in output, f"failure after the commit not labelled with sync commands: exit {exit_code} {output.strip()[-200:]}")
    expect(problems, current_head(repository) != before_failure, "the commit before the simulated failure was not made")

    def commit_other_before_sync(original):
        def wrapper(*arguments):
            moved["stale"] = commit_other("other3.txt")
            return original(*arguments)
        return wrapper

    fixture_git(repository, "update-index", "--cacheinfo", f"100644,{object_id_of(repository, 'HEAD:a.txt')},a.txt")
    write_files(repository, {path: "a\nmine\nmore\nagain\n"})
    with patched("sync_shared_index", commit_other_before_sync):
        exit_code, output = run_tool(repository, spec)
    expect(problems, exit_code == 3 and "changes my paths ['a.txt']" in output, f"a stale-index commit right after mine was not reported: exit {exit_code} {output.strip()[-200:]}")
    expect(problems, current_head(repository) == moved.get("stale"), "the simulated stale commit is not HEAD")

    def report_failure_after_writing_commit(original):
        def wrapper(root, arguments, environment, data=None, check=True):
            result = original(root, arguments, environment, data, check)
            if "commit" in arguments and "-F" in arguments:
                return subprocess.CompletedProcess(result.args, 1, result.stdout, b"simulated: the commit was written, then git failed")
            return result
        return wrapper

    write_files(repository, {path: "a\nmine\nmore\nagain\nlast\n"})
    before_reported_failure = current_head(repository)
    with patched("run_git", report_failure_after_writing_commit):
        exit_code, output = run_tool(repository, spec)
    expect(problems, exit_code == 3 and "reported failure after making commit" in output, f"a commit made before git reported failure was called 'nothing committed': exit {exit_code} {output.strip()[-200:]}")
    expect(problems, current_head(repository) != before_reported_failure, "the simulated commit was not made")
    return problems


def case_shared_index_entries_kept(repository):
    log = "Docs/log.md"
    log_base = "# log\n\n## [2026-11-20 10:00] codex | - | 완료 | 옛 항목\n- 옛 줄\n"
    mine_block = "## [2026-11-21 09:00] claude | - | 완료 | 내 항목\n- 내 줄\n"
    base_head = make_fixture(repository, {"a.txt": "a\n", "b.txt": "b\n", "mine.txt": "m\n", "gone.txt": "g\n", log: log_base})
    write_files(repository, {"a.txt": "a other\n", "b.txt": "b other staged\n", "their_new.txt": "t\n"})
    fixture_git(repository, "add", "--", "a.txt", "b.txt", "their_new.txt")
    write_files(repository, {"b.txt": "b mine\n", "mine.txt": "m mine\n", "new.txt": "n\n", "ita.txt": "i\n", log: log_base + "\n" + mine_block})
    fixture_git(repository, "add", "-N", "--", "ita.txt")
    (repository / "gone.txt").unlink()
    spec = {"message": SELF_TEST_MESSAGE, "full_paths": ["b.txt", "mine.txt", "new.txt", "ita.txt"], "deleted_paths": ["gone.txt"], "blocks": [{"path": log, "header": "## [2026-11-21 09:00] claude"}]}
    exit_code, output = run_tool(repository, spec)
    problems = []
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, shared_entry(repository, "a.txt") == text_object_id(repository, "a other\n"), "another session's staged a.txt changed")
    expect(problems, shared_entry(repository, "b.txt") == text_object_id(repository, "b other staged\n"), "another session's staged b.txt was overwritten")
    expect(problems, shared_entry(repository, "their_new.txt") == text_object_id(repository, "t\n"), "another session's staged new file changed")
    expect(problems, "left as staged by someone else 1 ['b.txt']" in output, f"output does not report exactly the left b.txt entry: {output.strip()[-300:]}")
    expect_shared_matches_head(problems, repository, ["mine.txt", "new.txt", "ita.txt", log])
    expect(problems, shared_entry(repository, "gone.txt") is None, "deleted path still in the shared index")
    expect(problems, committed_bytes(repository, "b.txt") == b"b mine\n", "b.txt commit is not the working tree version")
    expect(problems, committed_bytes(repository, "gone.txt") is None, "gone.txt still in HEAD")
    expect(problems, committed_bytes(repository, "a.txt") == b"a\n" and committed_bytes(repository, "their_new.txt") is None, "another session's staged change got into my commit")
    staged = sorted(fixture_git(repository, "diff", "--cached", "--name-only").split())
    expect(problems, staged == ["a.txt", "b.txt", "their_new.txt"], f"shared index staged set after sync is {staged} (an add -N entry left behind deletes the file on the next commit)")
    return problems


def case_hooks(repository):
    base_head = make_fixture(repository, {"a.txt": "a\n", "b.txt": "b\n"})
    hooks = repository.with_name(repository.name + "-hooks")
    marker = hooks / "marker.txt"
    write_files(hooks, {"pre-commit": f"#!/bin/sh\ngit ls-files -- '*.txt' > '{marker.as_posix()}'\n"})
    write_files(repository, {"a.txt": "a mine\n"})
    problems = []
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": ["a.txt"]})
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, marker.exists() and "a.txt" in marker.read_text(encoding="utf-8"), "pre-commit hook did not run, or its glob pathspec saw GIT_LITERAL_PATHSPECS")
    first_commit = current_head(repository)
    write_files(hooks, {"pre-commit": "#!/bin/sh\necho 'blocked by hook' >&2\nexit 1\n"})
    write_files(repository, {"b.txt": "b mine\n"})
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": ["b.txt"]})
    expect_refused(problems, repository, first_commit, exit_code, output, 1, "blocked by hook")
    write_files(hooks, {"pre-commit": "#!/bin/sh\ngit add extra.txt\n"})
    write_files(repository, {"extra.txt": "sneaked in\n"})
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": ["b.txt"]})
    expect(problems, exit_code == 3 and "differs from the verified tree" in output, f"index change by a hook not detected: exit {exit_code} {output.strip()[-200:]}")
    return problems


def case_refuse_bad_specs(repository):
    board, log, first = "Docs/board.md", "Docs/log.md", "Docs/first.md"
    base_board = "# 대장\n\n- 상태: done\n\n- 상태: done\n- 결과: 옛 값\n- 끝\n"
    log_base = "# log\n\n## [2026-11-20 10:00] claude | - | 완료 | 옛 항목\n- 옛 줄\n"
    base_head = make_fixture(repository, {board: base_board, log: log_base, first: "# 제목\n- 줄\n", "a.txt": "a\n"})
    write_files(repository, {
        board: "# 대장\n\n- 상태: done\n\n- 내 줄(빈 줄 아래)\n- 상태: done\n- 결과: 새 값\n- 끝\n",
        log: log_base + "\n## [2026-11-21 09:00] claude | - | 완료 | 새 항목\n- 새 줄 하나\n",
        first: "- 새 첫 줄\n# 제목\n- 줄\n",
        "Saved/x.log": "ignored\n",
        "Docs/New.md": "new\n",
    })
    attempts = (
        ({"rows": [{"path": board, "prefix": "- 상태"}]}, 1, "need exactly 1"),
        ({"lines": [{"path": board, "prefix": "- 내 줄(빈 줄 아래)"}]}, 1, "appears 2 times"),
        ({"rows": [{"path": board, "prefix": "- 끝"}]}, 1, "unchanged"),
        ({"lines": [{"path": board, "prefix": "- 끝"}]}, 1, "use rows"),
        ({"rows": [{"path": board, "prefix": "- 결과:"}], "lines": [{"path": board, "prefix": "- 결과: 새"}]}, 1, "overlaps"),
        ({"lines": [{"path": first, "prefix": "- 새 첫 줄"}]}, 1, "first line"),
        ({"blocks": [{"path": log, "header": "## [2026-11-20 10:00] claude"}]}, 1, "HEAD already has a line"),
        ({"full_paths": ["Saved/x.log"]}, 1, "non-ignored"),
        ({"full_paths": ["a.txt"]}, 1, "no change against HEAD"),
        ({"deleted_paths": ["a.txt"]}, 1, "still exists"),
        ({"full_paths": ["Docs/new.md"]}, 1, "full_paths Docs/new.md"),
        ({"full_path": ["a.txt"]}, 2, "unknown spec keys"),
        ({"full_paths": ["../a.txt"]}, 2, "repository-relative"),
        ({"full_paths": ["a.txt"], "blocks": [{"path": "a.txt", "header": "# x"}]}, 2, "both whole and partial"),
    )
    problems = []
    for fields, expected_code, phrase in attempts:
        exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, **fields})
        expect_refused(problems, repository, base_head, exit_code, output, expected_code, phrase)
    merge_head = repository / ".git" / "MERGE_HEAD"
    merge_head.write_text(base_head + "\n", encoding="ascii")
    try:
        exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "blocks": [{"path": log, "header": "## [2026-11-21 09:00] claude"}]})
        expect_refused(problems, repository, base_head, exit_code, output, 1, "MERGE_HEAD exists")
    finally:
        merge_head.unlink()
    return problems


def case_dry_run_changes_nothing(repository):
    log = "Docs/log.md"
    log_base = "# log\n\n## [2026-11-20 10:00] codex | - | 완료 | 옛 항목\n- 옛 줄\n"
    block = "## [2026-11-21 09:00] claude | - | 완료 | 내 항목\n- 내 줄\n"
    base_head = make_fixture(repository, {"a.txt": "a\n", log: log_base})
    write_files(repository, {"a.txt": "a mine\n", log: log_base + "\n" + block})
    shared_index = repository / ".git" / "index"
    index_before = shared_index.read_bytes()
    spec = {"message": SELF_TEST_MESSAGE, "full_paths": ["a.txt"], "blocks": [{"path": log, "header": "## [2026-11-21 09:00] claude"}]}
    exit_code, output = run_tool(repository, spec, "--dry-run")
    problems = []
    expect(problems, exit_code == 0 and "DRY-RUN OK" in output, f"dry-run failed: exit {exit_code} {output.strip()[-300:]}")
    expect(problems, "+## [2026-11-21 09:00] claude" in output and "a.txt" in output, "dry-run output lacks the stat line or the staged block patch")
    expect(problems, current_head(repository) == base_head, "HEAD moved in dry-run")
    expect(problems, shared_index.read_bytes() == index_before, "shared index changed in dry-run")
    return problems


def case_many_long_paths(repository):
    tracked = [f"Content/__ExternalActors__/World/{index % 16:02d}/T{'L' * 24}{index:05d}.txt" for index in range(800)]
    fresh = [f"Content/__ExternalActors__/World/{index % 16:02d}/A{'L' * 24}{index:05d}.txt" for index in range(800)]
    base_head = make_fixture(repository, {name: f"{index}\n" for index, name in enumerate(tracked)})
    write_files(repository, {name: f"{index} mine\n" for index, name in enumerate(tracked)})
    write_files(repository, {name: f"{index}\n" for index, name in enumerate(fresh)})
    last = max(tracked + fresh)
    write_files(repository, {last: "staged by someone else\n"})
    fixture_git(repository, "add", "--", last)
    write_files(repository, {last: f"{tracked.index(last)} mine\n"})
    problems = []
    expect(problems, min(sum(len(name) + 1 for name in group) for group in (tracked, fresh)) > 40000, "fixture paths are too short to cross the command line limit")
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": tracked + fresh})
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, f"synced {len(tracked) + len(fresh) - 1}, already matching 0, left as staged by someone else 1 ['{last}']" in output, f"sync summary differs: {output.strip()[-300:]}")
    committed = set(fixture_git(repository, "ls-tree", "-r", "--name-only", "HEAD").splitlines())
    expect(problems, set(fresh) <= committed, "not every new path reached HEAD")
    staged = fixture_git(repository, "diff", "--cached", "--name-only").split()
    expect(problems, staged == [last], f"shared index staged set after sync has {len(staged)} paths {staged[:3]}, want only the other session's entry in the last chunk")
    return problems


def case_lfs_file(repository):
    if subprocess.run(["git", "lfs", "version"], env=git_environment(), capture_output=True).returncode != 0:
        return ["SKIP: git-lfs is not installed"]
    make_fixture(repository, {"a.txt": "a\n"})
    fixture_git(repository, "config", "--unset", "core.hooksPath")
    fixture_git(repository, "lfs", "install", "--local")
    write_files(repository, {".gitattributes": SELF_TEST_FILES[".gitattributes"] + "*.bin filter=lfs diff=lfs merge=lfs -text\n"})
    fixture_git(repository, "add", "--", ".gitattributes")
    fixture_git(repository, "commit", "-q", "-m", "track bin with LFS")
    base_head = current_head(repository)
    path = "Content/big.bin"
    write_files(repository, {path: bytes(range(256)) * 64})
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": [path]})
    problems = []
    expect_committed(problems, repository, base_head, exit_code, output)
    pointer = committed_bytes(repository, path) or b""
    expect(problems, pointer.startswith(b"version https://git-lfs.github.com/spec/v1"), f"committed blob is not an LFS pointer: {pointer[:60]!r}")
    expect_shared_matches_head(problems, repository, [path])
    expect(problems, fixture_git(repository, "status", "--porcelain", "--", path).strip() == "", "LFS file is not clean after the commit")
    return problems


def case_block_code_fences(repository):
    path = "Docs/Lessons/repo.md"
    base = "# 교훈\n\n### L-1 옛 항목\n- 옛 줄\n"
    indented_close = "### L-2 내 항목(들여쓴 닫는 펜스)\n- 해결:\n```bash\npython x.py\n  ```\n- 끝 줄\n"
    other = "### L-3 남의 항목\n- 남의 줄\n"
    hash_in_fence = "### L-4 내 항목(펜스 속 # 줄)\n```bash\n# 주석\nrun\n```\n"
    shorter_inner_fence = "### L-6 내 항목(백틱 넷 펜스 속 셋)\n````\n```\n# x\n````\n"
    backtick_info = "### L-7 내 항목(백틱이 든 정보 문자열은 펜스가 아님)\n```a`b\n- 다음 줄\n"
    other_after = "### L-8 남의 항목\n- 남의 줄 둘\n"
    unclosed = "### L-5 내 항목(닫히지 않은 펜스)\n~~~\ncode\n"
    base_head = make_fixture(repository, {path: base})
    write_files(repository, {path: base + "\n" + "\n".join((indented_close, other, hash_in_fence, shorter_inner_fence, backtick_info, other_after, unclosed))})
    problems = []
    for header, phrase in (("### L-4", "inside a code fence"), ("### L-6", "inside a code fence"), ("### L-5", "not closed")):
        exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "blocks": [{"path": path, "header": header}]})
        expect_refused(problems, repository, base_head, exit_code, output, 1, phrase)
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "blocks": [{"path": path, "header": "### L-2"}]})
    expect_committed(problems, repository, base_head, exit_code, output)
    expect(problems, committed_bytes(repository, path) == (base + "\n" + indented_close).encode("utf-8"), "block with an indented closing fence did not stop before the next entry")
    first_commit = current_head(repository)
    exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "blocks": [{"path": path, "header": "### L-7"}]})
    expect_committed(problems, repository, first_commit, exit_code, output)
    expect(problems, committed_bytes(repository, path) == (base + "\n" + indented_close + "\n" + backtick_info).encode("utf-8"), "a backtick line with a backtick in its info string was taken as a fence")
    return problems


def case_shared_index_lock_retry(repository):
    base_head = make_fixture(repository, {"a.txt": "a\n", "b.txt": "b\n"})
    lock = repository / ".git" / "index.lock"
    problems = []

    def hold_lock_during_sync(seconds):
        def make_wrapper(original):
            def wrapper(*arguments):
                lock.write_bytes(b"")
                timer = threading.Timer(seconds, lambda: lock.unlink(missing_ok=True))
                timer.start()
                try:
                    return original(*arguments)
                finally:
                    timer.cancel()
                    lock.unlink(missing_ok=True)
            return wrapper
        return make_wrapper

    write_files(repository, {"a.txt": "a mine\n"})
    with patched("sync_shared_index", hold_lock_during_sync(0.6)):
        exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": ["a.txt"]})
    expect_committed(problems, repository, base_head, exit_code, output)
    expect_shared_matches_head(problems, repository, ["a.txt"])
    first_commit = current_head(repository)
    write_files(repository, {"b.txt": "b mine\n"})
    with patched("sync_shared_index", hold_lock_during_sync(60)), patched("INDEX_LOCK_RETRY_COUNT", lambda _original: 2):
        exit_code, output = run_tool(repository, {"message": SELF_TEST_MESSAGE, "full_paths": ["b.txt"]})
    expect(problems, exit_code == 3 and "git update-index --add --cacheinfo" in output, f"a lock that never clears is not reported with sync commands: exit {exit_code} {output.strip()[-200:]}")
    expect(problems, current_head(repository) != first_commit, "the second commit was not made before the sync failure")
    return problems


def main(argv=None):
    configure_utf8_output()
    parser = argparse.ArgumentParser(description="Commit only the changes listed in a JSON spec through a private index, then sync the shared index for those paths.", epilog=SPEC_HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("spec", nargs="?", help="JSON spec path")
    parser.add_argument("--dry-run", action="store_true", help="stage and check in a private index, print its diff, commit nothing")
    parser.add_argument("--root", default=str(DEFAULT_ROOT), help="repository root (default: the folder above Tools)")
    parser.add_argument("--self-test", action="store_true", help="run the fixture tests in temporary repositories")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return run_self_test()
    if arguments.spec is None:
        parser.print_usage()
        print(f"{SpecError.label}: spec path required")
        return SpecError.exit_code
    try:
        return commit_own_changes(load_spec(arguments.spec), Path(arguments.root), arguments.dry_run)
    except (SpecError, RefusedError, AfterCommitError) as error:
        print(f"{error.label}: {error}")
        return error.exit_code


if __name__ == "__main__":
    sys.exit(main())

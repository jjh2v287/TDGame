"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py levels 로 실행.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py levels --out Saved/FluidFX/fxp/levels.json --md Saved/FluidFX/fxp/levels.md [--root <저장소 사본>].
출력: 58레벨 대응표(runbook-fxp-v3 3.5·4.4): 씨앗 Docs/FluidFX/live2_level_map.json·content_list·대조표 셋업 항목·Content 맵·무대 보고(장면 이름·cd_ids)·계획 장면으로 레벨마다 느슨(대표 맵 ≥1)·엄격(셋업 중 씨앗 맵 ∧ 장면 확인 또는 보고 cd_ids) 비율, 별칭(levels_static.json)·manual- 참조·모르는 ID(경고). 종료 1 = 없는 맵·없는 장면·씨앗 행/항목 불일치·근거 없는 excluded·수 ≠ 59/58·reason D-49·별칭 근거 불일치.
상태: 2026-11-07 FX-P X3(B4) 구현, fixture fixtures_levels.py(별칭·manual-·근거 없는 excluded·수 ≠ 59/58·없는 맵·없는 장면·reason D-49·미생성 cd_ids·같은 바이트).
"""
import re

import common

LEVELS_STATIC_PATH = "Tools/FluidFX/fxp/levels_static.json"
SETUP_SOURCE = "setup"
ITEM_LEVEL_PATTERN = re.compile(r"^SE?\.(\d+)\.(\d+)\.")
EVIDENCE_LINE_PATTERN = re.compile(r"^(?P<file>.+):(?P<line>\d+)$")
SCENE_EVIDENCE_KINDS = ("plan", "report", "cd_ids")
MANUAL_EVIDENCE = "manual"
EVIDENCE_KINDS = SCENE_EVIDENCE_KINDS + (MANUAL_EVIDENCE,)
EXCLUDED_OUT_OF_SCOPE = "범위 밖"
EXCLUDED_WAITING = "후속 대기"
EXCLUDED_KINDS = (EXCLUDED_OUT_OF_SCOPE, EXCLUDED_WAITING)
LIVE2_WORD = "LIVE-2"
MAP_SUFFIX = ".umap"
STATIC_LIST_KEYS = ("report_globs", "report_scene_containers", "report_scene_keys", "report_not_built_keys", "plan_sources", "aliases")
STATIC_TEXT_KEYS = ("level_map", "content_list", "coverage", "maps_dir", "manual_prefix")
LEVEL_MAP_KEYS = ("levels", "level_maps", "rows", "excluded")


def load_static(reads):
    static = reads.read_json(LEVELS_STATIC_PATH)
    if not isinstance(static, dict):
        raise common.InputError(f"{LEVELS_STATIC_PATH} 객체 필요")
    for key in STATIC_TEXT_KEYS:
        if not common.is_text(static.get(key)):
            raise common.InputError(f"{LEVELS_STATIC_PATH} {key} 문자열 필요")
    for key in STATIC_LIST_KEYS:
        if not isinstance(static.get(key), list):
            raise common.InputError(f"{LEVELS_STATIC_PATH} {key} 목록 필요")
    counts = static.get("expected_counts")
    if not isinstance(counts, dict) or not isinstance(counts.get("levels"), int) or not isinstance(counts.get("targets"), int):
        raise common.InputError(f"{LEVELS_STATIC_PATH} expected_counts{{levels, targets}} 정수 필요")
    if not isinstance(static.get("free_text_limit"), int):
        raise common.InputError(f"{LEVELS_STATIC_PATH} free_text_limit 정수 필요")
    for source in static["plan_sources"]:
        if not isinstance(source, dict) or not common.is_text(source.get("glob")) or not common.is_text(source.get("scene_key")):
            raise common.InputError(f"{LEVELS_STATIC_PATH} plan_sources[]{{glob, scene_key}} 필요")
    for alias in static["aliases"]:
        if not isinstance(alias, dict) or not common.is_text(alias.get("from")) or not common.is_text(alias.get("to")) or not isinstance(alias.get("evidence"), dict):
            raise common.InputError(f"{LEVELS_STATIC_PATH} aliases[]{{from, to, evidence}} 필요")
    return static


def read_content_list(reads, path):
    entries = []
    for number, line in enumerate(reads.read_text(path).splitlines(), start=1):
        if not line.strip():
            continue
        identifier, separator, name = line.partition("|")
        if not separator or not identifier.strip():
            raise common.InputError(f"content_list 형식(ID|이름): {path}:{number}")
        entries.append((identifier.strip(), name.strip()))
    return entries


def item_level(identifier):
    match = ITEM_LEVEL_PATTERN.match(identifier)
    return f"L.{match.group(1)}.{match.group(2)}" if match else None


class Coverage:
    def __init__(self, applicable, leaf_ids, row_ids):
        self.applicable = applicable
        self.leaf_ids = leaf_ids
        self.row_ids = row_ids
        self.by_id = {item["id"]: item for item in applicable}


def read_coverage(reads, path):
    document = reads.read_json(path)
    if not isinstance(document, dict) or not isinstance(document.get("items"), list) or not isinstance(document.get("rows"), list):
        raise common.InputError(f"{path} rows·items 목록 필요")
    row_ids = {row.get("id") for row in document["rows"] if isinstance(row, dict) and common.is_text(row.get("id"))}
    applicable, leaf_ids = [], set()
    for item in document["items"]:
        if not isinstance(item, dict) or item.get("source") != SETUP_SOURCE:
            continue
        if not common.is_text(item.get("id")):
            raise common.InputError(f"{path} 셋업 항목 id 필요")
        leaf_ids.add(item["id"])
        if item.get("not_applicable"):
            continue
        applicable.append({"id": item["id"], "row": item.get("row"), "level": item_level(item["id"])})
    return Coverage(applicable, leaf_ids, row_ids)


def map_name(value):
    if not common.is_text(value):
        return None
    name = str(value).replace("\\", "/").rsplit("/", 1)[-1]
    name = name.split(".", 1)[0] if not name.endswith(MAP_SUFFIX) else name[:-len(MAP_SUFFIX)]
    return name or None


def scalar_token(value):
    if isinstance(value, bool) or not isinstance(value, (str, int)):
        return None
    text = str(value).strip()
    return text or None


def element_tokens(element, keys):
    return [token for token in (scalar_token(element.get(key)) for key in keys) if token]


def add_token(tokens, map_value, token):
    if map_value and token:
        tokens.setdefault(map_value, set()).add(token)


def built_zone_keys(document):
    zones = document.get("built_zones")
    return {str(zone) for zone in zones} if isinstance(zones, list) else None


def collect_report_tokens(document, static, tokens):
    top_map = map_name(document.get("map"))
    keys = static["report_scene_keys"]
    built_zones = built_zone_keys(document)
    for container in static["report_scene_containers"]:
        value = document.get(container)
        if isinstance(value, list):
            pairs = [(None, element) for element in value]
        elif isinstance(value, dict):
            pairs = [(key, value[key]) for key in sorted(value) if not (container == "zones" and built_zones is not None and key not in built_zones)]
        else:
            continue
        for key, element in pairs:
            add_token(tokens, top_map, scalar_token(key))
            if isinstance(element, dict):
                element_map = map_name(element.get("map")) or top_map
                for token in element_tokens(element, keys):
                    add_token(tokens, element_map, token)
            else:
                add_token(tokens, top_map, scalar_token(element))


def collect_cd_ids(report_path, document, static):
    keys, not_built_keys, built_zones, found = static["report_scene_keys"], set(static["report_not_built_keys"]), built_zone_keys(document), []

    def visit(value, path, inherited_map, built):
        if isinstance(value, list):
            for index, element in enumerate(value):
                visit(element, f"{path}[{index}]", inherited_map, built)
            return
        if not isinstance(value, dict):
            return
        current_map = map_name(value.get("map")) or inherited_map
        identifiers = value.get("cd_ids")
        if isinstance(identifiers, list):
            owner_map = map_name(value.get("covered_by")) or current_map
            scene_tokens = element_tokens(value, keys)
            for identifier in identifiers:
                if common.is_text(identifier):
                    found.append({"id": identifier.strip(), "report": report_path, "path": f"{path}.cd_ids" if path else "cd_ids", "built": built, "map": owner_map, "scene_tokens": scene_tokens})
        for key in sorted(value):
            if key == "cd_ids":
                continue
            child_built = built and not (not path and key in not_built_keys) and not (path == "zones" and built_zones is not None and key not in built_zones)
            visit(value[key], f"{path}.{key}" if path else key, current_map, child_built)

    visit(document, "", map_name(document.get("map")), True)
    return found


class Scans:
    def __init__(self, reports, report_tokens, cd_entries, plan_files, plan_tokens, content_maps):
        self.reports = reports
        self.report_tokens = report_tokens
        self.cd_entries = cd_entries
        self.plan_files = plan_files
        self.plan_tokens = plan_tokens
        self.content_maps = content_maps
        self.cd_scene_tokens = {}
        for entry in cd_entries:
            if entry["built"]:
                for token in entry["scene_tokens"]:
                    add_token(self.cd_scene_tokens, entry["map"], token)

    def scene_found(self, evidence, map_value, scene):
        source = {"plan": self.plan_tokens, "report": self.report_tokens, "cd_ids": self.cd_scene_tokens}[evidence]
        return scene in source.get(map_value, set())


def scan_sources(reads, static):
    reports = sorted({path for pattern in static["report_globs"] for path in reads.glob(pattern)})
    report_tokens, cd_entries = {}, []
    for path in reports:
        document = reads.read_json(path)
        if not isinstance(document, dict):
            raise common.InputError(f"무대 보고 객체 필요: {path}")
        collect_report_tokens(document, static, report_tokens)
        cd_entries += collect_cd_ids(path, document, static)
    plan_files, plan_tokens = [], {}
    for source in static["plan_sources"]:
        for path in reads.glob(source["glob"]):
            document = reads.read_json(path)
            scenes = document.get("scenes") if isinstance(document, dict) else None
            if not isinstance(scenes, list):
                continue
            plan_files.append(path)
            for scene in scenes:
                if isinstance(scene, dict):
                    add_token(plan_tokens, map_name(scene.get("level")), scalar_token(scene.get(source["scene_key"])))
    content_maps = {map_name(path) for path in reads.glob(f"{static['maps_dir']}/*{MAP_SUFFIX}")}
    return Scans(reports, report_tokens, cd_entries, sorted(set(plan_files)), plan_tokens, content_maps)


def free_text_violation(text, limit):
    if not common.is_text(text):
        return "비어 있음"
    if len(text) > limit:
        return f"{limit}자 초과"
    if re.search(r"\d", text.replace(LIVE2_WORD, "")):
        return "숫자 포함"
    return None


def read_level_map(reads, path):
    document = reads.read_json(path)
    if not isinstance(document, dict) or not isinstance(document.get("levels"), list):
        raise common.InputError(f"{path} levels 목록 필요")
    for key in LEVEL_MAP_KEYS[1:]:
        if not isinstance(document.get(key), dict):
            raise common.InputError(f"{path} {key} 객체 필요")
    for level in document["levels"]:
        if not isinstance(level, dict) or not common.is_text(level.get("id")) or not isinstance(level.get("name"), str):
            raise common.InputError(f"{path} levels[]{{id, name}} 필요")
    for level, maps in document["level_maps"].items():
        if not isinstance(maps, list) or not all(common.is_text(value) for value in maps):
            raise common.InputError(f"{path} level_maps.{level} 문자열 목록 필요")
    for row, entries in document["rows"].items():
        if not isinstance(entries, list) or not all(isinstance(entry, dict) for entry in entries):
            raise common.InputError(f"{path} rows.{row} 객체 목록 필요")
    for level, entry in document["excluded"].items():
        if not isinstance(entry, dict):
            raise common.InputError(f"{path} excluded.{level} 객체 필요")
    return document


def check_levels_list(document, content_levels, coverage, expected, problems):
    declared = [(level["id"], level["name"]) for level in document["levels"]]
    if declared != content_levels:
        declared_ids, content_ids = {identifier for identifier, _ in declared}, {identifier for identifier, _ in content_levels}
        renamed = sorted(identifier for identifier, name in declared if identifier in content_ids and (identifier, name) not in content_levels)
        problems.append(f"levels가 content_list와 다름: 빠짐 {sorted(content_ids - declared_ids)} 더함 {sorted(declared_ids - content_ids)} 이름·순서 다름 {renamed}")
    level_ids = [identifier for identifier, _ in declared]
    with_items = {item["level"] for item in coverage.applicable}
    targets = [identifier for identifier in level_ids if identifier in with_items]
    stray = sorted({item["level"] or item["id"] for item in coverage.applicable} - set(level_ids))
    if stray:
        problems.append(f"셋업 항목의 레벨이 levels에 없음: {stray}")
    if len(level_ids) != expected["levels"] or len(targets) != expected["targets"]:
        problems.append(f"수 ≠ {expected['levels']}/{expected['targets']}: levels={len(level_ids)} targets={len(targets)}")
    return level_ids, targets


def check_level_maps(document, level_ids, targets, scans, problems):
    level_maps = {}
    for level in sorted(document["level_maps"], key=common.natural_key):
        if level not in level_ids:
            problems.append(f"level_maps에 모르는 레벨: {level}")
            continue
        maps = list(dict.fromkeys(document["level_maps"][level]))
        for value in maps:
            if value not in scans.content_maps:
                problems.append(f"없는 맵: level_maps.{level} {value}")
        level_maps[level] = maps
    for level in targets:
        if level not in document["level_maps"]:
            problems.append(f"level_maps에 대상 레벨 없음: {level}")
    return level_maps


class SeedEntry:
    def __init__(self, row, index, entry):
        self.row = row
        self.label = f"{row}[{index}]"
        self.map = entry.get("map")
        self.scene = entry.get("scene")
        self.evidence = entry.get("evidence")
        self.items = entry.get("items")
        self.valid = True

    def applies_to(self, item, level_maps):
        return (self.valid and self.evidence in SCENE_EVIDENCE_KINDS and self.map in level_maps.get(item["level"], ())
                and (self.items is None or item["id"] in self.items))


def check_seed_entry(seed, coverage, scans, level_maps, problems):
    errors = []
    if not common.is_text(seed.map) or seed.map not in scans.content_maps:
        errors.append(f"없는 맵: rows {seed.label} {seed.map}")
    if seed.evidence not in EVIDENCE_KINDS:
        errors.append(f"근거 종류 모름: rows {seed.label} {seed.evidence}")
    elif seed.evidence == MANUAL_EVIDENCE:
        if seed.scene is not None:
            errors.append(f"manual 근거는 scene null: rows {seed.label}")
    elif not common.is_text(seed.scene) or not scans.scene_found(seed.evidence, seed.map, seed.scene):
        errors.append(f"없는 장면: rows {seed.label} {seed.map} '{seed.scene}'({seed.evidence})")
    if seed.items is not None:
        if not isinstance(seed.items, list) or not seed.items or not all(common.is_text(value) for value in seed.items):
            errors.append(f"items는 셋업 ID 목록: rows {seed.label}")
        else:
            for identifier in seed.items:
                item = coverage.by_id.get(identifier)
                if item is None or item["row"] != seed.row:
                    errors.append(f"행에 없는 항목: rows {seed.label} {identifier}")
                elif seed.map not in level_maps.get(item["level"], ()):
                    errors.append(f"씨앗 항목 레벨에 맵 없음: rows {seed.label} {identifier}({item['level']}) {seed.map}")
    seed.valid = not errors
    problems.extend(errors)


def check_rows(document, coverage, scans, level_maps, problems):
    seeds = {}
    for row in sorted(document["rows"], key=common.natural_key):
        if row not in coverage.row_ids:
            problems.append(f"씨앗 행이 대조표에 없음: {row}")
        for index, entry in enumerate(document["rows"][row]):
            seed = SeedEntry(row, index, entry)
            check_seed_entry(seed, coverage, scans, level_maps, problems)
            seeds.setdefault(row, []).append(seed)
    return seeds


def check_excluded(reads, document, targets, follow_up_ids, limit, problems):
    excluded = {}
    for level in sorted(document["excluded"], key=common.natural_key):
        entry = document["excluded"][level]
        if level not in targets:
            problems.append(f"excluded에 대상 아닌 레벨: {level}")
            continue
        errors = []
        if entry.get("kind") not in EXCLUDED_KINDS:
            errors.append(f"excluded kind 모름: {level} {entry.get('kind')}")
        violation = free_text_violation(entry.get("reason"), limit)
        if violation:
            errors.append(f"reason D-49: {level} ({violation})")
        match = EVIDENCE_LINE_PATTERN.match(str(entry.get("evidence") or ""))
        evidence = {"file": match.group("file"), "line": int(match.group("line")), "expect": entry.get("expect")} if match else None
        if evidence is None or not common.check_evidence_line(reads, evidence):
            errors.append(f"근거 없는 excluded: {level} evidence '{entry.get('evidence')}' expect '{entry.get('expect')}'")
        follow_up = entry.get("follow_up")
        if entry.get("kind") == EXCLUDED_WAITING and follow_up not in follow_up_ids:
            errors.append(f"후속 대기 follow_up 모름: {level} {follow_up}")
        if entry.get("kind") == EXCLUDED_OUT_OF_SCOPE and follow_up is not None and follow_up not in follow_up_ids:
            errors.append(f"범위 밖 follow_up 모름: {level} {follow_up}")
        problems.extend(errors)
        excluded[level] = {"kind": entry.get("kind"), "reason": entry.get("reason"), "evidence": entry.get("evidence"), "follow_up": follow_up, "valid": not errors}
    return excluded


def check_aliases(reads, static, content_ids, follow_up_ids, problems):
    aliases = {}
    for alias in static["aliases"]:
        source, target = alias["from"], alias["to"]
        if source in content_ids:
            problems.append(f"별칭 원본이 실제 ID: {source}")
        if target not in content_ids:
            problems.append(f"별칭 대상이 content_list에 없음: {target}")
        if not common.check_evidence_line(reads, alias["evidence"]):
            problems.append(f"별칭 근거 줄 불일치: {source} → {target} ({alias['evidence'].get('file')}:{alias['evidence'].get('line')})")
        if alias.get("remove_after") is not None and alias.get("remove_after") not in follow_up_ids:
            problems.append(f"별칭 remove_after 모름: {source} {alias.get('remove_after')}")
        aliases[source] = target
    return aliases


class IdentifierAudit:
    def __init__(self, built, not_built, manual, aliased, unknown, non_leaf, cd_maps):
        self.built = built
        self.not_built = not_built
        self.manual = manual
        self.aliased = aliased
        self.unknown = unknown
        self.non_leaf = non_leaf
        self.cd_maps = cd_maps


def audit_identifiers(scans, aliases, content_ids, coverage, manual_prefix):
    built, not_built, manual, aliased, unknown, non_leaf, cd_maps = set(), set(), {}, [], set(), set(), {}
    for entry in scans.cd_entries:
        identifier = entry["id"]
        place = f"{entry['report']}#{entry['path']}"
        if identifier.startswith(manual_prefix):
            manual.setdefault(identifier, []).append(place)
            continue
        canonical = aliases.get(identifier, identifier)
        if canonical != identifier:
            aliased.append({"from": identifier, "to": canonical, "at": place})
        if canonical not in content_ids:
            unknown.add(identifier)
            continue
        if canonical not in coverage.leaf_ids:
            non_leaf.add(canonical)
        if entry["built"]:
            built.add(canonical)
            if entry["map"]:
                cd_maps.setdefault(canonical, set()).add(entry["map"])
        else:
            not_built.add(canonical)
    return IdentifierAudit(built, not_built - built, manual, aliased, unknown, non_leaf, cd_maps)


def strict_basis(item, identifiers, seeds, level_maps):
    basis = ["cd_ids"] if item["id"] in identifiers.built else []
    for seed in seeds.get(item["row"], []):
        if seed.applies_to(item, level_maps):
            basis.append(f"{seed.evidence}:{seed.map}:{seed.scene}")
    return basis


def build_level_rows(document, targets, level_maps, excluded, coverage, identifiers, seeds, scans):
    items_by_level = {}
    for item in coverage.applicable:
        items_by_level.setdefault(item["level"], []).append(item)
    rows = []
    for level in document["levels"]:
        identifier = level["id"]
        maps = level_maps.get(identifier, [])
        items = sorted(items_by_level.get(identifier, []), key=lambda item: common.natural_key(item["id"]))
        covered, uncovered, basis_counts = [], [], {"cd_ids": 0, "seed": 0}
        for item in items:
            basis = strict_basis(item, identifiers, seeds, level_maps)
            if basis:
                covered.append({"id": item["id"], "basis": basis})
                basis_counts["cd_ids" if basis[0] == "cd_ids" else "seed"] += 1
            else:
                uncovered.append(item["id"])
        exclusion = excluded.get(identifier)
        rows.append({"id": identifier, "name": level["name"], "target": identifier in targets, "excluded": exclusion["kind"] if exclusion else None,
                     "maps": maps, "loose": any(value in scans.content_maps for value in maps), "items": len(items), "strict_covered": len(covered),
                     "strict": round(len(covered) / len(items), 4) if items else None, "basis_counts": basis_counts, "covered": covered, "uncovered": uncovered})
    return rows


def collect_warnings(identifiers, aliases, coverage, level_maps):
    warnings = [f"모르는 ID: {identifier}" for identifier in sorted(identifiers.unknown, key=common.natural_key)]
    warnings += [f"잎 아닌 ID(셋업 항목 아님): {identifier}" for identifier in sorted(identifiers.non_leaf, key=common.natural_key)]
    used = {entry["from"] for entry in identifiers.aliased}
    warnings += [f"별칭 안 쓰임(삭제 후보): {source}" for source in sorted(aliases) if source not in used]
    for identifier in sorted(identifiers.cd_maps, key=common.natural_key):
        item = coverage.by_id.get(identifier)
        if item is None:
            continue
        missing = sorted(identifiers.cd_maps[identifier] - set(level_maps.get(item["level"], [])))
        if missing:
            warnings.append(f"cd_ids 맵이 레벨 맵에 없음: {identifier}({item['level']}) {missing}")
    return warnings


def summarize(rows, excluded, identifiers):
    scope = [row for row in rows if row["target"] and row["id"] not in excluded]
    loose = sum(1 for row in scope if row["loose"])
    ratios = [row["strict_covered"] / row["items"] for row in scope if row["items"]]
    strict_mean = round(sum(ratios) / len(ratios), 4) if ratios else 0.0
    covered = sum(row["strict_covered"] for row in scope)
    items = sum(row["items"] for row in scope)
    return {"levels": len(rows), "targets": sum(1 for row in rows if row["target"]), "non_targets": [row["id"] for row in rows if not row["target"]],
            "scope": len(scope), "loose": loose, "strict_mean": strict_mean, "strict_items": covered, "items": items, "excluded": len(excluded),
            "manual_refs": len(identifiers.manual), "unknown_ids": len(identifiers.unknown), "aliased": len(identifiers.aliased)}


def render_markdown(counts, rows, warnings, identifiers, scans, static):
    alias_text = ", ".join(f"{entry['from']} → {entry['to']}" for entry in identifiers.aliased) or "없음"
    lines = ["# FX-P 58레벨 대응 (levels, runbook-fxp-v3 3.5)", "",
             f"- 레벨 {counts['levels']}(대상 {counts['targets']}, 대상 아님 {', '.join(counts['non_targets']) or '없음'}), excluded {counts['excluded']}, 판정 범위 {counts['scope']}",
             f"- 느슨(대표 맵 ≥1, Content 존재): {counts['loose']}/{counts['scope']} · 엄격(셋업 중 씨앗 맵 ∧ 장면 확인 또는 보고 cd_ids): 레벨 평균 {counts['strict_mean']:.3f}, 항목 {counts['strict_items']}/{counts['items']}",
             f"- 원천: 씨앗 {static['level_map']} · 무대 보고 {len(scans.reports)} · 계획 {len(scans.plan_files)} · Content 맵 {len(scans.content_maps)}",
             f"- 별칭: {alias_text}(FU-16 뒤 삭제) · manual- 참조 {counts['manual_refs']}(매뉴얼 절, 따로 셈) · 모르는 ID {counts['unknown_ids']}(경고) · 미생성 cd_ids(blocked·deferred) {len(identifiers.not_built)}",
             "- 표기: 대응(무대 존재) ≠ 출하 동등(4.4), excluded는 근거 줄 필수", "",
             "| 레벨 | 이름 | 대상 | 맵 | 느슨 | 엄격 | 근거 cd/씨앗 | excluded |", "|---|---|---|---|---|---|---|---|"]
    for row in rows:
        strict = f"{row['strict_covered']}/{row['items']}" if row["items"] else "—"
        maps = "·".join(value.replace("LV_TDFluid", "") for value in row["maps"]) or "—"
        lines.append(f"| {row['id']} | {common.markdown_cell(row['name'])} | {'예' if row['target'] else '아님'} | {maps} | {'예' if row['loose'] else '아니오'} | {strict} | "
                     f"{row['basis_counts']['cd_ids']}/{row['basis_counts']['seed']} | {common.markdown_cell(row['excluded'])} |")
    lines += ["", "## 경고(종료 0)", ""] + ([f"- {warning}" for warning in warnings] or ["- 없음"])
    return lines


def build(context):
    reads, problems = context.reads, []
    static = load_static(reads)
    fxp_static = common.load_fxp_static(reads)
    follow_up_ids = {entry["id"] for entry in fxp_static["follow_ups"]}
    content = read_content_list(reads, static["content_list"])
    content_ids = {identifier for identifier, _ in content}
    content_levels = [(identifier, name) for identifier, name in content if identifier.startswith("L.")]
    coverage = read_coverage(reads, static["coverage"])
    document = read_level_map(reads, static["level_map"])
    scans = scan_sources(reads, static)
    level_ids, targets = check_levels_list(document, content_levels, coverage, static["expected_counts"], problems)
    level_maps = check_level_maps(document, level_ids, targets, scans, problems)
    seeds = check_rows(document, coverage, scans, level_maps, problems)
    excluded = check_excluded(reads, document, targets, follow_up_ids, static["free_text_limit"], problems)
    aliases = check_aliases(reads, static, content_ids, follow_up_ids, problems)
    identifiers = audit_identifiers(scans, aliases, content_ids, coverage, static["manual_prefix"])
    rows = build_level_rows(document, targets, level_maps, excluded, coverage, identifiers, seeds, scans)
    warnings = collect_warnings(identifiers, aliases, coverage, level_maps)
    counts = summarize(rows, excluded, identifiers)
    output = {"version": 1, "counts": counts, "levels": rows, "excluded": excluded, "warnings": warnings, "content_maps": sorted(scans.content_maps),
              "cd_ids": {"built": sorted(identifiers.built, key=common.natural_key), "not_built": sorted(identifiers.not_built, key=common.natural_key),
                         "manual": {key: identifiers.manual[key] for key in sorted(identifiers.manual)}, "aliased": identifiers.aliased,
                         "unknown": sorted(identifiers.unknown, key=common.natural_key), "non_leaf": sorted(identifiers.non_leaf, key=common.natural_key)},
              "seed": {"rows": len(seeds), "entries": sum(len(entries) for entries in seeds.values()), "valid_entries": sum(1 for entries in seeds.values() for seed in entries if seed.valid)}}
    summary = (f"levels={counts['levels']} targets={counts['targets']} loose={counts['loose']}/{counts['scope']} strict_mean={counts['strict_mean']:.3f} "
               f"excluded={counts['excluded']} manual_refs={counts['manual_refs']} unknown_ids={counts['unknown_ids']} aliased={counts['aliased']}")
    return common.Outcome(output, render_markdown(counts, rows, warnings, identifiers, scans, static), summary, problems)

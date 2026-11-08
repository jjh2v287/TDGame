"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py items 로 실행.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py items --out Saved/FluidFX/fxp/items.json --md Saved/FluidFX/fxp/items.md [--root <저장소 사본>].
출력: 넘어온 항목 표(runbook-fxp-v3 3.3, IT 38): items_static.json 항목마다 분류(닫힘·기록·FU-n·FQ-n)·근거 줄 대조·입력 대조(일지 PD-6·$PS fxp_items·pending_judgements·areas.<영역>.records·lines·calib 격차·승인 전용·비용 후보·package-*.json), 측정 불가 분류(비대상·기능 공백·기타), FU·FQ별 연결 항목. 종료 1 = 분류 없음·모르는 FU/FQ·근거 줄 불일치·입력 대조 불일치·FQ 인용 불일치.
상태: 2026-11-07 FX-P X1a(B1) 구현, fixture fixtures_items.py(굽기 3종·인용 변이·package 분기·분류 없음·근거 줄 변이), 2026-11-08 fix3 package 항목 상태 = 실제 package-<tag> 판정(규칙 문안은 status_rule).
"""
import re

import common

CLASS_CLOSED = "닫힘"
CLASS_RECORD = "기록"
BASE_CLASSES = (CLASS_CLOSED, CLASS_RECORD)
FOLLOW_UP_PREFIX = "FU-"
USER_ITEM_PREFIX = "FQ-"
UNMET_PREFIX = "미충족("
PARTIAL_PREFIX = "부분("
UNDECIDABLE_VERDICT = "판정 불가"
PACKAGE_TAG_PREFIX = "package-"


def compile_rules(table):
    rules = table.get("unmeasurable_rules")
    groups = table.get("unmeasurable_groups")
    if not isinstance(rules, list) or not isinstance(groups, dict):
        raise common.InputError(f"{common.ITEMS_STATIC_PATH} unmeasurable_rules 목록·unmeasurable_groups 객체 필요")
    compiled = []
    for rule in rules:
        if not isinstance(rule, dict) or rule.get("group") not in groups or not common.is_text(rule.get("sub")) or not common.is_text(rule.get("pattern")):
            raise common.InputError(f"{common.ITEMS_STATIC_PATH} 규칙 형식(group·sub·pattern): {rule}")
        try:
            compiled.append((rule["group"], rule["sub"], re.compile(rule["pattern"])))
        except re.error as error:
            raise common.InputError(f"{common.ITEMS_STATIC_PATH} 정규식 오류 {rule['pattern']}: {error}")
    return compiled, groups


def classify_unmeasurable(summary, rules, groups, problems):
    entries, counts = [], {group: {"n": 0, "class": groups[group], "subs": {}} for group in groups}
    for entry in (summary.get("fxp_items") or {}).get("unmeasurable") or []:
        if not isinstance(entry, dict):
            raise common.InputError("fxp_items.unmeasurable 항목은 객체")
        key, reason = entry.get("id") or entry.get("row"), entry.get("reason") if isinstance(entry.get("reason"), str) else ""
        match = next(((group, sub) for group, sub, pattern in rules if pattern.search(reason)), None)
        if match is None:
            problems.append(f"측정 불가 분류 없음: {entry.get('area')} {key} '{reason}'")
            entries.append({"area": entry.get("area"), "id": key, "group": None, "sub": None, "class": None})
            continue
        group, sub = match
        counts[group]["n"] += 1
        counts[group]["subs"][sub] = counts[group]["subs"].get(sub, 0) + 1
        entries.append({"area": entry.get("area"), "id": key, "group": group, "sub": sub, "class": groups[group]})
    return {"total": len(entries), "groups": counts, "entries": entries}


def ledger_lines(summary):
    return [(area, line) for area in sorted(summary.get("areas") or {}) for line in (summary["areas"][area] or {}).get("lines") or [] if isinstance(line, dict)]


class ItemFacts:
    def __init__(self, reads, summary, journal, unmeasurable):
        self.reads = reads
        self.summary = summary
        self.journal = journal
        self.unmeasurable = unmeasurable
        self.fxp_items = summary.get("fxp_items") or {}
        self.lines = ledger_lines(summary)

    def area_overalls(self):
        return {area: (entry or {}).get("overall") for area, entry in sorted((self.summary.get("areas") or {}).items())}


def final_summary(record):
    if record is None:
        return None
    return {"status": record["status"], "journal": record["journal"], "batch": record["batch"]}


def check_unmeasurable(check, facts, problems):
    groups = facts.unmeasurable["groups"]
    return {"total": facts.unmeasurable["total"], "groups": {group: {"n": value["n"], "class": value["class"], "subs": value["subs"]} for group, value in groups.items()}}, None


def check_ledger_lines(check, facts, problems):
    found = {}
    for area, line in facts.lines:
        if line.get("id") in check.get("ids", []):
            found.setdefault(line["id"], []).append({"area": area, "class": line.get("class"), "disposition": line.get("disposition")})
    for line_id in check.get("ids", []):
        if line_id not in found:
            problems.append(f"장부 줄 없음: {line_id}")
        elif check.get("class") and any(entry["class"] != check["class"] for entry in found[line_id]):
            problems.append(f"장부 줄 분류 불일치: {line_id} 기대 {check['class']} 실제 {[entry['class'] for entry in found[line_id]]}")
    return {line_id: found.get(line_id, []) for line_id in check.get("ids", [])}, None


def check_unassigned_items(check, facts, problems):
    items = facts.fxp_items.get("unassigned_items") or []
    return {"items": [{"id": item.get("id"), "area": item.get("area"), "disposition": item.get("disposition")} for item in items if isinstance(item, dict)]}, None


def check_package(check, facts, problems):
    verdicts = {}
    for tag in check.get("tags", []):
        path = f"{common.FXP_OUTPUT_DIR}/package-{tag}.json"
        document = facts.reads.read_json(path, required=False)
        if document is None:
            verdicts[tag] = check.get("missing")
            continue
        verdict = document.get("verdict") if isinstance(document, dict) else None
        if not common.is_text(verdict):
            problems.append(f"package 형식: {path} verdict 글자 없음")
        verdicts[tag] = verdict if common.is_text(verdict) else None
    closed = bool(verdicts) and all(verdict == check.get("closed_verdict") for verdict in verdicts.values())
    return {"verdicts": verdicts}, CLASS_CLOSED if closed else CLASS_RECORD


def package_status(verdicts):
    texts = ["없음" if verdict is None else verdict for verdict in verdicts.values()]
    if all(text == texts[0] for text in texts):
        return f"{'·'.join(PACKAGE_TAG_PREFIX + tag for tag in verdicts)} 판정: {texts[0]}"
    return " · ".join(f"{PACKAGE_TAG_PREFIX}{tag} 판정: {text}" for tag, text in zip(verdicts, texts))


def check_fixed_item(check, facts, problems):
    fixed = {item.get("id"): item for item in facts.fxp_items.get("fixed") or [] if isinstance(item, dict)}
    if check.get("id") not in fixed:
        problems.append(f"$PS fxp_items.fixed에 없음: {check.get('id')}")
        return {"id": check.get("id"), "area": None}, None
    item = fixed[check["id"]]
    return {"id": check["id"], "area": item.get("area"), "text": item.get("text")}, None


def check_perf_candidates(check, facts, problems):
    document = facts.reads.read_json(common.PERF_CANDIDATES_PATH)
    items = document.get("items") if isinstance(document, dict) else None
    if not isinstance(items, list):
        raise common.InputError(f"{common.PERF_CANDIDATES_PATH} items 목록 필요")
    return {"n": len(items), "plans": sorted(common.base_plan(item.get("plan")) for item in items if isinstance(item, dict) and common.is_text(item.get("plan")))}, None


def gap_counts(path, key):
    def handler(check, facts, problems):
        document = facts.reads.read_json(path)
        entries = document.get(key) if isinstance(document, dict) else None
        if not isinstance(entries, list):
            raise common.InputError(f"{path} {key} 목록 필요")
        kinds, deferred = {}, 0
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            kinds[str(entry.get("kind"))] = kinds.get(str(entry.get("kind")), 0) + 1
            deferred += any(isinstance(entry.get(field), str) and "deferred" in entry[field] for field in ("destination", "reason"))
        return {"n": len(entries), "kinds": kinds, "deferred": deferred}, None
    return handler


def check_journal_final(check, facts, problems):
    record = facts.journal.final_by_base.get(check.get("plan"))
    if record is None or record["status"] != check.get("status") or check.get("reason_contains", "") not in record["reason"]:
        problems.append(f"일지 최종 불일치: {check.get('plan')} 기대 {check.get('status')}·'{check.get('reason_contains', '')}' 실제 {record['status'] if record else '없음'}")
    return {"plan": check.get("plan"), "final": final_summary(record)}, None


def check_journal_none(check, facts, problems):
    record = facts.journal.final_by_base.get(check.get("plan"))
    if record is not None:
        problems.append(f"일지 최종이 있으면 안 됨: {check.get('plan')} 실제 {record['status']}({record['journal']})")
    return {"plan": check.get("plan"), "final": final_summary(record)}, None


def check_journal_finals(check, facts, problems):
    return {plan: final_summary(facts.journal.final_by_base.get(plan)) for plan in check.get("plans", [])}, None


def check_variant_finals(check, facts, problems):
    suffix = check.get("suffix", "")
    return {stem: final_summary(record) for stem, record in sorted(facts.journal.final_by_stem.items()) if stem.endswith(suffix)}, None


def check_pending_judgements(check, facts, problems):
    pending = facts.summary.get("pending_judgements") or {}
    items = pending.get("items") if isinstance(pending, dict) else None
    if not isinstance(items, list):
        raise common.InputError(f"{common.P_SUMMARY_PATH} pending_judgements.items 목록 필요")
    return {"items": list(items), "source": pending.get("source")}, None


def check_opposite_backend(check, facts, problems):
    items = [item for item in facts.fxp_items.get("opposite_backend") or [] if isinstance(item, dict)]
    return {"n": len(items), "ids": [item.get("id") for item in items]}, None


def check_records(check, facts, problems):
    records = [record for area in sorted(facts.summary.get("areas") or {}) for record in (facts.summary["areas"][area] or {}).get("records") or [] if isinstance(record, dict)]
    return {"total": len(records), "unique": sorted({record.get("id") for record in records if common.is_text(record.get("id"))})}, None


def check_unmet_areas(check, facts, problems):
    overalls = facts.area_overalls()
    return {"unmet": [area for area, overall in overalls.items() if isinstance(overall, str) and overall.startswith(UNMET_PREFIX)],
            "partial": [area for area, overall in overalls.items() if isinstance(overall, str) and overall.startswith(PARTIAL_PREFIX)]}, None


CHECKS = {"unmeasurable": check_unmeasurable, "ledger_lines": check_ledger_lines, "unassigned_items": check_unassigned_items, "package": check_package,
          "fixed_item": check_fixed_item, "perf_candidates": check_perf_candidates, "structural_gaps": gap_counts(common.STRUCTURAL_GAPS_PATH, "gaps"),
          "approval_only": gap_counts(common.APPROVAL_ONLY_PATH, "items"), "journal_final": check_journal_final, "journal_none": check_journal_none,
          "journal_finals": check_journal_finals, "variant_finals": check_variant_finals, "pending_judgements": check_pending_judgements,
          "opposite_backend": check_opposite_backend, "records": check_records, "unmet_areas": check_unmet_areas}


def validate_item(item):
    if not isinstance(item, dict) or not common.is_text(item.get("id")) or not common.is_text(item.get("title")) or not isinstance(item.get("class"), str):
        raise common.InputError(f"{common.ITEMS_STATIC_PATH} 항목 형식(id·title·class): {item}")
    if not isinstance(item.get("evidence"), list) or not item["evidence"] or not isinstance(item.get("refs", []), list):
        raise common.InputError(f"{common.ITEMS_STATIC_PATH} 항목 {item['id']} evidence 목록(1개 이상)·refs 목록 필요")
    for check in item.get("checks", []):
        if not isinstance(check, dict) or check.get("kind") not in CHECKS:
            raise common.InputError(f"{common.ITEMS_STATIC_PATH} 항목 {item['id']} 모르는 check: {check}")


def class_known(value, known_ids):
    return value in BASE_CLASSES or value in known_ids


def resolve_item(item, facts, known_ids, problems):
    evidence = []
    for entry in item["evidence"]:
        ok = isinstance(entry, dict) and common.check_evidence_line(facts.reads, entry)
        if not ok:
            problems.append(f"근거 줄 불일치: 항목 {item['id']} {entry}")
        evidence.append({"file": entry.get("file") if isinstance(entry, dict) else None, "line": entry.get("line") if isinstance(entry, dict) else None,
                         "expect": entry.get("expect") if isinstance(entry, dict) else None, "ok": ok})
    item_class, detail = item["class"], {}
    for check in item.get("checks", []):
        result, override = CHECKS[check["kind"]](check, facts, problems)
        detail[check["kind"]] = result
        item_class = override or item_class
    if not class_known(item_class, known_ids):
        problems.append(f"분류 없음: 항목 {item['id']} class '{item_class}'")
    for ref in item.get("refs", []):
        if ref not in known_ids:
            problems.append(f"모르는 관련 ID: 항목 {item['id']} {ref}")
    record = {"id": item["id"], "title": item["title"], "class": item_class, "refs": list(item.get("refs", [])), "status": item.get("status"),
              "closed_by": item.get("closed_by"), "evidence": evidence, "detail": detail}
    verdicts = detail.get("package", {}).get("verdicts")
    if verdicts:
        record.update(status=package_status(verdicts), status_rule=item.get("status"))
    return record


def link_items(entries, items):
    return [dict(entry, linked_items=[item["id"] for item in items if item["class"] == entry["id"] or entry["id"] in item["refs"]]) for entry in entries]


def class_counts(items):
    counts = {"closed": 0, "record": 0, "follow_up": 0, "user": 0, "unknown": 0}
    for item in items:
        if item["class"] == CLASS_CLOSED:
            counts["closed"] += 1
        elif item["class"] == CLASS_RECORD:
            counts["record"] += 1
        elif item["class"].startswith(FOLLOW_UP_PREFIX):
            counts["follow_up"] += 1
        elif item["class"].startswith(USER_ITEM_PREFIX):
            counts["user"] += 1
        else:
            counts["unknown"] += 1
    return counts


def undecidable_rows(summary):
    return [row.get("row") for area in sorted(summary.get("areas") or {}) for row in (summary["areas"][area] or {}).get("rows") or []
            if isinstance(row, dict) and row.get("verdict") == UNDECIDABLE_VERDICT]


def detail_text(item):
    parts = []
    for kind, value in sorted(item["detail"].items()):
        if kind == "package":
            parts.append("package " + " · ".join(f"{tag} {verdict}" for tag, verdict in sorted(value["verdicts"].items())))
        elif kind in ("journal_final", "journal_none"):
            parts.append(f"{value['plan']} 일지 최종 {value['final']['status'] if value['final'] else '없음'}")
        elif kind == "journal_finals" or kind == "variant_finals":
            parts.append(" · ".join(f"{plan} {final['status'] if final else '없음'}" for plan, final in sorted(value.items())))
        elif kind == "ledger_lines":
            parts.append(" · ".join(f"{line_id} {'/'.join(entry['class'] or '—' for entry in entries) or '없음'}" for line_id, entries in sorted(value.items())))
        elif kind == "unmeasurable":
            parts.append(" · ".join(f"{group} {info['n']}({info['class']})" for group, info in value["groups"].items()))
        elif kind in ("structural_gaps", "approval_only", "perf_candidates", "opposite_backend"):
            parts.append(f"{kind} {value['n']}" + (f"(deferred {value['deferred']})" if "deferred" in value else ""))
        elif kind == "records":
            parts.append(f"기록 {value['total']}줄 → 고유 {len(value['unique'])}")
        elif kind == "pending_judgements":
            parts.append(f"판정 대기 {len(value['items'])}")
        elif kind == "fixed_item":
            parts.append(f"$PS fixed {value['id']}({value['area']})")
        elif kind == "unassigned_items":
            parts.append("영역 밖 " + "·".join(str(entry["id"]) for entry in value["items"]))
        elif kind == "unmet_areas":
            parts.append(f"미충족 {len(value['unmet'])} · 부분 {len(value['partial'])}")
    return " / ".join(parts)


def render_markdown(items, unmeasurable, follow_ups, user_items, ledger, derived, static):
    cell = common.markdown_cell
    lines = ["# FX-P 넘어온 항목 표 (runbook-fxp-v3 3.3, IT 38)", "", f"- 환경 편차(4.5): {static.get('environment_note')}", "",
             "| 항목 | 이름 | 분류 | 관련 | 상태 | 입력 대조 | 근거 줄 |", "|---|---|---|---|---|---|---|"]
    for item in items:
        evidence = " · ".join(f"{entry['file']}:{entry['line']}{'' if entry['ok'] else '(불일치)'}" for entry in item["evidence"])
        lines.append(f"| {item['id']} | {cell(item['title'])} | {cell(item['class'])} | {cell('·'.join(item['refs']) or None)} | {cell(item['status'])} | {cell(detail_text(item) or None)} | {cell(evidence)} |")
    lines += ["", f"## 측정 불가 분류($PS fxp_items.unmeasurable {unmeasurable['total']})", "", "| 묶음 | 세부 | 수 | 분류 |", "|---|---|---|---|"]
    for group, info in unmeasurable["groups"].items():
        lines += [f"| {group} | {cell(sub)} | {count} | {info['class']} |" for sub, count in info["subs"].items()]
        lines.append(f"| {group} 합 | — | {info['n']} | {info['class']} |")
    lines += ["", "## 후속 후보 FU(5절)", "", "| FU | 항목 | 원천 | 연결 항목 |", "|---|---|---|---|"]
    lines += [f"| {entry['id']} | {cell(entry.get('item'))} | {cell(entry.get('source'))} | {cell('·'.join(entry['linked_items']) or None)} |" for entry in follow_ups]
    lines += ["", "## 사용자 대기 FQ(6절)", "", "| FQ | 묻는 것 | 근거 | 기록만 | 연결 항목 |", "|---|---|---|---|---|"]
    lines += [f"| {entry['id']} | {cell(entry.get('ask'))} | {cell(entry.get('source'))} | {'예' if entry.get('record_only') else '아니오'} | {cell('·'.join(entry['linked_items']) or None)} |"
              for entry in user_items]
    lines += ["", "## 장부·파생 수", "", f"- $PS areas.<영역>.lines {ledger['lines_total']}줄 → 고유 ID {ledger['lines_unique']}",
              f"- $PS areas.<영역>.records {ledger['records_total']}줄 → 고유 ID {len(ledger['records_unique'])}({'·'.join(ledger['records_unique'])})",
              f"- 판정 불가 행 {len(derived['undecidable_rows'])}({'·'.join(derived['undecidable_rows'])})"]
    return lines


def build(context):
    reads, problems = context.reads, []
    static = common.load_fxp_static(reads)
    problems += common.check_static_quotes(reads, static)
    table = reads.read_json(common.ITEMS_STATIC_PATH)
    if not isinstance(table, dict) or not isinstance(table.get("items"), list):
        raise common.InputError(f"{common.ITEMS_STATIC_PATH} items 목록 필요")
    summary = reads.read_json(common.P_SUMMARY_PATH)
    if not isinstance(summary, dict) or not isinstance(summary.get("areas"), dict):
        raise common.InputError(f"{common.P_SUMMARY_PATH} areas 객체 필요")
    rules, groups = compile_rules(table)
    for item in table["items"]:
        validate_item(item)
    identifiers = [item["id"] for item in table["items"]]
    if len(identifiers) != len(set(identifiers)):
        raise common.InputError(f"{common.ITEMS_STATIC_PATH} 항목 id 중복")
    unmeasurable = classify_unmeasurable(summary, rules, groups, problems)
    facts = ItemFacts(reads, summary, common.merge_journals(reads), unmeasurable)
    known_ids = common.static_ids(static)
    items = [resolve_item(item, facts, known_ids, problems) for item in table["items"]]
    follow_ups = link_items(static["follow_ups"], items)
    user_items = link_items(static["user_items"], items)
    records = check_records({}, facts, problems)[0]
    ledger = {"lines_total": len(facts.lines), "lines_unique": len({line.get("id") for _, line in facts.lines}), "records_total": records["total"], "records_unique": records["unique"]}
    derived = {"undecidable_rows": undecidable_rows(summary), "unmet_areas": check_unmet_areas({}, facts, problems)[0]}
    counts = class_counts(items)
    document = {"version": 1, "items": items, "unmeasurable": unmeasurable, "follow_ups": follow_ups, "user_items": user_items, "ledger": ledger, "derived": derived,
                "class_counts": counts, "journal_order": facts.journal.order()}
    groups_count = unmeasurable["groups"]
    summary_line = (f"items={len(items)} closed={counts['closed']} record={counts['record']} follow_up={counts['follow_up']} user={counts['user']} "
                    f"unmeasurable={unmeasurable['total']} non_target={groups_count.get('비대상', {}).get('n', 0)} feature_gap={groups_count.get('기능 공백', {}).get('n', 0)} "
                    f"other={groups_count.get('기타', {}).get('n', 0)} lines_unique={ledger['lines_unique']} records_unique={len(ledger['records_unique'])}")
    return common.Outcome(document, render_markdown(items, unmeasurable, follow_ups, user_items, ledger, derived, static), summary_line, problems)

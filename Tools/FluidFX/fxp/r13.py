"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py r13 으로 실행.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py r13 --out Saved/FluidFX/fxp/r13.json --md Saved/FluidFX/fxp/r13.md [--root <저장소 사본>].
출력: R13 감사 표(runbook-fxp-v3 3.6·4.3, PD-12b) 5줄: r13_static.json 행마다 P 요약 lines 처분·rows 값(입자 1만 IN1 옮기기만)·인용 {file, line, expect} 확인, 표기 셋(P 판정 인용·구조 격차·무대 없음), LIVE-2 열 고정. 새 측정·한도 판정 없음. 종료 1 = 인용 없음·불일치·인용에 없는 값·장면, 항목·행 근거 부족, 무대 없음 근거 아님, LIVE-2 수치 꼴(D-49).
상태: 2026-11-07 FX-P X4(B2) 구현, fixture fixtures_r13.py(정상·같은 바이트·인용 변이·인용 없음·값·장면·항목·무대·행·입력 없음).
"""
import re

import common

STATIC_PATH = "Tools/FluidFX/fxp/r13_static.json"
KIND_P_QUOTE, KIND_STRUCTURAL, KIND_NO_STAGE = "p_quote", "structural", "no_stage"
KINDS = (KIND_P_QUOTE, KIND_STRUCTURAL, KIND_NO_STAGE)
QUOTED_KINDS = (KIND_P_QUOTE, KIND_STRUCTURAL)
NO_STAGE_LABEL = "무대 없음(R13s 대기)"
STRUCTURAL_LABEL = "구조 격차(기존 측정 인용·보정 전, FX-7p {scenes})"
P_QUOTE_LABEL = "P 판정 인용({states})"
LIVE2_LABEL = "스트레스 정량 비공개(PF:206)"
NO_STAGE_DISPOSITION = "측정 불가"
NO_STAGE_REASON_TEXT = "무대"
LIVE2_NUMBER_PATTERN = re.compile(r"LIVE-2[^|\n]{0,40}\d+(\.\d+)?\s*(ms|fps|FPS|%)")
RULE = "4.3: 표기 셋 — P 판정 인용(<상태>)·구조 격차(기존 측정 인용·보정 전, FX-7p <장면>)·무대 없음(R13s 대기), LIVE-2 열 스트레스 정량 비공개(PF:206). 값은 P 요약·인용 줄에서 옮기기만, 새 측정·한도 판정 없음."


def read_static(reads):
    static = reads.read_json(STATIC_PATH)
    rows = static.get("rows") if isinstance(static, dict) else None
    if not isinstance(rows, list) or not isinstance(static.get("live2"), dict):
        raise common.InputError(f"{STATIC_PATH}: rows 목록·live2 객체 필요")
    for index, row in enumerate(rows):
        if not isinstance(row, dict) or not common.is_text(row.get("id")) or not common.is_text(row.get("name")) or row.get("kind") not in KINDS:
            raise common.InputError(f"{STATIC_PATH}: rows[{index}] id·name·kind({'|'.join(KINDS)}) 필요")
        for key in ("items", "p_rows", "values", "scenes"):
            if key in row and not isinstance(row[key], list):
                raise common.InputError(f"{STATIC_PATH}: rows[{index}].{key} 목록 필요")
    return static


def check_quotes(reads, owner, quotes, problems):
    if not isinstance(quotes, list) or not quotes:
        problems.append(f"인용 없음: {owner} quotes{{file, line, expect}} 필요")
        return []
    checked = []
    for index, quote in enumerate(quotes):
        if not isinstance(quote, dict) or not common.is_text(quote.get("file")) or not isinstance(quote.get("line"), int) or not common.is_text(quote.get("expect")):
            problems.append(f"인용 없음: {owner} quotes[{index}] file·line·expect 필요")
            checked.append({"file": None, "line": None, "expect": None, "ok": False})
            continue
        ok = common.check_evidence_line(reads, quote)
        if not ok:
            problems.append(f"인용 불일치: {owner} '{quote['expect']}'가 {quote['file']}:{quote['line']}의 부분 문자열이 아님")
        checked.append({"file": quote["file"], "line": quote["line"], "expect": quote["expect"], "ok": ok})
    return checked


def summary_index(summary):
    lines, rows = {}, {}
    for area, record in sorted(summary["areas"].items()):
        if not isinstance(record, dict):
            continue
        for line in record.get("lines") or []:
            if isinstance(line, dict) and common.is_text(line.get("id")):
                lines.setdefault(line["id"], dict(line, summary_area=area))
        for row in record.get("rows") or []:
            if isinstance(row, dict) and common.is_text(row.get("row")):
                rows.setdefault(row["row"], dict(row, summary_area=area))
    return lines, rows


def item_records(row, lines, problems):
    records = []
    for item_id in row.get("items") or []:
        line = lines.get(item_id)
        if line is None:
            problems.append(f"근거 부족: {row['name']} 항목 {item_id}가 P 요약 lines에 없음")
            continue
        records.append({"id": item_id, "area": line["summary_area"], "source": line.get("source"), "limit": line.get("limit"), "disposition": line.get("disposition"),
                        "reason": line.get("reason"), "rows": [value for value in line.get("rows") or [] if common.is_text(value)]})
    if not row.get("items"):
        problems.append(f"근거 부족: {row['name']} items 비어 있음")
    return records


def check_no_stage(row, items, problems):
    for item in items:
        if item["disposition"] != NO_STAGE_DISPOSITION or NO_STAGE_REASON_TEXT not in str(item["reason"] or ""):
            problems.append(f"무대 없음 근거 아님: {row['name']} {item['id']} 처분 {item['disposition']}·사유 {item['reason']}")


def p_row_records(row, items, rows, lines, problems):
    records = []
    if not row.get("p_rows"):
        problems.append(f"근거 부족: {row['name']} p_rows 비어 있음(P 판정 인용)")
    linked = {value for item in items for value in item["rows"]}
    for name in row.get("p_rows") or []:
        found = rows.get(name)
        if found is None:
            problems.append(f"근거 부족: {row['name']} P 행 {name}가 P 요약 rows에 없음")
            continue
        if name not in linked:
            problems.append(f"근거 부족: {row['name']} P 행 {name}가 항목 {'·'.join(item['id'] for item in items)}의 rows와 안 이어짐")
        d = found.get("d") if isinstance(found.get("d"), dict) else {}
        interval = found.get("interval") if isinstance(found.get("interval"), dict) else {}
        p95 = found.get("p95_record") if isinstance(found.get("p95_record"), dict) else {}
        limit_items = [{"id": item_id, "limit": (lines.get(item_id) or {}).get("limit")} for item_id in found.get("items") or []]
        records.append({"row": name, "area": found["summary_area"], "label": found.get("label"), "d_median": d.get("median"), "interval_lower": interval.get("lower"),
                        "interval_upper": interval.get("upper"), "limit_ms": found.get("limit_ms"), "p95_record_median": p95.get("median"), "limit_items": limit_items})
    return records


def check_values(row, quotes, problems):
    checked = []
    for index, value in enumerate(row.get("values") or []):
        quote_index = value.get("quote") if isinstance(value, dict) else None
        text = value.get("value") if isinstance(value, dict) else None
        quote = quotes[quote_index] if isinstance(quote_index, int) and 0 <= quote_index < len(quotes) else None
        ok = common.is_text(text) and quote is not None and quote["ok"] and text in quote["expect"]
        if not ok:
            problems.append(f"인용에 없는 값: {row['name']} values[{index}] {text!r}(인용 {quote_index})")
        checked.append({"name": value.get("name") if isinstance(value, dict) else None, "value": text, "quote": quote_index, "ok": ok})
    return checked


def check_scenes(row, quotes, problems):
    scenes = [scene for scene in row.get("scenes") or [] if common.is_text(scene)]
    if not scenes:
        problems.append(f"근거 부족: {row['name']} FX-7p 장면 없음(구조 격차)")
    for scene in scenes:
        pattern = re.compile(rf"(?<![A-Za-z0-9]){re.escape(scene)}(?![0-9])")
        if not any(quote["ok"] and pattern.search(quote["expect"]) for quote in quotes):
            problems.append(f"인용에 없는 장면: {row['name']} {scene}")
    return scenes


def row_label(row, p_rows, scenes):
    if row["kind"] == KIND_P_QUOTE:
        return P_QUOTE_LABEL.format(states=" · ".join(f"{record['row']} {record['label']}" for record in p_rows) or "—")
    if row["kind"] == KIND_STRUCTURAL:
        return STRUCTURAL_LABEL.format(scenes="·".join(scenes) or "—")
    return NO_STAGE_LABEL


def build_row(reads, row, lines, rows, problems):
    quotes = check_quotes(reads, row["name"], row.get("quotes"), problems)
    items = item_records(row, lines, problems)
    p_rows, scenes = [], []
    if row["kind"] == KIND_P_QUOTE:
        p_rows = p_row_records(row, items, rows, lines, problems)
    elif row["kind"] == KIND_STRUCTURAL:
        scenes = check_scenes(row, quotes, problems)
    else:
        check_no_stage(row, items, problems)
    values = check_values(row, quotes, problems)
    return {"id": row["id"], "name": row["name"], "kind": row["kind"], "label": row_label(row, p_rows, scenes), "basis": row.get("basis"), "source": row.get("source"),
            "items": items, "p_rows": p_rows, "scenes": scenes, "quotes": quotes, "values": values, "live2": LIVE2_LABEL}


def number_text(value):
    return "—" if value is None else repr(value)


def value_source_text(record):
    if record["p_rows"]:
        parts = []
        for p_row in record["p_rows"]:
            limits = " + ".join(f"{item['id']} {item['limit']}" for item in p_row["limit_items"])
            parts.append(f"{p_row['row']} d {number_text(p_row['d_median'])} ({number_text(p_row['interval_lower'])}~{number_text(p_row['interval_upper'])}) · 한도 "
                         f"{number_text(p_row['limit_ms'])}({limits})")
        return " / ".join(parts)
    if record["values"]:
        return " · ".join(f"{value['name']} {value['value']}" for value in record["values"])
    return "—"


def render_markdown(records, live2_quotes, static_rule):
    cell = common.markdown_cell
    lines = ["# FX-P R13 감사 (PD-12b, runbook-fxp-v3 3.6·4.3)", "", f"- {RULE}", f"- 정적 표: {STATIC_PATH} — {cell(static_rule)}",
             "- LIVE-2 열 인용: " + " · ".join(f"{quote['file']}:{quote['line']} {'일치' if quote['ok'] else '불일치'}" for quote in live2_quotes), "",
             "| 종 | 근거 | 값 원천 | 표기 | LIVE-2 | P 장부 처분 | 인용(파일:줄) |", "|---|---|---|---|---|---|---|"]
    for record in records:
        dispositions = " · ".join(f"{item['id']} {item['disposition']}({item['reason'] or '—'})" for item in record["items"]) or "—"
        quotes = " · ".join(f"{quote['file']}:{quote['line']} '{quote['expect']}' {'일치' if quote['ok'] else '불일치'}" for quote in record["quotes"]) or "없음"
        lines.append(f"| {record['name']} | {cell(record['basis'])} | {cell(value_source_text(record))} | {cell(record['label'])} | {record['live2']} | {cell(dispositions)} | {cell(quotes)} |")
    return lines


def build(context):
    reads, problems = context.reads, []
    summary = reads.read_json(common.P_SUMMARY_PATH)
    if not isinstance(summary, dict) or not isinstance(summary.get("areas"), dict):
        raise common.InputError(f"{common.P_SUMMARY_PATH} areas 객체 필요")
    static = read_static(reads)
    lines, rows = summary_index(summary)
    live2_quotes = check_quotes(reads, "LIVE-2 열", static["live2"].get("quotes"), problems)
    records = [build_row(reads, row, lines, rows, problems) for row in static["rows"]]
    document = {"version": 1, "rows": records, "live2": {"label": LIVE2_LABEL, "quotes": live2_quotes}, "rule": RULE, "static_rule": static.get("rule")}
    markdown = render_markdown(records, live2_quotes, static.get("rule"))
    for text in ["\n".join(markdown), common.json_bytes(document).decode("utf-8")]:
        match = LIVE2_NUMBER_PATTERN.search(text)
        if match:
            problems.append(f"LIVE-2 수치 꼴(D-49): '{match.group(0)}'")
            break
    quotes = [quote for record in records for quote in record["quotes"]] + live2_quotes
    summary_text = (f"rows={len(records)} quoted={sum(1 for record in records if record['kind'] in QUOTED_KINDS)} "
                    f"waiting={sum(1 for record in records if record['kind'] == KIND_NO_STAGE)} quotes={len(quotes)} quotes_ok={sum(1 for quote in quotes if quote['ok'])} "
                    f"items={sum(len(record['items']) for record in records)}")
    return common.Outcome(document, markdown, summary_text, problems)

"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py compare 로 실행.
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py compare --out Saved/FluidFX/fxp/compare.json --md Saved/FluidFX/fxp/compare.md [--root <저장소 사본>].
출력: 같은 기계 비교표(runbook-fxp-v3 4.2·PD-11): P 요약 areas.<영역>.rows 값을 옮기기만(d 중앙·구간, d′, p95 기록, 한도, P 판정), 한도 원천 = p_areas.json items[].source, LIVE-2 같은 기계 'P-4 미답', LIVE-2 공개·환산은 PF 줄 참조만, 표기 고정. LIVE-2 대비 판정 경로 없음. 종료 1 = 한도 원천 없음·한도 없음·P 요약의 p_areas sha 불일치·LIVE-2 수치 꼴(D-49).
상태: 2026-11-07 FX-P X1b(B2) 구현, fixture fixtures_compare.py(정상·같은 바이트·한도 원천 없음 둘·sha 불일치·D-49·입력 없음).
"""
import re

import common

P_AREAS_PATH = "Tools/FluidFX/perf/p/p_areas.json"
SAME_MACHINE_LABEL = "P-4 미답"
NOTATION = "PF 환산 ×2.3~3·상한 비교(증명 아님)·제안 예산 대비·상한 꺼짐(P-22)"
PF_PREFIX = "PF"
PF_CONVERSION_REFERENCE = "PF:75 환산 배율"
PF_REFERENCE_SUFFIX = "'LIVE-2 기준' 열"
NO_PF_REFERENCE = "PF 줄 없음(한도 원천이 PF 밖)"
SOURCE_TOKEN_PATTERN = re.compile(r"([A-Za-z][A-Za-z0-9]*):(\d+)")
CONTINUATION_PATTERN = re.compile(r"(\d+)")
LIVE2_NUMBER_PATTERN = re.compile(r"LIVE-2[^|\n]{0,40}\d+(\.\d+)?\s*(ms|fps|FPS|%)")
LABEL_GROUPS = ("통과", "미달", "판정 불가")
COLUMNS = ("영역", "행", "지표", "d 중앙(구간)", "d′", "p95 기록", "한도", "한도 원천(S###:줄)", "제안 예산 대비(P 판정)", "LIVE-2 같은 기계", "LIVE-2 공개·환산(PF 줄)", "표기")
RULE = ("값은 P 요약(Saved/AgentOps/20261106/p/p-summary.json areas.<영역>.rows)에서 옮기기만 한다. LIVE-2 같은 기계 실측 없음(P-4 미답)이라 LIVE-2 대비 판정 경로가 없고, "
        "공개·환산 열은 한도 원천의 PF 줄('LIVE-2 기준' 열)과 PF:75 환산 배율 참조만 둔다(D-49).")


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def number_text(value):
    return "—" if not is_number(value) else repr(value)


def interval_part(interval):
    if not isinstance(interval, dict):
        return None
    return {"lower": interval.get("lower"), "upper": interval.get("upper"), "state": interval.get("state")}


def statistic(row, key):
    value = row.get(key)
    if not isinstance(value, dict):
        return None
    return {"median": value.get("median"), "interval": interval_part(value.get("interval"))}


def statistic_text(median, interval):
    if not is_number(median):
        return "—"
    if not interval:
        return number_text(median)
    return f"{number_text(median)} ({number_text(interval['lower'])}~{number_text(interval['upper'])}, {interval['state']})"


def pf_lines(sources):
    lines = set()
    for source in sources:
        prefix = None
        for token in (part.strip() for part in source["source"].split("·")):
            match = SOURCE_TOKEN_PATTERN.match(token)
            continuation = CONTINUATION_PATTERN.match(token)
            if match:
                prefix, number = match.group(1), int(match.group(2))
            elif continuation and prefix:
                number = int(continuation.group(1))
            else:
                prefix = None
                continue
            if prefix == PF_PREFIX:
                lines.add(number)
    return sorted(lines)


def public_reference(lines):
    if not lines:
        return NO_PF_REFERENCE
    return f"{' · '.join(f'PF:{line}' for line in lines)} {PF_REFERENCE_SUFFIX} · {PF_CONVERSION_REFERENCE}"


def read_items(reads):
    document = reads.read_json(P_AREAS_PATH)
    items = document.get("items") if isinstance(document, dict) else None
    if not isinstance(items, list):
        raise common.InputError(f"{P_AREAS_PATH}: items 목록 필요")
    return {item["id"]: item for item in items if isinstance(item, dict) and common.is_text(item.get("id"))}


def check_p_areas_sha(summary, reads, problems):
    recorded = ((summary.get("inputs") or {}).get("p_areas") or {}).get("sha256") if isinstance(summary.get("inputs"), dict) else None
    actual = reads.snapshot().get(P_AREAS_PATH)
    if recorded != actual:
        problems.append(f"한도 원천 입력 불일치: P 요약 inputs.p_areas.sha256 {recorded} ≠ 현재 {P_AREAS_PATH} {actual}")
    return {"recorded": recorded, "actual": actual, "matches": recorded == actual}


def limit_sources(area, row, items, problems):
    item_ids = row.get("items") if isinstance(row.get("items"), list) else []
    if not item_ids:
        problems.append(f"한도 원천 없음: {area} {row['row']} items 비어 있음")
    sources = []
    for item_id in item_ids:
        item = items.get(item_id)
        source = item.get("source") if item else None
        if not common.is_text(source):
            problems.append(f"한도 원천 없음: {area} {row['row']} {item_id}({P_AREAS_PATH} items에 {'없음' if item is None else 'source 없음'})")
            continue
        sources.append({"item": item_id, "source": source})
    return sources


def row_record(area, row, items, problems):
    if not isinstance(row, dict) or not common.is_text(row.get("row")) or not common.is_text(row.get("metric")):
        raise common.InputError(f"P 요약 areas.{area}.rows 항목에 row·metric 필요")
    sources = limit_sources(area, row, items, problems)
    if not is_number(row.get("limit_ms")):
        problems.append(f"한도 없음: {area} {row['row']} limit_ms {row.get('limit_ms')!r}")
    d = {"median": (row.get("d") or {}).get("median") if isinstance(row.get("d"), dict) else None, "interval": interval_part(row.get("interval"))}
    p95 = row.get("p95_record") if isinstance(row.get("p95_record"), dict) else {}
    lines = pf_lines(sources)
    return {"area": area, "row": row["row"], "scene": row.get("scene"), "metric": row["metric"], "d": d, "d_prime": statistic(row, "d_prime"),
            "p95_record_median": p95.get("median"), "limit_ms": row.get("limit_ms"), "limit_sources": sources, "p_label": row.get("label"),
            "p_verdict": row.get("verdict"), "p_rule": row.get("rule"), "derived_from": row.get("from"), "live2_same_machine": SAME_MACHINE_LABEL,
            "live2_public_pf_lines": lines, "live2_public": public_reference(lines), "notation": NOTATION}


def p_label_text(record):
    label = common.markdown_cell(record["p_label"])
    if record["p_rule"] and record["p_rule"] != "judge.json":
        return f"{label}({common.markdown_cell(record['p_rule'])})"
    return label


def render_markdown(records, sha_check):
    cell = common.markdown_cell
    lines = ["# FX-P 같은 기계 비교표 (PD-11, runbook-fxp-v3 4.2)", "", f"- {RULE}", f"- 표기(모든 행): {NOTATION}",
             f"- 한도 원천 = {P_AREAS_PATH} items[].source(행 items 순서), P 요약 기록 sha {'일치' if sha_check['matches'] else '불일치'}",
             "- d 중앙(구간) = 효과 차분 중앙값(순서통계 구간, 상태), d′ = 화소 환산 중앙값(구간, 상태), p95 = 기록만(판정 안 함)", "",
             "| " + " | ".join(COLUMNS) + " |", "|" + "---|" * len(COLUMNS)]
    for record in records:
        d_prime = record["d_prime"] or {}
        sources = "; ".join(f"{source['item']}:{source['source']}" for source in record["limit_sources"]) or None
        values = [record["area"], record["row"], record["metric"], statistic_text(record["d"]["median"], record["d"]["interval"]),
                  statistic_text(d_prime.get("median"), d_prime.get("interval")), number_text(record["p95_record_median"]), number_text(record["limit_ms"]),
                  cell(sources), p_label_text(record), record["live2_same_machine"], record["live2_public"], record["notation"]]
        lines.append("| " + " | ".join(values) + " |")
    return lines


def label_counts(records):
    counts = {group: 0 for group in LABEL_GROUPS}
    counts["그 밖"] = 0
    for record in records:
        head = str(record["p_label"] or "").split("(", 1)[0].strip()
        counts[head if head in counts else "그 밖"] += 1
    return counts


def build(context):
    reads, problems = context.reads, []
    summary = reads.read_json(common.P_SUMMARY_PATH)
    if not isinstance(summary, dict) or not isinstance(summary.get("areas"), dict):
        raise common.InputError(f"{common.P_SUMMARY_PATH} areas 객체 필요")
    order = summary.get("areas_order") if isinstance(summary.get("areas_order"), list) else sorted(summary["areas"])
    items = read_items(reads)
    sha_check = check_p_areas_sha(summary, reads, problems)
    records = []
    for area in order:
        rows = (summary["areas"].get(area) or {}).get("rows") if isinstance(summary["areas"].get(area), dict) else None
        if not isinstance(rows, list):
            raise common.InputError(f"{common.P_SUMMARY_PATH} areas.{area}.rows 목록 필요")
        records += [row_record(area, row, items, problems) for row in rows]
    counts = label_counts(records)
    document = {"version": 1, "areas_order": list(order), "rows": records, "columns": list(COLUMNS), "label_counts": counts, "p_areas_sha": sha_check,
                "rule": {"copy": RULE, "notation": NOTATION, "same_machine": SAME_MACHINE_LABEL, "public": f"한도 원천 PF 줄 → '{PF_REFERENCE_SUFFIX}' · {PF_CONVERSION_REFERENCE}, 없으면 '{NO_PF_REFERENCE}'"}}
    markdown = render_markdown(records, sha_check)
    for text in ["\n".join(markdown), common.json_bytes(document).decode("utf-8")]:
        if LIVE2_NUMBER_PATTERN.search(text):
            problems.append(f"LIVE-2 수치 꼴(D-49): '{LIVE2_NUMBER_PATTERN.search(text).group(0)}'")
            break
    pf_rows = sum(1 for record in records if record["live2_public_pf_lines"])
    summary_text = (f"rows={len(records)} areas_with_rows={len({record['area'] for record in records})} pass={counts['통과']} fail={counts['미달']} "
                    f"undetermined={counts['판정 불가']} other={counts['그 밖']} pf_reference_rows={pf_rows} no_pf_rows={len(records) - pf_rows} "
                    f"p_areas_sha_matches={sha_check['matches']}")
    return common.Outcome(document, markdown, summary_text, problems)

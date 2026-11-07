"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, 에디터·빌드 불필요), fxp_audit.py areas 로 실행(p_plans.py import만, PD-8).
실행 명령: python -B Tools/FluidFX/fxp/fxp_audit.py areas --out Saved/FluidFX/fxp/areas.json --md Saved/FluidFX/fxp/areas.md [--root <저장소 사본>].
출력: 영역별 상태 표(PB2:214, runbook-fxp-v3 3.2): 계획당 한 칸(우선순위 12단계), 채택(고유)·영향·검증만·둔감·채택 없음·conflict·채택 뒤 제거·deferred(계획·항목)·그 밖, 성능 판정·막는 측정 불가·PR-11 귀결(4.7)·승인(A19)·basis 분포·동등 표기(4.1), plans{계획: 칸·판정 파일}. 종료 1 = 분류 없는 계획(= 검산 불일치: 칸 합 = 고유 기본 계획 − 분류 없음이라 따로 세지 않고 check.ok·md 검산 절로만 보인다)·영역 없는 계획·채택 계획 모름·verdict/expect 채택 수 불일치·성능 형식 모름.
상태: 2026-11-07 FX-P X1a(B1) 구현, fix1(빌더 A) 혼자 발화 못 하는 검산 불일치 problems 줄 삭제, fixture fixtures_areas.py(f5·R10>R9·보류 최솟값·승인 sha·접미사·conflict(일지·overlay)·헤드리스·run1 둔감·우선순위 충돌·deferred 중복 제거·basis 섞임·목표 0·input_).
"""
import common

CELL_ORDER = ("adopted", "influence", "verify_only", "conflict", "removed_after_adoption", "deferred", "insensitive", "no_adopt", "undetermined", "failed",
              "blocked_not_run", "retired", "unclassified")
CELL_LABELS = {"adopted": "① 번호 채택(expect adoptions)", "influence": "② 영향(expect influences, 채택 아님)", "verify_only": "③ 검증만(큐 class verify)",
               "conflict": "④ conflict(이유 접두 conflict:)", "removed_after_adoption": "⑤ 채택 뒤 제거(일지 adopted + overlay 마지막 remove, 헤드리스 되돌림)",
               "deferred": "⑥ deferred(일지 최종 deferred)", "insensitive": "⑦ 둔감(insensitive, not_run 'run1 둔감')", "no_adopt": "⑧ 채택 없음(no_adopt)",
               "undetermined": "⑨ 판정 불가(undetermined)", "failed": "⑩ 실패(failed)", "blocked_not_run": "⑪ 미실행(blocked·not_run)", "retired": "⑫ 폐기(retired)",
               "unclassified": "분류 없음(검산 실패)"}
CELL_SHORT = {"undetermined": "판정 불가", "failed": "실패", "blocked_not_run": "미실행", "retired": "폐기", "unclassified": "분류 없음"}
OTHER_CELLS = ("undetermined", "failed", "blocked_not_run", "retired", "unclassified")
INSENSITIVE_NOT_RUN_PREFIX = "run1 둔감"
CONFLICT_PREFIX = "conflict:"
HEADLESS_REVERT_TEXT = "헤드리스"
DEFERRED_TEXT = "deferred"
STATUS_TO_CELL = {"deferred": "deferred", "insensitive": "insensitive", "no_adopt": "no_adopt", "undetermined": "undetermined", "failed": "failed"}
EQUIVALENT_LABEL = "동등 확인(성능은 [제안] 예산 기준, 동일 이상 아님)"
APPROVED_PREFIX = "승인 "
REJECTED_PREFIX = "거부"
PERFORMANCE_UNMET_PREFIX = "미충족("
PERFORMANCE_PARTIAL_PREFIX = "부분("
PR11_UNMET = "FX-P 성능 미달"
PR11_PARTIAL_PREFIX = "FX-P 성능 부분("
PR11_PASS = "화면 승인만"
PR11_NO_INPUT = "판정 불가(성능 입력 없음)"
PR11_UNKNOWN = "판정 불가(성능 형식 모름)"
BASIS_KEYS = ("live2_image", "spec_proposal", "free_text", "unmarked")
JOURNAL_MERGE_RULE = "일지 합침 PD-6: 일지 순서 = holds[].acquired_at 최솟값 오름차순(같으면 배치 이름 자연수 정렬, 보류 없는 일지는 뒤), pending 무시, 계획(base_plan_id)별 마지막 상태"
EQUIVALENCE_RULE = "4.1: 동등 확인 ⟺ 승인(A19) ∧ 성능 충족(proposed) ∧ 영역 목표 basis 전부 live2_image(목표 0 = 확인 불가)"


def text_list(values):
    return [value for value in values if common.is_text(value)]


def queue_plan_entries(document, path):
    if not isinstance(document, dict):
        raise common.InputError(f"큐 형식(객체 필요): {path}")
    for key in ("blocks", "not_run", "blocked", "deferred"):
        if key in document and not isinstance(document[key], list):
            raise common.InputError(f"큐 {key} 목록 필요: {path}")
    for block in document.get("blocks") or []:
        if not isinstance(block, dict) or not isinstance(block.get("plans"), list):
            raise common.InputError(f"큐 blocks[].plans 목록 필요: {path}")
    return document


def read_queues(reads):
    paths = [common.MAIN_QUEUE_PATH] + [path for path in reads.glob(common.p_plans().PB2_FXP_SOURCE_GLOBS[1]) if path != common.MAIN_QUEUE_PATH]
    facts = {"paths": paths, "verify": {}, "blocked": {}, "not_run": {}, "insensitive_not_run": {}, "deferred": [], "plans": set()}
    for path in paths:
        document = queue_plan_entries(reads.read_json(path), path)
        for block in document.get("blocks") or []:
            for entry in block["plans"]:
                if not isinstance(entry, dict) or not common.is_text(entry.get("plan")):
                    raise common.InputError(f"큐 계획 항목에 plan 필요: {path}")
                base = common.base_plan(entry["plan"])
                facts["plans"].add(base)
                if entry.get("class") == "verify":
                    facts["verify"].setdefault(base, set()).add(path)
        for entry in document.get("not_run") or []:
            if not isinstance(entry, dict) or not common.is_text(entry.get("plan")):
                raise common.InputError(f"큐 not_run 항목에 plan 필요: {path}")
            base, reason = common.base_plan(entry["plan"]), entry.get("reason") if isinstance(entry.get("reason"), str) else ""
            facts["plans"].add(base)
            target = facts["insensitive_not_run"] if reason.startswith(INSENSITIVE_NOT_RUN_PREFIX) else facts["not_run"]
            target.setdefault(base, set()).add(path)
        for entry in document.get("blocked") or []:
            plan = entry.get("plan") if isinstance(entry, dict) else entry
            if not common.is_text(plan):
                raise common.InputError(f"큐 blocked 항목에 plan 필요: {path}")
            base = common.base_plan(plan)
            facts["plans"].add(base)
            facts["blocked"].setdefault(base, set()).add(path)
        for entry in document.get("deferred") or []:
            if not isinstance(entry, dict) or not common.is_text(entry.get("plan")) or not common.is_text(entry.get("scene")):
                raise common.InputError(f"큐 deferred 항목에 plan·scene 필요: {path}")
            facts["deferred"].append((common.base_plan(entry["plan"]), entry["scene"].strip(), path))
    return facts


def gap_item_scene(entry):
    item = entry.get("item") if isinstance(entry.get("item"), str) else ""
    if "@" in item:
        item = item.rsplit("@", 1)[1]
    return item.split("(", 1)[0].strip()


def read_gap_deferred(reads):
    found = []
    for path, key in ((common.APPROVAL_ONLY_PATH, "items"), (common.STRUCTURAL_GAPS_PATH, "gaps")):
        document = reads.read_json(path)
        if not isinstance(document, dict) or not isinstance(document.get(key), list):
            raise common.InputError(f"{path} 형식({key} 목록 필요)")
        for entry in document[key]:
            if not isinstance(entry, dict) or not common.is_text(entry.get("plan")):
                raise common.InputError(f"{path} 항목에 plan 필요")
            texts = [entry.get("destination"), entry.get("reason")]
            if any(isinstance(text, str) and DEFERRED_TEXT in text for text in texts):
                found.append((common.base_plan(entry["plan"]), gap_item_scene(entry), path))
    return found


def collect_deferred_items(queues, gap_deferred, problems):
    items = {}
    for base, scene, path in queues["deferred"] + gap_deferred:
        area = common.plan_area(base)
        if area is None:
            problems.append(f"영역 없는 계획(deferred 항목): {base}")
            continue
        items.setdefault((area, base, scene), set()).add(path)
    return [{"area": area, "plan": base, "scene": scene, "sources": sorted(paths)} for (area, base, scene), paths in sorted(items.items())]


def adoption_order(key):
    return common.p_plans().adoption_order(key)


def verdict_item_plan(verdict, key):
    entry = (verdict.get("adoptions") or {}).get(key)
    source = entry.get("state_source") if isinstance(entry, dict) else None
    plan = source.get("item_plan") if isinstance(source, dict) else None
    return plan if common.is_text(plan) else None


def expect_judges(expect):
    judges = {}
    for group in ("runs", "fallback_runs"):
        runs = expect.get(group) if isinstance(expect.get(group), dict) else {}
        for run_id in sorted(runs):
            run = runs[run_id] if isinstance(runs[run_id], dict) else {}
            for source in run.get("sources") or []:
                if isinstance(source, dict) and common.is_text(source.get("adoption")) and common.is_text(source.get("judge")):
                    judges.setdefault(source["adoption"], set()).add(source["judge"])
    return {key: sorted(paths) for key, paths in judges.items()}


def map_adoptions(expect, verdict, problems):
    adoptions = expect.get("adoptions")
    influences = expect.get("influences") if expect.get("influences") is not None else []
    if not isinstance(adoptions, dict) or not isinstance(influences, list):
        raise common.InputError("expect.json adoptions 객체·influences 목록 필요")
    mapped = {"adopted": {}, "influence": {}}
    for cell, keys in (("adopted", sorted(adoptions, key=adoption_order)), ("influence", [str(key) for key in influences])):
        for key in keys:
            plan = verdict_item_plan(verdict, key)
            if plan is None:
                problems.append(f"채택 계획 모름: expect {cell} {key}의 verdict state_source.item_plan 없음")
                continue
            mapped[cell][key] = {"plan": common.base_plan(plan), "item_plan": plan}
    return mapped


def check_adoption_counts(context, mapped, problems):
    p = common.p_plans()
    try:
        counts, _ = p.read_adoption_counts(context.root)
    except p.SummaryInputError as error:
        raise common.InputError(str(error))
    for area in common.sheet_areas():
        ours = sorted([key for cell in ("adopted", "influence") for key, value in mapped[cell].items() if common.plan_area(value["plan"]) == area], key=adoption_order)
        theirs = sorted(counts.get(area, []), key=adoption_order)
        if ours != theirs:
            problems.append(f"채택 수 불일치({area}): expect adoptions+influences {ours} ≠ p_plans.read_adoption_counts {theirs}")


def overlay_last_operations(overlay):
    history = overlay.get("history") if isinstance(overlay, dict) else None
    if not isinstance(history, list):
        raise common.InputError("overlay history 목록 필요")
    last = {}
    for entry in history:
        if not isinstance(entry, dict):
            continue
        source = entry.get("from") if isinstance(entry.get("from"), dict) else {}
        if common.is_text(source.get("plan")):
            last[common.base_plan(source["plan"])] = {"op": entry.get("op"), "reason": entry.get("reason") if isinstance(entry.get("reason"), str) else ""}
    return last


def classify_plan(base, facts):
    final = facts["journal"].final_by_base.get(base)
    status, reason = (final["status"], final["reason"]) if final else (None, "")
    last = facts["overlay_last"].get(base, {"op": None, "reason": ""})
    if base in facts["adopted_plans"]:
        return "adopted"
    if base in facts["influence_plans"]:
        return "influence"
    if base in facts["queues"]["verify"]:
        return "verify_only"
    if (status == "verify_failed" and reason.startswith(CONFLICT_PREFIX)) or (last["op"] == "remove" and last["reason"].startswith(CONFLICT_PREFIX)):
        return "conflict"
    if (status == "adopted" and last["op"] == "remove") or (status == "verify_failed" and HEADLESS_REVERT_TEXT in reason):
        return "removed_after_adoption"
    if status == "deferred":
        return "deferred"
    if status == "insensitive" or base in facts["queues"]["insensitive_not_run"]:
        return "insensitive"
    if status in STATUS_TO_CELL:
        return STATUS_TO_CELL[status]
    if base in facts["queues"]["blocked"] or base in facts["queues"]["not_run"]:
        return "blocked_not_run"
    if base in facts["retired"]:
        return "retired"
    return "unclassified"


def journal_judge(reads, record):
    runs = record.get("runs") or {}
    candidates = []
    if common.is_text(runs.get("confirm")):
        candidates.append(f"{runs['confirm']}/judge.json")
    if common.is_text(runs.get("screen")):
        candidates.append(f"{runs['screen']}/screen.json")
    return next((reads.relative(path) for path in candidates if reads.exists(path)), None)


def plan_evidence(reads, base, cell, facts):
    evidence = []
    adoption_keys = sorted([key for key, value in facts["mapped"]["adopted"].items() if value["plan"] == base], key=adoption_order)
    influence_keys = [key for key, value in facts["mapped"]["influence"].items() if value["plan"] == base]
    for key in adoption_keys + influence_keys:
        evidence += facts["expect_judges"].get(key, [])
    final = facts["journal"].final_by_base.get(base)
    judge = journal_judge(reads, final) if final else None
    queue_paths = sorted(facts["queues"]["verify"].get(base, set()) | facts["queues"]["blocked"].get(base, set()) | facts["queues"]["not_run"].get(base, set())
                         | facts["queues"]["insensitive_not_run"].get(base, set()))
    if cell in ("adopted", "influence"):
        files = [common.p_plans().EXPECT_PATH, common.p_plans().VERDICT_PATH] + evidence
    elif cell in ("blocked_not_run", "retired"):
        files = queue_paths + ([facts["retired"][base]] if base in facts["retired"] else [])
    else:
        files = ([final["journal"]] if final else []) + ([judge] if judge else []) + queue_paths
    if cell == "removed_after_adoption" and base in facts["overlay_last"]:
        files.append(facts["overlay_path"])
    unique = []
    for path in files:
        if path not in unique:
            unique.append(path)
    return {"area": common.plan_area(base), "cell": cell, "adoption_ids": adoption_keys, "influence_ids": influence_keys,
            "journal_status": final["status"] if final else None, "journal": final["journal"] if final else None,
            "journal_reason": common.strip_root(final["reason"], reads.root) if final else None, "judge": judge, "files": unique}


def count_basis(reads, plan_files, area):
    counts = {key: 0 for key in BASIS_KEYS}
    plans = 0
    for stem, path in sorted(plan_files.items()):
        if common.plan_area(stem) != area:
            continue
        plan = reads.read_json(path)
        if not isinstance(plan, dict):
            raise common.InputError(f"계획 형식(객체 필요): {path}")
        plans += 1
        metrics = plan.get("metrics") if isinstance(plan.get("metrics"), dict) else {}
        targets = metrics.get("targets") if isinstance(metrics.get("targets"), list) else []
        for target in targets:
            basis = target.get("basis") if isinstance(target, dict) else None
            if basis == "live2_image":
                counts["live2_image"] += 1
            elif basis == "spec_proposal":
                counts["spec_proposal"] += 1
            elif common.is_text(basis):
                counts["free_text"] += 1
            else:
                counts["unmarked"] += 1
    counts["targets"] = sum(counts[key] for key in BASIS_KEYS)
    counts["plans"] = plans
    return counts


def pr11_label(overall, problems, area):
    if overall is None:
        return PR11_NO_INPUT
    if overall.startswith(PERFORMANCE_UNMET_PREFIX) and overall.endswith(")"):
        return PR11_UNMET
    if overall.startswith(PERFORMANCE_PARTIAL_PREFIX) and overall.endswith(")"):
        return f"{PR11_PARTIAL_PREFIX}{overall[len(PERFORMANCE_PARTIAL_PREFIX):-1]})"
    if overall == common.p_plans().PROPOSED_PASS:
        return PR11_PASS
    problems.append(f"성능 형식 모름({area}): {overall}")
    return PR11_UNKNOWN


def performance_reason(overall, blocking):
    if overall is None:
        return "판정 불가"
    if overall.startswith(PERFORMANCE_UNMET_PREFIX):
        return "미충족"
    if overall.startswith(PERFORMANCE_PARTIAL_PREFIX):
        return f"부분(막는 측정 불가 {blocking})"
    if overall == common.p_plans().PROPOSED_PASS:
        return None
    return "판정 불가"


def approval_reason(approval):
    state, date = approval.get("state") or "", approval.get("date")
    if common.is_text(date) and state == f"{APPROVED_PREFIX}{date}":
        return None
    if common.is_text(date) and state.startswith(REJECTED_PREFIX):
        return REJECTED_PREFIX
    return common.p_plans().APPROVAL_PENDING


def basis_reasons(basis):
    if basis["targets"] == 0:
        return ["목표 없음(확인 불가)"]
    parts = [("목표 [제안]", basis["spec_proposal"]), ("자유 문안(출처 확인 불가)", basis["free_text"]), ("근거 미표기", basis["unmarked"])]
    return [f"{label} {count}" for label, count in parts if count]


def decide_equivalence(approval, overall, blocking, basis):
    approved = approval_reason(approval) is None
    performance_pass = overall == common.p_plans().PROPOSED_PASS
    all_live2 = basis["targets"] > 0 and basis["live2_image"] == basis["targets"]
    if approved and performance_pass and all_live2:
        return {"confirmed": True, "label": EQUIVALENT_LABEL, "reasons": []}
    reasons = [reason for reason in (approval_reason(approval), performance_reason(overall, blocking)) if reason] + basis_reasons(basis)
    label = common.p_plans().PROPOSED_PASS_LABEL if performance_pass else " · ".join(reasons)
    return {"confirmed": False, "label": label, "reasons": reasons}


def read_approvals(context):
    reads = context.reads
    reads.read_bytes(common.APPROVALS_PATH, required=False)
    p = common.p_plans()
    approvals_path = context.root / common.APPROVALS_PATH
    try:
        approvals, _ = p.read_approvals(context.root, approvals_path)
    except p.SummaryInputError as error:
        raise common.InputError(str(error))
    return approvals, approvals_path


def decide_area_approval(context, approvals, approvals_path, area):
    context.reads.read_bytes(f"{common.APPROVAL_DIR}/{area}/current.json", required=False)
    p = common.p_plans()
    try:
        return p.decide_approval(context.root, approvals, approvals_path, area)
    except p.SummaryInputError as error:
        raise common.InputError(str(error))


def build_universe(facts, plan_files):
    universe = set(common.base_plan(stem) for stem in plan_files) | set(common.base_plan(stem) for stem in facts["retired"])
    universe |= facts["queues"]["plans"] | {base for base, _, _ in facts["queues"]["deferred"]}
    universe |= {common.base_plan(stem) for stem in facts["journal"].seen_stems}
    universe |= facts["adopted_plans"] | facts["influence_plans"] | set(facts["overlay_last"])
    return sorted(universe)


def area_record(context, area, plans, facts, deferred_items, approvals, approvals_path, plan_files, problems):
    cells = {cell: sorted(base for base, details in plans.items() if details["area"] == area and details["cell"] == cell) for cell in CELL_ORDER}
    plan_count = sum(1 for details in plans.values() if details["area"] == area)
    cell_sum = sum(len(cells[cell]) for cell in CELL_ORDER if cell != "unclassified")
    adoption_ids = sorted([key for key, value in facts["mapped"]["adopted"].items() if common.plan_area(value["plan"]) == area], key=adoption_order)
    influence_ids = [key for key, value in facts["mapped"]["influence"].items() if common.plan_area(value["plan"]) == area]
    summary_area = (facts["summary"].get("areas") or {}).get(area)
    overall = summary_area.get("overall") if isinstance(summary_area, dict) and common.is_text(summary_area.get("overall")) else None
    blocking = summary_area.get("blocking_unmeasurable") if isinstance(summary_area, dict) else None
    approval = decide_area_approval(context, approvals, approvals_path, area)
    basis = count_basis(context.reads, plan_files, area)
    return {"plans": plan_count, "cells": cells, "adoption_ids": adoption_ids, "adoption_entries": len(adoption_ids), "adopted_plans": len(cells["adopted"]),
            "influence_ids": influence_ids, "deferred_items": [item for item in deferred_items if item["area"] == area],
            "performance": overall, "blocking_unmeasurable": blocking, "pr11": pr11_label(overall, problems, area),
            "approval": approval, "basis": basis, "equivalence": decide_equivalence(approval, overall, blocking, basis),
            "check": {"cell_sum": cell_sum, "plans": plan_count, "ok": cell_sum == plan_count}}


def totals(records, deferred_items):
    total = {"plans": sum(record["plans"] for record in records.values()),
             "adoption_entries": sum(record["adoption_entries"] for record in records.values()),
             "deferred_items": len(deferred_items),
             "equivalence_confirmed": sum(1 for record in records.values() if record["equivalence"]["confirmed"])}
    for cell in CELL_ORDER:
        total[cell] = sum(len(record["cells"][cell]) for record in records.values())
    total["other"] = sum(total[cell] for cell in OTHER_CELLS)
    return total


def other_cell_text(record):
    parts = [f"{CELL_SHORT[cell]} {len(record['cells'][cell])}" for cell in OTHER_CELLS if record["cells"][cell]]
    count = sum(len(record["cells"][cell]) for cell in OTHER_CELLS)
    return f"{count}({' · '.join(parts)})" if parts else "0"


def basis_text(basis):
    return f"live2 {basis['live2_image']} · [제안] {basis['spec_proposal']} · 자유 문안 {basis['free_text']} · 미표기 {basis['unmarked']}(목표 {basis['targets']})"


def approval_text(approval):
    return " · ".join(common.markdown_cell(value) for value in (approval.get("state"), approval.get("date"), approval.get("evidence")))


def render_markdown(order, records, plans, total, deferred_items, journal_order):
    cell = common.markdown_cell
    lines = ["# FX-P 영역별 상태 표 (PB2:214, runbook-fxp-v3 3.2)", "",
             "- 우선순위(계획당 한 칸, 위가 이김): " + " · ".join(CELL_LABELS[name] for name in CELL_ORDER[:-1]),
             f"- {JOURNAL_MERGE_RULE}", f"- {EQUIVALENCE_RULE}", "- 채택(고유) = expect 번호 채택 항목 수(고유 기본 계획 수), 영향(f5)은 채택에 넣지 않는다(PD-5)", "",
             "| 영역 | 채택(고유) | 영향 | 검증만 | 둔감 | 채택 없음 | conflict | 채택 뒤 제거 | deferred | 그 밖 | 성능 판정 | 막는 측정 불가 | PR-11 귀결 | 승인 상태·날짜·근거 경로 | basis 분포 | 동등 표기 | 칸별 계획 ID·판정 파일 |",
             "|" + "---|" * 17]
    for area in order:
        record = records[area]
        cells = record["cells"]
        lines.append("| " + " | ".join([area, f"{record['adoption_entries']}({record['adopted_plans']})", str(len(cells["influence"])), str(len(cells["verify_only"])),
                                        str(len(cells["insensitive"])), str(len(cells["no_adopt"])), str(len(cells["conflict"])), str(len(cells["removed_after_adoption"])),
                                        f"계획 {len(cells['deferred'])} · 항목 {len(record['deferred_items'])}", other_cell_text(record), cell(record["performance"]),
                                        cell(record["blocking_unmeasurable"]), cell(record["pr11"]), approval_text(record["approval"]), basis_text(record["basis"]),
                                        cell(record["equivalence"]["label"]), f"{record['plans']}계획(아래 칸별 표)"]) + " |")
    lines.append("| 합계 | " + " | ".join([f"{total['adoption_entries']}({total['adopted']})", str(total["influence"]), str(total["verify_only"]), str(total["insensitive"]),
                                          str(total["no_adopt"]), str(total["conflict"]), str(total["removed_after_adoption"]),
                                          f"계획 {total['deferred']} · 항목 {total['deferred_items']}", str(total["other"]), "—", "—", "—", "—", "—",
                                          f"동등 확인 {total['equivalence_confirmed']}영역", f"{total['plans']}계획"]) + " |")
    lines += ["", "## deferred 항목((영역, 계획, 장면) 키, 큐 deferred[]·approval-only·structural-gaps 합집합)", ""]
    lines += [f"- {item['area']} · {item['plan']} · {item['scene']} ← {' · '.join(item['sources'])}" for item in deferred_items] or ["- 없음"]
    lines += ["", "## 칸별 계획 ID·판정 파일", "", "| 영역 | 칸 | 계획 | 채택 번호 | 일지 최종 | 판정 파일 |", "|---|---|---|---|---|---|"]
    for area in order:
        for name in CELL_ORDER:
            for base in records[area]["cells"][name]:
                details = plans[base]
                numbers = "·".join(details["adoption_ids"] + details["influence_ids"]) or "—"
                status = f"{details['journal_status']} ({details['journal']})" if details["journal_status"] else "—"
                lines.append(f"| {area} | {cell(CELL_LABELS[name])} | {base} | {numbers} | {cell(status)} | {cell(' · '.join(details['files']) or None)} |")
    lines += ["", "## 일지 합침 순서(PD-6)", ""] + [f"- {entry['batch']} · {cell(entry['first_hold'])} · {entry['journal']}" for entry in journal_order]
    lines += ["", "## 검산", ""] + [f"- {area}: 칸 합 {records[area]['check']['cell_sum']} / 고유 기본 계획 {records[area]['check']['plans']} → {'일치' if records[area]['check']['ok'] else '불일치'}"
                                   for area in order]
    return lines


def summary_text(order, total):
    return (f"areas={len(order)} plans={total['plans']} adopted={total['adoption_entries']} adopted_plans={total['adopted']} influence={total['influence']} "
            f"verify_only={total['verify_only']} insensitive={total['insensitive']} no_adopt={total['no_adopt']} conflict={total['conflict']} "
            f"removed_after_adoption={total['removed_after_adoption']} deferred_plans={total['deferred']} deferred_items={total['deferred_items']} other={total['other']} "
            f"unclassified={total['unclassified']} equivalence_confirmed={total['equivalence_confirmed']}")


def build(context):
    reads, p, problems = context.reads, common.p_plans(), []
    summary = reads.read_json(common.P_SUMMARY_PATH)
    expect = reads.read_json(p.EXPECT_PATH)
    verdict = reads.read_json(p.VERDICT_PATH)
    if not isinstance(summary, dict) or not isinstance(summary.get("areas"), dict):
        raise common.InputError(f"{common.P_SUMMARY_PATH} areas 객체 필요")
    if not isinstance(expect, dict) or not isinstance(verdict, dict) or not isinstance(verdict.get("adoptions"), dict):
        raise common.InputError("expect.json·verdict-w11.json 형식(verdict adoptions 객체 필요)")
    if not common.is_text(expect.get("overlay")):
        raise common.InputError("expect.json overlay 경로 필요")
    overlay_path = reads.relative(expect["overlay"])
    mapped = map_adoptions(expect, verdict, problems)
    check_adoption_counts(context, mapped, problems)
    plan_files = {common.plan_stem(path): path for path in reads.glob(f"{common.CALIB_PLANS_DIR}/*.json")}
    facts = {"summary": summary, "mapped": mapped, "journal": common.merge_journals(reads), "queues": read_queues(reads),
             "overlay_last": overlay_last_operations(reads.read_json(overlay_path)), "overlay_path": overlay_path,
             "retired": {common.base_plan(path): path for path in reads.glob(f"{common.RETIRED_PLANS_DIR}/*.json")}, "expect_judges": expect_judges(expect),
             "adopted_plans": {value["plan"] for value in mapped["adopted"].values()}, "influence_plans": {value["plan"] for value in mapped["influence"].values()}}
    deferred_items = collect_deferred_items(facts["queues"], read_gap_deferred(reads), problems)
    plans = {}
    for base in build_universe(facts, plan_files):
        if common.plan_area(base) is None:
            problems.append(f"영역 없는 계획: {base}")
            continue
        cell = classify_plan(base, facts)
        if cell == "unclassified":
            problems.append(f"분류 없는 계획(12단계 밖): {base}")
        plans[base] = plan_evidence(reads, base, cell, facts)
    approvals, approvals_path = read_approvals(context)
    order = common.sheet_areas()
    records = {area: area_record(context, area, plans, facts, deferred_items, approvals, approvals_path, plan_files, problems) for area in order}
    total = totals(records, deferred_items)
    document = {"version": 1, "areas_order": list(order), "areas": records, "plans": plans, "totals": total, "deferred_items": deferred_items,
                "journal_order": facts["journal"].order(), "queues": facts["queues"]["paths"], "overlay": overlay_path,
                "rule": {"priority": [CELL_LABELS[name] for name in CELL_ORDER[:-1]], "journal_merge": JOURNAL_MERGE_RULE, "equivalence": EQUIVALENCE_RULE,
                         "pr11": "4.7: 미충족 → FX-P 성능 미달 · 부분(…) → FX-P 성능 부분(괄호 안 그대로) · 충족(proposed) → 화면 승인만 · 입력 없음 → 판정 불가(성능 입력 없음)",
                         "basis": "3.2: 영역 Tools/FluidFX/calib/plans/*.json(retired 제외) metrics.targets[].basis 전부, live2_image·spec_proposal·그 밖 글자 = 자유 문안·없음 = 미표기"}}
    markdown = render_markdown(order, records, plans, total, deferred_items, facts["journal"].order())
    return common.Outcome(document, markdown, summary_text(order, total), problems)

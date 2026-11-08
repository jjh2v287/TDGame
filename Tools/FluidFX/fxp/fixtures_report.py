"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 입력 9개·실제 fxp_static.json·합성 실행서(고정 문구 + 그 static 원래 줄로 만든 5·6절 표)/defs/sha 기록으로 fxp_audit.py report 를 돌리고(이음표·생략 경로는 check_static_tables 직접 호출) 정상·같은 바이트·종료 2(입력 빠짐·형식·날짜·원래 줄 없음)·종료 1(sha 불일치·D-49(40자 창 경계)·입력 problems·연결 불일치·4.1/4.7 재검산(basis 섞임·승인 대기 ∧ 충족)·행 동등·4.2·4.3·골든·고정 문구·FU·FQ 표(v3 FU-15 대 v4·조각(순서)·원천 조각(묶음·단일 id)·id 목록(빠짐·순서·중복)·기록만(묶음 가운데·표지 없는 줄))·excluded)·경고(모듈 사이 sha)를 본다.
상태: 2026-11-07 FX-P X6(B6), fix1(빌더 A) 반박 변이·FU·FQ 표 대조 사례 추가, 2026-11-08 fix2 반박 공백 4변이(순서·중복·기록만 둘·조각 순서)·원천 조각 사례 추가, fix3 X7 반박 공백(괄호·여러 묶음 모호·생략 경로 고정 '[회귀 고정]', 앞뒤 공백·이음표 수·생략 경고)·표기(d 없음·P 규칙·레벨 평균·합산·교훈 줄) 사례 추가.
"""
import copy
import hashlib
import json
import shutil
from pathlib import Path

import common
import report
from check_fxp_fixtures import TOOL_DIRECTORY, FixtureLog, run_audit, write_json, write_text

OUTPUT = "Saved/FluidFX/fxp"
STATIC = "Tools/FluidFX/fxp/fxp_static.json"
RUNBOOK = "Saved/AgentOps/fixture/runbook-fxp-vf.md"
DEFS = "Saved/AgentOps/fixture/fxp-defs-vf.ps1"
SHA_RECORD = f"{OUTPUT}/x0-runbook.sha256"
DATE = "2026-11-08"
UNMET = "미충족(통과 1 / 미달 1 / 행 2 / 판정 불가 0 / 측정 불가 3 / 출하 밖 0)"
PARTIAL = "부분(통과 1 / 행 1 / 판정 불가 0 / 측정 불가 2 / 출하 밖 0)"
CELL_NAMES = ("adopted", "influence", "verify_only", "insensitive", "no_adopt", "conflict", "removed_after_adoption", "deferred", "undetermined", "failed",
              "blocked_not_run", "retired", "unclassified")
EXPECTED_INPUTS = sorted([f"{OUTPUT}/{name}.json" for name, _, _ in report.INPUT_DOCUMENTS] + [STATIC, SHA_RECORD, RUNBOOK, DEFS])
V3_FU15_ITEM = "굽기 변형 R16: `ApplyMaterialLook` 빠진 키 되돌림(C++)"
V3_FU15_SOURCE = "`C0/impl/b5/index.md:112`"
MIXED_BASIS = {"live2_image": 1, "spec_proposal": 1, "free_text": 0, "unmarked": 0, "targets": 2, "plans": 1}
PENDING_WATER = {"state": "승인 대기", "date": None, "evidence": "Saved/FluidFX/approval/water/current.json"}


def sha_file(root, relative):
    return hashlib.sha256((Path(root) / relative).read_bytes()).hexdigest()


def real_static():
    return json.loads((TOOL_DIRECTORY / "fxp_static.json").read_text(encoding="utf-8"))


def table_section(heading, header, rows, text_key):
    return [heading, "", header, "|---|---|---|"] + [f"| {row['no']} | {row[text_key]} | {row['source']} |" for row in rows] + [""]


def runbook_text(static):
    lines = [static[key] for key in report.STATIC_PHRASE_KEYS] + list(report.runbook_phrases(common.p_plans())) + [""]
    lines += table_section("## 5. 후속 후보 FU-n (PD-2·15, FX-Q 제안)", "| FU | 항목 | 원천 |", static["follow_up_rows"], "item")
    lines += table_section("## 6. 사용자 대기 FQ-n (FX-P를 막지 않음)", "| FQ | 묻는 것 | 근거 |", static["user_item_rows"], "ask")
    return "\n".join(lines + ["## 7. 위험·복구", "- 끝"]) + "\n"


def output_document(command, **fields):
    document = {"version": 1, "command": command, "inputs": {}, "problems": [], "summary": f"SUMMARY {command} problems=0"}
    document.update(fields)
    return document


def area_record(approval, performance, basis, confirmed, label, pr11, adoption_entries=0, adopted_plans=0, cells=None):
    all_cells = {name: [] for name in CELL_NAMES}
    all_cells.update(cells or {})
    return {"adoption_entries": adoption_entries, "adopted_plans": adopted_plans, "plans": sum(len(value) for value in all_cells.values()), "approval": approval,
            "performance": performance, "blocking_unmeasurable": 3, "pr11": pr11, "basis": dict(basis, plans=1), "equivalence": {"confirmed": confirmed, "label": label},
            "cells": all_cells, "deferred_items": []}


def base_documents(static):
    pending = {"state": "승인 대기", "date": None, "evidence": "Saved/FluidFX/approval/fire/current.json"}
    approved = {"state": f"승인 {DATE}", "date": DATE, "evidence": "Saved/FluidFX/approval/water/current.json"}
    fire = area_record(pending, UNMET, {"live2_image": 1, "spec_proposal": 1, "free_text": 0, "unmarked": 0, "targets": 2}, False, "승인 대기 · 미충족 · 목표 [제안] 1",
                       "FX-P 성능 미달", 2, 1, {"adopted": ["fire_a"], "insensitive": ["fire_b"]})
    water = area_record(approved, "충족(proposed)", {"live2_image": 2, "spec_proposal": 0, "free_text": 0, "unmarked": 0, "targets": 2}, True, report.EQUIVALENT_LABEL,
                        "화면 승인만", 0, 0, {"no_adopt": ["water_a"]})
    golden_line = static["golden_line_template"].replace("<n>", "2").replace("<k>", "0").replace("<m>", "0")
    compare_row = {"area": "fire", "row": "FI1/ft", "metric": "ft", "d": {"median": 1.2, "interval": {"lower": 1.1, "upper": 1.3, "state": "통과"}}, "d_prime": None,
                   "p95_record_median": 1.25, "limit_ms": 4.65, "limit_sources": [{"item": "S019", "source": "PF:159 GPU"}], "p_label": "통과",
                   "live2_same_machine": report.COMPARE_SAME_MACHINE, "live2_public": "PF:159 'LIVE-2 기준' 열 · PF:75 환산 배율", "notation": report.COMPARE_NOTATION}
    r13_rows = [{"id": "particles_10k", "name": "입자 1만", "basis": "P IN1", "kind": "p_quote", "label": "P 판정 인용(IN1/ft 통과)", "live2": report.R13_LIVE2,
                 "p_rows": [{"row": "IN1/ft", "d_median": 0.16, "interval_lower": 0.12, "interval_upper": 0.21, "limit_ms": 0.5, "label": "통과"}], "values": []},
                {"id": "actors_49", "name": "액터 49(16)", "basis": "FX-7p S9", "kind": "structural", "label": "구조 격차(기존 측정 인용·보정 전, FX-7p S9)",
                 "live2": report.R13_LIVE2, "p_rows": [], "values": [{"name": "49×256 솔버(S9)", "value": "7.515"}]},
                {"id": "primitives_200", "name": "프리미티브 200", "basis": "S039", "kind": "no_stage", "label": "무대 없음(R13s 대기)", "live2": report.R13_LIVE2,
                 "p_rows": [], "values": []}]
    rows = [{"id": "L1", "area": "water", "status": "있음", "equivalent": True}, {"id": "L2", "area": "water", "status": "부분", "equivalent": False},
            {"id": "E1", "area": "fire", "status": "있음", "equivalent": False}, {"id": "A1", "area": "common", "status": "있음", "equivalent": False}]
    package_checks = [{"id": "①", "status": "통과", "title": "매니페스트에 PresetData 두 JSON"}, {"id": "②", "status": "통과", "title": "스테이징 sha"}]
    return {
        "areas": output_document("areas", areas_order=["fire", "water"], areas={"fire": fire, "water": water}, totals={"adoption_entries": 2}),
        "items": output_document("items", class_counts={"closed": 2}, follow_ups=[{"id": "FU-4", "linked_items": ["1"]}], user_items=[{"id": "FQ-1", "linked_items": ["2"]}],
                                 unmeasurable={"entries": [{"group": "비대상"}, {"group": "기능 공백"}]},
                                 items=[{"id": "1", "class": "닫힘", "title": "영역별 상태 표", "status": "areas가 센다", "refs": ["FU-4"]},
                                        {"id": "2", "class": "FQ-1", "title": "승인 상태", "status": "승인 대기", "refs": []}]),
        "golden": output_document("golden", adoptions=[{"id": "1"}, {"id": "2"}], discrete_adoptions=[], golden_path_adoptions=[],
                                  type_counts={"실수": 2, "열거": 0, "불": 0, "정수": 0}, golden_line=golden_line),
        "compare": output_document("compare", rows=[compare_row], label_counts={"통과": 1}),
        "rows": output_document("rows", areas_input=f"{OUTPUT}/areas.json", rows=rows, equivalent_rows=1,
                                distribution={"ledger": {"있음": 3, "부분": 1}, "final": {"있음": 3, "부분": 1}}, demoted=[], markers={"approval_open": 1},
                                w8={"closed": True, "rows": {"L18b": "해당 없음(LIVE-2 미지원)", "L19": "해당 없음(LIVE-2 미지원)"}},
                                areas={"water": {"rows": 2, "status": {"있음": 1, "부분": 1}, "demoted": 0, "approval": f"승인 {DATE}", "equivalent_rows": 1}},
                                pass_list={"mode": "on", "files": []}),
        "levels": output_document("levels", counts={"levels": 59, "targets": 58, "scope": 57, "loose": 56, "strict_mean": 0.5, "strict_items": 10, "items": 20, "excluded": 1,
                                                    "manual_refs": 0, "unknown_ids": 0, "aliased": 0},
                                  excluded={"L.3.7": {"kind": "후속 대기", "reason": "스트레스 무대 없음", "evidence": "Saved/x/plan.md:220", "follow_up": "FU-12", "valid": True}},
                                  levels=[{"id": "L.0.1", "name": "Clouds", "target": True, "loose": True, "excluded": None},
                                          {"id": "L.0.2", "name": "Smoke", "target": True, "loose": False, "excluded": None},
                                          {"id": "L.3.7", "name": "PerformanceTest_Primitives", "target": True, "loose": False, "excluded": {"kind": "후속 대기"}}]),
        "r13": output_document("r13", rows=r13_rows),
        "package-min": output_document("package", tag="min", verdict="첫 읽기 확인", branch="confirmed", checks=package_checks, cook={"ok": True}, launch={"present": True},
                                       records={"shader_mapping": "미검증(전용 로그 줄 없음)"}),
        "package-fluid": output_document("package", tag="fluid", verdict="첫 읽기 확인", branch="confirmed", checks=package_checks, cook={"ok": True}, launch={"present": True},
                                         records={"shader_mapping": "미검증(전용 로그 줄 없음)"}),
    }


def make_world(root, mutate=None, after=None):
    root = Path(root)
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    static = real_static()
    documents = base_documents(static)
    settings = {"static": static, "runbook": runbook_text(static), "defs": "defs fixture\n", "sha_lines": None, "rows_areas_sha": None, "keep_items_inputs": False}
    if mutate:
        mutate(documents, settings)
    write_json(root, STATIC, settings["static"])
    write_text(root, RUNBOOK, settings["runbook"])
    write_text(root, DEFS, settings["defs"])
    default_line = f"0 {sha_file(root, RUNBOOK)} {sha_file(root, DEFS)} {'a' * 40} {RUNBOOK}"
    lines = settings["sha_lines"](default_line) if settings["sha_lines"] else [default_line]
    if lines is not None:
        write_text(root, SHA_RECORD, "\n".join(lines) + "\n")
    for name in ("package-min", "package-fluid", "areas", "golden", "compare", "r13", "levels"):
        if documents[name] is not None:
            write_json(root, f"{OUTPUT}/{name}.json", documents[name])
    rows = documents["rows"]
    if rows is not None:
        if isinstance(rows.get("areas_input"), str):
            rows["inputs"][rows["areas_input"]] = settings["rows_areas_sha"] or sha_file(root, f"{OUTPUT}/areas.json")
        write_json(root, f"{OUTPUT}/rows.json", rows)
    items = documents["items"]
    if items is not None:
        if not settings["keep_items_inputs"]:
            items["inputs"].update({f"{OUTPUT}/package-min.json": sha_file(root, f"{OUTPUT}/package-min.json"),
                                    f"{OUTPUT}/package-fluid.json": sha_file(root, f"{OUTPUT}/package-fluid.json"), STATIC: sha_file(root, STATIC)})
        write_json(root, f"{OUTPUT}/items.json", items)
    write_text(root, f"{OUTPUT}/fxp-audit.json", "{not json")
    if after:
        after(root)
    return root


def run_report(root, name, runbook=RUNBOOK, defs=DEFS, date=DATE, public=True):
    public_path = Path(root) / "fixture-out" / f"{name}-public.md"
    if public_path.exists():
        public_path.unlink()
    extra = ["--runbook", runbook, "--defs", defs, "--date", date] + (["--public-md", str(public_path)] if public else [])
    run = run_audit(root, "report", name, extra)
    run.public_bytes = public_path.read_bytes() if public and public_path.is_file() else None
    return run


def problems_text(run):
    return " | ".join((run.document or {}).get("problems") or []) + run.stderr


def check_normal(log, root):
    world = make_world(root / "normal")
    first = run_report(world, "normal")
    log.check("정상 종료 0", first.code == 0, f"종료 {first.code} {problems_text(first)}")
    document = first.document or {}
    public_lines = (first.public_bytes or b"").decode("utf-8").splitlines()
    sha8 = sha_file(world, RUNBOOK)[:8]
    static = real_static()
    lesson = f"교훈: {static['lesson_line']}" if "lesson_line" in static else report.LESSON_LINE
    log.check("정상 공개 md 머리 3줄(교훈 줄 = fxp_static lesson_line)", public_lines[:3] == [f"{report.TITLE_PREFIX}{DATE}, claude)", lesson, report.COMMAND_LINE], str(public_lines[:3]))
    log.check("정상 원천 대조 생략 0줄(JSON·md 경고)", document.get("static_tables_skipped") == [] and "FU·FQ id별 원천 대조 생략 0줄" in first.markdown,
              f"{document.get('static_tables_skipped')}")
    log.check("정상 4.6 머리 채움", "동등 확인 1영역" in document.get("header", "") and f"실행서 sha {sha8}(기록 1줄)" in document.get("header", ""), document.get("header"))
    log.check("정상 md 끝 SUMMARY", first.markdown.rstrip("\n").splitlines()[-1].startswith("SUMMARY ") and public_lines[-1].startswith("SUMMARY "), first.markdown[-200:])
    log.check("정상 입력 고정 목록(fxp-audit* 제외)", sorted(document.get("inputs") or {}) == EXPECTED_INPUTS, str(sorted(document.get("inputs") or {})))
    log.check("정상 동등 영역·행", (document.get("areas") or {}).get("confirmed") == ["water"] and (document.get("rows") or {}).get("equivalent_rows") == 1, "")
    log.check("정상 공개 md sha 기록", (document.get("public_md") or {}).get("sha256") == hashlib.sha256(first.public_bytes or b"").hexdigest(), "")
    log.check("정상 출력에 --out·argv 없음", b"fixture-out" not in (first.json_bytes or b"") and b"--runbook" not in (first.json_bytes or b""), "")
    second = run_report(world, "normal-again")
    log.check("같은 바이트(JSON·md·공개 md)", first.json_bytes == second.json_bytes and first.md_bytes == second.md_bytes and first.public_bytes == second.public_bytes, "")
    third = run_report(world, "normal-no-public", public=False)
    log.check("공개 md 없이도 JSON 같은 바이트", third.code == 0 and third.json_bytes == first.json_bytes, f"종료 {third.code}")


def expect_code(log, root, case, code, mutate=None, after=None, expect_text=None, **run_options):
    slug = f"case{len(log.lines):03d}"
    world = make_world(root / slug, mutate, after)
    run = run_report(world, slug, **run_options)
    text_ok = expect_text is None or expect_text in problems_text(run)
    output_ok = run.document is None if code == 2 else run.document is not None
    log.check(case, run.code == code and text_ok and output_ok, f"종료 {run.code}(기대 {code}) {problems_text(run)[:300]}")
    return run


def set_value(name, path, value):
    def mutate(documents, settings):
        target = documents[name]
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
    return mutate


def chain(*mutations):
    def mutate(documents, settings):
        for mutation in mutations:
            mutation(documents, settings)
    return mutate


def setting(key, value):
    def mutate(documents, settings):
        settings[key] = value
    return mutate


def append_file(relative, text):
    def after(root):
        target = Path(root) / relative
        target.write_text(target.read_text(encoding="utf-8") + text, encoding="utf-8")
    return after


def rewrite_json(relative, key, value):
    def after(root):
        target = Path(root) / relative
        document = json.loads(target.read_text(encoding="utf-8"))
        document[key] = value
        target.write_text(json.dumps(document, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return after


def check_input_errors(log, root):
    expect_code(log, root, "입력 빠짐 r13 종료 2", 2, set_value_none("r13"))
    expect_code(log, root, "입력 빠짐 sha 기록 종료 2", 2, setting("sha_lines", lambda line: None))
    expect_code(log, root, "입력 빠짐 실행서 종료 2", 2, runbook="Saved/AgentOps/fixture/missing.md")
    expect_code(log, root, "날짜 형식 종료 2", 2, date="2026-1-8")
    expect_code(log, root, "command 다름 종료 2", 2, set_value("compare", ["command"], "areas"))
    expect_code(log, root, "package tag 다름 종료 2", 2, set_value("package-fluid", ["tag"], "min"))
    expect_code(log, root, "sha 줄 형식 종료 2", 2, setting("sha_lines", lambda line: ["0 xyz abc"]))
    expect_code(log, root, "머리 자리 표시 없음 종료 2", 2, lambda documents, settings: settings["static"].update(header_template="머리 문장"))


def set_value_none(name):
    def mutate(documents, settings):
        documents[name] = None
    return mutate


def other_sha_line(line):
    fields = line.split()
    return " ".join([fields[0], "1" * 64, "2" * 64, fields[3], "old.md"])


def check_runbook_sha(log, root):
    expect_code(log, root, "실행서 sha 불일치 종료 1", 1, after=append_file(RUNBOOK, "수정\n"), expect_text="실행서 sha 불일치")
    expect_code(log, root, "defs sha 불일치 종료 1", 1, after=append_file(DEFS, "수정\n"), expect_text="정의 묶음 sha 불일치")
    run = expect_code(log, root, "마지막 줄만 대조(앞 줄 다름) 종료 0", 0, setting("sha_lines", lambda line: [other_sha_line(line), line.replace("0 ", "1 ", 1)]))
    log.check("기록 2줄 머리", "(기록 2줄)" in (run.document or {}).get("header", ""), (run.document or {}).get("header"))
    expect_code(log, root, "마지막 줄 다름 종료 1", 1, setting("sha_lines", lambda line: [line, other_sha_line(line).replace("0 ", "1 ", 1)]), expect_text="실행서 sha 불일치")
    expect_code(log, root, "HEAD 다름은 대조 안 함 종료 0", 0, setting("sha_lines", lambda line: [line.replace("a" * 40, "b" * 40)]))
    expect_code(log, root, "고정 문구 실행서에 없음 종료 1", 1, lambda documents, settings: settings.update(runbook=settings["runbook"].replace(report.COMPARE_NOTATION, "")),
                expect_text="고정 문구")


def check_d49(log, root):
    expect_code(log, root, "D-49 LIVE-2 수치 종료 1", 1, set_value("items", ["items", 0, "title"], "LIVE-2 기준 16.6 ms"), expect_text="D-49")
    expect_code(log, root, "D-49 이미지 확장자 종료 1", 1, set_value("items", ["items", 0, "status"], "시트 sheet.png"), expect_text="이미지 확장자")
    expect_code(log, root, "D-49 원본 폴더 이름 종료 1", 1, set_value("items", ["items", 1, "status"], "Saved/FluidFX/Live2_Reference/x"), expect_text="원본 폴더")
    expect_code(log, root, "D-49 표 칸 경계(| 뒤 숫자) 종료 0", 0, chain(set_value("items", ["items", 0, "title"], "LIVE-2 열"), set_value("items", ["items", 0, "status"], "16 ms")))
    expect_code(log, root, "D-49 LIVE-2와 수치 사이 40자(창 끝) 종료 1", 1, set_value("items", ["items", 0, "title"], f"LIVE-2 {'가' * 38} 16.6 ms"), expect_text="D-49")
    expect_code(log, root, "D-49 LIVE-2와 수치 사이 41자(창 밖) 종료 0", 0, set_value("items", ["items", 0, "title"], f"LIVE-2 {'가' * 39} 16.6 ms"))


def check_chain_and_inputs(log, root):
    expect_code(log, root, "입력 problems 종료 1", 1, set_value("compare", ["problems"], ["x"]), expect_text="problems 1개")
    expect_code(log, root, "연결 불일치 package 뒤 items 종료 1", 1, after=rewrite_json(f"{OUTPUT}/package-min.json", "verdict", "첫 읽기 실패(①)"), expect_text="연결 불일치")
    expect_code(log, root, "연결 불일치 items가 package 전 종료 1", 1,
                chain(setting("keep_items_inputs", True), set_value("items", ["inputs"], {f"{OUTPUT}/package-min.json": None})), expect_text="package-min.json")
    expect_code(log, root, "rows 영역 입력 다른 경로 sha 같음 종료 0", 0, set_value("rows", ["areas_input"], "Saved/AgentOps/x/try-areas.json"))
    expect_code(log, root, "rows 영역 입력 sha 다름 종료 1", 1, chain(set_value("rows", ["areas_input"], "Saved/AgentOps/x/try-areas.json"), setting("rows_areas_sha", "0" * 64)),
                expect_text="영역 입력")
    run = expect_code(log, root, "모듈 사이 sha 다름은 경고 종료 0", 0,
                      chain(set_value("package-min", ["inputs"], {"Content/x.json": "a" * 64}), set_value("package-fluid", ["inputs"], {"Content/x.json": "b" * 64})))
    log.check("경고 1건·SUMMARY input_mismatch=1", len((run.document or {}).get("warnings") or []) == 1 and "input_mismatch=1" in (run.document or {}).get("summary", ""), "")


def fire_record(documents):
    return documents["areas"]["areas"]["fire"]


def update_fire(**fields):
    def mutate(documents, settings):
        fire_record(documents).update(fields)
    return mutate


def check_areas(log, root):
    expect_code(log, root, "거짓 동등(승인 대기) 종료 1", 1, update_fire(equivalence={"confirmed": True, "label": report.EQUIVALENT_LABEL}), expect_text="4.1 재검산(fire)")
    expect_code(log, root, "빠진 동등 종료 1", 1, chain(set_value("areas", ["areas", "water", "equivalence"], {"confirmed": False, "label": common.p_plans().PROPOSED_PASS_LABEL}),
                                                         set_value("rows", ["rows", 0, "equivalent"], False), set_value("rows", ["equivalent_rows"], 0)), expect_text="4.1 재검산(water)")
    expect_code(log, root, "basis 목표 0이면 동등 아님 종료 1", 1, set_value("areas", ["areas", "water", "basis"], {"live2_image": 0, "spec_proposal": 0, "free_text": 0, "unmarked": 0,
                                                                                                              "targets": 0}), expect_text="4.1 재검산(water)")
    expect_code(log, root, "4.1 사유 빠짐 종료 1", 1, update_fire(equivalence={"confirmed": False, "label": "승인 대기 · 미충족"}), expect_text="사유 빠짐")
    expect_code(log, root, "성능만 충족 표기 종료 0", 0, update_fire(performance="충족(proposed)", pr11="화면 승인만",
                                                                     equivalence={"confirmed": False, "label": common.p_plans().PROPOSED_PASS_LABEL}))
    expect_code(log, root, "성능만 충족 표기 틀림 종료 1", 1, update_fire(performance="충족(proposed)", pr11="화면 승인만", equivalence={"confirmed": False, "label": "승인 대기"}),
                expect_text="성능만 충족")
    expect_code(log, root, "4.7 PR-11 틀림 종료 1", 1, update_fire(pr11="화면 승인만"), expect_text="4.7 PR-11")
    partial = update_fire(performance=PARTIAL, blocking_unmeasurable=2, pr11=f"FX-P 성능 {PARTIAL}",
                          equivalence={"confirmed": False, "label": "승인 대기 · 부분(막는 측정 불가 2) · 목표 [제안] 1"})
    expect_code(log, root, "4.7 부분 괄호 그대로 종료 0", 0, partial)
    expect_code(log, root, "4.7 부분 괄호 바뀜 종료 1", 1, chain(partial, update_fire(pr11="FX-P 성능 부분(통과 1)")), expect_text="4.7 PR-11")
    expect_code(log, root, "4.7 성능 입력 없음 종료 0", 0, update_fire(performance=None, pr11="판정 불가(성능 입력 없음)",
                                                                       equivalence={"confirmed": False, "label": "승인 대기 · 판정 불가 · 목표 [제안] 1"}))


def update_water(**fields):
    def mutate(documents, settings):
        documents["areas"]["areas"]["water"].update(fields)
    return mutate


def water_not_equivalent(documents, settings):
    chain(update_water(equivalence={"confirmed": False, "label": common.p_plans().PROPOSED_PASS_LABEL}), set_value("rows", ["rows", 0, "equivalent"], False),
          set_value("rows", ["equivalent_rows"], 0))(documents, settings)


def check_equivalence_conditions(log, root):
    expect_code(log, root, "승인 ∧ 충족 ∧ basis 섞임(live2 1·[제안] 1) 동등 아님 종료 0", 0, chain(update_water(basis=dict(MIXED_BASIS)), water_not_equivalent))
    expect_code(log, root, "승인 ∧ 충족 ∧ basis 섞임인데 areas 동등 확인 종료 1", 1, update_water(basis=dict(MIXED_BASIS)), expect_text="4.1 재검산(water)")
    expect_code(log, root, "승인 대기 ∧ 충족 ∧ live2 전부 동등 아님 종료 0", 0, chain(update_water(approval=dict(PENDING_WATER)), water_not_equivalent))
    expect_code(log, root, "승인 대기 ∧ 충족 ∧ live2 전부인데 areas 동등 확인 종료 1", 1, update_water(approval=dict(PENDING_WATER)), expect_text="4.1 재검산(water)")


def static_entry(static, key, value, field="id"):
    return next(entry for entry in static[key] if entry[field] == value)


def v3_fu15(rows_too):
    def mutate(documents, settings):
        static = settings["static"]
        targets = [static_entry(static, "follow_ups", "FU-15")] + ([static_entry(static, "follow_up_rows", "15", "no")] if rows_too else [])
        for entry in targets:
            entry.update(item=V3_FU15_ITEM, source=V3_FU15_SOURCE)
    return mutate


def change_static(change):
    def mutate(documents, settings):
        change(settings["static"])
    return mutate


def add_runbook_fq_row(documents, settings):
    last = f"| {settings['static']['user_item_rows'][-1]['no']} |"
    lines = settings["runbook"].split("\n")
    index = next(number for number, line in enumerate(lines) if line.startswith(last))
    settings["runbook"] = "\n".join(lines[:index + 1] + ["| 21 | 새 질문 | DEC:999 |"] + lines[index + 1:])


def swap_fields(key, first, second, field):
    def change(static):
        a, b = static_entry(static, key, first), static_entry(static, key, second)
        a[field], b[field] = b[field], a[field]
    return change_static(change)


def swap_follow_up_order(static):
    entries = static["follow_ups"]
    first, second = [entries.index(static_entry(static, "follow_ups", identifier)) for identifier in ("FU-2", "FU-3")]
    entries[first], entries[second] = entries[second], entries[first]


def duplicate_follow_up(static):
    static["follow_ups"].insert(2, dict(static_entry(static, "follow_ups", "FU-2")))


def duplicate_row_and_entries(static):
    rows, entries = static["follow_up_rows"], static["follow_ups"]
    row_index = rows.index(static_entry(static, "follow_up_rows", "2 · 3", "no"))
    rows.insert(row_index + 1, dict(rows[row_index]))
    entry_index = entries.index(static_entry(static, "follow_ups", "FU-3"))
    entries[entry_index + 1:entry_index + 1] = [dict(static_entry(static, "follow_ups", "FU-2")), dict(static_entry(static, "follow_ups", "FU-3"))]


def reorder_rows_and_entries(static):
    rows, entries = static["follow_up_rows"], static["follow_ups"]
    first, second = [rows.index(static_entry(static, "follow_up_rows", number, "no")) for number in ("2 · 3", "4 · 5")]
    rows[first], rows[second] = rows[second], rows[first]
    order = {identifier: entries.index(static_entry(static, "follow_ups", identifier)) for identifier in ("FU-2", "FU-3", "FU-4", "FU-5")}
    moved = [static_entry(static, "follow_ups", identifier) for identifier in ("FU-4", "FU-5", "FU-2", "FU-3")]
    for slot, entry in zip(sorted(order.values()), moved):
        entries[slot] = entry


def shift_range_boundary(static):
    static_entry(static, "user_items", "FQ-7").update(ask="㊻·㊿(W11 #3")
    static_entry(static, "user_items", "FQ-8").update(ask="#6·#9·#11·#12·#13·#17)")


def add_later_table_line(documents, settings):
    last = f"| {settings['static']['user_item_rows'][-1]['no']} |"
    lines = settings["runbook"].split("\n")
    index = next(number for number, line in enumerate(lines) if line.startswith(last))
    settings["runbook"] = "\n".join(lines[:index + 1] + ["", "참고 표(대조 밖):", "| x | y | z |"] + lines[index + 1:])


def check_static_table_parent_cases(log, root):
    list_text = "FU 원래 줄 목록 순서·중복이 실행서 5절 표와 다름"
    expect_code(log, root, "원래 줄과 항목 함께 중복(2 · 3) 종료 1", 1, change_static(duplicate_row_and_entries), expect_text=list_text)
    expect_code(log, root, "원래 줄과 항목 함께 순서 바꿈(2 · 3 ↔ 4 · 5) 종료 1", 1, change_static(reorder_rows_and_entries), expect_text=list_text)
    expect_code(log, root, "범위 이음 경계 이동(FQ-7·FQ-8) 종료 1", 1, change_static(shift_range_boundary), expect_text="FQ 원래 줄 '3~8' 이음표 모호: FQ-7")
    expect_code(log, root, "범위 묶음 항목 맞바꿈(FQ-4·FQ-5) 종료 1", 1, swap_fields("user_items", "FQ-4", "FQ-5", "ask"), expect_text="FQ 원래 줄 '3~8' 조각 ≠ id별 ask")
    expect_code(log, root, "범위 압축 원천 맞바꿈(FQ-4·FQ-6) 종료 1", 1, swap_fields("user_items", "FQ-4", "FQ-6", "source"), expect_text="FQ 원래 줄 '3~8' 원천 조각 ≠ FQ-4 source")
    expect_code(log, root, "범위 원천 접두 펼침(FQ-10 = FU-6) 종료 1", 1, change_static(lambda static: static_entry(static, "user_items", "FQ-10").update(source="FU-6")),
                expect_text="FQ 원래 줄 '9 · 10~12' 원천 조각 ≠ FQ-10 source")
    expect_code(log, root, "범위 섞인 줄의 단일 id 원천(FQ-9) 종료 1", 1, change_static(lambda static: static_entry(static, "user_items", "FQ-9").update(source="DEC:999")),
                expect_text="FQ 원래 줄 '9 · 10~12' 원천 조각 ≠ FQ-9 source")
    expect_code(log, root, "공유 원천 범위(13~19) 하나만 다름 종료 1", 1, change_static(lambda static: static_entry(static, "user_items", "FQ-17").update(source="D10:27")),
                expect_text="FQ 원래 줄 '13~19' 원천 조각 ≠ FQ-17 source")
    expect_code(log, root, "표 뒤 다른 '|' 줄은 대조 밖 종료 0", 0, add_later_table_line)


def check_static_table_gaps(log, root):
    order_text = "FU id 목록 다름: 원래 줄에만 [] · follow_ups에만 []"
    expect_code(log, root, "FU id 순서 바뀜(FU-2·FU-3) 종료 1", 1, change_static(swap_follow_up_order), expect_text=order_text)
    expect_code(log, root, "FU id 중복(FU-2 두 번) 종료 1", 1, change_static(duplicate_follow_up), expect_text=order_text)
    expect_code(log, root, "기록만 묶음 13~19 가운데 FQ-15만 false 종료 1", 1,
                change_static(lambda static: static_entry(static, "user_items", "FQ-15").update(record_only=False)), expect_text="FQ 원래 줄 '13~19' 기록만 표지")
    expect_code(log, root, "표지 없는 줄 FQ-1 record_only true 종료 1", 1,
                change_static(lambda static: static_entry(static, "user_items", "FQ-1").update(record_only=True)), expect_text="FQ 원래 줄 '1' 기록만 표지")
    expect_code(log, root, "FU-2·FU-3 item 맞바꿈 종료 1", 1, swap_fields("follow_ups", "FU-2", "FU-3", "item"), expect_text="FU 원래 줄 '2 · 3' 조각 ≠ id별 item")
    expect_code(log, root, "FU-2·FU-3 source 맞바꿈 종료 1", 1, swap_fields("follow_ups", "FU-2", "FU-3", "source"),
                expect_text="FU 원래 줄 '2 · 3' 원천 조각 ≠ FU-2 source")
    expect_code(log, root, "단일 id 줄 FU-15 source 앞 조각만 종료 1", 1,
                change_static(lambda static: static_entry(static, "follow_ups", "FU-15").update(source=V3_FU15_SOURCE)), expect_text="FU 원래 줄 '15' 원천 조각 ≠ FU-15 source")


def check_static_tables(log, root):
    run = expect_code(log, root, "v3 문안(FU-15 = R16만) 대 v4 실행서 종료 1", 1, v3_fu15(True), expect_text="FU 원래 줄이 실행서 5절 표에 없음: '| 15 | 굽기 변형 R16:")
    log.check("v3 대 v4: 실행서 v4 줄도 static에 없음으로", "실행서 5절 표 줄이 fxp_static follow_up_rows에 없음: '| 15 | 굽기 변형: R16" in problems_text(run), problems_text(run)[:300])
    expect_code(log, root, "원래 줄 v4·FU-15 항목만 v3 종료 1", 1, v3_fu15(False), expect_text="FU 원래 줄 '15' 조각 ≠ id별 item")
    expect_code(log, root, "FQ 묶음(9 · 10~12) 조각 다름 종료 1", 1, change_static(lambda static: static_entry(static, "user_items", "FQ-10").update(ask="D-60 물")),
                expect_text="FQ 원래 줄 '9 · 10~12' 조각")
    expect_code(log, root, "실행서 6절에만 있는 줄 종료 1", 1, add_runbook_fq_row, expect_text="실행서 6절 표 줄이 fxp_static user_item_rows에 없음: '| 21 | 새 질문")
    expect_code(log, root, "FU id 목록 빠짐 종료 1", 1, change_static(lambda static: static.update(follow_ups=[entry for entry in static["follow_ups"] if entry["id"] != "FU-17"])),
                expect_text="FU id 목록 다름: 원래 줄에만 ['FU-17']")
    expect_code(log, root, "기록만 표지 다름 종료 1", 1, change_static(lambda static: static_entry(static, "user_items", "FQ-20").update(record_only=False)), expect_text="기록만 표지")
    expect_code(log, root, "원래 줄 없음 종료 2", 2, change_static(lambda static: static.pop("follow_up_rows")))


def check_rows(log, root):
    expect_code(log, root, "행 거짓 동등(동등 아닌 영역) 종료 1", 1, chain(set_value("rows", ["rows", 2, "equivalent"], True), set_value("rows", ["equivalent_rows"], 2)),
                expect_text="거짓 동등 1")
    expect_code(log, root, "행 빠진 동등 종료 1", 1, chain(set_value("rows", ["rows", 0, "equivalent"], False), set_value("rows", ["equivalent_rows"], 0)), expect_text="빠진 동등 1")
    expect_code(log, root, "행 동등 수 다름 종료 1", 1, set_value("rows", ["equivalent_rows"], 3), expect_text="행 동등 수")
    expect_code(log, root, "공통 행은 동등 아님 종료 1", 1, chain(set_value("rows", ["rows", 3, "area"], "common"), set_value("rows", ["rows", 3, "equivalent"], True),
                                                                  set_value("rows", ["equivalent_rows"], 2)), expect_text="거짓 동등 1")
    expect_code(log, root, "공통 행은 'common' 영역이 동등이어도 제외 종료 0", 0, add_confirmed_common_area)


def add_confirmed_common_area(documents, settings):
    areas = documents["areas"]
    areas["areas_order"].append("common")
    areas["areas"]["common"] = copy.deepcopy(areas["areas"]["water"])


def check_compare_r13_golden_levels(log, root):
    expect_code(log, root, "4.2 공개 열 수치 종료 1", 1, set_value("compare", ["rows", 0, "live2_public"], "PF:159 기준 16.6"), expect_text="공개·환산 열")
    expect_code(log, root, "4.2 같은 기계 열 종료 1", 1, set_value("compare", ["rows", 0, "live2_same_machine"], "측정"), expect_text="같은 기계")
    expect_code(log, root, "4.2 표기 종료 1", 1, set_value("compare", ["rows", 0, "notation"], "상한 비교"), expect_text="4.2 표기")
    expect_code(log, root, "4.2 금지 표현 종료 1", 1, set_value("compare", ["rows", 0, "p_label"], "동일 이상"), expect_text="금지 표현")
    expect_code(log, root, "4.3 표기 셋 밖 종료 1", 1, set_value("r13", ["rows", 2, "label"], "새 측정 결과"), expect_text="4.3 표기")
    expect_code(log, root, "4.3 종류·표기 어긋남 종료 1", 1, set_value("r13", ["rows", 2, "kind"], "p_quote"), expect_text="4.3 표기")
    expect_code(log, root, "4.3 LIVE-2 열 종료 1", 1, set_value("r13", ["rows", 0, "live2"], "비공개"), expect_text="4.3 LIVE-2")
    expect_code(log, root, "4.6 골든 줄 다름 종료 1", 1, set_value("golden", ["discrete_adoptions"], [{"id": "1"}]), expect_text="골든 줄")
    expect_code(log, root, "4.6 골든 채택 수 ≠ areas 종료 1", 1, set_value("areas", ["totals", "adoption_entries"], 3), expect_text="골든 채택 수")
    expect_code(log, root, "4.6 형식 집계 어긋남 종료 1", 1, set_value("golden", ["type_counts", "열거"], 1), expect_text="형식 집계")
    expect_code(log, root, "4.4 excluded 근거 없음 종료 1", 1, set_value("levels", ["excluded", "L.3.7", "evidence"], ""), expect_text="excluded 근거")


def synthetic_tables(rows, entries):
    lines = ["## 5. FU", "", "## 6. FQ", "", "| FQ | 묻는 것 | 근거 |", "|---|---|---|"] + [f"| {no} | {text} | {source} |" for no, text, source in rows] + ["", "## 7. 끝"]
    static = {"follow_up_rows": [], "follow_ups": [], "user_item_rows": [{"no": no, "ask": text, "source": source} for no, text, source in rows],
              "user_items": [{"id": identifier, "ask": text, "source": source, "record_only": False} for identifier, text, source in entries]}
    return "\n".join(lines) + "\n", static


def table_case(log, case, rows, entries, expect_text=None):
    problems = report.check_static_tables(*synthetic_tables(rows, entries))
    ok = not problems if expect_text is None else any(expect_text in problem for problem in problems)
    log.check(case, ok, str(problems)[:300])


def check_joiner_pins(log):
    table_case(log, "[회귀 고정] 여러 묶음 줄 단일 id 글자에 괄호 밖 ' · ' → 모호", [("1 · 2", "A · B · C", "S · T")], [("FQ-1", "A · B", "S"), ("FQ-2", "C", "T")],
               "FQ 원래 줄 '1 · 2' 이음표 모호: FQ-1")
    table_case(log, "[회귀 고정] 닫힌 괄호 뒤 이음표 'A(b)·C' → 모호", [("1~2", "A(b)·C·D", "S")], [("FQ-1", "A(b)·C", "S"), ("FQ-2", "D", "S")],
               "FQ 원래 줄 '1~2' 이음표 모호: FQ-1")
    table_case(log, "[회귀 고정] 짝 없는 닫는 괄호 뒤 이음표 'x)·d' → 모호", [("1~2", "x)·d·e", "S")], [("FQ-1", "x)·d", "S"), ("FQ-2", "e", "S")],
               "FQ 원래 줄 '1~2' 이음표 모호: FQ-1")
    table_case(log, "[회귀 고정] 범위 원천 펼침 불가 → 묶음 말고 줄 전체 생략(단일 id 원천도 대조 안 함)", [("1 · 2~3", "X · A·B", "P · Q·R·S")],
               [("FQ-1", "X", "WRONG"), ("FQ-2", "A", "Q"), ("FQ-3", "B", "R")])
    table_case(log, "[회귀 고정] 범위 원천 접두 없음 → 줄 생략", [("1~2", "A·B", "가·나")], [("FQ-1", "A", "가·나"), ("FQ-2", "B", "가·나")])
    table_case(log, "[회귀 고정] 범위 원천 조각 수 ≠ id 수 → 줄 생략", [("1~2", "A·B", "D:1·2·3")], [("FQ-1", "A", "D:1·2·3"), ("FQ-2", "B", "D:1·2·3")])
    table_case(log, "[회귀 고정] 원천 ' · ' 조각 수 ≠ 묶음 수 → 줄 생략", [("1 · 2", "X · Y", "P · Q · R")], [("FQ-1", "X", "WRONG"), ("FQ-2", "Y", "WRONG")])


def check_joiner_latent(log):
    table_case(log, "id 글자 앞뒤 공백(번호 나열을 공백 낀 붙여 잇기로) → 문제", [("1~2", "1 A · 2 B", "S")], [("FQ-1", "1 A ", "S"), ("FQ-2", " 2 B", "S")],
               "FQ 원래 줄 '1~2' FQ-1 ask 앞뒤 공백")
    table_case(log, "[회귀 고정] 번호 나열 참값(공백 없음) 통과", [("1~2", "1 A · 2 B", "S")], [("FQ-1", "A", "S"), ("FQ-2", "B", "S")])
    table_case(log, "붙여 잇기 이음표가 괄호 안('A(x·y' + 'z)') → 이음표 수 문제", [("1~2", "A(x·y·z)", "S")], [("FQ-1", "A(x·y", "S"), ("FQ-2", "z)", "S")],
               "FQ 원래 줄 '1~2' 붙여 잇기 이음표 수: FQ-1~2 괄호 밖 '·' 0개 ≠ id 수−1 1")
    table_case(log, "짝 없는 여는 괄호가 이음표를 가림('A(B·C' + 'D') → 이음표 수 문제", [("1~2", "A(B·C·D", "S")], [("FQ-1", "A(B·C", "S"), ("FQ-2", "D", "S")],
               "FQ 원래 줄 '1~2' 붙여 잇기 이음표 수")
    table_case(log, "[회귀 고정] 붙여 잇기 괄호 안 이음표만(㊿(W11 #3·#6)) 통과", [("1~2", "㊻·㊿(W11 #3·#6)", "DEC:498·499")], [("FQ-1", "㊻", "DEC:498"), ("FQ-2", "㊿(W11 #3·#6)", "DEC:499")])


def notation_rows(documents, settings):
    compare = documents["compare"]
    base = compare["rows"][0]
    compare["rows"] += [dict(base, area="smoke", row="HV2/ft", d={"median": None, "interval": None}, p95_record_median=0.97, p_label="판정 불가(불안정)", p_rule="judge.json"),
                        dict(base, area="fog", row="FO3/ft", d={"median": None, "interval": None}, p95_record_median=None, p_label="통과", p_rule="FO3 SM2/ft 통과(B17)",
                             derived_from="SM2/ft", live2_public="PF 줄 없음(한도 원천이 PF 밖)"),
                        dict(base, area="fog", row="FO9/ft", d={"median": 0.5, "interval": None})]
    documents["levels"]["counts"].update(strict_mean=0.4757, strict_items=110, items=294)


def md_line(run, prefix):
    return next((line for line in run.markdown.splitlines() if line.startswith(prefix)), "")


def check_notation(log, root):
    run = expect_code(log, root, "[회귀 고정] 표기 사례(d 없음·P 규칙 대신 판정·레벨 평균·합산) 종료 0", 0, notation_rows)
    hv2, fo3, fo9 = md_line(run, "| smoke | HV2/ft |"), md_line(run, "| fog | FO3/ft |"), md_line(run, "| fog | FO9/ft |")
    log.check("2절 d 없는 행 '—'(None 구간 아님)", hv2.startswith("| smoke | HV2/ft | ft | — | — | 0.97 |") and "None" not in hv2, hv2)
    log.check("2절 P 규칙 대신 판정 → 표기 칸에 규칙·원천 행·측정 없음", fo3.endswith(f"| {report.COMPARE_NOTATION} · P 규칙 FO3 SM2/ft 통과(B17), SM2/ft 행이 대신 판정, 이 행 측정 없음 |")
              and fo3.startswith("| fog | FO3/ft | ft | — |"), fo3)
    log.check("[회귀 고정] 2절 기본 규칙(judge.json) 행은 표기 그대로", hv2.endswith(f"| {report.COMPARE_NOTATION} |"), hv2)
    log.check("2절 구간 없는 중앙값은 값만", fo9.startswith("| fog | FO9/ft | ft | 0.5 |"), fo9)
    notes = {row.get("row"): row.get("rule_note") for row in ((run.document or {}).get("compare") or {}).get("rows") or []}
    log.check("2절 JSON rule_note(FO3만)", notes == {"FI1/ft": None, "HV2/ft": None, "FO3/ft": "P 규칙 FO3 SM2/ft 통과(B17), SM2/ft 행이 대신 판정, 이 행 측정 없음", "FO9/ft": None}, f"{notes}")
    log.check("5절 레벨 평균·합산 이름 붙여 병기", "엄격(셋업 항목) 레벨 평균 0.476 · 합산 110/294 = 0.374" in run.markdown, md_line(run, "- 느슨"))


def without_lesson(documents, settings):
    settings["static"].pop("lesson_line", None)


def with_lesson(value):
    def mutate(documents, settings):
        settings["static"]["lesson_line"] = value
    return mutate


def check_lesson_line(log, root):
    run = expect_code(log, root, "[회귀 고정] 교훈 키 없음(하위 호환) 종료 0", 0, without_lesson)
    lines = (run.public_bytes or b"").decode("utf-8").splitlines()
    log.check("[회귀 고정] 교훈 키 없음 → 고정 '–' 문구", lines[1:2] == [report.LESSON_LINE], str(lines[:3]))
    run = expect_code(log, root, "[회귀 고정] 교훈 키 있음 종료 0", 0, with_lesson("L-x-1(합성 교훈)"))
    lines = (run.public_bytes or b"").decode("utf-8").splitlines()
    log.check("교훈 키 있음 → '교훈: <lesson_line>'", lines[1:2] == ["교훈: L-x-1(합성 교훈)"], str(lines[:3]))
    expect_code(log, root, "교훈 키 빈 글자 종료 2", 2, with_lesson(""))


def unexpandable_rows(documents, settings):
    static = settings["static"]
    static_entry(static, "follow_up_rows", "2 · 3", "no")["source"] += " · 덧붙임"
    static_entry(static, "user_item_rows", "3~8", "no")["source"] = "DEC:489·493"
    settings["runbook"] = runbook_text(static)


def check_skipped_sources(log, root):
    run = expect_code(log, root, "[회귀 고정] 원천 펼칠 수 없는 줄 2개는 경고만 종료 0", 0, unexpandable_rows)
    skipped = [(entry.get("table"), entry.get("no"), entry.get("reason")) for entry in (run.document or {}).get("static_tables_skipped") or []]
    log.check("생략 줄 JSON static_tables_skipped(표·번호·사유)", skipped == [("FU", "2 · 3", "조각 수 ≠ 묶음 수"), ("FQ", "3~8", "범위 원천을 펼칠 수 없음(조각 수 ≠ id 수·접두 없음)")],
              f"{skipped}")
    log.check("생략 줄 md 11절 경고(줄 수·번호)", "FU·FQ id별 원천 대조 생략 2줄(그 줄 전체) — FU '2 · 3' 조각 수 ≠ 묶음 수 · FQ '3~8'" in run.markdown,
              md_line(run, "- 경고"))


def run(tmp_root):
    log = FixtureLog("report")
    root = Path(tmp_root)
    check_normal(log, root)
    check_input_errors(log, root)
    check_runbook_sha(log, root)
    check_d49(log, root)
    check_chain_and_inputs(log, root)
    check_areas(log, root)
    check_equivalence_conditions(log, root)
    check_static_tables(log, root)
    check_static_table_gaps(log, root)
    check_static_table_parent_cases(log, root)
    check_rows(log, root)
    check_compare_r13_golden_levels(log, root)
    check_joiner_pins(log)
    check_joiner_latent(log)
    check_notation(log, root)
    check_lesson_line(log, root)
    check_skipped_sources(log, root)
    return log.result()

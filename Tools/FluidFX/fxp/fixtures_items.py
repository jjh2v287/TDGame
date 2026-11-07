"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 사본에서 fxp_audit.py items 를 돌려 정상(분류·측정 불가 묶음·연결)·같은 바이트·package 분기 3종·굽기 3종(R2 일지 최종·R16 계획별 최종·creek 결과 없음)·인용 변이·종료 1(분류 없음·근거 줄·모르는 FU·모르는 관련 ID·package 형식·R2 상태 다름·R2 상태 같고 이유 다름)·종료 2(입력 없음·모르는 check)를 본다.
상태: 2026-11-07 FX-P X1a(B1), fix1(빌더 A) reason_contains 불일치 사례 추가.
"""
from pathlib import Path

from check_fxp_fixtures import FixtureLog, run_audit, write_json, write_text

P_SUMMARY = "Saved/AgentOps/20261106/p/p-summary.json"
FXP_STATIC = "Tools/FluidFX/fxp/fxp_static.json"
ITEMS_STATIC = "Tools/FluidFX/fxp/items_static.json"
DEV10 = "Saved/AgentOps/20261106/p/dev-10.md"
SPEC = "Saved/AgentOps/20261030/cal0/spec-cal0-v2.md"
QUOTE = "Q2 표시 없는 채택도 되돌림 후보(기본 넓히지 않음)"
UNMET = "미충족(통과 0 / 미달 1 / 행 1 / 판정 불가 0 / 측정 불가 0 / 출하 밖 0)"
PARTIAL = "부분(통과 1 / 행 2 / 판정 불가 1 / 측정 불가 2 / 출하 밖 0)"
EXPECTED_SUMMARY = ("SUMMARY items=16 closed=0 record=7 follow_up=4 user=5 unmeasurable=3 non_target=1 feature_gap=1 other=1 lines_unique=4 records_unique=2 problems=0")


def evidence(file, line, expect):
    return {"file": file, "line": line, "expect": expect}


def journal(acquired, items):
    return {"queue_sha256": "0" * 64, "holds": [{"owner": "fixture", "acquired_at": acquired}], "real_dt": 0.05,
            "items": [{"plan": f"Tools/FluidFX/calib/plans/{plan}.json", "status": status, "reason": reason, "runs": {}} for plan, status, reason in items]}


def static_items():
    return [
        {"id": "4", "title": "미충족 영역", "class": "FQ-1", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "unmet_areas"}]},
        {"id": "6", "title": "측정 불가", "class": "FU-10", "refs": [], "evidence": [evidence(DEV10, 2, "Q2")], "checks": [{"kind": "unmeasurable"}]},
        {"id": "7", "title": "R13 밖", "class": "FU-12", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "ledger_lines", "ids": ["S040"]}]},
        {"id": "11", "title": "같은 기계", "class": "FQ-1", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "unassigned_items"}]},
        {"id": "13", "title": "PresetData 첫 읽기", "class": "기록", "refs": [], "closed_by": "package", "evidence": [evidence(DEV10, 1, "# 편차")],
         "checks": [{"kind": "package", "tags": ["min", "fluid"], "closed_verdict": "첫 읽기 확인", "missing": "미검증(X5 전)"}]},
        {"id": "17", "title": "WA6", "class": "FU-1", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "fixed_item", "id": "WA6 ㉙"}, {"kind": "perf_candidates"}]},
        {"id": "18", "title": "구조 격차", "class": "기록", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "structural_gaps"}]},
        {"id": "19", "title": "승인 전용", "class": "FQ-1", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "approval_only"}]},
        {"id": "21a", "title": "굽기 R2", "class": "기록", "refs": [], "evidence": [evidence(SPEC, 1, "K1 fx8n_embers")],
         "checks": [{"kind": "journal_final", "plan": "fx8n_embers", "status": "insensitive", "reason_contains": "screen 둔감"}]},
        {"id": "21b", "title": "굽기 R16", "class": "FU-15", "refs": [], "evidence": [evidence(SPEC, 2, "R16 이관")], "checks": [{"kind": "journal_finals", "plans": ["fog_f1_layer", "track_sand_h4"]}]},
        {"id": "21c", "title": "creek", "class": "기록", "refs": [], "evidence": [evidence(SPEC, 3, "creek")], "checks": [{"kind": "journal_none", "plan": "w6_dense_creek"}]},
        {"id": "26", "title": "P-20", "class": "기록", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "variant_finals", "suffix": "-p20"}]},
        {"id": "27", "title": "판정 대기", "class": "FQ-1", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "pending_judgements"}]},
        {"id": "28", "title": "화면 밖 정지", "class": "기록", "refs": [], "evidence": [evidence(DEV10, 3, "S094·S095")], "checks": [{"kind": "ledger_lines", "ids": ["S094", "S095"], "class": "출하 밖"}]},
        {"id": "29", "title": "반대 backend", "class": "기록", "refs": [], "evidence": [evidence(DEV10, 1, "# 편차")], "checks": [{"kind": "opposite_backend"}, {"kind": "records"}]},
        {"id": "36", "title": "dev-10 Q", "class": "FQ-13", "refs": ["FQ-1"], "evidence": [evidence(DEV10, 2, "Q2")]}]


def world_files():
    files = {FXP_STATIC: {"follow_ups": [{"id": "FU-1", "item": "WA6", "source": "s"}, {"id": "FU-10", "item": "측정 경로", "source": "s"}, {"id": "FU-12", "item": "R13s", "source": "s"},
                                         {"id": "FU-15", "item": "R16", "source": "s"}],
                          "user_items": [{"id": "FQ-1", "ask": "승인", "source": "s"}, {"id": "FQ-13", "ask": QUOTE, "quote": QUOTE, "source": "D10:26", "record_only": True}],
                          "environment_note": "Test 빌드 불가", "quote_source": {"file": DEV10, "line": 2}},
             ITEMS_STATIC: {"unmeasurable_groups": {"비대상": "기록", "기능 공백": "FU-10", "기타": "기록"},
                            "unmeasurable_rules": [{"group": "기타", "sub": "1회성 트리거", "pattern": "^1회성"}, {"group": "비대상", "sub": "메모리 경로 없음", "pattern": "^메모리 경로 없음"},
                                                   {"group": "기능 공백", "sub": "장면·무대 없음", "pattern": "(장면 없음|무대)"}],
                            "items": static_items()},
             P_SUMMARY: {"areas": {"fire": {"overall": UNMET, "lines": [{"id": "S040", "class": "측정 불가", "disposition": "측정 불가"}, {"id": "S011", "class": "미달", "disposition": "행"}],
                                            "records": [{"id": "S059"}, {"id": "S059"}], "rows": [{"row": "FI1/gt", "verdict": "판정 불가"}, {"row": "FI2/gt", "verdict": "미달"}]},
                                   "smoke": {"overall": PARTIAL, "lines": [{"id": "S094", "class": "출하 밖", "disposition": "출하 밖"}, {"id": "S095", "class": "출하 밖", "disposition": "출하 밖"}],
                                             "records": [{"id": "S060"}], "rows": []}},
                         "fxp_items": {"unmeasurable": [{"area": "fire", "id": "S010", "reason": "메모리 경로 없음"}, {"area": "input", "id": "S039", "reason": "무대 없음"},
                                                        {"area": "fire", "id": "S014", "reason": "1회성 트리거, ABBA 불가"}],
                                       "fixed": [{"id": "WA6 ㉙", "area": "water", "text": "WA6는 FX-P 앞 별도"}],
                                       "opposite_backend": [{"id": "S001", "area": "smoke", "reason": "출하 프리셋 CPU"}],
                                       "unassigned_items": [{"id": "S097", "area": "(FX-P)", "disposition": "측정 불가", "reason": "같은 기계 없음"}]},
                         "pending_judgements": {"source": "PB2:230", "items": ["W-2s-a", "MeshArray"]}},
             "Saved/FluidFX/calib/batch/journal-K1.json": journal("2026-10-31T10:00:00", [("fx8n_embers", "insensitive", "screen 둔감(민감 손잡이 0)"), ("fog_f1_layer", "pending", "")]),
             "Saved/FluidFX/calib/batch-K2/journal.json": journal("2026-10-31T11:00:00", [("fx8n_embers", "pending", ""), ("w6_dense_creek", "pending", "")]),
             "Saved/FluidFX/calib/batch-R5/journal.json": journal("2026-11-01T12:00:00", [("fog_f1_layer", "adopted", "accept"), ("track_sand_h4", "no_adopt", "screen best 없음")]),
             "Saved/FluidFX/calib/batch-P20a/journal.json": journal("2026-11-01T13:00:00", [("fx5_flamethrower-p20", "adopted", "accept")]),
             "Tools/FluidFX/calib/approval-only.json": {"items": [{"plan": "a", "kind": "target", "destination": "승인 시트 뒤(deferred)"}, {"plan": "b", "kind": "scene", "destination": "승인 시트"}]},
             "Tools/FluidFX/calib/structural-gaps.json": {"gaps": [{"plan": "c", "kind": "plan", "destination": "없음"}]},
             "Saved/FluidFX/calib/perf-candidates.json": {"items": [{"plan": "Tools/FluidFX/calib/plans/w1b_water_particles.json"}]}}
    return files


TEXT_FILES = {DEV10: "# 편차\n| 10-14 | " + QUOTE + " · Q10 같은 기계 |\n| 10-10 | 화면 밖 정지 S094·S095 |\n",
              SPEC: "| R2 | K1 fx8n_embers 끝값 이미지 차 |\n**R16 이관(K10 굽기 변형)**\n| creek '굽기 의존' 손잡이 |\n"}


def make_world(root, files, texts=None):
    for relative, value in files.items():
        write_json(root, relative, value)
    for relative, text in (texts or TEXT_FILES).items():
        write_text(root, relative, text)
    return root


def item_of(document, item_id):
    return next((item for item in (document or {}).get("items", []) if item["id"] == item_id), {})


def check_normal(log, root):
    run = run_audit(root, "items", "normal")
    document = run.document or {}
    log.check("정상 종료 0", run.code == 0, f"종료 {run.code} {run.stderr.strip()[:300]} {run.stdout.strip()[-400:]}")
    log.check("정상 SUMMARY", EXPECTED_SUMMARY in run.markdown.splitlines()[-1:], f"{run.markdown.splitlines()[-1:] if run.markdown else run.stderr[:200]}")
    groups = (document.get("unmeasurable") or {}).get("groups", {})
    log.check("측정 불가 묶음(비대상 = 기록·기능 공백 = FU-10·기타 = 기록)", {group: (info["n"], info["class"]) for group, info in groups.items()} == {"비대상": (1, "기록"), "기능 공백": (1, "FU-10"), "기타": (1, "기록")}, f"{groups}")
    package = item_of(document, "13")
    log.check("package 없음 → 미검증(X5 전)·기록", package.get("class") == "기록" and package.get("detail", {}).get("package", {}).get("verdicts") == {"min": "미검증(X5 전)", "fluid": "미검증(X5 전)"}, f"{package}")
    r2 = item_of(document, "21a").get("detail", {}).get("journal_final", {}).get("final") or {}
    log.check("굽기 R2: K1 insensitive, 뒤 K2 pending 무시", r2.get("status") == "insensitive" and r2.get("batch") == "K1", f"{r2}")
    r16 = item_of(document, "21b").get("detail", {}).get("journal_finals", {})
    log.check("굽기 R16: 계획별 최종", {plan: (final or {}).get("status") for plan, final in r16.items()} == {"fog_f1_layer": "adopted", "track_sand_h4": "no_adopt"}, f"{r16}")
    creek = item_of(document, "21c").get("detail", {}).get("journal_none", {})
    log.check("굽기 creek: 결과 없음", creek.get("final") is None, f"{creek}")
    variants = item_of(document, "26").get("detail", {}).get("variant_finals", {})
    log.check("-p20 변형 최종", list(variants) == ["fx5_flamethrower-p20"], f"{variants}")
    linked = next((entry for entry in document.get("user_items", []) if entry["id"] == "FQ-1"), {}).get("linked_items")
    log.check("FQ 연결 항목(class·refs)", linked == ["4", "11", "19", "27", "36"], f"{linked}")
    ledger = document.get("ledger", {})
    log.check("장부 줄·기록 중복 제거", ledger == {"lines_total": 4, "lines_unique": 4, "records_total": 3, "records_unique": ["S059", "S060"]}, f"{ledger}")
    log.check("판정 불가 행 파생", document.get("derived", {}).get("undecidable_rows") == ["FI1/gt"], f"{document.get('derived')}")
    second = run_audit(root, "items", "normal-second")
    log.check("같은 입력 같은 바이트(json·md, --out 다름)", run.json_bytes == second.json_bytes and run.md_bytes == second.md_bytes and run.json_bytes is not None, "바이트 다름")


def variant(root, name, change=None, texts=None):
    files = world_files()
    if change:
        change(files)
    return make_world(root / name, files, texts)


def add_packages(verdicts):
    def change(files):
        for tag, verdict in verdicts.items():
            files[f"Saved/FluidFX/fxp/package-{tag}.json"] = {"tag": tag, "verdict": verdict} if verdict is not None else {"tag": tag}
    return change


def add_unmatched_reason(files):
    files[P_SUMMARY]["fxp_items"]["unmeasurable"].append({"area": "fog", "id": "S999", "reason": "새 사유"})


def later_embers_failure(files):
    files["Saved/FluidFX/calib/batch-R6/journal.json"] = journal("2026-11-02T10:00:00", [("fx8n_embers", "failed", "판정 종료 1")])


def later_embers_other_insensitive(files):
    files["Saved/FluidFX/calib/batch-R6/journal.json"] = journal("2026-11-02T10:00:00", [("fx8n_embers", "insensitive", "확인 둔감(잡음 안)")])


def creek_result(files):
    files["Saved/FluidFX/calib/batch-R5/journal.json"]["items"].append({"plan": "Tools/FluidFX/calib/plans/w6_dense_creek.json", "status": "failed", "reason": "x", "runs": {}})


def unknown_class(files):
    files[ITEMS_STATIC]["items"][1]["class"] = "FU-99"


def unknown_ref(files):
    files[ITEMS_STATIC]["items"][-1]["refs"] = ["FQ-77"]


def unknown_check(files):
    files[ITEMS_STATIC]["items"][0]["checks"] = [{"kind": "no_such_check"}]


def mutated_quote(files):
    files[FXP_STATIC]["user_items"][1]["quote"] = "Q2 표시 없는 채택은 그대로 유지"


def check_variants(log, root):
    closed = run_audit(variant(root, "pkg-closed", add_packages({"min": "첫 읽기 확인", "fluid": "첫 읽기 확인"})), "items", "pkg-closed")
    log.check("package 둘 다 첫 읽기 확인 → 닫힘", closed.code == 0 and item_of(closed.document, "13").get("class") == "닫힘", f"종료 {closed.code} {item_of(closed.document, '13')}")
    failed = run_audit(variant(root, "pkg-failed", add_packages({"min": "첫 읽기 확인", "fluid": "첫 읽기 실패(③)"})), "items", "pkg-failed")
    log.check("package 하나 실패 → 기록", failed.code == 0 and item_of(failed.document, "13").get("class") == "기록", f"종료 {failed.code} {item_of(failed.document, '13')}")
    texts = dict(TEXT_FILES)
    texts[DEV10] = TEXT_FILES[DEV10].replace("S094·S095", "S094")
    cases = (("pkg-bad", add_packages({"min": None, "fluid": "첫 읽기 확인"}), None, "package 형식: Saved/FluidFX/fxp/package-min.json"),
             ("unmatched", add_unmatched_reason, None, "측정 불가 분류 없음: fog S999"),
             ("evidence", None, texts, "근거 줄 불일치: 항목 28"),
             ("r2-final", later_embers_failure, None, "일지 최종 불일치: fx8n_embers"),
             ("r2-reason", later_embers_other_insensitive, None, "일지 최종 불일치: fx8n_embers 기대 insensitive·'screen 둔감' 실제 insensitive"),
             ("creek", creek_result, None, "일지 최종이 있으면 안 됨: w6_dense_creek"),
             ("class", unknown_class, None, "분류 없음: 항목 6 class 'FU-99'"),
             ("ref", unknown_ref, None, "모르는 관련 ID: 항목 36 FQ-77"),
             ("quote", mutated_quote, None, "FQ 인용 불일치: FQ-13"))
    for name, change, case_texts, text in cases:
        result = run_audit(variant(root, name, change, case_texts), "items", name)
        problems = (result.document or {}).get("problems", [])
        log.check(f"종료 1 {name}", result.code == 1 and any(text in problem for problem in problems), f"종료 {result.code} {problems} {result.stderr[:200]}")
    check_error = run_audit(variant(root, "check-kind", unknown_check), "items", "check-kind")
    log.check("종료 2 모르는 check", check_error.code == 2 and check_error.json_bytes is None, f"종료 {check_error.code}")
    files = world_files()
    del files[ITEMS_STATIC]
    missing = run_audit(make_world(root / "missing", files), "items", "missing")
    log.check("종료 2 items_static 없음", missing.code == 2 and missing.json_bytes is None, f"종료 {missing.code}")


def run(tmp_root):
    log = FixtureLog("items")
    root = Path(tmp_root)
    check_normal(log, make_world(root / "normal", world_files()))
    check_variants(log, root)
    return log.result()

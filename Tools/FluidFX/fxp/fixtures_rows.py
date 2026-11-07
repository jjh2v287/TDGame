"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 사본에서 fxp_audit.py rows 를 돌려 정상(시험 3종 통과·test_failing·test_unverified, 접두 앵커, PD-16 강등, 표지, 점 열쇠 데이터 경로, 행 동등, W-8 닫힘)·같은 바이트·--no-pass-list 강등·--areas·W-8 열림·LIVE-2 뺀 note·PD-16 보강(데이터+판정+미확인 시험 강등·행 동등은 강등 뒤 상태·같은 시험 결과 여럿 중 실패·계획 칸 섞임 blocked_calib)·종료 1(스키마·행 집합·d52·해석 실패·근거 부족·note D-49·이미지·live2_reference·모르는 FQ)·종료 2(입력 없음·형식·인수)를 본다.
상태: 2026-11-07 FX-P X2(B3), fix1 빌더 B가 PD-16 보강 4사례 추가(변이 rows-unverified-not-demoted·equivalent-from-ledger-status·last-result-only·blocked-marker-all).
"""
import copy

from check_fxp_fixtures import FixtureLog, run_audit, write_json, write_text

LEDGER = "Docs/FluidFX/live2_evidence.json"
ROADMAP = "Saved/AgentOps/20261019/fluidfx/parity2/roadmap-final.md"
AREAS = "Saved/FluidFX/fxp/areas.json"
AREAS_COPY = "Saved/AgentOps/fixture/x1/try-areas.json"
FXP_STATIC = "Tools/FluidFX/fxp/fxp_static.json"
HEADLESS = "Saved/AgentOps/20261106/w13-headless-tests.json"
RHI = "Saved/AgentOps/20261106/w13-rhi-tests.json"
EARLIER = "Saved/AgentOps/20261106/w13-headless-earlier.json"
CORE_HEADER = "Source/TDGame/FluidFX/TDFixtureCore.h"
TESTS_SOURCE = "Source/TDGame/FluidFX/Tests/TDFixtureTests.cpp"
EDITOR_TESTS = "Source/TDGameEditor/FluidFX/TDFixtureEditorTests.cpp"
TUNING = "Content/FluidFX/PresetData/TDFluidPresetTuning.json"
PLAN = "Tools/FluidFX/calib/plans/fx9_floor_smoke.json"
RETIRED_PLAN = "Tools/FluidFX/calib/retired/fog_f9_old.json"
JUDGE = "Saved/FluidFX/calib/batch/runs/fx9_floor_smoke/screen-1/screen.json"
SPEC = "Saved/AgentOps/fixture/spec-rows.md"
REFERENCE_FILE = "Saved/FluidFX/live2_reference/notes.txt"
PASS_ARGS = ("--pass-list", HEADLESS, "--pass-list", RHI)
UNMET = "미충족(통과 0 / 미달 1 / 행 1 / 판정 불가 0 / 측정 불가 3 / 출하 밖 0)"
PARTIAL = "부분(통과 1 / 행 2 / 판정 불가 0 / 측정 불가 2 / 출하 밖 0)"
PASS = "충족(proposed)"
ROADMAP_LINES = ["# 로드맵", "", "## 2. LIVE-2 전 영역 대조표", "", "| ID | 기능 | LIVE-2 | 우리 현재 | 격차 | 수단 | 목표 | 단계 |", "|---|---|---|---|---|---|---|---|",
                 "| A1 | 코어 | 셋 | 있음: 솔버 | — | — | — | 완료 |", "| B1 | 입력 | 붓 | 부분: 붓 | — | — | — | FX-15 |",
                 "| B2 | 편집기 입력 | 모드 | 없음 | — | — | — | FX-16 |", "| D1 | 연기 값 | 값 | 없음 | — | — | — | FX-9 |",
                 "| D2 | 연기 묶음 | 묶음 | 부분: 일부 | — | — | — | FX-9 |", "| E1 | 불 방식 | 방식 | 다름: 방식 | — | — | — | FX-5 |",
                 "| L18b | 수면선 | 미지원 | 없음 | LIVE-2도 미지원 | 범위 밖 | — | W-8 |", "| L19 | 부력 | 미지원 | 없음 | LIVE-2도 미지원 | 범위 밖 | — | W-8 |",
                 "| S8 | 멀티 | 복제 없음 | 해당 없음: 복제 없음 | 동등 | — | — | 완료 |", "| T1 | 우리만 | — | 있음 | — | — | — | — |", "",
                 "## 3. 로드맵", "", "| Z1 | 밖 | — | 있음 | — | — | — | — |"]
SPEC_LINES = ["# 명세", "멀티플레이 복제 없음", "물 부력은 미지원, 대안만 적음"]
SYMBOL = {"file": CORE_HEADER, "regex": r"class TDGAME_API UTDFixtureCore\b"}
DEFAULT_OFF = {"file": CORE_HEADER, "regex": r"bool bFixtureFeature = false;"}
ANCHOR_KEYS = ("symbols", "tests", "data", "plans", "judgments", "docs")
EXPECTED_SUMMARY = ("SUMMARY rows=9 present=2 partial=3 different=1 absent=0 not_applicable=3 live2_unsupported=2 demoted=2 default_off=1 approval_open=5 "
                    "performance=5 test_failing=1 test_unverified=2 blocked_calib=1 user_decision_open=1 common=2 w8=closed equivalent_rows=1 pass_list=on problems=0")
EXPECTED_NO_PASS = ("SUMMARY rows=9 present=1 partial=4 different=1 absent=0 not_applicable=3 live2_unsupported=2 demoted=3 default_off=1 approval_open=5 "
                    "performance=5 test_failing=0 test_unverified=5 blocked_calib=1 user_decision_open=1 common=2 w8=closed equivalent_rows=1 pass_list=off problems=0")
EXPECTED_MARKERS = {"A1": ["공통(영역 승인 밖)"], "B1": ["승인 대기", "성능 미충족", "test_failing"], "B2": ["승인 대기", "성능 미충족", "test_unverified", "user_decision_open"],
                    "D1": ["기본 꺼짐", "blocked_calib"], "D2": [], "E1": ["승인 대기", "성능 부분(막는 측정 불가 2)", "test_unverified"], "L18b": ["거부", "성능 미충족"],
                    "L19": ["거부", "성능 미충족"], "S8": ["공통(영역 승인 밖)"]}
EXPECTED_STATUS = {"A1": "있음", "B1": "부분", "B2": "부분", "D1": "있음", "D2": "부분", "E1": "다름", "L18b": "해당 없음(LIVE-2 미지원)", "L19": "해당 없음(LIVE-2 미지원)",
                   "S8": "해당 없음"}


def roadmap_line(row_id):
    return next(index for index, line in enumerate(ROADMAP_LINES, 1) if line.startswith(f"| {row_id} |"))


def ledger_row(row_id, d52, status, note="", default_off=None, user_items=None, **anchors):
    return {"id": row_id, "d52": d52, "status": status, "stages": [], "default_off": default_off, "user_items": list(user_items or []),
            "anchors": {kind: list(anchors.get(kind, [])) for kind in ANCHOR_KEYS}, "note": note}


def ledger_rows():
    tuning = [{"file": TUNING, "json_path": path} for path in ("Groups.Smoke.Density", "Materials./Game/FluidFX/M_Fixture.M_Fixture.Look", "List.0.Value")]
    return [ledger_row("A1", "있음", "있음", symbols=[SYMBOL], tests=["TDGame.FluidFX.Core.Pass"]),
            ledger_row("B1", "부분", "있음", symbols=[SYMBOL], tests=["TDGame.FluidFX.Core.Pass", "TDGame.FluidFX.Input.Fails"]),
            ledger_row("B2", "없음", "있음", user_items=["FQ-1"], symbols=[SYMBOL], tests=["TDGameEditor.FluidFX.Mode.Activation"]),
            ledger_row("D1", "없음", "있음", default_off=DEFAULT_OFF, data=tuning, plans=["fx9_floor_smoke"], judgments=[JUDGE]),
            ledger_row("D2", "부분", "부분", note="접두 묶음만 확인", tests=["TDGame.FluidFX.Pre.", "TDGame.FluidFXRhi.Smoke.Gpu"], plans=["fog_f9_old"]),
            ledger_row("E1", "다름", "다름", note="다른 방식", tests=["TDGame.FluidFX.Mixed."]),
            ledger_row("L18b", "없음", "해당 없음", docs=[{"file": ROADMAP, "line": roadmap_line("L18b")}]),
            ledger_row("L19", "없음", "해당 없음", docs=[{"file": ROADMAP, "line": roadmap_line("L19")}]),
            ledger_row("S8", "해당 없음", "해당 없음", note="복제 없음 그대로", docs=[{"file": SPEC, "line": 2}])]


def area(state, date, performance, blocking, confirmed):
    return {"approval": {"state": state, "date": date, "evidence": "Saved/FluidFX/approval/x/current.json"}, "performance": performance,
            "blocking_unmeasurable": blocking, "equivalence": {"confirmed": confirmed, "label": "fixture"}}


def test_source(names):
    return "".join(f'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTDFixture{index}, "{name}", EAutomationTestFlags::EngineFilter)\n' for index, name in enumerate(names))


def results(entries):
    return [{"path": path, "result": result, "events": []} for path, result in entries]


def world_files(root):
    headless = [("TDGame.FluidFX.Core.Pass", "Success"), ("TDGame.FluidFX.Input.Fails", "Fail"), ("TDGame.FluidFX.Pre.One", "Success"),
                ("TDGame.FluidFX.Pre.Two", "Success"), ("TDGame.FluidFX.Mixed.One", "Success")]
    return {ROADMAP: "\n".join(ROADMAP_LINES) + "\n", SPEC: "\n".join(SPEC_LINES) + "\n",
            CORE_HEADER: "class TDGAME_API UTDFixtureCore\n{\n\tbool bFixtureFeature = false;\n};\n",
            TESTS_SOURCE: test_source(["TDGame.FluidFX.Core.Pass", "TDGame.FluidFX.Input.Fails", "TDGame.FluidFX.Pre.One", "TDGame.FluidFX.Pre.Two",
                                       "TDGame.FluidFX.Mixed.One", "TDGame.FluidFX.Mixed.Two", "TDGame.FluidFXRhi.Smoke.Gpu"]),
            EDITOR_TESTS: test_source(["TDGameEditor.FluidFX.Mode.Activation"]),
            TUNING: {"Groups": {"Smoke": {"Density": 1.0}}, "Materials": {"/Game/FluidFX/M_Fixture.M_Fixture": {"Look": 1}}, "List": [{"Value": 2}]},
            PLAN: {}, RETIRED_PLAN: {}, JUDGE: {}, REFERENCE_FILE: "class TDGAME_API UTDFixtureCore\n",
            FXP_STATIC: {"follow_ups": [], "user_items": [{"id": "FQ-1"}, {"id": "FQ-2"}], "quote_source": {"file": SPEC, "line": 1}},
            AREAS: {"areas": {"input": area("승인 대기", None, UNMET, 3, False), "smoke": area("승인 2026-11-08", "2026-11-08", PASS, 0, True),
                              "fire": area("승인 대기", None, PARTIAL, 2, False), "water": area("거부 2026-11-08", "2026-11-08", UNMET, 1, False)},
                    "plans": {"fx9_floor_smoke": {"cell": "blocked_not_run"}, "fog_f9_old": {"cell": "retired"}}},
            HEADLESS: {"results": results(headless), "filters": ["TDGame.FluidFX"], "rhi": None, "incomplete": False, "crashed": False,
                       "log": str(root / "Saved/AgentOps/20261106/w13-headless-tests.log")},
            RHI: {"results": results([("TDGame.FluidFXRhi.Smoke.Gpu", "Success")]), "filters": ["TDGame.FluidFXRhi."], "rhi": "offscreen"},
            LEDGER: {"version": 1, "rows": ledger_rows()}}


def make_world(root, mutate=None):
    files = world_files(root)
    if mutate is not None:
        mutate(files)
    for relative, value in files.items():
        if value is None:
            continue
        if isinstance(value, str):
            write_text(root, relative, value)
        else:
            write_json(root, relative, value)
    return root


def find_row(files, row_id):
    return next(row for row in files[LEDGER]["rows"] if row["id"] == row_id)


def edit_row(row_id, **changes):
    def mutate(files):
        find_row(files, row_id).update(copy.deepcopy(changes))
    return mutate


def edit_anchors(row_id, **changes):
    def mutate(files):
        find_row(files, row_id)["anchors"].update(copy.deepcopy(changes))
    return mutate


def set_file(relative, value):
    def mutate(files):
        files[relative] = value
    return mutate


def add_row(row):
    def mutate(files):
        files[LEDGER]["rows"].append(copy.deepcopy(row))
    return mutate


def drop_row(row_id):
    def mutate(files):
        files[LEDGER]["rows"] = [row for row in files[LEDGER]["rows"] if row["id"] != row_id]
    return mutate


def add_ledger_key(files):
    files[LEDGER]["extra"] = True


def drop_area(name):
    def mutate(files):
        del files[AREAS]["areas"][name]
    return mutate


def move_areas(files):
    files[AREAS_COPY] = files[AREAS]
    files[AREAS] = None


def rows_by_id(run):
    return {row["id"]: row for row in (run.document or {}).get("rows", [])}


def check_normal(log, root):
    run = run_audit(make_world(root / "normal"), "rows", "normal", PASS_ARGS)
    rows = rows_by_id(run)
    log.check("정상 종료 0", run.code == 0, f"종료 {run.code} {run.stdout[-400:]} {run.stderr[-400:]}")
    log.check("정상 SUMMARY", run.markdown.rstrip().endswith(EXPECTED_SUMMARY), run.markdown.rstrip().splitlines()[-1:] if run.markdown else run.stderr)
    log.check("상태·표지(행 9)", {key: row["label"] for key, row in rows.items()} == EXPECTED_STATUS and
              {key: row["markers"] for key, row in rows.items()} == EXPECTED_MARKERS, {key: (row["label"], row["markers"]) for key, row in rows.items()})
    tests = {row_id: [test["state"] for test in rows[row_id]["tests"]] for row_id in ("A1", "B1", "B2", "E1")} if rows else {}
    log.check("시험 3종(통과·test_failing·test_unverified)", tests == {"A1": ["pass"], "B1": ["pass", "test_failing"], "B2": ["test_unverified"],
                                                                    "E1": ["test_unverified"]}, tests)
    log.check("강등 PD-16(B1 실패·B2 편집기 시험 목록 밖)", rows and rows["B1"]["demotion"] == "test_failing 1" and rows["B2"]["demotion"] == "test_unverified 1"
              and run.document["demoted"] == ["B1", "B2"], run.document and run.document.get("demoted"))
    log.check("접두 앵커 2개 전부 통과·RHI 목록", rows and rows["D2"]["tests"] == [{"anchor": "TDGame.FluidFX.Pre.", "names": 2, "state": "pass"},
                                                                     {"anchor": "TDGame.FluidFXRhi.Smoke.Gpu", "names": 1, "state": "pass"}], rows and rows["D2"]["tests"])
    log.check("데이터+판정 있음·점 열쇠 경로·행 동등", rows and rows["D1"]["status"] == "있음" and rows["D1"]["equivalent"] and run.document["equivalent_rows"] == 1,
              rows and rows["D1"])
    log.check("W-8 닫힘(L18b·L19)", run.document and run.document["w8"] == {"closed": True, "rows": {"L18b": "해당 없음(LIVE-2 미지원)", "L19": "해당 없음(LIVE-2 미지원)"}},
              run.document and run.document.get("w8"))
    log.check("통과 목록 log 절대 경로 안 옮김", run.document and "log" not in run.document["pass_list"]["files"][0], run.document and run.document["pass_list"])
    again = run_audit(root / "normal", "rows", "normal-again", PASS_ARGS)
    log.check("같은 바이트", again.code == 0 and again.json_bytes == run.json_bytes and again.md_bytes == run.md_bytes, "두 실행 바이트 다름")


def check_variants(log, root):
    no_pass = run_audit(make_world(root / "no-pass"), "rows", "no-pass", ("--no-pass-list",))
    log.check("--no-pass-list 시험 전부 확인 불가·강등", no_pass.code == 0 and no_pass.markdown.rstrip().endswith(EXPECTED_NO_PASS),
              no_pass.markdown.rstrip().splitlines()[-1:] if no_pass.markdown else no_pass.stderr)
    moved = run_audit(make_world(root / "areas-arg", move_areas), "rows", "areas-arg", PASS_ARGS + ("--areas", AREAS_COPY))
    log.check("--areas 다른 경로", moved.code == 0 and moved.document["areas_input"] == AREAS_COPY and moved.markdown.rstrip().endswith(EXPECTED_SUMMARY),
              f"종료 {moved.code} {moved.stderr[-300:]}")
    open_w8 = run_audit(make_world(root / "w8-open", edit_anchors("L19", docs=[{"file": SPEC, "line": 3}])), "rows", "w8-open", PASS_ARGS)
    log.check("W-8 열림(L19 문서 줄에 미지원만, LIVE-2 없음)", open_w8.code == 0 and open_w8.document["w8"]["closed"] is False and "w8=open" in open_w8.markdown
              and "live2_unsupported=1" in open_w8.markdown, f"종료 {open_w8.code} {open_w8.markdown[-300:]}")
    live2_note = run_audit(make_world(root / "live2-note", edit_row("D2", note="LIVE-2 와 같은 묶음")), "rows", "live2-note", PASS_ARGS)
    log.check("note 의 LIVE-2 는 숫자로 안 셈", live2_note.code == 0, f"종료 {live2_note.code} {live2_note.stdout[-300:]}")


def check_pd16(log, root):
    unverified = run_audit(make_world(root / "pd16-unverified", edit_anchors("D1", tests=["TDGame.FluidFX.Mixed.Two"])), "rows", "pd16-unverified", PASS_ARGS)
    row = rows_by_id(unverified).get("D1", {})
    log.check("데이터+판정 있음도 미확인 시험 앵커면 강등(PD-16)", unverified.code == 0 and row.get("status") == "부분" and row.get("demotion") == "test_unverified 1"
              and row.get("markers") == ["기본 꺼짐", "test_unverified", "blocked_calib"], f"종료 {unverified.code} {row}")
    failing = run_audit(make_world(root / "pd16-equivalent", edit_anchors("D1", tests=["TDGame.FluidFX.Input.Fails"])), "rows", "pd16-equivalent", PASS_ARGS)
    row = rows_by_id(failing).get("D1", {})
    log.check("행 동등은 강등 뒤 상태(장부 있음 → 부분이면 동등 아님)", failing.code == 0 and row.get("ledger_status") == "있음" and row.get("status") == "부분"
              and row.get("equivalent") is False and (failing.document or {}).get("equivalent_rows") == 0, f"종료 {failing.code} {row}")
    earlier = {"results": results([("TDGame.FluidFX.Core.Pass", "Fail")]), "filters": ["TDGame.FluidFX"], "rhi": None}
    repeated = run_audit(make_world(root / "pd16-repeated", set_file(EARLIER, earlier)), "rows", "pd16-repeated", ("--pass-list", EARLIER) + PASS_ARGS)
    row = rows_by_id(repeated).get("A1", {})
    log.check("같은 시험 결과 여럿 중 하나라도 Success 아님 → test_failing(앞 실패·뒤 성공)", repeated.code == 0
              and row.get("tests") == [{"anchor": "TDGame.FluidFX.Core.Pass", "names": 1, "state": "test_failing"}] and row.get("status") == "부분"
              and row.get("demotion") == "test_failing 1", f"종료 {repeated.code} {row}")
    mixed = run_audit(make_world(root / "plans-mixed", edit_anchors("D1", plans=["fx9_floor_smoke", "fog_f9_old"])), "rows", "plans-mixed", PASS_ARGS)
    row = rows_by_id(mixed).get("D1", {})
    log.check("계획 칸 섞임(blocked_not_run 하나·retired 하나)도 blocked_calib", mixed.code == 0
              and [plan.get("cell") for plan in row.get("plans", [])] == ["blocked_not_run", "retired"] and row.get("markers") == ["기본 꺼짐", "blocked_calib"],
              f"종료 {mixed.code} {row}")


FAILURE_CASES = (
    ("스키마 행 열쇠", edit_row("A1", claim="있음"), "스키마(A1)"),
    ("스키마 status 5종 밖", edit_row("A1", status="확인"), "스키마(A1)"),
    ("스키마 최상위 열쇠", add_ledger_key, "최상위"),
    ("스키마 docs 줄 형식", edit_anchors("S8", docs=[{"file": SPEC, "line": "둘"}]), "스키마(S8)"),
    ("행 집합 빠짐", drop_row("S8"), "장부에 없는 대조표 행 S8"),
    ("행 집합 3절 행", add_row(ledger_row("Z1", "있음", "해당 없음", docs=[{"file": SPEC, "line": 1}])), "대조표 2절에 없는 장부 행 Z1"),
    ("행 집합 T 제외", add_row(ledger_row("T1", "있음", "해당 없음", docs=[{"file": SPEC, "line": 1}])), "대조표 2절에 없는 장부 행 T1"),
    ("행 중복", add_row(ledger_row("A1", "있음", "있음", symbols=[SYMBOL], tests=["TDGame.FluidFX.Core.Pass"])), "중복 A1"),
    ("d52 불일치", edit_row("A1", d52="부분"), "d52 불일치(A1)"),
    ("해석 실패 정규식", edit_anchors("A1", symbols=[{"file": CORE_HEADER, "regex": "class NoSuchThing"}]), "해석 실패(A1) symbols"),
    ("해석 실패 정규식 오류", edit_anchors("A1", symbols=[{"file": CORE_HEADER, "regex": "("}]), "정규식 오류"),
    ("해석 실패 심볼 파일", edit_anchors("A1", symbols=[{"file": "Source/Missing.h", "regex": "x"}]), "해석 실패(A1) symbols"),
    ("해석 실패 시험 문자열", edit_anchors("A1", tests=["TDGame.FluidFX.Core.Nope"]), "해석 실패(A1) tests"),
    ("해석 실패 접두 0개", edit_anchors("D2", tests=["TDGame.FluidFX.Nope."]), "해석 실패(D2) tests"),
    ("해석 실패 데이터 경로", edit_anchors("D1", data=[{"file": TUNING, "json_path": "Groups.Smoke.Missing"}]), "해석 실패(D1) data"),
    ("해석 실패 계획", edit_anchors("D1", plans=["no_such_plan"]), "해석 실패(D1) plans"),
    ("해석 실패 판정 파일", edit_anchors("D1", judgments=["Saved/FluidFX/none.json"]), "해석 실패(D1) judgments"),
    ("해석 실패 문서 줄", edit_anchors("S8", docs=[{"file": SPEC, "line": 99}]), "해석 실패(S8) docs"),
    ("해석 실패 기본 꺼짐", edit_row("D1", default_off={"file": CORE_HEADER, "regex": "bool bMissing"}), "해석 실패(D1) default_off"),
    ("근거 부족 있음(시험 없음)", edit_anchors("A1", tests=[]), "근거 부족(A1)"),
    ("근거 부족 부분(note 없음)", edit_row("D2", note=""), "근거 부족(D2)"),
    ("근거 부족 해당 없음(docs 없음)", edit_anchors("S8", docs=[]), "근거 부족(S8)"),
    ("note D-49 숫자", edit_row("D2", note="앵커 3개만"), "note D-49(D2)"),
    ("note D-49 80자 초과", edit_row("D2", note="가" * 81), "note D-49(D2)"),
    ("이미지 참조", edit_row("D2", note="그림.png 대조"), "이미지 D-49(D2)"),
    ("live2_reference 참조", edit_anchors("A1", symbols=[SYMBOL, {"file": REFERENCE_FILE, "regex": "UTDFixtureCore"}]), "live2_reference D-49(A1)"),
    ("모르는 FQ", edit_row("B2", user_items=["FQ-99"]), "모르는 FQ(B2)"),
)

INPUT_CASES = (
    ("장부 없음", set_file(LEDGER, None), PASS_ARGS),
    ("areas 없음", set_file(AREAS, None), PASS_ARGS),
    ("areas 형식", set_file(AREAS, {"areas": {}}), PASS_ARGS),
    ("areas 영역 빠짐", drop_area("water"), PASS_ARGS),
    ("통과 목록 파일 없음", None, ("--pass-list", "Saved/AgentOps/none.json")),
    ("통과 목록 형식", set_file(HEADLESS, {"results": [{"path": 1}]}), PASS_ARGS),
    ("통과 목록 인수 없음", None, ()),
    ("통과 목록 인수 둘 다", None, PASS_ARGS + ("--no-pass-list",)),
)


def check_failures(log, root):
    for index, (case, mutate, expected) in enumerate(FAILURE_CASES):
        name = f"fail-{index:02d}"
        run = run_audit(make_world(root / name, mutate), "rows", name, PASS_ARGS)
        log.check(f"종료 1 {case}", run.code == 1 and run.document is not None and expected in run.stdout,
                  f"종료 {run.code} 기대 '{expected}' {run.stdout[-300:]} {run.stderr[-300:]}")
    for index, (case, mutate, extra) in enumerate(INPUT_CASES):
        name = f"input-{index:02d}"
        run = run_audit(make_world(root / name, mutate), "rows", name, extra)
        log.check(f"종료 2 {case}", run.code == 2 and run.document is None, f"종료 {run.code} {run.stderr[-300:]}")


def run(tmp_root):
    log = FixtureLog("rows")
    check_normal(log, tmp_root)
    check_variants(log, tmp_root)
    check_pd16(log, tmp_root)
    check_failures(log, tmp_root)
    return log.result()

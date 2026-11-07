"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 저장소 사본에서 fxp_audit.py areas 를 돌려 정상(칸·합계·PR-11·승인·동등 표기·basis 섞임)·같은 바이트·f5 영향·R10>R9·보류 최솟값 순서·우선순위 충돌(①②>③·④ overlay>⑤·⑤는 일지 adopted만·⑥>⑦·일지>⑪)·목표 0 동등 아님·승인 sha(A19)·종료 1 넷(영역 없는 계획·분류 없음(검산 불일치)·채택 계획 모름·성능 형식)·종료 2 셋(입력 없음·모르는 하위 명령·모듈 파일 없음)을 본다.
상태: 2026-11-07 FX-P X1a(B1), fix1(빌더 A) 반박 변이 사례 추가.
"""
import copy
import shutil
from pathlib import Path

from check_fxp_fixtures import AUDIT_PATH, FixtureLog, run_audit, write_json

P_SUMMARY = "Saved/AgentOps/20261106/p/p-summary.json"
EXPECT = "Saved/FluidFX/calib/final/expect.json"
VERDICT = "Saved/FluidFX/calib/final/verdict-w11.json"
OVERLAY = "Saved/FluidFX/calib/final/overlay-frozen.json"
MAIN_QUEUE = "Tools/FluidFX/calib/batch_queue.json"
PLANS = "Tools/FluidFX/calib/plans"
RETIRED = "Tools/FluidFX/calib/retired"
APPROVAL = "Saved/FluidFX/approval"
SHA_A, SHA_B, SHA_C, SHA_D = "a" * 64, "b" * 64, "c" * 64, "d" * 64
HEADLESS_REASON = "블록 K2 헤드리스 설계 단언 실패(TDGame.FluidFX.X) → 블록의 가장 최근 채택 되돌림"
CONFLICT_REASON = "conflict: 사슬 Groups.X 재검증 실패(1) → 가장 최근 채택 되돌림"
UNMET = "미충족(통과 0 / 미달 1 / 행 1 / 판정 불가 0 / 측정 불가 0 / 출하 밖 0)"
PARTIAL = "부분(통과 1 / 행 2 / 판정 불가 1 / 측정 불가 2 / 출하 밖 0)"
PASS = "충족(proposed)"
EXPECTED_SUMMARY = ("SUMMARY areas=9 plans=21 adopted=4 adopted_plans=3 influence=1 verify_only=1 insensitive=2 no_adopt=2 conflict=1 removed_after_adoption=2 "
                    "deferred_plans=1 deferred_items=3 other=8 unclassified=0 equivalence_confirmed=1 problems=0")

PLAN_TARGETS = {"fog_f1_layer": ["live2_image", "live2_image"], "fog_f5_cut_stir": ["live2_image"], "cloud_g9_mushroom": ["spec_proposal", None],
                "cloud_g4_painter": [], "cloud_g12_volcano": ["LIVE-2 그림 판독"], "viscous_fx14_blood": ["live2_image", "spec_proposal"], "fx9_hvol_vs_svol": ["live2_image"],
                "fx16a_cell_size_invariance": [], "fx15a_kernel_vs_jacobi": ["spec_proposal"], "foliage_k3_reed": [None], "w3_rain": ["live2_image"],
                "river_vorticity": [], "w6_ship_wake": [], "track_sand_h4": ["live2_image"], "campfire_vorticity": [], "fx8n_embers": [], "fx8n_drive": []}
RETIRED_PLANS = ("fx16b_fadeout", "fx15a_wind_and_anchor", "input_fx15b", "viscous_fx14_lava")


def plan_path(stem):
    return f"{PLANS}/{stem}.json"


def journal_item(stem, status, reason=""):
    return {"plan": plan_path(stem), "status": status, "reason": reason, "runs": {}}


def journal(acquired, items):
    holds = [{"owner": "fixture", "acquired_at": acquired, "released_at": acquired}] if acquired else []
    return {"queue_sha256": "0" * 64, "holds": holds, "real_dt": 0.05, "items": items}


def world_files():
    files = {}
    for stem, bases in PLAN_TARGETS.items():
        files[plan_path(stem)] = {"id": stem, "metrics": {"targets": [{"metric": f"m{index}", "basis": basis} for index, basis in enumerate(bases)]}}
    for stem in RETIRED_PLANS:
        files[f"{RETIRED}/{stem}.json"] = {"id": stem, "metrics": {"targets": []}}
    files[P_SUMMARY] = {"areas": {"fire": {"overall": UNMET, "blocking_unmeasurable": 0}, "smoke": {"overall": PARTIAL, "blocking_unmeasurable": 2},
                                  "fog": {"overall": PASS, "blocking_unmeasurable": 0}, "cloud": {"overall": PASS, "blocking_unmeasurable": 0},
                                  "input": {"overall": UNMET, "blocking_unmeasurable": 0}, "track": {"overall": PASS, "blocking_unmeasurable": 0},
                                  "viscous": {"overall": PASS, "blocking_unmeasurable": 0},
                                  "foliage": {"overall": "부분(통과 0 / 행 0 / 판정 불가 0 / 측정 불가 6 / 출하 밖 0)", "blocking_unmeasurable": 6}}}
    files[EXPECT] = {"adoptions": {"1": {}, "2": {}, "3": {}, "4": {}}, "influences": ["f5"], "overlay": OVERLAY,
                     "runs": {"f1": {"plan": plan_path("fog_f1_layer"), "sources": [{"adoption": "1", "judge": "Saved/FluidFX/calib/batch-R5/runs/fog_f1_layer/confirm-1/judge.json"}]},
                              "f5": {"plan": plan_path("fog_f5_cut_stir"), "sources": [{"adoption": "f5", "judge": "Saved/FluidFX/calib/batch-R5/runs/fog_f5_cut_stir/screen-1/screen.json"}]}},
                     "fallback_runs": {}}
    files[VERDICT] = {"adoptions": {"1": {"state_source": {"item_plan": plan_path("fog_f1_layer")}},
                                    "2": {"state_source": {"item_plan": "Saved/FluidFX/calib/final/plans/fog_f1_layer-w.json"}},
                                    "3": {"state_source": {"item_plan": plan_path("cloud_g9_mushroom-p20")}},
                                    "4": {"state_source": {"item_plan": plan_path("viscous_fx14_blood")}},
                                    "f5": {"state_source": {"item_plan": plan_path("fog_f5_cut_stir")}}}}
    files[OVERLAY] = {"history": [{"op": "add", "reason": "accept", "from": {"plan": plan_path("fx9_hvol_vs_svol")}},
                                  {"op": "remove", "reason": "P-22 출하 조건 확인 불가", "from": {"plan": plan_path("fx9_hvol_vs_svol")}},
                                  {"op": "add", "reason": "accept", "from": {"plan": plan_path("foliage_k3_reed")}},
                                  {"op": "remove", "reason": HEADLESS_REASON, "from": {"plan": plan_path("foliage_k3_reed")}},
                                  {"op": "add", "reason": "accept", "from": {"plan": plan_path("fog_f1_layer")}},
                                  {"op": "remove", "reason": "manual: 재실행", "from": {"plan": plan_path("fog_f1_layer")}},
                                  {"op": "add", "reason": "accept", "from": {"plan": plan_path("fog_f1_layer")}}]}
    files[MAIN_QUEUE] = {"blocks": [{"id": "K5", "plans": [{"plan": plan_path("fx16a_cell_size_invariance"), "class": "verify"}, {"plan": plan_path("fx15a_kernel_vs_jacobi"), "class": "cal0"},
                                                          {"plan": plan_path("fx9_hvol_vs_svol"), "class": "cal0"}]}],
                         "not_run": [{"plan": "river_vorticity", "reason": "run1 둔감(효과 < 2×잡음)"}, {"plan": "viscous_fx14_lava", "reason": "폐기·이관"},
                                     {"plan": "input_fx15b", "reason": "폐기·이관"}],
                         "blocked": [{"plan": plan_path("fx8n_drive"), "blocked": "목표 정의 불일치"}],
                         "deferred": [{"plan": "viscous_fx14_blood", "scene": "blood_splash", "reason": "D3"}]}
    files["Saved/FluidFX/calib/batch/queue-R2.json"] = {"blocks": [], "not_run": [], "blocked": [{"plan": plan_path("w6_ship_wake")}],
                                                        "deferred": [{"plan": "viscous_fx14_blood", "scene": "blood_splash", "reason": "D3"}]}
    files["Tools/FluidFX/calib/approval-only.json"] = {"items": [
        {"plan": "fx15a_wind_and_anchor", "kind": "target", "item": "anchored_cauldron.wake_length_cm@anchored_cauldron", "destination": "승인 시트 뒤(deferred)", "reason": "측정 함수 없음"},
        {"plan": "fx8n_drive", "kind": "scene", "item": "p2_smoke_sprite(손잡이 a)", "destination": "P2 승인 시트 뒤(deferred)", "reason": "정량 목표 없음"},
        {"plan": "fx8n_embers", "kind": "target", "item": "embers.count@ember_bed", "destination": "승인 시트", "reason": "육안"}]}
    files["Tools/FluidFX/calib/structural-gaps.json"] = {"gaps": [
        {"plan": "viscous_fx14_blood", "kind": "scene", "item": "blood_splash(목표 a·b)", "destination": "deferred(판정 미사용, FX-P 표)", "reason": "PIE 당 1회"},
        {"plan": "w3_rain", "kind": "knob", "item": "k", "destination": "없음", "reason": "검출 불가"}]}
    files["Saved/FluidFX/calib/batch/journal-K1.json"] = journal("2026-10-31T10:00:00", [journal_item("campfire_vorticity", "insensitive"), journal_item("fx8n_embers", "pending"),
                                                                                       journal_item("fx16b_fadeout", "pending"), journal_item("fog_f5_cut_stir", "pending")])
    files["Saved/FluidFX/calib/batch-K4/journal.json"] = journal("2026-10-31T12:00:00", [
        journal_item("fog_f5_cut_stir", "no_adopt", "screen best 없음"), journal_item("fog_f1_layer", "undetermined"), journal_item("fx16a_cell_size_invariance", "insensitive"),
        journal_item("fx15a_kernel_vs_jacobi", "no_adopt"), journal_item("cloud_g12_volcano", "insensitive"), journal_item("fx9_hvol_vs_svol", "failed")])
    files["Saved/FluidFX/calib/batch-R9/journal.json"] = journal("2026-11-01T09:00:00", [journal_item("track_sand_h4", "failed")])
    files["Saved/FluidFX/calib/batch-R10/journal.json"] = journal("2026-11-01T09:00:00", [journal_item("track_sand_h4", "insensitive")])
    files["Saved/FluidFX/calib/batch-P20a/journal.json"] = journal("2026-11-01T14:00:00", [journal_item("cloud_g12_volcano-p20", "no_adopt"), journal_item("cloud_g9_mushroom-p20", "adopted")])
    files["Saved/FluidFX/calib/batch-R5/journal.json"] = journal("2026-11-01T16:00:00", [
        journal_item("fx9_hvol_vs_svol", "adopted"), journal_item("foliage_k3_reed", "verify_failed", HEADLESS_REASON), journal_item("cloud_g4_painter", "verify_failed", CONFLICT_REASON),
        journal_item("w3_rain", "deferred", "블록 누적 → confirm 을 K10 으로"), journal_item("campfire_vorticity", "undetermined"), journal_item("fx8n_embers", "failed"),
        journal_item("viscous_fx14_blood", "adopted")])
    for area, sha in (("fog", SHA_A), ("cloud", SHA_C), ("viscous", SHA_D), ("track", SHA_A)):
        files[f"{APPROVAL}/{area}/current.json"] = {"area": area, "revision": 2, "sheet_json": f"{APPROVAL}/{area}/r2/sheet.json", "sheet_json_sha256": sha}
    files[f"{APPROVAL}/approvals.json"] = {"areas": {
        "fog": {"decision": "승인", "date": "2026-11-07", "sheet_json": f"{APPROVAL}/fog/r2/sheet.json", "sheet_json_sha256": SHA_A, "text": "확인"},
        "cloud": {"decision": "승인", "date": "2026-11-07", "sheet_json": f"{APPROVAL}/cloud/r2/sheet.json", "sheet_json_sha256": SHA_B, "text": "옛 판"},
        "viscous": {"decision": "승인", "date": "2026-11-08", "sheet_json": f"{APPROVAL}/viscous/r2/sheet.json", "sheet_json_sha256": SHA_D, "text": "확인"}}}
    return files


def make_world(root, files):
    for relative, value in files.items():
        write_json(root, relative, value)
    return root


def cell_of(document, plan):
    return ((document or {}).get("plans") or {}).get(plan, {}).get("cell")


def area_of(document, area):
    return ((document or {}).get("areas") or {}).get(area, {})


def check_normal(log, root):
    run = run_audit(root, "areas", "normal")
    document = run.document or {}
    log.check("정상 종료 0", run.code == 0, f"종료 {run.code} {run.stderr.strip()[:300]} {run.stdout.strip()[-300:]}")
    log.check("정상 SUMMARY", EXPECTED_SUMMARY in run.markdown.splitlines()[-1:], f"{run.markdown.splitlines()[-1:] if run.markdown else run.stderr[:200]}")
    expected_cells = {"fog_f1_layer": "adopted", "fog_f5_cut_stir": "influence", "cloud_g9_mushroom": "adopted", "viscous_fx14_blood": "adopted",
                      "fx16a_cell_size_invariance": "verify_only", "cloud_g4_painter": "conflict", "fx9_hvol_vs_svol": "removed_after_adoption",
                      "foliage_k3_reed": "removed_after_adoption", "w3_rain": "deferred", "river_vorticity": "insensitive", "track_sand_h4": "insensitive",
                      "fx15a_kernel_vs_jacobi": "no_adopt", "cloud_g12_volcano": "no_adopt", "campfire_vorticity": "undetermined", "fx8n_embers": "failed",
                      "fx8n_drive": "blocked_not_run", "w6_ship_wake": "blocked_not_run", "input_fx15b": "blocked_not_run", "viscous_fx14_lava": "blocked_not_run",
                      "fx16b_fadeout": "retired", "fx15a_wind_and_anchor": "retired"}
    wrong = {plan: cell_of(document, plan) for plan, cell in expected_cells.items() if cell_of(document, plan) != cell}
    log.check("칸 21계획(f5 영향·접미사 -p20·-w·검증만 우선·conflict:·헤드리스·run1 둔감·⑪>⑫)", not wrong, f"다름 {wrong}")
    log.check("input_ 접두 영역", ((document.get("plans") or {}).get("input_fx15b") or {}).get("area") == "input", "input_fx15b 영역이 input 아님")
    fog = area_of(document, "fog")
    log.check("채택 수 단위(fog 2(1), f5 제외)", fog.get("adoption_entries") == 2 and fog.get("adopted_plans") == 1 and fog.get("influence_ids") == ["f5"], f"{fog.get('adoption_entries')}({fog.get('adopted_plans')}) 영향 {fog.get('influence_ids')}")
    labels = {area: area_of(document, area).get("pr11") for area in ("fire", "smoke", "fog", "water")}
    log.check("PR-11 귀결 4종", labels == {"fire": "FX-P 성능 미달", "smoke": "FX-P 성능 부분(통과 1 / 행 2 / 판정 불가 1 / 측정 불가 2 / 출하 밖 0)", "fog": "화면 승인만",
                                       "water": "판정 불가(성능 입력 없음)"}, f"{labels}")
    approval = {area: area_of(document, area).get("approval", {}).get("state") for area in ("fog", "cloud", "viscous", "track")}
    log.check("승인 sha(A19): 일치 = 승인, 다름·기록 없음 = 대기", approval == {"fog": "승인 2026-11-07", "cloud": "승인 대기", "viscous": "승인 2026-11-08", "track": "승인 대기"}, f"{approval}")
    equivalence = {area: area_of(document, area).get("equivalence", {}) for area in ("fog", "cloud", "viscous", "track", "fire", "smoke")}
    log.check("동등 확인 = fog만(승인 ∧ 충족 ∧ live2 전부)", [area for area, value in equivalence.items() if value.get("confirmed")] == ["fog"], f"{equivalence}")
    log.check("성능만 충족 = [제안] 기준 충족, 승인 대기 사유", equivalence["track"].get("label") == "[제안] 기준 충족(동등 증명 아님)" and equivalence["track"].get("reasons") == ["승인 대기"],
              f"{equivalence['track']}")
    viscous_basis = area_of(document, "viscous").get("basis", {})
    log.check("승인 ∧ 충족 ∧ basis 섞임(live2 1·[제안] 1) = 동등 아님", not equivalence["viscous"].get("confirmed", True) and equivalence["viscous"].get("reasons") == ["목표 [제안] 1"]
              and equivalence["viscous"].get("label") == "[제안] 기준 충족(동등 증명 아님)" and (viscous_basis.get("live2_image"), viscous_basis.get("targets")) == (1, 2),
              f"{equivalence['viscous']} {viscous_basis}")
    log.check("미충족 사유 문구(목표 0 = 확인 불가)", equivalence["fire"].get("label") == "승인 대기 · 미충족 · 목표 없음(확인 불가)", f"{equivalence['fire']}")
    log.check("부분 사유 문구", equivalence["smoke"].get("label") == "승인 대기 · 부분(막는 측정 불가 2) · 목표 [제안] 1", f"{equivalence['smoke']}")
    cloud_basis = area_of(document, "cloud").get("basis", {})
    log.check("basis 분포(자유 문안·미표기)", cloud_basis == {"live2_image": 0, "spec_proposal": 1, "free_text": 1, "unmarked": 1, "targets": 3, "plans": 3}, f"{cloud_basis}")
    deferred = [(item["area"], item["plan"], item["scene"], len(item["sources"])) for item in document.get("deferred_items", [])]
    log.check("deferred 항목 합집합·중복 제거", deferred == [("fire", "fx8n_drive", "p2_smoke_sprite", 1), ("smoke", "fx15a_wind_and_anchor", "anchored_cauldron", 1),
                                                  ("viscous", "viscous_fx14_blood", "blood_splash", 3)], f"{deferred}")
    log.check("출력에 루트 절대 경로 없음", run.json_bytes is not None and str(root).replace("\\", "/").lower() not in run.json_bytes.decode("utf-8").replace("\\\\", "/").lower(), "절대 경로 있음")
    second = run_audit(root, "areas", "normal-second")
    log.check("같은 입력 같은 바이트(json·md, --out 다름)", run.json_bytes == second.json_bytes and run.md_bytes == second.md_bytes and run.json_bytes is not None, "바이트 다름")


def variant(root, name, change):
    files = world_files()
    change(files)
    return make_world(root / name, files)


def swap_r9_r10(files):
    files["Saved/FluidFX/calib/batch-R9/journal.json"]["items"] = [journal_item("track_sand_h4", "insensitive")]
    files["Saved/FluidFX/calib/batch-R10/journal.json"]["items"] = [journal_item("track_sand_h4", "failed")]


def hold_free_r_batches(files):
    for name in ("R9", "R10"):
        files[f"Saved/FluidFX/calib/batch-{name}/journal.json"]["holds"] = []


def stale_fog_sheet(files):
    files[f"{APPROVAL}/fog/current.json"]["sheet_json_sha256"] = SHA_B


def unknown_area_plan(files):
    files[plan_path("zzz_mystery")] = {"id": "zzz_mystery", "metrics": {"targets": []}}


def pending_only_plan(files):
    files["Saved/FluidFX/calib/batch-K4/journal.json"]["items"].append(journal_item("cloud_g2_wrap_seam", "pending"))


def adoption_without_plan(files):
    files[EXPECT]["adoptions"]["9"] = {}


def unknown_performance(files):
    files[P_SUMMARY]["areas"]["fire"]["overall"] = "알 수 없음"


def zero_fog_targets(files):
    for stem in ("fog_f1_layer", "fog_f5_cut_stir"):
        files[plan_path(stem)]["metrics"]["targets"] = []


def priority_collisions(files):
    queue = files[MAIN_QUEUE]
    queue["blocks"][0]["plans"] += [{"plan": plan_path("fog_f1_layer"), "class": "verify"}, {"plan": plan_path("fog_f5_cut_stir"), "class": "verify"}]
    queue["not_run"].append({"plan": "w3_rain", "reason": "run1 둔감(효과 < 2×잡음)"})
    queue["blocked"].append({"plan": plan_path("fx15a_kernel_vs_jacobi"), "blocked": "목표 정의 불일치"})
    files[OVERLAY]["history"] += [{"op": "add", "reason": "accept", "from": {"plan": plan_path("fx9_hvol_vs_svol")}},
                                  {"op": "remove", "reason": CONFLICT_REASON, "from": {"plan": plan_path("fx9_hvol_vs_svol")}},
                                  {"op": "add", "reason": "accept", "from": {"plan": plan_path("campfire_vorticity")}},
                                  {"op": "remove", "reason": "manual: 재실행", "from": {"plan": plan_path("campfire_vorticity")}}]


def hold(acquired, released):
    return {"owner": "fixture", "acquired_at": acquired, "released_at": released}


def multi_hold_r10(files):
    files["Saved/FluidFX/calib/batch-R9/journal.json"]["holds"] = [hold("2026-11-01T10:00:00", "2026-11-01T10:30:00")]
    files["Saved/FluidFX/calib/batch-R10/journal.json"]["holds"] = [hold("2026-11-01T21:00:00", "2026-11-01T21:30:00"), hold("2026-11-01T08:00:00", "2026-11-01T08:30:00")]


def first_hold_of(document, batch):
    return next((entry.get("first_hold") for entry in (document or {}).get("journal_order", []) if entry.get("batch") == batch), None)


def check_priority(log, root):
    run = run_audit(variant(root, "priority", priority_collisions), "areas", "priority")
    log.check("우선순위 충돌 무대 종료 0", run.code == 0, f"종료 {run.code} {(run.document or {}).get('problems')} {run.stderr[:200]}")
    cells = {plan: cell_of(run.document, plan) for plan in ("fog_f1_layer", "fog_f5_cut_stir", "fx9_hvol_vs_svol", "campfire_vorticity", "w3_rain", "fx15a_kernel_vs_jacobi")}
    log.check("① 채택·② 영향 > ③ 큐 verify(같은 계획이 verify 큐에도)", (cells["fog_f1_layer"], cells["fog_f5_cut_stir"]) == ("adopted", "influence"), f"{cells}")
    log.check("④ overlay 마지막 remove 'conflict:' > ⑤(일지 adopted여도 conflict)", cells["fx9_hvol_vs_svol"] == "conflict", f"{cells}")
    log.check("⑤는 일지 adopted일 때만(overlay 마지막 remove여도 일지 undetermined면 ⑨)", cells["campfire_vorticity"] == "undetermined", f"{cells}")
    log.check("⑥ 일지 deferred > ⑦ not_run 'run1 둔감'", cells["w3_rain"] == "deferred", f"{cells}")
    log.check("일지 최종(⑧ no_adopt) > ⑪ 큐 blocked", cells["fx15a_kernel_vs_jacobi"] == "no_adopt", f"{cells}")


def check_equivalence_and_holds(log, root):
    zero = run_audit(variant(root, "zero-targets", zero_fog_targets), "areas", "zero-targets")
    fog = area_of(zero.document, "fog").get("equivalence", {})
    log.check("목표 0 = 승인 ∧ 충족이어도 동등 아님(확인 불가)", zero.code == 0 and fog.get("confirmed") is False and fog.get("reasons") == ["목표 없음(확인 불가)"]
              and fog.get("label") == "[제안] 기준 충족(동등 증명 아님)" and ((zero.document or {}).get("totals") or {}).get("equivalence_confirmed") == 0, f"종료 {zero.code} {fog}")
    held = run_audit(variant(root, "multi-hold", multi_hold_r10), "areas", "multi-hold")
    log.check("보류 2개 일지 순서 = acquired_at 최솟값(R10 08:00 < R9 10:00 → R9 failed 최종)", cell_of(held.document, "track_sand_h4") == "failed"
              and first_hold_of(held.document, "R10") == "2026-11-01T08:00:00", f"{cell_of(held.document, 'track_sand_h4')} R10 {first_hold_of(held.document, 'R10')}")


def check_variants(log, root):
    swapped = run_audit(variant(root, "swap", swap_r9_r10), "areas", "swap")
    log.check("R10>R9 뒤집기: R10 failed가 최종", cell_of(swapped.document, "track_sand_h4") == "failed", f"{cell_of(swapped.document, 'track_sand_h4')}")
    free = run_audit(variant(root, "hold-free", hold_free_r_batches), "areas", "hold-free")
    log.check("보류 없는 일지도 R10>R9 자연수", cell_of(free.document, "track_sand_h4") == "insensitive", f"{cell_of(free.document, 'track_sand_h4')}")
    stale = run_audit(variant(root, "stale", stale_fog_sheet), "areas", "stale")
    fog = area_of(stale.document, "fog")
    log.check("승인 sha 낡음 → 대기·동등 0", fog.get("approval", {}).get("state") == "승인 대기" and not fog.get("equivalence", {}).get("confirmed", True) and stale.code == 0,
              f"{fog.get('approval')} {fog.get('equivalence')}")
    for name, change, text in (("no-area", unknown_area_plan, "영역 없는 계획: zzz_mystery"), ("unclassified", pending_only_plan, "분류 없는 계획(12단계 밖): cloud_g2_wrap_seam"),
                               ("adoption", adoption_without_plan, "채택 계획 모름: expect adopted 9"), ("performance", unknown_performance, "성능 형식 모름(fire)")):
        result = run_audit(variant(root, name, change), "areas", name)
        problems = (result.document or {}).get("problems", [])
        log.check(f"종료 1 {name}", result.code == 1 and any(text in problem for problem in problems), f"종료 {result.code} {problems}")
        if name == "unclassified":
            check = area_of(result.document, "cloud").get("check", {})
            log.check("분류 없음 = 검산 불일치(check.ok 거짓·md 불일치)", check.get("ok") is False and check.get("cell_sum", 0) + 1 == check.get("plans")
                      and "- cloud: 칸 합" in result.markdown and "→ 불일치" in result.markdown, f"{check}")
    missing = make_world(root / "missing", {key: value for key, value in world_files().items() if key != P_SUMMARY})
    absent = run_audit(missing, "areas", "missing")
    log.check("종료 2 입력 없음(출력 안 씀)", absent.code == 2 and absent.json_bytes is None, f"종료 {absent.code}")
    unknown = run_audit(root / "swap", "nosuch", "nosuch")
    log.check("종료 2 모르는 하위 명령", unknown.code == 2, f"종료 {unknown.code}")
    lonely = root / "lonely-tool"
    lonely.mkdir(parents=True, exist_ok=True)
    shutil.copy2(AUDIT_PATH, lonely / "fxp_audit.py")
    lone = run_audit(root / "swap", "areas", "lone", audit_path=lonely / "fxp_audit.py")
    log.check("종료 2 모듈 파일 없음", lone.code == 2 and "모듈 파일 없음" in lone.stderr, f"종료 {lone.code} {lone.stderr[:200]}")


def run(tmp_root):
    log = FixtureLog("areas")
    root = Path(tmp_root)
    normal = make_world(root / "normal", world_files())
    check_normal(log, normal)
    check_variants(log, root)
    check_priority(log, root)
    check_equivalence_and_holds(log, root)
    return log.result()

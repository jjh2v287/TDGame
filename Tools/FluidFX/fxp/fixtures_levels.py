"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 사본에서 fxp_audit.py levels 를 돌려 정상(느슨·엄격·별칭·manual-·미생성 cd_ids·manual 씨앗 안 셈·요청 파일 무시)·같은 바이트·경고 종료 0(별칭 없음·모르는 ID)·종료 1(수 ≠·levels 불일치·없는 맵·없는 장면·행 밖 항목·레벨 맵 밖 씨앗·근거 없는 excluded 3종·reason D-49·follow_up·대상 레벨 누락·별칭 근거)·느슨은 Content 맵 필요(맵이 전부 Content 밖이면 느슨 아님)·종료 2(입력 없음)·실제 levels_static 59/58·별칭을 본다.
상태: 2026-11-07 FX-P X3(B4), fix1 빌더 B가 느슨 Content 사례 추가(변이 levels-loose-without-content).
"""
import json
from pathlib import Path

from check_fxp_fixtures import FixtureLog, run_audit, write_json, write_text

TOOL_DIRECTORY = Path(__file__).resolve().parent
LEVELS_STATIC = "Tools/FluidFX/fxp/levels_static.json"
FXP_STATIC = "Tools/FluidFX/fxp/fxp_static.json"
LEVEL_MAP = "Docs/FluidFX/live2_level_map.json"
COVERAGE = "Docs/FluidFX/live2_coverage.json"
CONTENT_LIST = "Saved/AgentOps/20261019/fluidfx/parity2/fx7d-build/content_list.txt"
PB2 = "Saved/AgentOps/20261030/cal0/plan-batch-v2.md"
MAPS = "Content/FluidFX/Maps"
GAME_MAPS = "/Game/FluidFX/Maps"
CLOUD, PROPS, PARTICLE, SMOKE_SETUP = "LV_TDFluidCloudStage", "LV_TDFluidWaterPropsStage", "LV_TDFluidParticleStage", "LV_TDFluidSmokeSetupStage"
CONTENT_LINES = ["L.0.1|Clouds", "SE.0.1.1.1|Pillars", "SE.0.1.1.2|Vortex", "L.0.23|Water_Various", "S.0.23.1|Pool", "S.0.23.10|Legacy",
                 "SE.0.23.10.1|Orb", "SE.0.23.10.2|Sand Flow", "L.1.0|StartingGuide", "S.1.0.1|Guide", "L.3.4|Particles", "S.3.4.1|Painter particles",
                 "S.3.4.2|Velocity particles", "L.3.7|Perf_Primitives", "S.3.7.1|Stress"]
ALIAS_LINE = CONTENT_LINES.index("SE.0.23.10.2|Sand Flow") + 1
PB2_TEXT = "# 계획\n\n| R13 스트레스 무대 없는 종 | 무대 생성기 확장 명세(FX-P 앞 별도) |\n"
EXPECTED_SUMMARY = "SUMMARY levels=5 targets=4 loose=3/3 strict_mean=0.722 excluded=1 manual_refs=2 unknown_ids=0 aliased=1 problems=0"


def real_static():
    return json.loads((TOOL_DIRECTORY / "levels_static.json").read_text(encoding="utf-8"))


def fixture_static():
    static = real_static()
    static["expected_counts"] = {"levels": 5, "targets": 4}
    static["aliases"] = [{"from": "SE.0.23.10", "to": "SE.0.23.10.2", "remove_after": "FU-16", "evidence": {"file": CONTENT_LIST, "line": ALIAS_LINE, "expect": "SE.0.23.10.2|"}}]
    return static


def setup_item(identifier, row, not_applicable=None):
    return {"source": "setup", "id": identifier, "name": identifier, "row": row, "not_applicable": not_applicable}


def coverage():
    items = [setup_item("SE.0.1.1.1", "G3"), setup_item("SE.0.1.1.2", "G3"), setup_item("S.0.23.1", "L11"), setup_item("SE.0.23.10.1", "L11"),
             setup_item("SE.0.23.10.2", "L11"), setup_item("S.1.0.1", None, "안내"), setup_item("S.3.4.1", "C12"), setup_item("S.3.4.2", "C12"),
             setup_item("S.3.7.1", "R13"), {"source": "parameter", "id": "P.9.9", "name": "p", "row": "G3", "not_applicable": None}]
    return {"rows": [{"id": row, "name": row} for row in ("G3", "L11", "C12", "R13", "B1")], "items": items}


def level_map():
    return {"levels": [{"id": line.split("|")[0], "name": line.split("|")[1]} for line in CONTENT_LINES if line.startswith("L.")],
            "level_maps": {"L.0.1": [CLOUD], "L.0.23": [PROPS], "L.3.4": [PARTICLE], "L.3.7": []},
            "rows": {"G3": [{"map": CLOUD, "scene": "Pillars", "evidence": "report", "items": ["SE.0.1.1.1"]}, {"map": CLOUD, "scene": "S9", "evidence": "plan", "items": ["SE.0.1.1.2"]}],
                     "L11": [{"map": PROPS, "scene": None, "evidence": "manual", "items": ["SE.0.23.10.1"]}],
                     "C12": [{"map": CLOUD, "scene": "Pillars", "evidence": "report"}]},
            "excluded": {"L.3.7": {"kind": "후속 대기", "reason": "스트레스 무대 없음", "evidence": f"{PB2}:3", "expect": "무대 생성기 확장 명세", "follow_up": "FU-12"}}}


def game_map(name):
    return f"{GAME_MAPS}/{name}"


def world_files():
    return {LEVELS_STATIC: fixture_static(),
            FXP_STATIC: {"follow_ups": [{"id": "FU-12", "item": "R13s", "source": "s"}, {"id": "FU-16", "item": "별칭 삭제", "source": "s"}], "user_items": [],
                         "quote_source": {"file": PB2, "line": 1}},
            COVERAGE: coverage(), LEVEL_MAP: level_map(),
            "Saved/FluidFX/fluid_cloud_stage.json": {"map": game_map(CLOUD), "setups": [{"index": 0, "name": "Pillars"}, {"index": 1, "name": "Vortex"}]},
            "Saved/FluidFX/fluid_cloud_stage_request.json": {"cd_ids": ["X.9.9.9"]},
            "Saved/FluidFX/fluid_water_props_stage.json": {"map": game_map(PROPS), "built_zones": [0, 7], "blocked": {},
                                                           "zones": {"0": {"label": "Tank", "cd_ids": ["S.0.23.1"]}, "7": {"label": "WaterySand", "cd_ids": ["SE.0.23.10"]},
                                                                     "8": {"label": "NotBuilt", "cd_ids": ["SE.0.23.10.1"]}}},
            "Saved/FluidFX/fluid_particle_stage.json": {"map": game_map(PARTICLE), "scenes": [{"scene": "P1", "cd_ids": ["S.3.4.1"]}]},
            "Saved/FluidFX/fluid_smoke_setup_stage.json": {"map": game_map(SMOKE_SETUP), "scenes": [{"scene": "SetupPortalVortex", "cd_ids": ["manual-2.1", "manual-4.7"]}],
                                                           "deferred": [{"cd_ids": ["S.3.4.2"], "kind": "Later"}]},
            "Tools/FluidFX/calib/plans/cloud_g4_painter.json": {"scenes": [{"level": game_map(CLOUD), "name": "painter_stroke"}]},
            "Tools/FluidFX/fx7p_plan.json": {"scenes": [{"id": "S9", "level": game_map(CLOUD)}]}}


def make_world(root, files, texts=None, maps=(CLOUD, PROPS, PARTICLE, SMOKE_SETUP)):
    for relative, value in files.items():
        write_json(root, relative, value)
    base_texts = {CONTENT_LIST: "\n".join(CONTENT_LINES) + "\n", PB2: PB2_TEXT}
    base_texts.update(texts or {})
    for relative, text in base_texts.items():
        if text is not None:
            write_text(root, relative, text)
    for name in maps:
        write_text(root, f"{MAPS}/{name}.umap", "map")
    return root


def variant(root, name, change=None, texts=None):
    files = world_files()
    if change:
        change(files)
    return make_world(root / name, files, texts)


def level_of(document, identifier):
    return next((row for row in (document or {}).get("levels", []) if row["id"] == identifier), {})


def check_normal(log, root):
    run = run_audit(root, "levels", "normal")
    document = run.document or {}
    log.check("정상 종료 0", run.code == 0, f"종료 {run.code} {run.stderr.strip()[:300]} {run.stdout.strip()[-400:]}")
    log.check("정상 SUMMARY", run.markdown.splitlines()[-1:] == [EXPECTED_SUMMARY], f"{run.markdown.splitlines()[-1:] if run.markdown else run.stderr[:200]}")
    cd_ids = document.get("cd_ids", {})
    log.check("별칭 SE.0.23.10 → SE.0.23.10.2 엄격 셈", "SE.0.23.10.2" in cd_ids.get("built", []) and cd_ids.get("aliased", [{}])[0].get("from") == "SE.0.23.10", f"{cd_ids}")
    log.check("manual- 따로 셈(모르는 ID 아님)", sorted(cd_ids.get("manual", {})) == ["manual-2.1", "manual-4.7"] and cd_ids.get("unknown") == [], f"{cd_ids}")
    log.check("미생성 cd_ids(deferred·built_zones 밖) 안 셈", cd_ids.get("not_built") == ["S.3.4.2", "SE.0.23.10.1"], f"{cd_ids.get('not_built')}")
    log.check("L.0.23 엄격 2/3(manual 씨앗 안 셈)", level_of(document, "L.0.23").get("uncovered") == ["SE.0.23.10.1"], f"{level_of(document, 'L.0.23')}")
    log.check("L.3.4 엄격 1/2(레벨 맵 밖 행 씨앗 안 셈)", level_of(document, "L.3.4").get("uncovered") == ["S.3.4.2"], f"{level_of(document, 'L.3.4')}")
    log.check("계획 scene_key id(fx7p) 인정", level_of(document, "L.0.1").get("strict_covered") == 2, f"{level_of(document, 'L.0.1')}")
    log.check("대상 아님 L.1.0·excluded L.3.7 범위 밖", document.get("counts", {}).get("non_targets") == ["L.1.0"] and level_of(document, "L.3.7").get("excluded") == "후속 대기", f"{document.get('counts')}")
    log.check("요청 파일 안 읽음", not any(path.endswith("_request.json") for path in document.get("inputs", {})), f"{sorted(document.get('inputs', {}))}")
    second = run_audit(root, "levels", "normal-second")
    log.check("같은 입력 같은 바이트(json·md, --out 다름)", run.json_bytes == second.json_bytes and run.md_bytes == second.md_bytes and run.json_bytes is not None, "바이트 다름")


def drop_alias(files):
    files[LEVELS_STATIC]["aliases"] = []


def unknown_identifier(files):
    files["Saved/FluidFX/fluid_smoke_setup_stage.json"]["scenes"][0]["cd_ids"].append("SE.7.7.7.7")


def count_mismatch(files):
    files[LEVELS_STATIC]["expected_counts"] = {"levels": 59, "targets": 58}


def levels_drift(files):
    files[LEVEL_MAP]["levels"] = files[LEVEL_MAP]["levels"][:-1]


def missing_map(files):
    files[LEVEL_MAP]["level_maps"]["L.0.1"].append("LV_TDFluidNoSuchStage")


def missing_scene(files):
    files[LEVEL_MAP]["rows"]["G3"][0]["scene"] = "Pillarz"


def foreign_item(files):
    files[LEVEL_MAP]["rows"]["G3"][0]["items"] = ["S.0.23.1"]


def seed_outside_level(files):
    files[LEVEL_MAP]["rows"]["L11"].append({"map": CLOUD, "scene": "Vortex", "evidence": "report", "items": ["S.0.23.1"]})


def excluded_without_evidence(files):
    del files[LEVEL_MAP]["excluded"]["L.3.7"]["evidence"]


def excluded_wrong_line(files):
    files[LEVEL_MAP]["excluded"]["L.3.7"]["evidence"] = f"{PB2}:1"


def excluded_missing_file(files):
    files[LEVEL_MAP]["excluded"]["L.3.7"]["evidence"] = "Docs/NoSuch.md:3"


def reason_digits(files):
    files[LEVEL_MAP]["excluded"]["L.3.7"]["reason"] = "스트레스 무대 3종 없음"


def unknown_follow_up(files):
    files[LEVEL_MAP]["excluded"]["L.3.7"]["follow_up"] = "FU-99"


def target_missing(files):
    del files[LEVEL_MAP]["level_maps"]["L.3.4"]


def alias_wrong_line(files):
    files[LEVELS_STATIC]["aliases"][0]["evidence"]["line"] = ALIAS_LINE - 1


def check_warnings(log, root):
    no_alias = run_audit(variant(root, "no-alias", drop_alias), "levels", "no-alias")
    document = no_alias.document or {}
    log.check("별칭 없음 → 모르는 ID 경고·종료 0·엄격 1/3", no_alias.code == 0 and document.get("cd_ids", {}).get("unknown") == ["SE.0.23.10"]
              and "모르는 ID: SE.0.23.10" in document.get("warnings", []) and level_of(document, "L.0.23").get("strict_covered") == 1, f"종료 {no_alias.code} {document.get('cd_ids')}")
    unknown = run_audit(variant(root, "unknown-id", unknown_identifier), "levels", "unknown-id")
    log.check("모르는 ID는 경고(종료 0, unknown_ids=1)", unknown.code == 0 and "unknown_ids=1" in unknown.markdown.splitlines()[-1], f"종료 {unknown.code} {unknown.markdown.splitlines()[-1:]}")


def check_failures(log, root):
    cases = (("count", count_mismatch, None, "수 ≠ 59/58: levels=5 targets=4"),
             ("levels-drift", levels_drift, None, "levels가 content_list와 다름"),
             ("missing-map", missing_map, None, "없는 맵: level_maps.L.0.1 LV_TDFluidNoSuchStage"),
             ("missing-scene", missing_scene, None, "없는 장면: rows G3[0]"),
             ("foreign-item", foreign_item, None, "행에 없는 항목: rows G3[0] S.0.23.1"),
             ("seed-outside-level", seed_outside_level, None, "씨앗 항목 레벨에 맵 없음: rows L11[1] S.0.23.1(L.0.23) LV_TDFluidCloudStage"),
             ("excluded-no-evidence", excluded_without_evidence, None, "근거 없는 excluded: L.3.7"),
             ("excluded-wrong-line", excluded_wrong_line, None, "근거 없는 excluded: L.3.7"),
             ("excluded-missing-file", excluded_missing_file, None, "근거 없는 excluded: L.3.7"),
             ("reason-d49", reason_digits, None, "reason D-49: L.3.7 (숫자 포함)"),
             ("follow-up", unknown_follow_up, None, "후속 대기 follow_up 모름: L.3.7 FU-99"),
             ("target-missing", target_missing, None, "level_maps에 대상 레벨 없음: L.3.4"),
             ("alias-line", alias_wrong_line, None, "별칭 근거 줄 불일치: SE.0.23.10"))
    for name, change, texts, text in cases:
        result = run_audit(variant(root, name, change, texts), "levels", name)
        problems = (result.document or {}).get("problems", [])
        log.check(f"종료 1 {name}", result.code == 1 and any(text in problem for problem in problems), f"종료 {result.code} {problems} {result.stderr[:200]}")


def check_loose_needs_content(log, root):
    world = make_world(root / "loose-no-content", world_files(), maps=(CLOUD, PROPS, SMOKE_SETUP))
    run = run_audit(world, "levels", "loose-no-content")
    document = run.document or {}
    problems = document.get("problems", [])
    log.check("느슨은 Content 맵 필요(L.3.4 맵이 Content 밖 → 느슨 아니오·loose 2·없는 맵 종료 1)", run.code == 1
              and any("없는 맵: level_maps.L.3.4 LV_TDFluidParticleStage" in problem for problem in problems)
              and level_of(document, "L.3.4").get("loose") is False and document.get("counts", {}).get("loose") == 2,
              f"종료 {run.code} {problems} {level_of(document, 'L.3.4')} {document.get('counts')}")


def check_input_errors(log, root):
    files = world_files()
    del files[LEVEL_MAP]
    missing = run_audit(make_world(root / "no-level-map", files), "levels", "no-level-map")
    log.check("종료 2 씨앗 없음", missing.code == 2 and missing.json_bytes is None, f"종료 {missing.code}")
    no_list = run_audit(make_world(root / "no-content-list", world_files(), {CONTENT_LIST: None}), "levels", "no-content-list")
    log.check("종료 2 content_list 없음", no_list.code == 2 and no_list.json_bytes is None, f"종료 {no_list.code}")


def check_real_static(log):
    static = real_static()
    log.check("실제 levels_static 59/58", static.get("expected_counts") == {"levels": 59, "targets": 58}, f"{static.get('expected_counts')}")
    log.check("실제 별칭 SE.0.23.10 → SE.0.23.10.2(FU-16)", [(alias["from"], alias["to"], alias.get("remove_after")) for alias in static.get("aliases", [])] == [("SE.0.23.10", "SE.0.23.10.2", "FU-16")],
              f"{static.get('aliases')}")


def run(tmp_root):
    log = FixtureLog("levels")
    root = Path(tmp_root)
    check_normal(log, make_world(root / "normal", world_files()))
    check_warnings(log, root)
    check_failures(log, root)
    check_loose_needs_content(log, root)
    check_input_errors(log, root)
    check_real_static(log)
    return log.result()

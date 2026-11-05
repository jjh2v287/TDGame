"""실행 환경: 시스템 Python 3(에디터 불필요, calib_judge import 때문에 numpy·Pillow·scipy 필요, git 함수는 가짜로 바꿔 git 을 부르지 않음, T147 만 fluid_tuning_patch materialize 를 하위 프로세스로 읽기 전용 실행).
실행 명령: python -B Tools/FluidFX/calib_final_offline_test.py (종료 0 전부 통과 / 1 실패, 마지막 줄 SUMMARY passed=N/M)
출력: Saved/FluidFX/calib/final-offline-test/(실행마다 지움, 합성 fixture·산출물)과 PASS/FAIL 줄
상태: 2026-11-05 calib_final_verify 몫(명세 8.3 T130~T150, 8.4·8.5 의 calib_final_verify 부분)과 calib_batch 최종 모드(T100~T115·T161~T168·T179·T182~T188 의 calib_batch 부분, 가짜 run_tool 이 calib_judge·prereg_lock·calib_final_verify 를 같은 프로세스에서 실제로 부르고 스윕·A/B 판정·에디터·보류만 가짜), 개정 G 소수정 3건(관문 보고 표준 오류만·잠금 단계 interrupted·r2 covered 자식) 시험, 2026-11-06 개정 H 회귀 시험(반박자 재현 RI s1~s5·적합 R1~R4·fixture A1·D2·N1·C1·I1·H3: F0 관문 끊김 재개·남은 잠금·실행 폴더 재추첨·F0 하한·일지 손상·끝난 interrupted 스윕·outcome 삭제·W11 닻 커밋 시각·시작 예외·r2 시작 관문·자기 검증 복사본 대조, HEAD 코드에 돌려 빨강 확인), 개정 I 회귀 시험(반박 적합 M-1 N1·N2·N1r·M-2 N3·N3b, RI M1'·n2·n3 의 시도 끝 꼴·n4, fixture X1·B1, P-42: --init-journal·pin-journal·가짜 git 이력의 journal_id 첫 고정 대조, 관문 다시 계산, 시도 끝 산출물 sha, 실행 폴더 재사용 금지. 앞 판 동작으로 함수를 되돌린 구동기에서 빨강 확인)이 있다. 가짜 git 은 w-anchor 경로별 커밋 목록이고 reset_batch·simulate_batch 는 새 W 로 보고 고정을 지운다. 실제 배치·에디터는 돌리지 않는다.
"""
import contextlib
import copy
import io
import json
import os
import secrets
import shutil
import subprocess
import sys
import traceback
from datetime import datetime, timedelta
from pathlib import Path, PurePosixPath
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
import calib_batch  # noqa: E402
import calib_final_verify as verify  # noqa: E402
import calib_judge  # noqa: E402
import fluid_tuning_patch  # noqa: E402
import prereg_lock  # noqa: E402

ROOT = prereg_lock.ROOT
TEST_ROOT = "Saved/FluidFX/calib/final-offline-test"
REAL_FINAL_DIR = "Saved/FluidFX/calib/final"
REAL_OVERLAY = "Saved/FluidFX/calib/accepted/overlay.json"
RESULTS = []
GIT = {"problems": [], "time": "2026-11-05T00:00:00+09:00", "history": None, "commits": {}}
PATCH_TOOL = {"calls": [], "fail": None, "tamper": False}
ARCHIVE_NAME = "aborted-20261105-120000"

LEVEL = "/Game/Fixture/{}".format
HEAT = "Kinds.Flamethrower.TDFluidSimulationConfig.HeatCoolingPerSec"
AMBIENT = "Kinds.CloudCyclone.TDFluidSimulationConfig.AmbientRefillPerSec"
HARDNESS = "Kinds.SnowTrack.TDFluidInputBrushSettings.BrushHardness"
WIND = "Globals.TDFluidCloudSceneSettings.WindCurtainSpeedCmPerSec"
MI_SHIELD, MI_CAP, MI_PASSIVE, MI_LAKE, MI_SNOW = (f"/Game/Fixture/MI_TD_{name}" for name in ("Shield", "Cap", "Passive", "Lake", "Snow"))
NS_FIXTURE = "/Game/Fixture/NS_TD_Fixture"
COVERAGE = f"Look:Materials.{MI_SHIELD}.Scalars.Coverage"
EMISSIVE = f"Look:Materials.{MI_CAP}.Scalars.Emissive"
PASSIVE = f"Look:Materials.{MI_PASSIVE}.Scalars.PassiveCoverage"
LAKE = f"Look:Materials.{MI_LAKE}.Scalars.FogDensity"
CONTRAST = f"Look:Materials.{MI_SNOW}.Scalars.Contrast"
SPEED = f"Look:NiagaraSystems.{NS_FIXTURE}.Floats.User.Speed"
BUOYANCY = f"Look:NiagaraSystems.{NS_FIXTURE}.Floats.User.Buoyancy"
LIGHT = {"level": LEVEL("LV_Fog"), "label": "TDGen_LabGreenLight", "component": "light_component", "property": "volumetric_scattering_intensity"}
LIGHT_ID = fluid_tuning_patch.describe_actor_prop(LIGHT)
METRIC_BASES = {"m1": 1.0, "m2": 2.0}
SHIFT = 0.2

PLANS = {
    "pa": {"scenes": [("a_cyclone", "LV_Track")], "params": [{"json_path": AMBIENT, "values": [0.2]}]},
    "pb": {"scenes": [("b_cap", "LV_Cloud"), ("b_shield", "LV_Cloud")], "params": [{"json_path": EMISSIVE, "values": [0.03], "base": 0.015, "scenes": ["b_cap"]}]},
    "pb_src": {"scenes": [("b_cap", "LV_Cloud"), ("b_shield", "LV_Cloud")], "params": [{"json_path": COVERAGE, "values": [0.6], "base": 0.8, "scenes": ["b_shield"]}]},
    "pn": {"scenes": [("n_mass", "LV_Mass")], "params": [{"json_path": HEAT, "values": [4.0]}], "base_tuning": [{"json_path": BUOYANCY, "value": -300.0}]},
    "pn_p20": {"scenes": [("n_mass", "LV_Mass")], "params": [{"json_path": SPEED, "values": [750.0], "base": 900.0}], "base_tuning": [{"json_path": BUOYANCY, "value": -300.0}]},
    "pc": {"scenes": [("c_groove", "LV_Track")], "params": [{"json_path": HARDNESS, "values": [0.6]}]},
    "pc_p20": {"scenes": [("c_groove", "LV_Track")], "params": [{"json_path": CONTRAST, "values": [0.7], "base": 1.0}]},
    "pg": {"scenes": [("g_wall", "LV_Storm")], "params": [{"json_path": WIND, "values": [75.0]}]},
    "pf2": {"scenes": [("f2_field", "LV_Fog")], "params": [{"json_path": PASSIVE, "values": [0.35], "base": 0.5}]},
    "pu": {"scenes": [("u_pit", "LV_Water")], "params": [{"json_path": LAKE, "values": [0.004], "base": 0.002}]},
    "pf7": {"scenes": [("f7_lights", "LV_Fog")], "params": [{"actor_prop": {key: LIGHT[key] for key in ("label", "component", "property")}, "values": [8.0], "base": 4.0}]},
    "pf5": {"scenes": [("f5_cut", "LV_Fog")], "params": []},
}
ADOPTIONS = {
    "1": {"json_path": HEAT, "value": 4.0, "plan": "pn", "measured_under_base": [BUOYANCY]},
    "2": {"json_path": AMBIENT, "value": 0.2, "plan": "pa"},
    "3": {"json_path": COVERAGE, "value": 0.6, "plan": "pb_src", "base": 0.8},
    "9": {"json_path": HARDNESS, "value": 0.6, "plan": "pc", "split_group": True},
    "10": {"json_path": SPEED, "value": 750.0, "plan": "pn_p20", "base": 900.0, "crlf": True},
    "11": {"json_path": EMISSIVE, "value": 0.03, "plan": "pb", "base": 0.015},
    "12": {"json_path": CONTRAST, "value": 0.7, "plan": "pc_p20", "base": 1.0},
    "13": {"json_path": WIND, "value": 75.0, "plan": "pg"},
    "15": {"json_path": PASSIVE, "value": 0.35, "plan": "pf2", "base": 0.5},
    "17": {"json_path": LAKE, "value": 0.004, "plan": "pu", "base": 0.002},
    "18": {"actor_prop": LIGHT, "value": 8.0, "plan": "pf7"},
}
F0_RUNS = (("g13", "pg", "13"), ("f2", "pf2", "15"), ("uw", "pu", "17"), ("f7", "pf7", "18"))
AB_ROWS = (("pawab", "pa", "pa", "pa", "2", "2", "always_record"), ("pbwab11", "pb", "pb", "pb", "11", "11", "on_fail"),
           ("pbwab3", "pb_src", "pb_src", "pb", "3", "3", "always_record"), ("pnwab", "pn", "pn", "pnp20", "1", "10", "on_fail"),
           ("pnp20wab", "pn_p20", "pn_p20", "pnp20", "10", "10", "on_fail"), ("pcwab9", "pc", "pc", "pcw", "9", "12", "on_fail"),
           ("pcwab12", "pc_p20", "pc_p20", "pcw", "12", "12", "on_fail"), ("g13wab", "pg", "pg", "g13", "13", "13", "on_fail"),
           ("f2wab", "pf2", "pf2", "f2", "15", "15", "on_fail"), ("uwwab", "pu", "pu", "uw", "17", "17", "on_fail"),
           ("f7wab", "pf7", "pf7", "f7", "18", "18", "on_fail"))
TEST_SOURCES = {
    "HeatTests.cpp": ("TDGame.FluidFX.Fixture.HeatOwner", "HeatCoolingPerSec Flamethrower"),
    "TwoOwnerTests.cpp": ("TDGame.FluidFX.Fixture.TwoOwners", "Coverage MI_TD_Shield PassiveCoverage MI_TD_Passive"),
    "KindlessTests.cpp": ("TDGame.FluidFX.Fixture.KindlessHardness", "BrushHardness only"),
    "NoOwnerTests.cpp": ("TDGame.FluidFX.Fixture.NoOwner", "nothing"),
    "TimingTests.cpp": ("TDGame.FluidFX.Fixture.Timing", "frame timing"),
    "HeaderTests.cpp": (verify.HEADER_SYNC_TEST, "HeatCoolingPerSec Flamethrower header"),
    "PinTests.cpp": ("TDGame.FluidFXRhi.Fixture.PinMatch", "pinned"),
}
TEST_CLASSES = {"version": 2, "rules": {}, "tests": [], "timing_record_only": ["TDGame.FluidFX.Fixture.Timing"], "numeric_pinned": [],
                "numeric_pin_when_failures_match": {"TDGame.FluidFXRhi.Fixture.PinMatch": ["허용 문구"]}}


def check(name, condition, detail=""):
    RESULTS.append((name, bool(condition)))
    suffix = "" if condition or not detail else f" — {str(detail)[:600]}"
    print(f"{'PASS' if condition else 'FAIL'} {name}{suffix}")


@contextlib.contextmanager
def patched(target, **replacements):
    originals = {name: getattr(target, name) for name in replacements}
    for name, value in replacements.items():
        setattr(target, name, value)
    try:
        yield
    finally:
        for name, value in originals.items():
            setattr(target, name, value)


def absolute(text):
    return ROOT / text


def write_text(text, content):
    path = absolute(text)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content.encode("utf-8"))


def write_json(text, value):
    write_text(text, verify.json_text(value))


def read_json(text):
    return json.loads(absolute(text).read_text(encoding="utf-8"))


def sha(text):
    return prereg_lock.file_sha256(absolute(text))


def now_text():
    return datetime.now().astimezone().isoformat()


def append_byte(text, data=b" "):
    with absolute(text).open("ab") as handle:
        handle.write(data)


@contextlib.contextmanager
def restored(*texts):
    saved = {text: absolute(text).read_bytes() if absolute(text).is_file() else None for text in texts}
    try:
        yield
    finally:
        for text, data in saved.items():
            if data is None:
                if absolute(text).is_file():
                    absolute(text).unlink()
                continue
            absolute(text).parent.mkdir(parents=True, exist_ok=True)
            absolute(text).write_bytes(data)


def run_main(argv):
    output = io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
        code = verify.main([str(part) for part in argv])
    return code, output.getvalue()


def gate_of(function, *arguments):
    try:
        function(*arguments)
    except verify.GateFailure as failure:
        return failure.exit_code, " | ".join(failure.problems)
    except verify.INPUT_ERRORS as error:
        return verify.EXIT_INPUT, f"{type(error).__name__}: {error}"
    return 0, ""


def plan_path(world, name):
    return f"{world['root']}/plans/{name}.json"


def scene_object(world, name, level):
    return {"name": name, "level": LEVEL(level), "sources": ["probe"], "regions_template": f"{world['root']}/regions/{name}.json", "targets": f"{world['root']}/targets/{name}.json"}


def plan_document(world, name, definition, series_scenes=()):
    scenes, targets = [], []
    for scene_name, level in definition["scenes"]:
        scene = scene_object(world, scene_name, level)
        if scene_name in series_scenes:
            scene["shots"] = {"kind": "series"}
        scenes.append(scene)
        targets.append({"scene": scene_name, "metric": "m1", "low": 0.0, "high": 4.0, "tolerance": 0.0, "role": "score"})
        targets.append({"scene": scene_name, "metric": "m2", "low": 0.0, "high": 4.0, "tolerance": 0.0, "role": "constraint"})
    document = {"id": name, "scenes": scenes, "metrics": {"targets": targets, "derived": []}, "params": copy.deepcopy(definition["params"])}
    if definition.get("base_tuning"):
        document["base_tuning"] = copy.deepcopy(definition["base_tuning"])
    return document


def scene_values(plan, behaviors, shift=SHIFT):
    scenes = []
    for scene in plan["scenes"]:
        behavior = behaviors.get(scene["name"], "pass")
        steps = []
        for index in range(3):
            metrics = {}
            for target in plan["metrics"]["targets"]:
                if target.get("scene") != scene["name"]:
                    continue
                value = METRIC_BASES[target["metric"]] + (shift if behavior == "fail" else 0.0) + (shift if behavior == "undetermined" and index == 2 else 0.0)
                metrics[target["metric"]] = [value - 0.01, value, value + 0.01]
            steps.append({"step": index + 1, "variant": "v00", "valid": True, "name": f"{scene['name']}-{index + 1}", "metrics": metrics})
        scenes.append({"name": scene["name"], "sources": list(scene["sources"]), "regions_template": scene["regions_template"], "targets": scene["targets"], "steps": steps})
    return scenes


def make_sweep(plan_text, lock_text, stage, overlay_sha, canonical, behaviors, run_dir):
    plan = read_json(plan_text)
    return {"tool": "calib_sweep", "plan": plan_text, "plan_sha256": sha(plan_text), "stage": stage, "lock": lock_text, "lock_sha256": sha(lock_text),
            "overlay_sha256": overlay_sha, "canonical_tuning_sha256": canonical["tuning"], "canonical_look_sha256": canonical["look"], "started_at": now_text(),
            "invalid_count": 0, "run_dir": run_dir, "metrics": copy.deepcopy(plan["metrics"]), "params": copy.deepcopy(plan.get("params") or []),
            "scenes": scene_values(plan, behaviors)}


def before_documents():
    tuning = {"StructDefaults": {"TDFluidSimulationConfig": {"HeatCoolingPerSec": 1.0, "AmbientRefillPerSec": 0.05}, "TDFluidInputBrushSettings": {"BrushHardness": 0.3}},
              "Groups": {}, "Kinds": {"Flamethrower": {"TDFluidSimulationConfig": {"HeatCoolingPerSec": 2.5}}, "CloudCyclone": {"TDFluidSimulationConfig": {"AmbientRefillPerSec": 0.1}},
                                       "SnowTrack": {}},
              "Globals": {"TDFluidCloudSceneSettings": {"WindCurtainSpeedCmPerSec": 150.0}}}
    look = {"Materials": {MI_SHIELD: {"Scalars": {"Coverage": 0.8}}, MI_CAP: {"Scalars": {"Emissive": 0.015}}, MI_PASSIVE: {"Scalars": {"PassiveCoverage": 0.5}},
                          MI_LAKE: {"Scalars": {"FogDensity": 0.002}}, MI_SNOW: {"Scalars": {}}},
            "NiagaraSystems": {NS_FIXTURE: {"Floats": {"User.Speed": 900.0, "User.Buoyancy": -300.0}}}}
    return tuning, look


def apply_assignments(tuning, look, assignments):
    tuning, look = copy.deepcopy(tuning), copy.deepcopy(look)
    for entry in assignments:
        json_path = entry["json_path"]
        if fluid_tuning_patch.is_look_path(json_path):
            table_key, package, section, name = fluid_tuning_patch.parse_look_path(json_path)
            look.setdefault(table_key[len(fluid_tuning_patch.LOOK_PREFIX):], {}).setdefault(package, {}).setdefault(section, {})[name] = entry["value"]
            continue
        section, kind_name, struct_name, field_name = fluid_tuning_patch.parse_json_path(json_path)
        if section == fluid_tuning_patch.GLOBALS_SECTION:
            tuning[section][struct_name][field_name] = entry["value"]
            continue
        tuning[section].setdefault(kind_name, {}).setdefault(struct_name, {})[field_name] = entry["value"]
    return tuning, look


def write_pair(directory, tuning, look):
    write_json(f"{directory}/{fluid_tuning_patch.TUNING_FILE_NAME}", tuning)
    write_json(f"{directory}/{fluid_tuning_patch.LOOK_FILE_NAME}", look)
    return {"tuning": sha(f"{directory}/{fluid_tuning_patch.TUNING_FILE_NAME}"), "look": sha(f"{directory}/{fluid_tuning_patch.LOOK_FILE_NAME}")}


def pair_sha(directory):
    return {"tuning": sha(f"{directory}/{fluid_tuning_patch.TUNING_FILE_NAME}"), "look": sha(f"{directory}/{fluid_tuning_patch.LOOK_FILE_NAME}")}


def fake_patch_tool(world):
    def run(arguments):
        command = arguments[0]
        PATCH_TOOL["calls"].append(command)
        if PATCH_TOOL["fail"] == command:
            return 1, "", "가짜 fluid_tuning_patch 실패"
        options, index = {}, 1
        while index < len(arguments):
            if index + 1 < len(arguments) and not arguments[index + 1].startswith("--"):
                options[arguments[index]] = arguments[index + 1]
                index += 2
                continue
            options[arguments[index]] = True
            index += 1
        tuning, look = before_documents()
        if command == "restore":
            write_pair(world["live"], tuning, look)
            return 0, json.dumps({"restored": []}), ""
        if command == "overlay-remove":
            overlay = fluid_tuning_patch.read_overlay(absolute(options["--overlay"]))
            removed = [entry for entry in overlay["assignments"] if entry["json_path"] == options["--json-path"]]
            overlay["assignments"] = [entry for entry in overlay["assignments"] if entry["json_path"] != options["--json-path"]]
            if PATCH_TOOL["tamper"] and overlay["assignments"]:
                overlay["assignments"][0]["value"] = 123.0
            overlay["history"].extend(fluid_tuning_patch.history_entries("remove", removed, options["--reason"], "2026-11-05T12:00:00"))
            fluid_tuning_patch.write_overlay(absolute(options["--overlay"]), overlay)
            return 0, json.dumps({"overlay_sha256": sha(options["--overlay"])}), ""
        overlay = fluid_tuning_patch.read_overlay(absolute(options.get("--overlay") or options.get("--from-overlay")))
        tuning, look = apply_assignments(tuning, look, overlay["assignments"])
        if command == "materialize":
            shas = write_pair(options["--out"], tuning, look)
            return 0, json.dumps({"tuning_sha256": shas["tuning"], "look_sha256": shas["look"]}), ""
        if command == "write-canonical":
            shas = write_pair(world["live"], tuning, look)
            return 0, json.dumps({"written_sha256": shas["tuning"], "written_look_sha256": shas["look"]}), ""
        return 2, "", f"가짜 fluid_tuning_patch: 모르는 명령 {command}"
    return run


def world_scope(world):
    stack = contextlib.ExitStack()
    stack.enter_context(patched(verify, CANONICAL_BEFORE_SHA256=world["before_sha"], FOG_GENERATOR=world["fog_generator"], FOG_STAGE_REPORT=world["fog_report"],
                                TEST_CLASSES_PATH=world["test_classes"], canonical_now_sha=lambda: pair_sha(world["live"]), run_patch_tool=fake_patch_tool(world)))
    return stack


def fixture_note(options):
    return f"fixture {calib_batch.journal_note(options['journal_id'])}"


def make_lock(files, out_text, note="fixture"):
    path = absolute(out_text)
    if path.exists():
        path.unlink()
    prereg_lock.create_lock(files, path, note)
    return out_text


def make_source(world, number, plan_name, kind, scene_names=None):
    plan_text = plan_path(world, plan_name)
    folder = f"{world['root']}/src/{plan_name}-{number}"
    lock_text = make_lock(prereg_lock.required_files(plan_text), f"{folder}/lock.json")
    stage = "confirm" if kind == "confirm" else "screen"
    sweep_text = f"{folder}/{stage}-1/sweep.json"
    sweep = make_sweep(plan_text, lock_text, stage, None, world["before_sha"], {}, f"{folder}/{stage}-1")
    write_json(sweep_text, sweep)
    scenes = calib_judge.judge_sweep_scenes(sweep, read_json(plan_text), absolute(f"{folder}/{stage}-1/judge"))
    plan_sha = verify.crlf_sha(plan_text) if (ADOPTIONS.get(number) or {}).get("crlf") else sha(plan_text)
    if kind == "screen":
        judge = {"tool": "calib_judge", "version": 2, "stage": "screen", "plan": plan_text, "plan_sha256": plan_sha, "lock_sha256": sha(lock_text),
                 "screen_sweep": sweep_text, "screen_sweep_sha256": sha(sweep_text), "scenes": scenes, "keep": True, "best": None, "insensitive": False}
        judge_text = f"{folder}/screen-1/screen.json"
    else:
        for scene in scenes:
            scene["candidate_medians"] = {target["metric"]: METRIC_BASES[target["metric"]] for target in scene["targets"]}
        judge = {"tool": "calib_judge", "version": 2, "stage": "confirm", "sweep": sweep_text, "lock": lock_text, "plan": plan_text, "plan_sha256": plan_sha,
                 "lock_sha256": sha(lock_text), "scenes": scenes, "adopt": {"decision": True, "variant": "v01"}}
        judge_text = f"{folder}/confirm-1/judge.json"
    write_json(judge_text, judge)
    return {"judge": judge_text, "lock": lock_text, "sweep": sweep_text, "plan": plan_text}


def overlay_entry(number, source):
    adoption = ADOPTIONS[number]
    origin = {"plan": source["plan"], "plan_sha256": read_json(source["judge"])["plan_sha256"], "confirm_judge": source["judge"], "judge_sha256": sha(source["judge"]),
              "variant": "v01", "lock_sha256": sha(source["lock"])}
    if "actor_prop" in adoption:
        return dict(copy.deepcopy(adoption["actor_prop"]), value=adoption["value"], **{"from": origin})
    entry = {"json_path": adoption["json_path"], "value": adoption["value"], "split_group": bool(adoption.get("split_group")), "kind": "knob", "from": origin}
    if "base" in adoption:
        entry["base"] = adoption["base"]
        entry["levels"] = [LEVEL(PLANS[adoption["plan"]]["scenes"][-1][1])]
    if adoption.get("measured_under_base"):
        entry["measured_under_base"] = list(adoption["measured_under_base"])
    return entry


def source_entry(number, kind, judge_text, scenes, binding=None, lineage="clean", evidence=None):
    entry = {"adoption": number, "kind": kind, "judge": judge_text, "scenes": scenes, "binding": binding or {"kind": "same"}, "lineage": lineage}
    if evidence:
        entry["lineage_evidence"] = evidence
    return entry


def build_spec(world, sources):
    root, final_dir = world["root"], world["final"]
    plan = lambda name: plan_path(world, name)
    empty, actor = f"{final_dir}/overlay-empty.json", f"{final_dir}/overlay-actor-props.json"
    runs = []
    for run_id, plan_name, number in F0_RUNS:
        runs.append({"id": run_id, "block": "F0", "plan": plan(plan_name), "overlay": actor if number == "18" else empty, "lock": sources[number]["lock"],
                     "verify_against": sources[number]["judge"], "sources": [source_entry(number, "confirm", sources[number]["judge"], [PLANS[plan_name]["scenes"][0][0]])]})
    runs += [
        {"id": "pa", "block": "F1", "plan": plan("pa"), "overlay": empty, "sources": [source_entry("2", "confirm", sources["2"]["judge"], ["a_cyclone"])]},
        {"id": "pb", "block": "F1", "plan": plan("pb"), "overlay": empty,
         "sources": [source_entry("11", "confirm", sources["11"]["judge"], ["b_cap"]),
                     source_entry("3", "confirm", sources["3"]["judge"], ["b_shield"], {"kind": "cross_plan", "source_plan": plan("pb_src")}, "scene_changed", ["fixture-commit"])]},
        {"id": "pnp20", "block": "F1", "plan": plan("pn_p20"), "overlay": empty,
         "sources": [source_entry("10", "confirm", sources["10"]["judge"], ["n_mass"], {"kind": "eol", "eol_note": f"{root}/eol/eol-note.json"})]},
        {"id": "pcw", "block": "F1", "plan": f"{final_dir}/plans/pc_p20-w.json", "overlay": empty,
         "sources": [source_entry("12", "confirm", sources["12"]["judge"], ["c_groove"], {"kind": "copy", "source_plan": plan("pc_p20")})]},
        {"id": "f5", "block": "F1", "plan": plan("pf5"), "overlay": empty, "sources": [source_entry("f5", "screen", sources["f5"]["judge"], ["f5_cut"], lineage="other_after")]},
    ]
    covered = [{"adoption": "1", "by_run": "pnp20", "by_adoption": "10", "rule": "P-20",
                "fallback": {"id": "pn", "block": "F1", "plan": plan("pn"), "overlay": empty, "sources": [source_entry("1", "confirm", sources["1"]["judge"], ["n_mass"], lineage="scene_changed")]}},
               {"adoption": "9", "by_run": "pcw", "by_adoption": "12", "rule": "P-20",
                "fallback": {"id": "pc", "block": "F1", "plan": plan("pc"), "overlay": empty, "sources": [source_entry("9", "confirm", sources["9"]["judge"], ["c_groove"], lineage="scene_changed")]}}]
    ab = [{"id": identifier, "scene_plan": plan(scene_plan), "knob_plan": plan(knob_plan), "plan": f"{final_dir}/plans/{scene_plan}-wab.json", "overlay": empty,
           "for_run": for_run, "for_adoption": for_adoption, "trigger_adoption": trigger_adoption, "trigger": trigger, "cross": False}
          for identifier, scene_plan, knob_plan, for_run, for_adoption, trigger_adoption, trigger in AB_ROWS]
    ab.append({"id": "f5xf7wab", "scene_plan": plan("pf5"), "knob_plan": plan("pf7"), "plan": f"{final_dir}/plans/pf5-x-pf7-wab.json", "overlay": empty,
               "for_run": "f5", "for_adoption": "18", "trigger_adoption": "f5", "trigger": "on_fail", "cross": True})
    adoptions = {number: ({"actor_prop": copy.deepcopy(adoption["actor_prop"])} if "actor_prop" in adoption else {"json_path": adoption["json_path"]})
                 for number, adoption in ADOPTIONS.items()}
    return {"version": 4, "edition": "e1", "final_dir": final_dir, "overlay": f"{final_dir}/overlay-frozen.json", "canonical_before_dir": f"{final_dir}/canonical-before",
            "preview_dir": f"{final_dir}/preview", "w_expect": f"{final_dir}/w-expect.json", "test_owner": f"{final_dir}/test_owner.json", "pie_files": [f"{root}/pie/pie-plan.json"],
            "adoptions": adoptions, "influences": ["f5"], "plan_copies": [{"plan": plan("pc_p20"), "out": f"{final_dir}/plans/pc_p20-w.json"}], "runs": runs, "covered": covered, "ab": ab,
            "p22": {"plans": [plan("pn"), plan("pn_p20")], "ns_package": NS_FIXTURE, "runs": ["pnp20"]}, "source_scene_exceptions": [],
            "tests_required": {"w11": ["w4-header", "w10-rhi"], "w13": ["w4-header", "w10-rhi", "w13-headless", "w13-rhi"]}}


def fog_generator_text(value):
    return f'FIXTURE_LIGHTS = [\n    {{"label": "LabGreenLight", "intensity": 2000.0, "volumetric_scattering_intensity": {value}}},\n]\n'


def write_tests(world, label, failures=(), incomplete=False):
    results = [{"path": name, "result": "Success", "events": []} for name in ("TDGame.FluidFX.Fixture.Pass",)]
    results += [{"path": name, "result": "Fail", "events": list(events)} for name, events in failures]
    text = f"{world['root']}/tests/{label}-tests.json"
    write_json(text, {"results": results, "incomplete": incomplete, "crashed": False, "found_count": len(results)})
    return text


def build_world(name, preprocess=None):
    root = f"{TEST_ROOT}/{name}"
    final_dir = f"{root}/final"
    world = {"name": name, "root": root, "final": final_dir, "w_anchor": f"{root}/w-anchor.json", "live": f"{root}/live", "test_classes": f"{root}/test_classes.json",
             "fog_generator": f"{root}/fog/editor_make_fluid_fog_stage.py", "fog_report": f"{root}/fog/fluid_fog_stage.json"}
    shutil.rmtree(absolute(root), ignore_errors=True)
    tuning, look = before_documents()
    world["before_sha"] = write_pair(f"{final_dir}/canonical-before", tuning, look)
    write_json(world["test_classes"], TEST_CLASSES)
    write_text(world["fog_generator"], fog_generator_text(8.0))
    for plan_name, definition in PLANS.items():
        document = plan_document(world, plan_name, definition)
        for scene in document["scenes"]:
            write_json(scene["regions_template"], {"regions": [{"name": scene["name"]}]})
            write_json(scene["targets"], {"scene": scene["name"]})
        write_json(plan_path(world, plan_name), document)
    write_json(f"{root}/eol/eol-note.json", {"plans": [{"plan": plan_path(world, "pn_p20"), "from": "CRLF", "to": "LF"}]})
    write_json(f"{root}/pie/pie-plan.json", {"shots": []})
    for file_name, (test_name, body) in TEST_SOURCES.items():
        write_text(f"{root}/code/Tests/{file_name}", f'IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFixture, "{test_name}", 0)\n// {body}\n')
    with world_scope(world):
        sources = {number: make_source(world, number, adoption["plan"], "confirm") for number, adoption in ADOPTIONS.items()}
        sources["f5"] = make_source(world, "f5", "pf5", "screen")
        overlay = {"version": 2, "assignments": [overlay_entry(number, sources[number]) for number in verify.sorted_adoptions(ADOPTIONS) if "json_path" in ADOPTIONS[number]],
                   "actor_props": [overlay_entry("18", sources["18"])], "history": []}
        world["sources"] = sources
        spec = build_spec(world, sources)
        if preprocess:
            preprocess(world, spec, overlay)
        write_json(spec["overlay"], overlay)
        preview_tuning, preview_look = apply_assignments(tuning, look, overlay["assignments"])
        world["after_sha"] = write_pair(spec["preview_dir"], preview_tuning, preview_look)
        write_pair(world["live"], preview_tuning, preview_look)
        write_json(f"{final_dir}/builder-before/preset_builder_dump.json", {"Flamethrower": {"TDFluidSimulationConfig": {"HeatCoolingPerSec": 2.5}},
                                                                            "CloudCyclone": {"TDFluidSimulationConfig": {"AmbientRefillPerSec": 0.1}}})
        write_json(spec["w_expect"], verify.build_w_expect(spec["overlay"], spec["canonical_before_dir"], spec["preview_dir"], f"{final_dir}/builder-before/preset_builder_dump.json"))
        write_json(spec["test_owner"], verify.build_test_owner(spec["overlay"], world["test_classes"], f"{root}/code"))
        write_json(f"{final_dir}/final-spec.json", spec)
        world["spec"] = f"{final_dir}/final-spec.json"
        world["setup"] = [run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir]),
                          run_main(["expect", "--spec", world["spec"], "--out", f"{final_dir}/expect.json", "--queue-out", f"{final_dir}/final_queue.json",
                                    "--w-anchor", world["w_anchor"]])]
        world["setup"].append(run_anchor(world))
        write_json(f"{final_dir}/write-canonical.json", {"written_sha256": world["after_sha"]["tuning"], "written_look_sha256": world["after_sha"]["look"]})
        write_json(world["fog_report"], {"preset_tuning_sha256": world["after_sha"]["tuning"]})
    world["ns_dump"] = f"{root}/ns_after/NS_TD_Fixture.dump.json"
    write_ns_dump(world, {"User.Speed": 750.0, "User.Buoyancy": -300.0})
    for label in ("w4-header", "w10-rhi", "w13-headless", "w13-rhi"):
        write_tests(world, label)
    return world


def write_ns_dump(world, floats):
    variables = [{"name": name, "type": {"classStructOrEnum": {"refPath": "/Script/Niagara.NiagaraFloat"}}, "defaultValue": {"struct": {"refPath": "/Script/Niagara.NiagaraFloat"},
                                                                                                                      "value": {"value": value}}}
                 for name, value in floats.items()]
    write_json(world["ns_dump"], {"sections": {"UserVariables": {"userVariables": variables}}})


def run_anchor(world, spec_text=None, w_anchor_out=None):
    return run_main(["anchor", "--spec", spec_text or world["spec"], "--w-anchor", world["w_anchor"], "--w-anchor-out", w_anchor_out or world["w_anchor"]])


def setup_ok(world):
    return all(code == 0 for code, _ in world["setup"])


def setup_detail(world):
    return "; ".join(output.strip()[-300:] for code, output in world["setup"] if code != 0)


def queue_judge_files(queue):
    return list(queue["final"]["judge_files"]) + [queue["final"]["anchor"]]


def new_attempt(journal_dir, step, **fields):
    attempt = {"step": step, "stage": None, "opportunity": None, "try": None, "folder": None, "log": f"{journal_dir}/batch.log", "started_at": now_text(), "exit": 0, "state": "done"}
    attempt.update(fields)
    return attempt


def run_rows_of(expect, run_id):
    return [row for row in expect["rows"] if row["run"] == run_id]


def target_state_of(expect, decision_states, statuses, key):
    covered = next((item for item in expect["covered"] if item["adoption"] == key), None)
    run_id, adoption = (covered["by_run"], covered["by_adoption"]) if covered else (next(row["run"] for row in expect["rows"] if row["adoption"] == key), key)
    if statuses.get(run_id) == "incomplete":
        return "incomplete"
    return (decision_states.get(run_id) or {}).get(adoption)


def environment_attempts(journal_dir, folder_base, stage, opportunity, mode, start_count):
    attempts = []
    for try_number in (1, 2):
        folder = f"{folder_base}/{stage}-{start_count + try_number}"
        absolute(folder).mkdir(parents=True, exist_ok=True)
        exit_code, state = (None, "interrupted") if mode == "interrupted" else (1, "done")
        attempts.append(new_attempt(journal_dir, "sweep", stage=stage, opportunity=opportunity, folder=folder, exit=exit_code, state=state, **{"try": try_number}))
    return attempts


def world_final(expect):
    return expect["final_dir"]


def simulate_verify(world, expect, queue, queue_item, journal_dir, opportunities_plan, options, stop):
    run_id = queue_item["run"]
    run = expect["runs"][run_id]
    is_f0 = run["block"] == "F0"
    rows = run_rows_of(expect, run_id)
    keys = verify.sorted_adoptions({row["adoption"] for row in rows})
    scenes_of = {key: {row["scene"] for row in rows if row["adoption"] == key} for key in keys}
    stem = PurePosixPath(queue_item["plan"]).stem
    folder_base = f"{journal_dir}/runs/{stem}"
    attempts, artifacts = [], {}
    judge_files = queue_judge_files(queue)
    failure = options.get("fail") if (options.get("fail") or {}).get("item") == run_id else None
    item = {"plan": queue_item["plan"], "block": run["block"], "chain": None, "status": "pending", "runs": {}, "attempts": 0, "wall_seconds": 0.0, "estimate_seconds": 0.0, "reason": ""}
    final = {"kind": "verify", "run": run_id, "edition": expect["edition"], "attempts": attempts, "adoptions_first": {}, "adoptions": {}, "official_first": {}, "official_rerun": {},
             "self_check": None, "rerun": 0, "overlay_sha256_before": sha(queue_item["overlay"]), "overlay_sha256_after": None, "artifact_sha256": artifacts}
    item["final"] = final

    def fail_here(step):
        if not failure or failure["step"] != step:
            return False
        attempts.append(new_attempt(journal_dir, step, stage="verify" if step in ("sweep", "official", "compare") else None, exit=failure.get("exit"), state=failure.get("state", "done")))
        item["status"] = "failed"
        stop.update(event="E2", item=run_id, reason=f"{step} 종료 {failure.get('exit')}")
        return True

    if is_f0:
        attempts.append(new_attempt(journal_dir, "lock_verify"))
        if fail_here("judge_lock_create"):
            return item
        make_lock(judge_files, queue_item["judge_lock"], fixture_note(options))
        attempts.append(new_attempt(journal_dir, "judge_lock_create"))
        artifacts[queue_item["judge_lock"]] = sha(queue_item["judge_lock"])
        item["runs"].update(lock=queue_item["lock"], judge_lock=queue_item["judge_lock"])
    else:
        if fail_here("lock_create"):
            return item
        make_lock(prereg_lock.required_files(queue_item["plan"]) + judge_files, queue_item["lock"], fixture_note(options))
        attempts.append(new_attempt(journal_dir, "lock_create"))
        artifacts[queue_item["lock"]] = sha(queue_item["lock"])
        item["runs"]["lock"] = queue_item["lock"]
    lock_text = queue_item["lock"]
    count, outcomes, states = 0, {}, {}
    self_checks = {"first": None, "rerun": None}
    for index, opportunity in enumerate(verify.OPPORTUNITIES):
        if opportunity == "rerun" and (outcomes.get("first") != "done" or "undetermined" not in states["first"].values()):
            break
        plan_entry = opportunities_plan[index] if index < len(opportunities_plan) else {}
        if plan_entry.get("infeasible"):
            attempts.extend(environment_attempts(journal_dir, folder_base, "verify", opportunity, plan_entry["infeasible"], count))
            count += 2
            outcomes[opportunity] = "infeasible"
            continue
        if plan_entry.get("retry"):
            count += 1
            absolute(f"{folder_base}/verify-{count}").mkdir(parents=True, exist_ok=True)
            attempts.append(new_attempt(journal_dir, "sweep", stage="verify", opportunity=opportunity, folder=f"{folder_base}/verify-{count}", exit=1, **{"try": 1}))
            attempts.append(new_attempt(journal_dir, "ensure"))
        count += 1
        folder = f"{folder_base}/verify-{count}"
        if fail_here("sweep"):
            return item
        desired = {key: (plan_entry.get("states") or {}).get(key, "pass") for key in keys}
        behaviors = {scene: state for key, state in desired.items() for scene in scenes_of[key]}
        sweep = make_sweep(queue_item["plan"], lock_text, "verify", sha(queue_item["overlay"]), expect["canonical_after"], behaviors, folder)
        write_json(f"{folder}/sweep.json", sweep)
        attempts.append(new_attempt(journal_dir, "sweep", stage="verify", opportunity=opportunity, folder=folder, exit=0, **{"try": 2 if plan_entry.get("retry") else 1}))
        artifacts[f"{folder}/sweep.json"] = sha(f"{folder}/sweep.json")
        if is_f0:
            if fail_here("official"):
                return item
            official = calib_judge.judge_verify_document(sweep, absolute(f"{folder}/sweep.json"), read_json(queue_item["plan"]), absolute(lock_text), sha(lock_text),
                                                         run["verify_against"], absolute(f"{folder}/judge"))
            write_json(f"{folder}/verify.json", official)
            attempts.append(new_attempt(journal_dir, "official", stage="verify", opportunity=opportunity, folder=folder, exit=calib_judge.staged_exit_code(official)))
            artifacts[f"{folder}/verify.json"] = sha(f"{folder}/verify.json")
            official_states = verify.adoption_states(rows, official["scenes"], official["verify"]["checks"])
            if official_states != desired:
                raise RuntimeError(f"fixture 결함: {run_id} {opportunity} 공식 상태 {official_states} ≠ 의도 {desired}")
            final["official_first" if opportunity == "first" else "official_rerun"] = official_states
        if fail_here("compare"):
            return item
        report, exit_code = verify.compare_run(SimpleNamespace(expect=f"{world_final(expect)}/expect.json", run=run_id, sweep=f"{folder}/sweep.json", lock=lock_text,
                                                               judge_lock=queue_item.get("judge_lock"), work_dir=f"{folder}/compare-judge", w_anchor=options["w_anchor"]))
        if report["adoptions"] != desired:
            raise RuntimeError(f"fixture 결함: {run_id} {opportunity} 비교기 상태 {report['adoptions']} ≠ 의도 {desired}")
        tamper = (options.get("tamper_compare") or {}).get((run_id, opportunity))
        if tamper:
            tamper(report)
        write_json(f"{folder}/compare.json", report)
        attempts.append(new_attempt(journal_dir, "compare", stage="verify", opportunity=opportunity, folder=folder, exit=exit_code))
        artifacts[f"{folder}/compare.json"] = sha(f"{folder}/compare.json")
        item["runs"].update(verify=folder, compare=f"{folder}/compare.json")
        if is_f0:
            item["runs"]["verify_official"] = f"{folder}/verify.json"
        states[opportunity] = desired
        outcomes[opportunity] = "done"
        if is_f0:
            self_checks[opportunity] = not tamper and options.get("lie_self_check") != (run_id, opportunity)
            if self_checks[opportunity] is False:
                break
    final["self_check"] = self_checks if is_f0 else None
    final["overlay_sha256_after"] = sha(queue_item["overlay"])
    item["attempts"] = sum(1 for attempt in attempts if attempt["step"] == "sweep")
    if is_f0 and False in self_checks.values():
        item["status"] = "self_check_mismatch"
        stop.update(event="E3_repeat" if expect["restart"] == 1 else "E3", item=run_id, reason="자기 검증 불일치")
        return item
    if outcomes.get("first") == "infeasible":
        decision = {key: "incomplete" for key in keys}
    else:
        first = states["first"]
        final["adoptions_first"] = first
        final["rerun"] = 1 if "undetermined" in first.values() else 0
        decision = {}
        for key in keys:
            if first[key] in ("pass", "fail"):
                decision[key] = first[key]
            elif outcomes.get("rerun") == "done":
                decision[key] = states["rerun"][key]
            else:
                decision[key] = "incomplete"
    final["adoptions"] = decision
    item["status"] = "incomplete" if all(value == "incomplete" for value in decision.values()) else "verified"
    return item


def ab_sweep(queue_item, ab, lock_text, stage, expect, folder):
    plan = read_json(queue_item["plan"])
    params = copy.deepcopy(plan["params"])
    params[0]["base_value"] = ab["adopted_value"]
    params[0]["values"] = [ab["before_value"]]
    return {"tool": "calib_sweep", "plan": queue_item["plan"], "plan_sha256": sha(queue_item["plan"]), "stage": stage, "lock": lock_text, "lock_sha256": sha(lock_text),
            "overlay_sha256": sha(queue_item["overlay"]), "canonical_tuning_sha256": expect["canonical_after"]["tuning"], "canonical_look_sha256": expect["canonical_after"]["look"],
            "started_at": now_text(), "invalid_count": 0, "run_dir": folder, "params": params}


def ab_result(sweep, stage, scene_names, control_ok, extra):
    document = {"tool": "calib_judge", "version": 2, "stage": stage, "plan": sweep["plan"], "plan_sha256": sweep["plan_sha256"], "overlay_sha256": sweep["overlay_sha256"],
                "canonical_tuning_sha256": sweep["canonical_tuning_sha256"], "canonical_look_sha256": sweep["canonical_look_sha256"], "lock_sha256": sweep["lock_sha256"],
                "scenes": [{"name": name, "control_ok": control_ok, "targets": [], "control_reference": {}, "variants": {}} for name in scene_names]}
    document.update(extra)
    return document


def simulate_ab(world, expect, queue, queue_item, journal_dir, decision_states, statuses, ab_plan, options, stop):
    ab = next(entry for entry in expect["ab"] if entry["id"] == queue_item["ab"]["id"])
    trigger_state = "incomplete" if statuses.get(ab["for_run"]) == "incomplete" else (decision_states.get(ab["for_run"]) or {}).get(ab["trigger_adoption"])
    for_state = target_state_of(expect, decision_states, statuses, ab["for_adoption"])
    attempts, artifacts = [], {}
    item = {"plan": queue_item["plan"], "block": "F2", "chain": None, "status": "pending", "runs": {}, "attempts": 0, "wall_seconds": 0.0, "estimate_seconds": 0.0, "reason": ""}
    final = {"kind": "ab", "ab_id": ab["id"], "edition": expect["edition"], "trigger_state": trigger_state, "decision": None, "reason": "", "attempts": attempts,
             "overlay_sha256_before": sha(queue_item["overlay"]), "overlay_sha256_after": sha(queue_item["overlay"]), "perf_candidates_sha256_before": None,
             "perf_candidates_sha256_after": None, "artifact_sha256": artifacts}
    item["final"] = final
    if "incomplete" in (trigger_state, for_state) or (ab["trigger"] == "on_fail" and trigger_state == "pass"):
        item["status"], final["decision"] = "ab_skipped", "ab_skipped"
        final["reason"] = "대상 미완" if "incomplete" in (trigger_state, for_state) else "촉발 아님"
        return item
    failure = options.get("fail") if (options.get("fail") or {}).get("item") == ab["id"] else None
    make_lock(prereg_lock.required_files(queue_item["plan"]) + queue_judge_files(queue), queue_item["lock"], fixture_note(options))
    attempts.append(new_attempt(journal_dir, "lock_create"))
    artifacts[queue_item["lock"]] = sha(queue_item["lock"])
    item["runs"]["lock"] = queue_item["lock"]
    folder_base = f"{journal_dir}/runs/{PurePosixPath(queue_item['plan']).stem}"
    scene_names = [scene["name"] for scene in read_json(queue_item["plan"])["scenes"]]
    plan_entry = ab_plan.get(ab["id"]) or {"screen": "keep"}

    def finish(decision):
        item["status"], final["decision"] = "ab_done", decision
        item["attempts"] = sum(1 for attempt in attempts if attempt["step"] == "sweep")
        return item

    for stage in verify.AB_STAGES:
        if plan_entry.get("infeasible") == stage:
            attempts.extend(environment_attempts(journal_dir, folder_base, stage, None, "exit1", 0))
            return finish("ab_incomplete")
        folder = f"{folder_base}/{stage}-1"
        sweep = ab_sweep(queue_item, ab, queue_item["lock"], stage, expect, folder)
        write_json(f"{folder}/sweep.json", sweep)
        attempts.append(new_attempt(journal_dir, "sweep", stage=stage, folder=folder, exit=0, **{"try": 1}))
        outcome = plan_entry.get(stage, "keep")
        if stage == "screen":
            result = ab_result(sweep, stage, scene_names, outcome != "undetermined", {"screen_sweep": f"{folder}/sweep.json", "insensitive": False, "best": "v01" if outcome == "best" else None})
            result_text = f"{folder}/screen.json"
        else:
            adopted = outcome in ("v01", "v02")
            result = ab_result(sweep, stage, scene_names, outcome != "undetermined", {"adopt": {"decision": adopted, "variant": outcome if adopted else None}})
            result_text = f"{folder}/judge.json"
        write_json(result_text, result)
        if failure and failure["step"] == "judge":
            attempts.append(new_attempt(journal_dir, "judge", stage=stage, folder=folder, exit=failure["exit"], state=failure.get("state", "done")))
            item["status"] = "failed"
            stop.update(event="E2", item=ab["id"], reason="judge")
            return item
        attempts.append(new_attempt(journal_dir, "judge", stage=stage, folder=folder, exit=0 if outcome != "undetermined" else 1))
        report_text = f"{folder}/check-ab.json"
        code, _ = run_main(["check-ab", "--expect", f"{expect['final_dir']}/expect.json", "--ab", ab["id"], "--stage", stage, "--result", result_text, "--sweep", f"{folder}/sweep.json",
                            "--lock", queue_item["lock"], "--out", report_text, "--w-anchor", options["w_anchor"]])
        if code != 0:
            raise RuntimeError(f"fixture 결함: {ab['id']} {stage} check-ab 종료 {code}")
        attempts.append(new_attempt(journal_dir, "check_ab", stage=stage, folder=folder, exit=code))
        for text in (f"{folder}/sweep.json", result_text, report_text):
            artifacts[text] = sha(text)
        item["runs"].update({stage: folder, f"check_ab_{stage}": report_text})
        if outcome == "undetermined":
            return finish("ab_undetermined")
        if stage == "screen" and outcome != "best":
            return finish("ab_keep")
    return finish("ab_v01" if plan_entry.get("confirm") == "v01" else "ab_keep")


def pending_item(queue_item, block_id, expect):
    final = {"kind": "ab", "ab_id": queue_item["ab"]["id"]} if "ab" in queue_item else {"kind": "verify", "run": queue_item["run"]}
    final.update(edition=expect["edition"], attempts=[])
    return {"plan": queue_item["plan"], "block": block_id, "chain": None, "status": "pending", "runs": {}, "attempts": 0, "wall_seconds": 0.0, "estimate_seconds": 0.0,
            "reason": "", "final": final}


def f0_gate_of(expect, f0_items):
    self_checks, sample = {}, []
    for item in f0_items:
        final = item["final"]
        self_checks[final["run"]] = final["self_check"]
        if item["status"] == "self_check_mismatch" or (item["status"] == "verified" and "incomplete" not in final["adoptions"].values()):
            sample.append(final["run"])
    required = verify.F0_REQUIRED_SELF_CHECKS if expect["edition"] == "e1" else 0
    sample.sort()
    passed = len(sample) >= required and all(value is not False for run in sample for value in self_checks[run].values())
    return {"self_checks": self_checks, "sample": sample, "required": required, "passed": passed}


def simulate_batch(world, final_dir=None, verify_plan=None, ab_plan=None, options=None, w_anchor=None):
    final_dir = final_dir or world["final"]
    verify_plan, ab_plan = verify_plan or {}, ab_plan or {}
    options = dict(options or {}, w_anchor=w_anchor or world["w_anchor"], journal_id=secrets.token_hex(16))
    expect, queue = read_json(f"{final_dir}/expect.json"), read_json(f"{final_dir}/final_queue.json")
    journal_text = queue["final"]["journal"]
    journal_dir = verify.parent_text(journal_text)
    shutil.rmtree(absolute(journal_dir), ignore_errors=True)
    shutil.rmtree(absolute(f"{final_dir}/locks"), ignore_errors=True)
    absolute(journal_dir).mkdir(parents=True, exist_ok=True)
    stop, items, decision_states, statuses, f0_items = {}, [], {}, {}, []
    gate = None
    f0_count = sum(len(block["plans"]) for block in queue["blocks"] if block["id"] == "F0")
    stop_after = options.get("start_failed_after")
    for block in queue["blocks"]:
        for queue_item in block["plans"]:
            is_started = stop_after is None or len(items) < stop_after
            if block["id"] != "F0" and f0_count and gate is None and not stop and is_started:
                gate = f0_gate_of(expect, f0_items)
                if not gate["passed"] and expect["edition"] == "e1":
                    stop.update(event="F0_shortfall", item=None, reason="표본 3건 미만")
            if stop or not is_started:
                items.append(pending_item(queue_item, block["id"], expect))
                continue
            if "ab" in queue_item:
                items.append(simulate_ab(world, expect, queue, queue_item, journal_dir, decision_states, statuses, ab_plan, options, stop))
                continue
            item = simulate_verify(world, expect, queue, queue_item, journal_dir, verify_plan.get(queue_item["run"]) or [], options, stop)
            items.append(item)
            if item["status"] in verify.VERIFY_TERMINAL:
                decision_states[queue_item["run"]] = item["final"]["adoptions"]
                statuses[queue_item["run"]] = item["status"]
            if block["id"] == "F0" and item["status"] != "failed":
                f0_items.append(item)
    starts = [{"at": now_text(), "ok": True, "reason": ""}]
    outcome = {"event": stop["event"], "item": stop.get("item"), "reason": stop.get("reason"), "at": now_text()} if stop else None
    if stop_after is not None and not stop:
        starts += [{"at": now_text(), "ok": False, "reason": "가짜 보류 실패"}, {"at": now_text(), "ok": False, "reason": "가짜 보류 실패"}]
        outcome = {"event": "start_failed", "item": None, "reason": "시작 처리 연속 2회 실패", "at": now_text()}
    journal = {"queue_sha256": sha(f"{final_dir}/final_queue.json"), "items": items, "real_dt": None, "holds": [],
               "final_batch": {"edition": expect["edition"], "phase": expect["phase"], "restart": expect["restart"], "anchor_sha256": sha(f"{final_dir}/anchor.json"),
                               "w_anchor_sha256": sha(options["w_anchor"]), "journal_id": options["journal_id"], "starts": starts, "outcome": outcome,
                               "events": [dict(outcome)] if outcome else [], "f0_gate": gate}}
    if options.get("edit_journal"):
        options["edit_journal"](journal)
    write_json(journal_text, journal)
    pin_simulated(journal, final_dir, options["w_anchor"])
    return journal


def run_verdict(world, phase="w11", final_dir=None, labels=None, extra=(), out_name=None, w_anchor=None, ns_dump=True):
    final_dir = final_dir or world["final"]
    expect = read_json(f"{final_dir}/expect.json")
    queue = read_json(f"{final_dir}/final_queue.json")
    out = f"{world['root']}/verdict/{out_name or phase}.json"
    argv = ["verdict", "--phase", phase, "--expect", f"{final_dir}/expect.json", "--journal", queue["final"]["journal"], "--queue", f"{final_dir}/final_queue.json",
            "--out", out, "--w-anchor", w_anchor or world["w_anchor"]]
    for label in ((expect["tests_required"].get(phase) or []) if labels is None else labels):
        argv += ["--tests", f"{world['root']}/tests/{label}-tests.json"]
    if expect.get("p22") and ns_dump:
        argv += ["--ns-dump", world["ns_dump"]]
    argv += list(extra)
    if absolute(out).is_file():
        absolute(out).unlink()
    code, output = run_main(argv)
    report = read_json(out) if absolute(out).is_file() else {}
    return code, report, output


def decisions_of(report):
    return {key: value["decision"] for key, value in (report.get("adoptions") or {}).items()}


def journal_text_of(world, final_dir=None):
    return read_json(f"{final_dir or world['final']}/final_queue.json")["final"]["journal"]


def edit_json(text, function):
    document = read_json(text)
    function(document)
    write_json(text, document)


def copy_output_paths(world, final_dir=None):
    final_dir = final_dir or world["final"]
    return [output["path"] for output in read_json(f"{final_dir}/copies.json")["outputs"]] + [f"{final_dir}/copies.json"]


def item_of(journal, key):
    return next(item for item in journal["items"] if item["final"].get("run") == key or item["final"].get("ab_id") == key)


def folder_of(world, plan_name, stage="verify", number=1, final_dir=None, edition="e1"):
    return f"{final_dir or world['final']}/batch-{edition}/runs/{plan_name}/{stage}-{number}"


def compare_argv(world, run_id, sweep_text, out_name, judge_lock=True):
    expect = read_json(f"{world['final']}/expect.json")
    run = expect["runs"][run_id]
    argv = ["compare", "--expect", f"{world['final']}/expect.json", "--run", run_id, "--sweep", sweep_text, "--lock", run["lock"],
            "--work-dir", f"{world['root']}/cmp/{out_name}", "--out", f"{world['root']}/cmp/{out_name}.json", "--w-anchor", world["w_anchor"]]
    if run["block"] == "F0" and judge_lock:
        argv += ["--judge-lock", run["judge_lock"]]
    return argv


def run_compare(world, run_id, sweep_text, out_name):
    code, output = run_main(compare_argv(world, run_id, sweep_text, out_name))
    report_text = f"{world['root']}/cmp/{out_name}.json"
    return code, read_json(report_text) if absolute(report_text).is_file() else {}, output


def variant_sweep(world, source_text, name, function):
    document = read_json(source_text)
    function(document)
    text = f"{world['root']}/variants/{name}/sweep.json"
    write_json(text, document)
    return text


def archive_e1(world, parts=("journal", "locks", "anchor")):
    final_dir = world["final"]
    archive = f"{final_dir}/{ARCHIVE_NAME}"
    absolute(f"{archive}/locks").mkdir(parents=True, exist_ok=True)
    if "journal" in parts:
        shutil.move(str(absolute(f"{final_dir}/batch-e1")), str(absolute(f"{archive}/batch-e1")))
    if "locks" in parts:
        for path in sorted(absolute(f"{final_dir}/locks").glob("*-e1-prereg-lock.json")):
            shutil.move(str(path), str(absolute(f"{archive}/locks") / path.name))
    if "anchor" in parts:
        shutil.move(str(absolute(f"{final_dir}/anchor.json")), str(absolute(f"{archive}/anchor.json")))
    return archive


def rerun_expect(world, final_dir=None, w_anchor=None):
    final_dir = final_dir or world["final"]
    return run_main(["expect", "--spec", f"{final_dir}/final-spec.json", "--out", f"{final_dir}/expect.json", "--queue-out", f"{final_dir}/final_queue.json",
                     "--w-anchor", w_anchor or world["w_anchor"]])


def crafted_verdict(world, numbers):
    expect = read_json(f"{world['final']}/expect.json")
    revert = [{"adoption": number, "entry_id": expect["adoptions"][number]["entry_id"], "kind": expect["adoptions"][number]["kind"], "rules": ["R-W8"], "labels": [], "evidence": []}
              for number in numbers]
    document = {"tool": verify.TOOL_NAME, "version": 1, "phase": "w13", "edition": "e1", "restart": 0, "stop": None, "gate": None, "problems": [],
                "inputs": {"anchor": {"path": f"{world['final']}/anchor.json", "sha256": sha(f"{world['final']}/anchor.json")}}, "revert": revert,
                "r2_targets": verify.r2_targets(expect, {item["entry_id"] for item in revert})}
    text = f"{world['root']}/verdict/crafted-{'-'.join(numbers)}.json"
    write_json(text, document)
    return text


def rollback_argv(world, verdict_text, verdict_sha=None):
    return ["rollback", "--spec", world["spec"], "--verdict", verdict_text, "--verdict-sha", verdict_sha or sha(verdict_text), "--backup-dir", f"{world['root']}/backup",
            "--w-anchor", world["w_anchor"]]


def clear_rollback(world):
    final_dir = world["final"]
    for name in ("overlay-keep.json", "write-canonical-r2.json"):
        if absolute(f"{final_dir}/{name}").is_file():
            absolute(f"{final_dir}/{name}").unlink()
    for name in ("preview-r2", "r2", "canonical_backup_r2"):
        shutil.rmtree(absolute(f"{final_dir}/{name}"), ignore_errors=True)
    tuning, look = before_documents()
    overlay = fluid_tuning_patch.read_overlay(absolute(f"{final_dir}/overlay-frozen.json"))
    write_pair(world["live"], *apply_assignments(tuning, look, overlay["assignments"]))
    PATCH_TOOL.update(calls=[], fail=None, tamper=False)


def r2_spec_argv(world, verdict_text):
    final_dir = world["final"]
    return ["r2-spec", "--spec", world["spec"], "--verdict", verdict_text, "--overlay-keep", f"{final_dir}/overlay-keep.json", "--preview-dir", f"{final_dir}/preview-r2",
            "--out-dir", f"{final_dir}/r2", "--w-anchor", world["w_anchor"]]


def prepare_r2(world, verdict_text):
    final_dir = world["final"]
    steps = [run_main(rollback_argv(world, verdict_text)), run_main(r2_spec_argv(world, verdict_text)),
             run_main(["copies", "--spec", f"{final_dir}/r2/final-spec.json", "--out-dir", f"{final_dir}/r2"]), rerun_expect(world, f"{final_dir}/r2")]
    return steps


def test_preparation_commands(world):
    final_dir = world["final"]
    spec = read_json(world["spec"])
    check("준비: 기본 fixture 의 copies·expect·anchor 가 종료 0", setup_ok(world), setup_detail(world))
    copied = read_json(f"{final_dir}/plans/pc_p20-w.json")
    original = read_json(plan_path(world, "pc_p20"))
    check("T137 계획 사본: 지운 키는 Look base 하나, 원본 = 사본 + 지운 키, id 같음",
          verify.removed_base_paths(original, copied) == [CONTRAST] and copied["id"] == original["id"] and "base" not in copied["params"][0])
    outputs = copy_output_paths(world)
    check("T137 사본·copies.json 은 LF(CR 0)", all(b"\r" not in absolute(path).read_bytes() for path in outputs))
    values = {label: read_json(f"{final_dir}/plans/{name}-wab.json")["params"][0]["values"] for label, name in
              (("Kinds", "pa"), ("Globals", "pg"), ("Look", "pf2"), ("actor_prop", "pf7"), ("Kinds split", "pc"), ("Look 정본에 없음", "pc_p20"))}
    check("T137 채택 전 값 4종(+split·정본에 없는 Look base)이 canonical-before·동결본에서 나온다",
          values == {"Kinds": [0.1], "Globals": [150.0], "Look": [0.5], "actor_prop": [4.0], "Kinds split": [0.3], "Look 정본에 없음": [1.0]}, values)
    f7, f2, cross = (read_json(f"{final_dir}/plans/{name}.json")["params"][0] for name in ("pf7-wab", "pf2-wab", "pf5-x-pf7-wab"))
    check("T137 wab base: actor_prop 은 채택값 8.0, preview 에 있는 Look 은 base 뺌, 교차 wab 는 scenes 없음",
          f7.get("base") == 8.0 and "base" not in f2 and cross.get("base") == 8.0 and "scenes" not in cross and f7.get("scenes") is None)
    before = {path: absolute(path).read_bytes() for path in outputs}
    with restored(f"{world['live']}/{fluid_tuning_patch.TUNING_FILE_NAME}"):
        write_json(f"{world['live']}/{fluid_tuning_patch.TUNING_FILE_NAME}", {"changed": True})
        code, output = run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir])
    check("T137 지금 정본을 바꾼 fixture 도 같은 출력, 두 번 같은 바이트", code == 0 and all(absolute(path).read_bytes() == data for path, data in before.items()), output)
    code, _ = run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir, "--check"])
    with restored(f"{final_dir}/plans/pa-wab.json"):
        append_byte(f"{final_dir}/plans/pa-wab.json")
        changed_code, _ = run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir, "--check"])
    check("T137 copies --check: 같으면 0, 1바이트 다르면 1", code == 0 and changed_code == 1, (code, changed_code))
    overlay_text = spec["overlay"]
    for name, mutate in (("채택 전 = 채택", lambda overlay: next(entry for entry in overlay["assignments"] if entry["json_path"] == AMBIENT).update(value=0.1)),
                         ("Look base ≠ canonical-before 값", lambda overlay: next(entry for entry in overlay["assignments"] if entry["json_path"] == PASSIVE).update(base=0.6))):
        with restored(overlay_text):
            edit_json(overlay_text, mutate)
            code, output = run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir])
        check(f"T137 {name} → copies 종료 2", code == 2, output)
    code, output = run_main(["copies", "--spec", world["spec"], "--out-dir", world["root"]])
    check("T137 --out-dir ≠ 명세 final_dir → 종료 2", code == 2, output)
    test_owner(world, spec)
    test_w_expect_fixture(world, spec)
    test_expect_gates(world, spec)
    test_anchor_list(world, spec)


def test_owner(world, spec):
    rows = {row["test"]: row for row in read_json(spec["test_owner"])["rows"]}
    expected = {"TDGame.FluidFX.Fixture.HeatOwner": [HEAT], "TDGame.FluidFX.Fixture.TwoOwners": sorted([COVERAGE, PASSIVE]), "TDGame.FluidFX.Fixture.KindlessHardness": [],
                "TDGame.FluidFX.Fixture.NoOwner": []}
    check("T142 test-owner 겹침 0·1·2개, Kinds 종류 이름이 없으면 겹침 아님", {name: rows[name]["owners"] for name in expected} == expected,
          {name: rows.get(name, {}).get("owners") for name in expected})
    header = rows.get(verify.HEADER_SYNC_TEST) or {}
    check("T142 header_sync 특례(owners 빈 목록)·분류(timing·numeric_pin_when_failures_match·design)",
          header.get("special") == "header_sync" and header.get("owners") == [] and rows["TDGame.FluidFX.Fixture.Timing"]["class"] == "timing_record_only"
          and rows["TDGame.FluidFXRhi.Fixture.PinMatch"]["class"] == "numeric_pin_when_failures_match" and rows["TDGame.FluidFX.Fixture.NoOwner"]["class"] == "design")
    again = verify.json_text(verify.build_test_owner(spec["overlay"], world["test_classes"], f"{world['root']}/code"))
    check("T142 test-owner 두 번 같은 바이트", again.encode("utf-8") == absolute(spec["test_owner"]).read_bytes())


def w_expect_variant(world, spec, name, removed_paths):
    overlay = read_json(spec["overlay"])
    overlay["assignments"] = [entry for entry in overlay["assignments"] if entry["json_path"] not in removed_paths]
    folder = f"{world['root']}/w-expect-{name}"
    write_json(f"{folder}/overlay.json", overlay)
    tuning, look = before_documents()
    write_pair(f"{folder}/preview", *apply_assignments(tuning, look, overlay["assignments"]))
    return verify.build_w_expect(f"{folder}/overlay.json", spec["canonical_before_dir"], f"{folder}/preview", f"{world['final']}/builder-before/preset_builder_dump.json")


def test_w_expect_fixture(world, spec):
    document = read_json(spec["w_expect"])
    check("T147 w-expect(fixture): ns_expect·builder_diff·effective_diff·look_leaf_diff",
          document["ns_expect"] == {NS_FIXTURE: {"Floats": {"User.Speed": 750.0, "User.Buoyancy": -300.0}}}
          and [entry["json_path"] for entry in document["builder_diff"]] == [AMBIENT, HEAT]
          and [entry["json_path"] for entry in document["effective_diff"]] == [WIND, AMBIENT, HEAT, HARDNESS]
          and document["look_leaf_diff"] == sorted([COVERAGE, EMISSIVE, PASSIVE, LAKE, CONTRAST, SPEED])
          and document["written"] == world["after_sha"], document)
    without_speed = w_expect_variant(world, spec, "no10", [SPEED])
    without_heat = w_expect_variant(world, spec, "no1", [HEAT])
    check("T147 #10 을 뺀 덧씌움 → NS Speed 기대 900, #1 을 뺀 덧씌움 → builder_diff 1개(fixture 빌더 2종)",
          without_speed["ns_expect"][NS_FIXTURE]["Floats"]["User.Speed"] == 900.0 and len(without_heat["builder_diff"]) == 1)


def expect_gate(world, spec_change=None, overlay_change=None, recopy=False, before_sha=None, extra_restore=(), file_change=None):
    final_dir = world["final"]
    spec = read_json(world["spec"])
    paths = [world["spec"], spec["overlay"], *copy_output_paths(world), *extra_restore]
    with restored(*paths):
        if spec_change:
            edit_json(world["spec"], spec_change)
        if overlay_change:
            edit_json(spec["overlay"], overlay_change)
        if file_change:
            file_change()
        if recopy or spec_change:
            run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir])
        with patched(verify, CANONICAL_BEFORE_SHA256=before_sha or verify.CANONICAL_BEFORE_SHA256):
            return gate_of(verify.build_expect, world["spec"], world["w_anchor"])


def entry_by_path(overlay, json_path):
    return next(entry for entry in overlay["assignments"] if entry["json_path"] == json_path)


def test_expect_gates(world, spec):
    final_dir = world["final"]
    first = sha(f"{final_dir}/expect.json"), sha(f"{final_dir}/final_queue.json")
    code, output = rerun_expect(world)
    check("T138 expect 두 번 같은 sha(expect·큐)", code == 0 and (sha(f"{final_dir}/expect.json"), sha(f"{final_dir}/final_queue.json")) == first, output)
    expect = read_json(f"{final_dir}/expect.json")
    queue = read_json(f"{final_dir}/final_queue.json")
    check("T138 큐: F0 → F1 → F2, F2 는 명세 ab 순, judge_files 6, restart 0",
          [block["id"] for block in queue["blocks"]] == ["F0", "F1", "F2"] and [item["ab"]["id"] for item in queue["blocks"][2]["plans"]] == [ab["id"] for ab in spec["ab"]]
          and len(queue["final"]["judge_files"]) == 6 and queue["final"]["restart"] == 0 and expect["restart"] == 0)
    f5_rows = [row for row in expect["rows"] if row["adoption"] == "f5"]
    screen = read_json(world["sources"]["f5"]["judge"])["scenes"][0]
    check("T132 screen 출처(영향 f5): expected = control_reference, noise·drift_floor = screen 장면 noise",
          f5_rows and all(row["source"]["field"] == "control_reference" and row["expected"] == screen["control_reference"][row["metric"]]
                          and row["noise"] == screen["noise"][row["metric"]]["noise"] and row["drift_floor"] == screen["noise"][row["metric"]]["drift_floor"] for row in f5_rows))
    pb_rows = {(row["scene"], row["adoption"], row["source"]["judge"]) for row in expect["rows"] if row["run"] == "pb"}
    check("T131 출처 둘(pb): 장면별 기대·허용이 각 출처 judge 에서 나온다",
          pb_rows == {("b_cap", "11", world["sources"]["11"]["judge"]), ("b_shield", "3", world["sources"]["3"]["judge"])}, pb_rows)
    cases = [
        ("덮이지 않은 채택", dict(spec_change=lambda document: document["runs"].__setitem__(slice(None), [run for run in document["runs"] if run["id"] != "pa"])), "rows·covered"),
        ("from.judge_sha256 다름", dict(overlay_change=lambda overlay: entry_by_path(overlay, AMBIENT)["from"].update(judge_sha256="0" * 64), recopy=True), "from.judge_sha256"),
        ("fx9 경로", dict(overlay_change=lambda overlay: overlay["assignments"].append({"json_path": verify.FX9_REMOVED_PATH, "value": 80.0, "base": 120.0})), "fx9"),
        ("p22_pre 거짓", dict(file_change=lambda: edit_json(f"{final_dir}/preview/{fluid_tuning_patch.LOOK_FILE_NAME}",
                                                             lambda look: look["NiagaraSystems"][NS_FIXTURE]["Floats"].update({"User.Buoyancy": -250.0})),
                            recopy=True, extra_restore=[f"{final_dir}/preview/{fluid_tuning_patch.LOOK_FILE_NAME}"]), "p22_pre"),
        ("손잡이 장면 빠짐", dict(spec_change=lambda document: next(run for run in document["runs"] if run["id"] == "pb")["sources"][0].update(scenes=[])), "손잡이 장면"),
        ("canonical-before 다름", dict(before_sha={"tuning": "0" * 64, "look": "0" * 64}), "canonical-before"),
        ("adoptions ↔ 동결본 불일치", dict(spec_change=lambda document: document["adoptions"].pop("2")), "adoptions"),
        ("fallback 결합 불량", dict(spec_change=lambda document: document["covered"][0]["fallback"]["sources"][0].update(binding={"kind": "eol", "eol_note": f"{world['root']}/eol/eol-note.json"})),
         "CRLF"),
        ("명세에 restart 키", dict(spec_change=lambda document: document.update(restart=0)), "restart"),
    ]
    for name, arguments, keyword in cases:
        code, detail = expect_gate(world, **arguments)
        check(f"T138 {name} → expect 종료 2", code == 2 and keyword in detail, detail)
    lock_text = world["sources"]["2"]["lock"]
    measure = verify.MEASURE_TOOL_FILES[0]
    code, detail = expect_gate(world, file_change=lambda: edit_json(lock_text, lambda lock: lock["files"].update({measure: "0" * 64})), extra_restore=[lock_text])
    evidence = lambda document: next(run for run in document["runs"] if run["id"] == "pa")["sources"][0].update(lineage_evidence=["fixture-commit"])
    code_ok, detail_ok = expect_gate(world, spec_change=evidence, file_change=lambda: edit_json(lock_text, lambda lock: lock["files"].update({measure: "0" * 64})), extra_restore=[lock_text])
    check("T144 측정 도구 sha 가 다른데 lineage clean·근거 없음 → 2, 근거 있으면 통과", code == 2 and "lineage" in detail and code_ok == 0, (detail, detail_ok))


def test_anchor_list(world, spec):
    final_dir = world["final"]
    anchor_text = f"{final_dir}/anchor.json"
    first = absolute(anchor_text).read_bytes()
    code, output = run_anchor(world)
    check("T160 anchor 같은 입력 두 번 같은 바이트", code == 0 and absolute(anchor_text).read_bytes() == first, output)
    files = read_json(anchor_text)["files"]
    copies = read_json(f"{final_dir}/copies.json")
    required = set(verify.RULE_DOCUMENTS) | set(prereg_lock.JUDGE_TOOL_FILES) | {verify.BATCH_PATH, verify.TOOL_PATH, world["test_classes"], world["spec"], f"{final_dir}/expect.json",
                                                                              f"{final_dir}/final_queue.json", f"{final_dir}/copies.json", spec["overlay"], spec["w_expect"], spec["test_owner"]}
    required |= set(spec["pie_files"]) | {output["path"] for output in copies["outputs"]} | {path for output in copies["outputs"] for path in output["inputs"]}
    required |= {f"{world['root']}/regions/g_wall.json", f"{world['root']}/targets/g_wall.json", plan_path(world, "pn"), plan_path(world, "pc")}
    required |= {world["sources"][number][key] for number in ("13", "15", "17", "18") for key in ("lock", "judge")}
    missing = sorted(required - set(files))
    check("T160 닻 목록: 규칙 문서 8(개정 E·F)·잠금 도구 7·calib_batch·비교기·test_classes·명세·expect·큐·copies 입출력·w_expect·test_owner·pie_files·계획 영역 틀·목표·F0 원래 잠금",
          not missing and len(verify.RULE_DOCUMENTS) == 8 and f"{verify.RULE_DOCUMENT_DIR}/runbook-v3-amend-f.md" in files, missing)
    w_anchor = read_json(world["w_anchor"])
    check("T160 w-anchor 키가 정확히 anchor·edition·sha256", set(w_anchor) == {"anchor", "edition", "sha256"} and w_anchor["sha256"] == sha(anchor_text))


def test_compare(world):
    final_dir = world["final"]
    journal = read_json(journal_text_of(world))
    expect = read_json(f"{final_dir}/expect.json")
    g13 = folder_of(world, "pg")
    verify_document, compare_document = read_json(f"{g13}/verify.json"), read_json(f"{g13}/compare.json")
    rows = verify.run_rows(expect, "g13")
    check("T130 비트 동일: F0 공식 verify.checks = 비교기 checks, 장면 결과(경로 키 제외) 같음, 자기 검증 참",
          verify.canonical_text(verify_document["verify"]["checks"]) == verify.canonical_text(compare_document["checks"])
          and verify.self_check(rows, verify_document, compare_document, g13) is True
          and all(item["final"]["self_check"]["first"] is True for item in journal["items"] if item["block"] == "F0"))
    pb = read_json(f"{folder_of(world, 'pb')}/compare.json")
    check("T131 출처 둘: 분해 통과, binding 2개 모두 참", pb.get("decomposition_ok") is True and [entry["ok"] for entry in pb["binding"]] == [True, True], pb.get("binding"))
    pb_sweep, pb_plan = read_json(f"{folder_of(world, 'pb')}/sweep.json"), read_json(plan_path(world, "pb"))
    mixed = copy.deepcopy(verify.run_rows(expect, "pb"))
    moved = next(row for row in mixed if row["adoption"] == "11" and row["metric"] == "m1")
    moved.update(scene="b_shield", expected=9.0)
    _, merged = verify.rejudge(pb_sweep, pb_plan, mixed, f"{world['root']}/cmp/mixed")
    with patched(verify, run_rows=lambda expect_document, run_id: mixed):
        code, report, _ = run_compare(world, "pb", f"{folder_of(world, 'pb')}/sweep.json", "pb-mixed")
    check("T131 한 출처 행을 다른 장면에 섞음 → 분해 거짓, compare 종료 3",
          verify.decomposition_check(pb_sweep, pb_plan, mixed, f"{world['root']}/cmp/mixed-d", merged) is False and code == 3 and report.get("gate") == "rejudge", report)
    test_allowance_rules(world, expect)
    states = verify.adoption_states
    two = [{"adoption": "a", "scene": "s1", "metric": "m"}, {"adoption": "a", "scene": "s2", "metric": "m"}]
    check("T136 상태 판례: 한 장면 실패·한 장면 대조 표류 → undetermined, 모두 대조 통과 + 거짓 하나 → fail, 값 None → undetermined",
          states(two, [{"name": "s1", "control_ok": True}, {"name": "s2", "control_ok": False}], [{"scene": "s1", "metric": "m", "passed": False}, {"scene": "s2", "metric": "m", "passed": True}])
          == {"a": "undetermined"}
          and states(two, [{"name": "s1", "control_ok": True}, {"name": "s2", "control_ok": True}], [{"scene": "s1", "metric": "m", "passed": False}, {"scene": "s2", "metric": "m", "passed": True}])
          == {"a": "fail"}
          and states(two, [{"name": "s1", "control_ok": True}, {"name": "s2", "control_ok": True}], [{"scene": "s1", "metric": "m", "passed": None}, {"scene": "s2", "metric": "m", "passed": True}])
          == {"a": "undetermined"})
    pa_sweep = f"{folder_of(world, 'pa')}/sweep.json"
    invalid = variant_sweep(world, f"{g13}/sweep.json", "invalid", lambda document: document.update(invalid_count=1))
    code, report, _ = run_compare(world, "g13", invalid, "g13-invalid")
    check("T149 invalid_count > 0 → 모든 채택 undetermined, 종료 1", code == 1 and report.get("adoptions") == {"13": "undetermined"}, report)
    gates = []
    with restored(world["sources"]["2"]["judge"]):
        append_byte(world["sources"]["2"]["judge"])
        gates.append(("출처 judge sha 1바이트", run_compare(world, "pa", pa_sweep, "pa-judge")[:2], 3, "binding"))
    with restored(world["sources"]["2"]["lock"]):
        edit_json(world["sources"]["2"]["lock"], lambda lock: lock["files"].update({f"{world['root']}/plans/extra.json": "0" * 64}))
        gates.append(("출처 잠금 files 차이가 허용 밖", run_compare(world, "pa", pa_sweep, "pa-lock")[:2], 3, "binding"))
    for name, change, keyword in (("정본 sha", lambda document: document.update(canonical_tuning_sha256="0" * 64), "sweep"),
                                  ("잠금 created_at > 스윕 started_at", lambda document: document.update(started_at="2000-01-01T00:00:00+09:00"), "lock"),
                                  ("overlay sha", lambda document: document.update(overlay_sha256="0" * 64), "sweep")):
        gates.append((name, run_compare(world, "pa", variant_sweep(world, pa_sweep, name.split()[0], change), f"pa-{len(gates)}")[:2], 2, keyword))
    for name, (code, report), expected, keyword in gates:
        check(f"T135 관문 분류: {name} → 종료 {expected}", code == expected and report.get("gate") == keyword, (code, report.get("gate"), report.get("problems")))
    test_binding_rules(world)
    judge_lock = expect["runs"]["g13"]["judge_lock"]
    lock_files = read_json(judge_lock)["files"]
    with restored(judge_lock):
        make_lock([path for path in lock_files if not path.endswith("/anchor.json")], judge_lock)
        code, report, _ = run_compare(world, "g13", f"{g13}/sweep.json", "g13-noanchor")
        verdict_code, verdict, _ = run_verdict(world, out_name="t180")
    check("T180 실행·판정 잠금 files 에 anchor.json 이 있다, anchor 를 뺀 잠금 → compare 3·verdict 2",
          f"{final_dir}/anchor.json" in lock_files and code == 3 and report.get("gate") == "lock" and verdict_code == 2, (code, report.get("gate"), verdict_code, verdict.get("gate")))


def test_allowance_rules(world, expect):
    sweep = read_json(f"{folder_of(world, 'pg')}/sweep.json")
    plan = read_json(plan_path(world, "pg"))
    rows = [dict(row, drift_floor=0.0, tolerance=0.0, allowed=0.0) for row in verify.run_rows(expect, "g13")]
    _, same = verify.rejudge(sweep, plan, rows, f"{world['root']}/cmp/zero-same")
    shifted = [dict(row, expected=row["expected"] + 1e-9) for row in rows]
    _, moved = verify.rejudge(sweep, plan, shifted, f"{world['root']}/cmp/zero-moved")
    check("T133 허용 0(drift_floor 0·tolerance 0): 같은 값 통과, 1e-12 보다 큰 차이 실패",
          all(check_entry["allowed"] == 0.0 and check_entry["passed"] is True for check_entry in same) and all(check_entry["passed"] is False for check_entry in moved))
    judge = {"scenes": [{"name": "s", "series": True, "targets": [{"metric": "m", "tolerance": 0.3}], "noise": {"m": {"noise": 0.1, "drift_floor": 0.5}}, "candidate_medians": {"m": 1.0}}]}
    write_json(f"{world['root']}/cmp/series-judge.json", judge)
    series_rows = verify.source_rows("r", "a", "clean", "confirm", judge, f"{world['root']}/cmp/series-judge.json", ["s"])
    check("T134 계열 장면: allowed = tolerance", [row["allowed"] for row in series_rows] == [0.3])


def test_binding_rules(world):
    root = world["root"]
    source12 = {"adoption": "12", "binding": {"kind": "copy", "source_plan": plan_path(world, "pc_p20")}}
    judge12 = read_json(world["sources"]["12"]["judge"])
    bad_copy = read_json(f"{world['final']}/plans/pc_p20-w.json")
    bad_copy["params"][0]["values"] = [0.75]
    write_json(f"{root}/binding/copy-bad.json", bad_copy)
    write_json(f"{root}/binding/empty-note.json", {"plans": []})
    source10 = {"adoption": "10", "binding": {"kind": "eol", "eol_note": f"{root}/binding/empty-note.json"}}
    judge3 = read_json(world["sources"]["3"]["judge"])
    source3 = {"adoption": "3", "scenes": ["b_shield"], "binding": {"kind": "cross_plan", "source_plan": plan_path(world, "pb_src")}}
    shifted = read_json(plan_path(world, "pb"))
    shifted["scenes"][1]["level"] = LEVEL("LV_Other")
    write_json(f"{root}/binding/pb-scene.json", shifted)
    regions = read_json(plan_path(world, "pb"))
    regions["scenes"][1]["regions_template"] = f"{root}/binding/other-regions.json"
    write_json(f"{root}/binding/other-regions.json", {"regions": [{"name": "other"}]})
    write_json(f"{root}/binding/pb-regions.json", regions)
    cases = (("사본 base 밖 차이", f"{root}/binding/copy-bad.json", source12, judge12), ("EOL 메모 없는 CRLF", plan_path(world, "pn_p20"), source10, read_json(world["sources"]["10"]["judge"])),
             ("cross_plan 장면 한 줄", f"{root}/binding/pb-scene.json", source3, judge3), ("영역 틀 파일 다름", f"{root}/binding/pb-regions.json", source3, judge3))
    for name, plan_text, source, judge in cases:
        check(f"T135 결합 규칙: {name} → binding 문제(compare 에서 종료 3)", bool(verify.binding_problems(plan_text, source, judge)))
    check("T135 결합 규칙: 정상 copy·eol·cross_plan 은 문제 없음",
          not verify.binding_problems(f"{world['final']}/plans/pc_p20-w.json", source12, judge12)
          and not verify.binding_problems(plan_path(world, "pn_p20"), {"adoption": "10", "binding": {"kind": "eol", "eol_note": f"{root}/eol/eol-note.json"}}, read_json(world["sources"]["10"]["judge"]))
          and not verify.binding_problems(plan_path(world, "pb"), source3, judge3))


def test_replay_and_check_ab(world):
    root = world["root"]
    g13 = folder_of(world, "pg")
    code, output = run_main(["replay", "--verify", f"{g13}/verify.json", "--self", world["sources"]["13"]["judge"], "--identity", world["sources"]["f5"]["judge"],
                             "--out-dir", f"{root}/replay/ok"])
    cases = read_json(f"{root}/replay/ok/replay.json")["cases"]
    check("T141 replay: 공식 verify.json 같음, 자기 replay(--self)·항등(--identity) 같음 → 종료 0", code == 0 and [case["equal"] for case in cases] == [True, True, True], output)
    changed = read_json(f"{g13}/verify.json")
    changed["scenes"][0]["control_score"] = 99.0
    write_json(f"{root}/replay/changed/verify.json", changed)
    code, output = run_main(["replay", "--verify", f"{root}/replay/changed/verify.json", "--out-dir", f"{root}/replay/changed-out"])
    check("T141 replay: 장면 결과 한 키 다름 → 종료 1", code == 1, output)
    expect = read_json(f"{world['final']}/expect.json")
    pawab = next(ab for ab in expect["ab"] if ab["id"] == "pawab")
    screen_folder = folder_of(world, "pa-wab", "screen")

    def check_ab(sweep_text, result_text=f"{screen_folder}/screen.json", name="ok"):
        return run_main(["check-ab", "--expect", f"{world['final']}/expect.json", "--ab", "pawab", "--stage", "screen", "--result", result_text, "--sweep", sweep_text,
                         "--lock", pawab["lock"], "--out", f"{root}/check-ab/{name}.json", "--w-anchor", world["w_anchor"]])[0]
    sweep_text = f"{screen_folder}/sweep.json"
    results = {"정상": (check_ab(sweep_text), 0)}
    results["base_value ≠ 채택값"] = (check_ab(variant_sweep(world, sweep_text, "ab-base", lambda document: document["params"][0].update(base_value=0.3)), name="base"), 2)
    results["values ≠ [채택 전 값]"] = (check_ab(variant_sweep(world, sweep_text, "ab-values", lambda document: document["params"][0].update(values=[0.15])), name="values"), 2)
    changed_result = read_json(f"{screen_folder}/screen.json")
    changed_result["overlay_sha256"] = "0" * 64
    write_json(f"{root}/variants/ab-result/screen.json", changed_result)
    results["overlay sha 다름"] = (check_ab(sweep_text, f"{root}/variants/ab-result/screen.json", "overlay"), 2)
    results["정본 sha 다름"] = (check_ab(variant_sweep(world, sweep_text, "ab-canonical", lambda document: document.update(canonical_look_sha256="0" * 64)), name="canonical"), 2)
    lock_files = read_json(pawab["lock"])["files"]
    with restored(pawab["lock"]):
        make_lock([path for path in lock_files if not path.endswith("/anchor.json")], pawab["lock"])
        results["잠금에 anchor 없음"] = (check_ab(sweep_text, name="noanchor"), 3)
    check("T148 check-ab: 정상 0, base_value·values·overlay·정본 sha 다름 2, 잠금에 anchor 없음 3",
          all(code == expected for code, expected in results.values()), {name: code for name, (code, _) in results.items()})


def gate_name(world, code_output, out_text):
    code, output = code_output
    if out_text and absolute(out_text).is_file():
        return code, read_json(out_text).get("gate")
    return code, "anchor" if "관문 anchor" in output else output.strip()[-200:]


def gate_commands(world):
    final_dir, root = world["final"], world["root"]
    expect = read_json(f"{final_dir}/expect.json")
    pawab = next(ab for ab in expect["ab"] if ab["id"] == "pawab")
    screen_folder = folder_of(world, "pa-wab", "screen")
    commands = {
        "compare": (compare_argv(world, "g13", f"{folder_of(world, 'pg')}/sweep.json", "gate-compare"), f"{root}/cmp/gate-compare.json"),
        "check-ab": (["check-ab", "--expect", f"{final_dir}/expect.json", "--ab", "pawab", "--stage", "screen", "--result", f"{screen_folder}/screen.json", "--sweep",
                      f"{screen_folder}/sweep.json", "--lock", pawab["lock"], "--out", f"{root}/check-ab/gate.json", "--w-anchor", world["w_anchor"]], f"{root}/check-ab/gate.json"),
        "rollback": (rollback_argv(world, f"{root}/verdict/w11.json", "0" * 64), None),
        "r2-spec": (r2_spec_argv(world, f"{root}/verdict/w11.json"), None),
    }
    results = {}
    for name, (argv, out_text) in commands.items():
        if out_text and absolute(out_text).is_file():
            absolute(out_text).unlink()
        results[name] = gate_name(world, run_main(argv), out_text)
    code, report, _ = run_verdict(world, out_name="gate")
    results["verdict"] = (code, report.get("gate"))
    return results


def test_gate_anchor(world):
    root, final_dir = world["root"], world["final"]
    clean = gate_commands(world)
    check("T161 준비: 닻이 맞으면 관문 명령이 닻 관문에서 멈추지 않는다", all(gate != "anchor" for _, gate in clean.values()), clean)
    mutations = (("닻 안 목표 파일 1바이트", lambda: append_byte(f"{root}/targets/g_wall.json"), [f"{root}/targets/g_wall.json"]),
                 ("닻 안 개정 B 사본(wab) 1바이트", lambda: append_byte(f"{final_dir}/plans/pa-wab.json"), [f"{final_dir}/plans/pa-wab.json"]),
                 ("w-anchor sha 다름", lambda: edit_json(world["w_anchor"], lambda document: document.update(sha256="0" * 64)), [world["w_anchor"]]),
                 ("edition 다름", lambda: edit_json(world["w_anchor"], lambda document: document.update(edition="r2")), [world["w_anchor"]]),
                 ("git 미추적·변경(가짜)", lambda: GIT.update(problems=["가짜: w-anchor 미추적"]), []))
    for name, mutate, paths in mutations:
        with restored(*paths):
            mutate()
            results = gate_commands(world)
        GIT["problems"] = []
        check(f"T161 {name} → compare·check-ab·verdict·rollback·r2-spec 모두 종료 2(닻 관문)", all(code == 2 and gate == "anchor" for code, gate in results.values()), results)


def test_verdict_clean(world):
    code, report, output = run_verdict(world, out_name="w11")
    decisions = decisions_of(report)
    check("verdict 정상 w11: 종료 0, 모든 채택·영향 keep, F0 자기 검증 참", code == 0 and set(decisions.values()) == {"keep"} and len(decisions) == 12
          and all(value["first"] is True for value in report["self_check"].values()), output)
    check("T181 P-22: 덤프 float32 같음 → 통과, p22.runs 결정 상태 기록", (report.get("p22") or {}).get("ns_dump_equal") is True
          and report["p22"]["runs"] == {"pnp20": {"10": "pass"}}, report.get("p22"))
    with restored(world["ns_dump"]):
        write_ns_dump(world, {"User.Speed": 900.0, "User.Buoyancy": -300.0})
        speed_code, speed_report, _ = run_verdict(world, out_name="p22-speed")
    missing_code, missing_report, _ = run_verdict(world, out_name="p22-missing", ns_dump=False)
    check("T181 P-22: Speed 900 → verdict 2, --ns-dump 빠짐 → 2", speed_code == 2 and speed_report.get("gate") == "p22" and missing_code == 2, (speed_report.get("gate"), missing_report.get("gate")))
    journal_text = journal_text_of(world)
    old_mtime = (datetime.now() - timedelta(days=3)).timestamp()
    write_canonical = f"{world['final']}/write-canonical.json"
    stat = absolute(write_canonical).stat()
    os.utime(absolute(write_canonical), (old_mtime, old_mtime))
    early_code, early_report, _ = run_verdict(world, out_name="t162-w3")
    future = datetime.now().astimezone() + timedelta(hours=1)
    os.utime(absolute(write_canonical), (future.timestamp() + 3600, future.timestamp() + 3600))
    with patched_git_time(future.isoformat()):
        late_code, late_report, _ = run_verdict(world, out_name="t162-sweep")
    os.utime(absolute(write_canonical), (stat.st_atime, stat.st_mtime))
    check("T162 w-anchor 커밋 시각 > W3 write-canonical mtime → verdict 2, 커밋 시각 > 첫 스윕 started_at → 2",
          early_code == 2 and early_report.get("gate") == "time" and late_code == 2 and late_report.get("gate") == "time", (early_report.get("problems"), late_report.get("problems")))
    test_verdict_recompute(world, journal_text)


@contextlib.contextmanager
def patched_git_time(text):
    original = GIT["time"]
    GIT["time"] = text
    try:
        yield
    finally:
        GIT["time"] = original


def journal_variant(world, journal_text, name, function, extra_paths=()):
    with restored(journal_text, *extra_paths):
        function(read_json(journal_text), journal_text)
        return run_verdict(world, out_name=name)


def set_journal(journal_text, edit):
    def function(journal, text):
        edit(journal)
        write_json(text, journal)
    return function


def test_verdict_recompute(world, journal_text):
    final_dir = world["final"]
    pa_compare = f"{folder_of(world, 'pa')}/compare.json"
    g13_compare = f"{folder_of(world, 'pg')}/compare.json"
    screen = f"{folder_of(world, 'pa-wab', 'screen')}/screen.json"
    judge_lock = f"{final_dir}/locks/g13-judge-e1-prereg-lock.json"

    def refresh_artifact(journal, key, path):
        item_of(journal, key)["final"]["artifact_sha256"][path] = sha(path)

    def tamper_g13(journal, text):
        edit_json(g13_compare, lambda document: document["scenes"][0]["control_allowed"].update(m1=0.5))
        refresh_artifact(journal, "g13", g13_compare)
        write_json(text, journal)

    def tamper_lock(journal, text):
        edit_json(judge_lock, lambda lock: lock["files"].update({world["test_classes"]: "0" * 64}))
        refresh_artifact(journal, "g13", judge_lock)
        write_json(text, journal)

    def tamper_screen(journal, text):
        edit_json(screen, lambda document: document.update(canonical_tuning_sha256="0" * 64))
        refresh_artifact(journal, "pawab", screen)
        write_json(text, journal)

    def extra_folder(journal, text):
        absolute(f"{final_dir}/batch-e1/runs/pa/verify-9").mkdir(parents=True, exist_ok=True)

    cases = [
        ("일지 결정 손편집", set_journal(journal_text, lambda journal: item_of(journal, "pa")["final"]["adoptions"].update({"2": "fail"})), ()),
        ("compare.json 1바이트", lambda journal, text: append_byte(pa_compare), (pa_compare,)),
        ("폴더 수 ≠ attempts", extra_folder, ()),
        ("일지 self_check 참·산출물 불일치", tamper_g13, (g13_compare,)),
        ("잠금 판정 파일 sha ≠ 큐", tamper_lock, (judge_lock,)),
        ("A/B screen.json 정본 sha ≠ canonical_after", tamper_screen, (screen,)),
        ("일지 f0_gate.sample 손편집(T182)", set_journal(journal_text, lambda journal: journal["final_batch"]["f0_gate"].update(sample=["f2", "g13", "uw"])), ()),
        ("일지 self_check 거짓 기록·파일은 일치(T168)", set_journal(journal_text, lambda journal: item_of(journal, "g13")["final"]["self_check"].update(first=False)), ()),
        ("일지 판 ≠ 큐(T173)", set_journal(journal_text, lambda journal: journal["final_batch"].update(edition="r2")), ()),
        ("항목 하나의 final.edition 다름(T173)", set_journal(journal_text, lambda journal: item_of(journal, "pa")["final"].update(edition="r2")), ()),
    ]
    for name, function, paths in cases:
        code, report, output = journal_variant(world, journal_text, name, function, paths)
        shutil.rmtree(absolute(f"{final_dir}/batch-e1/runs/pa/verify-9"), ignore_errors=True)
        check(f"T143 verdict 다시 계산: {name} → 종료 2", code == 2 and report.get("gate") not in (None, "anchor"), (code, report.get("gate"), report.get("problems")))
    with restored(f"{final_dir}/plans/pa-wab.json"):
        append_byte(f"{final_dir}/plans/pa-wab.json")
        code, report, _ = run_verdict(world, out_name="copies-fail")
    check("T143 copies 실패(사본 1바이트) → 종료 2(닻 관문이 먼저 잡음)", code == 2, report.get("gate"))
    code, report, _ = run_verdict(world, phase="r2", labels=["w10-rhi-r2"], out_name="t173-phase")
    check("T173 phase r2 에 e1 일지 → 종료 2", code == 2 and report.get("gate") == "input", report.get("problems"))
    for step in (dict(step="sweep", exit=2), dict(step="official", exit=2), dict(step="compare", exit=3), dict(step="check_ab", exit=2), dict(step="lock_create", exit=1),
                 dict(step="lock_verify", exit=2), dict(step="judge", exit=None, state="timeout")):
        attempt = dict({"stage": None, "opportunity": None, "try": None, "folder": None, "log": "", "started_at": now_text(), "state": "done"}, **step)
        code, report, _ = journal_variant(world, journal_text, f"t163-{step['step']}",
                                          set_journal(journal_text, lambda journal: item_of(journal, "pawab" if step["step"] in ("check_ab", "judge") else "pa")["final"]["attempts"].append(attempt)))
        check(f"T163 verdict: attempts 에 {step['step']} 종료 {step.get('exit')}({step.get('state', 'done')}) → 일지 결정이 그럴듯해도 종료 2·stop E2",
              code == 2 and (report.get("stop") or {}).get("event") == "E2", (code, report.get("stop"), report.get("gate")))


def scenario(world, name, verify_plan=None, ab_plan=None, options=None, phase="w11", labels=None, extra=()):
    simulate_batch(world, verify_plan=verify_plan, ab_plan=ab_plan, options=options)
    return run_verdict(world, phase=phase, labels=labels, extra=extra, out_name=name)


def test_verdict_scenarios(world):
    code, report, output = scenario(world, "t174", {"pa": [{"states": {"2": "fail"}}]}, {"pawab": {"screen": "keep"}})
    entry = report.get("adoptions", {}).get("2", {})
    check("T174 fail + ab_keep(기록용) → user_decision '재현 근거 없음(유지)'·종료 1",
          code == 1 and entry.get("decision") == "user_decision" and any("재현 근거 없음(유지)" in label for label in entry.get("labels", [])), entry)
    code, report, output = scenario(world, "p20", {"pnp20": [{"states": {"10": "fail"}}]}, {"pnp20wab": {"screen": "best", "confirm": "v01"}, "pnwab": {"screen": "keep"}})
    decisions = decisions_of(report)
    check("T139 P-20: 자식 #10 은 자기 wab(v01) 로 revert, 부모 #1 은 자식 상태 + 자기 wab(keep) 로 user_decision", code == 1 and decisions.get("10") == "revert"
          and decisions.get("1") == "user_decision" and report["adoptions"]["1"]["state"] == "fail" and report["adoptions"]["1"]["ab"]["id"] == "pnwab", decisions)
    for mode in ("exit1", "interrupted"):
        code, report, output = scenario(world, f"t179-{mode}", {"pa": [{"infeasible": mode}]})
        check(f"T179 sweep {mode} 두 번 → 그 실행 채택 incomplete(E2 아님)·종료 1", code == 1 and report.get("stop") is None and decisions_of(report).get("2") == "incomplete"
              and report["adoptions"]["2"]["ab"]["decision"] == "ab_skipped", (code, report.get("stop"), decisions_of(report)))
    code, report, output = scenario(world, "t112", {"pa": [{"retry": True}]})
    check("E1 한 번 → 재시도 통과(폴더 2·sweep 시도 2)도 verdict 가 받는다", code == 0, report.get("problems"))
    code, report, output = scenario(world, "t182-3", {"uw": [{"infeasible": "exit1"}]})
    check("T182 F0 한 건 미완(표본 3) → F1 진행, 그 채택만 incomplete", code == 1 and report.get("stop") is None and decisions_of(report).get("17") == "incomplete"
          and decisions_of(report).get("2") == "keep", (report.get("stop"), decisions_of(report)))
    code, report, output = scenario(world, "t182-2", {"uw": [{"infeasible": "exit1"}], "f7": [{"states": {"18": "undetermined"}}, {"infeasible": "exit1"}]})
    journal = read_json(journal_text_of(world))
    reasons = {item["reason"] for item in report.get("incomplete", [])}
    check("T182·T150 두 건 미완(표본 2, 1회차 판정 불가 + rerun 실행 불가 포함) → F0_shortfall → verdict 전 채택 incomplete·근거 'F0 결원(개정 E1·D-W28'·종료 1",
          code == 1 and (report.get("stop") or {}).get("event") == "F0_shortfall" and journal["final_batch"]["f0_gate"]["sample"] == ["f2", "g13"]
          and set(decisions_of(report).values()) == {"incomplete"} and len(decisions_of(report)) == 12 and any("F0 결원(개정 E1·D-W28" in reason for reason in reasons),
          (report.get("stop"), journal["final_batch"]["f0_gate"], reasons))
    code, report, output = scenario(world, "t188-f2", {"f2": [{"states": {"15": "undetermined"}}, {"states": {"15": "pass"}}]})
    journal = read_json(journal_text_of(world))
    f2 = item_of(journal, "f2")
    official = read_json(f"{folder_of(world, 'pf2')}/verify.json")
    check("T188 개정 F2: 공식 장면 control_ok 거짓·checks 모두 참(verify.passed 참) → 1회차 undetermined, rerun 통과 → 결정 pass·종료 0",
          official["verify"]["passed"] is True and not official["scenes"][0]["control_ok"] and f2["final"]["official_first"] == {"15": "undetermined"}
          and code == 0 and report["adoptions"]["15"]["state_source"]["opportunity"] == "rerun", (code, report.get("problems"), f2["final"]["official_first"]))
    code, report, _ = journal_variant(world, journal_text_of(world), "t188-passed", set_journal(journal_text_of(world), lambda journal: item_of(journal, "f2")["final"].update(
        official_first={"15": "pass"})))
    check("T188 개정 F2: 일지 official_first 를 verify.passed(통과)로 적으면 verdict 2", code == 2, report.get("problems"))
    code, report, output = scenario(world, "t188-start", options={"start_failed_after": 2})
    decisions = decisions_of(report)
    check("T164·T188 첫 시작 성공·두 항목 끝난 뒤 시작 연속 2회 실패 → start_failed: 끝난 두 항목 채택은 결정, 나머지 incomplete·종료 1",
          code == 1 and (report.get("stop") or {}).get("event") == "start_failed" and decisions.get("13") == "keep" and decisions.get("15") == "keep"
          and all(decisions.get(key) == "incomplete" for key in ("17", "18", "2", "3", "f5")), (report.get("stop"), decisions))
    code, report, output = scenario(world, "t175", options={"fail": {"item": "pb", "step": "compare", "exit": 2}})
    check("T175 항목 pb 에서 E2 → 앞의 끝난 채택(F0 넷) settled, 나머지 incomplete, 종료 2",
          code == 2 and set(report.get("settled") or {}) == {"13", "15", "17", "18"} and {item["adoption"] for item in report.get("incomplete", [])} >= {"2", "3", "11", "f5"},
          (code, sorted(report.get("settled") or {}), [item["adoption"] for item in report.get("incomplete", [])]))
    with restored(world["w_anchor"]):
        edit_json(world["w_anchor"], lambda document: document.update(sha256="0" * 64))
        code, report, _ = run_verdict(world, out_name="t175-anchor")
    check("T175 닻 실패 → settled 빔·전 채택 incomplete·종료 2", code == 2 and report.get("settled") == {} and len(report.get("incomplete", [])) == 12, report.get("gate"))
    code, report, output = scenario(world, "t183", options={"tamper_compare": {("f2", "first"): lambda report: report["scenes"][0]["control_allowed"].update(m1=0.5)}})
    journal = read_json(journal_text_of(world))
    check("T183 control_allowed 만 다른 비교기 → E3 즉시 멈춤(남은 F0·F1·F2 pending), verdict stop E3 next restart·종료 1",
          code == 1 and report.get("stop") == {"event": "E3", "next": "restart"} and journal["final_batch"]["outcome"]["event"] == "E3"
          and all(item["status"] == "pending" for item in journal["items"][2:]), (code, report.get("stop"), report.get("problems")))


def test_tests_and_approval(world):
    simulate_batch(world)
    labels = ["w4-header", "w10-rhi", "w13-headless", "w13-rhi"]
    with restored(f"{world['root']}/tests/w13-rhi-tests.json"):
        write_tests(world, "w13-rhi", incomplete=True)
        code, report, _ = run_verdict(world, phase="w13", labels=labels, out_name="t169-json")
    owners = {"1", "3", "15"}
    check("T169 w13 label JSON incomplete 참 → test_owner owner 전부 incomplete·종료 1",
          code == 1 and {item["adoption"] for item in report.get("incomplete", [])} == owners, [item["adoption"] for item in report.get("incomplete", [])])
    evidence = f"{world['root']}/tests/w13-rhi-exits.txt"
    write_text(evidence, "exit 3\nexit 3\n")
    code, report, _ = run_verdict(world, phase="w13", labels=labels[:3], extra=["--tests-base-error", f"w13-rhi={evidence}"], out_name="t169-arg")
    check("T169 --tests-base-error w13-rhi=… → 같음", code == 1 and {item["adoption"] for item in report.get("incomplete", [])} == owners)
    with restored(f"{world['root']}/tests/w4-header-tests.json"):
        write_tests(world, "w4-header", incomplete=True)
        base_code, _, _ = run_verdict(world, phase="w13", labels=labels, out_name="t169-w4")
    missing_code, _, _ = run_verdict(world, phase="w13", labels=labels[:3], out_name="t169-missing")
    double_code, _, _ = run_verdict(world, phase="w13", labels=labels, extra=["--tests-base-error", f"w13-rhi={evidence}"], out_name="t169-double")
    write_tests(world, "extra-label")
    extra_code, _, _ = run_verdict(world, phase="w11", labels=["w4-header", "w10-rhi", "extra-label"], out_name="t169-extra")
    check("T169 w4 label 기반 오류 → 2, 필수 label 빠짐·겹침·목록 밖 → 2", (base_code, missing_code, double_code, extra_code) == (2, 2, 2, 2),
          (base_code, missing_code, double_code, extra_code))
    failures = [("TDGame.FluidFX.Fixture.HeatOwner", ["Error: 단언 실패"]), ("TDGame.FluidFX.Fixture.TwoOwners", ["Error: 단언 실패"]), ("TDGame.FluidFX.Fixture.Timing", ["Error: 느림"]),
                (verify.HEADER_SYNC_TEST, ["Error: 헤더 다름"]), ("TDGame.FluidFX.Fixture.Unknown", ["Error: 표에 없는 시험"]),
                ("TDGame.FluidFXRhi.Fixture.PinMatch", ["Error: 허용 문구 값 1.0"])]
    with restored(f"{world['root']}/tests/w13-headless-tests.json"):
        write_tests(world, "w13-headless", failures)
        code, report, _ = run_verdict(world, phase="w13", labels=labels, out_name="t146")
        failed = {entry["test"]: entry["outcome"] for entry in report["tests"]["w13-headless"]["failed"]}
        user_keys = {item["adoption"] for item in report.get("user_decision", [])}
        check("T146 시험: owners 1 → revert, 2 → user_decision, timing·조건부 고정(문구 일치) → 기록만, header_sync → w_defect, 표에 없는 시험 → user_decision",
              code == 1 and [item["adoption"] for item in report.get("revert", [])] == ["1"] and {"3", "15", None} <= user_keys and len(report.get("w_defect", [])) == 1
              and failed.get("TDGame.FluidFX.Fixture.Timing") == "recorded" and failed.get("TDGame.FluidFXRhi.Fixture.PinMatch") == "recorded"
              and failed.get("TDGame.FluidFX.Fixture.Unknown") == "user_decision", (failed, user_keys, report.get("revert")))
        write_tests(world, "w13-headless", [("TDGame.FluidFXRhi.Fixture.PinMatch", ["Error: 다른 오류"])])
        code, report, _ = run_verdict(world, phase="w13", labels=labels, out_name="t146-pin")
        check("T146 조건부 고정 시험의 Error 줄이 문구와 다르면 design(owners 0 → user_decision)",
              report["tests"]["w13-headless"]["failed"][0]["class"] == "design" and report["tests"]["w13-headless"]["failed"][0]["outcome"] == "user_decision")
    approved = f"{world['root']}/approved.json"
    write_text(f"{world['root']}/approved-evidence.md", "사용자 승인\n")
    write_json(approved, [{"json_path": AMBIENT, "rule": "user_approved", "evidence": f"{world['root']}/approved-evidence.md"}])
    code, report, _ = run_verdict(world, phase="w13", labels=labels, extra=["--user-approved", approved], out_name="t146-approved")
    write_json(f"{world['root']}/approved-missing.json", [{"json_path": AMBIENT, "rule": "user_approved", "evidence": f"{world['root']}/none.md"}])
    missing_code, _, _ = run_verdict(world, phase="w13", labels=labels, extra=["--user-approved", f"{world['root']}/approved-missing.json"], out_name="t146-missing")
    check("T146 --user-approved: 근거 있음 → revert(user_approved), 근거 없음 → 2", code == 1 and [item["adoption"] for item in report.get("revert", [])] == ["2"]
          and "user_approved" in report["revert"][0]["rules"] and missing_code == 2, (report.get("revert"), missing_code))


def test_decision_tables():
    rows = [("incomplete", "on_fail", "ab_skipped", "incomplete"), ("pass", "on_fail", "ab_skipped", "keep"), ("pass", "always_record", "ab_keep", "keep"),
            ("pass", "always_record", "ab_undetermined", "keep"), ("pass", "always_record", "ab_v01", "user_decision"), ("pass", "always_record", "ab_incomplete", "incomplete"),
            ("fail", "on_fail", "ab_v01", "revert"), ("undetermined", "on_fail", "ab_v01", "revert"), ("fail", "on_fail", "ab_keep", "user_decision"),
            ("undetermined", "on_fail", "ab_keep", "user_decision"), ("fail", "on_fail", "ab_undetermined", "user_decision"), ("undetermined", "on_fail", "ab_undetermined", "user_decision"),
            ("fail", "on_fail", "ab_incomplete", "incomplete"), ("undetermined", "on_fail", "ab_incomplete", "incomplete")]
    wrong = [(state, trigger, decision, verify.decide_adoption("2", state, trigger, decision)[0]) for state, trigger, decision, expected in rows
             if verify.decide_adoption("2", state, trigger, decision)[0] != expected]
    skipped = gate_of(verify.decide_adoption, "2", "fail", "on_fail", "ab_skipped")
    chain = verify.decide_adoption("4", "fail", "always_record", "ab_keep")
    keep_label = verify.decide_adoption("2", "fail", "on_fail", "ab_keep")[2]
    check("T139 6.2 결정 표 모든 행, fail·undetermined + ab_skipped → 2(A1), #4 사슬 의심, '재현 근거 없음(유지)'",
          not wrong and skipped[0] == 2 and chain[0] == "user_decision" and "사슬 의심" in chain[2] and "재현 근거 없음(유지)" in keep_label, (wrong, skipped, chain))
    band = {"metric": "m2", "role": "constraint", "low": 0.0, "high": 1.0}
    resolved = {"scenes": [{"targets": [band], "control_reference": {"m2": 1.5}, "variants": {"v01": {"medians": {"m2": 0.5}}}}]}
    score_only = {"scenes": [{"targets": [dict(band, role="score")], "control_reference": {"m2": 1.5}, "variants": {"v01": {"medians": {"m2": 0.5}}}}]}
    inside = {"scenes": [{"targets": [band], "control_reference": {"m2": 0.5}, "variants": {"v01": {"medians": {"m2": 0.4}}}}]}
    cross = {"id": "f5xf7wab", "for_adoption": "18"}
    influence = verify.decide_influence
    table = {
        "pass·ab_skipped": influence("f5", "pass", cross, {"decision": "ab_skipped"}, "f7") == [],
        "incomplete·ab_skipped": [entry[:2] for entry in influence("f5", "incomplete", cross, {"decision": "ab_skipped"}, "f7")] == [("f5", "incomplete")],
        "fail·v01 제약 해소": [entry[:2] for entry in influence("f5", "fail", cross, {"decision": "ab_v01", "confirm": resolved}, "f7")] == [("18", "revert")],
        "fail·v01 점수만": [entry[:2] for entry in influence("f5", "fail", cross, {"decision": "ab_v01", "confirm": score_only}, "f7")] == [("18", "user_decision")]
                         and "목표 충돌(f5 대 f7)" in influence("f5", "fail", cross, {"decision": "ab_v01", "confirm": score_only}, "f7")[0][3],
        "fail·v01 v00 밴드 안": [entry[:2] for entry in influence("f5", "fail", cross, {"decision": "ab_v01", "confirm": inside}, "f7")] == [("18", "user_decision")],
        "fail·ab_keep": [entry[:2] for entry in influence("f5", "fail", cross, {"decision": "ab_keep"}, "f7")] == [("f5", "user_decision")],
        "undetermined·ab_undetermined": [entry[:2] for entry in influence("f5", "undetermined", cross, {"decision": "ab_undetermined"}, "f7")] == [("f5", "user_decision")],
        "fail·ab_incomplete": [entry[:2] for entry in influence("f5", "fail", cross, {"decision": "ab_incomplete"}, "f7")] == [("f5", "incomplete")],
        "fail·교차 없음": [entry[:2] for entry in influence("f5", "fail", None, None, "f7")] == [("f5", "user_decision")],
    }
    check("T139·T176 영향 f5 표 모든 행: 제약 해소 → revert 18, 점수만 → user_decision 18 '목표 충돌', f5 incomplete → incomplete f5", all(table.values()),
          [name for name, passed in table.items() if not passed])


def test_r2_targets(world):
    expect = read_json(f"{world['final']}/expect.json")
    targets = verify.r2_targets
    cases = {
        "레벨 공유(#15 → #18·f5)": (targets(expect, {PASSIVE}), {"adoptions": ["18"], "runs": ["f5", "f7"], "influences": ["f5"]}),
        "레벨·P-20 공유(#12 → #2·#9, covered 부모는 fallback)": (targets(expect, {CONTRAST}), {"adoptions": ["2", "9"], "runs": ["pa", "pc"], "influences": []}),
        "장면·P-20 공유(#10 → #1 fallback pn)": (targets(expect, {SPEED}), {"adoptions": ["1"], "runs": ["pn"], "influences": []}),
    }
    synthetic = {"adoptions": {"a": {"entry_id": "x", "scenes": ["s1"], "levels": ["L1"], "owner": "O"}, "b": {"entry_id": "y", "scenes": ["s2"], "levels": ["L2"], "owner": "O"},
                               "c": {"entry_id": "z", "scenes": ["s3"], "levels": ["L3"], "owner": "P"}},
                 "runs": {"r1": {"plan": plan_path(world, "pa")}, "r2": {"plan": plan_path(world, "pg")}, "r3": {"plan": plan_path(world, "pu")}},
                 "rows": [{"run": "r1", "adoption": "a", "scene": "s1"}, {"run": "r2", "adoption": "b", "scene": "s2"}, {"run": "r3", "adoption": "c", "scene": "s3"}],
                 "fallback_runs": {}, "fallback_rows": [], "influences": [], "covered": []}
    cases["owner 공유"] = (targets(synthetic, {"x"}), {"adoptions": ["b"], "runs": ["r2"], "influences": []})
    wrong = {name: actual for name, (actual, expected) in cases.items() if actual != expected}
    check("T177 r2 대상: 장면·레벨·owner·P-20 공유 각각 → 대상, 되돌린 채택 자신은 빠짐, 정렬·결정론", not wrong and targets(expect, {PASSIVE}) == targets(expect, {PASSIVE}), wrong)


ALL_LABELS = ["w4-header", "w10-rhi", "w13-headless", "w13-rhi"]


def rollback_negatives(world, verdict_text):
    results = {}
    clear_rollback(world)
    results["verdict sha 다름"] = run_main(rollback_argv(world, verdict_text, "0" * 64))[0]
    bad = read_json(verdict_text)
    bad["revert"][0]["entry_id"] = "Look:Materials./Game/Fixture/MI_TD_None.Scalars.Missing"
    bad_text = f"{world['root']}/verdict/bad-entry.json"
    write_json(bad_text, bad)
    results["revert entry_id 가 동결본에 없음"] = run_main(rollback_argv(world, bad_text))[0]
    clear_rollback(world)
    PATCH_TOOL["tamper"] = True
    results["남긴 항목 값 변경(가짜 overlay-remove)"] = run_main(rollback_argv(world, verdict_text))[0]
    clear_rollback(world)
    PATCH_TOOL["fail"] = "materialize"
    results["fluid_tuning_patch 종료 1"] = run_main(rollback_argv(world, verdict_text))[0]
    clear_rollback(world)
    return results


def test_r2_chain(world):
    final_dir, root = world["final"], world["root"]
    code, report, _ = scenario(world, "w11", {"f2": [{"states": {"15": "fail"}}]}, {"f2wab": {"screen": "best", "confirm": "v01"}})
    check("되돌림 시나리오 w11: f2 실패 + f2wab v01 → revert 15·종료 1, w11 은 r2_targets 없음",
          code == 1 and [item["adoption"] for item in report.get("revert", [])] == ["15"] and report.get("r2_targets") is None, (code, report.get("revert"), report.get("problems")))
    code, report, _ = run_verdict(world, phase="w13", out_name="w13")
    expected_targets = {"adoptions": ["18"], "influences": ["f5"], "runs": ["f5", "f7"]}
    check("T177 verdict w13 r2_targets = r2-spec 계산(r2_targets 함수)", code == 1 and report.get("r2_targets") == expected_targets
          and report["r2_targets"] == verify.r2_targets(read_json(f"{final_dir}/expect.json"), {PASSIVE}), report.get("r2_targets"))
    verdict_text = f"{root}/verdict/w13.json"
    negatives = rollback_negatives(world, verdict_text)
    check("T171 rollback 실패 경로: verdict sha 다름·revert 가 동결본에 없음·남긴 항목 값 변경·fluid_tuning_patch 종료 1 → 각각 2",
          all(code == 2 for code in negatives.values()), negatives)
    code, output = run_main(rollback_argv(world, verdict_text))
    rollback = read_json(f"{final_dir}/r2/rollback.json") if code == 0 else {}
    r2_w_expect = read_json(f"{final_dir}/r2/w-expect.json") if code == 0 else {}
    check("T171 rollback 정상: 종료 0, 순서 restore → overlay-remove → materialize → write-canonical, A = K, followups(Globals·#18 아님)",
          code == 0 and PATCH_TOOL["calls"] == ["restore", "overlay-remove", "materialize", "write-canonical"] and rollback["expected_ids"] == rollback["actual_ids"]
          and len(rollback["actual_ids"]) == 10 and PASSIVE not in rollback["actual_ids"] and rollback["followups"]["header_revert"] is False
          and rollback["followups"]["fog_generator_value"] is None and rollback["followups"]["fog_stage_regeneration"] is False
          and rollback["followups"]["reverted_look_assets"] == [MI_PASSIVE] and pair_sha(world["live"]) == r2_w_expect.get("written"), (code, output, PATCH_TOOL["calls"]))
    code, output = run_main(rollback_argv(world, verdict_text))
    check("T171 출력 경로가 이미 있으면 rollback 2", code == 2 and "outputs" in output, output)
    code, output = run_main(r2_spec_argv(world, verdict_text))
    first = absolute(f"{final_dir}/r2/final-spec.json").read_bytes() if code == 0 else b""
    second_code, _ = run_main(r2_spec_argv(world, verdict_text))
    check("T145 r2-spec 두 번 같은 바이트", code == 0 and second_code == 0 and absolute(f"{final_dir}/r2/final-spec.json").read_bytes() == first, output)
    r2_spec = read_json(f"{final_dir}/r2/final-spec.json")
    sources = [source["adoption"] for run in r2_spec["runs"] for source in run["sources"]]
    check("T177·T145 r2 명세: 대상 실행 f7·f5, 되돌린 채택 #15 의 행·wab 빠짐, edition r2·restart 없음·tests_required r2",
          [run["id"] for run in r2_spec["runs"]] == ["f7", "f5"] and [ab["id"] for ab in r2_spec["ab"]] == ["f7wab", "f5xf7wab"] and "15" not in r2_spec["adoptions"]
          and "15" not in sources and r2_spec["edition"] == "r2" and "restart" not in r2_spec and r2_spec["tests_required"]["r2"] == ["w10-rhi-r2"], r2_spec.get("runs"))
    copies_code, copies_output = run_main(["copies", "--spec", f"{final_dir}/r2/final-spec.json", "--out-dir", f"{final_dir}/r2"])
    expect_code, expect_output = rerun_expect(world, f"{final_dir}/r2")
    check("r2 사전 등록: copies r2·expect r2 종료 0", copies_code == 0 and expect_code == 0, (copies_output, expect_output))
    r2_anchor = f"{root}/w-anchor-r2.json"
    anchor_r2 = lambda: run_anchor(world, f"{final_dir}/r2/final-spec.json", r2_anchor)
    with restored(world["fog_generator"]):
        write_text(world["fog_generator"], fog_generator_text(4.0))
        kept_code, kept_output = anchor_r2()
    check("T172 #18 남김인데 생성기 4.0 → anchor r2 종료 2(W12 검사, 측정 전 관문)", kept_code == 2 and "w12" in kept_output, kept_output)
    with restored(f"{root}/targets/g_wall.json"):
        append_byte(f"{root}/targets/g_wall.json")
        changed_code, changed_output = anchor_r2()
    with restored(world["w_anchor"]):
        edit_json(world["w_anchor"], lambda document: document.update(sha256="0" * 64))
        previous_code, previous_output = anchor_r2()
    r2_paths = [f"{final_dir}/r2/final-spec.json", f"{final_dir}/r2/expect.json", f"{final_dir}/r2/final_queue.json", *copy_output_paths(world, f"{final_dir}/r2")]
    with restored(*r2_paths):
        write_json(f"{root}/pie/new-target.json", {"new": True})
        edit_json(f"{final_dir}/r2/final-spec.json", lambda document: document["pie_files"].append(f"{root}/pie/new-target.json"))
        run_main(["copies", "--spec", f"{final_dir}/r2/final-spec.json", "--out-dir", f"{final_dir}/r2"])
        rerun_expect(world, f"{final_dir}/r2")
        outside_code, outside_output = anchor_r2()
    check("T170·T186 r1 닻 항목 1바이트 → 2, 앞 닻(w-anchor) sha 다름 → 2, 허용 목록 밖 새 경로 → 2",
          (changed_code, previous_code, outside_code) == (2, 2, 2) and "superset" in outside_output, (changed_output[-300:], previous_output[-300:], outside_output[-300:]))
    code, output = anchor_r2()
    r1_files = read_json(f"{final_dir}/anchor.json")["files"]
    r2_document = read_json(f"{final_dir}/r2/anchor.json") if code == 0 else {"files": {}}
    new_paths = sorted(set(r2_document["files"]) - set(r1_files))
    allowed = {f"{final_dir}/overlay-keep.json", f"{final_dir}/r2/w-expect.json", f"{final_dir}/r2/rollback.json", f"{final_dir}/write-canonical-r2.json",
               f"{final_dir}/r2/final-spec.json", f"{final_dir}/r2/copies.json", f"{final_dir}/r2/expect.json", f"{final_dir}/r2/final_queue.json"}
    allowed |= {output_entry["path"] for output_entry in read_json(f"{final_dir}/r2/copies.json")["outputs"]}
    check("T186 anchor r2 정상: 종료 0, r1 닻 항목 전부 같은 sha 로 담음, 새 경로는 W12 출력·r2 명세·copies·expect·큐뿐, previous_anchor·w12_check 기록",
          code == 0 and all(r2_document["files"].get(path) == value for path, value in r1_files.items())
          and all(path in allowed or path.startswith(f"{final_dir}/preview-r2/") for path in new_paths) and r2_document.get("previous_anchor", {}).get("sha256") == sha(f"{final_dir}/anchor.json")
          and all((r2_document.get("w12_check") or {}).values()) and read_json(r2_anchor)["edition"] == "r2", (code, output, new_paths))
    write_tests(world, "w10-rhi-r2")
    r2_final = f"{final_dir}/r2"
    r2_run = lambda name, verify_plan=None, ab_plan=None, options=None: (simulate_batch(world, r2_final, verify_plan, ab_plan, options, w_anchor=r2_anchor),
                                                                          run_verdict(world, phase="r2", final_dir=r2_final, out_name=name, w_anchor=r2_anchor))[1]
    code, report, output = r2_run("r2-clean")
    check("T186 w-anchor edition r2 → r2 일지·큐·expect 판 대조 통과, r2 verdict 종료 0", code == 0 and set(decisions_of(report)) == {"18", "f5"}, (code, report.get("problems")))
    code, report, _ = r2_run("r2-posthoc", {"f7": [{"states": {"18": "fail"}}]}, {"f7wab": {"screen": "best", "confirm": "v01"}})
    check("T178 r2 verdict 되돌림 → post_hoc, revert 빔·종료 1", code == 1 and [item["adoption"] for item in report.get("post_hoc", [])] == ["18"] and report.get("revert") == [],
          (report.get("post_hoc"), report.get("revert")))
    code, report, _ = r2_run("r2-e3", options={"tamper_compare": {("f7", "first"): lambda document: document["scenes"][0]["control_allowed"].update(m1=0.5)}})
    check("T178 r2 E3 → r2 대상 incomplete·종료 1", code == 1 and (report.get("stop") or {}).get("event") == "E3"
          and sorted(item["adoption"] for item in report.get("incomplete", [])) == ["18", "f5"], (report.get("stop"), report.get("incomplete")))
    code, report, _ = r2_run("r2-f0-incomplete", {"f7": [{"infeasible": "exit1"}]})
    check("T178 r2 F0 미완은 결원 규칙 없이 계속(f5 결정, #18 만 incomplete)", code == 1 and report.get("stop") is None and decisions_of(report) == {"18": "incomplete", "f5": "keep"},
          (report.get("stop"), decisions_of(report)))


def test_fog_chain(world):
    final_dir, root = world["final"], world["root"]
    code, report, _ = scenario(world, "w13", {"f7": [{"states": {"18": "fail"}}]}, {"f7wab": {"screen": "best", "confirm": "v01"}}, phase="w13")
    check("#18 되돌림 시나리오 w13: revert 18, r2_targets #15·f5", code == 1 and [item["adoption"] for item in report.get("revert", [])] == ["18"]
          and report.get("r2_targets") == {"adoptions": ["15"], "influences": ["f5"], "runs": ["f2", "f5"]}, (report.get("revert"), report.get("r2_targets")))
    verdict_text = f"{root}/verdict/w13.json"
    clear_rollback(world)
    steps = prepare_r2(world, verdict_text)
    rollback = read_json(f"{final_dir}/r2/rollback.json") if steps[0][0] == 0 else {}
    check("T171·T187 #18 만 되돌림(Look 없음): overlay-remove 호출 없이 사본에서 빠짐, followups fog_generator_value 4.0·fog_stage_regeneration 참",
          all(code == 0 for code, _ in steps) and PATCH_TOOL["calls"] == ["restore", "materialize", "write-canonical"] and LIGHT_ID not in rollback.get("actual_ids", [LIGHT_ID])
          and rollback["followups"]["fog_generator_value"] == 4.0 and rollback["followups"]["fog_stage_regeneration"] is True and rollback["followups"]["reverted_look_assets"] == [],
          ([output[-300:] for code, output in steps if code], PATCH_TOOL["calls"], rollback.get("followups")))
    r2_anchor = f"{root}/w-anchor-r2.json"
    anchor_r2 = lambda: run_anchor(world, f"{final_dir}/r2/final-spec.json", r2_anchor)
    immediate, immediate_output = anchor_r2()
    written = read_json(f"{final_dir}/r2/w-expect.json")["written"]["tuning"]
    write_text(world["fog_generator"], fog_generator_text(4.0))
    write_json(world["fog_report"], {"preset_tuning_sha256": written})
    past = datetime(2026, 1, 1).timestamp()
    os.utime(absolute(world["fog_report"]), (past, past))
    stale, stale_output = anchor_r2()
    write_json(world["fog_report"], {"preset_tuning_sha256": "0" * 64})
    wrong, wrong_output = anchor_r2()
    write_json(world["fog_report"], {"preset_tuning_sha256": written})
    fixed, fixed_output = anchor_r2()
    check("T172·T187 anchor r2 W12 검사: rollback 직후(생성기 8.0) → 2, 무대 보고 mtime ≤ finished_at → 2, 보고 preset sha ≠ r2 written → 2, 상수 4.0·무대 보고 갱신 뒤 → 0",
          (immediate, stale, wrong, fixed) == (2, 2, 2, 0), (immediate_output[-200:], stale_output[-200:], wrong_output[-200:], fixed_output[-200:]))


def test_crafted_rollbacks(world):
    final_dir, root = world["final"], world["root"]
    clear_rollback(world)
    all_numbers = verify.sorted_adoptions(ADOPTIONS)
    code, output = run_main(rollback_argv(world, crafted_verdict(world, all_numbers)))
    rollback = read_json(f"{final_dir}/r2/rollback.json") if code == 0 else {}
    check("T171 모든 채택 되돌림 → write-canonical 건너뜀(skipped_empty)·정본 = before",
          code == 0 and rollback.get("write_canonical") == "skipped_empty" and "write-canonical" not in PATCH_TOOL["calls"] and pair_sha(world["live"]) == world["before_sha"], output)
    clear_rollback(world)
    steps = prepare_r2(world, crafted_verdict(world, ["12"]))
    r2_spec = read_json(f"{final_dir}/r2/final-spec.json") if steps[1][0] == 0 else {"runs": [], "ab": []}
    runs = {run["id"]: run for run in r2_spec["runs"]}
    abs_ = {ab["id"]: ab for ab in r2_spec["ab"]}
    pawab = read_json(f"{final_dir}/r2/plans/pa-wab.json")["params"][0]["values"] if steps[2][0] == 0 else None
    check("T145 #12 되돌림 → groove 계열은 원 계획(binding same, fallback pc), #9 자기 wab for_run pc·trigger 9, r2 wab #2 v01 = W 전 정본 0.1, r2 expect 통과",
          all(code == 0 for code, _ in steps) and sorted(runs) == ["pa", "pc"] and runs["pc"]["plan"] == plan_path(world, "pc")
          and runs["pc"]["sources"][0]["binding"] == {"kind": "same"} and abs_.get("pcwab9", {}).get("for_run") == "pc" and abs_["pcwab9"]["trigger_adoption"] == "9"
          and "pcwab12" not in abs_ and pawab == [0.1], ([output[-300:] for code, output in steps if code], sorted(runs), sorted(abs_)))
    clear_rollback(world)
    steps = prepare_r2(world, crafted_verdict(world, ["10"]))
    r2_spec = read_json(f"{final_dir}/r2/final-spec.json") if steps[1][0] == 0 else {"runs": [], "ab": []}
    abs_ = {ab["id"]: ab for ab in r2_spec["ab"]}
    check("T145 #10 되돌림 → #1 fallback pn 실행·pnwab for_run pn·trigger 1, P-22 실행 id 도 fallback 으로",
          all(code == 0 for code, _ in steps) and [run["id"] for run in r2_spec["runs"]] == ["pn"] and abs_.get("pnwab", {}).get("for_run") == "pn"
          and abs_["pnwab"]["trigger_adoption"] == "1" and (r2_spec.get("p22") or {}).get("runs") == ["pn"], ([output[-300:] for code, output in steps if code], r2_spec.get("p22")))
    clear_rollback(world)


def tamper_f2(report):
    report["scenes"][0]["control_allowed"]["m1"] = 0.5


def test_restart(world):
    final_dir = world["final"]
    simulate_batch(world, options={"tamper_compare": {("f2", "first"): tamper_f2}})
    code, output = run_anchor(world)
    check("T162 e1·restart 0 에서 batch-e1/journal.json 이 있으면 anchor 2", code == 2 and "after_results" in output, output)
    archive = archive_e1(world, ("journal", "anchor"))
    expect_code, expect_output = rerun_expect(world)
    leftover_code, leftover_output = run_anchor(world)
    archive_e1(world, ("locks",))
    absolute(f"{final_dir}/aborted-20261105-130000").mkdir(parents=True, exist_ok=True)
    double_code, double_output = rerun_expect(world)
    absolute(f"{final_dir}/aborted-20261105-130000").rmdir()
    rerun_expect(world)
    with restored(f"{final_dir}/batch-e1/journal.json"):
        write_json(f"{final_dir}/batch-e1/journal.json", {})
        journal_code, _ = run_anchor(world)
    shutil.rmtree(absolute(f"{final_dir}/batch-e1"), ignore_errors=True)
    shutil.move(str(absolute(f"{archive}/anchor.json")), str(absolute(f"{world['root']}/held-anchor.json")))
    missing_code, _ = run_anchor(world)
    shutil.move(str(absolute(f"{world['root']}/held-anchor.json")), str(absolute(f"{archive}/anchor.json")))
    check("T185·T167 재시작 옮기기: 옛 F0 판정 잠금이 locks/ 에 남음 → 2, 기록 폴더 2개 → expect 2, batch-e1/journal.json 남음 → 2, 기록 폴더에 anchor.json 없음 → 2",
          expect_code == 0 and (leftover_code, double_code, journal_code, missing_code) == (2, 2, 2, 2) and "restart" in leftover_output, (leftover_output[-200:], double_output[-200:]))
    code, output = run_anchor(world)
    anchor = read_json(f"{final_dir}/anchor.json") if code == 0 else {}
    check("T185 재시작 정당성 통과: anchor restart 1, previous_journal = 기록 폴더 옛 일지, previous_anchor = 옮긴 W11 anchor.json sha(개정 F1)",
          code == 0 and anchor.get("restart") == 1 and anchor.get("previous_journal") == f"{archive}/batch-e1/journal.json"
          and anchor.get("previous_anchor") == {"path": f"{archive}/anchor.json", "sha256": sha(f"{archive}/anchor.json")}, output)
    journal = simulate_batch(world)
    creates = [attempt for item in journal["items"] for attempt in item["final"]["attempts"] if attempt["step"] in ("judge_lock_create", "judge_lock_verify")]
    code, report, output = run_verdict(world, out_name="restart-ok")
    check("T185·T167 restart 1 정상: 판정 잠금 4개 judge_lock_create(verify 0회), verdict 는 새 일지만 읽고 종료 0(옛 폴더는 실행 폴더 집합에 없음)",
          [attempt["step"] for attempt in creates] == ["judge_lock_create"] * 4 and code == 0 and report.get("restart") == 1, (code, report.get("problems")))
    simulate_batch(world, options={"tamper_compare": {("f2", "first"): tamper_f2}})
    code, report, output = run_verdict(world, out_name="restart-e3")
    check("T167 restart 1 에서 다시 E3 → E3_repeat·verdict 전 채택 incomplete('D1 두 번째 E3')·종료 1",
          code == 1 and (report.get("stop") or {}).get("event") == "E3_repeat" and len(report.get("incomplete", [])) == 12
          and all(item["reason"] == "D1 두 번째 E3" for item in report["incomplete"]), (report.get("stop"), report.get("problems")))


def test_restart_without_mismatch(world):
    simulate_batch(world, options={"lie_self_check": ("f2", "first")})
    archive_e1(world)
    expect_code, _ = rerun_expect(world)
    code, output = run_anchor(world)
    check("T167 기록 폴더 옛 일지에 E3 기록은 있고 다시 계산하면 일치 → anchor restart 1 종료 2", expect_code == 0 and code == 2 and "불일치가 없다" in output, output[-300:])


def test_w_expect_real():
    folder = f"{TEST_ROOT}/w-expect-real"
    real_speed = "Look:NiagaraSystems./Game/FluidFX/Niagara/NS_TD_FluidFlamethrower.Floats.User.TDMassSpeedCmPerSec"
    real_heat = "Kinds.Flamethrower.TDFluidSimulationConfig.HeatCoolingPerSec"
    write_json(f"{folder}/builder-before.json", {"Flamethrower": {"TDFluidSimulationConfig": {"HeatCoolingPerSec": 2.5}},
                                                 "CloudCyclone": {"TDFluidSimulationConfig": {"AmbientRefillPerSec": 0.1}},
                                                 "ViscousBlood": {"TDFluidSimulationConfig": {"AdvectionSpeedScale": 0.55}}})
    absolute(f"{folder}/canonical-before").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(fluid_tuning_patch.canonical_tuning_path(), absolute(f"{folder}/canonical-before/{fluid_tuning_patch.TUNING_FILE_NAME}"))
    shutil.copyfile(fluid_tuning_patch.canonical_look_path(), absolute(f"{folder}/canonical-before/{fluid_tuning_patch.LOOK_FILE_NAME}"))
    results = {}
    for name, removed in (("full", []), ("no10", [real_speed]), ("no1", [real_heat])):
        overlay = read_json(REAL_OVERLAY)
        overlay["assignments"] = [entry for entry in overlay["assignments"] if entry["json_path"] not in [verify.FX9_REMOVED_PATH] + removed]
        write_json(f"{folder}/{name}/overlay.json", overlay)
        completed = subprocess.run([sys.executable, "-B", str(ROOT / "Tools/FluidFX/fluid_tuning_patch.py"), "materialize", "--overlay", f"{folder}/{name}/overlay.json",
                                    "--out", f"{folder}/{name}/preview"], cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if completed.returncode != 0:
            results[name] = {"error": completed.stderr[-400:]}
            continue
        results[name] = verify.build_w_expect(f"{folder}/{name}/overlay.json", f"{folder}/canonical-before", f"{folder}/{name}/preview", f"{folder}/builder-before.json")
    full = results["full"]
    floats = ((full.get("ns_expect") or {}).get("/Game/FluidFX/Niagara/NS_TD_FluidFlamethrower") or {}).get("Floats") or {}
    builder = {entry["json_path"]: (entry["before"], entry["after"]) for entry in full.get("builder_diff") or []}
    sections = [entry["json_path"].split(".")[0] for entry in full.get("effective_diff") or []]
    check("T147 w-expect(실제 덧씌움 − fx9, 읽기 전용 materialize): 3판 1.4 참고값(NS Speed 750·Buoyancy −300·Lifetime 0.5, builder_diff 3, effective_diff Kinds 4 + Globals 1, look_leaf_diff 11)",
          floats.get("User.TDMassSpeedCmPerSec") == 750 and floats.get("User.TDMassBuoyancyCmPerSec2") == -300 and floats.get("User.TDMassLifetimeSeconds") == 0.5
          and builder == {real_heat: (2.5, 4.0), "Kinds.CloudCyclone.TDFluidSimulationConfig.AmbientRefillPerSec": (0.1, 0.2),
                          "Kinds.ViscousBlood.TDFluidSimulationConfig.AdvectionSpeedScale": (0.55, 1.0)}
          and sorted(sections) == ["Globals", "Kinds", "Kinds", "Kinds", "Kinds"] and len(full.get("look_leaf_diff") or []) == 11,
          {"floats": floats, "builder": builder, "sections": sections, "look": len(full.get("look_leaf_diff") or []), "error": full.get("error")})
    no10 = (((results["no10"].get("ns_expect") or {}).get("/Game/FluidFX/Niagara/NS_TD_FluidFlamethrower") or {}).get("Floats") or {})
    check("T147 실제: #10 을 뺀 덧씌움 → NS Speed 기대 900, #1 을 뺀 덧씌움 → builder_diff 2개",
          no10.get("User.TDMassSpeedCmPerSec") == 900 and len(results["no1"].get("builder_diff") or []) == 2, (no10.get("User.TDMassSpeedCmPerSec"), results["no1"].get("builder_diff")))


def prepare_sweep(world, run_id, behaviors=None):
    final_dir = world["final"]
    expect, queue = read_json(f"{final_dir}/expect.json"), read_json(f"{final_dir}/final_queue.json")
    run = expect["runs"][run_id]
    folder = f"{final_dir}/batch-e1/runs/{PurePosixPath(run['plan']).stem}/verify-1"
    if run["block"] == "F0":
        make_lock(queue_judge_files(queue), run["judge_lock"])
    else:
        make_lock(prereg_lock.required_files(run["plan"]) + queue_judge_files(queue), run["lock"])
    write_json(f"{folder}/sweep.json", make_sweep(run["plan"], run["lock"], "verify", run["overlay_sha256"], expect["canonical_after"], behaviors or {}, folder))
    return f"{folder}/sweep.json"


def add_metric_to_source(world, spec, overlay):
    judge_text = world["sources"]["2"]["judge"]

    def change(judge):
        scene = judge["scenes"][0]
        scene["targets"].append(dict(scene["targets"][0], metric="m3"))
        scene["noise"]["m3"] = dict(scene["noise"]["m1"])
        scene["candidate_medians"]["m3"] = 3.0
    edit_json(judge_text, change)
    entry_by_path(overlay, AMBIENT)["from"]["judge_sha256"] = sha(judge_text)


def test_extra_metric_and_judge_file(world):
    check("준비: 지표를 더한 출처 judge fixture 의 copies·expect·anchor 가 종료 0", setup_ok(world), setup_detail(world))
    code, report, output = run_compare(world, "pa", prepare_sweep(world, "pa"), "pa-extra")
    check("T140 계획 targets 에 없는 지표가 출처 행에 있음(집합 검사) → compare 종료 3", code == 3 and report.get("gate") == "rejudge", (code, report.get("problems")))
    g13_sweep = prepare_sweep(world, "g13")
    append_byte(world["test_classes"], b"\n")
    expect_code, _ = rerun_expect(world)
    anchor_code, _ = run_anchor(world)
    code, report, output = run_compare(world, "g13", g13_sweep, "g13-judge-file")
    check("T135 판정 파일 1바이트(잠금 뒤 test_classes 변경, expect·anchor 다시 만듦) → compare 종료 3(잠금 관문)",
          expect_code == 0 and anchor_code == 0 and code == 3 and report.get("gate") == "lock", (expect_code, anchor_code, code, report.get("problems")))


def test_self_check_rules(world):
    expect = read_json(f"{world['final']}/expect.json")
    folder = folder_of(world, "pg")
    rows = verify.run_rows(expect, "g13")
    official, comparator = read_json(f"{folder}/verify.json"), read_json(f"{folder}/compare.json")

    def variant(change_comparator, change_official=None):
        changed_official, changed_comparator = copy.deepcopy(official), copy.deepcopy(comparator)
        change_comparator(changed_comparator)
        if change_official:
            change_official(changed_official)
        return (verify.self_check(rows, changed_official, changed_comparator, folder),
                calib_batch.is_self_check_equal(rows, changed_official, changed_comparator, folder))

    def path_keys(document):
        document["scenes"][0]["measure"] = "Saved/elsewhere/measure.json"
        document["scenes"][0]["sources"] = ["look"]
        document["scenes"][0]["undetermined_reasons"] = [f"{folder}/compare-judge/regions.json", str(absolute(f"{folder}/compare-judge/grid.json"))]

    def official_paths(document):
        document["scenes"][0]["undetermined_reasons"] = [f"{folder}/judge/regions.json", str(absolute(f"{folder}/judge/grid.json"))]

    def backslash_comparator(document):
        document["scenes"][0]["undetermined_reasons"] = [str(absolute(f"{folder}/compare-judge/grid.json")).replace("/", "\\")]

    def backslash_official(document):
        document["scenes"][0]["undetermined_reasons"] = [str(absolute(f"{folder}/judge/grid.json")).replace("/", "\\")]

    results = {
        "control_allowed 만 다름": (variant(lambda document: document["scenes"][0]["control_allowed"].update(m1=0.5)), False),
        "장면 결과의 다른 키 하나": (variant(lambda document: document["scenes"][0].update(control_score=(document["scenes"][0]["control_score"] or 0) + 1.0)), False),
        "(c) 채택 상태만 다름": (variant(lambda document: document.update(adoptions={"13": "fail"})), False),
        "경로 키(measure·sources·작업 폴더 문자열)만 다름 → 참": (variant(path_keys, official_paths), True),
        "같음 → 참": (variant(lambda document: None), True),
        "Windows 표기 작업 폴더 문자열만 다름 → 참": (variant(backslash_comparator, backslash_official), True),
    }
    check("T183 자기 검증 (a)(b)(c): control_allowed·다른 장면 키·채택 상태만 달라도 거짓, 경로 키만 다르면 참", all(pair[0] is expected for pair, expected in results.values()),
          {name: pair for name, (pair, expected) in results.items() if pair[0] is not expected})
    check("개정 H(반박 적합 S5): calib_batch 자기 검증 복사본 is_self_check_equal 이 같은 변형 여섯에서 calib_final_verify.self_check 와 같은 값",
          all(pair[0] == pair[1] for pair, _ in results.values()), {name: pair for name, (pair, _) in results.items() if pair[0] != pair[1]})


def test_rerun_mismatch(world):
    code, report, output = scenario(world, "t183-rerun", {"f2": [{"states": {"15": "undetermined"}}, {"states": {"15": "pass"}}]},
                                    options={"tamper_compare": {("f2", "rerun"): tamper_f2}})
    check("T183 rerun 기회에서만 다름 → 같은 E3 처리(verdict stop E3 next restart·종료 1)", code == 1 and report.get("stop") == {"event": "E3", "next": "restart"},
          (code, report.get("stop"), report.get("problems")))


def call_main(function, argv):
    saved = sys.argv
    sys.argv = [str(part) for part in argv]
    output = io.StringIO()
    try:
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            try:
                code = function()
            except SystemExit as error:
                code = error.code
    finally:
        sys.argv = saved
    return code, output.getvalue()


class FakeFinalTools:
    def __init__(self, world, final_dir=None):
        self.world = world
        self.final_dir = final_dir or world["final"]
        self.expect = read_json(f"{self.final_dir}/expect.json")
        self.queue = read_json(f"{self.final_dir}/final_queue.json")
        self.entries = {verify.normalized(item["plan"]): item for block in self.queue["blocks"] for item in block["plans"]}
        self.by_stem = {PurePosixPath(plan).stem: item for plan, item in self.entries.items()}
        self.lock_labels = {}
        for item in self.entries.values():
            identifier = self.entry_id(item)
            if item.get("lock"):
                self.lock_labels[verify.normalized(item["lock"])] = f"{identifier}-original" if item.get("judge_lock") else identifier
            if item.get("judge_lock"):
                self.lock_labels[verify.normalized(item["judge_lock"])] = f"{identifier}-judge"
        self.calls, self.commands, self.counts = [], [], {}
        self.sweeps, self.ab_outcomes, self.exits, self.interrupts, self.hooks, self.after_hooks, self.tamper_compare = {}, {}, {}, {}, {}, {}, {}

    @staticmethod
    def entry_id(item):
        return item.get("run") or item["ab"]["id"]

    def describe(self, command):
        script = Path(command[1]).stem
        arguments = [str(part) for part in command[2:]]
        option = lambda name: arguments[arguments.index(name) + 1] if name in arguments else None
        if script == "calib_sweep":
            return f"sweep {self.entry_id(self.entries[verify.normalized(option('--plan'))])} {option('--stage')}", arguments
        if script == "calib_judge":
            output = PurePosixPath(verify.normalized(option("--output")))
            item = self.by_stem[output.parent.parent.name]
            if output.name == "verify.json":
                return f"official {item['run']}", arguments
            return f"judge {item['ab']['id']} {'screen' if output.name == 'screen.json' else 'confirm'}", arguments
        if script == "prereg_lock":
            if arguments[0] == "lock":
                return f"lock {self.lock_labels.get(verify.normalized(option('--out')), option('--out'))}", arguments
            return f"verify-lock {self.lock_labels.get(verify.normalized(option('--lock')), option('--lock'))}", arguments
        if script == "calib_final_verify":
            if arguments[0] == "compare":
                return f"compare {option('--run')}", arguments
            return f"check-ab {option('--ab')} {option('--stage')}", arguments
        if script == "ue_editor":
            return arguments[0], arguments
        if script == "run_in_editor":
            return ("fps" if "OverrideFPS" in arguments[-1] else "reload"), arguments
        if script == "build_and_test":
            return ("acquire" if "--acquire-hold" in arguments else "release"), arguments
        return f"{script} {' '.join(arguments[:1])}", arguments

    def __call__(self, command, log_path, timeout):
        label, arguments = self.describe(command)
        self.calls.append(label)
        self.commands.append((label, arguments, timeout))
        self.counts[label] = self.counts.get(label, 0) + 1
        Path(log_path).parent.mkdir(parents=True, exist_ok=True)
        Path(log_path).write_text(label + "\n", encoding="utf-8")
        if self.interrupts.get(label) == self.counts[label]:
            raise KeyboardInterrupt(label)
        if label in self.hooks:
            self.hooks.pop(label)()
        overrides = self.exits.get(label)
        if overrides:
            return overrides.pop(0), f"{label} 가짜 종료"
        result = self.respond(label, arguments)
        if label in self.after_hooks:
            self.after_hooks.pop(label)()
        return result

    def respond(self, label, arguments):
        option = lambda name: arguments[arguments.index(name) + 1] if name in arguments else None
        words = label.split()
        if words[0] == "sweep":
            return self.sweep(words[1], option)
        if words[0] == "official":
            return call_main(calib_judge.main, ["calib_judge.py", *arguments])
        if words[0] == "judge":
            return self.ab_judge(words[1], words[2], option)
        if words[0] in ("lock", "verify-lock"):
            return call_main(prereg_lock.main, ["prereg_lock.py", *arguments])
        if words[0] == "compare":
            code, output = run_main(arguments)
            folder = PurePosixPath(verify.normalized(option("--sweep"))).parent.name
            tamper = self.tamper_compare.get((words[1], folder))
            if tamper and code in (0, 1) and absolute(verify.normalized(option("--out"))).is_file():
                edit_json(verify.normalized(option("--out")), tamper)
            return code, output
        if words[0] == "check-ab":
            return run_main(arguments)
        return 0, ""

    def sweep(self, identifier, option):
        outcomes = self.sweeps.get(identifier) or []
        outcome = outcomes.pop(0) if outcomes else {}
        run_dir = verify.normalized(option("--run-dir"))
        absolute(run_dir).mkdir(parents=True, exist_ok=True)
        code = outcome.get("exit", 0)
        if code != 0:
            return code, "가짜 sweep 실패"
        item = self.entries[verify.normalized(option("--plan"))]
        stage = option("--stage")
        if stage == "verify":
            rows = verify.run_rows(self.expect, item["run"])
            desired = {key: (outcome.get("states") or {}).get(key, "pass") for key in verify.sorted_adoptions({row["adoption"] for row in rows})}
            behaviors = {row["scene"]: desired[row["adoption"]] for row in rows}
            document = make_sweep(item["plan"], option("--lock"), "verify", sha(option("--overlay")), self.expect["canonical_after"], behaviors, run_dir)
        else:
            ab = next(entry for entry in self.expect["ab"] if entry["id"] == item["ab"]["id"])
            document = ab_sweep(item, ab, option("--lock"), stage, self.expect, run_dir)
        write_json(f"{run_dir}/sweep.json", document)
        return 0, ""

    def ab_judge(self, identifier, stage, option):
        outcome = (self.ab_outcomes.get(identifier) or {}).get(stage, "keep")
        sweep_text = verify.normalized(option("--sweep"))
        sweep_document = read_json(sweep_text)
        item = self.entries[verify.normalized(sweep_document["plan"])]
        scene_names = [scene["name"] for scene in read_json(item["plan"])["scenes"]]
        if stage == "screen":
            extra = {"screen_sweep": sweep_text, "insensitive": outcome == "insensitive", "best": "v01" if outcome == "best" else None}
        else:
            adopted = outcome in ("v01", "v02")
            extra = {"adopt": {"decision": adopted, "variant": outcome if adopted else None}}
        write_json(verify.normalized(option("--output")), ab_result(sweep_document, stage, scene_names, outcome != "undetermined", extra))
        return (1 if outcome == "undetermined" else 0), ""


@contextlib.contextmanager
def batch_scope(world, fake):
    replacements = dict(run_tool=fake, HOLD_FILE=absolute(f"{world['root']}/build_hold.json"), canonical_disk_sha=lambda: pair_sha(world["live"]),
                        FINAL_TEST_CLASSES_PATH=world["test_classes"], FOG_GENERATOR=world["fog_generator"], FOG_STAGE_REPORT=world["fog_report"],
                        PERF_CANDIDATES_PATH=absolute(f"{world['root']}/perf-candidates.json"))
    with patched(calib_batch, **replacements):
        yield


def reset_batch(final_dir, edition="e1"):
    shutil.rmtree(absolute(f"{final_dir}/batch-{edition}"), ignore_errors=True)
    shutil.rmtree(absolute(f"{final_dir}/locks"), ignore_errors=True)
    fresh_git(read_json(f"{final_dir}/final_queue.json")["final"]["w_anchor"], final_dir)


def init_and_pin(world, final_dir, queue, fake, extra=()):
    argv = ["calib_batch.py", "--queue", f"{final_dir}/final_queue.json", "--holder", "w-final", "--journal", queue["final"]["journal"], *extra, "--init-journal"]
    with batch_scope(world, fake):
        code, output = call_main(calib_batch.main, argv)
    if code != 0:
        return code, output
    pin_code, pin_output = pin_by_tool(final_dir, queue["final"]["w_anchor"])
    return pin_code, output + pin_output


def batch_case(world, configure=None, extra=(), final_dir=None, reset=True):
    final_dir = final_dir or world["final"]
    queue = read_json(f"{final_dir}/final_queue.json")
    journal_text = queue["final"]["journal"]
    if reset:
        reset_batch(final_dir, queue["final"]["edition"])
    fake = FakeFinalTools(world, final_dir)
    if configure:
        configure(fake)
    if "--resume" not in extra and not absolute(journal_text).is_file():
        code, output = init_and_pin(world, final_dir, queue, fake, extra)
        if code != 0:
            return code, read_json(journal_text) if absolute(journal_text).is_file() else None, fake, output
    argv = ["calib_batch.py", "--queue", f"{final_dir}/final_queue.json", "--holder", "w-final", "--journal", journal_text, *extra]
    with batch_scope(world, fake):
        try:
            code, output = call_main(calib_batch.main, argv)
        except KeyboardInterrupt as interrupt:
            code, output = "interrupted", str(interrupt)
    journal = read_json(journal_text) if absolute(journal_text).is_file() else None
    return code, journal, fake, output


def statuses_of(journal):
    return {item["final"].get("run") or item["final"].get("ab_id"): item["status"] for item in (journal or {}).get("items", [])}


def outcome_of(journal):
    return ((journal or {}).get("final_batch") or {}).get("outcome") or {}


def item_attempt_steps(journal, key):
    return [attempt["step"] for attempt in item_of(journal, key)["final"]["attempts"]]


def configure_with(**settings):
    def configure(fake):
        for name, value in settings.items():
            getattr(fake, name).update(copy.deepcopy(value))
    return configure


def calls_after(fake, label):
    return fake.calls[fake.calls.index(label) + 1:] if label in fake.calls else None


def lock_command_of(fake, label):
    return next((arguments for name, arguments, _ in fake.commands if name == label), [])


def test_batch_clean(world):
    final_dir, root = world["final"], world["root"]
    read_paths = []
    original_read = calib_batch.read_json

    def watched_read(path):
        read_paths.append(verify.normalized(path))
        return original_read(path)

    with patched(calib_batch, read_json=watched_read):
        code, journal, fake, output = batch_case(world, extra=("--overlay", f"{root}/watch/accepted-overlay.json", "--locks-dir", f"{root}/watch/locks"))
    statuses = statuses_of(journal)
    gate = (journal or {}).get("final_batch", {}).get("f0_gate") or {}
    check("T100·T104 최종 모드 정상: 종료 0, verify 9 verified, 기록용 A/B ab_done·on_fail A/B ab_skipped, 사건 없음",
          code == 0 and [statuses[run] for run in ("g13", "f2", "uw", "f7", "pa", "pb", "pnp20", "pcw", "f5")] == ["verified"] * 9
          and statuses["pawab"] == statuses["pbwab3"] == "ab_done" and statuses["g13wab"] == "ab_skipped" and outcome_of(journal) == {}, (code, statuses, output[-400:]))
    check("T182 F0 네 건 verified·자기 검증 참 → f0_gate.sample 4·passed, 일지 F0 self_check 참",
          gate.get("sample") == ["f2", "f7", "g13", "uw"] and gate.get("passed") is True and gate.get("required") == 3
          and all(item_of(journal, run)["final"]["self_check"] == {"first": True, "rerun": None} for run in ("g13", "f2", "uw", "f7")), gate)
    f1_lock = lock_command_of(fake, "lock pa")
    judge_lock = lock_command_of(fake, "lock g13-judge")
    queue = read_json(f"{final_dir}/final_queue.json")
    expected_files = sorted([*queue["final"]["judge_files"], f"{final_dir}/anchor.json"])
    files_of = lambda arguments: sorted(arguments[index + 1] for index, value in enumerate(arguments) if value == "--file")
    check("T102 실행 잠금: F1 은 --plan + 판정 파일 6 + anchor, F0 판정 잠금은 --plan 없이 --file 7, F0 원래 잠금은 verify",
          "--plan" in f1_lock and files_of(f1_lock) == expected_files and "--plan" not in judge_lock and files_of(judge_lock) == expected_files
          and "verify-lock g13-original" in fake.calls and item_attempt_steps(journal, "g13")[:2] == ["lock_verify", "judge_lock_create"], (f1_lock, judge_lock))
    sweep_overlays = {(name.split()[1], arguments[arguments.index("--overlay") + 1]) for name, arguments, _ in fake.commands if name.startswith("sweep")}
    expected_overlays = {(FakeFinalTools.entry_id(item), item["overlay"]) for block in queue["blocks"] for item in block["plans"]
                         if statuses.get(FakeFinalTools.entry_id(item)) not in ("ab_skipped",)}
    check("T103 스윕 명령은 항목 overlay 를 쓰고, 기본 --overlay(감시 경로)는 열지도 만들지도 않는다",
          sweep_overlays == expected_overlays and f"{root}/watch/accepted-overlay.json" not in read_paths and not absolute(f"{root}/watch").exists(),
          (sorted(sweep_overlays - expected_overlays), [path for path in read_paths if "watch" in path]))
    check("T108 최종 모드에서 블록 헤드리스·사슬 재검증·accept·overlay-remove 호출 0(fluid_tuning_patch·헤드리스 호출 없음)",
          not any(label.startswith(("fluid_tuning_patch", "UnrealEditor")) for label in fake.calls), sorted(set(fake.calls)))
    plan = read_json(plan_path(world, "pa"))
    estimate = calib_batch.estimate_stage_seconds(plan, "verify", {})
    sweep_limits = {timeout for name, _, timeout in fake.commands if name == "sweep pa verify"}
    real_names = ("cloud_g13_storm_wall fog_f2_passive w5_underwater_l18a fog_f7_lights fx5_flamethrower-p20 cloud_g11_cyclone_live cloud_g9_mushroom-p20 fog_f6_tile "
                  "viscous_fx14_blood cloud_g10_near cloud_g12_volcano cloud_h7_sandwall track_fx12c_groove-p20 fog_f1_layer fog_f5_cut_stir").split()
    real_total = round(sum(calib_batch.estimate_stage_seconds(read_json(f"Tools/FluidFX/calib/plans/{name}.json"), "verify", {}) for name in real_names), 1)
    check("T109 추정: verify 3단계 × 장면, 스윕 한도 2 × max(추정, 130), 3판 4.2 F0+F1 원 계획 15개 합 4848.0초",
          sweep_limits == {2.0 * max(estimate, 130.0)} and real_total == 4848.0, (sweep_limits, estimate, real_total))
    code, report, output = run_verdict(world, out_name="batch-clean")
    check("calib_batch 최종 모드 일지 → verdict w11 종료 0·모든 채택·영향 keep(일지 모양이 verdict 다시 계산과 맞음, 5절 ⑥)",
          code == 0 and set(decisions_of(report).values()) == {"keep"} and len(decisions_of(report)) == 12, (code, report.get("gate"), report.get("problems")))
    test_batch_inputs(world)


def test_batch_inputs(world):
    final_dir, root = world["final"], world["root"]
    queue_text = f"{final_dir}/final_queue.json"
    cases = []

    def queue_case(name, edit, rebuild_anchor=True):
        def mutate():
            edit_json(queue_text, edit)
            if rebuild_anchor:
                reset_batch(final_dir)
                anchor_code, anchor_output = run_anchor(world)
                if anchor_code != 0:
                    raise RuntimeError(f"fixture 결함: {name} 큐 편집 뒤 anchor 종료 {anchor_code}: {anchor_output[-300:]}")
        cases.append((name, mutate, [queue_text, f"{final_dir}/anchor.json", world["w_anchor"]], ("큐 항목",)))

    def first_item(block_id, run_id=None):
        return lambda queue: next(item for block in queue["blocks"] if block["id"] == block_id for item in block["plans"] if run_id is None or FakeFinalTools.entry_id(item) == run_id)

    cases.append(("판정 파일 6 중 하나(final-spec) sha 다름", lambda: append_byte(world["spec"]), [world["spec"]], ("판정 파일",)))
    cases.append(("test_classes 다름", lambda: append_byte(world["test_classes"], b"\n"), [world["test_classes"]], ("판정 파일",)))
    queue_case("항목 lock 없음", lambda queue: first_item("F1")(queue).pop("lock"))
    queue_case("항목 overlay 없음", lambda queue: first_item("F1")(queue).pop("overlay"))
    queue_case("ab.for_run 없음(큐 앞에 없는 실행)", lambda queue: first_item("F2")(queue)["ab"].update(for_run="nowhere"))
    queue_case("trigger 모름", lambda queue: first_item("F2")(queue)["ab"].update(trigger="sometimes"))
    queue_case("잠금 이름에 판 없음", lambda queue: first_item("F1")(queue).update(lock=f"{final_dir}/locks/pa-prereg-lock.json"))
    queue_case("F0 판정 잠금 이름에 판 없음", lambda queue: first_item("F0")(queue).update(judge_lock=f"{final_dir}/locks/g13-judge-prereg-lock.json"))
    queue_case("stages 모름", lambda queue: first_item("F1")(queue).update(stages=["screen"]))
    cases.append(("--journal ≠ final.journal", lambda: None, [], ("--journal", f"{final_dir}/batch-e1/other.json", "--journal")))
    cases.append(("--block 사용", lambda: None, [], ("--block", "F1", "--block")))
    cases.append(("--revisit-deferred 사용", lambda: None, [], ("--revisit-deferred", "--revisit-deferred")))
    cases.append(("T110 큐 뒤 expect 변경", lambda: append_byte(f"{final_dir}/expect.json"), [f"{final_dir}/expect.json"], ("판정 파일",)))
    results = {}
    for name, mutate, paths, settings in cases:
        keyword = settings[-1]
        extra = settings[:-1]
        with restored(*paths):
            reset_batch(final_dir)
            mutate()
            journal_text = read_json(queue_text)["final"]["journal"]
            code, journal, fake, output = batch_case(world, extra=extra, reset=False)
            results[name] = (code, fake.calls, absolute(journal_text).is_file(), keyword in output, output.strip().splitlines()[-1:] if output else [])
        GIT["problems"] = []
    reset_batch(final_dir)
    check("T101·T110 최종 모드 입력 검증: 판정 파일 sha·test_classes·lock·overlay·for_run·trigger·잠금 이름 판·stages·--journal·--block·--revisit-deferred·큐 뒤 expect 변경 → 각각 종료 2·하위 프로세스 0·일지 없음·그 검사의 사유",
          all(code == 2 and not calls and not journal_exists and has_keyword for code, calls, journal_exists, has_keyword, _ in results.values()),
          {name: value for name, value in results.items() if value[0] != 2 or value[1] or value[2] or not value[3]})
    normal_queue = f"{root}/normal-queue.json"
    write_json(normal_queue, {"test_classes": read_json(queue_text)["test_classes"], "blocks": [{"id": "K1", "plans": [{"plan": plan_path(world, "pa"), "chain": None, "stages": ["verify"]}]}]})
    fake = FakeFinalTools(world)
    with batch_scope(world, fake):
        normal_code, normal_output = call_main(calib_batch.main, ["calib_batch.py", "--queue", normal_queue, "--holder", "w-normal", "--journal", f"{root}/normal/journal.json"])
    check("T100 일반 모드(mode 없음)는 [\"verify\"] 를 여전히 종료 2 로 거부(하위 프로세스 0)", normal_code == 2 and not fake.calls and "stages" in normal_output, normal_output[-300:])
    anchor_cases = (("닻 안 목표 파일 1바이트", lambda: append_byte(f"{root}/targets/g_wall.json"), [f"{root}/targets/g_wall.json"]),
                    ("닻 안 개정 B 사본(wab) 1바이트", lambda: append_byte(f"{final_dir}/plans/pa-wab.json"), [f"{final_dir}/plans/pa-wab.json"]),
                    ("w-anchor sha 다름", lambda: edit_json(world["w_anchor"], lambda document: document.update(sha256="0" * 64)), [world["w_anchor"]]),
                    ("w-anchor edition 다름", lambda: edit_json(world["w_anchor"], lambda document: document.update(edition="r2")), [world["w_anchor"]]),
                    ("git 미추적·변경(가짜)", lambda: GIT.update(problems=["가짜: w-anchor 미추적"]), []))
    anchor_results = {}
    for name, mutate, paths in anchor_cases:
        with restored(*paths):
            mutate()
            code, journal, fake, output = batch_case(world)
        GIT["problems"] = []
        anchor_results[name] = (code, fake.calls, "닻 대조" in output)
    check("T161 calib_batch 시작 닻 대조: 목표 파일·wab 사본 1바이트·w-anchor sha·edition·git 미추적 → 각각 종료 2·하위 프로세스 0",
          all(code == 2 and not calls and is_anchor for code, calls, is_anchor in anchor_results.values()), anchor_results)
    write_canonical = f"{final_dir}/write-canonical.json"
    with patched_git_time((datetime.now().astimezone() + timedelta(hours=2)).isoformat()):
        code, journal, fake, output = batch_case(world)
    check("T162 w-anchor 커밋 시각 > W3 write-canonical.json mtime → calib_batch 종료 2·하위 프로세스 0", code == 2 and not fake.calls and "write-canonical" in output, output[-300:])
    self_copy = f"{root}/self/calib_batch.py"
    absolute(self_copy).parent.mkdir(parents=True, exist_ok=True)
    absolute(self_copy).write_bytes(calib_batch.SELF_PATH.read_bytes() + b"\n")
    with patched(calib_batch, SELF_PATH=absolute(self_copy)):
        copy_code, _, copy_fake, copy_output = batch_case(world)
    first_code, _, _, _ = batch_case(world, configure_with(interrupts={"sweep g13 verify": 1}))
    with patched(calib_batch, SELF_PATH=absolute(self_copy)):
        resume_code, _, resume_fake, resume_output = batch_case(world, extra=("--resume",), reset=False)
    check("T111 자기 sha: calib_batch 1바이트 바꾼 복사본으로 시작 → 종료 2, 시작 뒤 바꾸고 --resume → 종료 2(둘 다 하위 프로세스 0)",
          copy_code == 2 and not copy_fake.calls and first_code == "interrupted" and resume_code == 2 and not resume_fake.calls and "자기 sha" in resume_output,
          (copy_code, first_code, resume_code, resume_output[-200:]))


def test_batch_verify_flow(world):
    two_adoptions = {"pb": [{"states": {"11": "pass", "3": "undetermined"}}, {"states": {"11": "fail", "3": "pass"}}],
                     "pa": [{"states": {"2": "fail"}}],
                     "f2": [{"states": {"15": "undetermined"}}, {"states": {"15": "fail"}}],
                     "uw": [{"states": {"17": "undetermined"}}, {"states": {"17": "undetermined"}}],
                     "pnp20": [{"exit": 1}, {"exit": 1}]}
    abs_ = {"f2wab": {"screen": "best", "confirm": "v01"}, "uwwab": {"screen": "keep"}, "pawab": {"screen": "keep"}}
    code, journal, fake, output = batch_case(world, configure_with(sweeps=two_adoptions, ab_outcomes=abs_))
    pb = item_of(journal, "pb")["final"]
    statuses = statuses_of(journal)
    check("T104 두 채택(통과 + 판정 불가) → rerun 1회, rerun 이 통과 채택을 실패로 바꿔도 결정 상태는 통과, compare 실패 → fail, 덧씌움 sha 불변",
          code == 0 and pb["rerun"] == 1 and pb["adoptions_first"] == {"3": "undetermined", "11": "pass"} and pb["adoptions"] == {"3": "pass", "11": "pass"}
          and item_of(journal, "pa")["final"]["adoptions"] == {"2": "fail"} and fake.counts.get("sweep pb verify") == 2
          and all(item["final"]["overlay_sha256_before"] == item["final"]["overlay_sha256_after"] for item in journal["items"] if item["status"] != "ab_skipped"),
          (code, pb.get("adoptions"), output[-300:]))
    triggers = {key: (statuses[key], item_of(journal, key)["final"]["decision"]) for key in ("pbwab11", "f2wab", "uwwab", "pawab", "pnp20wab", "pnwab")}
    check("T106 A/B 촉발: 1회차 통과·rerun 실패 → on_fail ab_skipped, 판정 불가 → 실패·판정 불가 → 돈다(v01 → ab_v01), always_record 는 통과여도 돈다, 대상 미완 → ab_skipped",
          triggers == {"pbwab11": ("ab_skipped", "ab_skipped"), "f2wab": ("ab_done", "ab_v01"), "uwwab": ("ab_done", "ab_keep"), "pawab": ("ab_done", "ab_keep"),
                       "pnp20wab": ("ab_skipped", "ab_skipped"), "pnwab": ("ab_skipped", "ab_skipped")}
          and item_of(journal, "pnwab")["final"]["reason"] == "대상 미완" and statuses["pnp20"] == "incomplete", triggers)
    check("T106·T108 confirm v01 이어도 accept·overlay-remove 호출 0", not any("fluid_tuning_patch" in label for label in fake.calls), sorted(set(fake.calls)))
    code, report, _ = run_verdict(world, out_name="batch-flow")
    decisions = decisions_of(report)
    check("T104·T106 이 일지의 verdict 는 관문 없이 결정(15 revert·2 user_decision·17 user_decision·10 incomplete)",
          code == 1 and report.get("gate") is None and decisions.get("15") == "revert" and decisions.get("2") == "user_decision" and decisions.get("17") == "user_decision"
          and decisions.get("10") == "incomplete" and decisions.get("11") == "keep" and decisions.get("3") == "keep", (code, report.get("gate"), report.get("problems"), decisions))
    code, journal, fake, output = batch_case(world, configure_with(sweeps={"pa": [{"exit": 1}]}))
    pa = item_of(journal, "pa")
    folders = [attempt["folder"] for attempt in pa["final"]["attempts"] if attempt["step"] == "sweep"]
    verdict_code, report, _ = run_verdict(world, out_name="batch-e1-retry")
    check("T112 sweep 종료 1 한 번 → ensure 1회 + 같은 기회 재시도 통과(폴더 2·sweep 시도 2), verdict 0",
          code == 0 and pa["status"] == "verified" and len(folders) == 2 and pa["attempts"] == 2 and calls_after(fake, "sweep pa verify")[0] == "ensure"
          and verdict_code == 0, (code, folders, verdict_code, report.get("problems")))
    code, journal, fake, output = batch_case(world, configure_with(sweeps={"pa": [{"exit": 1}, {"exit": 1}]}))
    check("T112·T179 sweep 종료 1 두 번 → 그 실행 채택 incomplete(E2 아님), 배치 계속",
          code == 0 and statuses_of(journal)["pa"] == "incomplete" and item_of(journal, "pa")["final"]["adoptions"] == {"2": "incomplete"} and outcome_of(journal) == {}
          and statuses_of(journal)["pawab"] == "ab_skipped", statuses_of(journal))
    code, journal, fake, output = batch_case(world, configure_with(sweeps={"pa": [{"exit": 2}]}))
    check("T112 sweep 종료 2 → 재시도 없이 failed·종료 2·outcome E2, 다음 호출 0(보류 해제만)",
          code == 2 and statuses_of(journal)["pa"] == "failed" and outcome_of(journal).get("event") == "E2" and fake.counts.get("sweep pa verify") == 1
          and calls_after(fake, "sweep pa verify") == ["release"], (code, fake.calls[-4:]))
    code, journal, fake, output = batch_case(world, configure_with(ab_outcomes={"pawab": {"screen": "undetermined"}}))
    check("T115 A/B screen 판정 불가 → ab_undetermined, 재screen 0·confirm 0",
          code == 0 and item_of(journal, "pawab")["final"]["decision"] == "ab_undetermined" and fake.counts.get("sweep pawab screen") == 1 and "sweep pawab confirm" not in fake.calls,
          item_of(journal, "pawab")["final"].get("decision"))


def test_batch_e2_matrix(world):
    cases = (("compare pa", 2), ("compare pa", 3), ("official g13", 2), ("official g13", None), ("check-ab pawab screen", 2), ("check-ab pawab screen", 3),
             ("lock pa", 1), ("verify-lock g13-original", 2), ("verify-lock g13-judge", 2), ("judge pawab screen", 2))
    results = {}
    for label, exit_code in cases:
        configure = configure_with(exits={label: [exit_code]})
        reset_batch(world["final"])
        if label == "verify-lock g13-judge":
            batch_case(world, configure_with(interrupts={"sweep g13 verify": 1}), reset=False)
            code, journal, fake, output = batch_case(world, configure, extra=("--resume",), reset=False)
        else:
            code, journal, fake, output = batch_case(world, configure, reset=False)
        failed = [key for key, status in statuses_of(journal).items() if status == "failed"]
        verdict_code, report, _ = run_verdict(world, out_name=f"e2-{label.replace(' ', '-')}-{exit_code}")
        results[f"{label} {exit_code}"] = (code, outcome_of(journal).get("event"), failed, calls_after(fake, label), verdict_code, (report.get("stop") or {}).get("event"))
    expected = lambda value: value[0] == 2 and value[1] == "E2" and len(value[2]) == 1 and value[3] == ["release"] and value[4] == 2 and value[5] == "E2"
    check("T114·T163 판정 도구 종료 2·3·시간 초과(compare 2·3, 공식 judge 2·시간 초과, check-ab 2·3, A/B judge 2, lock_create 1, lock_verify 2·판정 잠금 verify 2) → 재시도 없이 그 항목 failed·outcome E2·종료 2·다음 호출 0, verdict 2·stop E2",
          all(expected(value) for value in results.values()), {name: value for name, value in results.items() if not expected(value)})
    root = world["root"]
    perf_text = f"{root}/perf-candidates.json"
    overlay_text = f"{world['final']}/overlay-empty.json"
    with restored(overlay_text, perf_text):
        overlay_code, overlay_journal, _, _ = batch_case(world, configure_with(after_hooks={"compare pa": lambda: append_byte(overlay_text)}))
    with restored(perf_text):
        perf_code, perf_journal, _, _ = batch_case(world, configure_with(hooks={"check-ab pawab screen": lambda: write_json(perf_text, {"candidate": True})}))
    variant_code, variant_journal, _, _ = batch_case(world, configure_with(ab_outcomes={"pawab": {"screen": "best", "confirm": "v02"}}))
    reason_of = lambda journal: outcome_of(journal).get("reason") or ""
    results = {"덧씌움 앞뒤 sha 다름": (overlay_code, outcome_of(overlay_journal).get("item"), "덧씌움 앞뒤" in reason_of(overlay_journal)),
               "perf-candidates 바뀜": (perf_code, outcome_of(perf_journal).get("item"), "perf-candidates" in reason_of(perf_journal)),
               "confirm adopt variant v02": (variant_code, outcome_of(variant_journal).get("item"), "P-24" in reason_of(variant_journal))}
    check("T166 2.1.7 failed: 덧씌움 앞뒤 sha 다름·perf-candidates 바뀜·confirm variant v02 → 각각 그 사유의 E2·종료 2",
          results == {"덧씌움 앞뒤 sha 다름": (2, "pa", True), "perf-candidates 바뀜": (2, "pawab", True), "confirm adopt variant v02": (2, "pawab", True)}, results)
    live = f"{world['live']}/{fluid_tuning_patch.TUNING_FILE_NAME}"
    with restored(live):
        append_byte(live)
        start_code, start_journal, start_fake, _ = batch_case(world)
    with restored(live):
        sweep_code, sweep_journal, sweep_fake, _ = batch_case(world, configure_with(hooks={"official g13": lambda: append_byte(live)}))
    check("T165 디스크 정본 ≠ canonical_after: 시작 때 → 재적재 1회 뒤 종료 2(E2, 스윕 0), 시도 직전 → canonical_reload 1회 뒤 그 항목 failed·종료 2",
          start_code == 2 and outcome_of(start_journal).get("event") == "E2" and start_fake.calls.count("reload") == 2 and not any(label.startswith("sweep") for label in start_fake.calls)
          and sweep_code == 2 and outcome_of(sweep_journal).get("item") == "f2" and item_attempt_steps(sweep_journal, "f2")[-1] == "canonical_reload"
          and "sweep f2 verify" not in sweep_fake.calls, (start_code, start_fake.calls, sweep_code, outcome_of(sweep_journal)))


def test_batch_start(world):
    code, journal, fake, _ = batch_case(world, configure_with(exits={"acquire": [4]}))
    first_starts = (journal or {}).get("final_batch", {}).get("starts", [])
    check("T113·T164 보류 획득 종료 4 → ensure 0회·종료 3, starts[ok false]", code == 3 and "ensure" not in fake.calls and [start["ok"] for start in first_starts] == [False],
          (code, fake.calls, first_starts))
    code, journal, fake, _ = batch_case(world, extra=("--resume",), reset=False)
    check("T164 시작 실패 뒤 --resume 성공 → 진행·종료 0, starts [false, true]",
          code == 0 and [start["ok"] for start in journal["final_batch"]["starts"]] == [False, True] and all(status in calib_batch.FINAL_TERMINAL_STATUSES for status in statuses_of(journal).values()),
          (code, journal["final_batch"]["starts"]))
    code, journal, fake, _ = batch_case(world, configure_with(exits={"ensure": [1]}))
    check("T113 ensure 실패 → 보류 해제 호출·종료 3", code == 3 and fake.calls[-1] == "release" and "fps" not in fake.calls, fake.calls)
    code, journal, fake, _ = batch_case(world, configure_with(exits={"acquire": [4]}))
    second_code, journal, second_fake, _ = batch_case(world, configure_with(exits={"acquire": [4]}), extra=("--resume",), reset=False)
    third_code, journal, third_fake, third_output = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="start-failed-twice")
    check("T164 연속 2회 시작 실패 → start_failed·종료 3, 그 뒤 --resume → 아무것도 안 하고 3, verdict → 전 채택 incomplete·종료 1",
          (code, second_code, third_code) == (3, 3, 3) and outcome_of(journal).get("event") == "start_failed" and not third_fake.calls and "start_failed" in third_output
          and verdict_code == 1 and set(decisions_of(report).values()) == {"incomplete"} and len(decisions_of(report)) == 12, (code, second_code, third_code, report.get("problems")))
    code, journal, fake, _ = batch_case(world, configure_with(interrupts={"verify-lock uw-original": 1}))
    second_code, _, _, _ = batch_case(world, configure_with(exits={"acquire": [4]}), extra=("--resume",), reset=False)
    third_code, journal, _, _ = batch_case(world, configure_with(exits={"acquire": [4]}), extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="start-failed-after-two")
    decisions = decisions_of(report)
    check("T188 첫 시작 성공·두 항목 끝난 뒤 재개 시작이 연속 2회 실패 → start_failed, verdict: 끝난 두 항목 채택 결정, 나머지 incomplete·종료 1",
          code == "interrupted" and (second_code, third_code) == (3, 3) and outcome_of(journal).get("event") == "start_failed" and verdict_code == 1
          and decisions.get("13") == "keep" and decisions.get("15") == "keep" and all(decisions.get(key) == "incomplete" for key in ("17", "18", "2", "3", "f5")),
          (code, second_code, third_code, report.get("problems"), decisions))


def test_batch_resume(world):
    code, journal, fake, _ = batch_case(world, configure_with(interrupts={"compare pa": 1}))
    running = [attempt["step"] for attempt in item_of(journal, "pa")["final"]["attempts"] if attempt["state"] == "running"]
    resume_code, journal, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    pa_steps = [(attempt["step"], attempt["state"]) for attempt in item_of(journal, "pa")["final"]["attempts"]]
    verdict_code, report, _ = run_verdict(world, out_name="resume-compare")
    check("T107 sweep 뒤 compare 전에 끊김 → --resume 은 sweep 없이 compare 만(판정 단계 interrupted 는 다시 돌림), verdict 0",
          code == "interrupted" and running == ["compare"] and resume_code == 0 and "sweep pa verify" not in resume_fake.calls and resume_fake.counts.get("compare pa") == 1
          and ("compare", "interrupted") in pa_steps and pa_steps.count(("sweep", "done")) == 1 and verdict_code == 0, (code, running, resume_code, pa_steps, report.get("problems")))
    code, journal, fake, _ = batch_case(world, configure_with(interrupts={"sweep pb verify": 1}))
    resume_code, journal, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    pb_sweeps = [(attempt["try"], attempt["exit"], attempt["state"]) for attempt in item_of(journal, "pb")["final"]["attempts"] if attempt["step"] == "sweep"]
    check("T107 재시도가 남은 기회 이어 하기: sweep 중 끊김(try 1 interrupted) → --resume 은 try 2 부터(ensure 추가 없음)·통과",
          resume_code == 0 and pb_sweeps == [(1, None, "interrupted"), (2, 0, "done")] and statuses_of(journal)["pb"] == "verified"
          and resume_fake.calls.count("ensure") == 1, (pb_sweeps, resume_fake.calls.count("ensure")))
    code, journal, fake, _ = batch_case(world, configure_with(interrupts={"sweep pa verify": 1}))
    second_code, journal, _, _ = batch_case(world, configure_with(interrupts={"sweep pa verify": 1}), extra=("--resume",), reset=False)
    third_code, journal, third_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="t179-interrupted")
    check("T179 interrupted sweep 두 번 → 두 시도를 다 쓴 기회는 실행 불가 → 그 실행 채택 incomplete(E2 아님), verdict incomplete·종료 1",
          (code, second_code, third_code) == ("interrupted", "interrupted", 0) and statuses_of(journal)["pa"] == "incomplete" and "sweep pa verify" not in third_fake.calls
          and verdict_code == 1 and report.get("stop") is None and decisions_of(report).get("2") == "incomplete", (code, second_code, third_code, report.get("problems")))
    code, journal, fake, _ = batch_case(world, configure_with(interrupts={"lock pa": 1}))
    lock_attempts = [(attempt["step"], attempt["state"]) for attempt in item_of(journal, "pa")["final"]["attempts"]]
    resume_code, journal, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="lock-interrupted")
    check("개정 G(b) lock_create interrupted(재부팅) → E2 아님: --resume 이 lock_create 를 다시 돌려 진행, verdict 는 그 시도를 E2 로 보지 않고 종료 0",
          code == "interrupted" and lock_attempts == [("lock_create", "running")] and resume_code == 0 and resume_fake.counts.get("lock pa") == 1
          and [attempt["state"] for attempt in item_of(journal, "pa")["final"]["attempts"] if attempt["step"] == "lock_create"] == ["interrupted", "done"]
          and verdict_code == 0, (code, lock_attempts, resume_code, report.get("stop"), report.get("problems")))
    journal_text = journal_text_of(world)

    def add_lock_attempt(state, exit_code):
        def edit(journal):
            attempt = {"step": "lock_verify", "stage": None, "opportunity": None, "try": None, "folder": None, "log": "", "started_at": now_text(), "exit": exit_code, "state": state}
            item_of(journal, "pb")["final"]["attempts"].insert(0, attempt)
        return set_journal(journal_text, edit)

    interrupted_code, interrupted_report, _ = journal_variant(world, journal_text, "lock-verify-interrupted", add_lock_attempt("interrupted", None))
    timeout_code, timeout_report, _ = journal_variant(world, journal_text, "lock-verify-timeout", add_lock_attempt("timeout", None))
    check("개정 G(b) verdict: 잠금 단계 lock_verify interrupted(종료 null) → E2 아님(종료 0), 시간 초과(timeout) → E2(P-09)",
          interrupted_code == 0 and timeout_code == 2 and (timeout_report.get("stop") or {}).get("event") == "E2",
          (interrupted_code, interrupted_report.get("problems"), timeout_code, timeout_report.get("stop")))
    code, journal, fake, _ = batch_case(world, configure_with(interrupts={"verify-lock f7-original": 1}))

    def lie(journal):
        item_of(journal, "g13")["final"]["self_check"]["first"] = False
    edit_json(journal_text, lie)
    resume_code, journal, _, _ = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="t168-lie")
    gate = journal["final_batch"]["f0_gate"] or {}
    check("T168 일지 self_check 거짓 기록·파일은 일치 → calib_batch F0 관문은 파일 기준으로 참(F1 진행), verdict 는 일지 ≠ 다시 계산이라 2",
          code == "interrupted" and resume_code == 0 and gate.get("self_checks", {}).get("g13") == {"first": True, "rerun": None} and gate.get("passed") is True
          and item_of(journal, "g13")["final"]["self_check"]["first"] is False and verdict_code == 2 and report.get("gate") == "recompute",
          (code, resume_code, gate, verdict_code, report.get("gate")))


def test_batch_f0_gate(world):
    code, journal, fake, _ = batch_case(world, configure_with(tamper_compare={("f2", "verify-1"): tamper_f2}))
    statuses = statuses_of(journal)
    verdict_code, report, _ = run_verdict(world, out_name="batch-e3")
    check("T105·T183 공식 = 비교기에서 장면 control_allowed 만 다름 → 즉시 종료 1·outcome E3·f0_gate null·남은 F0·F1·F2 pending·호출 0, verdict stop E3 next restart",
          code == 1 and outcome_of(journal).get("event") == "E3" and statuses["f2"] == "self_check_mismatch" and journal["final_batch"]["f0_gate"] is None
          and all(statuses[key] == "pending" for key in ("uw", "f7", "pa", "pawab")) and calls_after(fake, "compare f2") == ["release"]
          and item_of(journal, "f2")["final"]["self_check"] == {"first": False, "rerun": None} and verdict_code == 1
          and report.get("stop") == {"event": "E3", "next": "restart"}, (code, statuses, verdict_code, report.get("problems")))
    code, journal, fake, _ = batch_case(world, configure_with(sweeps={"uw": [{"exit": 1}, {"exit": 1}]}))
    gate = journal["final_batch"]["f0_gate"] or {}
    check("T115·T182 F0 한 건 첫 기회 E1 두 번으로 미완 → 표본 3 → F1 시작, 그 채택 incomplete",
          code == 0 and gate.get("sample") == ["f2", "f7", "g13"] and gate.get("passed") is True and statuses_of(journal)["uw"] == "incomplete" and "sweep pa verify" in fake.calls,
          gate)
    code, journal, fake, _ = batch_case(world, configure_with(sweeps={"uw": [{"exit": 1}, {"exit": 1}], "f7": [{"states": {"18": "undetermined"}}, {"exit": 1}, {"exit": 1}]}))
    gate = journal["final_batch"]["f0_gate"] or {}
    verdict_code, report, _ = run_verdict(world, out_name="batch-shortfall")
    check("T115·T182 두 건 미완(1회차 판정 불가 + rerun 실행 불가 포함, 1회차 자기 검증은 참) → 표본 2 → F0_shortfall·종료 4·F1 하위 프로세스 0, verdict 전 채택 incomplete",
          code == 4 and outcome_of(journal).get("event") == "F0_shortfall" and gate.get("sample") == ["f2", "g13"] and gate.get("passed") is False
          and gate.get("self_checks", {}).get("f7") == {"first": True, "rerun": None} and not any(label.split()[1] in ("pa", "pb", "pnp20", "pcw", "f5") for label in fake.calls
                                                                                             if label.startswith(("sweep", "lock", "compare")))
          and verdict_code == 1 and set(decisions_of(report).values()) == {"incomplete"}, (code, gate, verdict_code, report.get("problems")))
    code, journal, fake, _ = batch_case(world, configure_with(sweeps={"f2": [{"states": {"15": "undetermined"}}, {"states": {"15": "pass"}}]}))
    f2 = item_of(journal, "f2")["final"]
    official = read_json(f"{folder_of(world, 'pf2')}/verify.json")
    verdict_code, report, _ = run_verdict(world, out_name="batch-f2-control")
    check("T188 개정 F2: 공식 장면 control_ok 거짓·checks 모두 참(verify.passed 참) → 1회차 official_first undetermined(통과 아님)·rerun, 결정 pass, verdict 0",
          code == 0 and official["verify"]["passed"] is True and not official["scenes"][0]["control_ok"] and f2["official_first"] == {"15": "undetermined"}
          and f2["adoptions"] == {"15": "pass"} and f2["rerun"] == 1 and verdict_code == 0, (code, f2.get("official_first"), verdict_code, report.get("problems")))


def restart_history(world, old_w_anchor, extra_entries=()):
    return [*extra_entries, old_w_anchor, read_json(world["w_anchor"])]


def fake_w_anchor_history(path):
    current = read_json(path)
    entries = list(GIT["history"]) if GIT["history"] is not None else list(GIT["commits"].get(verify.normalized(path), []))
    contents = [entry["content"] if "content" in entry else entry for entry in entries]
    if not contents or contents[-1] != current:
        entries.append(current)
    return [entry if "content" in entry else {"committed_at": GIT["time"], "content": entry} for entry in entries]


def commit_w_anchor(w_anchor_text):
    GIT["commits"].setdefault(verify.normalized(w_anchor_text), []).append(read_json(w_anchor_text))


def fresh_git(w_anchor_text, final_dir):
    document = read_json(w_anchor_text)
    base = {key: document[key] for key in verify.W_ANCHOR_BASE_KEYS}
    write_json(w_anchor_text, base)
    earlier = []
    expect = read_json(f"{final_dir}/expect.json")
    archives = verify.archive_directories(final_dir)
    old_journal = f"{archives[0]}/batch-e1/journal.json" if len(archives) == 1 else None
    if expect["edition"] == "e1" and expect["restart"] == 1 and old_journal and absolute(old_journal).is_file():
        old = read_json(old_journal)["final_batch"]
        old_base = {"anchor": f"{final_dir}/anchor.json", "edition": "e1", "sha256": old["anchor_sha256"]}
        earlier += [old_base, dict(old_base, journal_id=old["journal_id"], restart=0)]
    GIT["commits"][verify.normalized(w_anchor_text)] = [*earlier, base]


def pin_simulated(journal, final_dir, w_anchor_text):
    fresh_git(w_anchor_text, final_dir)
    write_json(w_anchor_text, verify.pinned_w_anchor(read_json(w_anchor_text), journal, final_dir))
    commit_w_anchor(w_anchor_text)


def pin_by_tool(final_dir, w_anchor_text):
    code, output = run_main(["pin-journal", "--spec", f"{final_dir}/final-spec.json", "--w-anchor", w_anchor_text, "--w-anchor-out", w_anchor_text])
    if code == 0:
        commit_w_anchor(w_anchor_text)
    return code, output


def test_batch_restart(world):
    final_dir, root = world["final"], world["root"]
    code, journal, fake, _ = batch_case(world, configure_with(tamper_compare={("f2", "verify-1"): tamper_f2}))
    old_w_anchor = read_json(world["w_anchor"])
    archive = archive_e1(world)
    expect_code, _ = rerun_expect(world)
    anchor_code, anchor_output = run_anchor(world)
    check("재시작 준비: E3(종료 1) → 기록 폴더로 옮김 → expect restart 1 → anchor restart 1 종료 0",
          code == 1 and expect_code == 0 and anchor_code == 0 and read_json(f"{final_dir}/expect.json")["restart"] == 1, (code, expect_code, anchor_output[-300:]))
    GIT["history"] = [read_json(world["w_anchor"])]
    missing_code, _, missing_fake, missing_output = batch_case(world)
    GIT["history"] = restart_history(world, old_w_anchor)
    old_journal = f"{archive}/batch-e1/journal.json"
    with restored(old_journal):
        edit_json(old_journal, lambda document: document["final_batch"].update(anchor_sha256="0" * 64))
        sha_code, _, sha_fake, sha_output = batch_case(world)
    check("T184 W11 닻 확인: 가짜 w_anchor_history 에 옮긴 anchor.json sha 를 담은 e1 커밋 없음 → 2, 옛 anchor.json sha ≠ 옛 일지 anchor_sha256 → 2(둘 다 하위 프로세스 0)",
          missing_code == 2 and not missing_fake.calls and "e1 커밋이 없다" in missing_output and sha_code == 2 and not sha_fake.calls and "W11 닻 확인" in sha_output,
          (missing_output[-300:], sha_output[-300:]))
    outside = {}
    for name, target in (("test_owner", f"{final_dir}/test_owner.json"), ("명세", world["spec"]), ("목표 파일", f"{root}/targets/g_wall.json")):
        with restored(target, f"{final_dir}/expect.json", f"{final_dir}/final_queue.json", f"{final_dir}/anchor.json", world["w_anchor"], *copy_output_paths(world)):
            append_byte(target)
            copied = run_main(["copies", "--spec", world["spec"], "--out-dir", final_dir])[0] == 0
            rebuilt = copied and rerun_expect(world)[0] == 0 and run_anchor(world)[0] == 0
            GIT["history"] = restart_history(world, old_w_anchor)
            code, _, fake, output = batch_case(world)
            outside[name] = (rebuilt, code, fake.calls, "재시작 범위" in output)
    with restored(f"{final_dir}/plans/pc_p20-w.json"):
        append_byte(f"{final_dir}/plans/pc_p20-w.json")
        code, _, fake, output = batch_case(world)
        outside["r1 계획 사본(닻 대조가 먼저)"] = (True, code, fake.calls, "닻 대조" in output)
    old_anchor = f"{archive}/anchor.json"
    with restored(old_anchor, old_journal, f"{final_dir}/anchor.json", world["w_anchor"]):
        edit_json(old_anchor, lambda document: document["files"].update({f"{root}/only-in-old.json": "0" * 64}))
        edit_json(old_journal, lambda document: document["final_batch"].update(anchor_sha256=sha(old_anchor)))
        rebuilt = run_anchor(world)[0] == 0
        GIT["history"] = restart_history(world, dict(old_w_anchor, sha256=sha(old_anchor)))
        code, _, fake, output = batch_case(world)
        outside["옛 닻에만 있는 경로"] = (rebuilt, code, fake.calls, "옮긴 W11 닻에만 있는 경로" in output)
    GIT["history"] = restart_history(world, old_w_anchor)
    check("T184 재시작 범위: 세 파일 밖 항목(test_owner·명세·목표 파일 1바이트, 옛 닻에만 있는 경로) → 종료 2·하위 프로세스 0, r1 계획 사본 1바이트는 닻 대조에서 2",
          all(rebuilt and code == 2 and not calls and has_reason for rebuilt, code, calls, has_reason in outside.values()), outside)
    late_commit = {"committed_at": (datetime.now().astimezone() + timedelta(hours=2)).isoformat(timespec="seconds"), "content": old_w_anchor}
    GIT["history"] = [late_commit, read_json(world["w_anchor"])]
    late_code, _, late_fake, late_output = batch_case(world)
    GIT["history"] = restart_history(world, old_w_anchor)
    second_archive = f"{final_dir}/aborted-20261105-130000"
    absolute(second_archive).mkdir(parents=True, exist_ok=True)
    double_code, _, double_fake, double_output = batch_case(world)
    absolute(second_archive).rmdir()
    check("개정 H S-4(반박 fixture H3·RI S3): 옮긴 anchor.json sha 를 담은 e1 커밋 시각이 옛 일지 첫 calib_batch 시작보다 늦음 → 2, 기록 폴더 2개 → 2(둘 다 하위 프로세스 0)",
          late_code == 2 and not late_fake.calls and "커밋 시각" in late_output and double_code == 2 and not double_fake.calls and "기록 폴더" in double_output,
          (late_output[-300:], double_output[-300:]))
    pre_w11 = [dict(old_w_anchor, sha256="1" * 64), dict(old_w_anchor, sha256="2" * 64)]
    GIT["history"] = restart_history(world, old_w_anchor, pre_w11)
    code, journal, fake, output = batch_case(world)
    creates = [attempt["step"] for item in journal["items"] for attempt in item["final"]["attempts"] if attempt["step"].startswith("judge_lock")]
    verdict_code, report, _ = run_verdict(world, out_name="batch-restart-ok")
    check("T184·T185·T167 재시작 정상: W2 다시 하기로 W11 전 커밋이 여럿이어도 옛 일지가 대조한 커밋으로 통과, 판정 잠금 4개 judge_lock_create(verify 0), verdict 새 일지만 읽고 0",
          code == 0 and creates == ["judge_lock_create"] * 4 and verdict_code == 0 and report.get("restart") == 1, (code, output[-300:], creates, report.get("problems")))
    code, journal, fake, _ = batch_case(world, configure_with(tamper_compare={("f2", "verify-1"): tamper_f2}))
    verdict_code, report, _ = run_verdict(world, out_name="batch-restart-e3")
    check("T167 restart 1 에서 다시 E3 → outcome E3_repeat·종료 1 → verdict 전 채택 incomplete('D1 두 번째 E3')",
          code == 1 and outcome_of(journal).get("event") == "E3_repeat" and verdict_code == 1 and (report.get("stop") or {}).get("event") == "E3_repeat"
          and len(report.get("incomplete", [])) == 12, (code, outcome_of(journal), report.get("stop")))
    GIT["history"] = None


def prepare_r2_batch_world(name, verify_plan, ab_plan, fog_fix):
    world = build_world(name)
    with world_scope(world):
        final_dir, root = world["final"], world["root"]
        scenario(world, "w11", verify_plan, ab_plan)
        run_verdict(world, phase="w13", out_name="w13")
        steps = prepare_r2(world, f"{root}/verdict/w13.json")
        if fog_fix:
            written = read_json(f"{final_dir}/r2/w-expect.json")["written"]["tuning"]
            write_text(world["fog_generator"], fog_generator_text(4.0))
            write_json(world["fog_report"], {"preset_tuning_sha256": written})
        world["r2_anchor"] = f"{root}/w-anchor-r2.json"
        steps.append(rerun_expect(world, f"{final_dir}/r2", world["r2_anchor"]))
        steps.append(run_anchor(world, f"{final_dir}/r2/final-spec.json", world["r2_anchor"]))
        write_tests(world, "w10-rhi-r2")
    world["r2_ready"] = all(code == 0 for code, _ in steps)
    world["r2_detail"] = "; ".join(output[-200:] for code, output in steps if code)
    return world


def test_batch_r2_kept(world):
    final_dir, root = world["final"], world["root"]
    r2_final = f"{final_dir}/r2"
    check("r2 준비(#15 되돌림, #18 남김): rollback·r2-spec·copies·expect·anchor r2 종료 0", world["r2_ready"], world["r2_detail"])
    code, journal, fake, output = batch_case(world, final_dir=r2_final)
    verdict_code, report, _ = run_verdict(world, phase="r2", final_dir=r2_final, out_name="batch-r2", w_anchor=world["r2_anchor"])
    check("T186 r2 시작: 새 경로가 W12 출력·r2 폴더 출력뿐 → 통과, F0 표본 하한 없음, r2 일지 → verdict r2 종료 0(w-anchor edition r2 판 대조 통과)",
          code == 0 and journal["final_batch"]["edition"] == "r2" and (journal["final_batch"]["f0_gate"] or {}).get("required") == 0
          and verdict_code == 0 and set(decisions_of(report)) == {"18", "f5"}, (code, output[-300:], verdict_code, report.get("problems")))
    results = {}
    for name, target in (("r1 명세", world["spec"]), ("r1 계획 사본", f"{final_dir}/plans/pc_p20-w.json"), ("r1 목표 파일", f"{root}/targets/g_wall.json")):
        with restored(target):
            append_byte(target)
            code, _, fake, output = batch_case(world, final_dir=r2_final)
        results[name] = (code, fake.calls)
    with restored(world["fog_generator"]):
        write_text(world["fog_generator"], fog_generator_text(4.0))
        code, _, fake, output = batch_case(world, final_dir=r2_final)
        results["#18 남김인데 생성기 4.0"] = (code, fake.calls, "D5" in output)
    check("T186·T187 r2 시작: r1 닻 항목(r1 명세·r1 계획 사본·목표 파일)을 anchor r2 뒤에 바꿈 → 종료 2·하위 프로세스 0, #18 남김인데 생성기 4.0 → 2(D5)",
          all(value[0] == 2 and not value[1] for value in results.values()) and results["#18 남김인데 생성기 4.0"][2], results)
    gates = {}
    r1_anchor = f"{final_dir}/anchor.json"
    with restored(r1_anchor):
        append_byte(r1_anchor)
        code, _, fake, output = batch_case(world, final_dir=r2_final)
        gates["r1 anchor.json 1바이트(previous_anchor sha 다름)"] = (code, fake.calls, "previous_anchor" in output)
    r2_anchor = f"{r2_final}/anchor.json"
    outside_target = f"{root}/targets/new_target_outside.json"
    write_json(outside_target, {"scene": "new"})
    with restored(r2_anchor, world["r2_anchor"]):
        edit_json(r2_anchor, lambda document: document["files"].update({outside_target: sha(outside_target)}))
        edit_json(world["r2_anchor"], lambda document: document.update(sha256=sha(r2_anchor)))
        code, _, fake, output = batch_case(world, final_dir=r2_final)
        gates["r2 닻에 허용 목록 밖 새 경로(w-anchor sha 맞춤)"] = (code, fake.calls, "허용 목록 밖" in output)
    check("개정 H(반박 적합 S4·T170·T186): calib_batch r2 시작 — previous_anchor(r1 anchor.json) sha 다름·허용 목록 밖 새 경로 → 각각 종료 2·하위 프로세스 0·그 사유",
          all(code == 2 and not calls and has_reason for code, calls, has_reason in gates.values()), gates)


def test_batch_r2_fog(world):
    final_dir = world["final"]
    r2_final = f"{final_dir}/r2"
    check("r2 준비(#18 되돌림): 생성기 4.0·무대 보고 갱신 뒤 anchor r2 종료 0", world["r2_ready"], world["r2_detail"])
    results = {}
    with restored(world["fog_generator"]):
        write_text(world["fog_generator"], fog_generator_text(8.0))
        code, _, fake, output = batch_case(world, final_dir=r2_final)
        results["생성기 8.0"] = (code, fake.calls, "D5" in output)
    with restored(world["fog_report"]):
        past = datetime(2026, 1, 1).timestamp()
        os.utime(absolute(world["fog_report"]), (past, past))
        code, _, fake, output = batch_case(world, final_dir=r2_final)
        results["무대 보고 mtime ≤ rollback finished_at"] = (code, fake.calls, "D5" in output)
    with restored(world["fog_report"]):
        write_json(world["fog_report"], {"preset_tuning_sha256": "0" * 64})
        code, _, fake, output = batch_case(world, final_dir=r2_final)
        results["보고 preset sha ≠ r2 written"] = (code, fake.calls, "D5" in output)
    code, journal, fake, output = batch_case(world, final_dir=r2_final)
    check("T187 calib_batch r2 시작 D5: #18 되돌림인데 생성기 8.0·무대 보고 mtime ≤ finished_at·보고 preset sha ≠ written → 각각 종료 2(E2)·하위 프로세스 0, 모두 맞으면 시작 처리로 가서 종료 0",
          all(value[0] == 2 and not value[1] and value[2] for value in results.values()) and code == 0 and fake.calls[:2] == ["acquire", "ensure"], (results, code, output[-300:]))


def test_artifact_gate_outputs(world):
    final_dir, root = world["final"], world["root"]
    expect_text = f"{final_dir}/expect.json"
    w_expect_text = read_json(world["spec"])["w_expect"]
    test_owner_text = read_json(world["spec"])["test_owner"]
    before = {text: absolute(text).read_bytes() for text in (expect_text, w_expect_text, test_owner_text)}
    with restored(world["spec"]):
        edit_json(world["spec"], lambda document: document.update(restart=0))
        expect_code, expect_output = run_main(["expect", "--spec", world["spec"], "--out", expect_text, "--queue-out", f"{final_dir}/final_queue.json"])
    w_expect_code, w_expect_output = run_main(["w-expect", "--overlay", read_json(world["spec"])["overlay"], "--before-dir", f"{root}/missing-before", "--preview-dir",
                                               read_json(world["spec"])["preview_dir"], "--builder-before", f"{final_dir}/builder-before/preset_builder_dump.json", "--out", w_expect_text])
    owner_code, owner_output = run_main(["test-owner", "--overlay", read_json(world["spec"])["overlay"], "--test-classes", f"{root}/missing-classes.json",
                                         "--source-root", f"{root}/code", "--out", test_owner_text])
    after = {text: absolute(text).read_bytes() for text in before}
    check("개정 G(a) expect·w-expect·test-owner 관문 실패 → 종료 2, 관문 보고는 표준 오류에만('관문 …'), --out 산출물 바이트 불변",
          (expect_code, w_expect_code, owner_code) == (2, 2, 2) and after == before and all("관문" in output for output in (expect_output, w_expect_output, owner_output)),
          (expect_code, w_expect_code, owner_code, [text for text in before if after[text] != before[text]]))
    compare_out = f"{root}/cmp/gate-report.json"
    code, output = run_main(["compare", "--expect", expect_text, "--run", "nowhere", "--sweep", f"{root}/none.json", "--lock", f"{root}/none.json", "--work-dir", f"{root}/cmp/gate",
                             "--out", compare_out, "--w-anchor", world["w_anchor"]])
    check("개정 G(a) 대조: 산출물 명령이 아닌 compare 관문 실패는 지금처럼 --out 에 관문 보고를 쓴다", code == 2 and absolute(compare_out).is_file() and read_json(compare_out).get("gate"),
          output[-300:])


def test_r2_targets_covered_child(world):
    synthetic = {"adoptions": {"p": {"entry_id": "parent", "scenes": ["s1"], "levels": ["L1"], "owner": "O"}, "c": {"entry_id": "child", "scenes": ["s2"], "levels": ["L2"], "owner": "Q"},
                               "r": {"entry_id": "reverted", "scenes": ["s9"], "levels": ["L9"], "owner": "O"}},
                 "runs": {"child_run": {"plan": plan_path(world, "pa")}, "r_run": {"plan": plan_path(world, "pg")}}, "fallback_runs": {"parent_run": {"plan": plan_path(world, "pu")}},
                 "rows": [{"run": "child_run", "adoption": "c", "scene": "s2"}, {"run": "r_run", "adoption": "r", "scene": "s9"}],
                 "fallback_rows": [{"run": "parent_run", "adoption": "p", "scene": "s1"}], "influences": [],
                 "covered": [{"adoption": "p", "by_run": "child_run", "by_adoption": "c", "fallback": {"id": "parent_run"}}]}
    targets = verify.r2_targets(synthetic, {"reverted"})
    child_reverted = verify.r2_targets(synthetic, {"child"})
    check("개정 G(c) r2 대상: covered 부모만 대상(owner 공유)이고 덮는 자식은 아니면 → 자식 채택도 대상에 넣어 덮는 실행이 r2 큐에 남는다. 자식을 되돌리면 부모는 fallback",
          targets == {"adoptions": ["c", "p"], "runs": ["child_run"], "influences": []} and child_reverted == {"adoptions": ["p"], "runs": ["parent_run"], "influences": []},
          (targets, child_reverted))


SHORTFALL_SWEEPS = {"uw": [{"exit": 1}, {"exit": 1}], "f7": [{"states": {"18": "undetermined"}}, {"exit": 1}, {"exit": 1}]}
F1_RUN_IDS = ("pa", "pb", "pnp20", "pcw", "f5")


def f1_calls_of(fake):
    return [label for label in fake.calls if label.split()[0] in ("sweep", "compare", "lock", "verify-lock") and label.split()[1] in F1_RUN_IDS]


def crash_after_record(label, outcome):
    original = calib_batch.FinalBatchRunner.record

    def crashing(runner, record_label, record_outcome, detail=""):
        original(runner, record_label, record_outcome, detail)
        if (record_label, record_outcome) == (label, outcome):
            raise KeyboardInterrupt(f"{label} {outcome} 기록 직후 끊김")
    return patched(calib_batch.FinalBatchRunner, record=crashing)


def move_batch_out(world, tag):
    target = absolute(f"{world['root']}/moved-{tag}/batch-e1")
    shutil.rmtree(target.parent, ignore_errors=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(absolute(f"{world['final']}/batch-e1")), str(target))


def raw_batch(world, extra=()):
    queue = read_json(f"{world['final']}/final_queue.json")
    fake = FakeFinalTools(world)
    argv = ["calib_batch.py", "--queue", f"{world['final']}/final_queue.json", "--holder", "w-final", "--journal", queue["final"]["journal"], *extra]
    with batch_scope(world, fake):
        code, output = call_main(calib_batch.main, argv)
    return code, fake, output


def interrupt_parent():
    raise KeyboardInterrupt("부모만 끊김(자식 calib_sweep 은 sweep.json 을 다 썼다)")


def test_fix_h_f0_gate(world):
    journal_text = journal_text_of(world)
    with crash_after_record("F0 관문", "멈춤"):
        code, journal, _, _ = batch_case(world, configure_with(sweeps=SHORTFALL_SWEEPS))
    crashed = ((journal["final_batch"]["f0_gate"] or {}).get("passed"), outcome_of(journal))
    resume_code, journal, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="h-gate-crash")
    check("개정 H M-A(반박 RI M2·적합 M1·fixture A1): F0 결원 관문 기록 뒤·outcome 기록 전 끊김 → --resume 이 산출물로 관문을 다시 계산해 F1 없이 F0_shortfall·종료 4, verdict 전 채택 incomplete",
          code == "interrupted" and crashed == (False, {}) and resume_code == 4 and outcome_of(journal).get("event") == "F0_shortfall" and not f1_calls_of(resume_fake)
          and verdict_code == 1 and (report.get("stop") or {}).get("event") == "F0_shortfall" and set(decisions_of(report).values()) == {"incomplete"}
          and len(decisions_of(report)) == 12, (code, crashed, resume_code, f1_calls_of(resume_fake), verdict_code, report.get("stop"), report.get("problems")))
    edit_json(journal_text, lambda document: document["final_batch"].update(outcome=None))
    outcome_code, _, outcome_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    edit_json(journal_text, lambda document: document["final_batch"].update(outcome=None, events=[]))
    erased_code, journal, erased_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    check("개정 H M-A·S-3: 결원 뒤 outcome 만 지움 → --resume 은 events 로 하위 프로세스 0·종료 4, outcome·events 를 다 지움 → 관문 다시 계산으로 F1 없이 다시 F0_shortfall·4",
          outcome_code == 4 and not outcome_fake.calls and erased_code == 4 and not f1_calls_of(erased_fake) and outcome_of(journal).get("event") == "F0_shortfall",
          (outcome_code, outcome_fake.calls, erased_code, f1_calls_of(erased_fake)))
    original_gate = calib_batch.FinalBatchRunner.pass_f0_gate

    def trusting_gate(runner):
        if runner.final_batch.get("f0_gate") is not None:
            return
        original_gate(runner)
    with crash_after_record("F0 관문", "멈춤"):
        batch_case(world, configure_with(sweeps=SHORTFALL_SWEEPS))
    with patched(calib_batch.FinalBatchRunner, pass_f0_gate=trusting_gate):
        old_code, journal, old_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="h-gate-trusting")
    check("개정 H M-A verdict: f0_gate passed 거짓인데 F1·F2 가 끝난 일지(옛 코드의 재개, outcome 없음) → F1·F2 결과와 상관없이 전 채택 incomplete·stop F0_shortfall·종료 1",
          old_code == 0 and f1_calls_of(old_fake) and outcome_of(journal) == {} and verdict_code == 1 and (report.get("stop") or {}).get("event") == "F0_shortfall"
          and set(decisions_of(report).values()) == {"incomplete"} and len(decisions_of(report)) == 12, (old_code, verdict_code, report.get("gate"), report.get("problems")))


def test_fix_h_redraw(world):
    final_dir = world["final"]
    code, _, _, _ = batch_case(world, configure_with(exits={"compare pa": [2]}))
    again_code, _, again_fake, again_output = batch_case(world, reset=False)
    move_batch_out(world, "e2")
    moved_code, _, moved_fake, moved_output = batch_case(world, reset=False)
    check("개정 H M-B(반박 RI M1 반례 1): E2 뒤 --resume 없이 다시 → 일지 있음 종료 2(문구에 옮기기 권고 없음), batch-e1 을 final 밖으로 옮기고 새로 시작 → 남은 잠금(다른 일지 id)으로 E2·종료 2·하위 프로세스 0",
          code == 2 and again_code == 2 and not again_fake.calls and "옮" not in again_output and moved_code == 2 and not moved_fake.calls
          and "이 일지가 만들지 않은" in moved_output and "옮" not in moved_output, (code, again_output[-200:], moved_code, moved_output[-300:]))
    code, _, _, _ = batch_case(world, configure_with(sweeps={"pa": [{"states": {"2": "fail"}}]}))
    first_verdict, _, _ = run_verdict(world, out_name="h-fail-first")
    move_batch_out(world, "fail")
    moved_code, _, moved_fake, _ = batch_case(world, reset=False)
    code_e3, _, _, _ = batch_case(world, configure_with(tamper_compare={("f2", "verify-1"): tamper_f2}))
    shutil.rmtree(absolute(f"{final_dir}/batch-e1"))
    deleted_code, _, deleted_fake, _ = batch_case(world, reset=False)
    check("개정 H M-B(반박 RI M1 반례 2·fixture D2): 실패를 본 뒤 batch-e1 옮김·E3 뒤 batch-e1 삭제(판정 잠금은 남음) → 새로 시작 각각 종료 2·하위 프로세스 0(다시 추첨 없음)",
          code == 0 and first_verdict == 1 and moved_code == 2 and not moved_fake.calls and code_e3 == 1 and deleted_code == 2 and not deleted_fake.calls,
          (code, first_verdict, moved_code, code_e3, deleted_code))
    batch_case(world)
    absolute(journal_text_of(world)).unlink()
    shutil.rmtree(absolute(f"{final_dir}/locks"))
    folder_code, _, folder_fake, folder_output = batch_case(world, reset=False)
    batch_case(world, configure_with(interrupts={"compare pb": 1}))
    foreign_lock = f"{final_dir}/locks/zz-e1-prereg-lock.json"
    make_lock(queue_judge_files(read_json(f"{final_dir}/final_queue.json")), foreign_lock, f"fixture {calib_batch.journal_note('0' * 32)}")
    resume_code, _, resume_fake, resume_output = batch_case(world, extra=("--resume",), reset=False)
    check("개정 H M-B: journal.json·잠금만 지우고 실행 폴더가 남음 → 새 시작 2(일지가 모르는 실행 폴더), 이어 하기 중 다른 일지 id 잠금이 생김 → --resume 2(둘 다 하위 프로세스 0)",
          folder_code == 2 and not folder_fake.calls and "실행 폴더" in folder_output and resume_code == 2 and not resume_fake.calls and "zz-e1" in resume_output,
          (folder_code, folder_output[-300:], resume_code, resume_output[-300:]))
    batch_case(world, configure_with(sweeps={"pa": [{"states": {"2": "fail"}}]}))
    move_batch_out(world, "bypass")
    fresh_git(world["w_anchor"], final_dir)
    with patched(calib_batch, foreign_artifact_problems=lambda final_dir_text, edition, journal: []):
        bypass_code, _, bypass_fake, _ = batch_case(world, reset=False)
    bypass_verdict, bypass_report, _ = run_verdict(world, out_name="h-bypass")
    check("개정 H M-B verdict: calib_batch 검사를 건너뛰고 남은 잠금(다른 일지 id)을 verify 로 다시 쓴 일지 → verdict 2(gate journal, 잠금 note 에 이 일지 id 없음)",
          bypass_code == 0 and "verify-lock pa" in bypass_fake.calls and bypass_verdict == 2 and bypass_report.get("gate") == "journal"
          and any("일지 id" in line for line in bypass_report.get("problems", [])), (bypass_code, bypass_verdict, bypass_report.get("gate"), bypass_report.get("problems")))
    batch_case(world)
    extra_lock = f"{final_dir}/locks/zz-e1-prereg-lock.json"
    with restored(extra_lock):
        shutil.copyfile(absolute(f"{final_dir}/locks/pa-e1-prereg-lock.json"), absolute(extra_lock))
        lock_verdict, lock_report, _ = run_verdict(world, out_name="h-extra-lock")
    stash = f"{final_dir}/stash/runs/pa/verify-1"
    shutil.copytree(absolute(folder_of(world, "pa")), absolute(stash))
    folder_verdict, folder_report, _ = run_verdict(world, out_name="h-extra-folder")
    shutil.rmtree(absolute(f"{final_dir}/stash"))
    clean_verdict, _, _ = run_verdict(world, out_name="h-clean")
    check("개정 H M-B verdict: 일지가 쓰지 않은 잠금이 final_dir/locks 에 있음 → 2(gate journal), 일지가 모르는 실행 폴더가 final_dir 안 다른 곳에 있음 → 2(gate artifacts), 지운 뒤 0",
          lock_verdict == 2 and lock_report.get("gate") == "journal" and folder_verdict == 2 and folder_report.get("gate") == "artifacts" and clean_verdict == 0,
          (lock_verdict, lock_report.get("gate"), folder_verdict, folder_report.get("gate"), folder_report.get("problems"), clean_verdict))


def test_fix_h_journal_damage(world):
    journal_text = journal_text_of(world)
    batch_case(world, configure_with(interrupts={"compare pb": 1}))
    absolute(journal_text).write_bytes(b"\x00" * absolute(journal_text).stat().st_size)
    resume_code, resume_fake, resume_output = raw_batch(world, ("--resume",))
    fresh_code, fresh_fake, fresh_output = raw_batch(world)
    write_json(journal_text, {"items": [], "final_batch": {}})
    broken_code, broken_fake, broken_output = raw_batch(world, ("--resume",))
    check("개정 H S-1(반박 fixture C1·적합 S2): 재부팅으로 일지가 NUL → --resume 종료 2·하위 프로세스 0·'재부팅 손상'(사람 판단), 새 시작도 2(일지 있음), 키가 빠진 일지 → --resume 2",
          resume_code == 2 and not resume_fake.calls and "재부팅 손상" in resume_output and fresh_code == 2 and not fresh_fake.calls
          and broken_code == 2 and not broken_fake.calls and "재부팅 손상" in broken_output, (resume_output[-200:], fresh_output[-200:], broken_output[-200:]))
    fsync_calls = []
    original_fsync = os.fsync

    def counting_fsync(descriptor):
        fsync_calls.append(descriptor)
        return original_fsync(descriptor)
    with patched(calib_batch.os, fsync=counting_fsync):
        code, journal, _, _ = batch_case(world)
    raw = absolute(journal_text).read_bytes()
    check("개정 H S-1: 최종 모드 일지 저장은 임시 파일 flush·os.fsync 뒤 os.replace(fsync 호출 있음, 남은 .tmp 없음, LF), journal_id·events 키",
          code == 0 and fsync_calls and b"\r" not in raw and not absolute(journal_text + ".tmp").exists() and len(journal["final_batch"]["journal_id"]) == 32
          and journal["final_batch"]["events"] == [], (code, len(fsync_calls)))
    judge_lock = f"{world['final']}/locks/g13-judge-e1-prereg-lock.json"

    def partial_lock_write():
        absolute(judge_lock).parent.mkdir(parents=True, exist_ok=True)
        absolute(judge_lock).write_bytes(b"\x00" * 512)
        raise KeyboardInterrupt("판정 잠금 쓰는 중 끊김")
    code, _, _, _ = batch_case(world, configure_with(hooks={"lock g13-judge": partial_lock_write}))
    resume_code, _, resume_fake, resume_output = batch_case(world, extra=("--resume",), reset=False)
    check("개정 H(반박 fixture I1): 판정 잠금 쓰는 중 끊김(잠금 파일 NUL) → --resume 은 하위 프로세스 없이 종료 2(E2 '읽지 못함 — 재부팅 손상 의심', 사람이 판단, 자동으로 다시 만들지 않음)",
          code == "interrupted" and resume_code == 2 and not resume_fake.calls and "읽지 못함" in resume_output, (code, resume_code, resume_output[-300:]))
    live = f"{world['live']}/{fluid_tuning_patch.TUNING_FILE_NAME}"
    with restored(live):
        absolute(live).unlink()
        code, journal, fake, _ = batch_case(world)
    check("개정 H(반박 적합 S1·R4): 시작 처리 중 정본 파일 읽기 예외(FileNotFoundError) → 잡히지 않은 종료 1 이 아니라 E2·종료 2, starts[-1].ok false, 보류 해제",
          code == 2 and outcome_of(journal).get("event") == "E2" and journal["final_batch"]["starts"][-1]["ok"] is False and fake.calls[-1] == "release",
          (code, outcome_of(journal), fake.calls))


def test_fix_h_recovered_sweep(world):
    code, _, _, _ = batch_case(world, configure_with(sweeps={"pa": [{"states": {"2": "fail"}}]}, after_hooks={"sweep pa verify": interrupt_parent}))
    resume_code, journal, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    pa = item_of(journal, "pa")["final"]
    sweeps = [(attempt["try"], attempt["exit"], attempt["state"]) for attempt in pa["attempts"] if attempt["step"] == "sweep"]
    verdict_code, report, _ = run_verdict(world, out_name="h-recovered")
    check("개정 H S-2(반박 RI S1): 자식이 sweep.json(invalid_count 0)을 다 쓴 뒤 부모만 끊김 → --resume 은 다시 찍지 않고(sweep pa 0회) 그 결과(채택 2 fail)로 판정, 시도 recovered·sha 기록, verdict 관문 없음",
          code == "interrupted" and resume_code == 0 and sweeps == [(1, 0, "recovered")] and "sweep pa verify" not in resume_fake.calls and pa["adoptions"] == {"2": "fail"}
          and f"{folder_of(world, 'pa')}/sweep.json" in pa["artifact_sha256"] and report.get("gate") is None and decisions_of(report).get("2") == "user_decision",
          (code, resume_code, sweeps, report.get("gate"), report.get("problems")))
    batch_case(world, configure_with(sweeps={"pa": [{"states": {"2": "fail"}}]}, after_hooks={"sweep pa verify": interrupt_parent}))
    with patched(calib_batch, is_completed_sweep=lambda folder_text: False):
        redraw_code, _, redraw_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    redraw_verdict, redraw_report, _ = run_verdict(world, out_name="h-redraw")
    check("개정 H S-2 verdict: 끝난 스윕을 interrupted 로 세고 try 2 로 다시 찍은 일지(옛 코드) → verdict 2(gate artifacts, 다시 추첨)",
          redraw_code == 0 and redraw_fake.counts.get("sweep pa verify") == 1 and redraw_verdict == 2 and redraw_report.get("gate") == "artifacts",
          (redraw_code, redraw_verdict, redraw_report.get("gate"), redraw_report.get("problems")))

    def write_partial():
        edit_json(f"{folder_of(world, 'pa')}/sweep.json", lambda document: document.update(invalid_count=1))
        raise KeyboardInterrupt("자식이 미완 sweep.json 을 쓰고 끊김")
    batch_case(world, configure_with(after_hooks={"sweep pa verify": write_partial}))
    resume_code, journal, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    sweeps = [(attempt["try"], attempt["exit"], attempt["state"]) for attempt in item_of(journal, "pa")["final"]["attempts"] if attempt["step"] == "sweep"]
    verdict_code, report, _ = run_verdict(world, out_name="h-partial")
    check("개정 H S-2: 미완 sweep.json(invalid_count 1) 뒤 끊김 → interrupted(E1 한 번)로 세고 새 폴더 try 2 로 다시 찍음, verdict 0",
          resume_code == 0 and sweeps == [(1, None, "interrupted"), (2, 0, "done")] and resume_fake.counts.get("sweep pa verify") == 1 and verdict_code == 0,
          (resume_code, sweeps, verdict_code, report.get("problems")))


def test_fix_h_events(world):
    journal_text = journal_text_of(world)
    live = f"{world['live']}/{fluid_tuning_patch.TUNING_FILE_NAME}"
    with restored(live):
        append_byte(live)
        code, journal, _, _ = batch_case(world)
    events = [event["event"] for event in journal["final_batch"]["events"]]
    edit_json(journal_text, lambda document: document["final_batch"].update(outcome=None))
    resume_code, _, resume_fake, _ = batch_case(world, extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="h-events-e2")
    attempt_code, _, _, _ = batch_case(world, configure_with(exits={"compare pa": [2]}))
    edit_json(journal_text, lambda document: document["final_batch"].update(outcome=None, events=[]))
    attempt_verdict, attempt_report, _ = run_verdict(world, out_name="h-attempts-e2")
    check("개정 H S-3(반박 RI S2): 시작 때 정본 sha E2 뒤 일지 outcome 만 지움 → --resume 은 events 로 하위 프로세스 0·종료 2, verdict 는 events 로 stop E2·종료 2. compare 종료 2 뒤 outcome·events 를 다 지워도 attempts 로 E2",
          code == 2 and events == ["E2"] and resume_code == 2 and not resume_fake.calls and verdict_code == 2 and (report.get("stop") or {}).get("event") == "E2"
          and attempt_code == 2 and attempt_verdict == 2 and (attempt_report.get("stop") or {}).get("event") == "E2",
          (code, events, resume_code, verdict_code, report.get("gate"), report.get("stop"), attempt_verdict, attempt_report.get("stop")))


def no_f0_runs(world, spec, overlay):
    for run in spec["runs"]:
        if run["block"] == "F0":
            run["block"] = "F1"
            run.pop("lock")
            run.pop("verify_against")


def two_f0_runs(world, spec, overlay):
    for run in spec["runs"]:
        if run["id"] in ("uw", "f7"):
            run["block"] = "F1"
            run.pop("lock")
            run.pop("verify_against")


def test_fix_h_f0_floor():
    results = {}
    for name, preprocess in (("F0 0개", no_f0_runs), ("F0 2개", two_f0_runs)):
        world = build_world(f"f0-floor-{len(results)}", preprocess)
        expect_code, expect_output = world["setup"][1]
        results[name] = (expect_code, "표본 하한" in expect_output)
    check("개정 H M-C(반박 fixture N1·RI S4): e1 명세 F0 실행 0개·2개 → expect 종료 2(W2 결과 전 관문, 표본 하한 3)",
          all(code == 2 and has_reason for code, has_reason in results.values()), results)
    with patched(verify, f0_shortage_problems=lambda runs, edition: []):
        world = build_world("f0-floor-bypass", no_f0_runs)
    with world_scope(world):
        anchor_code, anchor_output = run_anchor(world)
        batch_code, _, batch_fake, batch_output = batch_case(world)
        simulate_batch(world)
        verdict_code, report, _ = run_verdict(world, out_name="f0-floor")
    check("개정 H M-C: 표본 하한 검사를 건너뛰어 만든 F0 0개 e1 사전 등록 → anchor 2·calib_batch 2(하위 프로세스 0)·verdict 2(gate input)",
          setup_ok(world) and anchor_code == 2 and "표본 하한" in anchor_output and batch_code == 2 and not batch_fake.calls and "표본 하한" in batch_output
          and verdict_code == 2 and report.get("gate") == "input", (setup_detail(world), anchor_code, batch_code, batch_output[-200:], verdict_code, report.get("gate")))


def hand_pin(final_dir, w_anchor_text):
    journal = read_json(read_json(f"{final_dir}/final_queue.json")["final"]["journal"])
    base = {key: read_json(w_anchor_text)[key] for key in verify.W_ANCHOR_BASE_KEYS}
    write_json(w_anchor_text, verify.pinned_w_anchor(base, journal, final_dir))
    commit_w_anchor(w_anchor_text)


def unpin_and_commit(w_anchor_text):
    write_json(w_anchor_text, {key: read_json(w_anchor_text)[key] for key in verify.W_ANCHOR_BASE_KEYS})
    commit_w_anchor(w_anchor_text)


def move_batch_into(world, target):
    absolute(target).mkdir(parents=True, exist_ok=True)
    shutil.move(str(absolute(f"{world['final']}/batch-e1")), str(absolute(f"{target}/batch-e1")))
    shutil.move(str(absolute(f"{world['final']}/locks")), str(absolute(f"{target}/locks")))


def redraw_after_pin(world, name):
    final_dir = world["final"]
    init_code, _, init_fake, init_output = batch_case(world, reset=False)
    unpin_and_commit(world["w_anchor"])
    repin_code, repin_output = pin_by_tool(final_dir, world["w_anchor"])
    hand_pin(final_dir, world["w_anchor"])
    start_code, _, start_fake, start_output = batch_case(world, reset=False)
    with patched(calib_batch, journal_pin_problems=lambda w_anchor, history, journal, final_dir_text: []):
        bypass_code, journal, bypass_fake, _ = batch_case(world, reset=False)
    verdict_code, report, _ = run_verdict(world, out_name=f"i-redraw-{name}")
    return {"pin-journal": (init_code, "관문 pin" in init_output, init_fake.calls), "되돌려 커밋 뒤 pin-journal": (repin_code, "처음 고정된 journal_id" in repin_output),
            "손 고정 뒤 시작": (start_code, "처음 고정된 journal_id" in start_output, start_fake.calls),
            "검사 건너뛴 측정": (bypass_code, "sweep pa verify" in bypass_fake.calls, item_of(journal, "pa")["final"]["adoptions"] if journal else None),
            "verdict": (verdict_code, report.get("gate"), any("처음 고정된 journal_id" in line for line in report.get("problems", []))),
            "손 고정 뒤 시작 문구": start_output[-800:]}


def is_blocked_redraw(result):
    return (result["pin-journal"] == (2, True, []) and result["되돌려 커밋 뒤 pin-journal"] == (2, True) and result["손 고정 뒤 시작"] == (2, True, [])
            and result["검사 건너뛴 측정"] == (0, True, {"2": "pass"}) and result["verdict"] == (2, "journal", True))


def test_fix_i_init_and_pin(world):
    final_dir = world["final"]
    reset_batch(final_dir)
    journal_text = journal_text_of(world)
    init_code, init_fake, init_output = raw_batch(world, ("--init-journal",))
    journal = read_json(journal_text) if absolute(journal_text).is_file() else {"final_batch": {}, "items": []}
    is_unmeasured = journal["final_batch"].get("starts") == [] and all(not item["final"]["attempts"] for item in journal["items"])
    again_code, again_fake, _ = raw_batch(world, ("--init-journal",))
    both_code, both_fake, _ = raw_batch(world, ("--init-journal", "--resume"))
    unpinned_code, unpinned_fake, unpinned_output = raw_batch(world)
    check("개정 I F-1: --init-journal → 종료 0·하위 프로세스 0·journal_id 32자리·starts·attempts 빔(측정 0), 다시 --init-journal → 2, --resume 과 함께 → 2, pin 전 측정 시작 → 2(HEAD w-anchor 에 고정 없음)·하위 프로세스 0",
          init_code == 0 and not init_fake.calls and len(journal["final_batch"].get("journal_id") or "") == 32 and is_unmeasured and again_code == 2 and not again_fake.calls
          and both_code == 2 and not both_fake.calls and unpinned_code == 2 and not unpinned_fake.calls and "고정이 없다" in unpinned_output,
          (init_code, init_output[-200:], again_code, both_code, unpinned_code, unpinned_output[-300:]))
    pin_code, pin_output = pin_by_tool(final_dir, world["w_anchor"])
    pinned = read_json(world["w_anchor"])
    repin_code, repin_output = pin_by_tool(final_dir, world["w_anchor"])
    start_code, start_fake, _ = raw_batch(world)
    restart_code, restart_fake, restart_output = raw_batch(world)
    verdict_code, report, _ = run_verdict(world, out_name="i-pinned")
    check("개정 I F-1: pin-journal → w-anchor = 닻 세 키 + journal_id·restart 0, 다시 pin-journal → 2(이미 고정), 고정 뒤 측정 시작 → 0, 시작한 일지로 --resume 없이 다시 → 2·하위 프로세스 0, verdict 0",
          pin_code == 0 and set(pinned) == {"anchor", "edition", "sha256", "journal_id", "restart"} and pinned["journal_id"] == journal["final_batch"].get("journal_id")
          and pinned["restart"] == 0 and repin_code == 2 and start_code == 0 and start_fake.calls and restart_code == 2 and not restart_fake.calls and "이미 시작" in restart_output
          and verdict_code == 0, (pin_output[-200:], repin_output[-200:], start_code, restart_code, verdict_code, report.get("problems")))
    normal_queue = f"{world['root']}/normal-queue-i.json"
    write_json(normal_queue, {"test_classes": read_json(f"{final_dir}/final_queue.json")["test_classes"],
                              "blocks": [{"id": "K1", "plans": [{"plan": plan_path(world, "pa"), "chain": None, "stages": ["screen"]}]}]})
    fake = FakeFinalTools(world)
    with batch_scope(world, fake):
        normal_code, normal_output = call_main(calib_batch.main, ["calib_batch.py", "--queue", normal_queue, "--holder", "w-normal", "--journal", f"{world['root']}/normal-i/journal.json",
                                                                  "--init-journal"])
    check("개정 I F-1: 일반 모드(mode 없음)에서 --init-journal → 종료 2·하위 프로세스 0", normal_code == 2 and not fake.calls and "최종 모드" in normal_output, normal_output[-200:])


def test_fix_i_redraw(world):
    final_dir, root = world["final"], world["root"]
    results = {}
    for name, target in (("aborted", f"{final_dir}/aborted-20261106-010000"), ("r2", f"{final_dir}/r2/old"), ("out", f"{root}/moved-all")):
        code, _, _, _ = batch_case(world, configure_with(sweeps={"pa": [{"states": {"2": "fail"}}]}))
        first_verdict, first_report, _ = run_verdict(world, out_name=f"i-first-{name}")
        move_batch_into(world, target)
        result = redraw_after_pin(world, name)
        result["첫 실행"] = (code, first_verdict, decisions_of(first_report).get("2"))
        results[name] = result
        shutil.rmtree(absolute(target), ignore_errors=True)
        shutil.rmtree(absolute(f"{final_dir}/r2"), ignore_errors=True)
    check("개정 I F-1(반박 적합 M-1 N1·N2·RI M1'·P-42): 채택 2 fail 을 본 뒤 batch-e1·locks 를 aborted-*·r2/·final_dir 밖으로 옮기고 새 일지 → pin-journal 2(이미 고정), 고정을 지워 커밋해도 pin-journal 2, 손으로 고정·커밋 → calib_batch 2·하위 프로세스 0, 그 검사까지 건너뛰고 측정(pass) → verdict 2(gate journal, git 에 처음 고정된 id 아님)",
          all(result["첫 실행"] == (0, 1, "user_decision") and is_blocked_redraw(result) for result in results.values()), results)


def test_fix_i_restart_pin(world):
    final_dir = world["final"]
    code, _, _, _ = batch_case(world, configure_with(tamper_compare={("f2", "verify-1"): tamper_f2}))
    archive = archive_e1(world)
    expect_code, _ = rerun_expect(world)
    anchor_code, _ = run_anchor(world)
    restart_code, _, _, restart_output = batch_case(world, configure_with(sweeps={"pa": [{"states": {"2": "fail"}}]}))
    pinned = read_json(world["w_anchor"])
    first_verdict, first_report, _ = run_verdict(world, out_name="i-restart-first")
    check("개정 I F-1 재시작: E3 → 기록 폴더·expect·anchor restart 1 → --init-journal·pin-journal(restart 1·archive·e3_evidence = 기록 폴더 옛 일지) → 측정 0, verdict 1(채택 2 user_decision)",
          code == 1 and expect_code == 0 and anchor_code == 0 and restart_code == 0 and pinned.get("restart") == 1 and pinned.get("archive") == archive
          and pinned.get("e3_evidence") == f"{archive}/batch-e1/journal.json" and first_verdict == 1 and decisions_of(first_report).get("2") == "user_decision",
          (code, expect_code, anchor_code, restart_code, restart_output[-300:], pinned, first_verdict, first_report.get("problems")))
    old_journal = f"{archive}/batch-e1/journal.json"
    with restored(old_journal):
        edit_json(old_journal, lambda document: document["final_batch"].update(journal_id="0" * 32))
        swapped_code, _, swapped_fake, swapped_output = batch_case(world, extra=("--resume",), reset=False)
        swapped_verdict, swapped_report, _ = run_verdict(world, out_name="i-restart-swapped")
    GIT["history"] = [entry for entry in fake_w_anchor_history(world["w_anchor"]) if verify.pin_round(entry["content"]) != ("e1", 0)]
    order_code, _, order_fake, order_output = batch_case(world, extra=("--resume",), reset=False)
    GIT["history"] = None
    check("개정 I F-1 재시작 순서·증거: 기록 폴더 옛 일지 journal_id 를 바꿔 넣음 → --resume 2·verdict 2(gate journal), git 이력에 e1 restart 0 고정이 없음 → --resume 2(모두 하위 프로세스 0)",
          swapped_code == 2 and not swapped_fake.calls and "옛 일지 journal_id" in swapped_output and swapped_verdict == 2 and swapped_report.get("gate") == "journal"
          and order_code == 2 and not order_fake.calls and "순서" in order_output, (swapped_output[-300:], swapped_report.get("problems"), order_output[-300:]))
    move_batch_into(world, f"{archive}/again")
    result = redraw_after_pin(world, "restart")
    check("개정 I F-1(반박 적합 N1r·RI n4): restart 1 결과를 본 뒤 batch-e1·locks 를 하나뿐인 기록 폴더 안 again/ 으로 옮기고 새 일지 → pin-journal 2, 고정을 지워 커밋해도 2, 손으로 고정 → calib_batch 2(처음 고정 아님·재시작 고정 2개), 검사를 건너뛴 측정 → verdict 2(gate journal)",
          is_blocked_redraw(result) and "2개 이상" in result["손 고정 뒤 시작 문구"], result)


def crash_before_gate(runner):
    raise KeyboardInterrupt("F0 마지막 항목 저장 뒤·관문 기록 전 끊김")


def test_fix_i_f0_gate_recompute(world):
    with patched(calib_batch.FinalBatchRunner, pass_f0_gate=crash_before_gate):
        code, _, _, _ = batch_case(world, configure_with(sweeps=SHORTFALL_SWEEPS))
    first_resume, _, _, _ = batch_case(world, configure_with(exits={"ensure": [1]}), extra=("--resume",), reset=False)
    second_resume, journal, _, _ = batch_case(world, configure_with(exits={"ensure": [1]}), extra=("--resume",), reset=False)
    verdict_code, report, _ = run_verdict(world, out_name="i-n3")
    check("개정 I F-2(반박 적합 M-2 N3): F0 마지막 항목 뒤·관문 기록 전 끊김 → 재개 시작 연속 2회 실패(start_failed, f0_gate null) → verdict 가 산출물로 관문을 다시 계산(표본 2 < 3) → stop F0_shortfall·전 채택 incomplete·종료 1",
          code == "interrupted" and (first_resume, second_resume) == (3, 3) and outcome_of(journal).get("event") == "start_failed" and journal["final_batch"]["f0_gate"] is None
          and verdict_code == 1 and (report.get("stop") or {}).get("event") == "F0_shortfall" and set(decisions_of(report).values()) == {"incomplete"}
          and len(decisions_of(report)) == 12, (code, first_resume, second_resume, outcome_of(journal), verdict_code, report.get("stop"), decisions_of(report)))
    with patched(calib_batch.FinalBatchRunner, pass_f0_gate=crash_before_gate):
        batch_case(world, configure_with(sweeps=SHORTFALL_SWEEPS))
    live = f"{world['live']}/{fluid_tuning_patch.TUNING_FILE_NAME}"
    with restored(live):
        append_byte(live)
        e2_code, journal, _, _ = batch_case(world, extra=("--resume",), reset=False)
    e2_verdict, e2_report, _ = run_verdict(world, out_name="i-n3b")
    check("개정 I F-2(반박 적합 N3b): 같은 끊김 뒤 재개 시작 E2(정본 sha) → verdict 2·settled 빔·전 채택 incomplete",
          e2_code == 2 and outcome_of(journal).get("event") == "E2" and e2_verdict == 2 and e2_report.get("settled") == {} and len(e2_report.get("incomplete", [])) == 12,
          (e2_code, e2_verdict, sorted(e2_report.get("settled") or {}), len(e2_report.get("incomplete", []))))
    batch_case(world, configure_with(interrupts={"sweep uw verify": 1}))
    open_verdict, open_report, _ = run_verdict(world, out_name="i-f0-open")
    check("개정 I F-2: F0 가 끝나지 않은 채 끊긴 일지(사건 없음) → 관문 미결이라 F0_shortfall 이 아니라 verdict 2(gate terminal, --resume 대상)",
          open_verdict == 2 and open_report.get("gate") == "terminal" and open_report.get("stop") is None, (open_verdict, open_report.get("gate"), open_report.get("stop")))


def crash_after_attempt(step, key):
    original = calib_batch.FinalBatchRunner.run_attempt
    state = {"done": False}

    def crashing(runner, item, attempt_step, *arguments, **keywords):
        attempt, tail = original(runner, item, attempt_step, *arguments, **keywords)
        if attempt_step == step and runner.item_id(item) == key and not state["done"]:
            state["done"] = True
            raise KeyboardInterrupt(f"{key} {step} 시도 끝 저장 뒤·항목 sha 저장 전 끊김")
        return attempt, tail
    return patched(calib_batch.FinalBatchRunner, run_attempt=crashing)


def crash_before_tool(plan_stem, label_prefix):
    original = calib_batch.FinalBatchRunner.item_tool
    state = {"done": False}

    def crashing(runner, item, label, command, log_path, timeout=calib_batch.TOOL_TIMEOUT_SECONDS):
        if not state["done"] and runner.plan_name(item) == plan_stem and label.startswith(label_prefix):
            state["done"] = True
            raise KeyboardInterrupt(f"시도 저장 뒤·로그·폴더 생성 전 끊김: {plan_stem} {label}")
        return original(runner, item, label, command, log_path, timeout)
    return patched(calib_batch.FinalBatchRunner, item_tool=crashing)


def test_fix_i_recorded_outputs(world):
    code, journal, _, _ = batch_case(world)
    ended = [attempt for item in journal["items"] for attempt in item["final"]["attempts"] if attempt["step"] in ("sweep", "official", "compare", "judge", "check_ab")]
    check("개정 I F-3: sweep·official·compare·judge·check_ab 시도는 끝날 때 산출물 경로·sha(output·output_sha256)를 종료 코드와 같은 저장에 적는다",
          code == 0 and ended and all(attempt.get("output") and attempt.get("output_sha256") == sha(attempt["output"]) for attempt in ended),
          [(attempt["step"], attempt.get("output"), attempt.get("output_sha256")) for attempt in ended][:3])
    sweep_text = f"{folder_of(world, 'pg')}/sweep.json"
    results = {}
    for name, mutate in (("지움", lambda: absolute(sweep_text).unlink()), ("invalid_count 1", lambda: edit_json(sweep_text, lambda document: document.update(invalid_count=1)))):
        batch_case(world, configure_with(interrupts={"official g13": 1}))
        mutate()
        resume_code, _, resume_fake, resume_output = batch_case(world, extra=("--resume",), reset=False)
        verdict_code, report, _ = run_verdict(world, out_name=f"i-recorded-{len(results)}")
        results[name] = (resume_code, resume_fake.calls, "sha 를 기록" in resume_output, verdict_code, report.get("gate"))
    check("개정 I F-3(반박 RI n2·n3 의 '시도가 끝난 뒤' 꼴): 끝난 sweep 의 sweep.json 을 지우거나 invalid_count 를 고친 뒤 --resume → 2·하위 프로세스 0, verdict 2(gate artifacts)",
          all(value[0] == 2 and not value[1] and value[2] and value[3] == 2 and value[4] == "artifacts" for value in results.values()), results)
    with crash_after_attempt("compare", "pa"):
        crash_code, _, _, _ = batch_case(world)
    compare_text = f"{folder_of(world, 'pa')}/compare.json"
    append_byte(compare_text)
    window_code, _, window_fake, window_output = batch_case(world, extra=("--resume",), reset=False)
    window_verdict, window_report, _ = run_verdict(world, out_name="i-window")
    check("개정 I F-3(반박 fixture B1 창): compare 시도 끝 저장 뒤·항목 sha 저장 전 끊김 → compare.json 1바이트 → --resume 2·하위 프로세스 0, verdict 2(gate artifacts)",
          crash_code == "interrupted" and window_code == 2 and not window_fake.calls and "sha 를 기록" in window_output and window_verdict == 2
          and window_report.get("gate") == "artifacts", (crash_code, window_code, window_output[-300:], window_verdict, window_report.get("gate")))


def test_fix_i_folder_reuse(world):
    results = {}
    for plan_stem, key in (("pa", "pa"), ("pg", "g13")):
        with crash_before_tool(plan_stem, "sweep verify first"):
            crash_code, _, _, _ = batch_case(world)
        folder_exists = absolute(folder_of(world, plan_stem)).exists()
        resume_code, journal, _, _ = batch_case(world, extra=("--resume",), reset=False)
        verdict_code, report, _ = run_verdict(world, out_name=f"i-x1-{key}")
        sweeps = [(attempt["try"], attempt["exit"], attempt["state"], attempt["folder"].rsplit("/", 1)[-1]) for attempt in item_of(journal, key)["final"]["attempts"]
                  if attempt["step"] == "sweep"]
        results[key] = (crash_code, folder_exists, resume_code, sweeps, verdict_code, report.get("gate"))
    expected = [(1, None, "interrupted", "verify-1"), (2, 0, "done", "verify-2")]
    check("개정 I F-4(반박 fixture X1): sweep 시도 저장 뒤·로그·폴더 생성 전 끊김 → --resume 의 try 2 는 일지에 적힌 verify-1 을 피해 verify-2 로 찍고, verdict 0(정상 일지를 거부하지 않음)",
          all(value[0] == "interrupted" and value[1] is False and value[2] == 0 and value[3] == expected and value[4] == 0 for value in results.values()), results)


def real_state():
    final = absolute(REAL_FINAL_DIR)
    files = sorted(str(path.relative_to(final)) for path in final.rglob("*") if path.is_file()) if final.is_dir() else []
    return {"tuning": prereg_lock.file_sha256(fluid_tuning_patch.canonical_tuning_path()), "look": prereg_lock.file_sha256(fluid_tuning_patch.canonical_look_path()),
            "overlay": sha(REAL_OVERLAY), "final": files, "w_anchor": absolute(verify.DEFAULT_W_ANCHOR).is_file()}


def run_all():
    base = build_world("base")
    with world_scope(base):
        test_preparation_commands(base)
        simulate_batch(base)
        test_compare(base)
        test_self_check_rules(base)
        test_replay_and_check_ab(base)
        test_verdict_clean(base)
        test_gate_anchor(base)
    test_decision_tables()
    test_r2_targets(base)
    scenario_world = build_world("scenario")
    with world_scope(scenario_world):
        test_verdict_scenarios(scenario_world)
        test_rerun_mismatch(scenario_world)
        test_tests_and_approval(scenario_world)
    for name, function in (("chain15", test_r2_chain), ("chain18", test_fog_chain), ("crafted", test_crafted_rollbacks), ("restart", test_restart),
                           ("restart-lie", test_restart_without_mismatch)):
        world = build_world(name)
        with world_scope(world):
            function(world)
    extra_world = build_world("extra-metric", add_metric_to_source)
    with world_scope(extra_world):
        test_extra_metric_and_judge_file(extra_world)
    test_w_expect_real()
    test_r2_targets_covered_child(base)
    batch_world = build_world("batch")
    with world_scope(batch_world):
        check("준비: calib_batch 최종 모드 fixture 의 copies·expect·anchor 가 종료 0", setup_ok(batch_world), setup_detail(batch_world))
        for function in (test_batch_clean, test_batch_verify_flow, test_batch_e2_matrix, test_batch_start, test_batch_resume, test_batch_f0_gate, test_artifact_gate_outputs):
            function(batch_world)
    restart_world = build_world("batch-restart")
    with world_scope(restart_world):
        test_batch_restart(restart_world)
    fix_world = build_world("batch-fix-h")
    with world_scope(fix_world):
        for function in (test_fix_h_f0_gate, test_fix_h_redraw, test_fix_h_journal_damage, test_fix_h_recovered_sweep, test_fix_h_events):
            function(fix_world)
    test_fix_h_f0_floor()
    fix_i_world = build_world("batch-fix-i")
    with world_scope(fix_i_world):
        for function in (test_fix_i_init_and_pin, test_fix_i_redraw, test_fix_i_f0_gate_recompute, test_fix_i_recorded_outputs, test_fix_i_folder_reuse):
            function(fix_i_world)
    fix_i_restart_world = build_world("batch-fix-i-restart")
    with world_scope(fix_i_restart_world):
        test_fix_i_restart_pin(fix_i_restart_world)
    for name, verify_plan, ab_plan, fog_fix, function in (("batch-r2-kept", {"f2": [{"states": {"15": "fail"}}]}, {"f2wab": {"screen": "best", "confirm": "v01"}}, False, test_batch_r2_kept),
                                                          ("batch-r2-fog", {"f7": [{"states": {"18": "fail"}}]}, {"f7wab": {"screen": "best", "confirm": "v01"}}, True, test_batch_r2_fog)):
        world = prepare_r2_batch_world(name, verify_plan, ab_plan, fog_fix)
        with world_scope(world):
            function(world)


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    shutil.rmtree(absolute(TEST_ROOT), ignore_errors=True)
    before = real_state()
    git_fakes = patched(verify, anchor_commit_problems=lambda path: list(GIT["problems"]), anchor_commit_time=lambda path: prereg_lock.parse_moment(GIT["time"]),
                        w_anchor_history=fake_w_anchor_history)
    batch_git_fakes = patched(calib_batch, anchor_commit_problems=lambda path: list(GIT["problems"]), anchor_commit_time=lambda path: prereg_lock.parse_moment(GIT["time"]),
                              w_anchor_history=fake_w_anchor_history)
    with git_fakes, batch_git_fakes:
        try:
            run_all()
        except Exception as error:
            check("하네스 예외 없이 끝남", False, f"{type(error).__name__}: {error}")
            traceback.print_exc()
    check("실행 전후 실제 정본·accepted 덧씌움·Saved/FluidFX/calib/final·w-anchor 불변", real_state() == before, (before, real_state()))
    passed = sum(1 for _, condition in RESULTS if condition)
    print(f"SUMMARY passed={passed}/{len(RESULTS)}")
    return 0 if passed == len(RESULTS) else 1


if __name__ == "__main__":
    sys.exit(main())

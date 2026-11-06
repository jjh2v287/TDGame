"""실행 환경: 시스템 Python 3.12(표준 라이브러리만, git hash-object 사용, 에디터 불필요). runbook-p-v3 T3.10 fixture 실행기: 임시 폴더(tempfile)에 가짜 LG v2·p_areas.json·0e/W11·expect 입력·대역 pie_profile_fluid.py(--check-plan·build gate만, FX7P_DEFAULTS는 진짜)·prereg_lock.py 복사본·가짜 실행 폴더(Saved/FluidFX/fx7p, merge는 진짜 PP merge_fx7p_plan 원문)·가짜 T2 판정 출력으로 저장소 사본을 만들고 p_plans.py build·current-lock·lock-list·preflight·runs·need-ladder·select-sp·status·summary 사례를 돌린다.
실행 명령: python -B Tools/FluidFX/perf/p/check_p_fixtures.py [--root <저장소 사본>](시험할 p_plans.py·prereg_lock.py·pie_profile_fluid.py FX7P_DEFAULTS가 있는 루트, 기본 이 파일의 저장소)
출력: 사례마다 '통과|실패 <이름>: <근거>' 한 줄과 끝 'P fixture 통과 <n>/<m>'(표준 출력). 끝에서 <root>/Tools/FluidFX/perf/p/fixtures_t1·t2·t4.py가 있으면 하위 프로세스로 돌리고 그 출력은 표준 오류. 종료 0 = 사례 전부 통과·하위 fixture 모두 0, 1 = 그 밖
상태: 2026-11-06 T3.10 사례 전부(build·current-lock·lock-list·preflight·runs·need-ladder·select-sp·status·summary), 반박 r1 M1(compare 하나를 함께 쓰는 채택의 장면 귀속)·M2(PB2:214 열 빠짐 입력 경로) 사례 추가, 2026-11-07 AM7 L1(재잠금 뒤 session 없는 폴더)·L2(T2 실제 열 결함 형식)·L3(pb2_fxp_sources) 사례 추가, 저장소 파일은 쓰지 않는다.
"""
import argparse
import ast
import contextlib
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[4]
LEDGER_REL = "Saved/AgentOps/20261106/p-prep/p-ledger-v2.md"
T_REL = "Tools/FluidFX/perf/p"
AREAS_REL = f"{T_REL}/p_areas.json"
LOCKS_REL = "Saved/AgentOps/20261106/p/locks"
P_REL = "Saved/AgentOps/20261106/p"
VERDICT_REL = "Saved/FluidFX/calib/final/verdict-w11.json"
EXPECT_REL = "Saved/FluidFX/calib/final/expect.json"
CLOUD_STAGE_LEVEL = "/Game/FluidFX/Maps/LV_TDFluidCloudStage"
MUSHROOM_COMPARE_REL = "Saved/FluidFX/calib/final/batch-e1/runs/cloud_g9_mushroom-p20/verify-1/compare.json"
MUSHROOM_SCENES = ("mushroom_cap", "mushroom_shield")
HOLDER = "p-perf"
SUB_FIXTURES = ("fixtures_t1.py", "fixtures_t2.py", "fixtures_t4.py")
CAMERA_STEP = {"action": "camera", "target_label": "TDGen_FixtureCam"}

ITEMS = [
    ("S001", "PF:1", "불꽃 카드", "≤0.3", "fire", "행 FI1/ft(몫)", "행", ["FI1/ft"], None),
    ("S002", "PF:2", "모닥불", "≤2.0", "fire", "행 FI1/gt", "행", ["FI1/gt"], None),
    ("S003", "PF:3", "불 RT", "≤0.15", "fire", "행 FI1/rt", "행", ["FI1/rt"], None),
    ("S004", "PF:4", "불티 ISM", "≤0.15", "fire", "행 FI5a/ft·FI1/ft(몫)", "행", ["FI5a/ft", "FI1/ft"], None),
    ("S005", "PF:5", "연기 SOFT", "≤4.0", "smoke", "행 SM2/ft", "행", ["SM2/ft"], None),
    ("S006", "PF:6", "hv2 효과", "≤6.0", "smoke", "행 HV2/ft", "행", ["HV2/ft"], None),
    ("S007", "PF:7", "FVOL", "≤0.8", "fog", "행 FO1/ft(몫)", "행", ["FO1/ft"], None),
    ("S008", "PF:8", "근거리 대안", "≤4.0", "fog", "행 FO3/ft(SM2/ft 공유, SVOL SOFT 대안, B17)", "행", ["FO3/ft"], None),
    ("S009", "PF:9", "젖음 패스", "≤0.15", "water", "행 WA3/gt", "행", ["WA3/gt"], None),
    ("S010", "PF:10", "구름 층", "≤2.5", "cloud", "행 CL1/ft", "행", ["CL1/ft"], None),
    ("S011", "PF:11", "층 추가", "≤0.3", "cloud", "행 CL1/ft 공유(층 차분)", "행 공유", ["CL1/ft"], None),
    ("S012", "PF:12", "스플랫", "≤0.5", "input", "행 IN1/ft", "행", ["IN1/ft"], None),
    ("S013", "PF:13", "페인터", "≤0.10", "track", "행 TR1/ft·TR1/ng", "행", ["TR1/ft", "TR1/ng"], None),
    ("S014", "PF:14", "점성", "≤0.15", "viscous", "행 VI1/ng", "행", ["VI1/ng"], None),
    ("S015", "PF:15", "메모리", "각", "각", "측정 불가(메모리 경로 없음)", "측정 불가", [], "메모리 경로 없음"),
    ("S016", "PF:16", "GPU 경로", "≤0.2", "smoke", "출하 밖(출하 프리셋 CPU)", "출하 밖", [], "출하 프리셋 CPU"),
    ("S017", "PF:17", "VSM 열", "—", "fire", "기록(FI1 장면 VSM 열)", "기록", [], "FI1 장면 VSM 열"),
    ("S018", "PF:18", "GPU 256² 안개", "≤0.15", "fog", "행 FO1/ft(몫)", "행", ["FO1/ft"], None),
]

FI1_TOGGLE = "L: B sleep 2 + Fire[cards,embers]·Smoke[volumes] 끔 + light(TDGen_CampfireSmoke) 끔"
FO1_TOGGLE = "setup `r.VolumetricFog 1`; L+X: B sleep 1(FogAmbient) + [volumes] 끔 + X(PassiveField {hidden true})"
CL1_TOGGLE = "setup CloudSetup 6·camera TDGen_CloudCam_Volcano; X(NS 조건 없음)"
LEDGER_ROWS = [
    ("FI1/ft", "fire", "~FireStage", "FI1", "TDGen_CampfireFire·TDGen_CampfireSmoke", FI1_TOGGLE, "A", "카드 0.3·S001; ISM 불티 0.15·S004; CPU 시뮬 2(ft 몫 없음)",
     "ft", "CPU 128²(TJ:1)+CPU 96²(TJ:2)", "0.45 = 0.3+0.15(캡슐 미포함 표기)", "BC(A)", "S001·S004"),
    ("FI1/gt", "fire", "~FireStage", "FI1", "TDGen_CampfireFire·TDGen_CampfireSmoke", "=FI1/ft", "A", "모닥불 CPU 2.0·S002", "gt", "CPU 128²+96²", "2.0 = 2.0", "BC(A)", "S002"),
    ("FI1/rt", "fire", "~FireStage", "FI1", "TDGen_CampfireFire·TDGen_CampfireSmoke", "=FI1/ft", "A", "불 RT 0.15·S003", "rt", "CPU 128²+96²", "0.15 = 0.15", "BC(A)", "S003"),
    ("FI5a/ft", "fire", "~EmberStage", "FI5a(E-A)", "TDGen_EmberA_Campfire(ISM)", "Lb: B [embers] 끔", "both", "ISM 불티 0.15·S004", "ft", "CPU 128²(TJ:3)", "0.15 = 0.15",
     "BC(both)·Lb 부품 가시 되읽기", "S004"),
    ("SM2/ft", "smoke", "~SmokeStage", "SM2", "TDGen_SmokeSoft", "L: B sleep + [volumes] 끔", "A", "포인트 SOFT 4.0·S005", "ft", "CPU 256²(TJ:4)", "4.0 = 4.0", "BC(A)", "S005·S008"),
    ("HV2/ft", "smoke", "~HvolStage", "HV2(구역1 VortexPair)", "TDGen_HvolVortexA·TDGen_HvolVortexB", "Ls+X: B sleep 2 + X {hidden true} 2", "A", "hv2 효과(시뮬 2·HVOL 2) 6.0·S006",
     "ft", "CPU 128²+128²(TJ:5; HVOL 256×256×128)", "6.0 = 6.0(효과 전체 값 [가정])", "BC(A)·TX", "S006"),
    ("FO1/ft", "fog", "~FogStage", "FO1(AmbientWindowAndField)", "TDGen_FogAmbient·TDGen_FogPassiveField(정적 상자)", FO1_TOGGLE, "A", "FVOL 0.8·S007; GPU 256² 시뮬 0.15·S018",
     "ft", "GPU 256²(TJ:6)", "0.95 = 0.8+0.15", "BG(A)·TX", "S007·S018"),
    ("FO3/ft", "fog", "없음(파생)", "없음(SM2 파생, B17)", "TDGen_SmokeSoft(SM2)", "없음", "A(SM2)", "SVOL SOFT 대안 4.0·S008", "ft(파생)", "CPU 256²(SM2)",
     "4.0 = SM2/ft 한도. SM2/ft 통과 → 통과", "SM2/ft 증거", "S008"),
    ("WA3/gt", "water", "~CoastStage", "WA3", "TDGen_CoastStageWaterSea", "C `TD.FluidFX.WaterWetness` A 1 / B 0", "both", "젖음 패스 0.15·S009", "gt", "CPU 192²(TJ:7)", "0.15 = 0.15",
     "EC WaterWetness·B4 (ii) CPU", "S009"),
    ("CL1/ft", "cloud", "~CloudStage", "CL1(volcano_s2)", "TDGen_CloudLayers", CL1_TOGGLE, "—", "구름 층 전체 2.5·S010", "ft", "없음", "2.5 = 2.5", "TX", "S010·S011"),
    ("IN1/ft", "input", "~MassInputStage", "IN1(M1)", "TDGen_MassBench10k", "C `TD.FluidFX.MassInput` A -1 / B 0", "both", "스플랫 1만 0.5·S012", "ft", "GPU 512²(TJ:8)", "0.5 = 0.5",
     "EC SplatCalls·B4 (ii) GPU", "S012"),
    ("TR1/ft", "track", "~SnowStage", "TR1(snow)", "TDGen_SnowTrack(변형 메시 없음)", "Ls", "A", "페인터 1024² 0.10·S013", "ft", "GPU 1024²(TJ:9)", "0.10 = 0.10", "BG(A)", "S013"),
    ("TR1/ng", "track", "~SnowStage", "TR1", "TDGen_SnowTrack", "=TR1/ft", "A", "페인터 1024² 0.10·S013", "ng", "GPU 1024²", "0.10 = 0.10", "BG(A)", "S013"),
    ("VI1/ng", "viscous", "~ViscousStage", "VI1(구역1)", "TDGen_ViscousBloodSlide", "Ls", "A", "점성 256² 0.15·S014", "ng", "GPU 256²(TJ:10)", "0.15 = 0.15", "BG(A)", "S014"),
]
CVAR_ROWS = [("TD.FluidFX.WaterWetness", "1", 1), ("TD.FluidFX.MassInput", "-1", -1), ("TD.FluidFX.CloudSetup", "-1", -1), ("r.VolumetricFog", "1", 1),
             ("t.OverrideFPS", "0.0", 0.0), ("r.ScreenPercentage", "100", 100)]


def ledger_text():
    lines = ["# 시험 장부(fixture)", "", "## 1. 출처 장부", "", "| ID | 출처(파일:줄) | 요지 | 한도 | 영역 | 처분 |", "|---|---|---|---|---|---|"]
    lines += [f"| {item[0]} | {item[1]} | {item[2]} | {item[3]} | {item[4]} | {item[5]} |" for item in ITEMS]
    lines += ["", "## 2. 행 표", "", "**토글:** fixture.", "",
              "| 행 ID | 영역 | 맵 | 장면 ID | 대상 라벨 | 토글 | aw | parts_off | 지표 | backend·grid | 한도 ms(조성식) | 증거 | 출처 줄 ID |",
              "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    lines += ["| " + " | ".join(row) + " |" for row in LEDGER_ROWS]
    lines += ["", "## 3. 몫 표", "", "fixture.", "", "## 4. 콘솔 변수", "", "| CVar | 확정 기본값 | 인용 줄 |", "|---|---|---|"]
    lines += [f"| {name} | {text} | fixture |" for name, text, _ in CVAR_ROWS]
    lines += ["", "- **기록만:** SolverBackend 0(BS:1) · WaterGpu 0(BS:2) · LogTickCost 0(LS:1).", "", "## 5. 증거 열", "", "| 쓰임 | 후보 이름 목록 | 종류 |", "|---|---|---|",
              "| WA3 EC | `TDFluid/GameThread/WaterWetness`, `TDFluid/AllWorkers/WaterWetness` | 시간 |", "| IN1 EC | `TDFluid/MassInputSplatCalls` | 사용자 |",
              "| L·Ls·Lb 토글 | `TDFluid/StepCount` | 사용자 |", "", "## 6. 독립 대조", "", "**독립 대조 2026-11-06: 누락 0 → 처분 추가**(fixture)",
              "- SW 범위 안 남은 누락 0(fixture).", "", "## 7. 개수 요약", "", "fixture.", ""]
    return "\n".join(lines)


def sleep_step(awake):
    return {"action": "sleep", "awake": awake}


def visibility_step(labels, parts, visible):
    return {"action": "visibility", "targets": labels, "parts": parts, "visible": visible}


def actor_step(labels, **properties):
    return {"action": "actor", "labels": labels, **properties}


def console_step(commands, restore=None):
    step = {"action": "console", "commands": commands}
    if restore is not None:
        step["restore"] = restore
    return step


def make_row(row_id, metric, parts, limit, formula, items, note=None):
    return {"id": row_id, "metric": metric, "parts_off": [{"part": part, "ms": ms, "item": item} for part, ms, item in parts], "limit_ms": limit,
            "limit_formula": formula, "items": items, "note": note}


def make_scene(scene_id, area, stage, targets, toggle, summary, sides, backend, grid, basis, setup, a, b, evidence, rows, block_frames=None, water_layer=False, count=None):
    return {"id": scene_id, "area": area, "level": f"/Game/FluidFX/Maps/LV_TDFluid{stage}", "targets": targets, "toggle": toggle, "toggle_summary": summary,
            "awake_sides": sides, "awake_step_count": 0 if sides == "none" else len(targets), "backend": backend, "grid": grid, "backend_basis": basis,
            "water_layer": water_layer, "block_frames": block_frames, "camera_source": "fixture 카메라", "hash_paths": [f"Content/FluidFX/Fixture/NS_Fixture_{scene_id}.uasset"],
            "steps": {"setup": setup, "a": a, "b": b}, "evidence": evidence, "meta": {"count": count if count is not None else len(targets), "note": None}, "rows": rows}


def evidence(step_count=False, columns=None, parts=None, light=False, console=None, actor=None):
    return {"step_count": step_count, "columns": columns or [], "parts": parts or {}, "light": light, "console": console or {}, "actor": actor}


def fixture_scenes():
    fire, smoke = ["TDGen_CampfireFire", "TDGen_CampfireSmoke"], ["TDGen_HvolVortexA", "TDGen_HvolVortexB"]

    def fi1_side(visible):
        return [sleep_step(visible), visibility_step(["TDGen_CampfireFire"], ["cards", "embers"], visible), visibility_step(["TDGen_CampfireSmoke"], ["volumes"], visible),
                {"action": "light", "targets": ["TDGen_CampfireSmoke"], "visible": visible}]

    return [
        make_scene("FI1", "fire", "FireStage", fire, "L", FI1_TOGGLE, "A", "CPU", {"TDGen_CampfireFire": 128, "TDGen_CampfireSmoke": 96}, "CPU 128²(TJ:1)+CPU 96²(TJ:2)",
                   [dict(CAMERA_STEP)], fi1_side(True), fi1_side(False),
                   evidence(True, parts={"TDGen_CampfireFire": ["cards", "embers"], "TDGen_CampfireSmoke": ["volumes"]}, light=True),
                   [make_row("FI1/ft", "ft", [("카드", 0.3, "S001"), ("ISM 불티", 0.15, "S004")], 0.45, "0.3+0.15", ["S001", "S004"], "캡슐 미포함 표기"),
                    make_row("FI1/gt", "gt", [("모닥불 CPU", 2.0, "S002")], 2.0, "2.0", ["S002"]),
                    make_row("FI1/rt", "rt", [("불 RT", 0.15, "S003")], 0.15, "0.15", ["S003"])], block_frames=1200),
        make_scene("FI5a", "fire", "EmberStage", ["TDGen_EmberA_Campfire"], "Lb", "Lb: B [embers] 끔", "both", "CPU", {"TDGen_EmberA_Campfire": 128}, "CPU 128²(TJ:3)",
                   [sleep_step(True), dict(CAMERA_STEP)], [visibility_step(["TDGen_EmberA_Campfire"], ["embers"], True)],
                   [visibility_step(["TDGen_EmberA_Campfire"], ["embers"], False)], evidence(True, parts={"TDGen_EmberA_Campfire": ["embers"]}),
                   [make_row("FI5a/ft", "ft", [("ISM 불티", 0.15, "S004")], 0.15, "0.15", ["S004"])]),
        make_scene("SM2", "smoke", "SmokeStage", ["TDGen_SmokeSoft"], "L", "L: B sleep + [volumes] 끔", "A", "CPU", {"TDGen_SmokeSoft": 256}, "CPU 256²(TJ:4)",
                   [dict(CAMERA_STEP)], [sleep_step(True), visibility_step(["TDGen_SmokeSoft"], ["volumes"], True)],
                   [sleep_step(False), visibility_step(["TDGen_SmokeSoft"], ["volumes"], False)], evidence(True, parts={"TDGen_SmokeSoft": ["volumes"]}),
                   [make_row("SM2/ft", "ft", [("포인트 SOFT", 4.0, "S005")], 4.0, "4.0", ["S005", "S008"])]),
        make_scene("HV2", "smoke", "HvolStage", smoke, "Ls+X", "Ls+X: B sleep 2 + X {hidden true} 2", "A", "CPU", {"TDGen_HvolVortexA": 128, "TDGen_HvolVortexB": 128},
                   "CPU 128²+128²(TJ:5; HVOL 256×256×128)", [dict(CAMERA_STEP)], [sleep_step(True), actor_step(smoke, hidden=False)],
                   [sleep_step(False), actor_step(smoke, hidden=True)], evidence(True, actor={"hidden": True, "niagara": False}),
                   [make_row("HV2/ft", "ft", [("hv2 효과", 6.0, "S006")], 6.0, "6.0", ["S006"], "효과 전체 값 [가정]")]),
        make_scene("FO1", "fog", "FogStage", ["TDGen_FogAmbient"], "L+X", FO1_TOGGLE, "A", "GPU", {"TDGen_FogAmbient": 256}, "GPU 256²(TJ:6)",
                   [console_step(["r.VolumetricFog 1"], True), dict(CAMERA_STEP)],
                   [sleep_step(True), visibility_step(["TDGen_FogAmbient"], ["volumes"], True), actor_step(["TDGen_FogPassiveField"], hidden=False)],
                   [sleep_step(False), visibility_step(["TDGen_FogAmbient"], ["volumes"], False), actor_step(["TDGen_FogPassiveField"], hidden=True)],
                   evidence(True, parts={"TDGen_FogAmbient": ["volumes"]}, actor={"hidden": True, "niagara": False}),
                   [make_row("FO1/ft", "ft", [("FVOL", 0.8, "S007"), ("GPU 256² 시뮬", 0.15, "S018")], 0.95, "0.8+0.15", ["S007", "S018"])]),
        make_scene("WA3", "water", "CoastStage", ["TDGen_CoastStageWaterSea"], "C", "C `TD.FluidFX.WaterWetness` A 1 / B 0", "both", "CPU", {"TDGen_CoastStageWaterSea": 192},
                   "CPU 192²(TJ:7)", [dict(CAMERA_STEP)], [console_step(["TD.FluidFX.WaterWetness 1"])], [console_step(["TD.FluidFX.WaterWetness 0"])],
                   evidence(columns=["TDFluid/GameThread/WaterWetness", "TDFluid/AllWorkers/WaterWetness"], console={"TD.FluidFX.WaterWetness": {"A": 1, "B": 0}}),
                   [make_row("WA3/gt", "gt", [("젖음 패스", 0.15, "S009")], 0.15, "0.15", ["S009"])], water_layer=True),
        make_scene("CL1", "cloud", "CloudStage", [], "X", CL1_TOGGLE, "none", None, {}, "없음",
                   [console_step(["TD.FluidFX.CloudSetup 6"], True), {"action": "camera", "target_label": "TDGen_CloudCam_Volcano"}],
                   [actor_step(["TDGen_CloudLayers"], hidden=False)], [actor_step(["TDGen_CloudLayers"], hidden=True)], evidence(actor={"hidden": True, "niagara": False}),
                   [make_row("CL1/ft", "ft", [("구름 층 전체", 2.5, "S010")], 2.5, "2.5", ["S010", "S011"])]),
        make_scene("IN1", "input", "MassInputStage", ["TDGen_MassBench10k"], "C", "C `TD.FluidFX.MassInput` A -1 / B 0", "both", "GPU", {"TDGen_MassBench10k": 512}, "GPU 512²(TJ:8)",
                   [dict(CAMERA_STEP)], [console_step(["TD.FluidFX.MassInput -1"])], [console_step(["TD.FluidFX.MassInput 0"])],
                   evidence(columns=["TDFluid/MassInputSplatCalls"], console={"TD.FluidFX.MassInput": {"A": -1, "B": 0}}),
                   [make_row("IN1/ft", "ft", [("스플랫 1만", 0.5, "S012")], 0.5, "0.5", ["S012"])]),
        make_scene("TR1", "track", "SnowStage", ["TDGen_SnowTrack"], "Ls", "Ls", "A", "GPU", {"TDGen_SnowTrack": 1024}, "GPU 1024²(TJ:9)",
                   [dict(CAMERA_STEP)], [sleep_step(True)], [sleep_step(False)], evidence(True),
                   [make_row("TR1/ft", "ft", [("페인터 1024²", 0.1, "S013")], 0.1, "0.10", ["S013"]), make_row("TR1/ng", "ng", [("페인터 1024²", 0.1, "S013")], 0.1, "0.10", ["S013"])]),
        make_scene("VI1", "viscous", "ViscousStage", ["TDGen_ViscousBloodSlide"], "Ls", "Ls", "A", "GPU", {"TDGen_ViscousBloodSlide": 256}, "GPU 256²(TJ:10)",
                   [dict(CAMERA_STEP)], [sleep_step(True)], [sleep_step(False)], evidence(True),
                   [make_row("VI1/ng", "ng", [("점성 256²", 0.15, "S014")], 0.15, "0.15", ["S014"])]),
    ]


def fixture_areas(ledger_sha):
    items = [{"id": item_id, "source": source, "summary": summary, "limit": limit, "area": area, "disposition": kind, "reason": reason, "rows": rows,
              "record": {"value": None, "file": "fixture", "date": "2026-11-06", "canonical_sha": None, "before_w": False} if kind == "기록" else None}
             for item_id, source, summary, limit, area, _, kind, rows, reason in ITEMS]
    return {"version": 1, "ledger": {"path": LEDGER_REL, "sha256": ledger_sha},
            "areas_order": ["fire", "smoke", "fog", "water", "cloud", "input", "track", "viscous"],
            "cvar_defaults": [{"name": name, "value": value, "source": "fixture", "rows": []} for name, _, value in CVAR_ROWS],
            "record_only_variables": ["TD.FluidFX.SolverBackend", "TD.FluidFX.WaterGpu", "TD.FluidFX.LogTickCost"], "items": items,
            "derived_rows": [{"id": "FO3", "area": "fog", "from": "SM2/ft", "rule": "B17", "items": ["S008"]}], "scenes": fixture_scenes()}


STANDIN_PIE_PROFILE = '''import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FX7P_DEFAULTS = __DEFAULTS__


def check_fx7p_build_gate():
    return {"passed": not (ROOT / "fixture_build_gate_fail.txt").exists(), "fixture": True}


def main():
    with open(ROOT / "standin-calls.log", "a", encoding="utf-8") as log:
        log.write(" ".join(sys.argv[1:]) + "\\n")
    if len(sys.argv) != 3 or sys.argv[1] != "--check-plan":
        print("STANDIN: --check-plan만 안다")
        return 2
    plan = sys.argv[2].replace("\\\\", "/")
    print("STANDIN-CHECKPLAN-STDOUT " + plan)
    failing = ROOT / "fixture_checkplan_fail.txt"
    if failing.exists() and plan in failing.read_text(encoding="utf-8").split():
        print("STANDIN: 실패로 지정한 계획")
        return 2
    json.loads((ROOT / plan).read_text(encoding="utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
'''


def read_pie_defaults_literal(tool_root):
    tree = ast.parse((tool_root / "Tools/FluidFX/pie_profile_fluid.py").read_text(encoding="utf-8"))
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "FX7P_DEFAULTS" for target in node.targets):
            return repr(ast.literal_eval(node.value))
    raise RuntimeError("pie_profile_fluid.py에 FX7P_DEFAULTS가 없다")


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def write_json(path, value):
    write_text(path, json.dumps(value, ensure_ascii=False, indent=2) + "\n")


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_sheet_inputs(root):
    write_json(root / "Saved/FluidFX/calib/final/verdict-w11.json", {"adoptions": {"2": {"evidence": [
        "Saved/FluidFX/calib/final/batch-e1/runs/cloud_g11_cyclone_live/verify-1/compare.json"], "state_source": {"item_plan": "Tools/FluidFX/calib/plans/cloud_g11_cyclone_live.json"}}}})
    write_json(root / "Saved/FluidFX/calib/final/batch-e1/runs/cloud_g11_cyclone_live/verify-1/compare.json", {"checks": [{"scene": "cyclone_live"}]})
    write_json(root / EXPECT_REL, {"adoptions": {"2": {"scenes": ["cyclone_live"], "levels": [CLOUD_STAGE_LEVEL]}}})
    write_json(root / "Saved/FluidFX/calib/frames/cloud_g11_cyclone_live/sweep.json", {"scenes": [{"name": "cyclone_live", "level": CLOUD_STAGE_LEVEL}]})
    (root / "Saved/FluidFX/calib/final/batch-e1/runs/cloud_g11_cyclone_live/verify-1/cyclone_live").mkdir(parents=True, exist_ok=True)
    for plan_id, scenes in (("campfire_vorticity", ["campfire"]), ("cloud_g11_cyclone_live", ["cyclone_live"]), ("cloud_g2_wrap_seam", ["wrap_seam"]), ("foliage_k3_reed", ["k3_reed"])):
        write_json(root / f"Tools/FluidFX/calib/plans/{plan_id}.json", {"scenes": [{"name": name} for name in scenes]})
    for plan_id, scene in (("campfire_vorticity", "campfire"), ("cloud_g11_cyclone_live", "cyclone_live"), ("cloud_g2_wrap_seam", "wrap_seam")):
        write_text(root / f"Saved/FluidFX/calib/frames/{plan_id}/{scene}/00_schedule_10000ms.png", "fixture png\n")
    (root / "Saved/FluidFX/calib/frames/foliage_k3_reed/k3_reed").mkdir(parents=True, exist_ok=True)


def write_shared_compare_adoptions(root):
    plan_text = "Tools/FluidFX/calib/plans/cloud_g9_mushroom-p20.json"
    edit_json(root / VERDICT_REL, lambda verdict: verdict["adoptions"].update(
        {key: {"evidence": [MUSHROOM_COMPARE_REL], "state_source": {"item_plan": plan_text}} for key in ("3", "11")}))
    edit_json(root / EXPECT_REL, lambda expect: expect["adoptions"].update(
        {"3": {"scenes": ["mushroom_shield"], "levels": [CLOUD_STAGE_LEVEL]}, "11": {"scenes": ["mushroom_cap"], "levels": [CLOUD_STAGE_LEVEL]}}))
    write_json(root / MUSHROOM_COMPARE_REL, {"checks": [{"scene": name} for name in MUSHROOM_SCENES]})
    write_json(root / "Tools/FluidFX/calib/plans/cloud_g9_mushroom.json", {"scenes": [{"name": name} for name in MUSHROOM_SCENES]})
    write_json(root / "Saved/FluidFX/calib/frames/cloud_g9_mushroom/sweep.json", {"scenes": [{"name": name, "level": CLOUD_STAGE_LEVEL} for name in MUSHROOM_SCENES]})
    for name in MUSHROOM_SCENES:
        write_text(root / f"Saved/FluidFX/calib/frames/cloud_g9_mushroom/{name}/00_schedule_10000ms.png", "fixture png\n")
        (root / MUSHROOM_COMPARE_REL).parent.joinpath(name).mkdir(parents=True, exist_ok=True)


def run_sheet_attribution_cases(run, base, parent):
    root = copy_root(base, parent, "sheet-shared-compare")
    write_shared_compare_adoptions(root)
    code, out, err = run.build(root)
    check_code, check_out, _ = run.build(root, True)
    sheet = json.loads((root / T_REL / "sheet_areas.json").read_text(encoding="utf-8")) if code == 0 else {}
    cloud = [(entry["plan"], entry["scene"], entry["adoptions"]) for entry in sheet.get("areas", {}).get("cloud", {}).get("scenes", [])]
    run.check("build sheet_areas 채택 귀속(반박 M1): compare.json 하나를 함께 쓰는 채택 3·11 → 장면마다 expect scenes·levels 채택만·입력 expect sha·--check 0",
              code == 0 and check_code == 0 and not check_out.strip()
              and cloud == [("cloud_g11_cyclone_live", "cyclone_live", ["2"]), ("cloud_g9_mushroom", "mushroom_shield", ["3"]), ("cloud_g9_mushroom", "mushroom_cap", ["11"])]
              and sheet["inputs"]["expect"] == {"path": EXPECT_REL, "sha256": sha256_file(root / EXPECT_REL)} and sheet["reshoot"] == ["campfire_vorticity"],
              f"종료 {code}·{check_code} {cloud} {(out + err)[-300:] if code else check_out[:200]}")
    negative_cases = [
        ("음성 sheet_areas expect 판정 장면이 compare checks에 없음", lambda expect: expect["adoptions"]["11"]["scenes"].append("mushroom_gill"),
         "판정 장면 mushroom_gill이 compare.json checks에 없다"),
        ("음성 sheet_areas 0e level ∉ expect levels", lambda expect: expect["adoptions"]["3"].__setitem__("levels", ["/Game/FluidFX/Maps/LV_TDFluidFogStage"]),
         "cloud_g9_mushroom/mushroom_shield 0e level"),
        ("음성 sheet_areas compare 근거는 있는데 expect 채택 장면 없음", lambda expect: expect["adoptions"].pop("11"), "adoptions.11.scenes가 없다"),
    ]
    for index, (name, mutate, needle) in enumerate(negative_cases):
        root = copy_root(base, parent, f"sheet-neg{index:02d}")
        write_shared_compare_adoptions(root)
        edit_json(root / EXPECT_REL, mutate)
        run.expect_check_failure(name, root, needle)
    root = copy_root(base, parent, "sheet-neg-expect")
    (root / EXPECT_REL).unlink()
    run.expect_check_failure("음성 sheet_areas expect.json 없음", root, "expect.json 읽기 실패")


def make_base_root(base, tool_root, module):
    ledger = ledger_text()
    write_text(base / LEDGER_REL, ledger)
    write_json(base / AREAS_REL, fixture_areas(hashlib.sha256(ledger.encode("utf-8")).hexdigest()))
    write_text(base / "Tools/FluidFX/pie_profile_fluid.py", STANDIN_PIE_PROFILE.replace("__DEFAULTS__", read_pie_defaults_literal(tool_root)))
    shutil.copyfile(tool_root / "Tools/FluidFX/prereg_lock.py", base / "Tools/FluidFX/prereg_lock.py")
    for relative in module.CANONICAL_FILES:
        (base / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(tool_root / relative, base / relative)
    (base / module.ACCEPTED_OVERLAY_PATH).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(tool_root / module.ACCEPTED_OVERLAY_PATH, base / module.ACCEPTED_OVERLAY_PATH)
    for scene in fixture_scenes():
        write_text(base / module.level_map_path(scene["level"]), "fixture map\n")
        for relative in scene["hash_paths"]:
            write_text(base / relative, "fixture asset\n")
    placeholders = list(module.EDITOR_DLLS) + list(module.L0_LEDGER_FILES) + list(module.L0_TOOL_FILES) + list(module.L0_HISTORY_FILES)
    for relative in placeholders:
        path = base / relative
        if not path.exists():
            write_text(path, "{}\n" if relative.endswith(".json") else "fixture placeholder\n")
    write_sheet_inputs(base)
    write_json(base / "Saved/AgentOps/build_hold.json", {"owner": HOLDER, "reason": "fixture"})


class FixtureRun:
    def __init__(self, tool):
        self.tool = tool
        self.results = []

    def check(self, name, passed, detail=""):
        self.results.append((name, bool(passed)))
        print(f"{'통과' if passed else '실패'} {name}: {detail}", flush=True)

    def run_tool(self, root, *arguments):
        environment = dict(os.environ, PYTHONIOENCODING="utf-8")
        completed = subprocess.run([sys.executable, "-B", str(self.tool), *arguments, "--root", str(root)], capture_output=True, env=environment, timeout=600)
        return completed.returncode, completed.stdout.decode("utf-8", errors="replace"), completed.stderr.decode("utf-8", errors="replace")

    def build(self, root, check=False):
        return self.run_tool(root, "build", "--ledger", LEDGER_REL, "--areas", AREAS_REL, "--out-dir", T_REL, *(["--check"] if check else []))

    def expect_check_failure(self, name, root, needle):
        code, out, err = self.build(root, True)
        lines = [line for line in out.splitlines() if needle in line]
        self.check(name, code == 2 and bool(lines), f"종료 {code}, '{needle}' " + (lines[0][:160] if lines else f"없음(출력 {out[:200]!r} {err[-200:]!r})"))


def copy_root(base, parent, name):
    target = parent / name
    shutil.copytree(base, target)
    return target


def edit_json(path, mutate):
    value = json.loads(Path(path).read_text(encoding="utf-8"))
    mutate(value)
    write_json(path, value)


def edit_areas(root, mutate):
    edit_json(root / AREAS_REL, mutate)


def edit_ledger(root, old, new, update_sha=True):
    text = (root / LEDGER_REL).read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"fixture 장부에 '{old}'가 없다")
    write_text(root / LEDGER_REL, text.replace(old, new, 1))
    if update_sha:
        edit_areas(root, lambda areas: areas["ledger"].__setitem__("sha256", sha256_file(root / LEDGER_REL)))


def scene_of(areas, scene_id):
    return next(scene for scene in areas["scenes"] if scene["id"] == scene_id)


def item_of(areas, item_id):
    return next(item for item in areas["items"] if item["id"] == item_id)


def row_of(areas, row_id):
    return next(row for scene in areas["scenes"] for row in scene["rows"] if row["id"] == row_id)


def hash_tree(root):
    return {path.relative_to(root).as_posix(): sha256_file(path) for path in sorted((root / T_REL).rglob("*.json"))}


def run_build_cases(run, base, parent, module):
    code, out, err = run.build(base)
    expected = {f"{area}/{name}" for area in module.AREAS for name in ("table.json", "pilot1.json")}
    expected |= {f"{area}/{name}" for area in ("fire", "smoke", "fog", "cloud", "input", "track") for name in ("ladder.json", "ladder-table.json")}
    expected.add("sheet_areas.json")
    produced = {path.relative_to(base / T_REL).as_posix() for path in (base / T_REL).rglob("*.json") if path.name != "p_areas.json"}
    run.check("build 양성: 영역 8 표·시운전, ft 영역만 사다리, sheet_areas", code == 0 and produced == expected,
              f"종료 {code}, 차이 {sorted(produced ^ expected)} {err[-300:] if code else ''}")
    leftovers = [path.name for path in (base / T_REL).rglob(".*.tmp-*")]
    run.check("build 원자적 쓰기: 임시 파일 없음", not leftovers, f"{leftovers}")
    code, out, err = run.build(base, True)
    run.check("build --check 양성(Ls+X·aw —·괄호 ID FI5a(E-A)·FO3 파생·'=' 토글·F7 a²+b²)", code == 0 and not out.strip(), f"종료 {code} {out[:300]}")
    before = hash_tree(base)
    code, _, _ = run.build(base)
    run.check("build 재생성 바이트: 같은 입력 = 같은 바이트", code == 0 and hash_tree(base) == before, f"종료 {code}")
    pilot = json.loads((base / T_REL / "fire/pilot1.json").read_text(encoding="utf-8"))
    ladder = json.loads((base / T_REL / "fire/ladder.json").read_text(encoding="utf-8"))
    sp_ok = all(scene["setup"][0] == module.B5_STEP and scene["setup"][-1] == {"action": "console", "commands": ["r.ScreenPercentage 100"], "restore": True}
                and scene["meta"]["screen_percentage"] == 100 for scene in pilot["scenes"])
    ladder_ids = [scene["id"] for scene in ladder["scenes"]]
    run.check("build SP100 단계 통과·B5 첫 단계·사다리 SP 바깥·LG 순서 안쪽", sp_ok and ladder_ids == ["FI1_sp130", "FI5a_sp130", "FI1_sp160", "FI5a_sp160", "FI1_sp200", "FI5a_sp200"]
              and pilot["write_fx7p_evidence"] is False and pilot["_p"]["ledger_sha256"] and pilot["scenes"][0].get("block_frames") == 1200, f"사다리 {ladder_ids}")
    smoke = json.loads((base / T_REL / "smoke/table.json").read_text(encoding="utf-8"))
    fire = json.loads((base / T_REL / "fire/table.json").read_text(encoding="utf-8"))
    cloud = json.loads((base / T_REL / "cloud/table.json").read_text(encoding="utf-8"))
    run.check("build 표: F7 grid {라벨: 실효}·Ls+X·aw none·target_labels []·regime",
              fire["scenes"]["FI1"]["grid"] == {"TDGen_CampfireFire": 128, "TDGen_CampfireSmoke": 96} and smoke["scenes"]["HV2"]["toggle"] == "Ls+X"
              and cloud["scenes"]["CL1"]["awake_sides"] == "none" and cloud["scenes"]["CL1"]["target_labels"] == [] and cloud["scenes"]["CL1"]["grid"] == {}
              and fire["scenes"]["FI1"]["regime"] is True and fire["reference"] is None and [row["row"] for row in fire["rows"]] == ["FI1/ft", "FI1/gt", "FI1/rt", "FI5a/ft"],
              f"{fire['scenes']['FI1']['grid']}")
    sheet = json.loads((base / T_REL / "sheet_areas.json").read_text(encoding="utf-8"))
    cloud_scenes = [(entry["plan"], entry["scene"], entry["source"]) for entry in sheet["areas"]["cloud"]["scenes"]]
    run.check("build sheet_areas 6.2: 채택 장면 + 2개 미만 채움·reshoot = W11 짝 없음·사진 0 계획 제외",
              cloud_scenes == [("cloud_g11_cyclone_live", "cyclone_live", "adoption"), ("cloud_g2_wrap_seam", "wrap_seam", "fill")]
              and [entry["scene"] for entry in sheet["areas"]["fire"]["scenes"]] == ["campfire"] and sheet["areas"]["fire"]["note"] == "대표 1개(0e 짝 없음)"
              and sheet["areas"]["foliage"]["scenes"] == [] and sheet["reshoot"] == ["campfire_vorticity", "cloud_g2_wrap_seam"], f"{cloud_scenes} {sheet['reshoot']}")
    negative_cases = [
        ("음성 처분 없음(LG)", lambda root: edit_ledger(root, "| 측정 불가(메모리 경로 없음) |", "| 보류 |"), "처분 없음"),
        ("음성 처분 둘(LG)", lambda root: edit_ledger(root, "| 출하 밖(출하 프리셋 CPU) |", "| 출하 밖(출하 프리셋 CPU) 측정 불가(x) |"), "처분 둘"),
        ("음성 처분 둘(p_areas)", lambda root: edit_areas(root, lambda areas: item_of(areas, "S016").__setitem__("disposition", "출하 밖·측정 불가")), "처분 없음 또는 처분 둘"),
        ("음성 한도 줄 기록", lambda root: edit_areas(root, lambda areas: item_of(areas, "S016").update(
            {"disposition": "기록", "record": {"value": 1, "file": "f", "date": "d", "canonical_sha": None, "before_w": True}})), "한도 있는 항목은 기록 불가"),
        ("음성 없는 행 인용", lambda root: edit_areas(root, lambda areas: item_of(areas, "S005").__setitem__("rows", ["SM2/ft", "SM9/ft"])), "없는 행 인용"),
        ("음성 행 미인용(양방향)", lambda root: edit_areas(root, lambda areas: item_of(areas, "S013").__setitem__("rows", ["TR1/ft"])), "행 미인용"),
        ("음성 조성식", lambda root: edit_areas(root, lambda areas: row_of(areas, "FI1/ft").__setitem__("limit_formula", "0.3+0.2")), "조성식 값"),
        ("음성 몫 합 ≠ 한도", lambda root: edit_areas(root, lambda areas: row_of(areas, "FO1/ft")["parts_off"][1].__setitem__("ms", 0.2)), "몫 합"),
        ("음성 허용 밖 키", lambda root: edit_areas(root, lambda areas: scene_of(areas, "SM2")["steps"]["setup"][0].__setitem__("zoom", 2)), "허용 밖 키"),
        ("음성 configure 단계", lambda root: edit_areas(root, lambda areas: scene_of(areas, "FI1")["steps"]["setup"].append(
            {"action": "configure", "targets": ["TDGen_CampfireFire"], "backend": "Gpu"})), "금지 단계 configure"),
        ("음성 X 토글 밖 키(niagara)", lambda root: edit_areas(root, lambda areas: [step.__setitem__("niagara", state) for step, state in zip(
            (scene_of(areas, "CL1")["steps"]["a"][0], scene_of(areas, "CL1")["steps"]["b"][0]), ("activate", "deactivate"))]), "X 토글 actor 키"),
        ("음성 tick 키", lambda root: edit_areas(root, lambda areas: scene_of(areas, "HV2")["steps"]["b"][1].__setitem__("tick", False)), "tick 키"),
        ("음성 setup console restore 없음", lambda root: edit_areas(root, lambda areas: scene_of(areas, "FO1")["steps"]["setup"][0].pop("restore")), "restore 참 아님"),
        ("음성 Lb에 sleep(토글↔단계)", lambda root: edit_areas(root, lambda areas: [scene_of(areas, "FI5a")["steps"][side].append(sleep_step(side == "a")) for side in ("a", "b")]),
         "허용 밖 단계"),
        ("음성 a = b 반대 아님", lambda root: edit_areas(root, lambda areas: scene_of(areas, "SM2")["steps"]["a"][1].__setitem__("visible", False)), "a 단계"),
        ("음성 LG 칸 불일치(한도)", lambda root: edit_ledger(root, "| 4.0 = 4.0 | BC(A) | S005·S008 |", "| 3.5 = 3.5 | BC(A) | S005·S008 |"), "LG v2 칸 불일치"),
        ("음성 LG 칸 불일치(토글 첫 기호 K4)", lambda root: edit_ledger(root, "| Lb: B [embers] 끔 |", "| L: B [embers] 끔 |"), "토글 첫 기호"),
        ("음성 LG 칸 불일치(grid F7 순서)", lambda root: edit_ledger(root, "CPU 128²(TJ:1)+CPU 96²(TJ:2)", "CPU 96²(TJ:1)+CPU 128²(TJ:2)"), "grid(F7 변환)"),
        ("음성 6절 남은 누락 > 0", lambda root: edit_ledger(root, "남은 누락 0", "남은 누락 2"), "남은 누락 2 > 0"),
        ("음성 ledger sha 불일치", lambda root: edit_ledger(root, "## 7. 개수 요약", "## 7. 개수 요약(수정)", update_sha=False), "ledger.sha256"),
        ("음성 FX7P 기본 cvars 빠짐", lambda root: edit_json(root / T_REL / "fire/pilot1.json", lambda plan: plan["cvars"].remove("r.VSync 0")), "FX7P 기본 cvars 빠짐"),
        ("음성 OverrideFPS 빠짐", lambda root: edit_json(root / T_REL / "fire/pilot1.json", lambda plan: plan["cvars"].remove("t.OverrideFPS 0")), "t.OverrideFPS 0 없음"),
        ("음성 맵 해시 빠짐", lambda root: edit_json(root / T_REL / "fire/pilot1.json", lambda plan: plan["hash_paths"].remove("Content/FluidFX/Maps/LV_TDFluidFireStage.umap")),
         "맵 해시 빠짐"),
        ("음성 SP 단계 없음", lambda root: edit_json(root / T_REL / "fire/pilot1.json", lambda plan: plan["scenes"][0]["setup"].pop()), "SP 단계 없음"),
        ("음성 SP ≠ meta", lambda root: edit_json(root / T_REL / "fire/pilot1.json", lambda plan: plan["scenes"][0]["meta"].__setitem__("screen_percentage", 160)), "SP ≠ meta"),
        ("음성 첫 단계 B5 아님", lambda root: edit_json(root / T_REL / "fire/pilot1.json", lambda plan: plan["scenes"][0]["setup"].pop(0)), "첫 단계 B5 아님"),
        ("음성 ft 없는 영역 사다리", lambda root: shutil.copyfile(root / T_REL / "fire/ladder.json", root / T_REL / "water/ladder.json"), "생성 밖 파일 water/ladder.json"),
    ]
    for index, (name, mutate, needle) in enumerate(negative_cases):
        root = copy_root(base, parent, f"neg{index:02d}")
        mutate(root)
        run.expect_check_failure(name, root, needle)
    root = copy_root(base, parent, "neg-write")
    edit_areas(root, lambda areas: scene_of(areas, "FI1")["steps"]["setup"].append({"action": "configure", "targets": ["TDGen_CampfireFire"]}))
    before = hash_tree(root)
    code, out, _ = run.build(root)
    run.check("음성 build 쓰기 거부: 문제 있으면 $T 바이트 불변", code == 2 and hash_tree(root) == before, f"종료 {code}")
    converted, problem = module.convert_ledger_grid("CPU 128²×2(TJ:1)", ["TDGen_A"])
    _, mismatch = module.convert_ledger_grid("CPU 128²+96²", ["TDGen_A"])
    run.check("F7 변환: ×n = 같은 라벨 n개(count)·항 수 ≠ 라벨 수 → 문제·없음 = {}",
              converted == {"backend": "CPU", "grid": {"TDGen_A": 128}, "count": 2} and problem is None and mismatch
              and module.convert_ledger_grid("없음", [])[0]["grid"] == {}, f"{converted} {mismatch}")
    symbols = [module.ledger_toggle_symbol(cell) for cell in (FO1_TOGGLE, "Ls(솔버째 멈춤)", "C `TD.FluidFX.MassInput` A -1 / B 0", CL1_TOGGLE,
                                                               "setup CloudSetup 6·camera Volcano(=CL1/ft); X", "Ls+X: B sleep", "setup 끝 없음")]
    run.check("AM6 K4 토글 첫 기호: setup …; 떼기·`:`·`(`·공백 앞·`+` 잇기", symbols == ["L+X", "Ls", "C", "X", "X", "Ls+X", None], f"{symbols}")
    xn_scene = {"id": "FI5b", "toggle": "Xn", "steps": {"setup": [], "a": [actor_step(["TDGen_EmberB"], hidden=False)], "b": [actor_step(["TDGen_EmberB"], hidden=True)]}}
    xn_problems = module.find_toggle_problems(xn_scene, {})
    composite = {"id": "SM9", "toggle": "Ls+Xn", "steps": {"setup": [], "a": [sleep_step(True), actor_step(["TDGen_B"], niagara="activate")],
                                                            "b": [sleep_step(False), actor_step(["TDGen_B"], niagara="deactivate")]}}
    run.check("음성 Xn 토글 밖 키(hidden)·양성 복합 Ls+Xn(+ 앞 기호 단계 + actor)", any("Xn 토글 actor 키" in problem for problem in xn_problems)
              and module.find_toggle_problems(composite, {}) == [] and module.parse_toggle("C+X") is None and module.parse_toggle("X+X") is None, f"{xn_problems}")


def write_lock(root, name, created_at, text=None):
    path = root / LOCKS_REL / name
    write_text(path, text if text is not None else json.dumps({"tool": "prereg_lock", "version": 2, "created_at": created_at, "note": "fixture", "files": {"x": "0" * 64}}) + "\n")


def run_current_lock_cases(run, base, parent):
    root = copy_root(base, parent, "locks")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L0")
    run.check("current-lock 잠금 없음 → 2", code == 2 and not out.strip(), f"종료 {code}")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L0", "--next")
    run.check("current-lock --next 처음 L0.json", code == 0 and out.strip() == f"{LOCKS_REL}/L0.json", f"{out.strip()}")
    write_lock(root, "L0.json", "2026-11-06T10:00:00+09:00")
    write_lock(root, "L0r1.json", "2026-11-06T11:00:00+09:00")
    write_lock(root, "L0r2.json", "2026-11-06T12:00:00+09:00")
    write_lock(root, "L0-head.txt", "", "abc\n")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L0")
    run.check("current-lock r2 최신", code == 0 and out.strip() == f"{LOCKS_REL}/L0r2.json", f"{out.strip()}")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L0", "--next")
    run.check("current-lock --next = L0r3.json(루트 상대 POSIX 한 줄)", code == 0 and out == f"{LOCKS_REL}/L0r3.json\n", f"{out!r}")
    write_lock(root, "L1-fire-1.json", "2026-11-06T11:30:00+09:00")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L1", "--area", "fire")
    run.check("current-lock 옛 L0 시기 L1 안 냄 → 2", code == 2 and not out.strip(), f"종료 {code} {out.strip()}")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L1", "--area", "fire", "--next")
    run.check("current-lock L1 --next = L1-fire-2.json", code == 0 and out.strip() == f"{LOCKS_REL}/L1-fire-2.json", f"{out.strip()}")
    write_lock(root, "L1-fire-2.json", "2026-11-06T12:00:00+09:00")
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L1", "--area", "fire")
    run.check("current-lock L1 created_at ≥ 현재 L0(같은 초 포함)", code == 0 and out.strip() == f"{LOCKS_REL}/L1-fire-2.json", f"{out.strip()}")
    code, _, _ = run.run_tool(root, "current-lock", "--stage", "L1")
    run.check("current-lock L1 --area 없음 → 2", code == 2, f"종료 {code}")
    write_lock(root, "L0r3.json", "", '{"tool": "prereg_lock", "created_at": "2026-11-06T13')
    code, out, _ = run.run_tool(root, "current-lock", "--stage", "L0")
    run.check("current-lock 잘린 JSON → 2", code == 2 and not out.strip(), f"종료 {code}")


def make_area_dir(root, l0_stem, pilot_exit):
    area_dir = root / P_REL / l0_stem / "fire"
    plan = json.loads((root / T_REL / "fire/pilot1.json").read_text(encoding="utf-8"))
    plan["run_label"] = "p-fire-judge"
    write_json(area_dir / "judge-plan.json", plan)
    write_json(area_dir / "sp-choice.json", {"area": "fire", "inputs": {}, "scenes": {}})
    write_json(area_dir / "pilot1-runs.json", {"exit": pilot_exit, "selected": None})
    write_json(area_dir / "ladder-need.json", {"area": "fire", "needed": False})
    if pilot_exit == 0:
        write_json(area_dir / "pilot1-judge.json", {"scenes": {}})
    return area_dir


def run_lock_list_cases(run, base, parent):
    root = copy_root(base, parent, "list-ok")
    code, out, err = run.run_tool(root, "lock-list", "--stage", "L0", "--areas", AREAS_REL)
    listed = [line for line in out.splitlines() if line.strip()]
    missing = [line for line in listed if not (root / line).is_file()]
    required = [AREAS_REL, f"{T_REL}/sheet_areas.json", f"{T_REL}/fire/table.json", f"{T_REL}/fire/ladder-table.json", f"{T_REL}/water/pilot1.json", LEDGER_REL,
                "Binaries/Win64/UnrealEditor-TDWorldGen.dll", "Content/FluidFX/PresetData/TDFluidMaterialLook.json", "Content/FluidFX/Maps/LV_TDFluidCloudStage.umap",
                "Content/FluidFX/Fixture/NS_Fixture_VI1.uasset", "Saved/AgentOps/20261106/p-prep/runbook-p-v3-amend6.md", "Tools/FluidFX/perf/p/check_p_fixtures.py"]
    calls = (root / "standin-calls.log").read_text(encoding="utf-8").splitlines() if (root / "standin-calls.log").is_file() else []
    run.check("lock-list L0 양성: 목록만 stdout·파일 존재·2.4 목록 포함", code == 0 and not missing and all(entry in listed for entry in required) and len(listed) == len(set(listed)),
              f"종료 {code}, {len(listed)}줄, 없음 {missing[:3]}, 빠짐 {[entry for entry in required if entry not in listed]} {err[-200:] if code else ''}")
    run.check("lock-list L0 stdout 미혼입·하위 출력은 stderr", "STANDIN" not in out and "STANDIN-CHECKPLAN-STDOUT" in err, f"stderr {err.count('STANDIN-CHECKPLAN-STDOUT')}회")
    run.check("lock-list L0 --root 대역 명령만·재귀 없음(--check-plan 14회)", len(calls) == 14 and all(call.startswith("--check-plan ") for call in calls), f"{len(calls)}회")
    for name, prepare, expected in (
            ("lock-list L0 잘린 JSON → 2", lambda target: write_text(target / "Content/FluidFX/PresetData/TDFluidMaterialLook.json", '{"Kinds": '), 2),
            ("lock-list L0 재생성 불일치 → 2", lambda target: write_text(target / T_REL / "fog/table.json", (target / T_REL / "fog/table.json").read_text(encoding="utf-8") + " "), 2),
            ("lock-list L0 check-plan 실패 → 2", lambda target: write_text(target / "fixture_checkplan_fail.txt", f"{T_REL}/smoke/pilot1.json\n"), 2),
            ("lock-list L0 2.4 파일 없음 → 2", lambda target: (target / "Saved/AgentOps/20261106/p/dev-10.md").unlink(), 2)):
        target = copy_root(base, parent, f"list-{len(run.results)}")
        prepare(target)
        code, out, err = run.run_tool(target, "lock-list", "--stage", "L0", "--areas", AREAS_REL)
        run.check(name, code == expected and not out.strip(), f"종료 {code}, stdout {out[:80]!r}, {err.strip().splitlines()[-1][:160] if err.strip() else ''}")
    root = copy_root(base, parent, "list-l1")
    write_lock(root, "L0.json", "2026-11-06T10:00:00+09:00")
    area_dir = make_area_dir(root, "L0", 4)
    code, out, err = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix())
    listed = out.split()
    run.check("lock-list L1 시운전 종료 4 → L1 성공(필수 4개, judge-plan check-plan)", code == 0 and listed == [f"{P_REL}/L0/fire/{name}" for name in
                                                                                         ("judge-plan.json", "sp-choice.json", "pilot1-runs.json", "ladder-need.json")],
              f"종료 {code} {listed} {err[-200:] if code else ''}")
    write_json(area_dir / "pilot1-runs.json", {"exit": 0, "selected": {"run_dir": "x"}})
    code, out, _ = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix())
    run.check("lock-list L1 시운전 exit 0인데 pilot1-judge 없음 → 2", code == 2 and not out.strip(), f"종료 {code}")
    write_json(area_dir / "pilot1-judge.json", {"scenes": {}})
    code, out, _ = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix())
    run.check("lock-list L1 있으면 pilot1-judge 포함", code == 0 and out.split()[-1] == f"{P_REL}/L0/fire/pilot1-judge.json", f"종료 {code}")
    write_text(root / "fixture_checkplan_fail.txt", f"{P_REL}/L0/fire/judge-plan.json\n")
    code, out, _ = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix())
    run.check("lock-list L1 judge-plan check-plan 실패 → 2", code == 2 and not out.strip(), f"종료 {code}")
    (root / "fixture_checkplan_fail.txt").unlink()
    (area_dir / "sp-choice.json").unlink()
    code, out, _ = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix())
    run.check("lock-list L1 필수 파일 없음 → 2", code == 2 and not out.strip(), f"종료 {code}")
    code, out, _ = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", f"{P_REL}/L0r9/fire")
    run.check("lock-list L1 --area-dir가 현재 L0 폴더 아님 → 2", code == 2 and not out.strip(), f"종료 {code}")
    write_lock(root, "L1-fire-1.json", "2026-11-06T10:30:00+09:00")
    code, out, _ = run.run_tool(root, "lock-list", "--stage", "L1", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix())
    run.check("lock-list L1 현재 L1 있음 → 3", code == 3 and not out.strip(), f"종료 {code}")


class FakeProbes:
    def __init__(self, ini_path, editor_count=1, gpu_ok=True):
        self.ini_path, self.editor_count, self.gpu_ok = ini_path, editor_count, gpu_ok

    def count_editor_processes(self):
        return self.editor_count, f"fixture UnrealEditor {self.editor_count}개"

    def query_gpu(self):
        return self.gpu_ok, "fixture nvidia-smi"

    def global_editor_ini(self):
        return self.ini_path


def run_preflight_cases(run, base, parent, module):
    root = copy_root(base, parent, "preflight")
    lock_out = f"{LOCKS_REL}/L0.json"
    completed = subprocess.run([sys.executable, "-B", str(root / "Tools/FluidFX/prereg_lock.py"), "lock", "--out", lock_out, "--file", LEDGER_REL, "--file", AREAS_REL],
                               cwd=str(root), capture_output=True, env=dict(os.environ, PYTHONIOENCODING="utf-8"), timeout=120)
    run.check("preflight 준비: prereg_lock 복사본으로 L0 잠금", completed.returncode == 0 and (root / lock_out).is_file(), f"종료 {completed.returncode}")
    ini = root / "fixture-ini/EditorSettings.ini"
    write_text(ini, "[/Script/UnrealEd.EditorPerformanceSettings]\nbThrottleCPUWhenNotForeground=False\n")

    def preflight(name, probes):
        out = f"{P_REL}/{name}.json"
        arguments = argparse.Namespace(root=str(root), lock=lock_out, holder=HOLDER, out=out)
        with contextlib.redirect_stdout(sys.stderr):
            code = module.preflight_command(arguments, probes)
        report = json.loads((root / out).read_text(encoding="utf-8")) if (root / out).is_file() else {"checks": [], "ok": None}
        bad = sorted(entry["name"] for entry in report["checks"] if not entry["ok"])
        return code, report, bad

    code, report, bad = preflight("pf-ok", FakeProbes(ini))
    run.check("preflight 양성: 정본·accepted 빈 v2·보류·빌드 관문·GPU·에디터 1·ini·L0 verify", code == 0 and report["ok"] is True and not bad
              and [entry["name"] for entry in report["checks"]][:2] == ["canonical_sha", "accepted_empty_v2"], f"종료 {code} 실패 {bad}")
    write_text(ini, "[/Script/UnrealEd.EditorPerformanceSettings]\nbThrottleCPUWhenNotForeground=True\n")
    code, report, bad = preflight("pf-ini", FakeProbes(ini))
    run.check("preflight 전역 ini True → 2", code == 2 and bad == ["a13_global_ini"], f"종료 {code} 실패 {bad}")
    write_text(ini, "[/Script/UnrealEd.EditorPerformanceSettings]\nbThrottleCPUWhenNotForeground=False\n")
    code, report, bad = preflight("pf-editor", FakeProbes(ini, editor_count=2))
    run.check("preflight 에디터 2개 → 2", code == 2 and bad == ["editor_single"], f"종료 {code} 실패 {bad}")
    old = time.time() - 4 * 3600
    os.utime(root / "Saved/AgentOps/build_hold.json", (old, old))
    code, report, bad = preflight("pf-hold", FakeProbes(ini))
    run.check("preflight 보류 3시간 초과 → 2", code == 2 and bad == ["hold_age"], f"종료 {code} 실패 {bad}")
    os.utime(root / "Saved/AgentOps/build_hold.json", None)
    write_text(root / AREAS_REL, (root / AREAS_REL).read_text(encoding="utf-8") + " ")
    code, report, bad = preflight("pf-verify", FakeProbes(ini))
    run.check("preflight L0 verify 실패 → 2", code == 2 and bad == ["l0_verify"], f"종료 {code} 실패 {bad}")
    write_text(root / "fixture_build_gate_fail.txt", "x\n")
    code, report, bad = preflight("pf-gate", FakeProbes(ini))
    run.check("preflight 빌드 관문 실패 → 2", code == 2 and "build_gate" in bad, f"종료 {code} 실패 {bad}")
    existing = root / P_REL / "pf-ok.json"
    before = existing.read_bytes()
    code, out, err = run.run_tool(root, "preflight", "--lock", lock_out, "--holder", HOLDER, "--out", f"{P_REL}/pf-ok.json")
    run.check("preflight --out 있음 → 2·덮어쓰지 않음", code == 2 and existing.read_bytes() == before, f"종료 {code}")
    leftovers = [path.name for path in (root / P_REL).glob(".*.tmp-*")]
    run.check("preflight 원자적 쓰기: 임시 파일 없음", not leftovers, f"{leftovers}")


RUNS_REL = "Saved/FluidFX/fx7p"
RUN_PLAN_REL = f"{T_REL}/viscous/pilot1.json"
RUN_OUT_REL = f"{P_REL}/L0/viscous/pilot1-runs.json"
HOLD_MESSAGE = "보류 owner other가 --holder p-perf와 다르다"


def local_iso(hour, minute, second=0):
    return datetime(2026, 11, 6, hour, minute, second).astimezone().isoformat(timespec="seconds")


def token_of(hour, minute, second=0):
    return f"20261106{hour:02d}{minute:02d}{second:02d}"


def write_files_lock(root, name, created_at, files):
    write_json(root / LOCKS_REL / name, {"tool": "prereg_lock", "version": 2, "created_at": created_at, "note": "fixture", "files": {relative: sha256_file(root / relative) for relative in files}})


def load_pie_merge(tool_root):
    source = (tool_root / "Tools/FluidFX/pie_profile_fluid.py").read_text(encoding="utf-8")
    namespace = {"json": json}
    function_source = None
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "FX7P_DEFAULTS" for target in node.targets):
            namespace["FX7P_DEFAULTS"] = ast.literal_eval(node.value)
        if isinstance(node, ast.FunctionDef) and node.name == "merge_fx7p_plan":
            function_source = ast.get_source_segment(source, node)
    exec(compile(function_source, "pie_profile_fluid.merge_fx7p_plan", "exec"), namespace)
    return namespace["merge_fx7p_plan"]


class RunFactory:
    def __init__(self, merge):
        self.merge = merge
        self.hashes = {}

    def git_hash(self, root, relative):
        data = (root / relative).read_bytes()
        key = (relative, hashlib.sha256(data).hexdigest())
        if key not in self.hashes:
            completed = subprocess.run(["git", "hash-object", relative], cwd=str(root), capture_output=True, timeout=120)
            self.hashes[key] = completed.stdout.decode("utf-8").strip()
        return self.hashes[key]

    def make(self, root, token, plan_rel=RUN_PLAN_REL, label="p-viscous-pilot1", session="valid", blocks=2, complete=True, plan_change=None, condition_change=None,
             frame_p50=16.0, gpu_max=4.0, environment_error=None, log_text=None, asset_change=None, session_plan=None):
        run_dir = root / RUNS_REL / f"{label}-{token}"
        merged = self.merge(json.loads((root / plan_rel).read_text(encoding="utf-8")))
        if plan_change:
            plan_change(merged)
        write_json(run_dir / "plan.json", merged)
        scene_ids = [scene["id"] for scene in merged["scenes"]]
        write_text(run_dir / "csv" / f"{scene_ids[0]}_warm1.csv", "FrameTime,GPUTime\n16,4\n")
        for index in range(blocks):
            write_text(run_dir / "csv" / f"{scene_ids[0]}_b{index}.csv", "FrameTime,GPUTime\n16,4\n")
        if log_text:
            write_text(run_dir / "pie.log", log_text + "\n")
        if session == "none":
            return run_dir
        if session == "truncated":
            write_text(run_dir / "session.json", '{"tool": "pie_profile_fluid --csv-gpu", "run_token": "' + token)
            return run_dir
        variables = {command.split()[0]: float(command.split()[1]) for command in merged["cvars"]}
        if condition_change:
            condition_change(variables)
        stats = {"FrameTime": {"n": 240, "p50": frame_p50, "p95": frame_p50, "mean": frame_p50, "max": frame_p50}, "GPUTime": {"n": 240, "p50": gpu_max, "p95": gpu_max, "mean": gpu_max, "max": gpu_max}}
        records = [{"id": scene_id, "kind": "csv", "conditions": {"variables": dict(variables), "viewport": [1920, 1080]},
                    "blocks": [{"block_id": f"{scene_id}_A1", "kind": "block", "stats": stats}] if blocks else [], "error": None} for scene_id in scene_ids]
        hashes = {relative: self.git_hash(root, relative) for relative in merged["hash_paths"]}
        if asset_change:
            hashes[asset_change] = "0" * 40
        started = f"2026-11-06 {token[8:10]}:{token[10:12]}:{token[12:14]}"
        write_json(run_dir / "session.json", {"tool": "pie_profile_fluid --csv-gpu", "run_token": token, "plan": session_plan or str((root / plan_rel).resolve()), "started": started,
                                              "asset_hashes": hashes, "scenes": records, "environment_error": environment_error})
        if complete:
            write_json(run_dir / "result.json", {"run": run_dir.name, "environment_error": None, "scenes": {scene_id: {"id": scene_id, "error": None} for scene_id in scene_ids}})
        return run_dir


def run_runs(run, root, plan_rel=RUN_PLAN_REL, lock_name="L0.json", out_rel=RUN_OUT_REL):
    code, out, err = run.run_tool(root, "runs", "--plan", plan_rel, "--lock", f"{LOCKS_REL}/{lock_name}", "--out", out_rel)
    report = json.loads((root / out_rel).read_text(encoding="utf-8")) if (root / out_rel).is_file() else None
    return code, report, out, err


def run_runs_cases(run, base, parent, factory):
    runs_base = copy_root(base, parent, "runs-base")
    write_files_lock(runs_base, "L0.json", local_iso(18, 39), [RUN_PLAN_REL, f"{T_REL}/viscous/table.json", LEDGER_REL])
    counter = [0]

    def fresh(two_locks=False):
        counter[0] += 1
        root = copy_root(runs_base, parent, f"runs-{counter[0]:02d}")
        if two_locks:
            (root / LOCKS_REL / "L0.json").unlink()
            write_files_lock(root, "L0.json", local_iso(18, 0), [RUN_PLAN_REL, LEDGER_REL])
            write_files_lock(root, "L0r1.json", local_iso(18, 39), [RUN_PLAN_REL, f"{T_REL}/viscous/table.json", LEDGER_REL])
        return root

    def classes(report):
        return [entry["class"] for entry in report["runs"]] if report else None

    root = fresh()
    factory.make(root, token_of(18, 40))
    code, report, out, err = run_runs(run, root)
    run.check("runs SS1: 같은 날 '2026-11-06 18:40:00' 실행 대 18:39+09:00 잠금 → 0(6 아님)·OUT에 exit·selected", code == 0 and report and report["exit"] == 0
              and report["selected"]["run_token"] == token_of(18, 40) and report["lock_created_at"] == local_iso(18, 39), f"종료 {code} {out.strip()[-200:]} {err[-200:]}")
    leftovers = [path.name for path in (root / P_REL).rglob(".*.tmp-*")]
    run.check("runs 원자적 쓰기: 임시 파일 없음", not leftovers, f"{leftovers}")
    root = fresh()
    factory.make(root, token_of(18, 0))
    factory.make(root, token_of(18, 45))
    code, report, out, _ = run_runs(run, root)
    run.check("runs ② 첫 잠금 전 시작 → 6(종착)·OUT 없음", code == 6 and report is None and "첫 잠금" in out, f"종료 {code} {out.strip()[-160:]}")
    root = fresh(two_locks=True)
    factory.make(root, token_of(18, 20))
    factory.make(root, token_of(18, 41))
    code, report, out, err = run_runs(run, root, lock_name="L0r1.json")
    run.check("runs ③ 이전 잠금 건너뜀(L0r1 현재)·첫 잠금 = L0", code == 0 and classes(report) == ["이전 잠금", "선택"] and report["first_lock"]["path"] == f"{LOCKS_REL}/L0.json"
              and report["selected"]["run_token"] == token_of(18, 41), f"종료 {code} {classes(report)} {err[-200:]}")
    code, report, _, _ = run_runs(run, root, lock_name="L0.json", out_rel=f"{P_REL}/L0/viscous/x-runs.json")
    run.check("runs LOCK이 그 계열 현재 잠금 아님 → 2", code == 2 and report is None, f"종료 {code}")
    root = fresh()
    factory.make(root, token_of(18, 41), plan_rel=f"{T_REL}/fire/pilot1.json", label="p-fire-pilot1")
    factory.make(root, token_of(18, 42), plan_change=lambda plan: plan.__setitem__("block_frames", 301))
    factory.make(root, token_of(18, 43))
    code, report, _, err = run_runs(run, root)
    run.check("runs ① 다른 계획 경로 무시·④ merge 다름 건너뜀", code == 0 and classes(report) == ["계획 다름", "선택"], f"종료 {code} {classes(report)} {err[-200:]}")
    code, report, _, _ = run_runs(run, root, plan_rel=f"{T_REL}/fire/pilot1.json", out_rel=f"{P_REL}/L0/fire/pilot1-runs.json")
    run.check("runs PLAN이 LOCK files에 없음 → 2", code == 2 and report is None, f"종료 {code}")
    root = fresh()
    for minute in (41, 42, 43):
        factory.make(root, token_of(18, minute), blocks=0, complete=False)
    factory.make(root, token_of(18, 44), complete=False, environment_error=HOLD_MESSAGE)
    factory.make(root, token_of(18, 45), complete=False, environment_error="보류가 3.4시간 지나 다른 세션이 무시한다: 해제 뒤 다시 획득한다")
    factory.make(root, token_of(18, 46), complete=False, log_text="환경 문제: " + HOLD_MESSAGE)
    factory.make(root, token_of(18, 47))
    code, report, _, err = run_runs(run, root)
    run.check("runs 블록 0·보류 문구(session·로그) 안 셈 → 0(시도 1)", code == 0 and classes(report) == ["블록 0"] * 3 + ["보류"] * 3 + ["선택"] and report["attempts"] == 1
              and report["counted_failures"] == 0, f"종료 {code} {classes(report)} {err[-200:]}")
    root = fresh()
    factory.make(root, token_of(18, 41), session="none")
    factory.make(root, token_of(18, 42), session="none", plan_change=lambda plan: plan.__setitem__("block_frames", 301))
    factory.make(root, token_of(18, 43), session="truncated")
    code, report, _, err = run_runs(run, root)
    run.check("runs session 없는·잘린 폴더 미완 2 → 4·sha 다른 폴더 무시(F5)", code == 4 and report and report["exit"] == 4 and report["reason"] == "측정 불가(실행)"
              and classes(report) == ["미완", "미완"] and report["counted_failures"] == 2, f"종료 {code} {classes(report)} {err[-200:]}")
    root = fresh()
    factory.make(root, token_of(17, 0), session="none")
    factory.make(root, token_of(18, 45))
    code, report, out, _ = run_runs(run, root)
    run.check("runs session 없는 폴더 토큰 시각(지역) < 현재 잠금 → 이전 잠금(③만, ② 아님·AM7 L1·F4) + 유효 → 0", code == 0 and classes(report) == ["이전 잠금", "선택"],
              f"종료 {code} {classes(report)} {out.strip()[-160:]}")
    old_judge_rel, new_judge_rel = f"{P_REL}/L0/viscous/judge-plan.json", f"{P_REL}/L0r1/viscous/judge-plan.json"
    relock_out_rel = f"{P_REL}/L0r1/viscous/judge-runs.json"
    root = fresh()
    plan = json.loads((root / RUN_PLAN_REL).read_text(encoding="utf-8"))
    plan["run_label"] = "p-viscous-judge"
    write_json(root / old_judge_rel, plan)
    write_json(root / new_judge_rel, plan)
    (root / LOCKS_REL / "L0.json").unlink()
    write_files_lock(root, "L0.json", local_iso(18, 0), [RUN_PLAN_REL, f"{T_REL}/viscous/table.json"])
    write_files_lock(root, "L1-viscous-1.json", local_iso(18, 10), [old_judge_rel])
    factory.make(root, token_of(18, 20), plan_rel=old_judge_rel, label="p-viscous-judge", session="none", complete=False)
    factory.make(root, token_of(18, 21), plan_rel=old_judge_rel, label="p-viscous-judge", session="truncated", complete=False)
    write_files_lock(root, "L0r1.json", local_iso(18, 30), [RUN_PLAN_REL, f"{T_REL}/viscous/table.json"])
    write_files_lock(root, "L1-viscous-2.json", local_iso(18, 40), [new_judge_rel])
    code, report, out, err = run_runs(run, root, plan_rel=new_judge_rel, lock_name="L1-viscous-2.json", out_rel=relock_out_rel)
    previous = [line for line in out.splitlines() if line.startswith("이전 잠금 ")]
    run.check("runs 재잠금 뒤 옛 계획 경로의 session 없는·잘린 폴더(sha 같음·블록 ≥1) → 이전 잠금 2·6 아님 → 3(AM7 L1)", code == 3 and report is None and len(previous) == 2,
              f"종료 {code} {out.strip()[-200:]} {err[-200:]}")
    factory.make(root, token_of(18, 41), plan_rel=new_judge_rel, label="p-viscous-judge", session="none", complete=False)
    factory.make(root, token_of(18, 42), plan_rel=new_judge_rel, label="p-viscous-judge", session="truncated", complete=False)
    code, report, _, err = run_runs(run, root, plan_rel=new_judge_rel, lock_name="L1-viscous-2.json", out_rel=relock_out_rel)
    run.check("runs 재잠금 뒤 현재 잠금 뒤에 시작한 session 없는·잘린 폴더 → 계속 미완 2 → 4(AM7 L1)", code == 4 and classes(report) == ["이전 잠금", "이전 잠금", "미완", "미완"]
              and report["counted_failures"] == 2, f"종료 {code} {classes(report)} {err[-200:]}")
    root = fresh()
    factory.make(root, token_of(18, 41), complete=False)
    code, report, out, _ = run_runs(run, root)
    first_code, first_report = code, report
    factory.make(root, token_of(18, 42), complete=False)
    code, report, _, err = run_runs(run, root)
    run.check("runs 미완 1 → 3(측정 필요·OUT 없음), 재실행 뒤 미완 2 → 4(디스크 셈 유지)", first_code == 3 and first_report is None and code == 4 and report["counted_failures"] == 2
              and report["reason"] == "측정 불가(실행)", f"종료 {first_code}→{code} {err[-200:]}")
    root = fresh()
    for minute in (41, 42, 43):
        factory.make(root, token_of(18, minute), frame_p50=333.0)
    code, report, _, err = run_runs(run, root)
    run.check("runs A13 3 → 4(측정 불가(실행: 시도 3회), 셈 있는 실패 0)", code == 4 and report["reason"] == "측정 불가(실행: 시도 3회)" and report["counted_failures"] == 0
              and classes(report) == ["A13"] * 3, f"종료 {code} {classes(report)} {err[-200:]}")
    root = fresh()
    factory.make(root, token_of(18, 41), frame_p50=333.0)
    factory.make(root, token_of(18, 42), gpu_max=0.0)
    factory.make(root, token_of(18, 43))
    code, report, _, err = run_runs(run, root)
    run.check("runs A13 2(FrameTime ≥300·GPUTime 전부 0) + 유효 → 0", code == 0 and classes(report) == ["A13", "A13", "선택"] and report["attempts"] == 3,
              f"종료 {code} {classes(report)} {err[-200:]}")
    root = fresh()
    factory.make(root, token_of(18, 41), condition_change=lambda variables: variables.__setitem__("t.OverrideFPS", 30.0))
    factory.make(root, token_of(18, 42), complete=False)
    factory.make(root, token_of(18, 43))
    code, report, _, err = run_runs(run, root)
    run.check("runs conditions 불일치 셈(1) + 미완 1 → 4", code == 4 and classes(report) == ["조건 불일치", "미완"] and report["counted_failures"] == 2, f"종료 {code} {classes(report)} {err[-200:]}")
    root = fresh()
    factory.make(root, token_of(18, 41), asset_change="Content/FluidFX/Fixture/NS_Fixture_VI1.uasset")
    code, report, out, _ = run_runs(run, root)
    run.check("runs ⑤ hash_paths 다름 → 5·OUT 없음", code == 5 and report is None and "hash_paths" in out, f"종료 {code} {out.strip()[-160:]}")
    root = fresh()
    factory.make(root, token_of(18, 45))
    factory.make(root, token_of(18, 41))
    code, report, _, _ = run_runs(run, root)
    run.check("runs 가장 이른 유효 완료 고름", code == 0 and report["selected"]["run_token"] == token_of(18, 41) and classes(report) == ["선택"], f"종료 {code} {classes(report)}")
    root = fresh()
    code, report, out, _ = run_runs(run, root)
    run.check("runs 실행 없음 → 3(측정 필요)·OUT 없음", code == 3 and report is None, f"종료 {code} {out.strip()[-120:]}")
    write_json(root / RUN_OUT_REL, {"exit": 0})
    before = (root / RUN_OUT_REL).read_bytes()
    factory.make(root, token_of(18, 41))
    code, _, _, _ = run_runs(run, root)
    run.check("runs --out 있음 → 2·덮어쓰지 않음", code == 2 and (root / RUN_OUT_REL).read_bytes() == before, f"종료 {code}")
    root = fresh()
    edit_json(root / RUN_PLAN_REL, lambda plan: plan["recorded_variables"].remove("TD.FluidFX.LogTickCost"))
    write_files_lock(root, "L0r1.json", local_iso(18, 40), [RUN_PLAN_REL, LEDGER_REL])
    factory.make(root, token_of(18, 41))
    code, report, _, err = run_runs(run, root, lock_name="L0r1.json")
    run.check("runs 계획 결함(cvars 이름이 recorded에 없음) → 2", code == 2 and report is None and "recorded_variables" in err, f"종료 {code} {err.strip()[-160:]}")
    judge_plan_rel = f"{P_REL}/L0/viscous/judge-plan.json"
    judge_out_rel = f"{P_REL}/L0/viscous/judge-runs.json"
    root = fresh()
    plan = json.loads((root / RUN_PLAN_REL).read_text(encoding="utf-8"))
    plan["run_label"] = "p-viscous-judge"
    write_json(root / judge_plan_rel, plan)
    (root / LOCKS_REL / "L0.json").unlink()
    write_files_lock(root, "L0.json", local_iso(18, 0), [RUN_PLAN_REL, f"{T_REL}/viscous/table.json"])
    write_files_lock(root, "L1-viscous-1.json", local_iso(18, 39), [judge_plan_rel])
    factory.make(root, token_of(18, 41), plan_rel=judge_plan_rel, label="p-viscous-judge")
    judge_root = copy_root(root, parent, "runs-judge-c3")
    code, report, _, err = run_runs(run, root, plan_rel=judge_plan_rel, lock_name="L1-viscous-1.json", out_rel=judge_out_rel)
    run.check("runs 판정 단계 L1 → 0(L1·현재 L0 verify 통과)", code == 0 and report["l0_lock"]["path"] == f"{LOCKS_REL}/L0.json" and report["first_lock"]["path"] == f"{LOCKS_REL}/L1-viscous-1.json",
              f"종료 {code} {err[-200:]}")
    table = judge_root / T_REL / "viscous/table.json"
    table.write_bytes(table.read_bytes() + b" ")
    code, report, out, _ = run_runs(run, judge_root, plan_rel=judge_plan_rel, lock_name="L1-viscous-1.json", out_rel=judge_out_rel)
    run.check("runs 판정 runs 직전 L0 표 1바이트 변경 → 5(C3)·OUT 없음", code == 5 and report is None, f"종료 {code} {out.strip()[-160:]}")


def pilot_scene(scene_id, strong=True, basic=True, error=None, pairs=2, header=None, regime=True):
    return {"scene": scene_id, "regime": {"A": {}, "B": {}, "strong": strong, "basic": basic} if regime else None, "error": error,
            "pairs": [{"index": index} for index in range(pairs)], "header_defects": header or [], "evidence": {}, "sp": {"meta": 100, "readback": 100}}


def write_pilot(area_dir, pilot_exit, scenes=None):
    write_json(area_dir / "pilot1-runs.json", {"exit": pilot_exit, "reason": "fixture", "selected": {"run_dir": "x", "run_token": "1", "started": "s"} if pilot_exit == 0 else None})
    if scenes is not None:
        write_json(area_dir / "pilot1-judge.json", {"mode": "pilot", "run_token": "1", "scenes": {scene["scene"]: scene for scene in scenes}})


def run_need_ladder_cases(run, base, parent):
    root = copy_root(base, parent, "need-ladder")
    counter = [0]

    def need(area_dir):
        counter[0] += 1
        out = area_dir / f"ladder-need-{counter[0]}.json"
        code, _, err = run.run_tool(root, "need-ladder", "--area", "fire", "--area-dir", area_dir.relative_to(root).as_posix(), "--out", out.relative_to(root).as_posix())
        return code, (json.loads(out.read_text(encoding="utf-8")) if out.is_file() else None), err

    old_dir, area_dir = root / P_REL / "L0/fire", root / P_REL / "L0r1/fire"
    write_pilot(old_dir, 0, [pilot_scene("FI1", strong=False), pilot_scene("FI5a")])
    write_pilot(area_dir, 4)
    code, result, err = need(area_dir)
    run.check("need-ladder 판정 없음(시운전 4)·옛 L0 폴더에만 판정(R4) → 거짓", code == 0 and result["needed"] is False and result["pilot_judge"] is None, f"종료 {code} {err[-160:]}")
    code, result, _ = need(old_dir)
    run.check("need-ladder SP100 강 미달 → 참·strong_fail·pilot_judge sha", code == 0 and result["needed"] is True and result["strong_fail"] == ["FI1"]
              and len(result["pilot_judge"]["sha256"]) == 64, f"종료 {code} {result}")
    write_pilot(area_dir, 0, [pilot_scene("FI1", regime=False), pilot_scene("FI5a", regime=False)])
    code, result, _ = need(area_dir)
    run.check("need-ladder ft 없음(regime 전부 null) → 거짓", code == 0 and result["needed"] is False, f"종료 {code} {result}")
    write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False, regime=False), pilot_scene("FI5a")])
    code, result, _ = need(area_dir)
    run.check("need-ladder regime null 장면 제외(강 통과만 남음) → 거짓", code == 0 and result["needed"] is False and result["strong_fail"] == [], f"종료 {code} {result}")
    write_pilot(area_dir, 0, [pilot_scene("FI1"), pilot_scene("FI5a", header=["GPUTime"])])
    code, result, err = need(area_dir)
    run.check("need-ladder 핵심 header_defects → 2·출력 없음", code == 2 and result is None and "header_defects" in err, f"종료 {code} {err.strip()[-160:]}")
    (area_dir / "pilot1-judge.json").unlink()
    write_pilot(area_dir, 0)
    code, result, _ = need(area_dir)
    run.check("need-ladder 시운전 exit 0인데 판정 없음 → 2", code == 2 and result is None, f"종료 {code}")
    write_pilot(old_dir, 0, [pilot_scene("FI1"), pilot_scene("FI5a")])
    write_json(old_dir / "taken.json", {})
    code, _, _ = run.run_tool(root, "need-ladder", "--area", "fire", "--area-dir", old_dir.relative_to(root).as_posix(), "--out", (old_dir / "taken.json").relative_to(root).as_posix())
    run.check("need-ladder --out 있음 → 2", code == 2, f"종료 {code}")


def ladder_scenes(scene_id, statuses):
    return [pilot_scene(f"{scene_id}_sp{percentage}", strong=strong, basic=basic, error=error) for percentage, (strong, basic, error) in statuses.items()]


def write_ladder(area_dir, ladder_exit, scenes=None):
    write_json(area_dir / "ladder-need.json", {"area": "fire", "needed": True})
    write_json(area_dir / "ladder-runs.json", {"exit": ladder_exit, "reason": "fixture", "selected": None})
    if scenes is not None:
        write_json(area_dir / "ladder-judge.json", {"mode": "pilot", "run_token": "2", "scenes": {scene["scene"]: scene for scene in scenes}})


def run_select_sp_cases(run, base, parent):
    counter = [0]

    def select(prepare):
        counter[0] += 1
        root = copy_root(base, parent, f"select-{counter[0]:02d}")
        area_dir = root / P_REL / "L0/fire"
        area_dir.mkdir(parents=True, exist_ok=True)
        write_json(area_dir / "ladder-need.json", {"area": "fire", "needed": False})
        prepare(area_dir)
        relative = area_dir.relative_to(root).as_posix()
        code, out, err = run.run_tool(root, "select-sp", "--area", "fire", "--area-dir", relative, "--out-plan", f"{relative}/judge-plan.json", "--out", f"{relative}/sp-choice.json")
        choice = json.loads((area_dir / "sp-choice.json").read_text(encoding="utf-8")) if (area_dir / "sp-choice.json").is_file() else None
        plan = json.loads((area_dir / "judge-plan.json").read_text(encoding="utf-8")) if (area_dir / "judge-plan.json").is_file() else None
        return code, choice, plan, err, root

    def scene_sp(choice, scene_id):
        return (choice["scenes"][scene_id]["sp"], choice["scenes"][scene_id]["preset"]) if choice else None

    def strong_first(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False, basic=True), pilot_scene("FI5a")])
        write_ladder(area_dir, 0, ladder_scenes("FI1", {130: (False, True, None), 160: (True, True, None), 200: (True, True, None)}))

    code, choice, plan, err, root = select(strong_first)
    by_id = {scene["id"]: scene for scene in plan["scenes"]} if plan else {}
    pilot = json.loads((root / T_REL / "fire/pilot1.json").read_text(encoding="utf-8"))
    run.check("select-sp 강 먼저(SP130 기본만·SP160 강 → 160)·강 통과 장면 SP100", code == 0 and scene_sp(choice, "FI1") == (160, None) and scene_sp(choice, "FI5a") == (100, None),
              f"종료 {code} {choice['scenes'] if choice else err[-200:]}")
    run.check("select-sp 판정 계획: run_label·repeats 5/5·강 미달 장면만 SP 단계·meta 변경·ID 불변",
              plan and plan["run_label"] == "p-fire-judge" and plan["repeats"] == 5 and plan["unstable_repeats"] == 5 and list(by_id) == [scene["id"] for scene in pilot["scenes"]]
              and by_id["FI1"]["setup"][-1] == {"action": "console", "commands": ["r.ScreenPercentage 160"], "restore": True} and by_id["FI1"]["meta"]["screen_percentage"] == 160
              and by_id["FI5a"] == pilot["scenes"][1] and choice["inputs"].get(f"{T_REL}/fire/pilot1.json") == sha256_file(root / T_REL / "fire/pilot1.json"),
              f"{plan.get('run_label') if plan else None}")

    def basic_next(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False, basic=False), pilot_scene("FI5a", strong=False, basic=True)])
        write_ladder(area_dir, 0, ladder_scenes("FI1", {130: (False, False, None), 160: (False, True, None), 200: (False, True, None)})
                     + ladder_scenes("FI5a", {130: (False, False, None), 160: (False, False, None), 200: (False, False, None)}))

    code, choice, _, err, _ = select(basic_next)
    run.check("select-sp 기본 다음(FI1 SP160 기본)·SP100 기본 통과면 100(FI5a)", code == 0 and scene_sp(choice, "FI1") == (160, None) and scene_sp(choice, "FI5a") == (100, None),
              f"종료 {code} {choice['scenes'] if choice else err[-200:]}")

    def all_fail(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False, basic=False), pilot_scene("FI5a")])
        write_ladder(area_dir, 0, ladder_scenes("FI1", {130: (False, False, None), 160: (False, False, None), 200: (False, False, None)}))

    code, choice, plan, err, _ = select(all_fail)
    run.check("select-sp 넷 다 기본 미달 → preset 판정 불가(체제)·SP100", code == 0 and scene_sp(choice, "FI1") == (100, "판정 불가(체제)")
              and plan["scenes"][0]["meta"]["screen_percentage"] == 100, f"종료 {code} {choice['scenes'] if choice else err[-200:]}")
    code, choice, _, err, _ = select(lambda area_dir: write_pilot(area_dir, 4))
    run.check("select-sp 시운전 없음(runs 4) → 전부 SP100·preset 없음", code == 0 and scene_sp(choice, "FI1") == (100, None) and scene_sp(choice, "FI5a") == (100, None),
              f"종료 {code} {err[-200:]}")

    def ladder_final(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False), pilot_scene("FI5a")])
        write_ladder(area_dir, 4)

    code, choice, _, err, _ = select(ladder_final)
    run.check("select-sp 사다리 4 → 강 미달 장면만 측정 불가(실행)(C6)", code == 0 and scene_sp(choice, "FI1") == (100, "측정 불가(실행)") and scene_sp(choice, "FI5a") == (100, None),
              f"종료 {code} {choice['scenes'] if choice else err[-200:]}")

    def ladder_scene_error(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False, basic=False), pilot_scene("FI5a")])
        write_ladder(area_dir, 0, ladder_scenes("FI1", {130: (True, True, "장면 오류"), 160: (True, True, None), 200: (True, True, None)}))

    code, choice, _, err, _ = select(ladder_scene_error)
    run.check("select-sp 사다리 장면 오류 SP는 미달(S11) → 다음 SP160", code == 0 and scene_sp(choice, "FI1") == (160, None), f"종료 {code} {choice['scenes'] if choice else err[-200:]}")

    def pilot_header(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1"), pilot_scene("FI5a", header=["FrameTime"])])

    code, choice, plan, err, _ = select(pilot_header)
    run.check("select-sp 시운전 header_defects → 2·출력 없음", code == 2 and choice is None and plan is None, f"종료 {code} {err.strip()[-160:]}")

    def ladder_header(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False), pilot_scene("FI5a")])
        scenes = ladder_scenes("FI1", {130: (True, True, None), 160: (True, True, None), 200: (True, True, None)})
        scenes[0]["header_defects"] = ["GPUTime"]
        write_ladder(area_dir, 0, scenes)

    code, choice, _, err, _ = select(ladder_header)
    run.check("select-sp 사다리 header_defects → 2", code == 2 and choice is None, f"종료 {code} {err.strip()[-160:]}")

    def ladder_missing(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1", strong=False), pilot_scene("FI5a")])

    code, choice, _, err, _ = select(ladder_missing)
    run.check("select-sp 강 미달인데 ladder-runs 없음 → 2", code == 2 and choice is None, f"종료 {code} {err.strip()[-160:]}")

    def interrupted(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1"), pilot_scene("FI5a")])
        write_text(area_dir / "judge-plan.json", '{"run_label": "p-fire-ju')

    code, choice, plan, err, _ = select(interrupted)
    run.check("select-sp judge-plan만 남은 중단 → 0(부속 다시 씀)", code == 0 and choice is not None and plan["run_label"] == "p-fire-judge", f"종료 {code} {err[-200:]}")

    def taken(area_dir):
        write_pilot(area_dir, 0, [pilot_scene("FI1"), pilot_scene("FI5a")])
        write_json(area_dir / "sp-choice.json", {"area": "fire"})

    code, choice, plan, _, _ = select(taken)
    run.check("select-sp --out 있음 → 2·judge-plan 쓰지 않음", code == 2 and plan is None and choice == {"area": "fire"}, f"종료 {code}")


def write_area_result(root, folder, area, token, runs_exit=0, runs_reason=None):
    area_dir = root / P_REL / folder / area
    selected = {"run_dir": f"{RUNS_REL}/p-{area}-judge-{token}", "run_token": token, "started": "2026-11-06T19:00:00+09:00"} if runs_exit == 0 else None
    write_json(area_dir / "judge-runs.json", {"plan": f"{P_REL}/{folder}/{area}/judge-plan.json", "lock": f"{LOCKS_REL}/L1-{area}-1.json", "exit": runs_exit,
                                              "reason": runs_reason or "가장 이른 유효 완료(시도 1)", "selected": selected})
    return area_dir


def judge_row_document(row, scene_id, verdict="통과", reason=None):
    return {"row": row["id"], "ledger_row": row["id"], "scene": scene_id, "metric": row["metric"], "verdict": verdict, "reason": reason, "interval": [0.01, 0.02],
            "d": 0.015, "d_prime": 0.014, "sp": 100, "viewport": [1920, 1080], "rounds": 3, "pairs": 6, "p95_record": 0.03, "backend": "CPU", "limit_ms": row["limit_ms"], "rt_control": None}


def write_summary_tree(root, verdicts=None, runs_final=None, presets=None, table_shas=None):
    verdicts, runs_final, presets, table_shas = verdicts or {}, runs_final or {}, presets or {}, table_shas or {}
    areas = json.loads((root / AREAS_REL).read_text(encoding="utf-8"))
    write_files_lock(root, "L0.json", local_iso(18, 0), [f"{T_REL}/{area}/table.json" for area in areas["areas_order"]])
    for index, area in enumerate(areas["areas_order"]):
        token = f"202611061{index}0000"
        area_dir = write_area_result(root, "L0", area, token, 4 if area in runs_final else 0, runs_final.get(area))
        scenes = [scene for scene in areas["scenes"] if scene["area"] == area]
        write_json(area_dir / "sp-choice.json", {"area": area, "inputs": {}, "scenes": {scene["id"]: {"sp": 100, "basis": "fixture", "preset": presets.get(scene["id"])} for scene in scenes}})
        if area in runs_final:
            continue
        rows = {row["id"]: judge_row_document(row, scene["id"], *verdicts.get(row["id"], ("통과", None))) for scene in scenes for row in scene["rows"]}
        table_rel = f"{T_REL}/{area}/table.json"
        write_json(area_dir / "judge.json", {"mode": "judge", "run_dir": f"{RUNS_REL}/p-{area}-judge-{token}", "run_token": token,
                                             "budget_table": {"path": table_rel, "sha256": table_shas.get(area, sha256_file(root / table_rel)), "area": area}, "rows": rows})
    write_json(root / "Saved/FluidFX/calib/final/expect.json", {"rows": [{"source": {"judge": "Saved/FluidFX/calib/batch-K3/runs/cloud_g11_cyclone_live/confirm-1/judge.json"}},
                                                                        {"source": {"judge": "Saved/FluidFX/calib/batch-R5/runs/fog_f2_passive/confirm-1/judge.json"}}]})
    edit_json(root / "Tools/FluidFX/calib/plans/cloud_g11_cyclone_live.json", lambda plan: plan.__setitem__("metrics", {"targets": [
        {"basis": "live2_image"}, {"basis": "spec_proposal"}, {"basis": "자유 문안 근거"}, {}]}))


def run_summary(run, root, approvals="Saved/FluidFX/approval/approvals.json"):
    out_rel, md_rel = f"{P_REL}/p-summary.json", f"{P_REL}/p-summary.md"
    code, out, err = run.run_tool(root, "summary", "--p-dir", P_REL, "--areas", AREAS_REL, "--approvals", approvals, "--out", out_rel, "--md", md_rel)
    summary = json.loads((root / out_rel).read_text(encoding="utf-8")) if (root / out_rel).is_file() else None
    return code, summary, err, root / md_rel


def row_label(summary, row_id):
    return next(row["label"] for area in summary["areas"].values() for row in area["rows"] if row["row"] == row_id)


def t4_compatible(summary):
    pattern = re.compile(r"(통과|미달|판정 불가|측정 불가|출하 밖) (\d+)")
    for area, entry in summary["areas"].items():
        counts = entry["counts"]
        if set(counts) != {"통과", "미달", "판정 불가", "측정 불가", "출하 밖"} or any(int(number) != counts[key] for key, number in pattern.findall(entry["overall"])):
            return False
        if (entry["table_sha256"] is None) != (area == "foliage"):
            return False
    return True


def run_summary_cases(run, base, parent):
    counter = [0]

    def scenario(prepare=None, **tree):
        counter[0] += 1
        root = copy_root(base, parent, f"summary-{counter[0]:02d}")
        write_summary_tree(root, **tree)
        if prepare:
            prepare(root)
        return run_summary(run, root)

    code, summary, err, md = scenario()
    fire = summary["areas"]["fire"] if summary else {}
    run.check("summary 양성: 승인 파일 없음 → 9영역 승인 대기·A10 부분(각 측정 불가가 막음 1)·T4 형식(counts 5키·종합 수·foliage 표 sha null)·md",
              code == 0 and all(entry["approval"]["state"] == "승인 대기" for entry in summary["areas"].values()) and list(summary["areas"]) == ["fire", "smoke", "fog", "water", "cloud", "input", "track", "viscous", "foliage"]
              and fire["overall"] == "부분(통과 4 / 행 4 / 판정 불가 0 / 측정 불가 1 / 출하 밖 0)" and fire["blocking_unmeasurable"] == 1 and t4_compatible(summary) and md.is_file()
              and summary["areas"]["foliage"]["overall"] == "부분(통과 0 / 행 0 / 판정 불가 0 / 측정 불가 1 / 출하 밖 0)", f"종료 {code} {fire.get('overall')} {err[-300:]}")
    run.check("summary 행별 필드·처분 '각' 펼침·기록 메타·FO3 통과(SM2/ft 통과)·PB2:214 열·K 경로·띠 basis",
              summary and row_label(summary, "FO3/ft") == "통과" and [line["id"] for line in summary["areas"]["water"]["lines"]] == ["S009", "S015"]
              and summary["areas"]["fire"]["records"][0]["id"] == "S017" and summary["areas"]["fire"]["rows"][0]["d_prime"] == 0.014 and summary["areas"]["fire"]["rows"][0]["count"] == 2
              and list(summary["pb2_214"][0]) == ["영역", "채택 수", "검증만", "둔감", "채택 없음", "conflict", "deferred", "성능", "승인 상태", "날짜", "근거 경로", "표기"]
              and summary["areas"]["cloud"]["pb2_214"]["채택 수"] == 1 and summary["areas"]["cloud"]["k_judges"] == ["Saved/FluidFX/calib/batch-K3/runs/cloud_g11_cyclone_live/confirm-1/judge.json"]
              and summary["areas"]["cloud"]["band_basis"] == {"LIVE-2 이미지": 1, "[제안] 동등 증명 아님(P-11)": 1, "자유 문안": 1, "근거 미표기": 1, "계획 없음": 0}
              and summary["areas"]["fog"]["band_basis"]["계획 없음"] == 1 and summary["pending_judgements"]["items"][0] == "W-2s-a 젖은 띠·레이스",
              f"{summary['areas']['cloud']['band_basis'] if summary else None}")
    absent_columns = ("검증만", "둔감", "채택 없음", "conflict", "deferred")
    run.check("summary PB2:214 verdict-w11에 출처 없는 다섯 열 = null·pb2_214_note·md 표기(반박 M2, 부모 결정 대기)",
              summary and all(row[name] is None for row in summary["pb2_214"] for name in absent_columns) and all(name in summary["pb2_214_note"] for name in absent_columns)
              and summary["pb2_214_note"] in md.read_text(encoding="utf-8"), f"{summary.get('pb2_214_note') if summary else None}")
    run.check("summary pb2_fxp_sources 키 있음(원본 없으면 [])·md '5열 FX-P 집계(AM7 L3)' 1줄", summary and summary.get("pb2_fxp_sources") == []
              and sum("5열 FX-P 집계(AM7 L3)" in line for line in md.read_text(encoding="utf-8").splitlines()) == 1, f"{summary.get('pb2_fxp_sources') if summary else None}")

    def fxp_sources(root):
        calib = root / "Saved/FluidFX/calib"
        for relative in ("batch/queue-R2.json", "batch-R5/queue-R5.json", "batch/journal-K1.json", "batch-K2/journal.json", "batch-K2/runs/x/journal.json",
                         "final/journal.json", "batch/queue.json", "batch-K2/journal.txt"):
            write_json(calib / relative, {"fixture": relative})

    code, summary, err, md = scenario(fxp_sources)
    expected_sources = ["Saved/FluidFX/calib/batch-K2/journal.json", "Saved/FluidFX/calib/batch-R5/queue-R5.json", "Saved/FluidFX/calib/batch/journal-K1.json",
                        "Saved/FluidFX/calib/batch/queue-R2.json"]
    run.check("summary pb2_fxp_sources = batch*/journal*.json·batch*/queue-*.json 저장소 상대 POSIX 정렬, 다섯 열 null 유지(AM7 L3)",
              code == 0 and summary["pb2_fxp_sources"] == expected_sources and all(row[name] is None for row in summary["pb2_214"] for name in absent_columns)
              and "5열 FX-P 집계(AM7 L3): 원본 4개" in md.read_text(encoding="utf-8"), f"종료 {code} {summary.get('pb2_fxp_sources') if summary else None} {err[-200:]}")
    code, summary, err, _ = scenario(lambda root: edit_areas(root, lambda areas: item_of(areas, "S015").__setitem__("area", "foliage")))
    run.check("summary A10 충족(proposed): '각' 측정 불가가 없으면 viscous 충족·표기 [제안]", code == 0 and summary["areas"]["viscous"]["overall"] == "충족(proposed)"
              and summary["areas"]["viscous"]["pb2_214"]["표기"] == "[제안] 기준 충족(동등 증명 아님)" and t4_compatible(summary), f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(verdicts={"TR1/ft": ("미달", None), "SM2/ft": ("미달", None)})
    run.check("summary A10 미충족·FO3 SM2/ft 미달 → 판정 불가(HVOL 대안 측정 불가; SM2/ft 미달)",
              code == 0 and summary["areas"]["track"]["overall"] == "미충족(통과 0 / 미달 1 / 행 1 / 판정 불가 0 / 측정 불가 1 / 출하 밖 0)"
              and row_label(summary, "FO3/ft") == "판정 불가(HVOL 대안 측정 불가; SM2/ft 미달)" and t4_compatible(summary), f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(verdicts={"SM2/ft": ("판정 불가", "걸침")})
    run.check("summary FO3 SM2/ft 판정 불가(걸침) → FO3 판정 불가(…; SM2/ft 판정 불가(걸침))", code == 0 and row_label(summary, "SM2/ft") == "판정 불가(걸침)"
              and row_label(summary, "FO3/ft") == "판정 불가(HVOL 대안 측정 불가; SM2/ft 판정 불가(걸침))", f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(runs_final={"smoke": "측정 불가(실행)"})
    run.check("summary FO3 smoke judge-runs 4 → SM2/ft 측정 불가(실행) → FO3 판정 불가(…; SM2/ft 측정 불가(실행))", code == 0 and row_label(summary, "HV2/ft") == "측정 불가(실행)"
              and row_label(summary, "FO3/ft") == "판정 불가(HVOL 대안 측정 불가; SM2/ft 측정 불가(실행))", f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(table_shas={"water": "f" * 64})
    run.check("summary C3 표 sha 다름 → 영역 행 전부 판정 불가(표 불일치)", code == 0 and row_label(summary, "WA3/gt") == "판정 불가(표 불일치)"
              and summary["areas"]["water"]["table_check"]["matches"] is False and row_label(summary, "CL1/ft") == "통과", f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(runs_final={"fire": "측정 불가(실행: 시도 3회)"}, presets={"FI1": "판정 불가(체제)", "SM2": "판정 불가(체제)"},
                                     table_shas={"smoke": "e" * 64})
    run.check("summary 순서(앞이 이김, S11): judge-runs 4 > preset > C3, FO3는 최종 SM2/ft 판정에서",
              code == 0 and [row["label"] for row in summary["areas"]["fire"]["rows"]] == ["측정 불가(실행: 시도 3회)"] * 4
              and row_label(summary, "SM2/ft") == "판정 불가(체제)" and row_label(summary, "HV2/ft") == "판정 불가(표 불일치)"
              and row_label(summary, "FO3/ft") == "판정 불가(HVOL 대안 측정 불가; SM2/ft 판정 불가(체제))", f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(presets={"CL1": "측정 불가(실행)", "TR1": "판정 불가(체제)"})
    run.check("summary sp-choice preset은 ft 행만(CL1/ft 측정 불가(실행)·TR1/ng 유지)", code == 0 and row_label(summary, "CL1/ft") == "측정 불가(실행)"
              and row_label(summary, "TR1/ft") == "판정 불가(체제)" and row_label(summary, "TR1/ng") == "통과", f"종료 {code} {err[-200:]}")
    code, summary, err, md = scenario(verdicts={"IN1/ft": ("판정 불가", "열 결함: TDFluid/RenderThread/MassInputSplatCalls")})
    run.check("summary 열 결함 이름(E8) 기록: T2 실제 reason '열 결함: <이름>'(괄호 없음, AM7 L2)",
              code == 0 and summary["areas"]["input"]["column_defects"] == {"evidence_names": ["TDFluid/RenderThread/MassInputSplatCalls"], "core_rows": []}
              and row_label(summary, "IN1/ft") == "판정 불가(열 결함: TDFluid/RenderThread/MassInputSplatCalls)"
              and "- input: 증거 열 ['TDFluid/RenderThread/MassInputSplatCalls']" in md.read_text(encoding="utf-8"), f"종료 {code} {summary['areas']['input']['column_defects'] if summary else None} {err[-200:]}")
    code, summary, err, _ = scenario(verdicts={"TR1/ft": ("판정 불가", "열 결함: TDFluid/RenderThread/MassInputSplatCalls, TDFluid/AllWorkers/MassInputSplatCalls"),
                                               "TR1/ng": ("판정 불가", "열 결함 의심(PR-4)")})
    run.check("summary 열 결함 이름 둘(', '로 나눔·정렬)·'열 결함 의심'은 이름 아닌 핵심 열 행(AM7 L2)",
              code == 0 and summary["areas"]["track"]["column_defects"] == {"evidence_names": ["TDFluid/AllWorkers/MassInputSplatCalls", "TDFluid/RenderThread/MassInputSplatCalls"],
                                                                            "core_rows": ["TR1/ng"]}, f"종료 {code} {summary['areas']['track']['column_defects'] if summary else None} {err[-200:]}")
    code, summary, err, _ = scenario(verdicts={"IN1/ft": ("판정 불가", "판정 불가(열 결함: TDFluid/RenderThread/MassInputSplatCalls)")})
    run.check("summary 열 결함 괄호 형식 '(열 결함: <이름>)'도 받음(AM7 L2)",
              code == 0 and summary["areas"]["input"]["column_defects"]["evidence_names"] == ["TDFluid/RenderThread/MassInputSplatCalls"], f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(verdicts={"IN1/ft": ("판정 불가", "판정 불가(열 결함: TDFluid/A, TDFluid/GameThread/B)")})
    run.check("summary 열 결함 괄호 형식 이름 둘 '(열 결함: a, b)'도 ', '로 나눔·정렬(AM7 L2)",
              code == 0 and summary["areas"]["input"]["column_defects"] == {"evidence_names": ["TDFluid/A", "TDFluid/GameThread/B"], "core_rows": []},
              f"종료 {code} {summary['areas']['input']['column_defects'] if summary else None} {err[-200:]}")

    def defects(root):
        write_text(root / P_REL / "defects/D1.md", "# D1\n- fixture: x\n- 사후 결함 행: TR1/ng\n")
        write_text(root / P_REL / "defects/D2.md", "# D2 (가) 분석 전 결함\n- 영향 행: VI1/ng\n")

    code, summary, err, _ = scenario(defects)
    run.check("summary 사후 결함: '사후 결함 행:' 줄의 행만 판정 불가(사후 결함 D1), FX-P 항목", code == 0 and row_label(summary, "TR1/ng") == "판정 불가(사후 결함 D1)"
              and row_label(summary, "VI1/ng") == "통과" and [entry["defect"] for entry in summary["fxp_items"]["post_hoc_defects"]] == ["D1"], f"종료 {code} {err[-200:]}")

    def approvals(root):
        write_json(root / "Saved/FluidFX/approval/fog/current.json", {"area": "fog", "revision": 1, "sheet_json_sha256": "a" * 64})
        write_json(root / "Saved/FluidFX/approval/cloud/current.json", {"area": "cloud", "revision": 2, "sheet_json_sha256": "c" * 64})
        write_json(root / "Saved/FluidFX/approval/approvals.json", {"areas": {
            "fog": {"decision": "승인", "date": "2026-11-07", "sheet_json": "Saved/FluidFX/approval/fog/r1/sheet.json", "sheet_json_sha256": "a" * 64, "text": "fixture"},
            "cloud": {"decision": "거부", "date": "2026-11-07", "sheet_json": "Saved/FluidFX/approval/cloud/r1/sheet.json", "sheet_json_sha256": "b" * 64, "text": "fixture"}}})

    code, summary, err, _ = scenario(approvals)
    run.check("summary 승인 sha 같음 → 승인 <날짜>, 다름 → 승인 대기(A19)", code == 0 and summary["areas"]["fog"]["approval"]["state"] == "승인 2026-11-07"
              and summary["areas"]["cloud"]["approval"]["state"] == "승인 대기" and summary["areas"]["fog"]["pb2_214"]["날짜"] == "2026-11-07", f"종료 {code} {err[-200:]}")
    code, summary, err, _ = scenario(lambda root: (root / "Saved/FluidFX/calib/final/verdict-w11.json").unlink())
    run.check("summary PB2:214 열 빠짐(verdict-w11 없음) → 2·출력 없음", code == 2 and summary is None and "PB2:214" in err, f"종료 {code} {err.strip()[-160:]}")
    code, summary, err, _ = scenario(lambda root: write_json(root / VERDICT_REL, {"adoptions": []}))
    run.check("summary PB2:214 열 빠짐(verdict-w11 adoptions 객체 아님 → 채택 수 못 셈) → 2·출력 없음", code == 2 and summary is None and "PB2:214 열 빠짐" in err,
              f"종료 {code} {err.strip()[-160:]}")
    code, summary, err, _ = scenario(lambda root: edit_json(root / VERDICT_REL, lambda verdict: verdict["adoptions"].__setitem__(
        "99", {"state_source": {"item_plan": "Tools/FluidFX/calib/plans/zz_unknown.json"}})))
    run.check("summary PB2:214 열 빠짐(채택 계획 영역 모름 → 영역별 채택 수 못 셈) → 2·출력 없음", code == 2 and summary is None and "채택 99" in err,
              f"종료 {code} {err.strip()[-160:]}")

    def second_done(root):
        write_area_result(root, "L0r1", "fire", "20261106200000", 4, "측정 불가(실행)")

    code, summary, err, _ = scenario(second_done)
    run.check("summary 끝난 폴더 둘 이상 → 2", code == 2 and summary is None, f"종료 {code} {err.strip()[-160:]}")
    code, summary, err, _ = scenario(lambda root: (root / P_REL / "L0/viscous/judge-runs.json").unlink())
    run.check("summary 8영역 중 끝난 폴더 없음 → 2", code == 2 and summary is None, f"종료 {code} {err.strip()[-160:]}")
    code, summary, err, _ = scenario(lambda root: write_json(root / P_REL / "p-summary.json", {"x": 1}))
    run.check("summary --out 있음 → 2·덮어쓰지 않음", code == 2 and summary == {"x": 1}, f"종료 {code}")


def run_status_cases(run, base, parent):
    root = copy_root(base, parent, "status")

    def status(area):
        code, out, err = run.run_tool(root, "status", "--area", area, "--p-dir", P_REL)
        return code, out.strip()

    area_dir = write_area_result(root, "L0", "fire", "20261106190000")
    write_json(area_dir / "judge.json", {"run_token": "20261106190000", "rows": {}})
    write_area_result(root, "L0", "smoke", "x", 4, "측정 불가(실행)")
    area_dir = write_area_result(root, "L0", "fog", "20261106190000")
    write_json(area_dir / "judge.json", {"run_token": "20261106180000", "rows": {}})
    results = [status("fire"), status("smoke"), status("fog"), status("water")]
    run.check("status run_token 일치 done·judge-runs 4 done·run_token 다름 todo·없음 todo", results == [(0, "done"), (0, "done"), (0, "todo"), (0, "todo")], f"{results}")
    write_area_result(root, "L0r1", "smoke", "x", 4, "측정 불가(실행)")
    code, out = status("smoke")
    run.check("status 끝난 폴더 둘 → 2", code == 2 and not out, f"종료 {code} {out}")
    code, _, _ = run.run_tool(root, "status", "--area", "fire", "--p-dir", f"{P_REL}/없음")
    run.check("status --p-dir 없음 → 2", code == 2, f"종료 {code}")


def load_module(tool):
    specification = importlib.util.spec_from_file_location("p_plans_under_test", tool)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def run_sub_fixtures(run, tool_root):
    codes = []
    for name in SUB_FIXTURES:
        path = tool_root / T_REL / name
        if not path.is_file():
            print(f"건너뜀 하위 fixture {name}: 파일 없음({T_REL}/{name})", flush=True)
            continue
        completed = subprocess.run([sys.executable, "-B", str(path)], cwd=str(tool_root), capture_output=True, env=dict(os.environ, PYTHONIOENCODING="utf-8"), timeout=3600)
        for stream in (completed.stdout, completed.stderr):
            text = stream.decode("utf-8", errors="replace")
            if text.strip():
                print(f"[{name}]\n{text}", file=sys.stderr, flush=True)
        codes.append((name, completed.returncode))
        run.check(f"하위 fixture {name} 종료 0", completed.returncode == 0, f"종료 {completed.returncode}")
    return codes


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="runbook-p-v3 T3.10 fixture")
    parser.add_argument("--root", default=str(REPO_ROOT))
    tool_root = Path(parser.parse_args().root).resolve()
    tool = tool_root / "Tools/FluidFX/perf/p_plans.py"
    module = load_module(tool)
    run = FixtureRun(tool)
    with tempfile.TemporaryDirectory(prefix="p_fixtures_t3_", ignore_cleanup_errors=True) as temporary:
        parent = Path(temporary)
        base = parent / "base"
        make_base_root(base, tool_root, module)
        run_build_cases(run, base, parent, module)
        run_sheet_attribution_cases(run, base, parent)
        run_current_lock_cases(run, base, parent)
        run_lock_list_cases(run, base, parent)
        run_preflight_cases(run, base, parent, module)
        run_runs_cases(run, base, parent, RunFactory(load_pie_merge(tool_root)))
        run_need_ladder_cases(run, base, parent)
        run_select_sp_cases(run, base, parent)
        run_status_cases(run, base, parent)
        run_summary_cases(run, base, parent)
    run_sub_fixtures(run, tool_root)
    passed = sum(1 for _, ok in run.results if ok)
    print(f"P fixture 통과 {passed}/{len(run.results)}")
    return 0 if passed == len(run.results) else 1


if __name__ == "__main__":
    sys.exit(main())

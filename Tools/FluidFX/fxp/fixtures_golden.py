"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 사본(헤더·골든 시험·스냅숏 생성기·expect·overlay·일지)에서 fxp_audit.py golden 을 돌려 정상(골든 형식 6종·골든 경로·상위 구조체·리터럴 ≠ 형식·골든 줄·PD-6 최종만 대조·actor_prop 사전 키)·같은 바이트·종료 1 여섯(비트 필드·모르는 엔진 속성·모르는 종류·일지 채택 expect 밖·일지 overlay 쓰기 없음·overlay에 없는 채택)·종료 2 둘을 본다.
상태: 2026-11-07 FX-P X1b(B2).
"""
import copy
import shutil
from pathlib import Path

from check_fxp_fixtures import FixtureLog, run_audit, write_json, write_text

GOLDEN_TEST = "Source/TDGame/FluidFX/Tests/TDFluidSolverGoldenHashTests.cpp"
GENERATOR = "Tools/FluidFX/make_fluid_golden_snapshots.py"
TYPES_HEADER = "Source/TDGame/FluidFX/TDFluidTypes.h"
CLOUD_HEADER = "Source/TDGame/FluidFX/TDFluidCloudTypes.h"
TUNING = "Content/FluidFX/PresetData/TDFluidPresetTuning.json"
EXPECT = "Saved/FluidFX/calib/final/expect.json"
OVERLAY = "Saved/FluidFX/calib/final/overlay-frozen.json"
PLANS = "Tools/FluidFX/calib/plans"
LIGHT = {"level": "/Game/Maps/L_Fixture", "label": "TDGen_Light", "component": "light_component", "property": "volumetric_scattering_intensity"}
LIGHT_KEY = "actor_prop:/Game/Maps/L_Fixture/TDGen_Light.light_component.volumetric_scattering_intensity"
HVOL_KEY = "Look:Materials./Game/FluidFX/Materials/MI_Hvol.Scalars.VolExtrudeHeightCm"
ADOPTIONS = {
    "1": ("Kinds.Flamethrower.TDFluidSimulationConfig.HeatCoolingPerSec", 4, "fx5_flamethrower"),
    "2": ("Kinds.Water.TDFluidSimulationConfig.JacobiIterations", 8, "cloud_g9_mushroom"),
    "3": ("Look:NiagaraSystems./Game/FluidFX/Niagara/NS_Fixture.Bools.User.bFlag", True, "cloud_g9_mushroom"),
    "4": ("Groups.WaterLayer.TDFluidSimulationConfig.Mode", "On", "fog_f1_layer"),
    "5": ("Look:Materials./Game/FluidFX/Materials/MI_Fixture.Vectors.Tint", [1.0, 0.0, 0.0, 1.0], "w5_underwater_l18a"),
    "6": ("Globals.TDFluidCloudSceneSettings.WindCurtainSpeedCmPerSec", 75, "cloud_g13_storm_wall"),
    "7": (LIGHT_KEY, 8.0, "fog_f7_lights"),
    "8": ("Kinds.SnowTrack.TDFluidChildSettings.HeatCoolingPerSec", 1.5, "track_fx12c_groove"),
}
EXPECTED_TYPES = {"1": "실수", "2": "정수", "3": "불", "4": "열거", "5": "벡터", "6": "실수", "7": "실수", "8": "실수"}
EXPECTED_SUMMARY = ("SUMMARY adoptions=8 influences=1 discrete=3 golden_path=2 scenarios=2 unknown_types=0 not_in_overlay=0 journals=3 journal_adopted=8 "
                    "journal_unmatched=0 problems=0")
EXPECTED_LINE = "채택된 열거·불·정수 분기는 골든이 덮지 않는다(RP-S1): 채택 8(W11 overlay 채택, 영향 f5 제외) 중 열거·불·정수 3, 골든 경로 2; CPU 솔버 2시나리오 회귀만 보증"

GOLDEN_TEST_TEXT = """IMPLEMENT_SIMPLE_AUTOMATION_TEST(
\tFTDFluidSolverGoldenStateChecksumTest,
\t"TDGame.FluidFX.SolverRegression.GoldenStateChecksums",
\tEAutomationTestFlags::EditorContext)

\tconst FTDFluidGoldenScenario GoldenScenarios[] = {
\t\t{ TEXT("WaterFixture"), ETDFluidPresetKind::Water, 96 },
\t\t{ TEXT("MudFixture"), ETDFluidPresetKind::Mud, 64 },
\t};
"""
GENERATOR_TEXT = 'DEFAULT_STRUCTS = ("TDFluidSimulationConfig",)\n'
TYPES_HEADER_TEXT = """UENUM(BlueprintType)
enum class ETDFluidPresetKind : uint8
{
\tWater,
\tMud,
\tFlamethrower,
\tSnowTrack
};

UENUM()
enum class ETDFluidFixtureMode : uint8
{
\tOff,
\tOn
};

USTRUCT(BlueprintType)
struct TDGAME_API FTDFluidSimulationConfig
{
\tGENERATED_BODY()

\tUPROPERTY(EditAnywhere, meta = (ClampMin = "0.0"))
\tfloat HeatCoolingPerSec = 2.0f;

\tUPROPERTY(EditAnywhere)
\tint32 JacobiIterations = 6;

\tUPROPERTY(EditAnywhere)
\tETDFluidFixtureMode Mode = ETDFluidFixtureMode::Off;

\tUPROPERTY(EditAnywhere)
\tuint8 bPacked : 1;
};

USTRUCT()
struct FTDFluidChildSettings : public FTDFluidSimulationConfig
{
\tGENERATED_BODY()

\tUPROPERTY(EditAnywhere)
\tbool bUseWind = false;
};
"""
CLOUD_HEADER_TEXT = """USTRUCT(BlueprintType)
struct FTDFluidCloudSceneSettings
{
\tGENERATED_BODY()

\tUPROPERTY(EditAnywhere)
\tfloat WindCurtainSpeedCmPerSec = 75.0f;

\tUPROPERTY(EditAnywhere)
\tFVector2D Offset = FVector2D::ZeroVector;
};
"""


def plan_path(stem):
    return f"{PLANS}/{stem}.json"


def history_add(key, stem):
    return {"op": "add", "json_path": key, "reason": "accept", "from": {"plan": plan_path(stem)}}


def journal(acquired, items):
    return {"holds": [{"owner": "fixture", "acquired_at": acquired}], "items": [{"plan": plan_path(stem), "status": status, "reason": "", "runs": {}} for stem, status in items]}


def world_files():
    adoptions = {key: {"entry_id": entry_id, "adopted_value": value, "kind": "assignment"} for key, (entry_id, value, _) in ADOPTIONS.items()}
    history = [history_add(entry_id, stem) for entry_id, _, stem in ADOPTIONS.values()]
    history += [{"op": "remove", "actor_prop": dict(LIGHT), "reason": "manual: 재실행", "from": {"plan": plan_path("fog_f7_lights")}},
                {"op": "add", "json_path": LIGHT_KEY, "reason": "accept", "from": {"plan": plan_path("fog_f7_lights")}},
                {"op": "add", "json_path": HVOL_KEY, "reason": "accept", "from": {"plan": plan_path("fx9_hvol_vs_svol")}},
                {"op": "remove", "json_path": HVOL_KEY, "reason": "P-22 출하 조건 확인 불가", "from": {"plan": plan_path("fx9_hvol_vs_svol")}}]
    assignments = [{"json_path": entry_id, "value": value} for entry_id, value, _ in ADOPTIONS.values() if entry_id != LIGHT_KEY]
    return {
        GOLDEN_TEST: GOLDEN_TEST_TEXT, GENERATOR: GENERATOR_TEXT, TYPES_HEADER: TYPES_HEADER_TEXT, CLOUD_HEADER: CLOUD_HEADER_TEXT,
        TUNING: {"Groups": {"WaterLayer": {"Kinds": ["Water", "SnowTrack"]}}},
        EXPECT: {"adoptions": adoptions, "influences": ["f5"], "overlay": OVERLAY},
        OVERLAY: {"assignments": assignments, "actor_props": [dict(LIGHT, value=8.0)], "history": history},
        "Saved/FluidFX/calib/batch-K1/journal.json": journal("2026-10-31T10:00:00", [("fx5_flamethrower", "adopted"), ("cloud_g9_mushroom", "adopted"),
                                                                                    ("cloud_g10_near", "adopted"), ("fog_f1_layer", "pending")]),
        "Saved/FluidFX/calib/batch-K2/journal.json": journal("2026-10-31T11:00:00", [("fog_f1_layer", "adopted"), ("w5_underwater_l18a", "adopted"),
                                                                                    ("cloud_g13_storm_wall", "adopted"), ("fog_f7_lights", "adopted"),
                                                                                    ("track_fx12c_groove", "adopted"), ("fx9_hvol_vs_svol", "adopted"),
                                                                                    ("foliage_k3_reed", "failed")]),
        "Saved/FluidFX/calib/batch-K3/journal.json": journal("2026-10-31T12:00:00", [("cloud_g10_near", "no_adopt"), ("cloud_g12_volcano", "pending")]),
    }


def make_world(root, files):
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    for relative, value in files.items():
        if isinstance(value, str):
            write_text(root, relative, value)
        else:
            write_json(root, relative, value)
    return root


def adoption(document, key):
    return next((record for record in (document or {}).get("adoptions", []) if record["id"] == key), {})


def check_normal(log, root):
    first = run_audit(root, "golden", "golden-1")
    second = run_audit(root, "golden", "golden-2")
    summary_line = first.markdown.strip().splitlines()[-1] if first.markdown else ""
    log.check("정상 종료 0·SUMMARY", first.code == 0 and summary_line == EXPECTED_SUMMARY, f"종료 {first.code} {summary_line} {first.stderr[-300:]} {(first.document or {}).get('problems')}")
    log.check("같은 바이트(json·md)", first.json_bytes is not None and first.json_bytes == second.json_bytes and first.md_bytes == second.md_bytes, "바이트 다름")
    document = first.document or {}
    types = {key: adoption(document, key).get("type_class") for key in EXPECTED_TYPES}
    log.check("골든 형식 6종 중 5종(실수·정수·불·열거·벡터) 분류", types == EXPECTED_TYPES, str(types))
    log.check("골든 줄 문안(4.6 그대로)", document.get("golden_line") == EXPECTED_LINE and f"골든 줄: {EXPECTED_LINE}" in first.markdown, str(document.get("golden_line")))
    log.check("열거·불·정수 목록 = 2·3·4", document.get("discrete_adoptions") == ["2", "3", "4"], str(document.get("discrete_adoptions")))
    log.check("골든 경로 = 스냅숏 구조체 ∧ 시나리오 종류(Kinds.Water·Groups.WaterLayer), Flamethrower·Globals·다른 구조체는 아님",
              document.get("golden_path_adoptions") == ["2", "4"], str(document.get("golden_path_adoptions")))
    child = adoption(document, "8")
    log.check("상위 구조체 필드(FTDFluidChildSettings → FTDFluidSimulationConfig)", child.get("type_source", "").startswith(f"{TYPES_HEADER}:") and child.get("cpp_type") == "float",
              str(child.get("type_source")))
    first_adoption = adoption(document, "1")
    log.check("값 리터럴 ≠ 형식(정수 리터럴 4 → 필드 float = 실수)", first_adoption.get("value_literal") == "정수 리터럴" and first_adoption.get("type_class") == "실수",
              str(first_adoption))
    stems = [result["stem"] for result in document.get("journal_check", [])]
    log.check("일지 대조는 PD-6 최종 adopted만(cloud_g10_near 최종 no_adopt·foliage failed 빠짐)", "cloud_g10_near" not in stems and "foliage_k3_reed" not in stems
              and len(stems) == 8, str(stems))
    hvol = next((result for result in document.get("journal_check", []) if result["stem"] == "fx9_hvol_vs_svol"), {})
    log.check("일지 채택 뒤 overlay 마지막 제거 = 대조됨", hvol.get("keys") == [{"key": HVOL_KEY, "matched": "overlay 마지막 제거"}], str(hvol))
    light = adoption(document, "7")
    log.check("actor_prop 사전 키(history remove)·문자열 키 같은 것으로 셈, 엔진 고정표 실수", light.get("in_overlay") is True and light.get("type_class") == "실수"
              and light.get("journal_sources") == ["K2:fog_f7_lights"], str(light))


def check_problem(log, root, case, needle, mutate):
    files = copy.deepcopy(world_files())
    mutate(files)
    slug = f"case-{len(log.lines)}"
    result = run_audit(make_world(root / slug, files), "golden", slug)
    problems = (result.document or {}).get("problems", [])
    log.check(f"종료 1 {case}", result.code == 1 and any(needle in problem for problem in problems), f"종료 {result.code} {problems} {result.stderr[-200:]}")


def set_entry(files, key, entry_id):
    files[EXPECT]["adoptions"][key]["entry_id"] = entry_id
    for assignment in files[OVERLAY]["assignments"]:
        if assignment["json_path"] == ADOPTIONS[key][0]:
            assignment["json_path"] = entry_id
    for entry in files[OVERLAY]["history"]:
        if entry.get("json_path") == ADOPTIONS[key][0]:
            entry["json_path"] = entry_id


def rename_light_property(files, property_name):
    new_key = LIGHT_KEY.replace(LIGHT["property"], property_name)
    files[EXPECT]["adoptions"]["7"]["entry_id"] = new_key
    files[OVERLAY]["actor_props"][0]["property"] = property_name
    for entry in files[OVERLAY]["history"]:
        if entry.get("json_path") == LIGHT_KEY:
            entry["json_path"] = new_key
        if isinstance(entry.get("actor_prop"), dict):
            entry["actor_prop"]["property"] = property_name


def check_variants(log, root):
    check_problem(log, root, "골든 형식 6종 중 미확인(비트 필드 uint8 : 1)", "형식 미확인: 채택 2", lambda files: set_entry(files, "2", "Kinds.Water.TDFluidSimulationConfig.bPacked"))
    check_problem(log, root, "모르는 엔진 속성", "형식 미확인: 채택 7", lambda files: rename_light_property(files, "intensity"))
    check_problem(log, root, "모르는 종류", "형식 미확인: 채택 1", lambda files: set_entry(files, "1", "Kinds.Lava.TDFluidSimulationConfig.HeatCoolingPerSec"))
    check_problem(log, root, "일지 채택이 expect 밖", "일지 채택이 expect·overlay 제거에 없음: fog_f6_tile",
                  lambda files: (files["Saved/FluidFX/calib/batch-K3/journal.json"]["items"].append({"plan": plan_path("fog_f6_tile"), "status": "adopted", "reason": "", "runs": {}}),
                                 files[OVERLAY]["history"].append({"op": "add", "json_path": "Look:Materials./Game/FluidFX/Materials/MI_Tile.Scalars.FogExtinctionPerM",
                                                                   "reason": "accept", "from": {"plan": plan_path("fog_f6_tile")}})))
    check_problem(log, root, "일지 채택에 overlay 쓰기 없음", "cloud_g10_near(Saved/FluidFX/calib/batch-K3/journal.json) overlay 쓰기 없음",
                  lambda files: files["Saved/FluidFX/calib/batch-K3/journal.json"]["items"].__setitem__(0, {"plan": plan_path("cloud_g10_near"), "status": "adopted", "reason": "", "runs": {}}))
    check_problem(log, root, "expect 채택이 overlay 현재 값에 없음", "expect 채택 6가 overlay 현재 값에 없음",
                  lambda files: files[OVERLAY].update(assignments=[item for item in files[OVERLAY]["assignments"] if item["json_path"] != ADOPTIONS["6"][0]]))
    for case, relative in (("골든 시험 없음", GOLDEN_TEST), ("스냅숏 생성기 DEFAULT_STRUCTS 없음", GENERATOR)):
        files = world_files()
        if relative == GOLDEN_TEST:
            del files[relative]
        else:
            files[relative] = "STRUCTS = ()\n"
        slug = f"case-{len(log.lines)}"
        result = run_audit(make_world(root / slug, files), "golden", slug)
        log.check(f"종료 2 {case}", result.code == 2 and result.json_bytes is None, f"종료 {result.code}")


def run(tmp_root):
    log = FixtureLog("golden")
    root = Path(tmp_root)
    check_normal(log, make_world(root / "normal", world_files()))
    check_variants(log, root)
    return log.result()

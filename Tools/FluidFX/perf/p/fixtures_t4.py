"""실행 환경: 시스템 Python 3.12 + PIL(Pillow), 프로젝트 루트(PowerShell). 에디터 불필요, 가짜 루트는 tempfile 임시 폴더에 만들고 끝나면 지운다
실행 명령: python -B Tools/FluidFX/perf/p/fixtures_t4.py
출력: 표준 출력 사례별 '통과|실패 <사례> <근거>' 줄과 합계 한 줄. 종료 0 전부 통과 / 1 실패 있음 / 2 PIL 없음
상태: 새 fixture(2026-11-06 claude 하위 에이전트, runbook-p-v3 T4 fixture 목록: 가짜 장면 2·짝 시각·Saved 밖·누락·같은 입력·r<n+1>·current.json·30줄·approval-only·7절 라벨·㊿ 7·㊻ 1·structural-gaps·reshoot 휘도 + 반박 r1: 성능 counts 키·종합 수·표 sha·foliage null, 공유 compare.json 채택 귀속(T3 모양 sheet_areas) + 반박 r2: 재촬영 invalid_count·problems·key_errors·부분 실행·0e 이름 불일치·잘린 PNG → 1(예외 없음), 앞 폴더 실패 시 -2 사용·둘 다 실패 → 후 없음, 재촬영 폴더 생김·통과 폴더 사진 잘림 → r<n+1>, 0e·W11 사진 잘림 → 2 + AM7 L4: K 판정이 batch(K1)에만 있음·K 폴더에 판정 파일 없음 + batch-R만 있음·K 폴더에 판정 파일 없음 + K1·R 둘 다 → K1(반박 S1)·아무것도 없음·판정 파일 있는 K 폴더 둘 → 2) + 결함 D2(2026-11-07): D2-1~6 자유 문안·[제안] 보존·근거 미표기·전 머리줄 괄호·perf-candidates 표기·낱말 줄바꿈(그림 글은 도구 모듈의 그리기 함수를 기록용 draw로 불러 확인)
"""
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

REPOSITORY_ROOT = Path(__file__).resolve().parents[4]
TOOL = REPOSITORY_ROOT / "Tools" / "FluidFX" / "make_approval_sheet.py"
CANONICAL_TUNING = "9ea0339795e64446ce8962f4816512a2c5284ce8ad69662ebb94481045b95d0b"
CANONICAL_LOOK = "22a5987c9f5e2fd24f99cfc63b4940af952b9f0e754f331e31d2595b1ea4d73c"
BEFORE_W_TUNING = "39b6c6e0fab13cf30700ec157d11969cf5372440d9e905fb0151d764befd3774"
BEFORE_W_LOOK = "aadc10d9156173089ee7fdc723d8fba83ebfebfa491067618095471402eb110b"
OTHER_LOOK = "5942385354e6402c85f780a54e48e95b4d53bb2b798f34aeb671cc1fe733ae2c"
SCHEDULE_TIMES = (1000, 1250, 1500)
AREAS_FILE = "Tools/FluidFX/perf/p/sheet_areas.json"
PERF_FILE = "Saved/AgentOps/20261106/p/p-summary.json"
APPROVAL_DIR = "Saved/FluidFX/approval"
RESHOOT_DIR = "Saved/FluidFX/approval/reshoot"
FIFTY_LABEL = "재현 실패 — W 경로 점검 / 재현 근거 없음(유지)"
PLAN_SCENES = {
    "fog_a": ("fa", ("4",)),
    "fog_b": ("fb", ("14",)),
    "cloud_a": ("ca", ("3", "6", "11", "13")),
    "track_a": ("ta", ("9", "12")),
    "w5_a": ("wa", ("17",)),
    "w1b_b": ("wb", ()),
    "fx9_a": ("sa", ()),
    "fx5_a": ("ia", ("1",)),
    "foliage_a": ("la", ()),
}
RERUN_PLANS = ("track_a",)
WAB_ADOPTIONS = ("4",)
AREA_SCENES = {
    "fog": (("fog_a", "fa"), ("fog_b", "fb")),
    "cloud": (("cloud_a", "ca"),),
    "track": (("track_a", "ta"),),
    "water": (("w5_a", "wa"), ("w1b_b", "wb")),
    "smoke": (("fx9_a", "sa"),),
    "input": (("fx5_a", "ia"),),
    "foliage": (("foliage_a", "la"),),
}
RESHOOT_PLANS = ("w1b_b", "fx9_a", "foliage_a")
PARTIAL_OVERALL = "부분(통과 1 / 행 2 / 판정 불가 0 / 측정 불가 1 / 출하 밖 0)"
BASE_COUNTS = {"통과": 1, "미달": 0, "판정 불가": 0, "측정 불가": 1, "출하 밖": 0}
TABLELESS_AREA = "foliage"
SHARED_COMPARE_PLAN = "cloud_a"
SHARED_COMPARE_SCENE = "cb"
SHARED_COMPARE_ADOPTION = "11"


def write_json(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_photo(path, gray):
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.new("RGB", (64, 36), (gray, gray, min(255, gray + 20))).save(path, format="PNG")


def write_schedule_photos(scene_dir, gray, times=SCHEDULE_TIMES):
    for milliseconds in times:
        write_photo(scene_dir / f"00_schedule_{milliseconds}ms.png", gray)


def level_of(plan):
    return f"/Game/Fake/LV_{plan}"


def metric_of(adoption):
    return f"region.metric_{adoption}"


def sha256_of(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_plans_and_frames(root):
    for plan, (scene, _) in PLAN_SCENES.items():
        basis = "live2_image" if plan == "fog_a" else "spec_proposal"
        write_json(root / f"Tools/FluidFX/calib/plans/{plan}.json", {"metrics": {"targets": [{"scene": None, "metric": f"region.metric_{plan}", "basis": basis}] + [{"scene": scene, "metric": metric_of(number), "basis": basis} for number in PLAN_SCENES[plan][1]]}})
        look = OTHER_LOOK if plan == "fog_b" else BEFORE_W_LOOK
        write_json(root / f"Saved/FluidFX/calib/frames/{plan}/sweep.json", {"plan_id": plan, "stage": "frame", "canonical_tuning_sha256": BEFORE_W_TUNING, "canonical_look_sha256": look, "scenes": [{"name": scene, "level": level_of(plan), "live2_refs": ["IMG 19", "L2"]}]})
        write_schedule_photos(root / f"Saved/FluidFX/calib/frames/{plan}/{scene}", 90)


def build_adoptions(root):
    verdict_adoptions = {}
    expect_adoptions = {}
    rows = []
    for plan, (scene, numbers) in PLAN_SCENES.items():
        if not numbers:
            continue
        run_dir = f"Saved/FluidFX/calib/final/batch-e1/runs/{plan}"
        verify_dirs = ["verify-1", "verify-2"] if plan in RERUN_PLANS else ["verify-1"]
        for verify in verify_dirs:
            times = (1000, 1300, 1500) if plan in RERUN_PLANS and verify == "verify-1" else SCHEDULE_TIMES
            write_schedule_photos(root / run_dir / verify / scene, 110, times)
            write_json(root / run_dir / verify / "sweep.json", {"plan_id": plan, "stage": "verify", "canonical_tuning_sha256": CANONICAL_TUNING, "canonical_look_sha256": CANONICAL_LOOK, "scenes": [{"name": scene, "level": level_of(plan)}]})
            write_json(root / run_dir / verify / "compare.json", {"checks": [{"scene": scene, "metric": metric_of(number), "value": 1.5} for number in numbers]})
        judge_path = f"Saved/FluidFX/calib/batch-R5/runs/{plan}/confirm-1/judge.json"
        write_json(root / judge_path, {"plan": f"Tools/FluidFX/calib/plans/{plan}.json", "scenes": [{"name": scene, "control_reference": {metric_of(number): 0.5 for number in numbers}, "targets": [{"metric": metric_of(number), "low": 1.0, "high": 2.0, "band_source": "plan"} for number in numbers]}]})
        for number in numbers:
            evidence = [f"{run_dir}/{verify}/compare.json" for verify in verify_dirs]
            if number in WAB_ADOPTIONS:
                wab = f"Saved/FluidFX/calib/final/batch-e1/runs/{plan}-wab/screen-1/screen.json"
                write_json(root / wab, {"scenes": [{"name": scene, "variants": {"v01": {"medians": {metric_of(number): 1.2}}}}]})
                evidence.append(wab)
            verdict_adoptions[number] = {
                "evidence": evidence,
                "labels": [FIFTY_LABEL if plan in ("cloud_a", "track_a", "w5_a") else "통과"],
                "state": "pass",
                "state_source": {"item_plan": f"Tools/FluidFX/calib/plans/{plan}.json", "opportunity": "rerun" if plan in RERUN_PLANS else "first"},
            }
            expect_adoptions[number] = {"scenes": [scene], "levels": [level_of(plan)]}
            rows.append({"adoption": number, "scene": scene, "metric": metric_of(number), "expected": 1.5, "source": {"judge": judge_path}})
    verdict_adoptions["f5"] = {"evidence": [], "labels": [], "state_source": {"item_plan": "Tools/FluidFX/calib/plans/fog_a.json", "opportunity": "first"}}
    user_decision = [{"adoption": number, "label": FIFTY_LABEL} for number in ("3", "6", "9", "11", "12", "13", "17")]
    user_decision.append({"adoption": None, "label": "설계 단언 실패 — TDGame.FluidFX.CloudSource.WindCurtainStamps(소유 0개)"})
    write_json(root / "Saved/FluidFX/calib/final/verdict-w11.json", {"adoptions": verdict_adoptions, "user_decision": user_decision})
    write_json(root / "Saved/FluidFX/calib/final/expect.json", {
        "canonical_after": {"tuning": CANONICAL_TUNING, "look": CANONICAL_LOOK},
        "canonical_before": {"tuning": BEFORE_W_TUNING, "look": BEFORE_W_LOOK},
        "adoptions": expect_adoptions,
        "rows": rows,
    })


def write_reshoot(root, folder, plan, gray, tuning=CANONICAL_TUNING, overlay="Saved/FluidFX/calib/final/overlay-empty.json", times=SCHEDULE_TIMES, problems=()):
    scene = PLAN_SCENES[plan][0]
    run_dir = root / RESHOOT_DIR / folder
    if run_dir.exists():
        shutil.rmtree(run_dir)
    write_schedule_photos(run_dir / scene, gray, times)
    overlay_sha = sha256_of(root / "Saved/FluidFX/calib/final/overlay-empty.json")
    write_json(run_dir / "sweep.json", {
        "plan_id": plan,
        "stage": "frame",
        "canonical_tuning_sha256": tuning,
        "canonical_look_sha256": CANONICAL_LOOK,
        "canonical_sha256_after": {"tuning": tuning, "look": CANONICAL_LOOK},
        "overlay": overlay,
        "overlay_sha256": overlay_sha,
        "key_errors": [],
        "problems": list(problems),
        "invalid_count": len(problems),
        "scenes": [{"name": scene, "level": level_of(plan), "shots_expected": len(SCHEDULE_TIMES)}],
    })
    return run_dir


def build_side_inputs(root):
    write_json(root / "Saved/FluidFX/calib/final/overlay-empty.json", {"version": 1, "entries": []})
    for plan in ("w1b_b", "fx9_a"):
        scene = PLAN_SCENES[plan][0]
        write_json(root / f"Saved/FluidFX/calib/batch-K9/runs/{plan}/screen-1/screen.json", {"plan": f"Tools/FluidFX/calib/plans/{plan}.json", "scenes": [{"name": scene, "control_reference": {f"region.metric_{plan}": 3.0}, "targets": [{"metric": f"region.metric_{plan}", "low": 2.0, "high": 4.0, "band_source": "region"}]}]})
    write_reshoot(root, "w1b_b", "w1b_b", 5)
    write_reshoot(root, "w1b_b-2", "w1b_b", 120)
    write_json(root / "Tools/FluidFX/calib/approval-only.json", {"items": [
        {"plan": "fog_a", "item": "fog 육안 항목", "destination": "승인 시트"},
        {"plan": "fx9_a", "item": "연기 꼬리", "destination": "승인 시트 뒤(deferred)"},
    ]})
    write_json(root / "Tools/FluidFX/calib/structural-gaps.json", {"gaps": [
        {"plan": "fog_a", "item": "fog 구조 항목", "destination": "승인 시트(육안)"},
        {"plan": "zzz_unknown", "item": "무관", "destination": "없음(구조)"},
    ]})
    write_json(root / "Saved/FluidFX/calib/perf-candidates.json", {"items": [{"plan": "Tools/FluidFX/calib/plans/w1b_b.json"}]})
    for name in ("fx11c_cloud_paint.png", "fx14_viscous_flow_decal.png", "w5_underwater_pit_over.png", "w5_underwater_pit_under.png"):
        write_photo(root / "Saved/AgentOps/20261030/wave3-assets/a8" / name, 60)
        write_photo(root / "Saved/AgentOps/20261106/w/w10-pass1/pie-shots" / name, 70)
    write_photo(root / "Saved/FluidFX/showcase/D-49-fx3-fire-2026-10-18.gif", 40)
    for folder in ("w1-river-20261023-021209", "fx4b-river-triptych-20261025-025943"):
        write_photo(root / "Saved/FluidFX/showcase" / folder / "p1.png", 80)
    write_json(root / AREAS_FILE, {
        "version": 1,
        "areas": {area: {"scenes": [{"plan": plan, "scene": scene} for plan, scene in scenes]} for area, scenes in AREA_SCENES.items()},
        "reshoot": list(RESHOOT_PLANS),
    })
    write_perf(root, {})


def write_perf(root, overrides):
    areas = {area: {"overall": PARTIAL_OVERALL, "counts": dict(BASE_COUNTS), "table_sha256": "c" * 64} for area in AREA_SCENES}
    areas["fog"]["overall"] = "충족(proposed)"
    areas[TABLELESS_AREA]["table_sha256"] = None
    areas.update(overrides)
    write_json(root / PERF_FILE, {"areas": areas})


def build_world(root):
    build_plans_and_frames(root)
    build_adoptions(root)
    build_side_inputs(root)
    (root / "Docs").mkdir(parents=True, exist_ok=True)
    return root


def run_tool(root, *arguments):
    environment = dict(os.environ, PYTHONIOENCODING="utf-8")
    completed = subprocess.run([sys.executable, "-B", str(TOOL), "--root", str(root), *arguments], capture_output=True, text=True, encoding="utf-8", env=environment, timeout=300)
    return completed.returncode, completed.stdout + completed.stderr


def run_sheet(root, area, *extra):
    return run_tool(root, "--area", area, "--areas", AREAS_FILE, "--perf", PERF_FILE, *extra)


def area_dir(root, area):
    return root / APPROVAL_DIR / area


def revisions(root, area):
    folder = area_dir(root, area)
    return sorted(item.name for item in folder.iterdir() if item.is_dir() and item.name.startswith("r")) if folder.is_dir() else []


def folder_hashes(folder):
    return {item.name: sha256_of(item) for item in sorted(folder.iterdir()) if item.is_file()}


def index_text(root, area, revision="r1"):
    return (area_dir(root, area) / revision / "index.md").read_text(encoding="utf-8")


def png_chunk_types(path):
    data = path.read_bytes()
    position = 8
    types = []
    while position < len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        types.append(data[position + 4:position + 8].decode("ascii"))
        position += 12 + length
    return types


class FixtureRun:
    def __init__(self, base):
        self.base = base
        self.results = []
        self.counter = 0

    def fresh_root(self):
        self.counter += 1
        root = self.base / f"w{self.counter}"
        root.mkdir()
        return build_world(root)

    def expect(self, name, condition, detail):
        self.results.append((name, bool(condition), detail))
        print(f"{'통과' if condition else '실패'} {name} {detail}")


def case_fake_scenes_two(run):
    root = run.fresh_root()
    code, output = run_sheet(root, "fog")
    run.expect("가짜 장면 2(종료 0·r1)", code == 0 and revisions(root, "fog") == ["r1"], f"종료 {code} {revisions(root, 'fog')} {output.strip()[:200]}")
    if code != 0:
        return root
    folder = area_dir(root, "fog") / "r1"
    sheet = read_json(folder / "sheet.json")
    run.expect("가짜 장면 2(sheet.json 장면)", [scene["scene"] for scene in sheet["scenes"]] == ["fa", "fb"], str([scene["scene"] for scene in sheet["scenes"]]))
    run.expect("가짜 장면 2(파일 목록)", sorted(item.name for item in folder.iterdir()) == ["index.md", "sheet.json", "sheet.png"], str(sorted(item.name for item in folder.iterdir())))
    current = read_json(area_dir(root, "fog") / "current.json")
    run.expect("current.json(r1·sha)", current["revision"] == 1 and current["sheet_json_sha256"] == sha256_of(folder / "sheet.json") and current["revision_dir"] == f"{APPROVAL_DIR}/fog/r1", json.dumps(current, ensure_ascii=False)[:160])
    run.expect("PNG 메타 없음", set(png_chunk_types(folder / "sheet.png")) == {"IHDR", "IDAT", "IEND"}, str(sorted(set(png_chunk_types(folder / "sheet.png")))))
    text = index_text(root, "fog")
    run.expect("index.md 30줄 이하", len(text.splitlines()) <= 30, f"{len(text.splitlines())}줄")
    run.expect("전 정본 ≠ W 직전 표기(fb만)", "fb(fog_b" in text and "전 정본 ≠ W 직전" in text.split("fb(fog_b")[1].splitlines()[0] and "전 정본 ≠ W 직전" not in text.split("fa(fog_a")[1].splitlines()[0], "fb 줄만")
    run.expect("7절 [제안] 기준 충족 대체·동등 확인 없음", "[제안] 기준 충족(동등 증명 아님)" in text and "동등 확인" not in text and "충족(proposed)" not in text, "성능 줄")
    before_sources = {metric["adoption"]: metric["before"]["source"] for scene in sheet["scenes"] for metric in scene["metrics"]}
    run.expect("전 출처 A/B v01 > K 대조", before_sources.get("4", "").startswith("손잡이 1개 되돌림") and before_sources.get("14", "").startswith("상류 덧씌움 섞임"), json.dumps(before_sources, ensure_ascii=False))
    basis = [metric["band"]["basis_label"] for scene in sheet["scenes"] for metric in scene["metrics"]]
    run.expect("띠 basis 라벨", basis == ["LIVE-2 이미지", "[제안] 동등 증명 아님(P-11)"], str(basis))
    run.expect("IMG 번호만", sheet["scenes"][0]["image_references"] == ["IMG19"], str(sheet["scenes"][0]["image_references"]))
    return root


def case_same_input_twice(run, root):
    before = folder_hashes(area_dir(root, "fog") / "r1")
    current_before = sha256_of(area_dir(root, "fog") / "current.json")
    code, output = run_sheet(root, "fog")
    run.expect("같은 입력 두 번 → 새 판 없음", code == 0 and revisions(root, "fog") == ["r1"] and "새 판 없음" in output, f"종료 {code} {revisions(root, 'fog')}")
    run.expect("같은 입력 두 번 → 바이트·current 불변", folder_hashes(area_dir(root, "fog") / "r1") == before and sha256_of(area_dir(root, "fog") / "current.json") == current_before, "sha 대조")
    code, output = run_sheet(root, "fog", "--check")
    run.expect("--check 일치 0", code == 0, f"종료 {code} {output.strip()[:160]}")


def case_deterministic_bytes(run, first_root):
    second_root = run.fresh_root()
    code, output = run_sheet(second_root, "fog")
    first = folder_hashes(area_dir(first_root, "fog") / "r1")
    second = folder_hashes(area_dir(second_root, "fog") / "r1") if code == 0 else {}
    run.expect("결정적 바이트(다른 루트 같은 입력)", code == 0 and first == second, f"종료 {code} png {first.get('sheet.png', '')[:12]} vs {second.get('sheet.png', '')[:12]}")


def case_changed_input_new_revision(run, root):
    before = folder_hashes(area_dir(root, "fog") / "r1")
    changed_counts = dict(BASE_COUNTS, **{"통과": 2})
    write_perf(root, {"fog": {"overall": "부분(통과 2 / 행 3 / 판정 불가 0 / 측정 불가 1 / 출하 밖 0)", "counts": changed_counts, "table_sha256": "d" * 64}})
    code, output = run_sheet(root, "fog")
    current = read_json(area_dir(root, "fog") / "current.json")
    run.expect("입력 바뀜 → r2", code == 0 and revisions(root, "fog") == ["r1", "r2"], f"종료 {code} {revisions(root, 'fog')}")
    if code == 0:
        perf_lines = [line for line in index_text(root, "fog", "r2").splitlines() if line.startswith("- 성능")]
        run.expect("성능 줄 = counts 다섯 키 그대로·표 sha 8자", len(perf_lines) == 1 and "통과 2·미달 0·판정 불가 0·측정 불가 1·출하 밖 0" in perf_lines[0] and "표 dddddddd" in perf_lines[0], perf_lines[0][:200] if perf_lines else "성능 줄 없음")
    run.expect("current.json → r2·r1 불변", current["revision"] == 2 and current["sheet_json_sha256"] == sha256_of(area_dir(root, "fog") / "r2" / "sheet.json") and folder_hashes(area_dir(root, "fog") / "r1") == before, json.dumps(current, ensure_ascii=False)[:120])
    code, _ = run_sheet(root, "fog", "--check")
    run.expect("--check r2 일치 0", code == 0, f"종료 {code}")
    index_path = area_dir(root, "fog") / "r2" / "index.md"
    original = index_path.read_bytes()
    index_path.write_bytes(original + b"x\n")
    code, output = run_sheet(root, "fog", "--check")
    run.expect("--check 바이트 변조 → 2", code == 2 and "불일치" in output, f"종료 {code} {output.strip()[:120]}")
    index_path.write_bytes(original)
    current_path = area_dir(root, "fog") / "current.json"
    current_original = current_path.read_bytes()
    write_json(current_path, dict(current, revision=1))
    code, output = run_sheet(root, "fog", "--check")
    run.expect("--check current.json 어긋남 → 2", code == 2 and "current.json" in output, f"종료 {code}")
    current_path.write_bytes(current_original)


def case_pair_time_mismatch(run):
    root = run.fresh_root()
    after = root / "Saved/FluidFX/calib/final/batch-e1/runs/fog_a/verify-1/fa"
    (after / "00_schedule_1250ms.png").rename(after / "00_schedule_1300ms.png")
    code, output = run_sheet(root, "fog")
    run.expect("짝 시각 불일치 → 1·판 없음", code == 1 and "짝 시각" in output and revisions(root, "fog") == [], f"종료 {code} {output.strip()[:160]}")


def case_outside_saved(run):
    root = run.fresh_root()
    code, output = run_sheet(root, "fog", "--out-root", "Docs/approval")
    run.expect("Saved 밖 출력 → 1·쓰기 없음", code == 1 and "Saved 밖" in output and not (root / "Docs/approval").exists(), f"종료 {code} {output.strip()[:120]}")
    code, output = run_sheet(root, "fog", "--out-root", "Saved/../Docs/approval")
    run.expect("Saved 밖(.. 경로) → 1", code == 1 and not (root / "Docs/approval").exists(), f"종료 {code}")


def case_missing_inputs(run):
    root = run.fresh_root()
    for photo in (root / "Saved/FluidFX/calib/frames/fog_b/fb").glob("*.png"):
        photo.unlink()
    code, output = run_sheet(root, "fog")
    run.expect("누락: 0e 전 사진 → 2", code == 2 and "0e 전 사진 없음" in output, f"종료 {code} {output.strip()[:120]}")
    root = run.fresh_root()
    (root / "Saved/FluidFX/calib/final/expect.json").unlink()
    code, output = run_sheet(root, "fog")
    run.expect("누락: expect.json → 2", code == 2 and "입력 없음" in output, f"종료 {code} {output.strip()[:120]}")
    root = run.fresh_root()
    shutil.rmtree(root / "Saved/FluidFX/calib/final/batch-e1/runs/fog_a/verify-1/fa")
    code, output = run_sheet(root, "fog")
    run.expect("누락: W11 후 사진 → 2", code == 2 and "W11 후 사진 없음" in output, f"종료 {code} {output.strip()[:120]}")
    root = run.fresh_root()
    perf = read_json(root / PERF_FILE)
    del perf["areas"]["fog"]
    write_json(root / PERF_FILE, perf)
    code, output = run_sheet(root, "fog")
    run.expect("누락: --perf 영역 → 2", code == 2 and "overall" in output, f"종료 {code} {output.strip()[:120]}")
    root = run.fresh_root()
    shutil.rmtree(root / "Saved/FluidFX/showcase/w1-river-20261023-021209")
    code, output = run_sheet(root, "water")
    run.expect("누락: 합칠 대기 경로 → 2", code == 2 and "입력 없음" in output, f"종료 {code} {output.strip()[:120]}")


def case_index_over_limit(run):
    root = run.fresh_root()
    document = read_json(root / "Tools/FluidFX/calib/approval-only.json")
    document["items"].extend({"plan": "fog_a", "item": f"추가 항목 {index}", "destination": "승인 시트"} for index in range(30))
    write_json(root / "Tools/FluidFX/calib/approval-only.json", document)
    code, output = run_sheet(root, "fog")
    run.expect("index.md 30줄 초과 → 2·판 없음", code == 2 and "30줄" in output and revisions(root, "fog") == [], f"종료 {code} {output.strip()[:120]}")


def case_approval_only(run, root):
    run_sheet(root, "smoke")
    fog_text, smoke_text = index_text(root, "fog"), index_text(root, "smoke")
    run.expect("approval-only 영역 항목", "approval-only #0 fog_a" in fog_text and "approval-only #1" not in fog_text, "fog #0만")
    run.expect("approval-only (deferred) → 뒤로 미룸", "뒤로 미룸: approval-only #1 fx9_a" in smoke_text, "smoke #1")
    broken = run.fresh_root()
    document = read_json(broken / "Tools/FluidFX/calib/approval-only.json")
    document["items"].append({"plan": "zzz_unknown", "item": "영역 없음", "destination": "승인 시트"})
    write_json(broken / "Tools/FluidFX/calib/approval-only.json", document)
    code, output = run_sheet(broken, "fog")
    run.expect("approval-only 영역 미정 → 2", code == 2 and "영역을 정할 수 없다" in output, f"종료 {code}")


def case_section_seven_labels(run, root):
    codes = {area: run_sheet(root, area)[0] for area in ("cloud", "track", "water", "input")}
    run.expect("7절 영역 시트 생성", all(code == 0 for code in codes.values()), str(codes))
    if not all(code == 0 for code in codes.values()):
        return
    texts = {area: index_text(root, area) for area in ("fog", "smoke", "cloud", "track", "water", "input")}
    water = texts["water"]
    run.expect("7절 water 라벨", all(label in water for label in ("⑫ 미승인 위에서 측정", "㉒ 미승인 위에서 측정", "판정 불가(구조) ㉓", "D-60 미결(GPU 물 기본 꺼짐·판정 보류)")), "⑫·㉒·㉓·D-60")
    run.expect("7절 cloud·smoke·track 라벨", "⑫ 미승인 위에서 측정" in texts["cloud"] and "판정 불가(구조) ㉓(g4 painter)" in texts["cloud"] and "㉝ fx9 hvol W 전 제거" in texts["smoke"] and "⑫ 미승인 위에서 측정" in texts["track"] and "⑫" not in texts["smoke"], "cloud·smoke·track")
    run.expect("7절 공통 PF·승인 대기·동등 확인 없음", all("PF 환산 ×2.3~3·상한 비교(증명 아님)" in text and "승인 대기" in text and "동등 확인" not in text for text in texts.values()), "6영역")
    run.expect("대표 1개(0e 짝 없음)", "대표 1개(0e 짝 없음)" in texts["input"] and "대표 1개" not in texts["fog"], "input")
    run.expect("합칠 대기·파동 3 짝", "Saved/FluidFX/showcase/w1-river-20261023-021209" in water and "탱크 코스틱 = 그림 없음(촬영 계획 없음)" in water and "w5_underwater_pit_under.png" in water and "fx11c_cloud_paint.png" in texts["cloud"], "water·cloud")
    run.expect("성능 후보(perf-candidates)", "성능 후보 1(perf-candidates: " in water and "성능 후보" not in texts["fog"], "water")
    numbers = []
    for text in texts.values():
        for line in text.splitlines():
            if "㊿" in line:
                numbers.extend(token[1:] for token in line.split() if token.startswith("#") and token[1:].isdigit())
    run.expect("㊿ 7(cloud 4·track 2·water 1)", sorted(numbers, key=int) == ["3", "6", "9", "11", "12", "13", "17"], str(numbers))
    forty_six = [line for text in texts.values() for line in text.splitlines() if "㊻" in line]
    run.expect("㊻ 1(cloud)", len(forty_six) == 1 and "WindCurtainStamps" in forty_six[0] and "㊻" in texts["cloud"], str(len(forty_six)))
    sheet = read_json(area_dir(root, "track") / "r1" / "sheet.json")
    run.expect("rerun 근거 → 마지막 verify", sheet["scenes"][0]["after"]["source"].endswith("track_a/verify-2"), sheet["scenes"][0]["after"]["source"] or "")
    sheet = read_json(area_dir(root, "water") / "r1" / "sheet.json")
    after = {scene["scene"]: scene["after"] for scene in sheet["scenes"]}
    run.expect("재촬영: 통과한 앞 폴더(-2)", after["wb"]["kind"] == "reshoot" and after["wb"]["source"].endswith("reshoot/w1b_b-2"), json.dumps(after["wb"], ensure_ascii=False)[:160])
    sheet = read_json(area_dir(root, "smoke") / "r1" / "sheet.json")
    run.expect("재촬영 없음 → 후 없음·정본 불변(채택 0)", sheet["scenes"][0]["after"]["kind"] == "none" and sheet["scenes"][0]["metrics"][0]["after"]["label"] == "정본 불변(채택 0)", json.dumps(sheet["scenes"][0]["after"], ensure_ascii=False)[:120])


def case_user_item_counts(run):
    root = run.fresh_root()
    verdict = read_json(root / "Saved/FluidFX/calib/final/verdict-w11.json")
    verdict["user_decision"] = [item for item in verdict["user_decision"] if item["adoption"] != "17"]
    write_json(root / "Saved/FluidFX/calib/final/verdict-w11.json", verdict)
    code, output = run_sheet(root, "fog")
    run.expect("㊿ 6개 → 2", code == 2 and "㊿" in output, f"종료 {code} {output.strip()[:120]}")
    root = run.fresh_root()
    verdict = read_json(root / "Saved/FluidFX/calib/final/verdict-w11.json")
    verdict["user_decision"].append(dict(verdict["user_decision"][-1]))
    write_json(root / "Saved/FluidFX/calib/final/verdict-w11.json", verdict)
    code, output = run_sheet(root, "cloud")
    run.expect("㊻ 2개 → 2", code == 2 and "㊻" in output, f"종료 {code} {output.strip()[:120]}")


def case_structural_gaps(run, root):
    text = index_text(root, "fog")
    run.expect("structural-gaps '승인 시트' 항목", "structural-gaps #0 fog_a" in text and "structural-gaps #1" not in text, "fog #0만")
    missing = run.fresh_root()
    (missing / "Tools/FluidFX/calib/structural-gaps.json").unlink()
    code, output = run_sheet(missing, "fog")
    run.expect("structural-gaps 누락 → 2", code == 2 and "structural-gaps.json" in output, f"종료 {code} {output.strip()[:120]}")
    broken = run.fresh_root()
    document = read_json(broken / "Tools/FluidFX/calib/structural-gaps.json")
    document["gaps"].append({"plan": "zzz_unknown", "item": "영역 없음", "destination": "승인 시트(육안)"})
    write_json(broken / "Tools/FluidFX/calib/structural-gaps.json", document)
    code, output = run_sheet(broken, "fog")
    run.expect("structural-gaps '승인 시트' 영역 미정 → 2", code == 2 and "영역을 정할 수 없다" in output, f"종료 {code}")


def case_reshoot_check(run, root):
    code, output = run_tool(root, "--reshoot-check", f"{RESHOOT_DIR}/w1b_b")
    run.expect("reshoot 휘도 <18 → 1", code == 1 and "휘도" in output, f"종료 {code} {output.strip()[:120]}")
    code, output = run_tool(root, "--reshoot-check", f"{RESHOOT_DIR}/w1b_b-2")
    run.expect("reshoot 통과 → 0", code == 0, f"종료 {code} {output.strip()[:120]}")
    code, output = run_tool(root, "--reshoot-check", f"{RESHOOT_DIR}/fx9_a")
    run.expect("reshoot 없음 → 1", code == 1 and "없음" in output, f"종료 {code}")
    write_reshoot(root, "fx9_a", "fx9_a", 120, tuning="0" * 64)
    code, output = run_tool(root, "--reshoot-check", f"{RESHOOT_DIR}/fx9_a")
    run.expect("reshoot 정본 sha 다름 → 1", code == 1 and "정본" in output, f"종료 {code}")
    write_reshoot(root, "fx9_a-2", "fx9_a", 120, overlay="Saved/FluidFX/calib/final/overlay-frozen.json")
    code, output = run_tool(root, "--reshoot-check", f"{RESHOOT_DIR}/fx9_a-2")
    run.expect("reshoot overlay 다름 → 1", code == 1 and "overlay" in output, f"종료 {code}")


def case_area_definition(run):
    root = run.fresh_root()
    areas = read_json(root / AREAS_FILE)
    areas["areas"]["fog"]["scenes"] = [{"plan": "fog_a", "scene": "fa"}]
    write_json(root / AREAS_FILE, areas)
    code, output = run_sheet(root, "fog")
    run.expect("6.2 ① 채택 장면 누락 → 2", code == 2 and "누락" in output, f"종료 {code} {output.strip()[:120]}")
    root = run.fresh_root()
    areas = read_json(root / AREAS_FILE)
    areas["areas"]["fog"]["scenes"][0]["adoptions"] = ["4", "99"]
    write_json(root / AREAS_FILE, areas)
    code, output = run_sheet(root, "fog")
    run.expect("sheet_areas adoptions 불일치 → 2", code == 2 and "adoptions" in output, f"종료 {code}")
    root = run.fresh_root()
    areas = read_json(root / AREAS_FILE)
    areas["reshoot"] = ["fx9_a"]
    write_json(root / AREAS_FILE, areas)
    code, output = run_sheet(root, "water")
    run.expect("채택 없는 장면이 reshoot 밖 → 2", code == 2 and "reshoot" in output, f"종료 {code}")
    code, output = run_sheet(root, "nowhere")
    run.expect("모르는 영역 → 2", code == 2, f"종료 {code}")


def expect_perf_rejected(run, name, entry, expected_text):
    root = run.fresh_root()
    write_perf(root, {"fog": entry})
    code, output = run_sheet(root, "fog")
    run.expect(name, code == 2 and expected_text in output and revisions(root, "fog") == [], f"종료 {code} {output.strip()[:160]}")


def case_perf_counts(run):
    table = "c" * 64
    expect_perf_rejected(run, "성능 counts 없음 → 2", {"overall": PARTIAL_OVERALL, "table_sha256": table}, "counts")
    expect_perf_rejected(run, "성능 counts 키 다름(pass·fail·row) → 2", {"overall": PARTIAL_OVERALL, "counts": {"pass": 3, "fail": 2, "row": 5}, "table_sha256": table}, "counts 키")
    missing_key = {key: value for key, value in BASE_COUNTS.items() if key != "출하 밖"}
    expect_perf_rejected(run, "성능 counts 키 하나 빠짐 → 2", {"overall": PARTIAL_OVERALL, "counts": missing_key, "table_sha256": table}, "counts 키")
    expect_perf_rejected(run, "성능 counts 값 불리언 → 2", {"overall": PARTIAL_OVERALL, "counts": dict(BASE_COUNTS, **{"미달": True}), "table_sha256": table}, "0 이상 정수")
    expect_perf_rejected(run, "성능 counts 값 음수 → 2", {"overall": PARTIAL_OVERALL, "counts": dict(BASE_COUNTS, **{"출하 밖": -1}), "table_sha256": table}, "0 이상 정수")
    expect_perf_rejected(run, "성능 종합 수 ≠ counts → 2", {"overall": PARTIAL_OVERALL, "counts": dict(BASE_COUNTS, **{"통과": 2}), "table_sha256": table}, "overall(종합)")
    expect_perf_rejected(run, "성능 표 sha null(행 있는 영역) → 2", {"overall": PARTIAL_OVERALL, "counts": dict(BASE_COUNTS), "table_sha256": None}, "table_sha256")
    expect_perf_rejected(run, "성능 표 sha 64자 아님 → 2", {"overall": PARTIAL_OVERALL, "counts": dict(BASE_COUNTS), "table_sha256": "abc"}, "table_sha256")
    root = run.fresh_root()
    code, output = run_sheet(root, TABLELESS_AREA)
    text = index_text(root, TABLELESS_AREA) if code == 0 else ""
    perf_lines = [line for line in text.splitlines() if line.startswith("- 성능")]
    run.expect("foliage 표 sha null → 0·표 —", code == 0 and len(perf_lines) == 1 and "표 —" in perf_lines[0] and "통과 1·미달 0·판정 불가 0·측정 불가 1·출하 밖 0" in perf_lines[0], f"종료 {code} {(perf_lines or [output.strip()])[0][:200]}")
    run.expect("foliage ㉘·갈대 그림 없음", "deferred ㉘(k2 반감기)" in text and "갈대 = 그림 없음(촬영 계획 없음)" in text, "foliage index.md")


def add_shared_compare_scene(root):
    plan, scene, number = SHARED_COMPARE_PLAN, SHARED_COMPARE_SCENE, SHARED_COMPARE_ADOPTION
    frames_dir = root / "Saved/FluidFX/calib/frames" / plan
    frames_sweep = read_json(frames_dir / "sweep.json")
    frames_sweep["scenes"].append({"name": scene, "level": level_of(plan), "live2_refs": []})
    write_json(frames_dir / "sweep.json", frames_sweep)
    write_schedule_photos(frames_dir / scene, 90)
    verify_dir = root / "Saved/FluidFX/calib/final/batch-e1/runs" / plan / "verify-1"
    write_schedule_photos(verify_dir / scene, 110)
    verify_sweep = read_json(verify_dir / "sweep.json")
    verify_sweep["scenes"].append({"name": scene, "level": level_of(plan)})
    write_json(verify_dir / "sweep.json", verify_sweep)
    compare = read_json(verify_dir / "compare.json")
    compare["checks"] = [dict(check, scene=name) for name in (PLAN_SCENES[plan][0], scene) for check in compare["checks"]]
    write_json(verify_dir / "compare.json", compare)
    judge_path = root / f"Saved/FluidFX/calib/batch-R5/runs/{plan}/confirm-1/judge.json"
    judge = read_json(judge_path)
    judge["scenes"].append(dict(judge["scenes"][0], name=scene))
    write_json(judge_path, judge)
    expect_path = root / "Saved/FluidFX/calib/final/expect.json"
    expect = read_json(expect_path)
    expect["adoptions"][number]["scenes"] = [scene]
    for row in expect["rows"]:
        if row["adoption"] == number:
            row["scene"] = scene
    write_json(expect_path, expect)


def t3_shape_scene(plan, scene, adoptions, after_dir):
    return {
        "plan": plan,
        "scene": scene,
        "source": "adoption" if adoptions else "fill",
        "adoptions": list(adoptions),
        "before": f"Saved/FluidFX/calib/frames/{plan}/{scene}",
        "after": f"{after_dir}/{scene}" if after_dir else None,
    }


def write_t3_shape_areas(root, cloud_adoptions):
    cloud_after = f"Saved/FluidFX/calib/final/batch-e1/runs/{SHARED_COMPARE_PLAN}/verify-1"
    areas = read_json(root / AREAS_FILE)
    areas["areas"]["cloud"] = {"has_adoption": True, "cap": 2, "scenes": [t3_shape_scene(SHARED_COMPARE_PLAN, scene, adoptions, cloud_after) for scene, adoptions in cloud_adoptions]}
    areas["areas"]["water"] = {"has_adoption": True, "cap": 2, "scenes": [
        t3_shape_scene("w5_a", "wa", ("17",), "Saved/FluidFX/calib/final/batch-e1/runs/w5_a/verify-1"),
        t3_shape_scene("w1b_b", "wb", (), None),
    ]}
    write_json(root / AREAS_FILE, areas)


def case_shared_compare_adoptions(run):
    root = run.fresh_root()
    add_shared_compare_scene(root)
    write_t3_shape_areas(root, (("ca", ("3", "6", "13")), (SHARED_COMPARE_SCENE, (SHARED_COMPARE_ADOPTION,))))
    code, output = run_sheet(root, "cloud")
    run.expect("공유 compare.json·판정 장면별 채택(T3 모양) → 0", code == 0, f"종료 {code} {output.strip()[:200]}")
    if code == 0:
        sheet = read_json(area_dir(root, "cloud") / "r1" / "sheet.json")
        attributed = [(scene["scene"], scene["adoptions"], sorted({metric["adoption"] for metric in scene["metrics"]})) for scene in sheet["scenes"]]
        run.expect("공유 compare.json 귀속 = expect 판정 장면", attributed == [("ca", ["3", "6", "13"], ["13", "3", "6"]), (SHARED_COMPARE_SCENE, ["11"], ["11"])], str(attributed))
    code, output = run_sheet(root, "water")
    run.expect("T3 모양 water(채택 []·after null) → 0", code == 0, f"종료 {code} {output.strip()[:200]}")
    root = run.fresh_root()
    add_shared_compare_scene(root)
    every_cloud_adoption = ("3", "6", "11", "13")
    write_t3_shape_areas(root, (("ca", every_cloud_adoption), (SHARED_COMPARE_SCENE, every_cloud_adoption)))
    code, output = run_sheet(root, "cloud")
    run.expect("compare.json 장면 전부 귀속(T3 현재 모양) → 2·판 없음", code == 2 and "adoptions" in output and revisions(root, "cloud") == [], f"종료 {code} {output.strip()[:200]}")


def set_sweep_field(key, value):
    return lambda sweep: sweep.__setitem__(key, value)


def drop_sweep_field(key):
    return lambda sweep: sweep.pop(key)


def keep_photos(scene_dir):
    return None


def remove_photo(milliseconds):
    return lambda scene_dir: (scene_dir / f"00_schedule_{milliseconds}ms.png").unlink()


def rename_photo(old_milliseconds, new_milliseconds):
    return lambda scene_dir: (scene_dir / f"00_schedule_{old_milliseconds}ms.png").rename(scene_dir / f"00_schedule_{new_milliseconds}ms.png")


def truncate_photo(milliseconds):
    def truncate(scene_dir):
        target = scene_dir / f"00_schedule_{milliseconds}ms.png"
        target.write_bytes(target.read_bytes()[:20])
    return truncate


def replace_photo_with_text(milliseconds):
    return lambda scene_dir: (scene_dir / f"00_schedule_{milliseconds}ms.png").write_bytes(b"not a png file\n")


def clear_shots_expected(sweep):
    sweep["scenes"][0]["shots_expected"] = None


def write_edited_reshoot(root, folder, edit_sweep, edit_photos):
    run_dir = write_reshoot(root, folder, "fx9_a", 120)
    sweep = read_json(run_dir / "sweep.json")
    edit_sweep(sweep)
    write_json(run_dir / "sweep.json", sweep)
    edit_photos(run_dir / PLAN_SCENES["fx9_a"][0])
    return run_dir


RESHOOT_VARIANTS = (
    ("reshoot 온전한 실행 → 0", lambda sweep: None, keep_photos, 0, "재촬영 통과"),
    ("reshoot shots_expected null·이름 일치 → 0", clear_shots_expected, keep_photos, 0, "재촬영 통과"),
    ("reshoot invalid_count 1 → 1", lambda sweep: sweep.update(invalid_count=1, problems=["sa: 룩데브 종료 1"]), keep_photos, 1, "invalid_count 1"),
    ("reshoot invalid_count 없음 → 1", drop_sweep_field("invalid_count"), keep_photos, 1, "invalid_count None"),
    ("reshoot invalid_count 불리언 → 1", set_sweep_field("invalid_count", False), keep_photos, 1, "invalid_count False"),
    ("reshoot problems 있음(invalid_count 0) → 1", set_sweep_field("problems", ["sa: 단계 결과 없음"]), keep_photos, 1, "problems 1개"),
    ("reshoot problems 없음 → 1", drop_sweep_field("problems"), keep_photos, 1, "problems 목록 없음"),
    ("reshoot key_errors 있음 → 1", set_sweep_field("key_errors", ["apply: 모르는 키"]), keep_photos, 1, "key_errors 1개"),
    ("reshoot 부분 실행(3장 중 2장) → 1", lambda sweep: None, remove_photo(1500), 1, "≠ shots_expected 3"),
    ("reshoot 사진 이름 ≠ 0e(장수 같음) → 1", lambda sweep: None, rename_photo(1250, 1300), 1, "사진 이름 ≠ 0e"),
    ("reshoot 잘린 PNG → 1·예외 없음", lambda sweep: None, truncate_photo(1500), 1, "사진 해석 실패: sa/00_schedule_1500ms.png"),
    ("reshoot PNG 아닌 파일 → 1·예외 없음", lambda sweep: None, replace_photo_with_text(1000), 1, "사진 해석 실패: sa/00_schedule_1000ms.png"),
)


def case_reshoot_run_validity(run):
    root = run.fresh_root()
    for name, edit_sweep, edit_photos, expected_code, expected_text in RESHOOT_VARIANTS:
        write_edited_reshoot(root, "fx9_a", edit_sweep, edit_photos)
        code, output = run_tool(root, "--reshoot-check", f"{RESHOOT_DIR}/fx9_a")
        run.expect(name, code == expected_code and expected_text in output and "Traceback" not in output, f"종료 {code} {output.strip()[:200]}")


def smoke_after(root, revision="r1"):
    return read_json(area_dir(root, "smoke") / revision / "sheet.json")["scenes"][0]["after"]


def case_reshoot_sheet_fallback(run):
    root = run.fresh_root()
    write_edited_reshoot(root, "fx9_a", lambda sweep: sweep.update(invalid_count=1, problems=["sa: 룩데브 종료 1(원격 실행 끊김)"]), remove_photo(1500))
    write_edited_reshoot(root, "fx9_a-2", lambda sweep: None, keep_photos)
    code, output = run_sheet(root, "smoke")
    after = smoke_after(root) if code == 0 else {}
    attempts = after.get("reshoot_attempts") or []
    run.expect("앞 폴더 무효·부분 + -2 통과 → 시트 0·-2 사용", code == 0 and after.get("kind") == "reshoot" and str(after.get("source")).endswith("reshoot/fx9_a-2") and after.get("name") == "00_schedule_1250ms.png", f"종료 {code} {output.strip()[:200]}")
    run.expect("실패한 앞 폴더 시도 기록(sheet.json)", len(attempts) == 1 and attempts[0]["folder"].endswith("reshoot/fx9_a") and any("invalid_count" in line for line in attempts[0]["problems"]), json.dumps(attempts, ensure_ascii=False)[:200])
    root = run.fresh_root()
    write_edited_reshoot(root, "fx9_a", lambda sweep: None, truncate_photo(1250))
    write_edited_reshoot(root, "fx9_a-2", lambda sweep: None, keep_photos)
    code, output = run_sheet(root, "smoke")
    after = smoke_after(root) if code == 0 else {}
    run.expect("앞 폴더 잘린 PNG + -2 통과 → 시트 0·-2 사용·예외 없음", code == 0 and str(after.get("source")).endswith("reshoot/fx9_a-2") and "Traceback" not in output, f"종료 {code} {output.strip()[:200]}")
    root = run.fresh_root()
    write_edited_reshoot(root, "fx9_a", lambda sweep: None, rename_photo(1250, 1300))
    write_edited_reshoot(root, "fx9_a-2", lambda sweep: None, replace_photo_with_text(1250))
    code, output = run_sheet(root, "smoke")
    after = smoke_after(root) if code == 0 else {}
    run.expect("두 폴더 모두 실패 → 시트 0·후 없음", code == 0 and after.get("kind") == "none" and after.get("reason") == "후 없음(재촬영 통과 폴더 없음)" and len(after.get("reshoot_attempts") or []) == 2 and "Traceback" not in output, f"종료 {code} {output.strip()[:200]}")
    sheet_text = (area_dir(root, "smoke") / "r1" / "sheet.json").read_text(encoding="utf-8") if code == 0 else ""
    run.expect("시도 기록에 루트 절대 경로 없음(결정적 바이트)", code == 0 and "이미지 형식을 알 수 없음" in sheet_text and run.base.name not in sheet_text, "sheet.json reshoot_attempts")
    root = run.fresh_root()
    (write_reshoot(root, "fx9_a", "fx9_a", 120) / "sweep.json").unlink()
    code, output = run_sheet(root, "smoke")
    after = smoke_after(root) if code == 0 else {}
    sheet_text = (area_dir(root, "smoke") / "r1" / "sheet.json").read_text(encoding="utf-8") if code == 0 else ""
    run.expect("sweep.json 없는 재촬영 폴더(중단) → 후 없음·경로 없는 사유", code == 0 and after.get("kind") == "none" and [attempt["problems"] for attempt in after.get("reshoot_attempts") or []] == [["sweep.json 없음"], ["재촬영 없음"]] and run.base.name not in sheet_text, f"종료 {code} {json.dumps(after.get('reshoot_attempts'), ensure_ascii=False)[:200]}")


def case_reshoot_folder_appears(run):
    root = run.fresh_root()
    code, _ = run_sheet(root, "smoke")
    first = smoke_after(root) if code == 0 else {}
    run.expect("재촬영 없음 → r1 후 없음·두 폴더 '재촬영 없음' 기록", code == 0 and first.get("kind") == "none" and [attempt["problems"] for attempt in first.get("reshoot_attempts") or []] == [["재촬영 없음"], ["재촬영 없음"]], json.dumps(first, ensure_ascii=False)[:200])
    before = folder_hashes(area_dir(root, "smoke") / "r1") if code == 0 else {}
    write_reshoot(root, "fx9_a", "fx9_a", 120)
    code, output = run_sheet(root, "smoke")
    second = smoke_after(root, "r2") if code == 0 and "r2" in revisions(root, "smoke") else {}
    run.expect("재촬영 폴더 생김 → r2(새 판 없음 아님)·r1 불변", code == 0 and revisions(root, "smoke") == ["r1", "r2"] and second.get("kind") == "reshoot" and folder_hashes(area_dir(root, "smoke") / "r1") == before, f"종료 {code} {revisions(root, 'smoke')} {output.strip()[:160]}")
    code, output = run_sheet(root, "smoke", "--check")
    run.expect("재촬영 폴더 생긴 뒤 --check r2 일치 0", code == 0, f"종료 {code} {output.strip()[:160]}")
    truncate_photo(1500)(root / RESHOOT_DIR / "fx9_a" / PLAN_SCENES["fx9_a"][0])
    code, output = run_sheet(root, "smoke")
    third = smoke_after(root, "r3") if code == 0 and "r3" in revisions(root, "smoke") else {}
    run.expect("통과 폴더의 대표 아닌 사진 잘림 → r3·후 없음(새 판 없음 아님)", code == 0 and revisions(root, "smoke") == ["r1", "r2", "r3"] and third.get("kind") == "none", f"종료 {code} {revisions(root, 'smoke')} {output.strip()[:160]}")
    truncate_photo(1000)(root / RESHOOT_DIR / "fx9_a" / PLAN_SCENES["fx9_a"][0])
    code, output = run_sheet(root, "smoke")
    fourth = smoke_after(root, "r4") if code == 0 and "r4" in revisions(root, "smoke") else {}
    failures = [line for attempt in fourth.get("reshoot_attempts") or [] for line in attempt["problems"] if line.startswith("사진 해석 실패")]
    run.expect("실패 폴더 내용 바뀜(계속 실패) → r4·시도 기록 갱신", code == 0 and revisions(root, "smoke") == ["r1", "r2", "r3", "r4"] and len(failures) == 2, f"종료 {code} {revisions(root, 'smoke')} {failures}")


def case_unreadable_sheet_photos(run):
    for name, relative_dir in (("0e 대표 사진 잘림 → 2·예외 없음·판 없음", "Saved/FluidFX/calib/frames/fog_a/fa"), ("W11 대표 사진 잘림 → 2·예외 없음·판 없음", "Saved/FluidFX/calib/final/batch-e1/runs/fog_a/verify-1/fa")):
        root = run.fresh_root()
        truncate_photo(1250)(root / relative_dir)
        code, output = run_sheet(root, "fog")
        run.expect(name, code == 2 and "사진 해석 실패" in output and "00_schedule_1250ms.png" in output and "Traceback" not in output and revisions(root, "fog") == [], f"종료 {code} {output.strip()[:200]}")


K_JUDGE_PLAN = "fx9_a"
K9_RUN_DIR = f"Saved/FluidFX/calib/batch-K9/runs/{K_JUDGE_PLAN}"


def write_k_judge(root, batch, metric, reference):
    path = root / f"Saved/FluidFX/calib/{batch}/runs/{K_JUDGE_PLAN}/screen-1/screen.json"
    write_json(path, {"plan": f"Tools/FluidFX/calib/plans/{K_JUDGE_PLAN}.json", "scenes": [{"name": PLAN_SCENES[K_JUDGE_PLAN][0], "control_reference": {metric: reference}, "targets": [{"metric": metric, "low": 2.0, "high": 4.0, "band_source": "region"}]}]})
    return path


def write_judgeless_run(root, batch):
    run_dir = root / f"Saved/FluidFX/calib/{batch}/runs/{K_JUDGE_PLAN}"
    (run_dir / "screen-1").mkdir(parents=True, exist_ok=True)
    (run_dir / "lock.log").write_text("judge 없음\n", encoding="utf-8")


def smoke_k_judge(root):
    scene = read_json(area_dir(root, "smoke") / "r1" / "sheet.json")["scenes"][0]
    return scene["k_judge"], [(metric["metric"], metric["before"]["value"]) for metric in scene["metrics"]]


def case_k_judge_search_order(run):
    root = run.fresh_root()
    shutil.rmtree(root / K9_RUN_DIR)
    write_k_judge(root, "batch", "region.k1_only", 2.5)
    code, output = run_sheet(root, "smoke")
    judge, metrics = smoke_k_judge(root) if code == 0 else (None, [])
    run.expect("K 판정이 batch(K1)에만 → K1 판정·지표 1", code == 0 and judge == f"Saved/FluidFX/calib/batch/runs/{K_JUDGE_PLAN}/screen-1/screen.json" and metrics == [("region.k1_only", 2.5)], f"종료 {code} {judge} {metrics} {output.strip()[:160]}")
    root = run.fresh_root()
    write_k_judge(root, "batch", "region.k1_loses", 2.5)
    code, output = run_sheet(root, "smoke")
    judge, metrics = smoke_k_judge(root) if code == 0 else (None, [])
    run.expect("batch-K* 와 batch(K1) 모두 판정 → batch-K* 우선", code == 0 and judge == f"{K9_RUN_DIR}/screen-1/screen.json" and [name for name, _ in metrics] == [f"region.metric_{K_JUDGE_PLAN}"], f"종료 {code} {judge} {metrics}")
    root = run.fresh_root()
    (root / K9_RUN_DIR / "screen-1" / "screen.json").unlink()
    write_judgeless_run(root, "batch-K2")
    write_judgeless_run(root, "batch")
    write_judgeless_run(root, "batch-R5")
    write_k_judge(root, "batch-R2", "region.r2_older", 2.2)
    write_k_judge(root, "batch-R2b", "region.r2b_newer", 3.5)
    code, output = run_sheet(root, "smoke")
    judge, metrics = smoke_k_judge(root) if code == 0 else (None, [])
    run.expect("K·K1 폴더에 판정 파일 없음 + batch-R만 → 판정 있는 R 이름 내림차순 첫 폴더(R2b)", code == 0 and judge == f"Saved/FluidFX/calib/batch-R2b/runs/{K_JUDGE_PLAN}/screen-1/screen.json" and metrics == [("region.r2b_newer", 3.5)], f"종료 {code} {judge} {metrics} {output.strip()[:160]}")
    root = run.fresh_root()
    (root / K9_RUN_DIR / "screen-1" / "screen.json").unlink()
    write_judgeless_run(root, "batch-K2")
    write_k_judge(root, "batch", "region.k1_beats_r", 2.5)
    write_k_judge(root, "batch-R2b", "region.r_after_k1", 3.5)
    code, output = run_sheet(root, "smoke")
    judge, metrics = smoke_k_judge(root) if code == 0 else (None, [])
    run.expect("K 폴더에 판정 파일 없음 + batch(K1)·batch-R 둘 다 판정 → K1 우선(R 아님)", code == 0 and judge == f"Saved/FluidFX/calib/batch/runs/{K_JUDGE_PLAN}/screen-1/screen.json" and metrics == [("region.k1_beats_r", 2.5)], f"종료 {code} {judge} {metrics} {output.strip()[:160]}")
    root = run.fresh_root()
    shutil.rmtree(root / K9_RUN_DIR)
    code, output = run_sheet(root, "smoke")
    judge, metrics = smoke_k_judge(root) if code == 0 else ("종료 실패", [])
    run.expect("K·K1·R 판정 모두 없음 → k_judge 없음·지표 0(시트 0)", code == 0 and judge is None and metrics == [], f"종료 {code} {judge} {metrics} {output.strip()[:160]}")
    root = run.fresh_root()
    write_k_judge(root, "batch-K2", "region.k2_second", 2.5)
    write_judgeless_run(root, "batch-K3")
    code, output = run_sheet(root, "smoke")
    run.expect("판정 파일 있는 batch-K* 폴더 둘 → 2·판 없음", code == 2 and "K 판정 폴더가 둘 이상" in output and "batch-K3" not in output and revisions(root, "smoke") == [], f"종료 {code} {output.strip()[:200]}")


FREE_TEXT_PREFIX = "자유 문안: "
MISSING_BASIS = "근거 미표기"
FREE_TEXT_LONG_BASIS = "인라인 삭제 → 안개 kind 띠 [2.66,4.44](P-11) 0e 사진 세 장의 가운데 장과 W11 verify 가운데 장을 같은 예약 시각으로 맞춘 뒤 읽은 값을 띠로 삼는다"
FREE_TEXT_PROPOSAL_BASIS = "  E7-e 줄무늬 늘어남 1.5~6 [제안]  "
OVERFLOW_BASIS = FREE_TEXT_LONG_BASIS + " " + FREE_TEXT_LONG_BASIS
MIXED_BEFORE_TEXT = "전 정본 ≠ W 직전(보정 아닌 차이 섞임)"
WRAP_SENTENCE = "성능: 부분(통과 1 / 행 2 / 판정 불가 1 / 측정 불가 17 / 출하 밖 6) · 통과 1·미달 0·판정 불가 1·측정 불가 17·출하 밖 6 · PF 환산 ×2.3~3·상한 비교(증명 아님) · 표 b1886e95 · 성능 후보 1(perf-candidates: v02 TDDebrisCount 300→1000 · gain 4.28)"
D2_PERF_CANDIDATES = [
    {"plan": "Tools/FluidFX/calib/plans/w1b_b.json", "plan_sha256": "a" * 64, "variant": "v02", "gain": 4.278895512499599, "assignments": [{"json_path": "Look:NiagaraSystems./Game/FluidFX/Niagara/NS_TD_FluidWaterDebris.Ints.User.TDDebrisCount", "value": 1000, "split_group": False, "base": 300}], "recorded_at": "2026-11-01T05:01:53+00:00"},
    {"plan": "Tools/FluidFX/calib/plans/w5_a.json", "variant": "v03", "assignments": [{"json_path": "Tuning:Water.Foam.Strength", "value": 0.25, "base": 0.5}, {"json_path": "Tuning:Water.Foam.Radius", "value": 12, "base": 8}]},
    {"plan": "Tools/FluidFX/calib/plans/fog_a.json", "variant": "v09", "gain": 1.5, "assignments": []},
]
D2_PERF_EXPECTED = [("v02 TDDebrisCount 300→1000", "gain 4.28"), ("v03 Strength 0.5→0.25; Radius 8→12", "—")]
D2_PERF_TAIL = "성능 후보 2(perf-candidates: v02 TDDebrisCount 300→1000 · gain 4.28, v03 Strength 0.5→0.25; Radius 8→12 · —)"


class RecordingDraw:
    def __init__(self):
        self.texts = []

    def rectangle(self, *arguments, **keywords):
        return None

    def text(self, position, text, font=None, fill=None):
        self.texts.append((text, fill))


class DiscardingSheet:
    def paste(self, *arguments, **keywords):
        return None


def load_tool_module():
    specification = importlib.util.spec_from_file_location("approval_sheet_under_test", TOOL)
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


def set_plan_basis(root, plan, metric, basis):
    path = root / f"Tools/FluidFX/calib/plans/{plan}.json"
    document = read_json(path)
    for target in document["metrics"]["targets"]:
        if target["metric"] == metric:
            target["basis"] = basis
    write_json(path, document)


def sheet_scenes(root, area, revision="r1"):
    path = area_dir(root, area) / revision / "sheet.json"
    return {scene["scene"]: scene for scene in read_json(path)["scenes"]} if path.is_file() else {}


def first_band(scenes, name):
    metrics = scenes.get(name, {}).get("metrics") or []
    return (metrics[0].get("band") or {}) if metrics else {}


def drawn_metric_texts(module, fonts, scene):
    draw = RecordingDraw()
    module.draw_metric_cell(draw, fonts, scene, 0, 0)
    return draw.texts


def without_spaces(text):
    return text.replace(" ", "")


def case_free_text_basis(run, module, fonts):
    root = run.fresh_root()
    set_plan_basis(root, "fog_a", metric_of("4"), FREE_TEXT_LONG_BASIS)
    set_plan_basis(root, "fog_b", metric_of("14"), FREE_TEXT_PROPOSAL_BASIS)
    code, output = run_sheet(root, "fog")
    scenes = sheet_scenes(root, "fog") if code == 0 else {}
    long_band, proposal_band = first_band(scenes, "fa"), first_band(scenes, "fb")
    run.expect("D2-1 자유 문안 → '자유 문안: 원문'·basis 원문 그대로", code == 0 and long_band.get("basis_label") == FREE_TEXT_PREFIX + FREE_TEXT_LONG_BASIS and long_band.get("basis") == FREE_TEXT_LONG_BASIS, f"종료 {code} {long_band.get('basis_label')!r} {output.strip()[-160:]}")
    run.expect("D2-2 '[제안]' 든 자유 문안 → 라벨에 '[제안]' 보존(앞뒤 공백 제거)", code == 0 and proposal_band.get("basis_label") == FREE_TEXT_PREFIX + FREE_TEXT_PROPOSAL_BASIS.strip() and proposal_band.get("basis") == FREE_TEXT_PROPOSAL_BASIS, f"종료 {code} {proposal_band.get('basis_label')!r}")
    texts = [text for text, _ in drawn_metric_texts(module, fonts, scenes["fa"])] if "fa" in scenes else []
    band_line_count = len(texts) - 3
    run.expect("D2-1 그림 띠 줄 = 자유 문안 전부(2줄 이상 줄바꿈·'…' 없음)", without_spaces(FREE_TEXT_PREFIX + FREE_TEXT_LONG_BASIS) in without_spaces("".join(texts)) and band_line_count >= 2 and not any(text.endswith("…") for text in texts), f"그림 글 {len(texts)}줄 {texts[1:4]}")


def case_band_overflow_whole_metrics(run, module, fonts):
    root = run.fresh_root()
    for number in PLAN_SCENES["cloud_a"][1]:
        set_plan_basis(root, "cloud_a", metric_of(number), OVERFLOW_BASIS)
    code, output = run_sheet(root, "cloud")
    scenes = sheet_scenes(root, "cloud") if code == 0 else {}
    metric_count = len(scenes.get("ca", {}).get("metrics") or [])
    texts = drawn_metric_texts(module, fonts, scenes["ca"]) if "ca" in scenes else []
    groups, overflow = [], None
    for text, fill in texts[1:]:
        if text.startswith("… 외 "):
            overflow = text
        elif fill == module.TEXT_COLOR:
            groups.append([text])
        elif groups:
            groups[-1].append(text)
    complete = all(len(group) >= 3 and group[1].startswith("띠 ") and "띠 안 전" in group[-1] for group in groups)
    full = all(without_spaces(FREE_TEXT_PREFIX + OVERFLOW_BASIS) in without_spaces("".join(group)) for group in groups)
    undrawn = metric_count - len(groups)
    run.expect("D2-1 지표 칸 넘침 → 지표 단위 '외 n개'(그린 지표의 띠 줄은 끝까지)", code == 0 and 1 <= len(groups) < metric_count and complete and full and overflow is not None and overflow.startswith(f"… 외 {undrawn}개"), f"종료 {code} 지표 {metric_count} 그림 {len(groups)} 완전 {complete} 전문 {full} 넘침 {overflow!r}")


def case_missing_basis(run):
    for name, first, second in (("None·빈 문자열", None, ""), ("공백만·문자열 아님(목록)", "   ", ["live2_image"])):
        root = run.fresh_root()
        set_plan_basis(root, "fog_a", metric_of("4"), first)
        set_plan_basis(root, "fog_b", metric_of("14"), second)
        code, output = run_sheet(root, "fog")
        scenes = sheet_scenes(root, "fog") if code == 0 else {}
        bands = [first_band(scenes, "fa"), first_band(scenes, "fb")]
        labels = [band.get("basis_label") for band in bands]
        run.expect(f"D2-3 basis {name} → '근거 미표기'·basis 원문 그대로", code == 0 and labels == [MISSING_BASIS, MISSING_BASIS] and [band.get("basis") for band in bands] == [first, second], f"종료 {code} {labels} {output.strip()[-160:]}")


def case_mixed_before_header(run, module, fonts):
    root = run.fresh_root()
    code, output = run_sheet(root, "fog")
    scenes = sheet_scenes(root, "fog") if code == 0 else {}
    headers = {}
    for name, scene in scenes.items():
        draw = RecordingDraw()
        module.draw_photo_cells(DiscardingSheet(), draw, fonts, root, scene, 0, 0)
        headers[name] = draw.texts[0][0] if draw.texts else ""
    mixed = headers.get("fb", "")
    run.expect("D2-4 전 정본 ≠ W 직전 그림 머리줄 = 6.3 전체 문구·fit_text 그대로(fb만)", code == 0 and mixed.endswith(" · " + MIXED_BEFORE_TEXT) and module.fit_text(fonts["body"], mixed, module.CELL_WIDTH - 12) == mixed and "전 정본 ≠ W 직전" not in headers.get("fa", "전 정본 ≠ W 직전"), f"종료 {code} fb {mixed!r} fa {headers.get('fa')!r}")
    lines = [line for line in index_text(root, "fog").splitlines() if "장면 fb(" in line] if code == 0 else []
    run.expect("D2-4 index.md fb 줄 6.3 전체 문구", len(lines) == 1 and lines[0].endswith(" · " + MIXED_BEFORE_TEXT), str(lines)[:200])


def case_perf_candidate_text(run):
    root = run.fresh_root()
    write_json(root / "Saved/FluidFX/calib/perf-candidates.json", {"version": 1, "items": D2_PERF_CANDIDATES})
    code, output = run_sheet(root, "water")
    sheet = read_json(area_dir(root, "water") / "r1" / "sheet.json") if code == 0 else {}
    listed = [(item["item"], item["destination"]) for item in sheet.get("perf_candidates") or []]
    run.expect("D2-5 perf-candidates 키(variant·gain·assignments) → sheet.json item·destination", code == 0 and listed == D2_PERF_EXPECTED, f"종료 {code} {listed} {output.strip()[-160:]}")
    text = index_text(root, "water") if code == 0 else ""
    perf_lines = [line for line in text.splitlines() if line.startswith("- 성능")]
    run.expect("D2-5 index.md 성능 줄 한 줄에 variant·gain·30줄 이하", len(perf_lines) == 1 and perf_lines[0].endswith(D2_PERF_TAIL) and len(text.splitlines()) <= 30, f"{len(text.splitlines())}줄 {perf_lines[0][-160:] if perf_lines else '성능 줄 없음'}")


def case_word_wrap(run, module, fonts):
    font = fonts["body"]
    widest = max(font.getlength(word) for word in WRAP_SENTENCE.split(" "))
    widths = sorted({*range(int(widest) + 1, module.PERF_WIDTH - 16, 3), module.PERF_WIDTH - 16})
    broken = []
    for width in widths:
        lines = module.wrap_text(font, WRAP_SENTENCE, width)
        if " ".join(lines) != WRAP_SENTENCE or any(font.getlength(line) > width or line.startswith(" ") for line in lines):
            broken.append(width)
    perf_cell_lines = module.wrap_text(font, WRAP_SENTENCE, module.PERF_WIDTH - 16)
    run.expect("D2-6 wrap_text 공백에서만 끊음(낱말이 폭 안인 모든 폭)", not broken and len(perf_cell_lines) >= 2, f"폭 {len(widths)}개 중 낱말 중간 끊김 {len(broken)} {broken[:6]} · 성능 칸 {perf_cell_lines[:3]}")
    long_word = "TDDebrisCount"
    narrow = font.getlength(long_word) / 2
    pieces = module.wrap_text(font, f"값 {long_word} 끝", narrow)
    run.expect("D2-6 폭보다 긴 낱말만 글자 단위", len(pieces) >= 3 and pieces[0] == "값" and "".join(pieces[1:]).replace(" ", "") == long_word + "끝" and all(font.getlength(piece) <= narrow for piece in pieces), str(pieces))


def main():
    if Image is None:
        print("PIL(Pillow) 없음", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="t4-fixture-") as base:
        run = FixtureRun(Path(base))
        root = case_fake_scenes_two(run)
        case_same_input_twice(run, root)
        case_deterministic_bytes(run, root)
        case_approval_only(run, root)
        case_section_seven_labels(run, root)
        case_structural_gaps(run, root)
        case_reshoot_check(run, root)
        case_changed_input_new_revision(run, root)
        case_pair_time_mismatch(run)
        case_outside_saved(run)
        case_missing_inputs(run)
        case_index_over_limit(run)
        case_user_item_counts(run)
        case_area_definition(run)
        case_perf_counts(run)
        case_shared_compare_adoptions(run)
        case_reshoot_run_validity(run)
        case_reshoot_sheet_fallback(run)
        case_reshoot_folder_appears(run)
        case_unreadable_sheet_photos(run)
        case_k_judge_search_order(run)
        module = load_tool_module()
        fonts = module.load_fonts()
        case_free_text_basis(run, module, fonts)
        case_band_overflow_whole_metrics(run, module, fonts)
        case_missing_basis(run)
        case_mixed_before_header(run, module, fonts)
        case_perf_candidate_text(run)
        case_word_wrap(run, module, fonts)
    failed = [name for name, passed, _ in run.results if not passed]
    print(f"합계: 통과 {len(run.results) - len(failed)} / {len(run.results)}, 실패 {len(failed)}")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())

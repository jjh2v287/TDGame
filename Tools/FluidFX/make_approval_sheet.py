"""실행 환경: 시스템 Python 3.12 + PIL(Pillow), 프로젝트 루트(PowerShell). 에디터·PIE·MCP 불필요, 글꼴 C:/Windows/Fonts/malgun.ttf 고정
실행 명령: python -B Tools/FluidFX/make_approval_sheet.py --area <영역> --areas <sheet_areas.json> --perf <p-summary.json> [--check] [--root <사본 루트>] [--out-root <Saved 아래 폴더>] | python -B Tools/FluidFX/make_approval_sheet.py --reshoot-check <재촬영 실행 폴더> [--root <사본 루트>]
출력: <out-root, 기본 Saved/FluidFX/approval>/<영역>/r<n>/{sheet.png, sheet-2.png…, index.md, sheet.json}·<영역>/current.json(입력 sha가 최신 판과 같으면 새 판 없음), 표준 출력 요약. 종료 0 성공·새 판 없음·--check 일치·재촬영 통과 / 1 출력·그림이 Saved 밖, 전후 짝 시각 불일치, 재촬영 실패·없음 / 2 입력 누락·형식(0e·W11 사진 해석 실패 포함)·sha·정의 불일치, index.md 30줄 초과, 필수 항목 누락, --check 바이트 불일치
상태: 새 도구(2026-11-06 claude 하위 에이전트, runbook-p-v3 5절 T4·6절·7절, 반박 r1 must 2 반영: --perf counts 다섯 키·종합 수 일치·표 sha 64자 필수, 반박 r2 must M3·M4 반영: 재촬영 통과에 invalid_count 0·problems·key_errors 빈 목록·장면 00_schedule 사진 수 = shots_expected·0e 사진 이름 집합 일치 추가, 재촬영 사진 해석 실패는 그 폴더 실패, 시도한 재촬영 폴더 전체를 입력 sha에 넣음, AM7 L4: K 판정 찾기 batch-K* → batch(K1) → batch-R* 이름 내림차순, 판정 파일 없는 폴더는 건너뜀). fixture Tools/FluidFX/perf/p/fixtures_t4.py 통과, 실제 P7 입력(T3 p-summary)으로 r1 생성(2026-11-07). 결함 D2(2026-11-07): 6.4 자유 문안·6.3 머리줄 괄호·perf-candidates 표기·낱말 줄바꿈
"""
import argparse
import hashlib
import io
import json
import os
import re
import struct
import sys
import tempfile
from pathlib import Path

try:
    import PIL
    from PIL import Image, ImageDraw, ImageFont, ImageStat
    PHOTO_READ_ERRORS = (OSError, SyntaxError, ValueError, EOFError, struct.error, Image.DecompressionBombError)
except ImportError:
    PIL = None
    PHOTO_READ_ERRORS = (OSError,)

TOOL_PATH = Path(__file__).resolve()
DEFAULT_ROOT = TOOL_PATH.parents[2]
FONT_PATH = Path("C:/Windows/Fonts/malgun.ttf")

EXIT_OK = 0
EXIT_POLICY = 1
EXIT_INPUT = 2

CANONICAL_TUNING_SHA256 = "9ea0339795e64446ce8962f4816512a2c5284ce8ad69662ebb94481045b95d0b"
CANONICAL_LOOK_SHA256 = "22a5987c9f5e2fd24f99cfc63b4940af952b9f0e754f331e31d2595b1ea4d73c"
MINIMUM_RESHOOT_LUMA = 18.0
INDEX_LINE_LIMIT = 30
ROWS_PER_PAGE = 4
LISTED_TEXT_LIMIT = 110

FRAMES_DIR = "Saved/FluidFX/calib/frames"
CALIB_DIR = "Saved/FluidFX/calib"
PLANS_DIR = "Tools/FluidFX/calib/plans"
VERDICT_PATH = "Saved/FluidFX/calib/final/verdict-w11.json"
EXPECT_PATH = "Saved/FluidFX/calib/final/expect.json"
APPROVAL_ONLY_PATH = "Tools/FluidFX/calib/approval-only.json"
STRUCTURAL_GAPS_PATH = "Tools/FluidFX/calib/structural-gaps.json"
PERF_CANDIDATES_PATH = "Saved/FluidFX/calib/perf-candidates.json"
OVERLAY_EMPTY_PATH = "Saved/FluidFX/calib/final/overlay-empty.json"
RESHOOT_DIR = "Saved/FluidFX/approval/reshoot"
DEFAULT_OUT_ROOT = "Saved/FluidFX/approval"
WAVE3_BEFORE_DIR = "Saved/AgentOps/20261030/wave3-assets/a8"
WAVE3_AFTER_DIR = "Saved/AgentOps/20261106/w/w10-pass1/pie-shots"

AREAS = ("fire", "smoke", "fog", "water", "cloud", "input", "track", "viscous", "foliage")
AREA_TITLES = {"fire": "불", "smoke": "연기·볼륨", "fog": "안개", "water": "물", "cloud": "구름", "input": "입력·대량", "track": "자국", "viscous": "점성", "foliage": "식생"}
PLAN_AREA_PREFIXES = (
    ("fx5_", "input"), ("input_", "input"), ("campfire_", "fire"), ("fx8n_", "fire"),
    ("fx9_", "smoke"), ("fx15a_", "smoke"), ("fx16a_", "smoke"), ("fx16b_", "smoke"),
    ("fog_", "fog"), ("w1b_", "water"), ("w3_", "water"), ("w5_", "water"), ("w6_", "water"), ("river_", "water"),
    ("cloud_", "cloud"), ("track_", "track"), ("viscous_", "viscous"), ("foliage_", "foliage"),
)
UNAPPROVED_TWELVE = "⑫ 미승인 위에서 측정"
AREA_LABELS = {
    "fire": (),
    "smoke": ("㉝ fx9 hvol W 전 제거(FX-P 출하 배치 장면 계획 후보)",),
    "fog": (UNAPPROVED_TWELVE,),
    "water": (UNAPPROVED_TWELVE, "㉒ 미승인 위에서 측정(#17)", "판정 불가(구조) ㉓(수중)", "D-60 미결(GPU 물 기본 꺼짐·판정 보류)"),
    "cloud": (UNAPPROVED_TWELVE, "판정 불가(구조) ㉓(g4 painter)"),
    "input": (UNAPPROVED_TWELVE,),
    "track": (UNAPPROVED_TWELVE,),
    "viscous": (UNAPPROVED_TWELVE,),
    "foliage": ("deferred ㉘(k2 반감기)",),
}
RETAINED_FIFTY = {"cloud": ("3", "6", "11", "13"), "track": ("9", "12"), "water": ("17",)}
RETAINED_FIFTY_MARK = "재현 근거 없음(유지)"
DESIGN_ASSERT_AREA = "cloud"
DESIGN_ASSERT_MARK = "WindCurtainStamps"
DESIGN_ASSERT_LABEL = "㊻ 미결(#13 유지) 위에서 측정"
PERF_LABEL = "PF 환산 ×2.3~3·상한 비교(증명 아님)"
STATUS_LABEL = "승인 대기"
PROPOSED_PASS_LABEL = "[제안] 기준 충족(동등 증명 아님)"
FORBIDDEN_PHRASES = ("동등 확인", "동일 이상")
PENDING_PATHS = {
    "fire": ("Saved/FluidFX/showcase/D-49-fx3-fire-2026-10-18.gif",),
    "water": ("Saved/FluidFX/showcase/w1-river-20261023-021209", "Saved/FluidFX/showcase/fx4b-river-triptych-20261025-025943"),
}
PENDING_WITHOUT_PICTURES = {
    "smoke": ("fx16b_fadeout 0·0.5·1초",),
    "cloud": ("구름 초승달 그림자",),
    "water": ("탱크 코스틱", "해변 물+안개 X4"),
    "fog": ("해변 물+안개 X4",),
    "foliage": ("갈대",),
}
WAVE3_PAIRS = {
    "cloud": ("fx11c_cloud_paint.png",),
    "viscous": ("fx14_viscous_flow_decal.png",),
    "water": ("w5_underwater_pit_over.png", "w5_underwater_pit_under.png"),
}
PERF_COUNT_KEYS = ("통과", "미달", "판정 불가", "측정 불가", "출하 밖")
PERF_TABLELESS_AREAS = ("foliage",)
OVERALL_COUNT = re.compile("(" + "|".join(re.escape(key) for key in PERF_COUNT_KEYS) + r") (\d+)")
SHA256_TEXT = re.compile(r"^[0-9a-f]{64}$")
BASIS_LABELS = {"live2_image": "LIVE-2 이미지", "spec_proposal": "[제안] 동등 증명 아님(P-11)"}
FREE_TEXT_BASIS_PREFIX = "자유 문안: "
MISSING_BASIS_LABEL = "근거 미표기"
MIXED_BEFORE_LABEL = "전 정본 ≠ W 직전(보정 아닌 차이 섞임)"
SAFE_NAME = re.compile(r"^[A-Za-z0-9_.\-]+$")
SCHEDULE_PHOTO = re.compile(r"^00_schedule_(\d+)ms\.png$")
ANY_SCHEDULE_PHOTO = re.compile(r"^\d\d_schedule_(\d+)ms\.png$")
IMAGE_REFERENCE = re.compile(r"^IMG\s?(\d+)$")
REVISION_NAME = re.compile(r"^r(\d+)$")

PAGE_TITLE_HEIGHT = 64
ROW_TITLE_HEIGHT = 30
CELL_WIDTH = 640
CELL_HEIGHT = 360
PERF_WIDTH = 320
ROW_GAP = 6
CELL_HEADER_HEIGHT = 24
METRIC_LINE_HEIGHT = 17
PAGE_WIDTH = CELL_WIDTH * 3 + PERF_WIDTH
BACKGROUND_COLOR = (20, 20, 24)
PANEL_COLOR = (34, 34, 40)
HEADER_COLOR = (0, 0, 0)
TEXT_COLOR = (235, 235, 235)
MUTED_COLOR = (170, 170, 170)
WARNING_COLOR = (255, 190, 90)


class SheetError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def fail_input(message):
    raise SheetError(EXIT_INPUT, message)


def fail_policy(message):
    raise SheetError(EXIT_POLICY, message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_directory(path):
    digest = hashlib.sha256()
    files = sorted((item for item in path.rglob("*") if item.is_file()), key=lambda item: item.relative_to(path).as_posix().lower())
    for file in files:
        digest.update(f"{file.relative_to(path).as_posix()}\0{sha256_file(file)}\n".encode("utf-8"))
    return digest.hexdigest()


def is_inside(path, folder):
    try:
        path.resolve().relative_to(folder.resolve())
    except ValueError:
        return False
    return True


def json_bytes(document):
    return (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def write_bytes_atomically(path, data):
    temporary = path.parent / f".{path.name}.tmp-{os.getpid()}"
    with temporary.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def shorten(text, limit=LISTED_TEXT_LIMIT):
    flat = " ".join(str(text).split())
    return flat if len(flat) <= limit else flat[: limit - 1] + "…"


def format_number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return "—"
    return f"{value:.4g}"


def area_of_plan(plan_id):
    for prefix, area in PLAN_AREA_PREFIXES:
        if plan_id.startswith(prefix):
            return area
    return None


def plan_id_from_path(text):
    name = Path(str(text)).name
    return name[:-5] if name.endswith(".json") else name


def normalized_relative(text, root):
    path = Path(str(text).replace("\\", "/"))
    if path.is_absolute():
        try:
            path = path.resolve().relative_to(root.resolve())
        except ValueError:
            return path.as_posix().lower()
    return path.as_posix().lower()


def schedule_photos(scene_dir, pattern=SCHEDULE_PHOTO):
    if not scene_dir.is_dir():
        return []
    photos = []
    for item in scene_dir.iterdir():
        match = pattern.match(item.name)
        if match and item.is_file():
            photos.append((int(match.group(1)), item.name, item))
    return sorted(photos)


def middle_photo(photos):
    return photos[(len(photos) - 1) // 2]


class InputLedger:
    def __init__(self, root):
        self.root = root
        self.hashes = {}
        self.json_cache = {}

    def relative_text(self, path):
        try:
            return path.resolve().relative_to(self.root.resolve()).as_posix()
        except ValueError:
            return path.resolve().as_posix()

    def resolve(self, text):
        path = Path(str(text))
        return path if path.is_absolute() else self.root / path

    def record(self, path):
        key = self.relative_text(path)
        if key in self.hashes:
            return self.hashes[key]
        if path.is_dir():
            digest = sha256_directory(path)
        elif path.is_file():
            digest = sha256_file(path)
        else:
            fail_input(f"입력 없음: {key}")
        self.hashes[key] = digest
        return digest

    def load_json(self, path):
        key = self.relative_text(path)
        if key in self.json_cache:
            return self.json_cache[key]
        if not path.is_file():
            fail_input(f"입력 없음: {key}")
        data = path.read_bytes()
        try:
            document = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as error:
            fail_input(f"JSON 해석 실패: {key} ({error})")
        self.hashes[key] = sha256_bytes(data)
        self.json_cache[key] = document
        return document

    def sorted_inputs(self):
        return [{"path": path, "sha256": self.hashes[path]} for path in sorted(self.hashes, key=str.lower)]


def check_reshoot_run(root, run_dir):
    sweep_path = run_dir / "sweep.json"
    if not sweep_path.is_file():
        return ["sweep.json 없음"], []
    try:
        data = sweep_path.read_bytes()
    except OSError as error:
        return [f"sweep.json 읽기 실패: {type(error).__name__} {error.strerror}"], []
    try:
        sweep = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        return [f"sweep.json 해석 실패: {error}"], []
    if not isinstance(sweep, dict):
        return ["sweep.json 최상위가 객체가 아님"], []
    expected_plan = run_dir.name[:-2] if run_dir.name.endswith("-2") else run_dir.name
    problems = reshoot_identity_problems(root, sweep, expected_plan)
    problems.extend(reshoot_validity_problems(sweep))
    scenes = sweep.get("scenes")
    if not isinstance(scenes, list):
        problems.append("scenes 목록 없음")
        scenes = []
    photos = []
    for scene in scenes:
        scene_problems, scene_photos = reshoot_scene_problems(root, run_dir, expected_plan, scene)
        problems.extend(scene_problems)
        photos.extend(scene_photos)
    if not photos:
        problems.append("사진 0장")
    lumas = []
    for path in photos:
        try:
            lumas.append(mean_luma(path))
        except PHOTO_READ_ERRORS as error:
            problems.append(f"사진 해석 실패: {path.relative_to(run_dir).as_posix()} ({photo_error_text(error)})")
    if lumas and min(lumas) < MINIMUM_RESHOOT_LUMA:
        problems.append(f"평균 휘도 최소 {min(lumas):.3f} < {MINIMUM_RESHOOT_LUMA:g}")
    return problems, lumas


def reshoot_identity_problems(root, sweep, expected_plan):
    problems = []
    if sweep.get("plan_id") != expected_plan:
        problems.append(f"plan_id {sweep.get('plan_id')!r} ≠ 폴더 계획 {expected_plan!r}")
    if sweep.get("stage") != "frame":
        problems.append(f"stage {sweep.get('stage')!r} ≠ 'frame'")
    if sweep.get("canonical_tuning_sha256") != CANONICAL_TUNING_SHA256 or sweep.get("canonical_look_sha256") != CANONICAL_LOOK_SHA256:
        problems.append("정본 sha ≠ P0 ①(9ea03397·22a5987c)")
    after = sweep.get("canonical_sha256_after")
    if isinstance(after, dict) and (after.get("tuning") != CANONICAL_TUNING_SHA256 or after.get("look") != CANONICAL_LOOK_SHA256):
        problems.append("실행 뒤 정본 sha ≠ P0 ①")
    if normalized_relative(sweep.get("overlay", ""), root) != OVERLAY_EMPTY_PATH.lower():
        problems.append(f"overlay {sweep.get('overlay')!r} ≠ {OVERLAY_EMPTY_PATH}")
    overlay_file = root / OVERLAY_EMPTY_PATH
    if not overlay_file.is_file() or sweep.get("overlay_sha256") != sha256_file(overlay_file):
        problems.append("overlay_sha256 ≠ overlay-empty.json 현재 sha")
    return problems


def reshoot_validity_problems(sweep):
    problems = []
    invalid_count = sweep.get("invalid_count")
    if isinstance(invalid_count, bool) or not isinstance(invalid_count, int) or invalid_count != 0:
        problems.append(f"invalid_count {invalid_count!r} ≠ 0(무효·중단 실행)")
    for key in ("problems", "key_errors"):
        value = sweep.get(key)
        if not isinstance(value, list):
            problems.append(f"{key} 목록 없음")
        elif value:
            problems.append(f"{key} {len(value)}개: {shorten(value[0], 80)}")
    return problems


def reshoot_scene_problems(root, run_dir, plan, scene):
    name = scene.get("name") if isinstance(scene, dict) else None
    if not isinstance(name, str) or not SAFE_NAME.match(name):
        return [f"장면 이름 오류: {name!r}"], []
    problems = []
    scheduled = schedule_photos(run_dir / name)
    expected = scene.get("shots_expected")
    if isinstance(expected, int) and not isinstance(expected, bool) and len(scheduled) != expected:
        problems.append(f"{name}: 00_schedule 사진 {len(scheduled)}장 ≠ shots_expected {expected}")
    names = {photo_name for _, photo_name, _ in scheduled}
    before_names = {photo_name for _, photo_name, _ in schedule_photos(root / FRAMES_DIR / plan / name)}
    if names != before_names:
        first = sorted(names ^ before_names)[0]
        problems.append(f"{name}: 00_schedule 사진 이름 ≠ 0e {FRAMES_DIR}/{plan}/{name}(재촬영 {len(names)}장·0e {len(before_names)}장, 첫 차이 {first})")
    return problems, [path for _, _, path in schedule_photos(run_dir / name, ANY_SCHEDULE_PHOTO)]


def photo_error_text(error):
    if isinstance(error, PIL.UnidentifiedImageError):
        return "이미지 형식을 알 수 없음"
    if isinstance(error, OSError) and error.filename:
        return f"{type(error).__name__}: {error.strerror}"
    return f"{type(error).__name__}: {error}"


def mean_luma(path):
    with Image.open(path) as image:
        return ImageStat.Stat(image.convert("L")).mean[0]


def run_reshoot_check(root, text):
    run_dir = Path(text) if Path(text).is_absolute() else root / text
    if not run_dir.is_dir():
        print(f"재촬영 없음: {text}")
        return EXIT_POLICY
    problems, lumas = check_reshoot_run(root, run_dir)
    if problems:
        print(f"재촬영 실패: {text}")
        for problem in problems:
            print(f"  - {problem}")
        return EXIT_POLICY
    print(f"재촬영 통과: {text} (사진 {len(lumas)}장, 휘도 최소 {min(lumas):.3f})")
    return EXIT_OK


def normalized_area_entries(sheet_areas):
    areas = sheet_areas.get("areas")
    if isinstance(areas, dict):
        return areas
    if isinstance(areas, list):
        entries = {}
        for entry in areas:
            if not isinstance(entry, dict) or not isinstance(entry.get("area"), str) or entry["area"] in entries:
                fail_input("sheet_areas.json areas 목록 항목에 area 가 없거나 겹친다")
            entries[entry["area"]] = entry
        return entries
    fail_input("sheet_areas.json 에 areas(객체 또는 목록)가 없다")


def read_area_definition(ledger, areas_path, area):
    sheet_areas = ledger.load_json(areas_path)
    if not isinstance(sheet_areas, dict):
        fail_input("sheet_areas.json 최상위가 객체가 아님")
    entry = normalized_area_entries(sheet_areas).get(area)
    if not isinstance(entry, dict) or not isinstance(entry.get("scenes"), list) or not entry["scenes"]:
        fail_input(f"sheet_areas.json 에 영역 {area} 의 scenes 가 없다")
    reshoot = sheet_areas.get("reshoot")
    if not isinstance(reshoot, list) or not all(isinstance(plan, str) for plan in reshoot):
        fail_input("sheet_areas.json reshoot 목록이 없다")
    declared = []
    seen = set()
    for scene in entry["scenes"]:
        if not isinstance(scene, dict):
            fail_input(f"영역 {area} scenes 항목이 객체가 아님")
        plan, name = scene.get("plan"), scene.get("scene")
        if not isinstance(plan, str) or not isinstance(name, str) or not SAFE_NAME.match(plan) or not SAFE_NAME.match(name):
            fail_input(f"영역 {area} scenes 항목 plan·scene 이름 오류: {scene!r}")
        if (plan, name) in seen:
            fail_input(f"영역 {area} 장면 겹침: {plan}/{name}")
        seen.add((plan, name))
        adoptions = scene.get("adoptions")
        if adoptions is not None and (not isinstance(adoptions, list) or not all(isinstance(item, str) for item in adoptions)):
            fail_input(f"영역 {area} {plan}/{name} adoptions 는 문자열 목록이어야 한다")
        declared.append({"plan": plan, "scene": name, "declared_adoptions": adoptions})
    return declared, set(reshoot)


def numbered_adoptions(verdict):
    adoptions = verdict.get("adoptions")
    if not isinstance(adoptions, dict):
        fail_input("verdict-w11.json adoptions 가 없다")
    return {key: value for key, value in adoptions.items() if key.isdigit() and isinstance(value, dict)}


def evidence_paths(adoption_entry, suffix):
    evidence = adoption_entry.get("evidence")
    return [path for path in evidence if isinstance(path, str) and path.endswith(suffix)] if isinstance(evidence, list) else []


def chosen_compare_path(adoption_entry):
    compares = evidence_paths(adoption_entry, "/compare.json")
    if not compares:
        return None
    opportunity = (adoption_entry.get("state_source") or {}).get("opportunity")
    return compares[-1] if opportunity == "rerun" else compares[0]


def wab_screen_path(adoption_entry):
    screens = [path for path in evidence_paths(adoption_entry, "/screen.json") if "-wab/" in path]
    return screens[0] if screens else None


def adoption_area(adoption_entry):
    item_plan = (adoption_entry.get("state_source") or {}).get("item_plan")
    return area_of_plan(plan_id_from_path(item_plan)) if isinstance(item_plan, str) else None


def read_user_items(verdict):
    decisions = verdict.get("user_decision")
    if not isinstance(decisions, list):
        fail_input("verdict-w11.json user_decision 목록이 없다")
    retained = sorted({str(item.get("adoption")) for item in decisions if isinstance(item, dict) and item.get("adoption") is not None and RETAINED_FIFTY_MARK in str(item.get("label", ""))}, key=int)
    expected = sorted({number for numbers in RETAINED_FIFTY.values() for number in numbers}, key=int)
    if retained != expected:
        fail_input(f"㊿ 유지 항목 {retained} ≠ 7절 {expected}(7개)")
    asserts = [item for item in decisions if isinstance(item, dict) and item.get("adoption") is None and DESIGN_ASSERT_MARK in str(item.get("label", ""))]
    if len(asserts) != 1:
        fail_input(f"㊻ 설계 단언({DESIGN_ASSERT_MARK}) {len(asserts)}개 ≠ 1")
    return retained, str(asserts[0].get("label"))


def find_scene(document, name):
    scenes = document.get("scenes") if isinstance(document, dict) else None
    if not isinstance(scenes, list):
        return None
    for scene in scenes:
        if isinstance(scene, dict) and scene.get("name") == name:
            return scene
    return None


def find_target(judge_scene, metric):
    targets = judge_scene.get("targets") if isinstance(judge_scene, dict) else None
    for target in targets if isinstance(targets, list) else []:
        if isinstance(target, dict) and target.get("metric") == metric:
            return target
    return None


def plan_basis(ledger, judge, scene_name, metric):
    plan_text = judge.get("plan") if isinstance(judge, dict) else None
    if not isinstance(plan_text, str):
        return None
    plan = ledger.load_json(ledger.resolve(plan_text))
    targets = (plan.get("metrics") or {}).get("targets") if isinstance(plan, dict) else None
    for target in targets if isinstance(targets, list) else []:
        if not isinstance(target, dict) or target.get("metric") != metric:
            continue
        if target.get("scene") in (None, scene_name):
            return target.get("basis")
    return None


def basis_label(basis):
    if not isinstance(basis, str) or not basis.strip():
        return MISSING_BASIS_LABEL
    return BASIS_LABELS.get(basis, FREE_TEXT_BASIS_PREFIX + basis.strip())


def band_from_target(ledger, judge, scene_name, metric):
    target = find_target(find_scene(judge, scene_name), metric)
    if target is None:
        return None
    basis = plan_basis(ledger, judge, scene_name, metric)
    return {
        "low": target.get("low"),
        "high": target.get("high"),
        "band_source": target.get("band_source"),
        "basis": basis,
        "basis_label": basis_label(basis),
    }


def inside_band(value, band):
    if band is None or isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    low, high = band.get("low"), band.get("high")
    if not isinstance(low, (int, float)) and not isinstance(high, (int, float)):
        return None
    if isinstance(low, (int, float)) and value < low:
        return False
    if isinstance(high, (int, float)) and value > high:
        return False
    return True


def numeric_or_none(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def compare_value(compare, scene_name, metric):
    checks = compare.get("checks") if isinstance(compare, dict) else None
    for check in checks if isinstance(checks, list) else []:
        if isinstance(check, dict) and check.get("scene") == scene_name and check.get("metric") == metric:
            return numeric_or_none(check.get("value"))
    return None


def before_from_wab(ledger, adoption_entry, scene_name, metric):
    screen_text = wab_screen_path(adoption_entry)
    if screen_text is None:
        return None
    screen = ledger.load_json(ledger.resolve(screen_text))
    variant = ((find_scene(screen, scene_name) or {}).get("variants") or {}).get("v01") or {}
    return numeric_or_none((variant.get("medians") or {}).get(metric))


def adopted_metrics(ledger, expect, adoptions, adoption_ids, scene_name):
    rows = [row for row in expect.get("rows", []) if isinstance(row, dict) and row.get("scene") == scene_name and str(row.get("adoption")) in adoption_ids]
    metrics = []
    for row in sorted(rows, key=lambda item: (int(str(item["adoption"])), str(item.get("metric")))):
        adoption_id = str(row["adoption"])
        metric = str(row.get("metric"))
        judge_text = (row.get("source") or {}).get("judge")
        if not isinstance(judge_text, str):
            fail_input(f"expect.json 행 {adoption_id}/{scene_name}/{metric} 에 source.judge 가 없다")
        judge = ledger.load_json(ledger.resolve(judge_text))
        band = band_from_target(ledger, judge, scene_name, metric)
        before_value = before_from_wab(ledger, adoptions[adoption_id], scene_name, metric)
        before_source = "손잡이 1개 되돌림(A/B v01)"
        if before_value is None:
            before_value = numeric_or_none(((find_scene(judge, scene_name) or {}).get("control_reference") or {}).get(metric))
            before_source = "상류 덧씌움 섞임(K control_reference)"
        if before_value is None:
            before_source = "—"
        compare_text = chosen_compare_path(adoptions[adoption_id])
        after_value = compare_value(ledger.load_json(ledger.resolve(compare_text)), scene_name, metric) if compare_text else None
        metrics.append({
            "adoption": adoption_id,
            "metric": metric,
            "band": band,
            "before": {"value": before_value, "source": before_source},
            "expected": numeric_or_none(row.get("expected")),
            "after": {"value": after_value, "label": None, "source": compare_text},
            "in_band": {"before": inside_band(before_value, band), "after": inside_band(after_value, band)},
            "judge": judge_text,
        })
    return metrics


def judge_file_of(run_dir):
    for candidate in ("confirm-1/judge.json", "screen-1/screen.json"):
        if (run_dir / candidate).is_file():
            return run_dir / candidate
    return None


def find_k_judge(ledger, plan):
    calib_dir = ledger.root / CALIB_DIR
    if not calib_dir.is_dir():
        return None
    k_run_dirs = sorted(calib_dir.glob(f"batch-K*/runs/{plan}"), key=lambda path: path.as_posix().lower())
    k_judges = [judge for judge in map(judge_file_of, k_run_dirs) if judge is not None]
    if len(k_judges) > 1:
        fail_input(f"K 판정 폴더가 둘 이상: {[ledger.relative_text(judge.parents[1]) for judge in k_judges]}")
    if k_judges:
        return k_judges[0]
    k1_judge = judge_file_of(calib_dir / "batch" / "runs" / plan)
    if k1_judge is not None:
        return k1_judge
    r_run_dirs = sorted(calib_dir.glob(f"batch-R*/runs/{plan}"), key=lambda path: path.parents[1].name, reverse=True)
    return next((judge for judge in map(judge_file_of, r_run_dirs) if judge is not None), None)


def unadopted_metrics(ledger, plan, scene_name, after_label):
    judge_path = find_k_judge(ledger, plan)
    if judge_path is None:
        return [], None
    judge = ledger.load_json(judge_path)
    judge_scene = find_scene(judge, scene_name) or {}
    references = judge_scene.get("control_reference") or {}
    targets = [target for target in judge_scene.get("targets") or [] if isinstance(target, dict) and isinstance(target.get("metric"), str)]
    metrics = []
    for target in sorted(targets, key=lambda item: item["metric"]):
        metric = target["metric"]
        band = band_from_target(ledger, judge, scene_name, metric)
        before_value = numeric_or_none(references.get(metric))
        metrics.append({
            "adoption": None,
            "metric": metric,
            "band": band,
            "before": {"value": before_value, "source": "상류 덧씌움 섞임(K control_reference)" if before_value is not None else "—"},
            "expected": None,
            "after": {"value": None, "label": after_label, "source": None},
            "in_band": {"before": inside_band(before_value, band), "after": None},
            "judge": ledger.relative_text(judge_path),
        })
    return metrics, ledger.relative_text(judge_path)


def read_sweep_scene(ledger, sweep_path, scene_name):
    sweep = ledger.load_json(sweep_path)
    scene = find_scene(sweep, scene_name)
    if scene is None:
        fail_input(f"{ledger.relative_text(sweep_path)} 에 장면 {scene_name} 이 없다")
    canonical = {"tuning": sweep.get("canonical_tuning_sha256"), "look": sweep.get("canonical_look_sha256")}
    if not all(isinstance(value, str) for value in canonical.values()):
        fail_input(f"{ledger.relative_text(sweep_path)} 정본 sha 가 없다")
    return sweep, scene, canonical


def require_saved_image(ledger, path):
    if not is_inside(path, ledger.root / "Saved"):
        fail_policy(f"그림이 Saved 밖: {path.as_posix()}")
    ledger.record(path)


def image_references(scene):
    references = []
    for item in scene.get("live2_refs") or []:
        match = IMAGE_REFERENCE.match(str(item).strip())
        if match:
            references.append(f"IMG{int(match.group(1))}")
    return sorted(set(references), key=lambda text: int(text[3:]))


def resolve_after_photo(ledger, scene_name, adoption_ids, adoptions, plan, reshoot_plans):
    for adoption_id in adoption_ids:
        compare_text = chosen_compare_path(adoptions[adoption_id])
        if compare_text is None:
            continue
        verify_dir = ledger.resolve(compare_text).parent
        photos = schedule_photos(verify_dir / scene_name)
        if not photos:
            fail_input(f"W11 후 사진 없음: {ledger.relative_text(verify_dir / scene_name)}")
        _, _, canonical = read_sweep_scene(ledger, verify_dir / "sweep.json", scene_name)
        return {"kind": "w11", "source": ledger.relative_text(verify_dir), "photos": photos, "canonical": canonical, "reason": None, "attempts": []}
    if adoption_ids:
        fail_input(f"채택 {adoption_ids} 의 W11 compare.json 근거가 없다({scene_name})")
    if plan not in reshoot_plans:
        fail_input(f"채택 없는 장면 {plan}/{scene_name} 의 계획이 sheet_areas reshoot 에 없다")
    attempts = []
    for folder in (plan, f"{plan}-2"):
        run_dir = ledger.root / RESHOOT_DIR / folder
        if not run_dir.is_dir():
            attempts.append({"folder": ledger.relative_text(run_dir), "problems": ["재촬영 없음"]})
            continue
        ledger.record(run_dir)
        problems, _ = check_reshoot_run(ledger.root, run_dir)
        if problems:
            attempts.append({"folder": ledger.relative_text(run_dir), "problems": problems})
            continue
        photos = schedule_photos(run_dir / scene_name)
        if not photos:
            return {"kind": "none", "source": ledger.relative_text(run_dir), "photos": [], "canonical": None, "reason": "재촬영 폴더에 이 장면 사진 없음", "attempts": attempts}
        _, _, canonical = read_sweep_scene(ledger, run_dir / "sweep.json", scene_name)
        ledger.record(ledger.root / OVERLAY_EMPTY_PATH)
        return {"kind": "reshoot", "source": ledger.relative_text(run_dir), "photos": photos, "canonical": canonical, "reason": None, "attempts": attempts}
    return {"kind": "none", "source": None, "photos": [], "canonical": None, "reason": "후 없음(재촬영 통과 폴더 없음)", "attempts": attempts}


def build_scene(ledger, context, declared, area_has_adoption):
    plan, scene_name = declared["plan"], declared["scene"]
    if not (ledger.root / PLANS_DIR / f"{plan}.json").is_file():
        fail_input(f"계획 없음(폐기·retired 제외 규칙): {PLANS_DIR}/{plan}.json")
    ledger.record(ledger.root / PLANS_DIR / f"{plan}.json")
    sweep_path = ledger.root / FRAMES_DIR / plan / "sweep.json"
    _, sweep_scene, before_canonical = read_sweep_scene(ledger, sweep_path, scene_name)
    level = str(sweep_scene.get("level", ""))
    adoptions = context["adoptions"]
    expect_adoptions = context["expect"].get("adoptions") or {}
    derived = sorted(
        (key for key, entry in adoptions.items()
         if scene_name in ((expect_adoptions.get(key) or {}).get("scenes") or [])
         and level in ((expect_adoptions.get(key) or {}).get("levels") or [])
         and adoption_area(entry) == context["area"]),
        key=int,
    )
    if declared["declared_adoptions"] is not None and sorted(declared["declared_adoptions"]) != sorted(derived):
        fail_input(f"{plan}/{scene_name} sheet_areas adoptions {declared['declared_adoptions']} ≠ verdict·expect 도출 {derived}")
    before_photos = schedule_photos(ledger.root / FRAMES_DIR / plan / scene_name)
    if not before_photos:
        fail_input(f"0e 전 사진 없음: {FRAMES_DIR}/{plan}/{scene_name}")
    after = resolve_after_photo(ledger, scene_name, derived, adoptions, plan, context["reshoot"])
    before_ms, before_name, before_path = middle_photo(before_photos)
    require_saved_image(ledger, before_path)
    after_record = {"kind": after["kind"], "source": after["source"], "path": None, "name": None, "schedule_ms": None, "canonical": after["canonical"], "reason": after["reason"], "reshoot_attempts": after["attempts"]}
    if after["photos"]:
        after_ms, after_name, after_path = middle_photo(after["photos"])
        if after_name != before_name:
            fail_policy(f"짝 시각 불일치: {plan}/{scene_name} 전 {before_name} ≠ 후 {after_name}")
        require_saved_image(ledger, after_path)
        after_record.update({"path": ledger.relative_text(after_path), "name": after_name, "schedule_ms": after_ms})
    if derived:
        metrics = adopted_metrics(ledger, context["expect"], adoptions, derived, scene_name)
        k_judge = None
    else:
        after_label = "정본 불변(채택 0)" if not area_has_adoption else "—(이 장면 채택 0)"
        metrics, k_judge = unadopted_metrics(ledger, plan, scene_name, after_label)
    w_before = context["canonical_before_w"]
    return {
        "plan": plan,
        "scene": scene_name,
        "level": level,
        "map": level.rsplit("/", 1)[-1],
        "adoptions": derived,
        "verdict_labels": [{"adoption": key, "labels": [str(label) for label in adoptions[key].get("labels") or []]} for key in derived],
        "image_references": image_references(sweep_scene),
        "before": {
            "path": ledger.relative_text(before_path),
            "name": before_name,
            "schedule_ms": before_ms,
            "canonical": before_canonical,
            "differs_from_w_before": before_canonical != w_before,
        },
        "after": after_record,
        "k_judge": k_judge,
        "metrics": metrics,
    }


def sort_scene_key(scene):
    if scene["adoptions"]:
        return (0, int(scene["adoptions"][0]), scene["plan"], scene["scene"])
    return (1, 0, scene["plan"], scene["scene"])


def required_adopted_scenes(context):
    expect_adoptions = context["expect"].get("adoptions") or {}
    required = set()
    for key, entry in context["adoptions"].items():
        if adoption_area(entry) != context["area"]:
            continue
        for scene_name in (expect_adoptions.get(key) or {}).get("scenes") or []:
            required.add((key, scene_name))
    return required


def listed_item_text(item):
    return str(item.get("item", "")), str(item.get("destination", ""))


def perf_candidate_text(item):
    assignments = item.get("assignments") if isinstance(item.get("assignments"), list) else []
    changes = "; ".join(
        f"{str(entry.get('json_path', '')).rsplit('.', 1)[-1]} {entry.get('base')}→{entry.get('value')}"
        for entry in assignments if isinstance(entry, dict)
    )
    gain = item.get("gain")
    destination = f"gain {gain:.2f}" if isinstance(gain, (int, float)) and not isinstance(gain, bool) else "—"
    return f"{item.get('variant', '')} {changes}".strip(), destination


def read_listed_items(ledger, path, list_key, area, keep_item, describe=listed_item_text):
    document = ledger.load_json(path)
    items = document.get(list_key) if isinstance(document, dict) else None
    if not isinstance(items, list):
        fail_input(f"{ledger.relative_text(path)} 에 {list_key} 목록이 없다")
    selected = []
    for index, item in enumerate(items):
        if not isinstance(item, dict) or not keep_item(item):
            continue
        plan = plan_id_from_path(item.get("plan", ""))
        item_area = area_of_plan(plan)
        if item_area is None:
            fail_input(f"{ledger.relative_text(path)} {list_key}[{index}] 계획 {plan!r} 의 영역을 정할 수 없다")
        if item_area == area:
            text, destination = describe(item)
            selected.append({"index": index, "plan": plan, "item": text, "destination": destination})
    return selected


def read_perf_entry(ledger, perf_path, area):
    perf = ledger.load_json(perf_path)
    areas = perf.get("areas") if isinstance(perf, dict) else None
    entry = None
    if isinstance(areas, dict):
        entry = areas.get(area)
    elif isinstance(areas, list):
        matches = [item for item in areas if isinstance(item, dict) and item.get("area") == area]
        entry = matches[0] if len(matches) == 1 else None
    if not isinstance(entry, dict) or not isinstance(entry.get("overall"), str) or not entry["overall"]:
        fail_input(f"--perf 에 영역 {area} 의 overall(종합) 이 없다")
    counts = read_perf_counts(entry, area)
    table_sha = read_perf_table_sha(entry, area)
    overall = PROPOSED_PASS_LABEL if entry["overall"].startswith("충족") else entry["overall"]
    return {"overall": overall, "overall_source": entry["overall"], "counts": counts, "table_sha256": table_sha}


def read_perf_counts(entry, area):
    counts = entry.get("counts")
    if not isinstance(counts, dict):
        fail_input(f"--perf 영역 {area} counts(판정별 수) 객체가 없다")
    if set(counts) != set(PERF_COUNT_KEYS):
        fail_input(f"--perf 영역 {area} counts 키 {sorted(counts)} ≠ {list(PERF_COUNT_KEYS)}")
    for key in PERF_COUNT_KEYS:
        value = counts[key]
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            fail_input(f"--perf 영역 {area} counts[{key}] = {value!r} 는 0 이상 정수여야 한다")
    for key, number in OVERALL_COUNT.findall(entry["overall"]):
        if int(number) != counts[key]:
            fail_input(f"--perf 영역 {area} overall(종합) 의 '{key} {number}' ≠ counts[{key}] {counts[key]}")
    return {key: counts[key] for key in PERF_COUNT_KEYS}


def read_perf_table_sha(entry, area):
    table_sha = entry.get("table_sha256")
    if table_sha is None and area in PERF_TABLELESS_AREAS:
        return None
    if not isinstance(table_sha, str) or not SHA256_TEXT.match(table_sha):
        fail_input(f"--perf 영역 {area} table_sha256(표 sha) 은 64자 소문자 16진이어야 한다(null 은 {', '.join(PERF_TABLELESS_AREAS)} 만): {table_sha!r}")
    return table_sha


def perf_line(perf, candidates):
    counts = "·".join(f"{key} {perf['counts'][key]}" for key in PERF_COUNT_KEYS)
    table = perf["table_sha256"][:8] if perf["table_sha256"] else "—"
    listed = ", ".join(f"{candidate['item']} · {candidate['destination']}" for candidate in candidates)
    candidate_text = f" · 성능 후보 {len(candidates)}(perf-candidates: {listed})" if candidates else ""
    return f"성능: {perf['overall']} · {counts} · {PERF_LABEL} · 표 {table}{candidate_text}"


def build_model(ledger, area, areas_path, perf_path, out_root):
    verdict = ledger.load_json(ledger.root / VERDICT_PATH)
    expect = ledger.load_json(ledger.root / EXPECT_PATH)
    if not isinstance(verdict, dict) or not isinstance(expect, dict):
        fail_input("verdict-w11.json·expect.json 최상위가 객체가 아님")
    canonical_after = expect.get("canonical_after") or {}
    if canonical_after.get("tuning") != CANONICAL_TUNING_SHA256 or canonical_after.get("look") != CANONICAL_LOOK_SHA256:
        fail_input("expect.json canonical_after ≠ P0 ①(9ea03397·22a5987c)")
    canonical_before_w = expect.get("canonical_before") or {}
    retained, design_assert = read_user_items(verdict)
    declared_scenes, reshoot_plans = read_area_definition(ledger, areas_path, area)
    perf = read_perf_entry(ledger, perf_path, area)
    approval_only = read_listed_items(ledger, ledger.root / APPROVAL_ONLY_PATH, "items", area, lambda item: True)
    structural = read_listed_items(ledger, ledger.root / STRUCTURAL_GAPS_PATH, "gaps", area, lambda item: str(item.get("destination", "")).startswith("승인 시트"))
    candidates = read_listed_items(ledger, ledger.root / PERF_CANDIDATES_PATH, "items", area, lambda item: True, perf_candidate_text)
    wave3 = []
    for name in WAVE3_PAIRS.get(area, ()):
        before_path, after_path = ledger.root / WAVE3_BEFORE_DIR / name, ledger.root / WAVE3_AFTER_DIR / name
        ledger.record(before_path)
        ledger.record(after_path)
        wave3.append({"before": ledger.relative_text(before_path), "after": ledger.relative_text(after_path)})
    pending = []
    for text in PENDING_PATHS.get(area, ()):
        ledger.record(ledger.root / text)
        pending.append({"name": text, "path": text, "picture": True})
    for text in PENDING_WITHOUT_PICTURES.get(area, ()):
        pending.append({"name": text, "path": None, "picture": False})
    if not FONT_PATH.is_file():
        fail_input(f"글꼴 없음: {FONT_PATH.as_posix()}")
    adoptions = numbered_adoptions(verdict)
    context = {
        "area": area,
        "adoptions": adoptions,
        "expect": expect,
        "reshoot": reshoot_plans,
        "canonical_before_w": {"tuning": canonical_before_w.get("tuning"), "look": canonical_before_w.get("look")},
    }
    required = required_adopted_scenes(context)
    area_has_adoption = bool(required)
    for declared in declared_scenes:
        if area_of_plan(declared["plan"]) != area:
            fail_input(f"장면 {declared['plan']}/{declared['scene']} 의 계획 영역 {area_of_plan(declared['plan'])} ≠ {area}")
    if not area_has_adoption and len(declared_scenes) > 4:
        fail_input(f"채택 없는 영역 {area} 장면 {len(declared_scenes)}개 > 4(6.2 ②)")
    scenes = sorted((build_scene(ledger, context, declared, area_has_adoption) for declared in declared_scenes), key=sort_scene_key)
    covered = {(key, scene["scene"]) for scene in scenes for key in scene["adoptions"]}
    missing = sorted(required - covered)
    if missing:
        fail_input(f"영역 {area} W11 채택 판정 장면 누락(6.2 ①): {missing}")
    fingerprint = {
        "area": area,
        "out_root": ledger.relative_text(out_root),
        "tool_sha256": sha256_file(TOOL_PATH),
        "font": {"path": FONT_PATH.as_posix(), "sha256": sha256_file(FONT_PATH)},
        "pil_version": PIL.__version__,
        "inputs": ledger.sorted_inputs(),
    }
    return {
        "area": area,
        "title": AREA_TITLES[area],
        "out_root": ledger.relative_text(out_root),
        "canonical_after": {"tuning": CANONICAL_TUNING_SHA256, "look": CANONICAL_LOOK_SHA256},
        "canonical_before_w": context["canonical_before_w"],
        "scenes": scenes,
        "labels": list(AREA_LABELS[area]),
        "retained_fifty": [number for number in retained if number in RETAINED_FIFTY.get(area, ())],
        "design_assert": design_assert if area == DESIGN_ASSERT_AREA else None,
        "approval_only": approval_only,
        "structural_gaps": structural,
        "perf_candidates": candidates,
        "wave3_pairs": wave3,
        "pending": pending,
        "perf": perf,
        "perf_line": perf_line(perf, candidates),
        "fingerprint": fingerprint,
    }


def band_summary(scene):
    summary = {}
    for side in ("before", "after"):
        decided = [metric["in_band"][side] for metric in scene["metrics"] if metric["in_band"][side] is not None]
        summary[side] = f"{sum(1 for value in decided if value)}/{len(decided)}" if decided else "—"
    return summary


def after_description(scene):
    after = scene["after"]
    if after["kind"] == "w11":
        return f"후=W11 {Path(after['source']).name}"
    if after["kind"] == "reshoot":
        return f"후=재촬영 {Path(after['source']).name}"
    return after["reason"] or "후 없음"


def revision_dir_text(model, revision):
    return f"{model['out_root']}/{model['area']}/r{revision}"


def now_line(model):
    return f"승인 대기: FluidFX {model['area']} ({model['out_root']}/{model['area']}/current.json)"


def build_index_lines(model, revision, page_count):
    after = model["canonical_after"]
    before_w = model["canonical_before_w"]
    lines = [
        f"# 승인 시트 — {model['title']}({model['area']}) · {STATUS_LABEL}",
        f"- 판: {revision_dir_text(model, revision)} · sheet {page_count}쪽 · sheet.json(입력 경로·sha)",
        f"- 정본: 조절 {after['tuning'][:8]} · 모양 {after['look'][:8]} (W 직전 {str(before_w.get('tuning'))[:8]} · {str(before_w.get('look'))[:8]})",
    ]
    for scene in model["scenes"]:
        summary = band_summary(scene)
        if scene["adoptions"]:
            verdicts = " / ".join(f"#{entry['adoption']} {'·'.join(entry['labels']) or '라벨 없음'}" for entry in scene["verdict_labels"])
        else:
            verdicts = "채택 0"
        mixed = f" · {MIXED_BEFORE_LABEL}" if scene["before"]["differs_from_w_before"] else ""
        lines.append(f"- 장면 {scene['scene']}({scene['plan']}, {scene['map']}) · {verdicts} · 띠 안 전 {summary['before']}·후 {summary['after']} · {after_description(scene)}{mixed}")
    if len(model["scenes"]) < 2:
        lines.append("- 대표 1개(0e 짝 없음)")
    for label in model["labels"]:
        lines.append(f"- 표기: {label}")
    if model["retained_fifty"]:
        lines.append(f"- 표기: {retained_fifty_text(model)} (기대 재등록은 사용자 직접 진술, K6a)")
    if model["design_assert"]:
        lines.append(f"- 표기: {DESIGN_ASSERT_LABEL} — {shorten(model['design_assert'])}")
    for item in model["approval_only"]:
        prefix = "뒤로 미룸: " if "(deferred)" in item["destination"] else ""
        lines.append(f"- {prefix}approval-only #{item['index']} {item['plan']}: {shorten(item['item'], 70)} → {shorten(item['destination'], 50)}")
    for item in model["structural_gaps"]:
        lines.append(f"- structural-gaps #{item['index']} {item['plan']}: {shorten(item['item'], 70)} → {shorten(item['destination'], 50)}")
    for pair in model["wave3_pairs"]:
        lines.append(f"- 파동 3 육안: 전 {pair['before']} ↔ 후 {pair['after']}")
    for item in model["pending"]:
        lines.append(f"- 합칠 대기: {item['path']}" if item["picture"] else f"- 합칠 대기: {item['name']} = 그림 없음(촬영 계획 없음)")
    lines.append(f"- {model['perf_line']}")
    lines.append(f"- NOW ②: {now_line(model)}")
    return lines


def retained_fifty_text(model):
    numbers = " ".join(f"#{number}" for number in model["retained_fifty"])
    return f"유지(재현 근거 없음) ㊿ {numbers}"


def required_index_tokens(model):
    tokens = [STATUS_LABEL, PERF_LABEL, now_line(model)]
    tokens.extend(f"장면 {scene['scene']}(" for scene in model["scenes"])
    tokens.extend(f"- 표기: {label}\n" for label in model["labels"])
    if model["retained_fifty"]:
        tokens.append(f"- 표기: {retained_fifty_text(model)} (")
    if model["design_assert"]:
        tokens.append(f"- 표기: {DESIGN_ASSERT_LABEL} — ")
    tokens.extend(f"approval-only #{item['index']} " for item in model["approval_only"])
    tokens.extend(f"structural-gaps #{item['index']} " for item in model["structural_gaps"])
    tokens.extend(pair["before"] for pair in model["wave3_pairs"])
    tokens.extend(item["path"] if item["picture"] else item["name"] for item in model["pending"])
    return tokens


def validate_index(model, lines):
    if len(lines) > INDEX_LINE_LIMIT:
        fail_input(f"index.md {len(lines)}줄 > {INDEX_LINE_LIMIT}줄(6.5)")
    text = "\n".join(lines) + "\n"
    missing = [token for token in required_index_tokens(model) if token not in text]
    if missing:
        fail_input(f"index.md 필수 항목 누락: {missing}")
    forbidden = [phrase for phrase in FORBIDDEN_PHRASES if phrase in text]
    if forbidden:
        fail_input(f"7절 금지 표기: {forbidden}")


def load_fonts():
    return {
        "title": ImageFont.truetype(str(FONT_PATH), 22),
        "header": ImageFont.truetype(str(FONT_PATH), 15),
        "body": ImageFont.truetype(str(FONT_PATH), 13),
    }


def fit_text(font, text, width):
    if font.getlength(text) <= width:
        return text
    trimmed = text
    while trimmed and font.getlength(trimmed + "…") > width:
        trimmed = trimmed[:-1]
    return trimmed + "…"


def fitting_prefix_length(font, text, width):
    length = 1
    while length < len(text) and font.getlength(text[:length + 1]) <= width:
        length += 1
    return length


def wrap_text(font, text, width):
    lines = []
    current = ""
    for word in re.findall(r"\s*\S+", text):
        if current and font.getlength(current + word) > width:
            lines.append(current)
            current = ""
            word = word.lstrip()
        current += word
        while len(current) > 1 and font.getlength(current) > width:
            length = fitting_prefix_length(font, current, width)
            lines.append(current[:length])
            current = current[length:].lstrip()
    if current:
        lines.append(current)
    return lines


def draw_cell_header(draw, fonts, origin, width, text):
    x, y = origin
    draw.rectangle((x, y, x + width - 1, y + CELL_HEADER_HEIGHT - 1), fill=HEADER_COLOR)
    draw.text((x + 6, y + 4), fit_text(fonts["body"], text, width - 12), font=fonts["body"], fill=TEXT_COLOR)


def paste_photo(sheet, root, relative_path, origin):
    try:
        with Image.open(root / relative_path) as source:
            tile = source.convert("RGB").resize((CELL_WIDTH, CELL_HEIGHT), Image.LANCZOS)
    except PHOTO_READ_ERRORS as error:
        fail_input(f"사진 해석 실패: {relative_path} ({photo_error_text(error)})")
    sheet.paste(tile, origin)


def canonical_text(canonical):
    if not canonical:
        return "정본 —"
    return f"정본 {str(canonical.get('tuning'))[:8]}/{str(canonical.get('look'))[:8]}"


def draw_photo_cells(sheet, draw, fonts, root, scene, x, y):
    before = scene["before"]
    paste_photo(sheet, root, before["path"], (x, y))
    mixed = f" · {MIXED_BEFORE_LABEL}" if before["differs_from_w_before"] else ""
    draw_cell_header(draw, fonts, (x, y), CELL_WIDTH, f"전 0e · {canonical_text(before['canonical'])} · {before['schedule_ms']}ms{mixed}")
    after = scene["after"]
    after_x = x + CELL_WIDTH
    if after["path"]:
        paste_photo(sheet, root, after["path"], (after_x, y))
        draw_cell_header(draw, fonts, (after_x, y), CELL_WIDTH, f"{after_description(scene)} · {canonical_text(after['canonical'])} · {after['schedule_ms']}ms")
        return
    draw.rectangle((after_x, y, after_x + CELL_WIDTH - 1, y + CELL_HEIGHT - 1), fill=PANEL_COLOR)
    draw_cell_header(draw, fonts, (after_x, y), CELL_WIDTH, "후 없음")
    draw.text((after_x + 12, y + CELL_HEADER_HEIGHT + 12), fit_text(fonts["header"], after["reason"] or "후 없음", CELL_WIDTH - 24), font=fonts["header"], fill=WARNING_COLOR)


def metric_text_lines(metric):
    band = metric["band"]
    if band:
        band_text = f"띠 {format_number(band['low'])}–{format_number(band['high'])} ({band['band_source']}, {band['basis_label']})"
    else:
        band_text = "띠 — (판정 파일에 목표 없음)"
    after = metric["after"]
    after_text = after["label"] if after["label"] else format_number(after["value"])
    marks = {True: "O", False: "X", None: "—"}
    values = f"전 {format_number(metric['before']['value'])}({metric['before']['source']}) · 기대 {format_number(metric['expected'])} · 후 {after_text} · 띠 안 전 {marks[metric['in_band']['before']]} 후 {marks[metric['in_band']['after']]}"
    prefix = f"#{metric['adoption']} " if metric["adoption"] else ""
    return [prefix + metric["metric"], band_text, values]


def draw_metric_cell(draw, fonts, scene, x, y):
    draw.rectangle((x, y, x + CELL_WIDTH - 1, y + CELL_HEIGHT - 1), fill=PANEL_COLOR)
    draw_cell_header(draw, fonts, (x, y), CELL_WIDTH, "지표 | 띠(basis) | 전(출처) | 기대 | 후(W11) | 띠 안")
    cursor = y + CELL_HEADER_HEIGHT + 6
    bottom = y + CELL_HEIGHT - METRIC_LINE_HEIGHT
    if not scene["metrics"]:
        draw.text((x + 8, cursor), "지표 없음(채택·K 판정 없음)", font=fonts["body"], fill=MUTED_COLOR)
        return
    width = CELL_WIDTH - 16
    for index, metric in enumerate(scene["metrics"]):
        name, band_text, values = metric_text_lines(metric)
        lines = [fit_text(fonts["body"], name, width), *wrap_text(fonts["body"], band_text, width), fit_text(fonts["body"], values, width)]
        if cursor + METRIC_LINE_HEIGHT * (len(lines) + 1) > bottom:
            draw.text((x + 8, cursor), f"… 외 {len(scene['metrics']) - index}개(sheet.json)", font=fonts["body"], fill=WARNING_COLOR)
            return
        for line_index, line in enumerate(lines):
            color = TEXT_COLOR if line_index == 0 else MUTED_COLOR
            draw.text((x + 8, cursor), line, font=fonts["body"], fill=color)
            cursor += METRIC_LINE_HEIGHT
        cursor += 5


def draw_perf_cell(draw, fonts, model, x, y):
    draw.rectangle((x, y, x + PERF_WIDTH - 1, y + CELL_HEIGHT - 1), fill=PANEL_COLOR)
    draw_cell_header(draw, fonts, (x, y), PERF_WIDTH, "성능 한 줄")
    cursor = y + CELL_HEADER_HEIGHT + 6
    for line in wrap_text(fonts["body"], model["perf_line"], PERF_WIDTH - 16):
        if cursor > y + CELL_HEIGHT - METRIC_LINE_HEIGHT:
            return
        draw.text((x + 8, cursor), line, font=fonts["body"], fill=TEXT_COLOR)
        cursor += METRIC_LINE_HEIGHT


def scene_row_title(scene):
    adoptions = " ".join(f"#{key}" for key in scene["adoptions"]) or "채택 0"
    references = " ".join(scene["image_references"]) or "IMG —"
    return f"{scene['scene']} ({scene['plan']}) · {scene['map']} · {adoptions} · LIVE-2 {references}(번호만)"


def render_page(model, root, fonts, page_scenes, revision, page_number, page_count):
    height = PAGE_TITLE_HEIGHT + len(page_scenes) * (ROW_TITLE_HEIGHT + CELL_HEIGHT + ROW_GAP)
    sheet = Image.new("RGB", (PAGE_WIDTH, height), BACKGROUND_COLOR)
    draw = ImageDraw.Draw(sheet)
    after = model["canonical_after"]
    draw.text((12, 6), f"승인 시트 — {model['title']}({model['area']}) · {STATUS_LABEL} · r{revision} · {page_number}/{page_count}쪽", font=fonts["title"], fill=TEXT_COLOR)
    draw.text((12, 38), f"정본 조절 {after['tuning'][:8]} · 모양 {after['look'][:8]} · 열: 전(0e) | 후(W11 verify, 없으면 P2 재촬영) | 지표 표 | 성능 · 갤러리 원본 없음", font=fonts["body"], fill=MUTED_COLOR)
    y = PAGE_TITLE_HEIGHT
    for scene in page_scenes:
        draw.text((12, y + 6), fit_text(fonts["header"], scene_row_title(scene), PAGE_WIDTH - 24), font=fonts["header"], fill=TEXT_COLOR)
        cell_y = y + ROW_TITLE_HEIGHT
        draw_photo_cells(sheet, draw, fonts, root, scene, 0, cell_y)
        draw_metric_cell(draw, fonts, scene, CELL_WIDTH * 2, cell_y)
        draw_perf_cell(draw, fonts, model, CELL_WIDTH * 3, cell_y)
        y = cell_y + CELL_HEIGHT + ROW_GAP
    buffer = io.BytesIO()
    sheet.save(buffer, format="PNG", compress_level=6)
    return buffer.getvalue()


def page_name(page_number):
    return "sheet.png" if page_number == 1 else f"sheet-{page_number}.png"


def render_revision(model, root, revision):
    pages = [model["scenes"][start:start + ROWS_PER_PAGE] for start in range(0, len(model["scenes"]), ROWS_PER_PAGE)]
    lines = build_index_lines(model, revision, len(pages))
    validate_index(model, lines)
    fonts = load_fonts()
    outputs = {}
    for number, page_scenes in enumerate(pages, start=1):
        outputs[page_name(number)] = render_page(model, root, fonts, page_scenes, revision, number, len(pages))
    outputs["index.md"] = ("\n".join(lines) + "\n").encode("utf-8")
    sheet_document = {
        "tool": "make_approval_sheet",
        "version": 1,
        "area": model["area"],
        "title": model["title"],
        "revision": revision,
        "revision_dir": revision_dir_text(model, revision),
        "status": STATUS_LABEL,
        "fingerprint": model["fingerprint"],
        "canonical_after": model["canonical_after"],
        "canonical_before_w": model["canonical_before_w"],
        "scenes": [{**scene, "band_summary": band_summary(scene)} for scene in model["scenes"]],
        "labels": model["labels"],
        "retained_fifty": model["retained_fifty"],
        "design_assert": model["design_assert"],
        "approval_only": model["approval_only"],
        "structural_gaps": model["structural_gaps"],
        "perf_candidates": model["perf_candidates"],
        "wave3_pairs": model["wave3_pairs"],
        "pending": model["pending"],
        "perf": model["perf"],
        "perf_line": model["perf_line"],
        "now_line": now_line(model),
        "index_line_count": len(lines),
        "outputs": [{"name": name, "sha256": sha256_bytes(data)} for name, data in sorted(outputs.items())],
    }
    outputs["sheet.json"] = json_bytes(sheet_document)
    return outputs


def latest_revision(area_dir):
    if not area_dir.is_dir():
        return None
    numbers = [int(match.group(1)) for item in area_dir.iterdir() if item.is_dir() for match in [REVISION_NAME.match(item.name)] if match]
    return max(numbers) if numbers else None


def read_revision_sheet(area_dir, revision):
    sheet_path = area_dir / f"r{revision}" / "sheet.json"
    if not sheet_path.is_file():
        fail_input(f"판 r{revision} 에 sheet.json 이 없다")
    data = sheet_path.read_bytes()
    try:
        return json.loads(data.decode("utf-8")), sha256_bytes(data)
    except (UnicodeDecodeError, ValueError) as error:
        fail_input(f"판 r{revision} sheet.json 해석 실패: {error}")


def current_document(model, revision, sheet_sha):
    directory = revision_dir_text(model, revision)
    return {"area": model["area"], "revision": revision, "revision_dir": directory, "sheet_json": f"{directory}/sheet.json", "sheet_json_sha256": sheet_sha}


def write_current(area_dir, model, revision, sheet_sha):
    data = json_bytes(current_document(model, revision, sheet_sha))
    current_path = area_dir / "current.json"
    if current_path.is_file() and current_path.read_bytes() == data:
        return False
    write_bytes_atomically(current_path, data)
    return True


def write_revision(area_dir, revision, outputs):
    target = area_dir / f"r{revision}"
    if target.exists():
        fail_input(f"판 폴더가 이미 있다: r{revision}")
    temporary = Path(tempfile.mkdtemp(prefix=f".r{revision}.tmp-{os.getpid()}-", dir=area_dir))
    for name, data in sorted(outputs.items()):
        with (temporary / name).open("wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    os.replace(temporary, target)


def check_latest_revision(area_dir, model, root):
    revision = latest_revision(area_dir)
    if revision is None:
        fail_input("판이 없다(--check 대상 없음)")
    _, sheet_sha = read_revision_sheet(area_dir, revision)
    current_path = area_dir / "current.json"
    if not current_path.is_file() or current_path.read_bytes() != json_bytes(current_document(model, revision, sheet_sha)):
        fail_input(f"current.json 이 최신 판 r{revision}·sheet.json sha 와 맞지 않는다")
    outputs = render_revision(model, root, revision)
    revision_dir = area_dir / f"r{revision}"
    existing = sorted(item.name for item in revision_dir.iterdir() if item.is_file())
    if existing != sorted(outputs):
        fail_input(f"r{revision} 파일 목록 {existing} ≠ 재생성 {sorted(outputs)}")
    differing = [name for name in sorted(outputs) if (revision_dir / name).read_bytes() != outputs[name]]
    if differing:
        fail_input(f"r{revision} 재생성 바이트 불일치: {differing}")
    print(f"--check 일치: {revision_dir_text(model, revision)} ({len(outputs)}개 파일)")
    return EXIT_OK


def produce_sheet(area_dir, model, root):
    revision = latest_revision(area_dir)
    if revision is not None:
        latest_sheet, sheet_sha = read_revision_sheet(area_dir, revision)
        if latest_sheet.get("fingerprint") == model["fingerprint"]:
            rewritten = write_current(area_dir, model, revision, sheet_sha)
            print(f"새 판 없음(입력 sha 같음): {revision_dir_text(model, revision)}" + (" · current.json 갱신" if rewritten else ""))
            return EXIT_OK
    next_revision = (revision or 0) + 1
    outputs = render_revision(model, root, next_revision)
    area_dir.mkdir(parents=True, exist_ok=True)
    write_revision(area_dir, next_revision, outputs)
    write_current(area_dir, model, next_revision, sha256_bytes(outputs["sheet.json"]))
    pages = sum(1 for name in outputs if name.endswith(".png"))
    print(f"새 판: {revision_dir_text(model, next_revision)} (장면 {len(model['scenes'])}, 쪽 {pages})")
    return EXIT_OK


def run_sheet(arguments, root):
    if arguments.area not in AREAS:
        fail_input(f"모르는 영역 {arguments.area!r}(허용 {', '.join(AREAS)})")
    if not arguments.areas or not arguments.perf:
        fail_input("--area 에는 --areas·--perf 가 필요하다")
    ledger = InputLedger(root)
    out_root = ledger.resolve(arguments.out_root)
    if not is_inside(out_root, root / "Saved"):
        fail_policy(f"출력이 Saved 밖: {out_root.as_posix()}")
    model = build_model(ledger, arguments.area, ledger.resolve(arguments.areas), ledger.resolve(arguments.perf), out_root)
    area_dir = out_root / arguments.area
    if arguments.check:
        return check_latest_revision(area_dir, model, root)
    return produce_sheet(area_dir, model, root)


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="FluidFX 영역 승인 시트(runbook-p-v3 T4·6절·7절)")
    parser.add_argument("--area")
    parser.add_argument("--areas")
    parser.add_argument("--perf")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--reshoot-check", dest="reshoot_check")
    parser.add_argument("--root", default=str(DEFAULT_ROOT))
    parser.add_argument("--out-root", dest="out_root", default=DEFAULT_OUT_ROOT)
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    if PIL is None:
        print("PIL(Pillow) 없음: 대안 없이 멈춘다", file=sys.stderr)
        return EXIT_INPUT
    root = Path(arguments.root).resolve()
    if arguments.reshoot_check and arguments.area:
        print("--reshoot-check 와 --area 는 함께 쓰지 않는다", file=sys.stderr)
        return EXIT_INPUT
    if arguments.reshoot_check:
        return run_reshoot_check(root, arguments.reshoot_check)
    if not arguments.area:
        print("--area 또는 --reshoot-check 가 필요하다", file=sys.stderr)
        return EXIT_INPUT
    try:
        return run_sheet(arguments, root)
    except SheetError as error:
        print(f"종료 {error.code}: {error}", file=sys.stderr)
        return error.code


if __name__ == "__main__":
    sys.exit(main())

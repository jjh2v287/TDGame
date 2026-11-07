"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 실제 r13_static.json 을 사본에 두고 인용 줄을 합성한 뒤 fxp_audit.py r13 을 돌려 정상(5줄·표기 셋·IN1 옮기기·한도 구성)·같은 바이트·인용 변이(글자·줄 번호·LIVE-2 열)·종료 1(인용 없음·값·장면·장면 경계 S9≠S90·항목·무대 사유·P 행·행 연결·D-49)·종료 2 둘을 본다.
상태: 2026-11-07 FX-P X4(B2), fix1 빌더 B가 장면 경계 사례 추가(변이 r13-scene-prefix-match)·줄끝 LF.
"""
import copy
import json
import shutil
from pathlib import Path

from check_fxp_fixtures import TOOL_DIRECTORY, FixtureLog, run_audit, write_json, write_text

P_SUMMARY = "Saved/AgentOps/20261106/p/p-summary.json"
STATIC = "Tools/FluidFX/fxp/r13_static.json"
PF = "Saved/AgentOps/20261019/fluidfx/parity2/performance.md"
EXPECTED_SUMMARY = "SUMMARY rows=5 quoted=2 waiting=3 quotes=7 quotes_ok=7 items=9 problems=0"
EXPECTED_LABELS = ["P 판정 인용(IN1/ft 통과 · IN1/gt 판정 불가(걸침))", "무대 없음(R13s 대기)", "무대 없음(R13s 대기)",
                   "구조 격차(기존 측정 인용·보정 전, FX-7p S9·S8)", "무대 없음(R13s 대기)"]


def real_static():
    return json.loads((TOOL_DIRECTORY / "r13_static.json").read_text(encoding="utf-8"))


def summary_document():
    lines = [{"id": "S038", "limit": "≤0.1", "disposition": "행", "reason": "몫", "rows": ["IN1/gt"]},
             {"id": "S039", "limit": "≤0.2·≤0.3", "disposition": "측정 불가", "reason": "무대 없음", "rows": []},
             {"id": "S041", "limit": "≤2.0·≤1.0", "disposition": "측정 불가", "reason": "무대 없음", "rows": []},
             {"id": "S043", "limit": "≤0.5", "disposition": "행", "reason": None, "rows": ["IN1/ft"]},
             {"id": "S045", "limit": "-", "disposition": "측정 불가", "reason": "무대·실측 없음", "rows": []},
             {"id": "S053", "limit": "≤4.0·≤2.0·≤0.3", "disposition": "측정 불가", "reason": "무대 없음", "rows": []},
             {"id": "S072", "limit": "≤0.3", "disposition": "행", "reason": "몫", "rows": ["IN4/gt", "IN1/gt"]}]
    rows = [{"row": "IN1/ft", "metric": "ft", "limit_ms": 0.5, "items": ["S043"], "d": {"median": 0.1665}, "interval": {"lower": 0.12645, "upper": 0.214, "state": "통과"},
             "p95_record": {"median": 0.123}, "label": "통과"},
            {"row": "IN1/gt", "metric": "gt", "limit_ms": 0.4, "items": ["S038", "S072"], "d": {"median": 0.4955},
             "interval": {"lower": -0.06255, "upper": 0.96095, "state": "걸침"}, "p95_record": None, "label": "판정 불가(걸침)"}]
    return {"areas": {"input": {"lines": lines, "rows": rows}, "fire": {"lines": [], "rows": []}}}


def quoted_files(static):
    files = {}
    for quote in [quote for row in static["rows"] for quote in row.get("quotes") or []] + list(static["live2"].get("quotes") or []):
        if isinstance(quote, dict) and isinstance(quote.get("file"), str) and isinstance(quote.get("line"), int):
            lines = files.setdefault(quote["file"], {})
            if quote["expect"] not in lines.get(quote["line"], ""):
                lines[quote["line"]] = " · ".join(part for part in (lines.get(quote["line"]), quote["expect"]) if part)
    return files


def make_world(root, static, summary, line_overrides=None):
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    if static is not None:
        write_json(root, STATIC, static)
    if summary is not None:
        write_json(root, P_SUMMARY, summary)
    files = quoted_files(real_static())
    for (relative, line), text in (line_overrides or {}).items():
        files.setdefault(relative, {})[line] = text
    for relative, lines in files.items():
        write_text(root, relative, "\n".join(lines.get(number, f"채움 줄 {number}") for number in range(1, max(lines) + 1)) + "\n")
    return root


def check_normal(log, root):
    first = run_audit(root, "r13", "r13-1")
    second = run_audit(root, "r13", "r13-2")
    summary_line = first.markdown.strip().splitlines()[-1] if first.markdown else ""
    log.check("정상 종료 0·SUMMARY(5줄·인용 2·대기 3)", first.code == 0 and summary_line == EXPECTED_SUMMARY,
              f"종료 {first.code} {summary_line} {(first.document or {}).get('problems')} {first.stderr[-300:]}")
    log.check("같은 바이트(json·md)", first.json_bytes is not None and first.json_bytes == second.json_bytes and first.md_bytes == second.md_bytes, "바이트 다름")
    rows = (first.document or {}).get("rows", [])
    log.check("표기 셋(4.3) 그대로", [row["label"] for row in rows] == EXPECTED_LABELS, str([row["label"] for row in rows]))
    log.check("LIVE-2 열 고정 '스트레스 정량 비공개(PF:206)'", all(row["live2"] == "스트레스 정량 비공개(PF:206)" for row in rows) and len(rows) == 5, "LIVE-2 열 다름")
    particles = rows[0] if rows else {}
    gt = next((record for record in particles.get("p_rows", []) if record["row"] == "IN1/gt"), {})
    log.check("입자 1만 IN1/gt 옮기기만(d·구간·한도·한도 구성 S038+S072)",
              gt.get("d_median") == 0.4955 and gt.get("interval_lower") == -0.06255 and gt.get("limit_ms") == 0.4
              and gt.get("limit_items") == [{"id": "S038", "limit": "≤0.1"}, {"id": "S072", "limit": "≤0.3"}], json.dumps(gt, ensure_ascii=False))
    actors = rows[3] if len(rows) > 3 else {}
    log.check("액터 49 값은 인용 줄에서만(7.515·≤4.0·2.4845)", [value["value"] for value in actors.get("values", [])] == ["7.515", "≤4.0", "2.4845"]
              and all(value["ok"] for value in actors.get("values", [])), str(actors.get("values")))


def check_problem(log, root, case, needle, mutate_static=None, mutate_summary=None, line_overrides=None):
    static, summary = copy.deepcopy(real_static()), summary_document()
    if mutate_static:
        mutate_static(static)
    if mutate_summary:
        mutate_summary(summary)
    slug = f"case-{len(log.lines)}"
    result = run_audit(make_world(root / slug, static, summary, line_overrides), "r13", slug)
    problems = (result.document or {}).get("problems", [])
    log.check(f"종료 1 {case}", result.code == 1 and any(needle in problem for problem in problems), f"종료 {result.code} {problems} {result.stderr[-200:]}")


def particles_quote(static):
    return static["rows"][0]["quotes"][0]


def scene_s90_texts(static):
    quotes = static["rows"][3]["quotes"]
    return [quotes[0]["expect"].replace("S9 ", "S90 "), quotes[1]["expect"].replace("·S9", "·S90")]


def use_scene_s90(static):
    for quote, text in zip(static["rows"][3]["quotes"], scene_s90_texts(real_static())):
        quote["expect"] = text


def check_variants(log, root):
    static = real_static()
    quote = particles_quote(static)
    check_problem(log, root, "인용 변이(줄 글자 바뀜)", "인용 불일치: 입자 1만", line_overrides={(quote["file"], quote["line"]): quote["expect"].replace("≤0.1ms", "≤0.2ms")})
    check_problem(log, root, "인용 변이(줄 번호 어긋남)", "인용 불일치: 입자 1만", mutate_static=lambda s: particles_quote(s).update(line=particles_quote(s)["line"] + 1))
    live2 = static["live2"]["quotes"][0]
    check_problem(log, root, "인용 변이(LIVE-2 열)", "인용 불일치: LIVE-2 열", line_overrides={(live2["file"], live2["line"]): "스트레스 수치 공개됨"})
    check_problem(log, root, "인용 없음", "인용 없음: 프리미티브 200", mutate_static=lambda s: s["rows"][1].update(quotes=[]))
    check_problem(log, root, "인용 칸 빠짐", "인용 없음: 페인터 64 quotes[0]", mutate_static=lambda s: s["rows"][4]["quotes"][0].pop("expect"))
    check_problem(log, root, "인용에 없는 값", "인용에 없는 값: 액터 49(16) values[0]", mutate_static=lambda s: s["rows"][3]["values"][0].update(value="7.6"))
    check_problem(log, root, "인용에 없는 장면", "인용에 없는 장면: 액터 49(16) S10", mutate_static=lambda s: s["rows"][3].update(scenes=["S9", "S10"]))
    actor_quotes = static["rows"][3]["quotes"]
    check_problem(log, root, "장면 경계(인용에 S90만 있으면 S9 없음)", "인용에 없는 장면: 액터 49(16) S9", mutate_static=use_scene_s90,
                  line_overrides={(quote["file"], quote["line"]): text for quote, text in zip(actor_quotes, scene_s90_texts(static))})
    check_problem(log, root, "항목 없음", "근거 부족: 액터 49(16) 항목 S053", mutate_summary=lambda s: s["areas"]["input"].update(lines=[line for line in s["areas"]["input"]["lines"] if line["id"] != "S053"]))
    check_problem(log, root, "무대 사유 아님", "무대 없음 근거 아님: 프리미티브 200 S039",
                  mutate_summary=lambda s: s["areas"]["input"]["lines"][1].update(reason="장면 간 비"))
    check_problem(log, root, "P 행 없음", "근거 부족: 입자 1만 P 행 IN1/gt",
                  mutate_summary=lambda s: s["areas"]["input"].update(rows=[row for row in s["areas"]["input"]["rows"] if row["row"] != "IN1/gt"]))
    check_problem(log, root, "P 행이 항목과 안 이어짐", "IN1/gt가 항목",
                  mutate_summary=lambda s: (s["areas"]["input"]["lines"][0].update(rows=[]), s["areas"]["input"]["lines"][6].update(rows=["IN4/gt"])))
    check_problem(log, root, "D-49 LIVE-2 수치 꼴", "LIVE-2 수치 꼴(D-49)",
                  mutate_static=lambda s: s["live2"]["quotes"][0].update(expect="LIVE-2 스트레스 3 ms"),
                  line_overrides={(PF, 206): "LIVE-2 스트레스 3 ms"})
    slug = f"case-{len(log.lines)}"
    missing = run_audit(make_world(root / slug, None, summary_document()), "r13", slug)
    log.check("종료 2 r13_static 없음", missing.code == 2 and missing.json_bytes is None, f"종료 {missing.code}")
    broken = copy.deepcopy(real_static())
    broken["rows"][2]["kind"] = "new_stage"
    slug = f"case-{len(log.lines)}"
    bad_kind = run_audit(make_world(root / slug, broken, summary_document()), "r13", slug)
    log.check("종료 2 모르는 kind", bad_kind.code == 2 and bad_kind.json_bytes is None, f"종료 {bad_kind.code}")


def run(tmp_root):
    log = FixtureLog("r13")
    root = Path(tmp_root)
    check_normal(log, make_world(root / "normal", real_static(), summary_document()))
    check_variants(log, root)
    return log.result()

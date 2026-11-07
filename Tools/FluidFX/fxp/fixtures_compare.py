"""실행 환경: 시스템 Python 3.12(표준 라이브러리만), check_fxp_fixtures.py 가 import 해서 run(임시 루트)을 부른다.
실행 명령: python -B Tools/FluidFX/fxp/check_fxp_fixtures.py(단독 실행 없음).
출력: (통과, 실패, 줄). 합성 사본에서 fxp_audit.py compare 를 돌려 정상(값 옮기기·한도 원천·PF 줄 이어 읽기·P-4 미답·표기·FO3 파생)·같은 바이트·종료 1 여섯(원천 항목 없음·source 없음·items 비어 있음·한도 없음·sha 불일치·D-49)·종료 2 둘을 본다.
상태: 2026-11-07 FX-P X1b(B2).
"""
import copy
import hashlib
import json
import shutil
from pathlib import Path

from check_fxp_fixtures import FixtureLog, run_audit, write_json

P_SUMMARY = "Saved/AgentOps/20261106/p/p-summary.json"
P_AREAS = "Tools/FluidFX/perf/p/p_areas.json"
NOTATION = "PF 환산 ×2.3~3·상한 비교(증명 아님)·제안 예산 대비·상한 꺼짐(P-22)"
EXPECTED_SUMMARY = ("SUMMARY rows=4 areas_with_rows=2 pass=2 fail=1 undetermined=1 other=0 pf_reference_rows=2 no_pf_rows=2 p_areas_sha_matches=True problems=0")


def p_areas_document():
    return {"items": [{"id": "S001", "source": "PF:159 GPU"}, {"id": "S002", "source": "PF:158 GT·RM:479"}, {"id": "S003", "source": "RM:463"},
                      {"id": "S004", "source": "PF:158·160 메모리"}, {"id": "S005", "source": "F11:56·103·CCT:6"}, {"id": "S006", "source": ""}]}


def summary_document():
    fire_rows = [{"row": "FI1/ft", "scene": "FI1", "metric": "ft", "limit_ms": 4.65, "items": ["S001", "S002"], "d": {"values": [1.3, 1.4], "median": 1.3877},
                  "interval": {"lower": 1.35805, "upper": 1.41345, "rank": 1, "state": "통과"},
                  "d_prime": {"scale": 0.7, "median": 1.0641, "interval": {"lower": 1.041391, "upper": 1.083874, "rank": 1, "state": "통과"}},
                  "p95_record": {"values": [1.38], "median": 1.3889}, "verdict": "통과", "label": "통과", "rule": "judge.json"},
                 {"row": "FI2/gt", "scene": "FI2", "metric": "gt", "limit_ms": 1.0, "items": ["S003"], "d": {"median": 1.5204},
                  "interval": {"lower": 1.35125, "upper": 1.7933, "rank": 1, "state": "미달"}, "d_prime": None, "p95_record": None, "verdict": "미달", "label": "미달",
                  "rule": "judge.json"}]
    fog_rows = [{"row": "FO3/ft", "scene": None, "metric": "ft", "limit_ms": 4.0, "items": ["S004"], "from": "SM2/ft", "verdict": "통과", "label": "통과",
                 "rule": "FO3 SM2/ft 통과(B17)"},
                {"row": "FO1/gt", "scene": "FO1", "metric": "gt", "limit_ms": 0.2, "items": ["S005"], "d": {"median": -0.0243},
                 "interval": {"lower": -0.61335, "upper": 0.89675, "rank": 2, "state": "걸침"}, "d_prime": None, "p95_record": None, "verdict": "판정 불가",
                 "label": "판정 불가(걸침)", "rule": "judge.json"}]
    return {"inputs": {"p_areas": {"path": P_AREAS, "sha256": None}}, "areas_order": ["fire", "fog", "foliage"],
            "areas": {"fire": {"rows": fire_rows}, "fog": {"rows": fog_rows}, "foliage": {"rows": []}}}


def make_world(root, p_areas, summary, sha_override=None):
    shutil.rmtree(root, ignore_errors=True)
    root.mkdir(parents=True)
    if p_areas is not None:
        path = write_json(root, P_AREAS, p_areas)
        summary = copy.deepcopy(summary)
        if summary is not None:
            summary["inputs"]["p_areas"]["sha256"] = sha_override or hashlib.sha256(path.read_bytes()).hexdigest()
    if summary is not None:
        write_json(root, P_SUMMARY, summary)
    return root


def row_by_name(document, name):
    return next((row for row in document["rows"] if row["row"] == name), None) if document else None


def check_normal(log, root):
    first = run_audit(root, "compare", "compare-1")
    second = run_audit(root, "compare", "compare-2")
    summary_line = first.markdown.strip().splitlines()[-1] if first.markdown else ""
    log.check("정상 종료 0·SUMMARY", first.code == 0 and summary_line == EXPECTED_SUMMARY, f"종료 {first.code} {summary_line} {first.stderr[-300:]}")
    log.check("같은 바이트(json·md)", first.json_bytes == second.json_bytes and first.md_bytes == second.md_bytes and first.json_bytes is not None, "바이트 다름")
    fi1 = row_by_name(first.document, "FI1/ft") or {}
    copied = (fi1.get("d") == {"median": 1.3877, "interval": {"lower": 1.35805, "upper": 1.41345, "state": "통과"}}
              and fi1.get("d_prime") == {"median": 1.0641, "interval": {"lower": 1.041391, "upper": 1.083874, "state": "통과"}}
              and fi1.get("p95_record_median") == 1.3889 and fi1.get("limit_ms") == 4.65 and fi1.get("p_label") == "통과")
    log.check("값 옮기기만(d·구간·d′·p95·한도·P 판정)", copied, json.dumps(fi1, ensure_ascii=False)[:400])
    log.check("한도 원천 = p_areas source(items 순서)",
              fi1.get("limit_sources") == [{"item": "S001", "source": "PF:159 GPU"}, {"item": "S002", "source": "PF:158 GT·RM:479"}], str(fi1.get("limit_sources")))
    log.check("LIVE-2 공개·환산 = PF 줄 참조만", fi1.get("live2_public") == "PF:158 · PF:159 'LIVE-2 기준' 열 · PF:75 환산 배율", str(fi1.get("live2_public")))
    fo3, fo1, fi2 = (row_by_name(first.document, name) or {} for name in ("FO3/ft", "FO1/gt", "FI2/gt"))
    log.check("PF 이어 읽기(PF:158·160) · 남의 접두 이어 읽기(F11:56·103 → PF 아님)",
              fo3.get("live2_public_pf_lines") == [158, 160] and fo1.get("live2_public_pf_lines") == [] and fo1.get("live2_public") == "PF 줄 없음(한도 원천이 PF 밖)"
              and fi2.get("live2_public_pf_lines") == [], f"{fo3.get('live2_public_pf_lines')} {fo1.get('live2_public_pf_lines')}")
    fixed = all(row.get("live2_same_machine") == "P-4 미답" and row.get("notation") == NOTATION for row in (first.document or {}).get("rows", []))
    log.check("LIVE-2 같은 기계 'P-4 미답'·표기 고정(모든 행)", fixed, "고정 칸 다름")
    log.check("파생 FO3 = d 없음·P 규칙 표시", fo3.get("d") == {"median": None, "interval": None} and fo3.get("derived_from") == "SM2/ft"
              and "통과(FO3 SM2/ft 통과(B17))" in first.markdown, json.dumps(fo3, ensure_ascii=False)[:300])
    log.check("LIVE-2 대비 판정 문구 없음('동일 이상'·'LIVE-2 이하')", "동일 이상" not in first.markdown and "LIVE-2 이하" not in first.markdown, "판정 문구 있음")
    header = "| 영역 | 행 | 지표 | d 중앙(구간) | d′ | p95 기록 | 한도 | 한도 원천(S###:줄) | 제안 예산 대비(P 판정) | LIVE-2 같은 기계 | LIVE-2 공개·환산(PF 줄) | 표기 |"
    log.check("열 = 4.2 그대로(12열)", header in first.markdown, "머리줄 다름")


def check_problem(log, root, case, needle, mutate_p_areas=None, mutate_summary=None, sha_override=None):
    p_areas, summary = p_areas_document(), summary_document()
    if mutate_p_areas:
        mutate_p_areas(p_areas)
    if mutate_summary:
        mutate_summary(summary)
    world = make_world(root / case, p_areas, summary, sha_override)
    result = run_audit(world, "compare", case)
    problems = (result.document or {}).get("problems", [])
    log.check(f"종료 1 {case}", result.code == 1 and any(needle in problem for problem in problems), f"종료 {result.code} {problems} {result.stderr[-200:]}")


def check_variants(log, root):
    check_problem(log, root, "원천 항목 없음", "한도 원천 없음: fire FI2/gt S999", mutate_summary=lambda s: s["areas"]["fire"]["rows"][1].update(items=["S999"]))
    check_problem(log, root, "source 빈 문자열", "한도 원천 없음: fog FO1/gt S006", mutate_summary=lambda s: s["areas"]["fog"]["rows"][1].update(items=["S006"]))
    check_problem(log, root, "items 비어 있음", "한도 원천 없음: fog FO3/ft items 비어 있음", mutate_summary=lambda s: s["areas"]["fog"]["rows"][0].update(items=[]))
    check_problem(log, root, "한도 없음", "한도 없음: fire FI1/ft", mutate_summary=lambda s: s["areas"]["fire"]["rows"][0].update(limit_ms=None))
    check_problem(log, root, "sha 불일치", "한도 원천 입력 불일치", sha_override="0" * 64)
    check_problem(log, root, "D-49 LIVE-2 수치", "LIVE-2 수치 꼴(D-49)", mutate_p_areas=lambda p: p["items"][2].update(source="RM:463 LIVE-2 환산 4.6 ms"))
    missing_summary = run_audit(make_world(root / "요약 없음", p_areas_document(), None), "compare", "missing-summary")
    log.check("종료 2 P 요약 없음", missing_summary.code == 2 and missing_summary.json_bytes is None, f"종료 {missing_summary.code}")
    world = make_world(root / "p_areas 없음", p_areas_document(), summary_document())
    (world / P_AREAS).unlink()
    missing_areas = run_audit(world, "compare", "missing-p-areas")
    log.check("종료 2 p_areas 없음", missing_areas.code == 2 and missing_areas.json_bytes is None, f"종료 {missing_areas.code}")


def run(tmp_root):
    log = FixtureLog("compare")
    root = Path(tmp_root)
    check_normal(log, make_world(root / "normal", p_areas_document(), summary_document()))
    check_variants(log, root)
    return log.result()

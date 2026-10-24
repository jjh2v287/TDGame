"""IWBench 1단계 결과 JSON 을 마크다운 표로 요약한다(두 회차 중앙값 비교 포함).
실행 환경: Python 3.12 (표준 라이브러리만), Windows PowerShell 또는 Git Bash
실행: python Tools/InfiniteWorld/bench/summarize_static.py [--results <결과 폴더 기본 Saved/AgentOps/IWBench/results>] [--out <md 경로>]  (results/terrain-r1·r2, ism-r1·r2, terrain-render1 이 있으면 함께)
상태: 실험 재현용(2026-10-23 무한 세계 벤치 1단계)
"""
import argparse
import json
from pathlib import Path

RESULTS = Path(__file__).resolve().parents[3] / "Saved/AgentOps/IWBench" / "results"

TERRAIN_METRICS = [
    "a0_height_generation_ms",
    "a_dynamicmesh_build_ms",
    "a_parallel16_heights_plus_mesh_per_chunk_ms",
    "b_component_no_collision.setmesh_ms",
    "b_component_no_collision.register_ms",
    "b_component_no_collision.total_ms",
    "b_component_no_collision.eof_flush_ms",
    "b_component_no_collision.next_frame_ms",
    "b_component_no_collision.destroy_ms",
    "c_sync_complex_collision.gt_setmesh_ms",
    "c_sync_complex_collision.gt_total_ms",
    "c_sync_complex_collision.trace_ready_frames",
    "c_sync_complex_collision.next_frame_ms",
    "c_sync_complex_collision.destroy_ms",
    "d_async_complex_collision.gt_total_ms",
    "d_async_complex_collision.trace_ready_wall_ms",
    "d_async_complex_collision.trace_ready_frames",
    "d_async_complex_collision.next_frame_ms",
    "c2_edit_recook_sync.gt_editmesh_ms",
    "d2_edit_recook_async.gt_editmesh_ms",
    "d2_edit_recook_async.trace_updated_wall_ms",
    "d2_edit_recook_async.trace_updated_frames",
    "d3_async_burst_8_chunks.gt_spawn_all_ms",
    "d3_async_burst_8_chunks.all_ready_wall_ms",
    "d3_async_burst_8_chunks.all_ready_frames",
    "e_chaos_heightfield_ctor_ms",
]

ISM_COLUMNS = [
    ("ism.total_ms", "생성+추가+등록"),
    ("ism.add_instances_unregistered_ms", "추가(등록 전)"),
    ("ism.register_ms", "등록"),
    ("ism.eof_flush_ms", "프레임 끝 갱신 강제"),
    ("ism.next_frame_ms", "다음 프레임"),
    ("ism.remove_10pct_ms", "10% 제거"),
    ("ism.add_one_registered_ms", "1개 추가(등록 후)"),
    ("ism.remove_one_registered_ms", "1개 제거(등록 후)"),
    ("ism.tree_after_add_wall_ms", "트리 완료(추가 후)"),
    ("ism.tree_after_remove_wall_ms", "트리 완료(제거 후)"),
    ("ism.destroy_ms", "파괴"),
]


def load(name):
    path = RESULTS / f"{name}.json"
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def index_samples(data):
    table = {}
    if not data:
        return table
    for record in data["results"]:
        if "median" in record:
            table[(record["name"], record["config"])] = record
    return table


def records_named(data, name):
    if not data:
        return []
    return [r for r in data["results"] if r.get("name") == name]


def fmt(value, digits=3):
    if value is None:
        return "-"
    if abs(value) >= 100:
        return f"{value:.1f}"
    if abs(value) >= 10:
        return f"{value:.2f}"
    return f"{value:.{digits}f}"


def diff_percent(a, b):
    if a is None or b is None or a == 0:
        return "-"
    return f"{(b - a) / a * 100:+.0f}%"


def terrain_configs(data):
    seen = []
    for record in data["results"]:
        config = record.get("config")
        if config and config not in seen:
            seen.append(config)
    return seen


def terrain_section(first, second, render, render_second=None, brief=False):
    lines = ["### terrain 스위트: 회차 1(r1)·회차 2(r2) 중앙값/p95/최대 (단위는 지표 이름 끝: _ms 밀리초, _frames 프레임)", ""]
    a = index_samples(first)
    b = index_samples(second)
    c = index_samples(render)
    d = index_samples(render_second)
    header = "| 지표 | 구성 | r1 중앙값 | r1 p95 | r1 최대 | r2 중앙값 | r2 p95 | r2 최대 | 중앙값 차 |"
    if render:
        header += " 렌더 켬 중앙값 (1회/2회) |"
    lines.append(header)
    lines.append("|" + "---|" * (header.count("|") - 1))
    for metric in TERRAIN_METRICS:
        for config in terrain_configs(first):
            ra = a.get((metric, config))
            rb = b.get((metric, config))
            if not ra and not rb:
                continue
            row = [metric, config]
            for record in (ra, rb):
                row += [fmt(record and record["median"]), fmt(record and record["p95"]), fmt(record and record["max"])]
            row.append(diff_percent(ra and ra["median"], rb and rb["median"]))
            if render:
                rc = c.get((metric, config))
                rd = d.get((metric, config))
                row.append(f"{fmt(rc and rc['median'])} / {fmt(rd and rd['median'])}")
            lines.append("| " + " | ".join(row) + " |")
            if brief and config in ("64m@1m_65x65", "64m@0.5m_129x129"):
                print(" | ".join(row))
    timeouts = []
    for data, tag in ((first, "r1"), (second, "r2"), (render, "render1")):
        for (name, config), record in index_samples(data).items():
            if name.endswith("timeout_count") and record["max"] > 0:
                timeouts.append(f"{tag} {name} [{config}] 최대 {record['max']}")
    lines.append("")
    lines.append("시간 초과: " + ("; ".join(timeouts) if timeouts else "없음(모든 회차·구성 timeout_count 최대 0)"))
    lines.append("")
    lines.append("### 정밀도(f): 수직 라인 트레이스 200개, 해석 높이(삼각형 보간)와 적중 높이의 절대 오차 (cm)")
    lines.append("")
    lines.append("| 구성 | 배치 | 오프셋(cm) | r1 적중 | r1 최대 오차 | r1 평균 오차 | r2 최대 오차 |")
    lines.append("|---|---|---|---|---|---|---|")
    second_precision = {(r["config"], r["layout"], r["offset_cm"]): r for r in records_named(second, "f_precision_trace")}
    for record in records_named(first, "f_precision_trace"):
        other = second_precision.get((record["config"], record["layout"], record["offset_cm"]))
        layout = "액터 변환에 오프셋" if record["layout"] == "offset_in_actor_transform" else "정점에 오프셋 굽기"
        lines.append(f"| {record['config']} | {layout} | {record['offset_cm']:.0e} | {record['hits']}/{record['rays']} | {record['max_abs_error_cm']:.5f} | {record['mean_abs_error_cm']:.5f} | {other['max_abs_error_cm']:.5f} |" if other else
                     f"| {record['config']} | {layout} | {record['offset_cm']:.0e} | {record['hits']}/{record['rays']} | {record['max_abs_error_cm']:.5f} | {record['mean_abs_error_cm']:.5f} | - |")
    lines.append("")
    lines.append("### 높이장(e) 레이캐스트 확인 (FHeightField::Raycast 직접 호출, 200개)")
    lines.append("")
    lines.append("| 구성 | 적중 | 최대 오차 cm (행=Y) | 최대 오차 cm (행=X) | 단위 축척 적중 | 단위 축척 최대 오차 cm (행=Y) |")
    lines.append("|---|---|---|---|---|---|")
    for record in records_named(first, "e_chaos_heightfield_raycast_check"):
        lines.append(f"| {record['config']} | {record['hits']}/200 | {record['max_error_cm_rows_are_y']:.4f} | {record['max_error_cm_rows_are_x']:.1f} | {record.get('unit_scale_hits', '-')}/200 | {record.get('unit_scale_max_error_cm_rows_are_y', 0):.4f} |")
    lines.append("")
    lines.append("### 메시 크기")
    lines.append("")
    for record in records_named(first, "mesh_info"):
        lines.append(f"- {record['config']}: 정점 {record['vertices']}, 삼각형 {record['triangles']}")
    return lines


def ism_section(first, second, render, render_second=None, brief=False):
    lines = ["### ism 스위트: 중앙값 ms (r1 / r2), 생성+추가+등록은 r1 p95·최대도 표기", ""]
    a = index_samples(first)
    b = index_samples(second)
    c = index_samples(render)
    configs = []
    for record in first["results"]:
        if record.get("config") and record["config"] not in configs:
            configs.append(record["config"])
    header = "| 구성 | " + " | ".join(label for _, label in ISM_COLUMNS) + " | 생성+추가+등록 r1 p95/최대 |"
    if render:
        header += " 렌더 켬 생성+추가+등록 / 프레임 끝 갱신 / 다음 프레임 중앙값 (1회; 2회) |"
    lines.append(header)
    lines.append("|" + "---|" * (header.count("|") - 1))
    for config in configs:
        row = [config]
        for metric, _ in ISM_COLUMNS:
            ra = a.get((metric, config))
            rb = b.get((metric, config))
            if not ra and not rb:
                row.append("-")
                continue
            row.append(f"{fmt(ra and ra['median'])} / {fmt(rb and rb['median'])}")
        total = a.get(("ism.total_ms", config))
        row.append(f"{fmt(total and total['p95'])} / {fmt(total and total['max'])}")
        if render:
            cells = []
            for source in (c, index_samples(render_second)):
                values = [source.get((m, config)) for m in ("ism.total_ms", "ism.eof_flush_ms", "ism.next_frame_ms")]
                cells.append(" / ".join(fmt(v and v["median"]) for v in values))
            row.append("; ".join(cells))
        lines.append("| " + " | ".join(row) + " |")
        if brief:
            print(" | ".join(row))
    frames = []
    for config in configs:
        record = a.get(("ism.tree_after_add_frames", config))
        if record:
            frames.append(f"{config} 트리(추가 후) 프레임 중앙값 {fmt(record['median'], 1)}")
    if frames:
        lines.append("")
        lines.append("HISM 트리 완료 프레임: " + "; ".join(frames))
    return lines


def meta_section(datasets):
    lines = ["### 실행 메타", "", "| 결과 | 상태 | 총 초 | 총 프레임 | 평균 프레임 ms | 렌더 가능 | 작업 스레드 | DDC 생략 임계 |", "|---|---|---|---|---|---|---|---|"]
    for name, data in datasets:
        if not data:
            continue
        meta = data["meta"]
        lines.append(f"| {name} | {data['status']} | {meta['total_seconds']:.1f} | {int(meta['total_frames'])} | {meta['average_frame_ms']:.3f} | {meta['can_ever_render']} | {meta['task_graph_worker_threads']} | {meta['skip_ddc_threshold']} |")
    return lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out")
    parser.add_argument("--results")
    parser.add_argument("--brief", action="store_true")
    args = parser.parse_args()
    global RESULTS
    if args.results:
        RESULTS = Path(args.results)
    terrain_first = load("terrain-r1")
    terrain_second = load("terrain-r2")
    terrain_render = load("terrain-render1")
    terrain_render_second = load("terrain-render2")
    ism_render_second = load("ism-render2")
    ism_first = load("ism-r1")
    ism_second = load("ism-r2")
    ism_render = load("ism-render1")
    lines = []
    lines += meta_section([("terrain-r1", terrain_first), ("terrain-r2", terrain_second), ("terrain-render1", terrain_render), ("terrain-render2", terrain_render_second),
                           ("ism-r1", ism_first), ("ism-r2", ism_second), ("ism-render1", ism_render), ("ism-render2", ism_render_second)])
    lines.append("")
    lines += terrain_section(terrain_first, terrain_second, terrain_render, terrain_render_second, args.brief)
    lines.append("")
    lines += ism_section(ism_first, ism_second, ism_render, ism_render_second, args.brief)
    text = "\n".join(lines) + "\n"
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    if args.brief:
        for line in lines:
            if line.startswith("시간 초과") or line.startswith("HISM") or line.startswith("| terrain") or line.startswith("| ism") or "1e+09" in line:
                print(line)
    else:
        print(text)


if __name__ == "__main__":
    main()

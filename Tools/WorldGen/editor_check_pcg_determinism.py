"""P3-07 PCG 결정론 검사: TDGen_PCG_BiomeForest 볼륨을 같은 시드로 cleanup → generate 2회 반복하고
각 회의 ISM 인스턴스 수와 위치 해시(소수 1자리 반올림·정렬·sha1)를 비교한다.

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_check_pcg_determinism.py
생성이 비동기라 스크립트는 즉시 반환하고, 에디터 틱 콜백이 단계를 진행한다.
결과: Saved/WorldGen/pcg_determinism.json (runs[], identical, 진행 상태). 완료 시 더티 패키지 저장.
빠른 집계만 원하면: python Tools/run_in_editor.py -c "import sys; sys.path.insert(0,'Tools/WorldGen'); import editor_check_pcg_determinism as m; print(m.snapshot())"
"""
import hashlib
import json
import os
import time

import unreal

VOLUME_LABEL = "TDGen_PCG_BiomeForest"
RUNS = 2
STABLE_SECONDS = 10.0
POLL_SECONDS = 3.0
TIMEOUT_SECONDS = 1200.0
RESULT_PATH = os.path.join(unreal.Paths.project_saved_dir(), "WorldGen", "pcg_determinism.json")

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def find_volume():
    for actor in eas.get_all_level_actors():
        if actor.get_actor_label() == VOLUME_LABEL:
            return actor
    return None


def pcg_output_actors(volume):
    actors = [volume]
    for actor in eas.get_all_level_actors():
        if isinstance(actor, unreal.PCGPartitionActor):
            actors.append(actor)
    return actors


def snapshot(volume=None):
    volume = volume or find_volume()
    if volume is None:
        return {"error": "volume not found"}
    rows = []
    per_mesh = {}
    partition_actor_count = 0
    for actor in pcg_output_actors(volume):
        if isinstance(actor, unreal.PCGPartitionActor):
            partition_actor_count += 1
        for component in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            mesh_name = mesh.get_name() if mesh else "None"
            count = component.get_instance_count()
            per_mesh[mesh_name] = per_mesh.get(mesh_name, 0) + count
            for index in range(count):
                transform = component.get_instance_transform(index, True)
                location = transform.translation
                rows.append(f"{mesh_name}|{location.x:.1f}|{location.y:.1f}|{location.z:.1f}")
    rows.sort()
    digest = hashlib.sha1("\n".join(rows).encode("utf-8")).hexdigest()
    return {"instances": len(rows), "hash": digest, "per_mesh": per_mesh, "partition_actors": partition_actor_count}


def write_result(state):
    os.makedirs(os.path.dirname(RESULT_PATH), exist_ok=True)
    payload = {k: v for k, v in state.items() if k not in ("volume", "component", "handle", "on_generated")}
    with open(RESULT_PATH, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, ensure_ascii=False, default=str)


def log(state, msg):
    unreal.log(f"[TDPcgCheck] {msg}")
    state.setdefault("log", []).append(f"{time.strftime('%H:%M:%S')} {msg}")
    write_result(state)


def start_generation(state):
    component = state["component"]
    state["generated_flag"] = False
    state["last_count"] = -1
    state["stable_since"] = None
    state["phase_started"] = time.time()
    state["phase"] = "generating"
    component.generate_local(True)
    log(state, f"run {state['run'] + 1}: generate started (seed={component.get_editor_property('seed')})")


def finish(state, identical):
    state["identical"] = identical
    state["phase"] = "done"
    state["finished_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    try:
        state["component"].on_pcg_graph_generated_external.remove_callable(state["on_generated"])
    except Exception as e:
        unreal.log_warning(f"[TDPcgCheck] delegate remove failed: {e}")
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(state, f"done identical={identical} saved={saved}")
    unreal.unregister_slate_post_tick_callback(state["handle"])


def tick(state, delta):
    now = time.time()
    if now - state.get("last_poll", 0.0) < POLL_SECONDS:
        return
    state["last_poll"] = now
    phase = state["phase"]
    if phase == "cleanup":
        if now - state["phase_started"] < 3.0:
            return
        start_generation(state)
        return
    if phase != "generating":
        return
    current = snapshot(state["volume"])
    count = current["instances"]
    elapsed = now - state["phase_started"]
    if count != state["last_count"]:
        state["last_count"] = count
        state["stable_since"] = now
        state["progress"] = {"run": state["run"] + 1, "instances": count, "elapsed": round(elapsed, 1)}
        write_result(state)
    stable_for = now - (state["stable_since"] or now)
    completed = state["generated_flag"] and stable_for >= POLL_SECONDS
    fallback = count > 0 and stable_for >= STABLE_SECONDS
    timed_out = elapsed > TIMEOUT_SECONDS
    if not (completed or fallback or timed_out):
        return
    current["elapsed_seconds"] = round(elapsed, 1)
    current["completion"] = "delegate" if completed else ("stable" if fallback else "timeout")
    state["runs"].append(current)
    log(state, f"run {state['run'] + 1}: instances={count} hash={current['hash']} via={current['completion']} in {elapsed:.0f}s")
    state["run"] += 1
    if state["run"] >= RUNS:
        hashes = {r["hash"] for r in state["runs"]}
        finish(state, len(hashes) == 1)
        return
    state["component"].cleanup(True)
    state["phase"] = "cleanup"
    state["phase_started"] = time.time()
    log(state, f"run {state['run'] + 1}: cleanup")


def main():
    volume = find_volume()
    if volume is None:
        raise RuntimeError(f"{VOLUME_LABEL} not found")
    component = volume.get_editor_property("pcg_component")
    state = {"volume": volume, "component": component, "run": 0, "runs": [], "phase": "cleanup",
             "phase_started": time.time(), "identical": None, "seed": component.get_editor_property("seed"),
             "graph": component.get_graph().get_path_name() if component.get_graph() else None,
             "started_at": time.strftime("%Y-%m-%d %H:%M:%S")}

    def on_generated(_component):
        state["generated_flag"] = True

    state["on_generated"] = on_generated
    component.on_pcg_graph_generated_external.add_callable(on_generated)
    component.cleanup(True)
    log(state, "run 1: cleanup")
    state["handle"] = unreal.register_slate_post_tick_callback(lambda delta: tick(state, delta))
    print(f"[TDPcgCheck] started; poll {RESULT_PATH}")


if __name__ == "__main__":
    main()

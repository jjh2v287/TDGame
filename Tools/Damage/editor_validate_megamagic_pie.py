"""언리얼 에디터 Python에서 실행: 실행 중인 전투 테스트 맵의 열 주문을 시전하고 피해·이펙트·정리를 검사한다.
실행: python Tools/run_in_editor.py Tools/Damage/editor_validate_megamagic_pie.py (PowerShell, PIE 시작 후)
출력: Saved/Damage/megamagic-pie.json 및 Saved/Screenshots/WindowsEditor/megamagic-*.png
상태: 현행
"""
import json
from pathlib import Path
import unreal


worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
if not worlds:
    raise RuntimeError("Start PIE in LV_TDMegaMagicArena first")
world = worlds[0]
player = unreal.GameplayStatics.get_player_pawn(world, 0)
if not isinstance(player, unreal.TDGameCharacter):
    raise RuntimeError("PIE must use TDGameCharacter")
targets = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageTarget))
if not targets:
    raise RuntimeError("The arena must contain damage targets")
spells = list(player.get_editor_property("damage_spells"))
if len(spells) != 10:
    raise RuntimeError(f"Expected 10 spells, got {len(spells)}")

output = Path(unreal.SystemLibrary.get_project_directory()) / "Saved/Damage/megamagic-pie.json"
output.parent.mkdir(parents=True, exist_ok=True)
report = {"map": world.get_path_name(), "state": "running", "spells": [], "passed": False}
durations = [7.0, 7.5, 3.5, 2.5, 5.0, 11.0, 6.5, 6.0, 5.5, 8.0]
state = {"slot": -1, "start": 0.0, "captured": False, "entry": None, "peak_entities": 0, "peak_fx": 0}
caster_combat = player.get_component_by_class(unreal.TDCombatComponent)
target_combat = [actor.get_component_by_class(unreal.TDCombatComponent) for actor in targets]
aim_actor = min(targets, key=lambda actor: (actor.get_actor_location() - unreal.Vector(400, 0, 65)).length())
aim = aim_actor.get_actor_location()
aim.z = 0.0


def save():
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


def begin_spell(now):
    state["slot"] += 1
    slot = state["slot"]
    if slot >= len(spells):
        report["state"] = "complete"
        report["passed"] = all(entry["passed"] for entry in report["spells"])
        save()
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log(f"MegaMagic PIE complete: passed={report['passed']}")
        return
    for combat in target_combat:
        stats = combat.get_stats()
        stats.set_editor_property("base_max_health", 10000.0)
        combat.set_stats(stats, True)
    stats = caster_combat.get_stats()
    caster_combat.set_stats(stats, True)
    before = [combat.get_current_health() for combat in target_combat]
    cast = player.cast_damage_spell(slot, aim)
    entry = {"slot": slot + 1, "asset": spells[slot].get_path_name(), "cast": cast,
             "health_before": before, "caster_before": caster_combat.get_current_health()}
    report["spells"].append(entry)
    state.update(start=now, captured=False, entry=entry, peak_entities=0, peak_fx=0)
    save()


def tick(delta):
    try:
        now = unreal.GameplayStatics.get_time_seconds(world)
        if state["slot"] < 0:
            begin_spell(now)
            return
        slot = state["slot"]
        if slot >= len(spells):
            return
        entities = list(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TDDamageEntity))
        active_fx = sum(1 for entity in entities for fx in entity.get_components_by_class(unreal.NiagaraComponent) if fx.is_active())
        state["peak_entities"] = max(state["peak_entities"], len(entities))
        state["peak_fx"] = max(state["peak_fx"], active_fx)
        elapsed = now - state["start"]
        if not state["captured"] and elapsed >= (1.25 if slot in (1, 4, 9) else 0.55):
            unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, f"megamagic-{slot + 1:02}.png")
            state["captured"] = True
        if elapsed < durations[slot]:
            return
        entry = state["entry"]
        after = [combat.get_current_health() for combat in target_combat]
        damage = sum(before - health for before, health in zip(entry["health_before"], after))
        entry.update(health_after=after, damage=damage, peak_entities=state["peak_entities"],
                     peak_active_fx=state["peak_fx"], remaining_entities=len(entities),
                     caster_after=caster_combat.get_current_health())
        entry["passed"] = bool(entry["cast"] and damage > 0 and state["peak_fx"] > 0
                               and not entities and entry["caster_before"] == entry["caster_after"])
        save()
        begin_spell(now)
    except Exception as error:
        report.update(state="failed", error=str(error))
        save()
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_error(str(error))


handle = unreal.register_slate_post_tick_callback(tick)
save()
print("MegaMagic PIE validation scheduled")

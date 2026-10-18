"""
실행 환경: Python 3.10 이상 표준 라이브러리만, 같은 폴더 sim_tactics.py를 import(언리얼·numpy 불필요).
실행 명령: PYTHONIOENCODING=utf-8 python Docs/MonsterAI_CombatSim/measurements/squad-tactics/sim_pack.py [--quick] [--only s1,s2,s3,s4,sensitivity,determinism]
출력: 같은 폴더 sim_pack_metrics.json, sim_pack_tables.md, 표준 출력에 진행 요약.
상태: 실험(scratch). 무리 행동 명세 v1(경보 연쇄·무리 사기·역할 독점·MD-43) 상수 검증용 결정론 2D 모형, 프로젝트 소스는 바꾸지 않음.
"""
import argparse
import hashlib
import json
import math
import os
import statistics
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import sim_tactics as st
from sim_tactics import (CAST, IDLE, MOVE, SPECIES, STEPS_PER_SECOND, V2_FIX_VARIANTS, Monster, SimulationV2, clamp,
                         logistic, percentile)

SPECIES["golem"] = {"speed": 230.0, "think_hz": 8, "min_hold": 0.6, "switch_ratio": 1.2, "range": 300.0,
                    "cooldown": 3.0, "cast": 1.0, "stop_at": 200.0, "hit_units": 600}

TACTICS_BASE = dict(V2_FIX_VARIANTS["F_final_B"])

PACK_DEFAULTS = {
    "engage_radius": 1500.0,
    "alert_radius": 900.0,
    "leash_radius": 3000.0,
    "lose_target_radius": 3500.0,
    "give_up_steps": 128,
    "max_chase_home": 4500.0,
    "home_arrival": 120.0,
    "return_stall_steps": 64,
    "arrival_settle_steps": 64,
    "alert_rule": "ripple",
    "hop_base": 8,
    "hop_span": 8,
    "max_hops": 2,
    "call_memory": 192,
    "call_radius": 1800.0,
    "morale_enabled": True,
    "witness_radius": 1200.0,
    "leader_radius": 1600.0,
    "loss_memory": 512,
    "min_losses": 2,
    "immune_weight": 2,
    "fear_pending": 64,
    "fear_min": 64,
    "fear_max": 128,
    "fear_speed_scale": 1.0,
    "rally_immune": 640,
    "retreat_progress": 30.0,
    "cornered_steps": 24,
    "elite_kinds": ("golem",),
    "flee_rule": "none",
    "flee_steps": 96,
    "flee_cooldown_steps": 384,
    "flee_speed_scale": 1.1,
    "flee_cornered": True,
    "flee_gate_enabled": True,
    "flee_gate_slope": -12.0,
    "flee_gate_center": 0.35,
    "flee_gate_range": 1200.0,
    "health_ratio": 1.0,
    "roles_enabled": False,
    "cap_kinds": {"hyena": "charger", "archer": "support"},
    "charger_cap": 1,
    "support_cap": 1,
    "band_floor_enabled": False,
    "band_gap": 150.0,
    "ranged_reach_margin": 100.0,
    "band_min_width": 100.0,
    "kill_interval": 0,
    "kill_count": 1,
    "kill_start": 256,
    "kill_order": "nearest",
    "wall_radius": 0.0,
    "wall_center": (0.0, 0.0),
    "loss_mode": "chain",
    "contagion_gate": "spec",
    "reapproach_distance": 400.0,
    "walk_stop": 900.0,
    "archer_sample_start": 384,
    "rules": "v1",
    "loss_window": 512,
    "loss_ring": 4,
    "loss_weight": 2,
    "panicking_weight": 2,
    "min_support": 0,
    "leader_shock_exclusive": False,
    "leader_fear_min": 64,
    "leader_fear_max": 128,
    "panic_hold_distance": 0.0,
    "death_alarm": False,
    "caller_same_kind": False,
    "retaliation_cap_bonus": False,
    "band_floor_v2": False,
    "band_gap_v2": 100.0,
    "band_floor_cap": 700.0,
    "flee_health_curve": "quadratic",
    "flee_gate_center_hyena": 0.3,
    "kill_max": None,
}

V2_PACK = {
    "rules": "v2",
    "loss_window": 192,
    "loss_weight": 2,
    "panicking_weight": 1,
    "min_support": 2,
    "leader_shock_exclusive": True,
    "leader_fear_min": 128,
    "leader_fear_max": 192,
    "fear_speed_scale": 1.2,
    "panic_hold_distance": 700.0,
    "death_alarm": True,
    "caller_same_kind": True,
    "retaliation_cap_bonus": True,
    "band_floor_v2": True,
    "flee_health_curve": "logistic",
    "flee_gate_center_hyena": 0.15,
}

FLEE_KINDS = {
    "goblin": {"weight": 3.0, "ally_radius": 600.0, "ally_range": 4.0, "gate_range": 1200.0, "speed": 1.1},
    "hyena": {"weight": 2.0, "ally_radius": 700.0, "ally_range": 3.0, "gate_range": 1500.0, "speed": 1.0},
}


def planar(ax, ay, bx, by):
    return math.hypot(ax - bx, ay - by)


def median_or_none(values):
    return round(statistics.median(values), 3) if values else None


class PackMonster(Monster):
    def __init__(self, sid, kind, x, y, goblin_cast_seconds, pack):
        super().__init__(sid, kind, x, y, goblin_cast_seconds)
        self.home_x = x
        self.home_y = y
        self.spawn_x = x
        self.spawn_y = y
        self.alive = True
        self.death_step = None
        self.first_engage_step = None
        self.returning = False
        self.reengage_block = 0
        self.out_of_reach = 0
        self.return_best = 0.0
        self.return_stall = 0
        self.alarm_role = "none"
        self.alarm_hop = 0
        self.alarm_call_step = 0
        self.is_immune = kind in pack["elite_kinds"]
        self.is_leader = self.is_immune
        self.cap_role = pack["cap_kinds"].get(kind) if pack["roles_enabled"] else None
        self.recent_loss = 0
        self.leader_shock = False
        self.last_loss_step = 0
        self.loss_steps = []
        self.has_rallied = False
        self.fear_steps = 0
        self.is_leader_fear = False
        self.retreat_hold = 0.0
        self.panic_streak = 0
        self.panic_streak_max = 0
        self.awaiting_approach = False
        self.fear_pending = False
        self.fear_pending_until = 0
        self.fear_start = 0
        self.rally_until = 0
        self.panic_depth = 0
        self.panic_starts = 0
        self.awaiting_reengage = False
        self.retreat_kind = None
        self.retreat_left = 0
        self.retreat_scale = 1.0
        self.retreat_best = -1.0
        self.retreat_stall = 0
        self.retreat_start = 0
        self.flee_cooldown_until = 0
        self.flee_streak = 0
        self.flee_streak_max = 0
        self.flee_starts = 0
        self.health_ratio = pack["health_ratio"] if kind in ("goblin", "hyena") else 1.0
        self.band_base = self.band
        self.home_steps = 0
        self.returning_steps = 0
        self.engaged_steps = 0
        self.panic_steps = 0
        self.retreat_steps = 0
        self.flee_steps = 0


class PackSimulation(SimulationV2):
    def __init__(self, config):
        self.p = dict(PACK_DEFAULTS)
        self.p.update(config.get("pack", {}))
        self.now = 0
        self.alarms = {}
        self.pending_deaths = []
        self.pending_death_alarms = []
        self.evaluate_next = False
        self.role_blocked = {"melee": 0, "ranged": 0}
        self.wake_log = []
        self.caller_log = []
        self.silent_count = 0
        self.kills = 0
        self.remote_kills = 0
        self.last_death_step = None
        self.empty_step = None
        self.panic_start_log = []
        self.pending_cancelled = 0
        self.retreat_log = []
        self.reengage_times = []
        self.reapproach_times = []
        self.panic_visible_steps = 0
        self.first_death_step = None
        self.archer_previous = {}
        self.archer_path = 0.0
        self.floor_changes = 0
        self.last_floor = None
        self.melee_holder_samples = [0, 0]
        self.return_log = []
        self.flee_starts = 0
        self.max_panicking = 0
        self.half_panic_steps = 0
        self.sum_engaged = 0
        self.sum_panic = 0
        self.sum_retreat = 0
        self.sum_flee = 0
        self.sum_returning = 0
        self.sum_home = 0
        self.holder_samples = {"charger": [0, 0], "support": [0, 0]}
        self.cast_samples = {"charger": 0, "support": 0}
        self.archer_distances = []
        self.floor_samples = []
        self.max_distance_seen = 0.0
        self.wall_contact_steps = 0
        super().__init__(config)
        self.all_monsters = list(self.monsters)
        self.monster_by_sid = {monster.sid: monster for monster in self.monsters}

    def spawn_monsters(self):
        positions = self.config.get("positions")
        low, high = self.config.get("spawn_band", (700.0, 1100.0))
        monsters = []
        for sid, kind in enumerate(self.config["composition"]):
            if positions is not None:
                x, y = positions[sid]
            else:
                angle = math.radians(st.wrap_degrees(sid * 137.50776))
                fraction = math.fmod(sid * 0.6180339887, 1.0)
                radius = low + (high - low) * fraction
                x, y = self.player_x + radius * math.cos(angle), self.player_y + radius * math.sin(angle)
            monsters.append(PackMonster(sid, kind, x, y, self.config.get("goblin_cast", 0.9), self.p))
        return monsters

    def player_position(self, step):
        if self.motion == "walk":
            return (min(600.0 * step / STEPS_PER_SECOND, self.p["walk_stop"]), 0.0)
        if self.motion != "chase":
            return super().player_position(step)
        if step == 0 or not self.monsters:
            return (self.player_x, self.player_y) if step > 0 else (0.0, 0.0)
        px, py = self.player_x, self.player_y
        nearest = min(self.monsters, key=lambda monster: (int((monster.x - px) ** 2 + (monster.y - py) ** 2), monster.sid))
        dx, dy = nearest.x - px, nearest.y - py
        distance = math.hypot(dx, dy)
        stop_distance = 130.0
        if distance <= stop_distance:
            return (px, py)
        move = min(600.0 / STEPS_PER_SECOND, distance - stop_distance)
        return (px + dx / distance * move, py + dy / distance * move)

    def tick_counters(self, monster):
        super().tick_counters(monster)
        if monster.reengage_block > 0:
            monster.reengage_block -= 1

    def home_to_player(self, monster):
        return planar(monster.home_x, monster.home_y, self.player_x, self.player_y)

    def can_discover(self, monster):
        p = self.p
        return (monster.reengage_block == 0 and self.distance_to_player(monster) <= p["engage_radius"]
                and self.home_to_player(monster) <= p["leash_radius"])

    def can_hear_alarm(self, listener, origin_x, origin_y):
        if listener.engaged or listener.returning or listener.reengage_block != 0:
            return False
        if planar(listener.x, listener.y, origin_x, origin_y) > self.p["alert_radius"]:
            return False
        return self.home_to_player(listener) <= self.p["leash_radius"]

    def engage(self, monster, step, hop):
        monster.engaged = True
        monster.engage_step = step
        monster.force_think = True
        monster.out_of_reach = 0
        monster.alarm_hop = hop
        if monster.first_engage_step is None:
            monster.first_engage_step = step
        self.wake_log.append((step, monster.sid, hop))

    def hop_delay_steps(self, distance):
        p = self.p
        return p["hop_base"] + (p["hop_span"] * int(clamp(distance, 0.0, p["alert_radius"]))) // int(p["alert_radius"])

    def reserve_hop(self, source, hop, step):
        self.reserve_from_point(source.x, source.y, hop, step)

    def reserve_from_point(self, origin_x, origin_y, hop, step):
        for listener in self.monsters:
            if not self.can_hear_alarm(listener, origin_x, origin_y):
                continue
            distance = planar(listener.x, listener.y, origin_x, origin_y)
            candidate = (step + self.hop_delay_steps(distance), hop)
            existing = self.alarms.get(listener.sid)
            if existing is None or candidate < existing:
                self.alarms[listener.sid] = candidate

    def alert_legacy(self, discoverers, step):
        for discoverer in discoverers:
            for listener in self.monsters:
                if not self.can_hear_alarm(listener, discoverer.x, discoverer.y):
                    continue
                self.engage(listener, step, 1)
                listener.alarm_role = "follower"

    def alert_ripple(self, discoverers, step):
        p = self.p
        for discoverer in discoverers:
            is_silent = any(other.alarm_call_step != 0 and step - other.alarm_call_step < p["call_memory"]
                            and (not p["caller_same_kind"] or other.kind == discoverer.kind)
                            and planar(other.x, other.y, discoverer.x, discoverer.y) <= p["call_radius"]
                            for other in self.monsters)
            if is_silent:
                discoverer.alarm_role = "silent"
                self.silent_count += 1
            else:
                discoverer.alarm_role = "caller"
                discoverer.alarm_call_step = step
                self.caller_log.append((step, discoverer.sid))
            self.reserve_hop(discoverer, 1, step)

    def process_due_alarms(self, step):
        due_entries = sorted((due, sid, hop) for sid, (due, hop) in self.alarms.items() if due <= step)
        for _, sid, hop in due_entries:
            del self.alarms[sid]
            listener = self.monster_by_sid[sid]
            if not listener.alive or listener.engaged or listener.returning or listener.reengage_block != 0:
                continue
            if self.home_to_player(listener) > self.p["leash_radius"]:
                continue
            self.engage(listener, step, hop)
            listener.alarm_role = "follower"
            if hop < self.p["max_hops"]:
                self.reserve_hop(listener, hop + 1, step)

    def begin_return(self, monster, step):
        monster.engaged = False
        monster.returning = True
        monster.home_x, monster.home_y = monster.spawn_x, monster.spawn_y
        monster.token = "-"
        monster.wait_slot = None
        monster.inner_slot = None
        monster.ranged_slot = None
        monster.state = IDLE
        monster.cast_remaining = 0
        monster.action = None
        monster.retreat_kind = None
        monster.retreat_left = 0
        monster.fear_pending = False
        monster.alarm_role = "none"
        monster.has_rallied = False
        if self.p["rules"] == "v2":
            monster.alarm_call_step = 0
        monster.out_of_reach = 0
        monster.return_best = planar(monster.x, monster.y, monster.home_x, monster.home_y)
        monster.return_stall = 0
        monster.flee_streak = 0
        self.return_log.append((step, monster.sid))

    def finish_return(self, monster):
        monster.returning = False
        monster.has_rallied = False
        monster.reengage_block = max(monster.reengage_block, self.p["arrival_settle_steps"])

    def update_returning(self, monster, step):
        p = self.p
        home_distance = planar(monster.x, monster.y, monster.home_x, monster.home_y)
        if home_distance <= p["home_arrival"]:
            self.finish_return(monster)
            return
        if home_distance < monster.return_best - 1.0:
            monster.return_best = home_distance
            monster.return_stall = 0
            return
        monster.return_stall += 1
        if monster.return_stall >= p["return_stall_steps"]:
            monster.home_x, monster.home_y = monster.x, monster.y
            self.finish_return(monster)

    def update_engaged_limits(self, monster, step):
        p = self.p
        if planar(monster.x, monster.y, monster.home_x, monster.home_y) > p["max_chase_home"]:
            self.begin_return(monster, step)
            return
        is_out_of_reach = self.home_to_player(monster) > p["leash_radius"] or self.distance_to_player(monster) > p["lose_target_radius"]
        monster.out_of_reach = monster.out_of_reach + 1 if is_out_of_reach else 0
        if monster.out_of_reach >= p["give_up_steps"]:
            self.begin_return(monster, step)

    def update_engagement(self, step):
        self.now = step
        p = self.p
        death_alarms = sorted(self.pending_death_alarms)
        self.pending_death_alarms = []
        self.process_home_kills(step)
        self.process_kills(step)
        discoverers = []
        for monster in self.monsters:
            if monster.returning:
                self.update_returning(monster, step)
            elif monster.engaged:
                self.update_engaged_limits(monster, step)
            elif self.can_discover(monster):
                discoverers.append(monster)
        for monster in discoverers:
            self.engage(monster, step, 0)
        if p["alert_rule"] == "legacy":
            self.alert_legacy(discoverers, step)
        elif p["alert_rule"] == "ripple":
            for _, origin_x, origin_y in death_alarms:
                self.reserve_from_point(origin_x, origin_y, 1, step)
            self.alert_ripple(discoverers, step)
            self.process_due_alarms(step)
        self.evaluate_morale(step)

    def process_home_kills(self, step):
        for kill_step, sid in self.config.get("home_kills", ()):
            if kill_step != step:
                continue
            victim = self.monster_by_sid[sid]
            if not victim.alive or victim.engaged or victim.returning:
                continue
            self.kill(victim, step)
            if self.p["death_alarm"]:
                self.pending_death_alarms.append((sid, victim.x, victim.y))

    def process_kills(self, step):
        p = self.p
        interval = p["kill_interval"]
        if interval <= 0 or step < p["kill_start"] or (step - p["kill_start"]) % interval != 0:
            return
        count = p["kill_count"]
        if p["kill_max"] is not None:
            count = min(count, p["kill_max"] - self.kills)
        if count <= 0:
            return
        pool = [monster for monster in self.monsters if monster.engaged]
        if not pool:
            return
        ordered = sorted(pool, key=lambda monster: (int((monster.x - self.player_x) ** 2 + (monster.y - self.player_y) ** 2), monster.sid))
        if p["kill_order"] == "leader_first":
            ordered = [monster for monster in ordered if monster.is_leader] + [monster for monster in ordered if not monster.is_leader]
        for victim in ordered[:count]:
            self.kill(victim, step)

    def kill(self, victim, step):
        if self.distance_to_player(victim) > 200.0:
            self.remote_kills += 1
        victim.alive = False
        victim.death_step = step
        self.monsters.remove(victim)
        self.alarms.pop(victim.sid, None)
        self.kills += 1
        self.last_death_step = step
        if self.first_death_step is None:
            self.first_death_step = step
        self.pending_deaths.append((victim.sid, victim.x, victim.y, victim.is_leader))

    def is_panicking(self, monster):
        return monster.fear_pending or monster.retreat_kind == "panic"

    def evaluate_morale(self, step):
        deaths = sorted(self.pending_deaths)
        self.pending_deaths = []
        if not self.p["morale_enabled"]:
            return
        should_judge = bool(deaths) or self.evaluate_next
        self.evaluate_next = False
        if should_judge:
            self.judge_morale(deaths, step)
        self.start_pending_panics(step)

    def is_rallied(self, monster, step):
        if self.p["rules"] == "v2":
            return monster.has_rallied
        return step < monster.rally_until

    def is_witness_candidate(self, monster, step):
        return monster.engaged and not monster.is_immune and not self.is_panicking(monster) and not self.is_rallied(monster, step)

    def record_loss(self, witness, step):
        p = self.p
        if p["rules"] == "v2":
            witness.loss_steps.append(step)
            del witness.loss_steps[:-p["loss_ring"]]
            return
        witness.recent_loss = witness.recent_loss + 1 if step - witness.last_loss_step <= p["loss_memory"] else 1
        witness.last_loss_step = step
        witness.loss_steps = [earlier for earlier in witness.loss_steps if step - earlier <= p["loss_memory"]] + [step]

    def effective_losses(self, witness, step):
        p = self.p
        if p["rules"] == "v2":
            return sum(1 for earlier in witness.loss_steps if step - earlier <= p["loss_window"])
        if p["loss_mode"] == "window":
            return sum(1 for earlier in witness.loss_steps if step - earlier <= p["loss_memory"])
        return witness.recent_loss if step - witness.last_loss_step <= p["loss_memory"] else 0

    def has_other_leader_near(self, witness):
        return any(other.is_leader and planar(other.x, other.y, witness.x, witness.y) <= self.p["leader_radius"]
                   for other in self.monsters if other is not witness)

    def compute_fear_steps(self, witness, step, is_leader_fear):
        p = self.p
        low, high = (p["leader_fear_min"], p["leader_fear_max"]) if is_leader_fear and p["rules"] == "v2" else (p["fear_min"], p["fear_max"])
        return low + (witness.sid * 37 + step) % (high - low + 1)

    def judge_morale(self, deaths, step):
        p = self.p
        is_v2 = p["rules"] == "v2"
        engaged = [monster for monster in self.monsters if monster.engaged]
        for _, death_x, death_y, was_leader in deaths:
            for witness in engaged:
                if not self.is_witness_candidate(witness, step):
                    continue
                distance = planar(witness.x, witness.y, death_x, death_y)
                if is_v2:
                    if distance <= p["witness_radius"]:
                        self.record_loss(witness, step)
                    if was_leader and distance <= p["leader_radius"] and not (p["leader_shock_exclusive"] and self.has_other_leader_near(witness)):
                        witness.leader_shock = True
                    continue
                radius = p["leader_radius"] if was_leader else p["witness_radius"]
                if distance > radius:
                    continue
                self.record_loss(witness, step)
                if was_leader:
                    witness.leader_shock = True
        panicking_ids = {monster.sid for monster in engaged if self.is_panicking(monster)}
        marks = []
        for witness in engaged:
            if not self.is_witness_candidate(witness, step):
                continue
            if witness.leader_shock:
                marks.append((witness, 0, True))
                continue
            losses = self.effective_losses(witness, step)
            required_losses = p["min_losses"] if is_v2 else {"spec": p["min_losses"], "any": 1, "ally": 0}[p["contagion_gate"]]
            if losses < required_losses:
                continue
            panicking_allies = 0
            deepest = -1
            support = 1
            for ally in engaged:
                if ally is witness or planar(ally.x, ally.y, witness.x, witness.y) > p["witness_radius"]:
                    continue
                if ally.sid in panicking_ids:
                    panicking_allies += 1
                    deepest = max(deepest, ally.panic_depth)
                else:
                    support += p["immune_weight"] if ally.is_immune else 1
            if is_v2:
                if support < p["min_support"]:
                    continue
                pressure = p["loss_weight"] * losses + p["panicking_weight"] * panicking_allies
            else:
                if losses + panicking_allies < p["min_losses"]:
                    continue
                pressure = 2 * (losses + panicking_allies)
            if pressure >= support:
                marks.append((witness, 0 if deaths else deepest + 1, False))
        for witness, depth, is_leader_fear in marks:
            witness.fear_pending = True
            witness.fear_pending_until = step + p["fear_pending"]
            witness.recent_loss = 0
            witness.loss_steps = []
            witness.leader_shock = False
            witness.panic_depth = depth
            witness.is_leader_fear = is_leader_fear
            witness.fear_steps = self.compute_fear_steps(witness, step, is_leader_fear)
            if is_v2:
                witness.has_rallied = True

    def start_pending_panics(self, step):
        p = self.p
        is_v2 = p["rules"] == "v2"
        started = False
        for witness in self.monsters:
            if not witness.fear_pending:
                continue
            if not witness.engaged:
                witness.fear_pending = False
                continue
            if step > witness.fear_pending_until:
                witness.fear_pending = False
                self.pending_cancelled += 1
                continue
            if witness.state == CAST or witness.retreat_left > 0:
                continue
            span = p["fear_max"] - p["fear_min"] + 1
            fear_steps = witness.fear_steps if is_v2 else p["fear_min"] + (witness.sid * 37 + step) % span
            self.begin_retreat(witness, step, "panic", fear_steps, p["fear_speed_scale"], p["panic_hold_distance"])
            witness.fear_pending = False
            if not is_v2:
                witness.rally_until = step + p["fear_max"] + p["rally_immune"]
            witness.fear_start = step
            witness.panic_starts += 1
            witness.awaiting_reengage = True
            witness.awaiting_approach = True
            since_death = step - self.last_death_step if self.last_death_step is not None else -1
            self.panic_start_log.append((step, witness.sid, witness.panic_depth, since_death))
            started = True
        if started:
            self.evaluate_next = True

    def begin_retreat(self, monster, step, kind, steps, scale, hold=0.0):
        monster.retreat_kind = kind
        monster.retreat_left = steps
        monster.retreat_scale = scale
        monster.retreat_hold = hold
        monster.retreat_best = -1.0
        monster.retreat_stall = 0
        monster.retreat_start = step
        monster.state = MOVE
        monster.action = "panic" if kind == "panic" else "flee"
        monster.cast_remaining = 0
        monster.force_think = False
        monster.hold_remaining = 0
        monster.intent = None

    def complete_retreat(self, monster, step, reason):
        kind = monster.retreat_kind
        self.retreat_log.append((kind, monster.sid, monster.retreat_start, step, reason))
        monster.retreat_kind = None
        monster.retreat_left = 0
        monster.state = IDLE
        monster.force_think = True
        monster.action = None
        monster.hold_remaining = 0

    def away_intent(self, monster, scale):
        dx = monster.x - self.player_x
        dy = monster.y - self.player_y
        length = math.hypot(dx, dy)
        if length < 1e-6:
            dx, dy, length = 1.0, 0.0, 1.0
        return (dx / length, dy / length, scale, 1.0e9)

    def step_retreat(self, monster, step):
        p = self.p
        monster.state = MOVE
        monster.retreat_left -= 1
        distance = self.distance_to_player(monster)
        if monster.retreat_hold > 0.0 and distance >= monster.retreat_hold:
            if monster.retreat_left <= 0:
                self.complete_retreat(monster, step, "timeout")
            return
        if distance > monster.retreat_best + p["retreat_progress"]:
            monster.retreat_best = distance
            monster.retreat_stall = 0
        else:
            monster.retreat_stall += 1
        is_cornered_rule = monster.retreat_kind == "panic" or p["flee_cornered"]
        if is_cornered_rule and monster.retreat_stall >= p["cornered_steps"]:
            self.complete_retreat(monster, step, "cornered")
            return
        if monster.retreat_left <= 0:
            self.complete_retreat(monster, step, "timeout")
            return
        monster.intent = self.away_intent(monster, monster.retreat_scale)

    def step_returning(self, monster):
        distance = planar(monster.x, monster.y, monster.home_x, monster.home_y)
        monster.intent = self.direction_to(monster, monster.home_x, monster.home_y, 1.0, distance)

    def think_and_act(self, monster, step):
        monster.intent = None
        if monster.returning:
            self.step_returning(monster)
            return
        if not monster.engaged:
            return
        if monster.retreat_left > 0:
            self.step_retreat(monster, step)
            return
        super().think_and_act(monster, step)

    def flee_speed_scale(self, monster):
        return FLEE_KINDS[monster.kind]["speed"]

    def flee_score(self, monster, distance):
        p = self.p
        spec = FLEE_KINDS[monster.kind]
        if p["flee_health_curve"] == "logistic":
            health = logistic(monster.health_ratio, -20.0, 0.3)
        else:
            health = clamp(1.0 - monster.health_ratio ** 2, 0.0, 1.0)
        allies = sum(1 for other in self.monsters if other is not monster and planar(other.x, other.y, monster.x, monster.y) <= spec["ally_radius"])
        ally_term = clamp(1.0 - clamp(allies / spec["ally_range"], 0.0, 1.0), 0.0, 1.0)
        score = spec["weight"] * health * ally_term
        if p["flee_rule"] != "new":
            return score
        if self.now < monster.flee_cooldown_until and monster.action != "flee":
            return 0.0
        if not p["flee_gate_enabled"]:
            return score
        center = p["flee_gate_center_hyena"] if monster.kind == "hyena" else p["flee_gate_center"]
        return score * logistic(distance / spec["gate_range"], p["flee_gate_slope"], center)

    def score_actions(self, monster, distance, is_ready):
        if monster.kind == "golem":
            in_range = distance <= monster.ability_range
            approach = logistic(distance / 1600.0, 10.0, 0.12)
            smash = 2.2 * logistic(distance / 1600.0, -25.0, 0.16) if is_ready and in_range else 0.0
            return [("approach", approach), ("cast", smash), ("idle", 0.05)]
        scores = super().score_actions(monster, distance, is_ready)
        if monster.kind in FLEE_KINDS and self.p["flee_rule"] != "none" and monster.health_ratio < 1.0:
            scores.append(("flee", self.flee_score(monster, distance)))
        return scores

    def decide(self, monster, step):
        previous = monster.action
        super().decide(monster, step)
        if monster.action == "flee" and previous != "flee":
            monster.flee_starts += 1
            self.flee_starts += 1

    def start_action(self, monster, name):
        if name != "flee":
            super().start_action(monster, name)
            return
        monster.state = MOVE
        if self.p["flee_rule"] == "new" and monster.retreat_left == 0:
            monster.flee_cooldown_until = self.now + self.p["flee_cooldown_steps"]
            self.begin_retreat(monster, self.now, "flee", self.p["flee_steps"], self.flee_speed_scale(monster))

    def compute_tactics_intent(self, monster):
        if monster.action == "flee":
            return self.away_intent(monster, self.flee_speed_scale(monster))
        return super().compute_tactics_intent(monster)

    def on_cast_started(self, monster, step):
        super().on_cast_started(monster, step)
        if monster.awaiting_reengage:
            monster.awaiting_reengage = False
            self.reengage_times.append((step - monster.fear_start) / STEPS_PER_SECOND)

    def clear_slots(self, monster):
        monster.token = "-"
        monster.wait_slot = None
        monster.inner_slot = None
        monster.ranged_slot = None
        monster.on_deck = False

    def update_tactics(self, step):
        constants = self.constants
        self.current_step = step
        previous_x, previous_y = self.previous_player
        self.player_velocity = ((self.player_x - previous_x) * STEPS_PER_SECOND, (self.player_y - previous_y) * STEPS_PER_SECOND)
        self.previous_player = (self.player_x, self.player_y)
        self.swing_player_weapon(step)
        engaged_all = [monster for monster in self.monsters if monster.engaged]
        if engaged_all and self.record is None:
            self.record = st.TargetRecordV2(step, constants)
            self.phase_timeline.append((round(step / STEPS_PER_SECOND, 2), st.PHASE_NAMES[0], 0))
        record = self.record
        if record is None:
            return
        count_radius = constants["engaged_count_radius"]
        record.n_melee = sum(1 for monster in engaged_all if monster.role == "melee" and self.distance_to_player(monster) <= count_radius)
        record.n_ranged = sum(1 for monster in engaged_all if monster.role == "ranged" and self.distance_to_player(monster) <= count_radius)
        for monster in engaged_all:
            if monster.retreat_left > 0:
                self.clear_slots(monster)
        engaged = [monster for monster in engaged_all if monster.retreat_left == 0]
        released = self.sync_tokens_v2(step, engaged)
        self.detect_blocked_slots(step, engaged)
        self.update_director_v2(step)
        if step % constants["ring_pass_period"] == 0:
            self.assign_wait_ring_v2(engaged)
            self.assign_ranged_sectors_v2(engaged)
        is_token_pass = step % constants["token_pass_period"] == 0
        if is_token_pass:
            self.update_budget_v2()
            self.revoke_over_budget_v2(step, engaged)
        if is_token_pass or released:
            self.grant_tokens_v2(step, engaged)
            self.assign_inner_sectors_v2(engaged)
        self.check_encounter_reset(step)

    def update_budget_v2(self):
        super().update_budget_v2()
        if self.role_blocked["melee"] > 0:
            self.record.has_queue = True

    def role_caps(self):
        p = self.p
        if not p["roles_enabled"]:
            return {}
        return {"charger": p["charger_cap"], "support": p["support_cap"]}

    def role_capped(self, candidate, holders, caps, step):
        if candidate.cap_role not in caps:
            return False
        bonus = 1 if self.p["retaliation_cap_bonus"] and self.is_retaliating(candidate, step) else 0
        return holders[candidate.cap_role] >= caps[candidate.cap_role] + bonus

    def grant_tokens_v2(self, step, engaged):
        constants = self.constants
        cost = constants["token_cost"]
        caps = self.role_caps()
        is_v2 = self.p["rules"] == "v2"
        for pool in ("melee", "ranged"):
            budget = self.pool_budget(pool)
            members = [monster for monster in engaged if monster.role == pool]
            for monster in members:
                monster.on_deck = False
            held = sum(cost for monster in members if monster.token in ("R", "C"))
            holders = {name: sum(1 for monster in members if monster.cap_role == name and monster.token in ("R", "C")) for name in caps}
            candidates = sorted((monster for monster in members if self.is_grant_candidate(monster, step)),
                                key=lambda monster: self.candidate_key_v2(monster, step))
            remaining = []
            blocked = 0
            for index, candidate in enumerate(candidates):
                if is_v2 and self.role_capped(candidate, holders, caps, step):
                    blocked += 1
                    continue
                if budget - held < cost:
                    remaining.extend(candidates[index:])
                    break
                if not is_v2 and self.role_capped(candidate, holders, caps, step):
                    blocked += 1
                    continue
                self.grant_token_v2(candidate, step)
                held += cost
                if candidate.cap_role in caps:
                    holders[candidate.cap_role] += 1
            self.role_blocked[pool] = blocked
            if not remaining:
                continue
            for candidate in [monster for monster in remaining if self.is_retaliating(monster, step)]:
                if self.role_capped(candidate, holders, caps, step):
                    continue
                candidate_distance = self.distance_to_player(candidate)
                victims = [monster for monster in members if monster.token == "R"
                           and self.distance_to_player(monster) >= constants["intercept_distance_ratio"] * candidate_distance]
                if not victims:
                    continue
                victim = max(victims, key=lambda monster: (self.distance_to_player(monster), -monster.sid))
                victim.token = "-"
                victim.inner_slot = None
                if victim.cap_role in caps:
                    holders[victim.cap_role] -= 1
                self.grant_token_v2(candidate, step)
                if candidate.cap_role in caps:
                    holders[candidate.cap_role] += 1
                remaining.remove(candidate)
                self.v2_events["intercepts"] += 1
            for candidate in remaining:
                if self.role_capped(candidate, holders, caps, step):
                    continue
                candidate.on_deck = True
                self.v2_events["ondeck_marks"] += 1
                break

    def assign_wait_ring_v2(self, engaged):
        super().assign_wait_ring_v2(engaged)
        p = self.p
        if not p["band_floor_enabled"]:
            return
        waiting = [monster for monster in engaged if monster.role == "melee" and monster.wait_slot is not None]
        if p["band_floor_v2"]:
            body = st.MONSTER_RADIUS
            floor = max((self.v2_ring_radius(monster, monster.wait_slot[0]) + body for monster in waiting), default=None)
            floor = 0.0 if floor is None else floor + p["band_gap_v2"] + body
            reach_cap = p["band_floor_cap"]
        else:
            floor = max((self.v2_ring_radius(monster, monster.wait_slot[0]) for monster in waiting), default=-p["band_gap"]) + p["band_gap"]
            floor = max(floor, 0.0)
            reach_cap = None
        self.floor_samples.append(floor)
        if self.last_floor is not None and abs(floor - self.last_floor) > 50.0:
            self.floor_changes += 1
        self.last_floor = floor
        for monster in engaged:
            if monster.role != "ranged" or monster.band_base is None:
                continue
            upper = monster.max_reach - p["ranged_reach_margin"]
            if reach_cap is not None:
                upper = min(upper, reach_cap)
            capped = max(0.0, min(floor, upper))
            band_min = max(monster.band_base[0], capped)
            band_max = max(monster.band_base[1], band_min + p["band_min_width"])
            monster.band = (band_min, band_max)

    def integrate_and_separate(self):
        super().integrate_and_separate()
        radius = self.p["wall_radius"]
        if radius <= 0.0:
            return
        center_x, center_y = self.p["wall_center"]
        for monster in self.monsters:
            offset_x, offset_y = monster.x - center_x, monster.y - center_y
            distance = math.hypot(offset_x, offset_y)
            if distance <= radius:
                continue
            monster.x = center_x + offset_x * radius / distance
            monster.y = center_y + offset_y * radius / distance

    def record_step(self, step):
        super().record_step(step)
        engaged_count = 0
        panic_count = 0
        for monster in self.monsters:
            if monster.returning:
                monster.returning_steps += 1
                self.sum_returning += 1
                continue
            if not monster.engaged:
                monster.home_steps += 1
                self.sum_home += 1
                continue
            engaged_count += 1
            monster.engaged_steps += 1
            if self.is_panicking(monster):
                panic_count += 1
                monster.panic_steps += 1
                monster.panic_streak += 1
                monster.panic_streak_max = max(monster.panic_streak_max, monster.panic_streak)
            else:
                monster.panic_streak = 0
            if monster.awaiting_approach and monster.retreat_left == 0 and self.distance_to_player(monster) <= self.p["reapproach_distance"]:
                monster.awaiting_approach = False
                self.reapproach_times.append((step - monster.fear_start) / STEPS_PER_SECOND)
            if monster.retreat_kind == "panic":
                self.sum_retreat += 1
                if self.distance_to_player(monster) <= 800.0:
                    self.panic_visible_steps += 1
            if monster.action == "flee":
                self.sum_flee += 1
                monster.flee_steps += 1
                monster.flee_streak += 1
                monster.flee_streak_max = max(monster.flee_streak_max, monster.flee_streak)
            else:
                monster.flee_streak = 0
            self.max_distance_seen = max(self.max_distance_seen, self.distance_to_player(monster))
        self.sum_engaged += engaged_count
        self.sum_panic += panic_count
        self.max_panicking = max(self.max_panicking, panic_count)
        if engaged_count > 0 and panic_count * 2 >= engaged_count:
            self.half_panic_steps += 1
        if not self.monsters and self.empty_step is None:
            self.empty_step = step
        for name in ("charger", "support"):
            holding = sum(1 for monster in self.monsters if monster.cap_role == name and monster.token in ("R", "C"))
            self.holder_samples[name][0] = max(self.holder_samples[name][0], holding)
            self.holder_samples[name][1] += holding
        melee_holding = sum(1 for monster in self.monsters if monster.role == "melee" and monster.token in ("R", "C"))
        self.melee_holder_samples[0] = max(self.melee_holder_samples[0], melee_holding)
        self.melee_holder_samples[1] += melee_holding
        for monster in self.monsters:
            if monster.kind != "archer":
                continue
            previous = self.archer_previous.get(monster.sid)
            if previous is not None:
                self.archer_path += math.hypot(monster.x - previous[0], monster.y - previous[1])
            self.archer_previous[monster.sid] = (monster.x, monster.y)
        for name in ("charger", "support"):
            casting = sum(1 for monster in self.monsters if monster.cap_role == name and monster.state == CAST)
            self.cast_samples[name] = max(self.cast_samples[name], casting)
        if step >= self.p["archer_sample_start"] and step % 8 == 0:
            for monster in self.monsters:
                if monster.kind == "archer" and monster.engaged and monster.state != CAST and monster.retreat_left == 0:
                    self.archer_distances.append(self.distance_to_player(monster))
        parts = [str(step)]
        for monster in self.monsters:
            parts.append(f"{monster.engaged:d}{monster.returning:d}{monster.retreat_left}{monster.fear_pending:d}{monster.alarm_hop}{monster.alarm_role[:1]}")
        self.hasher.update("|".join(parts).encode("ascii"))

    def compile_metrics(self):
        p = self.p
        steps = self.total_steps
        seconds = steps / STEPS_PER_SECOND
        starts = [step for step, _ in self.cast_log]
        gaps = [later - earlier for earlier, later in zip(starts, starts[1:])]
        alive_steps = self.empty_step if self.empty_step is not None else steps
        alive_seconds = max(alive_steps / STEPS_PER_SECOND, 1e-9)
        starvation = []
        for monster in self.all_monsters:
            if monster.first_engage_step is None:
                continue
            end = monster.death_step if monster.death_step is not None else steps
            points = [monster.first_engage_step] + monster.cast_starts + [end]
            starvation.append(max(later - earlier for earlier, later in zip(points, points[1:])) / STEPS_PER_SECOND)
        pack_size = len(self.all_monsters)
        woken = len({sid for _, sid, _ in self.wake_log})
        first_wake = min((step for step, _, _ in self.wake_log), default=None)
        wake_by_hop = {hop: sum(1 for _, _, wake_hop in self.wake_log if wake_hop == hop) for hop in range(0, p["max_hops"] + 2)}

        def awake_fraction(delay_seconds):
            if first_wake is None:
                return 0.0
            limit = first_wake + int(delay_seconds * STEPS_PER_SECOND)
            return round(sum(1 for step, _, _ in self.wake_log if step <= limit) / pack_size, 3)

        last_wake = max((step for step, _, _ in self.wake_log), default=None)
        panic_retreats = [entry for entry in self.retreat_log if entry[0] == "panic"]
        flee_retreats = [entry for entry in self.retreat_log if entry[0] == "flee"]
        unique_panicked = sum(1 for monster in self.all_monsters if monster.panic_starts > 0)
        depths = [entry[2] for entry in self.panic_start_log]
        since_death = [entry[3] for entry in self.panic_start_log if entry[3] >= 0]
        total_monster_steps = max(1, self.sum_engaged + self.sum_returning + self.sum_home)
        cap_starts = {name: sum(len(monster.cast_starts) for monster in self.all_monsters if monster.cap_role == name) for name in ("charger", "support")}
        archer_distances = self.archer_distances
        metrics = {
            "pack_size": pack_size,
            "kills": self.kills,
            "remote_kills": self.remote_kills,
            "attack_starts": len(starts),
            "pack_alive_s": round(alive_seconds, 2),
            "starts_per_alive_s": round(len(starts) / alive_seconds, 3),
            "longest_pack_gap_s": round(max(gaps) / STEPS_PER_SECOND, 2) if gaps else None,
            "starvation_max_s": round(max(starvation), 2) if starvation else None,
            "starvation_p95_s": round(percentile(starvation, 0.95), 2) if starvation else None,
            "max_concurrent": max(self.concurrent_samples) if self.concurrent_samples else 0,
            "first_attack_s": round(starts[0] / STEPS_PER_SECOND, 2) if starts else None,
            "woken": woken,
            "never_woken": pack_size - woken,
            "first_wake_step": first_wake,
            "wake_spread_s": round((last_wake - first_wake) / STEPS_PER_SECOND, 3) if first_wake is not None else None,
            "awake_1s": awake_fraction(1.0),
            "awake_3s": awake_fraction(3.0),
            "awake_6s": awake_fraction(6.0),
            "wake_by_hop": wake_by_hop,
            "callers": len(self.caller_log),
            "silent": self.silent_count,
            "caller_sids": [sid for _, sid in self.caller_log],
            "wake_steps_by_sid": {str(sid): step for step, sid, _ in sorted(self.wake_log, key=lambda entry: entry[1])},
            "panic_starts": len(self.panic_start_log),
            "unique_panicked": unique_panicked,
            "unique_panicked_fraction": round(unique_panicked / pack_size, 3),
            "repanic": sum(max(0, monster.panic_starts - 1) for monster in self.all_monsters),
            "pending_cancelled": self.pending_cancelled,
            "max_simultaneous_panic": self.max_panicking,
            "half_panic_fraction": round(self.half_panic_steps / steps, 3),
            "chain_length": (max(depths) + 1) if depths else 0,
            "contagion_share": round(sum(1 for depth in depths if depth >= 1) / len(depths), 3) if depths else 0.0,
            "panic_time_fraction": round(self.sum_panic / max(1, self.sum_engaged), 3),
            "retreat_time_fraction": round(self.sum_retreat / max(1, self.sum_engaged), 3),
            "panic_retreats": len(panic_retreats),
            "panic_cornered_fraction": round(sum(1 for entry in panic_retreats if entry[4] == "cornered") / len(panic_retreats), 3) if panic_retreats else 0.0,
            "panic_retreat_mean_s": round(statistics.fmean((entry[3] - entry[2] + 1) / STEPS_PER_SECOND for entry in panic_retreats), 3) if panic_retreats else None,
            "panic_retreat_max_s": round(max((entry[3] - entry[2] + 1) / STEPS_PER_SECOND for entry in panic_retreats), 3) if panic_retreats else None,
            "reengage_median_s": median_or_none(self.reengage_times),
            "reengage_max_s": round(max(self.reengage_times), 3) if self.reengage_times else None,
            "reapproach_median_s": median_or_none(self.reapproach_times),
            "reapproach_p90_s": round(percentile(self.reapproach_times, 0.9), 3) if self.reapproach_times else None,
            "reapproach_max_s": round(max(self.reapproach_times), 3) if self.reapproach_times else None,
            "reapproach_samples": len(self.reapproach_times),
            "panic_visible_steps": self.panic_visible_steps,
            "panic_visible_mean_s": round(self.panic_visible_steps / len(self.panic_start_log) / STEPS_PER_SECOND, 3) if self.panic_start_log else None,
            "panickers_1s_after_death": sum(1 for entry in self.panic_start_log if 0 <= entry[3] <= STEPS_PER_SECOND),
            "panic_since_death_min_s": round(min(since_death) / STEPS_PER_SECOND, 2) if since_death else None,
            "flee_engaged_fraction": round(self.sum_flee / max(1, self.sum_engaged), 3),
            "panic_streak_max_s": round(max((monster.panic_streak_max for monster in self.all_monsters), default=0) / STEPS_PER_SECOND, 2),
            "hop2_share": round(wake_by_hop.get(2, 0) / max(1, wake_by_hop.get(1, 0) + wake_by_hop.get(2, 0)), 3),
            "melee_holders_max": self.melee_holder_samples[0],
            "melee_holders_mean": round(self.melee_holder_samples[1] / steps, 3),
            "archer_path_cm_per_s": round(self.archer_path / seconds / max(1, sum(1 for monster in self.all_monsters if monster.kind == "archer")), 1),
            "floor_changes": self.floor_changes,
            "reengage_samples": len(self.reengage_times),
            "panic_after_last_death_max_s": round(max(since_death) / STEPS_PER_SECOND, 2) if since_death else None,
            "returns": len(self.return_log),
            "first_return_s": round(self.return_log[0][0] / STEPS_PER_SECOND, 2) if self.return_log else None,
            "flee_starts": self.flee_starts,
            "flee_time_fraction": round(self.sum_flee / total_monster_steps, 3),
            "returning_time_fraction": round(self.sum_returning / total_monster_steps, 3),
            "home_time_fraction": round(self.sum_home / total_monster_steps, 3),
            "engaged_time_fraction": round(self.sum_engaged / total_monster_steps, 3),
            "flee_retreats": len(flee_retreats),
            "flee_cornered": sum(1 for entry in flee_retreats if entry[4] == "cornered"),
            "longest_flee_s": round(max((monster.flee_streak_max for monster in self.all_monsters), default=0) / STEPS_PER_SECOND, 2),
            "max_distance_seen": round(self.max_distance_seen, 0),
            "charger_holders_max": self.holder_samples["charger"][0],
            "charger_holders_mean": round(self.holder_samples["charger"][1] / steps, 3),
            "support_holders_max": self.holder_samples["support"][0],
            "support_holders_mean": round(self.holder_samples["support"][1] / steps, 3),
            "charger_cast_max": self.cast_samples["charger"],
            "support_cast_max": self.cast_samples["support"],
            "charger_starts": cap_starts["charger"],
            "support_starts": cap_starts["support"],
            "role_blocked_melee_last": self.role_blocked["melee"],
            "archer_samples": len(archer_distances),
            "archer_median_cm": round(statistics.median(archer_distances), 1) if archer_distances else None,
            "archer_deciles_cm": [round(percentile(archer_distances, fraction / 10.0), 0) for fraction in range(1, 10)] if archer_distances else None,
            "archer_below_590": round(sum(1 for value in archer_distances if value < 590.0) / len(archer_distances), 3) if archer_distances else None,
            "archer_below_610": round(sum(1 for value in archer_distances if value < 610.0) / len(archer_distances), 3) if archer_distances else None,
            "band_floor_median_cm": round(statistics.median(self.floor_samples), 1) if self.floor_samples else None,
            "player_damage_pct_per_s": round(self.legacy_damage / st.PLAYER_MAX_UNITS * 100.0 / seconds, 3),
        }
        return metrics


def make_config(kinds, motion, seconds, pack=None, positions=None, tactics=None, player_attack="nearest", spawn_band=(700.0, 1100.0), home_kills=None):
    overrides = dict(TACTICS_BASE)
    if tactics:
        overrides.update(tactics)
    return {"composition": list(kinds), "positions": positions, "motion": motion, "policy": "V2", "overrides": overrides,
            "goblin_cast": 0.9, "spawn_band": spawn_band, "seconds": seconds, "player_attack": player_attack, "pack": pack or {}, "home_kills": home_kills or []}


def run_pack(config, with_hash=False):
    simulation = PackSimulation(config)
    metrics = simulation.run()
    if with_hash:
        metrics = dict(metrics, hash=simulation.hasher.hexdigest())
    return metrics


def s1_positions(layout, spacing):
    if layout == "line4":
        offsets = [(0.0, index * spacing) for index in range(4)]
    elif layout == "clump12":
        offsets = [(column * spacing, (row - 1.5) * spacing) for row in range(4) for column in range(3)]
    elif layout == "mixed8":
        offsets = [(column * spacing, (row - 1.5) * spacing) for row in range(4) for column in range(2)]
    else:
        first = [(column * spacing, (row - 1) * spacing) for row in range(3) for column in range(2)]
        shift = spacing + 800.0
        offsets = first + [(x + shift, y) for x, y in first]
    x_shift = 1400.0 - min(x for x, _ in offsets)
    while min(math.hypot(x + x_shift, y) for x, y in offsets) < 1400.0:
        x_shift += 1.0
    return [(x + x_shift, y) for x, y in offsets]


S1_LAYOUT_SIZES = {"line4": 4, "clump12": 12, "twin6": 12}


def s1_config(layout, spacing, motion, rule, seconds, pack_extra=None):
    positions = s1_positions(layout, spacing)
    pack = {"alert_rule": rule, "morale_enabled": False}
    pack.update(pack_extra or {})
    return make_config(["goblin"] * len(positions), motion, seconds, pack=pack, positions=positions, player_attack="none")


def experiment_s1(quick):
    seconds = 10 if quick else 20
    spacings = (650,) if quick else (500, 650, 800)
    rows = []
    for layout in ("line4", "clump12", "twin6"):
        for spacing in spacings:
            for motion in ("stationary", "walk"):
                for rule in ("legacy", "ripple"):
                    metrics = run_pack(s1_config(layout, spacing, motion, rule, seconds))
                    rows.append(dict(metrics, layout=layout, spacing=spacing, motion=motion, rule=rule))
    a1_positions = [(1400.0, 0.0), (1400.0, 700.0), (1400.0, 1400.0), (1400.0, 2100.0)]
    for rule in ("legacy", "ripple"):
        config = make_config(["goblin"] * 4, "stationary", seconds, pack={"alert_rule": rule, "morale_enabled": False},
                             positions=a1_positions, player_attack="none")
        metrics = run_pack(config)
        rows.append(dict(metrics, layout="A1line", spacing=700, motion="stationary", rule=rule))
    return rows


S2_PACKS = [("goblin3", ["goblin"] * 3), ("goblin5", ["goblin"] * 5), ("goblin8", ["goblin"] * 8), ("goblin12", ["goblin"] * 12),
            ("goblin20", ["goblin"] * 20), ("goblin5+golem", ["golem"] + ["goblin"] * 5), ("hyena6", ["hyena"] * 6)]
S2_KILLS = [("area3/1s", 64, 3), ("steady1/1.5s", 96, 1), ("slow1/4s", 256, 1)]
S2_WALLS = [("none", 0.0), ("1200", 1200.0)]


def s2_run(kinds, kill, wall, morale, seconds, extra=None, spawn_band=(700.0, 1100.0)):
    pack = {"alert_rule": "ripple", "morale_enabled": morale, "kill_interval": kill[1], "kill_count": kill[2], "wall_radius": wall}
    pack.update(extra or {})
    return run_pack(make_config(kinds, "stationary", seconds, pack=pack, spawn_band=spawn_band))


def s2_row(pack_name, kinds, kill, wall_name, wall, seconds, on_extra=None, base_extra=None, label=None, spawn_band=(700.0, 1100.0)):
    off = s2_run(kinds, kill, wall, False, seconds, base_extra, spawn_band)
    on = s2_run(kinds, kill, wall, True, seconds, dict(base_extra or {}, **(on_extra or {})), spawn_band)
    return dict(on, pack=pack_name, kill=kill[0], wall=wall_name, variant=label or "spec", baseline_starts=off["attack_starts"],
                baseline_gap_s=off["longest_pack_gap_s"], baseline_per_alive_s=off["starts_per_alive_s"], baseline_alive_s=off["pack_alive_s"],
                starts_ratio=round(on["attack_starts"] / off["attack_starts"], 3) if off["attack_starts"] else None)


def experiment_s2(quick):
    seconds = 20 if quick else 60
    packs = S2_PACKS if not quick else [entry for entry in S2_PACKS if entry[0] in ("goblin5", "goblin12", "goblin5+golem")]
    rows = []
    for pack_name, kinds in packs:
        for kill in S2_KILLS:
            for wall_name, wall in S2_WALLS:
                rows.append(s2_row(pack_name, kinds, kill, wall_name, wall, seconds))
        rows.append(s2_row(pack_name, kinds, S2_KILLS[1], "800(extra)", 800.0, seconds))
        eccentric = {"wall_center": (-600.0, 0.0)}
        rows.append(s2_row(pack_name, kinds, S2_KILLS[1], "1200 off-center(-600,0)(extra)", 1200.0, seconds, on_extra=eccentric, base_extra=eccentric))
    golem_kinds = ["golem"] + ["goblin"] * 5
    for kill in S2_KILLS[:2]:
        leader = {"kill_order": "leader_first"}
        rows.append(s2_row("goblin5+golem(leader first)", golem_kinds, kill, "none", 0.0, seconds, on_extra=leader, base_extra=leader))
    return rows


S2_VARIANTS = [("window", {"loss_mode": "window"}), ("any-gate", {"contagion_gate": "any"}),
               ("ally-gate", {"contagion_gate": "ally"}), ("window+ally-gate", {"loss_mode": "window", "contagion_gate": "ally"})]


def experiment_s2v(quick):
    seconds = 20 if quick else 60
    rows = []
    packs = [entry for entry in S2_PACKS if entry[0] in ("goblin8", "goblin12", "goblin20")]
    for pack_name, kinds in packs:
        for kill in S2_KILLS:
            for label, extra in S2_VARIANTS:
                rows.append(s2_row(pack_name, kinds, kill, "none", 0.0, seconds, on_extra=extra, label=label))
    early = {"kill_start": 64}
    for pack_name, kinds in packs[1:]:
        for kill in S2_KILLS[:2]:
            for label, extra in (("spread:spec", {}), ("spread:ally-gate", {"contagion_gate": "ally"})):
                rows.append(s2_row(pack_name, kinds, kill, "none", 0.0, seconds, on_extra=dict(early, **extra), base_extra=early,
                                   label=label, spawn_band=(1200.0, 1450.0)))
    return rows


def experiment_s3(quick):
    seconds = 20 if quick else 60
    rows = []
    for count in (1, 3):
        for motion in ("stationary", "chase", "kiting"):
            for rule in ("healthy", "old", "new", "new_no_cornered", "new_no_gate"):
                pack = {"alert_rule": "ripple", "morale_enabled": False, "health_ratio": 0.25}
                if rule == "healthy":
                    pack["flee_rule"] = "none"
                elif rule == "old":
                    pack["flee_rule"] = "old"
                else:
                    pack["flee_rule"] = "new"
                if rule == "new_no_cornered":
                    pack["flee_cornered"] = False
                if rule == "new_no_gate":
                    pack["flee_gate_enabled"] = False
                config = make_config(["goblin"] * count, motion, seconds, pack=pack, player_attack="none")
                metrics = run_pack(config)
                rows.append(dict(metrics, count=count, motion=motion, rule=rule))
    return rows


def s4_positions(kinds, ranged_radius):
    positions = []
    for sid, kind in enumerate(kinds):
        angle = math.radians(st.wrap_degrees(sid * 137.50776))
        if kind == "archer":
            radius = ranged_radius
        elif kind == "golem":
            radius = 600.0 + 100.0 * math.fmod(sid * 0.6180339887, 1.0)
        else:
            radius = 700.0 + 400.0 * math.fmod(sid * 0.6180339887, 1.0)
        positions.append((radius * math.cos(angle), radius * math.sin(angle)))
    return positions


def experiment_s4(quick):
    seconds = 20 if quick else 60
    rows = []
    for count in (4, 6):
        for roles in (False, True):
            for motion in (("stationary",) if quick else ("stationary", "kiting")):
                pack = {"morale_enabled": False, "roles_enabled": roles}
                metrics = run_pack(make_config(["hyena"] * count, motion, seconds, pack=pack))
                rows.append(dict(metrics, group=f"hyena{count}", roles=roles, motion=motion, floor=False))
    for roles in (False, True):
        for motion in (("stationary",) if quick else ("stationary", "kiting")):
            pack = {"morale_enabled": False, "roles_enabled": roles}
            metrics = run_pack(make_config(["archer"] * 3, motion, seconds, pack=pack))
            rows.append(dict(metrics, group="archer3(support)", roles=roles, motion=motion, floor=False))
    kinds = ["golem"] * 4 + ["archer"] * 2
    positions = s4_positions(kinds, 540.0)
    for floor in (False, True):
        for motion in (("stationary",) if quick else ("stationary", "kiting")):
            pack = {"morale_enabled": False, "band_floor_enabled": floor, "archer_sample_start": 384}
            metrics = run_pack(make_config(kinds, motion, seconds, pack=pack, positions=positions))
            rows.append(dict(metrics, group="golem4+archer2", roles=False, motion=motion, floor=floor))
    for label, kinds in (("goblin4+archer2", ["goblin"] * 4 + ["archer"] * 2),
                         ("golem2+goblin3+archer2", ["golem"] * 2 + ["goblin"] * 3 + ["archer"] * 2)):
        positions = s4_positions(kinds, 540.0)
        for floor in (False, True):
            for motion in (("stationary",) if quick else ("stationary", "kiting")):
                pack = {"morale_enabled": False, "band_floor_enabled": floor}
                metrics = run_pack(make_config(kinds, motion, seconds, pack=pack, positions=positions))
                rows.append(dict(metrics, group=label, roles=False, motion=motion, floor=floor))
    return rows


SENSITIVITY_GRID = [
    ("hop_delay", "hop_base+hop_span", [("4~8", {"hop_base": 4, "hop_span": 4}), ("8~16", {"hop_base": 8, "hop_span": 8}),
                                       ("16~32", {"hop_base": 16, "hop_span": 16})]),
    ("min_losses", "min_losses", [("2", {"min_losses": 2}), ("3", {"min_losses": 3})]),
    ("fear", "fear_min~fear_max", [("32~64", {"fear_min": 32, "fear_max": 64}), ("64~128", {"fear_min": 64, "fear_max": 128}),
                                   ("128~192", {"fear_min": 128, "fear_max": 192})]),
    ("loss_mode", "loss_mode", [("chain(spec)", {"loss_mode": "chain"}), ("window", {"loss_mode": "window"})]),
    ("contagion_gate", "contagion_gate", [("spec", {"contagion_gate": "spec"}), ("any", {"contagion_gate": "any"}), ("ally", {"contagion_gate": "ally"})]),
    ("rally", "rally_immune", [("320", {"rally_immune": 320}), ("640", {"rally_immune": 640}), ("1280", {"rally_immune": 1280}),
                               ("window+640", {"loss_mode": "window", "rally_immune": 640}), ("window+960", {"loss_mode": "window", "rally_immune": 960}),
                               ("window+1280", {"loss_mode": "window", "rally_immune": 1280})]),
    ("cornered(벽800)", "cornered_steps", [("16", {"cornered_steps": 16}), ("24", {"cornered_steps": 24}), ("48", {"cornered_steps": 48})]),
]


def experiment_sensitivity(quick):
    seconds = 20 if quick else 60
    rows = []
    packs = [entry for entry in S2_PACKS if entry[0] in ("goblin5", "goblin8", "goblin12")]
    kills = S2_KILLS if not quick else S2_KILLS[:2]
    baselines = {}
    for pack_name, kinds in packs:
        for kill in kills:
            for wall in (1200.0, 800.0):
                baselines[(pack_name, kill[0], wall)] = s2_run(kinds, kill, wall, False, seconds)
    for name, _, values in SENSITIVITY_GRID:
        for label, extra in values:
            if name == "hop_delay":
                runs = []
                for layout in ("line4", "clump12"):
                    runs.append(run_pack(s1_config(layout, 650, "stationary", "ripple", 10 if quick else 20, extra)))
                rows.append({"param": name, "value": label, "kind": "alarm",
                             "wake_spread_s": [run["wake_spread_s"] for run in runs],
                             "awake_1s": [run["awake_1s"] for run in runs], "awake_3s": [run["awake_3s"] for run in runs],
                             "awake_6s": [run["awake_6s"] for run in runs], "callers": [run["callers"] for run in runs],
                             "never_woken": [run["never_woken"] for run in runs]})
                continue
            results = []
            wall = 800.0 if name.startswith("cornered") else 1200.0
            for pack_name, kinds in packs:
                for kill in kills:
                    on = s2_run(kinds, kill, wall, True, seconds, extra)
                    off = baselines[(pack_name, kill[0], wall)]
                    results.append((on, off))
            rows.append({
                "param": name, "value": label, "kind": "morale", "runs": len(results),
                "panic_starts": sum(on["panic_starts"] for on, _ in results),
                "repanic": sum(on["repanic"] for on, _ in results),
                "unique_fraction_mean": round(statistics.fmean(on["unique_panicked_fraction"] for on, _ in results), 3),
                "max_chain": max(on["chain_length"] for on, _ in results),
                "retreat_time_mean": round(statistics.fmean(on["retreat_time_fraction"] for on, _ in results), 3),
                "cornered_mean": round(statistics.fmean(on["panic_cornered_fraction"] for on, _ in results), 3),
                "starts_ratio_mean": round(statistics.fmean(on["attack_starts"] / off["attack_starts"] for on, off in results if off["attack_starts"]), 3),
                "starts_ratio_min": round(min(on["attack_starts"] / off["attack_starts"] for on, off in results if off["attack_starts"]), 3),
                "gap_max_s": max(on["longest_pack_gap_s"] or 0.0 for on, _ in results),
            })
    return rows


def experiment_determinism(quick):
    seconds = 10 if quick else 30
    cases = {
        "s1 twin6 ripple walk": s1_config("twin6", 650, "walk", "ripple", seconds),
        "s2 goblin8 steady wall1200": make_config(["goblin"] * 8, "stationary", seconds,
                                                  pack={"morale_enabled": True, "kill_interval": 96, "kill_count": 1, "wall_radius": 1200.0}),
        "s2 golem+goblin5 leader first": make_config(["golem"] + ["goblin"] * 5, "stationary", seconds,
                                                     pack={"morale_enabled": True, "kill_interval": 96, "kill_order": "leader_first"}),
        "s3 goblin3 chase new": make_config(["goblin"] * 3, "chase", seconds, pack={"flee_rule": "new", "health_ratio": 0.25, "morale_enabled": False},
                                           player_attack="none"),
        "s4 hyena6 charger": make_config(["hyena"] * 6, "kiting", seconds, pack={"roles_enabled": True, "morale_enabled": False}),
        "s4 golem4+archer2 floor": make_config(["golem"] * 4 + ["archer"] * 2, "stationary", seconds,
                                              pack={"band_floor_enabled": True, "morale_enabled": False},
                                              positions=s4_positions(["golem"] * 4 + ["archer"] * 2, 540.0)),
    }
    cases.update(v2_determinism_cases(seconds))
    rows = []
    for label, config in cases.items():
        first = run_pack(config, with_hash=True)
        second = run_pack(config, with_hash=True)
        rows.append({"case": label, "hash_first": first["hash"], "hash_second": second["hash"], "identical": first == second})
    return rows


V2_SLIM_KEYS = ["panic_starts", "unique_panicked", "pack_size", "unique_panicked_fraction", "panic_visible_mean_s", "panic_visible_steps",
                "attack_starts", "longest_pack_gap_s", "reapproach_median_s", "reapproach_p90_s", "reapproach_max_s", "repanic", "kills",
                "chain_length", "contagion_share", "panic_retreat_max_s", "panic_streak_max_s", "returns", "panickers_1s_after_death",
                "panic_since_death_min_s", "reapproach_samples", "panic_cornered_fraction"]


def slim(metrics):
    return {key: metrics.get(key) for key in V2_SLIM_KEYS}


def x_run(kinds, kill, morale, rules, extra, seconds, motion="stationary", positions=None, player_attack="nearest",
          spawn_band=(700.0, 1100.0), home_kills=None, with_hash=False):
    pack = {"alert_rule": "ripple", "morale_enabled": morale, "kill_interval": kill[1] if kill else 0, "kill_count": kill[2] if kill else 1}
    if rules == "v2":
        pack.update(V2_PACK)
    pack.update(extra or {})
    config = make_config(kinds, motion, seconds, pack=pack, positions=positions, player_attack=player_attack, spawn_band=spawn_band, home_kills=home_kills)
    return run_pack(config, with_hash)


def b1_setup(golems, goblins):
    kinds = ["golem"] * golems + ["goblin"] * goblins
    positions = []
    for sid in range(len(kinds)):
        angle = math.radians(st.wrap_degrees(sid * 137.50776))
        positions.append((450.0 * math.cos(angle), 450.0 * math.sin(angle)))
    return kinds, positions


def v2_s1_rows():
    rows = []
    a1_positions = [(1400.0, 0.0), (1400.0, 700.0), (1400.0, 1400.0), (1400.0, 2100.0)]
    mixed_kinds = ["goblin" if (index // 2) % 2 == 0 else "archer" for index in range(8)]
    for rules in ("v1", "v2"):
        metrics = x_run(["goblin"] * 4, None, False, rules, {}, 10, positions=a1_positions, player_attack="none")
        wake = metrics["wake_steps_by_sid"]
        steps = [wake[str(index)] for index in range(4) if str(index) in wake]
        rows.append({"case": "A1 일렬4(700)", "rules": rules, "woken": f"{metrics['woken']}/4", "gaps": [later - earlier for earlier, later in zip(steps, steps[1:])],
                     "callers": metrics["callers"], "caller_kinds": ["goblin"] * metrics["callers"], "y_delay": None})
        metrics = x_run(["goblin"] * 2, None, False, rules, {}, 6, positions=[(2000.0, 0.0), (2000.0, 600.0)], player_attack="none", home_kills=[(64, 0)])
        y_step = metrics["wake_steps_by_sid"].get("1")
        rows.append({"case": "A4 한 방 처치(Home X 사망, 600 옆 Y)", "rules": rules, "woken": f"{metrics['woken']}/1", "gaps": [], "callers": metrics["callers"],
                     "caller_kinds": [], "y_delay": (y_step - 64) if y_step is not None else None})
        metrics = x_run(mixed_kinds, None, False, rules, {}, 10, positions=s1_positions("mixed8", 650), player_attack="none")
        rows.append({"case": "혼합 8(고블린·궁수 2열씩)", "rules": rules, "woken": f"{metrics['woken']}/8", "gaps": [], "callers": metrics["callers"],
                     "caller_kinds": [mixed_kinds[sid] for sid in metrics["caller_sids"]], "y_delay": None})
    return rows


def v2_s2_rows(seconds, packs):
    labels = [("v1", "v1 규칙(창512 연쇄)", {}), ("v2", "v2 창192", {"loss_window": 192}), ("v2", "v2 창512", {"loss_window": 512})]
    baselines = {}
    rows = []
    for pack_name, kinds in packs:
        for kill in S2_KILLS:
            off = x_run(kinds, kill, False, "v1", {}, seconds)
            baselines[(pack_name, kill[0])] = off
            for rules, label, extra in labels:
                on = x_run(kinds, kill, True, rules, extra, seconds)
                rows.append(dict(slim(on), label=label, pack=pack_name, kill=kill[0], baseline_starts=off["attack_starts"],
                                 baseline_gap_s=off["longest_pack_gap_s"],
                                 starts_ratio=round(on["attack_starts"] / off["attack_starts"], 3) if off["attack_starts"] else None,
                                 gap_s=on["longest_pack_gap_s"]))
    return rows, baselines


def v2_sweep_rows(seconds, packs, baselines, windows, min_losses_values, loss_weights):
    rows = []
    for window in windows:
        for min_losses in min_losses_values:
            for loss_weight in loss_weights:
                label = f"창{window}/MinLoss{min_losses}/가중{loss_weight}"
                for pack_name, kinds in packs:
                    for kill in S2_KILLS:
                        on = x_run(kinds, kill, True, "v2", {"loss_window": window, "min_losses": min_losses, "loss_weight": loss_weight}, seconds)
                        off = baselines[(pack_name, kill[0])]
                        rows.append(dict(slim(on), label=label, window=window, min_losses=min_losses, loss_weight=loss_weight, pack=pack_name, kill=kill[0],
                                         starts_ratio=round(on["attack_starts"] / off["attack_starts"], 3) if off["attack_starts"] else None,
                                         gap_s=on["longest_pack_gap_s"]))
    return rows


def v2_leader_rows(seconds):
    rows = []
    golem_kinds = ["golem"] + ["goblin"] * 5
    for fear in ((64, 128), (128, 192)):
        extra = {"leader_fear_min": fear[0], "leader_fear_max": fear[1], "kill_order": "leader_first"}
        for kill in S2_KILLS[:2]:
            off = x_run(golem_kinds, kill, False, "v1", {"kill_order": "leader_first"}, seconds)
            on = x_run(golem_kinds, kill, True, "v2", extra, seconds)
            rows.append(dict(slim(on), fear=f"{fear[0]}~{fear[1]}", scene=f"골렘+고블린5 {kill[0]} 우두머리 먼저", baseline_starts=off["attack_starts"],
                             starts_ratio=round(on["attack_starts"] / off["attack_starts"], 3) if off["attack_starts"] else None,
                             gap_s=on["longest_pack_gap_s"], baseline_gap_s=off["longest_pack_gap_s"]))
        for golems in (1, 2):
            kinds, positions = b1_setup(golems, 3)
            kill = ("b1", 10000 if golems == 1 else 128, 1)
            b1_extra = dict(extra, kill_start=192, kill_max=golems)
            off = x_run(kinds, kill, False, "v1", {"kill_order": "leader_first", "kill_start": 192, "kill_max": golems}, seconds, positions=positions)
            on = x_run(kinds, kill, True, "v2", b1_extra, seconds, positions=positions)
            rows.append(dict(slim(on), fear=f"{fear[0]}~{fear[1]}", scene=f"B1형 골렘{golems}+고블린3(450cm, 3초 뒤 처치)", baseline_starts=off["attack_starts"],
                             starts_ratio=round(on["attack_starts"] / off["attack_starts"], 3) if off["attack_starts"] else None,
                             gap_s=on["longest_pack_gap_s"], baseline_gap_s=off["longest_pack_gap_s"]))
    return rows


def v2_s3_rows(seconds):
    rows = []
    variants = [("건강 100%", "v1", {"flee_rule": "none", "health_ratio": 1.0}), ("v1 old 25%", "v1", {"flee_rule": "old", "health_ratio": 0.25}),
                ("v1 new 25%", "v1", {"flee_rule": "new", "health_ratio": 0.25}), ("v2 25%", "v2", {"flee_rule": "new", "health_ratio": 0.25}),
                ("v2 32%", "v2", {"flee_rule": "new", "health_ratio": 0.32}), ("v2 40%", "v2", {"flee_rule": "new", "health_ratio": 0.40})]
    for kind in ("goblin", "hyena"):
        for count in (1, 3):
            for motion in ("stationary", "chase", "kiting"):
                for label, rules, extra in variants:
                    metrics = x_run([kind] * count, None, False, rules, extra, seconds, motion=motion, player_attack="none")
                    rows.append(dict(metrics, kind=kind, count=count, motion=motion, variant=label))
    return rows


def v2_s4_rows(seconds):
    rows = []
    groups = (("golem4+archer2", ["golem"] * 4 + ["archer"] * 2), ("goblin4+archer2", ["goblin"] * 4 + ["archer"] * 2),
              ("golem2+goblin3+archer2", ["golem"] * 2 + ["goblin"] * 3 + ["archer"] * 2))
    modes = (("바닥 끔", {}), ("v1 바닥(고리+150)", {"band_floor_enabled": True}),
             ("v2 바닥(고리+몸+100+몸, 상한700)", {"band_floor_enabled": True, "band_floor_v2": True}))
    for label, kinds in groups:
        positions = s4_positions(kinds, 540.0)
        for motion in ("stationary", "kiting"):
            for mode_label, extra in modes:
                metrics = x_run(kinds, None, False, "v1", extra, seconds, motion=motion, positions=positions)
                rows.append(dict(metrics, group=label, motion=motion, mode=mode_label, section="floor"))
    cap_modes = (("상한 끔", {}), ("v1 상한", {"roles_enabled": True}), ("v2 상한(보복 +1)", {"roles_enabled": True, "retaliation_cap_bonus": True}))
    for label, kinds in (("hyena4", ["hyena"] * 4), ("hyena6", ["hyena"] * 6), ("archer3(support)", ["archer"] * 3)):
        for motion in ("stationary", "kiting"):
            for mode_label, extra in cap_modes:
                metrics = x_run(kinds, None, False, "v1", extra, seconds, motion=motion)
                rows.append(dict(metrics, group=label, motion=motion, mode=mode_label, section="cap"))
    return rows


def v2_determinism_cases(seconds):
    kinds, positions = b1_setup(2, 3)
    cases = {}
    for label, args in {
        "v2 s2 goblin8 steady 창192": (["goblin"] * 8, ("s2", 96, 1), True, "v2", {}, {}),
        "v2 s3 goblin3 chase 체력 25%": (["goblin"] * 3, None, False, "v2", {"flee_rule": "new", "health_ratio": 0.25}, {"motion": "chase", "player_attack": "none"}),
        "v2 A4 사망 경보": (["goblin"] * 2, None, False, "v2", {}, {"positions": [(2000.0, 0.0), (2000.0, 600.0)], "player_attack": "none", "home_kills": [(64, 0)]}),
        "v2 B1형 골렘2+고블린3": (kinds, ("b1", 128, 1), True, "v2", {"kill_order": "leader_first", "kill_start": 192, "kill_max": 2}, {"positions": positions}),
        "v2 s4 hyena6 상한(보복 +1) 카이팅": (["hyena"] * 6, None, False, "v2", {"roles_enabled": True}, {"motion": "kiting"}),
        "v2 s4 golem4+archer2 바닥": (["golem"] * 4 + ["archer"] * 2, None, False, "v2", {"band_floor_enabled": True},
                                    {"positions": s4_positions(["golem"] * 4 + ["archer"] * 2, 540.0)}),
    }.items():
        kinds_arg, kill, morale, rules, extra, options = args
        pack = {"alert_rule": "ripple", "morale_enabled": morale, "kill_interval": kill[1] if kill else 0, "kill_count": kill[2] if kill else 1}
        if rules == "v2":
            pack.update(V2_PACK)
        pack.update(extra)
        cases[label] = make_config(kinds_arg, options.get("motion", "stationary"), seconds, pack=pack, positions=options.get("positions"),
                                   player_attack=options.get("player_attack", "nearest"), home_kills=options.get("home_kills"))
    return cases


def experiment_v2(quick):
    seconds = 20 if quick else 60
    packs = S2_PACKS if not quick else [entry for entry in S2_PACKS if entry[0] in ("goblin5", "goblin8", "goblin12", "goblin5+golem")]
    s2_rows, baselines = v2_s2_rows(seconds, packs)
    windows = (64, 95, 128, 160, 192, 512)
    sweep = v2_sweep_rows(seconds, packs, baselines, windows, (2, 3) if not quick else (2,), (2, 1))
    return {"s1": v2_s1_rows(), "s2": s2_rows, "sweep": sweep, "leader": v2_leader_rows(15 if not quick else 12), "s3": v2_s3_rows(seconds),
            "s4": v2_s4_rows(seconds), "pack_count": len(packs)}


EXPERIMENTS = {
    "s1": experiment_s1,
    "s2": experiment_s2,
    "s3": experiment_s3,
    "s4": experiment_s4,
    "s2v": experiment_s2v,
    "sensitivity": experiment_sensitivity,
    "determinism": experiment_determinism,
    "v2": experiment_v2,
}


def cell(value, digits=2):
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def table(headers, body):
    lines = ["| " + " | ".join(headers) + " |", "|" + "---|" * len(headers)]
    lines += ["| " + " | ".join(cell(value) for value in row) + " |" for row in body]
    return lines + [""]


def tables_s1(rows):
    body = []
    for row in rows:
        hops = row["wake_by_hop"]
        callers_text = f"{row['callers']}"
        if row["layout"] == "twin6":
            sids = row["caller_sids"]
            callers_text = f"{row['callers']} (A {sum(1 for sid in sids if sid < 6)}/B {sum(1 for sid in sids if sid >= 6)})"
        body.append([row["layout"], row["spacing"], row["motion"], row["rule"], f"{row['woken']}/{row['pack_size']}", row["awake_1s"],
                     row["awake_3s"], row["awake_6s"], row["wake_spread_s"], f"{hops.get('0', hops.get(0))}/{hops.get('1', hops.get(1))}/{hops.get('2', hops.get(2))}",
                     row["hop2_share"], hops.get("3", hops.get(3)), callers_text, row["silent"]])
    return ["## S1 경보 연쇄", "",
            "깨어난 수/무리, 첫 발견 뒤 1·3·6초 시점 기상 비율, 첫~마지막 기상 간격(초), 단별 기상 수 0/1/2단, 3단 이상 기상 수, 호출자 수, 침묵 발견자 수.", ""] + \
        table(["배치", "간격", "플레이어", "규칙", "기상", "1초", "3초", "6초", "퍼짐s", "0/1/2단", "2단 비율", "3단+", "호출자", "침묵"], body)


def tables_s2(rows):
    body = []
    for row in rows:
        body.append([row["pack"], row["kill"], row["wall"], row["kills"], row["panic_starts"], f"{row['unique_panicked']}/{row['pack_size']}",
                     row["max_simultaneous_panic"], row["chain_length"], row["contagion_share"], row["retreat_time_fraction"],
                     row["panic_cornered_fraction"], f"{row['baseline_starts']}→{row['attack_starts']}", row["starts_ratio"],
                     f"{cell(row['baseline_gap_s'])}→{cell(row['longest_pack_gap_s'])}", row["repanic"], row["reapproach_median_s"],
                     row["reengage_median_s"], row["panic_after_last_death_max_s"], row["panic_streak_max_s"]])
    return ["## S2 무리 사기", "",
            "공격 시작은 사기 끔→켬, 최장 무공격은 무리 전체 연속 공격 시작 사이 최대 간격(초). 연쇄 길이는 전염 깊이+1, 전염 비율은 직접 사망 판정이 아닌 공황 시작 비중.", ""] + \
        table(["무리", "처치", "벽", "처치수", "공황 시작", "공황 개체", "동시 최대", "연쇄", "전염비율", "후퇴시간비율", "막힘종료", "공격 시작", "비율",
               "최장 무공격s", "재공황", "복귀(400cm) 중앙s", "재교전(시전) 중앙s", "마지막 사망 후 공황 최대s", "공황 연속 최대s"], body)


def tables_s2v(rows):
    body = []
    for row in rows:
        body.append([row["pack"], row["kill"], row["variant"], row["kills"], row["panic_starts"], f"{row['unique_panicked']}/{row['pack_size']}",
                     row["max_simultaneous_panic"], row["chain_length"], row["contagion_share"], row["retreat_time_fraction"],
                     f"{row['baseline_starts']}→{row['attack_starts']}", row["starts_ratio"],
                     f"{cell(row['baseline_gap_s'])}→{cell(row['longest_pack_gap_s'])}", row["repanic"], row["reapproach_median_s"],
                     row["panic_streak_max_s"]])
    return ["## S2 변형(명세 밖 제안: 손실 슬라이딩 창, 전염 문턱 완화)", "",
            "벽 없음. window = 최근 512스텝 안 사망 수(명세는 연쇄 갱신 카운터), any-gate = 사망을 1회 이상 본 개체는 손실+공황 아군 합이 MinLosses 이상이면 판정, ally-gate = 사망을 못 봤어도 공황 아군만으로 판정(전염 최대치, 연쇄 붕괴 위험 탐색).", ""] + \
        table(["무리", "처치", "변형", "처치수", "공황 시작", "공황 개체", "동시 최대", "연쇄", "전염비율", "후퇴시간비율", "공격 시작", "비율", "최장 무공격s",
               "재공황", "복귀 중앙s", "공황 연속 최대s"], body)


def tables_s3(rows):
    body = []
    for row in rows:
        body.append([row["count"], row["motion"], row["rule"], row["returns"], row["first_return_s"], row["flee_starts"], row["flee_time_fraction"],
                     row["returning_time_fraction"], row["longest_flee_s"], row["flee_cornered"], row["attack_starts"], row["starts_per_alive_s"],
                     row["longest_pack_gap_s"], row["max_distance_seen"]])
    return ["## S3 MD-43 저체력 도망(체력 25%)", "",
            "old = 무제한 MoveAway(영역·LoseTarget 포기→귀환→재교전), new = 1.5초·쿨다운 6초·대상 거리 관문, new_no_cornered = 막힘 종료 끔, new_no_gate = 거리 관문 끔.", ""] + \
        table(["수", "플레이어", "규칙", "귀환", "첫 귀환s", "도망 시작", "도망 시간비율", "귀환 시간비율", "최장 도망s", "막힘 종료", "공격 시작", "시작/초",
               "최장 무공격s", "최대 거리cm"], body)


def tables_s4(rows):
    body_roles = []
    body_floor = []
    for row in rows:
        if row["group"] in ("golem4+archer2", "goblin4+archer2", "golem2+goblin3+archer2"):
            body_floor.append([row["group"], row["motion"], "켬" if row["floor"] else "끔", row["archer_samples"], row["archer_median_cm"],
                               "/".join(str(int(value)) for value in (row["archer_deciles_cm"] or [])), row["archer_below_590"],
                               row["band_floor_median_cm"], row["floor_changes"], row["archer_path_cm_per_s"], row["attack_starts"], row["starts_per_alive_s"],
                               row["longest_pack_gap_s"], row["starvation_max_s"]])
        else:
            body_roles.append([row["group"], row["motion"], "켬" if row["roles"] else "끔", row["melee_holders_max"], row["melee_holders_mean"],
                               row["charger_holders_max"], row["charger_holders_mean"],
                               row["charger_cast_max"], row["support_holders_max"], row["support_holders_mean"], row["support_cast_max"],
                               row["max_concurrent"], row["attack_starts"], row["starts_per_alive_s"], row["longest_pack_gap_s"],
                               row["starvation_max_s"], row["first_attack_s"]])
    lines = ["## S4 역할 독점", "", "### 돌진자·지원 상한", ""]
    lines += table(["무리", "플레이어", "상한", "근접 보유 최대", "근접 보유 평균", "돌진 보유 최대", "돌진 보유 평균", "돌진 동시시전 최대", "지원 보유 최대", "지원 보유 평균", "지원 동시시전 최대",
                    "전체 동시시전 최대", "공격 시작", "시작/초", "최장 무공격s", "개체 최장 굶음s", "첫 공격s"], body_roles)
    lines += ["### 원거리 띠 바닥(궁수 표본은 6초 이후 8스텝마다, 시전 중 제외)", ""]
    lines += table(["무리", "플레이어", "바닥", "표본", "중앙cm", "10~90분위cm", "590미만 비율", "바닥 중앙cm", "바닥 변경수", "궁수 이동cm/s", "공격 시작", "시작/초", "최장 무공격s", "개체 최장 굶음s"],
                   body_floor)
    return lines


def tables_sensitivity(rows):
    lines = ["## 상수 민감도", ""]
    alarm = [row for row in rows if row["kind"] == "alarm"]
    lines += ["### 홉 지연(line4·clump12, 간격 650, 정지, 값은 [line4, clump12])", ""]
    lines += table(["지연", "퍼짐s", "1초 기상", "3초 기상", "6초 기상", "호출자", "미기상"],
                   [[row["value"], row["wake_spread_s"], row["awake_1s"], row["awake_3s"], row["awake_6s"], row["callers"], row["never_woken"]] for row in alarm])
    morale = [row for row in rows if row["kind"] == "morale"]
    lines += ["### 사기 상수(고블린 5·8·12 × 범위·꾸준·느림 처치 × 벽 1200(cornered 행만 벽 800), 사기 끔 기준선 대비)", ""]
    lines += table(["상수", "값", "실행", "공황 시작 합", "재공황 합", "공황 개체 비율 평균", "연쇄 최대", "후퇴시간 평균", "막힘종료 평균", "공격 비율 평균", "공격 비율 최소", "최장 무공격 최대s"],
                   [[row["param"], row["value"], row["runs"], row["panic_starts"], row["repanic"], row["unique_fraction_mean"], row["max_chain"],
                     row["retreat_time_mean"], row["cornered_mean"], row["starts_ratio_mean"], row["starts_ratio_min"], row["gap_max_s"]] for row in morale])
    return lines


def tables_determinism(rows):
    lines = ["## 결정론(같은 입력 2회 실행)", ""]
    lines += table(["사례", "해시 앞 16자", "일치"], [[row["case"], row["hash_first"][:16], row["identical"]] for row in rows])
    return lines


def s2_pass(kill, panic_packs, total):
    if kill.startswith("steady"):
        return panic_packs * 3 <= total
    if kill.startswith("slow"):
        return panic_packs == 0
    return panic_packs * 3 >= total * 2


def s2_summary(rows):
    groups = {}
    for row in rows:
        groups.setdefault((row["label"], row["kill"]), []).append(row)
    summary = []
    for (label, kill), group in groups.items():
        total = len(group)
        panic_packs = sum(1 for row in group if row["panic_starts"] > 0)
        group_ex = [row for row in group if row["pack"] != "goblin5+golem"]
        panic_ex = sum(1 for row in group_ex if row["panic_starts"] > 0)
        starts = sum(row["panic_starts"] for row in group)
        visible = sum(row["panic_visible_steps"] for row in group)
        ratios = [row["starts_ratio"] for row in group if row["starts_ratio"] is not None]
        summary.append({"label": label, "kill": kill, "total": total, "panic_packs": panic_packs, "passed": s2_pass(kill, panic_packs, total),
                        "panic_ex": panic_ex, "total_ex": len(group_ex), "passed_ex": s2_pass(kill, panic_ex, len(group_ex)),
                        "visible_mean": round(visible / starts / STEPS_PER_SECOND, 3) if starts else None,
                        "unique_mean": round(statistics.fmean(row["unique_panicked_fraction"] for row in group), 3),
                        "ratio_mean": round(statistics.fmean(ratios), 3) if ratios else None,
                        "gap_max": max((row["gap_s"] or 0.0) for row in group), "panic_starts": starts})
    return summary


def tables_v2(data):
    lines = ["## V2 규칙 재검증(명세 v2 규칙을 얹은 sim_pack, v1 규칙은 같은 코드에서 비교용으로 유지)", ""]
    lines += ANALYSIS_NOTES_V2
    lines += ["### V2-S1 경보(홉 지연 8~16 고정): 사망 경보·호출자 같은 종", ""]
    lines += table(["사례", "규칙", "기상", "기상 단 간격(스텝)", "호출자", "호출자 종", "Y 기상까지(스텝)"],
                   [[row["case"], row["rules"], row["woken"], row["gaps"], row["callers"], row["caller_kinds"], row["y_delay"]] for row in data["s1"]])
    total = data["pack_count"]
    summary = s2_summary(data["s2"])
    lines += [f"### V2-S2 합격선 대조(구성 {total}종 × 처치 모형 3, 벽 없음)", "",
              "합격선: 꾸준 = 공황 발생 무리 ≤ 1/3, 느림 = 0, 범위 = ≥ 2/3, 공황 가시 시간(후퇴 중이며 거리 ≤ 800) 평균 ≥ 0.8초.", ""]
    lines += table(["규칙/창", "처치", "공황 발생 무리", "합격선", "골렘 팩 제외", "제외 판정", "가시 시간 평균s", "가시 ≥ 0.8", "공황 개체 비율 평균", "공격 비율 평균", "최장 무공격 최대s", "공황 시작 합"],
                   [[item["label"], item["kill"], f"{item['panic_packs']}/{item['total']}", "통과" if item["passed"] else "실패",
                     f"{item['panic_ex']}/{item['total_ex']}", "통과" if item["passed_ex"] else "실패", item["visible_mean"],
                     "예" if (item["visible_mean"] or 0) >= 0.8 else "아니오", item["unique_mean"], item["ratio_mean"], item["gap_max"], item["panic_starts"]]
                    for item in summary])
    lines += ["### V2-S2 구성별 상세", ""]
    lines += table(["무리", "처치", "규칙/창", "처치수", "공황 시작", "공황 개체", "가시 시간s", "공격 시작(끔→켬)", "비율", "최장 무공격s(끔→켬)", "복귀 중앙s", "복귀 p90s", "재공황"],
                   [[row["pack"], row["kill"], row["label"], row["kills"], row["panic_starts"], f"{row['unique_panicked']}/{row['pack_size']}",
                     row["panic_visible_mean_s"], f"{row['baseline_starts']}→{row['attack_starts']}", row["starts_ratio"],
                     f"{cell(row['baseline_gap_s'])}→{cell(row['gap_s'])}", row["reapproach_median_s"], row["reapproach_p90_s"], row["repanic"]]
                    for row in data["s2"]])
    sweep_rows = data["sweep"]
    sweep = s2_summary(sweep_rows)
    labels = []
    for item in sweep:
        if item["label"] not in labels:
            labels.append(item["label"])
    lines += ["### V2-S2 합격선 탐색(창 × MinLoss × 손실 가중, 나머지 v2 규칙 고정)", ""]
    body = []
    for label in labels:
        by_kill = {item["kill"]: item for item in sweep if item["label"] == label}
        steady, slow, area = (by_kill[name] for name in ("steady1/1.5s", "slow1/4s", "area3/1s"))
        visible_starts = sum(item["panic_starts"] for item in by_kill.values())
        visible_mean = None
        if visible_starts:
            visible_mean = round(sum(row["panic_visible_steps"] for row in sweep_rows if row["label"] == label) / visible_starts / STEPS_PER_SECOND, 3)
        passed = steady["passed"] and slow["passed"] and area["passed"]
        passed_ex = steady["passed_ex"] and slow["passed_ex"] and area["passed_ex"]
        body.append([label, f"{steady['panic_packs']}/{total}", "통과" if steady["passed"] else "실패", f"{slow['panic_packs']}/{total}", "통과" if slow["passed"] else "실패",
                     f"{area['panic_packs']}/{total}", "통과" if area["passed"] else "실패", visible_mean, steady["ratio_mean"], steady["gap_max"],
                     "전부 통과" if passed else ("골렘 팩 제외 시 전부 통과" if passed_ex else "-")])
    lines += table(["조합", "꾸준 공황 무리", "판정", "느림 공황 무리", "판정", "범위 공황 무리", "판정", "가시 평균s", "꾸준 공격 비율 평균", "꾸준 최장 무공격 최대s", "종합"], body)
    lines += ["### V2 우두머리 공포 {64~128, 128~192}", ""]
    lines += table(["장면", "공포", "공황 시작", "사망 후 1초 안 공황", "사망→첫 공황 최소s", "가시 평균s", "복귀 중앙s", "복귀 최대s", "복귀 표본", "귀환", "공격 비율", "최장 무공격s(끔→켬)"],
                   [[row["scene"], row["fear"], row["panic_starts"], row["panickers_1s_after_death"], row["panic_since_death_min_s"], row["panic_visible_mean_s"],
                     row["reapproach_median_s"], row["reapproach_max_s"], row["reapproach_samples"], row["returns"], row["starts_ratio"],
                     f"{cell(row['baseline_gap_s'])}→{cell(row['gap_s'])}"] for row in data["leader"]])
    lines += ["### V2-S3 MD-43(체력 곡선 Logistic m -20 c 0.3, 고블린 거리 c 0.35·하이에나 c 0.15)", "",
              "건강 100% 행의 귀환은 플레이어 추격이 영역을 벗어나게 해서 생기는 기준선이다. MoveAway 비율 = 도망 스텝 / 교전 스텝.", ""]
    healthy = {(row["kind"], row["count"], row["motion"]): row for row in data["s3"] if row["variant"] == "건강 100%"}
    body = []
    for row in data["s3"]:
        base = healthy[(row["kind"], row["count"], row["motion"])]
        verdict = "-"
        if row["variant"].startswith("v2"):
            returns_ok = "귀환 0" if row["returns"] == 0 else ("귀환=기준선" if row["returns"] <= base["returns"] else "귀환 초과")
            verdict = f"{returns_ok}, MoveAway {'≤30%' if row['flee_engaged_fraction'] <= 0.30 else '>30%'}"
        body.append([row["kind"], row["count"], row["motion"], row["variant"], row["returns"], row["flee_starts"], row["flee_engaged_fraction"], row["longest_flee_s"],
                     row["flee_cornered"], row["attack_starts"], f"{round(row['attack_starts'] / base['attack_starts'], 2) if base['attack_starts'] else '-'}",
                     row["longest_pack_gap_s"], verdict])
    lines += table(["종", "수", "플레이어", "변형", "귀환", "도망 시작", "MoveAway 비율", "최장 도망s", "막힘 종료", "공격 시작", "건강 대비", "최장 무공격s", "판정(v2)"], body)
    lines += ["### V2-S4 원거리 띠 바닥(v2 식: 고리+몸+100+몸, 상한 min(사거리-100, 700))", "",
              "C3 합격선(골렘 4+궁수 2 정지): 중앙값 ≥ 640, 610 미만 표본 ≤ 10%.", ""]
    floor_rows = [row for row in data["s4"] if row["section"] == "floor"]
    lines += table(["무리", "플레이어", "바닥", "표본", "중앙cm", "10~90분위cm", "590미만", "610미만", "바닥 중앙cm", "바닥 변경수", "궁수 이동cm/s", "공격 시작", "시작/초", "개체 최장 굶음s"],
                   [[row["group"], row["motion"], row["mode"], row["archer_samples"], row["archer_median_cm"],
                     "/".join(str(int(value)) for value in (row["archer_deciles_cm"] or [])), row["archer_below_590"], row["archer_below_610"],
                     row["band_floor_median_cm"], row["floor_changes"], row["archer_path_cm_per_s"], row["attack_starts"], row["starts_per_alive_s"], row["starvation_max_s"]]
                    for row in floor_rows])
    lines += ["### V2-S4 돌진자·지원 상한(보복 중 +1)", ""]
    cap_rows = [row for row in data["s4"] if row["section"] == "cap"]
    off_rates = {(row["group"], row["motion"]): row["starts_per_alive_s"] for row in cap_rows if row["mode"] == "상한 끔"}
    lines += table(["무리", "플레이어", "상한", "근접 보유 최대", "돌진 보유 최대", "지원 보유 최대", "돌진 동시시전", "지원 동시시전", "전체 동시시전", "공격 시작", "시작/초", "끔 대비", "최장 무공격s", "개체 최장 굶음s"],
                   [[row["group"], row["motion"], row["mode"], row["melee_holders_max"], row["charger_holders_max"], row["support_holders_max"], row["charger_cast_max"],
                     row["support_cast_max"], row["max_concurrent"], row["attack_starts"], row["starts_per_alive_s"],
                     round(row["starts_per_alive_s"] / off_rates[(row["group"], row["motion"])], 2), row["longest_pack_gap_s"], row["starvation_max_s"]] for row in cap_rows])
    return lines


ANALYSIS_NOTES_V2 = [
    '### V2 요약(아래 표에서 확인한 것만)',
    '',
    '1. 창 192 대 512: 두 창 모두 꾸준 합격선(공황 무리 ≤ 1/3)에 실패한다(둘 다 6/7). 처치 간격 96스텝이 창보다 짧아 1.5초 간격 처치 두 번이 곧 손실 2가 되기 때문이다. 느림(4초당 1)은 창 192가 1/7(골렘 팩에서 골렘이 처치돼 우두머리 충격이 난 것, 골렘 팩 제외 0/6 통과), 창 512는 6/7로 실패. 범위(1초 3마리)는 둘 다 6/7 통과. 가시 시간은 모두 0.8초 이상(꾸준 1.53초, 느림 창192 2.97초, 범위 1.28초). v1은 꾸준 7/7·느림 7/7.',
    '2. 합격선 탐색: 창을 95스텝 이하로 줄이면(64·95 동일) MinLoss 2·가중 2에서 꾸준 0/7, 범위 6/7, 느림은 우두머리 충격 1/7 외 0, 가시 1.34초, 꾸준 공격 비율 1.00, 최장 무공격 1.72초 — 골렘 팩 제외 시 전부 통과. 창 128~160은 MinLoss 3이어야 꾸준을 통과하는데 MinLoss 3은 B2(0.5초 안 2마리 처치 → 공황)를 깬다. 권장: LossWindowSteps 64~95(1~1.5초 미만), MinLoss 2, 손실 가중 2. 가중 1은 범위 합격선(5/7)이 아슬아슬.',
    '3. 우두머리 공포 128~192: B1형(골렘+고블린 3, 3초 뒤 처치)에서 복귀(거리 400 이내) 최대 3.42초(64~128은 2.41초), 가시 2.42초(1.42초), 공격 비율 0.81(0.86). 골렘 2마리 장면은 첫 골렘 사망에서 충격이 없고 둘째 사망 직후 3마리가 공황한다(다른 우두머리 생존 규칙 작동). 128~192는 B1의 4초 한도에 0.58초 여유뿐이라 한도를 4.5초로 두거나 96~160을 고려.',
    "4. S3: 정지·카이팅 전 조합 귀환 0, MoveAway는 최대 25%로 합격. 추격은 건강 100%에서도 귀환이 생겨(고블린 3 2회, 하이에나 1 3회) '귀환 0'을 요구할 수 없다(v2 25%는 기준선과 같고 32%는 고블린 3에서 3회로 1회 초과). 체력 곡선은 약 32% 미만에서만 발동: 고블린은 40%에서 도망 0회. 하이에나 거리 c 0.15는 단독(정지 8회·카이팅 9회)에서만 발동하고 3마리·추격 단독에서는 0회(건강과 같은 행동) — 무리 안에서는 사실상 비활성(v1 c 0.3은 3마리 카이팅 25회).",
    '5. S4 바닥(v2 식 670): 골렘 4+궁수 2 정지 중앙 640.9(합격선 640, 610 미만 0%), 혼합(골렘 2+고블린 3)도 640.9, 고블린만이면 바닥 530이라 궁수 540 그대로. 그러나 궁수는 670이 아니라 641에서 멈춘다: Archer KeepDistance 곡선(중심 648cm, 폭 144)이 602~694cm에서 Idle(0.05)보다 낮아져 이동을 멈춘다. 합격선 640은 0.9cm 여유라 취약 — 합격선을 620으로 낮추거나 곡선 중심을 바닥에 맞출 것. 카이팅에서는 바닥이 유지되지 않는다(610 미만 54~62%, v1 바닥 55~61%).',
    '6. S4 상한(보복 +1): 정지에서는 보유·동시 시전 최대 1로 v1과 같고, 플레이어 타격이 있는 카이팅에서만 2까지 허용(동시 시전 2). 처리량은 상한 끔 대비 하이에나 4가 0.74(정지)·0.84(카이팅), 하이에나 6이 0.67·0.76, 궁수 3이 0.70. 하이에나 돌진자 지정 보류 권장이 수치로 뒷받침됨. C1·C2는 플레이어 무타격 장면 전제로 유지해야 한다.',
    '7. 경보: A4 사망 경보로 옆 동료가 14스텝 뒤 교전(v1은 0/1), 혼합 종 호출자는 v1 1명 → v2 2명(종별 1명). 홉 지연은 8~16 고정이며 A1 단 간격 14스텝.',
    '8. 결속 교전당 1회: 검토한 조합(창 64·95·192)에서 재공황 0. 공황 연속 시간 최대 2.97초(우두머리 공포 3초 이내), 후퇴 막힘 종료는 v2의 700cm 움츠림 때문에 0%.',
    '',
]


TABLE_WRITERS = {"s1": tables_s1, "s2": tables_s2, "s2v": tables_s2v, "s3": tables_s3, "s4": tables_s4, "sensitivity": tables_sensitivity, "determinism": tables_determinism, "v2": tables_v2}

ANALYSIS_NOTES = [
    '## 요약(전체 60초 모드 수치 기준, 아래 표에서 확인한 것만)',
    '',
    '### 규칙 결함 후보와 권장값',
    '',
    "1. 손실 카운터가 사라지지 않음(명세 2.4 1단계): RecentLossCount는 '직전 손실에서 512스텝 이내면 +1'이라 8초 이내 간격이면 끝없이 쌓인다. 느린 처치(4초당 1)에서 고블린 20은 15마리 처치 시점까지 공황 시작 20회·공황 개체 13마리·재공황 7회, 윈도 방식(최근 512스텝 안 사망 수)이면 6회·6마리·재공황 0. 권장: 최근 512스텝 윈도 카운트(또는 감쇠).",
    '2. 결속 면역 640스텝은 짧다: 고블린 5·8·12 × 범위·꾸준·느림 처치 아홉 실행에서 재공황이 640이면 4건(윈도 방식 2건), 320이면 9건, 1280이면 1건. 윈도+960·윈도+1280은 0건. 권장: 윈도 카운트와 함께 RallyImmuneSteps 960(총 면역 128+960스텝, 약 17초).',
    '3. Fear 128~192는 나쁨: 막힘 종료 44%(벽 1200 아홉 실행 평균), 공격 시작 비율 평균 0.70(최저 0.50), 무공격 최장 5.7초. 32~64는 0.81·2.9초지만 후퇴가 0.5~1초라 연출이 약하다. 64~128(명세)은 0.77·5.05초로 허용 범위.',
    "4. 공황의 비용 대부분은 후퇴(1~2초)가 아니라 복귀: 공황 시작부터 거리 400cm 복귀까지 중앙값이 대부분 2.3~3.2초(작은 무리 p90 최대 3.6초). 고블린 5에서 2마리를 1.5초 간격으로 잃으면 남은 3마리가 동시에 공황 → 무리 무공격 최장 1.09 → 5.05초(공격 시작 0.58배). 고블린 12 이상은 복귀자가 대기 고리(반경 550 바깥 고리)로 가서 '거리 400 이내' 판정이 최대 9.1초까지 늘어나므로, 명세 시험 B1·B2의 4초 한도는 작은 무리에만 유효.",
    '5. 전염 항은 기본 배치에서 죽은 항: S2 58행과 변형 36행 모두 연쇄 길이 1·전염 비율 0. 무리가 1200cm 안에 모여 같은 손실을 보고 같은 지지를 세어 한꺼번에 공황한다. 흩어진 무리(스폰 1200~1450, 1초 뒤 처치)에서만 연쇄 2·전염 비율 0.44~0.50이 나타났고, 사망을 못 본 개체도 공황 아군만으로 판정하게 풀면(ally-gate) 연쇄 4·전염 비율 0.67~0.77까지 올라간다. 연쇄 붕괴는 명세 게이트(MinLosses 이상 손실 목격)가 막고 있으므로 이 게이트를 풀지 말 것.',
    '6. 무한 공황·루프 없음: 공황 연속 시간 최대 2.41초, 마지막 사망 뒤 새 공황 시작 최대 0.86초, 공황 대기 만료 취소 0건(고블린 시전 58스텝 < 64라 구조상 발생 못 함), 후퇴 길이 최대 2.0초.',
    '7. 벽 반경 1200은 막힘을 못 만듦(후퇴가 최대 840cm, 대부분 700 이하에서 시작): 막힘 종료 0%. 반경 800은 고블린 5~20에서 33~62%, 하이에나 6에서 75%, 골렘 섞임·3마리에서 0%. 편심 벽(중심 -600,0)은 0~25%. Cornered 16/24/48(벽 800): 막힘 종료 51%/32%/11%, 공격 시작 비율은 모두 0.77이라 24 유지. 벽이 없을 때 막힘 종료 오탐은 0건.',
    '8. MinLosses 3은 5·8·12 무리에서 차이가 거의 없다(공황 시작 49→46, 공격 비율 0.77 동일). 4마리 이하 무리는 3이면 공황하지 않을 것으로 보이나(분석, 미측정) 2 유지.',
    '9. 경보: 신 규칙은 지연만이 아니라 도달 범위가 2배(구 1단 900 → 신 최대 1800cm). 깨어난 수가 일렬4(650) 2→3/4, 덩어리12(500/650/800) 8→12·6→10·6→8, 두 덩어리6+6(650) 4→7/12. 호출자는 모든 배치에서 무리당 최대 1, 3단 이상 0. A1 배치(700 간격)는 단 간격 14스텝×2로 명세 8~16에 맞음. 홉 지연 4~8/8~16/16~32는 총 퍼짐 0.19/0.41/0.84초이고 도달 비율은 같다. 파문을 보이게 하려면 16~32 권장.',
    '10. MD-43: 구 규칙은 6개 조합(1·3마리 × 정지·추격·카이팅) 전부 공격 시작 0, 귀환 3~12회/60초(정지 3·11, 추격 5·7, 카이팅 5·12), 연속 도망 6~9초. 신 규칙은 정지·카이팅에서 귀환 0, 도망 시간 23~25%(=1.5/6), 최장 도망 1.5초. 공격 시작은 건강 100% 대비 정지 0.58배·카이팅 0.32~0.50배(무공격 최장 정지·카이팅 4.2~6.3초). 추격에서 막힘 종료는 필수: 끄면 귀환이 재발(1마리 2회, 3마리 4회, 신 규칙은 0·2회). 거리 관문을 끄면 최대 거리가 1157→1393(1마리 정지)·1157→1640(3마리 정지)·1466→2090(3마리 추격)로 늘고 추격 3마리 귀환 2→3.',
    '11. 역할 독점: 상한은 정확히 지켜짐(돌진자·지원 보유 최대 1, 동시 시전 최대 1). 대가는 처리량: 하이에나 4 정지 1.28→0.95/초(-26%), 6은 1.40→0.93(-34%), 궁수 3은 1.63→1.15(-29%). 하이에나 6의 무리 무공격 최장 1.09→1.80초, 개체 굶음 최장 6.6→7.3초. 상한이 예약과 확정을 모두 세어 한 마리가 끝나야 다음이 예약하므로 인계 때 접근 시간이 공백이 되는 것으로 보인다(추정).',
    '12. 원거리 띠 바닥: 정지에서 골렘 4+궁수 2는 중앙 540→640(590 미만 100%→0%), 고블린 4+궁수 2는 바닥 500이라 궁수 불변(명세 예와 일치), 골렘 2+고블린 3 혼합도 640 유지·바닥 변경 0회. 플레이어가 원을 그리며 카이팅하면 바닥이 유지되지 않는다(590 미만 43~50%, 궁수 이동 140cm/s, 바닥 변경 2~4회).',
    '',
]


def write_tables(experiments, path, quick, elapsed):
    lines = ["# sim_pack 결과(무리 행동 명세 v1 검증)", "",
             f"모드: {'빠른(20초)' if quick else '전체(60초)'}, 64스텝/초, 소요 {elapsed:.0f}초. 기반: sim_tactics.SimulationV2 + 출하 상수(F_final_B). "
             "플레이어 처치는 가장 가까운 개체를 지정 간격으로 제거하는 추상 모형.", ""]
    lines += ANALYSIS_NOTES
    for name in TABLE_WRITERS:
        if name in experiments:
            lines += TABLE_WRITERS[name](experiments[name])
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--only", default="")
    parser.add_argument("--out-dir", default=HERE)
    arguments = parser.parse_args()
    selected = [name for name in arguments.only.split(",") if name] or list(EXPERIMENTS)
    os.makedirs(arguments.out_dir, exist_ok=True)
    metrics_path = os.path.join(arguments.out_dir, "sim_pack_metrics.json")
    tables_path = os.path.join(arguments.out_dir, "sim_pack_tables.md")
    experiments = {}
    if os.path.exists(metrics_path) and arguments.only:
        with open(metrics_path, "r", encoding="utf-8") as handle:
            experiments = json.load(handle).get("experiments", {})
    started_all = time.perf_counter()
    for name in selected:
        started = time.perf_counter()
        experiments[name] = EXPERIMENTS[name](arguments.quick)
        print(f"== {name} {len(experiments[name])}행 ({time.perf_counter() - started:.1f}s)")
        sys.stdout.flush()
    elapsed = time.perf_counter() - started_all
    output = {"quick": arguments.quick, "pack_defaults": {key: list(value) if isinstance(value, tuple) else value for key, value in PACK_DEFAULTS.items()},
              "tactics_base": TACTICS_BASE, "golem_species": SPECIES["golem"], "experiments": experiments}
    with open(metrics_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(output, handle, ensure_ascii=False, indent=1)
    write_tables(experiments, tables_path, arguments.quick, elapsed)
    determinism = experiments.get("determinism")
    if determinism is not None:
        print("결정론:", "모두 일치" if all(row["identical"] for row in determinism) else "불일치 있음")
    print(f"written {metrics_path}\nwritten {tables_path}")


if __name__ == "__main__":
    main()

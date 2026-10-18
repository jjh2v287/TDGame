"""
실행 환경: Python 3.10 이상 표준 라이브러리만, 언리얼·numpy 불필요.
실행 명령: python Tools/scratch/20261018_claude_m314_tactics/sim_tactics.py [--quick] [--only main,determinism,...]
출력: 같은 폴더 metrics.json(시나리오·정책·변형별 지표와 결정론 해시), 표준 출력에 요약 표.
상태: 실험(scratch). M3-14·M3-28 명세 v1 전술층 상수 검증용 결정론 2D 모형, 프로젝트 소스는 바꾸지 않음.
"""
import argparse
import hashlib
import json
import math
import os
import statistics
import sys
import time

STEPS_PER_SECOND = 64
PLAYER_RADIUS = 40.0
MONSTER_RADIUS = 40.0
ENGAGE_RADIUS = 1500.0
PLAYER_MAX_UNITS = 10000
LEGACY_MAX_CONCURRENT = 3
PHASE_NAMES = ("BuildUp", "SustainPeak", "PeakFade", "Relax")
IDLE, MOVE, CAST = 0, 1, 2

SPEC_CONSTANTS = {
    "token_pass_period": 8,
    "ring_pass_period": 32,
    "max_tokens": 8,
    "token_cost": 4,
    "reservation_expiry": 96,
    "regrant_cooldown": 48,
    "attack_start_spacing": 12,
    "opening_grace": 48,
    "grant_radius": 1600.0,
    "ranged_ability_range": 450.0,
    "wait_ring_base_radius": 380.0,
    "wait_ring_spacing": 200.0,
    "wait_sector_counts": (12, 16),
    "inner_sector_count": 8,
    "ranged_sector_count": 12,
    "ranged_sector_offset": 15.0,
    "slot_keep_bonus": 20.0,
    "budget_lower_pass_count": 2,
    "wait_stage_cap": 8,
    "heads_rule": "spec",
    "wait_ring_requires_queue": False,
    "director_enabled": True,
    "phase_multipliers": (100, 100, 50, 25),
    "intensity_peak": 700,
    "sustain_steps": 256,
    "fade_floor": 250,
    "fade_max_steps": 0,
    "relax_steps": 384,
    "calm_phase_decay": False,
    "relax_intensity_reset": -1,
    "expiry_sets_regrant_cooldown": False,
    "melee_grant_radius": 0.0,
    "committed_keeps_wait_slot": False,
    "wait_keep_max_error": 0.0,
    "proximity_radius": 600.0,
    "proximity_per_monster": 4,
    "proximity_cap": 40,
    "quiet_steps": 128,
    "quiet_decay": 30,
    "big_damage_ratio": 0.30,
    "freeze_steps": 64,
    "mercy_health_ratio": 0.25,
    "mercy_percent": 75,
    "wait_slow_scale": 0.6,
    "holder_push_share": 0.25,
}

SPECIES = {
    "goblin": {"speed": 420.0, "think_hz": 10, "min_hold": 0.4, "switch_ratio": 1.15, "range": 160.0,
               "cooldown": 1.2, "cast": 0.9, "stop_at": 140.0, "hit_units": 400},
    "hyena": {"speed": 560.0, "think_hz": 12, "min_hold": 0.3, "switch_ratio": 1.15, "range": 150.0,
              "cooldown": 2.0, "cast": 0.7, "stop_at": 115.0, "band": (230.0, 360.0), "band_scale": 0.6,
              "hit_units": 400},
    "archer": {"speed": 380.0, "think_hz": 8, "min_hold": 0.6, "switch_ratio": 1.2, "range": 900.0,
               "cooldown": 1.6, "cast": 0.8, "band": (500.0, 800.0), "band_scale": 1.0, "hit_units": 300},
    "ogre": {"speed": 340.0, "think_hz": 12, "min_hold": 0.8, "switch_ratio": 1.1, "range": 220.0,
             "cooldown": 2.5, "cast": 1.2, "stop_at": 200.0, "hit_units": 1000, "max_health": 4000},
}


def round_half_up(value):
    return int(math.floor(value + 0.5))


def seconds_to_steps(seconds):
    return max(0, round_half_up(seconds * STEPS_PER_SECOND))


def clamp(value, low, high):
    return low if value < low else high if value > high else value


def logistic(normalized, slope, center):
    x = clamp(normalized, 0.0, 1.0)
    return clamp(1.0 / (1.0 + math.exp(-slope * (x - center))), 0.0, 1.0)


def gaussian(normalized, width, center):
    x = clamp(normalized, 0.0, 1.0)
    offset = x - center
    return clamp(math.exp(-(offset * offset) / (2.0 * width * width)), 0.0, 1.0)


def wrap_degrees(angle):
    wrapped = math.fmod(angle, 360.0)
    return wrapped + 360.0 if wrapped < 0.0 else wrapped


def angular_difference(first, second):
    difference = abs(wrap_degrees(first) - wrap_degrees(second))
    return 360.0 - difference if difference > 180.0 else difference


def percentile(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    rank = max(0, min(len(ordered) - 1, int(math.ceil(fraction * len(ordered))) - 1))
    return ordered[rank]


def heads_for_engaged(engaged_count, constants):
    if engaged_count <= 0:
        return 0
    heads = clamp((engaged_count + 1) // 3, 1, constants["max_tokens"])
    if constants["heads_rule"] == "floor2":
        heads = max(heads, min(engaged_count, 2))
    return heads


class Monster:
    __slots__ = (
        "sid", "kind", "role", "x", "y", "speed", "think_period", "min_hold_steps", "switch_ratio",
        "ability_range", "cooldown_steps", "cast_steps", "hit_units", "stop_at", "band", "band_scale",
        "state", "cast_remaining", "cooldown_remaining", "action", "hold_remaining", "next_think_step",
        "force_think", "engaged", "engage_step", "token", "grant_step", "regrant_ready_step",
        "last_attack_end_step", "wait_slot", "remembered_wait_slot", "inner_slot", "remembered_inner_slot",
        "ranged_slot", "remembered_ranged_slot", "intent", "cast_starts", "wait_reassignments",
        "expired_at_step", "melee_reach", "max_reach", "wait_ring_radius", "inner_radius", "last_damaged_step",
        "on_deck", "slot_key", "slot_anchor_step", "slot_anchor_cm", "slot_best_cm",
    )

    def __init__(self, sid, kind, x, y, goblin_cast_seconds):
        species = SPECIES[kind]
        self.sid = sid
        self.kind = kind
        self.x = x
        self.y = y
        self.speed = species["speed"]
        self.think_period = max(2, round_half_up(STEPS_PER_SECOND / species["think_hz"]))
        self.min_hold_steps = seconds_to_steps(species["min_hold"])
        self.switch_ratio = species["switch_ratio"]
        self.ability_range = species["range"]
        self.role = "ranged" if self.ability_range >= SPEC_CONSTANTS["ranged_ability_range"] else "melee"
        self.cooldown_steps = seconds_to_steps(species["cooldown"])
        cast_seconds = goblin_cast_seconds if kind == "goblin" else species["cast"]
        self.cast_steps = max(1, seconds_to_steps(cast_seconds))
        self.hit_units = species["hit_units"]
        self.stop_at = species.get("stop_at", 0.0)
        self.band = species.get("band")
        self.band_scale = species.get("band_scale", 1.0)
        self.state = IDLE
        self.cast_remaining = 0
        self.cooldown_remaining = 0
        self.action = None
        self.hold_remaining = 0
        self.next_think_step = 1 + sid % self.think_period
        self.force_think = False
        self.engaged = False
        self.engage_step = None
        self.token = "-"
        self.grant_step = 0
        self.regrant_ready_step = 0
        self.last_attack_end_step = 0
        self.wait_slot = None
        self.remembered_wait_slot = None
        self.inner_slot = None
        self.remembered_inner_slot = None
        self.ranged_slot = None
        self.remembered_ranged_slot = None
        self.intent = None
        self.cast_starts = []
        self.wait_reassignments = 0
        self.expired_at_step = None
        self.melee_reach = 0.0
        self.max_reach = self.ability_range
        self.wait_ring_radius = 0.0
        self.inner_radius = self.stop_at
        self.last_damaged_step = 0
        self.on_deck = False
        self.slot_key = None
        self.slot_anchor_step = 0
        self.slot_anchor_cm = 0.0
        self.slot_best_cm = 0.0


class TargetRecord:
    def __init__(self, step, constants):
        self.created_step = step
        self.grace_until = step + constants["opening_grace"]
        self.freeze_until = 0
        self.last_attack_start = -100000
        self.budget = 0
        self.lower_count = 0
        self.intensity = 0
        self.phase = 0
        self.phase_entered = step
        self.phase_until = 0
        self.last_damage_step = step
        self.window_damage = 0
        self.has_queue = True


class Simulation:
    def __init__(self, config):
        self.config = config
        self.policy = config["policy"]
        self.is_spec = self.policy == "SPEC"
        self.constants = dict(SPEC_CONSTANTS)
        self.constants.update(config.get("overrides", {}))
        self.total_steps = int(config["seconds"] * STEPS_PER_SECOND)
        self.motion = config["motion"]
        self.player_x, self.player_y = self.player_position(0)
        self.player_hp = PLAYER_MAX_UNITS
        self.pending_damage = 0
        self.monsters = self.spawn_monsters()
        self.record = None
        self.casting_count = 0
        self.hasher = hashlib.sha256()
        self.events = {
            "hits": 0, "misses": 0, "player_resets": 0, "grants": 0, "expiries_time": 0,
            "expiries_distance": 0, "expiry_regranted_same": 0, "revocations_budget": 0, "slot_conflicts": 0,
            "tokenless_casts": 0, "freeze_triggers": 0, "relax_entries": 0, "double_slot_samples": 0,
        }
        self.concurrent_samples = []
        self.cast_log = []
        self.non_casting_distances = []
        self.waiting_distances = []
        self.min_gap_all = []
        self.min_gap_band = []
        self.goal_errors = []
        self.lag_samples = 0
        self.lag_count = 0
        self.phase_steps = [0, 0, 0, 0]
        self.phase_damage = [0, 0, 0, 0]
        self.phase_timeline = []
        self.commit_latencies = []
        self.reserved_steps = 0
        self.committed_steps = 0
        self.budget_samples = []
        self.intensity_samples = []
        self.legacy_damage = 0

    def player_position(self, step):
        seconds = step / STEPS_PER_SECOND
        if self.motion == "kiting":
            angle = 0.5 * seconds
            return 600.0 * math.cos(angle), 600.0 * math.sin(angle)
        if self.motion == "retreating":
            return 250.0 * seconds, 0.0
        return 0.0, 0.0

    def spawn_monsters(self):
        low, high = self.config.get("spawn_band", (700.0, 1100.0))
        start_x, start_y = self.player_x, self.player_y
        monsters = []
        for sid, kind in enumerate(self.config["composition"]):
            angle = math.radians(wrap_degrees(sid * 137.50776))
            fraction = math.fmod(sid * 0.6180339887, 1.0)
            radius = low + (high - low) * fraction
            monsters.append(Monster(sid, kind, start_x + radius * math.cos(angle), start_y + radius * math.sin(angle),
                                    self.config.get("goblin_cast", 0.9)))
        return monsters

    def distance_to_player(self, monster):
        return math.hypot(monster.x - self.player_x, monster.y - self.player_y)

    def bearing_from_player(self, monster):
        return wrap_degrees(math.degrees(math.atan2(monster.y - self.player_y, monster.x - self.player_x)))

    def run(self):
        for step in range(1, self.total_steps + 1):
            self.player_x, self.player_y = self.player_position(step)
            for monster in self.monsters:
                self.tick_counters(monster)
            self.update_engagement(step)
            if self.is_spec:
                self.update_tactics(step)
            else:
                self.casting_count = sum(1 for monster in self.monsters if monster.state == CAST)
            for monster in self.monsters:
                was_casting = monster.state == CAST
                self.think_and_act(monster, step)
                if not was_casting and monster.state == CAST:
                    self.on_cast_started(monster, step)
            self.integrate_and_separate()
            self.record_step(step)
        return self.compile_metrics()

    def tick_counters(self, monster):
        if monster.hold_remaining > 0:
            monster.hold_remaining -= 1
        if monster.cooldown_remaining > 0:
            monster.cooldown_remaining -= 1

    def update_engagement(self, step):
        for monster in self.monsters:
            if monster.engaged:
                continue
            if self.distance_to_player(monster) <= ENGAGE_RADIUS:
                monster.engaged = True
                monster.engage_step = step
                monster.force_think = True

    def has_attack_token(self, monster, step):
        if not self.is_spec:
            others = self.casting_count - (1 if monster.state == CAST else 0)
            return others < LEGACY_MAX_CONCURRENT
        record = self.record
        if record is None:
            return False
        if monster.token == "C":
            return True
        if monster.token != "R":
            return False
        spacing_ok = step - record.last_attack_start >= self.constants["attack_start_spacing"]
        return spacing_ok and step >= record.grace_until and step >= record.freeze_until

    def score_actions(self, monster, distance, is_ready):
        in_range = distance <= monster.ability_range
        if monster.kind == "ogre":
            approach = logistic(distance / 1600.0, 12.0, 0.14)
            smash = 2.0 * logistic(distance / 1600.0, -14.0, 0.15) if is_ready and in_range else 0.0
            return [("approach", approach), ("cast", smash), ("idle", 0.05)]
        if monster.kind == "goblin":
            approach = logistic(distance / 1200.0, 12.0, 0.15)
            slash = 2.0 * logistic(distance / 1200.0, -12.0, 0.14) if is_ready and in_range else 0.0
            return [("approach", approach), ("cast", slash), ("idle", 0.05)]
        if monster.kind == "hyena":
            approach = logistic(distance / 1500.0, 14.0, 0.12)
            bite = 2.5 * logistic(distance / 1500.0, -40.0, 0.1) if is_ready and in_range else 0.0
            circle = 0.0 if is_ready else 1.3 * logistic(distance / 1500.0, -20.0, 0.26)
            return [("approach", approach), ("cast", bite), ("circle", circle), ("idle", 0.05)]
        keep = 1.0 - gaussian(distance / 1200.0, 0.12, 0.54)
        shoot = 2.0 * gaussian(distance / 1200.0, 0.2, 0.54) if is_ready and in_range else 0.0
        return [("keep", keep), ("cast", shoot), ("idle", 0.05)]

    def think_and_act(self, monster, step):
        monster.intent = None
        if not monster.engaged:
            return
        is_due = step >= monster.next_think_step or monster.force_think
        if is_due:
            monster.next_think_step = step + monster.think_period
            monster.force_think = False
            if monster.state in (IDLE, MOVE):
                self.decide(monster, step)
        self.step_state(monster, step)

    def decide(self, monster, step):
        has_token = self.has_attack_token(monster, step)
        is_ready = has_token and monster.cooldown_remaining == 0
        scores = self.score_actions(monster, self.distance_to_player(monster), is_ready)
        best_name, best_score = scores[0]
        for name, score in scores[1:]:
            if score > best_score:
                best_name, best_score = name, score
        current_score = 0.0
        for name, score in scores:
            if name == monster.action:
                current_score = score
        if monster.action is not None and current_score > 0.0:
            is_weak = best_score <= current_score * monster.switch_ratio
            if monster.hold_remaining > 0 or best_name == monster.action or is_weak:
                self.start_action(monster, monster.action)
                return
        if best_name != monster.action:
            monster.hold_remaining = monster.min_hold_steps
        monster.action = best_name
        self.start_action(monster, best_name)

    def start_action(self, monster, name):
        if name == "cast":
            monster.state = CAST
            monster.cast_remaining = monster.cast_steps
            monster.cooldown_remaining = monster.cooldown_steps
        elif name == "idle":
            monster.state = IDLE
        else:
            monster.state = MOVE

    def step_state(self, monster, step):
        if monster.state == CAST:
            monster.cast_remaining -= 1
            if monster.cast_remaining <= 0:
                self.resolve_hit(monster)
                monster.state = IDLE
                monster.force_think = True
            return
        if monster.state != MOVE:
            return
        if self.is_spec:
            monster.intent = self.compute_tactics_intent(monster)
        else:
            monster.intent = self.compute_legacy_intent(monster)

    def resolve_hit(self, monster):
        if self.distance_to_player(monster) > monster.ability_range + 40.0:
            self.events["misses"] += 1
            return
        self.events["hits"] += 1
        units = monster.hit_units
        self.pending_damage += units
        self.legacy_damage += units
        if self.is_spec and self.record is not None:
            self.phase_damage[self.record.phase] += units
        self.player_hp -= units
        if self.player_hp <= 0:
            self.events["player_resets"] += 1
            self.player_hp = PLAYER_MAX_UNITS

    def direction_to(self, monster, goal_x, goal_y, scale, max_distance):
        dx = goal_x - monster.x
        dy = goal_y - monster.y
        length = math.hypot(dx, dy)
        if length < 1e-6 or max_distance <= 0.0:
            return None
        return (dx / length, dy / length, scale, max_distance)

    def compute_legacy_intent(self, monster):
        distance = self.distance_to_player(monster)
        if monster.action == "approach":
            if distance <= monster.stop_at:
                return None
            return self.direction_to(monster, self.player_x, self.player_y, 1.0, distance - monster.stop_at)
        low, high = monster.band
        if distance < low:
            away_x = monster.x + (monster.x - self.player_x)
            away_y = monster.y + (monster.y - self.player_y)
            return self.direction_to(monster, away_x, away_y, monster.band_scale, low - distance)
        if distance > high:
            return self.direction_to(monster, self.player_x, self.player_y, monster.band_scale, distance - high)
        return None

    def wait_radius(self, ring_index):
        return self.constants["wait_ring_base_radius"] + MONSTER_RADIUS + ring_index * self.constants["wait_ring_spacing"]

    def wait_goal_point(self, monster):
        ring_index, sector_index = monster.wait_slot
        radius = self.wait_radius(ring_index)
        direction_x, direction_y = self.sector_direction(self.constants["wait_sector_counts"][ring_index], sector_index, 0.0)
        return self.player_x + direction_x * radius, self.player_y + direction_y * radius

    def sector_direction(self, count, index, offset):
        angle = math.radians(index * 360.0 / count + offset)
        return math.cos(angle), math.sin(angle)

    def compute_wait_intent(self, monster, distance, scale_override):
        ring_index, sector_index = monster.wait_slot
        radius = self.wait_radius(ring_index)
        direction_x, direction_y = self.sector_direction(self.constants["wait_sector_counts"][ring_index], sector_index, 0.0)
        goal_x = self.player_x + direction_x * radius
        goal_y = self.player_y + direction_y * radius
        goal_distance = math.hypot(goal_x - monster.x, goal_y - monster.y)
        if goal_distance <= 40.0:
            return None
        scale = scale_override
        if scale is None:
            scale = self.constants["wait_slow_scale"] if distance <= radius * 1.4 else 1.0
        return self.direction_to(monster, goal_x, goal_y, scale, goal_distance)

    def compute_tactics_intent(self, monster):
        distance = self.distance_to_player(monster)
        holds_token = monster.token in ("R", "C")
        record = self.record
        uses_rings = record is not None and (record.has_queue or not self.constants["wait_ring_requires_queue"])
        if monster.action == "approach":
            if holds_token and monster.inner_slot is not None:
                if distance <= monster.stop_at:
                    return None
                direction_x, direction_y = self.sector_direction(self.constants["inner_sector_count"], monster.inner_slot, 0.0)
                goal_x = self.player_x + direction_x * monster.stop_at
                goal_y = self.player_y + direction_y * monster.stop_at
                return self.direction_to(monster, goal_x, goal_y, 1.0, math.hypot(goal_x - monster.x, goal_y - monster.y))
            if not holds_token and uses_rings and monster.wait_slot is not None:
                return self.compute_wait_intent(monster, distance, None)
            if not holds_token and uses_rings:
                stop_distance = self.wait_radius(0) + 200.0
                if distance <= stop_distance:
                    return None
                return self.direction_to(monster, self.player_x, self.player_y, 1.0, distance - stop_distance)
            return self.compute_legacy_intent(monster)
        if monster.role == "melee":
            if uses_rings and monster.wait_slot is not None:
                return self.compute_wait_intent(monster, distance, monster.band_scale)
            return self.compute_legacy_intent(monster)
        return self.compute_ranged_band_intent(monster, distance)

    def compute_ranged_band_intent(self, monster, distance):
        low, high = monster.band
        if distance < low or distance > high or monster.ranged_slot is None:
            return self.compute_legacy_intent(monster)
        count = self.constants["ranged_sector_count"]
        slot_angle = monster.ranged_slot * 360.0 / count + self.constants["ranged_sector_offset"]
        bearing = self.bearing_from_player(monster)
        signed_error = wrap_degrees(slot_angle - bearing)
        if signed_error > 180.0:
            signed_error -= 360.0
        if abs(signed_error) <= 180.0 / count:
            return None
        radial_x = (monster.x - self.player_x) / max(distance, 1e-6)
        radial_y = (monster.y - self.player_y) / max(distance, 1e-6)
        turn = 1.0 if signed_error > 0.0 else -1.0
        tangent_x, tangent_y = -radial_y * turn, radial_x * turn
        correction = clamp(((low + high) * 0.5 - distance) / 200.0, -1.0, 1.0)
        move_x = tangent_x + radial_x * correction
        move_y = tangent_y + radial_y * correction
        length = math.hypot(move_x, move_y)
        return (move_x / length, move_y / length, monster.band_scale, distance * math.radians(abs(signed_error)))

    def on_cast_started(self, monster, step):
        monster.cast_starts.append(step)
        self.cast_log.append((step, monster.sid))
        if not self.is_spec:
            self.casting_count += 1
            return
        if monster.token == "R":
            monster.token = "C"
            self.record.last_attack_start = step
            self.commit_latencies.append(step - monster.grant_step)
        elif monster.token != "C":
            self.events["tokenless_casts"] += 1

    def candidate_key(self, monster, step):
        regrant_late = 0 if step >= monster.regrant_ready_step else 1
        wait_stage = min((step - monster.last_attack_end_step) // 32, self.constants["wait_stage_cap"])
        dx = monster.x - self.player_x
        dy = monster.y - self.player_y
        return (regrant_late, -wait_stage, int(dx * dx + dy * dy), monster.sid)

    def release_token(self, monster, step):
        monster.token = "-"
        monster.last_attack_end_step = step
        monster.regrant_ready_step = step + self.constants["regrant_cooldown"]
        monster.inner_slot = None

    def drop_reservation(self, monster, step, reason):
        monster.token = "-"
        monster.inner_slot = None
        if reason:
            self.events[reason] += 1
        if reason in ("expiries_time", "expiries_distance"):
            monster.expired_at_step = step
            if self.constants["expiry_sets_regrant_cooldown"]:
                monster.regrant_ready_step = step + self.constants["regrant_cooldown"]

    def update_tactics(self, step):
        constants = self.constants
        engaged = [monster for monster in self.monsters if monster.engaged]
        if engaged and self.record is None:
            self.record = TargetRecord(step, constants)
            self.phase_timeline.append((round(step / STEPS_PER_SECOND, 2), PHASE_NAMES[0], 0))
        record = self.record
        if record is None:
            return
        for monster in self.monsters:
            if monster.token == "C" and monster.state != CAST:
                self.release_token(monster, step)
            elif monster.token == "R":
                if not monster.engaged:
                    self.drop_reservation(monster, step, None)
                elif step - monster.grant_step >= constants["reservation_expiry"]:
                    self.drop_reservation(monster, step, "expiries_time")
                elif self.distance_to_player(monster) > constants["grant_radius"] * 1.25:
                    self.drop_reservation(monster, step, "expiries_distance")
        self.update_director_damage(step)
        if step % constants["ring_pass_period"] == 0:
            self.update_director_pacing(step, engaged)
            self.assign_wait_ring(step, engaged)
            self.assign_ranged_sectors(engaged)
        if step % constants["token_pass_period"] == 0:
            self.update_budget(step, engaged)
            self.revoke_over_budget(step, engaged)
            self.grant_tokens(step, engaged)
            self.assign_inner_sectors(engaged)

    def update_director_damage(self, step):
        record = self.record
        if self.pending_damage <= 0:
            return
        if self.constants["director_enabled"]:
            record.intensity = min(1000, record.intensity + self.pending_damage * 2000 // PLAYER_MAX_UNITS)
        record.last_damage_step = step
        record.window_damage += self.pending_damage
        self.pending_damage = 0

    def enter_phase(self, step, phase):
        record = self.record
        record.phase = phase
        record.phase_entered = step
        if phase == 1:
            record.phase_until = step + self.constants["sustain_steps"]
        elif phase == 3:
            record.phase_until = step + self.constants["relax_steps"]
            self.events["relax_entries"] += 1
            if self.constants["relax_intensity_reset"] >= 0:
                record.intensity = min(record.intensity, self.constants["relax_intensity_reset"])
        self.phase_timeline.append((round(step / STEPS_PER_SECOND, 2), PHASE_NAMES[phase], record.intensity))

    def update_director_pacing(self, step, engaged):
        constants = self.constants
        record = self.record
        if record.window_damage >= constants["big_damage_ratio"] * PLAYER_MAX_UNITS:
            record.freeze_until = step + constants["freeze_steps"]
            self.events["freeze_triggers"] += 1
        record.window_damage = 0
        if not constants["director_enabled"]:
            return
        is_calm_phase = record.phase in (2, 3)
        if not (constants["calm_phase_decay"] and is_calm_phase):
            near = sum(1 for monster in engaged if self.distance_to_player(monster) <= constants["proximity_radius"])
            record.intensity = min(1000, record.intensity + min(near * constants["proximity_per_monster"], constants["proximity_cap"]))
        is_quiet = step - record.last_damage_step > constants["quiet_steps"]
        if is_quiet or (constants["calm_phase_decay"] and is_calm_phase):
            record.intensity = max(0, record.intensity - constants["quiet_decay"])
        if record.phase == 0 and record.intensity >= constants["intensity_peak"]:
            self.enter_phase(step, 1)
        elif record.phase == 1 and step >= record.phase_until:
            self.enter_phase(step, 2)
        elif record.phase == 2:
            is_faded = record.intensity <= constants["fade_floor"]
            is_fade_timed_out = constants["fade_max_steps"] > 0 and step - record.phase_entered >= constants["fade_max_steps"]
            if is_faded or is_fade_timed_out:
                self.enter_phase(step, 3)
        elif record.phase == 3 and step >= record.phase_until:
            self.enter_phase(step, 0)

    def is_mercy(self):
        return self.constants["director_enabled"] and self.player_hp < self.constants["mercy_health_ratio"] * PLAYER_MAX_UNITS

    def update_budget(self, step, engaged):
        constants = self.constants
        record = self.record
        engaged_count = len(engaged)
        heads = heads_for_engaged(engaged_count, constants)
        cost = constants["token_cost"]
        multiplier = constants["phase_multipliers"][record.phase] if constants["director_enabled"] else 100
        computed = heads * cost * multiplier // 100
        if self.is_mercy():
            computed = computed * constants["mercy_percent"] // 100
        if engaged_count >= 1:
            computed = max(computed, cost)
        if engaged_count == 0:
            record.budget = 0
            record.lower_count = 0
        elif computed < record.budget:
            record.lower_count += 1
            if record.lower_count >= constants["budget_lower_pass_count"]:
                record.budget = computed
                record.lower_count = 0
        else:
            record.budget = computed
            record.lower_count = 0
        record.has_queue = engaged_count * cost > record.budget

    def held_quarters(self, engaged):
        return sum(1 for monster in engaged if monster.token in ("R", "C")) * self.constants["token_cost"]

    def revoke_over_budget(self, step, engaged):
        held = self.held_quarters(engaged)
        if held <= self.record.budget:
            return
        reserved = sorted((monster for monster in engaged if monster.token == "R"),
                          key=lambda monster: self.candidate_key(monster, step), reverse=True)
        for monster in reserved:
            if held <= self.record.budget:
                break
            self.drop_reservation(monster, step, "revocations_budget")
            held -= self.constants["token_cost"]

    def grant_tokens(self, step, engaged):
        constants = self.constants
        held = self.held_quarters(engaged)
        if self.record.budget - held < constants["token_cost"]:
            return
        melee_radius = constants["melee_grant_radius"] if constants["melee_grant_radius"] > 0.0 else constants["grant_radius"]
        candidates = [monster for monster in engaged
                      if monster.token == "-" and monster.cooldown_remaining == 0
                      and self.distance_to_player(monster) <= (melee_radius if monster.role == "melee" else constants["grant_radius"])]
        candidates.sort(key=lambda monster: self.candidate_key(monster, step))
        for monster in candidates:
            if self.record.budget - held < constants["token_cost"]:
                break
            monster.token = "R"
            monster.grant_step = step
            held += constants["token_cost"]
            self.events["grants"] += 1
            if monster.expired_at_step is not None and step - monster.expired_at_step <= constants["token_pass_period"]:
                self.events["expiry_regranted_same"] += 1
            monster.expired_at_step = None

    def ordered_by_distance(self, monsters):
        def key(monster):
            dx = monster.x - self.player_x
            dy = monster.y - self.player_y
            return (int(dx * dx + dy * dy), monster.sid)
        return sorted(monsters, key=key)

    def choose_sector(self, bearing, count, offset, occupied, remembered_index):
        best_index = None
        best_cost = None
        keep_bonus = self.constants["slot_keep_bonus"]
        for index in range(count):
            if occupied[index]:
                continue
            cost = angular_difference(bearing, index * 360.0 / count + offset)
            if index == remembered_index:
                cost -= keep_bonus
            if best_cost is None or cost < best_cost - 1e-9:
                best_index, best_cost = index, cost
        return best_index

    def assign_wait_ring(self, step, engaged):
        counts = self.constants["wait_sector_counts"]
        occupied = [[False] * count for count in counts]
        melee = [monster for monster in engaged if monster.role == "melee"]
        for monster in melee:
            if monster.token != "C":
                continue
            if self.constants["committed_keeps_wait_slot"] and monster.wait_slot is not None:
                occupied[monster.wait_slot[0]][monster.wait_slot[1]] = True
            else:
                monster.wait_slot = None
        candidates = self.ordered_by_distance([monster for monster in melee if monster.token in ("-", "R")])
        kept = set()
        if self.constants["wait_keep_max_error"] > 0.0:
            for monster in candidates:
                remembered = monster.remembered_wait_slot
                if remembered is None or occupied[remembered[0]][remembered[1]]:
                    continue
                center = remembered[1] * 360.0 / counts[remembered[0]]
                if angular_difference(self.bearing_from_player(monster), center) > self.constants["wait_keep_max_error"]:
                    continue
                occupied[remembered[0]][remembered[1]] = True
                monster.wait_slot = remembered
                kept.add(monster.sid)
        for monster in candidates:
            if monster.sid in kept:
                continue
            bearing = self.bearing_from_player(monster)
            new_slot = None
            for ring_index, count in enumerate(counts):
                remembered = monster.remembered_wait_slot
                remembered_index = remembered[1] if remembered is not None and remembered[0] == ring_index else None
                sector = self.choose_sector(bearing, count, 0.0, occupied[ring_index], remembered_index)
                if sector is not None:
                    new_slot = (ring_index, sector)
                    occupied[ring_index][sector] = True
                    break
            if new_slot is not None and monster.remembered_wait_slot is not None and new_slot != monster.remembered_wait_slot:
                monster.wait_reassignments += 1
            monster.wait_slot = new_slot
            if new_slot is not None:
                monster.remembered_wait_slot = new_slot
        claimed = [monster.wait_slot for monster in melee if monster.wait_slot is not None]
        self.events["slot_conflicts"] += len(claimed) - len(set(claimed))
        self.events["double_slot_samples"] += sum(1 for monster in melee if monster.wait_slot is not None and monster.inner_slot is not None)

    def assign_ranged_sectors(self, engaged):
        count = self.constants["ranged_sector_count"]
        occupied = [False] * count
        ranged = self.ordered_by_distance([monster for monster in engaged if monster.role == "ranged"])
        for monster in ranged:
            sector = self.choose_sector(self.bearing_from_player(monster), count, self.constants["ranged_sector_offset"],
                                        occupied, monster.remembered_ranged_slot)
            monster.ranged_slot = sector
            if sector is not None:
                occupied[sector] = True
                monster.remembered_ranged_slot = sector
        claimed = [monster.ranged_slot for monster in ranged if monster.ranged_slot is not None]
        self.events["slot_conflicts"] += len(claimed) - len(set(claimed))

    def assign_inner_sectors(self, engaged):
        count = self.constants["inner_sector_count"]
        occupied = [False] * count
        holders = self.ordered_by_distance([monster for monster in engaged if monster.role == "melee" and monster.token in ("R", "C")])
        for monster in engaged:
            if monster.token not in ("R", "C"):
                monster.inner_slot = None
        for monster in holders:
            sector = self.choose_sector(self.bearing_from_player(monster), count, 0.0, occupied, monster.remembered_inner_slot)
            monster.inner_slot = sector
            if sector is not None:
                occupied[sector] = True
                monster.remembered_inner_slot = sector
        claimed = [monster.inner_slot for monster in holders if monster.inner_slot is not None]
        self.events["slot_conflicts"] += len(claimed) - len(set(claimed))

    def mobility(self, monster):
        if monster.state == CAST:
            return 0.0
        if self.is_spec and monster.token in ("R", "C"):
            return self.constants["holder_push_share"]
        return 1.0

    def integrate_and_separate(self):
        for monster in self.monsters:
            intent = monster.intent
            if intent is None:
                continue
            direction_x, direction_y, scale, max_distance = intent
            length = min(monster.speed * scale / STEPS_PER_SECOND, max_distance)
            monster.x += direction_x * length
            monster.y += direction_y * length
        monsters = self.monsters
        minimum_gap = MONSTER_RADIUS * 2.0
        minimum_gap_squared = minimum_gap * minimum_gap
        count = len(monsters)
        for first_index in range(count):
            first = monsters[first_index]
            for second_index in range(first_index + 1, count):
                second = monsters[second_index]
                dx = second.x - first.x
                if dx >= minimum_gap or dx <= -minimum_gap:
                    continue
                dy = second.y - first.y
                distance_squared = dx * dx + dy * dy
                if distance_squared >= minimum_gap_squared:
                    continue
                distance = math.sqrt(distance_squared)
                if distance < 1e-6:
                    normal_x, normal_y, distance = 1.0, 0.0, 0.0
                else:
                    normal_x, normal_y = dx / distance, dy / distance
                first_mobility = self.mobility(first)
                second_mobility = self.mobility(second)
                total = first_mobility + second_mobility
                if total <= 0.0:
                    continue
                overlap = minimum_gap - distance
                first.x -= normal_x * overlap * first_mobility / total
                first.y -= normal_y * overlap * first_mobility / total
                second.x += normal_x * overlap * second_mobility / total
                second.y += normal_y * overlap * second_mobility / total
        player_gap = PLAYER_RADIUS + MONSTER_RADIUS
        for monster in monsters:
            dx = monster.x - self.player_x
            dy = monster.y - self.player_y
            distance = math.hypot(dx, dy)
            if distance >= player_gap:
                continue
            if distance < 1e-6:
                dx, dy, distance = 1.0, 0.0, 1.0
            monster.x = self.player_x + dx / distance * player_gap
            monster.y = self.player_y + dy / distance * player_gap

    def record_step(self, step):
        casting = sum(1 for monster in self.monsters if monster.state == CAST)
        self.concurrent_samples.append(casting)
        if self.is_spec and self.record is not None:
            self.phase_steps[self.record.phase] += 1
            self.reserved_steps += sum(1 for monster in self.monsters if monster.token == "R")
            self.committed_steps += sum(1 for monster in self.monsters if monster.token == "C")
            if step % 32 == 0:
                self.budget_samples.append(self.record.budget)
                self.intensity_samples.append(self.record.intensity)
        digest_parts = [str(step), f"{self.player_hp}"]
        for monster in self.monsters:
            digest_parts.append(f"{monster.x:.6f},{monster.y:.6f},{monster.state},{monster.token},{monster.wait_slot},{monster.inner_slot}")
        if self.record is not None:
            digest_parts.append(f"{self.record.intensity},{self.record.phase},{self.record.budget}")
        self.hasher.update("|".join(digest_parts).encode("ascii"))
        if step < 10 * STEPS_PER_SECOND:
            return
        if step % 8 == 0:
            for monster in self.monsters:
                if not monster.engaged or monster.state == CAST:
                    continue
                distance = self.distance_to_player(monster)
                self.non_casting_distances.append(distance)
                if self.is_spec and monster.token == "-" and monster.role == "melee":
                    self.waiting_distances.append(distance)
                    if monster.wait_slot is not None:
                        goal_x, goal_y = self.wait_goal_point(monster)
                        error = math.hypot(goal_x - monster.x, goal_y - monster.y)
                        self.goal_errors.append(error)
                        self.lag_samples += 1
                        if error > 150.0:
                            self.lag_count += 1
        if step % 32 == 0:
            waiting = [monster for monster in self.monsters if monster.engaged and monster.state != CAST and monster.role == "melee"
                       and (not self.is_spec or monster.token == "-")]
            if len(waiting) >= 2:
                bearings = [(self.bearing_from_player(monster), self.distance_to_player(monster)) for monster in waiting]
                minimum_all = 360.0
                minimum_band = 360.0
                for first_index in range(len(bearings)):
                    for second_index in range(first_index + 1, len(bearings)):
                        gap = angular_difference(bearings[first_index][0], bearings[second_index][0])
                        minimum_all = min(minimum_all, gap)
                        if abs(bearings[first_index][1] - bearings[second_index][1]) < 100.0:
                            minimum_band = min(minimum_band, gap)
                self.min_gap_all.append(minimum_all)
                if minimum_band < 360.0:
                    self.min_gap_band.append(minimum_band)

    def compile_metrics(self):
        steps = self.total_steps
        seconds = steps / STEPS_PER_SECOND
        start_steps = [step for step, _ in self.cast_log]
        gaps = [later - earlier for earlier, later in zip(start_steps, start_steps[1:])]
        close_pairs = sum(1 for gap in gaps if gap < 12)
        longest_waits = []
        first_attack_delays = []
        for monster in self.monsters:
            if monster.engage_step is None:
                continue
            points = [monster.engage_step] + monster.cast_starts + [steps]
            longest_waits.append(max(later - earlier for earlier, later in zip(points, points[1:])) / STEPS_PER_SECOND)
            if monster.cast_starts:
                first_attack_delays.append((monster.cast_starts[0] - monster.engage_step) / STEPS_PER_SECOND)
        melee_count = sum(1 for monster in self.monsters if monster.role == "melee")
        metrics = {
            "max_concurrent": max(self.concurrent_samples),
            "mean_concurrent": round(statistics.fmean(self.concurrent_samples), 3),
            "attack_starts_per_s": round(len(self.cast_log) / seconds, 3),
            "distinct_attackers_30s": len({sid for step, sid in self.cast_log if step <= 30 * STEPS_PER_SECOND}),
            "monster_count": len(self.monsters),
            "starvation_p95_s": round(percentile(longest_waits, 0.95), 2) if longest_waits else None,
            "starvation_max_s": round(max(longest_waits), 2) if longest_waits else None,
            "close_start_fraction": round(close_pairs / len(gaps), 3) if gaps else 0.0,
            "same_step_starts": sum(1 for gap in gaps if gap == 0),
            "min_start_gap_steps": min(gaps) if gaps else None,
            "median_noncasting_distance": round(statistics.median(self.non_casting_distances), 1) if self.non_casting_distances else None,
            "median_waiting_distance": round(statistics.median(self.waiting_distances), 1) if self.waiting_distances else None,
            "min_gap_all_median_deg": round(statistics.median(self.min_gap_all), 1) if self.min_gap_all else None,
            "min_gap_all_p5_deg": round(percentile(self.min_gap_all, 0.05), 1) if self.min_gap_all else None,
            "min_gap_band_median_deg": round(statistics.median(self.min_gap_band), 1) if self.min_gap_band else None,
            "slot_conflicts": self.events["slot_conflicts"],
            "time_to_first_attack_s": round((start_steps[0] - 1) / STEPS_PER_SECOND, 3) if start_steps else None,
            "mean_first_attack_per_monster_s": round(statistics.fmean(first_attack_delays), 2) if first_attack_delays else None,
            "hits": self.events["hits"],
            "misses": self.events["misses"],
            "player_damage_pct_per_s": round(self.legacy_damage / PLAYER_MAX_UNITS * 100.0 / seconds, 3),
            "player_resets": self.events["player_resets"],
        }
        if not self.is_spec:
            return metrics
        phase_fraction = {}
        phase_dps = {}
        total_phase_steps = max(1, sum(self.phase_steps))
        for index, name in enumerate(PHASE_NAMES):
            phase_fraction[name] = round(self.phase_steps[index] / total_phase_steps, 3)
            phase_seconds = self.phase_steps[index] / STEPS_PER_SECOND
            phase_dps[name] = round(self.phase_damage[index] / PLAYER_MAX_UNITS * 100.0 / phase_seconds, 3) if phase_seconds > 0 else None
        token_steps = max(1, self.reserved_steps + self.committed_steps)
        metrics.update({
            "phase_fraction": phase_fraction,
            "damage_pct_per_s_by_phase": phase_dps,
            "phase_timeline": self.phase_timeline[:24],
            "relax_entries": self.events["relax_entries"],
            "freeze_triggers": self.events["freeze_triggers"],
            "grants": self.events["grants"],
            "reservation_expiries": self.events["expiries_time"] + self.events["expiries_distance"],
            "expiries_time": self.events["expiries_time"],
            "expiries_distance": self.events["expiries_distance"],
            "expiry_regranted_same": self.events["expiry_regranted_same"],
            "revocations_budget": self.events["revocations_budget"],
            "tokenless_casts": self.events["tokenless_casts"],
            "grant_to_commit_median_s": round(statistics.median(self.commit_latencies) / STEPS_PER_SECOND, 3) if self.commit_latencies else None,
            "grant_to_commit_p95_s": round(percentile(self.commit_latencies, 0.95) / STEPS_PER_SECOND, 3) if self.commit_latencies else None,
            "reserved_token_time_fraction": round(self.reserved_steps / token_steps, 3),
            "wait_reassign_per_monster_per_min": round(sum(m.wait_reassignments for m in self.monsters) / max(1, melee_count) / (seconds / 60.0), 2),
            "wait_goal_error_mean_cm": round(statistics.fmean(self.goal_errors), 1) if self.goal_errors else None,
            "wait_lag_fraction": round(self.lag_count / self.lag_samples, 3) if self.lag_samples else None,
            "double_slot_samples": self.events["double_slot_samples"],
            "budget_mean_quarters": round(statistics.fmean(self.budget_samples), 2) if self.budget_samples else None,
            "intensity_mean": round(statistics.fmean(self.intensity_samples), 1) if self.intensity_samples else None,
        })
        return metrics


V2_CONSTANTS = {
    "token_pass_period": 8,
    "ring_pass_period": 32,
    "max_tokens": 8,
    "max_ranged_tokens": 3,
    "token_cost": 4,
    "reservation_expiry": 160,
    "regrant_cooldown": 48,
    "wait_stage_cap": 64,
    "melee_grant_radius": 900.0,
    "ranged_grant_margin": 100.0,
    "engaged_count_radius": 1600.0,
    "ranged_role_range": 450.0,
    "reach_slack": 300.0,
    "boss_health_threshold": 2000,
    "boss_spacing": 24,
    "retaliation_steps": 64,
    "retaliation_order": "first",
    "intercept_distance_ratio": 2.0,
    "distance_revoke_ratio": 1.25,
    "self_rereserve": True,
    "wait_ring_inner_min": 320.0,
    "wait_ring_inner_max": 520.0,
    "wait_ring_margin": 200.0,
    "wait_ring_spacing": 200.0,
    "wait_sector_counts": (12, 16),
    "inner_sector_count": 8,
    "ranged_sector_count": 12,
    "ranged_sector_offset": 15.0,
    "slot_keep_error": 45.0,
    "slot_search_half": 90.0,
    "slot_gap": 60.0,
    "blocked_progress_steps": 64,
    "blocked_progress_cm": 20.0,
    "blocked_far_cm": 60.0,
    "blocked_sector_steps": 512,
    "ranged_blocked_detection": True,
    "moving_target_speed": 150.0,
    "rear_penalty": 60.0,
    "south_penalty": 90.0,
    "south_band_radius": 340.0,
    "drift_half": 6.0,
    "drift_period": 256,
    "feint_depth": 40.0,
    "feint_steps": 32,
    "feint_every": 6,
    "encounter_reset": 128,
    "spacing_by_phase": (20, 16, 24, 32),
    "mercy_spacing_extra": 12,
    "phase_multipliers": (100, 100, 75, 50),
    "budget_lower_pass_count": 2,
    "intensity_peak": 700,
    "sustain_steps": 256,
    "fade_floor": 250,
    "fade_max_steps": 256,
    "relax_steps": 256,
    "relax_intensity_cap": 250,
    "buildup_intensity_cap": 400,
    "start_intensity": 8,
    "damage_gain": 2,
    "quiet_steps": 64,
    "quiet_decay": 40,
    "big_damage_permille": 200,
    "freeze_steps": 64,
    "mercy_open_ratio": 0.25,
    "mercy_rearm_ratio": 0.40,
    "mercy_steps": 256,
    "mercy_freeze_steps": 32,
    "mercy_lockout_steps": 1920,
    "opening_grace": 96,
    "grace_min_engaged": 2,
    "holder_push_share": 0.25,
    "player_attack_period": 48,
    "player_attack_reach": 250.0,
    "player_rotate_reach": 600.0,
}


def v2_base_tokens(engaged_count, cap):
    if engaged_count <= 0:
        return 0
    return clamp(max((engaged_count + 1) // 3, min(engaged_count, 2)), 1, cap)


def v2_pool_tokens(base_tokens, multiplier):
    if base_tokens <= 0:
        return 0
    return max(1, max(base_tokens - 1, (base_tokens * multiplier + 50) // 100))


def v2_sector_width(radius, count, gap):
    arc = int(math.floor(2.0 * math.pi * radius / count))
    if arc <= 0:
        return count
    return max(1, (2 * int(MONSTER_RADIUS) + int(gap) + arc - 1) // arc)


def v2_covered_sectors(index, width, count):
    return [(index + offset) % count for offset in range(-((width - 1) // 2), width // 2 + 1)]


def v2_sector_center(count, index, offset):
    return wrap_degrees(index * 360.0 / count + offset)


def v2_unit(angle_degrees):
    radians = math.radians(angle_degrees)
    return math.cos(radians), math.sin(radians)


class TargetRecordV2:
    def __init__(self, step, constants):
        self.created_step = step
        self.grace_until = step + constants["opening_grace"]
        self.freeze_until = 0
        self.last_attack_start = -100000
        self.last_boss_start = -100000
        self.budget = 0
        self.budget_ranged = 0
        self.lower_count = 0
        self.lower_count_ranged = 0
        self.base_tokens = (0, 0)
        self.intensity = 0
        self.phase = 0
        self.phase_entered = step
        self.phase_until = 0
        self.last_damage_step = step
        self.windows = [0, 0]
        self.mercy_until = 0
        self.mercy_lockout_until = 0
        self.is_mercy_armed = True
        self.zero_engaged_since = None
        self.has_queue = False
        self.n_melee = 0
        self.n_ranged = 0
        self.blocked_until = {}


class SimulationV2(Simulation):
    def __init__(self, config):
        super().__init__(config)
        self.is_spec = True
        self.constants = dict(V2_CONSTANTS)
        self.constants.update(config.get("overrides", {}))
        self.player_attack = config.get("player_attack", "nearest")
        self.current_step = 0
        self.previous_player = (self.player_x, self.player_y)
        self.player_velocity = (0.0, 0.0)
        self.v2_events = {name: 0 for name in (
            "intercepts", "self_rereserves", "releases", "blocked_wait", "blocked_ranged", "blocked_while_still",
            "spacing_violations", "boss_spacing_violations", "mercy_openings", "player_hits", "record_resets", "ondeck_marks")}
        self.release_pending = {"melee": [], "ranged": []}
        self.ondeck_latencies = []
        self.boss_starts = []
        self.low_budget_steps = 0
        self.multi_melee_steps = 0
        self.mercy_steps = 0
        self.freeze_steps = 0
        constants = self.constants
        for monster in self.monsters:
            if SPECIES[monster.kind].get("max_health", 100) >= constants["boss_health_threshold"]:
                monster.role = "boss"
            monster.melee_reach = monster.ability_range if monster.ability_range < constants["ranged_role_range"] else 0.0
            monster.max_reach = monster.ability_range
            monster.wait_ring_radius = clamp(monster.melee_reach + constants["wait_ring_margin"] + MONSTER_RADIUS,
                                             constants["wait_ring_inner_min"], constants["wait_ring_inner_max"])
            monster.inner_radius = monster.stop_at

    def v2_spacing(self, record, step):
        spacing = self.constants["spacing_by_phase"][record.phase]
        if step < record.mercy_until:
            spacing += self.constants["mercy_spacing_extra"]
        return spacing

    def has_attack_token(self, monster, step):
        record = self.record
        if record is None:
            return False
        constants = self.constants
        if monster.role == "boss":
            if not constants.get("boss_waits_for_minions", True):
                return True
            return step - record.last_attack_start >= constants["boss_spacing"]
        if monster.token == "C":
            return True
        if monster.token != "R":
            return False
        if step - record.last_attack_start < self.v2_spacing(record, step):
            return False
        if step - record.last_boss_start < constants["boss_spacing"]:
            return False
        if step < record.grace_until and record.n_melee + record.n_ranged >= constants["grace_min_engaged"]:
            return False
        return step >= record.freeze_until

    def on_cast_started(self, monster, step):
        monster.cast_starts.append(step)
        self.cast_log.append((step, monster.sid))
        record = self.record
        constants = self.constants
        if record is None:
            self.events["tokenless_casts"] += 1
            return
        if monster.role == "boss":
            if step - record.last_attack_start < constants["boss_spacing"]:
                self.v2_events["boss_spacing_violations"] += 1
            record.last_boss_start = step
            record.intensity = min(1000, record.intensity + constants["start_intensity"])
            self.boss_starts.append(step)
            return
        if monster.token == "C":
            record.last_attack_start = step
            return
        if monster.token != "R":
            self.events["tokenless_casts"] += 1
            return
        if step - record.last_attack_start < self.v2_spacing(record, step):
            self.v2_events["spacing_violations"] += 1
        if step - record.last_boss_start < constants["boss_spacing"]:
            self.v2_events["boss_spacing_violations"] += 1
        monster.token = "C"
        record.last_attack_start = step
        record.intensity = min(1000, record.intensity + constants["start_intensity"])
        self.commit_latencies.append(step - monster.grant_step)
        pending = self.release_pending[monster.role]
        self.ondeck_latencies.extend(step - released for released in pending)
        pending.clear()

    def update_tactics(self, step):
        constants = self.constants
        self.current_step = step
        previous_x, previous_y = self.previous_player
        self.player_velocity = ((self.player_x - previous_x) * STEPS_PER_SECOND, (self.player_y - previous_y) * STEPS_PER_SECOND)
        self.previous_player = (self.player_x, self.player_y)
        self.swing_player_weapon(step)
        engaged = [monster for monster in self.monsters if monster.engaged]
        if engaged and self.record is None:
            self.record = TargetRecordV2(step, constants)
            self.phase_timeline.append((round(step / STEPS_PER_SECOND, 2), PHASE_NAMES[0], 0))
        record = self.record
        if record is None:
            return
        count_radius = constants["engaged_count_radius"]
        record.n_melee = sum(1 for monster in engaged if monster.role == "melee" and self.distance_to_player(monster) <= count_radius)
        record.n_ranged = sum(1 for monster in engaged if monster.role == "ranged" and self.distance_to_player(monster) <= count_radius)
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

    def swing_player_weapon(self, step):
        constants = self.constants
        period = constants["player_attack_period"]
        if self.player_attack == "none" or step % period != 0:
            return
        engaged = [monster for monster in self.monsters if monster.engaged]
        if self.player_attack == "rotate":
            in_reach = [monster for monster in engaged if self.distance_to_player(monster) <= constants["player_rotate_reach"]]
            if not in_reach:
                return
            target = in_reach[(step // period) % len(in_reach)]
        else:
            in_reach = [monster for monster in engaged if self.distance_to_player(monster) <= constants["player_attack_reach"]]
            if not in_reach:
                return
            target = min(in_reach, key=lambda monster: (int((monster.x - self.player_x) ** 2 + (monster.y - self.player_y) ** 2), monster.sid))
        target.last_damaged_step = step
        self.v2_events["player_hits"] += 1

    def grant_cap(self, monster):
        if monster.role == "ranged":
            return monster.max_reach + self.constants["ranged_grant_margin"]
        return self.constants["melee_grant_radius"]

    def is_retaliating(self, monster, step):
        return monster.last_damaged_step != 0 and step - monster.last_damaged_step <= self.constants["retaliation_steps"]

    def is_grant_candidate(self, monster, step):
        if not monster.engaged or monster.role == "boss" or monster.token != "-" or monster.cooldown_remaining != 0:
            return False
        distance = self.distance_to_player(monster)
        if monster.ability_range < distance - self.constants["reach_slack"]:
            return False
        return distance <= self.grant_cap(monster)

    def candidate_key_v2(self, monster, step):
        retaliation_rank = 0 if self.is_retaliating(monster, step) else 1
        regrant_rank = 0 if step >= monster.regrant_ready_step else 1
        wait_stage = min((step - monster.last_attack_end_step) // 32, self.constants["wait_stage_cap"])
        dx = monster.x - self.player_x
        dy = monster.y - self.player_y
        distance_squared = int(dx * dx + dy * dy)
        if self.constants["retaliation_order"] == "first":
            return (retaliation_rank, regrant_rank, -wait_stage, distance_squared, monster.sid)
        return (regrant_rank, retaliation_rank, -wait_stage, distance_squared, monster.sid)

    def revoke_with_penalty(self, monster, step, reason):
        monster.token = "-"
        monster.inner_slot = None
        if self.constants.get("expiry_resets_aging", True):
            monster.last_attack_end_step = step
        monster.regrant_ready_step = step + self.constants["regrant_cooldown"]
        self.events[reason] += 1

    def sync_tokens_v2(self, step, engaged):
        constants = self.constants
        released = False
        for monster in engaged:
            if monster.role == "boss":
                continue
            if monster.token == "C" and monster.state != CAST:
                monster.last_attack_end_step = step
                monster.regrant_ready_step = step + constants["regrant_cooldown"]
                monster.inner_slot = None
                self.v2_events["releases"] += 1
                self.release_pending[monster.role].append(step)
                released = True
                has_other = any(self.is_grant_candidate(other, step) for other in engaged if other is not monster and other.role == monster.role)
                if constants["self_rereserve"] and not has_other:
                    monster.token = "R"
                    monster.grant_step = step
                    self.v2_events["self_rereserves"] += 1
                else:
                    monster.token = "-"
                continue
            if monster.token != "R":
                continue
            if step - monster.grant_step >= constants["reservation_expiry"]:
                self.revoke_with_penalty(monster, step, "expiries_time")
            elif self.distance_to_player(monster) > self.grant_cap(monster) * constants["distance_revoke_ratio"]:
                self.revoke_with_penalty(monster, step, "expiries_distance")
        return released

    def v2_ring_radius(self, monster, ring):
        return monster.wait_ring_radius + ring * self.constants["wait_ring_spacing"]

    def v2_drift(self, monster):
        period = self.constants["drift_period"]
        position = (self.current_step + (monster.sid * 37) % period) % period
        triangle = abs(position - period / 2.0) / (period / 4.0) - 1.0
        sign = 1.0 if monster.sid % 2 == 0 else -1.0
        return sign * self.constants["drift_half"] * triangle

    def v2_feint(self, monster):
        constants = self.constants
        if ((self.current_step // constants["feint_steps"]) + monster.sid) % constants["feint_every"] == 0:
            return constants["feint_depth"]
        return 0.0

    def v2_wait_point(self, monster):
        constants = self.constants
        ring, index = monster.wait_slot
        angle = v2_sector_center(constants["wait_sector_counts"][ring], index, 0.0) + self.v2_drift(monster)
        if monster.band is not None and monster.action == "circle":
            radius = (monster.band[0] + monster.band[1]) * 0.5
        else:
            radius = self.v2_ring_radius(monster, ring) - self.v2_feint(monster)
        unit_x, unit_y = v2_unit(angle)
        return self.player_x + unit_x * radius, self.player_y + unit_y * radius, radius

    def wait_goal_point(self, monster):
        goal_x, goal_y, _ = self.v2_wait_point(monster)
        return goal_x, goal_y

    def ranged_goal_point(self, monster):
        constants = self.constants
        center = v2_sector_center(constants["ranged_sector_count"], monster.ranged_slot, constants["ranged_sector_offset"])
        radius = (monster.band[0] + monster.band[1]) * 0.5
        if math.cos(math.radians(center)) < -0.5 and constants["south_band_radius"] > 0.0:
            radius = min(radius, constants["south_band_radius"])
        unit_x, unit_y = v2_unit(center)
        return self.player_x + unit_x * radius, self.player_y + unit_y * radius

    def is_target_closing_in(self, monster):
        dx = monster.x - self.player_x
        dy = monster.y - self.player_y
        distance = math.hypot(dx, dy)
        if distance < 1e-6:
            return False
        velocity_x, velocity_y = self.player_velocity
        return (velocity_x * dx + velocity_y * dy) / distance > self.constants["moving_target_speed"]

    def detect_blocked_slots(self, step, engaged):
        constants = self.constants
        record = self.record
        for monster in engaged:
            key = None
            if monster.role == "melee" and monster.token == "-" and monster.wait_slot is not None:
                key = ("W", monster.wait_slot[0], monster.wait_slot[1])
                goal_x, goal_y = self.wait_goal_point(monster)
            elif monster.role == "ranged" and monster.ranged_slot is not None and constants["ranged_blocked_detection"]:
                key = ("R", monster.ranged_slot)
                goal_x, goal_y = self.ranged_goal_point(monster)
            if key is None:
                monster.slot_key = None
                continue
            distance = math.hypot(goal_x - monster.x, goal_y - monster.y)
            if key != monster.slot_key or distance <= constants["blocked_far_cm"]:
                monster.slot_key = key
                monster.slot_anchor_step = step
                monster.slot_anchor_cm = distance
                monster.slot_best_cm = distance
                continue
            monster.slot_best_cm = min(monster.slot_best_cm, distance)
            if step - monster.slot_anchor_step < constants["blocked_progress_steps"]:
                continue
            if monster.slot_anchor_cm - monster.slot_best_cm < constants["blocked_progress_cm"]:
                record.blocked_until[key] = step + constants["blocked_sector_steps"]
                self.v2_events["blocked_wait" if key[0] == "W" else "blocked_ranged"] += 1
                if monster.intent is None or monster.state == CAST:
                    self.v2_events["blocked_while_still"] += 1
            monster.slot_anchor_step = step
            monster.slot_anchor_cm = distance
            monster.slot_best_cm = distance

    def v2_enter_phase(self, step, phase):
        constants = self.constants
        record = self.record
        record.phase = phase
        record.phase_entered = step
        if phase == 1:
            record.phase_until = step + constants["sustain_steps"]
        elif phase == 2:
            record.phase_until = step + constants["fade_max_steps"]
        elif phase == 3:
            record.phase_until = step + constants["relax_steps"]
            record.intensity = min(record.intensity, constants["relax_intensity_cap"])
            self.events["relax_entries"] += 1
        else:
            record.intensity = min(record.intensity, constants["buildup_intensity_cap"])
        self.phase_timeline.append((round(step / STEPS_PER_SECOND, 2), PHASE_NAMES[phase], record.intensity))

    def update_director_v2(self, step):
        constants = self.constants
        record = self.record
        drop = self.pending_damage
        self.pending_damage = 0
        if drop > 0:
            drop_permille = drop * 1000 // PLAYER_MAX_UNITS
            record.intensity = min(1000, record.intensity + drop_permille * constants["damage_gain"])
            record.last_damage_step = step
            record.windows[1] += drop_permille
        if step % constants["ring_pass_period"] != 0:
            return
        if step - record.last_damage_step > constants["quiet_steps"]:
            record.intensity -= constants["quiet_decay"]
        if record.windows[0] + record.windows[1] >= constants["big_damage_permille"]:
            record.freeze_until = max(record.freeze_until, step + constants["freeze_steps"])
            self.events["freeze_triggers"] += 1
        record.windows = [record.windows[1], 0]
        record.intensity = clamp(record.intensity, 0, 1000)
        if record.phase == 0 and record.intensity >= constants["intensity_peak"]:
            self.v2_enter_phase(step, 1)
        elif record.phase == 1 and step >= record.phase_until:
            self.v2_enter_phase(step, 2)
        elif record.phase == 2 and (record.intensity <= constants["fade_floor"] or step >= record.phase_until):
            self.v2_enter_phase(step, 3)
        elif record.phase == 3 and step >= record.phase_until:
            self.v2_enter_phase(step, 0)
        health_ratio = self.player_hp / PLAYER_MAX_UNITS
        if health_ratio > constants["mercy_rearm_ratio"]:
            record.is_mercy_armed = True
        if health_ratio < constants["mercy_open_ratio"] and record.is_mercy_armed and step >= record.mercy_lockout_until:
            record.mercy_until = step + constants["mercy_steps"]
            record.freeze_until = max(record.freeze_until, step + constants["mercy_freeze_steps"])
            record.is_mercy_armed = False
            record.mercy_lockout_until = step + constants["mercy_lockout_steps"]
            self.v2_events["mercy_openings"] += 1

    def lowered_budget(self, current, lower_count, computed):
        if computed >= current:
            return computed, 0
        lower_count += 1
        if lower_count >= self.constants["budget_lower_pass_count"]:
            return computed, 0
        return current, lower_count

    def update_budget_v2(self):
        constants = self.constants
        record = self.record
        melee_base = v2_base_tokens(record.n_melee, constants["max_tokens"])
        ranged_base = v2_base_tokens(record.n_ranged, constants["max_ranged_tokens"])
        if melee_base + ranged_base > constants["max_tokens"]:
            ranged_base = max(0, constants["max_tokens"] - melee_base)
        record.base_tokens = (melee_base, ranged_base)
        multiplier = constants["phase_multipliers"][record.phase]
        cost = constants["token_cost"]
        record.budget, record.lower_count = self.lowered_budget(record.budget, record.lower_count, v2_pool_tokens(melee_base, multiplier) * cost)
        record.budget_ranged, record.lower_count_ranged = self.lowered_budget(record.budget_ranged, record.lower_count_ranged, v2_pool_tokens(ranged_base, multiplier) * cost)
        record.has_queue = record.n_melee * cost > record.budget

    def pool_budget(self, pool):
        return self.record.budget if pool == "melee" else self.record.budget_ranged

    def revoke_over_budget_v2(self, step, engaged):
        cost = self.constants["token_cost"]
        for pool in ("melee", "ranged"):
            budget = self.pool_budget(pool)
            members = [monster for monster in engaged if monster.role == pool]
            held = sum(cost for monster in members if monster.token in ("R", "C"))
            if held <= budget:
                continue
            reserved = sorted((monster for monster in members if monster.token == "R"),
                              key=lambda monster: self.candidate_key_v2(monster, step), reverse=True)
            for monster in reserved:
                if held <= budget:
                    break
                monster.token = "-"
                monster.inner_slot = None
                self.events["revocations_budget"] += 1
                held -= cost

    def grant_token_v2(self, monster, step):
        monster.token = "R"
        monster.grant_step = step
        self.events["grants"] += 1

    def grant_tokens_v2(self, step, engaged):
        constants = self.constants
        cost = constants["token_cost"]
        for pool in ("melee", "ranged"):
            budget = self.pool_budget(pool)
            members = [monster for monster in engaged if monster.role == pool]
            for monster in members:
                monster.on_deck = False
            held = sum(cost for monster in members if monster.token in ("R", "C"))
            candidates = sorted((monster for monster in members if self.is_grant_candidate(monster, step)),
                                key=lambda monster: self.candidate_key_v2(monster, step))
            index = 0
            while index < len(candidates) and budget - held >= cost:
                self.grant_token_v2(candidates[index], step)
                held += cost
                index += 1
            remaining = candidates[index:]
            if not remaining:
                continue
            for candidate in [monster for monster in remaining if self.is_retaliating(monster, step)]:
                candidate_distance = self.distance_to_player(candidate)
                victims = [monster for monster in members if monster.token == "R"
                           and self.distance_to_player(monster) >= constants["intercept_distance_ratio"] * candidate_distance]
                if not victims:
                    continue
                victim = max(victims, key=lambda monster: (self.distance_to_player(monster), -monster.sid))
                victim.token = "-"
                victim.inner_slot = None
                self.grant_token_v2(candidate, step)
                remaining.remove(candidate)
                self.v2_events["intercepts"] += 1
            if remaining:
                remaining[0].on_deck = True
                self.v2_events["ondeck_marks"] += 1

    def v2_is_blocked(self, key):
        return self.record.blocked_until.get(key, 0) > self.current_step

    def v2_rear_cost(self, center):
        velocity_x, velocity_y = self.player_velocity
        if math.hypot(velocity_x, velocity_y) < self.constants["moving_target_speed"]:
            return 0.0
        unit_x, unit_y = v2_unit(center)
        return self.constants["rear_penalty"] if unit_x * velocity_x + unit_y * velocity_y < 0.0 else 0.0

    def v2_ranged_cost(self, center):
        cost = self.v2_rear_cost(center)
        if math.cos(math.radians(center)) < -0.5:
            cost += self.constants["south_penalty"]
        return cost

    def v2_can_keep(self, bearing, count, offset, occupied, blocked_prefix, index, width):
        if self.v2_is_blocked(blocked_prefix + (index,)):
            return False
        if any(occupied[covered] for covered in v2_covered_sectors(index, width, count)):
            return False
        return angular_difference(bearing, v2_sector_center(count, index, offset)) <= self.constants["slot_keep_error"]

    def v2_search_sector(self, bearing, count, offset, occupied, blocked_prefix, width, extra_cost):
        best_index = None
        best_cost = None
        for index in range(count):
            center = v2_sector_center(count, index, offset)
            error = angular_difference(bearing, center)
            if error > self.constants["slot_search_half"]:
                continue
            if self.v2_is_blocked(blocked_prefix + (index,)):
                continue
            if any(occupied[covered] for covered in v2_covered_sectors(index, width, count)):
                continue
            cost = error + extra_cost(center)
            if best_cost is None or cost < best_cost - 1e-9:
                best_index, best_cost = index, cost
        return best_index

    def assign_wait_ring_v2(self, engaged):
        constants = self.constants
        melee = [monster for monster in engaged if monster.role == "melee"]
        if not self.record.has_queue:
            for monster in melee:
                monster.wait_slot = None
            return
        counts = constants["wait_sector_counts"]
        occupied = [[False] * count for count in counts]
        for monster in self.ordered_by_distance(melee):
            bearing = self.bearing_from_player(monster)
            widths = [v2_sector_width(self.v2_ring_radius(monster, ring), counts[ring], constants["slot_gap"]) for ring in range(len(counts))]
            chosen = None
            previous = monster.remembered_wait_slot
            if previous is not None and self.v2_can_keep(bearing, counts[previous[0]], 0.0, occupied[previous[0]], ("W", previous[0]), previous[1], widths[previous[0]]):
                chosen = previous
            if chosen is None:
                for ring, count in enumerate(counts):
                    index = self.v2_search_sector(bearing, count, 0.0, occupied[ring], ("W", ring), widths[ring], self.v2_rear_cost)
                    if index is not None:
                        chosen = (ring, index)
                        break
            if chosen is not None:
                for covered in v2_covered_sectors(chosen[1], widths[chosen[0]], counts[chosen[0]]):
                    occupied[chosen[0]][covered] = True
                if previous is not None and chosen != previous:
                    monster.wait_reassignments += 1
                monster.remembered_wait_slot = chosen
            monster.wait_slot = chosen

    def assign_ranged_sectors_v2(self, engaged):
        constants = self.constants
        count = constants["ranged_sector_count"]
        offset = constants["ranged_sector_offset"]
        occupied = [False] * count
        for monster in self.ordered_by_distance([monster for monster in engaged if monster.role == "ranged"]):
            bearing = self.bearing_from_player(monster)
            width = v2_sector_width((monster.band[0] + monster.band[1]) * 0.5, count, constants["slot_gap"])
            chosen = None
            previous = monster.remembered_ranged_slot
            if previous is not None and self.v2_can_keep(bearing, count, offset, occupied, ("R",), previous, width):
                chosen = previous
            if chosen is None:
                chosen = self.v2_search_sector(bearing, count, offset, occupied, ("R",), width, self.v2_ranged_cost)
            if chosen is not None:
                for covered in v2_covered_sectors(chosen, width, count):
                    occupied[covered] = True
                monster.remembered_ranged_slot = chosen
            monster.ranged_slot = chosen

    def assign_inner_sectors_v2(self, engaged):
        constants = self.constants
        count = constants["inner_sector_count"]
        occupied = [False] * count
        for monster in engaged:
            if monster.role != "melee" or monster.token not in ("R", "C"):
                monster.inner_slot = None
        holders = self.ordered_by_distance([monster for monster in engaged if monster.role == "melee" and monster.token in ("R", "C")])
        for monster in holders:
            width = v2_sector_width(max(monster.inner_radius, 1.0), count, constants["slot_gap"])
            bearing = self.bearing_from_player(monster)
            chosen = None
            previous = monster.remembered_inner_slot
            if previous is not None and self.v2_can_keep(bearing, count, 0.0, occupied, ("I",), previous, width):
                chosen = previous
            if chosen is None:
                chosen = self.v2_search_sector(bearing, count, 0.0, occupied, ("I",), width, self.v2_rear_cost)
            if chosen is not None:
                for covered in v2_covered_sectors(chosen, width, count):
                    occupied[covered] = True
                monster.remembered_inner_slot = chosen
            monster.inner_slot = chosen

    def check_encounter_reset(self, step):
        record = self.record
        if record.n_melee + record.n_ranged > 0:
            record.zero_engaged_since = None
            return
        if record.zero_engaged_since is None:
            record.zero_engaged_since = step
            return
        if step - record.zero_engaged_since < self.constants["encounter_reset"]:
            return
        for monster in self.monsters:
            monster.token = "-"
            monster.wait_slot = None
            monster.inner_slot = None
            monster.ranged_slot = None
            monster.on_deck = False
        self.record = None
        self.v2_events["record_resets"] += 1

    def outward_intent(self, monster, max_distance):
        away_x = monster.x + (monster.x - self.player_x)
        away_y = monster.y + (monster.y - self.player_y)
        return self.direction_to(monster, away_x, away_y, 1.0, max_distance)

    def compute_wait_intent_v2(self, monster, distance):
        goal_x, goal_y, radius = self.v2_wait_point(monster)
        goal_distance = math.hypot(goal_x - monster.x, goal_y - monster.y)
        if goal_distance <= 40.0:
            return None
        if distance < radius - 60.0:
            if self.is_target_closing_in(monster):
                return None
            return self.outward_intent(monster, radius - distance)
        return self.direction_to(monster, goal_x, goal_y, 1.0, goal_distance)

    def compute_ranged_intent_v2(self, monster, distance):
        low, high = monster.band
        if distance < low or distance > high or monster.ranged_slot is None:
            return self.compute_legacy_intent(monster)
        count = self.constants["ranged_sector_count"]
        center = v2_sector_center(count, monster.ranged_slot, self.constants["ranged_sector_offset"])
        if angular_difference(self.bearing_from_player(monster), center) <= 180.0 / count:
            return None
        goal_x, goal_y = self.ranged_goal_point(monster)
        return self.direction_to(monster, goal_x, goal_y, monster.band_scale, math.hypot(goal_x - monster.x, goal_y - monster.y))

    def compute_tactics_intent(self, monster):
        record = self.record
        if record is None or monster.role == "boss":
            return self.compute_legacy_intent(monster)
        distance = self.distance_to_player(monster)
        holds_token = monster.token in ("R", "C")
        if monster.action == "approach":
            if holds_token and monster.inner_slot is not None:
                if distance <= monster.stop_at:
                    return None
                unit_x, unit_y = v2_unit(v2_sector_center(self.constants["inner_sector_count"], monster.inner_slot, 0.0))
                goal_x = self.player_x + unit_x * monster.stop_at
                goal_y = self.player_y + unit_y * monster.stop_at
                return self.direction_to(monster, goal_x, goal_y, 1.0, math.hypot(goal_x - monster.x, goal_y - monster.y))
            if holds_token:
                return self.compute_legacy_intent(monster)
            if record.has_queue and monster.wait_slot is not None:
                return self.compute_wait_intent_v2(monster, distance)
            if record.has_queue and monster.role == "melee":
                radius = self.v2_ring_radius(monster, 0)
                if distance < radius - 60.0:
                    return self.outward_intent(monster, radius - distance)
                if distance <= radius + 200.0:
                    return None
                return self.direction_to(monster, self.player_x, self.player_y, 1.0, distance - radius - 200.0)
            return self.compute_legacy_intent(monster)
        if monster.role == "melee":
            if not holds_token and monster.wait_slot is not None:
                goal_x, goal_y, _ = self.v2_wait_point(monster)
                goal_distance = math.hypot(goal_x - monster.x, goal_y - monster.y)
                if goal_distance <= 40.0:
                    return None
                return self.direction_to(monster, goal_x, goal_y, monster.band_scale, goal_distance)
            return self.compute_legacy_intent(monster)
        return self.compute_ranged_intent_v2(monster, distance)

    def record_step(self, step):
        super().record_step(step)
        record = self.record
        if record is None:
            return
        if record.n_melee >= 2:
            self.multi_melee_steps += 1
            if record.budget <= self.constants["token_cost"]:
                self.low_budget_steps += 1
        if step < record.mercy_until:
            self.mercy_steps += 1
        if step < record.freeze_until:
            self.freeze_steps += 1

    def compile_metrics(self):
        metrics = super().compile_metrics()
        steps = self.total_steps
        boss = next((monster for monster in self.monsters if monster.role == "boss"), None)
        boss_points = ([boss.engage_step] + self.boss_starts) if boss is not None and boss.engage_step is not None else []
        boss_gaps = [later - earlier for earlier, later in zip(boss_points, boss_points[1:])]
        metrics.update({
            "low_budget_fraction": round(self.low_budget_steps / self.multi_melee_steps, 3) if self.multi_melee_steps else None,
            "mercy_fraction": round(self.mercy_steps / steps, 3),
            "freeze_fraction": round(self.freeze_steps / steps, 3),
            "boss_starts": len(self.boss_starts),
            "boss_gap_max_s": round(max(boss_gaps) / STEPS_PER_SECOND, 2) if boss_gaps else None,
            "boss_gap_median_s": round(statistics.median(boss_gaps[1:]) / STEPS_PER_SECOND, 2) if len(boss_gaps) > 1 else None,
            "boss_gap_limit_s": round((boss.cooldown_steps + 128) / STEPS_PER_SECOND, 2) if boss is not None else None,
            "ondeck_latency_median_s": round(statistics.median(self.ondeck_latencies) / STEPS_PER_SECOND, 3) if self.ondeck_latencies else None,
            "ondeck_latency_p95_s": round(percentile(self.ondeck_latencies, 0.95) / STEPS_PER_SECOND, 3) if self.ondeck_latencies else None,
        })
        metrics.update(self.v2_events)
        return metrics


def v2_policy_list():
    return [("LEGACY", "LEGACY", None), ("V1", "SPEC", None), ("V3", "SPEC", RECOMMENDED_V3_OVERRIDES), ("V2", "V2", None)]


V2_COMPOSITIONS = ["goblin1", "goblin3", "goblin6", "goblin10", "goblin20", "goblin30", "mixed", "boss"]
V2_FIX_VARIANTS = {
    "V2": {},
    "F_reach500": {"reach_slack": 500.0},
    "F_reach_off": {"reach_slack": 100000.0},
    "F_spacing_16_12_20_28": {"spacing_by_phase": (16, 12, 20, 28)},
    "F_max5": {"max_tokens": 5},
    "F_margin120_reach500": {"wait_ring_margin": 120.0, "reach_slack": 500.0},
    "F_big120": {"big_damage_permille": 120},
    "F_ranged_noblock_nosouth": {"ranged_blocked_detection": False, "south_band_radius": 0.0},
    "F_retaliate_after_cooldown": {"retaliation_order": "after_cooldown"},
    "F_combo": {"reach_slack": 500.0, "wait_ring_margin": 120.0, "max_tokens": 5, "ranged_blocked_detection": False,
                "south_band_radius": 0.0},
    "F_keep_aging_expiry256": {"expiry_resets_aging": False, "reservation_expiry": 256},
    "F_boss_priority": {"boss_waits_for_minions": False},
    "F_big160": {"big_damage_permille": 160},
    "F_final_A": {"reach_slack": 500.0, "wait_ring_margin": 150.0, "ranged_blocked_detection": False, "south_band_radius": 0.0,
                  "expiry_resets_aging": False, "reservation_expiry": 256, "boss_waits_for_minions": False},
    "F_final_B": {"reach_slack": 500.0, "wait_ring_margin": 150.0, "ranged_blocked_detection": False, "south_band_radius": 0.0,
                  "expiry_resets_aging": False, "reservation_expiry": 256, "boss_waits_for_minions": False, "max_tokens": 5},
}


def summarize_fix_variants(rows, legacy_rows):
    lines = ["### 변형별 요약(굶음은 goblin1 제외 전체 시나리오 최댓값)", "",
             "| 변형 | 소규모 정지 레거시대비(N3,6) | 소규모 이동(N3,6 카이팅) | N30 정지 레거시대비 | 최장굶음 정지/이동 s | 최소 공격자 비율 | N30 최대동시 | 만료 합 | 막힘 W/R 합 | 예산≤1 최대 | 동결 비율 평균 | 보스 간격 최대 정지·후퇴/카이팅 s | 피해%/초 정지 평균 |",
             "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for label in V2_FIX_VARIANTS:
        group = [row for row in rows if row.get("variant") == label]
        if not group:
            continue

        def ratio_mean(scenarios, motion):
            values = [v2_starts_ratio(row, legacy_rows) for row in group if row["scenario"] in scenarios and row["motion"] == motion]
            values = [value for value in values if value is not None]
            return round(statistics.fmean(values), 2) if values else None

        stationary = [row for row in group if row["motion"] == "stationary"]
        moving = [row for row in group if row["motion"] != "stationary"]
        boss_still = [row["boss_gap_max_s"] for row in group if row["scenario"] == "boss" and row["motion"] != "kiting" and row["boss_gap_max_s"] is not None]
        boss_kiting = [row["boss_gap_max_s"] for row in group if row["scenario"] == "boss" and row["motion"] == "kiting" and row["boss_gap_max_s"] is not None]
        lines.append("| " + " | ".join([
            label, v2_cell(ratio_mean(("goblin3", "goblin6"), "stationary")), v2_cell(ratio_mean(("goblin3", "goblin6"), "kiting")),
            v2_cell(ratio_mean(("goblin30",), "stationary")),
            f"{v2_cell(max(row['starvation_max_s'] for row in stationary))}/{v2_cell(max(row['starvation_max_s'] for row in moving))}",
            v2_cell(round(min(row["distinct_attackers_30s"] / row["monster_count"] for row in group), 2)),
            v2_cell(max(row["max_concurrent"] for row in group if row["scenario"] == "goblin30")),
            v2_cell(sum(row["reservation_expiries"] for row in group)),
            f"{sum(row['blocked_wait'] for row in group)}/{sum(row['blocked_ranged'] for row in group)}",
            v2_cell(max((row["low_budget_fraction"] or 0.0) for row in group)),
            v2_cell(round(statistics.fmean(row["freeze_fraction"] for row in group), 3), 3),
            f"{v2_cell(max(boss_still) if boss_still else None)}/{v2_cell(max(boss_kiting) if boss_kiting else None)}",
            v2_cell(round(statistics.fmean(row["player_damage_pct_per_s"] for row in stationary), 2))]) + " |")
    lines.append("")
    return lines


def v2_cell(value, digits=2):
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def v2_starts_ratio(row, legacy_rows):
    legacy = legacy_rows.get((row["scenario"], row["motion"]))
    if not legacy or not legacy["attack_starts_per_s"]:
        return None
    return round(row["attack_starts_per_s"] / legacy["attack_starts_per_s"], 2)


def write_v2_tables(rows, path, title):
    legacy_rows = {(row["scenario"], row["motion"]): row for row in rows if row.get("variant") == "LEGACY"}
    lines = [f"## {title}", ""]
    matrix = [row for row in rows if row.get("block") == "matrix"]
    if matrix:
        lines += ["### 비교 표(레거시 / v1 / v3 / v2)", "",
                  "| 시나리오 | 이동 | 정책 | 최대동시 | 시작/초 | 레거시대비 | 30초공격자/전체 | 최장굶음s | 12스텝연속 | 비시전중앙cm | 뒤처짐 | 피해%/초 |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for row in matrix:
            lines.append("| " + " | ".join([
                row["scenario"], row["motion"], row["variant"], v2_cell(row["max_concurrent"]), v2_cell(row["attack_starts_per_s"]),
                v2_cell(v2_starts_ratio(row, legacy_rows)), f"{row['distinct_attackers_30s']}/{row['monster_count']}",
                v2_cell(row["starvation_max_s"]), v2_cell(row["close_start_fraction"]), v2_cell(row["median_noncasting_distance"], 0),
                v2_cell(row.get("wait_lag_fraction")), v2_cell(row["player_damage_pct_per_s"])]) + " |")
        lines.append("")
    v2_rows = [row for row in rows if row.get("policy") == "V2" and "hash_first" not in row and row.get("block") != "fix"]
    if v2_rows:
        lines += ["### v2 전용 지표", "",
                  "| 시나리오 | 이동 | 변형 | 박자 B/S/F/R % | Relax 진입 | 예산≤1 비율 | 가로채기 | OnDeck 지연 중앙/p95 s | 간격 위반(졸개/보스) | 무토큰 시전 | 보스 간격 최대/한도 s | 만료(시간/거리) | 자기 재예약 | 막힘 W/R(정지 중) | 큰피해 동결 | 자비 |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for row in v2_rows:
            fractions = row.get("phase_fraction", {})
            phase_text = "/".join(str(round(fractions.get(name, 0.0) * 100)) for name in PHASE_NAMES)
            lines.append("| " + " | ".join([
                row["scenario"], row["motion"], row.get("variant", ""), phase_text, v2_cell(row.get("relax_entries")),
                v2_cell(row.get("low_budget_fraction")), v2_cell(row.get("intercepts")),
                f"{v2_cell(row.get('ondeck_latency_median_s'))}/{v2_cell(row.get('ondeck_latency_p95_s'))}",
                f"{row.get('spacing_violations')}/{row.get('boss_spacing_violations')}", v2_cell(row.get("tokenless_casts")),
                f"{v2_cell(row.get('boss_gap_max_s'))}/{v2_cell(row.get('boss_gap_limit_s'))}",
                f"{row.get('expiries_time')}/{row.get('expiries_distance')}", v2_cell(row.get("self_rereserves")),
                f"{row.get('blocked_wait')}/{row.get('blocked_ranged')}({row.get('blocked_while_still')})",
                v2_cell(row.get("freeze_triggers")), v2_cell(row.get("mercy_openings"))]) + " |")
        lines.append("")
    fixes = [row for row in rows if row.get("block") == "fix"]
    if fixes:
        lines += summarize_fix_variants(fixes, legacy_rows)
        lines += ["### 상수 수정 후보 비교(v2 기준)", "",
                  "| 시나리오 | 이동 | 변형 | 최대동시 | 시작/초 | 레거시대비 | 30초공격자/전체 | 최장굶음s | 예산≤1 | Relax | 만료 | 막힘 W/R | 피해%/초 |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for row in fixes:
            lines.append("| " + " | ".join([
                row["scenario"], row["motion"], row["variant"], v2_cell(row["max_concurrent"]), v2_cell(row["attack_starts_per_s"]),
                v2_cell(v2_starts_ratio(row, legacy_rows)), f"{row['distinct_attackers_30s']}/{row['monster_count']}",
                v2_cell(row["starvation_max_s"]), v2_cell(row.get("low_budget_fraction")), v2_cell(row.get("relax_entries")),
                v2_cell(row.get("reservation_expiries")), f"{row.get('blocked_wait')}/{row.get('blocked_ranged')}",
                v2_cell(row["player_damage_pct_per_s"])]) + " |")
        lines.append("")
    small = [row for row in rows if row.get("block") == "small"]
    if small:
        lines += ["### 소규모(N=2~7) 레거시 대비", "", "| 시나리오 | 이동 | 정책 | 시작/초 | 레거시대비 | 최대동시 | 최장굶음s |", "|---|---|---|---|---|---|---|"]
        for row in small:
            lines.append(f"| {row['scenario']} | {row['motion']} | {row['variant']} | {v2_cell(row['attack_starts_per_s'])} | "
                         f"{v2_cell(v2_starts_ratio(row, legacy_rows))} | {row['max_concurrent']} | {v2_cell(row['starvation_max_s'])} |")
        lines.append("")
    extra = [row for row in rows if row.get("block") in ("determinism", "retaliation")]
    if extra:
        lines += ["### 결정론·보복 모형", ""]
        for row in extra:
            if "hash_first" in row:
                lines.append(f"- 결정론 {row['scenario']} {row['motion']}: identical={row['identical']} {row['hash_first'][:16]}")
            else:
                lines.append(f"- 보복(플레이어 600cm 회전 타격) {row['scenario']} {row['motion']}: 가로채기 {row['intercepts']}, "
                             f"플레이어 타격 {row['player_hits']}, 시작/초 {row['attack_starts_per_s']}, 최장굶음 {row['starvation_max_s']}s")
        lines.append("")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def experiment_v2(quick):
    rows = []
    policies = v2_policy_list()
    compositions = ["goblin3", "goblin10", "goblin30", "mixed", "boss"] if quick else V2_COMPOSITIONS
    for composition in compositions:
        for motion in MOTIONS:
            for label, policy, overrides in policies:
                rows.append(dict(run_case(composition, motion, policy, overrides), variant=label, block="matrix"))
    for count in (2, 4, 5, 7):
        for motion in ("stationary", "kiting"):
            for label, policy, overrides in (policies[0], policies[3]):
                rows.append(dict(run_case(f"goblin{count}", motion, policy, overrides), variant=label, block="small"))
    for composition, motion in (("goblin10", "stationary"), ("mixed", "kiting"), ("boss", "kiting")):
        first = run_case(composition, motion, "V2", with_hash=True)
        second = run_case(composition, motion, "V2", with_hash=True)
        rows.append({"scenario": composition, "motion": motion, "policy": "V2", "variant": "V2", "block": "determinism",
                     "hash_first": first["hash"], "hash_second": second["hash"], "identical": first == second})
    for composition in ("goblin10", "mixed"):
        rows.append(dict(run_case(composition, "stationary", "V2", player_attack="rotate"), variant="V2_rotate", block="retaliation"))
    write_v2_tables(rows, os.path.join(os.path.dirname(os.path.abspath(__file__)), "v2_tables.md"), "v2 시뮬 결과(60초, 64스텝/초)")
    return rows


def experiment_v2_fixes(quick):
    rows = []
    for composition in ("goblin3", "goblin6", "goblin10", "goblin20", "goblin30", "mixed", "boss"):
        for motion in MOTIONS:
            rows.append(dict(run_case(composition, motion, "LEGACY"), variant="LEGACY", block="legacy"))
            for label, overrides in V2_FIX_VARIANTS.items():
                rows.append(dict(run_case(composition, motion, "V2", overrides), variant=label, block="fix"))
    write_v2_tables([row for row in rows if row.get("block") in ("fix", "legacy")],
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), "v2_fix_tables.md"), "v2 상수 수정 후보")
    return rows


def composition_for(name):
    if name == "boss":
        return ["ogre", "goblin", "goblin", "goblin"]
    if name.startswith("goblin"):
        return ["goblin"] * int(name[len("goblin"):])
    if name == "mixed":
        return ["goblin", "goblin", "archer", "goblin", "goblin", "archer", "goblin", "goblin", "archer", "hyena"]
    raise ValueError(name)


def run_case(composition_name, motion, policy, overrides=None, goblin_cast=0.9, spawn_band=(700.0, 1100.0), seconds=60, with_hash=False,
             player_attack="nearest"):
    config = {
        "composition": composition_for(composition_name),
        "motion": motion,
        "policy": policy,
        "overrides": overrides or {},
        "goblin_cast": goblin_cast,
        "spawn_band": spawn_band,
        "seconds": seconds,
        "player_attack": player_attack,
    }
    simulation = SimulationV2(config) if policy == "V2" else Simulation(config)
    metrics = simulation.run()
    result = {"scenario": composition_name, "motion": motion, "policy": policy, "goblin_cast": goblin_cast,
              "spawn_band": list(spawn_band), "overrides": {key: list(value) if isinstance(value, tuple) else value for key, value in (overrides or {}).items()}}
    result.update(metrics)
    if with_hash:
        result["hash"] = simulation.hasher.hexdigest()
    return result


MAIN_COMPOSITIONS = ["goblin1", "goblin3", "goblin6", "goblin10", "goblin20", "goblin30", "mixed"]
MOTIONS = ["stationary", "kiting", "retreating"]


def experiment_main(quick):
    compositions = ["goblin3", "goblin10", "goblin30", "mixed"] if quick else MAIN_COMPOSITIONS
    rows = []
    for composition in compositions:
        for motion in MOTIONS:
            for policy in ("LEGACY", "SPEC"):
                rows.append(run_case(composition, motion, policy))
    return rows


def experiment_determinism(quick):
    rows = []
    for composition, motion in (("goblin10", "stationary"), ("mixed", "kiting")):
        first = run_case(composition, motion, "SPEC", with_hash=True)
        second = run_case(composition, motion, "SPEC", with_hash=True)
        first_copy = {key: value for key, value in first.items() if key != "hash"}
        second_copy = {key: value for key, value in second.items() if key != "hash"}
        rows.append({"scenario": composition, "motion": motion, "hash_first": first["hash"], "hash_second": second["hash"],
                     "identical": first["hash"] == second["hash"] and first_copy == second_copy})
    return rows


def experiment_cast_sensitivity(quick):
    rows = []
    for cast in (0.6, 1.3):
        for composition in ("goblin3", "goblin10", "goblin30"):
            for policy in ("LEGACY", "SPEC"):
                rows.append(run_case(composition, "stationary", policy, goblin_cast=cast))
    return rows


def experiment_small_fights(quick):
    rows = []
    for count in (1, 2, 3, 4):
        for motion in ("stationary", "kiting"):
            rows.append(run_case(f"goblin{count}", motion, "LEGACY"))
            rows.append(dict(run_case(f"goblin{count}", motion, "SPEC"), variant="spec"))
            rows.append(dict(run_case(f"goblin{count}", motion, "SPEC", {"wait_ring_requires_queue": True}), variant="ring_only_with_queue"))
            rows.append(dict(run_case(f"goblin{count}", motion, "SPEC", {"heads_rule": "floor2", "wait_ring_requires_queue": True}), variant="floor2+ring_only_with_queue"))
    return rows


def experiment_expiry(quick):
    rows = []
    for expiry in (96, 160, 256):
        for composition in ("goblin10", "goblin30", "mixed"):
            for motion in ("stationary", "kiting", "retreating"):
                rows.append(dict(run_case(composition, motion, "SPEC", {"reservation_expiry": expiry}), variant=f"expiry{expiry}"))
    return rows


def experiment_grace(quick):
    rows = []
    for grace in (0, 24, 48):
        for composition in ("goblin3", "goblin10"):
            for spawn_band in ((200.0, 450.0), (700.0, 1100.0)):
                rows.append(dict(run_case(composition, "stationary", "SPEC", {"opening_grace": grace}, spawn_band=spawn_band), variant=f"grace{grace}"))
    return rows


def experiment_director(quick):
    variants = {
        "spec": {},
        "fade_timeout256": {"fade_max_steps": 256},
        "fade_timeout256+calm_decay": {"fade_max_steps": 256, "calm_phase_decay": True},
        "director_off": {"director_enabled": False},
    }
    rows = []
    for label, overrides in variants.items():
        for composition in ("goblin6", "goblin10", "goblin30", "mixed"):
            for motion in ("stationary", "kiting"):
                rows.append(dict(run_case(composition, motion, "SPEC", overrides), variant=label))
    return rows


def experiment_keep_bonus(quick):
    rows = []
    for bonus in (20.0, 35.0, 50.0):
        for composition in ("goblin10", "goblin20", "goblin30"):
            for motion in ("stationary", "kiting"):
                rows.append(dict(run_case(composition, motion, "SPEC", {"slot_keep_bonus": bonus}), variant=f"keep{int(bonus)}"))
    return rows


def experiment_spacing(quick):
    rows = []
    for spacing in (8, 12, 16, 20):
        for composition in ("goblin10", "goblin30"):
            rows.append(dict(run_case(composition, "stationary", "SPEC", {"attack_start_spacing": spacing}), variant=f"spacing{spacing}"))
    return rows


def experiment_aging(quick):
    rows = []
    for cap in (8, 16, 64):
        for composition in ("goblin6", "goblin10", "goblin20", "goblin30", "mixed"):
            for motion in ("stationary", "kiting"):
                rows.append(dict(run_case(composition, motion, "SPEC", {"wait_stage_cap": cap}), variant=f"stagecap{cap}"))
    return rows


RECOMMENDED_OVERRIDES = {
    "wait_stage_cap": 64,
    "reservation_expiry": 160,
    "expiry_sets_regrant_cooldown": True,
    "fade_max_steps": 256,
    "relax_intensity_reset": 250,
    "heads_rule": "floor2",
    "slot_keep_bonus": 35.0,
}


RECOMMENDED_V2_OVERRIDES = dict(RECOMMENDED_OVERRIDES, wait_slow_scale=1.0, melee_grant_radius=900.0, committed_keeps_wait_slot=True)
RECOMMENDED_V3_OVERRIDES = dict(RECOMMENDED_V2_OVERRIDES, wait_keep_max_error=45.0, phase_multipliers=(100, 100, 75, 50), relax_steps=256)


def experiment_recommended(quick):
    rows = []
    variants = {
        "recommended": RECOMMENDED_OVERRIDES,
        "recommended+wait_full_speed": dict(RECOMMENDED_OVERRIDES, wait_slow_scale=1.0),
        "recommended+full_speed+grant900": dict(RECOMMENDED_OVERRIDES, wait_slow_scale=1.0, melee_grant_radius=900.0),
        "recommended_v2": RECOMMENDED_V2_OVERRIDES,
        "recommended_v3": RECOMMENDED_V3_OVERRIDES,
        "recommended_no_relax_reset": dict(RECOMMENDED_OVERRIDES, relax_intensity_reset=-1),
        "recommended_spec_heads": dict(RECOMMENDED_OVERRIDES, heads_rule="spec"),
    }
    for label, overrides in variants.items():
        for composition in ["goblin1", "goblin2", "goblin4"] + MAIN_COMPOSITIONS[1:]:
            if label not in ("recommended", "recommended_v2", "recommended_v3") and composition in ("goblin1", "goblin2", "goblin4"):
                continue
            for motion in MOTIONS:
                rows.append(dict(run_case(composition, motion, "SPEC", overrides), variant=label))
    for composition in ("goblin10", "goblin30"):
        for cast in (0.6, 1.3):
            rows.append(dict(run_case(composition, "stationary", "SPEC", RECOMMENDED_V2_OVERRIDES, goblin_cast=cast), variant=f"recommended_v2_cast{cast}"))
            rows.append(dict(run_case(composition, "stationary", "SPEC", RECOMMENDED_V3_OVERRIDES, goblin_cast=cast), variant=f"recommended_v3_cast{cast}"))
            rows.append(dict(run_case(composition, "stationary", "LEGACY", goblin_cast=cast), variant=f"legacy_cast{cast}"))
    first = run_case("mixed", "kiting", "SPEC", RECOMMENDED_V3_OVERRIDES, with_hash=True)
    second = run_case("mixed", "kiting", "SPEC", RECOMMENDED_V3_OVERRIDES, with_hash=True)
    rows.append({"scenario": "mixed", "motion": "kiting", "policy": "SPEC", "variant": "recommended_determinism",
                 "hash_first": first["hash"], "hash_second": second["hash"], "identical": first == second})
    return rows


def experiment_final_sweeps(quick):
    rows = []
    for max_error in (0.0, 45.0, 60.0):
        for composition in ("goblin10", "goblin20", "goblin30", "mixed"):
            for motion in MOTIONS:
                overrides = dict(RECOMMENDED_V2_OVERRIDES, wait_keep_max_error=max_error)
                rows.append(dict(run_case(composition, motion, "SPEC", overrides), variant=f"v2_keeperr{int(max_error)}"))
    pacing = {
        "v2_pace_50_25_relax384": {},
        "v2_pace_75_50_relax384": {"phase_multipliers": (100, 100, 75, 50)},
        "v2_pace_50_25_relax256": {"relax_steps": 256},
        "v2_pace_75_50_relax256": {"phase_multipliers": (100, 100, 75, 50), "relax_steps": 256},
    }
    for label, extra in pacing.items():
        for composition in ("goblin6", "goblin10", "goblin20", "goblin30", "mixed"):
            for motion in ("stationary", "kiting"):
                rows.append(dict(run_case(composition, motion, "SPEC", dict(RECOMMENDED_V2_OVERRIDES, **extra)), variant=label))
    return rows


EXPERIMENTS = {
    "v2": experiment_v2,
    "v2_fixes": experiment_v2_fixes,
    "main": experiment_main,
    "final_sweeps": experiment_final_sweeps,
    "recommended": experiment_recommended,
    "aging": experiment_aging,
    "determinism": experiment_determinism,
    "cast_sensitivity": experiment_cast_sensitivity,
    "small_fights": experiment_small_fights,
    "expiry": experiment_expiry,
    "grace": experiment_grace,
    "director": experiment_director,
    "keep_bonus": experiment_keep_bonus,
    "spacing": experiment_spacing,
}

SUMMARY_COLUMNS = [
    ("scenario", 9), ("motion", 10), ("policy", 6), ("variant", 26), ("max_concurrent", 3), ("mean_concurrent", 5),
    ("attack_starts_per_s", 5), ("distinct_attackers_30s", 3), ("starvation_p95_s", 5), ("starvation_max_s", 5),
    ("close_start_fraction", 5), ("median_noncasting_distance", 6), ("min_gap_all_median_deg", 5),
    ("time_to_first_attack_s", 5), ("player_damage_pct_per_s", 5), ("reservation_expiries", 4),
    ("expiry_regranted_same", 4), ("wait_reassign_per_monster_per_min", 5), ("relax_entries", 3), ("slot_conflicts", 3),
]


def format_row(row):
    cells = []
    for key, width in SUMMARY_COLUMNS:
        value = row.get(key, "")
        cells.append(str(value if value is not None else "-")[:max(width, 3)].ljust(width))
    return " ".join(cells)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--only", default="")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "metrics.json"))
    arguments = parser.parse_args()
    selected = [name for name in arguments.only.split(",") if name] or list(EXPERIMENTS)
    output = {"constants": {key: list(value) if isinstance(value, tuple) else value for key, value in SPEC_CONSTANTS.items()},
              "species": SPECIES,
              "recommended_overrides": {"v1": RECOMMENDED_OVERRIDES, "v2": RECOMMENDED_V2_OVERRIDES, "v3": {key: list(value) if isinstance(value, tuple) else value for key, value in RECOMMENDED_V3_OVERRIDES.items()}},
              "experiments": {}}
    if os.path.exists(arguments.out) and arguments.only:
        with open(arguments.out, "r", encoding="utf-8") as existing:
            output["experiments"] = json.load(existing).get("experiments", {})
    print(" ".join(key[:max(width, 3)].ljust(width) for key, width in SUMMARY_COLUMNS))
    for name in selected:
        started = time.perf_counter()
        rows = EXPERIMENTS[name](arguments.quick)
        output["experiments"][name] = rows
        print(f"== {name} ({time.perf_counter() - started:.1f}s)")
        for row in rows:
            if "hash_first" in row:
                print(f"{row['scenario']} {row['motion']} identical={row['identical']} {row['hash_first'][:16]} {row['hash_second'][:16]}")
            else:
                print(format_row(row))
        sys.stdout.flush()
    with open(arguments.out, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(output, handle, ensure_ascii=False, indent=1)
    print(f"written {arguments.out}")


if __name__ == "__main__":
    main()

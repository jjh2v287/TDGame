"""에디터 밖에서 실행하는 결정론 던전 생성기 (언리얼 모듈 사용 안 함).

사용:
  python Tools/DungeonGen/generate_dungeon.py --theme Crypt --flow Linear|Branch|Loop|Hub|KeyLock --size Small|Medium|Large --seed 7 [--slot 3] [--out Saved/DungeonGen/이름]

출력 폴더(기본 Saved/DungeonGen/<theme>_<flow>_<seed>)에 layout.json, preview.png, report.md 를 쓴다.
같은 인자는 항상 같은 layout.json 을 만든다. 난수는 numpy.random.default_rng([seed, ...]) 만 쓴다.
격자 규약: 셀 400cm, 격자 x → 언리얼 X, 격자 y → 언리얼 Y, N = -Y, E = +X, S = +Y, W = -X. 회전은 시계 방향 90도 단위(N→E→S→W).
"""
import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_dungeon  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GENERATOR_VERSION = "1.0.0"
CELL_SIZE_CM = 400
DIRS = ["N", "E", "S", "W"]
DIR_VEC = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}
OPPOSITE = {"N": "S", "S": "N", "E": "W", "W": "E"}
DIR_YAW = {"N": -90.0, "E": 0.0, "S": 90.0, "W": 180.0}
SIZE_NODE_RANGE = {"Small": (6, 9), "Medium": (10, 15), "Large": (16, 24)}
FLOWS = ["Linear", "Branch", "Loop", "Hub", "KeyLock"]
ATLAS_ORIGIN_CM = (300000, 300000)
ATLAS_SLOT_STRIDE_CM = 30000
MAX_RESTARTS = 300
MAX_NODE_ATTEMPTS = 120
MAX_LOOP_PATH_LEN = 16
CORRIDOR_LENGTH_WEIGHTS = [0.3, 0.4, 0.2, 0.1]

THEMES = {
    "Crypt": {
        "Entrance": {"cells": [(0, 0)], "doors": [((0, 0), "N")], "tags": ["start"]},
        "Straight_A": {"cells": [(0, 0)], "doors": [((0, 0), "N"), ((0, 0), "S")], "tags": ["corridor"]},
        "Straight_B": {"cells": [(0, 0), (0, 1)], "doors": [((0, 0), "N"), ((0, 1), "S")], "tags": ["corridor"]},
        "Corner_A": {"cells": [(0, 0)], "doors": [((0, 0), "N"), ((0, 0), "E")], "tags": ["corridor"]},
        "Corner_B": {"cells": [(0, 0), (1, 0)], "doors": [((0, 0), "S"), ((1, 0), "N")], "tags": ["corridor"]},
        "T_Junction": {"cells": [(0, 0)], "doors": [((0, 0), "N"), ((0, 0), "E"), ((0, 0), "W")], "tags": ["combat"]},
        "Large_A": {
            "cells": [(0, 0), (1, 0), (0, 1), (1, 1)],
            "doors": [((0, 0), "N"), ((1, 0), "E"), ((1, 1), "S"), ((0, 1), "W")],
            "tags": ["combat"],
        },
        "Large_B": {
            "cells": [(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (2, 1)],
            "doors": [((1, 0), "N"), ((2, 1), "E"), ((1, 1), "S"), ((0, 0), "W")],
            "tags": ["combat"],
        },
        "DeadEnd": {"cells": [(0, 0)], "doors": [((0, 0), "S")], "tags": ["deadend"]},
        "Treasure": {"cells": [(0, 0)], "doors": [((0, 0), "S")], "tags": ["treasure"]},
        "Elite": {
            "cells": [(0, 0), (1, 0), (0, 1), (1, 1)],
            "doors": [((0, 1), "S"), ((1, 0), "N"), ((1, 1), "E")],
            "tags": ["elite"],
        },
        "Boss": {
            "cells": [(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (2, 1)],
            "doors": [((1, 1), "S")],
            "tags": ["boss"],
        },
    }
}

KIND_MODULES = {
    "Start": ["Entrance"],
    "Combat": ["T_Junction", "Large_A", "Large_B"],
    "Hub": ["Large_A", "Large_B"],
    "Elite": ["Elite"],
    "Treasure": ["Treasure"],
    "Key": ["Treasure"],
    "DeadEnd": ["DeadEnd"],
    "Boss": ["Boss"],
}
KIND_EXTRA_TAGS = {"Key": ["key"], "Hub": ["hub"]}
CORRIDOR_PAIR_MODULES = ["Straight_B", "Corner_B"]
CORRIDOR_SINGLE_MODULES = ["Straight_A", "Corner_A"]

TAG_COLORS = {
    "start": (96, 200, 96),
    "boss": (220, 70, 70),
    "elite": (240, 150, 60),
    "treasure": (240, 210, 70),
    "combat": (120, 150, 200),
    "deadend": (150, 120, 100),
    "corridor": (200, 200, 200),
}


def add_vec(a, b):
    return (a[0] + b[0], a[1] + b[1])


def sub_vec(a, b):
    return (a[0] - b[0], a[1] - b[1])


def rotate_vec(v, rotation):
    x, y = v
    for _ in range(rotation % 4):
        x, y = -y, x
    return (x, y)


def rotate_dir(direction, rotation):
    return DIRS[(DIRS.index(direction) + rotation) % 4]


_ROTATED_CACHE = {}


def rotated_module(theme, name, rotation):
    key = (theme, name, rotation)
    cached = _ROTATED_CACHE.get(key)
    if cached is not None:
        return cached
    module = THEMES[theme][name]
    cells = [rotate_vec(c, rotation) for c in module["cells"]]
    doors = [(rotate_vec(c, rotation), rotate_dir(d, rotation)) for c, d in module["doors"]]
    min_x = min(c[0] for c in cells)
    min_y = min(c[1] for c in cells)
    cells = [(c[0] - min_x, c[1] - min_y) for c in cells]
    doors = [((c[0] - min_x, c[1] - min_y), d) for c, d in doors]
    _ROTATED_CACHE[key] = (cells, doors)
    return cells, doors


def placements_for(theme, name, world_cell, world_dir):
    result = []
    for rotation in range(4):
        cells, doors = rotated_module(theme, name, rotation)
        for door_index, (local_cell, local_dir) in enumerate(doors):
            if local_dir != world_dir:
                continue
            origin = sub_vec(world_cell, local_cell)
            result.append((rotation, origin, door_index))
    return result


def choice(rng, items):
    return items[int(rng.integers(len(items)))]


def shuffled(rng, items):
    order = rng.permutation(len(items))
    return [items[int(i)] for i in order]


def weighted_index(rng, weights):
    total = float(sum(weights))
    roll = float(rng.random()) * total
    acc = 0.0
    for index, weight in enumerate(weights):
        acc += weight
        if roll < acc:
            return index
    return len(weights) - 1


class FlowGraph:
    def __init__(self):
        self.nodes = []
        self.edges = []

    def add_node(self, kind):
        node_id = "n%d" % len(self.nodes)
        self.nodes.append({"id": node_id, "kind": kind})
        return node_id

    def add_edge(self, a, b, kind="tree", locked=False, key_id=None):
        self.edges.append({"a": a, "b": b, "kind": kind, "locked": locked, "key_id": key_id})

    def add_chain(self, kinds):
        ids = [self.add_node(kind) for kind in kinds]
        for a, b in zip(ids, ids[1:]):
            self.add_edge(a, b)
        return ids

    def add_branch(self, attach_id, kinds):
        previous = attach_id
        ids = []
        for kind in kinds:
            node_id = self.add_node(kind)
            self.add_edge(previous, node_id)
            ids.append(node_id)
            previous = node_id
        return ids

    def degree(self, node_id):
        return sum(1 for e in self.edges if e["a"] == node_id or e["b"] == node_id)

    def to_dict(self):
        return {"nodes": list(self.nodes), "edges": list(self.edges)}


def main_chain_kinds(count):
    return ["Start"] + ["Combat"] * (count - 3) + ["Elite", "Boss"]


def pick_leaf_kind(rng):
    return ["Treasure", "Elite", "DeadEnd"][weighted_index(rng, [0.5, 0.25, 0.25])]


def build_flow_linear(total, rng):
    graph = FlowGraph()
    graph.add_chain(main_chain_kinds(total))
    return graph


def build_flow_branch(total, rng):
    graph = FlowGraph()
    branch_count = {6: 1, 7: 2, 8: 2, 9: 2}.get(total, 3 if total < 16 else 4)
    branch_lengths = [1 + int(rng.integers(2)) for _ in range(branch_count)]
    while branch_lengths and (total - sum(branch_lengths) - 3) * 2 < len(branch_lengths):
        branch_lengths.pop()
    main_count = total - sum(branch_lengths)
    main_ids = graph.add_chain(main_chain_kinds(main_count))
    attach_pool = main_ids[1:main_count - 2]
    attach_order = shuffled(rng, attach_pool)
    for index, length in enumerate(branch_lengths):
        attach_id = attach_order[index % len(attach_order)]
        kinds = ["Combat"] * (length - 1) + [pick_leaf_kind(rng)]
        graph.add_branch(attach_id, kinds)
    return graph


def build_flow_loop(total, rng):
    graph = FlowGraph()
    loop_length = min(int(rng.integers(0, 3)), max(0, total - 6))
    treasure_branch = 1 if total - loop_length - 6 >= 1 and rng.random() < 0.7 else 0
    main_count = total - loop_length - treasure_branch
    main_ids = graph.add_chain(main_chain_kinds(main_count))
    combat_last = main_count - 3
    i = int(rng.integers(1, combat_last - 1))
    j = int(rng.integers(i + 2, combat_last + 1))
    if loop_length == 0:
        graph.add_edge(main_ids[i], main_ids[j], kind="loop")
    else:
        loop_ids = graph.add_branch(main_ids[i], ["Combat"] * loop_length)
        graph.add_edge(loop_ids[-1], main_ids[j], kind="loop")
    if treasure_branch:
        candidates = [main_ids[k] for k in range(1, combat_last + 1) if k not in (i, j)]
        attach_id = choice(rng, candidates) if candidates else main_ids[1]
        graph.add_branch(attach_id, ["Treasure"])
    return graph


def build_flow_hub(total, rng):
    graph = FlowGraph()
    start_id = graph.add_node("Start")
    hub_id = graph.add_node("Hub")
    graph.add_edge(start_id, hub_id)
    spare = total - 6
    counts = [0, 0, 0]
    for _ in range(spare):
        counts[int(rng.integers(3))] += 1
    wing_order = shuffled(rng, [0, 1, 2])
    for wing in wing_order:
        combats = ["Combat"] * counts[wing]
        if wing == 0:
            graph.add_branch(hub_id, combats + ["Elite", "Boss"])
        elif wing == 1:
            graph.add_branch(hub_id, combats + ["Treasure"])
        else:
            graph.add_branch(hub_id, combats + [pick_leaf_kind(rng)])
    return graph


def build_flow_keylock(total, rng):
    graph = FlowGraph()
    key_branch_length = 1 + int(rng.integers(2))
    treasure_branch = 1 if total - key_branch_length - 5 >= 1 and rng.random() < 0.5 else 0
    main_count = total - key_branch_length - treasure_branch
    if main_count < 5:
        key_branch_length = 1
        treasure_branch = 0
        main_count = total - 1
    main_ids = graph.add_chain(main_chain_kinds(main_count))
    lock_index = main_count - 3 if rng.random() < 0.5 else main_count - 2
    key_id = "key_0"
    for edge in graph.edges:
        if edge["a"] == main_ids[lock_index] and edge["b"] == main_ids[lock_index + 1]:
            edge["locked"] = True
            edge["key_id"] = key_id
    attach_index = int(rng.integers(1, lock_index + 1))
    graph.add_branch(main_ids[attach_index], ["Combat"] * (key_branch_length - 1) + ["Key"])
    if treasure_branch:
        candidates = [main_ids[k] for k in range(1, main_count - 2) if k != attach_index]
        attach_id = choice(rng, candidates) if candidates else main_ids[1]
        graph.add_branch(attach_id, ["Treasure"])
    return graph


FLOW_BUILDERS = {
    "Linear": build_flow_linear,
    "Branch": build_flow_branch,
    "Loop": build_flow_loop,
    "Hub": build_flow_hub,
    "KeyLock": build_flow_keylock,
}


def build_flow(flow, size, rng):
    low, high = SIZE_NODE_RANGE[size]
    total = int(rng.integers(low, high + 1))
    return FLOW_BUILDERS[flow](total, rng)


class Layout:
    def __init__(self, theme):
        self.theme = theme
        self.rooms = []
        self.occupied = {}
        self.node_to_room = {}

    def is_free(self, cells):
        return all(c not in self.occupied for c in cells)

    def place(self, name, rotation, origin, tags, flow_node):
        cells, doors = rotated_module(self.theme, name, rotation)
        world_cells = [add_vec(origin, c) for c in cells]
        room = {
            "id": "r%d" % len(self.rooms),
            "module": name,
            "rotation": rotation,
            "cell_origin": origin,
            "cells": world_cells,
            "tags": list(tags),
            "flow_node": flow_node,
            "doors": [
                {"cell": add_vec(origin, c), "dir": d, "connected_room": None, "locked": False, "key_id": None}
                for c, d in doors
            ],
        }
        index = len(self.rooms)
        self.rooms.append(room)
        for c in world_cells:
            self.occupied[c] = index
        if flow_node is not None:
            self.node_to_room[flow_node] = index
        return index

    def free_doors(self, room_index):
        return [d for d in self.rooms[room_index]["doors"] if d["connected_room"] is None]

    def connect(self, room_a, cell_a, dir_a, room_b, locked=False, key_id=None):
        cell_b = add_vec(cell_a, DIR_VEC[dir_a])
        dir_b = OPPOSITE[dir_a]
        door_a = self.find_door(room_a, cell_a, dir_a)
        door_b = self.find_door(room_b, cell_b, dir_b)
        for door, other in ((door_a, room_b), (door_b, room_a)):
            door["connected_room"] = self.rooms[other]["id"]
            door["locked"] = locked
            door["key_id"] = key_id

    def find_door(self, room_index, cell, direction):
        for door in self.rooms[room_index]["doors"]:
            if door["cell"] == cell and door["dir"] == direction:
                return door
        raise KeyError("door not found %s %s %s" % (room_index, cell, direction))

    def bounds(self):
        xs = [c[0] for c in self.occupied]
        ys = [c[1] for c in self.occupied]
        return (min(xs), min(ys)), (max(xs), max(ys))


def random_corridor_path(layout, start_cell, in_dir, length, rng):
    path = []
    cell = start_cell
    door_in = in_dir
    used = set()
    for _ in range(length):
        if cell in layout.occupied or cell in used:
            return None
        forward = OPPOSITE[door_in]
        left = rotate_dir(forward, 3)
        right = rotate_dir(forward, 1)
        options = [forward, left, right]
        out_dir = options[weighted_index(rng, [0.6, 0.2, 0.2])]
        path.append((cell, door_in, out_dir))
        used.add(cell)
        cell = add_vec(cell, DIR_VEC[out_dir])
        door_in = OPPOSITE[out_dir]
    return path


def corridor_group_placement(theme, name, group):
    first_cell, first_in, _ = group[0]
    want_cells = set(c for c, _, _ in group)
    want_doors = set()
    want_doors.add((group[0][0], group[0][1]))
    want_doors.add((group[-1][0], group[-1][2]))
    for rotation, origin, _ in placements_for(theme, name, first_cell, first_in):
        cells, doors = rotated_module(theme, name, rotation)
        world_cells = set(add_vec(origin, c) for c in cells)
        world_doors = set((add_vec(origin, c), d) for c, d in doors)
        if world_cells == want_cells and world_doors == want_doors:
            return rotation, origin
    return None


def commit_corridor(layout, path, rng):
    room_indices = []
    index = 0
    while index < len(path):
        placed = None
        if index + 1 < len(path) and rng.random() < 0.5:
            group = path[index:index + 2]
            for name in CORRIDOR_PAIR_MODULES:
                placement = corridor_group_placement(layout.theme, name, group)
                if placement is not None:
                    placed = (name, placement, 2)
                    break
        if placed is None:
            group = path[index:index + 1]
            for name in CORRIDOR_SINGLE_MODULES:
                placement = corridor_group_placement(layout.theme, name, group)
                if placement is not None:
                    placed = (name, placement, 1)
                    break
        if placed is None:
            raise RuntimeError("corridor module not found for %s" % (path[index],))
        name, (rotation, origin), consumed = placed
        room_index = layout.place(name, rotation, origin, THEMES[layout.theme][name]["tags"], None)
        last = path[index + consumed - 1]
        room_indices.append((room_index, last[0], last[2]))
        index += consumed
    return room_indices


def link_chain(layout, parent_index, parent_door, corridor_rooms, child_index, locked, key_id):
    previous_room = parent_index
    previous_cell = parent_door["cell"]
    previous_dir = parent_door["dir"]
    for room_index, out_cell, out_dir in corridor_rooms:
        layout.connect(previous_room, previous_cell, previous_dir, room_index)
        previous_room = room_index
        previous_cell = out_cell
        previous_dir = out_dir
    layout.connect(previous_room, previous_cell, previous_dir, child_index, locked=locked, key_id=key_id)


def try_attach(layout, parent_index, node, degree_required, locked, key_id, rng):
    kind = node["kind"]
    modules = [
        name for name in KIND_MODULES[kind]
        if len(THEMES[layout.theme][name]["doors"]) >= degree_required
    ]
    if not modules:
        return None
    tags = list(THEMES[layout.theme][modules[0]]["tags"]) + KIND_EXTRA_TAGS.get(kind, [])
    for _ in range(MAX_NODE_ATTEMPTS):
        free_doors = layout.free_doors(parent_index)
        if not free_doors:
            return None
        parent_door = choice(rng, free_doors)
        length = weighted_index(rng, CORRIDOR_LENGTH_WEIGHTS)
        first_cell = add_vec(parent_door["cell"], DIR_VEC[parent_door["dir"]])
        path = random_corridor_path(layout, first_cell, OPPOSITE[parent_door["dir"]], length, rng)
        if path is None:
            continue
        if path:
            end_cell = add_vec(path[-1][0], DIR_VEC[path[-1][2]])
            end_dir = OPPOSITE[path[-1][2]]
        else:
            end_cell = first_cell
            end_dir = OPPOSITE[parent_door["dir"]]
        path_cells = set(c for c, _, _ in path)
        if end_cell in path_cells:
            continue
        name = choice(rng, modules)
        for rotation, origin, _ in shuffled(rng, placements_for(layout.theme, name, end_cell, end_dir)):
            cells, _ = rotated_module(layout.theme, name, rotation)
            world_cells = [add_vec(origin, c) for c in cells]
            if not layout.is_free(world_cells):
                continue
            if any(c in path_cells for c in world_cells):
                continue
            corridor_rooms = commit_corridor(layout, path, rng)
            child_index = layout.place(name, rotation, origin, tags, node["id"])
            link_chain(layout, parent_index, parent_door, corridor_rooms, child_index, locked, key_id)
            return child_index
    return None


def bfs_free_cells(layout, start_cell, goal_cell, region_min, region_max):
    if start_cell == goal_cell:
        return [start_cell]
    if start_cell in layout.occupied or goal_cell in layout.occupied:
        return None
    previous = {start_cell: None}
    frontier = [start_cell]
    depth = 0
    while frontier and depth < MAX_LOOP_PATH_LEN:
        next_frontier = []
        for cell in frontier:
            for direction in DIRS:
                neighbor = add_vec(cell, DIR_VEC[direction])
                if neighbor in previous or neighbor in layout.occupied:
                    continue
                if not (region_min[0] <= neighbor[0] <= region_max[0] and region_min[1] <= neighbor[1] <= region_max[1]):
                    continue
                previous[neighbor] = cell
                if neighbor == goal_cell:
                    path = [neighbor]
                    while previous[path[-1]] is not None:
                        path.append(previous[path[-1]])
                    path.reverse()
                    return path
                next_frontier.append(neighbor)
        frontier = next_frontier
        depth += 1
    return None


def direction_between(a, b):
    delta = sub_vec(b, a)
    for direction, vec in DIR_VEC.items():
        if vec == delta:
            return direction
    raise ValueError("cells not adjacent %s %s" % (a, b))


def try_close_loop(layout, room_a, room_b, rng):
    (min_x, min_y), (max_x, max_y) = layout.bounds()
    region_min = (min_x - 3, min_y - 3)
    region_max = (max_x + 3, max_y + 3)
    best = None
    for door_a in layout.free_doors(room_a):
        for door_b in layout.free_doors(room_b):
            out_a = add_vec(door_a["cell"], DIR_VEC[door_a["dir"]])
            out_b = add_vec(door_b["cell"], DIR_VEC[door_b["dir"]])
            if out_a == door_b["cell"] and door_b["dir"] == OPPOSITE[door_a["dir"]]:
                layout.connect(room_a, door_a["cell"], door_a["dir"], room_b)
                return True
            path = bfs_free_cells(layout, out_a, out_b, region_min, region_max)
            if path is None:
                continue
            if best is None or len(path) < len(best[0]):
                best = (path, door_a, door_b)
    if best is None:
        return False
    path, door_a, door_b = best
    corridor = []
    for index, cell in enumerate(path):
        door_in = OPPOSITE[door_a["dir"]] if index == 0 else direction_between(cell, path[index - 1])
        door_out = OPPOSITE[door_b["dir"]] if index == len(path) - 1 else direction_between(cell, path[index + 1])
        if door_in == door_out:
            return False
        corridor.append((cell, door_in, door_out))
    corridor_rooms = commit_corridor(layout, corridor, rng)
    link_chain(layout, room_a, door_a, corridor_rooms, room_b, False, None)
    return True


def solve_layout(graph, theme, rng):
    layout = Layout(theme)
    start_node = graph.nodes[0]
    layout.place("Entrance", 0, (0, 0), THEMES[theme]["Entrance"]["tags"], start_node["id"])
    tree_children = {}
    for edge in graph.edges:
        if edge["kind"] != "tree":
            continue
        tree_children.setdefault(edge["a"], []).append(edge)
    queue = [start_node["id"]]
    while queue:
        parent_id = queue.pop(0)
        parent_index = layout.node_to_room[parent_id]
        for edge in tree_children.get(parent_id, []):
            child = next(n for n in graph.nodes if n["id"] == edge["b"])
            child_index = try_attach(
                layout, parent_index, child, graph.degree(child["id"]), edge["locked"], edge["key_id"], rng
            )
            if child_index is None:
                return None
            queue.append(child["id"])
    for edge in graph.edges:
        if edge["kind"] != "loop":
            continue
        if not try_close_loop(layout, layout.node_to_room[edge["a"]], layout.node_to_room[edge["b"]], rng):
            return None
    return layout


def cell_center_cm(cell):
    return [cell[0] * CELL_SIZE_CM + CELL_SIZE_CM / 2, cell[1] * CELL_SIZE_CM + CELL_SIZE_CM / 2, 0.0]


def build_output(layout, graph, args_dict, slot):
    (min_x, min_y), (max_x, max_y) = layout.bounds()
    shift = (-min_x, -min_y)
    rooms = []
    for room in layout.rooms:
        rooms.append({
            "id": room["id"],
            "module": room["module"],
            "rotation": room["rotation"],
            "cell_origin": list(add_vec(room["cell_origin"], shift)),
            "cells": [list(add_vec(c, shift)) for c in room["cells"]],
            "tags": room["tags"],
            "flow_node": room["flow_node"],
            "doors": [
                {
                    "cell": list(add_vec(d["cell"], shift)),
                    "dir": d["dir"],
                    "connected_room": d["connected_room"],
                    "locked": d["locked"],
                    "key_id": d["key_id"],
                }
                for d in room["doors"]
            ],
        })
    doors = []
    seen = set()
    for room in rooms:
        for door in room["doors"]:
            if door["connected_room"] is None:
                continue
            other_cell = add_vec(tuple(door["cell"]), DIR_VEC[door["dir"]])
            key = tuple(sorted([(room["id"], tuple(door["cell"])), (door["connected_room"], other_cell)]))
            if key in seen:
                continue
            seen.add(key)
            doors.append({
                "room_a": room["id"],
                "cell_a": door["cell"],
                "dir_a": door["dir"],
                "room_b": door["connected_room"],
                "cell_b": list(other_cell),
                "dir_b": OPPOSITE[door["dir"]],
                "locked": door["locked"],
                "key_id": door["key_id"],
            })
    key_rooms = [r["id"] for r in rooms if "key" in r["tags"]]
    keys_locks = []
    for index, door in enumerate(doors):
        if not door["locked"]:
            continue
        keys_locks.append({"key_id": door["key_id"], "key_room": key_rooms[0] if key_rooms else None, "lock_door": index})
    start_room = next(r for r in rooms if "start" in r["tags"])
    boss_room = next(r for r in rooms if "boss" in r["tags"])
    entry_location = cell_center_cm(tuple(start_room["cells"][0]))
    entry_location[1] += CELL_SIZE_CM / 2
    boss_cells = [tuple(c) for c in boss_room["cells"]]
    boss_center = [
        (min(c[0] for c in boss_cells) + max(c[0] for c in boss_cells) + 1) * CELL_SIZE_CM / 2,
        (min(c[1] for c in boss_cells) + max(c[1] for c in boss_cells) + 1) * CELL_SIZE_CM / 2,
        0.0,
    ]
    width_cells = max_x - min_x + 1
    height_cells = max_y - min_y + 1
    output = {
        "generator_version": GENERATOR_VERSION,
        "seed": args_dict["seed"],
        "theme": args_dict["theme"],
        "flow": args_dict["flow"],
        "size": args_dict["size"],
        "cell_size_cm": CELL_SIZE_CM,
        "axis_convention": {"N": "-Y", "E": "+X", "S": "+Y", "W": "-X", "rotation": "clockwise 90deg steps"},
        "flow_graph": graph.to_dict(),
        "rooms": rooms,
        "doors": doors,
        "keys_locks": keys_locks,
        "bounds": {
            "min_cell": [0, 0],
            "max_cell": [width_cells - 1, height_cells - 1],
            "size_cells": [width_cells, height_cells],
            "size_cm": [width_cells * CELL_SIZE_CM, height_cells * CELL_SIZE_CM],
        },
        "entry_transform": {"location_cm": entry_location, "yaw": DIR_YAW["N"], "room": start_room["id"]},
        "exit_transform": {"location_cm": boss_center, "yaw": DIR_YAW["N"], "room": boss_room["id"]},
        "room_count": sum(1 for r in rooms if "corridor" not in r["tags"]),
        "module_count": len(rooms),
    }
    if slot is not None:
        output["slot"] = slot
        output["world_offset_cm"] = [ATLAS_ORIGIN_CM[0] + slot * ATLAS_SLOT_STRIDE_CM, ATLAS_ORIGIN_CM[1], 0.0]
    return output


def room_color(room):
    if "key" in room["tags"]:
        return (250, 230, 120)
    for tag in ["boss", "elite", "treasure", "start", "combat", "deadend", "corridor"]:
        if tag in room["tags"]:
            return TAG_COLORS[tag]
    return (180, 180, 180)


def door_box(x, y, scale, direction):
    half = scale // 2
    gap = 5
    if direction == "N":
        return [x + half - gap, y - 2, x + half + gap, y + 2]
    if direction == "S":
        return [x + half - gap, y + scale - 2, x + half + gap, y + scale + 2]
    if direction == "E":
        return [x + scale - 2, y + half - gap, x + scale + 2, y + half + gap]
    return [x - 2, y + half - gap, x + 2, y + half + gap]


def draw_preview(output, path):
    scale = 28
    margin = 20
    legend_height = 150
    width_cells, height_cells = output["bounds"]["size_cells"]
    image_width = max(width_cells * scale + margin * 2, 440)
    image_height = height_cells * scale + margin * 2 + legend_height
    image = Image.new("RGB", (image_width, image_height), (250, 250, 250))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=11)

    def cell_box(cell):
        return margin + cell[0] * scale, margin + cell[1] * scale

    for gx in range(width_cells + 1):
        x = margin + gx * scale
        draw.line([(x, margin), (x, margin + height_cells * scale)], fill=(225, 225, 225))
    for gy in range(height_cells + 1):
        y = margin + gy * scale
        draw.line([(margin, y), (margin + width_cells * scale, y)], fill=(225, 225, 225))

    wall_edges = {"N": ((0, 0), (1, 0)), "S": ((0, 1), (1, 1)), "E": ((1, 0), (1, 1)), "W": ((0, 0), (0, 1))}
    for room in output["rooms"]:
        color = room_color(room)
        cells = [tuple(c) for c in room["cells"]]
        cell_set = set(cells)
        for cell in cells:
            x, y = cell_box(cell)
            draw.rectangle([x + 1, y + 1, x + scale - 1, y + scale - 1], fill=color)
            for direction, vec in DIR_VEC.items():
                if add_vec(cell, vec) in cell_set:
                    continue
                (ax, ay), (bx, by) = wall_edges[direction]
                draw.line([(x + ax * scale, y + ay * scale), (x + bx * scale, y + by * scale)], fill=(40, 40, 40), width=2)
        if "corridor" in room["tags"]:
            continue
        label = room["id"]
        if "start" in room["tags"]:
            label = "S " + label
        elif "boss" in room["tags"]:
            label = "B " + label
        elif "key" in room["tags"]:
            label = "K " + label
        x, y = cell_box(cells[0])
        draw.text((x + 3, y + 3), label, fill=(20, 20, 20), font=font)

    for room in output["rooms"]:
        for door in room["doors"]:
            x, y = cell_box(tuple(door["cell"]))
            box = door_box(x, y, scale, door["dir"])
            if door["connected_room"] is None:
                draw.rectangle(box, fill=(160, 160, 160))
            elif door["locked"]:
                draw.rectangle(box, fill=(230, 30, 30))
                draw.text((box[0] - 2, box[1] - 12), "L", fill=(200, 0, 0), font=font)
            else:
                draw.rectangle(box, fill=(20, 20, 20))

    legend_top = margin + height_cells * scale + 12
    entries = [
        ("start", "Start (S)"), ("combat", "Combat"), ("elite", "Elite"), ("treasure", "Treasure / Key (K)"),
        ("boss", "Boss (B)"), ("deadend", "DeadEnd"), ("corridor", "Corridor"),
    ]
    for index, (tag, text) in enumerate(entries):
        x = margin + (index % 3) * 140
        y = legend_top + (index // 3) * 18
        draw.rectangle([x, y, x + 12, y + 12], fill=TAG_COLORS[tag])
        draw.text((x + 16, y - 1), text, fill=(20, 20, 20), font=font)
    y = legend_top + 3 * 18
    for index, (color, text) in enumerate([
        ((20, 20, 20), "door (connected)"), ((230, 30, 30), "locked door (L)"), ((160, 160, 160), "sealed socket"),
    ]):
        x = margin + index * 140
        draw.rectangle([x, y + 2, x + 12, y + 6], fill=color)
        draw.text((x + 16, y - 1), text, fill=(20, 20, 20), font=font)
    y += 18
    draw.text(
        (margin, y),
        "%s %s %s seed=%d rooms=%d modules=%d cell=%dcm N=up" % (
            output["theme"], output["flow"], output["size"], output["seed"], output["room_count"],
            output["module_count"], CELL_SIZE_CM,
        ),
        fill=(20, 20, 20), font=font,
    )
    y += 18
    validation = output.get("validation", {})
    status = "PASS" if validation.get("passed") else "FAIL"
    failed = [c["name"] for c in validation.get("checks", []) if not c["passed"]]
    draw.text((margin, y), "validation: %s %s" % (status, ", ".join(failed)), fill=(20, 20, 20), font=font)
    image.save(path)


def generate(theme, flow, size, seed, slot=None):
    if theme not in THEMES:
        raise ValueError("unknown theme %s" % theme)
    if flow not in FLOWS:
        raise ValueError("unknown flow %s" % flow)
    if size not in SIZE_NODE_RANGE:
        raise ValueError("unknown size %s" % size)
    graph = build_flow(flow, size, np.random.default_rng([seed, 0]))
    layout = None
    restarts_used = 0
    for restart in range(MAX_RESTARTS):
        layout = solve_layout(graph, theme, np.random.default_rng([seed, 1, restart]))
        restarts_used = restart
        if layout is not None:
            break
    if layout is None:
        return None
    args_dict = {"theme": theme, "flow": flow, "size": size, "seed": seed}
    output = build_output(layout, graph, args_dict, slot)
    output["layout_restarts"] = restarts_used
    output["validation"] = validate_dungeon.validate(output)
    return output


def write_outputs(output, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    layout_path = os.path.join(out_dir, "layout.json")
    with open(layout_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(output, handle, indent=1, sort_keys=True, ensure_ascii=False)
        handle.write("\n")
    draw_preview(output, os.path.join(out_dir, "preview.png"))
    validate_dungeon.write_report(output, output["validation"], os.path.join(out_dir, "report.md"))
    return layout_path


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--theme", default="Crypt", choices=sorted(THEMES))
    parser.add_argument("--flow", default="Linear", choices=FLOWS)
    parser.add_argument("--size", default="Medium", choices=sorted(SIZE_NODE_RANGE))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--slot", type=int, default=None)
    parser.add_argument("--out", default=None)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    out_dir = args.out or os.path.join(ROOT, "Saved", "DungeonGen", "%s_%s_%d" % (args.theme, args.flow, args.seed))
    output = generate(args.theme, args.flow, args.size, args.seed, args.slot)
    if output is None:
        print("FAIL: layout not solved after %d restarts (%s %s %s seed=%d)" % (
            MAX_RESTARTS, args.theme, args.flow, args.size, args.seed))
        return 2
    layout_path = write_outputs(output, out_dir)
    status = "PASS" if output["validation"]["passed"] else "FAIL"
    print("%s rooms=%d modules=%d restarts=%d -> %s" % (
        status, output["room_count"], output["module_count"], output["layout_restarts"], layout_path))
    return 0 if output["validation"]["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())

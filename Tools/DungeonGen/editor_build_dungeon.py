"""에디터 안에서 실행: DungeonGen 레이아웃(layout.json)을 던전 아틀라스 슬롯에 임시 지오메트리로 베이크한다(설계서 7장·12장 "Bake").

대상: Saved/DungeonGen/slot*/layout.json (slot 번호는 layout.json 의 slot 필드). 슬롯마다 기존 TDGenDungeon_<slot>_* 액터를 지우고 다시 만든다.
지오메트리(룸 모듈 아트가 없을 때의 대체품): 바닥 = 셀당 4m×4m 석판(엔진 Cube + MI_Bedrock2, 인스턴스), 벽 = 셀 변마다 4m×0.3m×3.5m(인스턴스),
문 = 벽 생략, 잠긴 문 = 목재 패널 2장 + 붉은 양 해골, 열쇠 방 = 상자, 보스 방 = 가마솥·붉은 빛, 전투 방 = 통·상자, 정예 방 = 무기 거치대, 복도 = 벽 등불.
마커: TDDungeonSlot_<k>(태그 TDDungeonSlot), TDDungeonEntry_<k>, TDDungeonExit_<k>, 슬롯 NavMeshBoundsVolume.
실행: python Tools/run_in_editor.py Tools/DungeonGen/editor_build_dungeon.py
"""
import glob
import json
import os
import time

import unreal

PROJECT = unreal.SystemLibrary.get_project_directory()
LAYOUTS = sorted(glob.glob(os.path.join(PROJECT, "Saved", "DungeonGen", "slot*", "layout.json")))
MESH_ROOT = "/Game/DarkFantasyTopDown/StaticMeshes"
MAT_ROOT = "/Game/DarkFantasyTopDown/Materials"
CUBE = "/Engine/BasicShapes/Cube"
FLOOR_MAT = f"{MAT_ROOT}/Nature/Surfaces/MI_Bedrock2"
WALL_MAT = f"{MAT_ROOT}/Nature/MI_Cliffs2"
WALL_H = 350.0
WALL_T = 30.0
DIRS = {"N": (0, -1), "S": (0, 1), "E": (1, 0), "W": (-1, 0)}
PROPS = {
    "combat": [(f"{MESH_ROOT}/Barrels/SM_Barrels1", (120, 120)), (f"{MESH_ROOT}/Containers/SM_WoodenCrate", (-130, 110)), (f"{MESH_ROOT}/SmallProps/SM_Skull", (60, -120))],
    "elite": [(f"{MESH_ROOT}/WoodenParts/SM_Armory", (0, 140)), (f"{MESH_ROOT}/Weapons/SM_Shield", (-120, -120)), (f"{MESH_ROOT}/SmallProps/SM_StickedSkull", (130, -100))],
    "treasure": [(f"{MESH_ROOT}/Containers/SM_WoodenChest", (0, 0)), (f"{MESH_ROOT}/Lanterns/SM_CandlesGroup", (90, 70)), (f"{MESH_ROOT}/Containers/SM_WoodenChestEmpty", (-110, 90))],
    "boss": [(f"{MESH_ROOT}/Props2/SM_Cauldron", (0, 0)), (f"{MESH_ROOT}/SmallProps/SM_RamSkull2", (0, -140)), (f"{MESH_ROOT}/SmallProps/SM_Bone", (120, 100)), (f"{MESH_ROOT}/SmallProps/SM_Skull", (-130, 60))],
    "deadend": [(f"{MESH_ROOT}/SmallProps/SM_Bone", (40, 40)), (f"{MESH_ROOT}/SmallProps/SM_Skull", (-60, 30))],
    "start": [(f"{MESH_ROOT}/Lanterns/SM_Lamppost", (150, 150))],
}

EAL = unreal.EditorAssetLibrary
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
t0 = time.time()


def log(msg):
    print(f"[TDDungeonBake {time.time() - t0:6.1f}s] {msg}")


def finish(actor, label, folder, tags=None):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    if tags:
        actor.tags = [unreal.Name(t) for t in tags]
    return actor


def clear_slot(prefix):
    n = 0
    for a in list(eas.get_all_level_actors()):
        if a.get_actor_label().startswith(prefix):
            eas.destroy_actor(a)
            n += 1
    return n


def spawn_ism(mesh_path, mat_path, transforms, label, folder):
    mesh = EAL.load_asset(mesh_path)
    actor = eas.spawn_actor_from_class(unreal.TDInstancedMeshActor, transforms[0].translation, unreal.Rotator(0, 0, 0))
    comp = actor.instanced_mesh_component
    comp.set_static_mesh(mesh)
    mat = EAL.load_asset(mat_path)
    if mat:
        comp.set_material(0, mat)
    comp.add_instances(transforms, True, True)
    return finish(actor, label, folder, ["TDGen", "TDGenDungeon"])


def spawn_mesh(mesh_path, loc, yaw, label, folder, scale=1.0, mat_path=None):
    mesh = EAL.load_asset(mesh_path)
    actor = eas.spawn_actor_from_object(mesh, loc, unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    if mat_path:
        m = EAL.load_asset(mat_path)
        if m:
            actor.static_mesh_component.set_material(0, m)
    actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    return finish(actor, label, folder, ["TDGen", "TDGenDungeon"])


def spawn_light(loc, intensity, color, radius, label, folder, volumetric=2.0):
    actor = eas.spawn_actor_from_class(unreal.PointLight, loc, unreal.Rotator(0, 0, 0))
    c = actor.light_component
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_intensity(intensity)
    c.set_light_color(unreal.LinearColor(color[0], color[1], color[2], 1.0))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("source_radius", 10.0)
    c.set_editor_property("volumetric_scattering_intensity", volumetric)
    c.set_editor_property("cast_shadows", False)
    return finish(actor, label, folder, ["TDGen", "TDGenDungeon"])


def bake(layout_path):
    d = json.load(open(layout_path, encoding="utf-8"))
    slot = int(d.get("slot", 0))
    cell = float(d["cell_size_cm"])
    ox, oy, oz = d["world_offset_cm"]
    prefix = f"TDGenDungeon_{slot}_"
    folder = f"TDGen/Dungeon/Slot{slot}"
    removed = clear_slot(prefix)
    log(f"slot {slot}: removed {removed} old actors; rooms {d['room_count']} modules {d['module_count']} flow {d['flow']}")

    def cell_center(cx, cy):
        return unreal.Vector(ox + (cx + 0.5) * cell, oy + (cy + 0.5) * cell, oz)

    room_of_cell = {}
    for r in d["rooms"]:
        for c in r["cells"]:
            room_of_cell[(c[0], c[1])] = r["id"]
    door_edges = set()
    locked_doors = []
    for door in d["doors"]:
        a = tuple(door["cell_a"])
        b = tuple(door["cell_b"])
        door_edges.add((a, b))
        door_edges.add((b, a))
        if door.get("locked"):
            locked_doors.append(door)

    floor_tr = []
    wall_tr = []
    for (cx, cy), rid in room_of_cell.items():
        center = cell_center(cx, cy)
        floor_tr.append(unreal.Transform(location=unreal.Vector(center.x, center.y, oz - 10.0), rotation=unreal.Rotator(0, 0, 0), scale=unreal.Vector(cell / 100.0, cell / 100.0, 0.2)))
        for name, (dx, dy) in DIRS.items():
            nb = (cx + dx, cy + dy)
            if nb in room_of_cell and room_of_cell[nb] == rid:
                continue
            if ((cx, cy), nb) in door_edges:
                continue
            wx = center.x + dx * cell * 0.5
            wy = center.y + dy * cell * 0.5
            yaw = 90.0 if dx != 0 else 0.0
            wall_tr.append(unreal.Transform(location=unreal.Vector(wx, wy, oz + WALL_H * 0.5), rotation=unreal.Rotator(roll=0, pitch=0, yaw=yaw), scale=unreal.Vector(cell / 100.0 + WALL_T / 100.0, WALL_T / 100.0, WALL_H / 100.0)))
    spawn_ism(CUBE, FLOOR_MAT, floor_tr, prefix + "Floor", folder)
    spawn_ism(CUBE, WALL_MAT, wall_tr, prefix + "Walls", folder)
    log(f"floor cells {len(floor_tr)} wall segments {len(wall_tr)}")

    n_props = 0
    n_lights = 0
    for r in d["rooms"]:
        tag = next((t for t in ("boss", "start", "treasure", "elite", "combat", "deadend") if t in r["tags"]), None)
        cells = r["cells"]
        mx = sum(c[0] for c in cells) / len(cells)
        my = sum(c[1] for c in cells) / len(cells)
        center = unreal.Vector(ox + (mx + 0.5) * cell, oy + (my + 0.5) * cell, oz)
        if tag in PROPS:
            for i, (mesh, (px, py)) in enumerate(PROPS[tag]):
                spawn_mesh(mesh, unreal.Vector(center.x + px, center.y + py, oz), (i * 73) % 360, f"{prefix}{r['id']}_{tag}_{i}", folder)
                n_props += 1
        if tag == "boss":
            spawn_light(unreal.Vector(center.x, center.y, oz + 180), 60.0, (1.0, 0.25, 0.12), 1400.0, f"{prefix}{r['id']}_Light", folder, 6.0)
            n_lights += 1
        elif tag in ("treasure", "elite", "start"):
            spawn_light(unreal.Vector(center.x, center.y, oz + 220), 25.0, (1.0, 0.72, 0.42), 1000.0, f"{prefix}{r['id']}_Light", folder)
            n_lights += 1
        elif "corridor" in r["tags"] and int(r["id"][1:]) % 3 == 0:
            spawn_mesh(f"{MESH_ROOT}/Lanterns/SM_Lamp", unreal.Vector(center.x, center.y + cell * 0.5 - 40, oz + 200), 0.0, f"{prefix}{r['id']}_Lamp", folder)
            spawn_light(unreal.Vector(center.x, center.y + cell * 0.5 - 60, oz + 210), 14.0, (1.0, 0.68, 0.38), 800.0, f"{prefix}{r['id']}_Light", folder, 1.5)
            n_props += 1
            n_lights += 1
        if any(k["key_room"] == r["id"] for k in d.get("keys_locks", [])):
            spawn_mesh(f"{MESH_ROOT}/Containers/SM_WoodenChest", unreal.Vector(center.x, center.y, oz), 0.0, f"{prefix}{r['id']}_KeyChest", folder)
            spawn_light(unreal.Vector(center.x, center.y, oz + 120), 20.0, (0.9, 0.85, 0.4), 700.0, f"{prefix}{r['id']}_KeyLight", folder, 3.0)
            n_props += 1
            n_lights += 1
    for i, door in enumerate(locked_doors):
        a = door["cell_a"]
        b = door["cell_b"]
        mid = unreal.Vector(ox + ((a[0] + b[0]) / 2 + 0.5) * cell, oy + ((a[1] + b[1]) / 2 + 0.5) * cell, oz)
        along_x = a[1] == b[1]
        yaw = 0.0 if along_x else 90.0
        for k, off in enumerate((-100.0, 100.0)):
            loc = unreal.Vector(mid.x + (0 if along_x else off), mid.y + (off if along_x else 0), oz)
            spawn_mesh(f"{MESH_ROOT}/WoodenParts/SM_WoodenConstruction7", loc, yaw, f"{prefix}Lock{i}_Panel{k}", folder)
        spawn_mesh(f"{MESH_ROOT}/SmallProps/SM_RamSkull2", unreal.Vector(mid.x, mid.y, oz + 260), yaw, f"{prefix}Lock{i}_Skull", folder, 1.6)
        spawn_light(unreal.Vector(mid.x, mid.y, oz + 240), 18.0, (1.0, 0.2, 0.1), 600.0, f"{prefix}Lock{i}_Light", folder, 4.0)
        n_props += 3
        n_lights += 1

    e = d["entry_transform"]
    entry = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(ox + e["location_cm"][0], oy + e["location_cm"][1], oz + 50), unreal.Rotator(roll=0, pitch=0, yaw=e["yaw"]))
    finish(entry, f"TDDungeonEntry_{slot}", folder, ["TDGen", "TDDungeonEntry", d["theme"], d["flow"]])
    x = d["exit_transform"]
    exit_pt = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(ox + x["location_cm"][0], oy + x["location_cm"][1], oz + 50), unreal.Rotator(roll=0, pitch=0, yaw=x["yaw"]))
    finish(exit_pt, f"TDDungeonExit_{slot}", folder, ["TDGen", "TDDungeonExit"])
    size_cm = d["bounds"]["size_cm"]
    slot_center = unreal.Vector(ox + size_cm[0] * 0.5, oy + size_cm[1] * 0.5, oz + 100)
    marker = eas.spawn_actor_from_class(unreal.TargetPoint, slot_center, unreal.Rotator(0, 0, 0))
    finish(marker, f"TDDungeonSlot_{slot}", folder, ["TDGen", "TDDungeonSlot", d["theme"], d["flow"], f"seed{d['seed']}"])
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, slot_center, unreal.Rotator(0, 0, 0))
    nav.set_actor_scale3d(unreal.Vector(size_cm[0] / 200.0 + 4.0, size_cm[1] / 200.0 + 4.0, 6.0))
    finish(nav, f"{prefix}NavBounds", folder, ["TDGen"])
    log(f"slot {slot}: props {n_props} lights {n_lights} entry {e['location_cm']} exit {x['location_cm']}")


def main():
    if not LAYOUTS:
        log("no layouts under Saved/DungeonGen/slot*/layout.json")
        return
    world = ues.get_editor_world()
    log(f"level {world.get_name()} layouts {len(LAYOUTS)}")
    for p in LAYOUTS:
        bake(p)
    ok = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log(f"saved: {ok}")


main()

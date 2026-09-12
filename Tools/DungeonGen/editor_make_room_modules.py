"""에디터 안에서 실행: Crypt 테마 룸 모듈 12개를 플레이스홀더 지오메트리 레벨로 만들고 테마 에셋에 연결한다(P2-03).

규격: Docs/WorldDungeonPCG/room-module-spec.md (셀 400cm, 원점 = 셀(0,0) 모서리, 문 개구부 240cm, 남쪽 벽 낮음).
생성: Content/Dungeon/Rooms/Crypt/LI_TDRoom_Crypt_<Module>.umap  (바닥·벽·문 마커 TargetPoint·NavMeshBoundsVolume·PlayerStart 없음)
실행: python Tools/run_in_editor.py Tools/DungeonGen/editor_make_room_modules.py   (끝나면 원래 레벨을 다시 연다)
"""
import unreal

CELL = 400.0
WALL_H = 350.0
SOUTH_WALL_H = 120.0
WALL_T = 30.0
DOOR_W = 240.0
ROOM_DIR = "/Game/Dungeon/Rooms/Crypt"
CUBE = "/Engine/BasicShapes/Cube"
FLOOR_MAT = "/Game/DarkFantasyTopDown/Materials/Nature/Surfaces/MI_Bedrock2"
WALL_MAT = "/Game/DarkFantasyTopDown/Materials/Nature/MI_Cliffs2"
DIR_OFF = {"North": (0, -1), "East": (1, 0), "South": (0, 1), "West": (-1, 0)}

EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

theme = EAL.load_asset("/Game/Dungeon/Themes/DA_TDTheme_Crypt")
if theme is None:
    raise RuntimeError("DA_TDTheme_Crypt 없음: 먼저 Tools/WorldGen/editor_make_definitions.py 실행")
original_level = ues.get_editor_world().get_path_name().split(".")[0]
cube = EAL.load_asset(CUBE)
floor_mat = EAL.load_asset(FLOOR_MAT)
wall_mat = EAL.load_asset(WALL_MAT)


def place_cube(loc, scale, yaw, mat, label):
    actor = eas.spawn_actor_from_object(cube, loc, unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    actor.set_actor_scale3d(scale)
    actor.static_mesh_component.set_material(0, mat)
    actor.static_mesh_component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    actor.set_actor_label(label)
    return actor


def dir_name(d):
    return str(d).split(".")[-1].split(":")[0].replace("_", "").title() if "." in str(d) else str(d)


if not EAL.does_directory_exist(ROOM_DIR):
    EAL.make_directory(ROOM_DIR)

modules = list(theme.get_editor_property("modules"))
updated = []
for module in modules:
    mid = str(module.get_editor_property("module_id"))
    cells = [(c.x, c.y) for c in module.get_editor_property("cells")]
    sockets = []
    for s in module.get_editor_property("sockets"):
        c = s.get_editor_property("cell")
        d = s.get_editor_property("direction")
        name = {unreal.TDDoorDirection.NORTH: "North", unreal.TDDoorDirection.EAST: "East", unreal.TDDoorDirection.SOUTH: "South", unreal.TDDoorDirection.WEST: "West"}[d]
        sockets.append(((c.x, c.y), name))
    level_path = f"{ROOM_DIR}/LI_TDRoom_Crypt_{mid}"
    if EAL.does_asset_exist(level_path):
        EAL.delete_asset(level_path)
    les.new_level(level_path)
    cell_set = set(cells)
    for (cx, cy) in cells:
        center = unreal.Vector((cx + 0.5) * CELL, (cy + 0.5) * CELL, 0.0)
        place_cube(unreal.Vector(center.x, center.y, -10.0), unreal.Vector(CELL / 100.0, CELL / 100.0, 0.2), 0.0, floor_mat, f"Floor_{cx}_{cy}")
        for dname, (dx, dy) in DIR_OFF.items():
            if (cx + dx, cy + dy) in cell_set:
                continue
            has_door = ((cx, cy), dname) in sockets
            wx = center.x + dx * CELL * 0.5
            wy = center.y + dy * CELL * 0.5
            height = SOUTH_WALL_H if dname == "South" else WALL_H
            yaw = 90.0 if dx != 0 else 0.0
            if not has_door:
                place_cube(unreal.Vector(wx, wy, height * 0.5), unreal.Vector((CELL + WALL_T) / 100.0, WALL_T / 100.0, height / 100.0), yaw, wall_mat, f"Wall_{dname}_{cx}_{cy}")
            else:
                side = (CELL - DOOR_W) * 0.5
                for k, sgn in enumerate((-1, 1)):
                    off = sgn * (DOOR_W * 0.5 + side * 0.5)
                    loc = unreal.Vector(wx + (0 if dx != 0 else off), wy + (off if dx != 0 else 0), height * 0.5)
                    place_cube(loc, unreal.Vector((side + WALL_T) / 100.0, WALL_T / 100.0, height / 100.0), yaw, wall_mat, f"DoorJamb_{dname}_{cx}_{cy}_{k}")
                marker = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(wx, wy, 0.0), unreal.Rotator(0, 0, 0))
                marker.set_actor_label(f"Door_{dname[0]}_{cx}_{cy}")
                marker.tags = [unreal.Name("TDDoorSocket"), unreal.Name(dname)]
    xs = [c[0] for c in cells]
    ys = [c[1] for c in cells]
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector((max(xs) + 1) * CELL * 0.5, (max(ys) + 1) * CELL * 0.5, 150.0), unreal.Rotator(0, 0, 0))
    nav.set_actor_scale3d(unreal.Vector((max(xs) + 3) * CELL / 200.0, (max(ys) + 3) * CELL / 200.0, 4.0))
    nav.set_actor_label("NavBounds")
    les.save_current_level()
    level_asset = EAL.load_asset(level_path)
    if level_asset is not None:
        module.set_editor_property("level_asset", level_asset)
    updated.append(mid)
theme.set_editor_property("modules", modules)
EAL.save_loaded_asset(theme)
les.load_level(original_level)
print("room module levels created:", updated)

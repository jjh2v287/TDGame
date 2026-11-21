"""실행 환경: 에디터 안(Tools/run_in_editor.py), PIE 가 꺼져 있을 때만(PIE 중에는 에디터 에셋 함수가 None 을 돌려준다). editor_make_landscape_material.py 가 이 파일을 모듈로 불러 ensure_leaf_litter_plane() 을 쓴다.
실행 명령: python Tools/run_in_editor.py -c "import sys; sys.argv=['x','<report|apply|save>']; exec(open('C:/Project/TDGame/Tools/WorldGen/editor_make_leaf_litter_plane.py', encoding='utf-8').read())" (절대 경로)
출력: 표준 출력 LEAF_LITTER {...} 한 줄(낙엽 판 메시·높이·LOD 수, GT_TD_ForestFloor 낙엽 변종 메시, 저장한 패키지). apply 는 메모리에서만 만들고 바꾼 뒤 grass.FlushCache, save 는 apply 뒤 두 에셋을 저장한다.
상태: GF-07 숲 바닥 낙엽 판 Z-파이팅 고침(2026-11-21). 원본 SM_BirchLavesPlane 은 두께 0 판이라 풀 배치(삼각형 높이·삼각형 법선)로 지형 면과 정확히 겹친다 → 판을 LEAF_LITTER_LIFT_CM 만큼 띄운 복사본을 쓴다(스케일 2.5~5 배라 실제 3.75~7.5cm).
"""
import json
import sys

import unreal

SOURCE_MESH_PATH = "/Game/DarkFantasyTopDown/StaticMeshes/Birch/SM_BirchLavesPlane"
LEAF_LITTER_MESH_PATH = "/Game/World/Landscape/Grass/SM_TD_LeafLitterPlane"
FOREST_FLOOR_GRASS_PATH = "/Game/World/Landscape/Grass/GT_TD_ForestFloor"
LEAF_LITTER_LIFT_CM = 1.5
LEAF_MESH_NAMES = ("SM_BirchLavesPlane", "SM_TD_LeafLitterPlane")

EAL = unreal.EditorAssetLibrary


def read_lift(mesh):
    box = mesh.get_bounding_box()
    return box.min.z, box.max.z


def lift_vertices(description):
    for index in range(description.get_vertex_count()):
        vertex = unreal.VertexID()
        vertex.set_editor_property("id_value", index)
        position = description.get_vertex_position(vertex)
        description.set_vertex_position(vertex, unreal.Vector(position.x, position.y, LEAF_LITTER_LIFT_CM))


def ensure_leaf_litter_plane():
    mesh = EAL.load_asset(LEAF_LITTER_MESH_PATH) if EAL.does_asset_exist(LEAF_LITTER_MESH_PATH) else EAL.duplicate_asset(SOURCE_MESH_PATH, LEAF_LITTER_MESH_PATH)
    if mesh is None:
        raise RuntimeError("leaf litter plane unavailable (PIE running or source missing): " + SOURCE_MESH_PATH)
    if mesh.get_num_lods() == 1 and read_lift(mesh) == (LEAF_LITTER_LIFT_CM, LEAF_LITTER_LIFT_CM):
        return mesh
    description = mesh.get_static_mesh_description(0)
    if description is None:
        raise RuntimeError("no LOD0 mesh description: " + LEAF_LITTER_MESH_PATH)
    lift_vertices(description)
    mesh.build_from_static_mesh_descriptions([description], False, False)
    return mesh


def point_forest_floor_at(mesh):
    grass_type = EAL.load_asset(FOREST_FLOOR_GRASS_PATH)
    if grass_type is None:
        raise RuntimeError("grass type missing: " + FOREST_FLOOR_GRASS_PATH)
    varieties = list(grass_type.get_editor_property("grass_varieties"))
    for variety in varieties:
        current = variety.get_editor_property("grass_mesh")
        if current is not None and current.get_name() in LEAF_MESH_NAMES:
            variety.set_editor_property("grass_mesh", mesh)
    grass_type.modify()
    grass_type.set_editor_property("grass_varieties", varieties)
    return grass_type


def describe(mesh, grass_type):
    low, high = read_lift(mesh) if mesh else (None, None)
    leaf_meshes = [v.get_editor_property("grass_mesh").get_path_name() for v in grass_type.get_editor_property("grass_varieties") if v.get_editor_property("grass_mesh") and v.get_editor_property("grass_mesh").get_name() in LEAF_MESH_NAMES] if grass_type else []
    return {"mesh": mesh.get_path_name() if mesh else None, "lift_cm": [low, high], "lods": mesh.get_num_lods() if mesh else None, "forest_floor_leaf_meshes": leaf_meshes}


def main(mode):
    if mode == "report":
        mesh = EAL.load_asset(LEAF_LITTER_MESH_PATH) if EAL.does_asset_exist(LEAF_LITTER_MESH_PATH) else None
        return describe(mesh, EAL.load_asset(FOREST_FLOOR_GRASS_PATH))
    mesh = ensure_leaf_litter_plane()
    grass_type = point_forest_floor_at(mesh)
    unreal.SystemLibrary.execute_console_command(None, "grass.FlushCache")
    report = describe(mesh, grass_type)
    if mode == "save":
        report["saved"] = [path for path in (LEAF_LITTER_MESH_PATH, FOREST_FLOOR_GRASS_PATH) if EAL.save_asset(path, only_if_is_dirty=False)]
    return report


if __name__ == "__main__":
    print("LEAF_LITTER " + json.dumps(main(sys.argv[1] if len(sys.argv) > 1 else "report"), ensure_ascii=False))

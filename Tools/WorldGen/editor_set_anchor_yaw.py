"""에디터 안에서 실행: 월드 정의 DA_TDWorld_Main 의 손수 배치 앵커 회전(YawDeg)을 갱신한다(입구 접근로 방향 = 생성기 facing).

실행: python Tools/run_in_editor.py Tools/WorldGen/editor_set_anchor_yaw.py
"""
import unreal

YAWS = {"MainDungeon": 135.0}
EAL = unreal.EditorAssetLibrary
world_def = EAL.load_asset("/Game/World/Definitions/DA_TDWorld_Main")
anchors = list(world_def.get_editor_property("hand_authored_anchors"))
for a in anchors:
    aid = str(a.get_editor_property("anchor_id"))
    if aid in YAWS:
        a.set_editor_property("yaw_deg", YAWS[aid])
    print("anchor", aid, "yaw", a.get_editor_property("yaw_deg"))
world_def.set_editor_property("hand_authored_anchors", anchors)
print("saved:", EAL.save_loaded_asset(world_def))

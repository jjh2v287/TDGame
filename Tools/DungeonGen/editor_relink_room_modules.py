"""에디터 안에서 실행: 이미 만들어진 룸 모듈 레벨(Content/Dungeon/Rooms/Crypt/LI_TDRoom_Crypt_<Module>)을 테마 에셋에 다시 연결한다.

실행: python Tools/run_in_editor.py Tools/DungeonGen/editor_relink_room_modules.py
"""
import unreal

EAL = unreal.EditorAssetLibrary
theme = EAL.load_asset("/Game/Dungeon/Themes/DA_TDTheme_Crypt")
modules = list(theme.get_editor_property("modules"))
linked = []
for m in modules:
    mid = str(m.get_editor_property("module_id"))
    level = EAL.load_asset(f"/Game/Dungeon/Rooms/Crypt/LI_TDRoom_Crypt_{mid}")
    if level is not None:
        m.set_editor_property("level_asset", level)
        linked.append(mid)
theme.set_editor_property("modules", modules)
EAL.save_loaded_asset(theme)
print("relinked:", linked)

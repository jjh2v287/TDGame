from pathlib import Path

root = Path('C:/Project/TDGame')
namespace = {}
exec(compile((root / 'Tools/BlenderAnimation/author_player_slash.py').read_text(encoding='utf-8'),
             'author_player_slash.py', 'exec'), namespace)
namespace['author']({
    'source_fbx': str(root / 'Saved/BlenderAnimation/SKM_Manny_Simple.fbx'),
    'output_directory': str(root / 'AnimationSources/Player'),
    'name': 'AS_TD_Player_Attack01_RToL_Blender',
    'mesh_asset': '/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple',
    'skeleton_asset': '/Game/Characters/Mannequins/Meshes/SK_Mannequin',
})

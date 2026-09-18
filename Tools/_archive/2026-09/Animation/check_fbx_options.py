import unreal
options = unreal.FbxImportUI()
print('import_mesh:', hasattr(options, 'import_mesh'))
print('import_animations:', hasattr(options, 'import_animations'))
print('mesh_type_to_import:', hasattr(options, 'mesh_type_to_import'))
for k in dir(unreal):
    if 'FBXImport' in k or 'FBXMesh' in k:
        print('Found enum/class:', k)
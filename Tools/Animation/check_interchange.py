
import unreal
mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
print('InterchangeManager:', mgr)
for m in dir(mgr):
    if 'import' in m.lower():
        print('Interchange method:', m)

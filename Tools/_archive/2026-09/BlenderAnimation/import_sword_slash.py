"""시스템 Python으로 실행(언리얼 에디터 열림 필요): AnimationSources/Player의 검 횡베기 FBX를 SK_Mannequin에 새 AnimSequence로 가져오고 루트 모션을 켠 뒤 DefaultSlot 몽타주(섹션 Attack01)를 만든다.
실행: python Tools/BlenderAnimation/import_sword_slash.py [--name AS_TD_Player_Attack01_SwordSlash_RToL]
출력: /Game/Characters/Mannequins/Anims/Blender/<name>, AM_<name 접미>, Saved/BlenderAnimation/SwordSlash/import-result.json
상태: 현행 (2026-09-18)
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools/AnimationAuthoring'))
from smoke_test import TDMcpAnimationClient  # noqa: E402

FOLDER = '/Game/Characters/Mannequins/Anims/Blender'
SKELETON = '/Game/Characters/Mannequins/Meshes/SK_Mannequin'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--name', default='AS_TD_Player_Attack01_SwordSlash_RToL')
    args = parser.parse_args()
    montage_name = 'AM_' + args.name[3:]
    client = TDMcpAnimationClient()
    result = {'sequence': client.call(
        '.TDBlenderAnimationTools.import_animation_fbx',
        source_file=str(ROOT / 'AnimationSources/Player' / (args.name + '.fbx')),
        destination_folder=FOLDER, asset_name=args.name, skeleton_path=SKELETON,
        sample_rate=30, import_uniform_scale=1.0, preserve_local_transform=True)}
    root_motion_script = (
        "import unreal, json\n"
        f"seq = unreal.EditorAssetLibrary.load_asset('{FOLDER}/{args.name}')\n"
        "seq.set_editor_property('enable_root_motion', True)\n"
        "seq.set_editor_property('root_motion_root_lock', unreal.RootMotionRootLock.REF_POSE)\n"
        "seq.set_editor_property('force_root_lock', False)\n"
        "saved = unreal.EditorAssetLibrary.save_loaded_asset(seq)\n"
        "print(json.dumps({'root_motion': seq.get_editor_property('enable_root_motion'), 'saved': saved,"
        " 'length': seq.get_editor_property('sequence_length') if hasattr(seq, 'sequence_length') else None}))\n")
    script_path = ROOT / 'Saved/BlenderAnimation/SwordSlash/editor_enable_root_motion.py'
    script_path.write_text(root_motion_script, encoding='utf-8')
    completed = subprocess.run([sys.executable, str(ROOT / 'Tools/run_in_editor.py'), str(script_path)], capture_output=True, text=True, cwd=ROOT)
    result['root_motion'] = {'returncode': completed.returncode, 'stdout': completed.stdout[-800:], 'stderr': completed.stderr[-400:]}
    result['montage'] = client.request('.TDAnimationAuthoringTools.CreateMontage', {
        'asset_path': f'{FOLDER}/{montage_name}', 'slot': 'DefaultSlot', 'save': True, 'blend_in': 0.1, 'blend_out': 0.2,
        'segments': [{'sequence': f'{FOLDER}/{args.name}'}], 'sections': [{'name': 'Attack01', 'time': 0}]})
    (ROOT / 'Saved/BlenderAnimation/SwordSlash/import-result.json').write_text(json.dumps(result, indent=1), encoding='utf-8')
    print(json.dumps(result)[:3000])
    if not result['montage'].get('success') or completed.returncode != 0:
        raise SystemExit(1)


if __name__ == '__main__':
    main()

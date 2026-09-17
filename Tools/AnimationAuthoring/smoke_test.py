import argparse
import json
import re
from pathlib import Path
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import uemcp


def unwrap(value):
    while True:
        if isinstance(value, str):
            try:
                value = json.loads(value)
                continue
            except json.JSONDecodeError:
                return value
        if isinstance(value, dict) and len(value) == 1:
            key = next(iter(value))
            if key.lower().replace('_', '') == 'returnvalue':
                value = value[key]
                continue
        return value


class TDMcpAnimationClient:
    def __init__(self):
        self.session = uemcp.connect()
        self.tools = uemcp.list_tools(self.session)
        self.discovery = any(tool['name'] == 'describe_toolset' for tool in self.tools)
        self.toolsets = []
        if self.discovery:
            listing = self.invoke('list_toolsets', {})
            self.toolsets = re.findall(r'^- ([^:\n]+):', listing, re.MULTILINE)

    def invoke(self, name, args):
        response = uemcp.call_tool(self.session, name, args)
        if not response or 'error' in response:
            raise RuntimeError(f'{name}: {response}')
        result = response['result']
        texts = [entry['text'] for entry in result.get('content', []) if entry['type'] == 'text']
        if result.get('isError') or len(texts) != 1:
            raise RuntimeError(f'{name}: {texts}')
        return unwrap(texts[0])

    def call(self, suffix, **arguments):
        matches = [tool for tool in self.tools if tool['name'].endswith(suffix)]
        if not matches and self.discovery:
            toolset_suffix = suffix.rsplit('.', 1)[0]
            toolset_names = [name for name in self.toolsets if name.endswith(toolset_suffix)]
            if len(toolset_names) != 1:
                raise RuntimeError(f'{suffix}: expected one toolset, found {toolset_names}')
            description = self.invoke('describe_toolset', {'toolset_name': toolset_names[0]})
            self.tools.extend(description['tools'])
            matches = [tool for tool in self.tools if tool['name'].endswith(suffix)]
        if len(matches) != 1:
            raise RuntimeError(f'{suffix}: expected one tool, found {len(matches)}')
        tool = matches[0]
        properties = tool['inputSchema'].get('properties', {})
        normalized = {key.lower().replace('_', ''): key for key in properties}
        args = {normalized[key.lower().replace('_', '')]: value for key, value in arguments.items()}
        if self.discovery:
            toolset_name, tool_name = tool['name'].rsplit('.', 1)
            return self.invoke('call_tool', {'toolset_name': toolset_name, 'tool_name': tool_name, 'arguments': args})
        return self.invoke(tool['name'], args)

    def request(self, suffix, request):
        return self.call(suffix, RequestJson=json.dumps(request, allow_nan=False))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mesh', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--save', action='store_true')
    args = parser.parse_args()
    client = TDMcpAnimationClient()
    root = '/Game/Tests/AnimationAuthoring/Run_' + uuid.uuid4().hex[:12]
    report = {'mesh': args.mesh, 'save': args.save, 'asset_root': root, 'checks': [], 'assets': []}

    def check(name, condition):
        if not condition:
            raise AssertionError(name)
        report['checks'].append(name)
        print('PASS:', name, flush=True)

    def succeeded(name, result):
        check(name, isinstance(result, dict) and result.get('success') is True)
        return result

    try:
        skeleton = succeeded('inspect skeleton', client.call('.TDAnimationAuthoringTools.InspectSkeleton', SkeletalMeshPath=args.mesh))
        bone = skeleton['bones'][0]['name']
        animation_request = {
            'asset_path': root + '/AS_BonePulse', 'skeletal_mesh': args.mesh,
            'fps': 30, 'num_frames': 30, 'save': args.save,
            'tracks': [{'bone': bone, 'keys': [
                {'frame': 0}, {'frame': 15, 'translation': [0, 0, 20]}, {'frame': 30}
            ]}]
        }
        invalid = dict(animation_request, fps=30.5)
        rejected = client.request('.TDAnimationAuthoringTools.CreateBoneAnimation', invalid)
        check('reject fractional frame rate before allocation', rejected.get('success') is False)
        created = succeeded('create bone animation', client.request('.TDAnimationAuthoringTools.CreateBoneAnimation', animation_request))
        report['assets'].append(created['asset_path'])
        rejected = client.request('.TDAnimationAuthoringTools.CreateBoneAnimation', animation_request)
        check('reject destination collision', rejected.get('success') is False)
        inspected = succeeded('inspect bone animation', client.call('.TDAnimationAuthoringTools.InspectAnimation', AssetPath=created['asset_path']))
        track = next(track for track in inspected['tracks'] if track['bone'] == bone)
        samples = track['samples']
        check('bone midpoint moves 20 cm and returns', abs(samples[1]['translation'][2] - samples[0]['translation'][2] - 20) < 0.001 and abs(samples[-1]['translation'][2] - samples[0]['translation'][2]) < 0.001)
        check('bone duration is one second', inspected['num_frames'] == 30 and inspected['fps'] == 30)

        # 몽타주 슬롯은 스켈레톤에 이미 있어야 한다. InspectSkeleton이 보고한 슬롯 중 하나를 쓴다.
        slots = [entry['slot'] for entry in skeleton.get('slots', []) if entry.get('slot')]
        check('skeleton reports at least one montage slot', bool(slots))
        slot = 'DefaultSlot' if 'DefaultSlot' in slots else slots[0]
        montage_request = {
            'asset_path': root + '/AM_BonePulse', 'save': args.save, 'slot': slot,
            'segments': [{'sequence': created['asset_path']}, {'sequence': created['asset_path']}],
            'sections': [{'name': 'Start', 'time': 0, 'next': 'Return'}, {'name': 'Return', 'time': 1}, {'name': 'Final', 'time': 1.5}]
        }
        rejected = client.request('.TDAnimationAuthoringTools.CreateMontage', dict(montage_request, slot='TD_Missing_Test_Slot'))
        check('reject unknown montage slot', rejected.get('success') is False)
        montage = succeeded('create two-segment montage', client.request('.TDAnimationAuthoringTools.CreateMontage', montage_request))
        report['assets'].append(montage['asset_path'])
        montage_info = succeeded('inspect montage', client.call('.TDAnimationAuthoringTools.InspectAnimation', AssetPath=montage['asset_path']))
        check('montage has two segments', len(montage_info['slots']) == 1 and len(montage_info['slots'][0]['segments']) == 2)
        check('montage preserves explicit section links', montage_info['sections'][0]['next'] == 'Return' and montage_info['sections'][1]['next'] == '')

        fk = succeeded('create native FK sequence', client.request('.TDControlRigTools.CreateFKSequence', {
            'asset_path': root + '/LS_FKPulse', 'skeletal_mesh': args.mesh,
            'fps': 30, 'num_frames': 31, 'save': args.save
        }))
        report['assets'].append(fk['asset_path'])
        sequence_ref = {'refPath': fk['asset_path']}
        check('open FK sequence', client.call('.sequencer.SequencerTools.open_sequence', sequence=sequence_ref) is True)
        sequence_info = succeeded('inspect sequence bindings', client.call('.TDSequencerAnimationTools.InspectSequence', SequencePath=fk['asset_path']))
        report['sequence_info'] = sequence_info
        rig_path = '/Script/ControlRig.FKControlRig'
        controls = client.call('.controlrig_sequencer.SequencerControlRigTools.get_controls_info', sequence=sequence_ref, control_rig_asset_path=rig_path)
        # UE 5.8 FKControlRig의 컨트롤 이름은 `<본 이름>_CONTROL`이다.
        expected = {(bone + suffix).lower() for suffix in ('_CONTROL', '_ctrl')}
        candidates = [control['name'] for control in controls if control['name'].lower() in expected]
        check('native FK has a root bone control', len(candidates) == 1)
        control = candidates[0]
        baseline = client.call('.controlrig_sequencer.SequencerControlRigTools.get_transform', sequence=sequence_ref, control_rig_asset_path=rig_path, control_name=control, frame=0)
        for frame, height in [(0, 0), (15, 20), (30, 0)]:
            location, rotation = baseline['location'], baseline['rotation']
            keyed = client.call('.controlrig_sequencer.SequencerControlRigTools.set_transform',
                sequence=sequence_ref, control_rig_asset_path=rig_path, control_name=control,
                frame=frame, location_x=location['x'], location_y=location['y'], location_z=location['z'] + height,
                rotation_pitch=rotation['pitch'], rotation_yaw=rotation['yaw'], rotation_roll=rotation['roll'], set_key=True)
            check(f'key native FK frame {frame}', keyed is True)
        middle = client.call('.controlrig_sequencer.SequencerControlRigTools.get_transform', sequence=sequence_ref, control_rig_asset_path=rig_path, control_name=control, frame=15)
        check('FK key reads back 20 cm motion', abs(middle['location']['z'] - baseline['location']['z'] - 20) < 0.001)
        baked = succeeded('bake FK into new animation', client.request('.TDSequencerAnimationTools.BakeAnimation', {
            'sequence': fk['asset_path'], 'binding': fk['binding'], 'skeletal_mesh': args.mesh,
            'asset_path': root + '/AS_FKBaked', 'save': args.save
        }))
        report['assets'].append(baked['asset_path'])
        baked_info = succeeded('inspect baked animation', client.call('.TDAnimationAuthoringTools.InspectAnimation', AssetPath=baked['asset_path']))
        baked_track = next(track for track in baked_info['tracks'] if track['bone'] == bone)
        baked_samples = baked_track['samples']
        check('baked animation contains root motion keys', abs(baked_samples[1]['translation'][2] - baked_samples[0]['translation'][2]) > 10)
        check('baked duration is one second with final pose retained', baked_info['num_frames'] == 30 and abs(baked_samples[-1]['translation'][2] - baked_samples[0]['translation'][2]) < 0.001)
        report['baked_root_samples'] = baked_samples
        report['success'] = True
    except Exception as error:
        report['success'] = False
        report['error'] = str(error)
        raise
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print('Report:', args.output.resolve(), flush=True)


if __name__ == '__main__':
    main()

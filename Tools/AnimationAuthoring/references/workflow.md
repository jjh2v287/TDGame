# Animation authoring workflow

## 접속 및 도구 탐색

프로젝트 루트에서 기존 `Tools/uemcp.py`를 사용할 수 있다. 이 클라이언트는 로컬 Unreal MCP의 Streamable HTTP에 연결한다. 클라이언트에 MCP 기능이 있으면 같은 주소로 직접 연결해도 된다.

```powershell
# Context: C:/Project/TDGame, read-only MCP discovery
python Tools/uemcp.py list
python Tools/uemcp.py desc '<EXACT_NAME_RETURNED_BY_LIST>'
```

`TDAnimationAuthoringTools`, `TDControlRigTools`, `TDSequencerAnimationTools`, `SequencerTools`, `SequencerControlRigTools`를 검색어로 사용한다. 각 도구의 MCP 이름·대소문자·객체 참조 직렬화는 실제 `tools/list`의 `inputSchema`가 기준이다. 세션이 바뀌면 임시 UObject 참조도 다시 조회한다.

### 검색 모드

현재 TDGame 에디터는 `tools/list`에 `list_toolsets`, `describe_toolset`, `call_tool`만 노출하는 검색 모드도 사용한다. 이 경우 TD 도구가 직접 보이지 않아도 없는 것으로 판단하지 않는다.

1. `list_toolsets`를 호출하고 반환된 목록에서 toolset 이름을 찾는다.
2. `describe_toolset`에 `{"toolset_name":"<목록의 정확한 이름>"}`을 전달한다. 응답의 `tools`에서 전체 도구 이름과 `inputSchema`를 읽는다.
3. `call_tool`에 `toolset_name`, 짧은 함수 이름 `tool_name`, 스키마에 맞는 `arguments`를 전달한다. `RequestJson`의 실제 매개변수 이름은 현재 스키마에서 `requestJson`이다.

아래는 로컬 세션에서 확인한 FK toolset을 조회하고 호출하는 예시다. JSON 블록의 첫 설명 주석을 빼고 각각 지정 파일에 저장한다. 도구 이름은 실행 시 발견한 이름과 대조하고 메쉬는 사전 조회한 경로를 넣는다.

```jsonc
// File: Tools/AnimationAuthoring/describe-args.json
{"toolset_name":"TDGameEditor.TDControlRigTools"}
```

```jsonc
// File: Tools/AnimationAuthoring/call-args.json
{
  "toolset_name": "TDGameEditor.TDControlRigTools",
  "tool_name": "CreateFKSequence",
  "arguments": {
    "requestJson": "{\"asset_path\":\"/Game/AnimationAuthoring/LS_FKStudy_v001\",\"skeletal_mesh\":\"<INSPECTED_MESH_PATH>\",\"fps\":30,\"num_frames\":30,\"save\":true}"
  }
}
```

```powershell
# Context: C:/Project/TDGame, arguments files prepared as shown above
python Tools/uemcp.py call list_toolsets
python Tools/uemcp.py call describe_toolset '@Tools/AnimationAuthoring/describe-args.json'
python Tools/uemcp.py call call_tool '@Tools/AnimationAuthoring/call-args.json'
```

`uemcp.py desc`는 최상위 도구를 설명하므로 검색 모드에서는 개별 함수 대신 `describe_toolset`을 사용한다. 개별 도구를 직접 노출하는 서버에서는 `tools/list`에서 찾은 전체 이름으로 바로 호출한다.

### TD 도구 목록

현재 C++ 도구는 아래 7개다. 클래스 이름은 탐색 기준이며 MCP 등록 접두사는 런타임 목록에서 찾는다.

| C++ 도구 | 역할 |
| --- | --- |
| `UTDAnimationAuthoringTools::InspectSkeleton` | 메쉬의 Skeleton, 본, 부모, 레퍼런스 포즈, 몽타주 슬롯 조회 |
| `UTDAnimationAuthoringTools::CreateBoneAnimation` | 직접 본 키로 새 AnimSequence 생성 |
| `UTDAnimationAuthoringTools::CreateMontage` | 새 AnimMontage와 세그먼트·섹션 생성 |
| `UTDAnimationAuthoringTools::InspectAnimation` | AnimSequence 또는 AnimMontage 결과 조회 |
| `UTDControlRigTools::CreateFKSequence` | 새 LevelSequence, 메쉬 spawnable, C++ FK 리그 생성 |
| `UTDSequencerAnimationTools::InspectSequence` | 바인딩 GUID, 메쉬, FPS, 재생 범위 조회 |
| `UTDSequencerAnimationTools::BakeAnimation` | 명시한 바인딩을 새 AnimSequence로 베이크 |

도구 호출용 JSON은 파일로 저장하고 `python Tools/uemcp.py call '<EXACT_TOP_LEVEL_TOOL_NAME>' '@<ARGUMENTS_FILE>'`로 넘긴다. `RequestJson` 도구의 인수는 요청 객체 자체가 아니라 **JSON으로 직렬화된 문자열 한 개**다. 바깥 인수 이름은 `inputSchema`에서 확인한다. 결과에는 MCP 오류 외에 JSON 문자열 내부의 `success`와 `error`가 있을 수 있다.

TD 도구의 `SequencePath` 등 문자열 인수와 엔진 도구의 UObject 인수를 구별한다. 엔진의 `sequence` 인수는 실제 에셋 오브젝트 경로를 담은 `{"refPath":"/Game/.../LS_Name.LS_Name"}` 형태로 전달한다. 생성 응답의 `asset_path`를 보관해 재사용하고 임의의 임시 객체 ID를 만들지 않는다. 이 참조는 현재 에디터에 존재하는 미저장 에셋에도 적용되지만, 미저장 에셋은 에디터 재시작 후 남는다고 가정하지 않는다.

연결이 거부되면 중복 에디터 프로세스가 없는지 확인하고 TDGame 에디터를 실행한다. 이미 실행 중이라면 프로젝트·포트·ModelContextProtocol 플러그인 상태를 확인한다. 목록에 새 TD 클래스가 없으면 에디터 모듈의 컴파일·로드 상태를 확인한다. 임의의 엔진 API 이름을 만들어 호출하지 않는다.

## C++ 본 애니메이션

`UTDAnimationAuthoringTools::InspectSkeleton(SkeletalMeshPath)`에서 실제 본 이름, 부모, 레퍼런스 로컬 포즈와 Skeleton을 확인한다. `CreateBoneAnimation(RequestJson)`은 리그 없이 본 트랙으로 새 `AnimSequence`를 만든다.

| 요청 필드 | 의미 |
| --- | --- |
| `asset_path` | 생성할 새 `/Game/.../AS_Name` 패키지 경로. 기존 목적지는 거부한다. |
| `skeletal_mesh` | 조회로 확인한 SkeletalMesh 에셋 경로. |
| `fps` | 정수 프레임 레이트. 예: `30`. |
| `num_frames` | 프레임 간격 개수. `30`이면 키 범위 `0..30`, 총 `31`샘플, 길이 `1`초 at 30fps. |
| `mode` | `reference_offset` 또는 `local_absolute`. |
| `save` | 생성 결과 저장 여부. |
| `tracks` | `{bone, keys}` 목록. `bone`은 조회된 이름. |
| `keys` | `{frame, translation?, rotation?, scale?}` 목록. |

키의 `translation`은 센티미터 `[x,y,z]`, `rotation`은 도 단위 `[pitch,yaw,roll]`, `scale`은 `[x,y,z]`다. `frame`은 `0..num_frames` 범위다. 희소 키 사이 이동·크기는 선형 보간, 회전은 quaternion slerp를 사용한다. 첫 키 이전과 마지막 키 이후는 가장 가까운 키를 유지한다.

`reference_offset`은 레퍼런스 로컬 위치에 `translation`을 더하고, 회전을 `RefQuat * OffsetQuat`로 합성하고, 스케일을 곱한다. 따라서 이동 오프셋은 부모 축, 회전 오프셋은 해당 본의 로컬 축 기준이다. `local_absolute`는 레퍼런스에 더하지 않고 로컬 값을 지정한다. 생략 채널은 오프셋 모드에서 위치·회전 `0`/스케일 `1`, 절대 모드에서 레퍼런스 채널을 사용한다. 생략 채널이 직전 키 값을 자동 상속한다고 생각하지 않는다. 작성하지 않은 본은 레퍼런스 포즈를 유지한다.

아래는 요청 **템플릿**이다. 메쉬와 본 자리표시자를 `InspectSkeleton` 응답으로 교체한 뒤 JSON으로 직렬화한다. 예제의 작은 회전은 API 형태 설명이며 특정 캐릭터의 완성된 제스처가 아니다.

```jsonc
// Context: CreateBoneAnimation RequestJson payload template
{
  "asset_path": "/Game/AnimationAuthoring/AS_PoseStudy_v001",
  "skeletal_mesh": "<SKELETAL_MESH_FROM_INSPECTION>",
  "fps": 30,
  "num_frames": 30,
  "mode": "reference_offset",
  "save": true,
  "tracks": [{
    "bone": "<BONE_FROM_INSPECTION>",
    "keys": [
      {"frame": 0, "rotation": [0, 0, 0]},
      {"frame": 15, "rotation": [10, 0, 0]},
      {"frame": 30, "rotation": [0, 0, 0]}
    ]
  }]
}
```

JSON 파일에는 위 설명 주석을 넣지 않는다. 생성 후 `InspectAnimation(AssetPath)`으로 다시 읽는다. 루트 본의 위치 키 작성과 애니메이션의 root-motion 추출/게임플레이 적용은 별도다. 요청에 없는 게임 코드나 AnimBlueprint 실행 로직은 추가하지 않는다.

## Control Rig → Sequencer → AnimSequence

리그가 없는 캐릭터에는 `UTDControlRigTools::CreateFKSequence(RequestJson)`를 사용한다. 메쉬를 담은 spawnable 템플릿과 C++ `FKControlRig` 트랙을 새 LevelSequence 안에 만들며 레벨에 영구 액터를 추가하거나 Blueprint 그래프를 변경하지 않는다.

| 요청 필드 | 의미 |
| --- | --- |
| `asset_path` | 새 `/Game/.../LS_Name` 패키지 경로. 기존 목적지는 거부한다. |
| `skeletal_mesh` | 조회한 SkeletalMesh 패키지 또는 오브젝트 경로. |
| `fps` | 정수 `1..240`, 기본 `30`. |
| `num_frames` | 정수 `2..18000`, 기본 `30`. 재생 프레임은 `0..num_frames-1`, 끝 경계는 exclusive. |
| `save` | 생성 당시 시퀀스 저장 여부, 기본 `true`. 후속 키 편집은 다시 저장해야 한다. |

응답의 `asset_path`, `binding`, `control_rig_asset_path`, `controls`를 보관한다. FK의 `control_rig_asset_path`는 `/Script/ControlRig.FKControlRig`다. 이 값은 기존 트랙을 찾는 키 입력 도구에 전달하며 ControlRigBlueprint 생성 입력으로 쓰지 않는다. FK 트랜스폼은 레퍼런스 포즈에서의 로컬 오프셋이다.

아래 Python 함수명과 시그니처는 UE 5.8 로컬 `AnimationAssistantToolset` 소스에서 확인한 탐색 기준이다. 실제 MCP 호출은 현재 스키마로 작성한다. 새 FK 시퀀스는 이미 FPS·범위·바인딩·리그 구성을 완료하므로 열기와 컨트롤 조회부터 진행한다.

| 역할 | 엔진 도구 함수 |
| --- | --- |
| LevelSequence 생성 | `SequencerTools.create_level_sequence(package_path, asset_name)` |
| 시퀀서 열기 | `SequencerTools.open_sequence(sequence)` |
| 프레임 레이트 | `SequencerTools.set_display_rate(sequence, numerator, denominator=1)` |
| 재생 범위 | `SequencerTools.set_playback_range(sequence, start_frame, end_frame)` |
| 캐릭터 바인딩 | `SequencerTools.add_actors(actors)` 또는 `SequencerTools.add_spawnable_from_instance(sequence, obj)`; 두 번째 인수 이름은 `obj`다 |
| 기존 리그 연결 | `SequencerControlRigTools.find_or_create_track(sequence, binding, control_rig_asset_path, is_layered=False)` |
| 리그 조회 | `SequencerControlRigTools.get_control_rigs(sequence)` |
| 컨트롤 이름·타입 | `SequencerControlRigTools.get_controls_info(sequence, control_rig_asset_path)` |
| 포즈 조회 | `SequencerControlRigTools.get_transform(sequence, control_rig_asset_path, control_name, frame)` |
| 포즈 키 | `SequencerControlRigTools.set_transform(sequence, control_rig_asset_path, control_name, frame, location_x=0, location_y=0, location_z=0, rotation_pitch=0, rotation_yaw=0, rotation_roll=0, set_key=True)` |
| 대량 키 | `SequencerControlRigTools.key_controls_at_frames(section, control_names, frames)` — 컨트롤 여러 개를 여러 프레임에 한 번에 키잉한다. 키 개수가 많으면 `set_transform` 반복 대신 이쪽을 쓴다 |
| 타입별 키 | 같은 toolset의 `set_float`, `set_bool`, `set_int`, `set_position`, `set_rotator`, `set_scale`, `set_euler_transform` |
| 평가 갱신 | `SequencerTools.force_evaluate()` |

1. `CreateFKSequence`로 새 FK 시퀀스를 만들거나, 기존 리그를 연결할 새 LevelSequence와 대상 캐릭터 바인딩을 만든다. 기존 레벨 액터를 사용할 때는 그 레벨 저장이 필요해지는지 확인한다. 작업용 시퀀스는 한 캐릭터·한 리그로 좁히면 바인딩 혼동을 줄일 수 있다.
2. 시퀀스를 연다. FPS와 재생 범위를 조회하여 키가 범위 안에 있는지 확인한다. FK의 `num_frames=30`은 `0..29`이고 직접 본 애니메이션의 `num_frames=30`은 `0..30`이다. 재생 범위와 베이크 샘플 간격은 같다고 가정하지 말고 베이크 후 실제 길이를 조회한다.
3. 캐릭터와 호환되는 기존 ControlRigBlueprint를 연결할 때만 `find_or_create_track`을 사용한다. 이 함수는 에셋을 로드하고 `get_control_rig_class()`를 호출하므로 **C++ `FKControlRig` 클래스 경로를 이 함수에 에셋처럼 넘기지 않는다.**
4. `get_controls_info`로 타입을 확인하고, 기존 로컬 포즈를 `get_transform` 등으로 조회한다. 바꾸지 않을 위치·회전은 명시적으로 보존한다. `set_transform`에는 **스케일 인수가 없다.** 인수는 `location_x/y/z`, `rotation_pitch/yaw/roll`, `set_key`뿐이며 생략 값은 전부 `0`이다. 따라서 부분 회전 수정에 기본값을 그대로 쓰면 기존 위치를 지운다. 스케일은 `set_scale` 또는 `set_euler_transform`으로만 설정한다.
5. 주요 포즈와 중간 키를 입력한다. `set_key=True`로 저장된 키를 만든다. 조회와 시각 확인을 반복하며 조정한다. 같은 리그가 여러 바인딩에 있으면 경로 부분 일치로 첫 리그를 선택하는 엔진 래퍼의 동작에 주의한다.
6. `EditorToolset.AssetTools.save_assets(asset_paths)`로 **키를 수정한 LevelSequence를 저장한다.** 생성 시의 `save=true`는 후속 변경을 저장하지 않으며 베이크의 `save`는 목적지 AnimSequence만 저장한다. `save=false`로 작업 중이면 미저장 상태임을 유지·보고한다.
7. `UTDSequencerAnimationTools::InspectSequence(SequencePath)`에서 대상 바인딩 GUID와 메쉬를 확인한다. `BakeAnimation(RequestJson)`에 `sequence`, `binding`, `skeletal_mesh`, 새 `asset_path`, `save`를 전달한다. 네이티브 spawnable 바인딩은 Sequencer를 열지 않아도 베이크할 수 있다. 그 밖의 바인딩은 해당 시퀀스를 열고 포커스한 상태에서 캐릭터가 존재하는 프레임을 평가해야 한다. 바인딩은 요청 메쉬와 일치하는 스켈레탈 컴포넌트 하나로 해석되어야 한다.
8. 베이크된 `AnimSequence`의 Skeleton과 길이를 조회하고 동일 프레임의 시각 결과를 원래 시퀀스와 비교한다. 몽타주가 요청되었으면 아래의 생성 도구를 사용한다.

엔진의 `SequencerImportExportTools.export_anim_sequence(world, sequence, anim_sequence, binding, create_link=False)`는 **이미 있는 목적지 AnimSequence에 기록**한다. TD 베이크 도구가 없는 환경에서만 새 목적지 에셋을 먼저 만든 다음 이 도구를 사용한다. 목적지 생성과 Skeleton 설정이 확인되기 전에는 내보내지 않는다. 기존 사용자 애니메이션을 목적지로 재사용하지 않는다. 이 래퍼는 기본 `AnimSeqExportOption`을 사용하며 별도 내보내기 옵션 인수가 없다.

TD FK 도구가 목록에 없다면 `TDGameEditor` 모듈의 빌드·로드 상태를 확인한다. 연결 또는 모듈 문제로 FK 경로를 사용할 수 없으면 그 원인을 보고하고 `CreateBoneAnimation` 경로로 작업한다. Blueprint 그래프를 새로 만들어 우회하지 않는다.

## AnimMontage

`UTDAnimationAuthoringTools::CreateMontage(RequestJson)`은 새 몽타주를 만들고 순서대로 세그먼트를 배치한다.

| 요청 필드 | 의미 |
| --- | --- |
| `asset_path` | 새 `/Game/.../AM_Name` 경로. |
| `slot` | Skeleton에 이미 있는 슬롯 이름. 기본 `DefaultSlot`도 존재 여부 확인. |
| `save` | 저장 여부. |
| `blend_in`, `blend_out` | 초 단위 블렌드 시간. |
| `segments` | `{sequence, start_time, end_time, play_rate, loop_count}` 목록. 모든 AnimSequence의 Skeleton은 동일해야 한다. |
| `sections` | 선택적 `{name, time, next?}` 목록. 생략 시 `Start` 섹션. |

`start_time`/`end_time`은 각 원본 AnimSequence의 초 단위 구간이고 섹션 `time`은 몽타주 타임라인의 초 단위 위치다. `next`는 실제 섹션 이름을 사용한다. 몽타주 재생을 위한 캐릭터의 C++/AnimGraph 슬롯 연결은 별도 통합 작업이며 에셋 생성만으로 런타임 재생을 보장하지 않는다. 노티파이·커브·루트 모션·IK·리타게팅이 필요하면 해당 기능이 현재 도구로 지원되는지 먼저 확인한다.

## 검증과 제한

- 도구 조회: 경로, 타입, Skeleton, 길이, 프레임, 본 트랙, 세그먼트·섹션을 확인한다. 저장 뒤 재조회한다.
- 시각 검증: 시작/중간/최대 동작/끝과 실제 재생을 확인한다. 루프는 끝→시작, 걷기는 접지·발 미끄러짐, 공격은 궤적·관통을 살핀다. 가능한 에디터 캡처 도구를 검색해 근거를 남긴다.
- 변경 검증: `git status --short`, 새 `.uasset`의 `git check-attr filter -- <path>`, `git lfs status`를 확인한다. 기존 사용자 변경과 생성물은 함께 저장·커밋하지 않는다.
- 이 도구는 에이전트가 정한 포즈를 에셋으로 작성한다. 자연어에서 고품질 모션을 추론하는 전용 모델, 모션캡처, 물리적 접지 보정은 포함하지 않는다. 엔진 실험적 toolset 버전과 리그 구조에 따라 기능 차이가 있다.
- 실제 실행하지 않은 Control Rig/베이크 조합, 시각 확인, 런타임 몽타주 재생은 검증된 것처럼 보고하지 않는다.

## 도구 연결 검증

프로젝트 루트의 스모크 테스트는 메쉬 조회, 직접 본 애니메이션·몽타주 생성, FK 생성·키 입력·베이크와 잘못된 입력 거부를 확인한다. 각 실행은 고유한 `/Game/Tests/AnimationAuthoring/Run_<id>` 경로를 사용한다.

```powershell
# Context: C:/Project/TDGame, running Unreal Editor required
python Tools/AnimationAuthoring/smoke_test.py --mesh /Game/Characters/Mannequins/Meshes/SKM_Manny_Simple --output Tools/AnimationAuthoring/smoke-report.json
```

`--output`은 JSON 검증 보고서 경로다. 기본값은 에디터 메모리에 에셋을 생성하며 `--save`를 추가하면 생성 에셋을 저장한다. 저장한 테스트 에셋은 Git/LFS 변경 검사를 거친다. 테스트 실행 자체가 시각적 모션 품질이나 게임에서의 재생을 검증하지는 않는다. 보고서의 실제 결과를 확인하고 통과 여부를 보고한다.

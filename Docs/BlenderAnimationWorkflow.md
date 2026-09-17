# Blender MCP 전투 애니메이션 제작

2026-09-17: 첫 예제의 다리·체중 이동 품질을 개선한 현재 검토 후보는 `AS_TD_Player_Attack01_Heavy_RToL_v03`이다. [품질 개선 방법과 도구 선택](AnimationQuality.md), [품질 스킬](../Tools/BlenderAnimation/SKILL.md)을 먼저 확인한다. 아래 첫 예제의 수치는 교환 검증 기록이며 사용자 품질 승인을 뜻하지 않는다.

TDGame의 Unreal 리소스를 Blender로 가져와 GPT/Codex, Claude, Gemini 에이전트가 자연어 요청에 맞는 포즈와 타이밍을 작성하고, 같은 Unreal Skeleton을 사용하는 새 애니메이션으로 돌려보내는 작업 흐름이다. 플레이어·몬스터·NPC에 같은 절차를 적용하되 실제 본 구조와 공격 방식을 먼저 확인한다.

별도의 text-to-motion 모델이나 외부 모션 생성 서비스는 설치하지 않는다. LLM이 요청을 동작 단계로 해석하고 Blender MCP의 `execute_blender_code`를 통해 `bpy`로 포즈·키프레임·내보내기를 작성한다. 따라서 MCP 연결 성공과 모션의 시각적 품질은 별도 검증 대상이다.

## 구성과 연결

| 구성 | 역할 |
| --- | --- |
| Unreal MCP `http://127.0.0.1:8000/mcp` | 실제 캐릭터와 Skeleton 확인, FBX 내보내기·가져오기, 생성 에셋 검사 |
| `Tools/BlenderMCP` | 프로젝트용 Blender MCP 의존성, 시작 스크립트, stdio 연결과 호출 CLI |
| Blender 5.2의 로컬 `127.0.0.1:9876` | MCP 서버가 Blender 작업 세션에 명령을 전달하는 소켓 |
| `Tools/BlenderAnimation` | 이 프로젝트의 Blender 애니메이션 제작 스크립트 |
| `Content/Python` | Unreal FBX 교환용 에디터 Python 도구 |
| `Tools/AnimationAuthoring` | 기존 Unreal 애니메이션 검사·몽타주 생성 도구의 사용 절차 |

에이전트는 Unreal MCP와 Blender MCP를 모두 사용한다. Blender 쪽 MCP는 stdio 서버이고, `9876`은 Blender 애드온의 로컬 소켓이다. 클라이언트의 HTTP MCP 주소에 `http://127.0.0.1:9876`을 넣는 방식이 아니다.

설치·시작 스크립트는 `ahujasid/blender-mcp`의 고정 리비전과 잠긴 Python 의존성을 사용한다. Blender 세션은 프로젝트용 시작 스크립트로 따로 실행한다. `9876`을 다른 프로세스나 다른 Blender 세션이 사용하면 자동으로 종료하지 않고 오류를 반환한다.

## 준비와 연결 확인

프로젝트 루트에서 처음 한 번 설치한다. 현재 기본 실행 경로는 `C:\Program Files\Blender Foundation\Blender 5.2\blender.exe`다.

```powershell
# Context: C:/Project/TDGame
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools/BlenderMCP/Install-BlenderMCP.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools/BlenderMCP/Start-BlenderMCP.ps1
python Tools/ue_editor.py ensure
```

Blender MCP 클라이언트의 실행 명령은 `powershell.exe`, 인수는 `-NoProfile -ExecutionPolicy Bypass -File C:/Project/TDGame/Tools/BlenderMCP/Run-BlenderMCP.ps1`다. 이 실행기가 필요할 때 프로젝트 Blender 세션을 시작한 후 stdio MCP 서버를 연결한다. `DISABLE_TELEMETRY=true`, `BLENDER_HOST=127.0.0.1`, `BLENDER_PORT=9876`을 사용한다.

클라이언트 설정을 바꾼 뒤에는 해당 에이전트 세션에서 MCP 연결을 다시 로드하고 두 서버가 보이는지 확인한다. 설정 파일 존재만으로 현재 대화에 도구가 로드되었다고 판단하지 않는다. 현재 작업은 프로젝트 폴더에서 실행하는 로컬 MCP 클라이언트를 대상으로 하며, 원격 서비스가 이 PC의 로컬 포트에 직접 접속할 수 있다고 가정하지 않는다.

설정 파일은 Codex `.codex/config.toml`, Claude Code `.mcp.json`, Gemini CLI `.gemini/settings.json`, Cursor `.cursor/mcp.json`, VS Code `.vscode/mcp.json`이다. Claude Desktop이나 웹 ChatGPT에 이 파일들이 자동 적용되는 것은 아니다. 이 다섯 설정의 구문과 공통 MCP 서버의 실제 연결은 검증했으며, 각 제품에서 새 대화를 열어 수행하는 별도 로그인/모델 호출 테스트는 하지 않았다.

자동 시작은 숨겨진 독립 프로세스를 사용한다. MCP stdio 클라이언트가 종료되어도 Blender 프로세스와 열린 작업은 유지된다. 여러 에이전트가 같은 `9876` 세션을 사용하므로 동시에 같은 씬을 편집하지 않는다. 서버는 로컬 Python 코드를 실행하므로 신뢰하는 로컬 에이전트에만 연결한다.

CLI에서도 실제 MCP 연결과 도구 목록을 확인할 수 있다.

```powershell
# Context: C:/Project/TDGame
Tools/BlenderMCP/.venv/Scripts/python.exe Tools/BlenderMCP/call_tool.py --list
Tools/BlenderMCP/.venv/Scripts/python.exe Tools/BlenderMCP/call_tool.py --tool get_scene_info
python Tools/uemcp.py call list_toolsets '{}'
```

Blender Python 파일은 다음 형식으로 MCP를 거쳐 실행한다. `--code`에는 작업 전에 검토한 로컬 Python 파일 경로를 전달한다.

```powershell
# Context: C:/Project/TDGame; <script.py>를 실제 제작 스크립트 경로로 대체
Tools/BlenderMCP/.venv/Scripts/python.exe Tools/BlenderMCP/call_tool.py --code <script.py> --output <result.json>
```

도구 이름과 인수는 연결된 서버의 스키마로 확인한다. Unreal MCP가 검색용 도구만 표시하면 `list_toolsets` → `describe_toolset` → `call_tool` 순서로 실제 기능을 찾는다.

프로젝트에서 추가한 Unreal 툴셋은 `td_blender_animation_tools.TDBlenderAnimationTools`다. `Content/Python/init_unreal.py`가 에디터 시작 때 등록한다.

| 도구 | 주요 인수와 결과 |
| --- | --- |
| `export_fbx` | `asset_path`, `output_file`: 실제 SkeletalMesh 또는 AnimSequence를 프로젝트 안의 새 FBX로 저장 |
| `import_animation_fbx` | `source_file`, `destination_folder`, `asset_name`, `skeleton_path`: 기존 Skeleton에 새 AnimSequence 생성. 기본 `sample_rate=30`, `import_uniform_scale=1`, `preserve_local_transform=true` |
| `sample_animation_poses` | `animation_path`, `skeletal_mesh_path`, `bone_names`, `sample_times`: 초 단위 시점의 로컬/컴포넌트 본 위치(cm), 회전(xyzw), 크기 반환 |

FBX 가져오기는 명시적으로 legacy `FbxFactory`를 사용하며 메시·머티리얼·텍스처·물리 에셋을 생성하지 않는다. Blender의 Armature 오브젝트 `root`가 Unreal의 루트 본으로 돌아가므로 이름과 단위를 유지한다. 원본의 임포트 본 방향을 자동 정렬하지 않고, leaf bone을 추가하지 않으며, 단일 Action을 매 프레임 베이크한다.

## 에이전트의 제작 순서

1. **대상 확인:** 실제 플레이어 클래스 또는 지정한 몬스터·NPC에서 사용하는 Skeletal Mesh와 Skeleton을 확인한다. 에셋 이름이 비슷하다는 이유만으로 다른 캐릭터를 선택하지 않는다. 요청이 여러 캐릭터에 해당하면 대상 경로부터 확정한다.
2. **요청 해석:** 사용 손·무기·공격 방향·공격 횟수·전체 길이·루트 이동 여부를 정리한다. “오른쪽에서 왼쪽”은 기본적으로 캐릭터 자신의 방향 기준으로 기록한다. 준비 → 타격 → 후속 동작 → 복귀 구간을 나눈다.
3. **리소스 내보내기:** Unreal에서 원본 Skeletal Mesh를 새 FBX 파일로 내보내고 원본 경로·Skeleton 경로·본 이름과 부모 관계를 기록한다. 기존 에셋과 원본 FBX를 덮어쓰지 않는다.
4. **Blender 검사:** FBX를 가져온 뒤 실제 Armature, 본 계층, 레스트 포즈, 좌표계, 크기를 검사한다. 원본 Skeleton에 필요한 본 이름과 부모 관계를 유지한다. 본 이름만으로 회전축을 단정하지 않는다.
5. **동작 작성:** 실제 리그에 맞춰 몸통·골반·팔·다리의 역할을 대응시킨다. 준비 동작, 타격 궤적, 체중 이동, 발 접지, 복귀를 키프레임으로 작성한다. 제약이나 보조 컨트롤을 사용했으면 내보내기용 본 동작으로 베이크한다.
6. **Blender 검토:** 여러 시점과 타격 프레임을 확인한다. 손/무기 궤적, 관통, 발 미끄러짐, 예상하지 않은 루트 이동과 크기 변화를 확인한다. `.blend` 원본과 애니메이션 FBX를 새 이름으로 저장한다.
7. **Unreal 가져오기:** 새 AnimSequence 이름을 사용하고 처음 확인한 기존 Skeleton을 명시한다. 생성된 에셋의 실제 경로, Skeleton, 길이, 샘플 수와 본 트랙을 다시 조회한다.
8. **몽타주와 최종 검토:** 같은 Skeleton의 새 몽타주를 만들고 호환되는 슬롯을 지정한다. Unreal에서 캐릭터에 재생해 포즈·방향·크기와 결과를 확인한다. 저장된 산출물과 검증 근거를 보고한다.

기본 결과는 독립적인 AnimSequence와 몽타주다. 기존 공격 설정, 게임플레이 노티파이, 데미지 구간, 입력 처리, 콤보 연결은 자동 교체하지 않는다. 게임에 연결할 때는 현재 C++ 공격 구조와 기존 슬롯·노티파이 규칙을 먼저 확인한다.

## 다족 몬스터와 다른 Skeleton

공통으로 사용하는 것은 교환 절차이며, 모든 캐릭터에 같은 사람형 포즈를 적용하는 것은 아니다. 각 캐릭터에 대해 다음 항목을 실제 리소스에서 정한다.

| 확인 항목 | 제작에 사용하는 정보 |
| --- | --- |
| 루트와 몸체 연결 | 이동·회전의 기준, 흉부·복부 등의 몸통 분할 |
| 팔다리 체인 | 본의 부모 관계, 관절 방향, 말단 본과 발/발톱 위치 |
| 공격 기관 | 손과 무기, 앞다리, 턱, 꼬리 등 요청한 타격 부위 |
| 지지 역할 | 지면에 남길 다리, 이동시킬 다리, 동작 구간별 접지 상태 |
| 보조 본 | 날개·꼬리·장식·IK·변형 보조 본의 용도와 유지 방법 |

예를 들어 다리가 여섯 개인 몬스터는 먼저 여섯 체인을 확인하고, 공격하는 앞다리와 몸체를 지지하는 다리를 구분한다. 어느 프레임에 어떤 발을 고정할지 정한 다음 동작을 작성한다. 사람형 리타게팅이나 자동 IK가 준비되어 있다고 가정하지 않는다. 체인 역할이 리소스만으로 명확하지 않으면 해당 역할만 사용자에게 확인한다.

다른 Skeleton으로 전용하는 작업은 별도의 본 대응과 시각 검증이 필요하다. 한 플레이어의 성공 결과를 모든 몬스터에 대한 검증으로 보고하지 않는다.

## 자연어 요청 예시

다음 문서를 읽도록 요청하면 클라이언트의 자동 스킬 탐색에 의존하지 않고 같은 작업 순서를 사용할 수 있다.

> `C:/Project/TDGame/Docs/BlenderAnimationWorkflow.md`를 읽고 Unreal MCP와 Blender MCP를 사용하세요. 현재 프로젝트 플레이어의 실제 Skeletal Mesh와 Skeleton을 확인한 뒤 Blender로 가져오세요. 오른손 한손 무기로 캐릭터 자신의 오른쪽에서 왼쪽으로 한 번 횡베기하는 기본 공격 1을 만드세요. 짧은 준비 동작, 몸통 회전, 타격 후 복귀를 넣고 발 접지를 확인하세요. 새 `.blend`, FBX, AnimSequence와 몽타주로 저장하고 기존 공격 연결은 유지하세요. Blender와 Unreal에서 결과를 검증하고 정확한 산출물 경로와 검증하지 못한 항목을 알려주세요.

후속 콤보는 방향과 시작·끝 포즈를 함께 지정한다.

> 같은 캐릭터의 기본 공격 2를 새 에셋으로 만드세요. 1번 공격의 종료 포즈에서 자연스럽게 시작하여 왼쪽에서 오른쪽으로 횡베기하세요. 1번 에셋은 수정하지 말고 연결 시점을 확인하세요.

“뒤에서 앞으로”는 찌르기, 내려찍기, 팔을 뒤로 뺐다가 휘두르기 중 어느 동작인지 리소스와 문맥만으로 결정하기 어려울 수 있다. 타격 궤적이 달라지는 이런 모호성은 먼저 확인한다.

다족 몬스터 요청에는 실제 경로와 공격 부위를 포함한다.

> `<실제 몬스터 Skeletal Mesh 경로>`를 검사하고 Blender로 가져오세요. 실제 다리 체인을 확인하여 오른쪽 앞다리로 한 번 할퀴고 나머지 지지 다리가 지면을 유지하는 공격을 만드세요. 기존 본 이름과 계층을 유지하고 사람이 아닌 체형에 맞게 몸체의 무게 이동을 설계하세요. 접지 구간과 검증 결과를 기록하세요.

## 결과를 판단하는 기준

- **연결:** Blender MCP와 Unreal MCP에서 실제 도구 호출이 성공했는가.
- **교환:** 내보낸 캐릭터를 Blender가 읽었으며 새 FBX가 의도한 기존 Unreal Skeleton에 연결되었는가.
- **동작:** 요청한 손·방향·횟수에 맞고 타격 구간과 준비·복귀 구간을 구분할 수 있는가.
- **모양:** 접지, 관통, 비정상 관절, 루트/스케일 변화가 없는지 Blender와 Unreal에서 확인했는가.
- **보존:** 원본 캐릭터, 기존 애니메이션, 게임플레이 연결이 유지되며 새 파일만 저장되었는가.
- **재현:** 제작 스크립트와 `.blend` 원본, FBX, 결과 에셋 경로가 남아 있는가.

수치 검사만으로 자연스러움이나 전투 감각을 확정하지 않는다. 실제 무기가 포함되지 않은 검토라면 손 궤적 확인과 무기 궤적 확인을 구분해서 기록한다. 게임 입력으로 공격을 재생하지 않았다면 런타임 공격 연결 검증을 완료로 표시하지 않는다.

## 첫 횡베기 예제의 검증 기록

2026-09-16 검증 완료. Blender `5.2.2 LTS`, upstream Blender MCP `1.9.4`, 리비전 `7684c6b3ad2aa0710bbdb1cb06b497c90899ae00`, 애드온 프로토콜 `7`을 사용했다. MCP `initialize`, `tools/list`, 실제 오브젝트/키프레임 편집, FBX 가져오기·제작·내보내기, 파일 재열기와 클라이언트 재연결을 실행했다. 외부 생성 서비스는 비활성화했고 텔레메트리는 껐다.

- 플레이어: `/Game/Combat/Blueprints/BP_TDCombatCharacter`
- 메시: `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple` — 루트를 포함해 89본
- 스켈레톤: `/Game/Characters/Mannequins/Meshes/SK_Mannequin`
- AnimBP: `/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed`, 실제 슬롯 `DefaultSlot`
- AnimSequence: `/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_RToL_Blender`
- Montage: `/Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_RToL_Blender`, 섹션 `Attack01`
- 편집 원본: [AS_TD_Player_Attack01_RToL_Blender.blend](../AnimationSources/Player/AS_TD_Player_Attack01_RToL_Blender.blend)
- 교환 파일: [AS_TD_Player_Attack01_RToL_Blender.fbx](../AnimationSources/Player/AS_TD_Player_Attack01_RToL_Blender.fbx)
- [움직이는 미리보기](Validation/BlenderAnimation/player-attack01.gif), [주요 포즈](Validation/BlenderAnimation/player-attack01-poses.png), [Unreal PIE 화면](Validation/BlenderAnimation/unreal-pie-attack01.png)

동작은 제자리 오른손 횡베기 1회다. 총 1.2초, 30fps, 36개 프레임 간격/37개 포즈이며, 타격 구간은 대략 11~21프레임이다. Blender 프레임은 1~37, Unreal 시간은 0~1.2초다. 준비·감기·가르기·후속 동작·준비 자세 복귀를 포함한다. 미리보기의 황금색 막대는 궤적 확인용 가상 검이며 게임 무기 에셋이 아니다. FBX에는 Armature 애니메이션만 내보냈다.

37개 프레임의 손/발 위치를 비교하여 Blender→Unreal 최대 오차 **0.0271cm 미만**, 루트 이동 **0cm**, 발 위치 변화 **0.00017cm 미만**, 컴포넌트 스케일 오차 **0.000006 미만**을 확인했다. 시작/끝 주요 본 위치 차이는 **0.00002cm 미만**이다. 근거는 [수치 검증 JSON](Validation/BlenderAnimation/player-attack01-validation.json)이며 `python Tools/BlenderAnimation/validate_player_sample.py`로 재확인한다.

PIE의 실제 `BP_TDCombatCharacter`/`ABP_Unarmed`에서 C++ `TDPlayMeleeMontage` 명령과 몽타주 API로 재생했다. `DefaultSlot` 활성 및 가중치 `1.0`, 0.55초 포즈를 확인했다. [PIE 검증 JSON](Validation/BlenderAnimation/pie-validation.json)에 기록했다. 시각 검토를 위해 PIE 인스턴스의 카메라와 숨겨진 창의 본 갱신 설정만 임시 변경했으며 PIE 종료로 해제되었다. 런타임 AnimBP의 리타게팅/Control Rig 보정 후 위치는 원시 AnimSequence 수치와 다를 수 있다.

현재 플레이어의 `skillSet=None`이므로 공격 입력·데미지 노티파이·콤보에는 연결하지 않았다. 몽타주 재생 가능 여부와 공격 게임플레이 연결은 구분한다. 다족 몬스터 실물 에셋에 대한 제작 테스트는 이번 예제에 포함되지 않았다. 본 교환 도구는 특정 체형을 가정하지 않지만 `author_player_slash.py`는 검증한 Manny 전용 예제다.

제작 스크립트는 `Tools/BlenderAnimation/author_player_slash.py`, 실행 예제는 `create_player_sample.py`, `import_player_sample.py`, `create_player_montage.py`다. 같은 이름의 결과가 존재하면 생성 스크립트는 거부한다. 새 변형은 요청의 이름/목적지를 바꾸고 원본을 보존한다. Manny 예제를 다시 제작할 때는 현재 문서를 저장한 뒤 새 Blender 문서를 열어 `root` 이름 충돌을 방지한다. 기존 결과를 열 때는 `open_player_source.py`를 Blender MCP로 실행한다.

PIE에서 아래 콘솔 명령으로 재생할 수 있다.

```text
// Context: Unreal PIE console
TDPlayMeleeMontage /Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_RToL_Blender
```

## 연결 문제 확인

Blender가 연결되지 않으면 `Tools/BlenderMCP/.runtime/blender.stdout.log`와 `blender.stderr.log`를 확인한다. `9876` 충돌은 사용 중인 세션을 확인한 뒤 처리하며 다른 작업의 Blender를 임의로 종료하지 않는다. `get_scene_info`는 성공하지만 제작 스크립트가 실패하면 실제 객체·본 이름, 현재 Action, 프레임 범위를 먼저 확인한다.

Unreal 도구가 보이지 않으면 TDGame 에디터가 열려 있는지와 `8000` MCP 연결을 먼저 확인한다. 새 도구 등록 상태를 재조회하고 실제 도구 스키마를 확인한다. 연결 실패와 도구 미구현을 구분한다.

FBX 재가져오기 후 크기·루트 방향·본 계층이 달라지면 결과를 게임에 연결하지 않고 내보내기 설정과 Blender 가져오기 상태를 조사한다. 기존 Skeleton을 변경해서 증상을 숨기지 않는다. 소스와 에셋 저장 후에는 Git diff/status와 `.uasset`의 LFS 속성을 확인한다.

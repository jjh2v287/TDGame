# AI 에이전트 애니메이션 제작 도구

TDGame의 Unreal MCP를 통해 AI 에이전트가 새 애니메이션 시퀀스와 몽타주를 작성하는 작업용 도구다. C++ 에디터 도구가 에셋 생성을 맡고, 기존 Unreal Control Rig/Sequencer 도구가 포즈와 키프레임 편집을 맡는다. 게임플레이 로직은 C++에 둔다.

## 구성

| 구성 | 역할 |
| --- | --- |
| `UTDAnimationAuthoringTools` | Skeleton 검사, 본 키프레임 기반 AnimSequence 생성, AnimMontage 생성, 결과 검사 |
| `UTDControlRigTools` | 캐릭터 spawnable과 C++ FK Control Rig가 연결된 새 LevelSequence 생성 |
| `UTDSequencerAnimationTools` | 시퀀스 바인딩 검사, 새 AnimSequence로 베이크 |
| UE `AnimationAssistantToolset` | 기존 Control Rig 연결, 컨트롤 검사와 키 입력, Sequencer 재생·범위 편집 |
| `Tools/uemcp.py` | 로컬 Unreal MCP 도구 탐색·스키마 조회·호출 CLI |
| [재사용 스킬](../Tools/AnimationAuthoring/SKILL.md) | 캐릭터 확인 → 키 작성 → 베이크 → 몽타주 → 검증 절차 |

작업에는 TDGame을 연 UE 5.8 에디터와 로드된 `TDGameEditor` 모듈이 필요하다. MCP 주소는 `http://127.0.0.1:8000/mcp`다. 실제 이름과 인수는 런타임 스키마로 확인한다. `tools/list`에 검색용 도구 3개만 보이면 `list_toolsets` → `describe_toolset`으로 TD 도구를 찾고 `call_tool`로 호출한다. 구체적인 `@file` CLI 예시는 [작업 절차](../Tools/AnimationAuthoring/references/workflow.md)에 있다.

## 사용

Gemini, Claude, GPT 에이전트에게 다음과 같이 요청할 수 있다. 스킬 경로를 명시하면 해당 클라이언트의 자동 스킬 검색 기능 유무에 의존하지 않는다.

> `C:/Project/TDGame/Tools/AnimationAuthoring/SKILL.md`를 읽고 Unreal MCP를 사용하세요. 현재 선택한 캐릭터의 메쉬와 본 구조를 먼저 확인한 뒤, 오른손을 들어 두 번 흔드는 2초 애니메이션과 몽타주를 새 에셋으로 만드세요. 호환되는 기존 Control Rig가 없으면 `CreateFKSequence`로 FK 리그와 시퀀서를 만들고 컨트롤 키를 작성한 뒤 베이크하세요. 편집한 시퀀서도 저장하고, 결과 포즈와 실제 길이를 검증하여 에셋 경로를 알려주세요.

대상 캐릭터가 명확하지 않으면 에이전트가 캐릭터 경로를 확인한다. 리그와 좌표계에 따라 같은 숫자가 다른 포즈가 될 수 있으므로 본·컨트롤 이름이나 회전축을 추측하지 않는다.

Codex에서 설치형 스킬로 사용하려면 `Tools/AnimationAuthoring`의 `SKILL.md`와 `references`를 함께 `$CODEX_HOME/skills/td-animation-authoring`에 복사한다. `CODEX_HOME`이 없으면 `~/.codex/skills/td-animation-authoring`를 사용한다. 설치된 스킬이 세션 목록에 보이면 `$td-animation-authoring`으로 지정할 수 있다. Gemini·Claude는 각 클라이언트의 스킬/문서 로딩 설정을 따르거나 위처럼 원본 파일을 직접 읽도록 지정한다. 저장소에 파일이 있다는 이유만으로 모든 에이전트가 자동 발견한다고 가정하지 않는다.

## 동작 경로

- **Control Rig 경로:** `CreateFKSequence`로 새 LevelSequence·캐릭터 바인딩·C++ FK 리그 구성 또는 기존 리그 연결 → 컨트롤 키 작성 → 시퀀서 저장 → 새 AnimSequence로 베이크 → 요청된 몽타주 구성. 별도 리그 에셋이 없는 캐릭터도 시퀀서에서 포즈를 수정할 수 있다. FK 도구는 영구 레벨 액터나 Blueprint 그래프를 변경하지 않는다.
- **본 키 경로:** Skeleton과 레퍼런스 포즈 조회 → 로컬 본 키 작성 → C++에서 샘플링하여 새 AnimSequence 생성 → 요청된 몽타주 구성. 별도 리그가 없는 캐릭터도 다룰 수 있다.

7개 C++ 도구의 목록과 입력 스키마, 좌표계, 프레임 계산, 네이티브 도구 제약은 [작업 절차](../Tools/AnimationAuthoring/references/workflow.md)에 있다. 생성 도구는 기존 목적지 에셋을 덮어쓰지 않는다. 몽타주에는 같은 Skeleton을 사용하는 애니메이션과 기존 슬롯을 지정한다. 생성 시의 `save`는 후속 키 변경을 저장하지 않으므로 편집한 LevelSequence는 따로 저장한다.

## 연결 검증 실행

TDGame 에디터를 연 상태에서 프로젝트 루트에서 실행한다.

```powershell
# Context: C:/Project/TDGame
python Tools/AnimationAuthoring/smoke_test.py --mesh /Game/Characters/Mannequins/Meshes/SKM_Manny_Simple --output Tools/AnimationAuthoring/smoke-report.json
```

`--output`에 JSON 검증 보고서를 쓴다. 각 실행은 `/Game/Tests/AnimationAuthoring/Run_<id>`에 테스트 에셋을 만들며, `--save`를 추가하면 디스크에도 저장한다. 기본 실행은 메모리에 에셋을 남긴다. 테스트는 본 키 생성·검사, 몽타주, FK 시퀀서·키 입력·베이크와 입력 거부를 확인한다. 통과 여부는 실제 실행 보고서에서 확인한다.

남은 작업과 알려진 결함은 [남은 작업 대장](AnimationAuthoring_Tasks.md)에 있다.

## 제공 범위와 검증

이 구성은 에이전트가 동작을 포즈와 타이밍으로 설계하고 엔진에 기록할 수 있게 한다. 자연어 입력만으로 사실적인 모션을 보장하는 전용 text-to-motion 모델은 포함하지 않으며 외부 모델 계정·API 키가 필요하지 않다.

에셋 생성 성공, 화면에서의 포즈 품질, 게임에서의 몽타주 재생은 각각 확인해야 한다. 발 접지, 손/무기 위치, 관통, 루프 연결, 루트 이동은 시각 검증 대상이다. 노티파이·리타게팅·IK·곡선·게임플레이 연결은 현재 노출된 도구와 별도 요구 범위를 확인한다.

도구 실행 후에는 생성 에셋 재조회와 Git/LFS 검사를 수행한다. 아직 실행하지 않은 리그/캐릭터 조합과 런타임 재생을 완료로 보고하지 않는다. 로컬 엔진의 `AnimationAssistantToolset/Content/Python/animation_toolset/toolsets/`가 네이티브 래퍼 구현의 근거이며, 설치 엔진 버전이 바뀌면 라이브 도구 스키마를 다시 확인한다.

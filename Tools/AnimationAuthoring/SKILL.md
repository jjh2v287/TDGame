---
name: td-animation-authoring
description: Author new TDGame Unreal Engine animation sequences and montages through Unreal MCP, using native FK Control Rig, an existing Control Rig, Sequencer baking, or C++ bone keyframes. Use for character motion authoring and animation asset inspection; excludes gameplay Blueprint logic and external text-to-motion services.
---

# TD animation authoring

사용자가 설명한 캐릭터 동작을 키프레임으로 구성하고 새 `AnimSequence` 및 요청된 `AnimMontage`를 만든다. Gemini, Claude, GPT 등 어떤 에이전트도 같은 MCP 계약을 사용할 수 있다. 이 스킬에는 모션 생성 AI 모델이나 모델 API 연결이 포함되지 않는다.

## 시작과 선택

1. TDGame을 연 Unreal Editor와 `http://127.0.0.1:8000/mcp` 연결을 확인한다. MCP `tools/list`에서 이름과 스키마를 찾는다. `list_toolsets`·`describe_toolset`·`call_tool`만 보이면 검색 모드이므로 `list_toolsets` → `describe_toolset`으로 실제 도구를 찾고 `call_tool`로 호출한다. 이 문서의 C++/Python 함수 이름을 MCP 이름이라고 추측하지 않는다. 연결 실패는 도구 부재와 구분한다.
2. 대상 `SkeletalMesh`, Skeleton, 기존 리그, 요청 동작, 길이, 반복 여부, 루트 이동을 확인한다. 메쉬·본·컨트롤·몽타주 슬롯 이름은 조회 결과를 사용한다. 대상 캐릭터를 특정할 수 없으면 그것만 질문한다. 빠진 스타일·길이는 작업 목적에 맞는 가정을 알리고 진행한다.
3. 생성할 에셋 경로와 동작을 사용자에게 알린다. 고유한 새 경로를 사용한다. 이번 작업에서 만든 에셋의 키 수정은 요청 범위 안에서 진행하되, 사용자의 기존 에셋을 삭제하거나 덮어쓰지 않는다.
4. 편집 가능한 Sequencer 결과가 필요하면 Control Rig 경로를 선택한다. 호환되는 기존 리그가 없으면 `UTDControlRigTools::CreateFKSequence`로 엔진의 C++ FK 리그와 캐릭터 바인딩을 함께 만든다. 시퀀서 없이 본 키만 작성하려면 `CreateBoneAnimation`을 사용한다. 상세 절차·스키마는 [references/workflow.md](references/workflow.md)를 읽는다.

## 작성 규칙

- 동작을 준비, 주요 포즈, 회복 구간으로 나누어 작성한다. 좌우·관절 축은 스켈레톤 조회와 실제 포즈로 확인한다. 본 이름만으로 회전축을 단정하지 않는다.
- 기존 리그 또는 엔진의 C++ FK 리그를 사용한다. 새 Blueprint 게임플레이/Control Rig 그래프 로직은 추가하지 않는다. 키프레임·에셋 구성은 에디터 작업이고 게임플레이 실행 로직은 C++에 남긴다.
- Control Rig 키는 컨트롤 공간의 값이다. 직접 본 키는 부모 기준 로컬 값 또는 레퍼런스 포즈 오프셋이다. 두 좌표계를 그대로 섞지 않는다.
- FK 생성 응답의 `binding`, `control_rig_asset_path`, `controls`를 후속 호출에 사용한다. 네이티브 도구의 UObject 인수에는 스키마에 맞는 `refPath` 객체를 사용한다. FK 키 범위는 `0..num_frames-1`이고 직접 본 키 범위는 `0..num_frames`이므로 베이크 후 실제 길이를 확인한다.
- MCP 성공 응답뿐 아니라 도구가 반환한 JSON의 `success`/`error`도 확인한다. 실패나 시간 초과 후에는 에셋 존재와 상태를 먼저 조회하고 같은 생성 요청을 무작정 반복하지 않는다.
- 베이크는 새 목적지 `AnimSequence`를 만드는 TD 도구를 우선 사용한다. 기존 애니메이션 에셋을 목적지로 지정하지 않는다.

## 완료 조건

생성 결과를 다시 조회하여 Skeleton, 길이, 샘플/트랙 수, 몽타주 세그먼트·섹션을 검증한다. 에디터에서 시작·주요 포즈·중간·마지막 프레임과 재생을 확인하여 관절 뒤집힘, 발 미끄러짐, 관통, 루프 경계, 루트 이동을 평가한다. 숫자 조회는 시각 품질 검증을 대신하지 않는다.

키 편집이 끝나면 LevelSequence도 저장한다. `CreateFKSequence`의 `save`는 생성 당시 상태만 저장하며 후속 키 변경이나 베이크가 원본 시퀀서를 자동 저장하지 않는다. 의도한 에셋만 저장하고 Git 변경과 LFS 속성을 확인한다. 결과 경로, 작성한 동작, 실제 검증, 미검증 항목을 보고한다. 화면 검증이나 런타임 재생을 못 했다면 그 상태를 명시하고 완성된 동작이라고 단정하지 않는다.

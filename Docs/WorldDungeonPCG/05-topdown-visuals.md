[← 인덱스로](../WorldDungeonPCG_Plan.md)

# 5. 탑다운 시각 정리 (P3-17, 결정 D-43)

큰 물체(나무·절벽·바위·기둥)가 카메라와 폰 사이를 가리거나 카메라가 메시 안으로 들어가는 문제를 두 층으로 푼다. 재질 층(이 문서 5.2~5.3)은 가리는 물체를 런타임에 디더로 비우고, 배치 층(5.4)은 걷는 길을 가리는 인스턴스를 생성 단계에서 줄인다. 팩 원본 `/Game/DarkFantasyTopDown`은 고치지 않는다(복제만).

## 5.1 카메라 사실 (실측, BP_TDCombatCharacter가 C++ 기본값을 덮어씀)

| 항목 | 값 |
|---|---|
| 스프링암 | 길이 1100cm, 상대 회전 피치 −50°·요 45°, 절대 회전, 카메라 지연 켜짐, 충돌 검사 끔 |
| 시야각 | 90° |
| 카메라 위치 | 폰 루트(캡슐 중심, 실측 지면 +90cm: `TDCombatCharacter` 캡슐 반높이 88) 기준 (−500, −500, +842.6)cm(PIE 실측) → 지면 위 약 9.3m. 배치 규칙은 보수적으로 지면 +96을 쓴다 |
| 방향 | 카메라는 +X+Y를 본다. 카메라 쪽 = −X−Y, 등 = +X+Y |
| 렌더 | 나나이트 꺼짐, TSR, 가상 섀도 맵, Lumen, Substrate 켜짐 |
| 태양(D-46) | 빛 진행 수평 방향은 카메라 시선 반대(yaw 225°) ±30° 안: 필드 (−70°, 205°)·별밭 (−70°, 200°)·고원 (−55°, 210°). 컷아웃은 그림자 패스에 원래 재질을 쓰므로, 태양이 카메라 뒤에 있으면 비운 가림물의 그림자만 폰 위에 남는다. 역광이면 그 그림자가 카메라 쪽(화면 아래)으로 빠진다. 바꿀 때는 `Tools/WorldGen/editor_set_sun.py`(미리보기·저장), 원천은 `editor_build_open_world.py` 태양 생성값과 `generate_realm.py` 대기 |

C++ 기본값(`TDGameCharacter.cpp:73-76`, 800/−60/0)과 다르므로 카메라 기하는 항상 위 값으로 계산한다.

## 5.2 재질 계층 (`/Game/World/Materials/TopDown/`)

```
Parameters/MPC_TD_TopDown        MPC(재질 매개변수 컬렉션)
Functions/MF_TD_OcclusionCutout  컷아웃 함수(입력 ExistingMask, 출력 OpacityMask, 45노드)
Masters/M_TD_Prop | M_TD_Rock | M_TD_PlantAnimated | M_TD_Leaf | M_TD_Bark
Instances/MI_TD_<원본 이름에서 MI_ 뺀 것>   (원본이 마스터 자체면 MI_TD_<M_ 뺀 이름>_Default)
```

**MPC_TD_TopDown** (게임 값은 C++가 매 틱 넣는다. 재질은 표현만, 판단 값은 C++, AGENTS.md 7절)

| 이름 | 종류 | 기본값 | 뜻 |
|---|---|---|---|
| OcclusionFocus | 벡터 | (0,0,0,0) | xyz = 폰 초점 월드 cm, w = 1 활성 / 0 끔 |
| OcclusionRadius | 스칼라 | 260 | 카메라→초점 선분 기준 원통 반지름 cm |
| OcclusionSoftness | 스칼라 | 120 | 가장자리 부드러움 폭 cm |
| OcclusionKeepBase | 스칼라 | 40 | 폰 발 위 이 높이 아래는 자르지 않음 |
| OcclusionMaxCut | 스칼라 | 0.92 | 최대 제거 비율(1 미만이라 점묘가 남음) |
| OcclusionFeetZ | 스칼라 | 0 | 폰 발의 월드 Z |

**MF_TD_OcclusionCutout** 계산은 카메라 상대 좌표다(LWC 안전: 카메라 상대 WorldPosition, `CameraPositionWS`, `TruncateLWC`). Prel = 픽셀 − 카메라, Frel = Focus.xyz − 카메라, t = dot(Prel,Frel)/max(dot(Frel,Frel),1), d = |Prel − Frel·t|, cut = Focus.w × step(0.02<t<0.97) × (1 − smoothstep(Radius−Softness, Radius, d)) × (픽셀 Z가 FeetZ+KeepBase 위). 결과 = min(ExistingMask, DitherTemporalAA(1 − cut×MaxCut)). 그림자 패스(`ShadowReplace`)와 Lumen 카드·레이트레이싱(`RayTracingQualitySwitch`)에서는 ExistingMask만 쓰므로 그림자·간접광은 컷아웃 영향이 없다.

**마스터** 5개는 팩 마스터의 복제이고 기존 OpacityMask 앞에 함수 호출 노드를 끼웠다. 대응: `M_MasterMaterialBase`→M_TD_Prop, `M_RocksMasterMaterial`→M_TD_Rock, `M_MasterMaterialBaseAnimated`→M_TD_PlantAnimated, `M_Branch`→M_TD_Leaf, `M_Bark`→M_TD_Bark(불투명이던 것을 Masked로 바꾸고 ExistingMask는 상수 1). 팩 `M_Bark`에 저장된 `bCanMaskedBeAssumedOpaque`가 복제본에 따라와 Masked여도 `GetBlendMode()`가 Opaque이므로, 재질 도구가 C++ `UTDWorldGenEditorLibrary::ClearMaskedAssumedOpaque`로 끈다(L-editor-17). 블렌드 확인은 속성값이 아니라 `get_blend_mode()`로 한다.

**거울 인스턴스**: 팩 재질 X(`/Game/DarkFantasyTopDown/Materials/**`, 최상위 마스터가 위 5개)마다 `MI_TD_<X>`를 만든다. 부모 = 대응 TD 마스터, 값 = 원본 체인의 유효 값 중 마스터 기본값과 다른 것(스칼라·벡터·텍스처·정적 스위치), 기저 속성 재정의는 `two_sided`·`dithered_lod_transition`·`opacity_mask_clip_value`·`shading_model`만 복사한다. 블렌드 모드 재정의는 복사하지 않는다(팩 MI 17개가 불투명으로 재정의하던 것이 Masked가 된다). 이름 규칙 정본은 `Tools/WorldGen/topdown_materials.py`의 `topdown_material_path()`다. 거울이 없는 재질(물·안개 평면·반투명·빛기둥·불꽃·지형 데칼·밧줄 `M_SimpleARMWindVertexPaint`·`M_Colored`·`M_Emissive`)은 원본 그대로 둔다.

**이계 MI** `MI_TDRealm_*` 중 부모가 거울 대상 팩 MI인 것은 그 거울 MI로 재부모화한다(재정의 값 유지를 값 대조로 확인하고 다르면 되돌린다). 이계 베이크(`editor_build_realm.py`)가 부모를 팩 MI로 다시 지정하므로 적용 도구가 매번 다시 맞춘다.

## 5.3 새 에셋을 추가할 때

1. 새 팩 재질을 쓰는 메시를 배치하려면 `python Tools/run_in_editor.py Tools/WorldGen/editor_make_topdown_materials.py`를 다시 실행한다(재실행 안전, 새 팩 MI의 거울만 만든다). 최상위 마스터가 5개 밖이면 거울이 없고 적용 도구가 건너뛴 이름을 보고서에 센다.
2. 새 마스터가 필요하면 `topdown_materials.py`의 `MASTER_TABLE`에 한 줄을 추가한다. 마스터에는 OpacityMask가 있어야 하고(없으면 Masked로 전환) 반투명·Material Attributes 마스터는 넣지 않는다.
3. 우리 쪽 재질(`MI_TDRealm_*` 등)을 새로 만들면 부모를 팩 MI로 두어도 된다. 적용 도구가 거울로 재부모화한다.
4. PCG 그래프에서 메시를 쓰면 적용 도구 옵션 `patch_pcg`가 스포너 항목의 `override_materials`를 거울로 채운다(재생성해도 유지).

## 5.4 배치 규칙 요약 (P 갈래, 명세 3절)

결정론 후처리이며 난수 소비는 바꾸지 않는다. 카메라 쪽은 −X−Y다. 정본 모듈은 `Tools/WorldGen/topdown_rules.py`(구현 상태는 대장 P3-17 확인).

| 규칙 | 내용 |
|---|---|
| R1 길 가림 제거 | 걸을 칸의 폰 머리(지면+170)→카메라 선분을 가리는 큰 인스턴스: 나무·버섯·흩뿌림은 제거, 바위·절벽은 z 축소(최소 0.6배, 바닥 높이 보존) 또는 제거, 저작 액터는 유지(재질이 처리). 카메라 점 반경 1.5m 안 메시도 같은 방식 |
| R2 바위·절벽 틈 | 무작위 pitch·roll은 소비만 하고 0 적용, 카메라 쪽 최대 틈만큼 sink 추가, 틈이 높이 35% 초과면 제거 |
| R3 높이 상한 | 이계 기둥은 걸을 칸 근처에서 지면 위 6m 상한 또는 이동 금지 띠 밖, LightBeam은 `collision: none` |
| R4 통로 단면 비대칭 | 카메라 쪽 벽 바닥 위 2.5m 이하, 먼 쪽 22m 유지, 카메라 점이 지형 아래인 통로 칸 0 |
| R5 통로 입구 | 상인방·덮개 제거, 기둥 꼭대기 6m 이하 |
| R6 구름 바다 | 고원 구름 평면을 이계 상자 밖 ±450m까지 확장(NoCollision) |

적용 결과(2026-10-14, 시드 7, 걷는 칸 기준, 측정 `python Tools/WorldGen/topdown_rules.py --measure`):

| 구간 | 시선 가림 % 전→후 | 카메라 메시 안 칸 전→후 |
|---|---|---|
| 필드 | 11.0 → 0.97 | 3336 → 0 |
| 가라앉은 별밭 | 1.32 → 0.09 | 65 → 0 |
| 창백한 고원 | 9.79 → 0.72 | 488 → 0 |
| 통로 남서 / 남동 / 북동 | 4.27→0.66 / 5.49→1.20 / 7.63→1.86 | 29·29·107 → 0 |

- 규칙별 변경(필드/별밭/고원): R1 제거 662/2/88·z 축소 1/11/24, R2 기울기 0 178/166/384·묻기 90/69/81·가장자리 제거 24/15/151, R3 기둥 상한 0/7/8(고원 첨탑 62m → 6m), R5 상인방·덮개 각 3 제거·기둥 6 상한, R6 구름 평면 128.
- R2는 전체에 걸면 필드에서만 705개가 사라져, 걷는 칸에서 보이는 구역(뒤 25m·옆 8m)으로 한정했다.
- R4 편차: 필드 접근로(s<−34m)의 카메라 쪽은 2.5m 턱 대신 45° 사면(최대 6.5m)으로 막았다(통로 밖으로 나가지 못하게). 카메라는 지형 위에 있다.
- 남은 가림은 저작 건물·말뚝(마을)이며 재질 컷아웃이 처리한다. 지형이 바뀐 통로 띠에서는 규칙 밖 인스턴스 8개가 새 지면에 다시 붙었다(z만 0.2~73.5cm, 난수 흐름 불변).
- 규칙 끄기 `--no-topdown`은 이전 출력과 바이트 동일하다.

## 5.5 검증 방법

- 재생성 순서(순서 계약): (재질 체계를 바꿨을 때만) 재질 도구 → 필드·이계 생성 → 베이크 → `patch_pcg` 적용 → PCG 재생성 → `editor_apply_topdown_materials.py` → 저장(PCG 파티션 액터는 `save_dirty_packages`). 정본 절차는 `Tools/WorldGen/README.md` 이계 절 7~9.
- 재질 도구 보고 `Saved/WorldGen/topdown_materials_report.json`: `compile_errors`(마스터별 오류 배열, 비어야 함), `mirror_value_mismatches`(비어야 함), `reparented`. 두 번째 실행은 `changes_total=0`.
- 적용 도구 보고 `Saved/WorldGen/topdown_apply_report.json`: 슬롯 수(`changed`/`already_topdown`/건너뜀), 건너뛴 재질 이름별 수, PCG 컴포넌트 트리거 요약. 옵션은 `Saved/WorldGen/topdown_apply_request.json` `{"dry_run": true}`로 세기만 한다. 두 번째 실행은 `changed_actors=0`.
- 동작 확인: 에디터에서 `unreal.MaterialLibrary.set_vector_parameter_value(world, mpc, "OcclusionFocus", LinearColor(x,y,z,1))`로 초점을 나무 뒤(+X+Y 쪽 약 260cm, 지면+90)에 두고 카메라 = 초점 + (−500,−500,+843), 피치 −50, 요 45로 `EditorAppToolset.CaptureViewport` 캡처, w=0과 비교한다. 월드의 MPC 인스턴스 값은 저장되지 않으므로 에디터를 다시 열면 기본값(w=0)이다. 증거는 `Saved/AgentOps/20261014/topdown/laneM/birch_w0.png`·`birch_w1.png`(잎만 잘림, 수정 전)·`bark_w1_fixed.png`(줄기까지 잘림). 게임 화면 검증은 `pie_capture_spots.py`(27지점 전후 비교)와 `pie_walk_passages.py`.
- 팩 원본 무변경: `git status --short Content/DarkFantasyTopDown`이 비어야 한다.
- 함정: `MaterialEditingLibrary.get_material_property_input_node`에 유효하지 않은 `MaterialProperty` 값(열거형 전체를 훑을 때)을 주면 에디터가 크래시한다. `delete_all_material_expressions_in_function`은 절반만 지운다(하나씩 지우는 반복이 필요). MF를 다시 만들면 마스터의 호출 노드 연결이 끊겨 재조정이 필요하다(도구가 처리). `unreal.Vector4f`는 `import_text`로만 값을 넣는다.

## 5.6 성능 주의

- 마스터 5개가 팩 마스터와 별도 셰이더 맵이라 처음 열 때 셰이더 컴파일이 수 분 걸린다(원격 실행 시간 초과 위험은 없었음, 마스터 재컴파일 합계 약 25초 + 백그라운드 컴파일).
- M_Bark를 불투명에서 Masked로 바꾸면 깊이 선행 패스·그림자 깊이 비용이 늘 수 있다(미측정). 불투명으로 재정의하던 팩 MI 17개도 Masked가 된다.
- 컷아웃 비용은 픽셀당 산술 연산 수십 개와 디더 함수 한 번이다(마스터 통계 약 240 픽셀 셰이더 명령어). `stat gpu`로 PrePass·BasePass·ShadowDepths 증가폭을 확인하고 +0.3ms 이하를 기준으로 삼는다(미측정).
- TSR에서 이동 중 경계 고스팅이 보이면 `bHasPixelAnimation`을 검토한다. 구멍이 화면 중앙에 고정되므로 MaxCut 1 미만·Softness·KeepBase 튜닝이 필요하다.
- 적용 도구는 외부 액터 약 2,700개(PCG 파티션 액터 포함)의 패키지를 저장한다. 커밋 범위는 LFS 확인 뒤 정한다.

## 5.7 알려진 한계 (2026-10-14 기준)

- 컷아웃은 원통 반지름 260cm라 폰 바로 앞의 큰 절벽 면은 구멍 테두리가 보인다. 값은 `Config/DefaultGame.ini` `[/Script/TDGame.TDTopDownSettings]`에서 바꾸고, 콘솔 변수 `td.TopDown.OcclusionCutout 0`으로 즉석에서 끈다.
- 거울이 없는 재질(위 5.2 목록)을 쓰는 큰 물체는 잘리지 않는다. 지금 레벨에서는 밧줄·데칼·물·안개·빛기둥뿐이다.
- PCG는 `GENERATE_ON_LOAD`이고 이미 생성된 결과를 저장하므로 실행 중에는 다시 생성하지 않는다. 에디터에서 다시 생성해도 그래프 패치로 거울 재질이 유지된다.
- 역광(D-46): 카메라를 향한 면(캐릭터 앞면, 절벽의 카메라 쪽 면)은 하늘빛·간접광만 받아 어둡다. 높은 벽 옆(남서 연결로 경계, 승강기 상단, 절벽 바로 앞)은 그림자가 화면을 더 덮는다. 열린 땅에서는 폰 주변 바닥에 넓은 정반사 하이라이트가 생긴다. 폰 바로 위 약 1.5m 안의 가림물은 어느 태양 방향이든 그림자를 남긴다(2026-10-16).

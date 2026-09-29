# 전투 애니메이션 품질 개선

## 2026-09-25 현행: `AS_TD_Player_SwordAttack01` (AAA 원본 동작 + 폴리시 도구, 우상→좌하 대각 베기)

사용자가 이전 기본 공격 전부(Attack01 RToL, Attack02 LToR, Attack03 모캡 후보)를 품질 불량으로 지우게 했다. 그리고 오른손 한손검 우→좌 대각 횡베기 하나를 AAA 수준으로 다시 만들게 했다. 요구 조건은 연계 1타, 루트 모션, 좋은 팔·손목·손 위치다. 이전 에셋 4개는 git 추적본이라 복구할 수 있고, 원본 파일 9개는 휴지통에 있다.

### 무엇이 달랐나
- **절차적 키 포즈를 버리고 AAA 원본 동작을 편집했다.** PJGame에는 Manny로 리타기팅된 Paragon Greystone 공격 20종이 있다. `editor_analyze_attack_candidates.py`로 오른손 궤적을 분류해, 한손 우상→좌하 하강 대각(손 174 cm → 76 cm)인 `Attack_PrimaryA`를 골랐다. Greystone의 PrimaryB(좌→우)·PrimaryC(수평)는 이 동작의 팔로스루에서 이어지는 연계로 설계되어 있다. Paragon 에셋은 Epic 무료 공개분이고 언리얼 프로젝트에서 상업 사용이 허용된다(Fab EULA 본문은 출시 전 확인 필요). 에셋 이름에는 Greystone을 쓰지 않는다.
- **근본 원인은 칼 쥐기였다.** 기존 미리보기는 칼을 `HandGrip_R`에 회전 0으로 붙였다. 그러면 칼날이 손가락 방향(손 축과 6°)으로 뻗는다. 이것이 이전 후보들의 손목·칼 문제의 공통 원인이다(L-anim-05).
  - 쥐기를 해부학적으로 다시 정의했다. 칼자루가 주먹 공간을 지나고, 칼날이 엄지 쪽으로 손 축과 73°를 이루며, 참날은 손가락 마디 쪽, 검지는 코등이에 닿는다.
  - 같은 원본에서 칼끝 최저가 −30.6 → 27.9 cm, 날 정렬 평균이 136° → 86°가 됐다.
- 두 차례 독립 다관점 비평(각 4명)과 v8·v9 검증 비평을 거쳐 다듬었다. 최종 검증은 "기계적 수정 3건(M1–M3) 후 경미 후속과 함께 합격"으로 판정했고, v10에서 그 수용 수치를 모두 통과했다. 비평 보고서는 `Saved/AgentOps/20260925/review-v3·v7·v8·v9/`에 있다.

### 폴리시 도구(`Tools/BlenderAnimation/sword_attack01_author.py`)가 한 일
- **시간 재매핑**(60 fps, 출력←원본): 1←1, 7←7, 10←9, 18←19, 22←23, 25←25.2, 34←33, 52←51, 96←79, 112←102.
  - 머리 감기는 1.25배 압축, 복귀는 0.64배로 늦췄다.
  - 타격은 f25–26(0.40–0.42 s)이고, 전체는 112프레임 1.85 s다.
- **팔 사슬 블렌드**(쇄골·위팔·아래팔·손)
  - 예비 동작: 캐릭터 공간에서 원본 f22.7→f23 자세(칼이 오른어깨 뒤)로 섞고, f22 이후에는 목표가 원본 시간과 같아진다. 쇄골은 부모 공간에서 절반만 섞는다. 가중치 키는 (4,0)(5,0)(13,1)(21,1)(24,0)(25,0)로 양 끝 기울기가 0이다.
    - f12–22에 칼끝이 오른쪽 뒤 위(높이 169–176 cm)로 천천히 감겨 드는 무빙 홀드가 된다. 칼 회전은 프레임당 1–3°이고, 원본의 머리 위 왼쪽 정지가 사라진다.
    - 풀림은 원본 타격 그대로다(16 → 120 → 119 → 123 → 112 cm/프레임).
  - 복귀: 부모 공간에서 원본 대기 자세로 40% 섞어(f51–105, 양 끝 기울기 0), 칼을 낮게 든 채 돌아오게 한다.
- **루트·발**
  - 루트 36 cm를 직접 작성했다(접촉까지 80%, f31에 완료).
  - 앞발(왼): f7에 떠서 smootherstep으로 36 cm 스텝하고, 착지 f21–24. 뒤꿈치 착지·발끝 들림이 있다.
  - 뒷발(오른): f26까지 볼을 고정하고, 누적 이동량은 f26–33에 걸쳐 푼다. 도달이 모자라면 발 자신의 가로축으로 뒤꿈치를 들되 발목–볼 경사는 68°까지만 허용하고, 나머지는 골반을 낮춘다. f26–36에 18 cm 끌어 붙이는 스텝, f67–81에 18 cm 복귀 스텝(볼 약 4 cm 들림).
  - 다리는 2본 IK로 풀고, 골반 하강은 원본의 75%에 도달 보정을 더했다.
- **팔·손목**
  - 팔은 원본 그대로다(팔꿈치 회전각 포함).
  - 손목 관절 파라미터(회내·굴곡·편위)는 원본을 쓰고 한계(굴곡 −78~60°, 편위 −40~28°) 초과분만 뺀다. 휩 구간 f23–35는 원본 그대로 보호한다.

### 산출물
- 에셋: `/Game/Characters/Mannequins/Anims/Sword/AS_TD_Player_SwordAttack01`(60 fps, 루트 모션 켬, RefPose 락)
- 몽타주: 없다. 런타임은 이 시퀀스를 UAF 주입으로 재생하고 판정·입력 창은 C++ 시간표 `FTDActionAnimation`이 정한다(2026-09-30 D44로 대체: 옛 `AM_TD_Player_SwordAttack01`(`DefaultSlot`, 섹션 `Attack` 0 s·`Recovery` f52 = 0.85 s, blend in 0.1·out 0.2)은 삭제됨)
- 원본 파일: [편집 원본 .blend](../AnimationSources/Player/AS_TD_Player_SwordAttack01.blend)(원본 Action 포함), [FBX](../AnimationSources/Player/AS_TD_Player_SwordAttack01.fbx), [제작 기록 JSON](../AnimationSources/Player/AS_TD_Player_SwordAttack01.json)
- 미리보기: [세 방향 실시간](Validation/BlenderAnimation/sword-attack01-three-views.gif), [1/3속](Validation/BlenderAnimation/sword-attack01-slow.gif), [손·게임 시점 1/3속](Validation/BlenderAnimation/sword-attack01-hand-game-slow.gif), [주요 포즈](Validation/BlenderAnimation/sword-attack01-keyposes.png), [스윙 구간](Validation/BlenderAnimation/sword-attack01-swing.png)

### 검증
| 항목 | 결과 |
|---|---|
| Blender→UE 뼈 위치 오차 | 최대 0.008 cm ([ue-validation](Validation/BlenderAnimation/sword-attack01-ue-validation.json)) |
| 루트 | 전방 36.0 cm, 옆·수직 0 |
| PIE(BP_TDCombatCharacter) | 1.85 s 재생, 실제 이동 36.0 cm, 슬롯 가중치 1.0 ([pie](Validation/BlenderAnimation/sword-attack01-pie.json)) (2026-09-30 D44로 대체: 몽타주·슬롯이 없어 슬롯 가중치 항목은 폐기. 루트모션 36.00 cm / 1.850 s는 헤드리스 `TDGame.Movement.RootMotionActionMovesPawn`이 재확인하고, PIE 공격 이동 36.0 cm는 [player-pie-check](Validation/Movement/player-pie-check.json)에 있다) |
| 궤적 | 활성 평면 잔차 4.0 cm, 하강 97 cm, 우→좌, 칼끝 최고 74 m/s, 타격 f25 = 0.40 s ([measure](Validation/BlenderAnimation/sword-attack01-measure.json)) |
| 날·팔 | 날 정렬 평균 79°(이상 90°), 팔–칼 각 140°, 프레임당 최대 뼈 회전 71°, 골반 → 팔·손 각속도 순서(f21 → f26) |
| 간격 | 칼끝 최저 23 cm, 칼–몸 최소 18 cm, 관통 없음, f112 = f1(루프 0.05 cm 이내) |
| 손목 | 휩 f26–32에서만 원본 수준의 순간 굽힘(최대 약 89°). 이 구간 편위 값은 굽힘 80° 이상 특이점이라 의미 없음 |

### 게임 무기 부착값
`BP_TDCombatCharacter`에는 아직 무기가 없다. 붙일 때 아래 값을 쓰면 미리보기와 같은 쥐기가 된다(정합 잔차 0.0001 cm, [attachment](Validation/BlenderAnimation/sword-attack01-attachment.json)).
- 대상: `HandGrip_R` 소켓 기준 `SM_Sword` 상대 변환
- 위치: (−5.052, 9.119, 25.857) cm
- 회전: roll −83.734°, pitch −15.888°, yaw 94.203°

### 연계·판정 권장 창 (60 fps, 노티파이는 삭제됨 — 창은 C++ 시간표 `FTDActionAnimation`에 초 단위로 넣는다, D44)
| 창 | 프레임 |
|---|---|
| 판정 | f23–f28 |
| 트레일 | f23–f30 |
| 입력 버퍼 | f12–f34 |
| 2타 분기 | f35–f48(2타 시작 자세 ≈ f39–41 = Greystone PrimaryB 첫 자세) |
| 회피 취소 | f33부터 |
| 이동 취소·Recovery 시작 | f52 (옛 몽타주 섹션 `Recovery`, 2026-09-30 D44로 몽타주 삭제) |

### 남은 한계
- 무게감 있는 Greystone 스타일을 물려받았다: 팔로스루에서 가슴이 약 145° 돌고 골반이 약 23 cm 내려간다. 원본에서 물려받은 경미 항목은 휩 순간 손목 굽힘 −89°, 접촉 직전 팔꿈치 잠김, 앞발 볼의 작은 표류다.
- 홀드에서 타격으로 풀리는 첫 프레임이 최고 속도에 가깝다(스냅형 풀림). 트레일·쓸기 판정을 전제로 한다.
- 복귀는 칼을 낮게 든 느린 회수로 바꿨지만 여전히 몸 앞을 지난다.
- 연계용 2타는 미제작이다.
- 무기 부착 C++, 무장 대기 동작이 없다. 판정·입력 창은 노티파이가 아니라 C++ 시간표(`FTDActionAnimation`)에 넣는다(2026-09-30 D44로 대체: 노티파이 삭제).
- UE 압축 후 칼끝 오차는 에디터 평가에서 0으로 나와 쿠킹 후 검증은 미실시다.

```powershell
# Context: C:/Project/TDGame; Blender MCP + 언리얼 에디터. 순서: 참고 내보내기 → 씬·베이크 → 폴리시 → 측정 → 렌더·합성 → 내보내기 → UE 가져오기 → 검증 → 부착 역산 → PIE
python Tools/run_in_editor.py Tools/BlenderAnimation/editor_export_reference_fbx.py
python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_scene.py
python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_author.py
python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_measure.py
python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_render.py "TD_OUT='v9'"
python Tools/BlenderAnimation/sword_attack01_compose.py v9 --gif --triptych front,right,game --tile 240 --every 2
python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_export.py
python Tools/BlenderAnimation/blender_run.py Tools/BlenderAnimation/sword_attack01_grip_points.py
python Tools/BlenderAnimation/import_sword_attack01.py --replace   # 몽타주 단계 제거(D44), PIE 뒤에는 에디터 재시작이 필요할 수 있다(L-editor-10)
python Tools/run_in_editor.py Tools/BlenderAnimation/editor_validate_sword_attack01.py
python Tools/run_in_editor.py Tools/BlenderAnimation/editor_solve_sword_attachment.py
python Tools/BlenderAnimation/validate_sword_attack01_pie.py
```

---

아래 두 절(2026-09-19 Attack01 RToL, 2026-09-23 Attack02 LToR)의 에셋과 원본은 2026-09-25에 삭제됐다. 절차 기록으로만 남긴다. (2026-09-30 D44로 대체: 이 아래의 몽타주·`DefaultSlot`·`ABP_Unarmed`·노티파이·`TDPlayMeleeMontage` 언급은 당시 경로이며 모두 폐기됐다. 런타임은 시퀀스를 UAF로 주입해 재생하고 판정·입력 창은 C++ 시간표 `FTDActionAnimation`이다. 몽타주 의존 스크립트는 `Tools/_archive/2026-09/BlenderAnimation/`으로 옮겨 실행할 수 없다.)

## 2026-09-19 (삭제됨): `AS_TD_Player_Attack01_SwordSlash_RToL` (절차적 저작, 참고 모션 없음)

사용자가 이전 후보(v02~v05·RToL_Blender·`Anims/Sword/AS_Sword_Slash_01`, 언리얼 10개 + `AnimationSources/Player` 원본 15개)를 품질 불량으로 모두 지우게 했고, 새 기본 공격 하나를 다시 만들었다. 요구: 한손검, 오른손, 캐릭터 기준 우→좌 횡베기, 루트 모션, 한 발 전진, 오른손 본에 검을 붙였을 때 검 궤적이 보기 좋을 것.

- 저작 방식(`Tools/BlenderAnimation/author_sword_slash.py`): 참고 모션 대신 코드로 정의한 키 포즈를 30fps 40포즈(1.3초)로 베이크한다. 발은 접지 모델(볼·뒤꿈치 피벗, 접지 중 이동 0)로, 다리·팔은 해석적 2본 IK로 푼다. 오른팔 팔꿈치는 손목 비틀림 최소화 + 힌트 방향 + 이전 프레임 연속성의 비용으로 고른다. 오른손 회전은 검 날 방향·날 선 방향(궤적 접선)에서 `HandGrip_R` 소켓을 거꾸로 풀어 정한다. 하박 twist 본에 손 롤을 0.62/0.30으로 나눈다. 손가락은 `MM_Attack_01` 첫 프레임 주먹을 오른손 0.88·왼손 0.32로 재사용한다.
- 동작 설계: 준비(f0~f8, 상체 우측 50° 코일·검을 오른쪽 뒤로) → 왼발 스텝(f5 이탈, f12 뒤꿈치 착지, f14 평발) → 타격(f13~f19, 골반이 먼저 열리고 상체·검이 따라옴, 접촉 f17=0.57초, 검 끝 높이 약 110cm) → 팔로스루(f19~f24, 검이 왼쪽 아래로) → 오른발 끌어당김(f21~f27) → 회복(f28~f39, 시작 자세 + 전진 50cm). 루트 전진 50cm.
- 무기: 미리보기는 실제 `SM_Sword`를 `HandGrip_R` 소켓 프로파일 + 메시 피벗 보정(언리얼 상대 위치 `(0, 32.2, -1.4)`cm)으로 붙였다. 게임 `BP_TDCombatCharacter`에는 아직 무기 컴포넌트가 없으므로 실제 부착 검증은 하지 않았다.
- 산출물: `/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_SwordSlash_RToL`(루트 모션 켬, RefPose 락), `AM_…_SwordSlash_RToL`(`DefaultSlot`, 섹션 `Attack01`, blend in 0.1·out 0.2), [편집 원본 .blend](../AnimationSources/Player/AS_TD_Player_Attack01_SwordSlash_RToL.blend), [FBX](../AnimationSources/Player/AS_TD_Player_Attack01_SwordSlash_RToL.fbx), [저작 기록 JSON](../AnimationSources/Player/AS_TD_Player_Attack01_SwordSlash_RToL.json). (2026-09-30 D44로 대체: 몽타주는 폐기)
- 미리보기: [세 방향 실시간](Validation/BlenderAnimation/sword-slash-three-views.gif), [1/3속](Validation/BlenderAnimation/sword-slash-slow.gif), [정면](Validation/BlenderAnimation/sword-slash-front.gif)·[측면](Validation/BlenderAnimation/sword-slash-side.gif)·[게임 시점](Validation/BlenderAnimation/sword-slash-game.gif), [주요 포즈](Validation/BlenderAnimation/sword-slash-poses.png), [검 끝 궤적 평면도](Validation/BlenderAnimation/sword-slash-tip-path.png).
- 검증: [변환·접지·루트 검사](Validation/BlenderAnimation/sword-slash-validation.json) — Blender→언리얼 본 위치 오차 최대 0.007cm, 접지 중 볼 이동 최대 0.0024cm, 루트 이동 50.0cm, 검 끝 최저 높이 11.8cm. [PIE](Validation/BlenderAnimation/sword-slash-pie.json)([스크린샷](Validation/BlenderAnimation/sword-slash-pie.png)) — `BP_TDCombatCharacter`에서 1.30초 재생, `DefaultSlot` 최대 가중치 1.0, 실제 이동 45.1cm(블렌드 구간 포함). (2026-09-30 D44로 대체: 슬롯 가중치는 몽타주 경로의 값이며 폐기)
- 시각 판단(에이전트, 정지 프레임·연속 포즈·손 근접 렌더 기준): 코일→스텝→회전→팔로스루 순서가 읽히고, 타격 구간에서 날이 진행 방향을 향하며 검 끝 궤적이 한 평면에 가깝다. 손목 비틀림은 twist 본으로 분산되어 근접 렌더에서 꺾임이 보이지 않았다. 사용자의 재생 승인은 별도다.
- 미검증·한계: 실제 무기 부착 상태 게임 재생, 공격 입력·데미지 노티파이·콤보 연결, 왼손은 중립 손목(별도 연출 없음), 다른 캐릭터 이식. (2026-09-30 D44로 대체: 데미지 노티파이는 삭제, 판정은 C++ 시간표 `HitWindows`)

```powershell
# Context: C:/Project/TDGame; Blender MCP + 언리얼 에디터 열림. 순서: 저작(TD_SAVE) → 미리보기 렌더 → 합성 → 가져오기·몽타주 → 검증 → PIE (2026-09-30 D44로 대체: import_sword_slash.py·validate_sword_slash_pie.py는 몽타주 의존이라 _archive로 이동, 실행 불가)
Tools/BlenderMCP/.venv/Scripts/python.exe Tools/BlenderMCP/call_tool.py --code Tools/BlenderAnimation/author_sword_slash.py
Tools/BlenderMCP/.venv/Scripts/python.exe Tools/BlenderMCP/call_tool.py --code Tools/BlenderAnimation/render_sword_slash_preview.py
python Tools/BlenderAnimation/compose_sword_slash_preview.py
python Tools/BlenderAnimation/import_sword_slash.py
python Tools/BlenderAnimation/validate_sword_slash.py
python Tools/BlenderAnimation/validate_sword_slash_pie.py
```

`call_tool.py --code`는 파일 본문을 그대로 실행하므로 저장하려면 본문 첫 줄에 `TD_SAVE = True`를 두거나 `execute_blender_code`에서 `exec(..., {'TD_SAVE': True})`로 넘긴다.

## 2026-09-23 (삭제됨): `AS_TD_Player_Attack02_SwordSlash_LToR` (블렌더 확장 툴 및 인체 역학 개선, 좌→우 횡베기)

공격 1번(우→좌)에 이어지는 2타 콤보 한손검 좌→우 횡베기 공격 2번 애니메이션 저작. 기존 절차적 모션의 치명적 결함(왼팔 몸통 파고듦, 우측 팔꿈치 과신전/급격한 스냅)을 블렌더 확장 툴(AnimAide F-Curve 이징 보간, 인체 해부학적 가동 범위 및 안정 힌트 벡터)을 적용하여 전면 개선했다.

- 저작 방식(`Tools/BlenderAnimation/author_sword_slash_ltor.py`): 30fps 40포즈(1.3초), 루트 전진 50cm.
- 개선 사항:
  1. 왼팔 가슴 관통 완전 제거: 왼손 구면 방위각을 흉부 안쪽(-20°~-30°)에서 몸통 외측(-55°~-88°)으로 재배치하고 팔꿈치 힌트 벡터를 갈비뼈 외측/후방으로 고정하여 전 프레임 관통 0cm 및 20cm 이상 안전 이격 확보.
  2. 오른팔 팔꿈치 꺾임/과신전 제거: 2본 IK 팔꿈치 후보 각도를 해부학적 운반각(Carrying Angle) 기준 ±30°로 제한하고 외측-하방 성분을 유지하여 13~21프레임 40cm 점프 현상 완전 제거.
  3. AnimAide 베지에 이징 적용: Action F-Curve 키프레임 보간을 `BEZIER` 및 `AUTO_CLAMPED`로 전환하여 부드러운 가속·감속 및 관성 운동 구현.
- 산출물: `/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack02_SwordSlash_LToR`(Root Motion 활성화, RefPose 락), `AM_TD_Player_Attack02_SwordSlash_LToR`([편집 원본 .blend](../AnimationSources/Player/AS_TD_Player_Attack02_SwordSlash_LToR.blend), [FBX](../AnimationSources/Player/AS_TD_Player_Attack02_SwordSlash_LToR.fbx), [JSON](../AnimationSources/Player/AS_TD_Player_Attack02_SwordSlash_LToR.json)).
- 시각 산출물: [주요 포즈 모음](Validation/BlenderAnimation/sword-slash-ltor-poses.png), [세 방향 실시간](Validation/BlenderAnimation/sword-slash-ltor-three-views.gif), [1/3속](Validation/BlenderAnimation/sword-slash-ltor-slow.gif), [정면](Validation/BlenderAnimation/sword-slash-ltor-front.gif)·[측면](Validation/BlenderAnimation/sword-slash-ltor-side.gif)·[게임 시점](Validation/BlenderAnimation/sword-slash-ltor-game.gif).
- 검증: [수치 검증](Validation/BlenderAnimation/sword-slash-ltor-validation.json) 통과 (`technical_checks_passed: true` — 본 위치 오차 최대 0.1188cm, 루트 전진 50.0cm 정확 일치, 오른발 착지 드리프트 0.1171cm, 검 끝 최저 높이 21.26cm).


---

아래는 이전 후보(v04)의 기록이다. 해당 에셋과 원본은 2026-09-19에 삭제되었고 절차만 참고한다.

2026-09-17. 사용자가 선택한 기준은 **무게감 있는 액션 게임 스타일**이다. 첫 제자리 횡베기는 파일 교환 검증용 수준이었고, 사용자가 다리와 동작 품질을 지적했다. 현재 검토 후보는 `AS_TD_Player_Attack01_Heavy_RToL_v04`이다. 기술 검증과 사용자의 시각적 승인은 구분한다.

## 첫 결과가 어색했던 이유

`author_player_slash.py`는 양발의 위치와 회전을 전체 구간에서 고정하고, 골반을 약간 위아래로만 움직였다. 상체 관절은 같은 진행값으로 동시에 돌았고, 각 키 사이에 독립적인 smoothstep을 적용해 여러 키에서 속도가 끊겼다. 발을 딛고 밀거나 발끝을 축으로 도는 동작, 체중을 옮겨 받는 스텝이 없었다. 가상의 막대로 궤적만 검토해 실제 검을 잡는 자세도 충분히 평가하지 못했다.

앞서 보고한 0.027cm 변환 오차와 거의 0인 발 이동량은 FBX 교환이 정확하다는 근거였다. 그것으로 자연스러운 전투 동작을 판단하면 안 된다.

## 이번에 적용한 방법

1. 실제 프로젝트의 `MM_Attack_01`, `MM_Attack_02`, `MM_Attack_03`을 메시와 함께 검토했다. 오른팔이 오른쪽에서 왼쪽으로 이동하고 전진 스텝이 있는 `MM_Attack_01`을 골랐다. 02는 반대쪽이 주도하고, 03은 발차기를 포함하므로 사용하지 않았다.
2. 원본의 전신 움직임을 보존하고 준비·타격·회수 구간을 다시 배분했다. 30fps, 43개 프레임 간격/44포즈, 총 약 1.43초다. 골반과 스텝에 비해 상체를 3프레임(0.1초) 늦춰, 베기가 앞발 착지와 더 가깝게 맞도록 조정했다. 끝에 회복용 3프레임을 추가했다. 원본의 약 150.6cm 전진을 유지했으며 `Root Motion`을 켰다. 제자리 공격으로 바꾸려면 발 접지와 스텝을 함께 다시 설계해야 한다.
3. Blender의 `Copy Rotation` 제약과 별도 `TD_WeaponAim` 컨트롤로 오른손의 검 방향을 편집한다. 원본 Action과 참고 Armature도 `.blend`에 보존한다. 전체 몸을 레스트 포즈에서 다시 계산하지 않는다.
4. 프로젝트의 실제 `/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword`를 Blender 미리보기에 연결했다. 해당 메시의 실루엣과 손잡이 위치를 사용하며 머티리얼은 검토용 단색이다. 런타임 캐릭터에 무기 장비 기능을 추가한 것은 아니다.
5. 정면·측면·게임 시점으로 132프레임을 렌더링했다. 실시간/느린 재생 GIF와 포즈 모음을 제공하며, 시각 판단을 FBX 검증 수치와 구분한다.

참고 Action을 다른 Armature에 바로 연결했을 때 약 4.7cm의 본 위치 차이와 착지 구간의 약 3.5cm 발끝 이동을 발견했다. 본 이름이 같아도 가져온 리그의 기준 포즈가 같다는 뜻은 아니었다. 최종 v04은 참고 리그의 평가된 포즈를 대상 리그의 기준 공간으로 변환한 뒤 베이크한다. 원본 하체와 비교한 위치 차이는 0.001cm 미만이며, 오른발 접지 이후 발끝 수평 변화는 약 0.17cm로 원본 수준을 유지한다.

## 도구 선택

| 선택 | 어떤 문제를 해결하는가 | 현재 적용/한계 |
| --- | --- | --- |
| 좋은 참고 모션 + Blender Action/제약/곡선 편집 | 스텝·골반·상체가 연결된 움직임을 보존하며 공격을 변형 | 이번 후보에 적용. 참고와 다른 종류의 공격은 별도 동작 설계 필요 |
| Rigify | IK/FK, 발·무릎·골반 등을 다루는 편집용 리그 구성 | 이 PC의 Blender에서 설치된 모듈 확인. 이번 후보에는 새 Rigify 리그를 생성하지 않음 |
| Auto-Rig Pro + Quick Rig | 기존 스켈레톤을 보존하며 컨트롤 리그 연결 | 유료 후보. 공식 문서에 UE5 Manny 왕복과 `Preserve` 방식이 명시됨. 구매·설치·Blender 5.2 호환 검증은 수행하지 않음 |
| Cascadeur AutoPhysics | 이미 작성한 포즈의 무게중심·접촉점·물리적 움직임 보정 | 별도 프로그램 후보. 이 환경에 설치하거나 MCP 연결을 검증하지 않음 |

Blender 안에서 반복 제작하려면 먼저 참고 모션과 편집용 리그를 갖추는 것이 유용하다. 많은 Manny 동작을 계속 만들 계획이면 Auto-Rig Pro + Quick Rig의 원본 스켈레톤 보존 경로를 검토할 가치가 있다. 물리적 무게 이동의 보정이 주된 병목이면 Cascadeur를 별도 검토한다. 어느 도구도 설치만으로 원하는 연출과 게임 감각을 자동 보장하지 않는다.

공식 근거: [Auto-Rig Pro Quick Rig / UE5 Manny 왕복](https://www.lucky3d.fr/auto-rig-pro/doc/quick_rig_doc.html), [Auto-Rig Pro 버전 변경 기록](https://www.lucky3d.fr/auto-rig-pro/doc/updates_log.html), [Blender 5.2 Rigify 문서](https://docs.blender.org/manual/en/5.2/addons/rigify/index.html), [Cascadeur AutoPhysics](https://cascadeur.com/help/tools/physics_tools/autophysics), [Cascadeur 접촉점](https://cascadeur.com/help/tools/physics_tools/fulcrum_points).

## 새 후보와 검증

- 애니메이션: `/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_Heavy_RToL_v04`
- 몽타주: `/Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_Heavy_RToL_v04`, `DefaultSlot`, `Attack01` (2026-09-30 D44로 대체: 몽타주는 폐기)
- [편집 원본](../AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v04.blend), [FBX](../AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v04.fbx)
- [정면 미리보기](Validation/BlenderAnimation/weighty-v04-front.gif), [세 방향 실시간 미리보기](Validation/BlenderAnimation/weighty-v04-three-views.gif), [느린 미리보기](Validation/BlenderAnimation/weighty-v04-slow.gif), [주요 포즈](Validation/BlenderAnimation/weighty-v04-poses.png)
- [변환·원본 보존·접지 검사](Validation/BlenderAnimation/weighty-v04-validation.json), [PIE 재생과 실제 이동](Validation/BlenderAnimation/weighty-v04-pie.json)

PIE의 실제 `BP_TDCombatCharacter`에서 몽타주가 약 1.43초 재생되었고 `DefaultSlot` 최대 가중치는 1.0, 캐릭터 실제 이동은 약 150.61cm였다(2026-09-30 D44로 대체: 몽타주·슬롯 경로는 폐기). 테스트 중 변경한 PIE 카메라와 본 갱신 설정은 PIE 종료 시 사라진다. 공격 입력/피격 판정은 연결하지 않았다.

v02는 기준 포즈 차이가 남은 중간 후보, v03은 착지보다 타격이 앞선 후보이며 v04를 검토 대상으로 사용한다. 기존 결과는 덮어쓰지 않았다. 현재 후보는 사람형 전진 횡베기이고, 다족 몬스터나 제자리 공격의 품질까지 검증한 것은 아니다. 무기 접촉·팔/몸 관통과 게임 감각의 최종 판단은 미리보기에서 별도 확인해야 한다.

독립 시각 검토에서는 v04의 23~24프레임에서 앞발 접지, 앞무릎 굽힘, 뒤 발끝의 밀어내기가 공격 자세와 맞물리는 개선을 확인했다. 검사한 프레임에서 새로운 명백한 몸통 관통이나 다리 뒤틀림은 발견하지 못했다. 높은 왼손 가드와 머리 가까운 회수에는 원본 권투 모션의 흔적이 남아 있다. 검술 전용 참고 모션과 실제 장비 소켓을 적용하는 단계에서는 이 부분을 더 다듬는다. 이 평가는 정지 프레임과 연속 포즈 검토이며 사용자의 재생 품질 승인을 대신하지 않는다.

에셋 가져오기·저장은 PIE를 끝낸 후 수행한다. Python FBX 도구에 PIE 중 가져오기를 거부하는 사전 검사를 추가해, 메모리에는 생성되지만 저장되지 않는 결과를 예방했다.

## 재사용 스킬

Codex 설치본: `C:/Users/jjh/.codex/skills/td-combat-animation-quality/SKILL.md`.
Claude·Gemini와 공유할 프로젝트본: [Tools/BlenderAnimation/SKILL.md](../Tools/BlenderAnimation/SKILL.md).

새 세션에서 `$td-combat-animation-quality`를 사용하거나 에이전트에게 프로젝트본 경로를 읽도록 요청한다. 스킬은 참고 모션 선택, 접지/피벗/스텝 구분, 실제 무기, 세 시점과 재생 속도별 검토, 원본 스켈레톤 보존을 안내한다. 스킬 자체가 새로운 모션 생성 모델인 것은 아니다.

```powershell
# Context: C:/Project/TDGame; Unreal editor open
python Tools/BlenderAnimation/validate_weighty_slash.py
```

# 전투 애니메이션 품질 개선

2026-09-17. 사용자가 선택한 기준은 **무게감 있는 액션 게임 스타일**이다. 첫 제자리 횡베기는 파일 교환 검증용 수준이었고, 사용자가 다리와 동작 품질을 지적했다. 현재 검토 후보는 `AS_TD_Player_Attack01_Heavy_RToL_v03`이다. 기술 검증과 사용자의 시각적 승인은 구분한다.

## 첫 결과가 어색했던 이유

`author_player_slash.py`는 양발의 위치와 회전을 전체 구간에서 고정하고, 골반을 약간 위아래로만 움직였다. 상체 관절은 같은 진행값으로 동시에 돌았고, 각 키 사이에 독립적인 smoothstep을 적용해 여러 키에서 속도가 끊겼다. 발을 딛고 밀거나 발끝을 축으로 도는 동작, 체중을 옮겨 받는 스텝이 없었다. 가상의 막대로 궤적만 검토해 실제 검을 잡는 자세도 충분히 평가하지 못했다.

앞서 보고한 0.027cm 변환 오차와 거의 0인 발 이동량은 FBX 교환이 정확하다는 근거였다. 그것으로 자연스러운 전투 동작을 판단하면 안 된다.

## 이번에 적용한 방법

1. 실제 프로젝트의 `MM_Attack_01`, `MM_Attack_02`, `MM_Attack_03`을 메시와 함께 검토했다. 오른팔이 오른쪽에서 왼쪽으로 이동하고 전진 스텝이 있는 `MM_Attack_01`을 골랐다. 02는 반대쪽이 주도하고, 03은 발차기를 포함하므로 사용하지 않았다.
2. 원본의 전신 움직임을 보존하고 준비·타격·회수 구간을 다시 배분했다. 30fps, 40개 프레임 간격/41포즈, 총 약 1.33초다. 원본의 약 150.6cm 전진을 유지했으며 `Root Motion`을 켰다. 제자리 공격으로 바꾸려면 발 접지와 스텝을 함께 다시 설계해야 한다.
3. Blender의 `Copy Rotation` 제약과 별도 `TD_WeaponAim` 컨트롤로 오른손의 검 방향을 편집한다. 원본 Action과 참고 Armature도 `.blend`에 보존한다. 전체 몸을 레스트 포즈에서 다시 계산하지 않는다.
4. 프로젝트의 실제 `/Game/DarkFantasyTopDown/StaticMeshes/Weapons/SM_Sword`를 Blender 미리보기에 연결했다. 해당 메시의 실루엣과 손잡이 위치를 사용하며 머티리얼은 검토용 단색이다. 런타임 캐릭터에 무기 장비 기능을 추가한 것은 아니다.
5. 정면·측면·게임 시점으로 123프레임을 렌더링했다. 실시간/느린 재생 GIF와 포즈 모음을 제공하며, 시각 판단을 FBX 검증 수치와 구분한다.

참고 Action을 다른 Armature에 바로 연결했을 때 약 4.7cm의 본 위치 차이와 착지 구간의 약 3.5cm 발끝 이동을 발견했다. 본 이름이 같아도 가져온 리그의 기준 포즈가 같다는 뜻은 아니었다. 최종 v03은 참고 리그의 평가된 포즈를 대상 리그의 기준 공간으로 변환한 뒤 베이크한다. 원본 하체와 비교한 위치 차이는 0.001cm 미만이며, 오른발 접지 이후 발끝 수평 변화는 약 0.17cm로 원본 수준을 유지한다.

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

- 애니메이션: `/Game/Characters/Mannequins/Anims/Blender/AS_TD_Player_Attack01_Heavy_RToL_v03`
- 몽타주: `/Game/Characters/Mannequins/Anims/Blender/AM_TD_Player_Attack01_Heavy_RToL_v03`, `DefaultSlot`, `Attack01`
- [편집 원본](../AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v03.blend), [FBX](../AnimationSources/Player/AS_TD_Player_Attack01_Heavy_RToL_v03.fbx)
- [세 방향 실시간 미리보기](Validation/BlenderAnimation/weighty-v03-three-views.gif), [느린 미리보기](Validation/BlenderAnimation/weighty-v03-slow.gif), [주요 포즈](Validation/BlenderAnimation/weighty-v03-poses.png)
- [변환·원본 보존·접지 검사](Validation/BlenderAnimation/weighty-v03-validation.json), [PIE 재생과 실제 이동](Validation/BlenderAnimation/weighty-v03-pie.json)

PIE의 실제 `BP_TDCombatCharacter`에서 몽타주가 약 1.33초 재생되었고 `DefaultSlot` 최대 가중치는 1.0, 캐릭터 실제 이동은 약 150.61cm였다. 테스트 중 변경한 PIE 카메라와 본 갱신 설정은 PIE 종료 시 사라진다. 공격 입력/피격 판정은 연결하지 않았다.

v02는 Action 연결 시 기준 포즈 차이가 남은 중간 후보이며 v03을 검토 대상으로 사용한다. 기존 결과는 덮어쓰지 않았다. 현재 후보는 사람형 전진 횡베기이고, 다족 몬스터나 제자리 공격의 품질까지 검증한 것은 아니다. 무기 접촉·팔/몸 관통과 게임 감각의 최종 판단은 미리보기에서 별도 확인해야 한다.

## 재사용 스킬

Codex 설치본: `C:/Users/jjh/.codex/skills/td-combat-animation-quality/SKILL.md`.
Claude·Gemini와 공유할 프로젝트본: [Tools/BlenderAnimation/SKILL.md](../Tools/BlenderAnimation/SKILL.md).

새 세션에서 `$td-combat-animation-quality`를 사용하거나 에이전트에게 프로젝트본 경로를 읽도록 요청한다. 스킬은 참고 모션 선택, 접지/피벗/스텝 구분, 실제 무기, 세 시점과 재생 속도별 검토, 원본 스켈레톤 보존을 안내한다. 스킬 자체가 새로운 모션 생성 모델인 것은 아니다.

```powershell
# Context: C:/Project/TDGame; Unreal editor open
python Tools/BlenderAnimation/validate_weighty_slash.py
```

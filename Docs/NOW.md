# NOW — 현재 상태 한 장

갱신: 2026-09-19 02:35 antigravity
형식: `AGENTS.md` 15절 / `Docs/AgentRules.md` OP-07. 세션 종료 시 덮어쓴다(재작성 전에 다시 읽고 남의 항목을 보존).

## ① 진행 중 (대장 doing)

아래 P 항목은 담당 필드가 없고 마지막 기록이 2026-09-12(claude)라 방치 상태다. 다음 세션은 사용자와 함께 `todo`로 되돌리거나 남은 조건을 확인한 뒤 청구한다(OP-11).

- 월드·던전(P): P2-02 P2-04 P2-05 P2-06 P2-07 P2-09 (`Docs/Tasks/phase-2-dungeon-vertical-slice.md`, 2026-09-12 claude, 담당 없음) · P3-00 P3-02 P3-03 P3-04 P3-05 P3-07 P3-08 P3-09 P3-11 (`Docs/Tasks/phase-3-outdoor-generator.md`, 2026-09-12 claude, 담당 없음)
- 몬스터 AI(M): doing 없음 (Phase 0 미착수, `Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md`)
- 애니메이션(A~G): doing 없음 (우선순위 높음 todo: A-02 A-03 A-04 C-01 F-01, `Docs/AnimationAuthoring_Tasks.md`). 현행 기본 공격은 2026-09-19 새로 만든 `AS_TD_Player_Attack01_SwordSlash_RToL`(D-29 갱신, `Docs/AnimationQuality.md` 첫 절). 공통 접지 QA 도구 `Tools/BlenderAnimation/validate_contacts.py` 구현 완료(2026-09-19 antigravity). 마우스 좌 클릭 이동 애니메이션 미재생 버그 C++ 해결 완료(`bUseAccelerationForPaths`, 2026-09-19 antigravity).
- 공통: 관리 체계 도입 완료(2026-09-18 claude), Claude 서브에이전트 6개 정의; 각 에이전트 드라이런 미실시

## ② 막힘·결정 대기

- 사용자 시각 승인 대기: 새 공격 애니메이션 미리보기(`Docs/Validation/BlenderAnimation/sword-slash-three-views.gif`·`-slow.gif`·`-poses.png`). 수정 요청은 `Tools/BlenderAnimation/author_sword_slash.py`의 키 트랙 값만 바꿔 재실행
- 게임 무기 부착 미구현: `BP_TDCombatCharacter`에 무기 컴포넌트 없음. 붙일 때 소켓 `HandGrip_R` + `SM_Sword` 상대 위치 (0, 32.2, -1.4)cm (C++ 구현, 7절)
- 남은 사용자 조치: D-24 재정규화 커밋, 이번 삭제·신규 파일 커밋(LFS: .blend/.fbx/.uasset/.gif/.png 확인됨), `Config/DefaultEditorSettings.ini`(PIE 스로틀 끔, L-editor-06) 커밋
- PIE 속도: 2026-09-19 원인 2가지 해결(L-editor-06). 편집기 뷰포트 Navigation 표시(P키)를 다시 켜면 이 맵의 PIE는 다시 약 2fps가 된다. 진단은 `python Tools/pie_profile.py`
- `Docs/Tasks/decisions.md#D-02`~`#D-05` (미정), `#P0-D2`, `#P2-D3`; `07-roadmap-and-tasks.md#M3-10`, `#MD-05`; blocked: M2-10 M3-08 M4-01~M4-05

## ③ 다음 행동

1. 사용자가 GIF·포즈표를 재생 확인 → 수정 지점(타이밍·높이·궤적)을 말해주면 `author_sword_slash.py` 트랙 값 조정 → 렌더·합성·가져오기·검증 재실행(명령은 `Docs/AnimationQuality.md` 첫 절).
2. 승인 뒤: C++ 무기 부착(소켓·오프셋 위), `TDPlayerPrimaryAttack01Ability`/전투 액션의 몽타주 연결과 `UTDAnimNotifyState_MeleeAttack` 배치(C-01).
3. 다음 에이전트 세션: `AGENTS.md` 15절대로 시작 → P 대장 doing 15건을 사용자와 정리(todo 복귀 또는 담당 청구).

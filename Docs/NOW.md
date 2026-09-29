# NOW — 현재 상태 한 장

갱신: 2026-09-29 23:40 claude

## ① 진행 중 (대장 doing)

- 월드·던전: P2-02·04·05·06·07·09 / P3-00·02·03·04·05·07·08·09·11. 담당 없음, 마지막 기록 2026-09-12 claude.
- 몬스터 AI: doing 없음. 게임 경로 수직 슬라이스 완료 — `Source/TDGame/MonsterAI/`, JSON 7종, LV-Cambat 7무리 25마리(09-25 고블린·스톤 골렘 리타기팅 추가), 테스트 `TDGame.MonsterAI.*` 9/9, PIE passed(증거 경로는 07 대장). 07 대장 M0-06·M1-01~07·M3-01·02 기록 갱신(M1-04만 done). 절차 `Docs/MonsterAI_CombatSim_Plan.md` §5.1.
- 애니메이션: 기본 공격을 `AS_TD_Player_SwordAttack01`(Greystone PrimaryA 편집, 해부학적 칼 쥐기)로 교체했다. 이전 Attack01·02·03은 삭제했다. 절차와 수치는 `Docs/AnimationQuality.md` 첫 절(L-anim-05·06).
- 공통: 관리 체계 유지 중.
- 최근 완료: claude 09-29 엔진 플러그인 Mover·UAF(+UAFAnimGraph·UAFStateTree·UAFChooser)·MoverAnimNext·GameplayCameras·Chooser·SmartObjects 활성화(빌드·에디터 로드 확인) / codex MegaMagic 10종(자동화 37/37·PIE 10/10, `Docs/Validation/combat/megamagic-validation-2026-09-24.md`) / claude 09-25 오픈월드 심리스 테스트 회귀 수정(L-editor-09, .uasset 미커밋).

## ② 막힘·결정 대기

- 이동·애니 전면 교체(CMC·AnimBP → Mover·UAF, APawn, StateTree 미사용) 계획 제시, 사용자 작업 지시·D4/AGENTS 13절 개정 승인 대기. 근거 Worklog [2026-09-29 23:40].
- 새 기본 공격 시각 확인(사용자): `Docs/Validation/BlenderAnimation/sword-attack01-three-views.gif`·slow·hand-game-slow·keyposes.
- BP_TDCombatCharacter 무기 부착 미구현(C++ 필요). 부착값은 HandGrip_R 기준 SM_Sword (−5.052, 9.119, 25.857) cm, roll −83.734·pitch −15.888·yaw 94.203(이전 값 (0,32.2,−1.4)은 권총식이라 틀림). 플레이어 근접 공격 몽타주에 노티파이가 없어 몬스터는 현재 주문(1~0키)으로만 잡힌다.
- 08 전투 재미 설계 사용자 검토(D39~D43 후보). MD-11은 사용자 커밋 dc9364e로 해소.
- 에디터 안 자동화로 `TDGame.Combat` 전체를 돌리면 MegaMagic.VisualTail에서 에디터 크래시 → 헤드리스로 실행(L-editor-08).
- PIE: Navigation 표시(P키)를 켜면 약 2fps 재발 가능(L-editor-06).

## ③ 다음 행동

1. 새 기본 공격 사용자 검토. 연계 2타는 같은 도구로 Greystone PrimaryB에서 만든다(시작 자세 ≈ f39–41).
2. 무기 부착 C++·몽타주 노티파이(판정 f23–28, 입력 버퍼 f12–34, 2타 분기 f35–48) 연결 — 플레이어 근접이 몬스터를 때리게 된다.
3. 몬스터: 잡몹 몸 `ATDMonsterPawn`(M3-01), 시뮬 몸(M1-06), 골렘 내려찍기 동작·고블린 무기 부착.

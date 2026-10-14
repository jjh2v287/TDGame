# NOW — 현재 상태 한 장

갱신: 2026-10-15 00:15 claude

## ① 진행 중 (대장 doing)

- 월드: P3-18 doing(다른 claude 세션, 19:05 시작) — 엘든링식 동적 승강기·통로 속도 조절. 에디터(pid 39400) 사용 중으로 보임.
- 몬스터 AI: M3-25 done(미커밋) — 교전 계층을 Home/Engaged/Returning 3상태로(집 기준 영역·귀환 커밋), 헤드리스 88/88. 근거 `research/project-engagement-leash-yoyo.md`.
- 월드: P3-17 done(미커밋) — 탑다운 재질 컷아웃(`/Game/World/Materials/TopDown/`, C++ `UTDTopDownOcclusionSubsystem`, 끄기 `td.TopDown.OcclusionCutout 0`) + 결정론 배치 규칙(`topdown_rules.py`) + 메인 부분·이계 재베이크. 통로 암전 해소, 걷는 칸 가림 11→1%. 증거 `Docs/Validation/P3-17-topdown.md`.
- 월드·이계: P3-13·P3-15 done(미커밋). 외곽 입구 3곳 통로를 걸어서 이계로(D-42).
- 월드·던전 남은 doing(담당 없음): P2-07·09 / P3-02·05·07·08·09·11, todo: P1-11·12, P2-11·13·14, P3-12·14·16.
- 애니메이션: H-01 blocked(Cascadeur Free FBX 잠금, L-anim-08), H-02 todo(노트 `Saved/AgentOps/20261011/re-*.md`).

## ② 막힘·결정 대기

- 몬스터 AI: M3-02 blocked — 원형 회피(격자·속도 인지·양보·플레이어 장애물, CVar `TD.MonsterAI.LocalAvoidance` 0이면 기존) 구현, 게임 타깃 컴파일만 통과(미커밋). 에디터가 비면 에디터 빌드 → `TDGame.MonsterAI`·`TDGame.Movement` 헤드리스(새 15개 포함) → 장면 기준값 보정·300마리 실측. ring_slot은 03 미결 2 대기. MD-45는 (a) 현행 유지로 결정(2026-10-15).
- 몬스터 AI 결정 대기: 길쭉한 몬스터 형상 회피 분석 완료(`Docs/MonsterAI_CombatSim/research/project-elongated-monster-avoidance.md`). MD-25~39(6절)·M3-18~24(5절)는 07에 미등록 — 승인 뒤 부모 세션이 등록, M3-18부터. 문헌 근거 `research/web-noncircular-agent-avoidance.md`.
- 몬스터 AI 결정 대기(M3-25 후속): MD-40 교전 상수 JSON화, MD-41 귀환 중 피격·회복, MD-42 보스 leash, MD-43 Flee 끝 조건, MD-44 몬스터<플레이어 속도 의도(07 §4).
- 사용자 확인(P3-17): 통로·가장자리 전후 `Docs/Validation/P3-17-topdown/*.jpg`, 고원 첨탑 62→6m·가장자리 절벽 151개 제거 괜찮은지 남은 한계: 급경사 지형 텍스처 늘어남·이계 상자 지형 끝선.
- 의도치 않은 재저장: `DA_TDTheme_Crypt.uasset`(editor_make_definitions 부수 효과, 다시 발생) — 사용자 판단.
- 커밋 범위가 크다: 외부 액터 약 7,000개(재베이크·재질 적용)·Content/World/{Realms,Materials/TopDown,PCG}·DA_TDRealm_* — LFS 확인 후 사용자 지시로 커밋.
- 통합 빌드 이름 충돌 패치(`TDDungeonLayoutSolver.cpp`, L-build-03)가 작업 트리에 있어야 빌드된다. 여러 세션이 빌드·헤드리스를 돌릴 땐 DLL 잠금 조정(L-build-05).
- 애니 경로 결정(조사 문서 7.2절), 의도 불명 .uasset(BookHeadMonster_Skeleton)·Paragon·Mocap 미커밋, 무기 부착 미구현(L-anim-05). UAF 첫 주입 ensure 정지는 PIE에서 3.9초(anim.md 59행).
- 에디터 안 `TDGame.Combat` 전체 실행 크래시 → 헤드리스(L-editor-08).

## ③ 다음 행동

1. 사용자 검토 뒤 커밋(이계·통로·탑다운 묶음).
2. P3-16 통로 조명 분위기 마무리(가림은 P3-17이 해결), 랜드스케이프 급경사 삼면 투영 검토, P3-14 이계 인카운터, P1-11 로딩 범위 불일치(실제 76800).
3. 애니: 블라인드 비교 → 해부학 게이트 대장 신설, H-01 라이선스 결정 뒤 종결.

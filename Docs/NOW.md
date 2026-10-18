# NOW — 현재 상태 한 장

갱신: 2026-10-18 00:48 claude

## ① 진행 중 (대장 doing)

- D-47 비동기 에셋 로드 done(미커밋): 동기 로드 32줄 전환·OP-33·검사 도구 0·새 실패 0. 근거 Worklog 10-17 14:05. 후속: 마네킹 애니 쿠킹(ini 2줄)·쿠킹 보장 검사 도구(Worklog 10-17 23:52, 10-18 00:45).
- P3-19·P3-20 done: 연속 세계·태양 역광(D-46). `Docs/Validation/P3-19-continuous.md`·`P3-20-sun.md`.
- M3-25 done: 집 기준 교전·귀환, 헤드리스88/88(`research/project-engagement-leash-yoyo.md`).
- M3-26 done(미커밋): 하이에나 이동 애니 루프 자산 3개·전 종 대기 bLoop(L-anim-15). 헤드리스127/126(기준선 CrossingStreams), PIE 육안 대기.
- P3-17 done: 탑다운 컷아웃, 가림11→1%. 끄기 `td.TopDown.OcclusionCutout 0`.
- 월드 doing(담당 없음): P2-07·09/P3-02·05·07·08·09·11. todo: P1-11·12/P2-11·13·14/P3-12·14·16.
- 애니 H-01 blocked(Cascadeur Free FBX 잠금,L-anim-08), H-02 todo(`Saved/AgentOps/20261011/re-*.md`).
- Fab 완료: 보유827개→후보13·우선5종(`Docs/Validation/FabAssetSelection_2026-10-15.md`). 가져오기·UE 검증 미실행.

## ② 막힘·결정 대기

- D-47 후속(사용자 결정): 피해 정의 표현 소프트화·남서 관문 GUI 지연(L-editor-21). OP-33에 check_cook_coverage 반영(10-18 사용자 승인).
- 어두움(P3-16): 수동 노출이라 B01 남서 경계 0.82·E3 절벽 앞 0.51·F12 승강기 상단 0.26·갱도 바닥 0.99. 국소 조명·하늘빛 최저값 결정. 나무 다리 1.65m 단차(기존, 작업 제안).
- M3-02 blocked: 격자·속도 인지 회피·양보·플레이어 장애물 구현(커밋됨). 헤드리스 MonsterAI 29/30(CrossingStreams 실패, 10-17 재확인). `TD.MonsterAI.LocalAvoidance` 0=기존. 장면 보정·300마리 실측 대기. ring_slot=03 미결2. MD-45=(a)120 유지(10-15).
- 길쭉한 형상 회피: `research/project-elongated-monster-avoidance.md` MD-25~39(§6)·M3-18~24(§5)는07 미등록. 승인 뒤 부모 등록(M3-18부터). 문헌 `research/web-noncircular-agent-avoidance.md`.
- M3-25 후속(07§4): MD-40 상수JSON/41 귀환 피격·회복/42 보스leash/43 Flee 종료/44 몬스터<플레이어 속도 의도.
- 공개 저장소에 Fab·Paragon·Manny 원본 에셋이 올라간다: 비공개 전환 여부 사용자 결정.
- 마지막 커밋의 DA_TDTheme_Crypt 재저장·BookHeadMonster_Skeleton·Paragon·Mocap·.casc: 유지/되돌림 사용자 판단.
- 빌드: `TDDungeonLayoutSolver.cpp` 충돌 패치 유지(L-build-03), 동시 빌드·헤드리스 DLL 잠금(L-build-05).
- 애니 경로 결정(조사§7.2), 무기 부착 미구현(L-anim-05). UAF 첫ensure PIE3.9초(anim.md:59).
- `TDGame.Combat` 전체 에디터 실행 크래시→헤드리스(L-editor-08).

## ③ 다음 행동

1. P3-16 통로·갱도 조명(태양 역광 이후 어두움 수치 기준).
2. P3-14 인카운터·Fab 메시 소량 검증, 급경사 삼면 투영, P1-11 로딩 범위(실제76800).
3. 애니 블라인드 비교→해부학 게이트 대장, H-01 라이선스 결정 뒤 종결.

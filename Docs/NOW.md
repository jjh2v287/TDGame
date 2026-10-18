# NOW — 현재 상태 한 장

갱신: 2026-10-18 16:36 claude

## ① 진행 중 (대장 doing)

- M3-14 PIE 검증 done(미커밋): 포위·예산·디렉터 확인, 300마리 81µs/스텝, 막힌 예약 수정.
- M3-30·31·32 done(미커밋): 경보 2단 파문·호출자, 무리 사기·공황 후퇴(+MD-43 Flee 끝), Charger·Support 독점·원거리 띠. MonsterAI 72/73(기준선), Pack 22/22(`research/project-pack-behaviors.md`).
- M3-29 done(커밋 3db74045): 종 JSON `role`·`boss`·`token_cost`(예약 최대·개시 정산), 체력 2000 보스 규칙·구 `AI/CombatToken` 삭제. TDGame.* 243/248(회귀 0), 반박 합격.
- M3-27 done(커밋 0d302219): 전투 완성(피해 표시·VFX·소리·HUD·4단 콤보·피격·래그돌).
- M3-14·M3-28 done(커밋 e785257d): 공격 토큰·포위 자리·전투 디렉터, 끄기 `TD.MonsterAI.Tactics 0`.
- FluidFX done(미커밋, D-48): 헤드리스 65/65·PIE 통과. 성능: 128 액터 4개 PIE 틱 약 27ms(설계의 약 30배) → 최적화 묶음 대기(`Validation/FluidFX/D-48-perf-2026-10-18.md`).
- D-47 done(커밋 3794ad14)+후속 미커밋(피해 정의 소프트화·쿠킹 보장 도구).
- M3-25·M3-26 done(M3-26 미커밋, PIE 육안 대기). P3-17·19·20 done.
- 월드 doing(담당 없음): P2-07·09/P3-02·05·07·08·09·11. todo: P1-11·12/P2-11·13·14/P3-12·14·16.
- 애니 H-01 blocked(Cascadeur Free, L-anim-08), H-02 todo.
- Fab 후보 5종 선정, 미가져옴.

## ② 막힘·결정 대기

- 07#MD-46(플레이어 피격 반응·넉백·히트스톱·래그돌 잠정값), MD-13·M3-13 미정.
- M3-27 남음: 근접 시점 육안(칼·방패 쥐기·피격 모션 품질), 4단 콤보는 Blender 검토 전 UE 이식함.
- 어두움(P3-16) 국소 조명 결정, 나무 다리 1.65m 단차.
- M3-02 blocked: 장면 보정·300마리 실측. 기준선 CrossingStreams 실패 1.
- 07#MD-47 (a) 유지 권장, ⑥ 막힌 예약 회수 적용, ⑦ 발짝 깊이·Relax 체감 검토.
- 07#MD-48 무리 행동 기본값·elite 데이터·Hyena 돌진자 보류(proposed), MD-43 Flee 끝 적용(proposed). 공황·호출 연출 미연결.
- M3-29 제안: AGENTS.md 14절 폴더 목록 `AI/{CombatToken,NPC}/` → `AI/NPC/`(decisions D-11, 승인 필요). 종별 `token_cost` 값(SC-03 모양) 미지정.
- 길쭉한 형상 회피 MD-25~39·M3-18~24 07 미등록(승인 대기). M3-25 후속 MD-40~44.
- 공개 저장소의 Fab·Paragon·Manny 원본 에셋: 비공개 전환 결정. 마지막 커밋 재저장 에셋 유지/되돌림 판단.
- 빌드 순서: `build_and_test.py --acquire-hold/--release-hold <이름>`·`--holder`(L-build-05·08).
- `TDGame.Combat` 에디터 실행 크래시→헤드리스(L-editor-08).

## ③ 다음 행동

1. M3-27 PIE 근접 육안·사용자 손맛 확인 → MD-46 결정.
2. MD-47·MD-48 결정, M3-29~32·M3-14 수정 커밋 지시 대기.
3. P3-16 통로·갱도 조명. 이어 P3-14 인카운터·P1-11 로딩 범위.

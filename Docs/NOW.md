# NOW — 현재 상태 한 장

갱신: 2026-10-18 19:16 claude

## ① 진행 중 (대장 doing)

- M3-33 done(로컬 커밋): 종 14개·종 표 `species_table.json`·추종 메시. M3-36 doing.
- M3-35 done(로컬 커밋): 공용 보스 기반(비행·소환·형상 예고·위험 1·페이즈 알림)·3페이즈 용 `TDSpawnMonster MountainDragon`. Boss 7/7, 합본 177/178, PIE 처치 통과.
- M3-34 done: 중간 보스 CentaurHunter·GriffonAlpha. MidBoss 11/11, 회귀 177/178, PIE 통과.
- 커밋 끝(07): M3-14 PIE(26eda9e8)·M3-30~32(0c716eac)·M3-29(3db74045)·M3-27(0d302219)·M3-14·28(e785257d). 전술 끄기 `TD.MonsterAI.Tactics 0`.
- FluidFX done(미커밋, D-48): 68/68·PIE 통과, 128×4 틱 27→1.3ms(`D-48-perf-2026-10-18.md`).
- D-47 done(커밋 3794ad14), 후속(c) 피해 정의 소프트화는 M3-35 앞 커밋으로 정리.
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
- 07#MD-50 몬스터화 종 구성·수치(proposed). Kwang 래그돌 없음·검사 무기 미부착.
- 07#MD-57 용 보스 수치·업화 안전 지대 1곳·생략 기술, MD-58 보스 기반 규칙, MD-53·54 중간 보스 수치·구현(proposed).
- 07#MD-48 무리 행동 기본값·elite 데이터·Hyena 돌진자 보류(proposed), MD-43 Flee 끝 적용(proposed). 공황·호출 연출 미연결.
- M3-29 제안: AGENTS.md 14절 폴더 목록 `AI/{CombatToken,NPC}/` → `AI/NPC/`(decisions D-11, 승인 필요). 종별 `token_cost` 값(SC-03 모양) 미지정.
- 길쭉한 형상 회피 MD-25~39·M3-18~24 07 미등록(승인 대기). M3-25 후속 MD-40~44.
- 공개 저장소의 Fab·Paragon·Manny 원본 에셋: 비공개 전환 결정. 마지막 커밋 재저장 에셋 유지/되돌림 판단.
- 빌드 순서: `build_and_test.py --acquire-hold/--release-hold <이름>`·`--holder`(L-build-05·08).
- `TDGame.Combat` 에디터 실행 크래시→헤드리스(L-editor-08).

## ③ 다음 행동

1. M3-27 PIE 근접 육안·사용자 손맛 확인 → MD-46 결정.
2. MD-47·MD-48 결정. M3-29(3db74045)·M3-30~32(0c716eac)·M3-14(26eda9e8) 로컬 커밋, 푸시 지시 대기.
3. P3-16 통로·갱도 조명. 이어 P3-14 인카운터·P1-11 로딩 범위.

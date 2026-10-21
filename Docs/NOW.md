# NOW — 현재 상태 한 장

갱신: 2026-10-21 20:25 claude

## ① 진행 중 (대장 doing)

- M3-33·35·36 done(로컬 커밋): 종 17·`LV_TDBossArena` 한 사이클·공용 보스 기반·3페이즈 용(`TDSpawnMonster MountainDragon`).
- M3-37 done(로컬 커밋): 타격감 7종, PIE 3종 통과. 끄기 `TD.Feedback.ShakeScale 0`.
- M3-38~40 done(로컬 커밋): 래그돌 정착·물리 에셋 7종·사망 즉시 래그돌(L-anim-23). 끄기 `TD.Death.ImmediateRagdoll 0`. 후속 M3-41 todo.
- M3-34 done(6d80f296·5dfac56b): 체력 3000/2400·끊기 1.5초·그리폰 착지 수정.
- 커밋 끝: M3-14·27~32, D-47(후속 c). 전술 끄기 `TD.MonsterAI.Tactics 0`.
- FluidFX doing(D-52): 12차 미커밋(WC14, N0 통과). 다음 꼬리 머리 분리.
- M3-25·26 done(26 미커밋, PIE 육안 대기). P3-17·19·20 done.
- 월드 doing(담당 없음): P2-07·09/P3-02·05·07·08·09·11. todo: P1-11·12/P2-11·13·14/P3-12·14·16.
- 애니 H-01 blocked, H-02 todo. Fab 후보 5종 미가져옴.
- 게임플레이 분석 done(미커밋): `Docs/Gameplay_Fun_And_Systems_Analysis.md`.

## ② 막힘·결정 대기

- 07#MD-59 타격감 기본값(MD-46·MD-13 대체, proposed). 흔들림+모션 블러 번짐 검토. M3-13 미정.
- 07#MD-62 사망 즉시 래그돌 기본값(proposed): 그리폰 알파·용 지상 클립, 바게스트 계열 클립 뒤 래그돌, 동시 시체 +2.3ms 수용.
- M3-27 남음: 근접 육안(칼·방패 쥐기·피격 모션), 4단 콤보는 Blender 검토 전 이식.
- P3-16 국소 조명 결정, 나무 다리 1.65m 단차.
- M3-02 blocked: 장면 보정·300마리 실측. 기준선 CrossingStreams 1.
- 07#MD-47 (a) 권장, 발짝·Relax 체감 검토.
- 07#MD-49 아레나 기본값, MD-50 몬스터화 종 구성·수치, MD-60·61 래그돌 정착(proposed, 3초 대 멈춤 속도 맞바꿈). Kwang 물리 에셋만·검사 무기 미부착.
- 07#MD-57 용 보스 수치·업화 안전 지대 1곳·생략 기술, MD-58 보스 기반 규칙, MD-53·54 중간 보스 수치·구현(proposed).
- 07#MD-48 무리 행동 기본값·elite 데이터·Hyena 돌진자 보류, MD-43 Flee 끝 적용(둘 다 proposed). 공황·호출 연출 미연결.
- M3-29 제안: AGENTS.md 14절 `AI/{CombatToken,NPC}/`→`AI/NPC/`(승인 필요). 종별 token_cost 미지정.
- 길쭉한 형상 회피 MD-25~39·M3-18~24 미등록(승인 대기). M3-25 후속 MD-40~44.
- 공개 저장소 Fab·Paragon·Manny 원본 비공개 전환·재저장 에셋 유지 판단.
- 분석 8절 결정 GQ-01~25, 먼저 GQ-01·10·11·12·13·20.
- 기록 누락 의심: M1-01~05(코드 있음, 대장 todo).
- 빌드 순서 `--acquire-hold`·`--holder`(L-build-05·08). `TDGame.Combat` GUI 크래시→헤드리스(L-editor-08).

## ③ 다음 행동

1. LV_TDBossArena 손 조작 완주·타격감·사망 래그돌 체감 → MD-59·MD-62·MD-49 결정.
2. MD-47·MD-48 결정, 로컬 커밋 푸시 지시 대기. P3-16 조명 → P3-14·P1-11.
3. GQ 결정 → GP-01 설계 문서 → GP-02·03·11.

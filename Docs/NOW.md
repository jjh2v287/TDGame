# NOW — 현재 상태 한 장

갱신: 2026-09-30 02:25 claude

## ① 진행 중 (대장 doing)

- 월드·던전: P2-02·04·05·06·07·09 / P3-00·02·03·04·05·07·08·09·11. 담당 없음, 마지막 기록 2026-09-12 claude.
- 몬스터 AI: doing 없음. 수직 슬라이스(`Source/TDGame/MonsterAI/`, JSON 7종, LV-Cambat 25마리)는 새 몸(Mover·UAF)으로 PIE 재통과.
- 이동·애니: M3-16 done(D44) — 모든 캐릭터 `APawn`+Mover+UAF, AnimBP·몽타주·AnimNotify·StateTree 미사용, 행동은 시퀀스 주입 + C++ 시간표 `FTDActionAnimation`. 브랜치 `feat/mover-uaf`에 커밋 후 main에 병합(2026-09-30 사용자 지시, 원격 푸시 안 함).
- 공통: 관리 체계 유지 중.
- 최근 완료: claude 09-30 M3-16(헤드리스 66/66, PIE 플레이어·몬스터 통과, 쿠킹 25마리 게임 스레드 7.23→4.12ms) / codex MegaMagic 10종(자동화 37/37·PIE 10/10) / claude 09-25 오픈월드 심리스 회귀 수정(L-editor-09).

## ② 막힘·결정 대기

- 의도 불명 .uasset 2건(BookHeadMonster_Skeleton, ExternalActors 6R2JE7…)·Paragon 복사본·Mocap은 계속 미커밋(사용자 판단 대기).
- 새 기본 공격 시각 확인(사용자): `Docs/Validation/BlenderAnimation/sword-attack01-three-views.gif` 외.
- 무기 부착 미구현(C++ 필요). 부착값 HandGrip_R 기준 SM_Sword (−5.052, 9.119, 25.857) cm, roll −83.734·pitch −15.888·yaw 94.203. 플레이어 근접 행동 정의(시간표)가 콘텐츠에 없어 몬스터는 주문(1~0키)으로만 잡힌다.
- 08 전투 재미 설계 사용자 검토(D39~D43 후보).
- 에디터 안 자동화로 `TDGame.Combat` 전체 실행 시 크래시 → 헤드리스(L-editor-08). PIE Navigation 표시 2fps(L-editor-06). LV-Cambat에는 내비메시 경계 없음(L-editor-13).

## ③ 다음 행동

1. 플레이어 근접 행동 정의: 무기 부착 C++ + `FTDActionAnimation`(AS_TD_Player_SwordAttack01, 타격 f23–28, 입력 버퍼 f12–34, 2타 분기 f35–48) — 플레이어 근접이 몬스터를 때리게 된다.
2. 이동·애니 후속: 화면 밖 UAF LOD(M3-05 재정의), Mover 서브스텝(프레임 끊김 시 점프 높이 변동, L-combat-03), 발 IK(UAFControlRig), 구르기 애니 지정.
3. 몬스터: 시뮬 몸(M1-06), 골렘 내려찍기 동작·고블린 무기 부착.

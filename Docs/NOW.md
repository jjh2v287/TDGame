# NOW — 현재 상태 한 장

갱신: 2026-09-24 12:25 codex

## ① 진행 중 (대장 doing)

- 월드·던전: P2-02·04·05·06·07·09 / P3-00·02·03·04·05·07·08·09·11. 담당 없음, 마지막 기록 2026-09-12 claude.
- 몬스터 AI: doing 없음, Phase 0 미착수. 전투 재미 설계 제안 `Docs/MonsterAI_CombatSim/08-combat-fun-design.md` 작성 완료(proposed, 승인 전 효력 없음).
- 애니메이션: Attack02(LToR) 블렌더 확장 툴(AnimAide 이징·해부학적 관절 가동 범위·팔꿈치 힌트 벡터) 교정, FBX/Montage 임포트 및 UE5 본 오차/접지 기술 검증 PASS. 시각 포즈표 산출 완료.
- 공통: 관리 체계 유지 중.
- 이번 완료: MegaMagic 주문 10종(신규 4종), DataAsset 22개·시험장·C++ 수명/지면 처리. 빌드·자동화 37/37·PIE 10/10 PASS. `Docs/Validation/combat/megamagic-validation-2026-09-24.md`. 에디터는 `LV_TDMegaMagicArena` 로드, Play 후 1~9·0으로 시전.

## ② 막힘·결정 대기

- Attack02(LToR) 시각 최종 확인: `Docs/Validation/BlenderAnimation/sword-slash-ltor-three-views.gif`·slow·poses.
- BP_TDCombatCharacter 무기 부착 미구현: HandGrip_R + SM_Sword 상대 위치 (0,32.2,-1.4)cm, C++ 구현 필요.
- 08 전투 재미 설계 사용자 검토: 착수를 막는 결정은 MD-11(미커밋 데미지·캐릭터·컨트롤러 변경 정리 시점)·MD-14(V0 밀기 회색 상자 1~1.5주 착수) 둘뿐. 결정 후보 D39~D43.
- PIE: Navigation 표시(P키)를 켜면 약 2fps 재발 가능(L-editor-06).

## ③ 다음 행동

1. Attack02(LToR) 시각 산출물 사용자 검토 피드백 확인.
2. 콤보 공격 연계(Attack01 -> Attack02 몽타주 노티파이 연결).
3. 캐릭터 무기 컴포넌트 장착 C++ 로직 연계.

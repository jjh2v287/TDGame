[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 가이드 · 작성: 2026-09-24 codex

# Lessons — combat

### L-combat-01 MegaMagic 폭발은 반복형이며 구형 효과의 기본 반경은 150cm다
- 증상: `N_FlameBlast`는 폭발 이름과 달리 `INFINITE`, 주기 2초로 설정되어 있다. 구형 방패를 배율 1로 투사체에 붙이면 작은 충돌 반경보다 시각 크기가 커진다.
- 해결: 원본 에셋을 보존하고 데미지 정의에서 배율을 지정한다. `FlameShield`·`AquaShield`·`ArcaneShield`·`PoisonShield`의 기본 메시 반경 50cm × 파티클 배율 3 = 150cm를 기준으로 삼는다. 피해 종료 시 Niagara를 비활성화하고 제한된 잔상 시간 후 액터를 제거한다.
- 범위: 현재 프로젝트의 MegaMagicVFXBundle 원본 4종 구형 방패 및 FlameBlast. 다른 번들·버전에 일반화하지 않는다.
- 증거: MCP Niagara 스택·메시 바운드 읽기와 `Docs/Validation/combat/megamagic-validation-2026-09-24.md`의 PIE 10종 정리·시각 검증.
- 날짜·상태: 2026-09-24 active
- 발견: codex

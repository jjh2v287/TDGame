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

### L-combat-02 GameInstance 없는 테스트 월드에서 `TryApplyDamage`·사망 처리가 에디터를 죽인다
- 증상: 자동화 테스트에서 `UTDCombatLibrary::TryApplyDamage` 호출 시 `Assertion failed: Router [GameplayMessageSubsystem.cpp:47]`, 호출 스택 `UGameplayMessageSubsystem::Get ← TryApplyDamage (TDCombatLibrary.cpp:47)`.
- 원인: `UWorld::CreateWorld`로 만든 픽스처 월드에는 GameInstance가 없어 메시지 서브시스템이 없다. `TryApplyDamage`는 `Event.Damage.Applied`를, 캐릭터 사망 처리(`HandleDeath`)는 `Event.Actor.Death`를 방송하므로 둘 다 단언에 걸린다.
- 해결: 픽스처 테스트에서는 `UTDCombatComponent::ReceiveDamage(Amount, Element, false, Context)`로 피해를 주고, 캐릭터가 죽는 시나리오는 GameInstance가 있는 월드(PIE 검증)로 옮긴다.
- 범위: `Source/TDGame/**/Tests`의 스코프 월드 픽스처(FTDScopedCombatWorld, FTDMonsterAITestWorld)
- 증거: 2026-09-24 `TDGame.MonsterAI.HeavyHitInterruptsTelegraph` 초판에서 재현 → `ReceiveDamage`로 바꾼 뒤 통과
- 날짜·상태: 2026-09-24 active
- 발견: claude

### L-combat-03 Mover 5.8.2 폰에서 CMC 습관대로 짜면 조용히 틀리는 곳
- 증상: ① `FindComponentByClass<UMovementComponent>`·GAS `ActorInfo->MovementComponent`가 null(UMoverComponent는 UActorComponent 파생)이라 빙결 루프가 이동을 못 멈춤 ② `SetActorRotation`·`SetRelativeTransform(메시)`이 다음 프레임 되돌려짐, 래그돌 몬스터에서 "완전 시뮬레이션 스켈레탈 메시를 이동하려 함" 경고 1,253건 ③ 구르기에서 `FaceRotationImmediately`(FTeleportEffect) 뒤 캡슐 축소 `AddActorWorldOffset(-54)`를 하면 텔레포트가 옛 위치로 되돌려 54cm 떠서 Falling ④ 프레임이 0.4초 끊기면 점프 최고점이 124cm → 48cm(Standalone 백엔드는 가변 스텝 한 번, CMC와 달리 서브스텝 없음) ⑤ 블루프린트 CDO에 `CommonLegacyMovementSettings`가 없다는 컴파일 오류(L-editor-13).
- 해결: ① 빙결은 `ATDCombatCharacter::SetMovementFrozen`(입력 0 + 0속도 효과 + UAF 비활성) ② 회전은 OrientationIntent 또는 FTeleportEffect, 메시 상대 트랜스폼을 바꾸면 `SetBaseVisualComponentTransform`도 갱신, 래그돌 직전에 `SetPrimaryVisualComponent(nullptr)` ③ 캡슐 모디파이어를 먼저 켜고 회전 ④ 미해결 위험 — 서브스텝 모드나 최대 스텝 제한이 필요(후속) ⑤ 저장 → 컴파일 → 저장. 외부 이동(TeleportTo·AddActorWorldOffset)은 `bAcceptExternalMovement=1`로 수용. 속도 설정 `UCommonLegacyMovementSettings`는 컴포넌트 인스턴스별이라 액터별 변경 안전. `APawn::AddMovementInput`은 매번 없는 UPawnMovementComponent를 찾으므로 `Internal_AddMovementInput`/`Internal_ConsumeMovementInputVector`를 쓴다.
- 범위: `Source/TDGame/Characters/TDCombatCharacter.*`·`TDMonsterCharacter.cpp`·`TDGameCharacter.cpp`, UE 5.8.2 Mover(Experimental, Standalone 백엔드)
- 증거: `Saved/AgentOps/20260930/impl-B.md`·`impl-E.md`, 반박 검토 결과(워크플로 m316-adversarial-review), `Docs/Validation/Movement/player-pie-check.json`
- 날짜·상태: 2026-09-30 active(④ 미해결)
- 발견: claude

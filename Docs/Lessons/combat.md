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
- 갱신 2026-10-18 claude: 캐릭터 사망 경로는 이제 픽스처에서도 안전하다 — `ATDMonsterCharacter::BroadcastDeathMessage`가 `UGameplayMessageSubsystem::HasInstance`로 가드되고 `StartRagdoll`은 PhysicsAsset이 없으면 돌아온다. 치명 `ReceiveDamage`로 몬스터를 죽이는 헤드리스 시험(`TDGame.MonsterAI.Pack.LeaderDeathScattersPack`·`BurstLossesPanicSmallPack`·`OneShotKillAlertsNeighbors`)이 통과한다. `TryApplyDamage`(메시지 방송) 금지는 그대로.

### L-combat-03 Mover 5.8.2 폰에서 CMC 습관대로 짜면 조용히 틀리는 곳
- 증상: ① `FindComponentByClass<UMovementComponent>`·GAS `ActorInfo->MovementComponent`가 null(UMoverComponent는 UActorComponent 파생)이라 빙결 루프가 이동을 못 멈춤 ② `SetActorRotation`·`SetRelativeTransform(메시)`이 다음 프레임 되돌려짐, 래그돌 몬스터에서 "완전 시뮬레이션 스켈레탈 메시를 이동하려 함" 경고 1,253건 ③ 구르기에서 `FaceRotationImmediately`(FTeleportEffect) 뒤 캡슐 축소 `AddActorWorldOffset(-54)`를 하면 텔레포트가 옛 위치로 되돌려 54cm 떠서 Falling ④ 프레임이 0.4초 끊기면 점프 최고점이 124cm → 48cm(Standalone 백엔드는 가변 스텝 한 번, CMC와 달리 서브스텝 없음) ⑤ 블루프린트 CDO에 `CommonLegacyMovementSettings`가 없다는 컴파일 오류(L-editor-13).
- 해결: ① 빙결은 `ATDCombatCharacter::SetMovementFrozen`(입력 0 + 0속도 효과 + UAF 비활성) ② 회전은 OrientationIntent 또는 FTeleportEffect, 메시 상대 트랜스폼을 바꾸면 `SetBaseVisualComponentTransform`도 갱신, 래그돌 직전에 `SetPrimaryVisualComponent(nullptr)` ③ 캡슐 모디파이어를 먼저 켜고 회전 ④ 미해결 위험 — 서브스텝 모드나 최대 스텝 제한이 필요(후속) ⑤ 저장 → 컴파일 → 저장. 외부 이동(TeleportTo·AddActorWorldOffset)은 `bAcceptExternalMovement=1`로 수용. 속도 설정 `UCommonLegacyMovementSettings`는 컴포넌트 인스턴스별이라 액터별 변경 안전. `APawn::AddMovementInput`은 매번 없는 UPawnMovementComponent를 찾으므로 `Internal_AddMovementInput`/`Internal_ConsumeMovementInputVector`를 쓴다.
- 범위: `Source/TDGame/Characters/TDCombatCharacter.*`·`TDMonsterCharacter.cpp`·`TDGameCharacter.cpp`, UE 5.8.2 Mover(Experimental, Standalone 백엔드)
- 증거: `Saved/AgentOps/20260930/impl-B.md`·`impl-E.md`, 반박 검토 결과(워크플로 m316-adversarial-review), `Docs/Validation/Movement/player-pie-check.json`
- 날짜·상태: 2026-09-30 active(④ 미해결)
- 발견: claude

### L-combat-04 속도 장애물(VO) 원뿔 포함 검사를 double 프로토에서 float로 옮기면 경계 후보가 안쪽으로 판정된다
- 증상: 파이썬(double) 프로토의 원뿔 포함 검사 여유값 1e-6을 float C++에 그대로 쓰자, 원뿔 경계 위 후보 속도가 반올림 오차(속도 500cm/s × 약 6e-8 ≈ 3e-5 > 1e-6)로 "안쪽"이 되어 HZD식 VO 결정이 파이썬과 41/56(73%)만 일치했다.
- 해결: 여유값을 속도 단위 1e-3cm/s로 키우자 56/56 일치했다. 게임 C++ 이식 때 같은 값으로 시작하고, 파이썬 기준 벡터 대조 시험으로 확인한다.
- 범위: 길쭉한 몬스터 회피(직사각형 VO) float 구현. 다른 기하 판정에는 일반화하지 않는다.
- 증거: `Docs/MonsterAI_CombatSim/measurements/elongated-avoidance/bench-results.md` 4절(63줄)·7절 10, `bench-validation-summary.txt`
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-combat-05 Mover 폰을 움직이는 받침(승강기 덱)에 태울 때: 받침 위 SetActorLocation은 되돌려지고, 덱 윗면이 바닥과 같은 높이면 받침이 바닥으로 잡힌다
- 증상: ① 승강기 상행에서 덱만 올라가고 폰은 갱도 바닥(−34m)에 남는다. 덱 윗면과 갱도 바닥 윗면이 같은 z라 바닥 판정이 정적 바닥을 받침으로 골랐다. ② 받침에 붙은 폰을 PIE 도구가 `SetActorLocation`으로 옮기면 다음 프레임에 옛 위치로 돌아간다(받침 기준 동기 상태가 덮어씀). ③ `FTeleportEffect`로 옮기면 받침이 풀려 덱과 함께 움직이지 않는다.
- 해결: 덱 메시를 바닥보다 2.5cm 높인다(`TDRealmLiftDeckTopClearanceCm`). 운행 중 폰이 덱 발자국 안인데 받침이 덱이 아니면 덱 윗면 + 반높이 + 2cm로 올리고 `CommonBlackboard::LastFloorResult`를 무효화해 다음 바닥 판정을 새로 하게 한다(`ATDRealmLiftDeck` 탑승자 맞춤, 로그 "rider ... snapped"). 검사 도구는 상행 하차 때 순간이동 대신 실제 걷기 입력으로 내린다. 덱은 Movable 프리미티브여야 동적 받침이 된다.
- 범위: UE 5.8.2 Mover(Standalone 백엔드), 움직이는 받침 위 폰(`Source/TDGame/World/Streaming/TDRealmLiftDeck.cpp`)
- 증거: `Saved/WorldGen/PieWalk/p3-19_walk2`·`p3-19_walk3`(SE 하행·상행 도착, 착지 오차 −2.21cm, 공중·기반 이탈 0), 헤드리스 `TDGame.RealmLift.*`
- 날짜·상태: 2026-10-15 active
- 발견: claude

### L-combat-06 탑다운 커서 Visibility 추적은 몬스터를 맞히지 못해 적 클릭이 바닥 이동이 된다
- 증상: PIE 시험에서 고블린 화면 위치로 `GetHitResultAtScreenPosition(ECC_Visibility)` 107회 모두 `StaticMeshActor_0`(바닥)을 맞힘. 실제 게임에서 적을 클릭해도 공격 대신 그 지점으로 이동
- 원인: 몬스터 캡슐·메시가 Visibility 채널을 막지 않는다(캡슐 Pawn 프로필, 메시 CharacterMesh 계열)
- 해결: `ATDGamePlayerController::ResolveHostileTargetAtScreenPosition` — ① 커서 위치에서 Pawn 오브젝트 타입 추적(`GetHitResultAtScreenPosition(ObjectTypes)`) ② 없으면 Visibility로 바닥 점을 얻고 반경 `CursorTargetAssistRadiusCm`(120) 안 가장 가까운 살아 있는 적. 시험도 같은 함수를 쓴다
- 범위: 커서로 적을 고르는 모든 입력(클릭 공격·스킬 대상)
- 증거: 2026-10-18 `Saved/AgentOps/automation_tests_20261018-1317.json`(수정 전 실패) → 1328(통과, 1회에 고블린)
- 날짜·상태: 2026-10-18 active
- 발견: claude

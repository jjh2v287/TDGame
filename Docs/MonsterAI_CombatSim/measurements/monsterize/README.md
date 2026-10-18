# M3-33 몬스터화 — 종 목록·사유·측정 (2026-10-18, claude)

근거 조사: `Saved/AgentOps/20261018/monsterize/understand/mesh-inventory.md`. 클립 실측: `Tools/MonsterAI/editor_measure_species_clips.py` 출력(`Saved/MonsterAI/species_measure*.json`, 이 폴더 `clip-measure.json`에 사본). PIE 증거: `pie-2026-10-18/`(종마다 이동·공격·피격·사망 PNG 4장 + `report.json`).

## 1. 종 목록 (정본: `Tools/MonsterAI/species_table.json` + `Content/MonsterAI/Definitions/<정의>.json`)

| 종 에셋 `DA_TDMonster_*` | 정의 | 역할 | 등급 | 몸체 | 이동 클립 | 능력(클립 → 피해 정의) | 피격·사망 | 비고 |
|---|---|---|---|---|---|---|---|---|
| Barghest (신규) | Barghest | Charger | 졸개(무리) | SK_BARGHEST ×0.6 + 갈기·꼬리 추종 | idleBreathe·walk·run (팩 원본) | Bite biteNormal→DA_TDBarghestBite, Pounce jumpBiteNormal→DA_TDBarghestPounce | getHitNormal·deathNormal | Normal 세트 |
| BarghestAlpha (신규) | Barghest_Alpha(extends) | Melee | elite | 같음 ×0.72 | idleAggressive·walk·run | Bite biteAggressive, Pounce jumpBiteAggressive | getHitAggressive·deathAggressive | 우두머리, Flee 제거 |
| Griffon (신규) | Griffon | Melee | elite | SK_Griffon ×0.5 | Idle·Walk·Gallop | Bite→DA_TDGriffonBite, Claws LeftClawsAttack→DA_TDGriffonClaws, Combo 2HitComboAttack→DA_TDGriffonCombo | GetHit1·Death | 기본 종(M3-34 보스판이 extends) |
| Centaur (신규) | Centaur_Archer | Ranged | elite | SK_Centaur ×0.75 + 추종 7(갑옷·어깨·활·칼집·검·갈기·수염) | IdleBow·WalkBow·GallopBow | Shoot ShootArrowToIdleAiming→DA_TDCentaurArrow(투사체), Kick LegAttackBow→DA_TDCentaurKick | GetHitBow·DeathBow | 활 원거리 정예 |
| Greystone (신규) | Greystone | Melee | elite | Paragon Greystone ×1 | Manny 리타깃 Idle·Walk·Run | Strike Attack_A_Fast→DA_TDQuickSlash, Cleave Attack_PrimaryC→DA_TDWideCleave, Slam Ability_R→DA_TDHeavySlam (자체 애니) | Manny 리타깃 HitReact·Death | |
| Kwang (신규) | Kwang | Melee | elite | Paragon Kwang_GDC ×1 | Manny 리타깃 | Strike PrimaryAttack_C_Slow→DA_TDQuickSlash, Slam Ability_R→DA_TDHeavySlam (자체 애니) | Manny 리타깃 | 물리 에셋 없음 → 사망은 클립만 |
| Wukong (신규, 가능 범위) | Wukong | Melee | elite | Paragon Wukong ×1 | Manny 리타깃 | Strike = Greystone Attack_PrimaryA(마네킹 스켈레톤) 리타깃→DA_TDBruteSmash, Spin = Emote_StaffSpin 1.9~3.3초 구간→DA_TDStaffSpin | Manny 리타깃 | 자체 동작은 봉 돌리기 1종뿐 |
| Swordsman (신규, Mixamo 검사) | Swordsman(extends Goblin_Melee) | Melee | 졸개 | SKM_Man ×1 | Manny 리타깃(Mixamo, 루트=Hips) | Slash A_NS_01→DA_TDGoblinSlash, Thrust = Manny SwordAttack01 리타깃→DA_TDQuickSlash | Manny 리타깃(사망은 루트 잠금 예외) | 손 무기 없음(정적 메시 부착 미지원) |
| Goblin (보강) | Goblin_Melee | Melee | 졸개 | 기존 | 기존 | 기존 Slash | + Death(Manny MM_Death_Front_01 리타깃) | |
| SkeletonGuard (보강) | Goblin_Melee | Melee | 졸개 | 기존 | 기존 | **Slash 새로**(Manny SwordAttack01 리타깃, 전에는 AbilityClips 비어 있음) | + Death 리타깃 | |
| StoneGolem (보강) | StoneGolem | Melee | elite | 기존 | 기존 | Smash 기존, **Stomp 분리**(Greystone Attack_RMB 리타깃) | + Death 리타깃 | |
| BookHeadBrute (보강) | BookHead_Brute | Melee | elite | 기존 | 기존 | Smash 기존, **Swipe 분리**(Greystone Attack_PrimaryB, 마네킹 호환 스켈레톤 직접) | + Death 리타깃 | |
| BookHeadCaster (보강) | BookHead_Caster | Ranged | 일반 | 기존 | 기존 | **Bolt 분리**(Greystone Attack_A_Med), Burst 기존 Attack01 | + Death 리타깃 | |
| Hyena (변경 없음) | Hyena | Melee | 졸개 | 기존 | M3-26 루프 클립 | Bite | Hit·Die 구간 | 손대지 않음 |

## 2. 제외·원천 전용

| 대상 | 판정 | 사유 |
|---|---|---|
| Swine (`DarkFantasyTopDown/.../Sk_Swine`) | 제외 | 애니가 대기·먹기 2개뿐(이동·공격·피격·사망 없음). 뼈 구조(26뼈 자체 이름)가 다른 사족과 달라 리타깃 품질이 낮고, 절차적 키 포즈 전투 동작은 금지(애니 저작 원칙) → 별도 저작 전까지 보류 |
| MountainDragon | 이 작업 범위 밖 | M3-35(용 보스 세션) 담당 |
| Rig_Medium·Rig_Large(KayKit), UAL2 | 몸체 아님(애니 원천) | 흰 더미 마네킹. 스켈레톤 14벌 중복, IK 리그 신설 필요 → 이번에는 쓰지 않음(후속: 해골·마법 시전 동작 원천) |
| Manny·Quinn | 몸체 아님 | 플레이어 몸, 리타깃 원천 |
| Skeleton_Guard 분리 파츠·UE4 데모 마네킹·Hyena 중복 임포트 | 제외 | 분리 파츠·중복 |

## 3. 실측 요약 (배율 1 기준, 클립 시험과 같은 방식)

- 4족 팩(Barghest·Griffon·Centaur)의 제자리 이동 클립은 모두 루프 기준 통과: 정지 꼬리 0~1.7%, 이음매 0~0.18%(한도 3%), 이음매 걸음 비 0.5~1.7(한도 3). 대기 클립은 `loop`가 꺼져 있어 종 도구가 켰다(팩 원본 에셋 수정, 이 팩은 미추적).
- 지면 속도(`_RM` 판 루트 이동, cm/s): Barghest walk 163·run 753, Griffon walk 234·gallop 935, Centaur walk 174·trot 276·gallop 1115. Paragon 리타깃(루트 궤적): Greystone 346/692, Kwang·Wukong 348/697. Mixamo는 루트가 Hips라 추정치 290/600.
- 타격 시각(가장 빠른 손·무기·머리 뼈 최고 속도 시각, 초): Barghest 물기 0.17·0.33(Aggressive), 점프 물기 0.27·0.30, Griffon 물기 0.43·발톱 0.47·2연타 둘째 1.3, Centaur 사격 0.27, Greystone A_Fast 0.15·PrimaryC 0.63·Ability_R 0.9, Kwang PrimaryAttack 0.19·Ability_R 0.9, Mixamo A_NS 0.6, 마네킹 Greystone 세트 PrimaryA 0.39·PrimaryB 0.40·A_Med 0.27·RMB 0.74.
- 새 공격 정의의 윈드업은 재생 속도(타격 시각÷윈드업)가 0.5~2배 안에 들도록 정했다(`TDMonsterAttackCatalog.cpp`).
- Mixamo `A_Combo_*`는 루트(Hips)가 235cm 이동해 루트 고정이 엉덩이까지 굳히므로 쓰지 않고, 제자리판 `A_NS_*`를 썼다. `A_NS_01~18`, `A_Combo_1~6`은 각각 같은 동작의 VFX 변형(측정값 동일).

## 4. PIE 결과 (`python Tools/MonsterAI/pie_check_species.py`, LV_TDMegaMagicArena, 2026-10-18 GUI 에디터)

절차: 종마다 `TDSpawnMonster <종> 1 <거리>`(Ranged·Support는 1300, 나머지 1000) → 1.6초 이동 거리 → 피해 엔티티 생성(공격 예고) 관측 → 최대 체력 30% 피해(피격) → 3배 피해(사망) 2.5초 뒤. 스크린샷은 `pie-2026-10-18/<종>_{1_move,2_attack,3_hit,4_death}.png`(절반 크기, 원본 2113×957은 `Saved/AgentOps/20261018/monsterize/pie-full/`), 한눈에 보기 `sheet_actions_{1,2}.png`(종×4단계)·`sheet_deaths.png`(사망 2.5초 뒤)·`sheet_closeups.png`(추종 메시·사망 확대, 래그돌 수정 전 회차).

| 종 | 스폰 거리 | 두뇌 | 추종 메시 | 이동 | 공격 관측 | 피격 뒤 체력/최대 | 사망 | 판정 |
|---|---|---|---|---|---|---|---|---|
| Hyena | 1000 | 예 | 0 | 472cm | 예 | 64/90 | 예 | 통과 |
| BookHeadBrute | 1000 | 예 | 0 | 596cm | 예 | 190/260 | 예 | 통과 |
| BookHeadCaster | 1300 | 예 | 0 | 200cm | 예 | 78/110 | 예 | 통과 |
| SkeletonGuard | 1000 | 예 | 0 | 548cm | 예 | 86/120 | 예 | 통과 |
| Goblin | 1000 | 예 | 0 | 556cm | 예 | 86/120 | 예 | 통과 |
| StoneGolem | 1000 | 예 | 0 | 513cm | 예 | 532/700 | 예 | 통과 |
| Barghest | 1000 | 예 | 2 | 390cm | 예 | 107/150 | 예 | 통과 |
| BarghestAlpha | 1000 | 예 | 2 | 391cm | 예 | 246/340 | 예 | 통과 |
| Griffon | 1000 | 예 | 0 | 438cm | 예 | 665/900 | 예 | 통과 |
| Centaur | 1300 | 예 | 7 | 198cm | 예 | 378/520 | 예 | 통과 |
| Greystone | 1000 | 예 | 0 | 526cm | 예 | 488/650 | 예 | 통과 |
| Kwang | 1000 | 예 | 0 | 514cm | 예 | 413/560 | 예 | 통과 |
| Wukong | 1000 | 예 | 0 | 507cm | 예 | 349/480 | 예 | 통과 |
| Swordsman | 1000 | 예 | 0 | 541cm | 예 | 115/160 | 예 | 통과 |

육안(스크린샷 확대): Centaur 손의 활·어깨 갑옷·칼집·머리 장식, Barghest 갈기·꼬리가 몸 포즈를 따라간다(리더 포즈가 UAF 몸에서 동작). 사망 2.5초 뒤 13종은 바닥에 눕는다. 인간형 Manny 사망 6종은 모두 골반을 약 85cm에 둔 채 끝나는 "래그돌 전 반응" 클립이라 인간형 8종은 `ragdoll_after_death`를 켰다(Greystone 머리 23cm·Swordsman 12cm·SkeletonGuard 7cm, 끄면 100cm 안팎에서 무릎 자세로 멈춤). 후속: 처치 1초 안 래그돌(M3-27 FullLoop 기준)을 지키려고 인간형 8종 Death 클립은 0.5초에서 끊는다(`end` 0.5, FullLoop seconds_to_ragdoll 0.503).

## 5. 남은 제한

- Kwang: 메시에 물리 에셋이 없어(`Kwang_Extents`는 래그돌용 아님) 사망이 무릎 자세로 끝난 뒤 가라앉는다.
- Paragon Greystone·Wukong·Mixamo SKM_Man은 데모 룸용 흰 재질이라 무채색으로 보인다(메시 원본 그대로).
- Swordsman(Mixamo): 루트가 Hips라 리타깃 이동·피격 클립에서 엉덩이 흔들림이 잠긴다(L-anim-19). 손에 검이 없다(정적 메시 소켓 부착 미지원 — SkeletonGuard 무기도 같음).
- 피격 단계는 체력 감소만 자동 판정한다. 경직(Hit 클립 재생)은 육안 확인이며 정량 판정은 없다.
- 타격 시각은 뼈 최고 속도로 추정했다. 판정 시각과 화면 타격의 정밀 정합은 근접 시점 육안 검토가 남았다.
- Wukong 봉 돌리기(Emote 7초의 1.9~3.3초 구간)는 공격용 원본이 아니라 시작·끝 자세 이음이 거칠 수 있다.
- 마켓 팩(QuadrapedCreatures·SlashTrail_SoftTofu·SwordTrailVFX)은 미추적이라, 커밋된 종 에셋은 그 팩이 있는 작업 트리에서만 열린다(공개 저장소 결정 대기, NOW ②).

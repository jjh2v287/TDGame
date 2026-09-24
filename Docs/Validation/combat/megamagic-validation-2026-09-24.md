# - MegaMagic 데미지 오브젝트 검증 (2026-09-24, codex)

교훈: L-repo-02, L-editor-03, L-combat-01. 자동화 성공은 종료 코드 대신 JSON 실패·미실행 수로 판정했다.
명령: `python Tools/ue_editor.py build`; `TDDamageExamples -MegaMagic`; `Automation RunTests TDGame.Combat`; `python Tools/run_in_editor.py Tools/Damage/editor_validate_megamagic_pie.py`.

## 산출물

- `Content/Combat/MegaMagic`: 시작점 10개, 파생 공격 11개, 빙결 상태 1개, 총 DataAsset 22개.
- `Content/Combat/Maps/LV_TDMegaMagicArena.umap`: 기본 전투 캐릭터·컨트롤러, 표적 6개, 바닥·장애물·내비게이션·조명. 무광 재질 2개 포함.
- 기존 6종과 신규 뇌전 결계·맹독 개화·성운 창·불사조 강하를 `1~9, 0`으로 시전한다.
- 원본 MegaMagic 이펙트와 기존 Examples 에셋 보존. 로직은 C++, 에셋은 구성 전용.

## 검증 결과

- Win64 Development Editor 빌드 성공. 최종 빌드 51.21초.
- C++ 전투 자동화 **37/37 성공**, 실패·미실행 0. MegaMagic 테스트 8개 포함.
- 기존 GAS 테스트의 GameplayCueNotifyPaths 미지정 경고 1건. 이번 변경과 무관한 설정 경고다.
- 저장된 그래프·VFX 경로·진영 필터·유효성, 신규 4종 발동 지연·반복 피해·연계·아군 제외, 종료 콜백 중 Hit/상태/예약 액션 취소를 검사했다.
- 최종 PIE **10/10 성공**. `ATDCombatCharacter::CastDamageSpell` → GAS → 데미지 엔티티 경로로 시전했다.
- 모두 적 체력 감소·시전자 체력 유지·Niagara 활성화를 확인했고 관측 종료 후 엔티티는 0개였다.
- 이번 PIE의 Python/Niagara 오류 없음. 종료 후 PIE 중지, 시험장 로드 유지, 미저장 패키지 0개.

피해 합계는 레벨 1 시전자와 시험장 표적 6개의 관측값이다. 산포·적중 대상 수에 따라 변하므로 밸런스 기준값이 아니다.

| 키 | 주문 | 표적 피해 합계 | 동시 활성 Niagara 최대 | 종료 후 엔티티 |
|---|---|---:|---:|---:|
| 1 | Fireball | 96 | 2 | 0 |
| 2 | Blizzard | 210 | 25 | 0 |
| 3 | Mine | 180 | 2 | 0 |
| 4 | Shockwave | 175 | 1 | 0 |
| 5 | Meteor | 295 | 3 | 0 |
| 6 | DelayedHoming | 96 | 2 | 0 |
| 7 | ThunderCage | 366 | 2 | 0 |
| 8 | VenomBloom | 254.39 | 2 | 0 |
| 9 | AstralLances | 105 | 7 | 0 |
| 0 | PhoenixDive | 529 | 5 | 0 |

## 시각 확인과 범위

- 투사체 원본 구형 메시 반경 150cm를 기준으로 충돌 크기에 맞춰 배율을 설정했다. 지상 효과는 수평 유지·지면 투영한다.
- 반복형 폭발도 게임플레이 종료 시 Niagara를 비활성화하고 한정된 잔상 시간 후 제거한다.
- 신규 4종에서 청색 전기, 녹색 독 함정, 보라색 유도 창, 주황색 낙하 폭발을 시각 확인했다. 번들에 얼음 전용 효과가 없어 Blizzard는 물빛 구체와 마법장 표현이다.
- 키 바인딩은 소스 검토로 확인했다. 자동 PIE는 같은 C++ 시전 함수를 직접 호출하며 물리 키 입력은 자동 조작하지 않았다.
- 쿠킹 경로와 하드 참조는 구성했지만 별도 패키지 빌드·멀티플레이·대량 전투 성능 측정은 수행하지 않았다. 기존 시스템은 LocalOnly GAS 시전이다.

원시 요약: [자동화·PIE JSON](megamagic-results-2026-09-24.json).

시각 증거: [뇌전 결계](megamagic-thunder-2026-09-24.png), [맹독 개화](megamagic-venom-2026-09-24.png), [성운 창](megamagic-astral-2026-09-24.png), [불사조 강하](megamagic-phoenix-2026-09-24.png).

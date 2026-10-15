# TDGame Fab 보유 에셋 선정

확인: 2026-10-15 codex. 사용자 요청으로 로그인된 Fab 브라우저 라이브러리를 열어 보유 여부를 확인하고 상품 설명을 대조했다. 유료 구매와 무료 등록을 구분하는 결제 이력은 조회하지 않았다.

## 요약

먼저 `Isometric World : Sky Temple`, `Stylized Modular Crystal Mine`, `Close Combat Animset`, `Slash Trail FX Elemental`, `Dark Knight - Male and Female` 5종을 추천한다. 현재 부족한 이계 메시, 회피·피격 동작, 근접 공격 궤적, 플레이어 외형을 각각 보완한다.

## 검토 범위와 근거

라이브러리 전체 표시는 827개였다. 다음 6개 범주의 목록을 끝까지 읽었다. 436은 범주별 항목의 합이며, 각 상품의 배포 파일을 검사한 수가 아니다.

| 범주 | 목록 항목 수 |
|---|---:|
| 애니메이션 | 26 |
| VFX | 49 |
| 캐릭터 및 크리처 | 91 |
| 환경 | 177 |
| 자연 및 식물 | 77 |
| 무기 및 전투 | 16 |
| 합계 | 436 |

- 엔진: `TDGame.uproject`의 `EngineAssociation`은 `5.8`.
- 애니메이션: `Docs/MonsterAI_CombatSim/00-decision-record.md` D44에 따라 `APawn + Mover + UAF`. 개별 동작을 활용하며 상태 선택은 C++가 담당한다.
- 이계: `Docs/Tasks/phase-3-outdoor-generator.md` P3-14가 수정·유적·구름 등의 전용 메시 확보를 요구한다. `SunkenStarfield`와 `PaleHeights`의 차별화가 우선이다.
- 화면: `Docs/WorldDungeonPCG/05-topdown-visuals.md`와 `Docs/Validation/P3-17-topdown.md`의 실제 탑다운 카메라 및 전후 화면을 확인했다. 높은 메시·천장·강한 발광은 플레이어와 공격 범위를 가릴 수 있다.
- 중복: `Content/MegaMagicVFXBundle`, `Content/DarkFantasyTopDown`이 이미 있다. `Content/Characters/Paragon/Greystone/Attack`의 공격 20종은 Manny 변환 팩과 일부 역할이 겹친다.
- 현재 몬스터: `BookHeadMonster`, `Skeleton_Guard`, `Stone_Golem`, `Fab/Hyena_A1`, `Fab/Stylized_Goblin_Minion_-_Free`를 파일 이름으로 확인했다.

후보 13종은 모두 보유 라이브러리에서 확인했다. 이름·폴더 검색으로 이 후보들의 전체 팩이 현재 프로젝트에 들어온 사실은 확인하지 못했다. 다른 이름으로 가져온 경우까지 배제하지 않는다.

## 먼저 적용할 5종

| 순서 | 보유 에셋 | 상품 설명에서 확인한 구성 | TDGame 추천 용도 | 적용 전 확인 |
|---:|---|---|---|---|
| 1 | [Isometric World : Sky Temple](https://www.fab.com/listings/57bff667-e233-43db-b8e0-d29b9fbdafde) | 탑다운·아이소메트릭 용도 명시. 고유 메시 54개, LOD 4단계, 자동 충돌, Niagara·데칼 포함. UE 5.8 지원 표기 확인. | `PaleHeights`의 부유 유적·신전·랜드마크. 현재 임시 기둥보다 지역 성격을 명확히 만들 수 있다. | 메시 스케일, 가림 처리, 자동 충돌의 보행 가능성. 패키지 지원 표기는 현재 프로젝트에서의 검증을 대신하지 않는다. |
| 2 | [Stylized Modular Crystal Mine](https://www.fab.com/listings/fa9ea4d7-ab8c-498b-aab5-42af7c7cb8f2) | 수정·광석·암석·터널, 조절 가능한 재질, LOD·라이트맵·충돌과 UE5 Lumen 설명. | `SunkenStarfield`의 수정 군락·광맥·동굴 소품. 색과 발광을 지역 팔레트에 맞춰 조정한다. | 큰 수정의 시야 가림, 재질·충돌. 광차 Blueprint 기능은 별도 로직이므로 이번 선정의 가치에 포함하지 않았다. |
| 3 | [Close Combat Animset](https://www.fab.com/listings/0a4a5e43-6941-4a4b-ba7f-2bf7e8015c5c) | 공격 10·회피 11·방어 3·피격 9·이동 13. In-place 46개와 Root Motion 33개. UE 표기는 5.6까지. | 플레이어·인간형 적의 회피, 피격, 방어 동작. 검 콤보 전용 팩이라고 단정하지 않는다. | 실제 클립의 무기 자세·골격 리타깃·루트모션. 이동 동작은 루트모션이 없다고 설명한다. UE5.8에서 가져오기 시험 필요. |
| 4 | [Slash Trail FX Elemental](https://www.fab.com/listings/a4b04697-30c1-4745-ae2f-a706278300bd) | 불·얼음·번개·어둠 등 원소별 검격, 무기 재질·오라·회전 투사체·타격 파티클. | 근접 공격 방향, 원소 부여, 명중 순간을 보여 주는 FX. 기존 마법 폭발·방패 FX와 용도가 다르다. | 탑다운에서 폭·발광·잔상 길이. 상품의 애니메이션 연결 예제를 그대로 채택하지 않고 C++ 전투 시간표에 맞춰 효과를 호출한다. |
| 5 | [Dark Knight - Male and Female](https://www.fab.com/listings/d237534b-9ba7-4c48-8cd9-8a1fd1965119) | 남녀 기사·모듈 갑옷·검. UE5 Quinn/UE4 Mannequin 호환 설명. 애니메이션 52개 중 공격 3·피격 8·사망 1. 캐릭터 각 약 3.45만 tris. | 플레이어 외형, 기사형 정예 적, NPC. 현재 다크판타지 방향과 잘 맞는 후보. | 현재 Manny 계열로 리타깃, 소켓·무기 부착, 갑옷 관통·LOD. 공격 3개만으로 풍부한 콤보 구성을 기대하지 않는다. |

위 용도와 순서는 프로젝트 상황을 바탕으로 한 선정 판단이다. 상품 설명에서 해당 지역이나 TDGame 호환을 보증한 것은 아니다.

## 다음 확장 후보 8종

| 분야 | 보유 에셋 | 추천 이유와 제한 |
|---|---|---|
| 애니메이션 | [Paragon animations retargeted to Manny](https://www.fab.com/listings/e6de87b1-b755-478d-9228-a9eb89ff4411) | 제작사 설명상 Paragon 인간형 동작 약 5,300개를 Manny로 변환한 FBX 묶음. 근접·마법 모션 후보를 캐릭터별로 골라 쓴다. 현재 Greystone 공격 20종과 부분 중복이다. Epic 원본 캐릭터 팩과 제3자 변환본을 구분하고 실제 파일·리타깃 결과를 검사한다. |
| 애니메이션 | [Combat Magic Animations](https://www.fab.com/listings/05d90ca6-d294-4b93-a1b7-232214057850) | 모캡 기반 시전·공격·방어·부유·파이어볼 투척 동작. 기존 마법 FX에 마법사 동작을 붙이기 좋다. 정확한 골격·클립 수·루트모션은 미확인이고 마법 FX 포함은 확인되지 않았다. |
| FX | [Niagara Footstep VFX](https://www.fab.com/listings/49dc53b7-0a5f-4905-8951-e42aaf1135e4) | 흙·풀·자갈·물·눈 등 Niagara 15종. 지형별 보행 피드백에 활용. 먼저 플레이어부터 적용하고 많은 몬스터의 동시 발생 비용을 측정한다. 발 접지 시점과 표면 선택은 프로젝트 로직에 연결해야 한다. |
| FX | [Immortal Loot Drop VFX](https://www.fab.com/listings/daae8e58-94c9-48ec-a5a2-7bf789be80ad) | 색·발광 레이어를 바꿀 수 있는 드랍 Niagara 효과. 탑다운에서 전리품 위치와 등급을 읽기 좋다. 효과 색과 게임의 등급·상호작용 로직은 별개다. |
| 던전 메시 | [Modular Dungeon Collection](https://www.fab.com/listings/3aef98fe-485d-46ae-a5ac-632e4d81358d) | 모듈 67개·소품 50개·샘플 던전. 벽은 조절 가능한 공통 타일링 재질. 현재 던전 규격은 400cm 셀이므로 실제 치수·피벗·문 연결부·천장 분리·충돌을 확인해야 하며 생성기에 바로 맞는다고 보증할 수 없다. |
| 몬스터 | [Quadruped Fantasy Creatures](https://www.fab.com/listings/52d686b6-1180-4f26-901f-ce3c69a14767) | Barghest·Centaur·Griffon·Mountain Dragon 4종. 각각 애니메이션 19·85·38·41개. 기존 하이에나와 다른 실루엣을 확보할 수 있다. 종별 골격·접지·이동 형태가 달라 한 종씩 연결하고 비행은 별도 이동 로직을 확인한다. |
| 보스 | [Dragon for Boss Monster : PBR](https://www.fab.com/listings/6c4bd321-0263-44fe-a3d9-82599eeee12c) | 드래곤 4종, 약 3,864~4,832 tris, 최대 18동작, 2K PBR. 탑다운 보스 외형 후보. 낮은 삼각형 수만으로 성능을 보증하지 않으며 사족 팩의 드래곤과는 먼저 한쪽을 고른다. |
| 무기 메시 | [Free Melee Weapon Pack (PBR and Stylized)](https://www.fab.com/listings/da6e582c-c12b-4768-b67f-8e248339c071) | 검·대검·도끼·단검·창·활, UE5 Mannequin 기준 크기와 손잡이 피벗. 무기 종류별 외형을 확보하기 좋다. 활 리깅·활시위 동작은 포함되지만 캐릭터 공격 모션을 대신하는 팩은 아니다. |

## 보류·기존 활용

- `Mega Magic VFX Bundle`, `Top-down Dark Fantasy environment`: 이미 프로젝트에 있어 신규 확보 우선순위는 낮다. 현재 콘텐츠를 먼저 재활용한다.
- [Mega Spear Animation Pack](https://www.fab.com/listings/a6f8de4a-b0a3-4b9b-8afc-bd6f1bf20bfa): 보유 확인. UE4 골격 기반 최신 설명은 219동작이며 업데이트 중단이 명시돼 있다. 창 전투가 실제 범위에 들어오면 원본 클립만 별도 검토한다.
- [Wood Monster](https://www.fab.com/listings/c123025e-bf13-4f87-8fc9-40cf1eafedab): 보유 확인. 외형은 후보이나 쇼케이스 애니메이션 미포함이 명시돼 있어 완성된 몬스터로 곧바로 쓰기는 어렵다.
- `AdaptivePathfindingSystem`, `VisAI`, `COMBAT EQS AI`, ALS 전체 시스템 등: 현재 C++ 몬스터 AI·Mover·UAF 구조와 도입 비용을 비교해야 한다. 이번 요청에서는 외형·원본 동작·효과를 우선했다.

## 검증 범위

완료: 로그인된 보유 목록 확인, 후보 상품 설명·일부 추가 기술 정보 확인, 프로젝트 문서·실제 Content 파일 이름·현재 화면 대조.

미실행: 다운로드, 구매, 라이선스 수락, 프로젝트 가져오기, 에셋 수정, 리타깃, 컴파일, PIE 성능·시각 검증. Sky Temple의 UE5.8 표기 외에 모든 팩의 최신 버전 지원을 동일하게 검증한 것은 아니다. AnimBP·몽타주·Notify 기반 상품 예제를 프로젝트의 런타임 구조로 도입하라는 추천은 아니다.

다음 적용은 상위 5종 중 한 팩의 필요한 메시·동작·Niagara를 소량 가져와 현재 게임 카메라에서 비교한 뒤 확대하는 순서가 적절하다.

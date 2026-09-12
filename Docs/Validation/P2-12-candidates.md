# P2-12 던전 후보 10개 생성 데모 (2026-09-12)

## 1. Python 생성기 스윕 (`python Tools/DungeonGen/batch_dungeons.py --seeds 1-10 --flow Branch --size Medium`)

| seed | 통과 | 실패 항목 | 룸 | 모듈 | 분기 | 루프 | 막다른길 비율 | 주경로(룸/모듈) | 재시도 | 점수 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | PASS | - | 12 | 22 | 3 | 0 | 0.25 | 6/12 | 0 | 96.0 |
| 2 | PASS | - | 15 | 33 | 3 | 0 | 0.2 | 12/29 | 0 | 98.0 |
| 3 | PASS | - | 14 | 26 | 3 | 0 | 0.2143 | 11/21 | 0 | 98.3 |
| 4 | PASS | - | 14 | 29 | 3 | 0 | 0.2143 | 8/17 | 2 | 101.7 |
| 5 | PASS | - | 14 | 28 | 3 | 0 | 0.2143 | 9/18 | 0 | 106.0 |
| 6 | PASS | - | 12 | 24 | 3 | 0 | 0.25 | 7/14 | 0 | 101.0 |
| 7 | PASS | - | 15 | 29 | 3 | 0 | 0.2 | 9/21 | 0 | 104.0 |
| 8 | PASS | - | 14 | 24 | 3 | 0 | 0.2143 | 10/16 | 0 | 102.6 |
| 9 | PASS | - | 12 | 26 | 3 | 0 | 0.25 | 7/16 | 0 | 101.0 |
| 10 | PASS | - | 14 | 28 | 3 | 0 | 0.2143 | 10/22 | 0 | 102.6 |

## 추천 상위 3
- seed 5: 점수 106.0, 룸 14, 분기 3, 루프 0, 막다른길 0.21, 주경로 9룸 -> Saved/DungeonGen/batch_Branch_Medium/seed_5/
- seed 7: 점수 104.0, 룸 15, 분기 3, 루프 0, 막다른길 0.20, 주경로 9룸 -> Saved/DungeonGen/batch_Branch_Medium/seed_7/
- seed 8: 점수 102.6, 룸 14, 분기 3, 루프 0, 막다른길 0.21, 주경로 10룸 -> Saved/DungeonGen/batch_Branch_Medium/seed_8/

미리보기: `Saved/DungeonGen/batch_Branch_Medium/seed_<N>/preview.png`, 검증 리포트 `report.md`.

## 2. C++ 커맨드릿 스윕 (`-run=TDWorldGen -Flow=KeyLock -Size=Medium -Seeds=1-5`)

결과 표는 `Docs/Validation/commandlet-dungeon-sweep.md`. 추천: 시드 1, 3, 5.

## 3. 수동 선택과 베이크 (설계서 R-43 흐름)

| 슬롯 | DungeonId | 채택 시드 | 근거 |
|---|---|---|---|
| 0 | MainCrypt | 7 | Python 스윕 2위(104.0, 룸 15, 주경로 9룸) |
| 1 | HollowCave | 3 | C++ 스윕 2위(71.21, 룸 26) |
| 2 | SunkenCrypt | 5 | 두 스윕 모두 상위 3(Python 1위 106.0, C++ 3위 68.83) |

베이크: `Tools/DungeonGen/editor_bake_dungeons_cpp.py`(C++ `GenerateAndBakeDungeon`) → 아틀라스 `DA_TDDungeonAtlas_Main` 슬롯 정의 갱신. 에디터 버튼 경로는 `ATDDungeonSlotAnchor`의 Generate Candidates / Bake Selected(P2-08)로 같은 함수를 호출한다.

## 4. 플레이 확인

- MainCrypt: PIE 걸어 들어가기 왕복 3회 통과(`Docs/Validation/P1-pie-checks-2026-09-12.md`), 입구→보스 내비 경로 6170cm/12점(P2-10).
- HollowCave/SunkenCrypt: 규격 검사·내비 경로 검사만 수행, 걸어서 확인은 남음(P2-D3 그리드 단위 결정 후 재베이크 예정이라 보류).

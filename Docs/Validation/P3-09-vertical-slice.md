# P3-09 버티컬 슬라이스 측정 (2026-09-12, 오프라인 추정)

전제: 이동 속도 600cm/s(기본 캐릭터 무브먼트), 도로 우회 계수 1.25, 마을 스타트 (−106m, 26m) 기준 직선거리.

| 목적지 | 직선거리 m | 추정 이동 시간 s |
|---|---:|---:|
| TDArena_ArenaEast | 386 | 80.4 |
| TDArena_ArenaNorth | 257 | 53.6 |
| TDArena_ArenaWest | 194 | 40.4 |
| TDDungeonEntrance_HollowCave | 391 | 81.4 |
| TDDungeonEntrance_MainCrypt | 495 | 103.1 |
| TDDungeonEntrance_SunkenCrypt | 571 | 118.9 |
| TDPoi_AmbushCart | 387 | 80.7 |
| TDPoi_BonePit | 379 | 79.0 |
| TDPoi_FishingDock | 140 | 29.2 |
| TDPoi_Graveyard | 374 | 78.0 |
| TDPoi_HunterCamp | 247 | 51.5 |
| TDPoi_MushroomGrove | 395 | 82.2 |
| TDPoi_Shrine | 227 | 47.4 |
| TDPoi_Watchtower | 273 | 56.8 |
| TDPoi_WayshrineEast | 433 | 90.3 |
| TDPoi_WayshrineNorth | 352 | 73.3 |
| TDPoi_WayshrineSouth | 246 | 51.3 |

- 콘텐츠 밀도: POI·입구·공터 17개 / 1.02 km² = 16.7개/km². 마을에서 가장 먼 콘텐츠까지 약 119초.
- 도로 무콘텐츠 구간 최장 36m(검증기 play_density), 즉 최대 약 8초의 빈 이동.
- 전투 밀도(1분당 조우 수)는 몬스터 스포너가 아직 없어 측정 불가 — 몬스터 AI 대장(M 단계)과 연동 후 PIE 실측으로 갱신.
- 규모 판단(설계서 13장): 1km² 필드에 마을 1, 던전 3, POI 11, 공터 3은 초기 목표(500m², 마을 1, 던전 3, POI 8)를 넘으므로 확장 전에 전투 밀도부터 채운다.

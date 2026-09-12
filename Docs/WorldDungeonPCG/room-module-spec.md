[← 인덱스로](../WorldDungeonPCG_Plan.md)

# 룸 모듈 규격 (P2-01, 2026-09-12 확정안)

던전은 손으로 만든 룸 모듈을 흐름 그래프에 따라 정수 격자 위에 조립한다(설계서 6장). 아트가 모듈을 만들 때와 생성기가 모듈을 읽을 때 공통으로 지키는 규격이다. 코드 정의는 `Source/TDWorldGen/Public/Dungeon/TDDungeonTypes.h`(`FTDRoomModuleDefinition`, `FTDDoorSocket`)와 `TDDungeonDefinitions.h`(`UTDDungeonTheme`)다.

## 1. 격자
- 셀 한 변 **400cm**(결정 D-03 권장안. 캐릭터 캡슐 반지름 34cm, 1칸 통로에서 회피 여유 확보). 테마 에셋 `CellSizeCm`로 바꿀 수 있지만 한 테마 안에서는 하나의 값만 쓴다.
- 축: 모듈 로컬 격자 x → 언리얼 +X, 격자 y → 언리얼 +Y. 문 방향 `North = -Y`, `East = +X`, `South = +Y`, `West = -X`. 회전은 시계 방향 90° 단위 0~3(`RotateCell`: (x, y) → (−y, x)).
- 모듈 바운드는 셀의 정수 배이며, `Cells` 목록에 점유 셀을 모두 적는다(비직사각형 허용). 셀 (0,0)이 모듈 원점이고 레벨 인스턴스 원점은 셀 (0,0)의 **모서리**(왼쪽 위, 최소 X·최소 Y)다. 셀 중심은 원점 + (200, 200)cm.

## 2. 도어 소켓
- 소켓은 바운드 가장자리 셀의 중앙에서 그 방향 벽 면 위에 있다: 위치 = 셀 중심 + 방향 × 200cm, 높이 0(바닥). 폭 등급은 1칸(400cm 벽 가운데 **240cm** 개구부, 높이 300cm)으로 통일한다.
- 태그: `Normal`(기본), `Boss`(보스 방 전용 진입), `Locked`(잠금 가능 문. 잠긴 문 소품은 베이커가 놓는다).
- 두 모듈은 서로 마주 보는 소켓(같은 벽, 반대 방향) 하나로만 연결된다. 소켓이 없는 벽은 막힌 벽이어야 한다. 소켓이 있지만 연결되지 않은 경우 베이커가 "봉인 플러그"(벽 패널)를 놓으므로 개구부 뒤 30cm 안에 다른 지오메트리를 두지 않는다.
- 레벨 인스턴스 안에서는 `UTDRoomDoorSocketComponent`(예정, 지금은 `ATargetPoint` 라벨 `Door_<N|E|S|W>_<x>_<y>`)로 표시한다. 생성기는 레벨을 읽지 않고 테마 에셋의 `Sockets` 배열만 읽으므로 둘을 일치시킨다.

## 3. 카탈로그 (Crypt 테마, `FillCryptPlaceholderModules`와 동일)
| 모듈 | 셀 | 소켓(셀, 방향) | 역할 | 회전 |
|---|---|---|---|---|
| Entrance | (0,0) | (0,0)N | Entrance | 허용 |
| Straight_A | (0,0) | N, S | Corridor | 허용 |
| Straight_B | (0,0),(0,1) | (0,0)N, (0,1)S | Corridor | 허용 |
| Corner_A | (0,0) | N, E | Corridor | 허용 |
| Corner_B | (0,0),(1,0) | (0,0)S, (1,0)N | Corridor | 허용 |
| T_Junction | (0,0) | N, E, W | Combat | 허용 |
| Large_A | 2×2 | (0,0)N, (1,0)E, (1,1)S, (0,1)W | Combat, Hub | 허용 |
| Large_B | 3×2 | (1,0)N, (2,1)E, (1,1)S, (0,0)W | Combat, Hub | 허용 |
| DeadEnd | (0,0) | S | DeadEnd | 허용 |
| Treasure | (0,0) | S | Treasure, Key | 허용 |
| Elite | 2×2 | (0,1)S, (1,0)N, (1,1)E | Elite | 허용 |
| Boss | 3×2 | (1,1)S | Boss | 허용 |

## 4. 탑다운 카메라 제약
- 카메라는 남쪽(+Y)에서 북쪽을 내려다본다(피치 약 −55°). 남쪽 벽(+Y 면)은 높이 **120cm 이하**로 만들거나 카메라 페이드 머티리얼을 쓴다. 북·동·서 벽 높이 상한 350cm.
- 천장은 두지 않는다(조명은 방 안 광원으로).

## 5. 스폰 마커·내비
- 인카운터 마커 `UTDEncounterMarkerComponent`(예정): 역할(Combat/Elite/Boss/Treasure/Key), 반경. 지금은 베이커가 역할별 소품과 조명을 대신 놓는다.
- 각 모듈 레벨에 `NavMeshBoundsVolume`을 모듈 바운드 + 1셀 여유로 둔다. 전투 방 최소 빈 공간: 2×2 셀 모듈은 중앙 400cm 원, 3×2는 600cm 원.

## 6. 파일·이름
- 레벨 인스턴스 소스 레벨: `Content/Dungeon/Rooms/<Theme>/LI_TDRoom_<Theme>_<Module>.umap`(OFPA 사용). 테마 에셋 `DA_TDTheme_<Theme>`의 모듈 항목 `LevelAsset`에 연결한다.
- 플레이스홀더 지오메트리 생성 스크립트: `Tools/DungeonGen/editor_make_room_modules.py`(엔진 Cube로 바닥·벽·소켓 개구부·마커·내비 볼륨 생성).

## 7. 검증
- 테마 에셋 `IsDataValid`: 필수 역할(Entrance/Boss/Corridor/Combat) 존재, 소켓 셀이 모듈 셀 안에 있는지.
- 규격 검사 커맨드(P2-08 예정): 레벨의 문 마커와 테마 소켓 일치, 남쪽 벽 높이, 내비 볼륨 존재.

# DungeonGen — 에디터 밖 결정론 던전 생성기·검증기

언리얼 에디터 없이 Python 3.12 + numpy + Pillow 만으로 룸 모듈 조립 던전 레이아웃을 만들고 검증한다.
설계 근거: `Docs/WorldDungeonPCG/research/dungeon-generation.md`(정수 격자 + 4방향 도어 소켓 + 90도 회전, Key/Lock 구성적 보장, 2단계 검증),
`Docs/WorldDungeonPCG/03-architecture.md` 3.3절(`FTDDungeonFlowGenerator` → `FTDDungeonLayoutSolver` → `FTDDungeonValidator` → `FTDCandidateSelector` 와 같은 단계 분리),
설계서 6장(흐름 템플릿 5종, Theme_Crypt 모듈), 7.1(아틀라스 슬롯), 11.1(검증 항목).

## 사용법

```
python Tools/DungeonGen/generate_dungeon.py --theme Crypt --flow KeyLock --size Medium --seed 7 [--slot 2] [--out Saved/DungeonGen/이름]
python Tools/DungeonGen/validate_dungeon.py Saved/DungeonGen/Crypt_KeyLock_7/layout.json [--max-deadend-ratio 0.4]
python Tools/DungeonGen/batch_dungeons.py --seeds 1-20 --flow Branch --size Medium        # --flow all 은 5종 모두
```

- `generate_dungeon.py`: `layout.json`, `preview.png`, `report.md` 를 출력 폴더(기본 `Saved/DungeonGen/<theme>_<flow>_<seed>`)에 쓴다. 종료 코드 0 = 검증 통과, 1 = 검증 실패, 2 = 배치 실패(재시도 상한 초과).
- `validate_dungeon.py`: 기존 `layout.json` 을 다시 검사해 `validation` 필드를 갱신하고 같은 폴더에 `report.md` 를 쓴다. `validate(layout)` 함수를 직접 불러도 된다.
- `batch_dungeons.py`: 시드 범위(`1-20` 또는 `1,3,7`)를 돌려 `Saved/DungeonGen/batch_<flow>_<size>/seed_<N>/` 에 결과를 쓰고, `Saved/DungeonGen/candidates_<flow>_<size>.md` 에 점수표와 상위 3개 추천을 쓴다.

결정론: 난수는 `numpy.random.default_rng([seed, 0])`(흐름 그래프)과 `default_rng([seed, 1, 재시도번호])`(배치)만 쓴다. 전역 난수·집합 순회 의존 없음. 같은 인자 → 같은 `layout.json`(해시 동일).
`generator_version` 이 바뀌면 같은 시드라도 결과가 달라질 수 있으므로 출시용은 베이크된 레이아웃을 고정한다.

## 규격

| 항목 | 값 |
|---|---|
| 셀 크기 | 400cm (`cell_size_cm`) |
| 축 | 격자 x → 언리얼 X, 격자 y → 언리얼 Y. N = -Y, E = +X, S = +Y, W = -X. 미리보기는 N 이 위 |
| 회전 | 시계 방향 90도 단위 0..3 (N→E→S→W). 회전 뒤 셀 최소 좌표가 (0,0)이 되도록 정규화 |
| 도어 소켓 | (모듈 로컬 셀, 방향). 두 소켓이 연결되려면 셀이 그 방향으로 인접하고 방향이 서로 반대여야 한다 |
| 미사용 소켓 | `connected_room: null` = 봉인(벽 플러그). 우연히 마주 보는 봉인 소켓 쌍은 `sealed_facing_pairs` 지표로만 보고 |
| 잠긴 문 | 도어 연결에 `locked: true`, `key_id`. 열쇠는 `key` 태그 룸(Treasure 모듈)에 놓인다 |
| 룸 좌표 → cm | 셀 중심 = (x×400+200, y×400+200). 출력 좌표는 최소 셀이 (0,0)이 되도록 평행 이동됨 |
| 아틀라스 슬롯 | `--slot K` → `world_offset_cm = [300000 + K×30000, 300000, 0]` (필드 밖, 슬롯 간 200m) |
| 크기별 룸 수 | Small 6~9, Medium 10~15, Large 16~24 (복도 모듈 제외) |

## 모듈 카탈로그 (Theme_Crypt, 코드 안 `THEMES` 딕셔너리)

| 모듈 | 셀 | 도어 소켓 (로컬 셀, 방향) | 태그 | 쓰임 |
|---|---|---|---|---|
| Entrance | 1×1 | (0,0)N | start | 시작. 남쪽이 바깥 입구(`entry_transform`) |
| Straight_A | 1×1 | (0,0)N, (0,0)S | corridor | 직선 복도 |
| Straight_B | 1×2 | (0,0)N, (0,1)S | corridor | 2칸 직선 복도 |
| Corner_A | 1×1 | (0,0)N, (0,0)E | corridor | 꺾임 |
| Corner_B | 2×1 | (0,0)S, (1,0)N | corridor | 어긋난 꺾임(지그재그) |
| T_Junction | 1×1 | (0,0)N, E, W | combat | 전투 소형(3문) |
| Large_A | 2×2 | (0,0)N, (1,0)E, (1,1)S, (0,1)W | combat | 전투 대형(4문), Hub 중앙 |
| Large_B | 3×2 | (1,0)N, (2,1)E, (1,1)S, (0,0)W | combat | 전투 대형(4문), Hub 중앙 |
| DeadEnd | 1×1 | (0,0)S | deadend | 막다른 방 |
| Treasure | 1×1 | (0,0)S | treasure (+key) | 보물, 열쇠 방 |
| Elite | 2×2 | (0,1)S, (1,0)N, (1,1)E | elite | 정예 |
| Boss | 3×2 | (1,1)S | boss | 보스(`exit_transform` = 중심) |

흐름 노드 → 모듈: Start→Entrance, Combat→T_Junction/Large_A/Large_B(필요 도어 수 이상인 것만), Hub→Large_A/B, Elite→Elite, Treasure/Key→Treasure, DeadEnd→DeadEnd, Boss→Boss.

## 알고리즘

1. 흐름 그래프(`build_flow_*`): 크기 범위에서 노드 수를 뽑고 템플릿별로 구성.
   - Linear: Start→Combat×n→Elite→Boss.
   - Branch: 주 경로 + 1~4개 가지(길이 1~2, 끝은 Treasure/Elite/DeadEnd). 한 노드에 가지 2개까지(도어 4개 한도).
   - Loop: 주 경로의 Combat i, j(j ≥ i+2)를 길이 0~2 가지로 이어 합류. 여유가 있으면 Treasure 가지 1개.
   - Hub: Start→Hub(4문) → 날개 3개(보스 날개 = Combat×b→Elite→Boss, 보물 날개, 잎 날개).
   - KeyLock: 주 경로의 Elite 앞 또는 Boss 앞 간선을 잠그고, 잠금 앞 노드에서 Key 가지(Combat 0~1 + Key). 열쇠 노드 순서 인덱스 < 잠금 인덱스를 생성 단계에서 보장.
2. 배치(`solve_layout`): Entrance 를 (0,0)에 놓고 트리 간선을 너비 우선으로 붙인다. 각 간선은 부모의 빈 소켓 → 복도 0~3칸(무작위 걷기) → 자식 모듈·회전·소켓 조합을 시도(노드당 120회). 루프 간선은 빈 셀 너비 우선 탐색으로 복도 경로를 찾아 봉합. 실패하면 파생 시드로 전체 재시도(최대 300회).
3. 검증(`validate_dungeon.validate`) → 4. 후보 채점(`batch_dungeons.score_candidate`).

## 출력 스키마 (`layout.json`)

```
generator_version, seed, theme, flow, size, cell_size_cm, axis_convention
flow_graph: {nodes:[{id, kind}], edges:[{a, b, kind: tree|loop, locked, key_id}]}
rooms: [{id, module, rotation, cell_origin:[x,y], cells:[[x,y],...], tags:[...], flow_node|null,
         doors:[{cell:[x,y], dir, connected_room|null, locked, key_id}]}]
doors: [{room_a, cell_a, dir_a, room_b, cell_b, dir_b, locked, key_id}]      # 연결된 문만, 중복 없음
keys_locks: [{key_id, key_room, lock_door(doors 인덱스)}]
bounds: {min_cell, max_cell, size_cells, size_cm}
entry_transform / exit_transform: {location_cm:[x,y,z], yaw, room}          # yaw: N=-90, E=0, S=90, W=180
room_count(복도 제외), module_count, layout_restarts
slot, world_offset_cm                                                          # --slot 지정 시
validation: {passed, checks:[{name, passed, message, details}], metrics:{...}, max_deadend_ratio}
```

## 검증 항목 (설계서 11.1)

| 검사 이름 | 내용 | 실패 시 details |
|---|---|---|
| required_rooms | start 태그 룸 1개, boss 룸 1개 이상 | — |
| overlap | 셀이 두 룸에 속하면 실패 | 셀 좌표와 룸 id |
| door_integrity_grid_nav | 모든 문 연결이 격자상 인접·반대 방향·셀 소유·소켓 존재를 만족 (내비메시 단절 검사의 데이터 단계 대체) | 문제 문의 룸 id, 셀 |
| connectivity | 잠금 무시하고 입구에서 모든 룸(보스 포함) 도달 | 도달 불가 룸 id, 셀 |
| room_count | 복도 제외 룸 수가 크기 범위 안 | 실제/허용 범위 |
| deadend_ratio | (start·boss 제외 도어 1개 룸) / 룸 수 ≤ 0.4 (인자로 조정) | 막다른 룸 id |
| progression_key_lock | 보유 열쇠 집합을 갱신하는 BFS 로 열쇠 방이 잠금 앞에서 도달 가능, 보스와 모든 룸 도달 가능 | 원인 |

지표(`metrics`): room_count, module_count, door_count, locked_door_count, loop_count(간선 - (모듈 - 1)), branch_count(도어 3개 이상 룸), deadend_count/ratio, main_path_rooms/modules/ratio(입구→보스 최단 경로), sealed_facing_pairs.

## 한계

- 내비메시 검증은 격자 문 정합 검사로 대체한다. 실제 내비 검사는 에디터에서 베이크 후 `UNavigationSystemV1::Build()` → `TestPathSync` 로 따로 한다(03-architecture 3.7).
- 우연히 마주 보는 봉인 소켓 쌍은 벽으로 취급한다(`sealed_facing_pairs` 지표). 베이크 시 벽 플러그를 넣어야 하며, 열어 주면 Key/Lock 우회가 생길 수 있다.
- 복도는 무작위 걷기(트리)와 최단 경로(루프)라 미학적 제어가 없다. 회전 4종을 모두 허용하므로 탑다운 벽 가림 정책(0° 기본)은 베이크 단계에서 별도 처리해야 한다.
- 모듈 카탈로그는 코드 안 딕셔너리다. 언리얼 데이터 에셋과 동기화하는 절차는 아직 없다(테마 추가 시 `THEMES` 에 항목 추가).
- 백트래킹 대신 전체 재시도 방식이라 Large Loop 는 재시도가 수십 회까지 갈 수 있다(100 시드 기준 최대 33회, 300회 상한 안).

# P1 PIE 검증 기록 (2026-09-12, 내비메시 Dynamic)

## 자동화 테스트 (NullRHI, `Automation RunTests TDGame.SeamlessTravel+TDGame.WorldState`)

```
LogTDGame: SeamlessTravel: 'MainCrypt' ready after 0.43s.
LogTDGame: SeamlessTravel: 'MainCrypt' traveled after 0.45s.
LogTDGame: SeamlessTravel: 'MainCrypt' ready after 6.77s.
LogTDGame: SeamlessTravel: 'MainCrypt' traveled after 6.79s.
LogAutomationController: Display: Test Completed. Result={Success} Name={RoundTripCompletesWithoutLoadingGap} Path={TDGame.SeamlessTravel.RoundTripCompletesWithoutLoadingGap}
LogAutomationController: Display: Test Completed. Result={Success} Name={ActorRecordRoundTrip} Path={TDGame.WorldState.ActorRecordRoundTrip}
LogTDGame: SeamlessTravel: 'MainCrypt' ready after 0.41s.
LogTDGame: SeamlessTravel: 'MainCrypt' traveled after 0.43s.
LogTDGame: SeamlessTravel: 'MainCrypt' ready after 0.02s.
LogTDGame: SeamlessTravel: 'MainCrypt' traveled after 0.05s.
LogTDGame: SeamlessTravel: 'MainCrypt' ready after 4.21s.
LogTDGame: SeamlessTravel: 'MainCrypt' traveled after 4.23s.
LogAutomationController: Display: Test Completed. Result={Success} Name={ChestStateSurvivesStreaming} Path={TDGame.WorldState.ChestStateSurvivesStreaming}

```

로그: `Saved/Logs/seamless_dynamic2.log`. 첫 실행(`seamless_dynamic.log`)은 왕복은 성공했으나 `ATDDungeonEntrance::BeginPlay`의 오버랩 델리게이트 중복 바인딩 ensure로 실패 → `AddUniqueDynamic`/`EndPlay RemoveDynamic` 수정 후 통과.

## PIE 수동 절차 자동화 (`python Tools/WorldGen/pie_p1_checks.py`)

# PIE P1 검증 (2026-09-12 13:39)
{'jsonrpc': '2.0', 'id': 2, 'result': {'content': [{'type': 'text', 'text': '{"returnValue":null}'}]}}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: -10600.000000, y: 2600.000000, z: 630.350028} state: None
## P1-05 걸어 들어가기 왕복
### round 1
- `approach MainCrypt` → approach MainCrypt -> {x: 27587.900000, y: -26587.900000, z: 1759.800000} | pawn: {x: 27587.900000, y: -26587.900000, z: 1759.800000}
- `enter MainCrypt` → enter MainCrypt -> {x: 29387.900000, y: -26587.900000, z: 1759.800000} | pawn: {x: 305000.000000, y: 303400.000000, z: 96.000000}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 305000.000000, y: 303400.000000, z: 98.149999} state: None
- `exit MainCrypt` → exit MainCrypt -> {x: 302600.000000, y: 300400.000000, z: 120.000000} | pawn: {x: 302600.000000, y: 300400.000000, z: 120.000000}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 29621.702908, y: -26810.198529, z: 2237.006528} state: None
- 판정: 입장 OK(x=305000) / 귀환 OK(x=29622)
### round 2
- `approach MainCrypt` → approach MainCrypt -> {x: 27587.900000, y: -26587.900000, z: 1759.800000} | pawn: {x: 27587.900000, y: -26587.900000, z: 1759.800000}
- `enter MainCrypt` → enter MainCrypt -> {x: 29387.900000, y: -26587.900000, z: 1759.800000} | pawn: {x: 29387.900000, y: -26587.900000, z: 1759.800000}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 305000.000000, y: 303400.000000, z: 98.149999} state: None
- `exit MainCrypt` → exit MainCrypt -> {x: 302600.000000, y: 300400.000000, z: 120.000000} | pawn: {x: 302600.000000, y: 300400.000000, z: 120.000000}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 29621.702908, y: -26810.198529, z: 2237.006528} state: None
- 판정: 입장 OK(x=305000) / 귀환 OK(x=29622)
### round 3
- `approach MainCrypt` → approach MainCrypt -> {x: 27587.900000, y: -26587.900000, z: 1759.800000} | pawn: {x: 27587.900000, y: -26587.900000, z: 1759.800000}
- `enter MainCrypt` → enter MainCrypt -> {x: 29387.900000, y: -26587.900000, z: 1759.800000} | pawn: {x: 29387.900000, y: -26587.900000, z: 1759.800000}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 305000.000000, y: 303400.000000, z: 98.149999} state: None
- `exit MainCrypt` → exit MainCrypt -> {x: 302600.000000, y: 300400.000000, z: 120.000000} | pawn: {x: 302600.000000, y: 300400.000000, z: 120.000000}
- `where` → subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 29621.702908, y: -26810.198529, z: 2237.006528} state: None
- 판정: 입장 OK(x=305000) / 귀환 OK(x=29622)
## P1-06/07 영속 상자
- `chest status` → chest: TDGen_TestChest_MainCryptGate opened: False stable_id: (guid) loc: {x: 29000.000000, y: -26200.000000, z: 2140.000000} | chest: TDGen_TestChest_Village opened: False stable_id: (guid) loc: {x: -10300.000000, y: 2900.000000, z: 530.000000} | chests loaded: 2
- `chest open` → opened 2 | chest: TDGen_TestChest_MainCryptGate opened: True stable_id: (guid) loc: {x: 29000.000000, y: -26200.000000, z: 2140.000000} | chest: TDGen_TestChest_Village opened: True stable_id: (guid) loc: {x: -10300.000000, y: 2900.000000, z: 530.000000} | chests loaded: 2
- `chest save 1` → save requested 1 | chest: TDGen_TestChest_MainCryptGate opened: True stable_id: (guid) loc: {x: 29000.000000, y: -26200.000000, z: 2140.000000} | chest: TDGen_TestChest_Village opened: True stable_id: (guid) loc: {x: -10300.000000, y: 2900.000000, z: 530.000000} | chests loaded: 2
- `to MainCrypt` → requested travel to MainCrypt | subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 29621.702908, y: -26810.198529, z: 2237.006528} state: None
- `field` → requested travel to field | subsystem lookup: module 'unreal' has no attribute 'SubsystemBlueprintLibrary' | pawn: {x: 305000.000000, y: 303400.000000, z: 98.149999} state: None
- `chest status` → chest: TDGen_TestChest_MainCryptGate opened: True stable_id: (guid) loc: {x: 29000.000000, y: -26200.000000, z: 2140.000000} | chest: TDGen_TestChest_Village opened: True stable_id: (guid) loc: {x: -10300.000000, y: 2900.000000, z: 530.000000} | chests loaded: 2
- `chest load 1` → load requested 1 | chest: TDGen_TestChest_MainCryptGate opened: True stable_id: (guid) loc: {x: 29000.000000, y: -26200.000000, z: 2140.000000} | chest: TDGen_TestChest_Village opened: True stable_id: (guid) loc: {x: -10300.000000, y: 2900.000000, z: 530.000000} | chests loaded: 2
- `chest status` → chest: TDGen_TestChest_MainCryptGate opened: True stable_id: (guid) loc: {x: 29000.000000, y: -26200.000000, z: 2140.000000} | chest: TDGen_TestChest_Village opened: True stable_id: (guid) loc: {x: -10300.000000, y: 2900.000000, z: 530.000000} | chests loaded: 2
{'jsonrpc': '2.0', 'id': 4, 'result': {'content': [{'type': 'text', 'text': '{"returnValue":null}'}]}}

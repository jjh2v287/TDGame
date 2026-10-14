# P3-13 이계 지역 2곳과 외곽 관문 심리스 이동 (2026-10-14, claude)
교훈: L-worldgen-01(귀환 준비 지연과 출발 지점 붙잡기), L-worldgen-02(DarkFantasy 재질 함정), L-editor-14, L-build-03
명령: `python Tools/WorldGen/generate_realm.py --realm all` → `python Tools/run_in_editor.py Tools/WorldGen/editor_build_realm.py` → 헤드리스 `Automation RunTests TDGame.WorldGen+TDGame.SeamlessTravel+TDGame.WorldState` → `python Tools/WorldGen/pie_p1_checks.py --ids SunkenStarfieldSouthwest,SunkenStarfieldNortheast,PaleHeightsSoutheast --rounds 1`

- 대장 `Docs/Tasks/phase-3-outdoor-generator.md` P3-13 / 결정 D-41
- 명세·조사·하위 에이전트 기록: `Saved/AgentOps/20261013/realm-spec.md`, `realm-understand.md`, `realm-implement-result.md`

## 만든 것
| 이계 | 중심(cm) | 크기 | 관문(필드 쪽 자리) | 분위기 |
|---|---|---|---|---|
| 가라앉은 별밭 `SunkenStarfield` | (−302400, 302400) | 504m×504m | `SunkenStarfieldSouthwest`(옛 HollowCave)·`SunkenStarfieldNortheast`(옛 SunkenCrypt) | 절벽 분지, 별 호수·수로, 청록·보라 발광 버섯 숲, 가는 별 기둥, 달빛 청색 |
| 창백한 고원 `PaleHeights` | (−302400, −302400) | 504m×504m | `PaleHeightsSoutheast`(옛 MainCrypt) | 구름 바다 위 메사 5개·다리 4개, 하얀 자작나무, 창백한 기둥 원환, 금빛 햇빛 |

- 이동: 관문 50m 앞(필드 쪽)·150m 앞(이계 쪽)부터 목적지 선로딩 → 관문 상자에 들어서면 지역 색으로 0.35초 페이드아웃 → 순간이동 → 같은 프레임에 전역 태양·스카이라이트·대기·안개를 이계 값으로 전환 → 정착 뒤 0.6초 페이드인. 이계 진입 뒤 10초 동안 그 관문의 필드 귀환점을 스트리밍 소스로 붙잡는다(`RealmDepartureHoldSeconds`).
- 던전 생성기·아틀라스 슬롯은 그대로이며 콘솔 `TDTravelToDungeon MainCrypt`로 들어간다.

## 자동화 테스트 (헤드리스 NullRHI)
`UnrealEditor-Cmd.exe TDGame.uproject -ExecCmds="Automation RunTests TDGame.WorldGen+TDGame.SeamlessTravel+TDGame.WorldState" -TestExit="Automation Test Queue Empty" -unattended -NullRHI`
- 결과: 29/29 Success (최종 `Saved/Logs/P3-13-parent-tests-3.log`, 붙잡기 위치를 관문 필드 귀환점으로 고친 뒤 귀환 준비 0.01~2.6s). 신규: `TDGame.SeamlessTravel.RealmRoundTrip`(관문 3곳 왕복·이계 상자 안 도착·활성 이계 ID·태양 세기 = 프로필, 귀환 뒤 필드 값 복원), `TDGame.SeamlessTravel.PreloadCancelAllowsOtherTarget`, `TDGame.WorldGen.Realm.FindGateAndBounds`, `TDGame.WorldGen.Realm.DataValidationRejectsBadGates`, `TDGame.WorldGen.Realm.DestroyLandscapeInBoundsUsesParentLocation`. 기존 MainCrypt 왕복·상자 상태 유지 테스트 회귀 없음.

## PIE 걸어서 왕복 (`python Tools/WorldGen/pie_p1_checks.py --ids SunkenStarfieldSouthwest,SunkenStarfieldNortheast,PaleHeightsSoutheast --rounds 1`)
- 관문 3곳 입장·귀환 판정 FAIL 0, 상자 열림 저장·복원 유지(`Saved/WorldGen/pie_p1_checks.md`).
- 준비 시간(에디터 로그 `ready after`):

| 경우 | 출발 지점 붙잡기 전 | 붙잡기 뒤 |
|---|---|---|
| 이계 진입(필드 관문 선로딩 뒤) | 0.03~2.3s | 0.03~3.7s |
| 도착 직후 필드 귀환 | 9.1~23.7s | 0.0~2.7s |
| 이계를 돌아다녀 필드가 내려간 뒤 귀환(콘솔, 선로딩 없이) | — | 11.7s(별밭)·약 13s(고원) |

- 마지막 경우 대책: 이계 쪽 출구 관문 선로딩 반경 150m. 기본 이동 속도 600cm/s(`ATDCombatCharacter::DefaultMaxMoveSpeed`)면 약 25초 걸어야 출구에 닿으므로 준비 시간(11.7~13s)을 덮는다.

## 캡처 (`Docs/Validation/P3-13-realms/`, 에디터 뷰포트에 이계 분위기를 저장 없이 얹어 촬영)
- 레이아웃: `layout_SunkenStarfield.jpg`, `layout_PaleHeights.jpg`
- 별밭: `sf_west_arrival`(서쪽 도착 광장), `sf_game_view`(게임 카메라 높이), `sf_mushroom_ring`(공터 C2), `sf_pillar_ring`(별 기둥 원환 C4), `sf_shore`(호숫가·보라 안개)
- 고원: `ph_arrival_game`(도착 메사), `ph_bridge`(구름 바다 위 다리), `ph_spire`(기둥 원환), `ph_west_edge`(서 메사 공터)
- 필드 관문: `gate_starfield_sw`, `gate_starfield_ne`, `gate_paleheights`

## 분위기 조정 기록
- 1차 캡처: 별밭 과다 암전(태양 1.2 lux), 가산 발광 재질(M_Emissive)이 노출 보정 +10.4에서 하얗게 타서 원반처럼 보임, `DiffuseMultiplier`를 스칼라로 넣어 적용 실패(실제는 벡터), 기둥 과대.
- 확정: 별밭 태양 3 lux·스카이라이트 1.3·안개 0.02·노출 +0.6, 발광 버섯은 `MI_Dreadplants`의 `AmbientColor`, 땅에 작은 발광 버섯 700(별), 기둥 xy 0.45~0.8·z 4~8. 고원은 바위흙 텍스처의 주황빛을 없애려고 랜드스케이프 재질에 `RealmDesaturation`(기본 0 = 메인 불변)을 추가해 0.78, 기둥 절반 크기.

## 남은 위험·후속
- 에디터 PIE 수치다. 쿠킹 빌드 준비 시간은 미측정(P4-03·P1-09와 함께).
- 목적지가 끝내 준비되지 않으면 페이드아웃 유지 상태로 계속 기다린다(기존 '경고 후 대기' 정책). 대장 todo로 남김.
- 이계 인카운터(몬스터)는 없다. 공터 마커 `TDRealmCombatClearing`만 있다.
- 아틀라스·검증기의 로딩 범위 하드코딩 12800 ↔ 레벨 실제 76800(umap 해석값) 불일치. 대장 todo로 남김.

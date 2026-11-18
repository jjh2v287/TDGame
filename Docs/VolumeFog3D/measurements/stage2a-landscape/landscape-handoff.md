# 2a-4 실제 지형(랜드스케이프) 확인 넘김 글 (2026-11-16 00:30~01:00 KST, 측정 작업자)

기준: `Docs/VolumeFog3D/stage2/prereg-2a-landscape.md`(읽기만). 판정은 적지 않는다. 장부·`Source/`·`Tools/`·정본 JSON·등록 문서는 고치지 않았다. 빌드 안 함. 에디터는 켜 둔 채(PID 17104, 열린 레벨 `LV_DarkFantasy_OpenWorld`, PIE 꺼짐, 저장 안 된 패키지 없음 — 시작 때 열린 레벨은 `LV_TDFog3DGroundStage` 였다).

## ① 한 일 / 못 한 일
- 한 일: L0·L1(두 곳 512기둥)·L2(비탈 묶음 + 85도 절벽 묶음, 창 자리 각 6곳)·L3(촬영 없는 걷기 10.7m×) · 정보 1·3·4(그림)·5.
- 못 한 일: 정보 2(다시 찍는 프레임 GPU 시간)와 정보 4 의 비용 비교(상자 높이 300 대 쓴 값) — **못 잼**(시간·호출 예산). 랜드스케이프 컴포넌트 크기(쿼드)는 Python 에 드러나지 않아 못 읽음.

## ② 값 표
| ID | 값 |
|---|---|
| 2A-L0 | Fog3D 경고·오류 0, RDG 오류 0. ensure 1건(같은 사건이 두 줄): UAF `TimelineTrait.GetState`(InjectionSiteTrait) — 15:54:25(UTC) 촬영 중 순간이동 직후, L-editor-98 꼴, Fog3D 아님. 순간이동 내비 ensure(L-editor-105)는 이번엔 없음. 끝 요약 `submitted=34367 rejected=0 deferred=22`(22는 첫 진입 entering·먼 순간이동 teleport — 아래 L3 와 별개). `git status --short Content` 시작 전 = 끝난 뒤(같음), `__ExternalActors__`·`__ExternalObjects__` 새 파일 0. 원자료 `l0_log_scan.json`, `pie_log_slice.txt` |
| 2A-L1 | 유효 512 / 뺀 것 0(되읽기 무효 0, 참값 못 얻음 0). 랜드스케이프 아닌 것에 먼저 닿아 다시 쏜 기둥 29(A 7, B 22, 뺀 것 아님). \|정본−참값\| 중앙값 0.0062cm, 95% 0.0164cm, 최대 **0.0215cm**(B, 세계 (−9554.69, 11617.19), 폰에서 1438cm, 경사 13.8도). 부호 평균 −0.006(정본이 아주 조금 낮음). 로그 높이는 소수 둘째 자리(해상도 0.01cm) |
| L1 곳 A(완만) | 폰 (−11800, 2200), 캡처 사각형 최소 칸 (−944, −48) 크기 384, 구역 중심 z 625·반높이 225(바닥 400, 땅 498.5~543.0), 256기둥, 최대 0.0201, 경사 최대 2.8도 |
| L1 곳 B(경사) | 폰 (−9800, 10200), 최소 칸 (−816, 464), 구역 중심 z 370·반높이 470(바닥 −100, 땅 12.0~538.9), 256기둥, 최대 0.0215, 경사 15도 넘는 기둥 11개(최대 21.8도, 그 11개의 최대 차 0.0186). A–B 거리 82.5m. 바깥 4분의 1 띠: A 192·B 144기둥 |
| 2A-L2 (비탈 B) | 묶음 최소 칸 (−720, 760) 16×16, 창 자리(캡처 최소 칸) P0 (−816,464) → x+16 → x+32 → x+16(되돌아옴) → P0 → y+16 → y+32 → P0, 자리마다 `GroundRecaptureNow` 뒤 되읽기. 기둥별 최대−최소의 최댓값 **0.00cm**, 벽 표시 0개(이웃 차 최대 6.85cm) → (나) 해당 없음. 같은 자리 두 번 연속 읽기 차 0.00(256기둥) |
| L2 추가 (85도 절벽 C) | 등록 2절의 "절벽에서 한 번 더". 곳 (64507.8, 48257.8)(능선 위, 땅 −2789~2586), 구역 중심 z 0·반높이 3000. 묶음 최소 칸 (4184, 3104), 창 자리 P0 (3936,2896) → x+16 → x+32 → x+16 → P0 → y−16 → y−32 → P0. 최대−최소 **0.00cm**, 안쪽 14×14 벽 110개, 창 자리 사이 바뀐 벽 표시 **0**, 두 번 읽기 차 0.00, 이웃 차 최대 145.2cm |
| 2A-L3 | 촬영·프로파일 없는 걷기(품질 2, 초속 600, x ±20m·y ±15m·대각 ±18m, 합 약 106.7m). 창 이동 재캡처 47회, 변환 42회(5회는 다음 창 요청이 1~2프레임 뒤 와서 앞 것이 덮임), 요청→변환 지연 모두 2프레임, **보류 걸음 0**. 재캡처 간격 최소 1·중앙 13·최대 26프레임. 원자료 `l3_walk_log_slice.txt`, `l3_order.json`(`fog3d_ground_cost.py order` 출력) |

## ③ 정보 1~5
1. 랜드스케이프: 3개, 칸 100cm(스케일 100), `bEnableNanite`=True 이지만 `r.Nanite.ProjectEnabled`=0(프로젝트 Nanite 꺼짐 → 보통 랜드스케이프 컴포넌트로 그려질 것으로 봄, 확인은 값으로만: 캡처 높이가 충돌면과 0.02cm 안이니 캡처에 지형이 그려졌다). `CollisionMipLevel` 0, `SimpleCollisionMipLevel` 0 → 참값 해상도가 그림과 같아 L1 을 가를 수 있는 조건. 에디터 쪽 컴포넌트: LandscapeComponent 384·HeightfieldCollision 384·LandscapeNaniteComponent 96, 스트리밍 프록시 96(PIE 에 83 로드). LOD0 화면 크기 0.5, LOD 분포 1.25/3.0.
2. 다시 찍는 프레임 GPU 시간: 못 잼.
3. 그림(media `p9_*`, index.html ⑨절): 비탈 옆 뒤·높은 카메라 켬|끔, 걷기 켬|끔 움직이는 그림. 끔에서는 안개 층이 구역 바닥(−100cm, 땅 밑)에 깔려 거의 안 보인다(평평한 층을 이 지형에 쓰면 구역 바닥을 어디 두느냐에 따라 묻히거나 뜸).
4. 캡처 사각형 밖: 구역을 반크기 7000 으로 키워 사각형(60m) 밖도 구역 안에 들게 한 먼 높은 그림 `p9_rect_edge_far.jpg`. 켬에서 사각형 남쪽 밖(화면 아래 앞부분)에도 안개 층이 보인다. 그 높이가 무엇을 따르는지는 이 그림만으로 가리지 못함(되읽기는 사각형 안만 읽는다). 비용 비교는 못 잼.
5. 겪은 제약: (가) 구역 상자는 그 자리 땅 높낮이를 직접 따라 잡아야 했다 — 바닥을 가장 낮은 땅보다 아래(높이 0 은 무효로 찍힘), 폰이 상자 안(세로 여유 200)이어야 돈다. 곳마다 상자 중심 z·반높이를 바꿨다(A 625/225, B 370/470, C 0/3000). 캡처 높이 설정(윗면+500)은 바꾸지 않았고 세 곳 모두 문제없었다(C 는 땅 높이차 54m). (나) 월드 파티션: PIE 에서 프록시 83/99 로드, 먼 곳(76m 밖)으로 순간이동 직후 변환 지연이 6프레임(teleport 보류 2)이었고 되읽기 무효는 없었다. 스트리밍이 늦게 뜨는 순간은 재지 않았다(등록 5절 범위 밖).

## ④ 구역을 PIE 에만 띄운 방법 (작업 폴더 스크립트, `bash run.sh <스크립트> '<JSON>'`)
1. 시도 1 `GameplayStatics.begin_deferred_actor_spawn_from_class` → Python 에 없음(AttributeError). 2. 시도 2 성공: PIE 월드의 플레이어 컨트롤러로 `EnableCheats` → `summon /Script/TDGame.TDFog3DZoneActor`(CheatManager 가 PIE 월드에 만든다) → `s5_fix.py`/`tdls.fix_zone_extent`: `ZoneVolume` 에 `/Engine/BasicShapes/Cube` 지정, `VolumeMaterial`=`M_TD_Fog3DVolume`, `ZoneHalfExtentCm` 설정, `ZoneVolume` 상대 스케일 = 반크기/50 → 조절 표 사본 다시 읽기(`reload_preset_tuning_from_paths`)로 설정 세대를 바꿔 `ConfigureLayer(true)` 를 다시 돌게 함. 클래스 기본 객체(CDO) 반크기를 바꾸고 summon 하는 길은 반영되지 않아 버렸다(CDO 는 원래 값 (3000,3000,150) 그대로 확인).
- 순서: `s1_open_level.py`(저장 없이 열기) → `s_tuning_on.py`(사본 `tuning_follow_on.json`: `bFollowsGround` true + 땅 재질 경로) → `s2_start_pie.py` → `s4_site.py`(폰 순간이동·summon) → `s5_fix.py` → `TD.Fog3D.Quality 2`(`s_cmd.py`) → `s6_l1.py`(참값 추적 + 되읽기 차례) → `l1_join.py A|B` → `s7_move.py`(곳 옮기기) → `s8_l2.py` → `l2_analyze.py` → `s10_walk.py` → `s11_shots.py`·`s13_walkshots.py`(촬영) → `s14_end.py`(차례 해제, 품질 0 복구, 정본 다시 읽기, PIE 끝) → `q_after.py`.
- 에디터 월드 구역 0, 카메라 액터 0 확인(PIE 끝난 뒤).

## ⑤ 겪은 문제와 교훈 후보
- `TD.Fog3D.Quality` 가 0 으로 남아 있었다(앞 작업의 비용 측정 잔재) → 처음 캡처가 크기 256(품질 0). 2 로 바꿔 쟀고 끝에 0 으로 되돌렸다. 교훈 후보: 측정 시작 때 이 콘솔 변수 값을 읽고 적는다.
- 15:49(UTC) 무렵 PIE 화면 클릭으로 탑다운 클릭 이동(`FX_Cursor_Success`)이 일어나 폰이 움직였다(창 재캡처 29회). 내 측정 구간 밖이었다. 교훈 후보: PIE 화면을 누르면 폰이 걸어가므로 측정 중에는 손대지 않는다.
- 되읽기 로그 좌표는 소수 첫째 자리라 `round(x,1)` 맞춤이 어긋날 수 있다 → 0.06cm 안 가장 가까운 것으로 맞춤.
- 절벽 찾기 넓은 선 추적에서 `line_trace_single` 이 못 맞히면 None 을 돌려준다(첫 시도 예외).
- HighResShot 을 6프레임 간격으로 내면 앞 것이 끝나기 전 요청이 버려진다(18장 요청에 4~5장) → 18프레임 간격·느린 걷기(입력 0.4)로 8~9장.
- 끔 비교 그림은 구역 바닥이 땅 밑이라 안개가 거의 안 보인다 — 감독이 "끔"의 뜻을 정할 때 참고.

## ⑥ git·저장 안 된 패키지
- `git_content_before.txt` = `git_content_after.txt`(diff 없음): 둘 다 `M TDFluidPresetTuning.json`(시작 전부터, 정본 sha256 fef5ba34… 그대로 읽음) + 미추적 `LV_TDFog3DGroundStage`·`LV_TDFog3DStage`·`M_TD_Fog3DVolume`·`M_TD_Fog3DVolumeGround`.
- 저장 안 된 패키지: 시작 전 맵 0·콘텐츠 0, 끝난 뒤 맵 0·콘텐츠 0. 저장 명령은 내리지 않았다.
- 새로 쓴 파일: 이 폴더 전부, `Docs/VolumeFog3D/measurements/stage2a-screen/media/p9_{slope_back,slope_high,rect_edge_far}.jpg`·`p9_walk_onoff.webp`(합 0.69MB, media 전체 4.3MB), 같은 폴더 `index.html` ⑨절 추가. `Saved/Screenshots/WindowsEditor/HighresScreenshot00003~00030.png` 이 촬영으로 생겼다(Saved).

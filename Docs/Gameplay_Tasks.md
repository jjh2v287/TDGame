# 게임플레이 작업 대장 — 한 판(수직 단면)·기반 시스템

작성: 2026-11-18 claude. 근거: `Docs/Gameplay_Fun_And_Systems_Analysis.md`(GP·GF·GQ·FL), `Docs/GameplayFoundation_Plan.md`(FD·FDQ). 등록 승인: 사용자 "권장대로 진행"(2026-11-18, 승인 카드 [1-13]·[1-15], `Docs/Tasks/decisions.md` D-65).
규칙: 상태는 todo·doing·blocked·done·decision, 담당은 `<agent>/<날짜>`, 청구·기록 형식은 AGENTS.md 15절. 상세 블록은 착수할 때 3절에 만들고, 그 전에는 2절 색인 한 줄이 대장 항목이다. 몬스터 AI 대장(`Docs/MonsterAI_CombatSim/07-roadmap-and-tasks.md`)에 이미 있는 일(M0-xx)은 그쪽 항목을 청구하고 여기에는 연결만 둔다. 증거는 `Docs/Validation/Gameplay/`, 교훈 코드는 gameplay.

## 1. 결정 (2026-11-18 사용자 "권장대로 진행" → 권장안 적용, claude 기록, 카드 번호로 뒤집을 수 있음)

| ID | 결정 | 메모 |
|---|---|---|
| FDQ-01 | 3D 안개 현 구간(2b)을 닫은 뒤 한 판 단계 3까지 게임 작업 우선(약 6~8주). 유체·안개는 막힘 수정과 게임 연결만. `Docs/NOW.md` 다음 행동 3개 중 2개는 한 판 작업 | 단계 1 관문(약 2주 뒤)에서 다시 판단 |
| FDQ-02 | `Source/TDGame/Foundation/` + `Tools/check_foundation_boundary.py`, 이 대장 신설 | AGENTS.md 14·15절 반영(2026-11-18) |
| FDQ-03 | 상호작용 입구(GP-17·FD-02)를 단계 2 첫머리로 | 단계 2의 F 키 소비자는 시험 상자, 성소는 겹침 출처 |
| FDQ-04 | 싱글 플레이 유료 패키지 가정 | 스팀 페이지 전에 다시 본다 |
| FDQ-05 | 지금 정하지 않음. 비전투 NPC AI 기본값은 C++ 유틸리티 | 거점(GP-36) 착수 때 |
| GQ-01 | (a) 한 판 성장 = 레벨 + 주문 해금 + 물약, 장비는 뒤 | 레벨 상한 (ㄱ) 상한 10 / (ㄴ) 레벨당 성장 축소는 단계 2 명세 때 사용자 확인, 임시 (ㄱ) |
| GQ-03 | (c) 은총·유니크 같은 데이터, 1차는 조우 보상으로 얻어 칸에 영구 장착·교체 | FD-06 |
| GQ-07 | 클릭 공격·Space 구르기·Q 밀치기·1~4 주문(시작 0칸, 첫 승리로 첫 주문)·R 물약·F 상호작용·오른쪽 클릭 막기 | |
| GQ-10 | (b) 아레나 실보상 지금, 재무장은 GP-33 | |
| GQ-11 | (a) 시작 맵 `LV_DarkFantasy_OpenWorld`(에디터 시작 맵도) | 1일차 `-game` 실행으로 확인 |
| GQ-12 | (a) 산룡 둥지 = 고원 메사 | |
| GQ-13 | (a) 조우는 아레나 열린 모드로 통일, 스포너는 던전 개방 때 지연 생성 | |
| GQ-14 | 한 판은 (a) 콘솔 유지(던전 제외), 본편은 (b) 승강기 하강 | 페이드 이동은 쓰지 않는다 |
| GQ-15 | 한 판 (b) 사망 손실 없음, 통화가 생기면 (a) | |
| GQ-16 | (b) 레벨업 때 늘어난 최대치만큼 체력 | |
| GQ-21 | HUD는 (a) 순수 C++(기존 오버레이 확장). 메뉴는 순수 C++ Slate로 판정(2026-11-18 claude, GP-11 착수 때 — 뒤집을 수 있음), 결과 화면도 같은 방식 기본 | |
| GQ-22 | 한 판 30분(20~40) / 본편 6~8시간 + 반복 10시간 이상 / 멀티 없음(복제 0 고정) | |
| GQ-25 | (a) 구르기 버퍼·고정 시험 변경 | |
| 남은 것 | GQ-20(미추적 팩, 라이선스는 사용자), GQ-24(외형, 단계 5), GQ-23(음악·사양), 그 밖의 GQ는 해당 단계 착수 때 | |

## 2. 색인

| ID | 할 일 | 단계 | 등급 | 상태 | 담당 | 연결 |
|---|---|---|---|---|---|---|
| GP-01 | 게임 설계 한 장·결정 묶음 | 0 | P0 | done |  | GQ 먼저 묶음, FDQ-01~05 |
| GP-02 | 실행하면 마을에서 시작 | 1 | P0 | doing(실행 확인) | claude/2026-11-18 | GQ-11 |
| GP-03 | 필드 공터 3곳 열린 조우 | 1 | P0 | todo |  | FL-05 |
| GP-04 | 고원 보스 길(켄타우로스→산룡) | 3 | P0 | todo |  | FL-01 |
| GP-05 | 처치 경험치·레벨업 | 2 | P0 | todo |  | FD-04, FL-07 |
| GP-06 | 주문 장착 칸·해금 | 2 | P0 | todo |  | FD-04, GQ-07 |
| GP-07 | 회복 API·충전식 물약 | 2 | P0 | todo |  | FD-04 |
| GP-08 | 성소: 부활·충전·자동 저장 | 2 | P0 | todo |  | FD-04, FL-04 |
| GP-09 | 진행 저장 v2·이어하기 | 3 | P0 | todo |  | FD-04 |
| GP-10 | 목표 한 줄·방향·결과 화면 | 2 | P0 | todo |  | FD-03 |
| GP-11 | Esc 일시정지·종료 | 1 | P0 | doing(사람 화면 확인) | claude/2026-11-18 | - |
| GP-12 | 손 조작 난이도 곡선 1차 조정 | 3 | P0 | todo |  | FL-01, FL-06 |
| GP-13 | 재미 측정 기록·자동 전투원 | 1(기준선은 0) | P1 | doing(기록기 끝) | claude/2026-11-18 | FD-05, M0-05 |
| GP-14 | 스태미나 경제(구르기 25) | 2 | P1 | todo |  | FD-04 |
| GP-15 | 주문 자원: 근접 충전 마력 | 4 | P1 | todo |  | - |
| GP-16 | 승리 보상 선택(제단·상자) | 4 | P1 | todo |  | FD-02, FD-06 |
| GP-17 | 상호작용 입력(F)·대상 입구 | 4(2로 앞당김 제안) | P1 | todo |  | FD-02, FDQ-03 |
| GP-18 | 클릭 조작 다듬기 | 4 | P1 | todo |  | - |
| GP-19 | 세계관 한 장·지역 배너 | 4 | P1 | todo |  | - |
| GP-20 | 음악 3층·환경음·보스 포효 | 5 | P1 | todo |  | - |
| GP-21 | 타이틀 화면 | 5 | P1 | todo |  | FD-04 |
| GP-22 | 쿠킹 빌드 완주·프레임 기준 | 5 | P1 | todo |  | - |
| GP-23 | 판단 정보 HUD 확장 | 4 | P1 | todo |  | FL-02, FL-08 |
| GP-24 | Q 방패 밀치기·V0-lite | 4 | P1 | todo |  | FL-03, FL-07 |
| GP-25 | 정예·보스 강인도·무력화 | 4 | P1 | todo |  | - |
| GP-26 | 막기·완벽 막기·예고 색 | 4 | P1 | todo |  | - |
| GP-27 | 동사 변경 효과 첫 12종 | 6 | P1 | todo |  | FD-06, FL-03 |
| GP-28 | 전리품·장비 최소판 | 6 | P1 | todo |  | FD-06 |
| GP-29 | 카메라 남쪽 사각 판정 | 4 | P1 | todo |  | MD-17 |
| GP-30 | 상태 3종·조건부·원소 폭발 | 6 | P2 | todo |  | FL-02 |
| GP-31 | 일반 공격 형상 다양화 | 6 | P2 | todo |  | - |
| GP-32 | 정예 접사 6종·표식 | 6 | P2 | todo |  | - |
| GP-33 | 시련 재무장·계약 | 7 | P2 | todo |  | FD-07 |
| GP-34 | 선택 정예 사냥·이계 공터 7곳 | 7 | P2 | todo |  | GF-06 |
| GP-35 | 지역 위협 등급·몬스터 레벨 | 7 | P2 | todo |  | FD-07 |
| GP-36 | 거점 최소·마을 재건 | 7 | P2 | todo |  | - |
| GP-37 | 지도·발견 기록·완료율 | 7 | P2 | todo |  | - |
| GP-38 | 지름길 | 7 | P2 | todo |  | - |
| GP-39 | 탐험 상자·열쇠·비밀 공간 | 7 | P2 | todo |  | - |
| GP-40 | 사망 손실: 미정산 통화·회수 | 7 | P2 | todo |  | - |
| GP-41 | 경보·공황 시각화 | 6 | P2 | todo |  | - |
| GP-42 | 새 역할: 자폭·방패·치유 | 7 | P2 | todo |  | - |
| GP-43 | 위치·타이밍 확정 치명 | 6 | P2 | todo |  | - |
| GP-44 | 환경 위험물·성소 횃불 | 7 | P2 | todo |  | FL-03 |
| GP-45 | 보스 기회 창·큰 몸 판정 | 6 | P2 | todo |  | - |
| GP-46 | 동적 사건 감독 | 7 | P2 | todo |  | - |
| GP-47 | 세계 등급 II·III | 7 | P2 | todo |  | - |
| GP-48 | 결정론 결투 시뮬·이상치 | 7 | P2 | todo |  | - |
| GP-49 | 던전 3곳 승강기 개방 | 7 | P2 | todo |  | GQ-14 |
| GP-50 | 고정 유니크 30종·도감 | 7 | P2 | todo |  | FD-06 |
| GP-51 | 설정 화면(음량·키) | 7 | P2 | todo |  | - |
| GP-52 | E 갈고리 | 선택 | P3 | todo |  | - |
| GP-53 | 08 반응층 | 선택 | P3 | todo |  | - |
| GP-54 | 카라반 판타지 | 선택 | P3 | todo |  | - |
| GP-55 | 시드 던전 심연 | 선택 | P3 | todo |  | GP-49 |
| GP-56 | 물·연기 반응 | 선택 | P3 | todo |  | - |
| GP-57 | 게임패드·현지화 | 선택 | P3 | todo |  | - |
| GF-01 | 원거리 투사체 바닥 예고 없음 | 3 | 결함 | todo |  | - |
| GF-02 | 구르기·공격 중 주문 시전 | 1 | 결함 | done | claude/2026-11-18 | - |
| GF-03 | 구르기 무반응 구간 | 3 | 결함 | todo |  | - |
| GF-04 | 보스·정예 빙결 점감 없음 | 3 | 결함 | todo |  | - |
| GF-05 | 전투 전역 난수·시드 고정 | 1 | 결함 | done(M0-02·M0-07) | claude/2026-11-18 | M0-01, M0-02, M0-07 |
| GF-06 | 미추적 팩 의존 깨짐 | 0 | 결함 | todo |  | GQ-20, GP-34 |
| FL-01 | 업화의 눈 안전 섬 | 1 | 유체 | todo |  | GP-04, GP-12, FL-B3 |
| FL-02 | 장판을 규칙 수명대로 그리기 | 2 | 유체 | todo |  | GP-23, GP-30 |
| FL-03 | 불 피하는 몬스터(불벽) | 4 | 유체 | todo |  | GP-24, GP-27, GP-44 |
| FL-04 | 성소 점화=부활·저장 | 2 | 유체 | todo |  | GP-08 |
| FL-05 | 공터 재 장막·승리 돌풍 | 1 | 유체 | todo |  | GP-03 |
| FL-06 | 웨이브 출현 소용돌이 예고 | 3 | 유체 | todo |  | GP-12 |
| FL-07 | 손맛 도장(밀치기·레벨업) | 2·4 | 유체 | todo |  | GP-24, GP-05 |
| FL-08 | 공격 준비 응축(조건부) | 4 | 유체 | todo |  | GP-23 |
| FL-B1 | 게임 구도 바닥 외형 승인 | 0 | 유체 | todo |  | - |
| FL-B2 | 바닥 프리셋 열 채널·열 발광 | 선택 | 유체 | todo |  | FL-B1 |
| FL-B3 | 피해 엔티티 생애 메시지 구독 | 1 | 유체 | todo |  | GP-13, FL-01 |
| FL-B4 | 게임의 유체 상태 읽기 검사 | 선택 | 유체 | todo |  | - |
| FD-01 | 경계 규칙·검사 도구 | 1 | 기반 | done | claude/2026-11-18 | FDQ-01 |
| FD-02 | 상호작용 입구 | 2 | 기반 | todo |  | GP-17, FDQ-03 |
| FD-03 | 목표 추적 | 2 | 기반 | todo |  | GP-10 |
| FD-04 | 저장 2판·자동 저장·이어하기 | 2~3 | 기반 | todo |  | GP-08, GP-09 |
| FD-05 | 사건 기록기·기반 시험 월드 | 1 | 기반 | doing | claude/2026-11-18 | GP-13, M0-05 |
| FD-06 | 효과 정의(공통 데이터) | 6 | 기반 | todo |  | GP-27, GP-28 |
| FD-07 | 상황 생성=조우 세트 확장(미룸) | 7 | 기반 | todo |  | GP-33, GP-35 |

행 수: GP 57 / GF 6 / FL 8 / FL-B 4 / FD 7 / 합계 82

## 3. 진행 중 항목

### M0-01·M0-02·M0-07 — 전투 난수 결정론(GF-05)
- 상태·담당·기록은 몬스터 AI 대장의 원 항목에 둔다. 이 대장에는 연결만.

### GP-01 한 판 설계 한 장
- 상태: done (2026-11-18, `Docs/Gameplay_SliceDesign.md`, 권장안 확정 — 레벨 상한만 단계 2 명세 때 확인)
- 목표: 핵심 판타지·코어 루프·목표 플레이 시간·성장·지표 7개·단계 관문·적용 결정을 2쪽 안에 고정한다.
- 완료 조건: 2쪽 이하, 지표 7개 수치, 사용자 확인 1회(이번 답 "권장대로"로 초안 승인, 레벨 상한만 남음).

### FD-01 경계 검사 도구
- 상태: done
- 담당: claude/2026-11-18
- 기록: 2026-11-18 도구 `Tools/check_foundation_boundary.py`·`Tools/week_ratio.py`, fixture 4경우 자기 시험 통과, Tools/README.md 등록. 독립 반박: 꺾쇠 인클루드(`#include <...>`) 거짓 음성 — 수정됨.
- 기록 추가: FD-01 반박 수정: 경계 검사 도구가 꺾쇠 #include 도 따옴표와 같은 규칙으로 해석(fixture angle 추가, 자체 시험 5경우 통과), docstring·README 를 실제 동작에 맞춤. week_ratio.py 는 머지 커밋 기본 제외(--include-merges)·무변경 커밋은 기타로 세어 머리줄 수=표 합. 현재 트리에는 Foundation 폴더가 없어 실제 저장소 검사는 '기반 코드 없음'. 독립 반박 R2 합격.
- 목표·완료 조건: `Docs/GameplayFoundation_Plan.md` 4절 FD-01.

### FD-05 사건 기록기·기반 시험 월드
- 상태: doing
- 담당: claude/2026-11-18
- 기록: FD-05(사건 기록기 부분) 완료 2026-11-18 claude: 기반 UTDGameplayRecorderSubsystem(Foundation/Recording, 버퍼 64줄·종료·Deinitialize 때 Saved/Telemetry/<시각>_<맵>.jsonl, TD.Recorder.Enabled) + TDGame 연결 UTDGameplayRecorderBridge(Framework, 피해·회피·사망·조우 배너·조우 끝·페이즈 6채널, 월드 시간) + Tools/summarize_play_log.py(결정론 요약, --json). 시험 3개(TDGame.Foundation.Recorder.*) 통과, TDGame.Combat 134/134 유지, 경계 검사 위반 0, 요약 두 번 동일. 남은 FD-05: 기반 시험 월드·자동 전투원 기준선·데이터 검증은 다음 회차. 연결 서브시스템은 게임 인스턴스가 CreateWorld 안에서 아직 없어 월드 종류만 보고 만들고 게임 인스턴스는 BeginPlay 때 확인(교훈 후보).
- 결정(2026-11-18 claude, 반박 R2가 지적한 규격 편차 — 뒤집을 수 있음): ① src·dst는 SimulationId가 생기는 M0-03 전까지 액터 이름 문자열, 그 뒤 정수로 바꾼다. ② ability는 지금 피해 전달 방식 이름, 피해 정의 ID로 바꾸는 일은 자동 전투원과 함께. ③ 머리줄의 build·dh(데이터 해시)는 자동 전투원·기준선 때 더하고, difficulty·equipment는 그 기능이 생길 때, persona_guess는 분석 도구가 계산한다. ④ 메시지 9종 중 휘두르기(매 타 고빈도)·카라반 파괴(한 판에 없음)·보스 체력 막대(피해와 중복)는 일부러 뺐다. 규격의 물약·플레이어 입력 기록은 GP-07·자동 전투원 때. ⑤ 요약 도구는 규격의 `summarize_batch.py --live`가 아직 없어 `Tools/summarize_play_log.py`로 따로 두고, 시뮬 요약 도구가 생기면 합친다.
- 남은 것: 기반 시험 월드(M0-05와 맞춤), 자동 전투원 모드와 변경 전 기준선, 데이터 검증.
- 기록 추가: W2 요약 도구 UTF-8 견고성: summarize_play_log.py 바이트 단위 해독(깨진 줄 broken 집계·건너뜀), --self-test 추가(fixture 5줄, broken 2, 두 번 실행 동일, PASS), README 해당 줄 갱신. 빌드 없음, 커밋 없음. 재확인 R3 합격.
- 목표·완료 조건: `Docs/GameplayFoundation_Plan.md` 4절 FD-05(기록 규격은 `Docs/MonsterAI_CombatSim/06-beyond-the-ask.md` 35·160-166줄).

### GF-02 구르기·공격 중 주문 시전
- 상태: done
- 담당: claude/2026-11-18
- 기록: GF-02: 완료 — 주문(TDDamageGameplayAbility)이 구르기 중에는 막히고, 공격 중에는 스킬 취소 창이 열렸을 때만 시전됩니다. 시험 TDGame.Combat.GAS.SpellCastBlockedWhileRollingOrAttacking 을 빨강에서 초록으로 확인했고, TDGame.Combat 135/135 통과(새 실패 0). 취소 창이 열린 경우의 시험은 없습니다(동료 캐릭터에 스킬 컴포넌트 없음). 근거는 Saved/AgentOps/20261118/G-combat-tests.json 입니다.
- 반박 R2 고칠 것: 스킬 컴포넌트가 있는 캐릭터로 '공격 중·취소 창 닫힘/열림'을 독립 시험하고 빨강을 다시 증명 — 해결: 2026-11-18 claude: GF-02 시험 보강 완료 — Source/TDGame/Combat/Tests/TDSpellActivationBlockTests.cpp 를 기본 플레이어(ATDGameCharacter, 스킬 컴포넌트 있음)로 4건 분리(TDGame.Combat.GAS.SpellCast.IdlePlayerCasts·RollingPlayerBlocked·AttackingClosedWindowBlocked·AttackingOpenWindowCasts, 경우마다 새 월드·새 시전자). 빨강: TDDamageGameplayAbility.cpp 수정(구르기 차단 태그·State_Attacking 취소 창 검사)을 되돌리면 구르기·창 닫힘 2건만 실패, 대기·창 열림 2건 통과(W1-red). 복구는 백업과 바이트 대조 동일, 초록 TDGame.Combat 138/138(W1-green). 증거 Saved/AgentOps/20261118/stage1-wave3/. 동료 캐릭터 경로 시험은 제품에서 일어나지 않는 분기라 제거. 재확인 R3(Opus) 합격, 지적 1건(시험 파일 동기 로드 3곳이 가드 밖)은 세션이 `#if WITH_DEV_AUTOMATION_TESTS` 가드로 고침 → check_sync_loads new=0, TDGame.Combat 138/138(Saved/AgentOps/20261118/stage1-wave3/FIX-combat.log).

### GP-02 실행하면 마을에서 시작
- 상태: doing
- 담당: claude/2026-11-18
- 기록: GP-02: 코드 완료, 사람 관문 대기 — GameDefaultMap·EditorStartupMap을 LV_DarkFantasy_OpenWorld 로 바꿨습니다(.umap 존재 확인). 디버그 안내와 시전자 레벨 문구는 콘솔 변수 TD.Dev.ShowOverlay(기본 0) 뒤로 옮겼습니다. 두 문구는 분석서가 가리킨 TDGameCharacter.cpp 가 아니라 TDGamePlayerController.cpp 527줄·1015줄에 있었습니다. -game 실행으로 조작 가능까지 시간과 디버그 문구 0을 보는 것은 미실시입니다.
- 기록 추가: GP-02 실제 실행 확인: -game -windowed 120초 실행, 맵 LV_DarkFantasy_OpenWorld 로드(LoadMap 시작 시각이 프로세스 시작 14초 뒤, 월드 구동 시작 15초 뒤, 스트리밍 생성 0.94초), 크래시·Fatal 없음, Error는 에디터 시작 때 LogPython의 ToolsetDefinition 속성 없음 2건뿐(게임플레이 무관), 폰 생성·컨트롤러 부착 로그와 TD.Dev.ShowOverlay 줄은 없음(화면 확인은 사람 관문), SyncLoad 경고 엔진 에셋 위주. 로그 Saved/AgentOps/20261118/stage1-wave3/GP02-game.log
- 남은 것: 사람 관문에서 화면(디버그 문구 0·마을 위치·조작 가능까지 시간)과 폰·컨트롤러 부착(로그에 해당 줄 없음) 확인.

### GP-11 Esc 일시정지·종료
- 상태: doing
- 담당: claude/2026-11-18
- 목표·완료 조건: 분석서 3절 GP-11 행(버튼 3개 계속·저장 후 종료·새로 시작, 일시정지 중 두뇌·타이머 정지, 재개 튐 0, 종료 시 저장). 메뉴는 순수 C++ Slate.
- 진행: 워크플로 `td-stage1-gp11`(2026-11-18, 주간 사용량 98%에서 착수). 끊기면 `git diff`로 Framework/의 일시정지 메뉴 파일·TDGamePlayerController.cpp 의 Escape 줄·Tests/ 를 보고 이어 가거나 되돌린다.
- 기록: 2026-11-18 claude: GP-11 Esc 일시정지·종료 메뉴 구현 — UTDPauseMenuSubsystem(월드)+STDPauseMenu(Slate 3버튼), Esc BindKey(bExecuteWhenPaused) 토글, 저장 후 종료(슬롯 TDPauseMenuAutoSave, 저장 실패 시 종료 안 함)·새로 시작(ClearAllState 후 맵 다시 열기). 몬스터 사고·NPC·타이머는 이미 일시정지를 따라 수정 없음(코드 읽기). 시험 PauseMenu 3/3, Combat 138/138, check_sync_loads 새 위반 0, check_foundation_boundary 0.
- 반박 R4(Opus) 꼭 고칠 것: 메뉴를 닫을 때 FInputModeGameOnly 가 마우스 잠금·고정밀 입력을 켜 커서·조준·클릭 이동이 바뀜 → 고침: 열 때 뷰포트 입력 상태(캡처·잠금·커서 숨김·입력 무시)를 저장하고 닫을 때 되돌린 뒤 게임 뷰포트에 포커스(SaveViewportInputState·RestoreViewportInputState), 빌드·시험 141/141·검사 통과. 이 경로는 시험에 뷰포트가 없어 실행되지 않음 — -game 확인 필요.
- 남은 것: `-game`에서 Esc로 열고 닫은 뒤 커서·조준·클릭 이동이 열기 전과 같은지와 버튼 모양·클릭(에디터 PIE에서는 Esc가 PIE 중지라 `-game`으로, L-gameplay-03). 몬스터 사고 일시정지 시험(c)은 코드 읽기로만 확인. '저장 후 종료' 슬롯은 이어하기(FD-04) 전까지 쓰기만 된다.

## 4. 이어 하기

- 2026-11-18: 주간 사용량 95%에서 시작했다. 워크플로 `td-stage01-kickoff`가 M0-01 → M0-02 → M0-07(코드), 색인·GP-01 초안, FD-01 도구, 반박 1회를 돌린다. 끊기면 `git status`·`git diff`로 남은 변경을 보고, `Saved/AgentOps/20261118/stage01/`의 보고서로 이어 간다(작업 전 트리는 깨끗했다, HEAD 495aa195).
- 다음: FD-05 사건 기록기(기반 시험 월드는 M0-05와 맞춤) → GP-02(시작 맵)·GP-03(공터 조우)·GP-11(Esc)·GF-02 → 12일차 사람 손 관문.
- 2026-11-18 11:24: 워크플로 1 끝 — M0-01 done(Combat 132), M0-02 done(134/134), M0-07·FD-01은 반박 고칠 것 각 1. 워크플로 2(`td-stage1-wave2`): 두 수정 → FD-05 기록기 → GF-02·GP-02 → 반박. 사용량 96%에서 착수.
- 2026-11-18 12:03: 워크플로 2 끝 — M0-07·FD-01 done(반박 R2 합격), FD-05 기록기 끝(결정 ①~⑤), GF-02·GP-02 코드 끝. 사용량 98%. 워크플로 3(`td-stage1-wave3`): GF-02 시험 보강·요약 도구 UTF-8·GP-02 `-game` 확인 → 좁은 재확인. 다음 회차: GP-03(공터 조우, 에디터 필요)·GP-11(Esc)·자동 전투원.
- 2026-11-18 12:26: 워크플로 3 끝 — GF-02 done(플레이어 시험 4, 빨강 재증명, Combat 138/138), GP-02 `-game` 확인(맵 로드·크래시 없음, 화면·폰은 사람 관문), 요약 도구 보강. 사용량 약 99%. 다음 회차(사용량 리셋 뒤): GP-03 공터 조우(에디터 필요, 맵 저장 전 사용자 알림), GP-11 Esc 메뉴, FD-05 남은 것, 단계 1 사람 손 관문. 전부 미커밋.
- 2026-11-18 12:28: GP-11 착수(워크플로 `td-stage1-gp11`, Sonnet 구현 + Opus 좁은 반박). 결과가 기록되지 않았으면 할당량이 바닥난 것 — 위 GP-11 블록의 '진행' 줄대로 확인.
- 2026-11-18 12:59: GP-11 끝(반박 수정 포함) — 오늘 작업 전체를 사용자 지시("끝나면 커밋")로 커밋. 다음 회차: GP-03 공터 조우(에디터), FD-05 나머지, 단계 1 사람 손 관문.

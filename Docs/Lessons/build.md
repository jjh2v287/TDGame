# Lessons — build (빌드·컴파일)

[← 인덱스로](../AgentCollaboration_Plan.md)
종류: 교훈 · 형식은 `Docs/AgentRules.md` 3절 A. 색인: `rg -n "^### L-" --glob '*.md' Docs/Lessons`

### L-build-01 새 C++ 모듈·클래스는 라이브 코딩이 안 된다
- 증상: 새 모듈·새 UCLASS를 추가한 뒤 라이브 코딩 컴파일이 성공해도 에디터에 반영되지 않거나 실패한다.
- 원인: 라이브 코딩은 기존 모듈 안의 함수 본문 변경만 패치한다.
- 해결: `python Tools/ue_editor.py restart` (에디터 닫고 Build.bat 후 재기동).
- 범위: UE 5.8, Source/TDGame·TDGameEditor·TDWorldGen
- 증거: Tools/README.md 5절(이관 전 기록) — 미검증(증거 파일 없음)
- 날짜·상태: 2026-09-18 active (원 기록 2026-09-12 이전)
- 발견: gemini/claude (이관: claude)

### L-build-02 컴파일 오류 원인은 로그의 들여쓴 다음 줄에 있다
- 증상: 머티리얼·파이썬 스크립트 오류가 났는데 마지막 줄만 보면 원인이 안 보인다.
- 해결: `Saved/Logs/TDGame.log`에서 `LogMaterial`/`LogPython` 줄 바로 다음의 들여쓴 줄을 읽는다.
- 범위: UE 5.8 에디터 로그
- 증거: 미검증
- 날짜·상태: 2026-09-18 active
- 발견: gemini (이관: claude)

### L-build-03 새 .cpp를 추가하자 다른 파일의 익명 네임스페이스 이름이 겹쳐 빌드가 깨진다(통합 빌드 묶음 변경)
- 증상: 손대지 않은 `TDDungeonLayoutSolver.cpp`에서 `PickWeightedIndex`·`ShuffleArray`·`FTDPlacementCandidate` 재정의 오류. 같은 이름이 `TDDungeonFlowGenerator.cpp`·`TDWorldGraphGenerator.cpp`의 익명 네임스페이스에도 있다.
- 원인: UBT 통합 빌드(unity build)는 여러 .cpp를 한 번역 단위로 묶는다. 모듈에 파일이 추가되면 묶음 구성이 바뀌어, 따로 컴파일되던 파일이 같은 묶음에 들어가고 익명 네임스페이스 이름이 충돌한다.
- 해결: 충돌한 쪽 이름을 파일 고유 이름으로 바꾼다(동작 변경 없음). 새 익명 네임스페이스 함수·구조체 이름에는 파일 역할을 넣는다(`PickLayoutWeightedIndex`). 패치 예: `Saved/AgentOps/20261013/B1_unity_collision_fix.patch`.
- 범위: UE 5.8 UBT, 모든 모듈(특히 TDWorldGen)
- 증거: `Saved/AgentOps/20261013/B1_build_1.log`(실패)·`B1_build_5.log`(패치 후 성공)
- 날짜·상태: 2026-10-14 active
- 발견: claude(P3-13 B1 하위 에이전트)

### L-build-04 Git Bash의 `cmd //c build.cmd`가 오류 없이 재빌드를 건너뛴다
- 증상: 독립 C++ 벤치를 Git Bash에서 `cmd //c build.cmd`로 빌드하자 오류 표시 없이 끝났지만 실행 파일 수정 시각이 바뀌지 않았고, 수정 검증이 이전 바이너리로 실행됐다.
- 해결: vcvars를 부르는 배치 빌드는 PowerShell에서 `cmd /c <배치>`로 실행하고, 빌드 뒤 산출물 수정 시각을 확인한다.
- 범위: Git Bash에서 MSVC 배치(vcvars64.bat 호출) 실행. 언리얼 Build.bat 경로는 확인하지 않았다.
- 증거: 미검증(하위 에이전트 1회 관찰) — `Docs/MonsterAI_CombatSim/measurements/elongated-avoidance/bench-results.md` 7절 14
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-build-05 다른 세션의 헤드리스 시험(UnrealEditor-Cmd)이 DLL을 잡고 있으면 빌드 링크가 LNK1104로 실패한다
- 증상: `python Tools/ue_editor.py restart` → `UbaSessionServer - ERROR opening file ...\Binaries\Win64\UnrealEditor-TDGameEditor.dll for write after retrying for 20 seconds (... being used by another process. - ...\UnrealEditor-Cmd.exe)` → `LINK : fatal error LNK1104` → `Result: Failed (OtherCompilationError)`.
- 원인: 에디터를 닫아도 다른 에이전트 세션이 돌리는 헤드리스 자동화 시험(`UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests ..."`)이 같은 프로젝트 DLL을 불러 잠근다.
- 해결: 빌드 전에 `tasklist //FI "IMAGENAME eq UnrealEditor-Cmd.exe"`(Git Bash) 또는 `tasklist /FI "IMAGENAME eq UnrealEditor-Cmd.exe"`(PowerShell)로 확인하고, 있으면 그 세션과 순서를 맞춘 뒤 빌드한다. 남의 프로세스를 종료하지 않는다.
- 범위: 여러 에이전트 세션이 같은 작업 트리에서 빌드·헤드리스 시험을 돌릴 때
- 증거: 2026-10-14 P3-17 빌드 로그 `Saved/Logs/TDGame_Build.log`(몬스터 AI 세션 시험 pid 15472와 겹침)
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-build-06 에디터가 켜져 있어도 게임 타깃 빌드로 C++ 컴파일은 확인할 수 있다
- 증상: 다른 세션·사용자가 에디터를 쓰고 있어 `Build.bat TDGameEditor ...`(에디터 DLL 링크)와 헤드리스 시험을 못 돌린다. 새 .cpp 추가는 라이브 코딩도 안 된다(L-build-01).
- 해결: PowerShell에서 `cmd /c "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TDGame Win64 Development -Project="C:\Project\TDGame\TDGame.uproject" -WaitMutex -NoHotReload`. 게임 타깃은 `TDGame.exe`만 만들고 에디터 DLL을 건드리지 않으며, Development라 자동화 시험 코드도 같이 컴파일된다. 통합 빌드 이름 충돌은 `-DisableAdaptiveUnity`로 한 번 더 본다(L-build-03). 시험 실행과 에디터 타깃 링크는 에디터가 비었을 때 따로 한다.
- 범위: TDGame 모듈 C++ 변경의 컴파일 확인. 에디터 전용 모듈(TDGameEditor)은 확인 못 함.
- 증거: 2026-10-14 M3-02 `Saved/AgentOps/20261014/m3-02-build-3-fix.log`·`m3-02-build-4-fix-unity.log`(Result: Succeeded, 에디터 pid 39400 실행 중)
- 날짜·상태: 2026-10-14 active
- 발견: claude

### L-build-07 C++ 생성자에서 기본값으로 넣은 소프트 에셋 경로는 쿠킹되지 않아 패키지 게임에서 빠진다
- 증상: 쿠킹한 게임 로그 `LogStreaming: Warning: LoadPackage: SkipPackage: /Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run (0xE2A393FE2948EC5B) - The package to load does not exist on disk or in the loader`(MM_Jump·MM_Fall_Loop·MM_Land도 같음). 에디터·PIE에서는 정상이라 드러나지 않는다.
- 원인: 쿠커는 패키지 직렬화·텍스트 가져오기 때 만나는 소프트 경로만 모은다. 네이티브 생성자(CDO)의 `TSoftObjectPtr` 기본값과 C++ 문자열 경로는 어느 패키지에도 저장되지 않아 발견되지 않는다. `ATDGameCharacter` 생성자의 플레이어 이동·점프 애니 기본값이 이 경우다(비동기 전환 전 `LoadSynchronous` 때도 같았다).
- 해결: 그 에셋을 다른 쿠킹 대상 에셋(BP 기본값·데이터 에셋)이 참조하게 하거나, 폴더를 `Config/DefaultGame.ini`의 `+DirectoriesToAlwaysCook`에 넣거나, `UDeveloperSettings` 소프트 필드(설정 ini 경로는 쿠킹됨)로 옮긴다. 확인은 `python Tools/cook_single_map.py` 뒤 게임 로그에서 `SkipPackage`를 찾는다. 마네킹 애니 4개는 `+DirectoriesToAlwaysCook=(Path="/Game/Characters/Mannequins/Anims/Unarmed")` 한 줄로 고쳤다(블렌드 스페이스가 그 폴더의 걷기·조깅 16개와 MM_Idle을 참조하므로 불필요 쿠킹은 MM_Attack_01 하나). 고치기 전에는 폴더 안 에셋이 다른 에셋의 하드 의존인지 문자열 표에서 확인한다. 기본 근접 공격 `AS_TD_Player_SwordAttack01`(`TDGamePlayerController.cpp:38`, `TDPlayMeleeAction` 호출 때만 요청이라 맵 실행 로그에 안 드러남)도 `Anims/Sword` 폴더 한 줄로 고쳤다(2026-10-18). 코드 안 경로 전체는 `python Tools/check_cook_coverage.py`로 판정한다(요청 때만 여는 경로까지 잡는다).
- 범위: 런타임 C++가 코드 안 소프트 경로·문자열 경로로 여는 모든 에셋(OP-33).
- 증거: 2026-10-17 `Saved/AgentOps/20261017/async-load/cook-run-game.log:930-933`, 엔진 `SoftObjectPath.cpp`(직렬화 경로에서만 수집, `engine-research.md` 5절). 수정 뒤 `Saved/AgentOps/20261017/anim-cook/game.log` SkipPackage 0·경고 13→9, 쿠킹 `AssetRegistry.bin`에 4개 존재(`anim-cook/judge.txt`). 근접 공격: `Saved/AgentOps/20261018/cook-path-audit/exec_melee-default.log`(쿠킹 게임 `-ExecCmds=TDPlayMeleeAction` 재생, SkipPackage 0)
- 날짜·상태: 2026-10-17 active
- 발견: claude

### L-build-08 다른 세션의 UBT·UAT가 돌면 `cook_single_map.py`가 몇 초 만에 뮤텍스 충돌로 실패한다
- 증상: `A conflicting instance of Global\UnrealBuildTool_Mutex_096b8b11a8779fc55e9da244f4a8ecf62868c83d is already running.` → `Result: Failed (ConflictingInstance)` → `AutomationTool exiting with ExitCode=10 (Error_SDKNotFound)`. 또는 `Unhandled exception: System.Exception: A conflicting instance of AutomationTool is already running.` → `ExitCode=1 (Error_Unknown)`. 종료 코드 이름(SDKNotFound)은 원인과 무관하다.
- 원인: UBT와 AutomationTool은 기계 전체에서 한 번에 하나만 돈다. 같은 작업 트리의 다른 세션 빌드(`Build.bat`·게임 타깃), 에디터가 띄운 Turnkey `VerifySdk`, 또는 같은 도구의 중복 실행이 뮤텍스를 잡고 있었다.
- 해결: 실행 전에 `Get-CimInstance Win32_Process`로 `UnrealEditor*`·`cl.exe`·`link.exe`·명령줄에 `UnrealBuildTool|AutomationTool`이 있는 dotnet을 확인한다. 공유 노트에 사용 계획·시작·끝을 적고 다른 세션과 차례를 정한다(쿠킹은 에디터가 꺼져 있어야 하고 약 2분). 남의 프로세스는 종료하지 않는다.
- 범위: 여러 에이전트 세션이 같은 기계에서 빌드·쿠킹·헤드리스를 돌릴 때(L-build-05와 같은 계열)
- 증거: 2026-10-17 `Saved/Logs/cook_anim-cook.log` 23:29 1차 실패, `C:/Users/jjh/AppData/Local/UnrealEngine/Programs/AutomationTool/Saved/Logs/ErrorLog.txt` 23:47:50
- 날짜·상태: 2026-10-17 active
- 발견: claude

### L-build-09 새 .cpp를 넣었는데 `BUILD Succeeded`인데도 그 파일이 컴파일되지 않아 새 시험이 0건으로 잡힌다
- 증상: `build_and_test.py`가 `BUILD Succeeded` 뒤 `TESTS 0/? passed`, 엔진 로그 `LogAutomationCommandLine: Error: No automation tests matched 'TDGame.MonsterAI.Pack'`(새 시험 파일 `TDPackBehaviorTests.cpp`를 막 복사한 직후).
- 원인: UBT가 캐시한 `Intermediate/Build/Win64/x64/TDGameEditor/Development/Makefile.bin`을 "유효"로 판정해 소스 목록을 다시 훑지 않았다. 다른 세션이 같은 트리를 방금 빌드해 makefile이 최신으로 보이면 새로 추가된 .cpp가 빠진다(헤더·기존 .cpp 수정은 정상 반영).
- 해결: 새 .cpp를 추가·삭제한 빌드 전에 그 `Makefile.bin`을 지운다(`rm -f Intermediate/Build/Win64/x64/TDGameEditor/Development/Makefile.bin`). 빌드 로그에 `Creating makefile for TDGameEditor (no existing makefile)`가 나오면 다시 훑은 것이다. 새 시험은 첫 실행에서 `found_count`가 기대 수인지 확인한다.
- 범위: 공용 작업 트리에 새 C++ 파일을 넣는 모든 빌드(통합 스크립트로 파일을 복사하는 사본 작업 포함)
- 증거: 2026-10-18 `Saved/AgentOps/20261018/pack-red-run1.out`(0건)·`pack-red-tests-backup-2026.10.18-06.15.43.log:2234` → Makefile.bin 삭제 뒤 `pack-red-build.log`(no existing makefile)·`pack-red-tests.json` found_count 17
- 날짜·상태: 2026-10-18 active
- 발견: claude

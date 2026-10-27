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
- 변형(2026-10-20 claude FX-8b): 파일 범위 `using namespace X;`(줄 첫머리)가 있으면 묶음 뒤쪽 파일의 지역 변수가 X의 이름을 가려 `error C4459: 'StepSeconds' ... 전역 선언을 숨깁니다`(/we4459). 손대지 않은 `TDRealmLiftDeck.cpp`·`TDRealmSubsystem.cpp`에서 났고 원인은 `TDMidBossTests.cpp:395`. 해결은 같다(가려진 이름을 파일 고유 이름 `MidBossStepSeconds`로). 찾기: `rg -n "^using namespace" Source --glob "*.cpp"`. 증거 `Saved/AgentOps/20261020/fx8b-pre-build.log`
- 변형(2026-10-21 claude FX-8c 11차): 손대지 않은 `TDFluidLiveComponent.cpp:25`·`TDFluidLiveActor.cpp:38`의 익명 네임스페이스 상수 `MaximumShadowCapsuleCount`가 게임 타깃 `-DisableAdaptiveUnity` 빌드에서 C2374 재정의. 액터 쪽을 `MaximumActorShadowCapsuleCount`로 바꿈. 빌드 전 검사 `python -B Tools/check_unity_collisions.py`(모듈별 익명 네임스페이스 같은 이름이면 종료 1, 이 충돌·합성 3종 검출 확인)
- 날짜·상태: 2026-10-14 active
- 발견: claude(P3-13 B1 하위 에이전트)
- 재발(2026-10-18, TDGameEditor): FluidFX 묶음 C가 에디터 모듈에 .cpp 2개를 더하자 `TDWorldStatePieTests.cpp`(TDPieTestDungeonId 등)·`TDWorldBaker.cpp`(TagGen)·`TDSequencerAnimationTools.cpp`(Fail) 이름이 겹쳐 다른 세션 빌드까지 깨졌다. 빌더가 `bUseUnity = false`로 모듈 전체를 끈 것은 빌드 시간 비용이라 되돌리고 같은 해결(파일 고유 이름)로 고쳤다. 새 파일을 더한 빌더는 모듈 전체 끄기 대신 이 해결을 쓴다.

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

### L-build-10 게임 타깃(`Build.bat TDGame`)으로 미리 컴파일이 통과해도 에디터 DLL 링크가 `FBodyInstanceCore::ShouldInstanceSimulatingPhysics` LNK2019로 실패한다
- 증상: `UnrealEditor-TDGame.dll : fatal error LNK1120`, `__imp_?ShouldInstanceSimulatingPhysics@FBodyInstanceCore@@QEBA_NXZ ... 확인할 수 없는 외부 기호`(호출처 `FBodyInstance::IsInstanceSimulatingPhysics()` 인라인). 같은 코드가 게임 타깃 빌드(368초)는 `Result: Succeeded`
- 원인: `IsInstanceSimulatingPhysics`는 헤더 인라인이라 PhysicsCore 모듈의 `FBodyInstanceCore` 심볼을 직접 부른다. TDGame 모듈은 PhysicsCore에 의존하지 않고, 게임 타깃은 단일 실행 파일로 링크돼 문제가 드러나지 않는다
- 해결: 래그돌 전신 시뮬레이션처럼 확인이 불필요하면 호출을 뺀다(M3-38 `ComputeRagdollMeanSpeed`). 꼭 필요하면 `TDGame.Build.cs`에 "PhysicsCore" 의존 추가. 게임 타깃 사전 컴파일은 컴파일 오류만 잡고 모듈 간 링크 오류는 못 잡는다(L-build-06 보완)
- 범위: UE 5.8 에디터(모듈) 빌드, PhysicsCore 인라인 API를 쓰는 TDGame 코드
- 증거: 2026-10-19 `Saved/AgentOps/20261019/m338-b1-build.log`(실패), `Saved/AgentOps/20261018/ragdoll-jitter/game-build-1.log`(게임 타깃 성공)
- 날짜·상태: 2026-10-19 active
- 후속(2026-10-19 M3-40): 같은 유형이 두 번 더 나왔다. `NewObject<UPhysicalMaterial>`(PhysicsCore, `TDGame.Build.cs` Private에 "PhysicsCore" 추가로 해결)과 시험의 `FlushRenderingCommands`(RenderCore, `LNK2019 __imp_?FlushRenderingCommands`, 호출 제거로 해결 — RenderCore 의존은 FluidFX가 따로 추가 예정). 엔진 모듈 함수를 새로 부르면 소속 모듈을 먼저 확인한다.
- 발견: claude

### L-build-11 자동화 시험 필터는 부분 문자열 일치라 `TDGame.FluidFX` 가 `TDGame.FluidFXRhi.*` 도 고르고, -NullRHI 에서 건너뛴 RHI 시험이 통과로 집계된다
- 증상: `build_and_test.py --filter TDGame.FluidFX`(헤드리스 `-NullRHI`)가 GPU 가 필요한 `TDGame.FluidFXRhi.*` 시험까지 실행하고, 시험 안의 '렌더러 없음 → 경고 후 통과' 분기 때문에 성공으로 센다. 시험 수가 부풀고 합격 판정에 건너뜀이 섞인다
- 원인: `RunTests` 인수에 `^`·`StartsWith:` 접두 기호가 없으면 `FAutomatedTestFilter` 가 `Contains` 비교(엔진 `AutomationCommandline.cpp:211-227`, `AutomationControllerSettings.h` PassesFilter). '접두사 일치'라는 가정이 틀렸다
- 해결: GPU 가 필요한 시험에 `EAutomationTestFlags::NonNullRHI` 를 붙인다 — 명령줄에 `-nullrhi` 가 있으면 `GetValidTestNames` 가 목록에서 뺀다(`AutomationTest.cpp:840-866`, 예 `ExternalToolTest.cpp:83`). 렌더러가 없을 때는 경고 대신 오류로 실패시킨다. 실제 GPU 시험은 `python Tools/build_and_test.py --rhi offscreen --filter TDGame.FluidFXRhi` 로 돌리고 로그의 RUN 줄 수로 실행을 확인한다. 정적 검사 `Tools/FluidFX/check_fluid_gpu_shaders.py` 가 플래그 누락을 잡는다
- 범위: UE 5.8 자동화 시험 명령줄, `Tools/build_and_test.py`
- 증거: FX-4a 묶음 A 반박·수정(워크플로 wf_49547c3c-a57), 엔진 소스 대조. 실제 실행 확인은 모듈 등록 뒤(미검증)
- 날짜·상태: 2026-10-19 active
- 발견: claude

### L-build-12 PowerShell `Copy-Item`으로 백업에서 소스를 되돌리면 백업의 예전 수정 시각이 따라와 UBT가 다시 컴파일하지 않고, 변이·실험 코드가 든 바이너리로 시험이 돈다
- 증상: 변이(이득 1.02)를 넣어 빌드·시험한 뒤 `Copy-Item <백업> Source/...cpp -Force`로 원본 해시를 되돌리고 `build_and_test.py`를 다시 돌렸는데 `BUILD Succeeded` 뒤에도 시험 로그 값이 변이 값 그대로였다(2026-10-23 D-53 시험, `cycle_overshoot=[123.37, 14609.1, 1694342.3]`).
- 원인: Copy-Item은 원본 파일의 LastWriteTime을 보존한다. 백업은 변이 전에 만들었으므로 되돌린 소스가 변이 빌드의 목적 파일보다 오래돼 UBT가 그 번역 단위를 최신으로 본다.
- 해결: 되돌린 뒤 `(Get-Item <파일>).LastWriteTime = Get-Date`로 시각을 갱신하고 다시 빌드한다(또는 `git apply -R`·편집으로 되돌려 시각이 새로 찍히게). 되돌림 뒤 시험 로그에서 변이 표지 값이 사라졌는지 확인한다.
- 범위: 변이 시험·빨강/초록 전환·사본 통합에서 파일 복사로 소스를 되돌리는 모든 경우(Bash `cp -p`도 같음, `cp`만은 새 시각)
- 증거: `Saved/AgentOps/20261023/w1gate-d53-green-tests.log`(변이 값) 대 `w1gate-d53-green2-tests.log`(시각 갱신 뒤 정상)
- 날짜·상태: 2026-10-23 active
- 발견: claude(W-1 종결 관문 D-53 시험)

### L-build-13 `TDGame.FluidFX.Perf.TransmittanceCpuCost`(CPU 투과율 96격자 ≤0.3ms 중앙값)는 평소 0.26ms라 여유가 13%뿐이고, 원격 데스크톱 호스트 같은 배경 부하가 있으면 0.31~0.39ms로 실패한다
- 증상: 코드 변경과 무관하게 `Expected 'CPU transmittance for a 96 grid stays under 0.3 ms' to be true`(TDFluidTransmittanceTests.cpp:194). 2026-10-22(재실행 통과)·2026-10-23(연속 2회 실패, median_ms 0.3893·0.3554). 같은 날 앞선 8회는 0.258~0.267.
- 원인: 시간 단언이 실행 기계 부하에 민감하다. 2026-10-23 실패 때 `remoting_host`(원격 데스크톱)가 CPU 상위였다. 투과율 코드는 바뀌지 않았다.
- 해결: 실패하면 먼저 `Get-Process | Sort-Object CPU -Descending`으로 배경 부하를 보고, 로그의 `TDFluidTransmittancePerf: ... median_ms=` 값을 같은 날 이전 실행과 비교한다. 회귀 판정은 다른 시험 결과와 이 값의 추세로 한다. 시험 자체를 덜 민감하게(최솟값·하위 백분위, 또는 한도 재설정) 바꾸는 것은 FluidFX 후속 결정 후보.
- 범위: FluidFX 헤드리스 시간 단언 시험 전반(TDGame.FluidFX.Perf.*)
- 증거: `Saved/AgentOps/20261023/w1gate-stage2-final-tests.log`·`w1gate-perf-rerun-tests.log`(실패) 대 `w1gate-asynctests-tests.log`(0.2603 통과)
- 날짜·상태: 2026-10-23 active(두 번째 발생)
- 발견: claude(W-1 관문 2단계 회귀 시험)

### L-build-14 Game 타깃은 `/fp:fast`, Editor 타깃은 `/fp:precise`로 컴파일되어 같은 결정론 생성 코드의 결과 비트가 다르다 — 헤드리스(Editor) 시험은 출시 빌드 차이를 잡지 못한다
- 증상: 정수 해시 노이즈 생성기를 cl 19.38로 UE 5.8 Game·Editor 플래그 조합으로 빌드하자 Game 기본은 높이 표본 46~48%가 Python float64와 달랐다(최대 1.85e-13m), `/fp:fast`+AVX2는 82~85%. Editor 기본·precise·strict는 0/1,148,488.
- 원인: VCToolChain.cs:1313-1335 — Editor·Program 타깃과 V7은 Precise, 그 밖은 `/fp:fast`(재결합·역수 치환, AVX2에서 FMA 융합). 프로젝트에 FPSemantics 지정 0건. `/fp:precise`로도 CRT 초월 함수의 FMA3 경로 차이(sin 0.94%, tan 2.6%)는 막지 못한다.
- 해결: 생성 결과가 저장 차분·골든 해시의 기준이 되는 모듈에 `FPSemantics = FPSemanticsMode.Precise`(ModuleRules.cs:784-790, 실행 비용 잡음 수준), 정체성(존재·칸·ID)은 정수 해시, 저장값은 양자화한 절대값, 생성 경로에서 CRT 초월 함수·`*Est`·엔진 비인라인 수학 금지. 1mm 양자화 뒤 불일치는 0건이었지만 확률(표본당 약 1e-11)이지 보장이 아니다.
- 범위: Win64 MSVC, 결정론이 필요한 모든 C++(월드 생성, 전투 시뮬 골든 해시 — 04-combat-simulator.md의 '부동소수점 모드 미확인'에 해당). UE 빌드 결과물(PCH·인라인 맥락)은 단독 cl로 대신 측정했다.
- 증거: `Docs/InfiniteWorld/measurements/fpdet/02-fp-determinism.md`·`compare.json`·`asm_stats.json`, 재현 `Tools/InfiniteWorld/fpdet/run_fpdet.py`
- 날짜·상태: 2026-10-23 active
- 발견: claude(무한 세계 가능성 검토)

### L-build-15 같은 설치판 엔진의 다른 편집기가 라이브 코딩 중이면, 별도 프로젝트의 편집기 대상 빌드도 "Unable to build while Live Coding is active"로 실패한다
- 증상: TDGame 에디터가 떠 있는 동안 분리 벤치 프로젝트 `IWBenchEditor` Build.bat이 `Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11 ...`로 종료.
- 원인: UBT의 라이브 코딩 검사(HotReload.cs:271-278)가 프로젝트가 아니라 같은 엔진 설치의 실행 중 편집기를 본다.
- 해결: 빌드 명령에 `-NoHotReloadFromIDE`(BuildConfiguration.cs:160)를 붙인다. 다른 세션 UBT와는 `-WaitMutex`로 순서를 기다린다. TDGame 바이너리·빌드 보류(build_hold)와 무관하게 별도 프로젝트를 빌드할 수 있다.
- 범위: UE 5.8 런처 설치판, 여러 세션이 한 엔진을 쓰는 기계
- 증거: `Saved/AgentOps/20261023/infinite-world/bench/logs/build1.log`(실패)·`build3.log`(성공), 실행 스크립트 `Tools/InfiniteWorld/bench/run_bench.ps1`
- 날짜·상태: 2026-10-23 active
- 발견: claude(무한 세계 가능성 검토)

### L-build-16 설치판(런처) UE 5.8에는 Win64 Test 게임 구성이 없어 Test 빌드·패키지를 만들 수 없다 — 성능 '최종 판정 Test 빌드' 규칙은 이 기계에서 실행 불가
- 증상: 로드맵·판정 규칙(RM:46, FX-7p judge-closure Q-e ②)이 'PIE 체제 관문 미달이면 패키지 Test 빌드에서 판정'을 요구했으나 Test 경로가 없다. UBT 예외 문구(소스): "Targets cannot be built in the {Test} configuration with this engine distribution."
- 원인: 설치판 `Engine/Config/BaseEngine.ini` Win64 게임 구성 목록이 DebugGame·Development·Shipping뿐(Test 0건), UBT가 목록 밖 구성을 거부. Test 엔진 오브젝트가 없어 소스 엔진 빌드 없이는 우회 불가. 에디터 바이너리 `-game`도 막힘(L-editor-12)
- 해결: 성능 판정은 PIE에서 SP(`r.ScreenPercentage` ≤200, TSR 상한)로 GPU 병목 체제를 만들어 하고, 패키지가 꼭 필요하면 Development 쿠킹 게임 + C++ 측정 실행기(빌드 3~5시간 추정)를 쓴다. Development는 CSV GPU 통계·로그·콘솔이 모두 동작
- 범위: 이 기계의 설치판 엔진을 쓰는 모든 성능 측정·패키징 계획(FluidFX FX-7p-r, 그 밖의 '최종 Test 빌드' 문구)
- 증거: `Saved/AgentOps/20261023/fx7pr/understand-package.md` 1절(엔진 설정·UBT 소스 근거, 실제 Test 빌드 실행 재현은 하지 않음 — 미검증 실행)
- 날짜·상태: 2026-10-24 active
- 발견: claude(FluidFX FX-7p-r)

### L-build-17 나이아가라 에디터·데이터 채널 C++ 함정 3가지: `SNiagaraModuleWizard.h` 포함 시 C2491, NDC 판독기 `FVector3f`는 StaticStruct 오류, `NewObject`로 만든 데이터 채널은 `PostLoad` 전까지 `IsValid` 거짓
- 증상: ① TDGameEditor에서 `SNiagaraModuleWizard.h`를 포함하면 error C2491(dllimport 함수 정의 불가, 원문 미보존) ② NDC 판독기 구조체 필드를 `FVector3f`로 두면 StaticStruct 관련 컴파일 오류 ③ 시험에서 리플렉션으로 만든 데이터 채널 에셋이 `IsValid` 거짓이라 쓰기·읽기가 0
- 해결: ① 헤더 대신 필요한 내보낸 함수 2개를 직접 선언 ② `FVector`를 쓰고 float 지역 변수는 `TLWCType`으로 ③ 시험에서 만든 뒤 `PostLoad()`를 불러 초기화(H5 DataChannelRoutesByChannel)
- 범위: TDGameEditor 나이아가라 모듈 라이브러리, TDGame 데이터 채널 판독기·시험
- 증거: `Saved/AgentOps/20261026/wave2/fx5/`(빌더 r0~r5 기록), `Saved/AgentOps/20261026/wave2/ib2/`
- 날짜·상태: 2026-10-27 active(①②는 원문 오류 줄 미보존 — 미검증 문구)
- 발견: claude(FluidFX FX-5 빌더)

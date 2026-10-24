<#
실행 환경: Windows PowerShell 5.1, UE 5.8 런처 설치판(IWBench 편집기 대상 빌드 완료 상태)
실행 명령: powershell -File Tools/InfiniteWorld/bench/run_bench.ps1 -Suite terrain|ism|stream|nav [-Tag r1] [-Render] [-TimeoutSec 600] [-Root <작업 폴더>] [-Extra @(...)]
출력: <Root 기본 <프로젝트>/Saved/AgentOps/IWBench>/logs/<Suite>-<Tag>.log, <Root>/results/<Suite>-<Tag>.json, 표준 출력에 종료 코드·PID·벽시계 초
상태: 실험 재현용(2026-10-23 무한 세계 벤치, TDGame 과 분리된 Tools/InfiniteWorld/IWBench 전용, 먼저 IWBenchEditor 빌드)
#>
param(
	[Parameter(Mandatory = $true)][string]$Suite,
	[string]$Tag = "r1",
	[switch]$Render,
	[int]$TimeoutSec = 600,
	[string[]]$Extra = @(),
	[string]$Root = ""
)
$ProjectRoot = (Resolve-Path "$PSScriptRoot/../../..").Path
if ($Root -eq "") { $Root = "$ProjectRoot/Saved/AgentOps/IWBench" }
$EngineRoot = (Select-String -Path "$ProjectRoot/Tools/ue_editor.py" -Pattern '^ENGINE = r"(.+)"').Matches[0].Groups[1].Value
$EditorExe = Join-Path $EngineRoot "Engine/Binaries/Win64/UnrealEditor.exe"
$Project = (Resolve-Path "$PSScriptRoot/../IWBench/IWBench.uproject").Path
New-Item -ItemType Directory -Force "$Root/logs", "$Root/results" | Out-Null
$Name = "$Suite-$Tag"
$Log = "$Root/logs/$Name.log"
$Out = "$Root/results/$Name.json"
if (Test-Path $Out) { Remove-Item $Out }
$ArgList = @("`"$Project`"", "-game", "-unattended", "-nosplash", "-nosound", "-log", "-abslog=`"$Log`"", "-TDBenchSuite=$Suite", "-TDBenchOut=`"$Out`"")
if ($Render) { $ArgList += @("-RenderOffscreen", "-ResX=320", "-ResY=240", "-windowed") } else { $ArgList += "-NullRHI" }
$ArgList += $Extra
$Watch = [System.Diagnostics.Stopwatch]::StartNew()
$Process = Start-Process -FilePath $EditorExe -ArgumentList $ArgList -PassThru
$Finished = $Process.WaitForExit($TimeoutSec * 1000)
if (-not $Finished) {
	Stop-Process -Id $Process.Id -Force
	"TIMEOUT pid=$($Process.Id) seconds=$([int]$Watch.Elapsed.TotalSeconds)"
} else {
	"EXIT code=$($Process.ExitCode) pid=$($Process.Id) seconds=$([int]$Watch.Elapsed.TotalSeconds) json=$(Test-Path $Out)"
}

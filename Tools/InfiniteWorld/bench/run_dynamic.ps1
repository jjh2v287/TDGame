<#
실행 환경: Windows PowerShell 5.1, UE 5.8 런처 설치판(IWBench 편집기 대상 빌드 완료 상태)
실행 명령: powershell -File Tools/InfiniteWorld/bench/run_dynamic.ps1 [-Only stream|nav|render] [-Root <작업 폴더>]
출력: logs/<스위트>-<태그>.log, results/<스위트>-<태그>.json, logs/dynamic-progress.txt(실행별 종료 코드·초)
상태: 실험 재현용(2026-10-23 무한 세계 벤치 2단계, 내비·스트리밍 동적 스위트, 약 38분 순차)
#>
param([string]$Only = "", [string]$Root = "")
if ($Root -eq "") { $Root = "$((Resolve-Path "$PSScriptRoot/../../..").Path)/Saved/AgentOps/IWBench" }
$Runner = "$PSScriptRoot/run_bench.ps1"
New-Item -ItemType Directory -Force "$Root/logs" | Out-Null
$Progress = "$Root/logs/dynamic-progress.txt"
$NavSection = "-ini:Engine:[/Script/NavigationSystem.RecastNavMesh]:TileSizeUU="
$Runs = @()
foreach ($Rep in @("r1", "r2")) {
	$Runs += , @("stream", "A-$Rep", @("-TDBenchCases=x6,d6,x15,d15"), $false)
	$Runs += , @("stream", "B-$Rep", @("-TDBenchCases=x50,d50,f6"), $false)
}
foreach ($Rep in @("r1", "r2")) {
	foreach ($Tile in @(800, 1000, 1600)) {
		$Runs += , @("nav", "T$Tile-$Rep", @("-TDBenchCases=x6i4000,x6i6000,f6i4000", "$NavSection$Tile"), $false)
	}
}
$Runs += , @("stream", "render", @("-TDBenchCases=x15,d50"), $true)
foreach ($Run in $Runs) {
	$Suite = $Run[0]; $Tag = $Run[1]; $Extra = $Run[2]; $Render = $Run[3]
	if ($Only -ne "" -and -not (($Only -eq "render" -and $Render) -or ($Only -eq $Suite -and -not $Render))) { continue }
	$Started = Get-Date -Format "HH:mm:ss"
	if ($Render) { $Result = & $Runner -Suite $Suite -Tag $Tag -TimeoutSec 600 -Render -Extra $Extra -Root $Root }
	else { $Result = & $Runner -Suite $Suite -Tag $Tag -TimeoutSec 600 -Extra $Extra -Root $Root }
	Add-Content -Path $Progress -Value "$Started $Suite-$Tag $Result"
}
Add-Content -Path $Progress -Value "ALLDONE $(Get-Date -Format 'HH:mm:ss')"

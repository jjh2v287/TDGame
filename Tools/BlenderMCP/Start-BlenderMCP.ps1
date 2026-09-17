param(
    [string]$BlenderPath = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
)

$ErrorActionPreference = 'Stop'
$bootstrapPath = Join-Path $PSScriptRoot 'bootstrap_blender.py'
$runtimePath = Join-Path $PSScriptRoot '.runtime'
$addonPath = Join-Path $runtimePath 'blender-mcp-7684c6b3ad2aa0710bbdb1cb06b497c90899ae00/addon.py'
if (-not (Test-Path $BlenderPath) -or -not (Test-Path $addonPath)) {
    throw 'Blender 5.2 or the pinned addon is missing. Run Install-BlenderMCP.ps1 first.'
}
$listener = Get-NetTCPConnection -State Listen -LocalPort 9876 -ErrorAction SilentlyContinue | Select-Object -First 1
if ($listener) {
    $owner = Get-CimInstance Win32_Process -Filter "ProcessId = $($listener.OwningProcess)"
    if ($owner.Name -ne 'blender.exe' -or -not $owner.CommandLine.Contains($bootstrapPath)) {
        throw 'Port 9876 belongs to another session. Stop that session or choose a separate Blender MCP port.'
    }
    Write-Output "TDGame Blender MCP is already running (PID $($listener.OwningProcess))."
    return
}
$env:DISABLE_TELEMETRY = 'true'
$process = Start-Process -FilePath $BlenderPath -ArgumentList @('--factory-startup', '--python', ('"' + $bootstrapPath + '"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $runtimePath 'blender.stdout.log') -RedirectStandardError (Join-Path $runtimePath 'blender.stderr.log')
$process.Id | Set-Content -LiteralPath (Join-Path $runtimePath 'blender.pid')
$deadline = [DateTime]::UtcNow.AddSeconds(45)
do {
    Start-Sleep -Milliseconds 300
    if ($process.HasExited) {
        throw "Blender exited before its MCP server started. Inspect $runtimePath/blender.stderr.log."
    }
    $listener = Get-NetTCPConnection -State Listen -LocalPort 9876 -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($listener -and $listener.OwningProcess -eq $process.Id) {
        Write-Output "TDGame Blender MCP started on 127.0.0.1:9876 (PID $($process.Id))."
        return
    }
} while ([DateTime]::UtcNow -lt $deadline)
throw "Blender MCP startup timed out. Inspect $runtimePath/blender.stdout.log."

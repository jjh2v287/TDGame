param()

$ErrorActionPreference = 'Stop'
$serverPath = Join-Path $PSScriptRoot '.venv/Scripts/blender-mcp.exe'
if (-not (Test-Path $serverPath)) {
    throw 'Blender MCP is not installed. Run Tools/BlenderMCP/Install-BlenderMCP.ps1 first.'
}
$listener = Get-NetTCPConnection -State Listen -LocalPort 9876 -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $listener) {
    $startup = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ ShowWindow = [uint16]0 }
    $startScript = Join-Path $PSScriptRoot 'Start-BlenderMCP.ps1'
    $commandLine = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $startScript + '"'
    $created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine; ProcessStartupInformation = $startup }
    if ($created.ReturnValue -ne 0) {
        throw "Could not start the independent Blender session: $($created.ReturnValue)."
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(50)
    do {
        Start-Sleep -Milliseconds 300
        $listener = Get-NetTCPConnection -State Listen -LocalPort 9876 -ErrorAction SilentlyContinue | Select-Object -First 1
    } while (-not $listener -and [DateTime]::UtcNow -lt $deadline)
    if (-not $listener) {
        throw 'Blender MCP startup timed out. Inspect Tools/BlenderMCP/.runtime logs.'
    }
}
& (Join-Path $PSScriptRoot 'Start-BlenderMCP.ps1') | Out-Null
$env:DISABLE_TELEMETRY = 'true'
$env:BLENDER_HOST = '127.0.0.1'
$env:BLENDER_PORT = '9876'
& $serverPath
exit $LASTEXITCODE

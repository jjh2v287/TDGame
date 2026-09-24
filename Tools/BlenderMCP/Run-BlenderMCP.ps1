param()

$ErrorActionPreference = 'Stop'
$serverPath = Join-Path $PSScriptRoot '.venv/Scripts/blender-mcp.exe'
if (-not (Test-Path $serverPath)) {
    throw 'Blender MCP is not installed. Run Tools/BlenderMCP/Install-BlenderMCP.ps1 first.'
}
$env:DISABLE_TELEMETRY = 'true'
$env:BLENDER_HOST = '127.0.0.1'
$env:BLENDER_PORT = '9876'
& $serverPath
exit $LASTEXITCODE

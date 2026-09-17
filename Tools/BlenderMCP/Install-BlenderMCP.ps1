param()

$ErrorActionPreference = 'Stop'
$revision = '7684c6b3ad2aa0710bbdb1cb06b497c90899ae00'
$archiveHash = '49D1FC4CC9191EE6C380A5618C1D73C80C654AC49F9D5BCF16E3FB8CF7F7620A'
$runtimePath = Join-Path $PSScriptRoot '.runtime'
$sourcePath = Join-Path $runtimePath "blender-mcp-$revision"
$archivePath = Join-Path $runtimePath "blender-mcp-$revision.zip"
New-Item -ItemType Directory -Path $runtimePath -Force | Out-Null
if (-not (Test-Path (Join-Path $sourcePath 'addon.py'))) {
    Invoke-WebRequest "https://github.com/ahujasid/blender-mcp/archive/$revision.zip" -OutFile $archivePath
    if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $archiveHash) {
        throw 'The Blender MCP source archive checksum does not match the pinned revision.'
    }
    Expand-Archive -LiteralPath $archivePath -DestinationPath $runtimePath -Force
}
& uv sync --project $PSScriptRoot --frozen
if ($LASTEXITCODE -ne 0) {
    throw 'Blender MCP Python dependency installation failed.'
}
Write-Output 'Blender MCP dependencies are installed. Run Start-BlenderMCP.ps1 or connect an MCP client.'

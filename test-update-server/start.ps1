# start.ps1 — Local update-check test server
#
# Serves a directory tree that mimics the GitHub Releases API so you can test
# the auto-update path (download portable zip -> extract -> updater.exe) without
# a public repo or a real release tag.
#
# What it serves:
#   GET /releases/latest                         -> JSON claiming version v1.0.1
#   GET /installer/wsl2ipfwd-portable-1.0.1.zip  -> the portable zip (copied from build)
#
# Workflow:
#   1. Build the C++ service + tools with the test URL:
#        .\build.ps1 -UpdateUrl http://localhost:8080/releases/latest
#
#   2. Build the C# GUI into the same bin folder:
#        dotnet build gui-cs\gui-cs.csproj -c Release
#
#   3. Package the portable zip:
#        .\build.ps1 -Portable
#      (or re-run step 1 with -Portable added)
#
#   4. Run this script (copies the zip, starts the server):
#        .\test-update-server\start.ps1
#
#   5. Start the service and open the GUI:
#        .\build\Release\bin\wsl2ipfwd-service.exe --debug
#      Then use Settings -> "Check now" (or wait for the interval). The update
#      bar appears; Download fetches the zip, Install Now runs the updater.
#
# The service built from common/version.h (default "1.0.0") sees "v1.0.1" as
# newer. Note: the zip you build also contains a 1.0.0 service, so after the
# update the version stays 1.0.0 — good for exercising the *mechanism*. To watch
# the version actually change, bump common/version.h to 1.0.1, rebuild, and
# repackage before serving.
#
# Press Ctrl+C to stop the server.

$ErrorActionPreference = "Stop"

$ScriptDir   = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

# Filename the mock release JSON points at (test-update-server/releases/latest).
$ZipDstName  = "wsl2ipfwd-portable-1.0.1.zip"
$ZipDst      = "$ScriptDir\installer\$ZipDstName"

# Newest portable zip produced by `.\build.ps1 -Portable`.
$ZipSrc = Get-ChildItem "$ProjectRoot\build\Release\wsl2ipfwd-portable-*.zip" `
            -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending |
            Select-Object -First 1

New-Item -ItemType Directory -Force "$ScriptDir\installer" | Out-Null

if ($ZipSrc) {
    Write-Host "Copying portable zip from build output..." -ForegroundColor Cyan
    Copy-Item $ZipSrc.FullName $ZipDst -Force
    Write-Host "  $($ZipSrc.Name)  ->  installer\$ZipDstName" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Warning "No portable zip found in build\Release\."
    Write-Warning "Build and package it first:"
    Write-Warning "  .\build.ps1 -UpdateUrl http://localhost:8080/releases/latest"
    Write-Warning "  dotnet build gui-cs\gui-cs.csproj -c Release"
    Write-Warning "  .\build.ps1 -Portable"
    Write-Host ""
    Write-Host "Continuing without a zip — the update bar appears but Download returns 404." -ForegroundColor Yellow
}

# ---- Start server -----------------------------------------------------------
Write-Host ""
Write-Host "Starting mock GitHub Releases server at http://localhost:8080" -ForegroundColor Green
Write-Host ""
Write-Host "  /releases/latest                        -> claims v1.0.1"      -ForegroundColor Cyan
Write-Host "  /installer/$ZipDstName  -> portable zip download"              -ForegroundColor Cyan
Write-Host ""
Write-Host "Service must be built with:" -ForegroundColor Yellow
Write-Host "  .\build.ps1 -UpdateUrl http://localhost:8080/releases/latest" -ForegroundColor White
Write-Host ""
Write-Host "Press Ctrl+C to stop." -ForegroundColor Yellow
Write-Host ""

Push-Location $ScriptDir
try {
    python -m http.server 8080
} finally {
    Pop-Location
}

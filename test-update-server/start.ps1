# start.ps1 — Local update-check test server
#
# Serves a directory tree that mimics the GitHub Releases API so you can
# test the auto-update feature without a public repo or a real release tag.
#
# What it serves:
#   GET /releases/latest  → JSON claiming version v1.0.1
#   GET /installer/wsl2ipfwd-setup.exe → the installer copied from build output
#
# Workflow:
#   1. Build the C++ service with the test URL:
#        .\build.ps1 -UpdateUrl http://localhost:8080/releases/latest
#
#   2. Build the installer (needed for the download button to work):
#        Push-Location build\Release && cmake --build . --target installer; Pop-Location
#
#   3. Run this script (copies the installer, starts the server):
#        .\test-update-server\start.ps1
#
#   4. In a second terminal, start the service:
#        .\build\Release\bin\wsl2ipfwd-service.exe --debug
#      Then open the GUI and go to Settings → "Check Now" or wait for the
#      scheduled interval to fire.
#
# The service was built with version "1.0.0" (common/version.h default) so it
# will see "v1.0.1" as a newer release and show the update bar.
#
# Press Ctrl+C to stop the server.

$ErrorActionPreference = "Stop"

$ScriptDir    = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot  = Split-Path -Parent $ScriptDir
$InstallerSrc = "$ProjectRoot\build\Release\installer\wsl2ipfwd-setup.exe"
$InstallerDst = "$ScriptDir\installer\wsl2ipfwd-setup.exe"

# ---- Copy installer ---------------------------------------------------------
if (Test-Path $InstallerSrc) {
    Write-Host "Copying installer from build output..." -ForegroundColor Cyan
    Copy-Item $InstallerSrc $InstallerDst -Force
    Write-Host "  $InstallerDst" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Warning "Installer not found at: $InstallerSrc"
    Write-Warning "Build it first:"
    Write-Warning "  .\build.ps1 -TestUpdateUrl http://localhost:8080/releases/latest"
    Write-Warning "  Push-Location build\Release; cmake --build . --target installer; Pop-Location"
    Write-Host ""
    Write-Host "Continuing without a real installer." -ForegroundColor Yellow
    Write-Host "The update bar will appear in the GUI but 'Download' will return a 404." -ForegroundColor Yellow
}

# ---- Start server -----------------------------------------------------------
Write-Host ""
Write-Host "Starting mock GitHub Releases server at http://localhost:8080" -ForegroundColor Green
Write-Host ""
Write-Host "  /releases/latest              -> claims v1.0.1"         -ForegroundColor Cyan
Write-Host "  /installer/wsl2ipfwd-setup.exe -> installer download"   -ForegroundColor Cyan
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

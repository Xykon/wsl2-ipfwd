# Quick update script — run from an admin PowerShell
# Stops the service, copies updated exe files, restarts the service.

$ErrorActionPreference = "Stop"

$src = "$PSScriptRoot\build\Release\bin"
$dst = "C:\Program Files\WSL2 IP Forwarder"

if (-not (Test-Path $src)) {
    Write-Error "Build output not found at $src. Build the project first."
}

Write-Host "Stopping service..." -ForegroundColor Yellow
sc.exe stop wsl2ipfwd | Out-Null
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt 15) {
    $state = (sc.exe query wsl2ipfwd | Select-String "STATE") -replace '.*STATE\s+:\s+\d+\s+', ''
    if ($state -match 'STOPPED') { break }
    Start-Sleep -Milliseconds 500
}
Write-Host "Service state: $state"

Write-Host "Copying executables..." -ForegroundColor Yellow
foreach ($f in Get-ChildItem "$src\*.exe") {
    Copy-Item $f.FullName "$dst\$($f.Name)" -Force
    Write-Host "  ✔ $($f.Name)"
}
foreach ($f in Get-ChildItem "$src\*.dll" -ErrorAction SilentlyContinue) {
    Copy-Item $f.FullName "$dst\$($f.Name)" -Force
    Write-Host "  ✔ $($f.Name)"
}

Write-Host "Starting service..." -ForegroundColor Yellow
sc.exe start wsl2ipfwd | Out-Null
Start-Sleep -Seconds 2
sc.exe query wsl2ipfwd | Select-String "STATE"

Write-Host ""
Write-Host "Done! wsl2ipfwd-notify.exe is now deployed." -ForegroundColor Green
Write-Host "Also cleared seenPorts by resetting config's disabled entries..." -ForegroundColor Cyan

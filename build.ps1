# Build script for WSL2 IP Forwarder (C++ components)
# Builds: wsl2ipfwd-service.exe  wsl2ipfwd-notify.exe
#
# The C# GUI (gui-cs/) is built separately:
#   dotnet build gui-cs\gui-cs.csproj -c Release
#
# No external packages or vcpkg required — nlohmann/json is vendored in third_party/.
#
# Usage:
#   .\build.ps1                                                          # Release build
#   .\build.ps1 -Config Debug                                            # Debug build
#   .\build.ps1 -Clean                                                   # Clean and rebuild
#   .\build.ps1 -UpdateUrl http://localhost:8080/releases/latest         # Point update checker at local test server
#
# Local update-check testing:
#   1. .\build.ps1 -UpdateUrl http://localhost:8080/releases/latest
#   2. Build the installer: cd build\Release && cmake --build . --target installer
#   3. Start the mock server: .\test-update-server\start.ps1
#   4. Run the service:  .\build\Release\bin\wsl2ipfwd-service.exe --debug
#
# Requirements:
#   Visual Studio 2022 (or Build Tools) with "Desktop development with C++" workload
#   cmake is bundled with VS and found automatically; no separate install needed.

param(
    [string]$Config    = "Release",
    [switch]$Clean,
    [string]$UpdateUrl = ""   # override the update-checker URL baked into the binary
                              # production: https://api.github.com/repos/<owner>/<repo>/releases/latest
                              # local test: http://localhost:8080/releases/latest
)

$ErrorActionPreference = "Stop"
$BuildDir = "build\$Config"

# ---- Locate cmake -------------------------------------------------------
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    # cmake ships with VS — search all installations via vswhere
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPaths = & $vsWhere -all -prerelease -products * -property installationPath 2>$null
    $found = $false
    foreach ($vsPath in $vsPaths) {
        $cmakeDir = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        $ninjaDir = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
        if (Test-Path "$cmakeDir\cmake.exe") {
            $env:PATH = "$cmakeDir;$ninjaDir;$env:PATH"
            Write-Host "Using CMake from: $cmakeDir" -ForegroundColor Cyan
            $found = $true; break
        }
    }
    if (-not $found) {
        Write-Error "cmake not found.`nInstall 'C++ CMake tools for Windows' via the VS Installer (Workload: Desktop development with C++)."
    }
}

# ---- Detect generator ---------------------------------------------------
# Prefer Ninja (fast, works in any VS dev shell).
# If cl.exe is not on PATH we fall back to the VS MSBuild generator.
$generator    = "Ninja"
$generatorArgs = @()
if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host "cl.exe on PATH — using Ninja generator" -ForegroundColor Cyan
} else {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsVer   = & $vsWhere -latest -products * -property installationVersion 2>$null
    if ($vsVer -match '^(\d+)\.') {
        $major   = [int]$Matches[1]
        $yearMap = @{16="2019"; 17="2022"; 18="2025"}
        if ($yearMap.ContainsKey($major)) {
            $generator    = "Visual Studio $major $($yearMap[$major])"
            $generatorArgs = @("-A", "x64")
            Write-Host "No dev shell — using generator: $generator" -ForegroundColor Yellow
            Write-Host "Tip: run from a VS Developer PowerShell to use Ninja instead." -ForegroundColor Yellow
        }
    }
    if ($generator -eq "Ninja") {
        Write-Error "No VS compiler on PATH and could not detect VS version.`nInstall Visual Studio 2022 with 'Desktop development with C++' workload."
    }
}

Write-Host "Build config: $Config" -ForegroundColor Cyan

# ---- Clean --------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

# ---- Configure & Build --------------------------------------------------
Push-Location $BuildDir
try {
    Write-Host "Configuring CMake..." -ForegroundColor Green
    $cmakeArgs = @("../..") + @("-G", $generator) + $generatorArgs + @("-DCMAKE_BUILD_TYPE=$Config")
    if ($UpdateUrl -ne "") {
        Write-Host "Update checker URL: $UpdateUrl" -ForegroundColor Magenta
        $cmakeArgs += "-DWSL2IPFWD_UPDATE_URL=$UpdateUrl"
    }
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    Write-Host "Building..." -ForegroundColor Green
    cmake --build . --config $Config --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    $absOut = (Resolve-Path ".").Path
} finally {
    Pop-Location
}

# ---- Next-step instructions (commands shown relative to the project root, --
# which is where the shell returns to after this script finishes) -----------
Write-Host ""
Write-Host "Build successful!  C++ executables in: $absOut\bin\" -ForegroundColor Green
Write-Host ""
Write-Host "Build the C# GUI:" -ForegroundColor Cyan
Write-Host "  dotnet build gui-cs\gui-cs.csproj -c $Config" -ForegroundColor Cyan
Write-Host ""
Write-Host "Build the Inno Setup installer (requires Inno Setup 6/7):" -ForegroundColor Cyan
Write-Host "  cmake --build $BuildDir --target installer" -ForegroundColor Cyan
Write-Host "  -> output: $absOut\installer\wsl2ipfwd-setup.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "Test without installing:" -ForegroundColor Cyan
Write-Host "  .\$BuildDir\bin\wsl2ipfwd-service.exe --debug" -ForegroundColor Cyan

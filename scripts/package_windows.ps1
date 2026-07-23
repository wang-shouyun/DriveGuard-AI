# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$BuildDir = "build",
    [string]$DistDir = "dist"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$version = (Get-Content -LiteralPath "VERSION" -Encoding UTF8).Trim()
$packageDir = Join-Path $DistDir "DriveGuard-AI-$version"
$exePath = Join-Path $BuildDir "bin\DriveGuardAI.exe"

if (-not (Test-Path -LiteralPath $exePath)) {
    throw "Executable not found: $exePath. Build first."
}

if (Test-Path -LiteralPath $packageDir) {
    Remove-Item -LiteralPath $packageDir -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $packageDir | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir "bin") -Destination (Join-Path $packageDir "bin") -Recurse
Copy-Item -LiteralPath "scripts" -Destination (Join-Path $packageDir "scripts") -Recurse
Copy-Item -LiteralPath "assets" -Destination (Join-Path $packageDir "assets") -Recurse
Copy-Item -LiteralPath "docs" -Destination (Join-Path $packageDir "docs") -Recurse
Copy-Item -LiteralPath "schemas" -Destination (Join-Path $packageDir "schemas") -Recurse

$files = @(
    "README.md",
    "DESIGN.md",
    "TEST_REPORT.md",
    "DELIVERY_GUIDE.md",
    "LICENSE",
    "VERSION",
    "THIRD_PARTY_NOTICES.md",
    "CHANGELOG.md",
    "AUTHORS.md",
    "verify_release.ps1",
    "run_driveguard.bat"
)
foreach ($file in $files) {
    if (Test-Path -LiteralPath $file) {
        Copy-Item -LiteralPath $file -Destination $packageDir
    }
}

$windeployqt = $null
if ($QtRoot) {
    $candidate = Join-Path $QtRoot "bin\windeployqt.exe"
    if (Test-Path -LiteralPath $candidate) {
        $windeployqt = $candidate
    }
}
if (-not $windeployqt) {
    $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
    if ($cmd) {
        $windeployqt = $cmd.Source
    }
}
if ($windeployqt) {
    & $windeployqt (Join-Path $packageDir "bin\DriveGuardAI.exe") --no-translations
} else {
    Write-Warning "windeployqt was not found. Ensure Qt DLLs and plugins are present in the package bin directory."
}

Get-ChildItem -LiteralPath $packageDir -Recurse -Directory |
    Where-Object { $_.Name -eq "__pycache__" } |
    Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath $packageDir -Recurse -File |
    Where-Object { $_.Extension -eq ".pyc" } |
    Remove-Item -Force

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\create_release_manifest.ps1 -Root $packageDir
Write-Host "Package created:" $packageDir

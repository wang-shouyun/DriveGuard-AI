# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$MinGWBin = $env:MINGW_BIN,
    [string]$BuildDir = "build",
    [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

if (-not $QtRoot) {
    throw "QtRoot is required. Example: scripts\build_windows.ps1 -QtRoot C:\Qt\6.x.x\mingw_64"
}

$cmake = (Get-Command cmake -ErrorAction Stop).Source
$args = @("-S", ".", "-B", $BuildDir, "-G", $Generator, "-DCMAKE_PREFIX_PATH=$QtRoot")
if ($MinGWBin) {
    $windres = Join-Path $MinGWBin "windres.exe"
    $gxx = Join-Path $MinGWBin "g++.exe"
    if (Test-Path -LiteralPath $windres) {
        $args += "-DCMAKE_RC_COMPILER=$windres"
    }
    if (Test-Path -LiteralPath $gxx) {
        $args += "-DCMAKE_CXX_COMPILER=$gxx"
    }
}
& $cmake @args
& $cmake --build $BuildDir --config Release

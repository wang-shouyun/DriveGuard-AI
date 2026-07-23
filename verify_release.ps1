# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

param(
    [string]$Root = (Split-Path -Parent $MyInvocation.MyCommand.Path)
)

$ErrorActionPreference = "Stop"
$rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$manifestPath = Join-Path $rootPath "SHA256SUMS.txt"

Write-Host "DriveGuard-AI Release Verification"
Write-Host "Author: Rao Jing"
Write-Host "License: GPL-3.0-only"

if (-not (Test-Path -LiteralPath $manifestPath)) {
    Write-Host "Missing SHA256SUMS.txt" -ForegroundColor Red
    exit 1
}

$script:checked = 0
$script:failed = 0

Get-Content -LiteralPath $manifestPath -Encoding UTF8 | ForEach-Object {
    $line = $_.Trim()
    if ($line.Length -eq 0 -or $line.StartsWith("#")) {
        return
    }

    if ($line -notmatch '^([0-9A-Fa-f]{64})\s{2}(.+)$') {
        Write-Host "Malformed manifest line: $line" -ForegroundColor Yellow
        $script:failed++
        return
    }

    $expected = $Matches[1].ToUpperInvariant()
    $relative = $Matches[2]
    $path = Join-Path $rootPath ($relative -replace '/', '\')
    $script:checked++

    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "MISSING  $relative" -ForegroundColor Red
        $script:failed++
        return
    }

    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToUpperInvariant()
    if ($actual -ne $expected) {
        Write-Host "CHANGED  $relative" -ForegroundColor Red
        $script:failed++
        return
    }
}

if ($script:failed -gt 0) {
    Write-Host ("Integrity: FAIL ({0} issue(s), {1} file(s) checked)" -f $script:failed, $script:checked) -ForegroundColor Red
    exit 1
}

Write-Host ("Integrity: PASS ({0} file(s) checked)" -f $script:checked) -ForegroundColor Green
exit 0

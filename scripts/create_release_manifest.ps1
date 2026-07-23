# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
$manifestPath = Join-Path $rootPath "SHA256SUMS.txt"

$files = Get-ChildItem -LiteralPath $rootPath -Recurse -File |
    Where-Object {
        $full = [System.IO.Path]::GetFullPath($_.FullName)
        $relative = $full.Substring($rootPath.Length).TrimStart('\', '/').Replace('\', '/')
        $relative -notlike "runtime/*" -and
        $relative -notlike "__pycache__/*" -and
        $relative -notlike "CMakeFiles/*" -and
        $relative -notlike "dist/*" -and
        $_.FullName -notmatch '\\__pycache__\\' -and
        $_.Name -ne "SHA256SUMS.txt"
    }

$lines = New-Object System.Collections.Generic.List[string]
[void]$lines.Add("# DriveGuard-AI release integrity manifest")
[void]$lines.Add("# Author: Rao Jing")
[void]$lines.Add("# License: GPL-3.0-only")
[void]$lines.Add(("# Generated: {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")))

$files |
    Sort-Object FullName -Unique |
    ForEach-Object {
        $full = [System.IO.Path]::GetFullPath($_.FullName)
        $relative = $full.Substring($rootPath.Length).TrimStart('\', '/')
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $full
        [void]$lines.Add(("{0}  {1}" -f $hash.Hash.ToUpperInvariant(), $relative.Replace('\', '/')))
    }

Set-Content -LiteralPath $manifestPath -Value $lines -Encoding UTF8
Write-Host "DriveGuard-AI SHA256SUMS.txt generated:" $manifestPath

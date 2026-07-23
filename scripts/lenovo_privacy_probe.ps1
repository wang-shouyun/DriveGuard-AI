# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

param(
    [ValidateSet('check', 'enable', 'disable')]
    [string]$Mode = 'check',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

Write-Host "Lenovo privacy probe is device-specific and optional."
Write-Host "It is intended only for some Lenovo PCs and is not required by DriveGuard-AI."

if (($Mode -eq 'enable' -or $Mode -eq 'disable') -and -not $Force) {
    throw "Changing Lenovo privacy state requires -Force. Run check mode first."
}

$nativeDir = 'C:\ProgramData\Lenovo\devicecenter\extends\modules\pcmprivacy\1.10\native'
$pcManagerDir = 'C:\Program Files (x86)\Lenovo\PCManager\5.1.190.5202'
$dllPath = Join-Path $nativeDir 'DesktopPrivacyPlugin.dll'

if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "Lenovo privacy plugin DLL was not found on this device."
}

if (-not [Environment]::Is64BitProcess) {
    $env:PATH = "$nativeDir;$pcManagerDir;$env:PATH"
    Set-Location $pcManagerDir

    $src = @"
using System;
using System.Runtime.InteropServices;

public static class LenovoDesktopPrivacyPlugin {
    [DllImport(@"$dllPath", CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Ansi)]
    public static extern int pcm_plugin_action(
        [MarshalAs(UnmanagedType.LPStr)] string action,
        [MarshalAs(UnmanagedType.LPStr)] string param,
        out IntPtr result);

    [DllImport(@"$dllPath", CallingConvention=CallingConvention.Cdecl)]
    public static extern void pcm_plugin_buffer_free(IntPtr buffer);
}
"@

    Add-Type -TypeDefinition $src

    function Invoke-PrivacyAction {
        param(
            [string]$Action,
            [string]$Param = ''
        )

        $ptr = [IntPtr]::Zero
        $ret = [LenovoDesktopPrivacyPlugin]::pcm_plugin_action($Action, $Param, [ref]$ptr)
        $text = ''
        if ($ptr -ne [IntPtr]::Zero) {
            $text = [Runtime.InteropServices.Marshal]::PtrToStringAnsi($ptr)
            [LenovoDesktopPrivacyPlugin]::pcm_plugin_buffer_free($ptr)
        }

        [pscustomobject]@{
            ReturnCode = $ret
            Output = $text
        }
    }

    if ($Mode -eq 'enable') {
        Invoke-PrivacyAction -Action 'NEWPLUGIN_DESKTOPPRIVACYPLUGIN_SETCAREMAPRIVACY' -Param 'normal' | ConvertTo-Json -Compress
    } elseif ($Mode -eq 'disable') {
        Invoke-PrivacyAction -Action 'NEWPLUGIN_DESKTOPPRIVACYPLUGIN_SETCAREMAPRIVACY' -Param 'open' | ConvertTo-Json -Compress
    }

    Invoke-PrivacyAction -Action 'NEWPLUGIN_DESKTOPPRIVACYPLUGIN_CHECKCAREMA' | ConvertTo-Json -Compress
    exit
}

$ps32 = Join-Path $env:WINDIR 'SysWOW64\WindowsPowerShell\v1.0\powershell.exe'
& $ps32 -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Mode $Mode -Force:$Force
exit $LASTEXITCODE

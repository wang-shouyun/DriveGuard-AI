# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

param(
    [string]$OutDir = (Join-Path $PSScriptRoot "..\assets\audio")
)

Add-Type -AssemblyName System.Speech

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$voice = $synth.GetInstalledVoices() |
    Where-Object { $_.VoiceInfo.Culture.Name -eq "zh-CN" } |
    Select-Object -First 1

if ($voice) {
    $synth.SelectVoice($voice.VoiceInfo.Name)
}

$synth.Volume = 100
$synth.Rate = -1

$utf8 = [System.Text.Encoding]::UTF8
$prompts = @{
    "light.wav"    = $utf8.GetString([Convert]::FromBase64String("55ay5Yqz5o+Q6YaS77yM6L275bqm55ay5Yqz77yM6K+35rOo5oSP44CC"))
    "moderate.wav" = $utf8.GetString([Convert]::FromBase64String("6K2m5ZGK77yM5Lit5bqm55ay5Yqz77yM6K+35L+d5oyB5riF6YaS77yM5bu66K6u5bC95b+r6Z2g6L655LyR5oGv44CC"))
    "severe.wav"   = $utf8.GetString([Convert]::FromBase64String("5Lil6YeN55ay5Yqz77yM6K+36YCJ5oup5ZCI6YCC5Yy65Z+f5YGc6L2m5LyR5oGv44CC6K+35Yu/57un57ut6am+6am244CC"))
    "invalid.wav"  = $utf8.GetString([Convert]::FromBase64String("5pGE5YOP5aS055S76Z2i5byC5bi477yM6K+35qOA5p+l6ZWc5aS055uW77yM6ZqQ56eB5byA5YWz77yM5oiW6L2v5Lu25Y2g55So44CC"))
}

foreach ($name in $prompts.Keys) {
    $path = Join-Path $OutDir $name
    $synth.SetOutputToWaveFile($path)
    $synth.Speak($prompts[$name])
    $synth.SetOutputToNull()
    Write-Host "generated $path"
}

$synth.Dispose()

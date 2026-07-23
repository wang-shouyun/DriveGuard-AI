# Assets

`assets/audio/*.wav` contains short local alert prompts used by the Windows desktop application.

The WAV files can be regenerated on Windows with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\generate_voice_alerts.ps1
```

They are not music assets and do not require cloud services. Users may replace them with their own prompts while keeping the project license and third-party notices intact. Windows System.Speech is used only by the local generation script and is not redistributed as part of the repository.

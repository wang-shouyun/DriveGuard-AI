# Changelog

## 1.0.0 - 2026-07-23

- Adopted GPL-3.0-only project licensing and SPDX headers.
- Reworked the Python detector protocol with backend, feature-origin, validity, FPS, and latency fields.
- Replaced frame-count PERCLOS with a 12-second timestamp window.
- Added blink and yawn finite-state machines.
- Shortened the default simulation cycle to about 30 seconds and made it reproducible by seed.
- Replaced the shared latest-frame overwrite with unique immutable runtime frames to avoid Windows file-sharing crashes.
- Added bounded runtime-frame retention, persistent event evidence snapshots, and a single-detector runtime mutex.
- Cached Python dependency discovery outside the real-time update loop to keep the Qt UI responsive.
- Made detector stop/resume asynchronous and suppressed intentional-stop crash notifications.
- Added event cooldown rules and database indexes.
- Added portable project/runtime/Python path discovery in the Qt application.
- Added self-contained HTML report images and UTF-8 CSV output.
- Added unit tests, smoke tests, repository checks, and GitHub community files.
- Added Windows setup, build, package, and release verification scripts.

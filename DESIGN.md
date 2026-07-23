# DriveGuard-AI Design

## Goal

DriveGuard-AI 1.0.0 demonstrates a local fatigue-driving warning workflow for a Qt/OpenCV/MediaPipe course project. The goal is stable demonstration, explainable rules, reproducible simulation, and complete local records.

## Process Architecture

The GUI stays in Qt 6 Widgets. The detector runs in Python because MediaPipe and OpenCV are easier to integrate and debug there. Qt launches the detector with QProcess and reads UTF-8 JSON Lines from stdout.

This split keeps the UI responsive, avoids freezing the window during frame processing, and allows Python detector tests without launching the GUI.

## Detection Model

The detector produces EAR, MAR, short-window PERCLOS, blink rate, yawn count, continuous eye-closure duration, pose angles, quality, calibration progress, backend, feature origin, validity, FPS, and latency.

PERCLOS is implemented as a timestamped 12-second deque. It is a short-window approximate indicator for demonstration and not a medical or traffic-safety standard.

Blink detection uses a close-open state machine. Closures between about 0.08 and 0.80 seconds are normal blinks. Longer closures become continuous eye-closure evidence and are not counted as ordinary blinks.

Yawn detection uses `mouth_closed -> candidate_open -> yawn_confirmed -> wait_until_closed`. A mouth opening must persist for about one second and the same continuous opening is counted once.

## Backends

- Simulation: `simulation_engine`, `virtual_scenario`, valid simulated data.
- MediaPipe: `mediapipe_face_mesh`, measured facial landmarks.
- Haar fallback: `haar_cascade`, basic face detection plus degraded simulated feature completion.
- Invalid camera frame: `camera_health_check`, invalid.
- Face missing: `face_detection`, unavailable.

Haar fallback is kept to preserve software flow in weak environments. It is not described as precise landmark measurement and does not advance MediaPipe calibration.

## Simulation

The default cycle is about 30 seconds:

- 0-7 seconds: normal;
- 7-14 seconds: light fatigue;
- 14-22 seconds: moderate fatigue;
- 22-30 seconds: severe fatigue.

The final level is still produced by `decide_level()` from generated features. The `--seed` option makes behavior reproducible.

## Data

SQLite stores detection records, safety events, metadata, and indexes for time/status queries. Runtime data is local and should not be committed.

## Reports

HTML reports summarize records and events. Event images are embedded as data URIs so a report can be moved without losing evidence images. CSV uses UTF-8.

## Deployment

The application resolves paths through environment variables, executable location, and compile-time fallback. Python discovery checks `DRIVEGUARD_PYTHON`, local `.venv`, bundled Python, and PATH.

## Limits

Performance and robustness depend on CPU, light, camera position, face visibility, glasses, reflection, and Python package versions. The project has not been validated on a large public dataset.

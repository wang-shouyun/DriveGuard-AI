# Detector JSON Lines Protocol

DriveGuard-AI uses UTF-8 JSON Lines between the Qt GUI and Python detector. Each stdout line is one complete JSON object. Diagnostics go to stderr.

Required sample fields:

- `protocol_version`: currently `1`;
- `message_type`: `sample`;
- `timestamp`, `mode`, `level`, `fatigue_score`, `reason`;
- `frame_path`, `ear`, `mar`, `perclos`, `blink_rate`, `yawn_count`;
- `eye_closed_seconds`, `pitch`, `yaw`, `roll`;
- `quality_score`, `calibration_progress`;
- `risk_factors`;
- `detector_backend`, `feature_origin`, `measurement_valid`, `perception_state`;
- `processing_fps`, `latency_ms`.

Level values are `normal`, `light`, `moderate`, `severe`, and `invalid`.

Mode values are `simulation`, `camera`, and `video`.

Backend values used by 1.0.0:

- `simulation_engine`;
- `mediapipe_face_mesh`;
- `haar_cascade`;
- `camera_health_check`;
- `face_detection`.

Feature origins:

- `virtual_scenario`;
- `measured`;
- `degraded_simulated_features`;
- `invalid`;
- `unavailable`.

Compatibility: Qt accepts missing new fields with conservative defaults, so older detector output does not crash the GUI.

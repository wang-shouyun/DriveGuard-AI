# Third-Party Notices

DriveGuard-AI is licensed as GPL-3.0-only. Third-party components keep their own licenses.

- Qt 6 and Qt Charts: The project uses Qt Widgets, Qt Charts, and Qt SQL. Qt Charts is used under GPL-compatible terms.
- OpenCV: Used for image capture, drawing, Haar cascade fallback, and video processing.
- MediaPipe: Used for CPU face-landmark inference when available.
- NumPy: Used for numeric operations in the Python detector.
- Python: Runs the detector process launched by Qt through QProcess.
- SQLite: Used through Qt SQL for local records and safety events.
- Windows System.Speech: Used only by the optional local WAV generation script.

No cloud service, paid API, CUDA, TensorRT, or ONNX Runtime dependency is required by the 1.0.0 implementation.

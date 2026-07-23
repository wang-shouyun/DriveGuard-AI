# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import detector


def fail(message: str) -> int:
    print(f"FAIL: {message}")
    return 1


def main() -> int:
    required = {
        "protocol_version",
        "message_type",
        "mode",
        "level",
        "fatigue_score",
        "risk_factors",
        "detector_backend",
        "feature_origin",
        "measurement_valid",
        "perception_state",
        "processing_fps",
        "latency_ms",
    }
    engine = detector.SimulationEngine(cycle_seconds=30.0, seed=20260723)
    samples = [engine.sample(t, ROOT / "runtime", no_frame_output=True) for t in [1.0, 8.5, 16.2, 20.0, 24.0, 27.0, 30.2]]
    levels = {sample["level"] for sample in samples[:-1]}
    if not {"normal", "light", "moderate", "severe"}.issubset(levels):
        return fail(f"simulation did not cover four levels: {sorted(levels)}")
    if samples[-1]["level"] not in {"normal", "light"} or samples[-1]["fatigue_score"] >= 40:
        return fail("new simulation cycle did not reset to low risk")
    for sample in samples:
        if not required.issubset(sample.keys()):
            return fail(f"missing fields: {sorted(required - set(sample.keys()))}")
        if not (0 <= int(sample["fatigue_score"]) <= 100):
            return fail("score out of bounds")
        if not (0.0 <= float(sample["perclos"]) <= 1.0):
            return fail("PERCLOS out of bounds")
        json.dumps(sample, ensure_ascii=False)

    with tempfile.TemporaryDirectory(prefix="driveguard_smoke_") as temp_dir:
        runtime = Path(temp_dir)
        frame = detector.np.zeros((90, 160, 3), dtype=detector.np.uint8)
        paths = [detector.write_runtime_frame(runtime, frame) for _ in range(8)]
        if any(not path or not Path(path).is_file() for path in paths):
            return fail("runtime frame write failed")
        if len(set(paths)) != len(paths):
            return fail("runtime frames did not use unique paths")
        if any(detector.cv2.imread(path) is None for path in paths):
            return fail("runtime frame could not be decoded")

        detector.cleanup_runtime_frames(runtime / "frames", keep=3)
        if len(list((runtime / "frames").glob("frame_*.jpg"))) > 3:
            return fail("runtime frame retention cleanup failed")

        first_lock = detector.RuntimeProcessLock(runtime)
        second_lock = detector.RuntimeProcessLock(runtime)
        first_lock.acquire()
        try:
            try:
                second_lock.acquire()
            except detector.DetectorAlreadyRunningError:
                pass
            else:
                second_lock.release()
                return fail("duplicate detector runtime lock was not rejected")
        finally:
            first_lock.release()

        second_lock.acquire()
        second_lock.release()

    print("PASS: detector simulation smoke test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

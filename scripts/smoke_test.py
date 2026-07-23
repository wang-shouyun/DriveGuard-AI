# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

import json
import sys
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
    print("PASS: detector simulation smoke test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

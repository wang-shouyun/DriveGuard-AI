# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

import ast
import json
import unittest
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import detector


class DetectorLogicTests(unittest.TestCase):
    def test_only_one_decide_level_definition(self):
        tree = ast.parse((ROOT / "scripts" / "detector.py").read_text(encoding="utf-8"))
        count = sum(isinstance(node, ast.FunctionDef) and node.name == "decide_level" for node in ast.walk(tree))
        self.assertEqual(count, 1)

    def test_perclos_bounds_and_time_pruning(self):
        state = detector.FatigueState()
        state.update(True, 0.3, 0.0)
        state.update(True, 0.3, 1.0)
        self.assertGreater(state.perclos, 0.0)
        state.update(False, 0.3, detector.PERCLOS_WINDOW_SECONDS + 2.0)
        self.assertGreaterEqual(state.perclos, 0.0)
        self.assertLessEqual(state.perclos, 1.0)
        self.assertEqual(len(state.closed_window), 1)

    def test_short_blink_counts(self):
        state = detector.FatigueState()
        state.update(True, 0.3, 1.00)
        state.update(False, 0.3, 1.20)
        self.assertEqual(state.blink_rate(1.20), 1.0)

    def test_long_eye_closure_is_not_blink(self):
        state = detector.FatigueState()
        state.update(True, 0.3, 1.00)
        state.update(False, 0.3, 2.20)
        self.assertEqual(state.blink_rate(2.20), 0.0)

    def test_yawn_counts_once_per_continuous_open(self):
        state = detector.FatigueState()
        state.update(False, 0.65, 1.00, 0.62)
        state.update(False, 0.66, 2.10, 0.62)
        state.update(False, 0.68, 3.20, 0.62)
        self.assertEqual(state.yawn_count, 1)

    def test_yawn_resets_after_close(self):
        state = detector.FatigueState()
        state.update(False, 0.65, 1.00, 0.62)
        state.update(False, 0.66, 2.10, 0.62)
        state.update(False, 0.50, 2.40, 0.62)
        state.update(False, 0.66, 3.00, 0.62)
        state.update(False, 0.67, 4.20, 0.62)
        self.assertEqual(state.yawn_count, 2)

    def test_simulation_covers_four_levels(self):
        engine = detector.SimulationEngine(cycle_seconds=30.0, seed=20260723)
        levels = {
            engine.sample(t, ROOT / "runtime", no_frame_output=True)["level"]
            for t in [1.0, 8.5, 16.5, 19.0, 24.0, 26.5, 29.0]
        }
        self.assertTrue({"normal", "light", "moderate", "severe"}.issubset(levels))

    def test_cycle_reset_starts_low_risk(self):
        engine = detector.SimulationEngine(cycle_seconds=30.0, seed=20260723)
        engine.sample(26.0, ROOT / "runtime", no_frame_output=True)
        sample = engine.sample(30.1, ROOT / "runtime", no_frame_output=True)
        self.assertIn(sample["level"], {"normal", "light"})
        self.assertLess(sample["fatigue_score"], 40)

    def test_score_bounds(self):
        for args in [
            (0.30, 0.35, 0.0, 0.0, 0.0, 0.0, 0.0),
            (0.08, 0.80, 0.95, 24.0, 3.0, 30.0, 35.0),
        ]:
            score, *_ = detector.decide_level(*args)
            self.assertGreaterEqual(score, 0)
            self.assertLessEqual(score, 100)

    def test_normal_average_lower_than_severe(self):
        normal_engine = detector.SimulationEngine(cycle_seconds=30.0, seed=20260723)
        severe_engine = detector.SimulationEngine(cycle_seconds=30.0, seed=20260723)
        normal = [normal_engine.sample(t, ROOT / "runtime", no_frame_output=True)["fatigue_score"] for t in [1.0, 2.0, 3.0]]
        severe = [severe_engine.sample(t, ROOT / "runtime", no_frame_output=True)["fatigue_score"] for t in [24.0, 26.0, 28.0]]
        self.assertLess(sum(normal) / len(normal), sum(severe) / len(severe))

    def test_required_fields_and_json_serializable(self):
        sample = detector.generate_simulation_sample(1.0)
        required = {
            "protocol_version",
            "message_type",
            "timestamp",
            "mode",
            "level",
            "fatigue_score",
            "reason",
            "frame_path",
            "ear",
            "mar",
            "perclos",
            "blink_rate",
            "yawn_count",
            "eye_closed_seconds",
            "pitch",
            "yaw",
            "roll",
            "quality_score",
            "calibration_progress",
            "risk_factors",
            "detector_backend",
            "feature_origin",
            "measurement_valid",
            "perception_state",
            "processing_fps",
            "latency_ms",
        }
        self.assertTrue(required.issubset(sample.keys()))
        json.dumps(sample, ensure_ascii=False)

    def test_source_fields(self):
        sample = detector.generate_simulation_sample(1.0)
        self.assertEqual(sample["detector_backend"], "simulation_engine")
        self.assertEqual(sample["feature_origin"], "virtual_scenario")
        self.assertTrue(sample["measurement_valid"])
        self.assertEqual(sample["perception_state"], "simulated")

    def test_invalid_sample_not_measurable(self):
        sample = detector.invalid_sample("camera", "invalid", "")
        self.assertFalse(sample["measurement_valid"])
        self.assertEqual(sample["level"], "invalid")
        self.assertEqual(sample["perception_state"], "invalid_frame")

    def test_fixed_seed_reproducible(self):
        a = detector.SimulationEngine(seed=20260723)
        b = detector.SimulationEngine(seed=20260723)
        seq_a = [(a.sample(t, ROOT / "runtime", no_frame_output=True)["level"], a.sample(t + 0.2, ROOT / "runtime", no_frame_output=True)["fatigue_score"]) for t in [1, 8, 16, 24]]
        seq_b = [(b.sample(t, ROOT / "runtime", no_frame_output=True)["level"], b.sample(t + 0.2, ROOT / "runtime", no_frame_output=True)["fatigue_score"]) for t in [1, 8, 16, 24]]
        self.assertEqual(seq_a, seq_b)


if __name__ == "__main__":
    unittest.main()

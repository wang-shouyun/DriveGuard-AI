# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

"""DriveGuard-AI detector process.

The Qt front end starts this script through QProcess and consumes one UTF-8
JSON object per line. The script intentionally stays CPU-only and keeps the
simulation path deterministic so that a course defense can be demonstrated
without a camera.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import sys
import time
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Deque, Dict, Iterable, List, Optional, Sequence, Tuple

os.environ.setdefault("GLOG_minloglevel", "2")
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import cv2
import numpy as np

try:
    from PIL import Image, ImageDraw, ImageFont
except Exception:
    Image = None
    ImageDraw = None
    ImageFont = None

try:
    import mediapipe as mp
except Exception:
    mp = None

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")


PROTOCOL_VERSION = 1
DEFAULT_SIMULATION_CYCLE_SECONDS = 30.0
DEFAULT_SEED = 20260723
PERCLOS_WINDOW_SECONDS = 12.0
BLINK_MIN_SECONDS = 0.08
BLINK_MAX_SECONDS = 0.80
YAWN_CONFIRM_SECONDS = 1.0
YAWN_HYSTERESIS = 0.08
DEFAULT_FRAME_WIDTH = 800
DEFAULT_FRAME_HEIGHT = 450
RUNTIME_FRAME_KEEP_COUNT = 240
RUNTIME_FRAME_CLEANUP_INTERVAL = 24

LEVEL_TEXT = {
    "normal": "正常",
    "light": "轻度疲劳",
    "moderate": "中度疲劳",
    "severe": "重度疲劳",
    "invalid": "无有效画面",
}

LEVEL_TEXT_ASCII = {
    "normal": "NORMAL",
    "light": "LIGHT",
    "moderate": "MODERATE",
    "severe": "SEVERE",
    "invalid": "INVALID",
}

LEFT_EYE = [33, 160, 158, 133, 153, 144]
RIGHT_EYE = [362, 385, 387, 263, 373, 380]
MOUTH = [61, 291, 13, 14, 81, 178, 312, 402]
POSE_POINTS = [1, 152, 33, 263, 61, 291]


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def normalize_pose_angle(angle: float) -> float:
    if angle > 90.0:
        return angle - 180.0
    if angle < -90.0:
        return angle + 180.0
    return angle


def euclidean(p1: Tuple[int, int], p2: Tuple[int, int]) -> float:
    return math.hypot(p1[0] - p2[0], p1[1] - p2[1])


def landmark_point(landmarks, index: int, width: int, height: int) -> Tuple[int, int]:
    lm = landmarks[index]
    return int(lm.x * width), int(lm.y * height)


def eye_aspect_ratio(points: Sequence[Tuple[int, int]]) -> float:
    p1, p2, p3, p4, p5, p6 = points
    vertical = euclidean(p2, p6) + euclidean(p3, p5)
    horizontal = 2.0 * max(euclidean(p1, p4), 1.0)
    return vertical / horizontal


def mouth_aspect_ratio(points: Sequence[Tuple[int, int]]) -> float:
    left, right, upper, lower, upper_l, lower_l, upper_r, lower_r = points
    vertical = euclidean(upper, lower) + euclidean(upper_l, lower_l) + euclidean(upper_r, lower_r)
    horizontal = 3.0 * max(euclidean(left, right), 1.0)
    return vertical / horizontal


def solve_head_pose(landmarks, width: int, height: int) -> Tuple[float, float, float]:
    image_points = np.array(
        [landmark_point(landmarks, index, width, height) for index in POSE_POINTS],
        dtype=np.float64,
    )
    model_points = np.array(
        [
            (0.0, 0.0, 0.0),
            (0.0, -63.6, -12.5),
            (-43.3, 32.7, -26.0),
            (43.3, 32.7, -26.0),
            (-28.9, -28.9, -24.1),
            (28.9, -28.9, -24.1),
        ],
        dtype=np.float64,
    )
    focal = float(width)
    center = (width / 2.0, height / 2.0)
    camera_matrix = np.array([[focal, 0, center[0]], [0, focal, center[1]], [0, 0, 1]], dtype=np.float64)
    dist_coeffs = np.zeros((4, 1), dtype=np.float64)
    success, rotation_vector, _ = cv2.solvePnP(
        model_points,
        image_points,
        camera_matrix,
        dist_coeffs,
        flags=cv2.SOLVEPNP_ITERATIVE,
    )
    if not success:
        return 0.0, 0.0, 0.0
    rotation_matrix, _ = cv2.Rodrigues(rotation_vector)
    angles, *_ = cv2.RQDecomp3x3(rotation_matrix)
    return (
        normalize_pose_angle(float(angles[0])),
        normalize_pose_angle(float(angles[1])),
        normalize_pose_angle(float(angles[2])),
    )


_runtime_frame_sequence = 0


def cleanup_runtime_frames(frame_dir: Path, keep: int = RUNTIME_FRAME_KEEP_COUNT) -> None:
    try:
        frames = sorted(
            (path for path in frame_dir.glob("frame_*.jpg") if path.is_file()),
            key=lambda path: path.stat().st_mtime_ns,
            reverse=True,
        )
    except OSError:
        return

    for old_frame in frames[max(1, keep):]:
        try:
            old_frame.unlink()
        except OSError:
            # A UI preview, antivirus scanner or report export may still be reading it.
            pass


def write_runtime_frame(runtime: Path, frame: np.ndarray, quality: int = 86) -> str:
    """Publish a completed frame through a unique immutable path.

    The GUI only sees the path after cv2.imwrite() returns. This avoids replacing
    a shared frame_latest.jpg while Windows or Qt still has that file open.
    """

    global _runtime_frame_sequence
    frame_dir = runtime / "frames"
    try:
        frame_dir.mkdir(parents=True, exist_ok=True)
        _runtime_frame_sequence += 1
        frame_path = frame_dir / (
            f"frame_{os.getpid()}_{time.time_ns()}_{_runtime_frame_sequence:08d}.jpg"
        )
        ok = cv2.imwrite(
            str(frame_path),
            frame,
            [int(cv2.IMWRITE_JPEG_QUALITY), int(quality)],
        )
    except (OSError, cv2.error) as exc:
        print(f"实时帧写入失败：{exc}", file=sys.stderr, flush=True)
        return ""

    if not ok:
        print(f"实时帧编码失败：{frame_path}", file=sys.stderr, flush=True)
        return ""

    if _runtime_frame_sequence % RUNTIME_FRAME_CLEANUP_INTERVAL == 0:
        cleanup_runtime_frames(frame_dir)
    return str(frame_path)


class DetectorAlreadyRunningError(RuntimeError):
    pass


class RuntimeProcessLock:
    """One detector process per runtime directory.

    Windows named mutexes are released by the OS even when QProcess terminates
    Python forcefully, so a crash cannot leave a stale lock behind.
    """

    def __init__(self, runtime: Path) -> None:
        runtime_key = str(runtime.resolve()).casefold().encode("utf-8", errors="surrogatepass")
        digest = hashlib.sha256(runtime_key).hexdigest()[:24]
        self.name = f"Local\\DriveGuardAI.Detector.{digest}"
        self.runtime = runtime
        self._handle = None
        self._file = None

    def acquire(self) -> None:
        if os.name == "nt":
            import ctypes
            from ctypes import wintypes

            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.CreateMutexW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.LPCWSTR]
            kernel32.CreateMutexW.restype = wintypes.HANDLE
            kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
            kernel32.CloseHandle.restype = wintypes.BOOL

            ctypes.set_last_error(0)
            handle = kernel32.CreateMutexW(None, False, self.name)
            error = ctypes.get_last_error()
            if not handle:
                raise OSError(error, "无法创建检测进程互斥锁")
            if error == 183:  # ERROR_ALREADY_EXISTS
                kernel32.CloseHandle(handle)
                raise DetectorAlreadyRunningError(
                    "同一运行目录已有检测进程，请关闭重复的软件窗口后重试。"
                )
            self._handle = (kernel32, handle)
            return

        import fcntl

        lock_path = self.runtime / "detector.lock"
        self._file = lock_path.open("a+", encoding="utf-8")
        try:
            fcntl.flock(self._file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            self._file.close()
            self._file = None
            raise DetectorAlreadyRunningError(
                "同一运行目录已有检测进程，请关闭重复的软件窗口后重试。"
            ) from exc

    def release(self) -> None:
        if self._handle is not None:
            kernel32, handle = self._handle
            kernel32.CloseHandle(handle)
            self._handle = None
        if self._file is not None:
            try:
                self._file.close()
            finally:
                self._file = None


def camera_frame_health(frame: Optional[np.ndarray]) -> Tuple[bool, float, float, str]:
    if frame is None or frame.size == 0:
        return False, 0.0, 0.0, "摄像头未返回图像帧"
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY) if len(frame.shape) == 3 else frame
    mean = float(np.mean(gray))
    std = float(np.std(gray))
    if mean < 8.0:
        return False, mean, std, f"画面过暗(mean={mean:.1f}, std={std:.1f})"
    if mean > 247.0:
        return False, mean, std, f"画面过亮(mean={mean:.1f}, std={std:.1f})"
    if std < 5.0:
        return False, mean, std, f"摄像头返回灰色隐私占位图(mean={mean:.1f}, std={std:.1f})"
    return True, mean, std, f"画面健康(mean={mean:.1f}, std={std:.1f})"


@dataclass
class PerformanceMeter:
    fps: float = 0.0
    latency_ms: float = 0.0
    _last_time: Optional[float] = None

    def update(self, started_at: float, finished_at: float) -> Tuple[float, float]:
        latency = max(0.0, (finished_at - started_at) * 1000.0)
        if self._last_time is not None:
            instant_fps = clamp(1.0 / max(finished_at - self._last_time, 1e-6), 0.0, 120.0)
            self.fps = instant_fps if self.fps <= 0.0 else self.fps * 0.85 + instant_fps * 0.15
        self.latency_ms = latency if self.latency_ms <= 0.0 else self.latency_ms * 0.80 + latency * 0.20
        self._last_time = finished_at
        return round(self.fps, 2), round(self.latency_ms, 2)

    def reset(self) -> None:
        self.fps = 0.0
        self.latency_ms = 0.0
        self._last_time = None


@dataclass
class AdaptiveCalibrator:
    ear_values: Deque[float] = field(default_factory=lambda: deque(maxlen=120))
    mar_values: Deque[float] = field(default_factory=lambda: deque(maxlen=120))
    ear_threshold: float = 0.20
    mar_threshold: float = 0.62

    def update(self, ear: float, mar: float, measurement_valid: bool, backend: str) -> None:
        if not measurement_valid or backend != "mediapipe_face_mesh":
            return
        if 0.20 <= ear <= 0.38 and mar <= 0.52:
            self.ear_values.append(ear)
            self.mar_values.append(mar)
        if len(self.ear_values) >= 20:
            avg_ear = float(sum(self.ear_values) / len(self.ear_values))
            self.ear_threshold = clamp(avg_ear * 0.72, 0.16, 0.23)
        if len(self.mar_values) >= 20:
            avg_mar = float(sum(self.mar_values) / len(self.mar_values))
            self.mar_threshold = clamp(avg_mar + 0.22, 0.56, 0.72)

    @property
    def progress(self) -> int:
        return int(clamp(len(self.ear_values) / 80.0 * 100.0, 0.0, 100.0))


@dataclass
class FatigueState:
    closed_window: Deque[Tuple[float, bool]] = field(default_factory=deque)
    blink_times: Deque[float] = field(default_factory=deque)
    prev_closed: bool = False
    closed_start: Optional[float] = None
    current_closed_seconds: float = 0.0
    yawn_count: int = 0
    mouth_state: str = "mouth_closed"
    mouth_open_start: Optional[float] = None

    def reset(self) -> None:
        self.closed_window.clear()
        self.blink_times.clear()
        self.prev_closed = False
        self.closed_start = None
        self.current_closed_seconds = 0.0
        self.yawn_count = 0
        self.mouth_state = "mouth_closed"
        self.mouth_open_start = None

    def update(self, closed: bool, mar: float, timestamp: float, yawn_threshold: float = 0.62) -> None:
        self.closed_window.append((timestamp, bool(closed)))
        self._prune_perclos(timestamp)

        if closed and not self.prev_closed:
            self.closed_start = timestamp
        elif not closed and self.prev_closed:
            if self.closed_start is not None:
                duration = timestamp - self.closed_start
                if BLINK_MIN_SECONDS <= duration <= BLINK_MAX_SECONDS:
                    self.blink_times.append(timestamp)
            self.closed_start = None

        self.prev_closed = bool(closed)
        self.current_closed_seconds = max(0.0, timestamp - self.closed_start) if self.closed_start is not None else 0.0
        self._prune_blinks(timestamp)
        self._update_yawn_state(mar, timestamp, yawn_threshold)

    def _prune_perclos(self, timestamp: float) -> None:
        while self.closed_window and timestamp - self.closed_window[0][0] > PERCLOS_WINDOW_SECONDS:
            self.closed_window.popleft()

    def _prune_blinks(self, timestamp: float) -> None:
        while self.blink_times and timestamp - self.blink_times[0] > 60.0:
            self.blink_times.popleft()

    def _update_yawn_state(self, mar: float, timestamp: float, threshold: float) -> None:
        close_threshold = threshold - YAWN_HYSTERESIS
        if self.mouth_state == "mouth_closed":
            if mar >= threshold:
                self.mouth_state = "candidate_open"
                self.mouth_open_start = timestamp
        elif self.mouth_state == "candidate_open":
            if mar <= close_threshold:
                self.mouth_state = "mouth_closed"
                self.mouth_open_start = None
            elif self.mouth_open_start is not None and timestamp - self.mouth_open_start >= YAWN_CONFIRM_SECONDS:
                self.yawn_count += 1
                self.mouth_state = "yawn_confirmed"
        elif self.mouth_state == "yawn_confirmed":
            if mar <= close_threshold:
                self.mouth_state = "mouth_closed"
                self.mouth_open_start = None
            else:
                self.mouth_state = "wait_until_closed"
        elif self.mouth_state == "wait_until_closed":
            if mar <= close_threshold:
                self.mouth_state = "mouth_closed"
                self.mouth_open_start = None

    @property
    def perclos(self) -> float:
        if not self.closed_window:
            return 0.0
        return clamp(sum(1 for _, closed in self.closed_window if closed) / len(self.closed_window), 0.0, 1.0)

    def blink_rate(self, timestamp: float) -> float:
        self._prune_blinks(timestamp)
        return float(len(self.blink_times))


def risk_factor(name: str, value: object, impact: int) -> Dict[str, object]:
    return {"name": name, "value": value, "impact": int(clamp(impact, 0, 100))}


def decide_level(
    ear: float,
    mar: float,
    perclos: float,
    blink_rate: float,
    eye_closed_seconds: float,
    pitch: float,
    yaw: float,
    yawn_count: int = 0,
    face_missing: bool = False,
    measurement_valid: bool = True,
    perception_state: str = "valid",
    adaptive_ear_threshold: float = 0.20,
    adaptive_mar_threshold: float = 0.62,
) -> Tuple[int, str, str, List[Dict[str, object]], str]:
    """Return score, level, reason, risk factors and attention state."""

    if perception_state == "invalid_frame":
        return (
            0,
            "invalid",
            "摄像头无有效画面，请检查镜头盖、隐私开关、系统权限或会议软件占用",
            [risk_factor("画面无效", "invalid_frame", 90)],
            "状态不可确认",
        )
    if face_missing:
        return (
            18,
            "invalid",
            "驾驶员面部未检测，当前样本不用于疲劳结论",
            [risk_factor("面部丢失", "face_missing", 70)],
            "状态不可确认",
        )

    ear_score = clamp((adaptive_ear_threshold - ear) / 0.09, 0.0, 1.0) * 16.0
    perclos_score = clamp((perclos - 0.12) / 0.50, 0.0, 1.0) * 34.0
    closed_score = clamp(eye_closed_seconds / 2.6, 0.0, 1.0) * 28.0
    blink_score = clamp((blink_rate - 18.0) / 18.0, 0.0, 1.0) * 8.0
    mar_score = clamp((mar - adaptive_mar_threshold + 0.04) / 0.24, 0.0, 1.0) * 15.0
    yawn_score = clamp(float(yawn_count) / 2.0, 0.0, 1.0) * 7.0
    pose_score = clamp((abs(pitch) - 12.0) / 18.0, 0.0, 1.0) * 7.0
    pose_score += clamp((abs(yaw) - 20.0) / 25.0, 0.0, 1.0) * 7.0
    degraded_penalty = 4.0 if (not measurement_valid and perception_state == "degraded") else 0.0

    score = int(round(clamp(
        ear_score + perclos_score + closed_score + blink_score + mar_score + yawn_score + pose_score + degraded_penalty,
        0.0,
        100.0,
    )))

    if score >= 75:
        level = "severe"
    elif score >= 56:
        level = "moderate"
    elif score >= 31:
        level = "light"
    else:
        level = "normal"

    factors: List[Dict[str, object]] = []
    reasons: List[str] = []
    if ear < adaptive_ear_threshold:
        impact = int(clamp((adaptive_ear_threshold - ear) / 0.10 * 60.0, 10.0, 75.0))
        factors.append(risk_factor("EAR低于个体阈值", f"{ear:.2f}/{adaptive_ear_threshold:.2f}", impact))
        reasons.append(f"EAR 低于个体阈值({ear:.2f}<{adaptive_ear_threshold:.2f})")
    if eye_closed_seconds >= 1.0:
        impact = int(clamp(eye_closed_seconds / 2.8 * 90.0, 20.0, 95.0))
        factors.append(risk_factor("连续闭眼", f"{eye_closed_seconds:.1f}s", impact))
        reasons.append(f"连续闭眼 {eye_closed_seconds:.1f}s")
    if perclos >= 0.30:
        impact = int(clamp(perclos * 100.0, 30.0, 95.0))
        factors.append(risk_factor("短窗PERCLOS偏高", f"{perclos:.2f}", impact))
        reasons.append(f"短窗 PERCLOS 偏高({perclos:.2f})")
    if mar >= adaptive_mar_threshold:
        impact = int(clamp((mar - adaptive_mar_threshold) / 0.25 * 70.0, 25.0, 80.0))
        factors.append(risk_factor("嘴部开合异常", f"{mar:.2f}", impact))
        reasons.append("疑似哈欠或长时间张嘴")
    if abs(pitch) > 18.0:
        factors.append(risk_factor("俯仰姿态偏移", f"{pitch:.1f}", int(clamp(abs(pitch), 20.0, 80.0))))
        reasons.append("低头/抬头幅度过大")
    if abs(yaw) > 26.0:
        factors.append(risk_factor("视线方向偏移", f"{yaw:.1f}", int(clamp(abs(yaw), 20.0, 80.0))))
        reasons.append("视线方向偏移")
    if not measurement_valid and perception_state == "degraded":
        factors.append(risk_factor("降级特征", "Haar+simulated", 35))
        reasons.append("Haar 基础检测 + 仿真特征补全")
    if not reasons:
        reasons.append("视觉特征稳定，未触发疲劳规则")

    if level == "normal":
        attention_state = "清醒"
    elif level == "light":
        attention_state = "注意力轻微下降"
    elif level == "moderate":
        attention_state = "持续疲劳风险"
    else:
        attention_state = "强疲劳风险"
    return score, level, "，".join(reasons), factors, attention_state


def add_protocol_fields(
    sample: Dict[str, object],
    detector_backend: str,
    feature_origin: str,
    measurement_valid: bool,
    perception_state: str,
    processing_fps: float,
    latency_ms: float,
) -> Dict[str, object]:
    sample.update(
        {
            "protocol_version": PROTOCOL_VERSION,
            "message_type": "sample",
            "detector_backend": detector_backend,
            "feature_origin": feature_origin,
            "measurement_valid": bool(measurement_valid),
            "perception_state": perception_state,
            "processing_fps": float(processing_fps),
            "latency_ms": float(latency_ms),
        }
    )
    return sample


def build_sample(
    *,
    mode: str,
    ear: float,
    mar: float,
    perclos: float,
    blink_rate: float,
    yawn_count: int,
    eye_closed_seconds: float,
    pitch: float,
    yaw: float,
    roll: float,
    adaptive_ear_threshold: float,
    adaptive_mar_threshold: float,
    quality_score: int,
    calibration_progress: int,
    frame_path: str,
    snapshot_path: str = "",
    detector_backend: str,
    feature_origin: str,
    measurement_valid: bool,
    perception_state: str,
    processing_fps: float = 0.0,
    latency_ms: float = 0.0,
    face_missing: bool = False,
) -> Dict[str, object]:
    score, level, reason, factors, attention_state = decide_level(
        ear=ear,
        mar=mar,
        perclos=perclos,
        blink_rate=blink_rate,
        eye_closed_seconds=eye_closed_seconds,
        pitch=pitch,
        yaw=yaw,
        yawn_count=yawn_count,
        face_missing=face_missing,
        measurement_valid=measurement_valid,
        perception_state=perception_state,
        adaptive_ear_threshold=adaptive_ear_threshold,
        adaptive_mar_threshold=adaptive_mar_threshold,
    )
    sample = {
        "timestamp": now_iso(),
        "mode": mode,
        "level": level,
        "level_text": LEVEL_TEXT.get(level, level),
        "fatigue_score": int(score),
        "reason": reason,
        "risk_factors": factors,
        "attention_state": attention_state,
        "frame_path": frame_path,
        "snapshot_path": snapshot_path,
        "ear": round(float(ear), 3),
        "mar": round(float(mar), 3),
        "perclos": round(float(clamp(perclos, 0.0, 1.0)), 3),
        "blink_rate": round(float(blink_rate), 2),
        "yawn_count": int(yawn_count),
        "eye_closed_seconds": round(float(eye_closed_seconds), 2),
        "pitch": round(float(pitch), 2),
        "yaw": round(float(yaw), 2),
        "roll": round(float(roll), 2),
        "quality_score": int(clamp(quality_score, 0, 100)),
        "calibration_progress": int(clamp(calibration_progress, 0, 100)),
        "adaptive_ear_threshold": round(float(adaptive_ear_threshold), 3),
        "adaptive_mar_threshold": round(float(adaptive_mar_threshold), 3),
    }
    return add_protocol_fields(
        sample,
        detector_backend,
        feature_origin,
        measurement_valid,
        perception_state,
        processing_fps,
        latency_ms,
    )


class SimulationEngine:
    def __init__(self, cycle_seconds: float = DEFAULT_SIMULATION_CYCLE_SECONDS, seed: int = DEFAULT_SEED) -> None:
        self.cycle_seconds = max(12.0, float(cycle_seconds))
        self.rng = random.Random(seed)
        self.state = FatigueState()
        self.last_cycle_index: Optional[int] = None

    def _phase_metrics(self, phase: float, elapsed: float) -> Tuple[float, float, bool, float, float, float]:
        noise = lambda amount: self.rng.uniform(-amount, amount)
        if 0.0 <= phase < 7.0:
            ear = 0.295 + noise(0.012)
            mar = 0.33 + noise(0.025)
            closed = phase > 0.50 and ((phase - 0.50) % 3.2) < 0.12
            pitch = 2.0 * math.sin(elapsed * 0.5) + noise(1.5)
            yaw = 4.0 * math.sin(elapsed * 0.35) + noise(1.2)
        elif 7.0 <= phase < 14.0:
            ear = 0.205 + noise(0.015)
            mar = 0.39 + noise(0.04)
            closed = (phase % 2.1) < 0.26
            pitch = 8.0 + 2.0 * math.sin(elapsed * 0.6) + noise(2.0)
            yaw = 8.0 + noise(4.0)
        elif 14.0 <= phase < 22.0:
            ear = 0.158 + noise(0.018)
            mar = 0.66 + noise(0.05) if 15.0 <= phase <= 18.4 else 0.46 + noise(0.04)
            closed = (phase % 2.6) < 0.85
            pitch = 16.0 + 3.0 * math.sin(elapsed * 0.5) + noise(2.0)
            yaw = 18.0 + 6.0 * math.sin(elapsed * 0.4) + noise(4.0)
        else:
            ear = 0.105 + noise(0.018)
            mar = 0.70 + noise(0.04) if 24.0 <= phase <= 27.4 else 0.48 + noise(0.04)
            closed = (phase % 3.8) < 2.65
            pitch = 24.0 + 3.0 * math.sin(elapsed * 0.45) + noise(2.0)
            yaw = 30.0 + 6.0 * math.sin(elapsed * 0.35) + noise(4.0)
        return clamp(ear, 0.05, 0.38), clamp(mar, 0.10, 0.90), closed, pitch, yaw, noise(3.0)

    def sample(
        self,
        elapsed: float,
        runtime: Path,
        no_frame_output: bool = False,
        frame_width: int = DEFAULT_FRAME_WIDTH,
        frame_height: int = DEFAULT_FRAME_HEIGHT,
        processing_fps: float = 0.0,
        latency_ms: float = 0.0,
    ) -> Dict[str, object]:
        cycle_index = int(elapsed // self.cycle_seconds)
        if self.last_cycle_index is None or cycle_index != self.last_cycle_index:
            self.state.reset()
            self.last_cycle_index = cycle_index

        phase = elapsed % self.cycle_seconds
        ear, mar, closed, pitch, yaw, roll = self._phase_metrics(phase, elapsed)
        self.state.update(closed, mar, elapsed, yawn_threshold=0.62)

        frame_path = ""
        if not no_frame_output:
            frame = draw_simulation_frame(
                frame_width,
                frame_height,
                ear,
                mar,
                self.state.perclos,
                pitch,
                yaw,
            )
            frame_path = write_runtime_frame(runtime, frame)

        return build_sample(
            mode="simulation",
            ear=ear,
            mar=mar,
            perclos=self.state.perclos,
            blink_rate=self.state.blink_rate(elapsed),
            yawn_count=self.state.yawn_count,
            eye_closed_seconds=self.state.current_closed_seconds,
            pitch=pitch,
            yaw=yaw,
            roll=roll,
            adaptive_ear_threshold=0.20,
            adaptive_mar_threshold=0.62,
            quality_score=92,
            calibration_progress=100,
            frame_path=frame_path,
            detector_backend="simulation_engine",
            feature_origin="virtual_scenario",
            measurement_valid=True,
            perception_state="simulated",
            processing_fps=processing_fps,
            latency_ms=latency_ms,
        )


def generate_simulation_sample(
    elapsed: float,
    engine: Optional[SimulationEngine] = None,
    runtime: Optional[Path] = None,
    no_frame_output: bool = True,
) -> Dict[str, object]:
    engine = engine or SimulationEngine()
    runtime = runtime or Path("runtime")
    return engine.sample(elapsed, runtime, no_frame_output=no_frame_output)


_FONT_CACHE: Dict[Tuple[int, bool], object] = {}


def load_cjk_font(size: int, bold: bool = False):
    if ImageFont is None:
        return None
    key = (size, bold)
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]

    font_names = [
        "msyhbd.ttc" if bold else "msyh.ttc",
        "simhei.ttf",
        "simsun.ttc",
    ]
    font_dirs = [
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts",
        Path("/System/Library/Fonts"),
        Path("/usr/share/fonts"),
    ]

    for font_dir in font_dirs:
        for name in font_names:
            path = font_dir / name
            if path.exists():
                try:
                    font = ImageFont.truetype(str(path), size)
                    _FONT_CACHE[key] = font
                    return font
                except Exception:
                    continue
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None
    _FONT_CACHE[key] = font
    return font


def draw_chinese_branding(frame: np.ndarray, level: str, score: int, backend: str) -> bool:
    if Image is None or ImageDraw is None:
        return False

    h, w = frame.shape[:2]
    header_h = max(56, h // 8)
    image = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
    draw = ImageDraw.Draw(image)

    title_font = load_cjk_font(max(24, h // 16), bold=True)
    badge_font = load_cjk_font(max(19, h // 20), bold=True)
    small_font = load_cjk_font(max(13, h // 34), bold=False)
    if not title_font or not badge_font or not small_font:
        return False

    badge_rgb = {
        "normal": (82, 175, 92),
        "light": (241, 161, 37),
        "moderate": (224, 106, 39),
        "severe": (216, 36, 60),
        "invalid": (140, 129, 118),
    }.get(level, (140, 129, 118))
    badge_w = max(170, w // 5)
    badge_box = (w - badge_w - 28, 16, w - 28, header_h - 14)

    draw.rectangle((0, 0, w, header_h), fill=(21, 27, 35))
    draw.text((28, 16), "DriveGuard-AI", font=title_font, fill=(242, 247, 251))
    draw.rectangle(badge_box, fill=badge_rgb)

    badge_text = LEVEL_TEXT.get(level, level)
    bbox = draw.textbbox((0, 0), badge_text, font=badge_font)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]
    badge_x = badge_box[0] + max(8, (badge_w - text_w) // 2)
    badge_y = badge_box[1] + max(2, (badge_box[3] - badge_box[1] - text_h) // 2) - 2
    draw.text((badge_x, badge_y), badge_text, font=badge_font, fill=(255, 255, 255))

    draw.text((24, h - 27), f"评分 {score:03d} | {backend}", font=small_font, fill=(245, 248, 252))
    author_text = "制作：饶晶 / 2026"
    author_bbox = draw.textbbox((0, 0), author_text, font=small_font)
    draw.text((w - (author_bbox[2] - author_bbox[0]) - 24, h - 27), author_text, font=small_font, fill=(220, 232, 242))

    frame[:] = cv2.cvtColor(np.asarray(image), cv2.COLOR_RGB2BGR)
    return True


def draw_branding(frame: np.ndarray, level: str, score: int, backend: str) -> np.ndarray:
    if draw_chinese_branding(frame, level, score, backend):
        return frame

    h, w = frame.shape[:2]
    header_h = max(56, h // 8)
    cv2.rectangle(frame, (0, 0), (w, header_h), (21, 27, 35), -1)
    cv2.putText(frame, "DriveGuard-AI", (28, 38), cv2.FONT_HERSHEY_SIMPLEX, 0.9, (242, 247, 251), 2, cv2.LINE_AA)
    badge_color = {
        "normal": (92, 175, 82),
        "light": (37, 161, 241),
        "moderate": (39, 106, 224),
        "severe": (60, 36, 216),
        "invalid": (118, 129, 140),
    }.get(level, (118, 129, 140))
    badge_w = max(170, w // 5)
    cv2.rectangle(frame, (w - badge_w - 28, 16), (w - 28, header_h - 14), badge_color, -1)
    cv2.putText(frame, LEVEL_TEXT_ASCII.get(level, level.upper()), (w - badge_w - 6, header_h - 28), cv2.FONT_HERSHEY_SIMPLEX, 0.66, (255, 255, 255), 2, cv2.LINE_AA)
    cv2.putText(frame, f"Score {score:03d} | {backend}", (24, h - 22), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (245, 248, 252), 1, cv2.LINE_AA)
    cv2.putText(frame, "Rao Jing / 2026", (w - 168, h - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.42, (220, 232, 242), 1, cv2.LINE_AA)
    return frame


def draw_simulation_frame(width: int, height: int, ear: float, mar: float, perclos: float, pitch: float, yaw: float) -> np.ndarray:
    frame = np.full((height, width, 3), (35, 42, 52), dtype=np.uint8)
    face_center = (width // 2 + int(yaw * 2.0), height // 2 + int(pitch * 1.4))
    face_axes = (max(82, width // 8), max(112, height // 3))
    cv2.ellipse(frame, face_center, face_axes, 0, 0, 360, (211, 224, 238), -1)
    eye_y = face_center[1] - face_axes[1] // 3
    eye_open = max(2, int(ear * 42))
    for dx in (-face_axes[0] // 3, face_axes[0] // 3):
        cv2.ellipse(frame, (face_center[0] + dx, eye_y), (32, eye_open), 0, 0, 360, (35, 42, 52), 2)
    mouth_open = max(5, int(mar * 70))
    cv2.ellipse(frame, (face_center[0], face_center[1] + face_axes[1] // 3), (45, mouth_open), 0, 0, 180, (35, 42, 52), 3)
    cv2.line(frame, (face_center[0], face_center[1] - 10), (face_center[0] - 8, face_center[1] + 44), (96, 108, 125), 2)
    level = "normal"
    score, level, *_ = decide_level(ear, mar, perclos, 0.0, 0.0, pitch, yaw)
    draw_branding(frame, level, score, "simulation")
    cv2.putText(
        frame,
        f"EAR {ear:.2f} | MAR {mar:.2f} | PERCLOS {perclos:.2f}",
        (24, height - 48),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.54,
        (245, 248, 252),
        1,
        cv2.LINE_AA,
    )
    return frame


def draw_mediapipe_landmarks(frame: np.ndarray, landmarks) -> None:
    height, width = frame.shape[:2]
    xs: List[int] = []
    ys: List[int] = []
    for index in set(LEFT_EYE + RIGHT_EYE + MOUTH + POSE_POINTS):
        x, y = landmark_point(landmarks, index, width, height)
        xs.append(x)
        ys.append(y)
        cv2.circle(frame, (x, y), 2, (95, 220, 145), -1)
    if xs and ys:
        cv2.rectangle(frame, (min(xs) - 28, min(ys) - 38), (max(xs) + 28, max(ys) + 38), (90, 220, 145), 2)


def invalid_sample(mode: str, reason: str, frame_path: str, processing_fps: float = 0.0, latency_ms: float = 0.0) -> Dict[str, object]:
    sample = build_sample(
        mode=mode,
        ear=0.0,
        mar=0.0,
        perclos=0.0,
        blink_rate=0.0,
        yawn_count=0,
        eye_closed_seconds=0.0,
        pitch=0.0,
        yaw=0.0,
        roll=0.0,
        adaptive_ear_threshold=0.20,
        adaptive_mar_threshold=0.62,
        quality_score=0,
        calibration_progress=0,
        frame_path=frame_path,
        detector_backend="camera_health_check",
        feature_origin="invalid",
        measurement_valid=False,
        perception_state="invalid_frame",
        processing_fps=processing_fps,
        latency_ms=latency_ms,
    )
    sample["reason"] = reason
    return sample


def face_missing_sample(mode: str, frame_path: str, processing_fps: float, latency_ms: float) -> Dict[str, object]:
    return build_sample(
        mode=mode,
        ear=0.0,
        mar=0.0,
        perclos=0.0,
        blink_rate=0.0,
        yawn_count=0,
        eye_closed_seconds=0.0,
        pitch=0.0,
        yaw=0.0,
        roll=0.0,
        adaptive_ear_threshold=0.20,
        adaptive_mar_threshold=0.62,
        quality_score=35,
        calibration_progress=0,
        frame_path=frame_path,
        detector_backend="face_detection",
        feature_origin="unavailable",
        measurement_valid=False,
        perception_state="face_missing",
        processing_fps=processing_fps,
        latency_ms=latency_ms,
        face_missing=True,
    )


class DetectorRunner:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.runtime = Path(args.runtime)
        self.runtime.mkdir(parents=True, exist_ok=True)
        (self.runtime / "snapshots").mkdir(parents=True, exist_ok=True)
        (self.runtime / "reports").mkdir(parents=True, exist_ok=True)
        self.state = FatigueState()
        self.calibrator = AdaptiveCalibrator()
        self.performance = PerformanceMeter()
        self.simulation = SimulationEngine(args.simulation_cycle, args.seed)
        self.haar_rng = random.Random(args.seed + 17)
        self.runtime_lock = RuntimeProcessLock(self.runtime)
        self.last_camera_frame_path = ""

    def emit(self, sample: Dict[str, object]) -> None:
        print(json.dumps(sample, ensure_ascii=False, separators=(",", ":")), flush=True)

    def run(self) -> int:
        try:
            self.runtime_lock.acquire()
        except DetectorAlreadyRunningError as exc:
            print(str(exc), file=sys.stderr, flush=True)
            return 4

        try:
            if self.args.mode == "simulation":
                return self.run_simulation()
            if self.args.mode in ("camera", "video"):
                return self.run_capture(self.args.mode)
            print(f"unknown mode: {self.args.mode}", file=sys.stderr)
            return 2
        finally:
            self.runtime_lock.release()

    def run_simulation(self) -> int:
        sample_count = 0
        start = time.monotonic()
        while self.args.max_samples is None or sample_count < self.args.max_samples:
            loop_started = time.monotonic()
            elapsed = sample_count * self.args.interval if self.args.max_samples else time.monotonic() - start
            sample = self.simulation.sample(
                elapsed=elapsed,
                runtime=self.runtime,
                no_frame_output=self.args.no_frame_output,
                frame_width=self.args.frame_width,
                frame_height=self.args.frame_height,
            )
            fps, latency = self.performance.update(loop_started, time.monotonic())
            sample["processing_fps"] = fps
            sample["latency_ms"] = latency
            self.emit(sample)
            sample_count += 1
            if self.args.interval > 0:
                time.sleep(self.args.interval)
        return 0

    def run_capture(self, mode: str) -> int:
        cap = self._open_capture(mode)
        if cap is None or not cap.isOpened():
            self.emit(invalid_sample(mode, "无法打开视频源或摄像头，请检查设备、权限或文件路径", "", 0.0, 0.0))
            return 1 if self.args.max_samples is None else 0

        face_mesh = None
        if mp is not None:
            face_mesh = mp.solutions.face_mesh.FaceMesh(
                static_image_mode=False,
                max_num_faces=1,
                refine_landmarks=True,
                min_detection_confidence=0.50,
                min_tracking_confidence=0.50,
            )
        haar = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
        sample_count = 0
        try:
            while self.args.max_samples is None or sample_count < self.args.max_samples:
                loop_started = time.monotonic()
                ok, frame = cap.read()
                if not ok or frame is None:
                    if mode == "video":
                        break
                    sample = invalid_sample(mode, "摄像头暂未返回有效帧", self.last_camera_frame_path)
                    fps, latency = self.performance.update(loop_started, time.monotonic())
                    sample["processing_fps"] = fps
                    sample["latency_ms"] = latency
                    self.emit(sample)
                    sample_count += 1
                    time.sleep(max(self.args.interval, 0.02))
                    continue

                frame = cv2.resize(frame, (self.args.frame_width, self.args.frame_height))
                healthy, mean, std, health_msg = camera_frame_health(frame)
                if not healthy:
                    frame_path = self._write_frame(frame, "invalid", 0, "camera_health")
                    fps, latency = self.performance.update(loop_started, time.monotonic())
                    self.emit(invalid_sample(mode, health_msg, frame_path, fps, latency))
                    sample_count += 1
                    time.sleep(max(self.args.interval, 0.02))
                    continue

                sample = self._process_frame(mode, frame, face_mesh, haar, loop_started, mean, std)
                self.emit(sample)
                sample_count += 1
                if self.args.interval > 0:
                    time.sleep(self.args.interval)
        finally:
            cap.release()
            if face_mesh is not None:
                face_mesh.close()
        return 0

    def _open_capture(self, mode: str):
        if mode == "video":
            return cv2.VideoCapture(self.args.source)
        source = int(self.args.source) if str(self.args.source).isdigit() else 0
        backends = [cv2.CAP_DSHOW, cv2.CAP_MSMF, cv2.CAP_ANY] if os.name == "nt" else [cv2.CAP_ANY]
        for backend in backends:
            cap = cv2.VideoCapture(source, backend)
            if cap.isOpened():
                cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.args.frame_width)
                cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.args.frame_height)
                return cap
            cap.release()
        return cv2.VideoCapture(source)

    def _process_frame(self, mode: str, frame: np.ndarray, face_mesh, haar, loop_started: float, mean: float, std: float) -> Dict[str, object]:
        h, w = frame.shape[:2]
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        timestamp = time.monotonic()
        if face_mesh is not None:
            results = face_mesh.process(rgb)
            if results.multi_face_landmarks:
                landmarks = results.multi_face_landmarks[0].landmark
                left_eye = [landmark_point(landmarks, idx, w, h) for idx in LEFT_EYE]
                right_eye = [landmark_point(landmarks, idx, w, h) for idx in RIGHT_EYE]
                mouth = [landmark_point(landmarks, idx, w, h) for idx in MOUTH]
                ear = (eye_aspect_ratio(left_eye) + eye_aspect_ratio(right_eye)) / 2.0
                mar = mouth_aspect_ratio(mouth)
                pitch, yaw, roll = solve_head_pose(landmarks, w, h)
                self.calibrator.update(ear, mar, True, "mediapipe_face_mesh")
                closed = ear < self.calibrator.ear_threshold
                self.state.update(closed, mar, timestamp, self.calibrator.mar_threshold)
                draw_mediapipe_landmarks(frame, landmarks)
                fps, latency = self.performance.update(loop_started, time.monotonic())
                temp_sample = build_sample(
                    mode=mode,
                    ear=ear,
                    mar=mar,
                    perclos=self.state.perclos,
                    blink_rate=self.state.blink_rate(timestamp),
                    yawn_count=self.state.yawn_count,
                    eye_closed_seconds=self.state.current_closed_seconds,
                    pitch=pitch,
                    yaw=yaw,
                    roll=roll,
                    adaptive_ear_threshold=self.calibrator.ear_threshold,
                    adaptive_mar_threshold=self.calibrator.mar_threshold,
                    quality_score=int(clamp(95 - abs(std - 55) * 0.15, 55, 98)),
                    calibration_progress=self.calibrator.progress,
                    frame_path="",
                    detector_backend="mediapipe_face_mesh",
                    feature_origin="measured",
                    measurement_valid=True,
                    perception_state="valid",
                    processing_fps=fps,
                    latency_ms=latency,
                )
                frame_path = self._write_frame(frame, temp_sample["level"], temp_sample["fatigue_score"], "mediapipe")
                temp_sample["frame_path"] = frame_path
                return temp_sample

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = haar.detectMultiScale(gray, scaleFactor=1.1, minNeighbors=4, minSize=(80, 80)) if not haar.empty() else []
        if len(faces) == 0:
            frame_path = self._write_frame(frame, "invalid", 18, "face_missing")
            fps, latency = self.performance.update(loop_started, time.monotonic())
            return face_missing_sample(mode, frame_path, fps, latency)

        x, y, fw, fh = sorted(faces, key=lambda item: item[2] * item[3], reverse=True)[0]
        cv2.rectangle(frame, (x, y), (x + fw, y + fh), (110, 210, 120), 2)
        phase = (timestamp % 30.0)
        ear = 0.22 + self.haar_rng.uniform(-0.025, 0.02)
        mar = 0.43 + self.haar_rng.uniform(-0.04, 0.08)
        closed = phase % 3.0 < 0.35
        if closed:
            ear = 0.16 + self.haar_rng.uniform(-0.02, 0.01)
        pitch = self.haar_rng.uniform(-8.0, 16.0)
        yaw = self.haar_rng.uniform(-14.0, 20.0)
        roll = self.haar_rng.uniform(-4.0, 4.0)
        self.state.update(closed, mar, timestamp, 0.62)
        fps, latency = self.performance.update(loop_started, time.monotonic())
        sample = build_sample(
            mode=mode,
            ear=ear,
            mar=mar,
            perclos=self.state.perclos,
            blink_rate=self.state.blink_rate(timestamp),
            yawn_count=self.state.yawn_count,
            eye_closed_seconds=self.state.current_closed_seconds,
            pitch=pitch,
            yaw=yaw,
            roll=roll,
            adaptive_ear_threshold=self.calibrator.ear_threshold,
            adaptive_mar_threshold=self.calibrator.mar_threshold,
            quality_score=int(clamp(65 - abs(std - 45) * 0.12, 35, 75)),
            calibration_progress=self.calibrator.progress,
            frame_path="",
            detector_backend="haar_cascade",
            feature_origin="degraded_simulated_features",
            measurement_valid=False,
            perception_state="degraded",
            processing_fps=fps,
            latency_ms=latency,
        )
        frame_path = self._write_frame(frame, sample["level"], sample["fatigue_score"], "haar")
        sample["frame_path"] = frame_path
        return sample

    def _write_frame(self, frame: np.ndarray, level: str, score: int, backend: str) -> str:
        draw_branding(frame, level, int(score), backend)
        if not self.args.no_frame_output:
            frame_path = write_runtime_frame(self.runtime, frame)
            if frame_path:
                self.last_camera_frame_path = frame_path
        return self.last_camera_frame_path


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DriveGuard-AI detector process")
    parser.add_argument("--mode", choices=["simulation", "camera", "video"], default="simulation")
    parser.add_argument("--source", default="0", help="video path or camera index")
    parser.add_argument("--runtime", default="runtime")
    parser.add_argument("--interval", type=float, default=0.08)
    parser.add_argument("--simulation-cycle", type=float, default=DEFAULT_SIMULATION_CYCLE_SECONDS)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--max-samples", type=int, default=None)
    parser.add_argument("--no-frame-output", action="store_true")
    parser.add_argument("--frame-width", type=int, default=DEFAULT_FRAME_WIDTH)
    parser.add_argument("--frame-height", type=int, default=DEFAULT_FRAME_HEIGHT)
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    runner = DetectorRunner(args)
    return runner.run()


if __name__ == "__main__":
    raise SystemExit(main())

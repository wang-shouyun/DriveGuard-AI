# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

import ast
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {
    ".cpp",
    ".h",
    ".hpp",
    ".py",
    ".ps1",
    ".bat",
    ".cmake",
    ".txt",
    ".md",
    ".json",
    ".yml",
    ".yaml",
    ".cff",
    ".qrc",
    ".rc",
    ".in",
}
IGNORED_DIRS = {"build", "runtime", ".git", "dist", ".venv", "venv"}


def iter_project_files():
    for path in ROOT.rglob("*"):
        if any(part in IGNORED_DIRS for part in path.relative_to(ROOT).parts):
            continue
        if path.is_file():
            yield path


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    errors = []
    required = [
        "LICENSE",
        "VERSION",
        "README.md",
        "DESIGN.md",
        "TEST_REPORT.md",
        "DELIVERY_GUIDE.md",
        "CHANGELOG.md",
        "AUTHORS.md",
        "THIRD_PARTY_NOTICES.md",
        "CITATION.cff",
        "CONTRIBUTING.md",
        "CODE_OF_CONDUCT.md",
        "SECURITY.md",
        "SUPPORT.md",
        "ROADMAP.md",
        "AGENTS.md",
        "docs/PROTOCOL.md",
        "docs/DEFENSE_GUIDE.md",
        "docs/DEMO_CHECKLIST.md",
        "docs/PRIVACY.md",
        "docs/KNOWN_LIMITATIONS.md",
        "schemas/detector-sample.schema.json",
        ".gitignore",
        ".gitattributes",
        ".editorconfig",
    ]
    for rel in required:
        if not (ROOT / rel).exists():
            errors.append(f"missing required file: {rel}")

    upload_notes = "GPT_UPLOAD" + "_NOTES.txt"
    if (ROOT / upload_notes).exists():
        errors.append(f"{upload_notes} should not be part of the open-source project")

    for path in iter_project_files():
        rel = path.relative_to(ROOT).as_posix()
        if "__pycache__" in path.parts:
            errors.append(f"__pycache__ found: {rel}")
        if path.suffix == ".pyc":
            errors.append(f"pyc found: {rel}")
        if path.suffix.lower() in TEXT_SUFFIXES:
            text = read_text(path)
            drive_pattern = r"\b" + "D" + r":[\\/]"
            if re.search(drive_pattern, text) and "lenovo_privacy_probe.ps1" not in rel:
                errors.append(f"personal absolute path found: {rel}")
            for banned in [
                "Anaconda3-2022" + " environment",
                "DriveGuard-AI" + " Pro",
                "All rights" + " reserved",
                "RAO JING" + " ORIGINAL",
                "GPT_UPLOAD" + "_NOTES",
            ]:
                if banned in text:
                    errors.append(f"banned text '{banned}' found: {rel}")
            if "\u7f6e\u4fe1\u5ea6" in text:
                errors.append(f"user-visible confidence wording found: {rel}")

    detector_path = ROOT / "scripts" / "detector.py"
    if detector_path.exists():
        tree = ast.parse(read_text(detector_path))
        count = sum(isinstance(node, ast.FunctionDef) and node.name == "decide_level" for node in ast.walk(tree))
        if count != 1:
            errors.append(f"decide_level definition count is {count}")

    version_path = ROOT / "VERSION"
    if version_path.exists():
        version = version_path.read_text(encoding="utf-8").strip()
        if version != "1.0.0":
            errors.append(f"VERSION expected 1.0.0, got {version!r}")
        authorship = ROOT / "assets" / "authorship.json"
        if authorship.exists() and version not in read_text(authorship):
            errors.append("authorship.json does not include VERSION")
        citation = ROOT / "CITATION.cff"
        if citation.exists() and version not in read_text(citation):
            errors.append("CITATION.cff does not include VERSION")

    if errors:
        print("FAIL: repository check")
        for error in errors:
            print(f"- {error}")
        return 1
    print("PASS: repository check")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Build the lightweight CrossMix Desktop SD-card installer archive."""

from __future__ import annotations

import argparse
import hashlib
import stat
import zipfile
from pathlib import Path


VERSION = "0.1.0"
PACKAGE_NAME = f"CrossMix-Desktop-v{VERSION}-TrimUI-Smart-Pro.zip"
TOP = "CrossMix-Desktop-Installer"
ROOT = Path(__file__).resolve().parents[2]
PACKAGE_DIR = ROOT / "packaging" / "desktop-installer"

RUNTIME_FILES = (
    "config.json",
    "launch.sh",
    "README.md",
    "LICENSE-font.txt",
    "font.ttf",
    "icon.png",
    "bin/desktop",
)
DATA_FILES = (
    "README.md",
    "Workspace/hello.py",
    "Workspace/hello.js",
)
EXECUTABLES = {
    f"{TOP}/install.sh",
    f"{TOP}/payload/Apps/Desktop/launch.sh",
    f"{TOP}/payload/Apps/Desktop/bin/desktop",
}


def add_file(archive: zipfile.ZipFile, source: Path, archive_name: str) -> None:
    if not source.is_file():
        raise FileNotFoundError(f"Required package file is missing: {source}")
    info = zipfile.ZipInfo(archive_name.replace("\\", "/"))
    info.create_system = 3
    mode = 0o755 if archive_name in EXECUTABLES else 0o644
    info.external_attr = (stat.S_IFREG | mode) << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    archive.writestr(info, source.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def build(output: Path) -> tuple[int, str]:
    app_dir = ROOT / "Apps" / "Desktop"
    data_dir = ROOT / "Data" / "Desktop"
    output.parent.mkdir(parents=True, exist_ok=True)

    with zipfile.ZipFile(output, "w") as archive:
        add_file(archive, PACKAGE_DIR / "README-Install.txt", f"{TOP}/README-Install.txt")
        add_file(archive, PACKAGE_DIR / "install.sh", f"{TOP}/install.sh")
        for relative in RUNTIME_FILES:
            add_file(archive, app_dir / relative, f"{TOP}/payload/Apps/Desktop/{relative}")
        for relative in DATA_FILES:
            add_file(archive, data_dir / relative, f"{TOP}/payload/Data/Desktop/{relative}")

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    return output.stat().st_size, digest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT.parent / PACKAGE_NAME,
        help="ZIP destination (default: next to the checkout)",
    )
    args = parser.parse_args()
    size, digest = build(args.output.resolve())
    print(f"Created: {args.output.resolve()}")
    print(f"Size: {size:,} bytes")
    print(f"SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Fetch the official MoltenVK prebuilts for local Apple builds.

Homebrew's molten-vk bottle is not built with MoltenVK private API support.
The official release ships a macOS private-API tarball that avoids runtime
warnings around primitive restart on pipelines that disable it.

This script extracts the macOS dynamic library, the iOS dynamic framework, and
the public MoltenVK headers into third_party/MoltenVK/. That directory is
intentionally gitignored because it contains binary dependencies.
"""

import argparse
import hashlib
import shutil
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

DEFAULT_VERSION = "v1.4.1"

REPO_ROOT = Path(__file__).resolve().parent.parent
DEST_DIR = REPO_ROOT / "third_party" / "MoltenVK"


def _download(url: str, tar_name: str, tmp_dir: Path) -> Path:
    tar_path = tmp_dir / tar_name
    print(f"Downloading {url}")
    with urllib.request.urlopen(url) as response, open(tar_path, "wb") as tar_file:
        shutil.copyfileobj(response, tar_file)
    return tar_path


def _extract_member_to_file(tar: tarfile.TarFile, member_path: str,
                            output_path: Path) -> None:
    member = tar.getmember(member_path)
    with tar.extractfile(member) as src:
        if src is None:
            raise RuntimeError(f"{member_path} is missing in tarball")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_bytes(src.read())


def _extract_headers(tar: tarfile.TarFile) -> None:
    for header_path in (
        "MoltenVK/MoltenVK/include/MoltenVK/mvk_config.h",
        "MoltenVK/MoltenVK/include/MoltenVK/mvk_datatypes.h",
        "MoltenVK/MoltenVK/include/MoltenVK/mvk_deprecated_api.h",
        "MoltenVK/MoltenVK/include/MoltenVK/mvk_private_api.h",
        "MoltenVK/MoltenVK/include/MoltenVK/mvk_vulkan.h",
        "MoltenVK/MoltenVK/include/MoltenVK/vk_mvk_moltenvk.h",
    ):
        try:
            _extract_member_to_file(
                tar, header_path,
                DEST_DIR / "include" / "MoltenVK" / Path(header_path).name)
        except KeyError:
            continue


def _extract_directory(tar: tarfile.TarFile, source_prefix: str,
                       output_dir: Path) -> None:
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    matched = False
    for member in tar.getmembers():
        if not member.name.startswith(source_prefix):
            continue
        relative = Path(member.name[len(source_prefix):])
        if not relative.parts:
            continue
        matched = True
        destination = output_dir / relative
        if member.isdir():
            destination.mkdir(parents=True, exist_ok=True)
            continue
        if not member.isfile():
            continue
        with tar.extractfile(member) as src:
            if src is None:
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(src.read())
    if not matched:
        raise RuntimeError(f"{source_prefix} is missing in tarball")


def _fetch_macos(version: str, tmp_dir: Path) -> None:
    macos_url = (
        "https://github.com/KhronosGroup/MoltenVK/releases/download/"
        f"{version}/MoltenVK-macos-privateapi.tar"
    )

    tar_path = _download(macos_url, "MoltenVK-macos-privateapi.tar", tmp_dir)
    print("Extracting macOS dylib and headers")
    with tarfile.open(tar_path, "r") as tar:
        _extract_member_to_file(
            tar,
            "MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib",
            DEST_DIR / "lib" / "libMoltenVK.dylib")
        _extract_headers(tar)

    dylib_path = DEST_DIR / "lib" / "libMoltenVK.dylib"
    digest = hashlib.sha256(dylib_path.read_bytes()).hexdigest()[:16]
    print(f"Installed {dylib_path} (sha256: {digest}...)")


def _fetch_ios(version: str, tmp_dir: Path) -> None:
    ios_url = (
        "https://github.com/KhronosGroup/MoltenVK/releases/download/"
        f"{version}/MoltenVK-ios.tar"
    )

    tar_path = _download(ios_url, "MoltenVK-ios.tar", tmp_dir)
    print("Extracting iOS framework and headers")
    with tarfile.open(tar_path, "r") as tar:
        _extract_directory(
            tar,
            "MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/"
            "MoltenVK.framework/",
            DEST_DIR / "ios" / "MoltenVK.framework")
        _extract_headers(tar)

    framework_binary = DEST_DIR / "ios" / "MoltenVK.framework" / "MoltenVK"
    digest = hashlib.sha256(framework_binary.read_bytes()).hexdigest()[:16]
    print(f"Installed {framework_binary} (sha256: {digest}...)")


def fetch(version: str, platform: str) -> None:
    DEST_DIR.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        if platform in ("all", "macos"):
            _fetch_macos(version, tmp_dir)
        if platform in ("all", "ios"):
            _fetch_ios(version, tmp_dir)

    (DEST_DIR / "VERSION").write_text(version + "\n")
    print("Re-run CMake/build to bundle the local MoltenVK artifact into the app.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        default=DEFAULT_VERSION,
        help=f"MoltenVK release tag to fetch (default: {DEFAULT_VERSION}).",
    )
    parser.add_argument(
        "--platform",
        choices=("all", "macos", "ios"),
        default="all",
        help="Apple platform artifact to fetch (default: all).",
    )
    args = parser.parse_args()
    try:
        fetch(args.version, args.platform)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

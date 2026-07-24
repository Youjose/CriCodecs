from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(*arguments: str) -> None:
    cmake = shutil.which("cmake")
    command = [cmake] if cmake else [sys.executable, "-m", "cmake"]
    subprocess.run([*command, *arguments], check=True)


root = Path(__file__).resolve().parent.parent
build = root / "build" / "wheel-core-build"
install = root / "build" / "wheel-core-install"

configure = [
    "-S",
    str(root),
    "-B",
    str(build),
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON",
    "-DCRICODECS_BUILD_PYTHON=OFF",
    "-DCRICODECS_BUILD_CLI=OFF",
    "-DCRICODECS_INCLUDE_CLI_API=ON",
    "-DCRICODECS_BUILD_CRISTUDIO=OFF",
    "-DCRICODECS_INSTALL_CPP=ON",
    "-DBUILD_SHARED_LIBS=OFF",
]

if os.name == "nt":
    requested_arch = os.environ.get(
        "CIBW_ARCHS", os.environ.get("PROCESSOR_ARCHITECTURE", "")
    ).upper()
    generator_arch = {
        "AMD64": "x64",
        "X86_64": "x64",
        "ARM64": "ARM64",
    }.get(requested_arch)
    if generator_arch is None:
        raise SystemExit(f"unsupported Windows wheel architecture: {requested_arch or '<unset>'}")
    configure.extend([
        "-G",
        "Visual Studio 18 2026",
        "-A",
        generator_arch,
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
    ])
else:
    configure.extend(["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"])
    launcher = (
        os.environ.get("CRICODECS_WHEEL_COMPILER_LAUNCHER")
        or os.environ.get("SCCACHE_PATH")
        or shutil.which("sccache")
    )
    if launcher:
        configure.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={launcher}")

run(*configure)
if os.name == "nt":
    compiler_file = next((build / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"), None)
    compiler_text = compiler_file.read_text(encoding="utf-8") if compiler_file else ""
    compiler_id = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)', compiler_text)
    compiler_arch = re.search(
        r'set\(CMAKE_CXX_COMPILER_ARCHITECTURE_ID "([^"]+)"\)', compiler_text
    )
    actual_id = compiler_id.group(1) if compiler_id else "<unknown>"
    actual_arch = compiler_arch.group(1) if compiler_arch else "<unknown>"
    if actual_id != "MSVC" or actual_arch.lower() != generator_arch.lower():
        raise SystemExit(
            f"wheel core requires MSVC {generator_arch}, got {actual_id} {actual_arch}"
        )
run("--build", str(build), "--config", "Release", "--target", "CriCodecs", "--parallel", "4")
run("--install", str(build), "--config", "Release", "--prefix", str(install))

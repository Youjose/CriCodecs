from __future__ import annotations

import os
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
    "-G",
    "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON",
    "-DCRICODECS_BUILD_TESTS=OFF",
    "-DCRICODECS_BUILD_PYTHON=OFF",
    "-DCRICODECS_BUILD_CLI=OFF",
    "-DCRICODECS_INCLUDE_CLI_API=ON",
    "-DCRICODECS_BUILD_CRISTUDIO=OFF",
    "-DCRICODECS_INSTALL_CPP=ON",
    "-DBUILD_SHARED_LIBS=OFF",
]

launcher = (
    os.environ.get("CRICODECS_WHEEL_COMPILER_LAUNCHER")
    or os.environ.get("SCCACHE_PATH")
    or shutil.which("sccache")
)
if launcher:
    configure.append(f"-DCMAKE_CXX_COMPILER_LAUNCHER={launcher}")
if os.name == "nt":
    configure.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")

run(*configure)
run("--build", str(build), "--config", "Release", "--target", "CriCodecs", "--parallel", "4")
run("--install", str(build), "--config", "Release", "--prefix", str(install))

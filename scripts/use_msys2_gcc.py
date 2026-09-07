# scripts/use_msys2_gcc.py — prefer MSYS2's g++15 on Windows
import os
import platform

IS_WINDOWS = platform.system().lower().startswith("win")
MSYS_GCC = r"D:\msys64\ucrt64\bin"

if IS_WINDOWS and os.path.isdir(MSYS_GCC):
    os.environ["PATH"] = MSYS_GCC + os.pathsep + os.environ.get("PATH", "")

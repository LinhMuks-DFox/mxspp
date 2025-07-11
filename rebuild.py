#!/usr/bin/env python3

import argparse
import pathlib
import shutil
import subprocess
import sys

# --- Configuration ---
BUILD_DIR = pathlib.Path("build")
BIN_DIR = pathlib.Path("bin")

def clean_directories():
    """彻底删除构建和输出目录"""
    print("--- Cleaning up old directories ---")
    for directory in [BUILD_DIR, BIN_DIR]:
        if directory.exists():
            print(f"Removing existing directory: {directory}")
            try:
                shutil.rmtree(directory)
            except OSError as e:
                print(f"Error: Failed to remove {directory}. Reason: {e}", file=sys.stderr)
                sys.exit(1)
        else:
            print(f"Directory {directory} does not exist, skipping removal.")

def run_cmake_configure():
    """运行 CMake 配置步骤"""
    print(f"\n--- Running CMake in '{BUILD_DIR}' to configure the project ---")

    # 确保 build 目录存在
    BUILD_DIR.mkdir(exist_ok=True)

    # --- 【修改】 ---
    # 1. 检查 Ninja 是否安装
    if not shutil.which("ninja"):
        print("Error: `ninja` command not found. Is Ninja build system installed and in your PATH?", file=sys.stderr)
        return False
        
    # 2. 在 CMake 命令中通过 -G 指定使用 Ninja
    cmake_configure_command = [
        "cmake",
        "-G", "Ninja",
        "-D", f"CMAKE_TOOLCHAIN_FILE={pathlib.Path('..') / 'toolchain.cmake'}",
        ".."
    ]
    # --- 【修改结束】 ---
    
    try:
        # `cwd` 参数让命令在指定目录中运行
        subprocess.run(cmake_configure_command, cwd=BUILD_DIR, check=True)
        print("\n--- CMake configuration successful ---")
        return True
    except FileNotFoundError:
        print("Error: `cmake` command not found. Is CMake installed and in your PATH?", file=sys.stderr)
        return False
    except subprocess.CalledProcessError as e:
        print(f"\nError: CMake configuration failed with exit code {e.returncode}.", file=sys.stderr)
        return False

def run_cmake_build():
    """运行 CMake 构建步骤 (编译)"""
    print(f"\n--- Running build process in '{BUILD_DIR}' ---")
    # 此命令会自动使用已配置的生成器 (Ninja, Make, etc.)
    cmake_build_command = ["cmake", "--build", "."]

    try:
        subprocess.run(cmake_build_command, cwd=BUILD_DIR, check=True)
        print("\n--- Project build successful ---")
        print(f"Executables and libraries are located in '{BIN_DIR}'.")
    except subprocess.CalledProcessError as e:
        print(f"\nError: Project build failed with exit code {e.returncode}.", file=sys.stderr)
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(
        description="Builds the project. Default is incremental build. Use --clean for a full rebuild."
    )
    parser.add_argument(
        "-c", '--clean',
        action='store_true',
        help='Perform a full clean rebuild by deleting the build and bin directories before configuration and compilation.'
    )
    args = parser.parse_args()

    if args.clean:
        print("=== Starting a FULL CLEAN REBUILD ===")
        clean_directories()
        BIN_DIR.mkdir(exist_ok=True) # 确保 bin 目录存在
        if not run_cmake_configure():
            sys.exit(1)
        run_cmake_build()
    else:
        print("=== Starting an INCREMENTAL BUILD ===")
        # 检查项目是否已经配置过。如果没有，则先进行配置。
        # 一个好的检查方法是查看 Makefile 或 build.ninja 是否存在。
        if not BUILD_DIR.exists() or not any((BUILD_DIR / f).exists() for f in ["Makefile", "build.ninja"]):
            print("Project has not been configured yet. Running configuration first...")
            if not run_cmake_configure():
                sys.exit(1)
        
        run_cmake_build()


if __name__ == "__main__":
    main()
#!/usr/bin/env python3

import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import re
import argparse
import zipfile
import platform # 引入 platform 模块

# ==============================================================================
# Configuration
# ==============================================================================

# --- Directories ---
LIB_DIR = pathlib.Path("lib")
VENV_DIR = pathlib.Path(".venv")
REQUIREMENTS_FILE = "requirements.txt"

# --- LLVM Configuration ---
LLVM_VERSION_TAG = "llvmorg-20.1.8"
LLVM_CONFIG = {
    "source": {
        "url": f"https://github.com/llvm/llvm-project/archive/refs/tags/{LLVM_VERSION_TAG}.tar.gz",
        "archive_name": f"{LLVM_VERSION_TAG}.tar.gz",
        "inner_dir_prefix": "llvm-project-",
        "target_dir_name": "llvm_src",
    },
    "prebuilt": {
        "linux": {
            "url": f"https://github.com/llvm/llvm-project/releases/download/{LLVM_VERSION_TAG}/LLVM-20.1.8-Linux-X64.tar.xz",
            "archive_name": "LLVM-20.1.8-Linux-X64.tar.xz",
            "inner_dir_prefix": "LLVM-20.1.8-Linux-X64",
            "target_dir_name": "llvm",
        },
        "darwin": { # macOS
            "url": f"https://github.com/llvm/llvm-project/releases/download/{LLVM_VERSION_TAG}/LLVM-20.1.8-macOS-ARM64.tar.xz",
            "archive_name": "LLVM-20.1.8-macOS-ARM64.tar.xz",
            "inner_dir_prefix": "LLVM-20.1.8-macOS-ARM64",
            "target_dir_name": "llvm",
        },
        "win32": { # Windows
            "url": f"https://github.com/llvm/llvm-project/releases/download/{LLVM_VERSION_TAG}/clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz",
            "archive_name": "clang+llvm-20.1.8-x86_64-pc-windows-msvc.tar.xz",
            "inner_dir_prefix": "clang+llvm-20.1.8-x86_64-pc-windows-msvc",
            "target_dir_name": "llvm",
        }
    }
}

# --- PEGTL Configuration ---
PEGTL_VERSION_TAG = "3.2.7"
PEGTL_CONFIG = {
    "url": f"https://github.com/taocpp/pegtl/archive/refs/tags/{PEGTL_VERSION_TAG}.zip",
    "archive_name": f"{PEGTL_VERSION_TAG}.zip",
    "inner_dir_prefix": f"pegtl-{PEGTL_VERSION_TAG}",
    "target_dir_name": "pegtl"
}


# ==============================================================================
# Helper Functions for Colored Output
# ==============================================================================

class Colors:
    """ANSI color codes"""
    BLUE = '\033[0;34m'
    GREEN = '\033[0;32m'
    RED = '\033[0;31m'
    YELLOW = '\033[0;33m'
    NC = '\033[0m'

def info(msg):
    print(f"{Colors.BLUE}[INFO]{Colors.NC} {msg}")

def success(msg):
    print(f"{Colors.GREEN}[SUCCESS]{Colors.NC} {msg}")

def warn(msg):
    print(f"{Colors.YELLOW}[WARNING]{Colors.NC} {msg}")

def error(msg):
    print(f"{Colors.RED}[ERROR]{Colors.NC} {msg}", file=sys.stderr)

# ==============================================================================
# Dependency Setup Functions
# ==============================================================================

def setup_venv():
    """Checks for and sets up the Python virtual environment."""
    info("Checking for Python virtual environment...")
    if VENV_DIR.is_dir():
        info(f"Virtual environment '{VENV_DIR}' already exists.")
        return

    info(f"'{VENV_DIR}' not found. Creating virtual environment...")
    try:
        subprocess.run([sys.executable, "-m", "venv", str(VENV_DIR)], check=True)
        info(f"Virtual environment created in '{VENV_DIR}'.")

        if pathlib.Path(REQUIREMENTS_FILE).is_file():
            info(f"Found {REQUIREMENTS_FILE}. Installing packages...")
            pip_path = "Scripts" if sys.platform == "win32" else "bin"
            pip_executable = VENV_DIR / pip_path / "pip"
            subprocess.run([str(pip_executable), "install", "-r", REQUIREMENTS_FILE], check=True)
            success("Python packages installed.")
        else:
            warn(f"'{REQUIREMENTS_FILE}' not found. Skipping package installation.")
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        error(f"Failed to set up virtual environment: {e}")
        sys.exit(1)

def check_system_dependencies():
    """Checks for compilers and libraries based on the OS."""
    info("Checking system compiler and library dependencies...")
    all_ok = True
    for cmd in ["clang", "clang++"]:
        if not shutil.which(cmd):
            error(f"Compiler '{cmd}' not found. Please install clang suite (version >= 20).")
            all_ok = False
            continue
        try:
            result = subprocess.run([cmd, "--version"], capture_output=True, text=True, check=True)
            version_line = result.stdout.splitlines()[0]
            match = re.search(r"version\s+(\d+)\.", version_line)
            if match and int(match.group(1)) < 20:
                error(f"Found {cmd} version {match.group(1)}, but version >= 20 is required.")
                all_ok = False
            elif match:
                info(f"Found {cmd} version {match.group(1)}. (OK)")
            else:
                warn(f"Could not determine version for {cmd}. Please verify it is >= 20.")
        except (subprocess.CalledProcessError, FileNotFoundError) as e:
            error(f"Could not execute '{cmd} --version': {e}")
            all_ok = False

    if sys.platform == "linux":
        try:
            result = subprocess.run(["ldconfig", "-p"], capture_output=True, text=True, check=True)
            if "libc++" not in result.stdout:
                error("libc++ not found in linker cache. Please install libc++.")
                all_ok = False
            else:
                info("Found libc++. (OK)")
        except (subprocess.CalledProcessError, FileNotFoundError):
            warn("`ldconfig` command not found or failed. Cannot check for libc++. Please verify it is installed.")
    
    if not all_ok:
        error("System dependency checks failed. Please resolve the issues mentioned above.")
        sys.exit(1)
    
    success("System dependencies look good.")


def download_and_extract(url, archive_name, target_dir, inner_dir_prefix, final_dir_name):
    """Downloads and extracts a given archive, then renames the extracted directory."""
    archive_path = target_dir / archive_name
    final_target_path = target_dir / final_dir_name
    
    # Check if final directory already exists
    if final_target_path.is_dir():
        success(f"Directory '{final_target_path}' already exists. Skipping.")
        return

    # Download
    try:
        def show_progress(block_num, block_size, total_size):
            downloaded = block_num * block_size
            percent = downloaded * 100 / total_size if total_size > 0 else 0
            sys.stdout.write(f"\rDownloading {archive_name}: {int(percent)}% complete")
            sys.stdout.flush()

        info(f"Downloading to '{archive_path}'...")
        urllib.request.urlretrieve(url, archive_path, show_progress)
        sys.stdout.write("\n")
        success("Download complete.")
    except Exception as e:
        error(f"Failed to download from {url}: {e}")
        sys.exit(1)

    # Extract
    extracted_path_name = None
    try:
        info(f"Extracting '{archive_path}'...")
        # Find the top-level directory name from the archive without full extraction first
        if archive_path.suffix == ".zip":
            with zipfile.ZipFile(archive_path, 'r') as zip_ref:
                top_level_dirs = {name.split('/')[0] for name in zip_ref.namelist()}
                for d in top_level_dirs:
                    if d.startswith(inner_dir_prefix):
                        extracted_path_name = d
                        break
                zip_ref.extractall(target_dir)

        elif str(archive_path).endswith((".tar.xz", ".tar.gz")):
            mode = "r:xz" if str(archive_path).endswith(".tar.xz") else "r:gz"
            with tarfile.open(archive_path, mode) as tar:
                top_level_dirs = {member.name.split('/')[0] for member in tar.getmembers()}
                for d in top_level_dirs:
                    if d.startswith(inner_dir_prefix):
                        extracted_path_name = d
                        break
                tar.extractall(path=target_dir)
        else:
            raise NotImplementedError(f"Extraction for {archive_path.suffix} not supported.")
        success(f"Archive extracted to '{target_dir}'.")
        
        # Rename
        if extracted_path_name:
            extracted_path = target_dir / extracted_path_name
            if extracted_path.is_dir():
                info(f"Renaming '{extracted_path.name}' to '{final_dir_name}'...")
                shutil.move(str(extracted_path), str(final_target_path))
                success(f"Renamed to '{final_target_path}'.")
        else:
            warn("Could not determine the top-level extracted directory for renaming.")

    except (tarfile.TarError, zipfile.BadZipFile, NotImplementedError) as e:
        error(f"Failed to extract archive: {e}")
        sys.exit(1)
    finally:
        info(f"Removing archive '{archive_path}'...")
        os.remove(archive_path)


def setup_prebuilt_llvm():
    """Downloads and sets up the pre-built LLVM for the current OS."""
    if sys.platform not in LLVM_CONFIG["prebuilt"]:
        error(f"No pre-built LLVM available for your OS: {sys.platform}. Please use --build-from-source.")
        sys.exit(1)
    
    config = LLVM_CONFIG["prebuilt"][sys.platform]
    install_path = LIB_DIR / config["target_dir_name"]

    if install_path.is_dir():
        success(f"LLVM already found at '{install_path}'. Skipping download.")
        return

    info(f"Setting up pre-built LLVM for {sys.platform}...")
    LIB_DIR.mkdir(exist_ok=True)
    download_and_extract(
        config["url"], 
        config["archive_name"], 
        LIB_DIR, 
        config["inner_dir_prefix"], 
        config["target_dir_name"]
    )
    success(f"Pre-built LLVM is ready at '{install_path}'.")

def build_llvm_from_source():
    """Downloads LLVM source and builds it using CMake."""
    warn("Building LLVM from source is a long and resource-intensive process.")
    warn("This can take over an hour and require >30GB of disk space.")
    if input("Do you want to continue? (y/N): ").lower() != 'y':
        info("Build cancelled by user.")
        sys.exit(0)

    if not all(shutil.which(cmd) for cmd in ["cmake", "ninja"]):
        error("Build requires `cmake` and `ninja`. Please install them and ensure they are in your PATH.")
        sys.exit(1)
    info("Found build tools: cmake, ninja. (OK)")

    config = LLVM_CONFIG["source"]
    source_path = LIB_DIR / config["target_dir_name"]
    # MODIFIED: Install to the same 'llvm' directory as prebuilt versions
    install_path = LIB_DIR / "llvm"
    
    if install_path.is_dir():
        success(f"Custom built LLVM already found at '{install_path}'. Skipping build.")
        return

    LIB_DIR.mkdir(exist_ok=True)
    if not source_path.is_dir():
        download_and_extract(
            config["url"], 
            config["archive_name"], 
            LIB_DIR, 
            config["inner_dir_prefix"], 
            config["target_dir_name"]
        )
    
    # MODIFIED: Determine target architecture dynamically
    arch = platform.machine().lower()
    if "x86_64" in arch or "amd64" in arch:
        llvm_target = "X86"
    elif "aarch64" in arch or "arm64" in arch:
        llvm_target = "AArch64"
    else:
        error(f"Unsupported architecture for LLVM build: {arch}. Please add it to the script.")
        sys.exit(1)
    info(f"Detected architecture '{arch}'. Setting LLVM target to '{llvm_target}'.")

    build_dir = source_path / "build"
    build_dir.mkdir(exist_ok=True)
    
    try:
        info("Configuring LLVM with CMake...")
        cmake_args = [
            "cmake",
            "-S", str(source_path / "llvm"),
            "-B", str(build_dir),
            "-G", "Ninja",
            f"-DCMAKE_INSTALL_PREFIX={install_path}",
            "-DLLVM_ENABLE_PROJECTS=",
            "-DLLVM_BUILD_TOOLS=OFF",
            "-DLLVM_BUILD_UTILS=OFF",
            "-DLLVM_INCLUDE_TESTS=OFF",
            "-DLLVM_INCLUDE_EXAMPLES=OFF",
            "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi;libunwind",
            "-DLLVM_ENABLE_LIBCXX=ON",
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_CXX_COMPILER=clang++",
            "-DCMAKE_C_FLAGS=-stdlib=libc++",
            "-DCMAKE_CXX_FLAGS=-stdlib=libc++ -std=c++23",
            "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -lc++abi -lunwind",
            "-DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++ -lc++abi -lunwind",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DLLVM_TARGETS_TO_BUILD={llvm_target}", # Use dynamic target
        ]
        # Only run cmake configure step if not already cached
        if not (build_dir / "build.ninja").exists():
            subprocess.run(cmake_args, check=True)
        else:
            info("CMake cache found. Skipping configuration.")
        
        info("Building and installing LLVM... (This will take a long time)")
        subprocess.run(["cmake", "--build", str(build_dir), "--target", "install"], check=True)
        
        success(f"LLVM successfully built and installed at '{install_path}'.")

        # MODIFIED: Clean up build directory ONLY on success
        info("Cleaning up build directory to save space...")
        shutil.rmtree(build_dir, ignore_errors=True)

    except subprocess.CalledProcessError as e:
        error(f"LLVM build failed: {e}")
        error(f"Build directory '{build_dir}' is kept for inspection and incremental builds.")
        sys.exit(1)

def setup_pegtl():
    """Downloads and sets up the PEGTL header-only library."""
    config = PEGTL_CONFIG
    install_path = LIB_DIR / config["target_dir_name"]

    if install_path.is_dir():
        success(f"PEGTL already found at '{install_path}'. Skipping download.")
        return

    info("Setting up PEGTL...")
    LIB_DIR.mkdir(exist_ok=True)
    download_and_extract(
        config["url"], 
        config["archive_name"], 
        LIB_DIR, 
        config["inner_dir_prefix"], 
        config["target_dir_name"]
    )
    success(f"PEGTL is ready at '{install_path}'.")

# ==============================================================================
# Main Execution
# ==============================================================================

def main():
    parser = argparse.ArgumentParser(description="Download and set up project dependencies.")
    parser.add_argument(
        "-b", "--build-from-source",
        action="store_true",
        help="Build LLVM from source instead of downloading a pre-built binary. Requires cmake and ninja."
    )
    # MODIFIED: Added short option -lvd
    parser.add_argument(
        "-lvd", "--llvm-download",
        action="store_true",
        help="Force download and setup of LLVM (either pre-built or from source)."
    )
    args = parser.parse_args()

    try:
        setup_venv()
        print()
        check_system_dependencies()
        print()

        if args.llvm_download:
            if args.build_from_source:
                build_llvm_from_source()
            else:
                setup_prebuilt_llvm()
        else:
            info("Skipping LLVM setup. Use --llvm-download (or -lvd) to force it.")
        
        print()
        setup_pegtl()
        
        print()
        success("Project dependencies are successfully set up.")

    except KeyboardInterrupt:
        error("\nScript interrupted by user.")
        sys.exit(130)
    except SystemExit as e:
        sys.exit(e.code)
    except Exception as e:
        error(f"An unexpected error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
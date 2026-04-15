#!/usr/bin/env python3

# Copyright 2025 Ben Vanik. All Rights Reserved.

"""Main build script and tooling for xenia.

Run with --help or no arguments for possible commands.
"""
from multiprocessing import Pool
from functools import partial
from argparse import ArgumentParser, ArgumentTypeError
from glob import glob
from json import loads as jsonloads
import os
import platform
from shutil import rmtree
import subprocess
import sys
import stat
import enum

__author__ = "ben.vanik@gmail.com (Ben Vanik)"


self_path = os.path.dirname(os.path.abspath(__file__))

class bcolors:
#    HEADER = "\033[95m"
#    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
#    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
#    BOLD = "\033[1m"
#    UNDERLINE = "\033[4m"

def print_error(text: str):
    print(f"{bcolors.FAIL}ERROR: {text}{bcolors.ENDC}")

def print_warning(text: str):
    print(f"{bcolors.WARNING}WARNING: {text}{bcolors.ENDC}")

class ResultStatus(enum.Enum):
    SUCCESS = enum.auto()
    FAILURE = enum.auto()

def print_status(status: ResultStatus):
    match status:
        case ResultStatus.SUCCESS:
            print(f"{bcolors.OKCYAN}Success!{bcolors.ENDC}")
        case ResultStatus.FAILURE:
            print(f"{bcolors.FAIL}Error!{bcolors.ENDC}")


# Detect if building on Android via Termux.
host_linux_platform_is_android = False
if sys.platform == "linux":
    try:
        host_linux_platform_is_android = subprocess.Popen(
            ["uname", "-o"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True).communicate()[0] == "Android\n"
    except Exception:
        pass


def import_subprocess_environment(args):
    popen = subprocess.Popen(
        args, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    variables, _ = popen.communicate()

    envvars_to_save = (
        "DEVENVDIR",
        "INCLUDE",
        "LIB",
        "LIBPATH",
        "PATH",
        "PATHEXT",
        "SYSTEMROOT",
        "TEMP",
        "TMP",
        "VCINSTALLDIR",
        "WindowsSdkDir",
        "PROGRAMFILES",
        "ProgramFiles(x86)",
        "VULKAN_SDK"
        "CC",
        "CXX",
        )

    # Extract and parse environment variables from stdout
    for line in variables.splitlines():
        if line.find("=") != -1:
            for envvar in envvars_to_save:
                var, setting = line.split("=", 1)

                var = var.upper()
                envvar = envvar.upper()

                if envvar == var:
                    if envvar == "PATH":
                        setting = f"{os.path.dirname(sys.executable)}{os.pathsep}{setting}"

                    os.environ[var] = setting
                    break

VSVERSION_MINIMUM = 2022
def import_vs_environment():
    """Finds the installed Visual Studio version and imports
    interesting environment variables into os.environ.

    Returns:
      A version such as 2022 or None if no installation is found.
    """

    if sys.platform != "win32":
        return None

    version = None
    install_path = None
    env_tool_args = None

    vswhere = subprocess.check_output(
        "tools/vswhere/vswhere.exe -version \"[17,)\" -latest -prerelease -format json -utf8 -products"
        " Microsoft.VisualStudio.Product.Enterprise"
        " Microsoft.VisualStudio.Product.Professional"
        " Microsoft.VisualStudio.Product.Community"
        " Microsoft.VisualStudio.Product.BuildTools",
        encoding="utf-8",
    )
    if vswhere:
        vswhere = jsonloads(vswhere)
    if vswhere and len(vswhere) > 0:
        # Map internal version to year version: 17->2022, 18->2026, etc.
        internal_version = int(vswhere[0].get("catalog", {}).get("productLineVersion", 17))
        version_map = {17: 2022, 18: 2026}
        version = version_map.get(internal_version, VSVERSION_MINIMUM)
        install_path = vswhere[0].get("installationPath", None)

    vsdevcmd_path = os.path.join(install_path, "Common7", "Tools", "VsDevCmd.bat")
    if os.access(vsdevcmd_path, os.X_OK):
        env_tool_args = [vsdevcmd_path, "-arch=amd64", "-host_arch=amd64", "&&", "set"]
    else:
        vcvars_path = os.path.join(install_path, "VC", "Auxiliary", "Build", "vcvarsall.bat")
        env_tool_args = [vcvars_path, "x64", "&&", "set"]

    if not version:
        return None

    import_subprocess_environment(env_tool_args)
    os.environ["VSVERSION"] = f"{version}"
    return version


vs_version = import_vs_environment()

default_branch = "canary_experimental"

def setup_vulkan_sdk():
    """Setup Vulkan SDK environment variables if not already set.

    Returns:
        True if Vulkan SDK is available and valid, False otherwise.
    """
    # Check if VULKAN_SDK is already set and valid
    existing_vulkan_sdk = os.environ.get("VULKAN_SDK")
    if existing_vulkan_sdk:
        # Validate that the path exists
        if os.path.exists(existing_vulkan_sdk):
            # Check if spirv-opt is accessible
            if has_bin("spirv-opt"):
                print(f"VULKAN_SDK is set to {existing_vulkan_sdk}")
                return True
            print_warning(f"VULKAN_SDK is set to {existing_vulkan_sdk} but spirv-opt not found in PATH")
        else:
            print_warning(f"VULKAN_SDK is set to {existing_vulkan_sdk} but directory does not exist")
        return False

    if sys.platform != "win32":
        # On Linux, find spirv-opt in PATH and set VULKAN_SDK based on its location
        spirv_opt_path = get_bin("spirv-opt")
        if spirv_opt_path:
            # spirv-opt is typically in $VULKAN_SDK/bin/, so get parent directory
            spirv_bin_dir = os.path.dirname(spirv_opt_path)
            vulkan_sdk = os.path.dirname(spirv_bin_dir)
            os.environ["VULKAN_SDK"] = vulkan_sdk
            print(f"Found Vulkan SDK at {vulkan_sdk} (from spirv-opt location)")
            return True
        return False

    # Windows: Check if Vulkan SDK is installed at the default location
    vulkan_base = "C:\\VulkanSDK"
    if not os.path.exists(vulkan_base):
        return False

    # Get the first (latest) version directory
    try:
        subdirs = [d for d in os.listdir(vulkan_base)
                   if os.path.isdir(os.path.join(vulkan_base, d))]
        if not subdirs:
            return False

        vulkan_sdk = os.path.join(vulkan_base, subdirs[0])
        vulkan_bin = os.path.join(vulkan_sdk, "Bin")

        os.environ["VULKAN_SDK"] = vulkan_sdk
        os.environ["PATH"] = f"{vulkan_bin}{os.pathsep}{os.environ['PATH']}"

        print(f"Found Vulkan SDK at {vulkan_sdk}")
        return True
    except Exception:
        return False


def setup_qt(target_arch=None):
    """Setup Qt environment variables if not already set.

    Args:
      target_arch: Normalized target architecture ("arm64", "x64", or None).
        When None or "x64", picks the x64 Qt tree (msvc*_64).
        When "arm64", picks the ARM64 Qt tree (msvc*_arm64) — requires
        the ARM64 Qt binaries to be installed (via aqtinstall or the
        official installer).

    Returns:
        True if Qt is available and valid, False otherwise.
    """
    # Check if QT_DIR is already set and valid
    existing_qt_dir = os.environ.get("QT_DIR")
    if existing_qt_dir:
        # Validate that the path exists
        if os.path.exists(existing_qt_dir):
            print(f"QT_DIR is set to {existing_qt_dir}")
            return True
        else:
            print_warning(f"QT_DIR is set to {existing_qt_dir} but directory does not exist")
        return False

    # Determine Qt base directory based on platform
    if sys.platform == "win32":
        qt_base = "C:\\Qt"
    else:
        qt_base = "/opt/Qt"

    if not os.path.exists(qt_base):
        return False

    # Get the first (latest) version directory
    try:
        # List all version directories (e.g., 6.8.1, 6.10.1)
        version_dirs = [d for d in os.listdir(qt_base)
                        if os.path.isdir(os.path.join(qt_base, d)) and d[0].isdigit()]
        if not version_dirs:
            return False

        # Sort versions using semantic versioning (split by dots and compare as integers)
        def version_key(v):
            try:
                return tuple(int(x) for x in v.split('.'))
            except ValueError:
                return (0,)
        version_dirs.sort(key=version_key, reverse=True)
        qt_version_dir = os.path.join(qt_base, version_dirs[0])

        if sys.platform == "win32":
            # Qt install dir names: msvc<year>_64 (x64), msvc<year>_arm64 or
            # msvc<year>_arm64_cross_compiled (ARM64).
            def _is_arm64(name):
                return name.startswith("msvc") and "arm64" in name
            def _is_x64(name):
                return name.startswith("msvc") and "_64" in name and "arm64" not in name
            def _pick(predicate):
                dirs = [d for d in os.listdir(qt_version_dir)
                        if os.path.isdir(os.path.join(qt_version_dir, d)) and predicate(d)]
                if not dirs:
                    return None
                dirs.sort(reverse=True)
                return os.path.join(qt_version_dir, dirs[0])

            is_native_arm64 = platform.machine() in ("ARM64", "aarch64")
            native_arch = "arm64" if is_native_arm64 else "x64"
            effective_target = target_arch or native_arch
            qt_dir = _pick(_is_arm64 if effective_target == "arm64" else _is_x64)
            if not qt_dir:
                if effective_target == "arm64":
                    print_error(
                        f"No ARM64 Qt build found in {qt_version_dir}.\n"
                        f"  Install via: python -m aqt install-qt windows desktop "
                        f"{version_dirs[0]} win64_msvc2022_arm64_cross_compiled "
                        f"-m qtmultimedia -O C:\\Qt")
                    sys.exit(1)
                return False

            # Qt cross-compile needs QT_HOST_PATH pointing at the host-arch
            # install (moc/rcc/uic run on the build machine).
            if target_arch and target_arch != native_arch:
                host_dir = _pick(_is_arm64 if is_native_arm64 else _is_x64)
                if not host_dir:
                    print_error(
                        f"Cross-compiling to {target_arch} needs a host ({native_arch}) Qt "
                        f"install at {qt_version_dir} for QT_HOST_PATH, but none was found.")
                    sys.exit(1)
                os.environ["QT_HOST_PATH"] = host_dir
                print(f"Found host Qt at {host_dir} (QT_HOST_PATH)")
        else:
            # On Linux, look for gcc_64 or similar compiler directories
            compiler_dirs = [d for d in os.listdir(qt_version_dir)
                             if os.path.isdir(os.path.join(qt_version_dir, d)) and
                             (d.startswith("gcc") or d.startswith("linux"))]
            if not compiler_dirs:
                return False

            # Prefer gcc_64 if available, otherwise use the first available
            if "gcc_64" in compiler_dirs:
                compiler_dir = "gcc_64"
            elif "linux_gcc_64" in compiler_dirs:
                compiler_dir = "linux_gcc_64"
            else:
                compiler_dirs.sort(reverse=True)
                compiler_dir = compiler_dirs[0]

            qt_dir = os.path.join(qt_version_dir, compiler_dir)

        os.environ["QT_DIR"] = qt_dir
        print(f"Found Qt at {qt_dir}")
        return True
    except Exception:
        return False


def main():
    # Add self to the root search path.
    sys.path.insert(0, self_path)

    # Setup Vulkan SDK and check if available
    vulkan_sdk_available = setup_vulkan_sdk()

    # Augment path to include our fancy things.
    os.environ["PATH"] += os.pathsep + os.pathsep.join([
        self_path,
        os.path.abspath(os.path.join("tools", "build")),
        ])

    # Check git exists.
    if not has_bin("git"):
        print_warning("Git should be installed and on PATH. Version info will be omitted from all binaries!\n")
    elif not git_is_repository():
        print_warning("The source tree is unversioned. Version info will be omitted from all binaries!\n")

    # Check python version.
    python_minimum_ver = 3,9
    if not sys.version_info[:2] >= (python_minimum_ver[0], python_minimum_ver[1]) or not sys.maxsize > 2**32:
        print_error(f"Python {python_minimum_ver[0]}.{python_minimum_ver[1]}+ 64-bit must be installed and on PATH")
        sys.exit(1)

    # Grab Visual Studio version and execute shell to set up environment.
    if sys.platform == "win32" and not vs_version:
        print_warning("Visual Studio not found!"
              "\nBuilding for Windows will not be supported."
              " Please refer to the building guide:"
              f"\nhttps://github.com/has207/xenia-edge/blob/{default_branch}/docs/building.md")

    # Setup main argument parser and common arguments.
    parser = ArgumentParser(prog="xenia-build.py")

    # Grab all commands and populate the argument parser for each.
    subparsers = parser.add_subparsers(title="subcommands",
                                       dest="subcommand")
    commands = discover_commands(subparsers)

    # If the user passed no args, die nicely.
    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(1)

    # Gather any arguments that we want to pass to child processes.
    command_args = sys.argv[1:]
    pass_args = []
    try:
        pass_index = command_args.index("--")
        pass_args = command_args[pass_index + 1:]
        command_args = command_args[:pass_index]
    except Exception:
        pass

    # Parse command name and dispatch.
    args = vars(parser.parse_args(command_args))
    command_name = args["subcommand"]

    qt_available = setup_qt(args.get("target_arch"))
    try:
        command = commands[command_name]
        return_code = command.execute(args, pass_args, os.getcwd())
    except Exception:
        raise
    sys.exit(return_code)


def print_box(msg):
    """Prints an important message inside a box
    """
    print(
        "┌{0:─^{2}}╖\n"
        "│{1: ^{2}}║\n"
        "╘{0:═^{2}}╝\n"
        .format("", msg, len(msg) + 2))


def has_bin(binary):
    """Checks whether the given binary is present.

    Args:
      binary: binary name (without .exe, etc).

    Returns:
      True if the binary exists.
    """
    bin_path = get_bin(binary)
    if not bin_path:
        return False
    return True


def get_bin(binary):
    """Checks whether the given binary is present and returns the path.

    Args:
      binary: binary name (without .exe, etc).

    Returns:
      Full path to the binary or None if not found.
    """
    for path in os.environ["PATH"].split(os.pathsep):
        path = path.strip("\"")
        exe_file = os.path.join(path, binary)
        if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
            return exe_file
        exe_file += ".exe"
        if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
            return exe_file
    return None


def shell_call(command, throw_on_error=True, stdout_path=None, stderr_path=None, shell=False):
    """Executes a shell command.

    Args:
      command: Command to execute, as a list of parameters.
      throw_on_error: Whether to throw an error or return the status code.
      stdout_path: File path to write stdout output to.
      stderr_path: File path to write stderr output to.

    Returns:
      If throw_on_error is False the status code of the call will be returned.
    """
    stdout_file = None
    if stdout_path:
        stdout_file = open(stdout_path, "w")
    stderr_file = None
    if stderr_path:
        stderr_file = open(stderr_path, "w")
    result = 0
    try:
        if throw_on_error:
            result = 1
            subprocess.check_call(command, shell=shell, stdout=stdout_file, stderr=stderr_file)
            result = 0
        else:
            result = subprocess.call(command, shell=shell, stdout=stdout_file, stderr=stderr_file)
    finally:
        if stdout_file:
            stdout_file.close()
        if stderr_file:
            stderr_file.close()
    return result


def generate_version_h(build_dir="build"):
    """Writes <build_dir>/version.h with the current branch/commit/PR info.
    The file is included by source files via `#include "version.h"`; the
    relevant build directory is added to the project include path by the
    root CMakeLists.txt, so different build trees (build/, build-vs/, ...)
    each get their own copy.
    """
    os.makedirs(build_dir, exist_ok=True)
    header_file = os.path.join(build_dir, "version.h")
    pr_number = None

    if git_is_repository():
        (branch_name, commit, commit_short) = git_get_head_info()

        if is_pull_request():
            pr_number = get_pr_number()
    else:
        branch_name = "tarball"
        commit = ":(-dont-do-this"
        commit_short = ":("

    # header
    contents_new = f"""// Autogenerated by xenia-build.py.
#ifndef GENERATED_VERSION_H_
#define GENERATED_VERSION_H_
#define XE_BUILD_BRANCH "{branch_name}"
#define XE_BUILD_COMMIT "{commit}"
#define XE_BUILD_COMMIT_SHORT "{commit_short}"
#define XE_BUILD_DATE __DATE__
"""

    # PR info (if available)
    if pr_number:
      contents_new += f"""#define XE_BUILD_IS_PR
#define XE_BUILD_PR_NUMBER "{pr_number}"
"""

    # footer
    contents_new += """#endif  // GENERATED_VERSION_H_
"""

    contents_old = None
    if os.path.exists(header_file) and os.path.getsize(header_file) < 1024:
        with open(header_file, "r") as f:
            contents_old = f.read()

    if contents_old != contents_new:
        with open(header_file, "w") as f:
            f.write(contents_new)


def git_get_head_info():
    """Queries the current branch and commit checksum from git.

    Returns:
      (branch_name, commit, commit_short)
      If the user is not on any branch the name will be 'detached'.
    """
    p = subprocess.Popen([
        "git",
        "symbolic-ref",
        "--short",
        "-q",
        "HEAD",
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (stdout, stderr) = p.communicate()
    branch_name = stdout.decode("ascii").strip() or "detached"
    p = subprocess.Popen([
        "git",
        "rev-parse",
        "HEAD",
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (stdout, stderr) = p.communicate()
    commit = stdout.decode("ascii").strip() or "unknown"
    p = subprocess.Popen([
        "git",
        "rev-parse",
        "--short",
        "HEAD",
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    (stdout, stderr) = p.communicate()
    commit_short = stdout.decode("ascii").strip() or "unknown"
    return branch_name, commit, commit_short


def git_is_repository():
    """Checks if git is available and this source tree is versioned.
    """
    if not has_bin("git"):
        return False
    return shell_call([
        "git",
        "rev-parse",
        "--is-inside-work-tree",
        ], throw_on_error=False, stdout_path=os.devnull, stderr_path=os.devnull) == 0

def is_pull_request():
    """Returns true if actions is building a pull request, otherwise false.
    """
    return os.getenv('GITHUB_EVENT_NAME') == 'pull_request'

def get_pr_number():
    """
    Returns the pull request number if the workflow is triggered by a PR, otherwise None.
    """
    github_ref = os.getenv('GITHUB_REF')
    
    if github_ref and github_ref.startswith('refs/pull/'):
        return github_ref.split('/')[2]
    
def git_submodule_update():
    """Runs a git submodule sync, init, and update.
    """
    # Sync submodule URLs from .gitmodules to local config
    shell_call([
        "git",
        "submodule",
        "sync",
        ])
    # Then update all submodules to their recorded commits
    shell_call([
        "git",
        "-c",
        "fetch.recurseSubmodules=on-demand",
        "submodule",
        "update",
        "--init",
        "--depth=1",
        "-j", f"{os.cpu_count()}",
        ])


def fetch_data_repos():
    """Fetches data repositories (game-patches, optimized-settings) into .data_repos/.

    Removes and re-clones all data repos fresh each time. They are not submodules
    to avoid constant submodule updates in the main repo.
    """
    print("- fetching data repositories...")

    data_dir = ".data_repos"
    if os.path.exists(data_dir):
        def remove_readonly(func, path, _):
            os.chmod(path, stat.S_IWRITE)
            func(path)
        rmtree(data_dir, onerror=remove_readonly)
    os.makedirs(data_dir)

    # Define data repos
    data_repos = [
        {
            "name": "game-patches",
            "url": "https://github.com/xenia-canary/game-patches.git",
            "branch": "main"
        },
        {
            "name": "optimized-settings",
            "url": "https://github.com/xenia-manager/optimized-settings.git",
            "branch": "main"
        }
    ]

    # Clone each repo fresh
    for repo in data_repos:
        repo_path = os.path.join(data_dir, repo["name"])
        print(f"  - cloning {repo['name']}...")
        shell_call([
            "git",
            "clone",
            "--depth=1",
            "--branch", repo["branch"],
            repo["url"],
            repo_path
        ])


def get_cc(cc=None):
    if sys.platform == "linux":
        if os.environ.get("CC"):
            if "gcc" in os.environ.get("CC"):
                return "gcc"
        return "clang"
    if sys.platform == "win32":
        return "msc"

def get_clang_format_binary():
    """Finds a clang-format binary. Aborts if none is found.

    Returns:
      A path to the clang-format executable.
    """
    clang_format_version_min = 19

    # Build list of all potential clang-format binaries
    all_binaries = []

    # Check versioned binaries from 21 down to 19
    # This prefers newer versions but accepts older ones
    for version in range(21, clang_format_version_min - 1, -1):
        binary = f"clang-format-{version}"
        if has_bin(binary):
            all_binaries.append(binary)

    # Also check generic clang-format
    all_binaries.append("clang-format")

    # Add Windows-specific paths
    if sys.platform == "win32":
        if "VCINSTALLDIR" in os.environ:
            all_binaries.append(os.path.join(os.environ["VCINSTALLDIR"], "Tools", "Llvm", "x64", "bin", "clang-format.exe"))
            all_binaries.append(os.path.join(os.environ["VCINSTALLDIR"], "Tools", "Llvm", "arm64", "bin", "clang-format.exe"))
        all_binaries.append(os.path.join(os.environ["ProgramFiles"], "LLVM", "bin", "clang-format.exe"))

    # Find the highest version available
    best_binary = None
    best_version = 0

    for binary in all_binaries:
        if has_bin(binary):
            try:
                clang_format_out = subprocess.check_output([binary, "--version"], text=True)
                version = int(clang_format_out.split("version ")[1].split(".")[0])
                if version >= clang_format_version_min and version > best_version:
                    best_version = version
                    best_binary = binary
                    best_output = clang_format_out
            except:
                continue

    if best_binary:
        print(best_output)
        return best_binary

    print_error(f"clang-format {clang_format_version_min} or newer is not on PATH")
    sys.exit(1)


def normalize_target_arch(value):
    """Normalizes --target-arch values to canonical names (arm64, x64, or None)."""
    v = value.lower()
    if v in ("arm64", "aarch64", "a64"):
        return "arm64"
    if v in ("x64", "x86_64", "amd64", "x86"):
        return "x64"
    raise ArgumentTypeError(
        f"unknown architecture '{value}' (expected: arm64, aarch64, a64, x64, amd64, x86_64, x86)")


def get_build_dir(target_arch=None):
    """Returns the Ninja build directory for the given target architecture.

    Uses a separate directory when cross-compiling to avoid cache conflicts.
    """
    is_native_arm64 = platform.machine() in ("ARM64", "aarch64")
    if target_arch == "arm64" and not is_native_arm64:
        return "build-arm64"
    if target_arch == "x64" and is_native_arm64:
        return "build-x64"
    return "build"


def run_cmake_configure(cc=None, generator=None, build_tests=False,
                        disable_lto=False, target_arch=None):
    """Runs `cmake` to (re)configure build/ from the source root.

    Uses Ninja Multi-Config by default on all platforms. On Linux the
    C/C++ compilers come from get_cc() / the CC env var; on Windows the
    detected Visual Studio toolchain wins. build_tests toggles
    -DXENIA_BUILD_TESTS=ON; disable_lto toggles -DXENIA_ENABLE_LTO=OFF
    (faster Release link, at the cost of LTO's whole-program opts).
    target_arch enables cross-compilation on Windows (arm64 target from
    an x64 host, or x64 target from an arm64 host) into a separate
    build-<arch>/ tree; off-Windows hosts reject non-native target_arch.
    """
    # Cross-compilation via --target-arch is only supported on Windows where
    # we can locate the MSVC cross-compiler automatically.  On Linux it would
    # silently produce a native build in a differently-named directory.
    if target_arch is not None and sys.platform != "win32":
        is_native_arm64 = platform.machine() in ("ARM64", "aarch64")
        native_arch = "arm64" if is_native_arm64 else "x64"
        if target_arch != native_arch:
            print_error(
                f"Cross-compilation (--target-arch {target_arch}) is only "
                f"supported on Windows.\n"
                f"  The current host architecture is {native_arch}.")
            return 1

    if not generator:
        generator = "Ninja Multi-Config"
    build_dir = get_build_dir(target_arch)
    args = [
        "cmake",
        "-S", ".",
        "-B", build_dir,
        "-G", generator,
    ]
    if sys.platform != "win32":
        if not cc:
            cc = get_cc(cc=cc)
        c_compiler = cc or os.environ.get("CC", "clang")
        cxx_compiler = (cc + "++") if cc else os.environ.get("CXX", "clang++")
        args += [
            f"-DCMAKE_C_COMPILER={c_compiler}",
            f"-DCMAKE_CXX_COMPILER={cxx_compiler}",
        ]
    else:
        is_native_arm64 = platform.machine() in ("ARM64", "aarch64")
        native_arch = "arm64" if is_native_arm64 else "x64"
        is_cross = target_arch is not None and target_arch != native_arch
        if is_cross:
            if target_arch == "x64":
                # ARM64 host → x64 target
                target = "x64"
                vcvars_arg = "arm64_amd64"
                processor = "AMD64"
                cl_glob = r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\HostARM64\x64\cl.exe"
            else:
                # x64 host → arm64 target
                target = "arm64"
                vcvars_arg = "x64_arm64"
                processor = "ARM64"
                cl_glob = r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\arm64\cl.exe"

            cl_paths = sorted(glob(cl_glob))
            if cl_paths:
                cl_exe = cl_paths[-1]
                # Derive the VS install root from the compiler path:
                # .../VC/Tools/MSVC/<ver>/bin/Host<x>/target<y>/cl.exe -> .../VC
                vc_root = cl_exe
                for _ in range(7):  # walk up 7 levels to VC/
                    vc_root = os.path.dirname(vc_root)
                vcvarsall = os.path.join(vc_root, "Auxiliary", "Build", "vcvarsall.bat")
                if os.path.exists(vcvarsall):
                    print(f"  Setting up {target.upper()} build environment via: {vcvarsall}")
                    cmd = f'"{vcvarsall}" {vcvars_arg} >nul 2>&1 && set'
                    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
                    if result.returncode == 0:
                        for line in result.stdout.splitlines():
                            if "=" in line:
                                key, _, value = line.partition("=")
                                os.environ[key] = value
                args += [
                    "-DCMAKE_SYSTEM_NAME=Windows",
                    f"-DCMAKE_SYSTEM_PROCESSOR={processor}",
                    f"-DCMAKE_C_COMPILER={cl_exe.replace(os.sep, '/')}",
                    f"-DCMAKE_CXX_COMPILER={cl_exe.replace(os.sep, '/')}",
                ]
            else:
                print(f"  WARNING: {target.upper()} cross-compiler not found. Install "
                      f"'MSVC {target.upper()} build tools' in Visual Studio.")
    if build_tests:
        args += ["-DXENIA_BUILD_TESTS=ON"]
    if disable_lto:
        args += ["-DXENIA_ENABLE_LTO=OFF"]
    qt_host_path = os.environ.get("QT_HOST_PATH")
    if qt_host_path:
        args += [f"-DQT_HOST_PATH={qt_host_path.replace(os.sep, '/')}"]
    ret = subprocess.call(args)
    if ret == 0:
        generate_version_h(build_dir)
    return ret


def get_build_bin_path(args):
    """Returns the path of the bin/ path with build results based on the
    configuration specified in the parsed arguments.

    Args:
      args: Parsed arguments.

    Returns:
      A full path for the bin folder.
    """
    if sys.platform == "darwin":
        platform_name = "macosx"
    elif sys.platform == "win32":
        platform_name = "windows"
    else:
        platform_name = "linux"
    build_dir = get_build_dir(args.get("target_arch"))
    return os.path.join(self_path, build_dir, "bin", platform_name.capitalize(), args["config"].capitalize())


def create_clion_workspace():
    """Creates some basic workspace information inside the .idea directory for first start.
    """
    if os.path.exists(".idea"):
        # No first start
        return False
    print("Generating CLion workspace files...")
    # Might become easier in the future: https://youtrack.jetbrains.com/issue/CPP-7911

    # Set the location of the CMakeLists.txt
    os.mkdir(".idea")
    with open(os.path.join(".idea", "misc.xml"), "w") as f:
        f.write("""<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="CMakeWorkspace" PROJECT_DIR="$PROJECT_DIR$/build">
    <contentRoot DIR="$PROJECT_DIR$" />
  </component>
</project>
""")

    # Set available configurations
    # TODO Find a way to trigger a cmake reload
    with open(os.path.join(".idea", "workspace.xml"), "w") as f:
        f.write("""<?xml version="1.0" encoding="UTF-8"?>
<project version="4">
  <component name="CMakeSettings">
    <configurations>
      <configuration PROFILE_NAME="Checked" CONFIG_NAME="Checked" />
      <configuration PROFILE_NAME="Debug" CONFIG_NAME="Debug" />
      <configuration PROFILE_NAME="Release" CONFIG_NAME="Release" />
    </configurations>
  </component>
</project>""")

    return True


def discover_commands(subparsers):
    """Looks for all commands and returns a dictionary of them.
    In the future commands could be discovered on disk.

    Args:
      subparsers: Argument subparsers parent used to add command parsers.

    Returns:
      A dictionary containing name-to-Command mappings.
    """
    commands = {
        "setup": SetupCommand(subparsers),
        "fetchdata": FetchDataCommand(subparsers),
        "build": BuildCommand(subparsers),
        "devenv": DevenvCommand(subparsers),
        "gentests": GenTestsCommand(subparsers),
        "test": TestCommand(subparsers),
        "clean": CleanCommand(subparsers),
        "lint": LintCommand(subparsers),
        "format": FormatCommand(subparsers),
        "tidy": TidyCommand(subparsers),
        }
    return commands


class Command(object):
    """Base type for commands.
    """

    def __init__(self, subparsers, name, help_short=None, help_long=None,
                 *args, **kwargs):
        """Initializes a command.

        Args:
          subparsers: Argument subparsers parent used to add command parsers.
          name: The name of the command exposed to the management script.
          help_short: Help text printed alongside the command when queried.
          help_long: Extended help text when viewing command help.
        """
        self.name = name
        self.help_short = help_short
        self.help_long = help_long

        self.parser = subparsers.add_parser(name,
                                            help=help_short,
                                            description=help_long)
        self.parser.set_defaults(command_handler=self)

    def execute(self, args, pass_args, cwd):
        """Executes the command.

        Args:
          args: Arguments hash for the command.
          pass_args: Arguments list to pass to child commands.
          cwd: Current working directory.

        Returns:
          Return code of the command.
        """
        return 1


class SetupCommand(Command):
    """'setup' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(SetupCommand, self).__init__(
            subparsers,
            name="setup",
            help_short="Setup the build environment.",
            *args, **kwargs)
        self.parser.add_argument(
            "--target-arch", type=normalize_target_arch, default=None,
            help="Target architecture (arm64/aarch64/a64, x64/amd64/x86_64/x86). "
                 "On Windows, non-native values enable cross-compilation into a "
                 "separate build-<arch>/ tree.")

    def execute(self, args, pass_args, cwd):
        print("Setting up the build environment...\n")

        print("- git submodule init / update...")
        if git_is_repository():
            git_submodule_update()
            fetch_data_repos()
        else:
            print_warning("Git not available or not a repository. Dependencies may be missing.")

        print("\n- running cmake configure...")
        ret = run_cmake_configure(target_arch=args.get("target_arch"))
        print_status(ResultStatus.SUCCESS if not ret else ResultStatus.FAILURE)
        return ret


class FetchDataCommand(Command):
    """'fetchdata' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(FetchDataCommand, self).__init__(
            subparsers,
            name="fetchdata",
            help_short="Fetches data repositories (game-patches, optimized-settings).",
            *args, **kwargs)

    def execute(self, args, pass_args, cwd):
        print("Fetching data repositories...\n")

        if git_is_repository():
            fetch_data_repos()
        else:
            print_warning("Git not available or not a repository.")
            return 1

        print("\nSuccess!")
        return 0


class BaseBuildCommand(Command):
    """Base command for things that require building.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(BaseBuildCommand, self).__init__(
            subparsers,
            *args, **kwargs)
        self.parser.add_argument(
            "--cc", choices=["clang", "gcc", "msc"], default=None,
            help="Compiler toolchain.")
        self.parser.add_argument(
            "--config", choices=["checked", "debug", "release", "valgrind"], default="debug",
            type=str.lower, help="Chooses the build configuration.")
        self.parser.add_argument(
            "--target", action="append", default=[],
            help="Builds only the given target(s).")
        self.parser.add_argument(
            "--force", action="store_true",
            help="Forces a full rebuild.")
        self.parser.add_argument(
            "--no_configure", action="store_true",
            help="Skips the cmake configure step before building.")
        self.parser.add_argument(
            "--build-tests", dest="build_tests", action="store_true",
            default=False,
            help="Enables building test suites (sets -DXENIA_BUILD_TESTS=ON).")
        self.parser.add_argument(
            "--disable-lto", dest="disable_lto", action="store_true",
            default=False,
            help="Disables link-time optimization for Release builds "
                 "(sets -DXENIA_ENABLE_LTO=OFF). Much faster Release link "
                 "at the cost of whole-program opts.")
        self.parser.add_argument(
            "--target-arch", type=normalize_target_arch, default=None,
            help="Target architecture (arm64/aarch64/a64, x64/amd64/x86_64/x86). "
                 "On Windows, non-native values enable cross-compilation into a "
                 "separate build-<arch>/ tree.")

    def execute(self, args, pass_args, cwd):
        if not os.environ.get("VULKAN_SDK"):
            print_error("Vulkan SDK not found!"
                  "\nPlease install Vulkan SDK from:"
                  "\nhttps://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.exe"
                  f"\nSee: https://github.com/has207/xenia-edge/blob/{default_branch}/docs/building.md")
            return 1

        target_arch = args.get("target_arch")
        if not args["no_configure"]:
            print("- running cmake configure...")
            ret = run_cmake_configure(
                cc=args["cc"],
                build_tests=args["build_tests"],
                disable_lto=args["disable_lto"],
                target_arch=target_arch,
            )
            if ret:
                return ret
            print("")

        build_dir = get_build_dir(target_arch)
        print("- building (%s):%s..." % (
            "all" if not len(args["target"]) else ", ".join(args["target"]),
            args["config"]))
        config = args["config"].title()
        cmake_args = [
            "cmake", "--build", build_dir,
            "--config", config,
        ]
        if args["force"]:
            cmake_args.append("--clean-first")
        if args["target"]:
            cmake_args.append("--target")
            cmake_args.extend(args["target"])
        result = subprocess.call(cmake_args + pass_args, env=dict(os.environ))
        if result != 0:
            print_error("cmake build failed with one or more errors.")
        return result


class BuildCommand(BaseBuildCommand):
    """'build' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(BuildCommand, self).__init__(
            subparsers,
            name="build",
            help_short="Builds the project with the default toolchain.",
            *args, **kwargs)

    def execute(self, args, pass_args, cwd):
        print(f"Building {args['config']}...\n")
        result = super(BuildCommand, self).execute(args, pass_args, cwd)
        print_status(ResultStatus.SUCCESS if not result else ResultStatus.FAILURE)
        return result


class TestCommand(BaseBuildCommand):
    """'test' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(TestCommand, self).__init__(
            subparsers,
            name="test",
            help_short="Runs automated tests that have been built with `xb build`.",
            help_long="""
            To pass arguments to the test executables separate them with `--`.
            For example, you can run only the instr_foo.s tests with:
              $ xb test -- instr_foo
            """,
            *args, **kwargs)
        self.parser.add_argument(
            "--no_build", action="store_true",
            help="Don't build before running tests.")
        self.parser.add_argument(
            "--continue", action="store_true",
            help="Don't stop when a test errors, but continue running all.")
        self.parser.add_argument(
            "--no_sde", action="store_true",
            help="Don't use Intel SDE to test different CPU instruction sets.")

    def execute(self, args, pass_args, cwd):
        print("Testing...\n")

        # The test executables that will be built and run.
        test_targets = args["target"] or [
            "xenia-base-tests",
            "xenia-cpu-ppc-tests"
            ]
        args["target"] = test_targets

        # Build all targets (if desired).
        if not args["no_build"]:
            result = super(TestCommand, self).execute(args, [], cwd)
            if result:
                print("Failed to build, aborting test run.")
                return result

        # Ensure all targets exist before we run.
        test_executables = [
            get_bin(os.path.join(get_build_bin_path(args), test_target))
            for test_target in test_targets]
        for i in range(0, len(test_targets)):
            if test_executables[i] is None:
                print_error(f"Unable to find {test_targets[i]} - build it.")
                return 1

        # Prepare environment with Qt bin directory in PATH if available
        test_env = dict(os.environ)
        qt_dir = os.environ.get("QT_DIR")
        if qt_dir and sys.platform == "win32":
            qt_bin = os.path.join(qt_dir, "bin")
            if os.path.exists(qt_bin):
                test_env["PATH"] = f"{qt_bin}{os.pathsep}{test_env['PATH']}"
                print(f"- Qt bin directory added to PATH: {qt_bin}\n")

        # Run tests.
        any_failed = False

        # Intel SDE configurations for testing different CPU paths
        # Only apply to xenia-cpu-tests and xenia-cpu-ppc-tests
        sde_executable = "/opt/intel-sde/sde64"

        # Check if Intel SDE is available and if we're testing CPU-related tests
        has_cpu_tests = any("cpu" in test.lower() for test in test_targets)
        use_sde = has_cpu_tests and os.path.exists(sde_executable) and not args.get("no_sde", False)

        if use_sde:
            print(f"Intel SDE detected at {sde_executable}")
            print("Will test CPU code with AVX2 and AVX512 instruction sets")
            print("(Test binaries require AVX2 minimum)\n")
            sde_configs = [
                ("", "Native CPU"),  # Run without SDE first
                ("-hsw", "Haswell (AVX2)"),
                ("-skx", "Skylake-X (AVX512)")
            ]
        else:
            if has_cpu_tests and not os.path.exists(sde_executable):
                print("Intel SDE not found - running CPU tests with native CPU only")
            sde_configs = [("", "Native CPU")]

        for test_executable in test_executables:
            test_name = os.path.basename(test_executable)

            # Only use SDE for CPU tests
            if use_sde and "cpu" in test_name.lower():
                for sde_flag, cpu_name in sde_configs:
                    if sde_flag:
                        print(f"- {test_executable} (emulating {cpu_name})")
                        cmd = [sde_executable, sde_flag, "--", test_executable] + pass_args
                    else:
                        print(f"- {test_executable} ({cpu_name})")
                        cmd = [test_executable] + pass_args

                    result = subprocess.call(cmd, env=test_env)
                    if result:
                        print_error(f"{test_name} failed with {cpu_name}")
                        any_failed = True
                        if not args["continue"]:
                            print_error("test failed, aborting, use --continue to keep going.")
                            return result
            else:
                # Non-CPU tests or SDE not available - run normally
                print(f"- {test_executable}")
                result = subprocess.call([test_executable] + pass_args, env=test_env)
                if result:
                    any_failed = True
                    if args["continue"]:
                        print_error("test failed but continuing due to --continue.")
                    else:
                        print_error("test failed, aborting, use --continue to keep going.")
                        return result

        if any_failed:
            print_error("one or more tests failed.")
            result = 1
        return result


class GenTestsCommand(Command):
    """'gentests' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(GenTestsCommand, self).__init__(
            subparsers,
            name="gentests",
            help_short="Generates test binaries.",
            help_long="""
            Generates test binaries (under src/xenia/cpu/ppc/testing/bin/).
            Run after modifying test .s files.
            """,
            *args, **kwargs)

    def process_src_file(test_bin, ppc_as, ppc_objdump, ppc_ld, ppc_nm, src_file):
        def make_unix_path(p):
            """Forces a unix path separator style, as required by binutils.
            """
            return p.replace(os.sep, "/")

        src_name = os.path.splitext(os.path.basename(src_file))[0]
        obj_file = f"{os.path.join(test_bin, src_name)}.o"
        shell_call([
            ppc_as,
            "-a32",
            "-be",
            "-mregnames",
            "-ma2",
            "-maltivec",
            "-mvsx",
            "-mvmx128",
            "-R",
            f"-o{make_unix_path(obj_file)}",
            make_unix_path(src_file),
            ])
        dis_file = f"{os.path.join(test_bin, src_name)}.dis"
        shell_call([
            ppc_objdump,
            "--adjust-vma=0x100000",
            "-Ma2",
            "-Mvmx128",
            "-D",
            "-EB",
            make_unix_path(obj_file),
            ], stdout_path=dis_file)
        # Eat the first 4 lines to kill the file path that'll differ across machines.
        with open(dis_file) as f:
            dis_file_lines = f.readlines()
        with open(dis_file, "w") as f:
            f.writelines(dis_file_lines[4:])
        shell_call([
            ppc_ld,
            "-A powerpc:common32",
            "-melf32ppc",
            "-EB",
            "-nostdlib",
            "--oformat=binary",
            "-Ttext=0x80000000",
            "-e0x80000000",
            f"-o{make_unix_path(os.path.join(test_bin, src_name))}.bin",
            make_unix_path(obj_file),
            ])
        shell_call([
            ppc_nm,
            "--numeric-sort",
            make_unix_path(obj_file),
            ], stdout_path=f"{os.path.join(test_bin, src_name)}.map")

        return src_file

    def execute(self, args, pass_args, cwd):
        print("Generating test binaries...\n")

        # Use the same binutils path on all platforms
        binutils_path = os.path.join("third_party", "binutils", "bin")

        ppc_as = os.path.join(binutils_path, "powerpc-none-elf-as")
        ppc_ld = os.path.join(binutils_path, "powerpc-none-elf-ld")
        ppc_objdump = os.path.join(binutils_path, "powerpc-none-elf-objdump")
        ppc_nm = os.path.join(binutils_path, "powerpc-none-elf-nm")

        # Check if binutils exists (with .exe on Windows)
        ppc_as_check = ppc_as + (".exe" if sys.platform == "win32" else "")
        if not os.path.exists(ppc_as_check):
            print("Binaries are missing, binutils build required\n")
            binutils_dir = os.path.join("third_party", "binutils")
            shell_script = "build.sh"

            # Save current directory
            original_dir = os.getcwd()

            if sys.platform == "linux":
                # Set executable bit for build script before running it
                os.chdir(binutils_dir)
                os.chmod(shell_script, stat.S_IRUSR | stat.S_IWUSR |
                         stat.S_IXUSR | stat.S_IRGRP | stat.S_IROTH)
                shell_call([f"./{shell_script}"])
                os.chdir(original_dir)
            elif sys.platform == "win32":
                # On Windows, add Cygwin to PATH and run bash
                cygwin_bin = r"C:\cygwin64\bin"
                os.environ["PATH"] = f"{cygwin_bin}{os.pathsep}{os.environ['PATH']}"
                os.chdir(binutils_dir)
                shell_call(["bash", shell_script])
                os.chdir(original_dir)

        test_src = os.path.join("src", "xenia", "cpu", "ppc", "testing")
        test_bin = os.path.join(test_src, "bin")

        # Ensure the test output path exists.
        if not os.path.exists(test_bin):
            os.mkdir(test_bin)

        src_files = [os.path.join(root, name)
                     for root, dirs, files in os.walk("src")
                     for name in files
                     if (name.startswith("instr_") or name.startswith("seq_"))
                     and name.endswith((".s"))]

        any_errors = False

        pool_func = partial(GenTestsCommand.process_src_file, test_bin, ppc_as, ppc_objdump, ppc_ld, ppc_nm)
        with Pool() as pool:
            for src_file in pool.imap_unordered(pool_func, src_files):
                print(f"- {src_file}")

        if any_errors:
            print_error("failed to build one or more tests.")
            return 1

        return 0


class CleanCommand(Command):
    """'clean' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(CleanCommand, self).__init__(
            subparsers,
            name="clean",
            help_short="Removes intermediate files and build outputs.",
            *args, **kwargs)

    def execute(self, args, pass_args, cwd):
        print("Cleaning build artifacts...")
        for _build_dir in ("build", "build-arm64", "build-x64", "build-vs"):
            if os.path.isdir(_build_dir):
                print(f"- cmake clean {_build_dir}...")
                subprocess.call(["cmake", "--build", _build_dir, "--target", "clean"])
        clean_generated_files()
        print_status(ResultStatus.SUCCESS)
        return 0


def clean_shader_bytecode():
    """Removes generated shader bytecode files."""
    bytecode_dirs = [
        "src/xenia/gpu/shaders/bytecode/d3d12_5_1",
        "src/xenia/gpu/shaders/bytecode/vulkan_spirv",
        "src/xenia/ui/shaders/bytecode/d3d12_5_1",
        "src/xenia/ui/shaders/bytecode/vulkan_spirv",
    ]
    for bytecode_dir in bytecode_dirs:
        if os.path.isdir(bytecode_dir):
            print(f"- removing {bytecode_dir}/...")
            rmtree(bytecode_dir)


def clean_moc_files():
    """Removes generated MOC files."""
    ui_dir = "src/xenia/ui"
    if os.path.isdir(ui_dir):
        for filename in os.listdir(ui_dir):
            if filename.startswith("moc_") and filename.endswith(".cc"):
                moc_path = os.path.join(ui_dir, filename)
                print(f"- removing {moc_path}...")
                os.remove(moc_path)


def clean_generated_files():
    """Removes generated shader bytecode and MOC files."""
    clean_shader_bytecode()
    clean_moc_files()


# Generated files that should be excluded from linting/formatting
GENERATED_FILES = [
    "src/xenia/ui/ui_resources_qrc.cpp",  # Qt resource file
]

def find_xenia_source_files():
    """Gets all xenia source files in the project.

    Returns:
      A list of file paths.
    """
    return [os.path.join(root, name)
            for root, dirs, files in os.walk("src")
            for name in files
            if name.endswith((".cc", ".c", ".h", ".inl", ".inc"))
            and os.path.join(root, name) not in GENERATED_FILES]


class LintCommand(Command):
    """'lint' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(LintCommand, self).__init__(
            subparsers,
            name="lint",
            help_short="Checks for lint errors with clang-format.",
            *args, **kwargs)
        self.parser.add_argument(
            "--all", action="store_true",
            help="Lint all files, not just those changed.")
        self.parser.add_argument(
            "--origin", action="store_true",
            help=f"Lints all files changed relative to origin/{default_branch}.")

    def execute(self, args, pass_args, cwd):
        clang_format_binary = get_clang_format_binary()

        difftemp = ".difftemp.txt"

        if args["all"]:
            all_files = find_xenia_source_files()
            all_files.sort()
            print(f"- linting {len(all_files)} files")
            any_errors = False
            for file_path in all_files:
                if os.path.exists(difftemp): os.remove(difftemp)
                ret = shell_call([
                    clang_format_binary,
                    "-output-replacements-xml",
                    "-style=file",
                    file_path,
                    ], throw_on_error=False, stdout_path=difftemp)
                with open(difftemp) as f:
                    had_errors = "<replacement " in f.read()
                if os.path.exists(difftemp): os.remove(difftemp)
                if had_errors:
                    any_errors = True
                    print(f"\n{file_path}")
                    shell_call([
                        clang_format_binary,
                        "-style=file",
                        file_path,
                        ], throw_on_error=False, stdout_path=difftemp)
                    shell_call([
                        sys.executable,
                        "tools/diff.py",
                        file_path,
                        difftemp,
                        difftemp,
                        ])
                    shell_call([
                        "type" if sys.platform == "win32" else "cat",
                        difftemp,
                        ], shell=True if sys.platform == "win32" else False)
                    if os.path.exists(difftemp):
                        os.remove(difftemp)
                    print("")
            if any_errors:
                print_error("1+ diffs. Stage changes and run 'xb format' to fix.")
                return 1
            else:
                print("\nLinting completed successfully.")
                return 0
        else:
            print("- git-clang-format --diff")
            if os.path.exists(difftemp): os.remove(difftemp)
            cmd = [
                sys.executable,
                "third_party/clang-format/git-clang-format",
                f"--binary={clang_format_binary}",
                f"--commit={'origin/canary_experimental' if args['origin'] else 'HEAD'}",
                "--style=file",
                "--diff",
            ]
            # Exclude generated files
            for generated_file in GENERATED_FILES:
                cmd.append(f":(exclude){generated_file}")
            ret = shell_call(cmd, throw_on_error=False, stdout_path=difftemp)
            with open(difftemp) as f:
                contents = f.read()
                not_modified = "no modified files" in contents
                not_modified = not_modified or "did not modify" in contents
                f.close()
            if os.path.exists(difftemp): os.remove(difftemp)
            if not not_modified:
                any_errors = True
                print("")
                cmd = [
                    sys.executable,
                    "third_party/clang-format/git-clang-format",
                    f"--binary={clang_format_binary}",
                    f"--commit={'origin/canary_experimental' if args['origin'] else 'HEAD'}",
                    "--style=file",
                    "--diff",
                ]
                # Exclude generated files
                for generated_file in GENERATED_FILES:
                    cmd.append(f":(exclude){generated_file}")
                shell_call(cmd, throw_on_error=False)
                print_error("1+ diffs. Stage changes and run 'xb format' to fix.")
                return 1
            else:
                print("Linting completed successfully.")
                return 0


class FormatCommand(Command):
    """'format' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(FormatCommand, self).__init__(
            subparsers,
            name="format",
            help_short="Reformats staged code with clang-format.",
            *args, **kwargs)
        self.parser.add_argument(
            "--all", action="store_true",
            help="Format all files, not just those changed.")
        self.parser.add_argument(
            "--origin", action="store_true",
            help=f"Formats all files changed relative to origin/{default_branch}.")

    def execute(self, args, pass_args, cwd):
        clang_format_binary = get_clang_format_binary()

        if args["all"]:
            all_files = find_xenia_source_files()
            all_files.sort()
            print(f"- clang-format [{len(all_files)} files]")
            any_errors = False
            for file_path in all_files:
                ret = shell_call([
                    clang_format_binary,
                    "-i",
                    "-style=file",
                    file_path,
                    ], throw_on_error=False)
                if ret:
                    any_errors = True
            if any_errors:
                print_error("1+ clang-format calls failed."
                      " Ensure all files are staged.")
                return 1
            else:
                print("\nFormatting completed successfully.")
                return 0
        else:
            print("- git-clang-format")
            cmd = [
                sys.executable,
                "third_party/clang-format/git-clang-format",
                f"--binary={clang_format_binary}",
                f"--commit={'origin/canary_experimental' if args['origin'] else 'HEAD'}",
            ]
            # Exclude generated files
            for generated_file in GENERATED_FILES:
                cmd.append(f":(exclude){generated_file}")

            ret = shell_call(cmd, throw_on_error=False)
            if ret != 0:
                print("\nFiles were formatted. Please stage the changes:")
                print("  git status")
                print("  git add <files>")
                return 1
            print("")

        return 0


# TODO(benvanik): merge into linter, or as lint --anal?
# TODO(benvanik): merge into linter, or as lint --anal?
class TidyCommand(Command):
    """'tidy' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(TidyCommand, self).__init__(
            subparsers,
            name="tidy",
            help_short="Runs the clang-tidy checker on all code.",
            *args, **kwargs)
        self.parser.add_argument(
            "--fix", action="store_true",
            help="Applies suggested fixes, where possible.")

    def execute(self, args, pass_args, cwd):
        # clang-tidy needs a compile_commands.json; CMake emits one when
        # CMAKE_EXPORT_COMPILE_COMMANDS is on.
        ret = subprocess.call([
            "cmake", "-S", ".", "-B", "build",
            "-G", "Ninja Multi-Config",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ])
        if ret != 0:
            return ret

        if sys.platform == "darwin":
            platform_name = "darwin"
        elif sys.platform == "win32":
            platform_name = "windows"
        else:
            platform_name = "linux"
        tool_root = f"build/llvm_tools/debug_{platform_name}"

        all_files = [file_path for file_path in find_xenia_source_files()
                     if not file_path.endswith("_test.cc")]
        # Tidy only likes .cc files.
        all_files = [file_path for file_path in all_files
                     if file_path.endswith(".cc")]

        any_errors = False
        for file in all_files:
            print(f"- clang-tidy {file}")
            ret = shell_call([
                "clang-tidy",
                "-p", tool_root,
                "-checks=" + ",".join([
                    "clang-analyzer-*",
                    "google-*",
                    "misc-*",
                    "modernize-*"
                    # TODO(benvanik): pick the ones we want - some are silly.
                    # "readability-*",
                ]),
                ] + (["-fix"] if args["fix"] else []) + [
                    file,
                ], throw_on_error=False)
            if ret:
                any_errors = True

        if any_errors:
            print_error("1+ clang-tidy calls failed.")
            return 1
        else:
            print("\nTidy completed successfully.")
            return 0

class DevenvCommand(Command):
    """'devenv' command.
    """

    def __init__(self, subparsers, *args, **kwargs):
        super(DevenvCommand, self).__init__(
            subparsers,
            name="devenv",
            help_short="Launches the development environment.",
            *args, **kwargs)
        self.parser.add_argument(
            "--target-arch", type=normalize_target_arch, default=None,
            help="Target architecture (arm64/aarch64/a64, x64/amd64/x86_64/x86). "
                 "On Windows, non-native values enable cross-compilation into a "
                 "separate build-vs-<arch>/ tree.")

    def execute(self, args, pass_args, cwd):
        target_arch = args.get("target_arch")
        if sys.platform == "win32":
            return self._launch_visual_studio(target_arch)
        # Non-Windows: CLion is the only IDE we know how to launch
        # automatically. macOS falls through to the manual-open message
        # (the Xcode generator isn't supported).
        if has_bin("clion") or has_bin("clion.sh"):
            print("Launching CLion...")
            show_reload_prompt = create_clion_workspace()
            if run_cmake_configure(target_arch=target_arch) != 0:
                return 1
            if show_reload_prompt:
                print_box("Please run \"File ⇒ ↺ Reload CMake Project\" from inside the IDE!")
            bin_name = "clion" if has_bin("clion") else "clion.sh"
            shell_call([bin_name, "."])
            return 0
        print("No supported IDE found. Open the project root in your IDE.")
        print("CMakeLists.txt and CMakePresets.json are in the project root.")
        return 0

    def _launch_visual_studio(self, target_arch=None):
        """Configures a VS build tree under build-vs[-arch]/ and launches devenv."""
        if not vs_version:
            print_error("Visual Studio is not installed.")
            return 1
        # Kept separate from build/ (Ninja Multi-Config) because CMake
        # refuses to change generators on an existing tree — mixing the
        # two workflows in one dir would force a full wipe each time.
        is_native_arm64 = platform.machine() in ("ARM64", "aarch64")
        effective_arch = target_arch or ("arm64" if is_native_arm64 else "x64")
        vs_arch = "ARM64" if effective_arch == "arm64" else "x64"
        vs_build_dir = "build-vs" if effective_arch == ("arm64" if is_native_arm64 else "x64") else f"build-vs-{effective_arch}"
        print(f"Configuring Visual Studio build tree ({vs_arch}) in {vs_build_dir}...")
        # -A <arch> without -G lets CMake pick whichever VS generator matches
        # the installed toolchain (VS 2022, 2026, ...).
        ret = subprocess.call([
            "cmake", "-S", ".", "-B", vs_build_dir, "-A", vs_arch,
        ])
        if ret != 0:
            print_error("cmake configure failed for the VS build tree")
            return ret
        generate_version_h(vs_build_dir)
        # VS 2026+ emits xenia.slnx; older VS emits xenia.sln.
        sln_path = os.path.join(vs_build_dir, "xenia.slnx")
        if not os.path.exists(sln_path):
            sln_path = os.path.join(vs_build_dir, "xenia.sln")
        if not os.path.exists(sln_path):
            print_error("cmake configured successfully but no .sln/.slnx was produced")
            return 1
        print(f"\n- launching devenv on {sln_path}...")
        shell_call(["devenv", sln_path])
        return 0


if __name__ == "__main__":
    main()

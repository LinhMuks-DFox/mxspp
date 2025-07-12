#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess as sp
import sys
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Literal, Set, Tuple

# --- Script Configuration ---
C_CPP_EXTENSIONS: Set[str] = {".c", ".cc", ".cpp", ".h", ".hpp"}
PYTHON_EXTENSIONS: Set[str] = {".py"}
TOOLS_TO_PROBE: List[str] = [
    "clang-format",
    "clang-tidy",
    "black",
    "ruff",
    "mypy",
    "autopep8",
]
LFS_THRESHOLD_BYTES: int = 5 * 1024 * 1024
TODO_PATTERN = re.compile(r"\b(TODO|FIXME|BUG)\b", re.IGNORECASE)
LICENSE_PATTERN = re.compile(r"SPDX-License-Identifier:", re.IGNORECASE)
CDB_CANDIDATES: List[str] = [
    "compile_commands.json", "build/compile_commands.json"]
EXCLUDED_FILES: Set[str] = {"download_dep.py",
                            "rebuild.py", "check_before_commit.py"}
MAX_WORKERS: int = os.cpu_count() or 4
# --- End Configuration ---


@dataclass
class CheckResult:
    """Represents the outcome of a single check."""

    item: str
    status: Literal["PASS", "FAIL", "WARN", "SKIP"]
    details: str = ""
    fix_suggestion: str = field(default="", compare=False)

    def is_successful(self) -> bool:
        """A check is successful if it's not a hard FAIL."""
        return self.status != "FAIL"


def get_git_files(mode: str) -> List[Path]:
    """Retrieves a list of files from Git based on the specified mode."""
    cmd_map = {
        "tree": ["git", "ls-files"],
        "edited": ["git", "diff", "HEAD", "--name-only"],
        "staged": ["git", "diff", "--cached", "--name-only"],
    }
    cmd = cmd_map.get(mode, cmd_map["staged"])

    try:
        process = sp.run(
            cmd, capture_output=True, text=True, check=True, encoding="utf-8"
        )
        return [
            Path(p)
            for p in process.stdout.splitlines()
            if p and Path(p).name not in EXCLUDED_FILES and Path(p).exists()
        ]
    except (sp.CalledProcessError, FileNotFoundError) as e:
        print(f"Error getting files from Git: {e}", file=sys.stderr)
        return []


def filter_files_by_extension(files: List[Path], extensions: Set[str]) -> List[Path]:
    return [p for p in files if p.suffix in extensions]


def run_command(cmd: List[str], cwd: str | None = None) -> sp.CompletedProcess:
    return sp.run(cmd, capture_output=True, text=True, cwd=cwd, encoding="utf-8")


# --- Individual Check Functions ---


def check_tool_availability(tool_name: str) -> CheckResult:
    if shutil.which(tool_name):
        return CheckResult(tool_name, "PASS", "Available")
    return CheckResult(
        tool_name,
        "FAIL",
        "Not found in PATH",
        f"Install '{tool_name}' or add it to your system's PATH.",
    )


def find_compilation_database() -> str | None:
    for candidate in CDB_CANDIDATES:
        if (path := Path(candidate)).is_file():
            return str(path.parent)
    return None


def format_clang_format(files: List[Path]) -> CheckResult:
    if not files:
        return CheckResult("clang-format", "SKIP", "No C/C++ files to format")
    run_command(["clang-format", "-i", *map(str, files)])
    return CheckResult("clang-format", "PASS", f"Formatted {len(files)} file(s)")


def format_python(
    files: List[Path], formatter: Literal["black", "autopep8"]
) -> CheckResult:
    if not files:
        return CheckResult(formatter, "SKIP", "No Python files to format")
    cmd = {"black": ["black", "--quiet"], "autopep8": ["autopep8", "--in-place"]}[
        formatter
    ]
    res = run_command([*cmd, *map(str, files)])
    if res.returncode == 0:
        return CheckResult(formatter, "PASS", f"Formatted {len(files)} file(s)")
    details = f"Formatter output:\n{res.stdout}\n{res.stderr}"
    return CheckResult(
        formatter, "FAIL", details, "Review the tool's output and fix manually."
    )


def lint_clang_tidy(files: List[Path], cdb_path: str | None) -> CheckResult:
    if not files:
        return CheckResult("clang-tidy", "SKIP", "No C/C++ files to lint")
    if not cdb_path:
        return CheckResult(
            "clang-tidy",
            "FAIL",
            "compile_commands.json not found",
            "Generate compile_commands.json, e.g., using `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`",
        )

    # checks = "-*,clang-analyzer-*,bugprone-*,modernize-*,performance-*"
    # warnings_as_errors = "clang-analyzer-*,bugprone-*"
    cmd = [
        "clang-tidy",
        "-quiet",
        "-fix",
        # f"-checks={checks}",
        # f"-warnings-as-errors={warnings_as_errors}",
        "-p",
        cdb_path,
        *map(str, files),
    ]
    res = run_command(cmd)
    if res.returncode == 0:
        return CheckResult(
            "clang-tidy", "PASS", "Auto-fixed issues and/or no issues found"
        )
    output = res.stdout.strip() or res.stderr.strip()
    return CheckResult(
        "clang-tidy",
        "FAIL",
        output,
        "Auto-fix applied. Please review changes and fix remaining issues manually.",
    )


def lint_ruff(files: List[Path]) -> CheckResult:
    if not files:
        return CheckResult("ruff", "SKIP", "No Python files to lint")
    run_command(
        ["ruff", "check", "--fix",
            "--exit-zero-even-if-changed", *map(str, files)]
    )
    res = run_command(["ruff", "check", *map(str, files)])
    if res.returncode == 0:
        return CheckResult("ruff", "PASS", "Auto-fixed issues and/or no issues found")
    output = res.stdout.strip() or res.stderr.strip()
    return CheckResult(
        "ruff",
        "FAIL",
        output,
        "Auto-fix applied. Please fix the remaining issues listed in the details.",
    )


def lint_mypy(files: List[Path]) -> CheckResult:
    if not files:
        return CheckResult("mypy", "SKIP", "No Python files to check")
    res = run_command(["mypy", "--strict", *map(str, files)])
    if res.returncode == 0:
        return CheckResult("mypy", "PASS", "No type errors found")
    return CheckResult(
        "mypy",
        "FAIL",
        res.stdout.strip(),
        "MyPy does not auto-fix. Please correct type errors manually.",
    )


def scan_for_todos(files: List[Path]) -> CheckResult:
    issues = [
        f"- {file}:{i}: {line.strip()}"
        for file in files
        for i, line in enumerate(file.read_text(errors="ignore").splitlines(), 1)
        if TODO_PATTERN.search(line)
    ]
    if not issues:
        return CheckResult("TODO/FIXME Scan", "PASS", "No pending markers found")
    details = f"{len(issues)} issue(s) found:\n" + "\n".join(issues)
    return CheckResult(
        "TODO/FIXME Scan",
        "WARN",
        details,
        "Address the pending tasks described in the code.",
    )


def check_license_headers(files: List[Path]) -> CheckResult:
    missing_license = [
        str(p)
        for p in files
        if not any(
            LICENSE_PATTERN.search(line)
            for line in p.read_text(errors="ignore").splitlines()[:5]
        )
    ]
    if not missing_license:
        return CheckResult(
            "License Header", "PASS", "All checked files have a license header"
        )
    details = (
        "The following files are missing an SPDX license header:\n- "
        + "\n- ".join(missing_license)
    )
    suggestion = "Add a license identifier (e.g., `// SPDX-License-Identifier: MIT` or `# SPDX-License-Identifier: Apache-2.0`) to the top of each file."
    return CheckResult("License Header", "FAIL", details, suggestion)


def check_large_files(files: List[Path]) -> CheckResult:
    large_files = [
        f"{p} ({p.stat().st_size / 1024 / 1024:.2f} MiB)"
        for p in files
        if p.is_file() and p.stat().st_size > LFS_THRESHOLD_BYTES
    ]
    if not large_files:
        return CheckResult(
            "Large File Check",
            "PASS",
            f"No files > {LFS_THRESHOLD_BYTES/1024/1024:.0f} MiB",
        )
    details = "The following files should be tracked with Git LFS:\n- " + "\n- ".join(
        large_files
    )
    suggestion = 'For each file, run `git lfs track "<file>"` and then re-add the file to the staging area.'
    return CheckResult("Large File Check", "FAIL", details, suggestion)


def generate_plain_text_report(results: List[CheckResult]) -> str:
    """Generates a structured, plain-text report for console output."""
    report_parts = []

    # --- Part 1: Summary Table ---
    summary_data = defaultdict(lambda: {"errors": 0, "warnings": 0})
    issue_details = defaultdict(list)

    for res in results:
        if res.status == "FAIL":
            summary_data[res.item]["errors"] += 1
            issue_details[res.item].append(res)
        elif res.status == "WARN":
            summary_data[res.item]["warnings"] += 1
            issue_details[res.item].append(res)

    # Create summary table
    tool_col_width = (
        max(len(tool) for tool in summary_data.keys()) if summary_data else 4
    )
    tool_col_width = max(tool_col_width, len("Tool"))

    header = f"| {'Tool'.ljust(tool_col_width)} | Errors | Warnings |\n"
    separator = f"+-{'-' * tool_col_width}-+--------+----------+\n"
    summary_table_rows = [separator, header, separator]
    for tool, counts in sorted(summary_data.items()):
        row = f"| {tool.ljust(tool_col_width)} | {str(counts['errors']).center(6)} | {str(counts['warnings']).center(8)} |\n"
        summary_table_rows.append(row)
    summary_table_rows.append(separator)

    report_parts.extend(
        [
            "---",
            "## Check Summary ",
            "---" "".join(summary_table_rows),
        ]
    )

    # --- Part 2: Issue Details ---
    if not issue_details:
        report_parts.append("\nNo issues found that require attention.")
        return "\n".join(report_parts)

    report_parts.extend(
        [
            "\n\n---",
            "## Issue Details ",
        ]
    )

    for tool, issues in sorted(issue_details.items()):
        report_parts.append(f"\n\n### [ {tool} ]")
        report_parts.append("-" * (len(tool) + 4))
        for i, issue in enumerate(issues, 1):
            details = issue.details.strip()
            report_parts.append(f"\n{i}. [{issue.status}]")
            # Indent multi-line details for readability
            for line in details.splitlines():
                report_parts.append(f"   {line}")
            if issue.fix_suggestion:
                report_parts.append(
                    f"   -> Suggestion: {issue.fix_suggestion}")

    return "\n".join(report_parts)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Pre-commit checks script with auto-fixing."
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "-t", "--tree", action="store_true", help="Check all files in the Git tree."
    )
    group.add_argument(
        "-e", "--edited", action="store_true", help="Check all edited (unstaged) files."
    )
    group.add_argument(
        "-s",
        "--staged",
        action="store_true",
        default=True,
        help="Check staged files (default).",
    )
    args = parser.parse_args()
    mode = "tree" if args.tree else "edited" if args.edited else "staged"

    print(f"Running checks on '{mode}' files...")
    all_files = get_git_files(mode)

    if not all_files:
        print("No files to check. Exiting.")
        return 0

    c_cpp_files = filter_files_by_extension(all_files, C_CPP_EXTENSIONS)
    py_files = filter_files_by_extension(all_files, PYTHON_EXTENSIONS)
    source_files = c_cpp_files + py_files

    tasks: List[Tuple[str, Callable[[], CheckResult]]] = []
    tool_availability: Dict[str, bool] = {}

    # 1. Tool availability
    for tool in TOOLS_TO_PROBE:
        result = check_tool_availability(tool)
        tool_availability[tool] = result.is_successful()
        tasks.append((f"Availability: {tool}", lambda r=result: r))

    cdb_path = find_compilation_database()
    cdb_check = CheckResult(
        "compile_commands.json",
        "PASS" if cdb_path else "WARN",
        f"Found: {cdb_path}" if cdb_path else "Not Found",
        "If missing, generate it via your build system (e.g., CMake) for clang-tidy to work.",
    )
    tasks.append(("Compilation DB", lambda r=cdb_check: r))

    # 2. Formatters & Linters (with auto-fix)
    if tool_availability.get("clang-format"):
        tasks.append(
            ("clang-format", lambda: format_clang_format(c_cpp_files)))
    if tool_availability.get("black"):
        tasks.append(("black", lambda: format_python(py_files, "black")))
    if tool_availability.get("autopep8"):
        tasks.append(("autopep8", lambda: format_python(py_files, "autopep8")))
    if tool_availability.get("clang-tidy"):
        tasks.append(
            ("clang-tidy", lambda: lint_clang_tidy(c_cpp_files, cdb_path)))
    if tool_availability.get("ruff"):
        tasks.append(("ruff", lambda: lint_ruff(py_files)))
    if tool_availability.get("mypy"):
        tasks.append(("mypy", lambda: lint_mypy(py_files)))

    # 3. Custom checks
    tasks.append(("TODO/FIXME Scan", lambda: scan_for_todos(source_files)))
    # tasks.append(
    # ("License Header", lambda: check_license_headers(source_files)))
    tasks.append(("Large File Check", lambda: check_large_files(all_files)))

    results: List[CheckResult] = []
    print(f"\nQueued {len(tasks)} checks to run in parallel...")
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
        future_to_name = {executor.submit(func): name for name, func in tasks}
        for i, future in enumerate(as_completed(future_to_name), 1):
            name = future_to_name[future]
            print(f"[{i}/{len(tasks)}] FINISHED: {name}")
            try:
                results.append(future.result())
            except Exception as exc:
                results.append(
                    CheckResult(
                        "Script Error",
                        "FAIL",
                        f"An exception occurred during check '{name}': {exc}",
                    )
                )

    report = generate_plain_text_report(results)

    print("\n\n--- Pre-Commit Check Report ---")
    print(report)
    print("\n--- End of Report ---\n")

    with open("check_result.md", "w") as f:
        f.write(report)

    if any(not r.is_successful() for r in results):
        print("❌ Commit hook failed. Please fix the issues above.")
        return 1

    print("✅ All checks passed. Some issues may have been auto-fixed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

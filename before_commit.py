#!/usr/bin/env python3
# before_commit.py — Industrial-grade pre-commit hook (with tool probe & markdown report)
# ░░░ 仅修改点 ░░░
# 1) 先检查 clang-format / clang-tidy / black / ruff / mypy / autopep8 是否可用
# 2) 结果汇总为 markdown 表，同步打印到终端与写入 check_result.md
# 3) 若缺工具 → 只报错，不崩溃；真正代码/类型/样式检查依赖于对应工具仍会阻断

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess as sp
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Dict, List, Tuple

# ────────────── 配置 ──────────────
CC_EXT = {".c", ".cc", ".cpp", ".h", ".hpp"}
PY_EXT = {".py"}
TOOLS  = ["clang-format", "clang-tidy", "black", "ruff", "mypy", "autopep8"]
MAX_WORKERS = os.cpu_count() or 4
TODO_PAT     = re.compile(r"\b(TODO|FIXME|BUG)\b", re.I)
LICENSE_PAT  = re.compile(r"SPDX-License-Identifier:", re.I)
LFS_THRESHOLD = 5 * 1024 * 1024  # 5 MiB

EXCEPTED_FILES = {"download_dep.py", "rebuild.py"}
# ────────────────────────────────
CDB_CANDIDATES = ["compile_commands.json", "build/compile_commands.json"]

results: List[Tuple[str, str, str]] = []           # (Item, Status, Detail)
fatal_error = False                                # 任何阻断条件置 True




# ── 公共辅助 ─────────────────────
def run(cmd: List[str], fatal: bool = True, **kw) -> None:
    """subprocess.run 封装；fatal=True 时非零即阻断"""
    global fatal_error
    res = sp.run(cmd, **kw)
    if fatal and res.returncode:
        results.append((" ".join(cmd[:2]), "ERROR", f"exit code {res.returncode}"))
        fatal_error = True

def find_cdb() -> str | None:
    for c in CDB_CANDIDATES:
        p = Path(c)
        if p.is_file():
            return str(p.parent)
    return None

# ── 静态分析 (传入 cdb_dir) ──
def lint_clang_tidy(paths: List[Path], tool_ok: bool, cdb_dir: str | None) -> None:
    if not tool_ok or not paths:
        return
    if cdb_dir is None:
        add_result("compile_commands.json", False, "not found")
        return                      # 已记录为 ERROR
    checks = "-*,clang-analyzer-*,bugprone-*,modernize-*,performance-*"
    errs   = "clang-analyzer-*,bugprone-*"
    run(
        ["clang-tidy", "-quiet", f"-checks={checks}",
         f"-warnings-as-errors={errs}", "-p", cdb_dir, *map(str, paths)]
    )
def git_files(mode: str) -> List[Path]:
    if mode == "tree":
        out = sp.check_output(["git", "ls-files"], text=True)
    elif mode == "edited":
        out = sp.check_output(["git", "diff", "HEAD", "--name-only"], text=True)
    else:  # staged
        out = sp.check_output(["git", "diff", "--cached", "--name-only"], text=True)
    return [
        Path(p) for p in out.splitlines()
        if p and Path(p).name not in EXCEPTED_FILES
    ]


def filter_ext(paths: List[Path], exts: set[str]) -> List[Path]:
    return [p for p in paths if p.suffix in exts and p.exists()]


def add_result(item: str, ok: bool, detail: str = "") -> None:
    results.append((item, "OK" if ok else "ERROR", detail))
    global fatal_error
    if not ok:
        fatal_error = True


# ── 工具可用性探测 ────────────────
def probe_tools() -> Dict[str, bool]:
    availability: Dict[str, bool] = {}
    for t in TOOLS:
        avail = shutil.which(t) is not None
        availability[t] = avail
        add_result(t, avail, "" if avail else "not installed / not in PATH")
    return availability


# ── 格式化 ───────────────────────
def fmt_clang(paths: List[Path], tool_ok: bool) -> None:
    if tool_ok and paths:
        run(["clang-format", "-i", *map(str, paths)], fatal=False)


def fmt_black(paths: List[Path], tool_ok: bool) -> None:
    if tool_ok and paths:
        run(["black", "--quiet", *map(str, paths)], fatal=False)


# ── 静态分析 ─────────────────────
def lint_clang_tidy(paths: List[Path], tool_ok: bool) -> None:
    if not tool_ok or not paths:
        return
    if not Path("compile_commands.json").exists():
        results.append(("clang-tidy", "WARN", "compile_commands.json not found, skipped"))
        return
    checks = "-*,clang-analyzer-*,bugprone-*,modernize-*,performance-*"
    errs   = "clang-analyzer-*,bugprone-*"
    run(
        ["clang-tidy", "-quiet", f"-checks={checks}", f"-warnings-as-errors={errs}", "-p", ".", *map(str, paths)]
    )


def lint_ruff(paths: List[Path], tool_ok: bool) -> None:
    if tool_ok and paths:
        run(["ruff", "check", *map(str, paths)])


def lint_mypy(paths: List[Path], tool_ok: bool) -> None:
    if tool_ok and paths:
        run(["mypy", "--strict", *map(str, paths)])


# ── 自定义检查 ───────────────────
def scan_todo(paths: List[Path]) -> None:
    issues: List[str] = []

    def _scan(p: Path) -> None:
        for ln, line in enumerate(p.read_text(errors="ignore").splitlines(), 1):
            if TODO_PAT.search(line):
                issues.append(f"{p}:{ln}: {line.strip()}")

    with ThreadPoolExecutor(MAX_WORKERS) as pool:
        pool.map(_scan, paths)

    add_result("TODO/FIXME", len(issues) == 0, f"{len(issues)} issue(s)" if issues else "")
    if issues:
        print("".join(" • "+i+"\n" for i in issues), file=sys.stderr)


def check_license(paths: List[Path]) -> None:
    missing = [
        str(p)
        for p in paths
        if not any(LICENSE_PAT.search(l) for l in p.read_text(errors="ignore").splitlines()[:5])
    ]
    add_result("SPDX license header", len(missing) == 0, f"{len(missing)} file(s) missing")
    if missing:
        print("\n".join(missing), file=sys.stderr)


def check_large(paths: List[Path]) -> None:
    offenders = [p for p in paths if p.stat().st_size > LFS_THRESHOLD]
    add_result("Large file check", len(offenders) == 0, f"{len(offenders)} file(s) >5 MiB")
    if offenders:
        print("\n".join(str(p) for p in offenders), file=sys.stderr)


# ── CLI ─────────────────────────
def parse_args() -> str:
    ap = argparse.ArgumentParser("pre-commit checks")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--check-git-tree", action="store_true", help="扫描整个仓库")
    g.add_argument("--check-edited-files", action="store_true", help="扫描工作区改动")
    args = ap.parse_args()
    if args.check_git_tree:
        return "tree"
    if args.check_edited_files:
        return "edited"
    return "staged"


# ── 主流程 ───────────────────────
def main() -> None:
    mode = parse_args()
    targets = git_files(mode)
    cc_files = filter_ext(targets, CC_EXT)
    py_files = filter_ext(targets, PY_EXT)

    # 1) 工具可用性
    tool_ok = probe_tools()
    tool_ok    = probe_tools()
    cdb_dir    = find_cdb()
    add_result("compile_commands.json", cdb_dir is not None,
               cdb_dir or "not found")          # 结果表显示
    
    # 2) 自动格式化
    fmt_clang(cc_files, tool_ok["clang-format"])
    fmt_black(py_files, tool_ok["black"])

    # 3) 静态分析
    lint_clang_tidy(cc_files, tool_ok["clang-tidy"])
    lint_ruff(py_files, tool_ok["ruff"])
    lint_mypy(py_files, tool_ok["mypy"])

    # 4) 自定义检查
    scan_todo(cc_files + py_files)
    check_license(cc_files + py_files)
    check_large(targets)

    # 5) 输出 markdown 报告
    md_lines = ["| Item | Status | Detail |", "|------|--------|--------|"]
    for item, st, dt in results:
        md_lines.append(f"| {item} | {st} | {dt} |")
    report = "\n".join(md_lines)
    Path("check_result.md").write_text(report, encoding="utf-8")

    print(report)                # 同步打印
    print("\n结果另写入 check_result.md")

    if fatal_error:
        sys.exit(1)


if __name__ == "__main__":
    main()

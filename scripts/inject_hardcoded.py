#!/usr/bin/env python3
"""
硬编码 C++ 文本汉化注入脚本
构建前注入中文，构建后还原英文。

映射表按**整行**匹配：每条目的 `line` 与 `zh` 都是完整源码行，注入时整行替换。
因此只有被显式列出的那一行会被改动，查找键（Get/Has/== 等）所在的行只要不入表
就绝不会被翻译，从结构上排除了"同一个字符串既是显示文本又是查找键"的误译。

用法：
    python scripts/inject_hardcoded.py                  # 注入中文
    python scripts/inject_hardcoded.py --restore         # 还原英文
    python scripts/inject_hardcoded.py --check           # 检查每行是否恰好匹配
    python scripts/inject_hardcoded.py --dry-run         # 同 --check，不修改源码
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
MAPPING_FILE = REPO_ROOT.parent / "es_translation" / "hardcoded_zh.json"
INJECTED_STATE = SCRIPT_DIR / ".injected_state.json"
LEGACY_RECORD = SCRIPT_DIR / ".injected_files"


def read_source(path: Path) -> str:
    with open(path, encoding="utf-8", newline="") as source:
        return source.read()


def write_source(path: Path, content: str) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as source:
            source.write(content)
            source.flush()
            os.fsync(source.fileno())
        os.chmod(temporary_path, path.stat().st_mode)
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def load_mapping() -> dict:
    with open(MAPPING_FILE, encoding="utf-8") as f:
        return json.load(f)


def inject():
    if INJECTED_STATE.exists():
        raise SystemExit("已有未恢复的注入状态，请先执行 --restore")
    if LEGACY_RECORD.exists():
        raise SystemExit("检测到旧版注入记录；为避免覆盖本地修改，请交由 Agent 检查后恢复")

    data = load_mapping()
    entries = data.get("entries", [])
    modified_files: dict[str, dict[str, str]] = {}
    warnings: list[str] = []

    by_file: dict[str, list] = {}
    for entry in entries:
        by_file.setdefault(entry["file"], []).append(entry)

    for file_path, file_entries in by_file.items():
        full_path = REPO_ROOT / file_path
        if not full_path.exists():
            warnings.append(f"文件不存在: {file_path}")
            continue

        original_content = read_source(full_path)
        lines = original_content.splitlines(keepends=True)

        # Resolve every entry against the *original* lines first, so that a
        # replacement can never be matched again by a later entry.
        planned: dict[int, str] = {}
        for entry in file_entries:
            target = entry["line"]
            replacement = entry["zh"]
            expected = entry.get("count", 1)

            indices = [
                i for i, line in enumerate(lines)
                if line.rstrip("\r\n") == target
            ]
            if len(indices) != expected:
                warnings.append(
                    f"匹配数不符(期望{expected},实际{len(indices)}): "
                    f"[{file_path}] {target.strip()[:70]}"
                )
                continue
            for i in indices:
                # Preserve this line's own line ending.
                ending = lines[i][len(lines[i].rstrip("\r\n")):]
                planned[i] = replacement + ending

        if not planned:
            continue

        for i, replacement in planned.items():
            lines[i] = replacement

        modified_files[file_path] = {
            "original": original_content,
            "injected": "".join(lines),
        }

    if modified_files:
        state = {"version": 1, "files": modified_files}
        temporary_state = INJECTED_STATE.with_suffix(".tmp")
        temporary_state.write_text(
            json.dumps(state, ensure_ascii=False),
            encoding="utf-8",
        )
        temporary_state.replace(INJECTED_STATE)

        for file_path, contents in modified_files.items():
            write_source(REPO_ROOT / file_path, contents["injected"])

    print(f"注入完成: {len(modified_files)} 个文件被修改")
    for w in warnings:
        print(f"  警告: {w}")
    if not modified_files:
        print("  没有文件被修改")


def restore():
    if not INJECTED_STATE.exists():
        if LEGACY_RECORD.exists():
            raise SystemExit("检测到旧版注入记录；为避免覆盖本地修改，请交由 Agent 检查后恢复")
        print("没有注入记录，无需还原")
        return

    state = json.loads(INJECTED_STATE.read_text(encoding="utf-8"))
    if state.get("version") != 1 or not isinstance(state.get("files"), dict):
        raise SystemExit("注入状态格式无效，请交由 Agent 检查")

    conflicts: list[str] = []
    for file_path, contents in state["files"].items():
        current = read_source(REPO_ROOT / file_path)
        if current not in (contents["original"], contents["injected"]):
            conflicts.append(file_path)

    if conflicts:
        print("以下文件在注入后又发生修改，已停止恢复以避免覆盖:", file=sys.stderr)
        for file_path in conflicts:
            print(f"  {file_path}", file=sys.stderr)
        raise SystemExit("请由 Agent 合并这些修改后再次恢复")

    restored_count = 0
    for file_path, contents in state["files"].items():
        full_path = REPO_ROOT / file_path
        if read_source(full_path) == contents["injected"]:
            write_source(full_path, contents["original"])
            restored_count += 1
            print(f"已还原: {file_path}")

    INJECTED_STATE.unlink()
    print(f"还原完成: {restored_count} 个文件")


def recover_unrecorded():
    if INJECTED_STATE.exists() or LEGACY_RECORD.exists():
        raise SystemExit("检测到注入状态记录，请使用 --restore")

    data = load_mapping()
    mapped_files = {entry["file"] for entry in data.get("entries", [])}
    result = subprocess.run(
        ["git", "diff", "--name-only", "HEAD", "--", "source"],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    changed_files = [path for path in result.stdout.splitlines() if path]
    if not changed_files:
        print("源码没有需要恢复的未提交修改")
        return

    unexpected = [path for path in changed_files if path not in mapped_files]
    if unexpected:
        print("以下修改不在硬编码注入映射中，已停止恢复:", file=sys.stderr)
        for file_path in unexpected:
            print(f"  {file_path}", file=sys.stderr)
        raise SystemExit("请由 Agent 检查映射外修改")

    without_chinese = [
        path for path in changed_files
        if not re.search(r"[\u3400-\u9fff]", read_source(REPO_ROOT / path))
    ]
    if without_chinese:
        print("以下修改没有检测到中文，已停止恢复:", file=sys.stderr)
        for file_path in without_chinese:
            print(f"  {file_path}", file=sys.stderr)
        raise SystemExit("请由 Agent 检查非注入修改")

    subprocess.run(
        ["git", "checkout", "--", *changed_files],
        cwd=REPO_ROOT,
        check=True,
    )
    print(f"无记录恢复完成: {len(changed_files)} 个源码文件")


def check():
    data = load_mapping()
    entries = data.get("entries", [])
    ok_count = 0
    fail_count = 0

    by_file: dict[str, list] = {}
    for entry in entries:
        by_file.setdefault(entry["file"], []).append(entry)

    for file_path, file_entries in by_file.items():
        full_path = REPO_ROOT / file_path
        if not full_path.exists():
            print(f"  FAIL 文件不存在: [{file_path}]")
            fail_count += len(file_entries)
            continue

        lines = full_path.read_text(encoding="utf-8").splitlines()
        for entry in file_entries:
            target = entry["line"]
            expected = entry.get("count", 1)
            found = sum(1 for line in lines if line == target)

            if found == expected:
                ok_count += 1
            else:
                print(
                    f"  FAIL 匹配数不符(期望{expected},实际{found}): "
                    f"[{file_path}] {target.strip()[:70]}"
                )
                fail_count += 1

    print(f"检查完成: {ok_count} 通过, {fail_count} 失败")
    return 1 if fail_count else 0


def main():
    parser = argparse.ArgumentParser(description="硬编码 C++ 文本汉化注入")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--restore", action="store_true", help="还原英文源码")
    group.add_argument("--check", "--dry-run", action="store_true", help="检查映射表匹配，不修改源码")
    group.add_argument(
        "--recover-unrecorded",
        action="store_true",
        help="由 Agent 确认后恢复缺少状态记录的中文注入",
    )
    args = parser.parse_args()

    if args.restore:
        restore()
    elif args.recover_unrecorded:
        recover_unrecorded()
    elif args.check:
        raise SystemExit(check())
    else:
        inject()


if __name__ == "__main__":
    main()

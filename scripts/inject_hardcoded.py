#!/usr/bin/env python3
"""
硬编码 C++ 文本汉化注入脚本
构建前注入中文，构建后还原英文。

用法：
    python scripts/inject_hardcoded.py                  # 注入中文
    python scripts/inject_hardcoded.py --restore         # 还原英文
    python scripts/inject_hardcoded.py --check           # 检查映射表匹配
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


def find_string_literal(line: str, target: str) -> list[tuple[int, int]]:
    """Find occurrences of a complete string literal whose content equals target.

    Returns list of (start, end) positions of the quote-delimited literal in the line.
    Only matches full string literals, not substrings.
    Handles C++ escape sequences (e.g. \" in source becomes " in content).
    """
    results = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line):
        raw = m.group(1)
        # Unescape C++ sequences for comparison
        unescaped = raw.replace('\\"', '"').replace('\\\\', '\\').replace('\\n', '\n').replace('\\t', '\t')
        if raw == target or unescaped == target:
            results.append((m.start(), m.end()))
    return results


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
        changed = False

        for entry in file_entries:
            en = entry["en"]
            zh = entry["zh"]
            context = entry.get("context")

            # Escape for C++ string literal: actual newlines -> \n, tabs -> \t, etc.
            zh_escaped = (zh
                .replace("\\", "\\\\")  # backslash first
                .replace("\n", "\\n")   # newline
                .replace("\t", "\\t")   # tab
                .replace("\r", "\\r")   # carriage return
                .replace('"', '\\"'))   # quotes

            found_count = 0
            for i, line in enumerate(lines):
                matches = find_string_literal(line, en)
                if not matches:
                    continue

                if context and context not in line:
                    continue

                for start, end in matches:
                    lines[i] = line[:start + 1] + zh_escaped + line[end - 1:]
                    line = lines[i]
                    found_count += 1
                    changed = True

            if found_count == 0:
                warnings.append(f"未匹配: [{file_path}] \"{en}\"")
            elif found_count > 1 and not context:
                warnings.append(f"多处匹配({found_count}): [{file_path}] \"{en}\"")

        if changed:
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

    for entry in entries:
        file_path = entry["file"]
        en = entry["en"]
        full_path = REPO_ROOT / file_path

        if not full_path.exists():
            print(f"  FAIL 文件不存在: [{file_path}] \"{en}\"")
            fail_count += 1
            continue

        lines = full_path.read_text(encoding="utf-8").splitlines()
        found = False
        for line in lines:
            if find_string_literal(line, en):
                found = True
                break

        if found:
            ok_count += 1
        else:
            print(f"  FAIL 未找到: [{file_path}] \"{en}\"")
            fail_count += 1

    print(f"检查完成: {ok_count} 通过, {fail_count} 失败")


def main():
    parser = argparse.ArgumentParser(description="硬编码 C++ 文本汉化注入")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--restore", action="store_true", help="还原英文源码")
    group.add_argument("--check", action="store_true", help="检查映射表匹配")
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
        check()
    else:
        inject()


if __name__ == "__main__":
    main()

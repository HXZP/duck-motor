#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import json
import os
from pathlib import Path
import sys
from typing import Any


def get_workspace_root() -> Path:
    """
    @brief 获取 Bazel 启动时传入的工作区根目录。
    @return Path 返回工作区根目录路径。
    """
    workspace_root = os.environ.get("BUILD_WORKSPACE_DIRECTORY", "")

    if workspace_root:
        return Path(workspace_root).resolve()
    else:
        return Path.cwd().resolve()


def resolve_workspace_path(workspace_root: Path, path_text: str) -> Path:
    """
    @brief 将输入路径解析为绝对路径。
    @param workspace_root 工作区根目录路径。
    @param path_text 输入路径文本。
    @return Path 返回绝对路径。
    """
    path = Path(path_text)

    if path.is_absolute():
        return path.resolve()

    workspace_path = (workspace_root / path).resolve()
    if workspace_path.exists():
        return workspace_path
    else:
        return (Path.cwd() / path).resolve()


def format_path(path: Path) -> str:
    """
    @brief 将路径格式化为 clangd 兼容的文本。
    @param path 路径对象。
    @return str 返回使用正斜杠的路径文本。
    """
    return path.resolve().as_posix()


def load_compile_commands(path: Path) -> list[dict[str, Any]]:
    """
    @brief 读取一个 compile_commands.json 文件。
    @param path 编译数据库文件路径。
    @return list[dict[str, Any]] 返回编译命令条目列表。
    """
    if not path.is_file():
        raise FileNotFoundError("找不到编译数据库文件: " + str(path))

    with path.open("r", encoding="utf-8") as file:
        data = json.load(file)

    if not isinstance(data, list):
        raise ValueError("编译数据库格式错误: " + str(path))

    return data


def normalize_entry(workspace_root: Path, entry: dict[str, Any]) -> dict[str, Any]:
    """
    @brief 规范化单条 clangd 编译命令。
    @param workspace_root 工作区根目录路径。
    @param entry 原始编译命令条目。
    @return dict[str, Any] 返回规范化后的编译命令条目。
    """
    if "file" not in entry:
        raise ValueError("编译命令缺少 file 字段")

    normalized = dict(entry)
    source_path = resolve_workspace_path(workspace_root, str(entry["file"]))

    normalized["directory"] = format_path(workspace_root)
    normalized["file"] = format_path(source_path)

    if "output" in normalized:
        output_path = resolve_workspace_path(workspace_root, str(normalized["output"]))
        normalized["output"] = format_path(output_path)

    return normalized


def merge_compile_commands(workspace_root: Path, input_paths: list[Path]) -> list[dict[str, Any]]:
    """
    @brief 合并多个编译数据库，并按源文件路径去重。
    @param workspace_root 工作区根目录路径。
    @param input_paths 编译数据库输入文件路径列表。
    @return list[dict[str, Any]] 返回合并后的编译命令条目列表。
    """
    merged_entries = []
    seen_files = set()

    for input_path in input_paths:
        entries = load_compile_commands(input_path)

        for entry in entries:
            normalized = normalize_entry(workspace_root, entry)
            source_file = normalized["file"]

            if source_file not in seen_files:
                seen_files.add(source_file)
                merged_entries.append(normalized)

    return merged_entries


def write_compile_commands(output_path: Path, entries: list[dict[str, Any]]) -> None:
    """
    @brief 写出 clangd 使用的 compile_commands.json 文件。
    @param output_path 输出文件路径。
    @param entries 编译命令条目列表。
    @return None。
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", encoding="utf-8", newline="\n") as file:
        json.dump(entries, file, ensure_ascii=False, indent=2)
        file.write("\n")


def parse_args() -> argparse.Namespace:
    """
    @brief 解析刷新 clangd 编译数据库的命令行参数。
    @return argparse.Namespace 返回命令行参数对象。
    """
    parser = argparse.ArgumentParser(description="刷新 clangd 使用的 compile_commands.json。")
    parser.add_argument("--input", action="append", required=True, help="Bazel 生成的编译数据库路径。")
    parser.add_argument("--output", required=True, help="clangd 使用的编译数据库输出路径。")
    return parser.parse_args()


def main() -> int:
    """
    @brief 执行 clangd 编译数据库刷新流程。
    @return int 返回进程退出码，0 表示成功。
    """
    args = parse_args()
    workspace_root = get_workspace_root()
    input_paths = [
        resolve_workspace_path(workspace_root, input_path)
        for input_path in args.input
    ]
    output_path = resolve_workspace_path(workspace_root, args.output)
    entries = merge_compile_commands(workspace_root, input_paths)

    write_compile_commands(output_path, entries)
    print("clangd compile_commands: " + str(output_path))
    print("entries: " + str(len(entries)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

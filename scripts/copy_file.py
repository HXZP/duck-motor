#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
from pathlib import Path
import shutil


def copy_file(input_path: Path, output_path: Path) -> None:
    """
    @brief 复制一个构建产物文件。
    @param input_path 输入文件路径。
    @param output_path 输出文件路径。
    @return 无。
    """
    if not input_path.is_file():
        raise FileNotFoundError("找不到输入文件: " + str(input_path))

    output_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(input_path, output_path)


def parse_args() -> argparse.Namespace:
    """
    @brief 解析文件复制脚本参数。
    @return 返回命令行参数对象。
    """
    parser = argparse.ArgumentParser(description="复制 Bazel 构建产物。")
    parser.add_argument("--input", required=True, type=Path, help="输入文件路径。")
    parser.add_argument("--output", required=True, type=Path, help="输出文件路径。")
    return parser.parse_args()


def main() -> int:
    """
    @brief 执行文件复制入口。
    @return 返回进程退出码，0 表示成功。
    """
    args = parse_args()
    copy_file(args.input, args.output)
    print("copy output: " + str(args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

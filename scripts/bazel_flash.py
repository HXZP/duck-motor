#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import glob
import os
from pathlib import Path
import shutil
import subprocess
import sys


DEFAULT_DEVICE = "STM32F103C8"
DEFAULT_ADDRESS = "0x08000000"
DEFAULT_JLINK_SPEED_KHZ = "4000"
DEFAULT_JLINK_PATHS = [
    r"C:\Program Files\SEGGER\JLink\JLink.exe",
    r"C:\Program Files\SEGGER\JLink_V922\JLink.exe",
]


def get_workspace_root() -> Path:
    """
    @brief 获取 Bazel 启动时传入的工作区根目录。
    @return 返回工作区根目录路径。
    """
    workspace_root = os.environ.get("BUILD_WORKSPACE_DIRECTORY", "")

    if workspace_root:
        return Path(workspace_root).resolve()
    else:
        return Path.cwd().resolve()


def ensure_build_dir(workspace_root: Path) -> Path:
    """
    @brief 确保烧录临时脚本输出目录存在。
    @param workspace_root 工作区根目录路径。
    @return 返回烧录临时脚本输出目录路径。
    """
    build_dir = workspace_root / "build"
    build_dir.mkdir(parents=True, exist_ok=True)
    return build_dir


def resolve_image_path(workspace_root: Path, image_path: str) -> Path:
    """
    @brief 解析待烧录镜像路径。
    @param workspace_root 工作区根目录路径。
    @param image_path 待烧录镜像路径。
    @return 返回绝对镜像路径。
    """
    image = Path(image_path)

    if image.is_absolute():
        return image.resolve()
    else:
        return (workspace_root / image).resolve()


def find_jlink(requested_flash_exe: str) -> str:
    """
    @brief 查找当前系统可用的 J-Link 可执行文件。
    @param requested_flash_exe 用户显式指定的 J-Link 可执行文件路径。
    @return 返回 J-Link 可执行文件路径。
    """
    if requested_flash_exe:
        return requested_flash_exe

    for name in ["JLink", "JLinkExe", "JLink.exe"]:
        exe = shutil.which(name)

        if exe:
            return exe

    for path in DEFAULT_JLINK_PATHS:
        if os.path.isfile(path):
            return path

    candidates = sorted(glob.glob(r"C:\Program Files\SEGGER\JLink*\JLink.exe"))

    if candidates:
        return candidates[-1]
    else:
        raise RuntimeError("未找到 J-Link 可执行文件，请通过 --flash-exe 指定 JLink.exe 路径。")


def build_jlink_lines(
    image_path: Path,
    address: str,
    device: str,
    speed: str,
    skip_verify: bool,
) -> list[str]:
    """
    @brief 生成 J-Link Commander 烧录命令。
    @param image_path 待烧录镜像路径。
    @param address 烧录起始地址。
    @param device J-Link 设备型号。
    @param speed SWD 速率，单位：kHz。
    @param skip_verify 是否跳过 verifybin 校验。
    @return 返回 J-Link Commander 命令列表。
    """
    lines = [
        "si 1",
        "speed " + speed,
        "device " + device,
        "r",
        "h",
        "erase",
        "loadbin \"" + str(image_path) + "\", " + address,
    ]

    if not skip_verify:
        lines.append("verifybin \"" + str(image_path) + "\", " + address)

    lines.extend([
        "r",
        "qc",
    ])
    return lines


def print_jlink_script(script_path: Path, lines: list[str]) -> None:
    """
    @brief 打印将要执行的 J-Link Commander 脚本内容。
    @param script_path J-Link Commander 临时脚本路径。
    @param lines J-Link Commander 命令列表。
    @return 无。
    """
    print("J-Link command preview:")
    print("Script : " + str(script_path))

    for line in lines:
        print("  " + line)


def run_jlink_script(
    script_path: Path,
    lines: list[str],
    requested_flash_exe: str,
    list_only: bool,
) -> int:
    """
    @brief 写入并执行 J-Link Commander 脚本。
    @param script_path J-Link Commander 临时脚本路径。
    @param lines J-Link Commander 命令列表。
    @param requested_flash_exe 用户显式指定的 J-Link 可执行文件路径。
    @param list_only 是否只打印脚本不执行烧录。
    @return 返回 J-Link Commander 进程退出码。
    """
    print_jlink_script(script_path, lines)

    if list_only:
        print("ListOnly enabled. Flash skipped.")
        return 0

    flash_exe = find_jlink(requested_flash_exe)
    script_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    try:
        return subprocess.call([flash_exe, "-CommanderScript", str(script_path)])
    finally:
        if script_path.exists():
            script_path.unlink()


def parse_args() -> argparse.Namespace:
    """
    @brief 解析 Bazel 烧录入口参数。
    @return 返回命令行参数对象。
    """
    parser = argparse.ArgumentParser(description="Run motor_duck flash script from Bazel.")
    parser.add_argument("--image", required=True, help="待烧录 full bin 镜像路径。")
    parser.add_argument("--address", default=DEFAULT_ADDRESS, help="烧录起始地址。")
    parser.add_argument("--device", default=DEFAULT_DEVICE, help="J-Link 设备型号。")
    parser.add_argument("--speed", default=DEFAULT_JLINK_SPEED_KHZ, help="SWD 速率，单位：kHz。")
    parser.add_argument("--flash-exe", default="", help="JLink/JLinkExe 可执行文件路径。")
    parser.add_argument("--list-only", action="store_true", help="只打印 J-Link 脚本，不执行烧录。")
    parser.add_argument("--skip-verify", action="store_true", help="跳过 verifybin 校验。")
    return parser.parse_args()


def main() -> int:
    """
    @brief 执行 Bazel full 镜像烧录入口。
    @return 返回进程退出码，0 表示成功。
    """
    args = parse_args()
    workspace_root = get_workspace_root()
    build_dir = ensure_build_dir(workspace_root)
    image_path = resolve_image_path(workspace_root, args.image)
    script_path = build_dir / "flash_full.jlink"

    if not image_path.is_file():
        raise FileNotFoundError("找不到待烧录镜像: " + str(image_path))

    lines = build_jlink_lines(
        image_path,
        args.address,
        args.device,
        args.speed,
        args.skip_verify,
    )

    print("Device : " + args.device)
    print("Image  : " + str(image_path))
    print("Address: " + args.address)
    return run_jlink_script(script_path, lines, args.flash_exe, args.list_only)


if __name__ == "__main__":
    sys.exit(main())

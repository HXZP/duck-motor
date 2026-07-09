#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
from datetime import datetime
import glob
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


DEFAULT_DEVICE = "STM32F103C8"
DEFAULT_ADDRESS = "0x08000000"
DEFAULT_ERASE_START = DEFAULT_ADDRESS
DEFAULT_ERASE_END = "0x0800FBFF"
DEFAULT_PRESERVE_START = "0x0800FC00"
DEFAULT_JLINK_SPEED_KHZ = "4000"
DEFAULT_JLINK_PATHS = [
    r"C:\Program Files\SEGGER\JLink\JLink.exe",
    r"C:\Program Files\SEGGER\JLink_V922\JLink.exe",
]


def parse_int(text: str) -> int:
    """
    @brief 解析十进制或十六进制整数。
    @param text 待解析文本。
    @return 返回解析后的整数值。
    """
    return int(text, 0)


def format_hex(value: int) -> str:
    """
    @brief 将整数格式化为 J-Link 使用的十六进制地址。
    @param value 待格式化整数。
    @return 返回十六进制地址字符串。
    """
    return f"0x{value:08X}"


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


def normalize_version(version: str) -> str:
    """
    @brief 规范化用户输入的版本号。
    @param version 用户输入版本号。
    @return 返回不带 v 前缀的版本号。
    """
    text = version.strip()

    if text.startswith("v") or text.startswith("V"):
        return text[1:]
    else:
        return text


def find_artifact_file(manifest: dict, role: str) -> str:
    """
    @brief 从发布包清单中查找指定角色文件。
    @param manifest 发布包清单。
    @param role 产物角色。
    @return 返回产物文件名。
    """
    for artifact in manifest.get("artifacts", []):
        if artifact.get("role") == role:
            return artifact["file"]

    raise RuntimeError("发布包清单中找不到产物角色: " + role)


def resolve_version_image_path(workspace_root: Path, version: str) -> Path:
    """
    @brief 根据发布版本解析 full bin 镜像路径。
    @param workspace_root 工作区根目录路径。
    @param version 发布版本号。
    @return 返回 full bin 镜像绝对路径。
    """
    normalized_version = normalize_version(version)
    package_dir = workspace_root / "firmware_package" / ("motor_duck_v" + normalized_version)
    manifest_path = package_dir / "manifest.json"

    if not manifest_path.is_file():
        raise FileNotFoundError("找不到版本发布包清单: " + str(manifest_path))

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    full_bin = package_dir / find_artifact_file(manifest, "full_bin")
    return full_bin.resolve()


def make_version_sort_key(version: str) -> tuple[int, int, int, str]:
    """
    @brief 生成版本排序键。
    @param version 版本号字符串。
    @return 返回版本排序键。
    """
    numbers = []

    for part in normalize_version(version).split("."):
        if part.isdigit():
            numbers.append(int(part))
        else:
            numbers.append(0)

    while len(numbers) < 3:
        numbers.append(0)

    return numbers[0], numbers[1], numbers[2], version


def format_timestamp(timestamp_s: float) -> str:
    """
    @brief 格式化文件时间戳。
    @param timestamp_s 时间戳，单位：秒。
    @return 返回可读时间字符串。
    """
    return datetime.fromtimestamp(timestamp_s).strftime("%Y-%m-%d %H:%M:%S")


def list_release_packages(workspace_root: Path) -> list[dict]:
    """
    @brief 列出根目录 firmware_package 下可烧录版本。
    @param workspace_root 工作区根目录路径。
    @return 返回可烧录版本列表。
    """
    archive_root = workspace_root / "firmware_package"
    packages = []

    if not archive_root.is_dir():
        return packages

    for package_dir in archive_root.glob("motor_duck_v*"):
        manifest_path = package_dir / "manifest.json"

        if not manifest_path.is_file():
            continue

        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        version = str(manifest.get("version", package_dir.name.replace("motor_duck_v", "", 1)))
        full_bin = package_dir / find_artifact_file(manifest, "full_bin")

        if not full_bin.is_file():
            continue

        packages.append(
            {
                "version": version,
                "package_dir": package_dir,
                "full_bin": full_bin.resolve(),
                "file": full_bin.name,
                "size": full_bin.stat().st_size,
                "time": format_timestamp(manifest_path.stat().st_mtime),
            }
        )

    packages.sort(key=lambda item: make_version_sort_key(item["version"]), reverse=True)
    return packages


def prompt_select_package(packages: list[dict]) -> dict:
    """
    @brief 交互选择待烧录版本。
    @param packages 可烧录版本列表。
    @return 返回用户选择的发布包信息。
    """
    print("可烧录版本：")

    for index, package in enumerate(packages, start=1):
        print(
            f"  {index}. v{package['version']}  "
            f"{package['file']}  "
            f"{package['size']} Byte  "
            f"{package['time']}"
        )

    while True:
        choice = input("请选择烧录版本序号，输入 q 退出: ").strip()

        if choice.lower() == "q":
            raise KeyboardInterrupt("用户取消烧录")

        if choice.isdigit():
            index = int(choice)

            if 1 <= index <= len(packages):
                return packages[index - 1]

        print("输入无效，请重新选择。")


def resolve_interactive_image_path(workspace_root: Path) -> Path:
    """
    @brief 通过交互菜单选择 full bin 镜像路径。
    @param workspace_root 工作区根目录路径。
    @return 返回用户选择的 full bin 镜像路径。
    """
    packages = list_release_packages(workspace_root)

    if not packages:
        raise RuntimeError("未找到可烧录发布包，请先执行 bazelisk build //:firmware")

    selected = prompt_select_package(packages)
    return selected["full_bin"]


def validate_image_preserve_area(
    image_path: Path,
    address: str,
    preserve_start: str,
) -> None:
    """
    @brief 检查待烧录镜像不会覆盖需要保留的 Flash 存储区。
    @param image_path 待烧录镜像路径。
    @param address 烧录起始地址。
    @param preserve_start 需要保留的 Flash 起始地址。
    @return 无。
    """
    start_address = parse_int(address)
    preserve_address = parse_int(preserve_start)
    image_size = image_path.stat().st_size
    image_end = start_address + image_size

    if image_end > preserve_address:
        raise RuntimeError(
            "待烧录镜像会覆盖用户存储区: "
            + "image=["
            + format_hex(start_address)
            + ", "
            + format_hex(image_end)
            + "), preserve_start="
            + format_hex(preserve_address)
        )


def validate_erase_preserve_area(
    erase_start: str,
    erase_end: str,
    preserve_start: str,
) -> None:
    """
    @brief 检查擦除范围不会触碰需要保留的 Flash 存储区。
    @param erase_start 擦除起始地址。
    @param erase_end 擦除结束地址。
    @param preserve_start 需要保留的 Flash 起始地址。
    @return 无。
    @note J-Link erase 结束地址按包含关系处理，所以结束地址必须小于保留区起始地址。
    """
    start_address = parse_int(erase_start)
    end_address = parse_int(erase_end)
    preserve_address = parse_int(preserve_start)

    if end_address < start_address:
        raise RuntimeError(
            "擦除范围无效: "
            + "erase_start="
            + format_hex(start_address)
            + ", erase_end="
            + format_hex(end_address)
        )

    if end_address >= preserve_address:
        raise RuntimeError(
            "擦除范围会覆盖用户存储区: "
            + "erase=["
            + format_hex(start_address)
            + ", "
            + format_hex(end_address)
            + "], preserve_start="
            + format_hex(preserve_address)
        )


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
    erase_start: str,
    erase_end: str,
) -> list[str]:
    """
    @brief 生成 J-Link Commander 烧录命令。
    @param image_path 待烧录镜像路径。
    @param address 烧录起始地址。
    @param device J-Link 设备型号。
    @param speed SWD 速率，单位：kHz。
    @param skip_verify 是否跳过 verifybin 校验。
    @param erase_start 擦除起始地址。
    @param erase_end 擦除结束地址。
    @return 返回 J-Link Commander 命令列表。
    """
    lines = [
        "si 1",
        "speed " + speed,
        "device " + device,
        "r",
        "h",
        "erase " + erase_start + ", " + erase_end,
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
    parser.add_argument("--default-image", default="", help=argparse.SUPPRESS)
    parser.add_argument("--image", default="", help="待烧录 full bin 镜像路径。")
    parser.add_argument("--version", default="", help="从根目录 firmware_package 选择待烧录版本。")
    parser.add_argument("--address", default=DEFAULT_ADDRESS, help="烧录起始地址。")
    parser.add_argument("--erase-start", default=DEFAULT_ERASE_START, help="擦除起始地址。")
    parser.add_argument("--erase-end", default=DEFAULT_ERASE_END, help="擦除结束地址。")
    parser.add_argument(
        "--preserve-start",
        default=DEFAULT_PRESERVE_START,
        help="需要保留的用户存储区起始地址。",
    )
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

    if args.version:
        image_path = resolve_version_image_path(workspace_root, args.version)
    elif args.image:
        image_path = resolve_image_path(workspace_root, args.image)
    else:
        image_path = resolve_interactive_image_path(workspace_root)

    script_path = build_dir / "flash_full.jlink"

    if not image_path.is_file():
        raise FileNotFoundError("找不到待烧录镜像: " + str(image_path))

    validate_erase_preserve_area(args.erase_start, args.erase_end, args.preserve_start)
    validate_image_preserve_area(image_path, args.address, args.preserve_start)
    lines = build_jlink_lines(
        image_path,
        args.address,
        args.device,
        args.speed,
        args.skip_verify,
        args.erase_start,
        args.erase_end,
    )

    print("Device : " + args.device)
    print("Image  : " + str(image_path))
    print("Address: " + args.address)
    print("Erase  : " + args.erase_start + " ~ " + args.erase_end)
    print("Preserve start: " + args.preserve_start)
    return run_jlink_script(script_path, lines, args.flash_exe, args.list_only)


if __name__ == "__main__":
    sys.exit(main())

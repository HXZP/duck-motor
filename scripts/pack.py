#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
from pathlib import Path


def parse_int(text: str) -> int:
    """
    @brief 解析十进制或十六进制整数。
    @param text 待解析的文本。
    @return 返回解析后的整数值。
    """
    return int(text, 0)


def read_binary(path: Path) -> bytes:
    """
    @brief 读取二进制文件内容。
    @param path 二进制文件路径。
    @return 返回文件字节数据。
    """
    if not path.is_file():
        raise FileNotFoundError(f"找不到输入文件: {path}")

    return path.read_bytes()


def build_image(
    boot_data: bytes,
    app_data: bytes,
    app_offset: int,
    userinfo_offset: int,
    boot_limit: int,
) -> bytes:
    """
    @brief 生成 boot 和 app 合并后的镜像数据。
    @param boot_data boot 二进制数据。
    @param app_data app 二进制数据。
    @param app_offset app 在镜像中的偏移地址，单位：字节。
    @param userinfo_offset userinfo 在镜像中的偏移地址，单位：字节。
    @param boot_limit boot 最大允许长度，单位：字节。
    @return 返回合并后的镜像数据。
    """
    if app_offset <= 0:
        raise ValueError("app 偏移必须大于 0")

    if userinfo_offset <= app_offset:
        raise ValueError("userinfo 偏移必须大于 app 偏移")

    if boot_limit > app_offset:
        raise ValueError("boot 最大长度不能超过 app 偏移")

    if len(boot_data) > boot_limit:
        raise ValueError(
            f"boot 超出限制: {len(boot_data)} 字节 > {boot_limit} 字节"
        )

    if len(boot_data) > app_offset:
        raise ValueError(
            f"boot 与 app 空间重叠: boot {len(boot_data)} 字节, app 偏移 {app_offset} 字节"
        )

    app_end = app_offset + len(app_data)

    if app_end > userinfo_offset:
        raise ValueError(
            f"app 超出 userinfo 前边界: 结束 0x{app_end:X}, userinfo 0x{userinfo_offset:X}"
        )

    image = bytearray([0xFF] * userinfo_offset)
    image[0:len(boot_data)] = boot_data
    image[app_offset:app_end] = app_data

    return bytes(image)


def write_binary(path: Path, data: bytes) -> None:
    """
    @brief 写入二进制文件内容。
    @param path 输出文件路径。
    @param data 待写入的字节数据。
    @return 无。
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main() -> int:
    """
    @brief 解析命令行参数并执行 boot/app 打包。
    @return 返回进程退出码，0 表示成功。
    """
    parser = argparse.ArgumentParser(description="打包 boot 和 app 为一个 bin 文件。")
    parser.add_argument("--boot", required=True, type=Path, help="boot.bin 路径")
    parser.add_argument("--app", required=True, type=Path, help="app.bin 路径")
    parser.add_argument("--output", required=True, type=Path, help="输出 bin 路径")
    parser.add_argument(
        "--app-offset",
        required=True,
        type=parse_int,
        help="app 偏移地址，单位：字节",
    )
    parser.add_argument(
        "--userinfo-offset",
        required=True,
        type=parse_int,
        help="userinfo 偏移地址，单位：字节",
    )
    parser.add_argument(
        "--boot-limit",
        required=True,
        type=parse_int,
        help="boot 最大长度，单位：字节",
    )
    args = parser.parse_args()

    boot_data = read_binary(args.boot)
    app_data = read_binary(args.app)
    image = build_image(
        boot_data,
        app_data,
        args.app_offset,
        args.userinfo_offset,
        args.boot_limit,
    )
    write_binary(args.output, image)

    print(f"pack 输出: {args.output}")
    print(f"boot 大小: {len(boot_data)} 字节")
    print(f"app 偏移: 0x{args.app_offset:X}")
    print(f"app 大小: {len(app_data)} 字节")
    print(f"合并大小: {len(image)} 字节")
    print(f"userinfo 偏移: 0x{args.userinfo_offset:X}, 已排除")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import struct
from pathlib import Path

OTA_INFO_MAGIC = 0x4F544149
OTA_INFO_VERSION = 1
OTA_INFO_FLAG_APP = 0


def crc16_ccitt(data: bytes | bytearray) -> int:
    """
    @brief 计算 CRC16-CCITT 校验值。
    @param data 输入数据。
    @return 返回 CRC16 校验值，单位：无。
    """
    crc = 0

    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if (crc & 0x8000) != 0:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


def crc32(data: bytes | bytearray) -> int:
    """
    @brief 计算 CRC32 校验值。
    @param data 输入数据。
    @return 返回 CRC32 校验值，单位：无。
    """
    crc = 0xFFFFFFFF

    for value in data:
        crc ^= value
        for _ in range(8):
            if (crc & 1) != 0:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
            crc &= 0xFFFFFFFF

    return crc ^ 0xFFFFFFFF


def build_ota_info_record(app_data: bytes) -> bytes:
    """
    @brief 生成 OTA 信息页记录。
    @param app_data App 二进制数据。
    @return 返回 OTA 信息记录字节数据。
    """
    app_crc16 = crc16_ccitt(app_data)
    record_without_crc = struct.pack(
        "<IIIIHH",
        OTA_INFO_MAGIC,
        OTA_INFO_VERSION,
        OTA_INFO_FLAG_APP,
        len(app_data),
        app_crc16,
        0,
    )
    record_crc32 = crc32(record_without_crc + struct.pack("<I", 0))
    return record_without_crc + struct.pack("<I", record_crc32)


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
    otainfo_offset: int,
    userinfo_offset: int,
    boot_limit: int,
) -> bytes:
    """
    @brief 生成 boot 和 app 合并后的镜像数据。
    @param boot_data boot 二进制数据。
    @param app_data app 二进制数据。
    @param app_offset app 在镜像中的偏移地址，单位：字节。
    @param otainfo_offset OTA 信息页在镜像中的偏移地址，单位：字节。
    @param userinfo_offset userinfo 在镜像中的偏移地址，单位：字节。
    @param boot_limit boot 最大允许长度，单位：字节。
    @return 返回合并后的镜像数据。
    """
    if app_offset <= 0:
        raise ValueError("app 偏移必须大于 0")

    if otainfo_offset <= app_offset:
        raise ValueError("OTA 信息页偏移必须大于 app 偏移")

    if userinfo_offset <= otainfo_offset:
        raise ValueError("userinfo 偏移必须大于 OTA 信息页偏移")

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

    if app_end > otainfo_offset:
        raise ValueError(
            f"app 超出 OTA 信息页前边界: 结束 0x{app_end:X}, otainfo 0x{otainfo_offset:X}"
        )

    image = bytearray([0xFF] * userinfo_offset)
    ota_info_record = build_ota_info_record(app_data)
    if (otainfo_offset + len(ota_info_record)) > userinfo_offset:
        raise ValueError("OTA 信息记录超出 OTA 信息页边界")

    image[0:len(boot_data)] = boot_data
    image[app_offset:app_end] = app_data
    image[otainfo_offset:otainfo_offset + len(ota_info_record)] = ota_info_record

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
        "--otainfo-offset",
        required=True,
        type=parse_int,
        help="OTA 信息页偏移地址，单位：字节",
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
        args.otainfo_offset,
        args.userinfo_offset,
        args.boot_limit,
    )
    write_binary(args.output, image)

    print(f"pack 输出: {args.output}")
    print(f"boot 大小: {len(boot_data)} 字节")
    print(f"app 偏移: 0x{args.app_offset:X}")
    print(f"app 大小: {len(app_data)} 字节")
    print(f"otainfo 偏移: 0x{args.otainfo_offset:X}, 已写入 App 校验信息")
    print(f"合并大小: {len(image)} 字节")
    print(f"userinfo 偏移: 0x{args.userinfo_offset:X}, 已排除")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

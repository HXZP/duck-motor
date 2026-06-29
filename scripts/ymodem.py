# -*- coding: utf-8 -*-
"""YMODEM 发包工具。"""

from __future__ import annotations


SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CA = 0x18
CRC_REQUEST = 0x43
PAD = 0x1A

PACKET_SIZE = 128
PACKET_1K_SIZE = 1024
BOOT_BUSINESS_HEADER_SIZE = 128


def crc16_ccitt(data: bytes | bytearray) -> int:
    """计算 CRC16-CCITT 校验值。

    Args:
        data: 输入数据。

    Returns:
        CRC16 校验值，单位：无。
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


def build_packet(sequence: int, payload: bytes | bytearray, packet_size: int) -> bytes:
    """构造 YMODEM 数据包。

    Args:
        sequence: 包序号，单位：包。
        payload: 包载荷。
        packet_size: 包载荷长度，单位：字节。

    Returns:
        YMODEM 包数据。
    """

    if packet_size not in (PACKET_SIZE, PACKET_1K_SIZE):
        raise ValueError("YMODEM 包长度只能是 128 或 1024 字节")

    if len(payload) > packet_size:
        raise ValueError("YMODEM 载荷长度超过包容量")

    start = SOH if packet_size == PACKET_SIZE else STX
    body = bytes(payload) + bytes([PAD]) * (packet_size - len(payload))
    crc = crc16_ccitt(body)
    sequence_byte = sequence & 0xFF
    return (
        bytes([start, sequence_byte, 0xFF - sequence_byte])
        + body
        + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    )


def build_header_packet(file_name: str, file_size: int) -> bytes:
    """构造 YMODEM 文件头包。

    Args:
        file_name: 文件名字符串。
        file_size: 文件长度，单位：字节。

    Returns:
        YMODEM 文件头包。
    """

    payload = bytearray(PACKET_SIZE)
    name_bytes = file_name.encode("ascii")
    size_bytes = str(file_size).encode("ascii")

    if len(name_bytes) + 1 + len(size_bytes) >= PACKET_SIZE:
        raise ValueError("YMODEM 文件名过长")

    payload[0 : len(name_bytes)] = name_bytes
    payload[len(name_bytes)] = 0
    size_start = len(name_bytes) + 1
    payload[size_start : size_start + len(size_bytes)] = size_bytes
    return build_packet(0, payload, PACKET_SIZE)


def build_empty_header_packet() -> bytes:
    """构造 YMODEM 结束空头包。

    Returns:
        YMODEM 结束空头包。
    """

    return build_packet(0, bytes(PACKET_SIZE), PACKET_SIZE)


def build_boot_ota_payload(app_image: bytes) -> bytes:
    """为当前 Boot OTA 格式添加 128 字节业务头。

    Args:
        app_image: App 固件镜像。

    Returns:
        YMODEM 文件内容。
    """

    header = bytearray(BOOT_BUSINESS_HEADER_SIZE)
    header[0:16] = b"MOTOR_DUCK_OTA\0\0"
    return bytes(header) + app_image

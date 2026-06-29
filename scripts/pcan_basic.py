# -*- coding: utf-8 -*-
"""PCANBasic 动态库的轻量封装。"""

from __future__ import annotations

import ctypes
import os
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PCAN_NONEBUS = 0x00
PCAN_USBBUS1 = 0x51

PCAN_ERROR_OK = 0x00000
PCAN_ERROR_QRCVEMPTY = 0x00020
PCAN_ERROR_INITIALIZE = 0x04000000

PCAN_ATTACHED_CHANNELS_COUNT = 0x2A
PCAN_ATTACHED_CHANNELS = 0x2B

PCAN_MESSAGE_STANDARD = 0x00

PCAN_CHANNEL_UNAVAILABLE = 0x00
PCAN_CHANNEL_AVAILABLE = 0x01
PCAN_CHANNEL_OCCUPIED = 0x02
PCAN_CHANNEL_PCANVIEW = 0x03

PCAN_BAUDRATES = {
    "1m": 0x0014,
    "1000k": 0x0014,
    "800k": 0x0016,
    "500k": 0x001C,
    "250k": 0x011C,
    "125k": 0x031C,
    "100k": 0x432F,
    "50k": 0x472F,
    "20k": 0x532F,
    "10k": 0x672F,
}


class PcanError(RuntimeError):
    """PCANBasic 调用失败异常。"""

    def __init__(self, code: int, message: str):
        """初始化 PCAN 异常。

        Args:
            code: PCANBasic 错误码，单位：无。
            message: 错误描述字符串。
        """

        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class CanFrame:
    """CAN 标准数据帧。"""

    std_id: int
    data: bytes


@dataclass(frozen=True)
class PcanChannelInfo:
    """PCAN 通道信息。"""

    handle: int
    device_type: int
    controller_number: int
    device_features: int
    device_name: str
    device_id: int
    condition: int


class _TPCANMsg(ctypes.Structure):
    """PCANBasic CAN 帧结构。"""

    _fields_ = [
        ("ID", wintypes.DWORD),
        ("MSGTYPE", ctypes.c_ubyte),
        ("LEN", ctypes.c_ubyte),
        ("DATA", ctypes.c_ubyte * 8),
    ]


class _TPCANTimestamp(ctypes.Structure):
    """PCANBasic 时间戳结构。"""

    _fields_ = [
        ("millis", wintypes.DWORD),
        ("millis_overflow", wintypes.WORD),
        ("micros", wintypes.WORD),
    ]


class _TPCANChannelInformation(ctypes.Structure):
    """PCANBasic 附加通道信息结构。"""

    _fields_ = [
        ("channel_handle", wintypes.WORD),
        ("device_type", wintypes.WORD),
        ("controller_number", ctypes.c_ubyte),
        ("device_features", wintypes.DWORD),
        ("device_name", ctypes.c_char * 32),
        ("device_id", wintypes.DWORD),
        ("channel_condition", wintypes.DWORD),
    ]


def format_data(data: bytes) -> str:
    """将字节数据格式化为十六进制字符串。

    Args:
        data: 字节数据。

    Returns:
        十六进制字符串。
    """

    return data.hex(" ").upper()


def parse_bitrate(value: str | int) -> int:
    """解析 CAN 波特率字符串。

    Args:
        value: 波特率字符串或 PCANBasic 波特率常量。

    Returns:
        PCANBasic 波特率常量，单位：无。
    """

    if isinstance(value, int):
        return value

    key = value.strip().lower()
    if key in PCAN_BAUDRATES:
        return PCAN_BAUDRATES[key]

    if key.startswith("0x"):
        return int(key, 16)

    raise ValueError(f"不支持的 PCAN 波特率: {value}")


def parse_channel(value: str | int | None) -> int | None:
    """解析 PCAN 通道参数。

    Args:
        value: 通道字符串，例如 auto、usb1、0x51。

    Returns:
        PCAN 通道句柄，auto 返回 None。
    """

    if value is None:
        return None

    if isinstance(value, int):
        return value

    text = value.strip().lower()
    if text == "auto":
        return None

    if text.startswith("0x"):
        return int(text, 16)

    if text.startswith("pcan_usbbus"):
        index_text = text.replace("pcan_usbbus", "", 1)
        return PCAN_USBBUS1 + int(index_text) - 1

    if text.startswith("usb"):
        index_text = text.replace("usb", "", 1)
        return PCAN_USBBUS1 + int(index_text) - 1

    return int(text, 0)


def channel_condition_text(condition: int) -> str:
    """将通道状态转换为可读文本。

    Args:
        condition: 通道状态码，单位：无。

    Returns:
        通道状态文本。
    """

    if condition == PCAN_CHANNEL_AVAILABLE:
        return "available"
    if condition == PCAN_CHANNEL_OCCUPIED:
        return "occupied"
    if condition == PCAN_CHANNEL_PCANVIEW:
        return "pcanview"
    if condition == PCAN_CHANNEL_UNAVAILABLE:
        return "unavailable"
    return f"unknown({condition})"


class PcanBasic:
    """PCANBasic 动态库访问对象。"""

    def __init__(self, dll_path: str | os.PathLike[str] | None = None):
        """初始化 PCANBasic 封装。

        Args:
            dll_path: PCANBasic.dll 路径，None 表示自动查找。
        """

        self.dll_path = self._resolve_dll_path(dll_path)
        self.dll = ctypes.WinDLL(str(self.dll_path))
        self._configure_symbols()

    def _resolve_dll_path(
        self,
        dll_path: str | os.PathLike[str] | None,
    ) -> Path | str:
        """查找 PCANBasic.dll。

        Args:
            dll_path: 用户指定的动态库路径。

        Returns:
            动态库路径或库名。
        """

        if dll_path is not None:
            return Path(dll_path)

        env_path = os.environ.get("PCANBASIC_DLL")
        if env_path:
            return Path(env_path)

        repo_root = Path(__file__).resolve().parents[1]
        local_dll = repo_root / "third_party" / "pcan" / "PCANBasic.dll"
        if local_dll.exists():
            return local_dll

        return "PCANBasic.dll"

    def _configure_symbols(self) -> None:
        """配置 PCANBasic 函数签名。"""

        self.dll.CAN_Initialize.argtypes = [
            wintypes.WORD,
            wintypes.WORD,
            ctypes.c_ubyte,
            wintypes.DWORD,
            wintypes.WORD,
        ]
        self.dll.CAN_Initialize.restype = wintypes.DWORD

        self.dll.CAN_Uninitialize.argtypes = [wintypes.WORD]
        self.dll.CAN_Uninitialize.restype = wintypes.DWORD

        self.dll.CAN_Read.argtypes = [
            wintypes.WORD,
            ctypes.POINTER(_TPCANMsg),
            ctypes.POINTER(_TPCANTimestamp),
        ]
        self.dll.CAN_Read.restype = wintypes.DWORD

        self.dll.CAN_Write.argtypes = [
            wintypes.WORD,
            ctypes.POINTER(_TPCANMsg),
        ]
        self.dll.CAN_Write.restype = wintypes.DWORD

        self.dll.CAN_GetValue.argtypes = [
            wintypes.WORD,
            ctypes.c_ubyte,
            ctypes.c_void_p,
            wintypes.DWORD,
        ]
        self.dll.CAN_GetValue.restype = wintypes.DWORD

        self.dll.CAN_GetErrorText.argtypes = [
            wintypes.DWORD,
            ctypes.c_ushort,
            ctypes.c_char_p,
        ]
        self.dll.CAN_GetErrorText.restype = wintypes.DWORD

    def error_text(self, code: int) -> str:
        """获取 PCANBasic 错误文本。

        Args:
            code: PCANBasic 错误码，单位：无。

        Returns:
            错误文本。
        """

        buffer = ctypes.create_string_buffer(256)
        self.dll.CAN_GetErrorText(code, 0, buffer)
        return buffer.value.decode("gbk", errors="replace")

    def check_status(self, code: int, action: str) -> None:
        """检查 PCANBasic 调用结果。

        Args:
            code: PCANBasic 错误码，单位：无。
            action: 当前动作描述。
        """

        if code == PCAN_ERROR_OK:
            return

        raise PcanError(code, f"{action}失败: 0x{code:08X} {self.error_text(code)}")

    def attached_channels(self) -> list[PcanChannelInfo]:
        """获取当前连接的 PCAN 通道。

        Returns:
            通道信息列表。
        """

        count = wintypes.DWORD(0)
        status = self.dll.CAN_GetValue(
            PCAN_NONEBUS,
            PCAN_ATTACHED_CHANNELS_COUNT,
            ctypes.byref(count),
            ctypes.sizeof(count),
        )
        self.check_status(status, "读取 PCAN 通道数量")

        if count.value == 0:
            return []

        array_type = _TPCANChannelInformation * count.value
        channels = array_type()
        status = self.dll.CAN_GetValue(
            PCAN_NONEBUS,
            PCAN_ATTACHED_CHANNELS,
            ctypes.byref(channels),
            ctypes.sizeof(channels),
        )
        self.check_status(status, "读取 PCAN 通道列表")

        result: list[PcanChannelInfo] = []
        for item in channels:
            raw_name = bytes(item.device_name).split(b"\x00", 1)[0]
            name = raw_name.decode("ascii", errors="replace")
            result.append(
                PcanChannelInfo(
                    handle=item.channel_handle,
                    device_type=item.device_type,
                    controller_number=item.controller_number,
                    device_features=item.device_features,
                    device_name=name,
                    device_id=item.device_id,
                    condition=item.channel_condition,
                )
            )

        return result

    def select_available_channel(self) -> int:
        """自动选择一个空闲 PCAN 通道。

        Returns:
            PCAN 通道句柄，单位：无。
        """

        channels = self.attached_channels()
        for channel in channels:
            if channel.condition == PCAN_CHANNEL_AVAILABLE:
                return channel.handle

        if not channels:
            raise RuntimeError("未发现 PCAN 设备")

        details = ", ".join(
            f"0x{item.handle:02X}:{channel_condition_text(item.condition)}"
            for item in channels
        )
        raise RuntimeError(f"未发现空闲 PCAN 通道，当前通道状态: {details}")

    def uninitialize(self, channel: int | None = None) -> None:
        """反初始化 PCAN 通道。

        Args:
            channel: PCAN 通道句柄，None 表示释放当前进程所有通道。
        """

        target = PCAN_NONEBUS if channel is None else channel
        status = self.dll.CAN_Uninitialize(target)
        if status not in (PCAN_ERROR_OK, PCAN_ERROR_INITIALIZE):
            self.check_status(status, "释放 PCAN 通道")

    def initialize(self, channel: int, bitrate: str | int) -> None:
        """初始化 PCAN 通道。

        Args:
            channel: PCAN 通道句柄，单位：无。
            bitrate: PCANBasic 波特率常量或字符串。
        """

        status = self.dll.CAN_Initialize(channel, parse_bitrate(bitrate), 0, 0, 0)
        self.check_status(status, "初始化 PCAN 通道")

    def read(self, channel: int) -> CanFrame | None:
        """读取一帧 CAN 数据。

        Args:
            channel: PCAN 通道句柄，单位：无。

        Returns:
            读到的 CAN 帧，队列为空返回 None。
        """

        message = _TPCANMsg()
        timestamp = _TPCANTimestamp()
        status = self.dll.CAN_Read(
            channel,
            ctypes.byref(message),
            ctypes.byref(timestamp),
        )
        if status == PCAN_ERROR_QRCVEMPTY:
            return None

        self.check_status(status, "读取 PCAN 帧")
        return CanFrame(
            std_id=message.ID,
            data=bytes(message.DATA[: message.LEN]),
        )

    def write(self, channel: int, frame: CanFrame) -> None:
        """写入一帧 CAN 数据。

        Args:
            channel: PCAN 通道句柄，单位：无。
            frame: CAN 标准数据帧。
        """

        if len(frame.data) > 8:
            raise ValueError("CAN 数据长度不能超过 8 字节")

        message = _TPCANMsg()
        message.ID = frame.std_id
        message.MSGTYPE = PCAN_MESSAGE_STANDARD
        message.LEN = len(frame.data)
        for index, value in enumerate(frame.data):
            message.DATA[index] = value

        status = self.dll.CAN_Write(channel, ctypes.byref(message))
        self.check_status(status, "写入 PCAN 帧")

    def drain(self, channel: int, max_frames: int = 4096) -> int:
        """读取并丢弃当前接收队列中的所有帧。

        Args:
            channel: PCAN 通道句柄，单位：无。
            max_frames: 最大清理帧数，单位：帧。

        Returns:
            实际清理的帧数，单位：帧。
        """

        drained = 0
        for _ in range(max_frames):
            frame = self.read(channel)
            if frame is None:
                break
            drained += 1

        return drained


def format_channels(channels: Iterable[PcanChannelInfo]) -> Iterable[str]:
    """将通道信息格式化为文本行。

    Args:
        channels: PCAN 通道信息列表。

    Returns:
        通道描述文本。
    """

    for channel in channels:
        yield (
            f"handle=0x{channel.handle:02X} "
            f"device={channel.device_name} "
            f"controller={channel.controller_number} "
            f"condition={channel_condition_text(channel.condition)}"
        )

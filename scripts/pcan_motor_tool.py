#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@brief Duck Mid PCAN 电机调试上位机。
@note 该工具只依赖 Python 标准库和 PEAK PCAN-Basic 驱动。
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import math
import queue
import struct
import threading
import time
import tkinter as tk
from collections import deque
from dataclasses import dataclass
from tkinter import filedialog, messagebox
from tkinter import ttk


CHART_HISTORY_SECONDS = 10.0
CHART_MIN_WINDOW_SECONDS = 0.2
CHART_MAX_WINDOW_SECONDS = 120.0
CHART_REFRESH_MS = 100
CHART_MAX_DRAW_POINTS = 1600
EVENT_QUEUE_MAX = 5000
EVENT_PROCESS_LIMIT = 300
LOG_MAX_LINES = 300
MOTOR_REPORT_PERIOD_MIN_MS = 2
MOTOR_REPORT_PERIOD_MAX_MS = 60000

PCAN_ERROR_OK = 0x00000
PCAN_ERROR_QRCVEMPTY = 0x00020
PCAN_MESSAGE_STANDARD = 0x00

MOTOR_HOST_CMD_BASE = 0x100
MOTOR_ACK_BASE = 0x180
MOTOR_REPORT_BASE = 0x200
MOTOR_PID_DEBUG_STATE_BASE = 0x300
MOTOR_PID_DEBUG_ERROR_BASE = 0x320
MOTOR_PID_DEBUG_INTEGRAL_BASE = 0x340
MOTOR_PID_DEBUG_OUTPUT_BASE = 0x360
MOTOR_FOC_TRACE_PART_BASES = (0x380, 0x3A0, 0x3C0)
MOTOR_MANAGE_NODE_ID = 0x7E
MOTOR_MANAGE_COMMAND_ID = 0x17E
MOTOR_MANAGE_ACK_ID = 0x1FE
MOTOR_MANAGE_REPORT_ID = 0x27E

MOTOR_CMD_SET_RUN_MODE = 0x01
MOTOR_CMD_SET_CURRENT_TARGET = 0x02
MOTOR_CMD_SET_SPEED_TARGET = 0x03
MOTOR_CMD_SET_POSITION_TARGET = 0x04
MOTOR_CMD_SET_SPEED_PID_PARAM = 0x10
MOTOR_CMD_READ_SPEED_PID_PARAM = 0x11
MOTOR_CMD_SET_POSITION_PID_PARAM = 0x12
MOTOR_CMD_READ_POSITION_PID_PARAM = 0x13
MOTOR_CMD_READ_NODE_ID = 0x21
MOTOR_CMD_READ_APP_VERSION = 0x22
MOTOR_CMD_SET_REPORT_CONFIG = 0x23
MOTOR_CMD_READ_REPORT_CONFIG = 0x24
MOTOR_CMD_SET_FOC_CONFIG = 0x26
MOTOR_CMD_READ_FOC_CONFIG = 0x27
MOTOR_CMD_ZERO_CALIBRATION = 0x30
MOTOR_CMD_READ_ZERO = 0x31
MOTOR_CMD_FOC_TRACE = 0x41
MOTOR_CMD_MANAGE_REPORT = 0x60
MOTOR_CMD_MANAGE_IDENTIFY = 0x61
MOTOR_CMD_MANAGE_SET_CAN_ID = 0x62

MOTOR_FOC_PARAM_POLE_PAIRS = 0x00
MOTOR_FOC_PARAM_MASTER_VOLTAGE_MV = 0x01
MOTOR_FOC_PARAM_CONTROL_HZ = 0x02
MOTOR_FOC_PARAM_SENSOR_HZ = 0x03
MOTOR_FOC_PARAM_PHASE_MAP = 0x04
MOTOR_FOC_PARAM_AS5600_BOOT_CONF = 0x05
MOTOR_FOC_PARAM_AS5600_ACTIVE_CONF = 0x06
MOTOR_FOC_PARAM_AS5600_READ_LAST_US = 0x07
MOTOR_FOC_PARAM_AS5600_READ_MIN_US = 0x08
MOTOR_FOC_PARAM_AS5600_READ_MAX_US = 0x09
MOTOR_FOC_PARAM_AS5600_READ_AVERAGE_US = 0x0A

MOTOR_PID_PARAM_P = 0x00
MOTOR_PID_PARAM_I = 0x01
MOTOR_PID_PARAM_D = 0x02
MOTOR_PID_PARAM_I_ACC_MAX = 0x03
MOTOR_PID_PARAM_OUT_MAX = 0x04

MOTOR_PID_PARAMETERS = (
    ("P", MOTOR_PID_PARAM_P),
    ("I", MOTOR_PID_PARAM_I),
    ("D", MOTOR_PID_PARAM_D),
    ("I_ACC_MAX", MOTOR_PID_PARAM_I_ACC_MAX),
    ("OUT_MAX", MOTOR_PID_PARAM_OUT_MAX),
)

MOTOR_STATUS_TEXT = {
    0x00: "OK",
    0x01: "INVALID_CMD",
    0x02: "INVALID_PARAM",
    0x03: "INVALID_MODE",
    0x04: "CAN_ERROR",
}

MOTOR_COMMAND_TEXT = {
    MOTOR_CMD_SET_RUN_MODE: "set_run_mode",
    MOTOR_CMD_SET_CURRENT_TARGET: "set_current",
    MOTOR_CMD_SET_SPEED_TARGET: "set_speed",
    MOTOR_CMD_SET_POSITION_TARGET: "set_position",
    MOTOR_CMD_SET_SPEED_PID_PARAM: "set_speed_pid",
    MOTOR_CMD_READ_SPEED_PID_PARAM: "read_speed_pid",
    MOTOR_CMD_SET_POSITION_PID_PARAM: "set_position_pid",
    MOTOR_CMD_READ_POSITION_PID_PARAM: "read_position_pid",
    MOTOR_CMD_READ_NODE_ID: "read_node",
    MOTOR_CMD_READ_APP_VERSION: "read_version",
    MOTOR_CMD_SET_REPORT_CONFIG: "set_report",
    MOTOR_CMD_READ_REPORT_CONFIG: "read_report",
    MOTOR_CMD_SET_FOC_CONFIG: "set_foc",
    MOTOR_CMD_READ_FOC_CONFIG: "read_foc",
    MOTOR_CMD_ZERO_CALIBRATION: "zero_calibration",
    MOTOR_CMD_READ_ZERO: "read_zero",
    MOTOR_CMD_FOC_TRACE: "foc_trace",
    MOTOR_CMD_MANAGE_REPORT: "manage_report",
    MOTOR_CMD_MANAGE_IDENTIFY: "manage_identify",
    MOTOR_CMD_MANAGE_SET_CAN_ID: "manage_set_id",
}

MOTOR_MODES = {
    "current": 0,
    "speed": 1,
    "position": 2,
}

MOTOR_NODE_IDS = tuple(range(0x01, 0x09))

PCAN_CHANNELS = {
    "PCAN_USBBUS1": 0x51,
    "PCAN_USBBUS2": 0x52,
    "PCAN_USBBUS3": 0x53,
    "PCAN_USBBUS4": 0x54,
    "PCAN_USBBUS5": 0x55,
    "PCAN_USBBUS6": 0x56,
    "PCAN_USBBUS7": 0x57,
    "PCAN_USBBUS8": 0x58,
    "PCAN_USBBUS9": 0x509,
    "PCAN_USBBUS10": 0x50A,
    "PCAN_USBBUS11": 0x50B,
    "PCAN_USBBUS12": 0x50C,
    "PCAN_USBBUS13": 0x50D,
    "PCAN_USBBUS14": 0x50E,
    "PCAN_USBBUS15": 0x50F,
    "PCAN_USBBUS16": 0x510,
}

PCAN_DEFAULT_CHANNEL_NAME = "PCAN_USBBUS1"

PCAN_CHANNEL_LABELS = [
    f"{channel_name} (0x{channel_value:X})"
    for channel_name, channel_value in PCAN_CHANNELS.items()
]

PCAN_BITRATES = {
    "1M": 0x0014,
    "800K": 0x0016,
    "500K": 0x001C,
    "250K": 0x011C,
    "125K": 0x031C,
}


class TPCANMsg(ctypes.Structure):
    """@brief PCAN-Basic CAN 报文结构。"""

    _fields_ = [
        ("ID", ctypes.c_uint32),
        ("MSGTYPE", ctypes.c_ubyte),
        ("LEN", ctypes.c_ubyte),
        ("DATA", ctypes.c_ubyte * 8),
    ]


class TPCANTimestamp(ctypes.Structure):
    """@brief PCAN-Basic 时间戳结构。"""

    _fields_ = [
        ("millis", ctypes.c_uint32),
        ("millis_overflow", ctypes.c_uint16),
        ("micros", ctypes.c_uint16),
    ]


@dataclass
class MotorState:
    """@brief 单个电机的接收和命令状态。"""

    name: str
    node_id: int
    position_mrad: int | None = None
    speed_mrad_s: int | None = None
    last_report_ms: int | None = None
    report_count: int = 0
    ack_count: int = 0
    tx_count: int = 0
    last_ack_cmd: int | None = None
    last_ack_status: int | None = None
    last_ack_ms: int | None = None
    version: str = "--"
    reported_node_id: int | None = None
    report_enabled: bool | None = None
    report_period_ms: int | None = None
    zero_mrad: int | None = None
    pole_pairs: int | None = None
    phase_map: int | None = None


@dataclass
class MotorReportEvent:
    """@brief 电机主动上报事件。"""

    node_id: int
    position_mrad: int
    speed_mrad_s: int
    timestamp_ms: int


@dataclass
class PidDebugEvent:
    """@brief 速度环 PID 诊断帧事件。"""

    node_id: int
    frame_type: str
    can_id: int
    value_a: float
    value_b: float | int
    raw_data: bytes
    timestamp_ms: int


@dataclass
class FocTracePartEvent:
    """@brief FOC 高速记录分片事件。"""

    node_id: int
    sample_index: int
    part_index: int
    payload: bytes


@dataclass
class MotorAckEvent:
    """@brief 电机应答事件。"""

    node_id: int
    command: int
    status: int
    payload: bytes
    timestamp_ms: int


@dataclass
class ManageReportEvent:
    """@brief 未配置电机管理上报事件。"""

    configured: int
    node_id: int
    uid32: int
    timestamp_ms: int


@dataclass
class ManageAckEvent:
    """@brief 管理命令应答事件。"""

    command: int
    status: int
    payload: bytes
    timestamp_ms: int


@dataclass
class ErrorEvent:
    """@brief 上位机错误事件。"""

    message: str


def now_monotonic_ms() -> int:
    """
    @brief 获取当前单调时间。
    @return int 当前单调时间，单位毫秒。
    """
    return int(time.monotonic() * 1000)


def int32_from_le(data: bytes) -> int:
    """
    @brief 从小端字节解析 int32。
    @param data 输入字节。
    @return int 解析出的有符号 32 位整数。
    """
    return int.from_bytes(data, byteorder="little", signed=True)


def int32_to_le(value: int) -> bytes:
    """
    @brief 将整数编码为小端 int32。
    @param value 输入整数。
    @return bytes 小端 int32 字节。
    """
    return int(value).to_bytes(4, byteorder="little", signed=True)


def float32_from_le(data: bytes) -> float:
    """
    @brief 从 IEEE 754 小端字节解析 float32。
    @param data 输入字节。
    @return float 解析出的单精度浮点数。
    """
    return struct.unpack("<f", data)[0]


def float32_to_le(value: float) -> bytes:
    """
    @brief 将浮点数编码为 IEEE 754 小端 float32。
    @param value 输入浮点数。
    @return bytes 小端 float32 字节。
    """
    return struct.pack("<f", value)


def uint16_to_le(value: int) -> bytes:
    """
    @brief 将整数编码为小端 uint16。
    @param value 输入整数。
    @return bytes 小端 uint16 字节。
    """
    return int(value).to_bytes(2, byteorder="little", signed=False)


def uint16_from_le(data: bytes) -> int:
    """
    @brief 从小端字节解析 uint16。
    @param data 输入字节。
    @return int 解析出的无符号 16 位整数。
    """
    return int.from_bytes(data, byteorder="little", signed=False)


def uint32_from_le(data: bytes) -> int:
    """
    @brief 从小端字节解析 uint32。
    @param data 输入字节。
    @return int 解析出的无符号 32 位整数。
    """
    return int.from_bytes(data, byteorder="little", signed=False)


def uint32_to_le(value: int) -> bytes:
    """
    @brief 将整数编码为小端 uint32。
    @param value 输入整数。
    @return bytes 小端 uint32 字节。
    """
    return int(value).to_bytes(4, byteorder="little", signed=False)


def mrad_to_degree(value_mrad: int | None) -> str:
    """
    @brief 将 mrad 格式化为角度字符串。
    @param value_mrad 输入角度，单位 mrad。
    @return str 角度字符串。
    """
    if value_mrad is None:
        return "--"

    return f"{(value_mrad / 1000.0) * 180.0 / math.pi:.2f}"


def format_optional_int(value: int | None) -> str:
    """
    @brief 格式化可选整数。
    @param value 输入整数。
    @return str 格式化结果。
    """
    if value is None:
        return "--"

    return str(value)


def parse_int_auto(text: str) -> int:
    """
    @brief 解析十进制或 0x 前缀十六进制整数。
    @param text 输入文本。
    @return int 解析结果。
    """
    return int(text.strip(), 0)


def channel_label_from_value(channel_value: int) -> str:
    """
    @brief 根据 PCAN 通道值生成下拉框显示文本。
    @param channel_value PCAN 通道值。
    @return str 下拉框显示文本。
    """
    for channel_name, item_value in PCAN_CHANNELS.items():
        if item_value == channel_value:
            return f"{channel_name} (0x{item_value:X})"

    return f"0x{channel_value:X}"


def channel_value_from_label(channel_label: str) -> int:
    """
    @brief 从下拉框显示文本解析 PCAN 通道值。
    @param channel_label 下拉框显示文本。
    @return int PCAN 通道值。
    """
    stripped_text = channel_label.strip()

    for channel_name, channel_value in PCAN_CHANNELS.items():
        if stripped_text.startswith(channel_name):
            return channel_value

    return parse_int_auto(stripped_text)


def node_label_from_id(node_id: int) -> str:
    """
    @brief 根据节点 ID 生成节点下拉框文本。
    @param node_id 节点 ID。
    @return str 下拉框文本。
    """
    return f"0x{node_id:02X}"


def motor_name_from_node_id(node_id: int) -> str:
    """
    @brief 根据节点 ID 生成电机显示名称。
    @param node_id 节点 ID。
    @return str 电机显示名称。
    """
    return f"node 0x{node_id:02X}"


def node_id_from_label(node_label: str) -> int:
    """
    @brief 从节点下拉框文本解析节点 ID。
    @param node_label 节点下拉框文本。
    @return int 节点 ID。
    """
    stripped_text = node_label.strip()

    if "(0x" in stripped_text and stripped_text.endswith(")"):
        start = stripped_text.rfind("(0x") + 1
        return parse_int_auto(stripped_text[start:-1])

    return parse_int_auto(stripped_text)


def command_text(command: int | None) -> str:
    """
    @brief 获取命令码显示文本。
    @param command 命令码。
    @return str 命令码显示文本。
    """
    if command is None:
        return "--"

    name = MOTOR_COMMAND_TEXT.get(command, "cmd")

    return f"{name}(0x{command:02X})"


def status_text(status: int | None) -> str:
    """
    @brief 获取状态码显示文本。
    @param status 状态码。
    @return str 状态码显示文本。
    """
    if status is None:
        return "--"

    return MOTOR_STATUS_TEXT.get(status, f"0x{status:02X}")


def report_config_text(enabled: bool | None, period_ms: int | None) -> str:
    """
    @brief 格式化主动上报配置。
    @param enabled 主动上报是否开启。
    @param period_ms 主动上报周期，单位毫秒。
    @return str 主动上报配置文本。
    """
    if enabled is None or period_ms is None:
        return "--"

    if period_ms <= 0:
        return f"{1 if enabled else 0}/--"

    return f"{1 if enabled else 0}/{period_ms}ms({1000.0 / period_ms:.1f}Hz)"


def foc_config_text(pole_pairs: int | None, phase_map: int | None) -> str:
    """
    @brief 格式化 FOC 基础配置摘要。
    @param pole_pairs 极对数。
    @param phase_map 相序映射编号。
    @return str FOC 配置摘要。
    """
    if pole_pairs is None and phase_map is None:
        return "--"

    pole_text = "--" if pole_pairs is None else str(pole_pairs)
    phase_text = "--" if phase_map is None else str(phase_map)
    return f"pp={pole_text}, pm={phase_text}"


class PcanBasic:
    """@brief PCAN-Basic DLL 轻量封装。"""

    def __init__(self) -> None:
        """
        @brief 加载 PCAN-Basic DLL 并配置函数原型。
        @return None
        """
        self.dll = ctypes.WinDLL("PCANBasic.dll")
        self._configure_prototypes()

    def _configure_prototypes(self) -> None:
        """
        @brief 配置 PCAN-Basic DLL 函数原型。
        @return None
        """
        self.dll.CAN_Initialize.argtypes = [
            ctypes.c_ushort,
            ctypes.c_ushort,
            ctypes.c_ubyte,
            ctypes.c_uint32,
            ctypes.c_ushort,
        ]
        self.dll.CAN_Initialize.restype = ctypes.c_uint32

        self.dll.CAN_Uninitialize.argtypes = [ctypes.c_ushort]
        self.dll.CAN_Uninitialize.restype = ctypes.c_uint32

        self.dll.CAN_Read.argtypes = [
            ctypes.c_ushort,
            ctypes.POINTER(TPCANMsg),
            ctypes.POINTER(TPCANTimestamp),
        ]
        self.dll.CAN_Read.restype = ctypes.c_uint32

        self.dll.CAN_Write.argtypes = [
            ctypes.c_ushort,
            ctypes.POINTER(TPCANMsg),
        ]
        self.dll.CAN_Write.restype = ctypes.c_uint32

        self.dll.CAN_GetErrorText.argtypes = [
            ctypes.c_uint32,
            ctypes.c_ushort,
            ctypes.c_char_p,
        ]
        self.dll.CAN_GetErrorText.restype = ctypes.c_uint32

    def initialize(self, channel: int, bitrate: int) -> int:
        """
        @brief 初始化 PCAN 通道。
        @param channel PCAN 通道。
        @param bitrate PCAN 波特率枚举值。
        @return int PCAN-Basic 状态码。
        """
        return int(self.dll.CAN_Initialize(channel, bitrate, 0, 0, 0))

    def probe_channel(self, channel: int, bitrate: int) -> bool:
        """
        @brief 探测 PCAN 通道是否可以初始化。
        @param channel PCAN 通道。
        @param bitrate PCAN 波特率枚举值。
        @return bool true 表示可用，false 表示不可用。
        """
        result = self.initialize(channel, bitrate)

        if result != PCAN_ERROR_OK:
            return False

        self.uninitialize(channel)
        return True

    def uninitialize(self, channel: int) -> int:
        """
        @brief 反初始化 PCAN 通道。
        @param channel PCAN 通道。
        @return int PCAN-Basic 状态码。
        """
        return int(self.dll.CAN_Uninitialize(channel))

    def read(self, channel: int) -> tuple[int, TPCANMsg, TPCANTimestamp]:
        """
        @brief 从 PCAN 通道读取一帧。
        @param channel PCAN 通道。
        @return tuple[int, TPCANMsg, TPCANTimestamp] 状态码、报文和时间戳。
        """
        message = TPCANMsg()
        timestamp = TPCANTimestamp()
        result = self.dll.CAN_Read(channel,
                                   ctypes.byref(message),
                                   ctypes.byref(timestamp))

        return int(result), message, timestamp

    def write(self, channel: int, frame_id: int, payload: bytes) -> int:
        """
        @brief 向 PCAN 通道发送标准帧。
        @param channel PCAN 通道。
        @param frame_id 标准帧 ID。
        @param payload 负载字节。
        @return int PCAN-Basic 状态码。
        """
        message = TPCANMsg()
        message.ID = frame_id
        message.MSGTYPE = PCAN_MESSAGE_STANDARD
        message.LEN = len(payload)

        for index, byte_value in enumerate(payload):
            message.DATA[index] = byte_value

        return int(self.dll.CAN_Write(channel, ctypes.byref(message)))

    def get_error_text(self, error_code: int) -> str:
        """
        @brief 获取 PCAN-Basic 错误文本。
        @param error_code PCAN-Basic 状态码。
        @return str 错误文本。
        """
        buffer = ctypes.create_string_buffer(256)
        result = self.dll.CAN_GetErrorText(error_code, 0x09, buffer)

        if int(result) != PCAN_ERROR_OK:
            return f"PCAN error 0x{error_code:08X}"

        return buffer.value.decode(errors="replace")


class PcanClient:
    """@brief PCAN 电机协议客户端。"""

    def __init__(self, event_queue: queue.Queue[object]) -> None:
        """
        @brief 初始化 PCAN 客户端。
        @param event_queue UI 事件队列。
        @return None
        """
        self.event_queue = event_queue
        self.basic: PcanBasic | None = None
        self.channel = 0
        self.running = threading.Event()
        self.thread: threading.Thread | None = None
        self.lock = threading.Lock()

    def connect(self, channel: int, bitrate: int) -> None:
        """
        @brief 连接 PCAN 通道并启动接收线程。
        @param channel PCAN 通道。
        @param bitrate PCAN 波特率枚举值。
        @return None
        """
        if self.running.is_set():
            return

        self.basic = PcanBasic()
        result = self.basic.initialize(channel, bitrate)

        if result != PCAN_ERROR_OK:
            raise RuntimeError(self.basic.get_error_text(result))

        self.channel = channel
        self.running.set()
        self.thread = threading.Thread(target=self._read_worker,
                                       name="pcan-motor-read",
                                       daemon=True)
        self.thread.start()

    def scan_channels(self, bitrate: int) -> list[tuple[str, int]]:
        """
        @brief 扫描当前可初始化的 PCAN 通道。
        @param bitrate PCAN 波特率枚举值。
        @return list[tuple[str, int]] 可用通道名称和值列表。
        """
        available_channels: list[tuple[str, int]] = []
        basic = PcanBasic()

        for channel_name, channel_value in PCAN_CHANNELS.items():
            if basic.probe_channel(channel_value, bitrate):
                available_channels.append((channel_name, channel_value))

        return available_channels

    def disconnect(self) -> None:
        """
        @brief 断开 PCAN 通道并停止接收线程。
        @return None
        """
        self.running.clear()

        if self.thread is not None:
            self.thread.join(timeout=1.0)
            self.thread = None

        if self.basic is not None and self.channel != 0:
            self.basic.uninitialize(self.channel)

        self.channel = 0
        self.basic = None

    def send_frame(self, frame_id: int, payload: bytes) -> None:
        """
        @brief 发送标准 CAN 帧。
        @param frame_id 标准帧 ID。
        @param payload 负载字节。
        @return None
        """
        if self.basic is None or self.channel == 0:
            raise RuntimeError("PCAN 未连接")

        with self.lock:
            result = self.basic.write(self.channel, frame_id, payload)

        if result != PCAN_ERROR_OK:
            raise RuntimeError(self.basic.get_error_text(result))

    def send_motor_command(self, node_id: int, payload: bytes) -> None:
        """
        @brief 向指定节点发送电机命令。
        @param node_id 目标节点 ID。
        @param payload 8 字节电机协议负载。
        @return None
        """
        if len(payload) != 8:
            raise ValueError("电机命令负载必须为 8 字节")

        self.send_frame(MOTOR_HOST_CMD_BASE + node_id, payload)

    def send_manage_command(self, payload: bytes) -> None:
        """
        @brief 发送管理命令帧。
        @param payload 8 字节管理命令负载。
        @return None
        """
        if len(payload) != 8:
            raise ValueError("管理命令负载必须为 8 字节")

        self.send_frame(MOTOR_MANAGE_COMMAND_ID, payload)

    def send_manage_identify(self, uid32: int, blink_hz: int, duration_s: int) -> None:
        """
        @brief 发送未配置电机识别命令。
        @param uid32 目标短芯片 ID。
        @param blink_hz 闪烁频率，单位 Hz。
        @param duration_s 持续时间，单位秒。
        @return None
        """
        payload = bytes([
            MOTOR_CMD_MANAGE_IDENTIFY,
            blink_hz & 0xFF,
            duration_s & 0xFF,
        ])
        payload += uint32_to_le(uid32)
        payload += bytes([0])
        self.send_manage_command(payload)

    def send_manage_set_can_id(self, uid32: int, node_id: int) -> None:
        """
        @brief 发送未配置电机业务节点 ID 配置命令。
        @param uid32 目标短芯片 ID。
        @param node_id 新业务节点 ID。
        @return None
        """
        payload = bytes([
            MOTOR_CMD_MANAGE_SET_CAN_ID,
            node_id & 0xFF,
            0,
        ])
        payload += uint32_to_le(uid32)
        payload += bytes([0])
        self.send_manage_command(payload)

    def send_run_mode(self, node_id: int, enable: bool, mode_id: int) -> None:
        """
        @brief 发送运行模式和使能命令。
        @param node_id 目标节点 ID。
        @param enable true 表示使能，false 表示停机。
        @param mode_id 控制模式编号。
        @return None
        """
        payload = bytes([
            MOTOR_CMD_SET_RUN_MODE,
            1 if enable else 0,
            mode_id,
            0,
            0,
            0,
            0,
            0,
        ])
        self.send_motor_command(node_id, payload)

    def send_current_target(self, node_id: int, target: int) -> None:
        """
        @brief 发送电流目标命令。
        @param node_id 目标节点 ID。
        @param target 电流目标控制量。
        @return None
        """
        payload = bytes([MOTOR_CMD_SET_CURRENT_TARGET])
        payload += int32_to_le(target)
        payload += bytes([0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_speed_target(self, node_id: int, target_mrad_s: int) -> None:
        """
        @brief 发送速度目标命令。
        @param node_id 目标节点 ID。
        @param target_mrad_s 速度目标，单位 mrad/s。
        @return None
        """
        payload = bytes([MOTOR_CMD_SET_SPEED_TARGET])
        payload += int32_to_le(target_mrad_s)
        payload += bytes([0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_position_target(self, node_id: int, target_mrad: int) -> None:
        """
        @brief 发送位置目标命令。
        @param node_id 目标节点 ID。
        @param target_mrad 位置目标，单位 mrad。
        @return None
        """
        payload = bytes([MOTOR_CMD_SET_POSITION_TARGET])
        payload += int32_to_le(target_mrad)
        payload += bytes([0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_pid_param(self,
                       node_id: int,
                       command: int,
                       param_id: int,
                       value: float) -> None:
        """
        @brief 发送位置环或速度环参数写入命令。
        @param node_id 目标节点 ID。
        @param command PID 参数写入命令码。
        @param param_id PID 参数编号。
        @param value PID 单精度浮点参数值。
        @return None
        """
        payload = bytes([command, param_id & 0xFF])
        payload += float32_to_le(value)
        payload += bytes([0, 0])
        self.send_motor_command(node_id, payload)

    def send_read_pid_param(self, node_id: int, command: int, param_id: int) -> None:
        """
        @brief 发送位置环或速度环参数读取命令。
        @param node_id 目标节点 ID。
        @param command PID 参数读取命令码。
        @param param_id PID 参数编号。
        @return None
        """
        payload = bytes([command, param_id & 0xFF])
        payload += bytes([0, 0, 0, 0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_report_config(self, node_id: int, enable: bool, period_ms: int) -> None:
        """
        @brief 发送主动上报配置命令。
        @param node_id 目标节点 ID。
        @param enable true 表示开启主动上报，false 表示关闭。
        @param period_ms 上报周期，单位毫秒。
        @return None
        """
        payload = bytes([
            MOTOR_CMD_SET_REPORT_CONFIG,
            1 if enable else 0,
        ])
        payload += uint16_to_le(period_ms)
        payload += bytes([0, 0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_foc_config(self, node_id: int, param_id: int, value: int) -> None:
        """
        @brief 发送 FOC 基础配置写入命令。
        @param node_id 目标节点 ID。
        @param param_id FOC 参数编号。
        @param value FOC 参数值。
        @return None
        """
        payload = bytes([MOTOR_CMD_SET_FOC_CONFIG, param_id & 0xFF])
        payload += uint32_to_le(value)
        payload += bytes([0, 0])
        self.send_motor_command(node_id, payload)

    def send_read_foc_config(self, node_id: int, param_id: int) -> None:
        """
        @brief 发送 FOC 基础配置读取命令。
        @param node_id 目标节点 ID。
        @param param_id FOC 参数编号。
        @return None
        """
        payload = bytes([MOTOR_CMD_READ_FOC_CONFIG, param_id & 0xFF])
        payload += bytes([0, 0, 0, 0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_simple_command(self, node_id: int, command: int) -> None:
        """
        @brief 发送无参数电机命令。
        @param node_id 目标节点 ID。
        @param command 命令码。
        @return None
        """
        payload = bytes([command, 0, 0, 0, 0, 0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_foc_trace_command(self, node_id: int, action: int) -> None:
        """
        @brief 发送 FOC 高速记录控制命令。
        @param node_id 目标节点 ID。
        @param action 操作编号，0 表示开始，1 表示停止，2 表示上传。
        @return None
        """
        payload = bytes([MOTOR_CMD_FOC_TRACE, action & 0xFF])
        payload += bytes([0, 0, 0, 0, 0, 0])
        self.send_motor_command(node_id, payload)

    def send_stop_output(self, node_id: int) -> None:
        """
        @brief 停止指定电机输出。
        @param node_id 目标节点 ID。
        @return None
        """
        self.send_run_mode(node_id, False, MOTOR_MODES["current"])

    def _read_worker(self) -> None:
        """
        @brief PCAN 接收线程入口。
        @return None
        """
        assert self.basic is not None

        while self.running.is_set():
            read_any = False

            while self.running.is_set():
                result, message, _timestamp = self.basic.read(self.channel)

                if result == PCAN_ERROR_QRCVEMPTY:
                    break

                if result != PCAN_ERROR_OK:
                    error_text = self.basic.get_error_text(result)
                    self.event_queue.put(ErrorEvent(error_text))
                    break

                read_any = True
                self._handle_message(message)

            if read_any:
                time.sleep(0.001)
            else:
                time.sleep(0.005)

    def _handle_message(self, message: TPCANMsg) -> None:
        """
        @brief 处理一帧 PCAN 报文。
        @param message PCAN 报文。
        @return None
        """
        data = bytes(message.DATA[:message.LEN])

        if message.ID == MOTOR_MANAGE_REPORT_ID:
            if len(data) >= 7 and data[0] == MOTOR_CMD_MANAGE_REPORT:
                event = ManageReportEvent(configured=data[1],
                                          node_id=data[2],
                                          uid32=uint32_from_le(data[3:7]),
                                          timestamp_ms=now_monotonic_ms())
                self.event_queue.put(event)

            return

        if message.ID == MOTOR_MANAGE_ACK_ID:
            if len(data) >= 2:
                event = ManageAckEvent(command=data[0],
                                       status=data[1],
                                       payload=data,
                                       timestamp_ms=now_monotonic_ms())
                self.event_queue.put(event)

            return

        if message.ID in range(MOTOR_REPORT_BASE + 1, MOTOR_REPORT_BASE + 0x80):
            if len(data) >= 8:
                event = MotorReportEvent(node_id=message.ID - MOTOR_REPORT_BASE,
                                         position_mrad=int32_from_le(data[0:4]),
                                         speed_mrad_s=int32_from_le(data[4:8]),
                                         timestamp_ms=now_monotonic_ms())
                self._put_motor_report(event)

            return

        debug_ranges = (
            (MOTOR_PID_DEBUG_STATE_BASE, "state"),
            (MOTOR_PID_DEBUG_ERROR_BASE, "error"),
            (MOTOR_PID_DEBUG_INTEGRAL_BASE, "integral"),
            (MOTOR_PID_DEBUG_OUTPUT_BASE, "output"),
        )

        for base_id, frame_type in debug_ranges:
            if message.ID in range(base_id + 1, base_id + 0x20):
                if len(data) >= 8:
                    value_a = float32_from_le(data[0:4])

                    if frame_type == "output":
                        value_b = int32_from_le(data[4:8])
                    else:
                        value_b = float32_from_le(data[4:8])

                    event = PidDebugEvent(node_id=message.ID - base_id,
                                          frame_type=frame_type,
                                          can_id=message.ID,
                                          value_a=value_a,
                                          value_b=value_b,
                                          raw_data=data,
                                          timestamp_ms=now_monotonic_ms())
                    self.event_queue.put(event)

                return

        for part_index, base_id in enumerate(MOTOR_FOC_TRACE_PART_BASES):
            if message.ID in range(base_id + 1, base_id + 0x20):
                if len(data) >= 8:
                    event = FocTracePartEvent(node_id=message.ID - base_id,
                                              sample_index=uint16_from_le(data[0:2]),
                                              part_index=part_index,
                                              payload=data[2:8])
                    self.event_queue.put(event)

                return

        if message.ID in range(MOTOR_ACK_BASE + 1, MOTOR_ACK_BASE + 0x80):
            if len(data) >= 2:
                event = MotorAckEvent(node_id=message.ID - MOTOR_ACK_BASE,
                                      command=data[0],
                                      status=data[1],
                                      payload=data,
                                      timestamp_ms=now_monotonic_ms())
                self.event_queue.put(event)

    def _put_motor_report(self, event: MotorReportEvent) -> None:
        """
        @brief 投递电机主动上报事件。
        @param event 电机主动上报事件。
        @return None
        @note 电机上报频率较高，队列过长时丢弃新采样以保护 GUI。
        """
        if self.event_queue.qsize() >= EVENT_QUEUE_MAX:
            return

        self.event_queue.put(event)


class MotorToolApp:
    """@brief Duck Mid PCAN 电机调试 GUI。"""

    def __init__(self, root: tk.Tk, default_channel: int, default_bitrate: str) -> None:
        """
        @brief 初始化 GUI。
        @param root Tk 根窗口。
        @param default_channel 默认 PCAN 通道。
        @param default_bitrate 默认波特率名称。
        @return None
        """
        self.root = root
        self.root.title("Duck Mid PCAN Motor Tool")
        self.events: queue.Queue[object] = queue.Queue()
        self.client = PcanClient(self.events)
        self.motor_states: dict[int, MotorState] = {
            node_id: MotorState(name=motor_name_from_node_id(node_id), node_id=node_id)
            for node_id in MOTOR_NODE_IDS
        }
        self.channel_var = tk.StringVar(value=channel_label_from_value(default_channel))
        self.bitrate_var = tk.StringVar(value=default_bitrate)
        self.node_var = tk.StringVar(value=node_label_from_id(0x01))
        self.enable_var = tk.BooleanVar(value=True)
        self.mode_var = tk.StringVar(value="current")
        self.target_var = tk.StringVar(value="0")
        self.report_enable_var = tk.BooleanVar(value=True)
        self.report_hz_var = tk.StringVar(value="500")
        self.chart_window_var = tk.StringVar(value=str(int(CHART_HISTORY_SECONDS)))
        self.status_var = tk.StringVar(value="未连接")
        self.new_uid_var = tk.StringVar(value="")
        self.new_node_var = tk.StringVar(value="0x04")
        self.new_pole_pairs_var = tk.StringVar(value="7")
        self.new_phase_map_var = tk.StringVar(value="0")
        self.new_zero_after_config_var = tk.BooleanVar(value=True)
        self.new_report_off_var = tk.BooleanVar(value=True)
        self.pid_window: tk.Toplevel | None = None
        self.speed_pid_vars: dict[int, tk.StringVar] = {}
        self.position_pid_vars: dict[int, tk.StringVar] = {}
        self.recording = False
        self.record_pending_save = False
        self.record_node_id = 0x01
        self.record_start_ms = 0
        self.record_rows: list[dict[str, object]] = []
        self.trace_parts: dict[int, dict[int, bytes]] = {}
        self.trace_expected_count = 0
        self.trace_sample_hz = 0
        self.record_status_var = tk.StringVar(value="未记录")
        self.connected = False
        self.manage_reports: dict[int, ManageReportEvent] = {}
        self.manage_listen_active = False
        self.pending_new_config: tuple[int, int, int, int] | None = None
        self.chart_history: dict[int, deque[tuple[int, int, int]]] = {
            node_id: deque()
            for node_id in MOTOR_NODE_IDS
        }

        self._build_layout()
        self._refresh_motor_table()
        self._process_events()
        self._refresh_chart()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_layout(self) -> None:
        """
        @brief 创建 GUI 布局。
        @return None
        """
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)
        self.root.rowconfigure(2, weight=0)
        self.root.rowconfigure(3, weight=0)
        self.root.rowconfigure(4, weight=1)
        self.root.rowconfigure(5, weight=0)

        top_frame = ttk.Frame(self.root, padding=8)
        top_frame.grid(row=0, column=0, sticky="ew")
        top_frame.columnconfigure(6, weight=1)

        ttk.Label(top_frame, text="PCAN 通道").grid(row=0, column=0, padx=(0, 4))
        self.channel_combo = ttk.Combobox(top_frame,
                                          textvariable=self.channel_var,
                                          values=PCAN_CHANNEL_LABELS,
                                          width=22)
        self.channel_combo.grid(row=0, column=1, padx=(0, 8))
        ttk.Label(top_frame, text="波特率").grid(row=0, column=2, padx=(0, 4))
        ttk.Combobox(top_frame,
                     textvariable=self.bitrate_var,
                     values=list(PCAN_BITRATES.keys()),
                     width=8,
                     state="readonly").grid(row=0, column=3, padx=(0, 12))
        ttk.Button(top_frame,
                   text="扫描",
                   command=self._scan_channels).grid(row=0, column=4, padx=(0, 8))
        self.connect_button = ttk.Button(top_frame,
                                         text="连接",
                                         command=self._toggle_connection)
        self.connect_button.grid(row=0, column=5, padx=(0, 12))
        ttk.Label(top_frame, textvariable=self.status_var).grid(row=0,
                                                                column=6,
                                                                sticky="w")

        self._build_motor_table()
        self._build_control_panel()
        self._build_new_motor_panel()
        self._build_chart_panel()
        self._build_log_panel()

    def _build_motor_table(self) -> None:
        """
        @brief 创建电机状态表格。
        @return None
        """
        table_frame = ttk.Frame(self.root, padding=(8, 0, 8, 8))
        table_frame.grid(row=1, column=0, sticky="nsew")
        table_frame.rowconfigure(0, weight=1)
        table_frame.columnconfigure(0, weight=1)

        columns = (
            "node",
            "position",
            "degree",
            "speed",
            "age",
            "report",
            "version",
            "zero",
            "foc",
            "report_cfg",
            "ack",
            "tx",
        )
        self.motor_table = ttk.Treeview(table_frame,
                                        columns=columns,
                                        show="tree headings",
                                        height=8)
        self.motor_table.heading("#0", text="电机")
        self.motor_table.heading("node", text="节点")
        self.motor_table.heading("position", text="位置 mrad")
        self.motor_table.heading("degree", text="位置 deg")
        self.motor_table.heading("speed", text="速度 mrad/s")
        self.motor_table.heading("age", text="更新 ms")
        self.motor_table.heading("report", text="上报")
        self.motor_table.heading("version", text="版本")
        self.motor_table.heading("zero", text="零点 mrad")
        self.motor_table.heading("foc", text="FOC")
        self.motor_table.heading("report_cfg", text="上报配置")
        self.motor_table.heading("ack", text="最后应答")
        self.motor_table.heading("tx", text="发送")

        self.motor_table.column("#0", width=90, anchor="center")
        self.motor_table.column("node", width=70, anchor="center")
        self.motor_table.column("position", width=110, anchor="e")
        self.motor_table.column("degree", width=90, anchor="e")
        self.motor_table.column("speed", width=110, anchor="e")
        self.motor_table.column("age", width=80, anchor="e")
        self.motor_table.column("report", width=80, anchor="e")
        self.motor_table.column("version", width=80, anchor="center")
        self.motor_table.column("zero", width=100, anchor="e")
        self.motor_table.column("foc", width=110, anchor="center")
        self.motor_table.column("report_cfg", width=130, anchor="center")
        self.motor_table.column("ack", width=150, anchor="center")
        self.motor_table.column("tx", width=70, anchor="e")
        self.motor_table.grid(row=0, column=0, sticky="nsew")

        for node_id in MOTOR_NODE_IDS:
            self._ensure_table_item(node_id)

    def _build_control_panel(self) -> None:
        """
        @brief 创建电机控制面板。
        @return None
        """
        control_frame = ttk.LabelFrame(self.root, text="电机控制", padding=8)
        control_frame.grid(row=2, column=0, sticky="ew", padx=8, pady=(0, 8))
        control_frame.columnconfigure(13, weight=1)

        node_labels = [
            node_label_from_id(node_id)
            for node_id in MOTOR_NODE_IDS
        ]

        ttk.Label(control_frame, text="电机").grid(row=0, column=0, padx=(0, 4))
        ttk.Combobox(control_frame,
                     textvariable=self.node_var,
                     values=node_labels,
                     width=8,
                     state="readonly").grid(row=0, column=1, padx=(0, 8))
        ttk.Checkbutton(control_frame,
                        text="使能",
                        variable=self.enable_var).grid(row=0, column=2, padx=(0, 8))
        ttk.Label(control_frame, text="模式").grid(row=0, column=3, padx=(0, 4))
        ttk.Combobox(control_frame,
                     textvariable=self.mode_var,
                     values=list(MOTOR_MODES.keys()),
                     width=10,
                     state="readonly").grid(row=0, column=4, padx=(0, 8))
        ttk.Button(control_frame,
                   text="设置模式",
                   command=self._send_run_mode).grid(row=0, column=5, padx=(0, 8))

        ttk.Label(control_frame, text="目标").grid(row=0, column=6, padx=(0, 4))
        ttk.Entry(control_frame,
                  textvariable=self.target_var,
                  width=12).grid(row=0, column=7, padx=(0, 8))
        ttk.Button(control_frame,
                   text="发送目标",
                   command=self._send_target_by_mode).grid(row=0, column=8, padx=(0, 8))
        ttk.Button(control_frame,
                   text="停止当前",
                   command=self._send_stop_selected).grid(row=0, column=9, padx=(0, 8))
        ttk.Button(control_frame,
                   text="停止全部",
                   command=self._send_stop_all).grid(row=0, column=10, padx=(0, 8))

        ttk.Label(control_frame, text="上报 Hz").grid(row=1, column=0, padx=(0, 4), pady=(8, 0))
        ttk.Entry(control_frame,
                  textvariable=self.report_hz_var,
                  width=12).grid(row=1, column=1, padx=(0, 8), pady=(8, 0))
        ttk.Checkbutton(control_frame,
                        text="开启上报",
                        variable=self.report_enable_var).grid(row=1,
                                                             column=2,
                                                             padx=(0, 8),
                                                             pady=(8, 0))
        ttk.Button(control_frame,
                   text="设置上报",
                   command=self._send_report_config).grid(row=1,
                                                        column=3,
                                                        padx=(0, 8),
                                                        pady=(8, 0))
        ttk.Button(control_frame,
                   text="读取上报",
                   command=self._send_read_report).grid(row=1,
                                                      column=4,
                                                      padx=(0, 8),
                                                      pady=(8, 0))
        ttk.Button(control_frame,
                   text="读取版本",
                   command=self._send_read_version).grid(row=1,
                                                       column=5,
                                                       padx=(0, 8),
                                                       pady=(8, 0))
        ttk.Button(control_frame,
                   text="读取节点",
                   command=self._send_read_node).grid(row=1,
                                                    column=6,
                                                    padx=(0, 8),
                                                    pady=(8, 0))
        ttk.Button(control_frame,
                   text="读取当前",
                   command=self._send_read_selected).grid(row=1,
                                                       column=7,
                                                       padx=(0, 8),
                                                       pady=(8, 0))
        ttk.Button(control_frame,
                   text="读取全部",
                   command=self._send_read_all).grid(row=1,
                                                   column=8,
                                                   padx=(0, 8),
                                                   pady=(8, 0))
        ttk.Button(control_frame,
                   text="零点校准",
                   command=self._send_zero_calibration).grid(row=1,
                                                           column=9,
                                                           padx=(0, 8),
                                                           pady=(8, 0))
        ttk.Button(control_frame,
                   text="读取零点",
                   command=self._send_read_zero).grid(row=1,
                                                    column=10,
                                                    padx=(0, 8),
                                                    pady=(8, 0))
        ttk.Button(control_frame,
                   text="位置/速度环配置",
                   command=self._open_pid_config_window).grid(row=1,
                                                             column=11,
                                                             padx=(0, 8),
                                                             pady=(8, 0))
        self.record_button = ttk.Button(control_frame,
                                        text="开始记录",
                                        command=self._toggle_recording)
        self.record_button.grid(row=0, column=11, padx=(0, 8))
        ttk.Label(control_frame,
                  textvariable=self.record_status_var).grid(row=0,
                                                            column=12,
                                                            padx=(0, 8),
                                                            sticky="w")

    def _toggle_recording(self) -> None:
        """
        @brief 切换速度环诊断记录状态。
        @return None
        """
        if self.recording:
            self._stop_recording_and_save()
        else:
            if self.record_pending_save:
                self._stop_recording_and_save()
            else:
                self._start_recording()

    def _start_recording(self) -> None:
        """
        @brief 启动当前选中电机的 FOC 高速环形记录。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        try:
            self.record_node_id = self._selected_node_id()
            self.client.send_foc_trace_command(self.record_node_id, 0)
            self._record_tx(self.record_node_id)
        except Exception as exc:
            messagebox.showerror("开始记录失败", str(exc))
            return

        self.record_start_ms = now_monotonic_ms()
        self.record_rows = []
        self.trace_parts = {}
        self.trace_expected_count = 0
        self.trace_sample_hz = 0
        self.recording = True
        self.record_pending_save = False
        self.record_button.configure(text="停止并保存")
        self.record_status_var.set(f"高速记录 0x{self.record_node_id:02X}")
        self._append_log(f"开始 FOC 高速记录 node=0x{self.record_node_id:02X}")

    def _stop_recording_and_save(self) -> None:
        """
        @brief 停止 FOC 高速记录并请求上传数据。
        @return None
        """
        if self.record_pending_save:
            self._show_trace_save_dialog()
            return

        self.recording = False
        self.record_button.configure(state="disabled")
        self.record_status_var.set("正在冻结并读取...")

        try:
            self.client.send_foc_trace_command(self.record_node_id, 1)
            self._record_tx(self.record_node_id)
        except Exception as exc:
            self.record_button.configure(text="开始记录", state="normal")
            messagebox.showerror("停止记录失败", str(exc))

    def _show_trace_save_dialog(self) -> None:
        """
        @brief 显示 FOC 高速记录保存对话框。
        @return None
        """
        if len(self.record_rows) == 0:
            self.record_status_var.set("未读取到高速数据")
            self.record_button.configure(text="开始记录", state="normal")
            messagebox.showwarning("无记录", "没有收到 FOC 高速记录数据")
            return

        default_name = time.strftime(
            f"motor_{self.record_node_id:02X}_foc_trace_%Y%m%d_%H%M%S.csv"
        )
        file_path = filedialog.asksaveasfilename(
            title="保存 FOC 高速记录",
            defaultextension=".csv",
            initialfile=default_name,
            filetypes=(("CSV 文件", "*.csv"), ("所有文件", "*.*")),
        )

        if file_path == "":
            self.record_pending_save = True
            self.record_button.configure(text="保存记录")
            self.record_status_var.set(f"待保存: {len(self.record_rows)} 帧")
            return

        self._save_record_csv(file_path)

    def _save_record_csv(self, file_path: str) -> None:
        """
        @brief 将当前诊断记录写入 CSV 文件。
        @param file_path CSV 文件路径。
        @return None
        """
        field_names = (
            "sample_index",
            "trigger_relative_us",
            "sensor_angle_mrad",
            "mechanical_angle_mrad",
            "electrical_angle_mrad",
            "speed_mrad_s",
            "q_target",
            "pwm_a",
            "pwm_b",
            "pwm_c",
        )

        try:
            with open(file_path, "w", newline="", encoding="utf-8-sig") as csv_file:
                writer = csv.DictWriter(csv_file, fieldnames=field_names)
                writer.writeheader()
                writer.writerows(self.record_rows)
        except OSError as exc:
            self.record_pending_save = True
            self.record_button.configure(text="保存记录")
            self.record_status_var.set(f"保存失败: {len(self.record_rows)} 帧")
            messagebox.showerror("保存失败", str(exc))
            return

        row_count = len(self.record_rows)
        self.record_pending_save = False
        self.record_button.configure(text="开始记录", state="normal")
        self.record_status_var.set(f"已保存: {row_count} 点")
        self._append_log(f"FOC 高速记录已保存: {file_path}, {row_count} 点")
        messagebox.showinfo("保存完成", f"已保存 {row_count} 点\n{file_path}")

    def _open_pid_config_window(self) -> None:
        """
        @brief 打开当前电机的位置环和速度环配置窗口。
        @return None
        """
        if self.pid_window is not None and self.pid_window.winfo_exists():
            self.pid_window.deiconify()
            self.pid_window.lift()
            self.pid_window.focus_force()
            return

        self.speed_pid_vars = {
            param_id: tk.StringVar(value="")
            for _param_name, param_id in MOTOR_PID_PARAMETERS
        }
        self.position_pid_vars = {
            param_id: tk.StringVar(value="")
            for _param_name, param_id in MOTOR_PID_PARAMETERS
        }

        self.pid_window = tk.Toplevel(self.root)
        self.pid_window.title("位置/速度环配置")
        self.pid_window.resizable(False, False)
        self.pid_window.transient(self.root)
        self.pid_window.protocol("WM_DELETE_WINDOW", self._close_pid_config_window)

        content_frame = ttk.Frame(self.pid_window, padding=12)
        content_frame.grid(row=0, column=0, sticky="nsew")
        ttk.Label(content_frame, text="当前电机").grid(row=0,
                                                       column=0,
                                                       padx=(0, 4),
                                                       pady=(0, 10),
                                                       sticky="w")
        ttk.Label(content_frame, textvariable=self.node_var).grid(row=0,
                                                                 column=1,
                                                                 pady=(0, 10),
                                                                 sticky="w")

        self._build_pid_group(content_frame,
                              1,
                              "速度环",
                              self.speed_pid_vars,
                              MOTOR_CMD_SET_SPEED_PID_PARAM,
                              MOTOR_CMD_READ_SPEED_PID_PARAM)
        self._build_pid_group(content_frame,
                              2,
                              "位置环",
                              self.position_pid_vars,
                              MOTOR_CMD_SET_POSITION_PID_PARAM,
                              MOTOR_CMD_READ_POSITION_PID_PARAM)

    def _build_pid_group(self,
                         parent: ttk.Frame,
                         row: int,
                         title: str,
                         parameter_vars: dict[int, tk.StringVar],
                         set_command: int,
                         read_command: int) -> None:
        """
        @brief 创建一组 PID 参数编辑控件。
        @param parent 父级界面容器。
        @param row 父级网格行号。
        @param title 参数组标题。
        @param parameter_vars 参数编号到输入变量的映射。
        @param set_command 参数写入命令码。
        @param read_command 参数读取命令码。
        @return None
        """
        group_frame = ttk.LabelFrame(parent, text=title, padding=10)
        group_frame.grid(row=row,
                         column=0,
                         columnspan=2,
                         padx=0,
                         pady=(0, 10),
                         sticky="ew")

        for column, (param_name, param_id) in enumerate(MOTOR_PID_PARAMETERS):
            ttk.Label(group_frame, text=param_name).grid(row=0,
                                                        column=column,
                                                        padx=(0, 8),
                                                        sticky="w")
            ttk.Entry(group_frame,
                      textvariable=parameter_vars[param_id],
                      width=12).grid(row=1,
                                     column=column,
                                     padx=(0, 8),
                                     pady=(4, 0))

        ttk.Button(group_frame,
                   text="读取",
                   command=lambda: self._read_pid_group(read_command)).grid(row=1,
                                                                           column=5,
                                                                           padx=(4, 8),
                                                                           pady=(4, 0))
        ttk.Button(group_frame,
                   text="写入",
                   command=lambda: self._write_pid_group(set_command,
                                                         parameter_vars)).grid(row=1,
                                                                              column=6,
                                                                              pady=(4, 0))

    def _close_pid_config_window(self) -> None:
        """
        @brief 关闭位置环和速度环配置窗口。
        @return None
        """
        if self.pid_window is not None:
            self.pid_window.destroy()

        self.pid_window = None

    def _read_pid_group(self, command: int) -> None:
        """
        @brief 读取当前电机的一组 PID 参数。
        @param command PID 参数读取命令码。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        try:
            node_id = self._selected_node_id()

            for _param_name, param_id in MOTOR_PID_PARAMETERS:
                self.client.send_read_pid_param(node_id, command, param_id)
                self._record_tx(node_id)
                time.sleep(0.002)

            self._append_log(
                f"tx node=0x{node_id:02X} {command_text(command)} all"
            )
        except Exception as exc:
            messagebox.showerror("读取失败", str(exc))

    def _write_pid_group(self,
                         command: int,
                         parameter_vars: dict[int, tk.StringVar]) -> None:
        """
        @brief 写入当前电机的一组 PID 参数。
        @param command PID 参数写入命令码。
        @param parameter_vars 参数编号到输入变量的映射。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        try:
            node_id = self._selected_node_id()
            values = {
                param_id: self._parse_pid_value(param_id,
                                                parameter_vars[param_id].get())
                for _param_name, param_id in MOTOR_PID_PARAMETERS
            }

            for _param_name, param_id in MOTOR_PID_PARAMETERS:
                self.client.send_pid_param(node_id,
                                           command,
                                           param_id,
                                           values[param_id])
                self._record_tx(node_id)
                time.sleep(0.002)

            self._append_log(
                f"tx node=0x{node_id:02X} {command_text(command)} all"
            )
        except Exception as exc:
            messagebox.showerror("写入失败", str(exc))

    def _parse_pid_value(self, param_id: int, text: str) -> float:
        """
        @brief 将 PID 参数输入文本转换为单精度浮点数。
        @param param_id PID 参数编号。
        @param text 参数输入文本。
        @return float 单精度浮点参数值。
        """
        value = float(text.strip())

        if not math.isfinite(value):
            raise ValueError("PID 参数必须是有限数值")

        if param_id in (MOTOR_PID_PARAM_I_ACC_MAX, MOTOR_PID_PARAM_OUT_MAX):
            if (value < 0.0) or (value > 32767.0):
                raise ValueError("I_ACC_MAX 和 OUT_MAX 必须在 0~32767 范围内")

        try:
            float32_to_le(value)
        except OverflowError as exc:
            raise ValueError("PID 参数超出 float32 范围") from exc

        return value

    def _build_new_motor_panel(self) -> None:
        """
        @brief 创建新电机配置面板。
        @return None
        """
        config_frame = ttk.LabelFrame(self.root, text="新电机配置", padding=8)
        config_frame.grid(row=3, column=0, sticky="ew", padx=8, pady=(0, 8))
        config_frame.columnconfigure(14, weight=1)

        node_labels = [
            node_label_from_id(node_id)
            for node_id in MOTOR_NODE_IDS
        ]

        ttk.Label(config_frame, text="UID32").grid(row=0, column=0, padx=(0, 4))
        ttk.Entry(config_frame,
                  textvariable=self.new_uid_var,
                  width=12).grid(row=0, column=1, padx=(0, 8))
        ttk.Button(config_frame,
                   text="监听发现",
                   command=self._listen_manage_reports).grid(row=0, column=2, padx=(0, 8))
        ttk.Button(config_frame,
                   text="识别",
                   command=self._send_manage_identify).grid(row=0, column=3, padx=(0, 12))

        ttk.Label(config_frame, text="新节点").grid(row=0, column=4, padx=(0, 4))
        ttk.Combobox(config_frame,
                     textvariable=self.new_node_var,
                     values=node_labels,
                     width=8).grid(row=0, column=5, padx=(0, 8))
        ttk.Label(config_frame, text="极对数").grid(row=0, column=6, padx=(0, 4))
        ttk.Entry(config_frame,
                  textvariable=self.new_pole_pairs_var,
                  width=6).grid(row=0, column=7, padx=(0, 8))
        ttk.Label(config_frame, text="相序").grid(row=0, column=8, padx=(0, 4))
        ttk.Combobox(config_frame,
                     textvariable=self.new_phase_map_var,
                     values=[str(index) for index in range(6)],
                     width=5).grid(row=0, column=9, padx=(0, 8))
        ttk.Checkbutton(config_frame,
                        text="配置后校零",
                        variable=self.new_zero_after_config_var).grid(row=0,
                                                                     column=10,
                                                                     padx=(0, 8))
        ttk.Checkbutton(config_frame,
                        text="关闭上报",
                        variable=self.new_report_off_var).grid(row=0,
                                                              column=11,
                                                              padx=(0, 8))
        ttk.Button(config_frame,
                   text="配置新电机",
                   command=self._configure_new_motor).grid(row=0, column=12, padx=(0, 8))
        ttk.Button(config_frame,
                   text="读 FOC",
                   command=self._send_read_foc_selected).grid(row=0, column=13, padx=(0, 8))

    def _build_chart_panel(self) -> None:
        """
        @brief 创建当前电机位置和速度曲线区域。
        @return None
        """
        chart_frame = ttk.LabelFrame(self.root, text="当前电机曲线", padding=8)
        chart_frame.grid(row=4, column=0, sticky="nsew", padx=8, pady=(0, 8))
        chart_frame.rowconfigure(1, weight=1)
        chart_frame.columnconfigure(0, weight=1)

        chart_toolbar = ttk.Frame(chart_frame)
        chart_toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        ttk.Label(chart_toolbar, text="窗口 s").grid(row=0, column=0, padx=(0, 4))
        ttk.Entry(chart_toolbar,
                  textvariable=self.chart_window_var,
                  width=8).grid(row=0, column=1, padx=(0, 8))
        ttk.Label(chart_toolbar,
                  text="蓝色=位置，橙色=速度").grid(row=0, column=2, sticky="w")

        self.chart_canvas = tk.Canvas(chart_frame,
                                      height=260,
                                      bg="#111827",
                                      highlightthickness=0)
        self.chart_canvas.grid(row=1, column=0, sticky="nsew")

    def _build_log_panel(self) -> None:
        """
        @brief 创建日志面板。
        @return None
        """
        log_frame = ttk.LabelFrame(self.root, text="日志", padding=8)
        log_frame.grid(row=5, column=0, sticky="ew", padx=8, pady=(0, 8))
        log_frame.columnconfigure(0, weight=1)

        self.log_text = tk.Text(log_frame, height=8, width=120)
        self.log_text.grid(row=0, column=0, sticky="ew")
        self.log_text.configure(state="disabled")

    def _toggle_connection(self) -> None:
        """
        @brief 切换 PCAN 连接状态。
        @return None
        """
        if self.connected:
            self.client.disconnect()
            self.connected = False
            self.manage_listen_active = False

            if self.pending_new_config is not None:
                self.pending_new_config = None
                self._append_log("configure new canceled by disconnect")

            self.connect_button.configure(text="连接")
            self.status_var.set("未连接")
            return

        try:
            channel = channel_value_from_label(self.channel_var.get())
            bitrate = PCAN_BITRATES[self.bitrate_var.get()]
            self.client.connect(channel, bitrate)
        except Exception as exc:
            messagebox.showerror("连接失败", str(exc))
            return

        self.connected = True
        self.connect_button.configure(text="断开")
        self.status_var.set(f"已连接 channel=0x{channel:X} bitrate={self.bitrate_var.get()}")
        self._append_log("PCAN connected")

    def _scan_channels(self) -> None:
        """
        @brief 扫描当前可用 PCAN 通道。
        @return None
        """
        try:
            bitrate = PCAN_BITRATES[self.bitrate_var.get()]
            available_channels = self.client.scan_channels(bitrate)
        except Exception as exc:
            messagebox.showerror("扫描失败", str(exc))
            return

        if len(available_channels) == 0:
            self.status_var.set("未找到可用 PCAN 通道")
            messagebox.showwarning("扫描结果", "未找到可用 PCAN 通道")
            return

        labels = [
            f"{channel_name} (0x{channel_value:X})"
            for channel_name, channel_value in available_channels
        ]
        self.channel_combo.configure(values=labels)
        self.channel_var.set(labels[0])
        self.status_var.set(f"找到 {len(labels)} 个 PCAN 通道")
        self._append_log("Available channels: " + ", ".join(labels))

    def _selected_node_id(self) -> int:
        """
        @brief 获取当前选中的电机节点 ID。
        @return int 节点 ID。
        """
        node_id = node_id_from_label(self.node_var.get())

        if node_id not in MOTOR_NODE_IDS:
            raise ValueError("节点 ID 范围必须为 0x01~0x08")

        return node_id

    def _report_period_from_hz(self) -> int:
        """
        @brief 根据界面上报频率计算周期。
        @return int 上报周期，单位毫秒。
        """
        hz = parse_int_auto(self.report_hz_var.get())

        if hz <= 0:
            raise ValueError("上报频率必须大于 0")

        period_ms = int(round(1000.0 / hz))

        if period_ms < MOTOR_REPORT_PERIOD_MIN_MS:
            raise ValueError("上报周期不能小于 2ms，最高建议 500Hz")

        if period_ms > MOTOR_REPORT_PERIOD_MAX_MS:
            raise ValueError("上报周期不能超过 60000ms")

        return period_ms

    def _get_or_create_state(self, node_id: int) -> MotorState:
        """
        @brief 获取或创建电机状态对象。
        @param node_id 节点 ID。
        @return MotorState 电机状态对象。
        """
        state = self.motor_states.get(node_id)

        if state is not None:
            return state

        state = MotorState(name=motor_name_from_node_id(node_id), node_id=node_id)
        self.motor_states[node_id] = state
        self._ensure_table_item(node_id)

        return state

    def _ensure_table_item(self, node_id: int) -> None:
        """
        @brief 确保表格中存在指定节点。
        @param node_id 节点 ID。
        @return None
        """
        iid = str(node_id)

        if self.motor_table.exists(iid):
            return

        state = self.motor_states[node_id]
        self.motor_table.insert("",
                                "end",
                                iid=iid,
                                text=state.name,
                                values=(f"0x{node_id:02X}",
                                        "--",
                                        "--",
                                        "--",
                                        "--",
                                        "0",
                                        "--",
                                        "--",
                                        "--",
                                        "--",
                                        "--",
                                        "0"))

    def _record_tx(self, node_id: int) -> None:
        """
        @brief 记录一次发送计数。
        @param node_id 节点 ID。
        @return None
        """
        state = self._get_or_create_state(node_id)
        state.tx_count += 1

    def _parse_new_uid(self) -> int:
        """
        @brief 解析新电机 UID32 输入框。
        @return int UID32。
        """
        uid32 = parse_int_auto(self.new_uid_var.get())

        if (uid32 < 0) or (uid32 > 0xFFFFFFFF):
            raise ValueError("UID32 必须在 0x00000000~0xFFFFFFFF 范围内")

        return uid32

    def _selected_new_node_id(self) -> int:
        """
        @brief 解析新电机目标节点 ID。
        @return int 节点 ID。
        """
        node_id = node_id_from_label(self.new_node_var.get())

        if node_id not in MOTOR_NODE_IDS:
            raise ValueError("新节点 ID 范围必须为 0x01~0x08")

        return node_id

    def _parse_new_foc_config(self) -> tuple[int, int]:
        """
        @brief 解析新电机 FOC 配置输入。
        @return tuple[int, int] 极对数和相序映射编号。
        """
        pole_pairs = parse_int_auto(self.new_pole_pairs_var.get())
        phase_map = parse_int_auto(self.new_phase_map_var.get())

        if (pole_pairs < 1) or (pole_pairs > 32):
            raise ValueError("极对数必须为 1~32")

        if (phase_map < 0) or (phase_map > 5):
            raise ValueError("相序必须为 0~5")

        return pole_pairs, phase_map

    def _listen_manage_reports(self) -> None:
        """
        @brief 使用当前接收线程监听未配置电机发现上报。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        self.manage_reports.clear()
        self.manage_listen_active = True
        self._append_log("listening manage reports for 2s")
        self.root.after(2000, self._finish_manage_listen)

    def _finish_manage_listen(self) -> None:
        """
        @brief 结束管理发现监听并显示结果。
        @return None
        """
        if not self.manage_listen_active:
            return

        self.manage_listen_active = False

        if len(self.manage_reports) == 0:
            self._append_log("manage report none")
            return

        reports = sorted(self.manage_reports.values(),
                         key=lambda item: (item.configured != 0, item.uid32))
        first_report = reports[0]
        self.new_uid_var.set(f"0x{first_report.uid32:08X}")

        for report in reports:
            self._append_log(
                f"manage uid=0x{report.uid32:08X} configured={report.configured} "
                f"node=0x{report.node_id:02X}"
            )

    def _send_manage_identify(self) -> None:
        """
        @brief 发送新电机识别灯效命令。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        try:
            uid32 = self._parse_new_uid()
            self.client.send_manage_identify(uid32, 5, 5)
            self._append_log(f"tx manage identify uid=0x{uid32:08X}")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _configure_new_motor(self) -> None:
        """
        @brief 按界面参数配置未配置电机。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        if self.pending_new_config is not None:
            messagebox.showwarning("正在配置", "正在等待上一条新电机配置应答")
            return

        try:
            uid32 = self._parse_new_uid()
            node_id = self._selected_new_node_id()
            pole_pairs, phase_map = self._parse_new_foc_config()

            self.pending_new_config = (uid32, node_id, pole_pairs, phase_map)
            self.client.send_manage_set_can_id(uid32, node_id)
            self._record_tx(node_id)
            self._append_log(f"tx manage set_id uid=0x{uid32:08X} node=0x{node_id:02X}")
            self.root.after(
                1200,
                lambda: self._check_new_motor_config_timeout(uid32, node_id),
            )
        except Exception as exc:
            self.pending_new_config = None
            messagebox.showerror("配置失败", str(exc))

    def _check_new_motor_config_timeout(self, uid32: int, node_id: int) -> None:
        """
        @brief 检查新电机配置流程是否等待管理应答超时。
        @param uid32 目标短芯片 ID。
        @param node_id 目标业务节点 ID。
        @return None
        """
        pending_config = self.pending_new_config

        if pending_config is None:
            return

        if pending_config[0] != uid32 or pending_config[1] != node_id:
            return

        self.pending_new_config = None
        self._append_log(
            f"configure new timeout uid=0x{uid32:08X} node=0x{node_id:02X}"
        )

    def _continue_pending_new_motor_config(self, uid32: int, node_id: int) -> None:
        """
        @brief 在管理节点 ID 配置成功后继续执行新电机配置流程。
        @param uid32 目标短芯片 ID。
        @param node_id 目标业务节点 ID。
        @return None
        """
        pending_config = self.pending_new_config

        if pending_config is None:
            return

        if pending_config[0] != uid32 or pending_config[1] != node_id:
            return

        self.pending_new_config = None
        self.root.after(
            100,
            lambda: self._finish_configure_new_motor(node_id,
                                                     pending_config[2],
                                                     pending_config[3]),
        )

    def _finish_configure_new_motor(self, node_id: int, pole_pairs: int, phase_map: int) -> None:
        """
        @brief 配置新电机业务 ID 后继续写入 FOC 参数。
        @param node_id 目标节点 ID。
        @param pole_pairs 极对数。
        @param phase_map 相序映射编号。
        @return None
        """
        if not self.connected:
            self._append_log(
                f"configure new stopped because PCAN disconnected node=0x{node_id:02X}"
            )
            return

        try:
            self.client.send_foc_config(node_id, MOTOR_FOC_PARAM_POLE_PAIRS, pole_pairs)
            self._record_tx(node_id)
            time.sleep(0.01)
            self.client.send_foc_config(node_id, MOTOR_FOC_PARAM_PHASE_MAP, phase_map)
            self._record_tx(node_id)
            time.sleep(0.01)

            if self.new_report_off_var.get():
                self.client.send_report_config(node_id, False, 10)
                self._record_tx(node_id)
                time.sleep(0.01)

            if self.new_zero_after_config_var.get():
                self.client.send_simple_command(node_id, MOTOR_CMD_ZERO_CALIBRATION)
                self._record_tx(node_id)
                time.sleep(0.01)

            self._send_read_info_for_node(node_id)
            self.node_var.set(node_label_from_id(node_id))
            self._append_log(
                f"tx configure new node=0x{node_id:02X} pole_pairs={pole_pairs} "
                f"phase_map={phase_map}"
            )
        except Exception as exc:
            messagebox.showerror("配置失败", str(exc))

    def _send_read_foc_selected(self) -> None:
        """
        @brief 读取当前选中电机的常用 FOC 配置。
        @return None
        """
        if not self.connected:
            messagebox.showwarning("未连接", "请先连接 PCAN")
            return

        try:
            node_id = self._selected_node_id()
            self.client.send_read_foc_config(node_id, MOTOR_FOC_PARAM_POLE_PAIRS)
            self._record_tx(node_id)
            time.sleep(0.002)
            self.client.send_read_foc_config(node_id, MOTOR_FOC_PARAM_PHASE_MAP)
            self._record_tx(node_id)
            self._append_log(f"tx node=0x{node_id:02X} read foc")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_run_mode(self) -> None:
        """
        @brief 发送当前选中电机运行模式。
        @return None
        """
        try:
            node_id = self._selected_node_id()
            mode_id = MOTOR_MODES[self.mode_var.get()]
            self.client.send_run_mode(node_id, self.enable_var.get(), mode_id)
            self._record_tx(node_id)
            self._append_log(
                f"tx node=0x{node_id:02X} set_run_mode enable={int(self.enable_var.get())} "
                f"mode={self.mode_var.get()}"
            )
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_target_by_mode(self) -> None:
        """
        @brief 按当前模式发送目标值。
        @return None
        """
        try:
            node_id = self._selected_node_id()
            target = parse_int_auto(self.target_var.get())
            mode_name = self.mode_var.get()

            if mode_name == "current":
                self.client.send_current_target(node_id, target)
            elif mode_name == "speed":
                self.client.send_speed_target(node_id, target)
            else:
                self.client.send_position_target(node_id, target)

            self._record_tx(node_id)
            self._append_log(f"tx node=0x{node_id:02X} target mode={mode_name} value={target}")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_stop_selected(self) -> None:
        """
        @brief 停止当前选中电机输出。
        @return None
        """
        try:
            node_id = self._selected_node_id()
            self.client.send_stop_output(node_id)
            self._record_tx(node_id)
            self._append_log(f"tx node=0x{node_id:02X} stop_output")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_stop_all(self) -> None:
        """
        @brief 停止 0x01~0x08 节点电机输出。
        @return None
        """
        try:
            for node_id in MOTOR_NODE_IDS:
                self.client.send_stop_output(node_id)
                self._record_tx(node_id)
                time.sleep(0.002)

            self._append_log("tx stop_output nodes 0x01~0x08")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_report_config(self) -> None:
        """
        @brief 设置当前选中电机主动上报配置。
        @return None
        """
        try:
            node_id = self._selected_node_id()
            period_ms = self._report_period_from_hz()
            self.client.send_report_config(node_id, self.report_enable_var.get(), period_ms)
            self._record_tx(node_id)
            self._append_log(
                f"tx node=0x{node_id:02X} set_report enable={int(self.report_enable_var.get())} "
                f"period={period_ms}ms"
            )
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_read_report(self) -> None:
        """
        @brief 请求读取当前选中电机主动上报配置。
        @return None
        """
        self._send_simple_selected(MOTOR_CMD_READ_REPORT_CONFIG)

    def _send_read_version(self) -> None:
        """
        @brief 请求读取当前选中电机版本号。
        @return None
        """
        self._send_simple_selected(MOTOR_CMD_READ_APP_VERSION)

    def _send_read_node(self) -> None:
        """
        @brief 请求读取当前选中电机节点 ID。
        @return None
        """
        self._send_simple_selected(MOTOR_CMD_READ_NODE_ID)

    def _send_zero_calibration(self) -> None:
        """
        @brief 请求当前选中电机执行零点校准并保存。
        @return None
        """
        self._send_simple_selected(MOTOR_CMD_ZERO_CALIBRATION)

    def _send_read_zero(self) -> None:
        """
        @brief 请求读取当前选中电机零点。
        @return None
        """
        self._send_simple_selected(MOTOR_CMD_READ_ZERO)

    def _send_read_selected(self) -> None:
        """
        @brief 请求读取当前选中电机常用信息。
        @return None
        """
        try:
            node_id = self._selected_node_id()
            self._send_read_info_for_node(node_id)
            self._append_log(f"tx node=0x{node_id:02X} read selected")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_read_all(self) -> None:
        """
        @brief 请求读取 0x01~0x08 节点电机常用信息。
        @return None
        """
        try:
            for node_id in MOTOR_NODE_IDS:
                self._send_read_info_for_node(node_id)
                time.sleep(0.004)

            self._append_log("tx read nodes 0x01~0x08")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _send_read_info_for_node(self, node_id: int) -> None:
        """
        @brief 请求读取指定电机常用信息。
        @param node_id 节点 ID。
        @return None
        """
        for command in (
            MOTOR_CMD_READ_NODE_ID,
            MOTOR_CMD_READ_APP_VERSION,
            MOTOR_CMD_READ_REPORT_CONFIG,
            MOTOR_CMD_READ_ZERO,
        ):
            self.client.send_simple_command(node_id, command)
            self._record_tx(node_id)
            time.sleep(0.002)

        for param_id in (
            MOTOR_FOC_PARAM_POLE_PAIRS,
            MOTOR_FOC_PARAM_PHASE_MAP,
        ):
            self.client.send_read_foc_config(node_id, param_id)
            self._record_tx(node_id)
            time.sleep(0.002)

    def _send_simple_selected(self, command: int) -> None:
        """
        @brief 向当前选中电机发送无参数命令。
        @param command 命令码。
        @return None
        """
        try:
            node_id = self._selected_node_id()
            self.client.send_simple_command(node_id, command)
            self._record_tx(node_id)
            self._append_log(f"tx node=0x{node_id:02X} {command_text(command)}")
        except Exception as exc:
            messagebox.showerror("发送失败", str(exc))

    def _process_events(self) -> None:
        """
        @brief 处理 PCAN 接收线程投递的事件。
        @return None
        """
        processed_count = 0

        while processed_count < EVENT_PROCESS_LIMIT:
            try:
                event = self.events.get_nowait()
            except queue.Empty:
                break

            processed_count += 1

            if isinstance(event, MotorReportEvent):
                self._handle_motor_report(event)

            if isinstance(event, PidDebugEvent):
                self._handle_pid_debug(event)

            if isinstance(event, FocTracePartEvent):
                self._handle_foc_trace_part(event)

            if isinstance(event, MotorAckEvent):
                self._handle_motor_ack(event)

            if isinstance(event, ManageReportEvent):
                self._handle_manage_report(event)

            if isinstance(event, ManageAckEvent):
                self._handle_manage_ack(event)

            if isinstance(event, ErrorEvent):
                self._append_log(f"ERROR: {event.message}")

        self.root.after(20, self._process_events)

    def _handle_motor_report(self, event: MotorReportEvent) -> None:
        """
        @brief 处理电机主动上报事件。
        @param event 电机主动上报事件。
        @return None
        """
        state = self._get_or_create_state(event.node_id)
        state.position_mrad = event.position_mrad
        state.speed_mrad_s = event.speed_mrad_s
        state.last_report_ms = event.timestamp_ms
        state.report_count += 1
        self._append_chart_sample(event)

    def _handle_pid_debug(self, event: PidDebugEvent) -> None:
        """
        @brief 处理并按需记录一帧速度环 PID 诊断数据。
        @param event PID 诊断帧事件。
        @return None
        """
        return

    def _handle_foc_trace_part(self, event: FocTracePartEvent) -> None:
        """
        @brief 接收并组装一帧 FOC 高速记录分片。
        @param event FOC 高速记录分片事件。
        @return None
        """
        if event.node_id != self.record_node_id:
            return

        sample_parts = self.trace_parts.setdefault(event.sample_index, {})
        sample_parts[event.part_index] = event.payload
        completed_count = sum(
            1
            for parts in self.trace_parts.values()
            if len(parts) == len(MOTOR_FOC_TRACE_PART_BASES)
        )
        self.record_status_var.set(
            f"读取高速数据: {completed_count}/{self.trace_expected_count}"
        )

        if ((self.trace_expected_count > 0)
            and (completed_count >= self.trace_expected_count)):
            self._build_foc_trace_rows()
            self.record_pending_save = True
            self.record_button.configure(text="保存记录", state="normal")
            self.record_status_var.set(f"读取完成: {len(self.record_rows)} 点")
            self._show_trace_save_dialog()

    def _build_foc_trace_rows(self) -> None:
        """
        @brief 将已组装的 FOC 高速记录转换为 CSV 行。
        @return None
        """
        decoded_samples: list[tuple[int, int, int, int, int, int, int, int, int, int]] = []

        for sample_index in range(self.trace_expected_count):
            parts = self.trace_parts.get(sample_index)

            if (parts is None) or (len(parts) != len(MOTOR_FOC_TRACE_PART_BASES)):
                continue

            raw_sample = b"".join(parts[index] for index in range(3))
            decoded_samples.append(struct.unpack("<HHHHihBBBB", raw_sample))

        trigger_index = 0
        for index, sample in enumerate(decoded_samples):
            if abs(sample[4]) >= 10000:
                trigger_index = index
                break

        trigger_timestamp_ms = decoded_samples[trigger_index][0]
        self.record_rows = []

        for index, sample in enumerate(decoded_samples):
            relative_ms = ((sample[0] - trigger_timestamp_ms + 32768) % 65536) - 32768
            self.record_rows.append({
                "sample_index": index,
                "trigger_relative_us": relative_ms * 1000,
                "sensor_angle_mrad": sample[1],
                "mechanical_angle_mrad": sample[2],
                "electrical_angle_mrad": sample[3],
                "speed_mrad_s": sample[4],
                "q_target": sample[5],
                "pwm_a": sample[6],
                "pwm_b": sample[7],
                "pwm_c": sample[8],
            })

    def _handle_motor_ack(self, event: MotorAckEvent) -> None:
        """
        @brief 处理电机应答事件。
        @param event 电机应答事件。
        @return None
        """
        state = self._get_or_create_state(event.node_id)
        state.last_ack_cmd = event.command
        state.last_ack_status = event.status
        state.last_ack_ms = event.timestamp_ms
        state.ack_count += 1

        if event.status == 0:
            self._update_state_from_ack(state, event)
            self._update_pid_window_from_ack(event)

        if event.command == MOTOR_CMD_FOC_TRACE:
            self._handle_foc_trace_ack(event)

        self._append_log(
            f"rx node=0x{event.node_id:02X} {command_text(event.command)} "
            f"status={status_text(event.status)}{self._pid_ack_detail(event)}"
        )

    def _handle_foc_trace_ack(self, event: MotorAckEvent) -> None:
        """
        @brief 处理 FOC 高速记录控制命令应答。
        @param event 电机应答事件。
        @return None
        """
        if event.status != 0 or len(event.payload) < 8:
            self.recording = False
            self.record_button.configure(text="开始记录", state="normal")
            self.record_status_var.set("高速记录命令失败")
            return

        action = event.payload[2]
        sample_count = uint16_from_le(event.payload[4:6])
        sample_hz = uint16_from_le(event.payload[6:8])

        if action == 1:
            self.trace_expected_count = sample_count
            self.trace_sample_hz = sample_hz
            self.trace_parts = {}

            if sample_count == 0:
                self.record_button.configure(text="开始记录", state="normal")
                self.record_status_var.set("高速缓存为空")
                return

            self.record_status_var.set(f"准备读取 {sample_count} 点...")
            self.client.send_foc_trace_command(self.record_node_id, 2)
            self._record_tx(self.record_node_id)
        else:
            if action == 2:
                self.trace_expected_count = sample_count
                self.trace_sample_hz = sample_hz
                self.record_status_var.set(f"读取高速数据: 0/{sample_count}")

    def _update_pid_window_from_ack(self, event: MotorAckEvent) -> None:
        """
        @brief 根据 PID 成功应答回填配置窗口。
        @param event 电机应答事件。
        @return None
        """
        if self.pid_window is None or not self.pid_window.winfo_exists():
            return

        try:
            selected_node_id = self._selected_node_id()
        except ValueError:
            return

        if event.node_id != selected_node_id or len(event.payload) < 7:
            return

        if event.command in (MOTOR_CMD_SET_SPEED_PID_PARAM,
                             MOTOR_CMD_READ_SPEED_PID_PARAM):
            parameter_vars = self.speed_pid_vars
        elif event.command in (MOTOR_CMD_SET_POSITION_PID_PARAM,
                               MOTOR_CMD_READ_POSITION_PID_PARAM):
            parameter_vars = self.position_pid_vars
        else:
            return

        param_id = event.payload[2]

        if param_id not in parameter_vars:
            return

        value = float32_from_le(event.payload[3:7])
        parameter_vars[param_id].set(self._format_pid_value(value))

    def _format_pid_value(self, value: float) -> str:
        """
        @brief 将 PID 单精度浮点值格式化为界面文本。
        @param value PID 单精度浮点参数值。
        @return str PID 参数显示文本。
        """
        return f"{value:.9g}"

    def _pid_ack_detail(self, event: MotorAckEvent) -> str:
        """
        @brief 生成 PID 参数应答日志详情。
        @param event 电机应答事件。
        @return str PID 参数详情文本，非 PID 应答时为空。
        """
        pid_commands = (
            MOTOR_CMD_SET_SPEED_PID_PARAM,
            MOTOR_CMD_READ_SPEED_PID_PARAM,
            MOTOR_CMD_SET_POSITION_PID_PARAM,
            MOTOR_CMD_READ_POSITION_PID_PARAM,
        )

        if event.command not in pid_commands or len(event.payload) < 7:
            return ""

        param_id = event.payload[2]
        value = float32_from_le(event.payload[3:7])
        param_name = next(
            (name for name, item_id in MOTOR_PID_PARAMETERS if item_id == param_id),
            f"0x{param_id:02X}",
        )
        return f" param={param_name} value={self._format_pid_value(value)}"

    def _handle_manage_report(self, event: ManageReportEvent) -> None:
        """
        @brief 处理未配置电机管理发现上报。
        @param event 管理发现上报事件。
        @return None
        """
        is_new_uid = event.uid32 not in self.manage_reports
        self.manage_reports[event.uid32] = event

        if self.new_uid_var.get().strip() == "" and event.configured == 0:
            self.new_uid_var.set(f"0x{event.uid32:08X}")

        if is_new_uid:
            self._append_log(
                f"rx manage report uid=0x{event.uid32:08X} "
                f"configured={event.configured} node=0x{event.node_id:02X}"
            )

    def _handle_manage_ack(self, event: ManageAckEvent) -> None:
        """
        @brief 处理未配置电机管理命令应答。
        @param event 管理命令应答事件。
        @return None
        """
        payload = event.payload
        detail = ""

        if event.command == MOTOR_CMD_MANAGE_IDENTIFY and len(payload) >= 8:
            detail = (
                f" hz={payload[2]} duration={payload[3]}s "
                f"uid=0x{uint32_from_le(payload[4:8]):08X}"
            )

        if event.command == MOTOR_CMD_MANAGE_SET_CAN_ID and len(payload) >= 7:
            uid32 = uint32_from_le(payload[3:7])
            node_id = payload[2]
            detail = f" node=0x{node_id:02X} uid=0x{uid32:08X}"

            if event.status == 0:
                self.new_uid_var.set(f"0x{uid32:08X}")
                self.new_node_var.set(node_label_from_id(node_id))
                self.node_var.set(node_label_from_id(node_id))
                self._continue_pending_new_motor_config(uid32, node_id)

        if event.command == MOTOR_CMD_MANAGE_SET_CAN_ID and event.status != 0:
            self.pending_new_config = None

        self._append_log(
            f"rx manage {command_text(event.command)} "
            f"status={status_text(event.status)}{detail}"
        )

    def _update_state_from_ack(self, state: MotorState, event: MotorAckEvent) -> None:
        """
        @brief 根据应答内容更新状态。
        @param state 电机状态对象。
        @param event 电机应答事件。
        @return None
        """
        data = event.payload

        if event.command == MOTOR_CMD_READ_APP_VERSION and len(data) >= 5:
            state.version = f"{data[2]}.{data[3]}.{data[4]}"
            return

        if event.command == MOTOR_CMD_READ_NODE_ID and len(data) >= 3:
            state.reported_node_id = data[2]
            return

        if event.command in (MOTOR_CMD_SET_REPORT_CONFIG, MOTOR_CMD_READ_REPORT_CONFIG):
            if len(data) >= 5:
                state.report_enabled = data[2] != 0
                state.report_period_ms = uint16_from_le(data[3:5])
                return

        if event.command in (MOTOR_CMD_ZERO_CALIBRATION, MOTOR_CMD_READ_ZERO):
            if len(data) >= 6:
                state.zero_mrad = int32_from_le(data[2:6])
                return

        if event.command in (MOTOR_CMD_SET_FOC_CONFIG, MOTOR_CMD_READ_FOC_CONFIG):
            if len(data) >= 7:
                param_id = data[2]
                param_value = uint32_from_le(data[3:7])

                if param_id == MOTOR_FOC_PARAM_POLE_PAIRS:
                    state.pole_pairs = param_value
                    return

                if param_id == MOTOR_FOC_PARAM_PHASE_MAP:
                    state.phase_map = param_value
                    return

    def _refresh_motor_table(self) -> None:
        """
        @brief 刷新电机状态表格。
        @return None
        """
        current_ms = now_monotonic_ms()

        for node_id in sorted(self.motor_states.keys()):
            state = self.motor_states[node_id]
            self._ensure_table_item(node_id)

            if state.last_report_ms is None:
                age_text = "--"
            else:
                age_text = str(current_ms - state.last_report_ms)

            ack = f"{command_text(state.last_ack_cmd)} {status_text(state.last_ack_status)}"
            self.motor_table.item(str(node_id),
                                  text=state.name,
                                  values=(f"0x{node_id:02X}",
                                          format_optional_int(state.position_mrad),
                                          mrad_to_degree(state.position_mrad),
                                          format_optional_int(state.speed_mrad_s),
                                          age_text,
                                          str(state.report_count),
                                          state.version,
                                          format_optional_int(state.zero_mrad),
                                          foc_config_text(state.pole_pairs,
                                                          state.phase_map),
                                          report_config_text(state.report_enabled,
                                                             state.report_period_ms),
                                          ack,
                                          str(state.tx_count)))

        self.root.after(100, self._refresh_motor_table)

    def _append_chart_sample(self, event: MotorReportEvent) -> None:
        """
        @brief 追加一条电机曲线采样。
        @param event 电机主动上报事件。
        @return None
        """
        history = self.chart_history.get(event.node_id)

        if history is None:
            history = deque()
            self.chart_history[event.node_id] = history

        history.append((event.timestamp_ms, event.position_mrad, event.speed_mrad_s))

        cutoff_ms = event.timestamp_ms - int(CHART_MAX_WINDOW_SECONDS * 1000.0)

        while len(history) > 0 and history[0][0] < cutoff_ms:
            history.popleft()

    def _selected_chart_node_id(self) -> int:
        """
        @brief 获取曲线当前选中的电机节点 ID。
        @return int 电机节点 ID。
        """
        try:
            return self._selected_node_id()
        except Exception:
            return 0x01

    def _get_chart_window_ms(self) -> int:
        """
        @brief 获取曲线显示窗口长度。
        @return int 曲线显示窗口长度，单位 ms。
        """
        try:
            window_seconds = float(self.chart_window_var.get().strip())
        except ValueError:
            window_seconds = CHART_HISTORY_SECONDS

        if not math.isfinite(window_seconds):
            window_seconds = CHART_HISTORY_SECONDS

        if window_seconds < CHART_MIN_WINDOW_SECONDS:
            window_seconds = CHART_MIN_WINDOW_SECONDS

        if window_seconds > CHART_MAX_WINDOW_SECONDS:
            window_seconds = CHART_MAX_WINDOW_SECONDS

        return int(window_seconds * 1000.0)

    def _decimate_chart_history(self,
                                history: list[tuple[int, int, int]]) -> list[tuple[int, int, int]]:
        """
        @brief 抽样曲线历史数据。
        @param history 曲线历史数据。
        @return list[tuple[int, int, int]] 抽样后的曲线历史数据。
        """
        if len(history) <= CHART_MAX_DRAW_POINTS:
            return history

        bucket_count = max(CHART_MAX_DRAW_POINTS // 5, 1)
        step = math.ceil(len(history) / bucket_count)
        decimated_history: list[tuple[int, int, int]] = []

        for start_index in range(0, len(history), step):
            bucket = history[start_index:start_index + step]
            selected_indices = {
                0,
                len(bucket) - 1,
            }
            selected_indices.add(min(range(len(bucket)), key=lambda index: bucket[index][1]))
            selected_indices.add(max(range(len(bucket)), key=lambda index: bucket[index][1]))
            selected_indices.add(min(range(len(bucket)), key=lambda index: bucket[index][2]))
            selected_indices.add(max(range(len(bucket)), key=lambda index: bucket[index][2]))

            for bucket_index in sorted(selected_indices):
                decimated_history.append(bucket[bucket_index])

        return decimated_history

    def _refresh_chart(self) -> None:
        """
        @brief 刷新当前电机位置和速度曲线。
        @return None
        """
        self._draw_chart()
        self.root.after(CHART_REFRESH_MS, self._refresh_chart)

    def _draw_chart(self) -> None:
        """
        @brief 绘制当前电机位置和速度曲线。
        @return None
        """
        node_id = self._selected_chart_node_id()
        state = self.motor_states.get(node_id)
        motor_name = node_label_from_id(node_id)

        if state is not None:
            motor_name = state.name

        history = self.chart_history.get(node_id, deque())
        canvas = self.chart_canvas
        width = max(canvas.winfo_width(), 400)
        height = max(canvas.winfo_height(), 160)
        left = 58
        right = width - 18
        top = 18
        bottom = height - 32
        mid_y = (top + bottom) // 2
        current_ms = now_monotonic_ms()
        window_ms = self._get_chart_window_ms()

        canvas.delete("all")
        canvas.create_rectangle(0, 0, width, height, fill="#111827", outline="")
        canvas.create_text(left,
                           8,
                           text=f"{motor_name} position/speed {window_ms / 1000.0:g}s",
                           fill="#e5e7eb",
                           anchor="nw")

        self._draw_chart_grid(canvas, left, right, top, bottom, mid_y)

        recent_history = [
            sample
            for sample in history
            if sample[0] >= current_ms - window_ms
        ]

        if len(recent_history) < 2:
            canvas.create_text(width // 2,
                               height // 2,
                               text="waiting motor report...",
                               fill="#9ca3af")
            return

        draw_history = self._decimate_chart_history(recent_history)

        self._draw_chart_series(canvas,
                                draw_history,
                                value_index=1,
                                color="#38bdf8",
                                left=left,
                                right=right,
                                top=top,
                                bottom=mid_y - 8)
        self._draw_chart_series(canvas,
                                draw_history,
                                value_index=2,
                                color="#f97316",
                                left=left,
                                right=right,
                                top=mid_y + 8,
                                bottom=bottom)

        last_sample = recent_history[-1]
        canvas.create_text(8,
                           top,
                           text=f"pos\n{last_sample[1]}",
                           fill="#38bdf8",
                           anchor="nw")
        canvas.create_text(8,
                           mid_y + 8,
                           text=f"spd\n{last_sample[2]}",
                           fill="#f97316",
                           anchor="nw")

    def _draw_chart_grid(self,
                         canvas: tk.Canvas,
                         left: int,
                         right: int,
                         top: int,
                         bottom: int,
                         mid_y: int) -> None:
        """
        @brief 绘制曲线网格。
        @param canvas 目标画布。
        @param left 绘图区左边界。
        @param right 绘图区右边界。
        @param top 绘图区上边界。
        @param bottom 绘图区下边界。
        @param mid_y 中线坐标。
        @return None
        """
        for index in range(6):
            x = left + ((right - left) * index) // 5
            canvas.create_line(x, top, x, bottom, fill="#1f2937")

        canvas.create_line(left, top, right, top, fill="#374151")
        canvas.create_line(left, mid_y, right, mid_y, fill="#374151")
        canvas.create_line(left, bottom, right, bottom, fill="#374151")

    def _draw_chart_series(self,
                           canvas: tk.Canvas,
                           history: list[tuple[int, int, int]],
                           value_index: int,
                           color: str,
                           left: int,
                           right: int,
                           top: int,
                           bottom: int) -> None:
        """
        @brief 绘制一条曲线。
        @param canvas 目标画布。
        @param history 曲线历史数据。
        @param value_index 数值字段下标。
        @param color 曲线颜色。
        @param left 绘图区左边界。
        @param right 绘图区右边界。
        @param top 绘图区上边界。
        @param bottom 绘图区下边界。
        @return None
        """
        values = [sample[value_index] for sample in history]
        min_value = min(values)
        max_value = max(values)
        span = max(max_value - min_value, 1)
        points: list[float] = []
        first_ms = history[0][0]
        last_ms = history[-1][0]
        window_ms = max(last_ms - first_ms, 1)

        for sample in history:
            x = left + ((right - left) * (sample[0] - first_ms) / window_ms)
            y = bottom - ((sample[value_index] - min_value) * (bottom - top) / span)
            points.extend([x, y])

        if len(points) >= 4:
            canvas.create_line(*points, fill=color, width=2)

        canvas.create_text(right,
                           top,
                           text=str(max_value),
                           fill=color,
                           anchor="ne")
        canvas.create_text(right,
                           bottom,
                           text=str(min_value),
                           fill=color,
                           anchor="se")

    def _append_log(self, text: str) -> None:
        """
        @brief 向日志窗口追加一行文本。
        @param text 日志文本。
        @return None
        """
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.configure(state="normal")
        self.log_text.insert("end", f"[{timestamp}] {text}\n")
        self._trim_log_text()
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _trim_log_text(self) -> None:
        """
        @brief 裁剪日志窗口行数。
        @return None
        """
        line_count = int(float(self.log_text.index("end-1c").split(".")[0]))

        if line_count <= LOG_MAX_LINES:
            return

        delete_end_line = line_count - LOG_MAX_LINES + 1
        self.log_text.delete("1.0", f"{delete_end_line}.0")

    def _on_close(self) -> None:
        """
        @brief 处理窗口关闭事件。
        @return None
        """
        self.client.disconnect()
        self.root.destroy()


def parse_args() -> argparse.Namespace:
    """
    @brief 解析命令行参数。
    @return argparse.Namespace 命令行参数。
    """
    parser = argparse.ArgumentParser(description="Duck Mid PCAN motor tool")
    parser.add_argument("--channel",
                        default=PCAN_DEFAULT_CHANNEL_NAME,
                        help="PCAN channel name or hex value, default: PCAN_USBBUS1")
    parser.add_argument("--bitrate",
                        default="1M",
                        choices=list(PCAN_BITRATES.keys()),
                        help="CAN bitrate, default: 1M")

    return parser.parse_args()


def main() -> None:
    """
    @brief 程序入口。
    @return None
    """
    args = parse_args()
    root = tk.Tk()
    MotorToolApp(root,
                 default_channel=channel_value_from_label(args.channel),
                 default_bitrate=args.bitrate)
    root.mainloop()


if __name__ == "__main__":
    main()

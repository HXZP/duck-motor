# -*- coding: utf-8 -*-
"""通过 PCAN 执行 motor_duck CAN OTA。"""

from __future__ import annotations

import argparse
import math
import os
import sys
import time
from dataclasses import dataclass
from pathlib import Path

try:
    from pcan_basic import (
        CanFrame,
        PcanBasic,
        PcanError,
        format_channels,
        format_data,
        parse_channel,
    )
    import ymodem
except ModuleNotFoundError:
    from scripts.pcan_basic import (
        CanFrame,
        PcanBasic,
        PcanError,
        format_channels,
        format_data,
        parse_channel,
    )
    from scripts import ymodem


OTA_REQUEST_PREFIX = bytes([0x7F, 0x5A, 0xA5])
DEFAULT_NODE_ID = 0x7E
DEFAULT_OTA_REQUEST_TARGET = 0x0A
DEFAULT_FRAME_DELAY_MS = 1.0
DEFAULT_APP_BITRATE = "1m"
DEFAULT_OTA_BITRATE = "1m"
DEFAULT_BITRATE = DEFAULT_OTA_BITRATE
DEFAULT_QUIESCE_NODE_IDS = tuple(
    node_id
    for node_id in range(0x01, 0x80)
    if node_id != DEFAULT_NODE_ID
)


@dataclass(frozen=True)
class OtaConfig:
    """OTA 测试配置。"""

    firmware_path: Path
    channel: int | None
    app_bitrate: str
    ota_bitrate: str
    node_id: int
    request_target: int
    frame_delay_ms: float
    start_timeout_s: float
    packet_timeout_s: float
    post_timeout_s: float
    cancel_first: bool
    post_check: bool
    allow_unconfigured: bool
    quiesce_reports: bool
    verbose: bool
    dll_path: Path | None


class PcanOtaClient:
    """PCAN OTA 执行器。"""

    def __init__(self, config: OtaConfig):
        """
        @brief 初始化 PCAN OTA 执行器。
        @param config OTA 测试配置。
        """

        self.config = config
        self.pcan = PcanBasic(config.dll_path)
        self.channel = 0
        self.current_bitrate = ""
        self.command_id = 0x100 + config.node_id
        self.response_id = 0x180 + config.node_id
        self.ota_control_id = 0x400 + config.node_id
        self.ota_response_id = 0x500 + config.node_id
        self.manage_report_id = 0x200 + DEFAULT_NODE_ID

    def log(self, message: str) -> None:
        """打印日志。

        Args:
            message: 日志字符串。
        """

        print(message, flush=True)

    def debug(self, message: str) -> None:
        """打印详细日志。

        Args:
            message: 日志字符串。
        """

        if self.config.verbose:
            self.log(message)

    def open(self) -> None:
        """
        @brief 按 App 通信波特率打开 PCAN 通道。
        @return void
        """

        self.pcan.uninitialize()
        if self.config.channel is None:
            channels = self.pcan.attached_channels()
            for line in format_channels(channels):
                self.debug(f"PCAN channel: {line}")
            self.channel = self.pcan.select_available_channel()
        else:
            self.channel = self.config.channel

        self.pcan.initialize(self.channel, self.config.app_bitrate)
        self.current_bitrate = self.config.app_bitrate
        self.log(
            f"PCAN 已打开: channel=0x{self.channel:02X} "
            f"bitrate={self.config.app_bitrate}"
        )

    def close(self) -> None:
        """
        @brief 关闭 PCAN 通道。
        @return void
        """

        if self.channel != 0:
            self.pcan.uninitialize(self.channel)
            self.current_bitrate = ""
            self.log("PCAN 已关闭")

    def switch_bitrate(self, bitrate: str, label: str) -> None:
        """
        @brief 切换当前 PCAN 通道波特率。
        @param bitrate 目标 CAN 波特率字符串。
        @param label 切换阶段说明。
        @return void
        """

        if self.current_bitrate == bitrate:
            return

        if self.channel == 0:
            raise RuntimeError("PCAN 通道尚未打开，无法切换波特率")

        self.pcan.uninitialize(self.channel)
        self.pcan.initialize(self.channel, bitrate)
        self.current_bitrate = bitrate
        self.log(f"PCAN 波特率切换: {label}, bitrate={bitrate}")

    def drain(self) -> int:
        """清空 PCAN 接收队列。

        Returns:
            清空帧数，单位：帧。
        """

        drained = self.pcan.drain(self.channel)
        self.debug(f"清空 PCAN 接收队列: {drained} 帧")
        return drained

    def write_frame(self, std_id: int, data: bytes) -> None:
        """发送一帧 CAN 数据。

        Args:
            std_id: 标准帧 ID，单位：无。
            data: CAN 数据。
        """

        deadline = time.time() + 2.0
        while True:
            try:
                self.pcan.write(self.channel, CanFrame(std_id=std_id, data=data))
                return
            except PcanError:
                if time.time() >= deadline:
                    raise
                time.sleep(0.002)

    def read_frame(self, timeout_s: float) -> CanFrame | None:
        """在限定时间内读取一帧 CAN 数据。

        Args:
            timeout_s: 超时时间，单位：秒。

        Returns:
            CAN 帧，超时返回 None。
        """

        deadline = time.time() + timeout_s
        while time.time() < deadline:
            frame = self.pcan.read(self.channel)
            if frame is not None:
                return frame
            time.sleep(0.002)

        return None

    def wait_response_byte(self, expected: int, timeout_s: float, label: str) -> None:
        """等待 Boot OTA 响应中的指定控制字节。

        Args:
            expected: 期望字节，单位：无。
            timeout_s: 超时时间，单位：秒。
            label: 等待阶段名称。
        """

        deadline = time.time() + timeout_s
        recent_frames: list[CanFrame] = []
        while time.time() < deadline:
            frame = self.read_frame(min(0.2, max(0.0, deadline - time.time())))
            if frame is None:
                continue

            recent_frames.append(frame)
            if len(recent_frames) > 12:
                recent_frames.pop(0)

            if frame.std_id in (
                self.ota_response_id,
                self.manage_report_id,
                0x180 + DEFAULT_NODE_ID,
            ):
                self.log(f"RX 0x{frame.std_id:03X}: {format_data(frame.data)}")
            else:
                self.debug(f"RX 0x{frame.std_id:03X}: {format_data(frame.data)}")

            if frame.std_id != self.ota_response_id:
                continue

            if expected in frame.data:
                return

        detail = ", ".join(
            f"0x{frame.std_id:03X}:{format_data(frame.data)}"
            for frame in recent_frames
        )
        raise TimeoutError(
            f"等待 {label} 超时，未收到 0x{expected:02X}，最近帧: {detail}"
        )

    def send_stream(self, data: bytes) -> None:
        """发送字节流到 OTA CAN 控制帧。

        Args:
            data: 待发送字节流。
        """

        delay_s = self.config.frame_delay_ms / 1000.0
        for offset in range(0, len(data), 8):
            self.write_frame(self.ota_control_id, data[offset : offset + 8])
            time.sleep(delay_s)

    def send_ota_request(self) -> None:
        """发送 OTA 入口请求。"""

        request = OTA_REQUEST_PREFIX + bytes([self.config.request_target & 0xFF])
        self.write_frame(self.ota_control_id, request)
        self.log(
            f"发送 OTA 请求: id=0x{self.ota_control_id:03X} "
            f"data={format_data(request)}"
        )

    def wait_app_ota_ack(self, timeout_s: float) -> bool:
        """
        @brief 等待 App 对进入 Boot OTA 命令的应答。
        @param timeout_s 超时时间，单位：秒。
        @return bool 收到成功应答返回 True，否则返回 False。
        """

        deadline = time.time() + timeout_s
        while time.time() < deadline:
            frame = self.read_frame(min(0.1, max(0.0, deadline - time.time())))
            if frame is None:
                continue

            if frame.std_id in (
                self.response_id,
                self.manage_report_id,
                0x180 + DEFAULT_NODE_ID,
            ):
                self.log(f"APP RX 0x{frame.std_id:03X}: {format_data(frame.data)}")
            else:
                self.debug(f"APP RX 0x{frame.std_id:03X}: {format_data(frame.data)}")

            if frame.std_id != self.response_id:
                continue

            if len(frame.data) < 2:
                continue

            if frame.data[0] != 0x40:
                continue

            return frame.data[1] == 0x00

        return False

    def send_report_config(self, node_id: int, enabled: bool, period_ms: int) -> None:
        """
        @brief 发送 App 主动上报配置命令。
        @param node_id 目标节点 ID，单位：无。
        @param enabled 主动上报使能标志。
        @param period_ms 主动上报周期，单位：毫秒。
        @return void
        """

        data = bytes(
            [
                0x23,
                0x01 if enabled else 0x00,
                period_ms & 0xFF,
                (period_ms >> 8) & 0xFF,
                0x00,
                0x00,
                0x00,
                0x00,
            ]
        )
        self.write_frame(0x100 + node_id, data)

    def quiesce_motor_reports(self) -> None:
        """
        @brief 关闭全部合法业务节点主动上报，降低 OTA 期间总线占用。
        @return void
        """

        if not self.config.quiesce_reports:
            return

        if self.config.app_bitrate != self.config.ota_bitrate:
            self.switch_bitrate(self.config.app_bitrate, "App 上报静默阶段")

        self.drain()
        for node_id in DEFAULT_QUIESCE_NODE_IDS:
            self.send_report_config(node_id, False, 10)
            time.sleep(0.001)

        ack_deadline = time.time() + 0.5
        ack_count = 0
        while time.time() < ack_deadline:
            frame = self.read_frame(min(0.05, max(0.0, ack_deadline - time.time())))
            if frame is None:
                continue

            if ((0x180 < frame.std_id < 0x200)
                and (frame.std_id != 0x1FE)
                and (len(frame.data) >= 2)):
                if (frame.data[0] == 0x23) and (frame.data[1] == 0x00):
                    ack_count += 1
                    self.debug(
                        f"已关闭节点 0x{frame.std_id - 0x180:02X} 主动上报"
                    )

        self.log(f"OTA 前主动上报静默完成: ack={ack_count}")

    def request_boot_ota_from_app(self) -> bool:
        """
        @brief 使用 App 通信波特率请求设备进入 Boot OTA。
        @return bool 收到 App 成功应答返回 True，否则返回 False。
        """

        self.switch_bitrate(self.config.app_bitrate, "App OTA 入口阶段")
        self.drain()
        self.send_ota_request()
        if self.config.app_bitrate == self.config.ota_bitrate:
            return False

        if self.wait_app_ota_ack(1.5):
            self.log("App 已确认进入 Boot OTA")
            return True

        self.log("未收到 App OTA 应答，继续切到 Boot 波特率等待")
        return False

    def send_cancel(self) -> None:
        """发送 YMODEM 取消序列。"""

        self.write_frame(self.ota_control_id, bytes([ymodem.CA, ymodem.CA]))
        self.debug("已发送 YMODEM 取消序列")

    def wait_app_report(self) -> int:
        """等待 App 管理发现上报。

        Returns:
            收到的管理上报数量，单位：帧。
        """

        deadline = time.time() + self.config.post_timeout_s
        report_count = 0
        while time.time() < deadline:
            frame = self.read_frame(min(0.2, max(0.0, deadline - time.time())))
            if frame is None:
                continue

            self.log(f"POST RX 0x{frame.std_id:03X}: {format_data(frame.data)}")
            if frame.std_id == self.manage_report_id:
                report_count += 1
                if report_count >= 2:
                    return report_count

        return report_count

    def wait_app_version(self) -> tuple[int, int, int]:
        """等待已配置 App 节点返回版本号。
        Returns:
            App 版本号三元组，单位：无。
        """

        deadline = time.time() + self.config.post_timeout_s
        next_request_time = 0.0
        request = bytes([0x22, 0, 0, 0, 0, 0, 0, 0])

        while time.time() < deadline:
            now = time.time()
            if now >= next_request_time:
                self.write_frame(self.command_id, request)
                self.log(
                    f"POST TX 0x{self.command_id:03X}: "
                    f"{format_data(request)}"
                )
                next_request_time = now + 0.5

            frame = self.read_frame(min(0.2, max(0.0, deadline - time.time())))
            if frame is None:
                continue

            self.log(f"POST RX 0x{frame.std_id:03X}: {format_data(frame.data)}")
            if frame.std_id != self.response_id:
                continue

            if len(frame.data) < 5:
                continue

            if frame.data[0] != 0x22:
                continue

            if frame.data[1] != 0x00:
                raise RuntimeError(f"App 版本读取失败: status=0x{frame.data[1]:02X}")

            return frame.data[2], frame.data[3], frame.data[4]

        raise TimeoutError("OTA 完成后未收到 App 版本响应")

    def run_post_check(self) -> None:
        """执行 OTA 后 App 恢复检查。"""

        if self.config.node_id == DEFAULT_NODE_ID:
            report_count = self.wait_app_report()
            if report_count < 1:
                raise TimeoutError("OTA 完成后未收到 App 管理上报")
            self.log(f"OTA 后 App 管理上报数量: {report_count}")
            return

        version = self.wait_app_version()
        self.log(f"OTA 后 App 版本: {version[0]}.{version[1]}.{version[2]}")

    def run(self) -> int:
        """
        @brief 执行完整 OTA 流程。
        @return int 成功返回 0。
        """

        app_image = self.config.firmware_path.read_bytes()
        ota_payload = ymodem.build_boot_ota_payload(app_image)
        total_size = len(ota_payload)
        packet_count = math.ceil(total_size / ymodem.PACKET_1K_SIZE)
        file_name = self.config.firmware_path.name

        self.log(f"固件文件: {self.config.firmware_path}")
        self.log(
            f"App大小={len(app_image)} 字节, "
            f"YMODEM文件大小={total_size} 字节, "
            f"数据包={packet_count}, "
            f"帧间隔={self.config.frame_delay_ms:.2f} ms, "
            f"App波特率={self.config.app_bitrate}, "
            f"OTA波特率={self.config.ota_bitrate}"
        )

        self.open()
        try:
            self.drain()
            self.quiesce_motor_reports()
            if (
                self.config.cancel_first
                and (self.config.app_bitrate == self.config.ota_bitrate)
            ):
                self.send_cancel()
                time.sleep(0.3)
                self.drain()

            self.request_boot_ota_from_app()
            self.switch_bitrate(self.config.ota_bitrate, "Boot OTA 数据阶段")

            try:
                self.wait_response_byte(
                    ymodem.CRC_REQUEST,
                    self.config.start_timeout_s,
                    "Boot YMODEM 起始 C",
                )
            except TimeoutError:
                if self.config.app_bitrate == self.config.ota_bitrate:
                    raise

                self.log("未收到 Boot 起始 C，切回 App 波特率重新发送入口请求")
                self.request_boot_ota_from_app()
                self.switch_bitrate(self.config.ota_bitrate, "Boot OTA 数据阶段重试")
                self.wait_response_byte(
                    ymodem.CRC_REQUEST,
                    self.config.start_timeout_s,
                    "Boot YMODEM 起始 C",
                )

            self.log("发送 YMODEM 文件头")
            self.send_stream(ymodem.build_header_packet(file_name, total_size))
            self.wait_response_byte(ymodem.ACK, self.config.packet_timeout_s, "文件头 ACK")
            self.wait_response_byte(ymodem.CRC_REQUEST, self.config.packet_timeout_s, "文件头 C")

            for packet_index in range(packet_count):
                sequence = packet_index + 1
                payload = ota_payload[
                    packet_index * ymodem.PACKET_1K_SIZE : (packet_index + 1)
                    * ymodem.PACKET_1K_SIZE
                ]
                self.send_stream(
                    ymodem.build_packet(sequence, payload, ymodem.PACKET_1K_SIZE)
                )
                self.wait_response_byte(
                    ymodem.ACK,
                    self.config.packet_timeout_s,
                    f"数据包 {sequence} ACK",
                )

                if (
                    sequence == 1
                    or sequence == packet_count
                    or (sequence % 4) == 0
                ):
                    sent = min(sequence * ymodem.PACKET_1K_SIZE, total_size)
                    self.log(f"数据包 {sequence}/{packet_count} 已确认: {sent}/{total_size}")

            self.log("发送第一次 EOT")
            self.write_frame(self.ota_control_id, bytes([ymodem.EOT]))
            self.wait_response_byte(ymodem.NAK, self.config.packet_timeout_s, "第一次 EOT NAK")

            self.log("发送第二次 EOT")
            self.write_frame(self.ota_control_id, bytes([ymodem.EOT]))
            self.wait_response_byte(ymodem.ACK, self.config.packet_timeout_s, "第二次 EOT ACK")
            self.wait_response_byte(ymodem.CRC_REQUEST, self.config.packet_timeout_s, "结束空头 C")

            self.log("发送结束空头包")
            self.send_stream(ymodem.build_empty_header_packet())
            self.wait_response_byte(ymodem.ACK, self.config.packet_timeout_s, "结束空头 ACK")

            if self.config.post_check:
                self.switch_bitrate(self.config.app_bitrate, "App 回跳检查阶段")
                self.drain()
                self.run_post_check()

            self.log("OTA 完成")
            return 0
        finally:
            self.close()


def parse_int(value: str) -> int:
    """解析整数参数。

    Args:
        value: 输入字符串。

    Returns:
        解析后的整数，单位：无。
    """

    return int(value, 0)


def resolve_default_firmware() -> Path:
    """查找默认 OTA 固件文件。

    Returns:
        OTA 固件路径。
    """

    candidates = [
        Path("bazel-bin") / "ota_app_release.bin",
        Path("ota_app_release.bin"),
    ]

    runfiles_dir = os.environ.get("RUNFILES_DIR")
    if runfiles_dir:
        candidates.append(Path(runfiles_dir) / "motor_duck" / "ota_app_release.bin")

    for candidate in candidates:
        if candidate.exists():
            return candidate

    return candidates[0]


def parse_args(argv: list[str]) -> OtaConfig:
    """
    @brief 解析命令行参数。
    @param argv 命令行参数列表。
    @return OtaConfig OTA 测试配置。
    """

    parser = argparse.ArgumentParser(description="motor_duck PCAN OTA 工具")
    parser.add_argument(
        "--file",
        type=Path,
        default=None,
        help="OTA App 固件路径，默认使用 bazel-bin/ota_app_release.bin",
    )
    parser.add_argument("--channel", default="auto", help="PCAN 通道，例如 auto、usb1、0x51")
    parser.add_argument(
        "--app-bitrate",
        default=DEFAULT_APP_BITRATE,
        help="App 通信波特率，默认 1m",
    )
    parser.add_argument(
        "--ota-bitrate",
        default=None,
        help="Boot OTA 数据传输波特率，默认 1m",
    )
    parser.add_argument(
        "--bitrate",
        default=None,
        help="兼容旧参数，等同于 --ota-bitrate",
    )
    parser.add_argument("--node", type=parse_int, default=DEFAULT_NODE_ID, help="OTA 节点 ID")
    parser.add_argument(
        "--request-target",
        type=parse_int,
        default=DEFAULT_OTA_REQUEST_TARGET,
        help="OTA 请求目标字节，默认 0x0A 广播",
    )
    parser.add_argument(
        "--frame-delay-ms",
        type=float,
        default=DEFAULT_FRAME_DELAY_MS,
        help="CAN 分片帧间隔，单位：毫秒",
    )
    parser.add_argument("--start-timeout-s", type=float, default=8.0, help="启动超时，单位：秒")
    parser.add_argument("--packet-timeout-s", type=float, default=20.0, help="单包超时，单位：秒")
    parser.add_argument("--post-timeout-s", type=float, default=10.0, help="回跳检查超时，单位：秒")
    parser.add_argument("--dll", type=Path, default=None, help="PCANBasic.dll 路径")
    parser.add_argument("--no-cancel-first", action="store_true", help="启动前不发送取消序列")
    parser.add_argument("--no-post-check", action="store_true", help="完成后不等待 App 管理上报")
    parser.add_argument("--allow-unconfigured", action="store_true", help="允许对未配置管理节点 0x7E 发起 OTA")
    parser.add_argument("--no-quiesce-reports", action="store_true", help="OTA 前不关闭其它节点主动上报")
    parser.add_argument("--verbose", action="store_true", help="打印详细 CAN 帧日志")
    parser.add_argument("--list-channels", action="store_true", help="列出 PCAN 通道后退出")

    args = parser.parse_args(argv)
    firmware_path = args.file if args.file is not None else resolve_default_firmware()
    ota_bitrate = args.ota_bitrate
    if ota_bitrate is None:
        ota_bitrate = args.bitrate

    if ota_bitrate is None:
        ota_bitrate = DEFAULT_OTA_BITRATE

    if args.list_channels:
        pcan = PcanBasic(args.dll)
        for line in format_channels(pcan.attached_channels()):
            print(line)
        raise SystemExit(0)

    if not firmware_path.exists():
        raise FileNotFoundError(f"OTA 固件不存在: {firmware_path}")

    if (args.node == DEFAULT_NODE_ID) and (not args.allow_unconfigured):
        raise ValueError(
            "默认禁止对未配置管理节点 0x7E 执行 OTA；"
            "请先通过 UID 配置业务 CAN ID 后使用 --node <业务ID>，"
            "单板调试旧固件时可显式添加 --allow-unconfigured"
        )

    return OtaConfig(
        firmware_path=firmware_path,
        channel=parse_channel(args.channel),
        app_bitrate=args.app_bitrate,
        ota_bitrate=ota_bitrate,
        node_id=args.node,
        request_target=args.request_target,
        frame_delay_ms=args.frame_delay_ms,
        start_timeout_s=args.start_timeout_s,
        packet_timeout_s=args.packet_timeout_s,
        post_timeout_s=args.post_timeout_s,
        cancel_first=not args.no_cancel_first,
        post_check=not args.no_post_check,
        allow_unconfigured=args.allow_unconfigured,
        quiesce_reports=not args.no_quiesce_reports,
        verbose=args.verbose,
        dll_path=args.dll,
    )


def main(argv: list[str]) -> int:
    """程序入口。

    Args:
        argv: 命令行参数列表。

    Returns:
        进程退出码。
    """

    try:
        config = parse_args(argv)
        return PcanOtaClient(config).run()
    except KeyboardInterrupt:
        print("用户中断", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"OTA 失败: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

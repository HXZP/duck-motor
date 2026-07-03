#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import time
from pathlib import Path


def main() -> int:
    """
    @brief 输出 Bazel workspace status，用于驱动固件报告动作每次构建都执行。
    @return 返回进程退出码，0 表示成功。
    """
    print("STABLE_MCU_REPORT_BUILD_ID " + str(time.time_ns()))
    print("STABLE_WORKSPACE_ROOT " + Path.cwd().as_posix())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

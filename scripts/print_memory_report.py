#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import re
import subprocess
from collections import OrderedDict
from pathlib import Path
from typing import Dict
from typing import List
from typing import Optional


def parse_size_token(token: str) -> int:
    """
    @brief 解析链接脚本中的地址或长度字符串。
    @param token 待解析的文本。
    @return 返回解析后的字节数。
    """
    text = token.strip()
    upper = text.upper()

    if upper.startswith("0X"):
        return int(upper, 16)

    multiplier = 1

    if upper.endswith("K"):
        multiplier = 1024
        upper = upper[:-1]
    elif upper.endswith("M"):
        multiplier = 1024 * 1024
        upper = upper[:-1]

    return int(upper, 10) * multiplier


def format_region_size(size: int) -> str:
    """
    @brief 将区域总大小格式化为可读文本。
    @param size 区域总字节数。
    @return 返回格式化后的文本。
    """
    if size % (1024 * 1024) == 0:
        return f"{size // (1024 * 1024)} MB"

    if size % 1024 == 0:
        return f"{size // 1024} KB"

    return f"{size} B"


def format_used_size(size: int) -> str:
    """
    @brief 将已使用大小格式化为文本。
    @param size 已使用字节数。
    @return 返回格式化后的文本。
    """
    return f"{size} B"


def parse_memory_regions(ld_path: Path) -> OrderedDict:
    """
    @brief 解析链接脚本中的 MEMORY 区域定义。
    @param ld_path 链接脚本路径。
    @return 返回区域信息有序字典。
    """
    content = ld_path.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r"MEMORY\s*\{(?P<body>.*?)\}", content, re.S)

    if match is None:
        raise ValueError(f"Failed to find MEMORY block in {ld_path}")

    regions = OrderedDict()
    pattern = re.compile(
        r"^\s*(?P<name>\w+)\s*\([^)]+\)\s*:\s*ORIGIN\s*=\s*(?P<origin>[^,]+),\s*LENGTH\s*=\s*(?P<length>[^\r\n]+)",
        re.M,
    )

    for item in pattern.finditer(match.group("body")):
        name = item.group("name")
        origin = parse_size_token(item.group("origin"))
        length = parse_size_token(item.group("length"))
        regions[name] = {
            "origin": origin,
            "length": length,
            "used_end": origin,
        }

    if not regions:
        raise ValueError(f"Failed to parse MEMORY regions from {ld_path}")

    return regions


def parse_objdump_sections(objdump_path: str, elf_path: Path) -> List[Dict[str, object]]:
    """
    @brief 解析 objdump -h 输出中的 section 信息。
    @param objdump_path objdump 程序路径。
    @param elf_path ELF 文件路径。
    @return 返回 section 信息列表。
    """
    result = subprocess.run(
        [objdump_path, "-h", str(elf_path)],
        check=True,
        text=True,
        capture_output=True,
    )

    lines = result.stdout.splitlines()
    sections = []
    index = 0
    header_pattern = re.compile(
        r"^\s*\d+\s+(?P<name>\S+)\s+(?P<size>[0-9A-Fa-f]+)\s+(?P<vma>[0-9A-Fa-f]+)\s+(?P<lma>[0-9A-Fa-f]+)\s+(?P<fileoff>[0-9A-Fa-f]+)\s+\S+"
    )

    while index < len(lines):
        line = lines[index]
        match = header_pattern.match(line)

        if match is None:
            index += 1
            continue

        flags_line = ""

        if index + 1 < len(lines):
            flags_line = lines[index + 1].strip()

        flags = [item.strip() for item in flags_line.split(",") if item.strip()]
        sections.append(
            {
                "name": match.group("name"),
                "size": int(match.group("size"), 16),
                "vma": int(match.group("vma"), 16),
                "lma": int(match.group("lma"), 16),
                "flags": flags,
            }
        )
        index += 2

    return sections


def find_region_name(regions: OrderedDict, address: int, size: int) -> Optional[str]:
    """
    @brief 根据地址范围查找所属的 MEMORY 区域。
    @param regions 区域信息字典。
    @param address 起始地址。
    @param size 区间大小。
    @return 返回区域名称，找不到时返回 None。
    """
    end = address + size

    for name, region in regions.items():
        origin = region["origin"]
        limit = origin + region["length"]

        if (address >= origin) and (end <= limit):
            return name

    return None


def account_span(regions: OrderedDict, region_name: Optional[str], address: int, size: int) -> None:
    """
    @brief 将一段地址范围累计到指定区域的使用量中。
    @param regions 区域信息字典。
    @param region_name 区域名称。
    @param address 起始地址。
    @param size 区间大小。
    @return 无。
    """
    if (region_name is None) or (size <= 0):
        return

    end = address + size

    if end > regions[region_name]["used_end"]:
        regions[region_name]["used_end"] = end


def accumulate_region_usage(regions: OrderedDict, sections: List[Dict[str, object]]) -> None:
    """
    @brief 根据 section 的 VMA/LMA 统计各区域使用量。
    @param regions 区域信息字典。
    @param sections section 信息列表。
    @return 无。
    """
    for section in sections:
        size = int(section["size"])
        flags = list(section["flags"])

        if (size <= 0) or ("ALLOC" not in flags):
            continue

        vma = int(section["vma"])
        lma = int(section["lma"])
        vma_region = find_region_name(regions, vma, size)
        account_span(regions, vma_region, vma, size)

        if ("LOAD" in flags) and (lma != vma):
            lma_region = find_region_name(regions, lma, size)

            if lma_region != vma_region:
                account_span(regions, lma_region, lma, size)


def make_report(regions: OrderedDict) -> str:
    """
    @brief 生成内存占用报告文本。
    @param regions 区域信息字典。
    @return 返回报告文本。
    """
    lines = ["Memory region         Used Size  Region Size  %age Used"]

    for name, region in regions.items():
        used = max(0, region["used_end"] - region["origin"])
        length = region["length"]
        percent = (used * 100.0 / length) if length != 0 else 0.0
        lines.append(
            f"{name:>16}: "
            f"{format_used_size(used):>12}  "
            f"{format_region_size(length):>11}  "
            f"{percent:>9.2f}%"
        )

    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    """
    @brief 解析命令行参数。
    @return 返回命令行参数对象。
    """
    parser = argparse.ArgumentParser(description="生成 MCU Flash/RAM 使用报告。")
    parser.add_argument("objdump", help="arm-none-eabi-objdump 路径。")
    parser.add_argument("elf", type=Path, help="ELF 文件路径。")
    parser.add_argument("ld", type=Path, help="链接脚本路径。")
    parser.add_argument("--output", type=Path, help="报告输出文件路径。")
    return parser.parse_args()


def main() -> int:
    """
    @brief 脚本入口。
    @return 返回进程退出码。
    """
    args = parse_args()
    regions = parse_memory_regions(args.ld)
    sections = parse_objdump_sections(args.objdump, args.elf)
    accumulate_region_usage(regions, sections)
    report = make_report(regions)

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    else:
        print(report, end="")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

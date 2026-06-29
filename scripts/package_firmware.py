#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
from typing import Dict
from typing import List


def read_version_define(text: str, name: str) -> int:
    """
    @brief 从头文件文本中读取版本宏。
    @param text 头文件文本。
    @param name 版本宏名称。
    @return 返回解析到的版本号。
    """
    pattern = r"#define\s+" + re.escape(name) + r"\s+([0-9]+)u?"
    match = re.search(pattern, text)

    if match is None:
        raise ValueError("找不到版本宏: " + name)

    return int(match.group(1), 10)


def read_app_version(version_header: Path) -> str:
    """
    @brief 从 app_version.h 读取 App 版本号。
    @param version_header App 版本头文件路径。
    @return 返回语义版本字符串。
    """
    text = version_header.read_text(encoding="utf-8")
    major = read_version_define(text, "APP_VERSION_MAJOR")
    minor = read_version_define(text, "APP_VERSION_MINOR")
    patch = read_version_define(text, "APP_VERSION_PATCH")
    return f"{major}.{minor}.{patch}"


def calc_sha256(path: Path) -> str:
    """
    @brief 计算文件 SHA256。
    @param path 文件路径。
    @return 返回 SHA256 十六进制字符串。
    """
    sha256 = hashlib.sha256()

    with path.open("rb") as file:
        while True:
            chunk = file.read(1024 * 1024)

            if not chunk:
                break

            sha256.update(chunk)

    return sha256.hexdigest()


def copy_artifact(package_dir: Path, package_name: str, version: str, artifact: List[str]) -> Dict[str, object]:
    """
    @brief 复制单个固件产物并生成清单条目。
    @param package_dir 发布包目录。
    @param package_name 发布包名称。
    @param version App 版本号。
    @param artifact 产物参数，格式为 role、source、output_template。
    @return 返回清单条目。
    """
    role = artifact[0]
    source = Path(artifact[1])
    output_template = artifact[2]

    if not source.is_file():
        raise FileNotFoundError("找不到固件产物: " + str(source))

    output_name = output_template.format(package=package_name, version=version)
    output_path = package_dir / output_name
    shutil.copyfile(source, output_path)

    return {
        "role": role,
        "file": output_name,
        "size": output_path.stat().st_size,
        "sha256": calc_sha256(output_path),
    }


def write_manifest(package_dir: Path, manifest: Dict[str, object]) -> None:
    """
    @brief 写入发布包清单。
    @param package_dir 发布包目录。
    @param manifest 清单数据。
    @return 无。
    """
    manifest_path = package_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def make_memory_report_title(role: str) -> str:
    """
    @brief 根据产物角色生成内存报告标题。
    @param role 产物角色名称。
    @return 返回报告标题。
    """
    if role == "boot_memory":
        return "BOOT MEMORY REPORT"

    if role == "app_memory":
        return "APP MEMORY REPORT"

    return role.upper() + " REPORT"


def print_memory_reports(artifacts: List[List[str]]) -> None:
    """
    @brief 在终端输出 boot 和 app 的内存占用报告。
    @param artifacts 产物参数列表，格式为 role、source、output_template。
    @return 无。
    """
    for artifact in artifacts:
        role = artifact[0]

        if role not in ["boot_memory", "app_memory"]:
            continue

        source = Path(artifact[1])

        if not source.is_file():
            raise FileNotFoundError("找不到内存报告: " + str(source))

        print("")
        print(make_memory_report_title(role))
        print(source.read_text(encoding="utf-8"), end="")


def parse_args() -> argparse.Namespace:
    """
    @brief 解析命令行参数。
    @return 返回命令行参数对象。
    """
    parser = argparse.ArgumentParser(description="打包固件发布目录。")
    parser.add_argument("--output-dir", required=True, type=Path, help="输出目录。")
    parser.add_argument("--version-header", required=True, type=Path, help="app_version.h 路径。")
    parser.add_argument("--package-name", required=True, help="发布包名称。")
    parser.add_argument("--build-type", required=True, help="构建类型。")
    parser.add_argument(
        "--artifact",
        action="append",
        nargs=3,
        metavar=("ROLE", "SOURCE", "OUTPUT_TEMPLATE"),
        default=[],
        help="固件产物，格式为角色、源路径、输出文件名模板。",
    )
    return parser.parse_args()


def main() -> int:
    """
    @brief 打包固件发布目录入口。
    @return 返回进程退出码，0 表示成功。
    """
    args = parse_args()
    version = read_app_version(args.version_header)
    package_dir = args.output_dir / f"{args.package_name}_v{version}"

    if args.output_dir.exists():
        shutil.rmtree(args.output_dir)

    package_dir.mkdir(parents=True, exist_ok=True)

    artifacts = []

    for artifact in args.artifact:
        artifacts.append(copy_artifact(package_dir, args.package_name, version, artifact))

    manifest = {
        "package": args.package_name,
        "version": version,
        "build_type": args.build_type,
        "directory": package_dir.name,
        "artifacts": artifacts,
    }
    write_manifest(package_dir, manifest)

    print("firmware package: " + str(package_dir))
    print("version: " + version)
    print("artifacts: " + str(len(artifacts)))
    print_memory_reports(args.artifact)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

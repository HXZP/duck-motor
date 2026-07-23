# motor_duck

## Bazel 构建

本工程保留原有 CMake 构建，同时提供参考 `acemate-mcu` 设计的 Bazel 构建入口。

```powershell
bazelisk build //stm32f103:motor
bazelisk build //boot:boot
bazelisk build //:full
```

常用入口是 `//:firmware`，它会一次生成 full、boot、app 和 OTA app 固件：

```powershell
bazelisk build //:firmware
```

release 版本主要输出：

```text
bazel-bin/full_release.bin
bazel-bin/boot/boot_release.bin
bazel-bin/boot/boot_release.memory.txt
bazel-bin/stm32f103/motor_release.bin
bazel-bin/stm32f103/motor_release.memory.txt
bazel-bin/ota_app_release.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/
```

Bazel 固件目标会同时生成两类占用报告：

```text
*.size.txt      arm-none-eabi-size 原始段大小报告
*.memory.txt    按链接脚本统计的 Flash/RAM 使用量和百分比
```

`bazel-bin/firmware_package` 目录会保存当前构建输出；工程根目录下的 `firmware_package/` 会同步保留各版本发布包，重新编译同一版本时只覆盖同名版本目录，不删除其它版本。发布包按 App 版本号命名，并包含带版本号的固件文件、内存报告、`manifest.json` 和 `readme.txt`：

```text
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_full_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_boot_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_boot_v1.0.0.memory.txt
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_app_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_app_v1.0.0.memory.txt
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_ota_app_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/manifest.json
bazel-bin/firmware_package/motor_duck_v1.0.0/readme.txt
firmware_package/motor_duck_v1.0.0/readme.txt
```

调试版本：

```powershell
bazelisk build //stm32f103:motor --config=debug
bazelisk build //boot:boot --config=debug
bazelisk build //:full --config=debug
```

调试版本也可以使用一条命令：

```powershell
bazelisk build //:firmware --config=debug
```

debug 版本主要输出：

```text
bazel-bin/full_debug.bin
bazel-bin/boot/boot_debug.bin
bazel-bin/boot/boot_debug.memory.txt
bazel-bin/stm32f103/motor_debug.bin
bazel-bin/stm32f103/motor_debug.memory.txt
bazel-bin/ota_app_debug.bin
```

如 arm-none-eabi 工具链不在默认路径，可通过 Bazel define 指定：

```powershell
bazelisk build //:firmware --define=ARM_NONE_EABI_BIN=C:/PROGRA~1/Arm/GNUTOO~1/bin
```

## clangd

VSCode 的 clangd 使用 Bazel 生成的编译数据库。首次打开工程或修改构建规则后，执行：

```powershell
bazelisk run //scripts:refresh_clangd
```

调试配置对应的编译数据库：

```powershell
bazelisk run --config=debug //scripts:refresh_clangd
```

生成文件位于 `.vscode/compile_commands.json`，该文件是本地刷新产物，不需要提交。

## Bazel 烧录

通过 J-Link 烧录 full 镜像，默认会从工程根目录 `firmware_package/` 中列出版本并交互选择：

```powershell
bazelisk run //scripts:flash_full
```

预览 J-Link Commander 脚本，不实际烧录：

```powershell
bazelisk run //scripts:flash_full -- --list-only
```

烧录根目录 `firmware_package/` 中保留的指定版本 full 镜像：

```powershell
bazelisk run //scripts:flash_full -- --version 1.0.1
bazelisk run //scripts:flash_full -- --version v1.0.1 --list-only
```

也可以直接指定任意 full bin 文件路径：

```powershell
bazelisk run //scripts:flash_full -- --image .\firmware_package\motor_duck_v1.0.1\motor_duck_full_v1.0.1.bin
```

如 J-Link 不在 PATH 中，可显式指定：

```powershell
bazelisk run //scripts:flash_full -- --flash-exe "C:\Program Files\SEGGER\JLink_V922\JLink.exe"
```

`flash_full` 默认只擦除 `0x08000000~0x0800FBFF`，保留从 `0x0800FC00` 开始的用户存储区。脚本会同时检查擦除范围和 full bin 写入范围，防止误覆盖用户存储页。

全片擦除会清除 `0x08000000~0x0800FFFF`，包括 Boot、App 和用户存储区：

```powershell
bazelisk run //scripts:erase_flash
```

预览全片擦除命令，不实际操作设备：

```powershell
bazelisk run //scripts:erase_flash -- --list-only
```

## PCAN OTA

先构建 release 固件：

```powershell
bazelisk build //:firmware
```

通过 PCAN-USB 执行 OTA，目标电机需要已经配置唯一业务 CAN ID。以下示例升级节点 `0x01`：

```powershell
python .\scripts\pcan_ota.py --file .\bazel-bin\ota_app_release.bin --node 0x01 --frame-delay-ms 1
```

也可以通过 Bazel 运行：

```powershell
bazelisk run //scripts:pcan_ota -- --file bazel-bin/ota_app_release.bin --node 0x01 --frame-delay-ms 1
```

常用参数：

```text
--channel auto          自动选择空闲 PCAN 通道
--app-bitrate 1m        App 通信波特率
--ota-bitrate 1m        Boot OTA 数据传输波特率
--bitrate 1m            兼容旧参数，等同于 --ota-bitrate
--node 0x01             目标电机业务节点 ID
--frame-delay-ms 1      OTA CAN 分片帧间隔，单位：毫秒
--no-post-check         OTA 完成后不检查 App 是否恢复响应
--allow-unconfigured    允许对 0x7E 发起 OTA，仅用于旧固件或单板调试
--list-channels         列出 PCAN 通道后退出
--verbose               打印详细 CAN 帧日志
```

脚本会先用 App 通信波特率发送 OTA 入口帧，进入 Boot 后使用 OTA 数据传输波特率，等待 Boot 返回 YMODEM `C`，然后传输 OTA app 镜像。默认 App 通信波特率和 Boot OTA 数据传输波特率均为 `1m`。未配置电机不允许 OTA，需要先通过管理发现和 UID 配置唯一业务 CAN ID。默认 post-check 会切回 App 通信波特率读取 App 版本号，确认 App 已恢复响应。

OTA app 镜像会由脚本自动追加 `128` 字节业务头，业务头包含 App 长度和 CRC16-CCITT。Boot 会在首个数据包校验业务头，业务头通过后才擦除 App 区；写入完成后再次读取 App Flash 计算 CRC16，校验通过后才清除 OTA 标志并跳转 App。当前 Boot 启动跳 App 前只检查向量表合法性，不做每次启动整包 CRC16 校验。

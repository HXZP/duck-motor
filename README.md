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
bazel-bin/stm32f103/motor_release.bin
bazel-bin/ota_app_release.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/
```

`firmware_package` 目录会按 App 版本号命名，并包含带版本号的固件文件和 `manifest.json`：

```text
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_full_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_boot_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_app_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/motor_duck_ota_app_v1.0.0.bin
bazel-bin/firmware_package/motor_duck_v1.0.0/manifest.json
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
bazel-bin/stm32f103/motor_debug.bin
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

通过 J-Link 烧录 full 镜像：

```powershell
bazelisk run //scripts:flash_full
```

预览 J-Link Commander 脚本，不实际烧录：

```powershell
bazelisk run //scripts:flash_full -- --list-only
```

如 J-Link 不在 PATH 中，可显式指定：

```powershell
bazelisk run //scripts:flash_full -- --flash-exe "C:\Program Files\SEGGER\JLink_V922\JLink.exe"
```

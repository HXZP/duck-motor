load("//bazel/mcu:mcu_rules.bzl", "McuFirmwareInfo")

def _selected_build_type(ctx):
    """
    @brief 根据 Bazel 编译模式选择 clangd 使用的构建类型。
    @param ctx Bazel 规则上下文。
    @return 返回 release 或 debug 构建类型。
    """
    if ctx.var.get("COMPILATION_MODE") == "dbg":
        return "debug"

    return "release"

def _mcu_compile_commands_impl(ctx):
    """
    @brief 暴露 MCU 固件目标生成的 compile_commands.json。
    @param ctx Bazel 规则上下文。
    @return 返回当前构建类型对应的编译数据库文件。
    """
    firmware_info = ctx.attr.firmware[McuFirmwareInfo]
    build_type = _selected_build_type(ctx)
    compile_commands = firmware_info.compile_commands[build_type]

    return [
        DefaultInfo(files = depset([compile_commands])),
    ]

mcu_compile_commands = rule(
    implementation = _mcu_compile_commands_impl,
    attrs = {
        "firmware": attr.label(providers = [McuFirmwareInfo], mandatory = True),
    },
)

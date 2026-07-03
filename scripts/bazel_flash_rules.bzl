def _selected_build_type(ctx):
    """
    @brief 根据 Bazel compilation_mode 选择镜像构建类型。
    @param ctx Bazel 规则上下文。
    @return 返回 debug 或 release 构建类型。
    """
    if ctx.var.get("COMPILATION_MODE") == "dbg":
        return "debug"

    return "release"

def _bazel_flash_target_impl(ctx):
    """
    @brief 生成 Windows 下的 Bazel 烧录运行入口。
    @param ctx Bazel 规则上下文。
    @return 返回可执行 bat 启动器。
    """
    build_type = _selected_build_type(ctx)
    image_name = ctx.attr.image_name + "_" + build_type + ".bin"
    image_file = None

    for data_file in ctx.files.data:
        if data_file.basename == image_name:
            image_file = data_file

    if image_file == None:
        fail("data must include " + image_name)

    launcher = ctx.actions.declare_file(ctx.label.name + ".bat")
    content = "\r\n".join([
        "@echo off",
        "setlocal",
        "set \"WORKSPACE=%BUILD_WORKSPACE_DIRECTORY%\"",
        "if \"%WORKSPACE%\"==\"\" set \"WORKSPACE=%cd%\"",
        (
            "\"" + ctx.attr.python + "\" "
            + "\"%WORKSPACE%\\scripts\\bazel_flash.py\" "
            + "--default-image \"" + image_file.path.replace("/", "\\") + "\" "
            + "--address " + ctx.attr.address + " "
            + "--device " + ctx.attr.device + " "
            + "--speed " + ctx.attr.speed + " "
            + "%*"
        ),
        "exit /b %ERRORLEVEL%",
        "",
    ])

    ctx.actions.write(
        output = launcher,
        content = content,
        is_executable = True,
    )

    return [
        DefaultInfo(
            executable = launcher,
            files = depset([launcher]),
            runfiles = ctx.runfiles(files = ctx.files.data),
        ),
    ]

bazel_flash_target = rule(
    implementation = _bazel_flash_target_impl,
    executable = True,
    attrs = {
        "image_name": attr.string(default = "full"),
        "address": attr.string(default = "0x08000000"),
        "device": attr.string(default = "STM32F103C8"),
        "speed": attr.string(default = "4000"),
        "python": attr.string(default = "python"),
        "data": attr.label_list(allow_files = True),
    },
)

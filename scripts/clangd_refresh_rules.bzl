def _windows_path(path):
    """
    @brief 将 Bazel 输出路径转换为 Windows 批处理可读路径。
    @param path Bazel 输出路径。
    @return 返回 Windows 风格路径。
    """
    return path.replace("/", "\\")

def _clangd_refresh_target_impl(ctx):
    """
    @brief 生成刷新 clangd 编译数据库的 Bazel 运行入口。
    @param ctx Bazel 规则上下文。
    @return 返回可执行 bat 启动器。
    """
    launcher = ctx.actions.declare_file(ctx.label.name + ".bat")
    input_args = []

    for compile_commands in ctx.files.compile_commands:
        input_args.append("--input \"" + _windows_path(compile_commands.path) + "\"")

    content = "\r\n".join([
        "@echo off",
        "setlocal",
        "set \"WORKSPACE=%BUILD_WORKSPACE_DIRECTORY%\"",
        "if \"%WORKSPACE%\"==\"\" set \"WORKSPACE=%cd%\"",
        (
            "\"" + ctx.attr.python + "\" "
            + "\"%WORKSPACE%\\scripts\\refresh_clangd.py\" "
            + " ".join(input_args) + " "
            + "--output \"%WORKSPACE%\\.vscode\\compile_commands.json\" "
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
            runfiles = ctx.runfiles(files = ctx.files.compile_commands + [ctx.file.refresh_tool]),
        ),
    ]

clangd_refresh_target = rule(
    implementation = _clangd_refresh_target_impl,
    executable = True,
    attrs = {
        "compile_commands": attr.label_list(allow_files = True, mandatory = True),
        "python": attr.string(default = "python"),
        "refresh_tool": attr.label(
            default = Label("//scripts:refresh_clangd.py"),
            allow_single_file = True,
        ),
    },
)

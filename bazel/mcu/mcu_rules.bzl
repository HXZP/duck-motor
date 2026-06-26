McuLibraryInfo = provider(
    doc = "MCU 固件库构建元数据。",
    fields = {
        "srcs": "源文件列表。",
        "hdrs": "头文件和辅助输入文件列表。",
        "includes": "传递给编译器的包含目录列表。",
        "defines": "传递给编译器的宏定义列表。",
        "copts": "当前库的附加编译参数列表。",
        "deps": "直接依赖库列表。",
    },
)

McuFirmwareInfo = provider(
    doc = "MCU 固件输出元数据。",
    fields = {
        "name": "固件工程逻辑名称。",
        "elf": "当前构建类型的 ELF 输出文件。",
        "bin": "当前构建类型的 BIN 输出文件。",
        "hex": "当前构建类型的 HEX 输出文件。",
        "map": "当前构建类型的 MAP 输出文件。",
        "size": "当前构建类型的 size 输出文件。",
        "artifacts": "按构建类型索引的固件输出文件。",
        "compile_commands": "按构建类型索引的 compile_commands.json 文件。",
    },
)

McuImageInfo = provider(
    doc = "MCU 派生镜像输出元数据。",
    fields = {
        "bin": "当前构建类型的 BIN 输出文件。",
        "artifacts": "按构建类型索引的镜像输出文件。",
    },
)

def _dedupe(items):
    """去除列表中的重复项并保持原始顺序。"""
    result = []
    seen = {}

    for item in items:
        key = str(item)
        if key not in seen:
            seen[key] = True
            result.append(item)

    return result

def _merge_library_infos(deps):
    """合并依赖库的 MCU 构建元数据。"""
    srcs = []
    hdrs = []
    includes = []
    defines = []
    copts = []

    for dep in deps:
        info = dep[McuLibraryInfo]
        srcs.extend(info.srcs)
        hdrs.extend(info.hdrs)
        includes.extend(info.includes)
        defines.extend(info.defines)
        copts.extend(info.copts)

    return {
        "srcs": _dedupe(srcs),
        "hdrs": _dedupe(hdrs),
        "includes": _dedupe(includes),
        "defines": _dedupe(defines),
        "copts": _dedupe(copts),
    }

def _mcu_library_impl(ctx):
    """收集一个 MCU 静态库目标的源文件和编译参数。"""
    dep_data = _merge_library_infos(ctx.attr.deps)

    return [
        McuLibraryInfo(
            srcs = _dedupe(ctx.files.srcs + dep_data["srcs"]),
            hdrs = _dedupe(ctx.files.hdrs + dep_data["hdrs"]),
            includes = _dedupe(ctx.attr.includes + dep_data["includes"]),
            defines = _dedupe(ctx.attr.defines + dep_data["defines"]),
            copts = _dedupe(ctx.attr.copts + dep_data["copts"]),
            deps = ctx.attr.deps,
        ),
    ]

mcu_library = rule(
    implementation = _mcu_library_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".c", ".cc", ".cpp", ".s", ".S"]),
        "hdrs": attr.label_list(allow_files = True),
        "includes": attr.string_list(),
        "defines": attr.string_list(),
        "copts": attr.string_list(),
        "deps": attr.label_list(providers = [McuLibraryInfo]),
    },
)

def _tool_bin_dir(ctx):
    """获取 arm-none-eabi 工具链目录。"""
    return ctx.var.get("ARM_NONE_EABI_BIN", "")

def _tool_path(ctx, tool_name):
    """生成工具链可执行文件路径。"""
    bin_dir = _tool_bin_dir(ctx)

    if bin_dir:
        if tool_name.endswith(".exe"):
            return bin_dir + "/" + tool_name

        return bin_dir + "/" + tool_name + ".exe"

    return tool_name

def _tool_env(ctx):
    """生成工具链运行环境变量。"""
    bin_dir = _tool_bin_dir(ctx)

    if not bin_dir:
        return {}

    return {
        "PATH": bin_dir + ";%PATH%",
    }

def _selected_build_type(ctx):
    """根据 Bazel compilation_mode 选择固件构建类型。"""
    if ctx.var.get("COMPILATION_MODE") == "dbg":
        return "debug"

    return "release"

def _make_common_flags(ctx, dep_copts, defines, includes, hdrs, build_type):
    """生成通用编译参数。"""
    common_flags = []
    common_flags.extend(ctx.attr.cpu_flags)
    common_flags.extend(ctx.attr.common_copts)
    common_flags.extend(dep_copts)
    common_flags.extend(ctx.attr.copts)

    if build_type == "debug":
        common_flags.extend(ctx.attr.debug_copts)
        for define in ctx.attr.debug_defines:
            common_flags.append("-D" + define)
    else:
        common_flags.extend(ctx.attr.release_copts)

    for define in defines:
        common_flags.append("-D" + define)

    for include in includes:
        common_flags.append("-I" + include)

    for header in hdrs:
        common_flags.append("-I" + header.dirname)

    return common_flags

def _make_compile_args(src, obj, flags):
    """生成单个源文件的编译参数。"""
    args = list(flags)

    if src.path.endswith(".s") or src.path.endswith(".S"):
        args.extend(["-x", "assembler-with-cpp"])
    else:
        args.extend(["-std=gnu11"])

    args.extend(["-c", src.path, "-o", obj.path])
    return args

def _compile_action(ctx, src, obj, flags, inputs):
    """注册单个源文件编译动作。"""
    ctx.actions.run(
        inputs = inputs,
        outputs = [obj],
        executable = _tool_path(ctx, ctx.attr.c_compiler),
        arguments = _make_compile_args(src, obj, flags),
        env = _tool_env(ctx),
        mnemonic = "McuCompile",
        progress_message = "Compiling MCU source %{input}",
    )

def _is_clangd_source(src):
    """判断源文件是否需要写入 clangd 编译数据库。"""
    path = src.path
    return path.endswith(".c") or path.endswith(".cc") or path.endswith(".cpp")

def _make_compile_command_entry(ctx, src, obj, flags):
    """生成 compile_commands.json 的单条记录。"""
    return {
        "directory": ".",
        "file": src.path,
        "output": obj.path,
        "arguments": [_tool_path(ctx, ctx.attr.c_compiler)] + _make_compile_args(src, obj, flags),
    }

def _write_compile_commands(ctx, build_type, entries):
    """写出单个构建类型的编译数据库文件。"""
    compile_commands = ctx.actions.declare_file(ctx.label.name + "_" + build_type + "_compile_commands.json")
    ctx.actions.write(
        output = compile_commands,
        content = json.encode_indent(entries) + "\n",
    )
    return compile_commands

def _build_firmware_variant(ctx, build_type, srcs, hdrs, defines, includes, dep_copts):
    """构建 debug 或 release 固件变体。"""
    common_flags = _make_common_flags(ctx, dep_copts, defines, includes, hdrs, build_type)
    compile_inputs = _dedupe(srcs + hdrs)
    objects = []
    compile_command_entries = []

    for src in srcs:
        obj_name = src.short_path.replace("/", "_").replace("\\", "_") + ".o"
        obj = ctx.actions.declare_file(ctx.label.name + "_" + build_type + "_objs/" + obj_name)
        _compile_action(ctx, src, obj, common_flags, compile_inputs)
        objects.append(obj)

        if _is_clangd_source(src):
            compile_command_entries.append(_make_compile_command_entry(ctx, src, obj, common_flags))

    elf = ctx.actions.declare_file(ctx.label.name + "_" + build_type + ".elf")
    map_file = ctx.actions.declare_file(ctx.label.name + "_" + build_type + ".map")
    link_args = []
    link_args.extend(ctx.attr.cpu_flags)
    link_args.extend([obj.path for obj in objects])
    link_args.extend(["-T", ctx.file.linker_script.path])
    link_args.extend(ctx.attr.linkopts)
    link_args.extend(["-Wl,-Map=" + map_file.path])
    link_args.extend(["-o", elf.path])

    ctx.actions.run(
        inputs = objects + [ctx.file.linker_script],
        outputs = [elf, map_file],
        executable = _tool_path(ctx, ctx.attr.linker),
        arguments = link_args,
        env = _tool_env(ctx),
        mnemonic = "McuLink",
        progress_message = "Linking MCU " + build_type + " firmware %{label}",
    )

    bin_file = ctx.actions.declare_file(ctx.label.name + "_" + build_type + ".bin")
    hex_file = ctx.actions.declare_file(ctx.label.name + "_" + build_type + ".hex")
    size_file = ctx.actions.declare_file(ctx.label.name + "_" + build_type + ".size.txt")

    ctx.actions.run(
        inputs = [elf],
        outputs = [bin_file],
        executable = _tool_path(ctx, ctx.attr.objcopy),
        arguments = ["-O", "binary", elf.path, bin_file.path],
        env = _tool_env(ctx),
        mnemonic = "McuObjcopyBin",
        progress_message = "Generating MCU " + build_type + " bin %{label}",
    )

    ctx.actions.run(
        inputs = [elf],
        outputs = [hex_file],
        executable = _tool_path(ctx, ctx.attr.objcopy),
        arguments = ["-O", "ihex", elf.path, hex_file.path],
        env = _tool_env(ctx),
        mnemonic = "McuObjcopyHex",
        progress_message = "Generating MCU " + build_type + " hex %{label}",
    )

    ctx.actions.run(
        inputs = [elf],
        outputs = [size_file],
        executable = ctx.executable.size_wrapper,
        arguments = [
            _tool_path(ctx, ctx.attr.size_tool),
            elf.path,
            size_file.path,
        ],
        env = _tool_env(ctx),
        mnemonic = "McuSize",
        progress_message = "Generating MCU " + build_type + " size report %{label}",
    )

    compile_commands = _write_compile_commands(ctx, build_type, compile_command_entries)

    return {
        "elf": elf,
        "bin": bin_file,
        "hex": hex_file,
        "map": map_file,
        "size": size_file,
        "compile_commands": compile_commands,
    }

def _mcu_firmware_impl(ctx):
    """构建 MCU 固件目标并导出当前构建类型产物。"""
    dep_data = _merge_library_infos(ctx.attr.deps)
    srcs = _dedupe(ctx.files.srcs + dep_data["srcs"])
    hdrs = _dedupe(ctx.files.hdrs + dep_data["hdrs"])
    includes = _dedupe(ctx.attr.includes + dep_data["includes"])
    defines = _dedupe(ctx.attr.defines + dep_data["defines"])
    dep_copts = _dedupe(dep_data["copts"])
    project_name = ctx.attr.project_name if ctx.attr.project_name else ctx.label.name
    release_artifacts = _build_firmware_variant(ctx, "release", srcs, hdrs, defines, includes, dep_copts)
    debug_artifacts = _build_firmware_variant(ctx, "debug", srcs, hdrs, defines, includes, dep_copts)
    artifacts = {
        "release": release_artifacts,
        "debug": debug_artifacts,
    }
    compile_commands = {
        "release": release_artifacts["compile_commands"],
        "debug": debug_artifacts["compile_commands"],
    }
    selected_artifacts = artifacts[_selected_build_type(ctx)]
    default_files = []

    for key in ["elf", "bin", "hex", "map", "size"]:
        default_files.append(selected_artifacts[key])

    return [
        DefaultInfo(files = depset(default_files)),
        McuFirmwareInfo(
            name = project_name,
            elf = selected_artifacts["elf"],
            bin = selected_artifacts["bin"],
            hex = selected_artifacts["hex"],
            map = selected_artifacts["map"],
            size = selected_artifacts["size"],
            artifacts = artifacts,
            compile_commands = compile_commands,
        ),
    ]

mcu_firmware = rule(
    implementation = _mcu_firmware_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".c", ".cc", ".cpp", ".s", ".S"]),
        "hdrs": attr.label_list(allow_files = True),
        "includes": attr.string_list(),
        "defines": attr.string_list(),
        "copts": attr.string_list(),
        "deps": attr.label_list(providers = [McuLibraryInfo]),
        "project_name": attr.string(),
        "linker_script": attr.label(allow_single_file = [".ld"], mandatory = True),
        "cpu_flags": attr.string_list(default = [
            "-mcpu=cortex-m3",
        ]),
        "common_copts": attr.string_list(default = [
            "-Wall",
            "-fdata-sections",
            "-ffunction-sections",
            "-fstack-usage",
        ]),
        "debug_copts": attr.string_list(default = ["-O0", "-g3"]),
        "release_copts": attr.string_list(default = ["-Os", "-g0"]),
        "debug_defines": attr.string_list(default = ["DEBUG"]),
        "linkopts": attr.string_list(default = [
            "--specs=nano.specs",
            "-Wl,--gc-sections",
            "-lm",
        ]),
        "c_compiler": attr.string(default = "arm-none-eabi-gcc"),
        "linker": attr.string(default = "arm-none-eabi-g++"),
        "objcopy": attr.string(default = "arm-none-eabi-objcopy"),
        "size_tool": attr.string(default = "arm-none-eabi-size"),
        "size_wrapper": attr.label(
            default = Label("//bazel/mcu:size_wrapper.bat"),
            executable = True,
            cfg = "exec",
            allow_single_file = True,
        ),
    },
)

def _mcu_full_image_impl(ctx):
    """合并 boot 与 app 固件为整片烧录镜像。"""
    boot_info = ctx.attr.boot[McuFirmwareInfo]
    app_info = ctx.attr.app[McuFirmwareInfo]
    build_type = _selected_build_type(ctx)
    boot_bin = boot_info.artifacts[build_type]["bin"]
    app_bin = app_info.artifacts[build_type]["bin"]
    out = ctx.actions.declare_file(ctx.label.name + "_" + build_type + ".bin")

    ctx.actions.run(
        inputs = [ctx.file.pack_tool, boot_bin, app_bin],
        outputs = [out],
        executable = ctx.attr.python,
        arguments = [
            ctx.file.pack_tool.path,
            "--boot",
            boot_bin.path,
            "--app",
            app_bin.path,
            "--output",
            out.path,
            "--app-offset",
            ctx.attr.app_offset,
            "--userinfo-offset",
            ctx.attr.userinfo_offset,
            "--boot-limit",
            ctx.attr.boot_limit,
        ],
        mnemonic = "McuFullImage",
        progress_message = "Packing full MCU image %{label}",
    )

    return [
        DefaultInfo(files = depset([out])),
        McuImageInfo(
            bin = out,
            artifacts = {
                build_type: out,
            },
        ),
    ]

mcu_full_image = rule(
    implementation = _mcu_full_image_impl,
    attrs = {
        "boot": attr.label(providers = [McuFirmwareInfo], mandatory = True),
        "app": attr.label(providers = [McuFirmwareInfo], mandatory = True),
        "app_offset": attr.string(default = "0x4400"),
        "userinfo_offset": attr.string(default = "0xFC00"),
        "boot_limit": attr.string(default = "0x4400"),
        "pack_tool": attr.label(
            default = Label("//scripts:pack.py"),
            allow_single_file = True,
        ),
        "python": attr.string(default = "python"),
    },
)

def _mcu_ota_app_image_impl(ctx):
    """
    @brief 生成 OTA 使用的 app 固件镜像副本。
    @param ctx Bazel 规则上下文。
    @return 返回 OTA app 固件输出文件。
    """
    app_info = ctx.attr.app[McuFirmwareInfo]
    build_type = _selected_build_type(ctx)
    app_bin = app_info.artifacts[build_type]["bin"]
    output_prefix = ctx.attr.output_prefix if ctx.attr.output_prefix else ctx.label.name
    out = ctx.actions.declare_file(output_prefix + "_" + build_type + ".bin")

    ctx.actions.run(
        inputs = [ctx.file.copy_tool, app_bin],
        outputs = [out],
        executable = ctx.attr.python,
        arguments = [
            ctx.file.copy_tool.path,
            "--input",
            app_bin.path,
            "--output",
            out.path,
        ],
        mnemonic = "McuOtaAppImage",
        progress_message = "Generating OTA app image %{label}",
    )

    return [
        DefaultInfo(files = depset([out])),
        McuImageInfo(
            bin = out,
            artifacts = {
                build_type: out,
            },
        ),
    ]

mcu_ota_app_image = rule(
    implementation = _mcu_ota_app_image_impl,
    attrs = {
        "app": attr.label(providers = [McuFirmwareInfo], mandatory = True),
        "output_prefix": attr.string(),
        "copy_tool": attr.label(
            default = Label("//scripts:copy_file.py"),
            allow_single_file = True,
        ),
        "python": attr.string(default = "python"),
    },
)

def _package_artifact_args(role, source_file, output_template):
    """
    @brief 生成固件发布包脚本的单个产物参数。
    @param role 产物角色名称。
    @param source_file 输入文件。
    @param output_template 输出文件名模板。
    @return 返回脚本参数列表。
    """
    return [
        "--artifact",
        role,
        source_file.path,
        output_template,
    ]

def _mcu_firmware_package_impl(ctx):
    """
    @brief 打包当前构建类型的固件发布目录。
    @param ctx Bazel 规则上下文。
    @return 返回发布目录输出。
    """
    build_type = _selected_build_type(ctx)
    boot_info = ctx.attr.boot[McuFirmwareInfo]
    app_info = ctx.attr.app[McuFirmwareInfo]
    full_info = ctx.attr.full[McuImageInfo]
    ota_app_info = ctx.attr.ota_app[McuImageInfo]
    boot_artifacts = boot_info.artifacts[build_type]
    app_artifacts = app_info.artifacts[build_type]
    full_bin = full_info.artifacts[build_type]
    ota_app_bin = ota_app_info.artifacts[build_type]
    output_dir = ctx.actions.declare_directory(ctx.label.name)
    package_name = ctx.attr.package_name
    inputs = [
        ctx.file.package_tool,
        ctx.file.version_header,
        full_bin,
        ota_app_bin,
        boot_artifacts["bin"],
        boot_artifacts["hex"],
        boot_artifacts["elf"],
        boot_artifacts["map"],
        boot_artifacts["size"],
        app_artifacts["bin"],
        app_artifacts["hex"],
        app_artifacts["elf"],
        app_artifacts["map"],
        app_artifacts["size"],
    ]
    arguments = [
        ctx.file.package_tool.path,
        "--output-dir",
        output_dir.path,
        "--version-header",
        ctx.file.version_header.path,
        "--package-name",
        package_name,
        "--build-type",
        build_type,
    ]

    arguments.extend(_package_artifact_args(
        "full_bin",
        full_bin,
        "{package}_full_v{version}.bin",
    ))
    arguments.extend(_package_artifact_args(
        "ota_app_bin",
        ota_app_bin,
        "{package}_ota_app_v{version}.bin",
    ))
    arguments.extend(_package_artifact_args(
        "boot_bin",
        boot_artifacts["bin"],
        "{package}_boot_v{version}.bin",
    ))
    arguments.extend(_package_artifact_args(
        "boot_hex",
        boot_artifacts["hex"],
        "{package}_boot_v{version}.hex",
    ))
    arguments.extend(_package_artifact_args(
        "boot_elf",
        boot_artifacts["elf"],
        "{package}_boot_v{version}.elf",
    ))
    arguments.extend(_package_artifact_args(
        "boot_map",
        boot_artifacts["map"],
        "{package}_boot_v{version}.map",
    ))
    arguments.extend(_package_artifact_args(
        "boot_size",
        boot_artifacts["size"],
        "{package}_boot_v{version}.size.txt",
    ))
    arguments.extend(_package_artifact_args(
        "app_bin",
        app_artifacts["bin"],
        "{package}_app_v{version}.bin",
    ))
    arguments.extend(_package_artifact_args(
        "app_hex",
        app_artifacts["hex"],
        "{package}_app_v{version}.hex",
    ))
    arguments.extend(_package_artifact_args(
        "app_elf",
        app_artifacts["elf"],
        "{package}_app_v{version}.elf",
    ))
    arguments.extend(_package_artifact_args(
        "app_map",
        app_artifacts["map"],
        "{package}_app_v{version}.map",
    ))
    arguments.extend(_package_artifact_args(
        "app_size",
        app_artifacts["size"],
        "{package}_app_v{version}.size.txt",
    ))

    ctx.actions.run(
        inputs = inputs,
        outputs = [output_dir],
        executable = ctx.attr.python,
        arguments = arguments,
        mnemonic = "McuFirmwarePackage",
        progress_message = "Packaging MCU firmware release directory %{label}",
    )

    return [DefaultInfo(files = depset([output_dir]))]

mcu_firmware_package = rule(
    implementation = _mcu_firmware_package_impl,
    attrs = {
        "boot": attr.label(providers = [McuFirmwareInfo], mandatory = True),
        "app": attr.label(providers = [McuFirmwareInfo], mandatory = True),
        "full": attr.label(providers = [McuImageInfo], mandatory = True),
        "ota_app": attr.label(providers = [McuImageInfo], mandatory = True),
        "package_name": attr.string(default = "motor_duck"),
        "version_header": attr.label(
            allow_single_file = [".h"],
            mandatory = True,
        ),
        "package_tool": attr.label(
            default = Label("//scripts:package_firmware.py"),
            allow_single_file = True,
        ),
        "python": attr.string(default = "python"),
    },
)

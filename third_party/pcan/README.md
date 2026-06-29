# PCANBasic 依赖说明

`scripts/pcan_ota.py` 使用 PEAK PCANBasic 动态库访问 PCAN-USB。

脚本加载顺序：

1. `--dll` 参数指定的路径
2. 环境变量 `PCANBASIC_DLL`
3. 当前目录 `third_party/pcan/PCANBasic.dll`
4. 系统 PATH 中的 `PCANBasic.dll`

通常安装 PEAK 驱动后，`PCANBasic.dll` 已经位于系统目录，不需要额外处理。

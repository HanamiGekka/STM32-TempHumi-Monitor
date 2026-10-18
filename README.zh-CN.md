[English](README.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md) | [繁體中文](README.zh-TW.md)

# TempHumi Monitor 温湿度监测系统

基于 STM32F103C8T6 的采集系统，配套 Qt 上位机，用于温湿度曲线显示、原始数据记录与导出。温度和湿度分别标明 REAL 实际输入或 MCU SIM 模拟来源。

当前发行：桌面版 **v010.18.1**，MCU 固件 **v010.17.1**。

![历史界面展示：REAL 温度与 MCU SIM 湿度使用独立纵轴](docs/release/desktop-showcase.png)

*维护者提供的历史展示图，界面版本为 v010.15.2。蓝色湿度曲线使用右轴，红色原始温度与橙色校正温度共用左轴；图中湿度来自 MCU 模拟。此图用于说明布局，不作为 v010.18.1 的新测量证据。[原有 v010.18.1 截图](docs/release/desktop-v010.18.1.png)保留，[图片来源说明](docs/release/PRESENTATION.md)可供核对。*

## 主要功能

- 同时显示原始温度与 MCU 校正温度，提供三种**暂定**修正配置。
- 分通道显式选择 REAL/SIM，支持 **T:REAL / H:SIM**；真实通道故障保持无效，不会静默切换为模拟。
- OLED 与 TH3 UART 使用同一份已发布测量快照。
- 温湿度独立纵轴、数据点与事件检查，支持串口、持续追加文件和 REPLAY 输入。
- 完整保存原始字节、CSV 及附属文件，导出带 SHA-256 校验的完整会话。暂停显示继续采集和记录；结束会话完成保存并释放串口。
- 自动重连与已确认配置恢复默认开启，间隔 5 秒、最多 12 次；恢复前核验当前设备身份、固件和回读结果。

## Windows 便携版与安装版

从 [Release v010.18.1](https://github.com/HanamiGekka/STM32-TempHumi-Monitor/releases/tag/v010.18.1) 下载**便携 ZIP** 或 **Setup**，并核对发行页提供的 SHA-256。运行发行程序无需安装开发工具。

1. 在 Windows 10/11 x64 上完整解压 ZIP 后运行 `TempHumi.exe`，或运行**未签名**的 Setup。Qt 库与插件须和 EXE 一起保留。
2. 接入板卡时，准备适用的 CH340 驱动，在 UART Settings 中选择实际串口，使用 **115200 波特、8N1、无流控**。
3. 选择记录文件夹，点击 **Draw / Start**。程序启动只准备 SERIAL，不自动打开串口。修改数据源或温度配置后点击对应 Apply，并检查 MCU 确认。

安装版可创建开始菜单和可选桌面快捷方式，不会自动连接串口或烧录；卸载保留非安装器拥有的文件和偏好设置。F1 打开离线帮助，About 分别显示桌面源码身份与实际观测到的 MCU 版本。

[上位机指南](docs/development/DESKTOP.md) · [硬件证据](docs/hardware/FINAL_TECHNICAL_AUDIT.md)

## 软件架构

HAL／平台 → BSP → Driver → 纯算法 → Application／数据源选择 → Protocol。OLED 与 UART 共用发布值，Qt 负责显示，不再次标定。固件上电默认为 REAL/REAL。

硬件设计包含 STM32F103C8T6、ADS1115、NTC/U6 温度链路、CHS1101/TLC556/OPA330 湿度链路、OLED 和 CH340 USB-UART。设计图不能代替实物通断核验。

[架构与数据流图](docs/architecture/OVERVIEW.md)

## 采集与通信协议

TH3 包含 **23 个字段**，记录原始码、单位、有效性、错误、分通道来源与代次。显示窗口为 **5–1200 秒**，最多 24,000 个点；显示裁剪和坐标轴调整不改变完整的原始／CSV 记录。左半区缩放和平移控制两条温度曲线，右半区控制湿度；Reset Y Scale 恢复自动范围。

[TH3 协议](docs/protocol/TH2_TH3.md) · [UART 命令](docs/protocol/UART_COMMANDS.md)

## 测试与验证

v010.18.1 发行验收记录了 MCU／Qt Debug 与 Release 零警告、零错误构建，以及 22 套主机回归、100%／150% 缩放各 94 项原生 GUI 检查、120 项便携包和 102 项 Bundle 检查；实际安装、帮助、记录、串口及卸载时的数据保留也已验证。

便携版和安装版分别通过 3600 秒主机／前向源时间门槛，时长为 **3665.000／3664.149 秒**和 **3664.995／3664.148 秒**。各项证据边界和保留异常见[验证报告](docs/release/VALIDATION.md)。这些是已有发行结果，本轮文档维护未重复长测或重建二进制。

提供的十组温度对照：原始 MAE **0.754 °C**、RMSE **1.020725 °C**；校正后 MAE **0.522 °C**、RMSE **0.720777 °C**，最大绝对误差 **1.53 °C**，**8/10** 在 ±1 °C 内。该样本不能证明独立全量程精度。

## 构建与固件烧录

已验证工具链为 **Qt 5.14.2／MinGW 7.3 x64**、**STM32CubeIDE 1.19.0／Arm GCC 13.3**、Python 3 和 STM32CubeProgrammer。[构建指南](docs/development/BUILD.md)提供上位机、MCU、主机检查和 SWD 烧录步骤。固件目标为 64 KiB Flash／20 KiB RAM。烧录前停止采集、确认目标与镜像来源、保存完整 Flash 备份；BIN 起始地址为 `0x08000000`，随后执行 Verify 和回读比较。

## 已知限制

- **REAL 湿度：PARTIAL / NOT VALIDATED**。SIM 湿度不代表真实 RH 精度。
- **PWM：NOT TESTED ON HARDWARE**，缺少波形、频率和占空比实测；**V_CHS1101：CONFLICT**，与跟随器／ADC 直流读数仍有矛盾。
- **USB 物理根因：UNRESOLVED**。软件恢复成功不能证明线缆、供电或焊点的具体原因。
- **独立全量程温度精度：NOT VERIFIED**。温度配置仍为暂定，不宣称新的 OLED 光学测量或第二台实体 Windows 电脑验收。
- **Setup：UNSIGNED（未签名）**。

## 发行与源码身份

桌面 **v010.18.1** 与 MCU **v010.17.1** 分别标识版本。[源码与资产对应关系](docs/release/SOURCE_IDENTITY.json)记录冻结构建、公开发行提交和资产哈希。重新构建会嵌入当时提交与时间，不替换同版本冻结资产。私有历史、原始会话、设备标识和含账号信息的 CAD 不进入公开仓库。

## 许可证与贡献

项目原创源码及文档采用 **GPL-3.0-or-later**。QCustomPlot 作者为 Emanuel Eichhammer；Qt／ST／Arm／MinGW 保留上游条款。仓库所有者为主要开发者，其他组员的贡献予以致谢，组员已同意公开。Qt 对应源码包和运行时声明随 Release 提供。

[LICENSE](LICENSE) · [NOTICE](NOTICE.md) · [依赖条款](docs/release/THIRD_PARTY_NOTICES.md)

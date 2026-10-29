# 00 移植流程：8 阶段与官方映射

## 官方口径 vs 本流程

OpenHarmony 把轻量芯片适配分为四个环节（`porting-minichip-overview`）：

| 官方环节 | 官方内容 |
|---|---|
| 移植准备 | 下载代码、搭建编译环境、创建厂商与产品目录并接入编译框架 |
| 移植内核 | 芯片架构适配（已有 arch 可跳过）、**厂商 SDK 接入**、内核配置、中断处理、内核子系统 |
| 移植子系统 | 启动、文件、安全、通信、外设驱动 |
| 移植验证 | XTS 兼容性测试 + 自有质量验证 |

本流程把"内核"与"子系统"拆细，便于逐段验收：

| 本流程 | 对应官方环节 |
|---|---|
| A0 环境与 SDK | 移植准备 |
| A1 工程骨架 | 移植准备 |
| A2 启动链 | 移植内核（中断、启动顺序） |
| A3 HDF 接通 | 移植子系统（驱动框架） |
| A4 平台驱动 | 移植子系统（外设） |
| A5 demo | —（应用示例，非官方环节） |
| A6 XTS | 移植验证 |
| A7 交付 | 移植验证（自有部分） |

**先确认架构是否已支持**：`kernel/liteos_m/arch` 已包含 arm9 / cortex-m3 / m4 / **m7** / m33、risc-v、csky、xtensa。
架构在内 → 不移植 arch；不在内 → 需实现 `arch/include` 下的 `los_arch.h`、`los_atomic.h`、`los_context.h`、
`los_interrupt.h`、`los_timer.h` 五组接口。

## 内存门槛：先定"做到哪一步"

| RAM | 组件最小集 | 说明 |
|---|---|---|
| < 128 KiB | kernel(liteos_m) + hdf(hdf_core) + startup(bootstrap_lite, init) + systemabilitymgr(samgr_lite) + hiviewdfx(hilog_lite, hievent_lite) + commonlibrary(utils_lite) | **只适配内核**；不引框架层、Ability、ArkUI、图形、媒体 |
| 128 KiB – 1 MiB | 上述 + 按需逐个追加 | 每加一个组件都要重新量 RAM |
| ≥ 1 MiB | 可评估 small system | 另议 |

判断依据是**链接后实际占用**，不是估算：`nm` 看 `.bss`/`data`，`size` 看总段。
低于 128 KiB 时，任何"要不要加 XX"的默认答案都是不加。

## 阶段 A0：环境与 SDK

**动作**
1. 工具链齐备：`hb`、`gn`、`ninja`、`arm-none-eabi-gcc`（或对应架构）、`objcopy`、`python3`。
2. SDK 完整性：`CMSIS/device`（设备头）、`CMSIS/core`、外设驱动源码目录（不是只有 `.a`）。
3. 通道可用：能编译、能烧录、能看串口。

**闸门**：`which` 全部有输出；SDK 头文件能被一个最小 C 文件 `-fsyntax-only` 通过。

**常见失败**
| 现象 | 处置 |
|---|---|
| `hb` 找不到 | `export PATH=$HOME/.local/bin:$PATH` |
| gcc 版本与 SDK 库不匹配 | 固定用 SDK 配套版本 |
| 只有 `.a` 没有源码 | 要源码；否则驱动只能调闭源库，无法排障 |

## 阶段 A1：工程骨架

**动作**
1. 三层目录：`device/soc/<v>/<c>`（SoC 事实）、`device/board/<v>/<b>`（板级事实）、`vendor/<v>/<p>`（产品配置）、`applications/sample/<b>`（应用）。
2. `config.gni`：`kernel_type=liteos_m`、`kernel_version=""`（两侧一致）、`board_cpu`、`board_cflags`、**`board_asmflags` 必须含 `-mfpu`/`-mfloat-abi`**。
3. `config.json`：按内存门槛选组件。
4. 应用注册：应用目录放 `bundle.json`，`subsystem` 与产品配置一致。

**闸门**：`hb set -p <产品>` 能选中；`hb build` 的失败点**必须在链接阶段**，不能是"找不到 device 目录/组件"。

**常见失败**
| 现象 | 根因 | 处置 |
|---|---|---|
| 报找不到 device 目录 | 两侧 `kernel_version` 不一致 | 统一置 `""` |
| gn 语法错误/路径被截断 | 源码根目录名含空格 | 改名 |
| 组件未安装 | `bundle.json` 的 subsystem 不匹配 | 两侧对齐 |

## 阶段 A2：启动链

**顺序不可调**（顺序错会互相掩盖故障）：
时钟与从核 → 早期指示灯 → 串口 → **内核前的引脚复用** → `LOS_KernelInit()` → `DeviceManagerStart()` → `OHOS_SystemInit()` → `LOS_Start()`。

**三个必须现场调对的参数**
| 参数 | 位置 | 判据 |
|---|---|---|
| 内核堆大小 | 链接脚本 `_Min_Heap_Size` | `nm` 的 `__heap_end - __heap_start` 等于设定值（堆是 NOLOAD，改它不改变文件大小） |
| 中断向量表对齐 | `LOSCFG_ARCH_HWI_VECTOR_ALIGN` | ≥ 向上取 2 的幂(向量数 × 4)；432 条向量 → 2048 |
| C/ASM 浮点一致 | `board_asmflags` 或汇编内显式定义宏 | `objdump` 出现 `vstmdb {d8-d15}` |

**闸门**：启动日志 5 行齐（版本串 / 串口 / 内核 OK / HDF ret=0 / 应用被拉起）+ 心跳 10 次偏差 <2%。

**常见失败**：见 `04-pitfalls.md` #4–#9。

## 阶段 A3：HDF 接通

**动作**：hcs 两段式（device_info 节点 + 参数节点 + `hdf.hcs` include）；控制器类补管理器节点；`policy` 与取设备方式匹配。

**闸门**：`.hdf.driver` 段长度按驱动数增加（32 位目标每个 +4 字节）；驱动 `Init()` 日志出现；**日志里没有"使用默认值"类提示**。

**常见失败**：见 `04-pitfalls.md` #10–#13。

## 阶段 A4：平台驱动

每个外设固定四文件 + 三处接线，详见 `01-hdf-driver.md`。

**闸门**：数据回路成立，不是"没报错"。

| 外设 | 回路判据 |
|---|---|
| GPIO | 电平随写入翻转 |
| UART | 回环收发一致 |
| I2C | 写 N 字节 → 读回逐字节相同 |
| SPI | 读器件 ID 与手册一致；或 MOSI/MISO 短接回读一致 |
| PWM | 示波器：频率符合设定，占空比随设置变化 |
| ADC | 原始码落在合理区间，且随被测量变化 |

## 阶段 A5：demo

`SYS_RUN` 注册任务，输出用无缓冲路径。任务设计见 `02-demo.md`。

**闸门**：应用被拉起、输出可直接判读、周期偏差 <5%、不拖慢其他任务。

## 阶段 A6：XTS

见 `03-xts.md`。**不要启用五项小型化参数**（实测不可用）。

**闸门**：套件名正确、同一用例只出现一遍、目标套件 failures = 0。

## 阶段 A7：交付

1. 版本串（每次编译唯一）、产物命名（正式版无后缀，调试版带版本）。
2. 全量重编，记录 `.o` 数量与 ELF 时间戳（工具报的耗时不可信）。
3. 校验清单全勾：`nm` 驱动表与堆、`strings` 版本串、`objdump` 浮点指令。
4. 提交前扫换行（`scripts/check_crlf.sh`）。

## 芯片相关 vs 芯片无关：分界线

| 只属于芯片 | 应保持通用 |
|---|---|
| 引脚号与 AF、时钟树、寄存器位、时序常量 | 三层目录结构、hcs 两段式、四文件分层、回调边界 |
| 链接脚本的内存段与对齐 | 驱动表校验方法、启动链顺序、闸门判据 |
| 启动汇编、SystemInit | demo 结构、XTS 流程、坑库条目 |

换芯片时，理论上只应改左列。若发现要改右列，说明分层被破坏了，先回头修分层。

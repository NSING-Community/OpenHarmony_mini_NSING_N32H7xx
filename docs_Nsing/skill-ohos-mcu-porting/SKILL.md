---
name: ohos-mcu-porting
description: Port OpenHarmony (LiteOS-M, mini) to a new MCU, and build HDF platform drivers, demos and XTS tests on it. Use when the user asks to port OpenHarmony to a chip or board, to add an HDF driver (GPIO/UART/I2C/SPI/PWM/ADC/...), to write a demo for such a driver, or to run XTS compatibility tests on a mini target. Works with any vendor SDK; the N32H7xx port in references/ is a worked example, not a requirement.
---

# OpenHarmony MCU 移植与 HDF 驱动开发

## 这个 skill 解决什么

两类任务，同一套知识：

| 模式 | 用户说什么 | 你要做什么 |
|---|---|---|
| **A 全流程** | "完成移植与驱动开发、demo 移植、XTS 测试的全部过程" | 按 8 个阶段推进，每阶段过闸门 |
| **B 单点** | "创建 SPI HDF，参考 SDK demo 实现一个 demo" | **只做这一个外设**，不碰其他驱动 |

模式判定见「路由」。两模式共用 `references/` 里的知识，区别只在范围。

## 前提：先确认三件事

开工前必须拿到，缺一不可：

1. **厂商 SDK 路径**，且包含 CMSIS 设备头 + 标准外设驱动源码（不只是库文件）。
2. **芯片资源**：内核型号（Cortex-M?/RISC-V?）、Flash、RAM 大小、主频。
3. **目标板**：引脚引出情况、外部器件（EEPROM/Flash/传感器）、调试串口。

缺任何一项 → 问，最多三个问题，问完就开工，不要自己假设。

## 内存门槛：决定"适配到什么程度"

| RAM | 结论 |
|---|---|
| **< 128 KiB** | **只适配内核（mini 最小系统）**：内核 + HDF + 启动 + 最小服务。**不要引入完整 OpenHarmony**（框架层/Ability/ArkUI/图形/媒体） |
| 128 KiB – 1 MiB | 轻量系统上限区间，逐个组件确认后再加 |
| ≥ 1 MiB | 可考虑 small system，组件选择另议 |

低于 128 KiB 时，凡是"要不要加 XX 子系统"的问题，答案默认是**不加**；除非用户明确要求，并接受由此带来的内存与启动时间代价。

## 路由

按用户意图分派，**不要按目录名分派**：

| 意图特征 | 模式 | 动作 |
|---|---|---|
| 提到"移植/porting/适配新芯片/整套流程" | A | 走 8 阶段 |
| 提到具体外设 + "HDF/驱动" | B | 只做该外设（+ 可选 demo） |
| 提到"XTS/兼容性测试" 且已有工程 | A 的阶段 7 | 只做测试部分 |
| 只说外设名（如 "HDF I2C"） | B（无 demo） | 只出驱动，不建应用任务 |
| 只问"怎么做/为什么" | 讲解 | 不写代码，引用 references 的对应章节 |

模式 A 的驱动队列**默认五个**：GPIO → UART → I2C → PWM → ADC。
**每次开始前问一句**："默认按 GPIO/UART/I2C/PWM/ADC 五个外设推进，需要调整吗？" 用户不改就按默认走。

## 模式 A：8 个阶段

每阶段固定产出 + 闸门。**闸门不过，不许进下一阶段。**

| 阶段 | 目标 | 闸门（可执行判据） |
|---|---|---|
| A0 环境与 SDK | 工具链、SDK 完整性 | 工具版本齐全；SDK 头与源码可编译 |
| A1 工程骨架 | 三层目录 + 产品配置 | `hb set` 能选中产品；失败点位于链接阶段 |
| A2 启动链 | 串口出字、调度器跑起来 | 启动日志 5 行齐；`nm` 堆差值 = 设定值；`objdump` 见浮点批量存取 |
| A3 HDF 接通 | hcs 解析、驱动被拉起 | `.hdf.driver` 增加 4 字节/驱动；日志无"使用默认值" |
| A4 平台驱动 | 每个外设能读能写 | 数据回路判据（见 references/01） |
| A5 demo | 应用任务可观测 | `SYS_RUN` 任务输出稳定，周期偏差 <5% |
| A6 XTS | 兼容性用例全通过 | 套件名正确、无重复执行、failures = 0 |
| A7 交付 | 固件 + 记录 | 版本串可追溯；检查清单全勾 |

每阶段的详细动作、常见失败、处置 → `references/00-porting-flow.md`。

## 模式 B：5 步，只碰四文件 + 三处接线

```
1 定位 SDK 例程      读出引脚、AF、时钟源、时序参数（不要凭猜）
2 确认参数语义      原值还是编码值？单位是 ns 还是 tick？（见 references/01 §单位与语义）
3 生成四文件 + 接线 模板见 templates/；只改必需的 BUILD.gn 分支与 hcs 节点
4 编译 + 自检        scripts/verify_image.sh；驱动表应精确 +4 字节
5 给 demo 与判据     可选；判据必须可观测（回读一致 / 示波器 / 数值区间）
```

**模式 B 不做的事**：不重构已有驱动、不动其他 hcs 节点、不改 `config.json` 组件清单、不动已有应用任务。
用户没要求的，一律不做。

## 通用性：芯片相关的东西只有一个地方

```
device/soc/<vendor>/<chip>/liteos_m/
├── include/n32_xxx_hw.h      # 对外只给原型与常量
├── src/n32_xxx_hw.c          # 唯一允许 include 厂商 SDK 头的文件
├── src/startup_*.s           # 启动汇编（可用厂商的，改为 GCC 语法）
├── board.ld                  # 内存布局、向量表对齐、.hdf.driver KEEP
└── config.gni                # 工具链与编译选项（含 asmflags 浮点）
```

换芯片时替换的只有：`src/n32_xxx_hw.c` 里的引脚表与时钟配置、`board.ld` 的内存段、`config.gni` 的工具链。
上层（HDF 驱动、hcs、应用、XTS）不动。

## 坑库怎么用

`references/04-pitfalls.md` 收录了 25 条实测过的坑，每条写成"现象 → 根因 → 处置 → 阶段"。
**写代码时对照阶段查表**；编译/运行异常时先查表，不要自由发挥。

其中若干条已经做成脚本断言（`scripts/`），优先跑脚本，而不是靠记忆。

## 校验优先于自信

任何"改好了"的结论，必须有一条产物级证据：

| 结论 | 证据 |
|---|---|
| 驱动进镜像了 | `nm` 的 `.hdf.driver` 差值 |
| 参数真的读到了 | 日志里没有"使用默认值"类提示 |
| 浮点上下文对了 | `objdump` 出现 `vstmdb {d8-d15}` |
| 版本对得上 | `strings` 里的 `BUILD=` 串 |
| 配置真的生效 | `strings` 里目标字符串存在/不存在 |

命令与期望值 → `references/05-verification.md`。

## 目录

```
SKILL.md                     本文件：路由、阶段、闸门
README.md                    给人看的：如何安装与使用
references/
  00-porting-flow.md         8 阶段细节、官方 4 阶段映射、内存门槛决策
  01-hdf-driver.md           HDF 模型、两段式 hcs、四文件、三要素、单位语义
  02-demo.md                 SYS_RUN、任务设计、日志、demo 门控
  03-xts.md                  hctest、套件、参数取舍、日志判读
  04-pitfalls.md             25 条坑（现象/根因/处置/阶段）
  05-verification.md         校验命令与期望值
templates/                   可直接改的骨架文件
scripts/                     可执行校验与脚手架
```

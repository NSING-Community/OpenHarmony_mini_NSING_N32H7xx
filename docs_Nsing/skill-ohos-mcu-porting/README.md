# ohos-mcu-porting — 使用说明

把 OpenHarmony（LiteOS-M / mini）移植到一颗新 MCU，并在其上开发 HDF 平台驱动、demo 与 XTS 测试。
适用于**任意厂商 SDK**；`references/` 里出现的 N32H7xx 只是完整走过一遍的实例，不是前提。

## 安装

把整个目录复制到 skill 目录即可生效（不自动安装）：

```bash
cp -r skill-ohos-mcu-porting ~/.workbuddy/skills/
```

放在这里（`docs_Nsing/`）是随仓库发布用的；是否装到本机由你决定。

## 怎么触发

不需要记固定句式。按意图说就行，两种典型用法：

**全流程**

> 完成移植与驱动开发，demo 移植，XTS 测试的全部过程

**单点**

> 请创建 SPI HDF，并参考 SDK 中的 demo，实现一个基于 SPI HDF 的 demo

其他等价说法也能识别，例如"把 OpenHarmony 移植到 XX 芯片"、"给 I2C 加个 HDF 驱动"、"跑一下 XTS"。
分派规则见 `SKILL.md` 的「路由」。

## 开工前它会问你什么

最多三个问题：

1. 厂商 SDK 路径（需含 CMSIS 设备头 + 外设驱动源码）
2. 芯片资源（内核、Flash、RAM、主频）
3. 目标板（引脚引出、外部器件、调试串口）

信息齐了就直接开工，不再追问。

## 内存门槛（重要）

| RAM | 适配范围 |
|---|---|
| **< 128 KiB** | **只适配内核**：内核 + HDF + 启动 + 最小服务；不引入完整 OpenHarmony |
| 128 KiB – 1 MiB | 逐组件确认 |
| ≥ 1 MiB | 可评估 small system |

RAM 不足 128 KiB 时，任何"要不要加某个子系统"的默认答案都是**不加**。

## 默认驱动队列

模式 A 默认按 **GPIO → UART → I2C → PWM → ADC** 推进。
开始前它会问一句是否调整；说"换成 SPI/ADC"，就按你的清单走。

## 它能做到 / 做不到

**能**：生成代码与配置、接构建、编译、跑产物级校验（`nm`/`strings`/`objdump`）、按照坑库自查。

**不能**：替你烧录、替你看示波器、替你授权外部服务。这三类必须人工，skill 会明确停在这里等你。

## 目录

```
SKILL.md        路由、8 阶段、闸门
references/     知识库（流程 / HDF / demo / XTS / 坑库 / 校验）
templates/      骨架文件，直接改
scripts/        校验与脚手架
```

## 怎么改成你自己的

- **换芯片**：只改 `device/soc/<厂商>/<芯片>/` 下的引脚表、`board.ld`、`config.gni`。
- **加坑**：往 `references/04-pitfalls.md` 追一条，四列写全；能写成脚本断言的优先写脚本。
- **改默认驱动队列**：改 `SKILL.md` 的「路由」一节。

# OpenHarmony MINI 移植与驱动开发标准作业程序（SOP）

| 项目 | 内容 |
|---|---|
| 文档编号 | OH-MINI-SOP-001 |
| 版本 | 1.0 |
| 适用范围 | 把 OpenHarmony 6.1 mini（LiteOS-M 内核）移植到一颗新的 Cortex-M MCU，并完成 HDF 平台驱动、应用 demo、XTS 测试与交付 |
| 读者 | 有 MCU 裸机/BSP 经验、**未接触过 OpenHarmony 与 HDF** 的开发人员 |
| 前置材料 | 《OpenHarmony MINI 移植与 HDF 驱动开发技术说明书》（原理与失效模式）、《N32H7xx 移植完整方案》（本项目的决策记录） |
| 参照实现 | N32H7xx EVAL（Cortex-M7 + CM4，SDK 1.3.0）。文中所有数值、日志、命令均来自该实现，可直接比对 |
| 修订 | 1.0 首版 |

---

## 0 怎么用这份 SOP

### 0.1 三条使用规则

1. **按阶段推进，不跳阶段。** 本文分 8 个阶段（S0–S7），每个阶段末尾有一个 **Gate（验收闸门）**。
2. **Gate 没过，不许进入下一阶段。** 每个 Gate 都给了"可执行判据"——不是"看起来对"，而是能跑出特定输出。Gate 不过就回到本阶段的"失败处理"，不要靠猜继续往下做。
3. **出问题先查"失败处理"，再查技术说明书。** 本文的失败处理只给最常见的 1–3 个原因与处置；更细的原理与失效模式在技术说明书对应章节。

### 0.2 阶段总览

| 阶段 | 目标 | 主要产出 | Gate 判据 | 参考工时 |
|---|---|---|---|---|
| S0 | 工具链与双机通道就绪 | 能编译、能烧录、能看串口 | 6 条自检命令全部通过 | 0.5–1 天 |
| S1 | 工程骨架能被构建系统识别 | 三层目录 + 产品配置 + 板级 config | `hb build` 走到链接阶段 | 1–2 天 |
| S2 | 启动链打通，串口出字、调度器跑起来 | 能打印 5 行启动日志 + 心跳 | 心跳周期稳定 | 2–5 天 |
| S3 | HDF 框架接通 | hcs 两段式 + 4 个节点 | 驱动程序被 HDF 拉起并打日志 | 0.5–1 天 |
| S4 | 第一个平台驱动跑通 | 四个文件 + 接线三要素 | 数据回路 PASS | 2–4 天/驱动 |
| S5 | 应用 demo | 一个注册到 `SYS_RUN` 的任务 | 输出稳定且可判读 | 0.5 天 |
| S6 | XTS 兼容性测试 | 测试固件 + 结果 | 用例全通过 | 1–2 天 |
| S7 | 交付 | 固件 + 记录 | 附录 C 清单全勾 | 0.5 天 |

**总计约 8–15 个工作日**（第一个驱动最慢，第二个起明显加速）。

### 0.3 路径与命名约定

本文用以下变量指代路径，执行时替换成实际值：

| 变量 | 含义 | 本项目取值 |
|---|---|---|
| `$SRC` | OpenHarmony 源码根目录 | `/home/<user>/oh_n32h7xx/OHOS_MINI` |
| `$SOC` | SoC 适配目录 | `$SRC/device/soc/Nsing/n32h7xx` |
| `$BRD` | 板级适配目录 | `$SRC/device/board/Nsing/n32h7xx_eval/liteos_m` |
| `$VEN` | 产品配置目录 | `$SRC/vendor/Nsing/n32h7xx_eval` |
| `$APP` | 应用目录 | `$SRC/applications/sample/n32h7xx_eval` |
| `$OUT` | 构建输出目录 | `$SRC/out/n32h7xx_eval/n32h7xx_eval` |

命名规则（强制执行，否则后期无法把日志对应回源码）：

- 版本串 `BUILD=v<序号>-<英文主题>`，每次编译必须变一次。
- 调试固件 `OHOS_Image_<版本>.hex`；正式固件 `OHOS_Image.hex`。

---

## 1 S0 环境与工具链

### 1.1 主机与目标机分工

| 角色 | 放什么 | 为什么 |
|---|---|---|
| **构建机**（Linux） | 源码、工具链、编译 | 构建系统依赖 Linux 工具链与符号链接 |
| **开发机**（Windows） | 编辑器、串口工具、镜像只读副本 | 便于阅读与烧录 |
| 目标板 | 串口 + 调试器 | 唯一能验证运行期行为的地方 |

Windows 与构建机之间只同步**源码子集**（读用），**编译只在构建机做**。同步脚本示例见附录 A。

### 1.2 官方资料与源码获取

移植过程中遇到任何"官方期望怎么做"的问题，以下入口是第一手来源。本文的"官方要求"均取自这些页面。

| 资源 | 地址 |
|---|---|
| OpenHarmony 官网 | `https://www.openharmony.cn` |
| 开发者文档（6.1） | `https://docs.openharmony.cn/pages/v6.1/zh-cn/` |
| 代码托管 | `https://gitcode.com/openharmony` |
| 轻量芯片移植总览 | `.../device-dev/porting/porting-minichip-overview.md` |
| 移植准备 | `.../device-dev/porting/porting-minichip-prepare.md` |
| 移植内核 | `.../device-dev/porting/porting-minichip-kernel.md` |
| 子系统移植总览 / 启动 / 外设驱动 | `.../porting-minichip-subsys-{overview,startup,driver}.md` |
| 移植验证 | `.../porting/porting-minichip-verification.md` |
| 构建子系统 | `.../subsystems/subsys-build-all.md` |
| HDF 驱动管理 | `.../driver/driver-hdf-manage.md` |
| XTS 用例开发与执行 | `.../device-test/xts.md` |

> 文档站按版本分目录（`v6.1`、`v5.0`…）。若某个页面打不开，改从代码仓 `openharmony/docs` 的 `zh-cn/` 下取同名 `.md`，内容一致。

**获取 6.1 LTS 源码**（在 Linux 构建机上执行）：

```bash
mkdir -p ~/ohos && cd ~/ohos
repo init -u https://gitcode.com/openharmony/manifest.git \
          -b OpenHarmony-v6.1-LTS --no-repo-verify
repo sync -c
repo forall -c 'git lfs pull'
```

- **6.1 LTS 是 tag，不是 `...-Release` 分支。** 依据：本工程 `.repo/manifests/default.xml` 中
  `<default remote="gitcode" revision="refs/tags/OpenHarmony-v6.1-LTS" .../>`，
  即该版本以 `OpenHarmony-v6.1-LTS` 标签发布。用 `-b OpenHarmony-v6.1-LTS` 即可检出（repo 支持 tag 名）。
  其他版本请先用 `git ls-remote --tags https://gitcode.com/openharmony/manifest.git` 确认实际标签，
  不要按 `OpenHarmony-<版本>-Release` 的格式推断。
- `repo sync -c` 只拉当前分支；`git lfs pull` 补齐大文件。
- 源码根目录名**不得含空格**（否则 gn 路径被切断）。
- 只需芯片适配时，可只同步 `kernel/`、`build/`、`base/`、`third_party/`、`drivers/`、`utils/` 与本厂商目录，但首次建议全量，避免依赖缺失。

### 1.3 必备工具

| 工具 | 版本要求 | 用途 | 检查命令 |
|---|---|---|---|
| `hb` | ohos-build 1.0.0 | 选产品、驱动构建 | `hb -v` |
| `gn` | ≥ 2023 | 生成 ninja | `gn --version` |
| `ninja` | ≥ 1.10 | 并行编译 | `ninja --version` |
| `arm-none-eabi-gcc` | 10.3.1（须与 SDK 库一致） | 交叉编译 | `arm-none-eabi-gcc --version` |
| `arm-none-eabi-objcopy` | 同工具链 | 生成 hex/bin | `arm-none-eabi-objcopy --version` |
| `py`/`python3` | ≥ 3.8 | 构建脚本依赖 | `python3 -V` |
| 串口工具 | 任意 | 看日志 | — |

### 1.4 步骤与判据

**步骤 1：确认工具链在 PATH 中。**

```bash
export PATH=$HOME/.local/bin:$PATH
which hb gn ninja arm-none-eabi-gcc arm-none-eabi-objcopy
```

期望：5 条路径全部有输出。

**步骤 2：确认磁盘与内存。** 源码树 + 构建输出约需 25–40 GB。

```bash
df -h $HOME | tail -1
free -g | head -2
```

期望：可用空间 ≥ 50 GB，内存 ≥ 8 GB（4 GB 也能编，但并行度要降）。

**步骤 3：确认能烧录、能看串口。** 用供应商提供的任意裸机例程烧一次，确认串口能出字。

> 这一步不能省。若后面"零输出"，需要先排除"本来就是线缆/驱动/波特率问题"。

**步骤 4：确认构建机与开发机通道可用。**

```bash
ssh -o BatchMode=yes -o ConnectTimeout=8 <user>@<build-host> 'echo OK'
scp -o BatchMode=yes <本地文件> <user>@<build-host>:<远端目录>
```

期望：打印 `OK`，scp 无报错。

> **必须带 `BatchMode=yes`**：交互式密码提示会让自动化命令挂住。

### 1.5 Gate G0

| 检查项 | 判据 |
|---|---|
| 5 个工具可见 | `which` 全部有输出 |
| 空间/内存 | ≥ 50 GB / ≥ 8 GB |
| 烧录通道 | 裸机例程能出串口 |
| 双机通道 | ssh 与 scp 均成功 |

**失败处理**

| 现象 | 原因 | 处置 |
|---|---|---|
| `hb` 找不到 | 未加入 PATH | `export PATH=$HOME/.local/bin:$PATH`，并写入 shell 配置 |
| gcc 版本不符 | 装了多个工具链 | 固定用 SDK 配套版本，`arm-none-eabi-gcc --version` 核对 |
| ssh 卡住要密码 | 未配置密钥 | 配 `ssh-keygen` + `ssh-copy-id`，或使用 `BatchMode=yes` 暴露问题 |

---

## 2 S1 源码与工程骨架

### 2.1 目标

让构建系统"认得"这颗芯片：能执行 `hb set` 选中产品，`hb build` 能走到链接阶段（此时链接失败是正常的，因为还没有板级代码）。

**官方移植流程与本 SOP 的对应关系**（官方把轻量芯片适配分为四个环节）：

| 官方环节 | 官方内容 | 本 SOP |
|---|---|---|
| 移植准备 | 下载代码、搭建编译环境、创建厂商与产品目录并接入编译框架 | S0、S1 |
| 移植内核 | 芯片架构适配（已有 arch 可跳过）、**厂商 SDK 接入**、内核配置、中断处理、内核子系统 | 1.4 节 + 2.4 节 + S2 |
| 移植子系统 | 启动子系统、文件子系统、安全子系统、通信子系统、外设驱动 | S3、S4 |
| 移植验证 | XTS 兼容性测试 + 自有质量验证 | S6 |

> 官方说明：`kernel/liteos_m/arch` 已支持 arm9 / cortex-m3 / cortex-m4 / **cortex-m7** / cortex-m33、risc-v、csky、xtensa。
> Cortex-M7 已在内，因此本工程**不需要移植 arch**，只做 SDK 接入与板级适配。

"移植内核"环节中，本工程**明确不做**以下三项，依据一并列出：

| 不做的事 | 依据 | 何时才需要做 |
|---|---|---|
| 移植 `arch` | `kernel/liteos_m/arch` 已支持 arm9、cortex-m3、cortex-m4、**cortex-m7**、cortex-m33，以及 risc-v、csky、xtensa | 目标架构不在该列表内时，才需实现 `arch/include` 下的 `los_arch.h`、`los_atomic.h`、`los_context.h`、`los_interrupt.h`、`los_timer.h` |
| 修改 `kernel/liteos_m` 内核源码 | 内核按原样使用；本工程仅对 `arch/arm/cortex-m7/gcc/los_dispatch.S` 做了一处浮点宏适配（见 3.3 节） | 换编译器（iar / keil）时，对应 `arch/<arch>/<compiler>` 另有实现 |
| 修改厂商 `firmware/` | SDK 原样放置，全部适配在其上层完成（薄封装 + HDF 驱动） | 不得修改；确需改动时复制到 SoC 目录并注明来源 |

### 2.2 三层目录（必须先建齐，否则后面每一步都在补窟窿）

```
$SRC/
├── device/soc/<厂商>/<芯片>/                 # SoC 事实
│   ├── liteos_m/
│   │   ├── build.gn 或 BUILD.gn
│   │   ├── config.gni                        # 与板级保持一致
│   │   ├── board.ld                          # 内存布局、向量表、堆、驱动表
│   │   ├── include/                          # 对外只暴露原型
│   │   └── src/                              # 启动汇编、SystemInit、SoC 薄封装
│   └── firmware/                             # 厂商 SDK（原样放置，不修改）
├── device/board/<厂商>/<板名>/liteos_m/
│   ├── config.gni                            # ★ 构建系统唯一读取的配置
│   ├── board/
│   │   ├── BUILD.gn
│   │   ├── include/                          # 板级对外头（LED、串口引脚定义）
│   │   └── src/                              # main.c、板级初始化
│   └── Kconfig
├── vendor/<厂商>/<板名>/
│   ├── config.json                           # ★ 组件清单
│   ├── kernel_configs/<产品>.config           # 内核开关
│   └── hdf_config/                           # HDF 设备树
└── applications/sample/<板名>/                # 应用（含 bundle.json）
```

### 2.3 逐文件填写

**步骤 1：板级 `config.gni`。** 逐项填，不要留空：

| 字段 | 填什么 | 本项目值 |
|---|---|---|
| `kernel_type` | 内核类型 | `"liteos_m"` |
| `kernel_version` | 与 SoC 侧 `config.gni` **完全一致** | `""` |
| `board_cpu` | CPU 型号 | `"cortex-m7"` |
| `board_arch` | 架构 | `"armv7e-m"` |
| `board_toolchain` | 工具链 | `"arm-none-eabi-gcc"` |
| `board_cflags` | 见下方模板 | — |
| `board_asmflags` | **必须包含 `-mfpu` 与 `-mfloat-abi`** | — |
| `board_ld_flags` | `--gc-sections` + `-Map` | — |
| `board_include_dirs` | SoC 头 + SDK 头 + 公共库 | — |

`board_cflags` 模板（按实际芯片替换主频与宏）：

```gn
board_cflags = [
  "-mcpu=cortex-m7", "-mthumb",
  "-mfpu=fpv5-d16", "-mfloat-abi=hard",
  "-O2", "-ffunction-sections", "-fdata-sections",
  "-D<芯片宏>", "-DCORE_CM7", "-DUSE_STDPERIPH_DRIVER",
  "-DOS_SYS_CLOCK=<实际 HCLK 赫兹>",
]
board_asmflags = [
  "-mcpu=cortex-m7", "-mthumb",
  "-mfpu=fpv5-d16", "-mfloat-abi=hard",   /* ★ 缺这两项 → 运行期上下文错位 */
]
```

> **强制**：`board_asmflags` 的浮点选项必须与 `board_cflags` 一致。这一步省掉，后面会遇到"进调度器就崩"（技术说明书 4.8）。

**步骤 2：产品 `config.json` 的组件清单。** 最小可用集合：

```json
"subsystems": [
  { "subsystem": "applications",     "components": [{ "component": "<你的app组件>" }] },
  { "subsystem": "kernel",           "components": [{ "component": "liteos_m" }] },
  { "subsystem": "hdf",              "components": [{ "component": "hdf_core" },
                                                    { "component": "drivers_tools" }] },
  { "subsystem": "startup",          "components": [{ "component": "bootstrap_lite" },
                                                    { "component": "init" }] },
  { "subsystem": "systemabilitymgr", "components": [{ "component": "samgr_lite" }] },
  { "subsystem": "hiviewdfx",        "components": [{ "component": "hilog_lite" },
                                                    { "component": "hievent_lite" }] },
  { "subsystem": "commonlibrary",    "components": [{ "component": "utils_lite" }] }
]
```

> **注意**：`kernel_version` 必须为空字符串，且与 SoC 侧一致。不一致的直接后果是构建系统报"找不到 device 目录"。

**步骤 3：应用组件注册。** 在应用目录放 `bundle.json`（不要改 `build/lite/components/*.json`）：

```json
{
  "name": "@ohos/<你的app>",
  "version": "1.0.0",
  "license": "Apache License 2.0",
  "publishAs": "code-segment",
  "segment": { "destPath": "applications/sample/<板名>" },
  "component": {
    "name": "<你的app组件名，与 config.json 一致>",
    "subsystem": "applications",
    "adapted_system_type": [ "mini" ],
    "deps": { "components": [ "utils_lite", "liteos_m", "hdf_core" ] },
    "build": { "sub_component": [ "//applications/sample/<板名>/app:app" ] }
  }
}
```

**步骤 4：执行构建。**

```bash
cd $SRC
export PATH=$HOME/.local/bin:$PATH
hb set -p <产品名>          # 交互选择，或直接写产品名
hb build
```

**期望输出**：能看到 `gn gen` 与 `ninja` 阶段启动。此时链接报符号未定义（缺 `main`、缺启动文件）是**允许的**。

### 2.4 厂商 SDK 如何接入

本节说明厂商 SDK 与 OpenHarmony 的衔接位置。多数移植工作以此为起点，应在首次接入时一次配置正确。

| 接入什么 | 放在哪 | 由谁读 | 本工程实例 |
|---|---|---|---|
| SDK 源码与头文件 | `device/soc/<厂商>/<芯片>/firmware/`，**原样放置不修改** | 只有 SoC 薄封装的 `.c` 引用 | `firmware/CMSIS/{core,device}`、`firmware/n32h7xx_std_periph_driver` |
| 头文件搜索路径 | 板级 `config.gni` 的 `board_include_dirs`，以及 SoC `BUILD.gn` 的 `include_dirs` | 编译器 | 三行 firmware 路径 |
| **链接脚本（.ld）** | `device/board/<厂商>/BUILD.gn` 的 `config("public") { ldflags }` | 最终链接 | `-Wl,-T device/soc/Nsing/n32h7xx/liteos_m/board.ld` |
| 启动文件 | SoC `src/startup_*.s` + `system_<芯片>.c`，列入 `kernel_module` 的 `sources` | 编译器 | `src/startup_n32h78x_cm7_gcc.s` |
| SDK 的外设 API | 只在 SoC 薄封装 `.c` 里调用 | 驱动与应用**不直接调** | `n32_i2c_hw.c` 调 `I2C_Init()`、`ADC_Init()` |
| 内核配置 | 官方用 `<板>/target_config.h` 覆盖 `los_config.h`；本工程用 `kernel_configs/*.config` | 内核 | 见 2.3 步骤 2 |

三条硬约束：

1. **`-nostartfiles`、`-nostdlib` 与 `-T` 必须成套给。** 本工程把它们统一放在 `device/board/<厂商>/BUILD.gn` 的 `config("public")` 里，通过 `//device/board/<厂商>:public` 传给最终链接。缺 `-T` → 用默认脚本、内存布局错；缺 `-nostdlib` → 链进 libc 启动、进不到 `main`。
2. **SDK 的 `.ld` 不要直接改，改了就等于改了厂商事实。** 需要调整堆、向量表、段布局时，把 `board.ld` 复制到 SoC 目录再改，并保留来源注释。
3. **内核配置二选一，不要混用。** 官方路径是 `target_config.h`（会覆盖 `los_config.h` 的默认值）；本工程走 `kernel_configs/*.config`（Kconfig）。两处同时写同一个宏，会出现"改了没生效"。

> 与官方示例的差异：官方文档给的是 `executable("OHOS_Image.elf")` + 自己在 `ldflags` 里写 `-T`、再用 `build_ext_component` 调 `objcopy`；
> 本工程用 `kernel_module` 编译、由板级 `config("public")` 统一提供链接参数、`objcopy` 手工执行。两者等价，区别只在"谁提供链接参数"。

### 2.5 Gate G1

| 检查项 | 判据 |
|---|---|
| `hb set` | 能列出并选中你的产品 |
| 组件被识别 | `ls $OUT/build_configs/` 下出现 `applications.json` 等，且含你的组件 |
| 编译推进 | 日志出现 `[NINJA] [n/m]` 编译条目 |
| 失败位置 | 若失败，失败点应在**链接**阶段，而不是"找不到 device 目录/组件" |

**失败处理**

| 现象 | 原因 | 处置 |
|---|---|---|
| 报找不到 device 目录 | 两侧 `kernel_version` 不一致 | 统一置 `""` |
| 组件未安装 | `bundle.json` 的 `subsystem` 与 `config.json` 不匹配 | 两侧对齐 |
| gn 语法错误、路径被截断 | 源码根目录名含空格 | 目录改名 |
| 编译到一半报头文件缺失 | `board_include_dirs` 不全 | 补 SoC/SDK 头路径 |

---

## 3 S2 启动链打通

### 3.1 目标与顺序

目标：串口能稳定出字，调度器跑起来，心跳周期稳定。

**必须按以下顺序做，每一步都有可观测结果。** 顺序错了，故障会互相掩盖。

| 步 | 做什么 | 写在哪 | 期望现象 |
|---|---|---|---|
| 1 | 启动汇编 + 向量表 | `$SOC/liteos_m/src/startup_*.s` | 能进 C |
| 2 | 时钟 + 释放从核 | `$BRD/board/src/main.c` 第一步 | — |
| 3 | 早期指示灯 | 同上第二步 | 灯亮 |
| 4 | 串口初始化 | 同上第三步 | 出字 |
| 5 | 内核初始化 | `LOS_KernelInit()` | 打印 OK |
| 6 | HDF 启动 | `DeviceManagerStart()` | 返回 0 |
| 7 | 引导层 | `OHOS_SystemInit()` | 应用被拉起 |
| 8 | 调度器 | `LOS_Start()` | 心跳出现 |

### 3.2 主入口模板

```c
LITE_OS_SEC_TEXT_INIT int main(void)
{
    BoardClockInit();          /* 1. 系统时钟 + 释放从核 */
    BoardLedEarlyOn();         /* 2. 早期指示灯，不依赖任何外设初始化 */
    UartInit();                /* 3. 早期串口，必须早于所有可能失败的动作 */
    /* 4. 所有引脚复用配置放这里 —— 厂商 GPIO 初始化函数不能在内核起来后调用 */

    if (LOS_KernelInit() != LOS_OK) {
        while (1) { }          /* 5. 内核失败就地停住，不要继续 */
    }

    ret = (UINT32)DeviceManagerStart();   /* 6. 解析 hcs、跑驱动 Bind/Init */
    if (ret != 0) {
        BootPuts("HDF start FAILED\r\n");  /* 必须打印，否则后面查不到 */
    }

    OHOS_SystemInit();         /* 7. 六层 initcall，应用在其中被拉起 */
    LOS_Start();               /* 8. 不返回 */
}
```

### 3.3 三个必须现场调对的参数

**（1）内核堆大小** —— 写在链接脚本里。

```ld
_Min_Heap_Size = 0x17000;   /* 92 KB，按实际 RAM 调整 */
```

判据（不能看文件大小，堆是 NOLOAD 段）：

```bash
arm-none-eabi-nm $OUT/OHOS_Image | grep __heap
# 期望：__heap_end - __heap_start == _Min_Heap_Size
```

若内核初始化失败并打印定时器相关错误，先加堆。

**（2）中断向量表对齐** —— 写在 `kernel_configs` 与链接脚本两处。

```
LOSCFG_ARCH_HWI_VECTOR_ALIGN = 0x800
```

要求：`对齐值 ≥ 向上取 2 的幂(向量数量 × 4)`。例：432 条向量 → 1728 字节 → 取 2048。

**（3）C 与汇编的浮点判定一致** —— 见 2.3 步骤 1 的强制项，验证方法：

```bash
arm-none-eabi-objdump -d $OUT/OHOS_Image | grep -E 'vstmdb|vldmia'
# 期望：出现 {d8-d15} 之类的批量存取指令
```

若没有这两条，而 C 侧用了硬浮点，运行期必崩在第一个任务切换处。

### 3.4 启动日志基准

打通后必须能复现以下日志（数值可不同，结构必须一致）：

```
[N32H7] BUILD=v<版本>
[N32H7] [1/7] clock    : ...
[N32H7] [2/7] early led: ...
[N32H7] [3/7] console  : ...
[N32H7] [4/7] kernel   : LOS_KernelInit() OK
[N32H7] [5/7] hdf      : DeviceManagerStart()
[N32H7]                  ret = 0x00000000
[N32H7] [6/7] bootstrap: OHOS_SystemInit()
[N32H7] app      : SYS_RUN entry ran
[N32H7] [7/7] scheduler: LOS_Start()
Entering scheduler
[N32H7] app: heartbeat 0
```

### 3.5 Gate G2

| 检查项 | 判据 |
|---|---|
| 串口 | 能稳定打印上述日志 |
| 内核 | `LOS_KernelInit() OK` |
| HDF | `ret = 0x00000000` |
| 应用 | `SYS_RUN entry ran` |
| 心跳 | 连续 10 跳，周期偏差 < 2% |
| 堆 | `nm` 差值等于链接脚本设定值 |
| 浮点 | `objdump` 能看到 `{d8-d15}` 批量存取 |

**失败处理**

| 现象 | 第一个要查的 | 处置 |
|---|---|---|
| 完全无输出 | 从核是否已释放复位 | 在时钟初始化里释放，再试 |
| 有输出但乱码 | 波特率/时钟源 | 核对 `OS_SYS_CLOCK` 与实际 HCLK |
| 到内核就停 | 堆太小 | 增大 `_Min_Heap_Size` 并 `nm` 复查 |
| 内核起来后一访问外设就 Type=11 | 向量表对齐 | 改为 `0x800` 并对齐链接脚本 |
| 进调度器崩、PC 形如 `0xAA0000xx` | 浮点上下文 | 补齐汇编侧 `-mfpu` / 显式定义宏 |
| 应用没跑 | `OHOS_SystemInit()` 是否调用 | 补上；并确认 `run` 层有注册 |

> 以上任一现象的具体原理与更多失效模式，见技术说明书第 4 章。

---

## 4 S3 HDF 框架接通

### 4.1 目标

不用写任何驱动业务逻辑，先让 HDF 跑起来：设备树能解析、节点能绑定、日志能看到"驱动被拉起"。

> **选型说明（官方给了两条路）**：轻量系统的外设驱动有两条适配路径——
> ① **`iot_hardware` 子系统**（`base/iot_hardware/peripheral/interfaces/kits` 下的 `iot_gpio.h`/`iot_i2c.h`/`iot_pwm.h`，
> 厂商在 `hals/iot_hardware/` 下实现，官方明确"资源有限时**建议**用这种方式"）；
> ② **HDF 驱动框架**（本工程所采用）。
>
> 选 HDF 的理由：与 6.1 的 `device/soc` + `device/board` 分层一致；hcs 使参数与代码分离；驱动可整份跨芯片复用；
> 后续要做服务化形态时不用推倒重来。
> 代价：镜像更大（框架 + hcs blob），接线步骤更多（三要素 + 两段式 hcs）。
> 若目标仅为最小功能验证（点灯或单传感器读取），`iot_hardware` 所需步骤更少。二者属工程取舍，不涉及正确性。

### 4.2 hcs 两段式模板（只替换名称）

**第一段**：`$VEN/hdf_config/device_info/device_info.hcs`

```hcs
root {
    device_info {
        match_attr = "hdf_manager";
        template host {
            hostName = "";
            priority = 100;
            template device {
                template deviceNode {
                    policy = 0;
                    priority = 100;
                    preload = 0;
                    permission = 0664;
                    moduleName = "";
                    serviceName = "";
                    deviceMatchAttr = "";
                }
            }
        }
        platform_host :: host {
            hostName = "platform_host";
            priority = 100;

            <外设>_device :: device {
                <外设>0 :: deviceNode {
                    policy = 0;                          /* 见 4.3 的选择表 */
                    priority = 100;
                    preload = 0;
                    permission = 0660;
                    moduleName = "<厂商>_<外设>_MODULE_HDF"; /* 与驱动入口逐字相同 */
                    serviceName = "";                    /* policy=0 时必须为空 */
                    deviceMatchAttr = "<厂商>_<外设>";    /* ★ 不能留空 */
                }
            }
        }
    }
}
```

**第二段**：`$VEN/hdf_config/<外设>/<外设>_config.hcs`

```hcs
root {
    platform {
        <外设>_config {
            match_attr = "<厂商>_<外设>";   /* ★ 与上面的 deviceMatchAttr 同一个串 */
            /* 在这里放该外设的所有参数 */
        }
    }
}
```

**第三段（容易漏）**：`$VEN/hdf_config/hdf.hcs` 必须包含第二段。

```hcs
#include "device_info/device_info.hcs"
#include "<外设>/<外设>_config.hcs"
```

> **为什么必须两段**：设备节点的参数不是从该节点自身读取的，而是按 `deviceMatchAttr` 在整棵树里反查（`hdf_device_node.c:287`）。`deviceMatchAttr` 留空的后果是：驱动照常启动、参数全部拿不到、静默使用默认值——最难发现的一类配置错误。

**`permission` 的含义。** 它是 HDF 为设备节点声明的**访问权限位**，采用八进制写法，语义与文件权限相同
（`0644` = 属主读写、其他只读；`0660` = 属主与同组读写）。框架在把 service 发布给客户端时用它做访问控制。

在 mini 系统上，它当前**没有实际约束力**：没有用户态进程、没有 UID/GID 隔离，所有代码同处一个地址空间。
但仍应填一个合理值，两个原因：

1. 它是设备节点声明的一部分，缺省/乱填在换到支持用户态的形态（small/standard）或其他内核时会暴露问题；
2. 它同时表达该服务的用途约定：`0660` 表示"只给同组用"，`0644` 表示"谁都能读"，读 hcs 时就能判断这个服务的意图。

### 4.3 policy 怎么选

| 外设类型 | policy | serviceName | 例子 |
|---|---|---|---|
| 控制器（按总线号取） | `0` | 空 | I2C、GPIO |
| 服务（按名字取） | `2` | `HDF_PLATFORM_<外设>_<序号>` | UART、PWM |

判据：应用侧是用 `I2cOpen(busId)` 还是 `<X>Open(num)` 取设备。前者走控制器数组，后者走服务名。

### 4.4 检查驱动是否进了镜像

```bash
arm-none-eabi-nm $OUT/OHOS_Image | grep _hdf_drivers
```

**符号含义。** `_hdf_drivers_start` / `_hdf_drivers_end` 并非变量，而是链接脚本在 `.hdf.driver` 段两侧
定义的**地址标记**（`board.ld`）：

```ld
_hdf_drivers_start = .;      /* 记下当前位置的地址 */
KEEP(*(.hdf.driver))         /* 把所有 HDF_INIT() 写进来的指针连排放在这里 */
_hdf_drivers_end = .;        /* 记下段尾地址 */
```

`HDF_INIT(driver)` 的展开结果是一条 `__attribute__((section(".hdf.driver")))` 的指针定义，
所以这一段里装的是**一串 `HdfDriverEntry *`**，没有别的东西。

**差值的含义。** 两个符号之差即该段占用的字节数：

```
0x15027cc4 (_end) - 0x15027cb0 (_start) = 0x14 = 20 字节
```

本工程为 32 位目标，单个指针占 4 字节，因此：

```
20 字节 / 4 字节 = 5 个驱动
```

对应 GPIO、UART、I2C 管理器、I2C 控制器、PWM。**每新增一个 HDF 驱动，这个数只会 +4**；
如果加了驱动但差值不变，说明它没进镜像。

**采用该判据的原因。** 源码里有 `HDF_INIT()` 不代表进了镜像，两种常见原因：
源文件没被 `BUILD.gn` 收录；或者虽然编进去了，但这段没有被任何代码直接引用，`--gc-sections` 把它回收了
（`KEEP` 的作用即在于此）。`nm` 读取的是链接结果，不依赖源码表象。

### 4.5 Gate G3

| 检查项 | 判据 |
|---|---|
| hcs 解析 | 无解析错误日志 |
| 驱动被拉起 | 驱动的 `Init()` 日志出现，且位置在 `DeviceManagerStart()` 之后 |
| 参数被读到 | **没有**"使用默认值"类提示 |
| 驱动表 | `.hdf.driver` 段长度按驱动数增加 |
| HDF 返回 | `DeviceManagerStart()` 返回 0 |

**失败处理**

| 现象 | 原因 | 处置 |
|---|---|---|
| 驱动完全没启动、无日志 | `moduleName` 与 hcs 不一致 | 逐字符比对；用 `nm` 看驱动表是否变长 |
| 驱动启动了但参数全是默认值 | `deviceMatchAttr` 为空 / 无同名 `match_attr` / 未 include | 按 4.2 三段检查 |
| 注册接口返回 `NOT_SUPPORT` | 控制器类外设缺管理器节点 | 补 `HDF_PLATFORM_<外设>_MANAGER` 节点，`priority` 设小 |
| 初始化成功但设备打不开 | `policy=2` 但未创建服务对象 | 显式调 `PlatformDeviceCreateService()` |
| 改 hcs 无任何变化 | `hdf.hcb` 未重新生成 | 看 `$OUT/gen/vendor/.../hdf_config/hdf.hcb` 时间戳 |

---

## 5 S4 第一个平台驱动

### 5.1 目标

写一个能读出数据/能驱动外设的 HDF 平台驱动，并通过"数据回路"验证。

**薄封装的定位。** 它是一层只完成翻译、不承担业务的中间层：

- **向上**（驱动、应用）：只看到 `n32_<外设>_hw.h` 里的函数原型和常量，看不到任何厂商类型、寄存器、宏；
- **向下**（厂商 SDK）：在 `.c` 里 `#include` 厂商头、调用 SDK 的 API、必要时直接写寄存器。

其作用可概括为：**将厂商 SDK 的调用方式封装为本工程自有的稳定接口。**

为什么必须有这一层，三个理由，按重要性排：

| 理由 | 说明 | 本工程证据 |
|---|---|---|
| 头文件冲突 | HDF 头链最终引入 `soc.h`，它和厂商设备头都声明 `enum IRQn`，两侧都没有保护宏 | 驱动里同时引两者必定 `redefinition` |
| 硬件语义集中 | 单位换算、地址编码、引脚复用这些都只在这一层出现，改硬件只改这里 | I2C 的 `<<1`、PWM 的 ns→tick、ADC 的 mV→℃ |
| 换 SDK 版本只动一层 | 厂商 API 改名/改签名时，只有薄封装要跟 | 1.2.0 → 1.3.0 升级时只重做 firmware 与薄封装 |

**职责边界**：不承担任务调度、不做数据缓冲、不解析 hcs 参数。输入为已完成换算的数值，输出即返回结果。

### 5.2 四个文件（模板，不要合并）

| 文件 | 放哪 | 职责 | 能否引厂商 SDK 头 |
|---|---|---|---|
| `n32_<外设>_hw.h` | `$SOC/liteos_m/include/` | 只放函数原型与常量 | **不能** |
| `n32_<外设>_hw.c` | `$SOC/liteos_m/src/` | 寄存器级实现 | 可以 |
| `<外设>_n32h7xx.c` | `$SRC/drivers/hdf_core/adapter/platform/<外设>/` | HDF 驱动 | **不能** |
| `<外设>_config.hcs` | `$VEN/hdf_config/<外设>/` | 参数 | — |

> **为什么不能合并**：HDF 头文件链最终会引入 `soc.h`，其中声明了 `enum IRQn`；厂商设备头也声明同名枚举，两侧都没有保护宏。同一个编译单元同时引入必定重定义。

### 5.3 驱动骨架

```c
/* $SRC/drivers/hdf_core/adapter/platform/<外设>/<外设>_n32h7xx.c */
#include "device_resource_if.h"
#include "hdf_device_desc.h"
#include "hdf_core_log.h"
#include "<外设>_core.h"          /* 该外设的 HDF 核心头，如 i2c_core.h / pwm_core.h */
#include "osal_mem.h"
#include "n32_<外设>_hw.h"        /* 只引薄封装，不引厂商头 */

#define HDF_LOG_TAG <外设>_n32h7xx

struct N32<外设>Res {
    /* 从 hcs 读到的参数，按外设定义 */
};

static int32_t N32<外设>Bind(struct HdfDeviceObject *device)
{
    if (device == NULL) { return HDF_ERR_INVALID_PARAM; }
    /* 按框架要求分配对象并挂到 device->service；具体类型见该外设 core 头 */
    return HDF_SUCCESS;
}

static int32_t N32<外设>GetResource(struct N32<外设>Res *res, const struct DeviceResourceNode *node)
{
    struct DeviceResourceIface *dri = NULL;

    /* 先写默认值，再尝试覆盖：默认值必须与 hcs 保持一致 */
    if (node == NULL) {
        return HDF_SUCCESS;              /* 参数缺失不阻塞 Init，但要能看出来 */
    }
    dri = DeviceResourceGetIfaceInstance(HDF_CONFIG_SOURCE);
    (void)dri->GetUint32(node, "<参数名>", &res-><字段>, <默认值>);
    return HDF_SUCCESS;
}

static int32_t N32<外设>Init(struct HdfDeviceObject *device)
{
    /* 1. 读参数  2. 首次调用时初始化硬件  3. 注册到框架  4. 打一行日志 */
    return HDF_SUCCESS;
}

static void N32<外设>Release(struct HdfDeviceObject *device)
{
    /* 反向注销 + 释放 */
}

struct HdfDriverEntry g_N32<外设>DriverEntry = {
    .moduleVersion = 1,
    .moduleName    = "<厂商>_<外设>_MODULE_HDF",   /* 与 hcs 逐字相同 */
    .Bind          = N32<外设>Bind,
    .Init          = N32<外设>Init,
    .Release       = N32<外设>Release,
};
HDF_INIT(g_N32<外设>DriverEntry);
```

**三个回调的边界（写错必出问题）**

| 回调 | 只做 | 绝对不要做 |
|---|---|---|
| `Bind` | 参数校验、分配对象 | 初始化硬件 |
| `Init` | 读 hcs、注册、申请资源 | **调厂商 GPIO/外设初始化函数** |
| `Release` | 注销、释放 | — |

### 5.4 接线三要素（缺一不可）

**① 内核配置开关**：`$VEN/kernel_configs/<产品>.config`

```
LOSCFG_DRIVERS_HDF_PLATFORM=y
LOSCFG_DRIVERS_HDF_PLATFORM_<外设大写>=y
```

**② 构建分支**：`$SRC/drivers/hdf_core/adapter/platform/<外设>/BUILD.gn`

```gn
if (defined(LOSCFG_SOC_COMPANY_<厂商大写>)) {
  sources += [ "<外设>_n32h7xx.c" ]
}
```

同时把薄封装加进 SoC 的构建：`$SOC/liteos_m/BUILD.gn` 的 `sources` 里加 `"src/n32_<外设>_hw.c"`。

**③ hcs 两个节点** —— 控制器类外设还需要管理器节点（见 4.3）。

### 5.5 先确认参数的语义，再写代码

**这一步能省掉最多时间。** 拿到厂商例程后，逐项确认：

| 要确认什么 | 怎么确认 | 本项目实例 |
|---|---|---|
| 引脚与复用号 | 以手册或厂商例程的宏为准，不作推测 | GTIMA1_CH1 = PA0(AF6) |
| 时钟源与分频 | 看例程的 `RCC_*` 与 `*_ConfigClk` | ADC3：AHB + PLL1B + DIV15 |
| 寄存器要求的数值形式 | **看驱动的写入函数是否移位/掩码** | I2C 地址要 8 位形式：0x50 → 0xA0 |
| 单位 | 框架用 ns 还是计数？ | PWM 的 `period/duty` 是**纳秒** |

> **代价最高的一处差异**：外设地址/参数是"原值"还是"编码过的"。厂商例程传 `0xA0` 而框架约定 `0x50` 时，移位必须放在贴合硬件的那一层（薄封装），且**只做一次**。

**引脚映射：为什么在这里做，以及它的局限。**

厂商 SDK 已提供 `GPIO_InitPeripheral()`，此处仍单独维护引脚映射，原因在于两者所处的层次不同：

因为 SDK 提供的是**机制**，不是**策略**：

- `GPIO_InitPeripheral(GPIOx, &init)` 只回答"把这个脚配成某个 AF"；
- 但"**I2C1 用哪两个脚**"这件事，在厂商 SDK 里**没有统一接口可查**——它散落在各个例程的 `main.h` 宏里，
  每个例程一份。换一个外设就要去翻另一份例程。

移植时将该策略集中于 `N32I2cHwPinInit`，使驱动与 hcs 无需处理 AF 编号。
当前实现存在三个局限：

| 局限 | 后果 |
|---|---|
| 只列了 2 个实例（I2C1 / I2C4） | 新增实例必须改代码 |
| 每个实例只写死一组引脚 | 同一实例的**其他可选引脚组合**无法表达（芯片通常支持重映射） |
| 与 hcs 脱节 | hcs 只能选 `port`，不能选引脚 |

**建议做法：表驱动，并支持 hcs 覆盖。**

```c
struct N32I2cPinMap {
    uint8_t  port;
    GPIO_Module *sclPort; uint16_t sclPin; uint32_t sclAf;
    GPIO_Module *sdaPort; uint16_t sdaPin; uint32_t sdaAf;
};

static const struct N32I2cPinMap s_pinMap[] = {
    /* 已验证：SDK I2C_EEPROM 例程 */
    { N32_I2C_PORT_1, GPIOB, GPIO_PIN_6, GPIO_AF11, GPIOB, GPIO_PIN_7, GPIO_AF10 },
    /* 已验证：板上 J6 引出 */
    { N32_I2C_PORT_4, GPIOD, GPIO_PIN_12, GPIO_AF10, GPIOD, GPIO_PIN_13, GPIO_AF8 },
};
```

改造后，新增实例或新增一组可选引脚只需增加一行表项，无需修改逻辑。进一步可将 `sclPin`、`sclAf` 等
作为可选参数写入 hcs，实现仅通过配置更换引脚。

> 待补：每个 I2C 实例的**全部**可选引脚组合，只能从芯片数据手册的复用表得到。当前仅录入两组已验证组合，
> 因此该表处于部分覆盖状态；补全属数据录入，不涉及代码改动。

### 5.6 验证三板斧（顺序固定）

**① 日志**：驱动 `Init()` 与注册成功的日志出现。

**② 数据回路**：读回来的值必须与写入一致，或与手册预期一致。只看"没报错"不算通过。

| 外设 | 回路判据 |
|---|---|
| I2C EEPROM | 写 8 字节 → 读回 8 字节逐字节相同 |
| PWM | 示波器：频率符合设定，占空比随设置变化 |
| ADC | 读数落在合理区间，且随环境变化 |
| GPIO | 电平随写入翻转 |

**③ 目标文件**：确认代码真的按预期生成。

```bash
arm-none-eabi-objdump -d $OUT/OHOS_Image | grep -A3 '<你的函数名>'
arm-none-eabi-nm $OUT/OHOS_Image | grep _hdf_drivers
```

### 5.7 Gate G4

| 检查项 | 判据 |
|---|---|
| 三要素齐全 | 开关 / 构建分支 / hcs 节点 都在 |
| 参数被读到 | 无默认值提示；日志能打出读到的值 |
| 驱动表 | 长度增加 4 字节 |
| 数据回路 | 通过（见 5.6 ②） |
| 引脚配置位置 | 在主入口，早于 `LOS_KernelInit()` |
| 头文件隔离 | 驱动文件不含厂商设备头 |

**第二个驱动开始，只需替换 5.2 的四个文件与 5.4 的三处接线，其余流程完全一样。** 已有实现可作参照：

| 外设 | 取设备方式 | 关键差异 |
|---|---|---|
| GPIO | `GpioCntlrAdd()` | 需要 `PlatformDeviceCreateService()` |
| UART | `UartHostCreate()`，按服务名 | `policy=2` |
| I2C | `I2cCntlrAdd()`，按总线号 | 需要管理器节点；地址要移位 |
| PWM | `PwmDeviceAdd()`，按服务名 | 不需要管理器；周期单位 ns |

---

## 6 S5 应用 demo

### 6.1 目标

写一个注册在 `SYS_RUN` 的任务，通过**平台接口**使用驱动，输出可判读的结果。

### 6.2 模板

```c
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "uart_if.h"
#include "<外设>_if.h"          /* 只引平台接口头 */

#define APP_<外设>_PRIO 9
static DevHandle g_handle = NULL;

static void *App<外设>Task(void *arg)
{
    (void)arg;
    osDelay(MsToTicks(1500));            /* 等日志/串口就绪 */

    g_handle = <外设>Open(<序号>);
    if (g_handle == NULL) {
        UartPuts("[APP] open failed\r\n");
        return NULL;
    }
    UartPuts("[APP] opened\r\n");

    while (1) {
        /* 1. 用平台接口操作设备
           2. 打印结果（内核起来后可用 printf；早期阶段与异常现场用 BootPuts）
           3. osDelay 让出 CPU */
        osDelay(MsToTicks(1000));
    }
    return NULL;
}

static void App<外设>Entry(void)
{
    if (AppCreateTask((osThreadFunc_t)App<外设>Task,
                      (osPriority_t)APP_<外设>_PRIO, "app_<外设>") == NULL) {
        UartPuts("[APP] task create FAILED\r\n");
    }
}
SYS_RUN(App<外设>Entry);
```

**三条硬约束**

1. 应用只 include 平台接口头（`gpio_if.h` / `uart_if.h` / `i2c_if.h` / `pwm_if.h`），**不引厂商头，也不引 SoC 头**。
2. 用 `SYS_RUN` 注册，不从 `main()` 直接调。
3. 输出分两种：**内核启动之前**用板级自实现的串口输出（`BootPuts`，直写寄存器，不依赖堆）；
   **内核建立之后**可正常使用 `printf`（HILOG 的默认输出即 `printf`）。异常现场优先用无缓冲输出。

### 6.3 Gate G5

| 检查项 | 判据 |
|---|---|
| 任务被拉起 | 启动日志里出现应用的注册与首行输出 |
| 输出可判读 | 打印内容能直接判断对错，而非"运行中"一类无信息量的输出） |
| 周期稳定 | 连续 10 次输出间隔偏差 < 5% |
| 无阻塞 | 其他任务（心跳等）不受影响 |

---

## 7 S6 XTS 兼容性测试

### 7.1 目标

跑通目标套件，得到"用例数 / 失败数"的明确结果。

### 7.2 官方 XTS 体系（6.1）

| 项目 | 内容 |
|---|---|
| 组成 | `acts`（用例）+ `tools`（框架）；后续扩展 `dcts`（设备兼容性） |
| 轻量系统框架 | **hctest**（在 unity 基础上增强），用例用 C 编写；小型/标准系统用 hcpptest / HJSUnit |
| 用例三要素 | `LITE_TEST_SUIT(子系统, 模块, 套件名)`、`LITE_TEST_CASE(套件, 用例, 属性)`、`RUN_TEST_SUITE(套件)` |
| 用例属性 | 类型（Function/Performance/…）\| 粒度（Small/Medium/Large）\| 级别（Level0–Level4） |
| 目录 | `test/xts/acts/subsystem_lite/<模块>/`；编译配置在 `acts/build_lite/BUILD.gn` |
| 链接方式 | acts 编成静态库，随 **debug** 版本链进镜像——不是独立工程 |
| 日志 | 套件开始 `Start to run test suite`，结束 `xx Tests xx Failures xx Ignored` |

**轻量系统必须开的小型化 Feature**（官方《轻量系统小型化适配指导》给的建议值）：

| Feature | 说明 | 默认 | 轻量建议 |
|---|---|---|---|
| `hctest_rodata_opt` | 测试套描述放 `.rodata`，省 RAM | false | **true** |
| `xts_overlay` | 多模块 `.bss` 复用同一 overlay 区 | false | **true** |
| `hctest_task_stack_size` | 测试任务栈 | 6144 | **2048** |
| `hctest_task_queue_size` | 测试任务队列 | 20 | **1** |
| `hctest_task_type` | 单任务 / 共享任务 | `SINGLE_TASK` | **`SHARED_TASK`** |

配置二选一，命令行：

```bash
hb build --gn-args hctest_rodata_opt=true xts_overlay=true \
  hctest_task_stack_size=2048 hctest_task_queue_size=1 'hctest_task_type="SHARED_TASK"'
```

或写进产品 `config.json` 的 `xts/acts` 组件 `features` 数组。

> 实测结论：前两项**不能直接开启**。`hctest_rodata_opt` 与 `xts_overlay` 会切换到新的 runner，
> 该 runner 要求链接脚本为**每个测试模块**提供 `xts_init_<套件>_start` / `_stop` 以及
> `xts_overlay_start` / `_stop` 符号（官方构建脚本按模块注入）。本工程 `board.ld` 未包含对应段，
> 开启后链接报 `undefined reference to 'xts_init_ActsBootstrapTest_start'` 与 `'xts_overlay_start'`。
>
> 后三项运行期参数经实测同样不可直接采用：栈 2048、队列 1、`SHARED_TASK` 下，ActsSamgrTest 的 82 条用例出现 **6 条失败**
（ShareTask 2 条、Broadcast02 4 条，均属异步时序类），并伴随 `SharedSend failed(-6)` 与 hilog 缓存溢出刷屏。
已全部回退默认值（栈 6144、队列 20、`SINGLE_TASK`）。
> **结论：官方给的小型化建议值并非全部可直接套用，需逐项验证链接与运行。**


Feature 是否生效，用符号验证：

```bash
nm $OUT/OHOS_Image | grep xts_init_Acts    # 每个模块一对 _start/_stop 符号
nm $OUT/OHOS_Image | grep xts_overlay      # overlay 区起止符号
```

### 7.3 固件划分

XTS 固件与 demo 固件必须**分开构建、分开归档**，不合并为一个镜像：

| 固件 | 构建参数 | 内容 | 归档目录 |
|---|---|---|---|
| demo 固件 | 默认（`app_demo=true`） | 业务任务：LED、心跳、I2C、PWM、ADC | `output/v<版本>/` |
| XTS 固件 | `build_xts=true xts_suite=<套件> app_demo=false` | 仅测试框架 + 指定套件 | `output/xts_v<版本>/<套件>/` |

两条理由：

1. **测试期间不应有业务任务。** `app_demo=false` 通过 `app/BUILD.gn` 的开关把 `APP_DEMO_TASKS` 置 0，
   四个 `SYS_RUN` 入口整段不注册——业务任务不抢 CPU、不占栈、不污染日志。
2. **一个镜像只装一个套件。** `xts_suite` 是字符串参数（`device/board/<厂商>/BUILD.gn`），
   每个套件编译成独立静态库并由 whole-archive 拉入；套件清单见 `test/xts/acts/build_lite/BUILD.gn`。
   出多个固件，而不是把全部套件塞进一个镜像。

判据：

```bash
nm $OUT/OHOS_Image | grep -c 'AppDemoEntry'     # 应为 0
# 启动日志：MODULE_INIT(run) : 0 entry（无业务入口）
```

### 7.4 启用

**步骤 1**：在 `$VEN/config.json` 的组件清单里加一行：

```json
{ "subsystem": "xts", "components": [{ "component": "acts" }] }
```

**步骤 2**：确认编译条件：

- 使用 **debug** 配置；
- 构建参数含 `build_xts=true`。

> XTS 会显著增大镜像（本项目的 `.bss` 增长数 KB）。若启动失败，先复查堆大小（3.3 节）。

### 7.5 编译与烧录

```bash
hb build            # 或按你的产品名构建
# 产物在 $OUT/OHOS_Image，转 hex/bin 后烧录
```

### 7.6 结果判读（按顺序看四行）

```
[xts] suite=<套件名>, N suite entry(s) registered
[xts] <用例名>  PASS / FAIL
tests: <n>   failures: <n>
```

判读顺序：**套件名对不对 → 注册条目数是否合理 → 逐条结果 → 汇总行**。

本项目实测基准（5 个套件全部通过）：

| 套件 | 用例 | 失败 |
|---|---|---|
| ActsSamgrTest | 82 | 0 |
| ActsParameterTest | 57 | 0 |
| ActsDfxFuncTest | 7 | 0 |
| ActsBootstrapTest | 7 | 0 |
| ActsHieventLiteTest | 4 | 0 |
| **合计** | **157** | **0** |

### 7.7 Gate G6

| 检查项 | 判据 |
|---|---|
| 套件被执行 | 日志出现套件名与逐条用例 |
| 无重复执行 | 同一套件的用例**只出现一遍** |
| 结果 | 目标套件 failures = 0 |

**失败处理**

| 现象 | 原因 | 处置 |
|---|---|---|
| 套件跑了两次 | 主入口也遍历了 `test` 层 | 删掉主入口的遍历，只保留测试框架的单一执行点 |
| 一个用例都没跑 | 组件未启用或未按 debug 构建 | 检查 `config.json` 与构建参数 |
| 跑到一半复位 | 堆/栈不足 | 增大堆，复查 `nm` |
| 大量用例失败 | 依赖的子系统未启用 | 按失败用例的依赖补组件（如参数、日志） |

---

## 8 S7 交付

### 8.1 交付物

| 序号 | 交付物 | 内容 |
|---|---|---|
| 1 | 固件 | `OHOS_Image.hex` / `.bin`，附版本串 |
| 2 | 源码改动清单 | 按目录分组，标明新增/修改 |
| 3 | 验证记录 | 启动日志、驱动回路日志、XTS 结果，各一份 |
| 4 | 本文档填写后的检查清单（附录 C） | 逐项打勾并签字 |

### 8.2 交付前三件事

```bash
# 1. 全量重编，记录目标文件数与 ELF 时间戳
find $OUT/obj -name '*.o' -delete && hb build
find $OUT/obj -name '*.o' | wc -l
ls -la $OUT/OHOS_Image

# 2. 确认版本串与调试开关
strings $OUT/OHOS_Image | grep BUILD=
strings $OUT/OHOS_Image | grep -c "no hcs node"    # 期望 0（说明参数确实读到了）

# 3. 确认驱动表与堆边界
arm-none-eabi-nm $OUT/OHOS_Image | grep -E '_hdf_drivers|__heap'
```

### 8.3 Gate G7

附录 C 清单全部勾选，且三条命令输出符合预期。

---

## 附录 A 命令速查

```bash
# 构建
export PATH=$HOME/.local/bin:$PATH
hb set -p <产品名>
hb build

# 全量重编（改了头文件/配置后必须做）
find $OUT/obj -name '*.o' -delete

# 产物
arm-none-eabi-objcopy -O ihex   $OUT/OHOS_Image out.hex
arm-none-eabi-objcopy -O binary $OUT/OHOS_Image out.bin

# 验证
arm-none-eabi-nm $OUT/OHOS_Image | grep _hdf_drivers
arm-none-eabi-nm $OUT/OHOS_Image | grep __heap
arm-none-eabi-objdump -d $OUT/OHOS_Image | grep -A5 '<函数名>'
arm-none-eabi-objdump -d $OUT/OHOS_Image | grep -E 'vstmdb|vldmia'
arm-none-eabi-objdump -s --start-address=<地址> --stop-address=<地址+8> $OUT/OHOS_Image
strings $OUT/OHOS_Image | grep -E 'BUILD=|<你的日志前缀>'
arm-none-eabi-readelf -S $OUT/OHOS_Image | grep -i <段名>

# 双机同步（示例）
rsync -az --delete <本地子集>/ <user>@<build-host>:<远端路径>/
```

**离线语法自检**（不联编、不联网，改完先过一遍）：

```bash
arm-none-eabi-gcc -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
  -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard \
  -D<芯片宏> -DCORE_CM7 -DUSE_STDPERIPH_DRIVER \
  -I<薄封装 include> -I<CMSIS core> -I<CMSIS device> -I<driver inc> \
  n32_<外设>_hw.c
```

---

## 附录 B 失败对照表

| # | 现象 | 根因 | 处置 | 阶段 |
|---|---|---|---|---|
| 1 | 报找不到 device 目录 | 两侧 `kernel_version` 不一致 | 统一置空 | S1 |
| 2 | gn 语法错误 / 路径被截断 | 目录名含空格 | 改名 | S1 |
| 3 | 组件未安装 | `bundle.json` 的 subsystem 不匹配 | 对齐 | S1 |
| 4 | 完全无串口输出 | 从核未释放复位 | 时钟初始化里释放 | S2 |
| 5 | 输出乱码 | `OS_SYS_CLOCK` 与实际 HCLK 不符 | 改正并重编 | S2 |
| 6 | 内核初始化失败 | 堆不足 | 增大 `_Min_Heap_Size`，`nm` 复查 | S2 |
| 7 | 内核后一访问外设即 Type=11 | 向量表对齐不足 | 改 `0x800` + 链接脚本对齐 | S2 |
| 8 | 进调度器崩、PC 含 `0xAA0000xx` | C/ASM 浮点判定不一致 | 汇编侧补 `-mfpu` 或显式定义宏 | S2 |
| 9 | 改头文件后行为不变 | 构建系统不跟踪头依赖 | 删 `.o` 全量重编 | 全程 |
| 10 | 驱动不启动且无日志 | `moduleName` 不匹配 | 逐字比对；查驱动表长度 | S3 |
| 11 | 参数全是默认值 | 两段式没写对 | 查 `deviceMatchAttr` / `match_attr` / include | S3 |
| 12 | 注册接口返回 NOT_SUPPORT | 缺管理器节点 | 补节点，`priority` 设小 | S3 |
| 13 | 初始化成功但设备打不开 | 未创建服务对象 | 显式 `PlatformDeviceCreateService()` | S3 |
| 14 | 外设初始化时内存异常 | 在 `Init()` 里调厂商 GPIO 函数 | 引脚配置移到主入口 | S4 |
| 15 | 数据写进去读不出 | 参数语义错误（原值 vs 编码值） | 按 5.5 逐项确认 | S4 |
| 16 | 判断"成功"实际失败 | 与 0 比较 | 改为 `!= 0` / `== SUCCESS` | 全程 |
| 17 | XTS 套件跑两遍 | 两处遍历同一注册表 | 保留单一执行点 | S6 |

---

## 附录 C 检查清单（可打印）

### C.1 环境（S0）

- [ ] `hb` / `gn` / `ninja` / `arm-none-eabi-gcc` / `objcopy` 全部可见
- [ ] 可用空间 ≥ 50 GB
- [ ] 裸机例程能出串口
- [ ] ssh / scp 免交互可用

### C.2 工程骨架（S1）

- [ ] 三层目录建齐
- [ ] 两侧 `kernel_version` 一致
- [ ] `board_cflags` 与 `board_asmflags` 的浮点选项一致
- [ ] `config.json` 组件清单完整
- [ ] 应用 `bundle.json` 的 subsystem 与产品配置一致
- [ ] `hb set` 能选中产品
- [ ] 失败点位于链接阶段

### C.3 启动链（S2）

- [ ] 时钟与从核释放完成
- [ ] 早期指示灯亮
- [ ] 串口出字，无乱码
- [ ] `LOS_KernelInit() OK`
- [ ] `nm` 验证堆差值等于设定值
- [ ] 向量表按 `0x800` 对齐
- [ ] `objdump` 能看到浮点批量存取指令
- [ ] `DeviceManagerStart()` 返回 0
- [ ] `SYS_RUN` 应用被执行
- [ ] 心跳连续 10 次周期偏差 < 2%

### C.4 HDF 与驱动（S3–S4）

- [ ] hcs 两段式三段齐全（节点 / 参数节点 / include）
- [ ] `policy` 与取设备方式匹配
- [ ] 驱动表长度按驱动数增加
- [ ] 驱动日志出现且无默认值提示
- [ ] 四个文件分离，驱动未引厂商头
- [ ] 引脚配置在主入口、内核之前
- [ ] 数据回路通过

### C.5 应用与测试（S5–S6）

- [ ] 应用只用平台接口
- [ ] 输出可直接判读
- [ ] XTS 组件已启用，debug 构建
- [ ] 套件名正确、无重复执行
- [ ] 目标套件 failures = 0

### C.6 交付（S7）

- [ ] 版本串已更新，产物名带版本
- [ ] 全量重编，记录 `.o` 数量与 ELF 时间戳
- [ ] `strings` 确认版本串，确认无"默认值"类提示
- [ ] `nm` 确认驱动表与堆边界
- [ ] 三类验证记录（启动 / 回路 / XTS）齐备
- [ ] 调试开关已关闭

---

## 附录 D 工时与人力

| 阶段 | 第一个平台 | 同系列第二颗 | 说明 |
|---|---|---|---|
| S0 环境 | 0.5–1 天 | 0.5 天 | 第二颗可复用工具链 |
| S1 骨架 | 1–2 天 | 0.5–1 天 | 目录与配置可复制 |
| S2 启动链 | 2–5 天 | 1–2 天 | 最难，取决于芯片差异 |
| S3 HDF 接通 | 0.5–1 天 | 0.5 天 | 模板化 |
| S4 每个驱动 | 2–4 天 | 1–2 天 | 第一个最慢 |
| S5 应用 demo | 0.5 天 | 0.5 天 | 模板化 |
| S6 XTS | 1–2 天 | 0.5–1 天 | 主要耗时在失败归因 |
| S7 交付 | 0.5 天 | 0.5 天 | — |
| **合计** | **8–15 天** | **5–8 天** | 不含认证流程 |

人力：1 人可独立完成；建议配置 1 名硬件/测试配合（示波器、烧录、串口抓取）。

---

*本 SOP 与《技术说明书 v1.1》互为配套：本文回答"怎么做"，说明书回答"为什么"与"坏了怎么想"。*

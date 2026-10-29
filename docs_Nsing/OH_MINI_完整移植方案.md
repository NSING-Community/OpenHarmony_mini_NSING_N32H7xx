# OpenHarmony LiteOS-M（mini）移植完整方案
## 目标：N32H7xx（NSING / 原 Nations）Cortex-M7 单核 + VFP-D16 硬浮点

> 本文是**网上公开资料 + 官方文档 + 实机踩坑**的汇总，作为 `OHOS_N32H7_Mini_Porting_Plan.md`（前人 V1.0.0 计划）的更新替代版本。
> 收集时间：2026-09-10。适用 OpenHarmony 6.1 LTS mini。

---

## 0. 三个层次的移植目标（先明确边界，避免过度设计）

| 层次 | 目标 | 验收标准 |
|---|---|---|
| L1 最小系统 | 内核能在目标 CPU 正确启动 | 串口打印内核 banner，空闲任务运行 |
| L2 资源管理 | 内核能管理时钟/中断/内存/串口 | SysTick 走时正确，动态内存分配可用，异常可打印 |
| L3 上层运行时 | 为应用提供稳定统一环境 | 多任务调度、IPC、软件定时器全部可用 |

**核心思路：「分而治之 + 由底向上」。** 不要一次搞定所有问题，按
`BSP 准备 → 内核最小系统 → 驱动组件 → 系统验证与调优` 推进，每阶段有明确输入/输出/验收。

---

## 1. 环境准备

### 1.1 主机要求

- **强烈建议 Ubuntu 18.04+（推荐 20.04/24.04）原生或 WSL**。用户名/路径**不能含中文和空格**
  （空格会切断 gn/ninja 的 shell 路径 —— 这是 P0 级坑，务必先规避）。
- 内存 ≥ 16 GB，磁盘 ≥ 100 GB。

### 1.2 依赖安装

```bash
sudo apt-get update
sudo apt-get install -y binutils binutils-dev git git-lfs gnupg flex bison gperf \
    build-essential zip curl zlib1g-dev gcc-multilib g++-multilib \
    libc6-dev-i386 lib32ncurses5-dev x11proto-core-dev libx11-dev lib32z1-dev \
    ccache libgl1-mesa-dev libxml2-utils xsltproc unzip m4 bc gnutls-bin python3-pip
```

### 1.3 交叉工具链

```bash
sudo apt-get install -y gcc-arm-none-eabi        # 本工程实测 10.3.1 可用
```

> **注意**：OpenHarmony 官方推荐用 `prebuilts_download.sh` 拉取自带工具链。
> 若跳过全量 prebuilts（本工程做法），需手动补 **gn（2023 版）** 与 **ninja 1.12.0**
> 到 `prebuilts/build-tools/linux-x86/bin/`。`hb` 用 `pip install --user ./build/hb` 安装。

### 1.4 repo 与 hb

```bash
mkdir ~/bin
curl https://gitee.com/oschina/repo/raw/fork_flow/repo-py3 -o ~/bin/repo
chmod a+x ~/bin/repo
pip3 install -i https://repo.huaweicloud.com/repository/pypi/simple requests
export PATH=~/bin:$PATH          # 写入 ~/.bashrc
```

---

## 2. 源码获取

```bash
repo init -u git@gitee.com:openharmony/manifest.git -b master --no-repo-verify
repo sync -c
repo forall -c 'git lfs pull'
bash build/prebuilts_download.sh          # 或按 §1.3 手动补 gn/ninja
```

> **Windows 搬树到 Linux 的 4 个必踩坑**（本工程全部踩过，复现时先查这几项）：
> 1. **CRLF 行尾**：全树 `.sh`/`.py` 变 CRLF → `find . -type f \( -name '*.sh' -o -name '*.py' \) -exec sed -i 's/\r$//' {} +`
> 2. **符号链接丢失**：Windows 上 symlink 退化成"内容是目标路径的普通文件" → 按 git mode 120000 记录还原
> 3. **`.git` 里反斜杠路径**（`gitdir: ..\..\..\.repo\...`）→ 全部 `\` 改 `/`，否则所有子仓 git 失效
> 4. **目录名含空格** → 改名（gn 报 `env: "MINI/vendor/..."` 就是它）

---

## 3. 目录结构（三层 BSP）

```
device/
├── board/<vendor>/<board_name>/liteos_m/
│   ├── config.gni           # 板级编译配置（工具链、CFLAGS、include、链接脚本）
│   ├── board.ld             # 链接脚本（内存布局，最关键）
│   ├── board/               # 板级初始化 + 驱动
│   └── BUILD.gn
└── soc/<vendor>/<soc_name>/
    ├── liteos_m/
    │   ├── config.gni
    │   ├── include/target_config.h    # 内核配置（时钟/Tick/中断/内存）
    │   └── board.ld / src/
    └── firmware/            # 厂商 SDK（CMSIS + std_periph_driver）
vendor/<vendor>/<board_name>/
├── config.json              # 产品定义（kernel_type/kernel_version 必须与 config.gni 一致）
└── ohos.build
```

**`config.json` 与 `config.gni` 的 `kernel_version` 必须完全一致**（本工程都设为 `""`），
否则 `hb set` 找不到 device 目录。

---

## 4. 内核启动流程

```
硬件上电
  → 取复位向量（MSP 初值 + Reset_Handler）
  → Reset_Handler：设栈指针 / 拷 .data / 清 .bss
  → SystemInit()：使能 FPU(CPACR)、设 VTOR、配 PWR、可选 ConfigTcmSize()
  → __libc_init_array()：C 静态构造
  → main()
       ├─ 配系统时钟（RCC_SetSysClkToMode0 等）
       ├─ 初始化调试串口
       ├─ ★ 所有外设 GPIO/UART 初始化（必须在 LOS_KernelInit 之前！）
       ├─ LOS_KernelInit()
       │     ├─ OsMemSystemInit()   堆初始化
       │     ├─ HalHwiInit()        建 RAM 向量表 + SCB->VTOR + 开 BUSFAULT/MEMFAULT
       │     ├─ OsTaskInit()/OsSwtmrInit()/OsTickInit() ...
       │     └─ 创建空闲任务
       └─ 创建业务任务 → LOS_Start()  启动调度
```

### 4.1 必须改动的关键点

#### ① 中断向量表重映射 + **对齐**（★ 本工程最大坑）

LiteOS-M 使用 **RAM 向量表** `g_hwiForm[]`：

```c
// kernel/liteos_m/utils/los_compiler.h
#define LITE_OS_SEC_VEC   __attribute__ ((section(".vector")))

// kernel/liteos_m/arch/arm/common/los_common_interrupt.c
LITE_OS_SEC_VEC
HWI_PROC_FUNC __attribute__((aligned(LOSCFG_ARCH_HWI_VECTOR_ALIGN))) g_hwiForm[OS_VECTOR_CNT] = {0};

// kernel/liteos_m/arch/arm/cortex-m7/gcc/los_interrupt.c : HalHwiInit()
SCB->VTOR = (UINT32)(UINTPTR)hwiForm;
```

**Cortex-M7 要求 VTOR 按「向量表大小向上取 2 的幂」对齐（最小 128 字节）。**

```
OS_VECTOR_CNT = OS_SYS_VECTOR_CNT(16) + LOSCFG_PLATFORM_HWI_LIMIT
```

| HWI_LIMIT | OS_VECTOR_CNT | 表字节数 | 需要对齐 |
|---|---|---|---|
| 224（参考工程） | 240 | 960 | 0x400 (1024) |
| **416（本工程）** | **432** | **1728** | **0x800 (2048)** |

`los_config.h` 的默认值是 **0x100（256）—— 对 432 条表来说太小**。后果：
内核忽略 VTOR 低位，异常取向量取到错误基址（如 0x24000000）→ 跳进垃圾 → **非精确总线错误**，
表现为内核刚使能 BUSFAULT/MEMFAULT 就崩：

```
Type = 11            ← OS_EXC_BF_IMPRECISERR（非精确总线错误）
FaultAddr = 0xABABABAB  ← 占位符，表示地址未知
Phase = exc in task     ← 误报（main 里 runTask 还是占位 TCB）
```

**修复（两处必须同时改）：**

```c
/* target_config.h —— 补上这一行（原本缺失，落了默认值 0x100） */
#define LOSCFG_ARCH_HWI_VECTOR_ALIGN   0x800
```

```ld
/* board.ld —— 显式定义 .vector 输出段并强制对齐（否则是孤儿段，可能只按 0x100 放） */
.vector (NOLOAD) :
{
  . = ALIGN(0x800);
  KEEP(*(.vector))
  . = ALIGN(4);
} >AXI_SRAM
```

验证：`arm-none-eabi-nm OHOS_Image | grep g_hwiForm` → 地址必须 `% 2048 == 0`。

#### ② 堆大小（★ 第二个坑）

内核对象 + 任务栈都从堆里分配。默认值常常太小，症状是：

```
OsSwtmrInit error / LOS_KernelInit FAILED
```

**两种堆模型**：

| 模型 | 配置 | 说明 |
|---|---|---|
| 静态数组自带 | `LOSCFG_SYS_EXTERNAL_HEAP=0` | `g_memStart[LOSCFG_SYS_HEAP_SIZE]` 落在 .bss（参考工程做法，0x10000=64K） |
| 链接脚本区域 | `LOSCFG_SYS_EXTERNAL_HEAP=1` | 用 `__heap_start/__heap_end`，须在 ld 里 `PROVIDE` |

本工程用后者，把 `_Min_Heap_Size` 从 0x2000（8K）提到 **0x1C000（112K）** 后内核初始化通过。

#### ③ NVIC 优先级分组

`HalHwiInit()` 会调用 `NVIC_SetPriorityGrouping(OS_NVIC_AIRCR_PRIGROUP)`。
LiteOS-M 需要 **PRIGROUP=3（NVIC_PRIORITYGROUP_4，全抢占位）**。
Cortex-M7 头文件里 `OS_NVIC_AIRCR_PRIGROUP=7`，**移植时需核对**与厂商 SDK/中断优先级规划是否冲突。

#### ④ Cache

- **D-Cache 默认关闭**（开启后必须处理 MPU/一致性，否则外设寄存器读写可能拿到陈旧数据）。
- I-Cache 走 XIP 安全，可开。
- 若开 D-Cache：外设区（如 0x58000000 GPIO）必须在 MPU 里配 Device 属性，且 DMA 缓冲区需 cache 维护。

#### ⑤ FPU

`SystemInit()` 里：

```c
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
  SCB->CPACR |= ((3UL << (10*2)) | (3UL << (11*2)));   /* CP10/CP11 full access */
#endif
```

- `__FPU_PRESENT` 来自设备头（`n32h7xx.h`）。
- `__FPU_USED` 由**编译选项**推出：`-mfloat-abi=hard` + 有浮点代码时为 1。
- **编译选项必须与链接的库（libc/libgcc）全部一致**：`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard`。
- 参考工程用 `fpv5-sp-d16`（单精度），本工程用 `fpv5-d16`（双精度）；**只要全工程 + 所有库统一即可**，混用会 HardFault。

实测反汇编确认 CPACR 确实被写入：
```
orr.w r2, r2, #0xf00000     ; 0xF << 20
str.w r2, [r3, #136]        ; r3 = 0xE000ED00 = SCB
```

#### ⑥ 向量段 / 段布局核对

```
.isr_vector  → FLASH 0x15000000 （SDK 原始向量表，SDK startup 保留）
.vector      → AXI_SRAM（RAM 向量表，2KB 对齐）  ← 内核用这张
.data        → AXI_SRAM  AT> FLASH
.bss         → AXI_SRAM
._user_heap_stack → AXI_SRAM（NOLOAD）
```

**启动代码只拷 `.data`（`_sdata.._edata`）、只清 `.bss`（`_sbss.._ebss`）。**
任何**既不在 .data 也不在 .bss** 的已初始化段都不会被初始化 → 必须显式定义或标 NOLOAD。

---

## 5. 内核配置裁剪要点（target_config.h / Kconfig）

```c
#define OS_SYS_CLOCK                        600000000UL  // 必须等于真实 HCLK，否则 tick 不准
#define LOSCFG_BASE_CORE_TICK_PER_SECOND    100UL        // 每秒 tick（1000 更精细但开销大）
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX  0xFFFFFFUL   // SysTick 重载上限
#define LOSCFG_PLATFORM_HWI_LIMIT           416          // 外部中断数（按芯片 IRQ 数）
#define LOSCFG_ARCH_HWI_VECTOR_ALIGN        0x800        // ★ 见 §4.1①
#define LOSCFG_BASE_CORE_TSK_LIMIT          24           // 任务数上限
#define LOSCFG_SYS_EXTERNAL_HEAP            1            // 堆来源
#define LOSCFG_BASE_CORE_SWTMR              1            // 软件定时器
#define LOSCFG_PLATFORM_EXC                 1            // 异常打印（调试阶段必开）
#define LOSCFG_KERNEL_BACKTRACE             1            // 栈回溯
```

**裁剪建议**：纯 C 应用关 `LOSCFG_KERNEL_CPPSUPPORT`；生产固件关 Shell，
但**保留异常日志**（省那几 KB 不值得）。

---

## 6. 编译与产物

```bash
# 源码根目录
export PATH=$HOME/.local/bin:$PATH
hb set -p <product_name>       # 如 n32h7xx_eval
hb build
```

产物在 `out/<product>/<product>/`：

| 文件 | 说明 |
|---|---|
| `OHOS_Image` | ELF（含调试符号） |
| `OHOS_Image.bin` | 裸二进制（烧录用） |
| `OHOS_Image.map` | 链接 map（查内存布局） |
| `OHOS_Image.sym.sorted` | 排序符号表 |
| `OHOS_Image.asm` | 全反汇编 |

> ⚠️ **`hb build` 不会自动生成 .hex**。需要手动：
> `arm-none-eabi-objcopy -O ihex OHOS_Image OHOS_Image.hex`

---

## 7. 最小系统验证清单

1. **串口心跳**：复位后 115200 8N1 应输出启动 banner。串口无输出先查 GPIO 复用 + 串口时钟 + 波特率。
2. **点灯**：直接操作 GPIO 寄存器（`GPIOx->POD ^= PIN`）验证 CPU 在跑。
3. **任务调度**：多优先级任务 + 打印，验证抢占。
4. **中断**：定时器/GPIO 中断触发信号量，测延迟。
5. **内存**：反复 alloc/free，用 `LOS_MemInfoGet()` 看碎片。
6. **异常**：故意除零/空指针，确认异常能打印出 Type/PC/LR/寄存器。

---

## 8. 典型故障排查表

| 现象 | 可能原因 | 排查 |
|---|---|---|
| 调试器可连，单步跑飞/HardFault | 栈太小；数组越界；**向量表未对齐** | 看 `SCB->CFSR`/`HFSR`；查 `_estack`；**查 VTOR 对齐** |
| 内核初始化后立即崩，Type=11 非精确总线错误 | **向量表 VTOR 对齐不足**；堆损坏；外设时钟未开 | 先验 `g_hwiForm % 2048 == 0` |
| `OsSwtmrInit error` / `LOS_KernelInit FAILED` | **堆太小** | 加大 `_Min_Heap_Size` / `LOSCFG_SYS_HEAP_SIZE`，看 map 里 `__heap_end` |
| 串口无输出 | GPIO 复用/时钟/波特率/流控 | 点灯法确认代码跑到 UART 初始化 |
| 任务创建失败返回错误码 | 任务数超 `TSK_LIMIT`；内存不足；优先级非法 | `LOS_TaskInfoGet` + 内存统计 |
| 跑一段时间死机 | 栈溢出；内存泄漏；ISR 过长；共享资源无锁 | 开栈溢出检测；定期打内存；用互斥锁 |
| 内核起来后外设访问崩（外设之前好好的） | 中断/异常一发生就跳垃圾 → 指向向量表问题 | 见 §4.1① |

**独家技巧**：诡异难以复现的问题用「代码二分注释法」快速定位；
善用 DWT 数据观察点与 SWV 实时输出，比打日志更高效。

---

## 9. N32H7xx 专属补充（本工程实测）

- **`ConfigTcmSize()` 只在 OTP 当前值为 `0x2c` 时才写 OTP 并复位**，否则改宏不生效（"改了但 map 没变"）。
  改宏必须同步改 scatter/ld 的 ITCM/DTCM/AXI_SRAM 长度。
- 恒等式：`ITCM + DTCM + AXI_SRAM2/3 = 1024 KB`，另加固定 128 KB AXI_SRAM1。
  - SDK 1.2.0 HMI 用 `0x18`（640/0/384）；SDK 1.3.0 用 `0x20`（256/256/512）。
- `SystemInit()` 里 `CORE_CM7` 分支有一句 `#ifdef USING_TCM` —— **不定义则完全不调 `ConfigTcmSize()`**，
  TCM 划分就沿用 OTP 默认值。排查内存布局时必须先确认这点。
- `SystemInit()` 会把 `SCB->VTOR` 设为 `FLASH_BANK1_BASE`（0x15000000），
  **之后由 `HalHwiInit()` 改写到 RAM `g_hwiForm`**，这是设计如此，不是 bug。
- 若使能 CM4（`RCC_EnableCM4`）：**Bank2 必须有有效 CM4 镜像**，否则 CM4 跑飞会干扰 M7 总线。

---

## 10. 参考资料

- OpenHarmony 官方 LiteOS-M 内核仓：`https://gitee.com/openharmony/kernel_liteos_m`
- Getting Started（DeepWiki）：`https://deepwiki.com/openharmony/kernel_liteos_m/2-getting-started`
- **Mini System SoC Porting Guide**（官方，移植必读）：
  `https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/porting/porting-mini-system.md`
- 官方环境搭建：`https://gitee.com/openharmony/docs/blob/master/zh-cn/device-dev/quick-start/`
- 社区移植工程（Cortex-M 参考实现）：
  - Cortex-M3 STM32F103（Keil）：`https://gitee.com/rtos_lover/stm32f103_simulator_keil`
  - Cortex-M4 STM32F429IGTb（Ninja/GCC/IAR）：`https://gitee.com/harylee/stm32f429ig_firechallenger`
  - Cortex-M7 Nucleo-F767ZI（Ninja/GCC/IAR）：`https://gitee.com/harylee/nucleo_f767zi` ← **同核，最有参考价值**
- 国民技术（NSING）官网下载中心（OpenHarmony 环境笔记）：
  `https://www.nationstech.com/support/down/`
- 前人 V1.0.0 移植参考（本工作区）：`AN_N32H7xx_OpenHarmony_V1.0.0/`
- 前人原始计划：`AN_N32H7xx_OpenHarmony_V1.0.0/OHOS_N32H7_Mini_Porting_Plan.md`

---

## 附：本工程 Bug 修复记录

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| 1 | CRLF/符号链接/反斜杠 gitdir/空格目录 | Windows 搬树 | 批量 sed / 按 git 还原 / 路径改斜杠 / 目录改名 |
| 2 | `hb` 找不到 device 目录 | `config.json` 的 `kernel_version` 与 config.gni 不一致 | 都设为 `""` |
| 3 | `soc.h` 里 `SA` 与 musl `#define SA` 冲突 | 引入了完整的 `n32h7xx.h` | `soc.h` 只给最小 CMSIS |
| 4 | `OsSwtmrInit error / LOS_KernelInit FAILED` | **内核堆仅 8 KB** | `_Min_Heap_Size` 0x2000 → **0x1C000（112K）** |
| 5 | 内核初始化后访问外设即非精确总线错误（Type=11） | **`g_hwiForm` 仅 256 字节对齐（需 2048）**，`target_config.h` 缺 `LOSCFG_ARCH_HWI_VECTOR_ALIGN` | 补 `0x800` + board.ld 显式 `.vector` 段 `ALIGN(0x800)` → `g_hwiForm=0x24000800` |
| 6 | 进入调度后 `Swt_Task` 崩 UsageFault（Type=9），`PC=0xaa00001e`，`R0-R6=0xAA000019..0x1F`、`R7=0x04040404` | **C 与 ASM 的 FPU 判定不一致**：C 侧（经 `soc.h`→`core_cm7.h`）`__FPU_USED=1`，`TaskContext` 含 64B `S16-S31` 前导；ASM 侧（`los_dispatch.S` 纯汇编不引头，**GCC 汇编器不定义 `__FPU_PRESENT/__FPU_USED`**）判定为假，走 `#else`（`add ip,#36`）。**两边差 0x40** → 汇编从 `context+36` 读走 `S25..S31+uwR4` 填进 `r0-r7`，`bx r6` 飞进 `0xAA00001F` | 在 `los_dispatch.S` 顶部按 `__ARM_FP` 显式定义 `__FPU_PRESENT/__FPU_USED`（带 `!defined` 兜底）。改后 objdump 确认 `add ip,#100`/`#72`/`vpush-vpop`/`vstmdb d8-d15`/`vldmia d8-d15` 全部就位 |

> **Bug 6 的通用教训**：`__FPU_PRESENT` / `__FPU_USED` **在 GCC 汇编里绝不自动定义**（只给 `__ARM_FP`）。
> 凡 C 与 ASM 都要用的布局开关，必须在汇编文件里显式定义，或给 ASM 补 `-imacros config.h`
> 以便统一吃 Kconfig 宏（参考工程 `kernel_liteos_m-master/config.gni` 的
> `kconfig_config_asmflags += [ "-imacros", "$LITEOS_MENUCONFIG_H" ]` 就是这个用途）。否则会出现
> **C 侧结构体带 FPU 字段、汇编侧按无 FPU 算偏移**的静默错位——编译链接全绿，一进调度即崩。
>
> **故障指纹速查**：`UsageFault (Type=9)` + 寄存器出现 `0xAA0000xx` / `0x04040404` → 上下文错位；
> `ImpreciseBusFault (Type=11)` + `FaultAddr=0xABABABAB` → **PC 不可信**，应查"内核初始化后改动了哪个全局状态"。

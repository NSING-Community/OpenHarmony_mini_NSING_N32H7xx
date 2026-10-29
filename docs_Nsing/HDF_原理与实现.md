# HDF 原理与实现：N32H7xx + OpenHarmony LiteOS-M mini

> 本文记录把 HDF（Hardware Driver Foundation）接入 N32H7xx（Cortex-M7）OpenHarmony 6.1 mini 的
> 完整过程：原理 → 架构 → 启动流程 → 配置 → 驱动模型 → 代码实现 → 构建接线 → 踩坑清单。
>
> 配套产物：`output/v6_hdf_demo/OHOS_Image_v6hdf.{hex,bin}`（版本串 `BUILD=v6-hdf-demo`）。

---

## 1. HDF 是什么，为什么需要它

裸机/RTOS 上，驱动通常是"每个外设一份代码，直接操作寄存器"。这在小项目里没问题，但：

- 换一颗芯片，**应用层代码要跟着改**（因为直接用了该芯片的寄存器）
- 同一份业务逻辑，换板子/换外设就要重写
- 驱动的加载顺序、依赖关系、电源管理都得手工维护

**HDF 解决的就是"应用与硬件解耦"**，做法和 Linux 的设备模型类似：

```
应用层     UartWrite(handle, buf, len)          ← 只认接口，不认芯片
   ↕  (HDF 统一设备接口)
框架层     uart_core / uart_if / uart_service   ← OHOS 提供，所有芯片共用
   ↕  (UartHostMethod 方法表)
适配层     uart_n32h7xx.c                       ← 我们写：把方法表落到 N32H7 寄存器
   ↕
硬件       USART1 @ 0x4000C400
```

**收益**：
- 应用代码零硬件依赖 —— 换芯片只改适配层
- 驱动通过**设备树（HCS）**声明，不写死在代码里 —— 换板子改配置不改代码
- 驱动的注册/加载/依赖由框架统一管理

**代价**：多一层间接（函数指针），Flash 占用增加（本项目 HDF 框架约 +98KB text）。

---

## 2. 架构：三层

| 层 | 位置 | 职责 | 谁写 |
|---|---|---|---|
| **框架层** framework | `drivers/hdf_core/framework/` | core（host/manager/shared）、support/platform（各外设 core/if/manager）、utils（hcs_parser） | OHOS |
| **适配层** adapter | `drivers/hdf_core/adapter/khdf/liteos_m/` | OS 适配：core / osal / platform；以及 **SoC 适配目录** `adapter/platform/{gpio,uart}/` | OHOS + 厂商 |
| **设备树/配置** | `vendor/<产品>/hdf_config/` | HCS 文件，描述 host / device / deviceNode | 产品 |

关键结论：**不用从零写驱动**。框架已经提供 `gpio_core.c`、`uart_core.c` 等，
我们只实现 **SoC 方法集**（`GpioMethod` / `UartHostMethod`）。

---

## 3. 启动流程

### 3.1 完整链路（本项目实测）

```
Reset_Handler (SDK startup.s)
  └─ SystemInit()                  时钟树 HSE→PLL→600MHz，使能 FPU
      └─ main()
          ├─ BoardClockInit()      时钟 + 释放 CM4
          ├─ BoardLedEarlyOn()     LED 引脚配置 + 时钟（必须在 kernel 前）
          ├─ UartInit()            控制台串口就绪
          ├─ LOS_KernelInit()      内核：内存池/任务/软定时器 + VTOR 重定向
          ├─ DeviceManagerStart()  ★ HDF 启动
          │     ├─ 解析 hcs blob → 得到 host/device/deviceNode 树
          │     ├─ 为每个 host 建 Host 线程
          │     └─ 按 moduleName 匹配驱动 → 调 Bind() → Init()
          ├─ OHOS_SystemInit()     ★ OHOS 启动编排
          │     ├─ MODULE_INIT(bsp)
          │     ├─ MODULE_INIT(device)
          │     ├─ MODULE_INIT(core)
          │     ├─ SYS_INIT(service)
          │     ├─ SYS_INIT(feature)
          │     └─ MODULE_INIT(run)  ← SYS_RUN(应用入口) 在这里执行
          └─ LOS_Start()           开调度，永不返回
```

**注意顺序**：`DeviceManagerStart()` 和 `OHOS_SystemInit()` 都在 `LOS_Start()` **之前**。
这与官方 mini 板（`device/qemu/riscv32_virt/liteos_m/board/main.c`）一致。

### 3.2 驱动是怎么"自己跑起来"的

两个宏是关键：

```c
/* 1) 驱动注册：把驱动入口指针放进 .hdf.driver 段 */
#define HDF_INIT(module)  HDF_DRIVER_INIT(module)
#define HDF_DRIVER_INIT(module) \
    const size_t USED_ATTR module##HdfEntry HDF_SECTION = (size_t)(&(module))
#define HDF_SECTION __attribute__((section(".hdf.driver")))

/* 2) 应用注册：把函数指针放进 .zinitcall.run*.init 段 */
#define SYS_RUN(func)  LAYER_INITCALL_DEF(func, run, "run")
```

框架/系统启动时**遍历段**起点到终点，逐个调用：

```c
/* HDF：遍历 .hdf.driver 段，得到所有 HdfDriverEntry */
size_t *start = HDF_DRIVER_BEGIN();      /* _hdf_drivers_start */
size_t *end   = HDF_DRIVER_END();        /* _hdf_drivers_end   */
for (p = start; p < end; p++) { entry = (struct HdfDriverEntry *)(*p); ... }
```

```c
/* OHOS：MODULE_INIT(run) 遍历 __zinitcall_run_start..end */
for (p = __zinitcall_run_start; p < __zinitcall_run_end; p++) { (*p)(); }
```

**所以链接脚本必须提供这些段和边界符号**（详见 §7.3）——漏了就会出现
"驱动编译了但没链接"（符号数 0）或"`__zinitcall_*` 未定义引用"。

---

## 4. HCS 设备树配置

### 4.1 是什么

HCS（HDF Configuration Source）是 HDF 的**声明式配置语言**，描述：

```
root
 └─ device_info
     ├─ template host       ← 模板，定义字段默认值
     ├─ uart_host :: host   ← 一个 host（对应一条 Host 线程）
     │   └─ uart_device :: device        ← 一个驱动模块（按 moduleName 匹配）
     │       └─ uart0 :: deviceNode      ← 一个实例（提供 serviceName）
     └─ gpio_host :: host
         └─ ...
```

三层含义：
- **host** —— 一条独立线程 + 一个驱动加载域（可设 priority 控制加载顺序）
- **device** —— 一个驱动模块，`moduleName` 必须与 C 代码里 `HdfDriverEntry.moduleName` **完全一致**
- **deviceNode** —— 一个实例，`serviceName` 是应用 `UartOpen(0)` 等接口查找的键

### 4.2 本项目的 device_info.hcs

```hcs
/* vendor/Nsing/n32h7xx_eval/hdf_config/device_info/device_info.hcs */
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
            uart_device :: device {
                uart0 :: deviceNode {
                    policy = 2;              /* 2 = 发布服务 */
                    priority = 100;
                    preload = 0;
                    permission = 0660;
                    moduleName = "NSING_UART_MODULE_HDF";   /* ↔ C 代码 */
                    serviceName = "HDF_PLATFORM_UART_0";    /* ↔ UartOpen(0) */
                    deviceMatchAttr = "";
                }
            }
            gpio_device :: device {
                gpio0 :: deviceNode {
                    policy = 2;
                    priority = 100;
                    preload = 0;
                    permission = 0660;
                    moduleName = "NSING_GPIO_MODULE_HDF";
                    serviceName = "HDF_PLATFORM_GPIO_0";
                    deviceMatchAttr = "";
                }
            }
        }
    }
}
```

入口文件 `hdf.hcs` 只负责 include 和声明根模块：

```hcs
#include "device_info/device_info.hcs"

root {
    module = "Nsing,n32h7xx_eval";
}
```

### 4.3 HCS 是怎么变成固件里一段数据的

**不是链接段，是"生成 C 源文件"**：

```
hdf_config/BUILD.gn 声明 hcs_sources
  → hdf.gni 的 hdf_driver() 模板自动调 hc_gen_hex()
    → build/config/components/hc_gen/hc_gen.gni
      → 执行 hc-gen 工具（drivers/hdf_core/framework/tools/hc-gen）
        → 生成 hdf_hex.c（一个 C 数组，含 4 字节对齐的二进制 blob）
          → 和其他 .c 一起编译进固件
            → 运行时 DeviceManagerStart() 用 hcs_parser 解析它
```

`hdf_config/BUILD.gn`：

```gn
import("//drivers/hdf_core/adapter/khdf/liteos_m/hdf.gni")

module_name = "hdf_config"
hdf_driver(module_name) {
  hcs_sources = [ "hdf.hcs" ]

  # hdf_driver 默认只对 HDF 内部可见，放开给板级目录（它依赖本 target）
  special_visibility = [ "//device/board/Nsing/n32h7xx_eval/liteos_m/board:*" ]
}
```

> **踩坑**：`build/lite`（mini/small）下 hc_gen.gni 原本用
> `external_deps = ["drivers_tools:hc_gen"]`（标准系统语法）+ 组件 label，
> 在 mini 下会报 `This doesn't specify a dependency`。
> 修法见 §8 坑 9。

---

## 5. 驱动模型

### 5.1 通用骨架

每个 HDF 驱动都长这样：

```c
/* ① 定义驱动入口（moduleName 必须与 hcs 一致） */
struct HdfDriverEntry g_XxxDriverEntry = {
    .moduleVersion = 1,
    .moduleName  = "NSING_XXX_MODULE_HDF",
    .Bind        = XxxDriverBind,      /* 可选：创建设备对象 */
    .Init        = XxxDriverInit,      /* 必填：挂方法表、注册到框架 */
    .Release     = XxxDriverRelease,   /* 卸载 */
};
HDF_INIT(g_XxxDriverEntry);            /* ② 自注册到 .hdf.driver */

/* ③ 方法表：框架通过这些回调访问硬件 */
static struct XxxMethod g_xxxMethod = { ... };

/* ④ Init：绑定设备 + 填 priv + 挂方法表 + 注册 */
static int32_t XxxDriverInit(struct HdfDeviceObject *device) { ... }
```

### 5.2 GPIO：`GpioCntlr` + `GpioMethod`

框架要求实现的钩子（`gpio_core.h`）：

```c
struct GpioMethod {
    int32_t (*request)(struct GpioCntlr *cntlr, uint16_t local);          /* 可选 */
    int32_t (*release)(struct GpioCntlr *cntlr, uint16_t local);          /* 可选 */
    int32_t (*write)(struct GpioCntlr *cntlr, uint16_t local, uint16_t val);
    int32_t (*read)(struct GpioCntlr *cntlr, uint16_t local, uint16_t *val);
    int32_t (*setDir)(struct GpioCntlr *cntlr, uint16_t local, uint16_t dir);
    int32_t (*getDir)(struct GpioCntlr *cntlr, uint16_t local, uint16_t *dir);
    int32_t (*setIrq)(...);  /* 中断相关，本项目暂不实现 */
    ...
};
```

**编号约定**：`local` 是本地引脚号；全局号 = `cntlr->start + local`。
本项目注册 `start = 0, count = 144`，于是全局号 == local：

```
gpio = group * 16 + pin        group 0..8 → GPIOA..GPIOI
PB3  = 1*16 + 3  = 19
PF10 = 5*16 + 10 = 90
PI8  = 8*16 + 8  = 136
```

### 5.3 UART：`UartHost` + `UartHostMethod`

```c
struct UartHostMethod {
    int32_t (*Init)(struct UartHost *host);
    int32_t (*Deinit)(struct UartHost *host);
    int32_t (*Read)(struct UartHost *host, uint8_t *data, uint32_t size);
    int32_t (*Write)(struct UartHost *host, uint8_t *data, uint32_t size);
    int32_t (*GetBaud)(struct UartHost *host, uint32_t *baudRate);
    int32_t (*SetBaud)(struct UartHost *host, uint32_t baudRate);
    int32_t (*GetAttribute)(struct UartHost *host, struct UartAttribute *attribute);
    int32_t (*SetAttribute)(struct UartHost *host, struct UartAttribute *attribute);
    int32_t (*SetTransMode)(struct UartHost *host, enum UartTransMode mode);
    int32_t (*pollEvent)(struct UartHost *host, void *filep, void *table);
};
```

UART 与 GPIO 的差别：
- 入口多了 **`Bind`**：`Bind` 里 `UartHostCreate(device)` 创建 host 对象，
  `Init` 里 `UartHostFromDevice(device)` 取回来、填 `priv` 和 `method`
- 服务名前缀固定：`HDF_PLATFORM_UART%d`（`UartOpen(0)` → `HDF_PLATFORM_UART_0`）

---

## 6. 本次代码实现

### 6.1 GPIO 驱动 `gpio_n32h7xx.c`（要点）

```c
/* 关键 1：本地声明寄存器布局，不引 n32h7xx.h（会与 soc.h 的 enum IRQn 冲突） */
#define N32_GPIO_BASE(group)    (0x58032800UL + (uint32_t)(group) * 0x400UL)
#define N32_GPIO_PMODE_OFFSET   0x00UL
#define N32_GPIO_PID_OFFSET     0x10UL
#define N32_GPIO_POD_OFFSET     0x14UL
#define N32_REG32(addr)         (*(volatile uint32_t *)(addr))

/* 关键 2：驱动入口，moduleName 与 device_info.hcs 一致 */
struct HdfDriverEntry g_GpioDriverEntry = {
    .moduleVersion = 1,
    .moduleName = "NSING_GPIO_MODULE_HDF",
    .Init = GpioDriverInit,
    .Release = GpioDriverRelease,
};
HDF_INIT(g_GpioDriverEntry);

/* 关键 3：方法表 */
static struct GpioMethod g_n32GpioMethod = {
    .write = GpioDevWrite, .read = GpioDevRead,
    .setDir = GpioDevSetDir, .getDir = GpioDevGetDir,
    /* 其余留 NULL，框架会跳过 */
};

/* 关键 4：写引脚（N32H7 用 POD 寄存器，不是 BSRR） */
static int32_t GpioDevWrite(struct GpioCntlr *cntlr, uint16_t local, uint16_t val)
{
    uint32_t group, pin, addr;
    if (N32GpioSplit(local, &group, &pin) != HDF_SUCCESS)
        return HDF_ERR_INVALID_PARAM;
    addr = N32_GPIO_BASE(group) + N32_GPIO_POD_OFFSET;
    if (val != 0U) N32_REG32(addr) |=  (1UL << pin);
    else           N32_REG32(addr) &= ~(1UL << pin);
    return HDF_SUCCESS;
}

/* 关键 5：设置方向（PMODE 每引脚 2 位：0=输入 1=输出 2=AF 3=模拟） */
static int32_t GpioDevSetDir(struct GpioCntlr *cntlr, uint16_t local, uint16_t dir)
{
    uint32_t group, pin, pmode, addr;
    if (N32GpioSplit(local, &group, &pin) != HDF_SUCCESS)
        return HDF_ERR_INVALID_PARAM;
    addr = N32_GPIO_BASE(group) + N32_GPIO_PMODE_OFFSET;
    pmode = (dir == GPIO_DIR_OUT) ? 0x1UL : 0x0UL;
    N32_REG32(addr) &= ~(0x3UL << (pin * 2U));
    N32_REG32(addr) |=  (pmode << (pin * 2U));
    return HDF_SUCCESS;
}

/* 关键 6：Init —— 绑定 + 挂方法表 + 注册到框架 */
static int32_t GpioDriverInit(struct HdfDeviceObject *device)
{
    int32_t ret = PlatformDeviceBind(&g_n32GpioCntlr.device, device);
    if (ret != HDF_SUCCESS) return ret;

    g_n32GpioCntlr.ops   = &g_n32GpioMethod;
    g_n32GpioCntlr.start = 0;
    g_n32GpioCntlr.count = N32_GPIO_TOTAL;      /* 144 */

    ret = GpioCntlrAdd(&g_n32GpioCntlr);        /* 注册，之后 GpioWrite() 才可用 */
    if (ret != HDF_SUCCESS) { HDF_LOGE("GpioCntlrAdd failed: %d", ret); return ret; }

    HDF_LOGI("N32H7xx GPIO registered, %u pins", N32_GPIO_TOTAL);
    return HDF_SUCCESS;
}
```

### 6.2 UART 驱动 `uart_n32h7xx.c`（要点）

```c
/* 寄存器（同样本地声明） */
#define N32_UART1_BASE          0x4000C400UL
#define N32_UART_STS_OFFSET     0x0CUL
#define N32_UART_DAT_OFFSET     0x10UL
#define N32_UART_STS_TXDE       0x00000080UL   /* ★ 数据寄存器空 */
#define N32_UART_STS_RXDNE      0x00000200UL

/* 入口：注意有 Bind */
struct HdfDriverEntry g_UartDriverEntry = {
    .moduleVersion = 1,
    .moduleName = "NSING_UART_MODULE_HDF",
    .Bind = UartDriverBind, .Init = UartDriverInit, .Release = UartDriverRelease,
};
HDF_INIT(g_UartDriverEntry);

/* Bind：创建 host 对象 */
static int32_t UartDriverBind(struct HdfDeviceObject *device)
{
    return (UartHostCreate(device) == NULL) ? HDF_FAILURE : HDF_SUCCESS;
}

/* Init：取回 host，填 priv + 方法表 */
static int32_t UartDriverInit(struct HdfDeviceObject *device)
{
    struct UartHost *host = UartHostFromDevice(device);
    if (host == NULL) return HDF_ERR_INVALID_OBJECT;
    host->priv   = &g_n32UartDevice;
    host->method = &g_n32UartHostMethod;
    return HDF_SUCCESS;
}

/* Write：★ 等 TXDE，绝不能等 TXC */
static int32_t UartHostDevWrite(struct UartHost *host, uint8_t *data, uint32_t size)
{
    UartDevice *dev = (UartDevice *)host->priv;
    uint32_t base = N32UartBase(dev->uartId), i;

    for (i = 0U; i < size; i++) {
        while ((N32_REG32(base + N32_UART_STS_OFFSET) & N32_UART_STS_TXDE) == 0U) {
            /* 等数据寄存器空 */
        }
        N32_REG32(base + N32_UART_DAT_OFFSET) = data[i];
    }
    return (int32_t)size;
}
```

> **为什么驱动不初始化 USART1**：引脚复用、GPIO 时钟、USART1 时钟都由板级
> `uart_console.c` 在 kernel 之前配好了。HDF 驱动只接管数据通道 —— 重复初始化会打架。

### 6.3 应用 `app_hdf_demo.c`

```c
#include "ohos_init.h"      /* SYS_RUN */
#include "cmsis_os2.h"      /* osThreadNew / osDelay */
#include "gpio_if.h"        /* GpioSetDir / GpioWrite */
#include "uart_if.h"        /* UartOpen / UartWrite */

#define LED_PIN_RED     ((uint16_t)(1 * 16 + 3))    /* PB3  */
#define LED_PIN_GREEN   ((uint16_t)(5 * 16 + 10))   /* PF10 */
#define LED_PIN_BLUE    ((uint16_t)(8 * 16 + 8))    /* PI8  */

static DevHandle g_uartHandle = NULL;

static void *DemoLedTask(const char *arg)
{
    uint32_t i = 0U;
    while (1) {
        (void)GpioWrite(g_ledPins[i], 0);   /* 共阳：低电平点亮 */
        osDelay(400);
        (void)GpioWrite(g_ledPins[i], 1);
        i = (i + 1U) % 3U;
    }
    return NULL;
}

static void *DemoLogTask(const char *arg)
{
    uint32_t count = 0U;
    while (1) {
        UartPuts("[N32H7] demo: heartbeat ");
        /* 手工 u32→十进制，不依赖 libc */
        UartPuts(" - led toggling\r\n");
        count++;
        osDelay(1000);
    }
    return NULL;
}

/* ★ 应用入口：由 OHOS_SystemInit() → MODULE_INIT(run) 自动调用 */
static void HdfDemoEntry(void)
{
    g_uartHandle = UartOpen(0);                    /* → HDF_PLATFORM_UART_0 */
    if (g_uartHandle == NULL) return;

    UartPuts("\r\n[N32H7] hdf demo: uart ready\r\n");

    for (i = 0U; i < 3U; i++) {
        GpioSetDir(g_ledPins[i], GPIO_DIR_OUT);
        GpioWrite(g_ledPins[i], 1);                /* 先全灭 */
    }
    UartPuts("[N32H7] hdf demo: gpio ready, creating tasks\r\n");

    osThreadNew((osThreadFunc_t)DemoLedTask, NULL, &attr_led);
    osThreadNew((osThreadFunc_t)DemoLogTask, NULL, &attr_log);

    UartPuts("[N32H7] hdf demo: tasks created\r\n");
}

SYS_RUN(HdfDemoEntry);     /* ★ 注册，不需要在 main() 里调用 */
```

---

## 7. 构建接线（完整清单）

### 7.1 产品配置 `vendor/Nsing/n32h7xx_eval/config.json`

```json
{
  "subsystems": [
    { "subsystem": "kernel", "components": [ { "component": "liteos_m" } ] },
    { "subsystem": "hdf", "components": [
        { "component": "hdf_core" },
        { "component": "drivers_tools" }
    ]},
    { "subsystem": "commonlibrary", "components": [
        { "component": "utils_lite", "features": ["utils_lite_feature_file = true"] }
    ]},
    ...
  ]
}
```

> - 子系统名是 **`hdf`**（不是 `drivers`；`drivers` 只是目录名）
> - `drivers_tools` 提供 hc-gen 工具
> - `utils_lite` 是 hcs_parser 依赖（`UtilsFileOpen` 等），且必须开 `utils_lite_feature_file`

### 7.2 内核开关 `vendor/.../kernel_configs/*.config`

```
LOSCFG_DRIVERS_HDF=y
LOSCFG_DRIVERS_HDF_PLATFORM=y
LOSCFG_DRIVERS_HDF_PLATFORM_GPIO=y
LOSCFG_DRIVERS_HDF_PLATFORM_UART=y
LOSCFG_SOC_COMPANY_NSING=y          # ★ 驱动 BUILD.gn 用它做条件
LOSCFG_KERNEL_PM=y                  # DRIVERS_HDF select KERNEL_PM
```

> `kernel_configs/*.config` → （Kconfig）→ `out/.../config.h`（C 宏）+ `out/.../config.gni`（gn 变量）。
> **gn 里 `defined(LOSCFG_XXX)` 判断的是 config.gni 里的变量**，不是 C 宏。

### 7.3 链接脚本 `board.ld` 必须提供的段

```ld
/* HDF 驱动表：HDF_INIT() 把驱动入口指针放这里 */
.rodata :
{
    *(.rodata*)
    _hdf_drivers_start = .;
    KEEP(*(.hdf.driver))          /* KEEP 必需：没人直接引用，否则被 --gc-sections 回收 */
    _hdf_drivers_end = .;
} >FLASH

/* OHOS 初始化调用表：MODULE_INIT/SYS_INIT/SYS_RUN 用 */
.zInit :
{
    __zinitcall_bsp_start = .;
    KEEP (*(.zinitcall.bsp0.init))  KEEP (*(.zinitcall.bsp1.init))
    KEEP (*(.zinitcall.bsp2.init))  KEEP (*(.zinitcall.bsp3.init))
    KEEP (*(.zinitcall.bsp4.init))
    __zinitcall_bsp_end = .;
    . = ALIGN(4);
    __zinitcall_device_start = .;
    KEEP (*(.zinitcall.device0.init)) ... KEEP (*(.zinitcall.device4.init))
    __zinitcall_device_end = .;
    . = ALIGN(4);
    __zinitcall_core_start = .;
    KEEP (*(.zinitcall.core0.init))  ... KEEP (*(.zinitcall.core4.init))
    __zinitcall_core_end = .;
    . = ALIGN(4);
    __zinitcall_sys_service_start = .;
    KEEP (*(.zinitcall.sys.service0.init)) ... 5 级
    __zinitcall_sys_service_end = .;
    . = ALIGN(4);
    __zinitcall_sys_feature_start = .;
    KEEP (*(.zinitcall.sys.feature0.init)) ... 5 级
    __zinitcall_sys_feature_end = .;
    . = ALIGN(4);
    __zinitcall_run_start = .;                     /* ★ SYS_RUN 落在这里 */
    KEEP (*(.zinitcall.run0.init)) ... KEEP (*(.zinitcall.run4.init))
    __zinitcall_run_end = .;
    /* app.service / app.feature / test / exit 同理，省略 */
} >FLASH
```

> **★★ `.zInit` 必须放 FLASH（`>FLASH`），不能放 RAM。**
> 它只含只读函数指针，放 FLASH 原地即可。
> 若写成 `>AXI_SRAM`，段会带上 LMA 而 VMA 落在 `_sdata.._edata` **之外**，
> Cortex-M 的 SDK startup **只拷贝 `.data`**，于是这段内存**永远不被初始化** ——
> 运行时读到的全是垃圾指针，`OHOS_SystemInit()` 一 `blx` 就总线错误。
> **判据：`VMA == LMA`？** 不等就说明需要运行时拷贝，必须落在 `_sdata.._edata` 内。
> （注意 `objdump -s -j .zInit` 显示的是 **LMA 内容**，看起来是好的，会骗过你。）

### 7.4 SoC 驱动的接入点

`drivers/hdf_core/adapter/platform/gpio/BUILD.gn`（uart 同理）：

```gn
if (defined(LOSCFG_SOC_COMPANY_NSING)) {
    sources += [ "gpio_n32h7xx.c" ]
}
```

### 7.5 板级依赖 HDF

`device/board/Nsing/n32h7xx_eval/liteos_m/board/BUILD.gn`：

```gn
kernel_module("bsp_config") {
  sources = [ "src/main.c", ..., "src/app_hdf_demo.c", "src/board_led.c" ]

  # 用 HDF 自带的公共 config 引入全部 HDF 头路径（别手写一长串 include）
  configs += [ "//drivers/hdf_core/adapter/khdf/liteos_m:hdf_config" ]

  deps = [
    "//device/soc/Nsing/n32h7xx/firmware/n32h7xx_std_periph_driver:n32h7xx_stdperiph",
    "//vendor/Nsing/n32h7xx_eval/hdf_config:hdf_config",   # hcs blob
  ]
}
```

---

## 8. 踩坑清单（15 条，全部实测）

### 8.1 配置层

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| 1 | `find subsystem drivers failed` | 子系统注册名是 **`hdf`**（`drivers` 只是 path） | config.json 用 `"subsystem": "hdf"` |
| 2 | `hilog/log.h: No such file` | mini 走了 small 的 `uhdf/posix` **用户态**分支 | `adapter/BUILD.gn` 加 `is_mini_system` 分支指向 `khdf` 内核态 |
| 3 | `libhdf_utils_module_info.json` 丢失 | `hdf_core/bundle.json` 的 `inner_kits` 指向未编译的用户态目标 | 清空 `inner_kits` |
| 11 | `LOSCFG_SOC_COMPANY_NSING` 始终不生成 | Kconfig 文件**位置不对**：`DEVICE_PATH` 指向 `.../liteos_m`，Kconfig 却在上一层 | 在 `.../liteos_m/Kconfig` 新建 |
| 12 | `'.../Kconfig' not found` | Kconfig 的 `source` 路径**相对 `$srctree`**，不是相对当前文件 | 写成 `source "../../device/soc/Nsing/Kconfig"` |
| 13 | 改了 `.config` 不生效 | `out/.../config.gni` 是缓存 | `rm -f out/.../config.gni` 后重编 |

### 8.2 构建/链接层

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| 4 | 改了 bundle.json 没反应 | 构建缓存（`build_configs`、`gen`、`build.ninja`） | 全删后强制重新 gn gen |
| 5 | **HDF 编译了但符号数 0** | `HDFTOPDIR` 引用的默认 target 不存在（实际叫 `hdf_lite`） | 补 `group("liteos_m") { deps = [":hdf_lite"] }` |
| 6 | `HDF_INIT` 的驱动被 GC | 链接脚本缺 `.hdf.driver` 段 | board.ld 加段 + `KEEP` |
| 7 | 18 个 `__zinitcall_*` 未定义 | 链接脚本缺 `.zInit` 段 | board.ld 加完整 `.zInit` 段 |
| 8 | `UtilsFileOpen` 未定义 | 缺 `commonlibrary/utils_lite` 组件 + feature | config.json 补声明 + `utils_lite_feature_file = true` |
| 9 | `This doesn't specify a dependency` | `hc_gen.gni` 用 `external_deps`（标准系统语法），build/lite 不支持 | ① 建 `hc-gen/bundle.json` 声明 `drivers_tools` 组件；② 改 gni 用 `deps = [...]` + `get_label_info("//...:build_hc_gen")` |
| 10 | `region AXI_SRAM overflowed` | HDF bss 撑爆 128KB | 堆 `_Min_Heap_Size` 112KB → 96KB |
| 16 | `Replacing nonempty list`（gn） | `configs = [...]` 覆盖了已有值 | 改 `configs += [...]` |
| 17 | **`DeviceManagerStart()` 崩 Type=11**，`PC` 落在 `HcsCheckBlobFormat` | **hcs blob 数组未 4 字节对齐**：hc-gen 生成 `unsigned char[]`（对齐要求 1），链接器放到了 4 字节不对齐的地址；而 `HcsCheckBlobFormat` 把它强转成 `struct HbcHeader *` 读 32 位字段 → **Cortex-M7 非对齐访问 → 总线错误** | 改 `bytecode_gen.cpp`，生成数组时加 `__attribute__((aligned(4)))`，然后重建 hc-gen + 重新生成 blob |
| 18 | **`OHOS_SystemInit()` 崩 Type=5**，`PC = 野地址`（如 `0x220e3040`） | **`.zInit` 段写在 RAM（`>AXI_SRAM`）但 startup 不拷贝它**：它 VMA 在 `_sdata.._edata` 之外，RAM 内容从未初始化 → 段里全是垃圾指针 → `blx` 野地址 | board.ld 里 `.zInit` 改 `>FLASH`（只读函数指针表，原地放） |
| 19 | **`OHOS_SystemInit()` 崩 Type=11**，`PC` 落在 `InitHiviewStaticCache`，指令是 `str.w r2, [ip, #7]` | **`#pragma pack(1)` 让结构体里的 32 位成员落在非对齐地址**：`HiviewCache.buffer`（指针）位于 offset 7 → 写到 `基址+7` → **Cortex-M7 非对齐访问总线错误**（M3/M4 容忍所以官方没暴露） | 去掉 `hiview_cache.h` 的 `pack(1)`（该结构体不做序列化）。⚠️ 改完必须**强制重编 hilog/hiview 的 .o**，增量构建不会因头文件变化重编 |

> **★ Cortex-M7 不容忍非对齐访问**（M3/M4 容忍）。
> 凡是 `#pragma pack(1)` 的结构体里有 `uint32` / 指针 / `uint64`，就埋了一颗雷。
> **判据：看成员的字节偏移是否是其大小的整数倍**（`uint32` 须落在 4 的倍数）。
> 本仓还有 5 处同类隐患（`HiLogCommon.fmt` @10、`HiEvent.payload` @9、
> `HiviewConfig.logOutputModule`(uint64) @2、`FileHeaderCommon.defineFileVersion` @7、
> `HiLogModuleInfo.name` @1），其中带"文件格式"语义的不能直接去 pack，需改成
> **逐字节 / `memcpy` 访问**。
>
> **建议**：编译期加 `-Wcast-align`，运行期可用
> `_Static_assert(offsetof(T, f) % sizeof(f) == 0, "unaligned")` 提前拦截。

### 8.3 代码层

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| 14 | `redeclaration of 'enum IRQn'` | 驱动引了 `n32h7xx.h`，与 HDF 链里的 `soc.h` 冲突 | 驱动内**本地镜像寄存器布局**，不引设备头 |
| 15 | `hdf_types.h` 找不到 | board 层缺 HDF include 路径 | 用 `configs += [ "...:hdf_config" ]` 一次引入 |
| — | `missing terminating " character` | 在 ssh heredoc 里用 python 写 C，`\\r\\n` 被 shell 解析成真换行 | 用 `chr(13)/chr(92)` 拼，或本地 Write + scp |

### 8.4 验证手法

```bash
# 驱动是否真的链接进来了（最可靠的判据）
arm-none-eabi-nm OHOS_Image | grep "HdfEntry"
#   → g_GpioDriverEntryHdfEntry / g_UartDriverEntryHdfEntry

# SYS_RUN 应用是否注册成功
arm-none-eabi-nm OHOS_Image | grep "__zinitcall_run_"
#   → __zinitcall_run_HdfDemoEntry

# 段边界
arm-none-eabi-nm OHOS_Image | grep "_hdf_drivers_"
```

**只跑通编译不算数** —— 必须用 `nm` 确认符号真的进了 ELF（`--gc-sections` 会静默回收未引用的东西）。

---

## 8.5 Cortex-M7 特有：非对齐访问排查（官方 STM32F407 教程未覆盖）

官方《轻量系统 STM32F407 芯片移植案例》要求的链接脚本改动（全套 `.zinitcall.*` 段、
`.hdf.driver` 段、段放 FLASH、内核初始化后调 `DeviceManagerStart()`）我们都已做到 ✓。

但它**完全未提及**"非对齐访问"和"`#pragma pack`" —— 原因是：

| | STM32F407（官方教程） | N32H7xx（本项目） |
|---|---|---|
| 内核 | **Cortex-M4** | **Cortex-M7** |
| 非对齐访问 | **硬件容忍**（自动拆分访问） | **产生 BusFault** |
| 后果 | 从未暴露 | 崩了 3 次（Bug 17/19/20） |

### 移植到 Cortex-M7 时必查清单

1. **凡 `#pragma pack(1)` 的结构体，逐个检查成员偏移**
   - `uint32` / 指针 必须落在 **4 的倍数**；`uint16` 落在 2 的倍数；`uint64` 落在 8 的倍数。
   - 本仓已修 6 处：`HiviewCache`、`HiLogModuleInfo`、`HiLogCommon`、`HiviewConfig`、`HiEvent`、`HiviewFile`。
2. **凡"字节数组强转结构体指针"的地方**
   - 典型：HDF 的 hcs blob（`HcsCheckBlobFormat((struct HbcHeader *)start)`）→ 数组必须 `aligned(4)`。
3. **链接脚本里非 `.text/.rodata` 的只读段**（函数指针表）**优先放 FLASH**
   - 若放 RAM，必须确认 startup 会拷贝它，否则内容是垃圾。
4. **可用编译期/工具辅助**
   ```c
   _Static_assert(offsetof(T, field) % sizeof(field) == 0, "unaligned member");
   ```
   ```bash
   # 编译时若支持，可开 -Wcast-align 提示可疑强转
   ```

### 排查手法（本次实战验证有效）

```
① addr2line 定位 PC/LR → 得到函数名
② objdump 反汇编该函数 → 找到出错指令（ldr/str 的偏移）
③ 看该偏移是否对齐（% 4）
④ 查结构体定义，确认是 pack(1) 还是缺 aligned
⑤ 修 → objdump 复查指令偏移变了没
```

**寄存器是路标**：`R0/R1` 是函数前两个参数，`R12(ip)` 常是结构体基址；
把它们与源码里的变量地址一对，就能确认"在操作哪个对象"。

---

## 8.6 HDF 平台驱动的 IPC 资源需求（易被忽略）

HDF 平台驱动在注册时会**消耗内核 IPC 资源**，而限额默认很小：

| 驱动 | 注册时消耗 | 数量 |
|---|---|---|
| **GPIO** | 每引脚 1 个 **mutex**（`GpioCntlrCreateGpioInfos()` 里 `OsalSpinInit()`） | = `cntlr->count` |
| UART | host 若干信号量/互斥锁 | 每实例数个 |
| 其他 | 视实现 | — |

关键：LiteOS-M 的 **`OsalSpinInit()` 是用互斥锁实现的**（不是真自旋锁）：

```c
int32_t OsalSpinInit(OsalSpinlock *spinlock)
{
    uint32_t muxId = 0;
    ret = LOS_MuxCreate(&muxId);      /* ← 每次消耗一个 mux */
    ...
}
```

所以一个 144 引脚的 GPIO 控制器会一次性吃掉 **144 个 mutex**。

### 症状与判据

- **症状**：驱动 `Init` 静默失败 → 应用调 `GpioSetDir()`/`UartWrite()` 返回错误码
  （"UART 能用、GPIO 不能用"这种**部分可用**现象就是典型信号）
- **根因**：`LOSCFG_BASE_IPC_MUX_LIMIT` / `SEM_LIMIT` 太小
- ⚠️ `GpioCntlrAdd()` 里的 `MAX_CNT_PER_CNTLR = 1024` 是**引脚数**上限，不是资源瓶颈；
  **真正的瓶颈是 `MUX_LIMIT`**

### 修复与预算

```c
/* target_config.h */
#define LOSCFG_BASE_IPC_MUX_LIMIT   256    /* 144 GPIO 引脚 + 内核/HDF/应用余量 */
#define LOSCFG_BASE_IPC_SEM_LIMIT   128
```

**预算公式**：`MUX_LIMIT ≥ GPIO总引脚数 + 其他驱动/内核/任务用量 + 余量`。

控制块大小（从池分配字节数反推，本平台实测）：
`sizeof(LosMuxCB) = 24`、`sizeof(LosSemCB) = 16`。
所以 256 个 mux 只占 **6 KB**、128 个 sem 占 **2 KB** —— 代价极小，别吝啬。
（池由 `OsMuxInit`/`OsSemInit` 在 `LOS_KernelInit` 里一次 `LOS_MemAlloc` 出来。）

### 调试建议

**驱动注册失败的日志发生在 hilog 初始化之前，会丢失**（`DeviceManagerStart()` 早于 `OHOS_SystemInit()`）。
所以：

1. **应用侧一定要打印返回码**，不要只打 `"failed"`：
   ```c
   int32_t ret = GpioSetDir(pin, GPIO_DIR_OUT);
   if (ret != HDF_SUCCESS) {
       /* 打印 pin 和 ret，一眼看到 HDF 错误码 */
   }
   ```
2. 或者临时把 `DeviceManagerStart()` 挪到 `OHOS_SystemInit()` 之后再复现一次，就能看到 HDF 的 `HDF_LOGE`。

---

## 8.7 ★★★ `hb build` 不跟踪头依赖 —— 改了 `target_config.h` 却毫无反应

这是本项目**最隐蔽、最浪费时间**的坑，也是 §8.6 一开始"改了却没用"的真正原因。

### 症状

- `target_config.h` 改成 `MUX_LIMIT 256`，`grep` 文件**确实是 256**；
- 但运行行为完全没变（`GpioCntlrAdd` 照样 `-104`）；
- 删掉 `config.gni` / `build.ninja` 强制 gn 重新生成，**依然没变**。

### 判据：两分钟定位

**① 比时间戳**（最快）：

```bash
ls -la --time-style=full-iso device/soc/.../include/target_config.h
ls -la --time-style=full-iso out/.../obj/kernel/liteos_m/kernel/src/kernel.los_mux.o
# 头比 .o 新 → .o 是旧的，改动没编进去
```

**② 反汇编看立即数**（最硬，推荐）：

```bash
arm-none-eabi-objdump -d OHOS_Image --disassemble=OsMuxInit | head -14
```
```asm
/* 坏： */
150056f4: mov.w  r1, #576    ; 0x240   ← 池只 576 字节
1500570a: mov.w  lr, #24               ← sizeof(LosMuxCB)
15005724: cmp    r3, #24               ← ★ 只有 24 个
/* 好： */
150056f4: mov.w  r1, #6144   ; 0x1800  ← 6144 / 24 = 256 个 ✓
```

### 根因

`los_config.h:40` `#include "target_config.h"`，宏来自**头文件**（无 `-D` 覆盖），
但 `hb build` 的增量构建**没有 depfile** → ninja 只知道 `.c` 是输入，
**不知道 `.h` 也是输入** → 改头文件后 `.c` 的 mtime 没变 → 判定"已最新" → 不重编。

> 删 `build.ninja`/`config.gni` 只让 gn **重新生成构建图**，并不会让 ninja 重编 `.o`
> —— ninja 照样只比时间戳。

### 修复

```bash
find out/n32h7xx_eval/n32h7xx_eval/obj -name "*.o" -delete
export PATH=$HOME/.local/bin:$PATH && hb build
```

### ★ 通用铁律

> **改了任何 `.h`，都必须 `find out/<product>/obj -name "*.o" -delete` 再 `hb build`，
> 然后用 `objdump` 验证生成码。**

- 只改 `.c` 的文件有 mtime 保护，是安全的 —— **所以症状极具欺骗性**：
  "改 C 代码立即生效，改配置宏完全没反应"。
- 本项目已因此踩坑两次：hilog/hiview 的 `pack(1)` 头（Bug 19/20）、
  `target_config.h` 的 IPC 上限（Bug 23）。
- **验证配置宏是否真的进了镜像，永远用 `objdump` 看立即数**，
  `grep` 头文件只能证明"文件内容"，不能证明"编译进去了"。

---

## 9. 一句话总结每层要改什么

| 想做什么 | 改哪里 |
|---|---|
| 换芯片 | `drivers/hdf_core/adapter/platform/<外设>/xxx_<soc>.c` + BUILD.gn 条件分支 |
| 换板子（引脚/串口变了） | `vendor/<产品>/hdf_config/device_info/*.hcs` |
| 加一个外设驱动 | 新增 SoC 适配 .c + hcs 里加 device/deviceNode + Kconfig 加开关 |
| 改业务逻辑 | 应用层 .c（只调 HDF 接口，不碰硬件） |
| 驱动没加载 | 查 `moduleName` 是否与 hcs 一致；查符号是否在 `.hdf.driver` 段 |
| 应用没启动 | 查 `SYS_RUN` 是否注册进 `__zinitcall_run_*`；查 `OHOS_SystemInit()` 是否被调用 |

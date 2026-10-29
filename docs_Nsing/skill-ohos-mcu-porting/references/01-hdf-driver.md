# 01 HDF 驱动开发

## HDF 解决三个问题

1. **统一驱动模型**：`Bind / Init / Release` 三个回调，框架知道怎么处理任何驱动。
2. **配置与代码分离**：参数放 hcs，编译期进镜像，改配置不改代码。
3. **按名字找服务**：应用不直接调驱动函数，由框架定位。

## 两种取设备模型（决定 `policy`）

| 模型 | 应用侧 | `policy` | `serviceName` | 例子 |
|---|---|---|---|---|
| 控制器（按总线号） | `I2cOpen(busId)` | `0` | 空 | I2C、GPIO |
| 服务（按名字） | `PwmOpen(num)` | `2` | `HDF_PLATFORM_<外设>_<n>` | UART、PWM |

判定方法：看应用侧是"用总线号/端口号取"还是"用服务名取"。
控制器模型还需要一个**管理器节点**（`HDF_PLATFORM_<外设>_MANAGER`），服务模型不需要——框架的 `*DeviceAdd()` 自己挂 service。

## hcs 必须两段式

参数不是从 `deviceNode` 自身读的，而是**按 `deviceMatchAttr` 在整棵树里反查**：

```
drivers/hdf_core/framework/core/host/src/hdf_device_node.c:287
    device->property = HcsGetNodeByMatchAttr(root, deviceInfo->deviceMatchAttr);
```

`deviceMatchAttr` 留空 → `property == NULL` → 参数全部拿不到，驱动静默走默认值。
这是最难发现的一类配置错误：驱动照常启动、日志正常、功能不对。

**正确写法（三处）**

```hcs
/* ① device_info/device_info.hcs —— 只说"用哪份配置" */
i2c0 :: deviceNode {
    policy = 0;
    priority = 100;
    moduleName = "<VENDOR>_I2C_MODULE_HDF";   /* 与驱动入口逐字相同 */
    serviceName = "";
    deviceMatchAttr = "<vendor>_i2c";          /* 不能留空 */
}

/* ② i2c/i2c_config.hcs —— 装值 */
root { platform { i2c_config {
    match_attr = "<vendor>_i2c";               /* 与上面同一个串 */
    port = 1; speed = 100000;
}}}

/* ③ hdf_config/hdf.hcs —— 必须 include ② */
#include "device_info/device_info.hcs"
#include "i2c/i2c_config.hcs"
```

漏 ③ 的症状与留空 `deviceMatchAttr` 相同。用 `scripts/check_hcs.py` 自动查这三个关系。

## 四个文件，不能合并

| 文件 | 位置 | 职责 | 能否 include 厂商设备头 |
|---|---|---|---|
| `<chip>_<periph>_hw.h` | `device/soc/.../include/` | 只放原型与常量 | **不能** |
| `<chip>_<periph>_hw.c` | `device/soc/.../src/` | 寄存器、时钟、引脚、时序 | 能（唯一允许） |
| `<periph>_<chip>.c` | `drivers/hdf_core/adapter/platform/<periph>/` | HDF 回调与框架对接 | **不能** |
| `<periph>_config.hcs` | `vendor/.../hdf_config/<periph>/` | 参数 | — |

**为什么不能合并**：HDF 头链最终引入 `soc.h`，它与厂商设备头都声明 `enum IRQn`，两侧都没有保护宏。
同一编译单元同时引用必定重定义。所以"必须有一个能引 SDK 头、但不能引 HDF 头的 `.c`"。

**这层不是重复造轮子**：厂商 SDK 提供的是寄存器级原语（`Init/Enable/GenerateStart/SendData/GetFlag`），
通常**没有**"一次完整传输"的函数；完整流程（START→地址→等标志→数据→STOP）在 SDK 里是例程自己写的，每个例程一份。
薄封装做的是把那段流程收编成可复用函数，供框架的 `transfer()` 调用。

## 回调边界

| 回调 | 只做 | 绝对不要做 |
|---|---|---|
| `Bind` | 参数校验、分配对象、挂 `device->service` | 初始化硬件 |
| `Init` | 读 hcs、注册控制器/服务、申请资源 | **调用厂商 GPIO/外设初始化函数**（内核起来后调用会越界） |
| `Release` | 注销、释放 | — |

引脚复用属于"板级启动事实"，放在主入口、`LOS_KernelInit()` **之前**；
外设寄存器配置属于"驱动内部实现"，放在 `Init()`。

## 三处接线（缺一不可）

1. **内核配置**：`LOSCFG_DRIVERS_HDF_PLATFORM=y`、`LOSCFG_DRIVERS_HDF_PLATFORM_<PERIPH>=y`
2. **构建分支**：`adapter/platform/<periph>/BUILD.gn` 加厂商分支；SoC 的 `BUILD.gn` 收录薄封装 `.c`
3. **hcs 两段式**（+ 控制器类需要管理器节点）

## 单位与语义：写代码前必须确认

| 项目 | 常见陷阱 | 确认方法 |
|---|---|---|
| 时间单位 | 框架给 **ns**，硬件要 tick | 看框架头文件的注释；换算只在一处做 |
| 地址形式 | 7 位地址 vs 8 位总线字节 | 看厂商写入函数是否移位/掩码：`SADR = address & MASK` 说明要完整字节 |
| 引脚 | AF 号、是否有内部上拉 | 抄手册/例程的宏，不推测 |
| 时钟 | 内核时钟源、分频、是否来自 PLL | 抄例程的 `RCC_*` 序列 |
| 数据宽度/对齐 | 16 位寄存器、4 字节对齐的常量数组 | 看结构体与 `objdump` |

**移位（编码类操作）只在贴近硬件的那一层做，且只做一次。**
框架层通常原样透传参数；若上层已编码，下层再移位就会错得毫无线索（"地址阶段 NAK"就是这么来的）。

## 引脚映射：表驱动，不要 switch

```c
struct <chip><Periph>PinMap {
    uint8_t  port;
    uint32_t gpioClk;              /* 该端口的时钟使能位，随端口变化 */
    GPIO_Module *sclPort; uint16_t sclPin; uint32_t sclAf;
    GPIO_Module *sdaPort; uint16_t sdaPin; uint32_t sdaAf;
};
static const struct <chip><Periph>PinMap g_pinMap[] = { /* 每行一组已验证组合 */ };
```

好处：新增实例或新增一组可选引脚 = 加一行；时钟位随表项走，不会出现"引脚配了但没使能该端口时钟"。

## `permission` 的含义

设备节点的访问权限位，八进制，语义同文件权限（`0660` = 属主与同组读写）。
mini 系统无用户态进程、无 UID 隔离，**当前不生效**；仍应填合理值，因为它属于节点声明的一部分，
换到支持用户态的形态时才不会暴露问题。

## 数据回路判据（Gate G4）

只看"没报错"不算通过：

| 外设 | 判据 |
|---|---|
| GPIO | 写 1/0，读回一致，电平表可测 |
| UART | 回环收发逐字节一致 |
| I2C | 写 N 字节 → 读回逐字节相同；或 ID 寄存器与手册一致 |
| SPI | 读 ID 与手册一致；或 MOSI/MISO 短接回读一致 |
| PWM | 示波器：频率、占空比符合设置 |
| ADC | 原始码在预期区间，且随激励变化（打印 raw，不只打印换算值） |

## 交付前自检

```bash
scripts/verify_image.sh <ELF> <期望驱动数>
scripts/check_hcs.py <hdf_config 目录>
```

两条都过，再谈"完成"。

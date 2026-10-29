# 02 demo 开发

## `SYS_RUN` 的真实机制

`SYS_RUN(func)` 只做一件事：把函数指针放进一个专用链接段。

```c
/* commonlibrary/utils_lite/include/ohos_init.h */
#define SYS_RUN(func)  LAYER_INITCALL_DEF(func, run, "run")   /* 优先级 2 */
/* 展开后：
 * static const InitCall __zinitcall_run_##func
 *     __attribute__((section(".zinitcall.run2.init"))) = func; */
```

`OHOS_SystemInit()` 逐层遍历，`MODULE_INIT(run)` 展开成
`for (p = start; p < end; p++) (*p)();` —— **段里每个指针都会被调用一次**。

推论（很重要）：
- 多个 `SYS_RUN` **不是"选一个"，而是全部执行**；
- 区分 demo 的是**任务名、优先级、周期、日志前缀**，不是 `SYS_RUN` 本身；
- 启动日志里的 `MODULE_INIT(run) : N entry` 中的 N = `SYS_RUN` 的个数，可用来核对是否被裁掉。

**同层内顺序不可控**（按链接顺序）。真有先后依赖时用 `SYS_RUN_PRI(func, 0..4)`。

## 一个 demo 一个任务

```c
static void *AppXxxTask(void *arg)
{
    (void)arg;
    osDelay(MsToTicks(1500));            /* 等串口/日志就绪 */
    handle = XxxOpen(0);
    if (handle == NULL) { AppLogf("[XXX] open failed\n"); return NULL; }
    while (1) {
        /* 用平台接口操作设备 → 打印结果 → 让出 CPU */
        osDelay(MsToTicks(PERIOD_MS));
    }
    return NULL;
}

static void AppXxxEntry(void) { AppCreateTask(..., "app_xxx"); }
#if APP_DEMO_TASKS
SYS_RUN(AppXxxEntry);
#endif
```

**三条硬约束**
1. 应用只引用平台接口头（`gpio_if.h` / `uart_if.h` / `i2c_if.h` / `pwm_if.h` …），
   不引厂商头，也不引 SoC 头。
2. 用 `SYS_RUN` 注册，不从 `main()` 直接调。
3. 输出走板级格式化器，不用默认 `printf` 的缓冲路径（见下）。

## 任务设计：三个参数都要有理由

| 参数 | 原则 |
|---|---|
| 优先级 | **频率越低的设备给越低优先级**。1 Hz 的采样任务应该比 LED/心跳更低，绝不能最高 |
| 栈 | 按"实测峰值 + 30% 余量"。格式化输出、浮点运算的任务给 4 KB；只翻转 IO 的给 1 KB |
| 周期 | 打印类 300 ms 以上；采样类按物理量变化速度定，不要盲目 10 ms |

**反例（实测踩过）**：采样任务给了最高优先级 + 1M 次忙等上限 → 一次异常让它空转十几秒，
把所有低优先级任务（心跳、PWM、日志）全部饿死，表现为"系统周期性卡顿"。

**规则**：任务上下文里的忙等，上限按**毫秒**设，不按"次数"随手拍；能 `osDelay` 就让出。

## 输出：板级格式化器 + 无缓冲写

很多移植在 `-nostdlib` 下直接调 `printf` 会挂死——因为标准库的 stdio 需要堆与 `FILE` 缓冲。
正确做法是板级提供一个自实现的格式化器，以弱符号导出：

```c
/* board_print.c 片段 */
static int BoardVsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
__attribute__((weak)) int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap)
{ return BoardVsnprintf(buf, cap, fmt, ap); }
/* 同时导出 printf / snprintf / vprintf，覆盖库里的实现 */
```

支撑的转换至少要有：`c s d i u x X p %`，`-`/`0` 标志、十进制字段宽度、`.` 精度、`l`/`ll`/`z`。
（hctest 用 `%10s` 打印进度、`%s:%d:%s:PASS` 打印结果。）

应用层的封装：

```c
static void AppLogf(const char *fmt, ...)
{
    char buf[128]; va_list ap; int n;
    va_start(ap, fmt); n = vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if (n > 0) { (void)UartWrite(g_uartHandle, (uint8_t *)buf, (uint32_t)n); }
}
```

**分工**：早期启动日志用"直写寄存器"的函数（无缓冲，异常现场不丢）；应用任务用 `AppLogf`。
**禁止**在中断与异常处理里调用格式化输出（不可重入）。

## demo 门控：产品固件与测试固件分开

`app/BUILD.gn` 加开关，`app_main.c` 用它包住每个 `SYS_RUN`：

```gn
declare_args() { app_demo = true }
kernel_module("xxx_app") {
  if (app_demo) { cflags = [ "-DAPP_DEMO_TASKS=1" ] } else { cflags = [ "-DAPP_DEMO_TASKS=0" ] }
}
```

```c
#ifndef APP_DEMO_TASKS
#define APP_DEMO_TASKS 1
#endif
...
#if APP_DEMO_TASKS
SYS_RUN(AppXxxEntry);
#endif
```

XTS 固件用 `--gn-args app_demo=false` 构建：测试期间不应有业务任务抢 CPU、占栈、污染日志。

**判据**：`nm <ELF> | grep -c 'App.*Entry'` 应为 0；启动日志 `MODULE_INIT(run) : 0 entry`。

## 输出设计：可判读

| 差 | 好 |
|---|---|
| `printf("running...")` | `[PWM] CH1=30%  CH2=70%` |
| 只打印换算结果 | `chip temperature = 38.8 C  (raw=1629)` |
| 错误码不打印 | `conversion timeout, ret=-2` |

**原始值一定要打**。只打换算结果时，一旦数值异常，无法区分"采样错"还是"换算错"。

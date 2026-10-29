# N32H7xx EVAL / OpenHarmony LiteOS-M (mini) —— XTS 兼容性测试方案

> 目标：在 N32H7xx EVAL 板上跑通 OpenHarmony XTS（acts / hctest / C 语言）用例，并通过串口判定 PASS/FAIL。
> 本文是**待审核的实施方案**，不是已完成的记录。所有"待验证"项都显式标出来了。
> 参考：仓库内 `test/xts/` 源码 + 官方文档《XTS测评用例开发指导》+《轻量系统 STM32F407 芯片移植案例 / XTS兼容性测评子系统适配》。

---

## 1. 结论摘要（先看这个）

1. **XTS 树是完整的**（`test/xts/acts`、`tools/lite/hctest`、`tools/lite/build/suite_lite.gni`、`build_xts` gn 参数都在），**不需要移植 XTS 本身**。
2. 需要做的是**四项底层适配 + 一套编译/触发接线**，其中两项是我们工程特有的：
   - **`printf` 能力不足**（hctest 用 `%10s`，我们的格式化器不支持字段宽度）
   - **`test` 层没人触发**（`OHOS_SystemInit()` 只走 6 层，不含 `test`；全仓没有任何 C 代码遍历 `__zinitcall_test_*`）
3. **建议分阶段**：先只加 `kernel_lite/kernelcmsis_hal`（`ActsCMSISTest`，CMSIS-RTOS2，零外部子系统依赖）打通链路，再逐个加套件。
4. **必须分套件跑**：`hctest` 任务栈就要 6 KB，加上内核堆 112 KB 与用例静态数据，全量链接会撑爆且无法定位。

---

## 2. 现状盘点

### 2.1 仓库侧（已具备，不用动）

| 项 | 位置 | 状态 |
|---|---|---|
| XTS 测试用例树 | `test/xts/acts/`（含 `kernel_lite/` 15 个子目录） | ✅ |
| CMSIS 套件 | `test/xts/acts/kernel_lite/kernelcmsis_hal/`，target 名 **`ActsCMSISTest`**，7 个源文件 | ✅ |
| hctest 框架 | `test/xts/tools/lite/hctest/`（`hctest.c`、`hctest_service.c`、`include/hctest.h`） | ✅ |
| 套件编译模板 | `test/xts/tools/lite/build/suite_lite.gni` → `hctest_suite()`；`_include_dirs` 已含 `//test/xts/tools/**lite**/hctest/include` | ✅ 路径正确 |
| unity | `third_party/unity/src/` | ✅ |
| gn 参数 | `build/config/BUILDCONFIG.gn:469` `build_xts = false` | ✅ 可用 `--gn-args build_xts=true` |
| xts 子系统排除逻辑 | `build/lite/lite_target_list.gni:28` — `build_xts=false` 时整体排除 xts | ✅ 解释了"现在没编 XTS 也没出错" |
| acts 组件名 | `test/xts/acts/bundle.json` → `component.name = "acts"`, `subsystem = "xts"` | ✅ **与我们 config.json 一致** |

### 2.2 我们工程侧（已具备）

| 项 | 位置 | 状态 |
|---|---|---|
| `.zInit` 六层 + `test` 层段 | `device/soc/Nsing/n32h7xx/liteos_m/board.ld:220` | ✅ 段都在（`test` 层段已定义） |
| `OHOS_SystemInit()` | `base/startup/bootstrap_lite/.../system_init.c` | ✅ 已调用 |
| `xts` 子系统声明 | `vendor/Nsing/n32h7xx_eval/config.json` | ✅ 有（`component: acts`） |
| `hal_sys_param.c` | `vendor/Nsing/n32h7xx_eval/hals/utils/sys_param/` | ⚠️ 只实现了 `HalGetSerial()` |
| `printf`/`vsnprintf` | `device/board/.../board/src/board_print.c`（weak 符号） | ⚠️ 能力不足，见 A1 |
| `samgr_lite` / `bootstrap_lite` / `hilog_lite` / `hievent_lite` / `utils_lite(file)` | `config.json` | ✅ 已在 |
| 板级 `BUILD.gn` | `device/board/Nsing/n32h7xx_eval/liteos_m/board/BUILD.gn` | ⚠️ 无 `build_xts` 的 whole-archive 配置 |

### 2.3 缺口总表

| # | 缺口 | 严重度 | 不做的后果 |
|---|---|---|---|
| **A1** | `printf` 不支持 `%Ns`（字段宽度用于 `%s`） | **阻塞** | 用例结果行错乱；`%10s` 原样打印 |
| **A2** | `hal_sys_param.c` 缺 18 个 `HalGet*` | **阻塞** | `ActsParameterTest` 链接失败；兼容性信息为空 |
| ~~A3~~ | ~~**`test` 层无人触发**~~ → **2026-10-13 证伪，该改动已移除**（见 §A3） | — | ~~阻塞~~ 已证伪 |
| **A4** | 板级 `config("public")` 未加 whole-archive | **阻塞** | 用例静态库不被链接进镜像 |
| **A5** | **`xts.json` 里没有 `acts` 组件** → `build_xts=true` 时 assert 极可能直接失败 | **阻塞** | XTS 根本编不出来（见 §A5） |
| **A6** | 内存预算未评估 | 中 | 全量链接失败或运行时 OOM |

---

## 3. 必须做的底层适配

### A1 —— 扩展 `board_print.c` 的格式化能力【阻塞】

**现象/依据**：`grep` 出 hctest 框架用到的格式符只有三个：`%d`、`%s`、**`%10s`**。
`third_party/unity` 另用 `%.7g` / `%.9g`（浮点，仅浮点断言失败时打印）。

**我们现状**：`board_print.c` 只支持 `%c %s %d %i %u %x %X %p %ld %lu %lx %%` + `0` 标志 + 宽度（**宽度只对整数生效**）。

**改动**：`device/board/Nsing/n32h7xx_eval/liteos_m/board/src/board_print.c`
- `%s` 支持字段宽度 → `%10s`（右对齐补空格）
- 顺手支持 `%-10s`（左对齐）与 `%.Ns`（截断），成本极低
- 建议再补 `%zu` / `%llu` / `%lld`，避免其他套件踩坑

**验收**：
```c
printf("[%10s]|[%-10s]\r\n", "abc", "abc");
/* 期望: [       abc]|[abc       ] */
```

**已知限制（先不做，但要记录）**：浮点 `%g/%f` 不支持。`kernelcmsis_hal` 不涉及浮点，可先跑；若后续套件有浮点断言，再补（届时需要软浮点格式化，成本较高）。

---

### A2 —— 补全 `hal_sys_param.c`【阻塞】

**依据**：`base/startup/init/interfaces/hals/hal_sys_param.h` 声明了 **19 个**接口：
```
HalGetDeviceType / Manufacture / Brand / MarketName / ProductSeries / ProductModel /
SoftwareModel / HardwareModel / HardwareProfile / Serial / BootloaderVersion / AbiList /
DisplayVersion / IncrementalVersion / BuildType / BuildUser / BuildHost / BuildTime
+ int HalGetFirstApiVersion(void)
```
**我们只实现了 `HalGetSerial()`。**

**改动**：`vendor/Nsing/n32h7xx_eval/hals/utils/sys_param/hal_sys_param.c`，按官方 STM32F407 案例的格式补齐，值按我们的板子写。**关键一条**：

```c
/* 我们是 Cortex-M7 + 硬浮点 fpv5-d16，不是 M4/fpv4 —— 别照抄参考值 */
static const char OHOS_ABI_LIST[] = {"armm7_hard_fpv5-d16-liteos"};
static const char OHOS_HARDWARE_PROFILE[] = {"RAM:192K,ROM:1M"};   /* 按实际改 */
```

**验收**：`ActsParameterTest` 能链接通过；串口能打印出产品信息（非空）。

---

### A3 —— 让 `test` 层被执行【⚠️ 2026-10-13 证伪：不需要做，而且做了有害】

> **修正**：本节原先的结论（"mini 上没人触发 `test` 层"）**是错的**。
>
> 真相在 `test/xts/tools/lite/hctest/src/hctest_service.c`（上游代码）：
> ```c
> SYSEX_SERVICE_INIT_PRI(Init, 4);      /* TestService 注册进 samgr */
> static BOOL Initialize(...) {
>     /* Original: self-message to defer xts until boot completes. */
>     SAMGR_SendRequest(&testService->identity, {.msgId = MSG_START_TEST}, (Handler)MessageHandle);
> }
> static BOOL MessageHandle(...) {
>     case MSG_START_TEST:
>         INIT_TEST_CALL();             /* ★ 官方就是靠这个遍历 test 层 */
> }
> ```
> → `SAMGR_Bootstrap()`（`OHOS_SystemInit()` 末尾）让服务给自己发消息，
> 服务的任务收到后调 **`INIT_TEST_CALL()`** 遍历 `test` 层跑套件 —— **hctest 自己会跑**，
> 而且是在 `LOS_Start()` 之后（服务任务需要调度器）。
>
> **我当时的错**：只 grep 了 `__zinitcall_test_start`，看到"只出现在链接脚本里"就下结论
> "没人触发"，**漏了 `INIT_TEST_CALL()` 宏**（展开后才含 `__zinitcall_test_*`）。
> 于是按本节在 `main()` 里又遍历了一遍 → **每个套件被执行两次** ✗
>
> **已移除该改动**（`main.c` 里现在只打印"登记了几个 entry"，不执行）。
> 教训：追"谁用了某个段符号"，要往上追一层到宏，不能只 grep 符号名。
>
> 下面原文保留，仅供追溯。

**原文（已作废）**：mini 上没有任何代码遍历 `__zinitcall_test_start..end`，需要 `main()` 补触发。

#### A3-原文：让 `test` 层被执行

**依据（源码事实）**：
```c
/* base/startup/bootstrap_lite/services/source/system_init.c —— 全文只有这些 */
void OHOS_SystemInit(void)
{
    MODULE_INIT(bsp); MODULE_INIT(device); MODULE_INIT(core);
    SYS_INIT(service); SYS_INIT(feature); MODULE_INIT(run);
    SAMGR_Bootstrap(); LiteParamService();
}
```
```c
/* test/xts/tools/lite/hctest/include/hctest.h —— 套件执行入口挂在 test 层 */
#define RUN_TEST_SUITE(test_suite)                        \
    static void runSuite##test_suite(void) { ... RunTestSuite(#test_suite); } \
    TEST_INIT(runSuite##test_suite);      /* TEST_INIT → LAYER_INITCALL_DEF(..., test, "test") */
```
**`__zinitcall_test_start/end` 只出现在链接脚本里**（`board.ld:220`、`liteos.ld`、`ESP32.ld`、`link.ld.S`），
**没有任何 C 代码遍历它**；`RunAllXtsTests` / `RunTestSuite(` 在 hctest 之外也没有调用者。

→ **结论：套件与用例会通过 `SYS_SERVICE_INIT` / `SYS_RUN` 正常注册（这两层 `OHOS_SystemInit` 会走），
但"执行"这一步挂在 `test` 层，mini 上默认不会被触发。**

**改动**：`device/board/Nsing/n32h7xx_eval/liteos_m/board/src/main.c`
在 `OHOS_SystemInit()` **返回之后**、`LOS_Start()` **之前**，手动遍历 `test` 层：

```c
#if BOARD_RUN_XTS
    /* XTS: OHOS_SystemInit() walks bsp/device/core/sys_service/sys_feature/run only.
       The hctest execution entry (RUN_TEST_SUITE -> TEST_INIT) lands in the `test`
       layer, which nothing calls on mini. Trigger it here, after SAMGR_Bootstrap()
       (hctest_service registers with samgr) and before LOS_Start(). */
    {
        BootInitCall *p = __zinitcall_test_start;
        while (p < __zinitcall_test_end) {
            (*p)();
            p++;
        }
    }
#endif
```
`__zinitcall_test_start/end` 已在 `main.c` 的 extern 声明清单里（打印 `.zInit` 表时用过）→ **零新增依赖**。

**顺序依据**：此时 `sys_service` 层的套件注册、`run` 层的用例注册都已完成，`SAMGR_Bootstrap()` 也已跑过 → `GetTestMgrInstance()` 可用。

**待确认（实现时验证）**：`hctest_task_type = "SINGLE_TASK"` +
`hctest_task_stack_size = 6144` 说明套件执行会**创建任务**。
- 若 `LOS_TaskCreate` 在 `LOS_Start()` **之前**调用 → LiteOS-M 允许（只入队，启动后调度）✅
- 若 `hctest` 内部依赖调度器已运行 → 需要把触发点挪到 `LOS_Start()` 之后，或改由 `SYS_RUN` 起的应用任务里触发

**验收**：串口出现 `Start to run test suite:...` / `xx Tests xx Failures xx Ignored` / `All the test suites finished!`。

---

### A4 —— 板级链接配置加 `build_xts` 的 whole-archive【阻塞】

**依据**：官方案例要求在 `config("public")` 里，仅在 `build_xts` 为真时用 `--whole-archive` 强制链接套件静态库
（否则未被引用的静态库会被 `--gc-sections` 丢掉 → 用例根本不进镜像）。

**改动**：`device/board/Nsing/n32h7xx_eval/liteos_m/BUILD.gn`（板级 company 层 `config("public")`，即提供 `-nostartfiles`/libs 的那个）新增：

```gn
if (build_xts) {
  lib_dirs = [ "$root_out_dir/libs" ]
  ldflags += [
    "-Wl,--whole-archive",
    "-lbootstrap", "-lbroadcast", "-lhctest",
    "-lmodule_ActsCMSISTest",          /* 阶段 1：只加这一个 */
    "-Wl,--no-whole-archive",
  ]
}
```

**注意**：库名规则是 `libmodule_<target_name>.a`（官方样例 `-lmodule_ActsDfxFuncTest`）；
`kernelcmsis_hal` 的 target 是 `ActsCMSISTest` → **`-lmodule_ActsCMSISTest`**（**待验证**：需在 `out/.../libs/` 里确认实际文件名）。

**验收**：`nm OHOS_Image | grep -i cmsis_` 能看到用例符号；`.zInit` 的 `sys_service`/`run` 层条目数增加。

---

### A5 —— `config.json` 组件核对【✅ 2026-10-08 实测：不阻塞，无需处理】

> **修正**：2026-10-08 用 `hb build -b debug --gn-args build_xts=true` 实测 ——
> `xts.json` 里确实仍然只有 `device_attest_lite`（没有 `acts`），**但 `assert(component_found)` 并没有触发**，
> `gn gen` 正常通过，XTS 也正常编进镜像。
> 所以本节下面那段"阻塞"结论**作废**。原文保留仅供追溯。

**现状**：我们写的是
```json
{ "subsystem": "xts", "components": [ { "component": "acts", "features": [] } ] }
```
**核对结果**：`test/xts/acts/bundle.json` 里 `component.name = "acts"` → **名字是对的** ✓
（官方文档用的 `xts_acts`/`xts_tools` 是旧版命名，本仓库是 `acts`。）

**⚠️ 已确认为坑（2026-09-15 实测）**：`out/.../build_configs/mini_adapter/xts.json` 的 parts
**只有 `device_attest_lite`，没有 `acts`**；而 `build/lite/lite_target_list.gni:47` 有
`assert(component_found, "Component ... not found")`。

- 现在（`build_xts=false`）没问题：同文件第 28 行把整个 `xts` 子系统跳过，assert 不执行 —— 这就是"没编 XTS 也不报错"的原因；
- **一旦 `build_xts=true`，`xts` 子系统被纳入 → 极可能直接 assert 失败**（`Component "acts" not found`）；
- 另外 `test/xts/tools/bundle.json` 存在（组件 `tools`），我们也没有声明。

**处理办法（按优先级）**：

1. **先跑一次** `hb build -f -b debug --gn-args build_xts=true`，确认 assert 是否真的触发（最快，一条命令）；
2. 若触发：查 `test/xts/acts/bundle.json` 为何没被收集进 `xts.json`
   —— 参考"应用接入"的经验（见 §A7），组件是靠**目录下的 `bundle.json` 被 hb 自动扫描**汇总到
   `build_configs/mini_adapter/<子系统>.json` + `parts_info/parts_modules_info.json` 的；
   `test/` 目录下的收集规则可能不同（或需要 `xts_tools` 配合），这是最可能的卡点；
3. 若只是 `tools` 缺，则在 `config.json` 的 `xts` 子系统下补 `{"component": "tools", "features": []}`。

> 佐证：`test/xts/acts/bundle.json` 的 `adapted_system_type` **含 `mini`**、`rom: ~256KB`
> —— 官方本来就支持 mini，所以这更像"组件收集/注册"环节的问题，不是能力问题。

---

### A7 —— 应用接入范式（**已在本项目实测走通，可直接照搬**）

迁移 demo 到 `applications/` 时把这条链路验证通了，XTS 的组件接入可以照同一套思路排查：

1. **组件靠目录下的 `bundle.json` 注册**（不是老的 `build/lite/components/*.json`，那个目录现在只剩 `communication.json`）。
   关键字段：`component.{name, subsystem, adapted_system_type}` + `component.build.sub_component = ["//<路径>:<target>"]`。
2. **hb 自动扫描并汇总**到：
   - `out/.../build_configs/mini_adapter/<subsystem>.json` → `{"parts": [组件名...]}`
   - `out/.../build_configs/parts_info/parts_modules_info.json` → **组件名 → gn target 列表**的映射
   → **新增 `bundle.json` 后 `hb build` 会自动收集，不需要手改清单**（实测确认）。
3. **`config.json` 里声明不存在的组件会直接 `assert` 失败**（`lite_target_list.gni:47`）。
4. **组件会被强制链接**：`build/lite/toolchain/clang.gni:118-122` 对 `build_target_list` 用
   `-Wl,--whole-archive ... --no-whole-archive`。
   → 这就是**应用里的 `SYS_RUN()` 不需要额外配置就能进镜像**的原因；
   → 也解释了为什么 XTS 需要**手动**加 whole-archive（XTS 不在 `lite_target_list` 里，是另一条路径）。

---

### A6 —— 内存预算与分套件策略【中】

**已知开销**：
| 项 | 大小 | 来源 |
|---|---|---|
| 内核堆 | 112 KB（`_Min_Heap_Size = 0x1C000`） | `board.ld` |
| mux / sem 池 | 6 KB + 2 KB | `target_config.h`，已含 HDF 用量 |
| hctest 任务栈 | **6 KB**（`hctest_task_stack_size = 6144`） | `hctest_opt_args.gni` |
| 套件静态数据 | 各套件不同，`acts` bundle 标称 `rom: ~256KB` | `test/xts/acts/bundle.json` |

**策略**：**一次只链一个套件**（`--whole-archive` 列表里只放一个 `-lmodule_*`）。
`.bin` 现在 144 KB，Flash 富余（N32H78x 1 MB）→ 瓶颈在 **RAM**（AXI SRAM 112 KB 堆 + 64 KB 上下文区）。

**验收**：新增 `nm`/`size` 对比；若 `bss` 增长过大，先调 `hctest_task_stack_size` 与内核堆的平衡。

**可选优化（先不用）**：`hctest_opt_args.gni` 提供 `hctest_rodata_opt` / `xts_overlay` 两个开关（默认都 `false`）。
打开会启用 `HCTEST_NEW_RUNNER` + `.xts_init` 段机制 —— **那需要额外改 `board.ld`**，且会改变注册机制，
**建议第一步不要开**，等基础链路通了再评估。

---

## 4. 编译与运行

### 4.1 编译命令

```bash
cd $SRC            # 例如 /home/<user>/oh_n32h7xx/OHOS_MINI
export PATH=$HOME/.local/bin:$PATH

# XTS 只在 debug + build_xts=true 时参与编译
hb set -p n32h7xx_eval          # 首次需要
hb build -f -b debug --gn-args build_xts=true

cd out/n32h7xx_eval/n32h7xx_eval
cp OHOS_Image OHOS_Image_xts.elf
arm-none-eabi-objcopy -O ihex   OHOS_Image_xts.elf OHOS_Image_xts.hex
arm-none-eabi-objcopy -O binary OHOS_Image_xts.elf OHOS_Image_xts.bin
```

> ⚠️ 本项目已知坑：**改了 `.h` 必须 `find out/.../obj -name "*.o" -delete` 再 build**（增量构建不跟踪头依赖）。
> 首次开 `build_xts` 属于 gn 参数变化，会重新生成构建图，但若出现"改了没反应"，先做全量重编。

### 4.2 运行方式

- mini **没有 shell**，套件靠 `test` 层触发（见 A3）后自动跑，结果**全走串口**。
- 保持串口日志完整（115200 8N1），**从复位开始录**，因为 `.zInit` 表和套件输出都在这条线上。

### 4.3 阶段 1 的套件选择

| 套件 | target | 为什么先跑它 |
|---|---|---|
| `kernel_lite/kernelcmsis_hal` | `ActsCMSISTest` | 纯 CMSIS-RTOS2（task/sem/mutex/msg/event/timer），**不依赖额外子系统**，最贴近我们已跑通的 KAL 层 |

> ⚠️ 注意：官方的 `liteos_m` XTS 列表（`test/xts/acts/build_lite/BUILD.gn` 第 36~52 行）里**没有 `kernel_lite`**
> —— `ActsKernelTest` 只挂在 `liteos_a` 分支（那些是 `*_posix` 套件，mini 跑不了）。
> **`ActsCMSISTest` 需要我们自己加进 liteos_m 的 features 列表**（见第 6 节步骤 2）。

---

## 5. 结果判读

```
Start to run test suite:taskFuncTest        ← 套件开始
taskFuncTest.c:590:testTaskCreate001:PASS   ← 每条例局 文件:行:用例名:PASS/FAIL
+-------------------------------------------+
 32 Tests 0 Failures 0 Ignored              ← 汇总（官方样例格式）
OK
All the test suites finished!               ← 全部结束
```

| 现象 | 含义 |
|---|---|
| 完全没有 `Start to run test suite` | `test` 层没被触发（A3 未生效）或套件库没被链接（A4） |
| 有 `Start...` 但无任何用例行 | `hctest` 的套件/用例注册没跑到（`SYS_SERVICE_INIT`/`SYS_RUN` 未执行） |
| 输出格式错乱（出现 `%10s` 字面量） | A1 未生效 |
| 跑到一半卡死 | 内存/栈不足（A6），或某个用例触发未适配的底层 |
| `Failures > 0` | 按 `文件:行号` 定位用例源码 |

---

## 6. 分阶段实施计划

| 步骤 | 内容 | 改动文件 | 验收 |
|---|---|---|---|
| **0** | 记录基线 | — | 存一份当前 `OHOS_Image.bin` 的 `size`/`nm` 快照 |
| **1** | A1 + A2（printf + sys_param） | `board_print.c`、`hal_sys_param.c` | 正常 `hb build` 通过；串口可打印 `%10s`；`HalGet*` 全部有实现 |
| **2** | 把 `ActsCMSISTest` 加进 liteos_m 列表 | `test/xts/acts/build_lite/BUILD.gn` | gn gen 通过，`out/.../libs/` 出现 `libmodule_ActsCMSISTest.a` |
| **3** | A4（whole-archive） | 板级 `BUILD.gn` | `nm OHOS_Image_xts.elf \| grep -i cmsis` 有符号 |
| **4** | A3（触发 `test` 层） | `main.c` | 串口出现 `Start to run test suite` 与汇总行 |
| **5** | 逐套件扩展 | `build_lite/BUILD.gn` + whole-archive 列表 | 每加一个都单独跑一轮，记录 RAM 增量 |

**每步都要"改一处 → 编译 → 烧录 → 看串口"，不要一次改多项。**

> 我们已经有现成的观测手段：`main.c` 打印的 **`.zInit` 六层条目数**。
> 加 XTS 后 `sys_service` 应从 `1 entry` 变大、`run` 从 `1 entry` 变大（用例数）——
> **这是"XTS 确实挂上去了"最直观的证据**，比翻代码可靠。

---

## 7. 风险与已知限制

| # | 风险 | 影响 | 应对 |
|---|---|---|---|
| R1 | `test` 层触发点放在 `LOS_Start()` 之前，而 `hctest` 需要调度器 | 卡死/不执行 | 若 A3 位置不行，改为在 `SYS_RUN` 的应用任务里触发（延后到调度器起来后） |
| R2 | 浮点格式化 `%g` 不支持 | 浮点断言失败信息错乱 | 阶段 1 不涉及；如需，单独补软浮点格式化 |
| R3 | `libmodule_ActsCMSISTest.a` 实际名与推测不符 | 链接找不到库 | 先 `ls out/.../libs/ \| grep Acts` 确认再用 |
| R4 | whole-archive 拉入过多代码 | Flash/RAM 撑爆 | 一次只加一个套件（A6） |
| R5 | `hctest` 依赖 samgr，而我们的 samgr 是"最小可用" | `GetTestMgrInstance()` 失败 | 阶段 1 若卡在这里，先确认 `SAMGR_Bootstrap()` 返回值与 `TestService` 注册 |
| R6 | `hb build` 头依赖坑 | 改了没反应，误判 | 改头文件后全量重编 + `strings`/`nm` 验证 |
| R7 | 兼容性测评的**完整**要求远不止这些套件 | 拿不到正式证书 | 本方案只保证"能在板上跑起来并看到结果"；正式测评需要按 `compatibility` SIG 清单逐项对齐（含 `hal_sys_param` 全部字段、必跑套件集） |

---

## 8. 附录 A：官方要求 vs 本方案

| 官方《STM32F407 移植案例》要求 | 我们的情况 | 本方案 |
|---|---|---|
| `config.json` 加 `xts_acts` + `xts_tools` | 已有 `xts/acts`；`tools` 未声明 | A5 核对后决定是否补 |
| 板级 `config("public")` 加 `build_xts` 的 `--whole-archive` | 无 | **A4** |
| `hal_sys_param.c` 填正确产品信息 | 只有 `HalGetSerial` | **A2** |
| 链接脚本含 `_hdf_drivers_start/end` + `.hdf.driver` | 已有 | 不用动 |
| 内核启动后调 `DeviceManagerStart()` | 已有 | 不用动 |
| `hb build -f -b debug --gn-args build_xts=true` | 参数可用 | 第 4 节 |
| 分套件测试（RAM 受限） | — | **A6** |
| **官方没提**：`test` 层没人触发 | — | **A3（我们实测发现的）** |
| **官方没提**：`printf` 需要字段宽度 | — | **A1（我们实测发现的）** |

---

## 附录 B：关键源码索引（改动时直接查）

| 用途 | 路径 |
|---|---|
| 套件/用例注册宏、`TEST_INIT` | `test/xts/tools/lite/hctest/include/hctest.h`（35、138-179 行） |
| hctest 执行逻辑 | `test/xts/tools/lite/hctest/src/hctest.c` |
| hctest 任务参数 | `test/xts/tools/lite/hctest/hctest_opt_args.gni` |
| 套件编译模板 | `test/xts/tools/lite/build/suite_lite.gni`（101-152 行） |
| CMSIS 套件 | `test/xts/acts/kernel_lite/kernelcmsis_hal/BUILD.gn`（target `ActsCMSISTest`） |
| liteos_m 套件清单 | `test/xts/acts/build_lite/BUILD.gn`（35-57 行） |
| initcall 层宏 | `commonlibrary/utils_lite/include/ohos_init.h`（116、166 行） |
| `OHOS_SystemInit` | `base/startup/bootstrap_lite/services/source/system_init.c` |
| 产品参数接口 | `base/startup/init/interfaces/hals/hal_sys_param.h` |
| `test` 层链接段 | `device/soc/Nsing/n32h7xx/liteos_m/board.ld:220` |
| 我们的 printf | `device/board/Nsing/n32h7xx_eval/liteos_m/board/src/board_print.c` |

---

## 附录 C：顺带回答——为什么 demo 不放 `applications/`

**现状**：仓库里 `applications/` 只有 `sample/camera`（Hi3516 标准系统）和 `sample/wifi-iot/app`（mini 示例）。
**我们的 demo 放在 board 层**（`device/board/Nsing/.../board/src/app_hdf_demo.c`），原因是：

1. **它和 board 强耦合**：LED 引脚（PB3/PF10/PI8）与共阳极（低电平点亮）都是**板级事实**。
   放进 `applications/` 就要么把这些知识"泄漏"过去，要么在那边再写一份板级宏 —— 违反职责边界。
2. **构建接线最省**：board 的 `BUILD.gn` 已经编译它；迁到 `applications/` 需要新增 component + 在 `config.json` 声明 + 调依赖链。
3. **当时的优先级**：先让 HDF 闭环跑起来，比目录整洁重要。

**现在正是迁移的时机**（当初约定"等 M5 通了再一次性迁"）。建议的迁法：

```
applications/sample/n32h7xx_eval/app/
  ├── BUILD.gn           # hctest 无关，普通 kernel_module
  └── src/app_main.c     # 只留业务策略：周期、任务、日志格式
```
- **板级事实**（3 个 LED 的引脚号 + 共阳极）移到 `device/board/.../board/include/board_led.h`，例如
  `#define BOARD_LED_RED_GPIO   (19)`（HDF 逻辑引脚号）
- app 只 `#include "board_led.h"`，**不再出现 `1 * 16 + 3` 这类编号运算**
- `vendor/.../config.json` 增加 `applications` 子系统的 component 声明
- 好处：board 答"硬件是什么"，app 答"业务怎么做"；将来多产品可复用 app

**代价**：多一层 gn 接线；且 `applications` 默认不在 mini 构建里，需在 `config.json` 显式声明。

> 另外，A1/A2 落地后如果要做 `GpioGetByName("PB3")` 那种"引脚号完全从应用消失"的写法，
> 需要驱动在 `GpioCntlrAdd()` 前预填 `ginfos[].name`（框架只在 `name` 为空时才生成默认名）。
> 这是迁移时顺手可以做的第二步。

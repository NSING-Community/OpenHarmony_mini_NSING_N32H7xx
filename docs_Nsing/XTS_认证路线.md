# N32H7xx EVAL / OpenHarmony 6.1 mini —— XTS 测试与认证路线

> 本文回答一个问题：**移植完成后，怎么把 XTS 跑起来并通过兼容性认证。**
> 技术实现细节（格式化器、`test` 层触发、whole-archive 等）见 **[`XTS_测试方案.md`](XTS_测试方案.md)**，本文不重复。
> 事实来源：本地仓库 `D:\OHOS_MINI` + 官方《XTS测评用例开发指导》+ 轻量系统认证经验文档。

---

## 0. 结论先行

| 判断 | 依据 |
|---|---|
| **认证用的 XTS 不是从我们源码树里编的**，而是从 **OpenHarmony 官网「兼容性 XTS 专区」**下载对应版本的套件包 | 官方流程步骤 2 明确写了"从官网兼容性 XTS 专区获取兼容性测试套件" |
| **轻量系统必须"分批烧录"** —— 每个测试模块一个固件镜像 | 官方轻量系统认证经验：*"芯片内存有限，无法一次性将全部 XTS 用例库链接进程序"* |
| **认证只考核 `acts` + `hats` 这两条线**，`dcts` 与 mini 无关 | `dcts/bundle.json` 的 `adapted_system_type = ["standard"]` |
| **我们的技术差距集中在 `hats`**：`acts` 在 mini 上有现成路径，`hats` 没有 | `acts/build_lite/BUILD.gn` 有 `liteos_m` 分支；`hats/test_packages.gni` 只有 standard/small 的 `is_standard_system` / `hats_rich` 判据 |
| **最短可行路线**：先用**源码树的 XTS 打通编译+运行链路**（开发期预研），认证时再换成**官网下载的官方套件包** | 官网包与源码树同源但版本可能有差异；先把链路打通，认证阶段只换包不换机制 |

---

## 1. 官方认证流程（6 步）

```
步骤 1  在开放原子开源基金会网站申请「企业账号」
步骤 2  从 Gitee 取代码适配开发
        ├─ 从 OpenHarmony 官网「兼容性 XTS 专区」下载套件 → 本地自测 → 导出串口日志报告
        ├─ 从「兼容性 PCS 专区」下载并填写 PCS 自检表
        └─ 失败项如需豁免，在兼容性平台申请
        （自测与 PCS 也可委托「兼容性测评合作中心」）
步骤 3  签《OpenHarmony 兼容性协议》+《隐私声明》
        上传：测试报告 + PCS 自检表 + 镜像  →  兼容性平台
        并寄送：产品样品
步骤 4  兼容性工作组测评 → 通过 / 通知整改
步骤 5  可能被抽中「复测」：工作组下发复测套件包 → 本地跑 → 上传复测报告
步骤 6  复测评审通过 → 本次测评通过
```

**平台地址**
- 兼容性平台（申报）：`https://compatibility.openharmony.cn/console`
- XTS 专区：`https://www.openharmony.cn/certification/document/xts`
- PCS 专区：`https://www.openharmony.cn/certification/document/pcs`
- 合作中心：`https://www.openharmony.cn/certification/cooperationCenter`

### 1.1 轻量系统的特殊要求（**最容易踩的部分**）

| 要求 | 说明 |
|---|---|
| **分批烧录** | 内存放不下全部套件 → 每个测试模块一个镜像，逐个烧录、抓串口日志 |
| **每个模块一个 txt 报告** | 自测导出的串口日志按模块分别保存 |
| **版本信息必须一致** | 软件版本号 / 安全补丁标签 / 版本 ID / **版本 Hash** 必须与测试日志里打印的一致 |
| **产品定义必须与源码一致** | 申报时填的操作系统发布版本、设备版本号、**芯片型号**要和代码里填的一样 |
| **要交可烧录的东西 + 指导文档** | 镜像 + 烧录工具/步骤说明；样品要把烧录口引出来 |

> ⚠️ 「芯片型号必须与源码一致」这条直接对应我们 `vendor/Nsing/n32h7xx_eval/hals/utils/sys_param/hal_sys_param.c`
> 的 `HalGetHardwareModel()` / `HalGetProductModel()` 等 —— 现在**只实现了 `HalGetSerial()`**，
> 这是认证前的硬性前置（见 §4）。

---

## 2. 现状盘点：我们站在哪

### 2.1 已经具备

| 项 | 状态 |
|---|---|
| 启动链（`LOS_KernelInit` → `DeviceManagerStart` → `OHOS_SystemInit` → `LOS_Start`） | ✅ 实机验证 |
| HDF 平台驱动（GPIO 144 脚 + UART） | ✅ 实机验证 |
| 应用层（`applications/sample/n32h7xx_eval`，`SYS_RUN` 自动拉起） | ✅ 实机验证 |
| 串口控制台 115200 8N1，日志可分块解析 | ✅ |
| 本地可分析/可编译/可取产物（`ohos_sync.sh`） | ✅ |
| 源码树里的 XTS 全树（`acts/dcts/hats/device_attest_lite/tools`） | ✅ 存在 |

### 2.2 缺口

| # | 缺口 | 类别 | 阻塞度 |
|---|---|---|---|
| **G1** | `printf` 不支持 `%10s`（hctest 用它） | 技术 | **阻塞** |
| **G2** | `hal_sys_param.c` 只实现了 19 个接口中的 1 个 | 技术 + 认证 | **阻塞** |
| ~~G3~~ | ~~`test` 层无人触发（`OHOS_SystemInit` 只走 6 层）~~ → **2026-10-13 证伪**：hctest 的 `TestService` 经 `SAMGR_Bootstrap()` → `MSG_START_TEST` → **`INIT_TEST_CALL()`** 自己就会遍历 `test` 层（在 `LOS_Start()` 之后）。**在 `main()` 里再遍历一遍会让每个套件跑两遍** | — | ~~阻塞~~ **该改动已移除** |
| **G4** | 板级 `config("public")` 没有 `build_xts` 的 whole-archive | 技术 | **阻塞** |
| ~~G5~~ | ~~`xts.json` 里没有 `acts` 组件 → `build_xts=true` 时 assert 会失败~~ → **2026-10-08 实测不触发**（`build_xts=true` 正常通过 gn gen），无需处理 | — | ~~阻塞~~ **已消除** |
| **G6** | `hats` 在 mini 上没有构建路径（只有 `is_standard_system`/`hats_rich`） | 技术 | **阻塞（hats 线）** |
| **G7** | 缺文件系统 / kv_store / huks / lwip 等组件 → 部分 acts 套件跑不了 | 组件 | 中 |
| **G8** | 企业账号、协议签署、样品寄送 | 流程 | 中（需人操作） |
| **G9** | 版本 Hash / 版本 ID 的生成与打印规范 | 流程 + 技术 | 中 |

---

## 3. 关键决策：认证用哪套 XTS？

**两条线，不要混。**

| | A. 开发期预研（现在做） | B. 正式认证（后做） |
|---|---|---|
| 套件来源 | **本地源码树 `test/xts`** | **官网「兼容性 XTS 专区」下载的对应版本包** |
| 目的 | 把**编译链路 + `test` 层触发 + 串口报告格式**全部打通 | 产出**可用于申报的正式测试报告** |
| 好处 | 有问题能改代码、能调试；不依赖外部流程 | 版本严格对应，工作组认可 |
| 风险 | 若官网包结构与源码树有差异，B 阶段要重做接线 | —— |

**先 A 后 B 的理由**：A 阶段把 4 个技术缺口（G1~G4）全修掉，且是**通用**的 ——
B 阶段无论官网包长什么样，机制（`hctest` + `suite_lite.gni` + whole-archive + `test` 层）都一样，
只需换套件源码。**反过来先做 B，会在"链路没通"的情况下被外部流程卡死。**

---

## 4. 必须补的技术适配（按依赖顺序）

> 技术细节与验收方法见 [`XTS_测试方案.md`](XTS_测试方案.md) 的 A1~A6。这里只列顺序与认证关联。

| 顺序 | 项 | 关联 |
|---|---|---|
| 1 | **`printf` 补 `%s` 字段宽度**（G1） | 没有它，套件日志是乱码，`acts`/`hats` 都跑不了 |
| 2 | **`hal_sys_param` 补全 19 个接口**（G2） | `ActsParameterTest` 要链接；**认证申报的"产品定义"直接读它** |
| 3 | ~~**`test` 层触发**（G3）~~ → **不需要改动**（hctest 自己会跑，见 G3 行） | — |
| 4 | **板级 whole-archive + `xts` 组件注册**（G4/G5） | 没有它，用例根本不进镜像 |
| 5 | **`hats` 的 mini 构建路径**（G6） | 见 §4.1 |
| 6 | **版本 Hash / ID 打印规范**（G9） | 认证要求日志里的版本信息与申报一致 |

### 4.1 `hats` 的 mini 适配（新增，之前方案里没覆盖）

**事实**（本地读到的）：
```
test/xts/hats/BUILD.gn          → ohos_test_suite("xts_hats") / merge_xts_notice —— standard 体系
test/xts/hats/test_packages.gni → 只有 selected_packages / _ivi / _intellitv / _wearable
                                  没有 liteos_m / mini 分支
test/xts/hats/kernel/BUILD.gn   → if (is_standard_system) { ... }
test/xts/hats/hdf/light/BUILD.gn → if (hats_rich) { } else { hdiCommonTest / hdiService }
```
**而 `acts` 有**：`acts/build_lite/BUILD.gn`（`liteos_m` 分支）+ `suite_lite.gni` + `hctest` 框架。

**结论**：`hats` 虽然 `bundle.json` 标了 `["mini","small","standard"]`，
但**构建脚本是 standard/small 体系**，mini 上要自己接线。

**可选做法（按代价排序）**：
1. **借用 `acts` 的 lite 链路**：把需要的 `hats/hdf/*` 用例改成 `hctest_suite`，挂进 `acts/build_lite` 同款清单
   —— 最省，但改动 XTS 源码（认证时官网包未必允许改）。
2. **在板级/产品侧接线**：像 `acts` 一样，按 `suite_lite.gni` 写一个 `hats/build_lite`，只挑 mini 相关模块
   —— 不改 XTS 源码，推荐。
3. **暂缓 `hats`**，先只做 `acts` 线 —— 但**认证大概率要求 `hats`**，不建议跳过。

**mini 上真正相关的 `hats` 模块**（需逐个确认依赖）：
```
hats/hdf/light       ← 灯（和我们的 LED 驱动对口）
hats/hdf/hdf_lite    ← HDF 框架本身
hats/hdf/sensor / input / display / audio / camera / codec / wlan / bt ...
                     ← 我们没有这些外设，需要确认"无硬件时是 PASS 还是 SKIP"
hats/kernel          ← 目前只在 is_standard_system 下挂载，mini 需确认
hats/startup         ← 启动恢复
```
> **待确认（重要）**：认证对"设备没有该外设"的模块是**允许跳过/豁免**，还是**必须跑**。
> 这决定我们要不要为没做的外设写适配。→ 需要在 PCS 自检表 / 工作组沟通中确认。

---

## 5. 分批烧录策略：怎么切模块

**硬约束**：`AXI_SRAM` 只有 128 KB（堆 92 KB + 栈 4 KB + `.data/.bss`）；
单套件固件 `.bin` 已到 163~252 KB（未启用 XTS 时 144 KB）。
一次把全部套件链进去必然撑爆 → **一个套件一个镜像**。

**实测结果（2026-10-08）**：6 个核心套件分别链接**全部成功**，各自出固件：

| 套件 | `test` 层入口 | `.bin` |
|---|---|---|
| `ActsCMSISTest` | 7 | 252,072 |
| `ActsDfxFuncTest` | 1 | 163,384 |
| `ActsHieventLiteTest` | 1 | 163,840 |
| `ActsBootstrapTest` | 1 | 166,940 |
| `ActsParameterTest` | 2 | 198,016 |
| `ActsSamgrTest` | 15 | 207,940 |

产物在 `output/xts/<Suite>/OHOS_Image.{hex,bin}`。

### 5.1 怎么换套件（已做成 gn 参数，不用改文件）

`device/board/Nsing/BUILD.gn` 顶部：
```gn
declare_args() {
  xts_suite = "ActsCMSISTest"        # 换套件只改这里，或命令行覆盖
}
```
链接处用 `-lmodule_${xts_suite}`。出某个套件的固件：
```bash
hb build -b debug --gn-args build_xts=true --gn-args xts_suite=ActsSamgrTest
```

**可以把 6 个套件都列进 `all_features` 让库一次性都编出来** —— 因为
`-Wl,--whole-archive` 只作用于**链接时列出的那一个库**，编 6 个只花编译时间，
**镜像里的 Flash/RAM 仍只含一个套件**。之后每次换套件只重链接，几秒完成。


**切分原则**
- 以**测试套件**为最小单位（`ActsXxxTest` / `HatsXxxTest`），不是一个模块里所有用例
- 每个镜像只 `whole-archive` 一个套件静态库
- 版本号跟着模块走：`BUILD=v24-xts-<模块名>` → 烧录哪个模块，日志里就能对上
  （正好满足认证要求的"版本信息与日志一致"）

**产物组织建议**
```
output/xts/
  ActsCMSISTest/   OHOS_Image.hex + OHOS_Image.bin + 串口日志.txt + 说明.md
  ActsDfxFuncTest/ ...
  HatsHdfLightTest/ ...
```
—— 这个目录结构直接就是**要寄给工作组的材料**雏形。

**推荐起手模块**（依赖最少 → 最容易先通）
1. `kernel_lite/kernelcmsis_hal` → `ActsCMSISTest`（纯 CMSIS-RTOS2，零外部组件）
2. `hiviewdfx_lite/hilog_hal` → `ActsDfxFuncTest`（我们有 hilog）
3. `hiviewdfx_lite/hievent_hal` → `ActsHieventLiteTest`（我们有 hievent）
4. `startup_lite/bootstrap_hal` → `ActsBootstrapTest`（我们有 bootstrap_lite）
5. `startup_lite/syspara_hal` → `ActsParameterTest`（补完 `hal_sys_param` 后可跑）
6. `distributed_schedule_lite/system_ability_manager_hal` → `ActsSamgrTest`（我们有 samgr_lite）
7. `hats/hdf/light` → 需要先解决 G6

**要注意的**：`commonlibrary_lite/file_hal`（需文件系统）、`distributeddatamgr_lite/kv_store_hal`（需 kv_store）、
`security_lite/huks`（需 huks）、`communication_lite/lwip_hal`（需 lwip）、
`iothardware_lite/peripheral_hal`（需 WiFi 部件）—— **这些要先补组件或确认能否豁免。**

---

## 6. 认证材料清单

| 材料 | 来源 | 我们现在的状态 |
|---|---|---|
| 企业账号 | 开放原子开源基金会网站 | ✗ 待申请 |
| 测试报告（每模块一份串口日志 txt） | 自测产出 | ✗ 待跑 |
| PCS 自检表 | 官网 PCS 专区下载填写 | ✗ 待填 |
| 镜像（每模块一个，可烧录） | 我们出 | ✗ 待出 |
| 烧录工具 + 步骤说明 | 我们写 | ✗ 待写（我们有 USB-TTL + hex 烧录流程） |
| 产品样品 | 硬件 | 需把烧录口引出 |
| 版本信息一致性 | 代码打印 vs 申报 | ⚠️ 需要规范化（G9） |
| 《兼容性协议》《隐私声明》 | 平台签署 | ✗ 待签 |

---

## 7. 分阶段路线图

| 阶段 | 目标 | 关键动作 | 验收 |
|---|---|---|---|
| **P0 准备** | 本地可分析可编译 | ✅ 已完成（`ohos_sync.sh`） | 本地改代码 → push → build → fetch 闭环 |
| **P1 链路打通** | **一个套件在板上跑出 PASS** | ✅ **已完成**（2026-10-13 实机验证）：修了 `printf` 宽度（G1）、whole-archive（G4）；**G5 不存在**；**G2 不需要**（框架有 weak 默认实现）；**G3 是被证伪的多余改动，已移除**（曾导致每套件跑两遍）。`ActsBootstrapTest` **7/7 全过** | 串口出现 `[xts] suite=...` → `Start to run test suite:` → `N Tests N Failures` ✓ |
| **P2 acts 扩展** | 把 mini 相关 acts 套件逐个跑通 | ✅ **编译层已完成**（2026-10-08）：6 个核心套件**全部编译+链接成功**，各出一个固件 → `output/xts/<Suite>/`。链哪个套件已做成 gn 参数 `xts_suite`，换套件不用改文件。**待实机验证** | 每个模块一份日志，`Failures == 0`（或记录豁免项）|
| **P3 hats 接线** | `hats` 在 mini 上能编能跑 | 按 §4.1 方案 2 建 `hats/build_lite`，先挑 `hdf/light` | `HatsHdfLightTest` 出日志 |
| **P4 产品化** | 补齐认证材料 | 补 `hal_sys_param` 全部字段；规范版本 Hash/ID；分批出镜像 + 写烧录说明；整理 `output/xts/` | 材料清单 §6 全绿 |
| **P5 申报** | 提交并过审 | 申请账号 → 自测 → 填 PCS → 上传 → 寄样 → 应对复测 | 拿到测评通过 |

**阶段之间的依赖**：P1 是所有后续的前提；**P3（hats）与 P2（acts 扩展）可以并行**，因为两者构建路径不同。

---

## 8. 风险与待确认

| # | 风险 / 未知 | 影响 | 应对 |
|---|---|---|---|
| R1 | **官网套件包与源码树结构可能有差异** | P5 阶段要重做接线 | 先做 P1~P3 把机制跑通；机制不依赖具体套件内容 |
| R2 | **认证对"无硬件外设"模块是否允许豁免** | 可能要写大量假驱动 | **尽早**通过 PCS 专区/工作组确认；这决定 P3 的范围 |
| R3 | `hats` 在 mini 上无现成路径，需自建 | P3 工作量 | 先只挑 `hdf/light`（和我们的 LED 对口）验证可行性 |
| R4 | 内存紧，分批数量可能很多 | 出固件/烧录/整理日志的工作量大 | 按套件切分；把"出固件+抓日志"脚本化 |
| R5 | 版本 Hash/ID 规范不清楚 | P4 返工 | 提前查兼容性平台的"产品定义"填写要求 |
| R6 | `printf` 只补了 `%s` 宽度，若某套件用 `%f`/`%lld` 会错乱 | 个别套件日志异常 | 先跑起来，遇到再补 |
| R7 | 认证要求的是**官网包的版本**，可能要求与内核/组件版本严格对应 | 版本不匹配要重来 | 记录我们用的 OH 版本（`OpenHarmony-v6.1-LTS`）与套件版本对应关系 |

---

## 附录 A：套件适配性总表（本地读 `bundle.json` 得到）

| 组件 | `adapted_system_type` | 说明 |
|---|---|---|
| `acts` | mini / small / standard | 应用兼容性测试套件 —— **主战场** |
| `hats` | mini / small / standard | HAL 兼容性测试套件 —— **但构建只支持 standard/small** |
| `device_attest_lite` | mini / small | 设备证明（需 samgr/ipc/hilog_lite/ace_engine_lite/init/syscap_codec） |
| `tools` | mini / small / standard | 测试框架（hctest / hcpptest） |
| `dcts` | **standard** 仅 | 分布式兼容性 —— **mini 不适用，排除** |

## 附录 B：关键路径速查（全部在本地 `D:\OHOS_MINI` 下）

| 用途 | 路径 |
|---|---|
| acts 的 mini 套件清单 | `test/xts/acts/build_lite/BUILD.gn`（`ohos_kernel_type == "liteos_m"` 分支） |
| CMSIS 套件 | `test/xts/acts/kernel_lite/kernelcmsis_hal/BUILD.gn`（target `ActsCMSISTest`） |
| hctest 框架与注册宏 | `test/xts/tools/lite/hctest/include/hctest.h` |
| 套件编译模板 | `test/xts/tools/lite/build/suite_lite.gni` |
| `test` 层链接段 | `device/soc/Nsing/n32h7xx/liteos_m/board.ld`（`__zinitcall_test_start/end`） |
| `OHOS_SystemInit`（只走 6 层） | `base/startup/bootstrap_lite/services/source/system_init.c` |
| 产品信息接口 | `base/startup/init/interfaces/hals/hal_sys_param.h` |
| 我们的产品参数实现 | `vendor/Nsing/n32h7xx_eval/hals/utils/sys_param/hal_sys_param.c` |
| 我们的 printf | `device/board/Nsing/n32h7xx_eval/liteos_m/board/src/board_print.c` |
| hats 构建（无 mini 分支） | `test/xts/hats/test_packages.gni`、`test/xts/hats/BUILD.gn` |

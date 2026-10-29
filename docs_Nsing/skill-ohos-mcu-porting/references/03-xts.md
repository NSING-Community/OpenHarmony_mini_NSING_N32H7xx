# 03 XTS 测试

## 体系

| 项 | 内容 |
|---|---|
| 组成 | `acts`（用例）+ `tools`（框架）；后续扩展 `dcts` |
| 轻量系统框架 | **hctest**（基于 unity 增强），用例用 C |
| 用例三要素 | `LITE_TEST_SUIT(子系统,模块,套件名)`、`LITE_TEST_CASE(套件,用例,属性)`、`RUN_TEST_SUITE(套件)` |
| 属性 | 类型（Function/Performance/…）\| 粒度（Small/Medium/Large）\| 级别（Level0–Level4） |
| 目录 | `test/xts/acts/subsystem_lite/<模块>/`；编译配置在 `acts/build_lite/BUILD.gn` |
| 链接 | acts 编成静态库，随 **debug** 版本链进镜像；不是独立工程 |
| 日志 | 套件开始 `Start to run test suite`，结束 `xx Tests xx Failures xx Ignored` |

## 启用

1. 产品 `config.json` 加 `xts/acts` 组件（`features` 保持空）。
2. 每个套件单独出一个固件：`hb build -b debug --gn-args build_xts=true --gn-args xts_suite=<套件> --gn-args app_demo=false`
3. 产物命名带套件名，归档到按套件分的目录。

**一个镜像一个套件**：`xts_suite` 是字符串参数，每个套件编成独立静态库由 whole-archive 拉入。

## 小型化参数：五项都不可直接启用（实测结论）

官方《轻量系统小型化适配指导》给出五项建议值，我们逐项验证的结果：

| Feature | 建议值 | 实测结果 |
|---|---|---|
| `hctest_rodata_opt` | true | **链接失败**：切到新 runner，需要链接脚本为每个模块提供 `xts_init_<套件>_start/_stop` |
| `xts_overlay` | true | **链接失败**：缺 `xts_overlay_start/_stop` 符号 |
| `hctest_task_stack_size` | 2048 | 与后两项共同导致异步用例失败 |
| `hctest_task_queue_size` | 1 | **主因**：`SharedSend failed(-6)`，TaskPool/Broadcast 类用例失败 |
| `hctest_task_type` | SHARED_TASK | 消息串行化，时序断言失败 |

**结论**：默认**五项全不启用**。若要尝试，必须逐项验证"链接 + 运行"两步，
且准备一份可回退的默认配置。开启前先读 `04-pitfalls.md` #18、#19。

## 结果判读：按顺序看四行

```
[xts] suite=<套件名>, N suite entry(s) registered
Start to run test suite:<套件名>
Run test suite 1 times                 ← 必须是 1，出现 2 说明被执行两遍
xx Tests xx Failures xx Ignored
All the test suites finished!
```

判读顺序：**套件名对不对 → 注册条目数是否合理 → 每个套件是否只跑 1 次 → 汇总行 failures 是否 0**。

## 失败归因

| 现象 | 先查 |
|---|---|
| 一个用例都没跑 | 组件是否启用；是否 debug 构建；`xts_suite` 是否对 |
| 套件跑两遍 | 是否同时存在"主入口遍历"与"框架自身执行"两处 |
| 跑到一半复位 | 堆/栈不足（`nm` 看堆；临时调大栈） |
| 大量用例失败且集中在异步类 | 任务队列长度、任务模型（见上表） |
| 偶发失败 | 日志缓存溢出（`Hilog writeToCache error`）说明日志速率过高，先降日志量再判 |

## 与 demo 的关系

XTS 固件**不应该**带 demo 任务：业务任务会抢 CPU、周期打乱断言时序、日志互相污染。
用 `app_demo=false` 构建，用 `MODULE_INIT(run) : 0 entry` 作为"确实没带业务任务"的证据。

平台驱动（GPIO/UART/I2C/PWM）保留：它们只做初始化、不建任务，是兼容性的一部分。

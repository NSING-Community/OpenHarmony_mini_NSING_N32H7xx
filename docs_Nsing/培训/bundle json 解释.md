`bundle.json` 是 OpenHarmony 中**组件的核心元数据声明文件**，每个组件都必须在根目录下包含这一个文件。它相当于组件的“身份证”和“说明书”，定义了该组件如何被识别、构建、集成和分发。

### 📦 核心定位：HPM 包与组件的描述文件
在 OpenHarmony 的生态中，`bundle.json` 首先被看作是 **HPM（HarmonyOS Package Manager）的包描述文件**。HPM 中的 **Bundle（包）** 是基本的分发单元，而 `bundle.json` 就是对这个分发包的元数据进行声明的文件，包含了包的名称、版本、依赖等核心信息。因此，当你看到一个包含 `bundle.json` 的目录，它通常就是一个可以独立开发、构建和发布的组件包。

### 🛠️ 主要作用与关键字段
`bundle.json` 的内容直接决定了组件在整个生命周期（开发、构建、部署）中的行为。其主要作用通过以下关键字段体现：

*   **标识与描述**：
    *   `name`：组件的唯一标识，格式通常为 `@组织名/组件名`（如 `@ohos/demos`）。
    *   `version`：组件的版本号，通常与 OpenHarmony 的大版本号保持一致。
    *   `description`：对组件功能的一句话简要描述。

*   **构建与编译**：
    *   `build`：这是非常关键的配置块，其中 `sub_component` 或 `moudles` 字段指定了组件的**编译入口**，告诉构建系统应该编译哪些目标（如动态库、可执行文件等）。
    *   `scripts`：定义在组件目录下可以执行的命令，例如 `build`、`test` 等。

*   **依赖与能力**：
    *   `dependencies` / `deps`：声明该组件运行或编译时所**依赖的其他组件或三方库**。
    *   `syscap`：声明该组件为应用提供的**系统能力**（System Capability），这是应用调用组件功能的基础。
    *   `features`：列出组件可配置的**特性列表**，供产品在集成时进行选择性配置。

*   **资源与兼容性**：
    *   `ram` / `rom`：预估该组件运行所需的 **RAM 和 ROM 资源**，用于系统资源规划。
    *   `adapted_system_type`：指明组件适用的**系统类型**，如轻量系统（mini）、小型系统（small）或标准系统（standard）。

*   **分发信息**：
    *   `publishAs`：定义组件的发布类型，通常是 `code-segment`（代码片段）或 `source`（源码）。
    *   `segment` / `dirs`：描述发布时组件的目录结构，特别是对外暴露的头文件路径必须在此声明。

### 💡 与其他配置文件的区别
需要注意将 `bundle.json` 与应用开发中的配置文件区分开：

*   **`bundle.json`**：用于 **OpenHarmony 系统组件/部件**的开发，是 HPM 包的元数据描述。
*   **`config.json`** (FA 模型) 或 **`module.json5`/`app.json`** (Stage 模型)：用于**应用开发**，描述应用的包名、权限、Ability 等结构信息。

`bundle.json` 是 OpenHarmony 组件化设计的基石，它让每个组件都成为一个自描述、可独立管理、依赖清晰的构建与分发单元。
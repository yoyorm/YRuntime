# YRuntime 文档

> **要动手了就直接看 [`START-HERE.md`](START-HERE.md)。** 本页只是索引，不是阅读顺序。

## 一句话定位

用 C++ 从零实现一个 **headless-first 的游戏 Runtime**：先把运行时核心（对象模型、场景树、资源、序列化、事件、任务调度、主循环）搭起来并跑通一个文字游戏，再把已有的 Vulkan 渲染器作为 **Renderer Backend** 接入，形成 `游戏状态 → Runtime → Renderer → GPU` 的完整链路。

## 索引

| 文档 | 内容 | 性质 |
|---|---|---|
| ⭐ [`START-HERE.md`](START-HERE.md) | 现在在哪、下一步做什么、决策协议、分工 | **活文档**，每次开工看 |
| [`../HANDOFF.md`](../HANDOFF.md) | 面向后续 AI 会话的项目概况、硬性约束、当前上下文与下一步 | **交接文档**，新 AI 先读 |
| [`02-roadmap.md`](02-roadmap.md) | M0~M11 任务清单 + 验收标准 + 常见坑 | **活文档**，勾选进度 |
| [`06-decisions.md`](06-decisions.md) | ADR：架构级取舍及其代价 | **活文档**，只增不改 |
| [`00-vision.md`](00-vision.md) | 定位、目标 / 非目标、成功判据、风险 | 参考，怀疑方向时看 |
| [`01-architecture.md`](01-architecture.md) | 分层、模块职责、核心概念、帧循环、线程模型 | 参考，写模块前看对应小节 |
| [`03-learning-map.md`](03-learning-map.md) | 知识点 → 里程碑映射、书单、16 个实验 | 参考，卡住时看 |
| [`04-godot-study.md`](04-godot-study.md) | Godot 4.8 源码索引（本机路径已验证）+ 对照问题 | 参考，对照阅读时看 |
| [`05-engineering.md`](05-engineering.md) | 构建 / 测试 / 日志 / 调试 / 剖析 / Git / CI / 代码规范 | 参考，查规范时看 |
| [`07-portfolio.md`](07-portfolio.md) | 理解自检、展示物清单、面试题库 | 参考，里程碑收口时看 |
| `media/` | 截图、GIF、录屏 | — |

## 维护规则

1. **文档与代码不一致时，先改文档再改代码。** 文档是设计意图的唯一来源。
2. **不新建过程性文档。** 新内容优先并进已有文档；根目录 `HANDOFF.md` 是面向 AI 的交接例外，只维护当前快照，不记流水账。
3. **进度只记两处**：根 `README.md` 的里程碑表 + `02-roadmap.md` 的 checkbox。其他地方不重复。
4. **架构图用 Mermaid**（GitHub 原生渲染、可 diff），不用画图软件截图。
5. **不把 AI 生成的长文本直接粘进来。** 每段结论你都要能脱稿复述，否则删掉重写。
6. **根 `HANDOFF.md` 只写当前状态和可执行约束。** 每次里程碑推进后更新，不追加逐日流水账。

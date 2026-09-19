# YRuntime 文档中心

> 本目录是 YRuntime 的**规划与知识沉淀中心**。代码会随重构变化，但这里记录的
> "为什么这么设计" 和 "我学到了什么" 才是这个项目真正的产出。
>
> 创建于 2026-09-19。当前状态：**规划完成，M0 未开始**。

## 一句话定位

用 C++ 从零实现一个 **headless-first 的游戏 Runtime**：先把运行时核心（对象模型、场景树、资源、序列化、事件、任务调度、主循环）搭起来并跑通一个文字游戏，再把已有的 Vulkan 渲染器作为 **Renderer Backend** 接入，最终形成 `游戏状态 → Runtime → Renderer → GPU` 的完整链路。详见 `00-vision.md`。

## 文档地图

> **如果你现在正准备动手，直接跳到 [`START-HERE.md`](START-HERE.md)。** 下表是全部文档的索引，不是阅读顺序。

| 文档 | 内容 | 什么时候读 |
|---|---|---|
| ⭐ [`START-HERE.md`](START-HERE.md) | **不知道接下来干什么时只看这篇**：心理负担、决策协议、第一周逐日计划、分工边界 | 每次迷茫时；每周开工前 |
| [`00-vision.md`](00-vision.md) | 定位、目标 / 非目标、成功判据、能力矩阵、风险 | 现在；以及每次怀疑"我为什么在做这个"时 |
| [`01-architecture.md`](01-architecture.md) | 分层架构、模块职责、依赖规则、核心概念、线程模型、目录布局 | 开始 M0 之前；之后每个里程碑开工前重读 |
| [`02-roadmap.md`](02-roadmap.md) | M0~M11 里程碑：任务清单 + 验收标准 + 常见坑 | 每周规划时 |
| [`03-learning-map.md`](03-learning-map.md) | C++ / 引擎知识点 → 里程碑映射 + 阅读材料 + 实验清单 | 卡住时、需要补知识时 |
| [`04-godot-study.md`](04-godot-study.md) | Godot 4.8 源码研究索引（本机真实路径）+ 对照问题 | 每个里程碑的"对照阅读"环节 |
| [`05-engineering.md`](05-engineering.md) | 构建、测试、格式化、日志、调试、性能分析、Git、CI、代码规范 | M0 落地；之后当 checklist 用 |
| [`06-decisions.md`](06-decisions.md) | ADR 架构决策记录（含被推翻的历史） | 做重大取舍前，先查有没有已记录的决策 |
| [`07-portfolio.md`](07-portfolio.md) | 作品展示策略：证据链、README、架构图、博客、面试话术 | 每个里程碑结束时更新 |
| `notes/` | 设计笔记、周记（模板已备好） | 持续 |

## 仓库当前状态（2026-09-19）

```
YRuntime/
├── README.md            ✅ 骨架（里程碑进度表 + 文档索引）
├── .clang-format        ✅ 已建（BasedOnStyle: LLVM，待按 05-engineering.md §12 显式补几项）
├── .vscode/settings.json ✅ 已建（clang-format on save）
├── src/                 ⚠️ 已建但为空 —— 见下方说明
└── doc/                 ✅ 本次规划的全部文档
```

> **关于 `src/`**：`01-architecture.md` §3 规划的顶层源码目录是 **`engine/`**（按层分 `engine/core/`、`engine/scene/`…），
> 另有 `apps/`（可执行 Demo）、`tools/`（工具）、`tests/`（测试）、`benchmarks/`。
> 这样安排的原因是：顶层目录名本身就表达了"引擎库 / 应用 / 工具 / 测试"的四分，
> 而单一 `src/` 会让 11 个 CMake target 的边界变得不明显（Yo_Renderer 的单 `src/` + `file(GLOB_RECURSE)` 就是前车之鉴，见 ADR D11）。
> **M0 开工时决定**：① 采用 `engine/` 并删掉空的 `src/`；或 ② 保留 `src/` 但在其下按层分子目录（`src/core/`…）。
> 两种都可以，选定后写进 ADR 并更新 `01-architecture.md` §3。

## 使用方式（比计划本身更重要）

1. **每周先开周记**：复制 `notes/_weekly-log-template.md` → `notes/weekly/2026-Wxx.md`（周号用 `date +%V` 查），
   写下本周要推进的里程碑条目（**不超过 3 条**）。周末填实际结果与偏差原因。
2. **每个模块先写设计笔记再写代码**：复制 `notes/_design-note-template.md` → `notes/design/<模块>.md`，
   先回答"为什么存在 / 输入输出 / 所有权 / 失败模式 / Godot 怎么做"，再动手。
3. **完成即打勾 + 写日期**：`02-roadmap.md` 的 checkbox 完成后改成 `- [x] ... ✅ 2026-10-05`。
   推翻旧决策时在 `06-decisions.md` **追加新 ADR**，不要就地改写旧 ADR。

## 文档维护规则

- 文档与代码不一致时：**先改文档，再改代码**。文档是设计意图的唯一来源。
- `01-architecture.md` 的架构图在每次分层变化后必须更新（用 Mermaid，GitHub 原生渲染）。
- **禁止把 AI 生成的长文本直接粘进 `doc/`**。每篇文档、每段结论你都要能脱稿口头复述，
  否则删掉重写。这是这个项目作为"个人作品"的底线（详见 `07-portfolio.md`）。
- 每完成一个里程碑，回到 `00-vision.md` 的"成功判据"清单打勾，并更新 `07-portfolio.md`。

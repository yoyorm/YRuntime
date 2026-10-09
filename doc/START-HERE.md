# START HERE · 现在做什么

> **不知道接下来干什么时只看这篇。** 它只回答三件事：现在在哪、下一步做什么、碰到决策怎么办。
> 参考性内容在 `doc/` 的其他文档里，见 §5。

---

## 1. 当前状态

| 里程碑 | 状态 |
|---|---|
| **M0 工程地基** | ✅ 已完成（`v0.M0`：CMake / CI / Catch2 / assert / log） |
| **M1 句柄、容器、时间、字符串** | ✅ 核心完成 —— Handle/SlotMap/ScopeTimer/StringId/ObjectID 完成；E1 perf 结论未做（非阻塞） |
| **M2 反射与对象模型** | ✅ 核心完成 —— Object/ObjectDB/Variant（10 tag）/PropertyInfo/ClassInfo/ClassDB 完成；`yr_inspect`、注册宏延后 |
| **M3 单线程 EventBus** | ✅ 已完成 / 收口（2026-10-08） |
| **M4 场景树与主循环** | 🔨 **下一主线** |
| M5 ~ M11 | ⬜ 未开始 |

进度**只在两处维护**：根 `README.md` 的里程碑表（对外）+ `02-roadmap.md` 的 checkbox（对内）。
别的地方不要重复记录，否则一定会漂移。

---

## 2. 下一步：M4 场景树与主循环

### M3 已完成：单线程 EventBus（2026-10-08）

- `Subscription` RAII：析构 / `reset()` 自动退订，move-only
- `EventBus` 同步 `subscribe` / `publish` / `unsubscribe`：订阅按注册顺序、类型分表隔离
- `post` / `flush` 双缓冲：flush 中 `post` 进下一轮、支持 move-only 事件、递归 flush 防护
- 重入语义：回调退订自己 / 他人安全、inactive 延迟到最外层清理、统一同步深度上限 8（第 9 层拒绝）
- `globalEventBus`，以及确定性重放 + 1000 事件正确性测试

### M3 明确推迟（不要当成已完成）

- **`MessageQueue`**：`ObjectID + StringId method + vector<Variant>` 的延迟调用。它依赖尚未实现的**方法绑定**
  与未来 `MainLoop` 的**安全 flush 点**（帧阶段 2），**不是纯多线程功能**；两样具备后再做
- **`post_from_any_thread` / MPSC / TSan 验证**：推迟到 **M7**
- 事件追踪日志、`std::function` 性能实验：非阻塞优化，不阻塞主线

### M4 第一个可独立任务（一次只引入一个概念）

1. **先研究并定义最小 `Node` 父子关系 / lifecycle 语义，不写代码骨架**：单亲指针 + 子列表的所有权规则、
   `add_child` / `remove_child` 的前置条件、`kEnterTree` / `kReady`（后序）/ `kExitTree` 的触发顺序，
   以及 `queue_free` 为什么必须延迟到安全点。
2. **动笔前读**：`01-architecture.md` §4.6 `Node` / `SceneTree`；`04-godot-study.md` 的
   "Node 生命周期" 行（`scene/main/node.h` + `node.cpp` 的 `_propagate_ready` / `_propagate_enter_tree` /
   `_propagate_exit_tree`）。
3. 产物先在对话里定成一页设计结论（仓库不产出笔记）；确认后再落 `yr/scene/node.h` 最小头文件。

之后再按 `02-roadmap.md` M4 清单逐项推进（`NodePath`、`SceneTree`、`MainLoop` 阶段……），**不要一次实现整章**。

benchmark 数据和结论统一写到 `benchmarks/README.md`；不要新建额外笔记文件。

**非阻塞待办**（不急，别打断主线）：
- log 的 fmt 格式化、时间戳 / 帧号 / 线程 ID、SBO 优化 → 见 `02-roadmap.md`
- ccache、`.editorconfig`

---

## 3. 决策协议（碰到再决定，不用凭经验）

按顺序套用，命中就停：

| # | 规则 |
|---|---|
| **R1** | 文档里已有倾向的（`01-architecture.md` §11 的 Q1~Q6、`06-decisions.md` 的 ADR）**直接照做，不重新讨论** |
| **R2** | 两个方案都行 → **选代码更少、概念更少的那个** |
| **R3** | 不确定要不要做 → 现在不做，写 `// TODO(M?)`，等它真的痛了再做 |
| **R4** | 只有"影响 ≥1 天工作量"或"改起来很贵"的选择，才值得停下来讨论 |
| **R5** | 理论极值 / 可预测的边界（例：generation 回绕）→ 理解并接受即可，不做注入机制；但**边界算术本身**（掩码、跳过 0）可用纯 helper 静态测试覆盖，无需真实触发 2^40 次 |
| **R6** | 纯粹的工程完备性（覆盖率门禁、tidy 全绿、make it "工业级"）→ 与学习价值无关就不做。**这是学习项目，理解 > 完成 > 展示** |

**讨论完怎么落地**：绝大多数问题**在对话里解决掉就完了，不产生新文档**。
只有满足下面任一条才写进 `06-decisions.md`：

1. 改变了**模块边界或依赖方向**
2. **改起来很贵**（例：D16「sanitizer 要不要并进 debug」、D9「序列化格式自研还是用 JSON」）

反例：「这个函数叫什么」「用 vector 还是 deque」——两条都不满足，讨论完就忘，没问题。

### 任务粒度规则

**一个步骤只引入一个新概念。** 自检：这一步做完，如果新接触的东西超过一件，说明步骤太大，拆。

**拆分顺序**：先用最土的办法体验一遍（手写、硬编码、命令行传参），理解了再抽象成配置/函数/宏。
Day 3b→3c 就是这个模式：先硬编码 flag 亲眼看 ASan 抓 bug，再做成 `option()` 开关。
这样你才知道那个抽象在替你做什么，而不是在"调配置直到它工作"。

**允许一次只做一半**，但每一步都要**独立可提交**（做完就能 commit + push，仓库始终是绿的）。

---

## 4. 分工

| AI 可以做 | AI 不做 |
|---|---|
| CMake / CI / Python 脚本等**构建样板** | `engine/` 下任何模块的**实现代码** |
| 解释概念、讲清坑的原理、Godot 源码导读 | 替你做设计决策（只给选项 + 代价） |
| review 你的头文件与实现、指出 bug 的**类型** | 替你写博客 |
| 给测试用例清单（清单是规格，实现你写） | 在没必要时跑后台验证（浪费你的 token） |

**卡住时的顺序**（不要第一步就问）：
① 把问题写成一句话 → ② 20 行最小复现 → ③ `clang -E` / gdb / 加日志缩小范围 →
④ 读 Godot 对应文件（`04-godot-study.md`）→ ⑤ 搜报错原文 → ⑥ 这时再问，并要求"只讲原理和方向"。

---

## 5. 其他文档什么时候看

| 文档 | 什么时候打开 |
|---|---|
| `02-roadmap.md` 的**当前里程碑那一节** | 每次开工前 |
| `01-architecture.md` 的对应 §4.x 小节 | 写某个模块的头文件之前 |
| `04-godot-study.md` 的对应主题行 | 做某个模块时的对照阅读 |
| `05-engineering.md` | 要查规范（命名 / 测试 / 提交 / CI）时 |
| `00-vision.md` | 怀疑方向、或想砍功能时 |
| `03-learning-map.md` | 卡住、需要补知识时 |
| `06-decisions.md` | 要做取舍前，先查有没有已经定过的 |
| `07-portfolio.md` | M8 之后再修订，现在不用看 |
| `HANDOFF.md`（根目录） | 交给新的 AI 会话时先读：项目概况、约束、当前上下文、下一步 |

> **一次开工实际需要的阅读量 ≈ 30 行。** 如果你发现自己在一次读 500 行文档，停下来 ——
> 那是在用"准备"代替"动手"。

---

## 6. 记录方式

**这个项目不在仓库里写笔记。** 记录只有三种载体：

| 载体 | 记什么 |
|---|---|
| **commit message** | 这次改动的**理由**（不是做了什么，diff 已经说明了） |
| **`06-decisions.md` 的 ADR** | 只记架构级取舍（见 §3 的两个条件） |
| **`02-roadmap.md` 的 checkbox + 日期** | 进度 |

章节性的总结你自己另外整理，不放进仓库。

代价是：**三个月后回来看，很多"为什么"只存在于 commit message 里。**
所以 commit message 要认真写 —— 它是这个项目唯一的高频记录。**写它是为了三个月后的你自己**能看懂当时为什么这么定，不是为了给别人看 `07-portfolio.md` §2。

写法：
```
feat(core): 用 index+generation 句柄而非裸指针

裸指针无法检测悬垂，shared_ptr 的控制块分离且会掩盖生命周期错误。
generation 让 erase 后旧句柄自动失效，这是 ABA 的最低成本解法。
详见 01-architecture.md §4.1。
```
第一行说"做了什么"，**正文说"为什么"**。

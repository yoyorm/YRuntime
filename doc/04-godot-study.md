# 04 · Godot 源码研究计划

> 生成于 2026-09-19。本机源码：`/home/yoyorm/Code/GodotDev/godot`，版本 **4.8.dev**（`git log` HEAD `dfa06cafb4`）。
> **本文所有路径都已在该 checkout 中验证存在**（验证日期 2026-09-19）。Godot 主干变动很快，
> 引用时请附上 commit hash 或直接用本机这份，不要引用"网上看到的行号"。
>
> 定位：**Godot 是参照系，不是抄写对象。** 每次阅读都必须以"我正在解决的具体问题"为入口，
> 读不出结论就换文件，不要通读。

---

## 1. 阅读方法论

### 1.1 单次阅读流程（≤60 分钟，时间盒）
1. **带着问题进去**：从下表"要回答的问题"里挑 1~2 条，写在笔记开头。
2. **只读头文件**：先看 `.h` 的类声明、注释、成员变量顺序（成员顺序往往暗示了设计意图）。
3. **画一张图**：调用时序 / 数据结构 / 所有权关系，手画在笔记里（Mermaid 或 ASCII）。
4. **只追一条路径进 `.cpp`**：找到那 1~2 个关键函数的实现，读懂就停。
5. **写三段式结论**（这是笔记的强制格式）：
   - **Godot 怎么做**：客观描述，附 `文件:行号`。
   - **我怎么做 / 差异**：我的方案，以及具体差在哪。
   - **为什么不同**：三选一 —— ①我能力/时间不够 ②项目规模不需要 ③我认为我的更好（**选③必须给出理由，并写下什么情况下我会改回 Godot 的做法**）。
6. **产出一条行动项**：这次阅读要不要改我的设计？要 → 立刻写进 roadmap 的对应 checkbox 或 `06-decisions.md` 的 ADR；不要 → 写"无变更，理由：…"。

### 1.2 有用的检索命令
```bash
cd /home/yoyorm/Code/GodotDev/godot
# 找一个类在哪定义
grep -rn "class SceneTree " --include=*.h scene/ core/
# 找谁调用了某函数（理解调用链）
grep -rn "queue_free()" --include=*.cpp scene/ | head -20
# 看某文件的 include 关系（理解分层）
sed -n '1,40p' scene/main/node.h
# 看官方对该类的文档说明（比读代码快）
cat doc/classes/Node.xml | head -60
# 看测试怎么写（理解语义边界最快的方式）
ls tests/core/object/ && sed -n '1,60p' tests/core/object/test_object.cpp
# 看某功能的演进历史与讨论（理解"为什么是这样"）
git log --oneline -- scene/main/scene_tree.cpp | head -20
git log -p -S "flush_transform_notifications" -- scene/main/scene_tree.cpp | head -80
```
> **`doc/classes/*.xml` 是被低估的入口**：它是官方 API 文档的源文件，比读代码快 10 倍，
> 而且里面的 `<description>` 常直接写明设计意图与注意事项。
> **`tests/` 也是被低估的入口**：Godot 用 doctest，`tests/core/object/test_object.cpp`、
> `tests/core/io/test_resource.cpp` 直接告诉你"语义边界在哪"，比自己猜准。

### 1.3 要不要自己编译 Godot？
- **M0~M6：不需要。** 只读源码 + `doc/classes/*.xml` 足够，编译一次要 30~60 分钟且占几十 GB。
- **M7 之后建议编译一次 Debug 版**（`scons platform=linuxbsd target=editor dev_build=yes -j$(nproc)`），
  用途是**用 gdb 单步跟一帧**：在 `Main::iteration()` 下断点，看真实的调用栈——这是理解主循环最有效的方式，
  比读 100 页文档都强。同时在 `SceneTree::process`、`MessageQueue::flush`、`ResourceLoader::load` 下断点观察。
- 编译产物别提交进 YRuntime 仓库（不同仓库，且 `.gitignore` 已覆盖 Godot 自己的 `bin/`）。

---

## 2. 主题索引（按里程碑组织）

图例：★★★ = 必读精读，★★ = 应读，★ = 选读。

### M1 · 句柄、容器、字符串
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| RID 与 slot 分配 | `core/templates/rid.h`（`class RID` 第 38 行）、`core/templates/rid_owner.h` | ★★★ | RID 如何编码 index+generation？`RID_Owner` 的空闲链怎么组织？为什么 RID 不带类型？ | `notes/godot/rid.md` |
| ObjectID | `core/object/object_id.h` | ★★ | ObjectID 的位布局？与 RID 为什么是两套？ | 同上 |
| StringName 驻留 | `core/string/string_name.h` + `.cpp` | ★★★ | 驻留表的哈希策略？为什么有 512 个 slot？StringName 的比较为什么是 O(1)？引用计数怎么管？ | `notes/godot/string_name.md` |
| 容器家族选型 | `core/templates/`：`vector.h`、`local_vector.h`、`hash_map.h`、`rb_map.h`、`list.h`、`self_list.h`、`lru.h`、`ring_buffer.h`、`fixed_vector.h`、`safe_list.h` | ★★ | Godot 为什么要自己写这么多容器而不用 STL？`Vector` 的 COW（`cowdata.h`）代价是什么？`LocalVector` 与 `Vector` 差别？ | `notes/godot/containers.md` |
| 内存分配 | `core/os/memory.h` + `core/os/memory.cpp` | ★ | 分配钩子怎么做？为什么引擎要接管 new/delete？ | 同上 |

### M2 · 对象模型与反射
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| Object 与 GDCLASS 宏 | `core/object/object.h` + `object.cpp` | ★★★ | `GDCLASS` 展开了什么（用 `gcc -E` 类比推理）？`notification` 如何沿继承链传播？`ObjectDB` 注册在哪一步？ | `notes/godot/object-model.md` |
| ClassDB | `core/object/class_db.h` + `class_db.cpp` | ★★★ | `ClassInfo` 里存了什么？`bind_property` 如何把成员变量/ getter-setter 对变成属性？继承链上的属性怎么合并？`instantiate` 如何处理抽象类？ | `notes/godot/class-db.md` |
| 属性描述 | `core/object/property_info.h`、`core/object/property_info.cpp` | ★★ | hint/usage flags 的设计？为什么要区分 `PROPERTY_USAGE_STORAGE` 与 `EDITOR`？ | 同上 |
| 方法绑定与类型擦除 | `core/object/method_bind.h`、`method_bind_common.h`、`callable_mp.h` | ★★ | 如何把任意签名的成员函数擦除成统一接口？`MethodBindT` 的模板爆炸怎么控制？（这题很难，读不懂就记"读不懂"） | `notes/godot/method-bind.md` |
| 显式注册顺序 | `core/register_core_types.cpp/.h`、`core/core_bind.cpp` | ★★ | 为什么不用静态自动注册而用显式函数调用？解决了什么 SIOF 问题？ | 同上 |
| 引用计数 | `core/object/ref_counted.h`（`class Ref` 第 59 行）+ `.cpp` | ★★★ | 侵入式计数的 atomic 用法？`WeakRef` 如何实现？循环引用官方怎么处理？`ref_derefed`/初始化竞态？ | `notes/godot/refcount.md` |
| Variant | `core/variant/variant.h`、`variant_parser.h` | ★★ | `sizeof(Variant)` 是多少、怎么塞下所有类型的？Type 枚举分组？为什么不用 `std::variant`？**只看结构，不要读完** | `notes/godot/variant.md` |
| Godot 怎么测反射 | `tests/core/object/test_class_db.cpp`、`test_object.cpp`、`test_method_bind.cpp` | ★★ | 反射系统的测试用例覆盖了哪些边界？我能抄哪些用例设计？ | 同上 |

### M3 · 事件、信号、延迟调用
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| Signal 机制 | `core/object/object.h`（`connect`/`disconnect`/`emit_signal`/`Signal` 类）、`object.cpp` 中 signal 实现 | ★★★ | 连接如何存储？发射时的重入保护（`emitting` 标志 / 拷贝连接列表）？连接目标死亡后如何自动断开？ | `notes/godot/signals.md` |
| MessageQueue | `core/object/message_queue.h` + `.cpp` | ★★★ | 双缓冲/环形缓冲怎么组织？`flush` 中 push 的消息去哪？为什么需要 `push_callp` 的延迟语义？多线程 push 怎么保证安全？ | `notes/godot/message-queue.md` |
| Callable | `core/variant/callable.h`、`core/object/callable_mp.h` | ★★ | Callable 如何统一"成员函数/lambda/RID 方法"？与我的 `std::function` 方案差别？ | 同上 |
| Undo/Redo | `core/object/undo_redo.h` | ★ | 属性修改如何自动记录？（S8 编辑器 stretch 时再精读） | — |

### M4 · 场景树与主循环
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| **一帧的权威顺序** | `main/main.cpp` 里的 `Main::iteration()` | ★★★ | 一个 iteration 里各步骤的确切顺序是什么？physics accumulator 怎么写？`_process` 与 `_physics_process` 的调度差异？时间缩放与暂停在哪一层生效？ | `notes/godot/main-iteration.md` |
| MainLoop 抽象 | `main/main.h`、`main/main.cpp` 的 `MainLoop`/`Main::start()` | ★★ | `MainLoop` 与 `SceneTree` 的关系？为什么要有这个抽象层（headless/server 模式）？ | 同上 |
| Node 生命周期 | `scene/main/node.h` + `node.cpp` | ★★★ | `_enter_tree`/`_ready`/`_exit_tree` 的派发顺序与递归实现（`_propagate_ready` 等）？`ready` 只触发一次怎么保证？`process_mode`（暂停传播）如何设计？ | `notes/godot/node-lifecycle.md` |
| 延迟删除 | `node.cpp` 的 `queue_delete()` / `scene_tree.cpp` 的 `flush_delete_queue()` | ★★★ | 待删队列如何处理父子同删？为什么必须在遍历外 flush？ | 同上 |
| SceneTree | `scene/main/scene_tree.h` + `.cpp` | ★★★ | `process()`/`physics_process()` 的节点分组（process group）如何避免每帧全树遍历？`flush_transform_notifications` 解决什么？节点增删如何不打断遍历（双缓冲列表）？ | `notes/godot/scene-tree.md` |
| SceneTreeTimer | `scene/main/scene_tree.h`（`class SceneTreeTimer` 第 57 行）、`scene/main/timer.h`（`Timer` 节点） | ★★ | Timer 与 SceneTreeTimer 的分工？暂停/时间缩放/物理帧标志怎么组合？ | 同上 |
| NodePath | `core/string/node_path.h` | ★★ | 路径如何编码（names + subnames + 绝对标志）？缓存策略？ | 同上 |
| Transform 传播 | `scene/main/canvas_item.h`、`scene/3d/node_3d.h` + `.cpp` | ★★ | 世界变换的脏标记如何向上传/向下传？通知 `NOTIFICATION_TRANSFORM_CHANGED` 的作用？ | `notes/godot/transform-propagation.md` |
| 固定步长插值（进阶） | `scene/main/scene_tree_fti.h/.cpp`、`scene_tree_fti_tests.cpp` | ★ | 渲染插值怎么解决物理/渲染频率不一致的抖动？（M4 降级可跳过，S4 时精读） | — |

### M5 · 序列化与场景格式
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| **.tscn 文本格式** | `scene/resources/resource_format_text.cpp` + `.h` | ★★★ | `[gd_scene load_steps= format= uid=]` / `[ext_resource]` / `[sub_resource]` / `[node]` / `[connection]` 各段如何解析？节点引用（`NodePath(".")`、`NodeID`）怎么表达？格式版本号如何影响解析？ | `notes/godot/tscn-format.md` |
| PackedScene | `scene/resources/packed_scene.h` + `.cpp` | ★★★ | `NodeData`/`Connection`/`EditableData` 三张表的结构？`instantiate()` 如何重建树并解析引用？`INSTANTIATE_*` 标志（编辑态/继承态）解决什么？ | `notes/godot/packed-scene.md` |
| .res 二进制格式 | `core/io/resource_format_binary.cpp` | ★★ | 二进制头/版本/字符串表怎么布局？如何处理"新版本读老文件"？ | `notes/godot/binary-format.md` |
| Variant 文本解析 | `core/variant/variant_parser.h` + `.cpp` | ★★ | `Vector3(1,2,3)`、`NodePath("A/B")` 这类字面量如何 tokenize + parse？错误位置如何报告？ | 同上 |
| 资源保存 | `core/io/resource_saver.h`、`scene/resources/resource_format_text.cpp` 的 saver 部分 | ★★ | 保存时如何决定哪些属性写出去（`PROPERTY_USAGE_STORAGE` + 与默认值比较）？ | `notes/godot/serialization-strategy.md` |
| 场景相关测试 | `tests/core/io/test_scenes.cpp`、`test_resource.cpp` | ★★ | Godot 如何测存读档 round-trip？ | 同上 |
| 兼容层 | `core/object/object.compat.inc`、`core/variant/*.compat.inc`（grep `compat`） | ★ | 大型项目如何处理"旧存档兼容"？（对照我的 migrator 链设计） | 同上 |

### M6 · 资源系统
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| Resource 基类 | `core/io/resource.h` + `.cpp` | ★★★ | `Resource` 与 `RefCounted` 的关系？`resource_local_to_scene`/`duplicate` 的语义？为什么要有 UID？ | `notes/godot/resource.md` |
| **异步加载** | `core/io/resource_loader.h` + `.cpp` | ★★★ | `ThreadLoadTask` 的状态机有哪几态？`load_threaded_request`/`load_threaded_get_status`/`load_threaded_get` 三段式 API 为什么这样设计？**加载过程中哪些步骤在工作线程、哪些必须回主线程**？错误如何传播？ | `notes/godot/async-loading.md` |
| 格式加载器分派 | `core/io/resource_loader.cpp` 的 loader 注册、`core/io/image_loader.h` | ★★ | 按扩展名/魔术字分派的机制？多个 loader 竞争怎么排序？ | 同上 |
| ResourceUID | `core/io/resource_uid.h` + `.cpp`、`tests/core/io/test_resource_uid.cpp` | ★★ | UID ↔ path 映射怎么维护？文件移动为什么不断链？UID 冲突如何处理？ | `notes/godot/resource-uid.md` |
| 文件抽象 | `core/io/file_access.h`、`file_access_memory.h`、`file_access_compressed.h` | ★★ | 为什么所有 IO 都走抽象接口？内存实现如何让测试脱离磁盘？ | 同上 |
| 资源测试 | `tests/core/io/test_resource.cpp`、`test_file_access.cpp` | ★★ | 有哪些边界用例值得抄？ | 同上 |

### M7 · 并发
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| WorkerThreadPool | `core/object/worker_thread_pool.h` + `.cpp` | ★★★ | 线程数如何决定？task group 与单任务的区别？**主线程等待时是否帮忙执行任务**（找到那段代码）？`wait_for_task_completion` 的死锁防护？ | `notes/godot/worker-thread-pool.md` |
| 跨线程命令队列 | `core/templates/command_queue_mt.h` | ★★★ | 无锁/有锁环形队列怎么实现？`push` 与 `flush` 的内存序选择？（M9/M10 渲染线程直接复用这个思路） | `notes/godot/command-queue.md` |
| 安全容器 | `core/templates/safe_list.h`、`safe_refcount.h`、`core/templates/ring_buffer.h` | ★★ | "safe" 系列解决了什么并发问题？ | 同上 |
| 资源加载的线程实现 | `core/io/resource_loader.cpp` 的 threaded 部分（M6 已读一遍，这次专看线程） | ★★★ | 哪些数据被锁保护？锁粒度多大？如何用 TSan 类工具验证（Godot 有 `dev_build` + sanitize 选项）？ | `notes/godot/threading-boundary.md` |

### M9 / M10 · Server 模式与渲染解耦
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| Server 抽象 | `servers/rendering/rendering_server.h` | ★★★ | RenderingServer 的 API 是"命令式"还是"对象式"？为什么场景侧只持 `RID` 而不持渲染对象？这样做换来了什么？ | `notes/godot/server-pattern.md` |
| **Server 多线程包装** | `servers/server_wrap_mt_common.h` | ★★★ | 宏如何把单线程 Server 自动包装成"命令入队 + 渲染线程执行"？这个设计的代价是什么（调试难度、延迟一帧）？ | `notes/godot/server-wrap-mt.md` |
| 默认实现 | `servers/rendering/rendering_server_default.h/.cpp` | ★★ | 单线程默认实现如何与 mt 包装共存？ | 同上 |
| DisplayServer | `servers/display/display_server.h`、`servers/display_server_wrapper.h` | ★★ | headless 后端怎么做（`display_server_headless`）？对照我的 NullBackend | `notes/godot/headless-backend.md` |
| Vulkan 驱动组织 | `drivers/vulkan/`（`rendering_device_driver_vulkan.*`、`vulkan_context.*`） | ★★ | RenderingDevice（RHI 层）与 RenderingServer（高层）如何分层？对照我 Yo_Renderer 的 `rhi/`+`render/` 分层 | `notes/godot/vulkan-driver-layering.md` |
| 快照式渲染数据 | `servers/rendering/rendering_server_globals.h`、`scene/3d/visual_instance_3d.h` | ★★ | Godot 的场景→渲染数据同步是"推"还是"拉"？与我的 RenderSnapshot 方案对比 | `notes/godot/render-sync.md` |

### M11 · 构建系统、模块、打包、测试、剖析
| 主题 | 文件 | 程度 | 要回答的问题 | 笔记 |
|---|---|---|---|---|
| 构建系统 | `SConstruct`、`methods.py`、`core/SCsub`、`scene/SCsub`、`platform/linuxbsd/SCsub` | ★★ | SCons 的"每目录一个 SCsub"模型 vs 我的"每层一个 CMakeLists"？代码生成（`*.gen.cpp`）在构建里怎么插入？ | `notes/godot/build-system.md` |
| 模块机制 | `modules/`（挑一个小模块如 `modules/hdr/`、`modules/bmp/` 看 `SCsub` + `register_types.cpp`）、根 `modules/register_module_types.h` | ★★ | 一个模块需要提供哪些钩子才能被引擎"发现"？对照我的分层 target 设计 | `notes/godot/module-system.md` |
| 打包格式 | `core/io/file_access_pack.h` + `.cpp`、`file_access_patched.h` | ★★★ | PCK 的头部/文件表/对齐/版本怎么设计？patch 包如何叠加？（`yr_pack` 直接对照这个做） | `notes/godot/pck-format.md` |
| 配置系统 | `core/config/project_settings.h` + `.cpp`、`core/io/config_file.h` | ★★ | 项目设置的默认值/覆盖/命令行优先级如何组织？对照我的 `EngineConfig` | `notes/godot/project-settings.md` |
| 测试框架 | `tests/test_main.cpp`、`tests/test_macros.h`、`tests/create_test.py`、`tests/core/io/test_logger.cpp` | ★★ | Godot 用 doctest 的组织方式？测试如何避免依赖真实文件系统？值得抄的 fixture 设计？ | `notes/godot/testing.md` |
| 剖析 | `core/debugger/engine_profiler.h`、`core/profiling/`（`ls core/profiling`）、`core/debugger/` | ★★ | 内建 profiler 的采样点如何埋？数据如何送到编辑器？（S10 stretch 参考） | `notes/godot/profiling.md` |
| 日志 | `core/io/logger.h`（若不存在则 grep `class Logger`）、`tests/core/io/test_logger.cpp` | ★ | Godot 的 Logger 抽象与 yo_lib logger 的差距？ | 同上 |

---

## 3. 阅读进度追踪

每篇笔记写完后在此打勾并写日期。**目标：项目结束时 ≥8 篇（`00-vision.md` §4.5）。**

- [ ] `notes/godot/rid.md`（M1）
- [ ] `notes/godot/string_name.md`（M1）
- [ ] `notes/godot/containers.md`（M1）
- [ ] `notes/godot/object-model.md`（M2）
- [ ] `notes/godot/class-db.md`（M2）
- [ ] `notes/godot/refcount.md`（M2）
- [ ] `notes/godot/variant.md`（M2）
- [ ] `notes/godot/method-bind.md`（M2）
- [ ] `notes/godot/signals.md`（M3）
- [ ] `notes/godot/message-queue.md`（M3）
- [ ] `notes/godot/main-iteration.md`（M4）
- [ ] `notes/godot/node-lifecycle.md`（M4）
- [ ] `notes/godot/scene-tree.md`（M4）
- [ ] `notes/godot/transform-propagation.md`（M4）
- [ ] `notes/godot/tscn-format.md`（M5）
- [ ] `notes/godot/packed-scene.md`（M5）
- [ ] `notes/godot/binary-format.md`（M5）
- [ ] `notes/godot/serialization-strategy.md`（M5）
- [ ] `notes/godot/resource.md`（M6）
- [ ] `notes/godot/async-loading.md`（M6）
- [ ] `notes/godot/resource-uid.md`（M6）
- [ ] `notes/godot/worker-thread-pool.md`（M7）
- [ ] `notes/godot/command-queue.md`（M7）
- [ ] `notes/godot/threading-boundary.md`（M7）
- [ ] `notes/godot/server-pattern.md`（M9）
- [ ] `notes/godot/server-wrap-mt.md`（M9/M10）
- [ ] `notes/godot/headless-backend.md`（M9）
- [ ] `notes/godot/render-sync.md`（M9）
- [ ] `notes/godot/vulkan-driver-layering.md`（M10）
- [ ] `notes/godot/build-system.md`（M11）
- [ ] `notes/godot/module-system.md`（M11）
- [ ] `notes/godot/pck-format.md`（M11）
- [ ] `notes/godot/project-settings.md`（M11）
- [ ] `notes/godot/testing.md`（M11）
- [ ] `notes/godot/profiling.md`（M11）

---

## 4. 超越读码：从 Issue/PR 学"为什么"

读代码只能看到"现在是什么样"，看不到"为什么不是别的样子"。Godot 的 PR 讨论是极好的教材。

**方法**：当你对某个设计产生"我觉得这样不好"的判断时，**先去搜有没有人提过同样的问题**。
```bash
cd /home/yoyorm/Code/GodotDev/godot
git log --oneline --grep="scene tree" -i | head -20       # 按关键词搜 commit
git log -p -S "<某段代码>" -- <文件> | head -100            # 找引入这段代码的 commit（pickaxe）
```
拿到 commit hash 后去 GitHub 看对应 PR 的讨论（`https://github.com/godotengine/godot/commit/<hash>`）。

**推荐追踪的几个长期争议主题**（每个都能写成一篇有观点的博客）：
| 主题 | 为什么值得看 |
|---|---|
| Node 生命周期与 `_ready` 时机的多次改动 | 生命周期语义极难一次设计对，看它如何演进 |
| Fixed timestep interpolation（`scene_tree_fti.*`）落地过程 | 一个功能从提案到合入的完整工程过程 |
| RenderingDevice / Vulkan 驱动重构 | RHI 层抽象如何在真实项目中演化（对你 M10 直接有用） |
| `WorkerThreadPool` 的引入与调整 | 引擎级线程池的设计权衡 |
| Typed Dictionary / Variant 类型系统演进 | 动态类型系统的工程代价（你本机 `GodotDev/typed_dict_review.markdown` 已经在跟这条线） |
| 构建系统（SCons → 是否换 CMake 的长期讨论） | 大型 C++ 项目构建系统的真实痛点，对你 M0/M11 有用 |

**输出要求**：每个主题写一篇 `notes/godot/pr-review-<主题>.md`，格式：
① 我原本的疑问/判断 → ② 社区里的实际讨论与结论（附链接/commit） → ③ 我的判断需要修正吗 → ④ 对 YRuntime 的影响（改 or 不改）。

---

## 5. 反模式警告

| 不要做 | 因为 |
|---|---|
| 通读 Godot 源码 | 14GB、几百万行，读不完，且 90% 与本项目无关 |
| 直接复制 Godot 的类结构 | 它的规模约束（支持脚本/编辑器/多平台/多后端）与你完全不同，照抄会背上不需要的复杂度 |
| 引用网上博客里的 Godot 行号/文件路径 | 版本漂移严重；**只引用本机 checkout 的实际路径 + 你自己的笔记** |
| 把 Godot 的宏照抄进 YRuntime | `GDCLASS` 的复杂度是为绑定 GDScript/C#/编辑器服务的；你的 `YR_CLASS` 应该简单一个数量级 |
| 用 Godot 的做法当"标准答案"压制自己的思考 | 你的目标是形成判断力。**差异 + 理由**才是笔记的价值所在 |
| 读源码代替写代码 | 每周 Godot 阅读时间上限 ≈ 总时间的 20%。超了就是在拖延 |

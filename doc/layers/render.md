# render 层（`yr_render_iface` + `yr_render_null` + `yr_render_vulkan`）

> 代码位置：`engine/render/iface/include/yr/render/`（+ `src/snapshot_io.cpp`）与 `engine/render/null/include/yr/render/null_backend.h`（+ `src/null_backend.cpp`）。
> 契约层 `yr_render_iface` 只依赖 `yr_core` 与 std；Null 后端 `yr_render_null` 依赖 `yr_render_iface` + `yr_core`。设计意图见 `../01-architecture.md` §4.11，推进范围见 `../02-roadmap.md` M5。本文件是**代码级契约的主来源**。

## render_iface（`yr::render` 契约层）

### AssetHandle（`yr::render::AssetHandle`）

- **职责**：资源句柄的**强类型**包装。跨 Runtime→Renderer 边界只作不透明 key。
- **字段 / 公开 API**：

  | 字段 / 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `std::uint64_t value = 0` | 原始值；`0` = 无效 | — |
  | `bool isValid() const noexcept` | `value != 0` | — |
  | `operator==`（default） | 按 `value` 比较 | — |

- **不变量 / 顺序**：默认构造即**无效**；不得从裸整数**隐式**转换（`std::is_convertible_v<uint64_t, AssetHandle>` 为 `false`）。注意：C++20 允许聚合的带括号逐字段初始化，`is_constructible` 仍为 `true`，类型安全靠"无隐式转换 + 具名字段"。
- **设计原因**：M5 无正式资源系统，用临时内建 triangle / default material ID 撑起契约；后端只当 key，不得索引运行时容器。
- **遗留 / 已知缺口**：正式 `AssetUID` 在 M9 替换本类型。

### RenderObjectId / TransformPod / DrawItem / CameraPod / RenderSnapshot

- **职责**：`SceneTree → Backend` 的纯值快照（M5 冻结最小版）。**不含任何 `yr::scene::*` / `Object*` / `Vk*` 类型，只传 ID 与值。**
- **逐字段语义与单位 / 坐标系**：

  | 类型 / 字段 | 语义 / 契约 |
  |---|---|
  | `RenderObjectId::value` | 承载运行时 `ObjectID` 的值；后端只当**不透明 key**，不得用它索引任何运行时容器，也不得假定其位布局 |
  | `TransformPod::m[16]` | 布局同 `yr::math::Mat4`：**column-major（index = col*4 + row）、列向量 v' = M · v、右手 Y-up、深度范围 [0,1]**（Vulkan NDC）。默认构造为 **identity**（与 `Mat4` 一致），不留全零退化矩阵 |
  | `DrawItem::object` | 该 item 对应的对象身份 |
  | `DrawItem::world` | 该对象的世界矩阵（列主序，同 TransformPod 约定） |
  | `DrawItem::mesh` | 网格引用；M5 为内建 / 无效 |
  | `DrawItem::material` | 材质引用；M5 为内建 / 无效 |
  | `CameraPod::view` / `proj` | 视图 / 投影矩阵，各为一个 `TransformPod`（同一矩阵约定） |
  | `RenderSnapshot::frame_index` | 递增帧序号（M5/M7 单缓冲；snapshot ring 按 D22 推迟） |
  | `RenderSnapshot::camera` | M5 **单相机**，固定默认值 |
  | `RenderSnapshot::items` | 本帧待绘条目；M5 按前序遍历顺序即确定，无排序键 |

- **不变量 / 顺序**：元素类型 `AssetHandle` / `RenderObjectId` / `TransformPod` / `DrawItem` / `CameraPod` 均 `std::is_trivially_copyable_v`（编译期断言在头文件内）；`sizeof(TransformPod) == 64`。`RenderSnapshot` 含 `std::vector`，**刻意不做 POD 断言**。
- **设计原因**：数据式契约把逻辑世界与图形 API 解耦——后端不得回调 Runtime、不得持有场景指针（D7）；换后端不需改 Runtime。
- **遗留 / 已知缺口**：`uploads` / `releases`（M9）、`LightPod`（M7）、相机 `fov`/`near`/`far` 与多相机（M7）、`sort_key` 位段（M7）、双 / 三缓冲交换协议（M7）均为**规划**，见 `../02-roadmap.md` M5 的"明确不做"表。

### BackendConfig / BackendStats / RendererBackend

- **职责**：后端接口与其输入 / 统计类型。
- **逐个语义**：

  | 类型 / 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `BackendConfig{}` | M5 headless：无窗口 / 设备参数；字段等到 M6 有消费者再加 | — |
  | `BackendStats::items` | 本帧条目数 | — |
  | `BackendStats::draw_calls` | 本帧 draw call 数 | — |
  | `BackendStats::triangles` | 本帧三角形数 | — |
  | `BackendStats::snapshot_bytes` | 快照编码字节数 | — |
  | `bool initialize(const BackendConfig&)` | 初始化设备 / 资源；成功返回 `true` | 失败返回 `false` |
  | `void shutdown()` | 释放资源；幂等 | — |
  | `void present(const RenderSnapshot&)` | 消费快照；**不得回调 Runtime** | — |
  | `const char* name() const noexcept` | 后端名（稳定字面量） | — |
  | `BackendStats last_frame_stats() const noexcept` | 最近一次 `present` 的统计 | 未 `present` 时为零值 |

- **所有权 / 生命周期**：`RendererBackend` 是接口；`present` 只借入快照的 const 引用，不取得其所有权、不得保存指针。后端实现由工厂（如 `create_null_backend()`）返回 `std::unique_ptr` 交给装配层。
- **设计原因**：接口只进快照、不出命令，后端无法反向依赖 Runtime；`BackendStats` 为后端自主性提供可测的观测点。
- **Godot 对照**：`servers/rendering/rendering_server.h`（**命令式 Server**：Runtime 发命令、场景侧只持 RID——我们刻意不照搬，改为一次性 POD 快照）；`core/templates/rid.h`（不透明 ID 与本层 `AssetHandle` / `RenderObjectId` 同源）。

### encodeSnapshot / decodeSnapshot

- **职责**：快照的确定性二进制序列化，用于 dump / replay 与契约完备性验收。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `std::vector<uint8_t> encodeSnapshot(const RenderSnapshot&)` | 按固定字段序写**小端**字节 | — |
  | `bool decodeSnapshot(const uint8_t* data, size_t size, RenderSnapshot& out)` | 校验 magic / version / 长度后解码 | 非法返回 `false` 且**不修改 `out`** |

- **字节格式**（全部小端；float 经 `std::bit_cast` 到 `uint32_t` 写入，不依赖宿主端序）：

  ```text
  magic        6 字节 "YRSNAP"
  version      u32 = 1
  frame_index  u64
  camera.view  16 × f32
  camera.proj  16 × f32
  item_count   u64
  重复 item_count 次：
    object     u64
    world      16 × f32
    mesh       u64
    material   u64
  ```

- **不变量 / 顺序**：
  - **确定性判据**：固定字段序 + **不复用结构体内存布局**（逐字段写，天然绕开 padding / 对齐），因此同一输入两次编码**逐字节相同**。
  - **解码严格性**：magic 必须匹配、`version == 1`、长度必须**恰好**匹配（多一个字节也算非法）；任一不满足返回 `false` 且 `out` 保持不变（内部先解码到局部变量，全部通过才 `out = std::move(...)`）。
- **设计原因**：dump / replay 是"契约真的完备"的可执行证据；不 memcpy 结构体使格式与 ABI / 编译器布局无关。
- **CLI 接入（M7d）**：`render_demo --dump-snapshot PATH` 在阶段 11 把**首个进入 present 阶段的快照**（正常路径下就是实际送显的那一帧）编码后写入文件（只写一次；写入失败置 `dumpFailed_`、请求退出并以 1 退出）；`--replay-snapshot PATH` 在 Vulkan 初始化后、创建 `SceneTree` 之前分流，只读文件 + `decodeSnapshot` + `VulkanBackend::present`，**不创建** `SceneTree` / `EventBus` / `MainLoop` / `ObjectDB`。两个 flag 互斥（同时出现按用法错误以 2 退出）；文件格式仍是 version 1。
- **遗留 / 已知缺口**：无版本迁移逻辑。

## NullBackend（`yr::render::NullBackend`）

- **职责**：`RendererBackend` 的 headless 实现，**只统计不画**，用于在没有 GPU 时驱动契约与确定性验收。
- **所有权 / 生命周期**：由 `create_null_backend()` 返回 `std::unique_ptr<RendererBackend>`；`initialize` 置 `initialized_ = true`，`shutdown` 置 `false`，二者均不拥有外部资源。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `bool initialize(const BackendConfig&) override` | 置 `initialized_ = true`，恒返回 `true` | — |
  | `void shutdown() override` | 置 `initialized_ = false`；幂等 | — |
  | `void present(const RenderSnapshot&) override` | 计算并存统计，不产生任何绘制 | — |
  | `const char* name() const noexcept override` | 恒为 `"null"` | — |
  | `BackendStats last_frame_stats() const noexcept override` | 最近一次 `present` 的统计 | — |
  | `std::unique_ptr<RendererBackend> create_null_backend()` | 构造 `NullBackend` 的工厂 | — |

- **统计口径**（`present` 时计算）：
  - `items = snapshot.items.size()`
  - `draw_calls = snapshot.items.size()`
  - `triangles = snapshot.items.size()`（M5 约定每个 item 一个内建三角形）
  - `snapshot_bytes = encodeSnapshot(snapshot).size()`
- **不变量 / 顺序**：同输入 `present` 两次统计完全相同（依赖 `encodeSnapshot` 的确定性）。`name()` 不依赖初始化状态。
- **设计原因**：NullBackend 是"契约消费者"的最小实现——它跑得通，说明快照自洽且可被独立后端消费，而无需任何图形 API。
- **Godot 对照**：`servers/display/display_server.h` 的 headless 形态（对照阅读）。
- **遗留 / 已知缺口**：`initialized_` 目前只作状态标记，`present` 不校验它；无依赖 `initialized_` 的行为。真实后端（`yr_render_vulkan`）在 M6/M7。

## Vulkan 后端（`yr_render_vulkan`，M6-1 外壳 + M6-2 Surface / Device + M6-3 Swapchain / 清屏 + M6-4a N 帧在飞 + M6-4b 三角形 + M7b 快照驱动）

> 代码位置：公开头 `engine/render/vulkan/include/yr/render/vk/vulkan_backend.h` + 私有实现 `src/{vulkan_backend,platform_window,vk_instance,vk_surface,vk_device,vk_device_selection,vk_swapchain,vk_command_pool,vk_frame_sync,vk_triangle_pipeline,vk_debug}.{h,cpp}` + shader `shaders/{triangle.vert,triangle.frag}` + 编译期配置唯一入口 `src/vk_hpp.h`。
> 构建开关 `YR_BUILD_VULKAN_BACKEND`（默认 **OFF**）：关掉时本 target 与 `apps/render_demo` 都不存在，默认构建不需要 Vulkan SDK / GLFW。**开这个开关用 `cmake --preset debug-vk`（或 `release-vk`）构建/开发**；clangd 需把根 `compile_commands.json` 指向该 preset 的 DB（`tools/link_compile_commands.sh debug-vk`），否则拿不到 vulkan TU 的编译命令、找不到 `<vulkan/...>`。环境细节见 `../05-engineering.md` §1 的 Vulkan SDK 行。

- **公开头必须 vulkan-free / GLFW-free**：`vulkan_backend.h` 只 include `<memory>` 与 `<yr/render/backend.h>`，用 PIMPL（`struct Impl` + `std::unique_ptr<Impl>`）把 `VkInstance` / `GLFWwindow` 关在 `.cpp` 与 `src/` 私有头里。这是"图形 API 不越过本层"的可编译证据。
- **图形 API 隔离是机械门禁**：`engine/render/vulkan/` 是**唯一**允许 `#include <vulkan/...>` / `<GLFW/...>` 的目录（`tools/check_deps.py` 的 `GRAPHIC_ALLOWED_DIRS`）；`apps/render_demo` 只 include `<yr/render/...>`。本层允许的依赖为 `render_iface` + `core` + `math`（M7b 起在 CPU 侧算 MVP）。
- **validation 层只在 Debug**：CMake 用 `$<$<CONFIG:Debug>:YR_ENABLE_VULKAN_VALIDATION=1>` 注入宏；`vk_instance.cpp` 在宏打开时先用 `vk::enumerateInstanceLayerProperties()` 探测 `VK_LAYER_KHRONOS_validation`，可见才启用并额外加 `VK_EXT_debug_utils`，不可见则**初始化硬失败**（抛异常，提示先 `sudo apt install vulkan-validationlayers`，或 `source ~/SDK/.../setup-env.sh` 提供 `VK_ADD_LAYER_PATH`），避免跑到没有 validation 的假绿。
- **`DebugStats` 的存亡契约**：它是 `Instance` 的成员并声明在 messenger 成员**之前**，`pUserData` 指向它；error 计数并立即打印，warning 只计数。计数只能在 `Instance` 析构前取走，`VulkanBackend::Impl::shutdown()` 先取副本再 `reset()`，因此该统计**不含** instance 自身析构期产生的消息（messenger 必须先于 instance 销毁，属固有限制）。
- **`Surface`（M6-2）由 `glfwCreateWindowSurface` 创建并包进 `raii::SurfaceKHR`**：依赖 instance + window，必须先于二者销毁。`shutdown()` 先 `surface.reset()` 再取 `DebugStats` 副本，使 surface 析构期产生的 validation 消息也被计入。
- **`Device`（M6-2）是逻辑设备 + 队列的所有者**：先枚举物理设备并调用**纯策略** `select_physical_device`（`vk_device_selection`，只读属性并打日志、不创建 Vulkan 对象；拒绝 `eCpu`，要求 `VK_KHR_swapchain` + graphics 族 + present 族，同族优先，逐设备记录拒绝原因），失败抛异常；只启用 `VK_KHR_swapchain`，graphics / present 按 distinct family 建 `DeviceQueueCreateInfo`（同族一条、异族两条，`queueCount = 1`）；`vk::Queue` 不包 RAII，只存由 device 拥有的裸句柄。`raii::Device` 的构造参数是 `raii::PhysicalDevice`，物理设备非拥有，故仅在构造期借 `Instance` 的 instance 句柄包一个临时 `raii::PhysicalDevice`。M6-3b 先用 `getFeatures2<PhysicalDeviceFeatures2, PhysicalDeviceVulkan13Features>()` 查询，`dynamicRendering` / `synchronization2` 任一不支持即抛异常；创建时用 `StructureChain<DeviceCreateInfo, PhysicalDeviceVulkan13Features>` 两项置 true，chain 活到 `createDevice` 返回。
- **`validationErrorCount()` 是具体后端的诊断访问器**：只在 `VulkanBackend` 上提供，不属于 `RendererBackend` 契约；返回本次运行累计的 validation error 数（validation 被编译掉时恒 0）。`render_demo` 用它把"零 error"变成进程退出码（有 error 返回 1）。
- **`Swapchain`（M6-3）只拥有 `raii::SwapchainKHR` + image views，不拥有 depth**：format / present mode / extent 从 surface 能力现查选择（优先 B8G8R8A8Srgb + SrgbNonlinear，否则 `formats[0]`；优先 `eMailbox`，否则 `eFifo`；extent 用 `currentExtent` 或窗口尺寸 clamp 到 min/max，0×0 抛异常）；graphics / present 不同族时用 `eConcurrent` + 两个族索引。`create()` 是 `const` 且只返回私有 `Created{handle, images, format, extent}`，不碰成员；`buildImageViews()` 同样只返回 locals。创建/重建时缓存 `std::vector<vk::Image>`（非拥有，句柄归 swapchain），提供 `image(index)`；帧循环不再调用 `getImages()`。构造与 `recreate` 都是**强异常安全**：先建新 swapchain → 建新 views → 用新 views 替换旧 views（旧 views 死，旧 swapchain 还活着）→ 替换 swapchain（旧 handle 死）→ 更新 images / format / extent；任何一步抛异常，旧链保持完整。
- **`vk_hpp.h` 是编译期配置的唯一入口**：所有 TU 只通过它包含 `<vulkan/vulkan_raii.hpp>`，并在包含前定义 `VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS`。宏生效后 acquire 返回 `ResultValue<uint32_t>`、present 返回 `vk::Result`，`eErrorOutOfDateKHR` **不抛异常**，而是作为正常返回值处理（D14：resize / 最小化是分支，不是异常控制流）。
- **`CommandPool`（M6-3b / M6-4a）持有 `framesInFlight` 条 primary command buffer**：`CommandPoolCreateInfo{eResetCommandBuffer, graphics family}`，用 `device.raiiDevice().allocateCommandBuffers()` 分配（`raii::CommandPool` 自身没有该成员）；按 frame slot 取，每帧 `reset()` 后重录。不可拷贝 / 移动。swapchain 重建不重建它：buffer 数只依赖 `framesInFlight`。
- **`FrameSync`（M6-4a）按两种粒度分配同步对象**：`imageAvailable` / `inFlight` 按 frame slot 分配（`framesInFlight = 2`，fence 全部**创建为 signaled**，否则第一轮 `waitForFences` 永久阻塞）；`renderFinished` 按 swapchain image 索引分配（`imageCount` 个）。前者靠 CPU 侧顺序成立：同一 slot 的下一次使用必然发生在它的 fence 已被 wait 过之后；后者靠 present 的因果成立：同一 image 的 present 完成才会让该 image 被再次 acquire，所以该 semaphore 的下一次 signal 必然发生在旧的 wait 被消费之后。把 `renderFinished` 按 slot 复用只是"实践上成立"的假设，规范不保证。swapchain 重建可能改变 `imageCount`，因此 `FrameSync` 与 swapchain 一起重建。
- **`TrianglePipeline`（M6-4b / M7b）只依赖 device + color format**：顶点由 vertex shader 的 `gl_VertexIndex` 生成，**无 vertex buffer / descriptor**；`PipelineLayout` 声明一个 80 字节 push constant（`{ mat4 mvp; vec4 color; }`，vertex + fragment 两阶段），逐 draw 推送；`PipelineRenderingCreateInfo` 的 format **来自当前 swapchain**（`swapchain->format()`），挂在 `GraphicsPipelineCreateInfo::pNext`；viewport / scissor 是 **dynamic state**，录制命令时按当前 extent 设置，因此 resize 本身不必重建 pipeline，但 swapchain 重建时**无条件重建 pipeline**（format 可能变化，代价小、逻辑简单）。shader 由构建期 `glslc` 编译，运行时从 `YR_VULKAN_SHADER_DIR` 加载 `triangle.{vert,frag}.spv`（M7/M9 换资源系统）。
- **Y-up 约定与负高度 viewport（M7b）**：`TransformPod` 约定世界 +Y 向上，而 Vulkan NDC 的 +Y 向下。渲染时用**负高度 viewport**（`height = -extent.height`）把 NDC 翻回 Y-up，三角形顶点也按 Y-up 定义。负高度会使绕序反转，因此 culling 保持 `eCullNone`；将来开启 culling 时需要相应调整 `frontFace`。
- **一帧算法（`Impl::renderFrame`，N 帧在飞）**：取 `slot = frameSlot_` → `waitForFences(inFlight(slot))` → `acquireNextImage(imageAvailable(slot))`（保留 `ResultValue`）→ 成功后 `resetFences(inFlight(slot))` → 对 `commandPool.commandBuffer(slot)` 做 `cmd.reset()` / `begin` → barrier2 `UNDEFINED → eColorAttachmentOptimal`（src/dst stage 均 `eColorAttachmentOutput`，src access `eNone`，dst access `eColorAttachmentWrite`）→ dynamic rendering `beginRendering` / `endRendering`（`layerCount = 1`，clearColor `{0.10, 0.18, 0.30, 1.0}`）→ 在 rendering 内 `bindPipeline` + `setViewport` / `setScissor`（按当前 extent）→ **每个 `DrawItem` 一次 `pushConstants` + `draw(3, 1, 0, 0)`**；`items` 为空时只清屏、不 draw → barrier2 `eColorAttachmentOptimal → ePresentSrcKHR`（dst stage / access 均 `eNone`）→ `cmd.end()` → `submit2`（graphics queue wait `imageAvailable(slot)` @ `eColorAttachmentOutput`，signal `renderFinished(imageIndex)`，fence `inFlight(slot)`）→ `presentKHR`（present queue wait `renderFinished(imageIndex)`）→ **提交之后**才推进 `frameSlot_ = (frameSlot_ + 1) % framesInFlight`。acquire 的 image index 与 frame slot 是两套索引。不建 render pass / framebuffer。
- **相机由 host 提供**：host 计算 `CameraPod`（`Mat4::perspective` + `Mat4::lookAt`）后调 `MainLoop::setCamera`；阶段 10 的 `RenderExtractSystem::extract` 把它原样写入快照，默认 identity。后端只消费快照，不计算相机。
- **每个 item 的 push constant 内容**：`mvp = proj * view * world`（CPU 计算，column-major 直接上传）；`color` 按 `item.object.value % 4` 从固定 4 色调色板选，仅用于区分对象。`TransformPod` 与 `Mat4` 同布局，转换用 `std::bit_cast` 并有 `static_assert(sizeof(TransformPod) == sizeof(Mat4))`。
- **`BackendStats` 填法（Vulkan）**：`items = snapshot.items.size()`、`draw_calls` 与 `triangles` 同值（每 item 一个三角形）、`snapshot_bytes = sizeof(RenderSnapshot) + items * sizeof(DrawItem)`。
- **`render_vulkan` 依赖 `math`**：CMake `PRIVATE yr::math`，`tools/check_deps.py` 的 `render_vulkan` 允许依赖相应加入 `math`（矩阵运算在 CPU 侧，不泄漏到契约层）。
- **0×0 跳过**：host 在阶段 1 调 `pollEvents()`；`present()` 在 framebuffer 为 0×0（最小化）时直接返回，不 acquire、不重建。
- **OOD / Suboptimal → 重建**：`needsRecreate()` 用 surface 能力 + 与 `create` 共用的 `computeExtent()` 现算期望 extent，与 `extent_` 比较（不拿窗口像素尺寸直接比）。acquire 的 OOD 路径**不 reset fence、不推进 slot**，`waitForFences` 失败或 acquire 其它失败同样不推进。present 的 OOD / Suboptimal（含 acquire 的 Suboptimal）在推进 slot 后重建。重建先 `waitIdle`（旧 `FrameSync` 的同步对象可能仍被 pending 提交引用），再 `swapchain->recreate(...)`，然后按新 `format` 无条件重建 `TrianglePipeline`，用新的 `imageCount` 重建 `FrameSync` 并复位 `frameSlot_ = 0`。swapchain 重建抛异常时旧链保持完整；若 swapchain 已换新而 `TrianglePipeline` / `FrameSync` 构造失败，对应 `optional` 为空，`present()` 下一帧发现缺失会再走一次重建恢复。重建异常只记日志，下一帧继续尝试。
- **`shutdown()` 先 `waitIdle` 再销毁同步对象**：fence / semaphore / command buffer 不能在提交仍 pending 时释放。
- **M7b 的 `present` 消费快照**：draw 次数与每个 item 的 MVP / 颜色都来自 `RenderSnapshot`；`last_frame_stats()` 每帧按快照更新（见上）。
- **单快照 vs frame-in-flight（M7/D22）**：`kFramesInFlight = 2` 缓冲的是 command buffer / fence / semaphore，不是 `RenderSnapshot`；`present()` 同步到 submit，GPU 执行异步。backend 在 record 阶段把 camera/world 复制进 command buffer/push constant，不保留快照指针，因此 M7 单快照安全；snapshot ring 按 D22 推迟到出现真实异步消费者。
- **`shouldClose()` 是 host 级平台查询**：不属于 `RendererBackend` 契约；M7a 起由 `DemoLoop::pollInput` 在阶段 1 泵事件后消费并调 `requestQuit()`。`pollEvents()` 同样只由 host 在阶段 1 调用，`present()` 不再泵事件。
- **`Key` / `isKeyDown()` 是 host 级输入查询（M7c）**：`Key` 只覆盖 W/A/S/D，`isKeyDown()` 在 `pollEvents()` 之后读取当前按键状态；二者不属于 `RendererBackend` 契约，公开头仍保持 GLFW-free。
- `create_vulkan_backend()` 与 `create_null_backend()` 同形（返回 `std::unique_ptr<RendererBackend>`）；`initialize` 把 Vulkan-Hpp 抛出的异常翻译成"日志 + `false`"，构造窗口/instance 失败不会泄漏半成品。

### M6 结构约束（跨 M6-1~M6-5）

**所有者与析构顺序**（`Impl` 成员声明顺序 = 依赖顺序；析构逆序）：

| 所有者 | 拥有 | 依赖 | 必须先于谁销毁 |
|---|---|---|---|
| `Window` | `GLFWwindow`、进程级 `glfwInit` | — | — |
| `Instance` | `raii::Instance`、messenger、`DebugStats` | Window | （最后） |
| `Surface`（M6-2） | `raii::SurfaceKHR` | Instance、Window | Instance |
| `Device`（M6-2） | `raii::Device`、物理设备句柄、graphics / present 队列 | Instance、Surface | Surface |
| `Swapchain`（M6-3） | `raii::SwapchainKHR`、缓存的 images（非拥有）、image views | Device | Device |
| `TrianglePipeline`（M6-4b） | 2 个 ShaderModule、PipelineLayout、Pipeline | Device（format 来自 Swapchain） | Device |
| `TrianglePipeline`（M6-4b） | `raii::ShaderModule` ×2、pipeline layout、pipeline | Device、Swapchain format | Swapchain |
| `CommandPool`（M6-3b） | `raii::CommandPool`、command buffers | Device | Device |
| `FrameSync`（M6-4a） | 信号量、fence | Device | Device |

- **顺序由依赖图决定**："能不能呈现"必须拿 surface 去问设备，所以 surface 先于 device；surface 由 instance 创建，所以 surface 先于 instance 销毁。
- **构造即有效**：组件不允许"构造了但没 init"；失败抛异常，只在 `VulkanBackend::initialize` 一个边界翻译成 `false`。`Impl` 用 `std::optional` 表达"存在 / 不存在"，不是两段式初始化。
- **`vk::Queue` 不是 RAII 对象**：由 device 拥有，只能存裸句柄。
- **只启用已验证且马上要用的扩展 / feature**：M6-2 只启用 `VK_KHR_swapchain`；M6-3b 通过 `PhysicalDeviceVulkan13Features` 同时查询并启用 `dynamicRendering` + `synchronization2`（两者都是 1.3 core feature，**不是**扩展字符串），M6-4b 只加三角形。
- **设备选择是纯策略**：显式拒绝 `DEVICE_TYPE_CPU`，要求 `VK_KHR_swapchain` + graphics 族（`queueCount > 0`）+ present 族；同族优先；逐设备记录拒绝原因。
- **公开头保持 vulkan-free / GLFW-free**：M6 期间不新增渲染契约方法；M7a 新增的 `pollEvents()` / `shouldClose()` 是 host 级平台接口，不是 `RendererBackend` 契约。

**Yo_Renderer 借鉴处置**（M6 期间的正式结论）：

| 来源 | 处置 | 落点 |
|---|---|---|
| `platform/VulkanContext` | 拆解重写 | `Surface` + `Device` + `vk_device_selection` |
| `platform/Swapchain` | 借鉴参数选择策略，重写 | `vk_swapchain`（M6-3）；depth 不进 |
| `platform/SyncObjects` / `rhi/sync/FrameSync` | 借鉴 per-slot 结构，重写 | `vk_frame_sync`（M6-4a） |
| `rhi/command/CommandRecorder` | 暂不做 | M6-4 直接用裸 begin / end / draw |
| `render/VulkanResourceTable` | 暂不做（M9） | — |
| `render/RenderServer` | 不搬 | M7 的 `RenderExtractSystem` 取代 |
| `app/Application` | 不搬 | M7 的 `MainLoop` 取代 |

**不照抄的反模式**：上帝对象；两段式 `init() / shutdown()`；无条件启用 validation；未查支持就置 feature；swapchain 类混入 depth 属主；两族不同却用 `eExclusive`；resize / OOD 处理散落多处；debug callback 绕过 logger。

## 本层遗留 / 接线现状

- **快照的产出在 engine 层**：`yr_engine` 的 `RenderExtractSystem`（`engine/engine/include/yr/engine/render_extract.h`）是 `SceneTree → RenderSnapshot` 的唯一单向转换点（D7 规则②）；帧阶段 10 `extract_render` 调用它，阶段 11 `present` 把快照交给装配者注入的非拥有 `RendererBackend*`。本层（`yr_render_iface` / `yr_render_null`）只提供契约与 Null 后端，不含抽取逻辑；层契约细节见 `engine.md` 的 `RenderExtractSystem` 小节。
- 其余推迟项（uploads / releases / lights / sort_key / 多相机 / 双三缓冲 / 独立渲染线程）见 `../02-roadmap.md` M5 的"明确不做"表。
# 资产层 worker 化（M10 最后一项 + M9.5d-0）

> 状态：**主体完成**（2026-11-19；E10 已达标，Sponza 4K 待外部资产实测）
> 最近更新：2026-11-19

## 1. 目标

把 `AssetDatabase` 的**文件读取 + 纹理解码**都搬到 `JobSystem`，主线程只做 `pump()` 安装；同时收掉 M9.5d-0（Sponza 4K 纹理）的遗留。

验收时能回答三个问题：

1. 慢 IO 下主循环帧时间是否不被拖垮（E10：`ThrottledFileAccess` 50ms × 20 资源，帧 p95 < 5ms）；
2. Sponza 72 张 4K 纹理的端到端时间（现在同步约 **98.4 s**）；
3. 任务系统作为"真实消费者"是否成立（背压、预算、停机、TSan 全绿）。

## 2. 现状（改造前）

```
AssetDatabase
 ├─ fileAccess_   (Disk / Memory / Throttled)
 ├─ loaderWorker_ (自有 std::thread：**只读字节**)
 └─ asyncLoader_  (in-flight 合并 + 状态机)
       ↓ takeCompleted()
   pump(maxTextureCompletions, maxMeshVertices)   ← **主线程里 decodeRgba8（4K 解码全在这里）**
       ↓
   Texture/资源构造 + 注册（必须在主线程）
```

- `LoaderWorker` 自己维护 `queue_` / `completed_` / 条件变量，与 `JobSystem` 完全独立。
- `LoadCompletion` 携带原始 `payload` 字节，`AssetDatabase::pump` 里才解码（`asset_database.cpp:247`）。
- 这是 98.4 s 的来源：单线程 worker 读完 72 张 4K PNG 后，主线程逐张解码、上传。

## 3. 目标结构（改造后）

```
AssetDatabase
 ├─ fileAccess_
 ├─ loaderWorker_  (JobSystem 后端：读字节 + decodeRgba8 + 下采样，全部在 worker)
 └─ asyncLoader_   (状态机不变)
       ↓ takeCompleted()（completion 已带 RGBA）
   pump()  ← 只安装：构造 Texture、写 width/height/rgba8、注册 cache_
```

铁律不变：**worker 只产出 POD 字节；`Resource`/`Texture` 的构造与注册只在主线程**（`AGENTS.md` §3）。

## 4. 分步执行

### 步骤 0（AI）：`on_main_thread` 审计基础设施 ✅ 2026-11-19

- 新增 `engine/core/include/yr/core/thread_check.h`：
  `markMainThread()` / `isMainThread()` / `mainThreadMarked()` / `YR_ASSERT_MAIN_THREAD()`（基于现有 `YR_ASSERT`）；
- 在 `AssetDatabase` 的 8 个主线程入口插断言：`store` / `loadGltfScene` / `requestLoad` / `pump` / `pumpMeshes` / `evict` / `collectGarbage` / `clear`；
- `Engine::Engine()` 调用 `markMainThread()`（宿主在主线程构造 Engine）；
- 测试：`tests/core/test_thread_check.cpp` 5 条（未标记 / 标记 / reset / 幂等 / 宏在 worker 上触发）；
- 关键取舍：**未标记时宏 no-op**——单测与无宿主场景不被误伤；只有 Engine 标记后才真正强制执行。

### 步骤 1（用户）：`LoaderWorker` 改为 `JobSystem` 后端

**已准备的脚手架（工作区未提交，等实现通过后一起提交）**：

- `LoaderWorker(IFileAccess&, yr::job::JobSystem&, std::size_t maxInFlight = 4)`——签名已改，旧线程实现保留，作为红灯期的可编译 stub；
- `AssetDatabase` 两个构造函数增加可选 `yr::job::JobSystem* jobs = nullptr`：为空时自持一个默认池（现有调用点无需改动），注入时要求 `JobSystem` 活得更久；
- `LoadCompletion` 增加过渡字段 `DecodedTexture decoded`（`payload` 暂留，迁移完成后删除）；
- `engine/asset/CMakeLists.txt`：`yr_asset` 增加 `yr::job` 公共依赖（`check_deps` 允许 asset → job）；
- 现有 `test_loader_worker.cpp` 的 8 处构造改为显式传入共享 `JobSystem`。

**你要实现的部分**：

- 删除自有 `std::thread` 与 `run()` 的等待循环；
- `submit(LoadRequest)`：入 `pending_`，在 `maxInFlight_` 预算内 `jobs_->submit(...)`；
- 任务体：`readFile → decodeRgba8（kind == kTexture）→ 锁内 push completion`；
- `takeCompleted(max)`：返回后释放额度、继续提升 `pending_`；
- 保持 `AsyncLoader` 状态机与 `pump` 轮询语义不变。

### 步骤 2（用户）：`LoadCompletion` 携带解码结果

- `LoadCompletion` 增加 `DecodedTexture decoded`（`width/height/rgba8`）与错误信息；不再需要 `payload` 与 `maxDimension`（下采样在 worker 完成）；
- `AssetDatabase::pump` 删除 `decodeRgba8` 调用，直接安装 `decoded`；
- 同步路径（`loadGltfScene` 非 defer）保持原样——它本来就是同步加载。

### 步骤 3（用户）：背压与预算（本任务的核心设计点）

- **在飞任务上限**（如 8~64）与**在飞字节预算**（decoded 大小估算）；
- 4K 的危险：72 × 4096² × 4 B ≈ **4.8 GB**，绝不能"全部解码完再安装"；
- 完成队列也计入预算：`takeCompleted` 安装后释放；
- 提供参数入口（`AssetDatabase` 构造参数或 `--texture-budget-mb`），默认值写清楚。

### 步骤 4（AI）：测试

`tests/asset/test_async_decode.cpp`（已写好，当前 4 条红）：

1. **texture completion carries decoded pixels**：`LoaderWorker` 对 PNG 的 completion 直接带 `decoded.width/height/rgba8`（红：目前 `decoded` 为空）；
2. **bad texture reports decode error**：坏 PNG 在 worker 内报 error（红：目前无解码）；
3. **in-flight reads are capped**：16 个 10ms 请求，并发读 `>= 2` 且 `<= 4`（红：目前单线程 `maxActive == 1`）；
4. **zero-worker JobSystem still completes**：`JobSystem(0)` 内联路径带回像素（红：同 1）；
5. **pump installs worker-decoded pixels**：`AssetDatabase` 端到端像素一致（绿，迁移后必须保持）。

### 步骤 5（AI + 用户）：Sponza 收口与数据

- `render_demo --gltf <sponza> --asset-root <path>`（**带纹理**）实测：
  - 4K 端到端时间（目标 ≤ 25 s，8 worker；达不到就分析瓶颈）；
  - 1K 快速路径（`maxDimension=1024`）≤ 5 s；
  - shadow 开关下的帧时间对比；
- 截图 `doc/media/m9.5-sponza-textured.png`；报告 `doc/m9-5d-0-async-decode-report.md`（格式对照 `m9-2b-streaming-report.md`）；
- 慢 IO 帧时间数据（E10）写进报告。

## 5. 文件所有权

**用户手写（并发桥接与预算核心）：**

```text
engine/asset/src/loader_worker.h / loader_worker.cpp
engine/asset/src/asset_database.cpp（pump 安装部分）
engine/asset/include/yr/asset/async_loader.h（LoadCompletion 字段）
```

**AI 负责：**

```text
engine/core/include/yr/core/thread_check.h（步骤 0 基础设施 + 测试）
tests/asset/**、tests/core/**
CMake、doc/**、benchmark、报告
```

> 红灯规则不变：AI 的失败测试与 stub 留在工作区，实现通过后一起提交。

## 6. 验收标准

- [x] `ThrottledFileAccess`（50 ms/次）× 20 资源：pump p95 **0.00 ms / max 0.01 ms**；1 worker 1002 ms、4 worker 256 ms 全部就绪（`YrBench_asset_async`）；
- [x] 同一资源并发请求 10 次只加载一次（现有 in-flight 合并测试保持绿）；
- [ ] Sponza 4K 带纹理端到端 ≤ 25 s；1K 快速路径 ≤ 5 s（**待外部资产实测**，命令与记录项见 `doc/m9-5d-0-async-decode-report.md` §5）；
- [x] 在飞字节预算生效：完成队列 256 MiB + 在飞 4 × 64 MiB，峰值 ≈ 512 MiB（报告 §3）；
- [x] `JobSystem(0)` / `--jobs 0` 路径全部测试通过；
- [x] debug 568/568、TSan 561/561、ASan（asset 107/107）全绿；
- [x] `check_format.sh`、`check_deps.py` 通过；
- [x] `engine/`（除 `render/vulkan/`）仍零 Vulkan include。

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| 4K 解码结果爆内存（≈4.8 GB） | 在飞字节预算 + 完成队列预算；默认值保守，参数可调 |
| worker 误构造 `Resource` / 注册 cache | 步骤 0 的 `YR_ASSERT_MAIN_THREAD()` + 代码评审 |
| `JobSystem` 生命周期短于 `AssetDatabase` | `Engine` 持有 `JobSystem`，声明顺序在 `AssetDatabase` 之前；析构顺序写进 `doc/layers/job.md` |
| `LoaderWorker` 删线程后 `takeCompleted` 语义变化 | 保持"轮询取完成项"不变，只把生产端换成 JobSystem |
| 停机时还有任务在跑 | `~JobSystem()` 先 `waitAll()`；`AssetDatabase` 析构前 `asyncLoader_->reset()` + 拒绝新请求 |
| Sponza 纹理不是 4K？先确认 | 用 `identify`/文件头统计实际尺寸，写进报告 |

## 8. 明确不做

- BCn / KTX2 等压缩纹理、GPU 直接上传路径；
- IBL / skybox / spot light；
- 资源 LRU 缓存策略调整（现状 fixpoint GC 不变）；
- 把 glTF **结构解析**搬上 worker（`loadGltfScene` 仍主线程同步解析）；
- work stealing（S13）。

## 9. Q&A / 决策记录

- 2026-11-19：解码搬到 worker 而不是"只并行读文件"——98.4 s 的主体是解码（PNG → RGBA），只并行 IO 收益很小。
- 2026-11-19：completion 携带解码结果（POD 字节），`Resource` 构造仍留 `pump()`，维持 §6 铁律。
- 2026-11-19：`LoaderWorker` 保留为 JobSystem 之上的门面（API 不变），`AsyncLoader` 状态机不动，减少一次性改动面。
- 2026-11-19：先加 `on_main_thread` 断言再改并发代码——这是 M10 收尾时明确推迟的审计项，现在有了真实消费者。
- 2026-11-19：步骤 0 实现细节：`std::atomic<std::thread::id>`（acquire/release）；标记未设置时 `YR_ASSERT_MAIN_THREAD()` **不做检查**（单测/无宿主不误伤）；`markMainThread()` 幂等，换线程重复标记时 Debug 断言；`resetMainThreadForTesting()` 仅供测试。验证：561/561（debug / ASan / TSan）。
- 2026-11-19：预算采用方案 A（只限完成队列字节），默认 `maxInFlight = 4` + `byteBudget = 256 MiB`（0 = 不限制）；结构上界 ≈ 512 MiB（队列 256 + 在飞 4×64）。调紧方式：`maxInFlight = 2` + 预算 128 MiB。
- 2026-11-19：E10 实测（`YrBench_asset_async`）：pump p95 0.00 ms、max 0.01 ms；1 worker 1002 ms / 4 worker 256 ms 就绪 20 张。
- 2026-11-19：`LoadCompletion::maxDimension` 删除（解码已在 worker 完成，无消费者）；`payload` 保留给非纹理（`kUnknown/kMesh`）原始字节，纹理路径解码后立即释放容量。
- 2026-11-19：`test_loader_worker` 两条"原始字节透传"用例的 kind 由 `kTexture` 改为 `kUnknown`——纹理路径现在会解码，旧断言编码的是迁移前契约。
- 2026-11-19：未做/待办：Sponza 4K 实测（需外部资产）；`LoaderWorker` → `JobLoadBackend` 改名（待用户确认后单独机械 commit）。

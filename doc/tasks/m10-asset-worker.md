# 资产层 worker 化（M10 最后一项 + M9.5d-0）

> 状态：规划中（M10 已收口；本任务开始前先读 `doc/layers/job.md` 与 `m9-2b-streaming-report.md`）
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

### 步骤 0（AI）：`on_main_thread` 审计基础设施

- 新增 `engine/core/include/yr/core/thread_check.h`：
  `markMainThread()` / `isMainThread()` / `YR_ASSERT_MAIN_THREAD()`（基于现有 `YR_ASSERT`）；
- 在 `AssetDatabase::store`、`pump` 里的 `Texture` 构造、glTF 安装点插入断言；
- 测试：`tests/core/test_thread_check.cpp`（主线程 true、子线程 false、未标记时行为）；
- 这是 asset worker 迁移的前置门禁，先加断言再动并发代码。

### 步骤 1（用户）：`LoaderWorker` 改为 `JobSystem` 后端

- 构造：`LoaderWorker(IFileAccess&, JobSystem&)`，**删除自有 `std::thread` 与它的 `mutex_/cv_` 等待循环**；
- `submit(LoadRequest)`：入 `pending_`，在 in-flight 预算内 `jobs.submit(...)`；任务体：
  `readFile → decodeRgba8 → 锁内 push completion`；
- `takeCompleted(max)`：语义不变（pump 轮询），返回后释放额度、继续提升 `pending_`；
- 删除 `LoaderWorker` 的线程，`AssetDatabase` 的析构顺序变成：`asyncLoader_` → `loaderWorker_` → `fileAccess_`（JobSystem 由 `Engine` 拥有，必须比 `AssetDatabase` 活得久）。

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

`tests/asset/test_async_decode.cpp`：

1. worker 解码：`MemoryFileAccess` + 小 PNG → completion 直接带 RGBA，`pump` 安装后像素与同步路径一致；
2. in-flight 上限：慢 `IFileAccess` + N 个请求，并发任务数不超过 cap；
3. 失败路径：文件缺失 / 坏 PNG → `kFailed` + error，不崩溃，后续请求可继续；
4. `--jobs 0`：`JobSystem(0)` 内联路径仍然正确（decode 在调用线程完成）；
5. 取消/evict：加载中 evict → 完成后不安装、不泄漏；
6. TSan 下全绿（`./tools/ctest_tsan.sh`）。

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

- [ ] `ThrottledFileAccess`（50 ms/次）× 20 资源：主循环帧 p95 < 5 ms，全部资源约 1 s 内就绪；
- [ ] 同一资源并发请求 10 次只加载一次（现有 in-flight 合并测试保持绿）；
- [ ] Sponza 4K 带纹理端到端 ≤ 25 s；1K 快速路径 ≤ 5 s（记录实测值与瓶颈）；
- [ ] 在飞字节预算生效：最坏情况下解码结果驻留内存有上界（报告里给数字）；
- [ ] `JobSystem(0)` / `--jobs 0` 路径全部测试通过；
- [ ] debug / ASan / TSan 全量绿（`556 + 新增` 条）；
- [ ] `check_format.sh`、`check_deps.py` 通过；
- [ ] `engine/`（除 `render/vulkan/`）仍零 Vulkan include。

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
# M9.5d-0 / M10 资产 worker 迁移报告（异步解码）

> 状态：主体完成（Sponza 4K 待外部资产实测） · 2026-11-19
> 任务卡：`doc/tasks/m10-asset-worker.md` · 相关：`doc/layers/job.md`、`doc/m9-2b-streaming-report.md`

## 1. 改了什么

```
改造前：worker(自有线程) 读字节 → completed_ → 主线程 pump 里 decodeRgba8 → Texture
改造后：JobSystem worker 读字节 + decodeRgba8 + 下采样 → completed_ → 主线程 pump 只安装
```

- **生产端**：`JobLoadBackend` 不再拥有线程，变成 `JobSystem` 之上的门面（`submit` → `jobs_->submit`）；
- **解码**：纹理在 worker 内完成（`decodeRgba8`，含 `maxDimension` 下采样），4K PNG 不再占主线程；
- **主线程**：`pump` 删除 `decodeRgba8`，只做 `Texture` 构造 / `markReady` / 注册 `cache_`（主线程铁律不变）；
- **背压**：在飞任务上限 `maxInFlight = 4` + 完成队列字节预算 `byteBudget = 256 MiB`（0 = 不限制）。

## 2. E10：慢 IO 下的主线程帧开销

场景：`ThrottledFileAccess(50 ms)` × 20 个纹理请求，主线程循环 `pump(1)`（间隔 1 ms 模拟帧）。

| workers | 全部就绪 | 总耗时 | pump p50 / p95 / max |
|---|---|---|---|
| 1 | 20/20 | 1002 ms | 0.00 / **0.00** / 0.01 ms |
| 4 | 20/20 | **256 ms** | 0.00 / **0.00** / 0.01 ms |

- 验收目标 p95 < 5 ms：**远低于目标**（解码与 IO 都不在主线程）；
- 1 worker 时总耗时 ≈ 20 × 50 ms（串行 IO）；4 worker 时 ≈ 5 × 50 ms；
- 复现：`cmake --preset benchmark && cmake --build --preset benchmark --target YrBench_asset_async`，
  然后 `./build/benchmark/benchmarks/YrBench_asset_async`。

## 3. 内存上界

4K RGBA8 单张 ≈ 64 MiB（4096² × 4）。

```
completed_ 驻留 ≤ byteBudget_ = 256 MiB
在飞解码     ≤ maxInFlight_ × 64 MiB = 256 MiB
峰值         ≈ 512 MiB（默认参数）
```

- 调紧：`maxInFlight = 2` + `byteBudget = 128 MiB` → 峰值 ≈ 256 MiB；
- 预算只统计"解码完在队列里等主线程取"的字节；`payload`（非纹理原始字节）也计入。

## 4. 测试与验证

| 检查 | 结果 |
|---|---|
| `YrTests_asset` | 107 cases / 1070 assertions |
| `[async_decode]` | 7 cases（像素随 completion、坏图 worker 报错、in-flight 2..4、0-worker、pump 像素一致、预算停滞、预算 0 不限） |
| 全量 ctest（debug） | 568/568 |
| 全量 TSan（`tools/ctest_tsan.sh`） | 561/561 |
| ASan（asset） | 107 cases 干净 |
| `check_format` / `check_deps` | 通过 |
| `--jobs 0` | 全部用例通过（内联路径同步加载） |

## 5. Sponza 4K 实测（2026-11-19 完成）

完整数据见 [`m9-5d-0-sponza-benchmark.md`](m9-5d-0-sponza-benchmark.md)（环境、方法、trace 口径都在里面）。摘要：

| 场景（72 张 4K 源，输出 1024²） | 首帧 | 全部就绪 | RSS 峰值 | pump p95 |
|---|---|---|---|---|
| 同步（M9.5 行为） | 35.2 s | 35.2 s | 2.07 GB | — |
| 异步 worker | **0.23 s** | **10.2 s** | 1.69 GB | 6.3 µs |
| 异步 + 预生成 1K 缓存（1K 源） | 0.22 s | **3.8 s** | 0.97 GB | 4.5 µs |
| 异步 + 4K 输出 | 0.24 s | 11.6 s | 6.7 GB | 7.2 µs |

- 首帧从 35.2 s 降到 0.23 s，`pump p95 ≤ 0.02 ms`（目标 < 5 ms）；
- 4096² 输出常驻 ≈ 4.6 GiB，默认 1024 是合理取舍；
- 截图：`doc/media/m9.5-sponza-textured.png`（异步 1K 缓存 + `--no-shadow`；无 IBL，室内偏暗）。

## 6. 已知限制

- `stb_image` 的 `stbi_failure_reason()` 使用全局缓冲区（未定义 `STBI_THREAD_LOCAL`）；多个 worker 同时解码**失败**时可能竞争该文本。只影响错误信息，修复方式是给 `stb_image_impl.cpp` 加 `#define STBI_THREAD_LOCAL thread_local`。
- `LoadCompletion::payload` 保留给非纹理（`kUnknown/kMesh`）原始字节；纹理路径解码后立即释放。
- 3 条旧的 job load backend 用例把 `kTexture` 改成 `kUnknown`：它们测的是"原始字节透传"，纹理路径现在会解码。
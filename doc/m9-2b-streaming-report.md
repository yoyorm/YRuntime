# M9.2b Mesh Streaming 数据报告

日期：2026-11-13
代码状态：HEAD `85de3de`，b-1 / b-2 / b-3 改动未提交
构建：`release-vk`（性能数据）、`debug-vk` + `asan`（正确性）

---

## 0. 结论摘要

1. **Sponza 的 mesh streaming 达到预期目标**：
   - 首帧 `tick` 从 **156.6ms → 19.5ms**（-88%）；
   - 首帧上传量从 **110.5MB → 2.0MB**（-98%），单帧最大上传从 **110.5MB → 3.4MB**；
   - 首帧可见时间从 **347.3ms → 135.2ms**（-61%）；
   - `pump` 在 streaming 窗口内 p95 **2.6ms**、max **7.7ms**；
   - 稳态帧时间与同步模式一致；
   - 最终 405 items / 3,747,018 triangles，`validation_errors=0`、`missing_assets=0`。

2. **代价是完整加载时间变长**：
   - Sponza 从首帧即全部可见（347ms）变为逐帧出现，完全 ready 需要 **1441ms**（+315%）；
   - 峰值 RSS 从 **583MB → 611MB**（+4.8%），主要因为 `GltfSourceData` 在 streaming 期间驻留更久。

3. **ToyCar 不适合 streaming**：
   - 场景只有 3 个 primitive；
   - `--stream-assets` 首帧 1 item、完全 ready 约 178ms，而同步模式 88ms 就全部可见；
   - 小场景继续默认同步。

4. **正确性没有回归**：
   - debug-vk 472/472，ASan 472/472；
   - ToyCar 默认仍是 3 items / 108,936 triangles / validation 0；
   - Sponza sync 与 stream 的最终 items、triangles、missing、validation 完全一致；
   - dump/replay 仍正常。

5. **一个值得注意的预算现象**：
   - 预算是按顶点数，不是按 job 数；
   - Sponza 最后一帧一次性安装了 45 个较小的 primitive，`pump` 达到 7.7ms；
   - 仍然远低于同步首帧，但如果要更平滑，可以增加“每帧最多 N 个 job”的限制。

---

## 1. 环境与测试方法

### 1.1 环境

```text
CPU        Intel GenuineIntel 0000 @ 3.00GHz，6 核 12 线程
GPU        NVIDIA GeForce RTX 5060，驱动 615.71.09，8151 MiB
Vulkan     Instance 1.4.313
显示       Xephyr :99，1280x720x24
构建       Release（release-vk），assert 关闭，日志开启
```

说明：

- 原生 `:0` 在当前环境中会触发已知的 GLFW `XCheckTypedWindowEvent` 卡死，因此所有数据在 Xephyr 嵌套 X server 中采集。
- Xephyr 的 `present` 存在约 **40ms** 的下限，绝对帧时间被显著抬高；因此本报告主要看：
  - `scene_load_ms`
  - `pump_ms`
  - `extract_ms`
  - `upload_bytes`
  - 相对差异
- 不把绝对 `p50` 当作引擎在原生窗口下的最终性能。

### 1.2 场景

**ToyCar**（仓库内）：

```text
nodes      11
primitives 3
triangles  108,936
textures   5
```

**Sponza Main**（外部路径，不进仓库）：

```text
path        /home/yoyorm/Storage/Models/main1_sponza
gltf        res://NewSponza_Main_glTF_003.gltf
nodes       155
primitives  405
vertices    2,049,137
triangles   3,747,018
bin         139,973,456 B（约 140MB）
textures    72 张 4096x4096（本次全部跳过）
```

Sponza 纹理解码后约 4.6GB，M9.2b 只测几何与 mesh streaming，统一加 `--skip-textures`。

### 1.3 配置矩阵

| ID | 场景 | 关键参数 | 目的 |
|---|---|---|---|
| T0 | ToyCar | `--frames 60` | 同步基线 |
| T1 | ToyCar | `--frames 60 --async-textures` | M9.2a 纹理异步 |
| T2 | ToyCar | `--frames 60 --stream-assets` | 纹理 + mesh 异步 |
| S0 | Sponza | `--skip-textures --frames 60` | 同步几何基线 |
| S1 | Sponza | `--skip-textures --stream-assets --frames 240` | mesh streaming |

### 1.4 采集与统计

- 每个配置先跑 1 次 warm-up，结果丢弃；
- 再跑 5 次测量，报告中取中位数，并在括号中给出 min..max；
- 另外跑 1 次带 `--trace` 的运行，用于逐帧曲线；trace 运行不参与 5 次计时中位数；
- `Maximum resident set size` 来自 `/usr/bin/time -v`；
- trace 字段：

```text
frame,wall_ms,tick_ms,pump_ms,extract_ms,present_ms,gc_ms,items,uploads,upload_bytes,missing,mesh_pending,texture_pending,resident,cpu_bytes,rss_kb
```

### 1.5 局限

- Xephyr 的 `present` 约 40ms 下限，绝对帧时间不代表原生窗口；
- 无 GPU timestamp，`present` 混合了上传、绘制、呈现和等待；
- 所有测量都是 warm page cache，未做 drop_caches；
- 未测 Sponza 纹理（4K，M9.5 范围）；
- glTF parse 仍同步，stream 只延迟 mesh 构建与上传。

---

## 2. 正确性

| 配置 | 最终 items | triangles | missing | failed_loads | validation | 结束 resident / cpu_bytes |
|---|---:|---:|---:|---:|---:|---:|
| ToyCar sync | 3 | 108,936 | 0 | 0 | 0 | 0 / 0 |
| ToyCar async-textures | 3 | 108,936 | 0 | 0 | 0 | 0 / 0 |
| ToyCar stream-assets | 3 | 108,936 | 0 | 0 | 0 | 0 / 0 |
| Sponza sync | 405 | 3,747,018 | 0 | 0 | 0 | 0 / 0 |
| Sponza stream | 405 | 3,747,018 | 0 | 0 | 0 | 0 / 0 |

结论：

- streaming 没有改变最终画面内容；
- `missing_assets` 全程为 0，说明 RenderExtract 正确跳过了尚未 ready 的 mesh；
- 场景销毁后 `resident=0`、`cpu_bytes=0`，没有资源泄漏。

测试门禁：

```text
debug-vk ctest   472/472
ASan ctest       472/472
check_format     通过
check_deps       通过
git diff --check 通过
```

---

## 3. ToyCar 结果

### 3.1 汇总（5 次中位数）

| 配置 | load_ms | first_item_ms | ready_ms | total p50 | total p95 | pump p95 | peak RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| sync | 59.1 | 88.0 | 88.0 | 18.8ms | 19.8ms | ~0 | 210.7MB |
| async-textures | 5.5 | 42.8 | 177.6 | 18.8ms | 25.6ms | 6.9ms | 196.3MB |
| stream-assets | 3.2 | 42.2 | 178.3 | 18.9ms | 25.7ms | 7.3ms | 196.3MB |

### 3.2 观察

- 同步模式在 load 阶段一次性建好 3 个 mesh，`ready_ms=88ms`。
- streaming 把 load 降到 3ms，但纹理解码 + mesh 构建分摊到前 6～7 帧：
  - 首 item 在 42ms 左右出现；
  - 完全 ready 反而比同步晚约 90ms。
- `p95` 从 19.8ms 升到 25.7ms，主要来自前几帧的纹理解码和 mesh 构建。
- ToyCar 太小，streaming 的收益不足以抵消额外复杂度；**继续默认同步**。

---

## 4. Sponza 结果

### 4.1 启动与首帧

| 指标 | sync | stream | 变化 |
|---|---:|---:|---:|
| `scene_load_ms` | 186.5 | 115.1 | -38% |
| 首帧 `tick_ms` | 156.6 | 19.5 | -88% |
| 首帧 `extract_ms` | 41.5 | 0.7 | -98% |
| 首帧 `present_ms` | 114.9 | 18.1 | -84% |
| 首帧 `upload_bytes` | 110.5MB | 2.0MB | -98% |
| `first_item_ms` | 347.3 | 135.2 | -61% |
| `ready_ms` | 347.3 | 1441.0 | +315% |
| 单帧最大上传 | 110.5MB | 3.41MB | -97% |

解释：

- **sync**：
  - `loadGltfScene` 同步构建 405 个 mesh，耗时约 186ms；
  - 首帧 `extract` 生成 405 个 `MeshUpload`（约 41.5ms）；
  - 首帧 `present` 上传约 110.5MB，耗时约 115ms。
- **stream**：
  - `loadGltfScene` 只建占位，耗时约 115ms；
  - 首帧只安装/上传少量 mesh，`tick=19.5ms`；
  - 之后约 40 帧内逐步完成 405 个 primitive。

### 4.2 Sponza streaming 时间线

| frame | items | mesh_pending | tick_ms | pump_ms | present_ms | upload MB | RSS MB |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 3 | 402 | 19.5 | 0.62 | 18.1 | 2.00 | 301 |
| 2 | 9 | 396 | 22.8 | 1.92 | 19.9 | 3.35 | 313 |
| 5 | 41 | 364 | 25.8 | 2.55 | 22.6 | 2.79 | 337 |
| 10 | 106 | 299 | 30.2 | 1.55 | 28.0 | 3.12 | 380 |
| 20 | 185 | 220 | 33.3 | 1.80 | 30.9 | 2.78 | 458 |
| 30 | 304 | 101 | 40.7 | 1.89 | 38.0 | 3.34 | 543 |
| 38 | 345 | 60 | 42.7 | 1.89 | 39.8 | 3.41 | 602 |
| 39 | 360 | 45 | 44.2 | 1.59 | 42.0 | 3.06 | 610 |
| 40 | 405 | 0 | 52.2 | 7.73 | 43.9 | 2.20 | 486 |

观察：

- items 单调增长，`mesh_pending` 稳定下降；
- 前 9 帧每帧约 5–16 个 uploads；
- 第 10 帧和第 30 帧出现较大的批次（31 / 25 个 uploads），因为小 primitive 的顶点数之和没有超过 64k 预算；
- 第 40 帧是最后一个批次：45 个 uploads、`pump=7.73ms`；
- 第 40 帧完成后 `GltfSourceData` 释放，RSS 从 610MB 降到 486MB。

### 4.3 streaming 窗口与稳态

| 窗口 | 指标 | sync | stream |
|---|---|---:|---:|
| streaming | `tick` p50 / p95 / max | 156.6 / 156.6 / 156.6 | 33.3 / 42.7 / 52.2ms |
| streaming | `pump` p50 / p95 / max | ~0 | 1.73 / 2.62 / 7.73ms |
| streaming | `present` p50 / p95 / max | 114.9 / 114.9 / 114.9 | 30.7 / 39.8 / 43.9ms |
| steady | `tick` p50 / p95 / max | 40.0 / 41.2 / 42.8ms | 40.0 / 41.4 / 43.1ms |
| steady | `pump` p50 / p95 / max | ~0 | 0.0017 / 0.0024 / 0.0036ms |
| steady | `present` p50 / p95 | 39.7 / 41.0ms | 39.8 / 41.1ms |

说明：

- sync 的 streaming 窗口只有第 1 帧；
- stream 的 streaming 窗口为第 1～40 帧；
- steady 窗口从 ready + 5 帧开始；
- Xephyr 的 present 下限约 40ms，所以 stream 的 `tick p95=42.7ms` 中，资产相关部分只有约 2.7ms；
- 首帧 `present=18.1ms` 低于稳态下限，属于 Xephyr 的调度波动；首帧结论以 `tick`、`pump`、`upload_bytes` 为准；
- `pump` 在 streaming 窗口 p95 2.6ms、max 7.7ms，符合“单帧预算”的目标。

### 4.4 上传

| 指标 | sync | stream | 变化 |
|---|---:|---:|---:|
| 总上传量 | 110.5MB | 110.5MB | 相同 |
| 单帧最大上传 | 110.5MB | 3.41MB | -97% |
| 首帧上传 | 110.5MB | 2.00MB | -98% |

总上传量相同，说明 streaming 只是把同一批数据分摊到多帧，没有额外拷贝；
首帧尖峰从 110.5MB 降到 2MB，是本次改动最直接的效果。

### 4.5 内存

| 指标 | sync | stream | 变化 |
|---|---:|---:|---:|
| 峰值 RSS（中位数） | 583.1MB | 610.9MB | +4.8% |
| 完全 ready 后 RSS | 约 583MB | 约 486MB | -17% |
| 结束 `cpu_bytes` | 0 | 0 | 相同 |

解释：

- streaming 期间 `GltfSourceData`（约 140MB）保留到最后一个 mesh 安装完；
- 因此峰值比 sync 高约 28MB；
- 第 40 帧模型释放后，stream RSS 降到 486MB；
- sync 的 trace 在 loop 开始时已经释放模型，但 glibc 可能保留了部分 arena，RSS 停在 583MB；
- 因此“ready 后 RSS”受分配器行为影响，不宜作为精确资源统计；`AssetStats.cpu_bytes` 在两种模式下最终都为 0。

### 4.6 差异汇总

| 维度 | 结果 |
|---|---|
| 首帧可见 | stream 更快（135ms vs 347ms） |
| 首帧卡顿 | stream 显著更低（19.5ms vs 156.6ms） |
| 单帧上传 | stream 显著更低（3.4MB vs 110.5MB） |
| 完全 ready | sync 更快（347ms vs 1441ms） |
| 稳态帧时间 | 基本一致 |
| 峰值 RSS | stream 高约 4.8% |
| 正确性 | 完全一致 |

---

## 5. 分析

### 5.1 假设验证

| 假设 | 结果 |
|---|---|
| H1：启动停顿下降 | 成立。首帧 156.6ms → 19.5ms；load 186.5ms → 115.1ms |
| H2：单帧尖峰下降 | 成立。最大上传 110.5MB → 3.4MB；pump max 7.7ms |
| H3：渐进可见 | 成立。首 item 347ms → 135ms，items 逐帧增长 |
| H4：稳态不变 | 成立。steady p50 40.0ms vs 40.0ms（Xephyr 下限主导） |
| H5：正确性不变 | 成立。items / triangles / validation / missing 一致 |
| H6：内存可接受 | 成立。峰值 +4.8%，最终资源归零 |

### 5.2 尖峰归因

**sync 首帧**：

```text
tick 156.6ms
  extract 41.5ms   // 生成 405 个 MeshUpload
  present 114.9ms  // 上传约 110.5MB
```

**stream 首帧**：

```text
tick 19.5ms
  pump 0.62ms      // 少量 mesh 构建
  extract 0.70ms
  present 18.1ms   // 上传约 2.0MB
```

**stream 最后一帧（第 40 帧）**：

```text
tick 52.2ms
  pump 7.73ms      // 45 个小 primitive 同帧构建
  present 43.9ms   // 上传约 2.2MB
```

最后一帧的 `pump=7.73ms` 说明当前预算是“顶点数”而不是“job 数”：

- 大 primitive 会被预算自然限制；
- 小 primitive 可以在一帧内批量装入；
- 如果希望更平滑，可以增加 `maxMeshJobsPerPump`（例如每帧最多 8 个 job）。

这不是正确性问题，最终 items/validation 都正常。

### 5.3 代价

streaming 的代价是**完整加载时间变长**：

- Sponza 从 347ms 变成 1441ms 完全 ready；
- 但首 item 从 347ms 降到 135ms；
- 在 Xephyr 的 40ms 帧率下，1441ms 约等于 40 帧；如果原生 60fps，同样的 40 帧约 0.7s。

因此结论取决于目标：

- **编辑器/交互式加载**：streaming 更好，用户更早看到内容，主线程不长时间卡住；
- **离线/一次性加载**：同步可能更快完成，但会有一次明显停顿。

### 5.4 数据可信度

- 所有配置在同一 Xephyr 环境、同一 release 二进制下运行；
- 每个配置 5 次测量，中位数稳定；
- `load_ms`、`pump_ms`、`upload_bytes` 是本次结论的主要依据；
- 绝对 `present` 受 Xephyr 影响，不能直接外推到原生窗口；
- 未做冷缓存测试；首次冷读的 `load_ms` 会更高；
- 没有 GPU timestamp，不能区分上传与绘制时间。

---

## 6. 结论与下一步

### 结论

1. **M9.2b 的 mesh streaming 是有效的**：
   - 把 Sponza 的首帧资产卡顿从约 156ms 降到约 19.5ms；
   - 把单帧上传从 110.5MB 降到 3.4MB；
   - 首 item 提前约 212ms；
   - 最终画面与同步完全一致。
2. **它不是“总加载时间更短”的优化**：
   - 完全 ready 时间从 347ms 增加到 1441ms；
   - 峰值内存增加约 4.8%。
3. **小场景不需要 streaming**：
   - ToyCar 默认同步仍然是最佳选择；
   - streaming 保持 opt-in。
4. **预算可以继续调优**：
   - 当前 `maxMeshVertices=65536`，最后一帧一次 45 个 job；
   - 可选增加每帧 job 数上限，降低最后一帧的 7.7ms pump 尖峰。

### 下一步建议

1. 保持默认同步，`--stream-assets` 用于 Sponza / 大场景。
2. 如果后续原生窗口数据仍显示单帧尖峰，增加 `maxMeshJobsPerPump`。
3. 如果冷启动 `load_ms` 仍然明显，做 2b-4 的异步 scene parse。
4. M9.5 处理 Sponza 纹理前，先加 `maxTextureSize`，否则 4K 纹理不可用。

---

## 7. 原始数据

采集目录（未入库）：

```text
/tmp/opencode/m9_2b/
  environment.txt
  toy_sync_*.log / *.time / *_trace.csv
  toy_tex_*.log / *.time / *_trace.csv
  toy_stream_*.log / *.time / *_trace.csv
  sponza_sync_*.log / *.time / *_trace.csv
  sponza_stream_*.log / *.time / *_trace.csv
  analysis.json
```

复现命令示例：

```bash
cmake --build --preset release-vk
BIN=./build/release-vk/bin/yr_render_demo
OUT=/tmp/opencode/m9_2b
export DISPLAY=:99

# ToyCar
"$BIN" --frames 60 --trace "$OUT/toy_sync_trace.csv"

# Sponza sync
"$BIN" \
  --asset-root /home/yoyorm/Storage/Models/main1_sponza \
  --gltf res://NewSponza_Main_glTF_003.gltf \
  --skip-textures --frames 60 \
  --trace "$OUT/sponza_sync_trace.csv"

# Sponza stream
"$BIN" \
  --asset-root /home/yoyorm/Storage/Models/main1_sponza \
  --gltf res://NewSponza_Main_glTF_003.gltf \
  --skip-textures --stream-assets --frames 240 \
  --trace "$OUT/sponza_stream_trace.csv"
```
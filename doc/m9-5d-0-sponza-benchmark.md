# M9.5d-0 · Sponza 异步纹理基准（2026-11-19）

> 目的：验证"纹理解码搬到 JobSystem worker"在真实压力场景（Sponza 72 张 4K PNG）下的收益与边界。
> 复现命令见 §7；原始 trace CSV 由 `--trace` 生成，不随仓库提交。

## 1. 环境

| 项 | 值 |
|---|---|
| CPU | Intel `Genuine Intel(R) CPU 0000 @ 3.00GHz`，12 线程 |
| GPU | NVIDIA GeForce RTX 5060（Discrete，Vulkan 1.4.351，驱动 258.066.080.0） |
| 软件 Vulkan | llvmpipe（被设备选择逻辑拒绝：CPU 设备） |
| 显示 | Xephyr `:99` 1280×720×24（软件合成；`present` 走 X11 复制） |
| 构建 | `release-vk`（`CMAKE_BUILD_TYPE=Release`），g++ 13.3.0，CMake 3.28.3 |
| 资产 | `/home/yoyorm/Storage/Models/main1_sponza`（6.3 GB） |

Sponza 规模（glTF 结构统计）：**155 nodes / 405 primitives / 3,747,018 triangles / 72 images（全部 4096²）/ 28 materials**，bin 139,973,456 B。

> ⚠ 帧时间口径：`present p50 ≈ frame total p50`（≈65 ms），说明瓶颈是 Xephyr 的软件合成/复制，不是 GPU 计算。**绝对帧时间不能代表真实 GPU 性能**，本报告只用它做同一环境内的相对比较（extract 仅 ~0.17 ms，pump ≤ 0.02 ms）。

## 2. 方法

- 运行 `yr_render_demo`，固定室内相机：`--camera -8.807,1.592,-0.858 --target -3.004,2.762,0.237`；
- 指标来源：
  - `scene_load_ms`：glTF 结构解析 + 同步资源路径耗时（异步模式下不含纹理解码）；
  - `first_item_ms`：第一帧出现 `items > 0` 的时间；
  - `ready_ms`：所有 deferred mesh 完成且所有纹理不再 `kLoading` 的时间（含 `kFailed` 视为已结束）；
  - `pump p50/p95`、`extract p50`、`present p50`：`FrameStats` 最后 N 帧窗口；
  - RSS：`/usr/bin/time -v` 的 `Maximum resident set size` + trace CSV 每帧 `rss_kb` 峰值；
  - `validation_errors` 来自 Vulkan validation layer。
- 加载参数：异步路径默认 `maxInFlight=4`、`byteBudget=256 MiB`；`--texture-max-dim` 控制解码后最大边长（默认 1024，`0` = 不限制）。

## 3. 几何基线（跳过纹理）

| 场景 | scene_load | first_item | ready | 帧 p50 / p95 | present p50 | RSS 峰值 | 帧数 / 墙钟 |
|---|---|---|---|---|---|---|---|
| shadow on | 266.7 ms | 447.3 ms | 447.3 ms | **65.0 / 65.9 ms** | 64.8 ms | 602 MB | 300 / 20.2 s |
| shadow off | 189.4 ms | 343.0 ms | 343.0 ms | **41.2 / 41.8 ms** | 40.9 ms | 601 MB | 300 / 13.0 s |

- 对比 M9.5 记录（`--skip-textures`）一致：405 items / 3,747,018 triangles / `validation_errors=0`；
- 阴影在软件合成环境下的增量约 **+23.8 ms/帧**（65.0 − 41.2），与 GPU 真实比例无关，仅说明 shadow pass 成本显著。

## 4. 纹理加载路径（源：72 张 4096² PNG）

### 4.1 同步加载 vs 异步 worker 解码（解码后 1024²）

| 路径 | scene_load | 首帧 first_item | 全部就绪 ready | 帧 p50 / p95 | RSS 峰值 | 墙钟 |
|---|---|---|---|---|---|---|
| 同步（M9.5 行为） | **34,657.6 ms** | 35,244.7 ms | 35,244.7 ms | 65.4 / 66.1 ms | 2,072 MB | 30 帧 / 37.5 s |
| 异步 worker（本次） | 194.8 ms | **229.4 ms** | **10,237.1 ms** | 64.9 / 68.9 ms | 1,694 MB（trace 1,637 MB） | 900 帧 / 52.9 s |

- **首帧提前 ≈ 35.2 s → 0.23 s**（主线程不再被解码阻塞）；
- **全部纹理就绪 35.2 s → 10.2 s**（12 线程并行解码；单张 4K PNG 解码仍受 CPU 限制）；
- **RSS 下降 ≈ 378 MB**：worker 解完立即产出 1024² RGBA，完成队列有 256 MiB 上限；
- `pump p95 = 6.3 µs`——主线程只做安装，验收目标（p95 < 5 ms）**远超**。

> 与迁移前报告中的 "98.4 s" 差异来自构建类型：98.4 s 是 `debug-vk` 的同步解码；本表为 Release，同一路径 34.7 s。

### 4.2 解码目标尺寸的影响（异步路径）

| `--texture-max-dim` | ready | 帧 p50 / p95（shadow on） | RSS 峰值（time / trace） |
|---|---|---|---|
| 256 | 9,847 ms | 67.8 / 69.7 ms | 1,622 / 1,572 MB |
| 1024（默认） | 10,237 ms | 64.9 / 68.9 ms | 1,694 / 1,637 MB |
| 4096（原生） | 11,612 ms | 68.8 / 71.3 ms | **6,686 / 5,953 MB** |

- 三个档位的 ready 时间接近（9.8~11.6 s）：**瓶颈是"解码 4K PNG"本身**，下采样只影响输出内存与采样带宽；
- 4096² 输出常驻约 **72 × 64 MiB ≈ 4.6 GiB**，实测 RSS 峰值 6.7 GB——默认 1024 是合理的；
- 4K 输出在 shadow on 下帧时间 +3.9 ms，no-shadow 下 +15 ms（§5），说明大纹理主要吃采样带宽。

## 5. 1K 预生成缓存（真正的"快速路径"）

把 72 张 4096² PNG 预缩放为 1024²（`convert -resize 1024x1024>`，8 路并行 **19 s**，输出 **118 MB**），glTF/KHR 结构不变，只替换纹理文件。

| 路径 | scene_load | 首帧 | ready | 帧 p50 / p95 | RSS 峰值 |
|---|---|---|---|---|---|
| 同步（1K 源） | 1,934 ms | 2,615 ms | 2,615 ms | 94.8 / 102.4 ms* | 2,072 MB |
| 异步 worker（1K 源） | 197 ms | 223 ms | **3,760 ms** | 68.6 / 69.9 ms | 973 MB（trace 927 MB） |

\* 同步 1K 的 30 帧样本包含首帧上传/预热，p50 偏高；异步 600 帧窗口已稳定。

- 相对 4K 源：**解码时间 10.2 s → 3.8 s（−63%）**，RSS **1.69 GB → 0.97 GB（−43%）**；
- 结论：**预生成 1K 缓存是收益最大的一步**，worker 解码则解决"主线程被卡住"；两者互补。

## 6. 帧成本分解（no-shadow，300~600 帧窗口）

| 配置 | 帧 p50 | 相对几何基线 |
|---|---|---|
| 几何（skip-textures） | 41.2 ms | — |
| + 1K 纹理（异步） | 42.9 ms | **+1.7 ms** |
| + 4K 纹理（异步） | 57.8 ms | **+16.6 ms** |
| 几何 + shadow | 65.0 ms | +23.8 ms |
| + 1K 纹理 + shadow | 68.6 ms | +3.6 ms（相对 shadow 基线） |

- 4K 纹理的带宽代价在软件合成环境下被放大；真实 GPU 上应显著更小，但趋势（1K 更省）成立；
- `extract_render p50` 全程 0.15~0.18 ms，与纹理尺寸无关；
- 所有运行 `validation_errors=0`，`failed_loads=0`。

## 7. 复现

```bash
# 构建
cmake --preset release-vk && cmake --build --preset release-vk

# 启动虚拟 X（软件合成）
Xephyr :99 -screen 1280x720x24 -ac -br &

# 几何基线
DISPLAY=:99 ./build/release-vk/bin/yr_render_demo \
  --gltf res://NewSponza_Main_glTF_003.gltf --asset-root /home/yoyorm/Storage/Models/main1_sponza \
  --skip-textures --frames 300 --trace /tmp/sponza_geo.csv \
  --camera -8.807,1.592,-0.858 --target -3.004,2.762,0.237

# 同步纹理（34.7 s 阻塞）
DISPLAY=:99 ./build/release-vk/bin/yr_render_demo \
  --gltf res://NewSponza_Main_glTF_003.gltf --asset-root /home/yoyorm/Storage/Models/main1_sponza \
  --frames 30 --camera -8.807,1.592,-0.858 --target -3.004,2.762,0.237

# 异步 worker 解码（4K 源，1K 输出）
DISPLAY=:99 ./build/release-vk/bin/yr_render_demo \
  --gltf res://NewSponza_Main_glTF_003.gltf --asset-root /home/yoyorm/Storage/Models/main1_sponza \
  --async-textures --texture-max-dim 1024 --frames 900 --trace /tmp/sponza_async_1k.csv \
  --camera -8.807,1.592,-0.858 --target -3.004,2.762,0.237

# 1K 缓存（先生成一次）
mkdir -p /home/yoyorm/Storage/Models/sponza_1k/textures
cp /home/yoyorm/Storage/Models/main1_sponza/NewSponza_Main_glTF_003.gltf /home/yoyorm/Storage/Models/sponza_1k/
ln -sf ../main1_sponza/NewSponza_Main_glTF_003.bin /home/yoyorm/Storage/Models/sponza_1k/
python3 - <<'PY' | xargs -P 8 -I{} sh -c 'convert "/home/yoyorm/Storage/Models/main1_sponza/{}" -resize "1024x1024>" "/home/yoyorm/Storage/Models/sponza_1k/{}"'
import json
d=json.load(open('/home/yoyorm/Storage/Models/main1_sponza/NewSponza_Main_glTF_003.gltf'))
for im in d['images']: print(im['uri'])
PY

DISPLAY=:99 ./build/release-vk/bin/yr_render_demo \
  --gltf res://NewSponza_Main_glTF_003.gltf --asset-root /home/yoyorm/Storage/Models/sponza_1k \
  --async-textures --texture-max-dim 1024 --frames 600 --trace /tmp/sponza_async_1kcache.csv \
  --camera -8.807,1.592,-0.858 --target -3.004,2.762,0.237

# 截图（异步 1K 缓存 + no-shadow）
import -window root doc/media/m9.5-sponza-textured.png
```

## 8. 结论

1. **目标 1（慢 IO 不卡主线程）达成且超额**：`pump p95 ≤ 0.02 ms`（E10 独立基准 ≤ 0.01 ms，目标 < 5 ms）；
2. **目标 2（Sponza 4K 端到端）**：4K 源 + 1K 输出从 **35.2 s → 10.2 s**；预生成 1K 缓存再降到 **3.8 s**；首帧从 35 s 降到 0.23 s；
3. **目标 3（任务系统作为真实消费者）成立**：4 worker in-flight + 256 MiB 完成队列预算，RSS 从 2.07 GB 降到 1.69 GB（4K 输出时 6.7 GB 有界）；TSan 全量绿；
4. **默认参数建议**：`maxInFlight=4`、`byteBudget=256 MiB`、`textureMaxDim=1024`；4K 输出只在需要近距离特写时开启；
5. **后续**：室内布光/IBL 仍缺（当前截图偏暗，见 `doc/media/m9.5-sponza-textured.png`，`--no-shadow` 拍摄）；Sponza 纹理格式仍是 PNG，KTX2/BCn 是后续优化方向。

## 9. 已知限制

- Xephyr 软合成导致帧时间上限 ≈ 65 ms，绝对数值不代表真实 GPU；本报告只用相对比较；
- `stb_image` 的错误路径使用全局缓冲区（未定义 `STBI_THREAD_LOCAL`），多 worker 同时解码**失败**时错误文本可能互相覆盖（只影响诊断信息）；
- 1K 缓存是手工生成的实验产物，仓库不提交（118 MB 也不进 git）；
- 同步路径（默认，不加 `--async-textures`）仍会在主线程解码 34.7 s，适合单张资源/离线工具，不适合大场景启动。
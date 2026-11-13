# M9.5 PBR 数据报告

日期：2026-11-13
代码状态：HEAD `113269d`（M9.5 代码已提交；本文档未提交）
构建：`release-vk`（性能）、`debug-vk` + `asan`（正确性）

---

## 0. 结论摘要

1. **ToyCar PBR 完成**：四贴图、Cook-Torrance、1 方向光 + 2 点光、单张方向光阴影；validation 0。
2. **Sponza 几何 PBR 完成**：405 primitives / 3,747,018 triangles，室内相机可正常出画；validation 0。
3. **Sponza 4K 纹理暂未验收**：
   - 72 张 4096×4096 PNG；
   - 同步加载实测约 **98.4s**；
   - M9.5 使用 `--skip-textures` 只验证几何/PBR factors；
   - worker 解码或 1K 缓存留到 M9.5d-0 / M10。
4. **阴影在 Sponza 室内会显著变暗**：
   - 单方向光被屋顶遮挡，室内主要依赖点光；
   - 当前点光按场景 AABB 放置，Sponza 室内照明不足；
   - ToyCar 的阴影证据清晰；Sponza 默认建议 `--no-shadow`。

---

## 1. 环境

```text
CPU        Intel 6 核 12 线程 @ 3.00GHz
GPU        NVIDIA GeForce RTX 5060，8GB
Vulkan     1.4.313
显示       Xephyr :99，1280x720x24
构建       release-vk / debug-vk
```

与 M9.2b 相同：原生 `:0` 会触发 GLFW 卡死，使用 Xephyr；`present` 有约 40ms 下限，绝对帧时间仅供参考。

---

## 2. 测试配置

| ID | 场景 | 参数 |
|---|---|---|
| T1 | ToyCar PBR | `--frames 60`（默认 1 方向 + 2 点光 + shadow） |
| T2 | ToyCar streaming | `--frames 60 --stream-assets` |
| S1 | Sponza 几何 | `--skip-textures --no-shadow --camera ... --target ... --frames 60/120` |
| S2 | Sponza + shadow | 同 S1，去掉 `--no-shadow` |

Sponza 室内相机（来自资产内 `PhysCamera001`）：

```bash
--camera -8.807,1.592,-0.858 --target -3.004,2.762,0.237
```

---

## 3. 正确性

```text
debug-vk ctest   504/504
ASan ctest       504/504
format / deps    通过
git diff --check 通过
```

| 场景 | items | triangles | missing | validation |
|---|---:|---:|---:|---:|
| ToyCar PBR + shadow | 3 | 108,936 | 0 | 0 |
| ToyCar streaming | 3 | 108,936 | 0 | 0 |
| Sponza 几何（no-shadow） | 405 | 3,747,018 | 0 | 0 |
| Sponza 几何 + shadow | 405 | 3,747,018 | 0 | 0 |

dump/replay v3 正常；ToyCar dump ≈22.66MB。

---

## 4. ToyCar

### 4.1 视觉

截图：

```text
doc/media/m9.5-toycar.png
```

- 绿色车身；
- 橙色车窗/拉花；
- 布料有 normal/MR 细节；
- 1 方向光 + 暖橙/冷蓝点光；
- 阴影对比度较低（点光占主导），但 shadow mask/diff 已验证方向光阴影参与光照。

### 4.2 帧时间（release / Xephyr）

| 配置 | p50 | p95 | 说明 |
|---|---:|---:|---|
| 单光源 PBR（a-5） | 19.2ms | 20.5ms | 基准 |
| 多光源（b） | 22.9ms | 37.5ms | +3 光源 |
| 多光源 + shadow（c） | 29.2ms | 42.5ms | +depth pass |

Xephyr 的 present 下限约 40ms；这里的 p50 仍受环境影响，主要用于相对比较。

---

## 5. Sponza

### 5.1 视觉

截图（几何 + PBR factors + 3 光源 + no-shadow）：

```text
doc/media/m9.5-sponza.png
```

- 室内相机；
- 405 primitives / 3,747,018 triangles；
- 无纹理，但能看到石柱/拱门的几何与 PBR 光照。

### 5.2 帧时间

| 配置 | p50 | p95 | 备注 |
|---|---:|---:|---|
| Sponza 几何 + 3 光源，no-shadow | 43.6–58.0ms | 44.9–64.2ms | 多次运行波动 |
| Sponza 几何 + 3 光源 + shadow | 69.5–107.0ms | 70.9–118.9ms | shadow pass 翻倍 draw |

结论：

- shadow pass 对 405 draws 的 Sponza 成本明显；
- 最终 demo 建议 ToyCar 开 shadow，Sponza 用 `--no-shadow`；
- 若要让 Sponza 也开 shadow，需要降低 map 尺寸、减少光源或加入光源/阴影剔除。

### 5.3 Sponza 阴影偏暗

开 shadow 后室内显著变暗：

- 单方向光被屋顶遮挡，室内几乎只收到 ambient（albedo×0.03）和点光；
- 当前点光位置按 AABB 中心偏移，Sponza 尺度下大部分在墙体/屋顶附近，贡献不足；
- 这不是 shadow map 通路错误；ToyCar 的 mask/diff 已证明 shadow sampling 生效。

后续需要：

- 按室内空间放置点光；
- 提高点光 intensity/range；
- 或提高 ambient（但会削弱阴影对比）。

---

## 6. Sponza 4K 纹理问题

实测（默认同步加载，带纹理，`--frames 30`）：

```text
load ≈ 98.4s
items=405 / triangles=3,747,018 / validation 0
```

原因：

- 72 张 4096×4096 PNG；
- 主线程 `stbi` 解码；
- 即使下采样到 1024，解码仍要处理全分辨率；
- 同步路径在 `loadGltfScene` 一次性完成。

当前策略：

- M9.5 只验收几何 + PBR factors；
- Sponza 带纹理留到：
  1. worker 解码 + 下采样 + in-flight 限制；或
  2. 预生成 1K Sponza 纹理缓存；或
  3. M10 JobSystem 多线程解码。

---

## 7. M9.5 范围核对（D21）

| 项 | 状态 |
|---|---|
| Cook-Torrance D/G/F | ✅ |
| metallic-roughness | ✅ |
| BaseColor/Normal/MR/Emissive | ✅（ToyCar） |
| Directional + Point ≤8 | ✅（demo 3 光源） |
| 单张方向光阴影 + PCF | ✅ |
| alpha mask | ✅（`discard`） |
| glTF 材质 | ✅ |
| ToyCar 视觉 | ✅ |
| Sponza 外部压力场景 | 几何 ✅；纹理 ⏳ |
| IBL/skybox/spot/alpha blend/ImGui | 未做（符合 D21） |

已知限制：

- 无 mipmap / anisotropy；
- 无 light culling；
- shadow pass 不做 alpha mask；
- 无点光/聚光阴影、无级联；
- `KHR_texture_transform` 未支持；
- `KHR_materials_transmission/clearcoat/sheen` 未支持；
- `BLEND` 按 opaque；
- 4K 纹理解码阻塞；
- Xephyr 的 present 下限影响绝对帧时间。

---

## 8. 结论与下一步

结论：

- M9.5 的 **PBR 渲染主体完成**：四贴图、多光源、方向光阴影、alpha mask；
- ToyCar 达到最终视觉验收；
- Sponza 几何/流式压力测试通过，纹理与室内光照仍需收口。

下一步建议：

1. M9.5d-0：worker 纹理解码 + 下采样 + 背压（或 1K Sponza 缓存）；
2. Sponza 室内点光调参，决定 shadow 是否默认开；
3. 之后进入 M10 任务系统，替换 LoaderWorker/解码为 JobSystem；
4. 补 mipmap/light culling（可放到 M11 或 Stretch）。

---

## 9. 原始数据

```text
/tmp/opencode/m9_5a/   单光源 PBR 数据
/tmp/opencode/m9_5b/   多光源数据
/tmp/opencode/m9_5c/   阴影数据
/tmp/opencode/m9_5d/   Sponza 室内/阴影数据
/tmp/opencode/pbr_shadow_toycar.png
/tmp/opencode/m9_5d/sponza_inside.png
```
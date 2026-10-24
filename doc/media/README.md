# media/ · 展示素材

存放 README 与博客引用的图片、GIF、录屏。

| 类型 | 命名 | 大小限制 | 说明 |
|---|---|---|---|
| 架构图（精修版） | `architecture.png` | — | 日常维护用 `01-architecture.md` 里的 Mermaid；此图只在里程碑收口时重新导出 |
| 终端录屏 | `m4-tick-sandbox.gif` / `.cast` | GIF ≤ 5MB | 优先 asciinema `.cast`（体积小、可搜索），GIF 只用于 README |
| 图形 Demo | `m7-render-demo.gif` / `m7-snapshot.png` | GIF ≤ 5MB | `yr_render_demo`：WASD 移动中间蓝色 Node（GIF 1280×720、24 帧 / 1.92s / ~12.5fps）；复现：live 与 `--dump-snapshot` / `--replay-snapshot` 画面一致 |
| 数据图 | `e10-async-loading.png` | — | 由 `tools/plot_stats.py` 生成，脚本必须一起提交 |
| 工具截图 | `m2-yr-inspect.png` | — | |

**规则**：① 大文件（>5MB）不进 git；② 每张图都要有生成它的脚本或明确的复现步骤；③ 文件名带里程碑前缀，便于清理。

## M7 图形 Demo

`m7-render-demo.gif`（24 帧 / 1.92s / 1280×720）与 `m7-snapshot.png` 由 `yr_render_demo` 生成：WASD 移动中间蓝色 Node。

```bash
cmake --preset debug-vk && cmake --build --preset debug-vk
./build/debug-vk/bin/yr_render_demo --frames 0          # WASD 操作
./build/debug-vk/bin/yr_render_demo --frames 600 --dump-snapshot /tmp/m7.yrsnap
./build/debug-vk/bin/yr_render_demo --replay-snapshot /tmp/m7.yrsnap --frames 600
```

dump 与 replay 应输出同一画面。GIF 由 `import` 逐帧截图后用 ImageMagick `convert -delay 8 -layers Optimize` 合成，中间帧不入库。

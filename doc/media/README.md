# media/ · 展示素材

存放 README 与博客引用的图片、GIF、录屏。

| 类型 | 命名 | 大小限制 | 说明 |
|---|---|---|---|
| 架构图（精修版） | `architecture.png` | — | 日常维护用 `01-architecture.md` 里的 Mermaid；此图只在里程碑收口时重新导出 |
| 终端录屏 | `m4-tick-sandbox.gif` / `.cast` | GIF ≤ 5MB | 优先 asciinema `.cast`（体积小、可搜索），GIF 只用于 README |
| 图形 Demo | `m10-render-demo.gif` | ≤ 5MB | mp4 原片不进 git，放 release 附件或外部链接 |
| 数据图 | `e10-async-loading.png` | — | 由 `tools/plot_stats.py` 生成，脚本必须一起提交 |
| 工具截图 | `m2-yr-inspect.png` | — | |

**规则**：① 大文件（>5MB）不进 git；② 每张图都要有生成它的脚本或明确的复现步骤；③ 文件名带里程碑前缀，便于清理。

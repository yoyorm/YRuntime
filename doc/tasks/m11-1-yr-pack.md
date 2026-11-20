# M11-1：`yr_pack` 打包工具 + `PakFileAccess`

> 状态：规划中（格式契约待用户确认；确认后交实现会话）
> 角色：规划（当前持有）
> 最近更新：2026-11-20

## 1. 目标

把 `assets/` 打成一个 `.yrpak` 文件，并让 Runtime 通过 `PakFileAccess` 完全从包里加载：

```bash
yr_pack --src assets --out game.yrpak
yr_render_demo --pak game.yrpak --gltf res://models/ToyCar/ToyCar.gltf
# assets/ 目录被删除后仍然能跑
```

做完能回答：资源如何从源文件变成运行时包？包损坏/版本不匹配/路径穿越怎么处理？为什么 `PakFileAccess` 可以不改任何 loader？

## 2. 格式契约（草案，待确认）

### 2.1 文件布局（v1.0，全小端）

```text
┌──────────────────────────── Header (64 B, 16 对齐) ────────────────────────────┐
│ magic[8] = "YRPAK001"                                                          │
│ versionMajor u16 | versionMinor u16                                            │
│ flags u32（bit0 压缩，v1 必须为 0）                                             │
│ entryCount u32                                                                 │
│ manifestOffset u64 | manifestSize u64                                          │
│ blobOffset u64 | blobSize u64                                                   │
│ reserved[8]（置 0；为 GPU 直接映射/未来扩展预留）                                 │
├──────────────────────────── Manifest（UTF-8 JSON） ─────────────────────────────┤
│ { "entries": [ { "path": "...", "offset": 0, "size": 0, "hash": "0x...",     │
│                 "align": 16 } ] }                                              │
├──────────────────────────── Blob（entry 依次排列） ─────────────────────────────┤
│ entry0（按 align 填充 0）│ entry1 │ …                                          │
└─────────────────────────────────────────────────────────────────────────────────┘
```

- `offset` 相对 **blob 起点**；`size` 为字节数；`align` 默认 16；
- **entry 按 `path` 字典序排序**：同一输入 → 逐字节相同的包（可复现构建）；
- 路径规范化：统一 `/`、去掉 `res://` 前缀、禁止 `..`/绝对路径/空段、大小写敏感。

### 2.2 关键决策（待你确认，括号内为推荐）

| # | 决策 | 推荐 | 备选 |
|---|---|---|---|
| D1 | manifest 格式 | **JSON（可读、可 diff）** | 二进制（等有性能消费方再做） |
| D2 | 哈希算法 | **FNV-1a 64**（无依赖、确定性；只做完整性校验，不做安全） | SHA-256（需要额外依赖） |
| D3 | 对齐 | **16 B** | 4 B / 无 |
| D4 | 压缩 | **v1 不做**，`flags` 留位 | 之后接 zstd |
| D5 | 重复路径 | **pack 阶段报错退出** | 后者覆盖前者 |
| D6 | 版本策略 | **major 不同直接拒绝**；minor 向后兼容 | 迁移链（与 D24 一致，不做） |
| D7 | `PakFileAccess` 的读模型 | **每次 `readFile` 打开/定位读取**（线程安全、无共享游标） | 共享 `ifstream` + 互斥量（会串行化） |

### 2.3 CLI 与失败语义

```text
yr_pack --src <dir> --out <file.yrpak> [--align N] [--verbose]
         缺 --src/--out、src 不是目录、重复路径、非法路径 → 非 0 退出 + 明确错误
         --align 必须是 2 的幂，默认 16

yr_render_demo --pak <file.yrpak> ...        # 与 --asset-root 互斥
yr_runtime_headless --pak <file.yrpak> --scene res://...
```

| 情况 | 期望 |
|---|---|
| 包里没有该路径 | `readFile` 返回 false，error 含路径 |
| 哈希不匹配 | 返回 false，error 指明 entry |
| major 版本不符 / magic 错误 / 截断 | 构造 `PakFileAccess` 失败，error 说明原因 |
| 路径穿越（`../`、绝对路径） | pack 阶段拒绝；读取阶段也拒绝 |

## 3. 文件所有权（按 AGENTS §7）

**用户手写：**

- `.yrpak` 格式核心：header/manifest 写入与解析、entry 排序、哈希、边界检查；
- `pack` 核心（目录遍历 + 路径规范化 + blob 编排）；
- **验收测试第一版**（见 §4，2~4 条）。

**AI 实现（分步提交，用户 review）：**

- `PakFileAccess`（`IFileAccess` 实现，含线程安全读取）；
- `yr_pack` 的 CLI 参数、CMake target、`tests/tools` 接线；
- 损坏包 / 版本不符 / 路径穿越 / fuzz / 并发读取测试；
- 文档与报告。

**放置建议（待确认）：**

```text
tools/yr_pack/main.cpp            # CLI
tools/yr_pack/pack_core.h/.cpp    # 格式核心（可被测试链接）
engine/asset/src/pak_file_access.*# IFileAccess 实现
tests/tools/test_yr_pack.cpp      # 格式/损坏包测试
tests/asset/test_pak_file_access.cpp
```

## 4. 测试分工

### 用户先写（验收用例）

1. **round-trip**：pack 一个含 2 个文件的目录 → `PakFileAccess` 读回两份字节，逐字节相同；
2. **可复现**：同一输入 pack 两次 → 两个包**逐字节相同**；
3. **缺失路径**：读取不存在的路径 → `false` + error 非空，不崩溃；
4. **非法输入**：`--src` 指向文件 / 不存在的目录 → 非 0 退出。

### AI 补充

- 损坏包：截断 header / manifest / blob、magic 错误、major 版本不符、size 越界；
- 哈希不匹配（改一个 blob 字节）；
- 重复路径拒绝、`../` 路径拒绝；
- fuzz：随机截断/翻转字节，只允许"明确失败"，不允许崩溃/越界（ASan）；
- 并发读取：8 线程同时 `readFile` 不同/相同 entry（TSan）；
- 端到端：`runtime_headless --pak` 加载包内 `.scn` 跑固定帧数（集成测试用临时目录 + 仓库小样例）。

### 验收命令

```bash
cmake --build --preset debug-vk
ctest --test-dir build/debug-vk -R "yr_pack|pak_file|pak_access"
ctest --test-dir build/debug-vk
cmake --build --preset asan && ctest --preset asan
cmake --build --preset tsan && ./tools/ctest_tsan.sh -R "pak"
./tools/check_format.sh && python3 tools/check_deps.py -v
```

端到端（真实 demo）：

```bash
./build/debug-vk/bin/yr_pack --src assets --out /tmp/toycar.yrpak
DISPLAY=:99 ./build/debug-vk/bin/yr_render_demo --pak /tmp/toycar.yrpak \
    --gltf res://models/ToyCar/ToyCar.gltf --frames 120
# 验收：临时把 assets/ 改名后仍能跑
```

## 5. 明确不做

- 压缩（zstd/lz4）、加密、签名；
- 内存映射 / 增量打包 / 去重 / 跨包引用；
- UID 映射（v1 只按路径）；
- pak 内嵌 pak、网络分发；
- `--pak` 之外的启动方式（Steam/平台集成）。

## 6. Q&A / 决策记录

- 2026-11-20：`PakFileAccess` 实现现有 `IFileAccess`，因此 AssetDatabase / glTF / `.scn` 全部**零改动**复用；这是本任务最重要的结构选择。
- 2026-11-20：`readFile` 必须线程安全——`JobLoadBackend` 会从多个 worker 并发调用；推荐每次打开独立句柄，避免把读包变成全局串行。
- 2026-11-20：entry 排序后写包，保证"同一输入 → 同字节输出"，与 `.scn` 的确定性原则一致（D24）。
- 2026-11-20：哈希只做完整性校验，不承担安全职责；因此 FNV-1a 64 足够，避免引入新依赖。

## 7. 交接块

- 角色：规划
- 当前 HEAD：`d8277ae`
- 工作区：有另一个会话的未提交改动（asset 常量/报告），本卡未触碰
- 本单元完成：M11-1 格式契约草案 + 测试分工 + 验收标准
- 验证命令与结果：文档变更，未跑测试
- 未决问题：D1~D7 待用户确认；`tools/yr_pack` 是否新建 CMake target 与 `tests/tools` 目标名
- 下一步：用户确认 D1~D7 → 交实现会话（先格式核心 + 验收测试第一版）；验证会话准备坏包/并发用例
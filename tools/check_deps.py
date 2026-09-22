#!/usr/bin/env python3
"""YRuntime 架构门禁：依赖方向 + 禁用符号检查。

对应文档：doc/01-architecture.md §2（分层）与 §8（强制机制）、ADR D11。
核心原则：**规则必须有机制保证，而不是靠自觉。**

用法：
    python3 tools/check_deps.py            # 检查，有问题退出码 1
    python3 tools/check_deps.py -v         # 额外打印扫描统计

它检查三件事：
  1. 反向依赖：某层 include 了它不允许 include 的层（例：core 里出现 <yr/scene/...>）
  2. 禁用符号：裸 new / delete（= delete 与 operator new 除外）
  3. 图形 API 泄漏：vulkan / GLFW 头文件出现在 engine/render/vulkan/ 之外
     （这是"Runtime 与 Renderer 解耦"的硬指标，见 00-vision.md §4.3）
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# ============================================================================
# 允许矩阵：唯一的真相来源。新增一层时必须在这里加一行。
# 依据 doc/01-architecture.md §2 的依赖图。key 可以依赖 value 里列出的层。
# ============================================================================
ALLOWED_DEPS: dict[str, set[str]] = {
    "core":          set(),                                        # 叶子：只依赖 std
    "job":           {"core"},
    "object":        {"core"},
    "render_iface":  {"core"},
    "event":         {"object", "core"},
    "serialize":     {"object", "core"},
    "asset":         {"object", "job", "core"},
    "scene":         {"object", "event", "serialize", "asset", "core"},
    "engine":        {"scene", "asset", "event", "serialize", "object", "job",
                      "render_iface", "core"},
    "render_null":   {"render_iface", "core"},
    "render_vulkan": {"render_iface", "core"},
}

# 目录 → 层名。engine/ 下按目录分；render 的三个后端在 engine/render/ 下。
DIR_TO_LAYER: dict[str, str] = {
    "engine/core":          "core",
    "engine/job":           "job",
    "engine/object":        "object",
    "engine/event":         "event",
    "engine/serialize":     "serialize",
    "engine/asset":         "asset",
    "engine/scene":         "scene",
    "engine/engine":        "engine",
    "engine/render/iface":  "render_iface",
    "engine/render/null":   "render_null",
    "engine/render/vulkan": "render_vulkan",
}

# include 前缀 → 层名。<yr/render/...> 归 render_iface（后端不被任何人 include）。
INCLUDE_TO_LAYER: dict[str, str] = {
    "yr/core":      "core",
    "yr/job":       "job",
    "yr/object":    "object",
    "yr/event":     "event",
    "yr/serialize": "serialize",
    "yr/asset":     "asset",
    "yr/scene":     "scene",
    "yr/engine":    "engine",
    "yr/render":    "render_iface",
}

# 扫描这些顶层目录
SCAN_DIRS = ("engine", "tests", "tools", "apps", "benchmarks")
SOURCE_SUFFIXES = {".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".inl"}
SKIP_DIR_NAMES = {"build", ".git", ".cache", "third_party", "out", ".github"}

# 图形 API 头文件：只允许出现在这些目录里
GRAPHIC_INCLUDE_RE = re.compile(r'#\s*include\s*[<"](vulkan/|GLFW/|glfw3)')
GRAPHIC_ALLOWED_DIRS = ("engine/render/vulkan",)

# 禁用符号。先剥掉注释与字符串字面量再匹配，避免误报。
NEW_RE = re.compile(r"\bnew\b")
DELETE_RE = re.compile(r"\bdelete\b")
# 合法用法白名单：删除的函数、operator new/delete 重载
LEGIT_DELETE_RE = re.compile(r"=\s*delete\b")
LEGIT_NEW_RE = re.compile(r"\boperator\s+new\b")
LEGIT_DELETE_OP_RE = re.compile(r"\boperator\s+delete\b")

INCLUDE_RE = re.compile(r'#\s*include\s*[<"]([^>"]+)[>"]')


def strip_comments_and_literals(src: str) -> str:
    """把注释和字符串/字符字面量的内容替换成空格，保留长度与换行。

    这样行号仍然准确，而 `// 这里用了 new` 或 `"delete"` 不会误报。
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if c == "/" and nxt == "/":                      # 行注释
            while i < n and src[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and nxt == "*":                    # 块注释
            out.append("  ")
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c in ('"', "'"):                            # 字符串 / 字符字面量
            quote = c
            out.append(" ")
            i += 1
            while i < n and src[i] != quote:
                if src[i] == "\\":                       # 跳过转义
                    out.append(" ")
                    i += 1
                if i < n:
                    out.append("\n" if src[i] == "\n" else " ")
                    i += 1
            if i < n:
                out.append(" ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def iter_sources() -> list[Path]:
    files: list[Path] = []
    for top in SCAN_DIRS:
        root = REPO_ROOT / top
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if any(part in SKIP_DIR_NAMES for part in path.relative_to(REPO_ROOT).parts):
                continue
            files.append(path)
    return files


def layer_of(rel: Path) -> str | None:
    """文件属于哪一层。tests/<layer>/ 视同 engine/<layer>（测试也要守分层）。"""
    parts = rel.parts
    if parts[0] == "tests" and len(parts) > 1 and parts[1] in DIR_TO_LAYER.values():
        return parts[1]
    for prefix, layer in sorted(DIR_TO_LAYER.items(), key=lambda kv: -len(kv[0])):
        if str(rel).startswith(prefix + "/"):
            return layer
    return None            # apps/ tools/ benchmarks/：允许 include 任何层


def included_layer(inc: str) -> str | None:
    for prefix, layer in sorted(INCLUDE_TO_LAYER.items(), key=lambda kv: -len(kv[0])):
        if inc == prefix or inc.startswith(prefix + "/"):
            return layer
    return None            # std / 第三方


def check() -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    stats = {"files": 0, "includes": 0, "layers": set()}

    for path in iter_sources():
        rel = path.relative_to(REPO_ROOT)
        stats["files"] += 1
        try:
            raw = path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:                              # pragma: no cover
            errors.append(f"{rel}: 读取失败：{exc}")
            continue
        clean = strip_comments_and_literals(raw)
        src_layer = layer_of(rel)
        if src_layer:
            stats["layers"].add(src_layer)

        for lineno, line in enumerate(clean.split("\n"), 1):
            # ---- 1. 依赖方向 ----
            m = INCLUDE_RE.search(line)
            if m:
                inc = m.group(1)
                stats["includes"] += 1
                target = included_layer(inc)
                if target and src_layer and target != src_layer:
                    allowed = ALLOWED_DEPS.get(src_layer, set())
                    if target not in allowed:
                        want = "、".join(sorted(allowed)) if allowed else "无（叶子层）"
                        errors.append(
                            f"{rel}:{lineno}: 反向依赖 {src_layer} → {target}"
                            f"（#include <{inc}>）。{src_layer} 只允许依赖：{want}"
                        )
                # ---- 3. 图形 API 泄漏 ----
                if GRAPHIC_INCLUDE_RE.search(line):
                    if not str(rel).startswith(GRAPHIC_ALLOWED_DIRS):
                        errors.append(
                            f"{rel}:{lineno}: 图形 API 头文件泄漏（#include <{inc}>）。"
                            f"只有 {'、'.join(GRAPHIC_ALLOWED_DIRS)}/ 可以 include vulkan/GLFW"
                            f"（见 00-vision.md §4.3 的解耦硬指标）"
                        )

            # ---- 2. 禁用符号 ----
            if NEW_RE.search(line) and not LEGIT_NEW_RE.search(line):
                errors.append(
                    f"{rel}:{lineno}: 禁用裸 `new`（用 make_ref / make_unique / SlotMap）。"
                    f"见 05-engineering.md §12"
                )
            if DELETE_RE.search(line) and not LEGIT_DELETE_RE.search(line) \
                    and not LEGIT_DELETE_OP_RE.search(line):
                errors.append(f"{rel}:{lineno}: 禁用裸 `delete`（用 RAII / Ref<T>）")

    # ---- 自检：有目录没登记进矩阵就报错，避免"新层悄悄绕过检查" ----
    for prefix, layer in DIR_TO_LAYER.items():
        if (REPO_ROOT / prefix).is_dir() and layer not in ALLOWED_DEPS:
            errors.append(f"tools/check_deps.py: 目录 {prefix}/ 存在，但层 '{layer}' "
                          f"不在 ALLOWED_DEPS 里 —— 请补上它的允许依赖")
    known = set(ALLOWED_DEPS)
    for layer, deps in ALLOWED_DEPS.items():
        unknown = deps - known
        if unknown:
            errors.append(f"tools/check_deps.py: 层 '{layer}' 依赖了未登记的层 {sorted(unknown)}")

    stats["layers"] = sorted(stats["layers"])               # type: ignore[assignment]
    return errors, stats                                    # type: ignore[return-value]


def main() -> int:
    ap = argparse.ArgumentParser(description="YRuntime 架构门禁检查")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印扫描统计")
    args = ap.parse_args()

    errors, stats = check()

    if args.verbose:
        print(f"扫描 {stats['files']} 个源文件，{stats['includes']} 条 #include，"
              f"涉及层：{stats['layers'] or '（暂无）'}")

    if not errors:
        print("✓ check_deps: 依赖方向、禁用符号、图形 API 隔离全部通过")
        return 0

    print(f"✗ check_deps: 发现 {len(errors)} 处违规\n", file=sys.stderr)
    for e in errors:
        print(f"  {e}", file=sys.stderr)
    print("\n如何修：① 真的是反向依赖 → 重新设计（把共有的东西下沉到更低的层）；"
          "\n        ② 允许矩阵该更新 → 改 tools/check_deps.py 的 ALLOWED_DEPS，"
          "并在 doc/06-decisions.md 写一条 ADR 说明为什么。", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())

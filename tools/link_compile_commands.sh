#!/usr/bin/env bash
# 把仓库根 compile_commands.json 指向某个 preset 的数据库，供 clangd / IDE 使用。
#
# 默认指 debug-vk：它是唯一带 Vulkan 后端的常用 preset，DB 含全部 target 与
# vulkan TU 的 -isystem；debug / release / asan（Vulkan 后端 OFF）里没有 vulkan TU。
#
# 用法：tools/link_compile_commands.sh [preset]   （默认 debug-vk）
set -euo pipefail

preset="${1:-debug-vk}"

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
db="build/${preset}/compile_commands.json"

if [[ ! -f "${repo_root}/${db}" ]]; then
  echo "error: ${db} 不存在，请先运行 cmake --preset ${preset}" >&2
  exit 1
fi

ln -sfn "${db}" "${repo_root}/compile_commands.json"
echo "compile_commands.json -> ${db}"
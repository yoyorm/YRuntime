#!/usr/bin/env bash
# 格式门禁：只检查仓库自有 C/C++。
# third_party/ 是 vendored 上游代码，必须保持字节不变（D29），不能参与格式化。
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

formatter="${CLANG_FORMAT:-clang-format-18}"

# -c -o --exclude-standard：把未跟踪的新文件也纳入检查，避免“本地绿、CI 红”。
# ':(exclude)third_party/**'：排除 vendored 代码。
git ls-files -z -co --exclude-standard -- '*.h' '*.cpp' ':(exclude)third_party/**' \
  | xargs -0 -r "${formatter}" --dry-run -Werror
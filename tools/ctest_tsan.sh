#!/usr/bin/env bash
# TSan 测试包装脚本。
#
# 背景：部分内核/容器配置下 TSan 启动会报
#     FATAL: ThreadSanitizer: unexpected memory mapping ...
# 原因是 TSan 与 ASLR（地址空间随机化）冲突。`setarch -R` 关闭 ASLR 后
# 整个 ctest 进程树都会继承该设置，测试二进制因此可以正常运行。
#
# 用法：
#   ./tools/ctest_tsan.sh                 # 全量
#   ./tools/ctest_tsan.sh -R "job"        # 按用例名过滤（ctest 正则）
#   ./tools/ctest_tsan.sh --rerun-failed --output-on-failure
set -euo pipefail

cd "$(dirname "$0")/.."

exec setarch "$(uname -m)" -R ctest --preset tsan "$@"
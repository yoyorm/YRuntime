#pragma once

#include <atomic>
#include <thread>

#include <yr/core/assert.h>

namespace yr::core {

  // 主线程标记与查询。
  //
  // 用途：worker 线程只能处理 POD 字节，Object/Resource 的构造与注册只在主线程
  // （AGENTS.md §3）。宿主在启动时调用 markMainThread()，之后用
  // YR_ASSERT_MAIN_THREAD() 在"仅主线程"的 API 入口做审计。
  //
  // 语义：
  // - 未调用 markMainThread() 时，isMainThread() 恒为 false，
  //   YR_ASSERT_MAIN_THREAD() **不做检查**（单测/无宿主场景不应被误伤）；
  // - markMainThread() 幂等；若已被另一个线程标记，Debug 下断言（配置错误）；
  // - 只读开销是两次原子读（默认 seq_cst），不需要为它单独设计内存序。
  //
  // 线程安全：所有函数可从任意线程调用；标记只应在启动阶段写一次。

  // 把当前线程标记为主线程（宿主启动时调用一次，例如 Engine 构造）。
  void markMainThread() noexcept;

  // 当前线程是否被标记为主线程。
  [[nodiscard]] bool isMainThread() noexcept;

  // 是否已经有人标记过主线程。
  [[nodiscard]] bool mainThreadMarked() noexcept;

  // 清空标记：**仅用于测试**（让每个用例从干净状态开始）。生产代码不要调用。
  void resetMainThreadForTesting() noexcept;

  namespace detail {

    inline std::atomic<std::thread::id>& mainThreadId() noexcept {
      static std::atomic<std::thread::id> id{};
      return id;
    }

  } // namespace detail

  inline void markMainThread() noexcept {
    const std::thread::id current = std::this_thread::get_id();
    std::thread::id expected{};
    if (!detail::mainThreadId().compare_exchange_strong(expected, current, std::memory_order_acq_rel)) {
      YR_ASSERT_MSG(expected == current, "markMainThread() 被另一个线程重复调用");
    }
  }

  [[nodiscard]] inline bool isMainThread() noexcept {
    const std::thread::id marked = detail::mainThreadId().load(std::memory_order_acquire);
    return marked != std::thread::id{} && marked == std::this_thread::get_id();
  }

  [[nodiscard]] inline bool mainThreadMarked() noexcept {
    return detail::mainThreadId().load(std::memory_order_acquire) != std::thread::id{};
  }

  inline void resetMainThreadForTesting() noexcept {
    detail::mainThreadId().store(std::thread::id{}, std::memory_order_release);
  }

} // namespace yr::core

// 仅在宿主已标记主线程时检查；未标记时 no-op（见上）。
#if YR_ENABLE_ASSERTS

#define YR_ASSERT_MAIN_THREAD()                                                                                        \
  do {                                                                                                                 \
    if (!::yr::core::mainThreadMarked() || ::yr::core::isMainThread()) {                                               \
      break;                                                                                                           \
    }                                                                                                                  \
    ::yr::core::reportAssert(                                                                                          \
        {__FILE__, __LINE__, __func__, "isMainThread()", "必须在主线程调用（worker 只能处理 POD 字节）"});             \
  } while (false)

#else
#define YR_ASSERT_MAIN_THREAD() ((void)0)
#endif
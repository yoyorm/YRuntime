#pragma once

// TODO(M3b): post / flush（双缓冲）。
// TODO(M3c): MessageQueue（延迟调用）。
#include <cstddef>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <yr/event/subscription.h>

namespace yr::evt {

  // 类型化同步事件总线。
  // - 每种事件类型 E 在 subscribers_ 里有一张独立分表：type_index → vector<Entry>。
  // - 回调公开形式是强类型 void(const E&)；内部用 std::function<void(const void*)>
  //   擦除类型。安全性来自"按 type_index 分区"：只有 publish<E> 会以 E* 解释该表的 payload。
  // - 退订只把对应记录的 active 置 false，不删除 vector 元素，避免 publish 遍历期间迭代器失效。
  // - 不拥有 Subscription：subscribe 返回的 Subscription 保存本总线的非拥有指针，
  //   正常使用要求 Subscription 不晚于其 EventBus 销毁。避免全局/静态 Subscription；
  //   确有需要时应在退出前显式 reset()。
  // - 线程约束：暂定仅主线程；主线程 assert / MPSC 见 M3 后续（roadmap §M3）。
  class EventBus {
  public:
    EventBus() = default;
    ~EventBus() = default;

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    // 订阅事件类型 E。返回的 Subscription 析构（或 reset）时自动退订。
    template <typename E> [[nodiscard]] Subscription subscribe(std::function<void(const E&)> callback) {
      const std::uint32_t id = nextId_++;
      auto& entries = subscribers_[std::type_index(typeid(E))];
      entries.push_back(
          Entry{id, true, [cb = std::move(callback)](const void* payload) { cb(*static_cast<const E*>(payload)); }});
      return Subscription(this, std::type_index(typeid(E)), id);
    }

    // 立即同步派发
    template <typename E> void publish(const E& event) const {
      const auto it = subscribers_.find(std::type_index(typeid(E)));
      if (it == subscribers_.end()) {
        return;
      }
      // 固定本轮订阅者数量：publish 期间新增的同类型订阅者不接收本次事件。
      // 按索引访问而非迭代器，回调里 subscribe 造成的 vector 扩容不会让遍历越界；
      // 但"publish 中订阅同一类型"（扩容会移动正在执行的 std::function）留待 M3 重入语义处理。
      const std::size_t count = it->second.size();
      for (std::size_t i = 0; i < count; ++i) {
        const Entry& entry = it->second[i];
        if (entry.active) {
          entry.callback(&event);
        }
      }
    }

    void unsubscribe(Subscription& sub) noexcept;

  private:
    struct Entry {
      std::uint32_t id = 0;
      bool active = false;
      std::function<void(const void*)> callback;
    };

    std::unordered_map<std::type_index, std::vector<Entry>> subscribers_;
    std::uint32_t nextId_ = 1;
  };

  // 进程级默认事件总线（函数内 static，与 globalClassDB / globalObjectDB 一致）。
  // 公共使用路径通过它共享同一实例；EventBus 仍可公开构造，局部实例用于测试/隔离。
  [[nodiscard]] EventBus& globalEventBus() noexcept;

} // namespace yr::evt
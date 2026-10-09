#pragma once

// TODO: MessageQueue（延迟调用）
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <yr/core/assert.h>
#include <yr/event/subscription.h>

namespace yr::evt {

  // 类型化同步事件总线。
  // - 每种事件类型 E 一张独立分表（type_index → deque<Entry>）；用 deque 保证 publish
  //   回调中订阅同类型时不会移动正在执行的 std::function。
  // - 回调以强类型 void(const E&) 公开，内部按 type_index 分区做类型擦除。
  // - 仅主线程；主线程 assert / MPSC 见 roadmap §M3。
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

    // 同步 publish 调用栈深度上限
    static constexpr std::size_t kMaxPublishDepth = 8;

    // 立即同步派发，超限直接返回
    template <typename E> void publish(const E& event) {
      if (publishDepth_ >= kMaxPublishDepth) {
        YR_ASSERT_MSG(publishDepth_ < kMaxPublishDepth, "EventBus::publish: max sync publish depth exceeded");
        return;
      }

      ++publishDepth_;
      struct DepthGuard {
        EventBus& bus;
        explicit DepthGuard(EventBus& target) noexcept : bus(target) {}
        ~DepthGuard() {
          if (--bus.publishDepth_ == 0) {
            bus.cleanupInactive(); // inactive 清理推迟到最外层 publish 返回
          }
        }
      } guard(*this);

      const auto it = subscribers_.find(std::type_index(typeid(E)));
      if (it == subscribers_.end()) {
        return;
      }
      std::deque<Entry>& entries = it->second;
      const std::size_t count = entries.size();
      for (std::size_t i = 0; i < count; ++i) {
        Entry& entry = entries[i];
        if (entry.active) {
          entry.callback(&event);
        }
      }
    }

    void unsubscribe(Subscription& sub) noexcept;

    // 入队事件，不立即派发；下一次 flush() 时按 post 顺序（跨类型）派发。
    template <typename E> void post(E&& event) {
      using EventType = std::decay_t<E>;
      auto& queue = writeQueueIs0_ ? pendings0_ : pendings1_;
      queue.push_back(std::make_unique<TypedPendingEvent<EventType>>(std::forward<E>(event)));
    }

    void flush();

    // 等待下一次 flush 派发的事件数（即当前写缓冲的长度）
    [[nodiscard]] std::size_t pending() const noexcept {
      return writeQueueIs0_ ? pendings0_.size() : pendings1_.size();
    }

  private:
    struct Entry {
      std::uint32_t id = 0;
      bool active = false;
      std::function<void(const void*)> callback;
    };

    // 类型擦除的待派发事件：虚函数在 flush 时回调到 EventBus::publish<E>。
    struct PendingEvent {
      virtual ~PendingEvent() = default;
      virtual void dispatch(EventBus& bus) = 0;
    };

    template <typename E> struct TypedPendingEvent final : PendingEvent {
      explicit TypedPendingEvent(E value) : event(std::move(value)) {}
      void dispatch(EventBus& bus) override { bus.publish<E>(event); }
      E event;
    };

    // 物理删除所有分表里 active == false 的 Entry。只在 publishDepth_ 归零（栈上无任何
    // publish）时调用，否则 erase 会使外层 publish 正在遍历的分表失效。保留空分表。
    void cleanupInactive();

    std::unordered_map<std::type_index, std::deque<Entry>> subscribers_;
    std::uint32_t nextId_ = 1;

    // 总线级同步 publish 调用栈深度（不按事件类型分开）；用于把 inactive 清理推迟到最外层。
    std::size_t publishDepth_ = 0;

    // 双缓冲待派发队列：writeQueueIs0_ 明确表示"post 当前写入哪个队列"：
    std::deque<std::unique_ptr<PendingEvent>> pendings0_;
    std::deque<std::unique_ptr<PendingEvent>> pendings1_;
    bool writeQueueIs0_ = true;
    bool flushing_ = false;
  };

  // 进程级默认事件总线
  [[nodiscard]] EventBus& globalEventBus() noexcept;

} // namespace yr::evt
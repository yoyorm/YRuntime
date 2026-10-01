#pragma once

#include <yr/core/object_id.h>

namespace yr::obj {

  // 所有具有全局运行时身份的对象的最小基类。
  // 构造时注册，析构时注销；禁止复制和移动以保持 ObjectID 与对象地址稳定绑定。
  class Object {
  public:
    Object();
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    [[nodiscard]] yr::core::ObjectID id() const noexcept { return id_; }

  private:
    yr::core::ObjectID id_;
  };

} // namespace yr::obj

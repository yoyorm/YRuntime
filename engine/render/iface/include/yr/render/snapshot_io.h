#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <yr/render/snapshot.h>

namespace yr::render {

  // 按固定字段顺序写小端字节；不复用结构体内存布局，天然绕开 padding。
  [[nodiscard]] std::vector<std::uint8_t> encodeSnapshot(const RenderSnapshot& snapshot);

  // 校验 magic/version/长度，非法返回 false 且 out 保持不变。
  [[nodiscard]] bool decodeSnapshot(const std::uint8_t* data, std::size_t size, RenderSnapshot& out);

} // namespace yr::render
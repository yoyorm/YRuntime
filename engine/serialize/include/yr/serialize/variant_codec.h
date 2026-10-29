#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <yr/object/variant.h>

namespace yr::ser {

  struct VariantEncodeResult {
    bool ok = false;
    std::string text;
    std::string error;
  };

  struct VariantDecodeResult {
    bool ok = false;
    obj::Variant value;
    // 从调用方传入的 offset 起消耗的字节数（含跳过的前导空白）；失败时为 0。
    std::size_t consumed = 0;
    // 失败信息的 0-based 字节偏移（相对 text 起点）。
    std::size_t error_offset = 0;
    std::string error;
  };

  // v1 值语法：true/false、int、double、string、Vector3(x, y, z)。
  // 只依赖 yr::object；不处理 Scene 的 node/property 结构。
  class VariantCodec {
  public:
    [[nodiscard]] static VariantEncodeResult encode(const obj::Variant& value);

    // 解析 text[offset..] 开头的一个值，允许前导空白；不要求消费到 text 末尾。
    [[nodiscard]] static VariantDecodeResult decode(std::string_view text, std::size_t offset = 0);
  };

} // namespace yr::ser
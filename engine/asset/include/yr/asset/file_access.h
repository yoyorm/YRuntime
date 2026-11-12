#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace yr::asset {
  // 文件读取接口
  class IFileAccess {
  public:
    virtual ~IFileAccess() = default;
    virtual bool readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const = 0;
  };

} // namespace yr::asset

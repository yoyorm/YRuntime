#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <yr/asset/file_access.h>

namespace yr::asset {

  class ThrottledFileAccess final : public IFileAccess {
  public:
    ThrottledFileAccess(std::unique_ptr<IFileAccess> inner, std::chrono::milliseconds latency);

    bool readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const override;

  private:
    std::unique_ptr<IFileAccess> inner_;
    std::chrono::milliseconds latency_;
  };

} // namespace yr::asset

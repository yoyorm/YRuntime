#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <yr/asset/file_access.h>

namespace yr::asset {
  class DiskFileAccess final : public IFileAccess {
  public:
    explicit DiskFileAccess(std::string assetRoot);
    bool readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const override;

  private:
    std::string assetRoot_;
  };

} // namespace yr::asset

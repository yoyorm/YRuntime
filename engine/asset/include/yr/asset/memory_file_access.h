#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <yr/asset/file_access.h>

namespace yr::asset {

  class MemoryFileAccess final : public IFileAccess {
  public:
    void addFile(std::string path, std::vector<std::uint8_t> bytes);
    void clear();

    bool readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const override;

  private:
    std::unordered_map<std::string, std::vector<std::uint8_t>> files_;
  };

} // namespace yr::asset

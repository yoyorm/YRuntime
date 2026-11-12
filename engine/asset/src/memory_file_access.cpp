#include <yr/asset/memory_file_access.h>

#include <string>
#include <utility>

namespace yr::asset {

  void MemoryFileAccess::addFile(std::string path, std::vector<std::uint8_t> bytes) {
    files_.insert_or_assign(std::move(path), std::move(bytes));
  }

  void MemoryFileAccess::clear() {
    files_.clear();
  }

  bool MemoryFileAccess::readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const {
    out.clear();
    error.clear();

    const auto it = files_.find(std::string(path));
    if (it == files_.end()) {
      error = "内存中找不到文件：" + std::string(path);
      return false;
    }

    out = it->second;
    return true;
  }

} // namespace yr::asset

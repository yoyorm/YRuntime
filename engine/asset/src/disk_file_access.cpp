#include <yr/asset/disk_file_access.h>

#include <filesystem>
#include <fstream>
#include <utility>

#include <yr/asset/asset_uid.h>

namespace yr::asset {
  DiskFileAccess::DiskFileAccess(std::string assetRoot) : assetRoot_(std::move(assetRoot)) {}

  bool DiskFileAccess::readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const {
    out.clear();
    error.clear();

    const AssetPath canonical = canonicalizeAssetPath(path);
    if (!canonical.ok()) {
      error = canonical.error;
      return false;
    }

    const std::filesystem::path filePath =
        std::filesystem::path(assetRoot_) / std::string(canonical.canonical.substr(std::string("res://").size()));

    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
      error = "无法打开文件：" + filePath.string();
      return false;
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
      error = "无法获取文件大小：" + filePath.string();
      return false;
    }

    out.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size)) {
      out.clear();
      error = "读取文件失败：" + filePath.string();
      return false;
    }

    return true;
  }
} // namespace yr::asset

#include <yr/asset/throttled_file_access.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace yr::asset {

  ThrottledFileAccess::ThrottledFileAccess(std::unique_ptr<IFileAccess> inner, std::chrono::milliseconds latency)
      : inner_(std::move(inner)), latency_(latency) {}

  bool ThrottledFileAccess::readFile(std::string_view path, std::vector<std::uint8_t>& out, std::string& error) const {
    out.clear();
    error.clear();

    if (!inner_) {
      error = "ThrottledFileAccess 未绑定内层 IFileAccess";
      return false;
    }

    std::this_thread::sleep_for(latency_);
    return inner_->readFile(path, out, error);
  }

} // namespace yr::asset

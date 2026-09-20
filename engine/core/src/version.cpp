#include <yr/core/version.h>

namespace yr::core {

  const char* versionString() noexcept {
    return YR_VERSION_STRING;
  }
  const char* buildConfig() noexcept {
    return YR_BUILD_CONFIG;
  }

} // namespace yr::core
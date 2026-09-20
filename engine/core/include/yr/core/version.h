#pragma once

namespace yr::core {

  /// 版本号字符串，由 CMake 的 project(VERSION) 注入。
  [[nodiscard]] const char* versionString() noexcept;

  /// 构建配置名（"Debug" / "Release" / ...），由 CMake 的 $<CONFIG> 注入。
  [[nodiscard]] const char* buildConfig() noexcept;

} // namespace yr::core
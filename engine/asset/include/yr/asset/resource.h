#pragma once

#include <cstdint>
#include <string>

#include <yr/asset/asset_uid.h>
#include <yr/object/ref_counted.h>

namespace yr::asset {

  enum class LoadState : std::uint8_t { kUnloaded, kLoading, kReady, kFailed };
  enum class ResourceKind : std::uint8_t { kUnknown, kMesh, kMaterial, kTexture };
  // kSrgb：baseColor/emissive 等色彩贴图；kLinear：normal/MR 等数据贴图。
  enum class TextureColorSpace : std::uint8_t { kSrgb, kLinear };

  // M9.0a：资源数据基类。状态机：
  //   kUnloaded --markLoading--> kLoading --markReady--> kReady
  //                                 |--markFailed--> kFailed --markLoading--> kLoading
  // mark* 返回 bool；非法转换断言并返回 false，不改变状态。markLoading/markReady 清 error，
  // markFailed 保存 error。
  class Resource : public obj::RefCounted {
  public:
    ~Resource() override = default;

    [[nodiscard]] AssetUID uid() const noexcept { return uid_; }
    [[nodiscard]] const std::string& sourcePath() const noexcept { return sourcePath_; }
    [[nodiscard]] LoadState state() const noexcept { return state_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] ResourceKind kind() const noexcept { return kind_; }

    // 资源 payload 的 CPU 字节数；不包含对象头、字符串和引用计数。
    [[nodiscard]] virtual std::uint64_t cpuBytes() const noexcept = 0;

    void setUid(AssetUID uid) noexcept { uid_ = uid; }
    void setSourcePath(std::string path);
    [[nodiscard]] bool markLoading() noexcept;
    [[nodiscard]] bool markReady() noexcept;
    [[nodiscard]] bool markFailed(std::string error);

  protected:
    explicit Resource(ResourceKind kind) noexcept;

  private:
    AssetUID uid_{};
    std::string sourcePath_;
    LoadState state_ = LoadState::kUnloaded;
    std::string error_;
    ResourceKind kind_ = ResourceKind::kUnknown;
  };

} // namespace yr::asset
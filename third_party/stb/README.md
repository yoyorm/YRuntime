# stb_image（vendored）

- 来源：`Yo_Renderer/third_party/stb/`（YRuntime 的上游 vendor）。
- 版本：stb_image v2.30。
- 文件：`stb_image.h`，保持上游字节不变。
- 许可：public domain / MIT（见 `stb_image.h` 文件头）。
- 本地 shim：`stb_image_impl.cpp` 只定义 `STB_IMAGE_IMPLEMENTATION` 并 include 头文件；不修改上游。
- 用途：`yr_asset` 的 PRIVATE 实现依赖，不进入公共头、不新增公共依赖。
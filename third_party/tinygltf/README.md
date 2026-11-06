# tinyGLTF v3（vendored）

- 来源：`Yo_Renderer/third_party/tinyGLTF/`（YRuntime 的上游 vendor）。
- 版本：tiny_gltf_v3 v3.0.0-alpha。
- 文件：`tiny_gltf_v3.h`、`tiny_gltf_v3.c`、`tinygltf_json_c.h`，保持上游字节不变。
- 许可：MIT（见 `tiny_gltf_v3.h` / `tiny_gltf_v3.c` / `tinygltf_json_c.h` 文件头）。
- 本地 shim：`tinygltf_impl.cpp` 只定义 `TINYGLTF3_IMPLEMENTATION` + `TINYGLTF3_ENABLE_FS` 并 include 头文件；不修改上游。
- 用途：`yr_asset` 的 PRIVATE 实现依赖，不进入公共头、不新增公共依赖。
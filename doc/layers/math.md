# math 层（`yr_math`）

> 代码位置：`engine/math/include/yr/math/` · 最小 3D 数学：`Vec3`、`Mat4`，值语义、`constexpr` 友好、无动态分配。
>
> 设计取舍见 `../01-architecture.md` §11 Q1（自研最小数学库，GLM 只在 Vulkan 后端用）。

## Mat4 约定（全层契约，先读这一节）

`Mat4` 的约定与 M5 `RenderSnapshot` 契约冻结一致，任何使用方都必须遵守：

1. **存储 column-major**：`m_[col * 4 + row]`，`m_[0..3]` 是第 0 列。仿射平移矩阵的平移量在第 3 列，即 `m_[12], m_[13], m_[14]`。
2. **列为向量，`v' = M * v`**。
3. **右手坐标系，Y-up**（glTF 标准）。
4. **深度范围 [0, 1]**（Vulkan NDC）。

> 这四条必须在 `Mat4` 头文件与本文同时可见；只写在代码里会让使用方（renderer / 序列化 / 相机）各按各的理解写。

## Vec3（`math::Vec3`）

- **职责**：最小 3D 向量，`{x, y, z}` 值语义。
- **所有权 / 生命周期**：纯值类型，无堆分配。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Vec3() / Vec3(x, y, z)` | 默认零向量 | — |
  | `operator== / !=` | 精确浮点比较，不做 epsilon | — |
  | `operator+= / -= / *= / /=`（原地） | 分量运算 / 标量缩放 | — |
  | `lengthSquared() / length() const` | 平方长度 / 长度 | — |
  | `normalized() const` | 单位化 | 零向量 = Debug assert，随后除零 |
  | `operator+ / - / * / /`（自由函数） | 向量加减、标量乘除（`float * Vec3` 也支持） | — |
  | `dot(a, b) / cross(a, b)` | 点积 / 叉积 | — |

- **设计原因**：`= default` 的比较与 `constexpr` 运算让它能直接用于结构体与编译期常量；`length()` 不用 `constexpr`（依赖 `std::sqrt`）。
- **Godot 对照**：`core/math/vector3.h`。
- **遗留 / 已知缺口**：无 epsilon 比较；无 `lerp` / `min` / `max` 等，需要时再加。

## Mat4（`math::Mat4`）

- **职责**：4x4 矩阵，只含当前 Transform 所需的子集（乘法、转置、点 / 向量变换）。
- **所有权 / 生命周期**：纯值类型，`std::array<float, 16>` 存储，无堆分配；默认构造为 identity（矩阵惯例）。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Mat4() noexcept` | identity | — |
  | `static Mat4 identity()` | 等价默认构造 | — |
  | `float& operator[](size_t)` / `const float&` | 平坦索引 `col * 4 + row` | 越界 = UB（无断言） |
  | `operator== / !=` | 精确逐元素比较 | — |
  | `Mat4 transpose() const` | 返回转置，不改自身 | — |
  | `Vec3 transformPoint(Vec3) const` | w = 1，含平移；本阶段无投影、不做透视除法 | — |
  | `Vec3 transformVector(Vec3) const` | w = 0，忽略平移 | — |
  | `operator*(const Mat4&, const Mat4&)` | `v' = lhs * (rhs * v)`，先 rhs 后 lhs | — |

- **不变量 / 顺序**：column-major 存储（见上节）；`operator[]` 的平坦索引语义必须与存储约定一起理解。
- **设计原因**：只实现 Transform 真正需要的操作，避免过早引入完整矩阵库；默认 identity 符合矩阵使用直觉。
- **Godot 对照**：`core/math/basis.h` / `core/math/transform_3d.h` / `core/math/projection.h`。
- **遗留 / 已知缺口**：
  - `inverse` 未实现 —— 计划用 TRS 逆合成（TODO M4 / M6-M7）。
  - `perspective` / `ortho` / `lookAt` 未实现（TODO M6-M7 相机）。
  - TRS 合成与分解未实现（TODO M4 Transform 层级）。
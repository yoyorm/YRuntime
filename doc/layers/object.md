# object 层（`yr_object`）

> 代码位置：`engine/object/include/yr/object/` · 对象模型 + 反射 + 动态值：`Object`、`ObjectDB`、`Variant`、`PropertyInfo`、`ClassInfo`、`ClassDB`。
>
> `ObjectID` 的编码与语义见 `core.md`。设计意图见 `../01-architecture.md` §4.2、§4.3。

## Object（`obj::Object`）

- **职责**：所有具有全局运行时身份的对象的最小基类。
- **所有权 / 生命周期**：构造时经 `ObjectDB` 注册、析构时注销。**禁止复制和移动**：`ObjectID` 必须与对象地址稳定绑定。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Object()` | 构造即向 `globalObjectDB()` 注册，取得 ID | — |
  | `virtual ~Object()` | 析构时注销 | 注销失败 = `YR_VERIFY` 断言（Debug） |
  | `ObjectID id() const noexcept` | 本对象的全局身份 | — |

- **不变量 / 顺序**：`id_` 在构造列表里初始化，`~Object` 里先注销再移交派生类析构完成——因此**任何持 ID 的弱引用在 `Object` 析构后即解析为空**。
- **设计原因**：把"注册 / 注销"与对象生命周期强制绑定，避免调用方忘记登记（`01-architecture` §4.2、§4.6 所有权矩阵）。
- **Godot 对照**：`core/object/object.h`。
- **遗留 / 已知缺口**：反射相关的 `get/set`、`notification`、`klass()` 与 `YR_CLASS` / `YR_PROPERTY` 宏未实现（注册宏见 `class_macros.h`，目前只有 `STR` / `CONCAT` 两个词法辅助宏）。

## ObjectDB（`obj::ObjectDB`）

- **职责**：全局、**非拥有**的 `ObjectID → Object*` 索引。
- **所有权 / 生命周期**：只登记地址，不拥有对象。**调用方必须在 `Object` 内存失效前注销**；对象内存失效后指向它的条目即不可用。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Object* get(ObjectID) / const Object* get(ObjectID) const` | 失效 ID 返回 `nullptr` | — |
  | `bool contains(ObjectID) const` | 槽非空且 generation 匹配 | — |
  | `size_t aliveCount() const` | 当前存活对象数 | — |
  | `ObjectID registerObject(Object&)`（private，friend `Object`） | 分配 ID | 槽数超 24 bit → Debug assert |
  | `bool unregisterObject(ObjectID) noexcept`（private，friend `Object`） | 前进 generation 使 ID 失效 | 未登记返回 `false` |
  | `globalObjectDB()` | 进程级默认表 | — |

- **不变量 / 顺序**：只按 ID 随机访问，因此保留空 slot + freelist 复用，**不维护稠密遍历顺序**；generation 规范化到 40 位、回绕跳过 0。
- **设计原因**：`WeakRef` 与 deferred call 都需要"用 ID 反查对象"，且不能因此延长对象寿命（`01-architecture` §4.1 决策点）。
- **Godot 对照**：`core/object/object.h`（`ObjectDB` 在 `object.cpp`）。
- **遗留 / 已知缺口**：**当前仅允许主线程访问**（无锁）；无 `Object*` 类型过滤。

## Variant（`obj::Variant`）

- **职责**：对应反射 / 序列化的动态值容器（tag + union）。
- **所有权 / 生命周期**：值语义、可拷贝可移动。`String/Array/Dict` 走堆（`unique_ptr`），其余内联于 union；拷贝为**深拷贝**，移动偷指针。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `Variant()` | 默认 `kNull` | — |
  | `Variant(T value)`（按类型约束） | 隐式构造 | 不支持的类型 = 编译期 `static_assert` |
  | `type() / isNil() const` | tag 查询 | — |
  | `is<T>() const` | 类型匹配 | — |
  | `T as<T>() const` | 取值 | 类型不符 = Debug assert + 返回 `T{}` |
  | `optional<T> tryAs<T>() const` | 类型不符返回 `nullopt` | — |
  | `set<T>(T)` | 重写值 | 不支持类型 = `static_assert` |
  | `reset()` | 释放堆成员并回到 `kNull` | — |
  | `operator== / !=` | 同类型逐值比较（`Array/Dict` 递归；`Dict` 保序，顺序不同即不等） | 类型不同直接 `false` |
  | `Array = std::vector<Variant>`、`Dict = std::vector<std::pair<StringId, Variant>>` | 递归容器类型别名 | — |

- **不变量 / 顺序**：`Type` 枚举共 10 种（`kNull/kBool/kInt/kFloat/kStringId/kObjectID/kString/kArray/kDict/kVec3`）；union 的堆成员生命周期由 `destroyActive()` 手动管理，`type_` 必须与活跃成员一致。写入前先 `destroyActive()` 再置 tag（构造函数与 `set` 共用 `assign`）。
- **设计原因**：`Dict` 用**保序 `vector`** 而非 `unordered_map`，因为序列化输出必须逐字节可复现（`06-decisions.md` D5）；且 `StringId` 无 `operator<`，也不能用 `std::map`。
- **Godot 对照**：`core/variant/variant.h`、`variant_parser.h`。
- **遗留 / 已知缺口**：实际支持 9 种类型（不含规划草图里的 `kVec2/kVec4/kColor`）；无 COW、无深度限制、无 `Variant` 文本解析。

## PropertyInfo（`obj::PropertyInfo`）与属性标志

- **职责**：单个属性的元数据，每个类一份。
- **所有权 / 生命周期**：`name` / `class_hint` 是驻留 `StringId`；`getter` / `setter` 是函数指针，由注册代码提供，无所有权。
- **公开 API / 字段**：

  | 字段 / 类型 | 语义 / 契约 |
  |---|---|
  | `StringId name` | 属性名（驻留，比较 O(1)） |
  | `Variant::Type type` | 属性 Variant 类型，`set` 前校验用 |
  | `uint32_t flags` | `PropertyFlags` 位或 |
  | `StringId class_hint` | 对象类型属性的目标类名；其余留空 |
  | `PropertyGetter getter` | `Variant (*)(const Object*)`；`nullptr` = 只写属性 |
  | `PropertySetter setter` | `bool (*)(Object*, const Variant&)`；`nullptr` = 只读属性 |

  | 标志 | 含义 |
  |---|---|
  | `kNone = 0` | 无标志 |
  | `kEditable = 1<<0` | 编辑器 / 检视器可修改 |
  | `kSerialized = 1<<1` | 参与存档序列化 |
  | `kReadOnly = 1<<2` | 只读：`set_property` 必须拒绝 |

- **设计原因**：属性存取用"成员指针 + 类型擦除"（getter/setter 适配），避免虚函数爆炸（`01-architecture` §4.2）。
- **Godot 对照**：`core/object/property_info.h`。
- **遗留 / 已知缺口**：无。

## ClassInfo（`obj::ClassInfo`）

- **职责**：单个类的静态元数据；提供工厂、属性查找与继承查询。
- **所有权 / 生命周期**：`name_` 是驻留 `StringId`；`properties_` 是指向**静态 `PropertyInfo` 数组**的 span，该数组必须比 `ClassInfo` 活得更久（手工注册用函数内 static，避开 SIOF）。`factory` 返回裸指针，所有权 / 回收交给调用方（未来是 `Ref`）。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `name() const` | 类名 | — |
  | `parent() const` | 基类 `ClassInfo*`，根为 `nullptr` | — |
  | `properties() const` | **仅本类**属性表，不含继承 | — |
  | `factory() const` | 创建实例；`nullptr` = 抽象类 | — |
  | `findProperty(StringId) const` | 本类优先，未命中递归 `parent_`；派生类可遮蔽基类同名属性 | 找不到返回 `nullptr` |
  | `isA(const ClassInfo* base) const` | 沿 `parent_` 逐级比指针 | — |
  | `get_property(const Object*, StringId, Variant&) const` | 读属性到 `out` | 属性不存在 / 无 getter → `false` |
  | `set_property(Object*, StringId, const Variant&) const` | 写属性 | 属性不存在 / 无 setter / `kReadOnly` → `false`；类型不符由 setter 内部再拒 |

- **不变量 / 顺序**：`set_property` 先查 `kReadOnly` 再调 setter；`properties()` 不含继承，继承由 `findProperty` 递归实现。
- **设计原因**：静态元数据不可变、可 `constexpr` 构造；工厂用裸函数指针避免每类一个 vtable。
- **Godot 对照**：`core/object/class_db.h`、`method_bind.h`。
- **遗留 / 已知缺口**：无方法绑定；`get_property` / `set_property` 不做 `class_hint` 校验。

## ClassDB（`obj::ClassDB`）

- **职责**：进程级类注册表，`StringId 类名 → const ClassInfo*`（非拥有），与 `ObjectDB` 同构。
- **所有权 / 生命周期**：只登记各类注册函数创建的静态 `ClassInfo` 的地址；不可拷贝 / 移动。
- **公开 API**：

  | 签名 | 语义 / 契约 | 失败模式 |
  |---|---|---|
  | `registerClass(const ClassInfo&)` | 登记类 | `freeze` 之后 / 名字重复 → Debug assert |
  | `freeze() / isFrozen() noexcept` | 冻结：此后禁止注册 | — |
  | `getClass(StringId) const` | 按类名查 | 未注册返回 `nullptr` |
  | `instantiate(StringId) const` | 按类名创建实例 | 未注册 / 抽象类 → `nullptr`（所有权归调用方） |
  | `allClasses() const` | 所有已注册类名 | — |
  | `inheritorsOf(StringId) const` | 继承自该类的类名（不含自身） | 未注册返回空 |
  | `globalClassDB()` | 进程级默认注册表 | — |

- **不变量 / 顺序**：**注册只发生在启动期**，之后 `freeze()`，此后 `get` / 查询线程只读、无需加锁。
- **设计原因**：写入集中在启动期、读多写少，用冻结换取无锁读；显式 `register_core_classes()` 避开静态初始化顺序（`01-architecture` §4.2）。
- **Godot 对照**：`core/object/class_db.h` / `.cpp`、`core/register_core_types.cpp`。
- **遗留 / 已知缺口**：`inheritorsOf` 由遍历全部类实现（非索引）；无类方法（method）注册。

## 核心类注册（`obj::register_core_classes`）

- **职责**：显式注册引擎核心类，`main` 最早处调用一次。
- **公开 API**：`void register_core_classes()`。注册 `Object` 本身（无属性、无工厂）。游戏 / 插件类在它之后注册，注册完成后由调用方决定何时 `globalClassDB().freeze()`。
- **设计原因**：函数内 static `ClassInfo`，首次调用创建一次，避开 SIOF。
- **遗留 / 已知缺口**：当前仅注册 `Object`。
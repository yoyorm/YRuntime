# Benchmarks

Benchmark 是手动运行的性能实验，不属于 Runtime 产品，也不注册到 `ctest`。默认配置下
`YR_BUILD_BENCHMARKS=OFF`，因此常规构建和 CI 不会编译或运行它。

## 构建与运行

```bash
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/benchmarks/YrBench_slot_map
```

`benchmark` preset 使用独立的 `build/benchmark/`，不会修改日常 `release` 构建目录的 CMake cache。

## E1 · SlotMap

比较对象：

- `yr::core::SlotMap<Payload>`：稠密数据 + slot/generation 元数据。
- `std::unordered_map<uint64_t, Payload>`：通用哈希表基线。
- `vector<optional<Payload>> + freelist`：直接 slot 存储基线，有空洞且不提供 generation 安全。

工作负载：插入、随机查找、按固定随机顺序删除 50% 元素、完整遍历。三种容器使用相同 Payload 和随机操作顺序；随机输入生成、容器准备和结果输出不计入对应操作的计时。删除实验只计时删除过程，剩余元素校验不计时。每项预热 3 次、正式运行 7 次，按元素规模分组输出以秒为单位的中位数、最小值、最大值和 checksum。

正式记录结论时补充 CPU、编译器版本、Release flags，以及 `perf stat` 的 cache 数据。

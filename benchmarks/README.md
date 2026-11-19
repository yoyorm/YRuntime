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

## E11 · JobSystem

比较对象：

- 串行基线：同一个 `burn(index)` 循环，无线程与队列开销。
- `submit` 每任务一次 + `waitAll()`。
- `parallelFor(tasks, 1, fn)`：每元素一个批次（最细粒度）。

工作负载：每个任务执行固定次数的 xorshift（默认 4096 次，约 3~4 µs）。规模 1k / 10k 任务，线程数取 1/2/4/8（不超过 `hardware_concurrency()`）；每项取 5 次采样的最小值，并校验 checksum 与串行一致。另做 batch size 扫描（1/8/64/512/4096），观察"任务太小时提交与唤醒开销吃掉收益"。

运行：

```bash
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/benchmarks/YrBench_jobs
```

样例（12 核容器，单任务 ~3.35 µs）：

```text
tasks  threads  serial ms  submit ms  speedup  pfor(1) ms  speedup
10000        1       34.1       38.9      0.9        38.9      0.9
10000        2       34.1       25.7      1.3        26.0      1.3
10000        4       34.1       15.6      2.2        16.0      2.1
10000        8       34.1        9.5      3.6        10.6      3.2
```

解读要点：任务粒度只有 ~3 µs 时，8 线程加速比约 3.6x，瓶颈在提交/唤醒而不是计算（roadmap 的 ≥5x 指标对应 ~100 µs 的任务；要复现可提高每任务计算量）。batch size 扫描显示过大批次会退化为串行（4096 时只剩 3 个批次）。

正式记录时补充 CPU 型号、编译器版本与 Release flags。

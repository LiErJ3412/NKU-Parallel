# PCFG 并行化探索记录

## 1. 实验要求梳理

- `2026并行程序设计Lab3_1_Pthread编程_password.pdf` 中将口令猜测任务定位为 PCFG 生成阶段的多线程优化，代码注释也直接标出 `guessing.cpp` 中 `PriorityQueue::Generate()` 的两个循环是主要并行化入口。
- `pthread-omp-LU.pptx` 中说明 ANN、NTT、口令猜测基础要求上限为 90%，内容类似高斯消去：需要任务分配算法、性能分析、串行/并行对比、不同线程数测试，并讨论 Pthread 与 OpenMP 的差异。
- 输出中的 `Guess time:`、`Hash time:`、`Train time:` 被代码标注为不要修改，因此实现保持这三行文本不变。

## 2. 现有代码结构

- `guess_mac/PCFG.h` 定义 `segment`、`PT`、`model` 和 `PriorityQueue`。
- `train.cpp` 负责读取 `rockyou.txt`、切分口令、统计 PT 和 segment value，并按频率排序。
- `guessing.cpp` 负责优先队列弹出、PT 扩展和候选口令生成。
- `main.cpp` 负责训练、初始化优先队列、循环生成 guesses，并定期调用 MD5/SIMD 哈希。

最关键的热点在 `PriorityQueue::Generate()`：

1. 当前 PT 除最后一个 segment 外的取值已经由 `curr_indices` 固定。
2. 最后一个 segment 的所有 `ordered_values[i]` 都可以独立拼接为候选口令。
3. 这些候选属于同一个 PT，概率相同，且按 `ordered_values` 顺序输出即可。

## 3. 论文启发

### Parallel_PCFG 论文

论文指出 PCFG 不能简单把多个 PT 并行弹出，因为这会破坏候选口令按概率降序生成的语义。它选择将并行粒度放在 candidate password 层，并通过 lookup/storage 让不同线程定位自己要生成的候选。

本次实现采用同样的核心思想，但结合课程代码简化为 CPU 版：

- 保留全局 `priority` 串行 `PopNext()`，确保 PT 出队顺序准确。
- 只并行同一个 PT 内部的最后一个 segment 展开。
- 每个线程处理一段连续 index，生成 `prefix + ordered_values[i]`。

### MultiQueue 论文

MultiQueue 论文讨论 relaxed concurrent priority queue，用多个局部队列换取吞吐量，并用 rank error/delay 描述质量损失。该思路可用于更激进的 PCFG 多 PT 并行弹出，但会引入概率顺序误差。

课程任务更重视正确性与可对比实验，因此本次没有把 `priority` 改成 relaxed 多队列，而是在报告中作为后续优化方向讨论。

## 4. 实现决策

### 生成模式

新增三种模式：

- `serial`：串行生成，用作 correctness 和性能基线。
- `pthread`：Pthread 静态分块生成，默认模式。
- `openmp`：OpenMP `parallel for schedule(static)` 生成。

运行时通过环境变量配置：

```sh
PCFG_GENERATE_MODE=serial|pthread|openmp
PCFG_THREADS=1|2|4|8
PCFG_GENERATE_THRESHOLD=50000
PCFG_TRAIN_LIMIT=3000000
PCFG_GUESS_LIMIT=10000000
```

### 并行写入策略

原始代码在循环中调用 `guesses.emplace_back()`，如果多线程直接使用会产生数据竞争。新实现改为：

1. 记录 `base = guesses.size()`。
2. `guesses.resize(base + n)` 预留输出位置。
3. 每个线程只写自己的 `[begin, end)` 区间：

```cpp
guesses[base + i] = prefix + ordered_values[i];
```

这样不需要给 `guesses` 加锁，也不会改变输出顺序。

### 任务划分

- Pthread：线程数为 `min(PCFG_THREADS, n)`，区间为 `n * tid / thread_count` 到 `n * (tid + 1) / thread_count`。
- OpenMP：使用 `schedule(static)`，保持相同的静态均分思想。
- 当 `n < PCFG_GENERATE_THRESHOLD` 或线程数为 1 时自动走串行，避免小任务线程创建成本超过收益。实测默认阈值取 `50000` 比 `4096` 更稳。

### 概率修正

探索中发现 `segment::order()` 会把 `ordered_freqs` 和 `total_freq` 累加两遍。虽然 value 没重复，但概率分母翻倍，会影响 PT 回插优先级。已改为排序前清空统计输出，并只累加一次。

## 5. 代码修改清单

### `guess_mac/PCFG.h`

- 新增 `GenerateMode` 枚举，用于区分 `Serial`、`Pthread`、`OpenMP` 三种候选口令生成模式。
- 在 `PriorityQueue` 中新增生成阶段配置项：
  - `generate_mode`
  - `generate_threads`
  - `generate_threshold`
- 新增 `ConfigureGeneration()` 接口，让 `main.cpp` 可以根据环境变量配置生成模式、线程数和并行阈值。

报告可写：这一部分是为了把原本写死的串行生成逻辑改造成可切换、可对比的实验框架，方便做串行、Pthread、OpenMP 三组对照实验。

### `guess_mac/guessing.cpp`

- 引入 `pthread.h`，并在 `_OPENMP` 存在时引入 `omp.h`。
- 新增 `GenerateTask`、`GenerateRange()`、`GenerateRangePthread()`，用于描述每个线程负责的连续 index 区间。
- 新增 `PriorityQueue::ConfigureGeneration()` 的实现。
- 重写 `PriorityQueue::Generate()` 的候选生成部分：
  - 先构造当前 PT 中已固定 segment 的 `prefix`。
  - 定位最后一个 segment 的 `ordered_values`。
  - 用 `guesses.resize(base + n)` 预分配输出空间。
  - 串行、Pthread、OpenMP 三种模式都写入同样的目标位置 `guesses[base + i]`。
- Pthread 模式按 `n * tid / thread_count` 静态划分 `[begin, end)`，每个线程只写自己的区间。
- OpenMP 模式使用 `#pragma omp parallel for schedule(static)`。
- 当候选数量小于 `generate_threshold` 或线程数为 1 时回退串行。

这里是核心并行化改动。原始代码在循环中 `emplace_back()`，不能直接并行；改为预分配加定址写入后，每个线程只写独立区间，因此不需要对 `vector` 加锁，同时保持候选输出顺序不变。

### `guess_mac/main.cpp`

- 新增环境变量读取函数 `ReadEnvInt()`。
- 新增 `ReadGenerateMode()` 和 `GenerateModeName()`，支持通过 `PCFG_GENERATE_MODE` 切换运行模式。
- 在创建 `PriorityQueue q` 后读取并应用以下配置：
  - `PCFG_GENERATE_MODE=serial|pthread|openmp`
  - `PCFG_THREADS=N`
  - `PCFG_GENERATE_THRESHOLD=N`
  - `PCFG_GUESS_LIMIT=N`
- 将原来写死的 `10000000` 生成上限改为可由 `PCFG_GUESS_LIMIT` 控制，默认仍为 `10000000`。
- 保留实验要求中标注不要修改的输出行：
  - `Guess time:`
  - `Hash time:`
  - `Train time:`

这一部分主要是为了实验自动化和性能对比，不改变 PCFG 算法本身，只让同一个程序可以通过环境变量跑不同线程数和不同并行后端。

### `guess_mac/train.cpp`

- 新增 `PCFG_TRAIN_LIMIT` 环境变量，训练集读取上限默认仍为 `3000000`。
- 修正训练上限判断位置：原来只在每 10000 行打印时判断，可能多读一段；现在每行读取后都检查。
- 修复 `segment::order()` 中 `ordered_freqs` 和 `total_freq` 被累加两遍的问题。
- 在 `segment::order()` 开头清空 `ordered_values`、`ordered_freqs`，并重置 `total_freq`，避免重复调用时累积旧数据。
- 为同频 segment value 增加字典序 tie-breaker，使不同编译器下排序结果更稳定。
- 将 `ordered_pts` 排序改为 `stable_sort`，减少同概率 PT 的顺序抖动。

这些属于正确性和可复现实验修正。尤其是 `total_freq` 重复累加会影响概率计算，进而影响优先队列中 PT 的回插顺序；稳定排序则保证串行、Pthread、OpenMP 的输出更容易对齐比较。

### `guess_mac/Makefile`

- 新增统一构建入口：
  - `make pthread` 生成 `main_pthread`
  - `make openmp` 生成 `main_openmp`
  - `make serial` 生成 `main_serial`
- Pthread 使用默认 `g++`/Apple clang 加 `-pthread`。
- OpenMP 使用 `/opt/homebrew/bin/g++-14 -fopenmp`，因为当前机器默认 Apple clang 没有直接配置 Homebrew `libomp`。

报告可写：Makefile 用于保证实验命令可复现，并明确区分 Pthread 和 OpenMP 编译工具链。

### `PCFG_parallel_exploration.md`

- 新增实验要求梳理、论文启发、实现决策、编译命令、运行命令、性能数据和本修改清单。
- 记录实测加速比：
  - 小规模生成阶段约 `1.25x`，耗时减少约 `19.8%`。
  - 默认规模附近生成阶段约 `1.14x`，耗时减少约 `12.4%`。

## 6. 编译方式

在 `guess_mac` 下：

```sh
make pthread
make openmp
make serial
```

单独编译命令：

```sh
g++ -std=c++17 -O2 -pthread main.cpp train.cpp guessing.cpp md5.cpp -o main_pthread
/opt/homebrew/bin/g++-14 -std=c++17 -O2 -fopenmp main.cpp train.cpp guessing.cpp md5.cpp -o main_openmp
g++ -std=c++17 -O2 -DUSE_SERIAL_HASH -pthread main.cpp train.cpp guessing.cpp md5.cpp -o main_serial
```

当前机器默认 `g++` 是 Apple clang，Pthread 可直接使用；OpenMP 需要 Homebrew GCC 14。

## 7. 实验命令模板

快速正确性测试：

```sh
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=serial PCFG_THREADS=1 ./main_pthread
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 ./main_pthread
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 ./main_openmp
```

正式性能测试建议：

```sh
for mode in serial pthread; do
  for t in 1 2 4 8; do
    PCFG_GENERATE_MODE=$mode PCFG_THREADS=$t ./main_pthread
  done
done
for t in 1 2 4 8; do
  PCFG_GENERATE_MODE=openmp PCFG_THREADS=$t ./main_openmp
done
```

OpenMP 模式应使用 `main_openmp`。

## 8. 报告可写结论

### 实测改进幅度

测试平台为当前 ARM macOS，训练集为 `../../guess_data/rockyou.txt`，哈希仍使用已有 SIMD 路径。以下加速比只按程序输出的 `Guess time` 计算，即只评价候选口令生成阶段，不包含训练和 MD5 哈希。

| 训练上限 | 生成上限 | 模式 | 线程/阈值 | Guess time | 相对串行 |
| --- | --- | --- | --- | ---: | ---: |
| 200000 | 2000000 | serial | 1 / 4096 | 0.071087s | 1.00x |
| 200000 | 2000000 | pthread | 4 / 4096 | 0.088619s | 0.80x |
| 200000 | 2000000 | openmp | 2 / 4096 | 0.075404s | 0.94x |
| 200000 | 2000000 | pthread | 4 / 50000 | 0.056980s | 1.25x |
| 3000000 | 10000000 | serial | 1 / 4096 | 0.194687s | 1.00x |
| 3000000 | 10000000 | pthread | 4 / 50000 | 0.170515s | 1.14x |

换算成百分比，在小规模测试中，调优后的 Pthread 生成阶段从 `0.071087s` 降到 `0.056980s`，耗时减少约 `19.8%`，加速比约 `1.25x`。在课程默认规模附近，生成阶段从 `0.194687s` 降到 `0.170515s`，耗时减少约 `12.4%`，加速比约 `1.14x`。

需要注意的是，默认 `4096` 阈值下 Pthread 反而变慢：`0.071087s -> 0.088619s`。原因是当前实现每次大 PT 展开都会创建和回收线程，许多 PT 的最后一个 segment value 数量不足以摊薄线程创建开销。因此最终采用 `50000` 作为默认阈值，只让足够大的展开任务进入并行路径。

- 本次并行化没有改变 PCFG 的全局优先队列语义，因此可以保持候选口令概率顺序。
- Pthread 和 OpenMP 的计算工作量相同，差异主要来自线程创建/调度开销和编译器运行时。
- 对小 PT，串行更快；对最后一个 segment value 数量较大的 PT，静态分块可以降低生成阶段耗时。
- `guesses.resize + index write` 是避免锁竞争的关键，否则并行 `push_back` 会让 vector 成为共享热点。
- 进一步优化可考虑持久线程池、批量生成与 SIMD hash pipeline 融合，或基于 MultiQueue 的 relaxed 多 PT 并行。

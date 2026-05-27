# OpenHarmony 服务器版 PCFG 并行改动记录

## 修改目标

参考 `guess_mac` 中已经验证的 PCFG 生成阶段并行优化，改进 `guess` 文件夹下的服务器版实现。服务器版保留 `PCFG.h` 中的 `#include <omp.h>`，方便使用 OpenMP 编译运行。

## 代码修改清单

### `PCFG.h`

- 保留原有 `#include <omp.h>`。
- 新增 `GenerateMode` 枚举：
  - `Serial`
  - `Pthread`
  - `OpenMP`
- 在 `PriorityQueue` 中新增：
  - `ConfigureGeneration(...)`
  - `generate_mode`
  - `generate_threads`
  - `generate_threshold`

### `guessing.cpp`

- 新增 Pthread 任务结构 `GenerateTask`。
- 新增 `GenerateRange()` 和 `GenerateRangePthread()`。
- 重写 `PriorityQueue::Generate()`：
  - 先拼出当前 PT 已固定部分的 `prefix`。
  - 找到最后一个 segment 的 `ordered_values`。
  - 用 `guesses.resize(base + n)` 预分配输出空间。
  - 每个线程写自己的 `guesses[base + i]`，避免并行 `emplace_back()` 的数据竞争。
  - 支持 `serial`、`pthread`、`openmp` 三种模式。
  - 当任务规模小于 `generate_threshold` 时自动回退串行。

### `main.cpp`

- 新增环境变量控制：
  - `PCFG_GENERATE_MODE=serial|pthread|openmp`
  - `PCFG_THREADS=N`
  - `PCFG_GENERATE_THRESHOLD=N`
  - `PCFG_TRAIN_LIMIT=N`
  - `PCFG_GUESS_LIMIT=N`
- 默认训练路径仍为服务器路径 `/guessdata/Rockyou-singleLined-full.txt`。
- 默认生成上限仍为 `10000000`。
- 保持以下输出文本不变：
  - `Guess time:`
  - `Hash time:`
  - `Train time:`

### `train.cpp`

- 新增 `PCFG_TRAIN_LIMIT` 环境变量。
- 修复训练上限只在每 10000 行检查的问题，现在每行都检查。
- 修复 `segment::order()` 中 `ordered_freqs` 和 `total_freq` 重复累加的问题。
- 同频 value 增加字典序 tie-breaker，减少不同编译器下的输出顺序差异。
- `ordered_pts` 使用 `stable_sort`。

### `md5.cpp`

- 将 `MD5Hash` 定义改为 `void MD5Hash(const string &input, bit32 *state)`，与 `md5.h` 声明一致，避免 GNU g++ 链接时报未定义符号。

## 编译验证

本地用 GNU g++-14 完成语法和链接验证：

```sh
/opt/homebrew/bin/g++-14 -std=c++17 -O2 -fopenmp -pthread main.cpp train.cpp guessing.cpp md5.cpp -o main_openmp_check
/opt/homebrew/bin/g++-14 -std=c++17 -O2 -fopenmp -pthread correctness.cpp train.cpp guessing.cpp md5.cpp -o correctness_check
```

本机没有 `/guessdata/Rockyou-singleLined-full.txt`，因此完整运行需要放到服务器上测试。

## 服务器运行建议

如果服务器脚本允许传递环境变量，可用：

```sh
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 ./test.sh
PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 ./test.sh
PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 ./test.sh
```

正式测试可以去掉 `PCFG_TRAIN_LIMIT` 和 `PCFG_GUESS_LIMIT`，使用默认的 300 万训练上限和 1000 万生成上限。

## 注意事项

- 不要删除 `PCFG.h` 中的 `#include <omp.h>`。
- 不修改 `test.sh` 和 `qsub.sh`。
- OpenMP 模式需要服务器编译命令带 `-fopenmp`。
- 如果 Pthread 线程数增大后变慢，可以调大 `PCFG_GENERATE_THRESHOLD`，例如：

```sh
PCFG_GENERATE_THRESHOLD=50000
```

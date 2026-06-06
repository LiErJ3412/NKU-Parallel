# PCFG MPI 流水线探索记录

## 目标

`guess_pipeline` 是从 `guess_mac` 复制出的进阶探索版本。本次尝试对应 `mpi.pptx` 中“更多探索：MPI 并行化的不同算法策略（如块划分、循环划分等不同任务划分方法，流水线算法等）”这一要求。

已完成的 `guess_mac` 版本采用 MPI 进程间 PT 批分发：rank 0 训练模型、维护全局优先队列并分发 PT；各 worker rank 本地生成 guesses 并执行 MD5 SIMD 哈希；worker 返回生成数量和哈希耗时。该版本的主要开销已经不完全来自 MPI 通信，局部生成和哈希之间仍是串行关系：

```cpp
q.Generate(task);
HashGuesses(q.guesses);
```

因此本次探索的问题是：能否在每个 MPI rank 内部把“候选口令生成”和“MD5 哈希”做成 producer/consumer 流水线，让哈希线程处理上一批 guesses 时，主线程继续生成下一批 guesses，从而降低暴露在总 wall time 中的等待开销。

## 实现变更

只修改 `guess_pipeline/main.cpp`，没有改动 `guess_mac`。

1. 增加头文件：

```cpp
#include <condition_variable>
#include <deque>
#include <mutex>
```

2. 新增 `ProcessLocalTasksPipeline`：

- producer：当前 MPI rank 的主线程继续调用原有 `q.Generate(task)`，生成出的 `q.guesses` 达到阈值后用 `swap` 交给队列。
- consumer：新增一个 `std::thread`，从有界队列中取出 `vector<string>` 批次并调用 `HashGuesses(batch)`。
- 队列同步：使用 `std::mutex` + `std::condition_variable` + `deque<vector<string>>`。
- 内存控制：用 `PCFG_PIPELINE_QUEUE` 限制排队批次数，默认 2。
- 数据竞争规避：`PriorityQueue q` 只由 producer 线程访问，consumer 只处理已经 `swap` 出来的独立 `vector<string>`。

3. 新增环境变量开关：

| 环境变量 | 默认值 | 作用 |
| --- | ---: | --- |
| `PCFG_PIPELINE` | `0` | 是否启用 rank 内生成/哈希流水线。只有 `PCFG_MPI_HASH=1` 时生效 |
| `PCFG_PIPELINE_BATCH` | `PCFG_HASH_BATCH` 或 `1000000` | producer 累积多少 guesses 后交给 consumer |
| `PCFG_PIPELINE_QUEUE` | `2` | producer/consumer 队列最多缓存多少个批次 |

4. `RunMPI` 中保留原有路径：

```cpp
if (pipeline && mpi_hash)
{
    ProcessLocalTasksPipeline(q, assigned[rank], local_generated, local_hash_time);
}
else
{
    ProcessLocalTasks(q, assigned[rank], mpi_hash, local_generated, local_hash_time);
}
```

这保证 `PCFG_PIPELINE=0` 时仍是原 MPI 基线，便于 A/B 对比。

## 测试环境

- 工作目录：`/Users/lierj/Desktop/大三下课程/并行设计/homework5/guess_pipeline`
- 数据集：`../../guess_data/rockyou.txt`，大小约 307 MB
- 系统：Darwin ARM64，`Darwin Kernel Version 22.6.0`
- MPI：MacPorts Open MPI 5.0.7，`/opt/local/bin/mpirun`
- 编译器：`/opt/local/bin/mpicxx`
- 构建命令：

```bash
make mpi
make pthread
```

编译结果：

```text
/opt/local/bin/mpicxx -std=c++17 -O2 -DUSE_MPI -pthread main.cpp train.cpp guessing.cpp md5.cpp -o main_mpi
c++ -std=c++17 -O2 -pthread main.cpp train.cpp guessing.cpp md5.cpp -o main_pthread
```

## 正确性和烟雾测试

### Pthread 默认全量参考

命令：

```bash
./main_pthread
```

结果：

| 指标 | 数值 |
| --- | ---: |
| 生成数量 | 10,096,227 |
| Guess time | 0.130715 s |
| Hash time | 0.979133 s |
| Train time | 8.2886 s |

该结果与 `guess_mac` 之前记录的 pthread 8 线程结果接近，可作为本次测试机器状态参考。

### MPI 小规模非流水线

命令：

```bash
PCFG_TRAIN_LIMIT=5000 \
PCFG_GUESS_LIMIT=50000 \
PCFG_GENERATE_MODE=serial \
PCFG_THREADS=1 \
PCFG_MPI_HASH=1 \
PCFG_MPI_BATCH=2 \
PCFG_MPI_BATCH_GUESS_LIMIT=50000 \
/opt/local/bin/mpirun -np 2 ./main_mpi
```

结果：

| 指标 | 数值 |
| --- | ---: |
| MPI pipeline | off |
| 生成数量 | 50,002 |
| MPI communication time | 0.008400 s |
| Guess time | 0.005744 s |
| Hash time | 0.008774 s |
| Train time | 0.007615 s |

### MPI 小规模流水线

命令：

```bash
PCFG_TRAIN_LIMIT=5000 \
PCFG_GUESS_LIMIT=50000 \
PCFG_GENERATE_MODE=serial \
PCFG_THREADS=1 \
PCFG_MPI_HASH=1 \
PCFG_PIPELINE=1 \
PCFG_PIPELINE_BATCH=10000 \
PCFG_PIPELINE_QUEUE=2 \
PCFG_MPI_BATCH=2 \
PCFG_MPI_BATCH_GUESS_LIMIT=50000 \
/opt/local/bin/mpirun -np 2 ./main_mpi
```

结果：

| 指标 | 数值 |
| --- | ---: |
| MPI pipeline | on |
| 生成数量 | 50,002 |
| MPI communication time | 0.005196 s |
| Guess time | 0.009332 s |
| Hash time | 0.004983 s |
| Train time | 0.006543 s |

结论：生成数量与非流水线一致，说明新路径可以正确退出并完成同等规模任务。小规模下耗时波动较大，只用于验证功能，不用于性能结论。

## 4 进程 1000 万规模 A/B 对比

统一参数：

```bash
PCFG_TRAIN_LIMIT=3000000
PCFG_GUESS_LIMIT=10000000
PCFG_GENERATE_MODE=serial
PCFG_THREADS=1
PCFG_MPI_BATCH=16
PCFG_MPI_BATCH_GUESS_LIMIT=1000000
/opt/local/bin/mpirun -np 4 ./main_mpi
```

### 非流水线基线

额外参数：无，`PCFG_PIPELINE=0`。

| 指标 | 数值 |
| --- | ---: |
| MPI pipeline | off |
| 生成数量 | 10,488,529 |
| MPI communication time | 0.202064 s |
| Guess time | 0.126278 s |
| Hash time | 0.516046 s |
| Train time | 9.37733 s |

### 流水线：batch = 200000

额外参数：

```bash
PCFG_PIPELINE=1
PCFG_PIPELINE_BATCH=200000
PCFG_PIPELINE_QUEUE=2
```

| 指标 | 数值 |
| --- | ---: |
| MPI pipeline | on |
| 生成数量 | 10,488,529 |
| MPI communication time | 0.219111 s |
| Guess time | 0.155433 s |
| Hash time | 0.520908 s |
| Train time | 9.42670 s |

### 流水线：batch = 1000000

额外参数：

```bash
PCFG_PIPELINE=1
PCFG_PIPELINE_BATCH=1000000
PCFG_PIPELINE_QUEUE=2
```

| 指标 | 数值 |
| --- | ---: |
| MPI pipeline | on |
| 生成数量 | 10,488,529 |
| MPI communication time | 0.227227 s |
| Guess time | 0.167167 s |
| Hash time | 0.535146 s |
| Train time | 9.56908 s |

### 流水线：batch = 50000

额外参数：

```bash
PCFG_PIPELINE=1
PCFG_PIPELINE_BATCH=50000
PCFG_PIPELINE_QUEUE=2
```

| 指标 | 数值 |
| --- | ---: |
| MPI pipeline | on |
| 生成数量 | 10,488,529 |
| MPI communication time | 0.219876 s |
| Guess time | 0.132954 s |
| Hash time | 0.549896 s |
| Train time | 9.68217 s |

## 分析

从 4 进程 1000 万规模结果看，当前 rank 内生成/哈希流水线没有带来正收益：

| 模式 | 生成数量 | 通信时间 | Guess time | Hash time | Guess + Hash |
| --- | ---: | ---: | ---: | ---: | ---: |
| 非流水线 | 10,488,529 | 0.202064 s | 0.126278 s | 0.516046 s | 0.642324 s |
| 流水线 batch 200000 | 10,488,529 | 0.219111 s | 0.155433 s | 0.520908 s | 0.676341 s |
| 流水线 batch 1000000 | 10,488,529 | 0.227227 s | 0.167167 s | 0.535146 s | 0.702313 s |
| 流水线 batch 50000 | 10,488,529 | 0.219876 s | 0.132954 s | 0.549896 s | 0.682850 s |

主要原因：

1. 在当前实现中，`Generate` 的开销相对 MD5 SIMD 哈希较小。非流水线时 4 进程基线的 Guess time 只有 0.126278 s，而 Hash time 为 0.516046 s，可被流水线隐藏的生成时间上限本来就很低。
2. producer/consumer 需要额外线程、mutex、condition_variable、队列管理和 `vector<string>` 批次转移。对于本实验中的短字符串 guesses，这些同步和调度开销足以抵消有限重叠收益。
3. MPI 外层仍采用同步轮次：rank 0 分发一批 PT，所有 rank 本地处理完并返回结果后才进入下一轮。因此本次局部流水线没有隐藏 rank 0 分发和结果收集阶段的通信等待。
4. 批大小存在明显折中：`batch=50000` 增加同步频率，哈希时间升高；`batch=1000000` 同步较少但重叠机会下降，整体也慢；`batch=200000` 居中但仍慢于非流水线。

## 结论

本次探索实现了可开关的 rank 内生成/哈希流水线，并通过 2 进程小规模测试验证了正确性。对 4 进程、300 万训练、1000 万猜测规模的实测表明，该流水线版本没有降低总开销，最佳流水线配置仍慢于非流水线基线。

因此，`guess_pipeline` 中保留 `PCFG_PIPELINE=1` 作为进阶探索代码和报告材料，但不建议把它作为最终性能路径默认启用。更有价值的后续方向应集中在 MPI 层面的通信隐藏，例如使用非阻塞 `MPI_Isend/Irecv` 预取下一批 PT、让 rank 0 的优先队列扩展与 worker 计算重叠，或调整任务划分以降低轮次同步等待。


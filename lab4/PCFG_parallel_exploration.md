# PCFG MPI 并行化探索记录

## 1. Lab4 要求梳理

- `2026并行程序设计Lab4_MPI编程.pdf` 和 `mpi.pptx` 要求在口令猜测选题中实现 MPI 多进程并行化，基础要求上限为 90%。
- 关键要求不是让多个进程重复执行同一任务，而是把一个口令猜测任务拆给多个 MPI 进程共同完成。
- 进阶方向包括：MPI 与 SIMD/多线程结合、不同通信方式对比、不同任务划分策略、流水线生成与哈希。
- 代码中原有三行输出仍保持不变：
  - `Guess time:`
  - `Hash time:`
  - `Train time:`

## 2. 保留与重写决策

上次实验遗留代码已经包含：

- PCFG 训练、排序和优先队列生成逻辑。
- 单 PT 内最后一个 segment 展开的 Pthread/OpenMP 并行。
- MD5 的标量和 SIMD 哈希路径。
- 通过环境变量切换模式、线程数、生成上限和训练上限。

本次没有大规模重写这些稳定部分，而是新增 MPI 调度层：

- rank 0 负责训练模型、初始化并维护全局优先队列。
- 每轮从优先队列中取出一批 PT，通过 MPI 分给不同 rank。
- 各 rank 只生成自己负责的 PT，并在本地调用 MD5/SIMD 哈希。
- 各 rank 返回生成数量和哈希耗时。
- rank 0 基于本轮取出的 PT 生成新 PT，重新计算概率并回插优先队列。

这样符合课件中“尝试一次性从优先队列中取出多个 PT 并同时进行口令生成”的进阶提示，同时避免所有 MPI 进程重复做同一批口令。

## 3. 实现策略

### 多 PT 静态分配

MPI 模式下每轮取出 `PCFG_MPI_BATCH` 个 PT，默认值为 `MPI进程数 * 4`。rank 0 按 `i % size` 把 PT 分给各进程。

优点：

- 实现清晰，通信协议简单。
- 全局优先队列只在 rank 0 修改，不需要分布式锁。
- 每个 PT 的候选生成仍可复用原来的 Pthread/OpenMP/serial 后端。

代价：

- 一轮内多个 PT 并行生成，严格概率顺序会在批内部被放宽。
- 批大小越大，吞吐潜力越高，但概率顺序误差和通信合并延迟越大。

### 通信内容

每个 PT 发送以下信息：

- segment 的 `type` 和 `length`
- `curr_indices`
- `max_indices`
- `pivot`

rank 0 训练出的模型中，生成阶段只需要 segment 的 `ordered_values`，因此 MPI 启动后 rank 0 会广播 letters/digits/symbols 的 segment value 表。概率相关的频率表只保留在 rank 0，用于新 PT 回插时计算概率。

### 进程内并行

MPI 负责进程间 PT 分配；单个进程内部继续复用上次实验的生成策略：

```sh
PCFG_GENERATE_MODE=serial|pthread|openmp
PCFG_THREADS=1|2|4|8
PCFG_GENERATE_THRESHOLD=50000
```

哈希阶段继续复用 SIMD MD5。`main_serial` 使用标量 MD5 作为对照。

## 4. 编译与运行

在 `guess_mac` 目录下：

```sh
make serial
make pthread
make openmp
make mpi
```

MPI 编译命令由 Makefile 管理：

```sh
mpicxx -std=c++17 -O2 -DUSE_MPI -pthread main.cpp train.cpp guessing.cpp md5.cpp -o main_mpi
```

本机使用 MacPorts Open MPI，工具链位于 `/opt/local/bin`。Makefile 会优先查找 PATH 中的 `mpicxx`，找不到时回退到 `/opt/local/bin/mpicxx` 或 Homebrew 的 `/opt/homebrew/bin/mpicxx`。

快速测试示例：

```sh
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 ./main_pthread
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 mpirun -np 2 ./main_mpi
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_MPI_BATCH=16 mpirun -np 4 ./main_mpi
```

MPI 烟测可关闭 hash，用于快速验证任务分发和通信协议：

```sh
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_MPI_HASH=0 \
PCFG_MPI_BATCH=4 PCFG_MPI_BATCH_GUESS_LIMIT=200000 \
/opt/local/bin/mpirun -np 2 ./main_mpi
```

正式实验建议固定训练上限和生成上限，测试：

- `np=1,2,4,8`
- `PCFG_MPI_BATCH=size, size*2, size*4, size*8`
- `PCFG_MPI_BATCH_GUESS_LIMIT=100000,500000,1000000`
- `PCFG_GENERATE_MODE=serial,pthread,openmp`
- `PCFG_THREADS=1,2,4`

## 5. 性能指标说明

- `Train time`：rank 0 训练和排序模型的时间。训练不并行化。
- `Hash time`：MPI 模式下按每轮各 rank 哈希耗时最大值累加，反映并行墙钟瓶颈。
- `Guess time`：MPI 模式下为总墙钟时间减去 `Hash time`，包含 PT 分发、生成、新 PT 回收和队列回插。
- `MPI communication time`：rank 0 统计的任务发送与结果回收通信时间，用于报告分析通信开销。
- `MPI generated guesses`：MPI 模式下所有 rank 合计生成的候选口令数量。
- `PCFG_MPI_HASH=0` 只建议用于调度烟测；正式性能实验应保持默认 hash 开启。

## 6. 报告可写结论

- 本次 MPI 并行化把任务粒度从单 PT 内部 value 展开提升到多 PT 并行生成，更符合 MPI 多进程分布式任务分配。
- 保留 Pthread/OpenMP/SIMD 后端后，可以测试 MPI-only、MPI+Pthread、MPI+OpenMP、MPI+SIMD 的组合效果。
- rank 0 集中维护优先队列简化了正确性和通信协议，但队列回插仍是串行瓶颈。
- `PCFG_MPI_BATCH` 是关键参数：小批次概率顺序更接近串行，大批次通信摊销更好但顺序误差更明显。
- 进一步优化可尝试非阻塞通信、生成/哈希流水线、持久 worker 循环、按 PT 预计候选数做负载均衡。

## 7. 代码修改清单

### `guess_mac/main.cpp`

- 新增 `USE_MPI` 条件编译路径，普通 `main_pthread/main_serial/main_openmp` 保持单进程逻辑，`main_mpi` 走 MPI 逻辑。
- 新增 `MPI_Init/MPI_Finalize`、`MPI_Comm_rank/MPI_Comm_size`，只让 rank 0 输出主要日志和三行实验指标。
- 新增 `RunMPI()`：
  - rank 0 训练模型、排序、初始化全局 `PriorityQueue`。
  - rank 0 广播生成阶段必需的 segment value 表。
  - rank 0 每轮取出一批 PT，按 `i % size` 静态分发给各 rank。
  - 各 rank 本地调用 `PriorityQueue::Generate()` 生成候选，并调用 SIMD/标量 MD5 计算 hash。
  - rank 0 收集各 rank 的生成数量和 hash 时间，更新全局统计。
  - rank 0 对取出的 PT 调用 `NewPTs()`，重新计算概率并回插优先队列。
- 新增 PT 序列化/反序列化：
  - `PackPT()` / `UnpackPT()`
  - `PackPTList()` / `UnpackPTList()`
  - `SendTasks()` / `RecvTasks()`
- 新增模型生成数据广播：
  - `BroadcastSegmentValues()`
  - `BroadcastModelForGenerate()`
- 新增 MPI 运行参数：
  - `PCFG_MPI_BATCH`：每轮最多取出的 PT 数量，默认 `MPI进程数 * 4`。
  - `PCFG_MPI_BATCH_GUESS_LIMIT`：每轮估计候选数量上限，默认 `1000000`。
  - `PCFG_MPI_HASH=0|1`：是否在 MPI 路径中执行 hash，默认开启；关闭只用于烟测。
- 修复 MPI 终止协议：rank 0 达到生成上限后广播停止信号，避免 worker 等待下一轮任务。
- 新增 `ReadEnvBool()`，允许 `PCFG_MPI_HASH=0` 正确关闭 hash。

### `guess_mac/guessing.cpp`

- `PriorityQueue::Generate()` 不再在开头调用 `CalProb(pt)`。
- 原因：MPI worker 只需要生成候选口令，不需要概率计算；概率计算集中由 rank 0 在回插新 PT 时完成。这样 worker 不需要接收完整频率表，广播数据量更小。

### `guess_mac/Makefile`

- 新增 `MPICXX`，默认自动查找：
  - PATH 中的 `mpicxx`
  - `/opt/local/bin/mpicxx`，对应 MacPorts Open MPI
  - `/opt/homebrew/bin/mpicxx`
- 新增目标：
  - `make mpi`
  - `main_mpi`
- `clean` 会删除 `main_mpi`。

### `PCFG_parallel_exploration.md`

- 将上次 Pthread/OpenMP 实验记录更新为 Lab4 MPI 记录。
- 增加 MPI 编译运行命令、参数说明、全量测试结果和报告可写结论。

## 8. 全量实验结果

### 测试环境与参数

- 平台：本机 macOS，Open MPI 来自 MacPorts，`mpirun` 为 Open MPI 5.0.7。
- 运行目录：`guess_mac`。
- 训练集路径：`../../guess_data/rockyou.txt`。
- 训练上限：`PCFG_TRAIN_LIMIT=3000000`。
- 生成上限：`PCFG_GUESS_LIMIT=10000000`。
- MPI 进程内生成模式：`PCFG_GENERATE_MODE=serial PCFG_THREADS=1`。
- MPI 批次候选估计上限：`PCFG_MPI_BATCH_GUESS_LIMIT=1000000`。
- MPI hash：默认开启，即 `PCFG_MPI_HASH=1`。
- Pthread 基线：`main_pthread` 默认 `PCFG_GENERATE_MODE=pthread`，本机检测到 8 线程。

### 测试命令

Pthread 基线：

```sh
PCFG_TRAIN_LIMIT=3000000 PCFG_GUESS_LIMIT=10000000 ./main_pthread
```

MPI 2 进程：

```sh
PCFG_TRAIN_LIMIT=3000000 PCFG_GUESS_LIMIT=10000000 \
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 \
PCFG_MPI_BATCH=8 PCFG_MPI_BATCH_GUESS_LIMIT=1000000 \
/opt/local/bin/mpirun -np 2 ./main_mpi
```

MPI 4 进程：

```sh
PCFG_TRAIN_LIMIT=3000000 PCFG_GUESS_LIMIT=10000000 \
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 \
PCFG_MPI_BATCH=16 PCFG_MPI_BATCH_GUESS_LIMIT=1000000 \
/opt/local/bin/mpirun -np 4 ./main_mpi
```

MPI 8 进程：

```sh
PCFG_TRAIN_LIMIT=3000000 PCFG_GUESS_LIMIT=10000000 \
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 \
PCFG_MPI_BATCH=32 PCFG_MPI_BATCH_GUESS_LIMIT=1000000 \
/opt/local/bin/mpirun -np 8 ./main_mpi
```

### 结果表

| 模式 | 进程/线程 | 生成数 | Guess time | Hash time | Train time | MPI communication |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `main_pthread` | 8 threads | 10,096,227 | 0.129706s | 0.976363s | 8.31247s | - |
| `main_mpi` | 2 proc | 10,045,959 | 0.177093s | 0.927655s | 9.00364s | 0.359188s |
| `main_mpi` | 4 proc | 10,488,529 | 0.143031s | 0.565009s | 9.68855s | 0.253549s |
| `main_mpi` | 8 proc | 10,574,001 | 0.168976s | 0.461686s | 13.9698s | 0.289341s |

### 结果分析

- 相比 Pthread 基线，MPI 对 hash 阶段有明显收益：
  - 2 进程：`0.976363 / 0.927655 ≈ 1.05x`
  - 4 进程：`0.976363 / 0.565009 ≈ 1.73x`
  - 8 进程：`0.976363 / 0.461686 ≈ 2.11x`
- MPI 的 `Guess time` 没有优于 Pthread 基线。原因是 MPI 生成阶段除了本地字符串拼接，还包含任务分发、结果回收、终止同步以及 rank 0 回插优先队列的串行开销。
- 4 进程是本次测试中较均衡的配置：`Hash time` 明显下降，`Guess time` 只略高于 Pthread 基线，通信时间低于 2 进程和 8 进程。
- 8 进程继续降低了 `Hash time`，但 `Train time` 和整体调度开销上升，说明本机资源和通信同步已经开始限制扩展性。
- 生成数略超过 1000 万是正常现象：程序以 PT/批次为单位生成候选，达到上限后在当前批次结束时停止。

### 报告可直接使用的表述

本次 MPI 实现没有让多个进程重复执行同一个口令猜测任务，而是由 rank 0 维护全局优先队列，将多个 PT 作为任务批次分发给不同 rank。各 rank 在本地完成候选口令生成和 MD5/SIMD hash，rank 0 汇总生成数量与 hash 时间，并负责生成新 PT 和回插优先队列。该设计保留了 PCFG 的全局调度控制，同时让生成和 hash 阶段可以跨进程并行。

实验结果显示，MPI 对 hash 阶段加速更明显，8 进程相对 Pthread 基线的 hash 时间加速约为 2.11 倍。但生成阶段受通信、同步和 rank 0 串行优先队列维护影响，未能超过已有 Pthread 版本。综合来看，4 进程配置在本机上取得了较好的折中，说明 MPI 并行度并非越高越好，需要结合任务粒度、通信开销和本机核心资源选择合适参数。

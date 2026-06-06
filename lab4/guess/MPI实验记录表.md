# MPI 实验记录表

## 0. 编译与提交前检查

在服务器 `guess` 目录下先编译：

```bash
cd /home/${USER}/guess
make clean
make mpi MPICXX=mpic++
make pthread
make serial
cp main_mpi main
```

`qsub_mpi.sh` 每次主要改三类内容：

```bash
#PBS -l nodes=1:ppn=8

export PCFG_TRAIN_LIMIT=3000000
export PCFG_GUESS_LIMIT=10000000
export PCFG_GENERATE_MODE=serial
export PCFG_THREADS=1
export PCFG_MPI_BATCH=32
export PCFG_MPI_BATCH_GUESS_LIMIT=1000000
export PCFG_MPI_HASH=1

/usr/local/bin/mpiexec -np 8 -machinefile $PBS_NODEFILE /home/${USER}/main
```

记录输出中的这些字段：

```text
Generate mode:
MPI size:
MPI generated guesses:
MPI communication time:
Guess time:
Hash time:
Train time:
```

`Guess+Hash` 手动计算：

```text
Guess+Hash = Guess time + Hash time
```

## 1. 小规模正确性测试

目的：先验证不会死锁，MD5 自测通过，MPI 可以正常分发任务。

| 实验 | nodes:ppn | np | PCFG_TRAIN_LIMIT | PCFG_GUESS_LIMIT | mode | threads | batch | batch_guess_limit | hash | 是否通过 | 备注 |
| --- | --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- | --- |
| 0.1 | 1:2 | 2 | 20000 | 200000 | serial | 1 | 4 | 200000 | 1 | 是 | Testing MD5Hash correctness...<br/>MD5Hash test passed!<br/>Generate mode:serial threads:1 threshold:50000<br/>MPI size:2 batch:4 batch_guess_limit:200000 hash:on<br/>Training...<br/>Training phase 1: reading and parsing passwords...<br/>Lines processed: 10000<br/>Lines processed: 20000<br/>Training phase 2: Ordering segment values and PTs...<br/>total pts898<br/>Ordering letters<br/>Ordering digits<br/>ordering symbols<br/>Guesses generated: 101410<br/>Guesses generated: 203659<br/>MPI generated guesses:203659<br/>MPI communication time:0.017596seconds<br/>Guess time:0.017502seconds<br/>Hash time:0.024966seconds<br/>Train time:0.471503seconds<br/><br/>Authorized users only. All activities may be monitored and reported. |

对应脚本参数：

```bash
#PBS -l nodes=1:ppn=2

export PCFG_TRAIN_LIMIT=20000
export PCFG_GUESS_LIMIT=200000
export PCFG_GENERATE_MODE=serial
export PCFG_THREADS=1
export PCFG_MPI_BATCH=4
export PCFG_MPI_BATCH_GUESS_LIMIT=200000
export PCFG_MPI_HASH=1

/usr/local/bin/mpiexec -np 2 -machinefile $PBS_NODEFILE /home/${USER}/main
```

## 2. MPI 进程数扩展性

目的：固定任务规模和 batch 规则，比较 `np=1,2,4,8` 时 MPI 加速效果。

公共参数：

```bash
export PCFG_TRAIN_LIMIT=3000000
export PCFG_GUESS_LIMIT=10000000
export PCFG_GENERATE_MODE=serial
export PCFG_THREADS=1
export PCFG_MPI_BATCH_GUESS_LIMIT=1000000
export PCFG_MPI_HASH=1
```

| 实验 | nodes:ppn | np | mode | threads | batch | generated | Guess time/s | Hash time/s | Guess+Hash/s | Train time/s | MPI comm/s | 备注 |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1.1 | 1:1 | 1 | serial | 1 | 4 | 10037945 | 0.529336seconds | 1.68765seconds |  | 31.5054seconds | 0.001742seconds | MPI 单进程基线 |
| 1.2 | 1:2 | 2 | serial | 1 | 8 | 10045959 | 0.467885seconds | 1.27581seconds |  | 31.3017seconds | 0.555128seconds | 2 进程 |
| 1.3 | 1:4 | 4 | serial | 1 | 16 |  10488529 | 0.344166seconds | 0.786898seconds |              | 31.1515seconds | 0.344502seconds | 4 进程 |
| 1.4 | 1:8 | 8 | serial | 1 | 32 | 10574001 | 0.281081seconds | 0.510121seconds |  | 29.6848seconds | 0.34792seconds | 8 进程 |
| 1.5 可选 | 2:8 | 16 | serial | 1 | 64 | 10669629 | 0.24273seconds | 0.390974seconds |  | 28.504seconds | 0.359497seconds | 跨节点 |

脚本中每次对应改：

| 实验 | PBS 资源行 | mpiexec 行 | batch |
| --- | --- | --- | ---: |
| 1.1 | `#PBS -l nodes=1:ppn=1` | `mpiexec -np 1 ...` | 4 |
| 1.2 | `#PBS -l nodes=1:ppn=2` | `mpiexec -np 2 ...` | 8 |
| 1.3 | `#PBS -l nodes=1:ppn=4` | `mpiexec -np 4 ...` | 16 |
| 1.4 | `#PBS -l nodes=1:ppn=8` | `mpiexec -np 8 ...` | 32 |
| 1.5 可选 | `#PBS -l nodes=2:ppn=8` | `mpiexec -np 16 ...` | 64 |

## 3. Batch 粒度对比

目的：证明 `PCFG_MPI_BATCH` 会影响通信频率、负载均衡和调度开销。

固定参数：

```bash
#PBS -l nodes=1:ppn=4

export PCFG_TRAIN_LIMIT=3000000
export PCFG_GUESS_LIMIT=10000000
export PCFG_GENERATE_MODE=serial
export PCFG_THREADS=1
export PCFG_MPI_BATCH_GUESS_LIMIT=1000000
export PCFG_MPI_HASH=1

/usr/local/bin/mpiexec -np 4 -machinefile $PBS_NODEFILE /home/${USER}/main
```

| 实验 | nodes:ppn | np | mode | threads | batch | generated | Guess time/s | Hash time/s | Guess+Hash/s | Train time/s | MPI comm/s | 备注 |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 2.1 | 1:4 | 4 | serial | 1 | 4 | 10037945 | 0.458996seconds |  1.19912seconds |  |  34.654seconds |  1.36344seconds | batch=np |
| 2.2 | 1:4 | 4 | serial | 1 | 8 | 10045959 | 0.393346seconds | 1.04617seconds |  | 31.3147seconds | 0.887416seconds | batch=2*np |
| 2.3 | 1:4 | 4 | serial | 1 | 16 | 10488529 | 0.342237seconds | 0.823043seconds |  | 32.5121seconds | 0.345179seconds | batch=4*np |
| 2.4 | 1:4 | 4 | serial | 1 | 32 | 10574001 | 0.333669seconds | 0.735299seconds |  | 33.4502seconds | 0.281221seconds | batch=8*np |

## 4. MPI + Pthread 组合

目的：比较相同总核心数下，不同 “MPI 进程数 × 进程内线程数” 的组合。

注意：

```text
np * PCFG_THREADS <= nodes * ppn
```

公共参数：

```bash
#PBS -l nodes=1:ppn=8

export PCFG_TRAIN_LIMIT=3000000
export PCFG_GUESS_LIMIT=10000000
export PCFG_MPI_BATCH_GUESS_LIMIT=1000000
export PCFG_MPI_HASH=1
```

| 实验 | nodes:ppn | np | mode | threads | batch | generated | Guess time/s | Hash time/s | Guess+Hash/s | Train time/s | MPI comm/s | 备注 |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 3.1 | 1:8 | 8 | serial | 1 | 32 | 10574001 | 0.298969seconds | 0.524883seconds |  | 33.4161seconds | 0.364247seconds | MPI-only |
| 3.2 | 1:8 | 4 | pthread | 2 | 16 | 10488529 | 0.361166seconds | 0.814781seconds |  | 33.3719seconds | 0.351994seconds | MPI + 2 threads/rank |
| 3.3 | 1:8 | 2 | pthread | 4 | 8 | 10045959 | 0.495512seconds | 1.32229seconds |  | 32.0615seconds | 0.604111seconds | MPI + 4 threads/rank |
| 3.4 | 1:8 | 1 | pthread | 8 | 4 | 10037945 | 0.524105seconds | 1.65458seconds |  | 32.762seconds | 0.002211seconds | 接近 Pthread 基线 |

## 5. 非 MPI 基线

目的：和本次 MPI 实现对比，说明 MPI 是否超过或接近上次 Pthread/SIMD 版本。
直接采用上次报告数据

## 6. 报告分析要点

1. MPI 进程数扩展性：比较实验 1.1 到 1.4 的 `Guess+Hash`，看 `np` 增大后是否下降。
2. Hash 阶段加速：比较 `Hash time`，MPI 通常对 MD5/SIMD 哈希阶段收益更明显。
3. 通信开销：比较 `MPI comm/s` 占 `Guess+Hash/s` 的比例，说明进程数过多或 batch 不合适时通信会吃掉收益。
4. Batch 粒度：比较实验 2.1 到 2.4，说明小 batch 通信频繁，大 batch 通信摊销更好但可能负载不均。
5. MPI + Pthread：比较实验 3.1 到 3.4，说明同样 8 核下不同组合是否比纯 MPI 更好。
6. `Train time` 不计入核心加速比，因为当前训练阶段只在 rank 0 串行执行。

## 7. 代码说明对应点

- `RunMPI`：MPI 主流程，rank 0 训练模型、维护优先队列、分发任务。
- `PCFG_MPI_BATCH`：每轮从优先队列取出的 PT 数量。
- `assigned[i % size]`：把一批 PT 轮转分配给不同 rank。
- `ProcessLocalTasks`：每个 rank 本地生成 guesses，并执行 MD5 SIMD/NEON hash。
- `MPI communication time`：rank 0 统计任务发送和结果回收的通信时间。
- `Guess time`：总墙钟时间减去 hash 时间，包含生成、调度、通信和队列回插。
- `Hash time`：每轮各 rank 哈希时间取最大值后累加，反映并行哈希阶段的墙钟瓶颈。

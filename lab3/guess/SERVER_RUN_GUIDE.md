# 服务器运行与优化数据对比指南

## 1. test.sh 参数含义

服务器要求通过 `test.sh` 提交任务：

```sh
sh test.sh 实验序号 节点数
```

课程说明里 SIMD 是第一次实验，因此本次多线程实验是第二次实验。最基础的运行方式是：

```sh
sh test.sh 2 1
```

含义：

- `2`：第二次实验，也就是本次 Pthread/OpenMP 多线程实验。
- `1`：申请 1 个节点。

如果需要申请更多节点，把第二个参数改成对应节点数，例如：

```sh
sh test.sh 2 2
```

不过当前 PCFG 程序是单进程多线程实现，不是跨节点 MPI 程序。通常先用 `1` 个节点测试即可，重点比较同一节点内不同线程数的效果。

## 2. 建议先做小规模正确性测试

为了避免一次正式任务跑太久，先用较小训练规模和生成规模确认程序能跑通。

如果服务器的 `test.sh/qsub` 会继承环境变量，可以这样运行：

```sh
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 sh test.sh 2 1
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 sh test.sh 2 1
```

每次运行后重点看输出里是否有：

```text
MD5Hash test passed!
Guess time:...
Hash time:...
Train time:...
```

如果 `Generate mode:` 没有显示你设置的模式，说明环境变量可能没有传到最终运行节点。此时看第 6 节的备选办法。

## 3. 正式对比实验怎么跑

正式对比建议固定训练集规模和生成上限，只改变生成模式和线程数。

### 3.1 串行基线

```sh
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1
```

记录输出：

```text
Guess time:...
Hash time:...
Train time:...
```

其中 `Guess time` 是本次 PCFG 候选生成优化最主要的比较指标。

### 3.2 Pthread 对比

建议跑 `2/4/8` 线程：

```sh
PCFG_GENERATE_MODE=pthread PCFG_THREADS=2 sh test.sh 2 1
PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 sh test.sh 2 1
PCFG_GENERATE_MODE=pthread PCFG_THREADS=8 sh test.sh 2 1
```

如果线程数增加后反而变慢，可以提高并行阈值，减少小任务创建线程的开销：

```sh
PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 PCFG_GENERATE_THRESHOLD=50000 sh test.sh 2 1
PCFG_GENERATE_MODE=pthread PCFG_THREADS=8 PCFG_GENERATE_THRESHOLD=50000 sh test.sh 2 1
```

当前代码默认阈值已经是 `50000`。

### 3.3 OpenMP 对比

如果服务器编译命令支持 `-fopenmp`，可以跑：

```sh
PCFG_GENERATE_MODE=openmp PCFG_THREADS=2 sh test.sh 2 1
PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 sh test.sh 2 1
PCFG_GENERATE_MODE=openmp PCFG_THREADS=8 sh test.sh 2 1
```

注意：`PCFG.h` 中保留了 `#include <omp.h>`，不要删除。OpenMP 模式需要服务器编译时带 `-fopenmp`。

## 4. 推荐记录表格

每次运行后，把结果填到表里：

| 实验 | 节点数 | 模式 | 线程数 | 阈值 | Guess time | Hash time | Train time | 加速比 |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline | 1 | serial | 1 | 50000 |  |  |  | 1.00x |
| pthread-2 | 1 | pthread | 2 | 50000 |  |  |  |  |
| pthread-4 | 1 | pthread | 4 | 50000 |  |  |  |  |
| pthread-8 | 1 | pthread | 8 | 50000 |  |  |  |  |
| openmp-2 | 1 | openmp | 2 | 50000 |  |  |  |  |
| openmp-4 | 1 | openmp | 4 | 50000 |  |  |  |  |
| openmp-8 | 1 | openmp | 8 | 50000 |  |  |  |  |

加速比计算方式：

```text
加速比 = 串行 Guess time / 并行 Guess time
耗时降低百分比 = (串行 Guess time - 并行 Guess time) / 串行 Guess time * 100%
```

例子：

```text
串行 Guess time = 0.194687s
Pthread 4 线程 Guess time = 0.170515s
加速比 = 0.194687 / 0.170515 = 1.14x
耗时降低 = (0.194687 - 0.170515) / 0.194687 * 100% = 12.4%
```

## 5. 看哪些指标

主要看：

- `Guess time`：PCFG 候选生成时间，是本次多线程改进的核心指标。
- `Hash time`：MD5/SIMD 哈希时间，本次没有重点改它，只作为总耗时参考。
- `Train time`：模型训练时间，本次也不是重点优化对象。

报告里建议这样写：

> 本次多线程优化主要作用于 `PriorityQueue::Generate()` 的候选口令生成阶段，因此性能评价以程序输出的 `Guess time` 为主。`Hash time` 和 `Train time` 分别反映 SIMD 哈希与模型训练成本，不作为 PCFG 生成并行化的直接加速指标。

## 6. 如果环境变量没有传到 qsub 任务

有些服务器的 `qsub` 不会自动继承你在命令前设置的环境变量。如果你运行：

```sh
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1
```

但输出仍显示默认值，例如：

```text
Generate mode:pthread threads:...
```

说明环境变量没有传到最终运行节点。

此时可以临时改 `main.cpp` 里的默认值来跑对比实验：

```cpp
const GenerateMode generate_mode = ReadGenerateMode();
const int generate_threads = ReadEnvInt("PCFG_THREADS", hw_threads);
const int generate_threshold = ReadEnvInt("PCFG_GENERATE_THRESHOLD", 50000);
```

例如要跑串行基线，可以临时改成：

```cpp
const GenerateMode generate_mode = GenerateMode::Serial;
const int generate_threads = 1;
const int generate_threshold = 50000;
```

要跑 Pthread 4 线程，可以临时改成：

```cpp
const GenerateMode generate_mode = GenerateMode::Pthread;
const int generate_threads = 4;
const int generate_threshold = 50000;
```

要跑 OpenMP 4 线程，可以临时改成：

```cpp
const GenerateMode generate_mode = GenerateMode::OpenMP;
const int generate_threads = 4;
const int generate_threshold = 50000;
```

每改一次源码后再运行：

```sh
sh test.sh 2 1
```

跑完记得把默认值改回环境变量版本，或者保留一份记录说明每次运行的源码配置。

## 7. 建议的最终实验顺序

推荐按下面顺序跑：

```sh
# 小规模正确性
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 sh test.sh 2 1

# 正式串行基线
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1

# 正式 Pthread
PCFG_GENERATE_MODE=pthread PCFG_THREADS=2 sh test.sh 2 1
PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 sh test.sh 2 1
PCFG_GENERATE_MODE=pthread PCFG_THREADS=8 sh test.sh 2 1

# 正式 OpenMP
PCFG_GENERATE_MODE=openmp PCFG_THREADS=2 sh test.sh 2 1
PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 sh test.sh 2 1
PCFG_GENERATE_MODE=openmp PCFG_THREADS=8 sh test.sh 2 1
```

如果排队时间很长，至少跑这三组：

```sh
PCFG_GENERATE_MODE=serial PCFG_THREADS=1 sh test.sh 2 1
PCFG_GENERATE_MODE=pthread PCFG_THREADS=4 sh test.sh 2 1
PCFG_GENERATE_MODE=openmp PCFG_THREADS=4 sh test.sh 2 1
```

## 8. 报告中可以直接写的结论模板

> 实验使用 `sh test.sh 2 1` 提交第二次多线程实验任务，节点数固定为 1。为了比较 PCFG 生成阶段的并行化效果，分别设置 `PCFG_GENERATE_MODE=serial/pthread/openmp`，并测试不同线程数。所有实验保持训练集和生成上限一致，使用 `Guess time` 作为候选口令生成阶段的主要性能指标。加速比定义为串行 `Guess time` 除以并行 `Guess time`。

> 从实验结果看，Pthread/OpenMP 的收益取决于单个 PT 展开的候选数量。当展开规模较小时，线程创建和调度开销会抵消并行收益；当提高并行阈值、只对较大任务并行时，可以降低 `Guess time`。因此最终实现中设置 `PCFG_GENERATE_THRESHOLD=50000`，小任务回退串行，大任务使用多线程静态划分。

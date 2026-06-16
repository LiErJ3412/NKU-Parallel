# Linux + CUDA 运行与分析指令

## 1. 准备数据

程序默认从 `../../guess_data/rockyou.txt` 读取训练集。云端目录可以按下面方式放置：

```bash
cd guess_CUDA
mkdir -p ../../guess_data
# 把 rockyou.txt 放到 ../../guess_data/rockyou.txt
```

如果你的数据放在别的位置，先改 `main.cpp` 里的 `kTrainPath`，或在同级目录建立软链接。

## 2. 编译

CPU baseline：

```bash
cd guess_CUDA
make clean
make serial
make pthread
make openmp
```

CUDA 版本：

```bash
cd guess_CUDA
make cuda
```

如需指定架构，例如 A100：

```bash
make cuda NVCCFLAGS="-std=c++17 -O2 -arch=sm_80"
```

## 3. 正确性冒烟测试

每个可执行文件启动时都会打印 `Testing MD5Hash correctness...` 和 `MD5Hash test passed!`。先用小规模参数确认能跑通：

```bash
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_HASH_BATCH=50000 ./main_serial
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_HASH_BATCH=50000 ./main_pthread
PCFG_TRAIN_LIMIT=20000 PCFG_GUESS_LIMIT=200000 PCFG_HASH_BATCH=50000 ./main_cuda
```

## 4. 性能实验

建议固定训练规模和生成规模，只改变哈希实现：

```bash
export PCFG_TRAIN_LIMIT=3000000
export PCFG_GUESS_LIMIT=10000000
export PCFG_HASH_BATCH=1000000
export PCFG_GENERATE_MODE=serial

./main_serial 2>&1 | tee serial.log
./main_pthread 2>&1 | tee pthread.log
./main_cuda 2>&1 | tee cuda.log
```

输出里的三行是报告中最直接可用的数据：

```text
Guess time:...seconds
Hash time:...seconds
Train time:...seconds
```

CUDA 版本主要优化 `Hash time`。如果 `PCFG_HASH_BATCH` 太小，PCIe 传输和 kernel 启动开销会掩盖 GPU 加速；建议对比 `100000`、`500000`、`1000000`、`2000000` 四档。

## 5. CUDA profiling

查看 kernel 与拷贝耗时：

```bash
nsys profile -t cuda,nvtx,osrt -o pcfg_cuda \
  env PCFG_TRAIN_LIMIT=3000000 PCFG_GUESS_LIMIT=10000000 PCFG_HASH_BATCH=1000000 ./main_cuda
```

生成摘要：

```bash
nsys stats pcfg_cuda.nsys-rep
```

如果云端提供 Nsight Compute，可以进一步看 kernel：

```bash
ncu --set full --target-processes all \
  env PCFG_TRAIN_LIMIT=300000 PCFG_GUESS_LIMIT=1000000 PCFG_HASH_BATCH=1000000 ./main_cuda
```

## 6. 报告分析建议

记录 CPU 串行、CPU SIMD/pthread、CUDA 三组 `Hash time` 与总运行时间。
重点讨论 batch size 对 CUDA 的影响：batch 越小，拷贝和启动开销占比越高；batch 足够大时，一个线程处理一个口令，MD5 位运算并行度提升，`Hash time` 才会下降。

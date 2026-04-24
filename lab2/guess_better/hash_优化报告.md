# Hash阶段瓶颈分析与优化报告

## 1. 原始瓶颈分析

根据 `perf_stat.md`，串行版本的 Hash time 为 `3.15382s`，并行/SIMD版本降到 `1.79606s`，说明 SIMD 对 MD5 轮函数已经有明显加速，约为 `1.76x`。但这个加速低于 4 路 SIMD 的理论上限，说明 hash 阶段还有较多非 SIMD 计算开销。

结合 `并行_report.txt`，`MD5HashSIMD` 在全程序中占比约 `4.25%`。全程序更大的热点在训练阶段的 `model::FindPT`、`segment::insert`、`segment::order` 以及 `unordered_map` 查找/排序。但如果只看 hash 阶段，主要瓶颈集中在输入预处理和内存管理，而不是 MD5 轮函数本身。

原 SIMD hash 路径中，`MD5HashSIMD` 每 4 个口令分一组，但每个 lane 仍然调用一次 `StringProcess`。`StringProcess` 会为每个口令单独执行：

- `string input` 按值传参，产生一次 `std::string` 拷贝；
- `new Byte[paddedLength]` 分配 padded message；
- `memcpy`、`memset` 写入 padding 后的数据；
- hash 结束后再 `delete[]` 释放。

在主程序中，每批约 100 万个候选口令进入 `MD5HashSIMD`，总量可达 1000 万级。因此“每个口令一次堆分配/释放 + 一次 string 拷贝”会形成很明显的固定开销，并且会破坏 cache locality。

## 2. 本次优化内容

### 2.1 消除 `StringProcess` 按值传参

将 `StringProcess` 的参数从：

```cpp
Byte *StringProcess(string input, int *n_byte)
```

改为：

```cpp
Byte *StringProcess(const string &input, int *n_byte)
```

同时将公开接口 `MD5Hash` 也从按值传参改成 `const string &`。这样标量校验路径和保留的 `StringProcess` 路径都不会再额外复制输入字符串。

### 2.2 SIMD路径不再为每个口令 `new[]/delete[]`

原实现中，`MD5HashSIMD` 对每个 lane 调用 `StringProcess`，先生成完整 padded buffer，再从 buffer 中读取 32-bit word。

优化后，`MD5HashSIMD` 不再调用 `StringProcess`，而是新增了两个轻量 helper：

- `PaddedLengthFor`：根据原始长度计算 MD5 padding 后的总长度；
- `BuildPaddedBlockWords`：在栈上直接构造当前 block 的 16 个 little-endian 32-bit word。

这样 SIMD 路径可以直接构造 `x[16]`，不需要为每个口令创建临时 padded message，也不需要在每组结束后释放内存。这里没有采用逐字节虚拟读取方案，因为那会在每个 word 上引入多次分支判断，实际会拖慢 hash 阶段。

### 2.3 更激进的短口令 fast path

RockYou 类数据和 PCFG 生成结果中，大量口令长度小于 56 字节。MD5 对这类输入只需要一个 512-bit block。

因此在 `MD5HashSIMD` 中增加单 block fast path：当当前 4-lane 组里的有效口令都只需要 1 个 block 时，直接：

- 在栈上构造 4 个 lane 的 16 个 word；
- 使用固定 IV 初始化 SIMD 状态；
- 执行一次 `MD5RoundsSIMD`；
- 直接写回最终 digest。

这样可以跳过通用多 block 路径中的 `laneState[4][4]` 初始化、`maxBlocks` 循环、`block < nBlocks[lane]` 判断和逐 block 状态写回。

### 2.4 避免输出数组无效清零

原实现使用：

```cpp
states.assign(inputs.size() * 4, 0);
```

这会先把整块输出数组清零，但后续每个 digest word 都会被覆盖。对于 100 万个口令，一批输出约 16MB，清零是纯额外内存写入。

现在改为：

```cpp
states.resize(inputs.size() * 4);
```

只保证容量和大小，不额外初始化整块输出。

## 3. 优化后的效果预期

这次优化主要减少 hash 阶段的固定开销：

- 去掉每个口令的 `std::string` 拷贝；
- 去掉每个口令的 `new[]/delete[]`；
- 减少 padded message 的写入和再读取；
- 降低 allocator 压力，提高 cache locality。

预期收益在短口令场景更明显。RockYou 类密码数据集中大量口令长度小于 56 字节，只需要一个 MD5 block，原来的动态分配成本相对 MD5 rounds 更突出。优化后，这部分口令会直接从原始字符串生成 MD5 word。

需要注意的是，这次优化没有改变 SIMD lane 数，也没有做多线程并行，所以不会解决全程序 `1.000 CPUs utilized` 的问题。全程序层面更大的瓶颈仍然在训练阶段；如果继续优化总时间，应优先处理 `FindPT`、`segment::insert`、`segment::order` 里的哈希表查找和排序开销。

## 4. 修改文件

- `md5.cpp`
  - `StringProcess` 改为 `const string &`；
  - 新增栈上 padded block word 构造 helper；
  - `MD5HashSIMD` 去掉逐口令 `StringProcess`、`new[]`、`delete[]`。
  - 为单 block 短口令增加 fast path。
  - `states.assign` 改为 `states.resize`，避免无效清零。
- `md5.h`
  - `MD5Hash` 声明改为 `const string &`。

## 5. 验证

完整工程编译时仍会因为环境缺少 `omp.h` 停在 `PCFG.h`，这不是本次 hash 优化引入的问题。

已单独执行：

```bash
g++ -std=c++17 -O2 -c md5.cpp -o /tmp/md5.o
```

`md5.cpp` 单独编译通过。

另外用临时测试程序对比了 `MD5HashSIMD` 和标量 `MD5Hash` 的输出，测试覆盖空串、短串、55/56 字节 padding 边界以及 100 字节多 block 输入，结果为：

```text
MD5 SIMD matches scalar
```

补充微基准：对 100 万个 `pass0` 到 `pass999999` 形式的短口令单独测试 hash 路径，第一版栈上构造方案结果为：

```text
simd 0.112123
scalar 0.133399
```

进一步加入单 block fast path、`memcpy` 构造 word、输出数组避免清零后，结果为：

```text
simd 0.0924745
scalar 0.148574
```

这个微基准只用于验证 hash 函数本身，完整程序耗时还会受到候选生成、批量大小、输出数组写入等因素影响。



最终优化后的运行结果（在服务器上）：

Guess time:0.545395seconds
Hash time:1.21857seconds
Train time:26.8888seconds



Perf stat:

Guess time:0.53414seconds
Hash time:1.22129seconds
Train time:31.2547seconds

 Performance counter stats for './main':

         37,674.69 msec task-clock:u              #    1.000 CPUs utilized          
                 0      context-switches:u        #    0.000 K/sec                  
                 0      cpu-migrations:u          #    0.000 K/sec                  
            66,480      page-faults:u             #    0.002 M/sec                  
    95,676,352,089      cycles:u                  #    2.540 GHz                    
    44,585,952,721      instructions:u            #    0.47  insn per cycle         
   <not supported>      branches:u                                                  
       694,945,838      branch-misses:u                                             

      37.680870842 seconds time elapsed
    
      37.188947000 seconds user
       0.395577000 seconds sys

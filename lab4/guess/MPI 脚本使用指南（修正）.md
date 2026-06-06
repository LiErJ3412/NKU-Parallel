# MPI 脚本使用指南（修正）

# 使用

本次实验不适用test\.sh作为提交手段，需要手动使用mpic\+\+进行编译，并通过qsub\_mpi\.sh进行提交，脚本运行方法如下：

```Bash
qsub qsub_mpi.sh
```

# qsub\_mpi\.sh脚本内容及说明

```Bash
#!/bin/sh
#PBS -N qsub_mpi
#PBS -e test.e
#PBS -o test.o
#PBS -l nodes=2:ppn=8

NODES=$(cat $PBS_NODEFILE | sort | uniq)
# 注意把所有的ntt换成你的选题

for node in $NODES; do
    scp master_ubss1:/home/${USER}/ntt/main ${node}:/home/${USER} 1>&2
    scp -r master_ubss1:/home/${USER}/ntt/files ${node}:/home/${USER}/ 1>&2
done

/usr/local/bin/mpiexec -np 8 -machinefile $PBS_NODEFILE /home/${USER}/main

scp -r /home/${USER}/files/ master_ubss1:/home/${USER}/ntt/ 2>&1

```

qsub\_mpi\.sh中共有三个参数：nodes, ppn, np。分别代表申请计算结点数，每个节点申请核心数，mpi运行要启动的进程数。每个核心运行一个进程，每个计算节点有8个核心，可以支持8个线程的运行，因此你的脚本参数需要保证：

$np \leq nodes \times ppn (nodes \leq4, ppn \leq 8)$

当不使用多线程时取等。

下面是两个例子：

1. 未使用多线程优化，想申请8个核心进行mpi实验，则  $nodes = 1, ppn = 8, np = nodes \times ppn$\(也可以取nodes = 2, ppn = 4\)

2. 使用了多线程优化，例如每个mpi进程使用2个线程，想申请8个核心16个线程进行mpi使用，则$nodes = 2, ppn = 8, np = nodes \times ppn \div 2$

# 注意事项

1. 同多线程实验，mpi实验很容易出现死锁，如果通过qstat相关指令发现上一个提交的任务长时间还未跑出，说明已经死锁，需要使用qdel指令删除死锁的任务，如果发现新提交的任务一直处于 Q 状态\(队列中\)，说明当前计算节点不够或者之前的某一次任务出现死锁

2. 要求服务器申请的计算节点数量不超过4，每个计算节点申请线程数不超过8，以避免单个任务占用过多服务器资源，且大于32进程后通信时间会显著增长导致加速意义不明显

3. 编译指令中需要把g\+\+修改为mpic\+\+



参考文档[MPI 脚本使用指南](https://nankai.feishu.cn/wiki/C7SkwVmeKiJLhJkW1TLcbanpnAf)


# VTune 专用程序

这个目录里的每个可执行文件只保留一个目标函数，方便直接在 VTune 中做单程序剖析。

## 可执行文件

- `matrix_naive.exe`
  - 目标函数：`matrix_dot_naive`
  - 建议分析：`Hotspots` 或 `Memory Access`
- `matrix_row_major.exe`
  - 目标函数：`matrix_dot_row_major`
  - 建议分析：`Hotspots` 或 `Memory Access`
- `sum_chain.exe`
  - 目标函数：`sum_chain`
  - 建议分析：`Hotspots` 或 `Microarchitecture Exploration`
- `sum_two_way.exe`
  - 目标函数：`sum_two_way`
  - 建议分析：`Hotspots` 或 `Microarchitecture Exploration`

## 编译

```powershell
./build_vtune.ps1
```

编译选项里包含 `-g`，这样 VTune 更容易显示函数与源码行信息。

## 直接运行

```powershell
./matrix_naive.exe
./matrix_row_major.exe
./sum_chain.exe
./sum_two_way.exe
```

## 规模与重复次数

- `matrix_*`：`n = 2048`
- `sum_*`：`n = 2^20`

重复次数已经固定到能稳定产生足够长的执行时间，适合 VTune 采样，不需要再额外传参。

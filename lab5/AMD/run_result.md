lab1 
Exercise 4: Analyze Your Results:

N=1048576, Trials=10
---
BS=16: 0.036269 0.036869 0.036508 0.036589 0.036429 0.028333 0.036388 0.036429 0.036229 0.036789 
BS=32: 0.020719 0.020719 0.020799 0.020719 0.020638 0.020679 0.020639 0.020679 0.020559 0.025287 
BS=64: 0.029896 0.020559 0.020559 0.017593 0.020559 0.020598 0.018595 0.018515 0.018515 0.017594 
BS=128: 0.020679 0.017634 0.017713 0.06332 0.018636 0.017713 0.017674 0.020599 0.017673 0.017713 
BS=256: 0.017674 0.022743 0.017673 0.017674 0.017633 0.018595 0.020639 0.018596 0.017674 0.017633 
BS=512: 0.020679 0.018635 0.017674 0.017673 0.017714 0.017673 0.017714 0.017673 0.017714 0.017713 
BS=1024: 0.017834 0.020799 0.017793 0.017833 0.018796 0.017793 0.017793 0.017874 0.018756 0.017834 

Block Size  Mean (ms)   Std (ms)   Min (ms)   Max (ms)  Trials
------------------------------------------------------------
        16     0.0355     0.0009     0.0329     0.0359      10
        32     0.0201     0.0009     0.0187     0.0207      10
        64     0.0193     0.0015     0.0177     0.0223      10
       128     0.0202     0.0015     0.0176     0.0226      10
       256     0.0208     0.0018     0.0177     0.0252      10
       512     0.0207     0.0000     0.0206     0.0208      10
      1024     0.0212     0.0014     0.0207     0.0253      10



Exercise 7: Analyze Sub-Wavefront Results:
Block Size  Mean (ms)   Std (ms)  Sub-Wave?
---------------------------------------------
         8     0.0551     0.0015        YES
        16     0.0350     0.0035        YES
        32     0.0211     0.0014         no
        64     0.0217     0.0049         no
       128     0.0206     0.0001         no
       256     0.0206     0.0001         no


lab2:
Exercise 3: Performance Analysis:
Test                     Elements    Mean (ms)   Std (ms)        Min        Max
--------------------------------------------------------------------------------
Test 1: 16x16                 256       0.0078     0.0001     0.0076     0.0078
Test 2: 128x32               4096       0.0090     0.0000     0.0090     0.0091
Test 3: 1x1024               1024       0.0076     0.0010     0.0071     0.0105
Test 4: 1001x2001         2003001       0.1117     0.0002     0.1114     0.1118

lab3:
Exercise 1: Analyze the Problem:
KERNEL_MS 5.57504
Naive run OK

Exercise 2: Understand the Optimization:
KERNEL_MS 0.185766
KERNEL_MS 2.36857
Optimized matches serial

Exercise 3: Record Results:
Test 2 (medium)    Serial CPU      mean=    2.4074 ms  std=  0.0335 ms
Test 2 (medium)    Naive GPU       mean=    5.5714 ms  std=  0.0046 ms
Test 2 (medium)    Optimized GPU   mean=    0.1857 ms  std=  0.0005 ms

Test 4 (large)     Serial CPU      mean=   23.9073 ms  std=  0.2903 ms
Test 4 (large)     Naive GPU       mean=  359.2654 ms  std=  0.1471 ms
Test 4 (large)     Optimized GPU   mean=    1.7784 ms  std=  0.0012 ms


Exercise 5: Contention Scenarios:

Uniform        mean=  0.1863 ms  std=0.0016
Gaussian       mean=  0.1857 ms  std=0.0003
All-same(0)    mean=  0.2712 ms  std=0.0001


lab4:

6. Run Naive Reduction:

=== Small Test (N=10) ===
4

=== Medium Test (N=1M) ===
-74993790

real	0m0.106s
user	0m0.053s
sys	0m0.044s


9. Large Scale Test:
Generating 3.in (this may take a moment)...
  testcases/1.in: 29 bytes
  testcases/2.in: 4,019,554 bytes
  testcases/3.in: 500,199,790 bytes
=== Large Test (N=100M) ===
-1984960864

real	0m2.109s
user	0m1.892s
sys	0m0.201s


=== Benchmark with N=1M (testcases/2.in) ===
=== Reduction Benchmark ===
Device: Radeon 8060S Graphics
N = 1000000, Block size = 256

Strategy                               Time (ms)          Result
----------------------------------- ------------ ---------------
0. Naive Atomic                           0.0312       -74993790
1. Tree (Shared Memory)                   0.0250       -74993790
2. Tree + Warp Shuffle                    0.0204       -74993790
3. Multi-Element + Tree + Shuffle         0.0129       -74993790

=== Speedup vs Naive ===
  Tree:          1.25x
  Warp Shuffle:  1.53x
  Multi-Element: 2.41x

Verification: ALL PASSED (all strategies produce same result)

lab5:
5. Run Small Test Cases
%%bash
echo "=== Test 1: Small sample (N=8) ==="
cat testcases/1.in
echo "Result:"
./exe_montecarlo testcases/1.in
=== Test 1: Small sample (N=8) ===
0 2 8
4.1805 1.7864 4.7554 3.9983 2.2786 1.1873 4.1222 1.442
Result:
5.937675
%%bash
echo "=== Test 2: Single sample (N=1) ==="
cat testcases/2.in
echo "Result:"
./exe_montecarlo testcases/2.in
=== Test 2: Single sample (N=1) ===
1 1 1
4.7499
Result:
0.000000


6. Performance Scaling
%%bash
echo "=== Test 4: N=100,000 ==="
time ./exe_montecarlo testcases/4.in
=== Test 4: N=100,000 ===
751909.082340

real	0m0.104s
user	0m0.035s
sys	0m0.054s
%%bash
echo "=== Test 7: N=10,000,000 ==="
time ./exe_montecarlo testcases/7.in
=== Test 7: N=10,000,000 ===
9996.980601

real	0m0.968s
user	0m0.879s
sys	0m0.075s
%%bash
echo "=== Test 8: N=100,000,000 ==="
time ./exe_montecarlo testcases/8.in
=== Test 8: N=100,000,000 ===
1000009.031397

real	0m8.284s
user	0m7.948s
sys	0m0.315s


lab6:
5. Run Test Cases
%%bash
echo "=== Test 1: Small random case ==="
head -5 testcases/1.in
echo "..."
./exe_kmeans < testcases/1.in
=== Test 1: Small random case ===
32 4 29
278.853597
-949.978490
-449.941363
-553.578524
...
473.555 389.117 -763.8 -547.538 
478.578 -473.859 -570.699 357.264 
%%bash
echo "=== Test 3: Minimal case (N=1, k=1) ==="
cat testcases/3.in
echo "Result:"
./exe_kmeans < testcases/3.in
=== Test 3: Minimal case (N=1, k=1) ===
1 1 1
669.515522
-684.741218
-848.788066
581.606063
Result:
669.516 
-684.741 
%%bash
echo "=== Test 4: Large case (N=50000, k=50) ==="
time ./exe_kmeans < testcases/4.in > /dev/null
=== Test 4: Large case (N=50000, k=50) ===

real	0m0.113s
user	0m0.052s
sys	0m0.047s
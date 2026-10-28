# Design and Evaluation of Work-Stealing Thread Pools

## Scope

This C++20 application compares bounded thread-per-task execution (V0), a shared FIFO pool (V1), independent round-robin FIFO queues (V2), mutex-protected work-stealing deques (V3), and atomic work-stealing stacks (V4). OpenMP dynamic scheduling supplies the external runtime baseline. An interactive terminal menu supports configuration, raw dataset generation, scheduler comparisons, correctness validation and exporting a results bundle.

V4 uses per-worker Treiber stacks with CAS push/pop/steal and immutable node links. Nodes are retained until workers join to prevent reclamation races and address-reuse ABA. Queue operations are lock-free on the tested pointer-atomic platform; allocation, admission, sleeping, shutdown and statistics are not lock-free. Owners and thieves both take LIFO work. This is an atomic-stack variant, not a Chase-Lev FIFO-steal deque; retained-node memory grows with accepted tasks.

## Reproduction and environment

- Host: Debian 13 container on WSL2 Linux 6.18.54.1, Windows 10 host
- Processor: Intel Core i3-1005G1 @ 1.20 GHz, 2 physical cores; 4 host logical CPUs.
- Toolchain: Clang 19.1.7 / libstdc++ 14 / OpenMP libomp 19 / CMake 3.31.6; Release -O3 -DNDEBUG
- Build: CMake, C++20, Release optimization.
- Source data: `results/full_linux/csv/benchmark_results.csv`; 369 measured scheduler runs.
- Seed(s): 42.
- Concurrency limits: 1, 2, 4.
- Executed matrix: `config/experiments.json`; 3 repeats and 1 unrecorded warm-up(s) per configuration. Worker counts exceeding host logical CPUs are skipped and logged.

```sh
docker build -f scripts/Dockerfile.testing -t vehicle-threadpool-test .
docker run --rm --security-opt seccomp=unconfined --mount "type=bind,source=YOUR_PROJECT_PATH,target=/workspace" vehicle-threadpool-test sh scripts/run_linux_checks.sh --full
python scripts/validate_artifacts.py --csv results/full_linux/csv/benchmark_results.csv --config config/experiments.json
python scripts/plot_results.py results/full_linux/csv/benchmark_results.csv --output results/full_linux/graphs
python scripts/summarize_results.py --csv results/full_linux/csv/benchmark_results.csv --config config/experiments.json --graphs-directory results/full_linux/graphs --compiler "Clang 19.1.7 / libstdc++ 14 / OpenMP libomp 19 / CMake 3.31.6; Release -O3 -DNDEBUG" --processor "Intel Core i3-1005G1 @ 1.20 GHz, 2 physical cores" --platform "Debian 13 container on WSL2 Linux 6.18.54.1, Windows 10 host"
```

The Docker script builds and tests Linux Release and ThreadSanitizer configurations, then executes the full configured matrix. Native Windows builds use `scripts/build_windows.ps1`; launch the menu with `build/vehicle_benchmark.exe --interactive`.

## Workloads and correctness

Mixed inputs merge five configured arrival streams: speed at 100 Hz, wheel vibration at 100 Hz, temperature at 5 Hz, CAN at 1000 Hz, and LiDAR at 10 Hz. Complete speed windows avoid scheduler-dependent state updates. Processing comprises moving averages, a radix-2 FFT, threshold checks, CAN decoding/checksum validation, and deterministic voxel centroid statistics. Light, heavy and deliberately imbalanced synthetic workloads stress different costs; burst workloads release arrivals in 100 ms groups.

Each scheduler receives the same immutable data and is checked against independently calculated serial outputs. Known-answer algorithm tests complement reference equivalence. Concurrency tests cover exactly-once execution, multiple producers, exception reporting, draining shutdown, shutdown/submission races, reuse and a blocked-owner scenario that requires stealing.

Measured runs reported 0 missing tasks, 0 duplicate executions and 0 reference mismatches across 402,300 completed tasks.

## Measurement method

Steady-clock elapsed time includes executor construction, submission, drain and shutdown. Dataset generation, serial result preparation, correctness checking and file output occur outside the interval. A serial run uses the same timing wrapper for each repeat. Throughput is completed tasks divided by elapsed time; speedup is serial elapsed divided by measured elapsed; efficiency is speedup divided by requested concurrency. Queue delay, execution time and submission-to-completion latency are recorded separately. p95/p99 use nearest-rank percentiles. Repeat order rotates among schedulers.

Streaming elapsed time includes arrival pacing. Arrival-to-finish latency includes producer lateness; deadlines use that latency. OpenMP dispatches its accumulated batch at wait(), so its response latency includes batching, and it is excluded from streaming runs. V0 concurrency is a bounded batch limit. V0/OpenMP idle time is unavailable. OpenMP waits use the PASSIVE policy in the experiment runner to reduce idle-runtime spin between scheduler measurements.

Process CPU and context switches are measured with getrusage(RUSAGE_SELF) around the execution interval on Linux, including terminated worker threads. CPU percent = 100 * (user + kernel CPU seconds) / elapsed seconds, so 100% represents one fully used logical CPU and process utilization may exceed 100%. Machine CPU percent divides by visible logical CPU count. These process-wide counters include scheduler/runtime overhead. Context switches are Linux guest counters, not host Windows ETW events. Snapshot calls sit just outside wall-clock endpoints, introducing small boundary error, and short runs are sensitive to timer resolution. Windows uses GetProcessTimes for CPU and exports context switches as unavailable, rather than inventing counts. V4 reclaims its task nodes during timed shutdown after all workers have joined.

## Measured comparison at concurrency 4

Throughput is mean +/- sample standard deviation over repeats. p99 is the mean of each repeat's p99, rather than a pooled percentile. The measured matrix supports comparisons on this host and does not establish universal scheduler rankings.

| Workload / mode / LiDAR points / FFT size | Scheduler | Tasks/s (mean +/- SD) | p99 response (us) | Speedup | Efficiency | Steals (mean) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| burst / streaming / 12000 / 256 | v0 | 1,360.3 +/- 78.5 | 9,407.1 | 0.946 | 0.236 | 0.0 |
| burst / streaming / 12000 / 256 | v1 | 1,436.1 +/- 28.9 | 1,214.0 | 0.998 | 0.250 | 0.0 |
| burst / streaming / 12000 / 256 | v2 | 1,436.6 +/- 12.7 | 9,250.1 | 0.999 | 0.250 | 0.0 |
| burst / streaming / 12000 / 256 | v3 | 1,447.9 +/- 20.3 | 1,669.9 | 1.007 | 0.252 | 143.0 |
| burst / streaming / 12000 / 256 | v4 | 1,440.0 +/- 19.5 | 1,139.5 | 1.001 | 0.250 | 158.0 |
| heavy / batch / 12000 / 256 | openmp | 1,308.9 +/- 44.8 | 910,161.2 | 2.244 | 0.561 | 0.0 |
| heavy / batch / 12000 / 256 | v0 | 698.6 +/- 111.1 | 20,192.4 | 1.201 | 0.300 | 0.0 |
| heavy / batch / 12000 / 256 | v1 | 1,257.1 +/- 76.0 | 940,384.7 | 2.157 | 0.539 | 0.0 |
| heavy / batch / 12000 / 256 | v2 | 770.6 +/- 368.0 | 1,586,067.0 | 1.331 | 0.333 | 0.0 |
| heavy / batch / 12000 / 256 | v3 | 1,138.4 +/- 35.3 | 1,027,766.2 | 1.954 | 0.489 | 306.3 |
| heavy / batch / 12000 / 256 | v4 | 1,146.2 +/- 46.9 | 1,027,100.8 | 1.966 | 0.492 | 299.0 |
| heavy / batch / 3000 / 1024 | openmp | 6,525.5 +/- 592.4 | 182,951.3 | 2.620 | 0.655 | 0.0 |
| heavy / batch / 3000 / 1024 | v0 | 2,678.2 +/- 403.1 | 6,136.6 | 1.066 | 0.266 | 0.0 |
| heavy / batch / 3000 / 1024 | v1 | 5,712.5 +/- 2,595.7 | 250,892.5 | 2.222 | 0.555 | 0.0 |
| heavy / batch / 3000 / 1024 | v2 | 4,674.9 +/- 515.7 | 247,932.8 | 1.857 | 0.464 | 0.0 |
| heavy / batch / 3000 / 1024 | v3 | 5,967.9 +/- 840.0 | 199,428.0 | 2.418 | 0.604 | 276.3 |
| heavy / batch / 3000 / 1024 | v4 | 7,473.1 +/- 557.7 | 156,278.1 | 2.978 | 0.745 | 281.3 |
| heavy / batch / 512 / 64 | openmp | 44,421.0 +/- 4,423.8 | 26,400.7 | 1.989 | 0.497 | 0.0 |
| heavy / batch / 512 / 64 | v0 | 9,910.0 +/- 347.7 | 707.8 | 0.444 | 0.111 | 0.0 |
| heavy / batch / 512 / 64 | v1 | 40,696.4 +/- 9,917.2 | 28,896.0 | 1.822 | 0.456 | 0.0 |
| heavy / batch / 512 / 64 | v2 | 32,938.4 +/- 3,647.6 | 33,758.8 | 1.477 | 0.369 | 0.0 |
| heavy / batch / 512 / 64 | v3 | 40,133.4 +/- 5,998.7 | 29,225.4 | 1.799 | 0.450 | 312.0 |
| heavy / batch / 512 / 64 | v4 | 40,921.3 +/- 6,773.5 | 28,724.7 | 1.832 | 0.458 | 277.0 |
| imbalanced / batch / 12000 / 256 | openmp | 4,166.1 +/- 493.4 | 285,183.9 | 2.084 | 0.521 | 0.0 |
| imbalanced / batch / 12000 / 256 | v0 | 1,706.8 +/- 127.9 | 11,488.0 | 0.854 | 0.213 | 0.0 |
| imbalanced / batch / 12000 / 256 | v1 | 3,742.2 +/- 854.6 | 324,897.9 | 1.836 | 0.459 | 0.0 |
| imbalanced / batch / 12000 / 256 | v2 | 2,021.5 +/- 355.6 | 601,893.7 | 1.009 | 0.252 | 0.0 |
| imbalanced / batch / 12000 / 256 | v3 | 3,636.1 +/- 540.5 | 311,712.6 | 1.843 | 0.461 | 513.3 |
| imbalanced / batch / 12000 / 256 | v4 | 3,821.6 +/- 978.7 | 306,210.6 | 1.835 | 0.459 | 729.3 |
| light / batch / 12000 / 256 | openmp | 1,050,782.6 +/- 356,862.9 | 847.4 | 0.184 | 0.046 | 0.0 |
| light / batch / 12000 / 256 | v0 | 11,914.2 +/- 2,480.7 | 569.0 | 0.002 | 0.001 | 0.0 |
| light / batch / 12000 / 256 | v1 | 362,580.3 +/- 109,089.6 | 386.0 | 0.066 | 0.017 | 0.0 |
| light / batch / 12000 / 256 | v2 | 589,008.2 +/- 320,005.6 | 477.6 | 0.105 | 0.026 | 0.0 |
| light / batch / 12000 / 256 | v3 | 491,719.7 +/- 176,985.3 | 593.4 | 0.091 | 0.023 | 483.3 |
| light / batch / 12000 / 256 | v4 | 616,442.5 +/- 264,019.3 | 249.4 | 0.106 | 0.026 | 482.0 |
| mixed / batch / 12000 / 256 | openmp | 51,001.4 +/- 13,686.3 | 18,911.4 | 1.706 | 0.427 | 0.0 |
| mixed / batch / 12000 / 256 | v0 | 8,261.5 +/- 821.8 | 3,461.4 | 0.282 | 0.070 | 0.0 |
| mixed / batch / 12000 / 256 | v1 | 41,750.6 +/- 16,644.1 | 22,979.7 | 1.422 | 0.355 | 0.0 |
| mixed / batch / 12000 / 256 | v2 | 49,677.1 +/- 3,619.3 | 22,047.6 | 1.704 | 0.426 | 0.0 |
| mixed / batch / 12000 / 256 | v3 | 46,809.4 +/- 5,078.8 | 20,186.9 | 1.631 | 0.408 | 228.7 |
| mixed / batch / 12000 / 256 | v4 | 45,731.0 +/- 8,179.0 | 21,669.3 | 1.524 | 0.381 | 316.7 |

## OS profiling at maximum measured concurrency

| Workload / LiDAR points / FFT size | Scheduler | Process CPU % (mean) | Machine CPU % (mean) | Voluntary switches (mean) | Involuntary switches (mean) |
| --- | --- | ---: | ---: | ---: | ---: |
| burst / 12000 / 256 | v0 | 24.1 | 6.0 | 91.0 | 12.3 |
| burst / 12000 / 256 | v1 | 12.0 | 3.0 | 31.0 | 2.0 |
| burst / 12000 / 256 | v2 | 13.6 | 3.4 | 35.3 | 2.7 |
| burst / 12000 / 256 | v3 | 11.2 | 2.8 | 29.7 | 3.0 |
| burst / 12000 / 256 | v4 | 12.8 | 3.2 | 25.0 | 0.3 |
| heavy / 12000 / 256 | openmp | 397.9 | 99.5 | 8.7 | 136.0 |
| heavy / 12000 / 256 | v0 | 114.1 | 28.5 | 379.0 | 396.0 |
| heavy / 12000 / 256 | v1 | 385.9 | 96.5 | 13.3 | 200.0 |
| heavy / 12000 / 256 | v2 | 188.9 | 47.2 | 13.3 | 23.3 |
| heavy / 12000 / 256 | v3 | 391.7 | 97.9 | 18.3 | 137.7 |
| heavy / 12000 / 256 | v4 | 396.8 | 99.2 | 12.3 | 213.3 |
| heavy / 3000 / 1024 | openmp | 396.1 | 99.0 | 9.0 | 14.7 |
| heavy / 3000 / 1024 | v0 | 76.0 | 19.0 | 430.7 | 126.7 |
| heavy / 3000 / 1024 | v1 | 394.2 | 98.5 | 12.0 | 88.7 |
| heavy / 3000 / 1024 | v2 | 210.2 | 52.5 | 21.7 | 4.7 |
| heavy / 3000 / 1024 | v3 | 394.3 | 98.6 | 8.7 | 31.3 |
| heavy / 3000 / 1024 | v4 | 392.1 | 98.0 | 14.0 | 47.7 |
| heavy / 512 / 64 | openmp | 389.2 | 97.3 | 9.3 | 13.7 |
| heavy / 512 / 64 | v0 | 78.6 | 19.7 | 432.0 | 48.7 |
| heavy / 512 / 64 | v1 | 340.2 | 85.0 | 16.3 | 20.3 |
| heavy / 512 / 64 | v2 | 201.1 | 50.3 | 24.3 | 1.7 |
| heavy / 512 / 64 | v3 | 335.9 | 84.0 | 9.0 | 24.7 |
| heavy / 512 / 64 | v4 | 328.4 | 82.1 | 8.3 | 5.7 |
| imbalanced / 12000 / 256 | openmp | 383.3 | 95.8 | 8.3 | 72.3 |
| imbalanced / 12000 / 256 | v0 | 59.7 | 14.9 | 317.0 | 59.0 |
| imbalanced / 12000 / 256 | v1 | 385.0 | 96.3 | 15.7 | 124.7 |
| imbalanced / 12000 / 256 | v2 | 100.9 | 25.2 | 59.7 | 8.0 |
| imbalanced / 12000 / 256 | v3 | 391.3 | 97.8 | 20.0 | 49.3 |
| imbalanced / 12000 / 256 | v4 | 360.3 | 90.1 | 17.7 | 537.0 |
| light / 12000 / 256 | openmp | 237.2 | 59.3 | 7.7 | 0.0 |
| light / 12000 / 256 | v0 | 81.7 | 20.4 | 331.0 | 5.3 |
| light / 12000 / 256 | v1 | 131.5 | 32.9 | 124.3 | 7.7 |
| light / 12000 / 256 | v2 | 167.5 | 41.9 | 125.0 | 4.0 |
| light / 12000 / 256 | v3 | 143.6 | 35.9 | 55.3 | 1.7 |
| light / 12000 / 256 | v4 | 181.8 | 45.5 | 107.0 | 3.0 |
| mixed / 12000 / 256 | openmp | 327.9 | 82.0 | 8.3 | 3.0 |
| mixed / 12000 / 256 | v0 | 73.1 | 18.3 | 323.0 | 7.7 |
| mixed / 12000 / 256 | v1 | 310.4 | 77.6 | 22.7 | 23.0 |
| mixed / 12000 / 256 | v2 | 302.9 | 75.7 | 15.7 | 18.3 |
| mixed / 12000 / 256 | v3 | 343.1 | 85.8 | 22.0 | 24.7 |
| mixed / 12000 / 256 | v4 | 323.4 | 80.8 | 12.3 | 10.7 |

## ThreadSanitizer evidence

The Linux Clang ThreadSanitizer positive control reported the deliberately injected race and exited with the expected failure status. All project CTest suites then passed with halt_on_error=1 and no race reports. Logs: `results/linux_tests/tsan_probe.txt`, `results/linux_tests/tsan_tests.txt`, and `results/linux_checks_log.txt`. OpenMP is excluded from the sanitized build; V0 through V4 are covered. This is evidence about executed paths, not a proof of race freedom.

## Complete point-cloud exports

LiDAR processing returns every downsampled voxel centroid, including averaged XYZ and intensity. Every benchmark compares the full cloud to its serial result. Complete ASCII PLY files are exported outside measured intervals and referenced by cloud_file in each task CSV. Content-addressed files are shared by verified scheduler runs. Raw input datasets and raw PLY clouds can be exported separately with --generate-only --dataset-output PATH.

## Independent artifact verification

The artifact checker verified all configured run keys, per-worker accounting, exactly-once task records and OS counter calculations. It read 1,959 unique PLY clouds containing 10,369,430 vertices, checked finite XYZ/intensity values and vertex counts, and recomputed mean centroid ranges against task CSVs. Evidence: `results/full_linux/artifact_validation.json`.


## Figures

![Heavy-workload scaling and latency](../results/full_linux/graphs/heavy_batch_1200_12000_256_42_0.5_100_100_5_1000_10_50000.png)

![Heavy-workload scaling and latency](../results/full_linux/graphs/heavy_batch_1200_12000_256_42_0.5_100_100_5_1000_10_50000_profiling.png)

![Deliberately imbalanced workload](../results/full_linux/graphs/imbalanced_batch_1200_12000_256_42_0.5_100_100_5_1000_10_50000.png)

![Deliberately imbalanced workload](../results/full_linux/graphs/imbalanced_batch_1200_12000_256_42_0.5_100_100_5_1000_10_50000_profiling.png)


## Observed trade-offs

- Light: openmp had the highest mean throughput at concurrency 4 in this matrix (1,050,783 tasks/s).
  V3/V2 mean throughput ratio: 0.835; V3 averaged 483.3 successful steals. Steal counts demonstrate redistribution; they do not by themselves prove a performance gain.
  V4/V3 mean throughput ratio: 1.254; atomic queues remove queue mutexes but retain allocation and lifecycle/accounting costs, and change the victim's removal order.
- Mixed: openmp had the highest mean throughput at concurrency 4 in this matrix (51,001 tasks/s).
  V3/V2 mean throughput ratio: 0.942; V3 averaged 228.7 successful steals. Steal counts demonstrate redistribution; they do not by themselves prove a performance gain.
  V4/V3 mean throughput ratio: 0.977; atomic queues remove queue mutexes but retain allocation and lifecycle/accounting costs, and change the victim's removal order.
- Heavy: openmp had the highest mean throughput at concurrency 4 in this matrix (1,309 tasks/s).
  V3/V2 mean throughput ratio: 1.477; V3 averaged 306.3 successful steals. Steal counts demonstrate redistribution; they do not by themselves prove a performance gain.
  V4/V3 mean throughput ratio: 1.007; atomic queues remove queue mutexes but retain allocation and lifecycle/accounting costs, and change the victim's removal order.
- Imbalanced: openmp had the highest mean throughput at concurrency 4 in this matrix (4,166 tasks/s).
  V3/V2 mean throughput ratio: 1.799; V3 averaged 513.3 successful steals. Steal counts demonstrate redistribution; they do not by themselves prove a performance gain.
  V4/V3 mean throughput ratio: 1.051; atomic queues remove queue mutexes but retain allocation and lifecycle/accounting costs, and change the victim's removal order.

## Limitations and interpretation

These are measurements in a Linux container under WSL2 on one general-purpose Windows host, without CPU affinity, frequency pinning, thermal control or isolated cores. Startup and instrumentation can dominate light workloads. Three repeats provide descriptive variation, not a statistical significance claim. The exact standard-library random distribution may differ across compiler implementations. Linux-container timings must not be pooled with older native Windows validation timings. Further independent repetitions are needed before drawing research conclusions.

Pool workers share a completion/accounting mutex; V3 also scans victims and uses try-lock acquisition. These implementation costs can offset load-balancing gains. V2 can win with balanced queues. Equal task counts do not imply equal work: worker figures include busy time as well as counts. OpenMP uses a different dispatch model. V0 bounds outstanding work and blocks its producer between batches; its short per-task response times must be interpreted alongside total completion time. Pool queues have no backpressure bound. Streaming deadlines are diagnostic and there is no hard real-time guarantee. CPU/context-switch counts measure resource use and do not attribute individual switches to a particular lock or runtime operation.

## Deliverables

- `build/vehicle_benchmark.exe`: native Windows executable and interactive menu.
- `build-linux/vehicle_benchmark`: Linux executable used by the full matrix; toolchain/cache and binary/source SHA-256 evidence are retained.
- `results/full_linux/csv/benchmark_results.csv`: measured summaries; sibling `details/` and `clouds/` contain processed outputs, timings and full PLY clouds.
- `results/full_linux/graphs`: throughput, p99 latency, speedup, efficiency, CPU/context-switch profiling and worker distributions.
- `results/test_results.txt`: test execution evidence.
- `tests/`: known-answer, concurrency, lifecycle, CLI and CSV checks.

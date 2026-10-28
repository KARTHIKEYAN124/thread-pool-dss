# Vehicle Sensor Thread-Pool Evaluation

Explore the results on the live [ThreadPool Lab website](https://threadpool-lab.vercel.app). The [web app](web/README.md) supports interactive charts, filters, run details, CSV downloads and local CSV loading. Native benchmark execution remains in the terminal application.

C++20 simulation and reproducible comparison of scheduling strategies for vehicle sensor processing. All schedulers process the same immutable dataset, and every measured output is checked against a serial reference. The application is a terminal program; no hardware is required.

The project includes V0–V4, an OpenMP baseline, an interactive menu, complete PLY point-cloud exports, OS CPU/context-switch profiling and a Linux ThreadSanitizer workflow. Actual measurements, verification logs and limitations are recorded in [the final report](docs/final_report.md). Earlier native Windows validation results remain under `results/csv/`; the larger Linux matrix is stored separately under `results/full_linux/`.

The full supported matrix completed **369 runs and 402,300 tasks**, with zero missing tasks, duplicate executions or output mismatches. All three CTest suites passed in Windows Release, Linux Release and Linux ThreadSanitizer builds; the sanitizer's intentional-race positive control was also verified. Independent artifact checks validated **1,959 complete PLY clouds containing 10,369,430 vertices**. The full study includes **55 figures**, including CPU and context-switch profiles. Evidence is in `results/full_linux/artifact_validation.json` and `results/linux_tests/`.

## Build

Linux/WSL:

```sh
sudo apt install build-essential cmake ninja-build libomp-dev python3-matplotlib
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows: use Visual Studio Build Tools with the C++ workload, or LLVM-MinGW, plus CMake and Ninja. For a Visual Studio developer terminal:

```powershell
cmake -S . -B build-msvc
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

For LLVM-MinGW (includes an OpenMP runtime):

```powershell
python -m pip install --user cmake ninja
./scripts/build_windows.ps1 -CompilerBin 'C:\path\to\llvm-mingw\bin'
```

Keep that compiler's `bin` directory on PATH when running its executables, since it contains runtime DLLs. OpenMP is discovered automatically. `--list-schedulers` reports which schedulers were compiled. `-DENABLE_OPENMP=OFF` builds without that external baseline. No dependency is fetched by CMake.

This workspace also contains a local LLVM-MinGW toolchain under `.tools/` (ignored by Git). `./scripts/build_windows.ps1` discovers it and copies the small runtime DLLs beside the executables so they can run directly. CMake and Ninja are installed in the Python user scripts directory, which the helper adds to its PATH.

## Run

```sh
./build/vehicle_benchmark --scheduler serial --tasks 100 --preview 100 --repeats 1 --warmups 0
./build/vehicle_benchmark --scheduler all --threads 4 --tasks 1200 --workload mixed --seed 42
./build/vehicle_benchmark --scheduler v3 --threads 4 --tasks 400 --workload burst --mode streaming --deadline-us 50000
./build/vehicle_benchmark --help
./build/vehicle_benchmark --interactive
./build/vehicle_benchmark --generate-only --tasks 100 --dataset-output data/generated/sensors.csv
```

On Windows use `./build/vehicle_benchmark.exe`. Launching with no arguments opens the interactive menu. It supports dataset generation, serial and V0–V4 execution, all-scheduler comparison, output validation, workload configuration, results-bundle export and the standard full experiment matrix. The menu stores its results under `results/menu/csv/`. The Python runner reads editable experiment JSON; the menu's built-in full matrix uses the standard seven scenarios and worker limits 1/2/4/8, filtered to host CPUs.

CSV summaries default to `results/csv/benchmark_results.csv`; per-task processed outputs, timestamps, and worker statistics are written under `results/csv/details/`. Each task row's `cloud_file` points to its complete downsampled PLY cloud, relative to the detail CSV. Clouds contain centroid XYZ and averaged intensity, and identical reference-verified outputs share content-addressed files under `clouds/`. Exporting and hashing occur outside timing. Raw dataset export includes complete sample windows, CAN payloads and raw LiDAR clouds. A command overwrites its summary unless `--append` is given. Use a distinct output filename for experiments with different noise/rate/deadline settings: detail filenames identify scheduler, workload, mode, task count, granularity, seed, and run, but not those additional settings. No console or file writes occur inside measured tasks. Summaries with an older schema cannot be appended to.

Run the full configurable suite and create figures:

```sh
python -m pip install -r scripts/requirements.txt
python scripts/run_experiments.py --exe build/vehicle_benchmark
python scripts/plot_results.py results/csv/benchmark_results.csv
```

On Windows pass `--exe build/vehicle_benchmark.exe`. The runner reads `config/experiments.json`, filters worker counts against logical CPU count, and stops on any failed experiment. Default runs include three repeats and one warm-up. Increase task count and repeats for research-quality results. Graphs show means and sample standard deviations, separately for each workload, mode, seed and granularity; worker plots show both counts and busy time.

## Architectures and sensor work

| Scheduler | Behavior |
| --- | --- |
| serial | Synchronous reference with the same measurement wrapper |
| V0 | New thread for each task; joins bounded batches of `--threads` threads |
| V1 | Fixed workers, single mutex-protected FIFO queue |
| V2 | Round-robin submission to independent FIFO queues |
| V3 | Round-robin submission, owner LIFO, thief FIFO, try-lock victim scan |
| V4 | Per-worker atomic Treiber stacks; CAS push, owner pop and stealing |
| OpenMP | `parallel for schedule(dynamic,1)`, requested team size |

V1–V4 execute tasks outside queue locks, use predicate-based condition-variable waits, drain accepted tasks during shutdown, and reject later submissions. `wait()` reports the first task exception after draining; a subsequent `wait()` can be used after further submissions. Destruction and `shutdown()` drain without throwing; callers should call `wait()` to observe task exceptions. Multiple external producers are supported; recursive task submission is outside the executor contract. Submitters must finish before object destruction; tasks must not call `wait()`/`shutdown()` on their own executor. Queues are unbounded; this is an experimental scheduler rather than a hard real-time system.

V4 queue push/pop/steal operations use lock-free pointer CAS. Links are immutable after publication, and nodes are retained until shutdown after all workers join, avoiding reclamation races and address-reuse ABA. Memory use grows with accepted tasks. Both owners and thieves use LIFO; this is a Treiber-stack variant, not a Chase-Lev deque. Allocation, lifecycle/admission, sleep and completion statistics still use locks. The entire executor is not claimed to be lock-free. A compile-time check requires lock-free pointer atomics on the target platform.

Speed tasks compute a five-sample moving average, wheel tasks compute a radix-2 FFT and dominant frequency, temperature tasks check thresholds, CAN tasks decode a two-byte value and validate an XOR checksum, and LiDAR tasks group points into 0.5-unit voxels, return complete centroid clouds and compute range statistics. Output verification compares all centroid coordinates and intensities, not just counts. Wheel windows model a vibration signal, rather than a complete four-wheel dynamics model. Algorithms have independent known-answer tests in addition to serial equivalence checks.

`mixed` and `burst` use deterministic rate-driven arrivals (100 Hz speed, 100 Hz wheel, 5 Hz temperature, 1000 Hz CAN, 10 Hz LiDAR). Rates can be changed with `--rates 100,100,5,1000,10`. `burst` quantizes arrivals into 100 ms batches. `light` alternates speed and temperature; `heavy` alternates FFT and LiDAR; `imbalanced` concentrates a minority of heavy tasks in round-robin worker-zero slots as far as the worker count permits. These synthetic stress workloads intentionally override the normal rate mix. Noise and task sizes are configurable. Speed windows are constructed before scheduling to remove state-order dependencies. Identical seeds reproduce datasets on the same standard-library/toolchain implementation; standard-library random distributions need not agree across toolchains.

## Measurement and correctness

Each measured interval includes executor construction, task submission, drain and shutdown. Dataset generation, reference calculation, verification, CSV and console writes are excluded. Per-task steady-clock timestamps distinguish submission, start and finish; queue delay, execution time and response latency are reported separately. p95/p99 use the nearest-rank definition. Speedup is same-repeat serial elapsed time divided by scheduler elapsed time; efficiency divides speedup by requested concurrency. V0 concurrency is a batch cap, rather than a persistent worker count. OpenMP collects submissions and dispatches at `wait()`, so its queue latency includes this batching phase. That baseline supports batch mode only and is excluded from streaming `all` runs.

Streaming submission waits for scheduled arrivals. Arrival-to-finish p99 and deadline misses include producer lateness and scheduling backpressure. Submission-to-finish response time is also reported. Streaming elapsed time includes arrival pacing, so speedup there measures whole-stream duration rather than peak compute throughput. Deadlines are diagnostic thresholds, not guaranteed bounds. Batch deadlines are relative to the task's producer arrival immediately before submission.

Worker busy time measures task execution. Pool idle time includes waiting and task acquisition, with startup and shutdown effects. V0 and OpenMP idle time is unavailable (exported as zero). Separate OS counters record process user/kernel CPU seconds. `cpu_percent = 100 * CPU seconds / elapsed seconds`: 100% is one logical CPU, so the process can exceed 100%. `machine_cpu_percent` divides by visible logical CPUs. Windows uses [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes). Linux uses [getrusage(RUSAGE_SELF)](https://www.man7.org/linux/man-pages/man2/getrusage.2.html) for CPU and voluntary/involuntary context switches across all process threads, including terminated V0 workers. Windows context switches are unavailable through this implementation and exported as empty fields with an availability flag, rather than false zeros. CPU snapshots occur immediately outside wall-clock endpoints and can show resolution/boundary noise on short runs. Counter totals are resource measurements, not attribution of a switch to a specific lock.

The test executable covers known-answer sensor processing and cloud centroids, deterministic generation, exactly-once execution, concurrent producers, exception propagation and reuse, draining destruction, submission/shutdown races, repeated shutdown, complete output equality and timestamp order. Controlled blocked-worker tests prove V3 and V4 steal queued work. A 24,000-task, six-producer test stresses V4 CAS contention. CLI tests inspect PLY files, raw datasets, profiling fields and the interactive menu/export flow. CTest enforces timeouts for deadlock detection.

For race detection, use a separate Linux build:

```sh
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON -DENABLE_OPENMP=OFF
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

Keep sanitizer results separate from Release timings. OpenMP is disabled for that run because runtime/sanitizer compatibility varies. See `docs/final_report.md` for actual measurements, methodology and limitations.

To build a Linux toolchain and run both Release and sanitizer checks from Windows using Docker Desktop:

```powershell
docker build -f scripts/Dockerfile.testing -t vehicle-threadpool-test .
docker run --rm --security-opt seccomp=unconfined --mount "type=bind,source=$((Get-Location).Path),target=/workspace" vehicle-threadpool-test sh scripts/run_linux_checks.sh --full
python scripts/plot_results.py results/full_linux/csv/benchmark_results.csv --output results/full_linux/graphs
python scripts/validate_artifacts.py --csv results/full_linux/csv/benchmark_results.csv --config config/experiments.json
```

The Docker image provides Debian, Clang 19, OpenMP, CMake, Ninja and Python. Removing `--full` runs only checks. The script first runs an intentional-race positive control and requires a genuine TSan report, then runs the project tests with `halt_on_error=1`. No sanitizer warnings are suppressed. The container's seccomp setting allows the runtime's per-process address-space setup; it does not change host ASLR. Logs are saved under `results/linux_tests/`. [ThreadSanitizer's supported platforms](https://clang.llvm.org/docs/ThreadSanitizer.html) exclude native Windows; use this Linux route for V0–V4 race detection. Even a clean run does not prove race freedom.

Repository artifacts: benchmark summary CSVs, plots, reports and test logs are tracked. Generated datasets, PLY clouds and per-task/worker detail files are excluded to keep the repository small. Run the full Docker workflow above to regenerate those files before validating complete artifacts or following cloud/detail links in the CSVs. The committed artifact validation records describe the original local run.

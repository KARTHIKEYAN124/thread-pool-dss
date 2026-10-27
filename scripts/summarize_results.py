"""Write an evidence-based project report from a completed benchmark matrix."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
import platform
import os
import json
from statistics import mean, stdev


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", default="results/csv/benchmark_results.csv")
    parser.add_argument("--output", default="docs/final_report.md")
    parser.add_argument("--compiler", default="See CMakeCache.txt for toolchain")
    parser.add_argument("--processor", default=platform.processor())
    parser.add_argument("--platform", default=platform.platform())
    parser.add_argument("--config", default="config/experiments.json")
    parser.add_argument("--graphs-directory")
    args = parser.parse_args()
    with open(args.csv, newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise SystemExit("No results")
    groups = defaultdict(list)
    for row in rows:
        key = tuple(row[k] for k in ("workload", "mode", "lidar_points", "fft_size", "threads", "scheduler"))
        groups[key].append(row)
    failures = {k: sum(int(r[k]) for r in rows) for k in ("missing", "duplicates", "mismatches")}
    max_threads = max(int(r["threads"]) for r in rows)
    config = json.loads(Path(args.config).read_text())
    graphs = Path(args.graphs_directory) if args.graphs_directory else Path(args.csv).parent.parent/'graphs'
    tsan_log = Path('results/linux_tests/tsan_tests.txt')
    probe_log = Path('results/linux_tests/tsan_probe.txt')
    tsan_passed = tsan_log.exists() and '100% tests passed' in tsan_log.read_text() and probe_log.exists() and 'ThreadSanitizer: data race' in probe_log.read_text()
    text = ["# Design and Evaluation of Work-Stealing Thread Pools",
            "", "## Scope", "",
            "This C++20 application compares bounded thread-per-task execution (V0), a shared FIFO pool (V1), independent round-robin FIFO queues (V2), mutex-protected work-stealing deques (V3), and atomic work-stealing stacks (V4). OpenMP dynamic scheduling supplies the external runtime baseline. An interactive terminal menu supports configuration, raw dataset generation, scheduler comparisons, correctness validation and exporting a results bundle.",
            "", "V4 uses per-worker Treiber stacks with CAS push/pop/steal and immutable node links. Nodes are retained until workers join to prevent reclamation races and address-reuse ABA. Queue operations are lock-free on the tested pointer-atomic platform; allocation, admission, sleeping, shutdown and statistics are not lock-free. Owners and thieves both take LIFO work. This is an atomic-stack variant, not a Chase-Lev FIFO-steal deque; retained-node memory grows with accepted tasks.",
            "", "## Reproduction and environment", "",
            f"- Host: {args.platform}", f"- Processor: {args.processor}; {os.cpu_count()} host logical CPUs.",
            f"- Toolchain: {args.compiler}", "- Build: CMake, C++20, Release optimization.",
            f"- Source data: `{args.csv}`; {len(rows)} measured scheduler runs.",
            f"- Seed(s): {', '.join(sorted({r['seed'] for r in rows}))}.",
            f"- Concurrency limits: {', '.join(sorted({r['threads'] for r in rows}, key=int))}.",
            f"- Executed matrix: `{args.config}`; {config['repeats']} repeats and {config['warmups']} unrecorded warm-up(s) per configuration. Worker counts exceeding host logical CPUs are skipped and logged.",
            "", "```sh", "docker build -f scripts/Dockerfile.testing -t vehicle-threadpool-test .",
            'docker run --rm --security-opt seccomp=unconfined --mount "type=bind,source=YOUR_PROJECT_PATH,target=/workspace" vehicle-threadpool-test sh scripts/run_linux_checks.sh --full',
            f"python scripts/validate_artifacts.py --csv {args.csv} --config {args.config}",
            f"python scripts/plot_results.py {args.csv} --output {graphs.as_posix()}",
            f'python scripts/summarize_results.py --csv {args.csv} --config {args.config} --graphs-directory {graphs.as_posix()} --compiler "{args.compiler}" --processor "{args.processor}" --platform "{args.platform}"', "```", "",
            "The Docker script builds and tests Linux Release and ThreadSanitizer configurations, then executes the full configured matrix. Native Windows builds use `scripts/build_windows.ps1`; launch the menu with `build/vehicle_benchmark.exe --interactive`.",
            "", "## Workloads and correctness", "",
            "Mixed inputs merge five configured arrival streams: speed at 100 Hz, wheel vibration at 100 Hz, temperature at 5 Hz, CAN at 1000 Hz, and LiDAR at 10 Hz. Complete speed windows avoid scheduler-dependent state updates. Processing comprises moving averages, a radix-2 FFT, threshold checks, CAN decoding/checksum validation, and deterministic voxel centroid statistics. Light, heavy and deliberately imbalanced synthetic workloads stress different costs; burst workloads release arrivals in 100 ms groups.",
            "", "Each scheduler receives the same immutable data and is checked against independently calculated serial outputs. Known-answer algorithm tests complement reference equivalence. Concurrency tests cover exactly-once execution, multiple producers, exception reporting, draining shutdown, shutdown/submission races, reuse and a blocked-owner scenario that requires stealing.",
            "", f"Measured runs reported {failures['missing']} missing tasks, {failures['duplicates']} duplicate executions and {failures['mismatches']} reference mismatches across {sum(int(r['completed']) for r in rows):,} completed tasks.",
            "", "## Measurement method", "",
            "Steady-clock elapsed time includes executor construction, submission, drain and shutdown. Dataset generation, serial result preparation, correctness checking and file output occur outside the interval. A serial run uses the same timing wrapper for each repeat. Throughput is completed tasks divided by elapsed time; speedup is serial elapsed divided by measured elapsed; efficiency is speedup divided by requested concurrency. Queue delay, execution time and submission-to-completion latency are recorded separately. p95/p99 use nearest-rank percentiles. Repeat order rotates among schedulers.",
            "", "Streaming elapsed time includes arrival pacing. Arrival-to-finish latency includes producer lateness; deadlines use that latency. OpenMP dispatches its accumulated batch at wait(), so its response latency includes batching, and it is excluded from streaming runs. V0 concurrency is a bounded batch limit. V0/OpenMP idle time is unavailable. OpenMP waits use the PASSIVE policy in the experiment runner to reduce idle-runtime spin between scheduler measurements.",
            "", "Process CPU and context switches are measured with getrusage(RUSAGE_SELF) around the execution interval on Linux, including terminated worker threads. CPU percent = 100 * (user + kernel CPU seconds) / elapsed seconds, so 100% represents one fully used logical CPU and process utilization may exceed 100%. Machine CPU percent divides by visible logical CPU count. These process-wide counters include scheduler/runtime overhead. Context switches are Linux guest counters, not host Windows ETW events. Snapshot calls sit just outside wall-clock endpoints, introducing small boundary error, and short runs are sensitive to timer resolution. Windows uses GetProcessTimes for CPU and exports context switches as unavailable, rather than inventing counts. V4 reclaims its task nodes during timed shutdown after all workers have joined.",
            "", f"## Measured comparison at concurrency {max_threads}", "",
            "Throughput is mean +/- sample standard deviation over repeats. p99 is the mean of each repeat's p99, rather than a pooled percentile. The measured matrix supports comparisons on this host and does not establish universal scheduler rankings.", "",
            "| Workload / mode / LiDAR points / FFT size | Scheduler | Tasks/s (mean +/- SD) | p99 response (us) | Speedup | Efficiency | Steals (mean) |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: |"]
    for key, entries in sorted(groups.items()):
        workload, mode, points, fft, threads, scheduler = key
        if int(threads) != max_threads:
            continue
        values = [float(r["throughput"]) for r in entries]
        deviation = stdev(values) if len(values) > 1 else 0
        text.append(f"| {workload} / {mode} / {points} / {fft} | {scheduler} | {mean(values):,.1f} +/- {deviation:,.1f} | {mean(float(r['p99_us']) for r in entries):,.1f} | {mean(float(r['speedup']) for r in entries):.3f} | {mean(float(r['efficiency']) for r in entries):.3f} | {mean(int(r['successful_steals']) for r in entries):.1f} |")
    text += ["", "## OS profiling at maximum measured concurrency", "",
             "| Workload / LiDAR points / FFT size | Scheduler | Process CPU % (mean) | Machine CPU % (mean) | Voluntary switches (mean) | Involuntary switches (mean) |",
             "| --- | --- | ---: | ---: | ---: | ---: |"]
    for key, entries in sorted(groups.items()):
        if int(key[4]) != max_threads or not all(r.get('cpu_available') == '1' for r in entries):
            continue
        switches = all(r.get('context_switches_available') == '1' for r in entries)
        voluntary = f"{mean(int(r['voluntary_context_switches']) for r in entries):.1f}" if switches else 'unavailable'
        involuntary = f"{mean(int(r['involuntary_context_switches']) for r in entries):.1f}" if switches else 'unavailable'
        text.append(f"| {key[0]} / {key[2]} / {key[3]} | {key[5]} | {mean(float(r['cpu_percent']) for r in entries):.1f} | {mean(float(r['machine_cpu_percent']) for r in entries):.1f} | {voluntary} | {involuntary} |")
    text += ["", "## ThreadSanitizer evidence", ""]
    text.append('The Linux Clang ThreadSanitizer positive control reported the deliberately injected race and exited with the expected failure status. All project CTest suites then passed with halt_on_error=1 and no race reports. Logs: `results/linux_tests/tsan_probe.txt`, `results/linux_tests/tsan_tests.txt`, and `results/linux_checks_log.txt`. OpenMP is excluded from the sanitized build; V0 through V4 are covered. This is evidence about executed paths, not a proof of race freedom.' if tsan_passed else 'No completed, verified ThreadSanitizer run was found. Do not interpret ordinary stress tests as sanitizer evidence.')
    text += ["", "## Complete point-cloud exports", "",
             "LiDAR processing returns every downsampled voxel centroid, including averaged XYZ and intensity. Every benchmark compares the full cloud to its serial result. Complete ASCII PLY files are exported outside measured intervals and referenced by cloud_file in each task CSV. Content-addressed files are shared by verified scheduler runs. Raw input datasets and raw PLY clouds can be exported separately with --generate-only --dataset-output PATH.",
             "", "## Figures", ""]
    artifact_path = Path(args.csv).parent.parent/'artifact_validation.json'
    if artifact_path.exists() and artifact_path.stat().st_mtime >= Path(args.csv).stat().st_mtime:
        evidence = json.loads(artifact_path.read_text())
        assert evidence['matrix_runs'] == len(rows) and evidence['completed_tasks'] == sum(int(r['completed']) for r in rows)
        text[-3:-3] = ["", "## Independent artifact verification", "",
                       f"The artifact checker verified all configured run keys, per-worker accounting, exactly-once task records and OS counter calculations. It read {evidence['unique_clouds']:,} unique PLY clouds containing {evidence['exported_vertices']:,} vertices, checked finite XYZ/intensity values and vertex counts, and recomputed mean centroid ranges against task CSVs. Evidence: `{artifact_path.as_posix()}`.", ""]
    for pattern, caption in (("heavy_batch_*_12000_256_*.png", "Heavy-workload scaling and latency"),
                             ("imbalanced_batch_*.png", "Deliberately imbalanced workload")):
        for figure in sorted(graphs.glob(pattern)):
            relative = os.path.relpath(figure, Path(args.output).parent).replace(os.sep, "/")
            text += [f"![{caption}]({relative})", ""]
    text += ["", "## Observed trade-offs", ""]
    for workload in ("light", "mixed", "heavy", "imbalanced"):
        subset = [(key, entries) for key, entries in groups.items() if key[0] == workload and key[1] == "batch" and key[2] == "12000" and int(key[4]) == max_threads]
        if not subset:
            continue
        best = max(subset, key=lambda pair: mean(float(r["throughput"]) for r in pair[1]))
        text.append(f"- {workload.capitalize()}: {best[0][-1]} had the highest mean throughput at concurrency {max_threads} in this matrix ({mean(float(r['throughput']) for r in best[1]):,.0f} tasks/s).")
        by_name = {key[-1]: entries for key, entries in subset}
        if "v2" in by_name and "v3" in by_name:
            ratio = mean(float(r["throughput"]) for r in by_name["v3"]) / mean(float(r["throughput"]) for r in by_name["v2"])
            text.append(f"  V3/V2 mean throughput ratio: {ratio:.3f}; V3 averaged {mean(int(r['successful_steals']) for r in by_name['v3']):.1f} successful steals. Steal counts demonstrate redistribution; they do not by themselves prove a performance gain.")
        if 'v3' in by_name and 'v4' in by_name:
            ratio = mean(float(r['throughput']) for r in by_name['v4'])/mean(float(r['throughput']) for r in by_name['v3'])
            text.append(f"  V4/V3 mean throughput ratio: {ratio:.3f}; atomic queues remove queue mutexes but retain allocation and lifecycle/accounting costs, and change the victim's removal order.")
    text += ["", "## Limitations and interpretation", "",
             "These are measurements in a Linux container under WSL2 on one general-purpose Windows host, without CPU affinity, frequency pinning, thermal control or isolated cores. Startup and instrumentation can dominate light workloads. Three repeats provide descriptive variation, not a statistical significance claim. The exact standard-library random distribution may differ across compiler implementations. Linux-container timings must not be pooled with older native Windows validation timings. Further independent repetitions are needed before drawing research conclusions.",
             "", "Pool workers share a completion/accounting mutex; V3 also scans victims and uses try-lock acquisition. These implementation costs can offset load-balancing gains. V2 can win with balanced queues. Equal task counts do not imply equal work: worker figures include busy time as well as counts. OpenMP uses a different dispatch model. V0 bounds outstanding work and blocks its producer between batches; its short per-task response times must be interpreted alongside total completion time. Pool queues have no backpressure bound. Streaming deadlines are diagnostic and there is no hard real-time guarantee. CPU/context-switch counts measure resource use and do not attribute individual switches to a particular lock or runtime operation.",
             "", "## Deliverables", "",
             "- `build/vehicle_benchmark.exe`: native Windows executable and interactive menu.",
             "- `build-linux/vehicle_benchmark`: Linux executable used by the full matrix; toolchain/cache and binary/source SHA-256 evidence are retained.",
             f"- `{args.csv}`: measured summaries; sibling `details/` and `clouds/` contain processed outputs, timings and full PLY clouds.",
             f"- `{graphs.as_posix()}`: throughput, p99 latency, speedup, efficiency, CPU/context-switch profiling and worker distributions.",
             "- `results/test_results.txt`: test execution evidence.",
             "- `tests/`: known-answer, concurrency, lifecycle, CLI and CSV checks.", ""]
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(text), encoding="utf-8")
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()

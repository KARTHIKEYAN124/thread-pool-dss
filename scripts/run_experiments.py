"""Run a controlled experiment matrix. Every invocation verifies against serial outputs."""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default="build/vehicle_benchmark.exe" if os.name == "nt" else "build/vehicle_benchmark")
    parser.add_argument("--config", default="config/experiments.json")
    parser.add_argument("--output", default="results/csv/benchmark_results.csv")
    args = parser.parse_args()
    cfg = json.loads(Path(args.config).read_text())
    environment = os.environ.copy()
    environment.setdefault('OMP_WAIT_POLICY','PASSIVE')
    environment.setdefault('OMP_DYNAMIC','FALSE')
    exe = str(Path(args.exe).resolve())
    available = subprocess.check_output([exe, "--list-schedulers"], text=True).split()
    print("Available schedulers:", ", ".join(available), flush=True)
    if "openmp" not in available:
        print("OpenMP baseline unavailable: install an OpenMP-enabled compiler/runtime to include it.", flush=True)
    first = True
    for scenario in cfg["scenarios"]:
        for threads in cfg["workers"]:
            if threads > (os.cpu_count() or 1):
                print(f'Skipping {threads} workers: host reports {os.cpu_count() or 1} logical CPUs.',flush=True)
                continue
            command = [exe, "--scheduler", "all", "--threads", str(threads),
                       "--tasks", str(scenario.get("tasks", cfg["tasks"])),
                       "--seed", str(cfg["seed"]), "--repeats", str(cfg["repeats"]),
                       "--warmups", str(cfg["warmups"]), "--workload", scenario["workload"],
                       "--mode", scenario.get("mode", "batch"),
                       "--lidar-points", str(scenario.get("lidar_points", 12000)),
                       "--fft-size", str(scenario.get("fft_size", 256)), "--output", args.output]
            if not first:
                command.append("--append")
            print("Running:", " ".join(command), flush=True)
            subprocess.run(command, check=True, env=environment)
            first = False


if __name__ == "__main__":
    main()

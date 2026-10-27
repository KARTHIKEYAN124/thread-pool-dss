"""Generate figures from measured CSVs; preserve mode, task sizes and seed groups."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path
from statistics import mean, stdev
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv", nargs="?", default="results/csv/benchmark_results.csv")
    parser.add_argument("--output", default="results/graphs")
    parser.add_argument("--worker-plots", choices=("representative", "all"), default="representative")
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    with open(args.csv, newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise SystemExit("No benchmark rows found")
    groups = defaultdict(list)
    for row in rows:
        if any(int(row[key]) for key in ("missing", "duplicates", "mismatches")):
            raise SystemExit("Refusing to plot invalid benchmark results")
        key = tuple(row[k] for k in ("workload", "mode", "tasks", "lidar_points", "fft_size", "seed", "noise", "speed_hz", "wheel_hz", "temperature_hz", "can_hz", "lidar_hz", "deadline_us"))
        groups[key].append(row)
    plt.rcParams.update({"figure.dpi": 140, "axes.grid": True, "grid.alpha": .2})
    metrics = [("throughput", "Completed tasks / second"), ("p99_us", "p99 response latency (microseconds)"),
               ("speedup", "Speedup vs serial"), ("efficiency", "Speedup / concurrency limit")]
    for key, entries in groups.items():
        tag = "_".join(key)
        fig, axes = plt.subplots(2, 2, figsize=(12, 8), constrained_layout=True)
        for ax, (metric, label) in zip(axes.flat, metrics):
            for scheduler in sorted({r["scheduler"] for r in entries}):
                by_thread = defaultdict(list)
                for row in entries:
                    if row["scheduler"] == scheduler:
                        by_thread[int(row["threads"])].append(float(row[metric]))
                x = sorted(by_thread)
                ax.errorbar(x, [mean(by_thread[n]) for n in x],
                            yerr=[stdev(by_thread[n]) if len(by_thread[n]) > 1 else 0 for n in x],
                            marker="o", capsize=3, label=scheduler)
            ax.set(xlabel="Workers (V0: batch concurrency limit)", ylabel=label)
            ax.set_xticks(sorted({int(row["threads"]) for row in entries}))
            ax.legend(fontsize=8)
        fig.suptitle(f"{key[0]} / {key[1]} | tasks={key[2]}, LiDAR={key[3]}, FFT={key[4]}, seed={key[5]}\nMean +/- sample standard deviation across repeats")
        fig.savefig(output / f"{tag}.png")
        plt.close(fig)
        if all(row.get('cpu_available') == '1' for row in entries):
            fig, axes = plt.subplots(1, 2, figsize=(12, 4), constrained_layout=True)
            for scheduler in sorted({r['scheduler'] for r in entries}):
                selected = [r for r in entries if r['scheduler'] == scheduler]
                by_thread = defaultdict(list)
                for row in selected:
                    by_thread[int(row['threads'])].append(row)
                x = sorted(by_thread)
                axes[0].plot(x,[mean(float(r['cpu_percent']) for r in by_thread[n]) for n in x],marker='o',label=scheduler)
                if all(r.get('context_switches_available') == '1' for r in selected):
                    axes[1].plot(x,[mean(int(r['voluntary_context_switches'])+int(r['involuntary_context_switches']) for r in by_thread[n]) for n in x],marker='o',label=scheduler)
            axes[0].set(xlabel='Concurrency',ylabel='Process CPU utilization (%)\n100% = one logical CPU')
            axes[1].set(xlabel='Concurrency',ylabel='Context switches\n(voluntary + involuntary)')
            for ax in axes:
                ax.set_xticks(sorted({int(r['threads']) for r in entries}))
                if ax.lines: ax.legend(fontsize=8)
            fig.suptitle(f'{key[0]} / {key[1]} | OS resource counters, averaged over repeats')
            fig.savefig(output / f'{tag}_profiling.png')
            plt.close(fig)
    details = Path(args.csv).parent / "details"
    max_threads = max(int(row["threads"]) for row in rows)
    for path in sorted(details.glob(Path(args.csv).stem + "_*_workers.csv")):
        if args.worker_plots == "representative" and not (f"_t{max_threads}_" in path.name and "_r0_workers.csv" in path.name):
            continue
        with path.open(newline="") as source:
            workers = list(csv.DictReader(source))
        fig, axes = plt.subplots(1, 2, figsize=(9, 3), constrained_layout=True)
        ids = [int(w["worker"]) for w in workers]
        worker_label = "Batch slot" if "_v0_" in path.name else "Worker"
        axes[0].bar(ids, [int(w["tasks"]) for w in workers])
        axes[0].set(xlabel=worker_label, ylabel="Tasks completed")
        axes[1].bar(ids, [float(w["busy_us"]) / 1000 for w in workers])
        axes[1].set(xlabel=worker_label, ylabel="Task busy time (ms)")
        fig.savefig(output / (path.stem + ".png"))
        plt.close(fig)
    print(f"Saved figures to {output}")


if __name__ == "__main__":
    main()

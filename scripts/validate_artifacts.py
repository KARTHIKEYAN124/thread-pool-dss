"""Validate full-matrix coverage, OS metrics, timings and exported PLY geometry."""
import argparse
import csv
import json
import math
import os
from pathlib import Path


def read_csv(path):
    with Path(path).open(newline='') as source:
        return list(csv.DictReader(source))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', default='results/full_linux/csv/benchmark_results.csv')
    parser.add_argument('--config', default='config/experiments.json')
    parser.add_argument('--logical-cpus', type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    summary = Path(args.csv)
    rows = read_csv(summary)
    config = json.loads(Path(args.config).read_text())
    key_fields = ('scheduler','threads','tasks','workload','mode','lidar_points','fft_size','seed','run')
    actual = [tuple(row[field] for field in key_fields) for row in rows]
    expected = set()
    for scenario in config['scenarios']:
        mode = scenario.get('mode','batch')
        schedulers = ['v0','v1','v2','v3','v4'] + (['openmp'] if mode == 'batch' else [])
        for threads in config['workers']:
            if threads > args.logical_cpus:
                continue
            for scheduler in schedulers:
                for repeat in range(config['repeats']):
                    expected.add(tuple(map(str,(scheduler,threads,scenario.get('tasks',config['tasks']),scenario['workload'],mode,
                                               scenario.get('lidar_points',12000),scenario.get('fft_size',256),config['seed'],repeat))))
    assert len(actual) == len(set(actual)), 'Duplicate summary run keys'
    assert set(actual) == expected, f'Matrix mismatch: missing={expected-set(actual)}, extra={set(actual)-expected}'
    clouds = {}
    completed = 0
    for row in rows:
        assert int(row['completed']) == int(row['tasks'])
        assert all(int(row[field]) == 0 for field in ('missing','duplicates','mismatches'))
        assert row['cpu_available'] == row['context_switches_available'] == '1', 'Linux OS counters missing'
        cpu_seconds = float(row['cpu_user_s'])+float(row['cpu_kernel_s'])
        assert cpu_seconds >= 0
        assert math.isclose(float(row['cpu_percent']),100*cpu_seconds/float(row['elapsed_s']),rel_tol=1e-8,abs_tol=1e-8)
        assert all(int(row[field]) >= 0 for field in ('voluntary_context_switches','involuntary_context_switches'))
        label = (f"{summary.stem}_{row['scheduler']}_{row['workload']}_{row['mode']}_t{row['threads']}"
                 f"_n{row['tasks']}_p{row['lidar_points']}_f{row['fft_size']}_s{row['seed']}_r{row['run']}")
        details = summary.parent/'details'
        tasks = read_csv(details/(label+'_tasks.csv'))
        workers = read_csv(details/(label+'_workers.csv'))
        assert len(tasks) == int(row['tasks']) == sum(int(worker['tasks']) for worker in workers)
        assert len({task['id'] for task in tasks}) == len(tasks)
        for task in tasks:
            assert int(task['executions']) == 1
            assert float(task['submitted_us']) <= float(task['start_us']) <= float(task['finish_us'])
            if task['sensor'] != 'lidar':
                assert not task['cloud_file']
                continue
            path = (details/task['cloud_file']).resolve()
            if path not in clouds:
                with path.open() as source:
                    header = []
                    for line in source:
                        header.append(line.strip())
                        if line.strip() == 'end_header':
                            break
                    assert header[:2] == ['ply','format ascii 1.0']
                    vertex_count = int(next(line.split()[-1] for line in header if line.startswith('element vertex ')))
                    count, distance = 0, 0.0
                    for line in source:
                        point = list(map(float,line.split()))
                        assert len(point) == 4 and all(math.isfinite(value) for value in point)
                        distance += math.sqrt(sum(value*value for value in point[:3]))
                        count += 1
                    assert count == vertex_count and count > 0
                    clouds[path] = (count,distance/count)
            count, mean_range = clouds[path]
            assert count == int(task['retained_points'])
            assert math.isclose(mean_range,float(task['value']),rel_tol=1e-9,abs_tol=1e-9)
        completed += len(tasks)
    evidence = {'matrix_runs':len(rows),'completed_tasks':completed,'unique_clouds':len(clouds),
                'exported_vertices':sum(value[0] for value in clouds.values()),
                'voluntary_context_switches':sum(int(row['voluntary_context_switches']) for row in rows),
                'involuntary_context_switches':sum(int(row['involuntary_context_switches']) for row in rows),
                'missing':0,'duplicates':0,'mismatches':0,'matrix_complete':True,'ply_geometry_verified':True}
    target = summary.parent.parent/'artifact_validation.json'
    target.write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(evidence,indent=2))


if __name__ == '__main__':
    main()

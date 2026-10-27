"""Exercise CLI validation, CSV output and streaming arrival/deadline accounting."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="vehicle-cli-") as temporary:
    output = Path(temporary) / "summary.csv"
    base = [exe, "--tasks", "30", "--threads", "2", "--repeats", "1", "--warmups", "0",
            "--lidar-points", "64", "--output", str(output)]
    subprocess.run(base + ["--scheduler", "all"], check=True, capture_output=True, text=True)
    with output.open(newline="") as source:
        rows = list(csv.DictReader(source))
    assert {"v0", "v1", "v2", "v3", "v4"} <= {r["scheduler"] for r in rows}
    for row in rows:
        assert int(row["completed"]) == 30
        assert all(int(row[key]) == 0 for key in ("missing", "duplicates", "mismatches"))
        assert row['cpu_available'] == '1' and float(row['cpu_percent']) >= 0
        if sys.platform.startswith('linux'):
            assert row['context_switches_available'] == '1'
            assert int(row['voluntary_context_switches']) >= 0 and int(row['involuntary_context_switches']) >= 0
        else:
            assert row['context_switches_available'] == '0' and row['voluntary_context_switches'] == ''
    details = list((output.parent / "details").glob("*_tasks.csv"))
    assert len(details) == len(rows)
    for path in details:
        with path.open(newline="") as source:
            tasks = list(csv.DictReader(source))
        assert len(tasks) == 30 and len({r["id"] for r in tasks}) == 30
        for task in tasks:
            assert float(task["submitted_us"]) <= float(task["start_us"]) <= float(task["finish_us"])
            assert int(task["executions"]) == 1
            if task['sensor'] == 'lidar':
                cloud_path = path.parent / task['cloud_file']
                lines = cloud_path.read_text().splitlines()
                end = lines.index('end_header')
                assert len(lines[end+1:]) == int(task['retained_points'])
                assert f"element vertex {task['retained_points']}" in lines
                assert all(len(line.split()) == 4 for line in lines[end+1:])
    subprocess.run(base + ["--scheduler", "v3", "--mode", "streaming", "--workload", "light",
                           "--deadline-us", "1000000", "--append"], check=True, capture_output=True, text=True)
    with output.open(newline="") as source:
        appended = list(csv.DictReader(source))
    assert len(appended) == len(rows) + 1
    streaming = appended[-1]
    assert streaming["mode"] == "streaming" and int(streaming["deadline_misses"]) == 0
    assert float(streaming["elapsed_s"]) >= .029
    dataset = Path(temporary) / 'raw.csv'
    subprocess.run([exe,'--generate-only','--tasks','30','--lidar-points','16','--dataset-output',str(dataset)],check=True,capture_output=True,text=True)
    with dataset.open(newline='') as source:
        raw = list(csv.DictReader(source))
    assert len(raw) == 30
    for reading in raw:
        if reading['sensor'] == 'lidar':
            lines = (dataset.parent / reading['raw_cloud']).read_text().splitlines()
            assert len(lines[lines.index('end_header')+1:]) == 16
    subprocess.run([exe,'--generate-only','--tasks','5','--lidar-points','16','--dataset-output','raw_local.csv'],
                   cwd=temporary,check=True,capture_output=True,text=True)
    with (Path(temporary)/'raw_local.csv').open(newline='') as source:
        local_raw = list(csv.DictReader(source))
    assert all((Path(temporary)/reading['raw_cloud']).exists() for reading in local_raw if reading['sensor'] == 'lidar')
    menu = subprocess.run([exe,'--interactive'], input='0\n',capture_output=True,text=True,check=True)
    assert 'Run V4' in menu.stdout and 'Export latest results bundle' in menu.stdout
    menu_work = Path(temporary)/'menu'; menu_work.mkdir()
    menu = subprocess.run([exe,'--interactive'],cwd=menu_work,
                          input='10\n12\n2\n42\nlight\nbatch\n7\n11\nresults/menu/csv/loop\n11\nbundle\n0\n',capture_output=True,text=True,check=True)
    assert 'Completed successfully' in menu.stdout, menu.stdout + menu.stderr
    assert 'Choose a new directory outside the current results directory.' in menu.stdout
    assert not (menu_work/'results/menu/csv/loop').exists()
    with (menu_work/'bundle/benchmark_results.csv').open(newline='') as source:
        menu_rows = list(csv.DictReader(source))
    assert len(menu_rows) == 3 and all(row['scheduler'] == 'v4' for row in menu_rows)
    for options in (["--threads", "0"], ["--tasks", "-1"], ["--tasks", "3x"],
                    ["--mode", "invalid"], ["--scheduler", "missing"], ["--fft-size", "3"],
                    ["--rates", "1,2"], ["--rates", "1,2,3,4,0"], ["--noise", "nan"],
                    ["--unknown", "1"]):
        result = subprocess.run([exe] + options, capture_output=True, text=True)
        assert result.returncode != 0, options
print("[PASS] CLI options, summary/detail CSVs, append, streaming timing and deadline accounting")

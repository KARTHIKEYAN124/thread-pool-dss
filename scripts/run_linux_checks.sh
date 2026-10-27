#!/bin/sh
set -eu
cd /workspace
mkdir -p results/linux_tests
export OMP_WAIT_POLICY=PASSIVE
export OMP_DYNAMIC=FALSE
{
    uname -a
    clang++-19 --version
    cmake --version
    getconf _NPROCESSORS_ONLN
} > results/linux_tests/environment.txt
cmake -S . -B /tmp/vehicle-release -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/vehicle-release -j 2
mkdir -p build-linux
cp /tmp/vehicle-release/vehicle_benchmark /tmp/vehicle-release/test_threadpool build-linux/
cp /tmp/vehicle-release/CMakeCache.txt build-linux/ReleaseCMakeCache.txt
sha256sum build-linux/vehicle_benchmark > results/linux_tests/binary_sha256.txt
find src include tests config -type f ! -name '*.pyc' -print | sort | xargs sha256sum > results/linux_tests/source_sha256.txt
ctest --test-dir /tmp/vehicle-release --output-on-failure --output-log /workspace/results/linux_tests/release_tests.txt
cmake -S . -B /tmp/vehicle-tsan -G Ninja -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TSAN=ON -DENABLE_OPENMP=OFF
cmake --build /tmp/vehicle-tsan -j 2
cp /tmp/vehicle-tsan/test_threadpool build-linux/test_threadpool_tsan
export TSAN_OPTIONS=halt_on_error=1:exitcode=66
clang++-19 -std=c++20 -O1 -g -fsanitize=thread tests/tsan_probe.cpp -o /tmp/vehicle-tsan/tsan_probe
set +e
/tmp/vehicle-tsan/tsan_probe > results/linux_tests/tsan_probe.txt 2>&1
probe_status=$?
set -e
if [ "$probe_status" -ne 66 ] || ! grep -q 'ThreadSanitizer: data race' results/linux_tests/tsan_probe.txt; then
    cat results/linux_tests/tsan_probe.txt
    echo "TSan positive control failed; refusing to claim a clean sanitizer run."
    exit 1
fi
ctest --test-dir /tmp/vehicle-tsan --output-on-failure --output-log /workspace/results/linux_tests/tsan_tests.txt
echo "TSan positive control detected its intentional race; all project tests passed without race reports."
if [ "${1:-}" = "--full" ]; then
    python3 scripts/run_experiments.py --exe /tmp/vehicle-release/vehicle_benchmark --config config/experiments.json --output results/full_linux/csv/benchmark_results.csv
fi

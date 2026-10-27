#include "benchmark/Metrics.hpp"
#include "sensor/SensorProcessor.hpp"
#include "benchmark/ProcessProfile.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
namespace vehicle {
using Clock = std::chrono::steady_clock;
double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    auto index = static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size())));
    return v[std::clamp<std::size_t>(index, 1, v.size()) - 1];
}
BenchmarkResult benchmark(const std::vector<SensorReading>& data, const std::vector<ProcessedReading>& ref,
                          const std::string& scheduler, std::size_t threads, bool streaming, double deadline_us) {
    if (data.empty() || data.size() != ref.size()) throw std::invalid_argument("Invalid benchmark dataset");
    if (streaming && scheduler == "openmp") throw std::invalid_argument("OpenMP baseline supports batch mode only");
    BenchmarkResult result; result.tasks.resize(data.size());
    auto seen = std::make_unique<std::atomic<unsigned>[]>(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) seen[i].store(0);
    // Timing includes executor startup, submission, drain and shutdown for every scheduler.
    const auto profile_start = process_snapshot();
    const auto epoch = Clock::now();
    auto now = [&] { return std::chrono::duration<double, std::micro>(Clock::now()-epoch).count(); };
    std::unique_ptr<IExecutor> executor;
    if (scheduler != "serial") executor = make_executor(scheduler, threads);
    const auto release_epoch = Clock::now();
    const double release_offset = std::chrono::duration<double, std::micro>(release_epoch-epoch).count();
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (streaming) std::this_thread::sleep_until(release_epoch + std::chrono::microseconds(data[i].timestamp_us));
        auto& timing = result.tasks[i];
        timing.arrival_us = streaming ? release_offset + static_cast<double>(data[i].timestamp_us) : now();
        timing.submitted_us = now();
        Task task = [&, i](std::size_t worker) {
            if (seen[i].fetch_add(1) != 0) return; // count duplicates without racing on output
            auto& t = result.tasks[i]; t.worker = worker; t.start_us = now();
            t.output = process(data[i]); t.finish_us = now();
        };
        if (executor) executor->submit(std::move(task)); else task(0);
    }
    if (executor) { executor->wait(); executor->shutdown(); }
    result.elapsed_s = now() / 1000000;
    const auto profile_end = process_snapshot();
    result.cpu_available = profile_start.cpu_available && profile_end.cpu_available;
    result.switches_available = profile_start.switches_available && profile_end.switches_available;
    if (result.cpu_available) {
        result.cpu_user_s = profile_end.user_s-profile_start.user_s;
        result.cpu_kernel_s = profile_end.kernel_s-profile_start.kernel_s;
        result.cpu_percent = 100*(result.cpu_user_s+result.cpu_kernel_s)/result.elapsed_s;
        result.machine_cpu_percent = result.cpu_percent/static_cast<double>(std::max(1u,std::thread::hardware_concurrency()));
    }
    if (result.switches_available) {
        result.voluntary_switches = profile_end.voluntary_switches-profile_start.voluntary_switches;
        result.involuntary_switches = profile_end.involuntary_switches-profile_start.involuntary_switches;
    }
    if (executor) result.executor = executor->stats(); else result.executor.workers.resize(1);
    std::vector<double> response, arrival;
    for (std::size_t i = 0; i < data.size(); ++i) {
        auto& t = result.tasks[i]; t.executions = seen[i].load();
        if (!t.executions) { ++result.missing; continue; }
        result.duplicates += t.executions - 1; ++result.completed;
        if (!(t.output == ref[i])) ++result.mismatches;
        const auto latency = t.finish_us-t.submitted_us;
        result.queue_mean_us += t.start_us-t.submitted_us;
        result.execution_mean_us += t.finish_us-t.start_us;
        response.push_back(latency); arrival.push_back(t.finish_us-t.arrival_us);
        if (t.finish_us-t.arrival_us > deadline_us) ++result.deadline_misses;
        if (!executor) { ++result.executor.workers[0].tasks; result.executor.workers[0].busy_us += t.finish_us-t.start_us; }
    }
    const double count = static_cast<double>(result.completed);
    if (count) {
        result.queue_mean_us /= count; result.execution_mean_us /= count;
        result.response_mean_us = std::accumulate(response.begin(), response.end(), 0.0) / count;
    }
    result.p95_us = percentile(response, .95); result.p99_us = percentile(response, .99);
    result.arrival_p99_us = percentile(arrival, .99);
    result.throughput = count / result.elapsed_s;
    return result;
}
std::vector<std::filesystem::path> export_clouds(const std::filesystem::path& directory, const std::vector<ProcessedReading>& readings) {
    std::vector<std::filesystem::path> paths(readings.size());
    for (std::size_t i = 0; i < readings.size(); ++i) {
        const auto& points = readings[i].points;
        if (points.empty()) continue;
        // Content-addressed exports avoid writing identical verified clouds for
        // every scheduler/repeat/worker-count combination. Hashing is untimed.
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto& p : points) for (double value : {p.x,p.y,p.z,p.intensity}) {
            auto bits = std::bit_cast<std::uint64_t>(value);
            for (int byte = 0; byte < 8; ++byte) { hash ^= bits & 255; hash *= 1099511628211ull; bits >>= 8; }
        }
        std::ostringstream name; name << "cloud_" << std::hex << hash << '_' << std::dec << points.size() << ".ply";
        paths[i] = directory/name.str();
        if (std::filesystem::exists(paths[i])) continue;
        std::filesystem::create_directories(directory);
        const auto temporary = std::filesystem::path(paths[i].string()+".tmp");
        std::ofstream cloud(temporary); cloud.exceptions(std::ios::failbit|std::ios::badbit);
        cloud << "ply\nformat ascii 1.0\ncomment voxel_size 0.5\nelement vertex " << points.size()
              << "\nproperty double x\nproperty double y\nproperty double z\nproperty double intensity\nend_header\n" << std::setprecision(17);
        for (const auto& p : points) cloud << p.x << ' ' << p.y << ' ' << p.z << ' ' << p.intensity << '\n';
        cloud.close(); std::filesystem::rename(temporary,paths[i]);
    }
    return paths;
}
void write_details(const std::filesystem::path& prefix, const std::vector<SensorReading>& data, const BenchmarkResult& r,
                   const std::vector<std::filesystem::path>& clouds) {
    if (!prefix.parent_path().empty()) std::filesystem::create_directories(prefix.parent_path());
    std::ofstream tasks(prefix.string()+"_tasks.csv"), workers(prefix.string()+"_workers.csv");
    tasks.exceptions(std::ios::failbit | std::ios::badbit); workers.exceptions(std::ios::failbit | std::ios::badbit);
    tasks << "id,sensor,arrival_us,submitted_us,start_us,finish_us,worker,executions,value,magnitude,retained_points,anomaly,cloud_file\n" << std::setprecision(12);
    for (std::size_t i = 0; i < data.size(); ++i) {
        const auto& t = r.tasks[i];
        tasks << data[i].id << ',' << sensor_name(data[i].type) << ',' << t.arrival_us << ',' << t.submitted_us << ',' << t.start_us << ',' << t.finish_us << ',' << t.worker << ',' << t.executions << ',' << t.output.value << ',' << t.output.magnitude << ',' << t.output.retained_points << ',' << t.output.anomaly << ',';
        if (!clouds[i].empty()) tasks << clouds[i].lexically_relative(prefix.parent_path()).generic_string();
        tasks << '\n';
    }
    workers << "worker,tasks,busy_us,idle_us\n";
    for (std::size_t i = 0; i < r.executor.workers.size(); ++i) {
        const auto& s = r.executor.workers[i]; workers << i << ',' << s.tasks << ',' << s.busy_us << ',' << s.idle_us << '\n';
    }
}
}

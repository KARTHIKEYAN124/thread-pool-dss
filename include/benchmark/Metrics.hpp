#pragma once
#include "sensor/SensorReading.hpp"
#include "threadpool/IExecutor.hpp"
#include <filesystem>
namespace vehicle {
struct TaskTiming {
    double submitted_us{}, start_us{}, finish_us{}, arrival_us{};
    std::size_t worker{};
    unsigned executions{};
    ProcessedReading output;
};
struct BenchmarkResult {
    double elapsed_s{}, throughput{}, queue_mean_us{}, execution_mean_us{}, response_mean_us{}, p95_us{}, p99_us{}, arrival_p99_us{};
    std::size_t completed{}, missing{}, duplicates{}, mismatches{}, deadline_misses{};
    ExecutorStats executor;
    std::vector<TaskTiming> tasks;
    double cpu_user_s{}, cpu_kernel_s{}, cpu_percent{}, machine_cpu_percent{};
    std::uint64_t voluntary_switches{}, involuntary_switches{};
    bool cpu_available{}, switches_available{};
};
BenchmarkResult benchmark(const std::vector<SensorReading>& data, const std::vector<ProcessedReading>& reference,
                          const std::string& scheduler, std::size_t threads, bool streaming, double deadline_us);
double percentile(std::vector<double> values, double p);
std::vector<std::filesystem::path> export_clouds(const std::filesystem::path& directory, const std::vector<ProcessedReading>& readings);
void write_details(const std::filesystem::path& prefix, const std::vector<SensorReading>& data, const BenchmarkResult& result,
                   const std::vector<std::filesystem::path>& clouds);
}

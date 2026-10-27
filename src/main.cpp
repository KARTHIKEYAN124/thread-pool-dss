#include "benchmark/Metrics.hpp"
#include "sensor/SensorProcessor.hpp"
#include "sensor/SensorSimulator.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>
int run_interactive(int (*entry)(int,char**));
using namespace vehicle;
namespace {
std::size_t number(const std::string& text) {
    std::size_t n{};
    auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), n);
    if (ec != std::errc{} || end != text.data()+text.size()) throw std::invalid_argument("Invalid unsigned integer: " + text);
    return n;
}
void help() {
    std::cout << "Vehicle sensor scheduling benchmark\n"
        "--scheduler serial|v0|v1|v2|v3|v4|openmp|all  --threads N\n"
        "--tasks N --workload light|mixed|heavy|burst|imbalanced --seed N\n"
        "--mode batch|streaming --repeats N --warmups N --deadline-us N\n"
        "--lidar-points N --fft-size N --noise N --rates 100,100,5,1000,10\n"
        "--output results/csv/benchmark_results.csv --append --preview N\n"
        "--list-schedulers --interactive --generate-only --dataset-output PATH\nV0 uses --threads as its bounded batch concurrency limit.\n"
        "OpenMP is optional at build time and supports batch mode only.\n";
}
}
int benchmark_main(int argc, char** argv) {
    try {
        std::map<std::string,std::string> args;
        bool append = false, generate_only = false;
        const std::vector<std::string> known{"scheduler","threads","tasks","workload","seed","mode","repeats","warmups","deadline-us","lidar-points","fft-size","noise","rates","output","preview","dataset-output"};
        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            if (key == "--help" || key == "-h") { help(); return 0; }
            if (key == "--list-schedulers") { std::cout << "serial"; for (const auto& s : available_schedulers()) std::cout << ' ' << s; std::cout << '\n'; return 0; }
            if (key == "--append") { append = true; continue; }
            if (key == "--generate-only") { generate_only = true; continue; }
            if (!key.starts_with("--") || std::find(known.begin(),known.end(),key.substr(2)) == known.end() || i+1 == argc) throw std::invalid_argument("Unknown or incomplete option: " + key);
            if (!args.emplace(key.substr(2),argv[++i]).second) throw std::invalid_argument("Repeated option: " + key);
        }
        auto get = [&](const std::string& k, const std::string& fallback) { auto it = args.find(k); return it == args.end() ? fallback : it->second; };
        SimulatorConfig config;
        config.tasks = number(get("tasks","1000")); config.lidar_points = number(get("lidar-points","12000"));
        config.fft_size = number(get("fft-size","256"));
        const auto seed = number(get("seed","42"));
        if (seed > UINT32_MAX) throw std::invalid_argument("Seed exceeds uint32 range");
        config.seed = static_cast<std::uint32_t>(seed); config.workload = get("workload","mixed");
        auto real = [](const std::string& value) { std::size_t used{}; double n = std::stod(value,&used); if (used != value.size() || !std::isfinite(n)) throw std::invalid_argument("Invalid number: " + value); return n; };
        config.noise = real(get("noise","0.5"));
        if (args.contains("rates")) {
            std::string values = args.at("rates"); std::size_t offset = 0;
            for (std::size_t i = 0; i < 5; ++i) {
                auto end = values.find(',',offset);
                if ((i < 4 && end == std::string::npos) || (i == 4 && end != std::string::npos)) throw std::invalid_argument("Provide exactly five rates");
                config.rates[i] = real(values.substr(offset,end == std::string::npos ? end : end-offset)); offset = end+1;
            }
        }
        const auto threads = number(get("threads",std::to_string(std::min(4u,std::max(1u,std::thread::hardware_concurrency())))));
        const auto repeats = number(get("repeats","3")), warmups = number(get("warmups","1"));
        if (!config.tasks || !threads || threads > 1024 || !repeats) throw std::invalid_argument("Tasks/repeats must be positive; threads must be 1..1024");
        const auto mode = get("mode","batch");
        if (mode != "batch" && mode != "streaming") throw std::invalid_argument("Mode must be batch or streaming");
        const bool streaming = mode == "streaming";
        const double deadline = real(get("deadline-us","50000"));
        if (deadline <= 0) throw std::invalid_argument("Deadline must be positive");
        auto scheduler = get("scheduler","all");
        auto schedulers = scheduler == "all" ? available_schedulers() : std::vector<std::string>{scheduler};
        if (scheduler == "all" && streaming) std::erase(schedulers,"openmp");
        for (const auto& s : schedulers) {
            if (generate_only) break;
            if (s != "serial") { auto probe = make_executor(s,threads); }
            if (s == "openmp" && streaming) throw std::invalid_argument("OpenMP is batch-only");
        }
        auto data = generate(config);
        // Concentrate expensive tasks at indices mapped to worker zero where possible.
        // Identical reordered inputs are passed to every scheduler and the serial reference.
        if (config.workload == "imbalanced") {
            std::stable_sort(data.begin(),data.end(),[](const auto& a,const auto& b) { return a.type == SensorType::Lidar && b.type != SensorType::Lidar; });
            // Use heavy/light groups with a repeating worker-aligned pattern.
            const auto heavy_count = static_cast<std::size_t>(std::count_if(data.begin(),data.end(),[](const auto& r) { return r.type == SensorType::Lidar; }));
            std::vector<SensorReading> arranged(data.size()); std::size_t heavy = 0, light = heavy_count;
            for (std::size_t i = 0; i < data.size(); ++i) {
                const bool take_heavy = i % threads == 0 || light == data.size();
                const auto index = take_heavy && heavy < heavy_count ? heavy++ : light++;
                arranged[i] = std::move(data[index]); arranged[i].id = i; arranged[i].timestamp_us = i*1000;
            }
            data = std::move(arranged);
        }
        if (generate_only || args.contains("dataset-output")) {
            const auto dataset_path = get("dataset-output","data/generated/sensors.csv");
            write_dataset(dataset_path,data);
            std::cout << "Generated " << data.size() << " readings: " << dataset_path << '\n';
            if (generate_only) return 0;
        }
        std::vector<ProcessedReading> reference; reference.reserve(data.size());
        for (const auto& r : data) reference.push_back(process(r));
        auto preview = number(get("preview","0"));
        for (std::size_t i = 0; i < std::min(preview,data.size()); ++i) std::cout << data[i].id << " | " << sensor_name(data[i].type) << " | value=" << reference[i].value << " retained=" << reference[i].retained_points << " anomaly=" << reference[i].anomaly << '\n';
        const std::filesystem::path output = get("output","results/csv/benchmark_results.csv");
        if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
        const std::string header = "scheduler,threads,tasks,workload,mode,seed,lidar_points,fft_size,run,elapsed_s,throughput,queue_mean_us,execution_mean_us,response_mean_us,p95_us,p99_us,arrival_p99_us,deadline_us,deadline_misses,serial_s,speedup,efficiency,successful_steals,failed_steals,completed,missing,duplicates,mismatches,noise,speed_hz,wheel_hz,temperature_hz,can_hz,lidar_hz,cpu_user_s,cpu_kernel_s,cpu_percent,machine_cpu_percent,voluntary_context_switches,involuntary_context_switches,cpu_available,context_switches_available";
        const bool existing = append && std::filesystem::exists(output) && std::filesystem::file_size(output) > 0;
        if (existing) { std::ifstream old(output); std::string line; std::getline(old,line); if (line != header) throw std::runtime_error("Existing CSV has incompatible schema"); }
        std::ofstream csv(output,append ? std::ios::app : std::ios::trunc); csv.exceptions(std::ios::failbit|std::ios::badbit);
        if (!existing) csv << header << '\n';
        csv << std::setprecision(12);
        const auto clouds = export_clouds(output.parent_path()/"clouds",reference);
        for (std::size_t w = 0; w < warmups; ++w) {
            benchmark(data,reference,"serial",1,streaming,deadline);
            for (const auto& s : schedulers) benchmark(data,reference,s,threads,streaming,deadline);
        }
        bool valid = true;
        for (std::size_t run = 0; run < repeats; ++run) {
            auto serial = benchmark(data,reference,"serial",1,streaming,deadline);
            // Rotate order across repeats to reduce fixed-order thermal/cache bias.
            for (std::size_t j = 0; j < schedulers.size(); ++j) {
                const auto& s = schedulers[(j+run)%schedulers.size()];
                const auto n = s == "serial" ? 1 : threads;
                auto r = s == "serial" ? serial : benchmark(data,reference,s,n,streaming,deadline);
                double speedup = serial.elapsed_s/r.elapsed_s;
                csv << s << ',' << n << ',' << data.size() << ',' << config.workload << ',' << mode << ',' << config.seed << ',' << config.lidar_points << ',' << config.fft_size << ',' << run << ',' << r.elapsed_s << ',' << r.throughput << ',' << r.queue_mean_us << ',' << r.execution_mean_us << ',' << r.response_mean_us << ',' << r.p95_us << ',' << r.p99_us << ',' << r.arrival_p99_us << ',' << deadline << ',' << r.deadline_misses << ',' << serial.elapsed_s << ',' << speedup << ',' << speedup/static_cast<double>(n) << ',' << r.executor.successful_steals << ',' << r.executor.failed_steals << ',' << r.completed << ',' << r.missing << ',' << r.duplicates << ',' << r.mismatches << ',' << config.noise;
                for (double rate : config.rates) csv << ',' << rate;
                csv << ',';
                if (r.cpu_available) csv << r.cpu_user_s;
                csv << ','; if (r.cpu_available) csv << r.cpu_kernel_s;
                csv << ','; if (r.cpu_available) csv << r.cpu_percent;
                csv << ','; if (r.cpu_available) csv << r.machine_cpu_percent;
                csv << ','; if (r.switches_available) csv << r.voluntary_switches;
                csv << ','; if (r.switches_available) csv << r.involuntary_switches;
                csv << ',' << r.cpu_available << ',' << r.switches_available;
                csv << '\n';
                const std::string label = output.stem().string()+"_"+s+"_"+config.workload+"_"+mode+"_t"+std::to_string(n)+"_n"+std::to_string(data.size())+"_p"+std::to_string(config.lidar_points)+"_f"+std::to_string(config.fft_size)+"_s"+std::to_string(seed)+"_r"+std::to_string(run);
                write_details(output.parent_path()/"details"/label,data,r,clouds);
                std::cout << s << " run=" << run << " completed=" << r.completed << " elapsed=" << r.elapsed_s << "s throughput=" << r.throughput << "/s p99=" << r.p99_us << "us steals=" << r.executor.successful_steals << " missing=" << r.missing << " duplicates=" << r.duplicates << " mismatches=" << r.mismatches << '\n';
                valid &= r.missing == 0 && r.duplicates == 0 && r.mismatches == 0;
            }
        }
        std::cout << "CSV: " << output.string() << '\n';
        return valid ? 0 : 2;
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
int main(int argc, char** argv) {
    // Respect explicit user settings; otherwise keep idle OpenMP workers from
    // spinning during later serial/custom-pool measurements in this process.
    for (const auto& setting : {std::pair{"OMP_WAIT_POLICY","PASSIVE"}, std::pair{"OMP_DYNAMIC","FALSE"}}) {
        if (std::getenv(setting.first)) continue;
#ifdef _WIN32
        _putenv_s(setting.first,setting.second);
#else
        setenv(setting.first,setting.second,0);
#endif
    }
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--interactive")) return run_interactive(benchmark_main);
    return benchmark_main(argc,argv);
}

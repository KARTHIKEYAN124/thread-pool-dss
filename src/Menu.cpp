#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int run_interactive(int (*entry)(int,char**)) {
    std::string tasks = "1000", threads = std::to_string(std::min(4u,std::max(1u,std::thread::hardware_concurrency())));
    std::string seed = "42", workload = "mixed", mode = "batch";
    const std::string output = "results/menu/csv/benchmark_results.csv";
    auto invoke = [&](std::vector<std::string> args) {
        args.insert(args.begin(),"vehicle_benchmark"); std::vector<char*> pointers;
        for (auto& a : args) pointers.push_back(a.data());
        return entry(static_cast<int>(pointers.size()),pointers.data());
    };
    auto setting = [](const std::string& label, std::string& value) {
        std::cout << label << " [" << value << "]: "; std::string input;
        if (std::getline(std::cin,input) && !input.empty()) value = input;
    };
    for (;;) {
        std::cout << "\nVehicle Sensor Processing System\n"
                  << "1. Generate and export sensor dataset\n2. Run serial reference\n3. Run V0 - thread per task\n"
                  << "4. Run V1 - shared queue\n5. Run V2 - per-worker queues\n6. Run V3 - work stealing\n"
                  << "7. Run V4 - atomic work stealing\n8. Compare all schedulers\n9. Validate all scheduler outputs\n"
                  << "10. Configure workload\n11. Export latest results bundle\n12. Run full experiment matrix\n0. Exit\nChoice: ";
        std::string choice; if (!std::getline(std::cin,choice) || choice == "0") return 0;
        try {
            if (choice == "10") {
                setting("Tasks",tasks); setting("Workers",threads); setting("Seed",seed);
                setting("Workload (light/mixed/heavy/burst/imbalanced)",workload); setting("Mode (batch/streaming)",mode);
                continue;
            }
            if (choice == "11") {
                const auto source = std::filesystem::weakly_canonical(std::filesystem::path(output).parent_path());
                if (!std::filesystem::exists(output)) { std::cout << "Run a comparison first.\n"; continue; }
                std::cout << "New destination directory: "; std::string destination; if (!std::getline(std::cin,destination)) return 0;
                if (destination.empty()) continue;
                const auto target = std::filesystem::weakly_canonical(std::filesystem::absolute(destination));
                auto path_key = [](const std::filesystem::path& p) {
                    auto key = p.generic_string();
#ifdef _WIN32
                    std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
                    return key;
                };
                const auto source_key = path_key(source), target_key = path_key(target);
                if (target_key == source_key || target_key.starts_with(source_key+"/") || std::filesystem::exists(target)) {
                    std::cout << "Choose a new directory outside the current results directory.\n"; continue;
                }
                std::filesystem::create_directories(target.parent_path());
                std::filesystem::copy(source,target,std::filesystem::copy_options::recursive);
                std::cout << "Exported summary, timings and point clouds to " << target.string() << '\n'; continue;
            }
            if (choice == "12") {
                bool append = false, failed = false;
                for (int scenario = 0; scenario < 7 && !failed; ++scenario) {
                    for (unsigned n : {1u,2u,4u,8u}) {
                        if (n > std::max(1u,std::thread::hardware_concurrency())) continue;
                        const std::string kinds[]{"light","mixed","heavy","imbalanced","burst","heavy","heavy"};
                        std::vector<std::string> args{"--scheduler","all","--tasks",scenario == 4 ? "300" : "1200","--threads",std::to_string(n),"--seed",seed,
                            "--workload",kinds[scenario],"--mode",scenario == 4 ? "streaming" : "batch","--repeats","3","--warmups","1",
                            "--lidar-points",scenario == 5 ? "512" : scenario == 6 ? "3000" : "12000","--fft-size",scenario == 5 ? "64" : scenario == 6 ? "1024" : "256","--output",output};
                        if (append) args.push_back("--append");
                        if (invoke(args)) { failed = true; break; } append = true;
                    }
                }
                std::cout << (failed ? "Experiment matrix failed.\n" : "Experiment matrix completed.\n"); continue;
            }
            if (choice == "1") { invoke({"--generate-only","--tasks",tasks,"--threads",threads,"--seed",seed,"--workload",workload}); continue; }
            const std::vector<std::string> choices{"2","3","4","5","6","7","8","9"};
            const auto found = std::find(choices.begin(),choices.end(),choice);
            if (found == choices.end()) { std::cout << "Unknown choice.\n"; continue; }
            const std::vector<std::string> schedulers{"serial","v0","v1","v2","v3","v4","all","all"};
            const bool validate = choice == "9";
            std::vector<std::string> args{"--scheduler",schedulers[static_cast<std::size_t>(found-choices.begin())],"--threads",threads,
                "--tasks",validate ? "160" : tasks,"--seed",seed,"--workload",validate ? "heavy" : workload,
                "--mode",validate ? "batch" : mode,"--output",output,"--repeats",validate ? "1" : "3","--warmups",validate ? "0" : "1"};
            if (validate) { args.push_back("--lidar-points"); args.push_back("128"); }
            const auto status = invoke(args);
            std::cout << (status == 0 ? "Completed successfully.\n" : "Operation failed; see the error above.\n");
        } catch (const std::exception& error) { std::cerr << "Menu operation failed: " << error.what() << '\n'; }
    }
}

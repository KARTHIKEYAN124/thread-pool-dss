#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace vehicle {
using Task = std::function<void(std::size_t)>;
struct WorkerStats { std::uint64_t tasks{}; double busy_us{}, idle_us{}; };
struct ExecutorStats {
    std::vector<WorkerStats> workers;
    std::uint64_t successful_steals{}, failed_steals{};
};
class IExecutor {
public:
    virtual ~IExecutor() = default;
    virtual void submit(Task task) = 0;
    virtual void wait() = 0;
    virtual void shutdown() = 0;
    virtual ExecutorStats stats() const = 0;
};
// V0 uses threads as its maximum simultaneous thread count (bounded batches).
std::unique_ptr<IExecutor> make_executor(const std::string& scheduler, std::size_t threads);
std::vector<std::string> available_schedulers();
}

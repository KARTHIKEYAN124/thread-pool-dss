#include "threadpool/IExecutor.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#ifdef VEHICLE_OPENMP
#include <omp.h>
#endif
namespace vehicle {
std::unique_ptr<IExecutor> make_lockfree_executor(std::size_t n);
namespace {
using Clock = std::chrono::steady_clock;
double micros(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
struct Queue { std::mutex mutex; std::deque<Task> tasks; };
class Pool final : public IExecutor {
    int version_;
    std::vector<std::unique_ptr<Queue>> queues_;
    std::vector<std::thread> workers_;
    mutable std::mutex gate_;
    std::mutex shutdown_gate_;
    std::condition_variable ready_, done_;
    bool accepting_{true}, stopping_{};
    std::size_t outstanding_{}, round_robin_{};
    std::atomic<std::size_t> queued_{};
    std::atomic<std::uint64_t> steals_{}, failures_{};
    ExecutorStats stats_;
    std::exception_ptr error_;
    bool pop(std::size_t index, bool steal, Task& task) {
        auto& q = *queues_[index];
        std::unique_lock lock(q.mutex, std::defer_lock);
        if (steal) { if (!lock.try_lock()) return false; } else lock.lock();
        if (q.tasks.empty()) return false;
        if (version_ == 3 && !steal) { task = std::move(q.tasks.back()); q.tasks.pop_back(); }
        else { task = std::move(q.tasks.front()); q.tasks.pop_front(); }
        --queued_;
        return true;
    }
    bool own_pending(std::size_t id) {
        auto& q = *queues_[version_ == 1 ? 0 : id];
        std::lock_guard lock(q.mutex);
        return !q.tasks.empty();
    }
    void run(std::size_t id) {
        auto idle_since = Clock::now();
        for (;;) {
            Task task;
            bool found = pop(version_ == 1 ? 0 : id, false, task);
            if (!found && version_ == 3) {
                // Rotate victim order to avoid concentrating contention on worker zero.
                for (std::size_t offset = 1; offset < queues_.size(); ++offset) {
                    if (pop((id + offset) % queues_.size(), true, task)) { ++steals_; found = true; break; }
                    ++failures_;
                }
            }
            if (found) {
                auto start = Clock::now();
                std::exception_ptr error;
                try { task(id); } catch (...) { error = std::current_exception(); }
                auto finish = Clock::now();
                {
                    std::lock_guard lock(gate_);
                    auto& s = stats_.workers[id];
                    ++s.tasks; s.busy_us += micros(finish-start); s.idle_us += micros(start-idle_since);
                    if (error && !error_) error_ = error;
                    --outstanding_;
                    if (!outstanding_) done_.notify_all();
                }
                idle_since = finish;
                continue;
            }
            std::unique_lock lock(gate_);
            ready_.wait(lock, [&] { return stopping_ || (version_ == 3 ? queued_.load() > 0 : own_pending(id)); });
            if (stopping_) {
                stats_.workers[id].idle_us += micros(Clock::now()-idle_since);
                return;
            }
        }
    }
public:
    Pool(int version, std::size_t n) : version_(version) {
        stats_.workers.resize(n);
        for (std::size_t i = 0; i < (version == 1 ? 1 : n); ++i) queues_.push_back(std::make_unique<Queue>());
        try { for (std::size_t i = 0; i < n; ++i) workers_.emplace_back([this,i] { run(i); }); }
        catch (...) {
            { std::lock_guard lock(gate_); stopping_ = true; }
            ready_.notify_all();
            for (auto& t : workers_) t.join();
            throw;
        }
    }
    ~Pool() override { shutdown(); }
    void submit(Task task) override {
        if (!task) throw std::invalid_argument("Empty task");
        {
            std::lock_guard lock(gate_);
            if (!accepting_) throw std::runtime_error("Executor closed");
            auto& q = *queues_[version_ == 1 ? 0 : round_robin_ % queues_.size()];
            std::lock_guard qlock(q.mutex);
            q.tasks.push_back(std::move(task));
            ++round_robin_; ++outstanding_; ++queued_;
        }
        ready_.notify_all();
    }
    void wait() override {
        std::exception_ptr error;
        { std::unique_lock lock(gate_); done_.wait(lock, [&] { return outstanding_ == 0; }); error = std::exchange(error_, {}); }
        if (error) std::rethrow_exception(error);
    }
    void shutdown() override {
        std::lock_guard lifecycle(shutdown_gate_);
        {
            std::unique_lock lock(gate_);
            accepting_ = false;
            done_.wait(lock, [&] { return outstanding_ == 0; });
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }
    ExecutorStats stats() const override {
        std::lock_guard lock(gate_);
        auto s = stats_; s.successful_steals = steals_; s.failed_steals = failures_; return s;
    }
};
class ThreadPerTask final : public IExecutor {
    const std::size_t limit_;
    mutable std::mutex submit_gate_, gate_;
    std::vector<std::thread> threads_;
    ExecutorStats stats_;
    bool accepting_{true};
    std::exception_ptr error_;
    void join() { for (auto& t : threads_) t.join(); threads_.clear(); }
public:
    explicit ThreadPerTask(std::size_t n) : limit_(n) { stats_.workers.resize(n); threads_.reserve(n); }
    ~ThreadPerTask() override { shutdown(); }
    void submit(Task task) override {
        if (!task) throw std::invalid_argument("Empty task");
        std::lock_guard submit_lock(submit_gate_);
        if (!accepting_) throw std::runtime_error("Executor closed");
        if (threads_.size() == limit_) join();
        auto id = threads_.size();
        threads_.emplace_back([this, task = std::move(task), id] {
            auto start = Clock::now(); std::exception_ptr error;
            try { task(id); } catch (...) { error = std::current_exception(); }
            auto finish = Clock::now();
            std::lock_guard lock(gate_);
            ++stats_.workers[id].tasks; stats_.workers[id].busy_us += micros(finish-start);
            if (error && !error_) error_ = error;
        });
    }
    void wait() override {
        std::lock_guard submit_lock(submit_gate_); join();
        std::exception_ptr error;
        { std::lock_guard lock(gate_); error = std::exchange(error_, {}); }
        if (error) std::rethrow_exception(error);
    }
    void shutdown() override { std::lock_guard lock(submit_gate_); accepting_ = false; join(); }
    ExecutorStats stats() const override { std::lock_guard lock(gate_); return stats_; }
};
#ifdef VEHICLE_OPENMP
class OpenMPExecutor final : public IExecutor {
    std::size_t n_;
    mutable std::mutex gate_;
    std::mutex lifecycle_;
    std::vector<Task> pending_;
    ExecutorStats stats_;
    bool accepting_{true};
    std::exception_ptr error_;
    void execute() {
        std::vector<Task> tasks;
        { std::lock_guard lock(gate_); tasks.swap(pending_); }
        const auto count = static_cast<std::int64_t>(tasks.size());
        // OpenMP dynamic scheduling provides a professional runtime baseline.
        #pragma omp parallel for schedule(dynamic, 1) num_threads(static_cast<int>(n_))
        for (std::int64_t i = 0; i < count; ++i) {
            auto id = static_cast<std::size_t>(omp_get_thread_num());
            auto start = Clock::now(); std::exception_ptr error;
            try { tasks[static_cast<std::size_t>(i)](id); } catch (...) { error = std::current_exception(); }
            auto finish = Clock::now();
            std::lock_guard lock(gate_);
            ++stats_.workers[id].tasks; stats_.workers[id].busy_us += micros(finish-start);
            if (error && !error_) error_ = error;
        }
    }
public:
    explicit OpenMPExecutor(std::size_t n) : n_(n) { stats_.workers.resize(n); }
    ~OpenMPExecutor() override { shutdown(); }
    void submit(Task task) override {
        if (!task) throw std::invalid_argument("Empty task");
        std::lock_guard lock(gate_);
        if (!accepting_) throw std::runtime_error("Executor closed");
        pending_.push_back(std::move(task));
    }
    void wait() override {
        std::lock_guard lifecycle(lifecycle_); execute();
        std::exception_ptr error;
        { std::lock_guard lock(gate_); error = std::exchange(error_, {}); }
        if (error) std::rethrow_exception(error);
    }
    void shutdown() override {
        std::lock_guard lifecycle(lifecycle_);
        { std::lock_guard lock(gate_); accepting_ = false; }
        execute();
    }
    ExecutorStats stats() const override { std::lock_guard lock(gate_); return stats_; }
};
#endif
}
std::vector<std::string> available_schedulers() {
    std::vector<std::string> names{"v0", "v1", "v2", "v3", "v4"};
#ifdef VEHICLE_OPENMP
    names.push_back("openmp");
#endif
    return names;
}
std::unique_ptr<IExecutor> make_executor(const std::string& name, std::size_t n) {
    if (!n || n > 1024) throw std::invalid_argument("Threads must be in [1,1024]");
    if (name == "v0") return std::make_unique<ThreadPerTask>(n);
    if (name == "v4") return make_lockfree_executor(n);
    if (name == "v1" || name == "v2" || name == "v3") return std::make_unique<Pool>(name[1]-'0', n);
#ifdef VEHICLE_OPENMP
    if (name == "openmp") return std::make_unique<OpenMPExecutor>(n);
#endif
    throw std::invalid_argument("Unknown or unavailable scheduler: " + name);
}
}

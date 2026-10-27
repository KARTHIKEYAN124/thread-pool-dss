#include "threadpool/IExecutor.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace vehicle {
namespace {
// Intrusive Treiber stacks. Nodes are never reused or freed until every worker
// has joined. A failed CAS may safely inspect next, and removed nodes cannot
// cause reclamation ABA. Only the winning consumer accesses the Task payload.
// Queue operations are lock-free; lifecycle/accounting and allocation are not.
class LockFreePool final : public IExecutor {
    struct Node { Task task; Node* next{}; explicit Node(Task t) : task(std::move(t)) {} };
    static_assert(std::atomic<Node*>::is_always_lock_free, "V4 requires lock-free pointer atomics");
    using Clock = std::chrono::steady_clock;
    const std::size_t count_;
    std::unique_ptr<std::atomic<Node*>[]> heads_;
    std::vector<std::unique_ptr<Node>> retained_;
    std::vector<std::thread> workers_;
    mutable std::mutex gate_;
    std::mutex lifecycle_;
    std::condition_variable ready_, done_;
    bool accepting_{true}, stopping_{};
    std::size_t outstanding_{}, round_robin_{};
    std::atomic<std::size_t> queued_{};
    std::atomic<std::uint64_t> steals_{}, failures_{};
    ExecutorStats stats_;
    std::exception_ptr error_;
    static double us(Clock::duration d) { return std::chrono::duration<double,std::micro>(d).count(); }
    bool pop(std::size_t index, Task& task) {
        auto& head = heads_[index];
        Node* node = head.load(std::memory_order_acquire);
        while (node) {
            Node* next = node->next;
            if (head.compare_exchange_weak(node,next,std::memory_order_acq_rel,std::memory_order_acquire)) {
                task = std::move(node->task);
                queued_.fetch_sub(1,std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }
    void worker(std::size_t id) {
        auto idle_since = Clock::now();
        for (;;) {
            Task task;
            bool found = pop(id,task);
            if (!found) {
                for (std::size_t offset = 1; offset < count_; ++offset) {
                    if (pop((id+offset)%count_,task)) { ++steals_; found = true; break; }
                    ++failures_;
                }
            }
            if (found) {
                const auto start = Clock::now(); std::exception_ptr error;
                try { task(id); } catch (...) { error = std::current_exception(); }
                const auto finish = Clock::now();
                {
                    std::lock_guard lock(gate_);
                    auto& s = stats_.workers[id]; ++s.tasks;
                    s.busy_us += us(finish-start); s.idle_us += us(start-idle_since);
                    if (error && !error_) error_ = error;
                    if (--outstanding_ == 0) done_.notify_all();
                }
                idle_since = finish;
            } else {
                std::unique_lock lock(gate_);
                ready_.wait(lock,[&] { return stopping_ || queued_.load(std::memory_order_relaxed) != 0; });
                if (stopping_) { stats_.workers[id].idle_us += us(Clock::now()-idle_since); return; }
            }
        }
    }
public:
    explicit LockFreePool(std::size_t n) : count_(n), heads_(std::make_unique<std::atomic<Node*>[]>(n)) {
        for (std::size_t i = 0; i < n; ++i) heads_[i].store(nullptr);
        stats_.workers.resize(n);
        try { for (std::size_t i = 0; i < n; ++i) workers_.emplace_back([this,i] { worker(i); }); }
        catch (...) {
            { std::lock_guard lock(gate_); stopping_ = true; }
            ready_.notify_all(); for (auto& t : workers_) t.join(); throw;
        }
    }
    ~LockFreePool() override { shutdown(); }
    void submit(Task task) override {
        if (!task) throw std::invalid_argument("Empty task");
        auto node = std::make_unique<Node>(std::move(task));
        {
            std::lock_guard lock(gate_);
            if (!accepting_) throw std::runtime_error("Executor closed");
            Node* raw = node.get(); retained_.push_back(std::move(node));
            auto& head = heads_[round_robin_++%count_];
            ++outstanding_; ++queued_; // publish count before node can be popped
            Node* expected = head.load(std::memory_order_acquire);
            do { raw->next = expected; }
            while (!head.compare_exchange_weak(expected,raw,std::memory_order_acq_rel,std::memory_order_acquire));
        }
        ready_.notify_all();
    }
    void wait() override {
        std::exception_ptr error;
        { std::unique_lock lock(gate_); done_.wait(lock,[&] { return outstanding_ == 0; }); error = std::exchange(error_,{}); }
        if (error) std::rethrow_exception(error);
    }
    void shutdown() override {
        std::lock_guard lifecycle(lifecycle_);
        {
            std::unique_lock lock(gate_); accepting_ = false;
            done_.wait(lock,[&] { return outstanding_ == 0; }); stopping_ = true;
        }
        ready_.notify_all(); for (auto& t : workers_) if (t.joinable()) t.join();
        // All readers have exited. Reclaim nodes inside the timed shutdown,
        // rather than deferring their allocation cost to executor destruction.
        retained_.clear();
    }
    ExecutorStats stats() const override {
        std::lock_guard lock(gate_); auto s = stats_;
        s.successful_steals = steals_; s.failed_steals = failures_; return s;
    }
};
}
std::unique_ptr<IExecutor> make_lockfree_executor(std::size_t n) { return std::make_unique<LockFreePool>(n); }
}

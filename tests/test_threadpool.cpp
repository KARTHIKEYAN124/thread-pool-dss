#include "benchmark/Metrics.hpp"
#include "sensor/SensorProcessor.hpp"
#include "sensor/SensorSimulator.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <thread>
using namespace vehicle;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void algorithms() {
    SensorReading r; r.type = SensorType::Speed; r.samples = {60,62,64,66,68};
    check(process(r).value == 64,"Moving average incorrect");
    r.type = SensorType::Temperature; r.samples = {120}; check(process(r).anomaly,"Temperature threshold incorrect");
    r.type = SensorType::Can; r.payload = {1,2,3,4,5,6,7,0};
    check(!process(r).anomaly,"Valid CAN frame rejected"); r.payload[7] = 1; check(process(r).anomaly,"CAN checksum not detected");
    r.type = SensorType::Wheel; r.samples.clear();
    for (int i = 0; i < 256; ++i) r.samples.push_back(std::sin(2*std::numbers::pi*28*i/256));
    auto fft = process(r); check(fft.value == 28 && std::abs(fft.magnitude-1) < 1e-10,"FFT known signal failed");
    r.type = SensorType::Lidar; r.points = {{0,0,0,1},{0.2,0,0,1},{1,0,0,1}};
    auto lidar = process(r); check(lidar.retained_points == 2 && std::abs(lidar.value-.55) < 1e-10,"Voxel centroid statistics incorrect");
    check(lidar.points.size() == 2 && lidar.points[0] == Point3D{.1,0,0,1} && lidar.points[1] == Point3D{1,0,0,1},"Complete voxel centroids/intensities incorrect");
    r.points = {{-.1,0,0,2},{.1,0,0,4},{.2,0,0,6}};
    const auto signed_voxels = process(r);
    check(signed_voxels.points.size() == 2 && signed_voxels.points[0].x == -.1 && signed_voxels.points[1].intensity == 5,"Signed voxel boundaries/intensity averaging failed");
    check(percentile({1,2,3,4,5},.95) == 5,"Percentile failed");
    SimulatorConfig config; config.tasks = 1300; config.lidar_points = 64;
    auto a = generate(config), b = generate(config); std::array<int,5> counts{};
    for (std::size_t i = 0; i < a.size(); ++i) {
        ++counts[static_cast<int>(a[i].type)];
        check(a[i].id == i && (i == 0 || a[i].timestamp_us >= a[i-1].timestamp_us),"Invalid ID or arrival order");
        check(a[i].timestamp_us == b[i].timestamp_us && process(a[i]) == process(b[i]),"Seed not reproducible");
    }
    for (int count : counts) check(count > 0,"Missing sensor type");
    check(counts[3] > counts[0]*8 && counts[0] > counts[4]*8,"Rate mix incorrect");
    std::cout << "[PASS] Known sensor algorithms, deterministic simulation, rate mix and percentiles\n";
}
void pool_tests(const std::string& name) {
    constexpr std::size_t count = 600;
    auto pool = make_executor(name,4);
    auto seen = std::make_unique<std::atomic<int>[]>(count);
    for (std::size_t i = 0; i < count; ++i) seen[i] = 0;
    std::vector<std::thread> producers;
    for (std::size_t p = 0; p < 4; ++p) producers.emplace_back([&,p] {
        for (std::size_t i = p; i < count; i += 4) pool->submit([&,i](std::size_t) { ++seen[i]; });
    });
    for (auto& p : producers) p.join();
    pool->wait();
    for (std::size_t i = 0; i < count; ++i) check(seen[i] == 1,"Lost or duplicate task during concurrent submission");
    pool->submit([](std::size_t) { throw std::runtime_error("expected task failure"); });
    bool reported = false;
    try { pool->wait(); } catch (const std::runtime_error&) { reported = true; }
    check(reported,"Task exception not reported");
    std::atomic<int> done{};
    pool->submit([&](std::size_t) { ++done; }); pool->wait(); check(done == 1,"Reuse after exception failed");
    pool->shutdown(); pool->shutdown();
    bool rejected = false;
    try { pool->submit([](std::size_t) {}); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected,"Submission after close accepted");
    std::atomic<int> drained{};
    { auto draining = make_executor(name,2); for (int i = 0; i < 75; ++i) draining->submit([&](std::size_t) { ++drained; }); }
    check(drained == 75,"Destructor did not drain accepted tasks");
    for (int cycle = 0; cycle < 12; ++cycle) {
        auto racing = make_executor(name,2); std::atomic<int> accepted{}, executed{};
        std::thread producer([&] {
            for (int i = 0; i < 100; ++i) {
                try { racing->submit([&](std::size_t) { ++executed; }); ++accepted; }
                catch (const std::runtime_error&) { break; }
            }
        });
        std::thread closer([&] { racing->shutdown(); });
        producer.join(); closer.join(); racing->shutdown();
        check(accepted == executed,"Submit/shutdown race lost accepted task");
    }
    SimulatorConfig c; c.tasks = 160; c.workload = "heavy"; c.lidar_points = 128;
    auto data = generate(c); std::vector<ProcessedReading> ref;
    for (const auto& r : data) ref.push_back(process(r));
    auto result = benchmark(data,ref,name,4,false,50000);
    check(result.completed == data.size() && !result.missing && !result.duplicates && !result.mismatches,"Parallel reference mismatch");
    std::uint64_t worker_total = 0;
    for (const auto& worker : result.executor.workers) worker_total += worker.tasks;
    check(worker_total == data.size(),"Worker accounting incorrect");
    for (const auto& t : result.tasks) check(t.arrival_us <= t.submitted_us && t.submitted_us <= t.start_us && t.start_us <= t.finish_us,"Timing order violated");
    std::cout << "[PASS] " << name << ": exactly once, producers, exceptions, drain, shutdown race, reference, timing\n";
}
void stealing_test(const std::string& scheduler) {
    auto pool = make_executor(scheduler,2);
    std::promise<void> started, release;
    auto released = release.get_future().share();
    pool->submit([&](std::size_t) { started.set_value(); released.wait(); });
    started.get_future().wait();
    std::atomic<int> finished{};
    // Round robin sends half this backlog to the occupied worker's queue.
    for (int i = 0; i < 100; ++i) pool->submit([&](std::size_t) { ++finished; });
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (finished < 100 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    const bool stole_backlog = finished == 100;
    release.set_value(); pool->wait();
    check(stole_backlog && pool->stats().successful_steals > 0,"Idle worker did not steal blocked worker backlog");
    std::cout << "[PASS] " << scheduler << " steals queued work while its owner is blocked\n";
}
void lockfree_contention() {
    constexpr std::size_t count = 24000;
    auto pool = make_executor("v4",4);
    auto counts = std::make_unique<std::atomic<unsigned>[]>(count);
    for (std::size_t i = 0; i < count; ++i) counts[i] = 0;
    std::vector<std::thread> producers;
    for (std::size_t producer = 0; producer < 6; ++producer) producers.emplace_back([&,producer] {
        for (std::size_t i = producer; i < count; i += 6) pool->submit([&,i](std::size_t) { ++counts[i]; });
    });
    for (auto& producer : producers) producer.join(); pool->wait();
    for (std::size_t i = 0; i < count; ++i) check(counts[i] == 1,"V4 CAS contention lost or duplicated a node");
    std::cout << "[PASS] V4 exactly-once CAS contention: 24,000 tasks from six producers\n";
}
int main() {
    try {
        algorithms(); for (const auto& name : available_schedulers()) pool_tests(name);
        stealing_test("v3"); stealing_test("v4"); lockfree_contention();
        bool rejected = false; try { auto p = make_executor("v3",0); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Zero workers accepted");
        std::cout << "All correctness and stress tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "[FAIL] " << e.what() << '\n'; return 1; }
}

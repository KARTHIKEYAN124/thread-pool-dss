#include "sensor/SensorSimulator.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
namespace vehicle {
std::vector<SensorReading> generate(const SimulatorConfig& c) {
    if (c.fft_size < 2 || (c.fft_size & (c.fft_size - 1))) throw std::invalid_argument("FFT size must be a power of two >= 2");
    if (!c.lidar_points || !std::isfinite(c.noise) || c.noise < 0) throw std::invalid_argument("Invalid point count or noise");
    if (c.workload != "mixed" && c.workload != "light" && c.workload != "heavy" && c.workload != "burst" && c.workload != "imbalanced") throw std::invalid_argument("Unknown workload");
    for (double rate : c.rates) if (!std::isfinite(rate) || rate <= 0) throw std::invalid_argument("Sensor rates must be finite and positive");
    std::mt19937 rng(c.seed);
    std::normal_distribution<double> noise(0, c.noise);
    std::uniform_real_distribution<double> xy(-20, 20), z(-2, 3);
    std::array<double, 5> next{};
    std::vector<SensorReading> out;
    out.reserve(c.tasks);
    for (std::size_t i = 0; i < c.tasks; ++i) {
        int kind = 0;
        double arrival = 0;
        if (c.workload == "mixed" || c.workload == "burst") {
            kind = static_cast<int>(std::min_element(next.begin(), next.end()) - next.begin());
            arrival = next[kind];
            next[kind] += 1000000.0 / c.rates[kind];
        } else {
            kind = c.workload == "light" ? (i % 2 == 0 ? 0 : 2) : (c.workload == "imbalanced" ? (i % 8 == 0 ? 4 : 0) : (i % 2 == 0 ? 4 : 1));
            arrival = static_cast<double>(i) * 1000;
        }
        SensorReading r;
        r.id = i;
        r.timestamp_us = static_cast<std::uint64_t>(arrival);
        if (c.workload == "burst") r.timestamp_us = (r.timestamp_us / 100000) * 100000;
        r.type = static_cast<SensorType>(kind);
        switch (r.type) {
        case SensorType::Speed:
            for (int j = 0; j < 5; ++j) r.samples.push_back(60 + 8 * std::sin((static_cast<double>(i) + j) * 0.03) + noise(rng));
            break;
        case SensorType::Wheel:
            for (std::size_t j = 0; j < c.fft_size; ++j) r.samples.push_back(std::sin(2 * std::numbers::pi * 28 * static_cast<double>(j) / r.sample_rate_hz) + noise(rng) * 0.02);
            break;
        case SensorType::Temperature: r.samples = {85 + noise(rng) + (i % 97 == 0 ? 35 : 0)}; break;
        case SensorType::Can:
            for (std::size_t j = 0; j < 7; ++j) r.payload[j] = static_cast<std::uint8_t>(rng() % 256);
            for (std::size_t j = 0; j < 7; ++j) r.payload[7] ^= r.payload[j];
            if (i % 101 == 0) r.payload[7] ^= 1;
            break;
        case SensorType::Lidar:
            r.points.reserve(c.lidar_points);
            for (std::size_t j = 0; j < c.lidar_points; ++j) r.points.push_back({xy(rng), xy(rng), z(rng), 1});
            break;
        }
        out.push_back(std::move(r));
    }
    return out;
}
}

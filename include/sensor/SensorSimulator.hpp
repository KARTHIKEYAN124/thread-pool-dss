#pragma once
#include "sensor/SensorReading.hpp"
#include <string>
#include <filesystem>
namespace vehicle {
struct SimulatorConfig {
    std::size_t tasks{1000}, lidar_points{12000}, fft_size{256};
    std::uint32_t seed{42};
    std::string workload{"mixed"};
    double noise{0.5};
    // speed, wheel, temperature, CAN, LiDAR; dataset arrivals use these rates.
    std::array<double, 5> rates{100, 100, 5, 1000, 10};
};
std::vector<SensorReading> generate(const SimulatorConfig& config);
void write_dataset(const std::filesystem::path& path, const std::vector<SensorReading>& readings);
}

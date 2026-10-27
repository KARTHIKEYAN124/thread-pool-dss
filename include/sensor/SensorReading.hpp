#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace vehicle {
enum class SensorType { Speed, Wheel, Temperature, Can, Lidar };
inline const char* sensor_name(SensorType type) {
    constexpr const char* names[]{"speed", "wheel", "temperature", "can", "lidar"};
    return names[static_cast<int>(type)];
}
struct Point3D {
    double x{}, y{}, z{}, intensity{};
    bool operator==(const Point3D&) const = default;
};
struct SensorReading {
    std::uint64_t id{}, timestamp_us{};
    SensorType type{};
    std::vector<double> samples;
    double sample_rate_hz{256};
    std::uint32_t can_id{0x120};
    std::array<std::uint8_t, 8> payload{};
    std::vector<Point3D> points;
};
struct ProcessedReading {
    double value{}, magnitude{};
    std::size_t retained_points{};
    bool anomaly{};
    std::vector<Point3D> points;
    bool operator==(const ProcessedReading&) const = default;
};
}

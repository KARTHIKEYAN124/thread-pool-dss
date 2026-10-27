#include "sensor/SensorProcessor.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <tuple>
namespace vehicle {
ProcessedReading process(const SensorReading& r) {
    ProcessedReading result;
    switch (r.type) {
    case SensorType::Speed:
        if (r.samples.empty()) throw std::invalid_argument("Empty speed window");
        result.value = std::accumulate(r.samples.begin(), r.samples.end(), 0.0) / static_cast<double>(r.samples.size());
        break;
    case SensorType::Temperature:
        if (r.samples.empty()) throw std::invalid_argument("Missing temperature");
        result.value = r.samples.front(); result.anomaly = result.value > 110 || result.value < -40;
        break;
    case SensorType::Can: {
        std::uint8_t checksum = 0;
        for (std::size_t j = 0; j < 7; ++j) checksum ^= r.payload[j];
        result.anomaly = r.can_id > 0x7ff || checksum != r.payload[7];
        result.value = (r.payload[0] + 256 * r.payload[1]) * 0.01;
        break;
    }
    case SensorType::Wheel: {
        const auto n = r.samples.size();
        if (n < 2 || (n & (n - 1)) || r.sample_rate_hz <= 0) throw std::invalid_argument("Invalid FFT input");
        std::vector<std::complex<double>> a(r.samples.begin(), r.samples.end());
        const double mean = std::accumulate(r.samples.begin(), r.samples.end(), 0.0) / static_cast<double>(n);
        for (auto& v : a) v -= mean;
        for (std::size_t i = 1, j = 0; i < n; ++i) {
            std::size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(a[i], a[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1) {
            const auto root = std::polar(1.0, -2 * std::numbers::pi / static_cast<double>(len));
            for (std::size_t i = 0; i < n; i += len) {
                std::complex<double> w{1, 0};
                for (std::size_t j = 0; j < len / 2; ++j) {
                    auto u = a[i+j], v = a[i+j+len/2] * w;
                    a[i+j] = u+v; a[i+j+len/2] = u-v; w *= root;
                }
            }
        }
        std::size_t peak = 1;
        for (std::size_t j = 2; j <= n / 2; ++j) if (std::abs(a[j]) > std::abs(a[peak])) peak = j;
        result.value = static_cast<double>(peak) * r.sample_rate_hz / static_cast<double>(n);
        result.magnitude = std::abs(a[peak]) * (peak == n / 2 ? 1 : 2) / static_cast<double>(n);
        break;
    }
    case SensorType::Lidar: {
        // Ordered voxels give deterministic centroid statistics regardless of scheduler.
        std::map<std::tuple<int,int,int>, std::pair<Point3D, std::size_t>> voxels;
        for (const auto& p : r.points) {
            auto& [sum, count] = voxels[{static_cast<int>(std::floor(p.x / 0.5)), static_cast<int>(std::floor(p.y / 0.5)), static_cast<int>(std::floor(p.z / 0.5))}];
            sum.x += p.x; sum.y += p.y; sum.z += p.z; sum.intensity += p.intensity; ++count;
        }
        result.retained_points = voxels.size();
        result.points.reserve(voxels.size());
        for (const auto& [key, entry] : voxels) {
            (void)key;
            const auto& [s, count] = entry;
            const double divisor = static_cast<double>(count);
            result.points.push_back({s.x/divisor, s.y/divisor, s.z/divisor, s.intensity/divisor});
            result.value += std::sqrt(s.x*s.x+s.y*s.y+s.z*s.z) / static_cast<double>(count);
        }
        if (!voxels.empty()) result.value /= static_cast<double>(voxels.size());
        break;
    }
    }
    return result;
}
}

#include "sensor/SensorSimulator.hpp"
#include <fstream>
#include <iomanip>
namespace vehicle {
void write_dataset(const std::filesystem::path& path, const std::vector<SensorReading>& readings) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream csv(path); csv.exceptions(std::ios::failbit|std::ios::badbit);
    csv << "id,timestamp_us,sensor,samples,sample_rate_hz,can_id,payload,raw_cloud\n" << std::setprecision(17);
    for (const auto& r : readings) {
        csv << r.id << ',' << r.timestamp_us << ',' << sensor_name(r.type) << ',';
        for (std::size_t i = 0; i < r.samples.size(); ++i) csv << (i ? ";" : "") << r.samples[i];
        csv << ',' << r.sample_rate_hz << ',' << r.can_id << ',';
        for (std::size_t i = 0; i < r.payload.size(); ++i) csv << (i ? ";" : "") << static_cast<unsigned>(r.payload[i]);
        csv << ',';
        if (r.type == SensorType::Lidar) {
            const auto directory = path.parent_path()/(path.stem().string()+"_raw_clouds");
            std::filesystem::create_directories(directory);
            const auto cloud_path = directory/("task_"+std::to_string(r.id)+".ply");
            std::ofstream cloud(cloud_path); cloud.exceptions(std::ios::failbit|std::ios::badbit);
            cloud << "ply\nformat ascii 1.0\ncomment raw sensor points\nelement vertex " << r.points.size()
                  << "\nproperty double x\nproperty double y\nproperty double z\nproperty double intensity\nend_header\n" << std::setprecision(17);
            for (const auto& p : r.points) cloud << p.x << ' ' << p.y << ' ' << p.z << ' ' << p.intensity << '\n';
            csv << cloud_path.lexically_relative(path.parent_path().empty() ? "." : path.parent_path()).generic_string();
        }
        csv << '\n';
    }
}
}

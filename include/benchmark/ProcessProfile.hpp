#pragma once
#include <cstdint>
namespace vehicle {
struct ProcessSnapshot {
    double user_s{}, kernel_s{};
    std::uint64_t voluntary_switches{}, involuntary_switches{};
    bool cpu_available{}, switches_available{};
};
ProcessSnapshot process_snapshot();
}

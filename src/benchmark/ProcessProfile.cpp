#include "benchmark/ProcessProfile.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/resource.h>
#endif
namespace vehicle {
ProcessSnapshot process_snapshot() {
    ProcessSnapshot result;
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user)) {
        auto seconds = [](FILETIME time) { return static_cast<double>((static_cast<std::uint64_t>(time.dwHighDateTime)<<32)|time.dwLowDateTime)/10000000; };
        result.user_s = seconds(user); result.kernel_s = seconds(kernel); result.cpu_available = true;
    }
    // Windows has no supported process-wide getrusage equivalent for switches
    // including terminated V0 threads. Export unavailable, never a false zero.
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF,&usage) == 0) {
        result.user_s = static_cast<double>(usage.ru_utime.tv_sec) + static_cast<double>(usage.ru_utime.tv_usec)/1000000;
        result.kernel_s = static_cast<double>(usage.ru_stime.tv_sec) + static_cast<double>(usage.ru_stime.tv_usec)/1000000;
        result.voluntary_switches = static_cast<std::uint64_t>(usage.ru_nvcsw);
        result.involuntary_switches = static_cast<std::uint64_t>(usage.ru_nivcsw);
        result.cpu_available = true; result.switches_available = true;
    }
#endif
    return result;
}
}

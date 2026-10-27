// An intentional race used only by scripts/run_linux_checks.sh to verify that
// the sanitizer runtime detects a real data race. Never part of the normal suite.
#include <thread>
int shared_value;
int main() {
    std::thread a([] { shared_value = 1; });
    std::thread b([] { shared_value = 2; });
    a.join(); b.join();
    return shared_value == 0;
}

// ============================================================
// server/fuzz/fuzz_db_timing.cpp
//
// Fuzzer for timedCall(). The input drives a sequence of timed calls with
// arbitrary labels against a request breakdown. Each call must append
// exactly one step carrying its label and a non-negative duration, and
// must hand back the wrapped function's result.
// ============================================================

#include "DbTiming.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    VectorPro::Vector<std::pair<std::string, double>> breakdown;
    currentBreakdown = &breakdown;

    std::size_t expected = 0;
    std::size_t pos = 0;
    while (pos < size && expected < 64) {
        const std::size_t length = std::min<std::size_t>(data[pos] % 24, size - pos - 1);
        const std::string label(reinterpret_cast<const char*>(data) + pos + 1, length);
        pos += 1 + length;

        const int result =
            timedCall(label.c_str(), [expected] { return static_cast<int>(expected); });
        ++expected;

        if (result != static_cast<int>(expected - 1) || breakdown.size() != expected ||
            breakdown[expected - 1].second < 0.0)
            __builtin_trap();
    }

    currentBreakdown = nullptr;
    return 0;
}

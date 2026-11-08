// ============================================================
// server/fuzz/fuzz_metrics_json.cpp
//
// Fuzzer for the /api/metrics serialization path. Route patterns, method
// names, step names and timings all end up in JSON that a browser parses,
// so text with quotes, control bytes or invalid UTF-8 must never crash
// the serializer or produce JSON that cannot be read back.
//
// The input is split into one MetricEntry (text fields, status, finite
// timings, breakdown steps). It is pushed through a MetricsRingBuffer, serialized
// with metricEntryToJson(), dumped to text, parsed back, and dumped
// again. The two dumps must match.
// ============================================================

#include "Metrics.h"
#include "MetricsJson.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

// Reads bytes from the fuzz input; returns zeros once it runs out.
class Reader {
  public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::uint8_t byte() {
        return pos_ < size_ ? data_[pos_++] : 0;
    }

    std::string text() {
        // Read the length byte first: it moves pos_, which the bound below uses.
        const std::size_t wanted = byte() % 48;
        const std::size_t length = std::min<std::size_t>(wanted, size_ - pos_);
        std::string out(reinterpret_cast<const char*>(data_) + pos_, length);
        pos_ += length;
        return out;
    }

    double number() {
        double value = 0.0;
        std::uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
            bits = (bits << 8) | byte();
        std::memcpy(&value, &bits, sizeof value);
        // Real timings come from a steady clock, so they are always finite.
        // JsonPro refuses to write NaN/Infinity, so those are not valid input.
        return std::isfinite(value) ? value : 0.0;
    }

  private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    Reader reader(data, size);

    MetricEntry entry;
    entry.route = reader.text();
    entry.method = reader.text();
    entry.status = static_cast<int>(reader.byte()) * 3;
    entry.totalMs = reader.number();
    entry.timestamp = reader.text();

    const int steps = reader.byte() % 4;
    for (int i = 0; i < steps; ++i)
        entry.breakdown.push_back({reader.text(), reader.number()});

    // Capacity comes from the input but is never 0 (the buffer does not allow it).
    MetricsRingBuffer buffer(static_cast<std::size_t>(reader.byte() % 8) + 1);
    buffer.push(entry);
    buffer.push(entry);
    for (const MetricEntry& stored : buffer.entries()) {
        const JsonPro::Json json = metricEntryToJson(stored);
        const std::string first = json.dump();

        try {
            const std::string second = JsonPro::Json::parse(first).dump();
            if (first != second)
                __builtin_trap();
        } catch (const std::exception&) {
            // Valid output must always parse back.
            __builtin_trap();
        }
    }
    return 0;
}

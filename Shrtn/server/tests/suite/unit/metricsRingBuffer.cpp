// Unit MetricsRingBuffer Test Suite
// Verifies the fixed-size buffer behind /api/metrics: it keeps the newest
// entries in order, forgets the oldest ones once full, and stays correct
// when several request threads push at the same time.
//
// Covers:
// - A new buffer is empty
// - Entries come back oldest first, before and after wrapping
// - Once full, the oldest entry is overwritten
// - totalPushed() counts every push, including overwritten ones
// - Concurrent pushes are all counted and never corrupt the buffer

#include <support/framework.h>

#include "Metrics.h"

#include <thread>

namespace {

MetricEntry makeEntry(int status) {
    MetricEntry entry;
    entry.route = "/api/test";
    entry.method = "GET";
    entry.status = status;
    return entry;
}

} // namespace

// Verifies a fresh buffer holds nothing.
static void starts_empty() {
    MetricsRingBuffer buffer(4);
    CHK(buffer.entries().empty());
    CHK(buffer.totalPushed() == 0);
}

// Verifies entries come back oldest first while the buffer is not yet full.
static void keeps_order_before_wrap() {
    MetricsRingBuffer buffer(5);
    buffer.push(makeEntry(1));
    buffer.push(makeEntry(2));
    buffer.push(makeEntry(3));

    const auto entries = buffer.entries();
    CHK(entries.size() == 3);
    CHK(entries[0].status == 1);
    CHK(entries[1].status == 2);
    CHK(entries[2].status == 3);
}

// Verifies the oldest entries are dropped and order is kept after wrapping.
static void overwrites_oldest_after_wrap() {
    MetricsRingBuffer buffer(3);
    for (int status = 1; status <= 7; ++status)
        buffer.push(makeEntry(status));

    const auto entries = buffer.entries();
    CHK(entries.size() == 3);
    CHK(entries[0].status == 5);
    CHK(entries[1].status == 6);
    CHK(entries[2].status == 7);
}

// Verifies the lifetime counter keeps counting past the buffer size.
static void total_counts_past_capacity() {
    MetricsRingBuffer buffer(2);
    for (int i = 0; i < 10; ++i)
        buffer.push(makeEntry(i));

    CHK(buffer.totalPushed() == 10);
    CHK(buffer.entries().size() == 2);
}

// Verifies a capacity of one always holds the latest entry.
static void capacity_of_one() {
    MetricsRingBuffer buffer(1);
    buffer.push(makeEntry(1));
    buffer.push(makeEntry(2));

    const auto entries = buffer.entries();
    CHK(entries.size() == 1);
    CHK(entries[0].status == 2);
}

// Verifies several threads pushing at once are all counted.
static void concurrent_pushes_are_all_counted() {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 500;
    MetricsRingBuffer buffer(50);

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&buffer] {
            for (int i = 0; i < kPerThread; ++i) {
                buffer.push(makeEntry(200));
                (void)buffer.entries();
            }
        });
    }
    for (auto& thread : threads)
        thread.join();

    CHK(buffer.totalPushed() == static_cast<std::uint64_t>(kThreads * kPerThread));
    CHK(buffer.entries().size() == 50);
}

static void run_tests() {
    RUN(starts_empty);
    RUN(keeps_order_before_wrap);
    RUN(overwrites_oldest_after_wrap);
    RUN(total_counts_past_capacity);
    RUN(capacity_of_one);
    RUN(concurrent_pushes_are_all_counted);
}

REGISTER_TEST_SUITE();

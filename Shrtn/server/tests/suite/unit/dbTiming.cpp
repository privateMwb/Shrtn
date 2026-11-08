// Unit DbTiming Test Suite
// Verifies timedCall(), which route handlers use to time a database call
// and attach it to the current request's breakdown.
//
// Covers:
// - timedCall() returns the wrapped function's result unchanged
// - Each call appends one named step, in call order, with a non-negative time
// - Each thread has its own breakdown, so concurrent requests never mix steps

#include <support/framework.h>

#include "DbTiming.h"

#include <thread>

namespace {

using Breakdown = VectorPro::Vector<std::pair<std::string, double>>;

// Points the calling thread's breakdown at `target` for the scope's lifetime.
struct BreakdownScope {
    explicit BreakdownScope(Breakdown& target) {
        currentBreakdown = &target;
    }
    ~BreakdownScope() {
        currentBreakdown = nullptr;
    }
};

} // namespace

// Verifies the wrapped function's return value is passed through.
static void returns_the_wrapped_result() {
    Breakdown breakdown;
    BreakdownScope scope(breakdown);

    const int value = timedCall("answer", [] { return 42; });
    CHK(value == 42);
}

// Verifies each call adds one named, non-negative step in order.
static void records_steps_in_order() {
    Breakdown breakdown;
    BreakdownScope scope(breakdown);

    (void)timedCall("first", [] { return 1; });
    (void)timedCall("second", [] { return 2; });

    CHK(breakdown.size() == 2);
    CHK(breakdown[0].first == "first");
    CHK(breakdown[1].first == "second");
    CHK(breakdown[0].second >= 0.0);
    CHK(breakdown[1].second >= 0.0);
}

// Verifies two threads timing calls at once keep separate breakdowns.
static void threads_do_not_share_a_breakdown() {
    constexpr int kCalls = 200;
    Breakdown first;
    Breakdown second;

    auto work = [](Breakdown& target, const char* label) {
        BreakdownScope scope(target);
        for (int i = 0; i < kCalls; ++i)
            (void)timedCall(label, [] { return 0; });
    };

    std::thread a(work, std::ref(first), "a");
    std::thread b(work, std::ref(second), "b");
    a.join();
    b.join();

    CHK(first.size() == static_cast<std::size_t>(kCalls));
    CHK(second.size() == static_cast<std::size_t>(kCalls));
    for (const auto& step : first)
        CHK(step.first == "a");
    for (const auto& step : second)
        CHK(step.first == "b");
}

static void run_tests() {
    RUN(returns_the_wrapped_result);
    RUN(records_steps_in_order);
    RUN(threads_do_not_share_a_breakdown);
}

REGISTER_TEST_SUITE();

/**
 * @file            Metrics.h
 *
 * @date            2026-11-8
 *
 * @version         1.0.0
 *
 * @copyright       Copyright (c) 2026 privateMwb
 *                  All rights reserved.
 *                  https://github.com/privateMwb/FalconEye
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

#pragma once

// clang-format off
#include <FalconHTTP/HTTP/HttpMethod.h>       // HttpMethod, methodToString
#include <FalconHTTP/HTTP/HttpRequest.h>      // HttpRequest
#include <FalconHTTP/HTTP/HttpResponse.h>     // HttpResponse
#include <FalconHTTP/Middleware/Middleware.h> // MiddlewareFn, NextHandler
#include <FalconHTTP/Routing/PathMatcher.h>   // PathMatcher
#include <FalconHTTP/Routing/Router.h>        // Router

#include <HashMapPro/HashMap.h> // HashMapPro::HashMap (PathMatcher's params out-param)
#include <VectorPro/Vector.h>   // VectorPro::Vector

#include "DbTiming.h" // currentBreakdown, currentRequestBreakdown(), timedCall()

#include <atomic>      // std::atomic
#include <chrono>      // std::chrono::steady_clock, std::chrono::system_clock
#include <cstdint>     // std::uint64_t
#include <ctime>       // std::time_t, std::tm, gmtime_r/gmtime_s
#include <iomanip>     // std::put_time
#include <memory>      // std::shared_ptr, std::make_shared
#include <mutex>       // std::mutex, std::lock_guard
#include <sstream>     // std::ostringstream
#include <string>      // std::string
#include <string_view> // std::string_view
#include <utility>     // std::pair, std::move
// clang-format on

// -------------------------------------------------------------------
// Timestamp helper
// -------------------------------------------------------------------

/**
 * @brief Formats the current UTC time as an ISO 8601 timestamp.
 * @return Timestamp string, e.g. "2026-08-08T10:15:22Z".
 * @details Uses `system_clock` (wall-clock time), not `steady_clock`,
 * which is only meaningful for measuring durations, not dates.
 * `gmtime_r`/`gmtime_s` (thread-safe variants) are used instead of plain
 * `std::gmtime`, which writes into a shared static buffer and would race
 * under FalconEye's multi-threaded request handling.
 */
inline std::string currentTimestampIso8601() {
    auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm utcTm{};
#if defined(_WIN32)
    gmtime_s(&utcTm, &t);
#else
    gmtime_r(&t, &utcTm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%S") << "Z";
    return oss.str();
}

// -------------------------------------------------------------------
// MetricEntry
// -------------------------------------------------------------------

/**
 * @brief One completed request's timing record.
 * @details Mirrors the `/api/metrics` JSON contract's `recentRequests[]`
 * shape field-for-field. `breakdown` is ordered (not a map) because call
 * order is meaningful and should be preserved in the JSON output.
 *
 * `breakdown` only ever contains entries added via `timedCall()` (i.e.
 * "dbQuery"-type entries from route handlers). It cannot contain
 * per-middleware entries for Recovery/Logger/Cors, since those are
 * compiled into the untouched `falconhttp/` submodule and have no hook
 * into `DbTiming.h`'s mechanism.
 */
struct MetricEntry {
    std::string route;     ///< Matched route pattern (e.g. "/api/todos/:id"),
                           ///< or the raw path if nothing matched (404s).
    std::string method;    ///< e.g. "GET", from `FalconHTTP::HTTP::methodToString()`.
    int status = 0;        ///< HTTP status code.
    double totalMs = 0.0;  ///< Total request duration in milliseconds.
    std::string timestamp; ///< ISO 8601 UTC. See `currentTimestampIso8601()`.
    VectorPro::Vector<std::pair<std::string, double>> breakdown; ///< dbQuery-type entries only.

    /// @brief Explicit (even though defaulted) so `MetricEntry` stops being
    /// an aggregate. Without this, `MetricEntry{}` default-initializes
    /// `breakdown` via a path the compiler treats as copy-initialization,
    /// which `VectorPro::Vector`'s explicit default constructor can't
    /// satisfy -- this switches that to direct-initialization instead.
    MetricEntry() = default;
};

// -------------------------------------------------------------------
// MetricsRingBuffer
// -------------------------------------------------------------------

/**
 * @brief Fixed-size, thread-safe ring buffer of `MetricEntry` records.
 * @details Pre-fills to `capacity` on construction and always overwrites
 * at `writeIndex_`, rather than growing until full -- this matches
 * `VectorPro::Vector`'s counting constructor (count + fill value) rather
 * than `std::vector::reserve()`. Guarded by a mutex because FalconEye's
 * requests are dispatched across `ThreadPoolPro` worker threads, so
 * concurrent `push()` calls are expected, not hypothetical.
 */
class MetricsRingBuffer {
  public:
    /// @brief Constructs a ring buffer pre-filled with `capacity` default
    /// entries.
    explicit MetricsRingBuffer(std::size_t capacity)
        : capacity_(capacity), entries_(capacity, MetricEntry{}) {}

    /// @brief Records one entry, overwriting the oldest slot once full.
    void push(MetricEntry entry) {
        const std::lock_guard<std::mutex> lock(mutex_);
        entries_[writeIndex_] = std::move(entry);
        writeIndex_ = (writeIndex_ + 1) % capacity_;
        if (filled_ < capacity_) {
            ++filled_;
        }
        totalPushed_.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Total entries ever pushed, independent of the ring buffer's
    /// capacity -- unlike `entries().size()` (capped), this only ever
    /// grows.
    [[nodiscard]] std::uint64_t totalPushed() const noexcept {
        return totalPushed_.load(std::memory_order_relaxed);
    }

    /// @brief Returns all recorded entries, oldest first, newest last.
    [[nodiscard]] VectorPro::Vector<MetricEntry> entries() const {
        const std::lock_guard<std::mutex> lock(mutex_);

        VectorPro::Vector<MetricEntry> result(filled_, MetricEntry{});
        if (filled_ < capacity_) {
            // Not wrapped yet: 0..filled_-1 is already chronological.
            for (std::size_t i = 0; i < filled_; ++i) {
                result[i] = entries_[i];
            }
        } else {
            // Wrapped: oldest entry sits at writeIndex_ (next to be
            // overwritten); walk forward from there with wraparound.
            for (std::size_t i = 0; i < capacity_; ++i) {
                result[i] = entries_[(writeIndex_ + i) % capacity_];
            }
        }
        return result;
    }

  private:
    mutable std::mutex mutex_;
    VectorPro::Vector<MetricEntry> entries_;
    std::size_t capacity_;
    std::size_t writeIndex_ = 0;
    std::size_t filled_ = 0;
    // Lives here (behind Metrics's shared_ptr<MetricsRingBuffer>), not
    // directly on Metrics, since std::atomic isn't copyable either --
    // same reasoning as std::mutex below.
    std::atomic<std::uint64_t> totalPushed_{0};
};

// -------------------------------------------------------------------
// Metrics middleware
// -------------------------------------------------------------------

/**
 * @brief Times the full request/response cycle, collects any DB timings
 * recorded via `timedCall()`, and records both into a `MetricsRingBuffer`.
 * @details Callable matches `FalconHTTP::Middleware::MiddlewareFn`'s
 * signature exactly, so an instance can be registered directly via
 * `Server::use()`. Register LAST in the chain (after Recovery/Logger/Cors)
 * so its "after next()" timing captures the entire request, including the
 * route handler.
 *
 * Route pattern resolution: `MetricEntry::route` holds the matched route
 * pattern, not the raw path. Resolved by re-running
 * `FalconHTTP::Routing::PathMatcher::match()` against the Router's public
 * `routes` list -- read-only use of FalconHTTP's own public API, not a
 * modification to the `falconhttp/` submodule. Falls back to the raw path
 * if no route matches (e.g. a 404).
 *
 * Exception safety: if a route handler throws, `Recovery` (registered
 * outermost, ahead of `Metrics`) is what converts it to a 500 -- but the
 * exception unwinds straight through this middleware's `next()` call.
 * `operator()` records the entry in a `catch(...)` block too, then
 * rethrows unchanged so `Recovery` still sees the original exception.
 *
 * Requests to `/api/metrics` itself are never recorded. Without this
 * exclusion, a dashboard polling `/api/metrics` on an interval would
 * flood its own "recent requests" list with nothing but repeated
 * `GET /api/metrics` entries. `next()` still runs normally either way --
 * only the recording is skipped.
 *
 * The ring buffer is held via `shared_ptr`, not by value. Every copy of
 * `Metrics` (`FunctionPro::Function` requires copy-constructibility)
 * still shares the same underlying buffer, which is what we want
 * regardless, since there should only ever be one ring buffer per server.
 */
class Metrics {
  public:
    /// @brief Constructs a Metrics middleware.
    /// @param router Router to resolve route patterns against. Must
    /// outlive this Metrics instance.
    /// @param bufferCapacity Ring buffer capacity.
    Metrics(const FalconHTTP::Routing::Router& router, std::size_t bufferCapacity = 50)
        : router_(router), buffer_(std::make_shared<MetricsRingBuffer>(bufferCapacity)) {}

    /// @brief Middleware entry point. Matches `MiddlewareFn`'s signature.
    void operator()(FalconHTTP::HTTP::HttpRequest& request,
                    FalconHTTP::HTTP::HttpResponse& response,
                    const FalconHTTP::Middleware::NextHandler& next) {
        if (request.path() == "/api/metrics") {
            next(request, response);
            return;
        }

        auto start = std::chrono::steady_clock::now();

        VectorPro::Vector<std::pair<std::string, double>> breakdown;

        // Point the thread-local breakdown pointer (DbTiming.h) at this
        // request's vector before entering the rest of the chain, so any
        // timedCall() made by a route handler on this thread lands here.
        currentBreakdown = &breakdown;

        try {
            next(request, response);
        } catch (...) {
            currentBreakdown = nullptr;
            recordEntry(request, response, start, std::move(breakdown));
            throw;
        }

        currentBreakdown = nullptr;
        recordEntry(request, response, start, std::move(breakdown));
    }

    /// @brief Returns the recorded entries, oldest first.
    [[nodiscard]] VectorPro::Vector<MetricEntry> entries() const {
        return buffer_->entries();
    }

    /// @brief Total requests handled since this Metrics instance was
    /// constructed -- NOT the ring buffer's current size, which is
    /// capped and overwrites oldest entries.
    [[nodiscard]] std::uint64_t totalRequestCount() const noexcept {
        return buffer_->totalPushed();
    }

  private:
    /// @brief Builds and pushes a MetricEntry for the request currently
    /// finishing. Shared by both the normal-return and exception paths in
    /// `operator()` so the recording logic isn't duplicated.
    void recordEntry(FalconHTTP::HTTP::HttpRequest& request,
                     const FalconHTTP::HTTP::HttpResponse& response,
                     std::chrono::steady_clock::time_point start,
                     VectorPro::Vector<std::pair<std::string, double>> breakdown) {
        const double totalMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();

        MetricEntry entry;
        entry.route = matchedPatternFor(request.path());
        entry.method = std::string(FalconHTTP::HTTP::methodToString(request.method()));
        entry.status = static_cast<int>(response.status());
        entry.totalMs = totalMs;
        entry.timestamp = currentTimestampIso8601();
        entry.breakdown = std::move(breakdown);

        buffer_->push(std::move(entry));
    }

    /// @brief Resolves `path` to its matching route pattern by re-running
    /// `PathMatcher` against the Router's own registered routes.
    /// @param path Raw request path.
    /// @return The matched pattern, or `path` itself if nothing matched.
    [[nodiscard]] std::string matchedPatternFor(std::string_view path) const {
        for (const auto& route : router_.routes) {
            HashMapPro::HashMap<std::string, std::string> params;
            if (FalconHTTP::Routing::PathMatcher::match(route.pattern, path, params)) {
                return route.pattern;
            }
        }
        return std::string(path);
    }

    const FalconHTTP::Routing::Router& router_;
    std::shared_ptr<MetricsRingBuffer> buffer_;
};

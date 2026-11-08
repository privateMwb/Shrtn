/**
 * @file            DbTiming.h
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
#include <VectorPro/Vector.h> // VectorPro::Vector

#include <chrono> // std::chrono::steady_clock, std::chrono::duration
#include <string> // std::string
#include <utility> // std::pair
// clang-format on

/**
 * @brief Timing helper for MiniDB calls made inside route handlers,
 * appending into the same breakdown array `Metrics.h` builds for that
 * request.
 * @details Wraps MiniDB calls from the outside (route handler call sites)
 * rather than editing MiniDB itself -- `minidb/` stays an untouched
 * submodule. This can only capture timing for code called directly via
 * `timedCall()` (e.g. "dbQuery"). It cannot add per-middleware entries
 * for Recovery/Logger/Cors, since those are compiled into the untouched
 * `falconhttp/` submodule and have no hook into this mechanism.
 */

/// @brief Set by `Metrics::operator()` to point at the current request's
/// `MetricEntry::breakdown` before calling `next()`, and cleared after.
/// `thread_local` because `Server` is thread-per-connection -- each
/// pooled thread handles exactly one request start-to-finish, so no
/// locking is needed here.
///
/// Named without a trailing underscore deliberately: that suffix is
/// this project's convention for private class members (see
/// .clang-tidy's PrivateMemberSuffix), and this is a namespace-scope
/// global, not a member of any class.
inline thread_local VectorPro::Vector<std::pair<std::string, double>>* currentBreakdown = nullptr;

/**
 * @brief Returns the breakdown vector for the request currently in
 * flight on this thread.
 * @return Reference to the current request's breakdown vector.
 * @details Only valid while a request is being handled inside
 * `Metrics::operator()`'s `next()` call. Calling this outside that scope
 * (`currentBreakdown == nullptr`) is undefined behavior -- by design,
 * `timedCall()` is only ever meant to be used from within a route
 * handler.
 */
inline VectorPro::Vector<std::pair<std::string, double>>& currentRequestBreakdown() {
    return *currentBreakdown;
}

/**
 * @brief Times a MiniDB call site and appends the result into the
 * current request's breakdown.
 * @param label Name to record for this call (e.g. "dbQuery").
 * @param fn Callable to invoke and time, e.g. a lambda wrapping a MiniDB
 * call.
 * @return Whatever `fn` returns.
 */
template <typename Fn> auto timedCall(const char* label, Fn&& fn) {
    auto start = std::chrono::steady_clock::now();
    auto result = fn();
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    currentRequestBreakdown().push_back({label, ms});
    return result;
}

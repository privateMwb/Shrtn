/**
 * @file            RateLimiterMiddleware.h
 *
 * @copyright       Copyright (c) 2026 privateMwb
 *                  All rights reserved.
 *                  https://github.com/privateMwb/Shrtn
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

#pragma once

// clang-format off
#include <FalconHTTP/HTTP/HttpRequest.h>      // HttpRequest
#include <FalconHTTP/HTTP/HttpResponse.h>     // HttpResponse
#include <FalconHTTP/HTTP/HttpStatus.h>       // HttpStatus
#include <FalconHTTP/Middleware/Middleware.h> // NextHandler

#include <ThrottlePro/RateLimiter.h> // rain::RateLimiter (== ThrottlePro::RateLimiter)

#include <chrono>  // std::chrono::milliseconds
#include <cstddef> // std::size_t
#include <string>  // std::string
// clang-format on

// FalconHTTP integration wrapper for ThrottlePro::RateLimiter -- this is
// the "Phase 2" piece RateLimiter.h's own header comment calls out as
// deliberately not its job. RateLimiter itself is framework-agnostic;
// this class adapts it to FalconHTTP's onion-model MiddlewareFn
// signature and owns the one FalconHTTP-specific decision RateLimiter
// can't make itself: what key to rate-limit on.
//
// KEY CHOICE: HttpRequest exposes no client-IP accessor (see
// HttpRequest.h -- only headers/query/path params/body). Shrtn reads
// its PORT from the environment, implying it runs behind a proxy
// (e.g. Render), so the real client IP is read from the
// X-Forwarded-For header instead, using the first (left-most) address
// when the header carries a chain of proxies. If that header is ever
// absent -- direct/local access, or a proxy that doesn't set it --
// every such caller collapses onto one shared "unknown" bucket, i.e.
// they all share one rate-limit window with each other. That's a
// known, accepted degradation, not a crash; revisit if Shrtn is ever
// deployed somewhere that doesn't guarantee this header.

namespace Shrtn::Middleware {

namespace detail {

inline constexpr std::string_view kUnknownClientKey = "unknown";
inline constexpr std::string_view kForwardedForHeader = "X-Forwarded-For";

/// @brief Derives the rate-limit key for a request.
/// @details X-Forwarded-For may be a comma-separated chain
/// ("client, proxy1, proxy2"); the first entry is the original
/// client. Not defended against spoofing -- a caller can set this
/// header to anything if it reaches Shrtn directly rather than
/// through the trusted proxy. Fine for Render's setup, where the
/// proxy overwrites rather than appends; revisit if Shrtn is ever
/// reachable both directly and via proxy.
inline std::string clientKey(const FalconHTTP::HTTP::HttpRequest& request) {
    if (!request.hasHeader(std::string(kForwardedForHeader))) {
        return std::string(kUnknownClientKey);
    }

    const std::string forwarded = request.header(std::string(kForwardedForHeader));
    const std::size_t comma = forwarded.find(',');
    return comma == std::string::npos ? forwarded : forwarded.substr(0, comma);
}

} // namespace detail

/**
 * @class RateLimiterMiddleware
 * @brief Adapts ThrottlePro::RateLimiter to FalconHTTP's middleware chain.
 * @details Keys on the caller's X-Forwarded-For address (see file
 * comment). On a denied request, short-circuits with `429 Too Many
 * Requests` and does NOT call `next` -- same short-circuit pattern as
 * Cors's OPTIONS-preflight branch. `operator()` is intentionally not
 * `const` (unlike Cors/Logger/Recovery): RateLimiter::allow() mutates
 * its internal cache, and that's fine here since MiddlewareFn's
 * signature doesn't require constness of the underlying callable.
 *
 */
class RateLimiterMiddleware {
  public:
    /**
     * @param requestsPerWindow Maximum allowed requests per client, per window.
     * @param windowDuration Length of the fixed window.
     * @param cacheCapacity Max distinct clients tracked before LRU eviction.
     * Defaults to 10,000 -- revisit if Shrtn's expected distinct-client
     * volume per window is likely to exceed that.
     */
    RateLimiterMiddleware(std::size_t requestsPerWindow, std::chrono::milliseconds windowDuration,
                          std::size_t cacheCapacity = 10000)
        : limiter_(requestsPerWindow, windowDuration, cacheCapacity) {}

    void operator()(FalconHTTP::HTTP::HttpRequest& request, FalconHTTP::HTTP::HttpResponse& response,
                    const FalconHTTP::Middleware::NextHandler& next) {
        const std::string key = detail::clientKey(request);

        if (!limiter_.allow(key)) {
            response.setStatus(FalconHTTP::HTTP::HttpStatus::TooManyRequests);
            response.setBody("Rate limit exceeded. Please slow down and try again shortly.");
            return;
        }

        next(request, response);
    }

  private:
    rain::RateLimiter limiter_;
};

} // namespace Shrtn::Middleware

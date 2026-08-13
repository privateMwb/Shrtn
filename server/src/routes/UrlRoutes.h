/**
 * @file            UrlRoutes.h
 *
 * @date            2026-8-12
 *
 * @version         0.1.0
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
#include <FalconHTTP/HTTP/HttpRequest.h>  // HttpRequest
#include <FalconHTTP/HTTP/HttpResponse.h> // HttpResponse
#include <FalconHTTP/HTTP/HttpStatus.h>   // HttpStatus

#include "DbTiming.h" // timedCall
#include "ShrtnDb.h"  // Shrtn::ShrtnDb

#include <JsonPro/Json.h> // JsonPro::Json
#include <VectorPro/Vector.h> // VectorPro::Vector

#include <chrono>      // std::chrono::system_clock
#include <ctime>       // std::time_t, std::tm, gmtime_r/gmtime_s
#include <iomanip>     // std::put_time
#include <random>      // std::mt19937, std::random_device, std::uniform_int_distribution
#include <sstream>     // std::ostringstream
#include <string>      // std::string
// clang-format on

// Phase 2: POST /shorten only -- validates the submitted URL, generates a
// random short code with bounded-retry collision handling, and inserts
// via MiniDB. No GET /:code redirect yet (Phase 3).

namespace Shrtn::Routes {

namespace detail {

/// Same reasoning as Metrics.h's currentTimestampIso8601() -- reused
/// here rather than shared, since duplicating one ten-line helper is
/// cheaper than introducing a cross-project dependency for it.
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

/// @brief Generates a random base62 short code.
/// @param length Number of characters to generate.
inline std::string randomCode(std::size_t length) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    // thread_local so concurrent requests on the thread pool don't share
    // (and contend on) one generator, and don't need their own mutex.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, alphabet.size() - 1);

    std::string code(length, '\0');
    for (char& c : code) {
        c = alphabet[dist(rng)];
    }
    return code;
}

/// @brief Maximum accepted length for a submitted URL, in characters.
/// @details Not from the guide's Phase 2/5 notes -- found while
/// reviewing for Phase 5. Without this, the only limit on "url" was
/// Server's 10 MiB body cap, so a multi-megabyte string would be
/// accepted and stored whole. 2048 matches the de facto practical
/// limit most browsers/servers already enforce on real URLs.
inline constexpr std::size_t kMaxUrlLength = 2048;

/// @brief Rejects obviously malformed or unsafe URLs.
/// @details Requires an http/https scheme -- this both catches clearly
/// malformed input (no scheme at all) and, as the project guide calls
/// out explicitly, blocks `javascript:`/`data:` (and any other non-http
/// scheme) as a basic open-redirect/XSS safeguard on the eventual
/// Phase 3 redirect. Deliberately simple: no full RFC 3986 parse, no
/// hostname/TLD validation.
inline bool isAcceptableUrl(const std::string& url) {
    if (url.size() > kMaxUrlLength) {
        return false;
    }
    return url.starts_with("http://") || url.starts_with("https://");
}

} // namespace detail

/// @brief Number of characters in a generated short code.
inline constexpr std::size_t kCodeLength = 6;

/// @brief Maximum collision-retry attempts before giving up on a single
/// POST /shorten request. Bounded deliberately -- see the project guide's
/// Phase 2 note against an infinite retry loop.
/// @details At kCodeLength == 6, the keyspace is 62^6 ~= 56.8 billion.
/// Even with a million existing codes already stored, a single random
/// attempt's collision odds are ~0.0000176 -- exhausting 5 retries is
/// not a realistic outcome at any scale Shrtn would plausibly reach.
/// This value is a safety bound against a pathological/adversarial
/// case, not a knob that needs tuning for expected load.
inline constexpr int kMaxCollisionRetries = 5;

/**
 * @brief Handles `POST /shorten`.
 * @param db Shrtn's database, shared across all requests.
 * @details Expects a JSON body `{"url": "..."}`. On success, responds
 * `201 Created` with `{"code": "..."}`. `400 Bad Request` for a missing
 * or unacceptable URL; `500 Internal Server Error` if collision retries
 * are exhausted or the insert otherwise fails.
 */
inline void postShorten(ShrtnDb& db, const FalconHTTP::HTTP::HttpRequest& request,
                        FalconHTTP::HTTP::HttpResponse& response) {
    JsonPro::Json body;
    try {
        body = request.json();
    } catch (...) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody("Malformed JSON body.");
        return;
    }

    if (!body.contains("url") || !body["url"].isString()) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody("Missing or non-string \"url\" field.");
        return;
    }

    const std::string originalUrl = body["url"].asString();
    if (!detail::isAcceptableUrl(originalUrl)) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody("URL must start with http:// or https://.");
        return;
    }

    // Optional -- defaults to public (matches the pre-existing behavior
    // for any caller that doesn't send this field at all).
    const bool isPrivate =
        body.contains("private") && body["private"].isBool() ? body["private"].asBool() : false;

    const std::string createdAt = detail::currentTimestampIso8601();

    for (int attempt = 0; attempt < kMaxCollisionRetries; ++attempt) {
        const std::string candidate = detail::randomCode(kCodeLength);

        const ShrtnDb::InsertOutcome outcome = timedCall("dbInsert", [&]() {
            return db.tryInsertUrl(candidate, originalUrl, createdAt, isPrivate);
        });

        if (outcome == ShrtnDb::InsertOutcome::Inserted) {
            JsonPro::Json result = JsonPro::Json::ObjectType{};
            result["code"] = candidate;
            response.setStatus(FalconHTTP::HTTP::HttpStatus::Created);
            response.setJson(result);
            return;
        }
        if (outcome == ShrtnDb::InsertOutcome::Error) {
            response.setStatus(FalconHTTP::HTTP::HttpStatus::InternalServerError);
            response.setBody("Failed to insert URL.");
            return;
        }
        // CodeTaken: loop and try another random candidate.
    }

    // Retries exhausted. No 503 in HttpStatus yet (see HttpStatus.h's
    // own note on its intentionally minimal set) -- 500 is the closest
    // available code until that's extended.
    response.setStatus(FalconHTTP::HTTP::HttpStatus::InternalServerError);
    response.setBody("Could not generate a unique short code; please retry.");
}

/**
 * @brief Handles `GET /:code`.
 * @param db Shrtn's database, shared across all requests.
 * @details Looks up `code` (`timedCall("dbLookup", ...)`); `404 Not Found`
 * if missing. On a match, increments clickCount
 * (`timedCall("dbClickIncrement", ...)`) and responds `302 Found` with
 * `Location` set to the stored URL. The increment's own success/failure
 * doesn't block the redirect -- a click that fails to record shouldn't
 * turn into a broken link for the visitor.
 */
inline void getRedirect(ShrtnDb& db, const FalconHTTP::HTTP::HttpRequest& request,
                        FalconHTTP::HTTP::HttpResponse& response) {
    const std::string code = request.pathParam("code");

    const ShrtnDb::LookupResult result =
        timedCall("dbLookup", [&]() { return db.findByCode(code); });

    if (!result.found) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::NotFound);
        response.setBody("Short code not found.");
        return;
    }

    timedCall("dbClickIncrement", [&]() { return db.incrementClickCount(result.id); });

    response.setStatus(FalconHTTP::HTTP::HttpStatus::Found);
    response.setHeader("Location", result.originalUrl);
}

/**
 * @brief Handles `GET /links`.
 * @param db Shrtn's database, shared across all requests.
 * @details Responds `200 OK` with a JSON array of every PUBLIC row:
 * `code`, `originalUrl`, `createdAt`, `clickCount`. Rows created with
 * `"private": true` are excluded -- use `GET /api/links/:code` (with
 * the code the creator already has) to poll a private link's own
 * clickCount instead. No pagination -- fine at Shrtn's expected scale
 * for now; revisit if the urls table grows large enough for this to
 * matter.
 */
inline void getLinks(ShrtnDb& db, const FalconHTTP::HTTP::HttpRequest&,
                     FalconHTTP::HTTP::HttpResponse& response) {
    const VectorPro::Vector<ShrtnDb::UrlEntry> entries = db.listPublic();

    JsonPro::Json::ArrayType arr;
    arr.reserve(entries.size());
    for (const auto& entry : entries) {
        JsonPro::Json row = JsonPro::Json::ObjectType{};
        row["code"] = entry.code;
        row["originalUrl"] = entry.originalUrl;
        row["createdAt"] = entry.createdAt;
        row["clickCount"] = entry.clickCount;
        arr.push_back(std::move(row));
    }

    JsonPro::Json result = arr;
    response.setStatus(FalconHTTP::HTTP::HttpStatus::Ok);
    response.setJson(result);
}

/**
 * @brief Handles `GET /api/links/:code`.
 * @param db Shrtn's database, shared across all requests.
 * @details Returns full metadata for one code -- public or private --
 * as JSON. Exists so a browser that created a private link can poll its
 * live clickCount, since that link is deliberately absent from
 * `GET /links`. `404 Not Found` if the code doesn't exist.
 * @attention This does NOT check isPrivate -- anyone who has (or
 * guesses) a code can call this, same as the redirect itself never
 * checked it either. "Private" only means "not listed"; treat it that
 * way when explaining this to users, not as real access control.
 */
inline void getLinkMeta(ShrtnDb& db, const FalconHTTP::HTTP::HttpRequest& request,
                        FalconHTTP::HTTP::HttpResponse& response) {
    const std::string code = request.pathParam("code");

    const ShrtnDb::MetadataResult result = db.getMetadataByCode(code);
    if (!result.found) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::NotFound);
        response.setBody("Short code not found.");
        return;
    }

    JsonPro::Json row = JsonPro::Json::ObjectType{};
    row["code"] = result.entry.code;
    row["originalUrl"] = result.entry.originalUrl;
    row["createdAt"] = result.entry.createdAt;
    row["clickCount"] = result.entry.clickCount;
    row["isPrivate"] = result.entry.isPrivate;

    response.setStatus(FalconHTTP::HTTP::HttpStatus::Ok);
    response.setJson(row);
}

} // namespace Shrtn::Routes

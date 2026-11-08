/**
 * @file            MetricsJson.h
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
#include "Metrics.h" // Metrics, MetricEntry

#include <JsonPro/Json.h>    // JsonPro::Json
#include <VectorPro/Vector.h> // VectorPro::Vector
// clang-format on

/**
 * @brief Serializes a single `MetricEntry` to the shape used in the
 * `/api/metrics` `recentRequests` array.
 * @param entry Entry to serialize.
 * @return The serialized JSON object.
 */
inline JsonPro::Json metricEntryToJson(const MetricEntry& entry) {
    JsonPro::Json obj = JsonPro::Json::ObjectType{};
    obj["route"] = entry.route;
    obj["method"] = entry.method;
    obj["status"] = entry.status;
    obj["totalMs"] = entry.totalMs;
    obj["timestamp"] = entry.timestamp;

    JsonPro::Json::ArrayType breakdownArr;
    breakdownArr.reserve(entry.breakdown.size());
    for (const auto& [name, ms] : entry.breakdown) {
        JsonPro::Json step = JsonPro::Json::ObjectType{};
        step["name"] = name;
        step["ms"] = ms;
        breakdownArr.push_back(std::move(step));
    }
    obj["breakdown"] = std::move(breakdownArr);

    return obj;
}

/**
 * @brief Serializes the full `/api/metrics` response.
 * @param metrics Metrics instance to read from.
 * @return A JSON object containing `requestCount` (lifetime total),
 * `errorCount` (status >= 400, within the current buffered window only),
 * `avgResponseMs`, and `recentRequests` (oldest first).
 */
inline JsonPro::Json metricsToJson(const Metrics& metrics) {
    const VectorPro::Vector<MetricEntry> entries = metrics.entries();

    double totalMs = 0.0;
    int errorCount = 0;
    for (const auto& entry : entries) {
        totalMs += entry.totalMs;
        if (entry.status >= 400) {
            ++errorCount;
        }
    }
    const double avgResponseMs =
        !entries.empty() ? totalMs / static_cast<double>(entries.size()) : 0.0;

    JsonPro::Json::ArrayType recentRequests;
    recentRequests.reserve(entries.size());
    for (const auto& entry : entries) {
        recentRequests.push_back(metricEntryToJson(entry));
    }

    JsonPro::Json root = JsonPro::Json::ObjectType{};
    root["requestCount"] = static_cast<double>(metrics.totalRequestCount());
    root["errorCount"] = errorCount;
    root["avgResponseMs"] = avgResponseMs;
    root["recentRequests"] = std::move(recentRequests);
    return root;
}

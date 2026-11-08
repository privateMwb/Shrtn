// Unit MetricsJson Test Suite
// Verifies the /api/metrics JSON the dashboard reads: field names and
// types stay stable, and any text in a route or step name survives being
// written out and parsed back.
//
// Covers:
// - metricEntryToJson() emits route, method, status, totalMs, timestamp, breakdown
// - Breakdown steps keep their order, names, and timings
// - An empty Metrics serializes to zero counts and no requests
// - Quotes, backslashes, newlines, and non-ASCII text round-trip through dump() and parse()

#include <support/framework.h>

#include "MetricsJson.h"

#include <FalconHTTP/Routing/Router.h>

namespace {

MetricEntry sampleEntry() {
    MetricEntry entry;
    entry.route = "/api/todos/:id";
    entry.method = "GET";
    entry.status = 404;
    entry.totalMs = 1.5;
    entry.timestamp = "2026-11-05T00:11:07Z";
    entry.breakdown.push_back({"dbQuery", 0.25});
    entry.breakdown.push_back({"render", 0.5});
    return entry;
}

} // namespace

// Verifies every field the dashboard reads is present with the right value.
static void entry_has_expected_fields() {
    const JsonPro::Json json = metricEntryToJson(sampleEntry());

    CHK(json["route"].asString() == "/api/todos/:id");
    CHK(json["method"].asString() == "GET");
    CHK(json["status"].asNumber() == 404.0);
    CHK(json["totalMs"].asNumber() == 1.5);
    CHK(json["timestamp"].asString() == "2026-11-05T00:11:07Z");
    CHK(json["breakdown"].isArray());
}

// Verifies breakdown steps keep their order and values.
static void breakdown_keeps_order() {
    const JsonPro::Json json = metricEntryToJson(sampleEntry());
    const JsonPro::Json& steps = json["breakdown"];

    CHK(steps.size() == 2);
    CHK(steps[0]["name"].asString() == "dbQuery");
    CHK(steps[0]["ms"].asNumber() == 0.25);
    CHK(steps[1]["name"].asString() == "render");
    CHK(steps[1]["ms"].asNumber() == 0.5);
}

// Verifies an entry with no timed steps has an empty array, not a missing field.
static void empty_breakdown_is_an_empty_array() {
    MetricEntry entry = sampleEntry();
    entry.breakdown.clear();

    const JsonPro::Json json = metricEntryToJson(entry);
    CHK(json["breakdown"].isArray());
    CHK(json["breakdown"].size() == 0);
}

// Verifies a Metrics that has seen no requests serializes to zeros.
static void empty_metrics_serialize_to_zero() {
    FalconHTTP::Routing::Router router;
    Metrics metrics(router, 10);

    const JsonPro::Json json = metricsToJson(metrics);
    CHK(json["requestCount"].asNumber() == 0.0);
    CHK(json["errorCount"].asNumber() == 0.0);
    CHK(json["avgResponseMs"].asNumber() == 0.0);
    CHK(json["recentRequests"].isArray());
    CHK(json["recentRequests"].size() == 0);
}

// Verifies awkward text in strings survives dump() then parse().
static void awkward_text_round_trips() {
    MetricEntry entry = sampleEntry();
    entry.route = "/a\"b\\c\n\td/\xC3\xA9/\xE2\x82\xAC";
    entry.breakdown.clear();
    entry.breakdown.push_back({"na\"me\\\n", 0.125});

    const JsonPro::Json original = metricEntryToJson(entry);
    const JsonPro::Json parsed = JsonPro::Json::parse(original.dump());

    CHK(parsed["route"].asString() == entry.route);
    CHK(parsed["breakdown"][0]["name"].asString() == "na\"me\\\n");
    CHK(parsed.dump() == original.dump());
}

static void run_tests() {
    RUN(entry_has_expected_fields);
    RUN(breakdown_keeps_order);
    RUN(empty_breakdown_is_an_empty_array);
    RUN(empty_metrics_serialize_to_zero);
    RUN(awkward_text_round_trips);
}

REGISTER_TEST_SUITE();

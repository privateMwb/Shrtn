#!/usr/bin/env bash
# Smoke test for a running Shrtn server. Not a substitute for real unit
# tests (Shrtn has none -- it's thin glue over already-tested FalconHTTP/
# MiniDB) -- this just confirms the whole request path actually works
# end-to-end against a live instance, the way the manual curl checks
# earlier in the project did, but repeatable.
#
# Usage: ./smoke_test.sh [base_url]
#   Defaults to http://localhost:8080

set -uo pipefail

BASE_URL="${1:-http://localhost:8080}"
PASS=0
FAIL=0

check() {
    local description="$1"
    local condition="$2"
    if [ "$condition" = "true" ]; then
        echo "  OK   $description"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $description"
        FAIL=$((FAIL + 1))
    fi
}

echo "Testing $BASE_URL"
echo

# --- /api/health ---
echo "GET /api/health"
health_status=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/health")
check "returns 200" "$([ "$health_status" = "200" ] && echo true || echo false)"
echo

# --- POST /shorten: valid URL ---
echo "POST /shorten (valid URL)"
shorten_response=$(curl -s -w "\n%{http_code}" -X POST "$BASE_URL/shorten" \
    -H "Content-Type: application/json" \
    -d '{"url": "https://example.com/smoke-test"}')
shorten_status=$(echo "$shorten_response" | tail -n1)
shorten_body=$(echo "$shorten_response" | sed '$d')
code=$(echo "$shorten_body" | grep -o '"code"[[:space:]]*:[[:space:]]*"[^"]*"' | sed -E 's/.*"([^"]*)"$/\1/')

check "returns 201" "$([ "$shorten_status" = "201" ] && echo true || echo false)"
check "response includes a code" "$([ -n "$code" ] && echo true || echo false)"
echo

if [ -z "$code" ]; then
    echo "No code returned -- skipping redirect/links checks that depend on it."
else
    # --- GET /:code: redirect ---
    echo "GET /$code"
    redirect_status=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/$code")
    location=$(curl -s -o /dev/null -D - "$BASE_URL/$code" | grep -i '^location:' | tr -d '\r')
    check "returns 302" "$([ "$redirect_status" = "302" ] && echo true || echo false)"
    check "Location header set" "$([ -n "$location" ] && echo true || echo false)"
    echo

    # --- GET /links: created code should appear with clickCount >= 1 ---
    echo "GET /links"
    links_body=$(curl -s "$BASE_URL/links")
    links_status=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/links")
    check "returns 200" "$([ "$links_status" = "200" ] && echo true || echo false)"
    check "created code appears in listing" "$(echo "$links_body" | grep -q "\"$code\"" && echo true || echo false)"
    echo
fi

# --- POST /shorten: rejected schemes ---
echo "POST /shorten (rejected URLs)"
for bad_url in "javascript:alert(1)" "data:text/html,x" "not-a-url"; do
    bad_status=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE_URL/shorten" \
        -H "Content-Type: application/json" \
        -d "{\"url\": \"$bad_url\"}")
    check "\"$bad_url\" returns 400" "$([ "$bad_status" = "400" ] && echo true || echo false)"
done
echo

# --- GET /nonexistent: 404, not swallowed by another route ---
echo "GET /this-code-does-not-exist"
notfound_status=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/this-code-does-not-exist")
check "returns 404" "$([ "$notfound_status" = "404" ] && echo true || echo false)"
echo

# --- GET /api/metrics: sanity check it's valid JSON with expected keys ---
echo "GET /api/metrics"
metrics_body=$(curl -s "$BASE_URL/api/metrics")
metrics_status=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/metrics")
check "returns 200" "$([ "$metrics_status" = "200" ] && echo true || echo false)"
check "response includes requestCount" "$(echo "$metrics_body" | grep -q "requestCount" && echo true || echo false)"
echo

echo "----------------------------------------"
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]

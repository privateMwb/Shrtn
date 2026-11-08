#!/usr/bin/env bash
# Checks a running Shrtn backend from the outside, the way a browser and FalconEye's
# dashboard would use it: the server answers, a link can be created, followed and
# counted, private links stay unlisted, bad input is refused, the metrics grow, and CORS
# is on. It creates two example links (one private) on the backend it checks.
#
#   scripts/check-demo.sh <backend-url> [frontend-origin]
#
#   scripts/check-demo.sh https://shrtn.onrender.com https://shrtn-nine.vercel.app
#   scripts/check-demo.sh http://localhost:8080 http://localhost:5173
#
# With a frontend origin it also checks that the CORS header lets that origin through.
# WAIT_SECONDS (default 120) is how long to wait for a cold start (a sleeping free-tier
# service takes a while). Needs curl. Exits non-zero if any check fails.
#
# Stays well under the server's rate limit (60 requests a minute per client).

set -uo pipefail

BASE="${1:?usage: check-demo.sh <backend-url> [frontend-origin]}"
BASE="${BASE%/}"
ORIGIN="${2:-}"
ORIGIN="${ORIGIN%/}"
WAIT="${WAIT_SECONDS:-120}"

passed=0
failed=0
ok()  { echo "PASS  $1"; passed=$((passed + 1)); }
bad() { echo "FAIL  $1${2:+  ($2)}"; failed=$((failed + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "got '$2', expected '$3'"; fi; }
http() { curl -s -m 10 -o /dev/null -w '%{http_code}' "$@" 2>/dev/null || echo 000; }
post() { curl -s -m 10 -X POST "$BASE/shorten" -H 'Content-Type: application/json' -d "$1"; }
# The server pretty-prints its JSON, so squash whitespace before picking a value out.
code_of() { tr -d ' \n\r' | sed -n 's/.*"code":"\([A-Za-z0-9]*\)".*/\1/p'; }
count() { curl -s -m 10 "$BASE/api/metrics" | tr -d ' \n\r' | sed -n 's/.*"requestCount":\([0-9]*\).*/\1/p'; }

# 1. The service answers (waiting out a cold start).
up=0
end=$((SECONDS + WAIT))
while [ "$SECONDS" -lt "$end" ]; do
    [ "$(http "$BASE/api/health")" = "200" ] && { up=1; break; }
    sleep 3
done
if [ "$up" != 1 ]; then
    bad "service answers /api/health within ${WAIT}s" "is the URL right, and is the service running?"
    echo; echo "RESULT: $passed passed, $failed failed"; exit 1
fi
ok "service answers /api/health"

# 2. Create a public link and follow it.
public="$(post '{"url": "https://example.com/check-demo"}' | code_of)"
if [ -z "$public" ]; then
    bad "POST /shorten returns a code"
else
    ok "POST /shorten returns a code ($public)"
    check "GET /$public redirects" "$(http "$BASE/$public")" "302"
    location="$(curl -s -m 10 -o /dev/null -D - "$BASE/$public" | tr -d '\r' | sed -n 's/^[Ll]ocation: //p')"
    check "Location is the original URL" "$location" "https://example.com/check-demo"
    clicks="$(curl -s -m 10 "$BASE/api/links/$public" | tr -d ' \n\r' | sed -n 's/.*"clickCount":\([0-9]*\).*/\1/p')"
    check "both visits were counted" "$clicks" "2"
    curl -s -m 10 "$BASE/links" | grep -q "\"$public\"" && ok "public link is listed" || bad "public link is listed"
fi

# 3. A private link is reachable by code but never listed.
private="$(post '{"url": "https://example.com/check-demo-private", "private": true}' | code_of)"
if [ -z "$private" ]; then
    bad "private POST /shorten returns a code"
else
    check "private link redirects" "$(http "$BASE/$private")" "302"
    curl -s -m 10 "$BASE/links" | grep -q "\"$private\"" && bad "private link stays out of /links" || ok "private link stays out of /links"
fi

# 4. Input checks.
check "javascript: URL is refused" "$(http -X POST "$BASE/shorten" -H 'Content-Type: application/json' -d '{"url": "javascript:alert(1)"}')" "400"
check "unknown code is 404" "$(http "$BASE/zzzzzz-not-a-code")" "404"

# 5. Metrics are real, keep growing, and show the database steps.
first="$(count)"
if [ -z "$first" ]; then
    bad "/api/metrics returns a requestCount"
else
    ok "/api/metrics returns a requestCount ($first)"
    http "$BASE/api/health" > /dev/null
    second="$(count)"
    if [ -n "$second" ] && [ "$second" -gt "$first" ]; then
        ok "requestCount keeps growing ($first -> $second)"
    else
        bad "requestCount keeps growing" "went from $first to ${second:-nothing}"
    fi
fi

body="$(curl -s -m 10 "$BASE/api/metrics")"
for field in route method status totalMs timestamp breakdown dbInsert dbLookup dbClickIncrement; do
    case "$body" in
        *"\"$field\""*|*"\"$field"*) ok "metrics mention \"$field\"" ;;
        *) bad "metrics mention \"$field\"" ;;
    esac
done
case "$body" in
    *"$public"*) [ -n "$public" ] && bad "metrics never contain a short code" || true ;;
    *) ok "metrics never contain a short code" ;;
esac

# 6. CORS.
allow="$(curl -s -m 10 -o /dev/null -D - -H "Origin: ${ORIGIN:-https://frontend.example}" "$BASE/api/metrics" \
    | tr -d '\r' | sed -n 's/^[Aa]ccess-[Cc]ontrol-[Aa]llow-[Oo]rigin: //p')"
if [ "$allow" = "*" ] || [ "$allow" = "${ORIGIN:-https://frontend.example}" ]; then
    ok "CORS lets ${ORIGIN:-a browser origin} through"
else
    bad "CORS lets ${ORIGIN:-a browser origin} through" "Access-Control-Allow-Origin was '$allow'"
fi

echo
echo "RESULT: $passed passed, $failed failed"
[ "$failed" -eq 0 ]

// Unit URL Validation Test Suite
// Verifies the input checks behind POST /shorten: which URLs are accepted,
// and that generated short codes have the right length and alphabet.
//
// Covers:
// - http:// and https:// URLs are accepted
// - Other schemes (javascript:, data:, ftp:) and bare text are rejected
// - The 2048-character cap is inclusive: 2048 passes, 2049 does not
// - randomCode() returns exactly the requested length of base62 characters
// - Repeated calls do not all return the same code

#include <support/framework.h>

#include "routes/UrlRoutes.h"

#include <algorithm>
#include <set>
#include <string>

namespace {

using Shrtn::Routes::detail::isAcceptableUrl;
using Shrtn::Routes::detail::kMaxUrlLength;
using Shrtn::Routes::detail::randomCode;

bool isBase62(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

} // namespace

// Verifies ordinary web URLs pass.
static void accepts_http_and_https() {
    CHK(isAcceptableUrl("http://example.com"));
    CHK(isAcceptableUrl("https://example.com"));
    CHK(isAcceptableUrl("https://example.com/a/b?c=d#e"));
}

// Verifies schemes that must never reach a redirect are refused.
static void rejects_other_schemes() {
    CHK(!isAcceptableUrl("javascript:alert(1)"));
    CHK(!isAcceptableUrl("data:text/html,<script>1</script>"));
    CHK(!isAcceptableUrl("ftp://example.com"));
    CHK(!isAcceptableUrl("example.com"));
    CHK(!isAcceptableUrl(""));
}

// Verifies the scheme match is case-sensitive, as documented by the prefix check.
static void scheme_match_is_case_sensitive() {
    CHK(!isAcceptableUrl("HTTPS://example.com"));
}

// Verifies the length cap is inclusive.
static void length_cap_is_inclusive() {
    const std::string prefix = "http://";
    const std::string atLimit = prefix + std::string(kMaxUrlLength - prefix.size(), 'a');
    const std::string overLimit = atLimit + "a";

    CHK(atLimit.size() == kMaxUrlLength);
    CHK(isAcceptableUrl(atLimit));
    CHK(!isAcceptableUrl(overLimit));
}

// Verifies codes have the requested length and only base62 characters.
static void codes_have_length_and_alphabet() {
    for (std::size_t length : {0U, 1U, 6U, 32U, 64U}) {
        const std::string code = randomCode(length);
        CHK(code.size() == length);
        CHK(std::all_of(code.begin(), code.end(), isBase62));
    }
}

// Verifies the generator is not constant (a 62^6 space makes a repeat across 50 draws
// vanishingly unlikely, so seeing more than one distinct value is a safe check).
static void codes_vary() {
    std::set<std::string> seen;
    for (int i = 0; i < 50; ++i)
        seen.insert(randomCode(6));
    CHK(seen.size() > 1);
}

static void run_tests() {
    RUN(accepts_http_and_https);
    RUN(rejects_other_schemes);
    RUN(scheme_match_is_case_sensitive);
    RUN(length_cap_is_inclusive);
    RUN(codes_have_length_and_alphabet);
    RUN(codes_vary);
}

REGISTER_TEST_SUITE();

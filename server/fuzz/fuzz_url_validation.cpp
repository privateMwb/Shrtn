// ============================================================
// server/fuzz/fuzz_url_validation.cpp
//
// Fuzzer for the input checks behind POST /shorten. The URL is
// attacker-controlled text, and isAcceptableUrl() is the boundary that
// keeps javascript:/data: links and oversized strings out of the database
// (and so out of the Location header of a later redirect).
//
// Checked for every input:
//   - a URL is accepted only if it is within the length cap AND starts
//     with http:// or https:// (checked independently of the function),
//   - randomCode() returns exactly the requested length, using only
//     base62 characters.
// ============================================================

#include "routes/UrlRoutes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

bool isBase62(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // First byte picks a code length; the rest is the candidate URL.
    const std::size_t codeLength = size > 0 ? data[0] % 65 : 0;
    const std::string url(reinterpret_cast<const char*>(data) + (size > 0 ? 1 : 0),
                          size > 0 ? size - 1 : 0);

    const bool accepted = Shrtn::Routes::detail::isAcceptableUrl(url);
    const bool expected = url.size() <= Shrtn::Routes::detail::kMaxUrlLength &&
                          (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0);
    if (accepted != expected)
        __builtin_trap();

    const std::string code = Shrtn::Routes::detail::randomCode(codeLength);
    if (code.size() != codeLength || !std::all_of(code.begin(), code.end(), isBase62))
        __builtin_trap();

    return 0;
}

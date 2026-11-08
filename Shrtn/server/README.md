# Wiring RateThrottle into Shrtn — setup steps

## 1. Add the submodule

```bash
cd server
git submodule add https://github.com/privateMwb/RateThrottle third_party/ratethrottle
git submodule update --init --recursive
```

`--recursive` matters here: RateThrottle pulls in `lrucache` as its own
nested submodule (for `CachePro::LRUCache`), so a plain `--init` without
`--recursive` will leave that nested submodule empty and break the build.

## 2. Files in this delivery

| File | Action |
|---|---|
| `CMakeLists.txt` | Replaces `server/CMakeLists.txt` — adds `add_subdirectory(third_party/ratethrottle)` and links `ThrottlePro`. |
| `main.cpp` | Replaces `server/src/main.cpp` — constructs `RateLimiterMiddleware` and registers it in the chain. |
| `RateLimiterMiddleware.h` | New file — put it at `server/src/middleware/RateLimiterMiddleware.h`. |

## 3. Before it'll compile

Check `HttpStatus.h` for a `TooManyRequests` entry. `RateLimiterMiddleware.h`
uses it, but given the "intentionally minimal set" note already in
`UrlRoutes.h` (no `503` yet either), it may not exist. If it's missing,
either add it to `HttpStatus.h`'s enum, or swap the middleware's status
line to `HttpStatus::InternalServerError`, matching the precedent
`UrlRoutes.h` already set for a missing status code.

## 4. Tuning

`main.cpp` sets 60 requests/minute per client, tracking up to 10,000
distinct clients. Both are starting points, not measured values —
adjust `requestsPerWindow`, `windowDuration`, and `cacheCapacity` in the
`RateLimiterMiddleware` constructor call once you have a sense of real
traffic.

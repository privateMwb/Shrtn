# Architecture

Shrtn has two halves: a C++ server (FalconHTTP for HTTP, MiniDB for storage) and a React
frontend that talks to it over JSON. The server also serves `/api/metrics`, the endpoint
[FalconEye](https://github.com/privateMwb/FalconEye) polls.

```
┌──────────────────────────────── server/ ──────────────────────────────────┐
│ FalconHTTP Server                                                          │
│   Recovery → Logger → RateLimiter → Cors → Metrics → Router → handler      │
│                          │                    │                  │         │
│                      429 if over              │                  └─ timedCall(...)
│                      the limit                ▼                  │         │
│                                    MetricsRingBuffer (200)       ▼         │
│                                                          ShrtnDb (mutex)   │
│                                                          MiniDB "urls"     │
│                                                          data/shrtn.json   │
└────────────────────────────────┬──────────────────────────────────────────┘
                                 │  JSON
┌────────────────────────────────▼──────── frontend/ ───────────────────────┐
│ React + Vite: shorten form, result card, link list, private-link tracking  │
└────────────────────────────────────────────────────────────────────────────┘
```

## The request path

1. A request enters FalconHTTP's middleware chain. `Recovery` and `Logger` come first, so
   even a rejected request is logged with its real status and duration.
2. `RateLimiterMiddleware` derives a client key from the first address in `X-Forwarded-For`
   and asks RateThrottle's `RateLimiter` for permission (60 requests per minute, up to
   10,000 clients tracked before LRU eviction). Over the limit, it answers `429` and does not
   call `next()`, so nothing downstream runs.
3. `Cors` adds its headers (it is constructed with no argument, so it allows every origin,
   `*`), then `Metrics` times the rest of the chain. `Metrics` is
   registered **last**, so its timer wraps the router and the matched handler.
4. The router matches one of five routes and calls the handler in `UrlRoutes.h`.
5. Handlers reach the database through `ShrtnDb`, wrapping each call in
   `timedCall(label, fn)` so it appears in that request's `breakdown`.
6. When the handler returns, `Metrics` records the route pattern, method, status, total
   time and breakdown in the ring buffer.

| Route | Handler | Timed steps |
|---|---|---|
| `POST /shorten` | `postShorten` | `dbInsert` |
| `GET /:code` | `getRedirect` | `dbLookup`, `dbClickIncrement` |
| `GET /links` | `getLinks` | |
| `GET /api/links/:code` | `getLinkMeta` | |
| `GET /api/health`, `GET /api/metrics` | inline in `main.cpp` | |

FalconHTTP's router tries routes in registration order and takes the first match, and
`GET /:code` matches any single path segment (`/links` included), so it is registered after
the other routes. Registering it earlier would swallow them.

## Why these designs

**Random codes with a bounded retry.** A code is six random base62 characters from a
thread-local `std::mt19937`, so concurrent requests don't share a generator or need a lock.
That is 62⁶ ≈ 56.8 billion codes. `postShorten` retries up to 5 times on a collision and then
answers `500`; the bound is a safety limit against a pathological case, not a tuned value.

**Check and insert in one critical section.** `tryInsertUrl()` tests for an existing code
and inserts under a single lock. Two separate locked calls would let two concurrent requests
both pass the check before either inserts, and MiniDB's `insertRecord()` only rejects
duplicate record ids, not duplicate field values.

**One mutex around the database.** `Table` and `Database` aren't internally thread-safe, and
FalconHTTP dispatches requests across a thread pool, so `ShrtnDb` serializes every access.
`incrementClickCount()` does its read-modify-write under one lock acquisition so concurrent
clicks aren't lost.

**Whole-database persistence.** Every successful insert or click rewrites `data/shrtn.json`
through `StorageEngine`. That is the simplest correct durability for this write volume; it
is also the first thing to revisit if writes become a bottleneck.

**Validation at the server.** `isAcceptableUrl()` requires an `http://` or `https://` prefix
and at most 2048 characters. This blocks `javascript:` and `data:` targets before they can
reach a redirect. The frontend repeats the check for a faster error, but the server is the
actual boundary.

**Private means unlisted.** `listPublic()` filters on `isPrivate` in the query, so private
rows are absent from the response rather than hidden client-side. The creator's browser keeps
its own codes in `localStorage` and polls `GET /api/links/:code` for live counts. That
endpoint doesn't check the flag, so the code itself is the only barrier.

**Reused observability.** `Metrics.h`, `DbTiming.h` and `MetricsJson.h` come from FalconEye.
Shrtn sets the ring buffer to 200 entries at the call site in `main.cpp`, without editing the
vendored files, because `/shorten` and `/:code` traffic can fill a 50-slot window within
seconds.

## The data model

One MiniDB table, `urls`, created on first run:

| Column | Type | Notes |
|---|---|---|
| `code` | string | The short code; the lookup key |
| `originalUrl` | string | Validated `http(s)` URL |
| `createdAt` | string | ISO 8601, UTC |
| `clickCount` | int | Incremented on every redirect |
| `isPrivate` | int | `0` or `1` |

The record id is an internal, monotonically increasing counter that MiniDB requires as a
storage key; it is never exposed. There is no migration path: changing the schema means
deleting `data/shrtn.json` and starting fresh.

## The frontend

`frontend/src/ShrtnApp.jsx` is one component tree with hand-written styles. It reads the
server's base URL from `VITE_API_BASE` (default `http://localhost:8080`), validates a URL
before submitting, shows the newest 50 links, and merges the creator's private links back
into their own view tagged `· private`. It makes no calls to `/api/metrics`.

## Fuzzing

`server/fuzz/` holds libFuzzer harnesses that ClusterFuzzLite builds under ASan and UBSan:

| Harness | Checks |
|---|---|
| `fuzz_url_validation` | `isAcceptableUrl()` agrees with an independent check for any input; `randomCode()` returns exactly the requested length of base62 characters |
| `fuzz_metrics_json` | Any route, method or step text serializes to JSON that parses back to the same text |
| `fuzz_db_timing` | Each `timedCall()` appends exactly one step with a non-negative duration and returns the wrapped result |

Each runs for 3 minutes on pull requests that touch the code involved, and for 30 minutes
every night.

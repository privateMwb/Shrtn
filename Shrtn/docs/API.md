# API Reference

Base URL: wherever `server/` is deployed (`http://localhost:8080` in dev).

All responses are JSON except `GET /:code`, which redirects.

---

### `POST /shorten`

Create a short link.

**Body**
```json
{ "url": "https://example.com/very/long/path", "private": false }
```
- `url` (required) -- must start with `http://` or `https://`. Max 2048 characters.
- `private` (optional, default `false`) -- if `true`, excluded from `GET /links`. Does **not** make the link inaccessible; see [Known limitations](../README.md#known-limitations).

**Responses**
| Status | Meaning |
|---|---|
| `201 Created` | `{"code": "j3dche"}` |
| `400 Bad Request` | Missing/non-string `url`, wrong scheme, or over the length cap |
| `500 Internal Server Error` | Collision retries exhausted (see note below), or a MiniDB failure |

---

### `GET /:code`

Redirects to the original URL and increments its click count.

| Status | Meaning |
|---|---|
| `302 Found` | `Location` header set to the original URL |
| `404 Not Found` | No such code |

Click-count increment failures are swallowed silently (the redirect still succeeds) -- there is currently no visibility into whether an individual click actually got recorded.

---

### `GET /links`

Returns every **public** link. Private links are excluded entirely -- not filtered client-side, actually absent from the response.

**Response**
```json
[
  {
    "code": "j3dche",
    "originalUrl": "https://example.com",
    "createdAt": "2026-08-12T03:14:07Z",
    "clickCount": 2
  }
]
```

No pagination. No ordering guarantee from the server -- the frontend sorts client-side, newest first, and caps display at 50.

---

### `GET /api/links/:code`

Returns metadata for one code, public or private. Exists so a browser that created a private link can poll its own live click count.

**Response**
```json
{
  "code": "j3dche",
  "originalUrl": "https://example.com",
  "createdAt": "2026-08-12T03:14:07Z",
  "clickCount": 2,
  "isPrivate": true
}
```

| Status | Meaning |
|---|---|
| `200 OK` | Found |
| `404 Not Found` | No such code |

**This endpoint does not check `isPrivate`.** Anyone with the code can call it. "Private" means unlisted, not protected -- see [Known limitations](../README.md#known-limitations).

---

### `GET /api/health`

Liveness check.

| Status | Meaning |
|---|---|
| `200 OK` | Body: `ok` |

---

### `GET /api/metrics`

Rolling request metrics -- request count, error count, average response time, and the last 200 requests with per-request timing breakdowns (e.g. `dbInsert`, `dbLookup`, `dbClickIncrement`).

Intended to be polled by [FalconEye](#), not the Shrtn frontend itself. No auth on this endpoint -- see [Known limitations](../README.md#known-limitations).

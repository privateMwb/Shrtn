# Deployment

Shrtn deploys in two independent pieces: the **frontend** on Vercel and the **backend** on
Render. They are different kinds of workload, so one platform can't host both.

## What gets deployed

| Piece | Where | What it is |
|---|---|---|
| Frontend | Vercel | The static Vite build of `frontend/` |
| Backend | Render web service | The repo's `Dockerfile`: `Shrtn_server` listening on `$PORT` |

**Why not Vercel for the backend.** `server/` is a long-running process that binds a
persistent TCP port (`server.start(port)` then `server.run()`, blocking forever) and writes
`data/shrtn.json` to disk. Vercel runs serverless functions that start per request and exit,
with no persistent port and no durable filesystem.

## 1. Check the image locally (optional, needs Docker)

```bash
docker build -t shrtn .
docker run --rm -p 8080:8080 shrtn
./scripts/smoke_test.sh http://localhost:8080
```

## 2. Render (backend)

The repo's `render.yaml` is a Blueprint describing the service, so you can either use it
(**New → Blueprint**, pick the repo) or set it up by hand:

1. Push the repo to GitHub.
2. In Render: **New → Web Service**, pick the repo, runtime **Docker** (it uses `./Dockerfile`).
3. Set the health check path to `/api/health`.
4. Wait for the first build (a few minutes; it compiles the C++ server).
5. Copy the service URL, for example `https://shrtn.onrender.com`, and open
   `<url>/api/health`: it should return `ok`.
6. Check it end to end from outside (it creates two example links on that backend):

   ```bash
   scripts/check-demo.sh https://shrtn.onrender.com https://<your-frontend>.vercel.app
   ```

   The *Demo image* workflow runs the same script against the Docker image on every change to
   the server, the Dockerfile or `scripts/`.

The server reads `PORT` from the environment and falls back to 8080 locally.

**Nested submodules.** Render clones this repo's submodules, but doesn't recurse into theirs
(MiniDB, for example, carries its own). `init-nested-submodules.sh` walks the tree for every
`.gitmodules` file and clones anything still empty. It works without `.git` metadata, so it
runs inside the Docker build before CMake configures.

**The image.** A multi-stage build: an `ubuntu:24.04` stage with the full toolchain compiles
`Shrtn_server` in Release mode with the submodules' tests, benchmarks and examples off, and
the final stage is a clean `ubuntu:24.04` with only `libstdc++6` and the binary.

## 3. Vercel (frontend)

1. **Add New → Project**, import the repo.
2. **Root Directory:** `frontend`. Framework preset: Vite.
3. Environment variable:

   ```
   VITE_API_BASE = https://<your-render-url>
   ```

   No trailing slash. It is read at build time, so changing it needs a redeploy. For local
   development, put it in `frontend/.env.local`.
4. Deploy.

`frontend/vercel.json` rewrites every path to `index.html` and adds `nosniff`,
`Referrer-Policy` and `X-Frame-Options: DENY` headers.

## Behavior to expect

- **Links are lost on redeploy.** `/app/data` is created inside the image layer, so it is
  **not** a persistent volume. Every redeploy, and on Render's free plan every spin-down
  after 15 minutes of inactivity, rebuilds the container and resets `shrtn.json` to empty.
  Attaching a Render persistent disk mounted at `/app/data` fixes this, at the paid tier's
  cost.
- **Free plan sleep.** The first request after a quiet spell takes about a minute.
- **Rate limiting behind a proxy.** The limiter reads the client from `X-Forwarded-For`.
  Render's proxy sets it, so each visitor gets their own 60-requests-per-minute bucket. A
  caller who can reach the container without the proxy could spoof it.
- **CORS.** The server's `Cors` middleware is constructed with no argument, so it allows
  every origin (`*`) and any frontend can call it. If you tighten that in `main.cpp`, the
  frontend's exact origin must be allowed or the browser blocks the calls.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Build fails with an empty `third_party/` directory | Nested submodules weren't cloned; check the output of `init-nested-submodules.sh` in the build log |
| Frontend loads but can't reach the server | `VITE_API_BASE` is wrong, has a trailing slash, or the frontend wasn't redeployed after changing it |
| Works in `curl`, fails in the browser | CORS, if `main.cpp` no longer uses the allow-all `Cors{}`: check the `Access-Control-Allow-Origin` header |
| Every link vanished | The container was rebuilt or spun down; see "Links are lost on redeploy" |
| `429 Too Many Requests` for everyone at once | The proxy isn't sending `X-Forwarded-For`, so all callers share one bucket |

## Updating

Merging to `main` redeploys both: Render rebuilds the image and Vercel rebuilds the
frontend. Expect the link data to reset (see above).

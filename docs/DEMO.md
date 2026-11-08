# Demo

A short tour of Shrtn, from shortening a link to watching it in FalconEye.

## Run it locally

```bash
# Terminal 1: the server (port 8080)
git clone --recurse-submodules https://github.com/privateMwb/Shrtn.git && cd Shrtn
cd server && cmake -B build && cmake --build build && mkdir -p data && ./build/Shrtn_server

# Terminal 2: the frontend (http://localhost:5173)
cd frontend && npm install && npm run dev
```

## What to look at

1. **Shorten a link.** Paste `https://example.com` into the form and submit. The result card
   shows the new code with a copy button.
2. **Visit it.** Open `http://localhost:8080/<code>`. You get a `302` to the original URL,
   and the click count goes up by one in the link list.
3. **Make a private link.** Turn on the private toggle and shorten another URL. It appears in
   your own list tagged `· private`, but `curl localhost:8080/links` doesn't contain it.
   Open the page in a private window: it isn't there either.
4. **Try a bad URL.** Submit `javascript:alert(1)`. The frontend refuses it, and so does the
   server if you send it directly:

   ```bash
   curl -i -X POST localhost:8080/shorten -H "Content-Type: application/json" \
     -d '{"url": "javascript:alert(1)"}'          # 400 Bad Request
   ```
5. **Hit the rate limit.** Without an `X-Forwarded-For` header every caller shares one
   bucket of 60 requests per minute:

   ```bash
   for i in $(seq 1 70); do curl -s -o /dev/null -w "%{http_code} " localhost:8080/api/health; done
   ```

   The last few answers are `429`.
6. **Check the whole path.** `./scripts/smoke_test.sh http://localhost:8080` runs the same
   checks end to end, and `./scripts/check-demo.sh http://localhost:8080` checks the server
   the way a deployed one is checked.

## Watch it in FalconEye

Shrtn serves the JSON shape [FalconEye](https://github.com/privateMwb/FalconEye)'s dashboard
reads. Run FalconEye's frontend and point it at Shrtn:

```bash
git clone https://github.com/privateMwb/FalconEye.git
cd FalconEye && npm --prefix frontend install && npm --prefix frontend run dev
# open http://localhost:5173/?target=http://localhost:8080
```

Use a different port if Shrtn's frontend is still running on 5173. Shrtn's `Cors` middleware
allows every origin, so the dashboard can read it from wherever it runs.

Then shorten a few links and visit them. In the request log:

- `POST /shorten` shows a `dbInsert` step.
- `GET /:code` shows `dbLookup` and `dbClickIncrement` as two separate steps. The route
  appears as the pattern `/:code`, never the raw code.
- The 404 for an unknown code counts as an error.

## The hosted demo

[shrtn-nine.vercel.app](https://shrtn-nine.vercel.app) is the frontend on Vercel, talking to
a backend on Render. See [DEPLOYMENT.md](DEPLOYMENT.md) for how that is set up and what to
expect from the free plan.

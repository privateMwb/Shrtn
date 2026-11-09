# Changelog

All notable changes to Shrtn are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Unit test suite (`server/tests/`, target `shrtn_tests`): `ShrtnDb`, URL validation and
  short-code generation, plus the metrics ring buffer, the `/api/metrics` JSON and
  `timedCall()`. CI runs it after every build; CMake option `SHRTN_BUILD_TESTS`, on by default.
- Screenshots workflow (`screenshots.yml`, `scripts/capture-screenshots.mjs`): captures the
  frontend in a headless browser, locally on the runner or from a live deployment, and opens
  a PR with the images.
- Demo image workflow (`demo-image.yml`) and `scripts/check-demo.sh`: build the Docker image
  and check the running container from outside. The script also works against a deployed
  service.
- `render.yaml` (Render Blueprint), `frontend/vercel.json`, and the `main-protection` branch
  ruleset (`.github/rulesets/main-protection.json`).
- ClusterFuzzLite fuzzing (`.clusterfuzzlite/`, `server/fuzz/`): harnesses for the URL
  validation and short-code generation behind `POST /shorten`, the `/api/metrics`
  serialization path, and `timedCall()`, with seed inputs, a short run on pull requests and
  a longer nightly run. Harnesses are picked up automatically from `server/fuzz/fuzz_*.cpp`
  (CMake option `SHRTN_BUILD_FUZZERS`, off by default).
- CodeQL workflow (C/C++ and JavaScript), `CODEOWNERS`, and Dependabot for GitHub Actions,
  npm, Docker and the git submodules, with updates grouped into one PR per ecosystem.
- `.dockerignore`, to keep the Docker build context small.
- `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, and guides under `docs/`
  (`ARCHITECTURE.md`, `DEMO.md`, `DEPLOYMENT.md`).
- Banner, divider and back-to-top artwork under `.github/assets/`.

### Changed

- Frontend and README artwork use one palette, "Magenta flame" (`#E879F9` → `#F472B6` → `#FB7185`):
  gradient wordmark, primary button and status dot; error states keep a separate true red. New
  favicon, linked from `index.html` along with a `theme-color`.
- Frontend dependencies upgraded together: React 19, Vite 8, `@vitejs/plugin-react` 6.
- Workflow actions are on the same versions FalconEye uses (`checkout@v7`, `setup-node@v7`,
  `upload-pages-artifact@v5`, `deploy-pages@v5`).
- The Docker build turns the test target off (`-DSHRTN_BUILD_TESTS=OFF`).
- `scripts/smoke_test.sh` is now executable.
- The root README is restructured to match FalconEye's: badge groups, a table of contents, a
  "How It Works" section with diagrams, and a Design Notes section that now holds the known
  limitations. It also documents RateThrottle, which was previously missing from the
  dependency list.

## [1.0.0]

First release: a URL shortener built on FalconHTTP and MiniDB.

### Added

**Server**

- Random 6-character base62 short codes via `POST /shorten`, with bounded collision-retry
  (5 attempts).
- `GET /:code` redirects with `302 Found` and increments the link's click count, timed as
  separate `dbLookup` and `dbClickIncrement` steps.
- Private links: an optional `private` flag excludes a link from `GET /links`;
  `GET /api/links/:code` lets its creator poll the live click count.
- Input validation: non-http(s) schemes (including `javascript:` and `data:`) and URLs over
  2048 characters are rejected.
- `Metrics`, `DbTiming.h` and `MetricsJson.h` vendored from FalconEye, with the ring buffer
  set to 200 entries at the call site, and `GET /api/metrics` for FalconEye's dashboard.
- Per-client rate limiting through RateThrottle (60 requests per minute, keyed on
  `X-Forwarded-For`).
- `Found = 302` added to FalconHTTP's `HttpStatus.h`, the one edit to that submodule.

**Frontend**

- Vite + React app: shorten form with a private-link toggle, result card with copy to
  clipboard, a scrollable list of the newest 50 links, and real loading, empty and error
  states.
- Private links are remembered in the creator's browser (`localStorage`) and shown in their
  own view.

**CI**

- Build, Clang Format, Clang Tidy, Sanitizers (ASan + UBSan, TSan), Frontend, Docs
  (Doxygen to GitHub Pages) and Release workflows.

### Known limitations

- No authentication or accounts; no schema migration path.
- Private links are unlisted, not access-controlled.
- `/app/data` is not a persistent volume on Render as deployed.
- No automated unit test suite at this release; `scripts/smoke_test.sh` was the end-to-end check.

[Unreleased]: https://github.com/privateMwb/Shrtn/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/privateMwb/Shrtn/releases/tag/v1.0.0

# Contributing to Shrtn

Issues and pull requests are welcome. This guide covers how to set up, what CI checks, and
how a change gets merged.

## Setting up

```bash
git clone --recurse-submodules https://github.com/privateMwb/Shrtn.git
cd Shrtn

cd server
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
mkdir -p data          # must exist before first run: StorageEngine won't create it
./build/Shrtn_server   # http://localhost:8080

cd ../frontend
npm install
npm run dev            # http://localhost:5173
```

FalconHTTP, MiniDatabase and RateThrottle are git submodules under `server/third_party/`,
and RateThrottle has a nested submodule of its own, so clone with `--recurse-submodules`
(or run `git submodule update --init --recursive`). Requirements: a C++23 compiler (GCC or
Clang, Linux), CMake 3.20+, and Node.js 20+.

## The workflow

1. Create a branch from `main`.
2. Make your change.
3. Run the checks below.
4. Open a pull request. CI runs on it; merge once everything passes.

## Checks to run locally

CI enforces all of these:

```bash
# Formatting (server/include and server/src only; third_party/ is upstream code)
find server/include server/src \( -name "*.h" -o -name "*.cpp" \) -exec clang-format --dry-run --Werror {} +

# Static analysis
clang-tidy -p server/build $(find server/include server/src -name "*.h" -o -name "*.cpp")

# Unit tests
cmake --build server/build --target shrtn_tests && ./server/build/shrtn_tests

# End-to-end smoke test against a running server
./scripts/smoke_test.sh http://localhost:8080

# Frontend
cd frontend && npm run build
```

## What to test

`server/tests/` holds the unit tests (`shrtn_tests`), which CI runs after every build. Each
file in `server/tests/suite/unit/` is one suite and registers itself, so a new `.cpp` file
there is picked up with no build-file edit. `scripts/smoke_test.sh` is the end-to-end check
against a live server.

- **`ShrtnDb` changes** (`tryInsertUrl`, `findByCode`, `incrementClickCount`): add or extend
  a case in `shrtnDb.cpp`. For locking changes, also say in the PR how you checked behavior
  under concurrent load; the TSan job in `sanitizers.yml` is the closest regression check.
- **API changes** (`UrlRoutes.h`): update the smoke test, `scripts/check-demo.sh` if it
  touches what that script checks, and `docs/API.md` together.
- **New code that parses or validates text:** consider a harness in `server/fuzz/`. Any
  `fuzz_<name>.cpp` file there is built automatically; put a few seed inputs in
  `server/fuzz/corpus/fuzz_<name>/`.

## Style

- C++ follows `.clang-format` and the naming rules in `.clang-tidy`.
- Comments explain why, not what.
- Keep the README, `CHANGELOG.md` and `docs/` in step with behavior changes. Add an entry
  under `[Unreleased]` in the changelog.
- The files `Metrics.h`, `DbTiming.h` and `MetricsJson.h` come from FalconEye. Change them
  there first so the two projects don't drift.

## Reporting bugs and vulnerabilities

For bugs, open an issue with the steps to reproduce, the commit you tested, and what you
expected. **Do not** report suspected vulnerabilities in public; see
[SECURITY.md](SECURITY.md).

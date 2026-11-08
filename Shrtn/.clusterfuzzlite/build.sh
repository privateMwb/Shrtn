#!/bin/bash -eu
# ============================================================
# .clusterfuzzlite/build.sh
#
# Builds every server/fuzz/fuzz_*.cpp harness for ClusterFuzzLite.
# CMake does the work (SHRTN_BUILD_FUZZERS=ON), so the include paths
# and libraries come from the same targets the server uses. Sanitizer
# flags reach the compiler through $CXXFLAGS, which CMake reads when it
# configures, and the engine is linked through $LIB_FUZZING_ENGINE.
#
# Harnesses are discovered by glob: drop a new
# server/fuzz/fuzz_<target>.cpp in place and it becomes its own
# $OUT binary with no edit here.
#
# Seed inputs: server/fuzz/corpus/<harness-name>/ is zipped into
# $OUT/<harness-name>_seed_corpus.zip when present.
#
# Nested submodules: the CI checkout is recursive. A local
# `docker build` of this image has no .git metadata, so
# init-nested-submodules.sh fills in anything still empty.
# ============================================================

cd "${SRC}/Shrtn"
if [ -x ./init-nested-submodules.sh ]; then
  ./init-nested-submodules.sh
fi

cd "${SRC}/Shrtn/server"

# No CMAKE_BUILD_TYPE: OSS-Fuzz's own optimization/debug flags are already
# in $CXXFLAGS and must not be overridden.
cmake -S . -B "${WORK}/build" \
  -DSHRTN_BUILD_FUZZERS=ON

TARGETS=()
for harness in fuzz/fuzz_*.cpp; do
  TARGETS+=("$(basename "${harness}" .cpp)")
done

cmake --build "${WORK}/build" --parallel "$(nproc)" --target "${TARGETS[@]}"

for name in "${TARGETS[@]}"; do
  cp "${WORK}/build/${name}" "${OUT}/${name}"

  if [ -d "fuzz/corpus/${name}" ]; then
    (cd "fuzz/corpus/${name}" && zip -q -r "${OUT}/${name}_seed_corpus.zip" .)
  fi
done

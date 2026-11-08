#!/bin/bash
set -euo pipefail

# Render clones this repo's own submodules (e.g. third_party/minidb,
# third_party/falconhttp) automatically, but does not recurse into
# THEIR .gitmodules files (e.g. libs/internal/JsonParser inside minidb).
# This script finds every .gitmodules file in the tree and clones any
# submodule path that's still empty. It works without .git metadata,
# since .gitmodules is a plain tracked text file that survives COPY.

find . -name ".gitmodules" | while read -r modfile; do
    dir=$(dirname "$modfile")
    echo "Found .gitmodules in: $dir"

    git config -f "$modfile" --get-regexp '\.path$' | while read -r key path; do
        name=$(echo "$key" | sed -E 's/^submodule\.(.*)\.path$/\1/')
        url=$(git config -f "$modfile" --get "submodule.$name.url")
        target="$dir/$path"

        if [ -z "$(ls -A "$target" 2>/dev/null || true)" ]; then
            echo "  Cloning missing submodule: $target <- $url"
            rm -rf "$target"
            git clone --depth 1 "$url" "$target"
        else
            echo "  Already present: $target"
        fi
    done
done

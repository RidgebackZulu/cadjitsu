#!/usr/bin/env bash
# The version label of this build, for file names and the app's version.
#
#   release (a v* tag, or RELEASE=1):  1.0.0
#   any other build:                   1.0.1-dev.<run>  (the next patch once
#                                      v<version> is tagged, else <version>-dev.<run>)
#
# The version is project(VERSION) in CMakeLists.txt. On a tag, the tag must be
# v<version>. Prints the label; with --github also writes label= and version=
# to $GITHUB_OUTPUT and CADJITSU_LABEL to $GITHUB_ENV.
set -euo pipefail
cd "$(dirname "$0")/.."

version=$(sed -n 's/^[[:space:]]*VERSION[[:space:]]\{1,\}\([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n 1)
if [ -z "$version" ]; then
    echo "version_label: no VERSION in CMakeLists.txt" >&2
    exit 1
fi

ref="${GITHUB_REF:-}"
run="${GITHUB_RUN_NUMBER:-0}"
if [[ "$ref" == refs/tags/v* ]]; then
    tag="${ref#refs/tags/}"
    if [ "$tag" != "v$version" ]; then
        echo "version_label: tag $tag does not match the project version $version" >&2
        exit 1
    fi
    label="$version"
elif [ "${RELEASE:-0}" = 1 ]; then
    label="$version"
else
    base="$version"
    if git rev-parse -q --verify "refs/tags/v$version" > /dev/null; then
        IFS=. read -r major minor patch <<< "$version"
        base="$major.$minor.$((${patch:-0} + 1))"
    fi
    label="$base-dev.$run"
fi

echo "$label"
if [ "${1:-}" = "--github" ]; then
    echo "label=$label" >> "$GITHUB_OUTPUT"
    echo "version=$version" >> "$GITHUB_OUTPUT"
    echo "CADJITSU_LABEL=$label" >> "$GITHUB_ENV"
fi

#!/usr/bin/env bash
# Regenerates the README's pictures in docs/images with the `showcase` self
# test: models built through the MCP tools and photographed in the app, the
# hero picture path traced at full quality (256 samples per pixel, about two
# minutes on a laptop CPU).
#
#   scripts/readme_images.sh [path/to/Cadjitsu]
#
# On Linux it runs under Xvfb (scripts/run_xvfb.sh); on macOS pass the app's
# binary, e.g. build/macos/src/app/Cadjitsu.app/Contents/MacOS/Cadjitsu.
set -euo pipefail
cd "$(dirname "$0")/.."

app="${1:-build/linux/src/app/Cadjitsu}"
out="$(mktemp -d)"
trap 'rm -rf "${out:?}"' EXIT

runner=()
if [[ "$(uname)" == Linux ]]; then
    runner=(scripts/run_xvfb.sh)
fi

CADJITSU_SHOWCASE_SAMPLES="${CADJITSU_SHOWCASE_SAMPLES:-256}" \
    ${runner[@]+"${runner[@]}"} "$app" --selftest=showcase --out "$out"

mkdir -p docs/images
for f in hero.jpg modeling.png sketch.png photo_to_sketch.jpg views_to_3d.png print_checks.png ai_agent.png; do
    cp "$out/$f" docs/images/
done
echo "docs/images updated"

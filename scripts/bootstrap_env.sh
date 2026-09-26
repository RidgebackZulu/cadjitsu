#!/usr/bin/env bash
# Create the Linux build environment (Qt 6.9, OpenCASCADE 7.9, Eigen, GCC, CMake,
# Ninja) with micromamba from conda-forge. Idempotent: re-running only updates.
#
#   CADLY_ENV   where the environment goes   (default /opt/cadly-tools/env)
#   CADLY_TOOLS where micromamba is unpacked (default /opt/cadly-tools)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CADLY_TOOLS="${CADLY_TOOLS:-/opt/cadly-tools}"
CADLY_ENV="${CADLY_ENV:-$CADLY_TOOLS/env}"
MICROMAMBA_VERSION="${MICROMAMBA_VERSION:-2.9.0}"
MM="$CADLY_TOOLS/bin/micromamba"

# Behind the Claude Code agent proxy, TLS is re-terminated; trust its CA.
if [[ -f /root/.ccr/ca-bundle.crt ]]; then
    export SSL_CERT_FILE="${SSL_CERT_FILE:-/root/.ccr/ca-bundle.crt}"
    export REQUESTS_CA_BUNDLE="${REQUESTS_CA_BUNDLE:-/root/.ccr/ca-bundle.crt}"
fi

mkdir -p "$CADLY_TOOLS"
if [[ ! -x "$MM" ]]; then
    echo "==> Downloading micromamba $MICROMAMBA_VERSION from conda-forge"
    tmp="$(mktemp -d)"
    curl -fsSL --retry 4 -o "$tmp/mm.tar.bz2" \
        "https://conda.anaconda.org/conda-forge/linux-64/micromamba-${MICROMAMBA_VERSION}-0.tar.bz2"
    tar -xjf "$tmp/mm.tar.bz2" -C "$CADLY_TOOLS" bin/micromamba
    rm -rf "$tmp"
fi

export MAMBA_ROOT_PREFIX="$CADLY_TOOLS/mamba"
if [[ -d "$CADLY_ENV/conda-meta" ]]; then
    echo "==> Updating environment at $CADLY_ENV"
    "$MM" install -y -p "$CADLY_ENV" -f "$ROOT/env/environment.yml"
else
    echo "==> Creating environment at $CADLY_ENV"
    "$MM" create -y -p "$CADLY_ENV" -f "$ROOT/env/environment.yml"
fi

# Headless display for running the app and its tests.
if ! command -v Xvfb >/dev/null 2>&1; then
    echo "==> Installing Xvfb + Mesa (apt)"
    if command -v apt-get >/dev/null 2>&1; then
        SUDO=""; [[ "$(id -u)" != 0 ]] && SUDO="sudo"
        $SUDO apt-get update -qq
        $SUDO apt-get install -y -qq xvfb libgl1-mesa-dri libglx-mesa0 libegl-mesa0
    else
        echo "warning: Xvfb not found and apt-get unavailable; app tests need an X server" >&2
    fi
fi

echo "==> Done. Next:  source scripts/env.sh && cmake --preset linux-conda && cmake --build --preset linux-conda"

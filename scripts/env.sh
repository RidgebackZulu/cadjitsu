# Source this file to use the conda build environment created by bootstrap_env.sh:
#   source scripts/env.sh
# A toolchain set up before the rename to Cadjitsu (/opt/cadly-tools) still works.
if [[ -z "${CADJITSU_TOOLS:-}" && ! -d /opt/cadjitsu-tools && -d /opt/cadly-tools ]]; then
    CADJITSU_TOOLS=/opt/cadly-tools
fi
export CADJITSU_TOOLS="${CADJITSU_TOOLS:-/opt/cadjitsu-tools}"
export CADJITSU_ENV="${CADJITSU_ENV:-$CADJITSU_TOOLS/env}"
export PATH="$CADJITSU_ENV/bin:$PATH"
export CC="$CADJITSU_ENV/bin/x86_64-conda-linux-gnu-gcc"
export CXX="$CADJITSU_ENV/bin/x86_64-conda-linux-gnu-g++"
export CCACHE_DIR="${CCACHE_DIR:-$CADJITSU_TOOLS/ccache}"

# Source this file to use the conda build environment created by bootstrap_env.sh:
#   source scripts/env.sh
export CADLY_TOOLS="${CADLY_TOOLS:-/opt/cadly-tools}"
export CADLY_ENV="${CADLY_ENV:-$CADLY_TOOLS/env}"
export PATH="$CADLY_ENV/bin:$PATH"
export CC="$CADLY_ENV/bin/x86_64-conda-linux-gnu-gcc"
export CXX="$CADLY_ENV/bin/x86_64-conda-linux-gnu-g++"
export CCACHE_DIR="${CCACHE_DIR:-$CADLY_TOOLS/ccache}"

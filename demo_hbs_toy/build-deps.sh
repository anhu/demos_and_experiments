#!/bin/bash
# Build the wolfSSL library and the wolfCLU command line tool that every
# script in stateful/ and stateless/ depends on.
#
#     ./build-deps.sh
#
# Everything is cloned and installed underneath build/ in this directory --
# nothing is installed system-wide and nothing needs root.
#
# Override the locations if you want them elsewhere:
#     BUILD_DIR=/somewhere/src PREFIX=/somewhere/install ./build-deps.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${HERE}/build}"
PREFIX="${PREFIX:-${BUILD_DIR}/install}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

WOLFSSL_REPO="https://github.com/wolfssl/wolfssl.git"
WOLFCLU_REPO="https://github.com/wolfssl/wolfCLU.git"

# ---------------------------------------------------------------------------
# Prerequisites.  Note we look for libtoolize, not libtool: autogen.sh calls
# libtoolize, and some distributions ship it without the libtool wrapper.
# ---------------------------------------------------------------------------
missing=""
for tool in git autoconf automake libtoolize make gcc; do
    command -v "$tool" >/dev/null 2>&1 || missing="${missing} ${tool}"
done
if [ -n "$missing" ]; then
    echo "missing build tools:${missing}" >&2
    echo "on Debian/Ubuntu:  sudo apt install git autoconf automake libtool make gcc" >&2
    exit 1
fi

echo "source   : ${BUILD_DIR}"
echo "install  : ${PREFIX}"
echo "parallel : ${JOBS}"
mkdir -p "$BUILD_DIR"

clone() {   # clone $1 into $2 unless it is already there
    if [ -d "${BUILD_DIR}/$2/.git" ]; then
        echo "== $2 already cloned, skipping =="
    else
        echo "== cloning $2 =="
        git clone --depth 1 "$1" "${BUILD_DIR}/$2"
    fi
}

clone "$WOLFSSL_REPO" wolfssl
clone "$WOLFCLU_REPO" wolfCLU

# ---------------------------------------------------------------------------
# wolfSSL.  --enable-wolfclu turns on the algorithms and APIs wolfCLU needs.
# ---------------------------------------------------------------------------
echo "== building wolfssl =="
cd "${BUILD_DIR}/wolfssl"
./autogen.sh
./configure --prefix="$PREFIX" --enable-wolfclu
make -j"$JOBS"
make install

# ---------------------------------------------------------------------------
# wolfCLU, linked against the wolfSSL we just installed.
# ---------------------------------------------------------------------------
echo "== building wolfCLU =="
cd "${BUILD_DIR}/wolfCLU"
export PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
./autogen.sh
./configure --prefix="$PREFIX" --with-wolfssl="$PREFIX"
make -j"$JOBS"
make install

# ---------------------------------------------------------------------------
# Check the tool actually runs and hashes correctly.  wolfCLU wants a dash on
# the algorithm name: "-hash -sha256", not "-hash sha256".
# ---------------------------------------------------------------------------
export PATH="${PREFIX}/bin:$PATH"
export LD_LIBRARY_PATH="${PREFIX}/lib:${LD_LIBRARY_PATH:-}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
printf 'abc' > "${tmp}/abc.txt"
got=$(wolfssl -hash -sha256 -in "${tmp}/abc.txt")
want="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
if [ "$got" != "$want" ]; then
    echo "self-test FAILED: SHA-256(\"abc\") = ${got}" >&2
    echo "                       expected = ${want}" >&2
    exit 1
fi

echo
echo "=============================================================="
echo "done -- SHA-256(\"abc\") matches the published test vector"
echo "=============================================================="
wolfssl version | head -1
echo
echo "wolfssl installed at: ${PREFIX}/bin/wolfssl"
echo
echo "To use it in your shell:"
echo "    export PATH=\"${PREFIX}/bin:\$PATH\""
echo "    export LD_LIBRARY_PATH=\"${PREFIX}/lib:\$LD_LIBRARY_PATH\""
echo
echo "The demo scripts find it automatically:"
echo "    ./stateful/demo.sh"
echo "    ./stateless/demo.sh"
echo

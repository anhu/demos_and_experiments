#!/bin/bash
# Build both DTLS 1.3 handshake benchmarks.
#
# Expects two library trees to already exist (see README.md):
#   ../openssl        OpenSSL master, built  (asm on by default)
#   ../wolfssl-bench  wolfSSL, built WITH Intel asm / AES-NI / SP-asm
#
# The wolfSSL tree must be the asm-enabled one. Building against a plain
# ./configure --enable-dtls13 tree compares wolfSSL's portable C against
# OpenSSL's hand-written assembly, which is not a fair comparison.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "$HERE")"
OSSL="${OSSL:-$TOP/openssl}"
WOLF="${WOLF:-$TOP/wolfssl-bench}"

[ -f "$OSSL/libssl.so" ] || [ -f "$OSSL/libssl.a" ] || {
    echo "error: no built OpenSSL at $OSSL (see README.md: Building and running)" >&2; exit 1; }
[ -f "$WOLF/src/.libs/libwolfssl.a" ] || {
    echo "error: no built wolfSSL at $WOLF (see README.md: Building and running)" >&2; exit 1; }

# Refuse to build against a wolfSSL tree without acceleration - that would
# silently produce an unfair result.
if ! grep -m1 '^AM_CFLAGS = ' "$WOLF/Makefile" | grep -q WOLFSSL_AESNI; then
    echo "error: $WOLF was built WITHOUT AES-NI/Intel asm." >&2
    echo "       Rebuild it per README.md, or the comparison is invalid." >&2
    exit 1
fi

# The test certificate and key are COPIED FROM THE PINNED wolfSSL TREE rather
# than committed to this directory. Two reasons: they would otherwise be the
# only thing here that a fresh clone is missing (run.sh takes them as
# arguments), and ecc-key.pem is a private key, which has no business sitting
# in a git repo even as a well-known test key. Copying also guarantees they
# match whatever commit $WOLF is pinned to.
echo "==> test cert + key (from $WOLF/certs)"
for f in server-ecc.pem ecc-key.pem; do
    [ -f "$WOLF/certs/$f" ] || {
        echo "error: $WOLF/certs/$f missing; is $WOLF a complete wolfSSL tree?" >&2
        exit 1; }
    cp -f "$WOLF/certs/$f" "$HERE/$f"
done

echo "==> bench_openssl  (against $OSSL)"
gcc -O2 -o "$HERE/bench_openssl" "$HERE/bench_openssl.c" \
    -I"$OSSL/include" -L"$OSSL" -lssl -lcrypto

echo "==> bench_wolfssl  (against $WOLF)"
gcc -O2 -o "$HERE/bench_wolfssl" "$HERE/bench_wolfssl.c" \
    -I"$WOLF" "$WOLF/src/.libs/libwolfssl.a" -lm

echo
echo "Built. Run ./run.sh"

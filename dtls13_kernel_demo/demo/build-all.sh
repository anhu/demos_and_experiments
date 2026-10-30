#!/bin/bash
# Build everything: the demo kernel module, the userspace programs that run
# inside the guest, the host-side client, and the initramfs.
#
# Assumes the prerequisites in README.md "Building and running" are done: both
# wolfSSL trees configured and built, and demo/kernel/vmlinuz in place.
# Safe to re-run.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "$HERE")"
KTREE="$TOP/wolfssl"          # configured --enable-linuxkm --enable-dtls13
UTREE="$TOP/wolfssl-user"     # configured --enable-dtls13 --enable-static
LIBA="$UTREE/src/.libs/libwolfssl.a"

fail() { echo "error: $*" >&2; exit 1; }

[ -f "$KTREE/linuxkm/libwolfssl.ko" ] || fail "missing $KTREE/linuxkm/libwolfssl.ko (see README.md: Building and running)"
[ -f "$LIBA" ]                        || fail "missing $LIBA (see README.md: Building and running)"
[ -f "$HERE/kernel/vmlinuz" ]         || fail "missing $HERE/kernel/vmlinuz (see README.md: Building and running)"

echo "==> embedding the ECC cert/key (kernel build has NO_FILESYSTEM)"
C="$KTREE/certs"
{
    echo "/* Auto-generated from wolfssl/certs by build-all.sh. Do not edit. */"
    echo "#ifndef DEMO_CERTS_H"
    echo "#define DEMO_CERTS_H"
    xxd -i -n server_ecc_der < "$C/server-ecc.der" | sed 's/unsigned/static const unsigned/'
    xxd -i -n ecc_key_der    < "$C/ecc-key.der"    | sed 's/unsigned/static const unsigned/'
    echo "#endif"
} > "$HERE/kmod/certs_data.h"

echo "==> building wolfdtls_srv.ko"
make -C "$HERE/kmod" >/dev/null
[ -f "$HERE/kmod/wolfdtls_srv.ko" ] || fail "module build produced no .ko"

echo "==> building guest userspace programs"
# NOTE: dtls_srv_user must be DYNAMIC. A statically linked glibc does not use
# the vDSO getrandom() fast path (measured 490 ns static vs 65 ns dynamic),
# which would silently cripple the comparison in Part 3.2.
gcc -O2 -o "$HERE/userland/dtls_srv_user" "$HERE/userland/dtls_srv_user.c" \
    -I"$UTREE" "$LIBA" -lm
gcc -O2 -o "$HERE/userland/relayd"  "$HERE/userland/relayd.c"
gcc -O2 -o "$HERE/userland/keyscan" "$HERE/userland/keyscan.c"

echo "==> building host clients"
gcc -O2 -o "$HERE/userland/dtls_bench_client" "$HERE/userland/dtls_bench_client.c" \
    -I"$UTREE" "$LIBA" -lm
gcc -O2 -o "$HERE/userland/dtls_hs_bench" "$HERE/userland/dtls_hs_bench.c" \
    -I"$UTREE" "$LIBA" -lm

echo "==> building initramfs"
"$HERE/mkinitramfs.sh"

echo
echo "Build complete. Run any of:"
echo "  demo/demo.sh        DTLS 1.3 handshake, host client -> in-kernel server"
echo "  demo/bench.sh       entropy comparison (kernel / urandom / vDSO)"
echo "  demo/isolation.sh   key-extraction attack, lockdown off then on"
echo "  demo/relay.sh       per-message handoff cost + its decomposition"

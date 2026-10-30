#!/bin/bash
# One-command end-to-end demo:
#   1. boot the QEMU guest, which loads libwolfssl.ko + wolfdtls_srv.ko
#   2. wait for the in-kernel DTLS 1.3 server to start listening
#   3. run the userspace DTLS 1.3 client here on the host against it
#   4. print both sides and shut the guest down
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "$HERE")"
USERTREE="$TOP/wolfssl-user"
CLIENT="$USERTREE/examples/client/client"
# The client example locates its cert files relative to the wolfSSL source
# root, so it must be run from there ("wolf root not found" otherwise).
CA="./certs/ca-ecc-cert.pem"
PORT="${PORT:-11111}"
VMLOG="$(mktemp -t dtlsdemo-vm.XXXXXX.log)"

[ -x "$CLIENT" ] || { echo "error: $CLIENT not built" >&2; exit 1; }

cleanup() { [ -n "${VMPID:-}" ] && kill -9 "$VMPID" 2>/dev/null; }
trap cleanup EXIT

echo "### booting guest VM (in-kernel DTLS 1.3 server) ..."
"$HERE/run-vm.sh" < /dev/null > "$VMLOG" 2>&1 &
VMPID=$!

for _ in $(seq 1 40); do
    sleep 2
    grep -qa "waiting for DTLS 1.3 handshake" "$VMLOG" && break
done

if ! grep -qa "waiting for DTLS 1.3 handshake" "$VMLOG"; then
    echo "error: guest never reached the listening state. Console log:" >&2
    tail -40 "$VMLOG" >&2
    exit 1
fi

echo "### guest is up. In-kernel server state:"
grep -a "wolfdtls_srv\|libwolfssl" "$VMLOG" | tail -5
echo

echo "### running host userspace DTLS 1.3 client -> guest kernel ..."
( cd "$USERTREE" && timeout 40 "$CLIENT" -u -v 4 -h 127.0.0.1 -p "$PORT" -A "$CA" )
rc=$?
echo "### client exit=$rc"
echo

sleep 2
echo "### guest kernel log (server side of the same handshake):"
grep -a "wolfdtls_srv" "$VMLOG" \
  | grep -aE "client connected|HANDSHAKE OK|recv |session closed" | tail -6

exit $rc

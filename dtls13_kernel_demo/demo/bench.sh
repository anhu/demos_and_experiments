#!/bin/bash
# Entropy benchmark: in-kernel vs userspace, all three servers in the SAME
# QEMU guest so the only variable is where the entropy comes from.
#
#   UDP 11111  kernel module   get_random_bytes()
#   UDP 11112  userspace       read(2) on /dev/urandom
#   UDP 11113  userspace       getrandom()  (vDSO on this kernel)
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT="$HERE/userland/dtls_bench_client"
ITERS="${ITERS:-200000}"
TRIALS="${TRIALS:-3}"
VMLOG="$(mktemp -t dtlsbench-vm.XXXXXX.log)"

[ -x "$CLIENT" ] || { echo "error: build demo/userland first" >&2; exit 1; }

cleanup() { [ -n "${VMPID:-}" ] && kill -9 "$VMPID" 2>/dev/null; }
trap cleanup EXIT

echo "### booting guest (kernel + 2 userspace DTLS 1.3 servers) ..."
"$HERE/run-vm.sh" < /dev/null > "$VMLOG" 2>&1 &
VMPID=$!

for _ in $(seq 1 40); do
    sleep 2
    grep -qa "listening on UDP 0.0.0.0:11113" "$VMLOG" && break
done
grep -qa "listening on UDP 0.0.0.0:11113" "$VMLOG" || {
    echo "error: guest servers never came up" >&2; tail -30 "$VMLOG" >&2; exit 1; }

echo "### guest up. ${ITERS} entropy fetches of 32 bytes, ${TRIALS} trials."
echo

for t in $(seq 1 "$TRIALS"); do
    echo "--- trial $t ---"
    for p in 11111 11112 11113; do
        "$CLIENT" -p "$p" -r "BENCH $ITERS" 2>/dev/null \
          | grep "reply:" | sed 's/^ *reply: */  /'
    done
done

echo
echo "### end-to-end round trip for a single 32-byte entropy request"
echo "### (this is what a naive 'is the kernel faster?' demo would measure)"
for p in 11111 11112; do
    printf "  port %s: " "$p"
    "$CLIENT" -p "$p" -r "ENT 32" 2>/dev/null | grep -o "round_trip_ns=[0-9]*"
done

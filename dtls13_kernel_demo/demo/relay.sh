#!/bin/bash
# Boundary-crossing cost for kernel-originated data: in-kernel DTLS vs handing
# each message out to a userspace daemon and back.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLIENT="$HERE/userland/dtls_bench_client"
MSGS="${MSGS:-20000}"
VMLOG="$(mktemp -t dtlsrelay.XXXXXX.log)"

cleanup() { [ -n "${VMPID:-}" ] && kill -9 "$VMPID" 2>/dev/null; }
trap cleanup EXIT

echo "### booting guest (kernel DTLS server + userspace relay daemon) ..."
"$HERE/run-vm.sh" < /dev/null > "$VMLOG" 2>&1 &
VMPID=$!
for _ in $(seq 1 40); do
    sleep 2; grep -qa "in-kernel  DTLS 1.3 server" "$VMLOG" && break
done

echo "### $MSGS messages of 1 KiB, three trials"
for t in 1 2 3; do
    "$CLIENT" -p 11111 -r "RELAY $MSGS" 2>/dev/null \
      | grep "reply:" | sed 's/^ *reply: */  /'
done

echo
echo "### context switches, self-reported by the userspace daemon:"
grep -a "relayd: msgs" "$VMLOG" | tail -3 | sed 's/^/  /'

#!/bin/bash
# Key-isolation demo: boot the guest twice, once with kernel lockdown off and
# once with lockdown=confidentiality, and try to steal the DTLS private key
# out of each server in turn.
#
# Both servers use the SAME P-256 key. The only difference is where it lives.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

run_once() {
    local mode="$1" log
    log="$(mktemp -t dtlsiso.XXXXXX.log)"

    echo "################################################################"
    if [ "$mode" = none ]; then
        echo "# BOOT 1 of 2: kernel lockdown OFF (default)"
        LOCKDOWN="" "$HERE/run-vm.sh" < /dev/null > "$log" 2>&1 &
    else
        echo "# BOOT 2 of 2: kernel lockdown = $mode"
        LOCKDOWN="$mode" "$HERE/run-vm.sh" < /dev/null > "$log" 2>&1 &
    fi
    echo "################################################################"
    local pid=$!

    for _ in $(seq 1 40); do
        sleep 2
        grep -qa "in-kernel  DTLS 1.3 server" "$log" && break
    done

    sed -n '/KEY ISOLATION TEST/,/^  in-kernel/p' "$log" \
      | grep -av "^  in-kernel" | sed 's/\r//'

    kill -9 $pid 2>/dev/null
    sleep 1
    echo
}

run_once none
run_once confidentiality

cat <<'EOF'
################################################################
# READ THIS BEFORE QUOTING THE RESULT
################################################################
The userspace key is recoverable in BOTH boots. It sits in the process's
.rodata and again on its heap after parsing, reachable by anything that can
open /proc/<pid>/mem -- a same-UID process, a ptrace-capable debugger, or
root. It also lands in core dumps and can be paged to swap.

The kernel key's reachability DEPENDS ON LOCKDOWN:
  lockdown=none            -> recoverable by root via /proc/kcore
  lockdown=confidentiality -> /proc/kcore is closed; not recoverable this way

So in-kernel key storage is not magic. What it buys is a change in the
privilege required to steal the key: from "any code running as the server's
user" to "kernel-level access". That is a real and useful step up, but it is
only airtight when the kernel is locked down.
EOF

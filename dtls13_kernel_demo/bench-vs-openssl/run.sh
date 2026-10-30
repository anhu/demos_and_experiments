#!/bin/bash
# Run the DTLS 1.3 full-handshake comparison.
#
#   ./run.sh              # fair comparison: both pinned to P-256
#   ./run.sh unpinned     # shows the confound: each library's own defaults
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "$HERE")"
export LD_LIBRARY_PATH="${OSSL:-$TOP/openssl}"
N="${N:-200}"
TRIALS="${TRIALS:-3}"
MODE="${1:-pinned}"

port() { echo $((14500 + RANDOM % 400)); }

if [ "$MODE" = unpinned ]; then
    cat <<'NOTE'
### UNPINNED - each library uses its own default group.
### This is NOT a valid comparison, and is included only to show why.
### The two libraries pick different key exchanges of different strength.

NOTE
    p=$(port)
    "$HERE/bench_openssl" "$N" "$HERE/server-ecc.pem" "$HERE/ecc-key.pem" "$p"
    "$HERE/bench_wolfssl" "$N" "$HERE/server-ecc.pem" "$HERE/ecc-key.pem" "$((p+1))"
    exit 0
fi

echo "### DTLS 1.3 full handshakes, both pinned to P-256 / AES-256-GCM-SHA384"
echo "### $N handshakes per run (first 10% discarded as warm-up), $TRIALS trials"
echo
for t in $(seq 1 "$TRIALS"); do
    p=$(port)
    "$HERE/bench_openssl" "$N" "$HERE/server-ecc.pem" "$HERE/ecc-key.pem" "$p"      P-256 2>/dev/null
    "$HERE/bench_wolfssl" "$N" "$HERE/server-ecc.pem" "$HERE/ecc-key.pem" "$((p+1))" P-256 2>/dev/null
done
echo
echo "### negotiated parameters (proof both sides used the same ones):"
p=$(port)
"$HERE/bench_openssl" 12 "$HERE/server-ecc.pem" "$HERE/ecc-key.pem" "$p"       P-256 2>&1 >/dev/null | head -1
"$HERE/bench_wolfssl" 12 "$HERE/server-ecc.pem" "$HERE/ecc-key.pem" "$((p+1))" P-256 2>&1 >/dev/null | head -1

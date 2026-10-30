#!/bin/bash
# Toy STATELESS hash-based signature scheme -- verify.
#
# usage: ./verify.sh <signed-file> <signature> [public-key-hex]
#
# Needs only the public key, not key/.  If no public key is given it is read
# from key/root.txt for convenience.
#
# The verifier recomputes the leaf numbers from the message itself, so a
# signature only checks out against the message that selected those leaves.

MSG="$1"
SIG="$2"
PUB="$3"

if [ -z "$MSG" ] || [ ! -f "$MSG" ] || [ -z "$SIG" ] || [ ! -f "$SIG" ]; then
    echo "usage: $0 <signed-file> <signature> [public-key-hex]" >&2
    exit 1
fi
if [ -z "$PUB" ]; then
    PUB=$(cat "$(dirname "$0")/key/root.txt")
fi

K=8
LEVELS=4

declare -A S
while IFS='=' read -r k v; do
    S["$k"]="$v"
done < "$SIG"

DIGEST=$(wolfssl -hash -sha256 -in "$MSG")

# The DIGEST line in the signature is only a convenience: the digest we trust
# is the one we just computed.  Cross-check it so a mismatched field cannot sit
# in a signature unnoticed.
if [ -n "${S[DIGEST]}" ] && [ "${S[DIGEST]}" != "$DIGEST" ]; then
    echo "VERIFY FAILED: DIGEST in the signature does not match the file" >&2
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

: > "${TMP}/all_roots.txt"
for j in $(seq 1 $K); do
    # Leaf number comes from the message, not from the signature.
    IDX=$((16#${DIGEST:$((j - 1)):1}))

    if [ -n "${S[IDX_${j}]}" ] && [ "${S[IDX_${j}]}" != "$IDX" ]; then
        echo "VERIFY FAILED: tree ${j} IDX does not match the message" >&2
        exit 1
    fi

    SECRET="${S[SECRET_${j}]}"
    if [ -z "$SECRET" ]; then
        echo "VERIFY FAILED: signature missing SECRET_${j}" >&2
        exit 1
    fi

    # Leaf value = hash of the secret file (hex plus a newline, as keygen wrote it).
    printf '%s\n' "$SECRET" > "${TMP}/secret.txt"
    CUR=$(wolfssl -hash -sha256 -in "${TMP}/secret.txt")

    # Climb to this tree's root.  Whether we are the left or right child at
    # each level comes from the leaf number.
    for L in $(seq 0 $((LEVELS - 1))); do
        SIBLING="${S[AUTH_${j}_${L}]}"
        if [ -z "$SIBLING" ]; then
            echo "VERIFY FAILED: signature missing AUTH_${j}_${L}" >&2
            exit 1
        fi
        if [ $(( (IDX >> L) & 1 )) -eq 0 ]; then
            echo -n "${CUR}${SIBLING}" > "${TMP}/concat.txt"   # we are the left child
        else
            echo -n "${SIBLING}${CUR}" > "${TMP}/concat.txt"
        fi
        CUR=$(wolfssl -hash -sha256 -in "${TMP}/concat.txt")
    done

    printf '%s\n' "$CUR" >> "${TMP}/all_roots.txt"
done

COMPUTED=$(wolfssl -hash -sha256 -in "${TMP}/all_roots.txt")

if [ "$COMPUTED" = "$PUB" ]; then
    echo "VERIFY OK"
    exit 0
else
    echo "VERIFY FAILED: computed root ${COMPUTED} != public key ${PUB}" >&2
    exit 1
fi

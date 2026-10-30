#!/bin/bash
# usage: ./sign.sh <file-to-sign> [signature-out]

MSG="$1"
SIG="${2:-signature.txt}"

if [ -z "$MSG" ] || [ ! -f "$MSG" ]; then
    echo "usage: $0 <file-to-sign> [signature-out]" >&2
    exit 1
fi

INDEX=$(cat key/index)
if [ "$INDEX" -gt 16 ]; then
    echo "key exhausted: all 16 one-time keys have been used" >&2
    exit 1
fi
ROOT=$(cat key/root.txt)

# Walk from the root down to leaf number INDEX-1, collecting the sibling hash
# at each of the 4 branches.  Bit 3 of the leaf number chooses at the root and
# bit 0 at the last branch: 0 = go LEFT, 1 = go RIGHT.
N=$((INDEX - 1))
NODE="key/${ROOT}"
AUTH=()
for b in 3 2 1 0; do
    LEFT=$(sed -n 's/^LEFT=//p' "${NODE}/NODE.txt")
    RIGHT=$(sed -n 's/^RIGHT=//p' "${NODE}/NODE.txt")
    if [ $(( (N >> b) & 1 )) -eq 0 ]; then
        AUTH+=("R:${RIGHT}")
        NODE="${NODE}/${LEFT}"
    else
        AUTH+=("L:${LEFT}")
        NODE="${NODE}/${RIGHT}"
    fi
done
LEAF=$(basename "$NODE")

# 256-bit message digest, expanded to one character per bit.
DIGEST=$(wolfssl -hash -sha256 -in "$MSG")
BITS=""
for k in $(seq 0 63); do
    case "${DIGEST:$k:1}" in
        0) BITS+="0000";; 1) BITS+="0001";; 2) BITS+="0010";; 3) BITS+="0011";;
        4) BITS+="0100";; 5) BITS+="0101";; 6) BITS+="0110";; 7) BITS+="0111";;
        8) BITS+="1000";; 9) BITS+="1001";; a) BITS+="1010";; b) BITS+="1011";;
        c) BITS+="1100";; d) BITS+="1101";; e) BITS+="1110";; f) BITS+="1111";;
        *) echo "bad digest character" >&2; exit 1;;
    esac
done

# For each bit position reveal the secret matching the bit value. The verifier
# needs all 512 public values to rebuild the leaf hash.
{
    echo "INDEX=${INDEX}"
    echo "LEAF=${LEAF}"
    for i in $(seq 1 256); do
        BIT="${BITS:$((i-1)):1}"
        OTHER=$((1 - BIT))
        echo "SECRET_${i}=$(cat "${NODE}/${i}_${BIT}_ots_secret.txt")"
        echo "OTHER_${i}=$(cat "${NODE}/${i}_${OTHER}_ots_public.txt")"
    done
    # Auth path, written bottom-up: AUTH_1 is the leaf's sibling.
    for j in 3 2 1 0; do
        echo "AUTH_$((4 - j))=${AUTH[$j]}"
    done
} > "$SIG"

# Advance the state.  This is what makes the scheme stateful.
echo -n "$((INDEX + 1))" > key/index

echo "signed with one-time key ${INDEX} of 16 -> ${SIG}"
echo "next unused index: $((INDEX + 1))"

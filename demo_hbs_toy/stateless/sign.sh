#!/bin/bash
# Toy STATELESS hash-based signature scheme -- sign.
#
# usage: ./sign.sh <file-to-sign> [signature-out]
#
# Nothing is read or written except the key and the signature: there is no
# index file and no state to advance.  Signing the same file twice produces
# byte-identical signatures.
#
# The first 8 hex characters of the message digest choose the leaves.  Hex
# character j is a number 0..15 and selects which of tree j's 16 leaves to
# open, so exactly one secret per tree is revealed.
#
# Each tree is stored as nested directories named after their hashes, so a leaf
# is reached by walking down from the root reading NODE.txt at each level.

MSG="$1"
SIG="${2:-signature.txt}"

if [ -z "$MSG" ] || [ ! -f "$MSG" ]; then
    echo "usage: $0 <file-to-sign> [signature-out]" >&2
    exit 1
fi

cd "$(dirname "$0")" 2>/dev/null
K=8             # trees
LEVELS=4        # levels to climb in each tree

# The forest lives in a directory named after the public key.
PUB=$(cat key/root.txt)
FOREST="key/${PUB}"

DIGEST=$(wolfssl -hash -sha256 -in "$MSG")

{
    echo "DIGEST=${DIGEST}"
    for j in $(seq 1 $K); do
        TREE="${FOREST}/tree_${j}"

        # Hex character j-1 of the digest is this tree's leaf number.
        IDX=$((16#${DIGEST:$((j - 1)):1}))
        echo "IDX_${j}=${IDX}"

        # Walk from this tree's root down to leaf IDX, collecting the sibling
        # hash at each of the 4 branches.  Bit 3 of the leaf number chooses at
        # the root and bit 0 at the last branch: 0 = go LEFT, 1 = go RIGHT.
        NODE="${TREE}/$(basename "$(find "$TREE" -mindepth 1 -maxdepth 1 -type d)")"
        AUTH=()
        for b in $(seq $((LEVELS - 1)) -1 0); do
            LEFT=$(sed -n 's/^LEFT=//p' "${NODE}/NODE.txt")
            RIGHT=$(sed -n 's/^RIGHT=//p' "${NODE}/NODE.txt")
            if [ $(( (IDX >> b) & 1 )) -eq 0 ]; then
                AUTH+=("${RIGHT}")     # we went left, so the sibling is on the right
                NODE="${NODE}/${LEFT}"
            else
                AUTH+=("${LEFT}")
                NODE="${NODE}/${RIGHT}"
            fi
        done

        echo "SECRET_${j}=$(cat "${NODE}/secret.txt")"

        # AUTH was collected top-down; the signature lists it bottom-up, so
        # AUTH_j_0 is the leaf's own sibling.
        for L in $(seq 0 $((LEVELS - 1))); do
            echo "AUTH_${j}_${L}=${AUTH[$((LEVELS - 1 - L))]}"
        done
    done
} > "$SIG"

echo "signed -> ${SIG}"
echo "leaves opened (tree:leaf): $(for j in $(seq 1 $K); do printf '%d:%d ' $j $((16#${DIGEST:$((j-1)):1})); done)"

#!/bin/bash
# Toy STATELESS hash-based signature scheme -- key generation.
#
# Based on FORS ("Forest Of Random Subsets"), the few-time signature inside
# SLH-DSA, stripped down to its simplest possible form.
#
# There are K = 8 independent Merkle trees, each with T = 16 leaves.  Every
# leaf hides one random secret; the leaf value is the hash of that secret.
#
# Every node of every tree is a DIRECTORY NAMED AFTER ITS OWN HASH, with its
# two children inside it and a NODE.txt recording which child is LEFT and which
# is RIGHT -- the same layout the stateful scheme uses.  A leaf directory holds
# the secret itself.
#
# The public key is the hash of the 8 tree roots, and it names the directory
# that holds the whole forest.  It is an 8-ary node: one parent, eight children.
# Order matters (the roots are hashed in tree order), so those eight children
# keep their positional names tree_1 ... tree_8.

K=8             # number of trees
T=16            # leaves per tree
LEVELS=4        # log2(T): 16 -> 8 -> 4 -> 2 -> 1

cd "$(dirname "$0")"

rm -rf key
mkdir -p key

for j in $(seq 1 $K); do
    TREE="key/tree_${j}"
    mkdir -p "$TREE"

    # --- leaves: one random secret each, in a directory named after its hash ---
    DIRS=()
    for i in $(seq 0 $((T - 1))); do
        wolfssl rand -out _tmp_secret.bin 32
        xxd -p -c 32 _tmp_secret.bin > _tmp_secret.txt
        rm -f _tmp_secret.bin
        LEAF_HASH=$(wolfssl -hash -sha256 -in _tmp_secret.txt)
        mkdir -p "${TREE}/${LEAF_HASH}"
        mv _tmp_secret.txt "${TREE}/${LEAF_HASH}/secret.txt"
        DIRS+=("${LEAF_HASH}")
    done

    # --- hash the leaves pairwise up the tree ---
    COUNT=$T
    while [ "$COUNT" -gt 1 ]; do
        PARENTS=()
        for i in $(seq 0 2 $((COUNT - 2))); do
            LEFT="${DIRS[$i]}"
            RIGHT="${DIRS[$((i + 1))]}"
            echo -n "${LEFT}${RIGHT}" > _tmp_concat.txt
            PARENT_HASH=$(wolfssl -hash -sha256 -in _tmp_concat.txt)
            rm -f _tmp_concat.txt
            mkdir -p "${TREE}/${PARENT_HASH}"
            mv "${TREE}/${LEFT}" "${TREE}/${RIGHT}" "${TREE}/${PARENT_HASH}/"
            echo "LEFT=${LEFT}"   > "${TREE}/${PARENT_HASH}/NODE.txt"
            echo "RIGHT=${RIGHT}" >> "${TREE}/${PARENT_HASH}/NODE.txt"
            PARENTS+=("${PARENT_HASH}")
        done
        DIRS=("${PARENTS[@]}")
        COUNT=${#DIRS[@]}
    done
    # tree_j now contains exactly one directory: this tree's root.
done

# --- public key: hash of all K tree roots, in tree order 1..K ---
: > key/all_roots.txt
for j in $(seq 1 $K); do
    basename "$(find "key/tree_${j}" -mindepth 1 -maxdepth 1 -type d)" >> key/all_roots.txt
done
PUB=$(wolfssl -hash -sha256 -in key/all_roots.txt)

# --- file the forest under a directory named after the public key ---
mkdir -p "key/${PUB}"
mv key/all_roots.txt "key/${PUB}/"
for j in $(seq 1 $K); do
    mv "key/tree_${j}" "key/${PUB}/"
done
echo -n "$PUB" > key/root.txt

echo "public key (FORS root): ${PUB}"
echo "${K} trees x ${T} leaves -- no state file, nothing to update when signing"

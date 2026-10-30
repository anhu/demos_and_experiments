#!/bin/bash
# Walk through the toy STATELESS (FORS) scheme end to end, showing the values
# at each step.  Regenerates key/ from scratch, so any existing key is discarded.

cd "$(dirname "$0")"

# Use the locally built wolfCLU if wolfssl is not already on PATH.
if ! command -v wolfssl >/dev/null 2>&1; then
    export PATH="$(cd .. && pwd)/build/install/bin:$PATH"
    export LD_LIBRARY_PATH="$(cd .. && pwd)/build/install/lib:$LD_LIBRARY_PATH"
fi
if ! command -v wolfssl >/dev/null 2>&1; then
    echo "wolfssl (wolfCLU) not found: build it into ../build/install first" >&2
    exit 1
fi

say() { echo; echo "=============================================================="; echo "$1"; echo "=============================================================="; }
pause() { [ -n "$DEMO_PAUSE" ] && read -rp "  [enter] "; }

say "1. KEY GENERATION"
cat <<'TXT'
FORS = "Forest Of Random Subsets".  Instead of one Merkle tree, there are 8
independent little trees, each with 16 leaves.  Every leaf hides one secret.
The public key is the hash of all 8 tree roots.
TXT
echo
time ./keygen.sh
pause

say "2. WHAT THE KEY LOOKS LIKE ON DISK"
PUB=$(cat key/root.txt)
FOREST="key/${PUB}"
echo "Every node is a directory named after its own hash, with its two children"
echo "inside it:"
echo
echo "    key/${PUB}/"
echo "        all_roots.txt      the 8 roots, in tree order; this is what was hashed"
echo "        tree_1/ .. tree_8/ one per tree; each holds a single root directory"
echo
ROOT1=$(basename "$(find "${FOREST}/tree_1" -mindepth 1 -maxdepth 1 -type d)")
echo "  tree_1's root node:"
echo "    ${ROOT1}"
echo "        NODE.txt      $(tr '\n' ' ' < "${FOREST}/tree_1/${ROOT1}/NODE.txt")"
echo "        <LEFT hash>/  <RIGHT hash>/   ... 4 levels down to 16 leaves"
echo
LEAF=$(find "${FOREST}/tree_1" -mindepth 5 -maxdepth 5 -type d | head -1)
echo "  A leaf directory holds nothing but the secret:"
echo "    $(basename "$LEAF")/secret.txt"
echo "      secret    : $(cat "${LEAF}/secret.txt")"
echo "      its hash  : $(wolfssl -hash -sha256 -in "${LEAF}/secret.txt")"
echo "                  ^ which is the directory's own name"
echo
echo "  tree_1 root  : ${ROOT1}"
echo "  public key   : ${PUB}"
echo "                 = SHA256 of all 8 roots concatenated"
pause

say "3. THE MESSAGE PICKS THE LEAVES"
echo -n "The quick brown fox jumps over the lazy dog" > msg.txt
echo "  message : $(cat msg.txt)"
DIGEST=$(wolfssl -hash -sha256 -in msg.txt)
echo "  digest  : $DIGEST"
echo "            ^^^^^^^^ only the first 8 hex characters matter (one per tree)"
echo
cat <<'TXT'
There are 8 trees, so 8 hex characters are used; one per tree.  Each hex
character is a number 0-15, and each tree has 16 leaves, so character j simply
names which leaf of tree j to open.  This is the whole trick: the message
chooses the subset, so no counter is needed.
TXT
echo
printf "    %-6s %-8s %-6s\n" "tree" "hex char" "leaf"
for j in $(seq 1 8); do
    C="${DIGEST:$((j-1)):1}"
    printf "    %-6s %-8s %-6s\n" "$j" "$C" "$((16#$C))"
done
pause

say "4. SIGNING"
echo "  Note what signing does NOT do: no index file, nothing written to key/."
echo
BEFORE=$(find key -type f | wc -l)
./sign.sh msg.txt sig1.txt
AFTER=$(find key -type f -newer msg.txt | wc -l)
echo
echo "  key/ files modified by signing: ${AFTER}   (out of ${BEFORE})"
echo
echo "Signature contents ($(wc -l < sig1.txt) lines, $(du -h sig1.txt | cut -f1)):"
head -7 sig1.txt | sed 's/^/    /'
echo "    ...  (one revealed secret + a 4-node auth path, per tree)"
pause

say "5. VERIFYING"
cat <<'TXT'
The verifier recomputes the leaf numbers FROM THE MESSAGE; it does not trust
the ones in the signature.  For each tree it hashes the revealed secret into a
leaf, climbs the auth path to that tree's root, then hashes all 8 recomputed
roots together and compares to the public key.
TXT
echo
./verify.sh msg.txt sig1.txt
pause

say "DONE"
echo "Files left behind: msg.txt sig1.txt"
echo "Run ./keygen.sh to start over with a fresh key."
echo

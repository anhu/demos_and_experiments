#!/bin/bash
# Walk through the toy STATEFUL scheme end to end, showing the values at each
# step.  Regenerates key/ from scratch, so any existing key is discarded.

cd "$(dirname "$0")"

# Use the locally built wolfCLU if wolfssl is not already on PATH.
if ! command -v wolfssl >/dev/null 2>&1; then
    export PATH="$(cd .. && pwd)/build/install/bin:$PATH"
    export LD_LIBRARY_PATH="$(cd .. && pwd)/build/install/lib:$LD_LIBRARY_PATH"
fi
if ! command -v wolfssl >/dev/null 2>&1; then
    echo "wolfssl (wolfCLU) not found -- build it into ../build/install first" >&2
    exit 1
fi

say() { echo; echo "=============================================================="; echo "$1"; echo "=============================================================="; }
pause() { [ -n "$DEMO_PAUSE" ] && read -rp "  [enter] "; }

say "1. KEY GENERATION"
cat <<'TXT'
16 one-time signature keys, each with 256 bit positions and TWO secrets per
position: one revealed if the digest bit is 0, one if it is 1.  Each OTS key
is hashed down to a single leaf value, and the 16 leaves are hashed pairwise
up a Merkle tree.  The root of that tree is the public key.
TXT
echo
time ./keygen.sh
pause

say "2. WHAT THE KEY LOOKS LIKE ON DISK"
echo "The Merkle tree IS the directory tree -- each node is named by its hash:"
echo
find key -maxdepth 3 -type d | head -7 | sed 's|key/|  key/|'
echo "  ... (5 levels deep, 16 leaf directories at the bottom)"
echo
for d in 1 2 3 4 5; do
    printf "  depth %d: %2d node(s)\n" $d $(find key -mindepth $d -maxdepth $d -type d | wc -l)
done
LEAF=$(find key -mindepth 5 -maxdepth 5 -type d | head -1)
echo
echo "One leaf holds 512 secrets + 512 public values:"
ls "$LEAF" | grep -E '^(1|2)_[01]_ots_(secret|public)\.txt$' | sort | sed 's/^/    /'
echo "    ... 1026 files in total"
echo
echo "  secret for bit 1, value 0 : $(cat "$LEAF/1_0_ots_secret.txt")"
echo "  its public value          : $(cat "$LEAF/1_0_ots_public.txt")"
echo "  (public = SHA256 of the secret file -- only the public half is committed to)"
pause

say "3. THE MESSAGE"
echo -n "The quick brown fox jumps over the lazy dog" > msg.txt
echo "  message : $(cat msg.txt)"
DIGEST=$(wolfssl -hash -sha256 -in msg.txt)
echo "  digest  : $DIGEST"
BITS=""
for k in $(seq 0 63); do
    case "${DIGEST:$k:1}" in
        0) BITS+="0000";; 1) BITS+="0001";; 2) BITS+="0010";; 3) BITS+="0011";;
        4) BITS+="0100";; 5) BITS+="0101";; 6) BITS+="0110";; 7) BITS+="0111";;
        8) BITS+="1000";; 9) BITS+="1001";; a) BITS+="1010";; b) BITS+="1011";;
        c) BITS+="1100";; d) BITS+="1101";; e) BITS+="1110";; f) BITS+="1111";;
    esac
done
echo "  bits    : ${BITS:0:64}..."
echo
echo "Each of the 256 bits picks ONE of that position's two secrets:"
for i in 1 2 3 4 5; do
    echo "    bit $i = ${BITS:$((i-1)):1}  ->  reveal ${i}_${BITS:$((i-1)):1}_ots_secret.txt, keep ${i}_$((1 - ${BITS:$((i-1)):1}))_ots_secret.txt hidden"
done
echo "    ..."
pause

say "4. SIGNING (consumes one-time key 1 of 16)"
echo "  key/index before : $(cat key/index)"
echo
./sign.sh msg.txt sig1.txt
echo
echo "  key/index after  : $(cat key/index)  <-- the state advanced"
echo
echo "Signature contents ($(wc -l < sig1.txt) lines, $(du -h sig1.txt | cut -f1)):"
head -4 sig1.txt | sed 's/^/    /'
echo "    ...  (256 revealed secrets, plus the 256 public values we did NOT reveal,"
echo "          which the verifier needs to rebuild the leaf hash)"
echo
echo "The 4 sibling hashes that climb from the leaf to the root:"
grep '^AUTH_' sig1.txt | sed 's/^/    /'
pause

say "5. VERIFYING"
cat <<'TXT'
The verifier has ONLY the public key.  It hashes each revealed secret to get
half the leaf's public values, takes the other half from the signature, hashes
all 512 into the leaf, then climbs the auth path and compares to the root.
TXT
echo
echo "  public key : $(cat key/root.txt)"
echo
./verify.sh msg.txt sig1.txt
pause

say "DONE"
echo "Files left behind: msg.txt sig1.txt"
echo "Run ./keygen.sh to start over with a fresh key."
echo

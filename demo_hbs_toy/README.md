# Toy Hash-Based Signature Schemes

Two deliberately minimal hash-based signature schemes, written in shell and
built on nothing but SHA-256:

| Directory    | Scheme                                  | Loosely modelled on |
|--------------|-----------------------------------------|---------------------|
| `stateful/`  | One-time keys under a Merkle tree       | LMS / XMSS          |
| `stateless/` | FORS — a forest of small Merkle trees   | SLH-DSA             |

Both are **toy examples**, not implementations you should use for anything
real. The parameters are far too small, the private keys sit unencrypted in
plain files and they have been overly simplified for the purpose of clarity.

Hashing is done with the wolfCLU `wolfssl` command line tool, so a "signature"
is a text file and a "key" is a directory tree that implements a merkle tree.

The same tool also generates randomness for the private keys.

---

## Quick start

```bash
./build-deps.sh          # builds wolfSSL + wolfCLU into build/ (no sudo needed)
./stateful/demo.sh       # walks the stateful scheme end to end
./stateless/demo.sh      # walks the stateless scheme end to end
```

The demo scripts add `build/install/bin` to `PATH` automatically. To run the
individual scripts yourself, put the tool on your path first:

```bash
export PATH="$PWD/build/install/bin:$PATH"
export LD_LIBRARY_PATH="$PWD/build/install/lib:$LD_LIBRARY_PATH"
```

---

## Building the dependencies

`build-deps.sh` clones and builds both projects underneath `build/`, installs
them to `build/install`, and self-tests the result against the published
SHA-256 of `abc`. Nothing is installed system-wide and nothing needs root.

```bash
./build-deps.sh
BUILD_DIR=/tmp/src PREFIX=/opt/wolf JOBS=4 ./build-deps.sh   # or override
```

It needs `git`, `autoconf`, `automake`, `libtoolize`, `make` and `gcc`
(Debian/Ubuntu: `sudo apt install git autoconf automake libtool make gcc`) and
skips the clone step if the repositories are already present.

---

## The stateful scheme

**16 signatures per key. The key remembers how many it has used.**

Each of the 16 one-time keys is a Lamport key pair: for each of the 256 bits of
a SHA-256 digest there are **two** secrets, one revealed if that bit is 0 and
one if it is 1. Hashing all 512 public values gives a single leaf, and the 16
leaves are hashed pairwise up a 5-level Merkle tree. The root is the public key.

The Merkle tree *is* the directory tree — every node is a directory named after
its own hash, with its two children inside it:

```
key/
  index                       # next unused one-time key, 1..16
  root.txt                    # the public key
  <root hash>/
    NODE.txt                  # LEFT= / RIGHT= child hashes
    <child hash>/ ...         # 5 levels down to 16 leaves
        1_0_ots_secret.txt    # secret for bit 1 if the bit is 0
        1_0_ots_public.txt    # its SHA-256
        1_1_ots_secret.txt    # secret for bit 1 if the bit is 1
        ...                   # 512 secrets + 512 publics per leaf
        all_public.txt        # all 512 publics; its hash names this directory
```

```bash
cd stateful
./keygen.sh                       # ~40 s, writes key/ and prints the public key
echo -n "hello" > msg.txt
./sign.sh msg.txt sig.txt         # consumes one key, advances key/index
./verify.sh msg.txt sig.txt       # VERIFY OK
./verify.sh msg.txt sig.txt <pubkey-hex>    # public key can be passed explicitly
```

The signature is 518 lines, about 38 KB:

```
INDEX=1                 which one-time key was used
LEAF=<hash>             convenience field, cross-checked by the verifier
SECRET_i=<hex>          the revealed secret for bit i        (256 of these)
OTHER_i=<hex>           the PUBLIC value of the secret we did not reveal
AUTH_1..4=<side>:<hash> sibling hashes from the leaf up to the root
```

`OTHER_i` is there because the leaf commits to all 512 public values but only
256 secrets are revealed. The verifier hashes the revealed secrets to get half
of them and takes the other half from the signature.

**The state is the whole point.** `sign.sh` advances `key/index` and refuses to
run past 16. Signing twice from one index reveals both secrets at some bit
positions, which is what lets an attacker start forging.

---

## The stateless scheme

**No counter, no index file, nothing to update. The message picks the secrets.**

FORS uses a *forest*: 8 independent Merkle trees of 16 leaves each, one secret
per leaf, 128 secrets in total. The public key is the hash of the 8 tree roots.

Signing takes the first 8 hex characters of the message digest. **Position picks
the tree, value picks the leaf** — the j-th digit says which of tree j's 16
leaves to open. A hex digit is 0–15 and each tree has exactly 16 leaves, so no
arithmetic is involved. (Real FORS slices bits out of the digest instead; the
parameters here were chosen to make the mapping readable.)

```
key/
  root.txt                    # convenience copy of the public key
  <public key>/               # directory named after its own hash
      all_roots.txt           # the 8 tree roots in order -- this is what was hashed
      tree_1/ ... tree_8/     # one per tree, each holding a single root directory
          <root hash>/
              NODE.txt        # LEFT= / RIGHT= child hashes
              <child hash>/   # ... 4 levels down
                  <leaf hash>/
                      secret.txt
```

Every node is a directory named after its own hash with its two children inside
it, exactly as in the stateful scheme, and a leaf directory holds nothing but
its secret. The public key is the node above all of it -- an 8-ary node with
eight children instead of two. Those eight keep positional names because the
roots are hashed **in tree order**: reordering them would change the public key,
and a directory listing sorts lexicographically rather than by tree number.
`key/root.txt` is redundant once the directory exists (`ls key/` is the public
key) and is kept only for convenience.

`sign.sh` finds leaf *idx* by walking down from the root, reading `NODE.txt` at
each level and branching on the bits of *idx* (bit 3 at the root, bit 0 at the
last branch), collecting the sibling hash on the way -- the same walk the
stateful `sign.sh` does.

```bash
cd stateless
./keygen.sh                       # ~3 s
echo -n "hello" > msg.txt
./sign.sh msg.txt sig.txt         # writes nothing to key/
./verify.sh msg.txt sig.txt       # VERIFY OK
```

The signature is 49 lines, about 3 KB:

```
DIGEST=<hex>            convenience field, cross-checked by the verifier
IDX_j=<0-15>            leaf opened in tree j  (the verifier recomputes this)
SECRET_j=<hex>          that leaf's secret                    (8 of these)
AUTH_j_0..3=<hash>      sibling hashes from the leaf to tree j's root
```

The verifier derives the leaf numbers from the message rather than trusting the
signature, which is what binds a signature to its message. Signing is
deterministic: the same message always produces a byte-identical signature.

Note that FORS on its own is a *few*-time signature. Each signature opens one
leaf per tree, so opened leaves accumulate across signatures. SLH-DSA solves
this by keeping an enormous number of FORS keys and using a hypertree to
authenticate whichever one a message lands on; this toy has a single FORS key.

---

---

## Layout

```
build-deps.sh          builds wolfSSL + wolfCLU into build/
build/                 cloned sources and the local install prefix
stateful/
  keygen.sh sign.sh verify.sh demo.sh
stateless/
  keygen.sh sign.sh verify.sh demo.sh
```

Running a `demo.sh` leaves a `msg.txt` and `sig1.txt` behind and generates
`key/`, which changes the public key meaning signatures made against an older
key will no longer verify.

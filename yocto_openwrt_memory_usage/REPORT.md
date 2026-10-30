**Contents**

- [TLS RAM usage: low-RAM wolfSSL vs distro OpenSSL on a Yocto QEMU image](#tls-ram-usage-low-ram-wolfssl-vs-distro-openssl-on-a-yocto-qemu-image)
  - [What was built](#what-was-built)
  - [wolfSSL low-RAM configuration](#wolfssl-low-ram-configuration)
  - [Method](#method)
  - [Results](#results)
    - [Headline: peak heap + stack of each TLS endpoint process (valgrind massif)](#headline-peak-heap--stack-of-each-tls-endpoint-process-valgrind-massif)
    - [Server initialisation only (listening, no connection yet)](#server-initialisation-only-listening-no-connection-yet)
    - [Where wolfSSL's 8.1 KiB of heap goes (server, TLS 1.3, at the peak)](#where-wolfssls-81-kib-of-heap-goes-server-tls-13-at-the-peak)
    - [Static footprint of the libraries in the image (size, target build)](#static-footprint-of-the-libraries-in-the-image-size-target-build)
    - [Full table](#full-table)
  - [Notes on interpretation](#notes-on-interpretation)
  - [Reproducing](#reproducing)
  - [Appendix A: steps to reproduce](#appendix-a-steps-to-reproduce)
    - [A.1 Environment used, and what you need](#a1-environment-used-and-what-you-need)
    - [A.2 Pinned sources](#a2-pinned-sources)
    - [A.3 The custom layer meta-tlsram](#a3-the-custom-layer-meta-tlsram)
    - [A.4 Build configuration](#a4-build-configuration)
    - [A.5 Boot the image and run the measurement](#a5-boot-the-image-and-run-the-measurement)
    - [A.6 Reading the numbers without any custom tooling](#a6-reading-the-numbers-without-any-custom-tooling)
    - [A.7 Expected results](#a7-expected-results)
    - [A.8 Static sizes](#a8-static-sizes)
    - [A.9 Fifteen-minute partial reproduction without Yocto](#a9-fifteen-minute-partial-reproduction-without-yocto)
    - [A.10 Repository layout](#a10-repository-layout)
    - [A.11 Reproduction check](#a11-reproduction-check)
    - [A.12 Helper scripts](#a12-helper-scripts)
- [TLS RAM usage: low-RAM wolfSSL vs OpenSSL behind libustream-ssl on an OpenWrt QEMU image](#tls-ram-usage-low-ram-wolfssl-vs-openssl-behind-libustream-ssl-on-an-openwrt-qemu-image)
  - [What was built (OpenWrt)](#what-was-built-openwrt)
  - [wolfSSL low-RAM configuration (OpenWrt)](#wolfssl-low-ram-configuration-openwrt)
    - [Where this differs from the Yocto build](#where-this-differs-from-the-yocto-build)
  - [Method](#method-1)
  - [Results](#results-1)
    - [Headline: peak heap + stack of the uclient-fetch process, TLS 1.3 (3 runs each)](#headline-peak-heap--stack-of-the-uclient-fetch-process-tls-13-3-runs-each)
    - [Detail](#detail)
    - [Static footprint](#static-footprint)
  - [Notes on interpretation](#notes-on-interpretation-1)
  - [Reproducing (OpenWrt)](#reproducing-openwrt)
  - [Appendix B: steps to reproduce (OpenWrt)](#appendix-b-steps-to-reproduce-openwrt)
    - [B.1 Environment used](#b1-environment-used)
    - [B.2 Source changes and configuration](#b2-source-changes-and-configuration)
    - [B.3 Host tools without root: host-tools.sh](#b3-host-tools-without-root-host-toolssh)
    - [B.4 The measurement shim memshim.c](#b4-the-measurement-shim-memshimc)
    - [B.5 Test driver, server policy and command files](#b5-test-driver-server-policy-and-command-files)
    - [B.6 The driver script reproduce.sh](#b6-the-driver-script-reproducesh)
    - [B.7 Reading the numbers](#b7-reading-the-numbers)
    - [B.8 Expected results](#b8-expected-results)
    - [B.9 Reproduction check](#b9-reproduction-check)
    - [B.10 Repository layout (OpenWrt part)](#b10-repository-layout-openwrt-part)

---

# TLS RAM usage: low-RAM wolfSSL vs distro OpenSSL on a Yocto QEMU image

Measured 2026-10-02 in a QEMU (qemux86-64) guest built with Yocto scarthgap. Summary: for an
ECDHE P-256 / ECDSA P-256 / AES-128-GCM connection, the wolfSSL endpoint process peaks at
10–13 KiB of heap+stack while the OpenSSL endpoint process peaks at 750–790 KiB, in both
directions and for both TLS 1.2 and TLS 1.3.

## What was built

| Item | Value |
|---|---|
| Yocto | poky **scarthgap** (5.0.20), `MACHINE=qemux86-64`, `core-image-minimal` based image `tls-ram-image` |
| OpenSSL | **3.5.8**, the scarthgap default recipe, unmodified (`openssl`, `openssl-bin`) |
| wolfSSL | **5.9.4** from `meta-wolfssl` master, rebuilt with the low-RAM configuration below |
| Measurement tools in the image | `valgrind` 3.22 (massif, `--stacks=yes`), GNU `time` 1.9 (`-v`, max RSS), `procps`, dropbear ssh |
| Test driver in the image | `/usr/bin/tls-ram-test` (package `tls-ram-tests`) |
| QEMU | `runqemu ... nographic slirp`, 2 vCPU, 1 GiB, TCG (no KVM access on this host) |

Layers: `poky/meta`, `poky/meta-poky`, `poky/meta-yocto-bsp`, `meta-wolfssl`, and the project layer
`meta-tlsram` (bbappend for wolfSSL, the test recipe and the image recipe).

## wolfSSL low-RAM configuration

`meta-tlsram/recipes-wolfssl/wolfssl/wolfssl_%.bbappend` adds to `EXTRA_OECONF`:

```
--enable-tls13 --disable-oldtls
--disable-rsa --disable-dh --disable-dsa --disable-psk
--disable-sha512 --disable-sha384
--disable-chacha --disable-poly1305 --disable-aescbc
--enable-ecc --disable-curve25519 --disable-ed25519
--disable-mlkem --disable-kyber --disable-dilithium
--enable-sp-math --enable-sp=smallec256 --disable-sp-asm
--enable-smallstack --enable-lowresource --enable-maxfragment
--enable-singlethreaded
CFLAGS += -DECC_USER_CURVES
```

Why each one matters for RAM in an ECDHE/ECDSA connection (wolfSSL manual ch. 2 "Build options",
tuning guide "decrease record size / remove unneeded features / choose smaller keys"):

- `--enable-sp-math --enable-sp=smallec256`: restricted single-precision math with only the P-256
  code, so `SP_INT_BITS` is 256. Every `mp_int`, `ecc_point` and `ecc_key` inside the `WOLFSSL`
  object and on the heap is sized for 256-bit numbers instead of 4096/8192-bit ones. `small` picks
  the compact point-multiplication variants (fewer precomputed points).
- `--disable-rsa --disable-dh --disable-dsa --disable-psk`: nothing else can enlarge the big-number
  type or add key material to the session.
- `--disable-sha384 --disable-sha512`: the handshake hash and TLS 1.3 key schedule keep only a SHA-256
  state; the only suites left are `TLS_AES_128_GCM_SHA256` / `ECDHE-ECDSA-AES128-GCM-SHA256`.
- `--disable-chacha --disable-poly1305 --disable-aescbc`, `--disable-curve25519/ed25519/mlkem/
  dilithium`: no extra cipher contexts or key-share groups (a default 5.9 build offers an ML-KEM
  hybrid key share, which alone costs over 1 KiB in the ClientHello).
- `--enable-smallstack` (`WOLFSSL_SMALL_STACK`): large temporaries go to the heap and are freed
  immediately, so the stack high-water mark stays small and the memory is not held for the whole
  connection.
- `--enable-lowresource`: `NO_SESSION_CACHE` (no static session table), `WOLFSSL_SMALL_CERT_VERIFY`
  (verify the peer certificate signature without a second decoded copy), `GCM_SMALL` and small AES
  tables (no per-context GCM lookup table), `WOLFSSL_NO_ASYNC_IO`.
- `--enable-maxfragment`: RFC 6066 max_fragment_length; the client asks for 512-byte records so
  record buffers never need to grow to 16 KiB. The server side honours a peer's request automatically.
- `--enable-singlethreaded`: no mutexes inside library objects.
- `--disable-oldtls`: TLS 1.2 and 1.3 only.
- `-DECC_USER_CURVES`: curve table contains only SECP256R1.

The bbappend also installs the example `client`/`server` as `/usr/bin/wolfssl-client` and
`/usr/bin/wolfssl-server`, plus the ECC test certificates under `/usr/share/wolfssl/certs`.

## Method

All connections: ECDHE P-256 key exchange, ECDSA P-256 server certificate (`server-ecc.pem`
signed by `ca-ecc-cert.pem`), AES-128-GCM-SHA256, server authentication only, client verifies
the server certificate, one application-data exchange, then an orderly shutdown.

| Scenario | Server | Client |
|---|---|---|
| `tls13_wolfserver_osslclient` | `wolfssl-server -v 4 -l TLS13-AES128-GCM-SHA256 -d` | `openssl s_client -tls1_3 ... -maxfraglen 512` |
| `tls13_osslserver_wolfclient` | `openssl s_server -tls1_3 -www -naccept 1` | `wolfssl-client -v 4 -F 1 -x -g` |
| `tls12_wolfserver_osslclient` | `wolfssl-server -v 3 -l ECDHE-ECDSA-AES128-GCM-SHA256 -d` | `openssl s_client -tls1_2 -cipher ECDHE-ECDSA-AES128-GCM-SHA256 -maxfraglen 512` |
| `tls12_osslserver_wolfclient` | `openssl s_server -tls1_2 -cipher ECDHE-ECDSA-AES128-GCM-SHA256 -www -naccept 1` | `wolfssl-client -v 3 -l ECDHE-ECDSA-AES128-GCM-SHA256 -F 1 -x -g` |
| `*_nomfl` | as the TLS 1.3 rows | without `-maxfraglen` / `-F 1` |

Each scenario is run twice inside the guest:

1. both processes under `valgrind --tool=massif --stacks=yes --peak-inaccuracy=0.1`, which records
   heap bytes requested (`mem_heap_B`), allocator overhead (`mem_heap_extra_B`) and stack bytes
   (`mem_stacks_B`) at every snapshot. "Peak total" is the snapshot with the largest sum.
2. both processes under `/usr/bin/time -v` (no instrumentation) for the maximum resident set size.

Full command lines are in `meta-tlsram/recipes-tlsram/tls-ram-tests/files/tls-ram-test.sh`;
raw outputs (`*.massif`, `*.time`, logs) are in `results/`.

## Results

Measured inside the QEMU guest on 2026-10-02 (`results/SUMMARY.md`, raw files in `results/`).
All 12 connection runs and both baseline runs completed with exit status 0; every connection
negotiated SECP256R1 and `TLS_AES_128_GCM_SHA256` (TLS 1.3) or
`TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256` (TLS 1.2) with the server certificate verified.

### Headline: peak heap + stack of each TLS endpoint process (valgrind massif)

| Connection | wolfSSL process | OpenSSL process |
|---|---|---|
| TLS 1.3, **wolfSSL server** / OpenSSL client | **10.5 KiB** (7.9 heap + 0.4 overhead + 2.1 stack) | 787.4 KiB (663.6 + 118.8 + 5.0) |
| TLS 1.3, OpenSSL server / **wolfSSL client** | **12.8 KiB** (8.9 + 0.5 + 3.3) | 757.4 KiB (635.0 + 117.6 + 4.8) |
| TLS 1.2, **wolfSSL server** / OpenSSL client | **10.4 KiB** (8.0 + 0.4 + 2.0) | 767.5 KiB (643.9 + 117.8 + 5.8) |
| TLS 1.2, OpenSSL server / **wolfSSL client** | **11.8 KiB** (8.0 + 0.4 + 3.3) | 753.2 KiB (630.5 + 118.0 + 4.7) |

Maximum stack depth seen at any point (massif `--stacks=yes`): wolfSSL 2.7 KiB (server) and
3.3 KiB (client); OpenSSL 7.7–8.0 KiB.

Maximum resident set size of the whole process (GNU `time -v`, no valgrind, includes code pages
of all shared libraries touched): wolfSSL 2.0–2.3 MiB, OpenSSL 8.7–9.1 MiB.

### Server initialisation only (listening, no connection yet)

| Server | Peak heap+stack before any connection | Peak with one connection | Added by the connection |
|---|---|---|---|
| wolfSSL `wolfssl-server` (TLS 1.3) | 10.2 KiB (7.4 heap) | 10.5 KiB | ≈ 0.3 KiB at the peak instant |
| OpenSSL `s_server` (TLS 1.3) | 636.4 KiB (514.1 heap) | 757.4 KiB | ≈ 121 KiB |

The wolfSSL example server creates its `WOLFSSL` object (1,280 B) before `accept()`, so it is
already included in the "before" figure; the handshake itself only adds short-lived temporaries
(the largest is 744 B for the ECDSA signature) because `WOLFSSL_SMALL_STACK` allocations are
freed as soon as each step completes.

### Where wolfSSL's 8.1 KiB of heap goes (server, TLS 1.3, at the peak)

| Allocation | Bytes |
|---|---|
| `WOLFSSL` session object (`wolfSSL_new`) | 1,280 |
| Server certificate DER (`wolfSSL_CTX_use_certificate_chain_file`) | 955 |
| Private key DER (`wolfSSL_CTX_use_PrivateKey_file`) | 199 |
| ECDSA sign temporaries (`wc_ecc_sign_hash`, during CertificateVerify) | 744 |
| Everything else (`WOLFSSL_CTX`, certificate manager, record buffers ≤ 512 B, key share, hash and cipher states, ~60 small blocks) | ≈ 4,950 |

OpenSSL's heap at its peak is 597 KiB of `CRYPTO_malloc` plus a 32 KiB aligned block inside
`libcrypto`, i.e. almost entirely library initialisation (default provider, method stores,
error strings, config) and `s_server`'s own 16 KiB I/O buffers; the per-connection part is the
≈ 121 KiB difference above.

### Static footprint of the libraries in the image (`size`, target build)

| File | text | data | bss |
|---|---|---|---|
| libwolfssl.so.46.0.0 | 496,491 | 5,072 | 244 |
| libssl.so.3 | 984,149 | 53,484 | 40 |
| libcrypto.so.3 | 5,574,809 | 518,944 | 12,712 |
| wolfssl-server / wolfssl-client | 34,892 / 39,172 | 2,992 / 3,080 | 60 / 4,172 |
| openssl (s_server/s_client binary) | 969,429 | 111,472 | 18,584 |

So the writable static RAM (data + bss) is about 5 KiB for libwolfssl versus about 585 KiB for
libssl + libcrypto; this is on top of the heap/stack figures above, which massif does not count.

### Full table

MFL = max_fragment_length extension requested by the client (512 bytes).

| Scenario | TLS | MFL | Role | Library | Peak total (KiB) | Heap at peak (KiB) | Heap extra (KiB) | Stack at peak (KiB) | Max heap (KiB) | Max stack (KiB) | Max RSS (KiB) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| tls13_wolfserver_osslclient | TLS 1.3 | MFL 512 | server | wolfSSL | 10.5 | 7.9 | 0.4 | 2.1 | 8.1 | 2.7 | 2048 |
| tls13_wolfserver_osslclient | TLS 1.3 | MFL 512 | client | OpenSSL | 787.4 | 663.6 | 118.8 | 5.0 | 663.6 | 7.8 | 9088 |
| tls13_osslserver_wolfclient | TLS 1.3 | MFL 512 | server | OpenSSL | 757.4 | 635.0 | 117.6 | 4.8 | 635.0 | 7.9 | 8832 |
| tls13_osslserver_wolfclient | TLS 1.3 | MFL 512 | client | wolfSSL | 12.8 | 8.9 | 0.5 | 3.3 | 8.9 | 3.3 | 2048 |
| tls12_wolfserver_osslclient | TLS 1.2 | MFL 512 | server | wolfSSL | 10.4 | 8.0 | 0.4 | 2.0 | 8.1 | 2.7 | 2176 |
| tls12_wolfserver_osslclient | TLS 1.2 | MFL 512 | client | OpenSSL | 767.5 | 643.9 | 117.8 | 5.8 | 643.9 | 7.7 | 8832 |
| tls12_osslserver_wolfclient | TLS 1.2 | MFL 512 | server | OpenSSL | 753.2 | 630.5 | 118.0 | 4.7 | 630.5 | 7.9 | 8704 |
| tls12_osslserver_wolfclient | TLS 1.2 | MFL 512 | client | wolfSSL | 11.8 | 8.0 | 0.4 | 3.3 | 8.0 | 3.3 | 2304 |
| tls13_wolfserver_osslclient_nomfl | TLS 1.3 | no MFL | server | wolfSSL | 10.4 | 7.9 | 0.4 | 2.1 | 8.0 | 2.7 | 2048 |
| tls13_wolfserver_osslclient_nomfl | TLS 1.3 | no MFL | client | OpenSSL | 787.4 | 663.6 | 118.8 | 5.0 | 663.6 | 7.8 | 8960 |
| tls13_osslserver_wolfclient_nomfl | TLS 1.3 | no MFL | server | OpenSSL | 772.2 | 649.9 | 117.5 | 4.8 | 649.9 | 8.0 | 8704 |
| tls13_osslserver_wolfclient_nomfl | TLS 1.3 | no MFL | client | wolfSSL | 12.5 | 8.7 | 0.4 | 3.3 | 8.7 | 3.3 | 2176 |
| baseline_wolfserver | none | init only | server | wolfSSL | 10.2 | 7.4 | 0.2 | 2.7 | 7.4 | 3.9 | - |
| baseline_osslserver | none | init only | server | OpenSSL | 636.4 | 514.1 | 114.7 | 7.6 | 514.1 | 8.4 | - |

Column meaning: "Peak total" is the massif snapshot with the largest heap + heap-extra + stack;
"Heap at peak" is bytes requested by the program at that instant; "Heap extra" is allocator
overhead (massif's 8 B/block estimate); "Max heap" / "Max stack" are the individual maxima over
all snapshots; "Max RSS" is from `/usr/bin/time -v`.

## Notes on interpretation

- **Both sides are the libraries' own demo programs.** `wolfssl-client`/`wolfssl-server` are the
  wolfSSL example applications; `openssl s_client`/`s_server` are the OpenSSL command line tools,
  which read `openssl.cnf`, load the default provider, build method stores and allocate 16 KiB I/O
  buffers before the handshake. The OpenSSL figures therefore describe "OpenSSL as shipped by the
  distro plus its demo app", not the smallest possible OpenSSL embedding. The init-only baselines
  (10.2 KiB vs 636.4 KiB) separate that fixed cost from the per-connection cost (≈0.3 KiB vs
  ≈121 KiB at the peak).
- **TLS 1.2 vs TLS 1.3 makes no practical difference** for the wolfSSL endpoints (10.4 vs 10.5 KiB
  server, 11.8 vs 12.8 KiB client).
- **Max fragment length.** With these small application payloads the no-MFL runs are within
  0.3 KiB of the MFL runs for wolfSSL, because wolfSSL only grows its record buffers to the size
  of the record actually received. MFL matters as a worst-case bound: without it a peer may send
  a 16 KiB record and the receive buffer must grow to match. OpenSSL did honour the extension:
  `s_server` peaked 15 KiB lower when the wolfSSL client requested MFL 512 (757.4 vs 772.2 KiB).
- **What massif counts.** `mem_heap_B` is malloc/calloc/realloc bytes requested, `mem_heap_extra_B`
  is estimated allocator overhead, `mem_stacks_B` is stack growth below the stack pointer at
  process start (dynamic loader included, same for both libraries). Code, `.data`/`.bss` and
  mmap'd pages are not included; the static-footprint table and the RSS column cover those.
- **RSS** (2 MiB vs 9 MiB) is dominated by the code pages of the shared libraries touched
  (libwolfssl 0.5 MB vs libcrypto 6 MB + libssl 1 MB), not by the TLS state.
- **The wolfSSL build is deliberately narrow:** P-256 only, SHA-256 only, AES-128-GCM only,
  no RSA/DH/PSK, no session cache, no X25519/ML-KEM. Peers must support
  `TLS_AES_128_GCM_SHA256` or `ECDHE-ECDSA-AES128-GCM-SHA256` with secp256r1 and an ECDSA P-256
  certificate chain. Re-enabling features (e.g. SHA-384 suites, RSA peers, session resumption)
  will raise the per-connection numbers; `--enable-sp-math` with P-256 only is the single largest
  contributor to the small `WOLFSSL`/key structures.
- **QEMU runs under TCG** (no KVM access on the build host). This only affects run time, not the
  memory figures.
- Two build-environment issues were found and handled: the wolfSSL example binaries must run
  from a directory containing `certs/dh2048.pem` (installed under `/usr/share/wolfssl`), and GNU
  `time` 1.9 cross-configured in scarthgap reports max RSS 0 unless `ac_cv_func_wait3_rusage=yes`
  is seeded (`meta-tlsram/recipes-extended/time/time_%.bbappend`).

## Reproducing

`$WORK` is the working directory described in A.1 (any empty directory holding the files from
Appendix A):

```
cd $WORK
source poky/oe-init-build-env build
bitbake tls-ram-image qemu-helper-native
cd .. && tools/run_qemu_tests.sh results        # boots QEMU, runs tls-ram-test over ssh, copies results
tools/parse_results.py results --md             # summary table
tools/compare_results.py results-published results   # peaks of a rerun vs the committed raw files (A.11)
```

(`results-published` is a copy of the committed `results/` made before rerunning; the committed
files are the first run, `results/REPRODUCTION.md` the comparison with the clean rebuild.)

Inside the guest the test can also be run by hand: `tls-ram-test /root/results [scenario ...]`.

A complete procedure for a fresh machine, with pinned sources and every custom file reproduced
verbatim, is in Appendix A. A 15-minute partial reproduction that needs no Yocto build is in A.9.

## Appendix A: steps to reproduce

### A.1 Environment used, and what you need

| | Used here | Needed |
|---|---|---|
| Host OS | Ubuntu 24.04.4 LTS, x86-64, 8 cores, 15 GB RAM | any Yocto-supported Linux; 8+ cores and 16 GB RAM recommended |
| Host packages | the Yocto scarthgap host dependency list (gawk, wget, git, diffstat, unzip, texinfo, gcc, g++, build-essential, chrpath, socat, cpio, python3, python3-pip, python3-pexpect, xz-utils, debianutils, iputils-ping, python3-git, python3-jinja2, python3-subunit, zstd, liblz4-tool, file, locales, libacl1) | same; `locale -a` must list `en_US.utf8` |
| Disk | 57 GB in `build/tmp`, 7 GB downloads, 5 GB sstate | about 70 GB free |
| Build time | about 2.5 h from source (the public sstate mirror did not match this configuration) | |
| KVM | not available (QEMU ran with TCG) | not needed; TCG only slows the run, it does not change the memory numbers |
| Network | needed for the first build (source downloads) | |

Everything below assumes a working directory `$WORK`, the root of the project: any empty directory
in which the files of this appendix are saved under the names given. `./setup.sh` (A.12) performs
A.2 and A.4 automatically.

### A.2 Pinned sources

| Component | Version / commit |
|---|---|
| poky | branch `scarthgap`, commit `3a3d07f625aee7b87ea1026526b756447c357652` (Yocto 5.0.20) |
| meta-wolfssl | branch `master`, commit `e029e80b743fb3096ccb494161144ae3888e1a48` (2026-09-29) |
| wolfSSL | 5.9.4, git `3c5eead44904df64e6a5a1f4ebdce377d35a849a` (pinned by meta-wolfssl's `wolfssl_5.9.4.bb`) |
| OpenSSL | 3.5.8 (poky `meta/recipes-connectivity/openssl/openssl_3.5.8.bb`, default options) |
| valgrind | 3.22.0 (poky `valgrind_3.22.0.bb`) |
| GNU time | 1.9 (poky `time_1.9.bb`) plus the one-line bbappend in A.4 |
| MACHINE | `qemux86-64`, kernel 6.6.151-yocto-standard, glibc |

```
mkdir -p $WORK && cd $WORK
git clone -b scarthgap https://git.yoctoproject.org/poky && git -C poky checkout 3a3d07f625aee7b87ea1026526b756447c357652
git clone https://github.com/wolfSSL/meta-wolfssl.git && git -C meta-wolfssl checkout e029e80b743fb3096ccb494161144ae3888e1a48
```

### A.3 The custom layer `meta-tlsram`

Create the directory tree below under `$WORK/meta-tlsram` with these contents. The listings are
functionally identical to the files that were built; the originals in this repository's
`meta-tlsram/` differ only in explanatory comments and `DESCRIPTION` strings.

```
meta-tlsram/
  conf/layer.conf
  recipes-wolfssl/wolfssl/wolfssl_%.bbappend
  recipes-core/images/tls-ram-image.bb
  recipes-tlsram/tls-ram-tests/tls-ram-tests.bb
  recipes-tlsram/tls-ram-tests/files/tls-ram-test.sh
  recipes-extended/time/time_%.bbappend
```

`conf/layer.conf`:

```
BBPATH .= ":${LAYERDIR}"
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb ${LAYERDIR}/recipes-*/*/*.bbappend"
BBFILE_COLLECTIONS += "tlsram"
BBFILE_PATTERN_tlsram = "^${LAYERDIR}/"
BBFILE_PRIORITY_tlsram = "10"
LAYERSERIES_COMPAT_tlsram = "scarthgap"
LAYERDEPENDS_tlsram = "core wolfssl"
```

`recipes-wolfssl/wolfssl/wolfssl_%.bbappend` (the low-RAM configuration; comments shortened):

```
EXTRA_OECONF += " \
    --enable-tls13 --disable-oldtls \
    --disable-rsa --disable-dh --disable-dsa --disable-psk \
    --disable-sha512 --disable-sha384 \
    --disable-chacha --disable-poly1305 --disable-aescbc \
    --enable-ecc --disable-curve25519 --disable-ed25519 \
    --disable-mlkem --disable-kyber --disable-dilithium \
    --enable-sp-math --enable-sp=smallec256 --disable-sp-asm \
    --enable-smallstack --enable-lowresource --enable-maxfragment \
    --enable-singlethreaded \
"
CFLAGS:append = " -DECC_USER_CURVES"

# Install the example client/server (built but not installed by wolfSSL's
# makefiles) and the ECC test certificates they need.
do_install:append() {
    install -d ${D}${bindir}
    for ex in client server; do
        if [ -x ${B}/examples/$ex/.libs/$ex ]; then
            install -m 0755 ${B}/examples/$ex/.libs/$ex ${D}${bindir}/wolfssl-$ex
        else
            install -m 0755 ${B}/examples/$ex/$ex ${D}${bindir}/wolfssl-$ex
        fi
    done
    # The examples locate the "wolfSSL root" by probing ./certs/dh2048.pem from
    # the current directory, so run them with cwd=${datadir}/wolfssl.
    install -d ${D}${datadir}/wolfssl/certs
    for c in server-ecc.pem ecc-key.pem ca-ecc-cert.pem client-ecc-cert.pem ecc-client-key.pem dh2048.pem; do
        install -m 0644 ${S}/certs/$c ${D}${datadir}/wolfssl/certs/
    done
}

PACKAGES =+ "${PN}-examples"
FILES:${PN}-examples = "${bindir}/wolfssl-client ${bindir}/wolfssl-server ${datadir}/wolfssl/certs"
RDEPENDS:${PN}-examples += "${PN}"
```

`recipes-core/images/tls-ram-image.bb`:

```
SUMMARY = "QEMU image to compare TLS RAM use: wolfSSL (low-RAM build) vs OpenSSL"
require recipes-core/images/core-image-minimal.bb

IMAGE_INSTALL += " \
    wolfssl wolfssl-examples \
    openssl openssl-bin \
    valgrind time procps \
    tls-ram-tests \
"
IMAGE_FEATURES += "ssh-server-dropbear"
# room for valgrind outputs / results (KiB)
IMAGE_ROOTFS_EXTRA_SPACE = "131072"
```

`recipes-tlsram/tls-ram-tests/tls-ram-tests.bb`:

```
SUMMARY = "Scripts to measure heap/stack use of wolfSSL vs OpenSSL TLS endpoints"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://tls-ram-test.sh"
S = "${WORKDIR}"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/tls-ram-test.sh ${D}${bindir}/tls-ram-test
}

RDEPENDS:${PN} = "wolfssl-examples openssl-bin valgrind time procps"
```

`recipes-extended/time/time_%.bbappend` (without this, GNU time reports a max RSS of 0 on
scarthgap because the cross-configure cannot run the wait3/rusage test):

```
CACHED_CONFIGUREVARS += "ac_cv_func_wait3_rusage=yes"
```

`recipes-tlsram/tls-ram-tests/files/tls-ram-test.sh` (the measurement script that runs inside
the guest; this is the file that produced every number in this report):

```
#!/bin/sh
# tls-ram-test: measure heap and stack use of a TLS endpoint process
# (wolfSSL example client/server vs OpenSSL s_client/s_server) with
# valgrind massif (--stacks=yes) and GNU time (max RSS).
#
# Usage: tls-ram-test [results-dir] [scenario ...]
set -u
RES=${1:-/root/results}
[ $# -gt 0 ] && shift
mkdir -p "$RES"

CERTS=${TLSRAM_CERTS:-/usr/share/wolfssl/certs}
SRV_CERT=$CERTS/server-ecc.pem
SRV_KEY=$CERTS/ecc-key.pem
CA_CERT=$CERTS/ca-ecc-cert.pem
# every server instance gets its own port (PORT_BASE+n) so a lingering
# socket from the previous run can never interfere
PORT_BASE=${TLSRAM_PORT:-11111}
PORT=$PORT_BASE
PORT_HEX=$(printf '%04X' $PORT)
RUN_N=0

# wolfSSL's example client/server probe ./certs/dh2048.pem to find their
# "root" directory, so run everything from the parent of the certs dir.
cd "$CERTS/.." || exit 1

MASSIF_OPTS="--tool=massif --stacks=yes --peak-inaccuracy=0.1 --max-snapshots=200 --detailed-freq=1000000"

log() { echo "[$(date +%H:%M:%S)] $*"; }

listening() {
    # local port in LISTEN state (0A) on IPv4 or IPv6 (remote addr is 8 or 32 hex zeros)
    grep -qsE ":$PORT_HEX 0+:0000 0A" /proc/net/tcp /proc/net/tcp6
}

wait_listen() {
    # wait until something listens on $PORT (state 0A = LISTEN), up to ~300 s,
    # giving up early if the server process ($1) has already exited
    i=0
    while [ $i -lt 3000 ]; do
        listening && return 0
        if ! kill -0 "$1" 2>&1; then
            log "ERROR: server process $1 exited before listening"
            return 1
        fi
        sleep 0.1
        i=$((i+1))
    done
    log "ERROR: nothing listening on port $PORT"
    return 1
}

wait_closed() {
    # wait until no listener remains on $PORT
    i=0
    while [ $i -lt 600 ]; do
        listening || return 0
        sleep 0.1
        i=$((i+1))
    done
    return 0
}

# run_case NAME "SERVER_CMD" "CLIENT_CMD" "CLIENT_STDIN"  (@PORT@ is substituted)
run_case() {
    name=$1; cli_in=$4
    for mode in massif time; do
        RUN_N=$((RUN_N+1))
        PORT=$((PORT_BASE+RUN_N))
        PORT_HEX=$(printf '%04X' $PORT)
        srv=$(printf '%s' "$2" | sed "s/@PORT@/$PORT/g")
        cli=$(printf '%s' "$3" | sed "s/@PORT@/$PORT/g")
        log "=== $name [$mode] port $PORT"
        if [ $mode = massif ]; then
            SW="valgrind $MASSIF_OPTS --massif-out-file=$RES/$name.server.massif"
            CW="valgrind $MASSIF_OPTS --massif-out-file=$RES/$name.client.massif"
        else
            SW="/usr/bin/time -v -o $RES/$name.server.time"
            CW="/usr/bin/time -v -o $RES/$name.client.time"
        fi
        wait_closed
        $SW $srv > "$RES/$name.server.$mode.log" 2>&1 &
        spid=$!
        if ! wait_listen $spid; then
            kill $spid 2>&1
            wait $spid
            echo "$name $mode server_exit=NOLISTEN client_exit=skipped" >> "$RES/exit_codes.txt"
            continue
        fi
        printf '%s' "$cli_in" | $CW $cli > "$RES/$name.client.$mode.log" 2>&1
        crc=$?
        wait $spid
        src=$?
        log "    server exit=$src client exit=$crc"
        echo "$name $mode server_exit=$src client_exit=$crc" >> "$RES/exit_codes.txt"
    done
}

# ---- command lines -------------------------------------------------------
WOLF_SRV13="wolfssl-server -v 4 -l TLS13-AES128-GCM-SHA256 -c $SRV_CERT -k $SRV_KEY -d -p @PORT@"
WOLF_SRV12="wolfssl-server -v 3 -l ECDHE-ECDSA-AES128-GCM-SHA256 -c $SRV_CERT -k $SRV_KEY -d -p @PORT@"
OSSL_CLI13="openssl s_client -connect 127.0.0.1:@PORT@ -tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256 -groups P-256 -sigalgs ecdsa_secp256r1_sha256 -CAfile $CA_CERT -verify_return_error -ign_eof"
OSSL_CLI12="openssl s_client -connect 127.0.0.1:@PORT@ -tls1_2 -cipher ECDHE-ECDSA-AES128-GCM-SHA256 -groups P-256 -sigalgs ecdsa_secp256r1_sha256 -CAfile $CA_CERT -verify_return_error -ign_eof"
OSSL_SRV13="openssl s_server -accept @PORT@ -cert $SRV_CERT -key $SRV_KEY -tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256 -groups P-256 -sigalgs ecdsa_secp256r1_sha256 -naccept 1 -www"
OSSL_SRV12="openssl s_server -accept @PORT@ -cert $SRV_CERT -key $SRV_KEY -tls1_2 -cipher ECDHE-ECDSA-AES128-GCM-SHA256 -groups P-256 -sigalgs ecdsa_secp256r1_sha256 -naccept 1 -www"
WOLF_CLI13="wolfssl-client -h 127.0.0.1 -p @PORT@ -v 4 -l TLS13-AES128-GCM-SHA256 -A $CA_CERT -x -g"
WOLF_CLI12="wolfssl-client -h 127.0.0.1 -p @PORT@ -v 3 -l ECDHE-ECDSA-AES128-GCM-SHA256 -A $CA_CERT -x -g"
MSG='hello from openssl
'

# run_baseline NAME "SERVER_CMD": start the server under massif, wait until it
# listens, then terminate it -> memory of library + application initialisation
# without any connection (valgrind still writes the massif file on SIGTERM).
run_baseline() {
    name=$1
    RUN_N=$((RUN_N+1))
    PORT=$((PORT_BASE+RUN_N))
    PORT_HEX=$(printf '%04X' $PORT)
    srv=$(printf '%s' "$2" | sed "s/@PORT@/$PORT/g")
    log "=== $name [massif, no connection] port $PORT"
    wait_closed
    valgrind $MASSIF_OPTS --massif-out-file=$RES/$name.server.massif $srv > "$RES/$name.server.massif.log" 2>&1 &
    spid=$!
    if wait_listen $spid; then
        sleep 1
        kill -TERM $spid
    fi
    wait $spid
    src=$?
    log "    server exit=$src (terminated on purpose)"
    echo "$name massif server_exit=$src client_exit=none" >> "$RES/exit_codes.txt"
}

ALL="tls13_wolfserver_osslclient tls13_osslserver_wolfclient tls12_wolfserver_osslclient tls12_osslserver_wolfclient tls13_wolfserver_osslclient_nomfl tls13_osslserver_wolfclient_nomfl baseline_wolfserver baseline_osslserver"
SEL="$*"
[ -z "$SEL" ] && SEL="$ALL"

{
    echo "date: $(date)"
    echo "uname: $(uname -a)"
    echo "openssl: $(openssl version)"
    echo "wolfssl: $(wolfssl-server -? 2>&1 | head -n 1)"
    echo "valgrind: $(valgrind --version)"
    for f in /usr/lib/libwolfssl.so.* /usr/lib/libssl.so.* /usr/lib/libcrypto.so.*; do
        [ -f "$f" ] && [ ! -L "$f" ] && echo "lib: $(stat -c '%s %n' "$f")"
    done
    grep -E "MemTotal|MemFree" /proc/meminfo
} > "$RES/sysinfo.txt"
cat "$RES/sysinfo.txt"
: > "$RES/exit_codes.txt"

for s in $SEL; do
    case $s in
    tls13_wolfserver_osslclient)
        run_case $s "$WOLF_SRV13" "$OSSL_CLI13 -maxfraglen 512" "$MSG" ;;
    tls13_osslserver_wolfclient)
        run_case $s "$OSSL_SRV13" "$WOLF_CLI13 -F 1" "" ;;
    tls12_wolfserver_osslclient)
        run_case $s "$WOLF_SRV12" "$OSSL_CLI12 -maxfraglen 512" "$MSG" ;;
    tls12_osslserver_wolfclient)
        run_case $s "$OSSL_SRV12" "$WOLF_CLI12 -F 1" "" ;;
    tls13_wolfserver_osslclient_nomfl)
        run_case $s "$WOLF_SRV13" "$OSSL_CLI13" "$MSG" ;;
    tls13_osslserver_wolfclient_nomfl)
        run_case $s "$OSSL_SRV13" "$WOLF_CLI13" "" ;;
    baseline_wolfserver)
        run_baseline $s "$WOLF_SRV13" ;;
    baseline_osslserver)
        run_baseline $s "$OSSL_SRV13" ;;
    *) log "unknown scenario $s" ;;
    esac
done
log "done; results in $RES"
```

### A.4 Build configuration

```
cd $WORK
source poky/oe-init-build-env build      # creates build/conf with the poky templates
```

Replace `build/conf/bblayers.conf` with:

```
POKY_BBLAYERS_CONF_VERSION = "2"
BBPATH = "${TOPDIR}"
BBFILES ?= ""
BBLAYERS ?= " \
  $WORK/poky/meta \
  $WORK/poky/meta-poky \
  $WORK/poky/meta-yocto-bsp \
  $WORK/meta-wolfssl \
  $WORK/meta-tlsram \
  "
```

(write the absolute path instead of `$WORK`). Replace `build/conf/local.conf` with:

```
MACHINE ?= "qemux86-64"
DISTRO ?= "poky"
PACKAGE_CLASSES ?= "package_ipk"
DL_DIR ?= "${TOPDIR}/../downloads"
SSTATE_DIR ?= "${TOPDIR}/../sstate-cache"
TMPDIR = "${TOPDIR}/tmp"
BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 8"
BB_HASHSERVE = "auto"
BB_SIGNATURE_HANDLER = "OEEquivHash"
# root login without password + dropbear, so the test can be driven over ssh
EXTRA_IMAGE_FEATURES ?= "debug-tweaks ssh-server-dropbear"
# QEMU guest: enough RAM for valgrind, 2 vCPUs
QB_MEM = "-m 1024"
QB_SMP = "-smp 2"
CONF_VERSION = "2"
```

Build:

```
bitbake tls-ram-image qemu-helper-native
```

Check the wolfSSL configuration actually used (the configure summary must say
`SP implementation: restricted`, `Small Stack: yes`, `Maximum Fragment Length: yes`,
`Single threaded: yes`, `RSA: no`, `SHA-384: no`):

```
grep -E "SP implementation|Small Stack|Maximum Fragment|Single threaded|RSA:|SHA-384:" \
  tmp/work/core2-64-poky-linux/wolfssl/5.9.4/temp/log.do_configure
grep -E "^#define (ECC_USER_CURVES|NO_SESSION_CACHE|WOLFSSL_SMALL_STACK|HAVE_MAX_FRAGMENT|SINGLE_THREADED|GCM_SMALL)$" \
  tmp/work/core2-64-poky-linux/wolfssl/5.9.4/build/wolfssl/options.h | sort -u | wc -l   # expect 6
```

The image lands in `tmp/deploy/images/qemux86-64/tls-ram-image-qemux86-64.rootfs.ext4`
(246,289,408 bytes) with its `.manifest` (56 packages; the size comes mostly from `libc6-dbg`,
which valgrind recommends) and `.qemuboot.conf`.

### A.5 Boot the image and run the measurement

Terminal 1 (no root needed; slirp networking forwards guest port 22 to host port 2222):

```
cd $WORK && source poky/oe-init-build-env build
runqemu $PWD/tmp/deploy/images/qemux86-64/tls-ram-image-qemux86-64.rootfs.qemuboot.conf nographic slirp
```

Terminal 2, once the login prompt appears (root has no password; the guest's host key changes
with every image, hence the throwaway known_hosts file):

```
SSH="ssh -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=$WORK/known_hosts_qemu root@localhost"
$SSH 'uname -a; openssl version; valgrind --version'
$SSH 'tls-ram-test /root/results'                        # about 3 minutes under TCG
$SSH 'cd /root && tar cf - results' | tar xf -           # results/ on the host
$SSH poweroff
```

`results/exit_codes.txt` must show `server_exit=0 client_exit=0` for all 12 connection runs and
`server_exit=143` (SIGTERM, intentional) for the two baselines. Each `*.client.*.log` /
`*.server.*.log` shows the negotiated protocol, cipher and curve, e.g.
`SSL version is TLSv1.3`, `SSL cipher suite is TLS_AES_128_GCM_SHA256`,
`SSL curve name is SECP256R1`, and `Verify return code: 0 (ok)` on the OpenSSL client side.

`tools/run_qemu_tests.sh` in the repository does all of A.5 unattended (`tools/run_qemu_tests.sh
results`), and `setup.sh` does A.2 and A.4.

### A.6 Reading the numbers without any custom tooling

Each `results/<scenario>.<role>.massif` is a plain-text valgrind massif file. Every snapshot has
`mem_heap_B` (bytes the program requested), `mem_heap_extra_B` (allocator overhead, 8 B/block
estimate plus alignment) and `mem_stacks_B` (stack in use). The "peak total" in the report is the
snapshot with the largest sum:

```
awk -F= '/^mem_heap_B/{h=$2} /^mem_heap_extra_B/{e=$2} /^mem_stacks_B/{s=$2; t=h+e+s;
         if(t>m){m=t;mh=h;me=e;ms=s}} END{printf "peak %d B (heap %d + extra %d + stack %d)\n",m,mh,me,ms}' \
    results/tls13_wolfserver_osslclient.server.massif
```

`ms_print results/<file>.massif | head -40` shows the same peak (marked `(peak)`) and its
allocation tree; `ms_print` is part of valgrind. Max RSS is the
`Maximum resident set size (kbytes)` line in each `results/<scenario>.<role>.time` file.
`tools/parse_results.py results` prints the full table.

### A.7 Expected results

| Measurement | Expected | Observed here |
|---|---|---|
| wolfSSL server, TLS 1.3, peak heap+extra+stack | 10–11 KiB | 10,736 B |
| wolfSSL client, TLS 1.3 | 12–13 KiB | 13,064 B |
| OpenSSL s_client, TLS 1.3 | 780–800 KiB | 806,296 B |
| OpenSSL s_server, TLS 1.3 | 750–790 KiB | 775,608 B |
| wolfSSL server, init only (baseline) | about 10 KiB | 10,496 B |
| OpenSSL s_server, init only (baseline) | 630–660 KiB | 651,680 B |
| wolfSSL max stack | under 4 KiB | 2,768–3,424 B during connections; 4,024 B in the init-only run (start-up) |
| OpenSSL max stack | 6–9 KiB | 7,848–8,160 B during connections (6,536 B for one client in the rebuild run, A.11); 8,584 B in the init-only run |
| Max RSS wolfSSL / OpenSSL | about 2 MiB / 9 MiB | 2,048–2,304 KiB / 8,576–9,088 KiB over both runs |

wolfSSL numbers are deterministic to within a few hundred bytes (certificate sizes and the
example program's buffers are fixed). OpenSSL numbers depend on the OpenSSL build and its config
file; a few percent of variation between builds is normal, and `s_server` without a connection
should always sit around 500 KiB of requested heap. Between the two runs made for this report
(A.11) every peak figure was in fact identical to the byte; only max RSS and massif's max-stack
figure moved.

### A.8 Static sizes

From the build tree, with the host `size`:

```
size tmp/work/core2-64-poky-linux/wolfssl/5.9.4/package/usr/lib/libwolfssl.so.46.0.0
size tmp/work/core2-64-poky-linux/openssl/3.5.8/package/usr/lib/libssl.so.3
size tmp/work/core2-64-poky-linux/openssl/3.5.8/package/usr/lib/libcrypto.so.3
```

### A.9 Fifteen-minute partial reproduction without Yocto

The measurement script only needs `wolfssl-server`, `wolfssl-client`, `openssl`, `valgrind` and
GNU `time` on `PATH`, so the wolfSSL side can be reproduced natively on any Linux host (numbers
differ slightly from the image because the host OpenSSL and glibc differ; the wolfSSL endpoints
came out at 16–17 KiB on this Ubuntu host versus 10–13 KiB in the image, the difference being the
host's larger glibc/ld.so stack and allocator rounding):

```
sudo apt install valgrind time openssl build-essential autoconf automake libtool
git clone https://github.com/wolfssl/wolfssl.git && cd wolfssl
git checkout 3c5eead44904df64e6a5a1f4ebdce377d35a849a
./autogen.sh
./configure --enable-tls13 --disable-oldtls --disable-rsa --disable-dh --disable-dsa --disable-psk \
  --disable-sha512 --disable-sha384 --disable-chacha --disable-poly1305 --disable-aescbc \
  --enable-ecc --disable-curve25519 --disable-ed25519 --disable-mlkem --disable-kyber --disable-dilithium \
  --enable-sp-math --enable-sp=smallec256 --disable-sp-asm \
  --enable-smallstack --enable-lowresource --enable-maxfragment --enable-singlethreaded \
  CFLAGS=-DECC_USER_CURVES
make -j8
mkdir -p ~/bin
ln -sf $PWD/examples/server/.libs/server ~/bin/wolfssl-server
ln -sf $PWD/examples/client/.libs/client ~/bin/wolfssl-client
PATH=~/bin:$PATH LD_LIBRARY_PATH=$PWD/src/.libs TLSRAM_CERTS=$PWD/certs \
  sh /path/to/tls-ram-test.sh $PWD/results
```

Then read `results/` as in A.6.

### A.10 Repository layout

| Path | Contents |
|---|---|
| `REPORT.md`, `REPORT.pdf` | this report |
| `meta-tlsram/` | the layer exactly as built (A.3) |
| `setup.sh` | clones poky and meta-wolfssl at the pinned commits and writes `build/conf` (A.2, A.4) |
| `tools/run_qemu_tests.sh` | boots the image, runs the test over ssh, copies results back (A.5) |
| `tools/parse_results.py` | summary table from a results directory (A.6) |
| `tools/compare_results.py` | peak-by-peak comparison of two results directories, flags differences over 5 % (A.11) |
| `tools/static_sizes.sh` | text/data/bss of the built libraries (A.8) |
| `tools/md2pdf.py`, `tools/make_report_pdf.sh` | render `REPORT.md` to `REPORT.pdf` |
| `results/` | every raw massif, time and log file behind the tables in this report, uncompressed |
| `results/REPRODUCTION.md` | the clean-rebuild check of 2026-10-23 with the full comparison table (A.11) |

Everything else (`poky/`, `meta-wolfssl/`, `build/`, `downloads/`, `sstate-cache/`) is generated
and ignored by git.
The scripts under `tools/` and `setup.sh` are reproduced in A.12.

### A.11 Reproduction check

After this report was written, everything generated was deleted and the project was rebuilt on
the same host from the committed files alone, exactly as in A.2 to A.5 (`setup.sh`, `bitbake
tls-ram-image qemu-helper-native`, `tools/run_qemu_tests.sh`), on 2026-10-23.

| Step | Outcome |
|---|---|
| Clone, download and build (8 threads, no sstate reuse) | 2 h 46 min; 4,212 tasks attempted, all succeeded |
| A.4 checks | configure summary as listed; 6 defines in `options.h`; ext4 246,289,408 bytes; 56 packages |
| Static sizes (A.8) | identical to the table in "Results" |
| Measurement run in QEMU | 3 min; every server and client exit code clean |
| `tools/compare_results.py` (±5 % on peak total) | 14 endpoints compared, 0 flagged, 0 missing |

Peak total, heap at peak and stack at peak were identical to the byte for all 14 endpoints. The
two figures that are not part of that comparison moved slightly: max RSS (page granularity, up to
256 KiB between runs) and massif's max stack for one endpoint (7,848 B then 6,536 B; massif samples
the stack only at allocation events, so the maximum over the whole run is less stable than the
stack at the peak snapshot). The raw files of the second run differ from the committed ones only
in process ids, timestamps and intermediate snapshots; they were not added to the repository
(they would double its size). `results/REPRODUCTION.md` holds the full comparison table.

| Endpoint (scenario.role) | Peak total (B) | Heap at peak (B) | Stack at peak (B) | Max stack, run 1 / run 2 (B) | Max RSS, run 1 / run 2 (KiB) |
|---|---|---|---|---|---|
| baseline_osslserver.server | 651,680 | 526,428 | 7,808 | 8,584 / 8,584 | – |
| baseline_wolfserver.server | 10,496 | 7,560 | 2,768 | 4,024 / 4,024 | – |
| tls12_osslserver_wolfclient.client | 12,080 | 8,200 | 3,424 | 3,424 / 3,424 | 2,304 / 2,176 |
| tls12_osslserver_wolfclient.server | 771,232 | 645,588 | 4,768 | 8,128 / 8,128 | 8,704 / 8,704 |
| tls12_wolfserver_osslclient.client | 785,960 | 659,388 | 5,904 | 7,848 / 6,536 | 8,832 / 9,088 |
| tls12_wolfserver_osslclient.server | 10,600 | 8,149 | 2,048 | 2,768 / 2,768 | 2,176 / 2,176 |
| tls13_osslserver_wolfclient.client | 13,064 | 9,164 | 3,392 | 3,392 / 3,392 | 2,048 / 2,048 |
| tls13_osslserver_wolfclient.server | 775,608 | 650,291 | 4,864 | 8,128 / 8,128 | 8,832 / 8,576 |
| tls13_osslserver_wolfclient_nomfl.client | 12,800 | 8,950 | 3,392 | 3,392 / 3,392 | 2,176 / 2,176 |
| tls13_osslserver_wolfclient_nomfl.server | 790,704 | 665,476 | 4,864 | 8,160 / 8,160 | 8,704 / 8,960 |
| tls13_wolfserver_osslclient.client | 806,296 | 679,513 | 5,152 | 8,000 / 8,000 | 9,088 / 8,832 |
| tls13_wolfserver_osslclient.server | 10,736 | 8,133 | 2,160 | 2,768 / 2,768 | 2,048 / 2,176 |
| tls13_wolfserver_osslclient_nomfl.client | 806,296 | 679,505 | 5,152 | 8,000 / 8,000 | 8,960 / 8,960 |
| tls13_wolfserver_osslclient_nomfl.server | 10,672 | 8,100 | 2,160 | 2,768 / 2,768 | 2,048 / 2,048 |

Peak total, heap and stack at peak are the same in both runs, so they are listed once. The
baselines are stopped with SIGTERM under massif and have no GNU time record, hence no RSS.

To repeat the check yourself: `cp -a results results-published` before running the measurement
(so the committed first-run files are kept), rerun A.5 into `results`, then
`tools/compare_results.py results-published results`. Exit status 0 means every peak total is
within 5 % of the published one.

### A.12 Helper scripts

These seven files are the ones named in A.5, A.8 and A.10; none is needed to understand the numbers,
they automate A.2 to A.8 and A.11 and render this report to PDF. Put `setup.sh` in `$WORK` and the
others in `$WORK/tools/` (`compare_results.py` imports `parse_results.py` from its own directory);
`chmod +x` all of them.
With them the whole procedure is `./setup.sh`, then `source poky/oe-init-build-env build && bitbake
tls-ram-image qemu-helper-native`, then `tools/run_qemu_tests.sh results`. `setup.sh` needs the
layer `meta-tlsram` from A.3 to exist already (it only writes `build/conf`); create that first.

`setup.sh` (A.2 and A.4):

```bash
#!/bin/bash
# setup.sh: fetch the pinned sources and write the build configuration, i.e.
# Appendix A.2 and A.4 of REPORT.md.  Run it from anywhere; it works relative
# to its own location (the repository root).
#
#   ./setup.sh
#   source poky/oe-init-build-env build
#   bitbake tls-ram-image qemu-helper-native
#   cd .. && tools/run_qemu_tests.sh results
set -e
WORK=$(cd "$(dirname "$0")" && pwd)
POKY_COMMIT=3a3d07f625aee7b87ea1026526b756447c357652        # scarthgap, Yocto 5.0.20
METAWOLFSSL_COMMIT=e029e80b743fb3096ccb494161144ae3888e1a48  # master, 2026-09-29 (wolfSSL 5.9.4)
cd "$WORK"

[ -d poky/.git ] || git clone -b scarthgap https://git.yoctoproject.org/poky
git -C poky checkout -q "$POKY_COMMIT"
[ -d meta-wolfssl/.git ] || git clone https://github.com/wolfSSL/meta-wolfssl.git
git -C meta-wolfssl checkout -q "$METAWOLFSSL_COMMIT"
echo "poky        $(git -C poky rev-parse HEAD)"
echo "meta-wolfssl $(git -C meta-wolfssl rev-parse HEAD)"

mkdir -p build/conf
cat > build/conf/bblayers.conf <<EOF
POKY_BBLAYERS_CONF_VERSION = "2"
BBPATH = "\${TOPDIR}"
BBFILES ?= ""
BBLAYERS ?= " \\
  $WORK/poky/meta \\
  $WORK/poky/meta-poky \\
  $WORK/poky/meta-yocto-bsp \\
  $WORK/meta-wolfssl \\
  $WORK/meta-tlsram \\
  "
EOF
cat > build/conf/local.conf <<'EOF'
MACHINE ?= "qemux86-64"
DISTRO ?= "poky"
PACKAGE_CLASSES ?= "package_ipk"
DL_DIR ?= "${TOPDIR}/../downloads"
SSTATE_DIR ?= "${TOPDIR}/../sstate-cache"
TMPDIR = "${TOPDIR}/tmp"
BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 8"
BB_HASHSERVE = "auto"
BB_SIGNATURE_HANDLER = "OEEquivHash"
# root login without password + dropbear, so the test can be driven over ssh
EXTRA_IMAGE_FEATURES ?= "debug-tweaks ssh-server-dropbear"
# QEMU guest: enough RAM for valgrind, 2 vCPUs
QB_MEM = "-m 1024"
QB_SMP = "-smp 2"
CONF_VERSION = "2"
EOF
echo "wrote build/conf/bblayers.conf and build/conf/local.conf"
echo "next: source poky/oe-init-build-env build && bitbake tls-ram-image qemu-helper-native"
```

`tools/run_qemu_tests.sh` (A.5):

```bash
#!/bin/bash
# Boot tls-ram-image in QEMU (slirp networking, no root needed), run the
# in-guest measurement script over ssh, and copy the results back.
#
# Usage: tools/run_qemu_tests.sh [results-dir-on-host] [scenario ...]
# (no "set -u": oe-init-build-env references unset variables when sourced)
TOP=$(cd "$(dirname "$0")/.." && pwd)
HOST_RES=${1:-$TOP/results}
case "$HOST_RES" in /*) ;; *) HOST_RES="$PWD/$HOST_RES" ;; esac
[ $# -gt 0 ] && shift
SCENARIOS="$*"
LOGDIR=$TOP/build/run-logs
SSH="ssh -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=$LOGDIR/known_hosts_qemu -o ConnectTimeout=5 -o BatchMode=yes -o LogLevel=ERROR root@localhost"

mkdir -p "$HOST_RES" "$LOGDIR"
cd "$TOP" || exit 1
# shellcheck disable=SC1091
source poky/oe-init-build-env build > "$LOGDIR/oe_init_run.log" 2>&1 || exit 1

echo "[host] booting qemux86-64 tls-ram-image (slirp, nographic) ..."
DEPLOY=$TOP/build/tmp/deploy/images/qemux86-64
QBCONF=$DEPLOY/tls-ram-image-qemux86-64.rootfs.qemuboot.conf
# pass the qemuboot.conf explicitly so runqemu does not need "bitbake -e"
runqemu "$QBCONF" nographic slirp > "$LOGDIR/runqemu.log" 2>&1 &
QPID=$!

# wait for ssh (dropbear) on forwarded port 2222
up=0
for i in $(seq 1 240); do
    if $SSH true 2> "$LOGDIR/ssh_probe.log"; then up=1; break; fi
    if ! kill -0 $QPID 2> "$LOGDIR/kill0.log"; then echo "[host] runqemu exited early"; tail -20 "$LOGDIR/runqemu.log"; exit 1; fi
    sleep 5
done
if [ $up -ne 1 ]; then
    echo "[host] ssh to guest never came up"; tail -30 "$LOGDIR/runqemu.log"; kill $QPID; exit 1
fi
echo "[host] guest is up after ~$((i*5)) s"
$SSH 'uname -a; openssl version; wolfssl-server -? 2>&1 | head -n 1; valgrind --version; free -k | head -n 2'

# make sure the guest runs the current version of the test script (the image
# is rebuilt from the same file, so this only matters while iterating)
$SSH 'cat > /usr/bin/tls-ram-test && chmod 0755 /usr/bin/tls-ram-test' < "$TOP/meta-tlsram/recipes-tlsram/tls-ram-tests/files/tls-ram-test.sh"

echo "[host] running tls-ram-test in guest (this takes a while under TCG + valgrind) ..."
# shellcheck disable=SC2086
$SSH "tls-ram-test /root/results $SCENARIOS" 2>&1 | tee "$LOGDIR/guest_tests.log"

echo "[host] fetching results ..."
$SSH 'cd /root && tar cf - results' | tar xf - -C "$HOST_RES/.." 2> "$LOGDIR/tar_fetch.log" || {
    # fallback: fetch file by file
    for f in $($SSH 'ls /root/results'); do
        $SSH "cat /root/results/$f" > "$HOST_RES/$f"
    done
}
ls "$HOST_RES" | head -50

echo "[host] shutting down guest"
$SSH 'poweroff' 2> "$LOGDIR/poweroff.log"
for i in $(seq 1 30); do kill -0 $QPID 2> "$LOGDIR/kill0.log" || break; sleep 2; done
kill $QPID 2> "$LOGDIR/kill_q.log"
pkill -f "qemu-system-x86_64.*tls-ram-image" 2> "$LOGDIR/pkill_q.log"
echo "[host] done; results in $HOST_RES"
```

`tools/parse_results.py` (A.6; `parse_results.py results --md` prints the Full table):

```python
#!/usr/bin/env python3
"""Summarise tls-ram-test results (valgrind massif + GNU time outputs).

Usage: parse_results.py RESULTS_DIR [--md]

For every <scenario>.<role>.massif file the peak snapshot (heap + heap extra +
stack) is reported together with the individual maxima of heap and stack over
all snapshots.  For every <scenario>.<role>.time file the maximum resident set
size is reported.
"""
import glob
import os
import re
import sys

SCENARIO_DESC = {
    "tls13_wolfserver_osslclient": ("TLS 1.3", "wolfSSL", "OpenSSL", "MFL 512"),
    "tls13_osslserver_wolfclient": ("TLS 1.3", "OpenSSL", "wolfSSL", "MFL 512"),
    "tls12_wolfserver_osslclient": ("TLS 1.2", "wolfSSL", "OpenSSL", "MFL 512"),
    "tls12_osslserver_wolfclient": ("TLS 1.2", "OpenSSL", "wolfSSL", "MFL 512"),
    "tls13_wolfserver_osslclient_nomfl": ("TLS 1.3", "wolfSSL", "OpenSSL", "no MFL"),
    "tls13_osslserver_wolfclient_nomfl": ("TLS 1.3", "OpenSSL", "wolfSSL", "no MFL"),
    "baseline_wolfserver": ("none", "wolfSSL", "-", "init only"),
    "baseline_osslserver": ("none", "OpenSSL", "-", "init only"),
}


def parse_massif(path):
    snaps = []
    cur = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("snapshot="):
                cur = {"n": int(line.split("=")[1])}
                snaps.append(cur)
            elif cur is not None and "=" in line and not line.startswith("desc") \
                    and not line.startswith("cmd") and not line.startswith("time_unit"):
                k, v = line.split("=", 1)
                if k in ("mem_heap_B", "mem_heap_extra_B", "mem_stacks_B", "time"):
                    cur[k] = int(v)
                elif k == "heap_tree":
                    cur[k] = v
    if not snaps:
        return None
    for s in snaps:
        s["total"] = s["mem_heap_B"] + s["mem_heap_extra_B"] + s["mem_stacks_B"]
    peak = max(snaps, key=lambda s: s["total"])
    return {
        "peak_total": peak["total"],
        "peak_heap": peak["mem_heap_B"],
        "peak_heap_extra": peak["mem_heap_extra_B"],
        "peak_stack_at_peak": peak["mem_stacks_B"],
        "max_heap": max(s["mem_heap_B"] for s in snaps),
        "max_heap_with_extra": max(s["mem_heap_B"] + s["mem_heap_extra_B"] for s in snaps),
        "max_stack": max(s["mem_stacks_B"] for s in snaps),
        "snapshots": len(snaps),
    }


def parse_time(path):
    d = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"\s*Maximum resident set size \(kbytes\): (\d+)", line)
            if m:
                d["max_rss_kb"] = int(m.group(1))
            m = re.match(r"\s*Exit status: (\d+)", line)
            if m:
                d["exit"] = int(m.group(1))
            m = re.match(r"\s*Elapsed \(wall clock\) time.*: (.*)", line)
            if m:
                d["elapsed"] = m.group(1).strip()
    return d


def kib(b):
    return f"{b/1024:.1f}"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    res = sys.argv[1]
    md = "--md" in sys.argv
    rows = []
    for scen, (ver, srv, cli, mfl) in SCENARIO_DESC.items():
        for role in ("server", "client"):
            impl = srv if role == "server" else cli
            if impl == "-":
                continue
            mpath = os.path.join(res, f"{scen}.{role}.massif")
            tpath = os.path.join(res, f"{scen}.{role}.time")
            m = parse_massif(mpath) if os.path.exists(mpath) else None
            t = parse_time(tpath) if os.path.exists(tpath) else {}
            if m is None and not t:
                continue
            rows.append((scen, ver, mfl, role, impl, m, t))

    hdr = ["Scenario", "TLS", "MFL", "Role", "Library", "Peak total (KiB)",
           "Heap at peak (KiB)", "Heap extra (KiB)", "Stack at peak (KiB)",
           "Max heap (KiB)", "Max stack (KiB)", "Max RSS (KiB)"]
    out = []
    for scen, ver, mfl, role, impl, m, t in rows:
        if m:
            vals = [kib(m["peak_total"]), kib(m["peak_heap"]), kib(m["peak_heap_extra"]),
                    kib(m["peak_stack_at_peak"]), kib(m["max_heap"]), kib(m["max_stack"])]
        else:
            vals = ["-"] * 6
        rss = str(t.get("max_rss_kb", "-"))
        out.append([scen, ver, mfl, role, impl] + vals + [rss])

    if md:
        print("| " + " | ".join(hdr) + " |")
        print("|" + "|".join(["---"] * len(hdr)) + "|")
        for r in out:
            print("| " + " | ".join(r) + " |")
    else:
        widths = [max(len(h), *(len(r[i]) for r in out)) for i, h in enumerate(hdr)]
        print("  ".join(h.ljust(w) for h, w in zip(hdr, widths)))
        for r in out:
            print("  ".join(c.ljust(w) for c, w in zip(r, widths)))

    ec = os.path.join(res, "exit_codes.txt")
    if os.path.exists(ec):
        print()
        print("exit codes:")
        print(open(ec).read().rstrip())


if __name__ == "__main__":
    main()
```

`tools/compare_results.py` (A.11):

```python
#!/usr/bin/env python3
"""Compare two tls-ram-test result directories (e.g. the committed results and a
fresh reproduction) and print a markdown table of the peak figures side by side.

usage: compare_results.py REFERENCE_DIR NEW_DIR [--tolerance-pct N]

For every <scenario>.<role>.massif present in both directories: peak total
(heap + heap extra + stack), heap at peak, stack at peak, max stack; for every
<scenario>.<role>.time: max RSS.  A row is flagged when the peak total differs
by more than the tolerance (default 5 %).  Exit status 1 if any row is flagged
or if files are missing.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse_results import parse_massif, parse_time  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    tol = 5.0
    for a in sys.argv[1:]:
        m = re.match(r"--tolerance-pct=(\d+(\.\d+)?)", a)
        if m:
            tol = float(m.group(1))
    if len(args) != 2:
        print(__doc__)
        sys.exit(2)
    ref, new = args
    names = sorted(f[:-len(".massif")] for f in os.listdir(ref) if f.endswith(".massif"))
    rows, flagged, missing = [], 0, 0
    print(f"| Endpoint (scenario.role) | peak total ref / new (B) | diff | heap at peak ref / new | stack at peak ref / new | max stack ref / new | max RSS ref / new (KiB) |")
    print("|---|---|---|---|---|---|---|")
    for n in names:
        rp, npth = os.path.join(ref, n + ".massif"), os.path.join(new, n + ".massif")
        if not os.path.exists(npth):
            print(f"| {n} | MISSING in new | | | | | |")
            missing += 1
            continue
        a, b = parse_massif(rp), parse_massif(npth)
        dpct = (b["peak_total"] - a["peak_total"]) * 100.0 / a["peak_total"]
        flag = " **(>%g%%)**" % tol if abs(dpct) > tol else ""
        flagged += 1 if flag else 0
        rt, nt = os.path.join(ref, n + ".time"), os.path.join(new, n + ".time")
        rss_a = parse_time(rt).get("max_rss_kb", "-") if os.path.exists(rt) else "-"
        rss_b = parse_time(nt).get("max_rss_kb", "-") if os.path.exists(nt) else "-"
        print(f"| {n} | {a['peak_total']:,} / {b['peak_total']:,} | {dpct:+.1f}%{flag} | "
              f"{a['peak_heap']:,} / {b['peak_heap']:,} | {a['peak_stack_at_peak']:,} / {b['peak_stack_at_peak']:,} | "
              f"{a['max_stack']:,} / {b['max_stack']:,} | {rss_a} / {rss_b} |")
    print()
    print(f"{len(names)} endpoints compared, {flagged} outside ±{tol:g}% on peak total, {missing} missing.")
    for d, label in ((ref, "reference"), (new, "new")):
        ec = os.path.join(d, "exit_codes.txt")
        if os.path.exists(ec):
            bad = [l.strip() for l in open(ec) if not re.search(r"server_exit=(0|143) client_exit=(0|none)", l)]
            print(f"{label} exit codes: {'all clean' if not bad else 'PROBLEMS: ' + '; '.join(bad)}")
    sys.exit(1 if (flagged or missing) else 0)


if __name__ == "__main__":
    main()
```

`tools/static_sizes.sh` (A.8; prints text/data/bss of the unstripped libraries and test binaries from
the Yocto work tree):

```bash
#!/bin/bash
# Print text/data/bss sizes of the target TLS libraries and test binaries
# from the Yocto package trees (unstripped copies live in .../package/).
TOP=$(cd "$(dirname "$0")/.." && pwd)
W=$TOP/build/tmp/work/core2-64-poky-linux
for f in $W/wolfssl/*/package/usr/lib/libwolfssl.so.*.*.* \
         $W/wolfssl/*/package/usr/bin/wolfssl-server \
         $W/wolfssl/*/package/usr/bin/wolfssl-client \
         $W/openssl/*/package/usr/lib/libssl.so.3 \
         $W/openssl/*/package/usr/lib/libcrypto.so.3 \
         $W/openssl/*/package/usr/bin/openssl; do
    [ -f "$f" ] || continue
    size "$f" | tail -1 | awk -v n="$(basename "$f")" '{printf "%-24s text=%8d data=%7d bss=%7d\n", n, $1, $2, $3}'
done
```

`tools/make_report_pdf.sh` and `tools/md2pdf.py` render `REPORT.md` to `REPORT.pdf`. They need
`python3-markdown-it-py` (or `pip install markdown-it-py`), `google-chrome` and `pdfinfo`
(poppler-utils), and expect `REPORT.md` in `$WORK`; the output is `$WORK/REPORT.pdf`:

```bash
#!/bin/bash
# REPORT.md -> REPORT.pdf (markdown-it-py for HTML, headless Chrome for printing)
TOP=$(cd "$(dirname "$0")/.." && pwd)
TMP=$TOP/build/report-tmp
mkdir -p "$TMP/chrome-profile"
python3 "$TOP/tools/md2pdf.py" "$TOP/REPORT.md" "$TMP/REPORT.html" || exit 1
google-chrome --headless=new --no-sandbox --disable-gpu --user-data-dir="$TMP/chrome-profile" \
    --no-pdf-header-footer --print-to-pdf="$TOP/REPORT.pdf" "file://$TMP/REPORT.html" \
    > "$TMP/chrome_pdf.log" 2>&1 || exit 1
pdfinfo "$TOP/REPORT.pdf" | grep -E "Pages|Title"
```

```python
#!/usr/bin/env python3
"""Render REPORT.md to a print-friendly HTML file (then printed to PDF with
headless Chrome by tools/make_report_pdf.sh).

Usage: md2pdf.py REPORT.md REPORT.html
"""
import html
import re
import sys

from markdown_it import MarkdownIt

CSS = """
@page { size: Letter; margin: 18mm 16mm 18mm 16mm; }
@page wide { size: Letter landscape; margin: 14mm 12mm; }
html { font-size: 10.5pt; }
body { font-family: "DejaVu Sans", "Liberation Sans", Arial, sans-serif; color: #111;
       line-height: 1.38; max-width: none; margin: 0; }
h1 { font-size: 19pt; margin: 0 0 6pt 0; line-height: 1.2; }
h2 { font-size: 14pt; margin: 18pt 0 6pt 0; border-bottom: 1px solid #999; padding-bottom: 2pt;
     page-break-after: avoid; }
h3 { font-size: 11.5pt; margin: 12pt 0 4pt 0; page-break-after: avoid; }
p  { margin: 4pt 0 6pt 0; }
ul { margin: 2pt 0 6pt 0; padding-left: 18pt; }
li { margin: 2pt 0; }
code { font-family: "DejaVu Sans Mono", "Liberation Mono", monospace; font-size: 88%;
       background: #f2f2f2; padding: 0 2px; border-radius: 2px; }
pre  { font-family: "DejaVu Sans Mono", "Liberation Mono", monospace; font-size: 8.3pt;
       background: #f4f4f4; border: 1px solid #ddd; padding: 6pt 8pt; white-space: pre-wrap;
       word-break: break-all; page-break-inside: auto; margin: 4pt 0 8pt 0; }
pre code { background: none; padding: 0; font-size: 100%; }
table { border-collapse: collapse; margin: 4pt 0 10pt 0; font-size: 9pt; page-break-inside: avoid;
        max-width: 100%; }
th, td { border: 1px solid #bbb; padding: 3pt 5pt; vertical-align: top; text-align: left;
         overflow-wrap: break-word; }
th { background: #e8e8e8; font-weight: bold; }
td code, th code { font-size: 90%; white-space: normal; overflow-wrap: break-word; }
td:first-child code { white-space: nowrap; }
tr:nth-child(even) td { background: #fafafa; }
em { color: #333; }
.wide { page: wide; page-break-before: always; page-break-after: always; }
.wide table { font-size: 8.6pt; }
.wide th, .wide td { padding: 2pt 4pt; white-space: nowrap; }
.wide th { white-space: normal; }
.wide td code, .wide th code { white-space: nowrap; }
.wide p { font-size: 9pt; }
.subtitle { color: #444; font-size: 9.5pt; margin-bottom: 10pt; }
"""


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()

    # Any "### " subsection that contains a table with 8 or more columns goes on
    # its own landscape page (the section runs up to the next "##"/"###" heading).
    md = MarkdownIt("commonmark").enable("table")
    wide_sections = {}

    def is_wide(section):
        for line in section.splitlines():
            if line.startswith("|") and line.count("|") >= 9:
                return True
        return False

    def stash(m):
        sec = m.group(0)
        if not is_wide(sec):
            return sec
        key = f"WIDE_TABLE_MARKER_{len(wide_sections)}"
        wide_sections[key] = md.render(sec)
        return key + "\n\n"

    text = re.sub(r"^### .*?(?=^##|\Z)", stash, text, flags=re.S | re.M)
    body = md.render(text)
    for key, html_sec in wide_sections.items():
        body = body.replace(f"<p>{key}</p>", f'<div class="wide">{html_sec}</div>')

    title = re.search(r"^# (.*)$", text, flags=re.M)
    title = html.escape(title.group(1)) if title else "Report"
    out = f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>{title}</title>
<style>{CSS}</style></head>
<body>{body}</body></html>"""
    open(dst, "w", encoding="utf-8").write(out)
    print(f"wrote {dst} ({len(out)} bytes)")


if __name__ == "__main__":
    main()
```

---

# TLS RAM usage: low-RAM wolfSSL vs OpenSSL behind `libustream-ssl` on an OpenWrt QEMU image

Measured 2026-10-28 in a QEMU (x86_64, KVM) guest built from OpenWrt 25.12. Summary: for a TLS 1.3
connection with ECDHE P-256 / ECDSA P-256 / AES-128-GCM, made by the stock `uclient-fetch` through
the `libustream-ssl` backend, the process peaks at about 41–44 KiB of heap+stack with the low-RAM
wolfSSL build, about 161–163 KiB with the default OpenWrt wolfSSL build, and about 556–558 KiB with
OpenSSL. The low-RAM flags from the Yocto report above therefore cut wolfSSL's peak by about 3.7–3.9
times on OpenWrt, and the result is about 13 times below OpenSSL. The gap is smaller than on Yocto
(10–13 KiB) because OpenWrt's TLS clients use wolfSSL's OpenSSL compatibility layer, which has to
stay enabled (see "Where this differs from the Yocto build").

## What was built (OpenWrt)

| Item | Value |
|---|---|
| OpenWrt | branch **openwrt-25.12**, commit `4a686570eb6495635279273f034caa2a0f37f36e`, `x86/64` generic, musl 1.2.5, kernel with no `/proc/self/smaps` |
| Package manager | none in the image (`apk` only exists in mbedTLS/OpenSSL variants); packages are chosen at build time |
| wolfSSL | **5.9.4** (`libwolfssl5.9.4.1587f288` in the low-RAM image), OpenWrt package `libwolfssl`, rebuilt with the low-RAM configuration below |
| OpenSSL | **3.5.9** (`libopenssl3`), the OpenWrt package, unmodified |
| TLS client | `uclient-fetch` 2026.06.07~daad21fa, `libustream-ssl` 2026.03.01~99f1c0db, built three ways: `libustream-wolfssl` (default flags), `libustream-wolfssl` (low-RAM flags), `libustream-openssl` |
| Measurement tool | `memshim.so`, an `LD_PRELOAD` malloc tracker written for this test (Appendix B.3); no valgrind, since the image has none |
| QEMU | 10.2.1, `-enable-kvm -m 512 -snapshot`, user-mode networking (host seen as 10.0.2.2), serial console driven from Python |
| Host build tools | gawk, bison, flex, swig, pcre2 built locally into `tools/prefix` (no sudo on the build host) |

Unlike the Yocto test, the endpoint under test is an **application**, not a wolfSSL or OpenSSL demo
program: `uclient-fetch` is the HTTP client that ships with OpenWrt, and the TLS library is selected
by which `libustream-ssl.so` is installed. The server is a Python `http.server` on the host. The
`libustream-wolfssl`, `libustream-openssl` and `libustream-mbedtls` packages all install the same
file `/lib/libustream-ssl.so`, so exactly one can be present.

## wolfSSL low-RAM configuration (OpenWrt)

OpenWrt's `package/libs/wolfssl/Makefile` was changed so that the flags from the Yocto report are
applied when a new option `WOLFSSL_LOW_RAM` (`package/libs/wolfssl/Config.in`, default `n`) is set.
The full change is `openwrt-tls-ram/wolfssl-lowram.patch`. The core of it:

```
ifeq ($(CONFIG_WOLFSSL_LOW_RAM),y)
TARGET_CFLAGS := $(filter-out -DFP_MAX_BITS=8192,$(TARGET_CFLAGS)) -DECC_USER_CURVES
CONFIGURE_ARGS := $(filter-out \
	--enable-lighty --enable-opensslall --enable-stunnel --enable-altcertchains \
	--enable-ocsp --enable-ocspstapling --enable-ocspstapling2 \
	--enable-wpas --enable-fortress --enable-fastmath --enable-intelasm \
	,$(CONFIGURE_ARGS))
CONFIGURE_ARGS += \
	--enable-tls13 --disable-oldtls \
	--disable-rsa --disable-dh --disable-dsa --disable-psk \
	--disable-sha512 --disable-sha384 \
	--disable-chacha --disable-poly1305 --disable-aescbc --disable-aesccm \
	--enable-ecc --disable-curve25519 --disable-ed25519 \
	--disable-mlkem --disable-kyber --disable-dilithium \
	--enable-sp-math-all --enable-sp=smallec256 --disable-sp-asm \
	--enable-smallstack --enable-lowresource --enable-maxfragment \
	--enable-singlethreaded \
	--disable-certgen --disable-arc4 --disable-session-ticket --disable-openvpn
endif
```

The package's own defaults are `--enable-opensslall --enable-opensslextra --enable-sni --enable-stunnel
--enable-altcertchains --enable-lighty`, `-DFP_MAX_BITS=8192`, `-Os`, and on x86_64 `--enable-intelasm
--enable-fastmath`, plus whatever the `Config.in` switches select. `--enable-opensslall`, `stunnel`,
`lighty`, OCSP, `fastmath` and `intelasm` are removed in the low-RAM case because they are
incompatible with the flags above or enlarge the big-number type. `--enable-opensslextra` and
`--enable-sni` stay.

### Where this differs from the Yocto build

| | Yocto report | OpenWrt here | Reason |
|---|---|---|---|
| Math | `--enable-sp-math` | `--enable-sp-math-all` | `libustream-wolfssl` calls `SSL_*`, `BIO_*`, `X509_check_host` through wolfSSL's OpenSSL compatibility layer, so `--enable-opensslextra` must stay. That layer's `wolfSSL_BN_mod_mul` and `wolfSSL_BN_mod_exp` call `sp_mulmod` and `sp_exptmod`, which exist only under `WOLFSSL_SP_MATH_ALL` (or SP RSA/DH). With restricted `--enable-sp-math` the link fails with `undefined reference to sp_mulmod / sp_exptmod`; `configure.ac` does not allow both options together. |
| `SP_INT_BITS` | 256 | **521** | Consequence of the row above. Every `sp_int` is sized for 521 bits instead of 256. |
| OpenSSL compat layer | not needed (native API) | `--enable-opensslextra`, `--enable-sni` | Required by `libustream-wolfssl`. |
| Max fragment length | client asks for 512 B records | compiled in, **not requested** | `libustream-ssl` never calls `wolfSSL_UseMaxFragment`, so records stay at 16 KiB (the largest single allocation is 16,401 B). |
| Test application | wolfSSL example client/server | `uclient-fetch` + `libustream-wolfssl` | The point of this test is the library as OpenWrt actually uses it. |
| Other packages | none | `wpad-basic-wolfssl`, `px5g-wolfssl` removed | They need features the low-RAM build removes (see "Notes on interpretation"). |

## Method

Every run: the guest's `uclient-fetch --ca-certificate=/tmp/c.pem -O /dev/null
https://hostsrv.test:8443/big.bin`, a 100,000-byte file, with the host server restricted to TLS 1.3
only. The server is Python's `ssl` module; `host/ossl.cnf` (set through `OPENSSL_CONF`) forces:

```
MinProtocol = TLSv1.3
MaxProtocol = TLSv1.3
Ciphersuites = TLS_AES_128_GCM_SHA256
Groups = P-256
```

The server certificate is ECDSA P-256 (CN and SAN `hostsrv.test`); the guest maps `hostsrv.test` to
10.0.2.2 in `/etc/hosts` because `ustream-ssl` does not accept an IP address in a SAN. The server
logs `(connection.version(), connection.cipher())` for every connection; every run in the tables
below logged `TLSv1.3` and `TLS_AES_128_GCM_SHA256`. HTTP/1.1 keep-alive is used so that the client
closes the connection first (see "Notes on interpretation").

| Scenario | `/lib/libustream-ssl.so` | `libwolfssl` | sha256 prefix of the backend |
|---|---|---|---|
| `wolfssl_default` | `libustream-wolfssl`, default OpenWrt flags | 1,380,416 B | `50de4a04` (`f209b8da` when rebuilt, see B.9) |
| `wolfssl_lowram` | `libustream-wolfssl`, low-RAM flags | 769,696 B | `e8249f3a` |
| `openssl` | `libustream-openssl` | not present | `88dfe5ab` |

Each scenario is run three times in a fresh boot, each run as `LD_PRELOAD=/tmp/memshim.so
uclient-fetch ...` (one process per run). The VM disk is booted with `-snapshot`, so each backend is
copied over `/lib/libustream-ssl.so` in a throwaway overlay and its hash printed before the run.

`memshim.so` (Appendix B.3) interposes `malloc`, `calloc`, `free`, `realloc`, `posix_memalign`,
`aligned_alloc` and `memalign`, and counts `malloc_usable_size` of every live block. At each
allocation it also computes the stack depth (`stack_top - current_sp`, with `stack_top` read from
`/proc/self/maps`). At exit it prints:

- **heap peak**: highest sum of live usable block sizes;
- **combined peak**: highest value of (live heap + stack depth) over all allocation events;
- **max stack depth at allocation**: the largest stack depth seen at any allocation;
- number of allocations and the largest single request;
- **resident** stack, `[heap]` and other anonymous read-write pages, counted with `mincore()` over
  `/proc/self/maps` (this kernel has no `/proc/self/smaps`), and `VmHWM`.

"Heap + stack" in the headline is the **combined peak**, not the sum of the separate maxima, because
the two maxima need not occur at the same instant.

## Results

### Headline: peak heap + stack of the `uclient-fetch` process, TLS 1.3 (3 runs each)

| Backend | Combined peak, heap + stack (B) | Combined peak (KiB) | Heap at peak | Stack at peak |
|---|---|---|---|---|
| wolfSSL, default OpenWrt build | 164,661 / 166,611 / 166,613 | **160.8–162.7** | 148,981 B | 15,680–17,632 B |
| wolfSSL, **low-RAM build** | 41,899 / 43,499 / 45,195 | **40.9–44.1** | 35,499 B | 6,400–9,696 B |
| OpenSSL 3.5.9 | 568,816 / 569,584 / 571,504 | **555.5–558.1** | 562,112 B | 6,704–9,392 B |

Low-RAM wolfSSL against default wolfSSL: 3.7–3.9 times smaller combined peak, 76 % less heap
(148,981 B to 35,499 B). Low-RAM wolfSSL against OpenSSL: about 13 times smaller (12.6–13.6).

### Detail

| | wolfSSL default | wolfSSL low-RAM | OpenSSL |
|---|---|---|---|
| Heap peak (B) | 148,979–148,981 | **35,499** (every run) | 562,112 (every run) |
| Heap peak (KiB) | 145.5 | **34.7** | 549.0 |
| Number of allocations | 346 | 642 | 11,346 |
| Largest single request (B) | 39,688 | 16,401 | 32,768 |
| Max stack depth at an allocation (B) | 23,728–25,680 | 9,408–12,704 | 9,280–11,968 |
| Resident stack (kB, `mincore`) | 44–48 | 16–20 | 12–16 |
| Resident `[heap]` / other anon rw (kB) | 4 / 300 | 4 / 24 | 20 / 40 |
| `VmHWM` of the whole process (kB) | 2,412–2,428 | 1,564–1,600 | 6,652–6,972 |
| `libwolfssl.so` in the rootfs (B) | 1,380,416 | 769,696 | n/a |

Peak heap is identical across runs to the byte (wolfSSL default differs by 2 B), as in the Yocto
report. Stack depth varies by about 3 KiB between runs of the same binary; it depends on how deep
the call chain is at the moment of an allocation, which changes with the environment size, the
alignment of the initial stack and the order in which sockets become ready.

**An upper bound for the combined peak.** The shim samples the stack only when an allocation
happens, so the true stack maximum can be larger than the sampled one. Heap peak plus the resident
stack pages gives an upper bound: about 190–194 KiB (default wolfSSL), 51–55 KiB (low-RAM wolfSSL),
561–565 KiB (OpenSSL).

### Static footprint

| File (target, stripped, as installed) | Size |
|---|---|
| `libwolfssl.so` default | 1,380,416 B |
| `libwolfssl.so` low-RAM | 769,696 B (44 % smaller) |
| Image `openwrt-x86-64-generic-ext4-combined.img.gz` of this experiment | 16,182,137 B (contains OpenSSL as well, so it is larger than a wolfSSL-only image) |

## Notes on interpretation

- **The measured process is the whole `uclient-fetch`**, not just the TLS library, and no baseline
  is subtracted. A plain-HTTP baseline was started but dropped, and the one plain-HTTP result in
  the scratch logs came from a test-server handler that raised an exception, so none is reported.
  `uclient-fetch`, `libubox`, libc and `libustream-ssl` are the same in all three scenarios, so the
  *differences* between scenarios come from the TLS library.
- **Heap is measured at `malloc` granularity** (usable size reported by musl's allocator), not at the
  page level; the page-level numbers are in the resident rows. Memory obtained with `mmap` directly
  (musl uses it for large blocks) is included in the heap figures only because the shim sees the
  `malloc` call, not because the pages are tracked.
- **Stack depth is sampled at allocation points only** (see the upper bound above). The Yocto report
  used valgrind massif, which samples on its own schedule; the two sets of numbers are comparable in
  order of magnitude, not to the byte.
- **OpenSSL's number is the library inside `libustream-openssl` plus `uclient-fetch`**, not the
  `openssl` command line tool used in the Yocto report. It is lower than there (549 KiB heap against
  about 630–680 KiB) and has 11,346 allocations, which points to library initialisation rather than
  the connection; this was not broken down further.
- **Stack in the low-RAM build is roughly the same as OpenSSL's** (9–12 KiB). `--enable-smallstack`
  moves the large temporaries of the default wolfSSL build to the heap, which is what removes about
  14 KiB of stack depth from the default wolfSSL result.
- **Max fragment length has no effect here.** `--enable-maxfragment` is compiled in, but `libustream-ssl`
  does not ask for it, so the receive buffer is a full 16 KiB record (the 16,401 B allocation is the
  largest single block of the low-RAM run, almost half of its heap).
- **Why it is above Yocto's 10–13 KiB.** Likely contributors are `SP_INT_BITS` 521 instead of 256,
  the OpenSSL compatibility layer (`opensslextra`) and its extra objects, and the 16 KiB record
  buffer (16,401 B of the 35,499 B heap). These are hypotheses from the build and the allocation
  sizes; the saving of each was not measured separately.
- **The low-RAM build is deliberately narrow:** TLS 1.2/1.3 only, ECDHE and ECDSA P-256 only, SHA-256
  only, AES-128-GCM only, no RSA/DH/PSK, no session cache, no X25519 or post-quantum. It **cannot
  verify RSA certificates**, so it will not connect to most public HTTPS sites (OpenWrt's CA bundle
  is mostly RSA). `wpa_supplicant`/`hostapd` (`wpad-basic-wolfssl`) and `px5g-wolfssl` need the
  removed features, so they are not in the image; Wi-Fi authentication and certificate generation
  would have to come from elsewhere.
- **Pairing with the wolfSSL backend is fragile in one case that was seen and not investigated.**
  When the Python server closed the connection without sending `close_notify`, the wolfSSL backend
  logged `fatal I/O error`; when it closed immediately with `close_notify` it logged `peer sent close
  notify alert / Connection failed`. Using HTTP/1.1 keep-alive, so that the client closes first,
  avoided this. Whether this is a `libustream-wolfssl` problem or a test-server artefact was not
  checked.
- **An earlier result was wrong and was discarded.** Copying the OpenSSL backend over
  `/lib/libustream-ssl.so` persisted into the VM disk, so a later "wolfSSL" run was actually OpenSSL
  (identical allocation counts exposed it). From then on every run booted with `-snapshot`, from a
  freshly unpacked image, with the backend hash printed first. The numbers in this report are only
  from runs after that fix.
- **Not done:** TLS 1.2 on OpenWrt, server-side measurement, the Yocto-style
  `--enable-sp-math` build (blocked, see above), and a repeat of the Yocto example-program
  measurements on OpenWrt (an attempt was abandoned in favour of `uclient-fetch`).

## Reproducing (OpenWrt)

Everything that is not upstream OpenWrt is reproduced verbatim in Appendix B. Save the files from
B.2 to B.6 under the names given there into one empty directory (the "kit"), install the build
prerequisites (B.1; `host-tools.sh` is the fallback without root), then:

```
chmod +x reproduce.sh host-tools.sh
export PATH=<prefix from host-tools.sh>/bin:$PATH     # only if host-tools.sh was needed
./reproduce.sh all          # clone, patch, build both variants, build the shim, run both tests
./reproduce.sh summary      # the key measurement lines
```

`reproduce.sh` takes the stages one by one as well (`source`, `build-default`, `build-lowram`,
`shim`, `testdata`, `run-default`, `run-lowram`, `summary`); its header documents them. The first
build compiles the toolchain and takes hours; each variant switch afterwards takes about 4.5
minutes. All output goes under `./work`. B.9 states exactly what was re-verified.

## Appendix B: steps to reproduce (OpenWrt)

### B.1 Environment used

| | Used here |
|---|---|
| Host OS | Ubuntu (Linux 7.0.0), x86-64, 12 cores, `/dev/kvm` readable (KVM used) |
| Host tools | gcc 15.2.0, Python 3.14.4, OpenSSL 3.5.5 (generates the certificate and is the library behind Python's `ssl`, so the *server* side uses it), QEMU 10.2.1, git, curl |
| Build prerequisites | the OpenWrt list for Debian/Ubuntu: `build-essential clang flex bison g++ gawk gcc-multilib gettext git libncurses-dev libssl-dev python3-setuptools rsync swig unzip zlib1g-dev file wget zstd`. Without root, `host-tools.sh` (B.3) builds gawk, bison, flex, pcre2 and swig into a private prefix; with mawk instead of gawk the feeds script fails with `awk: function asort never defined` |
| OpenWrt source | `https://git.openwrt.org/openwrt/openwrt.git` (mirror `https://github.com/openwrt/openwrt`), branch `openwrt-25.12`, commit `4a686570eb6495635279273f034caa2a0f37f36e` |
| Feeds | pinned to the commits in B.2.3 (taken from the build's `feeds.buildinfo`) |
| wolfSSL / OpenSSL / ustream-ssl | pinned by the package Makefiles of that commit: wolfSSL 5.9.4-stable, OpenSSL 3.5.9, ustream-ssl 2026.03.01~99f1c0db; the source tarballs are downloaded and hash-checked by OpenWrt |
| Network | needed for the clone, feeds and source downloads; the test itself only uses host-local servers |
| Disk | about 15 GB for the build tree of this configuration (`src`, measured) plus 250 MB for the two unpacked images |

### B.2 Source changes and configuration

#### B.2.1 Source change: `wolfssl-lowram.patch`

Apply from the root of the OpenWrt tree with `git apply wolfssl-lowram.patch`. It adds the
`WOLFSSL_LOW_RAM` option (`Config.in`), applies the low-RAM configure flags when it is set
(`Makefile`), and builds and packages the wolfSSL example programs as `libwolfssl-examples`
(`--enable-examples --enable-crypttests`, plus the package install rule). With the option unset the
wolfSSL flags are exactly those of the unmodified package except for that examples change.

```diff
diff --git a/package/libs/wolfssl/Config.in b/package/libs/wolfssl/Config.in
index 7c154cc..b0e8f2e 100644
--- a/package/libs/wolfssl/Config.in
+++ b/package/libs/wolfssl/Config.in
@@ -63,6 +63,15 @@ config WOLFSSL_ALT_NAMES
 	bool "Include SAN (Subject Alternative Name) support"
 	default y
 
+config WOLFSSL_LOW_RAM
+	bool "Low-RAM build: ECDHE/ECDSA P-256 + AES-128-GCM + SHA-256 only (TLS 1.2/1.3)"
+	help
+	  Experimental. Removes RSA, DH, DSA, PSK, SHA-384/512, ChaCha, AES-CBC,
+	  Curve25519, post-quantum and TLS < 1.2; uses SP math for P-256 only,
+	  small stack, low resource, max fragment length. Breaks wpa_supplicant
+	  and px5g, and cannot verify RSA certificates.
+	default n
+
 config WOLFSSL_HAS_DEVCRYPTO
 	bool
 
diff --git a/package/libs/wolfssl/Makefile b/package/libs/wolfssl/Makefile
index 4205526..6e97a53 100644
--- a/package/libs/wolfssl/Makefile
+++ b/package/libs/wolfssl/Makefile
@@ -28,6 +28,7 @@ PKG_MAINTAINER:=Eneas U de Queiroz <cotequeiroz@gmail.com>
 PKG_CPE_ID:=cpe:/a:wolfssl:wolfssl
 
 PKG_CONFIG_DEPENDS:=\
+	CONFIG_WOLFSSL_LOW_RAM \
 	CONFIG_WOLFSSL_HAS_AES_CCM \
 	CONFIG_WOLFSSL_HAS_ARC4 \
 	CONFIG_WOLFSSL_HAS_CERTGEN \
@@ -99,6 +100,12 @@ $(call Package/libwolfssl/Default)
   DEPENDS:=libwolfssl
 endef
 
+define Package/libwolfssl-examples
+$(call Package/libwolfssl/Default)
+  TITLE:=wolfSSL example client and server
+  DEPENDS:=libwolfssl
+endef
+
 define Package/libwolfsslcpu-crypto/description
 $(call Package/libwolfssl/description)
 This variant uses AES CPU instructions (Intel AESNI or ARMv8 Crypto Extension)
@@ -136,7 +143,8 @@ CONFIGURE_ARGS += \
 	--enable-stunnel \
 	--enable-altcertchains \
 	--$(if $(CONFIG_PACKAGE_libwolfssl-benchmark),enable,disable)-crypttests \
-	--disable-examples \
+	--enable-examples \
+	--enable-crypttests \
 	--disable-jobserver \
 	--$(if $(CONFIG_IPV6),enable,disable)-ipv6 \
 	--$(if $(CONFIG_WOLFSSL_HAS_AES_CCM),enable,disable)-aesccm \
@@ -204,6 +212,28 @@ CONFIGURE_ARGS += \
 	--enable-wpas --enable-fortress --enable-fastmath
 endif
 
+ifeq ($(CONFIG_WOLFSSL_LOW_RAM),y)
+# Flags from report_yocto.pdf. libustream-wolfssl needs the OpenSSL compat layer,
+# so --enable-opensslextra and --enable-sni are kept.
+TARGET_CFLAGS := $(filter-out -DFP_MAX_BITS=8192,$(TARGET_CFLAGS)) -DECC_USER_CURVES
+CONFIGURE_ARGS := $(filter-out \
+	--enable-lighty --enable-opensslall --enable-stunnel --enable-altcertchains \
+	--enable-ocsp --enable-ocspstapling --enable-ocspstapling2 \
+	--enable-wpas --enable-fortress --enable-fastmath --enable-intelasm \
+	,$(CONFIGURE_ARGS))
+CONFIGURE_ARGS += \
+	--enable-tls13 --disable-oldtls \
+	--disable-rsa --disable-dh --disable-dsa --disable-psk \
+	--disable-sha512 --disable-sha384 \
+	--disable-chacha --disable-poly1305 --disable-aescbc --disable-aesccm \
+	--enable-ecc --disable-curve25519 --disable-ed25519 \
+	--disable-mlkem --disable-kyber --disable-dilithium \
+	--enable-sp-math-all --enable-sp=smallec256 --disable-sp-asm \
+	--enable-smallstack --enable-lowresource --enable-maxfragment \
+	--enable-singlethreaded \
+	--disable-certgen --disable-arc4 --disable-session-ticket --disable-openvpn
+endif
+
 define Build/InstallDev
 	$(INSTALL_DIR) $(1)/usr/include $(1)/usr/lib/pkgconfig
 	$(CP) $(PKG_INSTALL_DIR)/usr/include/* $(1)/usr/include/
@@ -228,6 +258,13 @@ define Package/libwolfssl-benchmark/install
 	$(CP) $(PKG_BUILD_DIR)/wolfcrypt/benchmark/.libs/benchmark $(1)/usr/bin/wolfssl-benchmark
 endef
 
+define Package/libwolfssl-examples/install
+	$(INSTALL_DIR) $(1)/usr/bin
+	$(CP) $(PKG_BUILD_DIR)/examples/server/.libs/server $(1)/usr/bin/wolfssl-server
+	$(CP) $(PKG_BUILD_DIR)/examples/client/.libs/client $(1)/usr/bin/wolfssl-client
+endef
+
+$(eval $(call BuildPackage,libwolfssl-examples))
 $(eval $(call BuildPackage,libwolfssl))
 $(eval $(call BuildPackage,libwolfsslcpu-crypto))
 $(eval $(call BuildPackage,libwolfssl-benchmark))
```

#### B.2.2 Configurations: `diffconfig-default.txt` and `diffconfig-lowram.txt`

Minimal configs (output of `./scripts/diffconfig.sh`: every symbol that differs from the tree's
defaults). `cp diffconfig-<variant>.txt .config && make defconfig` gives the full `.config`; for the
low-RAM variant this was checked to give back exactly the set of enabled symbols of the build. The
two files differ in five lines: the default variant also builds `px5g-wolfssl`, `wpad-basic-wolfssl`
and `WPA_WOLFSSL`, and has no `CONFIG_WOLFSSL_LOW_RAM`. Both select x86/64 generic, wolfSSL as the
only TLS library for `libustream-ssl` in the image, no mbedTLS packages, `libopenssl` and
`openssl-util` for comparison, and `libustream-openssl` as a module (built, not installed).

`diffconfig-lowram.txt`:

```
CONFIG_TARGET_x86=y
CONFIG_TARGET_x86_64=y
CONFIG_TARGET_x86_64_DEVICE_generic=y
CONFIG_OPENSSL_ENGINE=y
CONFIG_OPENSSL_OPTIMIZE_SPEED=y
CONFIG_OPENSSL_WITH_ASM=y
CONFIG_OPENSSL_WITH_CHACHA_POLY1305=y
CONFIG_OPENSSL_WITH_CMS=y
CONFIG_OPENSSL_WITH_DEPRECATED=y
CONFIG_OPENSSL_WITH_ERROR_MESSAGES=y
CONFIG_OPENSSL_WITH_IDEA=y
CONFIG_OPENSSL_WITH_MDC2=y
CONFIG_OPENSSL_WITH_PSK=y
CONFIG_OPENSSL_WITH_SEED=y
CONFIG_OPENSSL_WITH_SRP=y
CONFIG_OPENSSL_WITH_TLS13=y
CONFIG_OPENSSL_WITH_WHIRLPOOL=y
# CONFIG_PACKAGE_apk-mbedtls is not set
CONFIG_PACKAGE_hostapd-common=y
# CONFIG_PACKAGE_libmbedtls is not set
CONFIG_PACKAGE_libopenssl=y
CONFIG_PACKAGE_libopenssl-conf=y
# CONFIG_PACKAGE_libustream-mbedtls is not set
CONFIG_PACKAGE_libustream-openssl=m
CONFIG_PACKAGE_libustream-wolfssl=y
CONFIG_PACKAGE_libwolfssl=y
CONFIG_PACKAGE_libwolfssl-examples=y
CONFIG_PACKAGE_openssl-util=y
CONFIG_PACKAGE_ucode-mod-nl80211=y
CONFIG_PACKAGE_ucode-mod-rtnl=y
# CONFIG_PACKAGE_zlib is not set
CONFIG_WOLFSSL_HAS_NO_HW=y
CONFIG_WOLFSSL_LOW_RAM=y
CONFIG_WPA_MBO_SUPPORT=y
CONFIG_WPA_MSG_MIN_PRIORITY=3
```

`diffconfig-default.txt`:

```
CONFIG_TARGET_x86=y
CONFIG_TARGET_x86_64=y
CONFIG_TARGET_x86_64_DEVICE_generic=y
CONFIG_OPENSSL_ENGINE=y
CONFIG_OPENSSL_OPTIMIZE_SPEED=y
CONFIG_OPENSSL_WITH_ASM=y
CONFIG_OPENSSL_WITH_CHACHA_POLY1305=y
CONFIG_OPENSSL_WITH_CMS=y
CONFIG_OPENSSL_WITH_DEPRECATED=y
CONFIG_OPENSSL_WITH_ERROR_MESSAGES=y
CONFIG_OPENSSL_WITH_IDEA=y
CONFIG_OPENSSL_WITH_MDC2=y
CONFIG_OPENSSL_WITH_PSK=y
CONFIG_OPENSSL_WITH_SEED=y
CONFIG_OPENSSL_WITH_SRP=y
CONFIG_OPENSSL_WITH_TLS13=y
CONFIG_OPENSSL_WITH_WHIRLPOOL=y
# CONFIG_PACKAGE_apk-mbedtls is not set
CONFIG_PACKAGE_hostapd-common=y
# CONFIG_PACKAGE_libmbedtls is not set
CONFIG_PACKAGE_libopenssl=y
CONFIG_PACKAGE_libopenssl-conf=y
# CONFIG_PACKAGE_libustream-mbedtls is not set
CONFIG_PACKAGE_libustream-openssl=m
CONFIG_PACKAGE_libustream-wolfssl=y
CONFIG_PACKAGE_libwolfssl=y
CONFIG_PACKAGE_libwolfssl-examples=y
CONFIG_PACKAGE_openssl-util=y
CONFIG_PACKAGE_px5g-wolfssl=y
CONFIG_PACKAGE_ucode-mod-nl80211=y
CONFIG_PACKAGE_ucode-mod-rtnl=y
CONFIG_PACKAGE_wpad-basic-wolfssl=y
# CONFIG_PACKAGE_zlib is not set
CONFIG_WOLFSSL_HAS_NO_HW=y
CONFIG_WPA_MBO_SUPPORT=y
CONFIG_WPA_MSG_MIN_PRIORITY=3
CONFIG_WPA_WOLFSSL=y
```

#### B.2.3 Pinned feeds: `feeds.conf`

Copied over the tree's `feeds.conf` by `reproduce.sh` before `scripts/feeds update -a`:

```
src-git packages https://git.openwrt.org/feed/packages.git^9edff735c87c0955998e134164fd4bff8ad54b44
src-git luci https://git.openwrt.org/project/luci.git^75a306df84a020c57a631ef5b4c02ace56d93387
src-git routing https://git.openwrt.org/feed/routing.git^48c808034414a2112cf9d5261a20a320e8d0d170
src-git telephony https://git.openwrt.org/feed/telephony.git^2618106d5846a4a542fdf5809f0d3ed228ce439b
src-git video https://github.com/openwrt/video.git^094bf58da6682f895255a35a84349a79dab4bf95
```

### B.3 Host tools without root: `host-tools.sh`

Only needed if gawk, bison, flex or swig are missing. The tarball hashes are those of the files that
were used for the build in this report.

```sh
#!/bin/sh
# host-tools.sh: build gawk, bison, flex, pcre2 and swig into a private prefix (no root needed).
# Only needed if the build host lacks them (with mawk instead of gawk, "scripts/feeds update" fails
# with "awk: function asort never defined"). On Debian/Ubuntu with root, install them instead:
#   apt install build-essential clang flex bison g++ gawk gcc-multilib gettext git libncurses-dev \
#               libssl-dev python3-setuptools rsync swig unzip zlib1g-dev file wget zstd
# Usage: ./host-tools.sh [prefix]     then: export PATH=<prefix>/bin:$PATH
set -eu
PREFIX=${1:-$PWD/tools/prefix}
B=$(dirname "$PREFIX")
mkdir -p "$B/dl" "$B/build" "$PREFIX"
export PATH=$PREFIX/bin:$PATH
JOBS=${JOBS:-$(nproc)}

build() {  # name version url sha256
    f=$B/dl/$(basename "$3")
    [ -f "$f" ] || curl -fL -o "$f" "$3"
    echo "$4  $f" | sha256sum -c -
    rm -rf "$B/build/$1-$2"
    tar -C "$B/build" -xf "$f"
    (cd "$B/build/$1-$2" && ./configure --prefix="$PREFIX" && make -j"$JOBS" && make install)
}

# pcre2 first: swig's configure finds it through pcre2-config on PATH
build gawk  5.3.1  https://ftp.gnu.org/gnu/gawk/gawk-5.3.1.tar.xz 694db764812a6236423d4ff40ceb7b6c4c441301b72ad502bb5c27e00cd56f78
build bison 3.8.2  https://ftp.gnu.org/gnu/bison/bison-3.8.2.tar.xz 9bba0214ccf7f1079c5d59210045227bcf619519840ebfa80cd3849cff5a5bf2
build flex  2.6.4  https://github.com/westes/flex/releases/download/v2.6.4/flex-2.6.4.tar.gz e87aae032bf07c26f85ac0ed3250998c37621d95f8bd748b31f15b33c45ee995
build pcre2 10.44  https://github.com/PCRE2Project/pcre2/releases/download/pcre2-10.44/pcre2-10.44.tar.gz 86b9cb0aa3bcb7994faa88018292bc704cdbb708e785f7c74352ff6ea7d3175b
build swig  4.2.1  https://downloads.sourceforge.net/project/swig/swig/swig-4.2.1/swig-4.2.1.tar.gz fa045354e2d048b2cddc69579e4256245d4676894858fcf0bab2290ecf59b7d8
gawk --version | head -1; bison --version | head -1; flex --version; swig -version | sed -n 2p
```

### B.4 The measurement shim `memshim.c`

Built by `reproduce.sh shim` with the OpenWrt musl toolchain
(`x86_64-openwrt-linux-musl-gcc -O2 -fPIC -shared -o memshim.so memshim.c -ldl`, with `STAGING_DIR`
set to the tree's `staging_dir`). Source:

```c
/* LD_PRELOAD malloc tracker: live/peak heap bytes. SIGUSR1 dumps to /tmp/heap.<pid>. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>

static void *(*r_malloc)(size_t);
static void (*r_free)(void *);
static void *(*r_realloc)(void *, size_t);
static void *(*r_calloc)(size_t, size_t);
static int (*r_pmemalign)(void **, size_t, size_t);
static void *(*r_amemalign)(size_t, size_t);
static size_t (*r_usable)(void *);

static long cur, peak, nalloc, nfree, maxreq;
static uintptr_t stk_top;
static long comb_peak, comb_heap, comb_stack, stk_max;
static char boot[8192]; static size_t bootoff;

static void init(void) {
    if (r_usable) return;
    r_malloc = dlsym(RTLD_NEXT, "malloc");
    r_free = dlsym(RTLD_NEXT, "free");
    r_realloc = dlsym(RTLD_NEXT, "realloc");
    r_calloc = dlsym(RTLD_NEXT, "calloc");
    r_pmemalign = dlsym(RTLD_NEXT, "posix_memalign");
    r_amemalign = dlsym(RTLD_NEXT, "aligned_alloc");
    r_usable = dlsym(RTLD_NEXT, "malloc_usable_size");
}
static void add(void *p, size_t req) {
    if (!p) return;
    long u = (long)r_usable(p);
    long c = __atomic_add_fetch(&cur, u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&nalloc, 1, __ATOMIC_RELAXED);
    long pk = __atomic_load_n(&peak, __ATOMIC_RELAXED);
    while (c > pk && !__atomic_compare_exchange_n(&peak, &pk, c, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    if (stk_top) {
        uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
        if (sp < stk_top && stk_top - sp < (8u << 20)) {
            long sd = (long)(stk_top - sp);
            if (sd > stk_max) stk_max = sd;
            if (c + sd > comb_peak) { comb_peak = c + sd; comb_heap = c; comb_stack = sd; }
        }
    }
    long mr = __atomic_load_n(&maxreq, __ATOMIC_RELAXED);
    while ((long)req > mr && !__atomic_compare_exchange_n(&maxreq, &mr, (long)req, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}
static void sub(void *p) {
    if (!p) return;
    __atomic_sub_fetch(&cur, (long)r_usable(p), __ATOMIC_RELAXED);
    __atomic_add_fetch(&nfree, 1, __ATOMIC_RELAXED);
}
static int isboot(void *p) { return (char *)p >= boot && (char *)p < boot + sizeof boot; }

void *malloc(size_t n) { init(); void *p = r_malloc(n); add(p, n); return p; }
void *calloc(size_t a, size_t b) {
    if (!r_calloc) { void *p = boot + bootoff; bootoff += (a * b + 15) & ~15UL; memset(p, 0, a * b); return p; }
    void *p = r_calloc(a, b); add(p, a * b); return p;
}
void free(void *p) { if (!p || isboot(p)) return; init(); sub(p); r_free(p); }
void *realloc(void *p, size_t n) {
    init();
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    long old = (long)r_usable(p);
    void *q = r_realloc(p, n);
    if (q) {
        __atomic_sub_fetch(&cur, old, __ATOMIC_RELAXED);
        __atomic_sub_fetch(&nalloc, 1, __ATOMIC_RELAXED); /* add() counts it again */
        add(q, n);
    }
    return q;
}
int posix_memalign(void **o, size_t al, size_t n) { init(); int r = r_pmemalign(o, al, n); if (!r) add(*o, n); return r; }
void *aligned_alloc(size_t al, size_t n) { init(); void *p = r_amemalign(al, n); add(p, n); return p; }
void *memalign(size_t al, size_t n) { return aligned_alloc(al, n); }

static void dump(int sig) {
    (void)sig;
    char b[256], path[64];
    int n = snprintf(b, sizeof b, "heap_peak_bytes=%ld heap_cur_bytes=%ld allocs=%ld frees=%ld largest_request=%ld\n", peak, cur, nalloc, nfree, maxreq);
    snprintf(path, sizeof path, "/tmp/heap.%d", getpid());
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { if (write(fd, b, n) < 0) {} close(fd); }
}
#include <sys/mman.h>
static long resident_kb(unsigned long a, unsigned long b) {
    unsigned char vec[1024]; long pages = 0; unsigned long ps = 4096;
    for (unsigned long p = a; p < b; ) {
        unsigned long len = b - p; if (len > sizeof vec * ps) len = sizeof vec * ps;
        if (mincore((void *)p, len, vec) == 0)
            for (unsigned long i = 0; i < (len + ps - 1) / ps; i++) pages += vec[i] & 1;
        p += len;
    }
    return pages * 4;
}
static void report(void) {
    FILE *f; char l[512], out[2048] = ""; int o = 0; long heap = 0, stack = 0, anon = 0, hwm = 0, stk = 0, data = 0;
    f = fopen("/proc/self/maps", "r");
    if (f) {
        while (fgets(l, sizeof l, f)) {
            unsigned long a, b; char perms[8]; char nm[256] = "";
            int c = sscanf(l, "%lx-%lx %7s %*s %*s %*s %255[^\n]", &a, &b, perms, nm);
            if (c < 3) continue;
            if (c == 4) {
                char *base = strrchr(nm, '/'); base = base ? base + 1 : nm;
                if ((strstr(base, "ssl") || strstr(base, "crypto") || strstr(base, "wolf") || strstr(base, "ustream")) && !strstr(out, base) && o < 1900)
                    o += snprintf(out + o, sizeof out - o, "%s ", base);
            }
            if (perms[0] == 'r' && perms[1] == 'w' && (c == 3 || !strcmp(nm, "[heap]") || !strcmp(nm, "[stack]"))) {
                long r = resident_kb(a, b);
                if (c == 4 && !strcmp(nm, "[stack]")) stack += r;
                else if (c == 4 && !strcmp(nm, "[heap]")) heap += r;
                else anon += r;
            }
        }
        fclose(f);
    }
    f = fopen("/proc/self/status", "r");
    if (f) { while (fgets(l, sizeof l, f)) { if (!strncmp(l, "VmHWM:", 6)) hwm = atol(l + 6); if (!strncmp(l, "VmStk:", 6)) stk = atol(l + 6); if (!strncmp(l, "VmData:", 7)) data = atol(l + 7); } fclose(f); }
    fprintf(stderr, "[memshim COMBINED pid=%d] combined_peak_bytes=%ld (heap=%ld stack=%ld) max_stack_depth_at_alloc=%ld\n", getpid(), comb_peak, comb_heap, comb_stack, stk_max);
    fprintf(stderr, "[memshim pid=%d] heap_peak_bytes=%ld allocs=%ld largest_request=%ld | resident: stack=%ldkB brk_heap=%ldkB other_anon_rw=%ldkB | VmHWM=%ldkB VmStk=%ldkB VmData=%ldkB\n[memshim] libs: %s\n",
            getpid(), peak, nalloc, maxreq, stack, heap, anon, hwm, stk, data, out);
}
static void find_stack_top(void) {
    static char buf[32768]; int fd = open("/proc/self/maps", O_RDONLY); if (fd < 0) return;
    long n = 0, r; while ((r = read(fd, buf + n, sizeof buf - 1 - n)) > 0 && n < (long)sizeof buf - 1) n += r;
    close(fd); buf[n] = 0;
    char *p = strstr(buf, "[stack]"); if (!p) return;
    while (p > buf && p[-1] != '\n') p--;
    unsigned long a, b; if (sscanf(p, "%lx-%lx", &a, &b) == 2) stk_top = b;
}
__attribute__((constructor)) static void ctor(void) { init(); find_stack_top(); signal(SIGUSR1, dump); atexit(report); }
```

### B.5 Test driver, server policy and command files

`vm2.py` runs three servers in one process (files on 127.0.0.1:8000, TLS on 127.0.0.1:8443, plain on
8001 which is unused in the final tests), boots QEMU on `owrt.img` with `-snapshot`, types the lines
of a command file into the serial console, prints the output, and finally prints the server-side TLS
log (the negotiated protocol and cipher of each connection). It expects, in its working directory:
`owrt.img`, `serve/` (`memshim.so`, `c.pem`, `big.bin`, and for `c16.txt` also `ustream-wolfssl.so`
and `ustream-openssl.so`) and `host/` (`k.pem`, `c.pem`, `ossl.cnf`). `reproduce.sh testdata` and
`reproduce.sh run-*` create exactly that. The host certificate and data file are made with:

```
head -c 100000 /dev/urandom > big.bin
openssl ecparam -genkey -name prime256v1 -noout -out k.pem
openssl req -new -x509 -key k.pem -out c.pem -days 30 -subj /CN=hostsrv.test \
  -addext subjectAltName=DNS:hostsrv.test
```

(The certificate of the reported runs was equivalent: ECDSA P-256, CN and SAN `hostsrv.test`. The
file content of `big.bin` is irrelevant to the memory figures.)

`ossl.cnf`, verbatim:

```
openssl_conf = ssl_init
[ssl_init]
ssl_conf = ssl_sect
[ssl_sect]
system_default = sys_def
[sys_def]
MinProtocol = TLSv1.3
MaxProtocol = TLSv1.3
Ciphersuites = TLS_AES_128_GCM_SHA256
Groups = P-256
```

`vm2.py` sets `OPENSSL_CONF` to this file before importing `ssl`, so the server only speaks TLS 1.3
with `TLS_AES_128_GCM_SHA256` and P-256.

`vm2.py`, verbatim:

```python
import socket,time,sys,subprocess,os
os.environ['OPENSSL_CONF']=os.path.abspath('host/ossl.cnf')
import ssl
# usage: vm.py <cmdfile>  ; runs each line, waits for prompt
import threading,http.server,functools
h=functools.partial(http.server.SimpleHTTPRequestHandler,directory="serve")
h.log_message=lambda *a,**k:None
class Q(http.server.SimpleHTTPRequestHandler):
    def log_message(self,*a,**k):pass
srv=http.server.ThreadingHTTPServer(("127.0.0.1",8000),functools.partial(Q,directory="serve"))
threading.Thread(target=srv.serve_forever,daemon=True).start()
tlslog=[]
class T(http.server.SimpleHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*a,**k):pass
    def do_GET(self):
        c=self.connection
        tlslog.append((c.version(),c.cipher()) if hasattr(c,"version") else "plain")
        d=open("serve/big.bin","rb").read()
        self.send_response(200);self.send_header("Content-Length",str(len(d)));self.end_headers();self.wfile.write(d); self.wfile.flush()
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain("host/c.pem","host/k.pem")
tsrv=http.server.ThreadingHTTPServer(("127.0.0.1",8443),T)
tsrv.socket=ctx.wrap_socket(tsrv.socket,server_side=True)
threading.Thread(target=tsrv.serve_forever,daemon=True).start()
psrv=http.server.ThreadingHTTPServer(("127.0.0.1",8001),T)
threading.Thread(target=psrv.serve_forever,daemon=True).start()
sock="ser.sock"
if os.path.exists(sock): os.remove(sock)
q=subprocess.Popen(["qemu-system-x86_64","-enable-kvm","-m","512","-nographic","-snapshot","-drive","file=owrt.img,format=raw","-nic","user,model=virtio-net-pci","-chardev","socket,id=s0,path="+sock+",server=on,wait=off","-serial","chardev:s0","-display","none","-monitor","none"],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
time.sleep(2)
c=socket.socket(socket.AF_UNIX);c.connect(sock);c.settimeout(0.5)
P="root@OpenWrt:~# "
def rd(until,t):
    end=time.time()+t;b=""
    while time.time()<end:
        try:
            d=c.recv(65536)
            if not d:break
            b+=d.decode(errors="replace")
            if until and b.rstrip(" ").endswith("# "):break
        except socket.timeout:pass
    return b
rd(None,35); c.send(b"\r"); rd(1,5)
try:
    for line in open(sys.argv[1]):
        line=line.rstrip("\n")
        if not line.strip(): continue
        tmo=float(os.environ.get("TMO","60"))
        c.send((line+"\r").encode()); out=rd(1,tmo)
        print(out.replace("\r",""),flush=True)
finally:
    q.terminate(); q.wait(); srv.shutdown(); print('SERVER-SIDE TLS LOG:',tlslog,'ctx ciphers/min:',ctx.minimum_version)
```

`c17.txt`, the low-RAM scenario (run on the low-RAM image, whose own backend is the wolfSSL one;
it fetches only `memshim.so` and `c.pem`):

```
uci set network.lan.proto=dhcp; uci -q delete network.lan.ipaddr; uci -q delete network.lan.netmask; uci commit network; /etc/init.d/network restart >/dev/null 2>&1
sleep 8; echo "10.0.2.2 hostsrv.test" >> /etc/hosts; for f in memshim.so c.pem; do uclient-fetch -q -O /tmp/$f http://10.0.2.2:8000/$f; done
U="uclient-fetch --ca-certificate=/tmp/c.pem -O /dev/null https://hostsrv.test:8443/big.bin"
sha256sum /lib/libustream-ssl.so | cut -c1-8; ls -l /usr/lib/libwolfssl* | cut -c20-100
echo "== WOLFSSL low-RAM tls1.3"; for i in 1 2 3; do LD_PRELOAD=/tmp/memshim.so $U 2>&1 | grep -E "Download comp|rror|memshim"; done
```

`c16.txt`, the default-wolfSSL and OpenSSL scenarios (run on the *default* image, whose `libwolfssl`
is the default build). It copies each backend over `/lib/libustream-ssl.so` and prints its hash first:

```
uci set network.lan.proto=dhcp; uci -q delete network.lan.ipaddr; uci -q delete network.lan.netmask; uci commit network; /etc/init.d/network restart >/dev/null 2>&1
sleep 8; echo "10.0.2.2 hostsrv.test" >> /etc/hosts; for f in memshim.so c.pem ustream-openssl.so ustream-wolfssl.so; do uclient-fetch -q -O /tmp/$f http://10.0.2.2:8000/$f; done
U="uclient-fetch --ca-certificate=/tmp/c.pem -O /dev/null https://hostsrv.test:8443/big.bin"
cp /tmp/ustream-wolfssl.so /lib/libustream-ssl.so; sha256sum /lib/libustream-ssl.so | cut -c1-8
echo "== WOLFSSL tls1.3"; for i in 1 2 3; do LD_PRELOAD=/tmp/memshim.so $U 2>&1 | grep -E "Download comp|rror|memshim"; done
cp /tmp/ustream-openssl.so /lib/libustream-ssl.so; sha256sum /lib/libustream-ssl.so | cut -c1-8
echo "== OPENSSL tls1.3"; for i in 1 2 3; do LD_PRELOAD=/tmp/memshim.so $U 2>&1 | grep -E "Download comp|rror|memshim"; done
```

The two backends come from the packages of the respective build: `reproduce.sh build-*` extracts
`libustream-wolfssl*.apk` and `libustream-openssl*.apk` from `bin/packages/x86_64/base` with the
tree's own host `apk` tool (`apk extract --allow-untrusted`); these are the stripped files that
were measured. (The unstripped `.so` in `build_dir` has a different hash and is not what was run.)

### B.6 The driver script `reproduce.sh`

```bash
#!/bin/bash
# reproduce.sh: rebuild the OpenWrt TLS-RAM experiment and rerun the measurements.
#
# Usage: ./reproduce.sh <stage>...
#   source         clone OpenWrt at the pinned commit, pin the feeds, apply wolfssl-lowram.patch
#   build-default  build the image with the default OpenWrt wolfSSL flags   -> artifacts/default/
#   build-lowram   rebuild wolfSSL + libustream-ssl with WOLFSSL_LOW_RAM=y   -> artifacts/lowram/
#   shim           cross-compile memshim.so with the OpenWrt musl toolchain
#   testdata       generate the host certificate and the 100,000-byte test file
#   run-default    boot artifacts/default in QEMU, run c16.txt (default wolfSSL, then OpenSSL)
#   run-lowram     boot artifacts/lowram in QEMU, run c17.txt (low-RAM wolfSSL)
#   summary        print the key measurement lines of both runs
#   all            all of the above, in this order
# build-default must come before build-lowram (the second reuses the first build tree).
# Environment: WORK (default ./work), SRC (default $WORK/src), JOBS (default nproc).
# Needs: the OpenWrt build prerequisites (see host-tools.sh), qemu-system-x86_64 with KVM
# (/dev/kvm readable), python3, openssl, git, curl.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$PWD/work}
SRC=${SRC:-$WORK/src}
JOBS=${JOBS:-$(nproc)}
ART=$WORK/artifacts
OPENWRT_URL=https://git.openwrt.org/openwrt/openwrt.git
OPENWRT_COMMIT=4a686570eb6495635279273f034caa2a0f37f36e

stage_source() {
    mkdir -p "$WORK"
    [ -d "$SRC/.git" ] || git clone -b openwrt-25.12 "$OPENWRT_URL" "$SRC"
    git -C "$SRC" checkout -q "$OPENWRT_COMMIT"
    cp "$HERE/feeds.conf" "$SRC/feeds.conf"
    if git -C "$SRC" apply --check -R "$HERE/wolfssl-lowram.patch" 2>/dev/null; then
        echo "wolfssl-lowram.patch already applied"
    else
        git -C "$SRC" apply "$HERE/wolfssl-lowram.patch"
    fi
    (cd "$SRC" && ./scripts/feeds update -a && ./scripts/feeds install -a)
}

build_variant() {  # default | lowram
    v=$1
    cd "$SRC"
    cp "$HERE/diffconfig-$v.txt" .config
    make defconfig
    if [ "$v" = lowram ]; then grep -q '^CONFIG_WOLFSSL_LOW_RAM=y' .config
    else ! grep -q '^CONFIG_WOLFSSL_LOW_RAM=y' .config; fi
    # libustream-ssl is compiled against the wolfSSL headers (options.h), so both must be rebuilt
    # when the wolfSSL configuration changes
    if [ -e build_dir/target-x86_64_musl/wolfssl-regular ]; then
        make package/libs/wolfssl/clean package/libs/ustream-ssl/clean
        # build wolfSSL alone first: its ABI-suffixed package name (libwolfssl5.9.4.<hash of the config>)
        # is recorded in libwolfssl-examples when that is packaged, and a single parallel pass over
        # 'world' can record the previous variant's name ("unable to select packages" at package/install)
        make -j"$JOBS" package/libs/wolfssl/compile
    fi
    make -j"$JOBS" download
    make -j"$JOBS" world
    mkdir -p "$ART/$v"
    gunzip -c bin/targets/x86/64/openwrt-x86-64-generic-ext4-combined.img.gz > "$ART/$v/owrt.img"
    # the two libustream-ssl.so backends, taken from the packages (the stripped files that were measured)
    for b in wolfssl openssl; do
        rm -rf "$ART/$v/x-$b"; mkdir -p "$ART/$v/x-$b"
        staging_dir/host/bin/apk extract --allow-untrusted --destination "$ART/$v/x-$b" \
            bin/packages/x86_64/base/libustream-$b*.apk
        cp "$ART/$v/x-$b/lib/libustream-ssl.so" "$ART/$v/ustream-$b.so"
    done
    # the wolfSSL configuration that was actually built
    grep -E '^#define (ECC_USER_CURVES|NO_SESSION_CACHE|WOLFSSL_SMALL_STACK|HAVE_MAX_FRAGMENT|SINGLE_THREADED|GCM_SMALL|WOLFSSL_SP_MATH_ALL)$' \
        build_dir/target-x86_64_musl/wolfssl-regular/wolfssl-5.9.4-stable/wolfssl/options.h | sort -u \
        > "$ART/$v/wolfssl-options.txt"
    ls -l build_dir/target-x86_64_musl/root-x86/usr/lib/libwolfssl* > "$ART/$v/libwolfssl-size.txt"
    (cd "$ART/$v" && sha256sum owrt.img ustream-*.so > SHA256SUMS)
    echo "== $v: options.h defines found: $(tr "\n" " " < "$ART/$v/wolfssl-options.txt")"
    cat "$ART/$v/libwolfssl-size.txt"
}

stage_shim() {
    mkdir -p "$ART"
    tc=$(ls -d "$SRC"/staging_dir/toolchain-x86_64_*_musl)
    export STAGING_DIR=$SRC/staging_dir
    "$tc/bin/x86_64-openwrt-linux-musl-gcc" -O2 -fPIC -shared -o "$ART/memshim.so" "$HERE/memshim.c" -ldl
}

stage_testdata() {
    d=$WORK/testdata; mkdir -p "$d"
    [ -f "$d/big.bin" ] || head -c 100000 /dev/urandom > "$d/big.bin"
    if [ ! -f "$d/k.pem" ]; then
        openssl ecparam -genkey -name prime256v1 -noout -out "$d/k.pem"
        openssl req -new -x509 -key "$d/k.pem" -out "$d/c.pem" -days 30 -subj /CN=hostsrv.test \
            -addext subjectAltName=DNS:hostsrv.test
    fi
}

run_variant() {  # default | lowram
    v=$1; cmds=c16.txt; [ "$v" = lowram ] && cmds=c17.txt
    t=$WORK/test-$v; rm -rf "$t"; mkdir -p "$t/serve" "$t/host"
    cp "$HERE/vm2.py" "$HERE/c16.txt" "$HERE/c17.txt" "$t/"
    cp "$HERE/ossl.cnf" "$WORK/testdata/k.pem" "$WORK/testdata/c.pem" "$t/host/"
    cp "$WORK/testdata/big.bin" "$WORK/testdata/c.pem" "$ART/memshim.so" "$ART/$v"/ustream-*.so "$t/serve/"
    ln -s "$ART/$v/owrt.img" "$t/owrt.img"
    (cd "$t" && TMO=40 timeout 400 python3 -u -I vm2.py "$cmds" > out.txt)
    echo "output: $t/out.txt"
}

stage_summary() {
    for v in default lowram; do
        echo "######## $v"
        grep -hE '^(== |[0-9a-f]{8}$)|Download comp|COMBINED|memshim pid|SERVER-SIDE' "$WORK/test-$v/out.txt" | cut -c1-230
    done
}

[ $# -gt 0 ] || { sed -n '2,17p' "$0"; exit 1; }
for s in "$@"; do
    case $s in
    source) stage_source ;;
    build-default) build_variant default ;;
    build-lowram) build_variant lowram ;;
    shim) stage_shim ;;
    testdata) stage_testdata ;;
    run-default) run_variant default ;;
    run-lowram) run_variant lowram ;;
    summary) stage_summary ;;
    all) "$0" source build-default build-lowram shim testdata run-default run-lowram summary ;;
    *) echo "unknown stage $s"; exit 1 ;;
    esac
done
```

### B.7 Reading the numbers

For each run the console log contains, per process:

```
[memshim COMBINED pid=3300] combined_peak_bytes=43499 (heap=35499 stack=8000) max_stack_depth_at_alloc=11008
[memshim pid=3300] heap_peak_bytes=35499 allocs=642 largest_request=16401 | resident: stack=16kB brk_heap=4kB other_anon_rw=24kB | VmHWM=1564kB ...
[memshim] libs: libwolfssl.so.5.9.4.1587f288 libustream-ssl.so
```

The `libs:` line confirms which TLS library was actually loaded into the process; use it to catch a
wrong backend. `Download completed (100000 bytes)` confirms the transfer, and the
`SERVER-SIDE TLS LOG` at the end of the file lists `('TLSv1.3', ('TLS_AES_128_GCM_SHA256', 'TLSv1.3',
128))` once per connection. `reproduce.sh summary` prints these lines for both runs.

### B.8 Expected results

| Measurement | Expected | Reported run | Rerun (B.9) |
|---|---|---|---|
| wolfSSL low-RAM, heap peak | 35,499 B | 35,499 B | 35,499 B |
| wolfSSL low-RAM, allocations / largest request | 642 / 16,401 B | same | same |
| wolfSSL low-RAM, combined peak | 40–46 KB | 41,899–45,195 B | 40,203–45,291 B |
| wolfSSL default, heap peak | about 148,980 B | 148,979–148,981 B | 148,979–148,980 B |
| wolfSSL default, allocations / largest request | 346 / 39,688 B | same | same |
| wolfSSL default, combined peak | 160–170 KB | 164,661–166,613 B | 164,132–168,739 B |
| OpenSSL, heap peak | 562,112 B | 562,112 B | 562,112 B |
| OpenSSL, allocations / largest request | 11,346 / 32,768 B | same | same |
| OpenSSL, combined peak | 565–575 KB | 568,816–571,504 B | 570,640–573,520 B |
| `VmHWM` low-RAM / default / OpenSSL | 1.6 / 2.4 / 6.8–7.0 MB | 1,564–1,600 / 2,412–2,428 / 6,652–6,972 kB | 1,592–1,596 / 2,400–2,432 / 6,832–6,912 kB |
| `libwolfssl.so` default / low-RAM | 1,380,416 / 769,696 B | same | same |

Heap peak, allocation count and largest request are deterministic. The stack part of the combined
peak is not: the sampled maximum stack depth moved between 7.7 and 14.0 KiB over the runs of the
same binary, and the three-run ranges of the two measurement sessions overlap only partly. Compare
heap figures exactly and combined figures to within a few KiB.

### B.9 Reproduction check

The kit (`reproduce.sh` and the files above) was run on the same host on 2026-10-30, with `SRC`
pointing at the existing OpenWrt tree of the reported build (so the toolchain, host tools and
downloads were reused), as: `shim testdata build-default run-default build-lowram run-lowram`.

| Step | Outcome |
|---|---|
| `shim`, `testdata` | `memshim.so` built with the OpenWrt toolchain; certificate has CN and SAN `hostsrv.test` |
| `build-default` | 4 min 21 s (incremental), exit 0; `libwolfssl.so` 1,380,416 B |
| `run-default` | all 6 downloads complete (100,000 B), server logged TLSv1.3 / `TLS_AES_128_GCM_SHA256` for all |
| `build-lowram` | 4 min 25 s (incremental), exit 0; `libwolfssl.so` 769,696 B; all 7 `options.h` defines present |
| `run-lowram` | all 3 downloads complete, TLSv1.3 / `TLS_AES_128_GCM_SHA256` |
| Patch on a pristine checkout of the pinned commit | `git apply --check` passes |

Heap peak, allocation count and largest request were identical to the byte to the reported runs for
all three backends (B.8). The OpenSSL backend (`88dfe5ab`) and the low-RAM wolfSSL backend
(`e8249f3a`) are bit-identical to the ones measured originally. The default wolfSSL backend differs
(`f209b8da` against `50de4a04`): it records the name of the wolfSSL library it was linked against,
and that name contains a hash of the wolfSSL configuration (`libwolfssl.so.5.9.4.c88582f0` now,
`.e624513f` in the reported build, because the option list that feeds the hash gained
`WOLFSSL_LOW_RAM`); the behaviour is the same. The raw logs of this rerun are in
`rerun-default-wolfssl-vs-openssl.txt` and `rerun-lowram-wolfssl.txt`.

Two things were found and fixed while doing this, and are why the script is as it is: the OpenSSL
and wolfSSL backends must be taken from the `.apk` files, because the `build_dir` copies are
unstripped; and after a variant switch a single parallel `make world` packaged
`libwolfssl-examples` with the *previous* variant's library name and failed at `package/install` with
`unable to select packages`, so `build_variant` builds `package/libs/wolfssl` on its own first.

Not re-run end to end: the `git clone` and `scripts/feeds update` stages (the pinned feed commits are
those recorded in the original build's `feeds.buildinfo`), the toolchain build from an empty tree,
and `host-tools.sh` (its tarball hashes were checked against the files used originally, the build
commands are the ones recorded in their `config.log`).

### B.10 Repository layout (OpenWrt part)

| Path | Contents |
|---|---|
| `REPORT.md` | this report |
| `openwrt-tls-ram/reproduce.sh`, `host-tools.sh` | B.6, B.3 |
| `openwrt-tls-ram/wolfssl-lowram.patch`, `diffconfig-*.txt`, `feeds.conf` | B.2 |
| `openwrt-tls-ram/memshim.c`, `vm2.py`, `ossl.cnf`, `c16.txt`, `c17.txt` | B.4, B.5 |
| `openwrt-tls-ram/results-*.txt` | raw console logs of the reported runs |
| `openwrt-tls-ram/rerun-*.txt` | raw console logs of the rerun in B.9 |
| `src/` | the OpenWrt tree of the experiment (generated; `build_dir/`, `staging_dir/`, `dl/`, `bin/` are build output) |
| `repro-check/work/` | `artifacts/` (images, backends, shim) and `test-*` directories of the rerun |
| `backup-wolfssl-only/` | older configs and Makefiles, and earlier versions of this file |
| `tools/` | locally built gawk, bison, flex, swig, pcre2 and their tarballs |

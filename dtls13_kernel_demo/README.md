# Reasoning notes: "should this run in the kernel?"

This file is the *thinking* behind the project: why each measurement was
designed the way it was, and what went wrong on the way. Read it first if you
are about to design a demo or a benchmark that is meant to prove something.
**Building and running** below is self-contained.

The project itself: a DTLS 1.3 server running inside the Linux kernel
(`libwolfssl.ko` + a custom module), with a userspace client outside the VM,
built to demonstrate an advantage of in-kernel DTLS. It took **three attempts**
to find an advantage that survived scrutiny. The two failures are more
instructive than the success.

---

## Building and running

Source trees are not vendored here. Both are pinned; fetch exactly these
commits or the measurements will not reproduce:

| tree | commit | configure |
|---|---|---|
| `wolfssl/` | `252c3b1aa3c8cbb5f5c052c9199f27a8a7ea2c29` | `--enable-linuxkm --enable-tls13 --enable-dtls --enable-dtls13 --with-linux-source=/lib/modules/$(uname -r)/build`, then `make module` |
| `wolfssl-user/` | same commit | `--enable-dtls13 --enable-static --disable-shared` |
| `wolfssl-bench/` | same commit | as above plus `--enable-intelasm --enable-sp-asm --enable-aesni` (only for `bench-vs-openssl/`) |
| `openssl/` | `2564382c0bfd5c8d5cfb28671457878ee61ed6df` | `./Configure` (DTLS 1.3 is on by default) |

A pinned shallow fetch is 31 MB rather than a full clone:

```sh
mkdir wolfssl && cd wolfssl && git init -q
git remote add origin https://github.com/wolfssl/wolfssl.git
git fetch --depth 1 origin 252c3b1aa3c8cbb5f5c052c9199f27a8a7ea2c29
git checkout FETCH_HEAD && cd ..
git clone -q wolfssl wolfssl-user      # inherits the pin
```

**The guest kernel image** is not vendored either. `/boot/vmlinuz-$(uname -r)`
is mode `0600`, so rather than use `sudo`, pull the identical image from the
distro archive and extract it to `demo/kernel/vmlinuz`:

```sh
apt-get download linux-image-unsigned-$(uname -r)     # no sudo
dpkg-deb -x linux-image-unsigned-*.deb extracted
cp extracted/boot/vmlinuz-* demo/kernel/vmlinuz
```

Then:

```sh
./demo/build-all.sh     # module, guest programs, host clients, initramfs
./demo/demo.sh          # DTLS 1.3 handshake: host client -> in-kernel server
./demo/bench.sh         # entropy: kernel vs /dev/urandom vs vDSO getrandom
./demo/isolation.sh     # key-extraction attack, lockdown off then on
./demo/relay.sh         # per-message handoff cost and its decomposition
./bench-vs-openssl/build.sh && ./bench-vs-openssl/run.sh
```

Each `demo/*.sh` boots its own QEMU guest and tears it down. No `sudo` anywhere.

### Two things that will bite you

**The guest boots the host's own kernel image**, so the already-installed
headers match and no kernel compile is needed. The flip side: a kernel module
is bound to one exact kernel build. Once the host is upgraded and the old
headers are uninstalled, the modules cannot be rebuilt and the existing `.ko`
files are the only copies. If `apt-cache policy linux-headers-<version>` says
`Candidate: (none)`, you are re-pinning the whole demo to your current kernel,
which means a fresh `vmlinuz`, a module rebuild, and re-running the benchmarks.

**Do not use Docker for this.** A container shares the host kernel, so `insmod`
loads into *your* kernel and a fault panics the machine. QEMU boots a separate
kernel, so a bug kills only the VM.

## The one-paragraph version

We tried to show in-kernel DTLS was faster at getting entropy. It isn't;
userspace `getrandom()` beats it 2.3x, because the vDSO does ChaCha20 in
userspace with no syscall and no locking, while `get_random_bytes()` pays for
being callable from any context. We then tried a hardware-TRNG framing; the
arithmetic killed it before we built anything, because any real TRNG's latency
swamps the syscall boundary. What actually holds up is **architectural**:
in-kernel DTLS wins when the data is already in the kernel or must never leave
it (key isolation). It does not win by computing anything faster. And the cost
it removes is not the syscall; that is only 708 ns of an 18,200 ns round trip.
It is the **per-message rendezvous with another thread**: 95% of the tax.

---

## Scoreboard of the three framings

| framing | prediction | measured | verdict |
|---|---|---|---|
| entropy: kernel vs `/dev/urandom` | kernel wins ~2.8x | 191-216 vs 540-614 ns | true but a **strawman** |
| entropy: kernel vs vDSO `getrandom()` | kernel loses | 191-216 vs 81-103 ns | **kernel loses ~2.3x** |
| TRNG served over DTLS | wash; hw latency dominates | not built; crossover at 2.6 µs | **killed on paper** |
| key isolation | kernel wins, conditionally | stolen from userspace both ways; kernel key needs lockdown | **true, conditional** |
| per-message handoff, kernel-originated data | kernel wins big | 17 ns vs ~18,200 ns; 1.00 ctx switch/msg | **true** |
| *…and the cause is?* | assumed "syscall boundary" | syscall = 708 ns (3.9%); rendezvous = 17,480 ns (96.1%) | **wrong cause, corrected** |

---

## Rules that earned their keep

Each of these is here because ignoring it cost time on *this* project.

### 1. Predict the result before you measure it

Before building the entropy demo I wrote down: *the kernel will beat
`/dev/urandom`, lose to vDSO `getrandom()`, and the end-to-end version will show
nothing.* All three came true. A prediction you write down converts a
measurement from "a number" into "a test of your model", and if you can't
predict it, you don't understand the system well enough to design the
experiment yet.

The corollary: **if the demo only makes sense when it wins, don't build it.**
Ask "what will I conclude if it comes out the other way?" If the answer is
"I'd quietly not ship this," that's a rigged experiment.

### 2. Isolate the variable

The first entropy design had the kernel server in a QEMU guest and the
userspace server on the bare-metal host. That measures virtualization and slirp
networking, not entropy. Fix: **both arms in the same guest, same kernel, same
CPU**; three DTLS servers on ports 11111/11112/11113 differing only in entropy
source.

If two arms of a comparison differ in more than one way, you have not measured
the thing you named.

### 3. Pick the strongest opponent, not the most convenient one

`/dev/urandom` is the obvious userspace entropy source and the kernel beats it
2.8x. It is also the *wrong* opponent: Linux 6.11+ serves `getrandom()` from
the vDSO with no syscall at all, and that beats the kernel module. Shipping the
`/dev/urandom`-only comparison would have been beating a strawman, and the first
knowledgeable reviewer would have said so.

Ask: **what is the best thing the other side could do?** Benchmark that.

### 4. Check that your cause can actually account for your effect

The end-to-end version of the entropy demo looked like a kernel win: **2.68 ms
vs 3.98 ms** round trip, userspace 49% slower. It is tempting to call that gap
noise, but it is probably not; it is systematic, and it comes from the two
servers being different programs with different I/O structures (the kernel
module polls with `msleep`; the userspace server blocks on `recvfrom`).

The gap is real. **Entropy is simply not what causes it.** Measured separately,
the per-call entropy difference is **382 ns**: `read(/dev/urandom)` at 589 ns
against `get_random_bytes()` at 207 ns. That is **0.029% of the 1.30 ms gap**.
Make entropy completely free in the kernel and the gap goes from 1.3000 ms to
1.2996 ms.

And the conclusion does not depend on 382 ns being exact. That figure comes from
a tight loop (warm cache, trained branch predictor), so a single cold call in a
real request plausibly costs more. Even at 10x it would be 0.29%. You would have
to be wrong by a factor of ~3,400 before entropy could explain the gap. *"This
cause cannot explain this effect under any plausible value"* is a far stronger
claim than any single number.

So the error to guard against is **misattribution, not imprecision**: pointing at
a real difference and crediting a cause that accounts for a fraction of a percent
of it. Before claiming X causes Y, compute the largest share of Y that X could
possibly explain. We made this same mistake again later, blaming the syscall for
the relay tax when the rendezvous was 96% of it (rule 8).

### 5. Separate fixed cost from marginal cost

"Why is the kernel 2.8x slower at 32 bytes?" was unanswerable until we swept size
from 32 B to 4096 B and fit a line (R² = 0.9999):

```
get_random_bytes()   fixed 319.2 ns/call  +  2.246 ns/byte
getrandom() [vDSO]   fixed 122.1 ns/call  +  1.508 ns/byte
```

The 2.8x at 32 bytes was **almost entirely ~197 ns of fixed per-call overhead**,
and it decays to 1.50x by 4 KB. One ratio at one size told us nothing about
mechanism; the decomposition told us everything, and it also flagged a
per-byte gap we could *not* explain, which got labelled as unexplained rather
than papered over.

### 6. Run a positive control before believing a negative result

The key-isolation scan reported **0 matches in kernel memory** twice, and both
times it looked like a clean win for the kernel. Both were scanner bugs. The
fix that should have been there from the start: **first search for something
you know is present.** If the control doesn't hit, a null result means the
instrument is broken, not that the thing is absent.

### 7. Distrust results that flatter you

Every single bug in this project failed in the direction that made the kernel
look better:

- static linking silently disabled the vDSO, making the opponent 7x slower
- the kcore scan couldn't read kernel memory, hiding the kernel's key
- the module-section bug missed `.rodata`, hiding it again

None of these would have been caught by "does the number look plausible?";
they all produced plausible numbers. They were caught by asking *why* a result
came out favourably. **A pleasing result deserves more scrutiny than a
disappointing one**, precisely because you won't go looking.

### 8. Decompose before you attribute a cause

The ~18 us relay tax looked like "crossing the user/kernel boundary is
expensive". It isn't. Measuring the same read()/write() pair with the daemon
never sleeping gave **708 ns**, so the crossing is only **3.9%** of the cost:

```
full ping-pong round trip : 18188 ns   (1.00 ctx switch/msg)
  syscall + 1KiB copy     :   708 ns   ( 3.9%)  0.0000 ctx switch/msg
  thread rendezvous       : 17480 ns   (96.1%)  <-- the real cost
```

**The rendezvous is 25x the boundary crossing.** Naming the wrong cause would
have produced the wrong advice ("avoid syscalls") instead of the right one
("avoid waking another thread per message"). A number is not a mechanism until
you have separated its parts.

### 9. Know the shape of the workload

TLS amortizes: `sendfile()` covers ~44 packets per boundary crossing. A VPN
cannot; it forwards individual packets and pays fixed costs per packet. Same
crypto, opposite conclusion about where the code should live.

Before comparing architectures, ask: **per-stream or per-packet? What can be
batched?** Fixed overheads only matter when you can't amortize them.

---

## The dead ends, with their one-line fixes

| what bit us | fix |
|---|---|
| Docker treated as kernel isolation; containers share the host kernel, so `insmod` loads into *your* kernel | use QEMU; reuse the host's own `vmlinuz` to skip a kernel build |
| entropy speed as the kernel's advantage | abandon it; the CRNG is the same for all three sources; only the access path differs |
| `-static` disabled the vDSO `getrandom()` path (490 ns vs 65 ns) | build the userspace opponent **dynamically**, ship glibc in the initramfs |
| `/proc/kcore` read at a module's virtual address returned nothing | it's an ELF core file: parse `PT_LOAD` and translate vaddr → file offset |
| `base`+`size` from `/proc/modules` missed `.rodata` | since 6.4 module memory is split (`MOD_TEXT`/`MOD_RODATA`/…); read `/sys/module/<m>/sections/` |
| positive control used `MODULE_DESCRIPTION`, never found | `.modinfo` is **freed after load**; pick a string that survives, e.g. a printk format |
| `-device e1000` left the guest with no NIC | `CONFIG_E1000=m` and the module isn't in the initramfs; use `virtio-net-pci` (`=y`) |
| `exec sh` in `/init` with stdin from `/dev/null` → EOF → init exits → panic | run a shell only when stdin is a tty, otherwise idle |
| added a `dmesg` poller and saw every line twice | printk already goes to `ttyS0`; `-nographic` routes it to the terminal |
| `pkill -f qemu-system-x86_64` killed the launcher | the pattern matches the wrapper shell too; anchor it: `pkill -f "^qemu-system-x86_64"` |
| wolfSSL headers vs kernel headers: no include order works | **split into two translation units**; don't fight `#undef current/min/max` |

---

## Checklist: does this belong in the kernel?

Work down the list. The first "yes" is usually the real reason; if you reach
the bottom with no yes, it belongs in userspace.

1. **Is the data already in the kernel, or must it never leave?**
   VPN datapath, storage transport, filesystem encryption. This is the strongest
   reason and usually the only one you need. Measured cost of getting it wrong:
   ~16 µs and 2 context switches per message.

2. **Is the cost per-packet with nothing to amortize over?**
   At 10 Gbps you have 1,200 ns (3,600 cycles) per packet; at 100 Gbps, 120 ns.
   A *rendezvous* (~17,500 ns) does not fit; a *syscall* (~708 ns) nearly does.
   That distinction decides the design. Batching amortizes the rendezvous --
   at batch 64 the tax falls to ~981 ns/packet, 19x better -- which is why
   every serious userspace VPN batches. The escape hatch: **DPDK/AF_XDP solves
   the same problem in the opposite direction**, keeping a userspace thread
   permanently awake so there is never a rendezvous at all. The enemy is the
   per-message thread handoff, not userspace and not syscalls.

3. **Must the key be unreachable from userspace?**
   Real, but conditional; root plus `/proc/kcore` defeats it unless
   `lockdown=confidentiality` is on. State the precondition or the claim is
   false.

4. **Does it need hardware only the kernel can drive?**
   NIC crypto offload needs the kernel to own session state
   (`CONFIG_TLS_DEVICE=y`). Note this is TLS-only in practice; no meaningful
   DTLS offload hardware exists.

5. **Is it a compliance scope argument?**
   One FIPS-validated module covering all kernel crypto is a genuine business
   reason even with zero performance benefit.

6. **Is it "it'll be faster"?**
   Almost always no. Userspace can be *faster*; vDSO `getrandom()` beats
   `get_random_bytes()`; DPDK beats in-kernel WireGuard. Kernel code doesn't run
   faster for being in the kernel; it just skips a boundary.

### And the counterweight, which belongs in every writeup

Mainline Linux **deliberately keeps the handshake out of the kernel**.
`CONFIG_TLS=m` is the record layer only; `CONFIG_NET_HANDSHAKE=y` exists so
in-kernel consumers can call *out* to a userspace daemon to do the handshake
and then hand keys down. The reason: handshake code is X.509/ASN.1 parsing,
historically the richest CVE source in any TLS stack, and mainline won't run it
in ring 0.

wolfSSL's `linuxkm` runs the whole handshake in-kernel. That's what made this
demo possible, and it's a real tradeoff, not a free win: a parsing bug that
would kill a process in userspace becomes a panic or privilege escalation.

---

## The corrected VPN arithmetic

An earlier version of these notes plugged the batch-size-1 ping-pong (~18.7 us)
into a per-packet budget table and concluded a userspace VPN was "14x over
budget at 10 Gbps". That was **wrong in a way worth preserving**: it used a
latency number as a throughput cost, and no competent userspace VPN runs at
batch size 1.

Amortizing one rendezvous over a batch of B, `cost(B) = 708 + 17480/B`:

| batch | ns/packet | 1 Gbps | 10 Gbps | 25 Gbps | 100 Gbps |
|---|---|---|---|---|---|
| 1 | 18188 | 1.5x | 15.2x | 37.9x | 151.6x |
| 8 | 2893 | 0.2x | 2.4x | 6.0x | 24.1x |
| 32 | 1254 | 0.1x | 1.0x | 2.6x | 10.5x |
| 64 | 981 | 0.1x | **0.8x** | 2.0x | 8.2x |
| 256 | 776 | 0.1x | 0.6x | 1.6x | 6.5x |

(multiples of the per-packet time budget; <1.0 means it fits)

The honest reading is more interesting than the original overstatement:

- **Batching buys ~19x**: the difference between hopeless and nearly viable.
  This is why every serious userspace VPN batches, and it is a real engineering
  answer, not a workaround.
- **Even so, 10 Gbps only just fits at batch 64 (0.8x)**, and that consumes
  essentially the whole budget, leaving nothing for the encryption itself.
- **25 Gbps and up never fits**, at any batch size, because the residual
  708 ns/packet syscall+copy floor does not amortize away.

So the conclusion survives, but for a sharper reason: it is not that userspace
is slow, it is that the *floor* (708 ns) is too high once the per-packet budget
drops below a microsecond. Kernel-bypass (DPDK/AF_XDP) attacks exactly that
floor by removing the syscall too, which is why it reaches 100 Gbps from
userspace.

---

## The principle underneath all of it

> In-kernel code doesn't win by computing faster. It wins by **not handing work
> to another thread**, or by putting data **where the other side can't reach**.

Every honest result here reduces to that sentence. Entropy lost because there
was nothing to remove; all three sources bottom out in the same ChaCha20 CRNG,
and the vDSO had already removed the syscall. The relay won because there was a
per-message thread handoff to remove, and that handoff (17,480 ns) dwarfs the
syscall it was hiding behind (708 ns). Key isolation won because it changes who
can reach the memory, not how fast anything runs.

When someone claims a kernel-space performance advantage, ask which of those
two it is. If it's neither, the claim is probably measurement error, and if
it's flattering, check twice.

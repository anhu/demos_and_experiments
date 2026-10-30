#!/bin/bash
# Build a minimal initramfs containing busybox, libwolfssl.ko and wolfdtls_srv.ko.
# The guest boots straight into /init, brings up eth0 via DHCP (QEMU user-net),
# loads both modules, and leaves a shell on the console.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOP="$(dirname "$HERE")"
WOLFSSL="$TOP/wolfssl"
ROOT="$HERE/initramfs"
OUT="$HERE/kernel/initramfs.cpio.gz"

LIBWOLFSSL_KO="$WOLFSSL/linuxkm/libwolfssl.ko"
DEMO_KO="$HERE/kmod/wolfdtls_srv.ko"
USER_SRV="$HERE/userland/dtls_srv_user"
KEYSCAN="$HERE/userland/keyscan"
RELAYD="$HERE/userland/relayd"
HSBENCH="$HERE/userland/dtls_hs_bench"

for f in "$LIBWOLFSSL_KO" "$DEMO_KO" "$USER_SRV" "$KEYSCAN" "$RELAYD" "$HSBENCH"; do
    [ -f "$f" ] || { echo "error: missing $f" >&2; exit 1; }
done

rm -rf "$ROOT"
mkdir -p "$ROOT"/{bin,sbin,proc,sys,dev,lib/modules}

# Static busybox gives us a userland without pulling in a libc.
BB=$(command -v busybox)
if ldd "$BB" >/dev/null 2>&1; then
    echo "note: busybox is dynamically linked; copying its shared libraries too"
    cp "$BB" "$ROOT/bin/busybox"
    mkdir -p "$ROOT/lib64" "$ROOT/lib/x86_64-linux-gnu"
    ldd "$BB" | grep -oE '/[^ ]+\.so[^ ]*' | while read -r lib; do
        mkdir -p "$ROOT$(dirname "$lib")"
        cp -L "$lib" "$ROOT$lib"
    done
else
    cp "$BB" "$ROOT/bin/busybox"
fi

# busybox applet symlinks
for applet in sh ls cat mount umount insmod rmmod lsmod dmesg ip ifconfig \
              udhcpc sleep echo grep ps kill poweroff mkdir; do
    ln -sf /bin/busybox "$ROOT/bin/$applet"
done

cp "$LIBWOLFSSL_KO" "$DEMO_KO" "$ROOT/lib/modules/"

# The userspace comparison server is dynamically linked ON PURPOSE.  A static
# glibc binary does NOT use the vDSO getrandom() fast path -- measured 490 ns
# static vs 65 ns dynamic for the same code -- which would quietly hand the
# in-kernel server an unfair win against a crippled opponent.
cp "$USER_SRV" "$ROOT/bin/dtls_srv_user"
copy_libs() {
    ldd "$1" 2>/dev/null | grep -oE '/[^ ]+\.so[^ ]*' | while read -r lib; do
        mkdir -p "$ROOT$(dirname "$lib")"
        cp -Ln "$lib" "$ROOT$lib" 2>/dev/null || true
    done
}
copy_libs "$USER_SRV"
cp "$KEYSCAN" "$ROOT/bin/keyscan"
copy_libs "$KEYSCAN"
cp "$RELAYD" "$ROOT/bin/relayd"
copy_libs "$RELAYD"
cp "$HSBENCH" "$ROOT/bin/dtls_hs_bench"
copy_libs "$HSBENCH"

cat > "$ROOT/init" <<'INIT'
#!/bin/sh
mount -t proc  none /proc
mount -t sysfs none /sys
mount -t securityfs none /sys/kernel/security 2>/dev/null
mount -t devtmpfs none /dev 2>/dev/null

echo "=============================================="
echo " DTLS 1.3 in-kernel demo guest"
echo " kernel: $(uname -r)"
echo "=============================================="

# QEMU user-mode networking: guest is 10.0.2.15, gateway/host is 10.0.2.2.
# The virtio NIC may come up as eth0 or enp0s2 depending on naming, so find it
# rather than assuming.
ip link set lo up
NIC=$(ls /sys/class/net | grep -v '^lo$' | head -1)
if [ -z "$NIC" ]; then
    echo "ERROR: no network interface found"
else
    echo "--- configuring $NIC ---"
    ip link set "$NIC" up
    ip addr add 10.0.2.15/24 dev "$NIC"
    ip route add default via 10.0.2.2
    ip addr show "$NIC" | grep -E 'inet |state'
fi

echo "--- loading libwolfssl.ko ---"
insmod /lib/modules/libwolfssl.ko || echo "FAILED to load libwolfssl.ko"

echo "--- loading wolfdtls_srv.ko ---"
insmod /lib/modules/wolfdtls_srv.ko || echo "FAILED to load wolfdtls_srv.ko"

echo "--- lsmod ---"
lsmod

echo "--- starting userspace comparison server on UDP 11112 ---"
# Same DTLS 1.3 server, same cert, same request protocol, same guest --
# the only difference is that its entropy comes from read(/dev/urandom)
# instead of the kernel's get_random_bytes().
/bin/dtls_srv_user --urandom -p 11112 >/dev/null 2>&1 &

# And a third server using getrandom(), which on this kernel is served by the
# vDSO with no syscall at all.  This is the honest opponent for the in-kernel
# server -- /dev/urandom alone would be a strawman.
/bin/dtls_srv_user --getrandom -p 11113 >/dev/null 2>&1 &

# Decomposition: first measure the syscall+copy cost with NO thread
# rendezvous (relay_read never blocks), so it can be subtracted from the full
# ping-pong later.
echo "--- measuring SYSCALL-ONLY cost (no wakeup, no context switch) ---"
echo 1 > /sys/module/wolfdtls_srv/parameters/relay_always_ready
/bin/relayd --syscall-bench 200000
echo 0 > /sys/module/wolfdtls_srv/parameters/relay_always_ready

# The userspace relay daemon: models handing kernel-originated data out to a
# userspace DTLS implementation and back.
echo "--- starting userspace relay daemon (/dev/wolfdtls_relay) ---"
/bin/relayd &

# Give the servers a moment to finish loading their keys, then run the
# key-extraction attack against both architectures.
sleep 2
echo
echo "=============================================================="
echo " KEY ISOLATION TEST"
echo " kernel lockdown: $(cat /sys/kernel/security/lockdown 2>/dev/null || echo unavailable)"
echo " Both servers use the SAME P-256 key. Can we steal it?"
echo "=============================================================="
echo
echo "--- [1] scanning all USERSPACE process memory ---"
keyscan procs
echo
echo "--- [2] POSITIVE CONTROL: can we read kernel module memory at all? ---"
echo "    (a printk format string that lives in the module's .rodata)"
keyscan kcore "waiting for DTLS 1.3 handshake"
echo
echo "--- [3] scanning loaded KERNEL MODULE images for the PRIVATE KEY ---"
keyscan kcore
echo "=============================================================="
echo

# Handshake benchmark, run INSIDE the guest over loopback.
# On the host this same test goes through QEMU slirp, where ~6 ms of a ~9 ms
# handshake is network RTT -- too blunt to compare server implementations.
# Over loopback the network almost disappears and what is left is the servers.
echo
echo "=============================================================="
echo " DTLS 1.3 FULL HANDSHAKE: in-kernel vs userspace (loopback)"
echo " same guest, same cert, same cipher; only the server differs"
echo "=============================================================="
dmesg -n 1        # console off: printk cost must not land on one side only
for pass in 1 2 3; do
  /bin/dtls_hs_bench -h 127.0.0.1 -p 11111 -n 40 -d 30 -l kernel
  /bin/dtls_hs_bench -h 127.0.0.1 -p 11112 -n 40 -d 30 -l user-urnd
  /bin/dtls_hs_bench -h 127.0.0.1 -p 11113 -n 40 -d 30 -l user-vdso
  echo "  ---"
done
dmesg -n 7        # console back on, demo.sh greps these
echo "=============================================================="

echo
echo "  in-kernel  DTLS 1.3 server : UDP 11111  (get_random_bytes)"
echo "  userspace  DTLS 1.3 server : UDP 11112  (read /dev/urandom)"
echo "  userspace  DTLS 1.3 server : UDP 11113  (getrandom, vDSO)"
echo "Run the benchmark client on the host now."
echo

# No log streamer is needed: printk already writes to ttyS0, which -nographic
# routes to the terminal, so pr_info() from the module appears live.

# An interactive console gets a shell.  When the VM is driven non-interactively
# (stdin redirected from /dev/null) sh would read EOF and exit immediately,
# init would return, and the kernel would panic -- so idle instead.
if [ -t 0 ]; then
    sh
fi
while true; do sleep 3600; done
INIT
chmod +x "$ROOT/init"

mkdir -p "$(dirname "$OUT")"
( cd "$ROOT" && find . -print0 | cpio --null -ov --format=newc 2>/dev/null ) \
    | gzip -9 > "$OUT"

echo "built $OUT ($(du -h "$OUT" | cut -f1))"

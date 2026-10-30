#!/bin/bash
# Boot the demo guest under QEMU.
#
# Networking: QEMU user-mode ("slirp") networking with a UDP port forward, so
# the host can reach the in-kernel DTLS server:
#
#     host 127.0.0.1:11111/udp  ->  guest 10.0.2.15:11111/udp
#
# No sudo, no tap device, no bridge required.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL="$HERE/kernel/vmlinuz"
INITRD="$HERE/kernel/initramfs.cpio.gz"
PORT="${PORT:-11111}"      # in-kernel server
UPORT="${UPORT:-11112}"    # userspace server, /dev/urandom
VPORT="${VPORT:-11113}"    # userspace server, getrandom() via vDSO

for f in "$KERNEL" "$INITRD"; do
    [ -f "$f" ] || { echo "error: missing $f (run mkinitramfs.sh first)" >&2; exit 1; }
done

# NIC choice: virtio-net is CONFIG_VIRTIO_NET=y (built into this kernel).
# e1000 is CONFIG_E1000=m, and that module is not in our initramfs, so an
# e1000 device would leave the guest with no usable network interface.
#
# -nographic puts the guest console on this terminal. Exit with Ctrl-a x.
# Use KVM when the current user can open /dev/kvm; fall back to TCG emulation.
ACCEL=()
if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    ACCEL=(-enable-kvm -cpu host)
    echo "note: using KVM acceleration"
else
    echo "note: /dev/kvm not accessible, falling back to TCG (slower)"
fi

exec qemu-system-x86_64 \
    "${ACCEL[@]}" \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -m 1024 \
    -smp 2 \
    -nographic \
    -no-reboot \
    -netdev "user,id=n0,hostfwd=udp:127.0.0.1:${PORT}-:${PORT},hostfwd=udp:127.0.0.1:${UPORT}-:${UPORT},hostfwd=udp:127.0.0.1:${VPORT}-:${VPORT}" \
    -device virtio-net-pci,netdev=n0 \
    -append "console=ttyS0 panic=1 ${LOCKDOWN:+lockdown=$LOCKDOWN}" \
    "$@"

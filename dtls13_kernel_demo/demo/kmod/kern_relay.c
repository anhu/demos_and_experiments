// SPDX-License-Identifier: GPL-2.0
/*
 * kern_relay.c - models the cost of doing DTLS in USERSPACE when the data
 * being protected originates in the KERNEL.
 *
 * If a kernel subsystem (VPN data plane, storage transport, telemetry) has a
 * message that must go out over DTLS, and the DTLS implementation lives in
 * userspace, then every message has to be handed out to a userspace daemon and
 * the result handed back.  That round trip costs two context switches and two
 * copies per message.  With in-kernel DTLS the message never leaves the kernel
 * and that cost is zero.
 *
 * This file exposes /dev/wolfdtls_relay.  A userspace daemon read()s a message
 * and write()s an acknowledgement.  relay_bench() enqueues N messages, waits
 * for N acks, and reports the total time -- i.e. the per-message tax that
 * in-kernel DTLS avoids.
 *
 * IMPORTANT: this is a separate translation unit on purpose.  It includes
 * <linux/miscdevice.h>, <linux/fs.h> and <linux/uaccess.h>, which cannot be
 * included in the same file as wolfssl/ssl.h -- linuxkm_wc_port.h #undefs
 * `current`, `min` and `max`, which those headers rely on.  Splitting the
 * module into two objects is the clean fix for that whole class of conflict.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <net/sock.h>

#include "kern_relay.h"

/*
 * Set a receive timeout on a kernel socket.
 *
 * This lives here rather than in wolfdtls_main.c because it needs struct sock
 * from <net/sock.h>, which cannot be included alongside wolfssl/ssl.h.  It is
 * the reason a blocking receive is possible at all: without it the DTLS server
 * had to poll with msleep(), which added milliseconds of sleep to every
 * handshake round trip and made any latency measurement meaningless.
 */
void wdtls_sock_set_rcvtimeo(struct socket *sock, long secs)
{
	lock_sock(sock->sk);
	sock->sk->sk_rcvtimeo = secs * HZ;
	release_sock(sock->sk);
}


#define RELAY_MSG_SZ 1024

static DECLARE_WAIT_QUEUE_HEAD(msg_wq);   /* daemon waits here for work    */
static DECLARE_WAIT_QUEUE_HEAD(ack_wq);   /* bench waits here for acks     */

static struct mutex relay_lock;
static unsigned int pending;              /* messages available to read    */
static unsigned int acked;                /* acks received                 */
static unsigned int target;               /* messages this run expects     */
static bool relay_active;

static unsigned char msg_payload[RELAY_MSG_SZ];

/*
 * When set, relay_read() returns immediately instead of sleeping.  This lets a
 * userspace daemon spin through read()/write() with NO thread rendezvous, so
 * the syscall+copy cost can be measured separately from the wakeup/scheduling
 * cost.  Toggle at runtime:
 *   echo 1 > /sys/module/wolfdtls_srv/parameters/relay_always_ready
 */
static bool relay_always_ready;
module_param(relay_always_ready, bool, 0644);
MODULE_PARM_DESC(relay_always_ready,
		 "relay_read() never blocks (for isolating syscall cost)");

static ssize_t relay_read(struct file *f, char __user *ubuf, size_t len,
			  loff_t *off)
{
	size_t n = min_t(size_t, len, (size_t)RELAY_MSG_SZ);

	if (!relay_always_ready) {
		if (wait_event_interruptible(msg_wq,
					     pending > 0 || !relay_active))
			return -ERESTARTSYS;
	}

	if (!relay_active)
		return 0;

	mutex_lock(&relay_lock);
	if (pending)
		pending--;
	mutex_unlock(&relay_lock);

	/* This copy_to_user() is precisely the cost in-kernel DTLS avoids. */
	if (copy_to_user(ubuf, msg_payload, n))
		return -EFAULT;

	return (ssize_t)n;
}

static ssize_t relay_write(struct file *f, const char __user *ubuf, size_t len,
			   loff_t *off)
{
	mutex_lock(&relay_lock);
	acked++;
	mutex_unlock(&relay_lock);
	wake_up(&ack_wq);
	return (ssize_t)len;
}

static const struct file_operations relay_fops = {
	.owner = THIS_MODULE,
	.read  = relay_read,
	.write = relay_write,
};

static struct miscdevice relay_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "wolfdtls_relay",
	.fops  = &relay_fops,
	.mode  = 0666,
};

/*
 * Enqueue n messages, wait for n acks, return elapsed nanoseconds.
 * Returns 0 on success, negative on error.
 */
int relay_bench(unsigned int n, unsigned long long *elapsed_ns)
{
	unsigned long long t0, t1;
	unsigned int i;

	if (!relay_active)
		return -ENODEV;

	mutex_lock(&relay_lock);
	pending = 0;
	acked = 0;
	target = n;
	mutex_unlock(&relay_lock);

	t0 = local_clock();

	for (i = 0; i < n; i++) {
		mutex_lock(&relay_lock);
		pending++;
		mutex_unlock(&relay_lock);
		wake_up(&msg_wq);

		/* Wait for this message's ack before sending the next, so we
		 * measure round-trip latency rather than batch throughput.
		 */
		if (wait_event_interruptible_timeout(ack_wq,
						     acked > i, HZ * 5) <= 0)
			return -ETIMEDOUT;
	}

	t1 = local_clock();
	*elapsed_ns = t1 - t0;
	return 0;
}

/*
 * The in-kernel baseline: the same N messages handled entirely in kernel
 * space, with no userspace involvement at all.
 */
int relay_bench_inkernel(unsigned int n, unsigned long long *elapsed_ns)
{
	unsigned char *sink;
	unsigned long long t0, t1;
	unsigned int i;

	sink = kmalloc(RELAY_MSG_SZ, GFP_KERNEL);
	if (!sink)
		return -ENOMEM;

	t0 = local_clock();
	for (i = 0; i < n; i++) {
		/* Same 1 KiB message movement, kernel-internal. */
		memcpy(sink, msg_payload, RELAY_MSG_SZ);
		barrier();
	}
	t1 = local_clock();

	kfree(sink);
	*elapsed_ns = t1 - t0;
	return 0;
}

int relay_init(void)
{
	int ret;

	mutex_init(&relay_lock);
	memset(msg_payload, 0xA5, sizeof(msg_payload));

	ret = misc_register(&relay_dev);
	if (ret)
		return ret;

	relay_active = true;
	pr_info("wolfdtls_srv: /dev/wolfdtls_relay registered\n");
	return 0;
}

void relay_exit(void)
{
	if (!relay_active)
		return;
	relay_active = false;
	wake_up_all(&msg_wq);
	wake_up_all(&ack_wq);
	misc_deregister(&relay_dev);
}

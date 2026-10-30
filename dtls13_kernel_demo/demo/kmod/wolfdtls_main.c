// SPDX-License-Identifier: GPL-2.0
/*
 * wolfdtls_srv.c - a DTLS 1.3 echo server that runs entirely in kernel space.
 *
 * Uses the DTLS 1.3 implementation exported by libwolfssl.ko (built with
 * --enable-linuxkm --enable-dtls13).  The UDP transport is a kernel socket
 * (sock_create_kern), and wolfSSL is driven through custom I/O callbacks
 * backed by kernel_recvmsg()/kernel_sendmsg(), because the in-kernel build
 * has no userspace file descriptors to hand to wolfSSL_set_fd().
 *
 * The handshake and the echo loop run on a kthread.  After a session ends the
 * thread loops back and waits for the next client.
 */

/*
 * Include order is load-bearing, in both directions:
 *
 *  - wolfSSL must come FIRST.  wolfssl/ssl.h pulls in
 *    linuxkm/linuxkm_wc_port.h, which includes the kernel headers it needs in
 *    a controlled order and then installs its own string shims.  Including
 *    <linux/module.h> first means fortify-string.h has already made strlen a
 *    macro, and the shim at linuxkm_wc_port.h:481 fails to compile.
 *
 *  - Almost nothing may come AFTER.  linuxkm_wc_port.h deliberately #undefs
 *    `current`, `min` and `max` (lines 1870-1877) as part of keeping wolfCrypt
 *    self-contained.  Any kernel header whose inline functions use those --
 *    <net/sock.h> is the big one -- will fail to compile if included later.
 *
 * Happily linuxkm_wc_port.h already includes module.h, moduleparam.h, delay.h,
 * kthread.h, slab.h, sched.h and net.h for us (they are gated only on
 * WC_CONTAINERIZE_THIS, which is off in a non-PIE build).  linux/net.h is what
 * declares sock_create_kern/kernel_bind/kernel_recvmsg/kernel_sendmsg, so the
 * socket API is available without reaching for <net/sock.h>.
 */
#include <wolfssl/ssl.h>
#include <wolfssl/error-ssl.h>

#include <linux/in.h>

#include "certs_data.h"
#include "kern_relay.h"

#define DRV "wolfdtls_srv"
#define pr_fmt_drv(fmt) DRV ": " fmt

static ushort port = 11111;
module_param(port, ushort, 0444);
MODULE_PARM_DESC(port, "UDP port to listen on (default 11111)");

#define RXBUF_SZ 1500
#define ENT_CHUNK 32   /* bytes fetched per entropy call, both servers */
#define RECV_TIMEO_SEC 2   /* bounded block, so the kthread stays stoppable */

struct dtls_ctx {
	struct socket *sock;
	struct sockaddr_in peer;
	bool have_peer;
};

static struct task_struct *srv_thread;
static WOLFSSL_CTX *ssl_ctx;

/*
 * ---------------------------------------------------------------------------
 * wolfSSL transport callbacks
 * ---------------------------------------------------------------------------
 * wolfSSL calls these instead of recv()/send().  Return values follow the
 * wolfSSL CBIO contract: >0 = bytes moved, or a WOLFSSL_CBIO_ERR_* code.
 */

static int dtls_recv_cb(WOLFSSL *ssl, char *buf, int sz, void *vctx)
{
	struct dtls_ctx *ctx = vctx;
	struct sockaddr_in from;
	struct msghdr msg;
	struct kvec iov;
	int ret;

	(void)ssl;

	/*
	 * Blocking receive.  sk_rcvtimeo is set on the socket at bind time (via
	 * wdtls_sock_set_rcvtimeo, which lives in kern_relay.c because it needs
	 * <net/sock.h>), so this returns -EAGAIN periodically rather than
	 * blocking forever: that keeps the kthread stoppable for rmmod and gives
	 * wolfSSL the timeout it needs to run DTLS retransmit timers.
	 *
	 * This used to poll with MSG_DONTWAIT + msleep(5), which added up to
	 * 5 ms of sleep to every handshake round trip and made latency
	 * measurements meaningless.
	 */
	if (kthread_should_stop())
		return WOLFSSL_CBIO_ERR_GENERAL;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &from;
	msg.msg_namelen = sizeof(from);

	iov.iov_base = buf;
	iov.iov_len = sz;

	ret = kernel_recvmsg(ctx->sock, &msg, &iov, 1, sz, 0);

	if (ret == -EAGAIN || ret == -EWOULDBLOCK)
		return WOLFSSL_CBIO_ERR_TIMEOUT;
	if (ret == -ETIMEDOUT)
		return WOLFSSL_CBIO_ERR_TIMEOUT;
	/* ICMP port-unreachable from a peer that has not started yet. */
	if (ret == -ECONNREFUSED)
		return WOLFSSL_CBIO_ERR_WANT_READ;
	if (ret < 0)
		return WOLFSSL_CBIO_ERR_GENERAL;
	if (ret == 0)
		return WOLFSSL_CBIO_ERR_CONN_CLOSE;

	/*
	 * First datagram of a session establishes the peer.  Afterwards we
	 * drop anything arriving from a different address, which is the
	 * minimal form of the association check a real DTLS server needs.
	 */
	if (!ctx->have_peer) {
		memcpy(&ctx->peer, &from, sizeof(from));
		ctx->have_peer = true;
		pr_info(pr_fmt_drv("client connected from %pI4:%u\n"),
			&ctx->peer.sin_addr.s_addr,
			ntohs(ctx->peer.sin_port));
	} else if (from.sin_addr.s_addr != ctx->peer.sin_addr.s_addr ||
		   from.sin_port != ctx->peer.sin_port) {
		return WOLFSSL_CBIO_ERR_WANT_READ;
	}

	return ret;
}

static int dtls_send_cb(WOLFSSL *ssl, char *buf, int sz, void *vctx)
{
	struct dtls_ctx *ctx = vctx;
	struct msghdr msg;
	struct kvec iov;
	int ret;

	(void)ssl;

	if (!ctx->have_peer)
		return WOLFSSL_CBIO_ERR_GENERAL;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &ctx->peer;
	msg.msg_namelen = sizeof(ctx->peer);

	iov.iov_base = buf;
	iov.iov_len = sz;

	ret = kernel_sendmsg(ctx->sock, &msg, &iov, 1, sz);

	if (ret == -EAGAIN || ret == -EWOULDBLOCK)
		return WOLFSSL_CBIO_ERR_WANT_WRITE;
	if (ret < 0)
		return WOLFSSL_CBIO_ERR_GENERAL;

	return ret;
}

/*
 * ---------------------------------------------------------------------------
 * Session handling
 * ---------------------------------------------------------------------------
 */

/*
 * Request protocol, identical to the userspace comparison server:
 *
 *   BENCH <n>  run <n> entropy fetches of ENT_CHUNK bytes, reply with the
 *              measured cost per call.  This is the number the demo is about:
 *              it times the entropy source alone, with no network in the path.
 *   ENT <n>    reply with <n> bytes of fresh entropy, hex encoded.
 *   <other>    echo it back.
 *
 * In-kernel this is get_random_bytes(): a direct call into the per-CPU
 * ChaCha20 CRNG.  No syscall, no copy_to_user.
 */
static int handle_request(const char *req, int len, char *out, int outsz)
{
	unsigned long long t0, t1, delta;
	unsigned int n, i;

	if (len >= 6 && memcmp(req, "BENCH ", 6) == 0) {
		unsigned int sz = ENT_CHUNK;
		unsigned char *big;
		const char *sp;
		char cbuf[16];

		/* "BENCH <n> [size]" -- size lets us separate the fixed
		 * per-call overhead from the actual ChaCha20 work.
		 */
		sp = strchr(req + 6, ' ');
		if (sp) {
			int clen = (int)(sp - (req + 6));

			if (clen <= 0 || clen >= (int)sizeof(cbuf))
				return scnprintf(out, outsz, "ERR bad count\n");
			memcpy(cbuf, req + 6, clen);
			cbuf[clen] = '\0';
			if (kstrtouint(cbuf, 10, &n) != 0)
				return scnprintf(out, outsz, "ERR bad count\n");
			if (kstrtouint(sp + 1, 10, &sz) != 0 ||
			    sz == 0 || sz > 4096)
				return scnprintf(out, outsz, "ERR bad size\n");
		} else if (kstrtouint(req + 6, 10, &n) != 0) {
			return scnprintf(out, outsz, "ERR bad count\n");
		}

		if (n == 0 || n > 10000000)
			return scnprintf(out, outsz, "ERR bad count\n");

		big = kmalloc(sz, GFP_KERNEL);
		if (!big)
			return scnprintf(out, outsz, "ERR nomem\n");

		/* Warm the path so the first-call cost is not counted. */
		for (i = 0; i < 1000; i++)
			get_random_bytes(big, sz);

		t0 = local_clock();
		for (i = 0; i < n; i++)
			get_random_bytes(big, sz);
		t1 = local_clock();

		kfree(big);
		delta = t1 - t0;
		pr_info(pr_fmt_drv("BENCH %u calls x %u B, %llu ns\n"),
			n, sz, delta);

		return scnprintf(out, outsz,
			"impl=get_random_bytes() space=kernel calls=%u bytes=%u "
			"total_ns=%llu ns_per_call=%llu.%02llu\n",
			n, sz, delta,
			delta / n, ((delta % n) * 100) / n);
	}

	if (len >= 6 && memcmp(req, "RELAY ", 6) == 0) {
		unsigned long long ns_user = 0, ns_kern = 0;
		int rc;

		if (kstrtouint(req + 6, 10, &n) != 0 || n == 0 || n > 100000)
			return scnprintf(out, outsz, "ERR bad count\n");

		rc = relay_bench_inkernel(n, &ns_kern);
		if (rc)
			return scnprintf(out, outsz, "ERR inkernel %d\n", rc);

		rc = relay_bench(n, &ns_user);
		if (rc)
			return scnprintf(out, outsz,
				"ERR relay %d (is relayd running in the guest?)\n",
				rc);

		return scnprintf(out, outsz,
			"msgs=%u bytes=1024 "
			"inkernel_ns_per_msg=%llu userspace_roundtrip_ns_per_msg=%llu "
			"tax_ns_per_msg=%llu\n",
			n, ns_kern / n, ns_user / n,
			(ns_user - ns_kern) / n);
	}

	if (len >= 4 && memcmp(req, "ENT ", 4) == 0) {
		unsigned char big[64];

		if (kstrtouint(req + 4, 10, &n) != 0 || n == 0 || n > sizeof(big))
			return scnprintf(out, outsz, "ERR bad count\n");

		get_random_bytes(big, n);
		return scnprintf(out, outsz, "entropy=%*phN\n", (int)n, big);
	}

	/* Default: echo, so the original demo behaviour still works. */
	if (len > outsz)
		len = outsz;
	memcpy(out, req, len);
	return len;
}

static void run_session(struct dtls_ctx *ctx)
{
	WOLFSSL *ssl;
	char *buf;
	char reply[512];
	int ret;

	buf = kmalloc(RXBUF_SZ, GFP_KERNEL);
	if (!buf)
		return;

	ssl = wolfSSL_new(ssl_ctx);
	if (!ssl) {
		pr_err(pr_fmt_drv("wolfSSL_new failed\n"));
		kfree(buf);
		return;
	}

	wolfSSL_SetIOReadCtx(ssl, ctx);
	wolfSSL_SetIOWriteCtx(ssl, ctx);

	/*
	 * DTLS 1.3 stateless cookie exchange (HelloRetryRequest).  This is what
	 * makes the server resistant to blind amplification; the build enables
	 * WOLFSSL_SEND_HRR_COOKIE, so turn it on for the session.
	 */
	ret = wolfSSL_send_hrr_cookie(ssl, NULL, 0);
	if (ret != WOLFSSL_SUCCESS)
		pr_warn(pr_fmt_drv("send_hrr_cookie: %d\n"), ret);

	pr_info(pr_fmt_drv("waiting for DTLS 1.3 handshake...\n"));

	ret = wolfSSL_accept(ssl);
	if (ret != WOLFSSL_SUCCESS) {
		int err = wolfSSL_get_error(ssl, ret);

		pr_err(pr_fmt_drv("handshake failed: ret=%d err=%d\n"),
		       ret, err);
		goto out;
	}

	pr_info(pr_fmt_drv("HANDSHAKE OK: %s / %s\n"),
		wolfSSL_get_version(ssl),
		wolfSSL_get_cipher(ssl));

	/* Echo loop: bounce application data back until the client shuts down. */
	for (;;) {
		if (kthread_should_stop())
			break;

		ret = wolfSSL_read(ssl, buf, RXBUF_SZ - 1);
		if (ret > 0) {
			int rlen;

			buf[ret] = '\0';
			pr_info(pr_fmt_drv("recv %d bytes: %s\n"), ret, buf);

			rlen = handle_request(buf, ret, reply, sizeof(reply));

			if (wolfSSL_write(ssl, reply, rlen) != rlen) {
				pr_err(pr_fmt_drv("write failed\n"));
				break;
			}
			continue;
		}

		if (wolfSSL_get_error(ssl, ret) == WOLFSSL_ERROR_WANT_READ)
			continue;

		/* Clean shutdown or hard error: either way, session is over. */
		pr_info(pr_fmt_drv("session closed (ret=%d err=%d)\n"),
			ret, wolfSSL_get_error(ssl, ret));
		break;
	}

	wolfSSL_shutdown(ssl);

out:
	wolfSSL_free(ssl);
	kfree(buf);
}

static int srv_fn(void *unused)
{
	struct dtls_ctx ctx;
	struct sockaddr_in addr;
	int ret;

	(void)unused;

	while (!kthread_should_stop()) {
		memset(&ctx, 0, sizeof(ctx));

		ret = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM,
				       IPPROTO_UDP, &ctx.sock);
		if (ret < 0) {
			pr_err(pr_fmt_drv("sock_create_kern: %d\n"), ret);
			return ret;
		}

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(port);

		ret = kernel_bind(ctx.sock, (struct sockaddr *)&addr,
				  sizeof(addr));
		if (ret < 0) {
			pr_err(pr_fmt_drv("bind to port %u: %d\n"), port, ret);
			sock_release(ctx.sock);
			return ret;
		}

		/* Bounded blocking receive; see kern_relay.c. */
		wdtls_sock_set_rcvtimeo(ctx.sock, RECV_TIMEO_SEC);

		pr_info(pr_fmt_drv("listening on UDP 0.0.0.0:%u\n"), port);

		run_session(&ctx);

		sock_release(ctx.sock);
	}

	return 0;
}

/*
 * ---------------------------------------------------------------------------
 * Module init / exit
 * ---------------------------------------------------------------------------
 */

static int __init wolfdtls_init(void)
{
	int ret;

	pr_info(pr_fmt_drv("loading, wolfSSL %s\n"), wolfSSL_lib_version());

	if (wolfSSL_Init() != WOLFSSL_SUCCESS) {
		pr_err(pr_fmt_drv("wolfSSL_Init failed\n"));
		return -EINVAL;
	}

	ssl_ctx = wolfSSL_CTX_new(wolfDTLSv1_3_server_method());
	if (!ssl_ctx) {
		pr_err(pr_fmt_drv("CTX_new(wolfDTLSv1_3_server_method) failed\n"));
		ret = -ENOMEM;
		goto err_cleanup;
	}

	/*
	 * NO_FILESYSTEM is set in the kernel build, so the certificate and key
	 * are compiled in as DER buffers (see certs_data.h).
	 */
	ret = wolfSSL_CTX_use_certificate_buffer(ssl_ctx, server_ecc_der,
						 server_ecc_der_len,
						 WOLFSSL_FILETYPE_ASN1);
	if (ret != WOLFSSL_SUCCESS) {
		pr_err(pr_fmt_drv("use_certificate_buffer: %d\n"), ret);
		ret = -EINVAL;
		goto err_free_ctx;
	}

	ret = wolfSSL_CTX_use_PrivateKey_buffer(ssl_ctx, ecc_key_der,
						ecc_key_der_len,
						WOLFSSL_FILETYPE_ASN1);
	if (ret != WOLFSSL_SUCCESS) {
		pr_err(pr_fmt_drv("use_PrivateKey_buffer: %d\n"), ret);
		ret = -EINVAL;
		goto err_free_ctx;
	}

	/* Demo server does not ask the client for a certificate. */
	wolfSSL_CTX_set_verify(ssl_ctx, WOLFSSL_VERIFY_NONE, NULL);

	wolfSSL_CTX_SetIORecv(ssl_ctx, dtls_recv_cb);
	wolfSSL_CTX_SetIOSend(ssl_ctx, dtls_send_cb);

	ret = relay_init();
	if (ret)
		pr_warn(pr_fmt_drv("relay_init failed: %d\n"), ret);

	srv_thread = kthread_run(srv_fn, NULL, DRV);
	if (IS_ERR(srv_thread)) {
		ret = PTR_ERR(srv_thread);
		pr_err(pr_fmt_drv("kthread_run: %d\n"), ret);
		srv_thread = NULL;
		goto err_free_ctx;
	}

	return 0;

err_free_ctx:
	wolfSSL_CTX_free(ssl_ctx);
	ssl_ctx = NULL;
err_cleanup:
	wolfSSL_Cleanup();
	return ret;
}

static void __exit wolfdtls_exit(void)
{
	relay_exit();
	if (srv_thread)
		kthread_stop(srv_thread);
	if (ssl_ctx)
		wolfSSL_CTX_free(ssl_ctx);
	wolfSSL_Cleanup();
	pr_info(pr_fmt_drv("unloaded\n"));
}

module_init(wolfdtls_init);
module_exit(wolfdtls_exit);

/* libwolfssl.ko exports its API with EXPORT_SYMBOL_NS_GPL into this namespace. */
MODULE_IMPORT_NS("WOLFSSL");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("DTLS 1.3 in-kernel demo");
MODULE_DESCRIPTION("In-kernel DTLS 1.3 echo server on top of libwolfssl.ko");

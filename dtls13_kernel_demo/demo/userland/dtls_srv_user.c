// SPDX-License-Identifier: GPL-2.0
/*
 * dtls_srv_user.c - userspace counterpart of wolfdtls_srv.ko.
 *
 * Deliberately mirrors the kernel module: same DTLS 1.3 server method, same
 * embedded ECC certificate, same BENCH/ENT/echo request protocol.  The only
 * intended difference is where entropy comes from:
 *
 *     kernel module : get_random_bytes()      -- direct call, no syscall
 *     this program  : read(2) on /dev/urandom -- syscall + copy_to_user
 *                     or getrandom() via the vDSO, with --getrandom
 *
 * It runs INSIDE the same QEMU guest as the kernel module, on the same CPU and
 * the same kernel, so the comparison isolates the entropy source instead of
 * measuring virtualization and network differences.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

#include "../kmod/certs_data.h"

#define ENT_CHUNK 32       /* must match the kernel module */
#define RXBUF_SZ  1500

enum ent_src { SRC_URANDOM, SRC_GETRANDOM };

static enum ent_src ent_src = SRC_URANDOM;
static int urandom_fd = -1;

static const char *src_name(void)
{
	return ent_src == SRC_URANDOM ? "read(/dev/urandom)" : "getrandom()";
}

/* One entropy fetch, matching what the kernel module does per call. */
static int get_entropy(unsigned char *buf, size_t n)
{
	if (ent_src == SRC_GETRANDOM) {
		if (getrandom(buf, n, 0) != (ssize_t)n)
			return -1;
		return 0;
	}

	if (read(urandom_fd, buf, n) != (ssize_t)n)
		return -1;
	return 0;
}

static unsigned long long now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned long long)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static int handle_request(const char *req, int len, char *out, int outsz)
{
	unsigned char ent[ENT_CHUNK];
	(void)ent;
	unsigned long long t0, t1, delta;
	unsigned int n, i;

	if (len >= 6 && memcmp(req, "BENCH ", 6) == 0) {
		unsigned int sz = ENT_CHUNK;
		unsigned char *big;
		const char *sp;

		/* "BENCH <n> [size]" -- see the kernel module for why. */
		n = (unsigned int)strtoul(req + 6, NULL, 10);
		sp = strchr(req + 6, ' ');
		if (sp)
			sz = (unsigned int)strtoul(sp + 1, NULL, 10);

		if (n == 0 || n > 10000000)
			return snprintf(out, outsz, "ERR bad count\n");
		if (sz == 0 || sz > 4096)
			return snprintf(out, outsz, "ERR bad size\n");

		big = malloc(sz);
		if (!big)
			return snprintf(out, outsz, "ERR nomem\n");

		/* Same warmup as the kernel side. */
		for (i = 0; i < 1000; i++)
			get_entropy(big, sz);

		t0 = now_ns();
		for (i = 0; i < n; i++) {
			if (get_entropy(big, sz) != 0) {
				free(big);
				return snprintf(out, outsz, "ERR entropy read\n");
			}
		}
		t1 = now_ns();

		free(big);
		delta = t1 - t0;
		fprintf(stderr, "dtls_srv_user: BENCH %u calls x %u B, %llu ns\n",
			n, sz, delta);

		return snprintf(out, outsz,
			"impl=%s space=user calls=%u bytes=%u "
			"total_ns=%llu ns_per_call=%llu.%02llu\n",
			src_name(), n, sz, delta,
			delta / n, ((delta % n) * 100) / n);
	}

	if (len >= 4 && memcmp(req, "ENT ", 4) == 0) {
		unsigned char big[64];
		int off = 0;

		n = (unsigned int)strtoul(req + 4, NULL, 10);
		if (n == 0 || n > sizeof(big))
			return snprintf(out, outsz, "ERR bad count\n");

		if (get_entropy(big, n) != 0)
			return snprintf(out, outsz, "ERR entropy read\n");

		off = snprintf(out, outsz, "entropy=");
		for (i = 0; i < n && off < outsz - 3; i++)
			off += snprintf(out + off, outsz - off, "%02x", big[i]);
		off += snprintf(out + off, outsz - off, "\n");
		return off;
	}

	if (len > outsz)
		len = outsz;
	memcpy(out, req, len);
	return len;
}

int main(int argc, char **argv)
{
	WOLFSSL_CTX *ctx;
	WOLFSSL *ssl;
	int sock, port = 11112, ret;
	struct sockaddr_in addr, peer;
	socklen_t peerlen;
	char buf[RXBUF_SZ], reply[512];

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--getrandom"))
			ent_src = SRC_GETRANDOM;
		else if (!strcmp(argv[i], "--urandom"))
			ent_src = SRC_URANDOM;
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			port = atoi(argv[++i]);
	}

	if (ent_src == SRC_URANDOM) {
		urandom_fd = open("/dev/urandom", O_RDONLY);
		if (urandom_fd < 0) {
			perror("open /dev/urandom");
			return 1;
		}
	}

	printf("dtls_srv_user: entropy source = %s\n", src_name());

	wolfSSL_Init();

	ctx = wolfSSL_CTX_new(wolfDTLSv1_3_server_method());
	if (!ctx) {
		fprintf(stderr, "CTX_new failed\n");
		return 1;
	}

	/* Same embedded DER cert/key the kernel module uses. */
	if (wolfSSL_CTX_use_certificate_buffer(ctx, server_ecc_der,
			server_ecc_der_len, WOLFSSL_FILETYPE_ASN1)
	    != WOLFSSL_SUCCESS) {
		fprintf(stderr, "use_certificate_buffer failed\n");
		return 1;
	}
	if (wolfSSL_CTX_use_PrivateKey_buffer(ctx, ecc_key_der,
			ecc_key_der_len, WOLFSSL_FILETYPE_ASN1)
	    != WOLFSSL_SUCCESS) {
		fprintf(stderr, "use_PrivateKey_buffer failed\n");
		return 1;
	}
	wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);

	for (;;) {
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock < 0) {
			perror("socket");
			return 1;
		}

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(port);

		if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("bind");
			return 1;
		}

		printf("dtls_srv_user: listening on UDP 0.0.0.0:%d\n", port);
		fflush(stdout);

		/*
		 * DTLS server idiom: peek the first datagram to learn the peer,
		 * then connect(2) the UDP socket to it so wolfSSL can use plain
		 * read/write on the fd.
		 */
		peerlen = sizeof(peer);
		ret = recvfrom(sock, buf, sizeof(buf), MSG_PEEK,
			       (struct sockaddr *)&peer, &peerlen);
		if (ret < 0) {
			perror("recvfrom");
			close(sock);
			continue;
		}

		if (connect(sock, (struct sockaddr *)&peer, peerlen) < 0) {
			perror("connect");
			close(sock);
			continue;
		}

		printf("dtls_srv_user: client connected from %s:%d\n",
		       inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

		ssl = wolfSSL_new(ctx);
		if (!ssl) {
			close(sock);
			continue;
		}
		wolfSSL_set_fd(ssl, sock);
		wolfSSL_dtls_set_peer(ssl, &peer, peerlen);
		wolfSSL_send_hrr_cookie(ssl, NULL, 0);

		if (wolfSSL_accept(ssl) != WOLFSSL_SUCCESS) {
			fprintf(stderr, "dtls_srv_user: handshake failed err=%d\n",
				wolfSSL_get_error(ssl, -1));
			wolfSSL_free(ssl);
			close(sock);
			continue;
		}

		printf("dtls_srv_user: HANDSHAKE OK: %s / %s\n",
		       wolfSSL_get_version(ssl), wolfSSL_get_cipher(ssl));
		fflush(stdout);

		for (;;) {
			ret = wolfSSL_read(ssl, buf, sizeof(buf) - 1);
			if (ret <= 0)
				break;
			buf[ret] = '\0';
			printf("dtls_srv_user: recv %d bytes: %s\n", ret, buf);
			fflush(stdout);

			ret = handle_request(buf, ret, reply, sizeof(reply));
			if (wolfSSL_write(ssl, reply, ret) != ret)
				break;
		}

		printf("dtls_srv_user: session closed\n");
		fflush(stdout);
		wolfSSL_shutdown(ssl);
		wolfSSL_free(ssl);
		close(sock);
	}

	wolfSSL_CTX_free(ctx);
	wolfSSL_Cleanup();
	return 0;
}

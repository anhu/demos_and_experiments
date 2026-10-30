// SPDX-License-Identifier: GPL-2.0
/*
 * dtls_hs_bench.c - DTLS 1.3 full-handshake benchmark against a RUNNING server.
 *
 * This is the kernel-vs-userland DTLS comparison that the rest of the demo
 * never actually made. The relay benchmark measured a handoff, not DTLS; the
 * entropy benchmark measured one primitive. This measures the protocol itself:
 * how long a full DTLS 1.3 handshake takes when the server is the in-kernel
 * module, versus when it is a userspace process.
 *
 * Runs on the HOST and drives a server inside the guest, so the network path
 * is identical for every target:
 *
 *   -p 11111   in-kernel   wolfdtls_srv.ko
 *   -p 11112   userspace   dtls_srv_user --urandom
 *   -p 11113   userspace   dtls_srv_user --getrandom
 *
 * Each iteration uses a FRESH socket, so every handshake is a full one (new
 * source port = new peer, no resumption). Both server types call
 * wolfSSL_send_hrr_cookie(), so the handshake shape is the same for all three.
 *
 * It also prints the negotiated cipher and group: if those differ between
 * targets the comparison is invalid, and you should see that rather than
 * discover it later.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static int cmp_d(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
	const char *host = "127.0.0.1", *label = "?";
	int port = 11111, n = 100, delay_ms = 0;
	WOLFSSL_CTX *ctx;
	struct sockaddr_in srv;
	double *samples;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-h") && i + 1 < argc) host = argv[++i];
		else if (!strcmp(argv[i], "-n") && i + 1 < argc) n = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-l") && i + 1 < argc) label = argv[++i];
		/* -d: idle gap between handshakes. These are single-session demo
		 * servers: they close and re-bind between clients, and a ClientHello
		 * landing in that window is simply dropped, costing the client a full
		 * ~1 s DTLS retransmit. The gap is NOT timed; only wolfSSL_connect is. */
		else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay_ms = atoi(argv[++i]);
	}

	wolfSSL_Init();
	ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
	if (!ctx) { fprintf(stderr, "CTX_new failed\n"); return 1; }
	wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);

	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons(port);
	inet_pton(AF_INET, host, &srv.sin_addr);

	samples = calloc(n, sizeof(double));
	if (!samples) return 1;

	int warm = n / 10 < 3 ? 3 : n / 10;
	int done = 0, failed = 0;
	const char *ver = "?", *cipher = "?", *group = "?";

	for (int i = 0; i < n + warm; i++) {
		if (delay_ms) usleep(delay_ms * 1000);
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0) break;
		struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
			close(fd); break;
		}

		WOLFSSL *ssl = wolfSSL_new(ctx);
		wolfSSL_set_fd(ssl, fd);
		wolfSSL_dtls_set_peer(ssl, &srv, sizeof(srv));

		double t0 = now_s();
		int rc = wolfSSL_connect(ssl);
		double el = now_s() - t0;

		if (rc != WOLFSSL_SUCCESS) {
			failed++;
		} else {
			if (i == 0) {
				ver = wolfSSL_get_version(ssl);
				cipher = wolfSSL_get_cipher_name(ssl);
				group = wolfSSL_get_curve_name(ssl);
			}
			if (i >= warm && done < n) samples[done++] = el;
		}
		wolfSSL_shutdown(ssl);
		wolfSSL_free(ssl);
		close(fd);
	}

	if (done == 0) {
		printf("%-10s port %d: NO SUCCESSFUL HANDSHAKES (%d failed)\n",
		       label, port, failed);
		return 1;
	}

	qsort(samples, done, sizeof(double), cmp_d);
	double sum = 0;
	for (int i = 0; i < done; i++) sum += samples[i];
	double mean = sum / done;
	double med = samples[done / 2];
	double p95 = samples[(int)(done * 0.95) >= done ? done - 1 : (int)(done * 0.95)];

	printf("%-10s port %-6d n=%-4d  mean %8.0f us   median %8.0f us   p95 %8.0f us   %6.1f hs/sec%s\n",
	       label, port, done, mean * 1e6, med * 1e6, p95 * 1e6, 1.0 / mean,
	       failed ? "  [SOME FAILED]" : "");
	fprintf(stderr, "    %-10s negotiated: %s / %s / %s\n", label, ver, cipher, group);

	free(samples);
	wolfSSL_CTX_free(ctx);
	wolfSSL_Cleanup();
	return 0;
}

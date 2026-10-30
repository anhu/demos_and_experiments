// SPDX-License-Identifier: GPL-2.0
/*
 * dtls_bench_client.c - DTLS 1.3 client that drives the BENCH/ENT protocol.
 *
 * Runs on the host, outside the VM.  Connects to one of the two servers in the
 * guest, sends a request, prints the reply, and reports the end-to-end round
 * trip time separately from the server-reported entropy cost.
 *
 *   ./dtls_bench_client -p 11111 -r "BENCH 200000"
 */

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

static unsigned long long now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned long long)t.tv_sec * 1000000000ull + t.tv_nsec;
}

int main(int argc, char **argv)
{
	const char *host = "127.0.0.1";
	const char *req = "BENCH 100000";
	const char *ca = NULL;
	int port = 11111, sock, ret;
	struct sockaddr_in addr;
	WOLFSSL_CTX *ctx;
	WOLFSSL *ssl;
	char buf[1024];
	unsigned long long t0, t1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-p") && i + 1 < argc)
			port = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-h") && i + 1 < argc)
			host = argv[++i];
		else if (!strcmp(argv[i], "-r") && i + 1 < argc)
			req = argv[++i];
		else if (!strcmp(argv[i], "-A") && i + 1 < argc)
			ca = argv[++i];
	}

	wolfSSL_Init();
	ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
	if (!ctx) {
		fprintf(stderr, "CTX_new failed\n");
		return 1;
	}

	if (ca && wolfSSL_CTX_load_verify_locations(ctx, ca, NULL)
		  != WOLFSSL_SUCCESS) {
		fprintf(stderr, "failed to load CA %s\n", ca);
		return 1;
	}
	if (!ca)
		wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, host, &addr.sin_addr);

	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect");
		return 1;
	}

	ssl = wolfSSL_new(ctx);
	wolfSSL_set_fd(ssl, sock);
	wolfSSL_dtls_set_peer(ssl, &addr, sizeof(addr));

	if (wolfSSL_connect(ssl) != WOLFSSL_SUCCESS) {
		fprintf(stderr, "handshake failed err=%d\n",
			wolfSSL_get_error(ssl, -1));
		return 1;
	}

	printf("  connected: %s / %s\n",
	       wolfSSL_get_version(ssl), wolfSSL_get_cipher(ssl));
	printf("  request:   %s\n", req);

	/* Time the full request/response round trip, network included. */
	t0 = now_ns();
	if (wolfSSL_write(ssl, req, (int)strlen(req)) <= 0) {
		fprintf(stderr, "write failed\n");
		return 1;
	}
	ret = wolfSSL_read(ssl, buf, sizeof(buf) - 1);
	t1 = now_ns();

	if (ret <= 0) {
		fprintf(stderr, "read failed err=%d\n",
			wolfSSL_get_error(ssl, ret));
		return 1;
	}
	buf[ret] = '\0';

	printf("  reply:     %s", buf);
	printf("  round_trip_ns=%llu (server work + DTLS + network)\n",
	       t1 - t0);

	wolfSSL_shutdown(ssl);
	wolfSSL_free(ssl);
	close(sock);
	wolfSSL_CTX_free(ctx);
	wolfSSL_Cleanup();
	return 0;
}

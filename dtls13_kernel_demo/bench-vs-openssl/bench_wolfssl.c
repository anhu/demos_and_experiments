// DTLS 1.3 full-handshake benchmark - wolfSSL.
// Deliberately mirrors bench_openssl.c line for line so the comparison is
// apples-to-apples:
//  - same PEM cert/key, same P-256 curve
//  - server on ONE unconnected socket learning the peer per handshake;
//    client on a FRESH socket per iteration, so every handshake is full
//  - SSL_VERIFY_NONE on the client in both benchmarks
//  - NO HelloRetryRequest cookie. This matters: HRR address validation adds a
//    whole extra round trip, and the OpenSSL benchmark does not do it either.
//    Enabling it on only one side would be a rigged comparison.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

static int PORT = 14444;
static const char *GROUP = NULL;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void die(const char *m)
{
    fprintf(stderr, "wolfssl-bench: %s\n", m);
    exit(1);
}

static WOLFSSL_CTX *mkctx(int server, const char *cert, const char *key)
{
    WOLFSSL_CTX *c = wolfSSL_CTX_new(server ? wolfDTLSv1_3_server_method()
                                            : wolfDTLSv1_3_client_method());
    if (!c) die("CTX_new");
    if (server) {
        if (wolfSSL_CTX_use_certificate_file(c, cert, WOLFSSL_FILETYPE_PEM)
            != WOLFSSL_SUCCESS) die("use_certificate_file");
        if (wolfSSL_CTX_use_PrivateKey_file(c, key, WOLFSSL_FILETYPE_PEM)
            != WOLFSSL_SUCCESS) die("use_PrivateKey_file");
    }
    if (GROUP) {
        /* Native wolfSSL API; set1_groups_list needs the OpenSSL compat layer,
         * which this build does not enable. */
        int g[1];
        if (!strcmp(GROUP, "P-256"))       g[0] = WOLFSSL_ECC_SECP256R1;
        else if (!strcmp(GROUP, "X25519")) g[0] = WOLFSSL_ECC_X25519;
        else die("unknown GROUP");
        if (wolfSSL_CTX_set_groups(c, g, 1) != WOLFSSL_SUCCESS)
            die("wolfSSL_CTX_set_groups");
    }
    wolfSSL_CTX_set_verify(c, WOLFSSL_VERIFY_NONE, NULL);
    return c;
}

static void server_loop(int n, const char *cert, const char *key)
{
    WOLFSSL_CTX *ctx = mkctx(1, cert, key);
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(PORT);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind");

    for (int i = 0; i < n; i++) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        char probe[1];
        if (recvfrom(fd, probe, 1, MSG_PEEK, (struct sockaddr *)&peer, &plen) < 0)
            die("recvfrom peek");

        WOLFSSL *ssl = wolfSSL_new(ctx);
        if (!ssl) die("wolfSSL_new");
        wolfSSL_set_fd(ssl, fd);
        wolfSSL_dtls_set_peer(ssl, &peer, plen);
        if (wolfSSL_accept(ssl) != WOLFSSL_SUCCESS) {
            fprintf(stderr, "accept err=%d\n", wolfSSL_get_error(ssl, -1));
            die("wolfSSL_accept");
        }
        wolfSSL_free(ssl);
    }
    close(fd);
    wolfSSL_CTX_free(ctx);
    _exit(0);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 200;
    const char *cert = argc > 2 ? argv[2] : "server-ecc.pem";
    const char *key  = argc > 3 ? argv[3] : "ecc-key.pem";
    if (argc > 4) PORT = atoi(argv[4]);
    if (argc > 5) GROUP = argv[5];

    wolfSSL_Init();

    pid_t pid = fork();
    if (pid == 0) server_loop(n, cert, key);
    usleep(300000);

    WOLFSSL_CTX *ctx = mkctx(0, NULL, NULL);
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    srv.sin_port = htons(PORT);

    int warm = n / 10 < 5 ? 5 : n / 10;
    double t0 = 0;
    const char *version = "?";

    for (int i = 0; i < n; i++) {
        if (i == warm) t0 = now_s();

        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) die("connect");
        WOLFSSL *ssl = wolfSSL_new(ctx);
        if (!ssl) die("wolfSSL_new");
        wolfSSL_set_fd(ssl, fd);
        wolfSSL_dtls_set_peer(ssl, &srv, sizeof(srv));
        if (wolfSSL_connect(ssl) != WOLFSSL_SUCCESS) {
            fprintf(stderr, "connect err=%d\n", wolfSSL_get_error(ssl, -1));
            die("wolfSSL_connect");
        }
        if (i == 0) {
            version = wolfSSL_get_version(ssl);
            fprintf(stderr, "wolfssl negotiated: cipher=%s group=%s\n",
                    wolfSSL_get_cipher_name(ssl),
                    wolfSSL_get_curve_name(ssl));
        }
        wolfSSL_free(ssl);
        close(fd);
    }
    double el = now_s() - t0;
    int timed = n - warm;

    printf("wolfssl  %-10s handshakes=%d  %8.1f us/handshake  %8.1f handshakes/sec\n",
           version, timed, el * 1e6 / timed, timed / el);

    int st; waitpid(pid, &st, 0);
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    return 0;
}

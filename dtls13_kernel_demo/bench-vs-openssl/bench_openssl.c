// DTLS 1.3 full-handshake benchmark - OpenSSL.
// Structure is deliberately identical to bench_wolfssl.c so the two are
// comparable: fork a server, run N sequential full handshakes over loopback
// UDP, time the client side.
//
// Fairness notes:
//  - server keeps ONE unconnected socket and learns the peer per handshake;
//    the client uses a FRESH socket each iteration, so every handshake is a
//    full one (new source port = new peer, no resumption).
//  - client verification is disabled on BOTH sides' benchmarks, so the same
//    work is done in each: server signs, client does not verify a chain.
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
#include <openssl/ssl.h>
#include <openssl/err.h>

static int PORT = 14443;
static const char *GROUP = NULL;

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void die(const char *m)
{
    fprintf(stderr, "openssl-bench: %s\n", m);
    ERR_print_errors_fp(stderr);
    exit(1);
}

static SSL_CTX *mkctx(int server, const char *cert, const char *key)
{
    SSL_CTX *c = SSL_CTX_new(server ? DTLS_server_method() : DTLS_client_method());
    if (!c) die("SSL_CTX_new");
    if (!SSL_CTX_set_min_proto_version(c, DTLS1_3_VERSION) ||
        !SSL_CTX_set_max_proto_version(c, DTLS1_3_VERSION))
        die("set proto version DTLS1_3");
    if (server) {
        if (SSL_CTX_use_certificate_file(c, cert, SSL_FILETYPE_PEM) != 1)
            die("use_certificate_file");
        if (SSL_CTX_use_PrivateKey_file(c, key, SSL_FILETYPE_PEM) != 1)
            die("use_PrivateKey_file");
    }
    if (GROUP && !SSL_CTX_set1_groups_list(c, GROUP)) die("set1_groups_list");
    SSL_CTX_set_verify(c, SSL_VERIFY_NONE, NULL);
    return c;
}

static void server_loop(int n, const char *cert, const char *key)
{
    SSL_CTX *ctx = mkctx(1, cert, key);
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
        /* Learn the peer without consuming the ClientHello. */
        if (recvfrom(fd, probe, 1, MSG_PEEK, (struct sockaddr *)&peer, &plen) < 0)
            die("recvfrom peek");

        SSL *ssl = SSL_new(ctx);
        BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
        BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_PEER, 0, &peer);
        SSL_set_bio(ssl, bio, bio);
        SSL_set_accept_state(ssl);
        if (SSL_accept(ssl) <= 0) die("SSL_accept");
        SSL_free(ssl);
    }
    close(fd);
    SSL_CTX_free(ctx);
    _exit(0);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 200;
    const char *cert = argc > 2 ? argv[2] : "server-ecc.pem";
    const char *key  = argc > 3 ? argv[3] : "ecc-key.pem";
    if (argc > 4) PORT = atoi(argv[4]);
    if (argc > 5) GROUP = argv[5];

    pid_t pid = fork();
    if (pid == 0) server_loop(n, cert, key);
    usleep(300000);   /* let the server bind */

    SSL_CTX *ctx = mkctx(0, NULL, NULL);
    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    srv.sin_port = htons(PORT);

    /* Warm-up handshakes are excluded from the timing. */
    int warm = n / 10 < 5 ? 5 : n / 10;
    double t0 = 0;
    const char *version = "?";

    for (int i = 0; i < n; i++) {
        if (i == warm) t0 = now_s();

        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (connect(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) die("connect");
        SSL *ssl = SSL_new(ctx);
        BIO *bio = BIO_new_dgram(fd, BIO_NOCLOSE);
        BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &srv);
        SSL_set_bio(ssl, bio, bio);
        SSL_set_connect_state(ssl);
        if (SSL_connect(ssl) <= 0) die("SSL_connect");
        if (i == 0) {
            version = SSL_get_version(ssl);
            int gid = SSL_get_negotiated_group(ssl);
            fprintf(stderr, "openssl negotiated: cipher=%s group=%s(0x%04x)\n",
                    SSL_get_cipher_name(ssl),
                    SSL_group_to_name(ssl, gid) ? SSL_group_to_name(ssl, gid) : "?",
                    gid);
        }
        SSL_free(ssl);
        close(fd);
    }
    double el = now_s() - t0;
    int timed = n - warm;

    printf("openssl  %-10s handshakes=%d  %8.1f us/handshake  %8.1f handshakes/sec\n",
           version, timed, el * 1e6 / timed, timed / el);

    int st; waitpid(pid, &st, 0);
    SSL_CTX_free(ctx);
    return 0;
}

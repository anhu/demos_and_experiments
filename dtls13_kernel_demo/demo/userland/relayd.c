// SPDX-License-Identifier: GPL-2.0
/*
 * relayd.c - the userspace daemon that a kernel subsystem must hand data to
 * when the DTLS implementation lives in userspace.
 *
 * Loop: read() a kernel-originated message, write() an ack.  That is one
 * kernel->user->kernel round trip per message: two context switches and two
 * copies.  With in-kernel DTLS none of this exists.
 *
 * Reports its own context-switch count so the cost is visible, not asserted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

static volatile int running = 1;
static void on_term(int s) { (void)s; running = 0; }

static long ctxt_switches(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long v = 0, tot = 0;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "voluntary_ctxt_switches: %ld", &v) == 1) tot += v;
        else if (sscanf(line, "nonvoluntary_ctxt_switches: %ld", &v) == 1) tot += v;
    }
    fclose(f);
    return tot;
}

#include <time.h>

static unsigned long long now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + t.tv_nsec;
}

/*
 * Isolate the syscall+copy cost: with relay_always_ready=1 the read() never
 * blocks, so there is NO wakeup and NO context switch -- just the boundary
 * crossings themselves.  The difference against the full ping-pong is the
 * scheduling/rendezvous cost.
 */
static int syscall_bench(int fd, long n)
{
    unsigned char buf[4096];
    unsigned long long t0, t1;
    long cs0, cs1;

    for (long i = 0; i < 1000; i++) {          /* warm */
        if (read(fd, buf, 1024) < 0) return 1;
        if (write(fd, "k", 1) < 0) return 1;
    }

    cs0 = ctxt_switches();
    t0 = now_ns();
    for (long i = 0; i < n; i++) {
        if (read(fd, buf, 1024) < 0) return 1;
        if (write(fd, "k", 1) < 0) return 1;
    }
    t1 = now_ns();
    cs1 = ctxt_switches();

    printf("relayd: SYSCALL-ONLY  n=%ld  ns_per_roundtrip=%llu  ctxt_switches=%ld (%.4f per msg)\n",
           n, (t1 - t0) / (unsigned long long)n, cs1 - cs0,
           (double)(cs1 - cs0) / (double)n);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char buf[4096];
    long msgs = 0, cs0;
    int fd;

    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    fd = open("/dev/wolfdtls_relay", O_RDWR);
    if (fd < 0) { perror("open /dev/wolfdtls_relay"); return 1; }

    if (argc > 2 && !strcmp(argv[1], "--syscall-bench")) {
        int rc = syscall_bench(fd, atol(argv[2]));
        close(fd);
        return rc;
    }

    cs0 = ctxt_switches();
    printf("relayd: started, ctxt_switches=%ld\n", cs0);
    fflush(stdout);

    while (running) {
        ssize_t n = read(fd, buf, sizeof(buf));   /* blocks: 1 ctx switch  */
        if (n <= 0) { if (n == 0) break; continue; }
        if (write(fd, "k", 1) < 0) break;         /* ack back into kernel  */
        msgs++;
        if ((msgs % 20000) == 0) {
            printf("relayd: msgs=%ld ctxt_switches=%ld (%.2f per msg)\n",
                   msgs, ctxt_switches() - cs0,
                   (double)(ctxt_switches() - cs0) / msgs);
            fflush(stdout);
        }
    }

    printf("relayd: exiting, msgs=%ld ctxt_switches=%ld\n",
           msgs, ctxt_switches() - cs0);
    close(fd);
    return 0;
}

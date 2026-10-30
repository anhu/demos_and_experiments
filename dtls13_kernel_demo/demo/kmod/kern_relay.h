/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interface between the wolfSSL-facing part of the module (wolfdtls_main.c)
 * and the kernel-headers-only part (kern_relay.c).  Deliberately free of both
 * wolfSSL and kernel struct types so it can be included from either side.
 */
#ifndef KERN_RELAY_H
#define KERN_RELAY_H

struct socket;
void wdtls_sock_set_rcvtimeo(struct socket *sock, long secs);

int  relay_init(void);
void relay_exit(void);
int  relay_bench(unsigned int n, unsigned long long *elapsed_ns);
int  relay_bench_inkernel(unsigned int n, unsigned long long *elapsed_ns);

#endif

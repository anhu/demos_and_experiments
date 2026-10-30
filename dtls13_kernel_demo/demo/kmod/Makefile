# Out-of-tree build for wolfdtls_srv.ko, the in-kernel DTLS 1.3 echo server.
#
# It links against symbols exported by libwolfssl.ko, so it must be compiled
# with the *same* preprocessor configuration wolfSSL itself was built with --
# otherwise the wolfSSL structs this module sees would not match the ones
# inside libwolfssl.ko.  Rather than hardcode ~100 -D flags, they are scraped
# out of the configured wolfSSL top-level Makefile.

WOLFSSL_TOP ?= $(abspath $(CURDIR)/../../wolfssl)
KDIR        ?= /lib/modules/$(shell uname -r)/build

obj-m := wolfdtls_srv.o

# Two translation units on purpose: wolfdtls_main.c includes wolfssl/ssl.h,
# kern_relay.c includes <net/*> and <linux/miscdevice.h>.  Those two sets of
# headers cannot coexist in one file (linuxkm_wc_port.h #undefs current/min/max),
# so the split is the clean fix rather than a workaround.
wolfdtls_srv-y := wolfdtls_main.o kern_relay.o

# The exact AM_CFLAGS defines from the configured wolfSSL build.
WOLFSSL_DEFS := $(shell grep -m1 '^AM_CFLAGS = ' $(WOLFSSL_TOP)/Makefile \
                  | tr ' ' '\n' | grep -E '^-D')

# -ffreestanding plus the compiler's own include dir: wc_port.h pulls in
# <stdatomic.h>, which lives there and not in the kernel's include path.
# This mirrors what wolfssl/linuxkm/Kbuild does for the library itself.
FREESTANDING := -ffreestanding -isystem $(shell $(CC) -print-file-name=include)

# Force-include the same config.h the library was compiled against, and set
# WOLFSSL_CUSTOM_CONFIG so settings.h knows the configuration arrives via
# CFLAGS rather than wolfssl/options.h.
# BUILDING_WOLFSSL is required even though this is a *consumer* of the library:
# linuxkm/linuxkm_wc_port.h gates its full kernel port block (time_t, timeval,
# NO_TIMEVAL, the mutex/heap shims) behind it, and without it wolfssl/ssl.h
# fails to compile with "unknown type name 'time_t'".  It also guarantees this
# module sees byte-identical struct layouts to those inside libwolfssl.ko.
ccflags-y := -DWOLFSSL_CUSTOM_CONFIG -DBUILDING_WOLFSSL -I$(WOLFSSL_TOP) \
             -include $(WOLFSSL_TOP)/config.h \
             $(WOLFSSL_DEFS) $(FREESTANDING) \
             -Wno-declaration-after-statement -Wno-redundant-decls

# Resolve wolfSSL's exported symbols at modpost time.
KBUILD_EXTRA_SYMBOLS := $(WOLFSSL_TOP)/linuxkm/Module.symvers

all:
	@test -f $(WOLFSSL_TOP)/Makefile || \
	  { echo "error: $(WOLFSSL_TOP) is not configured; run ./configure first" >&2; exit 1; }
	@test -f $(KBUILD_EXTRA_SYMBOLS) || \
	  { echo "error: $(KBUILD_EXTRA_SYMBOLS) missing; run 'make module' in wolfssl first" >&2; exit 1; }
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean

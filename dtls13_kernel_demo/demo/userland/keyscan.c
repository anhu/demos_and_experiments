// SPDX-License-Identifier: GPL-2.0
/*
 * keyscan.c - look for a DTLS server's EC private key in memory.
 *
 * Runs inside the guest.  Two modes:
 *
 *   keyscan procs     walk every process's address space via /proc/<pid>/mem
 *   keyscan kcore     walk kernel memory via /proc/kcore
 *
 * The search is STRUCTURAL: it scans for the SEC1 ECPrivateKey DER header
 * for a P-256 key --
 *
 *     30 77 02 01 01 04 20  <32-byte private scalar>
 *
 * so it does not need to know the key in advance, which is the point.  A real
 * attacker with a memory-disclosure primitive does exactly this.  The scalar
 * that follows is printed so it can be compared against the known key.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>

/* SEC1 ECPrivateKey, P-256: SEQUENCE(0x77) INTEGER 1, OCTET STRING len 0x20 */
static const unsigned char SIG[] = {
	0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20
};
#define SIGLEN  (sizeof(SIG))
#define SCALAR  32

static int hits;

/* Section windows overlap, so the same physical hit is seen once per section.
 * Report each unique address only once. */
static unsigned long long seen[256];
static int nseen;

static int already_seen(unsigned long long a)
{
	for (int i = 0; i < nseen; i++)
		if (seen[i] == a)
			return 1;
	if (nseen < 256)
		seen[nseen++] = a;
	return 0;
}

/* Pattern being searched: defaults to the SEC1 header, overridable so the
 * caller can run a positive control (a string known to be in the module). */
static const unsigned char *PAT = SIG;
static size_t PATLEN = SIGLEN;
static int show_scalar = 1;

static void report(const char *where, unsigned long long addr,
		   const unsigned char *scalar)
{
	printf("  *** FOUND  %s  @ 0x%llx\n", where, addr);
	printf("      private scalar: ");
	for (int i = 0; i < SCALAR; i++)
		printf("%02x", scalar[i]);
	printf("\n");
	hits++;
}

/* Scan a buffer for the signature; needs SIGLEN+SCALAR bytes to confirm. */
static void scan_buf(const unsigned char *buf, size_t len,
		     unsigned long long base, const char *where)
{
	size_t need = PATLEN + (show_scalar ? SCALAR : 0);

	if (len < need)
		return;

	for (size_t i = 0; i + need <= len; i++) {
		if (memcmp(buf + i, PAT, PATLEN) == 0) {
			if (already_seen(base + i))
				continue;
			if (show_scalar) {
				report(where, base + i, buf + i + PATLEN);
			} else {
				printf("  *** FOUND  %s  @ 0x%llx  (control)\n",
				       where, base + i);
				hits++;
			}
		}
	}
}

static void scan_proc(const char *pid, const char *comm)
{
	char path[288], line[512];
	unsigned char *buf;
	FILE *maps;
	int fd;

	snprintf(path, sizeof(path), "/proc/%s/maps", pid);
	maps = fopen(path, "r");
	if (!maps)
		return;

	snprintf(path, sizeof(path), "/proc/%s/mem", pid);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fclose(maps);
		return;
	}

	buf = malloc(1 << 20);
	if (!buf) {
		close(fd);
		fclose(maps);
		return;
	}

	while (fgets(line, sizeof(line), maps)) {
		unsigned long long start, end;
		char perms[8], label[128] = "";

		if (sscanf(line, "%llx-%llx %7s %*s %*s %*s %127[^\n]",
			   &start, &end, perms, label) < 3)
			continue;
		if (perms[0] != 'r')
			continue;
		/* Skip device mappings that would block or fault on read. */
		if (strstr(label, "/dev/") || strstr(label, "vvar") ||
		    strstr(label, "vsyscall"))
			continue;

		for (unsigned long long off = start; off < end; off += (1 << 20)) {
			size_t want = end - off;
			ssize_t got;

			if (want > (1 << 20))
				want = 1 << 20;

			got = pread(fd, buf, want, (off_t)off);
			if (got <= 0)
				continue;

			char where[256];
			snprintf(where, sizeof(where), "pid %s (%s) %s",
				 pid, comm, label[0] ? label : "[anon]");
			scan_buf(buf, (size_t)got, off, where);
		}
	}

	free(buf);
	close(fd);
	fclose(maps);
}

static void get_comm(const char *pid, char *out, size_t n)
{
	char path[288];
	FILE *f;

	snprintf(path, sizeof(path), "/proc/%s/comm", pid);
	out[0] = '\0';
	f = fopen(path, "r");
	if (f) {
		if (fgets(out, n, f))
			out[strcspn(out, "\n")] = '\0';
		fclose(f);
	}
}

static int scan_all_procs(void)
{
	struct dirent *de;
	DIR *d = opendir("/proc");
	int scanned = 0;

	if (!d)
		return 0;

	while ((de = readdir(d))) {
		char comm[64];

		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		if (atoi(de->d_name) == getpid())
			continue;

		get_comm(de->d_name, comm, sizeof(comm));
		scan_proc(de->d_name, comm);
		scanned++;
	}
	closedir(d);
	return scanned;
}

/*
 * Scan /proc/kcore.  This is the honest control: if the kernel module holds
 * the key, root can still reach it here unless kernel lockdown is on.
 *
 * /proc/kcore is an ELF core file -- file offsets are NOT virtual addresses.
 * A naive pread() at a module's virtual base silently reads the wrong thing
 * (or nothing) and yields a FALSE NEGATIVE, which would make the kernel look
 * more secure than it is.  So parse the PT_LOAD program headers and translate
 * vaddr -> file offset properly.
 */
#include <elf.h>

struct seg { unsigned long long vaddr, off, sz; };
static struct seg segs[512];
static int nsegs;

static int load_kcore_segments(int fd)
{
	Elf64_Ehdr eh;
	Elf64_Phdr *ph;

	if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh))
		return -1;
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0)
		return -1;

	ph = calloc(eh.e_phnum, sizeof(*ph));
	if (!ph)
		return -1;
	if (pread(fd, ph, eh.e_phnum * sizeof(*ph), eh.e_phoff)
	    != (ssize_t)(eh.e_phnum * sizeof(*ph))) {
		free(ph);
		return -1;
	}

	for (int i = 0; i < eh.e_phnum && nsegs < 512; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		segs[nsegs].vaddr = ph[i].p_vaddr;
		segs[nsegs].off   = ph[i].p_offset;
		segs[nsegs].sz    = ph[i].p_filesz;
		nsegs++;
	}
	free(ph);
	return nsegs;
}

/* Translate a kernel virtual address to a kcore file offset. */
static long long vaddr_to_off(unsigned long long v, unsigned long long *avail)
{
	for (int i = 0; i < nsegs; i++) {
		if (v >= segs[i].vaddr && v < segs[i].vaddr + segs[i].sz) {
			*avail = segs[i].vaddr + segs[i].sz - v;
			return (long long)(segs[i].off + (v - segs[i].vaddr));
		}
	}
	return -1;
}

static void scan_kcore(void)
{
	unsigned char *buf;
	FILE *mods;
	char line[512];
	int fd, nmod = 0;

	fd = open("/proc/kcore", O_RDONLY);
	if (fd < 0) {
		printf("  /proc/kcore not readable: %s\n", strerror(errno));
		printf("  (this is what kernel lockdown / CONFIG_PROC_KCORE=n gives you)\n");
		return;
	}

	if (load_kcore_segments(fd) <= 0) {
		printf("  could not parse /proc/kcore ELF headers\n");
		close(fd);
		return;
	}
	printf("  parsed %d PT_LOAD segments from /proc/kcore\n", nsegs);

	mods = fopen("/proc/modules", "r");
	if (!mods) {
		close(fd);
		return;
	}

	buf = malloc(1 << 20);
	if (!buf) {
		fclose(mods);
		close(fd);
		return;
	}

	while (fgets(line, sizeof(line), mods)) {
		char name[64], sdir[256];
		struct dirent *de;
		DIR *d;

		if (sscanf(line, "%63s", name) != 1)
			continue;

		/*
		 * Since kernel 6.4 a module's memory is split into separate
		 * allocations (MOD_TEXT, MOD_RODATA, MOD_DATA, ...), so the
		 * single base+size from /proc/modules covers only text and
		 * MISSES .rodata -- where an embedded key actually lives.
		 * /sys/module/<name>/sections/ gives every section's address.
		 */
		snprintf(sdir, sizeof(sdir), "/sys/module/%s/sections", name);
		d = opendir(sdir);
		if (!d)
			continue;

		nmod++;
		while ((de = readdir(d))) {
			char spath[512];
			unsigned long long saddr = 0;
			FILE *sf;

			if (de->d_name[0] != '.' || !de->d_name[1])
				continue;   /* sections are named .text, .rodata, ... */
			if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
				continue;

			snprintf(spath, sizeof(spath), "%s/%s", sdir, de->d_name);
			sf = fopen(spath, "r");
			if (!sf)
				continue;
			if (fscanf(sf, "%llx", &saddr) != 1) {
				fclose(sf);
				continue;
			}
			fclose(sf);
			if (!saddr)
				continue;

			/* Scan a window from each section start. */
			unsigned long long avail = 0;
			long long foff = vaddr_to_off(saddr, &avail);
			ssize_t got;
			size_t want = 1 << 20;

			if (foff < 0)
				continue;
			if (want > avail)
				want = (size_t)avail;

			got = pread(fd, buf, want, (off_t)foff);
			if (got <= 0)
				continue;

			char where[576];
			snprintf(where, sizeof(where), "kcore %s%s",
				 name, de->d_name);
			scan_buf(buf, (size_t)got, saddr, where);
		}
		closedir(d);
	}

	if (!nmod)
		printf("  no module base addresses available\n");

	free(buf);
	fclose(mods);
	close(fd);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "procs";

	printf("keyscan: searching for SEC1 P-256 ECPrivateKey DER header\n");
	printf("         (30 77 02 01 01 04 20 + 32-byte scalar)\n\n");

	if (argc > 2) {
		PAT = (const unsigned char *)argv[2];
		PATLEN = strlen(argv[2]);
		show_scalar = 0;
		printf("  (control mode: searching for literal \"%s\")\n\n",
		       argv[2]);
	}

	if (!strcmp(mode, "kcore")) {
		printf("[kcore] scanning loaded module images:\n");
		scan_kcore();
	} else {
		int n = scan_all_procs();

		printf("[procs] scanned %d process address spaces\n", n);
	}

	printf("\n==> %d match(es)\n", hits);
	return hits ? 0 : 1;
}

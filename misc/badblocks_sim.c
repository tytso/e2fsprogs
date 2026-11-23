/*
 * badblocks_sim.c --- scalable in-memory fault-injecting simulator for badblocks
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "et/com_err.h"
#include "ext2fs/ext2_fs.h"
#include "ext2fs/ext2fs.h"
#include "badblocks.h"

#ifdef BADBLOCKS_SIM

#define SIM_SECTOR_SIZE		512U
#define SIM_CHUNK_SECS		128U
#define SIM_CHUNK_BYTES		(SIM_SECTOR_SIZE * SIM_CHUNK_SECS)
#define SIM_CHUNK_BMAP_BYTES	(SIM_CHUNK_SECS / 8U)
#define SIM_HASH_BUCKETS	1024U
#define SIM_DEFAULT_MAXMEM	(256ULL * 1024ULL * 1024ULL)
#define SIM_MAX_FLAKY_EXPAND	65536ULL

#define SIM_FAULT_RERR		0x01U
#define SIM_FAULT_WERR		0x02U
#define SIM_FAULT_CORRUPT	0x04U

enum sim_errmode {
	SIM_ERR_EIO = 0,
	SIM_ERR_SHORT = 1
};

enum sim_fillmode {
	SIM_FILL_PRNG = 0,
	SIM_FILL_ZERO = 1,
	SIM_FILL_BYTE = 2
};

struct sim_fault_ext {
	__u64		start;
	__u64		len;
	unsigned int	flags;
};

struct sim_flaky {
	__u64		sec;
	unsigned int	skip;
	unsigned int	fail;
	unsigned int	reads;
};

struct sim_extent {
	__u64		start;
	__u64		len;
};

struct sim_ext_list {
	struct sim_extent	*exts;
	size_t			num;
	size_t			cap;
};

struct sim_chunk {
	__u64			idx;
	struct sim_chunk	*next;
	unsigned char		written_bmap[SIM_CHUNK_BMAP_BYTES];
	unsigned char		data[SIM_CHUNK_BYTES];
};

struct sim_state {
	__u64			size_sectors;
	unsigned int		tail_bytes;
	unsigned int		page_size;
	enum sim_errmode	errmode;
	enum sim_fillmode	fillmode;
	unsigned char		fill_byte;
	__u64			fill_seed;
	__u64			max_chunks;
	__u64			chunks_live;
	__u64			peak_chunks;
	__u64			sigint_after_writes;
	int			mounted;
	int			busy;
	int			trace;
	int			quiet;

	int			scan_started;
	int			reported;
	unsigned int		scan_block_size;
	__u64			scan_start_byte;
	__u64			scan_end_byte;

	__u64			reads;
	__u64			writes;
	__u64			flushes;
	__u64			direct_misaligned;
	__u64			out_of_range;

	struct sim_fault_ext	*faults;
	size_t			num_faults;
	size_t			cap_faults;

	struct sim_flaky	*flakies;
	size_t			num_flakies;
	size_t			cap_flakies;

	struct sim_ext_list	read_cov;
	struct sim_chunk	*buckets[SIM_HASH_BUCKETS];
};

static struct sim_state *g_active_sim;
static int g_atexit_registered;

static void sim_die(const char *msg, const char *arg)
{
	if (arg)
		fprintf(stderr, "sim: %s: %s\n", msg, arg);
	else
		fprintf(stderr, "sim: %s\n", msg);
	exit(1);
}

static __u64 splitmix64(__u64 x)
{
	x += 0x9e3779b97f4a7c15ULL;
	x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
	x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
	return x ^ (x >> 31);
}

static void fill_sector_raw(const struct sim_state *sim, __u64 sec,
			    unsigned char *buf)
{
	size_t i;
	__u64 st;

	if (sim->fillmode == SIM_FILL_ZERO) {
		memset(buf, 0, SIM_SECTOR_SIZE);
		return;
	}
	if (sim->fillmode == SIM_FILL_BYTE) {
		memset(buf, sim->fill_byte, SIM_SECTOR_SIZE);
		return;
	}

	st = splitmix64(sec ^ sim->fill_seed ^ 0x243f6a8885a308d3ULL);
	for (i = 0; i < SIM_SECTOR_SIZE; i += 8) {
		__u64 w = splitmix64(st);
		unsigned int b;

		st += 0x9e3779b97f4a7c15ULL;
		for (b = 0; b < 8; b++)
			buf[i + b] = (unsigned char) ((w >> (8U * b)) & 0xffU);
	}
}

static int parse_u64_strict(const char *s, __u64 *out_val)
{
	char *end = NULL;
	unsigned long long val;

	if (!s || !*s || *s == '-' || *s == '+' || isspace((unsigned char) *s))
		return -1;
	errno = 0;
	val = strtoull(s, &end, 0);
	if (errno == ERANGE || end == s || *end != '\0')
		return -1;
	*out_val = (__u64) val;
	return 0;
}

static void parse_size_sectors(const char *str, __u64 *secs_out,
			       unsigned int *tail_out)
{
	char *end = NULL;
	unsigned long long val;
	__u64 mult = 0;

	if (!str || !*str || *str == '-' || *str == '+' ||
	    isspace((unsigned char) *str))
		sim_die("invalid size", str);
	errno = 0;
	val = strtoull(str, &end, 0);
	if (errno == ERANGE || end == str)
		sim_die("invalid size", str);

	if (*end == '\0' || ((*end == 'B' || *end == 'b') && end[1] == '\0')) {
		*secs_out = (__u64) (val / SIM_SECTOR_SIZE);
		*tail_out = (unsigned int) (val % SIM_SECTOR_SIZE);
		return;
	}
	if (end[1] != '\0')
		sim_die("invalid size suffix", str);

	switch (*end) {
	case 's':
	case 'S':
		*secs_out = (__u64) val;
		*tail_out = 0;
		return;
	case 'K':
	case 'k':
		mult = 2ULL;
		break;
	case 'M':
	case 'm':
		mult = 2048ULL;
		break;
	case 'G':
	case 'g':
		mult = 2ULL << 20;
		break;
	case 'T':
	case 't':
		mult = 2ULL << 30;
		break;
	case 'P':
	case 'p':
		mult = 2ULL << 40;
		break;
	case 'E':
	case 'e':
		mult = 2ULL << 50;
		break;
	default:
		sim_die("invalid size suffix", str);
		return;
	}

	if ((__u64) val > (~0ULL / mult))
		sim_die("size overflow", str);
	*secs_out = (__u64) val * mult;
	*tail_out = 0;
}

static __u64 parse_mem_bytes(const char *str)
{
	char *end = NULL;
	unsigned long long val;
	__u64 mult = 1ULL;

	if (!str || !*str || *str == '-' || *str == '+' ||
	    isspace((unsigned char) *str))
		sim_die("invalid maxmem", str);
	errno = 0;
	val = strtoull(str, &end, 0);
	if (errno == ERANGE || end == str)
		sim_die("invalid maxmem", str);

	if (*end != '\0') {
		if (end[1] != '\0')
			sim_die("invalid maxmem suffix", str);
		switch (*end) {
		case 'B':
		case 'b':
			mult = 1ULL;
			break;
		case 'K':
		case 'k':
			mult = 1024ULL;
			break;
		case 'M':
		case 'm':
			mult = 1024ULL * 1024ULL;
			break;
		case 'G':
		case 'g':
			mult = 1024ULL * 1024ULL * 1024ULL;
			break;
		default:
			sim_die("invalid maxmem suffix", str);
		}
	}
	if ((__u64) val > (~0ULL / mult))
		sim_die("maxmem overflow", str);
	return (__u64) val * mult;
}

static void parse_sector_token(const char *tok, __u64 *start_out,
			       __u64 *end_out)
{
	const char *slash;
	char blk_buf[64];
	size_t blen;
	__u64 blk = 0, bs = 0, secs_per_blk;

	if (tok[0] != '@') {
		if (parse_u64_strict(tok, start_out) < 0)
			sim_die("invalid sector number", tok);
		*end_out = *start_out;
		return;
	}

	slash = strchr(tok + 1, '/');
	if (!slash || slash == tok + 1 || !slash[1])
		sim_die("invalid @block/bs specifier", tok);
	blen = (size_t) (slash - (tok + 1));
	if (blen >= sizeof(blk_buf))
		sim_die("invalid @block/bs specifier", tok);
	memcpy(blk_buf, tok + 1, blen);
	blk_buf[blen] = '\0';

	if (parse_u64_strict(blk_buf, &blk) < 0 ||
	    parse_u64_strict(slash + 1, &bs) < 0)
		sim_die("invalid @block/bs specifier", tok);
	if (bs < SIM_SECTOR_SIZE || bs > (1ULL << 24) || (bs & (bs - 1ULL)))
		sim_die("invalid block size in @block/bs", tok);

	secs_per_blk = bs / SIM_SECTOR_SIZE;
	if (blk > (~0ULL / secs_per_blk))
		sim_die("sector overflow in @block/bs", tok);
	*start_out = blk * secs_per_blk;
	if (secs_per_blk - 1ULL > ~0ULL - *start_out)
		sim_die("sector overflow in @block/bs", tok);
	*end_out = *start_out + secs_per_blk - 1ULL;
}

static void parse_sector_range(const char *str, __u64 *start_out,
			       __u64 *end_out)
{
	const char *dash = strchr(str, '-');
	char lhs[96];
	size_t llen;
	__u64 s0, e0, s1, e1;

	if (!dash) {
		parse_sector_token(str, start_out, end_out);
		return;
	}

	llen = (size_t) (dash - str);
	if (llen == 0 || llen >= sizeof(lhs) || !dash[1])
		sim_die("invalid sector range", str);
	memcpy(lhs, str, llen);
	lhs[llen] = '\0';

	parse_sector_token(lhs, &s0, &e0);
	parse_sector_token(dash + 1, &s1, &e1);
	if (s0 > e1)
		sim_die("inverted sector range", str);
	*start_out = s0;
	*end_out = e1;
}

static void fault_insert_at(struct sim_state *sim, size_t idx,
			    __u64 start, __u64 len, unsigned int flags)
{
	if (sim->num_faults == sim->cap_faults) {
		size_t new_cap = sim->cap_faults ? sim->cap_faults * 2 : 16;
		struct sim_fault_ext *nf;

		nf = realloc(sim->faults, new_cap * sizeof(*nf));
		if (!nf)
			sim_die("out of memory", NULL);
		sim->faults = nf;
		sim->cap_faults = new_cap;
	}
	if (idx < sim->num_faults) {
		memmove(&sim->faults[idx + 1], &sim->faults[idx],
			(sim->num_faults - idx) * sizeof(*sim->faults));
	}
	sim->faults[idx].start = start;
	sim->faults[idx].len = len;
	sim->faults[idx].flags = flags;
	sim->num_faults++;
}

static void fault_add_range(struct sim_state *sim, __u64 start, __u64 end_incl,
			    unsigned int flag)
{
	__u64 cur = start;
	__u64 rem;
	size_t i = 0;

	if (end_incl < start)
		return;
	if (end_incl == ~0ULL && start == 0)
		sim_die("sector range too large", NULL);
	rem = (end_incl - start) + 1ULL;
	if (rem == 0)
		sim_die("sector range overflow", NULL);

	while (rem > 0) {
		if (i >= sim->num_faults) {
			fault_insert_at(sim, i, cur, rem, flag);
			break;
		}
		if (cur >= sim->faults[i].start + sim->faults[i].len) {
			i++;
			continue;
		}
		if (cur < sim->faults[i].start) {
			__u64 gap = sim->faults[i].start - cur;
			__u64 add = gap < rem ? gap : rem;

			fault_insert_at(sim, i, cur, add, flag);
			cur += add;
			rem -= add;
			i++;
			continue;
		}
		if (cur > sim->faults[i].start) {
			__u64 prefix = cur - sim->faults[i].start;
			__u64 tail = sim->faults[i].len - prefix;
			unsigned int old_flags = sim->faults[i].flags;

			sim->faults[i].len = prefix;
			fault_insert_at(sim, i + 1, cur, tail, old_flags);
			i++;
			continue;
		}
		/* cur == sim->faults[i].start */
		if (rem < sim->faults[i].len) {
			__u64 tail = sim->faults[i].len - rem;
			unsigned int old_flags = sim->faults[i].flags;

			sim->faults[i].len = rem;
			sim->faults[i].flags |= flag;
			fault_insert_at(sim, i + 1, cur + rem, tail, old_flags);
			break;
		}
		sim->faults[i].flags |= flag;
		cur += sim->faults[i].len;
		rem -= sim->faults[i].len;
		i++;
	}

	/* Coalesce adjacent fault extents with identical flags */
	i = 0;
	while (i + 1 < sim->num_faults) {
		if (sim->faults[i].flags == sim->faults[i + 1].flags &&
		    sim->faults[i].start + sim->faults[i].len ==
		    sim->faults[i + 1].start) {
			sim->faults[i].len += sim->faults[i + 1].len;
			if (i + 2 < sim->num_faults) {
				memmove(&sim->faults[i + 1],
					&sim->faults[i + 2],
					(sim->num_faults - (i + 2)) *
					sizeof(*sim->faults));
			}
			sim->num_faults--;
		} else {
			i++;
		}
	}
}

static unsigned int fault_flags(const struct sim_state *sim, __u64 sec)
{
	size_t lo = 0;
	size_t hi = sim->num_faults;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const struct sim_fault_ext *f = &sim->faults[mid];

		if (sec < f->start)
			hi = mid;
		else if (sec >= f->start + f->len)
			lo = mid + 1;
		else
			return f->flags;
	}
	return 0;
}

static void flaky_add_sector(struct sim_state *sim, __u64 sec,
			     unsigned int fail, unsigned int skip)
{
	size_t lo = 0;
	size_t hi = sim->num_flakies;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;

		if (sim->flakies[mid].sec < sec)
			lo = mid + 1;
		else if (sim->flakies[mid].sec > sec)
			hi = mid;
		else {
			sim->flakies[mid].fail = fail;
			sim->flakies[mid].skip = skip;
			sim->flakies[mid].reads = 0;
			return;
		}
	}

	if (sim->num_flakies == sim->cap_flakies) {
		size_t new_cap = sim->cap_flakies ? sim->cap_flakies * 2 : 16;
		struct sim_flaky *nf;

		nf = realloc(sim->flakies, new_cap * sizeof(*nf));
		if (!nf)
			sim_die("out of memory", NULL);
		sim->flakies = nf;
		sim->cap_flakies = new_cap;
	}
	if (lo < sim->num_flakies) {
		memmove(&sim->flakies[lo + 1], &sim->flakies[lo],
			(sim->num_flakies - lo) * sizeof(*sim->flakies));
	}
	sim->flakies[lo].sec = sec;
	sim->flakies[lo].skip = skip;
	sim->flakies[lo].fail = fail;
	sim->flakies[lo].reads = 0;
	sim->num_flakies++;
}

static void flaky_add_spec(struct sim_state *sim, const char *val)
{
	const char *c1 = strchr(val, ':');
	const char *c2;
	char rbuf[128], cbuf[32];
	__u64 s0, s1, s;
	__u64 fail64, skip64 = 0;
	size_t len;

	if (!c1 || c1 == val || !c1[1])
		sim_die("invalid flaky spec", val);
	len = (size_t) (c1 - val);
	if (len >= sizeof(rbuf))
		sim_die("invalid flaky range", val);
	memcpy(rbuf, val, len);
	rbuf[len] = '\0';

	c2 = strchr(c1 + 1, ':');
	if (c2) {
		size_t clen = (size_t) (c2 - (c1 + 1));

		if (clen == 0 || clen >= sizeof(cbuf) || !c2[1])
			sim_die("invalid flaky spec", val);
		memcpy(cbuf, c1 + 1, clen);
		cbuf[clen] = '\0';
		if (parse_u64_strict(cbuf, &fail64) < 0 ||
		    parse_u64_strict(c2 + 1, &skip64) < 0)
			sim_die("invalid flaky counts", val);
	} else {
		if (parse_u64_strict(c1 + 1, &fail64) < 0)
			sim_die("invalid flaky count", val);
	}
	if (fail64 > UINT_MAX || skip64 > UINT_MAX)
		sim_die("flaky count too large", val);

	parse_sector_range(rbuf, &s0, &s1);
	if (s1 - s0 + 1ULL > SIM_MAX_FLAKY_EXPAND)
		sim_die("flaky range too large", val);
	for (s = s0; s <= s1; s++) {
		flaky_add_sector(sim, s, (unsigned int) fail64,
				 (unsigned int) skip64);
		if (s == ~0ULL)
			break;
	}
}

static int flaky_check_read(struct sim_state *sim, __u64 sec)
{
	size_t lo = 0;
	size_t hi = sim->num_flakies;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;

		if (sim->flakies[mid].sec < sec)
			lo = mid + 1;
		else if (sim->flakies[mid].sec > sec)
			hi = mid;
		else {
			struct sim_flaky *f = &sim->flakies[mid];

			f->reads++;
			if (f->reads > f->skip &&
			    f->reads - f->skip <= f->fail)
				return 1;
			return 0;
		}
	}
	return 0;
}

static void extent_add(struct sim_ext_list *list, __u64 start, __u64 len)
{
	__u64 end;
	size_t lo = 0;
	size_t hi = list->num;
	size_t first, last;

	if (len == 0)
		return;
	end = start + len;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		const struct sim_extent *e = &list->exts[mid];

		if (e->start + e->len < start)
			lo = mid + 1;
		else
			hi = mid;
	}
	first = lo;
	last = first;
	while (last < list->num && list->exts[last].start <= end) {
		if (list->exts[last].start < start)
			start = list->exts[last].start;
		if (list->exts[last].start + list->exts[last].len > end)
			end = list->exts[last].start + list->exts[last].len;
		last++;
	}

	if (last > first) {
		list->exts[first].start = start;
		list->exts[first].len = end - start;
		if (last - first > 1) {
			if (last < list->num) {
				memmove(&list->exts[first + 1],
					&list->exts[last],
					(list->num - last) *
					sizeof(*list->exts));
			}
			list->num -= (last - first - 1);
		}
		return;
	}

	if (list->num == list->cap) {
		size_t new_cap = list->cap ? list->cap * 2 : 16;
		struct sim_extent *ne;

		ne = realloc(list->exts, new_cap * sizeof(*ne));
		if (!ne)
			sim_die("out of memory", NULL);
		list->exts = ne;
		list->cap = new_cap;
	}
	if (first < list->num) {
		memmove(&list->exts[first + 1], &list->exts[first],
			(list->num - first) * sizeof(*list->exts));
	}
	list->exts[first].start = start;
	list->exts[first].len = end - start;
	list->num++;
}

static unsigned int chunk_hash(__u64 idx)
{
	__u64 h = idx * 11400714819323198485ULL;

	return (unsigned int) ((h ^ (h >> 32)) % SIM_HASH_BUCKETS);
}

static struct sim_chunk *sim_find_chunk(struct sim_state *sim, __u64 idx,
					struct sim_chunk ***prev_out)
{
	unsigned int b = chunk_hash(idx);
	struct sim_chunk **pp = &sim->buckets[b];

	while (*pp) {
		if ((*pp)->idx == idx) {
			if (prev_out)
				*prev_out = pp;
			return *pp;
		}
		pp = &(*pp)->next;
	}
	if (prev_out)
		*prev_out = pp;
	return NULL;
}

static int chunk_is_empty(const struct sim_chunk *c)
{
	size_t i;

	for (i = 0; i < SIM_CHUNK_BMAP_BYTES; i++) {
		if (c->written_bmap[i] != 0)
			return 0;
	}
	return 1;
}

static void sim_read_sector_raw(struct sim_state *sim, __u64 sec,
				unsigned char *dst, int apply_unwritten_corrupt)
{
	__u64 cidx = sec / SIM_CHUNK_SECS;
	unsigned int slot = (unsigned int) (sec % SIM_CHUNK_SECS);
	struct sim_chunk *c = sim_find_chunk(sim, cidx, NULL);

	if (c && (c->written_bmap[slot >> 3] & (1U << (slot & 7U)))) {
		memcpy(dst, c->data + (size_t) slot * SIM_SECTOR_SIZE,
		       SIM_SECTOR_SIZE);
		return;
	}

	fill_sector_raw(sim, sec, dst);
	if (apply_unwritten_corrupt &&
	    (fault_flags(sim, sec) & SIM_FAULT_CORRUPT))
		dst[0] ^= 0x01U;
}

static void sim_write_sector(struct sim_state *sim, __u64 sec,
			     const unsigned char *src)
{
	unsigned char expected[SIM_SECTOR_SIZE];
	unsigned char to_store[SIM_SECTOR_SIZE];
	__u64 cidx = sec / SIM_CHUNK_SECS;
	unsigned int slot = (unsigned int) (sec % SIM_CHUNK_SECS);
	struct sim_chunk **prev = NULL;
	struct sim_chunk *c;

	fill_sector_raw(sim, sec, expected);
	memcpy(to_store, src, SIM_SECTOR_SIZE);
	if (fault_flags(sim, sec) & SIM_FAULT_CORRUPT)
		to_store[0] ^= 0x5aU;

	c = sim_find_chunk(sim, cidx, &prev);
	if (memcmp(to_store, expected, SIM_SECTOR_SIZE) == 0) {
		if (c) {
			c->written_bmap[slot >> 3] &=
				(unsigned char) ~(1U << (slot & 7U));
			if (chunk_is_empty(c)) {
				*prev = c->next;
				free(c);
				sim->chunks_live--;
			}
		}
		return;
	}

	if (!c) {
		if (sim->chunks_live >= sim->max_chunks)
			sim_die("maxmem exceeded", NULL);
		c = calloc(1, sizeof(*c));
		if (!c)
			sim_die("out of memory", NULL);
		c->idx = cidx;
		c->next = sim->buckets[chunk_hash(cidx)];
		sim->buckets[chunk_hash(cidx)] = c;
		sim->chunks_live++;
		if (sim->chunks_live > sim->peak_chunks)
			sim->peak_chunks = sim->chunks_live;
	}
	memcpy(c->data + (size_t) slot * SIM_SECTOR_SIZE, to_store,
	       SIM_SECTOR_SIZE);
	c->written_bmap[slot >> 3] |= (unsigned char) (1U << (slot & 7U));
}

static int sim_check_o_direct(struct sim_state *sim, const struct bb_dev *dev,
			      const void *buf, size_t count,
			      ext2_loff_t offset)
{
	if (!dev->current_o_direct)
		return 0;

	if ((((unsigned long) buf & (sim->page_size - 1U)) != 0) ||
	    ((count & (sim->page_size - 1U)) != 0) ||
	    ((offset & (SIM_SECTOR_SIZE - 1U)) != 0)) {
		sim->direct_misaligned++;
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static void sim_check_bounds(struct sim_state *sim, ext2_loff_t offset,
			     size_t count)
{
	__u64 uoff;

	if (!sim->scan_started)
		return;
	if (offset < 0) {
		sim->out_of_range++;
		return;
	}
	uoff = (__u64) offset;
	if (uoff < sim->scan_start_byte || uoff >= sim->scan_end_byte ||
	    (__u64) count > sim->scan_end_byte - uoff)
		sim->out_of_range++;
}

static size_t sim_clamp_eof(const struct sim_state *sim, ext2_loff_t offset,
			    size_t count)
{
	__u64 uoff = (__u64) offset;
	__u64 start_sec = uoff / SIM_SECTOR_SIZE;
	size_t sec_off = (size_t) (uoff % SIM_SECTOR_SIZE);
	__u64 rem_secs;

	if (start_sec > sim->size_sectors ||
	    (start_sec == sim->size_sectors && sec_off >= sim->tail_bytes))
		return 0;

	rem_secs = sim->size_sectors - start_sec;
	if (rem_secs < ((~0ULL - sim->tail_bytes) / SIM_SECTOR_SIZE)) {
		__u64 avail = rem_secs * SIM_SECTOR_SIZE + sim->tail_bytes -
			      sec_off;

		if ((__u64) count > avail)
			return (size_t) avail;
	}
	return count;
}

static ssize_t sim_pread(struct bb_dev *dev, void *buf, size_t count,
			 ext2_loff_t offset)
{
	struct sim_state *sim = dev->priv;
	unsigned char secbuf[SIM_SECTOR_SIZE];
	unsigned char *dst = buf;
	size_t todo, done = 0;
	__u64 start_sec, end_sec, s;
	size_t sec_off;

	sim->reads++;
	sim_check_bounds(sim, offset, count);
	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}
	if (sim_check_o_direct(sim, dev, buf, count, offset) < 0) {
		if (sim->trace)
			fprintf(stderr, "sim: R off=0x%llx len=%zu -> -1 (EINVAL)\n",
				(unsigned long long) offset, count);
		return -1;
	}

	todo = sim_clamp_eof(sim, offset, count);
	if (todo == 0) {
		if (sim->trace)
			fprintf(stderr, "sim: R off=0x%llx len=%zu -> 0\n",
				(unsigned long long) offset, count);
		return 0;
	}

	start_sec = (__u64) offset / SIM_SECTOR_SIZE;
	sec_off = (size_t) ((__u64) offset % SIM_SECTOR_SIZE);
	end_sec = ((__u64) offset + todo - 1ULL) / SIM_SECTOR_SIZE + 1ULL;
	extent_add(&sim->read_cov, start_sec, end_sec - start_sec);

	for (s = start_sec; s < end_sec; s++) {
		size_t chunk_off = (s == start_sec) ? sec_off : 0;
		size_t chunk_len = SIM_SECTOR_SIZE - chunk_off;

		if (chunk_len > todo - done)
			chunk_len = todo - done;

		if ((fault_flags(sim, s) & SIM_FAULT_RERR) ||
		    flaky_check_read(sim, s)) {
			if (done == 0 || sim->errmode == SIM_ERR_EIO) {
				if (sim->trace)
					fprintf(stderr,
						"sim: R off=0x%llx len=%zu -> -1 (EIO)\n",
						(unsigned long long) offset,
						count);
				errno = EIO;
				return -1;
			}
			break;
		}

		sim_read_sector_raw(sim, s, secbuf, 1);
		memcpy(dst + done, secbuf + chunk_off, chunk_len);
		done += chunk_len;
	}

	if (sim->trace)
		fprintf(stderr, "sim: R off=0x%llx len=%zu -> %zu\n",
			(unsigned long long) offset, count, done);
	return (ssize_t) done;
}

static ssize_t sim_pwrite(struct bb_dev *dev, const void *buf, size_t count,
			  ext2_loff_t offset)
{
	struct sim_state *sim = dev->priv;
	unsigned char secbuf[SIM_SECTOR_SIZE];
	const unsigned char *src = buf;
	size_t todo, done = 0;
	__u64 start_sec, end_sec, s;
	size_t sec_off;

	sim->writes++;
	sim_check_bounds(sim, offset, count);
	if (offset < 0) {
		errno = EINVAL;
		return -1;
	}
	if (sim_check_o_direct(sim, dev, buf, count, offset) < 0) {
		if (sim->trace)
			fprintf(stderr, "sim: W off=0x%llx len=%zu -> -1 (EINVAL)\n",
				(unsigned long long) offset, count);
		return -1;
	}

	todo = sim_clamp_eof(sim, offset, count);
	if (todo == 0) {
		if (sim->trace)
			fprintf(stderr, "sim: W off=0x%llx len=%zu -> 0\n",
				(unsigned long long) offset, count);
		return 0;
	}

	start_sec = (__u64) offset / SIM_SECTOR_SIZE;
	sec_off = (size_t) ((__u64) offset % SIM_SECTOR_SIZE);
	end_sec = ((__u64) offset + todo - 1ULL) / SIM_SECTOR_SIZE + 1ULL;

	for (s = start_sec; s < end_sec; s++) {
		size_t chunk_off = (s == start_sec) ? sec_off : 0;
		size_t chunk_len = SIM_SECTOR_SIZE - chunk_off;

		if (chunk_len > todo - done)
			chunk_len = todo - done;

		if (fault_flags(sim, s) & SIM_FAULT_WERR) {
			if (done == 0 || sim->errmode == SIM_ERR_EIO) {
				if (sim->trace)
					fprintf(stderr,
						"sim: W off=0x%llx len=%zu -> -1 (EIO)\n",
						(unsigned long long) offset,
						count);
				errno = EIO;
				return -1;
			}
			break;
		}

		if (chunk_off != 0 || chunk_len < SIM_SECTOR_SIZE) {
			sim_read_sector_raw(sim, s, secbuf, 0);
			memcpy(secbuf + chunk_off, src + done, chunk_len);
			sim_write_sector(sim, s, secbuf);
		} else {
			sim_write_sector(sim, s, src + done);
		}
		done += chunk_len;
	}

	if (sim->trace)
		fprintf(stderr, "sim: W off=0x%llx len=%zu -> %zu\n",
			(unsigned long long) offset, count, done);

	if (sim->sigint_after_writes &&
	    sim->writes == sim->sigint_after_writes) {
		sim->sigint_after_writes = 0;
		raise(SIGINT);
	}
	return (ssize_t) done;
}

static errcode_t sim_open(struct bb_dev *dev EXT2FS_ATTR((unused)),
			  int rw EXT2FS_ATTR((unused)))
{
	return 0;
}

static errcode_t sim_get_size(struct bb_dev *dev, unsigned int block_size,
			      blk64_t *blocks)
{
	const struct sim_state *sim = dev->priv;

	if (block_size >= SIM_SECTOR_SIZE) {
		__u64 secs_per_blk = block_size / SIM_SECTOR_SIZE;

		*blocks = (blk64_t) (sim->size_sectors / secs_per_blk);
	} else {
		__u64 blks_per_sec = SIM_SECTOR_SIZE / block_size;

		*blocks = (blk64_t) (sim->size_sectors * blks_per_sec +
				     sim->tail_bytes / block_size);
	}
	return 0;
}

static errcode_t sim_check_mounted(struct bb_dev *dev, int *mount_flags)
{
	const struct sim_state *sim = dev->priv;

	*mount_flags = 0;
	if (sim->mounted)
		*mount_flags |= EXT2_MF_MOUNTED;
	if (sim->busy)
		*mount_flags |= EXT2_MF_BUSY;
	return 0;
}

static void sim_set_direct(struct bb_dev *dev EXT2FS_ATTR((unused)),
			   int direct EXT2FS_ATTR((unused)))
{
}

static errcode_t sim_flush(struct bb_dev *dev)
{
	struct sim_state *sim = dev->priv;

	sim->flushes++;
	return 0;
}

static void sim_print_never_read(const struct sim_state *sim)
{
	__u64 s0 = sim->scan_start_byte / SIM_SECTOR_SIZE;
	__u64 s1 = (sim->scan_end_byte + SIM_SECTOR_SIZE - 1ULL) /
		   SIM_SECTOR_SIZE;
	__u64 dev_secs = sim->size_sectors + (sim->tail_bytes ? 1ULL : 0ULL);
	__u64 cur;
	int first = 1;
	size_t i;

	if (s1 > dev_secs)
		s1 = dev_secs;

	fputs("sim: never-read: ", stderr);
	cur = s0;
	for (i = 0; i < sim->read_cov.num && cur < s1; i++) {
		__u64 es = sim->read_cov.exts[i].start;
		__u64 ee = es + sim->read_cov.exts[i].len;

		if (ee <= cur)
			continue;
		if (es > cur) {
			__u64 gend = es < s1 ? es : s1;

			if (!first)
				fputc(',', stderr);
			first = 0;
			if (gend - 1ULL == cur)
				fprintf(stderr, "%llu",
					(unsigned long long) cur);
			else
				fprintf(stderr, "%llu-%llu",
					(unsigned long long) cur,
					(unsigned long long) (gend - 1ULL));
		}
		cur = ee;
	}
	if (cur < s1) {
		if (!first)
			fputc(',', stderr);
		first = 0;
		if (s1 - 1ULL == cur)
			fprintf(stderr, "%llu", (unsigned long long) cur);
		else
			fprintf(stderr, "%llu-%llu",
				(unsigned long long) cur,
				(unsigned long long) (s1 - 1ULL));
	}
	if (first)
		fputs("none", stderr);
	fputc('\n', stderr);
}

static void sim_report(const struct sim_state *sim)
{
	__u64 changed_clean = 0;
	__u64 changed_faulty = 0;
	size_t b;

	for (b = 0; b < SIM_HASH_BUCKETS; b++) {
		const struct sim_chunk *c;

		for (c = sim->buckets[b]; c != NULL; c = c->next) {
			unsigned int slot;

			for (slot = 0; slot < SIM_CHUNK_SECS; slot++) {
				__u64 sec;
				unsigned int fl;

				if (!(c->written_bmap[slot >> 3] &
				      (1U << (slot & 7U))))
					continue;
				sec = c->idx * SIM_CHUNK_SECS + slot;
				fl = fault_flags(sim, sec);
				if (fl & (SIM_FAULT_RERR | SIM_FAULT_WERR |
					  SIM_FAULT_CORRUPT))
					changed_faulty++;
				else
					changed_clean++;
			}
		}
	}

	fprintf(stderr,
		"sim: reads=%llu writes=%llu flushes=%llu "
		"direct-misaligned=%llu out-of-range=%llu\n",
		(unsigned long long) sim->reads,
		(unsigned long long) sim->writes,
		(unsigned long long) sim->flushes,
		(unsigned long long) sim->direct_misaligned,
		(unsigned long long) sim->out_of_range);
	sim_print_never_read(sim);

	fputs("sim: content: ", stderr);
	if (sim->writes == 0 && changed_clean == 0 && changed_faulty == 0) {
		fputs("unchanged", stderr);
	} else if (changed_clean == 0 && changed_faulty == 0) {
		fputs("restored", stderr);
	} else if (changed_clean == 0) {
		fprintf(stderr, "restored (excluding %llu faulty sectors)",
			(unsigned long long) changed_faulty);
	} else {
		fprintf(stderr, "modified (%llu sectors changed)",
			(unsigned long long) (changed_clean + changed_faulty));
	}
	fprintf(stderr, "; chunks-live=%llu peak-bytes=%llu\n",
		(unsigned long long) sim->chunks_live,
		(unsigned long long) (sim->peak_chunks * SIM_CHUNK_BYTES));
	fflush(stderr);
}

static void sim_free_state(struct sim_state *sim)
{
	size_t b;

	for (b = 0; b < SIM_HASH_BUCKETS; b++) {
		struct sim_chunk *c = sim->buckets[b];

		while (c) {
			struct sim_chunk *next = c->next;

			free(c);
			c = next;
		}
		sim->buckets[b] = NULL;
	}
	free(sim->faults);
	free(sim->flakies);
	free(sim->read_cov.exts);
	free(sim);
}

static void sim_finish(struct sim_state *sim)
{
	if (!sim)
		return;
	if (sim->scan_started && !sim->reported && !sim->quiet)
		sim_report(sim);
	sim->reported = 1;
	if (g_active_sim == sim)
		g_active_sim = NULL;
	sim_free_state(sim);
}

static void sim_atexit(void)
{
	if (g_active_sim)
		sim_finish(g_active_sim);
}

static void sim_close(struct bb_dev *dev)
{
	struct sim_state *sim = dev->priv;

	dev->priv = NULL;
	sim_finish(sim);
}

static const struct bb_dev_ops sim_dev_ops = {
	.open		= sim_open,
	.pread		= sim_pread,
	.pwrite		= sim_pwrite,
	.get_size	= sim_get_size,
	.check_mounted	= sim_check_mounted,
	.set_direct	= sim_set_direct,
	.flush		= sim_flush,
	.close		= sim_close,
};

static void sim_parse_fill(struct sim_state *sim, const char *val)
{
	if (strcmp(val, "zero") == 0) {
		sim->fillmode = SIM_FILL_ZERO;
	} else if (strcmp(val, "prng") == 0) {
		sim->fillmode = SIM_FILL_PRNG;
	} else if (strncmp(val, "byte:", 5) == 0) {
		__u64 bval;

		if (parse_u64_strict(val + 5, &bval) < 0 || bval > 0xffU)
			sim_die("invalid fill byte", val);
		sim->fillmode = SIM_FILL_BYTE;
		sim->fill_byte = (unsigned char) bval;
	} else {
		sim_die("invalid fill mode", val);
	}
}

static void sim_parse_spec(struct sim_state *sim, const char *spec)
{
	char *copy;
	char *p;
	int have_size = 0;

	if (strncmp(spec, "sim:", 4) != 0)
		sim_die("expected sim: prefix", spec);

	copy = strdup(spec + 4);
	if (!copy)
		sim_die("out of memory", NULL);

	p = copy;
	while (p && *p) {
		char *comma = strchr(p, ',');
		char *tok = p;
		char *eq;
		const char *val;

		if (comma) {
			*comma = '\0';
			p = comma + 1;
		} else {
			p = NULL;
		}
		if (!*tok)
			continue;

		eq = strchr(tok, '=');
		val = eq ? eq + 1 : NULL;
		if (eq)
			*eq = '\0';

		if (strcmp(tok, "size") == 0) {
			if (!val)
				sim_die("missing value for size", NULL);
			parse_size_sectors(val, &sim->size_sectors,
					   &sim->tail_bytes);
			have_size = 1;
		} else if (strcmp(tok, "rerr") == 0) {
			__u64 s0, s1;

			if (!val)
				sim_die("missing value for rerr", NULL);
			parse_sector_range(val, &s0, &s1);
			fault_add_range(sim, s0, s1, SIM_FAULT_RERR);
		} else if (strcmp(tok, "werr") == 0) {
			__u64 s0, s1;

			if (!val)
				sim_die("missing value for werr", NULL);
			parse_sector_range(val, &s0, &s1);
			fault_add_range(sim, s0, s1, SIM_FAULT_WERR);
		} else if (strcmp(tok, "corrupt") == 0) {
			__u64 s0, s1;

			if (!val)
				sim_die("missing value for corrupt", NULL);
			parse_sector_range(val, &s0, &s1);
			fault_add_range(sim, s0, s1, SIM_FAULT_CORRUPT);
		} else if (strcmp(tok, "flaky") == 0) {
			if (!val)
				sim_die("missing value for flaky", NULL);
			flaky_add_spec(sim, val);
		} else if (strcmp(tok, "errmode") == 0) {
			if (!val)
				sim_die("missing value for errmode", NULL);
			if (strcmp(val, "eio") == 0)
				sim->errmode = SIM_ERR_EIO;
			else if (strcmp(val, "short") == 0)
				sim->errmode = SIM_ERR_SHORT;
			else
				sim_die("invalid errmode", val);
		} else if (strcmp(tok, "fill") == 0) {
			if (!val)
				sim_die("missing value for fill", NULL);
			sim_parse_fill(sim, val);
		} else if (strcmp(tok, "fillseed") == 0) {
			if (!val || parse_u64_strict(val, &sim->fill_seed) < 0)
				sim_die("invalid fillseed", val);
		} else if (strcmp(tok, "sigint_after_writes") == 0) {
			if (!val ||
			    parse_u64_strict(val, &sim->sigint_after_writes) < 0)
				sim_die("invalid sigint_after_writes", val);
		} else if (strcmp(tok, "maxmem") == 0) {
			__u64 bytes;

			if (!val)
				sim_die("missing value for maxmem", NULL);
			bytes = parse_mem_bytes(val);
			sim->max_chunks = bytes / SIM_CHUNK_BYTES;
			if (sim->max_chunks == 0)
				sim_die("maxmem must be at least 64K", val);
		} else if (strcmp(tok, "pagesize") == 0) {
			__u64 ps = 0;

			if (!val || parse_u64_strict(val, &ps) < 0 ||
			    ps < SIM_SECTOR_SIZE || ps > (1ULL << 24) ||
			    (ps & (ps - 1ULL)))
				sim_die("invalid pagesize", val);
			sim->page_size = (unsigned int) ps;
		} else if (strcmp(tok, "mounted") == 0 && !val) {
			sim->mounted = 1;
		} else if (strcmp(tok, "busy") == 0 && !val) {
			sim->busy = 1;
		} else if (strcmp(tok, "trace") == 0 && !val) {
			sim->trace = 1;
		} else if (strcmp(tok, "quiet") == 0 && !val) {
			sim->quiet = 1;
		} else {
			sim_die("unknown spec key", tok);
		}
	}

	free(copy);
	if (!have_size)
		sim_die("spec requires size=", spec);
}

int bb_sim_is_sim_device(const char *name)
{
	return name && strncmp(name, "sim:", 4) == 0;
}

void bb_sim_setup(const char *spec, struct bb_dev *dev,
		  unsigned int *page_size_out)
{
	struct sim_state *sim;

	sim = calloc(1, sizeof(*sim));
	if (!sim)
		sim_die("out of memory", NULL);

	sim->page_size = 4096U;
	sim->errmode = SIM_ERR_EIO;
	sim->fillmode = SIM_FILL_PRNG;
	sim->max_chunks = SIM_DEFAULT_MAXMEM / SIM_CHUNK_BYTES;

	sim_parse_spec(sim, spec);

	if (page_size_out)
		*page_size_out = sim->page_size;

	memset(dev, 0, sizeof(*dev));
	dev->ops = &sim_dev_ops;
	dev->name = spec;
	dev->fd = -1;
	dev->priv = sim;

	g_active_sim = sim;
	if (!g_atexit_registered) {
		atexit(sim_atexit);
		g_atexit_registered = 1;
	}
}

void bb_sim_begin_scan(struct bb_dev *dev, unsigned int block_size,
		       blk64_t first_block, blk64_t last_block)
{
	struct sim_state *sim = dev->priv;

	sim->scan_started = 1;
	sim->scan_block_size = block_size;
	sim->scan_start_byte = (__u64) first_block * block_size;
	if ((__u64) last_block > (~0ULL / block_size))
		sim->scan_end_byte = ~0ULL;
	else
		sim->scan_end_byte = (__u64) last_block * block_size;
}

#endif /* BADBLOCKS_SIM */
